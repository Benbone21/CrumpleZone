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
    TOOL_WRENCH,      // spanners: tie rods, sway bar links
    TOOL_RATCHET,     // a socket set: CV axles, control arms, drive shafts, spark plugs
    TOOL_HAMMER,      // persuasion: CV axles out of hubs, tie-rod ends out of knuckles
    TOOL_PRY_BAR,     // control arms back into place
    TOOL_TIRE_IRONS,  // a tire off and on its rim
    TOOL_JACK,        // a corner off the ground
    TOOL_JUMP_PACK,   // a dead battery brought back by hand (battery.h)
    TOOL_CABLES,      // jumper cables between two vehicles' batteries (battery.h)
    TOOL_COUNT
};

enum RepairPart {
    PART_TIRE,         // a new tire (fitted to the rim on the truck)
    PART_CV_AXLE,      // a half shaft with its CV joints
    PART_DRIVE_SHAFT,  // a prop shaft
    PART_TIE_ROD,
    PART_CONTROL_ARM,  // a suspension link (or a sway bar link)
    PART_SPARK_PLUGS,  // a set, for a flooded engine
    PART_ENGINE_OIL,   // a jug of oil, to refill the sump after the water is drained
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

// A repair is done in order, a step at a time, each a hold of E with the right thing in your hand (interact.h):
// at a corner the jack goes under its frame pad and the corner comes up, the wheel comes off (a truck's: wheelLoss),
// then the two jobs on the part (REMOVE the broken one, it drops beside the car; FIT the new one: that is what fixes it), then the
// wheel goes back on and the jack comes down. A tie rod, a sway bar link, a drive shaft, the plugs: just the work.
struct RepairStep {
    enum Type { JACK, WHEEL_OFF, WORK, WHEEL_ON, LOWER, HOOD_UP, HOOD_DOWN } type = WORK;
    // What a work step does besides the tool and the part (the engine job): the animation, sound and effect follow.
    enum Act { ACT_NONE, ACT_CRANK, ACT_DRAIN, ACT_FILL } act = ACT_NONE;
    const char* label = "";  // the checklist's line: "OLD AXLE OUT"
    const char* verb = "";   // the prompt's: "KNOCK THE OLD AXLE OUT"
    int tool = -1;           // the Tool in your hand for it (-1: none)
    int part = -1;           // ...or the new RepairPart: it goes in (and is used up) here
    double hold = 4;         // s of work
    bool fixes = false;      // the step that makes the part whole again
    bool removes = false;    // the broken part comes off here: dropped on the ground beside the car
    int end = 0;             // where: 0 at the fix, 1 / 2 the front / back end of a shaft
    bool ground = false;     // done on the wheel that came off, lying on the ground (the tire)
    bool under = false;      // done from underneath (the engine's drain plug): the work is at the sump, not the top of the engine
};
const char* jobName(int kind);  // "CV AXLE", "SWAY BAR LINK"
// Whether a job needs the corner up / the wheel off (the wheel only comes off a truck that loses wheels).
bool jobJacks(int kind, int wheel);
bool jobWheelOff(const Car& c, int kind, int wheel);
std::vector<RepairStep> repairSteps(int kind, bool jack, bool wheelOff);
// A new tire on a wheel off the truck (WheelItem::tireWork counts them): old tire off, new one on, bead seated and aired.
std::vector<RepairStep> tireSteps();
// The engine job's sump (the drain plug): under the engine, on the ground side (world).
V3 enginePlugAt(const Car& c);
// What step `step` of a flooded-engine job (Fix::SPARK_PLUGS) is; ACT_NONE for any other job or a step out of range.
RepairStep::Act engineStepAct(int fixKind, int step);

// How the corner is now, for the steps the jack and the wheel are.
struct CornerNow {
    bool lifted = false;    // off the ground (Interact::cornerLifted)
    bool jackUp = false;    // a jack under it, not let down
    bool wheelOn = true;    // a wheel bolted to the hub
    bool wheelGood = true;  // ...with its tire up (not flat)
    bool hoodOpen = false;  // the hood up (Car::hoodOpen)
    bool hoodShut = true;   // ...and shut again (Car::hoodShut)
};
// Where a job is: each step done or not, and the next one (-1: all done). `done`: its work steps done.
struct RepairPlan {
    std::vector<RepairStep> steps;
    std::vector<bool> done;
    int next = -1;
};
RepairPlan planRepair(int kind, bool jack, bool wheelOff, int done, bool fixed, const CornerNow& now);
// The job under way for this fix (an index into c.fieldJobs, -1: none yet).
int fieldJobOf(const Car& c, const Fix& f);
// What's missing in your hand for a step: "" if you have it. `own`: everything you own, when the kit rides in
// the truck's back (trunk.h: it has to be in your hand); null: the kit's on you (the old way). `handTool`,
// `handPart`: what you hold (-1 none).
std::string stepMissing(const RepairStep& st, const Kit& kit, const Kit* own, int handTool, int handPart);
