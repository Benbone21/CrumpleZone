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
