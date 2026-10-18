// The freeway: an endless straight (it repeats every 400 m and the sim moves everything
// back a period as you go), for top speed.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/mods.h"
#include "harness.h"

namespace {

constexpr double kLaneZ = 6.9;  // the middle eastbound lane

// Keeps the car in its lane the way a driver does at speed: asks for a little sideways
// pull toward the lane center (and against any drift), then steers just enough for that
// (the angle a given pull needs falls with the square of the speed).
// `trim` is the driver's slow correction: the caller keeps one per car.
void holdLane(Car& c, double z, double& trim) {
    const Frame f = c.frame();
    const double v = std::max(5.0, len(f.vel));
    const double e = f.com.z - z;  // + = right of the lane center (+z is right, heading +x)
    // (Plus a slow trim, as a driver holds against a steady pull: a lifted truck with a
    // panhard-located axle drifts a little to one side under power.)
    trim = clampd(trim + 0.08 * e * (1.0 / 60), -1.0, 1.0);
    const double pull = clampd(0.25 * e + trim + 2.5 * f.vel.z, -3.0, 3.0);  // m/s^2 to the left
    c.steer = clampd(c.wheelbase * pull / (v * v), -0.1, 0.1);
}

}  // namespace

VEHICLE_SCENARIO(topSpeed, "freeway/top-speed", "Flat out down the endless freeway until it stops gaining: top speed, and it stays in its lane") {
    // Civic Si ~220 km/h (137 mph); Jesko Attack with its big wing ~400+ (estimate: it's
    // drag-limited on 1,400 kg of downforce, ~460 on its 0.75 m^2; the low-drag Absolut is the 480+ km/h one); the
    // Bronco and the 4Runner are governed: 161 km/h (100 mph) and 171 km/h (106 mph). The buggy is limited to 150 km/h (an estimate:
    // its tires' rating) and the side-by-side to 130 km/h (Can-Am limits the Turbo RR near 85 mph).
    // Tacoma at ~180 (112 mph, for its all-terrains).
    static const struct { double lo, hi; } expect[] = {{200, 245}, {370, 470}, {155, 164}, {164, 174}, {144, 152}, {124, 132}, {172, 186}, {135, 154}};  // (Blazer: ~90-95 mph)
    ctx.sim.world = World(MAP_FREEWAY);
    Car& c = ctx.add(spec, ctx.sim.world.spawn.x, kLaneZ, 0);
    ctx.sim.player = (int)(&c - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 120 / 3.6);
    double top = 0, worstLane = 0, lastGain = ctx.t, lastTop = 0, scrape = 0, trim = 0;
    const double x0 = c.frame().com.x;
    ctx.runUntil(240, [&] { return ctx.t - lastGain > 6; }, [&] {
        c.throttle = 1, c.brake = 0, c.handbrake = 0;
        holdLane(c, kLaneZ, trim);
        const double kmh = len(c.frame().vel) * 3.6;
        static thread_local int fr = 0;
        if (getenv("CZ_DEBUG") && ++fr % 10 == 0) {
            double lowest = 1e9, sq[2] = {0, 0}, hubY[2] = {0, 0};
            for (int k = 0; k < c.bodyNodes; k++) lowest = std::min(lowest, c.nodes[k].p.y);
            for (const Wheel& w : c.wheels) {
                const Beam& sp = c.beams[w.spring];
                sq[w.front ? 0 : 1] += 0.5 * (w.springRest - len(c.nodes[sp.b].p - c.nodes[sp.a].p));
                hubY[w.front ? 0 : 1] += 0.25 * (c.nodes[w.inner].p.y + c.nodes[w.outer].p.y);
            }
            const Frame f = c.frame();
            std::printf("%5.0f km/h g%d z %.2f yaw %+.3f steer %+.4f lowest %.3f pitch %.2f roll %.2f slip", kmh, c.gear, f.com.z, std::atan2(-f.fwd.z, f.fwd.x) * 180 / M_PI, c.steer, lowest,
                        std::asin(f.fwd.y) * 180 / M_PI, std::asin(f.left.y) * 180 / M_PI);
            for (const Wheel& w : c.wheels) std::printf(" %.2f/%.0f", w.slip, w.load);
            std::printf("  tc %.2f\n", c.tcCut);
            (void)sq; (void)hubY;
        }
        top = std::max(top, kmh);
        if (top > lastTop + 0.5) lastTop = top, lastGain = ctx.t;
        worstLane = std::max(worstLane, std::fabs(c.frame().com.z - kLaneZ));
        for (int k = 0; k < c.bodyNodes; k++) {
            if (kmh > 250 && c.nodes[k].scrape > scrape && getenv("CZ_DEBUG"))
                std::printf("scrape node %d rest (%.2f %.2f %.2f) at %.0f km/h, %.3f m up\n", k, c.restLocal[k].x, c.restLocal[k].y, c.restLocal[k].z, kmh,
                            c.nodes[k].p.y - ctx.sim.world.height(c.nodes[k].p.x, c.nodes[k].p.z));
            scrape = std::max(scrape, (double)c.nodes[k].scrape * (kmh > 250 ? 1 : 0));
        }
        ctx.sample("Speed", spec.name, kmh, "km/h");
    });
    const double km = (c.frame().com.x - ctx.sim.wrapShift - x0) / 1000;
    std::printf("SCRAPE %.1f\n", scrape);
    std::printf("TOP %s %.0f km/h after %.1f km, %.0f s, gear %d, rpm %.0f\n", spec.name, top, km, ctx.t, c.gear, c.rpm);
    const int i = (int)(&spec - &vehicleCatalog()[0]);
    ctx.metric("top speed", top, "km/h", expect[i].lo, expect[i].hi);
    ctx.metric("distance driven", km, "km", 0.15, 40);
    ctx.metric("worst drift from the lane center", worstLane, "m", 0, spec.openFrame && spec.wheelRadius > 0.45 ? 2.6 : 1.2);  // (the buggy: soft 40s, solid axles)
    ctx.check("no damage", c.damage < 0.002);
    ctx.metric("anything scraping the road over 250 km/h (sliding speed)", scrape, "m/s", 0, 0);
}

SCENARIO(freewayWrap, "freeway/wrap", "Two Jeskos at 200 km/h through the 400 m wraps: speed and lane carry on as if nothing happened, the one behind stays the same distance back") {
    ctx.sim.world = World(MAP_FREEWAY);
    Car& lead = ctx.add(vehicleCatalog()[1], -150, kLaneZ, 0);
    Car& tail = ctx.add(vehicleCatalog()[1], -190, kLaneZ, 0);
    ctx.sim.player = 0;
    ctx.focus(lead);
    ctx.run(1.0);
    ctx.launch(lead, 200 / 3.6);
    ctx.launch(tail, 200 / 3.6);
    int wraps = 0;
    double lastShift = 0, worstJump = 0, gap0 = 0, worstGap = 0, trim[2] = {0, 0};
    V3 prev = lead.frame().vel;
    ctx.run(30, [&] {
        for (int k = 0; k < 2; k++) {
            Car& c = ctx.sim.cars[k];
            c.throttle = clampd(0.3 + (200 / 3.6 - len(c.frame().vel)) * 0.3, 0, 1), c.brake = 0, c.handbrake = 0;
            holdLane(c, kLaneZ, trim[k]);
        }
        const V3 v = lead.frame().vel;
        worstJump = std::max(worstJump, len(v - prev));  // per frame: a wrap must not jolt it
        prev = v;
        if (ctx.sim.wrapShift != lastShift) wraps++, lastShift = ctx.sim.wrapShift;
        const double gap = lead.frame().com.x - ctx.sim.cars[1].frame().com.x;
        if (gap0 == 0) gap0 = gap;
        worstGap = std::max(worstGap, std::fabs(gap - gap0));
    });
    ctx.metric("wraps (30 s at 200 km/h is 1.7 km)", wraps, "", 3, 6);
    ctx.metric("biggest change in velocity in one frame", worstJump, "m/s", 0, 0.5);
    ctx.metric("trailing truck's gap changed by", worstGap, "m", 0, 15);
    ctx.check("both still in their lane", std::fabs(lead.frame().com.z - kLaneZ) < 1.5 && std::fabs(ctx.sim.cars[1].frame().com.z - kLaneZ) < 1.5);
}
