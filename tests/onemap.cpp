// One map (docs/plans/one-map.md, src/onemap.h): the Backcountry with the Mountain, the Frontier and the Open Country stitched on.
// The regions are where the table says and are their old maps cell for cell; the Backcountry is as it was; the seams have no cliffs
// or holes; everything the old maps had is on dry ground a truck can get to; and the big map builds the same every time.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "../src/autopilot.h"
#include "../src/level.h"
#include "../src/meshcache.h"
#include "../src/onemap.h"
#include "../src/parallel.h"
#include "../src/places.h"
#include "../src/platform.h"
#include "../src/props.h"
#include "../src/worldcache.h"
#include "harness.h"

namespace {
// One big map for the tests that only look at it (it takes a few seconds and half a gigabyte to build).
const World& bigMap() {
    static std::once_flag once;
    static std::unique_ptr<World> w;
    std::call_once(once, [] {
        props::ScopedRockScans none(nullptr);
        World::stitchRegions = true;
        w = std::make_unique<World>(MAP_BACKCOUNTRY);
    });
    return *w;
}
const World& oldMap(int map) {
    static std::mutex lock;
    static std::map<int, std::unique_ptr<World>> built;
    std::lock_guard<std::mutex> lk(lock);
    std::unique_ptr<World>& w = built[map];
    if (!w) {
        props::ScopedRockScans none(nullptr);
        const int was = World::wildsKind;
        World::wildsKind = WILDS_MEADOW;
        w = std::make_unique<World>(map);
        World::wildsKind = was;
    }
    return *w;
}
bool dry(const World& w, const V3& p) { return w.waterLevel(p.x, p.z) < w.height(p.x, p.z) - 0.2; }
// Whether (x, z) is by one of the roads in from the Backcountry (its tread, its cuttings and fills) or a region's start pad, which
// change the ground they're laid on: 4 m cells within `reach` of them.
struct ByRoads {
    std::set<std::pair<int, int>> cells;
    ByRoads(const World& w, double reach) {
        auto mark = [&](const V3& p, double r) {
            for (double dx = -r; dx <= r; dx += 4)
                for (double dz = -r; dz <= r; dz += 4) cells.insert({(int)std::floor((p.x + dx) / 4), (int)std::floor((p.z + dz) / 4)});
        };
        for (const std::vector<V3>& road : w.regionRoads)
            for (size_t k = 0; k < road.size(); k += 2) mark(road[k], reach);
        for (const World::RegionStart& s : w.regionStarts) mark(s.spawn, 45);
    }
    bool operator()(double x, double z) const { return cells.count({(int)std::floor(x / 4), (int)std::floor(z / 4)}) > 0; }
    // The Backcountry's growth comes by 64 m cell, each from its own random numbers, which a road through the cell uses differently:
    // whether the 64 m cell (x, z) is in has a road in it.
    bool cell64(double x, double z) const {
        const int ci = (int)std::floor((x + kOldBackcountryHalf) / 64), cj = (int)std::floor((z + kOldBackcountryHalf) / 64);
        for (int a = ci * 16; a < ci * 16 + 16; a++)
            for (int b = cj * 16; b < cj * 16 + 16; b++)
                if (cells.count({a - (int)(kOldBackcountryHalf / 4), b - (int)(kOldBackcountryHalf / 4)})) return true;
        return false;
    }
};
}  // namespace

SCENARIO(onemapLayout, "onemap/layout", "The regions sit inside the big map, apart from each other and from the Backcountry's places; their coordinates go and come back") {
    const std::vector<MapRegion>& rs = mapRegions();
    ctx.check("three regions: open country, mountain, frontier", rs.size() == 3 && rs[0].map == MAP_WILDS && rs[1].map == MAP_MOUNTAIN && rs[2].map == MAP_FRONTIER);
    ctx.check("the big map's size", std::fabs(World::halfSizeFor(MAP_BACKCOUNTRY) - kOneMapHalf) < 1e-9 && World::gridSizeFor(MAP_BACKCOUNTRY) == 8193);
    bool inside = true, apart = true, clear = true, trip = true, named = true;
    std::string why;
    for (size_t a = 0; a < rs.size(); a++) {
        const MapRegion& r = rs[a];
        inside &= r.x0() >= -kOneMapHalf && r.x1() <= kOneMapHalf && r.z0() >= -kOneMapHalf && r.z1() <= kOneMapHalf;
        for (size_t b = a + 1; b < rs.size(); b++) {
            const MapRegion& q = rs[b];
            const double gx = std::max(q.x0() - r.x1(), r.x0() - q.x1()), gz = std::max(q.z0() - r.z1(), r.z0() - q.z1());
            if (std::max(gx, gz) < 2 * MapRegion::kSideFade) apart = false, why += std::string(" ") + r.name + "/" + q.name;
        }
        // (No place of the Backcountry's in a region's square or its seam band.)
        for (const MapPlace& p : mapPlaces(MAP_BACKCOUNTRY)) {
            const double dx = std::max({r.x0() - p.p.x, 0.0, p.p.x - r.x1()}), dz = std::max({r.z0() - p.p.z, 0.0, p.p.z - r.z1()});
            const double lx = p.p.x - r.mx, lz = p.p.z - r.mz;
            if (std::hypot(dx, dz) < 150 || (r.seamward(lx, lz) && r.coreOutside(lx, lz) < r.seamFade())) clear = false, why += " " + p.id;
        }
        const V3 q(123.5, 7, -88.25), w = regionToWorld(r.map, q), back = worldToRegion(r.map, w);
        trip &= std::fabs(back.x - q.x) < 1e-9 && std::fabs(back.z - q.z) < 1e-9 && back.y == q.y && std::fabs(w.x - q.x - r.mx) < 1e-9;
        named &= std::string(regionAt(MAP_BACKCOUNTRY, r.mx, r.mz)) == r.name && regionContaining(r.mx, r.mz) == &r && regionOf(r.map) == &r;
        named &= combinedMapFor(r.map) == MAP_BACKCOUNTRY;
    }
    ctx.check("inside the map", inside);
    ctx.check("apart from each other", apart, why);
    ctx.check("clear of the Backcountry's places", clear, why);
    ctx.check("coordinates round-trip", trip);
    ctx.check("named: regionAt, regionContaining, regionOf", named);
    ctx.check("not regions: the proving ground, the freeway, the Backcountry", !isRegionMap(MAP_PROVING_GROUND) && !isRegionMap(MAP_FREEWAY) &&
                                                                               !isRegionMap(MAP_BACKCOUNTRY) && combinedMapFor(MAP_FREEWAY) == MAP_FREEWAY);
    ctx.check("Elk Valley is still Elk Valley", std::string(regionAt(MAP_BACKCOUNTRY, 0, 0)) == "ELK VALLEY");
}

SCENARIO(onemapRegionCopy, "onemap/region-copy", "Each region's core is its old map cell for cell (heights, surfaces, paint, water), with its trees, rocks, stations, pads and start moved over") {
    const World& big = bigMap();
    const ByRoads byRoad(big, 26);
    for (const MapRegion& r : mapRegions()) {
        const World& old = oldMap(r.map);
        double cx0, cx1, cz0, cz1;
        r.core(cx0, cx1, cz0, cz1);
        long cells = 0, same = 0;
        std::string bad;
        for (double lx = cx0; lx <= cx1; lx += 3)
            for (double lz = cz0; lz <= cz1; lz += 3) {
                const int ob = old.cellIndex(lx, lz), bb = big.cellIndex(lx + r.mx, lz + r.mz);
                if (ob < 0 || bb < 0 || byRoad(lx + r.mx, lz + r.mz)) continue;
                cells++;
                const bool h = big.cellHeight(bb) == old.cellHeight(ob), s = big.cellSurface(bb) == old.cellSurface(ob);
                const double wo = old.waterLevel(lx, lz), wb = big.waterLevel(lx + r.mx, lz + r.mz);
                const bool wet = (wo < -1e8 && wb < -1e8) || std::fabs(wo - wb) < 1e-6;
                if (h && s && wet) same++;
                else if (bad.size() < 200) bad += " (" + std::to_string((int)lx) + "," + std::to_string((int)lz) + (h ? "" : " h") + (s ? "" : " s") + (wet ? "" : " w") + ")";
            }
        // (Where the road in from the Backcountry crosses the core, the cells are the road's: those aren't counted.)
        ctx.check(std::string(r.name) + ": the core's cells", cells > 10000 && same == cells, std::to_string(same) + "/" + std::to_string(cells) + bad);
        // Its trees in the core, every one, moved.
        auto inCore = [&](double x, double z) { return r.inCore(x, z) && !byRoad(x + r.mx, z + r.mz); };
        std::multiset<std::pair<long, long>> want, got;
        for (const Tree& t : old.trees)
            if (inCore(t.base.x, t.base.z)) want.insert({std::lround((t.base.x + r.mx) * 100), std::lround((t.base.z + r.mz) * 100)});
        for (const Tree& t : big.trees)
            if (inCore(t.base.x - r.mx, t.base.z - r.mz)) got.insert({std::lround(t.base.x * 100), std::lround(t.base.z * 100)});
        size_t missing = 0;
        for (const auto& p : want) missing += got.count(p) == 0;
        ctx.check(std::string(r.name) + ": its trees", !want.empty() && missing == 0,
                  std::to_string(missing) + " of " + std::to_string(want.size()) + " missing");
        int rocksWant = 0, rocksGot = 0;
        for (const Rock& k : old.rocks) rocksWant += inCore(k.c.x, k.c.z);
        for (const Rock& k : big.rocks) rocksGot += inCore(k.c.x - r.mx, k.c.z - r.mz);
        ctx.check(std::string(r.name) + ": its rocks", rocksGot == rocksWant, std::to_string(rocksGot) + " of " + std::to_string(rocksWant));
        // Its stations and spawn pads, moved.
        int stations = 0;
        for (const Station& s : old.stations)
            for (const Station& b : big.stations) stations += std::string(s.name) == b.name && std::hypot(b.p.x - s.p.x - r.mx, b.p.z - s.p.z - r.mz) < 0.01;
        ctx.check(std::string(r.name) + ": its stations", stations == (int)old.stations.size(), std::to_string(stations) + "/" + std::to_string(old.stations.size()));
        int pads = 0;
        for (const SpawnPad& s : old.spawnPads)
            for (const SpawnPad& b : big.spawnPads) pads += std::hypot(b.p.x - s.p.x - r.mx, b.p.z - s.p.z - r.mz) < 0.01;
        ctx.check(std::string(r.name) + ": its spawn pads", pads == (int)old.spawnPads.size());
        bool started = false;
        for (const World::RegionStart& s : big.regionStarts) {
            if (s.map != r.map) continue;
            started = true;
            if (!old.spawnPads.empty()) started &= std::hypot(s.spawn.x - old.spawn.x - r.mx, s.spawn.z - old.spawn.z - r.mz) < 0.01;
            // (Its start has a pad, whether it came with one or got one here.)
            bool padded = false;
            for (const SpawnPad& b : big.spawnPads) padded |= std::hypot(b.p.x - s.spawn.x, b.p.z - s.spawn.z) < 0.5;
            started &= padded && dry(big, s.spawn);
        }
        ctx.check(std::string(r.name) + ": its start, on a pad on dry ground", started);
        std::printf("REGION %-12s cells %ld/%ld  trees %zu  rocks %d/%d  stations %d  pads %d\n", r.name, same, cells, want.size(), rocksGot, rocksWant, stations, pads);
    }
    ctx.check("the Mountain's loop road is the big map's", !big.loopRoad.empty() && std::fabs(big.loopLength - oldMap(MAP_MOUNTAIN).loopLength) < 1e-6 &&
                                                                  std::fabs(big.loopRoad[0].x - oldMap(MAP_MOUNTAIN).loopRoad[0].x - regionOf(MAP_MOUNTAIN)->mx) < 1e-9);
    World w = big;
    ctx.check("startAtRegion", w.startAtRegion(MAP_FRONTIER) && std::hypot(w.spawn.x - oldMap(MAP_FRONTIER).spawn.x - regionOf(MAP_FRONTIER)->mx, w.spawn.z - oldMap(MAP_FRONTIER).spawn.z - regionOf(MAP_FRONTIER)->mz) < 0.01 &&
                                     !w.startAtRegion(MAP_FREEWAY));
    V3 p;
    double yaw = 0;
    ctx.check("...and its pad's slots", w.spawnSlot(1, p, yaw) && std::hypot(p.x - w.spawn.x, p.z - w.spawn.z) < 30);
}

SCENARIO(onemapBackcountryKept, "onemap/backcountry-kept", "The Backcountry within 2.6 km of its middle is as the 6 km map had it: its land, and every tree, rock, log and bush") {
    const World& big = bigMap();
    std::unique_ptr<World> old;
    {
        props::ScopedRockScans none(nullptr);
        World::stitchRegions = false;
        old = std::make_unique<World>(MAP_BACKCOUNTRY);
        World::stitchRegions = true;
    }
    ctx.check("the 6 km map without the regions", std::fabs(old->halfSize() - kOldBackcountryHalf) < 1e-9 && old->regionStarts.empty() && big.regionStarts.size() == 3);
    const double R = kSeamFrom - 50;
    const ByRoads byRoad(big, 26);  // (the roads out to the regions start at three of its places: theirs)
    // (And a place's yard where a road starts is levelled to the ground round it, which the road changed by a few millimetres.)
    auto nearPad = [&](double x, double z) {
        for (const char* id : {"BC.RANGER", "BC.FUEL_LAKE"})
            if (const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id); p && std::hypot(x - p->p.x, z - p->p.z) < p->pad * 1.5 + 70) return true;  // (+70: the trail network's trails out of these yards are pinned to their ground)
        return false;
    };
    long cells = 0, same = 0;
    double worst = 0;
    for (double x = -R; x <= R; x += 2)
        for (double z = -R; z <= R; z += 2) {
            const int a = big.cellIndex(x, z), b = old->cellIndex(x, z);
            if (byRoad(x, z) || nearPad(x, z)) continue;
            cells++;
            const double d = std::fabs(big.cellHeight(a) - old->cellHeight(b));
            worst = std::max(worst, d);
            const bool ok = d == 0 && big.cellSurface(a) == old->cellSurface(b) && big.cellPaint(a) == old->cellPaint(b);
            same += ok;
            if (!ok && cells - same < 12)
                std::printf("DIFF at %.0f,%.0f: h %.2f/%.2f surf %d/%d paint %d/%d\n", x, z, big.cellHeight(a), old->cellHeight(b), big.cellSurface(a), old->cellSurface(b),
                            big.cellPaint(a), old->cellPaint(b));
        }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%ld/%ld, worst height %.3f m", same, cells, worst);
    // (Elk River's west end now flows into its mouth lake, west of 2.6 km: within, the land is the same.)
    ctx.check("its land, cell for cell", same == cells, buf);
    auto key = [](const V3& p) { return std::make_pair(std::lround(p.x * 100), std::lround(p.z * 100)); };
    auto within = [&](const V3& p) { return std::max(std::fabs(p.x), std::fabs(p.z)) < R && !byRoad.cell64(p.x, p.z); };
    // (Rocks: the new country's big outcrops stop at 400, counted cell by cell from the west; out on the seams the land is lower now, so
    // the 400 run out a little later, and a few cells further east get a big rock where they had a small one: under 1% of them.)
    auto compare = [&](const char* what, auto get, double allowed = 0) {
        std::multiset<std::pair<long, long>> a, b;
        get(big, a), get(*old, b);
        size_t diff = 0;
        for (const auto& k : a)
            if (b.count(k) == 0 && ++diff <= 4) std::printf("ONLY BIG %s at %.2f, %.2f\n", what, k.first / 100.0, k.second / 100.0);
        for (const auto& k : b)
            if (a.count(k) == 0 && ++diff <= 8) std::printf("ONLY OLD %s at %.2f, %.2f\n", what, k.first / 100.0, k.second / 100.0);
        ctx.check(std::string("its ") + what, diff <= allowed * b.size() && !a.empty(), std::to_string(diff) + " differ of " + std::to_string(b.size()));
    };
    compare("trees", [&](const World& w, auto& s) { for (const Tree& t : w.trees) if (within(t.base)) s.insert(key(t.base)); });
    compare("rocks", [&](const World& w, auto& s) { for (const Rock& k : w.rocks) if (within(k.c)) s.insert(key(k.c)); }, 0.015);
    compare("logs", [&](const World& w, auto& s) { for (const Log& l : w.logs) if (within(l.a)) s.insert(key(l.a)); });
    // (Bushes: the trail network's fill (bctrails.cpp) puts a handful differently where the big map's land differs by millimetres.)
    compare("bushes", [&](const World& w, auto& s) { for (const Bush& u : w.bushes) if (within(u.p)) s.insert(key(u.p)); }, 0.0005);
    compare("boxes and models", [&](const World& w, auto& s) {
        for (const Box& u : w.boxes) if (within(u.c)) s.insert(key(u.c));
        for (const Placed& m : w.models) if (within(m.p)) s.insert(key(m.p));
    });
    ctx.check("its start", std::hypot(big.spawn.x - old->spawn.x, big.spawn.z - old->spawn.z) < 1e-6 && big.spawnYaw == old->spawnYaw);
}

SCENARIO(onemapSeams, "onemap/seams", "Where a region meets the Backcountry and the mountains round it: no cliffs past a slope bound, no holes, no pits") {
    const World& w = bigMap();
    // The Backcountry's own land there, before anything was laid on it (its badlands' mesas have cliffs of their own: not the seam's).
    std::vector<float> land;
    const int n = w.gridSize();
    backcountryLand(w.halfSize(), n, land);
    auto landAt = [&](double x, double z) {
        const int i = std::min(std::max((int)std::lround(x + w.halfSize()), 0), n - 1), j = std::min(std::max((int)std::lround(z + w.halfSize()), 0), n - 1);
        return (double)land[(size_t)i * n + j];
    };
    auto landSlope = [&](double x, double z) { return std::hypot(landAt(x + 1, z) - landAt(x - 1, z), landAt(x, z + 1) - landAt(x, z - 1)) / 2; };
    const ByRoads byRoad(w, 24);  // (a road's cuttings and fills are the road's: onemap/roads)
    for (const MapRegion& r : mapRegions()) {
        const World& old = oldMap(r.map);
        double cx0, cx1, cz0, cz1;
        r.core(cx0, cx1, cz0, cz1);
        // The steepest the old map itself was by its core's edge (a range there stays a range).
        double edgeSteep = 0;
        auto slopeOf = [](const World& m, double x, double z) {
            return std::hypot(m.height(x + 1, z) - m.height(x - 1, z), m.height(x, z + 1) - m.height(x, z - 1)) / 2;
        };
        for (double t = -kRegionHalf; t <= kRegionHalf; t += 2) {
            const double ex = r.seamSide == 0 ? cx1 - 1 : r.seamSide == 1 ? cx0 + 1 : clampd(t, cx0, cx1);
            const double ez = r.seamSide == 2 ? cz1 - 1 : r.seamSide == 3 ? cz0 + 1 : clampd(t, cz0, cz1);
            edgeSteep = std::max(edgeSteep, slopeOf(old, ex, ez));
        }
        double steepSeam = 0, steepSide = 0;
        int holes = 0, pits = 0, samples = 0;
        V3 worstAt, sideAt;
        int rangeFaces = 0;
        double steepRange = 0;
        const double reach = std::max(r.seamFade(), MapRegion::kSideFade);
        for (double x = r.x0() - reach; x <= r.x1() + reach; x += 2)
            for (double z = r.z0() - reach; z <= r.z1() + reach; z += 2) {
                if (std::max(std::fabs(x), std::fabs(z)) > kOneMapHalf - 340) continue;  // (the big map's own edge mountains)
                const double lx = x - r.mx, lz = z - r.mz, out = r.coreOutside(lx, lz);
                const bool seam = r.seamward(lx, lz);
                if (out <= 0 || out > r.fadeAt(lx, lz, out)) continue;
                samples++;
                const double h = w.height(x, z);
                if (!std::isfinite(h) || h < -40 || h > 600) holes++;
                // (Right by the core's edge the land is still mostly the old map's own, cliffs and all, smoothed over the first 40 m out:
                // the blend is judged past that. And Elk River's own canyon, which the seam doesn't touch, isn't the seam's.)
                bool river = false;
                for (const River& rv : w.rivers)
                    for (size_t k = 0; k < rv.spots.size() && !river; k += 2) river = std::hypot(rv.spots[k].p.x - x, rv.spots[k].p.z - z) < 45;
                // What the blend itself adds to the slope: the slope of (the land now - the Backcountry's own land there).
                auto added = [&](double ax, double az) { return w.height(ax, az) - landAt(ax, az); };
                const double s = out < 40 || river || byRoad(x, z) ? 0 : std::hypot(added(x + 1, z) - added(x - 1, z), added(x, z + 1) - added(x, z - 1)) / 2;
                if (seam && s > 1.0) {
                    // (A range along the core's edge, 50 m and more over the Backcountry's land within 200 m of here: the seam is that
                    // range's face, and as steep as a mountainside may be.)
                    bool range = false;
                    for (double t = -200; t <= 200 && !range; t += 10) {
                        const double ex = clampd(lx + (r.seamSide >= 2 ? t : 0), cx0, cx1), ez = clampd(lz + (r.seamSide >= 2 ? 0 : t), cz0, cz1);
                        range = old.height(ex, ez) - landAt(ex + r.mx, ez + r.mz) > 50;
                    }
                    if (range) rangeFaces++;
                    else if (s > steepSeam) steepSeam = s, worstAt = V3(x, h, z);
                    if (range && s > steepRange) steepRange = s;
                } else if (seam && s > steepSeam) steepSeam = s, worstAt = V3(x, h, z);
                if (!seam && s > steepSide) steepSide = s, sideAt = V3(x, h, z);
                // A pit: lower than everything round it by over a metre, and dry.
                double lowest = 1e9;
                for (int a = -1; a <= 1; a++)
                    for (int b = -1; b <= 1; b++)
                        if (a || b) lowest = std::min(lowest, w.height(x + a * 2, z + b * 2));
                if (h < lowest - 1.0 && dry(w, V3(x, h, z))) pits++;
            }
        char buf[260];
        std::snprintf(buf, sizeof buf, "%s: %d samples, steepest %.2f on the seam (at %.0f, %.0f) and %.2f beside it (at %.0f, %.0f); its own edge %.2f", r.name, samples,
                      steepSeam, worstAt.x, worstAt.z, steepSide, sideAt.x, sideAt.z, edgeSteep);
        std::printf("SEAM %s\n", buf);
        ctx.check(std::string(r.name) + ": no holes or pits", holes == 0 && pits == 0, std::to_string(holes) + " holes, " + std::to_string(pits) + " pits");
        // (The blend adds at most about 1.5 x the height between the two over the band; past that, only what either side had.)
        // (Past what the Backcountry's own land was: a slope over 1 there counts only for what it adds.)
        std::printf("SEAM %s: %d samples steeper than 1 on a range's face (steepest %.2f)\n", r.name, rangeFaces, steepRange);
        ctx.check(std::string(r.name) + ": the seam's blend no steeper than 1 (45 degrees)", steepSeam <= 1.0, buf);
        ctx.check(std::string(r.name) + ": where a range meets low ground, a mountainside (no steeper than 2: 63 degrees)", steepRange <= 2.0, buf);
        ctx.check(std::string(r.name) + ": its sides ease into the hills", steepSide <= 1.2, buf);
    }
}

SCENARIO(onemapSame, "onemap/same-every-time", "The big map builds the same on one thread or many, and the world cache gives back what was built (the multiplayer world check)") {
    const World& a = bigMap();
    const uint64_t ha = a.landHash();
    std::unique_ptr<World> b;
    {
        props::ScopedRockScans none(nullptr);
        const int cap = parallelRowsCap;
        parallelRowsCap = 1;
        b = std::make_unique<World>(MAP_BACKCOUNTRY);
        parallelRowsCap = cap;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%016llx vs %016llx", (unsigned long long)ha, (unsigned long long)b->landHash());
    std::printf("LANDHASH big map %016llx\n", (unsigned long long)ha);
    ctx.check("one thread or many: the same land hash", ha == b->landHash(), buf);
    // Through the cache: encoded, decoded, the same (every height and cell, its starts).
    const std::vector<uint8_t> bytes = worldcache::encode(a);
    World c(MAP_PROVING_GROUND);
    const bool ok = worldcache::decode(bytes, c);
    ctx.check("the world cache reads it back", ok && c.map == MAP_BACKCOUNTRY && c.gridSize() == a.gridSize());
    if (!ok) return;
    c.cutCachedRocks();
    c.buildGrid();
    bool cells = true;
    for (int id = 0; id < a.gridSize() * a.gridSize() && cells; id += 7)
        cells = a.cellHeight(id) == c.cellHeight(id) && a.cellSurface(id) == c.cellSurface(id) && a.cellPaint(id) == c.cellPaint(id);
    ctx.check("...every cell", cells);
    ctx.check("...the same land hash", c.landHash() == ha);
    bool starts = c.regionStarts.size() == a.regionStarts.size();
    for (size_t k = 0; starts && k < a.regionStarts.size(); k++)
        starts = c.regionStarts[k].map == a.regionStarts[k].map && len(c.regionStarts[k].spawn - a.regionStarts[k].spawn) < 1e-9 &&
                 c.regionStarts[k].parking.size() == a.regionStarts[k].parking.size();
    ctx.check("...its regions' starts", starts);
    std::printf("CACHE big map: %.1f MB\n", bytes.size() / 1048576.0);
}

SCENARIO(onemapRoads, "onemap/roads", "A gravel road from the Backcountry into each region: from its place to the region's start, loop road or lodge, no steeper than 13%, dry, its banks no cliffs") {
    const World& w = bigMap();
    ctx.check("three roads", w.regionRoads.size() == 3, std::to_string(w.regionRoads.size()));
    struct End {
        const char* what;
        V3 from;
        int region;
    };
    const End ends[] = {{"Ranger Station -> Open Country", findPlace(MAP_BACKCOUNTRY, "BC.RANGER")->p, MAP_WILDS},
                        {"Lakeshore Fuel -> Mountain", findPlace(MAP_BACKCOUNTRY, "BC.FUEL_LAKE")->p, MAP_MOUNTAIN},
                        {"Lost Mine wash -> Frontier", V3(2445, 0, -1762), MAP_FRONTIER}};
    for (const End& e : ends) {
        const std::vector<V3>* road = nullptr;
        for (const std::vector<V3>& r : w.regionRoads)
            if (!r.empty() && std::hypot(r.front().x - e.from.x, r.front().z - e.from.z) < 40) road = &r;
        ctx.check(std::string(e.what) + ": there", road != nullptr);
        if (!road) continue;
        const MapRegion* rg = regionOf(e.region);
        const V3 last = road->back();
        double L = 0, grade = 0, bank = 0;
        V3 bankAt;
        int wet = 0;
        for (size_t k = 1; k < road->size(); k++) {
            const V3 &a = (*road)[k - 1], &b = (*road)[k];
            L += std::hypot(b.x - a.x, b.z - a.z);
            if (k >= 6) grade = std::max(grade, std::fabs(w.height(b.x, b.z) - w.height((*road)[k - 6].x, (*road)[k - 6].z)) / 6);
            wet += !dry(w, b);
            // Its banks: either side, out to where the land is its own again.
            const V3 d = norm(V3(b.x - a.x, 0, b.z - a.z)), side(-d.z, 0, d.x);
            for (int sd = -1; sd <= 1; sd += 2)
                for (double o = 4; o < 10; o += 2) {  // (its cutting or fill; further out it's the land's own, a badlands mesa's cliff and all)
                    const V3 p = b + side * (sd * o), q = b + side * (sd * (o + 2));
                    const double bk = std::fabs(w.height(q.x, q.z) - w.height(p.x, p.z)) / 2;
                    if (bk > bank) bank = bk, bankAt = p;
                }
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s: %.2f km, steepest %.1f%%, banks %.2f (at %.0f, %.0f), %d m wet", e.what, L / 1000, grade * 100, bank, bankAt.x, bankAt.z, wet);
        std::printf("ROAD %s\n", buf);
        ctx.check(std::string(e.what) + ": ends in its region", rg && rg->inCore(last.x - rg->mx, last.z - rg->mz) || (rg && rg->coreOutside(last.x - rg->mx, last.z - rg->mz) < 60), buf);
        ctx.check(std::string(e.what) + ": no steeper than 13%", grade <= 0.13, buf);
        ctx.check(std::string(e.what) + ": dry all the way", wet == 0, buf);
        ctx.check(std::string(e.what) + ": its banks no cliffs", bank <= 1.2, buf);
        // Gravel, as the road's paint and surface say.
        int gravel = 0, n = 0;
        for (size_t k = 20; k + 20 < road->size(); k += 10, n++) gravel += w.surfaceAt((*road)[k].x, (*road)[k].z) == SURF_GRAVEL && w.roadAt((*road)[k].x, (*road)[k].z).paint == PAINT_ROAD;
        ctx.check(std::string(e.what) + ": gravel all along", gravel == n, std::to_string(gravel) + "/" + std::to_string(n));
    }
}

namespace {
// What each old map had to find, start from and drive to (its features, as the autopilot's and discovery's lists have them), in its
// own coordinates.
std::vector<std::pair<std::string, V3>> oldMapSpots(int map) {
    std::vector<std::pair<std::string, V3>> v;
    if (map == MAP_MOUNTAIN)
        for (const char* f : {"trailhead", "lookout", "dome", "gully", "halfway", "garage", "pit", "whoops"}) v.push_back({f, mountainFeature(f)});
    // (The Frontier's frozen tarn and slickrock bowl aren't here: the planner finds no way to them on the Frontier itself either. Echo Lake
    // is its beach, as the DRIVE TO list has it.)
    if (map == MAP_FRONTIER) {
        for (const char* f : {"start", "mesaclimb", "wall", "summit", "ford1", "bog", "delta", "mudhill", "snowfield", "lodge", "outpost", "lakeside", "hut"})
            v.push_back({f, frontierFeature(f)});
        v.push_back({"lake beach", frontierFeature("lake") + V3(-70, 0, 30)});
    }
    return v;
}
}  // namespace

SCENARIO(onemapPlacesReachable, "onemap/places-reachable", "Every region's start, stations and named places are on dry ground on the big map, and the autopilot (a Bronco, anywhere) finds a way to each from the Backcountry") {
    const World& w = bigMap();
    const VehicleSpec& bronco = vehicleCatalog()[2];
    struct Target {
        std::string name;
        V3 p;
    };
    std::vector<Target> todo;
    for (const World::RegionStart& s : w.regionStarts) todo.push_back({std::string(mapName(s.map)) + " START", s.spawn});
    for (const MapRegion& r : mapRegions()) {
        for (const auto& f : oldMapSpots(r.map)) todo.push_back({std::string(r.name) + " " + f.first, regionToWorld(r.map, f.second)});
        for (const Station& st : oldMap(r.map).stations) todo.push_back({std::string(r.name) + " " + st.name, regionToWorld(r.map, st.p)});
    }
    // Dry, where it was dry on its own map.
    std::string wet;
    for (const Target& t : todo) {
        const MapRegion* r = regionContaining(t.p.x, t.p.z);
        const V3 own = r ? worldToRegion(r->map, t.p) : t.p;
        const bool wasDry = !r || dry(oldMap(r->map), own);
        if (wasDry && !dry(w, t.p)) wet += " " + t.name;
    }
    ctx.check("on dry ground (where they were dry before)", wet.empty(), wet);
    // Reachable: from the Backcountry's places where the roads in start, the nearest target first, each from the nearest one reached.
    std::vector<V3> reached;
    for (const char* id : {"BC.RANGER", "BC.FUEL_LAKE", "BC.LOST_MINE"}) {
        const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
        reached.push_back(V3(p->p.x, w.height(p->p.x, p->p.z), p->p.z));
    }
    std::string unreached;
    int ok = 0;
    std::vector<Target> later;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) todo = later, later.clear();
        while (!todo.empty()) {
            size_t bi = 0;
            double best = 1e18;
            for (size_t i = 0; i < todo.size(); i++)
                for (const V3& r : reached)
                    if (std::hypot(todo[i].p.x - r.x, todo[i].p.z - r.z) < best) best = std::hypot(todo[i].p.x - r.x, todo[i].p.z - r.z), bi = i;
            const Target t = todo[bi];
            todo.erase(todo.begin() + bi);
            std::vector<size_t> order(reached.size());
            for (size_t j = 0; j < order.size(); j++) order[j] = j;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return std::hypot(t.p.x - reached[a].x, t.p.z - reached[a].z) < std::hypot(t.p.x - reached[b].x, t.p.z - reached[b].z);
            });
            const V3 goal(t.p.x, w.height(t.p.x, t.p.z), t.p.z);
            std::vector<V3> route;
            bool found = false;
            for (size_t k = 0; k < order.size() && k < (pass == 0 ? 4u : 60u) && !found; k++) found = planRoute(w, bronco, reached[order[k]], goal, PILOT_ANYWHERE, {}, route);
            std::printf("REACH %-34s %s\n", t.name.c_str(), found ? "ok" : "NONE");
            if (found) ok++, reached.push_back(goal);
            else if (pass == 0) later.push_back(t);
            else unreached += " " + t.name;
        }
    }
    ctx.check("a way to every one", unreached.empty(), unreached);
    std::printf("REACHED %d\n", ok);
}

SCENARIO(onemapOldLevels, "onemap/old-levels", "Levels made on the Mountain, the Frontier and the open country play in their regions of the big map: moved over whole, on the same ground") {
    // The game's own levels on the old maps: their starts and gates on the big map stand on the ground they stood on.
    const World& big = bigMap();
    const ByRoads byRoad(big, 26);
    int levels = 0;
    std::string bad;
    for (const Level& lv : builtinLevels()) {
        if (lv.map != MAP_MOUNTAIN && lv.map != MAP_FRONTIER) continue;
        Level placed = lv;
        int from = -1;
        if (!placeInRegion(placed, &from) || placed.map != MAP_BACKCOUNTRY || from != lv.map) {
            bad += " not-moved:" + lv.name;
            continue;
        }
        levels++;
        const World& old = oldMap(lv.map);
        std::vector<V3> spots;
        for (const Gate& g : lv.gates) spots.push_back(g.p);
        if (lv.hasStart) spots.push_back(lv.start);
        for (size_t k = 0; k < spots.size(); k++) {
            const V3 at = regionToWorld(lv.map, spots[k]);
            if (byRoad(at.x, at.z)) continue;
            if (std::fabs(old.height(spots[k].x, spots[k].z) - big.height(at.x, at.z)) > 1e-9) bad += " ground:" + lv.name;
        }
        for (size_t k = 0; k < lv.gates.size(); k++)
            if (len(placed.gates[k].p - regionToWorld(lv.map, lv.gates[k].p)) > 1e-9) bad += " gate:" + lv.name;
    }
    ctx.check("the game's levels on the Mountain and the Frontier, on their ground", levels >= 3 && bad.empty(), std::to_string(levels) + bad);

    // A level file as an older build wrote it, on the Mountain: cells edited, things placed and taken away, a start and a gate.
    Level lv;
    lv.name = "OLD MOUNTAIN FILE", lv.map = MAP_MOUNTAIN, lv.mode = LEVEL_SPRINT;
    const V3 th = mountainFeature("trailhead");
    lv.hasStart = true, lv.start = th + V3(30, 0, -40), lv.startYaw = 0.3;
    lv.gates.push_back({th + V3(80, 0, -40), 0.3, 14});
    const World& mt = oldMap(MAP_MOUNTAIN);
    const int c1 = mt.cellIndex(th.x + 60, th.z - 70), c2 = mt.cellIndex(th.x + 61, th.z - 70);
    lv.heights[c1] = 7.25f, lv.heights[c2] = 7.5f;
    lv.surfaces[c1] = (uint16_t)(SURF_MUD << 8 | PAINT_MUD);
    LevelObject rock;
    rock.kind = OBJ_ROCK, rock.p = th + V3(50, 0, -20), rock.size = V3(1.2, 0.8, 1.0), rock.top = 0.6, rock.seed = 77;
    lv.objects.push_back(rock);
    LevelObject box;
    box.kind = OBJ_BOX, box.p = th + V3(55, 2, -25), box.size = V3(2, 0.5, 1), box.col[0] = 0.5f;
    lv.objects.push_back(box);
    lv.removed.push_back(V3(th.x + 10, 0, th.z + 10));
    Level file;
    ctx.check("an older build's text reads", parseLevelText(saveLevelText(lv), file) && file.map == MAP_MOUNTAIN && file.heights.size() == 2);
    Level placed = file;
    int from = -1;
    ctx.check("moved into the Mountain's region", placeInRegion(placed, &from) && placed.map == MAP_BACKCOUNTRY && from == MAP_MOUNTAIN);
    const V3 off(regionOf(MAP_MOUNTAIN)->mx, 0, regionOf(MAP_MOUNTAIN)->mz);
    ctx.check("its start, gate, things and removals moved by the region's offset",
              len(placed.start - (lv.start + off)) < 1e-9 && len(placed.gates[0].p - (lv.gates[0].p + off)) < 1e-9 && len(placed.objects[0].p - (rock.p + off)) < 1e-9 &&
                  len(placed.objects[1].p - (box.p + off)) < 1e-9 && std::fabs(placed.removed[0].x - (th.x + 10 + off.x)) < 1e-9 &&
                  std::fabs(placed.removed[0].z - (th.z + 10 + off.z)) < 1e-9);
    World w = levelWorld(placed);
    applyLevel(placed, w);
    const int b1 = w.cellIndex(th.x + 60 + off.x, th.z - 70 + off.z), b2 = w.cellIndex(th.x + 61 + off.x, th.z - 70 + off.z);
    ctx.check("its cells edited where they were, on the big map's grid", b1 >= 0 && w.cellHeight(b1) == 7.25f && w.cellHeight(b2) == 7.5f && w.cellSurface(b1) == SURF_MUD &&
                                                                               w.cellPaint(b1) == PAINT_MUD);
    bool rockThere = false;
    for (const Rock& k : w.rocks) rockThere |= std::hypot(k.c.x - (rock.p.x + off.x), k.c.z - (rock.p.z + off.z)) < 0.5;
    ctx.check("its rock stands where it was put", rockThere);
    V3 p;
    double yaw = 0;
    ctx.check("it starts at its own start, on a pad", std::hypot(w.spawn.x - placed.start.x, w.spawn.z - placed.start.z) < 2 && w.spawnSlot(0, p, yaw));
    // The open country: its meadow levels go to its region; its other kinds (desert, forest...) stay on their own small maps.
    Level meadow;
    meadow.map = MAP_WILDS, meadow.terrain = WILDS_MEADOW, meadow.hasStart = true, meadow.start = V3(5, 0, 5);
    Level desert = meadow;
    desert.terrain = WILDS_DESERT;
    Level pg;
    pg.map = MAP_PROVING_GROUND;
    ctx.check("the open country's meadow: moved", placeInRegion(meadow) && meadow.map == MAP_BACKCOUNTRY && std::fabs(meadow.start.x - (5 + regionOf(MAP_WILDS)->mx)) < 1e-9);
    ctx.check("its desert, and the proving ground: as they were", !placeInRegion(desert) && desert.map == MAP_WILDS && !placeInRegion(pg) && pg.map == MAP_PROVING_GROUND);
}
