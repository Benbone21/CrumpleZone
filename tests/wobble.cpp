// High-speed wobble: a body knocked into a roll mid-corner has to settle, however grippy the
// tires. (On race slicks the Jesko rocked itself into a 3 Hz wobble at 80 mph that went on
// for seconds: the road's push went into the hub nodes alone and bent the knuckle, so the
// wheels toed in and out as the cornering load rocked from side to side.)
#include <cmath>
#include <cstdio>

#include "../src/mods.h"
#include "harness.h"

namespace {

// The Jesko with a given tread from the garage's tires slot (stock if it hasn't one).
const VehicleSpec& jeskoOn(int tread) {
    const int v = 1;
    for (int o = 0; o < (int)modOptions(v, MOD_TIRES).size(); o++) {
        ModSet ms;
        ms.pick[MOD_TIRES] = o;
        const VehicleSpec& s = tunedSpec(v, ms);
        if (s.tireType == tread) return s;
    }
    return vehicleCatalog()[v];
}

// Round a flat 110 m circle at 120 km/h (~1 g), then a sharp roll kick (~30 deg/s): the
// roll rate's swing (rms) over each half second after it.
std::vector<double> rollKick(Ctx& ctx, const VehicleSpec& spec) {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_FREEWAY);
    World& w = ctx.sim.world;
    for (double x = -400; x < 300; x += 1)
        for (double z = -500; z < -100; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellHeight(id, 0), w.setCellSurface(id, SURF_PAVED, PAINT_PAD);
        }
    w.buildGrid();
    const double R = 110, kmh = 120;
    Car& c = ctx.add(spec, -40, -400, 0);
    ctx.sim.player = 0;
    ctx.focus(c);
    ctx.run(1.0);
    const V3 center = c.frame().com + V3(0, 0, R);  // turning right
    ctx.launch(c, kmh / 3.6);
    double prevRoll = 0, t0 = ctx.t;
    std::vector<double> sum(6, 0);
    std::vector<int> n(6, 0);
    bool kicked = false;
    ctx.run(8.0, [&] {
        const Frame f = c.frame();
        V3 r = f.com - center;
        r.y = 0;
        const double look = std::max(6.0, 0.7 * len(f.vel)), ang = std::atan2(r.z, r.x) + look / R;
        const V3 aim = center + V3(std::cos(ang), 0, std::sin(ang)) * R - f.com;
        const double want = std::atan(2 * c.wheelbase * std::sin(std::atan2(dot(aim, f.left), dot(aim, f.fwd))) / look);
        c.steer += (clampd(want, -0.3, 0.3) - c.steer) * (Ctx::kFrame / 0.3);  // (a driver's hands: no quicker)
        c.throttle = clampd(0.3 + (kmh / 3.6 - len(f.vel)) * 0.4, 0, 1), c.brake = 0, c.handbrake = 0;
        const double roll = std::asin(f.left.y) * 180 / M_PI, rate = (roll - prevRoll) / Ctx::kFrame, tt = ctx.t - t0 - 5;
        prevRoll = roll;
        if (!kicked && tt >= 0) {
            for (Node& nd : c.nodes) nd.v.y += 0.5 * dot(nd.p - f.com, f.left);
            kicked = true;
        } else if (kicked && tt > 0.05 && tt < 3.05) {
            const int win = std::min(5, (int)((tt - 0.05) / 0.5));
            sum[win] += rate * rate, n[win]++;
        }
        ctx.sample("Roll rate", spec.name + std::string(spec.tireType == TIRE_RACE_SLICK ? " (slicks)" : ""), rate, "deg/s");
    });
    std::vector<double> rms(6);
    for (int i = 0; i < 6; i++) rms[i] = std::sqrt(sum[i] / std::max(1, n[i]));
    return rms;
}

}  // namespace

SCENARIO(rollSettle, "dynamics/roll-settle", "Jesko at 120 km/h round a 1 g bend, knocked into a roll: it settles within a second or so, on its own semi-slicks and on race slicks") {
    for (int tread : {(int)TIRE_SEMI_SLICK, (int)TIRE_RACE_SLICK}) {
        const VehicleSpec& spec = jeskoOn(tread);
        const std::vector<double> rms = rollKick(ctx, spec);
        const std::string on = tread == TIRE_RACE_SLICK ? "race slicks" : "semi-slicks";
        std::printf("%s on %s: roll rate swing after the kick (deg/s rms, each 0.5 s):", spec.name, on.c_str());
        for (double r : rms) std::printf(" %.2f", r);
        std::printf("\n");
        // (Before the fix: ~0.5 on slicks, still rocking at 1.25 deg/s 3 s later; ~0.05 on the semi-slicks.)
        ctx.metric(on + ": roll swing 1-1.5 s after the kick, against the first half second", rms[2] / std::max(1e-6, rms[0]), "x", 0, 0.2);
        ctx.metric(on + ": roll swing 2.5-3 s after it", rms[5], "deg/s", 0, 0.3);
    }
}
