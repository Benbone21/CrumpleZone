// A stand-in for one of the main menu's parked vehicles, for the world editor only (menucamp.h): a car-shaped block with its nose marked, 4.6 m
// long, so it can be picked, moved and turned like any placed model. The menu itself never draws it: the game parks real vehicles there.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

void menuVehicle(B& b, const P& p) {
    static const V3 body[5] = {V3(0.78, 0.55, 0.12), V3(0.2, 0.55, 0.85), V3(0.85, 0.25, 0.2), V3(0.3, 0.7, 0.35), V3(0.8, 0.8, 0.82)};
    const V3 c = body[std::clamp(p.i("slot"), 0, 4)];
    b.style(c, PAINT);
    b.box(V3(0, 0.75, 0), V3(4.6, 0.7, 1.9), 0.08);          // the body
    b.style(c * 0.75, PAINT);
    b.box(V3(-0.5, 1.4, 0), V3(2.6, 0.62, 1.7), 0.1);        // the cabin
    b.style(V3(0.12, 0.2, 0.28), MATTE);
    b.box(V3(-0.5, 1.45, 0), V3(2.62, 0.4, 1.72), 0.1);      // its glass band
    b.style(V3(0.08, 0.08, 0.08), MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(sx * 1.5, 0.42, sz * 0.78), V3(sx * 1.5, 0.42, sz * 1.0), 0.42);
    b.style(V3(1.0, 0.85, 0.2), MATTE);
    b.box(V3(2.45, 0.8, 0), V3(0.3, 0.35, 1.5), 0.04);       // the nose: a bright bumper
}

Reg menuVehicleReg({"menu_vehicle", "MENU VEHICLE", "MENU", "The main menu's parked vehicles in the editor (stand-ins: the game draws the real ones)", 5.0,
                    {{"CAR", "A car-shaped block; its yellow end is the nose", {num("slot", "COLOUR", 0, 0, 4, 1)}, menuVehicle}}});

}  // namespace
