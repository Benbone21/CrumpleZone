#include "carentry.h"

#include <cmath>

#include "interact.h"

namespace carentry {

Verdict verdict(const Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return NONE;
    const Car& c = sim.cars[car];
    if (c.isProp()) return NONE;
    const double speed = len(c.frame().vel);
    if (c.remote) return c.occupied ? RIDE : speed > 1.0 ? WAIT : TAKE;
    if (c.ai && speed > 0.5) return WAIT;  // (traffic: only once it's stopped)
    if (car != sim.player && speed > 2.0) return WAIT;
    return DRIVE;
}

int nearest(const Sim& sim, const V3& feet, double reach) {
    int best = -1;
    double bestD = reach;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (c.isProp()) continue;
        const Frame f = c.frame();
        const V3 d = feet + V3(0, 1, 0) - f.com;  // (to its outline, a box the size of the car)
        const double ax = std::max(0.0, std::fabs(dot(d, f.fwd)) - c.spec->length / 2), az = std::max(0.0, std::fabs(dot(d, f.left)) - c.spec->width / 2);
        const double dist = std::hypot(ax, az);
        if (dist < bestD) bestD = dist, best = i;
    }
    return best;
}

V3 door(const Car& c, bool passenger) {
    const Frame f = c.frame();  // (the driver's is prompts::doorAt's)
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);
    return f.com + left * ((passenger ? -1 : 1) * (c.spec->width / 2)) + fwd * 0.3 + V3(0, 0.2, 0);
}

V3 acrossCab(const Car& c, const V3& p) {
    const Frame f = c.frame();
    return p - f.left * (2 * dot(p - f.com, f.left));
}

bool option(const Sim& sim, int ci, InteractOption& o) {
    const Verdict v = verdict(sim, ci);
    if (v == NONE) return false;
    const Car& c = sim.cars[ci];
    o = InteractOption();
    o.car = ci;
    o.hold = 0;  // (a tap, J or E: prompts.cpp keys it on J)
    o.kind = v == RIDE ? IA_RIDE_ALONG : IA_ENTER_CAR;
    o.at = door(c, v == RIDE);
    const std::string name = c.spec->name;
    o.prompt = v == RIDE ? "RIDE ALONG IN THE " + name : "GET IN THE " + name;
    if (v == WAIT) o.blocked = true, o.prompt += ": WAIT FOR IT TO STOP";
    return true;
}

}  // namespace carentry
