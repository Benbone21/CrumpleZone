// The first second after a vehicle is added: each wheel's spin, shaft wind-up and load per frame.
#define private public
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int vi = argc > 1 ? std::atoi(argv[1]) : 4;
    const double secs = argc > 2 ? std::atof(argv[2]) : 1.0;
    Sim sim;
    sim.world = World(MAP_PROVING_GROUND);
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, -50, 0);
    std::printf("after add: t %.3f\n", ctx.t);
    for (const Wheel& w : c.wheels)
        std::printf("  wheel front %d left %d driven %d I %.2f R %.3f om %.2f side %.2f twist %.4f ld %.0f\n", w.front, w.left, w.driven, w.I, w.R, w.omega, w.sideOmega, w.twist, w.load);
    ctx.run(secs, [&] {
        std::printf("t %.3f gear %d rpm %.0f diff %d/%d |", ctx.t, c.gear, c.rpm, (int)c.diffType(true), (int)c.diffType(false));
        for (const Wheel& w : c.wheels)
            std::printf(" [om %+.1f side %+.1f tw %+.4f Ts %+.0f ld %.0f sl %.0f rub %.0f rt %+.0f nf %zu br %d]", w.omega, w.sideOmega, w.twist, w.shaftTorque, w.load, (double)w.shaftLoad, (double)w.rub, w.rubTorque, w.rubFaces.size(), w.shaftBroken);
        std::printf("\n");
    });
}
