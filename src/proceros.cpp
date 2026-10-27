// Hydraulic and thermal erosion of a heightfield grid: see proceros.h.
#include "proceros.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>

namespace proc {

namespace {
const int kDA[8] = {1, -1, 0, 0, 1, 1, -1, -1};
const int kDB[8] = {0, 0, 1, -1, 1, -1, 1, -1};

// The cells from the highest down (ties: the lower index first). A stable LSD radix sort on the float's bits (ordered as unsigned),
// so the order is the one total order of (height, index): the same everywhere.
void sortDown(const std::vector<float>& h, std::vector<uint32_t>& order, std::vector<uint32_t>& key, std::vector<uint32_t>& tmpO,
              std::vector<uint32_t>& tmpK) {
    const size_t N = h.size();
    for (size_t i = 0; i < N; i++) {
        uint32_t u;
        std::memcpy(&u, &h[i], 4);
        u = (u & 0x80000000u) ? ~u : (u | 0x80000000u);  // (ascending as unsigned)
        key[i] = ~u;                                      // (descending)
        order[i] = (uint32_t)i;
    }
    uint32_t count[2048];
    for (int pass = 0; pass < 3; pass++) {
        const int sh = pass * 11;
        std::memset(count, 0, sizeof count);
        for (size_t i = 0; i < N; i++) count[(key[i] >> sh) & 2047]++;
        uint32_t sum = 0;
        for (int k = 0; k < 2048; k++) {
            const uint32_t c = count[k];
            count[k] = sum, sum += c;
        }
        for (size_t i = 0; i < N; i++) {
            const uint32_t d = count[(key[i] >> sh) & 2047]++;
            tmpK[d] = key[i], tmpO[d] = order[i];
        }
        key.swap(tmpK), order.swap(tmpO);
    }
}
}  // namespace

namespace {
// One resolution's iterations (see proceros.h). The grid's edge cells are fixed: water and load reaching them leave the grid.
void erodeLevel(ErosionGrid& g, const ErosionRun& run, int iterations, std::vector<float>* areaOut) {
    const int n = g.n;
    const size_t N = (size_t)n * n;
    const double c = g.cell, cA = c * c;
    const double r2 = 1.4142135623730951;
    const double kInv[8] = {1 / c, 1 / c, 1 / c, 1 / c, 1 / (c * r2), 1 / (c * r2), 1 / (c * r2), 1 / (c * r2)};
    const double kDist[8] = {c, c, c, c, c * r2, c * r2, c * r2, c * r2};
    const ptrdiff_t off[8] = {n, -n, 1, -1, n + 1, n - 1, -n + 1, -n - 1};
    std::vector<uint32_t> order(N), key(N), tmpO(N), tmpK(N);
    std::vector<float> A(N), Q(N), dh(N);
    std::vector<uint8_t> edge(N, 0);
    for (int a = 0; a < n; a++)
        for (int b = 0; b < n; b++)
            if (a == 0 || b == 0 || a == n - 1 || b == n - 1) edge[(size_t)a * n + b] = 1;
    std::vector<float>& h = g.h;
    float* H = h.data();
    // (the lowest each cell may be worn to: its floor, or where it starts if lower)
    std::vector<float> lo(N), up(N);
    for (size_t i = 0; i < N; i++) lo[i] = g.floor.empty() ? -1e30f : std::min(H[i], g.floor[i]), up[i] = g.ceil.empty() ? 1e30f : std::max(H[i], g.ceil[i]);
    for (int it = 0; it < iterations; it++) {
        // ---- 1-2. Water, its load, the cut and the deposit: from the highest cell down ----
        sortDown(h, order, key, tmpO, tmpK);
        const float rain = (float)(run.rain * cA);
        for (size_t i = 0; i < N; i++) A[i] = rain, Q[i] = 0;
        for (size_t k = 0; k < N; k++) {
            const uint32_t i = order[k];
            if (edge[i]) continue;  // (off the grid)
            const double hi = H[i];
            double w[8], wsum = 0, smax = 0, hlow = hi, hup = 1e30, hmin = 1e30;
            for (int d = 0; d < 8; d++) {
                const double hj = H[i + off[d]];
                hmin = hj < hmin ? hj : hmin;
                if (hj < hi) {
                    const double s = (hi - hj) * kInv[d];
                    w[d] = s * s, wsum += w[d];
                    if (s > smax) smax = s;
                    if (hj < hlow) hlow = hj;
                } else {
                    w[d] = 0;
                    if (hj > hi && hj < hup) hup = hj;
                }
            }
            const double area = A[i], qin = Q[i];
            if (wsum <= 0) {  // (a closed hollow: its load silts it up, to just over its lowest neighbour so it drains next time)
                if (!g.fixed[i]) {
                    const double room = hmin - hi + 1e-3;
                    if (room > 0) H[i] = (float)std::min((double)up[i], hi + std::min(run.pitFill * qin / cA, room));
                }
                continue;
            }
            double out = g.fixed[i] == 2 ? 0.0 : qin;  // (standing water: what's carried settles in it, out of sight)
            if (!g.fixed[i]) {
                double e = g.kErode[i] * std::sqrt(area * 1e-4) * smax;
                if (e > run.maxCut) e = run.maxCut;
                double dep = g.gDep[i] * qin / area;
                if (dep * cA > qin) dep = qin / cA;
                double net = e - dep;  // (how much lower it goes)
                if (net > 0.9 * (hi - hlow)) net = 0.9 * (hi - hlow);  // (never cut under its lowest receiver: no pits dug)
                if (net < 0 && hup < 1e29 && -net > (hup - hi) + run.maxCut * 0.1) net = -((hup - hi) + run.maxCut * 0.1);  // (never piled far over the ground above it)
                if (hi - net < lo[i]) net = hi - lo[i] > 0 ? hi - lo[i] : 0;
                if (hi - net > up[i]) net = hi - up[i] < 0 ? hi - up[i] : 0;
                H[i] = (float)(hi - net);
                out = qin + net * cA;
                if (out < 0) out = 0;
            }
            const double fa = area / wsum, fq = out / wsum;
            for (int d = 0; d < 8; d++)
                if (w[d] > 0) A[i + off[d]] += (float)(fa * w[d]), Q[i + off[d]] += (float)(fq * w[d]);
        }
        // ---- 3. Talus: what's steeper than its repose slides down (Jacobi: every cell from the same heights) ----
        for (int p = 0; p < run.talusPasses; p++) {
            std::fill(dh.begin(), dh.end(), 0.0f);
            for (int a = 1; a + 1 < n; a++)
                for (int b = 1; b + 1 < n; b++) {
                    const size_t i = (size_t)a * n + b;
                    if (g.fixed[i]) continue;
                    const double hi = H[i], tal = g.talus[i];
                    double ex[8], sum = 0, mx = 0;
                    for (int d = 0; d < 8; d++) {
                        const double e = hi - H[i + off[d]] - tal * kDist[d];
                        ex[d] = e > 0 ? e : 0;
                        sum += ex[d], mx = ex[d] > mx ? ex[d] : mx;
                    }
                    if (sum <= 0) continue;
                    const double move = run.talusRate * mx * 0.5;
                    dh[i] -= (float)move;
                    const double f = move / sum;
                    for (int d = 0; d < 8; d++)
                        if (ex[d] > 0) dh[i + off[d]] += (float)(f * ex[d]);
                }
            for (size_t i = 0; i < N; i++)
                if (!g.fixed[i]) H[i] = std::min(up[i], std::max(lo[i], H[i] + dh[i]));
        }
        // ---- Soil creep: a little of the Laplacian (rounds the soft land; the rock keeps its edges) ----
        for (int a = 1; a + 1 < n; a++)
            for (int b = 1; b + 1 < n; b++) {
                const size_t i = (size_t)a * n + b;
                const double lap = 0.25 * ((double)H[i - n] + H[i + n] + H[i - 1] + H[i + 1]) - H[i];
                dh[i] = g.fixed[i] ? 0.0f : (float)(g.creep[i] * lap);
            }
        for (int a = 1; a + 1 < n; a++)
            for (int b = 1; b + 1 < n; b++) H[(size_t)a * n + b] = std::min(up[(size_t)a * n + b], std::max(lo[(size_t)a * n + b], H[(size_t)a * n + b] + dh[(size_t)a * n + b]));
    }
    if (areaOut) areaOut->assign(A.begin(), A.end());
}

// The ice (see ErosionRun): its flow lines are the land's fall lines as it was (hollows filled, so ice flows over their lips), fixed
// for the whole run; each iteration the ice gathers down them, wears its bed and drops its load where it melts out.
void glaciate(ErosionGrid& g, const ErosionRun& run) {
    const int n = g.n;
    const size_t N = (size_t)n * n;
    const double c = g.cell, cA = c * c;
    const ptrdiff_t off[8] = {n, -n, 1, -1, n + 1, n - 1, -n + 1, -n - 1};
    const double r2 = 1.4142135623730951;
    const double kDist[8] = {c, c, c, c, c * r2, c * r2, c * r2, c * r2};
    bool any = false;
    for (size_t i = 0; i < N && !any; i++) any = g.h[i] > run.glacierLine;
    if (!any) return;
    std::vector<float> F;
    fillDepressions(n, g.h, F);
    // (on the filled land, flats tilt toward their spill: a hair added a cell further from it, by a flood from the edges)
    std::vector<int32_t> rcv(N, -1);
    {
        std::vector<uint8_t> done(N, 0);
        using PE = std::pair<double, uint32_t>;
        std::priority_queue<PE, std::vector<PE>, std::greater<PE>> pq;
        std::vector<double> S(F.begin(), F.end());
        for (int a = 0; a < n; a++)
            for (int b = 0; b < n; b++)
                if (a == 0 || b == 0 || a == n - 1 || b == n - 1) done[(size_t)a * n + b] = 1, pq.push({S[(size_t)a * n + b], (uint32_t)(a * n + b)});
        while (!pq.empty()) {
            const PE e = pq.top();
            pq.pop();
            const int a = (int)(e.second / (uint32_t)n), b = (int)(e.second % (uint32_t)n);
            for (int d = 0; d < 8; d++) {
                const int na = a + kDA[d], nb = b + kDB[d];
                if (na < 0 || na >= n || nb < 0 || nb >= n) continue;
                const uint32_t j = (uint32_t)(na * n + nb);
                if (done[j]) continue;
                done[j] = 1;
                if (S[j] <= e.first) S[j] = e.first + 1e-4;
                rcv[j] = (int32_t)e.second;  // (drains to the cell it was reached from: down its fall line, or over a hollow's lip)
                pq.push({S[j], j});
            }
        }
        // (Not in a hollow: the steepest way down rather than the flood's.)
        for (int a = 1; a + 1 < n; a++)
            for (int b = 1; b + 1 < n; b++) {
                const size_t i = (size_t)a * n + b;
                double best = 0;
                int bd = -1;
                for (int d = 0; d < 8; d++) {
                    const double s = (S[i] - S[i + off[d]]) / kDist[d];
                    if (s > best) best = s, bd = d;
                }
                if (bd >= 0) rcv[i] = (int32_t)(i + off[bd]);
            }
        // (the order: highest first on the tilted surface, so every cell comes before the one it drains to)
        std::vector<uint32_t> ord(N);
        for (size_t i = 0; i < N; i++) ord[i] = (uint32_t)i;
        std::sort(ord.begin(), ord.end(), [&](uint32_t x, uint32_t y) { return S[x] > S[y] || (S[x] == S[y] && x < y); });
        F.assign(N, 0.0f);
        const std::vector<float> h0 = g.h;
        std::vector<double> Q(N), L(N), E(N), Dm(N);
        std::vector<float> tmp(N);
        for (int it = 0; it < run.glacierIterations; it++) {
            std::fill(Q.begin(), Q.end(), 0.0), std::fill(L.begin(), L.end(), 0.0), std::fill(E.begin(), E.end(), 0.0), std::fill(Dm.begin(), Dm.end(), 0.0);
            for (uint32_t i : ord) {
                const double mb = (h0[i] - run.glacierLine) * 0.01 * cA;  // (m3 a year: gathered above the line, melted below)
                double q = Q[i] + mb;
                if (q <= 0) {  // (melted out here: what it carried drops, a moraine)
                    if (L[i] > 0) {
                        const double dz = std::max(0.0, std::min({L[i] / cA, 3.0, 25.0 - (g.h[i] - h0[i])}));
                        Dm[i] += dz;
                        const double rest = L[i] - dz * cA;
                        if (rcv[i] >= 0 && rest > 0) L[rcv[i]] += rest;
                    }
                    continue;
                }
                double e = run.glacierCut * std::sqrt(q);
                if (e > 2.0) e = 2.0;
                if (h0[i] - g.h[i] + e > run.glacierMax) e = std::max(0.0, run.glacierMax - (h0[i] - g.h[i]));
                E[i] -= e;
                if (rcv[i] >= 0) Q[rcv[i]] += q, L[rcv[i]] += L[i] + e * cA;
            }
            // (Ice is wide: its wear spread over a few cells each way, a U-shaped trough not a slot.)
            for (int pass = 0; pass < 3; pass++) {
                for (int a = 1; a + 1 < n; a++)
                    for (int b = 1; b + 1 < n; b++) {
                        const size_t i = (size_t)a * n + b;
                        double sum = 4 * E[i];
                        for (int d = 0; d < 4; d++) sum += E[i + off[d]];
                        for (int d = 4; d < 8; d++) sum += 0.5 * E[i + off[d]];
                        tmp[i] = (float)(sum / 10);
                    }
                for (int a = 1; a + 1 < n; a++)
                    for (int b = 1; b + 1 < n; b++) E[(size_t)a * n + b] = tmp[(size_t)a * n + b];
            }
            // (the moraines: a little spread, a ridge round the ice's snout)
            for (int a = 1; a + 1 < n; a++)
                for (int b = 1; b + 1 < n; b++) {
                    const size_t i = (size_t)a * n + b;
                    double sum = 2 * Dm[i];
                    for (int d = 0; d < 8; d++) sum += 0.5 * Dm[i + off[d]];
                    tmp[i] = (float)(sum / 3);
                }
            for (int a = 1; a + 1 < n; a++)
                for (int b = 1; b + 1 < n; b++) E[(size_t)a * n + b] += tmp[(size_t)a * n + b];
            for (size_t i = 0; i < N; i++) g.h[i] += (float)E[i];
        }
    }
}
}  // namespace

void erodeGrid(ErosionGrid& g, const ErosionRun& run, std::vector<float>* areaOut) {
    const int n = g.n;
    if (run.maxFill < 1e9) {  // (silt never piles more than maxFill over the land: no fan buries a valley or a dam)
        g.ceil.resize(g.h.size());
        for (size_t i = 0; i < g.h.size(); i++) g.ceil[i] = g.h[i] + (float)run.maxFill;
    }
    if (run.coarseIterations > 0 && n % 2 == 1 && n >= 9) {
        // ---- Coarse first (twice the spacing: a quarter of the cells, the broad gullies and spurs), its change laid on the fine grid ----
        const int m = (n + 1) / 2;
        ErosionGrid c;
        c.n = m, c.cell = g.cell * 2;
        const size_t M = (size_t)m * m;
        c.h.resize(M), c.kErode.resize(M), c.gDep.resize(M), c.talus.resize(M), c.creep.resize(M), c.fixed.assign(M, 0);
        if (!g.floor.empty()) c.floor.resize(M);
        if (!g.ceil.empty()) c.ceil.resize(M);
        for (int a = 0; a < m; a++)
            for (int b = 0; b < m; b++) {
                const size_t i = (size_t)a * m + b, f = (size_t)(2 * a) * n + 2 * b;
                c.h[i] = g.h[f], c.kErode[i] = g.kErode[f], c.gDep[i] = g.gDep[f], c.talus[i] = g.talus[f], c.creep[i] = g.creep[f];
                if (!g.floor.empty()) c.floor[i] = g.floor[f];
                if (!g.ceil.empty()) c.ceil[i] = g.ceil[f];
                uint8_t fx = 0;  // (fixed if any fine cell round it is: a narrow channel isn't lost between the samples)
                for (int da = -1; da <= 1; da++)
                    for (int db = -1; db <= 1; db++) {
                        const int fa = 2 * a + da, fb = 2 * b + db;
                        if (fa >= 0 && fa < n && fb >= 0 && fb < n) fx = std::max(fx, g.fixed[(size_t)fa * n + fb]);
                    }
                c.fixed[i] = fx;
            }
        const std::vector<float> c0 = c.h;
        if (run.glacierIterations > 0) glaciate(c, run);
        erodeLevel(c, run, run.coarseIterations, nullptr);
        for (int a = 0; a < n; a++)
            for (int b = 0; b < n; b++) {
                const size_t f = (size_t)a * n + b;
                if (g.fixed[f]) continue;
                const int a0 = a >> 1, b0 = b >> 1, a1 = std::min(m - 1, a0 + (a & 1)), b1 = std::min(m - 1, b0 + (b & 1));
                const double d00 = c.h[(size_t)a0 * m + b0] - c0[(size_t)a0 * m + b0], d10 = c.h[(size_t)a1 * m + b0] - c0[(size_t)a1 * m + b0];
                const double d01 = c.h[(size_t)a0 * m + b1] - c0[(size_t)a0 * m + b1], d11 = c.h[(size_t)a1 * m + b1] - c0[(size_t)a1 * m + b1];
                const float v = g.h[f] + (float)(0.25 * (d00 + d10 + d01 + d11));
                g.h[f] = g.floor.empty() ? v : std::max(v, std::min(g.h[f], g.floor[f]));
                if (!g.ceil.empty()) g.h[f] = std::min(g.h[f], std::max(g.ceil[f], g.h[f] - (float)(0.25 * (d00 + d10 + d01 + d11))));
            }
    }
    erodeLevel(g, run, run.iterations, areaOut);
}

int findLakes(int n, double cell, const std::vector<float>& h, std::vector<float>& level, double minArea, double minDepth, std::vector<LakeInfo>* info) {
    if (info) info->clear();
    const size_t N = (size_t)n * n;
    std::vector<float> F;
    fillDepressions(n, h, F);
    level.assign(N, -1e30f);
    std::vector<uint8_t> seen(N, 0);
    std::vector<size_t> cells, st;
    int lakes = 0;
    for (size_t s0 = 0; s0 < N; s0++) {
        if (seen[s0] || F[s0] - h[s0] < 0.02f) continue;
        cells.clear(), st.assign(1, s0), seen[s0] = 1;
        double deep = 0;
        while (!st.empty()) {
            const size_t i = st.back();
            st.pop_back();
            cells.push_back(i), deep = std::max(deep, (double)(F[i] - h[i]));
            const int a = (int)(i / n), b = (int)(i % n);
            for (int d = 0; d < 8; d++) {
                const int na = a + kDA[d], nb = b + kDB[d];
                if (na < 0 || na >= n || nb < 0 || nb >= n) continue;
                const size_t j = (size_t)na * n + nb;
                if (!seen[j] && F[j] - h[j] >= 0.02f && F[j] == F[i]) seen[j] = 1, st.push_back(j);
            }
        }
        if ((double)cells.size() * cell * cell < minArea || deep < minDepth) continue;
        lakes++;
        for (size_t i : cells) level[i] = F[i];
        if (info) {
            LakeInfo L;
            L.level = F[s0], L.area = (double)cells.size() * cell * cell, L.depth = deep;
            L.a0 = L.b0 = n, L.a1 = L.b1 = -1;
            double sa = 0, sb = 0, best = 1e30;
            for (size_t i : cells) {
                const int a = (int)(i / n), b = (int)(i % n);
                sa += a, sb += b;
                L.a0 = std::min(L.a0, a), L.a1 = std::max(L.a1, a), L.b0 = std::min(L.b0, b), L.b1 = std::max(L.b1, b);
                for (int d = 0; d < 8; d++) {
                    const int na = a + kDA[d], nb = b + kDB[d];
                    if (na < 0 || na >= n || nb < 0 || nb >= n) continue;
                    const size_t j = (size_t)na * n + nb;
                    if (level[j] > -1e29f) continue;
                    const double v = h[j];
                    if (v < best || (v == best && j < (size_t)L.la * n + L.lb)) best = v, L.la = na, L.lb = nb;
                }
            }
            L.ca = sa / cells.size(), L.cb = sb / cells.size();
            info->push_back(L);
        }
    }
    return lakes;
}

void fillDepressions(int n, const std::vector<float>& h, std::vector<float>& filled) {
    const size_t N = (size_t)n * n;
    filled.assign(h.begin(), h.end());
    std::vector<uint8_t> done(N, 0);
    using E = std::pair<float, uint32_t>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
    for (int a = 0; a < n; a++)
        for (int b = 0; b < n; b++)
            if (a == 0 || b == 0 || a == n - 1 || b == n - 1) {
                const uint32_t i = (uint32_t)(a * n + b);
                done[i] = 1, pq.push({filled[i], i});
            }
    while (!pq.empty()) {
        const E e = pq.top();
        pq.pop();
        const int a = (int)(e.second / (uint32_t)n), b = (int)(e.second % (uint32_t)n);
        for (int d = 0; d < 8; d++) {
            const int na = a + kDA[d], nb = b + kDB[d];
            if (na < 0 || na >= n || nb < 0 || nb >= n) continue;
            const uint32_t j = (uint32_t)(na * n + nb);
            if (done[j]) continue;
            done[j] = 1;
            if (filled[j] < e.first) filled[j] = e.first;
            pq.push({filled[j], j});
        }
    }
}

}  // namespace proc
