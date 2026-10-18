// Your own vehicle left standing while you're on foot (Sim::playerAway) sleeps like any parked one: no physics
// while it stands, the same physics to the last bit while it's driven, and when something hits it, it wakes
// and takes the whole crash.
#include <cmath>

#include "harness.h"

namespace {
const VehicleSpec& blazer() { return vehicleCatalog()[7]; }

// Drives the player's Blazer through a few seconds of throttle, steering and braking, in a sim of its own; where its
// nodes end up.
std::vector<V3> drive(bool away) {
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    ctx.add(blazer(), -80, -60, 0);
    ctx.sim.player = 0;
    ctx.sim.playerAway = away;
    Car& c = ctx.car(0);
    const double t0 = ctx.t;
    ctx.run(4.0, [&] {
        const double t = ctx.t - t0;
        c.throttle = t < 2.5 ? 0.6 : 0;
        c.brake = t >= 2.5 ? 0.8 : 0;
        c.steer = 0.3 * std::sin(t * 2);
    });
    std::vector<V3> p;
    for (const Node& n : c.nodes) p.push_back(n.p);
    return p;
}

struct Hit {
    bool slept = false, woke = true;
    double damage = 0;
};
// A Blazer at 50 km/h into the nose of the player's Blazer, standing; `away`: the player's out on foot.
Hit hit(Ctx& ctx, bool away, double offset) {
    ctx.sim.cars.clear();
    ctx.sim.looseWheels.clear();
    const double z = -50;
    ctx.add(blazer(), 0, z, 180);
    ctx.add(blazer(), -(blazer().length + 3), z + offset, 0);
    ctx.sim.player = 0;
    ctx.sim.playerAway = away;
    ctx.run(3.0);
    Car& mine = ctx.car(0);
    Car& other = ctx.car(1);
    Hit r;
    r.slept = mine.sleeping;
    ctx.launch(other, 50 / 3.6);
    double touched = -1;
    ctx.run(2.0, [&] {
        if (touched < 0 && other.bbMax.x >= mine.bbMin.x) touched = ctx.t;
        if (touched >= 0 && ctx.t - touched > 0.1 && mine.sleeping) r.woke = false;
    });
    r.damage = mine.damage;
    ctx.sim.playerAway = false;
    return r;
}
}  // namespace

SCENARIO(playerAwayDriven, "carsleep/driven-identical", "The player's truck driven with the on-foot sleep allowed: every node where it is without it, to the last bit") {
    const std::vector<V3> a = drive(false), b = drive(true);
    double worst = 0;
    for (size_t k = 0; k < a.size() && k < b.size(); k++) worst = std::max(worst, len(a[k] - b[k]));
    ctx.check("same node count", a.size() == b.size());
    ctx.check("identical to the bit", worst == 0, std::to_string(worst) + " m");
}

SCENARIO(playerAwayHit, "carsleep/player-away-hit", "The player's truck left standing sleeps; hit at 50 km/h it wakes at once and takes the crash it would awake") {
    double asleep = 0, awake = 0;
    bool slept = true, woke = true, stayedAwake = true;
    for (double off : {-0.1, 0.0, 0.1}) {
        const Hit s = hit(ctx, true, off), w = hit(ctx, false, off);
        slept &= s.slept, woke &= s.woke, stayedAwake &= !w.slept;
        asleep += s.damage / 3, awake += w.damage / 3;
    }
    ctx.check("left standing while you're on foot, it fell asleep", slept);
    ctx.check("with you in it, it never sleeps", stayedAwake);
    ctx.check("it woke the moment it was hit", woke);
    ctx.metric("its damage, asleep / awake", asleep / std::max(awake, 1e-6), "", 0.6, 1.6);
}
