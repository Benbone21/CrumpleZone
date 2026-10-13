// Freeway flat-out trace: argv[1] vehicle, argv[2] seconds, argv[3] drive mode (optional)
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    double secs = argc > 2 ? atof(argv[2]) : 20;
    Sim sim; Ctx ctx(sim);
    sim.world = World(MAP_FREEWAY);
    Car& c = ctx.add(vehicleCatalog()[vi], sim.world.spawn.x, 6.9, 0);
    sim.player = 0;
    ctx.run(1.0);
    if (argc > 3) c.setDriveMode(atoi(argv[3]));
    ctx.launch(c, 120 / 3.6);
    int fr = 0;
    double steerI = 0;
    ctx.run(secs, [&] {
        c.throttle = 1; c.brake = 0;
        Frame f = c.frame();
        double yaw = std::atan2(-f.fwd.z, f.fwd.x);
        {
            const double v = std::max(5.0, len(f.vel));
            const double e = f.com.z - 6.9;
            steerI = clampd(steerI + 0.08 * e * (1.0 / 60), -1.0, 1.0);
            const double pull = clampd(0.25 * e + steerI + 2.5 * f.vel.z, -3.0, 3.0);
            c.steer = clampd(c.wheelbase * pull / (v * v), -0.1, 0.1);
        }
        if (++fr % 30) return;
        std::printf("t %.1f v %.1f g %d rpm %.0f tc %.2f yaw %.2f z %.2f st %.3f |", ctx.t, speedKmh(c), c.gear, c.rpm, c.tcCut, yaw * 57.3, f.com.z, c.steer);
        for (const Wheel& w : c.wheels) std::printf(" sx %.3f L %.0f d %.0f rub %.0f br %.0f tcb %.2f esc %.2f abs %.2f|", w.sx, w.load, w.driveActual, w.rubTorque, w.brakeReaction, w.tcBrake, w.escBrake, w.abs);
        std::printf("\n");
    });
}
