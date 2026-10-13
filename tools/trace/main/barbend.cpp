#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    double drop = argc > 2 ? atof(argv[2]) : 1.2, box = argc > 3 ? atof(argv[3]) : 0.225;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.0);
    Wheel& w = c.wheels[0];
    const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5;
    ctx.sim.world.addBox(V3(hub.x, ctx.sim.world.height(hub.x, hub.z) + box, hub.z), V3(0.35, box, 0.25), 0, 0, 0, V3(0.5, 0.5, 0.5));
    ctx.sim.world.buildGrid();
    for (Node& n : c.nodes) n.p.y += drop;
    double worst = 0;
    ctx.run(2.0, [&] { for (auto& a : c.axles) worst = std::max(worst, std::fabs(a.arbBend)); });
    printf("%s drop %.1f box %.2f: bar set front %.3f rear %.3f (yield %.3f / %.3f) roll %.2f deg damage %.2f%%\n", vehicleCatalog()[v].name, drop, box * 2, c.axles[0].arbBend, c.axles[1].arbBend, c.axles[0].arbYield, c.axles[1].arbYield,
           std::asin(c.frame().left.y) * 57.3, c.damage * 100);
}
