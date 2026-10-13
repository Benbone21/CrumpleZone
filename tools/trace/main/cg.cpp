// Center of mass height and roll inertia of each vehicle, settled on flat ground.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main() {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++) {
        Sim sim; Ctx ctx(sim);
        static VehicleSpec s; s = vehicleCatalog()[v];
        if (v == 3) s.items.push_back({"roof rack load", 350, -0.6, 2.05, 0});
        Car& c = ctx.add(s, -120, -50, 0);
        ctx.run(1.5, [&] { c.handbrake = 1; });
        double M = 0; V3 com;
        for (auto& n : c.nodes) { M += n.m; com += n.p * n.m; }
        com = com / M;
        double g = ctx.sim.world.height(com.x, com.z);
        double ixx = 0;
        for (auto& n : c.nodes) { V3 d = n.p - com; ixx += n.m * (d.y * d.y + d.z * d.z); }
        double track = std::fabs(c.nodes[c.wheels[0].outer].p.z - c.nodes[c.wheels[1].outer].p.z);
        printf("%-18s mass %.0f cg h %.3f track %.2f SSF %.2f Ixx %.0f\n", c.spec->name, M, com.y - g, track, track / 2 / (com.y - g), ixx);
    }
}
