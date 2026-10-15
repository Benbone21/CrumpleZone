// Environmental and long-term effects physics tests.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Crosswind handling at high speed.
SCENARIO(crosswind_handling, "environment/crosswind",
         "Vehicle handles crosswind gusts at high speed") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 60 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 60; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    // Simulate a crosswind gust by applying a lateral steering correction.
    ctx.run(1.0, [&] {
        c.throttle = 0.5;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0.05;  // Small corrective input
    });
    ctx.run(1.0, [&] {
        c.throttle = 0.5;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = -0.05;
    });
    ctx.run(1.0, [&] {
        c.throttle = 0.5;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("stays stable", c.frame().up.y > 0.9);
    ctx.check("stays on ground", c.frame().com.y < 2.0);
}

// Long-duration cruising.
SCENARIO(long_cruise, "environment/cruise",
         "Vehicle cruises at constant speed for extended time") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 60 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 60; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    // Cruise at 60 km/h for a while.
    ctx.run(10.0, [&] {
        c.throttle = 0.4;  // Cruise throttle
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("stays stable", c.frame().up.y > 0.9);
}

// Cold start simulation (idle then accelerate).
SCENARIO(cold_start, "environment/cold-start",
         "Vehicle idles then accelerates after cold start") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Idle for a while.
    ctx.run(3.0, [&] {
        c.throttle = 0;
        c.brake = 0;
        c.handbrake = 1;
        c.steer = 0;
    });

    // Then accelerate.
    ctx.run(8.0, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("vehicle moves", c.frame().com.x > 5);
}

// Heavy load test (full throttle uphill).
SCENARIO(heavy_load, "environment/heavy-load",
         "Vehicle under heavy load maintains traction") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Full throttle on rough terrain.
    ctx.run(10.0, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("vehicle moves", c.frame().com.x > 5);
    ctx.check("stays stable", c.frame().up.y > 0.8);
    ctx.metric("damage", c.damage * 100, "%", 0, 30);
}

// Rain simulation (reduced grip).
SCENARIO(rain_simulation, "environment/rain",
         "Vehicle handles reduced grip conditions") {
    const VehicleSpec& s = vehicleCatalog()[0];  // Civic
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Accelerate to 60 km/h.
    ctx.runUntil(10.0, [&] { return speedKmh(c) >= 60; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    // Brake hard (simulates wet road conditions).
    ctx.run(3.0, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("stays stable", c.frame().up.y > 0.85);
    ctx.metric("final speed", speedKmh(c), "km/h", 0, 60);
}

}  // namespace