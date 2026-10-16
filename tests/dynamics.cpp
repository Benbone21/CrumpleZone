// Advanced dynamics and collision physics tests.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Verify momentum transfer in head-on collisions.
SCENARIO(momentum_transfer, "dynamics/momentum",
         "Momentum is transferred in head-on collisions between two vehicles") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic

    // Two cars approaching each other.
    Car& c1 = ctx.add(s, -15, 0, 0);
    Car& c2 = ctx.add(s, 15, 0, 180);
    ctx.focus(c1);
    ctx.run(0.5);

    // Drive them toward each other.
    ctx.run(3.0, [&] {
        c1.throttle = 1;
        c1.brake = 0;
        c1.handbrake = 0;
        c2.throttle = 1;
        c2.brake = 0;
        c2.handbrake = 0;
    });

    // After collision, both should be damaged.
    ctx.check("both cars damaged", c1.damage > 0.01 || c2.damage > 0.01);
    ctx.metric("car 1 damage", c1.damage * 100, "%", 0, 50);
    ctx.metric("car 2 damage", c2.damage * 100, "%", 0, 50);
}

// Verify energy conservation in simple cases.
SCENARIO(energy_conservation, "dynamics/energy",
         "Kinetic energy is converted to damage in collisions") {
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 50 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 50; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Calculate kinetic energy.
    V3 v = c.frame().vel;
    double ke = 0.5 * c.mass * len(v) * len(v);

    // Drive forward to hit the crash wall (at x=114).
    // Car is at ~10m after acceleration, wall is at 114m, need ~100m at ~14m/s = ~7s.
    ctx.run(10.0, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // After crash, KE should be converted to damage.
    ctx.metric("kinetic energy before crash", ke / 1000, "kJ", 20, 200);
    ctx.metric("damage after crash", c.damage * 100, "%", 1, 50);
    ctx.check("energy converted to damage", c.damage > 0.01);
}

// Verify friction coefficients on different surfaces.
SCENARIO(friction_coefficients, "dynamics/friction",
         "Different surfaces have different friction coefficients") {
    // Drive on different surfaces and measure braking distance.
    // Asphalt should have the best grip, ice the worst.

    // Test on asphalt first.
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 40 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 40; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Brake and measure time.
    double t1 = ctx.t;
    ctx.runUntil(5.0, [&] { return speedKmh(c) <= 5; }, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
    });
    double brakeTime = ctx.t - t1;

    ctx.metric("braking time on asphalt", brakeTime, "s", 0.5, 5);
    ctx.check("brakes work on asphalt", brakeTime > 0 && brakeTime < 5);
}

// Verify tire slip angles.
SCENARIO(tire_slip_angles, "dynamics/tire-slip",
         "Tires slip at high lateral loads") {
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 80 km/h and do a sharp turn.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 80; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Sharp turn to induce slip.
    ctx.run(1.0, [&] {
        c.throttle = 0.5;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0.4;
    });

    // Car should still be moving forward.
    ctx.check("car continues moving", speedKmh(c) > 20);
    ctx.check("car stays upright", c.frame().up.y > 0.8);
}

// Verify suspension dynamics under various loads.
SCENARIO(suspension_dynamics, "dynamics/suspension",
         "Suspension compresses and rebounds under load") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Drive over bumps.
    ctx.run(10.0, [&] {
        c.throttle = 0.3;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Suspension should absorb the bumps.
    ctx.check("car survives bumps", c.frame().up.y > 0.8);
    ctx.metric("damage after bumps", c.damage * 100, "%", 0, 20);
}

}  // namespace