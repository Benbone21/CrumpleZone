// THROWAWAY: why doesn't the car drive in a generated level? Load the level like the game does,
// put a car at its start, full throttle, and report what's going on.
#include <cmath>
#include <cstdio>
#include "harness.h"
#include "level.h"
namespace {
void check(Ctx& ctx, const std::string& file, int model) {
    Level lv;
    std::string err;
    if (!loadLevelFile(file, lv, &err)) { std::printf("LOAD FAIL %s\n", err.c_str()); return; }
    ctx.sim.world = World(lv.map);
    applyLevel(lv, ctx.sim.world);
    World& w = ctx.sim.world;
    std::printf("LEVEL %s start (%.1f, %.1f) yaw %.2f: ground %.2f clearY %.2f water %.2f surface %s\n", lv.name.c_str(), lv.start.x, lv.start.z,
                lv.startYaw, w.height(lv.start.x, lv.start.z), w.clearY(lv.start.x, lv.start.z), w.waterLevel(lv.start.x, lv.start.z),
                surfaceInfo(w.surfaceAt(lv.start.x, lv.start.z)).name);
    Car& c = ctx.add(vehicleCatalog()[model], lv.start.x, lv.start.z, 0);
    c.respawn(V3(lv.start.x, w.clearY(lv.start.x, lv.start.z) + 0.05, lv.start.z), lv.startYaw);
    c.setDown(w);
    ctx.run(1);
    const V3 p0 = c.frame().com;
    ctx.run(8, [&] { c.throttle = 1, c.brake = 0, c.handbrake = 0, c.steer = 0; });
    const V3 p1 = c.frame().com;
    std::printf("  %s: com y %.2f (ground %.2f), moved %.1f m, fwd (%.2f, %.2f), up.y %.2f, gear %d rpm %.0f engineDead %d\n", vehicleCatalog()[model].name,
                p0.y, w.height(p0.x, p0.z), std::hypot(p1.x - p0.x, p1.z - p0.z), c.frame().fwd.x, c.frame().fwd.z, c.frame().up.y, c.gear, c.rpm, (int)c.engineDead);
}
int reg = [] {
    for (const char* n : {"wall-1", "mud-1", "hill-1", "dunes-1"})
        for (int m : {0, 2})
            scenarioRegistry().push_back({std::string("lvcheck/") + n + "-" + std::to_string(m), n, [n, m](Ctx& c) {
                check(c, std::string(getenv("HOME")) + "/.config/crumplezone/levels/gen-" + n + ".czl", m);
            }});
    return 0;
}();
}
