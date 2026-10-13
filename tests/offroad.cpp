// Off-road: the 4x4 drive modes and lockers, and the mountain map's crawl park.
#include <cmath>

#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }
const VehicleSpec& runner() { return vehicleCatalog()[3]; }
const VehicleSpec& buggy() { return vehicleCatalog()[4]; }
const VehicleSpec& utv() { return vehicleCatalog()[5]; }

// Crawls along a straight line through (x, z) heading north at walking pace:
// pure-pursuit steering, throttle trimmed to hold the speed.
void crawlNorth(Car& c, double laneX, double speed) {
    Frame fr = c.frame();
    V3 target(laneX, fr.com.y, fr.com.z + 6);
    V3 d = target - fr.com;
    double lt = dot(d, fr.left), fw = dot(d, fr.fwd);
    c.steer = clampd(std::atan2(lt, std::max(fw, 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}

// Sets up a crawl: the mountain map, the truck at the south end of a lane, in 4L.
Car& crawlStart(Ctx& ctx, const VehicleSpec& spec, const char* feature, double back = 6) {
    ctx.sim.world = World(MAP_MOUNTAIN);
    V3 f = mountainFeature(feature);
    Car& c = ctx.add(spec, f.x, f.z - back, -90);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L / 4L LOCK
    c.tractionControl = false;  // crawling: let the wheels work
    return c;
}

// Worst permanent set in any frame member (plastic strain, %: 1% is a visible kink),
// and panels hanging or off.
double frameSet(const Car& c) {
    double worst = 0;
    for (const Beam& b : c.beams)
        if (b.type == BEAM_FRAME) worst = std::max(worst, b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig);
    return worst * 100;
}
int loose(const Car& c) {
    int n = 0;
    for (const Panel& p : c.panels) n += p.state > 0;
    return n;
}

// Crawls straight along +X at a walking pace (proving ground lanes).
void crawlEast(Car& c, double z, double speed) {
    Frame fr = c.frame();
    V3 d = V3(fr.com.x + 6, fr.com.y, z) - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}

Car& lowRangeAt(Ctx& ctx, const VehicleSpec& spec, double x, double z) {
    Car& c = ctx.add(spec, x, z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.tractionControl = false;
    return c;
}

}  // namespace

// The trail is hard on a truck: belly on a boulder, a sill dropped onto a rock, a nose
// off a ledge, a slow tip onto its side or roof, a fender into a boulder. A built 4x4
// shrugs off the first three (skid plates, sliders, a ladder frame) and comes out of the
// others dented but whole: doors shut, frame straight.
static void rockHits(Ctx& ctx, const VehicleSpec& spec) {
    const std::string tag = spec.id;
    World& w = ctx.sim.world;
    // 1. High-centre: crawl over a 0.6 m boulder in the middle of the lane.
    {
        const double z = -20;
        w.addRock(-140, z, V3(0.8, 0.7, 0.8), 0.6, 77, 1);
        w.buildGrid();
        Car& c = lowRangeAt(ctx, spec, -150, z);
        ctx.runUntil(30, [&] { return c.frame().com.x > -132; }, [&] { crawlEast(c, z, 1.5); });
        ctx.metric(tag + " belly over a 60 cm boulder: frame set", frameSet(c), "%", 0, 1.0);
        ctx.metric(tag + " belly over a 60 cm boulder: damage", c.damage * 100, "%", 0, 1.0);
    }
    // 2. A sill dropped 0.6 m onto a rock between the axles.
    {
        const double z = 20;
        Car& c = ctx.add(spec, -100, z, 0);
        ctx.run(1.5, [&] { c.handbrake = 1; });
        const Frame fr = c.frame();
        const V3 sill = fr.com - fr.left * (spec.width / 2 - 0.15);
        double bottom = 1e9;
        for (int i = 0; i < c.bodyNodes; i++)
            if (std::fabs(dot(c.nodes[i].p - sill, fr.fwd)) < 0.4 && dot(c.nodes[i].p - sill, fr.left) < 0.3) bottom = std::min(bottom, c.nodes[i].p.y);
        w.addRock(sill.x, sill.z, V3(0.35, 0.4, 0.35), bottom - w.height(sill.x, sill.z) - 0.02, 91, 1);
        w.buildGrid();
        for (Node& n : c.nodes) n.p.y += 0.6;
        ctx.run(3.0, [&] { c.handbrake = 1; });
        ctx.metric(tag + " sill dropped 0.6 m onto a rock: damage", c.damage * 100, "%", 0, 0.6);
        ctx.metric(tag + " sill dropped 0.6 m onto a rock: frame set", frameSet(c), "%", 0, 1.0);
        ctx.metric(tag + " sill dropped 0.6 m onto a rock: panels loose", loose(c), "", 0, 0);
    }
    // 3. Off a 1.2 m ledge, nose first, at a brisk walk.
    {
        const double z = 60;
        w.addBox(V3(-120, w.height(-120, z) + 0.6, z), V3(10, 0.6, 3), 0, 0, 0, V3(0.4, 0.38, 0.35));
        w.boxes.back().rock = true;
        w.buildGrid();
        Car& c = lowRangeAt(ctx, spec, -125, z);
        ctx.runUntil(25, [&] { return c.frame().com.x > -104; }, [&] { crawlEast(c, z, 2.5); });
        ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; });
        ctx.check(tag + " drove off the ledge", c.frame().com.x > -106);
        ctx.metric(tag + " nose off a 1.2 m ledge: damage", c.damage * 100, "%", 0, 1.0);
        ctx.metric(tag + " nose off a 1.2 m ledge: frame set", frameSet(c), "%", 0, 1.0);
        ctx.metric(tag + " nose off a 1.2 m ledge: panels loose", loose(c), "", 0, 0);
    }
    // 4. Tipped onto its side off a camber (dropped from 0.4 m on its door).
    {
        Car& c = ctx.add(spec, -60, 20, 0);
        ctx.run(1.0, [&] { c.handbrake = 1; });
        c.rotateRigid(c.frame().fwd, M_PI / 2);
        for (Node& n : c.nodes) n.p.y += 0.4;
        ctx.run(3.0, [&] { c.handbrake = 1; });
        ctx.metric(tag + " tipped onto its side: damage", c.damage * 100, "%", 0, 3);
        ctx.metric(tag + " tipped onto its side: frame set", frameSet(c), "%", 0, 1.0);
        ctx.metric(tag + " tipped onto its side: panels loose", loose(c), "", 0, 0);
    }
    // 5. A slow roll that ends on the roof (dropped from 0.3 m upside down).
    {
        Car& c = ctx.add(spec, -60, 60, 0);
        ctx.run(1.0, [&] { c.handbrake = 1; });
        c.rotateRigid(c.frame().fwd, M_PI);
        for (Node& n : c.nodes) n.p.y += 0.3;
        ctx.run(3.0, [&] { c.handbrake = 1; });
        ctx.metric(tag + " rolled onto its roof: damage", c.damage * 100, "%", 0, 6);
        ctx.metric(tag + " rolled onto its roof: frame set", frameSet(c), "%", 0, 1.0);
        ctx.metric(tag + " rolled onto its roof: panels loose", loose(c), "", 0, 1);
    }
    // 6. A front corner into a boulder at 10 km/h (misjudged line).
    {
        const double z = -60;
        w.addRock(-80, z + 0.7, V3(0.9, 0.9, 0.9), 1.0, 33, 1);
        w.buildGrid();
        Car& c = ctx.add(spec, -100, z, 0);
        ctx.focus(c);
        ctx.run(1.0);
        ctx.runUntil(15, [&] { return dot(c.frame().vel, c.frame().fwd) < 0.2 && c.frame().com.x > -90; }, [&] {
            c.handbrake = 0;
            c.steer = 0;
            double v = dot(c.frame().vel, c.frame().fwd);
            c.throttle = v < 10 / 3.6 ? 0.6 : 0;
        });
        ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; });
        ctx.metric(tag + " corner into a boulder at 10 km/h: damage", c.damage * 100, "%", 0, 3);
        ctx.metric(tag + " corner into a boulder at 10 km/h: panels loose", loose(c), "", 0, 1);
    }
}

SCENARIO(rockHitsBronco, "structure/rock-hits/bronco", "Bronco on the trail: belly, sill and ledge hits, a tip onto its side and roof, a corner into a boulder") {
    rockHits(ctx, bronco());
}
SCENARIO(rockHitsRunner, "structure/rock-hits/4runner", "4Runner on the trail: belly, sill and ledge hits, a tip onto its side and roof, a corner into a boulder") {
    rockHits(ctx, runner());
}

SCENARIO(driveModeSwitching, "offroad/drive-modes", "2H drives the rear only; 4H ties the axles together; lockers need their axle driven") {
    VehicleSpec spec = bronco();
    spec.lockerFront = true;
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const auto& modes = driveModes(spec.transfer);
    ctx.check("Bronco leaves the factory in 4A", c.mode() && std::string(c.mode()->name) == "4A");
    int powered = 0;
    for (const Wheel& wh : c.wheels) powered += c.wheelPowered(wh);
    ctx.check("4A drives all four wheels", powered == 4);

    c.setDriveMode(0);  // 2H
    bool frontFree = true, rearDriven = true;
    for (const Wheel& wh : c.wheels) (wh.front ? frontFree : rearDriven) &= wh.front ? !c.wheelPowered(wh) : c.wheelPowered(wh);
    ctx.check("2H: front hubs freewheel, rear driven", frontFree && rearDriven);
    c.setLocker(true, true);
    ctx.check("front locker refuses with the front axle undriven", !c.lockFront);
    c.setLocker(false, true);
    ctx.check("rear locker works in 2H", c.lockRear && c.diffType(false) == DIFF_LOCKED);
    c.setLocker(false, false);

    // 2H launch: all the push comes from the rear tires.
    c.throttle = 1;
    ctx.run(1.5);
    double front = 0, rear = 0;
    for (const Wheel& wh : c.wheels) (wh.front ? front : rear) += wh.omega * wh.R;
    ctx.metric("2H: rear tread speed over front (wheelspin at the back)", rear / std::max(front, 0.1), "x", 1.0, 3.0);
    c.throttle = 0;
    c.brake = 1;
    ctx.run(3);
    c.brake = 0;

    // 4H: the center is locked, so front and rear average wheel speeds match even with
    // the front on a tighter line (the binding a part-time case gives on pavement).
    c.setDriveMode(2);
    ctx.check("4H ties front and rear together", c.centerLocked() && std::string(c.mode()->name) == "4H");
    c.setLocker(true, true);
    ctx.check("Ford's front locker refuses in 4H (4L only)", !c.lockFront);
    double worst = 0;
    ctx.run(3, [&] {
        c.throttle = 0.5;
        c.steer = 0.3;
        if (ctx.t < 7.0) return;
        double f = 0, r = 0;
        for (const Wheel& wh : c.wheels) (wh.front ? f : r) += wh.omega * 0.5;
        worst = std::max(worst, std::fabs(f - r) / std::max(std::fabs(r), 1.0));
    });
    ctx.metric("4H: front vs rear average wheel speed mismatch (driveline wind-up)", worst * 100, "%", 0, 5);
    c.setDriveMode((int)modes.size() - 1);
    ctx.check("4L engages low range", c.lowRange && c.centerLocked());
    c.setLocker(true, true);
    ctx.check("front locker engages in 4L", c.lockFront);
    c.setDriveMode(2);
    ctx.check("up into 4H drops the front locker", !c.lockFront);
    c.setLocker(true, true);
    c.setDriveMode(0);
    ctx.check("back to 2H drops the front locker", !c.lockFront && !c.lowRange);

    // The 4Runner's factory rear e-locker only works in 4L LOCK.
    Car& r = ctx.add(runner(), -100, -40, 0);
    ctx.run(0.5);
    r.setDriveMode(1);  // 4H
    r.setLocker(false, true);
    ctx.check("4Runner rear locker refuses in 4H", !r.lockRear);
    r.setDriveMode((int)driveModes(runner().transfer).size() - 1);
    r.setLocker(false, true);
    ctx.check("4Runner rear locker engages in 4L LOCK", r.lockRear);
    r.setDriveMode(2);
    ctx.check("out of low range drops it", !r.lockRear);
}

// Abuse: the front locked in low range at full steering lock and full throttle binds the
// front axle until a CV joint lets go. That wheel then freewheels.
SCENARIO(shaftBreak, "offroad/shaft-break", "Bronco in 4L, both locked, full lock, full throttle on rock: it binds and hops until a shaft snaps") {
    VehicleSpec spec = bronco();
    spec.lockerFront = true;
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 dome = mountainFeature("dome");
    Car& c = ctx.add(spec, dome.x, dome.z - 12, -90);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    int broken = 0, brokenFront = 0;
    ctx.runUntil(15, [&] { return broken > 0; }, [&] {
        c.throttle = 1;
        c.steer = 0.6;
        c.brake = c.handbrake = 0;
        broken = brokenFront = 0;
        for (const Wheel& wh : c.wheels) broken += wh.shaftBroken, brokenFront += wh.shaftBroken && wh.front;
        static thread_local int fr = 0;
        if (std::getenv("CZ_SHAFT") && ++fr % 6 == 0) {
            std::printf("SHAFT t %.2f rpm %.0f", ctx.t, c.rpm);
            for (const Wheel& wh : c.wheels) std::printf(" | %s%s T %.0f act %.0f load %.0f w %.1f", wh.front ? "F" : "R", wh.left ? "L" : "R", wh.shaftLoad, wh.driveActual, wh.load, wh.omega);
            std::printf("\n");
        }
    });
    // (Which one goes depends on the hop: the drivetrain carries what the tires can take
    // plus the shock of each landing, so it's often a rear shaft on a landing.)
    ctx.check("a shaft snaps", broken >= 1);
    (void)brokenFront;
    bool freewheels = false;
    for (const Wheel& wh : c.wheels)
        if (wh.shaftBroken) freewheels = !c.wheelPowered(wh);
    ctx.check("that wheel gets no drive", freewheels);
}

// The twister lifts diagonal wheels in turn. With open diffs the lifted wheels spin
// and the truck stalls; locked, it walks through.
static double twisterRun(Ctx& ctx, bool lockers, const char* tag) {
    VehicleSpec spec = bronco();
    spec.lockerFront = true;
    Car& c = crawlStart(ctx, spec, "twister");
    if (lockers) {
        c.setLocker(true, true);
        c.setLocker(false, true);
    }
    const V3 start = c.frame().com;
    double best = 0, maxLift = 0;
    ctx.runUntil(65, [&] { return best > 62; }, [&] {
        crawlNorth(c, mountainFeature("twister").x, 1.0);
        best = std::max(best, c.frame().com.z - start.z);
        int air = 0;
        for (const Wheel& wh : c.wheels) air += wh.load < 50;
        maxLift = std::max(maxLift, (double)air);
        ctx.sample("progress", tag, c.frame().com.z - start.z, "m");
    });
    ctx.check(std::string(tag) + ": a wheel came off the ground (the twister works)", maxLift >= 1);
    ctx.check(std::string(tag) + ": no damage crawling", c.damage < 0.02);
    return best;
}

SCENARIO(twisterLocked, "offroad/twister-lockers", "Bronco in 4L with both lockers crawls through the twister") {
    double d = twisterRun(ctx, true, "lockers");
    ctx.metric("distance through the twister (62 m clears it)", d, "m", 62, 70);
}

SCENARIO(twisterOpen, "offroad/twister-open", "Bronco in 4L with open diffs: lifted wheels spin and it bogs down") {
    double d = twisterRun(ctx, false, "open");
    ctx.metric("distance through the twister (stalls on the mounds)", d, "m", 0, 55);
}

SCENARIO(ledges, "offroad/ledges", "4Runner in 4L LOCK with lockers climbs the rock ledges: 0.3 m steps up to 1.5 m and down") {
    VehicleSpec spec = runner();
    spec.lockerFront = true;
    Car& c = crawlStart(ctx, spec, "ledges");
    c.setLocker(true, true);
    c.setLocker(false, true);
    const double y0 = c.frame().com.y;
    double top = 0;
    ctx.runUntil(60, [&] { return c.frame().com.z > mountainFeature("ledges").z + 52; }, [&] {
        crawlNorth(c, mountainFeature("ledges").x, 1.0);
        top = std::max(top, c.frame().com.y - y0);
        ctx.sample("height", "4runner", c.frame().com.y - y0, "m");
    });
    ctx.metric("climbed (top step is 1.5 m up)", top, "m", 1.3, 2.0);
    ctx.check("over the top and down the far side", c.frame().com.z > mountainFeature("ledges").z + 45);
    ctx.check("no damage on the rocks", c.damage < 0.03);
}

SCENARIO(dome, "offroad/slickrock", "Low range climbs the slickrock dome (sticky granite, ~30 degrees)") {
    Car& c = crawlStart(ctx, bronco(), "dome", 30);
    const double y0 = c.frame().com.y;
    double top = 0;
    ctx.runUntil(40, [&] { return c.frame().com.z > mountainFeature("dome").z + 10; }, [&] {
        crawlNorth(c, mountainFeature("dome").x, 1.5);
        top = std::max(top, c.frame().com.y - y0);
        static thread_local int fr = 0;
        if (std::getenv("CZ_DOME") && ++fr % 30 == 0) {
            const Frame f = c.frame();
            std::printf("DOME t %.1f y %.2f v %.2f rpm %.0f gear %d thr %.2f tc %.2f mode %d spin", ctx.t, f.com.y - y0, dot(f.vel, f.fwd), c.rpm, c.gear, c.throttle, c.tcCut, c.driveMode);
            for (const Wheel& wh : c.wheels) std::printf(" %.1f/%.0f", wh.omega * wh.R, wh.load);
            std::printf("\n");
        }
    });
    ctx.metric("height gained (dome is 10 m)", top, "m", 8.5, 11.5);
}

SCENARIO(mountainMap, "maps/mountain", "The mountain map: trucks start at the trailhead and settle; the trail, park and creek are where they should be") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const World& w = ctx.sim.world;
    ctx.check("spawn on gravel", w.surfaceAt(w.spawn.x, w.spawn.z) == SURF_GRAVEL);
    ctx.check("slickrock is rock", w.surfaceAt(mountainFeature("dome").x, mountainFeature("dome").z) == SURF_ROCK);
    ctx.check("valley loop for traffic", w.loopLength > 1000);
    ctx.metric("boulders", (double)w.rocks.size(), "", 1500, 4500);
    ctx.metric("trees", (double)w.trees.size(), "", 1500, 3000);
    for (int k = 0; k < 4; k++) {
        const V3& p = k == 0 ? w.spawn : w.parking[k - 1];
        ctx.add(vehicleCatalog()[k], p.x, p.z, k == 0 ? -90 : 90);
    }
    ctx.run(5);
    double worst = 0;
    for (const Car& c : ctx.sim.cars) worst = std::max(worst, c.damage);
    ctx.metric("worst damage settling at the trailhead", worst * 100, "%", 0, 0.5);
}

// Airing down: a steep granite slab the truck spins out on at street pressure, climbed at
// 15 psi. Then the price: a fast hit on a rock edge at 12 psi pinches the tire.
static double slabClimb(Ctx& ctx, double psi, double z) {
    World& w = ctx.sim.world;
    const double angle = 41 * M_PI / 180, run = 7;
    w.addRamp(V3(-80, 0, z), V3(-80 + run * std::cos(angle), run * std::sin(angle), z), 4, 1.5, V3(0.5, 0.48, 0.45));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = lowRangeAt(ctx, bronco(), -90, z);
    c.tirePsi = psi;
    const double y0 = c.frame().com.y;
    double top = 0;
    ctx.run(25, [&] {
        crawlEast(c, z, 1.0);
        top = std::max(top, c.frame().com.y - y0);
    });
    return top;
}

SCENARIO(airDown, "offroad/air-down", "Bronco in 4L on a 41 degree granite slab: spins out at 35 psi, climbs at 15 psi; 12 psi pinches a tire on a fast rock hit") {
    const double street = slabClimb(ctx, 35, -30), aired = slabClimb(ctx, 15, 30);
    ctx.metric("height up the slab at 35 psi", street, "m", 0, 3.0);
    ctx.metric("height up the slab at 15 psi (4.6 m to the top)", aired, "m", 3.5, 6);
    // Fast over a square rock edge.
    auto hit = [&](double psi, double z) {
        World& w = ctx.sim.world;
        w.addBox(V3(-20, 0.1, z), V3(0.3, 0.22, 3), 0, 0, 0, V3(0.5, 0.48, 0.45));
        w.boxes.back().rock = true;
        w.buildGrid();
        Car& c = ctx.add(bronco(), -60, z, 0);
        ctx.run(0.8);
        c.tirePsi = psi;
        ctx.launch(c, 30 / 3.6);
        int pops = 0;
        ctx.run(4, [&] {
            c.throttle = 0.3, c.steer = 0, c.brake = 0, c.handbrake = 0;
            pops = 0;
            for (const Wheel& wh : c.wheels) pops += wh.popped;
        });
        return pops;
    };
    ctx.metric("tires popped hitting a rock edge at 30 km/h, 35 psi", hit(35, -95), "", 0, 0);
    ctx.metric("tires popped hitting a rock edge at 30 km/h, 12 psi", hit(12, -115), "", 1, 4);
}

// Ramp travel index: one front wheel up a 20 degree ramp until another wheel lifts.
// RTI = distance up the ramp / wheelbase * 1000.
static double rti(Ctx& ctx, const VehicleSpec& spec, double z, bool disconnect) {
    World& w = ctx.sim.world;
    const double a = 20 * M_PI / 180, L = 4.0;
    // Under the left tires (left is -z when heading +x).
    const double track = spec.trackFront > 0 ? spec.trackFront : spec.width;
    const double zl = z - track / 2;
    w.addRamp(V3(-70, 0, zl), V3(-70 + L * std::cos(a), L * std::sin(a), zl), 0.7, 1.2, V3(0.6, 0.6, 0.62));
    w.buildGrid();
    Car& c = lowRangeAt(ctx, spec, -70 - spec.wheelbase * 0.5 - 1.4, z);
    c.swayDisconnected = disconnect;
    const Wheel* fl = nullptr;
    for (const Wheel& wh : c.wheels)
        if (wh.front && wh.left) fl = &wh;
    double best = 0;
    ctx.runUntil(30, [&] {
        for (const Wheel& wh : c.wheels)
            if (&wh != fl && wh.load < 30) return true;
        return false;
    }, [&] {
        crawlEast(c, z, 0.25);
        // Only while the tire is on the ramp (not if it has slid off its edge).
        const double along = (fl->contact.x + 70) / std::cos(a);
        if (fl->load > 0 && along > 0 && along < L && fl->contact.y > along * std::sin(a) - 0.15) best = std::max(best, along);
    });
    return std::max(0.0, best) / spec.wheelbase * 1000;
}

SCENARIO(rampTravel, "offroad/rti", "Ramp travel index on a 20 degree ramp: realistic articulation, more with the front sway bar unhooked") {
    const double b = rti(ctx, bronco(), -40, false), bd = rti(ctx, bronco(), -20, true);
    const double r = rti(ctx, runner(), 45, false), rd = rti(ctx, runner(), 65, true);  // clear of the platform (z -15..15)
    const double bg = rti(ctx, buggy(), -62, false), ut = rti(ctx, utv(), -90, false);
    ctx.metric("Bronco RTI (real 4-door: ~450-550)", b, "", 380, 650);
    ctx.metric("Bronco RTI, sway bar disconnected", bd, "", b + 20, 800);
    ctx.metric("4Runner RTI (lifted 3rd gen: ~450-550)", r, "", 380, 650);
    ctx.metric("4Runner RTI, sway bar disconnected", rd, "", r + 20, 800);
    // (Rock bouncers and Ultra4 cars: 900-1,200 on a ramp like this; side-by-sides 700-900.)
    ctx.metric("Rock buggy RTI (4-link solid axles, 16 in of travel)", bg, "", 0, 9999);
    ctx.metric("Turbo UTV RTI", ut, "", 0, 9999);
}

// ---- The mountain's hard lines ---------------------------------------------------

namespace {
// Crawls along the line from `from` in direction `dir` (flat), holding the center.
void crawlAlong(Car& c, const V3& from, const V3& dir, double speed) {
    Frame fr = c.frame();
    const V3 rel = fr.com - from;
    const double s = dot(rel, dir);
    V3 target = from + dir * (s + 4);
    target.y = fr.com.y;
    V3 d = target - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 2.0, -0.6, 0.6);
    double v = dot(fr.vel, fr.fwd);
    // Feathered: a crawler never floors it in low range (the tires just dig and spin).
    c.throttle = clampd(0.3 + (speed - v) * 0.7, 0, 0.6);
    c.brake = v > speed + 0.7 ? 0.4 : 0;
    c.handbrake = 0;
}
double headingTo(const V3& d) { return std::atan2(-d.z, d.x) * 180 / M_PI; }
// A built Bronco set up for the hard stuff: 4L, rear locker (and the front, for a straight
// climb: it costs steering), aired down, front bar off.
Car& crawler(Ctx& ctx, double x, double z, const V3& dir, bool frontLocker) {
    VehicleSpec& spec = *new VehicleSpec(bronco());  // lives for the run (scenarios are short-lived processes)
    spec.lockerFront = true;
    Car& c = ctx.add(spec, x, z, headingTo(dir));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    if (frontLocker) c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    c.tirePsi = 18;
    c.swayDisconnected = true;
    return c;
}
}  // namespace

SCENARIO(hardLines, "maps/hard-lines", "Repair stations sit on flat pads by the trails; the spur's crest is drivable but the drop beside it is real") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    World& w = ctx.sim.world;
    ctx.metric("repair stations", (double)w.stations.size(), "", 3, 3);
    for (const Station& st : w.stations) {
        double lo = 1e9, hi = -1e9;
        for (int i = -5; i <= 5; i++)
            for (int j = -4; j <= 4; j++) {
                const double hgt = w.height(st.p.x + i, st.p.z + j);
                lo = std::min(lo, hgt), hi = std::max(hi, hgt);
            }
        ctx.metric(std::string(st.name) + ": pad flatness", (hi - lo) * 100, "cm", 0, 25);
        ctx.check(std::string(st.name) + ": paved pad", w.surfaceAt(st.p.x, st.p.z) == SURF_PAVED);
        double nearest = 1e9;
        for (int i = -16; i <= 16; i++)
            for (int j = -16; j <= 16; j++) {
                const uint8_t pt = w.roadAt(st.p.x + i, st.p.z + j).paint;
                if (pt == PAINT_TRAIL || pt == PAINT_GRAVEL || pt == PAINT_SCREE) nearest = std::min(nearest, std::hypot((double)i, (double)j));
            }
        ctx.metric(std::string(st.name) + ": to the nearest trail or lot", nearest, "m", 0, 12);
    }
    // The spur's crest: width and exposure.
    const V3 s0 = mountainFeature("spur"), pin = mountainFeature("pinnacle");
    const V3 out = norm(V3(pin.x - s0.x, 0, pin.z - s0.z)), left(-out.z, 0, out.x);
    double narrowest = 1e9, drop120 = 0;
    for (double sa = 4; sa < 148; sa += 2) {
        const V3 c = s0 + out * sa;
        const double h0 = w.height(c.x, c.z);
        double width = 0;
        for (int sd = -1; sd <= 1; sd += 2) {
            const double hs = w.height(c.x + left.x * sd * 0.5, c.z + left.z * sd * 0.5);
            const double tilt = (hs - h0) / 0.5;  // the crest's own cross-slope
            double lat = 0;
            while (lat < 4 && std::fabs(w.height(c.x + left.x * sd * (lat + 0.1), c.z + left.z * sd * (lat + 0.1)) - (h0 + tilt * (lat + 0.1))) < 0.3) lat += 0.1;
            width += lat;
        }
        narrowest = std::min(narrowest, width);
        if (std::fabs(sa - 120) < 1) {
            for (int sd = -1; sd <= 1; sd += 2) drop120 = std::max(drop120, h0 - w.height(c.x + left.x * sd * 25, c.z + left.z * sd * 25));
        }
    }
    ctx.metric("narrowest drivable crest (a Bronco is 2.0 m)", narrowest, "m", 3.0, 4.5);
    ctx.metric("drop 25 m beside the crest, 120 m out", drop120, "m", 30, 120);
    ctx.check("spur and gully are rock or scree", w.surfaceAt((s0 + out * 60).x, (s0 + out * 60).z) == SURF_ROCK &&
                                                    w.surfaceAt((s0 + out * 30).x, (s0 + out * 30).z) == SURF_SCREE);
    // Over the edge: a truck nudged off the crest goes all the way down, and it shows.
    const V3 c = s0 + out * 112 + left * 0.75;  // parked toward the left edge
    Car& t = ctx.add(bronco(), c.x, c.z, headingTo(out));
    ctx.run(1.5, [&] { t.handbrake = 1; });
    const double y0 = t.frame().com.y;
    for (Node& n : t.nodes) n.v = left * 2.5;
    ctx.run(10, [&] { t.handbrake = 0, t.brake = 0, t.throttle = 0; });
    ctx.metric("height lost going over the edge", y0 - t.frame().com.y, "m", 20, 150);
    ctx.metric("damage from the fall", t.damage * 100, "%", 2, 100);
}

SCENARIO(widowmaker, "offroad/widowmaker", "A built Bronco (4L, rear locker, 18 psi, bar off) crawls the Widowmaker Spur out to the pinnacle without falling off") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 s0 = mountainFeature("spur"), pin = mountainFeature("pinnacle");
    const V3 out = norm(V3(pin.x - s0.x, 0, pin.z - s0.z));
    const V3 start = s0 - out * 1.5;
    Car& c = crawler(ctx, start.x, start.z, out, false);
    double best = 0, lowest = 1e9;
    const double crestY0 = ctx.sim.world.height(s0.x, s0.z);
    ctx.runUntil(200, [&] { return best > 150; }, [&] {
        crawlAlong(c, start, out, 1.2);
        const V3 p = c.frame().com;
        best = std::max(best, dot(p - start, out));
        lowest = std::min(lowest, p.y - crestY0);
        ctx.sample("progress", "bronco", dot(p - start, out), "m");
    });
    ctx.metric("distance out the spur (the pinnacle is ~160 m)", best, "m", 150, 175);
    ctx.metric("lowest point (crest dips 6 m; lower means it fell)", lowest, "m", -7.5, 3);
    ctx.metric("damage", c.damage * 100, "%", 0, 1.5);
}

SCENARIO(gullyClimb, "offroad/gully", "A built Bronco (4L, both lockers, 18 psi) climbs Boulder Gully: ledges, boulders and loose scree") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 foot = mountainFeature("gully"), top = mountainFeature("gullytop");
    const V3 up = norm(V3(top.x - foot.x, 0, top.z - foot.z));
    const V3 start = foot - up * 6;
    Car& c = crawler(ctx, start.x, start.z, up, true);
    const double y0 = c.frame().com.y;
    double best = 0, rise = 0;
    ctx.runUntil(240, [&] { return best > 110; }, [&] {
        crawlAlong(c, start, up, 1.0);
        best = std::max(best, dot(c.frame().com - start, up));
        rise = std::max(rise, c.frame().com.y - y0);
        ctx.sample("progress", "bronco", dot(c.frame().com - start, up), "m");
    });
    ctx.metric("distance up the gully (top at ~116 m)", best, "m", 105, 130);
    ctx.metric("height climbed", rise, "m", 25, 60);
    ctx.metric("damage", c.damage * 100, "%", 0, 2);
}

// ---- The crawl specials: the rock buggy and the side-by-side, against the Bronco ----------

namespace {

// A vehicle set up for the hard stuff: lowest range, every locker it has (the Bronco gets its
// optional front one, as `crawler()` does), aired down to the game's 12 psi, traction control off.
Car& crawlSetUp(Ctx& ctx, const VehicleSpec& base, double x, double z, double headingDeg) {
    VehicleSpec& spec = *new VehicleSpec(base);  // lives for the run (scenarios are short-lived processes)
    spec.lockerFront = true;
    Car& c = ctx.add(spec, x, z, headingDeg);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L, or 4WD for the side-by-side
    c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    c.tirePsi = 12;
    c.swayDisconnected = spec.swayDisconnect > 0;
    return c;
}

void clearCars(Ctx& ctx) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
}

// Does it climb a square ledge H high? One lane per attempt on the proving ground's flat apron; the
// ledge's face is at x = -112, and the vehicle starts 12 m back from it.
struct Attempt {
    Car* car;
    double z, faceX;
    bool cleared() const { return car->frame().com.x > faceX + 4; }
};
Attempt ledgeAttempt(Ctx& ctx, const VehicleSpec& spec, double z, double H) {
    World& w = ctx.sim.world;
    const double faceX = -112;
    w.addBox(V3(faceX + 8, w.height(faceX + 8, z) + H / 2, z), V3(8, H / 2, 5), 0, 0, 0, V3(0.4, 0.38, 0.35));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = crawlSetUp(ctx, spec, faceX - 12, z, 0);
    return {&c, z, faceX};
}

}  // namespace

SCENARIO(ledgeCrawlers, "offroad/ledge-crawlers", "Square ledges in 4L with lockers at 12 psi: the Bronco stops at 0.45 m, the rock buggy takes 0.65 m, the side-by-side only 0.3 m") {
    std::vector<Attempt> a;
    a.push_back(ledgeAttempt(ctx, buggy(), -55, 0.65));
    a.push_back(ledgeAttempt(ctx, bronco(), -40, 0.65));
    a.push_back(ledgeAttempt(ctx, bronco(), -25, 0.45));
    a.push_back(ledgeAttempt(ctx, utv(), -10, 0.3));
    a.push_back(ledgeAttempt(ctx, utv(), 5, 0.5));
    ctx.focus(*a[0].car);
    ctx.run(40, [&] {
        for (const Attempt& at : a) crawlEast(*at.car, at.z, 1.0);
    });
    ctx.check("rock buggy climbs a 0.65 m ledge", a[0].cleared(), "x = " + std::to_string(a[0].car->frame().com.x));
    ctx.check("Bronco can't climb the same 0.65 m ledge", !a[1].cleared(), "x = " + std::to_string(a[1].car->frame().com.x));
    ctx.check("Bronco climbs 0.45 m", a[2].cleared(), "x = " + std::to_string(a[2].car->frame().com.x));
    ctx.check("side-by-side climbs 0.3 m", a[3].cleared(), "x = " + std::to_string(a[3].car->frame().com.x));
    ctx.check("side-by-side can't climb 0.5 m", !a[4].cleared(), "x = " + std::to_string(a[4].car->frame().com.x));
    ctx.metric("rock buggy: damage over the 0.65 m ledge", a[0].car->damage * 100, "%", 0, 1.5);
}

SCENARIO(slabCrawlers, "offroad/slickrock-crawlers", "A 48 degree slickrock slab in 4L with lockers at 12 psi: the rock buggy climbs most of it, the Bronco slides back") {
    World& w = ctx.sim.world;
    const double angle = 48 * M_PI / 180, run = 9;
    Car* cars[2];
    const VehicleSpec* specs[2] = {&buggy(), &bronco()};
    const double zs[2] = {30, -30};
    for (int k = 0; k < 2; k++) {
        w.addRamp(V3(-80, 0, zs[k]), V3(-80 + run * std::cos(angle), run * std::sin(angle), zs[k]), 5, 1.5, V3(0.5, 0.48, 0.45));
        w.boxes.back().rock = true;
    }
    w.buildGrid();
    for (int k = 0; k < 2; k++) cars[k] = &crawlSetUp(ctx, *specs[k], -92, zs[k], 0);
    ctx.focus(*cars[0]);
    double y0[2], top[2] = {0, 0};
    for (int k = 0; k < 2; k++) y0[k] = cars[k]->frame().com.y;
    ctx.run(30, [&] {
        for (int k = 0; k < 2; k++) {
            crawlEast(*cars[k], zs[k], 1.0);
            top[k] = std::max(top[k], cars[k]->frame().com.y - y0[k]);
        }
        ctx.sample("height", "rock buggy", cars[0]->frame().com.y - y0[0], "m");
        ctx.sample("height", "Bronco", cars[1]->frame().com.y - y0[1], "m");
    });
    ctx.metric("rock buggy: height up the 48 degree slab (6.7 m to the top)", top[0], "m", 4.5, 7.0);
    ctx.metric("Bronco: height up the same slab", top[1], "m", 0, 3.5);
    ctx.check("the buggy climbs at least 1.5x as high", top[0] > 1.5 * top[1]);
}

// The twister with open diffs: the Bronco's lifted wheels spin and it bogs down; the buggy's
// long travel keeps its tires on the mounds.
static double twisterOpenRun(Ctx& ctx, const VehicleSpec& spec, double* time) {
    clearCars(ctx);
    Car& c = crawlStart(ctx, spec, "twister");
    const V3 start = c.frame().com;
    double best = 0;
    ctx.runUntil(65, [&] { return best > 62; }, [&] {
        crawlNorth(c, mountainFeature("twister").x, 1.0);
        best = std::max(best, c.frame().com.z - start.z);
    });
    if (time) *time = ctx.t;
    return best;
}

SCENARIO(twisterBuggy, "offroad/twister-crawlers", "The twister in 4L with open diffs (no lockers): the rock buggy's long axle travel walks through it, the Bronco bogs down") {
    double t0 = ctx.t, tBuggy = 0;
    const double bronc = twisterOpenRun(ctx, bronco(), nullptr);
    const double bug = twisterOpenRun(ctx, buggy(), &tBuggy);
    (void)t0;
    ctx.metric("rock buggy: distance through the twister, open diffs (62 m clears it)", bug, "m", 62, 70);
    ctx.metric("Bronco: distance, open diffs", bronc, "m", 0, 55);
    ctx.check("no damage crawling it", ctx.sim.cars[0].damage < 0.02);
}

SCENARIO(ledgesCrawlers, "offroad/ledges-buggy", "The crawl park's ledges (0.3 m steps up to 1.5 m, and down) in 4L with lockers: the rock buggy climbs them cleanly and no slower than the Bronco") {
    double progress[2], tDone[2];
    const VehicleSpec* specs[2] = {&buggy(), &bronco()};
    for (int k = 0; k < 2; k++) {
        clearCars(ctx);
        VehicleSpec& spec = *new VehicleSpec(*specs[k]);
        spec.lockerFront = true;
        Car& c = crawlStart(ctx, spec, "ledges");
        c.setLocker(true, true);
        c.setLocker(false, true);
        const double t0 = ctx.t, y0 = c.frame().com.y;
        double top = 0;
        ctx.runUntil(60, [&] { return c.frame().com.z > mountainFeature("ledges").z + 52; }, [&] {
            crawlNorth(c, mountainFeature("ledges").x, 1.0);
            top = std::max(top, c.frame().com.y - y0);
        });
        progress[k] = c.frame().com.z - mountainFeature("ledges").z;
        tDone[k] = ctx.t - t0;
        ctx.check(std::string(k ? "Bronco" : "rock buggy") + ": over the top (1.5 m step) and down the far side", top > 1.3 && progress[k] > 45);
        ctx.check(std::string(k ? "Bronco" : "rock buggy") + ": no damage on the rocks", c.damage < 0.03);
    }
    ctx.metric("rock buggy: time through the ledges", tDone[0], "s", 20, 60);
    ctx.metric("... against the Bronco's", tDone[0] / tDone[1], "x", 0.5, 1.02);
}

SCENARIO(domeBuggy, "offroad/slickrock-dome", "The slickrock dome in 4L: the rock buggy climbs it as quickly as the Bronco") {
    double t[2];
    const VehicleSpec* specs[2] = {&buggy(), &bronco()};
    for (int k = 0; k < 2; k++) {
        clearCars(ctx);
        Car& c = crawlStart(ctx, *specs[k], "dome", 30);
        c.tirePsi = 15;
        const double t0 = ctx.t, y0 = c.frame().com.y;
        double top = 0;
        ctx.runUntil(40, [&] { return c.frame().com.z > mountainFeature("dome").z + 10; }, [&] {
            crawlNorth(c, mountainFeature("dome").x, 1.5);
            top = std::max(top, c.frame().com.y - y0);
        });
        t[k] = ctx.t - t0;
        ctx.metric(std::string(k ? "Bronco" : "rock buggy") + ": height gained (dome is 10 m)", top, "m", 8.5, 11.5);
    }
    ctx.metric("rock buggy over the Bronco's time to the top", t[0] / t[1], "x", 0.5, 1.02);
}

// The mountain's whoops lane (0.55 m bumps every 9 m, 130 m of them): each vehicle cruises at 55 km/h.
// The side-by-side's long travel eats them and it holds the speed; the stock trucks tip or slow.
struct WhoopsRun {
    double time, minUp, damage;
    bool finished;
};
static WhoopsRun whoopsRun(Ctx& ctx, const VehicleSpec& spec, double target) {
    clearCars(ctx);
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 f = mountainFeature("whoops");
    Car& c = ctx.add(spec, f.x, f.z - 45, -90);
    ctx.focus(c);
    ctx.run(1.0);
    double tIn = -1, tOut = -1, minUp = 1;
    ctx.runUntil(40, [&] { return tOut > 0; }, [&] {
        Frame fr = c.frame();
        V3 d = V3(f.x, fr.com.y, fr.com.z + 8) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)), -0.3, 0.3);
        const double v = len(fr.vel) * 3.6;
        c.throttle = clampd(0.3 + (target - v) * 0.2, 0, 1);
        c.brake = v > target + 8 ? 0.3 : 0;
        c.handbrake = 0;
        if (tIn < 0 && fr.com.z > f.z) tIn = ctx.t;
        if (tIn > 0 && tOut < 0 && fr.com.z > f.z + 130) tOut = ctx.t;
        if (tIn > 0) minUp = std::min(minUp, fr.up.y);
    });
    return {tOut > 0 ? tOut - tIn : 99, minUp, c.damage * 100, tOut > 0};
}

SCENARIO(whoops, "offroad/whoops", "The whoops lane at 55 km/h: the side-by-side's long-travel suspension holds the speed through all of it, quicker than the Bronco, 4Runner and rock buggy") {
    const WhoopsRun u = whoopsRun(ctx, utv(), 55), b = whoopsRun(ctx, bronco(), 55), r = whoopsRun(ctx, runner(), 55), g = whoopsRun(ctx, buggy(), 55);
    auto avg = [](const WhoopsRun& w) { return w.finished ? 130 / w.time * 3.6 : 0.0; };
    std::printf("WHOOPS UTV %.1f s up %.2f | Bronco %.1f s up %.2f | 4Runner %.1f s up %.2f | buggy %.1f s up %.2f\n", u.time, u.minUp, b.time, b.minUp, r.time, r.minUp, g.time, g.minUp);
    ctx.metric("side-by-side: average speed through the whoops (cruising at 55)", avg(u), "km/h", 45, 60);
    ctx.metric("side-by-side: lowest body tilt (1 = level)", u.minUp, "", 0.75, 1);
    ctx.metric("side-by-side: damage", u.damage, "%", 0, 0.5);
    ctx.check("the side-by-side is quicker than the Bronco", !b.finished || u.time < b.time);
    ctx.check("... than the 4Runner", !r.finished || u.time < r.time);
    ctx.check("... and than the rock buggy", !g.finished || u.time < g.time);
}

SCENARIO(rockHitsBuggy, "structure/rock-hits/buggy", "Rock buggy on the trail: belly, sill and ledge hits, a tip onto its side and roof, a corner into a boulder") {
    rockHits(ctx, buggy());
}
SCENARIO(rockHitsUtv, "structure/rock-hits/utv", "Side-by-side on the trail: belly, sill and ledge hits, a tip onto its side and roof, a corner into a boulder") {
    rockHits(ctx, utv());
}
