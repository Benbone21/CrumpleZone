// Nose off a 1.2 m ledge (rock-hits #3): where does the frame take a set, and when.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cmath>
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
    const double H = argc > 3 ? atof(argv[3]) : 1.2;  // the ledge's height (m)
    w.addBox(V3(-120, w.height(-120, z) + H / 2, z), V3(10, H / 2, 3), 0, 0, 0, V3(0.4, 0.38, 0.35));
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
        if (getenv("LEDGE_LOG") && fr.com.x > -112.5) {
            // The frame's tail end (its last station, lowest node) and the body's lowest tail node against the ledge's top (1.2 m up, lip at x = -110).
            const double top = w.height(-120, z) + H;
            double ty = 1e9, tx = 0, tvy = 0, by = 1e9, bx = 0;
            for (int n : c.ladder) if (c.restLocal[n].x < c.restLocal[c.ladder.back()].x + 0.05 && c.nodes[n].p.y < ty) ty = c.nodes[n].p.y, tx = c.nodes[n].p.x, tvy = c.nodes[n].v.y;
            double minx = 1e9; for (int n = 0; n < c.bodyNodes; n++) minx = std::min(minx, (double)c.restLocal[n].x);
            for (int n = 0; n < c.bodyNodes; n++) if (c.restLocal[n].x < minx + 0.05 && c.nodes[n].p.y < by) by = c.nodes[n].p.y, bx = c.nodes[n].p.x;
            double sNow = 0; for (const Beam& b : c.beams) if (b.type == BEAM_FRAME) sNow = std::max(sNow, std::fabs(b.L0 - b.Lorig) / b.Lorig);
            printf("LOG t %.3f com %.2f pitch %5.1f v %.2f thr %.2f loads %5.0f %5.0f %5.0f %5.0f | rail tail %.2f past, %+.2f over top, vy %+.2f | body tail %.2f past, %+.2f over | set %.2f%%\n", ctx.t, fr.com.x + 110, std::asin(fr.fwd.y) * 180 / M_PI, vv, c.throttle,
                   c.wheels[0].load, c.wheels[1].load, c.wheels[2].load, c.wheels[3].load, tx + 110, ty - top, tvy, bx + 110, by - top, sNow * 100);
        }
        double s = 0;
        for (const Beam& b : c.beams) if (b.type == BEAM_FRAME) s = std::max(s, std::fabs(b.L0 - b.Lorig) / b.Lorig);
        if (s > worstSet + 0.005) {
            printf("t %.2f x %.1f set %.2f%% peak wheel loads", ctx.t, fr.com.x, s * 100); for (auto& wh : c.wheels) printf(" %.0f", wh.load); printf("\n"); worstSet = s;
            // Where the worst member is against the ledge's lip (x = -110, top 1.2 m over the ground below), and how it's moving.
            const double top = w.height(-120, z);
            size_t wb = 0; double wv = 0;
            for (size_t i = 0; i < c.beams.size(); i++) { const Beam& b = c.beams[i]; if (b.type != BEAM_FRAME) continue; const double d = std::fabs(b.L0 - b.Lorig) / b.Lorig; if (d > wv) wv = d, wb = i; }
            const Beam& b = c.beams[wb];
            for (int n : {b.a, b.b}) printf("    node %d (local %.2f %.2f %.2f) %.2f m past the lip, %.2f m over its top, v (%.2f %.2f) collide %d m %.1f kg\n", n, c.restLocal[n].x, c.restLocal[n].y, c.restLocal[n].z, c.nodes[n].p.x + 110, c.nodes[n].p.y - top, c.nodes[n].v.x, c.nodes[n].v.y, (int)c.nodes[n].collide, c.nodes[n].m);
            printf("    member now %.3f m (rest %.3f, factory %.3f), pitch %.1f deg, speed %.2f, throttle %.2f\n", len(c.nodes[b.a].p - c.nodes[b.b].p), b.L0, b.Lorig, std::asin(fr.fwd.y) * 180 / M_PI, vv, c.throttle);
            // The lowest colliding nodes near the lip.
            for (size_t i = 0; i < c.nodes.size(); i++) { const Node& n = c.nodes[i]; if (!n.collide) continue; if (std::fabs(n.p.x + 110) < 0.6 && n.p.y - top < 0.08) printf("    near the lip: node %zu (local %.2f %.2f %.2f) %.2f past, %.3f over the top\n", i, c.restLocal[i].x, c.restLocal[i].y, c.restLocal[i].z, n.p.x + 110, n.p.y - top); }
        }
    });
    ctx.run(1.0, [&] { c.throttle = 0, c.brake = 1; });
    std::set<int> lad(c.ladder.begin(), c.ladder.end());
    auto cat = [&](int n) { return n < c.bodyNodes ? "body" : lad.count(n) ? "rail" : n < c.frameNodes ? "pick" : "move"; };
    std::vector<std::pair<double,int>> ws;
    for (size_t i = 0; i < c.beams.size(); i++) { const Beam& b = c.beams[i]; if (b.type != BEAM_FRAME) continue; double d = std::fabs(b.L0 - b.Lorig) / b.Lorig; if (d > 0.001) ws.push_back({d, (int)i}); }
    std::sort(ws.rbegin(), ws.rend());
    { double pmin = 0; (void)pmin; }
    printf("%zu frame beams set, damage %.2f%%\n", ws.size(), c.damage * 100);
    for (int k = 0; k < 15 && k < (int)ws.size(); k++) { const Beam& b = c.beams[ws[k].second]; printf("  %.2f%% (%.1f mm) %s-%s (%.2f %.2f %.2f)-(%.2f %.2f %.2f) L %.2f\n", ws[k].first * 100, std::fabs(b.L0 - b.Lorig) * 1000, cat(b.a), cat(b.b), c.restLocal[b.a].x, c.restLocal[b.a].y, c.restLocal[b.a].z, c.restLocal[b.b].x, c.restLocal[b.b].y, c.restLocal[b.b].z, b.Lorig); }
}
