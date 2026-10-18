// Wheels as things: a wheel you can carry, a spare on its carrier, a wheel lying on the
// trail. A wheel's studs shear in a hard enough hit and it comes off; an aired-down tire
// rolls off its rim in a hard turn. On foot you can take a truck's spare, carry it, and
// bolt it (or any wheel lying about) onto an empty hub (interact.h).
#pragma once
#include <cstdint>
#include <string>

#include "mathx.h"

// A wheel (rim and tire) or a bare tire, off the vehicle: what you carry, what sits on a
// carrier, what lies in the dirt.
struct WheelItem {
    double R = 0.32, rimR = 0.21, width = 0.24;  // tire radius, rim radius, tread width (m)
    double mass = 20;                            // kg
    double I = 1.2;                              // spin inertia (kg m^2)
    bool popped = false;                         // a blown tire
    bool tireOnly = false;                       // just the tire: it rolled off its rim
    int style = 0;                               // the rim (WheelStyle), for drawing
    int tireWork = 0;                            // a new tire going on it, off the truck (fixes.h tireSteps): steps done
    std::string from;                            // the vehicle (spec id) it came off
    // "35 in", for prompts.
    std::string sizeName() const;
};

// A wheel lying (or rolling, or bouncing) about the world: a rigid cylinder of its own,
// against the ground, rocks and vehicles.
struct LooseWheel {
    WheelItem item;
    V3 p, v;        // center, velocity
    V3 w;           // angular velocity (world)
    Quat q;         // its frame: local z is the axle
    bool sleeping = false;
    double still = 0;    // s it's been at rest
    double age = 0;      // s since it came off (the oldest go first when there are too many)
    uint32_t netId = 0;  // multiplayer: the same on every machine
    bool remote = false; // another player's: moved by the network
    V3 f, t;             // force and torque from vehicles this step
    V3 axis() const { return q.rotate(V3(0, 0, 1)); }
};

// Blowouts: a tire driven at or past its speed rating (tireSpeedRatingKmh) can fail, at a chance
// per second that climbs steeply the further over it is and with load and tread heat.
// Tuning, so the curve can be moved in one place: rate = base * exp(steep * (frac - 1)) * load * heat,
// frac = tire road speed / rating, heat = wheelTempFactor (1 = nominal; tire temperature plugs in there).
// At nominal load: ~1/4000 s at the rating, ~1/200 s per tire at +15%, ~1/10 s at +30%.
struct BlowoutTuning {
    double base = 2.5e-4;   // per second, at the rating
    double steep = 20.3;    // e-fold per unit of frac
    double loadExp = 1.5;   // exponent on load over the tire's static share
};
extern BlowoutTuning gBlowout;
// Chance per second of this tire blowing: `frac` speed over rating, `loadRatio` load over its static share, tread heat.
double tireBlowoutRate(double frac, double loadRatio, double heat);
struct Wheel;
// How much hotter (>1) or cooler (<1) than nominal this tire's tread is: a multiplier on the blowout chance. 1 for now.
double wheelTempFactor(const Wheel& wh);
// A deterministic die in [0,1): the same (seed, wheel, tick) always rolls the same.
double blowoutDie(uint64_t seed, int wheel, uint32_t tick);

// Hub (rotor, studs and drum) a corner rolls on with no wheel bolted to it (m).
constexpr double kBareHubR = 0.17;
// How many loose wheels the world keeps (the oldest are cleared past this).
constexpr int kMaxLooseWheels = 16;
