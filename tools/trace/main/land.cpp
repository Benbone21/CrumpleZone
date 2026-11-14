// dynamics/landing-settle replica: vehicle argv[1] dropped argv[2] m flat (default 1.0). Per wheel:
// its static load, the highest its tire comes off the ground after the squat and when; the
// body's squat and rebound; the sprung mass on each axle against what its springs were sized for.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    const int vi = argc > 1 ? atoi(argv[1]) : 6;
    const double h = argc > 2 ? atof(argv[2]) : 1.0;
    Sim sim;
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, 0, 0);
    ctx.run(2.0, [&] { c.handbrake = 1; });
    double rest[4], lift[4] = {0, 0, 0, 0}, when[4] = {0, 0, 0, 0}, total = 0, front = 0;
    for (int i = 0; i < 4; i++) rest[i] = c.wheels[i].load, total += rest[i], front += c.wheels[i].front ? rest[i] : 0;
    for (Node& n : c.nodes) n.p.y += h, n.v = V3();
    c.wake();
    const double t0 = ctx.t;
    double yLow = 1e9, tLow = 0, yMax = -1e9;
    ctx.run(5.0, [&] {
        c.handbrake = 1;
        const double y = c.frame().com.y, t = ctx.t - t0;
        if (y < yLow) yLow = y, tLow = t, yMax = -1e9;
        yMax = std::max(yMax, y);
        if (t > 0.3 && t >= tLow)
            for (int i = 0; i < 4; i++) {
                const Wheel& w = c.wheels[i];
                const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5;
                const double l = hub.y - w.R - sim.world.height(hub.x, hub.z);
                if (l > lift[i]) lift[i] = l, when[i] = t;
            }
    });
    const double yEq = c.frame().com.y;
    std::printf("%s dropped %.1f m: %.0f kg, %.1f %% front; squat %.0f mm at %.2f s, rebound %.0f mm over rest\n", c.spec->name, h, c.mass, front / total * 100, (yEq - yLow) * 1000, tLow, (yMax - yEq) * 1000);
    for (int i = 0; i < 4; i++) {
        const Wheel& w = c.wheels[i];
        const Beam& sp = c.beams[w.spring];
        std::printf("  %s %s: static %.0f N, tire up to %.1f mm at %.2f s; spring %.0f N/m, damper %.0f N s/m, motion ratio %.2f\n", w.front ? "front" : "rear ", w.left ? "left " : "right", rest[i], lift[i] * 1000, when[i], sp.k, sp.c,
                    w.motionRatio);
    }
}
