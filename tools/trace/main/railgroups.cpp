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
    for (int s = 0; s < 2; s++) {
        ChassisFrame& cf = c.railFrame[s];
        double M = 0, Mw = 0; int nb = 0;
        for (size_t k = 0; k < cf.nodes.size(); k++) { M += c.nodes[cf.nodes[k]].m; Mw += c.nodes[cf.nodes[k]].m * cf.weight[k]; }
        printf("rail %d: %zu members, mass %.1f, held %.1f\n", s, cf.nodes.size(), M, Mw);
        for (size_t k = 0; k < cf.nodes.size(); k++) { V3 l = c.restLocal[cf.nodes[k]]; if (std::fabs(l.z) < 0.25) printf("   near centre: (%.2f %.2f %.2f) m %.1f w %.0f\n", l.x, l.y, l.z, c.nodes[cf.nodes[k]].m, cf.weight[k]); nb++; }
    }
}
