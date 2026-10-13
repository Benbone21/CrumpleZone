// frontier/snow-road traced: the 4Runner up the Cold Creek road at 30 km/h.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv) {
    bool noTC = argc > 1 && atoi(argv[1]);
    Sim sim; Ctx ctx(sim);
    ctx.sim.world = World(MAP_FRONTIER);
    std::vector<V3> pts;
    double L = 0;
    {
        V3 prev = frontierFeature("snowroad:0");
        for (int s = 0; s < 2000; s++) { V3 p = frontierFeature(("snowroad:" + std::to_string(s + 1)).c_str()); double d = std::hypot(p.x - prev.x, p.z - prev.z); if (d < 1e-6) break; L += d; prev = p; }
        for (double s = 4; s <= L - 2 + 1e-6; s += 2) pts.push_back(frontierFeature(("snowroad:" + std::to_string(s)).c_str()));
    }
    Car& c = ctx.add(vehicleCatalog()[3], pts[0].x, pts[0].z, std::atan2(-(pts[1].z - pts[0].z), pts[1].x - pts[0].x) * 180 / M_PI);
    if (noTC) c.tractionControl = false;
    ctx.run(1.0);
    std::vector<double> at(pts.size(), 0);
    for (size_t k = 1; k < pts.size(); k++) at[k] = at[k - 1] + std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    size_t seg = 0; int f = 0;
    ctx.runUntil(120, [&] { return seg + 1 >= pts.size(); }, [&] {
        const Frame fr = c.frame();
        while (seg + 1 < pts.size()) { const V3 a = pts[seg], b = pts[seg + 1]; const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z)); if (dot(fr.com - a, dir) < std::hypot(b.x - a.x, b.z - a.z)) break; seg++; }
        if (seg + 1 >= pts.size()) return;
        const V3 a = pts[seg], b = pts[seg + 1]; const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
        const double along = dot(fr.com - a, dir);
        const double v = dot(fr.vel, fr.fwd), look = 5 + 0.6 * std::fabs(v);
        double s = at[seg] + along + look; size_t k = seg;
        while (k + 2 < pts.size() && at[k + 1] < s) k++;
        const V3 q = pts[k] + (pts[k + 1] - pts[k]) * clampd((s - at[k]) / std::max(1e-6, at[k + 1] - at[k]), 0, 1);
        V3 aim = q - fr.com; aim.y = 0;
        c.steer = clampd(std::atan2(dot(aim, fr.left), dot(aim, fr.fwd)) * 1.3, -0.5, 0.5);
        const double want = 30 / 3.6;
        c.throttle = clampd(0.3 + (want - v) * 0.5, 0, 1), c.brake = v > want + 1.5 ? 0.4 : 0, c.handbrake = 0;
        if (f++ % 120 == 0) {
            printf("t %5.1f at %5.0f/%4.0f v %5.1f slope %4.1f thr %.2f tcCut %.2f esc %.2f surf %d spin", ctx.t, at[seg] + along, L, v * 3.6, std::asin(fr.fwd.y) * 180 / M_PI, c.throttle, c.tcCut, c.escLevel, (int)c.wheels[0].surface);
            for (auto& w : c.wheels) printf(" %.1f", w.omega * w.rollR() - w.groundSpeed);
            printf(" rub"); for (auto& w : c.wheels) printf(" %.0f", w.rub); printf(" dmg %.2f steer %.2f", c.damage * 100, c.steer);
            printf("\n");
        }
    });
}
