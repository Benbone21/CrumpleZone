// On foot: walking, stepping up, being blocked, wading, and getting in and out.
#include <cmath>

#include "../src/walker.h"
#include "harness.h"

SCENARIO(walkerBasics, "walker/basics", "Out of the truck beside it, walks 20 m in ~12 s (runs it in ~5), steps onto a 0.3 m rock, is stopped by a tree, wades and floats") {
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    World& w = ctx.sim.world;
    Walker wk;
    wk.exitCar(w, c);
    const Frame f = c.frame();
    ctx.metric("out beside the truck", len(V3(wk.p.x - f.com.x, 0, wk.p.z - f.com.z)), "m", 1.2, 2.5);
    ctx.check("can get back in", wk.carToEnter(ctx.sim) == ctx.sim.player);
    auto walkFor = [&](double secs, double fwd, bool run, double yaw) {
        for (double t = 0; t < secs; t += Ctx::kFrame) wk.step(w, ctx.sim, Ctx::kFrame, fwd, 0, run, false, yaw);
    };
    // Walk east 20 m (away from the truck, on the pad).
    wk.p = V3(-100, w.height(-100, -50), -50), wk.v = V3();
    double t = 0;
    while (wk.p.x < -80 && t < 30) wk.step(w, ctx.sim, Ctx::kFrame, 1, 0, false, false, 0), t += Ctx::kFrame;
    ctx.metric("walking 20 m", t, "s", 9, 15);
    wk.p = V3(-100, w.height(-100, -40), -40), wk.v = V3();
    t = 0;
    while (wk.p.x < -80 && t < 30) wk.step(w, ctx.sim, Ctx::kFrame, 1, 0, true, false, 0), t += Ctx::kFrame;
    ctx.metric("running 20 m", t, "s", 3.5, 6);
    // A rock to step onto (0.3 m showing), and a tree to walk into.
    w.addRock(-60, -20, V3(1.5, 0.4, 1.5), 0.3, 77);
    w.addTree(-60, 0, 10, 0);
    w.buildGrid();
    wk.p = V3(-64, w.height(-64, -20), -20), wk.v = V3();
    double lift = 0;
    for (double tt = 0; tt < 6; tt += Ctx::kFrame) {
        wk.step(w, ctx.sim, Ctx::kFrame, 1, 0, false, false, 0);
        lift = std::max(lift, wk.p.y - w.height(wk.p.x, wk.p.z));
    }
    ctx.metric("stepped up over the 0.3 m rock (highest above the ground)", lift, "m", 0.2, 0.45);
    ctx.check("and on past it", wk.p.x > -57, std::to_string(wk.p.x));
    wk.p = V3(-64, w.height(-64, 0), 0), wk.v = V3();
    walkFor(5, 1, false, 0);
    ctx.check("stopped by the tree", wk.p.x < -60.2, std::to_string(wk.p.x));
    // Water: a basin dug and flooded 2 m deep.
    for (int i = -8; i <= 8; i++)
        for (int j = -8; j <= 8; j++) {
            const int id = w.cellIndex(-30 + i, 30 + j);
            w.setCellHeight(id, (float)(w.height(-30 + i, 30 + j) - 2.5));
            w.setCellWater(id, (float)(w.height(-30, 60)) - 0.5f);
        }
    wk.p = V3(-30, w.height(-30, 30), 30), wk.v = V3();
    walkFor(5, 0, false, 0);
    ctx.check("floats in deep water", wk.inWater > 0.3 && wk.p.y > w.height(-30, 30) + 0.5, std::to_string(wk.inWater));
}
