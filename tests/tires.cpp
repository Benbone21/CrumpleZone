// Wheels that come off, and changing them on foot (src/tires.*, src/interact.*): the trucks
// (Bronco, 4Runner, Tacoma) only.
#include <cmath>

#include "../src/handwork.h"
#include "../src/interact.h"
#include "../src/modelkit.h"
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

// Lets go of E (a held job that finished wants it let go before the next).
void letGo(Ctx& ctx, Interact& ia, Walker& wk) {
    const V3 eye = wk.p + V3(0, 1.62, 0);
    ia.update(ctx.sim, wk, eye, V3(1, 0, 0), false, Ctx::kFrame);
}

// The jack out of the back, under corner k, worked up until the tire's off the ground.
bool jackUp(Ctx& ctx, Interact& ia, Walker& wk, Car& c, int k) {
    const World& w = ctx.sim.world;
    const V3 stow = c.stowAt(Car::STOW_JACK);
    standAt(w, c, wk, stow, 1.0);
    if (holdOn(ctx, ia, wk, stow, 4) != "GOT THE JACK") return false;
    letGo(ctx, ia, wk);
    const V3 spot = Interact::jackAim(ctx.sim, c, k);
    standAt(w, c, wk, hubOf(c, k), 1.1);
    if (holdOn(ctx, ia, wk, spot, 4) != "JACK UNDER") return false;
    letGo(ctx, ia, wk);
    const std::string up = holdOn(ctx, ia, wk, spot, 8);
    letGo(ctx, ia, wk);
    ctx.run(0.5);
    return up == "CORNER UP" && Interact::cornerLifted(ctx.sim, c, k);
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
    // Jack it up, then the flat off.
    ctx.check("the corner jacks up (hold E: the jack out, under, up)", jackUp(ctx, ia, wk, c, k), std::to_string(c.jackHeight));
    const V3 hub = hubOf(c, k);
    standAt(w, c, wk, hub, 0.9);
    letGo(ctx, ia, wk);
    ia.update(ctx.sim, wk, wk.p + V3(0, 1.62, 0), hub - (wk.p + V3(0, 1.62, 0)), false, Ctx::kFrame);
    ctx.check("a wheel's five nuts take 4 to 6 s (about two ratchet swings each)", ia.current.kind == IA_REMOVE_WHEEL && ia.current.hold >= 4 && ia.current.hold <= 6, std::to_string(ia.current.hold));
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
    letGo(ctx, ia, wk);
    const V3 jack = Interact::jackAim(ctx.sim, c, k);
    ctx.check("the jack comes down", holdOn(ctx, ia, wk, jack, 8) == "JACK DOWN");
    letGo(ctx, ia, wk);
    const std::string out = holdOn(ctx, ia, wk, jack, 3);
    ctx.check("...and out", out == "GOT THE JACK" && !c.jackPlaced, out + " / " + ia.current.prompt);
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
    // (Set it down to work the jack, jack the bare corner up, pick it up again.)
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    ctx.check("the bare corner jacks up", jackUp(ctx, ia, wk, c, k));
    const V3 lw = ctx.sim.looseWheels[0].p;
    standAt(w, c, wk, lw, 0.7);
    holdOn(ctx, ia, wk, lw, 3);
    letGo(ctx, ia, wk);
    const V3 hub = hubOf(c, k);
    standAt(w, c, wk, hub, 0.9);
    said = holdOn(ctx, ia, wk, hub, 14);
    ctx.check("bolted back on", said == "WHEEL ON" && c.wheels[k].mounted, said);
    c.jackPlaced = false, c.jackHeight = 0;
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
    ctx.check("the bare corner jacks up", jackUp(ctx, ia, wk, b, 2));
    wk.carrying = big;
    const V3 hub = hubOf(b, 2);
    standAt(w, b, wk, hub, 0.9);
    const std::string said = holdOn(ctx, ia, wk, hub, 14);
    ctx.check("the 35 in wheel goes on", said == "WHEEL ON" && std::fabs(b.wheels[2].R - big.R) < 1e-9, said);
    b.jackPlaced = false, b.jackHeight = 0;
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

// A jack goes under the truck's frame: its saddle meets the jack pad welded under the rail's outside edge, just beside the wheel well
// (behind a front wheel, ahead of a rear one: Car::jackPoint), handle out. (A unibody has no rail: its axle or arm, as before.)
SCENARIO(tireJackSpots, "tires/jack-spots", "Every listed vehicle, every corner: a truck's jack point is on its frame rail's jack pad, outside the wheel well (clear of the tire's footprint along the car) and within 1.2 m of the hub, over the ground, its handle pointing out; a unibody's is under the axle or arm") {
    const std::vector<int>& list = listedVehicles();
    std::vector<int> cars;
    for (int i = 0; i < (int)list.size(); i++) cars.push_back((int)(&ctx.add(vehicleCatalog()[list[i]], -200 + 25 * i, -50, 0) - ctx.sim.cars.data()));
    ctx.run(1.0);
    for (int ci : cars) {
        const Car& c = ctx.sim.cars[ci];
        const std::string who = c.spec->id;
        const Frame fr = c.frame();
        const bool rail = !c.ladder.empty();
        int near = 0, clear = 0, onRail = 0, under = 0, handle = 0, ground = 0, other = 0;
        std::string detail;
        double track = 0;
        for (int k = 0; k < (int)c.wheels.size(); k++) track = std::max(track, std::fabs(dot(hubOf(c, k) - fr.com, fr.left)));
        track *= 2;
        for (int k = 0; k < (int)c.wheels.size(); k++) {
            const Wheel& wh = c.wheels[k];
            const V3 hub = hubOf(c, k), point = c.jackPoint(k), spot = Interact::jackSpot(ctx.sim, c, k);
            const double along = dot(point - hub, fr.fwd);
            near += std::fabs(along) < 1.2 && len(V3(point.x - hub.x, 0, point.z - hub.z)) < 1.2;
            clear += !rail || std::fabs(along) > wh.R;  // (outside the tire's footprint: not in the well)
            ground += spot.y < point.y - 0.05 && std::fabs(spot.x - point.x) < 0.02 && std::fabs(spot.z - point.z) < 0.02;
            if (rail) {
                // On the rail: the pad is under its outside edge (across, between the rail's inside face and a pad's width past the outside one),
                // its underside a pad's depth under the rail's underside.
                const int side = c.restLocal[wh.inner].z > 0 ? 1 : -1;
                const double x = c.jackPadX(k);
                const V3 ob = c.railPoint(side, x, 1), ib = c.railPoint(side, x, 0);
                V3 o = ob - ib;
                const double w = len(o);
                o = o / w;
                const double lat = dot(point - ib, o), drop = dot(ob - point, fr.up);
                const bool ok = lat > 0 && lat < w + 0.06 && std::fabs(dot(point - ob, fr.fwd)) < 0.02;
                onRail += ok;
                under += drop > 0.005 && drop < 0.04;
                if (!ok) detail += " wheel " + std::to_string(k) + " is off its rail (" + std::to_string(lat) + " across, " + std::to_string(w) + " wide);";
            } else {
                under += point.y < hub.y - 0.03 && point.y > spot.y + 0.05;
                onRail++;
            }
            const Placed jack = Interact::jackModel(ctx.sim, c, k);
            handle += std::fabs(dot(jack.ax[0], c.jackOut(k))) > 0.99 && jack.p.x == jack.p.x;
            // Its own side, outside the middle third (the old rule: it is not under the middle of the car).
            const double lat2 = dot(point - fr.com, fr.left), hubLat = dot(hub - fr.com, fr.left);
            other += std::fabs(lat2) > track / 6 && lat2 * hubLat > 0;
        }
        const int n = (int)c.wheels.size();
        ctx.check(who + ": each jack point is by its wheel (within 1.2 m)", near == n, std::to_string(near) + " of " + std::to_string(n));
        ctx.check(who + ": ...outside the wheel well (clear of the tire's footprint)", clear == n, std::to_string(clear) + " of " + std::to_string(n));
        ctx.check(who + ": ...on the frame rail's jack pad" + (rail ? "" : " (a unibody: n/a)"), onRail == n, detail + std::to_string(onRail) + " of " + std::to_string(n));
        ctx.check(who + ": ...the saddle under the rail (a pad's depth)" + (rail ? "" : " / under the axle or arm"), under == n, std::to_string(under) + " of " + std::to_string(n));
        ctx.check(who + ": ...on its own side, outside the middle third", other == n, std::to_string(other) + " of " + std::to_string(n));
        ctx.check(who + ": ...the jack stands on the ground below it", ground == n, std::to_string(ground) + " of " + std::to_string(n));
        ctx.check(who + ": ...the handle along the way out", handle == n, std::to_string(handle) + " of " + std::to_string(n));
    }
}

SCENARIO(tireJackLifts, "tires/jack-lifts", "Every listed vehicle, every corner: worked up, the jack lifts that wheel's tire at least 5 cm off the ground, and the other three stay down") {
    const std::vector<int>& list = listedVehicles();
    std::vector<int> cars;
    for (int i = 0; i < (int)list.size(); i++) cars.push_back((int)(&ctx.add(vehicleCatalog()[list[i]], -200 + 25 * i, -50, 0) - ctx.sim.cars.data()));
    for (int ci : cars) ctx.sim.cars[ci].handbrake = 1;
    ctx.run(1.5);
    const World& w = ctx.sim.world;
    for (int k = 0; k < 4; k++) {
        for (int ci : cars) ctx.sim.cars[ci].placeJack(w, k);
        // Worked up at the game's rate until the corner's clear (Interact::cornerLifted), then held a moment.
        ctx.runUntil(6, [&] {
            bool all = true;
            for (int ci : cars) all = all && Interact::cornerLifted(ctx.sim, ctx.sim.cars[ci], k);
            return all;
        }, [&] {
            for (int ci : cars) {
                Car& c = ctx.sim.cars[ci];
                if (!Interact::cornerLifted(ctx.sim, c, k)) c.jackHeight = std::min(Interact::kJackMax, c.jackHeight + 0.14 * Ctx::kFrame);
                c.wake();
            }
        });
        ctx.run(1.0);
        for (int ci : cars) {
            const Car& c = ctx.sim.cars[ci];
            const Wheel& wh = c.wheels[k];
            const V3 hub = hubOf(c, k);
            const double clear = hub.y - wh.R - w.height(hub.x, hub.z);
            const std::string tag = std::string(c.spec->id) + " corner " + std::to_string(k);
            ctx.check(tag + ": the tire is 5 cm off the ground", clear >= 0.05, std::to_string(clear) + " m (jack " + std::to_string(c.jackHeight) + ")");
            double other = 1e9;
            for (int j = 0; j < (int)c.wheels.size(); j++) {
                if (j == k) continue;
                const V3 hj = hubOf(c, j);
                other = std::min(other, hj.y - c.wheels[j].R - w.height(hj.x, hj.z));
            }
            ctx.check(tag + ": the others stay on the ground", other < 0.04, std::to_string(other));
        }
        for (int ci : cars) {
            Car& c = ctx.sim.cars[ci];
            c.jackPlaced = false, c.jackHeight = 0, c.jackWheel = -1;
        }
        ctx.run(1.5);
    }
}

// The first-person work (handwork.h): the tool's socket is coaxial with the stud, and the handle swings an arc about it.
SCENARIO(tireWrenchAxis, "tires/wrench-axis", "The wrench, ratchet and spanner at a corner have their working axis along the axle, the lug wrench swings 60 to 90 degrees about the stud, no hand is drawn") {
    Car& c = ctx.add(vehicleCatalog()[7], -100, -50, 0);  // (the Blazer)
    ctx.run(1.0);
    const int k = 2;
    const V3 axle = norm(c.nodes[c.wheels[k].outer].p - c.nodes[c.wheels[k].inner].p);
    Walker wk;
    Interact ia;
    const V3 hub = hubOf(c, k);
    const V3 eye = hub + axle * 1.0 + V3(0, 0.6, 0);
    wk.active = true;
    auto frame = [&](double progress, const char* id, V3* origin = nullptr) {
        const Handwork h = handwork(ctx.sim, wk, ia, eye, hub - eye, 0);
        (void)progress;
        for (const Placed& p : h.models)
            if (p.id == "fp_hand") return V3(1e9, 0, 0);
        for (const Placed& p : h.models)
            if (p.id == id) {
                if (origin) *origin = p.p;
                return p.ax[1];
            }
        return V3(0, 0, 0);
    };
    // The lug wrench round the nuts: sampled across the first nut's visit (0.1 s apart).
    ia.current.kind = IA_REMOVE_WHEEL, ia.current.car = (int)(&c - ctx.sim.cars.data()), ia.current.wheel = k, ia.current.hold = 5.8;
    int coaxial = 0, total = 0, noHand = 0;
    double lo = 1e9, hi = -1e9;
    V3 ref;
    for (double s = 0.05; s < 5.0; s += 0.05) {
        ia.progress = s;
        V3 origin;
        const V3 up = frame(s, "lug_wrench", &origin);
        total++;
        coaxial += std::fabs(dot(up, axle)) > 0.999;
        noHand += up.x < 1e8;
        // The handle's direction about the axle, as an angle to the first sample's, over the first nut (about 1 s).
        if (s < 1.0) {
            const Handwork h = handwork(ctx.sim, wk, ia, eye, hub - eye, 0);
            for (const Placed& p : h.models)
                if (p.id == "lug_wrench") {
                    const V3 d = p.ax[0];
                    if (s < 0.06) ref = d;
                    const double ang = std::atan2(dot(cross(ref, d), axle), dot(ref, d));
                    lo = std::min(lo, ang), hi = std::max(hi, ang);
                }
        }
    }
    ctx.check("the lug wrench's socket axis is along the axle at every moment", coaxial == total && total > 50, std::to_string(coaxial) + " of " + std::to_string(total));
    ctx.check("no hand is drawn during the wheel job", noHand == total);
    ctx.metric("handle swing about the stud", (hi - lo) * 180 / M_PI, "deg", 60, 90);
    // The socket set and spanners in a repair.
    ia.current.kind = IA_FIX;
    ia.current.fix = Fix{Fix::CV_AXLE};
    ia.current.fix.part = PART_CV_AXLE;
    ia.current.fix.wheel = k;
    ia.current.hold = 4.5;
    int fixCoaxial = 0, fixTotal = 0;
    for (int tool : {TOOL_RATCHET, TOOL_WRENCH}) {  // (a repair's step: one tool for the whole hold)
        ia.current.tool = tool;
        for (double s = 0.1; s < 3.4; s += 0.2) {
            ia.progress = s;
            const V3 up = frame(s, tool == TOOL_RATCHET ? "ratchet" : "wrench");
            fixTotal++;
            fixCoaxial += std::fabs(dot(up, axle)) > 0.999;
        }
    }
    ctx.check("the ratchet and the spanner work along the axle", fixCoaxial == fixTotal && fixTotal > 20, std::to_string(fixCoaxial) + " of " + std::to_string(fixTotal));
}
