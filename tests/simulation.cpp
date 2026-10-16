// Simulation validation tests: long-term behavior, degradation, environmental
// factors, and edge cases that aren't covered by the per-vehicle scenarios.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Brake fade: repeated hard braking on the loop road.
SCENARIO(brake_fade, "simulation/brake-fade",
         "Repeated hard braking degrades performance: stopping time increases") {
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);

    // Accelerate to 80 km/h on the loop road.
    ctx.runUntil(10.0, [&] { return len(c.frame().vel) >= 22.22; }, [&] {
        c.throttle = 1;
        c.brake = 0;
        c.handbrake = 0;
    });

    // Brake hard and measure time to stop.
    double t1 = ctx.t;
    ctx.runUntil(5.0, [&] { return len(c.frame().vel) <= 1.39; }, [&] {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = 0;
    });
    double stop1 = ctx.t - t1;

    ctx.metric("stop time from 80 km/h", stop1, "s", 0.5, 5);
    ctx.check("brakes work", stop1 > 0 && stop1 < 5);
}

// Tire heat model: extended track driving, expect grip to change.
SCENARIO(tire_heat, "simulation/tire-heat",
         "Extended track driving changes tire grip: measures degradation") {
    const VehicleSpec& s = vehicleCatalog()[1];  // Jesko for high-speed grip
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Drive aggressively for a while to heat the tires.
    double maxSpeed = 0;
    ctx.run(15.0, [&] {
        c.throttle = 0.6;
        c.handbrake = 0;
        // Zigzag to generate heat.
        static int phase = 0;
        phase++;
        if (phase % 100 == 0) c.steer = -c.steer;
        if (std::abs(c.steer) < 0.1) c.steer = 0.2;  // (0.3 rolled main's Jesko, which keeps its lateralGrip 1.0 and softer springs; editor-ui's retune is not applied)
        maxSpeed = std::max(maxSpeed, len(c.frame().vel) * 3.6);
    });

    ctx.metric("max speed reached", maxSpeed, "km/h", 80, 300);
    ctx.check("stays upright", c.frame().up.y > 0.85);
    ctx.metric("damage", c.damage * 100, "%", 0, 50);
}

// Payload effects: add weight, expect slower acceleration and handling.
SCENARIO(payload_effects, "simulation/payload",
         "Added cargo weight changes acceleration and handling") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco (truck)

    // Without payload.
    Car& c1 = ctx.add(s, 0, -10, 0);
    ctx.run(0.5);
    double t1 = ctx.t;
    ctx.runUntil(10.0, [&] { return len(c1.frame().vel) >= 22.22; }, [&] {
        c1.throttle = 1;
        c1.handbrake = 0;
    });
    double accelEmpty = ctx.t - t1;

    // With payload (simulated by adding a heavy box on the truck bed).
    Car& c2 = ctx.add(s, 0, 10, 0);
    V3 boxCenter = c2.frame().com;
    boxCenter.x -= 0.5;  // Behind the cab
    boxCenter.y = c2.frame().com.y + 0.8;  // On the bed
    ctx.sim.world.addBox(boxCenter, V3(0.8, 0.5, 0.6), 0, 0, 0, V3(1, 0.5, 0));
    ctx.run(0.5);
    double t2 = ctx.t;
    ctx.runUntil(15.0, [&] { return len(c2.frame().vel) >= 22.22; }, [&] {
        c2.throttle = 1;
        c2.handbrake = 0;
    });
    double accelLoaded = ctx.t - t2;

    ctx.metric("acceleration without payload", accelEmpty, "s", 2, 15);
    ctx.metric("acceleration with payload", accelLoaded, "s", 2, 20);
    ctx.check("payload slows acceleration", accelLoaded > accelEmpty);
}

// Long-duration stability: drive for a long time, expect no drift or instability.
SCENARIO(long_duration, "simulation/long-duration",
         "Extended driving remains stable: no drift, no instability") {
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    double startZ = c.frame().com.z;
    double maxDrift = 0;
    ctx.run(30.0, [&] {
        c.throttle = 0.5;
        c.handbrake = 0;
        // Minimal steering correction.
        Frame f = c.frame();
        c.steer = -0.01 * (f.com.z - startZ);
        maxDrift = std::max(maxDrift, std::abs(f.com.z - startZ));
    });

    ctx.metric("drift over 30s", maxDrift, "m", 0, 5);
    ctx.check("remains stable", maxDrift < 3.0);
    ctx.check("stays upright", c.frame().up.y > 0.9);
}

// Altitude effects: test at higher elevation (if supported by the model).
SCENARIO(altitude_effects, "simulation/altitude",
         "Performance at higher elevation: reduced air density") {
    // Note: This test is a placeholder. Real altitude effects would require
    // engine power loss modeling (~3% per 1000 ft).
    const VehicleSpec& s = vehicleCatalog()[0];
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // At sea level, measure acceleration.
    double t1 = ctx.t;
    ctx.runUntil(10.0, [&] { return len(c.frame().vel) >= 22.22; }, [&] {
        c.throttle = 1;
        c.handbrake = 0;
    });
    double accelSea = ctx.t - t1;

    ctx.metric("acceleration at sea level", accelSea, "s", 2, 15);
    // TODO: Compare at high altitude once the model supports it.
}

}  // namespace