// Holds a vehicle's body still in the air (wheels hanging at full droop) and prints each tire's
// rub against its own body: there should be none.
#define private public
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int vi = argc > 1 ? std::atoi(argv[1]) : 5;
    Sim sim;
    sim.world = World(MAP_PROVING_GROUND);
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, -50, 0);
    ctx.run(1.0);
    for (int k = 0; k < c.bodyNodes; k++) c.nodes[k].p.y += 2.0;
    ctx.run(1.5, [&] {
        for (int k = 0; k < c.bodyNodes; k++) c.nodes[k].v = V3();  // body pinned in place
    });
    for (const Wheel& w : c.wheels) {
        const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5;
        std::printf("%s %s: load %.0f rub %.0f N, hub %.3f m below the body's rest\n", w.front ? "front" : "rear ", w.left ? "left " : "right", w.load, (double)w.rub,
                    c.frame().com.y - hub.y);
    }
}
