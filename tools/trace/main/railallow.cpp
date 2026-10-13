#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <set>
int main(int argc, char** argv) {
    for (int v = 2; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        std::set<int> lad(c.ladder.begin(), c.ladder.end());
        for (size_t wi = 0; wi < c.wheels.size(); wi++) {
            const Wheel& w = c.wheels[wi];
            int nr = 0; double amax = 0, asum = 0; int ns = 0;
            for (size_t ri = 0; ri < w.rubFaces.size(); ri++) {
                if (!lad.count(w.rubFaces[ri][0])) continue;
                nr++;
                for (int s = 0; s < 7; s++) { double a = w.rubAllow[ri * 7 + s]; amax = std::max(amax, a); if (a > 0) asum += a, ns++; }
            }
            printf("%s wheel %zu (%s %s): rail faces %d, allowance max %.3f, mean of nonzero %.3f (%d samples)\n", vehicleCatalog()[v].name, wi, w.front ? "front" : "rear", w.left ? "L" : "R", nr, amax, ns ? asum / ns : 0, ns);
        }
    }
}
