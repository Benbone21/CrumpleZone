// Trace a full-throttle launch: gear, rpm, engine torque, tcCut, wheel omega*R vs speed.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/mods.h"
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int veh = argc > 1 ? atoi(argv[1]) : 2;
    int opt = argc > 2 ? atoi(argv[2]) : 0;
    int slot = argc > 3 ? atoi(argv[3]) : 0;
    ModSet ms; ms.pick[slot] = opt;
    const VehicleSpec& spec = tunedSpec(veh, ms);
    Sim sim;
    Ctx ctx(sim);
    Car& c = ctx.add(spec, -120, -50, 0);
    ctx.run(2.5);
    int n = 0;
    ctx.runUntil(12, [&] { return c.frame().vel.x * 3.6 > 100; }, [&] {
        c.throttle = 1;
        if (n++ % 6 == 0) {
            double v = c.frame().vel.x;
            printf("t=%.2f v=%5.1f g=%d rpm=%5.0f tq=%4.0f tc=%.2f boost=%.2f shift=%.2f |", ctx.t - 2.5, v * 3.6, c.gear, c.rpm,
                   c.engineTorque(c.rpm), c.tcCut, c.boost, c.shiftTimer);
            for (auto& w : c.wheels) printf(" %4.2f/%5.0f/%4.2f", w.omega * w.R / std::max(0.1, v), w.shaftTorque, w.twist);
            printf("\n");
        }
    });
}
