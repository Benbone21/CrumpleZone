// The VW Baja Bug: what it was built for. Kicker jumps and hard landings that a desert Bug
// shrugs off.
#include <cstdio>
#include <cstdlib>

#include "harness.h"

namespace {

const VehicleSpec& baja() {
    for (const VehicleSpec& s : vehicleCatalog())
        if (std::string(s.id) == "baja") return s;
    return vehicleCatalog()[0];
}

// What a hard landing can break on it.
struct Hurt {
    int shafts = 0, popped = 0, detached = 0, arms = 0;
};
Hurt hurt(const Car& c) {
    Hurt h;
    for (const Wheel& w : c.wheels) h.shafts += w.shaftBroken, h.popped += w.popped, h.detached += w.detached;
    for (const Beam& b : c.beams) {
        h.shafts += b.type == BEAM_SHAFT && b.broken;
        h.arms += (b.type == BEAM_ARM || b.type == BEAM_SPRING || b.type == BEAM_TIEROD) && b.broken;
    }
    return h;
}

}  // namespace

// The kicker on the proving ground (3.4 m up over 13 m): a desert Bug is built to fly. Taken at
// 70-76 km/h it flies ~20 m, its tail-heavy weight swinging it back about level, and comes down
// on the flat pad on its long travel: the shocks and bump stops take the landing, not the body,
// and it rolls on with nothing broken. (Each run stops before the landing ramp's back face, 35 m
// on: that's for faster cars. Slower, this kicker noses everything over; faster, it flips.)
SCENARIO(bajaJump, "baja/kicker-jump", "Baja Bug takes the kicker at 70, 73 and 76 km/h: flies, lands about level on its long travel, rolls on with nothing broken") {
    for (double vJump : {70.0, 73.0, 76.0}) {
        ctx.sim.cars.clear();
        Car& c = ctx.add(baja(), 10, -30, 0);
        ctx.focus(c);
        ctx.run(1.0);
        ctx.launch(c, vJump / 3.6);
        double peak = 0, airStart = -1, air = 0, landX = 0, landPitch = 0, landT = -1, worstUp = 1, after = 0;
        const double base = c.frame().com.y;
        bool flying = false;
        ctx.runUntil(6.0, [&] { return landT > 0 && ctx.t > landT + 0.6; }, [&] {
            c.throttle = c.frame().com.x < 53 ? clampd((vJump - speedKmh(c)) * 0.2, 0, 1) : 0.1;
            c.steer = clampd(-0.05 * (c.frame().com.z + 30), -0.1, 0.1);
            peak = std::max(peak, c.frame().com.y - base);
            bool off = true;
            for (const Wheel& w : c.wheels) off &= w.load == 0;
            const double pitch = std::asin(clampd(c.frame().fwd.y, -1, 1)) * 180 / M_PI;  // (+ nose up)
            if (off && c.frame().com.y - base > 1.0) {
                if (!flying && airStart < 0) airStart = ctx.t;
                flying = true;
            } else if (flying && c.frame().com.x > 55) {  // (a hop off the ramp's own face doesn't count)
                flying = false;
                air = ctx.t - airStart;
                if (landT < 0) landT = ctx.t, landX = c.frame().com.x, landPitch = pitch;
            }
            if (landT > 0) after = speedKmh(c);
            worstUp = std::min(worstUp, c.frame().up.y);
            ctx.sample("Pitch", std::to_string((int)vJump) + " km/h", pitch, "deg");
        });
        const Hurt h = hurt(c);
        const std::string at = " (" + std::to_string((int)vJump) + " km/h)";
        ctx.metric("peak height gained" + at, peak, "m", 3, 8);
        ctx.metric("time in the air" + at, air, "s", 1.0, 2.5);
        ctx.metric("touchdown along the pad (lip at 53 m)" + at, landX, "m", 62, 86);
        ctx.metric("pitch at touchdown (+ nose up)" + at, landPitch, "deg", -25, 25);
        ctx.check("stays on its wheels" + at, worstUp > 0.6, "min up.y " + std::to_string(worstUp));
        ctx.metric("damage from the landing" + at, c.damage * 100, "%", 0, 3);
        ctx.metric("shafts, CVs, links, springs or tie rods broken" + at, h.shafts + h.arms, "", 0, 0);
        ctx.metric("tires blown or wheels torn off" + at, h.popped + h.detached, "", 0, 0);
        ctx.metric("engine health" + at, c.engineHealth, "", 0.95, 1.0);
        ctx.metric("still rolling 0.6 s after touchdown" + at, after, "km/h", 30, 90);
    }
}

// Dropped flat from 2.5 m onto the pad (a 2.5 m flat landing: what a Baja's bump stops and
// long travel are for): it soaks it up and drives on.
SCENARIO(bajaFlatDrop, "baja/flat-drop", "Baja Bug dropped flat from 2.5 m onto the pavement: bump stops take it, nothing breaks, it drives off") {
    Car& c = ctx.add(baja(), -120, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    for (Node& n : c.nodes) n.p.y += 2.5;
    ctx.run(3.0);
    const double dmg = c.damage;
    const V3 p0 = c.frame().com;
    ctx.run(3.0, [&] { c.throttle = 0.5; });
    const Hurt h = hurt(c);
    ctx.metric("damage from a 2.5 m flat landing", dmg * 100, "%", 0, 2.5);
    ctx.metric("shafts or CVs broken", h.shafts, "", 0, 0);
    ctx.metric("suspension links, springs or tie rods broken", h.arms, "", 0, 0);
    ctx.metric("tires blown", h.popped, "", 0, 0);
    ctx.metric("wheels torn off", h.detached, "", 0, 0);
    ctx.metric("drives off", len(c.frame().com - p0), "m", 5, 100);
}
