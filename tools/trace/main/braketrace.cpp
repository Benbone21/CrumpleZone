#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    ctx.run(1.0);
    ctx.launch(c, 100 / 3.6);
    int n = 0;
    ctx.run(3.0, [&] {
        c.throttle = 0; c.brake = 1;
        if (n++ % 6) return;
        Frame f = c.frame();
        printf("t=%.2f v=%5.1f yaw %+.2f esc %.2f yr %+.3f pitch %+.1f |", ctx.t, len(f.vel) * 3.6, std::atan2(-f.fwd.z, f.fwd.x) * 57.3, c.escLevel, c.escYaw, std::asin(f.fwd.y) * 57.3);
        for (auto& w : c.wheels) {
            V3 ax = norm(c.nodes[w.outer].p - c.nodes[w.inner].p); if (!w.left) ax = -ax;
            double toe = std::asin(std::max(-1.0, std::min(1.0, dot(ax, f.fwd)))) * 57.3;  // + = rotated toward the front
            const Beam& sp = c.beams[w.spring];
            double Ls = len(c.nodes[sp.b].p - c.nodes[sp.a].p);
            printf(" L%5.0f sx%+.2f abs%.2f toe%+.2f dr%+.3f", w.load, w.sx, w.abs, toe, Ls - sp.maxL);
        }
        printf("\n");
    });
}
