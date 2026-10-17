// The truck's cargo (trunk.h): the back gate swinging on its hinges, and what rides in the back.
#include "trunk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

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
constexpr double kLiner = 0.014;      // m a pickup's bed carpet stands over its floor
constexpr double kGap = 0.02;         // m between things

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
    V3 e[3];            // model front, up and right, in the rest frame
    double s = 1;
    V3 lo, hi;          // the model's bounds, turned and scaled, from its origin
};

// Lies on its side if it's tall and thin (the jack, an ice screw): its height goes across the truck.
bool lieDown(const mk::Mesh& m) { return (m.hi.y - m.lo.y) > 1.3 * std::max(m.hi.x - m.lo.x, m.hi.z - m.lo.z); }

// `turned`: a quarter turn about the vertical, long side across the truck (or along it).
Pose poseOf(const mk::Mesh& m, double s, bool laid, bool turned) {
    Pose p;
    p.s = s;
    if (laid) {  // stood on its foot in the model, laid on its side here
        p.e[0] = V3(1, 0, 0), p.e[1] = V3(0, 0, 1), p.e[2] = V3(0, -1, 0);
    } else {
        p.e[0] = V3(1, 0, 0), p.e[1] = V3(0, 1, 0), p.e[2] = V3(0, 0, 1);
    }
    if (turned)
        for (V3& e : p.e) e = V3(-e.z, e.y, e.x);
    p.lo = V3(1e9, 1e9, 1e9), p.hi = V3(-1e9, -1e9, -1e9);
    for (int i = 0; i < 8; i++) {
        const V3 q((i & 1) ? m.hi.x : m.lo.x, (i & 2) ? m.hi.y : m.lo.y, (i & 4) ? m.hi.z : m.lo.z);
        const V3 v = (p.e[0] * q.x + p.e[1] * q.y + p.e[2] * q.z) * s;
        p.lo = V3(std::min(p.lo.x, v.x), std::min(p.lo.y, v.y), std::min(p.lo.z, v.z));
        p.hi = V3(std::max(p.hi.x, v.x), std::max(p.hi.y, v.y), std::max(p.hi.z, v.z));
    }
    return p;
}

constexpr double kPileX = 0.007, kPileZ = 0.005;  // how far each copy of a stack sits off the one under it
constexpr double kPileStep = 0.04;                // ...and the most it rises

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

const mk::Mesh& itemMesh(const std::string& model) {
    static std::map<std::string, mk::Mesh> built;  // (as the renderer builds a placed part with Placed::bare: placedMesh)
    static uint64_t builtFor = 0;
    if (builtFor != mk::picksVersion()) built.clear(), builtFor = mk::picksVersion();
    auto it = built.find(model);
    if (it != built.end()) return it->second;
    mk::Mesh& m = built[model];
    if (const mk::ModelDef* d = mk::findModel(model)) {
        const int variant = mk::pickedVariant(*d);
        mk::P p = mk::pickedParams(*d, variant);
        p.v["bare"] = 1;
        mk::build(*d, variant, p, m);
    }
    return m;
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
        out.lo = V3(x + 0.10 - kOver, y + kDeck, -hw);  // (a deck on the sill, the gate's bottom edge: the load clears it)
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
    out.lo = V3(x + 0.12, floorY + kLiner, -hw);  // (on a carpet laid over the bed's black liner: models/cargo.cpp)
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
    struct Entry {
        int item;
        Pose pose[2];  // as it lies, and turned a quarter
        int copies;
        double step;   // how much each copy adds to the pile's height
    };
    struct Rect {
        double x0, z0, x1, z1;
    };
    // Packs everything at a shrink factor `f` (the big things shrink first: a hammer stays a hammer);
    // false if it doesn't all fit the floor.
    auto pack = [&](double f, int order, int rule, std::vector<TrunkSlot>& out) {
        out.clear();
        std::vector<Entry> es;
        for (int i = 0; i < (int)items.size(); i++) {
            const mk::Mesh& m = itemMesh(items[i].model);
            const bool laid = lieDown(m);
            const double s0 = preferredScale(items[i].model);
            const Pose big = poseOf(m, s0, laid, false);
            const double longest = std::max(big.hi.x - big.lo.x, big.hi.z - big.lo.z);
            const double s = s0 * std::pow(f, clampd(longest / 0.8, 0.3, 1.0));
            Entry e;
            e.item = i, e.copies = items[i].ref.kind == TK_TOOL || items[i].ref.kind == TK_JACK ? 1 : kMaxCopies;  // (a pile has its room, however many are in it: a count changing doesn't shuffle the back)
            e.pose[0] = poseOf(m, s, laid, false), e.pose[1] = poseOf(m, s, laid, true);
            e.step = clampd((e.pose[0].hi.y - e.pose[0].lo.y) * 0.6, 0.015, kPileStep);
            es.push_back(e);
        }
        // The big ones first (by footprint, by longest side or by width), the shelves filling in from the back.
        auto key = [&](const Entry& e) {
            const double dx = e.pose[0].hi.x - e.pose[0].lo.x, dz = e.pose[0].hi.z - e.pose[0].lo.z;
            if (order > 2) return dx * dz * (0.4 + 1.2 * mk::hash01(order, e.item));  // (a shuffle that still leans to the big ones)
            return order == 0 ? dx * dz : order == 1 ? std::max(dx, dz) : std::min(dx, dz);
        };
        std::stable_sort(es.begin(), es.end(), [&](const Entry& a, const Entry& b) { return key(a) > key(b); });
        const double m0 = kGap * 0.5;
        std::vector<Rect> free = {{box.lo.x + m0, box.lo.z + m0, box.hi.x - m0 + kGap, box.hi.z - m0 + kGap}};
        for (const Entry& e : es) {
            int bo = -1;
            size_t bi = 0;
            double bShort = 1e9, bX = 1e9;
            for (int o = 0; o < 2; o++) {
                const Pose& p = e.pose[o];
                const double w = p.hi.x - p.lo.x + kPileX * (e.copies - 1) + kGap, d = p.hi.z - p.lo.z + kPileZ * (e.copies - 1) + kGap;
                const double high = (p.hi.y - p.lo.y) + e.step * (e.copies - 1) + 0.004;
                if (high > box.hi.y - box.lo.y) continue;
                for (size_t k = 0; k < free.size(); k++) {
                    const double fw = free[k].x1 - free[k].x0 - w, fd = free[k].z1 - free[k].z0 - d;
                    if (fw < -1e-9 || fd < -1e-9) continue;
                    // rule 0: the snuggest side; 1: the least area left over; 2: the rearmost, then the leftmost
                    const double sh = rule == 0 ? std::min(fw, fd) : rule == 1 ? (free[k].x1 - free[k].x0) * (free[k].z1 - free[k].z0) - w * d : free[k].x0 * 4 + free[k].z0;
                    if (sh < bShort - 1e-9 || (sh < bShort + 1e-9 && free[k].x0 < bX)) bShort = sh, bX = free[k].x0, bo = o, bi = k;
                }
            }
            if (bo < 0) return false;
            const Pose& p = e.pose[bo];
            const double w = p.hi.x - p.lo.x + kPileX * (e.copies - 1) + kGap, d = p.hi.z - p.lo.z + kPileZ * (e.copies - 1) + kGap;
            const Rect at{free[bi].x0, free[bi].z0, free[bi].x0 + w, free[bi].z0 + d};
            for (int k = 0; k < e.copies; k++) {
                TrunkSlot s;
                s.item = e.item, s.copy = k, s.scale = p.s;
                for (int a = 0; a < 3; a++) s.ax[a] = p.e[a];
                const V3 corner(at.x0 + kPileX * k, box.lo.y + 0.004 + e.step * k, at.z0 + kPileZ * k);
                s.at = corner - p.lo;
                s.lo = corner, s.hi = corner + (p.hi - p.lo);
                out.push_back(s);
            }
            // The free space around it, split off (and the ones that are only a part of another dropped).
            std::vector<Rect> next;
            for (const Rect& r : free) {
                if (at.x0 >= r.x1 || at.x1 <= r.x0 || at.z0 >= r.z1 || at.z1 <= r.z0) {
                    next.push_back(r);
                    continue;
                }
                if (at.x0 > r.x0) next.push_back({r.x0, r.z0, at.x0, r.z1});
                if (at.x1 < r.x1) next.push_back({at.x1, r.z0, r.x1, r.z1});
                if (at.z0 > r.z0) next.push_back({r.x0, r.z0, r.x1, at.z0});
                if (at.z1 < r.z1) next.push_back({r.x0, at.z1, r.x1, r.z1});
            }
            free.clear();
            for (size_t a = 0; a < next.size(); a++) {
                bool inside = false;
                for (size_t b = 0; b < next.size() && !inside; b++)
                    inside = a != b && next[a].x0 >= next[b].x0 && next[a].z0 >= next[b].z0 && next[a].x1 <= next[b].x1 && next[a].z1 <= next[b].z1 && (next[a].x0 != next[b].x0 || next[a].z0 != next[b].z0 || next[a].x1 != next[b].x1 || next[a].z1 != next[b].z1 || a > b);
                if (!inside) free.push_back(next[a]);
            }
        }
        return true;
    };
    // How much of it is out of sight from where you stand behind the back (a few spots across it, a little above
    // the floor): the top of each pile, hidden if another pile is in the way. Tall things go at the far end.
    auto hidden = [&](const std::vector<TrunkSlot>& sl) {
        std::vector<V3> lo(items.size(), V3(1e9, 1e9, 1e9)), hi(items.size(), V3(-1e9, -1e9, -1e9));
        std::vector<V3> top(items.size());
        std::vector<int> top_copy(items.size(), -1);
        for (const TrunkSlot& t : sl) {
            const size_t k = t.item;
            lo[k] = V3(std::min(lo[k].x, t.lo.x), std::min(lo[k].y, t.lo.y), std::min(lo[k].z, t.lo.z));
            hi[k] = V3(std::max(hi[k].x, t.hi.x), std::max(hi[k].y, t.hi.y), std::max(hi[k].z, t.hi.z));
            if (t.copy > top_copy[k]) top_copy[k] = t.copy, top[k] = V3((t.lo.x + t.hi.x) * 0.5, t.hi.y, (t.lo.z + t.hi.z) * 0.5);
        }
        int n = 0;
        for (double ez : {-0.35, 0.0, 0.35}) {
            const V3 eye(box.lo.x - 1.6, box.lo.y + 0.55, ez);
            for (size_t k = 0; k < items.size(); k++) {
                if (top_copy[k] < 0) continue;
                const V3 d = top[k] - eye;
                for (size_t j = 0; j < items.size() && true; j++) {
                    if (j == k || top_copy[j] < 0) continue;
                    double t0 = 0, t1 = 0.995;  // (the slab test along the sight line)
                    const double dd[3] = {d.x, d.y, d.z}, ee[3] = {eye.x, eye.y, eye.z};
                    const double l3[3] = {lo[j].x + 0.004, lo[j].y, lo[j].z + 0.004}, h3[3] = {hi[j].x - 0.004, hi[j].y - 0.004, hi[j].z - 0.004};
                    bool in = true;
                    for (int a = 0; a < 3 && in; a++) {
                        if (std::fabs(dd[a]) < 1e-9) in = ee[a] >= l3[a] && ee[a] <= h3[a];
                        else {
                            double ta = (l3[a] - ee[a]) / dd[a], tb = (h3[a] - ee[a]) / dd[a];
                            if (ta > tb) std::swap(ta, tb);
                            t0 = std::max(t0, ta), t1 = std::min(t1, tb);
                            in = t0 < t1;
                        }
                    }
                    if (in) {
                        n++;
                        break;
                    }
                }
            }
        }
        return n;
    };
    // The biggest everything can be and still be all there: the largest shrink factor (found by halving) at which
    // one of a few orders and fit rules packs it all; then, of the ways it packs (a little under that, too), the one
    // that hides least. (Cached: a stock changes now and then, a frame asks every time.)
    std::string key;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f", box.lo.x, box.lo.y, box.lo.z, box.hi.x, box.hi.y, box.hi.z);
    key = buf;
    for (const TrunkItem& it : items) key += "|" + it.model + ":" + std::to_string(it.ref.kind == TK_TOOL || it.ref.kind == TK_JACK);
    static std::mutex lock;
    static std::map<std::string, std::vector<TrunkSlot>> cache;
    std::vector<TrunkSlot> all;
    {
        std::lock_guard<std::mutex> g(lock);
        auto hit = cache.find(key);
        if (hit != cache.end()) all = hit->second;
    }
    constexpr int kTries = 30;
    if (all.empty()) {
        std::vector<TrunkSlot> tmp;
        auto fits = [&](double f) {
            for (int order = 0; order < 3 + kTries; order++)
                for (int rule = 0; rule < 3; rule++)
                    if (pack(f, order, rule, tmp)) return true;
            return false;
        };
        double fmax = 1.0;
        if (!fits(1.0)) {
            double lo = 0.02, hi = 1.0;  // (lo always fits: everything a speck)
            for (int i = 0; i < 14; i++) {
                const double mid = (lo + hi) * 0.5;
                (fits(mid) ? lo : hi) = mid;
            }
            fmax = lo;
        }
        int bestHidden = 1 << 30;
        for (int step = 0; step < 4 && bestHidden > 0; step++) {
            const double f = fmax * (1 - 0.04 * step);
            for (int order = 0; order < 3 + kTries && bestHidden > 0; order++)
                for (int rule = 0; rule < 3 && bestHidden > 0; rule++)
                    if (pack(f, order, rule, tmp)) {
                        const int h = hidden(tmp);
                        if (h < bestHidden) bestHidden = h, all = tmp;
                    }
        }
        std::lock_guard<std::mutex> g(lock);
        if (cache.size() > 64) cache.clear();
        cache[key] = all;
    }
    for (const TrunkSlot& t : all)  // (as many of a pile as there are of it)
        if (t.copy < std::min(items[t.item].count, kMaxCopies)) slots.push_back(t);
    return slots;
}

double slide(const Car& c) {
    if (c.gatePanel() < 0) return 0;
    const double s = c.gate.share;
    const double t = s * s * (3 - 2 * s);
    return kSlide * t - kOver * (1 - t);  // (shut, the deck rides kOver forward: all of it's in the bay)
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
    if (cargoBox(c, box) && mk::findModel("cargo_mat")) {  // (a floor to lay it on: an SUV's own is far below the sill, a pickup's bed liner is black)
        const bool bed = c.bedGate();
        Placed pl;
        pl.id = "cargo_mat", pl.oriented = true, pl.sunlit = true;
        pl.p = world(c, fr, V3((box.lo.x + box.hi.x) * 0.5 - slide(c), bed ? box.lo.y - 0.004 : box.lo.y, 0));
        pl.ax[0] = fr.fwd, pl.ax[1] = fr.up, pl.ax[2] = -fr.left;
        pl.set = {{"depth", box.hi.x - box.lo.x + (bed ? 0.03 : 0.1)}, {"width", box.hi.z - box.lo.z + (bed ? 0.05 : 0.1)}, {"height", bed ? 0.05 : kDeck - 0.02}};
        out.push_back(pl);
    }
    const std::vector<TrunkItem> items = wholeLoad(c, own, ci == sim.player, inHand, holdingJack, holdingChain);
    for (const TrunkSlot& s : layout(c, items)) {
        if (s.copy >= copiesShown(items[s.item])) continue;  // (taken: its place is left empty)
        Placed pl;
        pl.id = items[s.item].model, pl.oriented = true, pl.sunlit = true, pl.bare = true;  // (bare: an anchor without the ground it's set in)
        place(c, fr, s, pl.p, pl.ax);
        pl.scale = V3(s.scale, s.scale, s.scale);
        out.push_back(pl);
    }
}

bool backOpen(const Car& c) { return !c.hasGate() || c.gateOpen() || c.gate.dir > 0; }

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
