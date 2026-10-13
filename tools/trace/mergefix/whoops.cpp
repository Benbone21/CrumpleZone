// The whoops run from tests/offroad.cpp for one vehicle, traced: speed, pitch, roll, wheel loads, ESC/TC.
#define private public
#include "../../../tests/harness.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int vi = argc > 1 ? std::atoi(argv[1]) : 5;
    const double target = argc > 2 ? std::atof(argv[2]) : 55;
    Sim sim;
    sim.world = World(MAP_MOUNTAIN);
    sim.setupEmpty();
    Ctx ctx(sim);
    const V3 f = mountainFeature("whoops");
    Car& c = ctx.add(vehicleCatalog()[vi], f.x, f.z - 45, -90);
    ctx.run(1.0);
    if (argc > 3) c.tractionControl = std::atoi(argv[3]) != 0;
    if (argc > 4 && std::atoi(argv[4])) for (Wheel& w : c.wheels) w.rubFaces.clear(), w.rubAllow.clear();
    int fr = 0;
    double tIn = -1;
    ctx.runUntil(30, [&] { return c.frame().com.z > f.z + 135; }, [&] {
        Frame fr0 = c.frame();
        V3 d = V3(f.x, fr0.com.y, fr0.com.z + 8) - fr0.com;
        c.steer = clampd(std::atan2(dot(d, fr0.left), std::max(dot(d, fr0.fwd), 0.5)), -0.3, 0.3);
        const double v = len(fr0.vel) * 3.6;
        c.throttle = clampd(0.3 + (target - v) * 0.2, 0, 1);
        c.brake = v > target + 8 ? 0.3 : 0;
        c.handbrake = 0;
        if (tIn < 0 && fr0.com.z > f.z) tIn = ctx.t;
        if (++fr % 6) return;
        std::printf("t %5.2f z %+6.1f h %.2f v %5.1f thr %.2f br %.2f pitch %+6.1f roll %+6.1f up %+.2f esc %.2f tc %.2f |", ctx.t, fr0.com.z - f.z, fr0.com.y - sim.world.height(fr0.com.x, fr0.com.z), v, c.throttle, c.brake,
                    std::asin(clampd(fr0.fwd.y, -1, 1)) * 180 / M_PI, std::asin(clampd(fr0.left.y, -1, 1)) * 180 / M_PI, fr0.up.y, c.escLevel, c.tcCut);
        for (const Wheel& w : c.wheels) std::printf(" %5.0f/%.0f", w.load, (double)w.rub);
        std::printf("\n");
    });
}
