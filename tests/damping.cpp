// Shock absorbers: how the body settles after a bump, and how the wheels follow rough ground.
//
// Push-down-and-let-go (the classic fender bounce test), a 0.3 m drop onto flat ground, a
// 10 cm speed bump at 30 km/h, a lane of small random bumps at 50 km/h, and (survey only)
// 0.6 and 1 m drops and the mountain's whoops lane at 40 km/h. CZ_DAMP_TRACE=file.csv
// appends the body-height traces (test, vehicle, t, height off its final level).
// Real cars settle a body bump in about one to one and a half cycles: a first overshoot of
// ~15-40 % of the push (body damping ratio ~0.25-0.5), and nothing after the second.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "harness.h"
#include "../src/mods.h"

namespace {

// The sprung mass: every node that isn't part of a wheel's assembly.
struct Sprung {
    std::vector<int> nodes;
    double m = 0;
    explicit Sprung(const Car& c) {
        std::vector<char> unsprung(c.nodes.size(), 0);
        for (const Wheel& w : c.wheels)
            for (int n : w.assembly) unsprung[n] = 1;
        for (size_t i = 0; i < c.nodes.size(); i++)
            if (!unsprung[i]) nodes.push_back((int)i), m += c.nodes[i].m;
    }
    double y(const Car& c) const {
        double s = 0;
        for (int i : nodes) s += c.nodes[i].p.y * c.nodes[i].m;
        return s / m;
    }
    double vy(const Car& c) const {
        double s = 0;
        for (int i : nodes) s += c.nodes[i].v.y * c.nodes[i].m;
        return s / m;
    }
};

// A decaying oscillation about its final level, measured from the moment it's let go.
struct Settle {
    double amp = 0;        // the starting excursion (m)
    double overshoot = 0;  // the first overshoot past the final level, as a share of the start
    int overshoots = 0;    // swings past 10 % of the start (after the first release)
    double settle = 0;     // last time it was more than 10 % of the start away (s)
    double zeta = 0;       // damping ratio from the first overshoot
    double freq = 0;       // Hz, from the first two turning points
};

Settle analyse(const std::vector<double>& t, const std::vector<double>& y, double yEq) {
    Settle s;
    if (y.size() < 4) return s;
    s.amp = std::fabs(y[0] - yEq);
    if (s.amp < 1e-4) return s;
    const double sign0 = y[0] - yEq > 0 ? 1 : -1;
    std::vector<std::pair<double, double>> ext;  // (time, excursion) of each turning point
    for (size_t i = 1; i + 1 < y.size(); i++) {
        const double a = y[i - 1] - yEq, b = y[i] - yEq, c = y[i + 1] - yEq;
        if ((b > a && b >= c) || (b < a && b <= c)) ext.push_back({t[i], b});
    }
    for (const auto& e : ext)
        if (std::fabs(e.second) > 0.1 * s.amp) s.overshoots++;
    for (const auto& e : ext)
        if (e.second * sign0 < 0) {
            s.overshoot = std::fabs(e.second) / s.amp;
            break;
        }
    for (size_t i = 0; i < y.size(); i++)
        if (std::fabs(y[i] - yEq) > 0.1 * s.amp) s.settle = t[i] - t[0];
    const double os = std::max(s.overshoot, 1e-4), l = std::log(os);
    s.zeta = s.overshoot > 0 ? -l / std::sqrt(M_PI * M_PI + l * l) : 1;
    if (ext.size() >= 2) s.freq = 0.5 / std::max(1e-3, ext[1].first - ext[0].first);
    return s;
}

void trace(const char* test, const VehicleSpec& spec, const std::vector<double>& t, const std::vector<double>& y, double yEq) {
    const char* path = std::getenv("CZ_DAMP_TRACE");
    if (!path) return;
    FILE* f = std::fopen(path, "a");
    if (!f) return;
    for (size_t i = 0; i < t.size(); i++) std::fprintf(f, "%s,%s,%.4f,%.5f\n", test, spec.name, t[i], y[i] - yEq);
    std::fclose(f);
}

// Pushed down by `push` of its sprung weight until it's still, then let go.
Settle pushAndRelease(Ctx& ctx, const VehicleSpec& spec, double z, double push) {
    ctx.sim.cars.clear();
    Car& c = ctx.add(spec, -100, z, 0);
    ctx.focus(c);
    const Sprung sp(c);
    c.extForce.assign(c.nodes.size(), V3());
    ctx.run(1.0, [&] { c.handbrake = 1; });
    for (double k = 0; k <= 1.0; k += 0.05) {  // eased on (it sinks without a bounce)
        for (int i : sp.nodes) c.extForce[i] = V3(0, -push * k * 9.81 * c.nodes[i].m, 0);
        ctx.run(0.1, [&] { c.handbrake = 1; });
    }
    ctx.run(1.5, [&] { c.handbrake = 1; });
    for (V3& f : c.extForce) f = V3();
    c.wake();
    std::vector<double> t, y;
    const double t0 = ctx.t;
    t.push_back(0), y.push_back(sp.y(c));
    ctx.run(4.0, [&] {
        c.handbrake = 1;
        t.push_back(ctx.t - t0), y.push_back(sp.y(c));
    });
    double yEq = 0;
    int n = 0;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] > 3.0) yEq += y[i], n++;
    yEq /= std::max(1, n);
    trace("release", spec, t, y, yEq);
    return analyse(t, y, yEq);
}

// Lifted `h` and dropped flat onto the apron: the landing's bounce, from the deepest squat on.
struct Drop {
    Settle s;
    double airborne = 0;  // time any wheel spends off the ground after first touching down (s)
};
Drop dropTest(Ctx& ctx, const VehicleSpec& spec, double z, double h) {
    ctx.sim.cars.clear();
    Car& c = ctx.add(spec, -100, z, 0);
    ctx.focus(c);
    const Sprung sp(c);
    ctx.run(2.0, [&] { c.handbrake = 1; });
    for (Node& n : c.nodes) n.p.y += h, n.v = V3();
    c.wake();
    std::vector<double> t, y;
    double tLow = -1, yLow = 1e9, touched = -1;
    Drop d;
    const double t0 = ctx.t;
    ctx.run(4.5, [&] {
        c.handbrake = 1;
        t.push_back(ctx.t - t0), y.push_back(sp.y(c));
        bool any = false, all = true;
        for (const Wheel& w : c.wheels) any |= w.load > 0, all &= w.load > 0;
        if (touched < 0 && any && ctx.t - t0 > 0.05) touched = ctx.t;
        if (touched > 0 && !all) d.airborne += Ctx::kFrame;
    });
    double yEq = 0;
    int n = 0;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] > 3.5) yEq += y[i], n++;
    yEq /= std::max(1, n);
    for (size_t i = 0; i < t.size(); i++)
        if (y[i] < yLow) yLow = y[i], tLow = t[i];
    std::vector<double> tt, yy;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] >= tLow) tt.push_back(t[i]), yy.push_back(y[i]);
    trace("drop", spec, tt, yy, yEq);
    d.s = analyse(tt, yy, yEq);
    return d;
}

// Over a 10 cm, 0.6 m long speed bump at `kmh` (in a clear lane of the apron: the ramp block sits at x -85..-55, |z| < 15): the hardest jolt through the body, how long
// wheels are off the ground, and how much the body is still bouncing a second after.
struct BumpRun {
    double peakG = 0, airborne = 0, residual = 0;
};
BumpRun speedBump(Ctx& ctx, const VehicleSpec& spec, double z, double kmh) {
    ctx.sim.cars.clear();
    World& w = ctx.sim.world;
    const double bx = -100;
    w.addBox(V3(bx, w.height(bx, z) + 0.05, z), V3(0.3, 0.05, 4), 0, 0, 0, V3(0.5, 0.5, 0.5));
    w.buildGrid();
    Car& c = ctx.add(spec, bx - 22, z, 0);
    ctx.focus(c);
    const Sprung sp(c);
    ctx.run(1.0);
    ctx.launch(c, kmh / 3.6);
    double vyWas = sp.vy(c), xRearPast = 1e9;
    std::vector<double> yAfter, tb, yb;
    BumpRun r;
    ctx.runUntil(8, [&] { return c.frame().com.x > bx + 20; }, [&] {
        const double v = speedKmh(c);
        c.throttle = clampd((kmh - v) * 0.15, 0, 1);
        c.brake = 0, c.handbrake = 0, c.steer = 0;
        const double vy = sp.vy(c);
        const Frame fr = c.frame();
        double xRear = 1e9;
        for (const Wheel& wh : c.wheels) xRear = std::min(xRear, c.nodes[wh.outer].p.x);
        const bool near = fr.com.x > bx - 4 && xRear < bx + 2;
        if (near) r.peakG = std::max(r.peakG, std::fabs(vy - vyWas) / Ctx::kFrame / 9.81);
        if (near)
            for (const Wheel& wh : c.wheels)
                if (wh.load <= 0) r.airborne += Ctx::kFrame / c.wheels.size();
        if (xRear > bx + 1 && xRearPast > 1e8) xRearPast = ctx.t;
        // (Against the ground under it: the apron isn't quite level.)
        const double ride = sp.y(c) - w.height(fr.com.x, fr.com.z);
        if (xRearPast < 1e8 && ctx.t > xRearPast + 0.8 && ctx.t < xRearPast + 2.3) yAfter.push_back(ride);
        if (fr.com.x > bx - 6) tb.push_back(fr.com.x - bx), yb.push_back(ride);
        vyWas = vy;
    });
    w.boxes.pop_back();
    w.buildGrid();
    trace("bump", spec, tb, yb, yb.empty() ? 0 : yb.back());
    if (yAfter.size() > 4) {
        double lo = 1e9, hi = -1e9;
        for (double y : yAfter) lo = std::min(lo, y), hi = std::max(hi, y);
        r.residual = hi - lo;
    }
    return r;
}

// The whoops lane at `kmh`: the share of time each wheel is on the ground, and the body's
// vertical acceleration (RMS, g).
struct Rough {
    double ground = 0, accelRms = 0;
    bool finished = false;
};
Rough whoops(Ctx& ctx, const VehicleSpec& spec, double kmh) {
    ctx.sim.cars.clear();
    const V3 f = mountainFeature("whoops");
    Car& c = ctx.add(spec, f.x, f.z - 45, -90);
    ctx.focus(c);
    const Sprung sp(c);
    ctx.run(1.0);
    double tIn = -1, tOut = -1, vyWas = sp.vy(c), a2 = 0, on = 0, n = 0;
    ctx.runUntil(30, [&] { return tOut > 0; }, [&] {
        const Frame fr = c.frame();
        V3 d = V3(f.x, fr.com.y, fr.com.z + 8) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)), -0.3, 0.3);
        const double v = len(fr.vel) * 3.6;
        c.throttle = clampd(0.3 + (kmh - v) * 0.2, 0, 1);
        c.brake = v > kmh + 8 ? 0.3 : 0;
        c.handbrake = 0;
        if (tIn < 0 && fr.com.z > f.z) tIn = ctx.t;
        if (tIn > 0 && tOut < 0 && fr.com.z > f.z + 100) tOut = ctx.t;
        const double vy = sp.vy(c);
        if (tIn > 0 && tOut < 0) {
            const double a = (vy - vyWas) / Ctx::kFrame / 9.81;
            a2 += a * a;
            for (const Wheel& w : c.wheels) on += w.load > 0 ? 1.0 / c.wheels.size() : 0;
            n++;
        }
        vyWas = vy;
    });
    Rough r;
    r.finished = tOut > 0;
    if (n > 0) r.ground = on / n, r.accelRms = std::sqrt(a2 / n);
    return r;
}

// A lane of small, random bumps (3-8 cm, like a broken-up dirt road) on the apron at `kmh`:
// the share of time the wheels are on the ground, and the body's vertical acceleration (RMS, g).
Rough bumpyLane(Ctx& ctx, const VehicleSpec& spec, double kmh) {
    ctx.sim.cars.clear();
    World& w = ctx.sim.world;
    const double z = -50, x0 = -150, x1 = -95;
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
    int added = 0;
    for (double x = x0; x < x1; x += 1.2 + 2.5 * rnd()) {
        const double hgt = 0.03 + 0.05 * rnd(), lenh = 0.2 + 0.3 * rnd(), off = (rnd() - 0.5) * 1.6;
        w.addBox(V3(x, w.height(x, z) + hgt / 2 - 0.01, z + off), V3(lenh, hgt / 2, 0.9), 0, 0, 0, V3(0.5, 0.45, 0.4));
        added++;
    }
    w.buildGrid();
    Car& c = ctx.add(spec, x0 - 18, z, 0);
    ctx.focus(c);
    const Sprung sp(c);
    ctx.run(1.0);
    ctx.launch(c, kmh / 3.6);
    double vyWas = sp.vy(c), a2 = 0, on = 0, n = 0;
    ctx.runUntil(12, [&] { return c.frame().com.x > x1 + 3; }, [&] {
        const Frame fr = c.frame();
        const double v = speedKmh(c);
        c.throttle = clampd((kmh - v) * 0.15, 0, 1);
        c.brake = 0, c.handbrake = 0;
        const V3 d = V3(fr.com.x + 10, fr.com.y, z) - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)), -0.15, 0.15);
        const double vy = sp.vy(c);
        if (fr.com.x > x0 + 2 && fr.com.x < x1) {
            const double a = (vy - vyWas) / Ctx::kFrame / 9.81;
            a2 += a * a;
            for (const Wheel& wh : c.wheels) on += wh.load > 0 ? 1.0 / c.wheels.size() : 0;
            n++;
        }
        vyWas = vy;
    });
    for (int i = 0; i < added; i++) w.boxes.pop_back();
    w.buildGrid();
    Rough r;
    r.finished = c.frame().com.x > x1;
    if (n > 0) r.ground = on / n, r.accelRms = std::sqrt(a2 / n);
    return r;
}

// The most a 0.3 m drop may overshoot: long-travel off-roaders (and the Beetle's long trailing
// arms) settle almost at once; the rest about as a real car on good shocks (body damping ratio
// ~0.3-0.4: a first overshoot under ~35 %). On the old valving the Civic, Jesko and the trucks
// overshot 37-42 %.
double dropLimit(const VehicleSpec& s) {
    const std::string n = s.name;
    if (n == "TURBO UTV" || n == "ROCK BUGGY" || n == "VW BAJA BUG") return 25;
    return 36;
}

}  // namespace

VEHICLE_SCENARIO(bumpSettle, "dynamics/bump-settle",
                 "The shocks: pushed down and let go, and dropped 0.3 m, the body settles in about a cycle (no repeated pogo); over a speed bump and a lane of small bumps it isn't harsh and the wheels stay on the ground") {
    // The explicit step's margin on the shocks' end nodes (h^2 k/m + 2 h c/m along the shock, at its
    // steepest: the bump stop and the low-speed rebound slope; the hard limit is 4). The lightest
    // node on a coil-over is usually a knuckle's or a UTV's light arm end.
    {
        Car& c = ctx.add(spec, -100, 0, 0);
        const double h = 1.0 / 2000.0;
        double worst = 0, lightest = 1e9;
        for (const Beam& b : c.beams) {
            if (b.type != BEAM_SPRING) continue;
            for (int n : {b.a, b.b}) {
                const double m = c.nodes[n].m;
                worst = std::max(worst, (h * h * 26 * b.k + 2 * h * Damper::kMaxSlope * b.c) / m);
                lightest = std::min(lightest, m);
            }
        }
        std::printf("STEP %-18s shock end nodes: lightest %.1f kg, worst h^2k/m + 2hc/m %.2f (limit 4)\n", spec.name, lightest, worst);
        ctx.metric("the shocks' end nodes step stably (h^2 k/m + 2 h c/m)", worst, "", 0, 2.0);
        ctx.sim.cars.clear();
    }
    const Settle s = pushAndRelease(ctx, spec, 0, 0.4);
    const Drop d = dropTest(ctx, spec, 0, 0.3);
    const BumpRun br = speedBump(ctx, spec, -40, 30);
    const Rough lane = bumpyLane(ctx, spec, 50);
    std::printf("DAMP %-18s release: amp %.0f mm, overshoot %.0f %%, %d swings, settle %.2f s, zeta %.2f, %.2f Hz | drop: overshoot %.0f %%, %d swings, settle %.2f s, air %.2f s | bump: peak %.2f g, air %.2f s, residual %.0f mm\n",
                spec.name, s.amp * 1000, s.overshoot * 100, s.overshoots, s.settle, s.zeta, s.freq, d.s.overshoot * 100, d.s.overshoots, d.s.settle,
                d.airborne, br.peakG, br.airborne, br.residual * 1000);
    std::printf("LANE %-18s bumpy lane 50: wheels on the ground %.0f %%, body accel %.2f g rms%s\n", spec.name, lane.ground * 100, lane.accelRms, lane.finished ? "" : " (didn't finish)");
    // Pushed down 40 % of its weight and let go: a small, slow body bounce.
    ctx.metric("let go: first overshoot (share of the push)", s.overshoot * 100, "%", 0, 30);
    ctx.metric("let go: swings past 10 % of the push", s.overshoots, "", 0, 2);
    ctx.metric("let go: settled to within 10 %", s.settle, "s", 0, 0.8);
    // Dropped 0.3 m flat: a fast squat (most of them onto the bump stops), then the rebound.
    ctx.metric("dropped 0.3 m: first overshoot (share of the squat)", d.s.overshoot * 100, "%", 5, dropLimit(spec));
    ctx.metric("dropped 0.3 m: swings past 10 % of the squat", d.s.overshoots, "", 0, 2);
    ctx.metric("dropped 0.3 m: settled to within 10 %", d.s.settle, "s", 0, 0.9);
    // Not over-damped: sharp hits stay soft and the wheels follow the ground.
    ctx.metric("speed bump at 30 km/h: body still moving a second after (peak to peak)", br.residual * 1000, "mm", 0, 15);
    ctx.metric("speed bump at 30 km/h: hardest jolt through the body", br.peakG, "g", 0, 3.0);
    ctx.check("through the bumpy lane", lane.finished);
    // (Before the valving change: wheels down 64-85 %, 0.34-0.76 g; the Jesko is the stiffest, the UTV the
    // lightest and quickest to skip. Over-damped shocks would skip the wheels and jar the body.)
    ctx.metric("bumpy lane at 50 km/h: wheels on the ground", lane.ground * 100, "%", 58, 100);
    ctx.metric("bumpy lane at 50 km/h: body vertical acceleration (RMS)", lane.accelRms, "g", 0, 0.85);
}

SCENARIO(dampingSurvey, "survey/damping", "Survey (no limits): the Civic's, Bronco's and UTV's garage suspension kits; hard landings (dropped 0.6 and 1 m) and the whoops lane at 40 km/h for every vehicle") {
    for (const VehicleSpec& s : vehicleCatalog()) {
        for (double h : {0.6, 1.0}) {
            const Drop d = dropTest(ctx, s, 0, h);
            std::printf("JUMP %-18s dropped %.1f m: squat %.0f mm, overshoot %.0f %%, %d swings, settle %.2f s, wheels up %.2f s\n", s.name, h, d.s.amp * 1000,
                        d.s.overshoot * 100, d.s.overshoots, d.s.settle, d.airborne);
        }
    }
    // The garage's suspension kits: the same valving, scaled by each kit's damping ratio.
    for (int v : {0, 2, 5}) {
        const auto& opts = modOptions(v, MOD_SUSPENSION);
        for (size_t o = 0; o < opts.size(); o++) {
            if (opts[o].name.find(':') != std::string::npos) continue;  // (its own kits, not swaps from other vehicles)
            ModSet ms;
            ms.pick[MOD_SUSPENSION] = (int)o;
            const VehicleSpec& ts = tunedSpec(v, ms);
            const Settle r = pushAndRelease(ctx, ts, 0, 0.4);
            const Drop d = dropTest(ctx, ts, 0, 0.3);
            std::printf("KIT %-18s %-22s damping %.2f: let go overshoot %.0f %% (%.2f Hz), dropped 0.3 m overshoot %.0f %%, %d swings, settle %.2f s\n", vehicleCatalog()[v].name,
                        opts[o].name.c_str(), ts.dampingRatio, r.overshoot * 100, r.freq, d.s.overshoot * 100, d.s.overshoots, d.s.settle);
        }
    }
    ctx.sim.world = World(MAP_MOUNTAIN);
    for (const VehicleSpec& s : vehicleCatalog()) {
        const Rough r = whoops(ctx, s, 40);
        std::printf("ROUGH %-18s whoops 40: wheels on the ground %.0f %%, body accel %.2f g rms%s\n", s.name, r.ground * 100, r.accelRms, r.finished ? "" : " (didn't finish)");
        ctx.note(std::string(s.name) + ": wheels down " + std::to_string((int)(r.ground * 100)) + " %");
    }
}
