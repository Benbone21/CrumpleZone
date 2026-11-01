#include "invmenu.h"

#include <algorithm>
#include <cmath>

#include "gamepad.h"
#include "walker.h"

namespace {

int groupOf(TrunkRef r) { return r.kind == TK_JACK || r.kind == TK_TOOL ? 0 : r.kind == TK_PART ? 1 : 2; }

int whole(float x) { return std::max(1, (int)std::lround(x)); }

}  // namespace

std::vector<std::string> invWrap(const std::string& t, float s, float w) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find(' ', i);
        if (j == std::string::npos) j = t.size();
        const std::string word = t.substr(i, j - i);
        if (!line.empty() && (float)(line.size() + 1 + word.size()) * 6 * s > w) out.push_back(line), line.clear();
        line += (line.empty() ? "" : " ") + word;
        i = j + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::string InvMenu::blurb(const InvSlot& s) {
    const size_t colon = s.desc.find(": ");
    return colon == std::string::npos ? s.desc : s.desc.substr(colon + 2);
}

std::string InvMenu::keys(bool padLast) {
    if (padLast) return std::string("D-PAD: MOVE   ") + pad::hint("", pad::B_B) + ": CLOSE";
    return "WASD: MOVE   ESC: CLOSE";
}

std::string InvMenu::describe(TrunkRef r) {
    switch (r.kind) {
        case TK_TOOL:
            switch ((Tool)r.id) {
                case TOOL_LUG_WRENCH: return "LUG WRENCH: TAKES A WHEEL OFF AND PUTS IT BACK ON";
                case TOOL_WRENCH: return "WRENCHES: FOR TIE RODS, SWAY BAR LINKS AND AN ENGINE'S DRAIN PLUG";
                case TOOL_RATCHET: return "SOCKET SET: FOR CV AXLES, CONTROL ARMS AND SPARK PLUGS";
                case TOOL_HAMMER: return "HAMMER: KNOCKS A CV AXLE OUT OF ITS HUB AND A TIE ROD OFF ITS KNUCKLE";
                case TOOL_PRY_BAR: return "PRY BAR: LEVERS A BENT CONTROL ARM OR SWAY BAR LINK BACK OUT";
                case TOOL_TIRE_IRONS: return "TIRE IRONS: GET A TIRE OFF ITS RIM AND BACK ON";
                case TOOL_JUMP_PACK: return "JUMP PACK: HOLD E AT A DEAD BATTERY AND IT STARTS";
                case TOOL_CABLES: return "JUMPER CABLES: CLAMP THEM FROM A RUNNING VEHICLE'S BATTERY TO A DEAD ONE";
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
                case PART_ENGINE_OIL: return "ENGINE OIL: REFILLS THE SUMP AFTER THE WATER IS DRAINED FROM A FLOODED ENGINE";
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
    TrunkRef was;
    if (const InvSlot* s = chosen()) was = s->ref;
    slots = build(sim, car, own, wk);
    holding = wk.hand.any() || wk.holdingJack || wk.holdingChain;
    if (sel >= 0) {  // (the slot moved, or went: follow it)
        int at = -1;
        for (int i = 0; i < (int)slots.size(); i++)
            if (was.any() && slots[i].ref == was) at = i;
        sel = at >= 0 ? at : std::min(sel, (int)slots.size() - 1);
    }
    if (sel == INV_NONE) {  // (first up: what you hold, else the first slot)
        for (int i = 0; i < (int)slots.size() && sel == INV_NONE; i++)
            if (slots[i].inHand) sel = i;
        if (sel == INV_NONE && !slots.empty()) sel = 0;
    }
}

InvLayout InvMenu::layout(int W, int H, float u, bool padLast) const {
    InvLayout L;
    L.u = u;
    L.slot = std::max(32.0f, std::round(50 * u));
    const float gap = std::max(3.0f, std::round(4 * u)), pad = std::max(8.0f, std::round(10 * u)), sp = std::max(5.0f, std::round(7 * u));
    const float gridW = kInvCols * L.slot + (kInvCols - 1) * gap, innerW = gridW;
    L.titleScale = whole(1.7f * u);
    L.bodyScale = whole(1.0f * u);
    size_t longest = 8;
    for (const InvSlot& s : slots) longest = std::max(longest, s.name.size());
    L.nameScale = std::max(1, std::min(whole(1.7f * u), (int)std::floor(innerW / (6.0f * (float)longest))));
    L.descLines = 1;
    for (const InvSlot& s : slots) L.descLines = std::max(L.descLines, (int)invWrap(blurb(s), (float)L.bodyScale, innerW).size());
    const int rows = std::max(1, ((int)slots.size() + kInvCols - 1) / kInvCols);
    const float titleH = 8.0f * L.titleScale, bs = 8.0f * L.bodyScale, ls = bs + std::max(1.0f, std::round(2 * u));
    const float gridH = rows * L.slot + (rows - 1) * gap;
    const float descH = 8.0f * L.nameScale + std::max(2.0f, std::round(3 * u)) + L.descLines * ls + ls + 2 * std::max(3.0f, std::round(4 * u));  // (name, blurb, the state / key line)
    const int keyLines = (int)invWrap(keys(padLast), (float)L.bodyScale, innerW).size();
    const float footH = keyLines * ls;
    const float w = innerW + 2 * pad, h = pad + titleH + sp + gridH + sp + descH + sp + footH + pad;
    L.panel = {std::round(((float)W - w) / 2), std::max(4.0f, std::round(((float)H - h) / 2)), w, h};
    const float x0 = L.panel.x + pad;
    float y = L.panel.y + pad;
    L.title = {x0, y, innerW, titleH};
    y += titleH + sp;
    L.cards.assign(slots.size(), InvRect());
    for (int i = 0; i < (int)slots.size(); i++) L.cards[i] = {x0 + (i % kInvCols) * (L.slot + gap), y + (i / kInvCols) * (L.slot + gap), L.slot, L.slot};
    y += gridH + sp;
    L.desc = {x0, y, innerW, descH};
    y += descH + sp;
    L.foot = {x0, y, innerW, footH};
    return L;
}

void InvMenu::move(int dx, int dy) {
    const int n = (int)slots.size();
    if (n == 0) return;
    if (sel < 0 || sel >= n) {
        sel = 0;
        return;
    }
    const int rows = (n + kInvCols - 1) / kInvCols;
    int col = sel % kInvCols, row = sel / kInvCols;
    if (dx) sel = (sel + dx + n) % n;
    if (dy) {
        const int from = row;
        row = (row + dy + rows) % rows;
        sel = row * kInvCols + col;
        if (sel >= n) sel = dy < 0 ? sel - kInvCols : from < rows - 1 ? n - 1 : col;  // (the short last row: down lands on its last slot, from it wraps to the top; up lands on the row above)
    }
}

int InvMenu::hit(const InvLayout& lay, float x, float y) const {
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
