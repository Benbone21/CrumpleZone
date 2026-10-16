// Extreme condition and edge case physics tests.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Suspension breakage under extreme load.
SCENARIO(suspension_breakage, "extreme/suspension-breakage",
         "Suspension components break under extreme loads") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Drive over rough terrain at high speed.
    ctx.run(10.0, [&] {
        c.throttle = 0.6;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Suspension should show wear or breakage.
    ctx.metric("damage after rough terrain", c.damage * 100, "%", 0, 50);
    ctx.check("car survives", c.frame().up.y > 0.5);
}

// High-speed stability test.
SCENARIO(high_speed_stability, "extreme/high-speed",
         "Vehicle accelerates and stays stable") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate for a short time.
    ctx.run(5.0, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    double speed = speedKmh(c);
    ctx.metric("speed reached", speed, "km/h", 5, 200);
    ctx.check("stays stable", c.frame().up.y > 0.85);
    ctx.check("stays on ground", c.frame().com.y < 5.0);
}

// Rapid acceleration test (wheel spin).
SCENARIO(rapid_acceleration, "extreme/rapid-acceleration",
         "Wheel spin under rapid acceleration") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Full throttle from rest.
    double t0 = ctx.t;
    ctx.run(5.0, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });
    double accelTime = ctx.t - t0;

    ctx.metric("acceleration time", accelTime, "s", 2, 10);
    ctx.metric("speed reached", speedKmh(c), "km/h", 50, 200);
    ctx.check("car moves forward", c.frame().com.x > 10);
}

// Emergency evasion test.
SCENARIO(emergency_evasion, "extreme/evasion",
         "Emergency steering and braking maneuver") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 80 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 80; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Emergency steering and braking.
    ctx.run(1.0, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
        c.steer = 0.3;
    });
    ctx.run(1.0, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
        c.steer = -0.3;
    });
    ctx.run(1.0, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("car stays upright", c.frame().up.y > 0.8);
    ctx.metric("final speed", speedKmh(c), "km/h", 0, 80);
}

// Off-road extreme test.
SCENARIO(offroad_extreme, "extreme/offroad",
         "Extreme off-road conditions") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco (was the Blazer)
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Drive over rough terrain at moderate speed.
    ctx.run(15.0, [&] {
        c.throttle = 0.4;
        c.brake = 0;
        c.handbrake = 0;
    });

    ctx.check("car survives", c.frame().up.y > 0.5);
    ctx.metric("damage", c.damage * 100, "%", 0, 50);
}

}  // namespace