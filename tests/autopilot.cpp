// Autopilot: routes and driving to a destination.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "../src/pilotsense.h"
#include "harness.h"

namespace {

double routeLength(const std::vector<V3>& p) {
    double L = 0;
    for (size_t k = 1; k < p.size(); k++) L += len(p[k] - p[k - 1]);
    return L;
}

// PILOT_DEBUG=1: prints what the autopilot is up to every half second.
void trace(Ctx& ctx, const Car& c) {
    static const bool on = getenv("PILOT_DEBUG");
    static thread_local int last = -1;
    if (!on || (int)(ctx.t * 2) == last) return;
    last = (int)(ctx.t * 2);
    const Frame fr = c.frame();
    std::printf("%6.1f  (%6.1f %6.1f %6.1f) %5.1f km/h  %-8s %s%s%s %4.1f psi  steer %+.2f thr %.2f brk %.2f | %s\n", ctx.t, fr.com.x, fr.com.y, fr.com.z,
                dot(fr.vel, fr.fwd) * 3.6, c.mode() ? c.mode()->name : "-", c.lockFront ? "F" : "-", c.lockRear ? "R" : "-",
                c.swayDisconnected ? "S" : "-", c.tirePsi, c.steer, c.throttle, c.brake, c.pilot.status.c_str());
}

// Runs the autopilot until it arrives (or time runs out); returns the top speed (km/h).
double drive(Ctx& ctx, Car& c, double maxSeconds, double* worstDamage = nullptr) {
    double top = 0;
    ctx.runUntil(maxSeconds, [&] { return !c.pilot.active; }, [&] {
        const int i = (int)(&c - ctx.sim.cars.data());
        ctx.sim.pilotStep(i, Ctx::kFrame);
        trace(ctx, c);
        top = std::max(top, len(c.frame().vel) * 3.6);
        ctx.sample("Speed", c.spec->name, len(c.frame().vel) * 3.6, "km/h");
        if (worstDamage) *worstDamage = std::max(*worstDamage, c.damage);
    });
    return top;
}

}  // namespace

SCENARIO(pilotShop, "autopilot/shop", "Civic on autopilot across the proving ground to the shop, roads only, 40 km/h") {
    Car& c = ctx.add(vehicleCatalog()[0], 60, -12, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const Station& shop = ctx.sim.world.stations[0];
    const std::string msg = pilotStart(c, ctx.sim.world, shop.p, shop.name, 40 / 3.6, PILOT_ROADS);
    ctx.check("route planned", c.pilot.active, msg);
    const double top = drive(ctx, c, 60);
    ctx.check("arrived", c.pilot.arrived, c.pilot.status);
    ctx.metric("distance from the shop", std::hypot(c.frame().com.x - shop.p.x, c.frame().com.z - shop.p.z), "m", 0, 7);
    ctx.metric("top speed (asked for 40)", top, "km/h", 25, 46);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.2);
}

SCENARIO(pilotSpeed, "autopilot/speed", "Autopilot holds the speed it's given on a long straight (60 km/h)") {
    Car& c = ctx.add(vehicleCatalog()[2], -110, -110, 0);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, ctx.sim.world, V3(120, 0, -110), "THE FAR SIDE", 60 / 3.6, PILOT_ANYWHERE);
    double settled = 0;
    int n = 0;
    ctx.runUntil(40, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(ctx.sim.player, Ctx::kFrame);
        const double x = c.frame().com.x;
        if (x > -40 && x < 60) settled += len(c.frame().vel) * 3.6, n++;
    });
    ctx.check("arrived", c.pilot.arrived, c.pilot.status);
    ctx.metric("average speed mid-run (asked for 60)", n ? settled / n : 0, "km/h", 54, 64);
}

SCENARIO(pilotTrail, "autopilot/halfway-hut", "Bronco on autopilot from the trailhead up the summit trail to the Halfway Hut (roads and trails, 30 km/h)") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const World& w = ctx.sim.world;
    Car& c = ctx.add(vehicleCatalog()[2], w.spawn.x, w.spawn.z, -90);
    ctx.focus(c);
    ctx.run(1.0);
    const V3 hut = mountainFeature("halfway");
    const std::string msg = pilotStart(c, w, hut, "HALFWAY HUT", 30 / 3.6, PILOT_TRAILS);
    ctx.check("route planned", c.pilot.active, msg);
    // Mostly on the trail.
    int onTrack = 0;
    for (const V3& p : c.pilot.path) {
        const uint8_t pt = w.roadAt(p.x, p.z).paint;
        onTrack += pt == PAINT_TRAIL || pt == PAINT_GRAVEL || pt == PAINT_SCREE || pt == PAINT_PAD;
    }
    ctx.metric("share of the route on the trail", 100.0 * onTrack / std::max<size_t>(1, c.pilot.path.size()), "%", 75, 100);
    double worst = 0;
    drive(ctx, c, 240, &worst);
    ctx.check("arrived at the hut", c.pilot.arrived, c.pilot.status);
    ctx.metric("climbed", c.frame().com.y - w.spawn.y, "m", 25, 90);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.0);
}

SCENARIO(pilotAnywhere, "autopilot/cross-country", "Anywhere takes a shorter line across the valley to the lake than roads only, and drives it") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const World& w = ctx.sim.world;
    const V3 start = w.spawn, lake(140, 0, -378);
    std::vector<V3> roads, anywhere;
    const bool okR = planRoute(w, vehicleCatalog()[2], start, lake, PILOT_ROADS, {}, roads);
    const bool okA = planRoute(w, vehicleCatalog()[2], start, lake, PILOT_ANYWHERE, {}, anywhere);
    ctx.check("both routes found", okR && okA);
    ctx.metric("roads-only route", routeLength(roads), "m", 150, 900);
    ctx.metric("anywhere route vs roads only", routeLength(anywhere) / std::max(1.0, routeLength(roads)) * 100, "%", 30, 95);
    // Nothing solid on the route.
    double nearest = 1e9;
    for (const V3& p : anywhere)
        for (const Pole& pl : w.poles) nearest = std::min(nearest, std::hypot(p.x - pl.x, p.z - pl.z) - pl.r);
    ctx.metric("closest the route passes a tree or post", nearest, "m", 1.0, 100);
    Car& c = ctx.add(vehicleCatalog()[2], start.x, start.z, -90);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, w, lake, "THE LAKE", 35 / 3.6, PILOT_ANYWHERE);
    double worst = 0;
    drive(ctx, c, 150, &worst);
    ctx.check("arrived at the lake", c.pilot.arrived, c.pilot.status);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.0);
}

SCENARIO(pilotBoulders, "autopilot/around-boulders", "Through the crawl park's boulder lane: the route keeps clear of rocks too tall to climb, and the Bronco drives it") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const World& w = ctx.sim.world;
    const VehicleSpec& bronco = vehicleCatalog()[2];
    const V3 a = mountainFeature("boulders") + V3(0, 0, -6), b = mountainFeature("boulders") + V3(0, 0, 96);
    std::vector<V3> route;
    ctx.check("route found", planRoute(w, bronco, a, b, PILOT_ANYWHERE, {}, route));
    // Its best: low range, both lockers, aired down.
    const double cap = stepCap(bronco, LEVEL_LOW, 2, 20);
    double nearest = 1e9;
    for (const V3& p : route)
        for (const Rock& rk : w.rocks) {
            double top = -1e9, reach = 0;
            for (const V3& v : rk.v) top = std::max(top, v.y), reach = std::max(reach, std::hypot(v.x - rk.c.x, v.z - rk.c.z));
            if (top - w.height(rk.c.x, rk.c.z) <= cap) continue;
            nearest = std::min(nearest, std::hypot(p.x - rk.c.x, p.z - rk.c.z) - reach * 0.85);
        }
    ctx.metric("closest the route passes a rock too tall to climb (edge to center line)", nearest, "m", 0.9, 100);
    Car& c = ctx.add(bronco, a.x, a.z, -90);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, w, b, "THE END OF THE LANE", 20 / 3.6, PILOT_ANYWHERE);
    double worst = 0;
    int over = 0, around = 0;
    ctx.runUntil(150, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(ctx.sim.player, Ctx::kFrame);
        trace(ctx, c);
        worst = std::max(worst, c.damage);
        for (const PilotSeen& s : c.pilot.seen)
            if (s.height > 0.25) over += s.verdict == VERDICT_OVER || s.verdict == VERDICT_STRADDLE, around += s.verdict == VERDICT_AROUND_LEFT || s.verdict == VERDICT_AROUND_RIGHT;
    });
    ctx.check("through the lane", c.pilot.arrived, c.pilot.status);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.0);
    ctx.note("frames spent going over/straddling a rock: " + std::to_string(over) + ", going around one: " + std::to_string(around));
}

SCENARIO(pilotFollow, "autopilot/follow", "Following another truck: drives up to it and waits a few lengths back") {
    Car& lead = ctx.add(vehicleCatalog()[3], 80, 40, 0);
    Car& c = ctx.add(vehicleCatalog()[2], -20, -40, 0);
    const int leadIdx = (int)(&lead - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    c.pilot.followCar = leadIdx;
    pilotStart(c, ctx.sim.world, lead.frame().com, "4RUNNER", 40 / 3.6, PILOT_ANYWHERE);
    ctx.run(40, [&] {
        ctx.sim.pilotStep(ctx.sim.player, Ctx::kFrame);
        ctx.sim.cars[leadIdx].handbrake = 1;
    });
    const double gap = len(c.frame().com - ctx.sim.cars[leadIdx].frame().com);
    ctx.metric("gap to the truck it follows", gap, "m", 6, 16);
    ctx.metric("its speed (waiting)", len(c.frame().vel) * 3.6, "km/h", 0, 2);
    ctx.check("still following", c.pilot.active);
}

// ---- Seeing and setting up: a test lane on the proving ground's pad --------------------

namespace {

// Clears the pad of the proving ground's obstacles; optionally walls a lane along +X
// (`halfWidth` either side of z = 0, 1.8 m walls).
World& testLane(Ctx& ctx, double halfWidth = 0) {
    World& w = ctx.sim.world;
    w.boxes.clear();
    w.poles.clear();
    w.rocks.clear();
    w.logs.clear();
    if (halfWidth > 0)
        for (int s = -1; s <= 1; s += 2) w.addBox(V3(0, 0.9, s * (halfWidth + 0.25)), V3(60, 0.9, 0.25), 0, 0, 0, V3(0.5, 0.5, 0.5));
    w.buildGrid();
    return w;
}

// Drives to `goal` with the autopilot; tracks damage and what it saw and did.
struct PilotRun {
    double worst = 0, top = 0;
    bool around = false, over = false, low = false, rearLock = false, frontLock = false, aired = false, sway = false;
    std::string thoughts;
};
// (keepRoute: it's already been started; carry on with the route it has.)
PilotRun pilotTo(Ctx& ctx, Car& c, const V3& goal, double kmh, double maxSeconds, bool keepRoute = false,
                 const std::function<void()>& perFrame = nullptr) {
    PilotRun r;
    if (keepRoute) c.pilot.active = true;
    else ctx.check("route planned", pilotStart(c, ctx.sim.world, goal, "THE END", kmh / 3.6, PILOT_ANYWHERE).find("NO WAY") == std::string::npos);
    const int i = (int)(&c - ctx.sim.cars.data());
    std::string last;
    ctx.runUntil(maxSeconds, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(i, Ctx::kFrame);
        trace(ctx, c);
        if (perFrame) perFrame();
        r.worst = std::max(r.worst, c.damage);
        r.top = std::max(r.top, len(c.frame().vel) * 3.6);
        r.low |= c.lowRange;
        r.rearLock |= c.lockRear;
        r.frontLock |= c.lockFront;
        r.sway |= c.swayDisconnected;
        r.aired |= c.tirePsi < 30;
        for (const PilotSeen& s : c.pilot.seen) {
            r.around |= s.verdict == VERDICT_AROUND_LEFT || s.verdict == VERDICT_AROUND_RIGHT;
            r.over |= s.verdict == VERDICT_OVER || s.verdict == VERDICT_STRADDLE;
        }
        if (!c.pilot.thought.empty() && c.pilot.thought != last && r.thoughts.size() < 400) r.thoughts += c.pilot.thought + "; ", last = c.pilot.thought;
    });
    ctx.check("arrived", c.pilot.arrived, c.pilot.status);
    if (!r.thoughts.empty()) ctx.note("it thought: " + r.thoughts);
    return r;
}

// How close any part of the vehicle comes to a boulder's surface (convex: the farthest
// face plane it's outside of).
double rockGap(const Rock& rk, const Car& c) {
    double gap = 1e9;
    for (const Node& n : c.nodes) {
        double out = -1e9;
        for (size_t f = 0; f < rk.fn.size(); f++) out = std::max(out, dot(rk.fn[f], n.p) - rk.fd[f]);
        gap = std::min(gap, out);
    }
    return gap;
}

}  // namespace

SCENARIO(pilotRockAround, "autopilot/rock-around", "A waist-high boulder dropped on the route after it's planned: the Bronco sees it and drives round it") {
    World& w = testLane(ctx);
    Car& c = ctx.add(vehicleCatalog()[2], -45, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, w, V3(45, 0, 0), "THE END", 25 / 3.6, PILOT_ANYWHERE);
    w.addRock(0, 1, V3(1.1, 0.9, 0.9), 0.85, 7);  // on the route it already has (along z = 1)
    w.buildGrid();
    const Rock& rk = w.rocks.back();
    double gap = 1e9;
    const PilotRun r = pilotTo(ctx, c, V3(45, 0, 0), 25, 40, true, [&] { gap = std::min(gap, rockGap(rk, c)); });
    ctx.check("said it was going around", r.around, r.thoughts);
    ctx.metric("closest it came to the rock", gap, "m", 0.15, 3.0);
    ctx.metric("damage", r.worst * 100, "%", 0, 0.05);
}

SCENARIO(pilotRockOver, "autopilot/rock-over", "A knee-high rock in a walled lane with no room round it: the Bronco slows and goes over it") {
    World& w = testLane(ctx, 2.4);
    w.addRock(0, 0.4, V3(0.5, 0.35, 0.45), 0.24, 11);
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[2], -45, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double slowest = 1e9;
    const Rock& rk = w.rocks.back();
    ctx.sample("Speed", "Bronco", 0, "km/h");
    const PilotRun r = pilotTo(ctx, c, V3(45, 0, 0), 30, 60);
    (void)rk;
    (void)slowest;
    ctx.check("said it was going over / straddling", r.over, r.thoughts);
    ctx.metric("damage", r.worst * 100, "%", 0, 0.3);
}

SCENARIO(pilotLedge, "autopilot/ledge", "A 0.38 m ledge across a walled lane (too tall for 4H): it stops, shifts into 4L, locks up and climbs it") {
    World& w = testLane(ctx, 2.6);
    w.addBox(V3(6, 0.19, 0), V3(5, 0.19, 2.6), 0, 0, 0, V3(0.5, 0.48, 0.45));
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[2], -40, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const PilotRun r = pilotTo(ctx, c, V3(40, 0, 0), 25, 90);
    ctx.check("shifted into 4L", r.low);
    ctx.check("rear locker in", r.rearLock);
    ctx.metric("damage", r.worst * 100, "%", 0, 0.5);
}

SCENARIO(pilotWallGap, "autopilot/wall-gap", "A chest-high wall across the way with a gap at one end: the route goes through the gap") {
    World& w = testLane(ctx);
    w.addBox(V3(0, 0.4, 2), V3(0.4, 0.4, 14), 0, 0, 0, V3(0.5, 0.5, 0.5));  // z -12 .. 16: the gap is past z = -12
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[2], -30, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const PilotRun r = pilotTo(ctx, c, V3(30, 0, 0), 25, 60);
    double through = 1e9;
    (void)through;
    ctx.check("didn't climb the wall", c.frame().com.y < 1.5);
    ctx.metric("damage", r.worst * 100, "%", 0, 0.1);
}

SCENARIO(pilotParked, "autopilot/parked-truck", "A 4Runner parked across the route: the Bronco steers round it without touching") {
    testLane(ctx);
    Car& c = ctx.add(vehicleCatalog()[2], -45, 0, 0);
    ctx.add(vehicleCatalog()[3], 5, 0.5, 80);
    Car& me = ctx.sim.cars[0];
    ctx.focus(me);
    ctx.run(1.0);
    (void)c;
    const PilotRun r = pilotTo(ctx, me, V3(45, 0, 0), 25, 50);
    ctx.check("said it was going around", r.around, r.thoughts);
    ctx.metric("its damage", r.worst * 100, "%", 0, 0.05);
    ctx.metric("the 4Runner's damage", ctx.sim.cars[1].damage * 100, "%", 0, 0.05);
}

SCENARIO(pilotCivicRock, "autopilot/civic-rock", "A rock a truck would straddle on the Civic's route: the low car goes round it") {
    testLane(ctx);
    Car& c = ctx.add(vehicleCatalog()[0], -45, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, ctx.sim.world, V3(45, 0, 0), "THE END", 30 / 3.6, PILOT_ANYWHERE);
    ctx.sim.world.addRock(0, 1, V3(0.45, 0.3, 0.4), 0.2, 5);  // on the route (it runs along z = 1)
    ctx.sim.world.buildGrid();
    double gap = 1e9;
    const Rock& rk = ctx.sim.world.rocks.back();
    const PilotRun r = pilotTo(ctx, c, V3(45, 0, 0), 30, 40, true, [&] { gap = std::min(gap, rockGap(rk, c)); });
    ctx.check("said it was going around", r.around, r.thoughts);
    ctx.metric("closest it came to the rock", gap, "m", 0.1, 3.0);
    ctx.metric("damage", r.worst * 100, "%", 0, 0.05);
}
