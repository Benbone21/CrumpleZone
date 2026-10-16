// A truck's ladder frame drawn as code-made models (src/carframe.h, src/models/frames.cpp): every body-on-frame
// vehicle has its rails strung along the frame's nodes, its crossmembers, brackets, body mounts, recovery points and
// jack pads; the pieces follow the nodes (a bent frame draws bent); the frame's jack pads are where Car::jackPoint
// is, and a jack worked up there lifts the body.
#include <cmath>
#include <map>
#include <string>

#include "harness.h"
#include "../src/carframe.h"
#include "../src/interact.h"
#include "../src/modelkit.h"

namespace {

std::map<std::string, int> count(const std::vector<Placed>& parts) {
    std::map<std::string, int> n;
    for (const Placed& p : parts) n[p.id]++;
    return n;
}
int of(const std::map<std::string, int>& n, const char* id) {
    auto it = n.find(id);
    return it == n.end() ? 0 : it->second;
}

}  // namespace

SCENARIO(frameModelsRegistered, "carframe/models", "The frame's pieces are all there as models, each with at least two designs") {
    for (const char* id : {"frame_rail", "frame_cross", "frame_hanger", "frame_shackle", "frame_tower", "frame_stay", "frame_bodymount", "frame_hook", "frame_skid", "frame_jackpad"}) {
        const mk::ModelDef* d = mk::findModel(id);
        ctx.check(std::string(id) + ": registered", d != nullptr);
        if (d) ctx.check(std::string(id) + ": two or more designs", d->variants.size() >= 2, std::to_string(d->variants.size()));
    }
}

VEHICLE_SCENARIO(frameOnTheCar, "carframe/placed", "Every ladder-frame vehicle has its frame as models: a rail section between every two stations of each rail (boxed or C-channel as that truck's is), a crossmember at each tie, brackets, body mounts, four recovery points, a skid plate, a jack pad at each wheel's jack point; a unibody has none") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.focus(c);
    ctx.run(1.0);
    std::vector<Placed> all, big;
    carFrameModels(c, all, PARTS_ALL);
    carFrameModels(c, big, PARTS_BIG);
    if (c.ladder.empty()) {
        ctx.check("a unibody has no frame models", all.empty(), std::to_string(all.size()));
        return;
    }
    const auto n = count(all);
    const int stretches = 2 * (c.ladderStations - 1);
    ctx.check("a rail section on every stretch of both rails", of(n, "frame_rail") == stretches, std::to_string(of(n, "frame_rail")) + " for " + std::to_string(stretches));
    ctx.check("a crossmember at each tie", of(n, "frame_cross") == (int)c.crossStations.size(), std::to_string(of(n, "frame_cross")) + " for " + std::to_string(c.crossStations.size()));
    ctx.check("a dropped gearbox crossmember", [&] {
        int k = 0;
        for (const Placed& p : all) k += p.id == "frame_cross" && p.variant == 2;
        return k == 1;
    }());
    ctx.check("four recovery points (both ends of both rails)", of(n, "frame_hook") == 4, std::to_string(of(n, "frame_hook")));
    ctx.check("a jack pad at every wheel", of(n, "frame_jackpad") == (int)c.wheels.size(), std::to_string(of(n, "frame_jackpad")));
    ctx.check("body mounts", of(n, "frame_bodymount") >= 4, std::to_string(of(n, "frame_bodymount")));
    ctx.check("a skid plate if it has a transfer case", of(n, "frame_skid") == (spec.transfer != XFER_NONE ? 1 : 0), std::to_string(of(n, "frame_skid")));
    int leaf = 0;
    for (const Wheel& wh : c.wheels) leaf += wh.shackle >= 0;
    ctx.check("a shackle mount at every leaf's rear eye", of(n, "frame_shackle") == leaf, std::to_string(of(n, "frame_shackle")) + " for " + std::to_string(leaf));
    ctx.check("hangers, towers and stays on the suspension", of(n, "frame_hanger") + of(n, "frame_tower") >= (int)c.wheels.size(), std::to_string(of(n, "frame_hanger") + of(n, "frame_tower")));
    ctx.check("from far off: the rails, crossmembers, recovery points and skid plate, no more",
              of(count(big), "frame_rail") == stretches && of(count(big), "frame_jackpad") == 0 && of(count(big), "frame_hanger") == 0 && big.size() < all.size(), std::to_string(big.size()) + " of " + std::to_string(all.size()));
    // The rails: the section the truck has, and its own height and width.
    const std::string id = spec.id;
    int channel = 0, boxed = 0, wrongSize = 0;
    for (const Placed& p : all) {
        if (p.id != "frame_rail") continue;
        (p.variant == 1 ? channel : boxed)++;
        double h = 0, w = 0;
        for (const auto& kv : p.set) {
            if (kv.first == "h") h = kv.second;
            if (kv.first == "w") w = kv.second;
        }
        wrongSize += std::fabs(h - spec.railHeight) > 1e-9 || std::fabs(w - spec.railWidth) > 1e-9;
    }
    ctx.check("the rails are as deep and wide as the spec's", wrongSize == 0, std::to_string(wrongSize));
    if (id == "blazer") ctx.check("the K5's rails are riveted C-channel all along", boxed == 0 && channel == stretches, std::to_string(channel) + " channel, " + std::to_string(boxed) + " boxed");
    if (id == "bronco") ctx.check("the Bronco's rails are boxed all along", channel == 0 && boxed == stretches, std::to_string(channel) + " channel, " + std::to_string(boxed) + " boxed");
    if (id == "4runner" || id == "tacoma") ctx.check("boxed up front, an open C-channel tail", boxed > 0 && channel > 0 && boxed > channel / 2, std::to_string(channel) + " channel, " + std::to_string(boxed) + " boxed");
    // Everything sits within the car's box (a little over), is a real model, and is square-turned.
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
        const mk::ModelDef* d = mk::findModel(p.id);
        const bool real = d && p.variant >= 0 && p.variant < (int)d->variants.size() && p.oriented && p.bare && std::isfinite(p.p.x + p.p.y + p.p.z) && p.scale.x > 0 &&
                          p.scale.y > 0 && p.scale.z > 0 && p.scale.x < 6 && p.scale.y < 6 && p.scale.z < 6 && std::fabs(len(p.ax[0]) - 1) < 1e-6 &&
                          std::fabs(len(p.ax[1]) - 1) < 1e-6 && std::fabs(dot(p.ax[0], p.ax[1])) < 1e-6 &&
                          std::fabs(dot(p.ax[2], cross(p.ax[0], p.ax[1])) - 1) < 1e-6;
        if (!real) bad++, where = p.id;
    }
    ctx.check("every piece is inside the car's box", out == 0, std::to_string(out) + " outside, e.g. " + where);
    ctx.check("every piece is a real design, finite and square-turned", bad == 0, std::to_string(bad) + " not, e.g. " + where);
    // The pads are where the jack goes.
    int onPoint = 0;
    for (const Placed& p : all)
        if (p.id == "frame_jackpad")
            for (int k = 0; k < (int)c.wheels.size(); k++) onPoint += len(p.p - c.jackPoint(k)) < 0.005;
    ctx.check("each jack pad's underside is the wheel's jack point", onPoint == (int)c.wheels.size(), std::to_string(onPoint));
}

VEHICLE_SCENARIO(frameFollowsNodes, "carframe/follows", "The frame's models follow its nodes: shove one rail station and the rail sections either side of it move with it, the rest don't") {
    Car& c = ctx.add(spec, -100, -50, 0);
    ctx.run(1.0);
    if (c.ladder.empty()) return;
    std::vector<Placed> before, after;
    carFrameModels(c, before, PARTS_BIG);
    const int st = c.ladderStations / 2;
    const V3 shove(0.0, 0.0, 0.08);
    for (int k = 0; k < 8; k++) c.nodes[c.ladder[st * 8 + k]].p += shove;
    carFrameModels(c, after, PARTS_BIG);
    ctx.check("the same pieces", before.size() == after.size(), std::to_string(before.size()) + " then " + std::to_string(after.size()));
    if (before.size() != after.size()) return;
    int moved = 0, still = 0, rails = 0;
    // (the rails come first, side by side: stretch s of the left rail is index s, of the right rail index stretches + s)
    const int stretches = c.ladderStations - 1;
    for (int i = 0; i < 2 * stretches; i++) {
        if (before[i].id != "frame_rail") continue;
        rails++;
        const int s = i % stretches;
        const double d = len(after[i].p - before[i].p);
        if (s == st - 1 || s == st) moved += d > 0.02;
        else still += d < 1e-6;
    }
    ctx.check("both rails' sections beside the shoved station moved with it", moved == 4, std::to_string(moved) + " of 4");
    ctx.check("the other sections stayed", still == rails - 4, std::to_string(still) + " of " + std::to_string(rails - 4));
}

VEHICLE_SCENARIO(frameJackLift, "carframe/jack-lift", "A jack worked up under a frame's jack pad lifts that corner of the body (the pad rises with the jack, the other rail stays), and the wheel comes off the ground") {
    Car& c = ctx.add(spec, -100, -50, 0);
    c.handbrake = 1;
    ctx.run(1.5);
    if (c.ladder.empty()) return;
    const World& w = ctx.sim.world;
    const int k = 0;
    const V3 padBefore = c.jackPoint(k);
    V3 otherBefore;
    for (int j = 0; j < (int)c.wheels.size(); j++)
        if (c.restLocal[c.wheels[j].inner].z * c.restLocal[c.wheels[k].inner].z < 0 && c.wheels[j].front == c.wheels[k].front) otherBefore = c.jackPoint(j);
    c.placeJack(w, k);
    ctx.runUntil(8, [&] { return Interact::cornerLifted(ctx.sim, c, k); }, [&] {
        if (!Interact::cornerLifted(ctx.sim, c, k)) c.jackHeight = std::min(Interact::kJackMax, c.jackHeight + 0.14 * Ctx::kFrame);
        c.wake();
    });
    ctx.run(1.0);
    const V3 padAfter = c.jackPoint(k);
    ctx.check("the pad has risen", padAfter.y - padBefore.y > 0.1, std::to_string(padAfter.y - padBefore.y) + " m");
    ctx.check("held up at the saddle's height", std::fabs(padAfter.y - (c.jackFoot.y + c.jackBase + c.jackHeight)) < 0.03,
              std::to_string(padAfter.y) + " against " + std::to_string(c.jackFoot.y + c.jackBase + c.jackHeight));
    ctx.check("the wheel is off the ground", Interact::cornerLifted(ctx.sim, c, k));
    const Placed jack = Interact::jackModel(ctx.sim, c, k);
    ctx.check("the jack's foot is on the ground under the pad", std::fabs(jack.p.x - padAfter.x) < 0.05 && std::fabs(jack.p.z - padAfter.z) < 0.05, std::to_string(len(V3(jack.p.x - padAfter.x, 0, jack.p.z - padAfter.z))) + " m off");
    for (int j = 0; j < (int)c.wheels.size(); j++)
        if (c.restLocal[c.wheels[j].inner].z * c.restLocal[c.wheels[k].inner].z < 0 && c.wheels[j].front == c.wheels[k].front)
            ctx.check("the other rail's pad rises less", c.jackPoint(j).y - otherBefore.y < (padAfter.y - padBefore.y) * 0.8, std::to_string(c.jackPoint(j).y - otherBefore.y) + " against " + std::to_string(padAfter.y - padBefore.y));
    std::printf("  %s corner %d: the jack went up %.2f m (saddle %.2f m over the ground, reach %.2f m)\n", spec.id, k, c.jackHeight, c.jackBase + c.jackHeight, c.jackReach(k));
}
