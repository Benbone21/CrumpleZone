// The live rear axle's hub shake. A rear hub sits at the end of its housing on beams that hold it
// loosely in toe; with the tire's side force in the loop it rang at ~22 Hz, the hub axis toeing
// +-2 degrees while the hub's centre barely moved, on every live-axle truck: the Blazer on race
// slicks from 25 km/h, on its all-terrains too, a trace on the Tacoma. Sampled at 60 Hz that
// aliases to a slow yaw weave, and what a driver felt was a steady pull. So these sample the hub
// itself at the physics step, where nothing can alias it. (fitment/blazer/tires catches the pull.)
// Side against side and in the shake's own band: a leaf-sprung axle hopping under power (the
// Tacoma on stock tires, ~16 Hz, both sides together) is something else again.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/carsave.h"
#include "../src/mods.h"
#include "harness.h"

namespace {

const World& padWorld() {
    static const World w = [] {
        World x(MAP_FREEWAY);
        for (double px = -320; px < 320; px += 1)
            for (double pz = -500; pz < -300; pz += 1) {
                const int id = x.cellIndex(px, pz);
                if (id >= 0) x.setCellHeight(id, 0), x.setCellSurface(id, SURF_PAVED, PAINT_PAD);
            }
        x.buildGrid();
        return x;
    }();
    return w;
}

int vehicle(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return -1;
}

// That truck on race slicks (or as it comes).
VehicleSpec onTires(int v, bool slicks) {
    if (!slicks) return vehicleCatalog()[v];
    const auto& opts = modOptions(v, MOD_TIRES);
    for (int o = 1; o < (int)opts.size(); o++) {
        ModSet m;
        m.pick[MOD_TIRES] = o;
        if (tunedSpec(v, m).tireType == TIRE_RACE_SLICK) return tunedSpec(v, m);
    }
    return vehicleCatalog()[v];
}

// A wheel's steer against the body, from its hub's axle line (deg, + left).
double hubToe(const Car& c, const Wheel& wh) {
    const V3 a = c.chassis.rot.unrotate(c.nodes[wh.outer].p - c.nodes[wh.inner].p), a0 = c.restLocal[wh.outer] - c.restLocal[wh.inner];
    return std::remainder(std::atan2(a.x, a.z) - std::atan2(a0.x, a0.z), 2 * M_PI) * 180 / M_PI;
}

// The biggest sine between 18 and 28 Hz in a series sampled at `hz` (its amplitude; Hann window).
double shakeOf(const std::vector<double>& v, double hz) {
    const int n = (int)v.size();
    double mean = 0, best = 0;
    for (double x : v) mean += x / n;
    for (double f = 18; f <= 28; f += 0.25) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)), ph = 2 * M_PI * f * i / hz;
            re += (v[i] - mean) * w * std::cos(ph), im += (v[i] - mean) * w * std::sin(ph);
        }
        best = std::max(best, 4 * std::hypot(re, im) / n);
    }
    return best;
}

// The beam from a live axle's outer hub node to its link bracket.
int hubBeam(const Car& c, const Wheel& wh) {
    for (size_t bi = 0; bi < c.beams.size(); bi++) {
        const Beam& b = c.beams[bi];
        if (b.type == BEAM_KNUCKLE && (b.a == wh.outer || b.b == wh.outer) && (b.a == wh.lower || b.b == wh.lower)) return (int)bi;
    }
    return -1;
}

// A rear hub's toe and the housing's own yaw against the body, both as steer (deg). The housing's
// from its diff to the seats and brackets of both ends.
double housingYaw(const Car& c, const Axle& ax) {
    const Wheel &l = c.wheels[ax.wheel[0]], &r = c.wheels[ax.wheel[1]];
    const auto line = [&](auto at) { return at(r.top) + at(r.lower) - at(l.top) - at(l.lower); };
    const V3 a = c.chassis.rot.unrotate(line([&](int n) { return c.nodes[n].p; })), a0 = line([&](int n) { return c.restLocal[n]; });
    return std::remainder(std::atan2(a.x, a.z) - std::atan2(a0.x, a0.z), 2 * M_PI) * 180 / M_PI;
}

const Axle& rearAxle(const Car& c) {
    for (const Axle& a : c.axles)
        if (!a.front) return a;
    return c.axles.back();
}

struct Shake {
    double toe = 0, load = 0, pull = 0, kmh = 0, restToe = 0;
};

// Up to `target` km/h hands off down the pad and held there for 4 s, sampled every physics step.
Shake drive(Ctx& ctx, const VehicleSpec& spec, double target, double bend = 0) {
    ctx.sim = Sim();
    ctx.sim.world = padWorld();
    Car& c = ctx.add(spec, -300, -400, 0);
    c.blowoutsOn = false;
    ctx.sim.player = 0;
    ctx.run(2.5, [&] { c.handbrake = 1; });
    c.handbrake = 0;
    std::vector<int> rear;
    for (size_t i = 0; i < c.wheels.size(); i++)
        if (!c.wheels[i].front) rear.push_back((int)i);
    if (bend > 0) c.beams[hubBeam(c, c.wheels[rear[0]])].L0 *= 1 + bend;
    Shake r;
    for (int w : rear) r.restToe = std::max(r.restToe, std::fabs(hubToe(c, c.wheels[w])));
    auto spinning = [&] {
        double worst = 0;
        for (const Wheel& q : c.wheels)
            if (q.driven) worst = std::max(worst, std::fabs(q.sx));
        return worst > 0.12;
    };
    const double t0 = ctx.t;
    ctx.runUntil(30, [&] { return speedKmh(c) > target - 3; }, [&] { c.steer = 0, c.brake = 0, c.throttle = (ctx.t - t0 < 4 ? 0.45 : 0.9) * (spinning() ? 0.25 : 1.0); });
    auto hold = [&] { return clampd((target - speedKmh(c)) * 0.05, 0.0, 0.35); };
    ctx.run(1.0, [&] { c.steer = 0, c.throttle = hold(); });
    const auto heading = [&] { const Frame f = c.frame(); return std::atan2(f.fwd.z, f.fwd.x) * 180 / M_PI; };
    const double h0 = heading(), hz = 1 / ctx.sim.h;
    std::vector<double> toe, load;
    const int steps = (int)(4 * hz), perFrame = (int)(hz / 60);
    for (int i = 0; i < steps; i++) {
        if (i % perFrame == 0) c.steer = 0, c.brake = 0, c.throttle = hold();
        ctx.sim.substep();
        const Wheel &a = c.wheels[rear[0]], &b = c.wheels[rear[1]];
        toe.push_back((hubToe(c, a) - hubToe(c, b)) / 2), load.push_back((a.load - b.load) / 2);
    }
    r.toe = shakeOf(toe, hz), r.load = shakeOf(load, hz);
    r.pull = std::remainder(heading() - h0, 360);
    r.kmh = speedKmh(c);
    return r;
}

}  // namespace

SCENARIO(liveAxleShake, "dynamics/live-axle-shake", "Live rear axles at a steady speed, on slicks and stock tires: the rear hubs hold their toe, no 22 Hz shake, and the truck runs straight") {
    const struct {
        const char* id;
        bool slicks;
        double kmh;
    } runs[] = {{"blazer", true, 58}, {"blazer", true, 25}, {"blazer", false, 25}, {"blazer", false, 58}, {"tacoma", true, 58}, {"tacoma", false, 25}, {"bronco", true, 58}, {"bronco", false, 25}};
    for (const auto& run : runs) {
        const int v = vehicle(run.id);
        if (v < 0) {
            ctx.check(std::string(run.id) + " is in the catalog", false);
            continue;
        }
        const VehicleSpec spec = onTires(v, run.slicks);
        const Shake s = drive(ctx, spec, run.kmh);
        char tag[64];
        std::snprintf(tag, sizeof tag, "%s %s %.0f km/h", run.id, run.slicks ? "slicks" : "stock", run.kmh);
        ctx.metric(std::string(tag) + ": rear hubs' toe shake, side against side", s.toe, "deg", 0, 0.02);
        ctx.metric(std::string(tag) + ": rear wheels' load shake, side against side", s.load, "N", 0, 10);
        ctx.metric(std::string(tag) + ": heading drift in 4 s", std::fabs(s.pull), "deg", 0, 1.5);
        ctx.metric(std::string(tag) + ": rear toe at rest", s.restToe, "deg", 0.2, 0.5);
    }
}

SCENARIO(liveAxleSteer, "dynamics/live-axle-steer", "The rear hub toe damper works against the housing: the whole axle still steers on its links as it articulates, as far as it did without one") {
    const struct {
        const char* id;
        double steer;  // deg, before there was a damper
    } trucks[] = {{"blazer", 1.05}, {"tacoma", 0.82}, {"bronco", 0.33}};
    for (const auto& truck : trucks) {
        const char* id = truck.id;
        const int v = vehicle(id);
        ctx.sim = Sim();
        ctx.sim.world = padWorld();
        Car& c = ctx.add(vehicleCatalog()[v], -300, -400, 0);
        ctx.sim.player = 0;
        ctx.run(2.0, [&] { c.handbrake = 1; });
        const Axle& ax = rearAxle(c);
        const Wheel &l = c.wheels[ax.wheel[0]], &r = c.wheels[ax.wheel[1]];
        const double yaw0 = housingYaw(c, ax);
        // Levered up on one side and down on the other: the axle rolls under the truck.
        c.extForce.assign(c.nodes.size(), V3());
        for (int n : {l.inner, l.outer}) c.extForce[n] = V3(0, 4000, 0);
        for (int n : {r.inner, r.outer}) c.extForce[n] = V3(0, -4000, 0);
        ctx.run(1.5, [&] { c.handbrake = 1; });
        const double yaw = housingYaw(c, ax) - yaw0;
        c.extForce.clear();
        ctx.metric(std::string(id) + ": the axle's steer, rolled under the truck", std::fabs(yaw), "deg", truck.steer * 0.9, truck.steer * 1.1);
    }
}

SCENARIO(liveAxleBentHub, "dynamics/live-axle-bent-hub", "A bent rear hub doesn't shake either, sits where its beams put it through a save and back, and straightened comes back true") {
    const int v = vehicle("blazer");
    const Shake bent = drive(ctx, onTires(v, true), 25, 0.005);
    ctx.metric("bent, on slicks at 25 km/h: rear hubs' toe shake", bent.toe, "deg", 0, 0.02);
    ctx.metric("bent, on slicks at 25 km/h: rear wheels' load shake", bent.load, "N", 0, 10);
    ctx.sim = Sim();
    ctx.sim.world = padWorld();
    Car& c = ctx.add(vehicleCatalog()[v], -300, -400, 0);
    ctx.sim.player = 0;
    ctx.run(2.0, [&] { c.handbrake = 1; });
    const Wheel& wh = c.wheels[rearAxle(c).wheel[0]];
    const int beam = hubBeam(c, wh);
    if (!ctx.check("the hub's beam is there", beam >= 0)) return;
    const double straight = hubToe(c, wh);
    c.beams[beam].L0 = c.beams[beam].Lorig * 1.03;
    ctx.run(1.5, [&] { c.handbrake = 1; });
    const double out = hubToe(c, wh);
    ctx.metric("bent 3 %: toe moved", std::fabs(out - straight), "deg", 0.2, 30);
    const std::string saved = carsave::capture(ctx.sim, c);
    ctx.sim = Sim();
    ctx.sim.world = padWorld();
    Car& back = ctx.add(vehicleCatalog()[v], -300, -400, 0);
    ctx.sim.player = 0;
    ctx.check("the save reads back", carsave::restore(ctx.sim, back, saved));
    ctx.run(1.5, [&] { back.handbrake = 1; });
    const Wheel& hub = back.wheels[rearAxle(back).wheel[0]];
    ctx.metric("bent, saved and back: toe against the bent one's", std::fabs(hubToe(back, hub) - out), "deg", 0, 0.1);
    back.beams[beam].L0 = back.beams[beam].Lorig;
    ctx.run(1.5, [&] { back.handbrake = 1; });
    ctx.metric("straightened: toe off where it was", std::fabs(hubToe(back, hub) - straight), "deg", 0, 0.4);
}
