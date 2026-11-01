// A car slammed into a tree, then reset (R: repaired in place) or put back on its wheels (T):
// it must come out clear of the trunk and drive away, not stay hung on it.
#include <cmath>
#include <cstdio>

#include "harness.h"

namespace {

const VehicleSpec& spec(const char* id) {
    for (const VehicleSpec& s : vehicleCatalog())
        if (std::string(s.id) == id) return s;
    return vehicleCatalog()[0];
}

struct TrunkFit {
    int inside = 0;          // body/wheel nodes inside the trunk's cylinder
    bool inFootprint = false;  // the trunk's axis within the car's outline (seen from above, in its own frame)
    double clearance = 1e9;  // nearest node's distance to the trunk's surface (m; negative: inside)
};

bool torn(const Car& c, int i) {
    for (const Panel& p : c.panels)
        if (p.state == 2)
            for (int j : p.panelNodes)
                if (j == i) return true;
    return false;
}

TrunkFit fit(const Car& c, const Pole& p) {
    TrunkFit f;
    const Frame fr = c.frame();
    const V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z)), left(fwd.z, 0, -fwd.x);
    double a0 = 1e9, a1 = -1e9, l0 = 1e9, l1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (int i = 0; i < (int)c.nodes.size(); i++) {
        if (torn(c, i)) continue;  // (a door lying where it came off isn't the car)
        const Node& n = c.nodes[i];
        const V3 r(n.p.x - fr.com.x, 0, n.p.z - fr.com.z);
        a0 = std::min(a0, dot(r, fwd)), a1 = std::max(a1, dot(r, fwd));
        l0 = std::min(l0, dot(r, left)), l1 = std::max(l1, dot(r, left));
        y0 = std::min(y0, n.p.y), y1 = std::max(y1, n.p.y);
        if (n.p.y < p.y0 || n.p.y > p.y1) continue;
        const double d = std::hypot(n.p.x - p.x, n.p.z - p.z) - p.r;
        f.clearance = std::min(f.clearance, d);
        f.inside += d < 0;
    }
    const V3 t(p.x - fr.com.x, 0, p.z - fr.com.z);
    const double ta = dot(t, fwd), tl = dot(t, left);
    f.inFootprint = ta > a0 && ta < a1 && tl > l0 && tl < l1 && y0 < p.y1 && y1 > p.y0;
    return f;
}

bool drivable(const Car& c) {
    if (c.engineDead || c.hydrolocked) return false;
    for (const Wheel& wh : c.wheels)
        if (wh.detached || wh.shaftBroken || wh.popped) return false;
    return true;
}

}  // namespace

SCENARIO(treeStuck, "recovery/tree-stuck", "Slammed into a tree at 60-120 km/h, then R (repaired in place) or T (back on its wheels): set down clear of the trunk, not on top of it, and drives away") {
    World& w = ctx.sim.world;
    const double x = -60, z = -20;
    w.addTree(x + 30, z, 16, 0);
    w.buildGrid();
    const Pole trunk = w.poles.back();
    struct Case {
        const char* id;
        double kmh, off, heading;  // speed, sideways offset from the trunk (m), heading (deg; 0 straight at it)
        bool boxedIn;              // a second tree right behind it after the hit: straight back isn't free
    };
    const Case cases[] = {
        {"blazer", 60, 0, 0, false}, {"blazer", 90, 0.4, 0, false}, {"blazer", 120, 0, 0, false}, {"blazer", 100, 0, 15, true},
        {"civic", 60, 0, 0, false},  {"civic", 90, 0.4, 0, false},  {"civic", 120, 0, 0, false},  {"civic", 100, 0, -15, true},
    };
    int struck = 0, stuckBefore = 0;
    for (const Case& cs : cases)
        for (int how = 0; how < 2; how++) {  // 0: R (recoverPlayer), 1: T in real recovery (rightPlayer)
            ctx.sim.cars.clear();
            ctx.sim.ropes.clear();
            const double hd = cs.heading * M_PI / 180, back = 30;
            Car& c = ctx.add(spec(cs.id), x + 30 - back * std::cos(hd), z + cs.off + back * std::sin(hd), cs.heading);
            ctx.focus(c);
            ctx.run(0.3);
            ctx.launch(c, cs.kmh / 3.6);
            ctx.run(3.0, [&] { c.throttle = 0.6; });
            const TrunkFit hit = fit(c, trunk);
            struck += hit.clearance < 0.3;
            stuckBefore += hit.inFootprint;
            size_t extra = w.poles.size();
            if (cs.boxedIn) {
                const Frame fr = c.frame();
                const V3 bk = -norm(V3(fr.fwd.x, 0, fr.fwd.z)) * (c.spec->length * 0.5 + 1.0);
                w.addTree(fr.com.x + bk.x, fr.com.z + bk.z, 14, 1);
                w.buildGrid();
            }
            if (how == 0) ctx.sim.recoverPlayer();
            else ctx.sim.rightPlayer();
            Car& r = ctx.sim.cars[ctx.sim.player];
            const TrunkFit placed = fit(r, trunk);
            const TrunkFit placed2 = cs.boxedIn ? fit(r, w.poles[extra]) : TrunkFit();
            ctx.run(1.0, [&] { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); });
            const TrunkFit settled = fit(r, trunk);
            const bool go = how == 0 || drivable(r);
            const V3 p0 = r.frame().com;
            if (go) ctx.run(4.0, [&] { ctx.sim.drive(false, true, false, false, false, Ctx::kFrame); });  // backs away
            const double backed = std::hypot(r.frame().com.x - p0.x, r.frame().com.z - p0.z);
            if (cs.boxedIn) {  // (the extra tree out again)
                w.poles.erase(w.poles.begin() + extra);
                w.trees.pop_back();
                w.buildGrid();
            }
            char tag[96], det[240];
            std::snprintf(tag, sizeof tag, "%s %.0f km/h%s%s, %s", cs.id, cs.kmh, cs.off ? " offset" : "", cs.boxedIn ? " boxed in" : "", how ? "T" : "R");
            std::snprintf(det, sizeof det, "hit: %d nodes in the trunk, it in the outline %d, clearance %.2f m | placed: %d / %d / %.2f m, on the ground %.2f m | settled: %d / %d / %.2f m | backed %.1f m",
                          hit.inside, hit.inFootprint, hit.clearance, placed.inside, placed.inFootprint, placed.clearance,
                          r.frame().com.y - w.height(r.frame().com.x, r.frame().com.z), settled.inside, settled.inFootprint, settled.clearance, backed);
            ctx.note(std::string(tag) + ": " + det);
            ctx.check(std::string(tag) + ": set down clear of the trunk", placed.inside == 0 && !placed.inFootprint && placed.clearance > 0.1, det);
            if (cs.boxedIn) ctx.check(std::string(tag) + ": and of the tree behind it", placed2.inside == 0 && !placed2.inFootprint, det);
            ctx.check(std::string(tag) + ": still clear a second later", settled.inside == 0 && !settled.inFootprint, det);
            if (go) ctx.metric(std::string(tag) + ": backs away in 4 s", backed, "m", 3, 100);
        }
    ctx.metric("runs that struck the trunk", struck, "", 12, 16);
    ctx.metric("...and stopped with it inside the car's outline (the bug's setup)", stuckBefore, "", 4, 16);
}
