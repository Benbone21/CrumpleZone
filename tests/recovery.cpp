// Recovery: winching, towing on a strap, repair stations, and putting a wreck back on
// its wheels.
#include <cmath>

#include "../src/interact.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }
const VehicleSpec& runner() { return vehicleCatalog()[3]; }

int indexOf(Ctx& ctx, const Car& c) { return (int)(&c - ctx.sim.cars.data()); }

double frameSet(const Car& c) {
    double worst = 0;
    for (const Beam& b : c.beams)
        if (b.type == BEAM_FRAME) worst = std::max(worst, b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig);
    return worst * 100;
}

// Leaves a truck with a dead engine (water in the intake), in neutral and off the brakes.
void kill(Car& c) {
    c.hydrolocked = true;
    c.engineDead = true;
}

// Pushes on every body node with a total force F (N), shared by mass.
void pushBody(Car& c, const V3& F) {
    c.extForce.assign(c.nodes.size(), V3());
    double m = 0;
    for (int i = 0; i < c.bodyNodes; i++) m += c.nodes[i].m;
    for (int i = 0; i < c.bodyNodes; i++) c.extForce[i] = F * (c.nodes[i].m / m);
}

}  // namespace

SCENARIO(winchSelf, "recovery/winch-self", "A Bronco with a dead engine at the foot of the slickrock dome winches itself up to a boulder on top") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 dome = mountainFeature("dome");
    World& w = ctx.sim.world;
    w.addRock(dome.x, dome.z + 2, V3(1.3, 1.1, 1.3), 1.0, 4242, 1);
    w.buildGrid();
    Car& c = ctx.add(bronco(), dome.x, dome.z - 27, -90);
    ctx.focus(c);
    ctx.run(1.0);
    kill(c);
    const int ci = indexOf(ctx, c);
    const std::string msg = ctx.sim.hookWinch(ci);
    ctx.check("winch hooks a boulder ahead", ctx.sim.ropeOn(ci, ROPE_WINCH) >= 0 && ctx.sim.ropes[0].b.kind == ANCHOR_ROCK, msg);
    const V3 start = c.frame().com;
    double peak = 0, rise = 0;
    bool snapped = false;
    ctx.run(120, [&] {
        c.throttle = c.brake = c.handbrake = 0;
        c.steer = 0;
        ctx.sim.setWinch(ci, 1);
        if (ctx.sim.ropeOn(ci, ROPE_WINCH) < 0) {
            snapped = true;
            return;
        }
        peak = std::max(peak, ctx.sim.ropes[ctx.sim.ropeOn(ci, ROPE_WINCH)].shown);
        rise = std::max(rise, c.frame().com.y - start.y);
        ctx.sample("Line tension", "bronco", ctx.sim.ropes[ctx.sim.ropeOn(ci, ROPE_WINCH)].shown / 1000, "kN");
    });
    ctx.check("line holds", !snapped);
    ctx.metric("pulled up the dome", c.frame().com.z - start.z, "m", 10, 30);
    ctx.metric("height gained", rise, "m", 3, 10);
    ctx.metric("peak line tension (a 9,500 lb winch stalls at 44 kN)", peak / 1000, "kN", 5, 46);
    ctx.metric("damage from the pull", c.damage * 100, "%", 0, 0.3);
    ctx.metric("frame set from the pull", frameSet(c), "%", 0, 0.5);
}

SCENARIO(winchStall, "recovery/winch-stall", "A braked Bronco winched to a tree: slides, then stalls the winch against 40 kN more, then snaps the line at 110 kN") {
    // Proving-ground poles stand in a row at z = 38.
    Car& c = ctx.add(bronco(), 50, 16, -90);
    ctx.focus(c);
    ctx.run(1.0);
    const int ci = indexOf(ctx, c);
    const std::string msg = ctx.sim.hookWinch(ci);
    ctx.check("winch hooks the pole ahead", ctx.sim.ropeOn(ci, ROPE_WINCH) >= 0 && ctx.sim.ropes[0].b.kind == ANCHOR_TREE, msg);
    auto line = [&]() -> Rope* { int r = ctx.sim.ropeOn(ci, ROPE_WINCH); return r >= 0 ? &ctx.sim.ropes[r] : nullptr; };
    const double z0 = c.frame().com.z;
    // 1. Brakes locked: the winch drags it (four sliding tires, ~20 kN).
    ctx.run(8, [&] {
        c.brake = 1, c.handbrake = 1, c.throttle = 0;
        ctx.sim.setWinch(ci, 1);
    });
    ctx.metric("dragged toward the tree with the brakes locked", c.frame().com.z - z0, "m", 0.2, 1.2);
    // 2. 40 kN more holding it back: past the winch's rating, so it stalls and holds.
    pushBody(c, V3(0, 0, -40000));
    ctx.run(1.0, [&] { ctx.sim.setWinch(ci, 1); });
    const double lenBefore = line() ? line()->length : 0;
    double tmax = 0;
    ctx.run(3.0, [&] {
        ctx.sim.setWinch(ci, 1);
        if (line()) tmax = std::max(tmax, line()->shown);
    });
    ctx.check("line holds at the stall", line() != nullptr);
    ctx.metric("line reeled in while stalled", line() ? (lenBefore - line()->length) * 100 : 99, "cm", 0, 3);
    ctx.metric("tension held at the stall", tmax / 1000, "kN", 40, 75);
    ctx.metric("frame set from 60 kN on the recovery point", frameSet(c), "%", 0, 1.0);
    // 3. 110 kN: more than the line takes.
    pushBody(c, V3(0, 0, -110000));
    bool snapped = false;
    ctx.runUntil(2.0, [&] { return snapped; }, [&] {
        ctx.sim.setWinch(ci, 0);
        snapped = line() == nullptr;
    });
    c.extForce.clear();
    ctx.check("synthetic line snaps past ~90 kN", snapped);
    ctx.run(1.0);
}

SCENARIO(strapTow, "recovery/strap-tow", "A 4Runner tows a dead Bronco 100 m on a strap to the proving-ground shop, where it's repaired") {
    const Station& shop = ctx.sim.world.stations[0];
    // Both facing -X toward the shop; the 4Runner in front.
    Car& dead = ctx.add(bronco(), shop.p.x + 108, shop.p.z, 180);
    Car& tow = ctx.add(runner(), shop.p.x + 99, shop.p.z, 180);
    ctx.focus(tow);
    ctx.run(1.0);
    kill(dead);
    const int ti = indexOf(ctx, tow), di = indexOf(ctx, dead);
    const std::string msg = ctx.sim.hookStrap(ti);
    ctx.check("strap from the 4Runner's rear to the Bronco's front", ctx.sim.ropeOn(ti, ROPE_STRAP) >= 0 && ctx.sim.ropes[0].a.hook == 1 &&
                                                                       ctx.sim.ropes[0].b.car == di && ctx.sim.ropes[0].b.hook == 0, msg);
    const V3 start = dead.frame().com;
    double peak = 0, lateral = 0;
    bool snapped = false;
    // Drive west at ~12 km/h until the Bronco is in the middle of the pad, then stop.
    ctx.runUntil(60, [&] { return dead.frame().com.x < shop.p.x + 0.5; }, [&] {
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        Frame fr = tow.frame();
        V3 d = V3(fr.com.x - 6, fr.com.y, shop.p.z) - fr.com;
        tow.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.5, 0.5);
        double v = dot(fr.vel, fr.fwd);
        double want = dead.frame().com.x - shop.p.x < 12 ? 1.5 : 3.4;
        tow.throttle = clampd(0.25 + (want - v) * 0.5, 0, 1);
        tow.brake = v > want + 0.8 ? 0.4 : 0;
        tow.handbrake = 0;
        int r = ctx.sim.ropeOn(ti, ROPE_STRAP);
        if (r < 0) snapped = true;
        else peak = std::max(peak, ctx.sim.ropes[r].shown);
        lateral = std::max(lateral, std::fabs(dead.frame().com.z - shop.p.z));
        ctx.sample("Strap tension", "strap", r >= 0 ? ctx.sim.ropes[r].shown / 1000 : 0, "kN");
    });
    ctx.check("strap holds", !snapped);
    ctx.metric("distance towed", start.x - dead.frame().com.x, "m", 95, 115);
    ctx.metric("towed truck stays in line (worst lateral)", lateral, "m", 0, 1.5);
    ctx.metric("peak strap tension", peak / 1000, "kN", 1, 40);
    ctx.metric("tow damage (4Runner)", tow.damage * 100, "%", 0, 0.2);
    ctx.metric("tow damage (Bronco)", dead.damage * 100, "%", 0, 0.2);
    ctx.check("Bronco on the shop pad", ctx.sim.stationAt(dead.frame().com) == 0);
    // Stop and let the shop work.
    bool repaired = false;
    ctx.runUntil(40, [&] { return repaired; }, [&] {
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        tow.throttle = 0, tow.brake = 1;
        repaired = !ctx.sim.cars[di].hydrolocked;
    });
    ctx.check("shop fixes the Bronco's engine", repaired);
    ctx.check("strap comes off for the repair", ctx.sim.ropeOn(di, -1) < 0);
}

SCENARIO(repairStation, "recovery/repair", "A battered Bronco drives onto the shop pad: moving resets the work, standing still gets it fixed") {
    const Station& shop = ctx.sim.world.stations[0];
    Car& c = ctx.add(bronco(), shop.p.x - 30, shop.p.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    // Roll it onto its roof, put it back on its wheels, pop a tire, snap a shaft.
    c.rotateRigid(c.frame().fwd, M_PI);
    for (Node& n : c.nodes) n.p.y += 4.0;  // (the trucks are tough: from 2.5 m the roof barely marks)
    ctx.run(3.0);
    const int ci = indexOf(ctx, c);
    ctx.sim.rightPlayer();
    ctx.run(2.0);
    c.wheels[0].popped = true;
    c.wheels[2].shaftBroken = true;
    const double hurt = c.damage;
    ctx.check("needs repair", c.needsRepair());
    ctx.metric("damage before the shop", hurt * 100, "%", 0.01, 30);
    const double need = c.repairSeconds();
    ctx.metric("repair time quoted", need, "s", 10, 60);
    // Drive onto the pad.
    auto toward = [&](double x, double speed) {
        Frame fr = c.frame();
        V3 d = V3(x, fr.com.y, shop.p.z) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(std::fabs(dot(d, fr.fwd)), 0.5)) * 1.2, -0.5, 0.5);
        double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (speed - v) * 0.6, 0, 1);
        c.brake = v > speed + 0.5 ? 0.5 : 0;
        c.handbrake = 0;
    };
    ctx.runUntil(30, [&] { return c.frame().com.x > shop.p.x - 1; }, [&] { toward(shop.p.x + 3, 2.0); });
    ctx.run(3.0, [&] { c.throttle = 0, c.brake = 1; });
    ctx.check("parked on the pad", ctx.sim.stationAt(c.frame().com) == 0);
    ctx.metric("work started", c.repairTime, "s", 1, 3.5);
    // Roll forward: the crew downs tools.
    ctx.run(1.5, [&] { c.brake = 0, c.throttle = 0.5; });
    ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; });
    ctx.metric("moving resets the work", c.repairTime, "s", 0, 1.2);
    bool done = false;
    ctx.runUntil(need + 10, [&] { return done; }, [&] {
        c.throttle = 0, c.brake = 1;
        done = !ctx.sim.cars[ci].needsRepair();
    });
    const Car& f = ctx.sim.cars[ci];
    ctx.check("fixed", done);
    ctx.metric("damage after", f.damage * 100, "%", 0, 0.01);
    bool tires = true;
    for (const Wheel& wh : f.wheels) tires &= !wh.popped && !wh.shaftBroken;
    ctx.check("tires and shafts replaced", tires);
    ctx.check("R still goes back to the original spawn", len(f.spawnPos - V3(shop.p.x - 30, f.spawnPos.y, shop.p.z)) < 1.0);
}

SCENARIO(rightingRealMode, "recovery/upright", "On its roof, T (real recovery) puts the truck back on its wheels with its damage intact") {
    Car& c = ctx.add(bronco(), -60, -20, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.rotateRigid(c.frame().fwd, M_PI);
    for (Node& n : c.nodes) n.p.y += 2.5;  // rolled off a ledge: lands hard on its roof
    ctx.run(4.0);
    ctx.check("lands on its roof", c.frame().up.y < -0.8);
    const double hurt = c.damage;
    ctx.sim.rightPlayer();
    ctx.run(3.0);
    ctx.check("back on its wheels", c.frame().up.y > 0.95);
    ctx.metric("damage kept (no free repair)", c.damage / std::max(hurt, 1e-6) * 100, "%", 95, 110);
    ctx.metric("new damage setting it down", (c.damage - hurt) * 100, "%", -0.05, 0.1);
    int loaded = 0;
    for (const Wheel& wh : c.wheels) loaded += wh.load > 500;
    ctx.metric("wheels on the ground", loaded, "", 4, 4);
}

// Rebuilt in place on a steep hillside (arcade T, a vehicle swap, a garage refit): the
// truck is set down matching the slope, so no tire starts inside the hill and gets rammed
// up through the suspension into the body.
SCENARIO(rebuildOnSlope, "recovery/rebuild-on-slope", "Repaired in place on a 26 degree hillside: wheels stay under the truck, on the ground") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    World& w = ctx.sim.world;
    double x = 0, z = 0;
    for (double a = 0; a < 6.28 && x == 0; a += 0.05)
        for (double r = 100; r < 300; r += 7) {
            const double xx = 40 + r * std::cos(a), zz = 260 + r * std::sin(a);
            if (std::fabs(w.normal(xx, zz).y - 0.9) < 0.01) {
                x = xx, z = zz;
                break;
            }
        }
    ctx.check("found a ~26 degree hillside", x != 0);
    int k = 0;
    for (const VehicleSpec* spec : {&bronco(), &runner()})
        for (double heading : {-90.0, 0.0}) {  // nose up the hill, then across it
            Car& c = ctx.add(*spec, x + 25 * k, z, heading);
            ctx.focus(c);
            ctx.run(0.5, [&] { c.handbrake = 1; });
            ctx.sim.recoverPlayer();
            Car& r = ctx.sim.cars[ctx.sim.player];
            ctx.run(3.0, [&] { r.handbrake = 1, r.brake = 1; });
            const Frame fr = r.frame();
            double worst = -1e9;
            int grounded = 0;
            for (const Wheel& wh : r.wheels) {
                worst = std::max(worst, dot((r.nodes[wh.inner].p + r.nodes[wh.outer].p) * 0.5 - fr.com, fr.up));
                grounded += wh.load > 300;
            }
            const std::string tag = std::string(spec->id) + (heading < 0 ? " up the hill" : " across it");
            ctx.metric(tag + ": highest wheel hub relative to the body's center", worst, "m", -1.0, -0.25);
            ctx.metric(tag + ": tires on the ground", grounded, "", 3, 4);
            ctx.metric(tag + ": damage", r.damage * 100, "%", 0, 0.5);
            k++;
        }
}

// Trail repairs are hands-on (fixes.h): on foot at the broken corner, with your own tools and
// a spare part. A truck parked alongside no longer fixes anything by itself.
SCENARIO(fieldRepair, "recovery/trail-repair", "A Bronco with a flat, a snapped CV axle, a bent tie rod and a flooded engine: nothing gets fixed by a truck parked alongside; on foot, short of tools it can't be; with the tools and the parts each is fixed by hand, jacked up where it needs it") {
    Car& b = ctx.add(bronco(), -60, -30, 0);
    ctx.focus(b);
    ctx.run(1.0);
    const int bi = indexOf(ctx, b);
    b.wheels[0].popped = true;
    b.wheels[2].shaftBroken = true;
    b.wheels[1].tieOffset = 0.01;
    b.hydrolocked = b.engineDead = true;
    ctx.add(runner(), -60, -25, 0);
    ctx.run(10.0, [&] { ctx.sim.cars[bi].handbrake = 1; });
    Car& c = ctx.sim.cars[bi];
    ctx.check("a truck parked alongside fixes nothing", c.wheels[0].popped && c.wheels[2].shaftBroken && c.hydrolocked);
    const std::vector<Fix> fixes = fixesFor(c);
    ctx.check("four repairs to do", fixes.size() == 4, std::to_string(fixes.size()));
    Walker wk;
    wk.exitCar(ctx.sim.world, c);
    Interact ia;
    // Hold E at a fix until it's done (or give up); the walker stands just off it.
    auto workOn = [&](Fix::Kind kind, double secs, std::string& prompt) {
        std::string said;
        V3 at;
        for (const Fix& f : fixesFor(c))
            if (f.kind == kind) at = f.at;
        V3 out = at - c.frame().com;
        out.y = 0;
        const V3 q = at + norm(out) * 1.0;
        wk.p = V3(q.x, ctx.sim.world.height(q.x, q.z), q.z);
        for (double t = 0; t < secs && said.empty(); t += Ctx::kFrame) {
            const V3 eye = wk.p + V3(0, 1.6, 0);
            said = ia.update(ctx.sim, wk, eye, at - eye, true, Ctx::kFrame);
            prompt = ia.current.prompt;
            ctx.run(Ctx::kFrame);
        }
        const V3 eye = wk.p + V3(0, 1.6, 0);
        ia.update(ctx.sim, wk, eye, V3(1, 0, 0), false, Ctx::kFrame);  // (let go)
        return said;
    };
    // A career with no tools but the lug wrench and the jack, and no parts.
    ia.kit.unlimited = false;
    ia.kit.tools[TOOL_LUG_WRENCH] = ia.kit.tools[TOOL_JACK] = true;
    std::string prompt;
    ctx.check("no socket set: the CV axle can't be done", workOn(Fix::CV_AXLE, 6, prompt).empty() && c.wheels[2].shaftBroken, prompt);
    ctx.check("...and it says what's missing", prompt.find("SOCKET SET") != std::string::npos && prompt.find("HAMMER") != std::string::npos, prompt);
    for (int t = 0; t < TOOL_COUNT; t++) ia.kit.tools[t] = true;
    workOn(Fix::CV_AXLE, 6, prompt);
    ctx.check("tools but no part: it says so", prompt.find("NO CV AXLE") != std::string::npos, prompt);
    for (int p = 0; p < PART_COUNT; p++) ia.kit.parts[p] = 1;
    workOn(Fix::CV_AXLE, 6, prompt);
    ctx.check("the corner has to be up: it says so", prompt.find("JACK") != std::string::npos, prompt);
    // Jack the corner (by hand here: the jack's own test is tires/swap).
    auto lift = [&](int k) {
        c.placeJack(ctx.sim.world, k);
        ctx.runUntil(6, [&] { return Interact::cornerLifted(ctx.sim, c, k); }, [&] { c.jackHeight += 0.14 * Ctx::kFrame, c.wake(); });
        return Interact::cornerLifted(ctx.sim, c, k);
    };
    ctx.check("the CV corner jacks up", lift(2));
    ctx.check("the CV axle goes in", workOn(Fix::CV_AXLE, 8, prompt) == "CV AXLE FITTED" && !c.wheels[2].shaftBroken, prompt);
    ctx.check("...and uses the part", ia.usedPart == PART_CV_AXLE);
    ia.usedPart = -1;
    c.jackPlaced = false, c.jackHeight = 0;
    ctx.run(1.0);
    ctx.check("the tie rod (no jack needed)", workOn(Fix::TIE_ROD, 8, prompt) == "TIE ROD FITTED" && std::fabs(c.wheels[1].tieOffset) < 1e-9, prompt);
    ctx.check("the spark plugs: the water out", workOn(Fix::SPARK_PLUGS, 8, prompt) == "SPARK PLUGS FITTED" && !c.hydrolocked && !c.engineDead, prompt);
    ctx.check("the flat corner jacks up", lift(0));
    ctx.check("a new tire on the rim", workOn(Fix::NEW_TIRE, 8, prompt) == "TIRE FITTED" && !c.wheels[0].popped, prompt);
    c.jackPlaced = false, c.jackHeight = 0;
    ctx.run(2.0);
    ctx.check("nothing left to fix out here", fixesFor(c).empty() && !c.needsFieldRepair());
    int powered = 0;
    for (const Wheel& wh : c.wheels) powered += c.wheelPowered(wh);
    ctx.metric("driven wheels after the repairs", powered, "", 4, 4);
}

SCENARIO(recoveryPicking, "recovery/pointer-picking", "Pointing at front/rear vehicle areas selects those recovery points; empty space selects nothing") {
    ctx.add(runner(), -40, -40, 0);
    ctx.add(bronco(), -47, -40, 0);
    ctx.sim.player = 0;
    ctx.run(0.5);
    auto targets = ctx.sim.strapTargets(0, 1);
    ctx.check("both ends of the other vehicle are offered", targets.size() == 2);
    const V3 eye(-47, 6, -34);
    for (int h = 0; h < 2; h++) {
        const V3 hook = ctx.sim.cars[1].hookPoint(h);
        const int pick = ctx.sim.pickRecoveryTarget(targets, eye, norm(hook - eye), 0.02);
        ctx.check(h ? "clicking the rear picks the rear" : "clicking the front picks the front", pick >= 0 && targets[pick].end.hook == h);
    }
    const int body = ctx.sim.pickRecoveryTarget(targets, eye, norm(ctx.sim.cars[1].frame().com + V3(1.5, 0.1, 0) - eye), 0.02);
    ctx.check("a click on the vehicle body snaps to its front point", body >= 0 && targets[body].end.hook == 0);
    ctx.check("empty space does not silently hook a distant car", ctx.sim.pickRecoveryTarget(targets, eye, V3(0, 1, 0), 0.02) == -1);
    if (targets.empty()) return;
    const std::string hooked = ctx.sim.hookStrapTo(0, 1, targets[0].end);
    ctx.check("the selected endpoints are attached", ctx.sim.ropes.size() == 1 && ctx.sim.ropes[0].a.hook == 1 && ctx.sim.ropes[0].b.hook == targets[0].end.hook, hooked);
    RopeEnd invalid; invalid.car = 999;
    ctx.sim.hookStrapTo(0, 1, invalid);
    ctx.check("an invalid target preserves the existing strap", ctx.sim.ropes.size() == 1);
}

SCENARIO(towingHealthy, "recovery/healthy-car-tow", "A running unattended truck rolls in neutral, with no hidden slack-line brake, and restores its gear when taking the wheel") {
    ctx.add(runner(), -45, -40, 0);
    ctx.add(bronco(), -52, -40, 0);
    ctx.sim.player = 0;
    ctx.run(1);
    Car& tow = ctx.sim.cars[0]; Car& other = ctx.sim.cars[1];
    other.gear = 3; other.brake = other.handbrake = other.brakePressure = 1;
    RopeEnd target; target.car = 1;
    ctx.sim.hookStrapTo(0, 1, target);
    ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
    ctx.check("unattended tow selects neutral", other.gear == 0 && other.towRolling);
    ctx.check("brakes are released even with a slack strap", other.brake == 0 && other.handbrake == 0 && other.brakePressure == 0);
    const V3 start = other.frame().com;
    double peak = 0;
    ctx.run(12, [&] {
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        const double speed = dot(tow.frame().vel, tow.frame().fwd);
        tow.throttle = clampd((2.0 - speed) * 0.35, 0, 0.4);
        const int r = ctx.sim.ropeOn(0, ROPE_STRAP);
        if (r >= 0) peak = std::max(peak, ctx.sim.ropes[r].shown);
    });
    ctx.metric("running vehicle rolls behind the tow truck", other.frame().com.x - start.x, "m", 8, 35);
    ctx.metric("tow force on paved ground stays modest", peak / 1000, "kN", 0.1, 20);
    ctx.check("strap holds", ctx.sim.ropeOn(0, ROPE_STRAP) >= 0);
    ctx.sim.player = 1;
    ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
    ctx.check("taking the wheel restores the selected gear", other.gear == 3 && !other.towRolling);
}

SCENARIO(winchSpeedControl, "recovery/winch-speed-control", "Faster drum speeds scale line travel but preserve rated stall pull and payout bounds") {
    Car& c = ctx.add(bronco(), -50, -50, 0);
    ctx.focus(c); ctx.run(0.5);
    RopeEnd anchor; anchor.kind = ANCHOR_STAKE; anchor.p = c.hookPoint(0) + V3(14, 0, 0);
    ctx.sim.hookWinchTo(0, anchor);
    ctx.check("fixed anchor connected", ctx.sim.ropes.size() == 1);
    if (ctx.sim.ropes.empty()) return;
    Rope& line = ctx.sim.ropes[0];
    line.length = 25; line.tension = 0; line.winch = 1; c.winchSpeed = 4;
    const double before = line.length;
    ctx.sim.substep();
    ctx.metric("default 4x unloaded line speed", (before - line.length) / ctx.sim.h, "m/s", 0.519, 0.521);
    line.tension = line.stallN; c.winchSpeed = 20;
    const double stalled = line.length;
    ctx.sim.substep();
    ctx.metric("20x cannot reel past rated stall load", std::fabs(stalled - line.length), "m", 0, 1e-9);
    line.tension = 0; line.winch = -1; line.length = line.maxLen;
    ctx.sim.substep();
    ctx.check("payout cannot exceed the available line", line.length == line.maxLen);
    line.winch = 1; line.length = 25;
    const double fast = line.length;
    ctx.sim.substep();
    ctx.metric("maximum 20x unloaded line speed", (fast - line.length) / ctx.sim.h, "m/s", 2.599, 2.601);
}
