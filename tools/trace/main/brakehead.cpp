// Full brakes from 100 km/h: heading and drift, plus each wheel's toe and brake torque.
#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    Sim sim; Ctx ctx(sim);
    static VehicleSpec spec; spec = vehicleCatalog()[v];
    if (argc > 2) spec.frameStiffness = atof(argv[2]);
    if (argc > 3) spec.railYield = atof(argv[3]);
    Car& c = ctx.add(spec, -120, -50, 0);
    ctx.run(1.0);
    ctx.launch(c, 100 / 3.6);
    int f = 0;
    ctx.runUntil(8.0, [&] { return speedKmh(c) < 0.5; }, [&] {
        c.throttle = 0; c.brake = 1; c.steer = 0;
        if (f++ % 15 == 0) {
            Frame fr = c.frame();
            printf("t %.2f v %5.1f head %6.2f z %6.2f loads", ctx.t, speedKmh(c), headingDeg(c), fr.com.z + 50);
            for (auto& w : c.wheels) printf(" %5.0f", w.load);
            printf(" toe(deg)");
            for (auto& w : c.wheels) { V3 ax = norm(c.nodes[w.outer].p - c.nodes[w.inner].p); printf(" %5.2f", std::asin(dot(ax, fr.fwd)) * 180 / M_PI); }
            {
                // Rear axle: lateral offset and height of its centre in the body frame, vs rest (mm).
                const Wheel& l = c.wheels[2]; const Wheel& r = c.wheels[3];
                V3 ac = (c.nodes[l.inner].p + c.nodes[r.inner].p) * 0.5 - fr.com;
                V3 ar = (c.restLocal[l.inner] + c.restLocal[r.inner]) * 0.5;
                static V3 a0; static bool first = true;
                V3 now(dot(ac, fr.fwd), dot(ac, fr.up), -dot(ac, fr.left));
                if (first) a0 = now, first = false;
                (void)ar;
                printf(" axle d(x,y,z) mm %5.1f %5.1f %5.1f", (now.x - a0.x) * 1000, (now.y - a0.y) * 1000, (now.z - a0.z) * 1000);
                printf(" roll %.2f", std::asin(fr.left.y) * 180 / M_PI);
                V3 lat = c.nodes[r.inner].p - c.nodes[l.inner].p;
                printf(" axleYaw %.2f", std::atan2(dot(lat, fr.fwd), -dot(lat, fr.left)) * 180 / M_PI);
                for (const SuspLink& lk : c.links) if (lk.kind == LINK_TRAILING) { const Beam& b = c.beams[lk.beam]; printf(" L%d %.1fmm", lk.wheel, (len(c.nodes[b.b].p - c.nodes[b.a].p) - b.L0) * 1000); }
            }
            // Matchboxing: left rail vs right rail fore-aft offset at each station (mm), vs rest.
            printf(" box(mm)");
            for (int st = 0; st < c.ladderStations; st += 2) {
                double xl = 0, xr = 0;
                for (int k = 0; k < 4; k++) { xl += dot(c.nodes[c.ladder[st * 8 + k]].p - fr.com, fr.fwd); xr += dot(c.nodes[c.ladder[st * 8 + 4 + k]].p - fr.com, fr.fwd); }
                double r = 0; for (int k = 0; k < 4; k++) r += c.restLocal[c.ladder[st * 8 + k]].x - c.restLocal[c.ladder[st * 8 + 4 + k]].x;
                printf(" %4.1f", ((xl - xr) - r) / 4 * 1000);
            }
            printf("\n");
        }
    });
}
