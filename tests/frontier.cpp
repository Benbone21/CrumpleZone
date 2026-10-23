// The Frontier map: it builds, and its features are where (and what) they should be.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/autopilot.h"
#include "harness.h"

SCENARIO(frontierMap, "maps/frontier", "The Frontier: builds in reasonable time; fords are shallow and the rivers deep elsewhere; every surface is somewhere; the trucks settle at the start") {
    const auto t0 = std::chrono::steady_clock::now();
    ctx.sim.world = World(MAP_FRONTIER);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const World& w = ctx.sim.world;
    ctx.metric("time to build the map", secs, "s", 0, 6);
    auto depthAt = [&](const V3& p) {
        double deepest = 0;
        for (double dx = -2; dx <= 2; dx += 1)
            for (double dz = -2; dz <= 2; dz += 1) deepest = std::max(deepest, w.waterLevel(p.x + dx, p.z + dz) - w.height(p.x + dx, p.z + dz));
        return deepest;
    };
    for (const char* f : {"ford1", "ford2", "ford3", "ford4"}) {
        const double d = depthAt(frontierFeature(f));
        const V3 fp = frontierFeature(f);
        std::printf("FORD %s at (%.0f, %.0f) depth %.2f flow %.2f\n", f, fp.x, fp.z, d, len(w.waterFlow(fp.x, fp.z)));
        ctx.metric(std::string(f) + ": water depth (a truck can ford ~0.6 m)", d, "m", 0.15, 0.6);
    }
    // Mid-river, away from the fords: too deep to drive.
    const double deep = depthAt(V3(242, 0, 112));
    ctx.metric("Snake River away from the fords", deep, "m", 0.9, 2.0);
    ctx.metric("Snake River current there", len(w.waterFlow(242, 112)), "m/s", 0.8, 2.0);
    ctx.metric("Echo Lake depth at the middle", depthAt(frontierFeature("lake")), "m", 1.5, 4);
    int count[SURF_COUNT] = {0};
    for (double x = -500; x <= 500; x += 2)
        for (double z = -500; z <= 500; z += 2) count[w.surfaceAt(x, z)]++;
    for (int s = 0; s < SURF_COUNT; s++) {
        std::printf("SURF %-12s %6.1f%%\n", surfaceInfo((Surface)s).name, 100.0 * count[s] / (501.0 * 501.0));
        ctx.check(std::string("some ") + surfaceInfo((Surface)s).name, count[s] > 20, std::to_string(count[s]));
    }
    ctx.check("ice on the tarn", w.surfaceAt(frontierFeature("tarn").x, frontierFeature("tarn").z) == SURF_ICE);
    ctx.check("mud in the bog", w.surfaceAt(frontierFeature("bog").x, frontierFeature("bog").z) == SURF_MUD);
    ctx.check("the circuit is paved", w.surfaceAt(60, -118) == SURF_PAVED);
    ctx.metric("summit height", w.height(frontierFeature("summit").x, frontierFeature("summit").z), "m", 100, 220);
    std::printf("summit %.1f mesa top %.1f start %.1f wall foot %.1f\n", w.height(frontierFeature("summit").x, frontierFeature("summit").z),
                w.height(frontierFeature("mesa").x, frontierFeature("mesa").z), w.height(frontierFeature("start").x, frontierFeature("start").z),
                w.height(frontierFeature("wall").x, frontierFeature("wall").z));
    std::printf("rocks %zu trees %zu bushes %zu boxes %zu stations %zu\n", w.rocks.size(), w.trees.size(), w.bushes.size(), w.boxes.size(), w.stations.size());
    // Everyone parked at the start settles without damage.
    ctx.sim.setup();
    ctx.run(4.0);
    double worst = 0;
    for (const Car& c : ctx.sim.cars) worst = std::max(worst, c.damage);
    ctx.metric("damage settling at the start", worst * 100, "%", 0, 0.1);
}

namespace {
const VehicleSpec& fBronco() { return vehicleCatalog()[2]; }

// Heading (degrees, the harness's convention: 0 = +x, 90 = -z) from a to b.
double headingFromTo(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI; }

// Runs the autopilot to `goal`; returns whether it arrived.
bool pilotRun(Ctx& ctx, Car& c, const V3& goal, double kmh, double maxS, int route, double* worst = nullptr) {
    const std::string msg = pilotStart(c, ctx.sim.world, goal, "THERE", kmh / 3.6, route);
    ctx.check("route planned", c.pilot.active, msg);
    if (std::getenv("CZ_DEBUG"))
        for (size_t k = 0; k < c.pilot.path.size(); k += 1) {
            const V3& p = c.pilot.path[k];
            const double dep = ctx.sim.world.waterLevel(p.x, p.z) - ctx.sim.world.height(p.x, p.z);
            if (dep > 0 || std::getenv("CZ_ROUTE")) std::printf("ROUTE %zu (%.0f, %.0f) y %.1f water depth %.2f\n", k, p.x, p.z, p.y, dep);
        }
    const int i = (int)(&c - ctx.sim.cars.data());
    ctx.runUntil(maxS, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(i, Ctx::kFrame);
        if (worst) *worst = std::max(*worst, c.damage);
        static thread_local int fr = 0;
        if (std::getenv("CZ_TRACE") && ++fr % (std::getenv("CZ_FAST") ? 10 : 60) == 0) {
            const Frame f = c.frame();
            std::printf("TRACE t %.0f (%.1f %.1f %.1f) v %.1f mode %d gear %d lock %d%d psi %.0f tc %d thr %.2f brk %.2f need %d@%.1f step %.2f stuck %d %.1f steer %.2f route %d lt %.1f | %s | %s\n", ctx.t, f.com.x, f.com.y,
                        f.com.z, dot(f.vel, f.fwd), c.driveMode, c.gear, c.lockFront, c.lockRear, c.tirePsi, c.tractionControl, c.throttle, c.brake, c.pilot.needLevel,
                        std::min(99.0, c.pilot.needDist), c.pilot.needStep, c.pilot.stuckCount, c.pilot.stuckTime, c.steer, c.pilot.onRoute, c.pilot.lockTime[0], c.pilot.status.c_str(),
                        c.pilot.thought.c_str());
            if (std::getenv("CZ_WHEELS"))
                for (const Wheel& wh : c.wheels)
                    std::printf("      %s%s load %5.0f spin %+5.2f T %5.0f brk %3.0f%% sink %.2f %s\n", wh.front ? "F" : "R", wh.left ? "L" : "R", wh.load,
                                wh.omega * wh.rollR() - wh.groundSpeed, wh.shaftTorque, wh.tcBrake * 100, wh.sink, wh.shaftBroken ? "BROKEN" : "");
        }
        static thread_local bool told = false;
        if (c.engineDead && !told && std::getenv("CZ_DEBUG")) {
            told = true;
            const Frame f = c.frame();
            std::printf("ENGINE DIED at (%.1f, %.1f, %.1f) water %.2f ground %.2f t %.1f\n", f.com.x, f.com.y, f.com.z, ctx.sim.world.waterLevel(f.com.x, f.com.z),
                        ctx.sim.world.height(f.com.x, f.com.z), ctx.t);
        }
    });
    return c.pilot.arrived;
}
}  // namespace

SCENARIO(frontierFord, "frontier/ford", "Bronco in 4H wades the Snake River ford at walking pace: across, engine still running") {
    ctx.sim.world = World(MAP_FRONTIER);
    const World& w = ctx.sim.world;
    const V3 f = frontierFeature("ford1");
    // Along the rally stage across the river.
    const std::vector<V3> rally = splinePoints({{150, 0, 232}, {262, 0, 214}, {360, 0, 196}}, false);
    const V3 a = f + norm(V3(-1, 0, 0.16)) * 22, b = f + norm(V3(1, 0, -0.16)) * 22;
    (void)rally;
    Car& c = ctx.add(fBronco(), a.x, a.z, headingFromTo(a, b));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(2);
    double deepest = 0;
    ctx.runUntil(40, [&] { return dot(c.frame().com - f, norm(b - a)) > 18; }, [&] {
        const Frame fr = c.frame();
        V3 d = b - fr.com;
        d.y = 0;
        c.steer = clampd(std::atan2(dot(d, fr.left), dot(d, fr.fwd)) * 1.2, -0.4, 0.4);
        const double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (2.5 - v) * 0.6, 0, 1), c.brake = 0, c.handbrake = 0;
        deepest = std::max(deepest, w.waterLevel(fr.com.x, fr.com.z) - w.height(fr.com.x, fr.com.z));
    });
    ctx.check("across", dot(c.frame().com - f, norm(b - a)) > 18);
    ctx.check("engine still running", !c.engineDead);
    ctx.metric("deepest water it drove through", deepest, "m", 0.25, 0.65);
}

SCENARIO(frontierCurrent, "frontier/current", "A Civic stalled in the deep part of the Snake River floats and is carried downstream by the current") {
    ctx.sim.world = World(MAP_FRONTIER);
    const World& w = ctx.sim.world;
    const V3 p(242, 0, 112);
    Car& c = ctx.add(vehicleCatalog()[0], p.x, p.z, 0);
    ctx.focus(c);
    const V3 flow = w.waterFlow(p.x, p.z);
    const V3 c0 = c.frame().com;
    int fr = 0;
    ctx.run(12, [&] {
        c.throttle = 0, c.brake = 0, c.handbrake = 0;
        if (std::getenv("CZ_DEBUG") && fr++ % 60 == 0) {
            const Frame f = c.frame();
            const V3 fl = w.waterFlow(c.nodes[0].p.x, c.nodes[0].p.z);
            std::printf("t %.0f com (%.2f %.2f %.2f) water %.2f ground %.2f flow (%.2f %.2f) loads", ctx.t, f.com.x, f.com.y, f.com.z, w.waterLevel(f.com.x, f.com.z),
                        w.height(f.com.x, f.com.z), fl.x, fl.z);
            for (const Wheel& wh : c.wheels) std::printf(" %.0f", wh.load);
            std::printf(" hb %.1f gear %d\n", c.handbrake, c.gear);
        }
    });
    const V3 moved = c.frame().com - c0;
    ctx.metric("drift downstream in 12 s", dot(moved, norm(flow)), "m", 1.5, 40);
    ctx.check("flooded engine", c.engineDead || c.hydrolocked);
}

SCENARIO(frontierMesa, "frontier/mesa-climb", "Autopilot takes the Bronco up the mesa's ledge staircase from the foot to the top") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 foot = frontierFeature("mesaclimb") + V3(10, 0, 0), top = frontierFeature("mesa") + V3(25, 0, 0);
    Car& c = ctx.add(fBronco(), foot.x, foot.z, 180);
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotRun(ctx, c, top, 15, 240, PILOT_ANYWHERE, &worst);
    ctx.check("on top of the mesa", ok && c.frame().com.y > ctx.sim.world.height(foot.x, foot.z) + 7, c.pilot.status);
    ctx.metric("damage on the way up", worst * 100, "%", 0, 2);
}

SCENARIO(frontierWall, "frontier/the-wall", "Autopilot takes the Bronco up THE WALL: ten rock ledges up the foothills to the lookout") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 top = frontierFeature("walltop"), foot = frontierFeature("wallfrom");
    if (std::getenv("CZ_DEBUG"))
        std::printf("WALL from (%.0f %.0f) foot (%.0f %.0f) top (%.0f %.0f)\n", foot.x, foot.z, frontierFeature("wall").x, frontierFeature("wall").z, top.x, top.z);
    Car& c = ctx.add(fBronco(), foot.x, foot.z, headingFromTo(foot, frontierFeature("wall")));
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const double y0 = c.frame().com.y;
    const bool ok = pilotRun(ctx, c, top, 15, 240, PILOT_ANYWHERE, &worst);
    ctx.check("at the top", ok, c.pilot.status);
    ctx.metric("climbed", c.frame().com.y - y0, "m", 7, 40);
    ctx.metric("damage on the way up", worst * 100, "%", 0, 2);
}

SCENARIO(frontierRally, "frontier/autopilot-river", "Autopilot from the paddock to the far side of the Snake River ford on roads and trails") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 s = frontierFeature("start");
    Car& c = ctx.add(fBronco(), s.x, s.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotRun(ctx, c, frontierFeature("ford1") + V3(30, 0, -6), 40, 300, PILOT_TRAILS, &worst);
    ctx.check("arrived", ok, c.pilot.status);
    ctx.check("engine still running", !c.engineDead);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.5);
}

namespace {
// A set-up Bronco (4L, both lockers, 18 psi) crawls along the polyline `pts` at walking
// pace; returns how high it got above where it started.
double crawlUp(Ctx& ctx, const std::vector<V3>& pts, double maxS, double* worst) {
    VehicleSpec spec = fBronco();
    static std::vector<VehicleSpec> keep;
    keep.reserve(8);
    keep.push_back(spec);
    Car& c = ctx.add(keep.back(), pts[0].x, pts[0].z, headingFromTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(false, true);
    c.setLocker(true, true);
    c.tirePsi = 18;
    c.tractionControl = false;
    const double y0 = c.frame().com.y;
    double top = 0;
    size_t seg = 0;
    ctx.runUntil(maxS, [&] { return seg + 1 >= pts.size(); }, [&] {
        const Frame f = c.frame();
        const V3 a = pts[seg], b = pts[seg + 1];
        const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
        const double along = dot(f.com - a, dir);
        if (along > std::hypot(b.x - a.x, b.z - a.z) - 1.5) seg++;
        // Pure pursuit to the line a-b, 6 m ahead.
        V3 aim = a + dir * (along + 6) - f.com;
        aim.y = 0;
        c.steer = clampd(std::atan2(dot(aim, f.left), dot(aim, f.fwd)) * 1.2, -0.4, 0.4);
        const double v = dot(f.vel, f.fwd);
        c.throttle = clampd(0.35 + (1.0 - v) * 0.9, 0, 1), c.brake = v > 2.2 ? 0.3 : 0, c.handbrake = 0;
        top = std::max(top, f.com.y - y0);
        if (worst) *worst = std::max(*worst, c.damage);
        static thread_local int fr = 0;
        if (std::getenv("CZ_DEBUG") && ++fr % 60 == 0)
            std::printf("t %.0f seg %zu along %.1f pos (%.1f %.1f %.1f) v %.1f steer %.2f fwd (%.2f %.2f %.2f) aim (%.1f %.1f)\n", ctx.t, seg, along, f.com.x, f.com.y, f.com.z, v, c.steer,
                        f.fwd.x, f.fwd.y, f.fwd.z, aim.x, aim.z);
    });
    return top;
}
}  // namespace

SCENARIO(frontierMesaCrawl, "frontier/mesa-stairs", "A set-up Bronco (4L, rear locker, 18 psi) crawls the mesa's fourteen 0.32 m ledges to the top") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 a = frontierFeature("mesaclimb") + V3(8, 0, 0), b = frontierFeature("mesa") + V3(30, 0, 0);
    double worst = 0;
    const double up = crawlUp(ctx, {a, b}, 150, &worst);
    ctx.metric("climbed", up, "m", 8, 16);
    ctx.metric("damage on the way up", worst * 100, "%", 0, 2);
}

SCENARIO(frontierWallCrawl, "frontier/wall-stairs", "A set-up Bronco crawls THE WALL's twelve 0.32 m ledges up the gully to the lookout") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 foot = frontierFeature("wall"), top = frontierFeature("walltop"), from = frontierFeature("wallfrom");
    double worst = 0;
    // In along the approach track: to the run-up lined up with the stairs, then up them.
    const V3 dir = norm(V3(top.x - foot.x, 0, top.z - foot.z));
    const double up = crawlUp(ctx, {from, foot - dir * 12, foot, top}, 150, &worst);
    ctx.metric("climbed", up, "m", 7, 16);
    ctx.metric("damage on the way up", worst * 100, "%", 0, 2);
}

// ---- Blackwater Bayou and Cold Creek ---------------------------------------------------------
namespace {
// The points `from`..`to` metres along a trail ("bayou" or "snowroad"), every 2 m.
std::vector<V3> trailLine(const char* trail, double from, double to) {
    std::vector<V3> pts;
    for (double s = from; s <= to + 1e-6; s += 2) pts.push_back(frontierFeature((std::string(trail) + ":" + std::to_string(s)).c_str()));
    return pts;
}
// How long a trail is (m).
double trailLength(const char* trail) {
    double s = 0;
    V3 prev = frontierFeature((std::string(trail) + ":0").c_str());
    for (;; s += 1) {
        const V3 p = frontierFeature((std::string(trail) + ":" + std::to_string(s + 1)).c_str());
        if (std::hypot(p.x - prev.x, p.z - prev.z) < 0.01) return s;
        prev = p;
    }
}
// Drives `c` along the polyline at about `kmh` (pure pursuit); returns how far along it got
// (m, measured along the line) when it reached the end, gave up, or ran out of time.
double driveLine(Ctx& ctx, Car& c, const std::vector<V3>& pts, double kmh, double maxS, double* worst = nullptr) {
    std::vector<double> at(pts.size(), 0);
    for (size_t k = 1; k < pts.size(); k++) at[k] = at[k - 1] + std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    size_t seg = 0;
    double best = 0;
    ctx.runUntil(maxS, [&] { return seg + 1 >= pts.size(); }, [&] {
        const Frame f = c.frame();
        while (seg + 1 < pts.size()) {
            const V3 a = pts[seg], b = pts[seg + 1];
            const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
            if (dot(f.com - a, dir) < std::hypot(b.x - a.x, b.z - a.z)) break;
            seg++;
        }
        if (seg + 1 >= pts.size()) return;
        const V3 a = pts[seg], b = pts[seg + 1];
        const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
        const double along = dot(f.com - a, dir);
        best = std::max(best, at[seg] + along);
        // Aim at the point a few metres further on (more at speed).
        const double v = dot(f.vel, f.fwd), look = 5 + 0.6 * std::fabs(v);
        double s = at[seg] + along + look;
        size_t k = seg;
        while (k + 2 < pts.size() && at[k + 1] < s) k++;
        const V3 q = pts[k] + (pts[k + 1] - pts[k]) * clampd((s - at[k]) / std::max(1e-6, at[k + 1] - at[k]), 0, 1);
        V3 aim = q - f.com;
        aim.y = 0;
        c.steer = clampd(std::atan2(dot(aim, f.left), dot(aim, f.fwd)) * 1.3, -0.5, 0.5);
        const double want = kmh / 3.6;
        c.throttle = clampd(0.3 + (want - v) * 0.5, 0, 1), c.brake = v > want + 1.5 ? 0.4 : 0, c.handbrake = 0;
        if (worst) *worst = std::max(*worst, c.damage);
    });
    return seg + 1 >= pts.size() ? at.back() : best;
}
// A Bronco set up for the slop: 4L, both lockers, aired down to 18 psi, no traction control.
void setUpForMud(Car& c, const VehicleSpec& spec) {
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(false, true);
    c.setLocker(true, true);
    c.tirePsi = 18;
    c.tractionControl = false;
}
}  // namespace

SCENARIO(frontierBayouMap, "maps/frontier-bayou", "Blackwater Bayou: a swamp of mud and standing water; its trail has puddles of every depth, two wallows and MUD HILL") {
    ctx.sim.world = World(MAP_FRONTIER);
    const World& w = ctx.sim.world;
    auto water = [&](const V3& p) { return w.waterLevel(p.x, p.z) - w.height(p.x, p.z); };
    // Along the trail: count the puddles (a stretch under water) and how deep they get.
    const double L = trailLength("bayou");
    int puddles = 0;
    double shallowest = 9, deepest = 0, cur = -1;
    for (double s = 0; s <= L; s += 0.5) {
        const double d = water(frontierFeature(("bayou:" + std::to_string(s)).c_str()));
        if (d > 0.02) cur = std::max(cur, d);
        else if (cur > 0) {
            puddles++, shallowest = std::min(shallowest, cur), deepest = std::max(deepest, cur), cur = -1;
        }
    }
    std::printf("bayou trail %.0f m, %d puddles %.2f..%.2f m deep\n", L, puddles, shallowest, deepest);
    ctx.metric("trail length", L, "m", 600, 900);
    ctx.metric("puddles along the trail", puddles, "", 10, 60);
    ctx.metric("the shallowest puddle", shallowest, "m", 0.02, 0.15);
    ctx.metric("the deepest water on the trail (a wallow)", deepest, "m", 0.4, 0.6);
    for (const char* f : {"wallow1", "wallow2"}) {
        const V3 p = frontierFeature(f);
        ctx.metric(std::string(f) + ": water over the bottom", water(p), "m", 0.35, 0.6);
        ctx.check(std::string(f) + ": mud bottom", w.surfaceAt(p.x, p.z) == SURF_MUD);
    }
    // The swamp round the middle: mud and water, and grass on the hummocks.
    const V3 mid = frontierFeature("bayou");
    int mud = 0, wet = 0, n = 0;
    for (double x = mid.x - 130; x <= mid.x + 130; x += 3)
        for (double z = mid.z - 60; z <= mid.z + 60; z += 3) {
            if (std::hypot((x - mid.x) / 130, (z - mid.z) / 60) > 1) continue;
            n++;
            mud += w.surfaceAt(x, z) == SURF_MUD;
            wet += water(V3(x, 0, z)) > 0.02;
        }
    ctx.metric("the swamp: share of mud", 100.0 * mud / n, "%", 40, 95);
    ctx.metric("the swamp: share under water", 100.0 * wet / n, "%", 15, 60);
    const V3 hill = frontierFeature("mudhill");
    ctx.metric("MUD HILL above the swamp", w.height(hill.x, hill.z) - w.height(mid.x, mid.z), "m", 4.5, 9);
    ctx.check("MUD HILL is mud", w.surfaceAt(hill.x, hill.z) == SURF_MUD);
}

SCENARIO(frontierSnowMap, "maps/frontier-snow", "Cold Creek: the valley is under snow, with a frozen creek and pond; the snow road is packed snow with ice on it; the lodge at the head") {
    ctx.sim.world = World(MAP_FRONTIER);
    const World& w = ctx.sim.world;
    int count[SURF_COUNT] = {0}, n = 0;
    const V3 mid = frontierFeature("coldcreek");
    for (double x = mid.x - 70; x <= mid.x + 70; x += 2)
        for (double z = mid.z - 110; z <= mid.z + 110; z += 2) {
            if (std::hypot((x - mid.x) / 70, (z - mid.z) / 110) > 1) continue;
            count[w.surfaceAt(x, z)]++, n++;
        }
    ctx.metric("Cold Creek: share under snow", 100.0 * count[SURF_SNOW] / n, "%", 75, 100);
    ctx.metric("Cold Creek: share of ice", 100.0 * count[SURF_ICE] / n, "%", 1, 10);
    ctx.check("the pond is frozen", w.surfaceAt(frontierFeature("icepond").x, frontierFeature("icepond").z) == SURF_ICE);
    ctx.check("the tarn is still frozen", w.surfaceAt(frontierFeature("tarn").x, frontierFeature("tarn").z) == SURF_ICE);
    const double L = trailLength("snowroad");
    int snow = 0, ice = 0, cnt = 0;
    double steepest = 0;
    for (double s = 100; s < L; s += 1) {
        const V3 p = frontierFeature(("snowroad:" + std::to_string(s)).c_str()), q = frontierFeature(("snowroad:" + std::to_string(s + 1)).c_str());
        snow += w.surfaceAt(p.x, p.z) == SURF_SNOW, ice += w.surfaceAt(p.x, p.z) == SURF_ICE, cnt++;
        steepest = std::max(steepest, std::fabs(w.height(q.x, q.z) - w.height(p.x, p.z)));
    }
    std::printf("snow road %.0f m: snow %d ice %d of %d, steepest %.1f%%\n", L, snow, ice, cnt, steepest * 100);
    ctx.metric("snow road length", L, "m", 250, 450);
    ctx.metric("snow road in the valley: share packed snow", 100.0 * snow / cnt, "%", 80, 100);
    ctx.check("ice on the snow road", ice >= 6, std::to_string(ice));
    ctx.metric("snow road: steepest grade", steepest * 100, "%", 3, 11);
    bool lodge = false;
    for (const Station& st : w.stations) lodge |= std::hypot(st.p.x - frontierFeature("lodge").x, st.p.z - frontierFeature("lodge").z) < 3;
    ctx.check("a repair station at the lodge", lodge);
}

SCENARIO(frontierWallowCivic, "frontier/wallow-civic", "A Civic tries THE WALLOW on the bayou trail at a crawl: it drops in and can't climb out the far side") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 wl = frontierFeature("wallow1");
    double sW = 0;
    for (double s = 0, best = 1e9; s < 400; s += 0.5) {
        const V3 p = frontierFeature(("bayou:" + std::to_string(s)).c_str());
        if (std::hypot(p.x - wl.x, p.z - wl.z) < best) best = std::hypot(p.x - wl.x, p.z - wl.z), sW = s;
    }
    const std::vector<V3> pts = trailLine("bayou", sW - 26, sW + 26);
    Car& c = ctx.add(vehicleCatalog()[0], pts[0].x, pts[0].z, headingFromTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    const double got = driveLine(ctx, c, pts, 8, 45);
    ctx.metric("how far it got (the far lip is ~33 m)", got, "m", 18, 31);
    ctx.check("engine still running (it's mud, not deep water)", !c.engineDead);
}

SCENARIO(frontierWallowBronco, "frontier/wallow-bronco", "A set-up Bronco (4L, lockers, 18 psi) wades through both wallows on the bayou trail") {
    ctx.sim.world = World(MAP_FRONTIER);
    const VehicleSpec& spec = fBronco();
    for (const char* f : {"wallow1", "wallow2"}) {
        const V3 wl = frontierFeature(f);
        double sW = 0;
        for (double s = 0, best = 1e9; s < 800; s += 0.5) {
            const V3 p = frontierFeature(("bayou:" + std::to_string(s)).c_str());
            if (std::hypot(p.x - wl.x, p.z - wl.z) < best) best = std::hypot(p.x - wl.x, p.z - wl.z), sW = s;
        }
        const std::vector<V3> pts = trailLine("bayou", sW - 26, sW + 26);
        ctx.sim.cars.clear();
        Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingFromTo(pts[0], pts[1]));
        ctx.focus(c);
        ctx.run(1.0);
        setUpForMud(c, spec);
        double worst = 0;
        const double got = driveLine(ctx, c, pts, 6, 60, &worst);
        ctx.metric(std::string(f) + ": through and out the far side", got, "m", 50, 54);
        ctx.check(std::string(f) + ": engine still running", !c.engineDead);
        ctx.metric(std::string(f) + ": damage", worst * 100, "%", 0, 1);
    }
}

SCENARIO(frontierMudHill, "frontier/mud-hill", "A set-up Bronco climbs MUD HILL's mud face and comes down the far side") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 a = frontierFeature("mudhillfrom"), b = frontierFeature("mudhillto");
    const V3 d = norm(V3(b.x - a.x, 0, b.z - a.z));
    std::vector<V3> pts;
    for (double s = 3; s <= std::hypot(b.x - a.x, b.z - a.z) - 3; s += 2) pts.push_back(a + d * s);
    const VehicleSpec& spec = fBronco();
    Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingFromTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    setUpForMud(c, spec);
    const double y0 = c.frame().com.y;
    double worst = 0;
    const double got = driveLine(ctx, c, pts, 8, 90, &worst);
    ctx.metric("over the hill (m along the line)", got, "m", pts.size() * 2.0 - 4, 1e3);
    ctx.metric("ends back down at the swamp (height change)", c.frame().com.y - y0, "m", -1, 1);
    ctx.metric("damage", worst * 100, "%", 0, 1);
}

SCENARIO(frontierSnowRoad, "frontier/snow-road", "The 4Runner drives the Cold Creek snow road from the rally stage to the lodge at 30 km/h") {
    ctx.sim.world = World(MAP_FRONTIER);
    const double L = trailLength("snowroad");
    const std::vector<V3> pts = trailLine("snowroad", 4, L - 2);
    Car& c = ctx.add(vehicleCatalog()[3], pts[0].x, pts[0].z, headingFromTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const double t0 = ctx.t;
    const double got = driveLine(ctx, c, pts, 30, 120, &worst);
    ctx.metric("got to the lodge (m along the road)", got, "m", L - 10, 1e4);
    ctx.metric("time up the road", ctx.t - t0, "s", 20, 80);
    ctx.metric("damage", worst * 100, "%", 0, 0.5);
}

SCENARIO(frontierPilotLodge, "frontier/autopilot-lodge", "Autopilot takes the 4Runner from the stage up Cold Creek to the lodge (roads and trails)") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 s = frontierFeature("snowroad") + V3(-14, 0, 6);
    Car& c = ctx.add(vehicleCatalog()[3], s.x, s.z, headingFromTo(s, frontierFeature("snowroad:20")));
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotRun(ctx, c, frontierFeature("lodge"), 30, 200, PILOT_TRAILS, &worst);
    ctx.check("arrived", ok, c.pilot.status);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1);
}

SCENARIO(frontierPilotBayou, "frontier/autopilot-bayou", "Autopilot takes the Bronco from the paddock down the rally stage and into Blackwater Bayou") {
    ctx.sim.world = World(MAP_FRONTIER);
    const V3 s = frontierFeature("start");
    Car& c = ctx.add(fBronco(), s.x, s.z, 0);
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotRun(ctx, c, frontierFeature("bayou:40"), 40, 300, PILOT_TRAILS, &worst);
    ctx.check("arrived", ok, c.pilot.status);
    ctx.metric("damage on the way", worst * 100, "%", 0, 1.5);
}

SCENARIO(frontierFeatureTableCheck, "frontier/feature-table", "The Frontier's searched-for features (THE WALL, the summit, the garden) are where its data table says, so levels need no map built") {
    World w(MAP_FRONTIER);
    std::string why;
    ctx.check("built where the table has them", frontierFeatureTable(&why), why);
}
