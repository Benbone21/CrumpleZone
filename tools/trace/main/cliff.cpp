#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include <cstdio>
#include <cstdlib>
V3 mountainFeature(const char*);
int main(int argc, char** argv) {
    int node = argc > 1 ? atoi(argv[1]) : 371;
    Sim sim; Ctx ctx(sim);
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 s0 = mountainFeature("spur"), pin = mountainFeature("pinnacle");
    const V3 out = norm(V3(pin.x - s0.x, 0, pin.z - s0.z)), left(-out.z, 0, out.x);
    const V3 c = s0 + out * 112 + left * 0.75;
    Car& t = ctx.add(vehicleCatalog()[2], c.x, c.z, std::atan2(-out.z, out.x) * 180 / M_PI);
    ctx.run(1.5, [&] { t.handbrake = 1; });
    for (Node& n : t.nodes) n.v = left * 2.5;
    double worst = 0, tw = 0; int wn = -1;
    ctx.run(10, [&] {
        t.handbrake = 0, t.brake = 0, t.throttle = 0;
        for (size_t i = 0; i < t.nodes.size(); i++) {
            const Node& n = t.nodes[i];
            double d = ctx.sim.world.height(n.p.x, n.p.z) - n.p.y;
            if (d > worst) worst = d, tw = ctx.t, wn = (int)i;
        }
        const Node& n = t.nodes[node];
        double gy = ctx.sim.world.height(n.p.x, n.p.z);
        V3 nr = ctx.sim.world.normal(n.p.x, n.p.z);
        if (gy - n.p.y > 0.1) {
            const Wheel& w = t.wheels[0];
            std::printf("t=%.3f node %d under %.3f  normal.y %.2f  v (%.1f %.1f %.1f)  wheel0 load %.0f rub %.0f popped %d det %d  up.y %.2f\n", ctx.t, node, gy - n.p.y, nr.y, n.v.x, n.v.y, n.v.z, w.load, w.rub, w.popped, w.detached, t.frame().up.y);
        }
    });
    std::printf("worst %.3f at t=%.2f node %d  damage %.1f%%\n", worst, tw, wn, t.damage * 100);
}
