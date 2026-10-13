// ai/stuck-recovery traced.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main() {
    Sim sim; Ctx ctx(sim);
    const auto& road = ctx.sim.world.loopRoad;
    int idx = 300;
    V3 d = road[idx + 1] - road[idx];
    V3 t = norm(V3(d.x, 0, d.z));
    V3 lane = road[idx] + V3(-t.z, 0, t.x) * 2.6;
    double heading = std::atan2(-t.z, t.x) * 180 / M_PI;
    ctx.addProp(barrierSpec(), lane.x + t.x * 4, lane.z + t.z * 4, heading + 90);
    Car& c = ctx.add(vehicleCatalog()[0], lane.x - t.x * 0.5, lane.z - t.z * 0.5, heading - 25, true);
    c.aiDir = 1;
    for (int f = 0; f < 60 * 40; f++) {
        ctx.step();
        if (f % 60 == 0) {
            Frame fr = c.frame();
            printf("t %4.1f idx %d v %5.1f km/h fwd %+5.1f gear %d thr %.2f brk %.2f steer %+.2f stuck %.1f rev %.1f dmg %.2f\n", ctx.t, c.aiIndex, speedKmh(c), dot(fr.vel, fr.fwd) * 3.6, c.gear, c.throttle, c.brake, c.steer, c.aiStuckTime, c.aiReverseTime, c.damage * 100);
            for (auto& w : c.wheels) printf("     %s%s w %.1f ground %.1f load %.0f shaftBroken %d detached %d sx %.2f surf %d\n", w.front ? "F" : "R", w.left ? "L" : "R", w.omega, w.groundSpeed, w.load, (int)w.shaftBroken, (int)w.detached, w.sx, (int)w.surface);
        }
    }
}
