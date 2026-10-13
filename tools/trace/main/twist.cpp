// Torsion of a ladder frame alone (body mounts cut) vs with the body on, loaded at the axle stations.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    for (int cut = 0; cut < 2; cut++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
        for (Node& n : c.nodes) n.p.y += 40;
        c.extForce.assign(c.nodes.size(), V3());
        if (cut) for (int bi : c.bodyMountBeams) c.beams[bi].broken = true;
        ctx.run(0.3);
        double xf = -1e9, xr = 1e9;
        for (const Wheel& w : c.wheels) xf = std::max(xf, c.restLocal[w.inner].x), xr = std::min(xr, c.restLocal[w.inner].x);
        auto st = [&](double x) { int b = 0; for (int s = 0; s < c.ladderStations; s++) if (std::fabs(c.restLocal[c.ladder[s * 8]].x - x) < std::fabs(c.restLocal[c.ladder[b * 8]].x - x)) b = s; return b; };
        int sf = st(xf), sr = st(xr);
        auto side = [&](int s, int lr) { V3 p; for (int k = 0; k < 4; k++) p += c.nodes[c.ladder[s * 8 + lr * 4 + k]].p; return p / 4.0; };
        auto roll = [&](int s) { V3 d = side(s, 0) - side(s, 1); return std::atan2(d.y, -d.z); };
        double sep = len(side(sf, 0) - side(sf, 1));
        const double T = 3000;
        double f0 = roll(sf), r0 = roll(sr), twist = 0;
        ctx.run(0.8, [&] {
            double ramp = smoothstep(0, 0.3, ctx.t - 0.3);
            std::fill(c.extForce.begin(), c.extForce.end(), V3());
            V3 F(0, ramp * T / sep / 4, 0);
            for (int k = 0; k < 4; k++) {
                c.extForce[c.ladder[sf * 8 + k]] += F; c.extForce[c.ladder[sf * 8 + 4 + k]] -= F;
                c.extForce[c.ladder[sr * 8 + k]] -= F; c.extForce[c.ladder[sr * 8 + 4 + k]] += F;
            }
            twist = ((roll(sf) - f0) - (roll(sr) - r0)) * 180 / M_PI;
        });
        printf("%s %s: %.1f kNm/deg (twist %.2f deg)\n", c.spec->name, cut ? "frame alone" : "with body", T / 1000 / std::fabs(twist), twist);
    }
}
