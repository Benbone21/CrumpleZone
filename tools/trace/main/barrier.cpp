// damage/barrier-bump (square): which beams tear.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    double ang = argc > 2 ? atof(argv[2]) : 0;
    Sim sim; sim.setupEmpty(); Ctx ctx(sim);
    const VehicleSpec& spec = vehicleCatalog()[v];
    ctx.addProp(barrierSpec(), 0, 0, 90);
    Car& c = ctx.add(spec, -1.5 - spec.length / 2, 0, ang);
    ctx.run(1.0);
    ctx.launch(c, 18 / 3.6);
    ctx.run(2.5);
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pick" : "move"; };
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    for (const Beam& b : c.beams) if (b.broken) printf("broken %s %s-%s (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", tn[b.type], cat(b.a), cat(b.b), c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z);
    printf("brokenBeams %d damage %.2f%%\n", c.brokenBeams, c.damage * 100);
}
