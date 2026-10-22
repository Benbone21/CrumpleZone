// On foot, hands on (interact.h): the spare, the wheels, and the hold-E jobs.
#include "interact.h"

#include "carentry.h"

#include <cmath>

#include "modelkit.h"
#include "objshape.h"
#include "prompts.h"
#include "walker.h"

namespace {

// Hold times (s): the work each job is, kept short (the game shows the job being done). The wheel jobs are
// five nuts a go at about two ratchet swings each (handwork.cpp), so they run to nearly 6 s.
constexpr double kTakeSpare = 1.0, kStowSpare = 1.0, kRemoveWheel = 5.8, kFitWheel = 5.8, kReseatTire = 3.5, kPickUp = 0.5;
constexpr double kTakeJack = 0.6, kStowJack = 0.6, kPlaceJack = 1.0, kRemoveJack = 0.8;
constexpr double kJackRate = 0.14;  // m/s the saddle moves while E is held
constexpr double kTakeChain = 0.6, kFitChain = 3.0, kRemoveChain = 2.0, kStowChain = 0.6;
constexpr double kGrabLine = 1.0, kHookLine = 1.2, kUnhookLine = 0.8, kStowLine = 0.6;  // the recovery line jobs
constexpr double kLineReach = 3.0;  // m from your chest to the anchor you hook to
constexpr double kOpenCache = 1.5;  // prise the lid off a supply cache
constexpr double kTakeItem = 0.5, kStowItem = 0.5;  // the back, and things in and out of it
constexpr double kReach = prompts::kReach;  // m from your chest

// Whether a car already has a line of this kind of its own out (a winch hooked from it; a strap at either end).
bool hasLine(const Sim& sim, int ci, RopeKind kind) {
    for (const Rope& r : sim.ropes)
        if (r.kind == kind && ((r.a.kind == ANCHOR_CAR && r.a.car == ci) || (kind == ROPE_STRAP && r.b.kind == ANCHOR_CAR && r.b.car == ci))) return true;
    return false;
}

std::string withSize(const char* what, const WheelItem& it) { return std::string(what) + " (" + it.sizeName() + ")"; }

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

CornerNow cornerNow(const Sim& sim, const Car& c, int k) {
    CornerNow n;
    if (k < 0 || k >= (int)c.wheels.size()) return n;
    const Wheel& wh = c.wheels[k];
    n.lifted = Interact::cornerLifted(sim, c, k);
    n.jackUp = c.jackPlaced && c.jackWheel == k && c.jackHeight > 0;
    n.wheelOn = wh.mounted;
    n.wheelGood = wh.mounted && !wh.popped && !wh.debeaded;
    return n;
}

std::string cornerName(const Car& c, int k) { return std::string(c.wheels[k].front ? "FRONT " : "REAR ") + (c.wheels[k].left ? "LEFT" : "RIGHT"); }

// The job record for a repair (made when its first step's done).
Car::FieldJob& jobFor(Car& c, const Fix& f) {
    const int ji = fieldJobOf(c, f);
    if (ji >= 0) return c.fieldJobs[ji];
    Car::FieldJob j;
    j.kind = f.kind, j.wheel = f.wheel, j.axle = f.axle, j.beam = f.beam;
    c.fieldJobs.push_back(j);
    return c.fieldJobs.back();
}

int handTool(const Walker& wk) { return wk.hand.kind == TK_TOOL ? wk.hand.id : -1; }
int handPart(const Walker& wk) { return wk.hand.kind == TK_PART ? wk.hand.id : -1; }

RepairChecklist tireChecklist(const WheelItem& it) {
    RepairChecklist l;
    l.title = "NEW TIRE ON THE WHEEL";
    const std::vector<RepairStep> st = tireSteps();
    for (int i = 0; i < (int)st.size(); i++) l.rows.push_back(st[i].label), l.done.push_back(i < it.tireWork || !it.popped);
    l.next = it.popped ? std::min(it.tireWork, (int)st.size() - 1) : -1;
    return l;
}

}  // namespace

RepairPlan Interact::planFor(const Sim& sim, const Car& c, int kind, int wheel, const Car::FieldJob* job) {
    return planRepair(kind, jobJacks(kind, wheel), jobWheelOff(c, kind, wheel), job ? job->done : 0, job && job->fixed, cornerNow(sim, c, wheel));
}

RepairChecklist Interact::checklistFor(const Sim& sim, const Car& c, int kind, int wheel, int axle, const Car::FieldJob* job) {
    RepairChecklist l;
    l.title = jobName(kind);
    if (wheel >= 0 && wheel < (int)c.wheels.size()) l.title += ", " + cornerName(c, wheel);
    else if (axle >= 0 && axle < (int)c.axles.size()) l.title += c.axles[axle].front ? ", FRONT" : ", REAR";
    const RepairPlan p = planFor(sim, c, kind, wheel, job);
    for (size_t i = 0; i < p.steps.size(); i++) l.rows.push_back(p.steps[i].label), l.done.push_back(p.done[i]);
    l.next = p.next;
    return l;
}

InteractOption Interact::fixOption(const Sim& sim, const Walker& wk, int ci, const Fix& f, const Kit& kit, const TrunkStock* own) {
    InteractOption o;
    const Car& c = sim.cars[ci];
    const int ji = fieldJobOf(c, f);
    const RepairPlan p = planFor(sim, c, f.kind, f.wheel, ji >= 0 ? &c.fieldJobs[ji] : nullptr);
    if (p.next < 0) return o;
    const RepairStep& st = p.steps[p.next];
    o.car = ci, o.wheel = f.wheel, o.kind = IA_FIX, o.fix = f, o.at = f.at, o.step = p.next, o.tool = st.tool, o.part = st.part, o.hold = st.hold;
    const std::string what = std::string(jobName(f.kind)) + ": ";
    switch (st.type) {
    case RepairStep::JACK:
        o.blocked = true;
        o.prompt = what + (kit.has(TOOL_JACK) || c.hasJack || c.jackPlaced ? "JACK THIS CORNER UP FIRST" : "JACK THIS CORNER UP FIRST: NEEDS A JACK");
        break;
    case RepairStep::WHEEL_OFF: {  // (the wheel jobs: the lug wrench within reach, as ever)
        o.kind = IA_REMOVE_WHEEL, o.hold = kRemoveWheel, o.prompt = what + "REMOVE WHEEL (LUG WRENCH)";
        const std::string miss = stepMissing(st, kit, nullptr, -1, -1);
        if (!miss.empty()) o.blocked = true, o.prompt = what + miss;
        break;
    }
    case RepairStep::WORK: {
        o.prompt = (st.removes || (st.part >= 0 && f.kind != Fix::NEW_TIRE) ? std::string() : what) + st.verb + (st.tool >= 0 ? std::string(" (") + toolName((Tool)st.tool) + ")" : "");
        const std::string miss = st.ground ? std::string(st.verb) : stepMissing(st, kit, own ? &own->kit : nullptr, handTool(wk), handPart(wk));
        if (!miss.empty()) o.blocked = true, o.prompt = what + miss;
        break;
    }
    default:
        o.blocked = true, o.prompt = what + st.verb;
        break;
    }
    if (st.end && f.beam >= 0 && f.beam < (int)c.beams.size()) {  // a shaft's ends: each one where it bolts on
        const Beam& b = c.beams[f.beam];
        const V3 pa = c.nodes[b.a].p, pb = c.nodes[b.b].p;
        const bool aFront = dot(pa - pb, c.frame().fwd) > 0;
        const V3 end = (st.end == 1) == aFront ? pa : pb;
        o.at = end + ((pa + pb) * 0.5 - end) * 0.12, o.atEnd = true;
    }
    return o;
}

std::string Interact::pruneJobs(Sim& sim) {
    std::string said;
    for (Car& c : sim.cars) {
        if (c.remote || c.fieldJobs.empty()) continue;
        const std::vector<Fix> fixes = fixesFor(c);
        for (size_t i = 0; i < c.fieldJobs.size();) {
            Car::FieldJob& j = c.fieldJobs[i];
            bool drop = j.wheel >= (int)c.wheels.size() || (j.wheel >= 0 && c.wheels[j.wheel].detached);
            bool open = false;
            for (const Fix& f : fixes) open |= fieldJobOf(c, f) == (int)i;
            if (!drop) {
                if (j.fixed && open) j.fixed = false, j.done = 0;  // (broken again)
                if (j.kind != Fix::NEW_TIRE && !j.fixed && !open) drop = true;  // fixed some other way (a station, R)
                else if (planFor(sim, c, j.kind, j.wheel, &j).next < 0) drop = true, said = std::string(jobName(j.kind)) + ": ALL DONE";
            }
            if (drop) c.fieldJobs.erase(c.fieldJobs.begin() + i);
            else i++;
        }
    }
    return said;
}

V3 Interact::jackSpot(const Sim& sim, const Car& c, int k) {
    V3 at = c.jackPlaced && c.jackWheel == k ? c.jackFoot : c.jackPoint(k);
    at.y = sim.world.height(at.x, at.z);
    return at;
}

V3 Interact::jackAim(const Sim& sim, const Car& c, int k) {
    // Where you look to work it: the middle of the jack, between its saddle and its standing bar out past the body.
    return jackSpot(sim, c, k) + c.jackOut(k) * (c.jackReach(k) * 0.8) + V3(0, 0.3, 0);
}

Placed Interact::jackModel(const Sim& sim, const Car& c, int k) {
    // Each design's handle is on a different end of it, the saddle sits at its own height, and it lifts so far.
    double handle = -1, saddle0 = 0.30, range = 0.75;  // (+1: the crank is on the model's +X end; -1: the lever or bar is on its -X end)
    if (const mk::ModelDef* d = mk::findModel("jack")) {
        const std::string& n = d->variants[mk::pickedVariant(*d)].name;
        if (n == "SCISSOR JACK") handle = 1, saddle0 = 0.12, range = 0.35;
        else if (n == "BOTTLE JACK") saddle0 = 0.26, range = 0.35;
    }
    const V3 out = c.jackOut(k), foot = jackSpot(sim, c, k);
    Placed pl;
    pl.id = "jack", pl.oriented = true;
    pl.ax[0] = out * handle, pl.ax[1] = V3(0, 1, 0), pl.ax[2] = cross(pl.ax[0], pl.ax[1]);
    const double saddle = c.jackBase + c.jackHeight, lift = clampd((saddle - saddle0) / range, 0, 1);
    pl.p = foot;
    pl.p.y += std::max(-0.08, saddle - (saddle0 + range * lift));  // (a low car: the jack's foot sunk a little; never gapped)
    // (The saddle is at the pad, the bar it climbs out past the body's side: `reach` is the lifting arm's length.)
    pl.set = {{"lift", lift}, {"reach", c.jackReach(k)}};
    return pl;
}

bool Interact::cornerLifted(const Sim& sim, const Car& c, int k) {
    const Wheel& wh = c.wheels[k];
    const V3 hub = hubOf(c, wh);
    const double ground = sim.world.height(hub.x, hub.z);
    if (!wh.mounted) return hub.y - ground > wh.R * 0.92;  // room for a wheel under it
    return hub.y - wh.R - ground > 0.05;
}

InteractOption Interact::pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots, const Kit& kit, const TrunkStock* own, const PartHit* hover, const PickOpts* opts) {
    InteractOption best;
    double bestScore = 1e9;
    const V3 chest = wk.p + V3(0, 1.0, 0);
    const V3 dir = norm(look);
    // A candidate at `at`: scored on how far off your aim it is (to the edge of it: a wheel is a big target), then how far
    // away (a job you can't do yet loses to one you can, close by). The one you have already keeps it a while.
    auto consider = [&](InteractOption o, double reach = kReach, double bias = 0, double distW = 0.15) {
        if (o.radius <= 0) o.radius = prompts::info(o.kind).radius;
        if (opts && opts->nearby && len(o.at - chest) <= std::max(reach, prompts::kMarkerRange)) opts->nearby->push_back(o);
        const double d = len(o.at - eye);
        if (d < 1e-6) return;
        const bool st = opts && opts->sticky && (opts->sticky->kind != IA_NONE || opts->sticky->passive) && o.same(*opts->sticky);
        double score;
        if (!prompts::aimScore(eye, dir, chest, o.at, o.radius, reach, distW * d + (o.blocked ? 0.04 : 0) + bias, st, opts ? opts->stickyBonus : prompts::kStickyBonus, score)) return;
        if (score < bestScore) bestScore = score, best = o;
    };
    // A job that needs tools you may not have, or a corner up off the ground.
    auto needs = [&](InteractOption& o, std::initializer_list<Tool> tools, bool lift, bool lifted) {
        std::string miss;
        for (Tool t : tools)
            if (!kit.has(t)) miss += (miss.empty() ? "" : ", ") + std::string(toolName(t));
        if (!miss.empty()) {
            for (Tool t : tools)
                if (!kit.has(t) && kit.stowedTools[t]) {
                    miss += " (IN THE TRUCK: OPEN THE BACK)";
                    break;
                }
            o.blocked = true, o.prompt += ": NEEDS " + miss;
        }
        else if (lift && !lifted) o.blocked = true, o.prompt += ": JACK THIS CORNER UP FIRST";
    };
    const bool lineHeld = sim.handLine.on() && sim.handLine.car < (int)sim.cars.size();
    const bool hands = wk.carrying.has_value() || wk.holdingChain || lineHeld;
    const bool busy = hands || wk.holdingJack;
    const bool wheelInHands = wk.carrying.has_value() && !wk.carrying->tireOnly, tireInHands = wk.carrying.has_value() && wk.carrying->tireOnly;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.isProp()) continue;
        const bool trunkCar = own && !c.ai && (c.hasGate() || ci == sim.player);  // (its chains, jack and your kit are in the back: the inventory, below)
        if (c.spec->wheelLoss) {
            // Snow chains: in the back on one side (the jack's on the other), onto a tire, off
            // again, back in.
            if (c.spec->snowChains > 0) {
                InteractOption o;
                o.car = ci;
                o.at = c.stowAt(Car::STOW_CHAINS);
                if (trunkCar) {
                } else if (!busy && c.chains > 0) {
                    o.kind = IA_TAKE_CHAIN, o.hold = kTakeChain, o.prompt = "TAKE A SNOW CHAIN (" + std::to_string(c.chains) + " LEFT)";
                    consider(o);
                } else if (wk.holdingChain && c.chains < c.spec->snowChains) {
                    o.kind = IA_STOW_CHAIN, o.hold = kStowChain, o.prompt = "STOW CHAIN";
                    consider(o);
                }
                for (int k = 0; k < (int)c.wheels.size(); k++) {
                    const Wheel& wh = c.wheels[k];
                    if (wh.detached || !wh.mounted || wh.debeaded) continue;
                    InteractOption w;
                    w.car = ci, w.wheel = k;
                    w.at = hubOf(c, wh);
                    if (wk.holdingChain && !wh.chained) {
                        w.kind = IA_FIT_CHAIN, w.hold = kFitChain, w.prompt = "WRAP THE CHAIN ROUND THIS TIRE";
                        consider(w);
                    } else if (!busy && wh.chained) {
                        w.kind = IA_REMOVE_CHAIN, w.hold = kRemoveChain, w.prompt = "REMOVE CHAIN";
                        consider(w);
                    }
                }
            }
            if (!c.spareNodes.empty()) {
                InteractOption o;
                o.car = ci;
                o.at = c.spareAt();
                if (c.hasSpare && !busy) {
                    o.kind = IA_TAKE_SPARE, o.hold = kTakeSpare, o.prompt = withSize("TAKE SPARE", c.spare);
                    consider(o);
                } else if (!c.hasSpare && wheelInHands) {
                    o.kind = IA_STOW_SPARE, o.hold = kStowSpare, o.prompt = withSize("STOW IT AS THE SPARE", *wk.carrying);
                    consider(o);
                }
            }
        }
        // The jack: the truck's own, or yours (and one that's under a corner stays workable).
        if (c.hasJack || kit.has(TOOL_JACK) || c.jackPlaced) {
            InteractOption o;
            o.car = ci;
            o.at = c.stowAt(Car::STOW_JACK);
            if (trunkCar) {
            } else if (!c.jackPlaced && !busy) {
                o.kind = IA_TAKE_JACK, o.hold = kTakeJack, o.prompt = "TAKE JACK";
                consider(o);
            } else if (wk.holdingJack) {
                o.kind = IA_STOW_JACK, o.hold = kStowJack, o.prompt = "STOW JACK";
                consider(o);
            }
            if (wk.holdingJack && !c.jackPlaced)
                for (int k = 0; k < (int)c.wheels.size(); k++) {
                    if (c.wheels[k].detached) continue;
                    InteractOption p;
                    p.car = ci, p.wheel = k, p.kind = IA_PLACE_JACK, p.hold = kPlaceJack, p.prompt = "PLACE JACK UNDER THIS CORNER";
                    p.at = jackAim(sim, c, k);
                    consider(p);
                }
            if (c.jackPlaced && !busy && c.jackWheel >= 0 && c.jackWheel < (int)c.wheels.size()) {
                InteractOption j;
                j.car = ci, j.wheel = c.jackWheel;
                j.at = jackAim(sim, c, c.jackWheel);
                if (c.jackWorked && c.jackHeight <= 0.005) j.kind = IA_REMOVE_JACK, j.hold = kRemoveJack, j.prompt = "REMOVE JACK";
                else if (!cornerLifted(sim, c, c.jackWheel) && c.jackHeight < kJackMax) j.kind = IA_RAISE_JACK, j.continuous = true, j.prompt = "RAISE THE JACK";
                else j.kind = IA_LOWER_JACK, j.continuous = true, j.prompt = "LOWER THE JACK";
                bool working = false;  // (a repair still to do at this corner: the work beside it wins over letting it down)
                if (j.kind == IA_LOWER_JACK && !busy)
                    for (const Fix& f : fixesFor(c)) working |= f.wheel == c.jackWheel;
                consider(j, kReach, working ? 0.25 : 0);
            }
        }
        // The recovery gear: the winch line at the fairlead and a tow strap at either recovery point are grabbed by hand, to walk out
        // to the anchor (the line pays out of the drum as you go); the drum takes it back.
        if (!busy) {
            if ((c.spec->extras & EXTRA_WINCH) && !hasLine(sim, ci, ROPE_WINCH)) {
                InteractOption o;
                o.kind = IA_GRAB_LINE, o.car = ci, o.hook = 0, o.lineKind = ROPE_WINCH, o.hold = kGrabLine;
                o.at = c.hookPoint(0) + V3(0, 0.15, 0);
                o.prompt = "GRAB WINCH LINE";
                consider(o);
            }
            if (!hasLine(sim, ci, ROPE_STRAP))
                for (int h = 0; h < 2; h++) {
                    InteractOption o;
                    o.kind = IA_GRAB_LINE, o.car = ci, o.hook = h, o.lineKind = ROPE_STRAP, o.hold = kGrabLine;
                    o.at = c.hookPoint(h) - V3(0, 0.15, 0);
                    o.prompt = h ? "GRAB TOW STRAP (REAR POINT)" : "GRAB TOW STRAP (FRONT POINT)";
                    consider(o);
                }
        }
        if (lineHeld && sim.handLine.car == ci) {
            InteractOption o;
            o.kind = IA_STOW_LINE, o.car = ci, o.hook = sim.handLine.hook, o.lineKind = sim.handLine.kind, o.hold = kStowLine;
            o.at = c.hookPoint(sim.handLine.hook);
            o.prompt = sim.handLine.kind == ROPE_WINCH ? "SPOOL WINCH LINE BACK" : "STOW TOW STRAP";
            consider(o);
        }
        // The back (trunk.h): E at it opens the inventory (invmenu.h), where you take what you need and put it back.
        if (trunkCar && !wk.carrying.has_value() && !lineHeld) {
            InteractOption g;
            g.car = ci, g.at = trunk::gateAt(c), g.kind = IA_OPEN_INVENTORY;
            const bool held = wk.hand.any() || wk.holdingJack || wk.holdingChain;
            g.prompt = held ? "INVENTORY: PUT BACK " + (wk.holdingJack ? std::string("JACK") : wk.holdingChain ? std::string("SNOW CHAIN") : trunk::name(wk.hand)) : "INVENTORY";
            consider(g, 3.2);
        }
        // Wheels off and on (a lug wrench, the corner up off the ground).
        if (c.spec->wheelLoss)
            for (int k = 0; k < (int)c.wheels.size(); k++) {
                const Wheel& wh = c.wheels[k];
                if (wh.detached) continue;
                InteractOption o;
                o.car = ci, o.wheel = k;
                o.at = hubOf(c, wh);
                const bool lifted = cornerLifted(sim, c, k);
                if (wh.mounted && !busy) {
                    o.kind = IA_REMOVE_WHEEL, o.hold = kRemoveWheel;
                    o.prompt = wh.debeaded ? "REMOVE RIM" : wh.popped ? "REMOVE FLAT" : "REMOVE WHEEL";
                    needs(o, {TOOL_LUG_WRENCH}, true, lifted);
                    consider(o);
                } else if (!wh.mounted && wheelInHands) {
                    o.kind = IA_FIT_WHEEL, o.hold = kFitWheel, o.prompt = withSize("FIT THE WHEEL", *wk.carrying);
                    needs(o, {TOOL_LUG_WRENCH}, true, lifted);
                    consider(o);
                } else if (!wh.mounted && !busy) {  // a repair under way here: what the bare hub's waiting for
                    for (const Car::FieldJob& j : c.fieldJobs) {
                        if (j.wheel != k) continue;
                        const RepairPlan p = planFor(sim, c, j.kind, j.wheel, &j);
                        if (p.next < 0) continue;
                        const RepairStep& st = p.steps[p.next];
                        if (st.type == RepairStep::WHEEL_ON) o.prompt = std::string(jobName(j.kind)) + ": PICK THE WHEEL UP AND PUT IT BACK ON";
                        else if (st.ground) o.prompt = std::string(jobName(j.kind)) + ": " + st.verb;
                        else continue;
                        o.kind = IA_FIT_WHEEL, o.hold = kFitWheel, o.blocked = true;
                        consider(o);
                        break;
                    }
                } else if (wh.mounted && wh.debeaded && tireInHands) {
                    o.kind = IA_RESEAT_TIRE, o.hold = kReseatTire, o.prompt = "MOUNT TIRE TO RIM";
                    needs(o, {TOOL_TIRE_IRONS}, false, lifted);
                    consider(o);
                }
            }
        // Tire pressure: air up with the compressor (or shop air) and let air out at the valves,
        // on foot or in the seat, stopped. Placed on the tire's sidewall (offset from the hub) so
        // aiming at the hub still picks the wheel jobs; lower priority (bias) than wheel work.
        {
            const double speed = len(c.frame().vel);
            if (speed < 0.5 && c.tirePsi < c.spec->tirePsi - 0.5)
                for (int k = 0; k < (int)c.wheels.size(); k++) {
                    const Wheel& wh = c.wheels[k];
                    if (wh.detached) continue;
                    // Sidewall position: offset from the hub along the wheel's out direction.
                    V3 wheelOut = c.nodes[wh.outer].p - c.nodes[wh.inner].p;
                    wheelOut = len(wheelOut) > 1e-6 ? norm(wheelOut) : c.frame().fwd;
                    const V3 sidewall = hubOf(c, wh) + wheelOut * wh.R;
                    InteractOption o;
                    o.car = ci, o.wheel = k, o.kind = IA_AIR_UP_TIRE, o.continuous = true;
                    o.at = sidewall;
                    o.radius = 0.5;
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "AIR UP (%.0f PSI)", c.tirePsi);
                    o.prompt = buf;
                    consider(o, kReach, 0.1);
                    if (c.tirePsi > 12.5) {
                        InteractOption o2;
                        o2.car = ci, o2.wheel = k, o2.kind = IA_AIR_DOWN_TIRE, o2.continuous = true;
                        o2.at = sidewall;
                        o2.radius = 0.5;
                        char buf2[64];
                        std::snprintf(buf2, sizeof buf2, "LET AIR OUT (%.0f PSI)", c.tirePsi);
                        o2.prompt = buf2;
                        consider(o2, kReach, 0.1);
                    }
                }
        }
        // Repairs (fixes.h): in order, a step at a time, each with the right thing in your hand.
        if (!busy)
            for (const Fix& f : fixesFor(c)) {
                InteractOption o = fixOption(sim, wk, ci, f, kit, own);
                if (o.kind == IA_NONE) continue;
                if (hover && hover->ok && hover->car == ci && partMatchesFix(hover->tag, f)) {  // the part you're looking at decides
                    if (!o.atEnd) o.at = hover->at;  // (a shaft's end is still where you work it)
                    consider(o, kReach, -1.0);
                } else {
                    consider(o);
                }
            }
    }
    // Lines that are out: off again, at either end (yours: another player's is theirs to take off).
    if (!busy)
        for (int ri = 0; ri < (int)sim.ropes.size(); ri++) {
            const Rope& r = sim.ropes[ri];
            if (r.a.kind != ANCHOR_CAR || r.a.car < 0 || r.a.car >= (int)sim.cars.size() || sim.cars[r.a.car].remote) continue;
            for (const RopeEnd* e : {&r.a, &r.b}) {
                if (e->kind == ANCHOR_CAR && (e->car < 0 || e->car >= (int)sim.cars.size())) continue;
                InteractOption o;
                o.kind = IA_UNHOOK_LINE, o.rope = ri, o.lineKind = r.kind, o.hold = kUnhookLine;
                o.at = e->kind == ANCHOR_CAR ? sim.cars[e->car].hookPoint(e->hook) : e->p;
                o.prompt = r.kind == ROPE_WINCH ? "UNHOOK THE WINCH LINE" : "UNHOOK THE TOW STRAP";
                consider(o, kLineReach, -0.12);  // (at the fairlead, the line that's out wins over grabbing another)
            }
        }
    // The line in your hands: hook it where you stand (a tree, a boulder, a stake in the ground, a vehicle's recovery point).
    if (lineHeld) {
        const Sim::HandLine& hl = sim.handLine;
        std::vector<WinchTarget> ts;
        if (hl.kind == ROPE_WINCH) {
            V3 lk;
            bool hasLook = false;
            for (double d = 0.4; d < kLineReach + 1.5; d += 0.25) {
                const V3 q = eye + dir * d;
                if (q.y <= sim.world.height(q.x, q.z)) {
                    lk = q, hasLook = true;
                    objshape::WorldHit solid;
                    if (objshape::rayWorld(sim.world, eye, dir, d, solid)) hasLook = false;  // (a boulder or trunk in the way is what you're looking at)
                    break;
                }
            }
            ts = sim.winchTargets(hl.car, hasLook ? &lk : nullptr);
        } else ts = sim.strapTargets(hl.car, hl.hook);
        for (const WinchTarget& t : ts) {
            if (len(t.at - chest) > kLineReach) continue;
            InteractOption o;
            o.kind = IA_HOOK_LINE, o.car = hl.car, o.lineKind = hl.kind, o.hook = hl.hook, o.end = t.end, o.hold = kHookLine, o.at = t.at;
            if (hl.kind == ROPE_WINCH && !t.usable) o.blocked = true, o.prompt = t.why;
            else o.prompt = std::string(hl.kind == ROPE_WINCH ? "HOOK THE WINCH TO THE " : "HOOK THE STRAP TO THE ") + t.what;
            consider(o, kLineReach, t.end.kind == ANCHOR_STAKE ? 0.15 : 0);
        }
    }
    if (!hands && spots)
        for (int si = 0; si < (int)spots->size(); si++) {
            InteractOption o;
            o.kind = IA_OPEN_CACHE, o.spot = si, o.hold = kOpenCache, o.at = (*spots)[si].at + V3(0, 0.5, 0);
            o.prompt = (*spots)[si].prompt;
            consider(o);
        }
    if (!hands)
        for (int li = 0; li < (int)sim.looseWheels.size(); li++) {
            const LooseWheel& lw = sim.looseWheels[li];
            if (lw.remote) continue;
            InteractOption o;
            o.kind = IA_PICK_UP, o.loose = li, o.hold = kPickUp, o.at = lw.p;
            o.prompt = withSize(lw.item.tireOnly ? "PICK UP THE TIRE" : "PICK UP THE WHEEL", lw.item);
            consider(o);
            // A flat off the truck, lying there: a new tire on it, a step at a time (the tire irons or the tire in your hand).
            if (lw.item.popped && !lw.item.tireOnly) {
                const std::vector<RepairStep> steps = tireSteps();
                const int idx = std::max(0, std::min(lw.item.tireWork, (int)steps.size() - 1));
                const RepairStep& st = steps[idx];
                const std::string miss = stepMissing(st, kit, own ? &own->kit : nullptr, handTool(wk), handPart(wk));
                const bool forIt = own ? handTool(wk) == TOOL_TIRE_IRONS || handPart(wk) == PART_TIRE : miss.empty();
                if (forIt) {
                    InteractOption t;
                    t.kind = IA_TIRE_WORK, t.loose = li, t.step = idx, t.tool = st.tool, t.part = st.part, t.hold = st.hold;
                    t.at = lw.p + V3(0, 0.1, 0);
                    t.prompt = std::string("NEW TIRE: ") + st.verb + (st.tool >= 0 ? std::string(" (") + toolName((Tool)st.tool) + ")" : "");
                    if (!miss.empty()) t.blocked = true, t.prompt = "NEW TIRE: " + miss;
                    consider(t, kReach, -0.3);
                }
            }
        }
    // Getting in at a door (carentry.h: yours, anyone's with nobody in it, a buddy's passenger door): J or E, a tap.
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        InteractOption o;
        if (!carentry::option(sim, ci, o)) continue;
        if (wk.carrying && !o.blocked) o.blocked = true, o.prompt += ": PUT WHEEL DOWN FIRST";
        consider(o, kReach + 1.0, 0.03);
    }
    return best;
}

std::string Interact::update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt) {
    std::string said = run(sim, wk, eye, look, holdE, dt);
    const std::string done = pruneJobs(sim);
    if (!done.empty()) toSay = done;  // (after whatever the step itself says)
    // The checklist: the repair you're on, else the part you're looking at, else the nearest under way.
    checklist = RepairChecklist();
    const InteractOption& o = current;
    const Car* oc = o.car >= 0 && o.car < (int)sim.cars.size() ? &sim.cars[o.car] : nullptr;
    if ((o.kind == IA_TIRE_WORK || o.kind == IA_PICK_UP) && o.loose >= 0 && o.loose < (int)sim.looseWheels.size()) {
        const WheelItem& it = sim.looseWheels[o.loose].item;
        if (it.popped && !it.tireOnly) checklist = tireChecklist(it);
    } else if (oc && (o.kind == IA_FIX || (o.kind == IA_REMOVE_WHEEL && o.step >= 0))) {
        const int ji = fieldJobOf(*oc, o.fix);
        checklist = checklistFor(sim, *oc, o.fix.kind, o.fix.wheel, o.fix.axle, ji >= 0 ? &oc->fieldJobs[ji] : nullptr);
    } else if (oc && o.wheel >= 0) {
        for (const Car::FieldJob& j : oc->fieldJobs)
            if (j.wheel == o.wheel) {
                checklist = checklistFor(sim, *oc, j.kind, j.wheel, j.axle, &j);
                break;
            }
        if (!checklist.any())
            for (const Fix& f : fixesFor(*oc))
                if (f.wheel == o.wheel) {
                    checklist = checklistFor(sim, *oc, f.kind, f.wheel, f.axle, nullptr);
                    break;
                }
    }
    if (!checklist.any() && hover.ok && hover.car >= 0 && hover.car < (int)sim.cars.size())
        for (const Fix& f : fixesFor(sim.cars[hover.car]))
            if (partMatchesFix(hover.tag, f)) {
                const Car& hc = sim.cars[hover.car];
                const int ji = fieldJobOf(hc, f);
                checklist = checklistFor(sim, hc, f.kind, f.wheel, f.axle, ji >= 0 ? &hc.fieldJobs[ji] : nullptr);
                break;
            }
    if (!checklist.any()) {
        double best = 12;
        for (const Car& c : sim.cars) {
            if (c.remote || c.fieldJobs.empty()) continue;
            const double d = len(c.frame().com - wk.p);
            if (d < best) best = d, checklist = checklistFor(sim, c, c.fieldJobs[0].kind, c.fieldJobs[0].wheel, c.fieldJobs[0].axle, &c.fieldJobs[0]);
        }
    }
    if (said.empty()) said = toSay, toSay.clear();
    return said;
}

std::string Interact::run(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt) {
    PickOpts po;
    po.sticky = &current;
    po.stickyBonus = holdE && progress > 0 ? prompts::kHoldBonus : prompts::kStickyBonus;  // (working on it: only a clear change of aim lets go)
    nearby.clear();
    po.nearby = &nearby;
    InteractOption o = pick(sim, wk, eye, look, &spots, kit, trunkOn ? &own : nullptr, hover.ok ? &hover : nullptr, &po);
    if (!holdE) waitRelease = false;
    // Working the jack: it goes on while E is held, whatever it turns into on the way.
    if (holdE && progress > 0 && current.continuous && o.car == current.car) o = current;
    if (!o.same(current)) progress = 0;
    const InteractOption prev = current;
    current = o;
    // If we were airing on a previous interaction and now we're not, stop airing.
    if ((prev.kind == IA_AIR_UP_TIRE || prev.kind == IA_AIR_DOWN_TIRE) && prev.car >= 0 && prev.car < (int)sim.cars.size() && !o.same(prev)) {
        sim.cars[prev.car].airing = 0;
    }
    if (!holdE || o.kind == IA_NONE || o.blocked || waitRelease) {
        // Stop airing when we let go (or the interaction ends).
        if ((prev.kind == IA_AIR_UP_TIRE || prev.kind == IA_AIR_DOWN_TIRE) && prev.car >= 0 && prev.car < (int)sim.cars.size()) {
            sim.cars[prev.car].airing = 0;
        }
        progress = 0;
        return "";
    }
    progress += dt;
    if (o.continuous) {
        Car& c = sim.cars[o.car];
        c.wake();
        if (o.kind == IA_RAISE_JACK) {
            c.jackHeight = std::min(kJackMax, c.jackHeight + kJackRate * dt);
            c.jackWorked = true;
            if (cornerLifted(sim, c, o.wheel) || c.jackHeight >= kJackMax) {
                progress = 0, waitRelease = true;
                return c.jackHeight >= kJackMax && !cornerLifted(sim, c, o.wheel) ? "THE JACK'S AS HIGH AS IT GOES" : "CORNER UP";
            }
        } else if (o.kind == IA_LOWER_JACK) {
            c.jackHeight = std::max(0.0, c.jackHeight - kJackRate * dt);
            if (c.jackHeight <= 0) {
                progress = 0, waitRelease = true;
                return "JACK DOWN";
            }
        } else if (o.kind == IA_AIR_UP_TIRE) {
            c.airing = 1;
            if (c.tirePsi >= c.spec->tirePsi - 0.5) {
                c.airing = 0;
                progress = 0, waitRelease = true;
                char buf[64];
                std::snprintf(buf, sizeof buf, "TIRES UP TO %.0f PSI", c.spec->tirePsi);
                return buf;
            }
        } else if (o.kind == IA_AIR_DOWN_TIRE) {
            c.airing = -1;
            if (c.tirePsi <= 12.5) {
                c.airing = 0;
                progress = 0, waitRelease = true;
                return "TIRES AT 12 PSI";
            }
        }
        return "";
    }
    if (progress < o.hold) return "";
    progress = 0;
    if (o.kind == IA_FIX || o.kind == IA_TIRE_WORK || o.kind == IA_GRAB_LINE || o.kind == IA_HOOK_LINE || o.kind == IA_UNHOOK_LINE || o.kind == IA_STOW_LINE)
        waitRelease = true;  // (a step at a time: let go before the next)
    return perform(sim, wk, o);
}

std::string Interact::perform(Sim& sim, Walker& wk, const InteractOption& o) {
    WheelItem it;
    switch (o.kind) {  // (what fills your arms puts down whatever's in your hand)
        case IA_TAKE_SPARE: case IA_REMOVE_WHEEL: case IA_PICK_UP: case IA_TAKE_CHAIN: case IA_REMOVE_CHAIN: case IA_TAKE_JACK: case IA_REMOVE_JACK:
            wk.hand = TrunkRef();
            break;
        default:
            break;
    }
    switch (o.kind) {
        case IA_TAKE_SPARE:
            if (!sim.takeSpare(o.car, it)) return "STOP THE TRUCK FIRST";
            wk.carrying = it;
            return "GOT THE SPARE";
        case IA_STOW_SPARE:
            if (!sim.stowSpare(o.car, *wk.carrying)) return "STOP THE TRUCK FIRST";
            wk.carrying.reset();
            return "SPARE STOWED";
        case IA_REMOVE_WHEEL: {
            if (!sim.removeWheel(o.car, o.wheel, it)) return "STOP THE TRUCK FIRST";
            wk.carrying = it;
            Car& c = sim.cars[o.car];
            if (o.step >= 0) jobFor(c, o.fix);  // (a repair's step: it's under way)
            if (it.popped) {  // a flat off: a new tire's under way
                Fix t{Fix::NEW_TIRE};
                t.wheel = o.wheel;
                jobFor(c, t);
            }
            return "WHEEL OFF";
        }
        case IA_FIT_WHEEL:
        case IA_RESEAT_TIRE:
            if (!sim.fitWheel(o.car, o.wheel, *wk.carrying)) return "STOP THE TRUCK FIRST";
            wk.carrying.reset();
            return o.kind == IA_FIT_WHEEL ? "WHEEL ON" : "TIRE BACK ON THE RIM";
        case IA_PICK_UP:
            if (!sim.pickUpWheel(o.loose, it)) return "";
            wk.carrying = it;
            return "";
        case IA_OPEN_CACHE:
            if (o.spot >= 0 && o.spot < (int)spots.size()) openedSpot = spots[o.spot].id;
            return "";
        case IA_TAKE_CHAIN: {
            Car& c = sim.cars[o.car];
            if (c.chains <= 0) return "";
            c.chains--;
            wk.holdingChain = true;
            return "GOT A SNOW CHAIN";
        }
        case IA_STOW_CHAIN:
            sim.cars[o.car].chains++;
            wk.holdingChain = false;
            return "CHAIN PUT AWAY";
        case IA_FIT_CHAIN: {
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            Wheel& wh = c.wheels[o.wheel];
            if (wh.chained || !wh.mounted) return "";
            wh.chained = true;
            wh.chainStrain = 0;
            wk.holdingChain = false;
            c.wake();
            int on = 0;
            for (const Wheel& w2 : c.wheels) on += w2.chained;
            return "CHAIN ON (" + std::to_string(on) + " OF " + std::to_string(c.wheels.size()) + " TIRES)";
        }
        case IA_REMOVE_CHAIN: {
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            Wheel& wh = c.wheels[o.wheel];
            if (!wh.chained) return "";
            wh.chained = false;
            wk.holdingChain = true;
            return "CHAIN OFF";
        }
        case IA_TAKE_JACK:
            wk.holdingJack = true;
            return "GOT THE JACK";
        case IA_STOW_JACK:
            wk.holdingJack = false;
            return "JACK PUT AWAY";
        case IA_PLACE_JACK: {
            Car& c = sim.cars[o.car];
            c.placeJack(sim.world, o.wheel);
            wk.holdingJack = false;
            return "JACK UNDER";
        }
        case IA_REMOVE_JACK: {
            Car& c = sim.cars[o.car];
            c.jackPlaced = false, c.jackWheel = -1, c.jackHeight = 0;
            wk.holdingJack = true;
            return "GOT THE JACK";
        }
        case IA_ENTER_CAR:
        case IA_RIDE_ALONG:
            enterCar = o.car;
            waitRelease = true;
            return "";
        case IA_OPEN_INVENTORY: {
            openedInventory = o.car;
            waitRelease = true;  // (E is let go before it does anything else)
            return "";
        }
        case IA_TAKE_ITEM:
            wk.hand = o.item;
            return "GOT THE " + trunk::name(o.item);
        case IA_STOW_ITEM:
            wk.hand = TrunkRef();
            return "PUT BACK " + trunk::name(o.item);
        case IA_FIX: {  // a repair's work step, in its turn (fixes.h)
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            const std::vector<RepairStep> steps = repairSteps(o.fix.kind, jobJacks(o.fix.kind, o.fix.wheel), jobWheelOff(c, o.fix.kind, o.fix.wheel));
            if (o.step < 0 || o.step >= (int)steps.size() || steps[o.step].type != RepairStep::WORK) return "";
            int w = 0;
            for (int i = 0; i < o.step; i++) w += steps[i].type == RepairStep::WORK;
            Car::FieldJob& job = jobFor(c, o.fix);
            if (job.done != w) return "";  // (not its turn)
            const RepairStep& st = steps[o.step];
            if (st.part >= 0) {
                if (wk.hand.kind == TK_PART && wk.hand.id == st.part) wk.hand = TrunkRef();  // (the part in your hand went in)
                usedPart = st.part;
            }
            job.done++;
            if (st.removes) {  // the broken one comes off: down on the ground beside the car
                const Frame fr = c.frame();
                const V3 fwd = norm(V3(fr.fwd.x, 0, fr.fwd.z)), left(fwd.z, 0, -fwd.x);
                const double side = dot(o.fix.at - fr.com, left) < 0 ? -1.0 : 1.0;
                Sim::DroppedPart d;
                d.part = o.fix.part;
                d.p = fr.com + left * (side * (c.spec->width / 2 + 0.8)) + fwd * dot(o.fix.at - fr.com, fwd);
                d.p.y = sim.world.height(d.p.x, d.p.z) + 0.04;
                d.yaw = std::atan2(-fwd.z, fwd.x) + 1.2 * (double)(sim.droppedParts.size() % 5);
                sim.droppedParts.push_back(d);
                if (sim.droppedParts.size() > 12) sim.droppedParts.erase(sim.droppedParts.begin());
            }
            if (st.fixes) {
                applyFix(c, o.fix);
                job.fixed = true;
                return std::string(st.label) + ": " + jobName(o.fix.kind) + " FIXED";
            }
            c.wake();
            return st.label;
        }
        case IA_TIRE_WORK: {  // a new tire onto a wheel lying there
            if (o.loose < 0 || o.loose >= (int)sim.looseWheels.size()) return "";
            WheelItem& lw = sim.looseWheels[o.loose].item;
            const std::vector<RepairStep> steps = tireSteps();
            if (!lw.popped || lw.tireWork != o.step || o.step < 0 || o.step >= (int)steps.size()) return "";
            const RepairStep& st = steps[o.step];
            if (st.part >= 0) {
                if (wk.hand.kind == TK_PART && wk.hand.id == st.part) wk.hand = TrunkRef();
                usedPart = st.part;
            }
            lw.tireWork++;
            if (st.fixes) {
                lw.popped = false, lw.tireWork = 0;
                return "NEW TIRE ON: MOUNT WHEEL";
            }
            return st.label;
        }
        case IA_GRAB_LINE: {
            const std::string why = sim.takeLine(o.car, o.lineKind, o.hook, wk.p + V3(0, 1.0, 0));
            if (!why.empty()) return why;
            return o.lineKind == ROPE_WINCH ? "GOT THE WINCH LINE: WALK IT TO AN ANCHOR" : "GOT THE TOW STRAP: WALK ITS END TO THE OTHER VEHICLE";
        }
        case IA_HOOK_LINE:
            return sim.hookHandLine(o.end);
        case IA_UNHOOK_LINE:
            return sim.unhookRope(o.rope);
        case IA_STOW_LINE:
            sim.dropHandLine();
            return o.lineKind == ROPE_WINCH ? "WINCH LINE SPOOLED BACK" : "TOW STRAP PUT BACK";
        default:
            return "";
    }
}


InteractOption Interact::takeOption(const Car& c, int ci, const TrunkItem& it) {
    InteractOption o;
    if (it.count - it.taken <= 0) return o;
    o.car = ci, o.item = it.ref;
    switch (it.ref.kind) {
        case TK_JACK: o.kind = IA_TAKE_JACK, o.hold = kTakeJack, o.prompt = "TAKE " + it.name; break;
        case TK_CHAINS: o.kind = IA_TAKE_CHAIN, o.hold = kTakeChain, o.prompt = "TAKE A SNOW CHAIN (" + std::to_string(c.chains) + " LEFT)"; break;
        default:
            o.kind = IA_TAKE_ITEM, o.hold = kTakeItem, o.prompt = "TAKE " + it.name;
            if (!it.endless && it.count - it.taken > 1) o.prompt += " (" + std::to_string(it.count - it.taken) + " IN THE BACK)";
    }
    return o;
}

InteractOption Interact::stowOption(const Car& c, int ci, const Walker& wk) {
    InteractOption o;
    o.car = ci;
    if (wk.carrying.has_value()) return o;
    if (wk.holdingJack) o.kind = IA_STOW_JACK, o.hold = kStowJack, o.prompt = "STOW JACK";
    else if (wk.holdingChain && c.chains < std::max(c.spec->snowChains, 4)) o.kind = IA_STOW_CHAIN, o.hold = kStowChain, o.prompt = "STOW CHAIN";
    else if (wk.hand.any() && !wk.holdingChain) o.kind = IA_STOW_ITEM, o.hold = kStowItem, o.item = wk.hand, o.prompt = "PUT BACK " + trunk::name(wk.hand);
    return o;
}

std::string Interact::putBackInBack(Sim& sim, Walker& wk, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return "";
    const InteractOption o = stowOption(sim.cars[car], car, wk);
    return o.kind == IA_NONE ? "" : perform(sim, wk, o);
}

std::string Interact::takeFromBack(Sim& sim, Walker& wk, int car, const TrunkStock& stock, TrunkRef ref) {
    if (car < 0 || car >= (int)sim.cars.size() || wk.carrying.has_value()) return "";
    if (sim.handLine.on()) return "LET GO OF THE LINE FIRST (G)";
    const Car& c = sim.cars[car];
    const bool inHand = (ref.kind == TK_JACK && wk.holdingJack) || (ref.kind == TK_CHAINS && wk.holdingChain) || (ref.any() && wk.hand == ref);
    if (inHand) return putBackInBack(sim, wk, car);  // (the one you hold: back it goes)
    const bool holding = wk.hand.any() || wk.holdingJack || wk.holdingChain;
    if (holding) {
        putBackInBack(sim, wk, car);
        if (wk.hand.any() || wk.holdingJack || wk.holdingChain) return "PUT WHAT YOU HOLD BACK FIRST";
    }
    for (const TrunkItem& it : trunk::wholeLoad(c, stock, car == sim.player, wk.hand, wk.holdingJack, wk.holdingChain)) {
        if (it.ref != ref) continue;
        const InteractOption o = takeOption(c, car, it);
        if (o.kind == IA_NONE) return it.ref.kind == TK_JACK ? "THE JACK IS OUT ALREADY" : "NONE LEFT IN THE BACK";
        return perform(sim, wk, o);
    }
    return "";
}

bool Interact::drop(Sim& sim, Walker& wk, const V3& look) {
    if (!wk.carrying) return false;
    sim.dropWheel(*wk.carrying, wk.p, look);
    wk.carrying.reset();
    return true;
}

bool Interact::dropJack(Sim& /*sim*/, Walker& wk) {
    if (!wk.holdingJack) return false;
    wk.holdingJack = false;
    return true;
}
