#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
int main() {
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[2], -100, 125.2, 0);
    const auto& modes = driveModes(c.spec->transfer);
    for (size_t m = 0; m < modes.size(); m++) if (!modes[m].front) c.setDriveMode((int)m);
    ctx.run(1.0);
    int n = 0;
    ctx.run(4.0, [&] {
        c.throttle = 1; c.steer = 0;
        if (n++ % 6 == 0) {
            double v = c.frame().vel.x;
            printf("t=%.2f v=%5.1f g=%d rpm=%5.0f tc=%.2f |", ctx.t, v * 3.6, c.gear, c.rpm, c.tcCut);
            for (auto& w : c.wheels) if (!w.front) printf(" slide %5.2f brk %3.0f load %5.0f |", w.omega * w.R - w.groundSpeed, w.tcBrake * 100, w.load);
            printf("\n");
        }
    });
}
