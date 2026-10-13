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
    Frame fr = c.frame();
    for (const SuspLink& l : c.links) {
        const Beam& b = c.beams[l.beam];
        V3 a = c.nodes[b.a].p - fr.com, bb = c.nodes[b.b].p - fr.com;
        V3 A(dot(a, fr.fwd), dot(a, fr.up), -dot(a, fr.left)), B(dot(bb, fr.fwd), dot(bb, fr.up), -dot(bb, fr.left));
        V3 d = B - A;
        printf("link kind %d wheel %d: (%.2f %.2f %.2f)->(%.2f %.2f %.2f) len %.2f, rise %.1f deg\n", (int)l.kind, l.wheel, A.x, A.y, A.z, B.x, B.y, B.z, len(d), std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * 180 / M_PI);
    }
}
