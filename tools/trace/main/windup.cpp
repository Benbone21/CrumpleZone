#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    Sim sim; Ctx ctx(sim);
    VehicleSpec spec = vehicleCatalog()[2];
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.run(1.0);
    c.setDriveMode(2);
    printf("mode %s centerLocked %d\n", c.mode()->name, c.centerLocked());
    int n = 0;
    ctx.run(4, [&] {
        c.throttle = 0.5; c.steer = 0.3;
        if (n++ % 15) return;
        double f = 0, r = 0, fs = 0, rs = 0;
        for (auto& w : c.wheels) { (w.front ? f : r) += w.omega * 0.5; (w.front ? fs : rs) += w.sideOmega * 0.5; }
        printf("t=%.2f v=%.1f g=%d rpm=%.0f wheels f %.2f r %.2f  sides f %.2f r %.2f |", ctx.t, len(c.frame().vel), c.gear, c.rpm, f, r, fs, rs);
        for (auto& w : c.wheels) printf(" tw%+.3f T%+5.0f", w.twist, w.shaftTorque);
        printf("\n");
    });
}
