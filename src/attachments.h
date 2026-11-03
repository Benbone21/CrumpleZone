// Garage attachments that fit any vehicle (mods.h MOD_WINCH, MOD_BUMPER; docs/parts-simplified.md, "Attachments"):
//
// - WINCH: a 12,000 lb electric winch (drum and synthetic line, motor, gearbox, hawse fairlead and
//   hook, ~40 kg) on a plate bolted ahead of the nose, or in the steel bumper's cradle when that's
//   fitted. The winch line leaves from its fairlead (Car::hookRest[0]). A vehicle that came with a
//   winch keeps it as its factory part; its other option takes it off ("NO WINCH").
// - STEEL BUMPER: a plate-steel bumper with a tube grille guard (~75 kg). In the sim it's a row of
//   heavy frame nodes ahead of the nose, braced back into the front of the body (and onto a ladder
//   frame's horns) with stiff, strong steel members, and the lowest rows of the nose behind it are
//   boxed in by its mounts: a crash into a wall or a rock crushes the nose less (carbuild.cpp).
//
// The fit comes from each vehicle's own front (its length, nose height, width at the nose), so the
// parts are placed the same way by the physics (carbuild.cpp) and the drawing (carvisual.cpp).
#pragma once
#include <vector>

#include "mathx.h"
#include "mods.h"

constexpr double kWinchKitKg = 40;       // a 12,000 lb winch with 30 m of synthetic line and its mount plate
constexpr double kWinchKitPull = 53400;  // N: 12,000 lb on the first layer
constexpr double kSteelBumperKg = 75;    // plate-steel bumper, grille guard, D-ring mounts and brackets

// Where the attachments sit on this vehicle (car frame: x forward, y up, z right, metres).
struct AttachFit {
    double noseX = 0;         // the body's front face
    double y0 = 0, y1 = 0;    // the bumper's bottom and top
    double halfW = 0;         // the bumper's half width
    double faceX = 0;         // the bumper's front face
    double depth = 0;         // the bumper's depth (nose to face)
    double guardTop = 0;      // top of the grille guard hoop
    double guardHalfW = 0;    // half width of the guard's outer hoops
    V3 winch;                 // the winch drum's middle
    double winchScale = 1;    // its size against a 12,000 lb winch (a smaller factory winch is smaller)
    V3 fairlead;              // where the line leaves (the front recovery point when either is fitted)
};
AttachFit attachFit(const VehicleSpec& vs);

// The options in each attachment slot for this catalog vehicle (option 0: as it came).
std::vector<ModOption> winchOptions(const VehicleSpec& vs);
std::vector<ModOption> bumperOptions(const VehicleSpec& vs);

// Whether the big winch model is drawn: the winch attachment, or any winch in the steel bumper's cradle.
inline bool drawsWinchKit(const VehicleSpec& vs) { return vs.winchKit || (vs.steelBumper && (vs.extras & EXTRA_WINCH)); }
