#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v0 = argc > 1 ? atoi(argv[1]) : 0, v1 = argc > 1 ? v0 + 1 : 5;
    for (int v = v0; v < v1; v++) {
        Sim sim; Ctx ctx(sim);
        VehicleSpec spec = vehicleCatalog()[v];
        spec.tireGrip = 2.5;  // a tilt table's grippy deck: nothing slides
        Car& c = ctx.add(spec, -100, -50, 0);
        ctx.run(1.5, [&] { c.handbrake = 1; });
        // Downhill is the car's right (+z): a lip outboard of the right tires stops them sliding.
        double zr = -1e9, xs = 0;
        for (const Wheel& w : c.wheels) if (!w.left) zr = std::max(zr, c.nodes[w.outer].p.z);
        World& wd = ctx.sim.world;
        (void)zr;
        ctx.run(0.5, [&] { c.handbrake = 1; });
        std::vector<double> rest;
        for (const Wheel& w : c.wheels) rest.push_back(w.load);
        c.extForce.assign(c.nodes.size(), V3());
        double ang = 0, lift = -1;
        const double t0 = ctx.t;
        // CG height and track at rest
        double cy = 0, m = 0; for (const Node& n : c.nodes) cy += n.p.y * n.m, m += n.m;
        cy /= m;
        double gy = wd.height(c.frame().com.x, c.frame().com.z);
        double tr = 0; { V3 a = c.nodes[c.wheels[0].inner].p + c.nodes[c.wheels[0].outer].p, b = c.nodes[c.wheels[1].inner].p + c.nodes[c.wheels[1].outer].p; tr = len(a - b) / 2; }
        ctx.runUntil(90, [&] { return lift >= 0; }, [&] {
            ang = std::min(75.0, 20 + (ctx.t - t0) * 0.5) * M_PI / 180;
            c.handbrake = 1;
            const V3 right = -c.frame().left;  // downhill
            for (size_t i = 0; i < c.nodes.size(); i++)
                c.extForce[i] = (right * std::sin(ang) + V3(0, 1, 0) * (1 - std::cos(ang))) * (9.81 * c.nodes[i].m);
            bool up = true;
            for (size_t i = 0; i < c.wheels.size(); i++)
                if (c.wheels[i].left && c.wheels[i].load > 0.05 * rest[i]) up = false;
            if (up) lift = ang;
            static int fr = 0;
            if (argc > 2 && ++fr % 120 == 0) {
                printf("ang %.1f loads:", ang * 180 / M_PI);
                for (size_t i = 0; i < c.wheels.size(); i++) printf(" %s%s %.0f", c.wheels[i].front ? "F" : "R", c.wheels[i].left ? "L" : "R", c.wheels[i].load);
                printf("  up.y %.2f com (%.2f %.2f %.2f)\n", c.frame().up.y, c.frame().com.x, c.frame().com.y, c.frame().com.z);
            }
        });
        double ssf = tr / (2 * (cy - gy));
        printf("%-18s CG %.3f track %.3f SSF %.3f  lift at %.1f deg  TTR %.3f  (ratio %.2f)\n", spec.name, cy - gy, tr, ssf, lift * 180 / M_PI, std::tan(lift), std::tan(lift) / ssf);
    }
}
