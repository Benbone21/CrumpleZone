#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int veh = argc > 1 ? atoi(argv[1]) : 2;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[veh], 0, -50, 0);
    ctx.run(2.0);
    Frame f = c.frame();
    double front = 0, rear = 0, mfront = 0, total = 0, fy = 1e9;
    for (size_t i = 0; i < c.nodes.size(); i++) {
        double x = dot(c.nodes[i].p - f.com, f.fwd);
        total += c.nodes[i].m;
        if (x > 1.5) mfront += c.nodes[i].m;
        if (x > 1.9 && (int)i < c.bodyNodes) fy = std::min(fy, c.nodes[i].p.y);
    }
    for (auto& w : c.wheels) (w.front ? front : rear) += w.load;
    printf("mass %.0f  front-axle load %.1f%%  com y %.3f  pitch %.2f deg  mass ahead of 1.5 m %.0f  lowest nose node y %.3f  max node mass %.1f\n", total, front / (front + rear) * 100, f.com.y,
           std::asin(f.fwd.y) * 57.3, mfront, fy, [&] { double m = 0; for (auto& n : c.nodes) m = std::max(m, n.m); return m; }());
}
