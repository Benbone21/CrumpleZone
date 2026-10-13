// Substep trace of one wheel around the first touchdown after a vehicle is added.
#define private public
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int vi = argc > 1 ? std::atoi(argv[1]) : 4;
    const int wi = argc > 2 ? std::atoi(argv[2]) : 0;
    Sim sim;
    sim.world = World(MAP_PROVING_GROUND);
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -100, -50, 0);
    Wheel& w = c.wheels[wi];
    bool touched = false;
    int after = 0;
    for (int s = 0; s < 4000 && after < 60; s++) {
        sim.advance(sim.h, 1);
        if (w.load > 0) touched = true;
        if (!touched) continue;
        after++;
        const Frame f = c.frame();
        const V3 vc = (c.nodes[w.inner].v + c.nodes[w.outer].v) * 0.5;
        const V3 axis = norm(c.nodes[w.outer].p - c.nodes[w.inner].p);
        std::printf("s %d om %+.2f side %+.2f tw %+.5f Ts %+.0f sl %.0f ld %.0f gs %+.2f hubFwd %+.2f hubUp %+.2f bodyFwd %+.2f axis (%.2f %.2f %.2f) sx %+.2f\n", s, w.omega, w.sideOmega, w.twist, w.shaftTorque,
                    (double)w.shaftLoad, w.load, w.groundSpeed, dot(vc, f.fwd), vc.y, dot(f.vel, f.fwd), axis.x, axis.y, axis.z, w.sx);
    }
}
