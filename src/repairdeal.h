// Career: what R costs. In a career R does nothing by itself (no free repair, no free reset); it opens a confirmation
// with two paid choices, and the truck only changes once the player picks one and can pay. Sandbox keeps R as it was.
//
// Pricing, by how hurt the truck is (the HUD's damage bar: Car::damage runs 0..0.5 to fill it, so d = 2 * damage, 0..1):
//   respawn at the nearest station = $50 + $350 * d   ($50 .. $400): a tow and a repair at a shop that already has the
//                                                      parts and the bay; the dear part is the damage, not the trip.
//   repair on the spot             = $100 + $600 * d  ($100 .. $700): a mechanic called out to the trail with the parts on
//                                                      a truck; always dearer than the tow (the call-out alone is $100,
//                                                      twice the tow's base), and it grows faster with the damage.
// Against $500 (hardcore) .. $2000 (light) of starting cash: a fender-bender is cheap everywhere, a wreck is dear
// enough that driving to a station (free) is still the better plan when you can, and the tow is the way out when
// you are stuck. A totalled truck on hardcore cash can afford the tow, never the call-out.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "career.h"

namespace repairdeal {

enum Choice { SPOT = 1, STATION = 2 };

inline double hurt(double damage) { return std::min(1.0, std::max(0.0, std::isfinite(damage) ? damage * 2.0 : 0.0)); }

inline int price(Choice c, double damage) {
    const double d = hurt(damage);
    return c == SPOT ? 100 + (int)std::lround(600 * d) : 50 + (int)std::lround(350 * d);
}

// Tries to pay for `c`: on success takes the cash and returns true (the caller then repairs / moves the truck); else says
// why in `why` and changes nothing. `haveStation`: the map has a repair station to respawn at.
inline bool pay(Career& career, Choice c, double damage, bool haveStation, std::string& why) {
    if (c == STATION && !haveStation) {
        why = "NO REPAIR STATION ON THIS MAP TO RESPAWN AT";
        return false;
    }
    const int cost = price(c, damage);
    if (career.cash < cost) {
        why = "CAN'T AFFORD IT: $" + std::to_string(cost) + " NEEDED, YOU HAVE $" + std::to_string(std::max(0, career.cash));
        return false;
    }
    career.cash -= cost;
    why = (c == SPOT ? "REPAIRED ON THE SPOT: -$" : "RESPAWNED REPAIRED AT THE STATION: -$") + std::to_string(cost);
    return true;
}

// The confirmation's choice lines.
inline std::string lineSpot(double damage) { return "1: REPAIR ON THE SPOT  $" + std::to_string(price(SPOT, damage)); }
inline std::string lineStation(double damage) { return "2: RESPAWN REPAIRED AT THE NEAREST STATION  $" + std::to_string(price(STATION, damage)); }

}  // namespace repairdeal
