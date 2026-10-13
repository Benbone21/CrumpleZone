#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <set>
int main() {
    for (int v = 0; v < 5; v++) {
        Sim sim; Ctx ctx(sim);
        Car& c = ctx.add(vehicleCatalog()[v], -100, -50, 0);
        ctx.run(2.0, [&] { c.handbrake = 1; });
        Frame fr = c.frame();
        std::set<int> wheelNodes;
        for (const Wheel& w : c.wheels) wheelNodes.insert(w.assembly.begin(), w.assembly.end()), wheelNodes.insert(w.inner), wheelNodes.insert(w.outer);
        double g = ctx.sim.world.height(fr.com.x, fr.com.z);
        // Axle x positions and tire radius (local x along fwd).
        double xf = -1e9, xr = 1e9, R = c.wheels[0].R;
        for (const Wheel& w : c.wheels) { double x = dot((c.nodes[w.inner].p + c.nodes[w.outer].p) * 0.5 - fr.com, fr.fwd); if (w.front) xf = std::max(xf, x); else xr = std::min(xr, x); }
        double clearance = 1e9, approach = 90, depart = 90, breakover = 180;
        int lowest = -1;
        for (size_t i = 0; i < c.nodes.size(); i++) {
            if (!c.nodes[i].collide) continue;
            V3 d = c.nodes[i].p - fr.com;
            double x = dot(d, fr.fwd), h = c.nodes[i].p.y - g - 0.04;  // (the node is a 4 cm sphere)
            if (h < clearance) clearance = h, lowest = (int)i;
            // approach: ramp tangent to the front tire touching the node (tire centre at (xf, R))
            auto tangentAngle = [&](double dx, double hh) {  // dx = horizontal distance from axle outward (>0), node height hh
                // smallest angle th where the ramp line tangent to the circle passes through (dx, hh):
                // line: points with (p - c)·n = R, n = (sin th, -cos th) outward-down; c = (0, R).
                // => dx sin th - (hh - R) cos th = R
                double lo = 0, hi = M_PI / 2;
                for (int it = 0; it < 60; it++) { double m = 0.5 * (lo + hi); if (dx * std::sin(m) - (hh - R) * std::cos(m) < R) lo = m; else hi = m; }
                return lo;
            };
            if (x > xf) approach = std::min(approach, tangentAngle(x - xf, h) * 180 / M_PI);
            if (x < xr) depart = std::min(depart, tangentAngle(xr - x, h) * 180 / M_PI);
            if (x < xf && x > xr) {
                // breakover: two ramps from the tires meeting under the node: angle = 180 - (a1 + a2) style; use sum of tangent angles from each tire
                double a1 = tangentAngle(xf - x, h), a2 = tangentAngle(x - xr, h);
                breakover = std::min(breakover, (a1 + a2) * 180 / M_PI);
            }
        }
        printf("%-18s clearance %.3f m (node %d)  approach %.1f  departure %.1f  breakover %.1f deg\n", vehicleCatalog()[v].name, clearance, lowest, approach, depart, breakover);
    }
}
