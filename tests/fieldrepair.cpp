// Field repairs in order (src/fixes.*, src/interact.*): the corner jacked, the wheel off, the work a step at a
// time with the right thing in your hand, the wheel back on, the jack down. Each repair's order is enforced, the
// wheel steps can be undone, a job half done waits for you, and the end state is fixed.
#include <cmath>

#include "../src/handwork.h"
#include "../src/interact.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

V3 hubOf(const Car& c, int w) { return (c.nodes[c.wheels[w].inner].p + c.nodes[c.wheels[w].outer].p) * 0.5; }

void standAt(const World& w, const Car& c, Walker& wk, const V3& at, double back) {
    V3 out = at - c.frame().com;
    out.y = 0;
    out = norm(out);
    const V3 q = at + out * back;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

V3 eyeOf(const Walker& wk) { return wk.p + V3(0, 1.62, 0); }

// Lets go of E, then looks at `at` for a frame: what E would do there now.
const InteractOption& lookAt(Ctx& ctx, Interact& ia, Walker& wk, const V3& at) {
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return ia.current;
}

// Holds E on `at` until something's said (or `secs` pass), then lets go.
std::string holdOn(Ctx& ctx, Interact& ia, Walker& wk, const V3& at, double secs) {
    std::string said;
    lookAt(ctx, ia, wk, at);
    for (double t = 0; t < secs && said.empty(); t += Ctx::kFrame) {
        said = ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), true, Ctx::kFrame);
        ctx.run(Ctx::kFrame);
    }
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return said;
}

// The jack under corner k and up until the tire's clear (the jack's own hold-E is tires/swap's).
bool lift(Ctx& ctx, Car& c, int k) {
    c.placeJack(ctx.sim.world, k);
    c.jackWorked = true;
    ctx.runUntil(6, [&] { return Interact::cornerLifted(ctx.sim, c, k); }, [&] { c.jackHeight += 0.14 * Ctx::kFrame, c.wake(); });
    ctx.run(0.3);
    return Interact::cornerLifted(ctx.sim, c, k);
}

const Fix* fixOf(const std::vector<Fix>& fs, Fix::Kind kind) {
    for (const Fix& f : fs)
        if (f.kind == kind) return &f;
    return nullptr;
}

TrunkStock stock(int parts) {
    TrunkStock s;
    s.kit.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) s.kit.tools[t] = true;
    for (int p = 0; p < PART_COUNT; p++) s.kit.parts[p] = parts;
    return s;
}

std::string rows(const RepairChecklist& l) {
    std::string s = l.title + ":";
    for (size_t i = 0; i < l.rows.size(); i++) s += std::string(l.done[i] ? " [X] " : " [ ] ") + l.rows[i];
    return s;
}

int nearestLoose(const Sim& sim, const V3& p) {
    int best = -1;
    for (int i = 0; i < (int)sim.looseWheels.size(); i++)
        if (best < 0 || len(sim.looseWheels[i].p - p) < len(sim.looseWheels[best].p - p)) best = i;
    return best;
}

}  // namespace

SCENARIO(fieldRepairSteps, "fieldrepair/steps", "Every field repair is a list of steps in order: the corner jobs jack first and lower last with the wheel off between, the part goes in before the bolt that fixes it, 3 to 8 steps each, a tie rod needs no jack") {
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.run(0.3);
    const Fix::Kind kinds[] = {Fix::CV_AXLE, Fix::DRIVE_SHAFT, Fix::TIE_ROD, Fix::CONTROL_ARM, Fix::SWAY_BAR, Fix::NEW_TIRE, Fix::SPARK_PLUGS};
    for (Fix::Kind k : kinds) {
        const int wheel = k == Fix::DRIVE_SHAFT || k == Fix::SWAY_BAR || k == Fix::SPARK_PLUGS ? -1 : 0;
        const std::vector<RepairStep> st = repairSteps(k, jobJacks(k, wheel), jobWheelOff(c, k, wheel));
        const std::string tag = std::string(jobName(k)) + ": ";
        std::string list;
        for (const RepairStep& s : st) list += std::string(s.label) + "; ";
        ctx.check(tag + "3 to 8 steps", st.size() >= 3 && st.size() <= 8, list);
        int part = -1, fixes = -1;
        for (int i = 0; i < (int)st.size(); i++) {
            if (st[i].part >= 0 && part < 0) part = i;
            if (st[i].fixes) fixes = i;
        }
        ctx.check(tag + "one step fixes it, after the new part's in", fixes >= 0 && (k == Fix::NEW_TIRE || (part >= 0 && part <= fixes)), list);
        if (jobJacks(k, wheel)) {
            ctx.check(tag + "jack first, wheel off, ..., wheel on, jack down", st.front().type == RepairStep::JACK && st[1].type == RepairStep::WHEEL_OFF &&
                                                                                  st[st.size() - 2].type == RepairStep::WHEEL_ON && st.back().type == RepairStep::LOWER, list);
        }
    }
    ctx.check("a tie rod needs no jack", !jobJacks(Fix::TIE_ROD, 0));
    // A drive shaft's two ends are two different spots.
    const int a = c.axles[0].propShaft >= 0 ? 0 : 1;
    ctx.check("a driven axle with a prop shaft", c.axles[a].propShaft >= 0);
    c.beams[c.axles[a].propShaft].broken = true;
    const Fix* ds = fixOf(fixesFor(c), Fix::DRIVE_SHAFT);
    ctx.check("the drive shaft is a repair", ds != nullptr);
    if (!ds) return;
    Walker wk;
    const InteractOption front = Interact::fixOption(ctx.sim, wk, 0, *ds, Kit(), nullptr);
    c.fieldJobs.push_back({Fix::DRIVE_SHAFT, -1, ds->axle, ds->beam, 1, false});
    const InteractOption back = Interact::fixOption(ctx.sim, wk, 0, *ds, Kit(), nullptr);
    ctx.check("its two ends are unbolted where each one is", front.atEnd && back.atEnd && len(front.at - back.at) > 0.3, std::to_string(len(front.at - back.at)));
}

SCENARIO(fieldRepairCv, "fieldrepair/cv-axle", "A snapped CV axle on a Bronco, done by the book with the kit in the back: jack, wheel off, socket set, hammer, the new axle, socket set, wheel on, jack down; out of turn it says why; the wheel goes back on with nothing fixed; walk off and come back and the job's where you left it") {
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const int k = 2;  // rear left
    c.wheels[k].shaftBroken = true;
    c.handbrake = 1;
    ctx.run(1.0);
    const World& w = ctx.sim.world;
    Walker wk;
    Interact ia;
    ia.trunkOn = true;
    ia.own = stock(0);
    ia.kit = ia.own.kit;
    const Fix* f0 = fixOf(fixesFor(c), Fix::CV_AXLE);
    ctx.check("the CV axle is a repair", f0 != nullptr);
    if (!f0) return;
    const Fix fix = *f0;
    standAt(w, c, wk, fix.at, 1.2);
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    const InteractOption& o1 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("socket set in hand, the corner down: it says jack it up first", o1.blocked && o1.prompt.find("JACK THIS CORNER UP FIRST") != std::string::npos, o1.prompt);
    ctx.check("...and holding E undoes nothing", holdOn(ctx, ia, wk, fix.at, 6).empty() && c.fieldJobs.empty());
    ctx.check("the checklist shows the job", ia.checklist.title == "CV AXLE, REAR LEFT" && ia.checklist.rows.size() == 8 && ia.checklist.next == 0, rows(ia.checklist));
    ctx.check("the corner jacks up", lift(ctx, c, k));
    const InteractOption& o2 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("jacked: next, the wheel off with the lug wrench", o2.kind == IA_REMOVE_WHEEL && !o2.blocked && o2.prompt == "CV AXLE: TAKE THE WHEEL OFF (LUG WRENCH)", o2.prompt);
    std::string said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("the wheel comes off into your arms", said == "WHEEL OFF" && wk.carrying && !c.wheels[k].mounted, said);
    ctx.check("...the job's under way", c.fieldJobs.size() == 1 && c.fieldJobs[0].done == 0);
    ctx.check("...and that corner isn't driven", !c.wheelPowered(c.wheels[k]));
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    const InteractOption& o3 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("empty-handed: it needs the socket set", o3.blocked && o3.prompt.find("NEEDS THE SOCKET SET") != std::string::npos, o3.prompt);
    wk.hand = TrunkRef{TK_TOOL, TOOL_HAMMER};
    ctx.check("the hammer won't do the nut", holdOn(ctx, ia, wk, fix.at, 6).empty() && c.fieldJobs[0].done == 0, ia.current.prompt);
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    const InteractOption& o4 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("socket set in hand: undo the axle nut", o4.kind == IA_FIX && !o4.blocked && o4.prompt == "CV AXLE: UNDO THE AXLE NUT (SOCKET SET)", o4.prompt);
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("hold E: the nut's off", said == "AXLE NUT OFF" && c.fieldJobs[0].done == 1, said);
    // Walk off and come back.
    const V3 home = wk.p;
    wk.p = home + V3(30, 0, 0);
    wk.p.y = w.height(wk.p.x, wk.p.z);
    for (int i = 0; i < 30; i++) ia.update(ctx.sim, wk, eyeOf(wk), V3(1, 0, 0), false, Ctx::kFrame), ctx.run(Ctx::kFrame);
    wk.p = home;
    lookAt(ctx, ia, wk, fix.at);
    ctx.check("walked off and back: the job's where it was", c.fieldJobs.size() == 1 && c.fieldJobs[0].done == 1 && ia.checklist.next == 3 && ia.checklist.done[0] && ia.checklist.done[1] && ia.checklist.done[2], rows(ia.checklist));
    // The wheel back on with nothing fixed, and off again.
    const int li = nearestLoose(ctx.sim, home);
    ctx.check("the wheel lies by the truck", li >= 0);
    if (li < 0) return;
    wk.hand = TrunkRef();
    said = holdOn(ctx, ia, wk, ctx.sim.looseWheels[li].p, 2);
    ctx.check("picked up", wk.carrying.has_value(), said);
    const V3 hub = hubOf(c, k);
    said = holdOn(ctx, ia, wk, hub, 8);
    ctx.check("the wheel goes back on with nothing fixed", said == "WHEEL ON" && c.wheels[k].mounted && c.wheels[k].shaftBroken, said);
    wk.hand = TrunkRef{TK_TOOL, TOOL_HAMMER};
    const InteractOption& o5 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("...so it says take the wheel off first", o5.kind == IA_REMOVE_WHEEL && c.fieldJobs[0].done == 1, o5.prompt);
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("off again", said == "WHEEL OFF" && !c.wheels[k].mounted, said);
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    wk.hand = TrunkRef{TK_TOOL, TOOL_HAMMER};
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("hammer: the old axle out", said == "OLD AXLE OUT", said);
    // No part: it can't go on.
    wk.hand = TrunkRef();
    const InteractOption& o6 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("no CV axle: it says so", o6.blocked && o6.prompt.find("NO CV AXLE") != std::string::npos, o6.prompt);
    ia.own.kit.parts[PART_CV_AXLE] = 1;
    ia.kit = ia.own.kit;
    const InteractOption& o7 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("one in the back: get it", o7.blocked && o7.prompt.find("NEEDS THE NEW CV AXLE") != std::string::npos, o7.prompt);
    wk.hand = TrunkRef{TK_PART, PART_CV_AXLE};
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("the new axle goes in (out of your hand, used up)", said == "NEW AXLE IN" && ia.usedPart == PART_CV_AXLE && !wk.hand.any(), said);
    ia.usedPart = -1;
    ctx.check("...not bolted up yet: still broken", c.wheels[k].shaftBroken);
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("socket set: torqued, fixed", said == "AXLE NUT TORQUED: CV AXLE FIXED" && !c.wheels[k].shaftBroken, said);
    lookAt(ctx, ia, wk, hub);
    ctx.check("the checklist: the wheel back on next", ia.checklist.next == 6 && ia.checklist.rows[6] == "WHEEL BACK ON", rows(ia.checklist));
    wk.hand = TrunkRef();
    said = holdOn(ctx, ia, wk, ctx.sim.looseWheels[nearestLoose(ctx.sim, home)].p, 2);
    said = holdOn(ctx, ia, wk, hubOf(c, k), 8);
    ctx.check("the wheel back on", said == "WHEEL ON" && c.wheels[k].mounted, said);
    standAt(w, c, wk, hubOf(c, k), 1.1);
    said = holdOn(ctx, ia, wk, Interact::jackAim(ctx.sim, c, k), 8);
    ctx.check("the jack down", said == "JACK DOWN", said);
    lookAt(ctx, ia, wk, hubOf(c, k));
    ctx.check("the job's done and gone", c.fieldJobs.empty());
    ctx.check("the corner's driven again", c.wheelPowered(c.wheels[k]));
}

SCENARIO(fieldRepairBareHub, "fieldrepair/bare-hub", "The jack let down with the wheel off: the bare hub drops onto the ground, the work waits for the jack again, and that corner gets no drive (driving off with it half done is allowed, the axle stays broken)") {
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const int k = 2;
    c.wheels[k].shaftBroken = true;
    c.handbrake = 1;
    ctx.run(1.0);
    Walker wk;
    Interact ia;
    const Fix fix = *fixOf(fixesFor(c), Fix::CV_AXLE);
    standAt(ctx.sim.world, c, wk, fix.at, 1.2);
    ctx.check("jacked", lift(ctx, c, k));
    std::string said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("wheel off", said == "WHEEL OFF" && !c.wheels[k].mounted, said);
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("(kit on you) the nut off", said == "AXLE NUT OFF", said);
    const double up = hubOf(c, k).y - ctx.sim.world.height(hubOf(c, k).x, hubOf(c, k).z);
    c.jackHeight = 0;
    c.wake();
    ctx.run(2.0);
    const V3 h = hubOf(c, k);
    const double down = h.y - ctx.sim.world.height(h.x, h.z);
    ctx.check("let down, the bare hub sits on the ground", down < up - 0.1 && down < kBareHubR + 0.08, std::to_string(up) + " -> " + std::to_string(down));
    const InteractOption o = Interact::fixOption(ctx.sim, wk, 0, *fixOf(fixesFor(c), Fix::CV_AXLE), ia.kit, nullptr);
    ctx.check("the next step wants the corner up again", o.blocked && o.prompt.find("JACK THIS CORNER UP FIRST") != std::string::npos, o.prompt);
    c.jackPlaced = false, c.jackWheel = -1;
    // Drive off on three wheels (an open diff spins the bare side): the bare corner isn't driven, the job and the break stay.
    c.handbrake = 0;
    const V3 start = c.frame().com;
    for (int i = 0; i < 240; i++) {
        c.throttle = 0.5;
        ctx.run(Ctx::kFrame);
    }
    ctx.metric("moved with the throttle on, the job half done (m)", len(c.frame().com - start), "m", 0, 100);
    ctx.check("the bare corner isn't driven", !c.wheelPowered(c.wheels[k]));
    ctx.check("the axle stays broken and the job stays", c.wheels[k].shaftBroken && c.fieldJobs.size() == 1 && c.fieldJobs[0].done == 1);
    // Fixed some other way (a station): the job goes.
    c.wheels[k].shaftBroken = false;
    for (int bi : c.wheels[k].shafts) c.beams[bi].broken = false;
    ia.update(ctx.sim, wk, eyeOf(wk), V3(1, 0, 0), false, Ctx::kFrame);
    ctx.check("fixed elsewhere: the job's dropped", c.fieldJobs.empty());
}

SCENARIO(fieldRepairTire, "fieldrepair/new-tire", "A flat on a Bronco without a spare: jack, the flat off and down on the ground, tire irons the old tire off, the new tire on, the bead seated and aired, the wheel back on, the jack down") {
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.focus(c);
    ctx.run(1.0);
    const int k = 0;
    c.wheels[k].popped = true;
    c.handbrake = 1;
    ctx.run(1.0);
    Walker wk;
    Interact ia;
    ia.trunkOn = true;
    ia.own = stock(1);
    ia.kit = ia.own.kit;
    const Fix fix = *fixOf(fixesFor(c), Fix::NEW_TIRE);
    standAt(ctx.sim.world, c, wk, hubOf(c, k), 1.0);
    const InteractOption& o0 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("jack first", o0.blocked && o0.prompt.find("JACK") != std::string::npos, o0.prompt);
    ctx.check("jacked", lift(ctx, c, k));
    std::string said = holdOn(ctx, ia, wk, hubOf(c, k), 8);
    ctx.check("the flat off", said == "WHEEL OFF" && wk.carrying && wk.carrying->popped, said);
    ctx.check("...a new tire's under way", c.fieldJobs.size() == 1 && c.fieldJobs[0].kind == Fix::NEW_TIRE);
    Interact::drop(ctx.sim, wk, V3(1, 0, 0));
    const InteractOption& oh = lookAt(ctx, ia, wk, hubOf(c, k));
    ctx.check("the bare hub says the tire's to fix on the ground", oh.blocked && oh.prompt.find("NEW TIRE") != std::string::npos, oh.prompt);
    const int li = nearestLoose(ctx.sim, wk.p);
    const V3 at = ctx.sim.looseWheels[li].p;
    wk.hand = TrunkRef{TK_PART, PART_TIRE};
    const InteractOption& o1 = lookAt(ctx, ia, wk, at);
    ctx.check("the new tire before the old one's off: tire irons first", o1.kind == IA_TIRE_WORK && o1.blocked && o1.prompt.find("TIRE IRONS") != std::string::npos, o1.prompt);
    ctx.check("...and the checklist is the tire's", ia.checklist.rows.size() == 3 && ia.checklist.next == 0, rows(ia.checklist));
    wk.hand = TrunkRef{TK_TOOL, TOOL_TIRE_IRONS};
    ctx.check("tire irons: the old tire off", holdOn(ctx, ia, wk, at, 8) == "OLD TIRE OFF");
    wk.hand = TrunkRef{TK_PART, PART_TIRE};
    said = holdOn(ctx, ia, wk, at, 8);
    ctx.check("the new tire on (used up)", said == "NEW TIRE ON" && ia.usedPart == PART_TIRE, said);
    wk.hand = TrunkRef{TK_TOOL, TOOL_TIRE_IRONS};
    said = holdOn(ctx, ia, wk, at, 8);
    ctx.check("bead seated, aired: a good wheel", !ctx.sim.looseWheels[li].item.popped && said.find("NEW TIRE ON") == 0, said);
    wk.hand = TrunkRef();
    holdOn(ctx, ia, wk, at, 2);
    said = holdOn(ctx, ia, wk, hubOf(c, k), 8);
    ctx.check("back on", said == "WHEEL ON" && c.wheels[k].mounted && !c.wheels[k].popped, said);
    standAt(ctx.sim.world, c, wk, hubOf(c, k), 1.1);
    said = holdOn(ctx, ia, wk, Interact::jackAim(ctx.sim, c, k), 8);
    lookAt(ctx, ia, wk, hubOf(c, k));
    ctx.check("jack down: the job's done", said == "JACK DOWN" && c.fieldJobs.empty() && fixesFor(c).empty(), said);
}
