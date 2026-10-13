#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    Sim sim; Ctx ctx(sim);
    const VehicleSpec& vs = vehicleCatalog()[v];
    Car& c = ctx.add(vs, -100, -50, 0);
    ctx.run(2.0, [&]{ c.handbrake = 1; });
    Frame fr = c.frame();
    double g = ctx.sim.world.height(fr.com.x, fr.com.z);
    auto L = [&](int n) { V3 d = c.nodes[n].p - fr.com; return V3(dot(d, fr.fwd), c.nodes[n].p.y - g, -dot(d, fr.left)); };
    const char* kn[] = {"LOWER_ARM","UPPER_ARM","TIE_ROD","TOE_LINK","TRAILING","PANHARD","BUSHING","STRUT_GUIDE","LEAF"};
    for (const SuspLink& l : c.links) {
        const Beam& b = c.beams[l.beam];
        V3 a = L(b.a), bb = L(b.b);
        if (l.wheel >= 0 && c.wheels[l.wheel].front != (argc > 2)) continue;
        if (l.wheel < 0 && argc > 2) continue;
        printf("%-10s wheel %d  (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)\n", kn[l.kind], l.wheel, a.x, a.y, a.z, bb.x, bb.y, bb.z);
    }
    double cgy = 0, m = 0; for (const Node& n : c.nodes) cgy += (n.p.y - g) * n.m, m += n.m;
    printf("CG height %.3f  wheelbase %.3f  front share %.3f\n", cgy / m, c.wheelbase, c.frontShare);
    for (const Wheel& w : c.wheels) if (w.front == (argc > 2)) { V3 h = L(w.inner); printf("hub (%.2f %.2f %.2f) R %.3f\n", h.x, h.y, h.z, w.R); break; }
}
