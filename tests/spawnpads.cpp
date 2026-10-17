// Spawn pads (spawnpad.h): wherever the game puts a vehicle down, it's a flat concrete slab with a ramp down to the
// ground ahead: each map's start, the section park's entries, the grid at a level's start. A truck set down there settles
// level on concrete with nothing in or on it, stays put, and drives off the end of the pad 30 m without touching anything.
#include <cmath>
#include <cstdio>

#include "../src/level.h"
#include "../src/levelrun.h"
#include "../src/spawnpad.h"
#include "harness.h"

namespace {

const VehicleSpec& truck() { return vehicleCatalog()[2]; }

const VehicleSpec& longest() {
    const VehicleSpec* best = &vehicleCatalog()[0];
    for (const VehicleSpec& s : vehicleCatalog())
        if (s.length > best->length) best = &s;
    return *best;
}

// The corners and middle of a vehicle's footprint set down at p facing yaw.
std::vector<V3> footprint(const VehicleSpec& s, const V3& p, double yaw) {
    std::vector<V3> out;
    const V3 f(std::cos(yaw), 0, -std::sin(yaw)), l(std::sin(yaw), 0, std::cos(yaw));
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++) out.push_back(p + f * (a * s.length / 2) + l * (b * s.width / 2));
    return out;
}

// What the ground there is like before anything drives on it. Returns why not, "" if fine.
std::string groundProblem(const World& w, const VehicleSpec& s, const V3& p, double yaw) {
    double lo = 1e9, hi = -1e9;
    for (const V3& q : footprint(s, p, yaw)) {
        const double h = w.height(q.x, q.z);
        lo = std::min(lo, h), hi = std::max(hi, h);
        if (w.surfaceAt(q.x, q.z) != SURF_PAVED) return "not concrete at " + std::to_string(q.x) + "," + std::to_string(q.z);
        if (w.waterLevel(q.x, q.z) > h - 0.05) return "under water";
        if (w.clearY(q.x, q.z) > h + 0.12) return "something solid on it";
    }
    if (hi - lo > 0.15) return "not level: " + std::to_string(hi - lo) + " m across the truck";
    return "";
}

double pitchDeg(const Car& c) { return std::asin(clampd(c.frame().fwd.y, -1, 1)) * 180 / M_PI; }
double rollDeg(const Car& c) { return std::asin(clampd(c.frame().left.y, -1, 1)) * 180 / M_PI; }

// One truck set down at p facing yaw on ctx's world: settles level, stays put, drives 30 m off the pad.
void tryStart(Ctx& ctx, const std::string& tag, const V3& p, double yaw, double dist = 30) {
    const std::string why = groundProblem(ctx.sim.world, truck(), p, yaw);
    ctx.check(tag + ": the ground is concrete, level and clear", why.empty(), why);
    Car& c = ctx.add(truck(), p.x, p.z, yaw * 180 / M_PI);
    ctx.run(3.0);
    ctx.check(tag + ": settles level", std::fabs(pitchDeg(c)) < 3 && std::fabs(rollDeg(c)) < 3, "pitch " + std::to_string(pitchDeg(c)) + " roll " + std::to_string(rollDeg(c)));
    const V3 f0 = c.frame().com;
    ctx.run(1.0);
    ctx.check(tag + ": stays put", len(c.frame().com - f0) < 0.3, std::to_string(len(c.frame().com - f0)));
    // Off the pad: straight ahead (`dist` m: a level's or section's own obstacles begin not far past its start).
    const V3 dir(std::cos(yaw), 0, -std::sin(yaw)), start = c.frame().com;
    double worst = 0;
    ctx.runUntil(14, [&] { return dot(c.frame().com - start, dir) > dist; }, [&] {
        const Frame f = c.frame();
        const V3 aim = start + dir * (dot(f.com - start, dir) + 12);
        c.throttle = 0.7, c.brake = 0, c.handbrake = 0;
        c.steer = clampd(std::atan2(dot(aim - f.com, f.left), 12), -0.3, 0.3);
        worst = std::max({worst, std::fabs(pitchDeg(c)), std::fabs(rollDeg(c))});
    });
    ctx.check(tag + ": drives " + std::to_string((int)dist) + " m ahead", dot(c.frame().com - start, dir) > dist, std::to_string(dot(c.frame().com - start, dir)) + " m");
    ctx.check(tag + ": without hitting anything", c.damage < 0.02 && worst < 25, "damage " + std::to_string(c.damage) + " tilt " + std::to_string(worst));
}

}  // namespace

SCENARIO(spawnPadMaps, "spawnpads/maps", "Each map's start is a slab: concrete, level and clear, the truck settles on it and drives off it; every place in a multiplayer grid is on it too") {
    for (int map : {MAP_PROVING_GROUND, MAP_MOUNTAIN, MAP_FRONTIER, MAP_BACKCOUNTRY, MAP_FREEWAY}) {
        const bool was = World::withShowcase;
        World::withShowcase = true;  // (the proving ground as the game builds it)
        ctx.sim.world = World(map);
        World::withShowcase = was;
        ctx.sim.cars.clear();
        const World& w = ctx.sim.world;
        const std::string tag = std::string(mapName(map)) + " start";
        V3 p;
        double yaw;
        if (map == MAP_FREEWAY) {  // (a lane of the road itself)
            ctx.check(tag + ": no pad on the road", w.spawnPads.empty());
            p = w.spawn, yaw = w.spawnYaw;
        } else {
            ctx.check(tag + ": has a pad", !w.spawnPads.empty() && w.spawnSlot(0, p, yaw));
            ctx.check(tag + ": the start is on it", std::hypot(p.x - w.spawn.x, p.z - w.spawn.z) < 0.5);
            const SpawnPad* pad = nullptr;
            for (const SpawnPad& sp : w.spawnPads)
                if (std::hypot(sp.p.x - w.spawn.x, sp.p.z - w.spawn.z) < 0.5) pad = &sp;
            ctx.check(tag + ": room for eight", pad && pad->slots >= 8);
            ctx.check(tag + ": the pad is level with its start", std::fabs(w.height(p.x, p.z) - w.spawn.y) < 0.05);
            yaw = w.spawnYaw;
        }
        tryStart(ctx, tag, p, yaw);
        // The whole grid at once: each place level, concrete and clear, and nobody inside anybody.
        if (!w.spawnPads.empty()) {
            ctx.sim.cars.clear();
            bool grounded = true;
            for (int k = 0; k < 8; k++) {
                V3 q;
                double qy;
                ctx.check(tag + ": place " + std::to_string(k), w.spawnSlot(k, q, qy));
                const std::string why = groundProblem(w, longest(), q, qy);
                if (!why.empty()) grounded = false, ctx.check(tag + ": place " + std::to_string(k) + " ground", false, why);
                ctx.add(longest(), q.x, q.z, qy * 180 / M_PI);
            }
            ctx.check(tag + ": the grid's ground is concrete, level and clear", grounded);
            ctx.run(3.0);
            bool level = true;
            double moved = 0;
            for (Car& c : ctx.sim.cars) level &= std::fabs(pitchDeg(c)) < 3 && std::fabs(rollDeg(c)) < 3;
            for (size_t i = 0; i < ctx.sim.cars.size(); i++)
                for (size_t j = i + 1; j < ctx.sim.cars.size(); j++) moved += intrusions(ctx.sim.cars[i], ctx.sim.cars[j]) + intrusions(ctx.sim.cars[j], ctx.sim.cars[i]);
            ctx.check(tag + ": the grid settles level", level);
            ctx.check(tag + ": nobody inside anybody", moved == 0, std::to_string(moved));
        }
    }
}

SCENARIO(spawnPadSections, "spawnpads/sections", "The section park's entries each have a pad a little before them, and a truck starts from it level and drives on into the section") {
    const bool was = World::withShowcase;
    World::withShowcase = true;
    ctx.sim.world = World(MAP_PROVING_GROUND);
    World::withShowcase = was;
    const World& w = ctx.sim.world;
    ctx.check("seven sections", w.sections.size() == 7, std::to_string(w.sections.size()));
    for (const World::SectionSpot& s : w.sections) {
        ctx.sim.cars.clear();
        const std::string tag = s.name + " start";
        const V3 f(std::cos(s.spawnYaw), 0, -std::sin(s.spawnYaw));
        ctx.check(tag + ": before the entry, facing it", dot(s.entry - s.spawn, f) > 6 && dot(s.entry - s.spawn, f) < 25);
        tryStart(ctx, tag, s.spawn, s.spawnYaw, dot(s.entry - s.spawn, f) + 4);  // (to the entry and a little in)
    }
}

SCENARIO(spawnPadLevels, "spawnpads/levels", "The game's own levels start on a pad: the grid's places are on it, level and concrete, and a truck drives off it") {
    for (const Level& lv : builtinLevels()) {
        World w(lv.map);
        applyLevel(lv, w);
        ctx.sim.world = w;
        ctx.sim.cars.clear();
        const std::string tag = lv.name + " start";
        V3 q0;
        double y0;
        ctx.check(tag + ": a pad", ctx.sim.world.spawnSlot(0, q0, y0) && ctx.sim.world.spawnPads.back().slots >= 8);
        for (int k : {0, 1, 7}) {
            V3 p;
            double yaw;
            LevelRun::gridSlot(lv, k, p, yaw);
            const std::string why = groundProblem(ctx.sim.world, longest(), p, yaw);
            ctx.check(tag + ": place " + std::to_string(k) + " is concrete, level and clear", why.empty(), why);
        }
        V3 p;
        double yaw;
        LevelRun::gridSlot(lv, 0, p, yaw);
        // As far as 30 m, or to just short of the first gate (a level's own obstacles begin about there).
        const V3 f(std::cos(yaw), 0, -std::sin(yaw));
        tryStart(ctx, tag, p, yaw, clampd(dot(lv.gates[0].p - p, f) - 3, 8, 30));
    }
}
