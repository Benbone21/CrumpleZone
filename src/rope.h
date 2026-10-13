// Recovery gear: winch lines and tow straps between a truck's recovery points and
// trees, boulders, ground stakes or other vehicles.
#pragma once
#include <cstdint>

#include "mathx.h"

enum RopeKind { ROPE_WINCH, ROPE_STRAP };
enum AnchorKind { ANCHOR_CAR, ANCHOR_TREE, ANCHOR_ROCK, ANCHOR_STAKE };

// One end of a line: a vehicle's recovery point (front or rear), or a fixed point.
struct RopeEnd {
    AnchorKind kind = ANCHOR_CAR;
    int car = -1;   // ANCHOR_CAR: index into Sim::cars
    int hook = 0;   // 0 front, 1 rear
    V3 p;           // fixed anchors: where the line is tied
};

struct Rope {
    RopeKind kind = ROPE_WINCH;
    RopeEnd a, b;           // a: the truck it belongs to (the winch, or whoever hooked the strap)
    double length = 0;      // unstretched length of line out (m)
    double maxLen = 30;     // all the line there is
    double k = 0, c = 0;    // stretch stiffness (N/m) and damping (N*s/m)
    double breakN = 0;      // breaking strength (N)
    double stallN = 0;      // winch: the pull it stalls at (N)
    double tension = 0;     // N, this step
    double shown = 0;       // N, smoothed for the HUD
    int winch = 0;          // +1 reeling in, -1 paying out, 0 holding
    double overload = 0;    // s spent over the breaking strength
    uint32_t netId = 0;     // multiplayer: the same line on every machine
};

// Something a winch line could be hooked to (what the player picks from).
struct WinchTarget {
    RopeEnd end;
    V3 at;              // where the line would tie on
    double dist = 0;    // from the winch's fairlead
    bool inArc = true;  // within the fairlead's reach (not behind the truck)
    const char* what = "";
};

// Real-world numbers the recovery gear is built from.
namespace recovery {
constexpr double kWinchLine = 30.0;      // m of 3/8 in synthetic line
constexpr double kWinchExtension = 30.0; // m: the extension line in the recovery bag
constexpr double kWinchReach = kWinchLine + kWinchExtension;
constexpr double kWinchStall = 44000;    // N: a 9,500 lb winch on its first layer
constexpr double kWinchSpeed = 0.13;     // m/s line speed with no load
constexpr double kWinchBreak = 90000;    // N: synthetic line, ~2x the winch
constexpr double kStrapLen = 9.0;        // m: a 30 ft kinetic strap
constexpr double kStrapBreak = 130000;   // N
constexpr double kStrapReach = 9.0;      // m: how far apart two trucks can be to hook a strap
}  // namespace recovery
