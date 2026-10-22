// Tire speed ratings and blowouts (src/tires.h, vehicles.h tireSpeedRatingKmh): no ECU governor caps
// the trucks any more; a tire driven past its rating can fail, and fails like any flat.
#include <cmath>

#include "../src/fixes.h"
#include "../src/hudcontext.h"
#include "harness.h"

namespace {

constexpr double kLaneZ = 6.9;  // the middle eastbound lane (freeway.cpp)

// Steers a little to stay in the lane at speed (freeway.cpp's holdLane, without the slow trim).
void holdLane(Car& c) {
    const Frame f = c.frame();
    const double v = std::max(5.0, len(f.vel));
    const double pull = clampd(0.25 * (f.com.z - kLaneZ) + 2.5 * f.vel.z, -3.0, 3.0);
    c.steer = clampd(c.wheelbase * pull / (v * v), -0.1, 0.1);
}

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }  // all-terrains: 170 km/h

// Drives the Bronco on the freeway with every node held at `kmh` (wheels spinning to match)
// until a tire blows or `secs` pass. Returns the time of the first blowout (-1: none) and which wheel.
struct Run {
    double t = -1;
    int wheel = -1;
    double yaw = 0;
};

Run hold(Ctx& ctx, Car& c, double kmh, double secs) {
    Run r;
    ctx.launch(c, kmh / 3.6);
    const double t0 = ctx.t;
    ctx.runUntil(secs, [&] { return r.t >= 0; }, [&] {
        const Frame f = c.frame();
        holdLane(c);
        for (Node& n : c.nodes) n.v = n.v + f.fwd * (kmh / 3.6 - dot(f.vel, f.fwd));
        for (Wheel& wh : c.wheels) wh.omega = kmh / 3.6 / wh.R;
        for (size_t k = 0; k < c.wheels.size(); k++)
            if (c.wheels[k].popped && r.t < 0) r.t = ctx.t - t0, r.wheel = (int)k;
    });
    return r;
}

Car& fresh(Ctx& ctx, uint64_t seed) {
    ctx.sim.cars.clear();
    ctx.sim.world = World(MAP_FREEWAY);
    Car& c = ctx.add(bronco(), ctx.sim.world.spawn.x, kLaneZ, 0);
    ctx.sim.player = 0;
    c.blowSeed = seed;
    c.blowoutsOn = true;  // (off in the game for now: these test the mechanism)
    ctx.focus(c);
    ctx.run(1.0);
    return c;
}

}  // namespace

SCENARIO(blowoutRatings, "blowout/ratings", "Speed ratings: mud and snow 160, all-terrain 170, street 180, semi-slick and race slick 270; no vehicle has a governor; the failure curve is zero below the rating, rare at it, minutes at +15%, seconds at +30%") {
    ctx.check("mud 160", tireSpeedRatingKmh(TIRE_MUD_TERRAIN) == 160 && tireSpeedRatingKmh(TIRE_SNOW) == 160);
    ctx.check("all-terrain 170", tireSpeedRatingKmh(TIRE_ALL_TERRAIN) == 170);
    ctx.check("street 180", tireSpeedRatingKmh(TIRE_STREET) == 180);
    ctx.check("slicks 270", tireSpeedRatingKmh(TIRE_SEMI_SLICK) == 270 && tireSpeedRatingKmh(TIRE_RACE_SLICK) == 270);
    ctx.check("below the rating: never", tireBlowoutRate(0.999, 1, 1) == 0 && tireBlowoutRate(0.5, 2, 1.6) == 0);
    const double r0 = tireBlowoutRate(1.0, 1, 1), r15 = tireBlowoutRate(1.15, 1, 1), r30 = tireBlowoutRate(1.3, 1, 1);
    // Per car (4 tires, warm, nominal load): mean time to the first blowout.
    ctx.metric("at the rating: mean s to the first blowout", 1 / (4 * r0), "s", 600, 1e9);
    ctx.metric("+15%: mean s to the first blowout", 1 / (4 * r15), "s", 20, 120);
    ctx.metric("+30%: mean s to the first blowout", 1 / (4 * r30), "s", 0.5, 6);
    ctx.check("hotter or more load: higher", tireBlowoutRate(1.15, 1.5, 1) > r15 && tireBlowoutRate(1.15, 1, 1.5) > r15);
    ctx.check("dice are deterministic and spread", blowoutDie(7, 2, 100) == blowoutDie(7, 2, 100) && blowoutDie(7, 2, 100) != blowoutDie(7, 3, 100) &&
                                                       blowoutDie(7, 2, 100) != blowoutDie(8, 2, 100));
    double lo = 1, hi = 0, sum = 0;
    for (uint32_t i = 0; i < 20000; i++) {
        const double d = blowoutDie(3, 1, i);
        lo = std::min(lo, d), hi = std::max(hi, d), sum += d;
    }
    ctx.check("dice are uniform in [0,1)", lo >= 0 && hi < 1 && std::fabs(sum / 20000 - 0.5) < 0.01, std::to_string(sum / 20000));
}

SCENARIO(blowoutStockRating, "blowout/stock-rating", "The Jesko's stock semi-slicks are rated 500 km/h; any other tire fitted uses its type's rating (270 for race slicks)") {
    VehicleSpec j = vehicleCatalog()[1];
    ctx.check("stock tires: 500", j.tireRatingKmh() == 500, std::to_string(j.tireRatingKmh()));
    j.tireType = TIRE_RACE_SLICK;
    ctx.check("race slicks fitted: 270", j.tireRatingKmh() == 270);
    j.tireType = TIRE_STREET;
    ctx.check("street tires fitted: 180", j.tireRatingKmh() == 180);
    ctx.check("a Bronco has no stock override", bronco().tireRatingKmh() == 170);
}

SCENARIO(blowoutBelow, "blowout/below-rating", "A Bronco held at 95% of its tires' rating for three minutes never blows a tire") {
    Car& c = fresh(ctx, 5);
    const Run r = hold(ctx, c, 170 * 0.95, 180);
    ctx.check("no blowout below the rating", r.t < 0, std::to_string(r.t));
    ctx.check("every tire still up", !c.wheels[0].popped && !c.wheels[1].popped && !c.wheels[2].popped && !c.wheels[3].popped);
}

SCENARIO(blowoutOver, "blowout/over-rating", "A Bronco held at +30% (221 km/h) on its all-terrains blows a tire within seconds, the same way for the same seed; the blown tire is a flat (rolls on the rim, less grip, a FLAT TIRE warning) and a new tire repairs it") {
    Car& c = fresh(ctx, 5);
    const Run a = hold(ctx, c, 170 * 1.3, 60);
    ctx.metric("+30%: s to the first blowout", a.t, "s", 0, 30);
    ctx.check("a tire blew", a.t >= 0 && c.blowouts == 1 && c.tirePops >= 1, std::to_string(a.t));
    if (a.wheel >= 0) {
        const Wheel& wh = c.wheels[a.wheel];
        ctx.check("it is a flat: popped, rolling on the rim", wh.popped && wh.rollR() < wh.R - 0.05, std::to_string(wh.rollR()));
        // Pulls toward its side: heading change while the wheels keep their speed and the steering is straight.
        const V3 fwd0 = c.frame().fwd;
        ctx.run(1.5, [&] {
            const Frame f = c.frame();
            for (Node& n : c.nodes) n.v = n.v + f.fwd * (221 / 3.6 - dot(f.vel, f.fwd));
            for (Wheel& w : c.wheels) w.omega = (w.popped ? w.rollR() : w.R) > 0 ? 221 / 3.6 / (w.popped ? w.rollR() : w.R) : 0;
        });
        const double yaw = cross(fwd0, c.frame().fwd).y;  // + turns left (about +y)
        ctx.metric("heading change toward the blown side (+ = toward)", wh.left ? yaw : -yaw, "rad", -0.001, 5);
        const auto fixes = fixesFor(c);
        bool offered = false;
        for (const Fix& f : fixes) offered |= f.kind == Fix::NEW_TIRE && f.wheel == a.wheel;
        ctx.check("field repair offers a new tire for it", offered);
        c.repairKit.spareTires = 1;
        c.fieldRepair();
        ctx.check("a spare tire repairs it", !c.wheels[a.wheel].popped);
    }
    // The same seed, the same blowout.
    Car& c2 = fresh(ctx, 5);
    const Run b = hold(ctx, c2, 170 * 1.3, 60);
    ctx.check("same seed: same wheel at the same time", b.wheel == a.wheel && std::fabs(b.t - a.t) < 1e-9, std::to_string(a.t) + " vs " + std::to_string(b.t));
    Car& c3 = fresh(ctx, 6);
    const Run d = hold(ctx, c3, 170 * 1.3, 60);
    ctx.check("another seed: another roll", d.t != a.t || d.wheel != a.wheel, std::to_string(d.t));
}

SCENARIO(blowoutRemote, "blowout/remote", "A remote car's tires never blow here (its owner decides; the flag arrives in the snapshot)") {
    Car& c = fresh(ctx, 5);
    c.remote = true;
    const Run r = hold(ctx, c, 170 * 1.3, 20);
    ctx.check("no blowout on a remote car", r.t < 0);
}

SCENARIO(blowoutHud, "blowout/hud", "TIRES OVER SPEED RATING: faded out below the rating, up while over it, gone again after") {
    using namespace hudctx;
    Context c;
    Sample s;
    s.speed = 40;
    s.tireFrac = 0.9f;
    for (int i = 0; i < 120; i++) c.update(s, 1.0f / 60);
    ctx.check("under the rating: hidden", c.alpha(E_TIRESPEED) == 0);
    s.tireFrac = 1.05f;
    for (int i = 0; i < 30; i++) c.update(s, 1.0f / 60);
    ctx.check("over it: up", c.alpha(E_TIRESPEED) > 0.99f);
    s.tireFrac = 0.9f;
    for (int i = 0; i < 120; i++) c.update(s, 1.0f / 60);
    ctx.check("back under: faded", c.alpha(E_TIRESPEED) == 0);
}
