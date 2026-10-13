#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = atoi(argv[1]);
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    for (int a = 2; a < argc; a++) {
        int n = atoi(argv[a]);
        int cnt[12] = {0};
        for (const Beam& b : c.beams) if (b.a == n || b.b == n) cnt[b.type]++;
        printf("node %d (%.2f %.2f %.2f) m %.1f:", n, c.restLocal[n].x, c.restLocal[n].y, c.restLocal[n].z, c.nodes[n].m);
        for (int t = 0; t < 12; t++) if (cnt[t]) printf(" %s x%d", tn[t], cnt[t]);
        printf("\n");
    }
    printf("wheel 0 hub (%.2f %.2f %.2f) R %.3f\n", c.restLocal[c.wheels[0].inner].x, c.restLocal[c.wheels[0].inner].y, c.restLocal[c.wheels[0].inner].z, c.wheels[0].R);
}
