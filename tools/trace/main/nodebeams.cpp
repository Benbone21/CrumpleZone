// List the beams at the frame node nearest a rest position.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = atoi(argv[1]);
    V3 p(atof(argv[2]), atof(argv[3]), atof(argv[4]));
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    int best = 0;
    for (size_t i = 0; i < c.nodes.size(); i++) if (len(c.restLocal[i] - p) < len(c.restLocal[best] - p)) best = (int)i;
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pick" : "move"; };
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    printf("node %d %s (%.2f %.2f %.2f) m %.2f\n", best, cat(best), c.restLocal[best].x, c.restLocal[best].y, c.restLocal[best].z, c.nodes[best].m);
    for (const Beam& b : c.beams) {
        if (b.a != best && b.b != best) continue;
        int o = b.a == best ? b.b : b.a;
        printf("  %s to %s (%.2f %.2f %.2f) k %.2g L %.2f\n", tn[b.type], cat(o), c.restLocal[o].x, c.restLocal[o].y, c.restLocal[o].z, b.k, b.Lorig);
    }
}
