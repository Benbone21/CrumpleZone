// Wrecks: rusting vehicles left out in the country, off the trails as much as on them: in the scrub, down gullies, among trees, at the
// foot of a cliff, half buried or rolled. No sign, no name, no marker: you come across one and search it on foot (hold E, as a supply
// cache). About half hold something: a fuel can, a part, a tool, recovery gear, some cash. The rest still take the hold and say so.
//
// A wreck is a place (places.h PK_WRECK: the procedural world's from ProcWorld::nodeWrecks, the Backcountry's and the others' from the
// scanned tables); everything about how it looks and what's in it comes from a hash of its id, so it's the same on every machine.
//
// Career: searched wrecks are kept with the world, like caches, and what's in one is yours. Sandbox has everything already: the search
// says what you found and gives nothing, and searched wrecks stay searched for the session. Either way a search is told to the others
// ("wreck MAP ID"): chatter, no protocol change; an older build ignores it. In a career, as with a cache, it counts for everyone there.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "career.h"
#include "careersession.h"
#include "mathx.h"
#include "places.h"

class World;
class Sim;

namespace wrecks {

constexpr int kLootPercent = 50;   // how many hold anything
constexpr double kReach = 4.6;     // m from your chest to a wreck's middle (it's a big thing: you search it from any side)

// How one lies: which model and design, and how it's tipped and sunk.
struct Look {
    const char* model = "";
    int variant = 0;
    V3 center, half;                    // its collider, in the model's frame (front, up, left)
    double pitch = 0, roll = 0, sink = 0;  // nose up, rolled about its length (radians); m below the ground
};
uint32_t hashOf(const std::string& id, uint64_t seed);
Look lookOf(uint32_t hash);
int modelCount();                       // how many different models there are (tests)
const char* modelAt(int i);
int variantAt(int i);

// Where one stands in `w`: its frame (front, up, left) and origin, settled on the slope under it.
struct Pose {
    Look look;
    V3 at, ax[3];
    V3 middle() const { return at + ax[0] * look.center.x + ax[1] * look.center.y + ax[2] * look.center.z; }
};
Pose poseOf(const World& w, const MapPlace& pl);
// Puts it in the world: the model and its (hidden) collider.
void place(World& w, const MapPlace& pl);
// All of a built map's (not the procedural world's: buildPlaces does those).
void placeAll(World& w);

// The wrecks on the map as things to search (the window's, on the procedural world). What's in each is settled by its id.
std::vector<CacheSpot> spots(const World& w);
CacheSpot spotOf(const World& w, const MapPlace& pl);

// The sandbox's searched wrecks, this session (keys: Career::cacheKey).
struct Searched {
    std::set<std::string> keys;
};
bool isSearched(const CareerSession& run, const Searched& done, const CacheSpot& c);
// Searches it: returns the banner ("FOUND: FUEL CAN, $120", "NOTHING USEFUL"), or "" if it had been searched. A career gets what's in it.
std::string search(CareerSession& run, const Sim& sim, Searched& done, const CacheSpot& c);

// What goes to the others, and reading it back: false if it's not one.
std::string message(int map, const std::string& id);
bool takeMessage(const std::string& text, int& map, std::string& id);

}  // namespace wrecks
