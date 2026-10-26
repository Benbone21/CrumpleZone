// The procedural world (src/procworld.h, docs/superpowers/specs/2026-10-25-procedural-world-design.md): the same seed and chunk
// always make the same bytes, other seeds make other land, chunk borders are seamless, the window slides without the land moving
// under a vehicle, far out is the same as near, and a chunk is made within its budget.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <array>
#include <set>

#include "../src/procworld.h"
#include "../src/net.h"
#include "harness.h"
#include <thread>
#include <unistd.h>

using namespace proc;

namespace {
const VehicleSpec& p4Runner() { return vehicleCatalog()[3]; }

// A world of `seed` round global (gx, gz) (a chunk corner near it).
World windowAt(uint64_t seed, double gx, double gz) {
    World::procSeed = seed;
    World::procOriginSet = true;
    World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
    World w(MAP_PROCEDURAL);
    World::procOriginSet = false;
    return w;
}
std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
    return b;
}
}  // namespace

SCENARIO(procDeterministic, "procworld/deterministic", "The same seed and chunk make the same bytes every time (heights, ground, water, trails and everything placed); a cached chunk is the one made afresh") {
    const ProcWorld& G = ProcWorld::forSeed(12345);
    bool same = true;
    std::string bad;
    const int64_t spots[][2] = {{0, 0}, {-1, -1}, {7, -3}, {-40, 25}, {1562, -1562}, {-156250, 156250}};
    for (const auto& s : spots) {
        const uint64_t a = G.makeChunk(s[0], s[1])->hash(), b = G.makeChunk(s[0], s[1])->hash(), c = G.chunk(s[0], s[1])->hash();
        if (a != b || a != c) same = false, bad += " " + std::to_string(s[0]) + "," + std::to_string(s[1]);
    }
    ctx.check("same seed + chunk: same hash (fresh, again, cached)", same, bad);
    // A fixed chunk's hash, pinned: this changes only with the generator (bump kGenVersion when it does). Every platform must agree.
    const uint64_t h00 = G.makeChunk(0, 0)->hash();
    ctx.note("chunk (0,0) of seed 12345: " + hex(h00) + " (generator v" + std::to_string(kGenVersion) + ")");
    // A second ProcWorld for the seed (as another machine would make) gives the same.
    const ProcWorld other(12345);
    ctx.check("another generator for the seed: the same chunk", other.makeChunk(7, -3)->hash() == G.makeChunk(7, -3)->hash());
}

SCENARIO(procSeedsDiffer, "procworld/seeds-differ", "Different seeds make clearly different land: other heights and another mix of biomes") {
    const uint64_t seeds[] = {1, 2, 3, 99, 123456789, 0xDEADBEEFull};
    std::vector<std::vector<double>> hs;
    std::vector<std::array<double, PB_COUNT>> mixes;
    for (uint64_t s : seeds) {
        const ProcWorld& G = ProcWorld::forSeed(s);
        std::vector<double> h;
        std::array<double, PB_COUNT> mix{};
        for (int a = -20; a <= 20; a++)
            for (int b = -20; b <= 20; b++) {
                const Macro m = G.macro(a * 250.0, b * 250.0);
                h.push_back(m.h);
                for (int k = 0; k < PB_COUNT; k++) mix[k] += m.w[k] / (41.0 * 41.0);
            }
        hs.push_back(h), mixes.push_back(mix);
        char line[200];
        std::snprintf(line, sizeof line, "seed %llu: meadow %.2f forest %.2f badlands %.2f mountains %.2f dunes %.2f", (unsigned long long)s, mix[0], mix[1], mix[2],
                      mix[3], mix[4]);
        ctx.note(line);
    }
    double minRms = 1e9, minMix = 1e9;
    for (size_t i = 0; i < hs.size(); i++)
        for (size_t j = i + 1; j < hs.size(); j++) {
            double s = 0, m = 0;
            for (size_t k = 0; k < hs[i].size(); k++) s += (hs[i][k] - hs[j][k]) * (hs[i][k] - hs[j][k]);
            for (int k = 0; k < PB_COUNT; k++) m += std::fabs(mixes[i][k] - mixes[j][k]);
            minRms = std::min(minRms, std::sqrt(s / hs[i].size()));
            minMix = std::min(minMix, m);
        }
    ctx.metric("smallest height difference between two seeds (rms over 10 x 10 km)", minRms, "m", 8, 1e9);
    ctx.metric("smallest difference in biome mix between two seeds (sum of |share|)", minMix, "", 0.1, 10);
}

SCENARIO(procBorders, "procworld/borders", "Chunk borders are seamless: the cells either side of every border of a 4 x 4 block of chunks step no more than the land does inside a chunk") {
    const ProcWorld& G = ProcWorld::forSeed(777);
    double worstBorder = 0, worstInside = 0;
    for (int64_t cx = -2; cx < 2; cx++)
        for (int64_t cz = -2; cz < 2; cz++) {
            const auto a = G.chunk(cx, cz), e = G.chunk(cx + 1, cz), n = G.chunk(cx, cz + 1);
            for (int k = 0; k < kChunk; k++) {
                worstBorder = std::max(worstBorder, (double)std::fabs(a->h[63 * kChunk + k] - e->h[0 * kChunk + k]));
                worstBorder = std::max(worstBorder, (double)std::fabs(a->h[k * kChunk + 63] - n->h[k * kChunk + 0]));
                for (int q = 0; q + 1 < kChunk; q++) worstInside = std::max(worstInside, (double)std::fabs(a->h[q * kChunk + k] - a->h[(q + 1) * kChunk + k]));
            }
        }
    ctx.metric("worst step across a chunk border", worstBorder, "m", 0, std::max(0.5, worstInside * 1.01));
    ctx.metric("worst step between neighbours inside a chunk", worstInside, "m", 0, 1e9);
}

SCENARIO(procWindowSlide, "procworld/window-slide", "Two windows a slide apart hold the same land where they overlap: heights, ground and every tree and rock, moved by exactly the offset") {
    World a = windowAt(4242, 0, 0), b = windowAt(4242, 512, -512);
    const double dx = (double)(b.proc.ox - a.proc.ox), dz = (double)(b.proc.oz - a.proc.oz);
    ctx.check("the second window is 512 m east and south", dx == 512 && dz == -512);
    double worst = 0;
    int ground = 0;
    for (double x = -900; x <= 900; x += 7)
        for (double z = -900; z <= 900; z += 7) {
            worst = std::max(worst, std::fabs(a.height(x, z) - b.height(x - dx, z - dz)));
            ground += a.surfaceAt(x, z) != b.surfaceAt(x - dx, z - dz);
        }
    ctx.metric("worst height difference where they overlap", worst, "m", 0, 1e-6);
    ctx.check("the same ground where they overlap", ground == 0, std::to_string(ground) + " cells differ");
    // Trees in the overlap (well inside both): each one in a is in b, moved by the offset.
    auto key = [](double x, double z) { return std::make_pair(std::llround(x * 100), std::llround(z * 100)); };
    std::set<std::pair<long long, long long>> inB;
    for (const Tree& t : b.trees) inB.insert(key(t.base.x + dx, t.base.z + dz));
    int checked = 0, missing = 0;
    for (const Tree& t : a.trees) {
        if (std::fabs(t.base.x - dx * 0.5) > 900 || std::fabs(t.base.z - dz * 0.5) > 900) continue;
        checked++;
        missing += !inB.count(key(t.base.x, t.base.z));
    }
    ctx.check("every tree in the overlap is in both", checked > 100 && missing == 0, std::to_string(missing) + " of " + std::to_string(checked) + " missing");
    int rocksA = 0, rocksMatched = 0;
    std::set<std::pair<long long, long long>> rocksB;
    for (const Rock& r : b.rocks) rocksB.insert(key(r.at.x + dx, r.at.z + dz));
    for (const Rock& r : a.rocks) {
        if (std::fabs(r.at.x - dx * 0.5) > 900 || std::fabs(r.at.z - dz * 0.5) > 900) continue;
        rocksA++;
        rocksMatched += rocksB.count(key(r.at.x, r.at.z)) > 0;
    }
    ctx.check("every rock in the overlap is in both", rocksA > 20 && rocksMatched == rocksA, std::to_string(rocksMatched) + " of " + std::to_string(rocksA));
    char line[160];
    std::snprintf(line, sizeof line, "window: %zu trees, %zu rocks, %zu bushes, %zu logs", a.trees.size(), a.rocks.size(), a.bushes.size(), a.logs.size());
    ctx.note(line);
}

SCENARIO(procFarOut, "procworld/far-out", "Far from the origin (100 km and 10,000 km) the land is as varied and as smooth as at home, and the window's local coordinates stay small") {
    const ProcWorld& G = ProcWorld::forSeed(31337);
    auto stats = [&](double ox, double oz, double& sd, double& step) {
        double s = 0, s2 = 0, worst = 0;
        int n = 0;
        for (int a = 0; a < 60; a++)
            for (int b = 0; b < 60; b++) {
                const double h = G.macroHeight(ox + a * 100.0, oz + b * 100.0);
                s += h, s2 += h * h, n++;
            }
        const auto c = G.chunk(proc::floorDiv((int64_t)ox, kChunk), proc::floorDiv((int64_t)oz, kChunk));
        for (int i = 0; i + 1 < kChunk; i++)
            for (int j = 0; j < kChunk; j++) worst = std::max(worst, (double)std::fabs(c->h[i * kChunk + j] - c->h[(i + 1) * kChunk + j]));
        sd = std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n)));
        step = worst;
    };
    double sd0, st0, sd1, st1, sd2, st2;
    stats(0, 0, sd0, st0);
    stats(100000, -100000, sd1, st1);
    stats(1e7, 1e7, sd2, st2);
    ctx.metric("relief at home (sd of heights over 6 km)", sd0, "m", 3, 400);
    ctx.metric("relief 100 km out", sd1, "m", 3, 400);
    ctx.metric("relief 10,000 km out", sd2, "m", 3, 400);
    ctx.metric("worst cell step 10,000 km out", st2, "m", 0, 2.5);
    World w = windowAt(31337, 100000, -100000);
    ctx.check("100 km out: the window's middle is a chunk corner there", w.proc.ox == 100032 && w.proc.oz == -99968,
              std::to_string(w.proc.ox) + "," + std::to_string(w.proc.oz));
    ctx.check("...and its land is the generator's there (local 10, 20 = global 100042, -99948)", w.height(10, 20) == (double)G.chunk(1563, -1562)->h[10 * 64 + 20]);
}

SCENARIO(procChunkBudget, "procworld/chunk-budget", "A chunk is made within its budget (the median of 64 fresh ones on one core), and a whole window builds in a few seconds") {
    const ProcWorld& G = ProcWorld::forSeed(2024);
    std::vector<double> ms;
    for (int k = 0; k < 64; k++) ms.push_back(G.makeChunk(300 + k % 8, -200 + k / 8)->genMs);
    std::sort(ms.begin(), ms.end());
    ctx.metric("median ms per chunk (one core; the budget is 6 ms alone, 8 with the suite running beside it)", ms[ms.size() / 2], "ms", 0, 8);
    ctx.metric("slowest of 64 (the ones that lay out new trails)", ms.back(), "ms", 0, 150);
    clearChunkCache();
    const auto t0 = std::chrono::steady_clock::now();
    World w = windowAt(2024, 5000, 5000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ctx.metric("a whole window, from nothing cached", s, "s", 0, 20);
    const auto t1 = std::chrono::steady_clock::now();
    World w2 = windowAt(2024, 5512, 5000);
    const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    ctx.metric("the next window, slid 512 m (the rest cached)", s2, "s", 0, 10);
}

SCENARIO(procRiversTrails, "procworld/rivers-trails", "A window has rivers with water, trails and fords; the water lies level across chunk borders and runs one way along a channel; trails are continuous treads") {
    int withWater = 0, withTrails = 0, fords = 0, seeds = 0;
    long borderPairs = 0, innerPairs = 0, borderSteps = 0, innerSteps = 0, treadPairs = 0, treadSteps = 0;
    double worstTread = 0;
    std::string worstAt;
    std::string where;
    for (uint64_t seed : {11ull, 22ull, 33ull, 44ull}) {
        World w = windowAt(seed, 0, 0);
        seeds++;
        int water = 0, tread = 0, ford = 0;
        const int n = w.gridSize();
        const double half = w.halfSize();
        auto onTread = [&](double x, double z) {
            const RoadInfo r = w.roadAt(x, z);
            return r.lateral != 0 && std::fabs(r.lateral) < 1.5 && (r.paint == PAINT_TRAIL || r.paint == PAINT_GRAVEL || r.paint == PAINT_SNOW);
        };
        for (int i = 1; i < n - 1; i += 1)
            for (int j = 1; j < n - 1; j += 1) {
                const double x = -half + i, z = -half + j;
                const double wl = w.waterLevel(x, z);
                const bool wet = wl > -1e8 && wl > w.height(x, z), tr = onTread(x, z);
                water += wet;
                tread += tr;
                if (wet && tr) {
                    ford++;
                    if (where.empty()) where = "seed " + std::to_string(seed) + " ford at " + std::to_string((int)x) + "," + std::to_string((int)z);
                }
                // The water's level between wet neighbours (east): steps (a creek running into a river) no more often at a chunk border than inside one.
                const double wl2 = w.waterLevel(x + 1, z);
                if (wet && wl2 > -1e8) {
                    const bool border = i % 64 == 63, step = std::fabs(wl2 - wl) > 0.05;
                    (border ? borderPairs : innerPairs)++;
                    (border ? borderSteps : innerSteps) += step;
                }
                if (tr && onTread(x + 1, z) && onTread(x - 1, z))  // (a kink: the grade changing by over 30% from one metre to the next)
                    treadPairs++, treadSteps += std::fabs(w.height(x + 1, z) - 2 * w.height(x, z) + w.height(x - 1, z)) > 0.3;
                if (tr && onTread(x + 1, z) && std::fabs(w.height(x + 1, z) - w.height(x, z)) > worstTread) {
                    worstTread = std::fabs(w.height(x + 1, z) - w.height(x, z));
                    char b[160];
                    std::snprintf(b, sizeof b, "worst tread step: seed %llu at %.0f,%.0f: %.2f -> %.2f (lat %.2f, %.2f)", (unsigned long long)seed, x, z, w.height(x, z),
                                  w.height(x + 1, z), w.roadAt(x, z).lateral, w.roadAt(x + 1, z).lateral);
                    worstAt = b;
                }
            }
        withWater += water > 500, withTrails += tread > 2000, fords += ford;
        ctx.note("seed " + std::to_string(seed) + ": " + std::to_string(water) + " wet cells, " + std::to_string(tread) + " trail cells, " + std::to_string(ford) + " in fords");
    }
    if (!where.empty()) ctx.note(where);
    ctx.note(worstAt);
    ctx.check("rivers with water in most windows", withWater >= 2, std::to_string(withWater) + " of " + std::to_string(seeds));
    ctx.check("trails in every window", withTrails == seeds);
    ctx.check("fords where trails cross water", fords > 0);
    const double fb = borderPairs ? (double)borderSteps / borderPairs : 0, fi = innerPairs ? (double)innerSteps / innerPairs : 0;
    ctx.metric("share of wet neighbours across a chunk border whose level steps", fb, "", 0, 2 * fi + 0.01);
    ctx.metric("...and inside a chunk (creeks joining rivers)", fi, "", 0, 1);
    ctx.metric("share of a trail's tread where the grade kinks by over 30% in a metre", treadPairs ? (double)treadSteps / treadPairs : 1, "", 0, 0.002);
    ctx.metric("worst step along a trail's tread (between neighbouring cells)", worstTread, "m", 0, 2.0);
}

SCENARIO(procPlaces, "procworld/places", "Places at the trail nodes: an outpost within ~1.1 km of anywhere, ids unique and names unique within 20 km, every place on flat dry ground clear of trees, the same in every window that holds it, the start at an outpost") {
    const ProcWorld& G = ProcWorld::forSeed(5150);
    // Names and ids over 41 x 41 nodes (20 km): no two alike.
    std::set<std::string> ids, names;
    int dupIds = 0, dupNames = 0, outposts = 0, caches = 0, kinds[PK_COUNT] = {};
    for (int64_t i = -20; i <= 20; i++)
        for (int64_t j = -20; j <= 20; j++) {
            std::vector<MapPlace> v;
            G.nodePlaces(i, j, v);
            for (const MapPlace& p : v) {
                dupIds += !ids.insert(p.id).second;
                dupNames += !names.insert(p.name).second;
                outposts += p.kind == PK_RANGER_STATION, caches += p.kind == PK_CACHE;
                kinds[p.kind]++;
            }
        }
    ctx.check("ids unique", dupIds == 0, std::to_string(dupIds));
    ctx.metric("names repeated within 20 km", dupNames, "", 0, 0);
    ctx.metric("outposts per 100 km2 (one per 3 x 3 cells: ~42)", outposts * 100.0 / (41 * 41 * 0.512 * 0.512), "", 30, 60);
    ctx.metric("caches per 100 km2", caches * 100.0 / (41 * 41 * 0.512 * 0.512), "", 50, 400);
    std::string mix;
    for (int k = 0; k < PK_COUNT; k++)
        if (kinds[k]) mix += std::string(placeKindName(k)) + " " + std::to_string(kinds[k]) + ", ";
    ctx.note(mix);
    // The farthest any point is from an outpost (over a 6 km square, every 100 m).
    double worst = 0;
    for (int a = -30; a <= 30; a++)
        for (int b = -30; b <= 30; b++) {
            const double x = a * 100.0, z = b * 100.0;
            double best = 1e18;
            for (int64_t i = floorDiv((int64_t)x, kCell) - 3; i <= floorDiv((int64_t)x, kCell) + 3; i++)
                for (int64_t j = floorDiv((int64_t)z, kCell) - 3; j <= floorDiv((int64_t)z, kCell) + 3; j++) {
                    const TrailNode n = G.node(i, j);
                    if (n.outpost) best = std::min(best, std::hypot(n.x - x, n.z - z));
                }
            worst = std::max(worst, best);
        }
    ctx.metric("farthest from an outpost", worst, "m", 0, 1400);
    // In a window: on flat, dry ground with nothing on it; and as the next window has them.
    World a = windowAt(5150, 0, 0), b = windowAt(5150, 512, 0);
    const std::vector<MapPlace> pa = windowPlaces(a), pb = windowPlaces(b);
    int bad = 0, checked = 0, matched = 0, inBoth = 0;
    std::string why;
    for (const MapPlace& p : pa) {
        if (p.kind == PK_CACHE || p.kind == PK_RIVER_CROSSING || p.kind == PK_ABANDONED_VEHICLE) continue;
        checked++;
        const double h = a.height(p.p.x, p.p.z);
        double rough = 0;
        for (int k = 0; k < 8; k++) {
            const double r = std::max(3.0, p.pad - 3), ang = k * 0.785398;
            rough = std::max(rough, std::fabs(a.height(p.p.x + r * std::cos(ang), p.p.z + r * std::sin(ang)) - h));
        }
        const bool wet = a.waterLevel(p.p.x, p.p.z) > h;
        if (rough > 0.6 || wet) bad++, why += " " + p.id + (wet ? " wet" : " rough " + std::to_string(rough));
        if (std::fabs(p.p.x - 512) < 1300) {
            inBoth++;
            for (const MapPlace& q : pb) matched += q.id == p.id && std::fabs(q.p.x + 512 - p.p.x) < 1e-6 && std::fabs(q.p.z - p.p.z) < 1e-6;
        }
    }
    ctx.check("every place on level, dry ground", checked > 10 && bad == 0, std::to_string(bad) + " of " + std::to_string(checked) + why);
    ctx.check("the next window has the same places in the same spots", inBoth > 5 && matched == inBoth, std::to_string(matched) + " of " + std::to_string(inBoth));
    int stations = 0;
    for (const Station& s : a.stations) stations++;
    ctx.check("the outposts' repair stations are built", stations > 0, std::to_string(stations));
    // The start: on an outpost's lot.
    World::procSeed = 5150, World::procOriginSet = false;
    World s(MAP_PROCEDURAL);
    double near = 1e9;
    for (const Station& st : s.stations) near = std::min(near, std::hypot(st.p.x - s.spawn.x, st.p.z - s.spawn.z));
    ctx.metric("the start from the nearest repair station", near, "m", 5, 40);
    setActiveWindow(s);
    ctx.check("mapPlaces(PROCEDURAL) is the window's", !mapPlaces(MAP_PROCEDURAL).empty() && mapPlaces(MAP_PROCEDURAL).size() == windowPlaces(s).size());
    ctx.note(std::string("region at the start: ") + regionAt(MAP_PROCEDURAL, s.spawn.x, s.spawn.z));
    // Found places and opened caches are kept by the world's seed and the place's id (another world's place of the same name isn't
    // found), and shown by name.
    const std::vector<DiscoverSpot> spots = discoverSpots(MAP_PROCEDURAL);
    bool keyed = !spots.empty();
    for (const DiscoverSpot& d : spots) keyed &= d.name.rfind("5150#PW.", 0) == 0 && !d.label.empty() && d.label.find("PW.") == std::string::npos;
    ctx.check("discoveries kept by seed and place id, shown by name", keyed, spots.empty() ? "none" : spots[0].name + " / " + spots[0].shown());
    std::vector<std::string> found;
    const std::vector<std::string> fresh = visitSpots(found, MAP_PROCEDURAL, spots[0].p, spots);
    ctx.check("...and found as such", !fresh.empty() && found[0] == std::to_string(MAP_PROCEDURAL) + ":" + spots[0].name);
}

// A map of a window from above (CZ_PROC_MAPS=dir: written there as map_seed<N>.ppm): the land shaded, its ground, water, trails, trees.
SCENARIO(procMapImages, "procworld/map-images", "(with CZ_PROC_MAPS=dir) top-down maps of a few seeds' first windows, for looking at") {
    const char* dir = std::getenv("CZ_PROC_MAPS");
    if (!dir) {
        ctx.note("CZ_PROC_MAPS not set: nothing drawn");
        return;
    }
    for (uint64_t seed : {1ull, 2ull, 11ull, 123456789ull}) {
        World::procSeed = seed, World::procOriginSet = false;
        World w(MAP_PROCEDURAL);
        const int S = 1024, n = w.gridSize();
        const double half = w.halfSize(), step = 2 * half / S;
        std::vector<unsigned char> img((size_t)S * S * 3);
        for (int py = 0; py < S; py++)
            for (int px = 0; px < S; px++) {
                const double x = -half + (px + 0.5) * step, z = half - (py + 0.5) * step;
                const double h = w.height(x, z), hx = w.height(x + step, z), hz = w.height(x, z + step);
                const double shade = std::clamp(0.75 + 0.9 * ((hx - h) - (hz - h)) / step, 0.35, 1.25);
                const RoadInfo r = w.roadAt(x, z);
                const Surface s = w.surfaceAt(x, z);
                double c[3] = {0.36, 0.52, 0.22};
                auto set = [&](double a, double b, double d) { c[0] = a, c[1] = b, c[2] = d; };
                if (r.paint == PAINT_DESERT) set(0.78, 0.6, 0.42);
                else if (r.paint == PAINT_REDROCK) set(0.62, 0.3, 0.18);
                else if (r.paint == PAINT_SAND) set(0.88, 0.78, 0.55);
                else if (r.paint == PAINT_SNOW) set(0.95, 0.95, 0.98);
                else if (r.paint == PAINT_SCREE) set(0.55, 0.53, 0.5);
                else if (s == SURF_ROCK) set(0.5, 0.48, 0.45);
                for (double& v : c) v *= shade * (0.8 + 0.2 * std::clamp(h / 150, 0.0, 1.0));
                if (r.lateral != 0 && std::fabs(r.lateral) < 2.5 && (r.paint == PAINT_TRAIL || r.paint == PAINT_GRAVEL)) set(r.paint == PAINT_GRAVEL ? 0.95 : 0.55, r.paint == PAINT_GRAVEL ? 0.9 : 0.35, 0.2);
                const double wl = w.waterLevel(x, z);
                if (wl > h) set(0.15, 0.35, 0.75);
                for (int k = 0; k < 3; k++) img[((size_t)py * S + px) * 3 + k] = (unsigned char)std::clamp(c[k] * 255, 0.0, 255.0);
            }
        {  // (where to look: the nearest water and the nearest ford to the start)
            double bw = 1e18, bf = 1e18;
            V3 pw, pf;
            for (double x = -half + 2; x < half - 2; x += 2)
                for (double z = -half + 2; z < half - 2; z += 2) {
                    if (w.waterLevel(x, z) <= w.height(x, z)) continue;
                    const double d = std::hypot(x - w.spawn.x, z - w.spawn.z);
                    if (d < bw) bw = d, pw = V3(x, 0, z);
                    const RoadInfo r = w.roadAt(x, z);
                    if (r.lateral != 0 && std::fabs(r.lateral) < 1.5 && d < bf) bf = d, pf = V3(x, 0, z);
                }
            char b[200];
            std::snprintf(b, sizeof b, "seed %llu: start %.0f,%.0f; nearest water %.0f,%.0f (%.0f m); nearest ford %.0f,%.0f (%.0f m)", (unsigned long long)seed, w.spawn.x,
                          w.spawn.z, pw.x, pw.z, bw, pf.x, pf.z, bf);
            ctx.note(b);
        }
        for (const Tree& t : w.trees) {  // (a dark dot each)
            const int px = (int)((t.base.x + half) / step), py = (int)((half - t.base.z) / step);
            if (px >= 0 && py >= 0 && px < S && py < S)
                for (int k = 0; k < 3; k++) img[((size_t)py * S + px) * 3 + k] = (unsigned char)(img[((size_t)py * S + px) * 3 + k] * 0.45);
        }
        for (const MapPlace& p : windowPlaces(w)) {  // (a square each: outposts red, fuel orange, caches yellow, fords cyan, the rest white)
            const int px = (int)((p.p.x + half) / step), py = (int)((half - p.p.z) / step), r = p.kind == PK_RANGER_STATION ? 5 : 3;
            const unsigned char col[3] = {(unsigned char)(p.kind == PK_RIVER_CROSSING ? 0 : 255),
                                          (unsigned char)(p.kind == PK_RANGER_STATION ? 0 : p.kind == PK_FUEL_STOP ? 140 : 255),
                                          (unsigned char)(p.kind == PK_RANGER_STATION || p.kind == PK_FUEL_STOP || p.kind == PK_CACHE ? 0 : 255)};
            for (int a = -r; a <= r; a++)
                for (int b = -r; b <= r; b++)
                    if (px + a >= 0 && py + b >= 0 && px + a < S && py + b < S)
                        for (int k = 0; k < 3; k++) img[((size_t)(py + b) * S + px + a) * 3 + k] = col[k];
        }
        const std::string path = std::string(dir) + "/map_seed" + std::to_string(seed) + ".ppm";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", S, S);
            std::fwrite(img.data(), 1, img.size(), f);
            std::fclose(f);
            ctx.note("wrote " + path + " (" + std::to_string(n) + " cells a side, north up)");
        }
    }
    World::procOriginSet = false;
}

SCENARIO(procCareerHook, "procworld/career-hook", "The career's world save line: a new world's seed, and a saved one built again round where it was left") {
    ctx.check("not a procedural line: refused", !ProcWorld::useSaveLine("map=backcountry"));
    ctx.check("a new world's line", ProcWorld::useSaveLine("map=procedural seed=777") && World::procSeed == 777 && !World::procOriginSet);
    World a(MAP_PROCEDURAL);
    World b = windowAt(777, (double)a.proc.ox + 1024, (double)a.proc.oz - 512);
    const std::string line = ProcWorld::saveLine(b);
    ctx.note(line);
    ctx.check("a saved one's line read back", ProcWorld::useSaveLine(line) && World::procSeed == 777 && World::procOriginSet);
    World c(MAP_PROCEDURAL);
    World::procOriginSet = false;
    ctx.check("...builds the same window", c.proc.ox == b.proc.ox && c.proc.oz == b.proc.oz && c.landHash() == b.landHash());
    const V3 g = ProcWorld::toGlobal(c, V3(10.5, 3, -20.25));
    const V3 l = ProcWorld::toLocal(c, g);
    ctx.check("global and window coordinates, there and back", l.x == 10.5 && l.z == -20.25 && g.x == (double)c.proc.ox + 10.5);
}

SCENARIO(procNetSlide, "procworld/net-slide", "Multiplayer on the procedural world: the guest is told the host's seed, window and generator; the host slides everyone's window at a moment on the shared clock and the guest's truck, seen on the host, doesn't jump") {
    const uint64_t seed = 6060;
    ctx.sim.world = windowAt(seed, 0, 0);
    const V3 at = ctx.sim.world.spawn;
    Car& hostCar = ctx.add(p4Runner(), at.x + 12, at.z - 10, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = 27900 + (int)(getpid() % 90);
    ctx.check("host opens a session on the procedural world", host.host(port, "HOSTY", MAP_PROCEDURAL), host.error);
    host.proc.on = true, host.proc.gen = (uint16_t)kGenVersion, host.proc.seed = seed, host.proc.ox = ctx.sim.world.proc.ox, host.proc.oz = ctx.sim.world.proc.oz;
    host.adopt(ctx.sim);
    NetSession client;
    bool joined = false, done = false;
    std::thread joiner([&] {
        joined = client.join("127.0.0.1:" + std::to_string(port), "BUDDY", 3.0);
        done = true;
    });
    for (int k = 0; k < 400 && !done; k++) host.receive(ctx.sim, ctx.t), usleep(5000);
    joiner.join();
    ctx.check("guest let in", joined, client.error);
    if (!joined) return;
    ctx.check("guest told the seed, window and generator", client.map == MAP_PROCEDURAL && client.proc.on && client.proc.seed == seed &&
                                                           client.proc.ox == ctx.sim.world.proc.ox && client.proc.gen == kGenVersion);
    Sim sim2;
    sim2.world = windowAt(client.proc.seed, (double)client.proc.ox, (double)client.proc.oz);
    sim2.setupEmpty();
    sim2.addCar(p4Runner(), V3(at.x - 10, sim2.world.clearY(at.x - 10, at.z + 8) + 0.6, at.z + 8), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    sim2.cars[0].gear = 1;
    client.adopt(sim2);
    // The next window (512 m east), made ahead on both machines.
    World nextHost = windowAt(seed, (double)ctx.sim.world.proc.ox + 512, (double)ctx.sim.world.proc.oz);
    World nextGuest = windowAt(seed, (double)sim2.world.proc.ox + 512, (double)sim2.world.proc.oz);
    const V3 d((double)(ctx.sim.world.proc.ox - nextHost.proc.ox), 0, 0);
    bool hostSwapped = false, guestSwapped = false, guestTold = false;
    double worst = 0, due = -1;
    auto frameBoth = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        for (const NetSession::GameMsg& m : client.gameIn)
            if (m.text.rfind("procshift ", 0) == 0) {
                unsigned long long sd = 0;
                long long ox = 0, oz = 0;
                double t = 0;
                if (std::sscanf(m.text.c_str() + 10, "%llu %lld %lld %lf", &sd, &ox, &oz, &t) == 4)
                    guestTold = true, client.scheduleShift(t, V3((double)(sim2.world.proc.ox - ox), 0, (double)(sim2.world.proc.oz - oz)));
            }
        client.gameIn.clear();
        // Each machine swaps at the named moment on its own idea of the shared clock.
        if (due >= 0 && !hostSwapped && host.sharedTime(ctx.t) >= due) ctx.sim.world = std::move(nextHost), shiftSim(ctx.sim, d), host.shiftNow(), hostSwapped = true;
        if (client.shiftPending() && !guestSwapped && client.sharedTime(ctx.t) >= client.shiftDue()) sim2.world = std::move(nextGuest), shiftSim(sim2, d), client.shiftNow(), guestSwapped = true;
        sim2.cars[0].throttle = 0.3;
        sim2.advance(Ctx::kFrame, 1000);
        client.send(sim2, ctx.t);
        // Where the host shows the guest's truck against where it is, in world coordinates.
        const Car* r = nullptr;
        for (const Car& c : ctx.sim.cars)
            if (c.remote) r = &c;
        if (r && ctx.t > 2.0) {
            const V3 shown = ProcWorld::toGlobal(ctx.sim.world, r->frame().com), real = ProcWorld::toGlobal(sim2.world, sim2.cars[0].frame().com);
            worst = std::max(worst, len(V3(shown.x - real.x, 0, shown.z - real.z)));
            if (std::getenv("CZ_NETSLIDE_LOG")) std::printf("t %.2f host %.2f guest %.2f due %.2f swapped %d %d err %.2f told %d\n", ctx.t, host.sharedTime(ctx.t), client.sharedTime(ctx.t), due, hostSwapped, guestSwapped, len(V3(shown.x - real.x, 0, shown.z - real.z)), guestTold);
        }
        usleep(300);
    };
    ctx.run(2.5, frameBoth);
    due = host.sharedTime(ctx.t) + 1.0;
    char msg[128];
    std::snprintf(msg, sizeof msg, "procshift %llu %lld %lld %.4f", (unsigned long long)seed, (long long)(ctx.sim.world.proc.ox + 512), (long long)ctx.sim.world.proc.oz, due);
    host.sendGame(msg);
    host.scheduleShift(due, d);
    {  // (a third player joining in the moment before the slide is told it's coming, and where to)
        NetSession late;
        bool lj = false, ld = false;
        std::thread j2([&] {
            lj = late.join("127.0.0.1:" + std::to_string(port), "LATE", 3.0);
            ld = true;
        });
        for (int k = 0; k < 400 && !ld; k++) host.receive(ctx.sim, ctx.t), client.receive(sim2, ctx.t), usleep(5000);
        j2.join();
        ctx.check("a guest joining before the slide is told it's coming", lj && late.proc.sliding && late.proc.tx == ctx.sim.world.proc.ox + 512 &&
                                                                        late.proc.tz == ctx.sim.world.proc.oz && std::fabs(late.proc.due - due) < 1e-6,
                  late.error);
        late.close();
    }
    ctx.run(3.0, frameBoth);
    ctx.check("the guest was told and both slid", guestTold && hostSwapped && guestSwapped);
    ctx.metric("the guest's truck as the host shows it vs where it is, through the slide", worst, "m", 0, 1.5);
    client.close();
    host.close();
}

SCENARIO(procDriveSlide, "procworld/drive-slide", "100 km out, a truck driving along while the window slides under it (everything moved by the offset) goes on exactly as the same truck with no slide") {
    // Two sims on the same window; at 1 s one of them gets the next window (512 m east) and is moved by the offset.
    auto setup = [&](Sim& s) {
        s.world = windowAt(9090, 100000, -100000);
        const V3 at = s.world.spawn;
        s.addCar(p4Runner(), V3(at.x, s.world.clearY(at.x, at.z) + 0.6, at.z), 0, V3(0.5, 0.5, 0.5));
        s.cars[0].gear = 1;
    };
    Sim a, b;
    setup(a), setup(b);
    Ctx ca(a), cb(b);
    ca.run(1.5), cb.run(1.5);  // (settle)
    for (Car* c : {&a.cars[0], &b.cars[0]}) c->throttle = 0.35;
    ca.run(1.0), cb.run(1.0);
    World next = windowAt(9090, (double)b.world.proc.ox + 512, (double)b.world.proc.oz);
    const V3 d((double)(b.world.proc.ox - next.proc.ox), 0, (double)(b.world.proc.oz - next.proc.oz));
    b.world = std::move(next);
    shiftSim(b, d);
    ca.run(3.0), cb.run(3.0);
    const V3 pa = a.cars[0].frame().com, pb = b.cars[0].frame().com - d;
    ctx.metric("where the two trucks are apart after 3 s more", len(pa - pb), "m", 0, 0.05);
    ctx.metric("speed of the one that slid", len(b.cars[0].frame().vel), "m/s", 0.5, 30);
    ctx.check("it moved under its own power (not stuck or flung)", len(pa - b.cars[0].frame().com + d) < 0.05 && std::isfinite(pb.x));
}

SCENARIO(procStartPad,"procworld/start-pad", "Round the start (its pad, the ground and what's cleared from in front of it) every window that holds it is the same") {
    World::procSeed = 12345, World::procOriginSet = false;
    World a(MAP_PROCEDURAL);  // (round the start)
    World b = windowAt(12345, (double)a.proc.ox + 64, (double)a.proc.oz + 128);
    const double dx = (double)(b.proc.ox - a.proc.ox), dz = (double)(b.proc.oz - a.proc.oz);
    ctx.check("both have the start pad", !a.spawnPads.empty() && !b.spawnPads.empty());
    if (!a.spawnPads.empty() && !b.spawnPads.empty())
        ctx.check("...in the same place", std::fabs(a.spawnPads[0].p.x - (b.spawnPads[0].p.x + dx)) < 1e-6 && std::fabs(a.spawnPads[0].p.z - (b.spawnPads[0].p.z + dz)) < 1e-6,
                  std::to_string(a.spawnPads[0].p.x) + "," + std::to_string(a.spawnPads[0].p.z) + " vs " + std::to_string(b.spawnPads[0].p.x + dx) + "," +
                      std::to_string(b.spawnPads[0].p.z + dz));
    int hd = 0, pd = 0;
    for (double x = a.spawn.x - 80; x <= a.spawn.x + 80; x += 1)
        for (double z = a.spawn.z - 80; z <= a.spawn.z + 80; z += 1) {
            hd += std::fabs(a.height(x, z) - b.height(x - dx, z - dz)) > 1e-6;
            pd += a.roadAt(x, z).paint != b.roadAt(x - dx, z - dz).paint;
        }
    ctx.check("the same heights round it", hd == 0, std::to_string(hd));
    ctx.check("the same paint round it", pd == 0, std::to_string(pd));
    auto count = [&](const World& w, double ox, double oz) {
        int t = 0, r = 0, bu = 0;
        for (const Tree& q : w.trees) t += std::hypot(q.base.x + ox - a.spawn.x, q.base.z + oz - a.spawn.z) < 80;
        for (const Rock& q : w.rocks) r += std::hypot(q.at.x + ox - a.spawn.x, q.at.z + oz - a.spawn.z) < 80;
        for (const Bush& q : w.bushes) bu += std::hypot(q.p.x + ox - a.spawn.x, q.p.z + oz - a.spawn.z) < 80;
        return std::to_string(t) + " trees " + std::to_string(r) + " rocks " + std::to_string(bu) + " bushes";
    };
    ctx.check("the same things round it", count(a, 0, 0) == count(b, dx, dz), count(a, 0, 0) + " vs " + count(b, dx, dz));
}
