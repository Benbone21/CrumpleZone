// The dev free cam (freecam.h): carrying a vehicle moves it rigidly (its shape node for node), sets it down upright
// and at rest, letting go drops it without an explosion; spawning at the crosshair puts the vehicle there; the
// crosshair finds the vehicle it's on; and its keys don't clash with each other or with the always-on keys.
#include <cmath>
#include <set>
#include <vector>

#include "../src/devmenu.h"
#include "../src/freecam.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "../src/vehicles.h"
#include "harness.h"

namespace {

std::vector<double> beamLengths(const Car& c) {
    std::vector<double> r;
    r.reserve(c.beams.size());
    for (const Beam& b : c.beams) r.push_back(len(c.nodes[b.a].p - c.nodes[b.b].p));
    return r;
}

double worstChange(const std::vector<double>& a, const std::vector<double>& b) {
    double w = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) w = std::max(w, std::fabs(a[i] - b[i]));
    return a.size() == b.size() ? w : 1e9;
}

double fastestNode(const Car& c) {
    double v = 0;
    for (const Node& n : c.nodes) v = std::max(v, len(n.v));
    return v;
}

}  // namespace

SCENARIO(freecamCarry, "freecam/carry-drop", "Free cam: a carried vehicle keeps its shape node for node, follows and turns as held, and set down it lands upright and settles without a jolt") {
    Sim& sim = ctx.sim;
    for (int model : {2, 0}) {
        const int i = (int)sim.cars.size();
        ctx.add(vehicleCatalog()[model], 30.0 * i, 0, 20);
        ctx.run(1.5);
        Car& c = sim.cars[i];
        const std::string tag = std::string(" (") + c.spec->name + ")";
        const std::vector<double> before = beamLengths(c);
        const double damage0 = c.damage;
        const int broken0 = c.brokenBeams;
        freecam::Carry cr;
        ctx.check("it can be picked up" + tag, freecam::carriable(sim, i) && freecam::pickUp(sim, i, cr) && cr.car == i);
        const V3 start = c.frame().com;
        V3 at;
        double worst = 0, worstOff = 0;
        for (int f = 0; f < 90; f++) {  // (flown off and up, turning: the sim steps between, as in the game)
            ctx.step();
            cr.yaw += 0.04;
            at = start + V3(0.25 * f, 4 + 0.05 * f, -0.1 * f);
            freecam::hold(sim, cr, at);
            worst = std::max(worst, worstChange(before, beamLengths(c)));
            worstOff = std::max(worstOff, len(c.frame().com - at));
        }
        ctx.metric("largest change in a beam's length while carried" + tag, worst, "m", 0, 1e-9);
        ctx.check("held where it was asked to be" + tag, worstOff < 1e-9);
        const Frame held = c.frame();
        ctx.check("held upright, at the heading asked" + tag, held.up.y > 0.99 && std::fabs(wrapAngle(std::atan2(-held.fwd.z, held.fwd.x) - cr.yaw)) < 0.1);
        ctx.check("at rest while held" + tag, fastestNode(c) == 0);
        // Set down at a ground point (the crosshair's).
        const double x = at.x + 6, z = at.z - 3, yaw = cr.yaw;
        freecam::dropAt(sim, cr, x, z);
        ctx.check("the carry ends" + tag, !cr.on());
        double fastest = 0;
        ctx.run(2.0, [&] { fastest = std::max(fastest, fastestNode(c)); });
        const Frame f = c.frame();
        ctx.check("set down at the point" + tag, std::hypot(f.com.x - x, f.com.z - z) < 1.5);
        ctx.check("facing the way it was held" + tag, std::fabs(wrapAngle(std::atan2(-f.fwd.z, f.fwd.x) - yaw)) < 0.15);
        ctx.check("upright on its wheels" + tag, f.up.y > 0.95 && f.com.y - sim.world.height(f.com.x, f.com.z) < 2.0);
        ctx.check("settled" + tag, len(f.vel) < 0.3 && c.finite());
        ctx.metric("fastest node after the set-down" + tag, fastest, "m/s", 0, 3);
        ctx.check("no damage from it" + tag, c.damage - damage0 < 0.005 && c.brokenBeams == broken0);
    }
}

SCENARIO(freecamLetGo, "freecam/let-go", "Free cam: let go where it hangs, a vehicle falls, lands and settles in one piece (no explosion)") {
    Sim& sim = ctx.sim;
    Car& c = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    ctx.run(1.5);
    freecam::Carry cr;
    freecam::pickUp(sim, 0, cr);
    const V3 at = c.frame().com + V3(5, 1.2, 0);
    for (int f = 0; f < 10; f++) ctx.step(), freecam::hold(sim, cr, at);
    freecam::letGo(sim, cr);
    double fastest = 0;
    ctx.run(3.0, [&] { fastest = std::max(fastest, fastestNode(c)); });
    const Frame f = c.frame();
    ctx.check("it fell and landed upright", f.up.y > 0.9 && f.com.y < at.y - 0.5 && c.finite());
    ctx.check("settled", len(f.vel) < 0.3);
    ctx.metric("fastest node", fastest, "m/s", 0, 15);
    ctx.metric("damage from a 1.2 m drop", c.damage, "", 0, 0.03);
}

SCENARIO(freecamSpawnAim, "freecam/spawn-at-crosshair", "Free cam: the crosshair's ray finds the ground point and the vehicle it's on; a vehicle spawned at that point stands there") {
    Sim& sim = ctx.sim;
    const V3 eye(0, sim.world.height(0, 60) + 30, 60), want(20, sim.world.height(20, 20), 20);
    const V3 dir = norm(want - eye);
    V3 p;
    const bool hit = freecam::groundHit(sim.world, eye, dir, 4000, p);
    ctx.check("the ray meets the ground at the point looked at", hit && len(p - want) < 0.6, hit ? "" : "missed");
    V3 sky;
    ctx.check("looking up it meets nothing", !freecam::groundHit(sim.world, eye, norm(V3(0.3, 1, 0)), 4000, sky));
    const int i = freecam::spawnAt(sim, vehicleCatalog()[2], p.x, p.z, 0.7);
    ctx.check("spawned", i == 0 && (int)sim.cars.size() == 1);
    ctx.run(1.5);
    const Frame f = sim.cars[i].frame();
    ctx.check("standing at the crosshair point", std::hypot(f.com.x - p.x, f.com.z - p.z) < 1.5 && f.up.y > 0.95 && len(f.vel) < 0.3);
    ctx.check("with its handbrake on", sim.cars[i].handbrake > 0.5);
    const V3 at = f.com + V3(0, 0.3, 0);
    double t = 0;
    ctx.check("the crosshair on it picks it out", freecam::pickVehicle(sim, eye, norm(at - eye), 600, -1, &t) == i && t > 10 && t < len(at - eye));
    ctx.check("looking past it, or looking through it (the carried one), doesn't", freecam::pickVehicle(sim, eye, norm(V3(-1, -0.2, 0)), 600) < 0 &&
                                                                                       freecam::pickVehicle(sim, eye, norm(at - eye), 600, i) < 0);
    ctx.check("nor further than asked", freecam::pickVehicle(sim, eye, norm(at - eye), 5) < 0);
}

SCENARIO(freecamKeys, "freecam/keys", "Free cam: its key (F8) is a dev key and a VIEW dev menu entry; its own keys are all different and leave the always-on keys alone") {
    const Keybinds kb;
    ctx.check("FREE CAM is F8, a dev key in the DEV group", kb.key(KA_DEV_FREECAM) == SC_F8 && isDevAction(KA_DEV_FREECAM) && actionGroup(KA_DEV_FREECAM) == KG_DEV);
    bool clash = false;
    for (int a = 0; a < KA_COUNT; a++)
        if (a != KA_DEV_FREECAM && kb.key((KeyAction)a) == SC_F8 && isDevAction((KeyAction)a) && (actionContexts((KeyAction)a) & actionContexts(KA_DEV_FREECAM))) clash = true;
    ctx.check("no other dev key on F8", !clash);
    auto press = [&](bool devKeys) {
        devmenu::Action got;
        devmenu::dispatchKey(kb, SC_F8, false, devKeys, KCTX_DRIVE, [](void* u, const devmenu::Action& a) { *static_cast<devmenu::Action*>(u) = a; }, &got);
        return got;
    };
    ctx.check("in DEV KEYS mode F8 is FREE CAM; with gameplay keys it does nothing", press(true).act == devmenu::FREE_CAM && !press(false));
    ctx.check("and on foot too", kb.resolve(SC_F8, false, false, KCTX_FOOT, true).action == KA_DEV_FREECAM);
    bool inView = false;
    for (const devmenu::Item& it : devmenu::items(devmenu::G_VIEW, &kb)) inView |= it.action.act == devmenu::FREE_CAM && it.label == "FREE CAM" && it.hint.find("KEY F8") == 0;
    ctx.check("FREE CAM is in the dev menu's VIEW group, its key named", inView);
    ctx.check("the key and the menu entry are one action", devmenu::keyActionOf({devmenu::FREE_CAM, 0}) == KA_DEV_FREECAM && !devmenu::offlineOnly(devmenu::FREE_CAM));
    const std::vector<int>& k = freecam::keys();
    ctx.check("free cam's own keys are all different", std::set<int>(k.begin(), k.end()).size() == k.size());
    bool leaves = true;
    for (int sc : k)
        for (KeyAction a : {KA_DEV_MENU, KA_DEV_TOGGLE, KA_DEBUG_INFO, KA_DEV_RELOAD, KA_PHOTO, KA_FULLSCREEN, KA_DEV_FREECAM, KA_HUD_ALL})
            leaves &= sc != kb.key(a) && !freecam::passesThrough(sc);
    bool passes = true;
    for (KeyAction a : {KA_DEV_MENU, KA_DEV_TOGGLE, KA_DEBUG_INFO, KA_DEV_RELOAD, KA_PHOTO, KA_FULLSCREEN, KA_HUD_ALL}) passes &= freecam::passesThrough(kb.key(a));
    ctx.check("none of them is an always-on key (dev menu, key mode, debug info, reload, photo, fullscreen), which pass through", leaves && passes);
    ctx.check("the speed steps go up", [] {
        for (int s = 1; s < freecam::kSpeedCount; s++)
            if (freecam::speedOf(s) <= freecam::speedOf(s - 1)) return false;
        return freecam::speedOf(-3) == freecam::kSpeeds[0] && freecam::speedOf(99) == freecam::kSpeeds[freecam::kSpeedCount - 1] && freecam::speedOf(freecam::kSpeedCount - 1) >= 500;
    }());
}
