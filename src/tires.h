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

// Hub (rotor, studs and drum) a corner rolls on with no wheel bolted to it (m).
constexpr double kBareHubR = 0.17;
// How many loose wheels the world keeps (the oldest are cleared past this).
constexpr int kMaxLooseWheels = 16;
