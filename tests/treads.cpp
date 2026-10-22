// Tire treads and snow chains: each tread grips each surface its own way, and chains are
// fitted on foot, grip ice and snow, and snap when driven too fast.
#include <cmath>

#include "../src/interact.h"
#include "../src/mods.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

// Paints a strip of ground with one surface (x from x0 to x1, |z - z0| < 6).
void paintStrip(World& w, double x0, double x1, double z0, Surface s) {
    for (double x = x0; x <= x1; x += 0.5)
        for (double z = z0 - 6; z <= z0 + 6; z += 0.5) {
            const int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellSurface(id, s, s == SURF_ICE ? PAINT_ICE : s == SURF_MUD ? PAINT_MUD : PAINT_PAD);
        }
}

// The Bronco with a given tread (from the garage's tires slot), or chains on every wheel.
const VehicleSpec& broncoOn(int tread) {
    const int v = 2;
    for (int o = 0; o < (int)modOptions(v, MOD_TIRES).size(); o++) {
        ModSet ms;
        ms.pick[MOD_TIRES] = o;
        const VehicleSpec& s = tunedSpec(v, ms);
        if (s.tireType == tread) return s;
    }
    return vehicleCatalog()[v];
}

// Full brakes from 40 km/h on a strip of `surf`: the distance to a stop.
double stopDistance(Ctx& ctx, const VehicleSpec& spec, Surface surf, bool chains, double z) {
    paintStrip(ctx.sim.world, -150, 40, z, surf);
    Car& c = ctx.add(spec, -130, z, 0);
    const int idx = (int)ctx.sim.cars.size() - 1;
    ctx.run(1.0);
    if (chains)
        for (Wheel& wh : ctx.sim.cars[idx].wheels) wh.chained = true;
    if (spec.tireType == TIRE_RACE_SLICK && surf == SURF_PAVED)  // (slicks are raced warm: tiretemp.h)
        for (Wheel& wh : ctx.sim.cars[idx].wheels) wh.tempC = 90;
    ctx.launch(c, 40 / 3.6);
    const V3 p0 = ctx.sim.cars[idx].frame().com;
    ctx.runUntil(30, [&] { return len(ctx.sim.cars[idx].frame().vel) < 0.3; }, [&] {
        Car& cc = ctx.sim.cars[idx];
        cc.throttle = 0, cc.brake = 1, cc.handbrake = 0, cc.steer = 0;
    });
    return len(ctx.sim.cars[idx].frame().com - p0);
}

}  // namespace

SCENARIO(treadGrip, "tires/treads", "Each tread on ice, pavement and mud: winter tires and chains stop shortest on ice, race slicks on pavement, slicks are hopeless in mud") {
    const double iceStreet = stopDistance(ctx, broncoOn(TIRE_STREET), SURF_ICE, false, -50);
    const double iceSnow = stopDistance(ctx, broncoOn(TIRE_SNOW), SURF_ICE, false, -30);
    const double iceChains = stopDistance(ctx, broncoOn(TIRE_STREET), SURF_ICE, true, -10);
    ctx.metric("ice, street tires: stopping from 40 km/h", iceStreet, "m", 25, 120);
    ctx.metric("ice, winter tires vs street (shorter)", iceSnow / iceStreet, "x", 0.3, 0.8);
    ctx.metric("ice, chains vs street (shortest)", iceChains / iceStreet, "x", 0.1, 0.6);
    ctx.check("chains beat winter tires on ice", iceChains < iceSnow, std::to_string(iceChains) + " vs " + std::to_string(iceSnow) + " m");
    const double dryStreet = stopDistance(ctx, broncoOn(TIRE_STREET), SURF_PAVED, false, 10);
    const double drySlick = stopDistance(ctx, broncoOn(TIRE_RACE_SLICK), SURF_PAVED, false, 30);
    // (On slicks the Bronco's brakes, not the rubber, set the stop: with every tire
    // kTireGripScale grippier the street tires come closer to that limit, ~0.94x became ~0.96x.)
    ctx.metric("pavement, race slicks vs street (shorter)", drySlick / dryStreet, "x", 0.6, 0.97);
    const double mudAT = stopDistance(ctx, broncoOn(TIRE_MUD_TERRAIN), SURF_MUD, false, 50);
    const double mudSlick = stopDistance(ctx, broncoOn(TIRE_RACE_SLICK), SURF_MUD, false, 70);
    ctx.metric("mud, race slicks vs mud tires (much longer)", mudSlick / mudAT, "x", 1.5, 8);
}

SCENARIO(chainsOnFoot, "tires/chains", "On foot: a snow chain out of the back, round a tire (stopped), off again; they snap after a couple of seconds at 80 km/h") {
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);  // the Bronco carries four
    ctx.focus(c);
    ctx.run(1.0);
    ctx.check("the Bronco carries four chains", c.chains == 4, std::to_string(c.chains));
    World& w = ctx.sim.world;
    Walker wk;
    wk.exitCar(w, c);
    Interact ia;
    // Hold E toward something until it's done (or give up): returns what it said.
    auto hold = [&](const V3& at, double secs) {
        std::string said;
        for (double t = 0; t < secs && said.empty(); t += Ctx::kFrame) {
            const V3 eye = wk.p + V3(0, 1.6, 0);
            said = ia.update(ctx.sim, wk, eye, at - eye, true, Ctx::kFrame);
        }
        return said;
    };
    // Stand behind the truck and take a chain.
    const Frame f = c.frame();
    const V3 back = f.com - f.fwd * (c.spec->length * 0.5);
    wk.p = back - f.fwd * 1.2;
    wk.p.y = w.height(wk.p.x, wk.p.z);
    const V3 tailAt = c.stowAt(Car::STOW_CHAINS);
    ctx.check("takes a chain", hold(tailAt, 5).find("CHAIN") != std::string::npos && wk.holdingChain && c.chains == 3);
    // Walk to the front left tire and wrap it.
    Wheel& wh = c.wheels[0];
    const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
    const V3 out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    wk.p = hub + out * 1.0;
    wk.p.y = w.height(wk.p.x, wk.p.z);
    const std::string fitted = hold(hub, 12);
    ctx.check("wraps it round the tire", wh.chained && !wk.holdingChain, fitted);
    // Take it off again, and put it back.
    ctx.check("takes it off", hold(hub, 8) == "CHAIN OFF" && !wh.chained && wk.holdingChain);
    wk.p = back - f.fwd * 1.2;
    wk.p.y = w.height(wk.p.x, wk.p.z);
    ctx.check("puts it back", hold(tailAt, 5) == "CHAIN PUT AWAY" && c.chains == 4);
    // All four on, then 80 km/h on pavement: they snap.
    for (Wheel& w2 : c.wheels) w2.chained = true;
    c.chains = 0;
    ctx.launch(c, 80 / 3.6);
    ctx.run(4.0, [&] { c.throttle = 0.8, c.brake = 0; });
    int still = 0;
    for (const Wheel& w2 : c.wheels) still += w2.chained;
    ctx.check("at 80 km/h the chains snap", c.chainsSnapped >= 2 && still <= 2, std::to_string(c.chainsSnapped) + " snapped");
}

SCENARIO(chainsLook, "tires/chains-look", "A Bronco parked with chains on all four tires (for screenshots: --scenario tires/chains-look)") {
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(0.5);
    for (Wheel& wh : c.wheels) wh.chained = true;
    c.chains = 0;
    ctx.run(2.0, [&] { c.handbrake = 1; });
    int on = 0;
    for (const Wheel& wh : c.wheels) on += wh.chained;
    ctx.check("chains stay on while parked", on == 4);
}
