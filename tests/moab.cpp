// Moab (assets/maps/moab, docs/real-terrain.md, docs/plans/real-moab.md): the real map the game ships. Its import is the same every
// time (the files' hash, the plan's fingerprint, the land's hash) and its heights are the real ones; the map builds with its town, its
// river and the bridge over it; and each of its courses is driven start to finish through every gate: the hard ones by a set-up
// Bronco, the easy one by a stock 4Runner.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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
constexpr uint64_t kMoabFiles = 0x75671d7aca889dccull;  // the DEM's bytes and terrain.txt (Spec::hash)
constexpr uint64_t kMoabPlan = 0xa77c73b3ca463285ull;  // places, trails and courses (planFingerprint)
constexpr uint64_t kMoabLand = 0x6d9f143f6611e4cfull;  // World::landHash, code-made rocks (Linux x86-64)

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
    for (const rt::PlanCourse& c : p.courses) mix(c.length), mix(c.climb), mix((double)c.line.size());
    for (const rt::PlanRiver& r : p.rivers) mix((double)r.pts.size());
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
    {38.578, -109.490, 1482},  // Sand Flats
};
}  // namespace

SCENARIO(moabImport, "moab/import", "Moab's files import the same every time (files, plan and land fingerprints) with the real heights: the checked points within 12 m, the Colorado at 1205 m") {
    std::string why;
    auto t = loadMoab(&why);
    ctx.check("the shipped map loads (assets/maps/moab)", t != nullptr, why);
    if (!t) return;
    ctx.check("named MOAB, 12 km square", t->spec.name == "MOAB" && t->half == 6000, t->spec.name + " " + std::to_string(t->half));
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
    ctx.check("five courses, each on its trail", p.courses.size() == 5);
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
    auto t = loadMoab();
    ctx.check("the shipped map loads", t != nullptr);
    if (!t) return;
    rt::ScopedTerrain scope(t, {findAssetDir("assets/maps")});
    ctx.check("REAL: MOAB on the map list", mapListed(MAP_REAL) && std::string(mapName(MAP_REAL)) == "REAL: MOAB", mapName(MAP_REAL));
    std::unique_ptr<World> w = buildMoab(t);
    ctx.metric("land cells", (double)w->gridSize() * w->gridSize(), "", 1.4e8, 1.5e8);
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
    ctx.check("the named trails on the M map", w->trails.size() >= 5);
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
    std::vector<V3> pts;
    for (size_t k = 2; k < pc->line.size(); k += 2) pts.push_back(pc->line[k]);
    pts.push_back(pc->line.back());
    const std::string id = pc->spec.vehicle.empty() ? "4runner" : pc->spec.vehicle;
    const VehicleSpec& spec = vehicle(id);
    Car& c = ctx.add(spec, lv.start.x, lv.start.z, lv.startYaw * 180 / M_PI);
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
    const double want = pc->length;
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

SCENARIO(moabSandFlats, "moab/sand-flats-road", "SAND FLATS ROAD (difficulty 1): a stock 4Runner drives from Moab up the graded road to the Sand Flats entrance, every gate") { driveMoab(ctx, "SAND FLATS ROAD"); }
SCENARIO(moabHellsRevenge, "moab/hells-revenge", "HELL'S REVENGE (difficulty 4): a set-up Bronco over the slickrock fins from the Sand Flats trailhead, every gate") { driveMoab(ctx, "HELL'S REVENGE"); }
SCENARIO(moabFins, "moab/fins-n-things", "FINS N' THINGS (difficulty 3): a set-up Bronco round the loop over the Sand Flats fins, every gate") { driveMoab(ctx, "FINS N' THINGS"); }
SCENARIO(moabRim, "moab/moab-rim", "MOAB RIM (difficulty 5): a set-up Bronco up the slab from the river to the rim and on toward Hidden Valley, every gate") { driveMoab(ctx, "MOAB RIM"); }
SCENARIO(moabPoisonSpider, "moab/poison-spider", "POISON SPIDER MESA (difficulty 4): a set-up Bronco from the Potash Road side up onto the mesa, every gate") { driveMoab(ctx, "POISON SPIDER MESA"); }
