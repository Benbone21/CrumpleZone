#define private public
#include "../../../tests/harness.h"
#undef private
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int v = argc > 1 ? atoi(argv[1]) : 3;
    Sim sim; Ctx ctx(sim);
    Car& c = ctx.add(vehicleCatalog()[v], -120, -50, 0);
    ctx.run(1.0);
    ctx.launch(c, 100 / 3.6);
    const Axle* ra = nullptr;
    for (const Axle& a : c.axles) if (!a.front) ra = &a;
    std::vector<int> lk;
    for (size_t i = 0; i < c.links.size(); i++) if (c.links[i].wheel < 0 || !c.wheels[c.links[i].wheel].front) lk.push_back((int)i);
    std::vector<double> L0;
    for (int i : lk) { const Beam& b = c.beams[c.links[i].beam]; L0.push_back(len(c.nodes[b.b].p - c.nodes[b.a].p)); }
    int n = 0;
    ctx.run(3.0, [&] {
        c.throttle = 0; c.brake = 1;
        if (n++ % 6) return;
        Frame f = c.frame();
        const Wheel& wl = c.wheels[ra->wheel[0]]; const Wheel& wr = c.wheels[ra->wheel[1]];
        V3 across = c.nodes[wr.inner].p - c.nodes[wl.inner].p;
        double steer = std::atan2(dot(across, f.fwd), -dot(across, f.left)) * 57.3;
        printf("t=%.2f yaw %+.2f axleSteer %+.2f lat %+.3f |", ctx.t, std::atan2(-f.fwd.z, f.fwd.x) * 57.3, steer, dot((c.nodes[wr.inner].p + c.nodes[wl.inner].p) * 0.5 - f.com, f.left));
        for (size_t k = 0; k < lk.size(); k++) { const Beam& b = c.beams[c.links[lk[k]].beam]; printf(" k%d %+.1fmm", c.links[lk[k]].kind, (len(c.nodes[b.b].p - c.nodes[b.a].p) - L0[k]) * 1000); }
        // axle yaw from the lower link axle ends (lowMount = wheel.lower)
        V3 acr2 = c.nodes[wr.lower].p - c.nodes[wl.lower].p;
        double steer2 = std::atan2(dot(acr2, f.fwd), -dot(acr2, f.left)) * 57.3;
        V3 acr3 = c.nodes[wr.outer].p - c.nodes[wl.outer].p;
        double steer3 = std::atan2(dot(acr3, f.fwd), -dot(acr3, f.left)) * 57.3;
        // frame ends of the two lower trailing links
        int fe[2] = {-1, -1};
        for (int i : lk) if (c.links[i].kind == LINK_TRAILING && c.links[i].wheel >= 0) { const Beam& b = c.beams[c.links[i].beam]; int fr_ = (b.a == c.wheels[c.links[i].wheel].lower) ? b.b : b.a; fe[c.wheels[c.links[i].wheel].left ? 0 : 1] = fr_; }
        static V3 r0[2]; static bool init = false;
        auto loc = [&](int nn) { V3 d = c.nodes[nn].p - f.com; return V3(dot(d, f.fwd), dot(d, f.up), -dot(d, f.left)); };
        if (!init) { r0[0] = loc(fe[0]); r0[1] = loc(fe[1]); init = true; }
        V3 d0 = loc(fe[0]) - r0[0], d1 = loc(fe[1]) - r0[1];
        printf("  linkEnds %+.2f hubs %+.2f  frameBrkt dx L %+.1fmm R %+.1fmm  load L %.0f R %.0f\n", steer2, steer3, d0.x * 1000, d1.x * 1000, wl.load, wr.load);
    });
}
