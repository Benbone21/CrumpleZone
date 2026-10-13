// Nose off a 1.2 m ledge (rock-hits #3): where does the frame take a set, and when.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <algorithm>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    double speed = argc > 2 ? atof(argv[2]) : 2.5;
    Sim sim; Ctx ctx(sim);
    World& w = ctx.sim.world;
    const double z = 60;
    w.addBox(V3(-120, w.height(-120, z) + 0.6, z), V3(10, 0.6, 3), 0, 0, 0, V3(0.4, 0.38, 0.35));
    w.boxes.back().rock = true;
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[v], -125, z, 0);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(c.spec->transfer).size() - 1);
    c.tractionControl = false;
    std::vector<double> L0(c.beams.size());
    for (size_t i = 0; i < c.beams.size(); i++) L0[i] = c.beams[i].L0;
    double worstSet = 0;
    ctx.runUntil(25, [&] { return c.frame().com.x > -104; }, [&] {
        Frame fr = c.frame();
        double vv = dot(fr.vel, fr.fwd);
        c.steer = 0;
        c.throttle = clampd(0.3 + (speed - vv) * 0.9, 0, 1);
        c.brake = vv > speed + 1.0 ? 0.3 : 0;
        c.handbrake = 0;
        double s = 0;
        for (const Beam& b : c.beams) if (b.type == BEAM_FRAME) s = std::max(s, std::fabs(b.L0 - b.Lorig) / b.Lorig);
        if (s > worstSet + 0.005) { printf("t %.2f x %.1f set %.2f%% peak wheel loads", ctx.t, fr.com.x, s * 100); for (auto& wh : c.wheels) printf(" %.0f", wh.load); printf("\n"); worstSet = s; }
    });
    ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; });
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pick" : "move"; };
    std::vector<std::pair<double,int>> ws;
    for (size_t i = 0; i < c.beams.size(); i++) { const Beam& b = c.beams[i]; if (b.type != BEAM_FRAME) continue; double d = std::fabs(b.L0 - b.Lorig) / b.Lorig; if (d > 0.001) ws.push_back({d, (int)i}); }
    std::sort(ws.rbegin(), ws.rend());
    printf("%zu frame beams set, damage %.2f%%\n", ws.size(), c.damage * 100);
    for (int k = 0; k < 15 && k < (int)ws.size(); k++) { const Beam& b = c.beams[ws[k].second]; printf("  %.2f%% (%.1f mm) %s-%s (%.2f %.2f %.2f)-(%.2f %.2f %.2f) L %.2f\n", ws[k].first * 100, std::fabs(b.L0 - b.Lorig) * 1000, cat(b.a), cat(b.b), c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z, b.Lorig); }
}
