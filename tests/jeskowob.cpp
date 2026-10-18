// Jesko straight-line wobble under power. Tuned with the race boost, the track setup and race
// slicks, full throttle down a straight set it weaving at ~3 Hz from ~150 km/h (yaw +-15 deg/s,
// stability control braking a front wheel each way), and on race slicks alone it spun: the
// drive load toed the rear wheels, through a knuckle that was a near-flat truss and links far
// more compliant in toe than real arms. Keys go in the way the game puts them (Sim::drive,
// steering assist on), at 60 Hz and at an uncapped ~400 fps with jittery frames.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/mods.h"
#include "harness.h"

namespace {

// A mile of flat pavement on the freeway map, along +X at z = -400.
void flatStrip(World& w) {
    for (double x = -300; x < 1700; x += 1)
        for (double z = -440; z < -360; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellHeight(id, 0), w.setCellSurface(id, SURF_PAVED, PAINT_PAD);
        }
    w.buildGrid();
}

// The swing of a 60 Hz series about its own ~0.2 s running mean (what's left: ~2 Hz and up), rms.
double swing(const std::vector<double>& v) {
    const int win = 6;
    if ((int)v.size() <= 2 * win) return 0;
    double s = 0;
    int n = 0;
    for (int i = win; i + win < (int)v.size(); i++) {
        double m = 0;
        for (int k = -win; k <= win; k++) m += v[i + k];
        m /= 2 * win + 1;
        s += (v[i] - m) * (v[i] - m), n++;
    }
    return std::sqrt(s / std::max(1, n));
}

void startOnStrip(Ctx& ctx, const VehicleSpec& spec) {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_FREEWAY);
    flatStrip(ctx.sim.world);
    Car& c = ctx.add(spec, -250, -400, 0);
    c.blowoutsOn = false;  // (these measure the car, not its tires: the Jesko is past its slicks' 270 km/h rating)
    ctx.sim.player = 0;
    ctx.sim.steeringAssist = true;
    ctx.focus(c);
    ctx.run(1.0);
}

struct Wob {
    double yaw = 0, roll = 0, kmh = 0;  // yaw and roll rate swing (deg/s rms), speed at the end
};

// Drives `seconds` in frames of `dt` (each one dt * (1 +- jitter)): full throttle from a standstill
// (hold = 0) or holding `hold` km/h, tapping the steering to stay in lane, nudged into a 2 deg/s
// yaw every 2 s (a seam, a gust). Swings over the time after `from`.
Wob drive(Ctx& ctx, const VehicleSpec& spec, double seconds, double from, double hold, double dt, double jitter) {
    startOnStrip(ctx, spec);
    Car& c = ctx.sim.cars[0];
    if (hold > 0) ctx.launch(c, hold / 3.6);
    std::vector<double> yaw, roll;
    double prevYaw = 0, prevRoll = 0, t = 0, sampleT = 0;
    bool first = true;
    unsigned seed = 12345;
    while (t < seconds) {
        double d = dt;
        if (jitter > 0) {
            seed = seed * 1103515245u + 12345u;
            d *= 1 + jitter * (((seed >> 8) & 0xffff) / 32768.0 - 1);
        }
        const Frame f = c.frame();
        if (std::fmod(t, 2.0) < d && t > 0.5)
            for (Node& nd : c.nodes) nd.v += cross(V3(0, 2 * M_PI / 180, 0), nd.p - f.com);
        // (+z is to the right facing +x; heading + is turned right.)
        const double off = f.com.z + 400, head = std::atan2(f.fwd.z, f.fwd.x), want = off * 0.02 + head;
        ctx.sim.drive(hold <= 0 || len(f.vel) * 3.6 < hold, false, want > 0.02, want < -0.02, false, d);
        ctx.sim.advance(d, 1000);
        for (Car& cc : ctx.sim.cars) cc.updateStats();
        ctx.sim.events.clear();
        t += d, sampleT += d;
        if (sampleT < Ctx::kFrame) continue;
        sampleT -= Ctx::kFrame;  // (sampled at 60 Hz whatever the frame rate)
        const Frame g = c.frame();
        const double y = std::atan2(g.fwd.z, g.fwd.x) * 180 / M_PI, r = std::asin(g.left.y) * 180 / M_PI;
        if (!first && t >= from) yaw.push_back((y - prevYaw) / Ctx::kFrame), roll.push_back((r - prevRoll) / Ctx::kFrame);
        first = false;
        prevYaw = y, prevRoll = r;
    }
    return {swing(yaw), swing(roll), speedKmh(c)};
}

// Full power held at about 150-250 km/h (as up a steep grade: a uniform pull back on the whole
// car), kicked into a 3 deg/s yaw: the yaw rate's swing 1-2 s after the kick against the first
// half second (under 1, it dies away).
double gradeKick(Ctx& ctx, const VehicleSpec& spec) {
    startOnStrip(ctx, spec);
    Car& c = ctx.sim.cars[0];
    ctx.launch(c, 150 / 3.6);
    double prevYaw = 0, a0 = 0, a1 = 0;
    int n0 = 0, n1 = 0;
    const double t0 = ctx.t;
    ctx.run(3.6, [&] {
        const Frame f = c.frame();
        const double tt = ctx.t - t0;
        const V3 back = norm(V3(f.fwd.x, 0, f.fwd.z)) * (-0.5 * 9.81 * Ctx::kFrame);
        for (Node& nd : c.nodes) nd.v += back;
        if (std::fabs(tt - 1.5) < 0.5 * Ctx::kFrame)
            for (Node& nd : c.nodes) nd.v += cross(V3(0, 3 * M_PI / 180, 0), nd.p - f.com);
        c.throttle = 1, c.brake = 0, c.steer = 0, c.handbrake = 0;
        const double y = std::atan2(f.fwd.z, f.fwd.x) * 180 / M_PI, r = (y - prevYaw) / Ctx::kFrame;
        prevYaw = y;
        if (tt > 1.55 && tt < 2.05) a0 += r * r, n0++;
        if (tt > 2.55 && tt < 3.55) a1 += r * r, n1++;
        ctx.sample("Yaw rate, full power, kicked", spec.modSummary.empty() ? "stock" : spec.modSummary, r, "deg/s");
    });
    return std::sqrt(a1 / std::max(1, n1)) / std::max(1e-6, std::sqrt(a0 / std::max(1, n0)));
}

// The Jesko with the garage options of these names (others stock).
const VehicleSpec& jeskoWith(std::initializer_list<const char*> names) {
    ModSet ms;
    for (const char* name : names)
        for (int k = 0; k < MOD_SLOTS; k++)
            for (int o = 0; o < (int)modOptions(1, k).size(); o++)
                if (modOptions(1, k)[o].name == name) ms.pick[k] = o;
    return tunedSpec(1, ms);
}

}  // namespace

SCENARIO(jeskoStraightAccel, "dynamics/jesko-straight-accel",
         "Jesko flat out down a straight from a standstill and held at full power, stock, on race slicks and on the full track tune: no weave, no spin") {
    struct Build {
        const char* name;
        const VehicleSpec* spec;
    } builds[] = {
        {"stock", &vehicleCatalog()[1]},
        {"race slicks", &jeskoWith({"RACE SLICKS"})},
        {"boost, track setup, slicks", &jeskoWith({"RACE FUEL AND BOOST", "TRACK SETUP", "TRACK DIFF MAP", "RACE SLICKS"})},
    };
    for (const Build& b : builds) {
        const std::string on = b.name;
        // (Before the fix, on the full tune: yaw 6.5 deg/s and roll 14.7 deg/s rms, a 3 Hz weave from
        // ~150 km/h, and the kick grew x3.7 in a second; on slicks alone it spun out at ~200 km/h.
        // Now ~0.3 and ~0.4, and the kick dies away, x0.01.)
        const Wob w = drive(ctx, *b.spec, 12, 1.5, 0, Ctx::kFrame, 0);
        std::printf("%s: flat out to %.0f km/h, yaw swing %.2f deg/s, roll swing %.2f deg/s\n", b.name, w.kmh, w.yaw, w.roll);
        ctx.metric(on + ": flat out from a standstill, yaw rate swing", w.yaw, "deg/s", 0, 0.5);
        ctx.metric(on + ": flat out, roll rate swing", w.roll, "deg/s", 0, 0.6);
        ctx.metric(on + ": flat out, speed after 12 s (no spin)", w.kmh, "km/h", 300, 450);
        const double grow = gradeKick(ctx, *b.spec);
        std::printf("%s: full power kicked, yaw swing a second later x%.3f\n", b.name, grow);
        ctx.metric(on + ": full power, kicked: yaw swing a second later against just after", grow, "x", 0, 0.3);
    }
    // At an uncapped frame rate (the game's ~400 fps, frames 2.5 ms +- 40%) and at a steady 150.
    const VehicleSpec& tuned = *builds[2].spec;
    const Wob fast = drive(ctx, tuned, 12, 1.5, 0, 1.0 / 400, 0.4);
    std::printf("full tune at ~400 fps: flat out to %.0f km/h, yaw swing %.2f deg/s, roll swing %.2f deg/s\n", fast.kmh, fast.yaw, fast.roll);
    ctx.metric("full tune at ~400 fps, jittery frames: flat out, yaw rate swing", fast.yaw, "deg/s", 0, 0.5);
    const Wob cruise = drive(ctx, tuned, 8, 2, 150, Ctx::kFrame, 0);
    std::printf("full tune at a steady 150: yaw swing %.2f deg/s, roll swing %.2f deg/s\n", cruise.yaw, cruise.roll);
    ctx.metric("full tune at a steady 150 km/h: yaw rate swing", cruise.yaw, "deg/s", 0, 0.4);
}
