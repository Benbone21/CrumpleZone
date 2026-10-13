// Test scenarios. Each one scripts a situation on the real map and checks the
// outcome against expected real-world-ish numbers. Ranges are deliberately
// physical ("a 100 km/h stop takes 33-45 m"), not tuned to whatever the code
// happens to do.
#include <chrono>
#include <cstdio>
#include <functional>

#include "../src/mods.h"
#include "harness.h"

namespace {

// A clear straight on the test pad, running along +X at z = -50.
const double kLaneZ = -50, kLaneStartX = -120;

// Test driver: pure-pursuit steering along a path z = path(x) heading +X.
void followPath(Car& c, const std::function<double(double)>& path) {
    Frame f = c.frame();
    double look = 6.0 + 0.5 * len(f.vel);
    double tx = f.com.x + look;
    V3 to = V3(tx, f.com.y, path(tx)) - f.com;
    c.steer = clampd(std::atan2(dot(to, f.left), dot(to, f.fwd)), -0.4, 0.4);
}
void holdLine(Car& c, double lineZ) {
    followPath(c, [lineZ](double) { return lineZ; });
}

// Places a vehicle in the right-hand lane of the loop road at sample `idx`.
Car& addOnLoop(Ctx& ctx, const VehicleSpec& spec, int idx, int dir, bool ai, double lane = 2.6) {
    const auto& road = ctx.sim.world.loopRoad;
    int n = (int)road.size();
    idx = ((idx % n) + n) % n;
    V3 d = road[((idx + dir) % n + n) % n] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 p = road[idx] + V3(-t.z, 0, t.x) * lane;
    Car& c = ctx.add(spec, p.x, p.z, std::atan2(-t.z, t.x) * 180 / M_PI, ai);
    if (ai) {
        c.aiDir = dir;
        c.aiIndex = idx;
    }
    return c;
}

// Road progress (m) of a car since `start`, following the loop in `dir`.
struct LoopProgress {
    double total = 0, last = -1;
    void update(const World& w, int idx, int dir) {
        double a = w.loopAlong[idx];
        if (last >= 0) {
            double d = (a - last) * dir;
            if (d < -w.loopLength / 2) d += w.loopLength;
            if (d > w.loopLength / 2) d -= w.loopLength;
            total += d;
        }
        last = a;
    }
};

double minUp(const Car& c, double cur) { return std::min(cur, c.frame().up.y); }

// Length of the body along its own forward axis (shrinks when the front crumples).
double bodyLength(const Car& c) {
    Frame f = c.frame();
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < c.bodyNodes; i++) {
        double x = dot(c.nodes[i].p - f.com, f.fwd);
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    return hi - lo;
}

struct Range {
    double lo, hi;
};

// Bolt-on panels (doors, hood, bumpers...) hanging open or torn off.
int loosePanels(const Car& c) {
    int n = 0;
    for (const Panel& p : c.panels) n += p.state > 0;
    return n;
}
// Doors (front or rear) that came open or off.
int looseDoors(const Car& c) {
    int n = 0;
    for (const Panel& p : c.panels) n += p.state > 0 && std::string(p.name).find("DOOR") != std::string::npos;
    return n;
}

// Shattered glass triangles by where they face: windshield, side windows, back glass.
std::string brokenGlassWhere(const Car& c) {
    int n[3] = {0, 0, 0};
    Frame f = c.frame();
    for (const Tri& t : c.tris) {
        if (t.mat != MAT_GLASS || !t.shattered) continue;
        double fx = dot(t.nrm, f.fwd);
        n[fx > 0.5 ? 0 : fx < -0.5 ? 2 : 1]++;
    }
    return std::to_string(n[0]) + " / " + std::to_string(n[1]) + " / " + std::to_string(n[2]);
}
std::string loosePanelNames(const Car& c) {
    std::string s;
    for (const Panel& p : c.panels)
        if (p.state > 0) s += std::string(s.empty() ? "" : ", ") + p.name + (p.state == 2 ? " (off)" : " (open)");
    return s.empty() ? "none" : s;
}
// Per-vehicle expectations, indexed like vehicleCatalog(): Civic Si, Jesko, Bronco, 4Runner.
// The ranges are the real cars' road-test figures (0-100 km/h, 100-0 km/h braking,
// skidpad grip), with some room for the differences between tests.
int vehicleIndex(const VehicleSpec& s) { return (int)(&s - &vehicleCatalog()[0]); }

}  // namespace

// =============================================================================
// Vehicle dynamics

VEHICLE_SCENARIO(settle, "dynamics/settle", "Sits still for 5 s: settles level, no creep, no damage, no jitter") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    V3 p0 = c.frame().com;
    double maxV = 0;
    ctx.run(4.0, [&] {
        double v = 0;
        for (const Node& n : c.nodes) v = std::max(v, len(n.v));
        if (ctx.t > 4.0) maxV = std::max(maxV, v);  // last second: should be dead still
        ctx.sample("Center of mass height", spec.name, c.frame().com.y, "m");
    });
    Frame f = c.frame();
    ctx.metric("creep in 4 s", len(f.com - p0) * 100, "cm", 0, 2);
    ctx.metric("fastest node in the final second", maxV, "m/s", 0, 0.02);
    ctx.metric("body roll/pitch (1 - up.y)", (1 - f.up.y) * 1000, "milli", 0, 2);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.1);
    // Front and rear carry different loads (that's the weight split); left and right
    // on the same axle should match closely.
    double worstSide = 1, total = 0;
    for (const Axle& a : c.axles) {
        double l = c.wheels[a.wheel[0]].load, r = c.wheels[a.wheel[1]].load;
        worstSide = std::max(worstSide, std::max(l, r) / std::max(1.0, std::min(l, r)));
    }
    for (const Wheel& w : c.wheels) total += w.load;
    ctx.metric("left/right wheel load difference (worst axle)", (worstSide - 1) * 100, "%", 0, 12);
    ctx.metric("total wheel load vs weight", total / (c.mass * 9.81) * 100, "%", 90, 110);
}

VEHICLE_SCENARIO(accel, "dynamics/acceleration", "Full throttle from rest on the pad: 0-100 km/h time, straightness, clean shifts") {
    // Civic Si ~7.2 s (0-60 mph in 6.8), Jesko ~2.5-3 s, Bronco 2.7 ~6.9 s, 4Runner ~10 s
    // (factory 3.4: 10.1-10.5 s; the blower makes up for the weight and the 35s).
    // EF replaces the 200 hp 2022 sedan: 108 hp and ~1,037 kg, estimated 0-100 ~10 s.
    // (Engines run ~15% over the factory figures, see vehicles.cpp: a little quicker.)
    static const Range expect[] = {{8.5, 11.0}, {2.4, 3.4}, {5.6, 7.2}, {8.5, 11.0}};
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double t0 = ctx.t, t100 = -1;
    int shifts = 0, lastGear = c.gear, downshifts = 0;
    ctx.runUntil(16.0, [&] { return t100 > 0; }, [&] {
        c.throttle = 1;
        holdLine(c, kLaneZ);
        double kmh = speedKmh(c);
        if (kmh >= 100 && t100 < 0) t100 = ctx.t - t0;
        if (c.gear != lastGear) {
            shifts++;
            if (c.gear < lastGear) downshifts++;
            lastGear = c.gear;
        }
        ctx.sample("Speed", spec.name, kmh, "km/h");
        ctx.sample("Engine rpm", spec.name, c.rpm, "rpm");
        static thread_local int fr = 0;
        if (std::getenv("CZ_ACCEL") && ++fr % 15 == 0) {
            double spin = 0;
            for (const Wheel& wh : c.wheels)
                if (c.wheelPowered(wh)) spin = std::max(spin, wh.sx);
            std::printf("ACCEL lag %.2f %s t %.2f %.0f km/h gear %d rpm %.0f tc %.2f boost %.2f slip %.2f shift %.2f\n", c.spec->turboLag, spec.name, ctx.t - t0, kmh, c.gear, c.rpm, c.tcCut, c.boost, spin,
                        c.shiftTimer);
        }
    });
    const Range& r = expect[vehicleIndex(spec)];
    ctx.metric("0-100 km/h", t100 > 0 ? t100 : 99, "s", r.lo, r.hi);
    ctx.metric("downshifts during the run (gear hunting)", downshifts, "", 0, 0);
    ctx.metric("drift off the line", std::fabs(c.frame().com.z - kLaneZ), "m", 0, 1.0);
}

VEHICLE_SCENARIO(braking, "dynamics/braking", "Full brakes from 100 km/h: stopping distance and stability (ABS)") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 100 / 3.6);
    ctx.run(0.2, [&] { holdLine(c, kLaneZ); });
    V3 p0 = c.frame().com;
    double v0 = speedKmh(c);
    bool stopped = ctx.runUntil(8.0, [&] { return speedKmh(c) < 0.5; }, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.steer = 0;
        ctx.sample("Speed", spec.name, speedKmh(c), "km/h");
    });
    double dist = travelled(c, p0, V3(1, 0, 0)) * (100 / v0) * (100 / v0);  // normalize to exactly 100 km/h
    ctx.check("comes to a stop", stopped);
    // EF Civic on 185/60R14 street tires without ABS: ~43 m (estimate); Jesko ~31 m (carbon-ceramics, Cup 2 R), Bronco ~44 m, 2002 4Runner ~45 m (rear drums).
    static const Range expect[] = {{38, 49}, {27, 35}, {39, 50}, {40, 52}};
    const Range& r = expect[vehicleIndex(spec)];
    ctx.metric("100-0 km/h stopping distance", dist, "m", r.lo, r.hi);
    ctx.metric("sideways drift while braking", std::fabs(c.frame().com.z - kLaneZ), "m", 0, 0.8);
    ctx.metric("heading change while braking", std::fabs(headingDeg(c)), "deg", 0, 3);
}

VEHICLE_SCENARIO(skidpad, "dynamics/skidpad", "Holds a 30 m circle, speeding up slowly: peak steady lateral grip, no rollover") {
    // Like a magazine skidpad test: the driver holds the circle and creeps up the speed
    // until the car can't stay on it.
    // EF Civic on period-size street tires: ~0.8 g (estimate), Jesko well over 1.1 g, Bronco ~0.70 g, 2002 4Runner ~0.68 g.
    static const Range expect[] = {{0.75, 0.90}, {1.08, 1.45}, {0.62, 0.78}, {0.60, 0.76}};
    const double R = 30;
    Car& c = ctx.add(spec, -40, -100, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const V3 center = c.frame().com + V3(0, 0, R);  // turning right (heading +X, right is +Z)
    V3 prevVel = c.frame().vel;
    double peak = 0, up = 1, smooth = 0, t0 = ctx.t, damage0 = c.damage;
    bool lost = false;
    ctx.run(40.0, [&] {
        Frame f = c.frame();
        V3 r = f.com - center;
        r.y = 0;
        const double dist = len(r), err = dist - R;  // + = running wide
        // Pure pursuit to a point on the circle a little ahead.
        const double look = std::max(6.0, 0.7 * len(f.vel)), ang = std::atan2(r.z, r.x) + look / R;
        V3 aim = center + V3(std::cos(ang), 0, std::sin(ang)) * R - f.com;
        const double alpha = std::atan2(dot(aim, f.left), dot(aim, f.fwd));
        c.steer = clampd(std::atan(2 * c.wheelbase * std::sin(alpha) / look), -0.55, 0.55);
        // Target speed rises ~0.45 m/s every second (to 22 m/s: 1.6 g on this circle).
        const double vt = 4.0 + 0.45 * (ctx.t - t0), v = len(f.vel);
        c.throttle = clampd((vt - v) * 0.4, 0, 1);
        c.brake = 0;
        c.handbrake = 0;
        V3 acc = (f.vel - prevVel) / Ctx::kFrame;  // how fast the velocity itself turns
        prevVel = f.vel;
        V3 side = norm(cross(V3(0, 1, 0), norm(f.vel + V3(1e-9, 0, 0))));
        smooth += (std::fabs(dot(acc, side)) / 9.81 - smooth) * 0.05;  // ~0.3 s average
        double sideslip = std::fabs(std::atan2(dot(f.vel, f.left), std::max(0.1, dot(f.vel, f.fwd))));
        lost |= std::fabs(err) > 3.0 || c.damage > damage0 + 1e-5;  // can't hold the circle any more
        if (!lost && f.up.y > 0.8 && sideslip < 0.15) peak = std::max(peak, smooth);
        if (lost) c.throttle = 0;
        up = minUp(c, up);
        ctx.sample("Lateral acceleration", spec.name, smooth, "g");
    });
    const Range& r = expect[vehicleIndex(spec)];
    ctx.metric("peak steady lateral acceleration", peak, "g", r.lo, r.hi);
    ctx.check("stays on its wheels", up > 0.5, "min up.y " + std::to_string(up));
}

VEHICLE_SCENARIO(stability, "dynamics/high-speed", "140 km/h hands-off on the straight: no weave, no drift") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 140 / 3.6);
    double maxYaw = 0;
    ctx.run(3.5, [&] {
        c.throttle = 0.5;
        c.steer = 0;
        maxYaw = std::max(maxYaw, std::fabs(headingDeg(c)));
        ctx.sample("Heading", spec.name, headingDeg(c), "deg");
    });
    ctx.metric("heading wander", maxYaw, "deg", 0, 1.5);
    ctx.metric("sideways drift", std::fabs(c.frame().com.z - kLaneZ), "m", 0, 1.0);
}

VEHICLE_SCENARIO(laneChange, "dynamics/lane-change", "Quick double lane change at 60 km/h: recovers, stays upright, no damage") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 60 / 3.6);
    // Driver follows a path that jumps 3.5 m left over ~20 m and comes back 20 m later.
    double up = 1, worstYawRate = 0, prevH = headingDeg(c), x0 = c.frame().com.x;
    ctx.run(6.0, [&] {
        double x = c.frame().com.x - x0;
        followPath(c, [&](double px) {
            double rx = px - x0;
            return kLaneZ - 3.5 * (smoothstep(10, 30, rx) - smoothstep(45, 65, rx));
        });
        c.throttle = speedKmh(c) < 60 ? 0.3 : 0.0;
        up = minUp(c, up);
        double h = headingDeg(c);
        if (x > 90) worstYawRate = std::max(worstYawRate, std::fabs(h - prevH) / Ctx::kFrame);
        prevH = h;
        ctx.sample("Heading", spec.name, h, "deg");
        ctx.sample("Lateral position", spec.name, c.frame().com.z - kLaneZ, "m");
    });
    ctx.check("stays upright", up > 0.85, "min up.y " + std::to_string(up));
    ctx.metric("back on the original line", std::fabs(c.frame().com.z - kLaneZ), "m", 0, 0.6);
    ctx.metric("residual yaw wobble after the maneuver", worstYawRate, "deg/s", 0, 4);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.1);
    ctx.check("no doors or panels come loose", loosePanels(c) == 0, loosePanelNames(c));
}

VEHICLE_SCENARIO(slopeHold, "dynamics/slope-hold", "Handbrake holds it nose-up on the 15 degree kicker ramp") {
    Car& c = ctx.add(spec, 47, -30, 0);
    ctx.focus(c);
    c.handbrake = 1;
    c.brake = 0;
    ctx.run(4.0, [&] { c.handbrake = 1; });  // dropped onto the slope: let it settle first
    V3 p0 = c.frame().com;
    ctx.run(8.0, [&] { c.handbrake = 1; });
    ctx.metric("slide down the ramp in 8 s", len(c.frame().com - p0) * 100, "cm", 0, 5);
}

VEHICLE_SCENARIO(drop, "dynamics/drop", "Dropped from 0.3 m and 1.2 m: suspension soaks it up without damage and settles quickly") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double restY = c.frame().com.y;
    for (Node& n : c.nodes) n.p.y += 0.3;
    double settleT = -1, t0 = ctx.t;
    int crossings = 0, side = 1;  // count swings through rest height bigger than +-5 mm
    ctx.run(3.0, [&] {
        double err = c.frame().com.y - restY;
        if (err * side < -0.005) {
            crossings++;
            side = -side;
        }
        if (std::fabs(err) > 0.01) settleT = -1;
        else if (settleT < 0) settleT = ctx.t - t0;
        ctx.sample("Body height after 0.3 m drop", spec.name, err * 100, "cm");
    });
    ctx.metric("settle time after 0.3 m drop (within 1 cm)", settleT < 0 ? 9 : settleT, "s", 0.2, 1.6);
    ctx.metric("swings through rest height (real cars: 2-4)", crossings, "", 0, 4);
    for (Node& n : c.nodes) n.p.y += 1.2;
    ctx.run(3.0);
    int popped = 0;
    for (const Wheel& w : c.wheels) popped += w.popped;
    ctx.metric("damage after 1.2 m drop", c.damage * 100, "%", 0, 0.5);
    ctx.metric("tires blown by 1.2 m drop", popped, "", 0, 0);
}

VEHICLE_SCENARIO(washboard, "dynamics/washboard", "40 km/h across the washboard bumps: no damage, tires survive, keeps its line") {
    if (spec.layerY[0] + spec.bumperLift < 0.14) {
        // The bumps are 12 cm tall: a hypercar's splitter just hits them (as it would for real).
        ctx.note(std::string(spec.name) + " sits too low for the washboard; skipped");
        return;
    }
    Car& c = ctx.add(spec, -10, -70, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 40 / 3.6);
    double up = 1, t0 = ctx.t;
    ctx.runUntil(15.0, [&] { return c.frame().com.x > 75; }, [&] {
        // (Easing off when a driven tire spins up, as a driver does: the old Civic has an
        // open diff and no traction control, so flat out over the bumps one front tire just
        // peels away against the rev limiter.)
        double spin = 0;
        for (const Wheel& w : c.wheels)
            if (c.wheelPowered(w)) spin = std::max(spin, std::fabs(w.omega * w.rollR()) - std::fabs(w.groundSpeed));
        c.throttle = clampd((40 - speedKmh(c)) * 0.15, 0, 1) * clampd(1 - (spin - 1.5) / 4, 0.25, 1);
        holdLine(c, -70);
        up = minUp(c, up);
        ctx.sample("Body height", spec.name, c.frame().com.y, "m");
        ctx.sample("Speed", spec.name, speedKmh(c), "km/h");
    });
    // (The 1990 Civic, with no traction control and an open diff, rides these sharp 12 cm
    // ridges in first, hopping, at ~20-25 km/h. A tire that wrapped the ridge's edge instead
    // of meeting it at one point would hit softer; the tire model doesn't do that yet.)
    ctx.metric("average speed over the bumps", (c.frame().com.x + 10) / (ctx.t - t0) * 3.6, "km/h", spec.tractionFitted ? 28 : 20, 45);
    int popped = 0;
    for (const Wheel& w : c.wheels) popped += w.popped;
    ctx.metric("damage (cosmetic at most)", c.damage * 100, "%", 0, 0.5);
    ctx.metric("tires blown", popped, "", 0, 0);
    ctx.metric("drift off the line", std::fabs(c.frame().com.z + 70), "m", 0, 1.5);
    ctx.check("made it across", c.frame().com.x > 75, "x = " + std::to_string(c.frame().com.x));
    ctx.check("no doors or panels shaken loose", loosePanels(c) == 0, loosePanelNames(c));
    ctx.check("stays upright", up > 0.85);
}

VEHICLE_SCENARIO(reverse, "dynamics/reverse", "Backs up: reverse gear engages and reaches walking-plus speed") {
    Car& c = ctx.add(spec, 0, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.run(5.0, [&] { ctx.sim.drive(false, true, false, false, false, Ctx::kFrame); });
    Frame f = c.frame();
    ctx.check("in reverse gear", c.gear == -1);
    ctx.metric("reverse speed after 5 s (rev-limited)", -dot(f.vel, f.fwd) * 3.6, "km/h", 12, 80);
}

// =============================================================================
// Structure: how stiff the body is, how much it jiggles, where the weight sits

namespace {

// Body nodes in a cross-section of the car at local x (within +-halfWidth), split
// into left (z < 0) and right halves of the outer part of the body.
struct Section {
    std::vector<int> all, left, right;
};
Section sectionAt(const Car& c, double x, double halfWidth = 0.3) {
    Section s;
    const double w = c.spec->width;
    for (int i = 0; i < c.bodyNodes; i++) {
        const V3& l = c.restLocal[i];
        if (std::fabs(l.x - x) > halfWidth) continue;
        s.all.push_back(i);
        if (l.z < -0.3 * w) s.left.push_back(i);
        if (l.z > 0.3 * w) s.right.push_back(i);
    }
    return s;
}
V3 avgPos(const Car& c, const std::vector<int>& g) {
    V3 s;
    for (int i : g) s += c.nodes[i].p;
    return s / (double)std::max<size_t>(1, g.size());
}
void pushGroup(Car& c, const std::vector<int>& g, const V3& total) {
    for (int i : g) c.extForce[i] += total / (double)g.size();
}
double axleX(const Car& c, bool front) {
    double x = front ? -1e9 : 1e9;
    for (const Wheel& w : c.wheels) {
        double wx = c.restLocal[w.inner].x;
        x = front ? std::max(x, wx) : std::min(x, wx);
    }
    return x;
}

// Puts a fresh vehicle 40 m up in the air (pointing +X), where it can be loaded in
// free fall without the ground or the suspension taking any of the load.
Car& rigInAir(Ctx& ctx, const VehicleSpec& spec) {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    for (Node& n : c.nodes) n.p.y += 40;
    c.extForce.assign(c.nodes.size(), V3());
    ctx.focus(c);
    ctx.run(0.3);  // let the suspension drop to full droop and the body settle
    return c;
}

// Dynamic, non-rigid motion of the body: every body node's position in the car's own
// frame, minus its recent average (~0.15 s). Rigid motion and slow sag don't count;
// shaking and wobbling do.
struct FlexMeter {
    std::vector<V3> mean;
    double sum = 0;
    long n = 0;
    double worst = 0;
    void sample(const Car& c, bool record = true) {
        Frame f = c.frame();
        bool first = mean.empty();
        if (first) mean.resize(c.bodyNodes);
        double frameSum = 0;
        for (int i = 0; i < c.bodyNodes; i++) {
            V3 d = c.nodes[i].p - f.com;
            V3 l(dot(d, f.fwd), dot(d, f.up), dot(d, f.left));
            if (first) mean[i] = l;
            mean[i] += (l - mean[i]) * 0.1;
            double e = dot(l - mean[i], l - mean[i]);
            frameSum += e;
        }
        if (record) {
            sum += frameSum;
            n += c.bodyNodes;
            worst = std::max(worst, std::sqrt(frameSum / c.bodyNodes));
        }
    }
    double rmsMm() const { return n ? std::sqrt(sum / n) * 1000 : 0; }
};

}  // namespace

VEHICLE_SCENARIO(torsion, "structure/torsion", "Twisting rig in free fall: torsional stiffness of the body between the axles") {
    // Real cars: roughly 10-35 kNm/deg (older or open bodies lower, modern unibodies higher).
    // (Unibody Civic, carbon tub Jesko, body-on-frame Bronco and 4Runner.)
    static const Range expect[] = {{12, 55}, {15, 80}, {6, 36}, {6, 36}};
    Car& c = rigInAir(ctx, spec);
    Section f = sectionAt(c, axleX(c, true)), r = sectionAt(c, axleX(c, false));
    auto roll = [&](const Section& s) {
        V3 d = avgPos(c, s.left) - avgPos(c, s.right);
        return std::atan2(d.y, -d.z);
    };
    double sep = len(avgPos(c, f.left) - avgPos(c, f.right));
    const double T = 3000;  // N*m
    double f0 = roll(f), r0 = roll(r);
    double twist = 0;
    ctx.run(0.8, [&] {
        double ramp = smoothstep(0, 0.3, ctx.t - 0.3);
        std::fill(c.extForce.begin(), c.extForce.end(), V3());
        V3 F(0, ramp * T / sep, 0);
        pushGroup(c, f.left, F);
        pushGroup(c, f.right, -F);
        pushGroup(c, r.left, -F);
        pushGroup(c, r.right, F);
        twist = ((roll(f) - f0) - (roll(r) - r0)) * 180 / M_PI;
        ctx.sample("Twist under 3 kNm", spec.name, twist, "deg");
    });
    const Range& e = expect[vehicleIndex(spec)];
    ctx.metric("torsional stiffness", T / 1000 / std::max(std::fabs(twist), 1e-4), "kNm/deg", e.lo, e.hi);
}

VEHICLE_SCENARIO(bending, "structure/bending", "Bending rig in free fall: load the middle, hold at the axles, measure the sag") {
    // Real cars: roughly 5-25 kN/mm measured this way.
    Car& c = rigInAir(ctx, spec);
    double xf = axleX(c, true), xr = axleX(c, false);
    Section f = sectionAt(c, xf), r = sectionAt(c, xr), m = sectionAt(c, (xf + xr) / 2, 0.45);
    auto sag = [&] { return (avgPos(c, f.all).y + avgPos(c, r.all).y) / 2 - avgPos(c, m.all).y; };
    const double F = 6000;  // N
    double s0 = sag(), s = 0;
    ctx.run(0.8, [&] {
        double ramp = smoothstep(0, 0.3, ctx.t - 0.3);
        std::fill(c.extForce.begin(), c.extForce.end(), V3());
        pushGroup(c, m.all, V3(0, -F * ramp, 0));
        pushGroup(c, f.all, V3(0, F * ramp / 2, 0));
        pushGroup(c, r.all, V3(0, F * ramp / 2, 0));
        s = sag() - s0;
        ctx.sample("Sag under 6 kN", spec.name, s * 1000, "mm");
    });
    ctx.metric("bending stiffness", F / 1000 / std::max(s * 1000, 1e-4), "kN/mm", 4, 40);
}

VEHICLE_SCENARIO(jiggle, "structure/jiggle", "How much the body itself shakes: cruising, over bumps, and braking hard") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 80 / 3.6);
    ctx.run(0.5, [&] { holdLine(c, kLaneZ); });
    FlexMeter cruise;
    ctx.run(2.5, [&] {
        c.throttle = clampd((80 - speedKmh(c)) * 0.1, 0, 1);
        holdLine(c, kLaneZ);
        cruise.sample(c);
        ctx.sample("Body flex (rms)", "cruise 80 km/h", cruise.worst * 1000, "mm");
    });
    FlexMeter braking;
    ctx.runUntil(5.0, [&] { return speedKmh(c) < 5; }, [&] {
        c.throttle = 0;
        c.brake = 1;
        holdLine(c, kLaneZ);
        braking.sample(c);
    });
    // Smooth road: a real body doesn't visibly flex at all.
    ctx.metric("body flex cruising at 80 km/h (rms)", cruise.rmsMm(), "mm", 0, 0.5);
    ctx.metric("body flex braking from 80 km/h (rms)", braking.rmsMm(), "mm", 0, 1.5);

    Sim& sim = ctx.sim;
    sim.cars.clear();
    Car& b = ctx.add(spec, -10, -70, 0);
    ctx.focus(b);
    ctx.run(1.0);
    ctx.launch(b, 40 / 3.6);
    FlexMeter bumps;
    ctx.runUntil(12.0, [&] { return b.frame().com.x > 75; }, [&] {
        b.throttle = clampd((40 - speedKmh(b)) * 0.15, 0, 1);
        holdLine(b, -70);
        bumps.sample(b);
        ctx.sample("Body flex (rms)", "washboard 40 km/h", bumps.worst * 1000, "mm");
    });
    ctx.metric("body flex over the washboard at 40 km/h (rms)", bumps.rmsMm(), "mm", 0, 3);
}

VEHICLE_SCENARIO(travel, "structure/suspension-travel", "Body pressed down to 1.8 g and lifted to 0.4 g: the suspension stays stiff all the way, no collapse") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    c.handbrake = 1;
    ctx.run(1.5);
    c.extForce.assign(c.nodes.size(), V3());
    auto bodyY = [&] {
        double y = 0, m = 0;
        for (int i = 0; i < c.frameNodes; i++) {
            y += c.nodes[i].p.y * c.nodes[i].m;
            m += c.nodes[i].m;
        }
        return y / m;
    };
    auto springDev = [&] {
        double worst = 0;
        for (const Wheel& w : c.wheels) {
            const Beam& sp = c.beams[w.spring];
            worst = std::max(worst, std::fabs(len(c.nodes[sp.b].p - c.nodes[sp.a].p) - w.springRest));
        }
        return worst;
    };
    ctx.metric("ride height error at rest (worst coil-over vs design)", springDev() * 1000, "mm", 0, 12);
    const double y0 = bodyY();
    // Extra load as a fraction of weight: ramp up to +0.8, down to -0.6, back to 0, slowly
    // (quasi-static). Within each ramp the height must keep moving the right way: every
    // further 0.1 g of load pushes it lower (or, lifting, higher). A linkage going over
    // center shows up as the height running away the wrong way.
    struct Step {
        double load, y;
    };
    std::vector<Step> down, up;
    double lowest = y0, highest = y0;
    ctx.run(9.0, [&] {
        double t = ctx.t - 1.5, load;
        if (t < 3) load = 0.8 * t / 3;
        else if (t < 7.5) load = 0.8 - 1.4 * (t - 3) / 4.5;
        else load = -0.6 + 0.6 * (t - 7.5) / 1.5;
        for (int i = 0; i < c.frameNodes; i++) c.extForce[i] = V3(0, -9.81 * c.nodes[i].m * load, 0);
        double y = bodyY();
        if (t > 0.4 && t < 3 && (down.empty() || load - down.back().load >= 0.1)) down.push_back({load, y});
        if (t > 3.6 && t < 7.5 && (up.empty() || up.back().load - load >= 0.1)) up.push_back({load, y});
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
        ctx.sample("Body height vs rest", spec.name, (y - y0) * 1000, "mm");
    });
    double worst = 1e9;  // smallest drop per extra g between steps (m per g)
    for (size_t i = 1; i < down.size(); i++) worst = std::min(worst, (down[i - 1].y - down[i].y) / (down[i].load - down[i - 1].load));
    for (size_t i = 1; i < up.size(); i++) worst = std::min(worst, (up[i].y - up[i - 1].y) / (up[i - 1].load - up[i].load));
    // (Down to ~25 mm: the Jesko's ~2.3 Hz ride plus its heave springs is ~4 cm per g
    // before the stops.)
    ctx.metric("compression at +0.8 g extra load", (y0 - lowest) * 1000, "mm", 25, 170);
    ctx.metric("extension at 0.6 g lift", (highest - y0) * 1000, "mm", 20, 170);
    // Positive everywhere = the body always moves with the load. (Bump stops make it small.)
    ctx.metric("least travel per g anywhere in the range", worst * 1000, "mm/g", 2, 1000);
}

VEHICLE_SCENARIO(bumpSteer, "structure/bump-steer", "Body pressed down and lifted 5 cm: how much the front wheels steer by themselves") {
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    c.handbrake = 1;
    ctx.run(1.5);
    c.extForce.assign(c.nodes.size(), V3());
    auto toe = [&](const Wheel& w) {  // + = toe in, degrees, relative to the body
        Frame f = c.frame();
        V3 ax = norm(c.nodes[w.outer].p - c.nodes[w.inner].p);
        double a = std::atan2(dot(ax, f.fwd), std::fabs(dot(ax, f.left)));
        return (w.left ? -a : a) * 180 / M_PI;
    };
    auto rideY = [&] {
        double y = 0, m = 0;
        for (int i = 0; i < c.frameNodes; i++) {
            y += c.nodes[i].p.y * c.nodes[i].m;
            m += c.nodes[i].m;
        }
        return y / m;
    };
    double y0 = rideY(), worst = 0, worstAt = 0;
    std::vector<double> toe0;
    for (const Wheel& w : c.wheels) toe0.push_back(toe(w));
    // Slowly load the body down (to +0.7 g) and then lift it (to -0.5 g).
    ctx.run(7.0, [&] {
        double t = ctx.t - 1.5, load = t < 3 ? 0.7 * t / 3 : 0.7 - 1.2 * (t - 3) / 4;
        for (int i = 0; i < c.frameNodes; i++) c.extForce[i] = V3(0, -9.81 * c.nodes[i].m * load, 0);
        double travel = rideY() - y0;
        for (size_t wi = 0; wi < c.wheels.size(); wi++) {
            if (!c.wheels[wi].front || std::fabs(travel) > 0.05) continue;
            double d = toe(c.wheels[wi]) - toe0[wi];
            if (std::fabs(d) > std::fabs(worst)) {
                worst = d;
                worstAt = travel;
            }
        }
        ctx.sample("Front-left toe change", spec.name, toe(c.wheels[0]) - toe0[0], "deg");
        ctx.sample("Body travel", spec.name, travel * 100, "cm");
    });
    ctx.metric("worst front toe change within +-5 cm of travel", std::fabs(worst), "deg", 0, 0.4);
    ctx.note("worst at " + std::to_string(worstAt * 100) + " cm of travel");
}

VEHICLE_SCENARIO(weight, "structure/weight", "Curb weight, front/rear split and center of gravity height against real vehicles") {
    struct Expect {
        Range kg, front, cog;
    };
    // Published curb weights and weight splits (Honda, Koenigsegg, Ford, Toyota); CG
    // heights from typical measurements of these kinds of vehicle.
    static const Expect expect[] = {
        {{1000, 1080}, {58, 64}, {0.42, 0.53}},  // EF Civic Si: ~1,037 kg, estimated 60/40
        {{1380, 1460}, {39, 45}, {0.34, 0.46}},      // Jesko Attack: 1,420 kg, ~42/58
        {{2150, 2290}, {54, 59}, {0.70, 0.86}},      // Bronco 4-door 2.7: ~2,214 kg, ~56/44
        {{2060, 2200}, {52, 58}, {0.74, 0.92}},      // 4Runner Limited as built: 1,867 kg + ~290 kg of parts, ~55/45, lifted 170 mm
    };
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(2.0);
    double front = 0, total = 0;
    for (const Wheel& w : c.wheels) {
        total += w.load;
        if (w.front) front += w.load;
    }
    Frame f = c.frame();
    double M = 0, cy = 0;
    for (const Node& n : c.nodes) {
        M += n.m;
        cy += n.m * n.p.y;
    }
    cy = cy / M - ctx.sim.world.height(f.com.x, f.com.z);
    // Pitch inertia: how spread out the mass is along the car (sets how fast it rotates
    // nose-down off a jump, or pitches over bumps). The dynamic index k^2 / (a b), with k
    // the pitch radius of gyration and a, b the axle distances from the CG, is ~0.8-1.2
    // for real cars (near 1: the axles bounce almost independently).
    double Ipitch = 0;
    for (const Node& n : c.nodes) {
        V3 d = n.p - f.com;
        double along = dot(d, f.fwd), up = dot(d, f.up);
        Ipitch += n.m * (along * along + up * up);
    }
    const double frontShare = front / std::max(total, 1.0), a = c.wheelbase * (1 - frontShare), b = c.wheelbase * frontShare;
    ctx.metric("pitch dynamic index k^2 / (a b)", Ipitch / M / std::max(1e-6, a * b), "", 0.8, 1.2);
    const Expect& e = expect[vehicleIndex(spec)];
    ctx.metric("curb weight", c.mass, "kg", e.kg.lo, e.kg.hi);
    ctx.metric("weight on the front axle", front / std::max(total, 1.0) * 100, "%", e.front.lo, e.front.hi);
    ctx.metric("center of gravity height", cy, "m", e.cog.lo, e.cog.hi);
}

// =============================================================================
// Crashes and damage

VEHICLE_SCENARIO(wall50, "damage/wall-50", "50 km/h into the concrete wall: crumple depth, deceleration, engine and glass") {
    Car& c = ctx.add(spec, 100 - spec.length / 2, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double L0 = bodyLength(c);
    ctx.launch(c, 50 / 3.6);
    double prevV = len(c.frame().vel), peakDecel = 0, smooth = 0, rebound = 0;
    ctx.run(2.5, [&] {
        double v = len(c.frame().vel), vx = c.frame().vel.x;
        smooth += ((prevV - v) / Ctx::kFrame - smooth) * 0.3;
        peakDecel = std::max(peakDecel, smooth / 9.81);
        rebound = std::max(rebound, -vx * 3.6);
        prevV = v;
        ctx.sample("Speed along +X", spec.name, vx * 3.6, "km/h");
    });
    double crush = L0 - bodyLength(c);
    ctx.metric("crumple depth", crush, "m", 0.12, 1.0);  // (tougher than stock: see wall-25)
    ctx.metric("peak deceleration (smoothed)", peakDecel, "g", 8, 60);
    // (A long-nosed car can take a 50 km/h hit in its crumple zone before the engine.)
    ctx.metric("engine health", c.engineHealth, "", 0.15, 0.99);
    ctx.check("does not pass through the wall", c.frame().com.x < 114 - 0.5);
    ctx.metric("rebound speed (real cars: ~5-8 km/h; stiffer here)", rebound, "km/h", 0, 14);
    int glass = 0;
    for (const Tri& t : c.tris) glass += t.mat == MAT_GLASS && t.shattered;
    ctx.metric("broken glass panels", glass, "", 1, 60);
    // (Steel armor bumpers have no covers to knock off.)
    ctx.metric("body panels knocked loose", loosePanels(c), "", 0, 6);
    ctx.note("loose: " + loosePanelNames(c));
    // Crash-test cars keep their doors shut (so rescuers can open them).
    ctx.metric("doors come open", looseDoors(c), "", 0, 0);
}

VEHICLE_SCENARIO(wall8, "damage/parking-bump", "8 km/h into the wall (bumper-standard speed): no panels loose, barely a mark") {
    Car& c = ctx.add(spec, 114 - 1.2 - spec.length / 2, 0, 0);  // nose 1.2 m from the wall
    ctx.focus(c);
    ctx.run(1.0);
    double L0 = bodyLength(c);
    ctx.launch(c, 8 / 3.6);
    ctx.run(2.5);
    int glass = 0;
    for (const Tri& t : c.tris) glass += t.mat == MAT_GLASS && t.shattered;
    ctx.metric("permanent crush", (L0 - bodyLength(c)) * 100, "cm", -1, 2.5);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.6);
    ctx.metric("panels knocked loose", loosePanels(c), "", 0, 0);
    ctx.note("loose: " + loosePanelNames(c));
    ctx.metric("broken glass", glass, "", 0, 0);
}

VEHICLE_SCENARIO(wall25, "damage/wall-25", "25 km/h into the wall: nose bent, doors stay shut, glass mostly whole") {
    Car& c = ctx.add(spec, 114 - 1.2 - spec.length / 2, 0, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double L0 = bodyLength(c);
    ctx.launch(c, 25 / 3.6);
    ctx.run(2.5);
    int glass = 0;
    for (const Tri& t : c.tris) glass += t.mat == MAT_GLASS && t.shattered;
    // (A steel winch bumper on the frame takes a 25 km/h knock with hardly a mark.)
    // Steel bumpers take it with no crush at all; a body-on-frame truck's horns barely
    // move (1 cm+); a unibody's bumper beam and crush cans give a few centimetres.
    const bool ladderFrame = spec.frameStrength > 1;
    // (The cars are built ~40% tougher than bare steel, for the game: a 25 km/h knock dents.)
    ctx.metric("crumple depth", (L0 - bodyLength(c)) * 100, "cm", (spec.extras & EXTRA_ARMOR) || ladderFrame ? -1 : 0.5, 30);
    ctx.metric("damage", c.damage * 100, "%", 0.05, 8);
    ctx.metric("doors come open", looseDoors(c), "", 0, 0);
    ctx.metric("panels knocked loose", loosePanels(c), "", 0, 2);
    ctx.note("loose: " + loosePanelNames(c));
    ctx.metric("broken glass", glass, "", 0, 12);
    ctx.note("broken glass (windshield / sides / back): " + brokenGlassWhere(c));
    ctx.metric("engine health", c.engineHealth, "", 0.6, 1.0);
}

SCENARIO(pole, "damage/pole", "Civic hits a 44 cm pole head-on at 50 km/h: stopped by it, not passing through") {
    Car& c = ctx.add(vehicleCatalog()[0], 70, 20, -90);  // heading +Z toward the pole at (70, 38)
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 50 / 3.6);
    ctx.run(3.0, [&] { ctx.sample("Speed", "Civic", speedKmh(c), "km/h"); });
    int beyond = 0;
    for (int i = 0; i < c.bodyNodes; i++) beyond += c.nodes[i].p.z > 38.5;
    ctx.check("stopped by the pole", speedKmh(c) < 10, "speed " + std::to_string(speedKmh(c)) + " km/h");
    ctx.metric("body nodes that slipped past the pole", beyond, "", 0, 6);
    ctx.metric("damage", c.damage * 100, "%", 2, 60);
}

SCENARIO(tree, "damage/tree", "Civic runs into a tree at 50 km/h: trunks are solid") {
    const World& w = ctx.sim.world;
    // Pick a tree with open ground in front of it (to its -X side).
    const Tree* pick = nullptr;
    for (const Tree& t : w.trees) {
        bool clear = true;
        for (const Tree& o : w.trees)
            if (&o != &t && std::fabs(o.base.z - t.base.z) < 3 && o.base.x < t.base.x && o.base.x > t.base.x - 25) clear = false;
        if (clear && std::fabs(w.normal(t.base.x - 12, t.base.z).y) > 0.97) {
            pick = &t;
            break;
        }
    }
    if (!ctx.check("found a test tree", pick != nullptr)) return;
    Car& c = ctx.add(vehicleCatalog()[0], pick->base.x - 14, pick->base.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 50 / 3.6);
    ctx.run(3.0);
    ctx.check("stopped by the trunk", c.frame().com.x < pick->base.x, "x " + std::to_string(c.frame().com.x - pick->base.x));
}

SCENARIO(headOn, "damage/head-on", "Two coupes head-on at 60 km/h each: big crumple, no passing through each other") {
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& a = ctx.add(s, -20, kLaneZ, 0);
    Car& b = ctx.add(s, 20, kLaneZ, 180);
    ctx.focus(a);
    ctx.run(1.0);
    ctx.launch(a, 60 / 3.6);
    ctx.launch(b, 60 / 3.6);
    int worst = 0;
    ctx.run(3.0, [&] {
        worst = std::max(worst, intrusions(a, b) + intrusions(b, a));
        ctx.sample("Speed", "A", speedKmh(a), "km/h");
        ctx.sample("Speed", "B", speedKmh(b), "km/h");
    });
    ctx.metric("worst deep intrusions (nodes inside the other car)", worst, "", 0, 8);
    ctx.check("cars stay on their own sides", a.frame().com.x < b.frame().com.x);
    ctx.metric("damage A", a.damage * 100, "%", 8, 70);
    ctx.metric("damage B", b.damage * 100, "%", 8, 70);
}

SCENARIO(tBone, "damage/t-bone", "Bronco T-bones a parked Jesko at 50 km/h") {
    Car& h = ctx.add(vehicleCatalog()[1], 0, kLaneZ, 0);
    Car& p = ctx.add(vehicleCatalog()[2], 0, kLaneZ - 12, -90);  // heading +Z into the Jesko's side
    ctx.focus(p);
    ctx.run(1.0);
    ctx.launch(p, 50 / 3.6);
    int worst = 0;
    ctx.run(3.0, [&] { worst = std::max(worst, intrusions(p, h) + intrusions(h, p)); });
    ctx.metric("worst deep intrusions", worst, "", 0, 10);
    // The Jesko's carbon tub barely marks; the doors and sills take the hit (bent panel
    // metal, the same measure as the body's damage).
    double dent = 0;
    int nPanel = 0;
    for (const Beam& b : h.beams)
        if (b.type == BEAM_PANEL) {
            nPanel++;
            dent += b.broken ? 1.0 : std::min(1.0, std::fabs(b.L0 - b.Lorig) / b.Lorig * 8);
        }
    ctx.note("Jesko tub damage " + std::to_string(h.damage * 100) + " %");
    ctx.metric("Jesko damage (tub and panels)", (h.damage + dent / std::max(nPanel, 1)) * 100, "%", 0.5, 60);
    ctx.metric("Jesko shoved sideways", h.frame().com.z - kLaneZ, "m", 1, 15);
    // (Like the IIHS side test, a 1,900 kg barrier at 50 km/h: doors are dented in but
    // should stay latched.)
    ctx.metric("Jesko panels knocked loose", loosePanels(h), "", 0, 8);
    ctx.note("Jesko loose: " + loosePanelNames(h) + "; Bronco loose: " + loosePanelNames(p));
}

SCENARIO(rearEnd, "damage/rear-end", "4Runner rear-ends a stopped Civic at 40 km/h: momentum is shared sensibly") {
    Car& c = ctx.add(vehicleCatalog()[0], 0, kLaneZ, 0);
    Car& v = ctx.add(vehicleCatalog()[3], -15, kLaneZ, 0);
    ctx.focus(v);
    ctx.run(1.0);
    ctx.launch(v, 40 / 3.6);
    double pBefore = v.mass * 40 / 3.6;
    ctx.run(1.6);  // contact at ~0.95 s, then let the shove happen
    double pAfter = dot(c.frame().vel, V3(1, 0, 0)) * c.mass + dot(v.frame().vel, V3(1, 0, 0)) * v.mass;
    ctx.metric("momentum kept through the impact", pAfter / pBefore * 100, "%", 80, 102);
    ctx.check("Civic is pushed forward", c.frame().com.x > 0.5);
    ctx.check("no passing through", v.frame().com.x < c.frame().com.x);
}

SCENARIO(rollover, "damage/rollover", "4Runner with 350 kg on the roof rack does a fishhook at 80 km/h: tips over, nothing explodes") {
    // An empty SUV sits too low to trip on dry pavement (like the real thing); a heavy
    // roof load raises its center of gravity until it can't stay on its wheels.
    // (A 4Runner on sticky road tires: on its own all-terrains it slides before it tips.)
    static const VehicleSpec loaded = [] {
        VehicleSpec v = vehicleCatalog()[3];
        v.items.push_back({"roof rack load", 350, -0.6, 2.05, 0});
        v.tireGrip = 1.1;
        v.lateralGrip = 1.0;
        return v;
    }();
    Car& v = ctx.add(loaded, kLaneStartX, kLaneZ + 30, 0);
    ctx.focus(v);
    ctx.run(1.0);
    ctx.launch(v, 80 / 3.6);
    double up = 1, t0 = ctx.t;
    ctx.run(5.0, [&] {
        double s = ctx.t - t0;
        v.steer = s < 0.5 ? 0.3 : -0.5;
        v.throttle = 0;
        up = minUp(v, up);
        ctx.sample("Up vector y", "4Runner", v.frame().up.y, "");
    });
    ctx.check("rolls (or at least lifts two wheels hard)", up < 0.5, "min up.y " + std::to_string(up));
    ctx.metric("damage", v.damage * 100, "%", 0.5, 80);
    // Real rollovers scrape and dent the panels, but doors stay latched.
    ctx.metric("doors come open", looseDoors(v), "", 0, 0);
    ctx.metric("panels knocked loose", loosePanels(v), "", 0, 3);
    ctx.note("loose: " + loosePanelNames(v));
}

SCENARIO(jump, "damage/jump", "Civic takes the kicker at 70 km/h: airborne, noses over like a real car off a flat lip, stays in one piece") {
    Car& c = ctx.add(vehicleCatalog()[0], 20, -30, 0);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 70 / 3.6);
    double peak = 0, base = c.frame().com.y, noseDown = 0;
    bool airborne = false;
    ctx.run(5.0, [&] {
        c.throttle = 0.3;
        peak = std::max(peak, c.frame().com.y - base);
        bool flying = true;
        for (const Wheel& w : c.wheels) flying &= w.load == 0;
        if (flying && c.frame().com.y - base > 1.0) {
            airborne = true;
            noseDown = std::max(noseDown, -std::asin(clampd(c.frame().fwd.y, -1, 1)) * 180 / M_PI);
        }
        ctx.sample("Height", "Civic", c.frame().com.y - base, "m");
    });
    int detached = 0;
    for (const Wheel& w : c.wheels) detached += w.detached;
    ctx.check("leaves the ground", airborne);
    ctx.metric("peak height gained", peak, "m", 3, 9);
    // The front leaves the lip first while the rear is still on the ramp, so gravity tips
    // the nose down in the air: how far depends on speed and the car's pitch inertia.
    ctx.metric("nose-down pitch in the air", noseDown, "deg", 15, 120);
    ctx.metric("wheels torn off", detached, "", 0, 0);
    ctx.check("lands in one piece (still driveable)", speedKmh(c) < 45 && c.finite() && !c.engineDead,
              "speed " + std::to_string(speedKmh(c)) + " km/h, damage " + std::to_string(c.damage * 100) + " %");
}

SCENARIO(props, "damage/crash-boxes", "Civic ploughs through a stack of foam boxes at 60 km/h") {
    for (int row = 0; row < 3; row++)
        for (int k = 0; k < 3 - row; k++)
            ctx.sim.addCar(crashBoxSpec(), V3(0, 0.05 + row * 1.04, kLaneZ - 1.04 + 1.04 * k + 0.52 * row), 0,
                           V3(0.95, 0.52, 0.12));
    Car& c = ctx.add(vehicleCatalog()[0], -30, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(3.0);
    ctx.launch(c, 60 / 3.6);
    ctx.run(3.0);
    double thrown = 0;
    for (Car& b : ctx.sim.cars)
        if (b.isProp()) thrown = std::max(thrown, len(b.frame().com - b.spawnPos));
    ctx.metric("furthest box thrown", thrown, "m", 3, 60);
    ctx.metric("Civic speed kept", speedKmh(c), "km/h", 25, 60);
    ctx.metric("Civic damage (240 kg of foam at 60 km/h)", c.damage * 100, "%", 0, 15);
}

// =============================================================================
// Mods: every engine, suspension and transmission option, against the stock car

namespace {

// Height of the floor above the ground, averaged over the floor nodes.
double floorHeight(const Ctx& ctx, const Car& c) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < c.bodyNodes; i++)
        if (std::fabs(c.restLocal[i].y - c.spec->layerY[0]) < 1e-6) {
            sum += c.nodes[i].p.y - ctx.sim.world.height(c.nodes[i].p.x, c.nodes[i].p.z);
            n++;
        }
    return sum / std::max(1, n);
}

void modScenario(Ctx& ctx, int vehicle, int slot, int option) {
    ModSet ms;
    ms.pick[slot] = option;
    const VehicleSpec& stock = vehicleCatalog()[vehicle];
    const VehicleSpec& spec = tunedSpec(vehicle, ms);
    // The modded car and a stock one side by side.
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    Car& s = ctx.add(stock, kLaneStartX, kLaneZ + 8, 0);
    ctx.focus(c);
    ctx.run(2.5);
    Frame f = c.frame();
    ctx.metric("sits level (1 - up.y)", (1 - f.up.y) * 1000, "milli", 0, 3);
    double load = 0;
    for (const Wheel& w : c.wheels) load += w.load;
    ctx.metric("wheels carry its weight", load / (c.mass * 9.81) * 100, "%", 92, 108);
    // Ride height: the floor moves by exactly what the parts say (lift, lowering, tires).
    double want = (spec.layerY[0] - stock.layerY[0]) * 1000, got = (floorHeight(ctx, c) - floorHeight(ctx, s)) * 1000;
    ctx.metric("ride height change vs the parts (mm off)", got - want, "mm", -15, 15);
    ctx.note("ride height change " + std::to_string((int)std::lround(got)) + " mm");

    // Full throttle to 80 km/h, both cars.
    double t0 = ctx.t, tc = -1, ts = -1;
    ctx.runUntil(20.0, [&] { return tc > 0 && ts > 0; }, [&] {
        for (Car* car : {&c, &s}) {
            car->throttle = 1;
            car->handbrake = 0;
        }
        holdLine(c, kLaneZ);
        holdLine(s, kLaneZ + 8);
        if (tc < 0 && speedKmh(c) >= 80) tc = ctx.t - t0;
        if (ts < 0 && speedKmh(s) >= 80) ts = ctx.t - t0;
        ctx.sample("Speed", "modded", speedKmh(c), "km/h");
        ctx.sample("Speed", "stock", speedKmh(s), "km/h");
    });
    ctx.check("reaches 80 km/h", tc > 0, "stock took " + std::to_string(ts) + " s");
    double ratio = tc > 0 && ts > 0 ? tc / ts : 9;
    ctx.note("0-80 km/h: " + std::to_string(tc) + " s vs stock " + std::to_string(ts) + " s");
    if (slot == MOD_ENGINE) {
        double p = peakPower(spec) / peakPower(stock);
        if (peakPower(stock) / stock.structureMass > 1500)  // ~700 hp per tonne: wheelspin, not power, sets 0-80
            ctx.metric("0-80 time vs stock (traction-limited: about the same)", ratio, "x", 0.85, 1.03);
        else if (p > 1.05) ctx.metric("0-80 time vs stock (more power: quicker)", ratio, "x", 0.5, 0.99);
        else if (p < 0.95) ctx.metric("0-80 time vs stock (less power: slower)", ratio, "x", 1.0, 1.5);
        else ctx.metric("0-80 time vs stock", ratio, "x", 0.8, 1.2);
    } else if (slot == MOD_TRANSMISSION && spec.finalDrive > stock.finalDrive * 1.03 && spec.gears == stock.gears) {
        // (Not always quicker to 80: a shorter final drive can add an upshift on the way.)
        ctx.metric("0-80 time vs stock (shorter gearing: not slower)", ratio, "x", 0.6, 1.05);
    } else {
        ctx.metric("0-80 time vs stock", ratio, "x", 0.6, 1.35);
    }
    ctx.metric("damage", c.damage * 100, "%", 0, 0.3);
    ctx.check("stays upright", c.frame().up.y > 0.9);
}

std::string slug(const std::string& s) {
    std::string out;
    for (char ch : s) out += ch == ' ' ? '-' : (char)std::tolower((unsigned char)ch);
    return out;
}

int modScenarios = [] {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int k = 0; k < MOD_SLOTS; k++)
            for (int o = 1; o < (int)modOptions(v, k).size(); o++)
                scenarioRegistry().push_back({std::string("mods/") + vehicleCatalog()[v].id + "/" + slug(modSlotName(k)) + "/" +
                                                  slug(modOptions(v, k)[o].name),
                                              "Fitted next to a stock car: sits level, right ride height, drives, the expected effect",
                                              [v, k, o](Ctx& c) { modScenario(c, v, k, o); }});
    return 0;
}();

}  // namespace

// =============================================================================
// AI drivers

VEHICLE_SCENARIO(aiLap, "ai/lap", "AI drives a full lap of the loop road: lane keeping, speed, no damage") {
    Car& c = addOnLoop(ctx, spec, 0, 1, true);
    ctx.focus(c);
    LoopProgress prog;
    double errSum = 0, worst = 0;
    int samples = 0;
    bool done = ctx.runUntil(300, [&] { return prog.total >= ctx.sim.world.loopLength; }, [&] {
        prog.update(ctx.sim.world, c.aiIndex, c.aiDir);
        double err = std::fabs(loopLateral(ctx.sim.world, c.frame().com, c.aiIndex) - 2.6 * c.aiDir);
        errSum += err;
        worst = std::max(worst, err);
        samples++;
        if (samples % 15 == 0) ctx.sample("Speed", spec.name, speedKmh(c), "km/h");
    });
    ctx.check("finishes the lap within 5 minutes", done);
    ctx.metric("lap time", ctx.t, "s", 90, 240);
    ctx.metric("average lane error", errSum / std::max(1, samples), "m", 0, 0.7);
    ctx.metric("worst lane error", worst, "m", 0, 2.6);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.5);
}

SCENARIO(aiLapReverse, "ai/lap-reverse", "AI Civic laps the loop the other way round") {
    Car& c = addOnLoop(ctx, vehicleCatalog()[0], 0, -1, true);
    ctx.focus(c);
    LoopProgress prog;
    double worst = 0;
    bool done = ctx.runUntil(300, [&] { return prog.total >= ctx.sim.world.loopLength; }, [&] {
        prog.update(ctx.sim.world, c.aiIndex, c.aiDir);
        worst = std::max(worst, std::fabs(loopLateral(ctx.sim.world, c.frame().com, c.aiIndex) + 2.6));
    });
    ctx.check("finishes the lap", done);
    ctx.metric("worst lane error", worst, "m", 0, 2.6);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.5);
}

SCENARIO(aiFollow, "ai/follow", "Fast AI catches a slow AI in the same lane: follows at a safe gap, no contact") {
    Car& slow = addOnLoop(ctx, vehicleCatalog()[3], 40, 1, true);
    Car& fast = addOnLoop(ctx, vehicleCatalog()[0], 0, 1, true);
    slow.aiCruise = 10;  // 36 km/h
    ctx.focus(fast);
    double minGap = 1e9;
    ctx.run(60, [&] {
        if (fast.aiGap < 1e8) minGap = std::min(minGap, fast.aiGap);
        ctx.sample("Gap to leader", "Civic", std::min(fast.aiGap, 80.0), "m");
        ctx.sample("Speed", "Civic", speedKmh(fast), "km/h");
        ctx.sample("Speed", "4Runner (leader)", speedKmh(slow), "km/h");
    });
    ctx.metric("closest gap", minGap, "m", 3, 40);
    ctx.metric("follower damage", fast.damage * 100, "%", 0, 0.1);
    ctx.metric("leader damage", slow.damage * 100, "%", 0, 0.1);
}

SCENARIO(aiPass, "ai/oncoming", "Two AIs in opposite directions pass each other on the same stretch") {
    Car& a = addOnLoop(ctx, vehicleCatalog()[0], 0, 1, true);
    Car& b = addOnLoop(ctx, vehicleCatalog()[2], 120, -1, true);
    ctx.focus(a);
    ctx.run(25);
    ctx.metric("damage A", a.damage * 100, "%", 0, 0.1);
    ctx.metric("damage B", b.damage * 100, "%", 0, 0.1);
}

SCENARIO(aiObstacle, "ai/obstacle", "A crash box sits in the AI's lane: it stops short, then pulls out and goes around it") {
    const auto& road = ctx.sim.world.loopRoad;
    int idx = 70;
    V3 d = road[idx + 1] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 p = road[idx] + V3(-t.z, 0, t.x) * 2.6;
    ctx.addProp(crashBoxSpec(), p.x, p.z, 0);
    Car& c = addOnLoop(ctx, vehicleCatalog()[0], 0, 1, true);
    Car& box = ctx.sim.cars[0];
    ctx.focus(c);
    double minGap = 1e9, stoppedFor = 0;
    LoopProgress prog;
    ctx.run(40, [&] {
        prog.update(ctx.sim.world, c.aiIndex, c.aiDir);
        if (c.aiGap < 1e8) minGap = std::min(minGap, c.aiGap);
        if (speedKmh(c) < 5 && prog.total > 50) stoppedFor += Ctx::kFrame;
        ctx.sample("Gap", "Civic", std::min(c.aiGap, 100.0), "m");
        ctx.sample("Lane offset", "Civic", c.aiLane, "m");
    });
    double boxAlong = ctx.sim.world.loopAlong[70] - ctx.sim.world.loopAlong[0];
    ctx.metric("closest approach to the box", minGap, "m", 0.5, 10);
    ctx.metric("time spent waiting behind it (below 5 km/h)", stoppedFor, "s", 1.5, 15);
    ctx.check("gets past the box", prog.total > boxAlong + 30, "progress " + std::to_string(prog.total) + " m");
    ctx.metric("box pushed", len(box.frame().com - box.spawnPos), "m", 0, 0.3);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.05);
    ctx.check("back in its own lane afterwards", !c.aiPassing && c.aiLane > 2.0);
}

SCENARIO(aiOffroad, "ai/off-road-return", "AI starts 15 m off the road on grass: drives back and carries on") {
    const auto& road = ctx.sim.world.loopRoad;
    int idx = 200;
    V3 d = road[idx + 1] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 p = road[idx] + V3(-t.z, 0, t.x) * 15;
    Car& c = ctx.add(vehicleCatalog()[0], p.x, p.z, std::atan2(-t.z, t.x) * 180 / M_PI, true);
    c.aiDir = 1;
    ctx.focus(c);
    double back = -1;
    ctx.run(20, [&] {
        if (back < 0 && std::fabs(loopLateral(ctx.sim.world, c.frame().com, c.aiIndex) - 2.6) < 1.5) back = ctx.t;
    });
    ctx.metric("time to get back into its lane", back < 0 ? 99 : back, "s", 0, 12);
}

SCENARIO(aiStuck, "ai/stuck-recovery", "AI starts nose against a barrier: backs off and gets going again") {
    const auto& road = ctx.sim.world.loopRoad;
    int idx = 300;
    V3 d = road[idx + 1] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 lane = road[idx] + V3(-t.z, 0, t.x) * 2.6;
    double heading = std::atan2(-t.z, t.x) * 180 / M_PI;
    // A barrier across the lane ahead and slightly right, the car right behind it, angled into it.
    ctx.addProp(barrierSpec(), lane.x + t.x * 4, lane.z + t.z * 4, heading + 90);
    Car& c = ctx.add(vehicleCatalog()[0], lane.x - t.x * 0.5, lane.z - t.z * 0.5, heading - 25, true);
    c.aiDir = 1;
    ctx.focus(c);
    LoopProgress prog;
    ctx.run(40, [&] { prog.update(ctx.sim.world, c.aiIndex, c.aiDir); });
    ctx.metric("road progress after 40 s", prog.total, "m", 100, 5000);
}

SCENARIO(aiTraffic, "ai/traffic", "Eight AI vehicles share the loop for 90 s: nobody crashes") {
    const auto& cat = vehicleCatalog();
    int n = (int)ctx.sim.world.loopRoad.size();
    for (int k = 0; k < 8; k++) addOnLoop(ctx, cat[k % 4], k * n / 8, k % 2 ? -1 : 1, true);
    ctx.focus(ctx.sim.cars[0]);
    ctx.run(90);
    double worst = 0;
    int stuck = 0;
    for (Car& c : ctx.sim.cars) {
        worst = std::max(worst, c.damage);
        stuck += speedKmh(c) < 3;
    }
    ctx.metric("worst damage", worst * 100, "%", 0, 0.3);
    ctx.metric("vehicles stopped at the end", stuck, "", 0, 1);
}

// =============================================================================
// Engine-level properties

namespace {
// A short, busy scenario used for determinism and step-size checks.
double brakeDistance(double h) {
    Sim s;
    s.setupEmpty();
    s.h = h;
    Ctx c(s);
    Car& car = c.add(vehicleCatalog()[0], kLaneStartX, kLaneZ, 0);
    c.run(1.0);
    c.launch(car, 100 / 3.6);
    V3 p0 = car.frame().com;
    c.runUntil(8, [&] { return speedKmh(car) < 0.5; }, [&] { car.brake = 1; });
    return travelled(car, p0, V3(1, 0, 0));
}
}  // namespace

SCENARIO(determinism, "engine/determinism", "Same crash run twice gives bit-identical results") {
    auto runOnce = [] {
        Sim s;
        s.setupEmpty();
        Ctx c(s);
        Car& a = c.add(vehicleCatalog()[0], -20, kLaneZ, 0);
        Car& b = c.add(vehicleCatalog()[2], 20, kLaneZ, 180);
        c.run(0.5);
        c.launch(a, 15);
        c.launch(b, 15);
        c.run(2.0);
        V3 sum;
        for (const Car& car : s.cars)
            for (const Node& n : car.nodes) sum += n.p;
        return sum;
    };
    V3 r1 = runOnce(), r2 = runOnce();
    ctx.check("identical node positions", r1.x == r2.x && r1.y == r2.y && r1.z == r2.z);
}

SCENARIO(timestep, "engine/timestep", "Braking distance barely changes when the physics step is halved (2 kHz vs 4 kHz)") {
    double a = brakeDistance(1.0 / 2000), b = brakeDistance(1.0 / 4000);
    ctx.metric("2 kHz braking distance", a, "m", 30, 50);
    ctx.metric("4 kHz braking distance", b, "m", 30, 50);
    ctx.metric("difference", std::fabs(a - b) / b * 100, "%", 0, 3);
}

SCENARIO(sleeping, "engine/sleep", "Default map: parked vehicles and the prop stacks settle and go to sleep") {
    ctx.sim.setup();
    ctx.run(9.0);
    int awake = 0, total = 0;
    for (size_t i = 0; i < ctx.sim.cars.size(); i++) {
        if ((int)i == ctx.sim.player) continue;
        total++;
        awake += !ctx.sim.cars[i].sleeping;
    }
    ctx.metric("bodies still awake after 9 s", awake, "", 0, 0);
    ctx.metric("bodies on the map", total, "", 10, 60);
    double dmg = 0;
    for (Car& c : ctx.sim.cars) dmg = std::max(dmg, c.damage);
    ctx.metric("worst damage from just sitting", dmg * 100, "%", 0, 0.1);
}

SCENARIO(perf, "engine/performance", "Twelve vehicles all awake and driving: simulation speed vs real time") {
    const auto& cat = vehicleCatalog();
    for (int i = 0; i < 12; i++) ctx.add(cat[i % 4], -100 + 14.0 * (i % 6), -40 - 12.0 * (i / 6), 0);
    ctx.run(0.5);
    auto t0 = std::chrono::steady_clock::now();
    ctx.run(3.0, [&] {
        for (Car& c : ctx.sim.cars) c.throttle = 0.6;
    });
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ctx.metric("real-time factor (single core, may vary under load)", 3.0 / wall, "x", 1.0, 1000);
}

VEHICLE_SCENARIO(steerSnap, "structure/steering-snap", "Steering snapped lock to lock while pulling away: the rack keeps up without bending its tie rods") {
    // The AI (or a keyboard) can ask for full lock in one frame. A real rack takes ~0.4 s
    // to swing the wheels lock to lock, and the tie rods mustn't take a set from it.
    Car& c = ctx.add(spec, kLaneStartX, kLaneZ, 0);
    ctx.focus(c);
    ctx.run(1.0);
    for (double st : {0.3, 0.55, -0.55, 0.55})
        ctx.run(1.0, [&] {
            c.steer = st;
            c.steerInput = st / 0.6;
            c.throttle = 0.3;
            c.handbrake = 0;
        });
    ctx.metric("tie rod bend (worst)", std::max(std::fabs(c.wheels[0].tieOffset), std::fabs(c.wheels[1].tieOffset)) * 1000, "mm", 0, 0.5);
    ctx.metric("damage", c.damage * 100, "%", 0, 0.1);
}


VEHICLE_SCENARIO(barrierBump, "damage/barrier-bump", "18 km/h into a concrete barrier, square and at an angle: dented, nothing torn") {
    // A parking-lot knock. (Contact once pushed nose nodes out through the barrier's top
    // instead of its face and tore the front apart; short nose beams also gave way.)
    for (double ang : {0.0, 25.0}) {
        Sim sim2;
        sim2.setupEmpty();
        Ctx c2(sim2);
        c2.addProp(barrierSpec(), 0, 0, 90);
        Car& c = c2.add(spec, -1.5 - spec.length / 2, 0, ang);
        c2.run(1.0);
        c2.launch(c, 18 / 3.6);
        c2.run(2.5);
        const std::string tag = ang == 0 ? " (square)" : " (25 deg)";
        // (A light car's grille brace may crack as the nose rides on the barrier face.)
        ctx.metric("body beams torn" + tag, c.brokenBeams, "", 0, 2);
        ctx.metric("damage" + tag, c.damage * 100, "%", 0, 3);
        ctx.metric("engine health" + tag, c.engineHealth, "", 0.9, 1.0);
    }
}
