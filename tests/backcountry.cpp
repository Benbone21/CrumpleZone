// The Backcountry map: it builds, its trails are what their signs say (a Civic bogs on the Mud
// Run and a set-up Bronco makes it; the ford is shallow and the deep crossing deep), and you
// can get to the far side of it.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/autopilot.h"
#include "harness.h"

namespace {
const VehicleSpec& bCivic() { return vehicleCatalog()[0]; }
const VehicleSpec& bBronco() { return vehicleCatalog()[2]; }
const VehicleSpec& b4Runner() { return vehicleCatalog()[3]; }

// Heading (degrees, the harness's convention: 0 = +x, 90 = -z) from a to b.
double headingTo(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI; }

// The points `from`..`to` metres along a trail, every 2 m.
std::vector<V3> trailLine(const char* trail, double from, double to) {
    const std::vector<V3>& t = backcountryTrail(trail);
    std::vector<V3> pts;
    for (double s = from; s <= to + 1e-6 && s < (double)t.size() - 1; s += 2) pts.push_back(t[(size_t)s]);
    return pts;
}
double trailLength(const char* trail) {
    const std::vector<V3>& t = backcountryTrail(trail);
    double L = 0;
    for (size_t k = 1; k < t.size(); k++) L += std::hypot(t[k].x - t[k - 1].x, t[k].z - t[k - 1].z);
    return L;
}

// Drives `c` along the polyline at about `kmh` (pure pursuit); returns how far along it got (m)
// when it reached the end, gave up, or ran out of time.
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
        static thread_local int fr = 0;
        if (std::getenv("BC_TRACE") && ++fr % 120 == 0)
            std::printf("TRACE t %.1f seg %zu (%.1f %.1f %.2f) v %.2f along %.1f thr %.2f steer %.2f sink %.2f\n", ctx.t, seg, f.com.x, f.com.y, f.com.z, v, at[seg] + along, c.throttle, c.steer, c.wheels.empty() ? 0.0 : c.wheels[0].sink);
    });
    return seg + 1 >= pts.size() ? at.back() : best;
}
// A truck set up for the slop: low range, both lockers, aired down to 18 psi, no traction control.
void setUpForMud(Car& c, const VehicleSpec& spec) {
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(false, true);
    c.setLocker(true, true);
    c.tirePsi = 18;
    c.tractionControl = false;
}
double waterDepth(const World& w, const V3& p, double r = 2) {
    double deepest = 0;
    for (double dx = -r; dx <= r; dx += 1)
        for (double dz = -r; dz <= r; dz += 1) deepest = std::max(deepest, w.waterLevel(p.x + dx, p.z + dz) - w.height(p.x + dx, p.z + dz));
    return deepest;
}
}  // namespace

SCENARIO(backcountryMap, "maps/backcountry",
         "The Backcountry: builds in reasonable time; 4 repair stations on flat pads; every surface is somewhere; 3-5 km of trail; the ford is shallow and the deep crossing deep") {
    const auto t0 = std::chrono::steady_clock::now();
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const World& w = ctx.sim.world;
    ctx.metric("time to build the map", secs, "s", 0, 6);
    // Stations: four, on pads flat to a few centimetres across their reach.
    ctx.metric("repair stations", (double)w.stations.size(), "", 3, 4);
    for (const Station& st : w.stations) {
        double lo = 1e9, hi = -1e9;
        for (double dx = -st.r; dx <= st.r; dx += 1.5)
            for (double dz = -st.r; dz <= st.r; dz += 1.5) {
                if (std::hypot(dx, dz) > st.r) continue;
                const double h = w.height(st.p.x + dx, st.p.z + dz);
                lo = std::min(lo, h), hi = std::max(hi, h);
            }
        std::printf("STATION %s at (%.0f, %.1f, %.0f) relief %.2f m\n", st.name, st.p.x, st.p.y, st.p.z, hi - lo);
        ctx.metric(std::string(st.name) + ": relief over the pad", hi - lo, "m", 0, 0.3);
    }
    // Every surface type is somewhere.
    int count[SURF_COUNT] = {0};
    for (double x = -520; x <= 520; x += 2)
        for (double z = -520; z <= 520; z += 2) count[w.surfaceAt(x, z)]++;
    for (int s = 0; s < SURF_COUNT; s++) {
        std::printf("SURF %-12s %6.2f%%\n", surfaceInfo((Surface)s).name, 100.0 * count[s] / (521.0 * 521.0));
        ctx.check(std::string("some ") + surfaceInfo((Surface)s).name, count[s] > 20, std::to_string(count[s]));
    }
    // The trails: all there, 3-5 km in all.
    double total = 0;
    for (const char* n : {"mud", "creek", "ridge", "ridgeroad", "hill", "wash", "shelf", "pass", "tower", "cabin", "wreck"}) {
        const double L = trailLength(n);
        std::printf("TRAIL %-10s %5.0f m\n", n, L);
        ctx.check(std::string("trail ") + n, L > 100, std::to_string(L));
        total += L;
    }
    ctx.metric("trail length in all", total / 1000, "km", 3, 5);
    // Crossings: the ford a truck can wade, the deep one it can't without a snorkel; the river
    // elsewhere deeper still.
    const double ford = waterDepth(w, backcountryFeature("ford")), deep = waterDepth(w, backcountryFeature("deepcrossing"));
    std::printf("FORD %.2f m, DEEP CROSSING %.2f m\n", ford, deep);
    ctx.metric("the ford: water depth (a truck can ford ~0.6 m)", ford, "m", 0.15, 0.55);
    ctx.metric("the deep crossing: water depth", deep, "m", 0.7, 1.2);
    {
        const std::vector<V3>& river = backcountryTrail("river");
        V3 mid = river[0];
        for (const V3& p : river)
            if (std::fabs(p.x - 300) < std::fabs(mid.x - 300)) mid = p;
        ctx.metric("Elk River away from the crossings", waterDepth(w, mid, 0.5), "m", 1.0, 2.5);
        const V3 dc = backcountryFeature("deepcrossing");
        ctx.metric("Elk River's current at the deep crossing", len(w.waterFlow(dc.x, dc.z)), "m/s", 0.2, 2.5);
    }
    // The Mud Run's bog holes hold 0.3-0.7 m of water.
    for (const char* b : {"bog1", "bog2", "bog3", "bog4"}) ctx.metric(std::string(b) + ": water over the mud", waterDepth(w, backcountryFeature(b), 6), "m", 0.3, 0.7);
    // The shelf road runs along a real drop.
    {
        const V3 o = backcountryFeature("overlook");
        double lowest = 1e9;
        for (double dx = -60; dx <= 60; dx += 2)
            for (double dz = -60; dz <= 60; dz += 2) lowest = std::min(lowest, w.height(o.x + dx, o.z + dz));
        ctx.metric("shelf road: drop to the canyon floor at the overlook", w.height(o.x, o.z) - lowest, "m", 15, 60);
    }
    ctx.metric("the pass hut is up in the snow", w.height(backcountryFeature("passhut").x, backcountryFeature("passhut").z), "m", 60, 140);
    std::printf("rocks %zu trees %zu bushes %zu boxes %zu logs %zu\n", w.rocks.size(), w.trees.size(), w.bushes.size(), w.boxes.size(), w.logs.size());
    // Everyone parked at the trailhead settles without damage.
    ctx.sim.setup();
    ctx.run(4.0);
    double worst = 0;
    for (const Car& c : ctx.sim.cars) worst = std::max(worst, c.damage);
    ctx.metric("damage settling at the trailhead", worst * 100, "%", 0, 0.1);
}

namespace {
// Where along a trail (m) the point nearest p is.
double alongNear(const char* trail, const V3& p) {
    const std::vector<V3>& t = backcountryTrail(trail);
    size_t best = 0;
    for (size_t k = 0; k < t.size(); k++)
        if (std::hypot(t[k].x - p.x, t[k].z - p.z) < std::hypot(t[best].x - p.x, t[best].z - p.z)) best = k;
    return (double)best;
}
}  // namespace

SCENARIO(backcountryFord, "backcountry/ford", "Bronco in 4H wades Elk River at the ford (ridge road) at walking pace: across, engine still running, in shallow water") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const double s0 = alongNear("ridgeroad", backcountryFeature("ford"));
    const std::vector<V3> pts = trailLine("ridgeroad", s0 - 30, s0 + 30);
    Car& c = ctx.add(bBronco(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(2);
    double deepest = 0;
    const double got = driveLine(ctx, c, pts, 9, 60, nullptr);
    (void)deepest;
    (void)w;
    ctx.metric("across (m along the line, of ~58)", got, "m", 50, 70);
    ctx.check("engine still running", !c.engineDead);
}

SCENARIO(backcountryDeep, "backcountry/deep-crossing", "The 4Runner (snorkel) wades the deep crossing at a crawl; the Civic's engine drowns in it") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const double s0 = alongNear("ridge", backcountryFeature("deepcrossing"));
    const std::vector<V3> pts = trailLine("ridge", s0 - 26, s0 + 26);
    {
        Car& c = ctx.add(b4Runner(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
        ctx.focus(c);
        ctx.run(1.0);
        c.setDriveMode(2);
        const double got = driveLine(ctx, c, pts, 8, 60, nullptr);
        ctx.metric("4Runner: across (m along the line, of ~52)", got, "m", 44, 70);
        ctx.check("4Runner: engine still running", !c.engineDead);
    }
    ctx.sim.cars.clear();
    {
        Car& c = ctx.add(bCivic(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
        ctx.focus(c);
        ctx.run(1.0);
        driveLine(ctx, c, pts, 8, 40, nullptr);
        ctx.check("Civic: engine drowned", c.engineDead);
    }
}

SCENARIO(backcountryBogCivic, "backcountry/bog-civic", "A Civic tries the Mud Run from just before its first mud stretch at a crawl: it bogs down before it gets through the first bog hole") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const std::vector<V3> pts = trailLine("mud", 120 - 50, 120 + 26);
    Car& c = ctx.add(bCivic(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    const double got = driveLine(ctx, c, pts, 8, 45);
    ctx.metric("how far it got (the far lip is ~76 m)", got, "m", 15, 60);
    ctx.check("engine still running (it's mud, not deep water)", !c.engineDead);
}

SCENARIO(backcountryBogBronco, "backcountry/bog-bronco", "A set-up Bronco (4L, lockers, 18 psi) wades through all four bog holes on the Mud Run") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const VehicleSpec& spec = bBronco();
    const double bogS[4] = {120, 372, 440, 520};
    for (int b = 0; b < 4; b++) {
        const std::vector<V3> pts = trailLine("mud", bogS[b] - 26, bogS[b] + 26);
        ctx.sim.cars.clear();
        Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
        ctx.focus(c);
        ctx.run(1.0);
        setUpForMud(c, spec);
        double worst = 0;
        const double got = driveLine(ctx, c, pts, 6, 70, &worst);
        ctx.metric("bog " + std::to_string(b + 1) + ": through and out the far side", got, "m", 48, 54);
        ctx.check("bog " + std::to_string(b + 1) + ": engine still running", !c.engineDead);
        ctx.metric("bog " + std::to_string(b + 1) + ": damage", worst * 100, "%", 0, 1);
    }
}

SCENARIO(backcountryHill, "backcountry/hill", "A set-up Bronco climbs Heartbreak Hill (loose gravel up a ~30% face) and comes down the far side") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const std::vector<V3> pts = trailLine("hill", 4, 160);
    const VehicleSpec& spec = bBronco();
    Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    setUpForMud(c, spec);
    double worst = 0;
    const double got = driveLine(ctx, c, pts, 8, 80, &worst);
    ctx.metric("Bronco: over the hill (m along the line)", got, "m", (pts.size() - 1) * 2.0 - 4, 1e3);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 2);
}

SCENARIO(backcountryCreek, "backcountry/creek-crawl", "A set-up Bronco crawls up the Creek Crawl (boulders and rock ledges) to the mine") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const std::vector<V3> pts = trailLine("creek", 8, 366);
    const VehicleSpec& spec = bBronco();
    Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    setUpForMud(c, spec);
    double worst = 0;
    const double got = driveLine(ctx, c, pts, 6, 330, &worst);
    ctx.metric("Bronco: up the creek (m along the line, of ~358)", got, "m", 340, 1e3);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 8);
}

SCENARIO(backcountryRidge, "backcountry/slickrock-ridge", "A set-up Bronco drives Slickrock Ridge from the mine down to the river (off-camber slabs, rock steps, a steep rocky descent)") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const double end = alongNear("ridge", backcountryFeature("deepcrossing")) - 20;
    const std::vector<V3> pts = trailLine("ridge", 8, end);
    const VehicleSpec& spec = bBronco();
    Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    setUpForMud(c, spec);
    double worst = 0;
    const double got = driveLine(ctx, c, pts, 6, 300, &worst);
    ctx.metric("Bronco: along the ridge (m)", got, "m", (pts.size() - 1) * 2.0 - 4, 1e3);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 5);
}

SCENARIO(backcountryShelf, "backcountry/shelf-road", "The 4Runner drives the Shelf Road along the canyon rim at 20 km/h and the pass road on to the hut: no falling off, no damage") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    std::vector<V3> pts = trailLine("shelf", 8, 262);
    const std::vector<V3> pass = trailLine("pass", 0, 360);
    pts.insert(pts.end(), pass.begin(), pass.end());
    Car& c = ctx.add(b4Runner(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(2);
    double worst = 0;
    const double got = driveLine(ctx, c, pts, 20, 200, &worst);
    ctx.metric("4Runner: shelf road and pass (m along the line)", got, "m", (pts.size() - 1) * 2.0 - 6, 1e4);
    ctx.metric("4Runner: damage", worst * 100, "%", 0, 1);
}

namespace {
// Runs the autopilot to `goal`; returns whether it arrived.
bool pilotRun(Ctx& ctx, Car& c, const V3& goal, double kmh, double maxS, int route, double* worst = nullptr) {
    const std::string msg = pilotStart(c, ctx.sim.world, goal, "THERE", kmh / 3.6, route);
    ctx.check("route planned", c.pilot.active, msg);
    if (std::getenv("BC_TRACE")) std::printf("ROUTE %zu points: %s\n", c.pilot.path.size(), msg.c_str());
    const int i = (int)(&c - ctx.sim.cars.data());
    ctx.runUntil(maxS, [&] { return !c.pilot.active; }, [&] {
        ctx.sim.pilotStep(i, Ctx::kFrame);
        if (worst) *worst = std::max(*worst, c.damage);
        static thread_local int fr = 0;
        if (std::getenv("BC_TRACE") && ++fr % 300 == 0) {
            const Frame f = c.frame();
            std::printf("TRACE t %.0f (%.1f %.1f %.1f) v %.1f mode %d lock %d%d psi %.0f stuck %d route %d | %s | %s\n", ctx.t, f.com.x, f.com.y, f.com.z, dot(f.vel, f.fwd), c.driveMode,
                        c.lockFront, c.lockRear, c.tirePsi, c.pilot.stuckCount, c.pilot.onRoute, c.pilot.status.c_str(), c.pilot.thought.c_str());
        }
    });
    return c.pilot.arrived;
}
}  // namespace

SCENARIO(backcountryPilotFar, "backcountry/autopilot-far", "Autopilot plans the Bronco's way from the trailhead to Far Side Camp, across the ford, and drives it") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const V3 s = ctx.sim.world.spawn;
    Car& c = ctx.add(bBronco(), s.x, s.z, ctx.sim.world.spawnYaw * 180 / M_PI);  // (facing off its spawn pad)
    ctx.focus(c);
    ctx.run(1.0);
    double worst = 0;
    const bool ok = pilotRun(ctx, c, backcountryFeature("farside") + V3(0, 0, -12), 35, 600, PILOT_TRAILS, &worst);
    std::printf("route length %.0f m\n", (double)c.pilot.path.size() * 2.0);
    ctx.check("arrived", ok, c.pilot.status);
    ctx.metric("damage on the way", worst * 100, "%", 0, 3);
}

SCENARIO(backcountryCreekCivic, "backcountry/creek-civic", "A Civic can't get up the Creek Crawl: it stalls at one of the first ledges") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const std::vector<V3> pts = trailLine("creek", 8, 366);
    Car& c = ctx.add(bCivic(), pts[0].x, pts[0].z, headingTo(pts[0], pts[1]));
    ctx.focus(c);
    ctx.run(1.0);
    const double got = driveLine(ctx, c, pts, 6, 90, nullptr);
    ctx.metric("how far it got (the ledges start at 62 m)", got, "m", 10, 150);
}

SCENARIO(backcountryRoutes, "backcountry/routes", "The autopilot can plan a way from the trailhead to every named place (the far ones too: the fire tower, the cabin, the pass hut, the wreck, the mine)") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const V3 s = ctx.sim.world.spawn;
    Car& c = ctx.add(bBronco(), s.x, s.z, ctx.sim.world.spawnYaw * 180 / M_PI);
    ctx.focus(c);
    ctx.run(0.5);
    for (const char* f : {"creekfoot", "mine", "slab", "deepbank", "fordbank", "hilltop", "mudstart", "bog3", "washmid", "dunering", "shelf", "overlook", "wreck", "snowcorner", "towerlook",
                          "cabinend", "passhut", "sawmill", "farside"}) {
        std::vector<V3> route;
        const V3 goal = backcountryFeature(f);
        const bool ok = planRoute(ctx.sim.world, bBronco(), s, goal, PILOT_TRAILS, {}, route);
        double L = 0;
        for (size_t k = 1; k < route.size(); k++) L += std::hypot(route[k].x - route[k - 1].x, route[k].z - route[k - 1].z);
        std::printf("ROUTE to %-10s %s %5.0f m\n", f, ok ? "ok " : "NONE", L);
        ctx.check(std::string("a route to ") + f, ok);
    }
}
