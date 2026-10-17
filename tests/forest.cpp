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
