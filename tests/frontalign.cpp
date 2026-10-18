// Steering stops. The sandbox 4Runner (long travel, 37s, crawl box) turned to full lock in 4L
// with the pedal down: its inside front tire, unloaded, spinning and rubbing the frame, wound the
// knuckle round past full lock until the tie rod lay along the steering arm, and the linkage
// folded over and locked there, the wheel turned ~105 degrees with nothing bent: undrivable, and
// nothing for the repair menu to fix. Knuckles now meet a stop a little past full lock.
#include <cmath>
#include <cstdio>

#include "../src/mods.h"
#include "harness.h"

namespace {

int optIndex(int vehicle, int slot, const std::string& name) {
    const auto& o = modOptions(vehicle, slot);
    for (int i = 0; i < (int)o.size(); i++)
        if (o[i].name.find(name) != std::string::npos) return i;
    return 0;
}
const VehicleSpec& sandbox4Runner() {
    ModSet ms;
    ms.pick[MOD_ENGINE] = optIndex(3, MOD_ENGINE, "1GR SWAP");
    ms.pick[MOD_SUSPENSION] = optIndex(3, MOD_SUSPENSION, "LONG TRAVEL AND 37S");
    ms.pick[MOD_TRANSMISSION] = optIndex(3, MOD_TRANSMISSION, "CRAWL BOX");
    return tunedSpec(3, ms);
}

// A front wheel's steer against the body (rad, + left), from its axle line.
double steerOf(const Car& c, const Wheel& wh) {
    const V3 a = c.chassis.rot.unrotate(c.nodes[wh.outer].p - c.nodes[wh.inner].p), a0 = c.restLocal[wh.outer] - c.restLocal[wh.inner];
    return std::remainder(std::atan2(a.x, a.z) - std::atan2(a0.x, a0.z), 2 * M_PI);
}
// The inside wheel's angle at the rack's full lock (the rack's Ackermann).
double insideLock(const Car& c, const Wheel& wh) {
    const double track = std::fabs(c.restLocal[wh.outer].z + c.restLocal[wh.inner].z);
    return std::atan(1.0 / (1.0 / std::tan(0.6) - 0.5 * track / c.wheelbase * 0.7));
}
double frameSet(const Car& c) {
    double worst = 0;
    for (const Beam& b : c.beams)
        if (b.type == BEAM_FRAME) worst = std::max(worst, b.broken ? 1.0 : std::fabs(b.L0 - b.Lorig) / b.Lorig);
    return worst * 100;
}
// Front wheels' steer now, and the worst change of either from `start` (deg).
double toeMoved(const Car& c, const double start[2]) {
    double off = 0;
    int k = 0;
    for (const Wheel& wh : c.wheels)
        if (wh.front && k < 2) off = std::max(off, std::fabs(steerOf(c, wh) - start[k++]) * 180 / M_PI);
    return off;
}
void frontSteers(const Car& c, double out[2]) {
    int k = 0;
    for (const Wheel& wh : c.wheels)
        if (wh.front && k < 2) out[k++] = steerOf(c, wh);
}

struct Run {
    const VehicleSpec* spec;
    std::string tag;
    Surface surf;
    bool lockers;
    double start[2] = {0, 0};  // front wheels' steer at rest, before
    double past = 0;           // furthest any front wheel turned past the inside lock (deg)
};

}  // namespace

SCENARIO(steeringStops, "components/steering-stops",
         "Sandbox 4Runner (37s), stock 4Runner and Bronco at full lock in 4L flat out, open and locked: no front wheel turns more than a few degrees past full lock or stays turned; light rock knocks leave the toe alone") {
    std::vector<Run> runs;
    for (const VehicleSpec* sp : {&sandbox4Runner(), &vehicleCatalog()[3], &vehicleCatalog()[2]})
        for (Surface s : {SURF_PAVED, SURF_DIRT})
            for (bool lk : {false, true}) {
                if (sp != &sandbox4Runner() && s == SURF_DIRT) continue;  // (the 37s on both; the others on pavement)
                Run r;
                r.spec = sp, r.surf = s, r.lockers = lk;
                r.tag = std::string(sp->id) + (sp->wheelRadius > 0.46 ? " on 37s" : "") + (s == SURF_PAVED ? ", pavement" : ", dirt") + (lk ? ", lockers" : "");
                runs.push_back(r);
            }
    // One at a time on an open stretch of the pad (dirt painted on it where it's wanted): full left
    // lock, pedal down, then full right, then straight and stopped. (Unpatched, the 37s' inside
    // knuckle folded over within ~0.6 s.)
    for (Run& r : runs) {
        ctx.sim.cars.clear(), ctx.sim.ropes.clear(), ctx.sim.player = 0;
        ctx.sim.world = World(MAP_PROVING_GROUND);
        World& w = ctx.sim.world;
        if (r.surf != SURF_PAVED)
            for (double x = -230; x < -150; x += 1)
                for (double z = 30; z < 90; z += 1) {
                    const int id = w.cellIndex(x, z);
                    if (id >= 0) w.setCellSurface(id, r.surf, PAINT_TRAIL);
                }
        w.buildGrid();
        Car& c = ctx.add(*r.spec, -190, 60, 0);
        ctx.focus(c);
        ctx.run(1.0);
        c.setDriveMode((int)driveModes(r.spec->transfer).size() - 1);  // 4L (LOCK)
        c.tractionControl = false;
        if (r.lockers) c.setLocker(true, true), c.setLocker(false, true);
        frontSteers(c, r.start);
        auto watch = [&] {
            for (const Wheel& wh : c.wheels)
                if (wh.front) r.past = std::max(r.past, (std::fabs(steerOf(c, wh)) - insideLock(c, wh)) * 180 / M_PI);
        };
        for (double dir : {1.0, -1.0})
            ctx.run(4.0, [&] {
                watch();
                c.steer = 0.6 * dir, c.throttle = 1, c.brake = 0, c.handbrake = 0;
            });
        ctx.run(2.0, [&] {
            watch();
            c.steer = 0, c.throttle = 0, c.brake = 1;
        });
        ctx.metric(r.tag + ": furthest a front wheel turned past full lock", r.past, "deg", -5, 8);
        ctx.metric(r.tag + ": front toe off where it started, straightened out", toeMoved(c, r.start), "deg", 0, 1.5);
        ctx.metric(r.tag + ": frame set", frameSet(c), "%", 0, 1);
    }

    // Light knocks: a front tire over a 15 or 25 cm rock at 35 and 50 km/h leaves the toe where it was.
    ctx.sim.cars.clear(), ctx.sim.ropes.clear(), ctx.sim.player = 0;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    struct Knock {
        double kmh, top, z;
        Car* c;
        double start[2];
    };
    std::vector<Knock> knocks;
    double z = -160;
    for (double kmh : {35.0, 50.0})
        for (double top : {0.15, 0.25}) {
            ctx.add(sandbox4Runner(), -200, z, 0);
            knocks.push_back({kmh, top, z, nullptr, {0, 0}});
            z += 12;
        }
    for (size_t i = 0; i < knocks.size(); i++) knocks[i].c = &ctx.car((int)i);
    ctx.focus(*knocks[0].c);
    ctx.run(1.0);
    for (Knock& k : knocks) {
        Car& c = *k.c;
        frontSteers(c, k.start);
        double wz = 0;
        for (const Wheel& wh : c.wheels)
            if (wh.front && wh.left) wz = c.nodes[wh.outer].p.z - c.frame().com.z;
        ctx.sim.world.addRock(-150, k.z + wz, V3(0.3, k.top, 0.3), k.top, 51, 1);
        ctx.launch(c, k.kmh / 3.6);
    }
    ctx.sim.world.buildGrid();
    ctx.run(8.0, [&] {
        for (Knock& k : knocks) {
            Car& c = *k.c;
            const Frame f = c.frame();
            if (f.com.x > -130) {
                c.throttle = 0, c.brake = 0.8, c.steer = 0;
                continue;
            }
            const V3 d = V3(f.com.x + 10, f.com.y, k.z) - f.com;
            c.steer = clampd(std::atan2(dot(d, f.left), dot(d, f.fwd)), -0.2, 0.2);
            c.throttle = clampd((k.kmh - dot(f.vel, f.fwd) * 3.6) * 0.2, 0, 1), c.brake = 0;
        }
    });
    ctx.run(1.5, [&] {
        for (Knock& k : knocks) k.c->throttle = 0, k.c->brake = 1, k.c->steer = 0;
    });
    for (Knock& k : knocks) {
        char name[128];
        std::snprintf(name, sizeof name, "4runner on 37s over a %.0f cm rock at %.0f km/h", k.top * 100, k.kmh);
        ctx.check(std::string(name) + ": went over it", k.c->frame().com.x > -146);
        ctx.metric(std::string(name) + ": front toe moved", toeMoved(*k.c, k.start), "deg", 0, 0.5);
    }
}
