// Career worlds: the difficulty and mode a world is created with, and the folder name it gets.
#pragma once
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
    bool damaged = false;
};

std::string worldsDir();  // careerConfigDir() + "/worlds"
bool saveWorld(const WorldSave& w);  // atomic; the previous good world.txt becomes world.txt.bak
bool loadWorld(const std::string& slug, WorldSave& w);  // falls back to the .bak; both bad: w.damaged, false
std::vector<WorldSave> listWorlds();  // every folder in worldsDir(), damaged ones too, most recently saved first
bool deleteWorld(const std::string& slug);
// A new world (not saved): the slug is free among `existing`, build is this release.
WorldSave newWorld(const std::string& name, int map, Difficulty d, WorldMode m, const std::vector<WorldSave>& existing);
