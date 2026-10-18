// The sculpted body shells (bodyshell.cpp) cut their flat runs of quads into fewest triangles: a triangle budget per
// vehicle, and a warped body still carries the merged surface with its nodes (no vertex of the full grid strays from
// the merged triangle that replaced it).
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#include "../src/bodyshell.h"
#include "../src/carvisual.h"
#include "harness.h"

namespace {

// What evalBody would draw: a triangle face is one triangle, a quad two.
size_t trianglesOf(const VisualTemplate& t) {
    size_t n = 0;
    for (const auto& f : t.faces) n += f.rv[3] == f.rv[2] ? 1 : 2;
    return n;
}

// Every topological vertex on the (live) nodes.
std::vector<V3> livePos(const Car& c, const VisualTemplate& t) {
    std::vector<V3> p(t.topoCount);
    for (int v = 0; v < t.topoCount; v++)
        for (int k = t.wStart[v]; k < t.wStart[v + 1]; k++) p[v] += c.nodes[t.wNode[k]].p * t.wVal[k];
    return p;
}

V3 restOf(const VisualTemplate& t, int rv) { return V3(t.rvStatic[rv].lat[0], t.rvStatic[rv].lat[1], t.rvStatic[rv].lat[2]); }

// Distance from p to triangle abc.
double toTriangle(const V3& p, const V3& a, const V3& b, const V3& c) {
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const V3 n = cross(ab, ac);
    const double nl = len(n);
    const V3 q = p - n * (dot(ap, n) / std::max(nl * nl, 1e-18));
    const double w1 = dot(cross(q - a, ac), n) / std::max(nl * nl, 1e-18), w2 = dot(cross(ab, q - a), n) / std::max(nl * nl, 1e-18);
    if (nl > 1e-9 && w1 >= 0 && w2 >= 0 && w1 + w2 <= 1) return len(p - q);
    double best = 1e9;
    const V3 e[3][2] = {{a, b}, {b, c}, {c, a}};
    for (const auto& s : e) {
        const V3 d = s[1] - s[0];
        const double u = std::clamp(dot(p - s[0], d) / std::max(dot(d, d), 1e-12), 0.0, 1.0);
        best = std::min(best, len(p - (s[0] + d * u)));
    }
    return best;
}

struct Budget {
    const char* id;
    size_t tris;
};

}  // namespace

SCENARIO(carShell, "visual/body-shell", "Each sculpted body shell draws within its triangle budget (flat runs merged), and after a warp every vertex of the full grid still lies on the merged surface that replaced it") {
    const Budget budgets[] = {{"4runner", 22000}, {"bronco", 21000}, {"civic", 20800}, {"tacoma", 26200}, {"blazer", 21900}, {"baja", 18200}};
    for (const Budget& b : budgets) {
        const VehicleSpec* vs = nullptr;
        for (const VehicleSpec& s : vehicleCatalog())
            if (std::string(s.id) == b.id) vs = &s;
        if (!vs) {
            ctx.check(std::string(b.id) + ": in the catalog", false);
            continue;
        }
        Car c;
        c.build(*vs, V3(), 0, V3(0.4, 0.5, 0.6));
        VisualTemplate merged, full;
        ctx.check(std::string(b.id) + ": a shell", buildBodyShell(c, merged, true) && buildBodyShell(c, full, false));
        const size_t tm = trianglesOf(merged), tf = trianglesOf(full);
        ctx.metric(std::string(b.id) + ": body shell triangles", (double)tm, "tris", 0, (double)b.tris);
        // (The Baja's Beetle dome is curved nearly everywhere: only its tub sides and running boards merge.)
        ctx.check(std::string(b.id) + ": fewer than the full grid", tm * 100 < tf * (std::string(b.id) == "baja" ? 97 : 85), std::to_string(tm) + " of " + std::to_string(tf));
        ctx.check(std::string(b.id) + ": glass and panels keep their faces", merged.topoCount < full.topoCount);

        // (A few panel vertices sit off their rest spot by design: skip them and any triangle that has one.)
        auto stray = [&](const VisualTemplate& tp) {
            const auto P0 = livePos(c, tp);
            std::vector<char> bad(tp.rvTopo.size());
            for (size_t r = 0; r < bad.size(); r++) bad[r] = len(restOf(tp, (int)r) - P0[tp.rvTopo[r]]) > 0.2;
            return bad;
        };
        std::vector<char> badF = stray(full);
        {  // (and every vertex that shares a spot with one: the other side of that seam)
            std::set<std::array<long, 3>> spots;
            const auto key = [&](int r) { const V3 p = restOf(full, r); return std::array<long, 3>{std::lround(p.x * 500), std::lround(p.y * 500), std::lround(p.z * 500)}; };
            for (size_t r = 0; r < badF.size(); r++)
                if (badF[r]) spots.insert(key((int)r));
            for (size_t r = 0; r < badF.size(); r++)
                if (spots.count(key((int)r))) badF[r] = 1;
        }
        // Warp the body: a smooth bend and twist plus a dent, then compare.
        Car& w = c;
        for (size_t n = 0; n < w.nodes.size(); n++) {
            const V3 r = w.nodes[n].p;  // (the car is built at the origin, heading 0)
            const V3 d(0.03 * std::sin(3 * r.z), 0.08 * std::sin(2.1 * r.x) + 0.05 * r.z * r.x, 0.04 * std::sin(2.7 * r.y + r.x));
            w.nodes[n].p += d;
            if (len(r - V3(0.5, 0.4, 0.5)) < 0.5) w.nodes[n].p += V3(0, -0.12, -0.1);
        }
        const std::vector<V3> pm = livePos(w, merged), pf = livePos(w, full);
        double worst = 0;
        for (size_t r = 0; r < full.rvTopo.size(); r++) {
            if (badF[r]) continue;
            const V3 rest = restOf(full, (int)r), live = pf[full.rvTopo[r]];
            // The merged triangles covering this rest point (all of them, any panel), nearest in the warped body.
            double best = 1e9;
            for (const auto& f : merged.faces)
                for (int k = 0; k < (f.rv[3] == f.rv[2] ? 1 : 2); k++) {
                    const int i0 = f.rv[0], i1 = f.rv[1 + k], i2 = f.rv[2 + k];
                    if (toTriangle(rest, restOf(merged, i0), restOf(merged, i1), restOf(merged, i2)) > 1e-3) continue;
                    const double dd = toTriangle(live, pm[merged.rvTopo[i0]], pm[merged.rvTopo[i1]], pm[merged.rvTopo[i2]]);
                    best = std::min(best, dd);
                }
            if (best < 1e8) worst = std::max(worst, best);
        }
        ctx.metric(std::string(b.id) + ": warped, the grid's farthest vertex from its merged triangle", worst, "m", 0, 0.05);
    }
}
