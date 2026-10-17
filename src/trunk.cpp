// The truck's cargo (trunk.h): the back gate's hinges, and the kit that rides in the back.
#include "trunk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include "bodyshell.h"
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

constexpr double kBedDepth = 1.60;    // a pickup's bed (its gate is drawn at the front of this)
constexpr double kHalfWidth = 0.28;   // of the body width: between the wheel wells
constexpr double kGateT = 0.05;      // a pickup's tailgate: its thickness (models/cargo.cpp)
constexpr double kLiner = 0.014;      // m a pickup's bed carpet stands over its floor

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

bool cargoBox(const Car& c, Box& out) {
    if (c.isProp() || !c.bedGate() || c.restLocal.empty()) return false;
    const double hw = kHalfWidth * c.spec->width;
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

const char* gateName(const Car& c) {
    const int pi = c.gatePanel();
    return pi >= 0 && !std::strcmp(c.panels[pi].name, "LIFTGATE") ? "LIFTGATE" : "TAILGATE";
}

bool bedGatePlace(const Car& c, const Frame& fr, V3& at, V3 ax[3], double& width, double* height) {
    BedMouth m;
    if (!c.bedGate() || c.restLocal.empty() || !bedMouth(*c.spec, m)) return false;
    const double s = c.gate.share, phi = (s * s * (3 - 2 * s)) * (M_PI / 2) * 0.985;
    const V3 e0(std::cos(phi), std::sin(phi), 0), e1(-std::sin(phi), std::cos(phi), 0), e2(0, 0, 1);
    // Shut it closes the bed's mouth: its outside flush with the walls' ends, edge to edge between them, up to the rail.
    at = world(c, fr, V3(m.x + kGateT + 0.012, m.floorY + kLiner, 0));
    const V3* e[3] = {&e0, &e1, &e2};
    for (int a = 0; a < 3; a++) ax[a] = fr.fwd * e[a]->x + fr.up * e[a]->y - fr.left * e[a]->z;
    width = 2 * m.inner + 0.01;
    if (height) *height = m.railY - m.floorY - kLiner - 0.004;
    return true;
}

void gateModels(const Sim& sim, int ci, std::vector<Placed>& out) {
    const Car& c = sim.cars[ci];
    if (c.isProp() || !c.hasGate()) return;
    V3 at, ax[3];
    double width = 1.2, height = 0.33;
    if (!bedGatePlace(c, c.frame(), at, ax, width, &height) || !mk::findModel("bed_gate")) return;
    Placed pl;
    pl.id = "bed_gate", pl.p = at, pl.oriented = true;
    for (int a = 0; a < 3; a++) pl.ax[a] = ax[a];
    pl.set = {{"width", width}, {"height", height}, {"r", clampd(c.color.x, 0, 1)}, {"g", clampd(c.color.y, 0, 1)}, {"b", clampd(c.color.z, 0, 1)}};
    out.push_back(pl);
}

bool kitInReach(const Sim& sim, const V3& at) {
    for (const Car& c : sim.cars) {
        if (c.remote || c.isProp() || c.ai) continue;
        if (c.hasGate()) {
            if (len(at - gateAt(c)) < kReach) return true;
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
