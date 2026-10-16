// Levels: a map, what's been changed on it, and what you have to do there.
//
// A level is a plain text file (.czl, see saveLevelText for the format). It names one of the
// built-in maps, then the edits on top: terrain heights, surfaces, water, objects added and
// objects taken away. Then the job, if there is one:
//   FREE    nothing to do: a world to drive (what the editor makes by default)
//   RACE    laps of a circuit: through every gate in order, the last gate is the line
//   SPRINT  from the start through the gates to the last one (rally stages, hill climbs)
// with medal times, an optional time limit, a damage limit and whether R repairs are allowed.
//
// The world editor writes these; the game ships some (builtinLevels) and loads the player's
// own from the levels folders (levelFolders). In multiplayer the host sends the level's text
// to everyone, so a level made on one machine plays on all of them.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "world.h"

enum LevelMode { LEVEL_FREE, LEVEL_RACE, LEVEL_SPRINT, LEVEL_MODE_COUNT };
const char* levelModeName(int mode);

// A gate to drive through: a line `width` wide across (x, z), crossed heading `yaw`
// (radians, 0 = +x, the car convention: forward (cos, -sin)).
struct Gate {
    V3 p;
    double yaw = 0, width = 14;
};

// Something placed in the world.
enum LevelObjectKind { OBJ_ROCK, OBJ_BOX, OBJ_LOG, OBJ_TREE, OBJ_STATION, OBJ_KIND_COUNT };
struct LevelObject {
    int kind = OBJ_ROCK;
    V3 p;              // position (rock, tree, station: on the ground at x, z; box: its center)
    V3 size;           // rock: half-sizes (along, up, across); box: half extents; log: (r, 0, 0)
    V3 p2;             // log: the other end
    double yaw = 0, pitch = 0, roll = 0;  // radians
    double top = 0;    // rock: how much shows above the ground
    uint32_t seed = 1; // rock: its shape
    int sub = 0;       // tree: 0 pine, 1 broadleaf, 2 dead; box: 1 = rock slab
    float col[3] = {0.6f, 0.6f, 0.6f};
    std::string name;  // station
};

// An obstacle section authored in the editor (sections.h, sectionfile.h): a strip of ground with an
// entry (the course start) and an exit (the last gate), made on a cleared canvas and saved with what
// it tests and what kit it wants, for the stitcher to place anywhere later. The file keeps these in
// "#section" comment lines (the loader of an older build skips comments), so a section opens as an
// ordinary level anywhere.
enum SectionKind { SK_LEDGES, SK_CLIMB, SK_BOULDERS, SK_BOG, SK_CROSSING, SK_SHELF, SK_TWISTER, SK_TIMBER, SK_DUNES, SK_DROP, SK_GAP, SK_CUSTOM, SK_COUNT };
enum SectionTest { ST_SKILL = 1, ST_PROBLEM = 2, ST_COMMIT = 4, ST_ATTRITION = 8, ST_COOP = 16, ST_COUNT = 5 };  // bits of `tests`
enum SectionKit { SKIT_4L = 1, SKIT_LOCKERS = 2, SKIT_AIRDOWN = 4, SKIT_WINCH = 8, SKIT_BUDDY = 16, SKIT_COUNT = 5 };  // bits of `kit`
const char* sectionKindName(int kind);     // "LEDGES"
const char* sectionTestName(int bit);      // bit index 0..4: "PROBLEM-SOLVING"
const char* sectionKitName(int bit);       // bit index 0..4: "AIR DOWN"
// As the files write them: "SKILL,COMMIT" ("-" none), and back.
std::string sectionBitsText(int bits, bool kit);
int sectionBitsParse(const std::string& list, bool kit);

struct SectionInfo {
    bool on = false;
    int kind = SK_CUSTOM, difficulty = 3;   // difficulty 1..5
    int tests = 0, kit = 0;
    // The canvas: a rectangle of cleared ground, `length` m along `heading` from its back-centre
    // (x, z), `half` m either side. The entry (the start) and the exit (the last gate) are
    // inside it; the section's local frame is the entry's (u along the heading, v to the left).
    double x = 0, z = 0, heading = 0, length = 260, half = 45;
    int gen = -1;           // the generator it was stamped from (SectionType), if any
    uint32_t genSeed = 0;
    // How far (wx, wz) is outside the canvas (negative inside).
    double outside(double wx, double wz) const {
        const double dx = wx - x, dz = wz - z, u = dx * std::cos(heading) - dz * std::sin(heading), v = dx * std::sin(heading) + dz * std::cos(heading);
        return std::max(std::max(-u, u - length), std::fabs(v) - half);
    }
};

struct Level {
    std::string name = "UNTITLED", about, author, file;
    SectionInfo section;
    int map = MAP_FRONTIER;
    int mode = LEVEL_FREE;
    int laps = 1;
    int vehicle = -1;         // the vehicle it's for (-1: your choice)
    double timeOfDay = -1;    // hours (-1: whatever the clock says)
    bool resets = true;       // R repairs allowed mid-run
    double maxDamage = 0;     // % of the vehicle wrecked before you're out (0: no limit)
    double timeLimit = 0;     // s (0: none)
    double medals[3] = {0, 0, 0};  // gold, silver, bronze times (s; 0: none)
    bool hasStart = false;
    V3 start;
    double startYaw = 0;
    std::vector<Gate> gates;
    // Edits, by grid cell (World::cellIndex): new height, surface + paint, water level.
    std::map<int, float> heights;
    std::map<int, uint16_t> surfaces;  // surface << 8 | paint
    std::map<int, float> water;
    std::map<int, std::pair<float, float>> flow;  // current in the water there (world x, z m/s): a stamped river keeps its own ("#flow" comment lines)
    std::vector<LevelObject> objects;
    std::vector<V3> removed;           // base-map objects taken out: (x, 0, z), each the nearest within 1.5 m

    bool builtin = false;
    bool hasJob() const { return mode != LEVEL_FREE && !gates.empty(); }
};

// Puts a level's edits on a freshly built World(level.map), then rebuilds its lookups.
void applyLevel(const Level& lv, World& w);
// Adds one object to the world (as applyLevel does).
void placeObject(const LevelObject& o, World& w);
// Posts and a banner at each gate (to see, not to hit), for playing the level.
void addGateMarkers(const Level& lv, World& w);

std::string saveLevelText(const Level& lv);
bool parseLevelText(const std::string& text, Level& lv, std::string* error = nullptr);
bool loadLevelFile(const std::string& path, Level& lv, std::string* error = nullptr);
bool saveLevelFile(const Level& lv, const std::string& path, std::string* error = nullptr);

// Where players' levels live: the user's (~/.config/crumplezone/levels) first, then the
// game's own folder (./levels). New levels are saved to the first.
std::vector<std::string> levelFolders();
std::string userLevelFolder();
// The files in a folder ending in `ext` (".czl"), sorted, as "dir/name.czl".
std::vector<std::string> listLevelFiles(const std::string& dir, const std::string& ext);
// The game's levels, then every .czl in the folders.
std::vector<Level> listLevels();
const std::vector<Level>& builtinLevels();
