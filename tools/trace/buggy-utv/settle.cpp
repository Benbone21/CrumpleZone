// Settle test replica: which nodes are still moving after 4 s, and how.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    ctx.run(4.0);
    std::map<int, int> fastest;
    double maxV = 0;
    ctx.run(1.0, [&] {
        int best = -1; double v = 0;
        for (size_t i = 0; i < c.nodes.size(); i++) if (len(c.nodes[i].v) > v) v = len(c.nodes[i].v), best = (int)i;
        fastest[best]++;
        maxV = std::max(maxV, v);
    });
    std::printf("max %.3f m/s sleeping %d\n", maxV, (int)c.sleeping);
    for (auto& f : fastest) {
        const V3& r = c.restLocal[f.first];
        std::printf("node %d (%.3f %.3f %.3f) m %.2f collide %d frames %d v %.3f\n", f.first, r.x, r.y, r.z, c.nodes[f.first].m, (int)c.nodes[f.first].collide, f.second, len(c.nodes[f.first].v));
    }
    // per-substep velocity history of the top node
    int top = fastest.rbegin()->first; int cnt = 0;
    for (auto& f : fastest) if (f.second > cnt) cnt = f.second, top = f.first;
    const int ref = argc > 2 ? atoi(argv[2]) : top;
    for (int s = 0; s < 1; s++) {
        sim.substep();
        if (s % 20) continue;
        const V3 v = c.nodes[top].v, vr = c.nodes[ref].v;
        double bodyV = 0; for (int i = 0; i < c.bodyNodes; i++) bodyV = std::max(bodyV, len(c.nodes[i].v));
        std::printf("%d: v (%.3f %.3f %.3f) body max %.3f |", s, v.x, v.y, v.z, bodyV);
        for (int n : c.axles[1].housing) std::printf(" %d:(%.3f %.3f)", n, c.nodes[n].v.x, c.nodes[n].v.y);
        const Wheel& w = c.wheels[2];
        std::printf(" hub (%.3f %.3f) om %.3f", c.nodes[w.inner].v.x, c.nodes[w.inner].v.y, w.omega);
        std::printf("\n");
    }
    for (size_t bi = 0; bi < c.beams.size(); bi++) {
        const Beam& b = c.beams[bi];
        if (b.a != top && b.b != top) continue;
        const int o = b.a == top ? b.b : b.a;
        std::printf("beam %zu to %d type %d k %.0f c %.1f L0 %.4f Lorig %.4f now %.4f broken %d yield %.3f\n", bi, o, (int)b.type, b.k, b.c, b.L0, b.Lorig, len(c.nodes[b.a].p - c.nodes[b.b].p), (int)b.broken, b.yieldStrain);
    }
    for (int n : c.chassis.nodes) if (n == top) std::printf("in chassis group\n");
    for (const Wheel& w : c.wheels) std::printf("wheel omega %.3f load %.0f sideOm %.3f twist %.5f\n", w.omega, w.load, w.sideOmega, w.twist);
}
