// Traffic LOD: an AI car driving far from everyone is a rigid proxy on the road (no soft-body step); it wakes into the
// full sim well before anything can touch it, with its damage kept and no jump in position or velocity.
#include <cmath>

#include "harness.h"

namespace {
const VehicleSpec& blazer() { return vehicleCatalog()[7]; }

struct Setup {
    Car* player = nullptr;
    int idx = 0;
};

// The player parked well away from the loop; the AI car on the loop at the sample farthest from them.
Car& addTraffic(Ctx& ctx, Setup& s, bool lod, int pick = 0) {
    Sim& sim = ctx.sim;
    sim.trafficLod = lod;
    s.player = &ctx.add(blazer(), -100, -60, 0);
    sim.player = 0;
    s.player->handbrake = 1;
    const auto& road = sim.world.loopRoad;
    double best = 0;
    for (int i = 0; i < (int)road.size(); i += 10) {
        const double d = len(V3(road[i].x + 100, 0, road[i].z + 60));
        if (d > best) best = d, s.idx = i;
    }
    s.idx = (s.idx + pick * 20) % (int)road.size();
    const int n = (int)road.size();
    const V3 d = road[(s.idx + 1) % n] - road[s.idx];
    const V3 t = norm(V3(d.x, 0, d.z));
    const V3 p = road[s.idx] + V3(-t.z, 0, t.x) * 2.6;
    Car& c = ctx.add(blazer(), p.x, p.z, std::atan2(-t.z, t.x) * 180 / M_PI, true);
    c.aiDir = 1, c.aiIndex = s.idx;
    return c;
}

double lateral(const World& w, const Car& c) {
    const auto& road = w.loopRoad;
    const int n = (int)road.size();
    int best = 0;
    double bd = 1e18;
    const V3 p = c.frame().com;
    for (int i = 0; i < n; i++) {
        const double d = (road[i].x - p.x) * (road[i].x - p.x) + (road[i].z - p.z) * (road[i].z - p.z);
        if (d < bd) bd = d, best = i;
    }
    const V3 t = norm(road[(best + 1) % n] - road[best]);
    return (p.x - road[best].x) * -t.z + (p.z - road[best].z) * t.x;
}
}  // namespace

SCENARIO(lodProxy, "trafficlod/far-car-proxy", "A far AI car becomes a proxy, keeps to its lane and keeps moving, and the soft body stays asleep") {
    Setup s;
    Car& c = addTraffic(ctx, s, true);
    ctx.run(12);
    ctx.check("became a proxy", c.proxy && c.sleeping);
    double worst = 0, v = 0, dist = 0;
    V3 last = c.frame().com;
    ctx.run(15, [&] {
        worst = std::max(worst, std::fabs(lateral(ctx.sim.world, c) - 2.6));
        v = len(c.frame().vel);
        dist += len(c.frame().com - last);
        last = c.frame().com;
    });
    ctx.check("still a proxy", c.proxy);
    ctx.metric("worst distance off its lane", worst, "m", 0, 1.2);
    ctx.metric("speed", v, "m/s", 5, 40);
    ctx.metric("covered in 15 s", dist, "m", 80, 600);
    ctx.check("upright", c.frame().up.y > 0.9);
    ctx.check("not frozen nor flying (nodes stay on the terrain)", c.frame().com.y > ctx.sim.world.height(last.x, last.z) && c.frame().com.y < ctx.sim.world.height(last.x, last.z) + 3);
    // With the LOD off, the same car never sleeps while it drives.
    Sim off;
    off.setupEmpty();
    Ctx octx(off);
    Setup so;
    Car& d = addTraffic(octx, so, false);
    octx.run(12);
    ctx.check("with the LOD off it stays a full soft body", !d.proxy && !d.sleeping);
}

SCENARIO(lodWakeWatcher, "trafficlod/wake-before-contact", "A player closing at 40 m/s wakes the proxy long before they could touch it") {
    Setup s;
    Car& c = addTraffic(ctx, s, true);
    ctx.run(12);
    ctx.check("proxy first", c.proxy);
    V3 at = c.frame().com;
    V3 from = at + V3(1, 0, 0) * 400;
    double woke = -1, dist = 1e9;
    for (int k = 0; k < 600 && woke < 0; k++) {
        const V3 w = from + (c.frame().com - from) * (k / 600.0 * 1.0);
        ctx.sim.lodWatch = {w};
        ctx.step();
        if (!c.proxy) woke = len(w - c.frame().com);
        (void)dist;
    }
    ctx.sim.lodWatch.clear();
    ctx.metric("distance when it woke", woke, "m", 70, 400);
    ctx.check("woke", woke > 0);
}

namespace {
struct Obstacle {
    double woke = -1, dmgAI = 0, dmgParked = 0, minGap = 1e9;
    bool proxied = false;
};
// A parked car in the AI car's lane about 350 m ahead of where it is after 12 s.
Obstacle obstacleRun(bool lod) {
    Sim sim;
    sim.setupEmpty();
    Ctx cx(sim);
    Setup s;
    Car& c = addTraffic(cx, s, lod);
    cx.run(12);
    Obstacle r;
    r.proxied = c.proxy;
    const auto& road = sim.world.loopRoad;
    const int n = (int)road.size();
    int j = c.aiIndex;
    double acc = 0;
    while (acc < 350) {
        const int nx = (j + 1) % n;
        acc += len(road[nx] - road[j]);
        j = nx;
    }
    const V3 d = road[(j + 1) % n] - road[j];
    const V3 t = norm(V3(d.x, 0, d.z));
    const V3 p = road[j] + V3(-t.z, 0, t.x) * 2.6;
    Car& parked = cx.add(blazer(), p.x, p.z, std::atan2(-t.z, t.x) * 180 / M_PI);
    parked.handbrake = 1;
    cx.run(60, [&] {
        if (r.woke < 0 && !c.proxy) r.woke = len(c.frame().com - parked.frame().com);
        r.minGap = std::min(r.minGap, len(c.frame().com - parked.frame().com));
    });
    r.dmgAI = c.damage, r.dmgParked = parked.damage;
    return r;
}
}  // namespace

SCENARIO(lodWakeCar, "trafficlod/wake-for-obstacle", "A parked car in its lane ahead wakes the proxy early; it is no worse off than a car that never was a proxy") {
    const Obstacle on = obstacleRun(true), off = obstacleRun(false);
    ctx.check("it was a proxy before", on.proxied);
    ctx.metric("distance to the parked car when it woke", on.woke, "m", 60, 400);
    ctx.metric("closest approach, LOD on", on.minGap, "m", 0, 1000);
    ctx.metric("closest approach, LOD off", off.minGap, "m", 0, 1000);
    ctx.metric("damage to the AI car, LOD on", on.dmgAI * 100, "%", 0, 100);
    ctx.metric("damage to the AI car, LOD off", off.dmgAI * 100, "%", 0, 100);
    ctx.check("no more damage than without the LOD", on.dmgAI <= off.dmgAI + 0.005 && on.dmgParked <= off.dmgParked + 0.005);
}

SCENARIO(lodDamage, "trafficlod/damage-persists", "A dented car keeps its damage through the proxy state and out of it") {
    Setup s;
    Car& c = addTraffic(ctx, s, true);
    ctx.run(2);
    // A dent and a few snapped beams, as a crash leaves them.
    int bent = 0;
    for (Beam& b : c.beams)
        if (b.type == BEAM_BODY && !b.broken && bent < 6) b.broken = true, bent++;
    for (Beam& b : c.beams)  // (set as it would be: the rest length is where the nodes sit)
        if (b.type == BEAM_PANEL && !b.broken && bent < 12) b.L0 = len(c.nodes[b.a].p - c.nodes[b.b].p) * 0.995, bent++;
    ctx.runUntil(15, [&] { return c.proxy; });
    ctx.check("became a proxy", c.proxy);
    std::vector<double> L0;
    std::vector<char> broken;
    for (const Beam& b : c.beams) L0.push_back(b.L0), broken.push_back(b.broken);
    ctx.run(5);
    ctx.check("still a proxy", c.proxy);
    const double dmg = c.damage;  // (the stats have settled to the dents by now)
    const int snapped = c.brokenBeams;
    ctx.sim.lodWatch = {c.frame().com};
    ctx.step();
    ctx.sim.lodWatch.clear();
    ctx.check("woke again", !c.proxy);
    bool same = true;
    for (size_t k = 0; k < c.beams.size(); k++)  // (a tie rod's length is the steering's, not damage)
        same &= (char)c.beams[k].broken == broken[k] && (c.beams[k].type == BEAM_TIEROD || std::fabs(c.beams[k].L0 - L0[k]) < 5e-4);
    ctx.check("every beam as it was left", same);
    ctx.check("damage and snapped beams as they were", std::fabs(c.damage - dmg) < 0.002 && c.brokenBeams == snapped && snapped > 0, std::to_string(dmg) + " -> " + std::to_string(c.damage));
}

SCENARIO(lodHandover, "trafficlod/no-pop", "No jump in position or velocity when it becomes a proxy or wakes") {
    Setup s;
    Car& c = addTraffic(ctx, s, true);
    double popPos = 0, popVel = 0, maxSpeed = 0;
    bool sawProxy = false, sawWake = false, was = false;
    auto track = [&] {
        const Frame f0 = c.frame();
        ctx.step();
        const Frame f1 = c.frame();
        if (ctx.t > 1.5) {  // (it's still settling onto its wheels before that)
            popPos = std::max(popPos, len(f1.com - f0.com - (f0.vel + f1.vel) * 0.5 * Ctx::kFrame));
            popVel = std::max(popVel, len(f1.vel - f0.vel));
        }
        maxSpeed = std::max(maxSpeed, len(f1.vel));
        sawProxy |= c.proxy;
        sawWake |= was && !c.proxy;
        was = c.proxy;
    };
    while (ctx.t < 14) track();
    // Someone drives up: it wakes, and carries on.
    for (int k = 0; k < 240; k++) {
        ctx.sim.lodWatch = {c.frame().com + V3(1, 0, 0) * (130 - k * 0.5)};
        track();
    }
    ctx.sim.lodWatch.clear();
    ctx.check("was a proxy", sawProxy);
    ctx.check("woke up", sawWake);
    ctx.metric("worst position jump over a frame (beyond its own travel)", popPos, "m", 0, 0.12);
    ctx.metric("worst velocity jump over a frame", popVel, "m/s", 0, 0.8);
    ctx.metric("top speed", maxSpeed, "m/s", 3, 45);
}

SCENARIO(lodHit, "trafficlod/hit-takes-full-damage", "A car that was a proxy and is hit takes the crash a never-proxied car takes") {
    // (A rear-ender's crumple picks its own buckling order: one hit took 6% or 14% by a
    // hair's difference. Each side is the mean of three, the hitter a little left, dead
    // behind and a little right.)
    double dmg[2] = {0, 0}, dmgB[2] = {0, 0};
    bool hurt = true;
    for (int lod = 0; lod < 2; lod++)
        for (double off : {-0.2, 0.0, 0.2}) {
            Sim sim;
            sim.setupEmpty();
            Ctx cx(sim);
            Setup s;
            Car& c = addTraffic(cx, s, lod == 1);
            cx.run(12);
            if (lod) cx.check("proxy before the hit", c.proxy);
            if (lod) {  // somebody comes near: it wakes (and nobody is driving it after)
                sim.lodWatch = {c.frame().com};
                cx.run(0.1);
                sim.lodWatch.clear();
                cx.check("woke", !c.proxy);
            }
            c.ai = false;
            // Both hit in the same place: the pad, level. (Where each had got to along the loop
            // road was ~20 m apart on a 13% downhill, launched level off it: two different
            // crashes, ~2x apart in damage, whichever had been a proxy.)
            c.placeUpright(sim.world, -120, -50, 0);
            cx.run(1.0);
            c.handbrake = 0;
            cx.launch(c, 20);
            c.throttle = c.brake = 0;
            // A Blazer 14 m behind it, closing at 50 km/h.
            const Frame f = c.frame();
            const V3 back = norm(V3(f.fwd.x, 0, f.fwd.z)), left(back.z, 0, -back.x);
            const V3 p = f.com - back * 14 + left * off;
            Car& b = cx.add(blazer(), p.x, p.z, std::atan2(-back.z, back.x) * 180 / M_PI);
            cx.launch(b, 20 + 50 / 3.6);
            cx.run(3);
            hurt &= c.damage > 0.01;
            dmg[lod] += c.damage / 3, dmgB[lod] += b.damage / 3;
        }
    ctx.check("every crash hurts", hurt);
    ctx.metric("hit car's damage, was a proxy / never", dmg[1] / std::max(dmg[0], 1e-6), "", 0.6, 1.6);
    ctx.metric("hitter's damage, proxy minus never", dmgB[1] - dmgB[0], "", -0.2, 0.2);
}

namespace {
// Mean speed (m/s) of the AI car over 12..72 s.
double meanSpeed(bool lod) {
    Sim sim;
    sim.setupEmpty();
    Ctx cx(sim);
    Setup s;
    Car& c = addTraffic(cx, s, lod);
    double sum = 0;
    int n = 0;
    cx.run(72, [&] {
        if (cx.t > 12) sum += len(c.frame().vel), n++;
    });
    return sum / std::max(n, 1);
}
}  // namespace

SCENARIO(lodSpeed, "trafficlod/same-speed", "A proxy's mean speed along the road is within 5% of the full sim's") {
    const double on = meanSpeed(true), off = meanSpeed(false);
    ctx.metric("mean speed, proxy", on, "m/s", 5, 40);
    ctx.metric("mean speed, full sim", off, "m/s", 5, 40);
    ctx.check("within 5%", std::fabs(on - off) < 0.05 * off, std::to_string(on) + " vs " + std::to_string(off));
}
