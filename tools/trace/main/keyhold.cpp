// handling/key-hold-130 traced: sideslip, steer, each wheel's load, slip angle and toe (rear axle steer).
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 4;
    double kmh = argc > 2 ? atof(argv[2]) : 130, hold = argc > 3 ? atof(argv[3]) : 1.5;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -125, -50, 0);
    ctx.sim.player = 0;
    ctx.run(1.0);
    ctx.launch(c, kmh / 3.6);
    const double t0 = ctx.t;
    int f = 0;
    ctx.run(3.0, [&] {
        const double t = ctx.t - t0;
        const Frame fr = c.frame();
        const bool up = dot(fr.vel, fr.fwd) < kmh / 3.6;
        ctx.sim.drive(up, false, t > 0.3 && t < 0.3 + hold, false, false, Ctx::kFrame);
        if (f++ % 6 == 0) {
            double slip = std::atan2(dot(fr.vel, fr.left), std::max(0.1, dot(fr.vel, fr.fwd))) * 180 / M_PI;
            printf("t %.2f v %3.0f slip %5.1f steer %5.1f roll %4.1f |", t, len(fr.vel) * 3.6, slip, c.steer * 180 / M_PI, std::asin(fr.left.y) * 180 / M_PI);
            for (auto& w : c.wheels) {
                V3 ax = norm(c.nodes[w.outer].p - c.nodes[w.inner].p);
                printf(" %5.0fN a%5.1f t%5.2f", w.load, w.slip * 180 / M_PI, std::asin(dot(ax, fr.fwd)) * 180 / M_PI);
            }
            printf("\n");
        }
    });
}
