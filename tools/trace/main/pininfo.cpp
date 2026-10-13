#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = atoi(argv[1]), n = atoi(argv[2]);
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    printf("node %d m %.3f\n", n, c.nodes[n].m);
    for (size_t i = 0; i < c.pins.size(); i++) { const Pin& p = c.pins[i]; if (p.panel == n || p.body == n) printf(" pin %zu kind %d body %d (m %.2f) panel %d k %.0f c %.0f slack %.3f brk %.3f\n", i, p.kind, p.body, c.nodes[p.body].m, p.panel, p.k, p.c, p.slack, p.breakDist); }
    for (const Beam& b : c.beams) if (b.a == n || b.b == n) printf(" beam type %d to %d k %.0f c %.1f L %.3f\n", b.type, b.a == n ? b.b : b.a, b.k, b.c, b.Lorig);
    for (int s = 0; s < 40; s++) { sim.substep(); printf("  s%d v (%.2f %.2f %.2f)\n", s, c.nodes[n].v.x, c.nodes[n].v.y, c.nodes[n].v.z); }
}
