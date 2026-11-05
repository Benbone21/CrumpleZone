// Water that runs like water (docs/plans/notes-backlog.md "Water"): a channel from its source to its outlet in one piece, no strips.
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "../src/procworld.h"
#include "../src/rivers.h"
#include "harness.h"

namespace {
// One piece of water: the cells of visible water (over the land) joined side by side or corner to corner.
struct WaterPiece {
    long cells = 0, flowing = 0;
    int i0 = 1 << 30, i1 = -1, j0 = 1 << 30, j1 = -1;
    std::vector<int> ids;
    bool has(int i, int j) const { return i >= i0 && i <= i1 && j >= j0 && j <= j1 && std::binary_search(ids.begin(), ids.end(), i * stride + j); }
    int stride = 0;
    bool flows() const { return flowing * 2 > cells; }
    int span() const { return std::max(i1 - i0, j1 - j0) + 1; }  // m (cells are 1 m)
    bool clipped(int n) const { return i0 <= 1 || j0 <= 1 || i1 >= n - 2 || j1 >= n - 2; }  // (runs off the grid's edge: its length isn't known)
};

std::vector<char> wetCells(const World& w) {
    const int n = w.gridSize();
    std::vector<char> wet((size_t)n * n, 0);
    for (int id = 0; id < n * n; id++) wet[id] = w.waterY[id] > -1e8f && w.waterY[id] > w.cellCenter(id).y;
    return wet;
}

// The ways water is meant to go on without showing on the grid, as water: down a falls (its model is the water) and past a tread it runs
// under or that climbs out of a ford: a channel broken only by these is one channel.
std::vector<char> withCrossings(const World& w, std::vector<char> wet) {
    const int n = w.gridSize();
    const double half = w.halfSize();
    for (const Falls& f : w.falls) {
        const V3 a = f.lip, b = f.base();
        const double run = std::hypot(b.x - a.x, b.z - a.z) + 30, ux = std::cos(f.yaw), uz = -std::sin(f.yaw), wide = f.width / 2 + 8;
        for (double t = -15; t <= run; t += 1)
            for (double l = -wide; l <= wide; l += 1) {
                const int id = w.cellIndex(a.x + ux * t - uz * l, a.z + uz * t + ux * l);
                if (id >= 0) wet[id] = 1;
            }
    }
    std::vector<int> tread;  // (cells of tread within 6 m of water, reached outward from the water)
    for (int i = 6; i < n - 6; i += 1)
        for (int j = 6; j < n - 6; j += 1) {
            if (!wet[i * n + j] || (wet[(i - 1) * n + j] && wet[(i + 1) * n + j] && wet[i * n + j - 1] && wet[i * n + j + 1])) continue;
            for (int a = i - 6; a <= i + 6; a++)
                for (int b = j - 6; b <= j + 6; b++)
                    if (!wet[a * n + b]) {
                        const RoadInfo r = w.roadAt(a - half, b - half);
                        if (r.near && (r.paint == PAINT_TRAIL || r.paint == PAINT_ROAD || r.paint == PAINT_TRACK || std::fabs(r.lateral) > 0)) tread.push_back(a * n + b);
                    }
        }
    for (int id : tread) wet[id] = 1;
    return wet;
}

std::vector<WaterPiece> waterPieces(const World& w, std::vector<char> wet) {
    const int n = w.gridSize();
    std::vector<WaterPiece> out;
    std::vector<int> st;
    for (int s = 0; s < n * n; s++) {
        if (!wet[s]) continue;
        WaterPiece p;
        p.stride = n;
        wet[s] = 0, st.push_back(s);
        while (!st.empty()) {
            const int id = st.back(), i = id / n, j = id % n;
            st.pop_back();
            p.cells += w.waterY[id] > -1e8f, p.flowing += w.flowX[id] != 0 || w.flowZ[id] != 0, p.ids.push_back(id);
            p.i0 = std::min(p.i0, i), p.i1 = std::max(p.i1, i), p.j0 = std::min(p.j0, j), p.j1 = std::max(p.j1, j);
            for (int a = std::max(0, i - 1); a <= std::min(n - 1, i + 1); a++)
                for (int b = std::max(0, j - 1); b <= std::min(n - 1, j + 1); b++)
                    if (wet[a * n + b]) wet[a * n + b] = 0, st.push_back(a * n + b);
        }
        std::sort(p.ids.begin(), p.ids.end());
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<WaterPiece> waterPieces(const World& w) { return waterPieces(w, wetCells(w)); }

void report(Ctx& ctx, const World& w, const std::vector<WaterPiece>& ps, const std::string& label) {
    const int n = w.gridSize();
    long flow = 0, shortFlow = 0;
    for (const WaterPiece& p : ps) flow += p.flows(), shortFlow += p.flows() && p.span() < 100 && !p.clipped(n);
    long under = 0;
    for (int id = 0; id < n * n; id++) under += w.waterY[id] > -1e8f && w.waterY[id] <= w.cellCenter(id).y;
    char b[200];
    std::snprintf(b, sizeof b, "%s: %zu pieces, %ld flowing, %ld flowing under 100 m; %ld cells under the land", label.c_str(), ps.size(), flow, shortFlow, under);
    ctx.note(b);
    if (std::getenv("WATER_LIST")) {
        for (const WaterPiece& p : ps) {
            if (!p.flows() && p.cells < 400) continue;
            std::printf("  %s %ld cells, x %.0f..%.0f z %.0f..%.0f\n", p.flows() ? "flow " : "still", p.cells, p.i0 - w.halfSize(), p.i1 - w.halfSize(), p.j0 - w.halfSize(), p.j1 - w.halfSize());
        }
    }
}

// Flowing channels (falls and tread crossings counted through), how many are cut off under 100 m long, and how many pairs lie within 15 m
// of each other: a break in what should be one channel.
struct Continuity {
    long flowing = 0, cutOff = 0, breaks = 0;
};
Continuity continuity(const World& w) {
    const int n = w.gridSize();
    const std::vector<WaterPiece> ps = waterPieces(w, withCrossings(w, wetCells(w)));
    Continuity c;
    constexpr int kBin = 16;
    const int nb = n / kBin + 1;
    std::vector<std::vector<int>> bins((size_t)nb * nb);
    for (size_t k = 0; k < ps.size(); k++) {
        if (!ps[k].flows()) continue;
        c.flowing++, c.cutOff += ps[k].span() < 100 && !ps[k].clipped(n);
        for (int id : ps[k].ids) {
            std::vector<int>& b = bins[(size_t)(id / n / kBin) * nb + (id % n) / kBin];
            if (b.empty() || b.back() != (int)k) b.push_back((int)k);
        }
    }
    std::set<std::pair<int, int>> pairs;
    for (int a = 0; a < nb; a++)
        for (int b = 0; b < nb; b++)
            for (int p : bins[(size_t)a * nb + b])
                for (int da = 0; da <= 1 && a + da < nb; da++)
                    for (int db = -1; db <= 1 && b + db >= 0 && b + db < nb; db++)
                        for (int q : bins[(size_t)(a + da) * nb + b + db]) {
                            if (p == q) continue;
                            // (two pieces in neighbouring bins: how near, by their cells there)
                            double best = 1e18;
                            for (int u : ps[p].ids) {
                                const int ui = u / n, uj = u % n;
                                if (ui / kBin != a || uj / kBin != b) continue;
                                for (int v : ps[q].ids) {
                                    const int vi = v / n, vj = v % n;
                                    if (vi / kBin != a + da || vj / kBin != b + db) continue;
                                    best = std::min(best, std::hypot((double)(ui - vi), (double)(uj - vj)));
                                }
                            }
                            if (best < 15) pairs.insert({std::min(p, q), std::max(p, q)});
                        }
    c.breaks = (long)pairs.size();
    return c;
}

// The piece of water at (x, z), by the cell nearest it that's wet within `reach` m: its index in pieces, -1 if none.
int pieceAt(const World& w, const std::vector<WaterPiece>& ps, double x, double z, double reach = 6) {
    double best = 1e18;
    int bi = -1, bj = -1;
    const int n = w.gridSize();
    for (double a = -reach; a <= reach; a += 1)
        for (double b = -reach; b <= reach; b += 1) {
            const int id = w.cellIndex(x + a, z + b);
            if (id < 0 || w.waterY[id] < -1e8f || w.waterY[id] <= w.cellCenter(id).y) continue;
            if (a * a + b * b < best) best = a * a + b * b, bi = id / n, bj = id % n;
        }
    if (bi < 0) return -1;
    for (size_t k = 0; k < ps.size(); k++)
        if (ps[k].has(bi, bj)) return (int)k;
    return -1;
}

const River* riverNamed(const World& w, const char* name) {
    for (const River& r : w.rivers)
        if (r.name == name) return &r;
    return nullptr;
}

// Down a river's spots: its level never rising, no dry stretch, its width only growing (but for a ford's spreading out).
void checkDescends(Ctx& ctx, const World& w, const std::vector<WaterPiece>& ps, const char* name, double minLen, double fordSlack) {
    const River* r = riverNamed(w, name);
    ctx.check(std::string(name) + ": on the map", r != nullptr);
    if (!r || r->spots.size() < 2) return;
    double length = 0, worstGap = 0, worstRise = 0, worstNarrow = 0, widest = 0;
    int pieces = 0, last = -2;
    for (size_t k = 0; k < r->spots.size(); k++) {
        const RiverSpot& s = r->spots[k];
        const int p = pieceAt(w, ps, s.p.x, s.p.z);
        if (p != last) pieces++, last = p;
        widest = std::max(widest, (double)s.halfWidth);
        worstNarrow = std::max(worstNarrow, widest - s.halfWidth);
        if (!k) continue;
        const RiverSpot& q = r->spots[k - 1];
        const double d = std::hypot(s.p.x - q.p.x, s.p.z - q.p.z);
        length += d, worstGap = std::max(worstGap, d), worstRise = std::max(worstRise, s.p.y - q.p.y);
    }
    ctx.note(std::string(name) + ": " + std::to_string((int)length) + " m, " + std::to_string(pieces) + " piece(s)");
    ctx.check(std::string(name) + ": runs its whole length", length > minLen, std::to_string(length));
    ctx.check(std::string(name) + ": one piece of water", pieces == 1, std::to_string(pieces));
    ctx.check(std::string(name) + ": no dry stretch (the spots are 6 m apart)", worstGap < 13, std::to_string(worstGap));
    ctx.check(std::string(name) + ": never rises going downstream", worstRise <= 0.005, std::to_string(worstRise));
    ctx.check(std::string(name) + ": no narrower downstream", worstNarrow <= fordSlack, std::to_string(worstNarrow));
    ctx.check(std::string(name) + ": starts as a trickle (no cut end)", r->spots.front().halfWidth < 0.45 * widest, std::to_string(r->spots.front().halfWidth));
}
}  // namespace

SCENARIO(waterBackcountry, "water/backcountry", "Backcountry's water runs in channels: Elk River split by its falls alone, Copper Creek whole down to its pond, ROCKY RIVER fed and draining into Elk River, nothing flowing cut off") {
    const World w(MAP_BACKCOUNTRY);
    const std::vector<WaterPiece> ps = waterPieces(w);
    report(ctx, w, ps, "backcountry");
    // (Flowing water, falls and tread crossings counted through: Elk River, Copper Creek, the Creek Crawl's and the canyon's trickles, the
    // Open Country's river.)
    const Continuity c = continuity(w);
    ctx.metric("flowing channels", c.flowing, "", 4, 6);
    ctx.metric("...of them under 100 m", c.cutOff, "", 0, 0);
    ctx.metric("...breaks of under 15 m", c.breaks, "", 0, 0);

    checkDescends(ctx, w, ps, "COPPER CREEK", 700, 0.7);
    checkDescends(ctx, w, ps, "ROCKY RIVER", 1100, 1.8);

    // Copper Creek ends in a pond (still water, the same piece), clear of the road but for its two fords.
    const River* cc = riverNamed(w, "COPPER CREEK");
    if (cc) {
        const V3 end = cc->spots.back().p;
        ctx.check("Copper Creek: its outlet is still water", len(w.waterFlow(end.x, end.z)) < 1e-6);
        int wetRoad = 0;
        const std::vector<V3>& road = bcTrailLine("COPPER VALLEY ROAD");
        for (const V3& p : road) wetRoad += p.x > -340 && p.x < 440 && p.z > 840 && p.z < 930 && w.waterLevel(p.x, p.z) > w.height(p.x, p.z);
        ctx.metric("Copper Creek: valley road under its water", wetRoad, "m", 4, 40);
    }
    // ROCKY RIVER: the trail wades it at the section; it's the same water as Elk River.
    const River* rr = riverNamed(w, "ROCKY RIVER");
    if (rr) {
        const V3 end = rr->spots.back().p;
        const int mouth = pieceAt(w, ps, end.x, end.z), elk = pieceAt(w, ps, end.x, elkRiverZ(end.x));
        ctx.check("ROCKY RIVER: runs into Elk River", mouth >= 0 && mouth == elk);
        const BcSetPiece* gully = nullptr;
        for (const BcSetPiece& s : bcSetPieces()) gully = std::string(s.name) == "BOULDER GULLY" ? &s : gully;
        double deepest = 0;
        for (double t = 0; gully && t < 1; t += 0.002) {
            const V3 p = gully->entry + (gully->exit - gully->entry) * t;
            deepest = std::max(deepest, w.waterLevel(p.x, p.z) - w.height(p.x, p.z));
        }
        ctx.metric("ROCKY RIVER: the ford on BOULDER GULLY", deepest, "m", 0.3, 0.8);
    }
}

SCENARIO(waterProcedural, "water/procedural", "The procedural world's rivers and creeks run unbroken through a window: no dry bands across them, few short pieces (falls and tread crossings counted through; v10 had 134, 9 and 91 pieces, most under 100 m)") {
    struct Limit {
        uint64_t seed;
        long channels, cutOff, breaks;  // (at least, at most, at most)
    };
    // (What's left: a creek's head above a stretch too far over its land to sink, and trails riding a creek's line for a way.)
    for (const Limit& l : {Limit{1, 8, 14, 18}, Limit{2, 3, 1, 1}, Limit{123456789, 8, 7, 7}, Limit{77, 6, 5, 9}}) {
        World::procSeed = l.seed, World::procOriginSet = false;
        const World w(MAP_PROCEDURAL);
        report(ctx, w, waterPieces(w), "seed " + std::to_string(l.seed));
        const Continuity c = continuity(w);
        char b[120];
        std::snprintf(b, sizeof b, "seed %llu: %ld channels, %ld cut off under 100 m, %ld breaks", (unsigned long long)l.seed, c.flowing, c.cutOff, c.breaks);
        ctx.note(b);
        const std::string tag = "seed " + std::to_string(l.seed) + ": ";
        ctx.check(tag + "there are rivers and creeks", c.flowing >= l.channels, std::to_string(c.flowing));
        ctx.check(tag + "few cut off under 100 m", c.cutOff <= l.cutOff, std::to_string(c.cutOff));
        ctx.check(tag + "few breaks of under 15 m", c.breaks <= l.breaks, std::to_string(c.breaks));
    }
    World::procOriginSet = false;
}
