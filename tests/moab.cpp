// Moab (assets/maps/moab, docs/real-terrain.md, docs/plans/real-moab.md, docs/plans/moab-trails.md): the real map the game ships, box D
// (16.5 km). Its import is the same every time (the files' hash, the plan's fingerprint, the land's hash) and its heights are the real
// ones; the map builds with its town, its river and the bridge over it; its famous trails are on OpenStreetMap's lines and its named
// obstacles at OpenStreetMap's points; and each course is shown completable start to finish through every gate (the hard ones by a
// set-up Bronco, taking the bypasses the real trails have; the easy one by a stock 4Runner). That check proves a course can be done;
// it doesn't judge how it plays (docs/terrain-generator-handoff.md).
#include <algorithm>
#include <cmath>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../src/cliffwalls.h"
#include "../src/level.h"
#include "../src/places.h"
#include "../src/platform.h"
#include "../src/props.h"
#include "../src/realterrain.h"
#include "../src/rivers.h"
#include "../src/worldcache.h"
#include "harness.h"

namespace rt = realterrain;

namespace {
// What the shipped files make (update with the files: a new crop, a changed terrain.txt or a change to the importer that moves the land).
constexpr uint64_t kMoabFiles = 0x5de765863760d6ecull;  // the DEM's bytes, terrain.txt, moab.txt, osm.txt, the lidar patches and cover.tif (Spec::hash)
constexpr uint64_t kMoabPlan = 0x675c14bcf4759c2eull;  // places, trails, obstacles and courses (planFingerprint)
constexpr uint64_t kMoabLand = 0x2e75363ef34b0a00ull;  // World::landHash, code-made rocks (Linux x86-64)
// A built Moab is about 2.3 GB (the 16.5 km grid): one at a time, whatever --jobs is (docs/real-terrain.md, memory).
std::mutex gOneMoab;

std::string moabDir() {
    const std::string d = findAssetDir("assets/maps");
    return d.empty() ? std::string() : d + "/moab";
}
std::shared_ptr<const rt::Terrain> loadMoab(std::string* why = nullptr) {
    rt::Spec s;
    std::string err;
    if (!rt::loadSpec(moabDir(), s, &err)) {
        if (why) *why = err;
        return nullptr;
    }
    auto t = rt::prepare(s, &err);
    if (why) *why = err;
    return t;
}
// The plan, as numbers: where every place, trail point and course is.
uint64_t planFingerprint(const rt::Terrain& t) {
    const rt::Terrain::Plan& p = t.plan();
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](double v) {
        const int64_t q = (int64_t)std::llround(v * 100);
        for (int k = 0; k < 8; k++) h = (h ^ (uint8_t)(q >> (8 * k))) * 1099511628211ull;
    };
    for (const MapPlace& m : p.places) mix(m.p.x), mix(m.p.z), mix(m.kind);
    for (const rt::PlanTrail& tr : p.trails)
        for (const V3& q : tr.pts) mix(q.x), mix(q.z), mix(q.y);
    for (const rt::PlanCourse& c : p.courses) mix(c.length), mix(c.climb), mix((double)c.line.size()), mix((double)c.easiest.size());
    for (const rt::PlanRiver& r : p.rivers) mix((double)r.pts.size());
    for (const rt::PlanObstacle& o : p.obstacles) mix(o.at.x), mix(o.at.z), mix(o.from), mix(o.to), mix(o.bypass);
    return h;
}
const VehicleSpec& vehicle(const std::string& id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (id == v.id) return v;
    return vehicleCatalog()[0];
}
std::unique_ptr<World> buildMoab(std::shared_ptr<const rt::Terrain> t) {
    rt::ScopedTerrain scope(t);
    props::ScopedRockScans none(nullptr);
    const bool was = worldcache::bypass;
    worldcache::bypass = true;
    auto w = std::make_unique<World>(MAP_REAL);
    worldcache::bypass = was;
    return w;
}
const double kRealMetres[][3] = {
    // lat, lon, height (m): the plan's checked heights (docs/plans/real-moab.md), the ones inside the map
    {38.573, -109.550, 1229},  // Moab town
    {38.560, -109.580, 1347},  // Moab Rim
    {38.587, -109.513, 1424},  // Hell's Revenge slickrock
    {38.578, -109.495, 1460},  // Sand Flats (near the map's east edge)
    {38.560, -109.625, 1373},  // Poison Spider Mesa
};
}  // namespace

SCENARIO(moabImport, "moab/import", "Moab's files import the same every time (files, plan and land fingerprints) with the real heights: the checked points within 12 m, the Colorado at 1205 m") {
    std::string why;
    auto t = loadMoab(&why);
    ctx.check("the shipped map loads (assets/maps/moab)", t != nullptr, why);
    if (!t) return;
    ctx.check("named MOAB, 16.5 km square (box D)", t->spec.name == "MOAB" && t->half == 8250, t->spec.name + " " + std::to_string(t->half));
    char b[64];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)t->spec.hash);
    ctx.check("its files are the ones shipped", t->spec.hash == kMoabFiles, b);
    ctx.check("its backdrop (the 30 m country round it) loads, reaching 30 km or more", !t->backdrop.v.empty() && t->bdReach > 30000, std::to_string(t->bdReach));
    for (const auto& p : kRealMetres) {
        double x, z;
        t->toMap({p[0], p[1]}, x, z);
        const double e = t->realElevation(t->demAt(x, z));
        std::snprintf(b, sizeof b, "%.1f m (real %.0f)", e, p[2]);
        ctx.check("the height at " + std::to_string(p[0]).substr(0, 6) + " " + std::to_string(p[1]).substr(0, 8), std::fabs(e - p[2]) < 12, b);
    }
    {  // The river at the US-191 bridge: its lowest within 300 m.
        double x, z, lo = 1e9;
        t->toMap({38.605, -109.575}, x, z);
        for (double a = -300; a <= 300; a += 10)
            for (double c = -300; c <= 300; c += 10) lo = std::min(lo, t->realElevation(t->demAt(x + a, z + c)));
        std::snprintf(b, sizeof b, "%.1f m", lo);
        ctx.check("the Colorado at the US-191 bridge, 1205 m", std::fabs(lo - 1205) < 5, b);
    }
    // The same again from the files: the same plan.
    auto t2 = loadMoab();
    const uint64_t f1 = planFingerprint(*t), f2 = t2 ? planFingerprint(*t2) : 0;
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)f1);
    ctx.check("prepared twice, the same plan", f1 == f2, b);
    ctx.check("the plan's fingerprint", f1 == kMoabPlan, b);
    const rt::Terrain::Plan& p = t->plan();
    ctx.check("nine courses, each on its trail", p.courses.size() == 9);
    bool town = false;
    for (const MapPlace& m : p.places) town |= m.name == "MOAB" && (m.services & SVC_REPAIR) && (m.services & SVC_FUEL) && (m.services & SVC_SHOP);
    ctx.check("MOAB is the first place: repair, fuel and shop", town && !p.places.empty() && p.places[0].name == "MOAB");
    bool river = false;
    for (const rt::PlanRiver& r : p.rivers) river |= r.name == "COLORADO RIVER" && r.pts.size() > 400;
    ctx.check("the Colorado River runs across the map", river);
    ctx.check("one bridge (US-191)", p.bridges.size() == 1);
    for (const rt::PlanCourse& c : p.courses)
        std::printf("MOAB COURSE %-20s %.2f km, %4.0f m up, difficulty %d, %s\n", c.spec.name.c_str(), c.length / 1000, c.climb, c.spec.difficulty,
                    c.spec.vehicle.empty() ? "any" : c.spec.vehicle.c_str());
}

SCENARIO(moabMap, "moab/map", "Moab as a map: on the list as REAL: MOAB, built with the town at the start, the Colorado deep and unfordable, a deck over it, the land hash fixed") {
    std::lock_guard<std::mutex> one(gOneMoab);
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    rt::ScopedTerrain scope(t, {findAssetDir("assets/maps")});
    ctx.check("REAL: MOAB on the map list", mapListed(MAP_REAL) && std::string(mapName(MAP_REAL)) == "REAL: MOAB", mapName(MAP_REAL));
    std::unique_ptr<World> w = buildMoab(t);
    ctx.metric("land cells", (double)w->gridSize() * w->gridSize(), "", 2.7e8, 2.8e8);
    char b[64];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)w->landHash());
    std::printf("MOAB LAND %s\n", b);
#if defined(__linux__) && defined(__x86_64__)
    ctx.check("its land hash (as built on Linux x86-64)", w->landHash() == kMoabLand, b);  // (the rocks' placement uses sin/cos: elsewhere it may differ)
#else
    ctx.note(std::string("land hash ") + b);
#endif
    const MapPlace* moab = findPlaceByName(MAP_REAL, "MOAB");
    ctx.check("MOAB is a place on the map", moab != nullptr);
    if (moab) ctx.check("the start is in MOAB", std::hypot(w->spawn.x - moab->p.x, w->spawn.z - moab->p.z) < 40);
    ctx.check("a station (repair) at MOAB", !w->stations.empty());
    const River* col = nullptr;
    for (const River& r : w->rivers)
        if (r.name == "COLORADO RIVER") col = &r;
    ctx.check("the Colorado River is on the map", col != nullptr);
    if (col) {
        int deep = 0;
        for (const RiverSpot& s : col->spots) deep += s.depth >= 3 && s.cls != RIVER_FORD;
        ctx.check("...deep (3 m and more) and no ford on it", deep == (int)col->spots.size(), std::to_string(deep) + " of " + std::to_string(col->spots.size()));
    }
    const rt::PlanBridge& br = t->plan().bridges.at(0);
    const V3 mid = (br.a + br.b) * 0.5;
    if (std::getenv("MOAB_TRACE")) {
        for (int k = -4; k <= 24; k++) {
            const V3 q = br.a + (br.b - br.a) * (k / 20.0);
            std::printf("BRIDGE %d (%.0f %.0f) ground %.2f water %.2f clear %.2f\n", k, q.x, q.z, w->height(q.x, q.z), w->waterLevel(q.x, q.z), w->clearY(q.x, q.z));
        }
        for (const River& r : w->rivers)
            if (r.name == "COLORADO RIVER")
                for (const RiverSpot& s : r.spots)
                    if (s.depth < 3 || s.cls == RIVER_FORD) std::printf("SHALLOW (%.0f %.0f) %.2f cls %d\n", s.p.x, s.p.z, s.depth, s.cls);
    }
    (void)mid;
    // Over the water under it, the deck: a box a few metres over the water, and a road onto each end.
    int wetUnder = 0, decked = 0;
    for (int k = 0; k <= 40; k++) {
        const V3 q = br.a + (br.b - br.a) * (k / 40.0);
        const double wl = w->waterLevel(q.x, q.z);
        if (wl < -1e8) continue;
        wetUnder++;
        for (const Box& bx : w->boxes)
            if (std::hypot(bx.c.x - q.x, bx.c.z - q.z) < 8 && bx.c.y > wl + 2) {
                decked++;
                break;
            }
    }
    std::snprintf(b, sizeof b, "%d of %d points over the water decked", decked, wetUnder);
    ctx.check("the bridge's deck spans the river, well over the water", wetUnder >= 3 && decked == wetUnder, b);
    ctx.check("...and a road reaches each end", w->roadAt(br.a.x, br.a.z).near && w->roadAt(br.b.x, br.b.z).near);
    ctx.check("the named trails on the M map", w->trails.size() >= 20);
}

namespace {
// Pure pursuit along the course line at about `kmh` (as the Backcountry's trail tests drive); stuck, it backs off and tries again
// (five goes). Returns how far along it got; `gates` how many of the level's gates it came through (within half their width).
double driveCourse(Ctx& ctx, Car& c, const std::vector<V3>& pts, const Level& lv, double kmh, double maxS, int& gates, double& worst) {
    std::vector<double> at(pts.size(), 0);
    for (size_t k = 1; k < pts.size(); k++) at[k] = at[k - 1] + std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    std::vector<uint8_t> through(lv.gates.size(), 0);
    size_t seg = 0;
    double best = 0, lastBest = 0, progressT = ctx.t, backUntil = -1;
    int tries = 0;
    ctx.runUntil(maxS, [&] { return seg + 1 >= pts.size() || ctx.t - progressT > 45; }, [&] {
        const Frame f = c.frame();
        for (size_t g = 0; g < lv.gates.size(); g++)
            if (std::hypot(f.com.x - lv.gates[g].p.x, f.com.z - lv.gates[g].p.z) < 0.5 * lv.gates[g].width) through[g] = 1;
        worst = std::max(worst, c.damage);
        if (ctx.t < backUntil) {
            c.gear = -1, c.throttle = 0.45, c.brake = 0, c.steer = 0;
            return;
        }
        if (backUntil > 0) c.gear = 1, backUntil = -1;
        while (seg + 1 < pts.size()) {
            const V3 a = pts[seg], b = pts[seg + 1];
            const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
            if (dot(f.com - a, dir) < std::hypot(b.x - a.x, b.z - a.z)) break;
            seg++;
        }
        if (seg + 1 >= pts.size()) return;
        const V3 a = pts[seg], b = pts[seg + 1];
        const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z));
        const double al = dot(f.com - a, dir);
        best = std::max(best, at[seg] + al);
        if (best > lastBest + 0.5) lastBest = best, progressT = ctx.t;
        if (ctx.t - progressT > 6 * (tries + 1) && tries < 5) {
            tries++, backUntil = ctx.t + 2.0;
            return;
        }
        const double v = dot(f.vel, f.fwd), look = 4 + 0.6 * std::fabs(v);
        const double s = at[seg] + al + look;
        size_t k = seg;
        while (k + 2 < pts.size() && at[k + 1] < s) k++;
        const V3 q = pts[k] + (pts[k + 1] - pts[k]) * clampd((s - at[k]) / std::max(1e-6, at[k + 1] - at[k]), 0, 1);
        V3 aim = q - f.com;
        aim.y = 0;
        c.steer = clampd(std::atan2(dot(aim, f.left), dot(aim, f.fwd)) * 1.6, -1.0, 1.0);
        const double want = kmh / 3.6;
        c.throttle = clampd(0.35 + (want - v) * 0.6, 0, c.lowRange ? 0.7 : 1.0), c.brake = v > want + 1.5 ? 0.4 : 0, c.handbrake = 0;
        static thread_local int fr = 0;
        if (std::getenv("MOAB_TRACE") && ++fr % 2000 == 0) std::printf("TRACE t %.0f along %.0f of %.0f (%.0f %.0f) v %.1f\n", ctx.t, best, at.back(), f.com.x, f.com.z, v);
    });
    if (std::getenv("MOAB_TRACE")) {
        const Frame f = c.frame();
        const World& w = ctx.sim.world;
        std::printf("STOPPED at (%.1f %.1f %.1f) along %.0f, fwd (%.2f %.2f %.2f) gear %d, ground %.2f, surface %d, slope here %.2f\n", f.com.x, f.com.y, f.com.z, best,
                    f.fwd.x, f.fwd.y, f.fwd.z, c.gear, w.height(f.com.x, f.com.z), (int)w.surfaceAt(f.com.x, f.com.z),
                    std::hypot(w.height(f.com.x + 2, f.com.z) - w.height(f.com.x - 2, f.com.z), w.height(f.com.x, f.com.z + 2) - w.height(f.com.x, f.com.z - 2)) / 4);
        for (const Rock& k : w.rocks)
            if (std::hypot(k.c.x - f.com.x, k.c.z - f.com.z) < 8) std::printf("  rock (%.1f %.1f %.1f) r %.2f\n", k.c.x, k.c.y, k.c.z, k.r);
        for (const Pole& p : w.poles)
            if (std::hypot(p.x - f.com.x, p.z - f.com.z) < 8) std::printf("  pole (%.1f %.1f) r %.2f\n", p.x, p.z, p.r);
        for (const Box& b : w.boxes)
            if (std::hypot(b.c.x - f.com.x, b.c.z - f.com.z) < 14) std::printf("  box (%.1f %.1f)\n", b.c.x, b.c.z);
        for (size_t k = 0; k + 1 < pts.size(); k += 3)
            if (std::fabs(at[k] - best) < 30) std::printf("  line %.0f (%.1f %.1f) ground %.2f\n", at[k], pts[k].x, pts[k].z, w.height(pts[k].x, pts[k].z));
    }
    gates = 0;
    for (uint8_t g : through) gates += g;
    return seg + 1 >= pts.size() ? at.back() : best;
}

// A course of the shipped Moab, as the level list has it, driven start to finish.
void driveMoab(Ctx& ctx, const std::string& name) {
    std::lock_guard<std::mutex> one(gOneMoab);
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    rt::ScopedTerrain scope(t, {findAssetDir("assets/maps")});
    const rt::PlanCourse* pc = nullptr;
    for (const rt::PlanCourse& c : t->plan().courses)
        if (c.spec.name == name) pc = &c;
    Level lv;
    bool found = false;
    for (const Level& l : realCourseLevels())
        if (l.name == name) lv = l, found = true;
    ctx.check(name + " is a level (a sprint on MAP_REAL)", found && pc && lv.map == MAP_REAL && lv.mode == LEVEL_SPRINT && lv.gates.size() >= 3);
    if (!found || !pc) return;
    {
        std::unique_ptr<World> w = buildMoab(t);
        ctx.sim.world = std::move(*w);
    }
    applyLevel(lv, ctx.sim.world);
    const World& w = ctx.sim.world;
    ctx.check("its start is on dry ground", w.waterLevel(lv.start.x, lv.start.z) < w.height(lv.start.x, lv.start.z));
    // (the line with its optional obstacles taken by their bypasses: the real trails' way through for a capable rig)
    std::vector<V3> pts;
    for (size_t k = 2; k < pc->easiest.size(); k += 2) pts.push_back(pc->easiest[k]);
    pts.push_back(pc->easiest.back());
    // (Tuning only: MOAB_FROM=M [MOAB_SPAN=S] drives just S m (default 300) of it from M m along, no gates: one obstacle at a time.)
    V3 start = lv.start;
    double startYaw = lv.startYaw;
    const bool part = std::getenv("MOAB_FROM") != nullptr;
    if (part) {
        const double from = std::atof(std::getenv("MOAB_FROM")), span = std::getenv("MOAB_SPAN") ? std::atof(std::getenv("MOAB_SPAN")) : 300;
        std::vector<V3> cut;
        double a = 0;
        for (size_t k = 0; k < pts.size(); k++) {
            if (k) a += std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
            if (a >= from && a <= from + span) cut.push_back(pts[k]);
        }
        if (cut.size() >= 4) {
            pts.swap(cut);
            start = pts[0], startYaw = std::atan2(-(pts[2].z - pts[0].z), pts[2].x - pts[0].x);
            lv.gates.clear();
        }
    }
    const std::string id = std::getenv("MOAB_VEHICLE") ? std::getenv("MOAB_VEHICLE") : pc->spec.vehicle.empty() ? "4runner" : pc->spec.vehicle;
    const VehicleSpec& spec = vehicle(id);
    Car& c = ctx.add(spec, start.x, start.z, startYaw * 180 / M_PI);
    ctx.focus(c);
    ctx.run(1.0);
    const bool hard = pc->spec.difficulty >= 3;
    if (hard) {  // set up: low range, both lockers, aired down, no traction control
        c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
        c.setLocker(false, true);
        c.setLocker(true, true);
        c.tirePsi = 18;
        c.tractionControl = false;
    }
    const double kmh = hard ? 10 : 30;
    int gates = 0;
    double worst = 0;
    const double got = driveCourse(ctx, c, pts, lv, kmh, pc->length / (kmh / 3.6) * 3 + 120, gates, worst);
    double want = 0;
    for (size_t k = 1; k < pts.size(); k++) want += std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    char b[160];
    std::snprintf(b, sizeof b, "%.0f of %.0f m in %.0f s, %d of %d gates, damage %.1f%%", got, want, ctx.t, gates, (int)lv.gates.size(), worst * 100);
    std::printf("MOAB DRIVE %s (%s%s): %s\n", name.c_str(), spec.name, hard ? ", set up" : ", stock", b);
    ctx.metric("driven", got / std::max(1.0, want), "", 0.97, 1.01);
    ctx.metric("course time", ctx.t, "s", 0, 1e9);
    ctx.check(std::string("a ") + spec.name + (hard ? " (set up)" : " (stock)") + " drives it start to finish", got > want - 20, b);
    ctx.check("...through every gate", gates == (int)lv.gates.size(), b);
    ctx.check("...in one piece", worst < 0.6, b);
}
}  // namespace

SCENARIO(moabSandFlats, "moab/sand-flats-road", "SAND FLATS ROAD (difficulty 1): a stock 4Runner up the county road from Mill Creek Drive to the Fins N' Things trailhead, every gate") { driveMoab(ctx, "SAND FLATS ROAD"); }
SCENARIO(moabHellsRevenge, "moab/hells-revenge", "HELL'S REVENGE (difficulty 4, RR4W 6): a set-up Bronco round OSM's line from the trailhead to the exit, every gate (the options left to the player)") { driveMoab(ctx, "HELL'S REVENGE"); }
SCENARIO(moabFins, "moab/fins-n-things", "FINS N' THINGS (difficulty 3, RR4W 4): a set-up Bronco east on the daylight section and back on the north section, every gate") { driveMoab(ctx, "FINS N' THINGS"); }
SCENARIO(moabRim, "moab/moab-rim", "MOAB RIM (difficulty 5, RR4W 7): a set-up Bronco from Kane Creek Road up the slab, Devil's Crack and the Z Turn, round the dome on top, every gate") { driveMoab(ctx, "MOAB RIM"); }
SCENARIO(moabPoisonSpider, "moab/poison-spider", "POISON SPIDER MESA (difficulty 4, RR4W 6): a set-up Bronco from the Potash Road trailhead up the Waterfall to the mesa, every gate") { driveMoab(ctx, "POISON SPIDER MESA"); }
SCENARIO(moabGoldenSpike, "moab/golden-spike", "GOLDEN SPIKE (difficulty 5, RR4W 7): a set-up Bronco from Poison Spider to Gold Bar Rim over all its named obstacles (Double Whammy by its bypass), every gate") { driveMoab(ctx, "GOLDEN SPIKE"); }
SCENARIO(moabGoldBar, "moab/gold-bar-rim", "GOLD BAR RIM (difficulty 4, RR4W 6): a set-up Bronco from the US-191 trailhead up the Waterfall to the rim, every gate") { driveMoab(ctx, "GOLD BAR RIM"); }
SCENARIO(moabCliffhanger, "moab/cliffhanger", "CLIFFHANGER (difficulty 5, RR4W 8): a set-up Bronco from Kane Creek Road up Amasa Back and over the Cliffhanger ledge, every gate") { driveMoab(ctx, "CLIFFHANGER"); }
SCENARIO(moabMetalMasher, "moab/metal-masher", "METAL MASHER (difficulty 5, RR4W 7): a set-up Bronco over Tailpipe Hill and Mirror Gulch to Eagles Perch (Rock Chunker by its bypass), every gate") { driveMoab(ctx, "METAL MASHER"); }

namespace {
// Where an OSM point is on the map.
V3 osmPoint(const rt::Terrain& t, int64_t node) {
    const rt::OsmNode* n = t.spec.osmNode(node);
    double x = 0, z = 0;
    if (n) t.toMap(n->at, x, z);
    return V3(x, 0, z);
}
// The nearest a polyline comes to (x, z) in plan (m).
double distToLine(const std::vector<V3>& L, const V3& p) {
    double best = 1e18;
    for (size_t k = 0; k + 1 < L.size(); k++) {
        const V3 a = L[k], b = L[k + 1];
        const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz;
        const double u = L2 > 1e-12 ? clampd(((p.x - a.x) * dx + (p.z - a.z) * dz) / L2, 0, 1) : 0;
        best = std::min(best, std::hypot(p.x - a.x - dx * u, p.z - a.z - dz * u));
    }
    return best;
}
}  // namespace

SCENARIO(moabCliffWalls, "moab/cliff-walls", "Moab's cliff walls (cliffwalls.h) along its trails: carved into the rock, never out past the land a tire hits (no vertex outside it), the same every build") {
    std::lock_guard<std::mutex> one(gOneMoab);
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    std::unique_ptr<World> w = buildMoab(t);
    const double half = w->halfSize();
    // The terrain chunks (64 m) within 120 m of the named trails, every 40 m along them.
    std::set<std::pair<int, int>> chunks;
    for (const rt::PlanTrail& tr : t->plan().trails) {
        if (tr.name.empty()) continue;
        for (size_t k = 0; k < tr.pts.size(); k += 40)
            for (int dx = -120; dx <= 120; dx += 60)
                for (int dz = -120; dz <= 120; dz += 60) {
                    const int i = (int)std::floor((tr.pts[k].x + dx + half) / 64), j = (int)std::floor((tr.pts[k].z + dz + half) / 64);
                    if (i >= 0 && j >= 0 && (i + 1) * 64 < w->gridSize() && (j + 1) * 64 < w->gridSize()) chunks.insert({i, j});
                }
    }
    std::vector<std::pair<int, int>> list(chunks.begin(), chunks.end());
    // (a spread of them: every 7th)
    std::vector<std::pair<int, int>> pick;
    for (size_t k = 0; k < list.size(); k += 7) pick.push_back(list[k]);
    cliffwalls::Beds beds;
    size_t verts = 0, withWalls = 0, outside = 0, reachable = 0;
    double worst = 0, deepest = 0;
    for (const auto& c : pick) {
        std::vector<cliffwalls::Vert> v;
        std::vector<unsigned> idx;
        cliffwalls::build(*w, beds, c.first * 64, c.second * 64, 64, 64, 7, v, idx);
        verts += v.size(), withWalls += !idx.empty();
        for (const cliffwalls::Vert& q : v) {
            const double x = q.p[0], y = q.p[1], z = q.p[2], out = y - w->height(x, z);
            deepest = std::max(deepest, (double)q.c[1]);
            if (out <= 0.005) continue;
            outside++, worst = std::max(worst, out);
            // (outside the land: then it's up a steep face, well above the lowest ground round it)
            double low = 1e9;
            for (int a = -4; a <= 4; a++)
                for (int b = -4; b <= 4; b++) low = std::min(low, w->height(x + a, z + b));
            const double gx = w->height(x + 0.5, z) - w->height(x - 0.5, z), gz = w->height(x, z + 0.5) - w->height(x, z - 0.5);
            if (y - low < 1.5 || gx * gx + gz * gz < 1.0) reachable++;
        }
    }
    ctx.metric("chunks looked at", (double)pick.size(), "", 50, 1e9);
    ctx.metric("chunks with walls", (double)withWalls, "", 10, 1e9);
    ctx.metric("deepest carving", deepest, "m", 1.0, 4.0);
    ctx.metric("worst outside the land", worst, "m", 0, 0.05);
    ctx.metric("vertices outside the land where a tire could be", (double)reachable, "", 0, 0);
    ctx.note(std::to_string(verts) + " vertices, " + std::to_string(outside) + " outside the land");
    // The same every build.
    if (!pick.empty()) {
        const auto c = pick[pick.size() / 2];
        std::vector<cliffwalls::Vert> a, b;
        std::vector<unsigned> ia, ib;
        cliffwalls::build(*w, beds, c.first * 64, c.second * 64, 64, 64, 7, a, ia);
        cliffwalls::build(*w, beds, c.first * 64, c.second * 64, 64, 64, 7, b, ib);
        ctx.check("built twice, the same mesh", ia == ib && a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0));
    }
}

SCENARIO(moabTrails, "moab/osm-trails", "Moab's famous trails are on OpenStreetMap's lines (credited, ODbL): each trail's laid line within a few metres of its OSM ways where they're on the map; Lion's Back left off (closed, private)") {
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    ctx.check("its OSM file is credited (OpenStreetMap contributors, ODbL)", t->spec.osmText.find("OpenStreetMap contributors") != std::string::npos && t->spec.osmText.find("ODbL") != std::string::npos);
    const rt::Terrain::Plan& p = t->plan();
    for (const char* name : {"SAND FLATS ROAD", "HELL'S REVENGE", "FINS N' THINGS", "MOAB RIM", "POISON SPIDER MESA", "GOLDEN SPIKE", "GOLD BAR RIM", "CLIFFHANGER", "METAL MASHER",
                             "HELL'S GATE", "ESCALATOR", "MICKEY'S HOT TUB", "TIPOVER CHALLENGE", "STAIRCASE"}) {
        const rt::SpecTrail* st = nullptr;
        const rt::PlanTrail* pt = nullptr;
        for (const rt::SpecTrail& s : t->spec.trails)
            if (s.name == name) st = &s;
        for (const rt::PlanTrail& q : p.trails)
            if (q.name == name && !pt) pt = &q;  // (the authored one: the rules' come after, and may share its OSM name)
        if (!st || !pt || st->osmWays.empty()) {
            ctx.check(std::string(name) + " is on the map from OSM", false);
            continue;
        }
        const std::vector<V3> laid = rt::smoothLine(pt->pts, 1.0);
        // Every OSM vertex well inside the map (and between the trail's cuts) within 6 m of the line as laid (it's smoothed a little).
        int n = 0, near = 0;
        double worst = 0, len = 0;
        for (size_t k = 1; k < laid.size(); k++) len += std::hypot(laid[k].x - laid[k - 1].x, laid[k].z - laid[k - 1].z);
        std::string ids;
        for (int64_t w : st->osmWays) {
            ids += (ids.empty() ? "" : " ") + std::to_string(w < 0 ? -w : w);
            for (const rt::LatLon& ll : t->spec.osmWay(w < 0 ? -w : w)->pts) {
                double x, z;
                t->toMap(ll, x, z);
                if (std::fabs(x) > t->half - 230 || std::fabs(z) > t->half - 230) continue;
                const V3 q(x, 0, z);
                if (std::hypot(q.x - laid.front().x, q.z - laid.front().z) > 0 && distToLine(laid, q) > 50) continue;  // (cut off by from/to)
                const double d = distToLine(laid, q);
                n++, near += d < 6, worst = std::max(worst, d);
            }
        }
        char b[200];
        std::snprintf(b, sizeof b, "%.2f km; %d of %d OSM points within 6 m (worst %.1f m); ways %s", len / 1000, near, n, worst, ids.c_str());
        std::printf("MOAB TRAIL %-24s %s\n", name, b);
        ctx.check(std::string(name) + " follows its OSM ways", n >= 2 && near >= n * 0.97, b);
    }
    // Lion's Back: closed. A sign at it, no trail within 40 m of its top (Hell's Revenge and the alternate route pass its foot).
    const rt::SpecLandmark* lion = nullptr;
    for (const rt::SpecLandmark& m : t->spec.landmarks)
        if (m.name.find("LION'S BACK") == 0) lion = &m;
    ctx.check("Lion's Back is marked closed", lion && lion->name.find("CLOSED") != std::string::npos);
    if (lion) {
        const V3 q = osmPoint(*t, lion->node);
        double nearest = 1e9;
        for (const rt::PlanTrail& tr : p.trails) nearest = std::min(nearest, distToLine(tr.pts, q));
        ctx.check("...and no trail onto it", nearest > 40, std::to_string(nearest));
    }
}

SCENARIO(moabObstacles, "moab/obstacles", "Moab's named obstacles: each at its OpenStreetMap point (its built stretch within 4 m of it), shaped and laid with scanned-rock ledges as authored, named over it, its bypass laid where the real trail has one") {
    std::lock_guard<std::mutex> one(gOneMoab);
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    const rt::Terrain::Plan& p = t->plan();
    // The ones the owner named (docs/plans/moab-trails.md), all inside box D.
    for (const char* name : {"HELL'S GATE", "ESCALATOR", "MICKEY'S HOT TUB", "TIPOVER CHALLENGE", "THE STAIRCASE", "LAUNCHING PAD", "ZUKI HILL", "GOLDEN CRACK",
                             "GOLDEN STAIRCASE", "DOUBLE WHAMMY", "BODY SNATCHER", "THE WATERFALL", "ROCK CHUNKER", "MIRROR GULCH", "TAILPIPE HILL", "MOAB RIM DOME"}) {
        bool found = false;
        for (const rt::PlanObstacle& o : p.obstacles) found |= o.spec.name == name;
        ctx.check(std::string(name) + " is on the map", found);
    }
    rt::ScopedTerrain scope(t, {findAssetDir("assets/maps")});
    std::unique_ptr<World> w = buildMoab(t);
    for (const rt::PlanObstacle& o : p.obstacles) {
        const rt::SpecObstacle& so = o.spec;
        const std::vector<V3> L = rt::smoothLine(p.trails[o.trail].pts, 1.0);
        std::vector<double> at(L.size(), 0);
        for (size_t k = 1; k < L.size(); k++) at[k] = at[k - 1] + std::hypot(L[k].x - L[k - 1].x, L[k].z - L[k - 1].z);
        std::vector<V3> stretch;
        for (size_t k = 0; k < L.size(); k++)
            if (at[k] >= o.from - 0.5 && at[k] <= o.to + 0.5) stretch.push_back(L[k]);
        const V3 want = so.node ? osmPoint(*t, so.node) : o.at;
        const double off = stretch.size() >= 2 ? distToLine(stretch, want) : 1e9;
        char b[240];
        // Shaped: the land under the stretch isn't the DEM's (the profile moved somewhere by a ledge's height or more), and rocks on it.
        double moved = 0;
        for (size_t k = 0; k + 1 < stretch.size(); k++) {
            const V3 d = norm(V3(stretch[k + 1].x - stretch[k].x, 0, stretch[k + 1].z - stretch[k].z)), lf(-d.z, 0, d.x);
            for (double v = -so.width; v <= so.width + 1e-9; v += 0.5) {
                const V3 q = stretch[k] + lf * v;
                moved = std::max(moved, std::fabs(w->height(q.x, q.z) - t->heightAt(q.x, q.z)));
            }
        }
        int rocks = 0;
        for (const Rock& r : w->rocks)
            if (distToLine(stretch, r.c) < so.width + 6) rocks++;
        bool tagged = false;
        for (const World::Tag& g : w->tags) tagged |= g.text == so.name && std::hypot(g.p.x - o.at.x, g.p.z - o.at.z) < 1;
        // A ledge: at each lip the tread rises most of its height within 2 m (the grid's ramp; the rocks laid there make the face).
        double minStep = 1e9;
        auto hAlong = [&](double sAt) {
            size_t i = 0;
            while (i + 1 < at.size() && at[i] < sAt) i++;
            return w->height(L[i].x, L[i].z);
        };
        for (int k = 0; k < so.steps; k++) {  // (the most it rises in 2 m about each lip)
            const double lp = o.from + (o.to - o.from) * (k + 0.5) / so.steps;
            double best = 0;
            for (double x = lp - 2; x <= lp; x += 0.5) best = std::max(best, std::fabs(hAlong(x + 2) - hAlong(x)));
            minStep = std::min(minStep, best);
        }
        if (std::getenv("MOAB_TRACE"))
            for (double sAt = o.from - 6; sAt <= o.to + 6; sAt += 1) std::printf("  %s s %.0f h %.2f\n", so.name.c_str(), sAt, hAlong(sAt));
        // (how far the OSM node itself is off its way: one or two aren't on it)
        double nodeOff = 0;
        if (so.node) {
            const rt::SpecTrail* st = nullptr;
            for (const rt::SpecTrail& x : t->spec.trails)
                if (x.name == so.trail) st = &x;
            nodeOff = st ? 1e9 : distToLine(p.trails[o.trail].pts, want);  // (one by the rules: on a trail of OSM's own)
            for (int64_t wid : st ? st->osmWays : std::vector<int64_t>()) {
                std::vector<V3> wl;
                for (const rt::LatLon& ll : t->spec.osmWay(wid < 0 ? -wid : wid)->pts) {
                    double x, z;
                    t->toMap(ll, x, z);
                    wl.push_back(V3(x, 0, z));
                }
                nodeOff = std::min(nodeOff, distToLine(wl, want));
            }
        }
        std::snprintf(b, sizeof b, "%s on %s: %.1f m from %s; land moved up to %.2f m; %d rocks; %s%s", so.name.c_str(), so.trail.c_str(), off,
                      so.node ? ("OSM node " + std::to_string(so.node)).c_str() : "its point", moved, rocks, so.steps ? ("lips rise " + std::to_string(minStep).substr(0, 4) + " m; ").c_str() : "",
                      o.bypass >= 0 ? "bypass laid" : "no bypass");
        std::printf("MOAB OBSTACLE %s\n", b);
        ctx.check(so.name + " at its point (its stretch within 4 m, more where OSM's node is off its way)", off < 4 + nodeOff, b);
        ctx.check(so.name + ": shaped", moved > 0.3, b);
        ctx.check(so.name + ": named over it", tagged, b);
        if (so.steps > 0 || so.walls > 0 || so.boulders > 0) ctx.check(so.name + ": scanned rocks laid on it", rocks >= 3, b);
        if (so.steps > 0) ctx.check(so.name + ": each ledge rises most of its height within 2 m", minStep > 0.6 * so.rise, b);
        if (!so.bypass.empty()) {
            ctx.check(so.name + ": its bypass is laid", o.bypass >= 0, b);
            if (o.bypass >= 0) {
                const std::vector<V3>& by = p.trails[o.bypass].pts;
                const V3 mid = by[by.size() / 2];
                ctx.check(so.name + ": ...and is a trail on the ground", w->roadAt(mid.x, mid.z).near, b);
            }
        }
    }
}
