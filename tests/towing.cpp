// Towing and payload dynamics tests.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

// Trailer towing test.
SCENARIO(trailer_towing, "towing/trailer",
         "Vehicle tows a trailer and maintains stability") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Add a trailer (heavy box behind the vehicle).
    V3 trailerPos = c.frame().com;
    trailerPos.x -= 3.0;  // 3 meters behind
    ctx.sim.world.addBox(trailerPos, V3(1.5, 0.8, 1.2), 0, 0, 0, V3(0.5, 0.5, 0.5));

    // Accelerate slowly with the trailer.
    ctx.run(15.0, [&] {
        c.throttle = 0.3;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("vehicle moves", c.frame().com.x > 10);
    ctx.check("stays stable", c.frame().up.y > 0.85);
    ctx.metric("damage", c.damage * 100, "%", 0, 20);
}

// Payload distribution test.
SCENARIO(payload_distribution, "towing/payload",
         "Payload distribution affects vehicle handling") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Add payload to the bed.
    V3 payloadPos = c.frame().com;
    payloadPos.x -= 1.0;  // Behind the cab
    payloadPos.y += 0.5;
    ctx.sim.world.addBox(payloadPos, V3(0.8, 0.5, 0.6), 0, 0, 0, V3(0.5, 0.5, 0.5));

    // Accelerate with the payload.
    ctx.run(10.0, [&] {
        c.throttle = 0.5;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("vehicle moves", c.frame().com.x > 10);
    ctx.check("stays stable", c.frame().up.y > 0.85);
}

// Heavy towing on incline.
SCENARIO(heavy_towing_incline, "towing/incline",
         "Vehicle tows heavy load on incline") {
    const VehicleSpec& s = vehicleCatalog()[2];  // Bronco
    Car& c = ctx.add(s, 0, 0, 0);
    ctx.focus(c);
    ctx.run(0.5);

    // Add a heavy trailer.
    V3 trailerPos = c.frame().com;
    trailerPos.x -= 3.0;
    ctx.sim.world.addBox(trailerPos, V3(1.5, 0.8, 1.2), 0, 0, 0, V3(0.5, 0.5, 0.5));

    // Drive over rough terrain with the trailer.
    ctx.run(10.0, [&] {
        c.throttle = 0.4;
        c.brake = 0;
        c.handbrake = 0;
        c.steer = 0;
    });

    ctx.check("vehicle moves", c.frame().com.x > 5);
    ctx.check("stays stable", c.frame().up.y > 0.8);
}

}  // namespace