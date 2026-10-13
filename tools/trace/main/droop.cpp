#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 0;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], 0, -50, 0);
    ctx.run(1.0);
    std::vector<V3> rest;
    Frame f0 = c.frame();
    for (auto& w : c.wheels) rest.push_back((c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - f0.com);
    for (Node& n : c.nodes) { n.p.y += 20; n.v = V3(); }
    c.engineDead = true; c.rpm = 0; for (Wheel& w : c.wheels) { w.omega = 30; w.stickX = w.stickY = 0.01; w.groundSpeed = 50; w.abs = 0.1; }
    ctx.run(0.12, [&] {
        static int k = 0;
        
        Frame f = c.frame();
        printf("t=%.2f", ctx.t);
        for (size_t i = 0; i < c.wheels.size(); i++) {
            auto& w = c.wheels[i];
            V3 d = (c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - f.com - rest[i];
            printf(" | w%zu om %.2f rub %5.0f rt %.1f", i, w.omega, w.rub, w.rubTorque);
        }
        printf("\n");
    });
    printf("spec droop rear %.3f front %.3f rearSusp %d\n", vehicleCatalog()[v].droopRear, vehicleCatalog()[v].droopFront, vehicleCatalog()[v].rearSusp);
}
