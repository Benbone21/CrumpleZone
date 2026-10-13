// Full throttle from rest on the proving ground pad; prints the drivetrain each 0.25 s.
#define private public
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int vi = argc > 1 ? std::atoi(argv[1]) : 4;
    Sim sim;
    sim.world = World(MAP_PROVING_GROUND);
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, -50, 0);
    ctx.run(1.0);
    int fr = 0;
    ctx.run(6.0, [&] {
        c.throttle = 1, c.brake = 0, c.handbrake = 0;
        if (++fr % 15) return;
        std::printf("t %.2f v %.1f gear %d rpm %.0f tc %.2f esc %.2f sleep %d fuel %.1f dead %d hyd %d drag %.2f brakeP %.2f |", ctx.t, speedKmh(c), c.gear, c.rpm, c.tcCut, c.escLevel,
                    c.sleeping, c.fuel, c.engineDead, c.hydrolocked, c.dragAssist, c.brakePressure);
        for (const Wheel& w : c.wheels)
            std::printf(" [%d om %.1f side %.1f tq %.0f ld %.0f abs %.2f tcb %.2f escb %.2f br %d]", w.driven, w.omega, w.sideOmega, w.shaftTorque, w.load, w.abs, w.tcBrake, w.escBrake, w.shaftBroken);
        std::printf("\n");
    });
}
