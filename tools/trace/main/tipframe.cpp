#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -60, 20, 0);
    ctx.run(1.0, [&] { c.handbrake = 1; });
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pickup" : "moving"; };
    auto report = [&](const char* when) {
        std::vector<std::pair<double, int>> w;
        for (size_t i = 0; i < c.beams.size(); i++) {
            const Beam& b = c.beams[i];
            if (b.type != BEAM_FRAME) continue;
            w.push_back({b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig, (int)i});
        }
        std::sort(w.rbegin(), w.rend());
        printf("%s:\n", when);
        for (int k = 0; k < 6 && k < (int)w.size(); k++) {
            const Beam& b = c.beams[w[k].second];
            printf("  %.1f%% %s(%d)-%s(%d) L0 %.3f orig %.3f k %.0f yield %.3f broken %d\n", w[k].first * 100, cat(b.a), b.a, cat(b.b), b.b, b.L0, b.Lorig, b.k, b.yieldStrain, b.broken);
        }
    };
    report("after settle");
    if (getenv("BOULDER")) {
        ctx.sim.cars.clear();
        World& w = ctx.sim.world;
        w.addRock(-140, -20, V3(0.8, 0.7, 0.8), 0.6, 77, 1);
        w.buildGrid();
        Car& d = ctx.add(vehicleCatalog()[v], -150, -20, 0);
        ctx.run(1.0);
        d.setDriveMode((int)driveModes(d.spec->transfer).size() - 1);
        d.tractionControl = false;
        lad = std::set<int>(d.ladder.begin(), d.ladder.end());
        ctx.runUntil(30, [&] { return d.frame().com.x > -132; }, [&] {
            Frame fr = d.frame(); V3 dd = V3(fr.com.x + 6, fr.com.y, -20) - fr.com;
            d.steer = std::max(-0.55, std::min(0.55, std::atan2(dot(dd, fr.left), std::max(dot(dd, fr.fwd), 0.5)) * 1.5));
            double vv = dot(fr.vel, fr.fwd); d.throttle = std::max(0.0, std::min(1.0, 0.3 + (1.5 - vv) * 0.9)); d.brake = vv > 2.5 ? 0.3 : 0; d.handbrake = 0;
        });
        Car& c2 = d;
        std::vector<std::pair<double, int>> w2;
        for (size_t i = 0; i < c2.beams.size(); i++) { const Beam& b = c2.beams[i]; if (b.type == BEAM_FRAME) w2.push_back({b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig, (int)i}); }
        std::sort(w2.rbegin(), w2.rend());
        for (int k = 0; k < 8; k++) { const Beam& b = c2.beams[w2[k].second]; printf("  %.1f%% %s-%s  a(%.2f %.2f %.2f) b(%.2f %.2f %.2f) yield %.3f\n", w2[k].first*100, b.a < c2.bodyNodes ? "body" : lad.count(b.a) ? "rail" : "pick", b.b < c2.bodyNodes ? "body" : lad.count(b.b) ? "rail" : "pick", c2.restLocal[b.a].x, c2.restLocal[b.a].y, c2.restLocal[b.a].z, c2.restLocal[b.b].x, c2.restLocal[b.b].y, c2.restLocal[b.b].z, b.yieldStrain); }
        return 0;
    }
    c.rotateRigid(c.frame().fwd, M_PI / 2);
    for (Node& n : c.nodes) n.p.y += 0.4;
    ctx.run(3.0, [&] { c.handbrake = 1; });
    report("after tip");
}
