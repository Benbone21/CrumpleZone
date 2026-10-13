#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    VehicleSpec spec = vehicleCatalog()[v];
    if (mode >= 1) for (MassItem& it : spec.items) it.z = 0;
    if (mode == 2) for (MassItem& it : spec.items) it.z = 0, spec.engineZ = 0;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(spec, -120, -50, 0);
    ctx.run(1.0);
    double l = 0, r = 0;
    for (auto& w : c.wheels) (w.left ? l : r) += w.load;
    ctx.launch(c, 100 / 3.6);
    double worst = 0;
    ctx.runUntil(8, [&] { return len(c.frame().vel) < 0.2; }, [&] {
        c.throttle = 0; c.brake = 1; c.steer = 0;
        Frame f = c.frame();
        worst = std::max(worst, std::fabs(std::atan2(-f.fwd.z, f.fwd.x) * 57.3));
    });
    Frame f = c.frame();
    printf("%s mode %d: static L %.0f R %.0f  final yaw %+.2f worst %.2f\n", spec.name, mode, l, r, std::atan2(-f.fwd.z, f.fwd.x) * 57.3, worst);
}
