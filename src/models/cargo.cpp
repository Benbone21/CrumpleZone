// Cargo: what the truck's back (trunk.h) needs besides the tools and parts: a new tire to carry,
// and a pickup's tailgate (drawn over the bed, since the body has no panel there).
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

// A tire lying flat (origin on the floor at its middle, hub up): black tread and sidewalls, a steel hub.
void tirePart(B& b, const P& p) {
    const double R = p("radius"), w = p("width");
    b.style(V3(0.045, 0.045, 0.05), MATTE);
    b.lathe(V3(0, 0, 0), {V2(R * 0.62, 0), V2(R * 0.9, 0), V2(R * 0.98, w * 0.18), V2(R, w * 0.5), V2(R * 0.98, w * 0.82), V2(R * 0.9, w), V2(R * 0.62, w), V2(R * 0.6, w * 0.5), V2(R * 0.62, 0)}, 28, true);
    // Tread blocks round the shoulder, and the sidewall's raised lettering ring.
    b.style(V3(0.07, 0.07, 0.075), MATTE);
    for (int i = 0; i < 24; i++) {
        const double a = 2 * M_PI * i / 24;
        b.push();
        b.move(std::cos(a) * R * 0.985, w * 0.5, std::sin(a) * R * 0.985);
        b.yaw(-a * 180 / M_PI);
        b.box(V3(0.012, 0, 0), V3(0.03, w * 0.86, 0.045), 0.004);
        b.pop();
    }
    b.style(V3(0.1, 0.1, 0.105), MATTE);
    b.torus(V3(0, w * 0.97, 0), R * 0.78, 0.006, 28, 5);
}

Reg tirePartReg({"tire_part", "TIRE", "REPAIR", "Spare part: a new tire to carry (lying flat in the truck's back); origin on the floor at its middle", 0.8,
                 {
                     {"ALL-TERRAIN", "A fat all-terrain tire", {num("radius", "RADIUS", 0.36, 0.28, 0.5), num("width", "WIDTH", 0.28, 0.2, 0.4)}, tirePart},
                 }});

// A pickup's tailgate, hinged on its bottom edge: origin at the middle of the hinge line (on the
// bed floor, at the very back), the panel standing up along +Y (closed), thin along X, across Z.
// Folds down over the bed floor going over backwards.
void bedGate(B& b, const P& p) {
    const double w = p("width"), h = p("height"), t = 0.05;
    const V3 body(p("r"), p("g"), p("b"));
    b.style(body, PAINT);
    b.box(V3(-t * 0.5, h * 0.5, 0), V3(t, h, w), 0.006);
    // The pressed panel on its face (the outside is toward -X) and a handle recess.
    b.style(body * 0.82, PAINT);
    b.box(V3(-t - 0.002, h * 0.52, 0), V3(0.006, h * 0.62, w * 0.84), 0.004);
    b.style(V3(0.08, 0.08, 0.085), MATTE);
    b.box(V3(-t - 0.006, h * 0.78, 0), V3(0.012, 0.05, 0.18), 0.006);
    // Hinge pins and the cable stays at each corner.
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(-t * 0.5, 0, s * (w * 0.5 - 0.03)), V3(-t * 0.5, 0, s * (w * 0.5 - 0.09)), 0.016, 8);
}

Reg bedGateReg({"bed_gate", "BED GATE", "REPAIR", "A pickup's tailgate: drawn over the bed, hinged on the bottom; folds down when opened (trunk.h)", 1.4,
                {
                    {"PAINTED", "Body-coloured, a pressed panel and a handle",
                     {num("width", "WIDTH", 1.25, 0.8, 1.8), num("height", "HEIGHT", 0.36, 0.25, 0.5), num("r", "RED", 0.7, 0, 1), num("g", "GREEN", 0.1, 0, 1), num("b", "BLUE", 0.1, 0, 1)},
                     bedGate},
                }});

}  // namespace
