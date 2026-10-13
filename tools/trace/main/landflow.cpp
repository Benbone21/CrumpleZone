// A jump landing (axle-landing "1.5 m at 40 km/h"): which frame members flow, and how far.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    double h = argc > 2 ? atof(argv[2]) : 1.5, pitch = argc > 3 ? atof(argv[3]) : 4, speed = argc > 4 ? atof(argv[4]) / 3.6 : 40 / 3.6;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
    ctx.run(1.0);
    std::vector<V3> r0[2];
    for (int s = 0; s < 2; s++) r0[s] = c.railFrame[s].rest;
    if (pitch != 0) c.rotateRigid(c.frame().left, pitch * M_PI / 180);
    const V3 fwd = c.frame().fwd;
    for (Node& n : c.nodes) { n.p.y += h; n.v = fwd * speed; }
    for (Wheel& w : c.wheels) w.omega = speed / w.R;
    c.brake = speed > 0 ? 0.4 : 0;
    double peak[4] = {0};
    ctx.run(3.0, [&] { for (int i = 0; i < 4; i++) peak[i] = std::max(peak[i], (double)c.wheels[i].load); });
    printf("peak wheel loads %.0f %.0f %.0f %.0f, damage %.2f%%\n", peak[0], peak[1], peak[2], peak[3], c.damage * 100);
    std::vector<std::pair<double, std::string>> out;
    for (int s = 0; s < 2; s++) {
        ChassisFrame& cf = c.railFrame[s];
        for (size_t k = 0; k < cf.nodes.size(); k++) {
            V3 d = c.railFrame[s].rot.rotate(cf.rest[k] - r0[s][k]);
            double fl = len(cf.rest[k] - r0[s][k]);
            if (fl < 0.003) continue;
            V3 l = c.restLocal[cf.nodes[k]];
            char buf[200];
            snprintf(buf, sizeof buf, "rail %d %s (%.2f %.2f %.2f) flow %.1f mm (%.1f %.1f %.1f)", s, cf.weight[k] > 1 ? "bracket" : "rail   ", l.x, l.y, l.z, fl * 1000, d.x * 1000, d.y * 1000, d.z * 1000);
            out.push_back({fl, buf});
        }
    }
    std::sort(out.rbegin(), out.rend());
    for (size_t i = 0; i < out.size() && i < 14; i++) printf("  %s\n", out[i].second.c_str());
}
