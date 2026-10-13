#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    for (int f = 0; f < 6; f++) {
        if (f) ctx.run(0.05);
        std::printf("t %.2f broken beams %d:", ctx.t, c.brokenBeams);
        for (size_t w = 0; w < c.wheels.size(); w++) {
            std::printf(" w%zu pow%d shafts", w, (int)c.wheelPowered(c.wheels[w]));
            for (int bi : c.wheels[w].shafts) std::printf(" %d%s(L0 %.3f now %.3f)", bi, bi >= 0 && c.beams[bi].broken ? "X" : "", bi>=0?c.beams[bi].L0:0, bi>=0?len(c.nodes[c.beams[bi].a].p-c.nodes[c.beams[bi].b].p):0);
        }
        std::printf("\n");
    }
    int nb = 0;
    for (size_t i = 0; i < c.beams.size(); i++) if (c.beams[i].broken) { nb++; if (nb < 20) std::printf("broken beam %zu type %d a %d b %d\n", i, (int)c.beams[i].type, c.beams[i].a, c.beams[i].b); }
    std::printf("gearboxOut %d frameNodes %d bodyNodes %d nodes %zu driveMode %d front %d\n", c.gearboxOut, c.frameNodes, c.bodyNodes, c.nodes.size(), c.driveMode, (int)c.frontEngaged());
}
