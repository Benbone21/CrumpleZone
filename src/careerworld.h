// Career worlds: the difficulty and mode a world is created with, and the folder name it gets.
#pragma once
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
