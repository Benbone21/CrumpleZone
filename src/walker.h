// On foot: get out of the truck to spot a line, walk the trail ahead, and hook the winch
// to a tree by hand. A capsule-ish body (two spheres, feet and chest) that walks, runs,
// jumps, steps up rocks and logs, wades (and swims) and keeps out of trees and trucks.
#pragma once
#include "softbody.h"

struct Walker {
    bool active = false;
    V3 p;              // feet
    V3 v;              // velocity
    double yaw = 0;    // facing (0 = +x, the car convention)
    double stride = 0; // walk cycle (radians)
    bool grounded = false;
    double inWater = 0;  // how deep the water is where it stands (m)

    // Steps (dt, ~60 Hz): `fwd`/`side` (-1..1) move relative to `viewYaw` (the camera's
    // heading), `run` faster, `jump` once when grounded.
    void step(const World& w, const Sim& sim, double dt, double fwd, double side, bool run, bool jump, double viewYaw);
    // Out of a car, beside its driver's door (on the ground, clear of it).
    void exitCar(const World& w, const Car& c);
    // The nearest vehicle (index into sim.cars, not someone else's) within reach to get
    // into, or -1.
    int carToEnter(const Sim& sim, double reach = 4.0) const;
};
