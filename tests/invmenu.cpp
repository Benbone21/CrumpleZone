// The inventory menu (src/invmenu.*): E at the back opens it, every kind in the back gets a slot in a compact five-column grid with a model to draw, a
// description and a count, it lays out small and readable at 720p and 1080p, and what it takes and puts back is what the bed's
// own take was (Interact::takeFromBack / putBackInBack).
#include <cmath>
#include <set>

#include "../src/interact.h"
#include "../src/invmenu.h"
#include "../src/modelkit.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec* byId(const char* id) {
    for (const VehicleSpec& v : vehicleCatalog())
        if (std::string(v.id) == id) return &v;
    return nullptr;
}

TrunkStock fullStock(int n) {
    TrunkStock s;
    s.kit.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) s.kit.tools[t] = true;
    for (int p = 0; p < PART_COUNT; p++) s.kit.parts[p] = n;
    for (int g = 0; g < GEAR_COUNT; g++) s.gear.n[g] = n;
    return s;
}

void standBehind(const World& w, const Car& c, Walker& wk, double back) {
    const Frame f = c.frame();
    const V3 fl = norm(V3(f.fwd.x, 0, f.fwd.z));
    const V3 q = f.com - fl * (c.spec->length * 0.5 + back);
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

std::string snapshot(const Car& c, const Sim& sim, const TrunkStock& own, const Walker& wk) {
    std::string s;
    for (const InvSlot& x : InvMenu::build(sim, sim.player, own, wk)) s += x.name + "=" + std::to_string(x.left) + (x.inHand ? "*" : "") + ";";
    return s + "chains=" + std::to_string(c.chains);
}

bool overlap(const InvRect& a, const InvRect& b) { return a.x < b.x + b.w - 0.01f && b.x < a.x + a.w - 0.01f && a.y < b.y + b.h - 0.01f && b.y < a.y + a.h - 0.01f; }

}  // namespace

SCENARIO(invmenuSlots, "invmenu/slots", "A full load (career and sandbox, SUVs, a pickup and a truck with no gate) is one card per kind in the menu: a model that builds to a real mesh for its icon, a name, a description saying what it's for, a count (ENDLESS in the sandbox); the jack, tools, parts and recovery gear in that order") {
    const char* ids[] = {"blazer", "bronco", "4runner", "tacoma", "jesko"};
    TrunkStock endless;
    endless.kit.unlimited = true;
    for (int i = 0; i < 5; i++) {
        Car& c = ctx.add(*byId(ids[i]), -140, -50 + 14 * i, 0);
        ctx.focus(c);
        c.chains = 3;
        c.setJack(true);
        ctx.run(0.6);
        Walker wk;
        wk.active = true;
        const std::string tag = std::string(ids[i]) + ": ";
        for (int sandbox = 0; sandbox < 2; sandbox++) {
            const TrunkStock st = sandbox ? endless : fullStock(3);
            const std::string t2 = tag + (sandbox ? "sandbox: " : "career: ");
            const std::vector<InvSlot> slots = InvMenu::build(ctx.sim, ctx.sim.player, st, wk);
            const std::vector<TrunkItem> whole = trunk::wholeLoad(c, st, true, TrunkRef(), false, false);
            ctx.check(t2 + "a card per kind in the back", slots.size() == whole.size() && (int)slots.size() == 1 + 1 + (TOOL_COUNT - 1) + PART_COUNT + GEAR_COUNT, std::to_string(slots.size()));
            int noIcon = 0, noName = 0, noDesc = 0, noCount = 0, badOrder = 0, dup = 0;
            std::string which;
            std::set<std::string> seen;
            int lastGroup = 0;
            for (const InvSlot& s : slots) {
                const mk::Mesh& m = trunk::itemMesh(s.model);
                if (m.triangles() < 8) noIcon++, which += " " + s.name;
                if (s.name.empty()) noName++;
                if (s.desc.size() < 20 || s.desc.find(':') == std::string::npos || s.desc.rfind(s.name.substr(0, 4), 0) != 0) noDesc++, which += " " + s.name + "(desc)";
                if (s.left < 1 && !s.endless) noCount++;
                if (s.group < lastGroup) badOrder++;
                lastGroup = s.group;
                dup += !seen.insert(s.name).second;
            }
            ctx.check(t2 + "every card's model builds to a mesh to draw as its icon", noIcon == 0, which);
            ctx.check(t2 + "every card has a name and a description that starts with it", noName == 0 && noDesc == 0, which);
            ctx.check(t2 + "every card has a count (or is endless); none twice", noCount == 0 && dup == 0);
            ctx.check(t2 + "tools, then parts, then recovery gear", badOrder == 0 && !slots.empty() && slots[0].ref.kind == TK_JACK);
            if (sandbox) {
                int endlessParts = 0;
                for (const InvSlot& s : slots) endlessParts += s.ref.kind == TK_PART && s.endless;
                ctx.check(t2 + "the parts are endless", endlessParts == PART_COUNT);
            }
        }
    }
}

SCENARIO(invmenuLayout, "invmenu/layout", "The menu is a compact grid, five slots across and as many rows as the load needs, with the gate left shut: inside a 1280x720 and a 1920x1080 window with a full load the panel is on screen and well under a quarter of it, the slots are small squares (64-80 px at 1080p), in the panel and clear of each other, the name and description fit the strip; the mouse finds each slot and the keys reach every one of them, and the held thing's slot is the one chosen first") {
    Car& c = ctx.add(*byId("blazer"), -140, -50, 0);
    ctx.focus(c);
    c.chains = 3;
    c.setJack(true);
    ctx.run(0.5);
    Walker wk;
    wk.active = true;
    const TrunkStock st = fullStock(3);
    for (int heldKind = 0; heldKind < 2; heldKind++) {
        if (heldKind) wk.hand = {TK_TOOL, TOOL_HAMMER};
        InvMenu m;
        m.show(ctx.sim.player);
        m.refresh(ctx.sim, st, wk);
        ctx.run(0.5);
        ctx.check("the gate stays shut", c.gateShut() && c.gate.dir == 0);
        ctx.check(heldKind ? "holding: the hammer's slot is chosen first" : "empty-handed: the first slot is chosen", heldKind ? m.holding && m.slots[m.sel].inHand && m.slots[m.sel].ref == TrunkRef{TK_TOOL, TOOL_HAMMER} : !m.holding && m.sel == 0);
        const int sizes[2][2] = {{1280, 720}, {1920, 1080}};
        for (const auto& wh : sizes) {
            const int W = wh[0], H = wh[1];
            const float u = std::max(0.75f, H / 720.0f);
            const std::string tag = std::to_string(W) + "x" + std::to_string(H) + (heldKind ? " holding" : "") + ": ";
            for (int pad = 0; pad < 2; pad++) {
                const InvLayout L = m.layout(W, H, u, pad);
                const std::string t2 = tag + (pad ? "pad: " : "");
                ctx.check(t2 + "the panel is on screen", L.panel.x >= 0 && L.panel.y >= 0 && L.panel.x + L.panel.w <= W && L.panel.y + L.panel.h <= H, std::to_string(L.panel.h) + " high");
                ctx.metric(t2 + "the panel's share of the screen", L.panel.w * L.panel.h / ((float)W * H), "", 0.02, 0.2);
                ctx.check(t2 + "desc and keys are inside the panel", L.desc.y + L.desc.h <= L.panel.y + L.panel.h && L.foot.y + L.foot.h <= L.panel.y + L.panel.h && L.desc.y >= L.cards.back().y + L.slot);
                int outside = 0, clash = 0, notSquare = 0, wide = 0, longestDesc = 0;
                std::set<float> cols;
                for (size_t i = 0; i < L.cards.size(); i++) {
                    const InvRect& a = L.cards[i];
                    outside += !(a.x >= L.panel.x && a.y >= L.panel.y && a.x + a.w <= L.panel.x + L.panel.w && a.y + a.h <= L.panel.y + L.panel.h);
                    for (size_t j = i + 1; j < L.cards.size(); j++) clash += overlap(a, L.cards[j]);
                    notSquare += std::fabs(a.w - a.h) > 0.01f || std::fabs(a.w - L.slot) > 0.01f;
                    wide += (float)m.slots[i].name.size() * 6 * L.nameScale > L.desc.w;
                    longestDesc = std::max(longestDesc, (int)invWrap(InvMenu::blurb(m.slots[i]), (float)L.bodyScale, L.desc.w).size());
                    cols.insert(a.x);
                    if (m.hit(L, a.cx(), a.cy()) != (int)i) clash++;
                }
                ctx.check(t2 + "every slot is a square inside the panel, clear of the others, and found by the mouse", outside == 0 && clash == 0 && notSquare == 0);
                ctx.check(t2 + "five columns, rows to fit", (int)cols.size() == kInvCols && L.cards.back().y > L.cards.front().y && (int)((L.cards.back().y - L.cards.front().y) / (L.slot + 1) + 1.01f) == ((int)m.slots.size() + kInvCols - 1) / kInvCols, std::to_string(cols.size()));
                ctx.metric(t2 + "slot side in px at 1080p-equivalent", L.slot / u * 1.5f, "px", 64, 80);
                ctx.check(t2 + "names and descriptions fit the strip", wide == 0 && longestDesc <= L.descLines);
                ctx.check(t2 + "text is whole-number scale, readable", L.bodyScale >= 1 && L.nameScale >= 1 && L.titleScale >= 2 * (int)std::floor(u * 0.75));
            }
            const InvLayout L = m.layout(W, H, u);
            // The keys reach every slot.
            std::set<int> reached = {m.sel};
            std::vector<int> todo = {m.sel};
            while (!todo.empty()) {
                const int at = todo.back();
                todo.pop_back();
                for (int d = 0; d < 4; d++) {
                    InvMenu k = m;
                    k.sel = at;
                    k.move(d == 0 ? 1 : d == 1 ? -1 : 0, d == 2 ? 1 : d == 3 ? -1 : 0);
                    if (reached.insert(k.sel).second) todo.push_back(k.sel);
                }
            }
            ctx.check(tag + "the keys reach every slot", reached.size() == m.slots.size() && !reached.count(INV_NONE), std::to_string(reached.size()) + " of " + std::to_string(m.slots.size()));
            // Up and down stay in their column, wrap round the grid and never land on nothing.
            InvMenu k = m;
            k.sel = 0;
            k.move(0, -1);
            ctx.check(tag + "up from the top wraps to the bottom, in a real slot", k.sel > 0 && k.sel < (int)k.slots.size() && k.sel / kInvCols == ((int)k.slots.size() - 1) / kInvCols - (k.sel % kInvCols > ((int)k.slots.size() - 1) % kInvCols ? 1 : 0));
            k.sel = 14;
            k.move(0, 1);
            ctx.check(tag + "down into the short last row lands on its end", k.sel == (int)k.slots.size() - 1, std::to_string(k.sel));
            (void)L;
        }
    }
}

SCENARIO(invmenuTake, "invmenu/take", "Choosing a slot takes that thing into your hands exactly as lifting it out of the bed does (a tool, part or anchor in the hand, the jack held, a chain off the stack); choosing another swaps; PUT BACK and choosing what you hold both put it back and the counts are what they were; E at the back asks for the menu with the gate shut") {
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
    // E at the shut back: the menu is asked for, once, and the gate stays shut.
    const V3 eye = wk.p + V3(0, 1.62, 0), gate = trunk::gateAt(c);
    ia.kit = trunk::reachKit(ia.own.kit, false, wk.hand, wk.holdingJack);
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("E at the shut tailgate: the menu is asked for and the gate stays shut", ia.openedInventory == ci && c.gateShut() && c.gate.dir == 0);
    ia.openedInventory = -1;
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("...and not again while E is held", ia.openedInventory < 0);
    ia.update(ctx.sim, wk, eye, gate - eye, false, Ctx::kFrame);
    ctx.run(2.0);
    ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("E again asks for the menu again", ia.openedInventory == ci && c.gateShut());
    ia.openedInventory = -1;
    ia.update(ctx.sim, wk, eye, gate - eye, false, Ctx::kFrame);

    InvMenu m;
    m.show(ci);
    m.refresh(ctx.sim, ia.own, wk);
    const std::string base = snapshot(c, ctx.sim, ia.own, wk);
    ctx.check("empty-handed: no PUT BACK", !m.holding);
    bool closed = false;
    for (int i = 0; i < (int)m.slots.size(); i++) {
        const InvSlot s = m.slots[i];
        const std::string tag = s.name + ": ";
        const std::string said = m.choose(ia, ctx.sim, wk, ia.own, i, closed);
        const bool inHand = s.ref.kind == TK_JACK ? wk.holdingJack : s.ref.kind == TK_CHAINS ? wk.holdingChain : wk.hand == s.ref;
        const bool onlyThat = (s.ref.kind == TK_JACK) == wk.holdingJack && (s.ref.kind == TK_CHAINS) == wk.holdingChain && ((s.ref.kind == TK_JACK || s.ref.kind == TK_CHAINS) == !wk.hand.any());
        ctx.check(tag + "in your hands and nothing else, as the bed's take leaves it", inHand && onlyThat && said.rfind("GOT", 0) == 0 && closed, said);
        m.refresh(ctx.sim, ia.own, wk);
        ctx.check(tag + "the menu now offers to put it back, and marks the slot", m.holding && m.slots[i].inHand && !m.slots[i].takable);
        ctx.check(tag + "one fewer in the back", snapshot(c, ctx.sim, ia.own, wk) != base);
        m.choose(ia, ctx.sim, wk, ia.own, i, closed);  // (put back by choosing the slot of the thing in hand)
        m.refresh(ctx.sim, ia.own, wk);
        ctx.check(tag + "put back: hands empty, the counts what they were", !wk.hand.any() && !wk.holdingJack && !wk.holdingChain && snapshot(c, ctx.sim, ia.own, wk) == base && closed && !m.holding, snapshot(c, ctx.sim, ia.own, wk));
    }
    // Swapping: the hammer in hand, then the jack, then the stakes.
    const auto idx = [&](TrunkRef r) {
        for (int i = 0; i < (int)m.slots.size(); i++)
            if (m.slots[i].ref == r) return i;
        return -1;
    };
    m.choose(ia, ctx.sim, wk, ia.own, idx({TK_TOOL, TOOL_HAMMER}), closed);
    m.refresh(ctx.sim, ia.own, wk);
    m.choose(ia, ctx.sim, wk, ia.own, idx({TK_JACK, 0}), closed);
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("hammer then jack: the jack's in hand, the hammer's back in the truck", wk.holdingJack && !wk.hand.any() && m.slots[idx({TK_TOOL, TOOL_HAMMER})].takable);
    m.choose(ia, ctx.sim, wk, ia.own, idx({TK_CHAINS, 0}), closed);
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("jack then a chain: the chain's in hand, the jack's back", wk.holdingChain && !wk.holdingJack && c.chains == 2);
    m.choose(ia, ctx.sim, wk, ia.own, idx({TK_GEAR, GEAR_STAKE}), closed);
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("chain then a stake: the stake's in hand, the chain's back (3 again)", wk.hand == TrunkRef{TK_GEAR, GEAR_STAKE} && !wk.holdingChain && c.chains == 3);
    m.choose(ia, ctx.sim, wk, ia.own, idx({TK_GEAR, GEAR_STAKE}), closed);
    ctx.check("and back", !wk.hand.any() && snapshot(c, ctx.sim, ia.own, wk) == base);
    // A jack under the truck is a slot that can't be taken, marked; a thing that's out working shows it.
    c.placeJack(ctx.sim.world, 0);
    m.refresh(ctx.sim, ia.own, wk);
    const InvSlot& jack = m.slots[idx({TK_JACK, 0})];
    ctx.check("jack under a corner: marked, not takable", jack.state == "UNDER THE TRUCK" && !jack.takable);
    const std::string said = m.choose(ia, ctx.sim, wk, ia.own, idx({TK_JACK, 0}), closed);
    ctx.check("...and choosing it says so, keeping the menu up", !closed && !wk.holdingJack && !said.empty(), said);
    c.jackPlaced = false, c.jackWheel = -1;
    c.wheels[0].chained = true;
    c.chains = 2;
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("a chain rigged on a tire: the chains slot says so", m.slots[idx({TK_CHAINS, 0})].state == "1 ON TIRES");
    ctx.sim.setAnchors.push_back(SetAnchor{V3(0, 0, 0), GEAR_STAKE, 0});
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("a stake set in the ground: the stakes slot says so", m.slots[idx({TK_GEAR, GEAR_STAKE})].state == "1 SET OUT");
    ctx.run(2.0);
    ctx.check("through all of it the gate never moved", c.gateShut() && c.gate.dir == 0 && c.gate.share == 0);
}
