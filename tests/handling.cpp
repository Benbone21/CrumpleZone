// Handling at speed with keyboard steering: what a player gets holding W and tapping A.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Holds the throttle at `kmh` on the pad's long straight, then holds left for `hold`
// seconds and lets go. Returns the worst body sideslip (deg) and peak lateral g.
void keyboardTurn(Ctx& ctx, const VehicleSpec& spec, double kmh, double hold, double& slip, double& latG, double& yawRate) {
    Car& c = ctx.add(spec, -125, -50, 0);
    ctx.sim.player = (int)(&c - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, kmh / 3.6);
    slip = latG = yawRate = 0;
    V3 prevVel = c.frame().vel, prevFwd = c.frame().fwd;
    const double t0 = ctx.t;
    double smooth = 0;
    ctx.run(3.0, [&] {
        const double t = ctx.t - t0;
        const Frame f = c.frame();
        // Hold the speed (W on and off, as a player does), steer left for `hold` s.
        const bool up = dot(f.vel, f.fwd) < kmh / 3.6;
        ctx.sim.drive(up, false, t > 0.3 && t < 0.3 + hold, false, false, Ctx::kFrame);
        const V3 acc = (f.vel - prevVel) / Ctx::kFrame;
        prevVel = f.vel;
        const V3 side = norm(cross(V3(0, 1, 0), norm(f.vel + V3(1e-9, 0, 0))));
        smooth += (std::fabs(dot(acc, side)) / 9.81 - smooth) * 0.1;
        latG = std::max(latG, smooth);
        slip = std::max(slip, std::fabs(std::atan2(dot(f.vel, f.left), std::max(0.1, dot(f.vel, f.fwd)))) * 180 / M_PI);
        yawRate = std::max(yawRate, std::acos(clampd(dot(norm(f.fwd), norm(prevFwd)), -1, 1)) / Ctx::kFrame * 180 / M_PI);
        prevFwd = f.fwd;
        ctx.sample("Sideslip", spec.name, std::atan2(dot(f.vel, f.left), std::max(0.1, dot(f.vel, f.fwd))) * 180 / M_PI, "deg");
        ctx.sample("Steer", spec.name, c.steer * 180 / M_PI, "deg");
    });
}

}  // namespace

VEHICLE_SCENARIO(keyTapFast, "handling/key-tap-130", "130 km/h (80 mph), W held, A tapped for 0.25 s: a lane-change sized nudge, no slide") {
    double slip, g, yaw;
    keyboardTurn(ctx, spec, 130, 0.25, slip, g, yaw);
    std::printf("TAP %s slip %.1f deg, %.2f g, yaw %.0f deg/s\n", spec.name, slip, g, yaw);
    ctx.metric("worst body sideslip", slip, "deg", 0, 2.5);
    ctx.metric("peak lateral acceleration", g, "g", 0.05, 1.0);
}

VEHICLE_SCENARIO(keyHoldFast, "handling/key-hold-130", "130 km/h (80 mph), W held, A held for 1.5 s: a hard sweeper at the grip limit, no spin") {
    double slip, g, yaw;
    keyboardTurn(ctx, spec, 130, 1.5, slip, g, yaw);
    std::printf("HOLD %s slip %.1f deg, %.2f g, yaw %.0f deg/s\n", spec.name, slip, g, yaw);
    ctx.metric("worst body sideslip (street tires slip ~9 deg at their peak)", slip, "deg", 0, 11);
    ctx.metric("peak lateral acceleration", g, "g", 0.5, 1.6);
}
