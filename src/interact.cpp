// On foot, hands on (interact.h): the spare, the wheels, and the hold-E jobs.
#include "interact.h"

#include <cmath>

#include "walker.h"

namespace {

// Hold times (s): the work each job is.
constexpr double kTakeSpare = 5, kStowSpare = 5, kRemoveWheel = 8, kFitWheel = 10, kReseatTire = 20, kPickUp = 1;
constexpr double kReach = 2.5;           // m from your chest
constexpr double kCone = 35 * M_PI / 180;  // off your line of sight

std::string withSize(const char* what, const WheelItem& it) { return std::string(what) + " (" + it.sizeName() + ")"; }

}  // namespace

InteractOption Interact::pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look) {
    InteractOption best;
    double bestScore = 1e9;
    const V3 chest = wk.p + V3(0, 1.0, 0);
    const V3 dir = norm(look);
    // A candidate at `at`: scored on how far off your aim it is, then how far away.
    auto consider = [&](InteractOption o) {
        if (len(o.at - chest) > kReach) return;
        const V3 to = o.at - eye;
        const double d = len(to);
        if (d < 1e-6) return;
        const double off = std::acos(clampd(dot(to / d, dir), -1, 1));
        if (off > kCone) return;
        const double score = off + 0.15 * d;
        if (score < bestScore) bestScore = score, best = o;
    };
    const bool hands = wk.carrying.has_value();
    const bool wheelInHands = hands && !wk.carrying->tireOnly, tireInHands = hands && wk.carrying->tireOnly;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.isProp() || !c.spec->wheelLoss) continue;
        if (!c.spareNodes.empty()) {
            InteractOption o;
            o.car = ci;
            o.at = c.spareAt();
            if (c.hasSpare && !hands) {
                o.kind = IA_TAKE_SPARE, o.hold = kTakeSpare, o.prompt = withSize("TAKE THE SPARE", c.spare);
                consider(o);
            } else if (!c.hasSpare && wheelInHands) {
                o.kind = IA_STOW_SPARE, o.hold = kStowSpare, o.prompt = withSize("STOW IT AS THE SPARE", *wk.carrying);
                consider(o);
            }
        }
        for (int k = 0; k < (int)c.wheels.size(); k++) {
            const Wheel& wh = c.wheels[k];
            if (wh.detached) continue;
            InteractOption o;
            o.car = ci, o.wheel = k;
            o.at = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
            if (wh.mounted && !hands) {
                o.kind = IA_REMOVE_WHEEL, o.hold = kRemoveWheel;
                o.prompt = wh.debeaded ? "TAKE THE BARE RIM OFF" : wh.popped ? "TAKE THE FLAT OFF" : "TAKE THE WHEEL OFF";
                consider(o);
            } else if (!wh.mounted && wheelInHands) {
                o.kind = IA_FIT_WHEEL, o.hold = kFitWheel, o.prompt = withSize("FIT THE WHEEL", *wk.carrying);
                consider(o);
            } else if (wh.mounted && wh.debeaded && tireInHands) {
                o.kind = IA_RESEAT_TIRE, o.hold = kReseatTire, o.prompt = "PUT THE TIRE BACK ON THE RIM";
                consider(o);
            }
        }
    }
    if (!hands)
        for (int li = 0; li < (int)sim.looseWheels.size(); li++) {
            const LooseWheel& lw = sim.looseWheels[li];
            if (lw.remote) continue;
            InteractOption o;
            o.kind = IA_PICK_UP, o.loose = li, o.hold = kPickUp, o.at = lw.p;
            o.prompt = withSize(lw.item.tireOnly ? "PICK UP THE TIRE" : "PICK UP THE WHEEL", lw.item);
            consider(o);
        }
    return best;
}

std::string Interact::update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt) {
    const InteractOption o = pick(sim, wk, eye, look);
    if (!o.same(current)) progress = 0;
    current = o;
    if (!holdE || o.kind == IA_NONE) {
        progress = 0;
        return "";
    }
    progress += dt;
    if (progress < o.hold) return "";
    progress = 0;
    WheelItem it;
    switch (o.kind) {
        case IA_TAKE_SPARE:
            if (!sim.takeSpare(o.car, it)) return "STOP THE TRUCK FIRST";
            wk.carrying = it;
            return "GOT THE SPARE";
        case IA_STOW_SPARE:
            if (!sim.stowSpare(o.car, *wk.carrying)) return "STOP THE TRUCK FIRST";
            wk.carrying.reset();
            return "SPARE STOWED";
        case IA_REMOVE_WHEEL:
            if (!sim.removeWheel(o.car, o.wheel, it)) return "STOP THE TRUCK FIRST";
            wk.carrying = it;
            return "WHEEL OFF";
        case IA_FIT_WHEEL:
        case IA_RESEAT_TIRE:
            if (!sim.fitWheel(o.car, o.wheel, *wk.carrying)) return "STOP THE TRUCK FIRST";
            wk.carrying.reset();
            return o.kind == IA_FIT_WHEEL ? "WHEEL ON" : "TIRE BACK ON THE RIM";
        case IA_PICK_UP:
            if (!sim.pickUpWheel(o.loose, it)) return "";
            wk.carrying = it;
            return "";
        default:
            return "";
    }
}

bool Interact::drop(Sim& sim, Walker& wk, const V3& look) {
    if (!wk.carrying) return false;
    sim.dropWheel(*wk.carrying, wk.p, look);
    wk.carrying.reset();
    return true;
}
