#define private public
#include "../../../tests/harness.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
// usage: brake VEH [opts...]   opts: zero (items z=0), mirror, noesc, noabs, mode=N, neutral, verbose, nobrake
static double ang(const V3& a, const V3& b, const V3& up) { return std::atan2(dot(cross(a, b), up), dot(a, b)) * 180 / M_PI; }
int main(int argc, char** argv) {
    int vi = argc > 1 ? atoi(argv[1]) : 3;
    VehicleSpec spec = vehicleCatalog()[vi];
    bool noesc = false, verbose = false, neutral = false, nobrake = false;
    int mode = -1;
    double kick = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "zero")) for (auto& it : spec.items) it.z = 0;
        if (!strcmp(argv[i], "mirror")) for (auto& it : spec.items) it.z = -it.z;
        if (!strcmp(argv[i], "noesc")) noesc = true;
        if (!strcmp(argv[i], "noabs")) spec.absEnabled = false;
        if (!strncmp(argv[i], "mode=", 5)) mode = atoi(argv[i] + 5);
        if (!strcmp(argv[i], "neutral")) neutral = true;
        if (!strcmp(argv[i], "verbose")) verbose = true;
        if (!strcmp(argv[i], "nobrake")) nobrake = true;
        if (!strncmp(argv[i], "take=", 5)) {  // take=VEH:field  copy one field from another vehicle
            int src = argv[i][5] - '0';
            const VehicleSpec& o = vehicleCatalog()[src];
            std::string f = argv[i] + 7;
#define TK(x) if (f == #x) spec.x = o.x;
            TK(wheelbase) TK(trackFront) TK(trackRear) TK(wheelRadius) TK(wheelWidth) TK(wheelMass) TK(hubMass) TK(axleMass)
            TK(tireGrip) TK(lateralGrip) TK(items) TK(engineMass) TK(engineY) TK(engineX) TK(gearboxMass) TK(structureMass)
            TK(panelMass) TK(frameMass) TK(rideFreqFront) TK(rideFreqRear) TK(arbFront) TK(arbRear) TK(tirePsi)
            TK(frameStiffness) TK(railSpacing) TK(bumpRear) TK(droopRear) TK(bumpFront) TK(droopFront) TK(brakeFront) TK(brakeRear)
            if (f == "layerY") spec.layerY = o.layerY;
        }
        double v;
        if (sscanf(argv[i], "kick=%lf", &v) == 1) kick = v;
        if (sscanf(argv[i], "bf=%lf", &v) == 1) spec.brakeFront = v;
        if (sscanf(argv[i], "br=%lf", &v) == 1) spec.brakeRear = v;
        if (sscanf(argv[i], "fs=%lf", &v) == 1) spec.frameStiffness = v;
        if (sscanf(argv[i], "arbf=%lf", &v) == 1) spec.arbFront = v;
        if (sscanf(argv[i], "arbr=%lf", &v) == 1) spec.arbRear = v;
        if (sscanf(argv[i], "rfr=%lf", &v) == 1) spec.rideFreqRear = v;
        if (sscanf(argv[i], "rff=%lf", &v) == 1) spec.rideFreqFront = v;
        if (sscanf(argv[i], "lat=%lf", &v) == 1) spec.lateralGrip = v;
        if (sscanf(argv[i], "damp=%lf", &v) == 1) spec.dampingRatio = v;
        if (sscanf(argv[i], "bs=%lf", &v) == 1) spec.bodyStiffness = v;
    }
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    Car& c = ctx.add(spec, -120, -50, 0);
    ctx.focus(c);
    if (noesc) c.tractionControl = false;
    if (mode >= 0) c.driveMode = mode;
    ctx.run(1.0);
    ctx.launch(c, 100 / 3.6);
    ctx.run(0.2, [&] {
        Frame f = c.frame();
        double tx = f.com.x + 6 + 0.5 * len(f.vel);
        V3 to = V3(tx, f.com.y, -50) - f.com;
        c.steer = clampd(std::atan2(dot(to, f.left), dot(to, f.fwd)), -0.4, 0.4);
    });
    if (kick != 0) {
        V3 com = c.frame().com, w(0, kick / 57.3, 0);
        for (Node& n : c.nodes) n.v += cross(w, n.p - com);
    }
    double t0 = ctx.t, x0 = c.frame().com.x, v0 = speedKmh(c);
    int fr = 0;
    auto hubAxis = [&](int wi) {
        const Wheel& w = c.wheels[wi];
        V3 a = norm(c.nodes[w.outer].p - c.nodes[w.inner].p);
        return w.left ? a : -a;
    };
    ctx.runUntil(8.0, [&] { return speedKmh(c) < 0.5; }, [&] {
        c.throttle = 0;
        c.brake = nobrake ? 0 : 1;
        c.steer = 0;
        if (neutral) c.gear = 0;
        if (++fr % (verbose ? 3 : 6) == 0) {
            Frame f = c.frame();
            double rs = 0, fs = 0;
            for (int i = 0; i < (int)c.wheels.size(); i++) {
                double a = ang(f.left, hubAxis(i), f.up);
                if (c.wheels[i].front) fs += a / 2;
                else rs += a / 2;
            }
            std::printf("t %.2f v %5.1f hd %6.2f z %6.3f yaw %6.2f esc %.2f | ", ctx.t - t0, speedKmh(c), headingDeg(c), f.com.z + 50, c.escYaw * 57.3, c.escLevel);
            for (const Wheel& w : c.wheels) std::printf("%s%s L%5.0f sx%5.2f a%4.2f | ", w.front ? "F" : "R", w.left ? "l" : "r", w.load, w.sx, w.abs);
            std::printf("fs %5.2f rs %5.2f", fs, rs);
            if (verbose) {
                double roll = std::asin(f.left.y) * 57.3, pitch = std::asin(f.fwd.y) * 57.3;
                std::printf(" roll %5.2f pitch %5.2f toe", roll, pitch);
                for (int i = 0; i < (int)c.wheels.size(); i++) std::printf(" %5.2f", ang(f.left, hubAxis(i), f.up));
                // housing yaw from the two lower-link mounts and the two spring seats
                {
                    const Wheel& l = c.wheels[2]; const Wheel& r = c.wheels[3];
                    V3 lm = norm(c.nodes[l.lower].p - c.nodes[r.lower].p), sm = norm(c.nodes[l.top].p - c.nodes[r.top].p);
                    V3 hm = norm(c.nodes[l.inner].p - c.nodes[r.inner].p);
                    std::printf(" hsg low %5.2f seat %5.2f hubin %5.2f", ang(f.left, lm, f.up), ang(f.left, sm, f.up), ang(f.left, hm, f.up));
                }
                std::printf(" links");
                for (const SuspLink& l : c.links) {
                    if (l.kind != LINK_TRAILING && l.kind != LINK_PANHARD) continue;
                    const Beam& b = c.beams[l.beam];
                    V3 d = c.nodes[b.b].p - c.nodes[b.a].p;
                    std::printf(" [%d %c dy%5.3f dx%5.3f]", l.wheel, l.kind == LINK_PANHARD ? 'P' : 'T', dot(d, f.up), dot(d, f.fwd));
                }
            }
            std::printf("\n");
        }
    });
    std::printf("END heading %.2f drift %.2f dist %.1f\n", headingDeg(c), c.frame().com.z + 50, (c.frame().com.x - x0) * (100 / v0) * (100 / v0));
}
