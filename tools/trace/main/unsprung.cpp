#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <set>
int main() {
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        const VehicleSpec& vs = vehicleCatalog()[v];
        Car& c = ctx.add(vs, -100, -50, 0);
        printf("%-18s mass %.0f  wheelMass? hubMass %.1f axleMass %.1f\n", vs.name, c.mass, vs.hubMass, vs.axleMass);
        std::set<int> seen;
        for (size_t i = 0; i < c.wheels.size(); i++) {
            const Wheel& w = c.wheels[i];
            double m = 0; int cnt = 0;
            std::set<int> s(w.assembly.begin(), w.assembly.end()); s.insert(w.inner); s.insert(w.outer);
            for (int n : s) m += c.nodes[n].m, cnt++;
            printf("   wheel %zu %s%s assembly %d nodes %.1f kg (inner %.1f outer %.1f)\n", i, w.front ? "F" : "R", w.left ? "L" : "R", cnt, m, c.nodes[w.inner].m, c.nodes[w.outer].m);
        }
    }
}
