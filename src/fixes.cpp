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
            f.axle = a, f.beam = ax.propShaft, f.part = PART_DRIVE_SHAFT, f.tools = {TOOL_RATCHET}, f.hold = 4;
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

const char* jobName(int kind) {
    static const char* const n[] = {"CV AXLE", "DRIVE SHAFT", "TIE ROD", "CONTROL ARM", "SWAY BAR LINK", "NEW TIRE", "SPARK PLUGS"};
    return kind >= 0 && kind <= Fix::SPARK_PLUGS ? n[kind] : "REPAIR";
}

bool jobJacks(int kind, int wheel) { return wheel >= 0 && (kind == Fix::CV_AXLE || kind == Fix::CONTROL_ARM || kind == Fix::NEW_TIRE); }
bool jobWheelOff(const Car& c, int kind, int wheel) { return jobJacks(kind, wheel) && c.spec->wheelLoss; }

std::vector<RepairStep> tireSteps() {
    std::vector<RepairStep> s(3);
    s[0].label = "OLD TIRE OFF", s[0].verb = "LEVER THE OLD TIRE OFF THE RIM", s[0].tool = TOOL_TIRE_IRONS, s[0].hold = 5;
    s[1].label = "NEW TIRE ON", s[1].verb = "WORK THE NEW TIRE ON", s[1].part = PART_TIRE, s[1].hold = 4;
    s[2].label = "BEAD SEATED, AIRED UP", s[2].verb = "SEAT THE BEAD AND AIR IT UP", s[2].tool = TOOL_TIRE_IRONS, s[2].hold = 4, s[2].fixes = true;
    return s;
}

std::vector<RepairStep> repairSteps(int kind, bool jack, bool wheelOff) {
    std::vector<RepairStep> s;
    auto step = [&](RepairStep::Type t, const char* label, const char* verb = "", int tool = -1, int part = -1, double hold = 4, bool fixes = false, int end = 0) {
        RepairStep r;
        r.type = t, r.label = label, r.verb = verb, r.tool = tool, r.part = part, r.hold = hold, r.fixes = fixes, r.end = end;
        s.push_back(r);
    };
    const auto W = RepairStep::WORK;
    if (jack) step(RepairStep::JACK, "JACKED UP", "JACK THIS CORNER UP");
    if (wheelOff) step(RepairStep::WHEEL_OFF, "WHEEL OFF", "REMOVE WHEEL", TOOL_LUG_WRENCH);
    // Every repair is two jobs: the broken part comes off (and is dropped beside the car), then the new one goes on.
    auto swap = [&](const char* rm, const char* fit, int tool, int part, double holdOff, double holdOn) {
        step(W, rm, rm, tool, -1, holdOff);
        s.back().removes = true;
        step(W, fit, fit, -1, part, holdOn, true);
    };
    switch (kind) {
    case Fix::CV_AXLE: swap("REMOVE CV AXLE", "FIT CV AXLE", TOOL_HAMMER, PART_CV_AXLE, 6, 4); break;
    case Fix::CONTROL_ARM: swap("REMOVE CONTROL ARM", "FIT CONTROL ARM", TOOL_RATCHET, PART_CONTROL_ARM, 6, 4); break;
    case Fix::TIE_ROD: swap("REMOVE TIE ROD", "FIT TIE ROD", TOOL_WRENCH, PART_TIE_ROD, 5, 3.5); break;
    case Fix::SWAY_BAR: swap("REMOVE SWAY BAR LINK", "FIT SWAY BAR LINK", TOOL_WRENCH, PART_CONTROL_ARM, 4, 3.5); break;
    case Fix::DRIVE_SHAFT: swap("REMOVE DRIVE SHAFT", "FIT DRIVE SHAFT", TOOL_RATCHET, PART_DRIVE_SHAFT, 6, 5); break;
    case Fix::SPARK_PLUGS: swap("REMOVE SPARK PLUGS", "FIT SPARK PLUGS", TOOL_RATCHET, PART_SPARK_PLUGS, 4, 4); break;
    case Fix::NEW_TIRE:
        if (wheelOff) {  // (the tire's work is on the wheel once it's off, on the ground: tireSteps)
            step(W, "NEW TIRE ON THE RIM", "PUT A NEW TIRE ON THE WHEEL YOU TOOK OFF", -1, -1, 0, true);
            s.back().ground = true;
        } else {
            for (const RepairStep& t : tireSteps()) s.push_back(t);
        }
        break;
    }
    if (wheelOff) step(RepairStep::WHEEL_ON, "WHEEL BACK ON", "MOUNT WHEEL", TOOL_LUG_WRENCH);
    if (jack) step(RepairStep::LOWER, "JACK DOWN", "LET THE JACK DOWN");
    return s;
}

RepairPlan planRepair(int kind, bool jack, bool wheelOff, int done, bool fixed, const CornerNow& now) {
    RepairPlan p;
    p.steps = repairSteps(kind, jack, wheelOff);
    p.done.assign(p.steps.size(), false);
    int nWork = 0;
    for (const RepairStep& st : p.steps) nWork += st.type == RepairStep::WORK;
    // A tire off the truck: its work is done once a good wheel is back on the hub (or the spare is).
    if (kind == Fix::NEW_TIRE && wheelOff) done = now.wheelOn && now.wheelGood ? nWork : 0, fixed = done > 0;
    const bool worked = fixed || done >= nWork;
    const bool back = now.wheelOn && (kind != Fix::NEW_TIRE || now.wheelGood);  // (a flat back on is no new tire)
    int w = 0;
    for (size_t i = 0; i < p.steps.size(); i++) {
        const RepairStep& st = p.steps[i];
        bool d = false;
        switch (st.type) {
        case RepairStep::JACK: d = worked || now.lifted; break;
        case RepairStep::WHEEL_OFF: d = worked || !now.wheelOn; break;
        case RepairStep::WORK: d = w++ < done || worked; break;
        case RepairStep::WHEEL_ON: d = worked && back; break;
        case RepairStep::LOWER: d = worked && (!wheelOff || back) && !now.jackUp; break;
        }
        p.done[i] = d;
        if (!d && p.next < 0) p.next = (int)i;
    }
    return p;
}

int fieldJobOf(const Car& c, const Fix& f) {
    for (int i = 0; i < (int)c.fieldJobs.size(); i++) {
        const Car::FieldJob& j = c.fieldJobs[i];
        if (j.kind == f.kind && j.wheel == f.wheel && j.axle == f.axle && (f.kind == Fix::CV_AXLE || f.kind == Fix::NEW_TIRE || j.beam == f.beam)) return i;
    }
    return -1;
}

std::string stepMissing(const RepairStep& st, const Kit& kit, const Kit* own, int handTool, int handPart) {
    if (st.tool >= 0) {
        const Tool t = (Tool)st.tool;
        if (own) {
            if (handTool == t) return "";
            return own->has(t) ? std::string("NEEDS THE ") + kToolNames[t] + " (GET IT FROM THE BACK)" : std::string("NEEDS ") + kToolA[t];
        }
        if (kit.has(t)) return "";
        return std::string("NEEDS ") + kToolA[t] + (kit.stowedTools[t] ? " (IN THE TRUCK: OPEN THE BACK)" : "");
    }
    if (st.part >= 0) {
        const RepairPart p = (RepairPart)st.part;
        if (own) {
            if (handPart == p && own->hasPart(p)) return "";
            return own->hasPart(p) ? std::string("NEEDS THE NEW ") + kPartNames[p] + " (GET IT FROM THE BACK)" : std::string("NO ") + kPartNames[p] + ": FIND OR BUY ONE";
        }
        if (kit.hasPart(p)) return "";
        return kit.stowedParts[p] ? std::string("THE ") + kPartNames[p] + " IS IN THE TRUCK" : std::string("NO ") + kPartNames[p] + ": FIND OR BUY ONE";
    }
    return "";
}
