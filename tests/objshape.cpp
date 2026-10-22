// Exact object shapes (objshape.h): rays and nearest points hit the real hull, a log's capsule, a
// trunk's cylinder and not the bounding sphere; a strap round a rock hugs its cross-section; the
// winch's boulder anchor is on the stone; the walker is stopped by the hull, not the sphere.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "../src/modelkit.h"
#include "../src/objshape.h"
#include "../src/props.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

using namespace objshape;

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

struct Spec {
    const char* name;
    V3 radii;
    double top;
    uint32_t seed;
};
// A flat slab, a tall pillar, a round boulder, a long lump, and a few more shapes by seed.
const Spec kSpecs[] = {
    {"slab", V3(1.8, 0.4, 1.3), 0.45, 11}, {"tall", V3(0.8, 1.5, 0.7), 1.7, 5},     {"round", V3(1.0, 0.9, 1.0), 0.8, 3},    {"long", V3(2.2, 0.7, 0.6), 0.9, 21},
    {"seed 1", V3(1.2, 0.8, 0.9), 0.7, 1}, {"seed 2", V3(1.0, 0.6, 1.4), 0.5, 2},   {"seed 7", V3(0.9, 1.1, 0.8), 1.0, 7},  {"seed 99", V3(1.5, 0.5, 1.1), 0.4, 99},
    {"seed 1234", V3(0.7, 0.7, 0.7), 0.6, 1234}, {"seed 4242", V3(1.3, 1.1, 1.3), 1.0, 4242},
};

// The rocks to test on: code-made and, where the library is there, scanned, each on the flat pad.
std::vector<Rock> makeRocks(World& w, bool scanned) {
    props::ScopedRockScans scans(scanned ? &props::sharedHullLibrary() : nullptr);
    std::vector<Rock> out;
    double x = -100;
    for (const Spec& s : kSpecs) {
        out.push_back(w.addRock(x, -118, s.radii, s.top, s.seed, 1));
        x += 18;
    }
    w.rocks.clear();
    return out;
}

// Reference: the nearest triangle hit of the rock's mesh (Moller-Trumbore), -1 if none.
double bruteRay(const Rock& k, const V3& o, const V3& d) {
    double best = -1;
    for (const auto& f : k.f) {
        const V3 &a = k.v[f[0]], &b = k.v[f[1]], &c = k.v[f[2]];
        const V3 e1 = b - a, e2 = c - a, p = cross(d, e2);
        const double det = dot(e1, p);
        if (std::fabs(det) < 1e-12) continue;
        const V3 s = o - a;
        const double u = dot(s, p) / det;
        if (u < -1e-9 || u > 1 + 1e-9) continue;
        const V3 q = cross(s, e1);
        const double v = dot(d, q) / det;
        if (v < -1e-9 || u + v > 1 + 1e-9) continue;
        const double t = dot(e2, q) / det;
        if (t >= 0 && (best < 0 || t < best)) best = t;
    }
    return best;
}

double worstPlane(const Rock& k, const V3& p) {  // > 0 outside the hull
    double m = -1e18;
    for (size_t f = 0; f < k.f.size(); f++) m = std::max(m, dot(k.fn[f], p) - k.fd[f]);
    return m;
}

// Every shape the loop tests run over: (label, rock).
struct Labeled {
    std::string name;
    Rock rock;
};
std::vector<Labeled> allRocks(World& w, bool& scannedToo) {
    std::vector<Labeled> out;
    int i = 0;
    for (const Rock& k : makeRocks(w, false)) out.push_back({std::string("made ") + kSpecs[i++].name, k});
    scannedToo = props::sharedHullLibrary().loaded();
    if (scannedToo) {
        i = 0;
        for (const Rock& k : makeRocks(w, true)) out.push_back({std::string("scan ") + kSpecs[i++].name, k});
    }
    return out;
}

}  // namespace

SCENARIO(objRays, "objshape/rays", "Rays hit the real shape: the rock's hull (checked against every triangle), a log's capsule, a trunk's cylinder and a box. A ray that passes inside a rock's bounding sphere but misses the hull reports no hit") {
    World& w = ctx.sim.world;
    bool scanned = false;
    const std::vector<Labeled> rocks = allRocks(w, scanned);
    std::mt19937 rng(7);
    auto rnd = [&]() { return (rng() & 0xFFFFFF) / double(0x1000000); };
    int rays = 0, mismatches = 0, wrongNormal = 0, offSurface = 0, missedSphereHits = 0, sphereOnlyMisses = 0;
    double worstT = 0;
    for (const Labeled& L : rocks) {
        const Rock& k = L.rock;
        for (int i = 0; i < 400; i++) {
            // From a random point on a shell round the rock toward a random point near its middle: hits, grazes and clean misses.
            const double a = rnd() * 2 * M_PI, e = std::asin(rnd() * 2 - 1);
            const V3 o = k.c + V3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)) * (k.r * 2.5);
            const V3 aim = k.c + V3((rnd() - 0.5) * k.r * 1.6, (rnd() - 0.5) * k.r * 1.6, (rnd() - 0.5) * k.r * 1.6);
            const V3 d = norm(aim - o);
            Hit h;
            const bool hit = rayRock(k, o, d, 1e9, h);
            const double ref = bruteRay(k, o, d);
            rays++;
            if (hit != (ref >= 0) || (hit && std::fabs(h.t - ref) > 1e-6)) {
                mismatches++;
                continue;
            }
            if (!hit) {
                // A miss that went through the sphere is the case the bounding sphere got wrong.
                const V3 oc = o - k.c;
                const double b = dot(oc, d), disc = b * b - (dot(oc, oc) - k.r * k.r);
                if (disc > 0 && b < 0) sphereOnlyMisses++;
                continue;
            }
            worstT = std::max(worstT, std::fabs(h.t - ref));
            if (dot(h.n, d) >= 0) wrongNormal++;
            if (std::fabs(worstPlane(k, h.p)) > 1e-6) offSurface++;
            if (len(h.p - k.c) > k.r + 1e-6) missedSphereHits++;
        }
    }
    ctx.metric("rays cast", rays, "", 1000, 1e9);
    ctx.check("every ray agrees with the triangle mesh", mismatches == 0, std::to_string(mismatches) + " of " + std::to_string(rays));
    ctx.check("hit normals face the ray", wrongNormal == 0, std::to_string(wrongNormal));
    ctx.check("hit points lie on the hull", offSurface == 0, std::to_string(offSurface));
    ctx.check("hits are inside the bounding sphere (sanity)", missedSphereHits == 0);
    ctx.check("many rays pass through the sphere and miss the hull", sphereOnlyMisses > 100, std::to_string(sphereOnlyMisses));
    ctx.note(std::string("scanned rocks ") + (scanned ? "included" : "not available (no assets/props): code-made only"));

    // The explicit case: a horizontal ray just over a flat slab's top, through the middle of its bounding sphere.
    int explicitCases = 0;
    for (const Labeled& L : rocks) {
        const Rock& k = L.rock;
        const double top = rockTop(k);
        const double y = top + 0.1;
        if (y > k.c.y + k.r * 0.85 || y < k.c.y - k.r) continue;  // (needs the sphere to reach well over the top)
        const V3 o(k.c.x - k.r * 3, y, k.c.z);
        Hit h;
        const double inSphere = std::hypot(0.0, y - k.c.y);
        ctx.check(L.name + ": ray over the top passes inside the bounding sphere", inSphere < k.r - 0.01);
        ctx.check(L.name + ": ...and reports no hit", !rayRock(k, o, V3(1, 0, 0), 1e9, h));
        const V3 o2(k.c.x - k.r * 3, top - 0.1, k.c.z);
        explicitCases++;
    }
    ctx.check("some shapes were flat enough for the sphere-miss case", explicitCases >= 3, std::to_string(explicitCases));

    // A rock on the map: rayWorld finds it, with the log, the trunk and the box.
    Rock& r0 = w.addRock(-60, -150, V3(1.0, 0.5, 0.9), 0.5, 3, 1);
    w.addLog(V3(-40, 0.4, -150), V3(-34, 0.4, -150), 0.4);
    w.addTree(-20, -150, 8, 0);
    w.addBox(V3(0, 1, -150), V3(1, 1, 1), 0, 0, 0, V3(0.5, 0.5, 0.5));
    w.buildGrid();
    const int rockIdx = (int)w.rocks.size() - 1;
    const int logIdx = (int)w.logs.size() - 1, poleIdx = (int)w.poles.size() - 1, boxIdx = (int)w.boxes.size() - 1;
    WorldHit wh;
    const V3 eye(-80, r0.c.y, -150);  // (at the rock's centre height: the pad's level ground sets where it sits)
    ctx.check("rayWorld finds the rock first", rayWorld(w, eye, V3(1, 0, 0), 200, wh) && wh.kind == SH_ROCK && wh.index == rockIdx);
    ctx.check("the rock hit is on its hull", wh.kind == SH_ROCK && std::fabs(worstPlane(r0, wh.hit.p)) < 1e-6 && wh.hit.n.x < 0);
    ctx.check("past the rock: the log", rayWorld(w, V3(-50, 0.4, -150), V3(1, 0, 0), 200, wh) && wh.kind == SH_LOG && wh.index == logIdx &&
                                          std::fabs(wh.hit.p.x - (-40 - 0.4)) < 1e-6 && std::fabs(wh.hit.n.x + 1) < 1e-6);
    ctx.check("a log's end is rounded", rayWorld(w, V3(-30, 0.4, -150), V3(-1, 0, 0), 200, wh) && wh.kind == SH_LOG && std::fabs(wh.hit.p.x - (-34 + 0.4)) < 1e-6);
    const Pole& tp = w.poles[poleIdx];
    ctx.check("then the trunk, at its radius", rayWorld(w, V3(-30, tp.y0 + 1, -150), V3(1, 0, 0), 200, wh) && wh.kind == SH_POLE && wh.index == poleIdx &&
                                                 std::fabs(wh.hit.p.x - (-20 - tp.r)) < 1e-6, std::to_string(wh.hit.p.x));
    ctx.check("a ray over the trunk's top misses it", !rayWorld(w, V3(-30, tp.y1 + 0.5, -150), V3(1, 0, 0), 12, wh));
    ctx.check("and the box face", rayWorld(w, V3(-10, 1, -150), V3(1, 0, 0), 200, wh) && wh.kind == SH_BOX && wh.index == boxIdx && std::fabs(wh.hit.p.x + 1) < 1e-6);
    ctx.check("a ray from inside reports t = 0", rayWorld(w, V3(0, 1, -150), V3(1, 0, 0), 5, wh) && wh.hit.t == 0);
    ctx.check("down onto the rock's top", rayWorld(w, V3(r0.c.x, 20, r0.c.z), V3(0, -1, 0), 40, wh) && wh.kind == SH_ROCK && std::fabs(wh.hit.p.y - rockTop(r0)) < 0.3 && wh.hit.n.y > 0);
}

SCENARIO(objNearest, "objshape/nearest", "The nearest point on a rock, log, trunk or box is on its real surface, and no corner of the hull is nearer") {
    World& w = ctx.sim.world;
    bool scanned = false;
    const std::vector<Labeled> rocks = allRocks(w, scanned);
    std::mt19937 rng(11);
    auto rnd = [&]() { return (rng() & 0xFFFFFF) / double(0x1000000); };
    int bad = 0, n = 0, nearer = 0, insideBad = 0;
    double sphereGap = 0;
    for (const Labeled& L : rocks) {
        const Rock& k = L.rock;
        for (int i = 0; i < 200; i++) {
            const V3 q = k.c + V3(rnd() - 0.5, rnd() - 0.5, rnd() - 0.5) * (k.r * 3.0);
            const Near nr = closestOnRock(k, q);
            n++;
            const bool inside = worstPlane(k, q) <= 0;
            if (std::fabs(worstPlane(k, nr.p)) > 1e-6 || std::fabs(nr.dist - (inside ? 0.0 : len(q - nr.p))) > 1e-6) bad++;
            if (inside) {
                insideBad += nr.dist != 0;
                continue;
            }
            for (const V3& v : k.v)
                if (len(q - v) < nr.dist - 1e-6) nearer++;
            // The distance to the sphere's surface is what the old reach checks used: it can be metres off.
            sphereGap = std::max(sphereGap, std::fabs((len(q - k.c) - k.r) - nr.dist));
        }
    }
    ctx.check("nearest points are on the hull and the distance is theirs", bad == 0, std::to_string(bad) + " of " + std::to_string(n));
    ctx.check("no corner is nearer than the nearest point", nearer == 0, std::to_string(nearer));
    ctx.check("inside is distance 0", insideBad == 0);
    ctx.metric("the most the bounding sphere's surface was off the real one", sphereGap, "m", 0.3, 100);

    // Log, trunk, box.
    const Log g{V3(0, 1, 0), V3(4, 1, 0), 0.3};
    Near nl = closestOnLog(g, V3(2, 2, 0));
    ctx.check("log: straight over the middle", std::fabs(nl.dist - 0.7) < 1e-9 && std::fabs(nl.p.y - 1.3) < 1e-9 && nl.n.y > 0.99);
    nl = closestOnLog(g, V3(5, 1, 0));
    ctx.check("log: past the end, round it", std::fabs(nl.dist - 0.7) < 1e-9 && std::fabs(nl.p.x - 4.3) < 1e-9);
    const Pole p{10, 10, 0, 6, 0.25};
    Near np = closestOnPole(p, V3(11, 2, 10));
    ctx.check("trunk: beside it, at its radius", std::fabs(np.dist - 0.75) < 1e-9 && std::fabs(np.p.x - 10.25) < 1e-9 && np.n.x > 0.99);
    np = closestOnPole(p, V3(10, 7, 10));
    ctx.check("trunk: over the top", std::fabs(np.dist - 1.0) < 1e-9 && np.n.y > 0.99);
    w.addBox(V3(0, 1, 0), V3(1, 1, 1), 0, 0, 0, V3(0.5, 0.5, 0.5));
    const Near nb = closestOnBox(w.boxes.back(), V3(3, 1.5, 0));
    ctx.check("box: off a face", std::fabs(nb.dist - 2.0) < 1e-9 && std::fabs(nb.p.x - 1.0) < 1e-9);
    // nearestSolid finds the nearest of everything.
    World& m = ctx.sim.world;
    m.addRock(30, -140, V3(1, 1, 1), 0.8, 8, 1);
    m.addTree(36, -140, 8, 0);
    m.buildGrid();
    Kind kind;
    int idx;
    Near out;
    ctx.check("nearestSolid: the rock side", nearestSolid(m, V3(32.5, 0.3, -140), 5, kind, idx, out) && kind == SH_ROCK);
    ctx.check("nearestSolid: the trunk side", nearestSolid(m, V3(34.5, 0.3, -140), 5, kind, idx, out) && kind == SH_POLE);
    ctx.check("nearestSolid: nothing within reach", !nearestSolid(m, V3(33.5, 0.3, -100), 3, kind, idx, out));
}

SCENARIO(objWrap, "objshape/wrap", "A strap round a rock lies within a few cm of the stone all round, on every shape (code-made and scanned): never inside it, never floating, the attach point the nearest to the winch, clear of the ground") {
    World& w = ctx.sim.world;
    bool scanned = false;
    const std::vector<Labeled> rocks = allRocks(w, scanned);
    double worstFloat = 0, worstDepth = 1e9, worstGround = 1e9, minClear = 1e9;
    int badAttach = 0, loops = 0, noLoop = 0;
    for (const Labeled& L : rocks) {
        const Rock& k = L.rock;
        double y;
        const bool holds = rockStrapHeight(w, k, y);
        if (!holds) {
            noLoop++;
            continue;
        }
        double lowestPoint = 1e9, rockMax = 0, rockMin = 1e9;
        for (int dir = 0; dir < 8; dir++) {
            const double a = dir * M_PI / 4 + 0.3;
            const V3 from = k.c + V3(std::cos(a), 0, std::sin(a)) * (k.r * 2);
            const Wrap wr = wrapRock(k, y, from);
            if (!wr.ok) {
                ctx.check(L.name + ": a wrap exists at the strap height", false);
                continue;
            }
            loops++;
            // Walk the loop in 5 cm steps: its distance from the stone.
            const int n = (int)wr.loop.size();
            double nearestToFrom = 1e18, minDist = 1e9, maxDist = 0;
            for (int i = 0; i < n; i++) {
                const V3 &p = wr.loop[i], &q = wr.loop[(i + 1) % n];
                const int steps = std::max(1, (int)std::ceil(len(q - p) / 0.05));
                for (int s = 0; s < steps; s++) {
                    const V3 c = p + (q - p) * ((double)s / steps);
                    const double d = closestOnRock(k, c).dist;
                    maxDist = std::max(maxDist, d), minDist = std::min(minDist, d);
                    nearestToFrom = std::min(nearestToFrom, std::hypot(c.x - from.x, c.z - from.z));
                    lowestPoint = std::min(lowestPoint, c.y - w.height(c.x, c.z));
                    if (worstPlane(k, c) < -1e-6) worstDepth = std::min(worstDepth, -worstPlane(k, c));
                }
            }
            worstFloat = std::max(worstFloat, maxDist);
            if (std::hypot(wr.attach.x - from.x, wr.attach.z - from.z) > nearestToFrom + 1e-6) badAttach++;
            rockMax = std::max(rockMax, maxDist), rockMin = std::min(rockMin, minDist);
        }
        ctx.check(L.name + ": the strap hugs the stone from every side (" + std::to_string((int)(rockMax * 1000)) + " mm at most, " + std::to_string((int)(rockMin * 1000)) + " mm at least)",
                  rockMax <= kStandoff + 1e-3 && rockMin > 0);
        minClear = std::min(minClear, lowestPoint);
        worstGround = std::min(worstGround, lowestPoint);
    }
    ctx.metric("the strap's furthest point from the stone, any shape", worstFloat * 1000, "mm", 0, 13);
    ctx.check("the strap is never inside the stone", worstDepth > 1e8, std::to_string(worstDepth));
    ctx.check("the attach point is the loop's nearest to the winch", badAttach == 0, std::to_string(badAttach));
    ctx.metric("the strap's lowest point over the ground", minClear * 100, "cm", 7, 400);
    ctx.metric("wraps checked", loops, "", 100, 1e9);
    ctx.check("rocks that show enough to strap: nearly all", noLoop <= 2, std::to_string(noLoop) + " didn't");

    // A trunk: a circle of its radius.
    World& m = ctx.sim.world;
    m.addTree(40, -150, 9, 0);
    const Pole& tr = m.poles.back();
    const Wrap tw = wrapTrunk(tr, 0.5, V3(30, 0.5, -150), 0.0);
    double dev = 0;
    for (const V3& p : tw.loop) dev = std::max(dev, std::fabs(std::hypot(p.x - tr.x, p.z - tr.z) - tr.r));
    ctx.check("trunk strap is the trunk's radius all round", tw.ok && dev < 1e-9);
    ctx.check("and its attach point faces the winch", std::fabs(tw.attach.x - (tr.x - tr.r)) < 1e-6 && std::fabs(tw.attach.z - tr.z) < 1e-6 && tw.out.x < -0.99);

    // The slings as models, built along that outline: both variants (chain, web) stay on the path.
    {
        const Rock& rk = rocks[0].rock;  // (a slab)
        double sy;
        rockStrapHeight(w, rk, sy);
        const Wrap sw2 = wrapRock(rk, sy, V3(rk.c.x + 9, 0, rk.c.z + 4));
        const std::vector<V3> path = localPath(sw2, 0.09);
        const mk::ModelDef* def = mk::findModel("chain_sling");
        ctx.check("the sling model is there with two variants", def && def->variants.size() == 2);
        if (def && sw2.ok) {
            double px0 = 1e9, px1 = -1e9, pz0 = 1e9, pz1 = -1e9;
            for (const V3& q : path) px0 = std::min(px0, q.x), px1 = std::max(px1, q.x), pz0 = std::min(pz0, q.z), pz1 = std::max(pz1, q.z);
            for (size_t vi = 0; vi < def->variants.size(); vi++) {
                mk::pathHint() = path;
                mk::Mesh m;
                mk::build(*def, (int)vi, mk::defaults(def->variants[vi]), m);
                mk::pathHint().clear();
                m.finish();
                // The model spans the outline (to the shackle's reach on the +x side) and not a metre more.
                ctx.check(def->variants[vi].name + ": built round the outline (" + std::to_string(m.triangles()) + " triangles)", m.triangles() > 500 && std::isfinite(m.lo.x + m.hi.z) &&
                              m.lo.x > px0 - 0.1 && m.hi.x < px1 + 0.45 && m.lo.z > pz0 - 0.1 && m.hi.z < pz1 + 0.1 && m.lo.z < pz0 + 0.1 && m.hi.z > pz1 - 0.1,
                          std::to_string(m.lo.x) + ".." + std::to_string(m.hi.x) + " vs " + std::to_string(px0) + ".." + std::to_string(px1));
                // ...and its lowest and highest are a strap's height (chain wire / webbing) either side of the loop.
                ctx.check(def->variants[vi].name + ": about the loop's own height", m.lo.y > -0.08 && m.hi.y < 0.12, std::to_string(m.lo.y) + ".." + std::to_string(m.hi.y));
            }
        }
    }
    // The local path the models follow: starts on one side of the attach point, ends on the other, all round at x <= the standoff.
    const Wrap sw = wrapRock(rocks[2].rock, 0.3, V3(0, 0, 0));
    if (sw.ok) {
        const std::vector<V3> path = localPath(sw, 0.09);
        ctx.check("model path: a tail either side of the attach point", path.size() > 4 && path.front().z > 0 && path.back().z < 0);
        double maxX = -1e9;
        for (const V3& p : path) maxX = std::max(maxX, p.x);
        ctx.check("model path: out of the stone is +x (the attach point is its furthest)", maxX < 0.02, std::to_string(maxX));
    }
}

SCENARIO(objAnchors, "objshape/anchors", "The winch's boulder anchor is tied on the stone: on the strap round its real cross-section, nowhere near the bounding sphere's surface; the pull and distance use that point") {
    ctx.sim = Sim();
    World& w = ctx.sim.world;
    // A slab, a pillar and a round one in a row ahead of the winch.
    const double zs[3] = {-10, -2, 8};
    const Spec* use[3] = {&kSpecs[0], &kSpecs[1], &kSpecs[2]};
    for (int i = 0; i < 3; i++) w.addRock(100, zs[i], use[i]->radii, use[i]->top, use[i]->seed, 1);
    w.buildGrid();
    Car& c = ctx.add(bronco(), 75, -4, 0);
    ctx.focus(c);
    ctx.run(1.0);
    Sim& sim = ctx.sim;
    const int ci = (int)(&c - sim.cars.data());
    const V3 hook = c.hookPoint(0);
    int found = 0;
    double worstFromSurface = 0, worstSphereGap = 0, minOverGround = 1e9;
    for (const WinchTarget& t : sim.winchTargets(ci)) {
        if (t.end.kind != ANCHOR_ROCK) continue;
        const int ri = rockAt(w, t.at, 0.5);
        if (ri < 0) continue;
        const Rock& k = w.rocks[ri];
        found++;
        const Near nr = closestOnRock(k, t.at);
        worstFromSurface = std::max(worstFromSurface, nr.dist);
        worstSphereGap = std::max(worstSphereGap, std::fabs(len(t.at - k.c) - k.r));
        minOverGround = std::min(minOverGround, t.at.y - w.height(t.at.x, t.at.z));
        ctx.check("the target point is the end's point, on the strap", len(t.at - t.end.p) < 1e-9);
        ctx.check("the distance is to that point", std::fabs(t.dist - len(t.at - hook)) < 1e-6);
        // It faces the winch: the nearest of its strap loop.
        const Wrap wr = wrapRock(k, t.at.y, hook);
        ctx.check("it is the point of the strap nearest the winch", wr.ok && std::hypot(wr.attach.x - t.at.x, wr.attach.z - t.at.z) < 0.02);
    }
    ctx.check("all three boulders are targets", found == 3, std::to_string(found));
    ctx.metric("anchor point off the stone (worst of three)", worstFromSurface * 1000, "mm", 0, 13);
    ctx.metric("anchor point over the ground (lowest of three)", minOverGround * 100, "cm", 7, 300);
    ctx.metric("the old anchor point sat this far from where it is now (a sphere-surface guess)", worstSphereGap, "m", 0, 100);
    // Hooking one: the rope's end is that point and the line pulls on it.
    sim.anchorStock.n[GEAR_SLING] = 2;
    const std::string msg = sim.hookWinch(ci);
    ctx.check("hooks a boulder", sim.ropeOn(ci, ROPE_WINCH) >= 0 && sim.ropes[0].b.kind == ANCHOR_ROCK, msg);
    if (!sim.ropes.empty()) {
        const V3 p = sim.ropes[0].b.p;
        const int ri = rockAt(w, p, 0.5);
        ctx.check("the rope ends on the stone", ri >= 0 && closestOnRock(w.rocks[ri], p).dist < kStandoff + 1e-3);
        const double x0 = c.frame().com.x;
        c.handbrake = 1;  // (braked: the line comes up tight and the winch has to drag it)
        double peak = 0;
        for (double t = 0; t < 5; t += Ctx::kFrame) sim.setWinch(ci, 1), ctx.step(), peak = std::max(peak, sim.ropes[0].tension);
        ctx.check("the winch drags the truck toward the boulder", c.frame().com.x > x0 + 0.05, std::to_string(c.frame().com.x - x0));
        ctx.check("with the line under tension, pulling at the point on the stone", peak > 2000, std::to_string(peak));
        ctx.check("the rope's end hasn't moved", len(sim.ropes[0].b.p - p) < 1e-9);
    }
}

SCENARIO(objWalker, "objshape/walker", "On foot, the hull stops you, not the bounding sphere: you can walk up to a standing rock's face from any side and stand right at it (well inside where its bounding sphere would have stopped you), close enough to put a sling on it") {
    World& w = ctx.sim.world;
    Rock& pillar = w.addRock(-60, -118, V3(0.8, 1.5, 0.7), 1.7, 5, 1);  // (too tall to step onto)
    w.buildGrid();
    Car& c = ctx.add(vehicleCatalog()[2], -120, -50, 0);
    ctx.focus(c);
    ctx.run(0.5);
    Walker wk;
    int inside = 0;
    for (int dir = 0; dir < 8; dir++) {
        const double a = dir * M_PI / 4 + 0.2;
        // Start 1 m outside the bounding sphere on that bearing and walk at the rock's middle (sliding round it when it's in the way).
        const V3 q = pillar.c + V3(std::cos(a), 0, std::sin(a)) * (pillar.r + 1.0);
        wk.p = V3(q.x, w.height(q.x, q.z), q.z), wk.v = V3();
        wk.active = true;
        const double yaw = std::atan2(-(pillar.c.z - q.z), pillar.c.x - q.x);
        double closest = 1e9, closestSphere = 1e9, inRock = 0;
        for (double t = 0; t < 6; t += Ctx::kFrame) {
            wk.step(w, ctx.sim, Ctx::kFrame, 1, 0, false, false, yaw);
            const double hullGap = closestOnRock(pillar, V3(wk.p.x, wk.p.y + 0.5, wk.p.z)).dist;
            closest = std::min(closest, hullGap);
            closestSphere = std::min(closestSphere, len(V3(wk.p.x, 0, wk.p.z) - V3(pillar.c.x, 0, pillar.c.z)) - pillar.r);
            inRock = std::max(inRock, -worstPlane(pillar, V3(wk.p.x, wk.p.y + 0.9, wk.p.z)));  // (how far into the hull the body got)
        }
        inside += closestSphere < -0.1;
        ctx.check("bearing " + std::to_string(dir) + ": walked right up to the stone (" + std::to_string((int)(closest * 100)) + " cm off it at the nearest)", closest < 0.6);
        ctx.check("bearing " + std::to_string(dir) + ": never into it (" + std::to_string((int)(inRock * 100)) + " cm)", inRock < 0.05);
    }
    ctx.check("most bearings get well inside the bounding sphere", inside >= 4, std::to_string(inside) + " of 8");
    ctx.note("the walker pushes out of World::contact, which tests the hull's own faces (the bounding sphere is only a quick reject)");
}
