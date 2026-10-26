// The Backcountry's trail network (src/bctrails.cpp): the trails are where the table says, graded as their ratings say, marked, on the
// M map; COPPER PASS is a hard pass a set-up truck makes and a stock one doesn't; the rock features and the fill are there.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "../src/places.h"
#include "../src/props.h"
#include "harness.h"

namespace {
const VehicleSpec& vehicle(const char* id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (std::string(v.id) == id) return v;
    return vehicleCatalog()[0];
}
double headingTo(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI; }
// The trail's line from `from` to `to` m along it, every 2 m.
std::vector<V3> lineOf(const char* trail, double from, double to) {
    const std::vector<V3>& t = bcTrailLine(trail);
    std::vector<V3> pts;
    for (double s = std::max(0.0, from); s <= to + 1e-6 && s < (double)t.size() - 1; s += 2) pts.push_back(t[(size_t)s]);
    return pts;
}
double along(const char* trail, const V3& p) {
    const std::vector<V3>& t = bcTrailLine(trail);
    size_t best = 0;
    for (size_t k = 0; k < t.size(); k++)
        if (std::hypot(t[k].x - p.x, t[k].z - p.z) < std::hypot(t[best].x - p.x, t[best].z - p.z)) best = k;
    return (double)best;
}
// Pure pursuit along the line at about `kmh` (backcountry.cpp's tests drive the same way); how far along it got.
double drive(Ctx& ctx, Car& c, const std::vector<V3>& pts, double kmh, double maxS, double* worst) {
    std::vector<double> at(pts.size(), 0);
    for (size_t k = 1; k < pts.size(); k++) at[k] = at[k - 1] + std::hypot(pts[k].x - pts[k - 1].x, pts[k].z - pts[k - 1].z);
    size_t seg = 0;
    double best = 0, lastBest = 0, progressT = ctx.t, backUntil = -1;
    int tries = 0;
    ctx.runUntil(maxS, [&] { return seg + 1 >= pts.size() || ctx.t - progressT > 40; }, [&] {
        const Frame f = c.frame();
        // Stuck at an obstacle for a few seconds: back off a couple of metres and have another go at it, as a driver would (five goes).
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
            if (std::getenv("BC_TRACE")) std::printf("TRACE backing off at %.1f m (go %d)\n", best, tries);
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
        c.throttle = clampd(0.35 + (want - v) * 0.6, 0, c.lowRange ? 0.7 : 1.0), c.brake = v > want + 1.5 ? 0.4 : 0, c.handbrake = 0;  // (feathered in low range)
        if (worst) *worst = std::max(*worst, c.damage);
        static thread_local int fr = 0;
        if (std::getenv("BC_TRACE") && ++fr % 1000 == 0)
            std::printf("TRACE t %.1f (%.1f %.1f %.1f) v %.2f along %.1f\n", ctx.t, f.com.x, f.com.y, f.com.z, v, at[seg] + al);
    });
    if (std::getenv("BC_TRACE")) {
        const Frame f = c.frame();
        const World& w = ctx.sim.world;
        std::printf("STOPPED at (%.1f %.1f %.1f) fwd (%.2f %.2f %.2f), steer %.2f thr %.2f gear %d rpm %.0f\n", f.com.x, f.com.y, f.com.z, f.fwd.x, f.fwd.y, f.fwd.z, c.steer,
                    c.throttle, c.gear, c.rpm);
        for (const Rock& k : w.rocks)
            if (std::hypot(k.c.x - f.com.x, k.c.z - f.com.z) < 8) std::printf("  rock (%.1f %.1f %.1f) r %.2f\n", k.c.x, k.c.y, k.c.z, k.r);
        for (const Pole& p : w.poles)
            if (std::hypot(p.x - f.com.x, p.z - f.com.z) < 8) std::printf("  pole (%.1f %.1f) r %.2f\n", p.x, p.z, p.r);
        for (const Log& l : w.logs)
            if (std::hypot(l.a.x - f.com.x, l.a.z - f.com.z) < 10) std::printf("  log (%.1f %.1f)\n", l.a.x, l.a.z);
        for (const Box& b : w.boxes)
            if (std::hypot(b.c.x - f.com.x, b.c.z - f.com.z) < 10) std::printf("  box (%.1f %.1f)\n", b.c.x, b.c.z);
    }
    return seg + 1 >= pts.size() ? at.back() : best;
}
void setUp(Car& c, const VehicleSpec& spec) {  // low range, both lockers, aired down
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    c.setLocker(false, true);
    c.setLocker(true, true);
    c.tirePsi = 18;
    c.tractionControl = false;
}
// The 6 km Backcountry, once, for the tests that only look (its middle is the big map's, onemap/backcountry-kept).
const World& smallMap() {
    static std::once_flag once;
    static std::unique_ptr<World> w;
    std::call_once(once, [] {
        props::ScopedRockScans none(nullptr);
        World::stitchRegions = false;
        w = std::make_unique<World>(MAP_BACKCOUNTRY);
        World::stitchRegions = true;
    });
    return *w;
}
// A stretch of COPPER PASS driven by a set-up Bronco (or `who` as it comes): how far it got of how far.
void passStretch(Ctx& ctx, const char* from, const char* to, const char* who, bool setup, double kmh, double secs, double& got, double& want, double& worst) {
    World::stitchRegions = false;
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    World::stitchRegions = true;
    const double a = along("COPPER PASS", bcTrailFeature(from)), b = along("COPPER PASS", bcTrailFeature(to));
    const std::vector<V3> pts = lineOf("COPPER PASS", a, b);
    if (std::getenv("BC_TRACE"))
        for (size_t k = 0; k + 1 < pts.size(); k++)
            std::printf("LINE %5.0f (%.1f %.1f) h %.2f  rocks near %d\n", k * 2.0, pts[k].x, pts[k].z, ctx.sim.world.height(pts[k].x, pts[k].z), [&] {
                int n = 0;
                for (const Rock& r : ctx.sim.world.rocks) n += std::hypot(r.c.x - pts[k].x, r.c.z - pts[k].z) < r.r + 1.5;
                return n;
            }());
    const VehicleSpec& spec = vehicle(who);
    Car& c = ctx.add(spec, pts[0].x, pts[0].z, headingTo(pts[0], pts[2]));
    ctx.focus(c);
    ctx.run(1.0);
    if (setup) setUp(c, spec);
    else c.setDriveMode(std::min(2, (int)driveModes(spec.transfer).size() - 1));
    worst = 0;
    got = drive(ctx, c, pts, kmh, secs, &worst);
    want = (pts.size() - 1) * 2.0;
    std::printf("PASS STRETCH %s -> %s (%s%s): %.0f of %.0f m, damage %.1f%%\n", from, to, who, setup ? ", set up" : "", got, want, worst * 100);
}
}  // namespace

SCENARIO(bctrailsNetwork, "bctrails/network", "The trail network: every trail laid where its table says, graded no steeper than its rating allows, signed at both ends and on the M map; 25 km and more of it") {
    const World& w = smallMap();
    double total = 0;
    int marked = 0;
    for (const BcTrailInfo& t : bcTrails()) {
        const std::vector<V3>& L = bcTrailLine(t.name);
        ctx.check(t.name + ": a line", L.size() > 50, std::to_string(L.size()));
        if (L.size() < 50) continue;
        total += L.size();
        // Its tread: painted, and its grade (over 6 m, through the fords' cuttings too) within its rating's (with the cut and fill's slack).
        int painted = 0, n = 0;
        double steep = 0;
        V3 steepAt;
        for (size_t k = 6; k + 6 < L.size(); k += 3) {
            n++;
            painted += w.roadAt(L[k].x, L[k].z).near;
            const double g = std::fabs(w.height(L[k + 3].x, L[k + 3].z) - w.height(L[k - 3].x, L[k - 3].z)) / 6;
            if (g > steep) steep = g, steepAt = L[k];
        }
        const double limit = t.rating == 0 ? 0.17 : t.rating == 1 ? 0.24 : 0.45;
        std::printf("TRAIL %-24s %d  %5.0f m  steepest %.2f at (%.0f, %.0f)  painted %.0f%%\n", t.name.c_str(), t.rating, (double)L.size(), steep, steepAt.x, steepAt.z, 100.0 * painted / std::max(1, n));
        ctx.check(t.name + ": its tread laid", painted > 0.9 * n, std::to_string(painted) + "/" + std::to_string(n));
        if (t.rating <= 1) ctx.check(t.name + ": no steeper than its rating", steep <= limit, std::to_string(steep));
        for (const World::MarkedTrail& m : w.trails) marked += m.name == t.name;
    }
    ctx.metric("the network's length", total / 1000, "km", 25, 60);
    ctx.check("every trail on the M map", marked == (int)bcTrails().size(), std::to_string(marked));
    // Signs: one by each end of each trail.
    int signs = 0, signed_ = 0;
    for (const Prop& f : w.fixtures) signs += f.kind == PROP_SIGN;
    for (const BcTrailInfo& t : bcTrails()) signed_ += t.rating < 4;  // (a faint two-track has a cairn at each end, no sign)
    ctx.check("signs at the trails' ends", signs >= 2 * signed_, std::to_string(signs));
    // Copper Peak: the highest ground, snow on top; the Shelf runs along a real drop.
    const V3 peak = bcTrailFeature("peak"), slab = bcTrailFeature("slab");
    ctx.metric("Copper Peak's top", w.height(peak.x, peak.z), "m", 280, 330);
    ctx.check("snow on Copper Peak", w.surfaceAt(peak.x, peak.z) == SURF_SNOW);
    {
        const std::vector<V3>& L = bcTrailLine("COPPER PASS");
        const double s = along("COPPER PASS", slab);
        const V3 d = norm(L[(size_t)s + 3] - L[(size_t)s - 3]), l(-d.z, 0, d.x);
        double drop = 0;
        for (int side = -1; side <= 1; side += 2)
            for (double o = 5; o <= 12; o += 1) drop = std::max(drop, w.height(slab.x, slab.z) - w.height(slab.x + l.x * side * o, slab.z + l.z * side * o));
        ctx.metric("the Shelf: the drop beside the slab (5-12 m out)", drop, "m", 8, 40);
    }
    // Copper Creek has water where the pass crosses it.
    const V3 ck = bcTrailFeature("creek");
    double water = 0;
    {  // (on the pass's line, where it wades it)
        const std::vector<V3>& L = bcTrailLine("COPPER PASS");
        const double s0 = along("COPPER PASS", ck);
        for (double s = std::max(0.0, s0 - 30); s < s0 + 30 && s < (double)L.size(); s += 1)
            water = std::max(water, w.waterLevel(L[(size_t)s].x, L[(size_t)s].z) - w.height(L[(size_t)s].x, L[(size_t)s].z));
    }
    for (const River& r : w.rivers)
        if (r.name == "COPPER CREEK") std::printf("COPPER CREEK %zu spots, from (%.0f, %.0f) to (%.0f, %.0f); the pass crosses near (%.0f, %.0f)\n", r.spots.size(), r.spots.front().p.x,
                                                  r.spots.front().p.z, r.spots.back().p.x, r.spots.back().p.z, ck.x, ck.z);
    ctx.metric("Copper Creek at the pass: water", water, "m", 0.15, 0.6);
}

SCENARIO(bctrailsRocks, "bctrails/rocks", "Rock features and the fill: big boulders (car-sized and more) all over the new country, outcrops, the set pieces; never on a trail's tread") {
    const World& w = smallMap();
    std::set<std::pair<int, int>> tread;  // (2 m cells within 2 m of a trail's middle, but the pass's and its spur's)
    for (const BcTrailInfo& t : bcTrails()) {
        if (t.name == "COPPER PASS" || t.name == "COPPER PEAK SPUR") continue;
        for (const V3& p : bcTrailLine(t.name))
            for (int a = -1; a <= 1; a++)
                for (int b = -1; b <= 1; b++) tread.insert({(int)std::floor(p.x / 2) + a, (int)std::floor(p.z / 2) + b});
    }
    int big = 0, onTread = 0;
    for (const Rock& k : w.rocks) {
        const double far = std::max(std::fabs(k.c.x), std::fabs(k.c.z));
        if (far < 560) continue;
        big += k.r > 2.2;
        if (k.r > 1.0 && tread.count({(int)std::floor(k.c.x / 2), (int)std::floor(k.c.z / 2)})) onTread++;
    }
    std::printf("ROCKS %zu, %d big out in the new country\n", w.rocks.size(), big);
    ctx.metric("big boulders in the new country", big, "", 1200, 20000);
    // (On a tread only where a trail means them: COPPER PASS's ledges, choke and slab.)
    ctx.metric("big rocks on a trail's tread outside the pass", onTread, "", 0, 40);
    for (const char* id : {"BC.SLAB_CITY", "BC.BALANCED_ROCK", "BC.THE_TORS"}) {
        const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
        int n = 0;
        for (const Rock& k : w.rocks) n += std::hypot(k.c.x - p->p.x, k.c.z - p->p.z) < 35;
        ctx.check(std::string(id) + ": its rocks", n >= 5, std::to_string(n));
    }
    // The open country has something in it everywhere: in every 100 m square of the new country, a rock, a bush or a tree.
    std::set<std::pair<int, int>> filled;
    auto sq = [](double v) { return (int)std::floor((v + 2550) / 100); };
    for (const Rock& k : w.rocks) filled.insert({sq(k.c.x), sq(k.c.z)});
    for (const Bush& b : w.bushes) filled.insert({sq(b.p.x), sq(b.p.z)});
    for (const Tree& t : w.trees) filled.insert({sq(t.base.x), sq(t.base.z)});
    int squares = 0, empty = 0;
    for (double x = -2550; x < 2550; x += 100)
        for (double z = -2550; z < 2550; z += 100) {
            if (std::max(std::fabs(x + 50), std::fabs(z + 50)) < 620) continue;
            if (w.waterLevel(x + 50, z + 50) > w.height(x + 50, z + 50)) continue;  // (the lake)
            squares++;
            empty += filled.count({sq(x + 1), sq(z + 1)}) == 0;
        }
    ctx.metric("empty 100 m squares in the new country", 100.0 * empty / std::max(1, squares), "%", 0, 3);
}

SCENARIO(bctrailsPassNotch, "bctrails/pass-notch", "COPPER PASS, the Notch: a set-up Bronco crawls up the rock garden and the four ledges from Pass Hut") {
    double got, want, worst;
    passStretch(ctx, "passstart", "notchtop", "bronco", true, 5, 300, got, want, worst);
    ctx.metric("Bronco: up the Notch (m along it)", got, "m", want - 6, 1e4);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 10);
}
SCENARIO(bctrailsPassNotchStock, "bctrails/pass-notch-stock", "COPPER PASS, the Notch: a stock Civic stops in the rock garden or at the ledges") {
    double got, want, worst;
    passStretch(ctx, "passstart", "notchtop", "civic", false, 5, 150, got, want, worst);
    ctx.metric("Civic: how far up the Notch (m)", got, "m", 0, want - 15);
}
SCENARIO(bctrailsPassStock, "bctrails/pass-stock", "COPPER PASS, the Choke and the Shelf in a stock Tacoma (2WD, street pressures): how far it gets (a measurement: it should struggle)") {
    double got, want, worst;
    passStretch(ctx, "choke", "shelftop", "tacoma", false, 6, 300, got, want, worst);
    ctx.metric("Tacoma: through the Choke and up the Shelf (m)", got, "m", 0, 1e4);
}
SCENARIO(bctrailsPassClimb, "bctrails/pass-climb", "COPPER PASS: a set-up Bronco from the Notch's top across Copper Creek, up the scree and the seven switchbacks to the Choke") {
    double got, want, worst;
    passStretch(ctx, "notchtop", "choke", "bronco", true, 7, 900, got, want, worst);
    ctx.metric("Bronco: to the Choke (m along it)", got, "m", want - 12, 1e4);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 10);
}
SCENARIO(bctrailsPassShelf, "bctrails/pass-shelf", "COPPER PASS: a set-up Bronco through the Choke, up the Shelf (over the Slab) and on over the saddle to the Surveyor's Cabin") {
    double got, want, worst;
    passStretch(ctx, "choke", "passend", "bronco", true, 6, 900, got, want, worst);
    ctx.metric("Bronco: over the pass (m along it)", got, "m", want - 6, 1e4);
    ctx.metric("Bronco: damage", worst * 100, "%", 0, 10);
}
