// Which faces does a tire rub at speed? argv[1] vehicle, argv[2] seconds
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 4;
    double secs = argc > 2 ? atof(argv[2]) : 5;
    Sim sim; Ctx ctx(sim);
    sim.world = World(MAP_FREEWAY);
    Car& c = ctx.add(vehicleCatalog()[vi], sim.world.spawn.x, 6.9, 0);
    sim.player = 0;
    ctx.run(1.0);
    ctx.launch(c, 120 / 3.6);
    double steerI = 0;
    std::map<int, int> hits;
    int fr = 0;
    ctx.run(secs, [&] {
        c.throttle = 1; c.brake = 0;
        Frame f = c.frame();
        const double v = std::max(5.0, len(f.vel));
        const double e = f.com.z - 6.9;
        steerI = clampd(steerI + 0.08 * e * (1.0 / 60), -1.0, 1.0);
        const double pull = clampd(0.25 * e + steerI + 2.5 * f.vel.z, -3.0, 3.0);
        c.steer = clampd(c.wheelbase * pull / (v * v), -0.1, 0.1);
        fr++;
        for (size_t wi = 0; wi < c.wheels.size(); wi++) {
            const Wheel& w = c.wheels[wi];
            if (w.rub <= 0) continue;
            // hub in the car's frame vs rest
            const V3 hub = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - f.com;
            const V3 rest = (c.restLocal[w.inner] + c.restLocal[w.outer]) * 0.5;
            if (fr % 6 == 0)
                std::printf("t %.2f wheel %zu rub %.0f N torque %.0f hub local (%.3f %.3f %.3f) rest (%.3f %.3f %.3f)\n", ctx.t, wi, w.rub, w.rubTorque, dot(hub, f.fwd), dot(hub, f.up), -dot(hub, f.left), rest.x, rest.y, rest.z);
            const Node& a = c.nodes[w.inner];
            const Node& b = c.nodes[w.outer];
            const V3 ctr = (a.p + b.p) * 0.5, ax = norm(b.p - a.p);
            for (size_t ri = 0; ri < w.rubFaces.size(); ri++)
                for (int k = 0; k < 3; k++) {
                    const V3 d = c.nodes[w.rubFaces[ri][k]].p - ctr;
                    const double da = dot(d, ax);
                    if (std::fabs(da) < w.width * 0.5 && len(d - ax * da) < w.R) hits[w.rubFaces[ri][k]]++;
                }
        }
    });
    for (auto& h : hits) {
        const V3& r = c.restLocal[h.first];
        std::printf("node %d rest (%.3f %.3f %.3f) inside tire %d frames\n", h.first, r.x, r.y, r.z, h.second);
    }
}
