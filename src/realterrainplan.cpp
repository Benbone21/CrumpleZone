// Real-world terrain, the plan (realterrain.h): on a coarse grid over the map, with no World -- the drainage (priority flood,
// flow accumulation), lakes in the closed hollows, rivers and dry washes along the drainage lines, outposts on flat ground near
// water, lookouts and summits on the tops, roads and trails between them by A* over a slope cost.
// (Plain arithmetic throughout, and every tie broken by a cell's index: the same plan from the same file everywhere.)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <string>

#include "realterrain.h"
#include "world.h"

namespace realterrain {

double Terrain::Plan::cx(int i) const { return -half + i * cs; }
int Terrain::Plan::cellOf(double v) const {
    const int i = (int)std::floor((v + half) / cs + 0.5);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

std::vector<V3> smoothLine(const std::vector<V3>& in, double step) {
    std::vector<V3> p = in;
    for (int pass = 0; pass < 3 && p.size() > 2; pass++) {
        std::vector<V3> q;
        q.push_back(p.front());
        for (size_t k = 0; k + 1 < p.size(); k++) {
            q.push_back(p[k] * 0.75 + p[k + 1] * 0.25);
            q.push_back(p[k] * 0.25 + p[k + 1] * 0.75);
        }
        q.push_back(p.back());
        p.swap(q);
    }
    std::vector<V3> out;
    if (p.empty()) return out;
    out.push_back(p[0]);
    double carry = 0;
    for (size_t k = 0; k + 1 < p.size(); k++) {
        const V3 a = p[k], b = p[k + 1];
        const double L = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
        double s = step - carry;
        while (s <= L) {
            out.push_back(a + (b - a) * (s / L));
            s += step;
        }
        carry = L - (s - step);
    }
    if (out.size() < 2 || std::sqrt((out.back().x - p.back().x) * (out.back().x - p.back().x) + (out.back().z - p.back().z) * (out.back().z - p.back().z)) > 0.01)
        out.push_back(p.back());
    return out;
}

namespace {
constexpr double kSqrt2 = 1.4142135623730951;
const int kDI[8] = {1, -1, 0, 0, 1, 1, -1, -1}, kDJ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr double kSqrt5 = 2.2360679774997898;
const int kMI[16] = {1, -1, 0, 0, 1, 1, -1, -1, 2, 2, -2, -2, 1, 1, -1, -1}, kMJ[16] = {0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 2, -2, 2, -2};

struct QItem {
    double key;
    uint32_t seq;
    int id;
    bool operator>(const QItem& o) const { return key != o.key ? key > o.key : seq > o.seq; }
};
using MinQ = std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>>;

uint32_t mix32(uint32_t h) {
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
    return h;
}
double roll(uint32_t a, uint32_t b) { return (mix32(a * 0x9e3779b9u ^ mix32(b + 0x85ebca6bu)) & 0xffffff) / double(0x1000000); }

// "NORTH", "SOUTHEAST"... of (x, z) from the middle (z north), "CENTRAL" near it.
std::string dirWord(double x, double z, double half) {
    if (x * x + z * z < 0.04 * half * half) return "CENTRAL";
    const double ax = std::fabs(x), az = std::fabs(z);
    if (az > 2.414 * ax) return z > 0 ? "NORTH" : "SOUTH";
    if (ax > 2.414 * az) return x > 0 ? "EAST" : "WEST";
    return std::string(z > 0 ? "NORTH" : "SOUTH") + (x > 0 ? "EAST" : "WEST");
}

// Two-pass chamfer distance (m) from the cells `src` marks.
std::vector<float> chamfer(int n, double cs, const std::vector<uint8_t>& src) {
    std::vector<float> d((size_t)n * n, 1e9f);
    for (size_t i = 0; i < d.size(); i++)
        if (src[i]) d[i] = 0;
    const float a = (float)cs, b = (float)(cs * kSqrt2);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            float& v = d[(size_t)i * n + j];
            if (i > 0) v = std::min(v, d[(size_t)(i - 1) * n + j] + a);
            if (j > 0) v = std::min(v, d[(size_t)i * n + j - 1] + a);
            if (i > 0 && j > 0) v = std::min(v, d[(size_t)(i - 1) * n + j - 1] + b);
            if (i > 0 && j + 1 < n) v = std::min(v, d[(size_t)(i - 1) * n + j + 1] + b);
        }
    for (int i = n - 1; i >= 0; i--)
        for (int j = n - 1; j >= 0; j--) {
            float& v = d[(size_t)i * n + j];
            if (i + 1 < n) v = std::min(v, d[(size_t)(i + 1) * n + j] + a);
            if (j + 1 < n) v = std::min(v, d[(size_t)i * n + j + 1] + a);
            if (i + 1 < n && j + 1 < n) v = std::min(v, d[(size_t)(i + 1) * n + j + 1] + b);
            if (i + 1 < n && j > 0) v = std::min(v, d[(size_t)(i + 1) * n + j - 1] + b);
        }
    return d;
}

// The max (or min) of `v` within r cells (a square), by rows then columns.
std::vector<float> windowExtreme(int n, const std::vector<float>& v, int r, bool max) {
    std::vector<float> a(v.size()), b(v.size());
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            float m = v[(size_t)i * n + j];
            for (int k = std::max(0, j - r); k <= std::min(n - 1, j + r); k++) m = max ? std::max(m, v[(size_t)i * n + k]) : std::min(m, v[(size_t)i * n + k]);
            a[(size_t)i * n + j] = m;
        }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            float m = a[(size_t)i * n + j];
            for (int k = std::max(0, i - r); k <= std::min(n - 1, i + r); k++) m = max ? std::max(m, a[(size_t)k * n + j]) : std::min(m, a[(size_t)k * n + j]);
            b[(size_t)i * n + j] = m;
        }
    return b;
}
}  // namespace

const Terrain::Plan& Terrain::plan() const {
    std::call_once(planOnce_, [this] {
        auto P = std::make_unique<Plan>();
        Plan& p = *P;
        p.half = half;
        p.cs = std::max(4.0, std::ceil(2 * half / 1024));
        p.n = (int)std::floor(2 * half / p.cs) + 1;
        const int n = p.n;
        const double cs = p.cs;
        const size_t N = (size_t)n * n;
        auto X = [&](int i) { return p.cx(i); };
        p.h.resize(N);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) p.h[(size_t)i * n + j] = (float)demAt(X(i), X(j));
        const std::vector<float>& H = p.h;
        const double seaY = spec.seaLevel > -1e8 ? mapHeight(spec.seaLevel) : -1e9;
        auto cellAt = [&](const LatLon& ll, int& id) {
            double x, z;
            if (!toMap(ll, x, z)) return false;
            id = p.cellOf(x) * n + p.cellOf(z);
            return true;
        };

        // ---- The sidecar's big rivers (SpecRiver): each leg between its points along the lowest ground (the DEM's own water
        // surface is the lowest thing in a valley), a corridor of its half-width either side; and the bridges over them.
        std::vector<uint8_t> big(N, 0), deck(N, 0);
        std::vector<float> HA = H;  // (the land as the roads see it: over a bridge, its deck)
        for (const SpecBridge& sb : spec.bridges) {
            PlanBridge b;
            b.name = sb.name, b.halfWidth = sb.halfWidth;
            double ax, az, bx, bz;
            if (!toMap(sb.a, ax, az) || !toMap(sb.b, bx, bz)) continue;
            auto bank = [&](double x, double z) {  // (the bank's height there: the highest of the land a few cells round)
                const int ci = p.cellOf(x), cj = p.cellOf(z);
                double hi = -1e9;
                for (int a = -1; a <= 1; a++)
                    for (int c = -1; c <= 1; c++) hi = std::max(hi, (double)H[(size_t)std::min(n - 1, std::max(0, ci + a)) * n + std::min(n - 1, std::max(0, cj + c))]);
                return hi;
            };
            b.a = V3(ax, bank(ax, az), az), b.b = V3(bx, bank(bx, bz), bz);
            const double L = std::sqrt((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
            for (double u = 0; u <= L; u += cs * 0.5)
                for (double v = -b.halfWidth; v <= b.halfWidth; v += cs * 0.5) {
                    const double x = ax + (bx - ax) * u / L - (bz - az) / L * v, z = az + (bz - az) * u / L + (bx - ax) / L * v;
                    const int id = p.cellOf(x) * n + p.cellOf(z);
                    deck[id] = 1, HA[id] = (float)(b.a.y + (b.b.y - b.a.y) * u / L);
                }
            p.bridges.push_back(b);
        }
        for (const SpecRiver& sr : spec.rivers) {
            std::vector<int> way;
            for (const LatLon& ll : sr.pts) {
                int id;
                if (!cellAt(ll, id)) continue;
                // (onto the lowest cell within 150 m: the water)
                const int ci = id / n, cj = id % n, r = (int)std::ceil(150 / cs);
                int best = id;
                for (int a = -r; a <= r; a++)
                    for (int c = -r; c <= r; c++) {
                        const int ni = ci + a, nj = cj + c;
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n || (a * a + c * c) * cs * cs > 150.0 * 150) continue;
                        if (H[(size_t)ni * n + nj] < H[best]) best = ni * n + nj;
                    }
                way.push_back(best);
            }
            PlanRiver r;
            r.name = sr.name, r.halfWidth = sr.halfWidth, r.depth = sr.depth;
            for (size_t w = 0; w + 1 < way.size(); w++) {
                // Dijkstra from one to the next, a step dearer the higher it is over the lower end's water.
                const double ref = std::min(H[way[w]], H[way[w + 1]]);
                std::vector<double> g(N, 1e30);
                std::vector<int> came(N, -1);
                MinQ q;
                uint32_t seq = 0;
                g[way[w]] = 0, q.push({0, seq++, way[w]});
                while (!q.empty()) {
                    const QItem c = q.top();
                    q.pop();
                    if (c.key > g[c.id]) continue;
                    if (c.id == way[w + 1]) break;
                    for (int k = 0; k < 8; k++) {
                        const int ni = c.id / n + kDI[k], nj = c.id % n + kDJ[k];
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                        const int nb = ni * n + nj;
                        const double L = k < 4 ? cs : cs * kSqrt2, up = std::max(0.0, (double)H[nb] - ref);
                        const double ng = g[c.id] + L * (1 + 3 * up + 0.5 * up * up);
                        if (ng < g[nb]) g[nb] = ng, came[nb] = c.id, q.push({ng, seq++, nb});
                    }
                }
                std::vector<int> leg;
                for (int c = way[w + 1]; c >= 0 && c != way[w]; c = came[c]) leg.push_back(c);
                leg.push_back(way[w]);
                std::reverse(leg.begin(), leg.end());
                for (size_t k = r.pts.empty() ? 0 : 1; k < leg.size(); k++) {
                    r.pts.push_back(V3(X(leg[k] / n), H[leg[k]], X(leg[k] % n)));
                    r.area.push_back(1e4f);
                }
            }
            if (r.pts.size() < 3) continue;
            // Its corridor: the cells within its half-width (and a cell) of the line; under a bridge's deck, passable.
            const int rr = (int)std::ceil((sr.halfWidth + cs) / cs);
            for (size_t k = 0; k < r.pts.size(); k++) {
                const int ci = p.cellOf(r.pts[k].x), cj = p.cellOf(r.pts[k].z);
                for (int a = -rr; a <= rr; a++)
                    for (int c = -rr; c <= rr; c++) {
                        const int ni = ci + a, nj = cj + c;
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n || (a * a + c * c) * cs * cs > (sr.halfWidth + cs) * (sr.halfWidth + cs)) continue;
                        big[(size_t)ni * n + nj] = 1;
                    }
            }
            p.rivers.push_back(std::move(r));
        }
        const size_t bigRivers = p.rivers.size();

        // ---- Drainage: a priority flood from the map's edge (and the sea) inward. Each cell drains to the one it was reached
        // from, so every hollow spills out over its lowest rim, and the flats drain toward their outlets.
        std::vector<float> filled = H;
        std::vector<int> parent(N, -1);
        std::vector<uint8_t> closed(N, 0);
        std::vector<int> order;
        order.reserve(N);
        p.lake.assign(N, -1);
        {
            MinQ q;
            uint32_t seq = 0;
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++) {
                    const size_t id = (size_t)i * n + j;
                    const bool sea = H[id] <= seaY;
                    if (sea) p.lake[id] = -2;
                    if (i == 0 || j == 0 || i == n - 1 || j == n - 1 || sea) closed[id] = 1, q.push({H[id], seq++, (int)id});
                }
            while (!q.empty()) {
                const QItem c = q.top();
                q.pop();
                order.push_back(c.id);
                const int ci = c.id / n, cj = c.id % n;
                for (int k = 0; k < 8; k++) {
                    const int ni = ci + kDI[k], nj = cj + kDJ[k];
                    if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                    const size_t nb = (size_t)ni * n + nj;
                    if (closed[nb]) continue;
                    closed[nb] = 1;
                    filled[nb] = std::max(H[nb], filled[c.id]);
                    parent[nb] = c.id;
                    q.push({filled[nb], seq++, (int)nb});
                }
            }
        }
        std::vector<double> acc(N, cs * cs);
        for (size_t k = order.size(); k-- > 0;)
            if (parent[order[k]] >= 0) acc[parent[order[k]]] += acc[order[k]];
        p.acc.resize(N);
        for (size_t i = 0; i < N; i++) p.acc[i] = (float)(acc[i] * 1e-6);

        // ---- Lakes: the hollows the flood filled, where they're deep and big enough.
        {
            std::vector<uint8_t> seen(N, 0);
            for (size_t s = 0; s < N; s++) {
                if (seen[s] || filled[s] - H[s] < 0.25f || p.lake[s] != -1 || big[s]) continue;
                std::vector<int> comp = {(int)s}, stack = {(int)s};
                seen[s] = 1;
                const float lvl = filled[s];
                while (!stack.empty()) {
                    const int c = stack.back();
                    stack.pop_back();
                    for (int k = 0; k < 8; k++) {
                        const int ni = c / n + kDI[k], nj = c % n + kDJ[k];
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                        const size_t nb = (size_t)ni * n + nj;
                        if (seen[nb] || filled[nb] - H[nb] < 0.25f || std::fabs(filled[nb] - lvl) > 1e-3f || big[nb]) continue;
                        seen[nb] = 1, comp.push_back((int)nb), stack.push_back((int)nb);
                    }
                }
                const double area = comp.size() * cs * cs;
                if (area < spec.lakeArea * 1e6) continue;
                PlanLake L;
                L.level = lvl, L.x = X((int)s / n), L.z = X((int)s % n), L.area = area;
                for (int c : comp) p.lake[c] = (int)p.lakes.size();
                p.lakes.push_back(L);
            }
        }

        // ---- Rivers and washes: the drainage lines, traced down from their heads.
        const double washM2 = spec.washArea * 1e6, wetM2 = spec.riverArea * 1e6;
        std::vector<uint8_t> channel(N, 0), hasChild(N, 0), visited(N, 0);
        for (size_t i = 0; i < N; i++) channel[i] = acc[i] >= washM2 && p.lake[i] == -1 && !big[i];
        for (size_t i = 0; i < N; i++)
            if (channel[i] && parent[i] >= 0 && channel[parent[i]]) hasChild[parent[i]] = 1;
        p.wet.assign(N, 0), p.wash.assign(N, 0);
        for (size_t i = 0; i < N; i++) p.wet[i] = big[i] && !deck[i];
        for (size_t s = 0; s < N; s++) {
            if (!channel[s] || hasChild[s]) continue;
            std::vector<int> cells;
            int cur = (int)s;
            for (;;) {
                cells.push_back(cur);
                if (visited[cur]) break;
                visited[cur] = 1;
                const int up = parent[cur];
                if (up < 0) break;
                if (p.lake[up] != -1 || !channel[up]) {
                    cells.push_back(up);
                    break;
                }
                cur = up;
            }
            if (cells.size() < 4) continue;
            size_t w = 0;
            while (w < cells.size() && acc[cells[w]] < wetM2) w++;
            auto add = [&](size_t a, size_t b, bool wet) {
                if (b - a < 3) return;
                PlanRiver r;
                r.wet = wet;
                for (size_t k = a; k < b; k++) {
                    const int c = cells[k];
                    r.pts.push_back(V3(X(c / n), H[c], X(c % n)));
                    r.area.push_back((float)(acc[c] * 1e-6));
                    (wet ? p.wet : p.wash)[c] = 1;
                }
                p.rivers.push_back(std::move(r));
            };
            if (w > 0) add(0, std::min(cells.size(), w + 1), false);
            if (w < cells.size()) add(w, cells.size(), true);
        }

        // ---- Places ----------------------------------------------------------------------------------------------------------
        const double area = 4 * half * half * 1e-6;  // km2
        auto edgeOK = [&](int id, double margin) { return std::fabs(X(id / n)) < half - margin && std::fabs(X(id % n)) < half - margin; };
        std::vector<uint8_t> water(N, 0), anyCh(N, 0);
        for (size_t i = 0; i < N; i++) water[i] = p.wet[i] || p.lake[i] != -1, anyCh[i] = p.wet[i] || p.wash[i];
        const std::vector<float> waterDist = chamfer(n, cs, water), chDist = chamfer(n, cs, anyCh);
        const int rr = (int)std::ceil(24 / cs);
        const std::vector<float> hiR = windowExtreme(n, H, rr, true), loR = windowExtreme(n, H, rr, false);
        auto relief = [&](size_t id) { return (double)(hiR[id] - loR[id]); };
        auto valley = [&](int id) {
            const int ci = id / n, cj = id % n, r = (int)std::ceil(250 / cs);
            double s = 0;
            int c = 0;
            for (int a = -r; a <= r; a += r)
                for (int b = -r; b <= r; b += r) {
                    if (!a && !b) continue;
                    const int ni = std::min(n - 1, std::max(0, ci + a)), nj = std::min(n - 1, std::max(0, cj + b));
                    s += H[(size_t)ni * n + nj], c++;
                }
            return H[id] - s / c;
        };
        struct Cand {
            int id;
            double score;
        };
        std::vector<Cand> cands;
        for (int id = 0; id < (int)N; id++) {
            if (!edgeOK(id, 250) || p.lake[id] != -1 || relief(id) > 2.5 || waterDist[id] < 40 || chDist[id] < 30) continue;
            const double score = (2.5 - relief(id)) + 2.0 / (1 + std::max(0.0, waterDist[id] - 50.0) / 250.0) + clampd(-valley(id) / 40, -1, 1.5) +
                                 0.3 * roll((uint32_t)id, spec.seed);
            cands.push_back({id, score});
        }
        // (A sidecar's own places take the outposts' turn: with them, none are picked unless `outposts` asks.)
        const int nOut = spec.outposts >= 0 ? spec.outposts : !spec.places.empty() ? 0 : std::min(9, std::max(2, (int)(area / 5 + 1.5)));
        const double spacing = std::max(300.0, 0.6 * 2 * half / std::sqrt((double)std::max(1, nOut)));
        std::vector<int> chosen;
        if (!cands.empty() && nOut > 0) {
            int best = -1;
            double bs = -1e18;
            for (size_t k = 0; k < cands.size(); k++) {
                const double x = X(cands[k].id / n), z = X(cands[k].id % n);
                const double s = cands[k].score - 2.5 * std::sqrt(x * x + z * z) / half;
                if (s > bs) bs = s, best = (int)k;
            }
            chosen.push_back(cands[best].id);
            // (The others only where a trail (<= 25%) can get from it: not on a mesa top behind cliffs, or across a gorge.)
            std::vector<uint8_t> reach(N, 0);
            {
                std::vector<int> stack = {cands[best].id};
                reach[cands[best].id] = 1;
                while (!stack.empty()) {
                    const int c = stack.back();
                    stack.pop_back();
                    for (int k = 0; k < 16; k++) {
                        const int ni = c / n + kMI[k], nj = c % n + kMJ[k];
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                        const int nb = ni * n + nj;
                        if (reach[nb] || p.lake[nb] != -1 || !edgeOK(nb, 160)) continue;
                        const double L = k < 4 ? cs : k < 8 ? cs * kSqrt2 : cs * kSqrt5;
                        if (std::fabs((double)H[nb] - H[c]) > 0.25 * L) continue;
                        reach[nb] = 1, stack.push_back(nb);
                    }
                }
            }
            std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
            for (const Cand& c : cands) {
                if ((int)chosen.size() >= nOut) break;
                bool ok = true;
                for (int o : chosen) {
                    const double dx = X(c.id / n) - X(o / n), dz = X(c.id % n) - X(o % n);
                    if (dx * dx + dz * dz < spacing * spacing) ok = false;
                }
                if (ok && reach[c.id]) chosen.push_back(c.id);
            }
        }
        const std::string& nm = spec.name;
        std::vector<std::string> used;
        auto uniqueName = [&](std::string s) {
            std::string t = s;
            for (int k = 2; std::find(used.begin(), used.end(), t) != used.end(); k++) t = s + " " + std::to_string(k);
            used.push_back(t);
            return t;
        };
        auto padHeight = [&](int id, double r) {
            const int ci = id / n, cj = id % n, k = (int)std::ceil(r / cs);
            double s = 0;
            int c = 0;
            for (int a = -k; a <= k; a++)
                for (int b = -k; b <= k; b++) {
                    const int ni = ci + a, nj = cj + b;
                    if (ni < 0 || nj < 0 || ni >= n || nj >= n || (a * a + b * b) * cs * cs > r * r) continue;
                    s += H[(size_t)ni * n + nj], c++;
                }
            return c ? s / c : (double)H[id];
        };
        auto place = [&](int id, int kind, const std::string& pid, const std::string& name, double radius, double pad, unsigned svc) {
            MapPlace m;
            m.id = pid, m.name = uniqueName(name), m.kind = kind, m.map = MAP_REAL;
            m.p = V3(X(id / n), 0, X(id % n));
            m.yaw = M_PI / 2, m.radius = radius, m.pad = pad, m.services = svc;
            p.places.push_back(m);
            p.padY.push_back(pad > 0 ? padHeight(id, pad) : (double)H[id]);
        };
        const unsigned camp = SVC_TOW | SVC_SAVE;
        // The sidecar's places first (its town, the start, first of all), where they're given; then the ones picked.
        std::vector<const SpecPlace*> authored;
        for (const SpecPlace& sp : spec.places)
            if (sp.town) authored.push_back(&sp);
        for (const SpecPlace& sp : spec.places)
            if (!sp.town && sp.kind != PK_LOOKOUT) authored.push_back(&sp);
        for (const SpecPlace* sp : authored) {
            int id;
            if (!cellAt(sp->at, id)) continue;
            const std::string pid = "RT." + sp->name;
            const size_t at = p.places.size();
            if (sp->town)
                place(id, PK_RANGER_STATION, pid, sp->name, 60, 26, SVC_REPAIR | SVC_SHOP | SVC_FUEL | SVC_JOBS | SVC_GARAGE | SVC_DEALER | SVC_TOW | SVC_SAVE);
            else if (sp->kind == PK_FUEL_STOP) place(id, PK_FUEL_STOP, pid, sp->name, 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
            else if (sp->kind == PK_CABIN) place(id, PK_CABIN, pid, sp->name, 35, 9, camp | SVC_PARTS);
            else place(id, PK_OLD_CAMP, pid, sp->name, 35, 10, camp | SVC_REPAIR);
            double x, z;
            toMap(sp->at, x, z);
            p.places[at].p = V3(x, 0, z), p.places[at].yaw = sp->yawDeg * M_PI / 180;
        }
        for (size_t k = 0; k < chosen.size(); k++) {
            const int id = chosen[k];
            const std::string dw = dirWord(X(id / n), X(id % n), half);
            if (k == 0)
                place(id, PK_RANGER_STATION, "RT.RANGER", nm + " RANGER STATION", 50, 26,
                      SVC_REPAIR | SVC_SHOP | SVC_FUEL | SVC_JOBS | SVC_GARAGE | SVC_DEALER | SVC_TOW | SVC_SAVE);
            else if (k % 3 == 1) place(id, PK_FUEL_STOP, "RT.FUEL." + std::to_string(k), dw + " FUEL", 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
            else if (k % 3 == 2) place(id, PK_CABIN, "RT.CABIN." + std::to_string(k), dw + " CABIN", 35, 9, camp | SVC_PARTS);
            else place(id, PK_OLD_CAMP, "RT.CAMP." + std::to_string(k), dw + " CAMP", 35, 10, camp | SVC_REPAIR);
        }
        const int nOutposts = (int)p.places.size();
        // The tops: highest within 120 m, standing 20 m over what's within 400 m.
        std::vector<int> tops;
        {
            const std::vector<float> hi = windowExtreme(n, H, (int)std::ceil(120 / cs), true), lo = windowExtreme(n, H, (int)std::ceil(400 / cs), false);
            std::vector<int> c;
            for (int id = 0; id < (int)N; id++)
                if (edgeOK(id, 200) && H[id] >= hi[id] && H[id] - lo[id] >= 20 && p.lake[id] == -1) c.push_back(id);
            std::stable_sort(c.begin(), c.end(), [&](int a, int b) { return H[a] > H[b]; });
            const int nTop = std::min(8, std::max(1, (int)(area / 4 + 1)));
            const double apart = std::max(400.0, half / 6);  // (a big map's tops spread over it, not all on its highest ridge)
            for (int id : c) {
                if ((int)tops.size() >= nTop) break;
                bool ok = true;
                for (int o : tops) {
                    const double dx = X(id / n) - X(o / n), dz = X(id % n) - X(o % n);
                    if (dx * dx + dz * dz < apart * apart) ok = false;
                }
                for (int o : chosen) {
                    const double dx = X(id / n) - X(o / n), dz = X(id % n) - X(o % n);
                    if (dx * dx + dz * dz < 150.0 * 150.0) ok = false;
                }
                if (ok) tops.push_back(id);
            }
        }

        // ---- Roads and trails: A* over the coarse cells, a step no steeper than the grade, the steep and the water dear.
        std::vector<uint8_t> network(N, 0), named(N, 0);
        bool preferNet = true;  // (roads keep to the network where it goes their way; a named trail keeps to its own line)
        auto astar = [&](int from, int to, double maxGrade, std::vector<int>& path) {
            // (to < 0: to any cell already on the network)
            std::vector<double> g(N, 1e30);
            std::vector<int> came(N, -1);
            MinQ q;
            uint32_t seq = 0;
            const double tx = to >= 0 ? X(to / n) : 0, tz = to >= 0 ? X(to % n) : 0;
            auto heur = [&](int id) {
                if (to < 0) return 0.0;
                const double dx = X(id / n) - tx, dz = X(id % n) - tz;
                return 0.35 * std::sqrt(dx * dx + dz * dz);
            };
            g[from] = 0;
            q.push({heur(from), seq++, from});
            const double limit = 8 * 2 * half;
            int goal = -1;
            while (!q.empty()) {
                const QItem c = q.top();
                q.pop();
                if (c.key > g[c.id] + heur(c.id) + 1e-9) continue;
                if (c.id == to || (to < 0 && network[c.id] && c.id != from)) {
                    goal = c.id;
                    break;
                }
                if (g[c.id] > limit) break;
                const int ci = c.id / n, cj = c.id % n;
                // (16 ways: the knight's moves too, so a traverse across a slope runs straight, not as a zigzag that smoothing
                // would straighten into the fall line.)
                for (int k = 0; k < 16; k++) {
                    const int ni = ci + kMI[k], nj = cj + kMJ[k];
                    if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                    const int nb = ni * n + nj;
                    auto shut = [&](int id) { return p.lake[id] != -1 || (big[id] && !deck[id]); };  // (a big river: only over its bridge)
                    if (shut(nb) || (!edgeOK(nb, 160) && nb != to)) continue;
                    if (k >= 8) {  // (the two cells it passes between: no lake)
                        const int ai = ci + kMI[k] / 2, aj = cj + kMJ[k] / 2, bi = ci + (kMI[k] - kMI[k] / 2), bj = cj + (kMJ[k] - kMJ[k] / 2);
                        if (shut(ai * n + aj) || shut(bi * n + bj)) continue;
                    }
                    if (k >= 4 && (deck[nb] != deck[c.id] || (k >= 8 && deck[nb]))) continue;  // (onto and along a deck straight, not cutting a corner off it)
                    const double L = k < 4 ? cs : k < 8 ? cs * kSqrt2 : cs * kSqrt5, gr = std::fabs((double)HA[nb] - HA[c.id]) / L;
                    if (gr > maxGrade) continue;
                    double cost = L * (1 + 6 * (gr / maxGrade) * (gr / maxGrade));
                    if (p.wet[nb]) cost += 120;
                    else if (p.wash[nb]) cost += 6;
                    if (network[nb] && preferNet) cost *= 0.35;
                    const double ng = g[c.id] + cost;
                    if (ng < g[nb]) g[nb] = ng, came[nb] = c.id, q.push({ng + heur(nb), seq++, nb});
                }
            }
            path.clear();
            if (goal < 0) return false;
            for (int c = goal; c >= 0; c = came[c]) path.push_back(c);
            std::reverse(path.begin(), path.end());
            return true;
        };
        // A named trail's leg: A* over (cell, heading) in a window round the leg, so the line never doubles back on itself: a turn
        // dearer the sharper it is, none sharper than ~95 degrees (a switchback is two of them a cell or two apart, wide enough for
        // a truck once smoothed). `dir` in: the heading it arrives on (-1 any); out: the one it leaves on.
        double cosT[16][16];
        for (int a = 0; a < 16; a++)
            for (int b = 0; b < 16; b++)
                cosT[a][b] = (kMI[a] * kMI[b] + kMJ[a] * kMJ[b]) / std::sqrt((double)(kMI[a] * kMI[a] + kMJ[a] * kMJ[a]) * (kMI[b] * kMI[b] + kMJ[b] * kMJ[b]));
        auto astarTurns = [&](int from, int to, double maxGrade, int& dir, std::vector<int>& path) {
            const int pad = 90;
            const int i0 = std::max(0, std::min(from / n, to / n) - pad), i1 = std::min(n - 1, std::max(from / n, to / n) + pad);
            const int j0 = std::max(0, std::min(from % n, to % n) - pad), j1 = std::min(n - 1, std::max(from % n, to % n) + pad);
            const int ww = j1 - j0 + 1, M = (i1 - i0 + 1) * ww;
            auto local = [&](int id) { return (id / n - i0) * ww + (id % n - j0); };
            std::vector<double> g((size_t)M * 16, 1e30);
            std::vector<int> came((size_t)M * 16, -1);
            MinQ q;
            uint32_t seq = 0;
            const double tx = X(to / n), tz = X(to % n);
            auto heur = [&](int id) { return 0.35 * std::sqrt((X(id / n) - tx) * (X(id / n) - tx) + (X(id % n) - tz) * (X(id % n) - tz)); };
            for (int d = 0; d < 16; d++)
                if (dir < 0 || cosT[dir][d] > 0.65) g[(size_t)local(from) * 16 + d] = 0, q.push({heur(from), seq++, local(from) * 16 + d});
            int goal = -1;
            while (!q.empty()) {
                const QItem c = q.top();
                q.pop();
                const int ls = c.id / 16, d = c.id % 16, ci = ls / ww + i0, cj = ls % ww + j0, id = ci * n + cj;
                if (c.key > g[c.id] + heur(id) + 1e-9) continue;
                if (id == to) {
                    goal = c.id;
                    break;
                }
                for (int k = 0; k < 16; k++) {
                    if (cosT[d][k] < 0.6 && !(id == from && dir < 0)) continue;  // (no turn sharper than ~53 degrees a step: a hairpin takes four or more)
                    const int ni = ci + kMI[k], nj = cj + kMJ[k];
                    if (ni < i0 || nj < j0 || ni > i1 || nj > j1) continue;
                    const int nb = ni * n + nj;
                    auto shut = [&](int x) { return p.lake[x] != -1 || (big[x] && !deck[x]); };
                    if (shut(nb) || (!edgeOK(nb, 160) && nb != to)) continue;
                    if (k >= 8) {
                        const int ai = ci + kMI[k] / 2, aj = cj + kMJ[k] / 2, bi = ci + (kMI[k] - kMI[k] / 2), bj = cj + (kMJ[k] - kMJ[k] / 2);
                        if (shut(ai * n + aj) || shut(bi * n + bj)) continue;
                    }
                    const double L = k < 4 ? cs : k < 8 ? cs * kSqrt2 : cs * kSqrt5, gr = std::fabs((double)HA[nb] - HA[id]) / L;
                    if (gr > maxGrade) continue;
                    // (and dear along a cliff's edge or across a steep side slope: the steepest way down from the cell, past the grade)
                    double fall = 0;
                    for (int e = 0; e < 8; e++) {
                        const int ei = std::min(n - 1, std::max(0, ni + kDI[e])), ej = std::min(n - 1, std::max(0, nj + kDJ[e]));
                        fall = std::max(fall, std::fabs((double)H[nb] - H[(size_t)ei * n + ej]) / (e < 4 ? cs : cs * kSqrt2));
                    }
                    double cost = L * (1 + 6 * (gr / maxGrade) * (gr / maxGrade) + 8 * std::max(0.0, fall - maxGrade)) + (id == from && dir < 0 ? 0 : 30 * (1 - cosT[d][k]));
                    if (p.wet[nb]) cost += 120;
                    else if (p.wash[nb]) cost += 6;
                    const size_t ns = (size_t)local(nb) * 16 + k;
                    const double ng = g[c.id] + cost;
                    if (ng < g[ns]) g[ns] = ng, came[ns] = c.id, q.push({ng + heur(nb), seq++, (int)ns});
                }
            }
            path.clear();
            if (goal < 0) return false;
            dir = goal % 16;
            for (int c = goal; c >= 0; c = came[c]) {
                const int ls = c / 16, id = (ls / ww + i0) * n + (ls % ww + j0);
                if (path.empty() || path.back() != id) path.push_back(id);
            }
            std::reverse(path.begin(), path.end());
            return true;
        };
        auto addTrail = [&](const std::vector<int>& path, int from, int to, bool road, double grade) {
            PlanTrail t;
            t.from = from, t.to = to, t.road = road, t.maxGrade = grade;
            for (int c : path) t.pts.push_back(V3(X(c / n), HA[c], X(c % n))), network[c] = 1;
            p.trails.push_back(std::move(t));
        };
        // (Laid first: the roads between the outposts then keep to them where they go their way.)
        std::vector<int> starts;
        // The sidecar's named trails: through its points in order, each leg the cheapest way no steeper than its grade (a little
        // steeper where there's no such way), and a spur from its start to the network when it isn't on it.
        for (const SpecTrail& st : spec.trails) {
            std::vector<int> way;
            for (const LatLon& ll : st.pts) {
                int id;
                if (!cellAt(ll, id)) continue;
                // (off water and the rim: onto the nearest dry cell inside)
                const int ci = id / n, cj = id % n;
                int best = -1;
                double bd = 1e30;
                for (int a = -6; a <= 6; a++)
                    for (int c = -6; c <= 6; c++) {
                        const int ni = std::min(n - 1, std::max(0, ci + a)), nj = std::min(n - 1, std::max(0, cj + c)), nb = ni * n + nj;
                        if (p.lake[nb] != -1 || (big[nb] && !deck[nb]) || !edgeOK(nb, 160)) continue;
                        const double d = a * a + c * c;
                        if (d < bd) bd = d, best = nb;
                    }
                if (best >= 0 && (way.empty() || way.back() != best)) way.push_back(best);
            }
            if (way.size() < 2) {
                std::printf("real terrain %s: trail %s has no points on the map\n", spec.name.c_str(), st.name.c_str());
                continue;
            }
            std::vector<int> all, leg;
            bool ok = true;
            double used = st.grade;
            preferNet = false;
            int dir = -1;
            for (size_t w = 0; w + 1 < way.size() && ok; w++) {
                bool got = false;
                for (double gr = st.grade; !got && gr <= st.grade * 1.6 + 1e-9; gr += st.grade * 0.2) {
                    int d = dir;
                    if ((got = astarTurns(way[w], way[w + 1], gr, d, leg))) used = std::max(used, gr), dir = d;
                }
                if (!got) {
                    std::printf("real terrain %s: trail %s: no way from point %d to %d\n", spec.name.c_str(), st.name.c_str(), (int)w + 1, (int)w + 2);
                    ok = false;
                    break;
                }
                all.insert(all.end(), leg.begin() + (all.empty() ? 0 : 1), leg.end());
            }
            preferNet = true;
            if (!ok) continue;
            addTrail(all, -1, -1, false, used);
            p.trails.back().name = st.name, p.trails.back().rating = st.rating;
            starts.push_back(all[0]);
        }
        // The outposts joined by the shortest tree of straight lines (Prim's, from the ranger station).
        {
            std::vector<uint8_t> in(nOutposts, 0);
            if (nOutposts > 0) in[0] = 1;
            for (int added = 1; added < nOutposts; added++) {
                int ba = -1, bb = -1;
                double bd = 1e30;
                for (int a = 0; a < nOutposts; a++)
                    if (in[a])
                        for (int b = 0; b < nOutposts; b++)
                            if (!in[b]) {
                                const double dx = p.places[a].p.x - p.places[b].p.x, dz = p.places[a].p.z - p.places[b].p.z, d = dx * dx + dz * dz;
                                if (d < bd) bd = d, ba = a, bb = b;
                            }
                in[bb] = 1;
                const int ca = p.cellOf(p.places[ba].p.x) * n + p.cellOf(p.places[ba].p.z), cb = p.cellOf(p.places[bb].p.x) * n + p.cellOf(p.places[bb].p.z);
                std::vector<int> path;
                if (astar(ca, cb, 0.12, path)) addTrail(path, ba, bb, true, 0.12);
                else if (astar(ca, cb, 0.25, path)) addTrail(path, ba, bb, false, 0.25);
            }
        }
        // A spur onto the network from each named trail's start, where it's off it.
        for (const PlanTrail& t : p.trails)
            if (t.rating >= 0)
                for (const V3& q : t.pts) named[p.cellOf(q.x) * n + p.cellOf(q.z)] = 1;
        for (int st0 : starts) {
            bool onNet = false;
            for (int a = -3; a <= 3 && !onNet; a++)
                for (int c = -3; c <= 3 && !onNet; c++) {
                    const int ni = st0 / n + a, nj = st0 % n + c;
                    if (ni >= 0 && nj >= 0 && ni < n && nj < n && network[ni * n + nj] && !named[ni * n + nj]) onNet = true;
                }
            std::vector<int> spur;
            if (!onNet && nOutposts > 0) {
                std::vector<uint8_t> keepNet = network;  // (onto a road, not back onto a named trail)
                for (size_t i = 0; i < N; i++) network[i] = network[i] && !named[i];
                const bool got = astar(st0, -1, 0.25, spur);
                network = keepNet;
                if (got) {
                    std::reverse(spur.begin(), spur.end());
                    addTrail(spur, -1, -1, false, 0.25);
                }
            }
        }
        // The sidecar's lookouts: a trail up from the network.
        for (const SpecPlace& sp : spec.places) {
            int id;
            if (sp.kind != PK_LOOKOUT || !cellAt(sp.at, id)) continue;
            std::vector<int> path;
            place(id, PK_LOOKOUT, "RT." + sp.name, sp.name, 40, 6, 0);
            double x, z;
            toMap(sp.at, x, z);
            p.places.back().p = V3(x, 0, z), p.places.back().yaw = sp.yawDeg * M_PI / 180;
            if (!p.trails.empty() && astar(id, -1, 0.30, path)) {
                std::reverse(path.begin(), path.end());
                addTrail(path, -1, (int)p.places.size() - 1, false, 0.30);
            }
        }
        // The tops: a trail up (<= 30%) from the network makes one a lookout; one with no way up is a summit to find on foot.
        for (int id : tops) {
            const double elev = realElevation(H[id]);
            const std::string dw = dirWord(X(id / n), X(id % n), half);
            std::vector<int> path;
            const bool any = !p.trails.empty();
            if (any && astar(id, -1, 0.30, path)) {
                std::reverse(path.begin(), path.end());  // (from the network up)
                place(id, PK_LOOKOUT, "RT.LOOKOUT." + std::to_string(p.places.size()), dw + " LOOKOUT", 40, 6, 0);
                addTrail(path, -1, (int)p.places.size() - 1, false, 0.30);
            } else {
                char b[48];
                std::snprintf(b, sizeof b, "PEAK %d", (int)std::floor(elev + 0.5));
                place(id, PK_SUMMIT, "RT.SUMMIT." + std::to_string(p.places.size()), b, 35, 0, 0);
            }
        }
        // Where a road or trail fords a river.
        for (const PlanTrail& t : p.trails)
            for (const V3& q : t.pts) {
                const int id = p.cellOf(q.x) * n + p.cellOf(q.z);
                if (!p.wet[id]) continue;
                bool near = false;
                for (const MapPlace& m : p.places)
                    if (m.kind == PK_RIVER_CROSSING && (m.p.x - q.x) * (m.p.x - q.x) + (m.p.z - q.z) * (m.p.z - q.z) < 150.0 * 150.0) near = true;
                if (!near) place(id, PK_RIVER_CROSSING, "RT.FORD." + std::to_string(p.places.size()), dirWord(q.x, q.z, half) + " FORD", 45, 0, 0);
            }
        // The wrecks (wrecks.h): dry, gentle ground off the roads, well apart: about one a square kilometre, 12 to 40.
        {
            const int want = (int)std::clamp(4 * half * half / 1e6, 12.0, 40.0);
            int made = 0;
            uint32_t rng = 0x7EC4u;
            auto next = [&] {
                rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
                return rng;
            };
            std::vector<V3> at;
            for (const MapPlace& m : p.places) at.push_back(m.p);
            const size_t ownFrom = at.size();
            for (int t = 0; t < 4000 && made < want; t++) {
                const int ci = 2 + (int)(next() % (uint32_t)(n - 4)), cj = 2 + (int)(next() % (uint32_t)(n - 4)), id = ci * n + cj;
                if (p.lake[id] != -1 || p.wet[id] || p.wash[id] || network[id] || !edgeOK(id, 160)) continue;
                if (std::fabs((double)H[id + n] - H[id - n]) / (2 * cs) > 0.35 || std::fabs((double)H[id + 1] - H[id - 1]) / (2 * cs) > 0.35) continue;
                const V3 q(X(ci), 0, X(cj));
                bool ok = true;
                for (size_t k = 0; k < at.size() && ok; k++) ok = len(at[k] - q) > (k < ownFrom ? 70.0 : 180.0);
                for (int a = -2; a <= 2 && ok; a++)
                    for (int b = -2; b <= 2 && ok; b++) ok = !network[(ci + a) * n + cj + b];
                if (!ok) continue;
                MapPlace m;
                m.id = "RT.WRECK." + std::to_string(++made), m.kind = PK_WRECK, m.map = MAP_REAL, m.p = q;
                m.yaw = (next() % 628) / 100.0, m.radius = 8;
                p.places.push_back(m);
                p.padY.push_back((double)H[id]);
                at.push_back(q);
            }
        }
        // The sidecar's courses: each along its trail's line, as it will be laid (smoothed, a metre apart).
        for (const SpecCourse& sc : spec.courses) {
            PlanCourse c;
            c.spec = sc;
            for (size_t k = 0; k < p.trails.size(); k++)
                if (p.trails[k].name == sc.trail) c.trail = (int)k;
            if (c.trail < 0) continue;
            c.line = smoothLine(p.trails[c.trail].pts, 1.0);
            for (size_t k = 0; k < c.line.size(); k++) {
                c.line[k].y = demAt(c.line[k].x, c.line[k].z);
                if (k == 0) continue;
                c.length += std::sqrt((c.line[k].x - c.line[k - 1].x) * (c.line[k].x - c.line[k - 1].x) + (c.line[k].z - c.line[k - 1].z) * (c.line[k].z - c.line[k - 1].z));
                const double dy = c.line[k].y - c.line[k - 1].y;
                (dy > 0 ? c.climb : c.drop) += std::fabs(dy);
            }
            p.courses.push_back(std::move(c));
        }
        (void)bigRivers;
        plan_ = std::move(P);
    });
    return *plan_;
}

}  // namespace realterrain
