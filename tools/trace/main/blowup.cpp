#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    for (int step = 0; step < 10000; step++) {
        sim.substep();
        int worst = -1; double vmax = 0;
        for (size_t i = 0; i < c.nodes.size(); i++) { double s = len(c.nodes[i].v); if (s > vmax) vmax = s, worst = (int)i; }
        if (step % 500 == 0) {
            int cnt[12] = {0}; double kmax = 0;
            for (const Beam& b : c.beams) if (b.a == worst || b.b == worst) { cnt[b.type]++; kmax = std::max(kmax, b.k); }
            printf("step %d vmax %.2f node %d m %.2f collide %d local(%.2f %.2f %.2f) kmax %.2e:", step, vmax, worst, c.nodes[worst].m, (int)c.nodes[worst].collide, c.restLocal[worst].x, c.restLocal[worst].y, c.restLocal[worst].z, kmax);
            for (int t = 0; t < 12; t++) if (cnt[t]) printf(" %s%d", tn[t], cnt[t]);
            printf("\n");
            if (vmax > 50) break;
        }
    }
}
