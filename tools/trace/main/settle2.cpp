// dynamics/settle: the fastest node each 0.1 s of the last 2 s, and the body's motion.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    if (argc > 2) c.setDriveMode(atoi(argv[2]));
    ctx.run(3.0);
    for (int f = 0; f < (argc > 3 ? atoi(argv[3]) : 120); f++) {
        ctx.step();
        if (f % 6 == 0) {
            int best = 0; double bv = 0;
            for (size_t i = 0; i < c.nodes.size(); i++) { double s = len(c.nodes[i].v); if (s > bv) bv = s, best = (int)i; }
            Frame fr = c.frame();
            printf("t %.2f fastest %d (%.2f %.2f %.2f) %.4f m/s  com v %.4f  wheels w %.3f %.3f %.3f %.3f  rpm %.0f gear %d\n", ctx.t, best, c.restLocal[best].x, c.restLocal[best].y, c.restLocal[best].z, bv, len(fr.vel),
                   c.wheels[0].omega, c.wheels[1].omega, c.wheels[2].omega, c.wheels[3].omega, c.rpm, c.gear);
        }
    }
}
