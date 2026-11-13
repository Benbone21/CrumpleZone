// Axle hop under power: argv[1] vehicle, argv[2] throttle (0-1), argv[3] seconds. Full throttle from rest
// on the pad, the rear wheels' loads and hub heights sampled every physics step: the strongest
// 6-30 Hz line of the rear load (both sides together, and side against side) per half second.
#define private public
#define protected public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void line(const std::vector<double>& v, double hz, double& bestF, double& bestA) {
    const int n = (int)v.size();
    double mean = 0;
    for (double x : v) mean += x / n;
    bestF = bestA = 0;
    for (double f = 6; f <= 30; f += 0.25) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)), ph = 2 * M_PI * f * i / hz;
            re += (v[i] - mean) * w * std::cos(ph), im += (v[i] - mean) * w * std::sin(ph);
        }
        const double a = 4 * std::hypot(re, im) / n;
        if (a > bestA) bestA = a, bestF = f;
    }
}
int main(int argc, char** argv) {
    const int vi = argc > 1 ? atoi(argv[1]) : 6;
    const double thr = argc > 2 ? atof(argv[2]) : 1.0, secs = argc > 3 ? atof(argv[3]) : 6.0;
    Sim sim;
    Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[vi], -120, -50, 0);
    ctx.run(2.5, [&] { c.handbrake = 1; });
    c.handbrake = 0;
    std::vector<int> rear;
    for (size_t i = 0; i < c.wheels.size(); i++)
        if (!c.wheels[i].front) rear.push_back((int)i);
    const double hz = 1 / sim.h;
    const int win = (int)(hz / 2), perFrame = (int)(hz / 60);
    std::printf("%s throttle %.2f: static rear load %.0f N a side\n", c.spec->name, thr, (c.wheels[rear[0]].load + c.wheels[rear[1]].load) / 2);
    for (double t = 0; t < secs; t += 0.5) {
        std::vector<double> both, diff, hub, slip;
        double minLoad = 1e9, sx = 0;
        for (int i = 0; i < win; i++) {
            if (i % perFrame == 0) c.steer = 0, c.brake = 0, c.throttle = thr;
            sim.substep();
            const Wheel &a = c.wheels[rear[0]], &b = c.wheels[rear[1]];
            both.push_back((a.load + b.load) / 2), diff.push_back((a.load - b.load) / 2);
            const Frame f = c.frame();
            hub.push_back(dot((c.nodes[a.inner].p + c.nodes[b.inner].p) * 0.5 - f.com, f.up));
            minLoad = std::min(minLoad, (double)std::min(a.load, b.load));
            sx = std::max(sx, (double)std::max(std::fabs(a.sx), std::fabs(b.sx)));
        }
        double f1, a1, f2, a2, f3, a3;
        line(both, hz, f1, a1), line(diff, hz, f2, a2), line(hub, hz, f3, a3);
        std::printf("t %.1f-%.1f  %5.1f km/h gear %d  load together %5.0f N at %4.1f Hz, side-side %5.0f N at %4.1f Hz, hub %5.2f mm at %4.1f Hz, least load %5.0f N, worst slip %.2f\n", t, t + 0.5,
                    len(c.frame().vel) * 3.6, c.gear, a1, f1, a2, f2, a3 * 1000, f3, minLoad, sx);
    }
}
