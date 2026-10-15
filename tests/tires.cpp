// Wheels that come off, and changing them on foot (src/tires.*, src/interact.*): the trucks
// (Bronco, 4Runner, Tacoma) only.
#include <cmath>

#include "../src/interact.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }
const VehicleSpec& runner() { return vehicleCatalog()[3]; }

double totalMass(const Car& c) {
    double m = 0;
    for (const Node& n : c.nodes) m += n.m;
    return m;
}

V3 hubOf(const Car& c, int w) { return (c.nodes[c.wheels[w].inner].p + c.nodes[c.wheels[w].outer].p) * 0.5; }

// Stands the walker `back` m from `at` (out from the truck's middle) and looks at it.
void standAt(const World& w, const Car& c, Walker& wk, const V3& at, double back) {
    V3 out = at - c.frame().com;
    out.y = 0;
    out = norm(out);
    const V3 q = at + out * back;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

// Holds E on whatever's in front of the walker until something's said (or `secs` pass).
std::string holdOn(Ctx& ctx, Interact& ia, Walker& wk, const V3& at, double secs) {
    std::string said;
    double t = 0;
    while (said.empty() && t < secs) {
        const V3 eye = wk.p + V3(0, 1.62, 0);
        said = ia.update(ctx.sim, wk, eye, at - eye, true, Ctx::kFrame);
        ctx.run(Ctx::kFrame);
        t += Ctx::kFrame;
    }
    return said;
}

// Drives straight east at a walking pace for `dist` m; returns how far it wandered sideways.
double driveStraight(Ctx& ctx, Car& c, double dist) {
    const V3 start = c.frame().com;
    double wander = 0;
    ctx.runUntil(40, [&] { return c.frame().com.x - start.x > dist; }, [&] {
        const Frame f = c.frame();
        c.handbrake = 0;
        c.brake = 0;
        c.throttle = clampd(0.35 + (3.0 - dot(f.vel, f.fwd)) * 0.5, 0, 0.7);
        c.steer = clampd(-(f.com.z - start.z) * 0.08 - std::atan2(-f.fwd.z, f.fwd.x) * 1.2, -0.3, 0.3);
        wander = std::max(wander, std::fabs(f.com.z - start.z));
    });
    return wander;
}

}  // namespace

SCENARIO(tireSwap, "tires/swap", "A flat on the Bronco: on foot, take the flat off, set it down, take the spare off the gate, bolt it on and drive away") {
    Car& c = ctx.add(bronco(), -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    World& w = ctx.sim.world;
    ctx.check("the Bronco carries a spare", c.hasSpare && !c.spareNodes.empty());
    const int k = 2;  // left rear
    c.wheels[k].popped = true;
    c.handbrake = 1;
    ctx.run(1.0);
    Walker wk;
    Interact ia;
    // The flat off.
    const V3 hub = hubOf(c, k);
    standAt(w, c, wk, hub, 0.9);
    std::string said = holdOn(ctx, ia, wk, hub, 12);
    ctx.check("the flat comes off (hold E)", said == "WHEEL OFF" && wk.carrying && wk.carrying->popped, said);
    ctx.check("the corner is on its bare hub", !c.wheels[k].mounted);
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    ctx.check("set down, it lies by the truck", ctx.sim.looseWheels.size() == 1 && !wk.carrying);
    // The spare off the gate.
    const double before = totalMass(c);
    const V3 spare = c.spareAt();
    standAt(w, c, wk, spare, 0.8);
    said = holdOn(ctx, ia, wk, spare, 8);
    ctx.check("the spare comes off the gate", said == "GOT THE SPARE" && wk.carrying && !c.hasSpare, said);
    ctx.metric("weight off the truck with the spare", before - totalMass(c), "kg", 30, 50);
    // On it goes.
    const V3 hub2 = hubOf(c, k);
    standAt(w, c, wk, hub2, 0.9);
    said = holdOn(ctx, ia, wk, hub2, 14);
    ctx.check("the spare goes on", said == "WHEEL ON" && c.wheels[k].mounted && !c.wheels[k].popped && !wk.carrying, said);
    ctx.run(1.5);
    // And away (back in, driving it).
    wk.active = false;
    const double dmg = c.damage;
    const double wander = driveStraight(ctx, c, 50);
    ctx.metric("drives 50 m on the spare: sideways wander", wander, "m", 0, 1.5);
    ctx.metric("no damage from the change", (c.damage - dmg) * 100, "%", 0, 0.2);
}

SCENARIO(tireLooseWheel, "tires/loose-wheel", "A wheel rolled off at 25 km/h rolls on, falls over and comes to rest on the ground, whole") {
    Car& c = ctx.add(bronco(), -100, -50, 0);
    ctx.focus(c);
    ctx.run(0.5);
    World& w = ctx.sim.world;
    WheelItem it = c.wheels[0].item;
    const int li = ctx.sim.dropWheel(it, V3(-80, w.height(-80, -40), -40), V3(1, 0, 0));
    LooseWheel& lw = ctx.sim.looseWheels[li];
    lw.v = V3(7, 0, 0);
    lw.w = lw.axis() * (-7 / it.R);
    // (Its axle points across the way it rolls: forward roll is the right-hand spin about -axle or +axle; pick the one that rolls it east.)
    if (dot(cross(lw.w, V3(0, -it.R, 0)), V3(1, 0, 0)) < 0) lw.w = -lw.w;
    const V3 start = lw.p;
    double lowest = 1e9;
    ctx.run(12.0, [&] {
        const LooseWheel& l = ctx.sim.looseWheels[0];
        lowest = std::min(lowest, l.p.y - w.height(l.p.x, l.p.z));
    });
    const LooseWheel& l = ctx.sim.looseWheels[0];
    ctx.check("stays whole (finite)", std::isfinite(l.p.x + l.p.y + l.p.z));
    ctx.metric("rolled", len(V3(l.p.x - start.x, 0, l.p.z - start.z)), "m", 5, 200);
    ctx.metric("never sank into the ground (lowest center over it)", lowest, "m", it.width * 0.3, 1);
    ctx.check("came to rest", l.sleeping || len(l.v) < 0.2, std::to_string(len(l.v)));
}

SCENARIO(tireRefitLost, "tires/refit-lost", "A Bronco wheel that came off is picked up, carried back and bolted on again; it drives") {
    Car& c = ctx.add(bronco(), -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    World& w = ctx.sim.world;
    c.handbrake = 1;
    const int k = 1;  // right front
    const int li = ctx.sim.shedWheel(ctx.sim.player, k, false);
    ctx.check("the wheel comes off", li >= 0 && !c.wheels[k].mounted);
    ctx.run(4.0);
    Walker wk;
    Interact ia;
    const V3 at = ctx.sim.looseWheels[0].p;
    standAt(w, c, wk, at, 0.7);
    std::string said = holdOn(ctx, ia, wk, at, 3);
    ctx.check("picked up", wk.carrying.has_value() && ctx.sim.looseWheels.empty());
    const V3 hub = hubOf(c, k);
    standAt(w, c, wk, hub, 0.9);
    said = holdOn(ctx, ia, wk, hub, 14);
    ctx.check("bolted back on", said == "WHEEL ON" && c.wheels[k].mounted, said);
    ctx.run(1.5);
    const double wander = driveStraight(ctx, c, 30);
    ctx.metric("drives 30 m: sideways wander", wander, "m", 0, 1.5);
}

SCENARIO(tireWrongSize, "tires/wrong-size", "A 35 in wheel off the 4Runner bolted onto the Bronco's 33 in corner: it fits, leans a little and drives") {
    Car& b = ctx.add(bronco(), -100, -50, 0);
    Car& r = ctx.add(runner(), -100, -40, 0);
    ctx.focus(b);
    ctx.run(1.0);
    World& w = ctx.sim.world;
    b.handbrake = r.handbrake = 1;
    Walker wk;
    Interact ia;
    const int bi = 0, ri = 1;
    (void)ri;
    WheelItem off;
    ctx.check("Bronco wheel off", ctx.sim.removeWheel(bi, 2, off));
    WheelItem big;
    ctx.check("4Runner spare off", ctx.sim.takeSpare(ri, big));
    wk.carrying = big;
    const V3 hub = hubOf(b, 2);
    standAt(w, b, wk, hub, 0.9);
    const std::string said = holdOn(ctx, ia, wk, hub, 14);
    ctx.check("the 35 in wheel goes on", said == "WHEEL ON" && std::fabs(b.wheels[2].R - big.R) < 1e-9, said);
    ctx.run(2.0);
    ctx.metric("its radius", b.wheels[2].R * 2 / 0.0254, "in", 34, 36);
    ctx.sim.player = bi;
    const double wander = driveStraight(ctx, b, 30);
    ctx.metric("drives 30 m: sideways wander", wander, "m", 0, 2.0);
}

SCENARIO(tireShear, "tires/shear", "A Bronco sliding sideways into a curb at 45 km/h shears a wheel off; at 15 km/h it doesn't") {
    for (double kmh : {15.0, 45.0}) {
        ctx.sim.cars.clear();
        ctx.sim.looseWheels.clear();
        ctx.sim.player = 0;
        World& w = ctx.sim.world;
        const double z = kmh < 20 ? -50 : 40;
        w.addBox(V3(-100, w.height(-100, z + 6) + 0.125, z + 6), V3(8, 0.125, 0.3), 0, 0, 0, V3(0.5, 0.5, 0.5));
        w.boxes.back().rock = true;
        w.buildGrid();
        Car& c = ctx.add(bronco(), -100, z, 0);
        ctx.focus(c);
        ctx.run(1.0);
        for (Node& n : c.nodes) n.v = V3(0, 0, kmh / 3.6);
        double peak = 0;
        ctx.run(3.0, [&] {
            for (const Wheel& wh : c.wheels) peak = std::max(peak, (double)wh.studLoad);
        });
        int off = 0;
        for (const Wheel& wh : c.wheels) off += !wh.mounted;
        const std::string tag = std::to_string((int)kmh) + " km/h";
        ctx.metric(tag + ": peak load on the studs", peak / 1000, "kN", 0, 1000);
        if (kmh < 20) ctx.check(tag + ": every wheel stays on", off == 0, std::to_string(off));
        else ctx.check(tag + ": a wheel comes off", off >= 1 && !ctx.sim.looseWheels.empty(), std::to_string(off));
    }
}

SCENARIO(tireDebead, "tires/debead", "The 4Runner aired to 8 psi, flung into a hard turn at 45 km/h, rolls a tire off its rim; at 20 psi it doesn't") {
    for (double psi : {20.0, 8.0}) {
        ctx.sim.cars.clear();
        ctx.sim.looseWheels.clear();
        ctx.sim.player = 0;
        Car& c = ctx.add(runner(), -150, psi > 10 ? -50 : 40, 0);
        ctx.focus(c);
        ctx.run(1.0);
        c.tirePsi = psi;
        // Up to speed, then the wheel hard over.
        ctx.runUntil(15, [&] { return dot(c.frame().vel, c.frame().fwd) > 12.5; }, [&] {
            c.throttle = 1;
            c.steer = 0;
        });
        ctx.run(3.0, [&] {
            c.throttle = 0.3;
            c.steer = 0.5;
        });
        int off = 0;
        for (const Wheel& wh : c.wheels) off += wh.debeaded;
        const std::string tag = std::to_string((int)psi) + " psi";
        if (psi > 10) ctx.check(tag + ": the beads hold", off == 0, std::to_string(off));
        else ctx.check(tag + ": a tire rolls off its rim", off >= 1, std::to_string(off));
    }
}

SCENARIO(tirePressure, "tires/pressure", "Airing changes pressure at stopped vehicles and preserves damaged tire contact radii") {
    Car& c = ctx.add(bronco(), -150, -50, 0);
    c.tirePsi = 35;
    c.airing = -1;
    ctx.sim.updateTrail(1);
    ctx.check("valves deflate five psi per second", std::fabs(c.tirePsi - 30) < 1e-9);
    c.airing = 1;
    ctx.sim.updateTrail(1);
    ctx.check("onboard compressor inflates two psi per second", std::fabs(c.tirePsi - 32) < 1e-9);
    ctx.sim.updateTrail(10);
    ctx.check("inflation stops at forty psi", c.tirePsi == 40);
    c.airing = -1;
    ctx.sim.updateTrail(10);
    ctx.check("deflation stops at twelve psi", c.tirePsi == 12);
    for (Node& n : c.nodes) n.v = V3(2, 0, 0);
    c.airing = 1;
    ctx.sim.updateTrail(1);
    ctx.check("moving vehicles cannot inflate", c.tirePsi == 12);
    Wheel wh = c.wheels[0];
    wh.load = 3000;
    const double full = wh.loadedRadius(35, 35), low = wh.loadedRadius(12, 35);
    ctx.check("low pressure lowers the hub without collapsing onto the rim", low < full && low > wh.rimR + 0.05);
    ctx.check("low stock pressure remains finite", std::isfinite(wh.loadedRadius(12, 12)) && wh.loadedRadius(12, 12) > wh.rimR);
    wh.load = 0;
    ctx.check("unloaded tires do not shrink when aired down", wh.loadedRadius(12, 35) == wh.R);
    wh.load = 3000;
    wh.popped = true;
    ctx.check("punctured tires keep the rim contact radius", wh.loadedRadius(35, 35) == wh.rollR());
    wh.popped = false; wh.debeaded = true;
    ctx.check("debeaded tires keep the rim contact radius", wh.loadedRadius(12, 35) == wh.rollR());
    wh.debeaded = false; wh.mounted = false;
    ctx.check("missing wheels keep the bare hub radius", wh.loadedRadius(12, 35) == kBareHubR);
}
