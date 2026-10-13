#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <set>
int main() {
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        ctx.run(2.0, [&] { c.handbrake = 1; });
        double g = ctx.sim.world.height(c.frame().com.x, c.frame().com.z);
        printf("%s\n", vehicleCatalog()[v].name);
        for (size_t a = 0; a < c.axles.size(); a++) {
            const Axle& ax = c.axles[a];
            printf("  axle %zu %s housing:", a, ax.front ? "front" : "rear");
            for (int n : ax.housing) printf(" %d(%.3f%s)", n, c.nodes[n].p.y - g, c.nodes[n].collide ? "" : " nc");
            printf("  diff:");
            for (int n : ax.diffNodes) printf(" %d(%.3f%s)", n, c.nodes[n].p.y - g, c.nodes[n].collide ? "" : " nc");
            if (ax.diffNode >= 0) printf("  diffNode %d(%.3f%s)", ax.diffNode, c.nodes[ax.diffNode].p.y - g, c.nodes[ax.diffNode].collide ? "" : " nc");
            printf("\n");
        }
        for (size_t i = 0; i < c.wheels.size(); i++) {
            const Wheel& w = c.wheels[i];
            double lo = 1e9; int ln = -1; int nc = 0;
            for (int n : w.assembly) { if (c.nodes[n].p.y - g < lo) lo = c.nodes[n].p.y - g, ln = n; nc += !c.nodes[n].collide; }
            printf("  wheel %zu assembly %zu nodes, lowest %d at %.3f (%s), non-colliding %d\n", i, w.assembly.size(), ln, lo, c.nodes[ln].collide ? "collides" : "no collide", nc);
        }
        // lowest colliding nodes overall excluding hub inner/outer
        std::vector<std::pair<double,int>> low;
        for (size_t i = 0; i < c.nodes.size(); i++) if (c.nodes[i].collide) low.push_back({c.nodes[i].p.y - g, (int)i});
        std::sort(low.begin(), low.end());
        printf("  lowest colliding:");
        for (int k = 0; k < 6; k++) printf(" %d(%.3f)", low[k].second, low[k].first);
        printf("\n");
    }
}
