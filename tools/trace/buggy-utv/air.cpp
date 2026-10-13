#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, -50, 0);
    ctx.run(1.0);
    if (argc > 2) c.setDriveMode(atoi(argv[2]));
    for (Node& n : c.nodes) { n.p.y += 20; n.v = V3(); }
    c.engineDead = true;
    c.rpm = 0;
    for (Wheel& w : c.wheels) { w.omega = 30; w.stickX = w.stickY = 0.01; w.groundSpeed = 50; w.abs = 0.1; }
    std::printf("mode %d center %d front %d\n", c.driveMode, (int)c.centerLocked(), (int)c.frontEngaged());
    for (int s = 0; s < 200; s++) {
        sim.substep();
        if (s % 10 == 0) {
            std::printf("s %d", s);
            for (const Wheel& w : c.wheels) std::printf(" | w %.3f sg %.3f tw %.5f xf %.1f", w.omega, w.sideOmega, w.twist, w.xferTorque);
            std::printf("\n");
        }
    }
}
