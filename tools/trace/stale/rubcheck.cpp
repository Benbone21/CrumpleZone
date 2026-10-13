#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], 0, -50, 0);
        ctx.run(1.5);
        printf("%-16s tris:", vehicleCatalog()[v].name);
        for (auto& w : c.wheels) printf(" %zu", w.rubTris.size());
        printf("  rest rub:");
        for (auto& w : c.wheels) printf(" %.0f", w.rub);
        // full-lock circle at walking pace, then a hard bump: max rub
        double mx[4] = {0, 0, 0, 0};
        ctx.run(6, [&] { c.throttle = 0.25; c.steer = (ctx.t < 4.5 ? 0.6 : -0.6); for (int i = 0; i < 4; i++) mx[i] = std::max(mx[i], (double)c.wheels[i].rub); });
        printf("  full-lock max rub:");
        for (double m : mx) printf(" %.0f", m);
        printf("\n");
    }
}
