#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    double h = argc > 2 ? atof(argv[2]) : 1.0;
    Sim sim; Ctx ctx(sim);
    V3 at(-100, 0, -50);
    if (argc > 3) at = ctx.sim.world.courseStart(atoi(argv[3])) + V3(8, 0, 0);
    Car& c = ctx.add(vehicleCatalog()[v], at.x, at.z, 0);
    ctx.run(1.0);
    std::vector<double> L0(c.beams.size());
    for (size_t i = 0; i < c.beams.size(); i++) L0[i] = c.beams[i].L0;
    for (Node& n : c.nodes) { n.p.y += h; n.v = V3(); }
    ctx.run(3.0);
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pick" : "move"; };
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    std::vector<std::pair<double,int>> w;
    for (size_t i = 0; i < c.beams.size(); i++) { double d = std::fabs(c.beams[i].L0 - L0[i]); if (d > 0.001 || c.beams[i].broken) w.push_back({d, (int)i}); }
    std::sort(w.rbegin(), w.rend());
    printf("%zu beams changed, damage %.3f%%\n", w.size(), c.damage*100);
    {
        double mx[12] = {0}; int cnt[12] = {0};
        for (size_t i = 0; i < c.beams.size(); i++) { int t = c.beams[i].type; double d = std::fabs(c.beams[i].L0 - L0[i]); if (d > 0.001) cnt[t]++; mx[t] = std::max(mx[t], d); }
        for (int t = 0; t < 12; t++) if (cnt[t]) printf("   %s: %d set, max %.1f mm\n", tn[t], cnt[t], mx[t]*1000);
    }
    for (int k = 0; k < 12 && k < (int)w.size(); k++) { const Beam& b = c.beams[w[k].second]; printf("  %.1f mm %s %s-%s a(%.2f %.2f %.2f) b(%.2f %.2f %.2f) L %.2f ys %.3f bs %.3f %s\n", w[k].first*1000, tn[b.type], cat(b.a), cat(b.b), c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z, b.Lorig, b.yieldStrain, b.breakStrain, b.broken ? "BROKEN" : ""); }
}
