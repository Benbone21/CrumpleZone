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

#include "fixes.h"
#include "softbody.h"

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
    bool same(const InteractOption& o) const { return kind == o.kind && car == o.car && wheel == o.wheel && loose == o.loose && spot == o.spot; }
};

struct Interact {
    Kit kit;                 // your tools and parts (set by the game; sandbox: everything)
    InteractOption current;  // what E does now (kind IA_NONE: nothing in reach)
    int usedPart = -1;       // set when a repair used a part (RepairPart): the game takes it from here
    bool waitRelease = false;  // a held job finished: E has to be let go before the next
    double progress = 0;     // s held on it so far
    std::vector<InteractSpot> spots;  // things out in the world to open (set by the game)
    std::string openedSpot;           // set when one's been opened (its id): the game takes it from here
    double share() const { return current.hold > 0 ? std::min(1.0, progress / current.hold) : 0; }
    static constexpr double kJackMax = 0.45;  // m the saddle can rise

    // The job in front of you: the nearest thing to where you're looking, within reach
    // (2.5 m) and no more than ~35 degrees off your line of sight.
    static InteractOption pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots = nullptr,
                               const Kit& kit = Kit());
    // Once a frame on foot: finds the job, runs the hold, and does it when the hold is done.
    // Returns what to announce ("" most frames).
    std::string update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt);
    // Puts down what you carry, stood on its tread in front of you (G). False if empty-handed.
    static bool drop(Sim& sim, Walker& wk, const V3& look);
    // Drops the jack you're holding.
    static bool dropJack(Sim& sim, Walker& wk);
    // Where a jack goes for a corner: on the ground under its jack point (Car::jackPoint: the
    // axle tube or the lower arm, just inboard of the tire).
    static V3 jackSpot(const Sim& sim, const Car& c, int wheel);
    // The jack standing there, as models turn it: its handle (the crank, the pump lever) sticks
    // out from under the car, toward you. `x` is where the model's +X points, `lift` its param.
    static Placed jackModel(const Sim& sim, const Car& c, int wheel);
    // Whether the corner's off the ground (its tire clear, or its bare hub high enough for one).
    static bool cornerLifted(const Sim& sim, const Car& c, int wheel);
};
