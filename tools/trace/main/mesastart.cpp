#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include "../../../src/autopilot.h"
#include <cstdio>
V3 frontierFeature(const char*);
int main() {
    Sim sim; Ctx ctx(sim);
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 foot = frontierFeature("mesaclimb") + V3(10, 0, 0), top = frontierFeature("mesa") + V3(25, 0, 0);
    Car& c = ctx.add(vehicleCatalog()[2], foot.x, foot.z, 180);
    int n = 0;
    auto pr = [&] {
        printf("t=%.3f v=%.2f g=%d low=%d mode=%d rpm=%4.0f thr=%.2f brk=%.2f hb=%.1f |", ctx.t, len(c.frame().vel), c.gear, c.lowRange, c.driveMode, c.rpm, c.throttle, c.brake, c.handbrake);
        for (auto& w : c.wheels) printf(" w%5.1f s%6.1f T%6.0f L%5.0f%s", w.omega, w.sideOmega, w.shaftTorque, w.load, w.shaftBroken ? "X" : "");
        printf("\n");
    };
    ctx.run(1.0, [&] { if (n++ % 6 == 0) pr(); });
    pilotStart(c, ctx.sim.world, top, "THERE", 15 / 3.6, PILOT_ANYWHERE);
    ctx.run(2.0, [&] { ctx.sim.pilotStep(0, Ctx::kFrame); pr(); });
}
