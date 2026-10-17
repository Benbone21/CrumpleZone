// Gamepad rumble as an event signal: silent at idle and cruising (pavement, gravel), a light
// intermittent texture on rock, and clear short pulses for hits, landings and broken parts.
// Runs scripted drives through the same pad::Rumbler the game uses (gamepad.cpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "../src/autopilot.h"
#include "../src/gamepad.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }
const VehicleSpec& runner() { return vehicleCatalog()[3]; }

constexpr int kLevel = 1;  // (the default: LOW)
thread_local double gHz = 60;  // (frames per second the drives are stepped at)

// What the pad was asked for over a drive: how many frames had any rumble at all, how hard.
struct Duty {
    int frames = 0, on = 0, pulses = 0;
    double sum = 0, peak = 0, lowPeak = 0;
    bool was = false;
    std::vector<float> trace;  // (the level asked for, every frame)
    int run = 0, longest = 0;  // (the longest unbroken stretch of rumble, in frames)
    void add(const pad::RumbleOut& o) {
        const double m = std::max(o.low, o.high);
        const bool now = m > 0.01;
        frames++, on += now, pulses += now && !was, sum += m, peak = std::max(peak, m), lowPeak = std::max(lowPeak, (double)o.low), was = now;
        trace.push_back((float)m);
        run = now ? run + 1 : 0, longest = std::max(longest, run);
    }
    // The duty cycle and peak of the last `seconds` of the drive.
    double tailDuty(double seconds) const {
        const int n = std::min((int)trace.size(), (int)(seconds * gHz));
        int k = 0;
        for (size_t i = trace.size() - n; i < trace.size(); i++) k += trace[i] > 0.01f;
        return n ? (double)k / n : 0;
    }
    double duty() const { return frames ? (double)on / frames : 0; }
    double mean() const { return frames ? sum / frames : 0; }
};


// One frame of `dt` seconds, the way the game steps (not the harness's fixed 60 Hz).
void tick(Ctx& ctx) {
    const double dt = 1.0 / gHz;
    ctx.sim.updateAI(dt);
    ctx.sim.advance(dt, 1000);
    for (Car& c : ctx.sim.cars) c.updateStats();
    ctx.t += dt;
}
void runFor(Ctx& ctx, double seconds, const std::function<bool()>& done, const std::function<void()>& each) {
    const double end = ctx.t + seconds;
    while (ctx.t < end && !(done && done())) {
        each();
        tick(ctx);
    }
}

// One drive's rumble, frame by frame as the game would send it.
struct Feed {
    pad::Rumbler r;
    Duty d;
    int lvl = kLevel;
    void frame(Car& c, double dt) {
        d.add(r.update(c, lvl, dt, c.impactEnergy));
        c.impactEnergy = 0;  // (the game takes this after every frame too)
    }
};

void report(const char* what, const Feed& f) {
    const Duty& d = f.d;
    std::printf("RUMBLE %-22s duty %5.1f %% mean %.3f peak %.2f pulses %d\n", what, d.duty() * 100, d.mean(), d.peak, d.pulses);
}

// Steady speed along +X with a lazy weave (what cruising looks like).
void cruise(Car& c, double kmh, double z, double t) {
    const Frame f = c.frame();
    const double v = dot(f.vel, f.fwd) * 3.6;
    c.throttle = clampd(0.25 + (kmh - v) * 0.05, 0, 1);
    c.brake = v > kmh + 8 ? 0.3 : 0;
    c.handbrake = 0;
    const V3 d = V3(f.com.x + 8, f.com.y, z + 1.2 * std::sin(t * 0.8)) - f.com;
    c.steer = clampd(std::atan2(dot(d, f.left), std::max(dot(d, f.fwd), 0.5)) * 1.2, -0.3, 0.3);
}

Car& startOn(Ctx& ctx, const VehicleSpec& spec, V3 p, double heading = 0) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Car& c = ctx.add(spec, p.x, p.z, heading);
    ctx.focus(c);
    ctx.run(1.5);
    return c;
}

Feed idleDrive(Ctx& ctx) {
    Car& c = startOn(ctx, bronco(), V3(-110, 0, 40));
    Feed f;
    runFor(ctx, 6.0, nullptr, [&] {
        c.throttle = 0, c.brake = 1;
        f.frame(c, 1.0 / gHz);
    });
    return f;
}

Feed roadDrive(Ctx& ctx, Surface s, double kmh) {
    const V3 p = s == SURF_PAVED ? V3(-110, 0, 40) : World(MAP_PROVING_GROUND).courseStart(s) + V3(8, 0, 0);
    Car& c = startOn(ctx, bronco(), p);
    ctx.launch(c, kmh / 3.6);
    Feed f;
    const double z = c.frame().com.z, x0 = c.frame().com.x, t0 = ctx.t;
    runFor(ctx, 12, [&] { return c.frame().com.x - x0 > (s == SURF_PAVED ? 100 : 55); }, [&] {
        cruise(c, kmh, z, ctx.t - t0);
        f.frame(c, 1.0 / gHz);
    });
    return f;
}

// Crawls the mountain map's ledges (0.3 m steps up to 1.5 m) in 4L at a walking pace.
Feed rockCrawl(Ctx& ctx) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_MOUNTAIN);
    VehicleSpec spec = runner();
    spec.lockerFront = true;
    static std::vector<VehicleSpec> keep;
    keep.reserve(8);
    keep.push_back(spec);
    const V3 f = mountainFeature("ledges");
    Car& c = ctx.add(keep.back(), f.x, f.z - 6, -90);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(c.spec->transfer).size() - 1);
    c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    Feed fd;
    runFor(ctx, 60, [&] { return c.frame().com.z > f.z + 52; }, [&] {
        const Frame fr = c.frame();
        const V3 d = V3(f.x, fr.com.y, fr.com.z + 6) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
        const double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (1.0 - v) * 0.9, 0, 1);
        c.brake = v > 2.0 ? 0.3 : 0;
        c.handbrake = 0;
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

// The mountain's summit trail (a graded dirt road, switchbacks and washboard) at 40 km/h, on the autopilot.
Feed trailDrive(Ctx& ctx) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 f = mountainFeature("trailhead");
    Car& c = ctx.add(bronco(), f.x, f.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    pilotStart(c, ctx.sim.world, mountainFeature("lookout"), "UP", 40 / 3.6, PILOT_TRAILS);
    Feed fd;
    runFor(ctx, 40, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(0, 1.0 / gHz);
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

// The whoops lane at 55 km/h.
Feed whoopsDrive(Ctx& ctx) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 f = mountainFeature("whoops");
    Car& c = ctx.add(bronco(), f.x, f.z - 45, -90);
    ctx.focus(c);
    ctx.run(1.0);
    Feed fd;
    runFor(ctx, 40, [&] { return c.frame().com.z > f.z + 130; }, [&] {
        const Frame fr = c.frame();
        const V3 d = V3(f.x, fr.com.y, fr.com.z + 8) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)), -0.3, 0.3);
        const double v = len(fr.vel) * 3.6;
        c.throttle = clampd(0.3 + (55 - v) * 0.2, 0, 1);
        c.brake = v > 63 ? 0.3 : 0;
        c.handbrake = 0;
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

// Dropped from `h` metres onto the pavement pad, then sitting there.
Feed landing(Ctx& ctx, double h) {
    Car& c = startOn(ctx, bronco(), V3(-100, 0, 55));
    for (Node& n : c.nodes) n.p.y += h;
    Feed fd;
    runFor(ctx, 3.0, nullptr, [&] {
        c.throttle = 0, c.brake = 1;
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

// Into the crash wall at x = 114 at 14 m/s.
Feed crash(Ctx& ctx) {
    ctx.sim.cars.clear();
    ctx.sim.ropes.clear();
    ctx.sim.player = 0;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Car& c = ctx.add(bronco(), 70, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 14);
    Feed fd;
    runFor(ctx, 7.0, nullptr, [&] { fd.frame(c, 1.0 / gHz); });
    return fd;
}

// Parked and floored with the revs held on the limiter for 1.5 s, then let off.
Feed limiter(Ctx& ctx, int level = kLevel) {
    Car& c = startOn(ctx, bronco(), V3(-110, 0, 40));
    Feed fd;
    fd.lvl = level;
    const double t0 = ctx.t;
    runFor(ctx, 3.0, nullptr, [&] {
        const bool pinned = ctx.t - t0 < 1.5;
        c.throttle = pinned ? 1 : 0, c.brake = 1;
        if (pinned) c.rpm = c.spec->redline;
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

// Cruising on the pavement; at 1 s `snap` breaks something, and it is followed for 3 s.
Feed breakAt(Ctx& ctx, const std::function<void(Car&)>& snap, int level = kLevel) {
    Car& c = startOn(ctx, bronco(), V3(-110, 0, 40));
    ctx.launch(c, 60 / 3.6);
    Feed fd;
    fd.lvl = level;
    const double z = c.frame().com.z, t0 = ctx.t;
    bool done = false;
    runFor(ctx, 4.0, nullptr, [&] {
        cruise(c, 60, z, ctx.t - t0);
        if (!done && ctx.t - t0 >= 1.0) snap(c), done = true;
        fd.frame(c, 1.0 / gHz);
    });
    return fd;
}

}  // namespace

SCENARIO(rumbleQuiet, "rumble/quiet", "Rumble is silent at idle, cruising on pavement, gravel and a graded dirt trail, at 60 and 144 frames a second") {
    for (double hz : {60.0, 144.0}) {
        gHz = hz;
        const std::string at = " (" + std::to_string((int)hz) + " fps)";
        const Feed idle = idleDrive(ctx), road = roadDrive(ctx, SURF_PAVED, 80), gravel = roadDrive(ctx, SURF_GRAVEL, 50), trail = trailDrive(ctx);
        report(("idle" + at).c_str(), idle), report(("pavement 80 km/h" + at).c_str(), road);
        report(("gravel 50 km/h" + at).c_str(), gravel), report(("summit trail 40 km/h" + at).c_str(), trail);
        ctx.metric("idle in park: share of frames with rumble" + at, idle.d.duty() * 100, "%", 0, 0);
        ctx.metric("pavement at 80 km/h" + at, road.d.duty() * 100, "%", 0, 0);
        ctx.metric("gravel at 50 km/h" + at, gravel.d.duty() * 100, "%", 0, 0);
        ctx.metric("dirt trail at 40 km/h" + at, trail.d.duty() * 100, "%", 0, 0.5);
    }
}

SCENARIO(rumbleRough, "rumble/rough", "Rough ground only gets a light, sparse tick: rock crawling is mostly quiet, the whoops pulse but never buzz") {
    for (double hz : {60.0, 144.0}) {
        gHz = hz;
        const std::string at = " (" + std::to_string((int)hz) + " fps)";
        const Feed rock = rockCrawl(ctx), whoops = whoopsDrive(ctx);
        report(("rock crawl" + at).c_str(), rock), report(("whoops 55 km/h" + at).c_str(), whoops);
        ctx.metric("rock crawl: share of frames with rumble" + at, rock.d.duty() * 100, "%", 0, 25);
        ctx.metric("rock crawl: strongest tick" + at, rock.d.peak, "0..1", 0, 0.35);
        ctx.metric("whoops: share of frames with rumble" + at, whoops.d.duty() * 100, "%", 0, 45);
        ctx.metric("whoops: longest unbroken stretch of rumble" + at, whoops.d.longest / hz, "s", 0, 0.8);
        ctx.check("the whoops give pulses" + at, whoops.d.pulses >= 5);
    }
}

SCENARIO(rumbleEvents, "rumble/events", "Hits, landings, broken parts and the rev limiter each give a clear short pulse, scaled by severity, and then it goes quiet") {
    for (double hz : {60.0, 144.0}) {
        gHz = hz;
        const std::string at = " (" + std::to_string((int)hz) + " fps)";
        const Feed wall = crash(ctx), drop3 = landing(ctx, 3), drop1 = landing(ctx, 1);
        report(("crash 14 m/s" + at).c_str(), wall), report(("landing 3 m" + at).c_str(), drop3), report(("landing 1 m" + at).c_str(), drop1);
        ctx.metric("crash: the heavy motor" + at, wall.d.lowPeak, "0..1", 0.4, 1);
        ctx.metric("crash: share of frames with rumble" + at, wall.d.duty() * 100, "%", 0.1, 25);
        ctx.metric("crash: rumble in the last 2 s of the wreck sitting there" + at, wall.d.tailDuty(2.0) * 100, "%", 0, 0);
        ctx.metric("3 m landing: strongest pulse" + at, drop3.d.peak, "0..1", 0.3, 1);
        ctx.metric("1 m landing: strongest pulse" + at, drop1.d.peak, "0..1", 0.15, 1);
        ctx.check("a harder landing is a harder pulse" + at, drop3.d.peak >= drop1.d.peak);
        ctx.metric("3 m landing: share of frames with rumble" + at, drop3.d.duty() * 100, "%", 0.1, 35);
        const Feed shaft = breakAt(ctx, [](Car& c) { c.wheels[0].shaftBroken = true; });
        const Feed bead = breakAt(ctx, [](Car& c) { c.wheels[1].debeaded = true; });
        const Feed lost = breakAt(ctx, [](Car& c) { c.wheels[2].mounted = false; });
        ctx.metric("shaft snap: pulse" + at, shaft.d.peak, "0..1", 0.4, 1);
        ctx.metric("tire debeads: pulse" + at, bead.d.peak, "0..1", 0.3, 1);
        ctx.metric("wheel comes off: pulse" + at, lost.d.peak, "0..1", 0.45, 1);
        ctx.check("a break is one pulse, then quiet" + at, shaft.d.pulses == 1 && bead.d.pulses == 1 && shaft.d.tailDuty(2.0) == 0 && bead.d.tailDuty(2.0) == 0);
        const Feed lim = limiter(ctx), limHigh = limiter(ctx, 2);
        ctx.metric("rev limiter: ticks in 1.5 s" + at, lim.d.pulses, "", 3, 6);
        ctx.metric("rev limiter: share of frames with rumble (1.5 s of 3 s on the limiter)" + at, lim.d.duty() * 100, "%", 1, 15);
        ctx.check("... and quiet when the revs come off it" + at, lim.d.tailDuty(1.0) == 0);
        ctx.check("HIGH is stronger than LOW" + at, limHigh.d.peak > lim.d.peak && drop3.d.peak > 0);
        ctx.check("OFF is silent" + at, limiter(ctx, 0).d.peak == 0 && breakAt(ctx, [](Car& c) { c.wheels[0].shaftBroken = true; }, 0).d.peak == 0);
    }
}
