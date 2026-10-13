// What the autopilot sees: the solid things around the vehicle and the height of whatever
// its tires and underbody would meet, and how hard a ledge each drive setup can climb.
//
// The scan is exact geometry, not a guess: a vertical line through (x, z) cut against
// each boulder's hull, each box, log and parked vehicle, over the terrain. Results are
// kept in a fine grid around the vehicle for the frame, since the candidate lines it
// tries overlap a lot.
#pragma once
#include <cstdint>
#include <vector>

#include "mathx.h"

class World;
class Car;
struct Rock;
struct Box;
struct Log;
struct VehicleSpec;

enum SeenKind : uint8_t { SEEN_ROCK, SEEN_LEDGE, SEEN_LOG, SEEN_TREE, SEEN_VEHICLE, SEEN_WATER, SEEN_WALL };
const char* seenName(int kind);

// Top of a solid at (x, z) along a vertical line, or -1e9 if the line misses it.
double rockTop(const Rock& r, double x, double z);
double boxTop(const Box& b, double x, double z);
double logTop(const Log& l, double x, double z);

// One solid thing near the vehicle.
struct Thing {
    uint8_t kind;
    int index;       // into the world's rocks / boxes / logs / poles, or the vehicle index
    double x, z, r;  // where it is and how far it reaches (xz)
    double height;   // how far its top stands above the ground under it
    double fx = 0, fz = 0, hl = 0, hw = 0;  // vehicles: heading and half length / width
};

class Scene {
public:
    // Collects what's within `radius` of `around`: the world's solids and other
    // vehicles (skipping `self`). Clears the height cache.
    void gather(const World& w, const std::vector<Car>* cars, const Car* self, const V3& around, double radius);
    // Height of the top of whatever is solid at (x, z) (the terrain if nothing): `what`
    // gets the index in things(), or -1 for bare ground. Deep water and trees count as
    // very tall.
    double top(double x, double z, int* what = nullptr);
    double ground(double x, double z) const;
    // How well tires grip the ground there (share of dry pavement).
    double grip(double x, double z) const;
    const std::vector<Thing>& things() const { return list; }

private:
    const World* w = nullptr;
    const std::vector<Car>* cars = nullptr;
    std::vector<Thing> list;
    // Cache: 0.125 m cells over a square around the gather point.
    static constexpr double kRes = 0.125;
    static constexpr int kN = 384;
    V3 origin;
    std::vector<float> h;
    std::vector<int16_t> id;
    std::vector<uint8_t> have;
    // Things binned by where they reach (4 m buckets over the same square).
    static constexpr double kBin = 4.0;
    static constexpr int kBins = 12;
    std::vector<int> bins[kBins * kBins];
    std::vector<int> farList;  // everything (for points outside the square)
    double compute(double x, double z, int* what) const;
};

// Drive setups, easiest to hardest: 2WD, 4WD (open center), 4WD with the center locked,
// low range. Not every truck has every one.
enum DriveLevel { LEVEL_2WD, LEVEL_4WD, LEVEL_4WD_LOCKED, LEVEL_LOW, LEVEL_COUNT };
const char* levelName(const Car& c, int level);
int currentLevel(const Car& c);
int bestLevel(const VehicleSpec& vs);
// The transfer-case position for a level (-1 if this truck has none).
int modeForLevel(const VehicleSpec& vs, int level);
// Tallest square ledge (m) it climbs in that setup, from measured runs (Bronco in 4L:
// ~one tire radius; 2WD: about a third of one). Lockers and aired-down tires add a little.
double stepCap(const VehicleSpec& vs, int level, int lockers, double psi);
