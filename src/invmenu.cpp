#include "invmenu.h"

#include <algorithm>
#include <cmath>

#include "walker.h"

namespace {

int groupOf(TrunkRef r) { return r.kind == TK_JACK || r.kind == TK_TOOL ? 0 : r.kind == TK_PART ? 1 : 2; }

const char* const kHeads[3] = {"TOOLS", "REPAIR PARTS", "RECOVERY AND ANCHOR GEAR"};

int whole(float x) { return std::max(1, (int)std::lround(x)); }

}  // namespace

std::string InvMenu::describe(TrunkRef r) {
    switch (r.kind) {
        case TK_TOOL:
            switch ((Tool)r.id) {
                case TOOL_LUG_WRENCH: return "LUG WRENCH: TAKES A WHEEL OFF AND PUTS IT BACK ON";
                case TOOL_WRENCH: return "WRENCHES: FOR TIE RODS, DRIVE SHAFTS AND SWAY BAR LINKS";
                case TOOL_RATCHET: return "SOCKET SET: FOR CV AXLES, CONTROL ARMS AND SPARK PLUGS";
                case TOOL_HAMMER: return "HAMMER: KNOCKS A CV AXLE OUT OF ITS HUB AND A TIE ROD OFF ITS KNUCKLE";
                case TOOL_PRY_BAR: return "PRY BAR: LEVERS A BENT CONTROL ARM OR SWAY BAR LINK BACK OUT";
                case TOOL_TIRE_IRONS: return "TIRE IRONS: GET A TIRE OFF ITS RIM AND BACK ON";
                default: break;
            }
            break;
        case TK_PART:
            switch ((RepairPart)r.id) {
                case PART_TIRE: return "TIRE: A NEW ONE FOR A SHREDDED OR FLAT WHEEL (NEEDS TIRE IRONS AND A LUG WRENCH)";
                case PART_CV_AXLE: return "CV AXLE: REPLACES A SNAPPED FRONT OR REAR SHAFT";
                case PART_DRIVE_SHAFT: return "DRIVE SHAFT: A NEW PROP SHAFT WHEN THE OLD ONE SHEARS OR BENDS";
                case PART_TIE_ROD: return "TIE ROD: PUTS THE STEERING STRAIGHT AFTER A BENT OR BROKEN ROD";
                case PART_CONTROL_ARM: return "CONTROL ARM: REPLACES A BENT SUSPENSION ARM OR SWAY BAR LINK";
                case PART_SPARK_PLUGS: return "SPARK PLUGS: A DRY SET FOR AN ENGINE THAT FLOODED";
                default: break;
            }
            break;
        case TK_GEAR:
            switch ((AnchorGear)r.id) {
                case GEAR_STRAP: return "TREE STRAP: GOES ROUND A TRUNK FOR THE WINCH; COMES BACK WITH THE LINE";
                case GEAR_SLING: return "CHAIN SLING: GOES ROUND A BOULDER FOR THE WINCH; COMES BACK WITH THE LINE";
                case GEAR_STAKE: return "GROUND STAKE: DRIVEN INTO OPEN GROUND FOR THE WINCH; STAYS WHERE YOU SET IT";
                case GEAR_BOLT: return "ROCK BOLT: DRILLED INTO BARE ROCK FOR THE WINCH; STAYS IN THE ROCK";
                case GEAR_ICE: return "ICE ANCHOR: SCREWED INTO SNOW OR ICE FOR THE WINCH; STAYS WHERE IT IS";
                default: break;
            }
            break;
        case TK_CHAINS: return "SNOW CHAINS: WRAP ONE ROUND A STOPPED TIRE FOR GRIP ON SNOW AND ICE";
        case TK_JACK: return "HI-LIFT JACK: LIFTS A CORNER OFF THE GROUND FOR WHEEL AND SUSPENSION WORK";
    }
    return trunk::name(r);
}

std::vector<InvSlot> InvMenu::build(const Sim& sim, int car, const TrunkStock& own, const Walker& wk) {
    std::vector<InvSlot> out;
    if (car < 0 || car >= (int)sim.cars.size()) return out;
    const Car& c = sim.cars[car];
    const std::vector<TrunkItem> items = trunk::wholeLoad(c, own, car == sim.player, wk.hand, wk.holdingJack, wk.holdingChain);
    int chained = 0;
    for (const Wheel& wh : c.wheels) chained += wh.chained;
    std::vector<int> set(GEAR_COUNT, 0);
    for (const SetAnchor& sa : sim.setAnchors) set[sa.gear]++;
    auto add = [&](TrunkRef ref, int count, bool endless, int taken) {
        InvSlot s;
        s.ref = ref, s.name = trunk::name(ref), s.model = trunk::model(ref), s.desc = describe(ref);
        s.endless = endless;
        s.left = std::max(count - taken, 0);
        s.inHand = (ref.kind == TK_JACK && wk.holdingJack) || (ref.kind == TK_CHAINS && wk.holdingChain) || (ref.kind != TK_JACK && ref.kind != TK_CHAINS && wk.hand == ref);
        s.group = groupOf(ref);
        if (s.inHand) s.state = "IN YOUR HAND";
        else if (ref.kind == TK_JACK && c.jackPlaced) s.state = "UNDER THE TRUCK";
        else if (ref.kind == TK_CHAINS && chained > 0) s.state = std::to_string(chained) + " ON TIRES";
        else if (ref.kind == TK_GEAR) {
            const int g = ref.id;
            const int rigged = anchors::usedUp((AnchorGear)g) ? 0 : std::max(0, sim.anchorStock.n[g] - sim.anchorsFree((AnchorGear)g));
            if (rigged > 0) s.state = std::to_string(rigged) + " ON A LINE", s.left = std::max(0, s.left - rigged);
            else if (set[g] > 0) s.state = std::to_string(set[g]) + " SET OUT";
        }
        s.takable = s.left > 0 && !s.inHand;
        out.push_back(s);
    };
    for (const TrunkItem& it : items) add(it.ref, it.count, it.endless, it.taken);
    // A kind with none in the back can still be out working: the chains all round the tires, the last stake in the ground.
    bool haveChains = false;
    std::vector<bool> haveGear(GEAR_COUNT, false);
    for (const InvSlot& s : out) haveChains |= s.ref.kind == TK_CHAINS, haveGear[s.ref.id] = haveGear[s.ref.id] || s.ref.kind == TK_GEAR;
    if (!haveChains && chained > 0 && car == sim.player) add({TK_CHAINS, 0}, 0, false, 0);
    for (int g = 0; g < GEAR_COUNT; g++)
        if (!haveGear[g] && set[g] > 0 && car == sim.player) add({TK_GEAR, g}, 0, false, 0);
    // Tools first (the jack leading them), then parts, then the recovery gear; each in the game's own order.
    std::stable_sort(out.begin(), out.end(), [&](const InvSlot& a, const InvSlot& b) {
        const int ra = a.group * 100 + (a.ref.kind == TK_JACK || a.ref.kind == TK_CHAINS ? 0 : 1 + a.ref.id);
        const int rb = b.group * 100 + (b.ref.kind == TK_JACK || b.ref.kind == TK_CHAINS ? 0 : 1 + b.ref.id);
        return ra < rb;
    });
    return out;
}

void InvMenu::show(int ci) {
    open = true, car = ci, sel = INV_NONE;
}

void InvMenu::refresh(const Sim& sim, const TrunkStock& own, const Walker& wk) {
    if (!open || car < 0 || car >= (int)sim.cars.size()) return;
    const Car& c = sim.cars[car];
    TrunkRef was;
    if (const InvSlot* s = chosen()) was = s->ref;
    slots = build(sim, car, own, wk);
    holding = wk.hand.any() || wk.holdingJack || wk.holdingChain;
    heldName = wk.holdingJack ? trunk::name({TK_JACK, 0}) : wk.holdingChain ? trunk::name({TK_CHAINS, 0}) : wk.hand.any() ? trunk::name(wk.hand) : "";
    heldModel = wk.holdingJack ? "jack" : wk.holdingChain ? "chain_bundle" : wk.hand.any() ? trunk::model(wk.hand) : "";
    gate = c.hasGate() ? trunk::gateName(c) : "TRUNK";
    canShut = c.hasGate() && c.gateOpen() && c.gate.dir == 0;
    if (sel >= 0) {  // (the card moved, or went: follow it)
        int at = -1;
        for (int i = 0; i < (int)slots.size(); i++)
            if (was.any() && slots[i].ref == was) at = i;
        sel = at >= 0 ? at : std::min(sel, (int)slots.size() - 1);
    }
    if (sel == INV_PUT_BACK && !holding) sel = INV_NONE;
    if (sel == INV_SHUT && !canShut) sel = INV_NONE;
    if (sel == INV_NONE) sel = holding ? INV_PUT_BACK : slots.empty() ? INV_NONE : 0;
}

InvLayout InvMenu::layout(int W, int H, float u) const {
    InvLayout L;
    L.u = u;
    L.titleScale = whole(2.2f * u);
    L.smallScale = whole(1.2f * u);
    L.bodyScale = whole(1.7f * u);
    int per[3] = {0, 0, 0};
    for (const InvSlot& s : slots) per[s.group]++;
    int cols = 5;
    for (int g = 0; g < 3; g++) cols = std::max(cols, std::min(per[g], 7));
    const float gap = 8 * u, pad = 20 * u;
    const float cardW = std::min(158 * u, ((float)W - 2 * pad - 2 * 16 * u - (cols - 1) * gap) / cols);
    size_t longest = 8;
    for (const InvSlot& s : slots) longest = std::max(longest, s.name.size());
    L.nameScale = std::max(1, (int)std::floor((cardW - 8 * u) / (6.0f * (float)longest)));
    const float iconH = cardW * 0.62f;
    const float cardH = iconH + 8.0f * L.nameScale + 14 * u;
    const float headH = 8.0f * whole(1.5f * u) + 10 * u;
    const float titleH = 8.0f * L.titleScale + 8 * u;
    const float descH = 8.0f * L.bodyScale * 2 + 16 * u;
    const float footH = 8.0f * L.bodyScale + 4 * u;
    int groups = 0;
    for (int g = 0; g < 3; g++) groups += per[g] > 0;
    const float w = cols * cardW + (cols - 1) * gap + 2 * pad;
    const float h = pad + titleH + 10 * u + groups * (headH + cardH + gap + 4 * u) + descH + 8 * u + footH + pad * 0.8f;
    L.panel = {((float)W - w) / 2, std::max(8 * u, ((float)H - h) / 2), w, h};
    const float x0 = L.panel.x + pad;
    float y = L.panel.y + pad;
    L.title = {x0, y, w - 2 * pad, titleH};
    // The buttons sit on the title row's right: SHUT, and PUT BACK to its left.
    const float bh = titleH + 4 * u, bs = 8.0f * L.bodyScale;
    float bx = L.panel.x + w - pad;
    if (canShut) {
        const float bw = (std::string("C  SHUT THE ") + gate).size() * 6.0f * L.bodyScale + 24 * u;
        bx -= bw;
        L.shut = {bx, y - 2 * u, bw, bh};
        bx -= gap;
    }
    if (holding) {
        const float bw = (std::string("PUT BACK  ") + heldName).size() * 6.0f * L.bodyScale + 24 * u + bh;
        bx -= bw;
        L.putBack = {bx, y - 2 * u, bw, bh};
    }
    (void)bs;
    y += titleH + 10 * u;
    L.cards.assign(slots.size(), InvRect());
    for (int g = 0; g < 3; g++) {
        if (!per[g]) continue;
        L.heads.push_back({kHeads[g], x0, y});
        y += headH;
        int k = 0;
        for (int i = 0; i < (int)slots.size(); i++) {
            if (slots[i].group != g) continue;
            L.cards[i] = {x0 + (k % cols) * (cardW + gap), y + (k / cols) * (cardH + gap), cardW, cardH};
            k++;
        }
        y += ((k + cols - 1) / cols) * (cardH + gap) + 4 * u;
    }
    L.iconArea = {0, 0, cardW, iconH};
    L.desc = {x0, y, w - 2 * pad, descH};
    y += descH + 8 * u;
    L.foot = {x0, y, w - 2 * pad, footH};
    return L;
}

void InvMenu::move(const InvLayout& lay, int dx, int dy) {
    struct Node {
        int id;
        InvRect r;
    };
    std::vector<Node> nodes;
    if (holding && lay.putBack.w > 0) nodes.push_back({INV_PUT_BACK, lay.putBack});
    if (canShut && lay.shut.w > 0) nodes.push_back({INV_SHUT, lay.shut});
    for (int i = 0; i < (int)slots.size() && i < (int)lay.cards.size(); i++) nodes.push_back({i, lay.cards[i]});
    if (nodes.empty()) return;
    const Node* from = nullptr;
    for (const Node& n : nodes)
        if (n.id == sel) from = &n;
    if (!from) {
        sel = nodes.front().id;
        return;
    }
    const Node* best = nullptr;
    float bestScore = 1e9f;
    for (const Node& n : nodes) {
        if (&n == from) continue;
        const float ddx = n.r.cx() - from->r.cx(), ddy = n.r.cy() - from->r.cy();
        const float along = dx != 0 ? ddx * dx : ddy * dy, across = dx != 0 ? std::fabs(ddy) : std::fabs(ddx);
        if (along < 4) continue;  // (not that way)
        const float score = along + across * 2.5f;
        if (score < bestScore) bestScore = score, best = &n;
    }
    if (best) sel = best->id;
    else if (dx != 0) {  // (off the end of a row: wrap round it)
        const Node* far = nullptr;
        float fs = -1;
        for (const Node& n : nodes) {
            if (std::fabs(n.r.cy() - from->r.cy()) > 4) continue;
            const float v = (n.r.cx() - from->r.cx()) * -dx;
            if (v > fs) fs = v, far = &n;
        }
        if (far) sel = far->id;
    }
}

int InvMenu::hit(const InvLayout& lay, float x, float y) const {
    if (holding && lay.putBack.w > 0 && lay.putBack.has(x, y)) return INV_PUT_BACK;
    if (canShut && lay.shut.w > 0 && lay.shut.has(x, y)) return INV_SHUT;
    for (int i = 0; i < (int)lay.cards.size() && i < (int)slots.size(); i++)
        if (lay.cards[i].has(x, y)) return i;
    return INV_NONE;
}

std::string InvMenu::choose(Interact& ia, Sim& sim, Walker& wk, const TrunkStock& own, int what, bool& closed) {
    closed = false;
    if (car < 0 || car >= (int)sim.cars.size()) {
        closed = true;
        return "";
    }
    if (what == INV_SHUT) {
        if (!canShut) return "";
        InteractOption o;
        o.kind = IA_CLOSE_GATE, o.car = car;
        const std::string said = ia.perform(sim, wk, o);
        closed = said.find("STOP") == std::string::npos;
        return said;
    }
    if (what == INV_PUT_BACK) {
        const std::string said = ia.putBackInBack(sim, wk, car);
        closed = !said.empty();
        return said;
    }
    if (what < 0 || what >= (int)slots.size()) return "";
    const InvSlot& s = slots[what];
    if (s.inHand) {
        const std::string said = ia.putBackInBack(sim, wk, car);
        closed = !said.empty();
        return said;
    }
    if (!s.takable) {
        if (s.ref.kind == TK_JACK && !s.state.empty()) return "THE JACK IS UNDER THE TRUCK: TAKE IT OUT FROM THERE";
        if (!s.state.empty()) return s.name + ": NONE LEFT IN THE BACK (" + s.state + ")";
        return s.name + ": NONE LEFT IN THE BACK";
    }
    const std::string said = ia.takeFromBack(sim, wk, car, own, s.ref);
    const bool got = said.rfind("GOT", 0) == 0;
    closed = got;
    return said;
}
