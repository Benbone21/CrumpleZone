#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include <cstdio>
#include <cstdlib>
V3 mountainFeature(const char*);
int main(int argc, char** argv) {
    double from = argc > 1 ? atof(argv[1]) : 0, to = argc > 2 ? atof(argv[2]) : 200;
    Sim sim; Ctx ctx(sim);
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 s0 = mountainFeature("spur"), pin = mountainFeature("pinnacle");
    const V3 out = norm(V3(pin.x - s0.x, 0, pin.z - s0.z));
    const V3 start = s0 - out * 1.5;
    static VehicleSpec spec = vehicleCatalog()[2];
    spec.lockerFront = true;
    Car& c = ctx.add(spec, start.x, start.z, std::atan2(-out.z, out.x) * 180 / M_PI);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(false, true);
    c.tractionControl = false;
    c.tirePsi = 18;
    c.swayDisconnected = true;
    int n = 0;
    ctx.run(to, [&] {
        Frame fr = c.frame();
        const V3 rel = fr.com - start;
        const double s = dot(rel, out);
        V3 target = start + out * (s + 4);
        target.y = fr.com.y;
        V3 d = target - fr.com;
        c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 2.0, -0.6, 0.6);
        double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (1.2 - v) * 0.7, 0, 0.6);
        c.brake = v > 1.9 ? 0.4 : 0;
        if (ctx.t < from || n++ % 30) return;
        printf("t=%.1f s=%.1f v=%.2f g=%d rpm=%.0f thr=%.2f pitch=%.0f |", ctx.t, s, v, c.gear, c.rpm, c.throttle, std::asin(fr.fwd.y) * 57.3);
        for (auto& w : c.wheels) printf(" L%5.0f sp%+.2f T%+5.0f%s", w.load, w.omega * w.rollR() - w.groundSpeed, w.shaftTorque, w.shaftBroken ? "X" : "");
        printf("\n");
    });
}
