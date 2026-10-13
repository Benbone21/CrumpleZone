#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 1;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    for (int s = 0; s < 5; s++) {
        ctx.run(1.0);
        int best = 0; double bv = 0;
        for (size_t i = 0; i < c.nodes.size(); i++) { double sp = len(c.nodes[i].v); if (sp > bv) bv = sp, best = (int)i; }
        printf("t %.0f fastest node %d (%.2f %.2f %.2f) %s m %.2f v %.2f m/s\n", ctx.t, best, c.restLocal[best].x, c.restLocal[best].y, c.restLocal[best].z,
               best < c.bodyNodes ? "body" : best < c.frameNodes ? "frame" : "other", c.nodes[best].m, bv);
    }
}
