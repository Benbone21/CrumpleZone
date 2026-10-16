// The truck's back (src/trunk.*): the gate swinging on its hinges, what's laid out in the cargo
// area, taking things out and putting them back, and whose kit is within reach.
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

SCENARIO(trunkGate, "trunk/gate", "The back opens on its real hinges (Blazer and Bronco swing gates, 4Runner liftgate): it swings well clear of the body, holds there like a gas strut without counting as damage, rides along while driving, and latches shut again; a pickup's drawn gate folds down and up") {
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
        ctx.metric(tag + "the cargo deck slid out behind it", trunk::slide(c), "m", trunk::kSlide * 0.95, trunk::kSlide * 1.01);
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
        ctx.metric(tag + "the cargo deck slid back in", trunk::slide(c), "m", 0, 0.001);
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

SCENARIO(trunkLayout, "trunk/layout", "Everything you carry is laid out inside the cargo area of every truck: a full career kit with a stack of everything, and the sandbox's endless one; nothing overlaps, stacks are capped, and it all rides with the body") {
    const char* ids[] = {"blazer", "bronco", "4runner", "tacoma"};
    for (int i = 0; i < 4; i++) {
        const VehicleSpec* vs = byId(ids[i]);
        Car& c = ctx.add(*vs, -140, -50 + 14 * i, 0);
        ctx.run(1.0);
        c.chains = 4;
        c.setJack(true);
        const std::string tag = std::string(ids[i]) + ": ";
        trunk::Box box;
        ctx.check(tag + "has a cargo area", trunk::cargoBox(c, box));
        double minX = 1e9, maxX = -1e9;
        for (int n = 0; n < c.bodyNodes; n++) minX = std::min(minX, (double)c.restLocal[n].x), maxX = std::max(maxX, (double)c.restLocal[n].x);
        ctx.check(tag + "the cargo area is inside the body", box.lo.x >= minX && box.hi.x <= maxX && box.hi.z <= vs->width * 0.5 && box.lo.z >= -vs->width * 0.5 && box.hi.z > box.lo.z + 0.8);
        struct Case {
            const char* name;
            TrunkStock stock;
            int expectItems;
        };
        TrunkStock endless;
        endless.kit.unlimited = true;
        endless.gear = AnchorStock();
        for (const Case& cs : {Case{"a full career kit", fullStock(9), 1 + 1 + (TOOL_COUNT - 1) + PART_COUNT + GEAR_COUNT}, Case{"the sandbox's", endless, 1 + 1 + (TOOL_COUNT - 1) + PART_COUNT + GEAR_COUNT}}) {
            const std::vector<TrunkItem> items = trunk::contents(c, cs.stock, true, TrunkRef(), false);
            ctx.check(tag + cs.name + ": every kind is in the back", (int)items.size() == cs.expectItems, std::to_string(items.size()) + " of " + std::to_string(cs.expectItems));
            const std::vector<TrunkSlot> slots = trunk::layout(c, items);
            bool inside = true, capped = true;
            std::vector<int> copies(items.size(), 0);
            for (const TrunkSlot& s : slots) {
                inside &= s.lo.x >= box.lo.x - 1e-6 && s.hi.x <= box.hi.x + 1e-6 && s.lo.z >= box.lo.z - 1e-6 && s.hi.z <= box.hi.z + 1e-6 && s.lo.y >= box.lo.y - 1e-6 && s.hi.y <= box.hi.y + 1e-6;
                copies[s.item]++;
            }
            for (size_t k = 0; k < items.size(); k++) capped &= copies[k] == std::min(items[k].count, trunk::kMaxCopies);
            ctx.check(tag + cs.name + ": all of it inside the cargo area", inside);
            ctx.check(tag + cs.name + ": a stack is its count, up to " + std::to_string(trunk::kMaxCopies) + " drawn", capped);
            int overlaps = 0;
            for (size_t a = 0; a < slots.size(); a++)
                for (size_t b = a + 1; b < slots.size(); b++) {
                    if (slots[a].item == slots[b].item) continue;
                    const double ox = std::min(slots[a].hi.x, slots[b].hi.x) - std::max(slots[a].lo.x, slots[b].lo.x);
                    const double oz = std::min(slots[a].hi.z, slots[b].hi.z) - std::max(slots[a].lo.z, slots[b].lo.z);
                    if (ox > 0.002 && oz > 0.002) overlaps++;
                }
            ctx.check(tag + cs.name + ": nothing lies on anything else", overlaps == 0, std::to_string(overlaps) + " overlaps");
            // It's all model-able: each item has a model.
            bool models = true;
            for (const TrunkItem& it : items) models &= mk::findModel(it.model) != nullptr;
            ctx.check(tag + cs.name + ": every item has a model", models);
        }
        // It rides with the body: where a slot is in the truck's own frame doesn't change as it drives.
        const TrunkStock st = fullStock(2);
        const std::vector<TrunkItem> items = trunk::contents(c, st, true, TrunkRef(), false);
        const std::vector<TrunkSlot> slots = trunk::layout(c, items);
        auto local = [&](const TrunkSlot& s) {
            const Frame f = c.frame();
            V3 at, ax[3];
            trunk::place(c, f, s, at, ax);
            const V3 d = at - f.com;
            return V3(dot(d, f.fwd), dot(d, f.up), dot(d, f.left));
        };
        std::vector<V3> before;
        for (const TrunkSlot& s : slots) before.push_back(local(s));
        {  // taking something out leaves the rest where it was (the whole load is laid out, the taken thing's place left empty)
            const std::vector<TrunkItem> whole = trunk::wholeLoad(c, st, true, TrunkRef(), false);
            const std::vector<TrunkItem> withHammer = trunk::wholeLoad(c, st, true, TrunkRef{TK_TOOL, TOOL_HAMMER}, false);
            const std::vector<TrunkSlot> a = trunk::layout(c, whole), b = trunk::layout(c, withHammer);
            double moved = a.size() == b.size() ? 0 : 99;
            for (size_t k = 0; k < a.size() && k < b.size(); k++) moved = std::max(moved, len(a[k].at - b[k].at));
            ctx.metric(tag + "the rest stays put when the hammer's taken", moved, "m", 0, 1e-9);
            int hammerShown = 0;
            for (size_t i = 0; i < withHammer.size(); i++)
                if (withHammer[i].ref == TrunkRef{TK_TOOL, TOOL_HAMMER}) hammerShown = trunk::copiesShown(withHammer[i]);
            ctx.check(tag + "...and the hammer's place is empty", hammerShown == 0);
        }
        ctx.launch(c, 6);
        ctx.run(2.0, [&] { c.throttle = 0.4, c.brake = 0, c.handbrake = 0; });
        double drift = 0;
        for (size_t k = 0; k < slots.size(); k++) drift = std::max(drift, len(local(slots[k]) - before[k]));
        ctx.metric(tag + "slots stay put in the truck's frame while driving", drift, "m", 0, 0.12);
        c.throttle = 0, c.brake = 1;
        ctx.run(2.0);
    }
}

SCENARIO(trunkTake, "trunk/take", "Open the back, hold E on a tool, a part, an anchor, the jack, the chains: it's in your hands and not in the back; put back at the open back, the counts are what they were. Shut, none of it can be taken") {
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
    const V3 gate = trunk::gateAt(c);
    // Shut: the only thing to do is open it.
    ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
    InteractOption o = Interact::pick(ctx.sim, wk, wk.p + V3(0, 1.62, 0), gate - (wk.p + V3(0, 1.62, 0)), nullptr, ia.kit, &ia.own);
    ctx.check("shut: looking at the back offers to open it", o.kind == IA_OPEN_GATE, o.prompt);
    ctx.check("shut: your kit isn't within reach", !trunk::kitInReach(ctx.sim, wk.p) && !ia.kit.has(TOOL_HAMMER) && !ia.kit.hasPart(PART_CV_AXLE));
    const V3 mid = trunk::cargoCenter(c);
    o = Interact::pick(ctx.sim, wk, wk.p + V3(0, 1.62, 0), mid - (wk.p + V3(0, 1.62, 0)), nullptr, ia.kit, &ia.own);
    ctx.check("shut: nothing in the back can be taken", o.kind != IA_TAKE_ITEM && o.kind != IA_TAKE_JACK && o.kind != IA_TAKE_CHAIN, o.prompt);
    std::string said = holdOn(ctx, ia, wk, gate, 3);
    ctx.check("hold E: it opens", said == "TAILGATE OPEN", said);
    letGo(ctx, ia, wk);
    ctx.run(2.0);
    ctx.check("open: the kit is within reach (within 8 m)", c.gateOpen() && trunk::kitInReach(ctx.sim, wk.p) && trunk::reachKit(ia.own.kit, true, TrunkRef(), false).has(TOOL_HAMMER));
    Walker far = wk;
    far.p += V3(trunk::kReach + 3, 0, 0) * (c.frame().fwd.x > 0 ? -1 : 1);
    ctx.check("open: and not 11 m away", !trunk::kitInReach(ctx.sim, far.p));
    // Everything in the back, one at a time.
    const std::vector<TrunkItem> items = trunk::contents(c, ia.own, true, TrunkRef(), false);
    ctx.check("open: a full load in the back", items.size() >= 19, std::to_string(items.size()));
    for (const TrunkItem& it : items) {
        const std::string tag = it.name + ": ";
        auto snapshot = [&] {
            std::string s;
            for (const TrunkItem& x : trunk::contents(c, ia.own, true, wk.hand, wk.holdingJack, wk.holdingChain)) s += x.name + "=" + std::to_string(x.count) + ";";
            return s + "chains=" + std::to_string(c.chains);
        };
        const std::string before = snapshot();
        const Frame fr = c.frame();
        V3 at;
        const std::vector<TrunkItem> now = trunk::wholeLoad(c, ia.own, true, wk.hand, wk.holdingJack, wk.holdingChain);
        const std::vector<TrunkSlot> slots = trunk::layout(c, now);
        int idx = -1;
        for (int k = 0; k < (int)now.size(); k++)
            if (now[k].ref == it.ref) idx = k;
        if (idx < 0 || !trunk::aimAt(c, fr, now, slots, idx, at)) {
            ctx.check(tag + "found in the back", false);
            continue;
        }
        said = holdOn(ctx, ia, wk, at, 3);
        letGo(ctx, ia, wk);
        const bool inHand = it.ref.kind == TK_JACK ? wk.holdingJack : it.ref.kind == TK_CHAINS ? wk.holdingChain : wk.hand == it.ref;
        ctx.check(tag + "taken into your hands", inHand && said.rfind("GOT", 0) == 0, said);
        const std::string held = snapshot();
        ctx.check(tag + "and one fewer in the back", held != before, held);
        said = holdOn(ctx, ia, wk, trunk::cargoCenter(c), 3);
        letGo(ctx, ia, wk);
        const bool empty = !wk.hand.any() && !wk.holdingJack && !wk.holdingChain;
        ctx.check(tag + "put back at the open back", empty && snapshot() == before, said + " / " + snapshot());
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
