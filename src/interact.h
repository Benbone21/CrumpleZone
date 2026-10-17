// On foot, hands on: what the thing in front of you lets you do (hold E), and doing it.
// Take a truck's spare off its carrier, jack a corner up, take a wheel off, carry it, bolt a
// wheel onto a bare hub (any size), put a tire back on its rim, pick a wheel up off the trail,
// stow one; take a snow chain out of the back, wrap it round a tire, take it off again; fix
// what's broken with your own tools and a spare part (fixes.h). The wheel jobs are on the
// trucks with wheelLoss (tires.h); the hold times are the work, and the game animates it
// (handwork.h).
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "carparts.h"
#include "fixes.h"
#include "softbody.h"
#include "trunk.h"

struct Walker;

enum InteractKind {
    IA_NONE,
    IA_TAKE_SPARE,    // the spare off its carrier (or from under the bed)
    IA_STOW_SPARE,    // the wheel you carry onto an empty carrier
    IA_REMOVE_WHEEL,  // a wheel off its hub (stopped)
    IA_FIT_WHEEL,     // the wheel you carry onto a bare hub
    IA_RESEAT_TIRE,   // the tire you carry back onto its rim
    IA_PICK_UP,       // a wheel lying about
    IA_TAKE_JACK,     // the jack out of the back
    IA_STOW_JACK,     // ...and back in
    IA_PLACE_JACK,    // under a corner, inboard of its wheel
    IA_RAISE_JACK,    // work it up (as long as E is held) until the tire's off the ground
    IA_LOWER_JACK,    // let it down (as long as E is held)
    IA_REMOVE_JACK,   // out from under, once it's down
    IA_TAKE_CHAIN,    // a snow chain out of the back
    IA_FIT_CHAIN,     // wrap the chain you carry round a tire (stopped)
    IA_REMOVE_CHAIN,  // unhook a chain from a tire
    IA_STOW_CHAIN,    // put the chain you carry back in the truck
    IA_OPEN_CACHE,    // open a supply cache (career.h)
    IA_FIX,           // a repair (fixes.h): a CV axle, a tie rod, a new tire...
    IA_OPEN_GATE,     // the tailgate / liftgate / bed gate (trunk.h)
    IA_CLOSE_GATE,
    IA_OPEN_INVENTORY,  // E at the back of your truck: what it carries, as a menu (invmenu.h); a shut gate swings open with it
    IA_TAKE_ITEM,     // a tool, part or anchor out of the open back, into your hands
    IA_STOW_ITEM,     // ...and back in
};

// Something out in the world to open (a career supply cache): where, and what to call it.
struct InteractSpot {
    std::string id;
    V3 at;
    std::string prompt;  // "OPEN THE SUPPLY CACHE"
};

struct InteractOption {
    InteractKind kind = IA_NONE;
    int car = -1, wheel = -1, loose = -1, spot = -1;
    double hold = 0;     // s of holding E it takes
    V3 at;               // where it is (for the prompt and the aim)
    std::string prompt;  // "TAKE THE SPARE (35 IN)"
    bool continuous = false;  // goes on as long as E is held (the jack)
    bool blocked = false;     // can't yet: the prompt says what's missing
    Fix fix{Fix::CV_AXLE};    // (IA_FIX)
    TrunkRef item;            // (IA_TAKE_ITEM, IA_STOW_ITEM)
    bool same(const InteractOption& o) const { return kind == o.kind && car == o.car && wheel == o.wheel && loose == o.loose && spot == o.spot && item == o.item; }
};

struct Interact {
    Kit kit;                 // the tools and parts you can use now (set by the game; sandbox: everything; trunk.h: within reach of the open back)
    TrunkStock own;          // everything you own (the back's contents)
    bool trunkOn = false;    // the game has set `own`: trucks with a back have to be opened (else the old way: the kit's on you)
    InteractOption current;  // what E does now (kind IA_NONE: nothing in reach)
    int usedPart = -1;       // set when a repair used a part (RepairPart): the game takes it from here
    bool waitRelease = false;  // a held job finished: E has to be let go before the next
    double progress = 0;     // s held on it so far
    std::vector<InteractSpot> spots;  // things out in the world to open (set by the game)
    std::string openedSpot;           // set when one's been opened (its id): the game takes it from here
    int openedInventory = -1;         // set when E opened a truck's inventory (its car): the game shows the menu
    PartHit hover;                    // the car part under the crosshair (set by the game each frame): its repair, if any, is the one picked
    double share() const { return current.hold > 0 ? std::min(1.0, progress / current.hold) : 0; }
    static constexpr double kJackMax = 0.45;  // m the saddle can rise above where it met the pad (the hi-lift's bar is 1.2 m)

    // The job in front of you: the nearest thing to where you're looking, within reach
    // (2.5 m) and no more than ~35 degrees off your line of sight.
    // `own`: everything you own, if the trucks' backs are in play (trunk.h).
    static InteractOption pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots = nullptr,
                               const Kit& kit = Kit(), const TrunkStock* own = nullptr, const PartHit* hover = nullptr);
    // Once a frame on foot: finds the job, runs the hold, and does it when the hold is done.
    // Returns what to announce ("" most frames).
    std::string update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt);
    // A job done (the hold's end; the inventory menu's own choices too): what to announce.
    std::string perform(Sim& sim, Walker& wk, const InteractOption& o);
    // The back's jobs, for the pick and the inventory menu alike: take `it` out of car `ci`'s back (kind IA_NONE:
    // none left to take), or put back what's in your hands (IA_NONE: nothing, or no room for it).
    static InteractOption takeOption(const Car& c, int ci, const TrunkItem& it);
    static InteractOption stowOption(const Car& c, int ci, const Walker& wk);
    // The menu's choices: put what you hold back and take `ref` (the same job as picking it from the bed), or just put it back.
    std::string takeFromBack(Sim& sim, Walker& wk, int car, const TrunkStock& stock, TrunkRef ref);
    std::string putBackInBack(Sim& sim, Walker& wk, int car);
    // Puts down what you carry, stood on its tread in front of you (G). False if empty-handed.
    static bool drop(Sim& sim, Walker& wk, const V3& look);
    // Drops the jack you're holding.
    static bool dropJack(Sim& sim, Walker& wk);
    // Where a jack goes for a corner: on the ground under its jack point (Car::jackPoint: the frame's
    // jack pad beside the wheel well; a unibody's axle or lower arm).
    static V3 jackSpot(const Sim& sim, const Car& c, int wheel);
    // Where you aim to work the jack there: the middle of it, between its saddle and its bar out past the body.
    static V3 jackAim(const Sim& sim, const Car& c, int wheel);
    // The jack standing there, as models turn it: its saddle on the pad, its bar and handle out past
    // the body's side, toward you. `x` is where the model's +X points, `lift` and `reach` its params.
    static Placed jackModel(const Sim& sim, const Car& c, int wheel);
    // Whether the corner's off the ground (its tire clear, or its bare hub high enough for one).
    static bool cornerLifted(const Sim& sim, const Car& c, int wheel);
};
