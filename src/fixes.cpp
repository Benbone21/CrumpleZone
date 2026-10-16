#include "fixes.h"

#include <cmath>

namespace {

const char* const kToolNames[TOOL_COUNT] = {"LUG WRENCH", "WRENCHES", "SOCKET SET", "HAMMER", "PRY BAR", "TIRE IRONS", "JACK"};
const char* const kToolModels[TOOL_COUNT] = {"lug_wrench", "wrench", "ratchet", "hammer", "pry_bar", "tire_iron", "jack"};
const char* const kToolA[TOOL_COUNT] = {"A LUG WRENCH", "WRENCHES", "A SOCKET SET", "A HAMMER", "A PRY BAR", "TIRE IRONS", "A JACK"};
const char* const kPartNames[PART_COUNT] = {"TIRE", "CV AXLE", "DRIVE SHAFT", "TIE ROD", "CONTROL ARM", "SPARK PLUGS"};
const char* const kPartModels[PART_COUNT] = {"tire_part", "cv_axle", "drive_shaft", "tie_rod", "control_arm", "spark_plug"};

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

bool bent(const Beam& b) { return beamBent(b); }

// The corner a beam belongs to: the nearest hub within reach of its middle (-1 none).
int cornerOf(const Car& c, const Beam& b) {
    const V3 mid = (c.nodes[b.a].p + c.nodes[b.b].p) * 0.5;
    int best = -1;
    double bd = 1.0;
    for (int k = 0; k < (int)c.wheels.size(); k++) {
        if (c.wheels[k].detached) continue;
        const double d = len(hubOf(c, c.wheels[k]) - mid);
        if (d < bd) bd = d, best = k;
    }
    return best;
}

}  // namespace

bool beamBent(const Beam& b) { return !b.broken && std::fabs(b.L0 - b.Lorig) > 0.005 * b.Lorig; }
const char* toolName(Tool t) { return kToolNames[t]; }
const char* toolModel(Tool t) { return kToolModels[t]; }
const char* repairPartName(RepairPart p) { return kPartNames[p]; }
const char* repairPartModel(RepairPart p) { return kPartModels[p]; }
std::string toolKey(Tool t) { return std::string("TOOL:") + kToolNames[t]; }
std::string repairPartKey(RepairPart p) { return std::string("REPAIR:") + kPartNames[p]; }

std::vector<Fix> fixesFor(const Car& c) {
    std::vector<Fix> out;
    if (c.isProp()) return out;
    const Frame fr = c.frame();
    auto add = [&](Fix::Kind k, int wheel, int beam, RepairPart part, std::vector<Tool> tools, bool lifted, double hold, V3 at, const char* what) {
        Fix f{k};
        f.wheel = wheel, f.beam = beam, f.part = part, f.tools = std::move(tools), f.lifted = lifted, f.hold = hold, f.at = at, f.what = what;
        out.push_back(f);
    };
    for (int k = 0; k < (int)c.wheels.size(); k++) {
        const Wheel& wh = c.wheels[k];
        if (wh.detached) continue;
        const V3 hub = hubOf(c, wh);
        const V3 inboard = hub + (fr.com - hub) * 0.25;  // (behind the wheel, at the knuckle)
        bool cv = wh.shaftBroken;
        int cvBeam = -1;
        for (int bi : wh.shafts)
            if (bi >= 0 && c.beams[bi].broken) {
                const Beam& b = c.beams[bi];
                const double L = len(c.nodes[b.b].p - c.nodes[b.a].p);
                if (L < b.Lorig * 1.2 && L > b.Lorig * 0.6) cv = true, cvBeam = bi;  // (the ends still where they go)
            }
        if (cv) add(Fix::CV_AXLE, k, cvBeam, PART_CV_AXLE, {TOOL_RATCHET, TOOL_HAMMER}, true, 5, inboard, "REPLACE THE CV AXLE");
        if (std::fabs(wh.tieOffset) > 0.002) add(Fix::TIE_ROD, k, -1, PART_TIE_ROD, {TOOL_WRENCH, TOOL_HAMMER}, false, 4, inboard, "REPLACE THE TIE ROD");
        if (wh.popped && wh.mounted && !wh.debeaded)  // (at the top of the tread: the hub's for the wheel itself)
            add(Fix::NEW_TIRE, k, -1, PART_TIRE, {TOOL_TIRE_IRONS, TOOL_LUG_WRENCH}, true, 5, hub + fr.up * (wh.R * 0.85), "PUT A NEW TIRE ON");
    }
    for (int bi = 0; bi < (int)c.beams.size(); bi++) {
        const Beam& b = c.beams[bi];
        if (!bent(b) || (b.type != BEAM_ARM && b.type != BEAM_TIEROD)) continue;
        const int k = cornerOf(c, b);
        const V3 at = (c.nodes[b.a].p + c.nodes[b.b].p) * 0.5;
        if (b.type == BEAM_ARM) add(Fix::CONTROL_ARM, k, bi, PART_CONTROL_ARM, {TOOL_RATCHET, TOOL_PRY_BAR}, true, 5, at, "REPLACE THE BENT CONTROL ARM");
        else add(Fix::TIE_ROD, k, bi, PART_TIE_ROD, {TOOL_WRENCH, TOOL_HAMMER}, false, 4, at, "REPLACE THE BENT TIE ROD");
    }
    for (int a = 0; a < (int)c.axles.size(); a++) {
        const Axle& ax = c.axles[a];
        if (std::fabs(ax.arbBend) > 0.005) {
            V3 at = fr.com + fr.fwd * (ax.front ? c.spec->wheelbase * 0.5 : -c.spec->wheelbase * 0.5) - fr.up * 0.3;
            Fix f{Fix::SWAY_BAR};
            f.axle = a, f.part = PART_CONTROL_ARM, f.tools = {TOOL_WRENCH, TOOL_PRY_BAR}, f.hold = 4, f.at = at, f.what = "REPLACE THE BENT SWAY BAR LINK";
            out.push_back(f);
        }
        if (ax.propShaft >= 0 && ax.propShaft < (int)c.beams.size() && c.beams[ax.propShaft].broken) {
            const Beam& b = c.beams[ax.propShaft];
            Fix f{Fix::DRIVE_SHAFT};
            f.axle = a, f.beam = ax.propShaft, f.part = PART_DRIVE_SHAFT, f.tools = {TOOL_WRENCH}, f.hold = 4;
            f.at = (c.nodes[b.a].p + c.nodes[b.b].p) * 0.5, f.what = "REPLACE THE DRIVE SHAFT";
            out.push_back(f);
        }
    }
    if (c.hydrolocked) {
        Fix f{Fix::SPARK_PLUGS};
        f.part = PART_SPARK_PLUGS, f.tools = {TOOL_RATCHET}, f.hold = 5;
        f.at = fr.com + fr.fwd * c.spec->engineX + fr.up * 0.35, f.what = "PULL THE PLUGS, CRANK THE WATER OUT";
        out.push_back(f);
    }
    return out;
}

std::string missingFor(const Fix& f, const Kit& kit, bool cornerLifted) {
    std::vector<Tool> need;
    for (Tool t : f.tools)
        if (!kit.has(t)) need.push_back(t);
    if (f.lifted && !cornerLifted && !kit.has(TOOL_JACK)) need.push_back(TOOL_JACK);
    if (!need.empty()) {
        std::string s = "NEEDS ";
        bool stowed = false;
        for (size_t i = 0; i < need.size(); i++) s += (i ? (i + 1 == need.size() ? " AND " : ", ") : "") + std::string(kToolA[need[i]]), stowed |= kit.stowedTools[need[i]];
        return stowed ? s + " (IN THE TRUCK: OPEN THE BACK)" : s;
    }
    if (!kit.hasPart(f.part)) return kit.stowedParts[f.part] ? std::string("THE ") + kPartNames[f.part] + " IS IN THE TRUCK" : std::string("NO ") + kPartNames[f.part] + ": FIND OR BUY ONE";
    if (f.lifted && !cornerLifted) return "JACK THIS CORNER UP FIRST";
    return "";
}

bool applyFix(Car& c, const Fix& f) {
    switch (f.kind) {
    case Fix::CV_AXLE: {
        if (f.wheel < 0 || f.wheel >= (int)c.wheels.size()) return false;
        Wheel& wh = c.wheels[f.wheel];
        wh.shaftBroken = false;
        wh.shaftLoad = 0;
        for (int bi : wh.shafts)
            if (bi >= 0 && c.beams[bi].broken) c.beams[bi].broken = false;
        break;
    }
    case Fix::DRIVE_SHAFT:
        if (f.beam < 0 || !c.beams[f.beam].broken) return false;
        c.beams[f.beam].broken = false;
        break;
    case Fix::TIE_ROD:
        if (f.beam >= 0) c.beams[f.beam].L0 = c.beams[f.beam].Lorig;
        if (f.wheel >= 0 && f.wheel < (int)c.wheels.size()) c.wheels[f.wheel].tieOffset = 0;
        break;
    case Fix::CONTROL_ARM:
        if (f.beam < 0) return false;
        c.beams[f.beam].L0 = c.beams[f.beam].Lorig;
        break;
    case Fix::SWAY_BAR:
        if (f.axle < 0) return false;
        c.axles[f.axle].arbBend = 0;
        break;
    case Fix::NEW_TIRE: {
        if (f.wheel < 0) return false;
        Wheel& wh = c.wheels[f.wheel];
        if (!wh.popped) return false;
        wh.popped = false;
        c.tirePsi = std::max(c.tirePsi, c.spec->tirePsi * 0.8);
        break;
    }
    case Fix::SPARK_PLUGS:
        if (!c.hydrolocked) return false;
        c.hydrolocked = false;
        c.engineDead = c.engineHealth < 0.12;
        break;
    }
    c.wake();
    return true;
}
