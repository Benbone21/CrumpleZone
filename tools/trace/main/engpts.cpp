#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    for (int v = 2; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        ctx.run(2.0, [&] { c.handbrake = 1; });
        Frame fr = c.frame();
        double g = ctx.sim.world.height(fr.com.x, fr.com.z);
        printf("%s engine/gearbox nodes (x fwd of com, height):", vehicleCatalog()[v].name);
        for (int n : c.engineNodes) printf(" (%.2f %.2f)", dot(c.nodes[n].p - fr.com, fr.fwd), c.nodes[n].p.y - g);
        double xf = -1e9, xr = 1e9;
        for (const Wheel& w : c.wheels) { double x = dot(c.nodes[w.inner].p - fr.com, fr.fwd); if (w.front) xf = std::max(xf, x); else xr = std::min(xr, x); }
        printf("\n  axles at x %.2f / %.2f; rail bottoms:", xf, xr);
        for (int st = 0; st < c.ladderStations; st++) printf(" (%.2f %.2f)", dot(c.nodes[c.ladder[st*8]].p - fr.com, fr.fwd), c.nodes[c.ladder[st*8]].p.y - g);
        printf("\n");
    }
}
