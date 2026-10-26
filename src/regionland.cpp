// The country grown round the big map's regions (regionland.h, docs/plans/regions2.md).
//
// Only polynomials, smoothsteps, the lattice noises and std::sin go into the land, as in the maps' own: the same heights on every
// machine (the multiplayer world check, World::landHash).
#include "regionland.h"

#include <algorithm>
#include <cmath>

#include "world.h"

namespace {
double sq(double v) { return v * v; }

// The Frontier's canyon: its centre line across the cold bench (z at x), and its floor's height there.
double canyonZ(double x) { return -3900 + 260 * std::sin(x * 0.0019 + 0.7) + 90 * std::sin(x * 0.0061 - 1.3); }
double canyonFloor(double x) { return 7 + 0.026 * std::max(0.0, x - 2300); }

double mountainCountry(const MapRegion& r, double x, double z) {
    const double cd = r.coreOutside(x - r.mx, z - r.mz);
    const double grow = smoothstep(60, 550, cd);  // (the valley the old map sits in, opening into the ranges round it)
    const double floor = 12 + 10 * (fbmNoise(x * 0.0025 + 3.7, z * 0.0025 - 8.1, 3) - 0.5);
    const double hills = 40 * (fbmNoise(x * 0.006 - 2.2, z * 0.006 + 5.3, 3) - 0.4);
    // Massifs (a kilometre or so across) with sharp ridgelines on them, broad valleys between.
    const double crest = ridgedNoise(x * 0.0021 + 6.1, z * 0.0021 - 2.4, 3);
    const double big = smoothstep(0.25, 0.65, fbmNoise(x * 0.0011 + 1.3, z * 0.0011 + 9.9, 3));
    // Higher the further from the valley and toward the map's south edge and its south-west corner: the high range there, which the
    // pass crosses.
    const double outer = std::min(1.0, 0.5 * smoothstep(300, 1600, cd) + 0.5 * smoothstep(-3500, -4500, z) + 0.6 * smoothstep(-2900, -4300, x));
    const double amp = 120 + 200 * outer;
    return floor + grow * (std::max(hills, -8.0) + amp * (0.55 * big * big + 0.45 * crest * crest * (0.4 + 0.6 * big)) + 70 * outer * crest);
}

double frontierCountry(const MapRegion& r, double x, double z) {
    const double cd = r.coreOutside(x - r.mx, z - r.mz);
    const double grow = smoothstep(40, 600, cd);
    const double plain = 5 + 3 * (fbmNoise(x * 0.004 + 2.2, z * 0.004 + 7.7, 3) - 0.5);
    // Badlands: mesas in tiers (flat tops, cliffs round them), slickrock domes, sandy washes.
    const double m = fbmNoise(x * 0.0042 + 9.0, z * 0.0042 - 2.0, 3) + 0.03 * (valueNoise(x * 0.03, z * 0.03) - 0.5);
    const double mesas = 16 * smoothstep(0.55, 0.565, m) + 13 * smoothstep(0.635, 0.65, m) + 9 * smoothstep(0.71, 0.725, m);
    const double domes = 8 * smoothstep(0.55, 0.85, valueNoise(x * 0.012 + 4.4, z * 0.012 - 1.9));
    const double bad = plain + mesas + domes + 6 * (fbmNoise(x * 0.01 - 3.3, z * 0.01 + 6.1, 2) - 0.5) - 3.5 * regionWash(x, z);
    // The cold high bench in the east, rising to peaks in the corner, cut by the canyon.
    const double rise = smoothstep(2400, 4400, x);
    double bench = 40 + 95 * rise + 30 * (fbmNoise(x * 0.003 - 5.0, z * 0.003 + 1.0, 3) - 0.5) +
                   90 * sq(ridgedNoise(x * 0.0026 + 2.0, z * 0.0026 - 7.0, 3)) * smoothstep(3100, 4400, x);
    const double d = std::fabs(z - canyonZ(x)), fy = canyonFloor(x);
    if (bench > fy) bench = fy + (bench - fy) * (0.12 * smoothstep(0, 30, d) + 0.88 * smoothstep(30, 115, d));
    const double east = smoothstep(2150, 3000, x);
    return plain + grow * ((bad - plain) * (1 - east) + (bench - plain) * east);
}

double openCountry(const MapRegion& r, double x, double z) {
    const double cd = r.coreOutside(x - r.mx, z - r.mz);
    const double grow = smoothstep(40, 500, cd);
    double h = 26 * (fbmNoise(x * 0.0032 + 1.1, z * 0.0032 - 4.4, 3) - 0.45) + 8 * (fbmNoise(x * 0.011 - 3.0, z * 0.011 + 2.0, 2) - 0.5);
    h += openWoods(x, z) * 34 * (fbmNoise(x * 0.006 + 8.0, z * 0.006, 3) - 0.35);  // wooded hills
    const double rim = fbmNoise(x * 0.0065 + 11.0, z * 0.0065 - 4.0, 3) + 0.012 * (valueNoise(x * 0.05, z * 0.05) - 0.5);
    h += openCanyon(x, z) * (17 * smoothstep(0.545, 0.556, rim) + 12 * smoothstep(0.62, 0.632, rim));  // red-rock mesas
    h -= 2.5 * regionWash(x, z);
    return 10 + grow * h;
}
}  // namespace

double regionWash(double x, double z) {
    // (Lines where a noise crosses its middle: winding, branching beds, a few hundred metres apart.)
    const double v = fbmNoise(x * 0.0032 + 13.0, z * 0.0032 - 6.0, 2);
    return smoothstep(0.025, 0.0, std::fabs(v - 0.5));
}

double regionLand(const MapRegion& r, double x, double z) {
    if (r.map == MAP_MOUNTAIN) return mountainCountry(r, x, z);
    if (r.map == MAP_FRONTIER) return frontierCountry(r, x, z);
    return openCountry(r, x, z);
}

double blendRegionLand(double x, double z, double backcountry) {
    double w[3];
    regionWeights(x, z, w);
    const double W = w[0] + w[1] + w[2];
    if (W <= 0) return backcountry;
    double h = backcountry * (1 - W);
    const std::vector<MapRegion>& rs = mapRegions();
    for (int k = 0; k < 3; k++)
        if (w[k] > 0) h += w[k] * regionLand(rs[k], x, z);
    return h;
}

bool regionGround(const MapRegion& r, double x, double z, double h, double slope, uint8_t& paint, uint8_t& surface) {
    // (Cheap noises only: this runs for every cell of the grown country.)
    const double nz = valueNoise(x * 0.04, z * 0.04), n1 = valueNoise(x * 0.011 + 3.0, z * 0.011 - 2.0);
    auto set = [&](uint8_t p, uint8_t s) { paint = p, surface = s; return true; };
    if (r.map == MAP_MOUNTAIN) {
        if (h > 212 + 16 * (n1 - 0.5) && slope < 0.8) return set(PAINT_NONE, SURF_SNOW);  // (the shader's snow, from 205 m)
        if (slope > 0.85 || (h > 175 && slope > 0.5)) return set(PAINT_NONE, SURF_ROCK);
        if (h > 110 && slope > 0.45 && nz > 0.55) return set(PAINT_SCREE, SURF_SCREE);
        if (slope < 0.3 && regionWash(x, z) > 0.6) return set(PAINT_GRAVEL, SURF_GRAVEL);  // (stony creek beds)
        return false;
    }
    if (r.map == MAP_FRONTIER) {
        const double cold = frontierCold(x, z);
        if (cold > 0.4 + 0.2 * nz) {
            if (std::fabs(z - canyonZ(x)) < 2.2 + 1.2 * nz && slope < 0.3) return set(PAINT_ICE, SURF_ICE);  // the frozen creek
            if (h > 135 + 20 * (n1 - 0.5) && slope < 0.8) return set(PAINT_SNOW, SURF_SNOW);
            if (slope > 0.8) return set(PAINT_NONE, SURF_ROCK);
            if (slope > 0.45 && nz > 0.6) return set(PAINT_SCREE, SURF_SCREE);
            return false;
        }
        if (slope > 0.75) return set(PAINT_REDROCK, SURF_ROCK);
        const double dome = smoothstep(0.55, 0.85, valueNoise(x * 0.012 + 4.4, z * 0.012 - 1.9));
        if (dome > 0.35 && slope > 0.08) return set(PAINT_ROCK, SURF_ROCK);  // slickrock
        if (regionWash(x, z) > 0.45) return set(PAINT_SAND, SURF_SAND);
        if (n1 < 0.33 && slope < 0.25) return false;  // (patches of dry grass)
        return set(PAINT_DESERT, SURF_DIRT);
    }
    // The Open Country.
    const double canyon = openCanyon(x, z);
    if (canyon > 0.4 + 0.2 * nz) {
        if (slope > 0.22) return set(PAINT_REDROCK, SURF_ROCK);
        if (n1 > 0.6) return set(PAINT_DESERT, SURF_DIRT);
    }
    if (slope > 0.9) return set(PAINT_NONE, SURF_ROCK);
    if (slope < 0.25 && regionWash(x, z) > 0.6) return set(PAINT_GRAVEL, SURF_GRAVEL);
    return false;
}
