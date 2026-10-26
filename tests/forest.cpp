// The forest (forest.h): trees drawn as instances of a few models, trunks that stop a car where they're
// drawn, and fallen logs that stay inside the capsule they collide as.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "../src/forest.h"
#include "harness.h"

SCENARIO(forestInstances, "forest/instances", "Every tree is one instance of a model built once: the meshes are a few percent of baking the forest, and far copies are much lighter") {
    World w(MAP_BACKCOUNTRY);
    forest::Set set;
    forest::build(w, false, false, set);
    ctx.check("one instance per tree", set.inst.size() == w.trees.size());
    ctx.metric("models built", (double)set.shapes.size(), "", 3, 120);
    ctx.metric("model vertices over a baked forest's", 100.0 * set.vertices() / std::max<size_t>(1, set.bakedVertices()), "%", 0, 5);
    double full = 0, far = 0;
    for (const forest::Inst& i : set.inst) full += set.shapes[i.shape].lod[0].v.size(), far += set.shapes[i.shape].lod[forest::kLods - 1].v.size();
    ctx.metric("the whole forest in far copies over in full models", far / std::max(1.0, full), "", 0, 0.45);
    bool kinds[3] = {};
    for (const forest::Inst& i : set.inst) kinds[set.shapes[i.shape].look.kind] = true;
    ctx.check("pines, broadleaf and snags all drawn", kinds[0] && kinds[1] && kinds[2]);
}

SCENARIO(forestTrunks, "forest/trunks", "Each kind and design of tree: its trunk's Pole is as thick as the drawn trunk 1 m up") {
    World w(MAP_BACKCOUNTRY);
    forest::Set set;
    forest::build(w, false, false, set);
    std::map<std::pair<int, int>, int> seen;
    for (const forest::Inst& in : set.inst) {
        const forest::Shape& s = set.shapes[in.shape];
        const auto key = std::make_pair(s.look.kind, s.look.design);
        if (seen[key]++ >= 2) continue;  // (a couple of each)
        const Tree& t = w.trees[in.tree];
        const Pole* pole = nullptr;
        for (const Pole& p : w.poles)
            if (std::hypot(p.x - t.base.x, p.z - t.base.z) < 0.01) pole = &p;
        if (!pole) {
            ctx.check("a pole under every tree", false);
            return;
        }
        // The drawn wood cut 1 m above the ground (1.2 m above the base), in the world.
        const double y = 1.2 / in.scale;
        std::vector<double> d;
        const auto& v = s.lod[0].v;
        for (size_t i = 0; i + 2 < v.size(); i += 3) {
            if (v[i].mat != mk::WOOD) continue;
            for (int e = 0; e < 3; e++) {
                const float* a = v[i + e].p;
                const float* b = v[i + (e + 1) % 3].p;
                if ((a[1] - y) * (b[1] - y) > 0 || a[1] == b[1]) continue;
                const double f = (y - a[1]) / (b[1] - a[1]);
                d.push_back(std::hypot(a[0] + (b[0] - a[0]) * f, a[2] + (b[2] - a[2]) * f) * in.scale);
            }
        }
        if (d.empty()) continue;
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        const double drawn = d[d.size() / 2];
        char name[96];
        std::snprintf(name, sizeof name, "kind %d design %d, %.0f m tall: pole over drawn trunk", s.look.kind, s.look.design, t.height);
        ctx.metric(name, pole->r / drawn, "", 0.85, 1.15);
    }
    ctx.metric("kinds and designs checked", (double)seen.size(), "", 6, 9);
}

SCENARIO(forestLogs, "forest/logs", "A fallen log's bark stays inside its capsule; it has ends and a stub or two, and moss in the woods") {
    World w(MAP_BACKCOUNTRY);
    if (!ctx.check("the backcountry has logs", !w.logs.empty())) return;
    int inside = 0, total = 0, ends = 0, mossy = 0;
    for (size_t k = 0; k < w.logs.size() && k < 40; k++) {
        const Log& g = w.logs[k];
        mk::Mesh m;
        forest::logMesh(w, g, (uint32_t)k + 1, m);
        const V3 ax = norm(g.b - g.a);
        const double L = len(g.b - g.a);
        for (const mk::MVert& q : m.v) {
            const V3 p(q.p[0], q.p[1], q.p[2]);
            const double t = std::clamp(dot(p - g.a, ax), 0.0, L);
            total++, inside += len(p - (g.a + ax * t)) <= g.r * 1.001;
            ends += q.mat == mk::MATTE;
            mossy += q.mat == mk::WOOD && q.c[1] > q.c[0] * 1.15;
        }
    }
    ctx.metric("vertices inside the capsule (the rest: stubs and splinters)", 100.0 * inside / std::max(1, total), "%", 80, 100);
    ctx.check("sawn or snapped ends drawn", ends > 0);
    ctx.check("some moss on the logs", mossy > 0);
}

namespace {
// A camera at (x, z), h m over the ground, looking along yaw (degrees: 0 +X, 90 -Z) and pitch, as the game's.
forest::View viewAt(const World& w, double x, double z, double h, double yawDeg, double pitchDeg, V3& fwd) {
    const double yaw = yawDeg * M_PI / 180, pitch = pitchDeg * M_PI / 180;
    const V3 eye(x, w.height(x, z) + h, z);
    fwd = V3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), -std::sin(yaw) * std::cos(pitch));
    forest::View v;
    v.vp = perspective((float)(60 * M_PI / 180), 16.0f / 9.0f, 0.05f, 6000.0f) * lookAt(eye, eye + fwd, V3(0, 1, 0));
    v.eye = eye;
    return v;
}
struct Count {
    long long tris = 0;
    int draws = 0, inst = 0;
};
Count count(const forest::Set& s, const std::vector<std::vector<forest::Pick>>& b) {
    Count c;
    for (size_t k = 0; k < b.size(); k++)
        if (!b[k].empty()) c.draws++, c.inst += (int)b[k].size(), c.tris += (long long)b[k].size() * s.shapes[k / forest::kLods].lod[k % forest::kLods].triangles();
    return c;
}
}  // namespace

SCENARIO(forestCulling, "forest/culling", "A view draws every tree and bush it can see (whole in view, in reach) and none behind it; bushes fade out at their reach") {
    for (int map : {MAP_FRONTIER, MAP_BACKCOUNTRY}) {
        World w(map);
        forest::Set sets[2];
        forest::build(w, false, false, sets[0]);
        forest::buildBushes(w, sets[1]);
        for (int k = 0; k < 2; k++) {
            const forest::Set& s = sets[k];
            forest::Picker p;
            p.build(s);
            std::vector<std::vector<forest::Pick>> b;
            int dropped = 0, behind = 0, inView = 0, badFade = 0;
            for (double yaw : {0.0, 90.0, 200.0, 300.0}) {
                V3 fwd;
                const forest::View v = viewAt(w, map == MAP_FRONTIER ? 290 : -80, map == MAP_FRONTIER ? -480 : -250, 2, yaw, -2, fwd);
                p.pick(s, v, b);
                std::vector<float> got(s.inst.size(), -1);
                for (const auto& bk : b)
                    for (const forest::Pick& q : bk) got[q.inst] = q.fade;
                for (size_t i = 0; i < s.inst.size(); i++) {
                    V3 c;
                    double r;
                    forest::Picker::sphere(s, s.inst[i], c, r);
                    const double d = std::hypot(s.inst[i].x - v.eye.x, s.inst[i].z - v.eye.z);
                    // Wholly in front, its middle well inside the picture, nearer than its fade starts: must be drawn.
                    const float* m = v.vp.m;
                    const double cx = m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12], cy = m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13];
                    const double cw = m[3] * c.x + m[7] * c.y + m[11] * c.z + m[15];
                    // (...and nearer than the view's far plane: the big map's far corners are past it from here)
                    const bool reachOk = (s.inst[i].reach <= 0 || d < s.inst[i].reach * 0.85) && d < 5800;
                    if (cw > r + 0.1 && std::fabs(cx) < 0.9 * cw && std::fabs(cy) < 0.9 * cw && reachOk) {
                        inView++;
                        if (got[i] < 0) dropped++;
                        else if (got[i] < 0.999f) badFade++;
                    }
                    if (dot(c - v.eye, fwd) < -r - 0.1 && got[i] >= 0) behind++;
                    if (s.inst[i].reach > 0 && d >= s.inst[i].reach && got[i] >= 0) behind++;
                }
            }
            char what[64];
            std::snprintf(what, sizeof what, "%s %s", map == MAP_FRONTIER ? "frontier" : "backcountry", k ? "bushes" : "trees");
            ctx.check(std::string(what) + ": some in view", inView > 50);
            ctx.check(std::string(what) + ": nothing in view dropped or faded", dropped == 0 && badFade == 0);
            ctx.check(std::string(what) + ": nothing behind the eye or past its reach drawn", behind == 0);
        }
    }
}

SCENARIO(forestBudget, "forest/budget", "The trees and bushes a dense view draws stay within a triangle and draw budget, shadows too; the scenery quality scales them") {
    World w(MAP_BACKCOUNTRY);
    forest::Set trees, bushes;
    forest::build(w, false, false, trees);
    forest::buildBushes(w, bushes);
    forest::Picker pt, pb;
    pt.build(trees), pb.build(bushes);
    std::vector<std::vector<forest::Pick>> b;
    V3 fwd;
    forest::View v = viewAt(w, -80, -250, 2, -39, -2, fwd);
    if (w.halfSize() > 1000) v.maxDist = 1200;  // (as the game draws a big map's forest at HIGH: render.cpp)
    pt.pick(trees, v, b);
    const Count t = count(trees, b);
    pb.pick(bushes, v, b);
    const Count u = count(bushes, b);
    ctx.metric("trees: triangles", (double)t.tris, "", 0, w.halfSize() > 1000 ? 260000 : 200000);  // (a big map: forest out to 1.2 km, not the old map's edge)
    ctx.metric("trees: draws", (double)t.draws, "", 0, 160);
    ctx.metric("bushes: triangles", (double)u.tris, "", 0, 30000);
    ctx.metric("bushes: draws", (double)u.draws, "", 0, 100);
    // Lower scenery quality: less.
    v.detail = 0.6;
    pt.pick(trees, v, b);
    ctx.metric("low detail: tree triangles over full", (double)count(trees, b).tris / std::max<long long>(1, t.tris), "", 0, 0.98);
    pb.pick(bushes, v, b);
    ctx.check("low detail draws fewer bushes", count(bushes, b).inst < u.inst);
    // A shadow pass (the main view's frustum standing in for the cascade's): coarser copies, bushes only near.
    v.detail = 1, v.lodBias = 1, v.shadow = true, v.reach = 0.5;
    pt.pick(trees, v, b);
    ctx.metric("shadows: tree triangles over the main view's", (double)count(trees, b).tris / std::max<long long>(1, t.tris), "", 0, 0.98);
    pb.pick(bushes, v, b);
    ctx.check("only near bushes cast shadows", count(bushes, b).inst < u.inst * 0.6);
}
