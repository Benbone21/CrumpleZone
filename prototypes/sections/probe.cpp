// THROWAWAY spike probes: can the world's primitives make obstacles that really stop a truck?
// Linked against the game's physics objects + the test runner; never part of the repo.
#include <cmath>
#include <cstdio>
#include <string>

#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }
const VehicleSpec& buggy() { return vehicleCatalog()[4]; }

// A blank test region: everything static removed, terrain flat at y = 0 over [-560, -200]^2,
// every cell `surf`. Features are then written in by the probe.
World& blank(Ctx& ctx, Surface surf) {
    World& w = ctx.sim.world;
    w.rocks.clear(), w.trees.clear(), w.poles.clear(), w.boxes.clear(), w.logs.clear(), w.bushes.clear();
    w.stations.clear(), w.fixtures.clear();
    w.crashProps = false;
    for (double x = -560; x <= -200; x += 1)
        for (double z = -560; z <= -200; z += 1) {
            int id = w.cellIndex(x, z);
            if (id < 0) continue;
            w.setCellHeight(id, 0);
            w.setCellSurface(id, surf, surf == SURF_ROCK ? PAINT_ROCK : surf == SURF_MUD ? PAINT_MUD : PAINT_TRAIL);
        }
    return w;
}
template <class F>
void shape(World& w, double x0, double x1, double z0, double z1, F h) {
    for (double x = x0; x <= x1; x += 1)
        for (double z = z0; z <= z1; z += 1) {
            int id = w.cellIndex(x, z);
            if (id >= 0) w.setCellHeight(id, (float)h(x, z));
        }
}

Car& setUp(Ctx& ctx, const VehicleSpec& base, double x, double z, double headingDeg, bool full) {
    VehicleSpec& spec = *new VehicleSpec(base);
    spec.lockerFront = full;
    Car& c = ctx.add(spec, x, z, headingDeg);
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    if (full) {
        c.setLocker(true, true), c.setLocker(false, true);
        c.tirePsi = 12;
        c.swayDisconnected = spec.swayDisconnect > 0;
    }
    c.tractionControl = false;
    return c;
}

// Drive along +X holding lane z (or don't hold: zLock = false lets it slide).
void drive(Car& c, double z, double speed, bool hold = true) {
    Frame fr = c.frame();
    V3 d = V3(fr.com.x + 6, fr.com.y, hold ? z : fr.com.z) - fr.com;
    c.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.5, -0.55, 0.55);
    double v = dot(fr.vel, fr.fwd);
    c.throttle = clampd(0.3 + (speed - v) * 0.9, 0, 1);
    c.brake = v > speed + 1.0 ? 0.3 : 0;
    c.handbrake = 0;
}

// ---- P1: does a grid wall seal a chokepoint? -------------------------------------------------
// A wall rising `H` m at `deg` degrees across the lane at x = -380, on rock (stickiest).
void wallProbe(Ctx& ctx, const VehicleSpec& spec, double deg, double H) {
    World& w = blank(ctx, SURF_ROCK);
    const double face = -380, run = H / std::tan(deg * M_PI / 180);
    shape(w, face, face + run + 30, -560, -200, [&](double x, double) { return std::min(H, std::max(0.0, (x - face) / run * H)); });
    w.buildGrid();
    Car& c = setUp(ctx, spec, face - 15, -380, 0, true);
    double xMax = -1e9;
    ctx.run(45, [&] {
        drive(c, -380, 1.2);
        xMax = std::max(xMax, c.frame().com.x);
    });
    double climbed = std::min(H, std::max(0.0, (xMax - face) / run * H));
    std::printf("PROBE wall %-8s %4.0f deg %4.1f m: reached height %.2f m %s\n", spec.name, deg, H, climbed,
                xMax > face + run + 2 ? "OVER THE TOP" : "held");
    ctx.metric("height reached", climbed, "m", 0, H);
}

// ---- P2: off-camber. A side slope falling toward +z at `deg`; drive along it at a crawl. ----
void camberProbe(Ctx& ctx, const VehicleSpec& spec, double deg, Surface surf, bool full) {
    World& w = blank(ctx, surf);
    const double g = std::tan(deg * M_PI / 180), zc = -380;
    shape(w, -560, -200, -560, -200, [&](double, double z) { return 30 - g * clampd(z - zc, -40, 40) * 1.0; });
    w.buildGrid();
    Car& c = setUp(ctx, spec, -460, zc, 0, full);
    const V3 start = c.frame().com;
    double minUp = 1, slide = 0;
    bool rolled = false;
    ctx.run(30, [&] {
        drive(c, zc, 1.5, true);
        Frame fr = c.frame();
        minUp = std::min(minUp, fr.up.y);
        rolled |= fr.up.y < 0.2;
    });
    slide = c.frame().com.z - start.z;
    const double along = c.frame().com.x - start.x;
    std::printf("PROBE camber %-8s %s %-5s %4.0f deg: along %5.1f m, slid downhill %5.1f m, max lean %4.0f deg %s\n", spec.name,
                full ? "SETUP" : "STOCK", surfaceInfo(surf).name, deg, along, slide, std::acos(clampd(minUp, -1, 1)) * 180 / M_PI,
                rolled ? "ROLLED" : "");
    ctx.metric("slide", slide, "m", -100, 100);
}

// ---- P3: bog. A mud pit `depth` deep, `len` long, banks 1:2, across the lane. Solo set-up truck.
void bogProbe(Ctx& ctx, const VehicleSpec& spec, double depth, double len, bool water) {
    World& w = blank(ctx, SURF_GRASS);
    const double x0 = -400;
    shape(w, x0 - 20, x0 + len + 20, -560, -200, [&](double x, double) {
        double inPit = std::min(x - x0, x0 + len - x);  // > 0 inside
        return -depth * clampd(inPit / (2 * depth), 0, 1);
    });
    for (double x = x0 - 2; x <= x0 + len + 2; x += 1)
        for (double z = -420; z <= -340; z += 1) {
            int id = w.cellIndex(x, z);
            w.setCellSurface(id, SURF_MUD, PAINT_MUD);
            if (water && w.cellHeight(id) < -0.1) w.setCellWater(id, -0.08f);
        }
    w.buildGrid();
    Car& c = setUp(ctx, spec, x0 - 15, -380, 0, true);
    double xMax = -1e9;
    ctx.run(60, [&] {
        drive(c, -380, 3.0);
        xMax = std::max(xMax, c.frame().com.x);
    });
    const bool out = xMax > x0 + len + 4;
    std::printf("PROBE bog %-8s depth %.1f m len %2.0f m %s: %s (got %.1f of %.0f m)%s\n", spec.name, depth, len, water ? "wet" : "dry",
                out ? "THROUGH" : "STUCK", xMax - x0, len, c.engineDead ? " ENGINE DEAD" : "");
    ctx.metric("progress", xMax - x0, "m", -100, 100);
}

int reg = [] {
    auto add = [](const std::string& n, std::function<void(Ctx&)> f) { scenarioRegistry().push_back({"probe/" + n, n, std::move(f)}); };
    for (double deg : {35.0, 45.0, 55.0, 65.0, 75.0}) {
        char b[64];
        std::snprintf(b, sizeof b, "wall/buggy-%02.0f", deg);
        add(b, [deg](Ctx& c) { wallProbe(c, buggy(), deg, 4); });
        std::snprintf(b, sizeof b, "wall/bronco-%02.0f", deg);
        add(b, [deg](Ctx& c) { wallProbe(c, bronco(), deg, 4); });
    }
    for (double deg : {20.0, 25.0, 30.0, 35.0, 40.0})
        for (Surface s : {SURF_DIRT, SURF_ROCK})
            for (bool full : {false, true}) {
                char b[64];
                std::snprintf(b, sizeof b, "camber/bronco-%s-%s-%02.0f", s == SURF_DIRT ? "dirt" : "rock", full ? "setup" : "stock", deg);
                add(b, [deg, s, full](Ctx& c) { camberProbe(c, bronco(), deg, s, full); });
            }
    for (double d : {0.6, 0.9, 1.2, 1.5})
        for (bool wet : {false, true}) {
            char b[64];
            std::snprintf(b, sizeof b, "bog/bronco-%.1f-%s", d, wet ? "wet" : "dry");
            add(b, [d, wet](Ctx& c) { bogProbe(c, bronco(), d, 14, wet); });
        }
    return 0;
}();

}  // namespace
