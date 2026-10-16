// The truck's cargo (trunk.h): the back gate swinging on its hinges, and what rides in the back.
#include "trunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "modelkit.h"

// ---- The back gate ---------------------------------------------------------------------------

namespace {

// The hinge line of a gate panel: its two live hinge nodes farthest apart (world).
bool hingeLine(const Car& c, const Panel& pn, V3& a, V3& axis) {
    int best0 = -1, best1 = -1;
    double bd = -1;
    std::vector<int> hs;
    for (size_t k = 0; k < pn.pins.size() && k < pn.bodyNodes.size(); k++) {
        const Pin& pin = c.pins[pn.pins[k]];
        if (pin.kind == PIN_HINGE && !pin.broken) hs.push_back(pn.bodyNodes[k]);
    }
    for (size_t i = 0; i < hs.size(); i++)
        for (size_t j = i + 1; j < hs.size(); j++) {
            const double d = len(c.nodes[hs[i]].p - c.nodes[hs[j]].p);
            if (d > bd) bd = d, best0 = hs[i], best1 = hs[j];
        }
    if (best0 < 0 || bd < 0.05) return false;
    a = c.nodes[best0].p;
    axis = norm(c.nodes[best1].p - a);
    return true;
}

V3 rotateAbout(const V3& r, const V3& axis, double ca, double sa) { return r * ca + cross(axis, r) * sa + axis * (dot(axis, r) * (1 - ca)); }

}  // namespace

int Car::gatePanel() const {
    for (size_t i = 0; i < panels.size(); i++)
        if (!std::strcmp(panels[i].name, "TAILGATE") || !std::strcmp(panels[i].name, "LIFTGATE")) return (int)i;
    return -1;
}

bool Car::bedGate() const {
    if (isProp() || !spec || spec->openFrame || gatePanel() >= 0) return false;
    for (const std::string& row : spec->profile)
        if (row.find('B') != std::string::npos) return true;
    return false;
}

bool Car::gateOpen() const {
    const int pi = gatePanel();
    if (pi >= 0 && panels[pi].state == 2) return true;  // (torn off: nothing to open)
    return gate.share >= 0.9 && gate.dir >= 0;
}

bool Car::gateShut() const { return gate.share <= 0.02 && gate.dir <= 0; }

bool Car::gateCarried() const {
    const int pi = gatePanel();
    return pi >= 0 && panels[pi].hand && !spec->spareFlat && gate.share > 0;
}

V3 Car::gateSwung(const V3& p, bool point) const {
    const int pi = gatePanel();
    V3 a, axis;
    if (pi < 0 || !hingeLine(*this, panels[pi], a, axis)) return p;
    const double s = gate.share, ang = panels[pi].handSign * kGateAngle * s * s * (3 - 2 * s);
    const V3 r = point ? p - a : p;
    const V3 q = rotateAbout(r, axis, std::cos(ang), std::sin(ang));
    return point ? a + q : q;
}

void Car::setGate(bool open) {
    if (!hasGate()) return;
    const int pi = gatePanel();
    if (pi >= 0) {
        Panel& pn = panels[pi];
        if (pn.state == 2) return;
        if (open && !pn.hand) {
            // Which way round the hinge line is out: the way the gate's middle goes toward the back.
            V3 a, axis;
            if (hingeLine(*this, pn, a, axis)) {
                V3 mid;
                for (int bn : pn.bodyNodes) mid += nodes[bn].p;
                mid = mid / (double)pn.bodyNodes.size();
                const V3 back = -frame().fwd, r = mid - a;
                const double plus = dot(rotateAbout(r, axis, 0, 1) - r, back), minus = dot(rotateAbout(r, axis, 0, -1) - r, back);
                pn.handSign = plus >= minus ? 1 : -1;
            } else {
                pn.handSign = 1;
            }
            pn.hand = true;
            // The latches and clips let go; the hinges stay.
            for (int pi2 : pn.pins) {
                Pin& pin = pins[pi2];
                if (pin.kind == PIN_LATCH || pin.kind == PIN_CLIP) pin.broken = true;
            }
        }
        if (!pn.hand && !open) return;  // (hanging loose after a crash: nothing to drive)
    }
    gate.dir = open ? 1 : -1;
    gate.jam = 0;
    wake();
}

void Car::gateStep(double dt) {
    if (gate.dir == 0) return;
    const double was = gate.share;
    gate.share = clampd(gate.share + gate.dir * dt / kGateTime, 0, 1);
    if (gate.dir > 0 && gate.share >= 1) gate.dir = 0;
    if (gate.dir < 0 && gate.share <= 0) {
        const int pi = gatePanel();
        if (pi < 0) {
            gate.dir = 0;
            return;
        }
        // Home: latch it again once the sheet's on the body (up against it); against something, give up.
        Panel& pn = panels[pi];
        double worst = 0;
        for (size_t k = 0; k < pn.panelNodes.size(); k++)
            if (pins[pn.pins[k]].kind != PIN_HINGE) worst = std::max(worst, len(nodes[pn.panelNodes[k]].p - nodes[pn.bodyNodes[k]].p));
        if (worst < 0.035) {
            for (int pi2 : pn.pins) {
                Pin& pin = pins[pi2];
                if (pin.kind == PIN_LATCH || pin.kind == PIN_CLIP) pin.broken = false;
            }
            pn.hand = false;
            gate.dir = 0;
        } else if ((gate.jam += dt) > 3) {
            pn.hand = false;  // (stuck: it hangs as it is)
            gate.dir = 0;
        }
    }
    if (was != gate.share) wake();
}

void Car::gateServo(double h) {
    (void)h;
    const int pi = gatePanel();
    if (pi < 0 || !panels[pi].hand) return;
    Panel& pn = panels[pi];
    V3 a, axis;
    if (!hingeLine(*this, pn, a, axis)) return;
    const double s = gate.share, ang = pn.handSign * kGateAngle * s * s * (3 - 2 * s);
    const double ca = std::cos(ang), sa = std::sin(ang);
    constexpr double w = 14, zeta = 0.8, cap = 70;  // a soft spring: a gas strut, not a bolt
    for (size_t k = 0; k < pn.panelNodes.size() && k < pn.pins.size(); k++) {
        const uint8_t kind = pins[pn.pins[k]].kind;
        if (kind == PIN_HINGE || kind == PIN_SEAL) continue;
        Node& B = nodes[pn.bodyNodes[k]];
        Node& P = nodes[pn.panelNodes[k]];
        const V3 r = B.p - a;
        const V3 target = a + rotateAbout(r, axis, ca, sa);
        V3 F = (target - P.p) * (P.m * w * w) + (B.v - P.v) * (P.m * 2 * zeta * w);
        const double l = len(F), top = P.m * cap;
        if (l > top) F = F * (top / l);
        P.f += F;
        B.f -= F;
    }
}

// ---- The cargo area and what's in it ---------------------------------------------------------

namespace trunk {

namespace {

constexpr double kSuvDepth = 0.95;    // m from the gate forward (the rear seat's back)
constexpr double kBedDepth = 1.60;    // a pickup's bed is longer: only the rear of it gets used
constexpr double kHalfWidth = 0.28;   // of the body width: between the wheel wells
constexpr double kDeck = 0.16;        // m the SUV's cargo deck stands over its sill
constexpr double kHeadroom = 0.50;    // m of cargo space over the floor (a pickup's: its walls)
constexpr double kGap = 0.035;        // m between things

// How big a thing looks in the back, against its model (the big coils and long shafts are
// shown a little smaller, or they'd fill it).
double preferredScale(const std::string& model) {
    if (model == "chain_sling") return 0.40;
    if (model == "tree_saver") return 0.70;
    if (model == "ground_stake" || model == "ice_anchor") return 0.70;
    if (model == "drive_shaft") return 0.85;
    if (model == "tie_rod") return 0.90;
    if (model == "jack") return 0.85;
    if (model == "tire_part") return 0.55;
    if (model == "spark_plug") return 2.0;
    if (model == "wrench" || model == "ratchet") return 1.3;
    return 1.0;
}

// How a model lies: which way its axes go in the rest frame, its size there.
struct Pose {
    V3 e[3];            // model front, up, right, in the rest frame
    double s = 1;
    V3 lo, hi;          // the model's bounds, turned and scaled, from its origin
};

Pose poseOf(const std::string& model, double s, bool laid, bool turned) {
    Pose p;
    p.s = s;
    if (laid) {  // the jack: stood on its foot in the model, laid on its side here (its length across the truck)
        p.e[0] = V3(1, 0, 0), p.e[1] = V3(0, 0, 1), p.e[2] = V3(0, -1, 0);
    } else if (turned) {  // long side across the truck
        p.e[0] = V3(0, 0, 1), p.e[1] = V3(0, 1, 0), p.e[2] = V3(-1, 0, 0);
    } else {
        p.e[0] = V3(1, 0, 0), p.e[1] = V3(0, 1, 0), p.e[2] = V3(0, 0, 1);
    }
    const mk::Mesh& m = mk::modelMesh(model);
    p.lo = V3(1e9, 1e9, 1e9), p.hi = V3(-1e9, -1e9, -1e9);
    for (int i = 0; i < 8; i++) {
        const V3 q((i & 1) ? m.hi.x : m.lo.x, (i & 2) ? m.hi.y : m.lo.y, (i & 4) ? m.hi.z : m.lo.z);
        const V3 v = (p.e[0] * q.x + p.e[1] * q.y + p.e[2] * q.z) * s;
        p.lo = V3(std::min(p.lo.x, v.x), std::min(p.lo.y, v.y), std::min(p.lo.z, v.z));
        p.hi = V3(std::max(p.hi.x, v.x), std::max(p.hi.y, v.y), std::max(p.hi.z, v.z));
    }
    return p;
}

}  // namespace

std::string name(TrunkRef r) {
    switch (r.kind) {
        case TK_TOOL: return toolName((Tool)r.id);
        case TK_PART: return repairPartName((RepairPart)r.id);
        case TK_GEAR: return anchors::gearName((AnchorGear)r.id);
        case TK_CHAINS: return "SNOW CHAINS";
        case TK_JACK: return "HI-LIFT JACK";
    }
    return "";
}

const char* model(TrunkRef r) {
    switch (r.kind) {
        case TK_TOOL: return toolModel((Tool)r.id);
        case TK_PART: return repairPartModel((RepairPart)r.id);
        case TK_GEAR: return anchors::gearModel((AnchorGear)r.id);
        case TK_CHAINS: return "chain_bundle";
        case TK_JACK: return "jack";
    }
    return "";
}

std::string lineFor(const TrunkItem& it) {
    std::string s = it.name;
    if (it.endless) s += " (ENDLESS)";
    else if (it.count > 1) s += " X" + std::to_string(it.count);
    return s;
}

bool cargoBox(const Car& c, Box& out) {
    if (c.isProp() || !c.hasGate() || c.restLocal.empty()) return false;
    const double hw = kHalfWidth * c.spec->width;
    const int pi = c.gatePanel();
    if (pi >= 0) {
        // The gate's own nodes: its rear face is the back of the cargo area, its bottom edge the floor.
        double x = 1e9, y = 1e9;
        for (int bn : c.panels[pi].bodyNodes) x = std::min(x, (double)c.restLocal[bn].x), y = std::min(y, (double)c.restLocal[bn].y);
        out.lo = V3(x + 0.10, y + kDeck, -hw);  // (a deck on the sill, the gate's bottom edge: the load clears it)
        out.hi = V3(x + 0.10 + kSuvDepth, y + kDeck + kHeadroom, hw);
        return true;
    }
    // A pickup: the bed, from the end of the body forward, on the floor layer ('B' is its walls).
    double x = 1e9;
    for (int i = 0; i < c.bodyNodes; i++) x = std::min(x, (double)c.restLocal[i].x);
    int row = 0;
    for (size_t k = 0; k < c.spec->profile.size(); k++)
        if (c.spec->profile[k].find('B') != std::string::npos) {
            row = (int)k;
            break;
        }
    const double floorY = c.spec->layerY[row];
    out.lo = V3(x + 0.12, floorY, -hw);
    out.hi = V3(x + 0.12 + kBedDepth, floorY + 0.40, hw);
    return true;
}

std::vector<TrunkItem> wholeLoad(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain) {
    std::vector<TrunkItem> out;
    auto add = [&](TrunkRef r, int count, bool endless, int taken) {
        if (count <= 0) return;
        TrunkItem it;
        it.ref = r;
        it.count = count;
        it.taken = endless ? 0 : taken;
        it.endless = endless;
        it.name = name(r);
        it.model = model(r);
        if (it.model.empty() || !mk::findModel(it.model)) return;
        out.push_back(it);
    };
    const bool mine = c.hasJack || (primary && own.kit.has(TOOL_JACK));
    if (mine) add({TK_JACK, 0}, 1, false, (holdingJack || c.jackPlaced) ? 1 : 0);
    if (c.chains > 0 || holdingChain) add({TK_CHAINS, 0}, c.chains + (holdingChain ? 1 : 0), false, holdingChain ? 1 : 0);
    if (!primary) return out;
    for (int t = 0; t < TOOL_COUNT; t++)
        if (t != TOOL_JACK && own.kit.has((Tool)t)) add({TK_TOOL, t}, 1, false, inHand.kind == TK_TOOL && inHand.id == t);
    for (int p = 0; p < PART_COUNT; p++) {
        if (!own.kit.hasPart((RepairPart)p)) continue;
        const bool held = inHand.kind == TK_PART && inHand.id == p;
        if (own.kit.unlimited) add({TK_PART, p}, 2, true, 0);  // (a sandbox's supply: a couple on show)
        else add({TK_PART, p}, own.kit.parts[p], false, held);
    }
    for (int g = 0; g < GEAR_COUNT; g++) add({TK_GEAR, g}, own.gear.n[g], false, inHand.kind == TK_GEAR && inHand.id == g);
    return out;
}

std::vector<TrunkItem> contents(const Car& c, const TrunkStock& own, bool primary, TrunkRef inHand, bool holdingJack, bool holdingChain) {
    std::vector<TrunkItem> out;
    for (TrunkItem it : wholeLoad(c, own, primary, inHand, holdingJack, holdingChain)) {
        it.count -= it.taken;
        it.taken = 0;
        if (it.count > 0) out.push_back(it);
    }
    return out;
}

std::vector<TrunkSlot> layout(const Car& c, const std::vector<TrunkItem>& items) {
    std::vector<TrunkSlot> slots;
    Box box;
    if (items.empty() || !cargoBox(c, box)) return slots;
    const double W = box.hi.z - box.lo.z, D = box.hi.x - box.lo.x;
    struct Entry {
        int item;
        Pose pose;
        int copies;
        double thick;  // how much each copy adds to the pile
    };
    // Packs everything at a shrink factor `f`; false if it doesn't fit the depth.
    auto pack = [&](double f, std::vector<TrunkSlot>& out) {
        out.clear();
        std::vector<Entry> es;
        for (int i = 0; i < (int)items.size(); i++) {
            const std::string& model = items[i].model;
            const mk::Mesh& m = mk::modelMesh(model);
            const bool laid = model == "jack";
            double s = preferredScale(model) * f;
            // Long side across the truck (so a row is as shallow as it can be).
            const bool turned = !laid && (m.hi.x - m.lo.x) > (m.hi.z - m.lo.z);
            Pose p = poseOf(model, s, laid, turned);
            if (p.hi.z - p.lo.z > W - 2 * kGap) {  // wider than the back: shrink it to fit
                s *= (W - 2 * kGap) / (p.hi.z - p.lo.z);
                p = poseOf(model, s, laid, turned);
            }
            Entry e;
            e.item = i, e.pose = p, e.copies = std::min(items[i].count, kMaxCopies), e.thick = std::max(0.03, p.hi.y - p.lo.y);
            es.push_back(e);
        }
        // Deepest first (a shelf each): the rows come out even.
        std::stable_sort(es.begin(), es.end(), [](const Entry& a, const Entry& b) { return (a.pose.hi.x - a.pose.lo.x) > (b.pose.hi.x - b.pose.lo.x); });
        double rowX = box.lo.x + kGap, rowDepth = 0, zc = 0;
        struct Placed {
            double x0, z0;
            const Entry* e;
        };
        std::vector<Placed> placed;
        size_t rowFirst = 0;
        auto centreRow = [&] {  // (a row of things sits in the middle of the back, not against one wall)
            const double shift = std::max(0.0, (W - zc) * 0.5);
            for (size_t k = rowFirst; k < placed.size(); k++) placed[k].z0 += shift;
            rowFirst = placed.size();
        };
        for (const Entry& e : es) {
            const double fx = e.pose.hi.x - e.pose.lo.x + kGap, fz = e.pose.hi.z - e.pose.lo.z + kGap;
            if (zc + fz > W - kGap && zc > 0) {
                centreRow();
                rowX += rowDepth, rowDepth = 0, zc = 0;
            }
            placed.push_back({rowX, box.lo.z + kGap * 0.5 + zc, &e});
            zc += fz;
            rowDepth = std::max(rowDepth, fx);
        }
        centreRow();
        const bool fits = rowX + rowDepth <= box.lo.x + D;
        for (const Placed& pl : placed) {
            const Entry& e = *pl.e;
            for (int k = 0; k < e.copies; k++) {
                TrunkSlot s;
                s.item = e.item, s.copy = k, s.scale = e.pose.s;
                for (int a = 0; a < 3; a++) s.ax[a] = e.pose.e[a];
                const V3 corner(pl.x0 + 0.012 * k, box.lo.y + 0.004 + std::min(e.thick * 0.97, 0.04) * k, pl.z0 + 0.008 * k);
                s.at = corner - e.pose.lo;
                s.lo = corner, s.hi = corner + (e.pose.hi - e.pose.lo);
                out.push_back(s);
            }
        }
        return fits;
    };
    double f = 1.0;
    while (!pack(f, slots) && f > 0.45) f *= 0.9;
    // A last resort (a lot of stock in a small back): everything's on show, and what's too far forward
    // is stood on top of the rest (a second layer).
    for (TrunkSlot& s : slots) {
        const double over = s.hi.x - box.hi.x;
        if (over > 0) {
            s.at.x -= over, s.lo.x -= over, s.hi.x -= over;
            const double lift = 0.12;
            s.at.y += lift, s.lo.y += lift, s.hi.y += lift;
        }
    }
    return slots;
}

double slide(const Car& c) {
    if (c.gatePanel() < 0) return 0;
    const double s = c.gate.share;
    return kSlide * s * s * (3 - 2 * s);
}

V3 world(const Car& c, const Frame& fr, const V3& rest) {
    int idx[4] = {-1, -1, -1, -1};
    double d2[4] = {1e18, 1e18, 1e18, 1e18};
    for (int i = 0; i < c.bodyNodes; i++) {
        const V3 d = c.restLocal[i] - rest;
        const double q = dot(d, d);
        if (q >= d2[3]) continue;
        int k = 3;
        while (k > 0 && q < d2[k - 1]) d2[k] = d2[k - 1], idx[k] = idx[k - 1], k--;
        d2[k] = q, idx[k] = i;
    }
    V3 sum;
    int n = 0;
    for (int k = 0; k < 4; k++) {
        if (idx[k] < 0) continue;
        const V3 off = rest - c.restLocal[idx[k]];
        sum += c.nodes[idx[k]].p + fr.fwd * off.x + fr.up * off.y - fr.left * off.z;
        n++;
    }
    return n ? sum / (double)n : fr.com;
}

void place(const Car& c, const Frame& fr, const TrunkSlot& s, V3& at, V3 ax[3]) {
    at = world(c, fr, s.at - V3(slide(c), 0, 0));
    for (int a = 0; a < 3; a++) ax[a] = fr.fwd * s.ax[a].x + fr.up * s.ax[a].y - fr.left * s.ax[a].z;
}

bool aimAt(const Car& c, const Frame& fr, const std::vector<TrunkItem>& items, const std::vector<TrunkSlot>& slots, int item, V3& at) {
    if (item < 0 || item >= (int)items.size()) return false;
    const int shown = copiesShown(items[item]);
    const TrunkSlot* top = nullptr;
    for (const TrunkSlot& s : slots)
        if (s.item == item && s.copy < shown && (!top || s.copy > top->copy)) top = &s;
    if (!top) return false;
    at = world(c, fr, (top->lo + top->hi) * 0.5 - V3(slide(c), 0, 0));
    return true;
}

V3 gateAt(const Car& c) {
    const int pi = c.gatePanel();
    if (pi >= 0) {  // the latch edge (the free side of a swing gate, the bottom of a liftgate): the spare on its carrier doesn't hide it
        const Panel& pn = c.panels[pi];
        V3 m;
        int n = 0;
        for (size_t k = 0; k < pn.pins.size() && k < pn.bodyNodes.size(); k++)
            if (c.pins[pn.pins[k]].kind == PIN_LATCH) m += c.nodes[pn.bodyNodes[k]].p, n++;
        if (n == 0)
            for (int bn : pn.bodyNodes) m += c.nodes[bn].p, n++;
        return m / (double)n;
    }
    const Frame fr = c.frame();
    Box b;
    if (cargoBox(c, b)) return world(c, fr, V3(b.lo.x - 0.05, b.lo.y + 0.2, 0));
    return fr.com - fr.fwd * (c.spec->length * 0.5);
}

V3 cargoCenter(const Car& c) {
    const Frame fr = c.frame();
    Box b;
    if (!cargoBox(c, b)) return fr.com - fr.fwd * (c.spec->length * 0.35);
    return world(c, fr, V3((b.lo.x + b.hi.x) * 0.5 - slide(c), b.lo.y + 0.1, 0));
}

const char* gateName(const Car& c) {
    const int pi = c.gatePanel();
    return pi >= 0 && !std::strcmp(c.panels[pi].name, "LIFTGATE") ? "LIFTGATE" : "TAILGATE";
}

bool bedGatePlace(const Car& c, const Frame& fr, V3& at, V3 ax[3], double& width) {
    Box b;
    if (!c.bedGate() || !cargoBox(c, b)) return false;
    const double s = c.gate.share, phi = (s * s * (3 - 2 * s)) * (M_PI / 2) * 0.985;
    const V3 e0(std::cos(phi), std::sin(phi), 0), e1(-std::sin(phi), std::cos(phi), 0), e2(0, 0, 1);
    at = world(c, fr, V3(b.lo.x - 0.035, b.lo.y, 0));
    const V3* e[3] = {&e0, &e1, &e2};
    for (int a = 0; a < 3; a++) ax[a] = fr.fwd * e[a]->x + fr.up * e[a]->y - fr.left * e[a]->z;
    width = (b.hi.z - b.lo.z) + 0.1;
    return true;
}

void models(const Sim& sim, int ci, const TrunkStock& own, TrunkRef inHand, bool holdingJack, bool holdingChain, std::vector<Placed>& out) {
    const Car& c = sim.cars[ci];
    if (c.isProp() || !c.hasGate()) return;
    const Frame fr = c.frame();
    V3 at, ax[3];
    double width = 1.2;
    if (bedGatePlace(c, fr, at, ax, width) && mk::findModel("bed_gate")) {
        Placed pl;
        pl.id = "bed_gate", pl.p = at, pl.oriented = true;
        for (int a = 0; a < 3; a++) pl.ax[a] = ax[a];
        pl.set = {{"width", width}, {"r", clampd(c.color.x, 0, 1)}, {"g", clampd(c.color.y, 0, 1)}, {"b", clampd(c.color.z, 0, 1)}};
        out.push_back(pl);
    }
    if (c.remote || c.ai || (!c.bedGate() && c.gate.share < 0.02)) return;  // (the back of a shut-up truck isn't on show)
    Box box;
    if (!c.bedGate() && cargoBox(c, box) && mk::findModel("cargo_mat")) {  // (a floor to lay it on: the body's own is far below the sill)
        Placed pl;
        pl.id = "cargo_mat", pl.oriented = true, pl.sunlit = true;
        pl.p = world(c, fr, V3((box.lo.x + box.hi.x) * 0.5 - slide(c), box.lo.y, 0));
        pl.ax[0] = fr.fwd, pl.ax[1] = fr.up, pl.ax[2] = -fr.left;
        pl.set = {{"depth", box.hi.x - box.lo.x + 0.1}, {"width", box.hi.z - box.lo.z + 0.1}, {"height", kDeck - 0.02}};
        out.push_back(pl);
    }
    const std::vector<TrunkItem> items = wholeLoad(c, own, ci == sim.player, inHand, holdingJack, holdingChain);
    for (const TrunkSlot& s : layout(c, items)) {
        if (s.copy >= copiesShown(items[s.item])) continue;  // (taken: its place is left empty)
        Placed pl;
        pl.id = items[s.item].model, pl.oriented = true, pl.sunlit = true;
        place(c, fr, s, pl.p, pl.ax);
        pl.scale = V3(s.scale, s.scale, s.scale);
        out.push_back(pl);
    }
}

bool kitInReach(const Sim& sim, const V3& at) {
    for (const Car& c : sim.cars) {
        if (c.remote || c.isProp() || c.ai) continue;
        if (c.hasGate()) {
            if (c.gateOpen() && len(at - gateAt(c)) < kReach) return true;
        } else if (len(at - c.frame().com) < kReach + c.spec->length * 0.5) {
            return true;
        }
    }
    return false;
}

Kit reachKit(const Kit& kit, bool inReach, TrunkRef inHand, bool holdingJack) {
    if (inReach) return kit;
    Kit k;
    k.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) {
        k.stowedTools[t] = kit.has((Tool)t);
        k.tools[t] = (inHand.kind == TK_TOOL && inHand.id == t) || (t == TOOL_JACK && holdingJack);
    }
    for (int p = 0; p < PART_COUNT; p++) {
        k.stowedParts[p] = kit.hasPart((RepairPart)p);
        k.parts[p] = inHand.kind == TK_PART && inHand.id == p ? 1 : 0;
    }
    return k;
}

}  // namespace trunk
