// damage/rollover: the loaded 4Runner's fishhook, traced.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    Sim sim; Ctx ctx(sim);
    static VehicleSpec v = vehicleCatalog()[3];
    v.items.push_back({"roof rack load", 350, -0.6, 2.05, 0});
    v.tireGrip = 1.1;
    v.lateralGrip = 1.0;
    if (argc > 2) v.frameStiffness = atof(argv[2]);
    if (argc > 3) v.railYield = atof(argv[3]);
    Car& c = ctx.add(v, -120, -20 + (argc > 1 ? atof(argv[1]) : 0), 0);
    ctx.run(1.0);
    ctx.launch(c, 80 / 3.6);
    double t0 = ctx.t;
    for (int f = 0; f < 300; f++) {
        double s = ctx.t - t0;
        c.steer = s < 0.5 ? 0.3 : -0.5;
        c.throttle = 0;
        ctx.step();
        if (f % 10 == 0) {
            Frame fr = c.frame();
            printf("t %.2f up.y %.2f v %.1f damage %.2f%% loads", s, fr.up.y, len(fr.vel) * 3.6, c.damage * 100);
            for (auto& w : c.wheels) printf(" %5.0f", w.load);
            printf("\n");
        }
    }
    const char* tn[] = {"BODY","CABIN","ARM","SPRING","KNUCKLE","FRAME","MOUNT","ENGINE","TIEROD","SHAFT","PANEL","BUMPSTOP"};
    double mx[12] = {0}; int cnt[12] = {0}, all[12] = {0};
    for (const Beam& b : c.beams) { all[b.type]++; double d = std::fabs(b.L0 - b.Lorig); if (d > 0.001) cnt[b.type]++; mx[b.type] = std::max(mx[b.type], d); }
    for (int t = 0; t < 12; t++) printf("   %s: %d of %d set, max %.1f mm\n", tn[t], cnt[t], all[t], mx[t]*1000);
}
