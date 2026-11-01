// Field repair seen by everyone in a multiplayer session: the other players see what you hold and the work you're doing
// (the tool on the nut, the jack going under, the wheel off, the new part going in), hear its tools, see the jack under
// your car go up and down, the broken part you drop, and get the repaired car.
//
// No new message type (net::kVersion stays 7): short text over the session's chatter channel (NetSession::sendGame,
// reliable, relayed by the host), the way the horn and the walk go. An older build ignores the lot (its own repairs
// still show to everyone through the car states: a wheel off its hub, a broken shaft, a flat).
//   "work [h=KIND,ID] [j=1] [c=1] [w=R,RIMR,WIDTH,STYLE,POPPED,TIREONLY] [do=JOB...]"  what you hold and do. Sent when it
//       changes and every kRefresh s while it isn't nothing (a bare "work": nothing); unheard for kExpiry s, it's nothing.
//       Unknown keys are skipped (a later build can add some). do=KIND,CAR,WHEEL,STEP,TOOL,PART,FIXKIND,FIXWHEEL,FIXPART,
//       CONTINUOUS,ATEND,HOLD,SECS,X,Y,Z: the job (InteractOption; CAR a net id) and how far into it (s): the others run
//       the clock on from there.
//   "jack CAR WHEEL BASE HEIGHT FX FY FZ" / "jack CAR -1"  the jack under one of your vehicles: when it goes under or
//       comes out, at most every kJackEvery s while it moves, and every kJackRefresh s while it's there.
//   "hood CAR OPEN"  the hood of one of your vehicles (Car::hoodOpen): 1 up, 0 down. Sent when it changes and every kHoodRefresh s
//       while it's up (so a late joiner learns it); the panel itself rides the car's states, this is the flag (Car::hood) the engine
//       work (and a buddy's jumper cables) read. Unheard for kHoodExpiry s, it's down.
//   "drop PART X Y Z YAW"  a broken part put down (Sim::droppedParts).
//   "cable CARA CARB" / "cable -"  your jumper cables clamped on two vehicles (battery.h): when they go on or come off and every
//       kCableRefresh s while they're on; unheard for kCableExpiry s they come off. Each machine charges its own vehicles from them.
//   "jump CAR"  you put the jump pack on a vehicle of the receiver's: its owner brings the battery up.
//   "fixed CAR KIND WHEEL BEAM AXLE"  a repair made whole on one of your vehicles: the others make their copy whole too
//       (only from its owner: whoever's vehicle it is does the fix, the same as everything else about it).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "handwork.h"
#include "interact.h"
#include "toolsounds.h"
#include "walker.h"

namespace repairsync {

constexpr double kRefresh = 1.0, kExpiry = 3.0, kMinGap = 0.1;
constexpr double kCableRefresh = 2.0, kCableExpiry = 6.0;
constexpr double kJackEvery = 0.2, kJackRefresh = 2.0, kJackExpiry = 6.0;
constexpr double kHoodRefresh = 3.0, kHoodExpiry = 9.0;

// What one player holds and does, as told.
struct Work {
    bool jack = false, chain = false;
    TrunkRef hand;
    bool carrying = false;
    WheelItem carry;
    InteractOption job;  // kind IA_NONE: no work under way (job.car is unused: carId)
    uint32_t carId = 0;
    double secs = 0;     // s into the job
    bool working() const { return job.kind != IA_NONE; }
    bool nothing() const { return !working() && !jack && !chain && !hand.any() && !carrying; }
    // The same thing held and the same job (not how far into it).
    bool same(const Work& o) const;
};
// This player's, now (`wk` null: not on foot).
Work workOf(const Sim& sim, const Walker* wk, const Interact& ia);
std::string encode(const Work& w);
// False if it isn't a work message. Anything out of range is dropped (a field, or the job).
bool decode(const std::string& text, Work& out);

// This machine's side: call once a frame, on foot or not. What's to go out comes back through `send` (nothing offline,
// but Interact's news is taken either way).
struct Sender {
    Work last;
    double lastAt = -1e9;
    struct JackSent {
        uint32_t id = 0;
        int wheel = -1;
        double height = 0, at = -1e9;
    };
    std::vector<JackSent> jacks;
    struct HoodSent {
        uint32_t id = 0;
        bool open = false;
        double at = -1e9;
    };
    std::vector<HoodSent> hoods;
    std::string lastCable;  // the last "cable ..." sent
    double cableAt = -1e9;
    void update(const Sim& sim, const Walker* wk, Interact& ia, double now, bool online, const std::function<void(const std::string&)>& send);
};

// The other players'.
struct Remote {
    struct Player {
        int player = -1;
        std::string who;
        Work work;
        double heard = -1e9;
        ToolSounds sounds;
        Interact ia;  // (their job as an Interact, for handwork and the sounds)
    };
    std::vector<Player> players;
    struct Jack {
        uint32_t id = 0;
        double height = 0, heard = 0;
    };
    std::vector<Jack> jacks;  // remote vehicles' jacks: where they're going (eased there)
    struct Hood {
        uint32_t id = 0;
        double heard = 0;
    };
    std::vector<Hood> hoods;  // remote vehicles' hoods that are up (the flag; the panel comes with the states)

    // A chatter line from `player` (called `who`): true if it was one of these. `now`: this machine's clock.
    bool onMessage(Sim& sim, int player, const std::string& who, const std::string& text, double now);
    // Once a frame: the jacks eased to where they were told, the unheard forgotten.
    void update(Sim& sim, double dt, double now);
    Player* find(const std::string& who);
    // What their hands are doing now, standing at `feet` facing `yaw` (handwork.h, as if seen through their eyes); its
    // Interact (p.ia) is set to their job, the clock run on. False: nothing in hand or under way.
    bool handwork(const Sim& sim, Player& p, const V3& feet, double yaw, double crouch, double now, Handwork& out, V3* eyeOut = nullptr);
    // Their tool sounds, at the work (call once a frame for each, after handwork).
    void sounds(Player& p, const Handwork& hw, const ToolSounds::Play& play, const V3& listener);
};

int carByNetId(const Sim& sim, uint32_t id);

}  // namespace repairsync
