// Career worlds: the difficulty and mode a world is created with, and the folder name it gets.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

enum Difficulty { DIFF_LIGHT, DIFF_MEDIUM, DIFF_HARDCORE, DIFF_COUNT };
enum WorldMode { MODE_PERSONAL, MODE_CREW };

const char* difficultyName(Difficulty d);
const char* modeName(WorldMode m);

// The cash a new world starts with.
int startCash(Difficulty d);

// Whether a vehicle (an index into vehicleCatalog()) can be a career starter. The Jesko can't.
bool startableVehicle(int catalogIndex);

// A folder-safe name for a world: lowercase letters and digits joined by '-', at most 32
// characters, "world" if nothing is left; "-2", "-3"... appended until it isn't in `taken`.
std::string worldSlug(const std::string& name, const std::vector<std::string>& taken);

// Where the profile (profile.txt) and the old career.txt live: configDir(), unless this thread has a ScopedCareerDir
// (the tests run as threads in one process, so each gives itself a folder instead of an environment variable).
std::string careerConfigDir();
struct ScopedCareerDir {
    explicit ScopedCareerDir(std::string dir);
    ~ScopedCareerDir();
    ScopedCareerDir(const ScopedCareerDir&) = delete;
    ScopedCareerDir& operator=(const ScopedCareerDir&) = delete;
private:
    std::string prev_;
};

// A car a player left behind in a world (owner = player name; condition = the car's saved damage text, kept as is).
struct LeftVehicle {
    std::string owner;
    int vehicle = 0;
    std::string condition;
};

// One world's save (worlds/<slug>/world.txt). `damaged` is set when neither world.txt nor its .bak could be read
// (only name = slug is then known).
struct WorldSave {
    std::string name, slug;
    int map = 0;
    Difficulty diff = DIFF_MEDIUM;
    WorldMode mode = MODE_PERSONAL;
    std::string build;
    std::vector<LeftVehicle> left;
    std::set<std::string> opened;
    double playedSeconds = 0;
    // A custom world (MAP_PROCEDURAL): the seed its land is made from (0 on a built map), and the window of it the cars in this save
    // stand in (World::ProcInfo ox/oz when it was saved: the positions in the cars' saves are round it). originSet false: the seed's start.
    uint64_t seed = 0;
    bool originSet = false;
    int64_t ox = 0, oz = 0;
    int lastVehicle = -1;    // the catalog index you were in when it was last saved (-1: not known)
    long long savedAt = 0;   // listWorlds(): world.txt's time (seconds since 1970; 0 not known). Not in the file.
    bool damaged = false;
};

std::string worldsDir();  // careerConfigDir() + "/worlds"
bool saveWorld(const WorldSave& w);  // atomic; the previous good world.txt becomes world.txt.bak
bool loadWorld(const std::string& slug, WorldSave& w);  // falls back to the .bak; both bad: w.damaged, false
std::vector<WorldSave> listWorlds();  // every folder in worldsDir(), damaged ones too, most recently saved first
bool deleteWorld(const std::string& slug);
// A new world (not saved): the slug is free among `existing`, build is this release. On MAP_PROCEDURAL it gets `seed`, or with 0 a
// random one that none of `existing` has (two new custom worlds are never the same land unless the player types the same seed).
WorldSave newWorld(const std::string& name, int map, Difficulty d, WorldMode m, const std::vector<WorldSave>& existing, uint64_t seed = 0);
// A random seed for a new custom world that none of `existing` has (never 0).
uint64_t freshWorldSeed(const std::vector<WorldSave>& existing);
// What a typed seed means: digits are the number itself, anything else is hashed (so "MY CANYON" is a seed too). 0 for blank.
uint64_t parseSeed(const std::string& text);
