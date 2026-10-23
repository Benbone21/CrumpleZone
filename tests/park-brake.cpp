// The parking brake (P): a latched rear brake, separate from the held handbrake.
#include <cmath>

#include "harness.h"

namespace {
const VehicleSpec& blazer() { return vehicleCatalog()[7]; }

// A Blazer launched at 8 m/s with no pedals: how far it coasts in 4 s, parking brake set or not.
double coast(bool park) {
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    ctx.add(blazer(), -80, -60, 0);
    Car& c = ctx.car(0);
    ctx.run(1.5);
    ctx.launch(c, 8.0);
    const V3 a = c.frame().com;
    ctx.run(4.0, [&] {
        c.throttle = 0, c.brake = 0, c.handbrake = 0;
        c.parkBrake = park;
    });
    const V3 b = c.frame().com;
    return std::hypot(b.x - a.x, b.z - a.z);
}
}  // namespace

SCENARIO(parkBrakeDrags, "controls/park-brake", "The parking brake stays on with the handbrake released and stops a coasting Blazer much sooner") {
    const double free = coast(false), parked = coast(true);
    ctx.note("coast: brake off " + std::to_string(free) + " m, parking brake " + std::to_string(parked) + " m");
    ctx.check("the parking brake stops it in under 60% of the free coast", parked < 0.6 * free,
              std::to_string(parked) + " m vs " + std::to_string(free) + " m");
}
