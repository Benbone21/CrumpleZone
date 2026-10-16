// On foot, hands on (interact.h): the spare, the wheels, and the hold-E jobs.
#include "interact.h"

#include <cmath>

#include "walker.h"

namespace {

// Hold times (s): the work each job is, kept short (the game shows the job being done).
constexpr double kTakeSpare = 1.0, kStowSpare = 1.0, kRemoveWheel = 2.5, kFitWheel = 2.5, kReseatTire = 3.5, kPickUp = 0.5;
constexpr double kTakeJack = 0.6, kStowJack = 0.6, kPlaceJack = 1.0, kRemoveJack = 0.8;
constexpr double kJackRate = 0.14;  // m/s the saddle moves while E is held
constexpr double kTakeChain = 0.6, kFitChain = 3.0, kRemoveChain = 2.0, kStowChain = 0.6;
constexpr double kOpenCache = 1.5;  // prise the lid off a supply cache
constexpr double kReach = 2.5;           // m from your chest
constexpr double kCone = 35 * M_PI / 180;  // off your line of sight

std::string withSize(const char* what, const WheelItem& it) { return std::string(what) + " (" + it.sizeName() + ")"; }

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

}  // namespace

V3 Interact::jackSpot(const Sim& sim, const Car& c, int k) {
    const Wheel& wh = c.wheels[k];
    const V3 hub = hubOf(c, wh), out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    V3 at = hub - out * (wh.width * 0.5 + 0.25);
    at.y = sim.world.height(at.x, at.z);
    return at;
}

bool Interact::cornerLifted(const Sim& sim, const Car& c, int k) {
    const Wheel& wh = c.wheels[k];
    const V3 hub = hubOf(c, wh);
    const double ground = sim.world.height(hub.x, hub.z);
    if (!wh.mounted) return hub.y - ground > wh.R * 0.92;  // room for a wheel under it
    return hub.y - wh.R - ground > 0.03;
}

InteractOption Interact::pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots, const Kit& kit) {
    InteractOption best;
    double bestScore = 1e9;
    const V3 chest = wk.p + V3(0, 1.0, 0);
    const V3 dir = norm(look);
    // A candidate at `at`: scored on how far off your aim it is, then how far away (a job you
    // can't do yet loses to one you can, close by).
    auto consider = [&](InteractOption o) {
        if (len(o.at - chest) > kReach) return;
        const V3 to = o.at - eye;
        const double d = len(to);
        if (d < 1e-6) return;
        const double off = std::acos(clampd(dot(to / d, dir), -1, 1));
        if (off > kCone) return;
        const double score = off + 0.15 * d + (o.blocked ? 0.04 : 0);
        if (score < bestScore) bestScore = score, best = o;
    };
    // A job that needs tools you may not have, or a corner up off the ground.
    auto needs = [&](InteractOption& o, std::initializer_list<Tool> tools, bool lift, bool lifted) {
        std::string miss;
        for (Tool t : tools)
            if (!kit.has(t)) miss += (miss.empty() ? "" : ", ") + std::string(toolName(t));
        if (!miss.empty()) o.blocked = true, o.prompt += ": NEEDS " + miss;
        else if (lift && !lifted) o.blocked = true, o.prompt += ": JACK THIS CORNER UP FIRST";
    };
    const bool hands = wk.carrying.has_value() || wk.holdingChain;
    const bool busy = hands || wk.holdingJack;
    const bool wheelInHands = wk.carrying.has_value() && !wk.carrying->tireOnly, tireInHands = wk.carrying.has_value() && wk.carrying->tireOnly;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.isProp()) continue;
        if (c.spec->wheelLoss) {
            // Snow chains: in the back on one side (the jack's on the other), onto a tire, off
            // again, back in.
            if (c.spec->snowChains > 0) {
                InteractOption o;
                o.car = ci;
                o.at = c.stowAt(Car::STOW_CHAINS);
                if (!busy && c.chains > 0) {
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
                    w.at = hubOf(c, wh);
                    if (wk.holdingChain && !wh.chained) {
                        w.kind = IA_FIT_CHAIN, w.hold = kFitChain, w.prompt = "WRAP THE CHAIN ROUND THIS TIRE";
                        consider(w);
                    } else if (!busy && wh.chained) {
                        w.kind = IA_REMOVE_CHAIN, w.hold = kRemoveChain, w.prompt = "TAKE THE CHAIN OFF";
                        consider(w);
                    }
                }
            }
            if (!c.spareNodes.empty()) {
                InteractOption o;
                o.car = ci;
                o.at = c.spareAt();
                if (c.hasSpare && !busy) {
                    o.kind = IA_TAKE_SPARE, o.hold = kTakeSpare, o.prompt = withSize("TAKE THE SPARE", c.spare);
                    consider(o);
                } else if (!c.hasSpare && wheelInHands) {
                    o.kind = IA_STOW_SPARE, o.hold = kStowSpare, o.prompt = withSize("STOW IT AS THE SPARE", *wk.carrying);
                    consider(o);
                }
            }
        }
        // The jack: the truck's own, or yours.
        if (c.hasJack || kit.has(TOOL_JACK)) {
            InteractOption o;
            o.car = ci;
            o.at = c.stowAt(Car::STOW_JACK);
            if (!c.jackPlaced && !busy) {
                o.kind = IA_TAKE_JACK, o.hold = kTakeJack, o.prompt = "TAKE THE JACK";
                consider(o);
            } else if (wk.holdingJack) {
                o.kind = IA_STOW_JACK, o.hold = kStowJack, o.prompt = "PUT THE JACK AWAY";
                consider(o);
            }
            if (wk.holdingJack && !c.jackPlaced)
                for (int k = 0; k < (int)c.wheels.size(); k++) {
                    if (c.wheels[k].detached) continue;
                    InteractOption p;
                    p.car = ci, p.wheel = k, p.kind = IA_PLACE_JACK, p.hold = kPlaceJack, p.prompt = "PUT THE JACK UNDER THIS CORNER";
                    p.at = jackSpot(sim, c, k) + V3(0, 0.3, 0);
                    consider(p);
                }
            if (c.jackPlaced && !busy && c.jackWheel >= 0 && c.jackWheel < (int)c.wheels.size()) {
                InteractOption j;
                j.car = ci, j.wheel = c.jackWheel;
                j.at = jackSpot(sim, c, c.jackWheel) + V3(0, 0.3, 0);
                if (c.jackWorked && c.jackHeight <= 0.005) j.kind = IA_REMOVE_JACK, j.hold = kRemoveJack, j.prompt = "TAKE THE JACK OUT";
                else if (!cornerLifted(sim, c, c.jackWheel) && c.jackHeight < kJackMax) j.kind = IA_RAISE_JACK, j.continuous = true, j.prompt = "RAISE THE JACK";
                else j.kind = IA_LOWER_JACK, j.continuous = true, j.prompt = "LOWER THE JACK";
                consider(j);
            }
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
                    o.prompt = wh.debeaded ? "TAKE THE BARE RIM OFF" : wh.popped ? "TAKE THE FLAT OFF" : "TAKE THE WHEEL OFF";
                    needs(o, {TOOL_LUG_WRENCH}, true, lifted);
                    consider(o);
                } else if (!wh.mounted && wheelInHands) {
                    o.kind = IA_FIT_WHEEL, o.hold = kFitWheel, o.prompt = withSize("FIT THE WHEEL", *wk.carrying);
                    needs(o, {TOOL_LUG_WRENCH}, true, lifted);
                    consider(o);
                } else if (wh.mounted && wh.debeaded && tireInHands) {
                    o.kind = IA_RESEAT_TIRE, o.hold = kReseatTire, o.prompt = "PUT THE TIRE BACK ON THE RIM";
                    needs(o, {TOOL_TIRE_IRONS}, false, lifted);
                    consider(o);
                }
            }
        // Repairs (fixes.h): your tools and a part.
        if (!busy)
            for (const Fix& f : fixesFor(c)) {
                InteractOption o;
                o.car = ci, o.wheel = f.wheel, o.kind = IA_FIX, o.fix = f, o.at = f.at, o.hold = f.hold;
                const bool lifted = f.wheel >= 0 && cornerLifted(sim, c, f.wheel);
                const std::string miss = missingFor(f, kit, lifted);
                o.prompt = f.what;
                if (!miss.empty()) o.blocked = true, o.prompt += ": " + miss;
                else if (!kit.unlimited) o.prompt += " (" + std::to_string(kit.parts[f.part]) + " " + repairPartName(f.part) + (kit.parts[f.part] == 1 ? ")" : "S)");
                consider(o);
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
    InteractOption o = pick(sim, wk, eye, look, &spots, kit);
    if (!holdE) waitRelease = false;
    // Working the jack: it goes on while E is held, whatever it turns into on the way.
    if (holdE && progress > 0 && current.continuous && o.car == current.car) o = current;
    if (!o.same(current)) progress = 0;
    current = o;
    if (!holdE || o.kind == IA_NONE || o.blocked || waitRelease) {
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
        } else {
            c.jackHeight = std::max(0.0, c.jackHeight - kJackRate * dt);
            if (c.jackHeight <= 0) {
                progress = 0, waitRelease = true;
                return "JACK DOWN";
            }
        }
        return "";
    }
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
            const Wheel& wh = c.wheels[o.wheel];
            const V3 hub = hubOf(c, wh);
            c.jackPlaced = true, c.jackWheel = o.wheel, c.jackHeight = 0, c.jackWorked = false;
            c.jackBase = hub.y - sim.world.height(hub.x, hub.z) - 0.02;  // (the saddle wound up to just under the axle)
            wk.holdingJack = false;
            return "JACK UNDER";
        }
        case IA_REMOVE_JACK: {
            Car& c = sim.cars[o.car];
            c.jackPlaced = false, c.jackWheel = -1, c.jackHeight = 0;
            wk.holdingJack = true;
            return "GOT THE JACK";
        }
        case IA_FIX: {
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            if (!applyFix(c, o.fix)) return "";
            usedPart = o.fix.part;
            return std::string(repairPartName(o.fix.part)) + " FITTED";
        }
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
