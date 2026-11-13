// tires/debead replica: the 4Runner (or argv[1]) at argv[2] psi flung into the test's turn; prints
// the peak bead load each wheel saw against the load that rolls it off (0.35 + psi / 20).
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    const int vi = argc > 1 ? atoi(argv[1]) : 3;
    const double psi = argc > 2 ? atof(argv[2]) : 8, steer = argc > 3 ? atof(argv[3]) : 0.5;
    Sim sim;
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -150, 40, 0);
    ctx.run(1.0);
    c.tirePsi = psi;
    ctx.runUntil(15, [&] { return dot(c.frame().vel, c.frame().fwd) > 12.5; }, [&] { c.throttle = 1, c.steer = 0; });
    double peak[4] = {0, 0, 0, 0}, latG = 0;
    V3 v0 = c.frame().vel;
    ctx.run(3.0, [&] {
        c.throttle = 0.3, c.steer = steer;
        for (size_t i = 0; i < c.wheels.size() && i < 4; i++) peak[i] = std::max(peak[i], (double)c.wheels[i].beadLoad);
        const Frame f = c.frame();
        latG = std::max(latG, std::fabs(dot(f.vel - v0, f.left)) * 60 / 9.81);
        v0 = f.vel;
    });
    int off = 0;
    for (const Wheel& wh : c.wheels) off += wh.debeaded;
    std::printf("%s %g psi steer %.2f: bead load peaks %.2f %.2f %.2f %.2f (rolls off past %.2f), peak lateral %.2f g, %d off, end %.0f km/h\n", c.spec->name, psi, steer, peak[0], peak[1], peak[2], peak[3], 0.35 + psi / 20, latG, off, len(c.frame().vel) * 3.6);
}
