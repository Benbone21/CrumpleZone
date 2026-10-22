// On foot: walking, stepping up, being blocked, wading, and getting in and out.
#include <cmath>

#include "../src/carentry.h"
#include "../src/interact.h"
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
    // Strafing: facing east (+x, with +y up, -z is on your left), A goes left and D right.
    for (int sd : {1, -1}) {
        wk.p = V3(-100, w.height(-100, -30), -30), wk.v = V3();
        for (double tt = 0; tt < 2; tt += Ctx::kFrame) wk.step(w, ctx.sim, Ctx::kFrame, 0, sd, false, false, 0);
        ctx.check(sd > 0 ? "A steps to the left" : "D steps to the right", sd > 0 ? wk.p.z < -31 : wk.p.z > -29, std::to_string(wk.p.z));
    }
    {  // and out of the driver's (left) door
        Walker w2;
        w2.exitCar(w, c);
        ctx.check("gets out on the driver's side", dot(w2.p - f.com, f.left) > 0.5);
    }
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

SCENARIO(gameplayFuel, "gameplay/fuel", "Fuel: a Bronco at full throttle burns about what a 330 hp engine should, stalls when the tank runs dry, and a shop pad fills it back up") {
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.fuelUse = 1;  // real
    const double f0 = c.fuel;
    ctx.check("starts full", std::fabs(f0 - c.spec->fuelCapacity) < 0.5, std::to_string(f0));
    ctx.run(8, [&] { c.throttle = 1, c.brake = 0, c.handbrake = 0; });
    const double used = f0 - c.fuel;
    // ~250 kW average over a full-throttle pull: 250*250/745/3600*8 = ~0.19 L.
    ctx.metric("burned in 8 s flat out", used, "L", 0.08, 0.4);
    c.fuel = 0.02;
    ctx.run(4, [&] { c.throttle = 1, c.brake = 0, c.handbrake = 0; });
    ctx.check("out of fuel", c.outOfFuel());
    ctx.metric("engine stops", c.rpm, "rpm", 0, 200);
    // Another truck, nearly dry, parked on the proving ground's shop pad.
    const Station st = ctx.sim.world.stations[0];
    Car& d = ctx.add(vehicleCatalog()[2], st.p.x, st.p.z, 0);
    ctx.run(0.5);
    d.fuelUse = 1;
    d.fuel = 1;
    ctx.run(5, [&] { d.throttle = 0, d.brake = 1, d.handbrake = 1; });
    ctx.metric("filled at the shop in 5 s", d.fuel, "L", 10, 20);
}

SCENARIO(walkerCrouch, "walker/crouch", "Crouch: the eyes ease down to ~60% in ~0.35 s and back up, walking drops to ~45% (no running or jumping), a repair prompt is still offered crouched, and standing up again") {
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    World& w = ctx.sim.world;
    Walker wk;
    wk.exitCar(w, c);
    wk.p = V3(-100, w.height(-100, -50), -50), wk.v = V3();
    auto stepFor = [&](double secs, double fwd, bool run) {
        for (double t = 0; t < secs; t += Ctx::kFrame) wk.step(w, ctx.sim, Ctx::kFrame, fwd, 0, run, false, 0);
    };
    stepFor(0.5, 0, false);
    ctx.metric("standing eye height", wk.eyeHeight(), "m", 1.6, 1.64);
    wk.crouching = true;
    stepFor(0.17, 0, false);
    ctx.check("halfway down after ~0.17 s", wk.eyeHeight() < 1.4 && wk.eyeHeight() > 1.1, std::to_string(wk.eyeHeight()));
    stepFor(0.3, 0, false);
    ctx.metric("crouched eye height (60% of standing)", wk.eyeHeight() / Walker::kEyeStand, "", 0.58, 0.62);
    // Speed: 10 s of walking, standing then crouched (running while crouched is still a walk).
    auto distance = [&](bool crouch, bool run) {
        wk.crouching = crouch;
        wk.p = V3(-100, w.height(-100, -40), -40), wk.v = V3();
        stepFor(1.0, 0, false);  // (settle into the stance)
        const double x0 = wk.p.x;
        stepFor(5, 1, run);
        return wk.p.x - x0;
    };
    const double stand = distance(false, false), low = distance(true, false), lowRun = distance(true, true);
    ctx.metric("crouched walking speed vs standing", low / stand, "", 0.40, 0.50);
    ctx.check("holding run while crouched stays a crouch walk", lowRun < stand * 0.55, std::to_string(lowRun));
    // A repair prompt is still offered with the eyes down: at the parked truck's door.
    {
        Car& t = ctx.add(vehicleCatalog()[2], -60, -30, 0);
        ctx.run(1.0);
        Walker pw;
        pw.exitCar(w, t);
        pw.crouching = true;
        for (double s = 0; s < 1; s += Ctx::kFrame) pw.step(w, ctx.sim, Ctx::kFrame, 0, 0, false, false, 0);
        const V3 eye = pw.p + V3(0, pw.eyeHeight(), 0);
        const V3 at = carentry::door(t, false);
        InteractOption o = Interact::pick(ctx.sim, pw, eye, at - eye);
        ctx.check("crouched at the door: the get-in prompt is still there", o.kind == IA_ENTER_CAR && !o.blocked, o.prompt);
    }
    // Stand up again.
    wk.crouching = false;
    stepFor(0.5, 0, false);
    ctx.metric("standing again", wk.eyeHeight(), "m", 1.6, 1.64);
    // (Standing up under a low ceiling: Walker::headBlocked keeps you down; no overhang in the test world to prove it.)
    wk.p = V3(-60, w.height(-60, 0), 0);
    stepFor(0.6, 0, false);
    ctx.check("out in the open the key's let go: stands", wk.eyeHeight() > 1.6, std::to_string(wk.eyeHeight()));
}
