// One map: the region table and its coordinates (onemap.h). The stitching itself is World::stitchRegionLand / stitchRegionContent
// (onemapbuild.cpp).
#include "onemap.h"

#include <algorithm>
#include <cmath>

#include "world.h"

void MapRegion::core(double& lx0, double& lx1, double& lz0, double& lz1) const {
    // (The seam's side and the two beside it are trusted as far as the old map's own land went; the far side, the big map's edge, keeps
    // its rim.)
    const bool alongX = seamSide >= 2;
    lx0 = alongX ? -sideTrust : -kRegionHalf, lx1 = alongX ? sideTrust : kRegionHalf;
    lz0 = alongX ? -kRegionHalf : -sideTrust, lz1 = alongX ? kRegionHalf : sideTrust;
    if (seamSide == 0) lx1 = trust;
    else if (seamSide == 1) lx0 = -trust;
    else if (seamSide == 2) lz1 = trust;
    else lz0 = -trust;
}

double MapRegion::coreOutside(double lx, double lz) const {
    double a, b, c, d;
    core(a, b, c, d);
    const double dx = std::max({a - lx, 0.0, lx - b}), dz = std::max({c - lz, 0.0, lz - d});
    return std::hypot(dx, dz);
}

bool MapRegion::inCore(double lx, double lz) const { return coreOutside(lx, lz) <= 0; }

bool MapRegion::seamward(double lx, double lz) const {
    return seamSide == 0 ? lx > trust : seamSide == 1 ? lx < -trust : seamSide == 2 ? lz > trust : lz < -trust;
}

double MapRegion::seamFade() const {
    // From the core's seam edge in to where the Backcountry's own land is left alone.
    const double edge = seamSide == 0 ? -(mx + trust) : seamSide == 1 ? mx - trust : seamSide == 2 ? -(mz + trust) : mz - trust;
    return std::max(100.0, edge - kSeamFrom);
}

double MapRegion::fadeAt(double lx, double lz, double out) const {
    if (out <= 0) return kSideFade;
    const double over = seamSide == 0 ? lx - trust : seamSide == 1 ? -trust - lx : seamSide == 2 ? lz - trust : -trust - lz;
    const double t = clampd(over / out, 0, 1);
    return kSideFade + (seamFade() - kSideFade) * t;
}

const std::vector<MapRegion>& mapRegions() {
    // (The Open Country's hills closed in from 470 m out, the Mountain's and the Frontier's rims from 520 m.)
    static const std::vector<MapRegion> r = {
        {MAP_WILDS, "OPEN COUNTRY", -3496, 0, 0, 465, 465},
        {MAP_MOUNTAIN, "MOUNTAIN", -1500, -3496, 2, 505, 515},
        {MAP_FRONTIER, "FRONTIER", 1500, -3496, 2, 505, 485},  // (its rim rises from 490 m out on its sides)
    };
    return r;
}

const MapRegion* regionOf(int map) {
    for (const MapRegion& r : mapRegions())
        if (r.map == map) return &r;
    return nullptr;
}

int combinedMapFor(int map) { return isRegionMap(map) ? (int)MAP_BACKCOUNTRY : map; }

V3 regionToWorld(int map, const V3& p) {
    const MapRegion* r = regionOf(map);
    return r ? V3(p.x + r->mx, p.y, p.z + r->mz) : p;
}

V3 worldToRegion(int map, const V3& p) {
    const MapRegion* r = regionOf(map);
    return r ? V3(p.x - r->mx, p.y, p.z - r->mz) : p;
}

const MapRegion* regionContaining(double x, double z) {
    for (const MapRegion& r : mapRegions())
        if (x >= r.x0() && x <= r.x1() && z >= r.z0() && z <= r.z1()) return &r;
    return nullptr;
}

double edgeRidgeOpening(double x, double z) {
    // Open within 150 m of a region's square, the ridge back by 650 m: low hills round each region, so its sides ease into country,
    // not into a wall of mountains (the big map's own edge has mountains of its own: bigMapEdgeRidge).
    double open = 0;
    for (const MapRegion& r : mapRegions()) {
        const double dx = std::max({r.x0() - x, 0.0, x - r.x1()}), dz = std::max({r.z0() - z, 0.0, z - r.z1()});
        open = std::max(open, smoothstep(650, 150, std::hypot(dx, dz)));
    }
    return open;
}

double bigMapEdgeRidge(double x, double z) { return smoothstep(kOneMapHalf - 330, kOneMapHalf - 30, std::max(std::fabs(x), std::fabs(z))); }
