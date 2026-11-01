// One map: the region table and its coordinates (onemap.h). The stitching itself is World::stitchRegionLand / stitchRegionContent
// (onemapbuild.cpp).
#include "onemap.h"
#include "detmath.h"

#include <algorithm>
#include <cmath>

#include "world.h"

void MapRegion::core(double& lx0, double& lx1, double& lz0, double& lz1) const {
    // (Trusted as far as the old map's own land went, on every side: past it the old rim rose, and the country round it is the region's.)
    const bool alongX = seamSide >= 2;
    lx0 = alongX ? -sideTrust : -farTrust, lx1 = alongX ? sideTrust : farTrust;
    lz0 = alongX ? -farTrust : -sideTrust, lz1 = alongX ? farTrust : sideTrust;
    if (seamSide == 0) lx1 = trust;
    else if (seamSide == 1) lx0 = -trust;
    else if (seamSide == 2) lz1 = trust;
    else lz0 = -trust;
}

double MapRegion::coreOutside(double lx, double lz) const {
    double a, b, c, d;
    core(a, b, c, d);
    const double dx = std::max({a - lx, 0.0, lx - b}), dz = std::max({c - lz, 0.0, lz - d});
    return dm::hypot(dx, dz);
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
    // Their grown country (docs/plans/regions2.md): the Open Country the whole west side (2.1 x 5.5 km), the Mountain the south-west
    // with its corner (4.6 x 2.1 km), the Frontier the south-east with its corner (4.9 x 2.1 km). North and east, past the Backcountry's
    // ridge, stay the big map's natural edge.
    constexpr double E = kOneMapHalf - 330;
    static const std::vector<MapRegion> r = {
        {MAP_WILDS, "OPEN COUNTRY", -3496, 0, 0, 465, 465, 465, -E, -kSeamFrom, -kSeamFrom, 2900, 1.0f, 60, 240},
        {MAP_MOUNTAIN, "MOUNTAIN", -1500, -3496, 2, 505, 515, 515, -E, -150, -E, -kSeamFrom, 0.0f, 165, 205},
        {MAP_FRONTIER, "FRONTIER", 1500, -3496, 2, 505, 485, 515, -150, E, -E, -kSeamFrom, 0.0f, 125, 400},  // (its rim rises from 490 m out on its sides)
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

namespace {
// How far (x, z) is outside a region's grown country (negative inside: how far in from its nearest edge).
double outsideCountry(const MapRegion& r, double x, double z) {
    const double dx = std::max(r.ex0 - x, x - r.ex1), dz = std::max(r.ez0 - z, z - r.ez1);
    if (dx <= 0 && dz <= 0) return std::max(dx, dz);
    return dm::hypot(std::max(dx, 0.0), std::max(dz, 0.0));
}
}  // namespace

void regionWeights(double x, double z, double w[3]) {
    w[0] = w[1] = w[2] = 0;
    const double far = std::max(std::fabs(x), std::fabs(z));
    if (far <= kSeamFrom) return;  // (the Backcountry's own, exactly)
    const double ring = smoothstep(kSeamFrom, kSeamFrom + 400, far);
    const std::vector<MapRegion>& rs = mapRegions();
    double a[3], sum = 0;
    for (int k = 0; k < 3; k++) {
        const MapRegion& r = rs[k];
        a[k] = smoothstep(kRegionBlend / 2, -kRegionBlend / 2, outsideCountry(r, x, z));
        // (Between the old map and the Backcountry, across its width, the seam stays the Backcountry's land eased into the old map's
        // edge, as the one map had it: its seam side is where its old land leans on the Backcountry's.)
        const double lx = x - r.mx, lz = z - r.mz;
        const double out = r.seamSide == 0 ? lx : r.seamSide == 1 ? -lx : r.seamSide == 2 ? lz : -lz, along = r.seamSide >= 2 ? lx : lz;
        a[k] *= 1 - smoothstep(r.trust - 40, r.trust + 120, out) * smoothstep(r.sideTrust + 400, r.sideTrust + 100, std::fabs(along));
        sum += a[k];
    }
    const double s = ring / std::max(1.0, sum);
    for (int k = 0; k < 3; k++) w[k] = a[k] * s;
}

const MapRegion* regionAround(double x, double z) {
    double w[3];
    regionWeights(x, z, w);
    for (int k = 0; k < 3; k++)
        if (w[k] >= 0.5) return &mapRegions()[k];
    return nullptr;
}

double openWoods(double, double z) { return smoothstep(700, 1600, z); }
double openCanyon(double, double z) { return smoothstep(-1100, -1800, z); }
double frontierCold(double x, double z) { return smoothstep(2500, 3500, x) * smoothstep(-2700, -3100, z); }

GroundLook groundLookAt(double x, double z) {
    GroundLook g;
    double w[3];
    regionWeights(x, z, w);
    const double W = w[0] + w[1] + w[2];
    if (W <= 0) return g;
    double meadow = 0, rock = g.rockFrom * (1 - W), snow = g.snowFrom * (1 - W);
    const std::vector<MapRegion>& rs = mapRegions();
    for (int k = 0; k < 3; k++) {
        if (w[k] <= 0) continue;
        double m = rs[k].meadow;
        if (rs[k].map == MAP_WILDS) m *= 1 - 0.7 * openWoods(x, z);  // (forest floor under its northern woods)
        meadow += w[k] * m, rock += w[k] * rs[k].rockFrom, snow += w[k] * rs[k].snowFrom;
    }
    g.meadow = (float)meadow, g.rockFrom = (float)rock, g.snowFrom = (float)snow;
    return g;
}

double edgeRidgeOpening(double x, double z) {
    // Open within 150 m of a region's grown country, the ridge back by 650 m: the regions run on from the Backcountry with no wall
    // between, and the ridge stays where the big map's edge is natural country (north, east).
    double open = 0;
    for (const MapRegion& r : mapRegions()) open = std::max(open, smoothstep(650, 150, std::max(0.0, outsideCountry(r, x, z))));
    return open;
}

double bigMapEdgeRidge(double x, double z) { return smoothstep(kOneMapHalf - 330, kOneMapHalf - 30, std::max(std::fabs(x), std::fabs(z))); }
