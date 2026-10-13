#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        for (const Panel& p : c.panels) {
            double m = 0; for (int n : p.panelNodes) m += c.nodes[n].m;
            printf("%s %-16s %5.1f kg  nodes %zu\n", vehicleCatalog()[v].name, p.name, m, p.panelNodes.size());
        }
    }
}
