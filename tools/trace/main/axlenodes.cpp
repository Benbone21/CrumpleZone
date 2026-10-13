#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    for (const Axle& a : c.axles) {
        if (a.housing.empty()) continue;
        for (int n : a.housing) {
            int nb = 0, nk = 0; double K = 0;
            for (const Beam& b : c.beams) if (b.a == n || b.b == n) { nb++; if (b.type == BEAM_KNUCKLE) nk++; K += b.k; }
            printf("node %d local (%.2f %.2f %.2f) m %.1f  beams %d (knuckle %d) sumK %.2e\n", n, c.restLocal[n].x, c.restLocal[n].y, c.restLocal[n].z, c.nodes[n].m, nb, nk, K);
        }
    }
}
