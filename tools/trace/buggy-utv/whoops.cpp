// Whoops lane trace: argv[1] vehicle, argv[2] target km/h
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include "../../../src/world.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static int run1(int argc, char** argv, double target);
int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[2]) == "all") { for (double t : {50.0, 53.0, 55.0, 57.0, 60.0}) run1(argc, argv, t); return 0; }
    return run1(argc, argv, argc > 2 ? atof(argv[2]) : 55);
}
static int run1(int argc, char** argv, double target) {
    int vi = argc > 1 ? atoi(argv[1]) : 5;
    Sim sim; Ctx ctx(sim);
    sim.world = World(MAP_MOUNTAIN);
    const V3 f = mountainFeature("whoops");
    VehicleSpec vs = vehicleCatalog()[vi];
    for (int a = 3; a + 1 < argc; a += 2) {
        std::string k = argv[a]; double v = atof(argv[a + 1]);
        if (k == "track") vs.trackFront = vs.trackRear = v;
        else if (k == "bumpF") vs.bumpFront = v;
        else if (k == "droopF") vs.droopFront = v;
        else if (k == "bumpR") vs.bumpRear = v;
        else if (k == "droopR") vs.droopRear = v;
        else if (k == "damp") vs.dampingRatio = v;
        else if (k == "freqF") vs.rideFreqFront = v;
        else if (k == "freqR") vs.rideFreqRear = v;
        else if (k == "armL") vs.armLower = v;
        else if (k == "armU") vs.armUpper = v;
        else if (k == "width") vs.width = v;
        else if (k == "raise") raiseBody(vs, v);
        else std::printf("unknown %s\n", k.c_str());
    }
    Car& c = ctx.add(vs, f.x, f.z - 45, -90);
    ctx.run(1.0);
    double tIn = -1, tOut = -1, minUp = 1;
    int fr = 0;
    ctx.runUntil(40, [&] { return tOut > 0; }, [&] {
        Frame fm = c.frame();
        V3 d = V3(f.x, fm.com.y, fm.com.z + 8) - fm.com;
        c.steer = clampd(std::atan2(dot(d, fm.left), std::max(dot(d, fm.fwd), 0.5)), -0.3, 0.3);
        const double v = len(fm.vel) * 3.6;
        c.throttle = clampd(0.3 + (target - v) * 0.2, 0, 1);
        if (getenv("THR")) c.throttle = atof(getenv("THR"));
        if (getenv("AIRLIFT")) { bool air = true; for (const Wheel& w : c.wheels) air &= w.load <= 0; if (air) c.throttle = 0; }
        c.brake = v > target + 8 ? 0.3 : 0;
        if (tIn < 0 && fm.com.z > f.z) tIn = ctx.t;
        if (tIn > 0 && tOut < 0 && fm.com.z > f.z + 130) tOut = ctx.t;
        if (tIn > 0) minUp = std::min(minUp, fm.up.y);
        if (++fr % 6 || tIn < 0 || !getenv("TRACE")) return;
        std::printf("t %.2f z %.1f v %.1f up %.2f pitch %+.1f roll %+.1f thr %.2f br %.1f g %d rpm %.0f mode %d |", ctx.t - tIn, fm.com.z - f.z, v, fm.up.y, std::asin(fm.fwd.y) * 57.3, std::asin(fm.left.y) * 57.3, c.throttle, c.brake, c.gear, c.rpm, c.driveMode);
        for (const Wheel& w : c.wheels) {
            const Beam& sp = c.beams[w.spring];
            const double L = len(c.nodes[sp.b].p - c.nodes[sp.a].p);
            std::printf(" L%5.0f c%+.2f w%4.1f", w.load, (w.springRest - L) / w.motionRatio, w.omega * w.R * 3.6);
        }
        std::printf("\n");
    });
    std::printf("%.0f: time %.1f minUp %.2f damage %.2f%%\n", target, tOut > 0 ? tOut - tIn : 99, minUp, c.damage * 100);
    return 0;
}
