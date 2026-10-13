// Trace: full throttle from rest for vehicle argv[1].
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    double secs = argc > 2 ? atof(argv[2]) : 8;
    Sim sim;
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    ctx.run(1.0);
    std::printf("mode %d low %d gear %d mass %.0f\n", c.driveMode, c.lowRange, c.gear, c.mass);
    int fr = 0;
    ctx.run(secs, [&] {
        c.throttle = 1;
        c.brake = 0;
        if (++fr % 15) return;
        Frame f = c.frame();
        std::printf("t %.2f v %.1f gear %d rpm %.0f tc %.2f up %.3f fwd.y %.3f y %.2f hd %.1f z %.2f|", ctx.t, speedKmh(c), c.gear, c.rpm, c.tcCut, f.up.y, f.fwd.y, f.com.y, headingDeg(c), f.com.z);
        for (const Wheel& w : c.wheels)
            std::printf(" w%.1f L%.0f d%.0f tcb%.2f|", w.omega, w.load, w.driveActual, w.tcBrake);
        std::printf("\n");
    });
}
