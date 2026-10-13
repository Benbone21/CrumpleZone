// Jobs to do together on a map:
//   RECOVER  a truck broke down somewhere rough (engine out of fuel, a flat, a snapped
//            shaft): get it to a repair shop: tow it, winch it, push it.
//   CONVOY   everyone's trucks to a far-off place, together (all within 30 m of it).
//   SCOUT    visit a handful of places (any truck within 20 m of each).
// The host picks one; its description goes out as text, so everyone tracks the same job.
#pragma once
#include <string>
#include <vector>

#include "softbody.h"

enum JobKind { JOB_RECOVER, JOB_CONVOY, JOB_SCOUT, JOB_KIND_COUNT };

struct Job {
    int kind = JOB_RECOVER;
    std::string title, about;
    V3 target;                   // RECOVER: where the wreck is; CONVOY: the meeting place
    std::string targetName;
    std::vector<V3> spots;       // SCOUT: places to visit
    std::vector<std::string> spotNames;
    std::vector<bool> visited;
    int vehicle = 2;             // RECOVER: which truck broke down (catalog index)
    uint32_t wreckId = 0;        // RECOVER: its network id (multiplayer) ...
    int wreck = -1;              // ... or its index in Sim::cars here
    bool done = false;
    double time = 0;             // s since it started

    std::string text() const;    // for the network
    static bool parse(const std::string& s, Job& out);
};

struct JobPlace {
    std::string name;
    V3 p;
};

// A few jobs that make sense on this map (from its places), different each `seed`.
std::vector<Job> makeJobs(const World& w, const std::vector<JobPlace>& places, uint32_t seed);

// The broken-down truck for a RECOVER job, added to the sim (host or offline): parked at the
// target, out of fuel, one tire flat, a half-shaft snapped. Returns its index.
int spawnWreck(Sim& sim, Job& job);

// Checks progress (every frame). `players`: everyone's truck positions (yours and the
// others'). True the moment it's done.
bool updateJob(Job& job, const Sim& sim, const std::vector<V3>& players, double dt);
// What's next, for the HUD ("TOW THE BRONCO TO A SHOP: 340 M").
std::string jobStatus(const Job& job, const Sim& sim, const V3& me);
// Where to point the player next (a marker).
bool jobMarker(const Job& job, const Sim& sim, const V3& me, V3& at, std::string& label);
