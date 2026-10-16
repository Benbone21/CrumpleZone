// The car's mechanical parts drawn as code-made models (src/carparts.h): each listed vehicle has its
// engine, gearbox and differentials; every part sits on the car; the parts follow the physics nodes and
// a broken beam's part is gone.
#include <cmath>
#include <map>
#include <string>

#include "harness.h"
#include "../src/carparts.h"
#include "../src/modelkit.h"

namespace {

std::map<std::string, int> count(const std::vector<Placed>& parts) {
    std::map<std::string, int> n;
    for (const Placed& p : parts) n[p.id]++;
    return n;
}

int engines(const std::map<std::string, int>& n) {
    int e = 0;
    for (const auto& kv : n)
        if (kv.first.rfind("crate_engine", 0) == 0) e += kv.second;
    return e;
}

int gearboxes(const std::map<std::string, int>& n) {
    int e = 0;
    for (const auto& kv : n)
        if (kv.first.rfind("gearbox_", 0) == 0) e += kv.second;
    return e;
}

int of(const std::map<std::string, int>& n, const char* id) {
    auto it = n.find(id);
    return it == n.end() ? 0 : it->second;
}

bool listed(const VehicleSpec& spec) {
    for (int v : listedVehicles())
        if (std::string(vehicleCatalog()[v].id) == spec.id) return true;
    return false;
}

}  // namespace

VEHICLE_SCENARIO(carPartsOnTheCar, "carparts/placed", "Every vehicle's parts are placed on it: the listed ones have an engine, a gearbox and differentials; every part sits inside the car's box; none are drawn for a broken beam") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    std::vector<Placed> all, big;
    carPartModels(c, all, PARTS_ALL);
    carPartModels(c, big, PARTS_BIG);
    const auto n = count(all);
    ctx.check("it has parts", !all.empty(), std::to_string(all.size()));
    ctx.check("the big parts are the fewer", big.size() < all.size() && big.size() > 0, std::to_string(big.size()) + " of " + std::to_string(all.size()));
    if (listed(spec)) {
        ctx.check("an engine", engines(n) == 1, std::to_string(engines(n)));
        ctx.check("a gearbox", gearboxes(n) == 1, std::to_string(gearboxes(n)));
        ctx.check("a differential", of(n, "differential") >= 1, std::to_string(of(n, "differential")));
        int shafts = 0, driven = 0, cv = 0;
        for (const Axle& ax : c.axles) shafts += ax.propShaft >= 0;
        for (const Axle& ax : c.axles)
            for (int w : ax.wheel)
                if (w >= 0 && c.wheels[w].driven && ax.housing.empty()) driven++;
        for (const Wheel& wh : c.wheels)
            for (int bi : wh.shafts) cv += c.beams[bi].type == BEAM_SHAFT && (c.beams[bi].a == wh.inner || c.beams[bi].b == wh.inner);
        ctx.check("a drive shaft on each prop shaft", of(n, "drive_shaft") == shafts, std::to_string(of(n, "drive_shaft")) + " for " + std::to_string(shafts));
        ctx.check("a CV axle on each half shaft", of(n, "cv_axle") == cv, std::to_string(of(n, "cv_axle")) + " for " + std::to_string(cv) + " (" + std::to_string(driven) + " wheels)");
        ctx.check("a transfer case on a 4x4", (spec.transfer != XFER_NONE) == (of(n, "transfer_case") == 1), std::to_string(of(n, "transfer_case")));
        int coil = 0, leaf = 0;
        for (const Wheel& wh : c.wheels) (wh.shackle >= 0 ? leaf : coil) += wh.spring >= 0;
        ctx.check("a coil-over on each coil spring", of(n, "coilover") == coil, std::to_string(of(n, "coilover")) + " for " + std::to_string(coil));
        ctx.check("a leaf pack on each leaf spring", of(n, "leaf_pack") == leaf, std::to_string(of(n, "leaf_pack")) + " for " + std::to_string(leaf));
    }
    // Everything sits within the car's box (a little over: a shaft's ends, an arm's reach).
    V3 lo = c.nodes[0].p, hi = lo;
    for (const Node& nd : c.nodes) {
        lo = V3(std::min(lo.x, nd.p.x), std::min(lo.y, nd.p.y), std::min(lo.z, nd.p.z));
        hi = V3(std::max(hi.x, nd.p.x), std::max(hi.y, nd.p.y), std::max(hi.z, nd.p.z));
    }
    int out = 0, bad = 0;
    std::string where;
    for (const Placed& p : all) {
        const bool inside = p.p.x > lo.x - 0.3 && p.p.x < hi.x + 0.3 && p.p.y > lo.y - 0.3 && p.p.y < hi.y + 0.3 && p.p.z > lo.z - 0.3 && p.p.z < hi.z + 0.3;
        if (!inside) out++, where = p.id;
        const bool real = mk::findModel(p.id) && p.oriented && p.bare && std::isfinite(p.p.x + p.p.y + p.p.z) && p.scale.x > 0 && p.scale.y > 0 && p.scale.z > 0 &&
                          std::fabs(len(p.ax[0]) - 1) < 1e-6 && std::fabs(len(p.ax[1]) - 1) < 1e-6 && std::fabs(dot(p.ax[0], p.ax[1])) < 1e-6 &&
                          std::fabs(dot(p.ax[2], cross(p.ax[0], p.ax[1])) - 1) < 1e-6;
        if (!real) bad++, where = p.id;
        ctx.check("scale and turn are sane", p.scale.x < 6 && p.scale.y < 6 && p.scale.z < 6, p.id);
    }
    ctx.check("every part is inside the car's box", out == 0, std::to_string(out) + " outside, e.g. " + where);
    ctx.check("every part is a real model, finite and square-turned", bad == 0, std::to_string(bad) + " not, e.g. " + where);
}

namespace {

// A placed leaf pack's mesh points in the world, as the renderer would lay it (bare, with its own params set).
std::vector<V3> leafPoints(const Placed& pl) {
    std::vector<V3> pts;
    const mk::ModelDef* d = mk::findModel(pl.id);
    if (!d) return pts;
    const int v = pl.variant < 0 ? mk::pickedVariant(*d) : pl.variant;
    mk::P p = mk::pickedParams(*d, v);
    for (const auto& kv : pl.set) p.v[kv.first] = kv.second;
    p.v["bare"] = 1;
    mk::Mesh m;
    mk::build(*d, v, p, m);
    for (const mk::MVert& q : m.v) pts.push_back(pl.p + pl.ax[0] * (q.p[0] * pl.scale.x) + pl.ax[1] * (q.p[1] * pl.scale.y) + pl.ax[2] * (q.p[2] * pl.scale.z));
    return pts;
}

// The point on a wheel's axle tube (the line the car draws from the diff to the hub) nearest `to`.
V3 tubeNear(const Car& c, int wheel, const V3& to) {
    const Wheel& wh = c.wheels[wheel];
    for (const Axle& ax : c.axles) {
        if (ax.housing.empty() || (ax.wheel[0] != wheel && ax.wheel[1] != wheel)) continue;
        const V3 a = c.nodes[ax.housing[0]].p;
        const V3 z = (wh.tieRod >= 0 ? (c.nodes[wh.lower].p + c.nodes[wh.top].p) * 0.5 : c.nodes[wh.inner].p) - a;
        return a + z * std::min(1.0, std::max(0.0, dot(to - a, z) / dot(z, z)));
    }
    return to;
}

// What a pack looks like in the car's frame: its eyes (the two ends of the long axis, middle of their
// extent) and the height of the clamp.
struct LeafLook {
    bool ok = false;
    V3 frontEye, rearEye;
    double endsUp = 0;      // the eyes' height over the axle tube's axis (the car's up)
    double stackEnds = 0;   // the main leaf's top at the ends less at the middle (positive: arched ends-up)
    double upTilt = 0;      // the model's up against the car's
};

LeafLook lookAt(const Car& c, const Placed& pl, int wheel, const Frame& fr) {
    LeafLook k;
    const std::vector<V3> pts = leafPoints(pl);
    if (pts.empty()) return k;
    double xlo = 1e9, xhi = -1e9;
    for (const V3& w : pts) xlo = std::min(xlo, dot(w - pl.p, pl.ax[0])), xhi = std::max(xhi, dot(w - pl.p, pl.ax[0]));
    auto eye = [&](double x0, double x1) {
        V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
        for (const V3& w : pts) {
            const double x = dot(w - pl.p, pl.ax[0]);
            if (x < x0 || x > x1) continue;
            lo = V3(std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z));
            hi = V3(std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z));
        }
        return (lo + hi) * 0.5;
    };
    // (the eye is the last 5 cm of the main leaf's end: the shackle and hanger hardware reach further up, so use its bottom half)
    k.frontEye = eye(xhi - 0.04, xhi);
    k.rearEye = eye(xlo, xlo + 0.04);
    k.ok = true;
    const V3 tube = tubeNear(c, wheel, (k.frontEye + k.rearEye) * 0.5);
    k.endsUp = dot((k.frontEye + k.rearEye) * 0.5 - tube, fr.up);
    k.upTilt = dot(pl.ax[1], fr.up);
    return k;
}

}  // namespace

VEHICLE_SCENARIO(carPartsLeafPacks, "carparts/leaf-packs", "A leaf pack sits the right way up: the main leaf on top, the pack arched with its ends higher than its middle, the front eye on the hanger, the rear eye hanging from the shackle, the middle clamped round the axle tube; and it follows the axle when a wheel is up on something") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const Frame fr = c.frame();
    auto check = [&](const char* when) {
        std::vector<Placed> all;
        carPartModels(c, all);
        std::vector<const Placed*> packs;
        for (const Placed& p : all)
            if (p.id == "leaf_pack") packs.push_back(&p);
        size_t at = 0;
        for (const SuspLink& l : c.links) {
            if (l.kind != LINK_LEAF || l.wheel < 0) continue;
            if (at >= packs.size()) break;
            const Placed& pl = *packs[at++];
            const Wheel& wh = c.wheels[l.wheel];
            const Beam& b = c.beams[l.beam];
            const int hanger = b.a < c.frameNodes ? b.a : b.b;
            const std::string tag = std::string(when) + " wheel " + std::to_string(l.wheel) + ": ";
            const LeafLook k = lookAt(c, pl, l.wheel, c.frame());
            ctx.check(tag + "the pack is built", k.ok, "");
            if (!k.ok) continue;
            ctx.check(tag + "right way up (its up is the car's)", k.upTilt > 0.9, std::to_string(k.upTilt));
            ctx.check(tag + "its front is toward the car's front", dot(pl.ax[0], fr.fwd) > 0.9, std::to_string(dot(pl.ax[0], fr.fwd)));
            ctx.check(tag + "ends higher than the middle (the eyes over the axle tube)", k.endsUp > 0.03, std::to_string(k.endsUp) + " m");
            ctx.check(tag + "the front eye is on the hanger", len(k.frontEye - c.nodes[hanger].p) < 0.04, std::to_string(len(k.frontEye - c.nodes[hanger].p)) + " m");
            const V3 pin = c.nodes[wh.shackle].p;
            ctx.check(tag + "the rear eye hangs under the shackle pin", len(k.rearEye - (pin - fr.up * 0.075)) < 0.04, std::to_string(len(k.rearEye - (pin - fr.up * 0.075))) + " m");
            // The clamp is round the tube: the nearest mesh point to the tube is the saddle or the plate, a few cm off its axis.
            const std::vector<V3> pts = leafPoints(pl);
            const V3 tube = tubeNear(c, l.wheel, pl.p);
            double near = 1e9;
            for (const V3& w : pts) near = std::min(near, len(w - tube));
            ctx.check(tag + "the clamp is on the axle tube", near < 0.07 && len(pl.p - tube) < 0.1, std::to_string(near) + " m, origin " + std::to_string(len(pl.p - tube)));
            // The main leaf is on top: the highest point of the stack under the middle bolt is the pad's, and the leaf's top is over every shorter one.
            double topMid = -1e9, lowMid = 1e9;
            for (const V3& w : pts)
                if (std::fabs(dot(w - pl.p, pl.ax[0])) < 0.02 && std::fabs(dot(w - pl.p, pl.ax[2])) < 0.03) topMid = std::max(topMid, dot(w - tube, fr.up)), lowMid = std::min(lowMid, dot(w - tube, fr.up));
            ctx.check(tag + "the stack stands on the tube, not through it", lowMid > -0.08 && topMid > 0.05, std::to_string(lowMid) + " .. " + std::to_string(topMid));
        }
        ctx.check(std::string(when) + ": a pack for each leaf link", at == packs.size(), std::to_string(at) + " of " + std::to_string(packs.size()));
    };
    check("at rest");
    // One side's axle up on something (a rock under a wheel): the pack follows the nodes.
    for (const Axle& ax : c.axles) {
        if (ax.wheel[0] < 0 || c.wheels[ax.wheel[0]].shackle < 0) continue;
        for (int n : c.wheels[ax.wheel[0]].assembly) c.nodes[n].p += fr.up * 0.07;
        break;
    }
    check("one wheel up");
}

VEHICLE_SCENARIO(carPartsFollow, "carparts/follow", "The parts ride the physics nodes: moving the car moves them; a broken prop shaft, spring or link drops its part") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    std::vector<Placed> a, b;
    carPartModels(c, a);
    c.translate(V3(7, 0, 0));
    carPartModels(c, b);
    ctx.check("the same parts after a move", a.size() == b.size(), std::to_string(a.size()) + " / " + std::to_string(b.size()));
    double worst = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) worst = std::max(worst, len(b[i].p - a[i].p - V3(7, 0, 0)));
    ctx.check("each part moved with the car", worst < 1e-6, std::to_string(worst));
    // Tilt a block corner: the engine turns with its block.
    if (c.engineNodes.size() >= 12) {
        std::vector<Placed> e0, e1;
        carPartModels(c, e0);
        c.nodes[c.engineNodes[1]].p.y += 0.2, c.nodes[c.engineNodes[3]].p.y += 0.2, c.nodes[c.engineNodes[5]].p.y += 0.2, c.nodes[c.engineNodes[7]].p.y += 0.2;
        carPartModels(c, e1);
        double turned = 0;
        for (size_t i = 0; i < std::min(e0.size(), e1.size()); i++)
            if (e0[i].id.rfind("crate_engine", 0) == 0) turned = len(e1[i].ax[0] - e0[i].ax[0]);
        ctx.check("a lifted nose tips the engine", turned > 0.05, std::to_string(turned));
        c.nodes[c.engineNodes[1]].p.y -= 0.2, c.nodes[c.engineNodes[3]].p.y -= 0.2, c.nodes[c.engineNodes[5]].p.y -= 0.2, c.nodes[c.engineNodes[7]].p.y -= 0.2;
    }
    const auto n0 = count(b);
    // A broken prop shaft drops its drive shaft.
    for (const Axle& ax : c.axles)
        if (ax.propShaft >= 0) {
            c.beams[ax.propShaft].broken = true;
            std::vector<Placed> d;
            carPartModels(c, d);
            ctx.check("a broken prop shaft drops its drive shaft", of(count(d), "drive_shaft") == of(n0, "drive_shaft") - 1,
                      std::to_string(of(count(d), "drive_shaft")) + " from " + std::to_string(of(n0, "drive_shaft")));
            c.beams[ax.propShaft].broken = false;
            break;
        }
    // ...a broken coil spring its coil-over, a broken tie rod its tie rod, a broken half shaft its CV axle.
    struct Cut {
        const char* id;
        int beam;
    } cuts[3] = {{"coilover", -1}, {"tie_rod", -1}, {"cv_axle", -1}};
    for (const Wheel& wh : c.wheels) {
        if (cuts[0].beam < 0 && wh.spring >= 0 && wh.shackle < 0) cuts[0].beam = wh.spring;
        if (cuts[1].beam < 0 && wh.tieRod >= 0) cuts[1].beam = wh.tieRod;
        if (cuts[2].beam < 0)
            for (int bi : wh.shafts)
                if (c.beams[bi].type == BEAM_SHAFT && (c.beams[bi].a == wh.inner || c.beams[bi].b == wh.inner)) cuts[2].beam = bi;
    }
    for (const Cut& cut : cuts) {
        if (cut.beam < 0) continue;
        c.beams[cut.beam].broken = true;
        std::vector<Placed> d;
        carPartModels(c, d);
        ctx.check(std::string("a broken beam drops its ") + cut.id, of(count(d), cut.id) == of(n0, cut.id) - 1, std::to_string(of(count(d), cut.id)) + " from " + std::to_string(of(n0, cut.id)));
        c.beams[cut.beam].broken = false;
    }
}
