// Water that runs like water (docs/plans/notes-backlog.md "Water"): a channel from its source to its outlet in one piece, no strips.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

std::vector<WaterPiece> waterPieces(const World& w) {
    const int n = w.gridSize();
    std::vector<char> wet((size_t)n * n, 0);
    for (int id = 0; id < n * n; id++) wet[id] = w.waterY[id] > -1e8f && w.waterY[id] > w.cellCenter(id).y;
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
            p.cells++, p.flowing += w.flowX[id] != 0 || w.flowZ[id] != 0, p.ids.push_back(id);
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
    const int n = w.gridSize();
    // (Flowing pieces: Elk River between its two falls and the sea (3), Copper Creek, the Creek Crawl's and the canyon's trickles, the
    // Open Country's river.)
    int flowing = 0, cutOff = 0;
    for (const WaterPiece& p : ps) flowing += p.flows(), cutOff += p.flows() && p.span() < 100 && !p.clipped(n);
    ctx.metric("flowing pieces", flowing, "", 5, 8);
    ctx.metric("...of them under 100 m", cutOff, "", 0, 1);

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
SCENARIO(waterPiecesProc, "water/pieces-procedural", "The procedural world's water") {
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        World::procSeed = seed, World::procOriginSet = false;
        const World w(MAP_PROCEDURAL);
        report(ctx, w, waterPieces(w), "seed " + std::to_string(seed));
    }
}
