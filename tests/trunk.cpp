// The truck's back (src/trunk.*): E at it opens the inventory with the gate left shut and nothing laid out in
// the bed, taking things out and putting them back, and whose kit is within reach. (The gate's hinges are still
// there: nothing opens it now, but it can be driven.)
#include <cmath>
#include <set>

#include "../src/interact.h"
#include "../src/modelkit.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec* byId(const char* id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (std::string(v.id) == id) return &v;
    return nullptr;
}

// Everything: every tool, `n` of every part and anchor.
TrunkStock fullStock(int n) {
    TrunkStock s;
    s.kit.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) s.kit.tools[t] = true;
    for (int p = 0; p < PART_COUNT; p++) s.kit.parts[p] = n;
    for (int g = 0; g < GEAR_COUNT; g++) s.gear.n[g] = n;
    return s;
}

// The farthest a gate's sheet is from the body under it (m), over its free (not hinged) nodes.
double swing(const Car& c) {
    const Panel& pn = c.panels[c.gatePanel()];
    double worst = 0;
    for (size_t k = 0; k < pn.panelNodes.size(); k++)
        worst = std::max(worst, len(c.nodes[pn.panelNodes[k]].p - c.nodes[pn.bodyNodes[k]].p));
    return worst;
}

bool latched(const Car& c) {
    const Panel& pn = c.panels[c.gatePanel()];
    for (int pi : pn.pins)
        if ((c.pins[pi].kind == PIN_LATCH || c.pins[pi].kind == PIN_CLIP) && c.pins[pi].broken) return false;
    return true;
}

// Stands the walker `back` m behind the truck's middle.
void standBehind(const World& w, const Car& c, Walker& wk, double back) {
    const Frame f = c.frame();
    const V3 fl = norm(V3(f.fwd.x, 0, f.fwd.z));
    const V3 q = f.com - fl * (c.spec->length * 0.5 + back);
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

std::string holdOn(Ctx& ctx, Interact& ia, Walker& wk, const V3& at, double secs) {
    std::string said;
    for (double t = 0; said.empty() && t < secs; t += Ctx::kFrame) {
        const V3 eye = wk.p + V3(0, 1.62, 0);
        ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
        said = ia.update(ctx.sim, wk, eye, at - eye, true, Ctx::kFrame);
        ctx.run(Ctx::kFrame);
    }
    return said;
}

void letGo(Ctx& ctx, Interact& ia, Walker& wk) {
    const V3 eye = wk.p + V3(0, 1.62, 0);
    ia.update(ctx.sim, wk, eye, V3(1, 0, 0), false, Ctx::kFrame);
}

}  // namespace

SCENARIO(trunkGate, "trunk/gate", "The gate (nothing opens it in play now) still works on its real hinges (Blazer and Bronco swing gates, 4Runner liftgate): it swings well clear of the body, holds there like a gas strut without counting as damage, rides along while driving, and latches shut again; a pickup's drawn gate folds down and up") {
    const char* ids[] = {"blazer", "bronco", "4runner"};
    for (int i = 0; i < 3; i++) {
        const VehicleSpec* vs = byId(ids[i]);
        Car& c = ctx.add(*vs, -140, -50 + 14 * i, 0);
        ctx.run(1.0);
        const std::string tag = std::string(ids[i]) + ": ";
        ctx.check(tag + "has a gate panel that starts shut", c.hasGate() && c.gatePanel() >= 0 && c.gateShut() && !c.gateOpen());
        if (c.gatePanel() < 0) continue;
        c.setGate(true);
        ctx.run(0.4);
        ctx.check(tag + "swinging, not open yet", !c.gateOpen() && c.gate.dir == 1);
        ctx.run(1.6);
        ctx.check(tag + "open", c.gateOpen());
        ctx.metric(tag + "the gate's swing out from the body", swing(c), "m", 0.7, 3.0);
        const double open = swing(c);
        ctx.check(tag + "open by hand is not damage", c.panels[c.gatePanel()].state == 0 && !c.needsRepair());
        // Held there, and carried: driving off with it open.
        ctx.launch(c, 4);
        ctx.run(2.5, [&] { c.throttle = 0.3, c.brake = 0, c.handbrake = 0; });
        ctx.check(tag + "still on its hinges after driving off open", c.panels[c.gatePanel()].state != 2 && c.gateOpen());
        ctx.metric(tag + "the gate's swing after driving", swing(c), "m", open * 0.6, open * 1.4);
        c.throttle = 0, c.brake = 1;
        ctx.run(3.0);
        c.setGate(false);
        ctx.run(0.5);
        ctx.check(tag + "closing", !c.gateShut() && c.gate.dir == -1);
        ctx.run(2.5);
        ctx.check(tag + "shut again", c.gateShut(), "share " + std::to_string(c.gate.share) + " dir " + std::to_string(c.gate.dir));
        ctx.check(tag + "latched again", latched(c) && !c.panels[c.gatePanel()].hand);
        ctx.metric(tag + "the gate home against the body", swing(c), "m", 0, 0.06);
    }
    // A pickup's gate: no panel, a drawn gate and the same open and shut.
    Car& t = ctx.add(*byId("tacoma"), -140, -50 + 14 * 3, 0);
    ctx.run(0.5);
    ctx.check("tacoma: a drawn bed gate (no panel)", t.hasGate() && t.bedGate() && t.gatePanel() < 0 && t.gateShut());
    t.setGate(true);
    ctx.run(1.5);
    ctx.check("tacoma: gate down", t.gateOpen() && t.gate.share >= 1);
    V3 at, ax[3];
    double width;
    ctx.check("tacoma: the gate lies flat when open", trunk::bedGatePlace(t, t.frame(), at, ax, width) && std::fabs(dot(ax[1], t.frame().fwd)) > 0.9 * std::cos(0.02) && std::fabs(ax[1].y) < 0.2);
    t.setGate(false);
    ctx.run(1.5);
    ctx.check("tacoma: gate up", t.gateShut());
    trunk::bedGatePlace(t, t.frame(), at, ax, width);
    ctx.check("tacoma: and stands upright when shut", ax[1].y > 0.95);
}

SCENARIO(trunkTake, "trunk/take", "Your kit is within reach at your own truck's back (gate shut) and not far off; take a tool, a part, an anchor, the jack, the chains out of it: it's in your hands and not in the back; put back, the counts are what they were") {
    Car& c = ctx.add(*byId("blazer"), -120, -50, 0);
    ctx.focus(c);
    c.chains = 3;
    c.setJack(true);
    c.handbrake = 1;
    ctx.run(1.0);
    Walker wk;
    Interact ia;
    ia.trunkOn = true;
    ia.own = fullStock(3);
    standBehind(ctx.sim.world, c, wk, 1.5);
    const int ci = ctx.sim.player;
    ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
    ctx.check("at the back (the gate shut): the kit is within reach (within 8 m)", c.gateShut() && trunk::kitInReach(ctx.sim, wk.p) && ia.kit.has(TOOL_HAMMER) && ia.kit.hasPart(PART_CV_AXLE));
    Walker far = wk;
    far.p += V3(trunk::kReach + 3, 0, 0) * (c.frame().fwd.x > 0 ? -1 : 1);
    ctx.check("...and not 11 m away", !trunk::kitInReach(ctx.sim, far.p));
    std::string said;
    // Everything in the back, one at a time.
    const std::vector<TrunkItem> items = trunk::contents(c, ia.own, true, TrunkRef(), false);
    ctx.check("a full load in the back", items.size() >= 19, std::to_string(items.size()));
    for (const TrunkItem& it : items) {
        const std::string tag = it.name + ": ";
        auto snapshot = [&] {
            std::string s;
            for (const TrunkItem& x : trunk::contents(c, ia.own, true, wk.hand, wk.holdingJack, wk.holdingChain)) s += x.name + "=" + std::to_string(x.count) + ";";
            return s + "chains=" + std::to_string(c.chains);
        };
        const std::string before = snapshot();
        said = ia.takeFromBack(ctx.sim, wk, ci, ia.own, it.ref);  // (the inventory menu's choice)
        const bool inHand = it.ref.kind == TK_JACK ? wk.holdingJack : it.ref.kind == TK_CHAINS ? wk.holdingChain : wk.hand == it.ref;
        ctx.check(tag + "taken into your hands", inHand && said.rfind("GOT", 0) == 0, said);
        const std::string held = snapshot();
        ctx.check(tag + "and one fewer in the back", held != before, held);
        said = ia.putBackInBack(ctx.sim, wk, ci);
        const bool empty = !wk.hand.any() && !wk.holdingJack && !wk.holdingChain;
        ctx.check(tag + "put back", empty && snapshot() == before, said + " / " + snapshot());
    }
    ctx.check("the anchor counts didn't change", ia.own.gear.n[GEAR_STAKE] == 3);
    // Carried far from the truck, a tool still works; left in the back, it doesn't.
    const Kit away = trunk::reachKit(ia.own.kit, false, TrunkRef{TK_TOOL, TOOL_HAMMER}, false);
    ctx.check("far away: the tool in your hand is usable, the rest aren't", away.has(TOOL_HAMMER) && !away.has(TOOL_PRY_BAR) && !away.hasPart(PART_CV_AXLE));
    const Kit partAway = trunk::reachKit(ia.own.kit, false, TrunkRef{TK_PART, PART_CV_AXLE}, false);
    ctx.check("far away: the part in your hand is usable", partAway.hasPart(PART_CV_AXLE) && !partAway.hasPart(PART_TIE_ROD));
    // ...and the job says where the missing tool is.
    Fix f{Fix::CV_AXLE};
    f.part = PART_CV_AXLE;
    f.tools = {TOOL_RATCHET, TOOL_HAMMER};
    const std::string miss = missingFor(f, away, true);
    ctx.check("the job says it's in the truck", miss.find("IN THE TRUCK") != std::string::npos, miss);
    // A truck with nothing to open carries the kit with it.
    Car& j = ctx.add(*byId("jesko"), -80, -50, 0);
    ctx.run(0.5);
    Walker beside;
    beside.p = j.frame().com + V3(0, 0, 2.5);
    ctx.check("no back to open: the kit rides within reach of it", !j.hasGate() && trunk::kitInReach(ctx.sim, beside.p));
}

SCENARIO(trunkShut, "trunk/shut", "E at the back of a Blazer, a 4Runner and a Tacoma asks for the inventory and leaves the gate shut (no swing, no hinge motion at all); the bed shows nothing of the load: the renderer gets no model but a pickup's shut tailgate; a thing taken out and put back moves no gate") {
    const char* ids[] = {"blazer", "4runner", "tacoma"};
    for (int i = 0; i < 3; i++) {
        Car& c = ctx.add(*byId(ids[i]), -140, -50 + 14 * i, 0);
        ctx.focus(c);
        c.chains = 3;
        c.setJack(true);
        c.handbrake = 1;
        ctx.run(1.0);
        const std::string tag = std::string(ids[i]) + ": ";
        Walker wk;
        Interact ia;
        ia.trunkOn = true;
        ia.own = fullStock(3);
        standBehind(ctx.sim.world, c, wk, 1.5);
        const V3 eye = wk.p + V3(0, 1.62, 0), gate = trunk::gateAt(c);
        ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
        const InteractOption o = Interact::pick(ctx.sim, wk, eye, gate - eye, nullptr, ia.kit, &ia.own);
        ctx.check(tag + "the back offers the inventory, with nothing to open first", o.kind == IA_OPEN_INVENTORY && o.hold <= 0 && o.prompt == "INVENTORY" && !o.blocked, o.prompt);
        const int ci = ctx.sim.player;
        const std::string said = holdOn(ctx, ia, wk, gate, 0.3);
        ctx.check(tag + "E: the inventory is asked for", ia.openedInventory == ci, said);
        ia.openedInventory = -1;
        holdOn(ctx, ia, wk, gate, 0.5);
        ctx.check(tag + "...once: E held on doesn't ask again", ia.openedInventory < 0);
        letGo(ctx, ia, wk);
        ctx.run(2.0);
        ctx.check(tag + "the gate never moved", c.gateShut() && c.gate.dir == 0 && c.gate.share == 0, std::to_string(c.gate.share));
        // Take and put back through the menu's calls: still shut, and the bed shows nothing but a pickup's own tailgate.
        std::string t = ia.takeFromBack(ctx.sim, wk, ci, ia.own, {TK_TOOL, TOOL_HAMMER});
        ctx.check(tag + "a tool comes out of the back with the gate shut", wk.hand == TrunkRef{TK_TOOL, TOOL_HAMMER} && t.rfind("GOT", 0) == 0, t);
        std::vector<Placed> out;
        trunk::gateModels(ctx.sim, ci, out);
        bool only = true;
        for (const Placed& pl : out) only &= pl.id == "bed_gate";
        ctx.check(tag + "the renderer is handed " + (c.bedGate() ? "the tailgate and nothing else" : "nothing"), only && out.size() == (c.bedGate() ? 1u : 0u), std::to_string(out.size()));
        if (c.bedGate()) {
            V3 at, ax[3];
            double width;
            ctx.check(tag + "the pickup's tailgate stands upright, shut", trunk::bedGatePlace(c, c.frame(), at, ax, width) && ax[1].y > 0.95);
        }
        ia.putBackInBack(ctx.sim, wk, ci);
        ctx.check(tag + "put back", !wk.hand.any() && c.gateShut() && c.gate.dir == 0);
    }
}
