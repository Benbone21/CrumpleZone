// The battery, the ignition, the jump pack and jumper cables.
//
// Every vehicle has a charge (Car::battery, 0..1). The engine is off from when you step out until the starter turns it over
// on getting in (Car::engineOff). With the engine off, headlights left on, a winch line being worked and cranking draw the
// charge down; a battery that goes under water is ruined; running, the alternator fills it. The starter cranks slower the
// weaker the battery, and not at all when it is flat (a click). Two ways to start a flat one:
//   - the JUMP PACK (a tool in your hand): hold E at the battery.
//   - JUMPER CABLES (a tool in your hand): clamp one end to a battery, walk the cables to a second vehicle and clamp that;
//     the one that is running feeds the other (works across players: the cables are told to the others, repairsync.h,
//     and each machine charges its own vehicles).
// No hood needed (a later Car::hoodOpen can gate the options in interact.cpp).
#pragma once
#include <functional>
#include <string>

#include "audio.h"
#include "softbody.h"

namespace battery {

constexpr double kFlat = 0.2;       // under this the starter only clicks
constexpr double kLow = 0.4;        // the dash light comes on
constexpr double kStrong = 0.55;    // at or over this it cranks at full speed
constexpr double kCrankTime = 0.9;  // s to start at full strength
constexpr double kCableLen = 5.0;   // m of cable (pulled off beyond kCableLen + 1.5 between posts)
constexpr double kPackLevel = 0.7;  // what a jump pack leaves

bool running(const Car& c);               // engine turning on its own
bool flat(const Car& c);                  // battery too weak to crank
bool lampsOn(const Sim& sim, const Car& c);
V3 post(const Car& c, int which);         // a battery terminal, world (0 red +, 1 black -)
V3 aim(const Car& c);                     // the middle of the battery, world
double supply(const Sim& sim, int car);   // what the starter has to work with: its charge, or a running car's through cables

// Once a frame for the vehicles that are ours (Sim::updateTrail calls it; dt in sim seconds).
void step(Sim& sim, double dt);

// On-foot jobs (interact.cpp). Each returns what to announce, "" if nothing.
std::string jump(Sim& sim, int car);                // the pack on: the battery brought up
std::string clamp(Sim& sim, int car, bool& done);   // first clamp (in hand) or the second (cable made: done)
std::string unclamp(Sim& sim, int cable);
int cableOn(const Sim& sim, int car);               // index in sim.cables (-1 none)

// The sounds of the starter and the clicking solenoid, for the vehicles in `sim` that are ours.
struct Sounds {
    double clickT = 0, crankT = 0;
    bool wasCranking = false;
    using Play = std::function<void(Sfx, const V3&, float, float, float)>;
    void update(const Sim& sim, double dt, const Play& play);
};

}  // namespace battery
