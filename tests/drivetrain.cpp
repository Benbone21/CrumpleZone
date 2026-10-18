// Driver-visible gear controls and coast-down regressions, using the real drivetrain.
#include "harness.h"
#include "../src/gamepad.h"
#include "../src/mods.h"

VEHICLE_SCENARIO(manualNeutral, "drivetrain/manual-neutral", "Manual R-N-1 selection, free revs, pedal release and independent brake controls") {
    Car& c = ctx.add(spec, -60, -50, 0);
    ctx.focus(c);
    c.manualShift = true;
    ctx.run(1);
    ctx.check("Q from first selects neutral", c.shift(-1) && c.gear == 0);
    const V3 start = c.frame().com;
    ctx.run(1.5, [&] { ctx.sim.drive(true, false, false, false, false, Ctx::kFrame); });
    ctx.check("gas preserves neutral", c.gear == 0);
    ctx.metric("engine revs freely in neutral", c.rpm - spec.idleRpm, "rpm", 2500, spec.redline);
    ctx.metric("neutral sends no propulsion to the wheels", len(c.frame().com - start), "m", 0, 0.15);
    ctx.run(2, [&] { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); });
    ctx.metric("released pedal returns to idle", std::fabs(c.rpm - spec.idleRpm), "rpm", 0, 10);
    ctx.check("Q from neutral selects reverse", c.shift(-1) && c.gear == -1);
    ctx.run(spec.shiftTime + 0.1);
    ctx.sim.drive(true, false, false, false, false, Ctx::kFrame);
    ctx.check("manual reverse: W is gas, gear stays reverse", c.throttle == 1 && c.brake == 0 && c.gear == -1);
    ctx.sim.drive(false, true, false, false, false, Ctx::kFrame);
    ctx.check("manual reverse: S brakes without selecting forward", c.throttle == 0 && c.brake == 1 && c.gear == -1);
    const auto p = pad::pedals(0.7, 0.2, -3, -1, true);
    ctx.check("manual controller pedals preserve reverse", p.gear == -1 && p.throttle == 0.7 && p.brake == 0.2);
    ctx.check("E from reverse selects neutral", c.shift(1) && c.gear == 0);
    ctx.run(spec.shiftTime + 0.1);
    // (Rolling at 10 m/s, or slower where first gear can't take it: a granny-geared truck box
    // tops out first well under 36 km/h.)
    const double firstTop = 0.6 * spec.redline * 2 * M_PI / 60 / (spec.gears[0] * spec.finalDrive) * spec.wheelRadius;
    ctx.launch(c, std::min(10.0, firstTop));
    c.gear = 0;  // launch() normally picks a suitable forward gear for its speed
    ctx.run(0.05, [&] { c.throttle = c.brake = c.handbrake = 0; });
    ctx.check("reverse cannot engage while rolling at speed", !c.shift(-1) && c.gear == 0);
    ctx.check("E from neutral selects first", c.shift(1) && c.gear == 1);
    ctx.run(spec.shiftTime + 0.1);
    ctx.launch(c, 50);
    c.gear = 0;
    ctx.run(0.05, [&] { c.throttle = c.brake = c.handbrake = 0; });
    ctx.check("neutral cannot select first at an over-revving road speed", !c.shift(1) && c.gear == 0);
}

VEHICLE_SCENARIO(reverseRelease, "drivetrain/reverse-release", "Reverse drive then pedal release: assist must not sustain acceleration or rev limiting") {
    Car& c = ctx.add(spec, -50, -55, 0);
    ctx.focus(c);
    ctx.run(1);
    c.gear = -1;
    ctx.run(3, [&] { c.throttle = 0.6; c.brake = c.handbrake = 0; });
    // Tire/shaft energy already stored under power can accelerate the body briefly
    // as wheelspin settles; judge sustained drive after that transient.
    ctx.run(0.35, [&] { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); });
    const double startSpeed = -dot(c.frame().vel, c.frame().fwd);
    double peak = startSpeed;
    ctx.check("vehicle is moving backwards", startSpeed > 1);
    ctx.run(5, [&] {
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        peak = std::max(peak, -dot(c.frame().vel, c.frame().fwd));
    });
    ctx.metric("no reverse acceleration after pedal release", peak - startSpeed, "m/s", 0, 0.6);
    ctx.metric("speed falls after throttle release", -dot(c.frame().vel, c.frame().fwd) / startSpeed, "share", 0, 0.98);
    ctx.metric("RPM leaves the rev limiter", c.rpm / spec.redline, "share", 0, 0.85);
    ctx.metric("drag assist closes", c.dragAssist, "throttle", 0, 0.05);
}

SCENARIO(shiftMatch, "drivetrain/shift-match", "An upshift opens the clutch and matches RPM over the shift instead of snapping in one physics step") {
    Car& c = ctx.add(vehicleCatalog()[0], -80, -50, 0);
    ctx.focus(c); c.manualShift = true;
    ctx.run(1);
    ctx.launch(c, 8);
    ctx.run(0.1, [&] { c.throttle = 0.3; });
    const double before = c.rpm;
    ctx.check("upshift begins", c.shift(1) && c.gear == 2);
    ctx.sim.substep();
    ctx.metric("RPM change in the first 0.5 ms", std::fabs(c.rpm - before), "rpm", 0, 5);
    ctx.run(c.spec->shiftTime + 0.2, [&] { c.throttle = 0; });
    double omega = 0; int n = 0;
    for (const Wheel& wh : c.wheels) if (c.wheelPowered(wh)) { omega += wh.sideOmega; n++; }
    const double inputRpm = omega / n * c.spec->gears[c.gear - 1] * c.spec->finalDrive * 60 / (2 * M_PI);
    ctx.metric("RPM matches the selected ratio after clutch closure", std::fabs(c.rpm - inputRpm), "rpm", 0, 150);
    ctx.check("manual selection survives coast-down", c.gear == 2);
}

SCENARIO(converterShift, "drivetrain/converter-shift", "An automatic unlocks its converter during a commanded shift without snapping pump RPM to wheel RPM") {
    Car& c = ctx.add(vehicleCatalog()[2], -80, -50, 0);
    ctx.focus(c); c.manualShift = true;
    ctx.run(1); ctx.launch(c, 8);
    c.gear = 2; c.rpm = 4000;
    ctx.check("manual selection starts an automatic's gear change", c.shift(1) && c.gear == 3);
    const double before = c.rpm;
    ctx.sim.substep();
    ctx.metric("pump RPM changes continuously at shift onset", std::fabs(c.rpm - before), "rpm", 0, 30);
    double omega = 0; int count = 0;
    for (const Wheel& wh : c.wheels) if (c.wheelPowered(wh)) { omega += wh.sideOmega; count++; }
    const double turbine = omega / count * c.spec->gears[2] * c.spec->finalDrive * 60 / (2 * M_PI);
    ctx.check("unlocked converter keeps engine speed independent of turbine", c.rpm > turbine + 300);
}

// ---- Low range with a big engine swap ------------------------------------------------------
namespace {
int partIndex(int vehicle, int slot, const std::string& name) {
    const auto& o = modOptions(vehicle, slot);
    for (int i = 0; i < (int)o.size(); i++)
        if (o[i].name.find(name) != std::string::npos) return i;
    return 0;
}
int lowMode(const VehicleSpec& s) {
    const auto& m = driveModes(s.transfer);
    for (int i = (int)m.size() - 1; i >= 0; i--)
        if (m[i].low) return i;
    return -1;
}
int shaftsGone(const Car& c) {
    int n = 0;
    for (const Wheel& w : c.wheels) n += w.shaftBroken;
    for (const Beam& b : c.beams) n += b.type == BEAM_SHAFT && b.broken;
    return n;
}
}  // namespace

// The garage's biggest engine on every low-range truck: into 4L at idle, then away gently,
// then a stab of the pedal. The engine's torque arrives over ~0.15 s, so the half shafts are
// not rung by a step (a spring's first swing is twice the steady torque): nothing snaps.
SCENARIO(lowRangeSwap, "drivetrain/low-range-swap", "Every low-range truck with the biggest engine swap: into 4L at idle, away gently, then a stab of the pedal: no shaft or CV snaps") {
    int lane = 0;
    for (int v : listedVehicles()) {
        const VehicleSpec& stock = vehicleCatalog()[v];
        if (stock.lowRange <= 0 || lowMode(stock) < 0) continue;
        ModSet ms;
        ms.pick[MOD_ENGINE] = partIndex(v, MOD_ENGINE, "BLOWN 6.2");  // the buggy's 820 N*m supercharged LS
        const VehicleSpec& spec = tunedSpec(v, ms);
        Car& c = ctx.add(spec, -60, -50 - 12 * lane++, 0);
        ctx.focus(c);
        ctx.run(1.5);
        c.setDriveMode(lowMode(spec));
        ctx.run(1.0, [&] { c.throttle = 0; });
        ctx.check(std::string(spec.id) + ": idling in 4L breaks nothing", shaftsGone(c) == 0);
        ctx.run(4, [&] { c.throttle = 0.3; });
        ctx.check(std::string(spec.id) + ": pulling away gently in 4L breaks nothing", shaftsGone(c) == 0 && speedKmh(c) > 3);
        ctx.run(0.5, [&] { c.throttle = 0; });
        ctx.run(2, [&] { c.throttle = 1; });
        ctx.check(std::string(spec.id) + ": a stab to the floor in 4L breaks nothing", shaftsGone(c) == 0);
    }
    ctx.check("some low-range trucks were tried", lane >= 3);
}

// The 4Runner as the career garage builds it: the LS7 on the Blazer's four-speed manual (6.55 first,
// 4.56 axles) in 4L is 77:1. The pedal hitting the floor used to snap the front CVs within 20 ms.
SCENARIO(lowRangeCrawler, "drivetrain/low-range-crawler", "4Runner with LS7 and the Blazer's manual box in 4L: the floored pedal ramps in; a stuck tire still snaps a shaft") {
    ModSet ms;
    ms.pick[MOD_ENGINE] = partIndex(3, MOD_ENGINE, "7.0 LS7");
    ms.pick[MOD_TRANSMISSION] = partIndex(3, MOD_TRANSMISSION, "4.56 GEARS AND LOCKERS");
    const VehicleSpec& spec = tunedSpec(3, ms);
    ctx.check("(the 77:1 stack is in)", spec.gears[0] * spec.finalDrive * spec.lowRange > 70);
    Car& c = ctx.add(spec, -60, -50, 0);
    Car& stuck = ctx.add(spec, -60, -65, 0);
    ctx.focus(c);
    ctx.run(1.5);
    c.setDriveMode(lowMode(spec));
    stuck.setDriveMode(lowMode(spec));
    ctx.run(2, [&] { c.throttle = 1; });
    ctx.check("floored from rest: nothing snaps", shaftsGone(c) == 0);
    ctx.metric("front shaft load against its limit", [&] {
        double m = 0;
        for (const Wheel& w : c.wheels) m = std::max(m, w.shaftLoad / (spec.shaftLimit * (w.front ? 0.65 : 1.0)));
        return m;
    }(), "share", 0, 1);
    // Legitimate breakage stays: flat out against a held brake (a tire that can't turn).
    ctx.run(3, [&] { stuck.throttle = 1; stuck.brake = 1; });
    ctx.check("flat out against the brakes still snaps a shaft", shaftsGone(stuck) > 0);
}

// The sandbox garage's 4Runner (LONG TRAVEL AND 37S, CRAWL BOX, stock wheels and tires) with each of
// the buggy's engines (and the saved 1GR swap): into 4L by the L key's route (4H, 4H LOCK, 4L LOCK)
// at idle, rolling, with the pedal already down (a pad trigger) and with the wheels spinning.
// Nothing may break on engaging; and the engines' own torque must not either, short of a stuck tire.
SCENARIO(lowRangeBuggyEngines, "drivetrain/low-range-buggy-engines", "4Runner (long travel 37s, crawl box) with each buggy engine: into 4L via 4H LOCK at idle, rolling, pedal down, wheels spinning: nothing breaks") {
    const int buggy = 4;
    ctx.check("(catalog index 4 is the buggy)", std::string(vehicleCatalog()[buggy].id) == "buggy");
    std::vector<int> engines{partIndex(3, MOD_ENGINE, "1GR SWAP")};
    const auto& opts = modOptions(3, MOD_ENGINE);
    for (int i = 0; i < (int)opts.size(); i++)
        if (opts[i].fromVehicle == buggy) engines.push_back(i);
    ctx.check("the buggy's engines are on offer", engines.size() >= 4);
    int lane = 0;
    for (int e : engines) {
        ModSet ms;
        ms.pick[MOD_ENGINE] = e;
        ms.pick[MOD_SUSPENSION] = partIndex(3, MOD_SUSPENSION, "LONG TRAVEL AND 37S");
        ms.pick[MOD_TRANSMISSION] = partIndex(3, MOD_TRANSMISSION, "CRAWL BOX");
        const VehicleSpec& spec = tunedSpec(3, ms);
        const std::string tag = modOptions(3, MOD_ENGINE)[e].name;
        for (int trial : {0, 3}) {
            // idle; or rolling 1 m/s with the pedal down at 0.7 and the wheels spinning 8 rad/s over the road
            Car& c = ctx.add(spec, -80 + 10 * trial, -40 - 14 * lane, 0);
            ctx.run(1.2);
            if (trial >= 1) ctx.launch(c, 1.0);
            if (trial == 3) for (Wheel& w : c.wheels) w.omega += 8, w.sideOmega = w.omega;
            const double pedal = trial >= 2 ? 0.7 : 0.0;
            auto hold = [&] { c.throttle = pedal; c.brake = c.handbrake = 0; };
            ctx.run(0.3, hold);
            c.setDriveMode(2);  // 4H LOCK on the way
            ctx.run(0.3, hold);
            c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L LOCK
            ctx.run(3, hold);
            ctx.check(tag + " trial " + std::to_string(trial) + ": nothing breaks engaging 4L", shaftsGone(c) == 0);
        }
        lane++;
    }
}

namespace {
// The sandbox garage's 4Runner as the player has it: the 1GR swap, long travel and 37s, the crawl box.
const VehicleSpec& sandboxRunner() {
    const int v = 3;
    ModSet ms;
    ms.pick[MOD_ENGINE] = partIndex(v, MOD_ENGINE, "1GR SWAP");
    ms.pick[MOD_SUSPENSION] = partIndex(v, MOD_SUSPENSION, "LONG TRAVEL AND 37S");
    ms.pick[MOD_TRANSMISSION] = partIndex(v, MOD_TRANSMISSION, "CRAWL BOX");
    return tunedSpec(v, ms);
}
int frontShaftsGone(const Car& c) {
    int n = 0;
    for (const Wheel& w : c.wheels) n += w.front && w.shaftBroken;
    return n;
}
}  // namespace

// The 4Runner's front CVs (65% of the 13,000 N*m axle shaft, less at lock) carry what a front tire can put into them:
// tire load x grip x radius, and a front tire on a ledge face carries 10 to 22 kN. At 10,000 they held 14 kN of traction
// on the 37s (the Tacoma's hold 16 on its 35s, the Bronco's 18 on 37s) and the sandbox truck lost a CV in 4 of these 12
// ledge crawls (9 of 24 with 2.0 m/s too). Square ledges across the lane, flat out in 4L with both lockers at 12 psi,
// head on and at 25 degrees; and the bumpy dirt (the whoops lane) at 35 km/h in 4H. Nothing may break.
SCENARIO(runnerFrontDurability, "drivetrain/4runner-front-durability", "Sandbox 4Runner (1GR, long travel 37s, crawl box) crawls 0.4 to 0.6 m ledges head on and at 25 degrees and drives bumpy dirt at 35 km/h: no CV snaps, no tie rod or arm bends") {
    const VehicleSpec& spec = sandboxRunner();
    ctx.check("(the 37s and the crawl box are in)", spec.wheelRadius > 0.46 && spec.lowRange > 4);
    ctx.sim.world = World(MAP_PROVING_GROUND);
    World& w = ctx.sim.world;
    struct Run {
        Car* c;
        double z, v;
    };
    std::vector<Run> runs;
    const double faceX = -112;
    int lane = 0;
    for (double ang : {0.0, 25.0})
        for (double H : {0.4, 0.5, 0.6})
            for (double v : {1.0, 1.5}) {
                const double z = -170 + 14 * lane++;
                w.addBox(V3(faceX + 8, w.height(faceX + 8, z) + H / 2, z), V3(8, H / 2, 5), 0, 0, 0, V3(0.4, 0.38, 0.35));
                w.boxes.back().rock = true;
                runs.push_back({nullptr, z, v});
                (void)ang;
            }
    w.buildGrid();
    lane = 0;
    for (double ang : {0.0, 25.0})
        for (int k = 0; k < 6; k++) {
            Run& r = runs[lane++];
            const double a = ang * M_PI / 180;
            r.c = &ctx.add(spec, faceX - 12 * std::cos(a), r.z + 12 * std::sin(a), -ang);
        }
    ctx.focus(*runs[0].c);
    ctx.run(1.0);
    for (Run& r : runs) {
        r.c->setDriveMode(lowMode(spec));
        r.c->setLocker(true, true), r.c->setLocker(false, true);
        r.c->tractionControl = false;
        r.c->tirePsi = 12;
    }
    double worstFront = 0;
    ctx.run(30, [&] {
        for (Run& r : runs) {
            Car& c = *r.c;
            const Frame fr = c.frame();
            const V3 d = V3(fr.com.x + 6, fr.com.y, r.z) - fr.com;
            c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
            const double vel = dot(fr.vel, fr.fwd);
            c.throttle = clampd(0.3 + (r.v - vel) * 0.9, 0, 1);  // (flat out at the ledge: a keyboard's W)
            c.brake = vel > r.v + 1.0 ? 0.3 : 0;
            c.handbrake = 0;
            for (const Wheel& wh : c.wheels)
                if (wh.front) worstFront = std::max(worstFront, wh.shaftLoad / (spec.shaftLimit * 0.65 * (1 - 0.35 * clampd(std::fabs(c.steer) / 0.6, 0, 1))));
        }
    });
    int lost = 0, bent = 0, cleared = 0;
    for (const Run& r : runs) {
        lost += frontShaftsGone(*r.c) > 0;
        cleared += r.c->frame().com.x > faceX + 4;
        for (const Wheel& wh : r.c->wheels) bent += std::fabs(wh.tieOffset) > 0.002;
    }
    ctx.check("no ledge crawl loses a front CV", lost == 0, std::to_string(lost) + " of " + std::to_string(runs.size()) + " lost one");
    ctx.check("no tie rod bends on them", bent == 0, std::to_string(bent));
    ctx.metric("ledges climbed (of 12)", cleared, "", 8, 12);
    ctx.metric("worst front CV load against its limit (1 snaps)", worstFront, "share", 0, 1);

    // Bumpy dirt: the whoops lane (0.55 m bumps every 9 m), 4H at 35 km/h.
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 f = mountainFeature("whoops");
    Car& c = ctx.add(spec, f.x, f.z - 45, -90);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(1);  // 4H (the centre diff free)
    double tOut = -1;
    ctx.runUntil(60, [&] { return tOut > 0; }, [&] {
        const Frame fr = c.frame();
        const V3 d = V3(f.x, fr.com.y, fr.com.z + 8) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)), -0.3, 0.3);
        const double kmh = len(fr.vel) * 3.6;
        c.throttle = clampd(0.3 + (35 - kmh) * 0.2, 0, 1);
        c.brake = kmh > 43 ? 0.3 : 0;
        if (fr.com.z > f.z + 130) tOut = ctx.t;
    });
    ctx.check("through the whoops", tOut > 0);
    ctx.check("bumpy dirt breaks nothing: no shaft, no CV", shaftsGone(c) == 0);
    bool bentParts = false;
    for (const Wheel& wh : c.wheels) bentParts |= std::fabs(wh.tieOffset) > 0.002;
    for (const Axle& ax : c.axles) bentParts |= std::fabs(ax.arbBend) > 0.005;
    ctx.check("...and bends no tie rod or sway bar", !bentParts);
}

// ...but a CV is still a fuse: the sandbox 4Runner in 4L with both lockers, full lock and the pedal on the floor on the
// slickrock dome (the lockers bind, a loaded front tire grips) snaps one, as the Bronco's does flat out into a ledge (offroad/shaft-break);
// and flat out against a held brake in the 77:1 stack still does (drivetrain/low-range-crawler).
SCENARIO(runnerFrontAbuse, "drivetrain/4runner-front-abuse", "Sandbox 4Runner flat out at full lock with both lockers in 4L on the rock dome: a CV still snaps and that wheel freewheels") {
    const VehicleSpec& spec = sandboxRunner();
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 dome = mountainFeature("dome");
    Car& c = ctx.add(spec, dome.x, dome.z - 22, -90);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(lowMode(spec));
    c.setLocker(true, true), c.setLocker(false, true);
    c.tractionControl = false;
    c.tirePsi = 15;
    ctx.runUntil(60, [&] { return shaftsGone(c) > 0; }, [&] {
        c.steer = 0.6 * ((int)(ctx.t / 6) % 2 ? -1 : 1);
        c.throttle = 1;
        c.brake = c.handbrake = 0;
    });
    ctx.check("a shaft snaps", shaftsGone(c) > 0);
    bool freewheels = false;
    for (const Wheel& wh : c.wheels)
        if (wh.shaftBroken) freewheels = !c.wheelPowered(wh);
    ctx.check("that wheel gets no drive", freewheels);

    // And no tougher than the other independent fronts: the traction a front CV holds (its limit over the tire radius) is
    // between the Tacoma's and the Bronco's, on the stock tires and on the 37s.
    auto holds = [](const VehicleSpec& s) { return s.shaftLimit * 0.65 / s.wheelRadius / 1000; };
    const double lo = holds(vehicleCatalog()[6]), hi = holds(vehicleCatalog()[2]);
    ctx.metric("stock 4Runner CVs: traction held (Tacoma's to the Bronco's)", holds(vehicleCatalog()[3]), "kN", lo * 0.95, hi * 1.05);
    ctx.metric("sandbox 4Runner CVs on the 37s: traction held", holds(spec), "kN", lo * 0.9, hi * 1.05);
}
