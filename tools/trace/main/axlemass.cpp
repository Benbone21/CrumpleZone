#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    for (int v = 2; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        for (const Axle& a : c.axles) {
            if (a.housing.empty()) continue;
            double m = 0; for (int n : a.housing) m += c.nodes[n].m;
            int k = (int)a.housing.size();
            printf("%s %s axle: housing mass %.1f kg (spec axleMass %.1f); pump %.1f tubes %.1f %.1f\n", vehicleCatalog()[v].name, a.front ? "front" : "rear", m, vehicleCatalog()[v].axleMass,
                   c.nodes[a.housing[k-3]].m, c.nodes[a.housing[k-2]].m, c.nodes[a.housing[k-1]].m);
        }
    }
}
