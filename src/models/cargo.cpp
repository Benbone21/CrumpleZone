// Cargo: what the truck's back (trunk.h) needs besides the tools and parts: a new tire to carry,
// and a pickup's drop-down tailgate (drawn over the bed, since the body has no panel there).
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

// The cargo platform of an SUV, standing on the sill: a carpeted deck on a plywood box (a drawer
// system's top), so what's laid on it clears the high sill. Origin at the middle of its top face;
// X is its depth (front to back of the truck), Z its width.
void cargoMat(B& b, const P& p) {
    const double d = p("depth"), w = p("width"), t = p("height");
    b.style(V3(0.36, 0.3, 0.22), MATTE);  // the carpet
    b.box(V3(0, -0.01, 0), V3(d, 0.02, w), 0.004);
    b.style(V3(0.52, 0.4, 0.26), WOOD);  // the plywood box under it, a hand-hold cut in the rear
    b.box(V3(0, -0.02 - (t - 0.02) * 0.5, 0), V3(d - 0.03, t - 0.02, w - 0.03), 0.006);
    b.style(V3(0.07, 0.07, 0.075), MATTE);
    b.box(V3(-d * 0.5 + 0.004, -t * 0.55, 0), V3(0.012, 0.05, 0.22), 0.004);
    b.style(V3(0.2, 0.17, 0.13), MATTE);  // the bound edge of the carpet
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, -0.01, s * (w * 0.5 - 0.012)), V3(d, 0.024, 0.024), 0.003);
        b.box(V3(s * (d * 0.5 - 0.012), -0.01, 0), V3(0.024, 0.024, w), 0.003);
    }
    // Lashing rings in the corners.
    b.style(V3(0.6, 0.6, 0.62), METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.torus(V3(sx * (d * 0.5 - 0.07), 0.004, sz * (w * 0.5 - 0.07)), 0.025, 0.004, 12, 5);
}

Reg cargoMatReg({"cargo_mat", "CARGO MAT", "REPAIR", "An SUV's cargo platform (trunk.h): a carpeted deck on a box, standing on the sill, that the load is laid on; origin at the middle of its top face", 1.2,
                 {
                     {"CARPET", "Carpeted deck on a plywood box, four lashing rings", {num("depth", "DEPTH", 1.0, 0.6, 1.6), num("width", "WIDTH", 1.1, 0.7, 1.6), num("height", "HEIGHT", 0.14, 0.05, 0.3)}, cargoMat},
                 }});

}  // namespace
