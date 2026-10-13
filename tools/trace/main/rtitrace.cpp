// RTI ramp: which wheel lifts, and where each spring is in its travel when it does.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
static void crawlEast(Car& c, double z, double speed) {
    Frame fr = c.frame();
    V3 d = V3(fr.com.x + 6, fr.com.y, z) - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    bool disc = argc > 2 && atoi(argv[2]);
    Sim sim; Ctx ctx(sim);
    static VehicleSpec spec; spec = vehicleCatalog()[v];
    if (argc > 3) spec.frameStiffness = atof(argv[3]);
    World& w = ctx.sim.world;
    const double a = 20 * M_PI / 180, L = 4.0, z = 45;
    const double track = spec.trackFront > 0 ? spec.trackFront : spec.width;
    const double zl = z - track / 2;
    w.addRamp(V3(-70, 0, zl), V3(-70 + L * std::cos(a), L * std::sin(a), zl), 0.7, 1.2, V3(0.6, 0.6, 0.62));
    w.buildGrid();
    Car& c = ctx.add(spec, -70 - spec.wheelbase * 0.5 - 1.4, z, 0);
    if (argc > 4 && atoi(argv[4])) for (int bi : c.bodyMountBeams) c.beams[bi].k *= 0.25;
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.tractionControl = false;
    c.swayDisconnected = disc;
    const Wheel* fl = nullptr;
    for (const Wheel& wh : c.wheels) if (wh.front && wh.left) fl = &wh;
    double best = 0; int f = 0;
    auto spr = [&](const Wheel& wh) {
        for (size_t i = 0; i < c.beams.size(); i++) { const Beam& b = c.beams[i]; if (b.type == BEAM_SPRING && (b.a == wh.top || b.b == wh.top || b.a == wh.lower || b.b == wh.lower || b.a == wh.inner || b.b == wh.inner)) { double Ln = len(c.nodes[b.b].p - c.nodes[b.a].p); return std::make_pair(Ln - b.minL, b.maxL - Ln); } }
        return std::make_pair(-1.0, -1.0);
    };
    ctx.runUntil(30, [&] {
        for (const Wheel& wh : c.wheels) if (&wh != fl && wh.load < 30) return true;
        return false;
    }, [&] {
        crawlEast(c, z, 0.25);
        const double along = (fl->contact.x + 70) / std::cos(a);
        if (fl->load > 0 && along > 0 && along < L && fl->contact.y > along * std::sin(a) - 0.15) best = std::max(best, along);
        if (f++ % 60 == 0) {
            Frame fr = c.frame();
            printf("t %.1f along %.2f roll %.1f pitch %.1f loads", ctx.t, along, std::asin(fr.left.y) * 180 / M_PI, std::asin(fr.fwd.y) * 180 / M_PI);
            for (auto& wh : c.wheels) printf(" %5.0f", wh.load);
            printf("  spring (to bump, to droop) cm");
            for (auto& wh : c.wheels) { auto s = spr(wh); printf(" (%.1f,%.1f)", s.first * 100, s.second * 100); }
            printf("\n");
        }
    });
    printf("at lift: spring (to bump, to droop) cm");
    for (auto& wh : c.wheels) { auto s2 = spr(wh); printf(" (%.1f,%.1f)", s2.first * 100, s2.second * 100); }
    { Frame fr = c.frame(); printf(" roll %.1f\n", std::asin(fr.left.y) * 180 / M_PI); }
    for (const Axle& ax : c.axles) printf("axle %s arbK %.0f bar travel used\n", ax.front ? "front" : "rear", ax.arbK);
    {
        double worst = 0; V3 wl;
        for (int r = 0; r < 2; r++) { ChassisFrame& cf = c.railFrame[r]; for (size_t k = 0; k < cf.nodes.size(); k++) { (void)k; } }
        double fs = 0; for (const Beam& b : c.beams) if (b.type == BEAM_FRAME) fs = std::max(fs, std::fabs(b.L0 - b.Lorig));
        printf("damage %.3f%%, worst frame beam set %.1f mm\n", c.damage * 100, fs * 1000); (void)worst; (void)wl;
    }
    printf("RTI %.0f; lifted:", best / spec.wheelbase * 1000);
    for (const Wheel& wh : c.wheels) printf(" %s%s=%.0f", wh.front ? "F" : "R", wh.left ? "L" : "R", wh.load);
    printf("\n");
}
