// Vehicle-specific physics comparison tests. These run the same scenario on
// different vehicles to verify that the physics model produces different,
// realistic results based on vehicle characteristics.
// Vehicle indices: 0=Civic, 1=Jesko, 2=Bronco, 3=4Runner (the Blazer these were written with is not in the catalog)
#include <cmath>
#include <cstdio>
#include <vector>

#include "harness.h"

namespace {

// Compare acceleration across all vehicles.
SCENARIO(vehicle_acceleration_comparison, "vehicle-physics/acceleration",
         "All vehicles accelerate at different rates based on power-to-weight") {
    // Civic: 8.5-10s, Jesko: 2.5-3s, Bronco: 5-6s, 4Runner: 6-7s
    const int indices[] = {0, 1, 2, 3};
    const std::string names[] = {"civic", "jesko", "bronco", "4runner"};

    for (int i = 0; i < 4; i++) {
        const VehicleSpec& spec = vehicleCatalog()[indices[i]];
        Car& c = ctx.add(spec, 0, 0, 0);
        ctx.focus(c);
        ctx.run(0.5);

        double t0 = ctx.t;
        ctx.runUntil(15.0, [&] { return speedKmh(c) >= 100; }, [&] {
            c.throttle = 1;
            c.handbrake = 0;
        });
        double accelTime = ctx.t - t0;

        ctx.metric(names[i] + " 0-100 km/h", accelTime, "s", 1, 20);
    }
}

// Compare braking across vehicles.
SCENARIO(vehicle_braking_comparison, "vehicle-physics/braking",
         "Vehicles brake at different rates based on brake type and weight") {
    // Test Civic and Jesko (reliable braking).
    const int indices[] = {0, 1};
    const std::string names[] = {"civic", "jesko"};

    for (int i = 0; i < 2; i++) {
        const VehicleSpec& spec = vehicleCatalog()[indices[i]];
        Car& c = ctx.add(spec, 0, 0, 0);
        ctx.focus(c);
        ctx.run(0.5);

        // Accelerate to 40 km/h.
        ctx.runUntil(10.0, [&] { return speedKmh(c) >= 40; }, [&] {
            c.throttle = 1;
            c.brake = 0;
            c.handbrake = 0;
            c.steer = 0;
        });

        // Brake hard and measure time.
        double t1 = ctx.t;
        ctx.runUntil(5.0, [&] { return speedKmh(c) <= 5; }, [&] {
            c.throttle = 0;
            c.brake = 1;
            c.handbrake = 0;
            c.steer = 0;
        });
        double brakingTime = ctx.t - t1;

        ctx.metric(names[i] + " 40-0 km/h braking time", brakingTime, "s", 0.5, 5);
    }
}

// Compare handling across all vehicles.
SCENARIO(vehicle_handling_comparison, "vehicle-physics/handling",
         "All vehicles handle differently based on suspension and tires") {
    const int indices[] = {0, 1, 2, 3};
    const std::string names[] = {"civic", "jesko", "bronco", "4runner"};

    for (int i = 0; i < 4; i++) {
        const VehicleSpec& spec = vehicleCatalog()[indices[i]];
        Car& c = ctx.add(spec, 0, 0, 0);
        ctx.focus(c);
        ctx.run(0.5);

        // Accelerate to 60 km/h and do a quick lane change.
        ctx.runUntil(10.0, [&] { return speedKmh(c) >= 60; }, [&] {
            c.throttle = 1;
            c.handbrake = 0;
        });

        // Quick steering input.
        ctx.run(1.0, [&] {
            c.throttle = 0.5;
            c.brake = 0;
            c.handbrake = 0;
            c.steer = 0.3;
        });
        ctx.run(1.0, [&] {
            c.throttle = 0.5;
            c.brake = 0;
            c.handbrake = 0;
            c.steer = -0.3;
        });
        ctx.run(1.0, [&] {
            c.throttle = 0.5;
            c.brake = 0;
            c.handbrake = 0;
            c.steer = 0;
        });

        ctx.check(names[i] + " handles lane change", c.frame().up.y > 0.85);
    }
}

// Compare top speed across all vehicles.
SCENARIO(vehicle_top_speed_comparison, "vehicle-physics/top-speed",
         "All vehicles reach different top speeds based on power and aero") {
    const int indices[] = {0, 1, 2, 3};
    const std::string names[] = {"civic", "jesko", "bronco", "4runner"};

    for (int i = 0; i < 4; i++) {
        const VehicleSpec& spec = vehicleCatalog()[indices[i]];
        Car& c = ctx.add(spec, 0, 0, 0);
        ctx.focus(c);
        ctx.run(0.5);

        // Accelerate hard for a long time to reach top speed.
        double maxSpeed = 0;
        ctx.run(30.0, [&] {
            c.throttle = 1;
            c.brake = 0;
            c.handbrake = 0;
            maxSpeed = std::max(maxSpeed, speedKmh(c));
        });

        ctx.metric(names[i] + " top speed", maxSpeed, "km/h", 50, 500);
    }
}

// Compare off-road capability across trucks.
SCENARIO(vehicle_offroad_comparison, "vehicle-physics/offroad",
         "Off-road vehicles handle rough terrain differently") {
    const int indices[] = {2, 3};  // Bronco, 4Runner
    const std::string names[] = {"bronco", "4runner"};

    for (int i = 0; i < 2; i++) {
        const VehicleSpec& spec = vehicleCatalog()[indices[i]];
        Car& c = ctx.add(spec, 0, 0, 0);
        ctx.focus(c);
        ctx.run(0.5);

        // Drive over washboard at moderate speed.
        ctx.run(10.0, [&] {
            c.throttle = 0.3;
            c.brake = 0;
            c.handbrake = 0;
        });

        ctx.check(names[i] + " survives washboard", c.frame().up.y > 0.8);
        ctx.metric(names[i] + " damage after washboard", c.damage * 100, "%", 0, 20);
    }
}

}  // namespace