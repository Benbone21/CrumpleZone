// On foot, hands on (interact.h): the spare, the wheels, and the hold-E jobs.
#include "interact.h"

#include <cmath>

#include "walker.h"

namespace {

// Hold times (s): the work each job is. Shortened for responsiveness.
constexpr double kTakeSpare = 3, kStowSpare = 3, kRemoveWheel = 4, kFitWheel = 4, kReseatTire = 10, kPickUp = 1;
constexpr double kTakeJack = 2, kPlaceJack = 3, kUseJack = 4, kLowerJack = 3, kRemoveJack = 2;
// Chains: lay it out, drive-free wrap round the tread, hook and tension it (a few minutes in
// real life; shortened like the rest).
constexpr double kTakeChain = 1.5, kFitChain = 8, kRemoveChain = 5, kStowChain = 1.5;
constexpr double kOpenCache = 2;  // prise the lid off a supply cache
constexpr double kReach = 2.5;           // m from your chest
constexpr double kCone = 35 * M_PI / 180;  // off your line of sight

std::string withSize(const char* what, const WheelItem& it) { return std::string(what) + " (" + it.sizeName() + ")"; }

// The jack lifts a corner off the ground so the wheel can be removed or fitted.
// A corner is "lifted" when the tire no longer touches the ground.
bool isCornerLifted(const Car& c, int wi) {
    const Wheel& wh = c.wheels[wi];
    if (!wh.mounted) return true;  // no wheel = lifted
    const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
    const V3 out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    const V3 contact = hub - out * wh.R;
    return contact.y > 0.05;  // 5 cm above ground
}

}  // namespace

InteractOption Interact::pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots) {
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
    const bool hands = wk.carrying.has_value() || wk.holdingChain;
    const bool wheelInHands = hands && !wk.carrying->tireOnly, tireInHands = hands && wk.carrying->tireOnly;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.isProp() || !c.spec->wheelLoss) continue;
        // Snow chains: out of the back (where the spare and the tools are), onto a tire, off
        // again, back in.
        if (c.spec->snowChains > 0) {
            InteractOption o;
            o.car = ci;
            o.at = c.spareNodes.empty() ? c.frame().com - c.frame().fwd * (c.spec->length * 0.5) : c.spareAt();
            if (!hands && !wk.holdingJack && c.chains > 0) {
                o.kind = IA_TAKE_CHAIN, o.hold = kTakeChain, o.prompt = "TAKE A SNOW CHAIN (" + std::to_string(c.chains) + " LEFT)";
                consider(o);
            } else if (wk.holdingChain && c.chains < c.spec->snowChains) {
                o.kind = IA_STOW_CHAIN, o.hold = kStowChain, o.prompt = "PUT THE CHAIN BACK";
                consider(o);
            }
            for (int k = 0; k < (int)c.wheels.size(); k++) {
                const Wheel& wh = c.wheels[k];
                if (wh.detached || !wh.mounted || wh.debeaded) continue;
                InteractOption w;
                w.car = ci, w.wheel = k;
                w.at = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
                if (wk.holdingChain && !wh.chained) {
                    w.kind = IA_FIT_CHAIN, w.hold = kFitChain, w.prompt = "WRAP THE CHAIN ROUND THIS TIRE";
                    consider(w);
                } else if (!hands && !wk.holdingJack && wh.chained) {
                    w.kind = IA_REMOVE_CHAIN, w.hold = kRemoveChain, w.prompt = "TAKE THE CHAIN OFF";
                    consider(w);
                }
            }
        }
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
        // Jack interactions.
        if (c.hasJack && !wk.holdingJack && !hands) {
            InteractOption o;
            o.car = ci;
            o.at = c.spareAt();  // jack is in the same place as the spare
            o.kind = IA_TAKE_JACK;
            o.hold = kTakeJack;
            o.prompt = "TAKE THE JACK";
            consider(o);
        }
        if (wk.holdingJack) {
            for (int k = 0; k < (int)c.wheels.size(); k++) {
                const Wheel& wh = c.wheels[k];
                if (wh.detached) continue;
                InteractOption o;
                o.car = ci;
                o.wheel = k;
                o.at = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
                if (!c.jackPlaced) {
                    o.kind = IA_PLACE_JACK;
                    o.hold = kPlaceJack;
                    o.prompt = "PLACE THE JACK";
                    consider(o);
                } else if (c.jackWheel == k && c.jackHeight < 0.1) {
                    // Jack is under this wheel but not lifting yet.
                    o.kind = IA_USE_JACK;
                    o.hold = kUseJack;
                    o.prompt = "LIFT WITH THE JACK";
                    consider(o);
                } else if (c.jackWheel == k && c.jackHeight > 0.1 && !isCornerLifted(c, k)) {
                    // Jack is under this wheel and lifting.
                    o.kind = IA_USE_JACK;
                    o.hold = kUseJack;
                    o.prompt = "LIFT MORE";
                    consider(o);
                } else if (c.jackWheel == k && isCornerLifted(c, k)) {
                    // Corner is lifted, can lower or remove wheel.
                    if (!wh.mounted && wheelInHands) {
                        o.kind = IA_FIT_WHEEL;
                        o.hold = kFitWheel;
                        o.prompt = withSize("FIT THE WHEEL", *wk.carrying);
                        consider(o);
                    } else if (wh.mounted) {
                        o.kind = IA_REMOVE_WHEEL;
                        o.hold = kRemoveWheel;
                        o.prompt = wh.debeaded ? "TAKE THE BARE RIM OFF" : wh.popped ? "TAKE THE FLAT OFF" : "TAKE THE WHEEL OFF";
                        consider(o);
                    } else {
                        o.kind = IA_LOWER_JACK;
                        o.hold = kLowerJack;
                        o.prompt = "LOWER THE JACK";
                        consider(o);
                    }
                }
            }
        }
        if (c.jackPlaced && wk.holdingJack) {
            InteractOption o;
            o.car = ci;
            o.at = c.spareAt();
            if (c.jackHeight > 0.1) {
                o.kind = IA_LOWER_JACK;
                o.hold = kLowerJack;
                o.prompt = "LOWER THE JACK";
            } else {
                o.kind = IA_REMOVE_JACK;
                o.hold = kRemoveJack;
                o.prompt = "REMOVE THE JACK";
            }
            consider(o);
        }
        for (int k = 0; k < (int)c.wheels.size(); k++) {
            const Wheel& wh = c.wheels[k];
            if (wh.detached) continue;
            InteractOption o;
            o.car = ci, o.wheel = k;
            o.at = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
            if (wh.mounted && !hands && !wk.holdingJack) {
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
        }
    return best;
}

std::string Interact::update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt) {
    const InteractOption o = pick(sim, wk, eye, look, &spots);
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
        case IA_TAKE_JACK:
            wk.holdingJack = true;
            return "GOT THE JACK";
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
        case IA_PLACE_JACK:
            sim.cars[o.car].jackPlaced = true;
            sim.cars[o.car].jackWheel = o.wheel;
            sim.cars[o.car].jackHeight = 0;
            wk.holdingJack = false;
            return "JACK PLACED";
        case IA_USE_JACK: {
            Car& c = sim.cars[o.car];
            if (!c.jackPlaced) return "PLACE THE JACK FIRST";
            if (c.jackWheel != o.wheel) return "USE THE JACK UNDER THIS WHEEL";
            // Lift the corner by moving the hub up.
            Wheel& wh = c.wheels[o.wheel];
            const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
            const V3 lift = V3(0, 0.02, 0);  // 2 cm per use
            for (Node& n : c.nodes) n.p += lift;
            c.jackHeight += 0.02;
            c.wake();
            return "LIFTING";
        }
        case IA_LOWER_JACK: {
            Car& c = sim.cars[o.car];
            if (!c.jackPlaced) return "";
            // Lower the corner.
            const V3 drop = V3(0, -0.02, 0);  // 2 cm per use
            for (Node& n : c.nodes) n.p += drop;
            c.jackHeight = std::max(0.0, c.jackHeight - 0.02);
            c.wake();
            return "LOWERING";
        }
        case IA_REMOVE_JACK:
            sim.cars[o.car].jackPlaced = false;
            sim.cars[o.car].jackWheel = -1;
            sim.cars[o.car].jackHeight = 0;
            wk.holdingJack = true;
            return "JACK REMOVED";
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

bool Interact::dropJack(Sim& sim, Walker& wk) {
    if (!wk.holdingJack) return false;
    wk.holdingJack = false;
    return true;
}
