// A modded truck at full throttle: does a tire rub its body? (vehicle, slot, option)
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/mods.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2, slot = argc > 2 ? atoi(argv[2]) : 1, opt = argc > 3 ? atoi(argv[3]) : 2;
    ModSet ms;
    ms.pick[slot] = opt;
    const VehicleSpec& spec = tunedSpec(v, ms);
    printf("%s R %.3f\n", modOptions(v, slot)[opt].name, spec.wheelRadius);
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(spec, -120, -50, 0);
    ctx.run(2.5);
    printf("parked rub:"); for (auto& w : c.wheels) printf(" %.0f", w.rub); printf("\n");
    int f = 0;
    ctx.runUntil(20, [&] { return speedKmh(c) >= 80; }, [&] {
        c.throttle = 1; c.handbrake = 0; c.steer = 0;
        if (f++ % 60 == 0) { printf("t %.1f v %.0f rub", ctx.t, speedKmh(c)); for (auto& w : c.wheels) printf(" %6.0f/%5.0fNm", w.rub, w.rubTorque); printf("\n"); }
    });
    printf("0-80 at t=%.1f\n", ctx.t - 2.5);
    // Where the right rear tire touches, in the body frame (deepest samples).
    Wheel& wh = c.wheels[3];
    const V3 a = c.nodes[wh.inner].p, b = c.nodes[wh.outer].p, ctr = (a + b) * 0.5, ax = norm(b - a);
    Frame fr = c.frame();
    V3 hl = ctr - fr.com;
    printf("RR hub in body frame: x %.2f y %.2f z %.2f (rest %.2f %.2f %.2f)\n", dot(hl, fr.fwd), dot(hl, fr.up), -dot(hl, fr.left), (c.restLocal[wh.inner].x + c.restLocal[wh.outer].x) / 2, 0.0, (c.restLocal[wh.inner].z + c.restLocal[wh.outer].z) / 2);
    for (size_t ri = 0; ri < wh.rubTris.size(); ri++) {
        const Tri& tr = c.tris[wh.rubTris[ri]];
        static const double S[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.5, 0}, {0, 0.5, 0.5}, {0.5, 0, 0.5}, {1.0 / 3, 1.0 / 3, 1.0 / 3}};
        for (int si = 0; si < 7; si++) {
            V3 p; for (int k = 0; k < 3; k++) p += c.nodes[tr.v[k]].p * S[si][k];
            V3 d = p - ctr; double da = dot(d, ax), dr = len(d - ax * da);
            double pr = wh.R - dr - wh.rubAllow[ri * 7 + si], pa = wh.width / 2 - std::fabs(da) - wh.rubAllow[ri * 7 + si];
            if (std::fabs(da) < wh.width / 2 && pr > 0 && pa > 0) {
                V3 l = p - fr.com;
                printf("  tri %d sample %d at (%.2f %.2f %.2f) allow %.3f penR %.3f penA %.3f da %.3f\n", wh.rubTris[ri], si, dot(l, fr.fwd), dot(l, fr.up), -dot(l, fr.left), wh.rubAllow[ri * 7 + si], pr, pa, da);
            }
        }
    }
}
