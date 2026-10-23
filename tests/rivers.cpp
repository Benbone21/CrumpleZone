// Rivers and waterfalls (docs/plans/big-maps.md phase 5): Elk River out in the Backcountry's new country by depth class, what a car
// does in each (wades a ford, drowns in a pool unless it has a snorkel, is carried off by the current), and its falls.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/modelkit.h"
#include "../src/watermesh.h"
#include "../src/worldsound.h"
#include "harness.h"

namespace {
const VehicleSpec& rCivic() { return vehicleCatalog()[0]; }
const VehicleSpec& r4Runner() { return vehicleCatalog()[3]; }

double headingTo(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI; }

// The river spot nearest a named place (backcountryFeature), out in the new country.
const RiverSpot* spotNear(const World& w, const char* feature) {
    const V3 f = backcountryFeature(feature);
    const RiverSpot* best = nullptr;
    double bd = 1e18;
    for (const River& r : w.rivers)
        for (const RiverSpot& s : r.spots) {
            const double d = std::hypot(s.p.x - f.x, s.p.z - f.z);
            if (d < bd) bd = d, best = &s;
        }
    return best;
}

// Straight across the river at `s`: from `reach` m off one bank to as far off the other, square to the current.
void across(const RiverSpot& s, const World& w, double reach, V3& a, V3& b) {
    V3 f = w.waterFlow(s.p.x, s.p.z);
    f.y = 0;
    if (len(f) < 1e-6) f = V3(-1, 0, 0);
    f = norm(f);
    const V3 l(-f.z, 0, f.x);
    a = s.p - l * reach, b = s.p + l * reach;
    a.y = b.y = 0;
}

// Drives toward b at about `kmh`, holding the line from a; true once past b's side of the middle by `past` m.
bool driveAcross(Ctx& ctx, Car& c, const V3& a, const V3& b, double kmh, double maxS, double past, double* deepest, const World& w) {
    const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z)), mid = (a + b) * 0.5;
    return ctx.runUntil(maxS, [&] { return dot(c.frame().com - mid, dir) > past; }, [&] {
        const Frame fr = c.frame();
        V3 d = b - fr.com;
        d.y = 0;
        c.steer = clampd(std::atan2(dot(d, fr.left), dot(d, fr.fwd)) * 1.2, -0.4, 0.4);
        const double v = dot(fr.vel, fr.fwd);
        c.throttle = clampd(0.3 + (kmh / 3.6 - v) * 0.6, 0, 1), c.brake = 0, c.handbrake = 0;
        if (deepest) *deepest = std::max(*deepest, w.waterLevel(fr.com.x, fr.com.z) - w.height(fr.com.x, fr.com.z));
    });
}
}  // namespace

SCENARIO(riversClasses, "rivers/depth-classes", "Elk River in the new country is made by depth class: riffles, fords, runs, pools, chutes and plunge pools are as deep and as fast as their class, on the grid the cars feel") {
    const World w(MAP_BACKCOUNTRY);
    int count[RIVER_CLASSES] = {};
    int bad = 0, total = 0;
    std::string worst;
    for (const River& r : w.rivers) {
        const bool outer = r.name != "ELK RIVER";
        for (const RiverSpot& s : r.spots) {
            if (!outer) continue;
            count[s.cls]++;
            if (s.cls == RIVER_FALLS || s.cls == RIVER_RUN) continue;
            // (Only where it's wholly that class: at a stretch's middle the blend is done.)
            if (std::fabs(s.depth - (riverClassDepth(s.cls).lo + riverClassDepth(s.cls).hi) / 2) > 0.2 && s.cls != RIVER_PLUNGE) continue;
            total++;
            const double d = w.waterLevel(s.p.x, s.p.z) - w.height(s.p.x, s.p.z), sp = len(w.waterFlow(s.p.x, s.p.z));
            const DepthBand b = riverClassDepth(s.cls);
            const bool deepOk = d > b.lo - 0.05 && d < b.hi + 0.15;
            const bool fastOk = s.cls == RIVER_CHUTE ? sp > 1.8 : sp < 1.6 && sp > 0.2;
            if (!deepOk || !fastOk) {
                bad++;
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s at (%.0f, %.0f): %.2f m deep, %.2f m/s; ", riverClassName(s.cls), s.p.x, s.p.z, d, sp);
                if (worst.size() < 600) worst += buf;
            }
        }
    }
    std::printf("  spots: run %d, riffle %d, ford %d, pool %d, chute %d, plunge %d, falls %d (%d checked)\n", count[RIVER_RUN], count[RIVER_RIFFLE],
                count[RIVER_FORD], count[RIVER_POOL], count[RIVER_CHUTE], count[RIVER_PLUNGE], count[RIVER_FALLS], total);
    ctx.check("riffles, fords and pools along it (3+ each), and two falls' chutes and plunge pools",
              count[RIVER_RIFFLE] >= 3 && count[RIVER_FORD] >= 3 && count[RIVER_POOL] >= 3 && count[RIVER_CHUTE] >= 2 && count[RIVER_PLUNGE] >= 2 && w.falls.size() >= 2);
    ctx.check("each class as deep and as fast as it's meant to be, mid-channel", bad == 0 && total > 40, worst);
    ctx.check("the old channel through Elk Valley is kept too", !w.rivers.empty() && w.rivers[0].name == "ELK RIVER" && w.rivers[0].spots.size() > 100);
    // Fords: posts on the banks.
    const RiverSpot* f = spotNear(w, "eastford");
    int posts = 0;
    for (const Prop& p : w.fixtures)
        if (p.kind == PROP_MARKER && f && std::hypot(p.a.x - f->p.x, p.a.z - f->p.z) < 25) posts++;
    ctx.check("the east ford has marker posts on both banks", posts >= 4, std::to_string(posts));
    // A rocky bed: scanned rocks along it.
    int rocks = 0;
    for (const Rock& k : w.rocks)
        if (std::fabs(k.c.x) > 620 && w.waterLevel(k.c.x, k.c.z) > -1e8 && std::fabs(k.c.z) < 400) rocks++;
    ctx.metric("rocks in or at the edge of the river out in the new country", rocks, "", 150, 2000);
}

SCENARIO(riversFordWade, "rivers/ford-wade", "A 4Runner wades the east ford (0.5 m, gravel) at walking pace: across, engine running") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const RiverSpot* s = spotNear(w, "eastford");
    ctx.check("there's a ford", s && s->cls == RIVER_FORD);
    if (!s) return;
    V3 a, b;
    across(*s, w, s->halfWidth + 14, a, b);
    Car& c = ctx.add(r4Runner(), a.x, a.z, headingTo(a, b));
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode(2);
    double deepest = 0;
    const bool got = driveAcross(ctx, c, a, b, 6, 60, s->halfWidth + 6, &deepest, w);
    ctx.check("across", got);
    ctx.check("engine still running", !c.engineDead && !c.hydrolocked);
    ctx.metric("deepest water it drove through", deepest, "m", 0.38, 0.68);
}

SCENARIO(riversPoolDrown, "rivers/pool-drown", "Into the east pool (1.2 m): a Civic's engine drinks the river and hydrolocks; the snorkelled 4Runner's doesn't") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const RiverSpot* s = spotNear(w, "eastpool");
    ctx.check("there's a pool", s && s->cls == RIVER_POOL);
    if (!s) return;
    const double depth = w.waterLevel(s->p.x, s->p.z) - w.height(s->p.x, s->p.z);
    ctx.metric("the pool's depth mid-channel", depth, "m", 0.9, 1.5);
    V3 a, b;
    across(*s, w, s->halfWidth + 14, a, b);
    {
        Car& c = ctx.add(rCivic(), a.x, a.z, headingTo(a, b));
        ctx.focus(c);
        ctx.run(1.0);
        driveAcross(ctx, c, a, b, 6, 40, 0, nullptr, w);
        ctx.run(3.0, [&] { c.throttle = 0.4; });
        ctx.check("Civic: hydrolocked", c.engineDead && c.hydrolocked);
    }
    ctx.sim.cars.clear();
    {
        Car& c = ctx.add(r4Runner(), a.x, a.z, headingTo(a, b));
        ctx.focus(c);
        ctx.run(1.0);
        c.setDriveMode(2);
        double deepest = 0;
        driveAcross(ctx, c, a, b, 5, 60, s->halfWidth + 6, &deepest, w);
        ctx.metric("4Runner: deepest water it was in", deepest, "m", 0.9, 1.6);
        ctx.check("4Runner (snorkel): engine still running", !c.engineDead && !c.hydrolocked);
    }
}

SCENARIO(riversCurrent, "rivers/current-push", "A Civic in neutral dropped into Bear Falls' plunge pool, facing downstream, is held up by the water and rolled off downstream by the current (as in the Frontier's deep water)") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    ctx.check("the Backcountry has falls", !w.falls.empty());
    if (w.falls.empty()) return;
    const Falls& f = w.falls[0];
    const V3 down(std::cos(f.yaw), 0, -std::sin(f.yaw));
    V3 p = f.base() + down * 12;
    {  // (on the pool's middle line: its bed is a bowl)
        double bd = 1e18;
        V3 best = p;
        for (const River& r : w.rivers)
            for (const RiverSpot& s : r.spots)
                if (s.cls == RIVER_PLUNGE && std::hypot(s.p.x - p.x, s.p.z - p.z) < bd) bd = std::hypot(s.p.x - p.x, s.p.z - p.z), best = s.p;
        p = V3(best.x, 0, best.z);
    }
    const V3 flow = w.waterFlow(p.x, p.z);
    ctx.metric("the pool's depth", w.waterLevel(p.x, p.z) - w.height(p.x, p.z), "m", 2.0, 3.2);
    const V3 fl0 = norm(V3(flow.x, 0, flow.z));
    Car& c = ctx.add(rCivic(), p.x, p.z, headingTo(p, p + fl0));  // (facing downstream, in neutral below: it rolls where it's pushed)
    ctx.focus(c);
    const double y0 = c.frame().com.y, lift = w.waterLevel(p.x, p.z) + 0.3 - y0;
    for (Node& n : c.nodes) n.p.y += lift;  // (dropped in at the surface)
    const V3 c0 = c.frame().com;
    int fr = 0;
    ctx.run(15, [&] {
        c.throttle = 0, c.brake = 0, c.handbrake = 0, c.gear = 0;  // (in neutral: the wheels roll)
        if (std::getenv("CZ_DEBUG") && fr++ % 60 == 0) {
            const Frame q = c.frame();
            std::printf("t %.1f com (%.2f %.2f %.2f) ground %.2f water %.2f flow (%.2f %.2f) v (%.2f %.2f %.2f)\n", ctx.t, q.com.x, q.com.y, q.com.z, w.height(q.com.x, q.com.z),
                        w.waterLevel(q.com.x, q.com.z), w.waterFlow(q.com.x, q.com.z).x, w.waterFlow(q.com.x, q.com.z).z, q.vel.x, q.vel.y, q.vel.z);
        }
    });
    const V3 moved = c.frame().com - c0;
    const V3 fd = norm(V3(flow.x, 0, flow.z));
    const double drift = dot(moved, fd), aside = std::fabs(dot(moved, V3(-fd.z, 0, fd.x)));
    ctx.metric("the current there", len(flow), "m/s", 0.5, 1.5);
    ctx.metric("drift downstream in 15 s", drift, "m", 2.5, 40);
    ctx.metric("drift speed, as a share of the current's", drift / 15 / std::max(0.1, len(flow)), "", 0.5, 1.2);
    ctx.check("carried with the current, more than across it", drift > aside, std::to_string(aside) + " m aside");
}

SCENARIO(riversFalls, "rivers/falls", "The Backcountry's falls: at least two where Elk River drops, each a lip of rock over a deep plunge pool, its model built to size in budget and drawn with the water, its roar heard at 250 m and not at 400") {
    const World w(MAP_BACKCOUNTRY);
    ctx.check("two falls or more", w.falls.size() >= 2, std::to_string(w.falls.size()));
    const mk::ModelDef* def = mk::findModel("waterfall");
    ctx.check("the waterfall model is in the catalog with its three designs", def && def->variants.size() == 3);
    if (!def) return;
    for (int v = 0; v < (int)def->variants.size(); v++) {
        mk::Mesh m;
        mk::build(*def, v, mk::defaults(def->variants[v]), m);
        m.finish();
        const size_t t = m.triangles();
        ctx.check(def->variants[v].name + ": " + std::to_string(t) + " triangles (under 1500)", t > 100 && t < 1500);
        bool water = true;
        for (const mk::MVert& q : m.v) water &= mk::waterMat(q.mat);
        ctx.check(def->variants[v].name + ": all of it water materials", water);
    }
    std::vector<Vtx> wv;
    buildWaterMesh(w, wv);
    size_t fallsVerts = 0;
    for (const Vtx& x : wv) fallsVerts += x.c[3] > 19.5f;
    ctx.check("the falls are drawn with the water", fallsVerts > 600 * w.falls.size(), std::to_string(fallsVerts / 3) + " triangles");
    WorldSounds ws;
    ws.prepare(w);
    for (const Falls& f : w.falls) {
        const V3 base = f.base(), down(std::cos(f.yaw), 0, -std::sin(f.yaw));
        const double above = w.height(f.lip.x - down.x * 1.5, f.lip.z - down.z * 1.5);  // (the bed just above the lip)
        ctx.metric(f.name + ": drop", f.height, "m", f.variant == 2 ? 4 : 12, 30);
        ctx.check(f.name + ": the water runs to the lip", w.waterLevel(f.lip.x - down.x * 1.5, f.lip.z - down.z * 1.5) > above && std::fabs(w.waterLevel(f.lip.x - down.x * 1.5, f.lip.z - down.z * 1.5) - f.lip.y) < 0.3);
        std::string prof;
        for (double o = -3; o <= 4; o += 0.5) {
            char buf[64];
            const V3 q = f.lip + down * o;
            std::snprintf(buf, sizeof buf, "%.1f:%.1f/%.1f ", o, w.waterLevel(q.x, q.z), w.height(q.x, q.z));
            prof += buf;
        }
        ctx.check(f.name + ": no water on the grid over the drop itself", w.waterLevel(f.lip.x + down.x * 1.2, f.lip.z + down.z * 1.2) < -1e8, prof);
        const V3 pool = base + down * 6;
        ctx.metric(f.name + ": the plunge pool's depth", w.waterLevel(pool.x, pool.z) - w.height(pool.x, pool.z), "m", 1.9, 3.3);
        ctx.check(f.name + ": its pool's surface is where the sheet lands", std::fabs(w.waterLevel(pool.x, pool.z) - base.y) < 0.3);
        WorldSounds::Voice v[WorldSounds::kVoices - 1];
        const int slot = WorldSounds::kShopVoices + WorldSounds::kWaterVoices;
        auto at = [&](double d) {
            ws.update(base + down * d, V3(0, 0, 1), v);
            return v[slot].loop == WL_FALLS ? v[slot].gain : 0.0f;
        };
        if (f.name == "BEAR FALLS") {
            ctx.check(f.name + ": roars at 250 m, silent at 400", at(250) > 0.02f && at(400) == 0.0f && at(30) > at(250));
        }
    }
}
