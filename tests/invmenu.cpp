// The inventory menu (src/invmenu.*): E at the back opens it, every kind in the back gets a card with a model to draw, a
// description and a count, it lays out readably at 720p and 1080p, and what it takes and puts back is what the bed's
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

SCENARIO(invmenuLayout, "invmenu/layout", "The menu lays out inside a 1280x720 and a 1920x1080 window with a full load: panel on screen, cards in the panel and clear of each other, the icons big enough to see and the names whole-number text that fits its card; the mouse finds each card and the keys reach every one of them") {
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
        c.setGate(true);
        ctx.run(2.5);
        m.refresh(ctx.sim, st, wk);
        ctx.check(heldKind ? "holding: PUT BACK is offered and chosen first" : "empty-handed: the first card is chosen", heldKind ? m.holding && m.sel == INV_PUT_BACK : !m.holding && m.sel == 0);
        ctx.check("the open gate can be shut from the menu", m.canShut && m.gate == "TAILGATE");
        const int sizes[2][2] = {{1280, 720}, {1920, 1080}};
        for (const auto& wh : sizes) {
            const int W = wh[0], H = wh[1];
            const float u = std::max(0.75f, H / 720.0f);
            const std::string tag = std::to_string(W) + "x" + std::to_string(H) + (heldKind ? " holding" : "") + ": ";
            const InvLayout L = m.layout(W, H, u);
            ctx.check(tag + "the panel is on screen", L.panel.x >= 0 && L.panel.y >= 0 && L.panel.x + L.panel.w <= W && L.panel.y + L.panel.h <= H, std::to_string(L.panel.h) + " high");
            int outside = 0, clash = 0, tiny = 0, wide = 0;
            for (size_t i = 0; i < L.cards.size(); i++) {
                const InvRect& a = L.cards[i];
                outside += !(a.x >= L.panel.x && a.y >= L.panel.y && a.x + a.w <= L.panel.x + L.panel.w && a.y + a.h <= L.panel.y + L.panel.h);
                for (size_t j = i + 1; j < L.cards.size(); j++) clash += overlap(a, L.cards[j]);
                tiny += !(L.iconArea.h * 1.0f >= 88 * u * 0.9f);
                wide += (float)m.slots[i].name.size() * 6 * L.nameScale > a.w;
                // the mouse finds it
                if (m.hit(L, a.cx(), a.cy()) != (int)i) clash++;
            }
            ctx.check(tag + "every card is inside the panel, clear of the others, and found by the mouse", outside == 0 && clash == 0);
            ctx.check(tag + "icons at least " + std::to_string((int)(88 * u * 0.9f)) + " px", tiny == 0, std::to_string(L.iconArea.h));
            ctx.check(tag + "names are whole-number text that fits the card, at least double size", wide == 0 && L.nameScale >= (int)std::floor(2 * u * 0.75), "scale " + std::to_string(L.nameScale));
            ctx.check(tag + "description, keys and buttons are inside the panel", L.desc.y + L.desc.h <= L.panel.y + L.panel.h && L.foot.y + L.foot.h <= L.panel.y + L.panel.h && L.shut.x + L.shut.w <= L.panel.x + L.panel.w && (!m.holding || L.putBack.x >= L.panel.x));
            // The keys reach every card.
            std::set<int> reached = {m.sel};
            std::vector<int> todo = {m.sel};
            while (!todo.empty()) {
                const int at = todo.back();
                todo.pop_back();
                for (int d = 0; d < 4; d++) {
                    InvMenu k = m;
                    k.sel = at;
                    k.move(L, d == 0 ? 1 : d == 1 ? -1 : 0, d == 2 ? 1 : d == 3 ? -1 : 0);
                    if (reached.insert(k.sel).second) todo.push_back(k.sel);
                }
            }
            int cards = 0;
            for (int id : reached) cards += id >= 0;
            ctx.check(tag + "the keys reach every card", cards == (int)m.slots.size(), std::to_string(cards) + " of " + std::to_string(m.slots.size()));
        }
    }
}

SCENARIO(invmenuTake, "invmenu/take", "Choosing a card takes that thing into your hands exactly as lifting it out of the bed does (a tool, part or anchor in the hand, the jack held, a chain off the stack); choosing another swaps; PUT BACK and choosing what you hold both put it back and the counts are what they were; E at the back asks for the menu") {
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
    // E at the shut back: the gate opens, the menu is asked for, once.
    const V3 eye = wk.p + V3(0, 1.62, 0), gate = trunk::gateAt(c);
    ia.kit = trunk::reachKit(ia.own.kit, false, wk.hand, wk.holdingJack);
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("E at the shut tailgate: the menu is asked for and the gate swings", ia.openedInventory == ci && c.gate.dir == 1);
    ia.openedInventory = -1;
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("...and not again while E is held", ia.openedInventory < 0);
    ia.update(ctx.sim, wk, eye, gate - eye, false, Ctx::kFrame);
    ctx.run(2.0);
    ia.kit = trunk::reachKit(ia.own.kit, trunk::kitInReach(ctx.sim, wk.p), wk.hand, wk.holdingJack);
    ia.update(ctx.sim, wk, eye, gate - eye, true, Ctx::kFrame);
    ctx.check("E at the open tailgate asks for the menu too", ia.openedInventory == ci);
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
        ctx.check(tag + "the menu now offers to put it back, and marks the card", m.holding && m.slots[i].inHand && !m.slots[i].takable);
        ctx.check(tag + "one fewer in the back", snapshot(c, ctx.sim, ia.own, wk) != base);
        if (i % 2) {  // put back by choosing the card of the thing in hand
            m.choose(ia, ctx.sim, wk, ia.own, i, closed);
        } else {  // put back by the PUT BACK button
            m.choose(ia, ctx.sim, wk, ia.own, INV_PUT_BACK, closed);
        }
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
    m.choose(ia, ctx.sim, wk, ia.own, INV_PUT_BACK, closed);
    ctx.check("and back", !wk.hand.any() && snapshot(c, ctx.sim, ia.own, wk) == base);
    // A jack under the truck is a card that can't be taken, marked; a thing that's out working shows it.
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
    ctx.check("a chain rigged on a tire: the chains card says so", m.slots[idx({TK_CHAINS, 0})].state == "1 ON TIRES");
    ctx.sim.setAnchors.push_back(SetAnchor{V3(0, 0, 0), GEAR_STAKE, 0});
    m.refresh(ctx.sim, ia.own, wk);
    ctx.check("a stake set in the ground: the stakes card says so", m.slots[idx({TK_GEAR, GEAR_STAKE})].state == "1 SET OUT");
    // Shut from the menu.
    m.choose(ia, ctx.sim, wk, ia.own, INV_SHUT, closed);
    ctx.check("SHUT: the gate closes and the menu is done", closed && c.gate.dir == -1);
    // Shut: nothing can be taken.
    ctx.run(3.0);
    ctx.check("shut: the back is closed to the menu", !trunk::backOpen(c) && ia.takeFromBack(ctx.sim, wk, ci, ia.own, {TK_TOOL, TOOL_HAMMER}).find("OPEN") != std::string::npos);
}
