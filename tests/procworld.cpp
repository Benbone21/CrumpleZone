// The procedural world (src/procworld.h, docs/superpowers/specs/2026-10-25-procedural-world-design.md): the same seed and chunk
// always make the same bytes, other seeds make other land, chunk borders are seamless, the window slides without the land moving
// under a vehicle, far out is the same as near, and a chunk is made within its budget.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <array>
#include <map>
#include <set>

#include "../src/procworld.h"
#include "../src/procrules.h"
#include "../src/net.h"
#include "../src/props.h"
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
                const Macro m = G.macroCached(a * 250.0, b * 250.0);  // (the land before its wear where no tile is made: seeds differ either way)
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
                for (int q = 0; q + 1 < kChunk; q++)  // (both ways: the border's steps are both ways too, and a steep slope may run either)
                    worstInside = std::max({worstInside, (double)std::fabs(a->h[q * kChunk + k] - a->h[(q + 1) * kChunk + k]), (double)std::fabs(a->h[k * kChunk + q] - a->h[k * kChunk + q + 1])});
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
    // (v6: a range every ~4 km, so 6 km square often holds a range's crest and the valleys either side: up to ~470 m; was 400)
    ctx.metric("relief at home (sd of heights over 6 km)", sd0, "m", 3, 600);
    ctx.metric("relief 100 km out", sd1, "m", 3, 600);
    ctx.metric("relief 10,000 km out", sd2, "m", 3, 600);
    ctx.metric("worst cell step 10,000 km out", st2, "m", 0, 2.5);
    World w = windowAt(31337, 100000, -100000);
    ctx.check("100 km out: the window's middle is a chunk corner there", w.proc.ox == 100032 && w.proc.oz == -99968,
              std::to_string(w.proc.ox) + "," + std::to_string(w.proc.oz));
    ctx.check("...and its land is the generator's there (local 10, 20 = global 100042, -99948)", w.height(10, 20) == (double)G.chunk(1563, -1562)->h[10 * 64 + 20]);
}

SCENARIO(procChunkBudget, "procworld/chunk-budget", "A chunk is made within its budget (the median of 64 fresh ones on one core), and a whole window builds in a few seconds") {
    const ProcWorld& G = ProcWorld::forSeed(2024);
    std::vector<double> ms;
    double edgeMax = 0;
    for (int k = 0; k < 64; k++) {
        auto c = G.makeChunk(300 + k % 8, -200 + k / 8);
        ms.push_back(c->genMs), edgeMax = std::max(edgeMax, c->edgeMs);
    }
    // (The trails a chunk needs are laid out ahead on the generator's quiet thread (prefetchErosion), like the worn land: not chunk time.)
    ctx.note("the most a chunk spent laying out trails nobody had laid out ahead: " + std::to_string((int)edgeMax) + " ms (in the game: ahead, on the quiet thread)");
    std::sort(ms.begin(), ms.end());
    ctx.metric("median ms per chunk (one core; the budget is 6 ms alone, 8 with the suite running beside it)", ms[ms.size() / 2], "ms", 0, 8);
    ctx.metric("slowest of 64 (its own work; its trails laid out ahead)", ms.back(), "ms", 0, 400);
    clearChunkCache();
    const auto t0 = std::chrono::steady_clock::now();
    World w = windowAt(2024, 5000, 5000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // (v8: the creeks' second levels read the land their first levels made: a second set of the channels' tiles, ~20% more for a
    // window from nothing (built on the streaming thread, not in a frame). So 25 s.)
    ctx.metric("a whole window, from nothing cached", s, "s", 0, 25);
    const auto t1 = std::chrono::steady_clock::now();
    World w2 = windowAt(2024, 5512, 5000);
    const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    ctx.metric("the next window, slid 512 m (the rest cached)", s2, "s", 0, 10);
}

SCENARIO(procRiversTrails, "procworld/rivers-trails", "A window has rivers with water, trails and fords; the water lies level across chunk borders and runs one way along a channel; trails are continuous treads") {
    int withWater = 0, withTrails = 0, fords = 0, seeds = 0;
    long borderPairs = 0, innerPairs = 0, borderSteps = 0, innerSteps = 0, treadPairs = 0, treadSteps = 0;
    double worstTread = 0, worstKinkV = 0;
    std::string worstKink;
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
                {
                    const double kink = std::fabs(w.height(x + 1, z) - 2 * w.height(x, z) + w.height(x - 1, z));
                    treadPairs++, treadSteps += kink > 0.3;
                    if (kink > worstKinkV) {
                        worstKinkV = kink;
                        char b[120];
                        std::snprintf(b, sizeof b, "%.2f at seed %llu %.0f,%.0f", kink, (unsigned long long)seed, x, z);
                        worstKink = b;
                    }
                }
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
    ctx.note("tread kinks: " + std::to_string(treadSteps) + " of " + std::to_string(treadPairs) + (worstKink.empty() ? "" : "; the worst " + worstKink));
    ctx.metric("share of a trail's tread where the grade kinks by over 30% in a metre", treadPairs ? (double)treadSteps / treadPairs : 1, "", 0, 0.006);  // (mountain trails: switchbacks and crossings)
    ctx.metric("worst step along a trail's tread (between neighbouring cells)", worstTread, "m", 0, 2.5);  // (where trails cross or a switchback runs by)
}

SCENARIO(procPlaces, "procworld/places", "Places at the trail nodes: an outpost within ~1.1 km of anywhere, ids unique and names unique within 20 km, every place on flat dry ground clear of trees, the same in every window that holds it, the start at an outpost") {
    const ProcWorld& G = ProcWorld::forSeed(5150);
    // Names and ids over 41 x 41 nodes (20 km): no two alike.
    std::set<std::string> ids, names;
    int dupIds = 0, dupNames = 0, outposts = 0, caches = 0, kinds[PK_COUNT] = {}, wrecks = 0, wreckLoot = 0;
    for (int64_t i = -20; i <= 20; i++)
        for (int64_t j = -20; j <= 20; j++) {
            std::vector<MapPlace> v;
            G.nodePlaces(i, j, v);
            for (const MapPlace& p : v) {
                dupIds += !ids.insert(p.id).second;
                if (p.vehicle == "wreck") { wrecks++, wreckLoot += p.reward > 0; continue; }  // (the wrecks: unlabelled scenery, some with loot)
                dupNames += !names.insert(p.name).second;
                outposts += p.kind == PK_RANGER_STATION, caches += p.kind == PK_CACHE;
                kinds[p.kind]++;
            }
        }
    ctx.check("ids unique", dupIds == 0, std::to_string(dupIds));
    ctx.metric("names repeated within 20 km", dupNames, "", 0, 0);
    ctx.metric("outposts per 100 km2 (one per 3 x 3 cells: ~42)", outposts * 100.0 / (41 * 41 * 0.512 * 0.512), "", 30, 60);
    ctx.metric("caches per 100 km2", caches * 100.0 / (41 * 41 * 0.512 * 0.512), "", 50, 400);
    ctx.metric("wrecks per 100 km2 (some with loot)", wrecks * 100.0 / (41 * 41 * 0.512 * 0.512), "", 100, 1000);
    ctx.check("some wrecks hold loot", wreckLoot > 0 && wreckLoot < wrecks);
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
        if (p.kind == PK_CACHE || p.kind == PK_RIVER_CROSSING || p.kind == PK_ABANDONED_VEHICLE || p.kind == PK_WATERFALL) continue;
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

SCENARIO(procWalls, "procworld/no-walls", "Mountains, valleys and trails without walls: the ground steeper than 2:1 is rare, and never a cliff a metre across") {
    // (A deep valley that ends short, two trails' treads at different heights side by side, or the land past a trail's earthworks
    // would each stand as a wall tens of metres high: the steepest ground a metre across tells.)
    for (uint64_t seed : {2ull, 123456789ull}) {
        World::procSeed = seed, World::procOriginSet = false;
        World w(MAP_PROCEDURAL);
        const double half = w.halfSize();
        long steep = 0, total = 0;
        double worst = 0, wx = 0, wz = 0;
        for (double x = -half + 2; x < half - 2; x += 2)
            for (double z = -half + 2; z < half - 2; z += 2) {
                const double h = w.height(x, z), s = std::max(std::fabs(w.height(x + 1, z) - h), std::fabs(w.height(x, z + 1) - h));
                if (s > 2) {  // (v7: a mesa's sheer band is meant (owner: Monument Valley cliffs): not counted)
                    const V3 g = ProcWorld::toGlobal(w, V3(x, 0, z));
                    if (ProcWorld::forSeed(seed).macroAt(g.x, g.z, false).w[PB_BADLANDS] > 0.1) continue;
                }
                total++, steep += s > 2;
                if (s > worst) worst = s, wx = x, wz = z;
            }
        char b[200];
        std::snprintf(b, sizeof b, "seed %llu: %.2f%% of the ground steeper than 2:1; the steepest %.1f m in a metre (at %.0f, %.0f)", (unsigned long long)seed,
                      100.0 * steep / total, worst, wx, wz);
        ctx.note(b);
        ctx.check(std::string("seed ") + std::to_string(seed) + ": under 1% steeper than 2:1", steep < total / 100);
        ctx.check(std::string("seed ") + std::to_string(seed) + ": no cliff over 15 m a metre across", worst < 15);
    }
}

SCENARIO(procGrades, "procworld/grades", "The roads' and trails' grades over 12 x 12 cells (6 km square) of a few seeds: the steepest, and how much is past each rating's limit") {
    // (Where a climb couldn't keep to its rating's grade within reach of its nodes, the grade was relaxed rather than leave the
    // tread far above or below the land: measured, not held. The worst stretch is over 20 m, so one 2 m sample can't make it.)
    const double lim[3] = {0.15, 0.22, 0.32};
    double roadLen = 0, roadOver = 0, roadWorst = 0, trailLen = 0, trailOver = 0, trailWorst = 0;
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -6; i < 6; i++)
            for (int64_t j = -6; j < 6; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on || e->y.size() < 12) continue;
                    for (size_t k = 0; k + 10 < e->y.size(); k++) {
                        const double run = e->along[k + 10] - e->along[k], gr = std::fabs(e->y[k + 10] - e->y[k]) / std::max(1e-6, run);
                        const double seg = e->along[k + 1] - e->along[k];
                        if (e->road) roadLen += seg, roadOver += gr > 0.12 ? seg : 0, roadWorst = std::max(roadWorst, gr);
                        else trailLen += seg, trailOver += gr > lim[std::min(2, std::max(0, e->rating))] ? seg : 0, trailWorst = std::max(trailWorst, gr);
                    }
                }
    }
    char b[300];
    std::snprintf(b, sizeof b, "roads: %.0f km, the steepest %.0f%%, %.1f%% of their length over 12%%", roadLen / 1000, 100 * roadWorst, 100 * roadOver / std::max(1.0, roadLen));
    ctx.note(b);
    std::snprintf(b, sizeof b, "trails: %.0f km, the steepest %.0f%%, %.1f%% of their length over their rating's limit", trailLen / 1000, 100 * trailWorst, 100 * trailOver / std::max(1.0, trailLen));
    ctx.note(b);
    ctx.check("there are roads and trails", roadLen > 1000 && trailLen > 1000);
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
    // (v6: round the world's start, on its gentle lot: the origin itself is now as like as not on a range's steep side)
    int64_t sx0, sz0;
    double syaw0;
    ProcWorld::forSeed(seed).start(sx0, sz0, syaw0);
    ctx.sim.world = windowAt(seed, (double)sx0, (double)sz0);
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
    // (Somewhere gentle to drive, 100 km out: the first of a row of spots with level ground round it; the ranges are steep.)
    double gx = 100000, gz = -100000;
    {
        const ProcWorld& G = ProcWorld::forSeed(9090);
        for (int k = 0; k < 256; k++, gx += 512) {
            const double h = G.groundHeight(gx, gz);
            double worst = 0;
            for (double a = -40; a <= 40; a += 20)
                for (double c = -40; c <= 40; c += 20) worst = std::max(worst, std::fabs(G.groundHeight(gx + a, gz + c) - h));
            for (double a = 5; a <= 45; a += 5)  // (v6: the knolls: its way east no steeper than 1:4 anywhere)
                worst = std::max(worst, 16 * std::fabs(G.groundHeight(gx + a, gz) - G.groundHeight(gx + a - 5, gz)) / 5);
            // (...and a clear way east from where it starts: no tree, log, stump or rock within 4 m of its first 40 m.)
            int64_t ox, oz;
            ProcWorld::snapOrigin(gx, gz, ox, oz);
            bool clear = true;
            for (int64_t cx = floorDiv(ox - 8, kChunk); cx <= floorDiv(ox + 48, kChunk); cx++)
                for (int64_t cz = floorDiv(oz - 8, kChunk); cz <= floorDiv(oz + 8, kChunk); cz++) {
                    auto c = G.chunk(cx, cz);
                    auto blocks = [&](double x, double z, double r) {
                        const double gx2 = (double)(cx * kChunk) + x - (double)ox, gz2 = (double)(cz * kChunk) + z - (double)oz;
                        return gx2 > -4 && gx2 < 44 && std::fabs(gz2) < 4 + r;
                    };
                    for (const TreeSpec& t : c->trees) clear &= !blocks(t.x, t.z, t.radius);
                    for (const StumpSpec& t : c->stumps) clear &= !blocks(t.x, t.z, t.r);
                    for (const RockSpec& r : c->rocks) clear &= !blocks(r.x, r.z, std::max(r.rx, r.rz));  // (rocks too: the slopes have more now)
                    for (const LogSpec& l : c->logs) clear &= !blocks(l.ax, l.az, 1) && !blocks(l.bx, l.bz, 1) && !blocks((l.ax + l.bx) / 2, (l.az + l.bz) / 2, 1);
                }
            if (worst < 4 && clear) break;
        }
    }
    auto setup = [&](Sim& s) {
        s.world = windowAt(9090, gx, gz);
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

namespace {
// The chunk cache's ground at a global metre (what the game stands on: carved, bumps, trails).
double chunkCell(const ProcWorld& G, int64_t ix, int64_t iz) {
    auto c = G.chunk(floorDiv(ix, kChunk), floorDiv(iz, kChunk));
    return c->h[(size_t)(ix - c->cx * kChunk) * kChunk + (size_t)(iz - c->cz * kChunk)];
}
double chunkH(const ProcWorld& G, double x, double z) {  // (bilinear between the cells, as the ground is drawn and driven on)
    const double fx = std::floor(x), fz = std::floor(z), tx = x - fx, tz = z - fz;
    const int64_t ix = (int64_t)fx, iz = (int64_t)fz;
    return (chunkCell(G, ix, iz) * (1 - tx) + chunkCell(G, ix + 1, iz) * tx) * (1 - tz) + (chunkCell(G, ix, iz + 1) * (1 - tx) + chunkCell(G, ix + 1, iz + 1) * tx) * tz;
}
}  // namespace

SCENARIO(procEarthworks, "procworld/earthworks", "Roads' and trails' earthworks keep to their rules (procrules.h): no tread rides a cut or fill deeper than its kind's limit, and no side is a wall") {
    // (Owner, screenshot 2: a trail along the top of a 10 m embankment with near-vertical sides. Across each tread, every 6 m along it:
    // how much steeper the ground beside it is than the land it was cut from, metre by metre out to 45 m.)
    struct Stat { long n = 0, fillOver = 0, sideOver = 0; double worstFill = 0, worstSide = 0, wx = 0, wz = 0; uint64_t ws = 0; };
    Stat st[2];
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -3; i < 3; i++)
            for (int64_t j = -3; j < 3; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on) continue;
                    Stat& s = st[e->road ? 1 : 0];
                    const RoadRule& rule = roadRule(e->road ? RK_GRAVEL : e->rating >= 2 ? RK_TWOTRACK : RK_TRAIL);
                    for (size_t k = 4; k + 4 < e->x.size(); k += 3) {
                        const double tx = e->x[k + 1] - e->x[k - 1], tz = e->z[k + 1] - e->z[k - 1], L = std::sqrt(tx * tx + tz * tz);
                        if (L < 1e-6) continue;
                        bool byBridge = false;  // (a bridge's deck and approaches stand over the land by design)
                        for (const TrailEdge::Span& b : e->bridges)
                            byBridge |= std::hypot(e->ax + e->x[k] - 0.5 * (b.ax + b.bx), e->az + e->z[k] - 0.5 * (b.az + b.bz)) < 0.5 * std::hypot(b.bx - b.ax, b.bz - b.az) + 20;
                        if (byBridge) continue;
                        const double nx = -tz / L, nz = tx / L, cx = e->ax + e->x[k], cz = e->az + e->z[k];
                        s.n++;
                        const double fill = std::fabs(e->fill[k]);
                        s.worstFill = std::max(s.worstFill, fill), s.fillOver += fill > rule.fillMax + 1.0;  // (a metre for the land's bumps)
                        if (G.macroAt(cx, cz, false).w[PB_BADLANDS] > 0.1) continue;  // (v7: among the mesas, their sheer bands are meant)
                        for (int sd = -1; sd <= 1; sd += 2) {
                            double worst = 0, pc = chunkH(G, cx + nx * sd * (e->half() + 1), cz + nz * sd * (e->half() + 1));
                            double pn = G.groundHeight(cx + nx * sd * (e->half() + 1), cz + nz * sd * (e->half() + 1));
                            for (double d = e->half() + 2; d < 46; d += 1) {
                                const double qx = cx + nx * sd * d, qz = cz + nz * sd * d, hc = chunkH(G, qx, qz), hn = G.groundHeight(qx, qz);
                                worst = std::max(worst, std::fabs(hc - pc) - std::max(1.0, std::fabs(hn - pn)));  // (steeper than 1:1 and than the land was)
                                pc = hc, pn = hn;
                            }
                            if (worst > s.worstSide) s.worstSide = worst, s.wx = cx, s.wz = cz, s.ws = seed;
                            s.sideOver += worst > 0.5;
                        }
                    }
                }
    }
    for (int r = 0; r < 2; r++) {
        const Stat& s = st[r];
        char b[260];
        std::snprintf(b, sizeof b, "%s: %ld samples; cut or fill: deepest %.1f m, %.2f%% past the limit; sides: steepest %.2f m a metre over 1:1 and the land's own (seed %llu at %.0f, %.0f), %.2f%% over 0.5",
                      r ? "gravel roads" : "trails", s.n, s.worstFill, 100.0 * s.fillOver / std::max(1L, s.n), s.worstSide, (unsigned long long)s.ws, s.wx, s.wz, 100.0 * s.sideOver / std::max(1L, 2 * s.n));
        ctx.note(b);
        ctx.check(std::string(r ? "roads" : "trails") + ": under 1% of the tread past its kind's cut and fill limit", s.fillOver * 100 < s.n);
        // (A level tread across a steep slope makes a short steep bank on its high side, so a few are over.)
        // (v0.4.7: 9.0 and 9.4 m a metre at worst. Two treads crossing at different heights still make a short steep ramp.)
        // (v6: the land is far steeper (mountains and knolls): a level tread across a 1:2 side makes a bank steeper than 1:1 on its high
        // side, and the same measure over profiles with no trail at all finds 6.4% (v5: 1.7%). So 30%; no wall still holds.)
        ctx.check(std::string(r ? "main trails" : "trails") + ": under 30% of the sides half a metre a metre steeper than 1:1 and the land", s.sideOver * 100 < 30 * 2 * s.n);
        ctx.check(std::string(r ? "main trails" : "trails") + ": no side a wall (over 4.5 m a metre steeper than 1:1 and the land)", s.worstSide < 4.5);
    }
}

SCENARIO(procRiverRules, "procworld/river-rules", "Rivers and creeks keep to the water rules (procrules.h kWater): the water never above the land beside it, never rising downstream, no rings, banks not walls") {
    // (Owner, screenshot 4: rivers along the tops of vertical-walled embankments, a creek in a ring round a sunken bowl.)
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        World::procSeed = seed, World::procOriginSet = false;
        World w(MAP_PROCEDURAL);
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const double half = w.halfSize() - 50;
        long wet = 0, above = 0, rises = 0, steps = 0, bankN = 0, bankWall = 0;
        double worstAbove = 0, worstRise = 0, worstBank = 0;
        for (double x = -half; x < half; x += 3)
            for (double z = -half; z < half; z += 3) {
                const double lv = w.waterLevel(x, z);
                if (lv < -1e5) continue;
                wet++;
                // Across the flow, 30 m out either side: the ground stands at or above the water.
                V3 f = w.waterFlow(x, z);
                double fl = std::sqrt(f.x * f.x + f.z * f.z);
                if (fl < 1e-6) continue;  // (still water: a lake, procworld/lakes)
                const double fx = f.x / fl, fz = f.z / fl;
                for (int sd = -1; sd <= 1; sd += 2) {
                    const double gx = x - fz * sd * 30, gz = z + fx * sd * 30;
                    if (std::fabs(gx) > half || std::fabs(gz) > half) continue;
                    const double over = lv - w.height(gx, gz);
                    // (not where it's a lake's water beside, nor a river's a hand lower: v8, a creek running the last of its way into the river
                    // beside it, its level kept over the river's at the join, stands up to half a metre over it there)
                    if (over > 0.1 && w.waterLevel(gx, gz) < lv - 0.5) above++, worstAbove = std::max(worstAbove, over);
                }
                // 4 m on downstream: the water is no higher.
                const double dx = x + fx * 4, dz = z + fz * 4, lv2 = w.waterLevel(dx, dz);
                if (lv2 > -1e5) {
                    steps++;
                    if (lv2 > lv + 0.05) rises++, worstRise = std::max(worstRise, lv2 - lv);
                }
                // The banks: from 2 m past the water's edge out 20 m, no metre 1.5 m high and steeper by 1.2 than the land the channel
                // was cut into. (v6: a creek slanting across a range's 1:0.7 side has that side for its bank; the land's own steepness.)
                if (((long)(x * 7 + z * 13)) % 5 == 0)
                    for (int sd = -1; sd <= 1; sd += 2) {
                        const V3 g0 = ProcWorld::toGlobal(w, V3(x, 0, z));
                        double prev = w.height(x, z), prevL = G.macroHeight(g0.x, g0.z), worst = 0;
                        bool dry = false;
                        for (double d = 1; d < 20; d += 1) {
                            const double gx = x - fz * sd * d, gz = z + fx * sd * d;
                            if (std::fabs(gx) > half || std::fabs(gz) > half) break;
                            const V3 gg = ProcWorld::toGlobal(w, V3(gx, 0, gz));
                            const double h = w.height(gx, gz), hl = G.macroHeight(gg.x, gg.z);
                            if (w.waterLevel(gx, gz) < -1e5) dry = true;
                            if (dry && std::fabs(h - prev) > 1.5 && G.macroAt(gg.x, gg.z, false).w[PB_BADLANDS] < 0.1) worst = std::max(worst, std::fabs(h - prev) - std::fabs(hl - prevL));  // (a mesa's sheer band by a canyon's river: meant)
                            prev = h, prevL = hl;
                        }
                        bankN++, bankWall += worst > 1.2, worstBank = std::max(worstBank, worst);
                    }
            }
        // No rings: the dry ground cut off from the window's edge by water (on a 6 m raster, the channels thickened) is small.
        const int R = (int)(2 * half / 6);
        std::vector<uint8_t> chan((size_t)R * R, 0), seen((size_t)R * R, 0);
        for (int a = 0; a < R; a++)
            for (int b = 0; b < R; b++)
                for (int q = 0; q < 4 && !chan[(size_t)a * R + b]; q++) chan[(size_t)a * R + b] = w.waterLevel(-half + a * 6 + (q & 1) * 3, -half + b * 6 + (q >> 1) * 3) > -1e5;
        std::vector<int> stack;
        for (int a = 0; a < R; a++)
            for (int b = 0; b < R; b++)
                if ((a == 0 || b == 0 || a == R - 1 || b == R - 1) && !chan[(size_t)a * R + b]) seen[(size_t)a * R + b] = 1, stack.push_back(a * R + b);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int a = c / R, b = c % R;
            const int nb[4][2] = {{a + 1, b}, {a - 1, b}, {a, b + 1}, {a, b - 1}};
            for (auto& n : nb)
                if (n[0] >= 0 && n[1] >= 0 && n[0] < R && n[1] < R && !chan[(size_t)n[0] * R + n[1]] && !seen[(size_t)n[0] * R + n[1]])
                    seen[(size_t)n[0] * R + n[1]] = 1, stack.push_back(n[0] * R + n[1]);
        }
        long enclosed = 0;
        for (int a = 0; a < R; a++)
            for (int b = 0; b < R; b++) enclosed += !chan[(size_t)a * R + b] && !seen[(size_t)a * R + b];
        char b[400];
        std::snprintf(b, sizeof b, "seed %llu: %ld wet samples; %ld (%.2f%%) with ground under the water 30 m aside (worst %.1f m); downstream rises %ld of %ld (worst %.2f m); "
                      "banks: %.2f%% with a metre over 1.5 m (steepest %.1f); %.0f m2 of dry land enclosed by water",
                      (unsigned long long)seed, wet, above, 100.0 * above / std::max(1L, 2 * wet), worstAbove, rises, steps, worstRise, 100.0 * bankWall / std::max(1L, bankN), worstBank,
                      36.0 * enclosed);
        ctx.note(b);
        ctx.check("seed " + std::to_string(seed) + ": there are rivers or creeks", wet > 200);
        ctx.check("seed " + std::to_string(seed) + ": the water is above the ground 30 m aside under 0.5% of the time", above * 200 < 2 * wet);
        ctx.check("seed " + std::to_string(seed) + ": the water never rises downstream (under 0.5%, a few cm)", rises * 200 < std::max(1L, steps) && worstRise < 0.5);
        ctx.check("seed " + std::to_string(seed) + ": no ring of water round dry land (under 2,000 m2 enclosed)", 36.0 * enclosed < 2000);
        ctx.check("seed " + std::to_string(seed) + ": banks under 1% walls", bankWall * 100 < std::max(1L, bankN));
    }
    // (And where v0.4.7 carried a river on a bank: seed 123456789 round (2340, 1435), where its water stood up to 7 m over the land
    // 30 m aside. Every chunk of 2 km square there: no water over the uncarved land, nor a metre over the ground beside it.)
    {
        const ProcWorld& G = ProcWorld::forSeed(123456789);
        long wetN = 0, raised = 0, beside = 0;
        for (int64_t cx = 20; cx < 52; cx++)
            for (int64_t cz = 7; cz < 39; cz++) {
                auto c = G.chunk(cx, cz);
                if (c->water.empty()) continue;
                for (int a = 0; a < kChunk; a += 2)
                    for (int b = 0; b < kChunk; b += 2) {
                        const float lv = c->water[(size_t)a * kChunk + b];
                        if (lv < -1e8f || (c->flowX[(size_t)a * kChunk + b] == 0 && c->flowZ[(size_t)a * kChunk + b] == 0)) continue;  // (lakes: still)
                        wetN++;
                        const double x = (double)(cx * kChunk + a), z = (double)(cz * kChunk + b);
                        raised += lv > G.macroAt(x, z, false).h + 0.5;
                        const double fx = c->flowX[(size_t)a * kChunk + b], fz = c->flowZ[(size_t)a * kChunk + b], fl = std::sqrt(fx * fx + fz * fz);
                        if (fl < 1e-6) continue;
                        for (int sd = -1; sd <= 1; sd += 2) {  // (beside a lake it flows into, the lake's floor: water beside water)
                            const double ax = x - fz / fl * sd * 30, az = z + fx / fl * sd * 30;
                            const Macro ma = G.macroAt(ax, az, false);
                            if (ma.lakeLevel > -1e8 && ma.h < ma.lakeLevel) continue;
                            // (nor beside another channel's water as high, where two run together into a confluence: water beside water)
                            if ((ma.riverOn > 0.5 && ma.riverS < ma.riverHW && ma.riverLevel > lv - 0.1) || (ma.creekOn > 0.5 && ma.creekS < ma.creekHW && ma.creekLevel > lv - 0.1)) continue;
                            beside += lv > G.groundHeight(ax, az, ma) + 1;
                        }
                    }
            }
        char b[200];
        std::snprintf(b, sizeof b, "seed 123456789 round (2340, 1435): %ld wet samples, %ld over the uncarved land, %ld a metre over the ground 30 m aside", wetN, raised, beside);
        ctx.note(b);
        ctx.check("no water carried over the land (seed 123456789, 2 km square)", wetN > 100 && raised == 0 && beside * 200 < wetN);
    }
    World::procOriginSet = false;
}

SCENARIO(procNoRings, "procworld/no-rings", "No river or creek runs in a ring (procrules.h kWater): over 8 km square of three seeds, no dry land is closed in by water") {
    // (Owner, screenshot 4: a creek in a ring round a sunken bowl. A channel is a contour of a noise field, which closes round the
    // field's bumps; v0.4.7 had 4, 1 and 4 such rings in 10 km square of these seeds.)
    const double half = 4000, C = 10;
    const int R = (int)(2 * half / C);
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        std::vector<uint8_t> ch((size_t)R * R), seen((size_t)R * R);
        std::vector<std::thread> th;
        for (int t = 0; t < 4; t++)
            th.emplace_back([&, t] {
                for (int a = t; a < R; a += 4)
                    for (int b = 0; b < R; b++) {
                        const double x = -half + a * C, z = -half + b * C;
                        const Macro m = G.macroAt(x, z, true);
                        ch[(size_t)a * R + b] = (m.creekOn > 0.5 && std::fabs(m.creekS) < m.creekHW + 5) || (m.riverOn > 0.5 && std::fabs(m.riverS) < m.riverHW + 5);
                    }
            });
        for (auto& t : th) t.join();
        std::vector<int> st;
        auto flood = [&](int start) {
            long n = 0;
            st.push_back(start), seen[(size_t)start] = 1;
            while (!st.empty()) {
                const int c = st.back(), a = c / R, b = c % R;
                st.pop_back(), n++;
                const int nb[4][2] = {{a + 1, b}, {a - 1, b}, {a, b + 1}, {a, b - 1}};
                for (auto& q : nb)
                    if (q[0] >= 0 && q[1] >= 0 && q[0] < R && q[1] < R && !ch[(size_t)q[0] * R + q[1]] && !seen[(size_t)q[0] * R + q[1]])
                        seen[(size_t)q[0] * R + q[1]] = 1, st.push_back(q[0] * R + q[1]);
            }
            return n;
        };
        for (int a = 0; a < R; a++)
            for (int b = 0; b < R; b++)
                if ((a == 0 || b == 0 || a == R - 1 || b == R - 1) && !ch[(size_t)a * R + b] && !seen[(size_t)a * R + b]) flood(a * R + b);
        int rings = 0;
        for (int a = 0; a < R; a++)
            for (int b = 0; b < R; b++)
                if (!ch[(size_t)a * R + b] && !seen[(size_t)a * R + b]) rings += flood(a * R + b) >= 4;
        ctx.note("seed " + std::to_string(seed) + ": " + std::to_string(rings) + " patches of land closed in by water");
        ctx.check("seed " + std::to_string(seed) + ": no rings", rings == 0);
    }
}

SCENARIO(procRulesFile, "procworld/rules-file", "The rules file (assets/procworld/rules.txt) is the built-in rules, every key in it; a file read over them changes what it names, and a bad line changes nothing") {
    // (CZ_WRITE_RULES=path writes the built-in rules as a file: how the shipped one is made after a default changes.)
    const std::string text = rulesText(Rules());
    if (const char* out = std::getenv("CZ_WRITE_RULES")) {
        if (FILE* f = std::fopen(out, "wb")) std::fwrite(text.data(), 1, text.size(), f), std::fclose(f), ctx.note(std::string("wrote ") + out);
    }
    std::string shipped;
    if (FILE* f = std::fopen("assets/procworld/rules.txt", "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) shipped.append(b, n);
        std::fclose(f);
    }
    ctx.check("assets/procworld/rules.txt is the built-in rules (CZ_WRITE_RULES=assets/procworld/rules.txt ./build/run_tests procworld/rules-file remakes it)", shipped == text);
    Rules r;
    std::vector<std::string> warn;
    parseRules(text, r, &warn);
    ctx.check("the built-in rules read back the same, with no warnings", rulesText(r) == text && warn.empty());
    parseRules("[biome.alpine]\ntree_line = 1234  # a comment\nsnow_full = lots\n[roads.trail]\nfill_max=1.5\nno_such = 3\n", r, &warn);
    ctx.check("a value read over the defaults", r.alpine.treeLine == 1234 && r.road[RK_TRAIL].fillMax == 1.5);
    ctx.check("a bad value keeps the default", r.alpine.snowFull == Rules().alpine.snowFull);
    ctx.check("bad lines are noted", warn.size() == 2);
    ctx.check("the rules in force in the tests are the built-in ones", rulesStamp() == 0);
}

SCENARIO(procAlpine, "procworld/alpine", "The alpine rules (procrules.h, biome.alpine): tall snowy peaks, pine forest on the slopes to a tree line, stunted near it, bare above it, snow more the higher") {
    const AlpineRule& A = rules().alpine;
    // Peaks: the highest ground on a 100 m lattice of 12 km square, three seeds.
    int peaks = 0;
    double top = 0;
    std::vector<std::array<double, 2>> rangeSpots;  // (alpine slopes to look at closely)
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        std::vector<std::array<double, 3>> hi;
        for (double x = -6000; x < 6000; x += 100)
            for (double z = -6000; z < 6000; z += 100) {
                const double h = G.macroCached(x, z).h;  // (the peaks before the wear, where no tile is made: within 60 m of the worn)
                top = std::max(top, h);
                if (h > 1500) hi.push_back({h, x, z});
                if (h > A.treeLine - 500 && h < A.treeLine - 300 && rangeSpots.size() < 400 && ((long)(x + z) / 100) % 7 == 0) rangeSpots.push_back({x, z});
            }
        std::sort(hi.begin(), hi.end(), [](auto& a, auto& b) { return a[0] > b[0]; });
        std::vector<std::array<double, 3>> kept;  // (peaks 1 km apart)
        for (auto& p : hi) {
            bool far = true;
            for (auto& k : kept) far &= (k[1] - p[1]) * (k[1] - p[1]) + (k[2] - p[2]) * (k[2] - p[2]) > 1000 * 1000;
            if (far) kept.push_back(p);
        }
        peaks += (int)kept.size();
    }
    char b[300];
    std::snprintf(b, sizeof b, "%d peaks over 1.5 km (1 km apart) in 3 x 144 km2; the highest %.0f m", peaks, top);
    ctx.note(b);
    // (The owner's rule, procworld-rules.md 2: peaks 1.5-2.5 km. The summits stand in massifs on the ranges' axes.)
    ctx.check("peaks: at least one over 1.5 km per 100 km2", peaks * 100 >= 3 * 144);
    ctx.check("peaks: none over 2.8 km", top < 2800);
    ctx.check("bare tops: some land over the tree line", top > A.treeLine + A.treeLineWobble);
    {  // (and snow on them: the highest ground of some seeds round their summits, a chunk at each top over 1.9 km)
        long cells = 0, snow = 0;
        double best = 0;
        for (uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 6ull, 123456789ull}) {
            const ProcWorld& G = ProcWorld::forSeed(seed);
            double bh = 0, bx = 0, bz = 0;
            for (double x = -8000; x < 8000; x += 200)
                for (double z = -8000; z < 8000; z += 200) {
                    const double h = G.macroCached(x, z).h;  // (the peaks before the wear, where no tile is made: within 60 m of the worn)
                    if (h > bh) bh = h, bx = x, bz = z;
                }
            best = std::max(best, bh);
            if (bh < 1900) continue;
            auto c = G.chunk(floorDiv((int64_t)bx, kChunk), floorDiv((int64_t)bz, kChunk));
            for (int k = 0; k < kChunk * kChunk; k++)
                if (c->h[k] > 1900) cells++, snow += c->surf[k] == SURF_SNOW || c->surf[k] == SURF_ROCK;
        }
        std::snprintf(b, sizeof b, "the highest of 7 seeds' 256 km2: %.0f m; over 1.9 km round their tops, %.0f%% snow or bare rock", best, 100.0 * snow / std::max(1L, cells));
        ctx.note(b);
        ctx.check("snow (or rock too steep for it) over the high tops", cells > 0 && snow * 10 >= cells * 7);
    }
    // The forest, the tree line and the snow, from the chunks: on the alpine slopes of three seeds' ranges (a chunk every 250 m).
    struct Band { long cells = 0, snow = 0, trees = 0, conifers = 0, area = 0; double treeH = 0; };
    std::map<int, Band> band;  // (by 100 m of height)
    for (uint64_t seed : {1ull, 2ull, 123456789ull})
    for (double x = -6000; x < 6000; x += 250)
        for (double z = -6000; z < 6000; z += 250) {
            const ProcWorld& G = ProcWorld::forSeed(seed);
            const Macro m = G.macro(x, z);
            if (m.w[PB_MOUNTAINS] < 0.9) continue;
            auto c = G.chunk(floorDiv((int64_t)x, kChunk), floorDiv((int64_t)z, kChunk));
            for (int k = 0; k < kChunk * kChunk; k++) {  // (by each cell's own height: the valleys are cut far under the range's land)
                Band& bd = band[(int)std::floor(c->h[k] / 100)];
                bd.cells++, bd.snow += c->surf[k] == SURF_SNOW;
                const int i = k / kChunk, j = k % kChunk;
                if (i > 0 && j > 0 && i < kChunk - 1 && j < kChunk - 1 && std::fabs(c->h[k + kChunk] - c->h[k - kChunk]) < 1.2 && std::fabs(c->h[k + 1] - c->h[k - 1]) < 1.2) bd.area++;
            }
            for (const TreeSpec& t : c->trees) {
                Band& bd = band[(int)std::floor(c->h[(size_t)std::min(63, (int)t.x) * kChunk + std::min(63, (int)t.z)] / 100)];
                bd.trees++, bd.conifers += t.kind == 0, bd.treeH += t.height;
            }
        }
    double prevSnow = -1;
    bool snowGrows = true, bareAbove = true;
    double forestHa = 0, forestN = 0, coniferShare = 0, coniferN = 0, lowH = 0, lowN = 0, highH = 0, highN = 0;
    for (auto& [bh, bd] : band) {
        if (bd.cells < 4096) continue;
        const double snow = (double)bd.snow / bd.cells, ha = bd.area / 10000.0, perHa = ha > 0 ? bd.trees / ha : 0;
        std::snprintf(b, sizeof b, "%4d-%4d m: %3ld chunks, snow %4.0f%%, %5.1f trees a hectare of gentle ground, %3.0f%% conifers, mean height %4.1f m", bh * 100, bh * 100 + 100,
                      bd.cells / 4096, 100 * snow, perHa, bd.trees ? 100.0 * bd.conifers / bd.trees : 0.0, bd.trees ? bd.treeH / bd.trees : 0.0);
        ctx.note(b);
        // (8 points slack: the bands up high are a score of chunks, and steep tops shed their snow)
        // (v6: the summits are carved crests, steep, shedding their snow; a band of 5-10 chunks up there swings 20 points, so 12 chunks)
        if (bh * 100 >= A.snowPatchy - 100 && bd.cells >= 4096 * 12) snowGrows &= snow >= prevSnow - 0.08, prevSnow = snow;
        if (bh * 100 >= A.treeLine + A.treeLineWobble) bareAbove &= bd.trees == 0;
        if ((bh + 1) * 100 <= A.treeLine - A.treeLineFade && bh * 100 >= rules().range.alpineFrom) forestHa += perHa * bd.cells, forestN += bd.cells, coniferShare += bd.conifers, coniferN += bd.trees, lowH += bd.treeH, lowN += bd.trees;
        if (bh * 100 >= A.treeLine - 150 && bh * 100 < A.treeLine + A.treeLineWobble) highH += bd.treeH, highN += bd.trees;
    }
    const double dens = forestN ? forestHa / forestN : 0;
    std::snprintf(b, sizeof b, "below the tree line: %.0f trees a hectare of gentle ground (rule %.0f, less the meadows), %.0f%% conifers; mean height %.1f m low, %.1f m near the line",
                  dens, A.treesPerHa, coniferN ? 100 * coniferShare / coniferN : 0.0, lowN ? lowH / lowN : 0.0, highN ? highH / highN : 0.0);
    ctx.note(b);
    ctx.check("thick forest on the slopes: over 190 trees a hectare, within 40-110% of the rule's density", dens > 190 && dens > 0.4 * A.treesPerHa && dens < 1.1 * A.treesPerHa);
    ctx.check("mostly conifers below the tree line", coniferN > 0 && coniferShare / coniferN > 0.8);
    ctx.check("stunted near the tree line", lowN > 0 && highN > 0 && highH / highN < 0.8 * lowH / lowN);
    ctx.check("no trees over the tree line", bareAbove);
    ctx.check("more snow the higher (where there's enough high ground to tell)", snowGrows);
}

SCENARIO(procDrainTree, "procworld/drainage-tree", "The drainage network (procworld.cpp): every node's way down ends in a sink, no node twice (a forest of trees, no loops); no channel's level ever rises downstream; sinks are rare") {
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const auto st = G.drainStats(-6000, -6000, 6000, 6000);
        char b[400];
        std::snprintf(b, sizeof b, "seed %llu, 144 km2: %ld nodes; %ld sinks; channels: %ld creek, %ld river, %ld big river nodes; longest way down %ld nodes; %ld loops or dead ends; %ld node-to-node rises; %ld rises along the pieces",
                      (unsigned long long)seed, st.nodes, st.sinks, st.creeks, st.rivers, st.bigRivers, st.maxChain, st.chainsBad, st.levelRises, st.pieceRises);
        ctx.note(b);
        ctx.check("seed " + std::to_string(seed) + ": every way down ends in a sink, no node visited twice", st.chainsBad == 0 && st.chainsChecked == st.nodes);
        ctx.check("seed " + std::to_string(seed) + ": no level rises downstream (node to node, and along every piece)", st.levelRises == 0 && st.pieceRises == 0);
        // (v6: the ranges come every ~4 km, not ~8, so twice the basins closed in between them: 15 in seed 2's 144 km2, was 10 at most.)
        ctx.check("seed " + std::to_string(seed) + ": sinks (lake basins) rare: under 20 in 144 km2", st.sinks < 20);
    }
}

SCENARIO(procRiverSpacing, "procworld/river-spacing", "Big rivers about every 5 km: 3-5 across a 20 km line on average (16 lines over 4 seeds); and the ranges' creeks carry water (out of the desert)") {
    double tot = 0;
    int n = 0;
    std::string l;
    for (uint64_t seed : {1ull, 2ull, 3ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const double ang[4][2] = {{1, 0}, {0, 1}, {0.7071, 0.7071}, {0.7071, -0.7071}};
        for (int d = 0; d < 4; d++) {
            auto c = G.channelsAcross(-10000 * ang[d][0], -10000 * ang[d][1], 10000 * ang[d][0], 10000 * ang[d][1], rules().water.bigRiverArea);
            int cnt = 0;
            double last = -1e9;
            for (auto& q : c) cnt += q.first - last > 300, last = q.first;  // (a river crossing the line a few times close together: once)
            tot += cnt, n++, l += " " + std::to_string(cnt);
        }
    }
    ctx.note("big rivers across each 20 km line:" + l);
    ctx.metric("big rivers across a 20 km line, on average", tot / n, "", 3, 5);
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const auto st = G.drainStats(-6000, -6000, 6000, 6000);
        char b[200];
        std::snprintf(b, sizeof b, "seed %llu: %ld creek nodes in the ranges out of the desert, %.0f%% with water (%.0f%% dry where they'd stand over the land beside them)", (unsigned long long)seed, st.rangeCreeks,
                      100.0 * st.rangeCreeksWet / std::max(1L, st.rangeCreeks), 100.0 * st.rangeCreeksPerched / std::max(1L, st.rangeCreeks));
        ctx.note(b);
        // (Was 70% before the summits went to 2.5 km: their gorges leave more creek stretches perched, dry there. The owner's trade.)
        // (v6: steeper, narrower ranges: more of a creek's stretches would stand over the land beside them (dry there); v7: a lone wet
        // stretch between dry ones is dry too (owner: no isolated strips of water): 52-66%, so half.)
        ctx.check("seed " + std::to_string(seed) + ": the ranges' creeks: half or more carry water", st.rangeCreeks > 100 && st.rangeCreeksWet * 100 >= st.rangeCreeks * 50);
    }
}

SCENARIO(procDrainMap, "procworld/drain-map", "(with CZ_PROC_MAPS=dir) a 24 km overview of each seed's land and drainage, for looking at (CZ_MAP_SPAN, CZ_MAP_X, CZ_MAP_Z)") {
    const char* dir = std::getenv("CZ_PROC_MAPS");
    if (!dir) {
        ctx.note("CZ_PROC_MAPS not set: nothing drawn");
        return;
    }
    const double span = std::getenv("CZ_MAP_SPAN") ? std::atof(std::getenv("CZ_MAP_SPAN")) : 24000;
    const double cx0 = std::getenv("CZ_MAP_X") ? std::atof(std::getenv("CZ_MAP_X")) : 0, cz0 = std::getenv("CZ_MAP_Z") ? std::atof(std::getenv("CZ_MAP_Z")) : 0;
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const int S = 900;
        const double step = span / S;
        std::vector<double> H((size_t)S * S);
        std::vector<Macro> MM((size_t)S * S);
        std::vector<std::thread> th;
        for (int t = 0; t < 4; t++)
            th.emplace_back([&, t] {
                for (int py = t; py < S; py += 4)
                    for (int px = 0; px < S; px++) {
                        const double x = cx0 - span / 2 + (px + 0.5) * step, z = cz0 + span / 2 - (py + 0.5) * step;
                        MM[(size_t)py * S + px] = G.macroAt(x, z, true);
                        H[(size_t)py * S + px] = G.groundHeight(x, z, MM[(size_t)py * S + px]);
                    }
            });
        for (auto& t : th) t.join();
        std::vector<unsigned char> img((size_t)S * S * 3);
        double top = 0;
        for (int py = 0; py < S; py++)
            for (int px = 0; px < S; px++) {
                const size_t k = (size_t)py * S + px;
                const Macro& m = MM[k];
                const double h = H[k];
                top = std::max(top, h);
                const double hx = H[(size_t)py * S + std::min(S - 1, px + 1)], hz = H[(size_t)std::max(0, py - 1) * S + px];
                const double shade = std::clamp(0.8 + 1.2 * ((hx - h) - (hz - h)) / step, 0.3, 1.4);
                double c[3];
                const double t = std::clamp(h / 2000, 0.0, 1.0);
                c[0] = 0.25 + 0.6 * t, c[1] = 0.45 + 0.35 * t, c[2] = 0.2 + 0.7 * t;
                if (m.w[PB_DUNES] + m.w[PB_BADLANDS] > 0.5) c[0] = 0.8, c[1] = 0.6, c[2] = 0.4;
                for (double& v : c) v *= shade;
                if (m.riverS < m.riverHW + step * 0.7) {
                    if (m.riverOn > 0.5) c[0] = 0.1, c[1] = m.bigRiver ? 0.2 : 0.4, c[2] = m.bigRiver ? 0.9 : 0.8;
                    else c[0] = 0.9, c[1] = 0.8, c[2] = 0.5;
                } else if (m.creekS < m.creekHW + step * 0.4 && m.creekOn > 0.5) c[0] = 0.3, c[1] = 0.6, c[2] = 0.9;
                for (int q = 0; q < 3; q++) img[k * 3 + q] = (unsigned char)std::clamp(c[q] * 255, 0.0, 255.0);
            }
        const std::string path = std::string(dir) + "/drain_seed" + std::to_string(seed) + ".ppm";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", S, S);
            std::fwrite(img.data(), 1, img.size(), f);
            std::fclose(f);
        }
        ctx.note("wrote " + path + ", highest " + std::to_string((int)top) + " m");
    }
}

SCENARIO(procTrailHugs, "procworld/trail-hugs", "Trails hug the land (rules 6; owner: \"it's not natural for the road to be raised\"): the tread never stands more than ~0.3 m over the land 5 m to either side of it (or under it, on a crest), away from bridges and the places' lots at its ends") {
    long n = 0, over = 0;
    double worst = 0, wx = 0, wz = 0, sum = 0;
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -3; i < 3; i++)
            for (int64_t j = -3; j < 3; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on) continue;
                    for (size_t k = 15; k + 15 < e->x.size(); k += 3) {  // (not the 30 m let down from a lot at either end)
                        bool nearBridge = false;  // (a deck and its approaches, let down to the land over 30 m past them)
                        for (const TrailEdge::Span& b : e->bridges) {
                            const double px = e->x[k] + e->ax, pz = e->z[k] + e->az, vx = b.bx - b.ax, vz = b.bz - b.az, L2 = vx * vx + vz * vz + 1e-9;
                            const double t = std::min(1.0, std::max(0.0, ((px - b.ax) * vx + (pz - b.az) * vz) / L2));
                            nearBridge |= std::hypot(px - b.ax - vx * t, pz - b.az - vz * t) < 45;
                        }
                        if (nearBridge) continue;
                        const double tx = e->x[k + 1] - e->x[k - 1], tz = e->z[k + 1] - e->z[k - 1], L = std::hypot(tx, tz);
                        if (L < 1e-6) continue;
                        const double nx = -tz / L, nz = tx / L, cx = e->ax + e->x[k], cz = e->az + e->z[k];
                        // (over the land either side, or the land under it where that's higher: along a ridge's narrow crest the land falls away
                        // either side of a tread that's on the ground, not raised)
                        const double up = e->y[k] - std::max({G.groundHeight(cx + nx * 5, cz + nz * 5), G.groundHeight(cx - nx * 5, cz - nz * 5), G.groundHeight(cx, cz)});
                        n++, over += up > 0.3, sum += std::max(0.0, up);
                        if (up > worst) worst = up, wx = cx, wz = cz;
                    }
                }
    }
    char b[240];
    std::snprintf(b, sizeof b, "%ld tread samples: %.2f%% over the land 5 m either side by more than 0.3 m; the most %.2f m (at %.0f, %.0f); on average %.3f m over", n, 100.0 * over / std::max(1L, n), worst, wx, wz,
                  sum / std::max(1L, n));
    ctx.note(b);
    // (v0.4.7 main: 4.6% over 0.3 m, the worst 6.7 m: trails along a valley's floor rode a bank over it. What's left is a tread across a
    // knoll's crown or the lip of a wash, a metre at most.)
    ctx.check("trails: under 1% of the tread more than 0.3 m over the land either side", over * 100 < n);
    ctx.check("trails: none on a bank (over 2 m above the land either side)", worst < 2.0);
}

SCENARIO(procWashShape, "procworld/wash-shape", "Dry washes are natural, not one stamped section (owner: \"the dry river bed is the exact same width and depth everywhere\"): followed down their course, their width and depth vary") {
    // (Each dry channel found over 6 km square of a few seeds, followed downstream 6 m a step; its section across, from the chunks'
    // ground: the depth under the lower of its two rims within 8 m, and the width at half that depth.)
    double cvW = 0, cvD = 0, mW = 0;
    int nc = 0;
    for (uint64_t seed : {2ull, 777ull, 2024ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        std::vector<std::pair<double, double>> starts;
        for (double x = -3000; x < 3000; x += 64)
            for (double z = -3000; z < 3000; z += 64) {
                const Macro m = G.macro(x, z);
                if (m.creekOn < 0.5 && m.creekS < 40 && m.creekHW > 0.6 && m.riverS > 60 && m.lakeLevel < -1e8) {
                    bool far = true;
                    for (const auto& q : starts) far &= std::hypot(q.first - x, q.second - z) > 600;
                    if (far) starts.push_back({x, z});
                }
            }
        for (const auto& st : starts) {
            double px = st.first, pz = st.second;
            std::vector<double> W, D;
            for (int step = 0; step < 60; step++) {
                const Macro m = G.macroAt(px, pz, false);
                if (m.creekS > 30 || m.creekOn > 0.5) break;
                const double fl = std::hypot(m.cflowX, m.cflowZ);
                if (fl < 1e-6) break;
                const double ux = m.cflowX / fl, uz = m.cflowZ / fl, nx = -uz, nz = ux;
                std::vector<double> p;
                for (double s = -20; s <= 20; s += 0.5) p.push_back(chunkH(G, px + nx * s, pz + nz * s));
                int im = 30;
                for (int q = 30; q <= 50; q++)
                    if (p[q] < p[im]) im = q;
                double rl = -1e9, rr = -1e9;
                for (int q = std::max(0, im - 16); q < im; q++) rl = std::max(rl, p[q]);
                for (int q = im + 1; q <= std::min((int)p.size() - 1, im + 16); q++) rr = std::max(rr, p[q]);
                const double depth = std::min(rl, rr) - p[im], half = p[im] + 0.5 * depth;
                int a = im, c = im;
                while (a > 0 && p[a - 1] < half) a--;
                while (c + 1 < (int)p.size() && p[c + 1] < half) c++;
                if (depth > 0.05) W.push_back((c - a) * 0.5), D.push_back(depth);
                px += nx * (-20 + im * 0.5) + ux * 6, pz += nz * (-20 + im * 0.5) + uz * 6;
            }
            if (W.size() < 15) continue;
            auto cv = [](const std::vector<double>& v, double& mean) {
                double s = 0, q = 0;
                for (double x : v) s += x;
                mean = s / v.size();
                for (double x : v) q += (x - mean) * (x - mean);
                return std::sqrt(q / v.size()) / mean;
            };
            double mw, md;
            cvW += cv(W, mw), cvD += cv(D, md), mW += mw, nc++;
        }
    }
    char b[200];
    std::snprintf(b, sizeof b, "%d dry channels followed: width at half depth %.1f m on average, varying along their course by %.2f (CoV); depth by %.2f", nc, nc ? mW / nc : 0, nc ? cvW / nc : 0,
                  nc ? cvD / nc : 0);
    ctx.note(b);
    ctx.check("there are dry washes to follow", nc >= 10);
    // (main before this: width CoV 0.17-0.26 and depth 0.24-0.38 by seed, and most of that the notch the wash runs in, not its bed.)
    ctx.check("a wash's width varies along it (CoV over 0.3)", nc && cvW / nc > 0.3);
    ctx.check("...and its depth (CoV over 0.35)", nc && cvD / nc > 0.35);
}

SCENARIO(procRocksSolid, "procworld/rocks-solid", "Every rock you can see is solid (owner: a 4Runner sat inside a big boulder on a steep slope): a truck driven into the biggest rocks of a mountain window (the game's scanned rocks: slabs, outcrops, clusters) never ends up inside one") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    if (!props::activeRockScans()) {
        ctx.note("no scanned rocks here (assets/props/rocks.hull): skipped");
        return;
    }
    const uint64_t seed = 326904175;
    int64_t sx0, sz0;
    double syaw0;
    ProcWorld::forSeed(seed).start(sx0, sz0, syaw0);
    ctx.sim.world = windowAt(seed, (double)sx0, (double)sz0);
    World& w = ctx.sim.world;
    // (the biggest rocks within 700 m, a spread of them)
    std::vector<int> big;
    for (int i = 0; i < (int)w.rocks.size(); i++)
        if (w.rocks[i].r > 2.2 && std::hypot(w.rocks[i].c.x, w.rocks[i].c.z) < 700) big.push_back(i);
    std::sort(big.begin(), big.end(), [&](int a, int b) { return w.rocks[a].r > w.rocks[b].r || (w.rocks[a].r == w.rocks[b].r && a < b); });
    if (big.size() > 8) big.resize(8);
    ctx.note(std::to_string(big.size()) + " big rocks tried (radius " + (big.empty() ? std::string("-") : std::to_string(w.rocks[big.back()].r) + " - " + std::to_string(w.rocks[big.front()].r)) + " m)");
    auto inside = [&](const V3& p) {  // (how deep p is inside any rock: the shallowest face's distance, across its pieces)
        double worst = 0;
        for (const Rock& k : w.rocks) {
            if (dot(p - k.c, p - k.c) > k.r * k.r) continue;
            const size_t np = std::max<size_t>(1, k.parts.size());
            for (size_t q = 0; q < np; q++) {
                const Rock& kp = k.parts.empty() ? k : k.parts[q];
                double out = -1e9;
                for (size_t f = 0; f < kp.f.size(); f++) out = std::max(out, dot(kp.fn[f], p) - kp.fd[f]);
                if (out < 0) worst = std::max(worst, -out);
            }
        }
        return worst;
    };
    double worstDeep = 0;
    int tried = 0;
    for (int ri : big) {
        ctx.sim.cars.clear();
        const Rock& k = w.rocks[ri];
        // (from 8 m off its edge, on its downhill side if it has one, driven straight at it)
        double best = -1e9, ang = 0;
        for (int a = 0; a < 8; a++) {
            const double t = a * M_PI / 4, x = k.c.x + std::cos(t) * (k.r + 8), z = k.c.z + std::sin(t) * (k.r + 8);
            const double s = -w.height(x, z);
            if (s > best) best = s, ang = t;
        }
        const double x = k.c.x + std::cos(ang) * (k.r + 8), z = k.c.z + std::sin(ang) * (k.r + 8);
        const double heading = std::atan2(-(k.c.z - z), k.c.x - x) * 180 / M_PI;  // (0 = +X, 90 = -Z)
        Car& c = ctx.add(p4Runner(), x, z, heading);
        ctx.launch(c, 7.0);
        double deep = 0;
        ctx.run(3.0, [&] {
            for (const Node& n : ctx.sim.cars[0].nodes) deep = std::max(deep, inside(n.p));
        });
        worstDeep = std::max(worstDeep, deep), tried++;
    }
    // (0.4: a node pressed into any hard surface under a truck at speed; deeper is inside the rock)
    ctx.metric("deepest a truck's node went inside a rock", worstDeep, "m", 0, 0.4);
    ctx.check("big rocks to drive into", tried >= 4);
}

SCENARIO(procCrossings, "procworld/crossings", "Big rivers are real obstacles: a trail crosses one only at a known ford (a riffle, shallow) or on a bridge whose deck stands over the water; the bridge is built in the window") {
    long trails = 0, bigFords = 0, bridges = 0, wades = 0, lowDecks = 0, gone = 0;
    std::string where;
    double bx = 0, bz = 0, by = 0;
    uint64_t bseed = 0;
    TrailEdge::Span bspan{};
    for (uint64_t seed : {1ull, 2ull, 3ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -6; i < 6; i++)
            for (int64_t j = -6; j < 6; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on) {
                        gone++;
                        continue;
                    }
                    trails++, bigFords += e->bigFord, bridges += (long)e->bridges.size();
                    for (size_t k = 0; k < e->x.size(); k++) {
                        const double x = e->ax + e->x[k], z = e->az + e->z[k];
                        const Macro m = G.macroAt(x, z, false);
                        const bool deep = m.bigRiver && !m.riverFord && m.riverOn > 0.5 && m.riverS < m.riverHW - 0.5;
                        if (deep && (e->onBridge.empty() || !e->onBridge[k])) {
                            wades++;
                            if (where.empty()) where = "seed " + std::to_string(seed) + " edge " + std::to_string(i) + "," + std::to_string(j) + "," + std::to_string(dir);
                        }
                    }
                    for (const TrailEdge::Span& s : e->bridges) {
                        const Macro m = G.macroAt(0.5 * (s.ax + s.bx), 0.5 * (s.az + s.bz), false);
                        lowDecks += s.y < m.riverLevel + 1.4;
                        if (!bseed) bseed = seed, bx = 0.5 * (s.ax + s.bx), bz = 0.5 * (s.az + s.bz), by = s.y, bspan = s;
                    }
                }
    }
    ctx.note(std::to_string(trails) + " trails in 4 x 36 km2: " + std::to_string(bigFords) + " ford a big river at a known ford, " + std::to_string(bridges) + " bridges; " + std::to_string(gone) + " links without a trail; " +
             std::to_string(wades) + " samples wading a big river elsewhere" + (where.empty() ? "" : " (first: " + where + ")"));
    ctx.check("no trail wades a big river off its known fords", wades == 0);
    ctx.check("there are known fords and bridges", bigFords > 0 && bridges > 0);
    ctx.check("every bridge's deck stands 1.4 m or more over the water", lowDecks == 0);
    if (bseed) {
        World w = windowAt(bseed, bx, bz);
        const double lx = bx - (double)w.proc.ox, lz = bz - (double)w.proc.oz;
        bool deck = false;
        for (const Box& b : w.boxes) deck |= std::fabs(b.c.x - lx) < 3 && std::fabs(b.c.z - lz) < 3 && std::fabs(b.c.y + 0.15 - by) < 0.2;
        ctx.check("the window round a bridge builds its deck", deck);
        // (Its approaches graded to the deck: the ground at each end and 4 m on never over the deck; and nothing grows or lies on the
        // span or its approaches.)
        const double ax = bspan.ax - (double)w.proc.ox, az = bspan.az - (double)w.proc.oz, ex = bspan.bx - (double)w.proc.ox, ez = bspan.bz - (double)w.proc.oz;
        const double len = std::sqrt((ex - ax) * (ex - ax) + (ez - az) * (ez - az)), ux = (ex - ax) / len, uz = (ez - az) / len;
        double over = -1e9;
        for (double t : {-4.0, -1.0, 0.0}) {
            over = std::max(over, w.height(ax + ux * t, az + uz * t) - by);
            over = std::max(over, w.height(ex - ux * t, ez - uz * t) - by);
        }
        ctx.check("the ground at a bridge's ends never over its deck (graded approaches)", over < 0.1, std::to_string(over) + " m");
        long clutter = 0;
        auto onSpan = [&](const V3& p, double r) {
            const double rx = p.x - ax, rz = p.z - az, al = rx * ux + rz * uz, ac = std::fabs(-rx * uz + rz * ux);
            return al > -6 - r && al < len + 6 + r && ac < 4 + r;
        };
        for (const Tree& t : w.trees) clutter += onSpan(t.base, 0.3);
        for (const Rock& r : w.rocks) clutter += onSpan(r.c, r.r);
        ctx.check("no tree or rock on a bridge's span or approaches", clutter == 0, std::to_string(clutter));
        ctx.note("a bridge: seed " + std::to_string(bseed) + " at " + std::to_string((int)bx) + "," + std::to_string((int)bz));
        World::procOriginSet = false;
    }
}

SCENARIO(procLakesFalls, "procworld/lakes-falls", "Lakes are the water the worn land holds behind its valleys' natural dams (owner: no dug holes; valley lakes only): a few in 144 km2 (2-6 aimed), of varied sizes; level, the land at the water's edge never under it, few steep banks, a gentle bed near the shore. Waterfalls only where the land really drops, each with a plunge pool") {
    long lakes = 0, bySub[5] = {}, lipOff = 0, lipN = 0, edgeN = 0, edgeLow = 0, edgeHigh = 0, bedN = 0, bedSteep = 0, levelN = 0, levelOff = 0, waterCells = 0;
    long falls = 0, tallFalls = 0, pools = 0, tarnCrowd = 0;
    double worstHigh = 0, worstLow = 0, spanMax = 0;
    int lowland = 0, mountainMin = 1 << 30;
    double rMin = 1e9, rMax = 0;
    long seedsN = 0, seedMax = 0;
    for (uint64_t seed : {1ull, 2ull, 3735928559ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const auto feats = G.waterFeatures(-6000, -6000, 6000, 6000);
        {
            int nl = 0;
            for (const auto& f : feats) nl += f.kind == 0;
            ctx.note("seed " + std::to_string(seed) + ": " + std::to_string(nl) + " lakes in 144 km2");
            if (seed == 3735928559ull) lowland = nl;  // (the lowland seed: plains and forest)
            else mountainMin = std::min(mountainMin, nl);
            seedsN++, seedMax = std::max(seedMax, (long)nl);
        }
        std::vector<std::array<double, 2>> tarns;
        int looked = 0;
        for (const auto& f : feats) {
            if (f.kind != 0) {
                falls++;
                auto ground = [&](double x, double z) { return G.groundHeight(x, z); };
                const double above = ground(f.x - f.fx * 3, f.z - f.fz * 3), below = ground(f.x + f.fx * 7, f.z + f.fz * 7);
                tallFalls += above - below >= 6;
                const Macro m = G.macroAt(f.x + f.fx * 10, f.z + f.fz * 10, true);
                pools += m.creekS < m.creekHW || m.riverS < m.riverHW ? (std::min(m.creekS < m.creekHW ? m.creekDepth : 9.0, m.riverS < m.riverHW ? m.riverDepth : 9.0) > 1.2) : 0;
                continue;
            }
            lakes++, bySub[std::min(4, std::max(0, f.sub))]++;
            rMin = std::min(rMin, f.size), rMax = std::max(rMax, f.size);
            if (f.sub == 2) {
                for (auto& t : tarns) tarnCrowd += (t[0] - f.x) * (t[0] - f.x) + (t[1] - f.z) * (t[1] - f.z) < 1000.0 * 1000.0;
                tarns.push_back({f.x, f.z});
            }
            if (looked++ >= 14) continue;
            // (Its water from the chunks, in a square round it: level; at its edge the land never under it, nor a bank; its bed gentle.)
            const double half = std::min(600.0, 2.5 * f.size + 60);
            const int64_t c0 = floorDiv((int64_t)(f.x - half), kChunk), c1 = floorDiv((int64_t)(f.x + half), kChunk);
            const int64_t d0 = floorDiv((int64_t)(f.z - half), kChunk), d1 = floorDiv((int64_t)(f.z + half), kChunk);
            auto cell = [&](int64_t gx, int64_t gz, double& h, double& w) {
                auto c = G.chunk(floorDiv(gx, kChunk), floorDiv(gz, kChunk));
                const size_t k = (size_t)(gx - c->cx * kChunk) * kChunk + (size_t)(gz - c->cz * kChunk);
                h = c->h[k], w = c->water.empty() ? -1e9 : c->water[k];
                return c->water.empty() ? false : (c->flowX[k] == 0 && c->flowZ[k] == 0 && std::fabs(c->water[k] - f.level) < 0.2);
            };
            double minX = 1e18, maxX = -1e18;
            for (int64_t cx = c0; cx <= c1; cx++)
                for (int64_t cz = d0; cz <= d1; cz++)
                    for (int a = 0; a < kChunk; a += 2)
                        for (int b2 = 0; b2 < kChunk; b2 += 2) {
                            const int64_t gx = cx * kChunk + a, gz = cz * kChunk + b2;
                            double h, w;
                            if (!cell(gx, gz, h, w)) continue;
                            waterCells++, levelN++, levelOff += std::fabs(w - f.level) > 0.05;
                            if (std::fabs(w - f.level) > 0.05 && std::getenv("CZ_LAKE_DBG")) {
                                static int dl = 0;
                                if (dl++ < 6) std::fprintf(stderr, "off level: lake %.0f,%.0f W %.3f: cell %lld,%lld water %.3f h %.2f\n", f.x, f.z, f.level, (long long)gx, (long long)gz, w, h);
                            }
                            minX = std::min(minX, (double)gx), maxX = std::max(maxX, (double)gx);
                            // (at its edge: a dry neighbour 2 m away)
                            const int nb[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
                            bool edge = false;
                            for (auto& q : nb) {
                                double h2, w2;
                                if (cell(gx + q[0], gz + q[1], h2, w2) || w2 > h2) continue;
                                edge = true, edgeN++;
                                if (h2 < f.level - 0.05) {
                                    edgeLow++, worstLow = std::max(worstLow, f.level - h2);
                                    static int dbg = 0;
                                    if (std::getenv("CZ_LAKE_DBG") && f.level - h2 > 1 && dbg++ < 10) {
                                        const Macro mm = G.macroAt(gx + q[0], gz + q[1], false);
                                        std::fprintf(stderr, "leak sub %d lip %.0f,%.0f W %.1f: cell %lld,%lld h %.1f; dry neighbour h %.1f macro h %.1f lake %.1f dam %.1f node %lld,%lld\n", f.sub, f.x, f.z, f.level, (long long)gx, (long long)gz, h, h2, mm.h, mm.lakeLevel, mm.damH, (long long)mm.floorNode, (long long)mm.floorNode2);
                                    }
                                }
                                // (a bank: the ground 3 m out from the edge, over 2.5 m above the water; not a tarn's back wall)
                                double h3, w3;
                                cell(gx + q[0] * 2, gz + q[1] * 2, h3, w3);
                                const bool backWall = f.sub == 2 && (q[0] * f.fx + q[1] * f.fz) < 0;
                                if (!backWall && h3 - f.level > 2.5) edgeHigh++, worstHigh = std::max(worstHigh, h3 - f.level);
                            }
                            if (!edge) {  // (its bed near the shore: the water under 2 m deep, the bed's slope)
                                if (f.level - h < 2) {
                                    double hx, wx, hz, wz;
                                    cell(gx + 2, gz, hx, wx), cell(gx, gz + 2, hz, wz);
                                    bedN++, bedSteep += std::max(std::fabs(hx - h), std::fabs(hz - h)) / 2 > 0.25;
                                }
                            }
                        }
            if (maxX > minX) spanMax = std::max(spanMax, maxX - minX);
        }
    }
    char b[500];
    std::snprintf(b, sizeof b, "6 x 144 km2: %ld lakes (%ld basins, %ld tarns, %ld valley lakes, %ld ponds), the longest looked at %.0f m across; %ld water cells looked at, %ld off the level; lips: %ld of %ld more than 0.5 m off the water",
                  lakes, bySub[1], bySub[2], bySub[3], bySub[4], spanMax, waterCells, levelOff, lipOff, lipN);
    ctx.note(b);
    std::snprintf(b, sizeof b, "edges: %ld points; %ld with the land under the water (worst %.1f m), %ld with a bank over 2.5 m (worst %.1f m); bed near the shore: %ld of %ld steeper than 1:4; tarns within 1 km of another: %ld",
                  edgeN, edgeLow, worstLow, edgeHigh, worstHigh, bedSteep, bedN, tarnCrowd);
    ctx.note(b);
    std::snprintf(b, sizeof b, "falls: %ld of %ld drop 6 m or more within 10 m; %ld with a plunge pool", tallFalls, falls, pools);
    ctx.note(b);
    std::snprintf(b, sizeof b, "lake radii %.0f-%.0f m; at most %ld lakes in one seed's 144 km2", rMin, rMax, seedMax);
    ctx.note(b);
    // (The owner's aim: valley lakes behind natural dams, on the order of 2-6 in 144 km2, of varied sizes.)
    ctx.metric("lakes in 144 km2, on average over the seeds", (double)lakes / std::max(1L, seedsN), "", 1.5, 8);
    ctx.check("no seed crowded with lakes (at most 10 in 144 km2)", seedMax <= 10, std::to_string(seedMax));
    ctx.check("lakes of varied sizes (the biggest over twice the smallest across)", rMax > 2 * rMin);
    ctx.check("lakes: the water lies level", levelOff == 0 && levelN > 1000);
    ctx.check("lakes: at the water's edge the land never under the water (under 1%)", edgeLow * 100 <= edgeN);
    ctx.check("lakes: few banks over 2.5 m at the water's edge (under 10%: where a worn slope comes down to it)", edgeHigh * 10 <= edgeN);
    ctx.check("lakes: the bed near the shore mostly gentle (85% no steeper than 1:4)", bedSteep * 100 <= bedN * 15);
    ctx.check("there are waterfalls", falls > 0);
    ctx.check("waterfalls: where the land drops 6 m or more within 10 m (95%: a few where a neighbour's valley cuts the step back)", tallFalls * 100 >= falls * 95);
    ctx.check("waterfalls: most with a plunge pool below (deeper than the stream: 1.2 m+)", pools * 10 >= falls * 8);
}

SCENARIO(procMemory, "procworld/memory", "What the generator keeps stays bounded: a world's window built, slid three times and its trails laid keeps under 1.5 GB of caches (the chunk cache and the drainage's blocks and tiles all have caps)") {
    clearChunkCache();
    World w = windowAt(4242, 0, 0);
    for (int k = 1; k <= 3; k++) World w2 = windowAt(4242, 512.0 * k, -512.0 * k);
    const double mb = cacheBytes() / 1048576.0;
    long rss = 0;
    if (FILE* f = std::fopen("/proc/self/status", "r")) {  // (the whole test process: noted, not checked; other tests run beside it)
        char line[256];
        while (std::fgets(line, sizeof line, f))
            if (std::sscanf(line, "VmHWM: %ld kB", &rss) == 1) break;
        std::fclose(f);
    }
    ctx.note("the process's peak resident memory so far: " + std::to_string(rss / 1024) + " MB");
    ctx.metric("the generator's caches after a window and three slides", mb, "MB", 0, 1500);
    World::procOriginSet = false;
}

#include "../src/proceros.h"
SCENARIO(procErosion, "procworld/erosion", "The land is worn by erosion in fixed tiles (proceros.h): the same tile wears to the same bytes on two threads at once, the tiles agree across their seams, a tile is made within its budget, and the worn land is worn (gullies cut, not left as it was)") {
    const ProcWorld& G = ProcWorld::forSeed(1);
    clearChunkCache();
    // (Two independent erosions of the same land, on two threads at once: the same bytes.)
    ErosionGrid g0;
    double ox, oz;
    G.erosionInput(-2, 2, g0, ox, oz);
    ErosionGrid g1 = g0, g2 = g0;
    ErosionRun run;
    run.coarseIterations = (int)rules().erosion.coarseIterations, run.iterations = (int)rules().erosion.iterations, run.maxCut = rules().erosion.maxCut;
    std::thread t1([&] { erodeGrid(g1, run); }), t2([&] { erodeGrid(g2, run); });
    t1.join(), t2.join();
    uint64_t h1 = 1469598103934665603ull, h2 = h1;
    for (size_t i = 0; i < g1.h.size(); i++) {
        uint32_t a, b;
        std::memcpy(&a, &g1.h[i], 4), std::memcpy(&b, &g2.h[i], 4);
        h1 = (h1 ^ a) * 1099511628211ull, h2 = (h2 ^ b) * 1099511628211ull;
    }
    ctx.check("the same tile worn on two threads at once: the same bytes", h1 == h2 && g1.h == g2.h);
    ctx.note("seed 1 tile (-2, 2) worn: " + hex(h1));
    // (The tile itself, timed, and its neighbours across the seams.)
    double cell, ms;
    int n;
    const std::vector<float> d = G.erosionTile(-2, 2, ox, oz, n, cell, ms);
    double worn = 0;
    for (float v : d) worn = std::max(worn, (double)-v);
    ctx.check("the land is worn: gullies over 10 m deep", worn > 10, std::to_string(worn) + " m");
    ctx.check("a tile is made within 3 s", ms < 3000, std::to_string((int)ms) + " ms");
    ctx.note("tile (-2, 2): " + std::to_string((int)ms) + " ms to wear");
    double ox2, oz2, ms2;
    int n2;
    const std::vector<float> e = G.erosionTile(-1, 2, ox2, oz2, n2, cell, ms2);
    double sq = 0;
    long cnt = 0;
    for (int a = 0; a < n; a++)
        for (int b = 0; b < n; b++) {
            const int a2 = a + (int)std::lround((ox - ox2) / cell);
            if (a2 < 0 || a2 >= n2) continue;
            const double v = d[(size_t)a * n + b] - e[(size_t)a2 * n2 + b];
            sq += v * v, cnt++;
        }
    const double rms = std::sqrt(sq / std::max(1L, cnt));
    ctx.check("two tiles agree over their shared band (under 1 m RMS)", cnt > 0 && rms < 1.0, std::to_string(rms) + " m over " + std::to_string(cnt));
    double step = 0;
    for (int k = 0; k < 200; k++)
        for (int q = -140; q < 140; q++) {
            const double x = -2048 + q, z = 4096 + 10 + k * 10;
            step = std::max(step, std::fabs(G.erosionAt(x + 1, z) - G.erosionAt(x, z)));
        }
    ctx.check("no step at the seam (under 3 m a metre)", step < 3, std::to_string(step));
    // (Multiplayer: two machines make the same land. From nothing cached, two threads at once each make the same chunk on worn land
    // (its tiles made by whichever gets there first), and another generator for the seed after the caches are dropped: one hash.)
    clearChunkCache();
    uint64_t c1 = 0, c2 = 0;
    clearErosionTiles();
    std::thread u1([&] { c1 = G.makeChunk(-40, 70)->hash(); }), u2([&] { c2 = G.makeChunk(-40, 70)->hash(); });
    u1.join(), u2.join();
    clearChunkCache(), clearErosionTiles();
    const ProcWorld other(1);
    const uint64_t c3 = other.makeChunk(-40, 70)->hash();
    ctx.check("the same worn chunk from two threads at once, and again from nothing", c1 == c2 && c1 == c3, hex(c1) + " " + hex(c2) + " " + hex(c3));
}
