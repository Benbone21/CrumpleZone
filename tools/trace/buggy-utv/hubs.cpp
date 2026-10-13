// Where each hub sits in the body's frame vs its design position: at rest, then flat out.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void show(Car& c, const char* tag) {
    // body frame from the body nodes: centroid now vs rest, axes from chassis.rot
    V3 cn, cr;
    for (int i = 0; i < c.bodyNodes; i++) cn += c.nodes[i].p, cr += c.restLocal[i];
    cn = cn / c.bodyNodes, cr = cr / c.bodyNodes;
    Frame f = c.frame();
    std::printf("%s:", tag);
    for (const Wheel& w : c.wheels) {
        const V3 d = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - cn;
        const V3 l(dot(d, f.fwd), dot(d, f.up), -dot(d, f.left));
        const V3 r = (c.restLocal[w.inner] + c.restLocal[w.outer]) * 0.5 - cr;
        const Beam& sp = c.beams[w.spring];
        const double comp = w.springRest - len(c.nodes[sp.b].p - c.nodes[sp.a].p);
        std::printf("  [dx %+.3f dy %+.3f dz %+.3f coil %+.3f wheel %+.3f L %.0f]", l.x - r.x, l.y - r.y, l.z - r.z, comp, comp / w.motionRatio, w.load);
    }
    std::printf("\n");
}
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    show(c, "spawn");
    ctx.run(2.0);
    show(c, "rest ");
    double v0 = speedKmh(c);
    for (int k = 0; k < 6; k++) {
        v0 = speedKmh(c);
        ctx.run(0.5, [&] { c.throttle = argc > 2 ? atof(argv[2]) : 1; c.steer = 0; });
        std::printf("accel %.2f g  ", (speedKmh(c) - v0) / 3.6 / 0.5 / 9.81);
        char t[32];
        std::snprintf(t, sizeof t, "thr %.1fkmh", speedKmh(c));
        show(c, t);
    }
}
