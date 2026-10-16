// On foot, hands on (interact.h): the spare, the wheels, and the hold-E jobs.
#include "interact.h"

#include <cmath>

#include "modelkit.h"
#include "walker.h"

namespace {

// Hold times (s): the work each job is, kept short (the game shows the job being done). The wheel jobs are
// five nuts a go at about two ratchet swings each (handwork.cpp), so they run to nearly 6 s.
constexpr double kTakeSpare = 1.0, kStowSpare = 1.0, kRemoveWheel = 5.8, kFitWheel = 5.8, kReseatTire = 3.5, kPickUp = 0.5;
constexpr double kTakeJack = 0.6, kStowJack = 0.6, kPlaceJack = 1.0, kRemoveJack = 0.8;
constexpr double kJackRate = 0.14;  // m/s the saddle moves while E is held
constexpr double kTakeChain = 0.6, kFitChain = 3.0, kRemoveChain = 2.0, kStowChain = 0.6;
constexpr double kOpenCache = 1.5;  // prise the lid off a supply cache
constexpr double kOpenGate = 0.5, kTakeItem = 0.5, kStowItem = 0.5;  // the back, and things in and out of it
constexpr double kReach = 2.5;           // m from your chest
constexpr double kCone = 35 * M_PI / 180;  // off your line of sight

std::string withSize(const char* what, const WheelItem& it) { return std::string(what) + " (" + it.sizeName() + ")"; }

V3 hubOf(const Car& c, const Wheel& wh) { return (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5; }

}  // namespace

V3 Interact::jackSpot(const Sim& sim, const Car& c, int k) {
    V3 at = c.jackPoint(k);
    at.y = sim.world.height(at.x, at.z);
    return at;
}

Placed Interact::jackModel(const Sim& sim, const Car& c, int k) {
    // Each design's handle is on a different end of it, and the saddle sits at its own height.
    double handle = -1, saddle0 = 0.30;  // (+1: the crank is on the model's +X end; -1: the lever or bar is on its -X end)
    if (const mk::ModelDef* d = mk::findModel("jack")) {
        const std::string& n = d->variants[mk::pickedVariant(*d)].name;
        if (n == "SCISSOR JACK") handle = 1, saddle0 = 0.12;
        else if (n == "BOTTLE JACK") saddle0 = 0.26;
    }
    const V3 out = c.jackOut(k), foot = jackSpot(sim, c, k);
    Placed pl;
    pl.id = "jack", pl.oriented = true;
    pl.ax[0] = out * handle, pl.ax[1] = V3(0, 1, 0), pl.ax[2] = cross(pl.ax[0], pl.ax[1]);
    const double saddle = c.jackBase + c.jackHeight, lift = clampd((saddle - saddle0) / 0.35, 0, 1);
    pl.p = foot;
    pl.p.y += std::max(-0.08, saddle - (saddle0 + 0.35 * lift));  // (a low car: the jack's foot sunk a little; never gapped)
    pl.set = {{"lift", lift}};
    return pl;
}

bool Interact::cornerLifted(const Sim& sim, const Car& c, int k) {
    const Wheel& wh = c.wheels[k];
    const V3 hub = hubOf(c, wh);
    const double ground = sim.world.height(hub.x, hub.z);
    if (!wh.mounted) return hub.y - ground > wh.R * 0.92;  // room for a wheel under it
    return hub.y - wh.R - ground > 0.05;
}

InteractOption Interact::pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots, const Kit& kit, const TrunkStock* own, const PartHit* hover) {
    InteractOption best;
    double bestScore = 1e9;
    const V3 chest = wk.p + V3(0, 1.0, 0);
    const V3 dir = norm(look);
    // A candidate at `at`: scored on how far off your aim it is, then how far away (a job you
    // can't do yet loses to one you can, close by).
    auto consider = [&](InteractOption o, double reach = kReach, double bias = 0, double distW = 0.15) {
        if (len(o.at - chest) > reach) return;
        const V3 to = o.at - eye;
        const double d = len(to);
        if (d < 1e-6) return;
        const double off = std::acos(clampd(dot(to / d, dir), -1, 1));
        if (off > kCone) return;
        const double score = off + distW * d + (o.blocked ? 0.04 : 0) + bias;
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
    const bool hands = wk.carrying.has_value() || wk.holdingChain;
    const bool busy = hands || wk.holdingJack;
    const bool wheelInHands = wk.carrying.has_value() && !wk.carrying->tireOnly, tireInHands = wk.carrying.has_value() && wk.carrying->tireOnly;
    for (int ci = 0; ci < (int)sim.cars.size(); ci++) {
        const Car& c = sim.cars[ci];
        if (c.remote || c.isProp()) continue;
        const bool trunkCar = own && !c.ai && c.hasGate();  // (its chains, jack and your kit are in the back: below)
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
        // The jack: the truck's own, or yours (and one that's under a corner stays workable).
        if (c.hasJack || kit.has(TOOL_JACK) || c.jackPlaced) {
            InteractOption o;
            o.car = ci;
            o.at = c.stowAt(Car::STOW_JACK);
            if (trunkCar) {
            } else if (!c.jackPlaced && !busy) {
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
        // The back (trunk.h): open and shut it; with it open, take what lies in it, put things back.
        if (trunkCar) {
            const bool moving = len(c.frame().vel) > 0.5;
            const bool primary = ci == sim.player;
            InteractOption g;
            g.car = ci, g.at = trunk::gateAt(c);
            if (!wk.carrying.has_value() && c.gate.dir == 0) {
                if (c.gateShut()) g.kind = IA_OPEN_GATE, g.hold = kOpenGate, g.prompt = std::string("OPEN THE ") + trunk::gateName(c);
                else if (c.gateOpen()) g.kind = IA_CLOSE_GATE, g.hold = kOpenGate, g.prompt = std::string("CLOSE THE ") + trunk::gateName(c);
                if (g.kind != IA_NONE) {
                    if (moving) g.blocked = true, g.prompt += ": STOP THE TRUCK FIRST";
                    consider(g, 3.2, g.kind == IA_CLOSE_GATE ? 0.12 : 0);  // (shutting it: aiming at a thing inside beats the gate it's behind)
                }
            }
            if (c.gateOpen() && c.gate.dir == 0 && len(trunk::cargoCenter(c) - chest) < 6) {
                const Frame fr = c.frame();
                const std::vector<TrunkItem> items = trunk::wholeLoad(c, *own, primary, wk.hand, wk.holdingJack, wk.holdingChain);
                const std::vector<TrunkSlot> slots = trunk::layout(c, items);
                if (!busy && !wk.hand.any())  // (hands full of a tool: put it back first)
                    for (int i = 0; i < (int)items.size(); i++) {
                        InteractOption o;
                        o.car = ci;
                        if (!trunk::aimAt(c, fr, items, slots, i, o.at)) continue;  // (none of it lying there)
                        o.item = items[i].ref;
                        const std::string& nm = items[i].name;
                        switch (items[i].ref.kind) {
                            case TK_JACK: o.kind = IA_TAKE_JACK, o.hold = kTakeJack, o.prompt = "TAKE THE " + nm; break;
                            case TK_CHAINS: o.kind = IA_TAKE_CHAIN, o.hold = kTakeChain, o.prompt = "TAKE A SNOW CHAIN (" + std::to_string(c.chains) + " LEFT)"; break;
                            default:
                                o.kind = IA_TAKE_ITEM, o.hold = kTakeItem, o.prompt = "TAKE THE " + nm;
                                const int left = items[i].count - items[i].taken;
                                if (!items[i].endless && left > 1) o.prompt += " (" + std::to_string(left) + " IN THE BACK)";
                        }
                        consider(o, trunk::kReachIn, 0, 0.02);  // (where you point picks it: the near one doesn't win)
                    }
                // Put it back: look anywhere over the open cargo area.
                InteractOption o;
                o.car = ci;
                if (wk.holdingJack) o.kind = IA_STOW_JACK, o.hold = kStowJack, o.prompt = "PUT THE JACK BACK";
                else if (wk.holdingChain && c.chains < std::max(c.spec->snowChains, 4)) o.kind = IA_STOW_CHAIN, o.hold = kStowChain, o.prompt = "PUT THE CHAIN BACK";
                else if (wk.hand.any() && !busy) o.kind = IA_STOW_ITEM, o.hold = kStowItem, o.item = wk.hand, o.prompt = "PUT THE " + trunk::name(wk.hand) + " BACK";
                trunk::Box box;
                if (o.kind != IA_NONE && dir.y < -0.02 && trunk::cargoBox(c, box)) {
                    const V3 mid = trunk::cargoCenter(c);
                    const double t = (mid.y - eye.y) / dir.y;
                    const V3 hit = eye + dir * t, d = hit - mid;
                    const V3 fl = norm(V3(fr.fwd.x, 0, fr.fwd.z)), sd = V3(-fl.z, 0, fl.x);
                    if (t > 0 && std::fabs(dot(d, fl)) < (box.hi.x - box.lo.x) * 0.5 + 0.15 && std::fabs(dot(d, sd)) < (box.hi.z - box.lo.z) * 0.5 + 0.15) {
                        o.at = hit;
                        consider(o, trunk::kReachIn, -0.5);  // (it's what you're pointing at)
                    }
                }
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
                if (hover && hover->ok && hover->car == ci && partMatchesFix(hover->tag, f)) {  // the part you're looking at decides
                    o.at = hover->at;
                    consider(o, kReach, -1.0);
                } else {
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
    InteractOption o = pick(sim, wk, eye, look, &spots, kit, trunkOn ? &own : nullptr, hover.ok ? &hover : nullptr);
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
        case IA_OPEN_GATE:
        case IA_CLOSE_GATE: {
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            c.setGate(o.kind == IA_OPEN_GATE);
            return std::string(trunk::gateName(c)) + (o.kind == IA_OPEN_GATE ? " OPEN" : " SHUT");
        }
        case IA_TAKE_ITEM:
            wk.hand = o.item;
            return "GOT THE " + trunk::name(o.item);
        case IA_STOW_ITEM:
            wk.hand = TrunkRef();
            return "PUT THE " + trunk::name(o.item) + " BACK";
        case IA_FIX: {
            Car& c = sim.cars[o.car];
            if (len(c.frame().vel) > 0.5) return "STOP THE TRUCK FIRST";
            if (!applyFix(c, o.fix)) return "";
            if (wk.hand.kind == TK_PART && wk.hand.id == o.fix.part) wk.hand = TrunkRef();  // (the part in your hand was the one)
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
