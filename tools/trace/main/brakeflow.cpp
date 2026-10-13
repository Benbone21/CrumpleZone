// Full brakes from 100 km/h: does the frame flow? toe of each wheel?
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    ctx.run(1.0);
    std::vector<V3> r0[2];
    for (int s = 0; s < 2; s++) r0[s] = c.railFrame[s].rest;
    ctx.launch(c, 100 / 3.6);
    int f = 0;
    ctx.runUntil(8.0, [&] { return speedKmh(c) < 0.5; }, [&] {
        c.throttle = 0; c.brake = 1; c.steer = 0;
        if (f++ % 15 == 0) {
            double worst = 0; V3 wl;
            for (int s = 0; s < 2; s++) for (size_t k = 0; k < r0[s].size(); k++) { double d = len(c.railFrame[s].rest[k] - r0[s][k]); if (d > worst) worst = d, wl = c.restLocal[c.railFrame[s].nodes[k]]; }
            Frame fr = c.frame();
            printf("t %.2f v %5.1f head %5.2f z %6.2f  max flow %.1f mm at (%.2f %.2f %.2f) dmg %.3f%%\n", ctx.t, speedKmh(c), headingDeg(c), fr.com.z + 50, worst * 1000, wl.x, wl.y, wl.z, c.damage * 100);
        }
    });
}
