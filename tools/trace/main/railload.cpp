#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    double h = argc > 2 ? atof(argv[2]) : 5.0;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.0);
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    for (Node& n : c.nodes) { n.p.y += h; n.v = V3(); }
    double worst = 0; int wb = -1; double worstT = 0; double peakWheel = 0, peakBump = 0;
    ctx.run(2.0, [&] {
        for (auto& w : c.wheels) peakWheel = std::max(peakWheel, (double)w.load);
        for (auto& w : c.wheels) if (w.bumpStop >= 0) { const Beam& b = c.beams[w.bumpStop]; double L = len(c.nodes[b.b].p - c.nodes[b.a].p); peakBump = std::max(peakBump, b.minL - L); }
        for (size_t i = 0; i < c.beams.size(); i++) {
            const Beam& b = c.beams[i];
            if (!lad.count(b.a) || !lad.count(b.b)) continue;
            double L = len(c.nodes[b.b].p - c.nodes[b.a].p), r = std::fabs(L - b.L0) / b.L0 / b.yieldStrain;
            if (r > worst) worst = r, wb = i, worstT = ctx.t;
        }
    });
    const Beam& b = c.beams[wb];
    printf("drop %.1f: worst rail beam reached %.2f of its yield at t=%.2f (len %.2f, yield %.3f, between (%.2f %.2f %.2f)-(%.2f %.2f %.2f)); peak wheel load %.0f N; jounce squash %.3f m\n", h, worst, worstT, b.Lorig, b.yieldStrain,
        c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z, peakWheel, peakBump);
}
