// The Wilderness (wilderness.cpp): a world generated from a seed. Every seed has to give a world
// that works: stations, places, a river with water and fords where the roads cross it, a lake
// the roads go round, and roads a truck can drive.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/autopilot.h"
#include "harness.h"

namespace {

double checksum(const World& w) {
    double s = 0;
    for (double x = -500; x <= 500; x += 37)
        for (double z = -500; z <= 500; z += 41) s += w.height(x, z) * (1 + 0.001 * (x + z));
    return s;
}

const Landmark* findPlace(const World& w, const char* name) {
    for (const Landmark& l : w.landmarks)
        if (l.name == name) return &l;
    return nullptr;
}

// Runs the autopilot to `goal`; whether it arrived (and the worst damage on the way).
bool pilotTo(Ctx& ctx, Car& c, const V3& goal, double kmh, double maxS, int route, double& worst) {
    const std::string msg = pilotStart(c, ctx.sim.world, goal, "THERE", kmh / 3.6, route);
    ctx.check("route planned", c.pilot.active, msg);
    const int i = (int)(&c - ctx.sim.cars.data());
    ctx.runUntil(maxS, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(i, Ctx::kFrame);
        worst = std::max(worst, c.damage);
    });
    return c.pilot.arrived;
}

}  // namespace

SCENARIO(wildernessGenerate, "wilderness/generate", "Five seeds: each builds in reasonable time with four stations, its places and caches on dry ground, a river and a lake, no deep water across a road; the same seed makes the same world") {
    for (uint32_t seed : {1u, 2u, 3u, 7u, 42u}) {
        const auto t0 = std::chrono::steady_clock::now();
        const World w(MAP_WILDERNESS, seed);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::string tag = "seed " + std::to_string(seed) + ": ";
        int caches = 0, riverCells = 0, deepRoad = 0, roadCells = 0;
        std::string bad;
        for (const Landmark& l : w.landmarks) {
            caches += l.cache >= 0;
            if (w.waterLevel(l.p.x, l.p.z) > w.height(l.p.x, l.p.z) - 0.05) bad += " wet:" + l.name;
            if (std::fabs(l.p.x) > 540 || std::fabs(l.p.z) > 540) bad += " off-map:" + l.name;
        }
        const int N = w.gridSize();
        for (int i = 0; i < N; i += 2)
            for (int j = 0; j < N; j += 2) {
                const double x = -World::kHalfSize + i, z = -World::kHalfSize + j;
                if (w.waterLevel(x, z) > w.height(x, z) && len(w.waterFlow(x, z)) > 0.05) riverCells++;
            }
        // Along every road's centre line: never more than wading depth.
        for (const Route& r : w.routes) {
            if (!r.road) continue;
            for (const V3& p : r.pts) {
                roadCells++;
                const double depth = w.waterLevel(p.x, p.z) - w.height(p.x, p.z);
                if (depth > 0.6) {
                    if (std::getenv("WILD_DEBUG") && deepRoad < 3) std::printf("    deep on %s at (%.0f, %.0f): %.2f m, flow %.2f, lake (%.0f, %.0f) r %.0f\n", r.name.c_str(), p.x, p.z, depth, len(w.waterFlow(p.x, p.z)), w.waters[0].x, w.waters[0].z, w.waters[0].r);
                    deepRoad++;
                }
            }
        }
        riverCells *= 4;
        std::printf("  seed %u: %.2f s, %zu stations, %zu places (%d caches), %zu trees, %zu rocks, river %d cells, lake %s, %d m of road (%d deep), spawn (%.0f, %.0f)\n", seed, secs, w.stations.size(),
                    w.landmarks.size(), caches, w.trees.size(), w.rocks.size(), riverCells, w.waters.empty() ? "none" : "yes", roadCells, deepRoad, w.spawn.x, w.spawn.z);
        ctx.metric(tag + "build time", secs, "s", 0, 6);
        ctx.check(tag + "four stations", w.stations.size() == 4, std::to_string(w.stations.size()));
        ctx.check(tag + "eight or more caches, ten or more places", caches >= 8 && w.landmarks.size() >= 10, std::to_string(caches) + " caches");
        ctx.check(tag + "places on dry ground, on the map", bad.empty(), bad);
        ctx.check(tag + "a river with flowing water, and a lake", riverCells > 400 && !w.waters.empty(), std::to_string(riverCells));
        ctx.check(tag + "no deep water across a road (fords only)", roadCells > 1200 && deepRoad == 0, std::to_string(deepRoad) + " of " + std::to_string(roadCells));
        bool onPad = false;
        for (const Station& st : w.stations) onPad |= std::hypot(st.p.x - w.spawn.x, st.p.z - w.spawn.z) < 20;
        ctx.check(tag + "you start at base camp", onPad);
    }
    const World a(MAP_WILDERNESS, 5), b(MAP_WILDERNESS, 5), c(MAP_WILDERNESS, 6);
    ctx.check("the same seed, the same world", checksum(a) == checksum(b) && a.landmarks.size() == b.landmarks.size() && a.trees.size() == b.trees.size());
    ctx.check("another seed, another world", std::fabs(checksum(a) - checksum(c)) > 100);
}

SCENARIO(wildernessDrive, "wilderness/drive", "Seed 1: the autopilot drives a Bronco from base camp round the road loop to the desert outpost and on to the lodge, fording the river where it crosses") {
    ctx.sim.world = World(MAP_WILDERNESS, 1);
    const World& w = ctx.sim.world;
    Car& c = ctx.add(vehicleCatalog()[2], w.spawn.x, w.spawn.z, w.spawnYaw * 180 / M_PI);
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    for (const char* stop : {"DESERT OUTPOST", "LAKESIDE LODGE"}) {
        const Landmark* l = findPlace(w, stop);
        ctx.check(std::string("there's a ") + stop, l != nullptr);
        if (!l) return;
        const bool ok = pilotTo(ctx, c, l->p, 45, 260, PILOT_ROADS, worst);
        ctx.check(std::string("arrived at the ") + stop, ok, c.pilot.status);
    }
    ctx.check("engine still running", !c.engineDead);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.5);
}

SCENARIO(wildernessSummit, "wilderness/summit", "Seed 1: the autopilot takes a Bronco up the summit trail from the mountain hut to the lookout") {
    ctx.sim.world = World(MAP_WILDERNESS, 1);
    const World& w = ctx.sim.world;
    const Landmark *hut = findPlace(w, "MOUNTAIN HUT"), *top = findPlace(w, "SUMMIT LOOKOUT");
    ctx.check("a hut and a summit", hut && top);
    if (!hut || !top) return;
    Car& c = ctx.add(vehicleCatalog()[2], hut->p.x, hut->p.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotTo(ctx, c, top->p, 25, 400, PILOT_TRAILS, worst);
    ctx.check("at the lookout", ok, c.pilot.status);
    ctx.metric("climbed", top->p.y - hut->p.y, "m", 50, 200);
    ctx.metric("damage on the way", worst * 100, "%", 0, 2);
}
