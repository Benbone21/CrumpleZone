// Drop a vehicle 0.3 m: how the body, the frame and the rear axle move afterwards.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    double h = argc > 2 ? atof(argv[2]) : 0.3;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    ctx.run(2.0);
    for (Node& n : c.nodes) n.p.y += h;
    for (int f = 0; f < 60 * 8; f++) {
        ctx.step();
        if (f % 20 == 0) {
            Frame fr = c.frame();
            int fast = 0; double fv = 0;
            for (size_t i = 0; i < c.nodes.size(); i++) { double s = len(c.nodes[i].v - fr.vel); if (s > fv) fv = s, fast = (int)i; }
            printf("rub %4.0f %4.0f %4.0f %4.0f ", c.wheels[0].rub, c.wheels[1].rub, c.wheels[2].rub, c.wheels[3].rub);
            printf("t %.2f com y %.3f pitch %.2f roll %.2f loads %5.0f %5.0f %5.0f %5.0f | fastest rel node %d (%.2f %.2f %.2f) %s %.2f m/s dmg %.3f%%\n", ctx.t, fr.com.y, std::asin(fr.fwd.y) * 180 / M_PI, std::asin(fr.left.y) * 180 / M_PI,
                   c.wheels[0].load, c.wheels[1].load, c.wheels[2].load, c.wheels[3].load, fast, c.restLocal[fast].x, c.restLocal[fast].y, c.restLocal[fast].z,
                   fast < c.bodyNodes ? "body" : fast < c.frameNodes ? "frame" : "other", fv, c.damage * 100);
        }
    }
    for (const SuspLink& l : c.links) {
        const Beam& b = c.beams[l.beam];
        if (std::fabs(b.L0 - b.Lorig) > 0.0005 || b.broken) printf("link kind %d wheel %d set %.1f mm (L %.2f, yield %.3f) %s\n", (int)l.kind, l.wheel, (b.L0 - b.Lorig) * 1000, b.Lorig, b.yieldStrain, b.broken ? "BROKEN" : "");
    }
    Wheel& wh = c.wheels[3];
    const V3 a = c.nodes[wh.inner].p, b = c.nodes[wh.outer].p, ctr = (a + b) * 0.5, ax = norm(b - a);
    Frame fr = c.frame();
    for (size_t ri = 0; ri < wh.rubTris.size(); ri++) {
        const Tri& tr = c.tris[wh.rubTris[ri]];
        static const double S[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.5, 0}, {0, 0.5, 0.5}, {0.5, 0, 0.5}, {1.0 / 3, 1.0 / 3, 1.0 / 3}};
        for (int si = 0; si < 7; si++) {
            V3 p; for (int k = 0; k < 3; k++) p += c.nodes[tr.v[k]].p * S[si][k];
            V3 d = p - ctr; double da = dot(d, ax), dr = len(d - ax * da);
            double al = wh.rubAllow[ri * 7 + si];
            double pr = wh.R - dr - al, pa = wh.width / 2 - std::fabs(da) - al;
            if (std::fabs(da) < wh.width / 2 && pr > 0 && pa > 0) {
                V3 l = p - fr.com;
                printf("  tri %d (mat %d, nodes %d %d %d) sample %d at (%.2f %.2f %.2f) allow %.3f penR %.3f penA %.3f da %.3f\n", wh.rubTris[ri], tr.mat, tr.v[0], tr.v[1], tr.v[2], si, dot(l, fr.fwd), dot(l, fr.up), -dot(l, fr.left), al, pr, pa, da);
            }
        }
    }
}
