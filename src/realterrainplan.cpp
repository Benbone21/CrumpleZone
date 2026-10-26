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
                if (seen[s] || filled[s] - H[s] < 0.25f || p.lake[s] != -1) continue;
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
                        if (seen[nb] || filled[nb] - H[nb] < 0.25f || std::fabs(filled[nb] - lvl) > 1e-3f) continue;
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
        for (size_t i = 0; i < N; i++) channel[i] = acc[i] >= washM2 && p.lake[i] == -1;
        for (size_t i = 0; i < N; i++)
            if (channel[i] && parent[i] >= 0 && channel[parent[i]]) hasChild[parent[i]] = 1;
        p.wet.assign(N, 0), p.wash.assign(N, 0);
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
        const int nOut = spec.outposts >= 0 ? spec.outposts : std::min(9, std::max(2, (int)(area / 5 + 1.5)));
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
            for (int id : c) {
                if ((int)tops.size() >= nTop) break;
                bool ok = true;
                for (int o : tops) {
                    const double dx = X(id / n) - X(o / n), dz = X(id % n) - X(o % n);
                    if (dx * dx + dz * dz < 400.0 * 400.0) ok = false;
                }
                for (int o : chosen) {
                    const double dx = X(id / n) - X(o / n), dz = X(id % n) - X(o % n);
                    if (dx * dx + dz * dz < 150.0 * 150.0) ok = false;
                }
                if (ok) tops.push_back(id);
            }
        }

        // ---- Roads and trails: A* over the coarse cells, a step no steeper than the grade, the steep and the water dear.
        std::vector<uint8_t> network(N, 0);
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
                    if (p.lake[nb] != -1 || (!edgeOK(nb, 160) && nb != to)) continue;
                    if (k >= 8) {  // (the two cells it passes between: no lake)
                        const int ai = ci + kMI[k] / 2, aj = cj + kMJ[k] / 2, bi = ci + (kMI[k] - kMI[k] / 2), bj = cj + (kMJ[k] - kMJ[k] / 2);
                        if (p.lake[ai * n + aj] != -1 || p.lake[bi * n + bj] != -1) continue;
                    }
                    const double L = k < 4 ? cs : k < 8 ? cs * kSqrt2 : cs * kSqrt5, gr = std::fabs((double)H[nb] - H[c.id]) / L;
                    if (gr > maxGrade) continue;
                    double cost = L * (1 + 6 * (gr / maxGrade) * (gr / maxGrade));
                    if (p.wet[nb]) cost += 120;
                    else if (p.wash[nb]) cost += 6;
                    if (network[nb]) cost *= 0.35;
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
        auto addTrail = [&](const std::vector<int>& path, int from, int to, bool road, double grade) {
            PlanTrail t;
            t.from = from, t.to = to, t.road = road, t.maxGrade = grade;
            for (int c : path) t.pts.push_back(V3(X(c / n), H[c], X(c % n))), network[c] = 1;
            p.trails.push_back(std::move(t));
        };
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
        plan_ = std::move(P);
    });
    return *plan_;
}

}  // namespace realterrain
