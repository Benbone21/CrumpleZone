// Anchor gear: what a winch line can be tied to, and what it takes. A tree needs a strap round
// it, a boulder a chain sling (both come back when the line comes off); open ground takes a
// stake, bare rock a bolt drilled in, snow and ice a picket (all three stay where they're set).
// Everything you can tune is in namespace anchors: the rules, the counts, the prices.
#pragma once
#include <string>

#include "world.h"

enum AnchorGear { GEAR_STRAP, GEAR_SLING, GEAR_STAKE, GEAR_BOLT, GEAR_ICE, GEAR_COUNT };

struct AnchorStock;

namespace anchors {
// ---- Where it works ----
constexpr double kTreeMinR = 0.15;      // m: thinner trunks and posts pull out
constexpr double kBoulderMinR = 0.45;   // m: smaller rocks just roll toward you
constexpr double kGroundMaxWater = 0.15; // m of water over the ground: deeper and nothing holds (the mud pit, the shallows)
constexpr double kSetReuse = 1.2;       // m: a pull from within this of a stake you left is on that stake

// ---- What you carry: TREE STRAP, CHAIN SLING, GROUND STAKE, ROCK BOLT, ICE ANCHOR ----
constexpr int kCareerStart[GEAR_COUNT] = {2, 1, 2, 0, 0};
constexpr int kSandboxStart[GEAR_COUNT] = {3, 2, 4, 2, 2};  // (and what a sandbox repair station or a respawn gives back)
constexpr int kPrice[GEAR_COUNT] = {80, 120, 40, 150, 90};  // career: the station rack

inline bool usedUp(AnchorGear g) { return g >= GEAR_STAKE; }  // stakes, bolts and pickets stay in the ground
const char* gearName(AnchorGear g);   // "TREE STRAP"
std::string gearKey(AnchorGear g);    // career stock key: "ANCHOR:TREE STRAP"
const char* gearModel(AnchorGear g);  // modelkit id: "tree_saver"
// Plural for the HUD line: "STRAPS", "SLING", "STAKES", "BOLTS", "ICE".
const char* gearShort(AnchorGear g);

// What open ground takes. `water`: how deep the water over it is (<= 0 dry).
struct GroundRule {
    AnchorGear gear = GEAR_COUNT;  // what sets there (GEAR_COUNT: nothing does)
    const char* why = "";          // nothing does: why
    const char* needs = "";        // out of that gear: what the target says
};
GroundRule groundRule(Surface s, double water);

// "STRAPS 2 SLING 1 STAKES 2 BOLTS 0 ICE 0" (the HUD's font has no '&').
std::string hudLine(const AnchorStock& s);
}  // namespace anchors

// The gear on you.
struct AnchorStock {
    int n[GEAR_COUNT];
    bool refills = true;  // sandbox: a repair station or a respawn fills it again (a career buys its own)
    AnchorStock() { fill(anchors::kSandboxStart); }
    void fill(const int* set) { for (int g = 0; g < GEAR_COUNT; g++) n[g] = set[g]; }
    bool operator==(const AnchorStock& o) const { for (int g = 0; g < GEAR_COUNT; g++) if (n[g] != o.n[g]) return false; return true; }
    bool operator!=(const AnchorStock& o) const { return !(*this == o); }
};

// A stake, bolt or picket you left in the ground: a line to the same spot costs nothing.
struct SetAnchor {
    V3 p;
    AnchorGear gear = GEAR_STAKE;
    double yaw = 0;  // which way it faces (toward the winch it was set from)
};
