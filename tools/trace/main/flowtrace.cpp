// Drop a truck and report when/where its rails flow (rest moves) for the first time.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    double h = argc > 2 ? atof(argv[2]) : 5.0;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.5, [&] { c.handbrake = 1; });
    std::vector<V3> r0[2];
    for (int s = 0; s < 2; s++) r0[s] = c.railFrame[s].rest;
    std::vector<char> seen[2];
    for (int s = 0; s < 2; s++) seen[s].assign(r0[s].size(), 0);
    for (Node& n : c.nodes) { n.p.y += h; n.v = V3(); }
    for (int f = 0; f < 180; f++) {
        ctx.step();
        c.handbrake = 1;
        for (int s = 0; s < 2; s++) {
            ChassisFrame& cf = c.railFrame[s];
            for (size_t k = 0; k < cf.nodes.size(); k++) {
                double d = len(cf.rest[k] - r0[s][k]);
                if (d > 0.002 && !seen[s][k]) {
                    seen[s][k] = 1;
                    V3 l = c.restLocal[cf.nodes[k]];
                    printf("t %.3f rail %d node (%.2f %.2f %.2f) w %.0f m %.1f flow %.1f mm  loads", ctx.t, s, l.x, l.y, l.z, cf.weight.empty() ? 1.0 : cf.weight[k], c.nodes[cf.nodes[k]].m, d * 1000);
                    for (auto& wh : c.wheels) printf(" %.0f", wh.load);
                    printf("\n");
                }
            }
        }
    }
}
