// Per vehicle and suspension mod: the half shaft's damper against the side gear's inertia, and the
// explicit (semi-implicit Euler) stability measure 2 h c / Is + h^2 k / Is (must stay under 4).
#define private public
#include "../../../tests/harness.h"
#include "../../../src/mods.h"
#include <cmath>
#include <cstdio>

int main() {
    Sim sim;
    sim.world = World(MAP_PROVING_GROUND);
    sim.setupEmpty();
    Ctx ctx(sim);
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        for (int o = 0; o < (int)modOptions(v, MOD_SUSPENSION).size(); o++) {
            ModSet m;
            m.pick[MOD_SUSPENSION] = o;
            const VehicleSpec& vs = tunedSpec(v, m);
            sim.cars.clear();
            Car& c = ctx.add(vs, -100, -50, 0);
            const double k = c.shaftStiffness(), h = sim.h;
            for (const Wheel& w : c.wheels) {
                if (!w.driven || !w.left || !w.front) continue;
                const double cd = std::sqrt(k * w.I);
                std::printf("%-18s susp %d  I %.2f k %.0f c %.0f (cap %.0f)  measure %.2f\n", vs.name, o, w.I, k, cd, 1.5 * kSideGearI / h, 2 * h * cd / kSideGearI + h * h * k / kSideGearI);
            }
            if (c.wheels.size() && !c.wheels[0].driven)
                for (const Wheel& w : c.wheels)
                    if (w.driven && w.left) std::printf("%-18s susp %d  rear I %.2f c %.0f\n", vs.name, o, w.I, std::sqrt(k * w.I));
        }
}
