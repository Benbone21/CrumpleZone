#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.0);
    Wheel& w = c.wheels[0];
    const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5;
    ctx.sim.world.addBox(V3(hub.x, ctx.sim.world.height(hub.x, hub.z) + 0.25, hub.z), V3(0.35, 0.25, 0.25), 0, 0, 0, V3(0.5, 0.5, 0.5));
    ctx.sim.world.buildGrid();
    c.beams[w.spring].broken = true;
    for (Node& n : c.nodes) n.p.y += 1.5;
    double peak = 0, worstStrain = 0; int worstBeam = -1;
    ctx.run(2.0, [&] {
        if (w.rub > peak) {
            peak = w.rub;
            // which tris are touched: report their nodes' types
            const V3 a = c.nodes[w.inner].p, b = c.nodes[w.outer].p, ctr = (a + b) * 0.5, ax = norm(b - a);
            printf("t=%.3f rub %.0f:", ctx.t, w.rub);
            for (int t : w.rubTris) {
                const Tri& tr = c.tris[t];
                V3 m = (c.nodes[tr.v[0]].p + c.nodes[tr.v[1]].p + c.nodes[tr.v[2]].p) / 3.0;
                V3 d = m - ctr; double da = dot(d, ax); double dr = len(d - ax * da);
                if (dr < w.R + 0.05 && std::fabs(da) < w.width) printf(" tri%d(mat%d nodes %d %d %d %s)", t, tr.mat, tr.v[0], tr.v[1], tr.v[2], tr.v[0] < c.bodyNodes ? "body" : "panel");
            }
            printf("\n");
        }
        for (size_t i = 0; i < c.beams.size(); i++) {
            const Beam& bm = c.beams[i];
            if (bm.type != BEAM_BODY && bm.type != BEAM_PANEL) continue;
            double L = len(c.nodes[bm.b].p - c.nodes[bm.a].p), st = std::fabs(L - bm.L0) / bm.L0 / bm.yieldStrain;
            if (st > worstStrain) worstStrain = st, worstBeam = i;
        }
    });
    printf("peak %.0f N; worst body/panel beam strain relative to its yield: %.2f (beam %d type %d)\n", peak, worstStrain, worstBeam, c.beams[worstBeam].type);
    { const Beam& b = c.beams[worstBeam]; printf("  (%.2f %.2f %.2f)-(%.2f %.2f %.2f) L %.2f yield %.4f set %.2f mm\n", c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z, b.Lorig, b.yieldStrain, (b.L0 - b.Lorig) * 1000); }
    double ms = 0; for (const Beam& b : c.beams) if (b.type == BEAM_BODY || b.type == BEAM_PANEL || b.type == BEAM_FRAME) ms = std::max(ms, std::fabs(b.L0 - b.Lorig));
    printf("  largest set in body/panel/frame: %.2f mm\n", ms * 1000);
}
