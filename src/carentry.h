// Getting into a vehicle on foot: any one with nobody driving it (a parked truck, one you
// spawned, a wreck, traffic once it's stopped, a buddy's while they're out of it), or riding
// along in a buddy's while they drive. The game does the getting in (main.cpp: J, or hold E at
// a door); a buddy's is handed over by the network (NetSession::requestTake).
#pragma once
#include "softbody.h"

struct InteractOption;

namespace carentry {
enum Verdict {
    NONE,   // not a vehicle
    DRIVE,  // mine: get in
    TAKE,   // someone else's with nobody in it: ask for it, then get in
    RIDE,   // someone else's they're driving: ride along
    WAIT,   // moving with nobody to ask to stop (traffic on its way, a rolling one): once it stops
};
Verdict verdict(const Sim& sim, int car);
// The nearest vehicle to `feet` within `reach` of its outline (props out, anyone's), or -1.
int nearest(const Sim& sim, const V3& feet, double reach = 4.0);
// Just outside a door, at the handle's height: the driver's (its left) or the passenger's.
V3 door(const Car& c, bool passenger);
// A point in the cab moved across it (the driver's eyes to the passenger's).
V3 acrossCab(const Car& c, const V3& p);
// The door job for car `ci`, if any (interact.h: IA_ENTER_CAR, IA_RIDE_ALONG). False: none.
bool option(const Sim& sim, int ci, InteractOption& o);
constexpr double kHold = 0.3;  // s of holding E at a door
}  // namespace carentry
