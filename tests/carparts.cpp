// The car's mechanical parts drawn as code-made models (src/carparts.h): each listed vehicle has its
// engine, gearbox and differentials; every part sits on the car; the parts follow the physics nodes and
// a broken beam's part is drawn snapped.
#include <cmath>
#include <map>
#include <string>

#include "harness.h"
#include "../src/carparts.h"
#include "../src/interact.h"
#include "../src/walker.h"
#include "../src/modelkit.h"
#include "../src/mods.h"

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

VEHICLE_SCENARIO(carPartsFollow, "carparts/follow", "The parts ride the physics nodes: moving the car moves them, and an intact car has nothing cut or marked") {
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
    // Nothing about an intact car is cut or marked.
    int marked = 0;
    for (const Placed& p : b) marked += p.clip || p.wear || p.id == "torn_end";
    ctx.check("an intact car has nothing snapped, bent or marked", marked == 0, std::to_string(marked));
}

namespace {

// A placement's -X and +X ends in the world (its model's bounds, cut to what's drawn).
void ends(const Placed& pl, V3& lo, V3& hi) {
    double x0 = -0.5, x1 = 0.5;
    if (const mk::ModelDef* d = mk::findModel(pl.id)) {
        const int v = pl.variant < 0 ? mk::pickedVariant(*d) : pl.variant;
        mk::P p = mk::pickedParams(*d, v);
        for (const auto& kv : pl.set) p.v[kv.first] = kv.second;
        p.v["bare"] = 1;
        mk::Mesh m;
        mk::build(*d, v, p, m);
        x0 = m.lo.x, x1 = m.hi.x;
    }
    if (pl.clip) x0 = std::max(x0, (double)pl.clipLo), x1 = std::min(x1, (double)pl.clipHi);
    lo = pl.p + pl.ax[0] * (x0 * pl.scale.x);
    hi = pl.p + pl.ax[0] * (x1 * pl.scale.x);
}

std::vector<const Placed*> ofId(const std::vector<Placed>& v, const char* id) {
    std::vector<const Placed*> o;
    for (const Placed& p : v)
        if (p.id == id) o.push_back(&p);
    return o;
}

}  // namespace

VEHICLE_SCENARIO(carPartsBreak, "carparts/damage", "A broken part is drawn broken: a snapped prop shaft is two halves, each hanging from its own end node with a torn end, drooping; a snapped CV axle, tie rod, arm or coil-over the same; a bent tie rod or arm is kinked at the middle; all marked as damaged") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    std::vector<Placed> whole;
    carPartModels(c, whole);
    const auto n0 = count(whole);
    // `halves`: the two cut pieces of `id` (clipped, marked broken) and the torn ends that cap them; each half's outer end is at its own node.
    auto snapped = [&](const char* what, const char* id, int beam) {
        c.beams[beam].broken = true;
        std::vector<Placed> d;
        carPartModels(c, d);
        const auto n = count(d);
        ctx.check(std::string("a broken ") + what + " is two halves", of(n, id) == of(n0, id) + 1, std::to_string(of(n, id)) + " from " + std::to_string(of(n0, id)));
        ctx.check("...with a torn end on each", of(n, "torn_end") == of(n0, "torn_end") + 2, std::to_string(of(n, "torn_end")));
        int cut = 0, red = 0;
        for (const Placed* p : ofId(d, id)) cut += p->clip, red += p->wear == 2;
        ctx.check(std::string("...both cut and marked broken: ") + what, cut == 2 && red == 2, std::to_string(cut) + " cut, " + std::to_string(red) + " marked");
        // Each half's far end (the one not cut) is at one of the beam's nodes, one half at each.
        V3 a = c.nodes[c.beams[beam].a].p, e = c.nodes[c.beams[beam].b].p;
        if (std::string(id) == "drive_shaft" && listed(spec)) {  // (a prop shaft is drawn joint to joint, between the yokes, not node to node)
            double best = 1e9;
            for (const Placed* p : ofId(whole, id)) {
                V3 ja, je;
                if (!driveShaftJoints(*p, ja, je)) continue;
                const double d = len(ja - a) + len(je - e);
                if (d < best) best = d, a = ja, e = je;
            }
        }
        std::vector<const Placed*> halves;
        for (const Placed* p : ofId(d, id))
            if (p->clip) halves.push_back(p);
        double worst = 0;
        if (halves.size() == 2) {
            for (const Placed* p : halves) {
                V3 lo, hi;
                ends(*p, lo, hi);
                const V3 far = p->clipHi < 1e8f ? lo : hi;
                worst = std::max(worst, std::min(len(far - a), len(far - e)));
            }
            V3 l0, h0, l1, h1;
            ends(*halves[0], l0, h0);
            ends(*halves[1], l1, h1);
            const V3 f0 = halves[0]->clipHi < 1e8f ? l0 : h0, f1 = halves[1]->clipHi < 1e8f ? l1 : h1;
            const bool oneEach = (len(f0 - a) < len(f0 - e)) != (len(f1 - a) < len(f1 - e));
            ctx.check(std::string("...each half still on its own end: ") + what, (oneEach || std::string(id) == "cv_axle") && worst < (std::string(id) == "cv_axle" || std::string(id) == "drive_shaft" ? 0.25 : 0.12), std::to_string(worst) + " m");
            // The free ends hang below where the whole part lay (unless it was standing upright).
            const V3 chord = e - a;
            if (std::fabs(chord.y) < 0.5 * len(chord)) {
                double lowest = 1e9;
                for (const Placed* p : ofId(d, "torn_end")) lowest = std::min(lowest, p->p.y);
                ctx.check(std::string("...and droop: ") + what, lowest < std::min(a.y, e.y) - 0.01, std::to_string(lowest - std::min(a.y, e.y)) + " m");
            }
        }
        c.beams[beam].broken = false;
    };
    for (int ai = 0; ai < (int)c.axles.size(); ai++)
        if (c.axles[ai].propShaft >= 0) {
            snapped("prop shaft", "drive_shaft", c.axles[ai].propShaft);
            break;
        }
    int cvBeam = -1, springBeam = -1, tieBeam = -1;
    for (const Wheel& wh : c.wheels) {
        if (springBeam < 0 && wh.spring >= 0 && wh.shackle < 0) springBeam = wh.spring;
        if (tieBeam < 0 && wh.tieRod >= 0) tieBeam = wh.tieRod;
        if (cvBeam < 0)
            for (int bi : wh.shafts)
                if (c.beams[bi].type == BEAM_SHAFT && (c.beams[bi].a == wh.inner || c.beams[bi].b == wh.inner)) cvBeam = bi;
    }
    if (springBeam >= 0) snapped("coil-over", "coilover", springBeam);
    if (tieBeam >= 0) snapped("tie rod", "tie_rod", tieBeam);
    if (cvBeam >= 0) snapped("half shaft", "cv_axle", cvBeam);
    // A bent tie rod (the wheel's knocked out of line): two cut halves meeting at a kink, marked bent.
    for (int k = 0; k < (int)c.wheels.size(); k++) {
        if (c.wheels[k].tieRod < 0) continue;
        c.wheels[k].tieOffset = 0.04;
        std::vector<Placed> d;
        carPartModels(c, d);
        c.wheels[k].tieOffset = 0;
        const auto rods = ofId(d, "tie_rod");
        int kinked = 0;
        for (const Placed* p : rods) kinked += p->clip && p->wear == 1;
        ctx.check("a bent tie rod is drawn in two kinked halves", of(count(d), "tie_rod") == of(n0, "tie_rod") + 1 && kinked == 2, std::to_string(kinked) + " bent halves");
        if (kinked == 2) {
            std::vector<const Placed*> h;
            for (const Placed* p : rods)
                if (p->clip) h.push_back(p);
            ctx.check("...the halves point different ways", dot(h[0]->ax[0], h[1]->ax[0]) < 0.995, std::to_string(dot(h[0]->ax[0], h[1]->ax[0])));
            V3 l0, h0, l1, h1;
            ends(*h[0], l0, h0);
            ends(*h[1], l1, h1);
            // ...and meet: the +X end of the first (cut at 0) and the -X end of the second are the same point (the kink).
            const V3 k0 = h[0]->clipHi < 1e8f ? h0 : l0, k1 = h[1]->clipHi < 1e8f ? h1 : l1;
            ctx.check("...and meet at the kink", len(k0 - k1) < 0.01, std::to_string(len(k0 - k1)) + " m");
        }
        break;
    }
    // A bent control arm (a link shortened for good): kinked too.
    for (const SuspLink& l : c.links)
        if (l.kind == LINK_LOWER_ARM && l.wheel >= 0) {
            Beam& b = c.beams[l.beam];
            const double was = b.L0;
            b.L0 = b.Lorig * 0.95;
            std::vector<Placed> d;
            carPartModels(c, d);
            b.L0 = was;
            int bentHalves = 0;
            for (const Placed* p : ofId(d, "control_arm")) bentHalves += p->clip && p->wear == 1;
            ctx.check("a bent control arm is drawn in two kinked halves", bentHalves == 2, std::to_string(bentHalves));
            break;
        }
    // A flooded engine and a dead one are marked.
    if (listed(spec)) {
        c.hydrolocked = true;
        std::vector<Placed> d;
        carPartModels(c, d);
        c.hydrolocked = false;
        int sooty = 0;
        for (const Placed& p : d) sooty += p.id.rfind("crate_engine", 0) == 0 && p.wear == 3;
        ctx.check("a flooded engine is marked", sooty == 1, std::to_string(sooty));
    }
}

VEHICLE_SCENARIO(carPartsHover, "carparts/hover", "Aiming at a part names it: a ray from a known eye at a known part returns that part, its name and how it is; the body hides what's under it, except a hurt part; far or missed rays find nothing") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    if (!listed(spec)) return;
    const Frame fr = c.frame();
    std::vector<Placed> parts;
    std::vector<PartTag> tags;
    carPartModels(c, parts, PARTS_ALL, &tags);
    ctx.check("every part is tagged", tags.size() == parts.size(), std::to_string(tags.size()) + " tags, " + std::to_string(parts.size()) + " parts");
    // The engine: from above (the body's skin is in the way), then with the body ignored.
    for (const PartTag& t : tags) {
        if (t.kind != PK_ENGINE) continue;
        const Placed& pl = parts[t.index];
        const V3 mid = pl.p + pl.ax[0] * 0.0;
        const V3 eye = mid + fr.up * 2.0 + fr.left * 0.3;
        PartHit h;
        ctx.check("the engine is found with the body ignored", pickCarPart(c, eye, mid - eye, 4.0, false, h) && h.tag.kind == PK_ENGINE, h.ok ? h.tag.label() : "no hit");
        ctx.check("it's named, with its make and how it is", h.ok && h.tag.label() == "ENGINE: " + std::string(spec.engineName) + ", OK", h.ok ? h.tag.label() : "");
        PartHit blocked;
        const bool seen = pickCarPart(c, eye, mid - eye, 4.0, true, blocked) && blocked.tag.kind == PK_ENGINE;
        if (spec.extras & EXTRA_BAJA)  // (a Baja Bug's engine stands bare out of its cut-off tail)
            ctx.check("the bare engine is seen from above", seen, seen ? "" : "hidden");
        else
            ctx.check("the body hides it from above", !seen, seen ? "seen through the roof" : "");
        c.hydrolocked = true;
        PartHit fl;
        const bool got = pickCarPart(c, eye, mid - eye, 4.0, false, fl);
        c.hydrolocked = false;
        ctx.check("a flooded engine says so", got && fl.tag.state == PS_FLOODED && fl.tag.label().find("FLOODED") != std::string::npos, got ? fl.tag.label() : "no hit");
        break;
    }
    // A prop shaft, from underneath: named by its axle, then broken.
    for (int ai = 0; ai < (int)c.axles.size(); ai++) {
        const Axle& ax = c.axles[ai];
        if (ax.propShaft < 0) continue;
        const Beam& b = c.beams[ax.propShaft];
        const V3 mid = (c.nodes[b.a].p + c.nodes[b.b].p) * 0.5, eye = mid - fr.up * 1.2;
        PartHit h;
        const std::string want = std::string(ax.front ? "FRONT" : "REAR") + " DRIVE SHAFT: OK";
        ctx.check("a drive shaft is found from underneath", pickCarPart(c, eye, mid - eye, 4.0, true, h) && h.tag.kind == PK_PROP_SHAFT, h.ok ? h.tag.label() : "no hit");
        ctx.check("...named for its axle", h.ok && h.tag.label() == want, h.ok ? h.tag.label() : "");
        ctx.check("...at the right distance", h.ok && std::fabs(h.t - 1.2) < 0.3, std::to_string(h.t));
        PartHit far;
        ctx.check("nothing past the reach", !pickCarPart(c, mid - fr.up * 9.0, mid - (mid - fr.up * 9.0), 4.0, true, far), far.ok ? far.tag.label() : "");
        PartHit away;
        ctx.check("nothing when looking away", !pickCarPart(c, eye, -fr.up, 4.0, true, away), away.ok ? away.tag.label() : "");
        c.beams[ax.propShaft].broken = true;
        // (after it's snapped: aim at one of its halves)
        std::vector<Placed> d;
        std::vector<PartTag> dt;
        carPartModels(c, d, PARTS_ALL, &dt);
        bool ok = false;
        std::string got = "no half";
        for (const PartTag& t : dt)
            if (t.kind == PK_PROP_SHAFT && d[t.index].id == "torn_end") {  // (the torn end: out in the open, clear of the diff and gearbox)
                const V3 at = d[t.index].p, eye2 = at - fr.up * 1.0;
                PartHit bh;
                ok = pickCarPart(c, eye2, at - eye2, 4.0, true, bh) && bh.tag.kind == PK_PROP_SHAFT && bh.tag.label() == std::string(ax.front ? "FRONT" : "REAR") + " DRIVE SHAFT: BROKEN";
                got = bh.ok ? bh.tag.label() : "no hit";
                break;
            }
        ctx.check("a snapped shaft's torn end is found, and called broken (a hurt part shows through the body)", ok, got);
        // Its repair is the one that goes with it.
        bool matched = false;
        for (const Fix& f : fixesFor(c))
            matched |= f.kind == Fix::DRIVE_SHAFT && partMatchesFix(h.tag, f) && h.ok;
        ctx.check("the broken shaft matches its repair", matched, "");
        c.beams[ax.propShaft].broken = false;
        break;
    }
}

SCENARIO(carPartsHoverFix, "carparts/hover-fix", "With two repairs near each other, the part under the crosshair decides which one hold-E does: a tie rod and a CV axle, aimed at one after the other") {
    const VehicleSpec* spec = nullptr;
    for (const VehicleSpec& s : vehicleCatalog())
        if (std::string(s.id) == "4runner") spec = &s;
    Car& c = ctx.add(*spec, -120, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    c.handbrake = 1;
    int k = -1;
    for (int i = 0; i < (int)c.wheels.size() && k < 0; i++)
        if (c.wheels[i].front && !c.wheels[i].shafts.empty() && c.wheels[i].tieRod >= 0) k = i;
    ctx.check("a front corner with a half shaft and a tie rod", k >= 0, "");
    if (k < 0) return;
    Wheel& wh = c.wheels[k];
    wh.tieOffset = 0.05;
    wh.shaftBroken = true;
    for (int bi : wh.shafts) c.beams[bi].broken = true;
    std::vector<Placed> parts;
    std::vector<PartTag> tags;
    carPartModels(c, parts, PARTS_ALL, &tags);
    Interact ia;
    Walker wk;
    const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
    const V3 out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    wk.p = hub + out * 1.4;
    wk.p.y = ctx.sim.world.height(wk.p.x, wk.p.z);
    const V3 eye = wk.p + V3(0, 1.62, 0);
    int aimed = 0;
    std::string tried;
    for (const PartTag& want : tags) {
        if (want.wheel != k || (want.kind != PK_TIE_ROD && want.kind != PK_CV_AXLE) || parts[want.index].id == "torn_end") continue;
        // Aim at the part's own middle (its first half).
        const Placed& pl = parts[want.index];
        V3 lo, hi;
        ends(pl, lo, hi);
        const V3 at = (lo + hi) * 0.5;
        PartHit h;
        if (!pickCarPart(c, eye, at - eye, 4.0, true, h) || h.tag.kind != want.kind) {  // (something nearer is in the way: the other part's turn)
            tried += want.name + " -> " + (h.ok ? h.tag.name : "nothing") + "; ";
            continue;
        }
        aimed++;
        h.car = 0;
        const InteractOption o = Interact::pick(ctx.sim, wk, eye, at - eye, nullptr, Kit(), nullptr, &h);
        ctx.check(std::string("aimed at the ") + want.name + ": that repair is the one picked", o.kind == IA_FIX && partMatchesFix(h.tag, o.fix), o.prompt);
        ctx.check("...and its kind is the part's", (want.kind == PK_CV_AXLE) == (o.fix.kind == Fix::CV_AXLE), o.prompt);
    }
    ctx.check("both parts could be aimed at", aimed >= 2, std::to_string(aimed) + ": " + tried);
}


// A drive shaft is drawn from the drive-line's own yokes: each end's U-joint sits on the yoke it couples (the transfer case's or
// gearbox's output yoke, the axle's pinion yoke), not on the physics node the shaft's beam happens to run between. It has to,
// whatever the vehicle, whatever its suspension (every lift kit and spring set it can be fitted with) and wherever the axles are
// in their travel: full droop, full bump, one wheel up.
SCENARIO(driveShaftOnYokes, "carparts/drive-shaft-joints", "Every listed vehicle with each of its suspensions, at rest, at full bump, at full droop and one wheel up: every drive shaft's two U-joints are within 3 cm of the output yoke and pinion yoke they couple, and it is not stretched out of shape") {
    int lane = 0;
    for (int v : listedVehicles()) {
        for (int o = 0; o < modOwnCount(v, MOD_SUSPENSION); o++) {
            ModSet ms;
            ms.pick[MOD_SUSPENSION] = o;
            const VehicleSpec& spec = tunedSpec(v, ms);
            Car& c = ctx.add(spec, -100, -50 - 8 * lane++, 0);
            ctx.focus(c);
            ctx.run(0.6);
            const Frame fr = c.frame();
            auto shiftAxles = [&](double dy, int onlyWheel) {
                for (int wi = 0; wi < (int)c.wheels.size(); wi++)
                    if (onlyWheel < 0 || wi == onlyWheel)
                        for (int n : c.wheels[wi].assembly) c.nodes[n].p += fr.up * dy;
            };
            auto check = [&](const std::string& when) {
                const std::string tag = std::string(spec.id) + " suspension " + std::to_string(o) + " " + when + ": ";
                std::vector<Placed> all;
                carPartModels(c, all);
                std::vector<V3> yokes;  // every yoke a shaft can couple to
                int shafts = 0;
                for (const Placed& pl : all) {
                    if (pl.id == "differential" || pl.id == "gearbox_manual" || pl.id == "gearbox_auto") yokes.push_back(partYoke(pl, false));
                    if (pl.id == "transfer_case") yokes.push_back(partYoke(pl, false)), yokes.push_back(partYoke(pl, true));
                }
                for (const Placed& pl : all) {
                    V3 a, e;
                    if (!driveShaftJoints(pl, a, e)) continue;
                    shafts++;
                    double da = 1e9, de = 1e9;
                    for (const V3& y : yokes) da = std::min(da, len(a - y)), de = std::min(de, len(e - y));
                    ctx.check(tag + "the shaft's front joint is on a yoke", da < 0.03, std::to_string(da * 100) + " cm");
                    ctx.check(tag + "the shaft's rear joint is on a yoke", de < 0.03, std::to_string(de * 100) + " cm");
                    ctx.check(tag + "the shaft is not stretched out of shape", pl.scale.x > 0.45 && pl.scale.x < 1.9, "x" + std::to_string(pl.scale.x));
                }
                // (and the yokes are where the physics' shaft beams are, give or take: the drive line is drawn where it runs)
                for (const Axle& ax : c.axles) {
                    if (ax.propShaft < 0) continue;
                    const Beam& b = c.beams[ax.propShaft];
                    double worst = 0;
                    for (int n : {b.a, b.b}) {
                        double d = 1e9;
                        for (const V3& y : yokes) d = std::min(d, len(c.nodes[n].p - y));
                        worst = std::max(worst, d);
                    }
                    ctx.check(tag + "the shaft's beam ends are near the yokes", worst < 0.45, std::to_string(worst * 100) + " cm");
                }
                int want = 0;
                for (const Axle& ax : c.axles) want += ax.propShaft >= 0;
                ctx.check(tag + "a drawn shaft for each prop shaft", shafts == want, std::to_string(shafts) + " of " + std::to_string(want));
            };
            check("at rest");
            shiftAxles(0.12, -1);
            check("at full bump");
            shiftAxles(-0.24, -1);
            check("at full droop");
            shiftAxles(0.12, -1);
            shiftAxles(0.1, 0);
            check("one wheel up");
        }
    }
}
