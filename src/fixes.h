// Hands-on trail repairs: on foot, at the broken corner (or under the hood), with your own
// tools and a spare part, holding E. No more crews: what you can fix out here depends on
// what you've found, won or bought: a lug wrench and a jack to change a wheel, a socket set
// and a hammer to swap a CV axle, and so on (career.h keeps them; sandbox has it all).
#pragma once
#include <string>
#include <vector>

#include "softbody.h"

enum Tool {
    TOOL_LUG_WRENCH,  // wheels off and on
    TOOL_WRENCH,      // spanners: tie rods, drive shafts, sway bar links
    TOOL_RATCHET,     // a socket set: CV axles, control arms, spark plugs
    TOOL_HAMMER,      // persuasion: CV axles out of hubs, tie-rod ends out of knuckles
    TOOL_PRY_BAR,     // control arms back into place
    TOOL_TIRE_IRONS,  // a tire off and on its rim
    TOOL_JACK,        // a corner off the ground
    TOOL_COUNT
};

enum RepairPart {
    PART_TIRE,         // a new tire (fitted to the rim on the truck)
    PART_CV_AXLE,      // a half shaft with its CV joints
    PART_DRIVE_SHAFT,  // a prop shaft
    PART_TIE_ROD,
    PART_CONTROL_ARM,  // a suspension link (or a sway bar link)
    PART_SPARK_PLUGS,  // a set, for a flooded engine
    PART_COUNT
};

const char* toolName(Tool t);           // "LUG WRENCH"
const char* toolModel(Tool t);          // its model (modelkit.h): "lug_wrench"
const char* repairPartName(RepairPart p);   // "CV AXLE"
const char* repairPartModel(RepairPart p);  // "cv_axle"
// Career keys (career.h owned / stock): "TOOL:LUG WRENCH", "REPAIR:CV AXLE".
std::string toolKey(Tool t);
std::string repairPartKey(RepairPart p);

// What you have to work with.
struct Kit {
    bool unlimited = true;           // sandbox: every tool, endless parts
    bool tools[TOOL_COUNT] = {};
    int parts[PART_COUNT] = {};
    // Yours, but in a truck that's shut or too far off (trunk.h): what the job says is missing.
    bool stowedTools[TOOL_COUNT] = {};
    bool stowedParts[PART_COUNT] = {};
    bool has(Tool t) const { return unlimited || tools[t]; }
    bool hasPart(RepairPart p) const { return unlimited || parts[p] > 0; }
};

// A repair a car needs, and what it takes.
struct Fix {
    enum Kind { CV_AXLE, DRIVE_SHAFT, TIE_ROD, CONTROL_ARM, SWAY_BAR, NEW_TIRE, SPARK_PLUGS } kind;
    int wheel = -1;    // the corner (-1: none: the engine, a prop shaft)
    int beam = -1;     // the broken or bent beam it replaces (-1: none)
    int axle = -1;     // (sway bars, prop shafts)
    RepairPart part;
    std::vector<Tool> tools;
    bool lifted = false;  // needs the corner jacked up
    double hold = 4;      // s of work
    V3 at;                // where you do it (world)
    std::string what;     // "REPLACE THE CV AXLE"
};

// A beam that has taken a permanent set (it was bent, not broken).
bool beamBent(const Beam& b);
// Every repair the car needs that can be done out here (the corner's, the engine's, the
// shafts'). Body damage is a station's job.
std::vector<Fix> fixesFor(const Car& c);
// What's missing to do it: "" if you can, else e.g. "NEEDS A SOCKET SET AND A HAMMER" or
// "NO CV AXLE: FIND OR BUY ONE".
std::string missingFor(const Fix& f, const Kit& kit, bool cornerLifted);
// Does it (and uses the part). False if it no longer applies.
bool applyFix(Car& c, const Fix& f);
