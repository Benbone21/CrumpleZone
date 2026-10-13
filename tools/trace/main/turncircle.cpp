#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    double lock = argc > 1 ? atof(argv[1]) : 0.6;
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        ctx.run(1.0);
        // outer front wheel when turning left = front right
        int wo = -1;
        for (size_t i = 0; i < c.wheels.size(); i++) if (c.wheels[i].front && !c.wheels[i].left) wo = (int)i;
        double xmin = 1e9, xmax = -1e9, zmin = 1e9, zmax = -1e9;
        double t0 = ctx.t;
        double maxBody = 0;
        ctx.run(14, [&] {
            c.steer = lock;
            double vf = dot(c.frame().vel, c.frame().fwd);
            c.throttle = clampd(0.25 + (1.5 - vf) * 0.5, 0, 1);
            c.brake = 0;
            if (ctx.t - t0 > 4) {
                const Wheel& w = c.wheels[wo];
                V3 p = c.nodes[w.outer].p;  // outer face of the tire ~ outer hub node
                xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x), zmin = std::min(zmin, p.z), zmax = std::max(zmax, p.z);
            }
        });
        double d = 0.5 * ((xmax - xmin) + (zmax - zmin));
        // left front actual angle
        printf("%-18s wheelbase %.2f  turning circle (outer front hub path) %.2f m\n", vehicleCatalog()[v].name, c.wheelbase, d + c.wheels[wo].width * 0.0);
    }
}
