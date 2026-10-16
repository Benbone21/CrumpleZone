// On foot, hands on: what the thing in front of you lets you do (hold E), and doing it.
// Take a truck's spare off its carrier, take a wheel off, carry it, bolt a wheel onto a bare
// hub (any size), put a tire back on its rim, pick a wheel up off the trail, stow one; take a
// snow chain out of the back, wrap it round a tire, take it off again. The
// wheel jobs are on the trucks with wheelLoss (tires.h); the hold times are the work.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

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
    IA_TAKE_JACK,     // take the jack from the car's bed
    IA_PLACE_JACK,    // place the jack under the car near a wheel
    IA_USE_JACK,      // operate the jack to lift the corner
    IA_LOWER_JACK,    // lower the jack back down
    IA_REMOVE_JACK,   // remove the jack after lowering
    IA_TAKE_CHAIN,    // a snow chain out of the back
    IA_FIT_CHAIN,     // wrap the chain you carry round a tire (stopped)
    IA_REMOVE_CHAIN,  // unhook a chain from a tire
    IA_STOW_CHAIN,    // put the chain you carry back in the truck
    IA_OPEN_CACHE,    // open a supply cache (career.h)
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
    bool same(const InteractOption& o) const { return kind == o.kind && car == o.car && wheel == o.wheel && loose == o.loose && spot == o.spot; }
};

struct Interact {
    InteractOption current;  // what E does now (kind IA_NONE: nothing in reach)
    double progress = 0;     // s held on it so far
    std::vector<InteractSpot> spots;  // things out in the world to open (set by the game)
    std::string openedSpot;           // set when one's been opened (its id): the game takes it from here
    double share() const { return current.hold > 0 ? std::min(1.0, progress / current.hold) : 0; }

    // The job in front of you: the nearest thing to where you're looking, within reach
    // (2.5 m) and no more than ~35 degrees off your line of sight.
    static InteractOption pick(const Sim& sim, const Walker& wk, const V3& eye, const V3& look, const std::vector<InteractSpot>* spots = nullptr);
    // Once a frame on foot: finds the job, runs the hold, and does it when the hold is done.
    // Returns what to announce ("" most frames).
    std::string update(Sim& sim, Walker& wk, const V3& eye, const V3& look, bool holdE, double dt);
    // Puts down what you carry, stood on its tread in front of you (G). False if empty-handed.
    static bool drop(Sim& sim, Walker& wk, const V3& look);
    // Drops the jack you're holding.
    static bool dropJack(Sim& sim, Walker& wk);
};
