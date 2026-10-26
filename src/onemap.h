// One map (docs/plans/one-map.md): the Backcountry is the middle of the big map, and three of the old maps are regions round it,
// each its old 1.2 km map moved over whole, sideways only (heights, yaws and everything else unchanged).
//
//   Open Country (MAP_WILDS, meadow)  west        its middle at (-3496, 0)
//   Mountain (MAP_MOUNTAIN)           south-west  (-1500, -3496)
//   Frontier (MAP_FRONTIER)           south-east  (1500, -3496)
//
// A point p on an old map is at p + (mx, 0, mz) on the big one (regionToWorld). The map ids stay as they were: MAP_BACKCOUNTRY is the
// big map, and the old ids name its regions (a start point in the menu, a level's map, a cache's or a discovery's map).
#pragma once
#include <vector>

#include "mathx.h"

class World;

constexpr double kOneMapHalf = 4096.0;   // the big map's half-size (m): 8192 m square
constexpr double kRegionHalf = 600.0;    // an old map's half-size
constexpr double kOldBackcountryHalf = 3072.0;  // the 6 km Backcountry's, whose land and places are the big map's middle
constexpr double kSeamFrom = 2650.0;     // the Backcountry's own land is left alone within this far of the middle (its places reach 2567 m)

struct MapRegion {
    int map;            // the old map's id (MapId)
    const char* name;   // as the M map and the menu show it
    double mx, mz;      // where its middle (0, 0) is on the big map
    // Which of its sides faces the Backcountry (in its own coordinates: 0 +x, 1 -x, 2 +z, 3 -z), and how far from its middle toward
    // that side its own land is used (past it, its old rim rose: the land there is the seam's blend instead).
    int seamSide;
    double trust;
    double sideTrust;   // the same across its two sides beside the seam (its far side is the big map's edge: its rim stays there)
    // Its square on the big map, and the core whose land, growth and everything else is the old map's own.
    double x0() const { return mx - kRegionHalf; }
    double x1() const { return mx + kRegionHalf; }
    double z0() const { return mz - kRegionHalf; }
    double z1() const { return mz + kRegionHalf; }
    void core(double& lx0, double& lx1, double& lz0, double& lz1) const;  // in its own coordinates
    bool inCore(double lx, double lz) const;                              // (its own coordinates)
    double coreOutside(double lx, double lz) const;                       // how far outside the core (0 inside)
    bool seamward(double lx, double lz) const;                            // past the core on the side facing the Backcountry
    double seamFade() const;    // over how far past the core's seam edge the land eases into the Backcountry's (m)
    static constexpr double kSideFade = 300;  // ...and past its sides, into the edge mountains round it
    // Over how far the land eases from the core into what's round it, at a point `out` past the core: the seam's length straight out
    // from the seam edge, the sides' beside it, and between the two round the corners (no step where one meets the other).
    double fadeAt(double lx, double lz, double out) const;
};

// The big map's regions (in a fixed order: Open Country, Mountain, Frontier).
const std::vector<MapRegion>& mapRegions();
const MapRegion* regionOf(int map);          // the region an old map became (nullptr: not one)
inline bool isRegionMap(int map) { return regionOf(map) != nullptr; }
int combinedMapFor(int map);                  // the map an old map's places are on now: MAP_BACKCOUNTRY for a region, else itself
V3 regionToWorld(int map, const V3& p);       // a point on an old map, on the big one (unchanged for a map that isn't a region)
V3 worldToRegion(int map, const V3& p);       // and back
const MapRegion* regionContaining(double x, double z);  // whose square (x, z) is in (nullptr: none)

// How open the Backcountry's edge ridge is at (x, z) on the big map (0 the ridge, 1 none): open round each region, so the seam is a
// broad valley and the region's other sides meet low hills, not a wall.
double edgeRidgeOpening(double x, double z);
// How far up the big map's own edge mountains (0..1): the last 330 m before its edge, all round.
double bigMapEdgeRidge(double x, double z);

// The world a pick of the map list is played on: a region's is the big map, started at that region (World::startAtRegion).
World worldForPick(int pick);
