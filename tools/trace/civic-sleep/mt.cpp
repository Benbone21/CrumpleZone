#define private public
#include "../../../tests/harness.h"
#include "../../../src/mods.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
static double kmh(Car& c) { return len(c.frame().vel) * 3.6; }
static void hold(Car& c, double z) {
    Frame f = c.frame();
    double look = 6 + 0.5 * len(f.vel);
    V3 to = V3(f.com.x + look, f.com.y, z) - f.com;
    c.steer = clampd(std::atan2(dot(to, f.left), dot(to, f.fwd)), -0.4, 0.4);
}
int main(int argc, char** argv) {
    int slot = argc > 1 ? atoi(argv[1]) : 0, opt = argc > 2 ? atoi(argv[2]) : 2;
    int solo = argc > 3 ? atoi(argv[3]) : 0;
    int every = argc > 4 ? atoi(argv[4]) : 30;
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    ModSet ms;
    ms.pick[slot] = opt;
    const VehicleSpec& stock = vehicleCatalog()[0];
    const VehicleSpec& spec = tunedSpec(0, ms);
    Car* cp = nullptr;
    Car* sp = nullptr;
    if (solo != 2) cp = &ctx.add(spec, -120, -50, 0);
    if (solo != 1) sp = &ctx.add(stock, -120, -42, 0);
    ctx.run(2.5);
    double t0 = ctx.t;
    int fr = 0;
    ctx.run(20, [&] {
        for (Car* c : {cp, sp})
            if (c) c->throttle = 1, c->handbrake = 0;
        if (cp) hold(*cp, -50);
        if (sp) hold(*sp, -42);
        if (fr++ % every == 0) {
            for (Car* c : {cp, sp})
                if (c) {
                    Frame f = c->frame();
                    printf("%s t=%.2f x=%.1f z=%.2f v=%.1f up=%.3f fwdy=%.3f steer=%.2f gear=%d dmg=%.3f L=", c == cp ? "MOD" : "STK",
                           ctx.t - t0, f.com.x, f.com.z, kmh(*c), f.up.y, f.fwd.y, c->steer, c->gear, c->damage * 100);
                    for (auto& w : c->wheels) printf("%.0f/%.2f ", w.load, w.sx);
                    printf("\n");
                }
        }
    });
}
