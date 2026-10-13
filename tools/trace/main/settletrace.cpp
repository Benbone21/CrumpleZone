#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int veh = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[veh], 0, -50, 0);
    int n = 0;
    ctx.run(5, [&] {
        if (n++ % 15) return;
        double vmax = 0; int imax = -1;
        for (size_t i = 0; i < c.nodes.size(); i++) { double v = len(c.nodes[i].v); if (v > vmax) vmax = v, imax = i; }
        printf("t=%.2f vmax %.3f node %d (body %d frame %d) rpm %.0f g %d |", ctx.t, vmax, imax, c.bodyNodes, c.frameNodes, c.rpm, c.gear);
        for (auto& w : c.wheels) printf(" w%+.2f s%+.2f T%+5.0f", w.omega, w.sideOmega, w.shaftTorque);
        printf("\n");
    });
}
