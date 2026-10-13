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
    ctx.run(2.0);
    double lowest = 1e9; int who = -1;
    for (size_t i = 0; i < c.nodes.size(); i++) if (c.nodes[i].collide && c.nodes[i].p.y < lowest) lowest = c.nodes[i].p.y, who = i;
    printf("lowest colliding node %d y %.3f (body %d, frameNodes %d, ladder %zu..)\n", who, lowest - ctx.sim.world.height(c.nodes[who].p.x, c.nodes[who].p.z), c.bodyNodes, c.frameNodes, c.ladder.size());
    double minL = 1e9;
    for (int n : c.ladder) minL = std::min(minL, c.nodes[n].p.y - ctx.sim.world.height(c.nodes[n].p.x, c.nodes[n].p.z));
    printf("lowest frame node above ground: %.3f\n", minL);
    for (auto& w : c.wheels) printf("wheel load %.0f\n", w.load);
}
