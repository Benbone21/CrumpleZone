// Rivers by depth class and the waterfalls on them (docs/plans/big-maps.md 3.8, 3.9).
//
// A river is carved into the height grid by its map's builder (backcountry.cpp: Elk River), which writes the water level and the
// current into World::waterY / flowX / flowZ, the way the cars feel it (softbody.cpp samples it at every node). What's kept
// besides is the river as laid out, a spot every few metres (World::rivers: its class, depth, width, current and bed), and its
// falls (World::falls: drawn from the "waterfall" model by the water pass, heard by worldsound.cpp). Both go into the world cache.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mathx.h"

// What a stretch of river is to someone driving into it.
enum RiverClass : uint8_t {
    RIVER_RUN,     // 0.6-0.9 m: the ordinary channel (wade it with a snorkel, or a tall truck slowly)
    RIVER_RIFFLE,  // 0.2-0.4 m: fast and shallow over stones, crossed at walking pace
    RIVER_FORD,    // 0.4-0.6 m: wide, slow and gravel-bedded, with marker posts: where a trail crosses
    RIVER_POOL,    // 0.9-1.5 m: slow and deep: a snorkel, or the engine drowns
    RIVER_DEEP,    // over 1.5 m (Elk Valley's old channel): a car floats
    RIVER_CHUTE,   // 0.4-0.8 m running 2-3 m/s over rock, above a falls
    RIVER_PLUNGE,  // 2-3 m under a falls: a car floats off downstream
    RIVER_FALLS,   // the drop itself (no water on the grid: the model is the water)
    RIVER_CLASSES
};
const char* riverClassName(int cls);
struct DepthBand {
    double lo, hi;
};
DepthBand riverClassDepth(int cls);  // the depth a class is made at, mid-channel (m)
int riverClassForDepth(double depth);  // which of run, riffle, ford, pool, deep a channel this deep is
enum RiverBed : uint8_t { BED_GRAVEL, BED_ROCK, BED_MUD };

// A spot on a river: the middle of the channel at the water's level.
struct RiverSpot {
    V3 p;
    float depth = 0, halfWidth = 0, speed = 0;  // mid-channel depth, half the wet width (m), the current (m/s)
    uint8_t cls = RIVER_RUN, bed = BED_GRAVEL;
};
struct River {
    std::string name;
    std::vector<RiverSpot> spots;  // downstream, a few metres apart
};

// A waterfall. In the model's frame the lip is at (0, height, 0) and the water falls toward +X onto the pool at y = 0.
struct Falls {
    std::string name;
    V3 lip;             // the middle of the lip, at the level of the water above it
    double yaw = 0;     // the way the water goes over (the game's yaw: 0 = +X, front (cos, 0, -sin))
    double height = 0;  // the drop to the pool's surface (m)
    double width = 0;   // of the falling sheet
    double throwOut = 0;  // how far out from the lip it lands
    int variant = 0;    // the model's design: 0 SHEET, 1 BROKEN, 2 CASCADE
    uint32_t seed = 1;
    double poolR = 0, poolDepth = 0;  // the plunge pool under it
    V3 base() const { return V3(lip.x + std::cos(yaw) * throwOut, lip.y - height, lip.z - std::sin(yaw) * throwOut); }  // where it lands
    // How loud it is at its foot (0..1) and how far off it's heard (m): by its size.
    double loudness() const;
    double hearRange() const;
};

// ---- Elk River in the Backcountry's new country (x beyond +-540 m: Bear Creek Canyon east, the west end down to the edge) -----
// What its channel is like at x: the build carves it from this (backcountry.cpp).
struct RiverStretch {
    double depth = 0.8, widthScale = 1, speed = 1, bankSlope = 0.35;
    int cls = RIVER_RUN, bed = BED_GRAVEL;
};
RiverStretch elkRiverAt(double x);
struct FallsSite {
    const char* name;
    double x;         // the lip (the cliff runs from x - kFallsFace up to x, across the canyon)
    double drop;      // m
    double throwOut;  // how far out the sheet lands
    int variant;
    double poolLen, poolDepth;
};
constexpr double kFallsFace = 2.5;  // (the cliff's run, m: the valley floor steps up over this)
const std::vector<FallsSite>& elkFalls();
double elkRiverZ(double x);  // its middle line: z at x (it flows west)
double elkLipX(const FallsSite& f, double z);  // where a falls' lip is, across the canyon (its cliff runs kFallsFace downstream of it)
double elkFallsRise(double x, double z);  // how much the valley floor has stepped up at (x, z), going upstream over the falls
int elkFallsFace(double x, double z);  // which falls' cliff face (x, z) is on (-1: none)
int elkFallsZone(double x, double z);  // how many falls' faces are at or below (x, z) (the reaches between the falls: 0 below them all)
const std::vector<double>& elkFords();  // where its fords are (x, mid-channel)  // how much the valley floor has stepped up by x, going upstream (east) over the falls
// The named spots (backcountryFeature): bearfalls, upperfalls (their lips), eastford, eastpool, westford, westpool, eastriffle.
bool elkRiverSpot(const std::string& name, double& x);
