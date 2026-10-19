// On foot: get out of the truck to spot a line, walk the trail ahead, and hook the winch
// to a tree by hand. A capsule-ish body (two spheres, feet and chest) that walks, runs,
// jumps, steps up rocks and logs, wades (and swims) and keeps out of trees and trucks.
#pragma once
#include <optional>

#include "softbody.h"
#include "trunk.h"

struct Walker {
    bool active = false;
    V3 p;              // feet
    V3 v;              // velocity
    double yaw = 0;    // facing (0 = +x, the car convention)
    double stride = 0; // walk cycle (radians)
    bool grounded = false;
    double inWater = 0;  // how deep the water is where it stands (m)
    // A wheel in your arms (interact.h): slow going, no running or jumping, and no getting
    // into a vehicle until it's put down or stowed.
    std::optional<WheelItem> carrying;
    // A tool in your hand (the jack): placed and used to lift a vehicle corner.
    bool holdingJack = false;
    bool holdingChain = false;  // a snow chain off the truck, to wrap a tire in
    // A tool, part or anchor out of the back (trunk.h): it's still in your kit, just in your hand
    // (and usable far from the truck); getting in or putting it back lays it in the back again.
    TrunkRef hand;

    // Steps (dt, ~60 Hz): `fwd`/`side` (-1..1) move relative to `viewYaw` (the camera's
    // heading), `run` faster, `jump` once when grounded.
    void step(const World& w, const Sim& sim, double dt, double fwd, double side, bool run, bool jump, double viewYaw);
    // Out of a car, beside its driver's door (on the ground, clear of it); `passenger`: the other door.
    void exitCar(const World& w, const Car& c, bool passenger = false);
    // The nearest vehicle (index into sim.cars, not someone else's) within reach to get
    // into, or -1.
    int carToEnter(const Sim& sim, double reach = 4.0) const;
};
