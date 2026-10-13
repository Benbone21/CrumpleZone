// Surfaces: how each one grips, slides, sinks and drags, on the proving ground's surface
// course (six 70 m strips: wet asphalt, gravel, sand, mud, snow, ice).
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/autopilot.h"
#include "harness.h"

namespace {

const VehicleSpec& civic() { return vehicleCatalog()[0]; }
const VehicleSpec& jesko() { return vehicleCatalog()[1]; }
const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

// A vehicle at the west end of a strip (or on the pad beside the course for pavement),
// facing east along it, settled.
Car& onStrip(Ctx& ctx, const VehicleSpec& spec, Surface s, double ahead = 2) {
    ctx.sim.cars.clear();  // one at a time: the last one is parked down the strip
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    const V3 p = s == SURF_PAVED ? V3(-110, 0, 40) : ctx.sim.world.courseStart(s) + V3(6 + ahead, 0, 0);
    Car& c = ctx.add(spec, p.x, p.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    return c;
}

// Holds the car straight along its strip (z of where it started).
void holdLine(Car& c, double z) {
    const Frame f = c.frame();
    const V3 d = V3(f.com.x + 8, f.com.y, z) - f.com;
    c.steer = clampd(std::atan2(dot(d, f.left), dot(d, f.fwd)) * 1.2, -0.3, 0.3);
}

// Stopping distance from `kmh` with the brakes hard on (ABS, or locked with it off).
double brakeOn(Ctx& ctx, const VehicleSpec& spec, Surface s, double kmh, bool abs = true) {
    VehicleSpec v = spec;
    v.absEnabled = abs;
    static std::vector<VehicleSpec> keep;  // (the car points at its spec)
    keep.reserve(64);
    keep.push_back(v);
    Car& c = onStrip(ctx, keep.back(), s, 1);
    const double z = c.frame().com.z;
    ctx.launch(c, kmh / 3.6);
    const double x0 = c.frame().com.x;
    ctx.runUntil(30, [&] { return len(c.frame().vel) < 0.3; }, [&] {
        c.throttle = 0, c.brake = 1, c.handbrake = 0;
        holdLine(c, z);
    });
    return c.frame().com.x - x0;
}

}  // namespace

SCENARIO(surfBraking, "surfaces/braking", "Bronco (all-terrain tires, ABS) stopping from 40 km/h on each surface: pavement shortest, ice ~8x as far") {
    struct Exp {
        Surface s;
        double lo, hi;
    };
    // 40 km/h = 11.1 m/s; d = v^2 / (2 mu g): mu 0.9 -> 7 m, 0.6 -> 10.5, 0.3 -> 21, 0.1 -> 63.
    const Exp exp[] = {{SURF_PAVED, 6, 10}, {SURF_WET, 8, 14}, {SURF_GRAVEL, 8, 16}, {SURF_SAND, 5, 15},
                       {SURF_MUD, 12, 26}, {SURF_SNOW, 13, 28}, {SURF_ICE, 40, 69}};
    double paved = 0, ice = 0;
    for (const Exp& e : exp) {
        const double d = brakeOn(ctx, bronco(), e.s, 40);
        std::printf("BRAKE %-12s %.1f m\n", surfaceInfo(e.s).name, d);
        ctx.metric(std::string("stopping distance on ") + surfaceInfo(e.s).name, d, "m", e.lo, e.hi);
        if (e.s == SURF_PAVED) paved = d;
        if (e.s == SURF_ICE) ice = d;
    }
    ctx.metric("ice vs pavement", ice / std::max(1.0, paved), "x", 5, 12);
}

SCENARIO(surfGravelLock, "surfaces/gravel-lockup", "Locked wheels: on pavement they slide far past an ABS stop; on gravel the stones pile up in front and it stops about as short") {
    const double padAbs = brakeOn(ctx, civic(), SURF_PAVED, 50, true), padLock = brakeOn(ctx, civic(), SURF_PAVED, 50, false);
    const double gravAbs = brakeOn(ctx, civic(), SURF_GRAVEL, 50, true), gravLock = brakeOn(ctx, civic(), SURF_GRAVEL, 50, false);
    std::printf("LOCK pad %.1f/%.1f gravel %.1f/%.1f\n", padAbs, padLock, gravAbs, gravLock);
    ctx.metric("pavement: locked vs ABS", padLock / padAbs, "x", 1.05, 1.6);
    ctx.metric("gravel: locked vs ABS", gravLock / gravAbs, "x", 0.7, 1.08);
}

SCENARIO(surfSand, "surfaces/sand", "Sand: tires sink and drag; aired down they float; spinning in place digs the truck in") {
    // Coasting from 30 km/h in neutral: the sand stops it (rolling drag), less aired down.
    auto coast = [&](double psi) {
        Car& c = onStrip(ctx, bronco(), SURF_SAND, 1);
        c.tirePsi = psi;
        const double z = c.frame().com.z;
        ctx.run(0.5);
        ctx.launch(c, 30 / 3.6);
        const double x0 = c.frame().com.x;
        double sink = 0;
        ctx.runUntil(20, [&] { return len(c.frame().vel) < 0.3; }, [&] {
            c.throttle = 0, c.brake = 0, c.handbrake = 0;
            holdLine(c, z);
            for (const Wheel& w : c.wheels) sink = std::max(sink, (double)w.sink);
        });
        return std::make_pair(c.frame().com.x - x0, sink);
    };
    const auto hi = coast(35), lo = coast(15);
    std::printf("SAND coast 35 psi %.1f m (sink %.3f), 15 psi %.1f m (sink %.3f)\n", hi.first, hi.second, lo.first, lo.second);
    ctx.metric("coasting from 30 km/h at 35 psi", hi.first, "m", 8, 40);
    ctx.metric("... aired down to 15 psi (floats: rolls further)", lo.first / hi.first, "x", 1.15, 3);
    // Floored in 2H from a stop: the rear tires spin and dig down.
    Car& c = onStrip(ctx, bronco(), SURF_SAND, 1);
    c.setDriveMode(0);
    c.tractionControl = false;
    double rest = 0, dug = 0;
    for (const Wheel& w : c.wheels) rest = std::max(rest, (double)w.sink);
    ctx.run(3, [&] {
        c.throttle = 1, c.brake = 0, c.handbrake = 0;
        for (const Wheel& w : c.wheels) dug = std::max(dug, (double)w.sink);
    });
    ctx.metric("parked: how far the tires settle", rest * 100, "cm", 2, 7);
    ctx.metric("floored in 2H: how far the spinning tires dig", dug * 100, "cm", 7, 12);
}

SCENARIO(surfMud, "surfaces/mud", "Mud: the Bronco's all-terrains pull through 50 m of it; the Jesko's semi-slicks barely move") {
    auto pull = [&](const VehicleSpec& spec) {
        Car& c = onStrip(ctx, spec, SURF_MUD, 1);
        const double z = c.frame().com.z, x0 = c.frame().com.x;
        double clog = 0;
        ctx.runUntil(15, [&] { return c.frame().com.x > x0 + 50; }, [&] {
            c.throttle = 1, c.brake = 0, c.handbrake = 0;
            holdLine(c, z);
            for (const Wheel& w : c.wheels) clog = std::max(clog, (double)w.clog);
        });
        std::printf("MUD %s %.1f m in %.1f s, clog %.2f\n", spec.name, c.frame().com.x - x0, ctx.t, clog);
        return std::make_pair(len(c.frame().vel) * 3.6, c.frame().com.x - x0);
    };
    const double t0 = ctx.t;
    const auto b = pull(bronco());
    const double tb = ctx.t - t0, t1 = ctx.t;
    const auto j = pull(jesko());
    const double tj = ctx.t - t1;
    ctx.metric("Bronco: time for 50 m from a stop", tb, "s", 4, 12);
    ctx.metric("Jesko: speed after 15 s (or the 50 m)", j.first, "km/h", 0, 60);
    ctx.check("the Jesko is far slower through it", tj > tb * 1.25 || j.second < 50, std::to_string(tj) + " s");
    (void)b;
}

SCENARIO(surfIce, "surfaces/ice", "Ice: the Civic at 40 km/h steering hard just slides on: about a tenth of a g sideways") {
    Car& c = onStrip(ctx, civic(), SURF_ICE, 1);
    ctx.launch(c, 40 / 3.6);
    double g = 0, smooth = 0;
    V3 prev = c.frame().vel;
    ctx.run(3, [&] {
        c.throttle = 0, c.brake = 0, c.steer = 0.3;
        const Frame f = c.frame();
        const V3 acc = (f.vel - prev) / Ctx::kFrame;
        prev = f.vel;
        const V3 side = norm(cross(V3(0, 1, 0), norm(f.vel + V3(1e-9, 0, 0))));
        smooth += (std::fabs(dot(acc, side)) / 9.81 - smooth) * 0.1;
        g = std::max(g, smooth);
    });
    // (Down to ~0.03 g: off the throttle in gear, a car without engine-drag control lets
    // engine braking drag its front tires on ice, and that grip goes to slowing, not turning.)
    ctx.metric("sideways grip", g, "g", 0.03, 0.16);
}

SCENARIO(surfPilotSand, "surfaces/autopilot-sand", "Autopilot down the sand strip: 4WD with the center locked, then airs down for a long stretch of it") {
    // A longer sand run: the whole strip and back is ~70 m; give it the strip end to end.
    World& w = ctx.sim.world;
    const V3 a = w.courseStart(SURF_SAND);
    Car& c = ctx.add(bronco(), a.x - 4, a.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const std::string msg = pilotStart(c, w, a + V3(76, 0, 0), "END OF THE SAND", 25 / 3.6, PILOT_ANYWHERE);
    ctx.check("route planned", c.pilot.active, msg);
    bool locked = false;
    ctx.runUntil(60, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(ctx.sim.player, Ctx::kFrame);
        locked |= c.centerLocked() && c.frontEngaged();
    });
    ctx.check("arrived", c.pilot.arrived, c.pilot.status);
    ctx.check("both axles pulling, tied together, in the sand", locked);
}

SCENARIO(surfPilotIce, "surfaces/autopilot-ice", "Autopilot on the ice strip asked for 80 km/h: keeps it slow and stops at the end without sliding past") {
    World& w = ctx.sim.world;
    const V3 a = w.courseStart(SURF_ICE);
    Car& c = ctx.add(civic(), a.x - 4, a.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, w, a + V3(70, 0, 0), "END OF THE ICE", 80 / 3.6, PILOT_ANYWHERE);
    double top = 0;
    ctx.runUntil(60, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(ctx.sim.player, Ctx::kFrame);
        if (w.surfaceAt(c.frame().com.x, c.frame().com.z) == SURF_ICE) top = std::max(top, len(c.frame().vel) * 3.6);
    });
    ctx.check("arrived", c.pilot.arrived, c.pilot.status);
    ctx.metric("top speed on the ice", top, "km/h", 5, 32);
    ctx.metric("stopped short of the end (the strip ends at +70 m)", c.frame().com.x - a.x, "m", 55, 72);
}

namespace {
// A Bronco dropped flat from `h` m (or landing a jump at `fwd` m/s, nose down `pitch`
// degrees) onto `s`: the pad's pavement, a granite slab laid on the pad, or the sand, mud
// or snow strip. Leaves it where it landed.
struct Landing {
    double damage = 0;  // %
    int popped = 0, broken = 0;
    double frameSet = 0;  // worst permanent set in a frame member (%: 1 is a visible kink)
    double peakG = 0;   // hardest deceleration of the body over a frame
    double sink = 0;    // deepest a tire went into the ground
    Car* car = nullptr;
};
Landing landOn(Ctx& ctx, Surface s, double h, double fwd = 0, double pitch = 0) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    World& w = ctx.sim.world;
    const V3 p = s == SURF_PAVED ? V3(-100, 0, 55) : s == SURF_ROCK ? V3(-100, 0, 30) : w.courseStart(s) + V3(20, 0, 0);
    if (s == SURF_ROCK) {  // a granite slab 30 cm proud of the pad, 100 m long
        bool laid = false;
        for (const Box& b : w.boxes) laid |= b.rock && std::fabs(b.c.x + 55) < 0.1 && std::fabs(b.c.z - 30) < 0.1;
        if (!laid) {
            w.addBox(V3(-55, w.height(-55, 30) + 0.15, 30), V3(50, 0.15, 6), 0, 0, 0, V3(0.45, 0.43, 0.4));
            w.boxes.back().rock = true;
            w.buildGrid();
        }
    }
    Car& c = ctx.add(bronco(), p.x, p.z, 0);
    ctx.focus(c);
    ctx.run(1.5, [&] { c.handbrake = 1; });
    const V3 ahead = norm(V3(c.frame().fwd.x, 0, c.frame().fwd.z));
    if (pitch != 0) c.rotateRigid(c.frame().left, pitch * M_PI / 180);  // nose down
    for (Node& n : c.nodes) n.p.y += h, n.v = ahead * fwd;
    for (Wheel& wh : c.wheels) wh.omega = fwd / wh.R;
    Landing r;
    V3 prev = c.frame().vel;
    ctx.run(fwd > 0 ? 2.0 : 3.0, [&] {
        c.throttle = 0, c.brake = fwd > 0 ? 0 : 1;
        const V3 v = c.frame().vel;
        r.peakG = std::max(r.peakG, len(v - prev) / Ctx::kFrame / 9.81);
        prev = v;
        for (const Wheel& wh : c.wheels) r.sink = std::max(r.sink, (double)wh.sink);
    });
    r.damage = c.damage * 100;
    for (const Wheel& wh : c.wheels) r.popped += wh.popped;
    for (const Beam& b : c.beams) {
        r.broken += b.broken;
        if (b.type == BEAM_FRAME) r.frameSet = std::max(r.frameSet, 100 * std::fabs(b.L0 - b.Lorig) / b.Lorig);
    }
    r.car = &c;
    return r;
}
}  // namespace

SCENARIO(surfSoftLanding, "surfaces/soft-landing", "Sand gives under a landing: a Bronco dropped 5-8 m or landing a jump nose-down takes far less on sand (and mud, snow) than on pavement or rock") {
    auto line = [](const char* what, Surface s, const Landing& r) {
        std::printf("LAND %-22s %-10s damage %5.2f %% popped %d broken %d frame set %4.1f %% peak %4.1f g sink %.0f cm\n", what,
                    surfaceInfo(s).name, r.damage, r.popped, r.broken, r.frameSet, r.peakG, r.sink * 100);
    };
    // Flat from 5 m (10 m/s): on hard ground the suspension bottoms and something gives: a
    // part breaks, or the frame kinks (a 1% set is a visible kink).
    const Landing rock5 = landOn(ctx, SURF_ROCK, 5), pad5 = landOn(ctx, SURF_PAVED, 5);
    line("5 m flat", SURF_ROCK, rock5), line("5 m flat", SURF_PAVED, pad5);
    const Landing mud5 = landOn(ctx, SURF_MUD, 5), snow5 = landOn(ctx, SURF_SNOW, 5);
    line("5 m flat", SURF_MUD, mud5), line("5 m flat", SURF_SNOW, snow5);
    const Landing sand5 = landOn(ctx, SURF_SAND, 5);
    line("5 m flat", SURF_SAND, sand5);
    ctx.check("5 m onto rock: something breaks or the frame kinks", (rock5.broken > 0 || rock5.frameSet > 1) && rock5.damage > 0.1,
              std::to_string(rock5.broken) + " beams, frame set " + std::to_string(rock5.frameSet) + " %");
    ctx.metric("5 m onto sand: frame set", sand5.frameSet, "%", 0, 1);
    ctx.metric("5 m onto sand: beams broken, vs rock", sand5.broken - rock5.broken, "", -100, 0);
    ctx.metric("5 m: damage on sand vs rock", sand5.damage / rock5.damage, "x", 0, 0.3);
    ctx.metric("5 m: damage on mud vs rock", mud5.damage / rock5.damage, "x", 0, 0.5);
    ctx.metric("5 m: damage on snow vs rock", snow5.damage / rock5.damage, "x", 0, 0.5);
    ctx.metric("5 m: hardest deceleration, sand vs pavement", sand5.peakG / pad5.peakG, "x", 0.3, 0.8);
    ctx.metric("5 m into sand: how deep the tires dig in", sand5.sink * 100, "cm", 12, 23);
    // It's in a crater, not stuck: 4 Lo and ease out (floored, it spins and digs in deeper).
    {
        Car& c = *sand5.car;
        c.setDriveMode((int)driveModes(c.spec->transfer).size() - 1);
        const V3 a = c.frame().com;
        const double t0 = ctx.t;
        ctx.runUntil(15, [&] { return len(c.frame().com - a) > 10; }, [&] {
            c.throttle = 0.4, c.brake = 0, c.handbrake = 0;
            holdLine(c, a.z);
        });
        ctx.metric("then eased out of the crater in 4 Lo: 10 m in", ctx.t - t0, "s", 1, 10);
    }
    // 8 m (12.5 m/s): rock pinches the tires on their rims; sand doesn't.
    const Landing rock8 = landOn(ctx, SURF_ROCK, 8), sand8 = landOn(ctx, SURF_SAND, 8);
    line("8 m flat", SURF_ROCK, rock8), line("8 m flat", SURF_SAND, sand8);
    ctx.check("8 m onto rock pops tires", rock8.popped > 0);
    ctx.metric("8 m onto sand: tires popped", sand8.popped, "", 0, 0);
    ctx.metric("8 m: damage on sand vs rock", sand8.damage / rock8.damage, "x", 0, 0.3);
    // A jump landed nose-down (35 degrees, 15 m/s, from 4 m): the bumper ploughs in.
    const Landing rockJ = landOn(ctx, SURF_ROCK, 4, 15, 35), sandJ = landOn(ctx, SURF_SAND, 4, 15, 35);
    line("jump, nose down 35 deg", SURF_ROCK, rockJ), line("jump, nose down 35 deg", SURF_SAND, sandJ);
    ctx.metric("nose-down jump: damage on sand vs rock", sandJ.damage / rockJ.damage, "x", 0, 0.6);
}

SCENARIO(surfRuts, "surfaces/ruts", "Three Broncos weave down the sand, mud and snow strips and park; then the one in the sand is floored against a block: its rear tires dig themselves in") {
    World& w = ctx.sim.world;
    const Surface ss[3] = {SURF_SAND, SURF_MUD, SURF_SNOW};
    double lane[3];
    for (int i = 0; i < 3; i++) {
        const V3 a = w.courseStart(ss[i]) + V3(14, 0, 0);
        ctx.add(bronco(), a.x, a.z, 0);
        lane[i] = a.z;
    }
    Car& sand = ctx.sim.cars[0];
    ctx.focus(sand);
    ctx.run(1.0);
    double parked[3] = {0, 0, 0};
    ctx.run(8.5, [&] {
        for (int i = 0; i < 3; i++) {
            Car& c = ctx.sim.cars[i];
            const bool go = ctx.t < 6.5;
            c.throttle = go ? 0.45 : 0, c.brake = go ? 0 : 1, c.handbrake = 0;
            holdLine(c, lane[i] + 1.2 * std::sin((ctx.t - 1.0) * 0.9 + i));
            if (!go) c.steer = 0;
        }
    });
    for (int i = 0; i < 3; i++)
        for (const Wheel& wh : ctx.sim.cars[i].wheels) parked[i] = std::max(parked[i], (double)wh.sink);
    // A block in front of the sand Bronco's bumper, then floored in 2H: the rears spin and dig in.
    {
        const Frame f = sand.frame();
        double nose = -1e9;
        for (int i = 0; i < sand.bodyNodes; i++) nose = std::max(nose, dot(sand.nodes[i].p - f.com, f.fwd));
        const V3 b = f.com + f.fwd * (nose + 0.3);
        w.addBox(V3(b.x, w.height(b.x, b.z) + 0.4, b.z), V3(0.25, 0.4, 1.3), std::atan2(-f.fwd.z, f.fwd.x), 0, 0, V3(0.5, 0.49, 0.47));
        w.buildGrid();
    }
    sand.setDriveMode(0);
    sand.tractionControl = false;
    double dug = 0;
    ctx.run(3.0, [&] {
        sand.throttle = 1, sand.brake = 0, sand.handbrake = 0, sand.steer = 0;
        for (const Wheel& wh : sand.wheels) dug = std::max(dug, (double)wh.sink);
    });
    ctx.run(3.0, [&] { sand.throttle = 0, sand.brake = 1; });  // (and the dust settles)
    double rear = 0;
    for (const Wheel& wh : sand.wheels)
        if (!wh.front) rear = std::max(rear, (double)wh.sink);
    for (int i = 0; i < 3; i++)
        ctx.metric(std::string("parked in the ") + surfaceInfo(ss[i]).name + ": tires sunk in", parked[i] * 100, "cm", 2, 9);
    ctx.metric("spinning against the block: the rears dig down", dug * 100, "cm", 7, 12);
    ctx.metric("... and stay down there once it stops", rear * 100, "cm", 7, 12);
}
