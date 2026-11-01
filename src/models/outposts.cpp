// Outposts: the backcountry's working buildings and gear. A detached garage for a pickup, the ranger station, fuel pumps,
// the mine headframe and a road gate.
//
// Garage frame (all variants): footprint 7.3 (X) x 4.2 (Z), door in the +X end. Floor top y = 0.05. Interior clear box
// X -3.45..3.45 at the walls (the truck's clear space is X -2.75..3.45 x Z +-1.6 x Y 0.05..2.65), a 0.7 m deep back zone
// (X -3.45..-2.75) holds the workbench, tyre rack and barrel, and a 0.3 m deep zone along each side wall (|Z| 1.6..1.9)
// holds shelves and tools. Door opening: 3.2 m wide (Z +-1.6), 2.5 m high, in the front wall at X 3.45..3.65.
#include <cmath>
#include <cstdlib>

#include "../modelkit.h"

using namespace mk;

namespace {

const double PI_ = 3.14159265358979323846;

// Test hook for the sheet tool: -DOUTPOST_TEST lets the environment override the defaults of the instance params.
double defOf(const char* env, double d) {
#ifdef OUTPOST_TEST
    if (const char* s = std::getenv(env)) return std::atof(s);
#else
    (void)env;
#endif
    return d;
}

V3 rustify(const V3& c, double rust) { return mix(c, V3(0.42, 0.2, 0.09), rust * 0.55) * (1.0 - 0.25 * rust); }

V3 woodCol(int i) {
    static const V3 w[] = {V3(0.40, 0.29, 0.20), V3(0.50, 0.47, 0.42), V3(0.44, 0.20, 0.15), V3(0.27, 0.20, 0.15), V3(0.46, 0.36, 0.24)};
    return w[i < 0 ? 0 : i > 4 ? 4 : i];
}
V3 roofCol(int i) {
    static const V3 w[] = {V3(0.20, 0.33, 0.25), V3(0.46, 0.25, 0.15), V3(0.55, 0.57, 0.57), V3(0.40, 0.15, 0.13), V3(0.28, 0.30, 0.33)};
    return w[i < 0 ? 0 : i > 4 ? 4 : i];
}
const std::vector<std::string> kWoodNames = {"BROWN", "SILVER", "BARN RED", "DARK", "PINE"};
const std::vector<std::string> kRoofNames = {"GREEN TIN", "RUST TIN", "GALVANISED", "RED TIN", "SLATE"};

// A flat blob on the ground (oil stain): ellipse with a lumpy edge, lying at height y0.
void stain(B& b, double x, double z, double rx, double rz, double y0, uint32_t seed) {
    std::vector<V2> poly;
    for (int i = 0; i < 14; i++) {
        const double a = 2 * PI_ * i / 14, k = 0.8 + 0.35 * hash01(seed, i);
        poly.push_back(V2(x + std::cos(a) * rx * k, z + std::sin(a) * rz * k));
    }
    b.extrudeUp(poly, 0.003, y0);
}

// A tyre standing with its axle along X, centred on c.
void tyreX(B& b, const V3& c, double R = 0.33, double w = 0.2) {
    b.push();
    b.move(c);
    b.pitch(90);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.lathe(V3(0, -w / 2, 0), {V2(R * 0.58, 0), V2(R * 0.95, 0), V2(R, w * 0.5), V2(R * 0.95, w), V2(R * 0.58, w), V2(R * 0.58, 0)}, 12, true);
    b.style(V3(0.32, 0.32, 0.33), METAL);
    b.cyl(V3(0, -w * 0.3, 0), V3(0, w * 0.3, 0), R * 0.58, 8);
    b.pop();
}

// Everything inside every garage: slab, stains, workbench, shelves, tyre rack, barrel, tools, tarp, bulb.
void garageInterior(B& b, double wallH, double ceilY) {
#ifdef NO_INT
    return;
#endif
    // The floor slab (inset in the walls) and the apron outside the door.
    b.style(V3(0.5, 0.49, 0.47), CONCRETE);
    b.box(V3(0, 0.025, 0), V3(7.2, 0.05, 3.9));
    b.style(V3(0.47, 0.46, 0.44), CONCRETE);
    b.box(V3(4.65, 0.018, 0), V3(2.0, 0.036, 3.6), 0.01);
    // Oil stains where the engine sits and drips on the way in, a lighter patch of spilled sawdust.
    b.style(V3(0.11, 0.1, 0.09), MATTE);
    stain(b, 0.8, 0.15, 0.55, 0.38, 0.0505, 3);
    stain(b, 1.0, 0.1, 0.25, 0.18, 0.0515, 8);
    stain(b, -1.2, -0.3, 0.3, 0.2, 0.0505, 13);
    stain(b, 2.5, -0.5, 0.12, 0.09, 0.0505, 21);
    b.style(V3(0.62, 0.52, 0.36), MATTE);
    stain(b, -2.4, 1.0, 0.3, 0.22, 0.0505, 5);

    // Workbench along the back wall, +Z half.
    const V3 bw = V3(0.46, 0.33, 0.22);
    b.style(bw, WOOD);
    b.box(V3(-3.2, 0.9, 0.98), V3(0.5, 0.06, 1.75), 0.01);
    b.style(bw * 0.8, WOOD);
    for (int i = 0; i < 4; i++) b.box(V3(i < 2 ? -2.98 : -3.4, 0.45, i % 2 ? 0.15 : 1.8), V3(0.07, 0.9, 0.07), 0.01);
    b.box(V3(-3.2, 0.25, 0.98), V3(0.46, 0.04, 1.7), 0.01);
    b.box(V3(-2.97, 0.58, 0.98), V3(0.04, 0.05, 1.7));
    // Vise, toolbox, cans, a hammer.
    b.style(V3(0.15, 0.17, 0.2), PAINT);
    b.box(V3(-2.94, 0.97, 1.6), V3(0.1, 0.1, 0.16), 0.01);
    b.cyl(V3(-2.89, 0.99, 1.6), V3(-2.76, 0.99, 1.6), 0.01, 6);
    b.style(V3(0.62, 0.14, 0.1), PAINT);
    b.box(V3(-3.2, 1.0, 0.5), V3(0.22, 0.14, 0.5), 0.015);
    b.style(V3(0.7, 0.7, 0.7), METAL);
    b.box(V3(-3.2, 1.075, 0.5), V3(0.06, 0.02, 0.2));
    b.style(V3(0.78, 0.7, 0.2), PAINT);
    b.cyl(V3(-3.3, 0.93, 1.1), V3(-3.3, 1.13, 1.1), 0.045, 10);
    b.style(V3(0.25, 0.32, 0.42), PAINT);
    b.cyl(V3(-3.15, 0.93, 1.25), V3(-3.15, 1.07, 1.25), 0.04, 10);
    b.style(V3(0.35, 0.25, 0.17), WOOD);
    b.beam(V3(-3.05, 0.945, 0.75), V3(-3.2, 0.945, 0.85), 0.03, 0.025);
    b.style(V3(0.35, 0.36, 0.38), METAL);
    b.box(V3(-3.2, 0.96, 0.78), V3(0.1, 0.05, 0.03));
    // Pegboard with tools hung on it.
    b.style(V3(0.36, 0.3, 0.22), MATTE);
    b.box(V3(-3.44, 1.55, 1.0), V3(0.02, 0.9, 1.7));
    b.style(V3(0.55, 0.56, 0.58), METAL);
    for (int i = 0; i < 6; i++) {
        const double z = 0.35 + i * 0.27, h = 0.25 + 0.2 * hash01(41, i);
        b.beam(V3(-3.42, 1.9 - h * 0.5, z), V3(-3.42, 1.9 + h * 0.5 - 0.15, z), 0.025, 0.02);
    }
    b.style(V3(0.18, 0.2, 0.22), METAL);
    b.box(V3(-3.42, 1.35, 1.35), V3(0.02, 0.12, 0.22));
    b.box(V3(-3.42, 1.7, 0.85), V3(0.02, 0.03, 0.3));

    // Tyre rack on the back wall, -Z half: a timber frame, two levels, two tyres a level.
    b.style(V3(0.38, 0.28, 0.2), WOOD);
    for (int k = 0; k < 3; k++) b.box(V3(-3.34, 0.62, -1.82 + k * 0.66), V3(0.14, 1.24, 0.07));
    for (int l = 0; l < 3; l++) b.box(V3(-3.34, 0.02 + l * 0.64, -1.17), V3(0.14, 0.05, 1.4));
    for (int l = 0; l < 2; l++)
        for (int k = 0; k < 2; k++) tyreX(b, V3(-3.3, 0.37 + l * 0.64, -1.5 + k * 0.67), 0.31, 0.2);
    // Oil drum by the bench.
    b.style(V3(0.2, 0.28, 0.4), PAINT);
    b.lathe(V3(-3.15, 0, -0.35), {V2(0, 0), V2(0.23, 0), V2(0.24, 0.03), V2(0.24, 0.84), V2(0.23, 0.87), V2(0, 0.87)}, 14, false);

    // Shelves along the +Z wall, front half of the garage.
    const V3 sh = V3(0.34, 0.27, 0.19);
    b.style(sh, WOOD);
    for (int k = 0; k < 2; k++) b.box(V3(-2.6 + k * 2.1, 1.0, 1.77), V3(0.06, 2.0, 0.26));
    for (int l = 0; l < 4; l++) b.box(V3(-1.55, 0.3 + l * 0.55, 1.77), V3(2.2, 0.035, 0.26));
    for (int l = 0; l < 3; l++)
        for (int k = 0; k < 7; k++) {
            const double h = hash01(71 + l, k);
            const double x = -2.4 + k * 0.28 + 0.05 * hash01(5, k + l * 7);
            const double y = 0.3 + l * 0.55 + 0.0175;
            if (h < 0.18) continue;
            if (h < 0.5) {
                b.style(mix(V3(0.7, 0.62, 0.2), V3(0.55, 0.15, 0.1), hash01(9, k + l)), PAINT);
                b.cyl(V3(x, y, 1.77), V3(x, y + 0.12 + 0.08 * hash01(2, k), 1.77), 0.05, 7);
            } else if (h < 0.8) {
                b.style(mix(V3(0.55, 0.45, 0.3), V3(0.35, 0.4, 0.3), hash01(4, k + l * 3)), MATTE);
                b.box(V3(x, y + 0.08, 1.77), V3(0.2, 0.16 + 0.05 * hash01(8, k), 0.18), 0.01);
            } else {
                b.style(V3(0.35, 0.4, 0.28), PAINT);
                b.box(V3(x, y + 0.14, 1.77), V3(0.12, 0.28, 0.2), 0.02);
                b.style(V3(0.1, 0.1, 0.1), MATTE);
                b.cyl(V3(x, y + 0.28, 1.77), V3(x, y + 0.33, 1.77), 0.025, 8);
            }
        }

    // A rolled tarp along the -Z wall, a shovel and a rake against it.
    b.style(V3(0.22, 0.38, 0.5), MATTE);
    b.cyl(V3(-2.1, 0.15, -1.76), V3(-0.8, 0.15, -1.76), 0.14, 10);
    b.style(V3(0.6, 0.5, 0.25), MATTE);
    for (int k = 0; k < 2; k++) b.cyl(V3(-1.9 + k * 0.7, 0.15, -1.76), V3(-1.88 + k * 0.7, 0.15, -1.76), 0.15, 10);
    b.style(V3(0.4, 0.3, 0.2), WOOD);
    b.beam(V3(1.5, 0.05, -1.82), V3(1.62, 1.75, -1.88), 0.035, 0.035);
    b.beam(V3(1.9, 0.05, -1.82), V3(1.98, 1.65, -1.88), 0.035, 0.035);
    b.style(V3(0.4, 0.42, 0.44), METAL);
    b.box(V3(1.5, 0.15, -1.78), V3(0.2, 0.3, 0.015));
    b.box(V3(1.9, 0.1, -1.78), V3(0.3, 0.02, 0.015));
    for (int k = 0; k < 5; k++) b.box(V3(1.9, 0.05, -1.78), V3(0.02, 0.06, 0.015), 0.002), b.box(V3(1.76 + k * 0.07, 0.04, -1.78), V3(0.015, 0.08, 0.015));

    // A bare bulb on its cord over the bench.
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    b.tube({V3(-3.05, ceilY, 0.8), V3(-3.05, ceilY - 0.4, 0.8)}, 0.006, 5);
    b.cyl(V3(-3.05, ceilY - 0.45, 0.8), V3(-3.05, ceilY - 0.4, 0.8), 0.02, 8);
    b.style(V3(1.0, 0.92, 0.7), EMISSIVE);
    b.sphere(V3(-3.05, ceilY - 0.48, 0.8), V3(0.035, 0.045, 0.035), 1);
    (void)wallH;
}

// A gable triangle standing in the YZ plane at x, thickness t.
void gable(B& b, double x, double t, double hw, double y0, double rise) {
    b.push();
    b.move(x, 0, 0);
    b.yaw(90);
    b.extrude({V2(-hw, y0), V2(hw, y0), V2(0, y0 + rise)}, t, 0);
    b.pop();
}

// Unit normal of a roof plane from a to c (in YZ), pointing up.
V3 roofNormal(const V3& a, const V3& c) {
    const V3 d = c - a;
    V3 n(0, -d.z, d.y);
    if (n.y < 0) n = n * -1.0;
    return norm(n);
}

// ---- 1. GARAGE ----------------------------------------------------------------------------------------------------

// TIMBER BARN: board-and-batten, gable tin roof, a pair of swing barn doors.
void garageBarn(B& b, const P& p) {
    const double open = p("open");
    const V3 wc = woodCol(p.i("wood")), rc = roofCol(p.i("roof"));
    const double H = 2.8, rise = 0.95;
    garageInterior(b, H, 2.62);
#ifdef ONLY_INT
    return;
#endif
    // Walls.
    b.style(wc, WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, H / 2, s * 2.0), V3(7.3, H, 0.2), 0.015);
    b.box(V3(-3.55, H / 2, 0), V3(0.2, H, 3.8), 0.015);
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.55, H / 2, s * 1.85), V3(0.2, H, 0.5), 0.015);
    b.box(V3(3.55, 2.65, 0), V3(0.2, 0.3, 3.2), 0.015);
    gable(b, 3.55, 0.2, 2.1, H, rise);
    gable(b, -3.55, 0.2, 2.1, H, rise);
    // Battens (darker) on the outside, and the corner and jamb posts.
    b.style(wc * 0.78, WOOD);
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 21; k++) b.box(V3(-3.5 + k * 0.35, H / 2, s * 2.115), V3(0.06, H - 0.04, 0.02));
    for (int k = 0; k < 11; k++) b.box(V3(-3.665, H / 2, -1.75 + k * 0.35), V3(0.02, H - 0.04, 0.06));
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k < 2; k++) b.box(V3(3.665, H / 2 - 0.1, s * (1.8 + k * 0.28 - (k ? 0 : 0.0))), V3(0.02, H - 0.2, 0.06));
    }
    for (int s = -1; s <= 1; s += 2)
        for (int k = -5; k <= 5; k++) {
            const double z = k * 0.35, yt = H + rise * (1 - std::fabs(z) / 2.1) - 0.05;
            if (yt < H + 0.1) continue;
            for (int e = -1; e <= 1; e += 2)
                if (s == e) b.box(V3(e * 3.665, (H + yt) / 2, z), V3(0.02, yt - H, 0.06));
        }
    b.style(wc * 0.62, WOOD);
    for (int cx = -1; cx <= 1; cx += 2)
        for (int cz = -1; cz <= 1; cz += 2) b.box(V3(cx * 3.58, H / 2, cz * 2.03), V3(0.18, H, 0.18), 0.012);
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.7, 1.25, s * 1.66), V3(0.08, 2.5, 0.1), 0.01);
    b.box(V3(3.7, 2.55, 0), V3(0.08, 0.1, 3.36), 0.01);
    // Plinth and a plank sill along the base.
    b.style(V3(0.45, 0.44, 0.42), CONCRETE);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.07, s * 2.1), V3(7.4, 0.14, 0.06), 0.01);
    // Window on the +Z side.
    b.style(wc * 0.55, WOOD);
    b.box(V3(1.5, 1.5, 2.13), V3(0.9, 0.7, 0.03));
    b.style(V3(0.18, 0.24, 0.28), GLASS);
    b.box(V3(1.5, 1.5, 2.155), V3(0.76, 0.56, 0.01));
    b.style(wc * 0.55, WOOD);
    b.box(V3(1.5, 1.5, 2.163), V3(0.03, 0.56, 0.01));
    b.box(V3(1.5, 1.5, 2.163), V3(0.76, 0.03, 0.01));
    // Roof: tin panels with ribs, ridge cap, fascia; rafters and tie beams inside.
    const double ze = 2.6;
    const double ye = H - (ze - 2.1) * rise / 2.1 + 0.07, yr = H + rise + 0.07;
    for (int s = -1; s <= 1; s += 2) {
        const V3 a(0, ye, s * ze), c(0, yr, 0);
        const V3 n = roofNormal(a, c);
        b.style(rc * 0.9, PAINT);
        b.beam(a, c, 8.0, 0.06, V3(0, 1, 0));
        b.style(rc * 1.12, PAINT);
        for (int k = 0; k < 27; k++) {
            const double x = -3.9 + k * 0.3;
            b.beam(a + V3(x, 0, 0) + n * 0.045, c + V3(x, 0, 0) + n * 0.045, 0.05, 0.035, V3(0, 1, 0));
        }
        b.style(wc * 0.55, WOOD);
        b.box(V3(0, ye - 0.02, s * (ze + 0.0)), V3(8.0, 0.14, 0.05));
    }
    b.style(rc * 0.75, PAINT);
    b.beam(V3(-4.0, yr + 0.07, 0), V3(4.0, yr + 0.07, 0), 0.22, 0.06, V3(0, 1, 0), 0.01);
    b.style(wc * 0.7, WOOD);
    for (int k = 0; k < 7; k++) {
        const double x = -3.3 + k * 1.1;
        b.box(V3(x, H - 0.07, 0), V3(0.1, 0.12, 4.0));
        for (int s = -1; s <= 1; s += 2) b.beam(V3(x, H + 0.0, s * 2.05), V3(x, H + rise + 0.0, 0), 0.08, 0.1, V3(0, 1, 0));
        b.box(V3(x, H + 0.3, 0), V3(0.08, 0.6, 0.08));
    }
    // Downpipe and gutter on the +Z eave.
    b.style(V3(0.3, 0.3, 0.3), METAL);
    b.cyl(V3(-3.7, 0.1, 2.25), V3(-3.7, H - 0.05, 2.25), 0.035, 8);
    b.beam(V3(-3.9, H - 0.14, 2.62), V3(3.9, H - 0.14, 2.62), 0.07, 0.06);

    // The door pair: board-and-batten leaves on strap hinges, swinging out to lie along the front.
    const double hx = 3.7, hz = 1.68, lw = 1.7, lh = 2.55;
    b.both([&]() {
        b.push();
        b.move(hx, 0, hz);
        b.yaw(-open * 100.0);
        b.style(wc * 1.05, WOOD);
        b.box(V3(0.03, 0.04 + lh / 2, -lw / 2), V3(0.06, lh, lw), 0.01);
        b.style(wc * 0.7, WOOD);
        for (int k = 0; k < 5; k++) b.box(V3(0.07, 0.04 + lh / 2, -0.15 - k * 0.37), V3(0.02, lh - 0.06, 0.07));
        b.beam(V3(0.07, 0.25, -0.2), V3(0.07, 2.35, -lw + 0.1), 0.1, 0.02, V3(1, 0, 0));
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int k = 0; k < 2; k++) {
            b.box(V3(0.075, k ? 2.2 : 0.4, -lw / 2), V3(0.012, 0.09, lw - 0.04));
            b.cyl(V3(0, k ? 2.2 : 0.4, 0), V3(0.0, (k ? 2.2 : 0.4) + 0.1, 0), 0.02, 6);
        }
        b.box(V3(0.075, 1.1, -lw + 0.12), V3(0.02, 0.2, 0.04));
        b.box(V3(-0.01, 1.3, -lw / 2), V3(0.02, 0.05, lw - 0.1));  // inside cross-bar
        b.pop();
    });
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.box(V3(3.69, 1.1, 0), V3(0.03, 0.04, 0.35));  // latch bar between the leaves
}

// CORRUGATED STEEL SHED: mono-pitch roof, a roll-up door that rises into the head box.
void garageSteel(B& b, const P& p) {
    const double open = p("open"), rust = p("rust");
    const V3 pc = rustify(paint(p.i("paint")) * 0.95, rust * 0.5);
    const double lowH = 2.8, highH = 3.35;
    auto top = [&](double z) { return lowH + (highH - lowH) * (2.1 - z) / 4.2; };
    garageInterior(b, lowH, top(0.8) - 0.12);
#ifdef ONLY_INT
    return;
#endif
    // Skin: plain steel backing walls; ribs on the outside; a timber frame inside.
    b.style(pc * 0.85, METAL);
    const double side[2] = {lowH, highH};
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, side[s > 0 ? 0 : 1] / 2, s * 2.0), V3(7.3, side[s > 0 ? 0 : 1], 0.2), 0.012);
    b.push();
    b.move(-3.55, 0, 0);
    b.yaw(90);
    b.extrude({V2(-1.9, 0), V2(1.9, 0), V2(1.9, top(-1.9)), V2(-1.9, top(1.9))}, 0.2, 0);
    b.pop();
    // Front wall: two piers and the header, topped by the slope.
    b.push();
    b.move(3.55, 0, 0);
    b.yaw(90);
    b.extrude({V2(-2.1, 0), V2(-1.6, 0), V2(-1.6, top(1.6)), V2(-2.1, top(2.1))}, 0.2, 0);
    b.extrude({V2(1.6, 0), V2(2.1, 0), V2(2.1, top(-2.1)), V2(1.6, top(-1.6))}, 0.2, 0);
    b.extrude({V2(-1.6, 2.5), V2(1.6, 2.5), V2(1.6, top(-1.6)), V2(-1.6, top(1.6))}, 0.2, 0);
    b.pop();
    // Side wall top fill: the high side is taller (box above), done as its own height already. Ribs outside.
    b.style(pc * 1.08, METAL);
    for (int k = 0; k < 37; k++) {
        const double x = -3.6 + k * 0.2;
        b.box(V3(x, lowH / 2, 2.112), V3(0.07, lowH - 0.04, 0.025));
        b.box(V3(x, highH / 2, -2.112), V3(0.07, highH - 0.04, 0.025));
    }
    for (int k = 0; k < 20; k++) {
        const double z = -1.9 + k * 0.2;
        b.box(V3(-3.662, (highH + 0.0) / 2 - 0.1, z), V3(0.025, highH - 0.3, 0.07));
    }
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 3; k++) {
            const double z = s * (1.65 + k * 0.17);
            b.box(V3(3.662, 1.2, z), V3(0.025, 2.3, 0.07));
        }
    // Frame: corner posts and a timber stud wall inside (visible studs).
    b.style(V3(0.36, 0.28, 0.2), WOOD);
    for (int k = 0; k < 6; k++) {
        const double x = -3.0 + k * 1.2;
        b.box(V3(x, 1.4, 1.87), V3(0.09, 2.8, 0.06));
        b.box(V3(x, 1.65, -1.87), V3(0.09, 3.3, 0.06));
    }
    b.box(V3(0, 1.0, 1.87), V3(7.0, 0.07, 0.05));
    b.box(V3(0, 1.0, -1.87), V3(7.0, 0.07, 0.05));
    b.box(V3(0, 1.9, 1.87), V3(7.0, 0.07, 0.05));
    b.box(V3(0, 1.9, -1.87), V3(7.0, 0.07, 0.05));
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.7, 1.25, s * 1.66), V3(0.08, 2.5, 0.1), 0.01);
    b.box(V3(3.7, 2.55, 0), V3(0.08, 0.1, 3.36), 0.01);
    // Corner trim in the body colour.
    b.style(pc * 0.7, PAINT);
    for (int cz = -1; cz <= 1; cz += 2) {
        b.box(V3(3.68, lowH / 2 + (cz < 0 ? 0.275 : 0), cz * 2.1), V3(0.06, cz < 0 ? highH : lowH, 0.06));
        b.box(V3(-3.68, lowH / 2 + (cz < 0 ? 0.275 : 0), cz * 2.1), V3(0.06, cz < 0 ? highH : lowH, 0.06));
    }
    // Rust streaks and patches.
    b.style(V3(0.4, 0.2, 0.09), MATTE);
    for (int k = 0; k < 9; k++) {
        const double x = -3.3 + hash01(51, k) * 6.6, h = 0.15 + 0.5 * hash01(52, k);
        b.box(V3(x, 0.2 + h / 2, 2.128), V3(0.06 + 0.06 * hash01(53, k), h * rust + 0.05, 0.004));
    }
    // Roof: single slope; ribbed panel on rafters.
    const double zr = 2.5;
    const V3 a(0, top(zr) + 0.08, zr), c(0, top(-zr) + 0.08, -zr);
    const V3 n = roofNormal(a, c);
    b.style(rustify(V3(0.55, 0.57, 0.57), rust * 0.6), METAL);
    b.beam(a, c, 8.0, 0.05, V3(0, 1, 0));
    b.style(rustify(V3(0.62, 0.64, 0.64), rust * 0.6), METAL);
    for (int k = 0; k < 42; k++) {
        const double x = -3.95 + k * 0.19;
        b.beam(a + V3(x, 0, 0) + n * 0.04, c + V3(x, 0, 0) + n * 0.04, 0.06, 0.03, V3(0, 1, 0));
    }
    b.style(pc * 0.6, PAINT);
    b.beam(V3(-4.0, top(zr) + 0.04, zr), V3(4.0, top(zr) + 0.04, zr), 0.06, 0.12);
    b.beam(V3(-4.0, top(-zr) + 0.04, -zr), V3(4.0, top(-zr) + 0.04, -zr), 0.06, 0.12);
    b.style(V3(0.36, 0.28, 0.2), WOOD);
    for (int k = 0; k < 7; k++) {
        const double x = -3.3 + k * 1.1;
        b.beam(V3(x, top(2.0) - 0.01, 2.0), V3(x, top(-2.0) - 0.01, -2.0), 0.09, 0.12, V3(0, 1, 0));
    }
    // Gutter and downpipe on the low side.
    b.style(V3(0.35, 0.35, 0.36), METAL);
    b.cyl(V3(-3.7, 0.1, 2.3), V3(-3.7, lowH - 0.05, 2.3), 0.035, 8);
    b.beam(V3(-3.9, lowH - 0.14, 2.62), V3(3.9, lowH - 0.14, 2.62), 0.07, 0.06);
    // Roll-up door: a ribbed curtain lifting into a drum in the head box.
    const double yb = 2.5 * open, topY = 2.6;
    b.style(V3(0.22, 0.22, 0.23), METAL);
    b.cyl(V3(3.3, 2.7, -1.55), V3(3.3, 2.7, 1.55), 0.12, 14);
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.37, 1.25, s * 1.64), V3(0.1, 2.5, 0.06));  // guide rails
    b.style(pc, METAL);
    const double hgt = topY - yb;
    if (hgt > 0.12) {
        b.box(V3(3.4, yb + hgt / 2 + 0.05, 0), V3(0.025, hgt - 0.1, 3.1));
        b.style(pc * 1.15, METAL);
        const int n2 = (int)((hgt - 0.1) / 0.1);
        for (int k = 0; k < n2; k++) b.box(V3(3.42, yb + 0.12 + k * 0.1, 0), V3(0.012, 0.035, 3.1));
    }
    b.style(V3(0.18, 0.18, 0.19), METAL);
    b.box(V3(3.42, yb + 0.05, 0), V3(0.07, 0.1, 3.16), 0.01);  // bottom bar
    if (open < 0.9) {
        b.style(V3(0.7, 0.7, 0.7), METAL);
        b.box(V3(3.47, yb + 0.12, 0.55), V3(0.04, 0.04, 0.18));  // handle
    }
    // Window.
    b.style(V3(0.2, 0.25, 0.3), GLASS);
    b.box(V3(1.5, 1.55, 2.14), V3(0.7, 0.5, 0.012));
    b.style(V3(0.9, 0.9, 0.88), PAINT);
    b.box(V3(1.5, 1.55, 2.138), V3(0.78, 0.04, 0.016));
    b.box(V3(1.5, 1.55, 2.138), V3(0.04, 0.58, 0.016));
}

// STONE-FOOTED LOG GARAGE: round logs on a fieldstone footing, gable shingle roof, an up-and-over plank door.
void garageLog(B& b, const P& p) {
    const double open = p("open");
    const V3 wc = woodCol(p.i("wood")), rc = roofCol(p.i("roof"));
    const double fh = 0.3, r = 0.15, pitch = 0.27;
    const int courses = 10;
    const double H = fh + courses * pitch + 0.03;  // ~3.03
    const double rise = 1.0;
    garageInterior(b, H, H - 0.1);
#ifdef ONLY_INT
    return;
#endif
    // Footing: dry-laid fieldstones round the walls.
    auto stone = [&](double x, double z, double k, int i) {
        const double v = hash01(61, i * 5 + 1), w = hash01(66, i * 5 + 2);
        b.style(mix(V3(0.42, 0.40, 0.37), V3(0.5, 0.46, 0.4), w) * (0.8 + 0.4 * v), ROCK);
        b.blob(V3(x, 0.14, z), V3(0.3 + 0.1 * v, 0.17, 0.22), 0.25, 30 + i, 0, 0.5, 0.0);
        (void)k;
    };
    int si = 0;
    for (int s2 = -1; s2 <= 1; s2 += 2)
        for (int k = 0; k < 15; k++) stone(-3.5 + k * 0.5 + 0.1 * hash01(60, k + (s2 > 0) * 20), s2 * 2.1, 0, si++);
    for (int e2 = -1; e2 <= 1; e2 += 2)
        for (int k = 0; k < 8; k++) {
            const double z = -1.75 + k * 0.5;
            if (e2 > 0 && std::fabs(z) < 1.7) continue;
            stone(e2 * 3.62, z, 0, si++);
        }
    // Footing core under the logs.
    b.style(V3(0.38, 0.37, 0.35), ROCK);
    b.box(V3(0, 0.15, 2.0), V3(7.3, 0.3, 0.2));
    b.box(V3(0, 0.15, -2.0), V3(7.3, 0.3, 0.2));
    b.box(V3(-3.55, 0.15, 0), V3(0.2, 0.3, 3.8));
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.55, 0.15, s * 1.85), V3(0.2, 0.3, 0.5));
    // Log courses. Sides run long with notched ends; ends run between them.
    for (int i = 0; i < courses; i++) {
        const double y = fh + r + i * pitch;
        const V3 col = wc * (0.85 + 0.25 * hash01(80, i));
        b.style(col, WOOD);
        for (int s = -1; s <= 1; s += 2) {
            const double ex = 0.12 + 0.1 * hash01(81 + (s > 0), i);
            b.cyl(V3(-3.65 - ex, y, s * 2.05), V3(3.65 + ex, y, s * 2.05), r, 8);
        }
        const double ez = 2.05 + 0.1 + 0.1 * hash01(83, i);
        b.cyl(V3(-3.6, y, -ez), V3(-3.6, y, ez), r, 8);
        const bool opening = y + r < 2.6;
        if (!opening) {
            b.cyl(V3(3.6, y, -ez), V3(3.6, y, ez), r, 8);
        } else {
            for (int s = -1; s <= 1; s += 2) {
                const double e1 = s * ez, e0 = s * 1.6;
                b.cyl(V3(3.6, y, e0), V3(3.6, y, e1), r, 8);
            }
        }
    }
    // Door jamb planks lining the opening.
    b.style(wc * 0.6, WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(3.6, 1.28, s * 1.64), V3(0.34, 2.56, 0.08), 0.01);
    b.box(V3(3.6, 2.6, 0), V3(0.34, 0.08, 3.36), 0.01);
    // Gable ends: plank-filled, verticals.
    b.style(wc * 0.9, WOOD);
    gable(b, 3.55, 0.14, 2.2, H - 0.05, rise);
    gable(b, -3.55, 0.14, 2.2, H - 0.05, rise);
    b.style(wc * 0.7, WOOD);
    for (int e = -1; e <= 1; e += 2)
        for (int k = -5; k <= 5; k++) {
            const double z = k * 0.38, yt = H + rise * (1 - std::fabs(z) / 2.2) - 0.1;
            if (yt < H + 0.1) continue;
            b.box(V3(e * 3.635, (H - 0.05 + yt) / 2, z), V3(0.02, yt - H + 0.05, 0.06));
        }
    // Shingled roof: panel with overlapping strips along the slope; a ridge beam.
    const double ze = 2.65, ye = H - 0.05 - (ze - 2.2) * rise / 2.2;
    for (int s = -1; s <= 1; s += 2) {
        const V3 a(0, ye + 0.07, s * ze), c(0, H - 0.05 + rise + 0.07, 0);
        const V3 n = roofNormal(a, c);
        b.style(rc * 0.65, WOOD);
        b.beam(a, c, 8.1, 0.07, V3(0, 1, 0));
        for (int k = 0; k < 9; k++) {
            const double t = 0.06 + k * 0.105;
            b.style(rc * (0.9 + 0.2 * hash01(90 + (s > 0), k)), WOOD);
            const V3 q = mix(a, c, t);
            b.beam(q + V3(0, 0, 0) + n * 0.05 - V3(-4.0, 0, 0), q + n * 0.05 + V3(4.0, 0, 0) - V3(0, 0, 0), 0.05, 0.03, V3(0, 1, 0));
        }
    }
    b.style(wc * 0.5, WOOD);
    b.beam(V3(-4.1, H - 0.05 + rise + 0.14, 0), V3(4.1, H - 0.05 + rise + 0.14, 0), 0.16, 0.12, V3(0, 1, 0), 0.02);
    // Purlin logs and trusses inside (above 2.7).
    b.style(wc * 0.8, WOOD);
    for (int k = 0; k < 4; k++) {
        const double x = -3.0 + k * 2.0;
        b.cyl(V3(x, H - 0.17, -2.0), V3(x, H - 0.17, 2.0), 0.07, 8);
    }
    // Up-and-over door: a plank panel riding track from vertical (closed) to flat under the ridge (open).
    const double L = 2.75, th = open * PI_ / 2;
    const double yP = 2.78, xB = 3.45 - 0.03;
    const V3 bot(xB, yP - L * std::cos(th), 0), tp(xB - L * std::sin(th), yP, 0);
    const V3 nrm(std::cos(th), std::sin(th), 0);
    // door sits in the opening plane when closed: push out so that it stands at x = xB.
    b.style(wc * 1.05, WOOD);
    b.beam(bot, tp, 3.12, 0.05, nrm);
    b.style(wc * 0.7, WOOD);
    for (int k = 0; k < 6; k++) {
        const V3 q = mix(bot, tp, 0.08 + k * 0.17) + nrm * 0.035;
        b.beam(q + V3(0, 0, -1.55), q + V3(0, 0, 1.55), 0.1, 0.03, nrm);
    }
    for (int k = 0; k < 9; k++) {
        const double z = -1.4 + k * 0.35;
        b.beam(bot + V3(0, 0, z) + nrm * 0.032, tp + V3(0, 0, z) + nrm * 0.032, 0.035, 0.012, nrm);
    }
    b.style(V3(0.12, 0.12, 0.12), METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.beam(V3(3.38, 0, s * 1.63), V3(3.38, yP, s * 1.63), 0.04, 0.05);      // vertical guide rail
        b.beam(V3(3.38, yP, s * 1.63), V3(0.2, yP + 0.06, s * 1.63), 0.04, 0.05);  // horizontal track
    }
    b.style(V3(0.65, 0.65, 0.65), METAL);
    {
        const V3 q = mix(bot, tp, 0.45) + nrm * 0.07;
        b.box(q + V3(0, 0, 0.55), V3(0.03, 0.03, 0.16));
    }
    // A small lantern by the door.
    b.style(V3(0.1, 0.1, 0.1), METAL);
    b.box(V3(3.72, 2.0, 1.95), V3(0.1, 0.18, 0.1), 0.01);
    b.style(V3(1.0, 0.85, 0.5), EMISSIVE);
    b.box(V3(3.77, 2.0, 1.95), V3(0.02, 0.12, 0.07));
}

Reg garageReg({"garage", "GARAGE", "BUILDINGS", "A remote cabin's detached garage for a pickup: door in the +X end (instance param open 0..1)", 8.0,
               {
                   {"TIMBER BARN", "Board-and-batten, tin gable roof, swing barn doors",
                    {choice("wood", "WOOD", 0, kWoodNames), choice("roof", "ROOF", 0, kRoofNames), num("open", "DOOR OPEN", defOf("OUT_OPEN", 0), 0, 1, 0.05)}, garageBarn},
                   {"STEEL SHED", "Rusting corrugated shed, sloping roof, roll-up door",
                    {paintParam(1), num("rust", "RUST", 0.5, 0, 1, 0.05), num("open", "DOOR OPEN", defOf("OUT_OPEN", 0), 0, 1, 0.05)}, garageSteel},
                   {"LOG GARAGE", "Round logs on a stone footing, shingle roof, up-and-over plank door",
                    {choice("wood", "WOOD", 0, kWoodNames), choice("roof", "ROOF", 3, kRoofNames), num("open", "DOOR OPEN", defOf("OUT_OPEN", 0), 0, 1, 0.05)}, garageLog},
               }});


// ---- shared helpers for the small gear ------------------------------------------------------------------------------

// A flat polygon (u = -z across, v = up) standing in the YZ plane at x, thickness t (centred).
void faceExtrude(B& b, const std::vector<V2>& poly, double x, double t) {
    b.push();
    b.move(x, 0, 0);
    b.yaw(90);
    b.extrude(poly, t, 0);
    b.pop();
}

// Convex polygon clipped to lo <= u <= hi.
std::vector<V2> clipU(std::vector<V2> in, double lo, double hi) {
    for (int pass = 0; pass < 2; pass++) {
        std::vector<V2> out;
        const double sgn = pass == 0 ? 1 : -1, lim = pass == 0 ? lo : -hi;
        const size_t n = in.size();
        for (size_t i = 0; i < n; i++) {
            const V2 a = in[i], c = in[(i + 1) % n];
            const double da = sgn * a.x - lim, dc = sgn * c.x - lim;
            if (da >= 0) out.push_back(a);
            if ((da >= 0) != (dc >= 0)) {
                const double t = da / (da - dc);
                out.push_back(V2(a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t));
            }
        }
        in = out;
        if (in.size() < 3) break;
    }
    return in;
}

// Diagonal hazard stripes over u in [u0,u1], v in [v0,v1] standing in the YZ plane at x (alternating with the base colour).
void hazardStripes(B& b, double x, double u0, double u1, double v0, double v1, double sw, const V3& col) {
    b.style(col, PAINT);
    const double h = v1 - v0;
    for (double u = u0 - h; u < u1; u += 2 * sw) {
        const std::vector<V2> poly = {V2(u, v0), V2(u + sw, v0), V2(u + sw + h, v1), V2(u + h, v1)};
        const std::vector<V2> c = clipU(poly, u0, u1);
        if (c.size() >= 3) faceExtrude(b, c, x, 0.006);
    }
}

// A tube-segment run along local -Z from z=za to zb (positive distances), alternating two colours.
void stripedRail(B& b, double x, double y, double za, double zb, double r, double seg, const V3& c1, const V3& c2, Mat m) {
    int k = 0;
    for (double z = za; z < zb - 1e-6; z += seg, k++) {
        b.style(k % 2 ? c2 : c1, m);
        b.cyl(V3(x, y, -z), V3(x, y, -std::min(zb, z + seg)), r, 8, false);
    }
}

// ---- 5. GATE ------------------------------------------------------------------------------------------------------
// Road along X; the leaf spans Z -2.5..+2.5 when shut. Hinge post at Z +2.7, latch post at Z -2.7. open 0..1 swings the leaf 90 degrees
// about the hinge towards +X (away from a traveller coming from -X).

const double kHingeZ = 2.62;

void gatePipe(B& b, const P& p) {
    const double open = p("open");
    const V3 col = paint(p.i("paint")), white(0.86, 0.85, 0.8);
    // Posts: concrete footings, white steel pipes with caps, a reflector on each.
    for (int s = -1; s <= 1; s += 2) {
        const double hgt = s > 0 ? 1.95 : 1.75;
        b.style(V3(0.5, 0.49, 0.46), CONCRETE);
        b.box(V3(0, 0.08, s * 2.7), V3(0.42, 0.16, 0.42), 0.02);
        b.style(white * 0.95, PAINT);
        b.cyl(V3(0, 0.05, s * 2.7), V3(0, hgt, s * 2.7), 0.075, 12);
        b.cone(V3(0, hgt, s * 2.7), V3(0, hgt + 0.06, s * 2.7), 0.085, 0.0, 12);
        b.style(col, PAINT);
        b.cyl(V3(0, 0.35, s * 2.7), V3(0, 0.75, s * 2.7), 0.081, 12);
        b.cyl(V3(0, 1.05, s * 2.7), V3(0, 1.45, s * 2.7), 0.081, 12);
        b.style(V3(1.0, 0.95, 0.85), EMISSIVE);
        b.box(V3(0.078, 1.62, s * 2.7), V3(0.012, 0.1, 0.08));
    }
    // Hinge pins, and the latch on the other post.
    b.style(V3(0.12, 0.12, 0.13), METAL);
    for (int k = 0; k < 2; k++) {
        b.cyl(V3(0.0, k ? 1.25 : 0.55, kHingeZ), V3(0.0, k ? 1.45 : 0.75, kHingeZ), 0.016, 6);
        b.box(V3(0.05, k ? 1.35 : 0.65, 2.69), V3(0.08, 0.05, 0.1));
    }
    b.box(V3(0.08, 0.95, -2.7), V3(0.08, 0.2, 0.07), 0.01);
    // The leaf.
    b.push();
    b.move(0, 0, kHingeZ);
    b.yaw(-open * 90.0);
    const double L = 5.1, rr = 0.032;
    stripedRail(b, 0, 1.35, 0.06, L, rr, 0.5, col, white, PAINT);
    stripedRail(b, 0, 0.95, 0.06, L, rr, 0.5, white, col, PAINT);
    stripedRail(b, 0, 0.55, 0.06, L, rr, 0.5, col, white, PAINT);
    b.style(white, PAINT);
    for (int k = 0; k < 2; k++) b.cyl(V3(0, 0.5, -(k ? L : 0.06)), V3(0, 1.4, -(k ? L : 0.06)), 0.035, 8);
    b.cyl(V3(0, 0.5, -2.6), V3(0, 1.4, -2.6), 0.025, 8);
    b.cyl(V3(0, 0.55, -0.1), V3(0, 1.35, -L + 0.05), 0.02, 6, false);
    b.style(V3(0.12, 0.12, 0.13), METAL);
    for (int k = 0; k < 2; k++) b.box(V3(0.0, k ? 1.35 : 0.55, -0.15), V3(0.06, 0.07, 0.2), 0.008);
    b.box(V3(0.06, 0.95, -L + 0.12), V3(0.05, 0.12, 0.08), 0.01);
    // Reflectors on the free end.
    b.style(V3(0.9, 0.12, 0.1), EMISSIVE);
    b.box(V3(0.04, 1.2, -L - 0.01), V3(0.012, 0.08, 0.07));
    b.box(V3(0.04, 0.7, -L - 0.01), V3(0.012, 0.08, 0.07));
    // The hazard panel between top and middle rails, and the sign plate between the middle and bottom.
    b.style(V3(0.88, 0.7, 0.08), PAINT);
    b.box(V3(0.045, 1.15, -1.6), V3(0.02, 0.28, 1.0), 0.004);
    hazardStripes(b, 0.057, 1.1, 2.1, 1.01, 1.29, 0.1, V3(0.07, 0.07, 0.07));
    b.style(V3(0.65, 0.1, 0.08), PAINT);
    b.box(V3(0.045, 0.75, -3.4), V3(0.02, 0.3, 0.9), 0.004);
    b.style(V3(0.95, 0.94, 0.9), MATTE);
    b.letters("NO ENTRY", V3(0.055, 0.75, -3.4), 0.0135, 0.004);
    b.pop();
}

void gateTimber(B& b, const P& p) {
    const double open = p("open");
    const V3 wc = woodCol(p.i("wood"));
    const bool chain = p.i("chain") != 0;
    // Posts: square timbers with sloped caps, braced into the ground by a stone collar.
    for (int s = -1; s <= 1; s += 2) {
        const double hgt = s > 0 ? 1.9 : 1.75;
        b.style(V3(0.42, 0.4, 0.37), ROCK);
        b.blob(V3(0.0, 0.06, s * 2.7), V3(0.32, 0.12, 0.32), 0.3, 11 + s, 1, 0.5, 0.0);
        b.style(wc * 0.82, WOOD);
        b.box(V3(0, hgt / 2, s * 2.7), V3(0.2, hgt, 0.2), 0.012);
        b.style(wc * 0.7, WOOD);
        b.extrude({V2(-0.13, hgt), V2(0.13, hgt), V2(0.0, hgt + 0.1)}, 0.26, s * 2.7);
    }
    b.style(V3(0.13, 0.13, 0.14), METAL);
    for (int k = 0; k < 2; k++) {
        b.box(V3(0.11, k ? 1.4 : 0.5, 2.7 - 0.0), V3(0.025, 0.07, 0.24), 0.004);
        b.cyl(V3(0, k ? 1.35 : 0.45, kHingeZ), V3(0, k ? 1.45 : 0.55, kHingeZ), 0.018, 6);
    }
    b.box(V3(0.11, 1.1, -2.7), V3(0.03, 0.08, 0.1), 0.006);
    // The leaf: five rough rails, end stiles, a diagonal brace.
    b.push();
    b.move(0, 0, kHingeZ);
    b.yaw(-open * 90.0);
    const double L = 5.1;
    for (int k = 0; k < 5; k++) {
        b.style(wc * (0.9 + 0.18 * hash01(5, k)), WOOD);
        b.box(V3(0.0, 0.42 + k * 0.23, -L / 2 - 0.03), V3(0.035, 0.15, L - 0.04), 0.008);
    }
    b.style(wc * 0.72, WOOD);
    for (int z = 0; z < 3; z++) b.box(V3(0.05, 0.9, -(z == 0 ? 0.12 : z == 1 ? L / 2 : L - 0.08)), V3(0.04, 1.08, 0.12), 0.008);
    b.beam(V3(0.075, 0.38, -0.12), V3(0.075, 1.38, -L + 0.12), 0.12, 0.035, V3(1, 0, 0));
    b.style(V3(0.13, 0.13, 0.14), METAL);
    for (int k = 0; k < 2; k++) b.box(V3(0.075, k ? 1.35 : 0.5, -0.2), V3(0.015, 0.06, 0.4), 0.003);
    for (int k = 0; k < 5; k++) b.cyl(V3(0.07, 0.42 + k * 0.23, -0.12), V3(0.092, 0.42 + k * 0.23, -0.12), 0.012, 6);
    // Hazard-striped reflector board on the free end (a hung plank) and a ring for the chain.
    b.style(V3(0.9, 0.9, 0.86), PAINT);
    b.box(V3(0.075, 1.3, -L + 0.22), V3(0.012, 0.2, 0.36), 0.002);
    hazardStripes(b, 0.083, L - 0.4, L - 0.04, 1.2, 1.4, 0.06, V3(0.75, 0.1, 0.08));
    b.pop();
    // The chain: from a ring on the latch post to the leaf and back round its stile (leaf ends at z -2.48).
    if (chain) {
        b.style(V3(0.2, 0.2, 0.21), METAL);
        const double lz = kHingeZ - L * (open > 0.5 ? 0 : 1);
        if (open < 0.5) {
            b.tube({V3(0.1, 1.15, -2.55), V3(0.14, 1.0, -2.6), V3(0.15, 0.88, -2.62), V3(0.12, 0.98, -2.68), V3(0.08, 1.15, -2.7)}, 0.011, 5);
            b.torus(V3(0.1, 1.15, -2.7), 0.04, 0.008, 8, 4);
        } else {
            // hanging free from the latch post
            b.tube({V3(0.1, 1.15, -2.7), V3(0.16, 0.9, -2.7), V3(0.17, 0.62, -2.7), V3(0.15, 0.5, -2.7)}, 0.011, 5);
            b.cyl(V3(0.15, 0.46, -2.7), V3(0.15, 0.52, -2.7), 0.025, 6);
        }
        (void)lz;
    }
    // Sign plate: a plain steel plate on the hinge post, bolted at the corners.
    b.style(V3(0.55, 0.57, 0.55), METAL);
    b.box(V3(0.108, 1.5, 2.7), V3(0.015, 0.3, 0.28), 0.004);
    b.style(V3(0.2, 0.2, 0.2), METAL);
    for (int i = 0; i < 4; i++) b.cyl(V3(0.115, 1.5 + (i / 2 ? 0.12 : -0.12), 2.7 + (i % 2 ? 0.1 : -0.1)), V3(0.124, 1.5 + (i / 2 ? 0.12 : -0.12), 2.7 + (i % 2 ? 0.1 : -0.1)), 0.012, 6);
}

Reg gateReg({"gate", "GATE", "BUILDINGS", "A road gate across a 5 m dirt road: leaf spans Z -2.5..2.5, hinge post at Z +2.7 (instance param open 0..1: swings 90 deg to +X)", 5.6,
             {
                 {"PIPE GATE", "Tubular steel pipe gate, red and white stripes, hazard panel, NO ENTRY plate",
                  {paintParam(0), num("open", "GATE OPEN", defOf("OUT_OPEN", 0), 0, 1, 0.05)}, gatePipe},
                 {"CATTLE GATE", "Rough timber rails, chained to the latch post",
                  {choice("wood", "WOOD", 1, kWoodNames), choice("chain", "CHAIN", 1, {"OFF", "ON"}), num("open", "GATE OPEN", defOf("OUT_OPEN", 0), 0, 1, 0.05)}, gateTimber},
             }});


// ---- 3. FUEL PUMP -------------------------------------------------------------------------------------------------

// The nozzle: a handle, a body, a spout, a trigger, in the +X direction from the point.
void nozzle(B& b, const V3& at, double yawDeg, const V3& col) {
    b.push();
    b.move(at);
    b.yaw(yawDeg);
    b.style(V3(0.1, 0.1, 0.11), MATTE);
    b.box(V3(0.04, 0.0, 0), V3(0.1, 0.09, 0.05), 0.012);
    b.style(col, PAINT);
    b.box(V3(0.11, 0.02, 0), V3(0.12, 0.07, 0.06), 0.012);
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.cyl(V3(0.17, 0.025, 0), V3(0.34, 0.0, 0), 0.012, 7);
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    b.beam(V3(0.07, -0.02, 0), V3(0.12, -0.07, 0), 0.015, 0.012);
    b.pop();
}

// The 1950s upright: plinth, a bulging pressed-steel body, round dial faces, a lit glass globe.
void pumpUpright(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const double lit = p.i("lit");
    b.style(V3(0.52, 0.51, 0.49), CONCRETE);
    b.box(V3(0.05, 0.08, 0), V3(0.9, 0.16, 0.56), 0.03);
    // Body: a tapered tower, base skirt, trim bands.
    b.style(col, PAINT);
    b.box(V3(0, 0.25, 0), V3(0.44, 0.18, 0.44), 0.03);
    b.box(V3(0, 0.92, 0), V3(0.36, 1.34, 0.4), 0.05);
    b.box(V3(0, 1.62, 0), V3(0.42, 0.28, 0.46), 0.05);
    b.style(V3(0.75, 0.75, 0.74), CHROME);
    b.box(V3(0, 0.355, 0), V3(0.46, 0.03, 0.46), 0.01);
    b.box(V3(0, 1.25, 0), V3(0.385, 0.03, 0.425), 0.01);
    b.box(V3(0, 1.755, 0), V3(0.45, 0.025, 0.49), 0.01);
    // Louvred vent low on the sides, a panel and a dial face on the front and back.
    b.style(col * 0.6, PAINT);
    for (int k = 0; k < 4; k++)
        for (int s = -1; s <= 1; s += 2) b.box(V3(0.0, 0.55 + k * 0.06, s * 0.205), V3(0.22, 0.025, 0.012));
    for (int e = -1; e <= 1; e += 2) {
        b.push();
        b.yaw(e < 0 ? 180 : 0);
        b.style(V3(0.9, 0.88, 0.8), MATTE);
        b.cyl(V3(0.215, 1.62, 0), V3(0.235, 1.62, 0), 0.115, 18);
        b.style(V3(0.78, 0.78, 0.76), CHROME);
        b.torus(V3(0.22, 1.62, 0), 0.118, 0.012, 18, 5, 360);
        b.style(V3(0.08, 0.08, 0.08), MATTE);
        b.box(V3(0.24, 1.62, 0.0), V3(0.006, 0.02, 0.13));
        b.beam(V3(0.242, 1.62, 0), V3(0.242, 1.7, 0.03), 0.012, 0.004);
        b.style(V3(0.12, 0.12, 0.12), MATTE);
        b.box(V3(0.185, 1.2, 0), V3(0.012, 0.2, 0.3), 0.004);
        b.style(V3(0.85, 0.84, 0.78), MATTE);
        for (int k = 0; k < 3; k++) b.box(V3(0.193, 1.2, -0.08 + k * 0.08), V3(0.006, 0.12, 0.05));
        b.pop();
    }
    // Globe cradle and the lit glass globe, a cap on top.
    b.style(V3(0.7, 0.7, 0.7), CHROME);
    b.cyl(V3(0, 1.77, 0), V3(0, 1.84, 0), 0.17, 16);
    b.style(lit ? V3(0.95, 0.84, 0.5) : V3(0.82, 0.8, 0.7), lit ? EMISSIVE : GLASS);
    b.sphere(V3(0, 2.07, 0), V3(0.2, 0.23, 0.2), 2);
    b.style(col, PAINT);
    b.cyl(V3(0, 2.28, 0), V3(0, 2.34, 0), 0.1, 14);
    b.cone(V3(0, 2.34, 0), V3(0, 2.4, 0), 0.1, 0.02, 14);
    // Hose from the top of the body round to a holster on the front, nozzle hung up.
    b.style(V3(0.08, 0.08, 0.09), MATTE);
    b.tube({V3(0.1, 1.4, 0.2), V3(0.22, 1.35, 0.3), V3(0.35, 0.85, 0.32), V3(0.4, 0.62, 0.2), V3(0.34, 0.9, 0.05)}, 0.022, 7);
    b.style(V3(0.65, 0.65, 0.66), METAL);
    b.box(V3(0.21, 0.83, 0.0), V3(0.05, 0.06, 0.08), 0.01);
    nozzle(b, V3(0.24, 0.9, 0.0), 0, col * 0.9);
    // The pump's wear: a band of grime on the plinth.
    b.style(V3(0.18, 0.17, 0.15), MATTE);
    b.box(V3(0.05, 0.015, 0), V3(0.91, 0.03, 0.57), 0.004);
}

// Twin pumps on an island under a little pitched canopy.
void pumpIsland(B& b, const P& p) {
    const V3 col = paint(p.i("paint")), steel(0.6, 0.62, 0.63);
    b.style(V3(0.5, 0.49, 0.47), CONCRETE);
    b.box(V3(0, 0.015, 0), V3(6.0, 0.03, 3.5), 0.006);
    b.style(V3(0.58, 0.57, 0.54), CONCRETE);
    b.box(V3(0, 0.1, 0), V3(4.3, 0.2, 1.1), 0.05);
    b.style(V3(0.86, 0.72, 0.1), PAINT);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * 2.17, 0.1, 0), V3(0.04, 0.12, 1.12), 0.01);
    // Two pumps: slab-sided cabinets with a display head, hose and nozzle each.
    for (int s = -1; s <= 1; s += 2) {
        b.push();
        b.move(s * 1.05, 0.2, 0);
        b.style(col, PAINT);
        b.box(V3(0, 0.7, 0), V3(0.55, 1.4, 0.4), 0.04);
        b.style(V3(0.82, 0.82, 0.8), PAINT);
        b.box(V3(0, 1.5, 0), V3(0.6, 0.35, 0.45), 0.05);
        for (int f = -1; f <= 1; f += 2) {
            b.style(V3(0.08, 0.09, 0.1), GLASS);
            b.box(V3(0, 1.52, f * 0.231), V3(0.4, 0.16, 0.012), 0.004);
            b.style(V3(0.95, 0.8, 0.3), EMISSIVE);
            b.box(V3(0, 1.52, f * 0.238), V3(0.26, 0.06, 0.008));
            b.style(V3(0.15, 0.16, 0.17), PAINT);
            b.box(V3(0, 0.95, f * 0.205), V3(0.4, 0.3, 0.012), 0.005);
            b.style(V3(0.75, 0.75, 0.74), CHROME);
            b.box(V3(0.0, 0.2, f * 0.205), V3(0.4, 0.03, 0.012));
            // hose and nozzle on each face
            b.style(V3(0.08, 0.08, 0.09), MATTE);
            b.tube({V3(0.22, 1.1, f * 0.22), V3(0.3, 1.0, f * 0.33), V3(0.33, 0.7, f * 0.35), V3(0.3, 0.55, f * 0.31)}, 0.02, 6);
            nozzle(b, V3(0.28, 0.45, f * 0.285), f < 0 ? 90 : -90, col * 0.85);
        }
        b.style(V3(0.8, 0.12, 0.1), PAINT);
        b.box(V3(0, 1.72, 0), V3(0.64, 0.05, 0.49), 0.01);
        b.pop();
    }
    // Two canopy posts at the island ends, the roof: flat gable ridge along X.
    b.style(steel, METAL);
    for (int e = -1; e <= 1; e += 2) {
        b.cyl(V3(e * 2.4, 0.0, 0), V3(e * 2.4, 3.2, 0), 0.13, 12);
        b.box(V3(e * 2.4, 0.1, 0), V3(0.42, 0.2, 0.42), 0.03);
    }
    b.style(V3(0.2, 0.22, 0.24), METAL);
    b.box(V3(0, 3.2, 0), V3(5.4, 0.18, 0.35), 0.02);
    const double yE = 3.2, rise = 0.45, hz = 1.75;
    for (int s = -1; s <= 1; s += 2) {
        const V3 a(0, 3.27, s * hz), c(0, 3.27 + rise, 0);
        b.style(V3(0.8, 0.8, 0.78), PAINT);
        b.beam(a, c, 6.0, 0.08, V3(0, 1, 0));
        b.style(col, PAINT);
        b.box(V3(0, 3.27 - 0.1, s * hz), V3(6.0, 0.38, 0.1), 0.012);  // fascia band
        b.style(V3(0.95, 0.95, 0.95), MATTE);
        for (int k = 0; k < 6; k++) b.box(V3(-2.5 + k, 3.27 - 0.1, s * hz + s * 0.052), V3(0.4, 0.16, 0.006));
    }
    (void)yE;
    // Soffit lights, a light-box on each end.
    b.style(V3(1.0, 0.95, 0.8), EMISSIVE);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * 1.2, 3.1, 0), V3(0.9, 0.04, 0.5), 0.01);
    // Price sign on its own pole at the +X end of the island.
    b.style(steel, METAL);
    b.cyl(V3(3.6, 0, 0), V3(3.6, 2.7, 0), 0.06, 8);
    b.style(V3(0.9, 0.9, 0.88), PAINT);
    b.box(V3(3.6, 2.95, 0), V3(0.16, 0.7, 1.2), 0.03);
    b.style(col, PAINT);
    b.box(V3(3.68, 3.15, 0), V3(0.01, 0.22, 1.14));
    b.style(V3(0.96, 0.96, 0.94), MATTE);
    b.letters("FUEL", V3(3.69, 3.15, 0), 0.026, 0.005);
    b.style(V3(0.1, 0.1, 0.1), PAINT);
    b.box(V3(3.68, 2.78, 0), V3(0.01, 0.38, 1.1));
    b.style(V3(0.95, 0.8, 0.3), EMISSIVE);
    for (int k = -1; k <= 1; k += 2) b.box(V3(3.69, 2.78, k * 0.28), V3(0.006, 0.14, 0.4));
    // Bollards at both ends of the island.
    b.style(V3(0.86, 0.72, 0.1), PAINT);
    for (int e = -1; e <= 1; e += 2)
        for (int s = -1; s <= 1; s += 2) b.cyl(V3(e * 2.0, 0.2, s * 0.46), V3(e * 2.0, 0.85, s * 0.46), 0.07, 8);
}

// A drum on a timber cradle: hand pump, hose, a couple of jerry cans.
void drumRack(B& b, const P& p) {
    const V3 col = paint(p.i("paint")), wc = woodCol(p.i("wood"));
    const double R = 0.29, len = 0.88;
    // Cradle: two A-frame ends and a base rail.
    b.style(wc, WOOD);
    for (int e = -1; e <= 1; e += 2) {
        const double z = e * 0.32;
        b.box(V3(0, 0.06, z), V3(0.6, 0.12, 0.1), 0.01);
        b.beam(V3(-0.2, 0.1, z), V3(-0.08, 0.5, z), 0.1, 0.08);
        b.beam(V3(0.2, 0.1, z), V3(0.08, 0.5, z), 0.1, 0.08);
    }
    b.box(V3(-0.28, 0.06, 0), V3(0.1, 0.1, 0.8), 0.01);
    b.box(V3(0.28, 0.06, 0), V3(0.1, 0.1, 0.8), 0.01);
    // The drum lying along Z, its chime rings; the bung plate on top.
    b.style(col, PAINT);
    b.push();
    b.move(0, 0.52 + R, 0);
    b.roll(90);
    b.lathe(V3(0, -len / 2, 0), {V2(0, 0), V2(R * 0.96, 0), V2(R, 0.03), V2(R, len - 0.03), V2(R * 0.96, len), V2(0, len)}, 16, false);
    b.style(col * 0.85, PAINT);
    for (int k = 1; k <= 3; k++) b.torus(V3(0, -len / 2 + k * len / 4, 0), R + 0.004, 0.011, 16, 4);
    b.pop();
    b.style(V3(0.55, 0.55, 0.55), METAL);
    b.cyl(V3(0, 0.52 + 2 * R - 0.01, 0.1), V3(0, 0.52 + 2 * R + 0.03, 0.1), 0.05, 10);
    // Rotary hand pump: a standing pipe, the crank and the spout.
    b.style(V3(0.35, 0.37, 0.38), METAL);
    b.cyl(V3(0, 0.52 + 2 * R + 0.03, 0.1), V3(0, 1.28, 0.1), 0.022, 8);
    b.cyl(V3(0, 1.1, 0.1), V3(0, 1.3, 0.1), 0.035, 8);
    b.style(V3(0.78, 0.15, 0.1), PAINT);
    b.cyl(V3(0.0, 1.3, 0.1), V3(0.0, 1.34, 0.1), 0.05, 8);
    b.beam(V3(0.0, 1.32, 0.1), V3(-0.12, 1.32, 0.35), 0.025, 0.025);
    b.style(V3(0.08, 0.08, 0.09), MATTE);
    b.box(V3(-0.13, 1.32, 0.38), V3(0.03, 0.03, 0.12));
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.cyl(V3(0.0, 1.18, 0.1), V3(0.18, 1.2, 0.1), 0.014, 7);
    // Hose: from the spout down and round, with the nozzle laid on the top.
    b.style(V3(0.08, 0.08, 0.09), MATTE);
    b.tube({V3(0.18, 1.2, 0.1), V3(0.27, 1.1, 0.12), V3(0.3, 0.8, 0.2), V3(0.28, 0.55, 0.3), V3(0.2, 0.55, 0.32)}, 0.016, 6);
    nozzle(b, V3(0.2, 0.56, 0.32), 90, col);
    // Jerry cans on the ground by the cradle.
    for (int k = 0; k < 2; k++) {
        b.push();
        b.move(0.55 + k * 0.12, 0, -0.35 + k * 0.52);
        b.yaw(k ? -20 : 14);
        b.style(k ? V3(0.76, 0.12, 0.1) : V3(0.3, 0.34, 0.2), PAINT);
        b.box(V3(0, 0.23, 0), V3(0.16, 0.46, 0.34), 0.025);
        b.style(V3(0.08, 0.08, 0.08), MATTE);
        b.tube({V3(-0.04, 0.47, -0.1), V3(-0.04, 0.52, -0.1), V3(-0.04, 0.52, 0.1), V3(-0.04, 0.47, 0.1)}, 0.012, 5);
        b.style(V3(0.7, 0.7, 0.6), METAL);
        b.cyl(V3(0, 0.46, 0.12), V3(0, 0.5, 0.12), 0.03, 8);
        b.pop();
    }
}

Reg fuelPumpReg({"fuel_pump", "FUEL PUMP", "BUILDINGS", "Remote fuel: an old roadside pump, a pump island under a canopy, or a drum with a hand pump", 3.0,
                 {
                     {"UPRIGHT PUMP", "1950s roadside pump with a lit globe on a plinth", {paintParam(0), choice("lit", "GLOBE LIT", 1, {"OFF", "ON"})}, pumpUpright},
                     {"PUMP ISLAND", "Twin pumps on a concrete island under a pitched canopy, price sign", {paintParam(5)}, pumpIsland},
                     {"DRUM RACK", "200 L drum on a timber cradle, hand pump, jerry cans", {paintParam(1), choice("wood", "WOOD", 3, kWoodNames)}, drumRack},
                 }});


// ---- 2. RANGER STATION --------------------------------------------------------------------------------------------
// Front +X. Body 6 (X) x 10 (Z) m on a 0.3 m plinth, a 2.5 m deep porch across the front, a gable (log) or mono-pitch (prefab) roof.

// A wall-mounted bulletin board with pinned notices and a little roof; x0 = the wall plane, board on its +X side.
void bulletin(B& b, double x0, double y, double z, const V3& wc) {
    b.style(wc * 0.7, WOOD);
    b.box(V3(x0 + 0.04, y, z), V3(0.06, 0.98, 1.38), 0.01);
    b.style(V3(0.6, 0.45, 0.3), MATTE);
    b.box(V3(x0 + 0.075, y, z), V3(0.012, 0.86, 1.26));
    for (int k = 0; k < 6; k++) {
        b.style(mix(V3(0.9, 0.9, 0.85), V3(0.85, 0.8, 0.5), hash01(14, k)), MATTE);
        const double zz = z - 0.46 + (k % 3) * 0.44 + 0.04 * hash01(3, k), yy = y + (k / 3 ? -0.2 : 0.2) + 0.03 * hash01(4, k);
        b.box(V3(x0 + 0.085, yy, zz), V3(0.006, 0.28 - 0.04 * hash01(6, k), 0.21));
        b.style(V3(0.8, 0.1, 0.1), MATTE);
        b.cyl(V3(x0 + 0.088, yy + 0.11, zz), V3(x0 + 0.095, yy + 0.11, zz), 0.012, 5);
    }
    b.style(wc * 0.6, WOOD);
    b.beam(V3(x0 + 0.02, y + 0.58, z - 0.74), V3(x0 + 0.2, y + 0.5, z), 0.05, 0.04, V3(0, 1, 0));
    b.beam(V3(x0 + 0.02, y + 0.58, z + 0.74), V3(x0 + 0.2, y + 0.5, z), 0.05, 0.04, V3(0, 1, 0));
    b.box(V3(x0 + 0.13, y + 0.58, z - 0.37), V3(0.3, 0.025, 0.74));
    b.box(V3(x0 + 0.13, y + 0.58, z + 0.37), V3(0.3, 0.025, 0.74));
}

// A slatted bench with its back to the wall plane x0, seat centred on z.
void bench(B& b, double x0, double z, double y0, const V3& wc) {
    b.style(wc * 0.85, WOOD);
    for (int k = 0; k < 3; k++) b.box(V3(x0 + 0.17 + k * 0.1, y0 + 0.45, z), V3(0.08, 0.04, 1.7), 0.006);
    for (int k = 0; k < 2; k++) b.box(V3(x0 + 0.06, y0 + 0.65 + k * 0.14, z), V3(0.035, 0.1, 1.7));
    b.style(wc * 0.65, WOOD);
    for (int e = -1; e <= 1; e += 2) {
        b.box(V3(x0 + 0.17, y0 + 0.22, z + e * 0.75), V3(0.06, 0.44, 0.06));
        b.box(V3(x0 + 0.36, y0 + 0.22, z + e * 0.75), V3(0.06, 0.44, 0.06));
        b.box(V3(x0 + 0.06, y0 + 0.5, z + e * 0.75), V3(0.05, 0.6, 0.06));
    }
}

// A neat stack of split firewood (log ends toward +X), spanning z0..z0+w, rows x cols, on a pair of bearers.
void woodStack(B& b, double x0, double y0, double z0, int rows, int cols) {
    b.style(V3(0.3, 0.22, 0.15), WOOD);
    b.box(V3(x0 + 0.3, y0 + 0.04, z0 + cols * 0.07), V3(0.1, 0.08, cols * 0.14 + 0.1));
    b.box(V3(x0 + 0.02, y0 + 0.04, z0 + cols * 0.07), V3(0.1, 0.08, cols * 0.14 + 0.1));
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            const double h = hash01(31, r * 17 + c);
            b.style(mix(V3(0.5, 0.36, 0.22), V3(0.62, 0.5, 0.34), h) * (0.85 + 0.2 * hash01(32, r * 9 + c)), WOOD);
            b.cyl(V3(x0 - 0.02, y0 + 0.14 + r * 0.125, z0 + 0.07 + c * 0.14), V3(x0 + 0.34, y0 + 0.14 + r * 0.125, z0 + 0.07 + c * 0.14), 0.062 + 0.01 * h, 6);
        }
}

// A flagpole at the base point: pole, ball, and a forest-green flag with a pine emblem flying toward +Z.
void flagpole(B& b, double x, double z, double h) {
#ifdef NO_POLES
    return;
#endif
    b.style(V3(0.5, 0.49, 0.46), CONCRETE);
    b.cyl(V3(x, 0.0, z), V3(x, 0.25, z), 0.28, 10);
    b.style(V3(0.86, 0.86, 0.84), PAINT);
    b.lathe(V3(x, 0.25, z), {V2(0, 0), V2(0.075, 0), V2(0.06, h * 0.5), V2(0.04, h - 0.25), V2(0.035, h - 0.25 - 0.0)}, 8, true);
    b.style(V3(0.85, 0.7, 0.2), METAL);
    b.sphere(V3(x, h + 0.18, z), V3(0.07, 0.07, 0.07), 1);
    // Flag: three wavy panels.
    const double fy = h - 1.2;
    for (int k = 0; k < 3; k++) {
        b.push();
        b.move(x, fy, z + 0.08 + k * 0.5);
        b.yaw(k == 1 ? 6 : -4);
        b.style(V3(0.16, 0.36, 0.22), MATTE);
        b.box(V3(0, 0.45, 0.25), V3(0.014, 0.9, 0.5));
        b.style(V3(0.88, 0.86, 0.72), MATTE);
        if (k == 1) {
            b.box(V3(0.012, 0.45, 0.25), V3(0.006, 0.3, 0.05));
            b.box(V3(0.012, 0.52, 0.25), V3(0.006, 0.24, 0.16));
            b.box(V3(0.012, 0.64, 0.25), V3(0.006, 0.12, 0.09));
        }
        b.pop();
    }
    b.style(V3(0.7, 0.7, 0.7), METAL);
    b.tube({V3(x + 0.04, fy + 0.9, z), V3(x + 0.1, fy + 0.9, z + 0.07)}, 0.006, 4);
    b.tube({V3(x + 0.04, fy + 0.02, z), V3(x + 0.1, fy + 0.02, z + 0.07)}, 0.006, 4);
}

// A radio mast: tapered pipe on a footing with three guy wires, a small dish, a whip antenna and (on the ground) an equipment cabinet.
void radioMast(B& b, const V3& at, double h, bool cabinet, bool guys, double dishYaw) {
#ifdef NO_POLES
    return;
#endif
    b.push();
    b.move(at);
    b.style(V3(0.5, 0.49, 0.46), CONCRETE);
    b.box(V3(0, 0.08, 0), V3(0.6, 0.16, 0.6), 0.02);
    b.style(V3(0.62, 0.64, 0.64), METAL);
    b.lathe(V3(0, 0.1, 0), {V2(0, 0), V2(0.08, 0), V2(0.07, h * 0.5), V2(0.045, h)}, 8, false);
    for (int k = 1; k <= 3; k++) b.torus(V3(0, 0.1 + k * h * 0.25, 0), 0.075 - k * 0.01, 0.012, 8, 4);
    // Dish near the top, a bracket, and the feed horn.
    b.push();
    b.move(0, h * 0.82, 0);
    b.yaw(dishYaw);
    b.box(V3(0.08, 0, 0), V3(0.1, 0.06, 0.1));
    b.style(V3(0.86, 0.86, 0.84), PAINT);
    b.push();
    b.move(0.15, 0, 0);
    b.pitch(-90);
    b.lathe(V3(0, 0, 0), {V2(0, 0), V2(0.08, 0.0), V2(0.28, 0.06), V2(0.42, 0.16), V2(0.4, 0.17), V2(0.0, 0.03)}, 14, true);
    b.pop();
    b.style(V3(0.3, 0.3, 0.3), METAL);
    b.tube({V3(0.17, 0, 0), V3(0.5, 0, 0)}, 0.012, 5);
    b.pop();
    // Whip on the top.
    b.style(V3(0.2, 0.2, 0.2), METAL);
    b.cone(V3(0, h, 0), V3(0, h + 2.4, 0), 0.025, 0.004, 5);
    b.cyl(V3(0.2, h - 0.5, 0.0), V3(0.2, h + 0.5, 0.0), 0.012, 4);
    b.box(V3(0.1, h - 0.5, 0), V3(0.2, 0.025, 0.025));
    if (guys)
        for (int k = 0; k < 3; k++) {
            const double a = k * 2.094 + 0.6, hh = h * 0.7;
            b.style(V3(0.25, 0.25, 0.26), METAL);
            b.tube({V3(0, hh, 0), V3(std::cos(a) * h * 0.25, hh * 0.5 + 0.1, std::sin(a) * h * 0.25), V3(std::cos(a) * h * 0.5, 0.06, std::sin(a) * h * 0.5)}, 0.007, 4);
            b.cyl(V3(std::cos(a) * h * 0.5, 0.0, std::sin(a) * h * 0.5), V3(std::cos(a) * h * 0.5, 0.18, std::sin(a) * h * 0.5), 0.03, 5);
        }
    if (cabinet) {
        b.style(V3(0.38, 0.4, 0.38), PAINT);
        b.box(V3(0.55, 0.5, 0.0), V3(0.45, 0.7, 0.55), 0.02);
        b.style(V3(0.2, 0.22, 0.2), PAINT);
        b.box(V3(0.55, 0.88, 0.0), V3(0.5, 0.05, 0.6), 0.01);
        b.box(V3(0.78, 0.5, 0.0), V3(0.012, 0.55, 0.4));
        b.style(V3(0.1, 0.12, 0.2), GLASS);
        b.push();
        b.move(0.55, 0.9, -0.3);
        b.pitch(30);
        b.box(V3(0, 0.3, 0), V3(0.5, 0.025, 0.6));
        b.pop();
    }
    b.pop();
}

// Window with trim: frame, glass (lit warm or dark), muntins, on the plane x = x0 facing +X.
void windowX(B& b, double x0, double y, double z, double w, double h, const V3& trim, bool lit) {
    b.style(trim, WOOD);
    b.box(V3(x0 + 0.03, y, z), V3(0.05, h + 0.14, w + 0.14), 0.01);
    b.style(lit ? V3(1.0, 0.82, 0.5) : V3(0.14, 0.2, 0.24), lit ? EMISSIVE : GLASS);
    b.box(V3(x0 + 0.06, y, z), V3(0.012, h, w));
    b.style(trim * 0.9, WOOD);
    b.box(V3(x0 + 0.072, y, z), V3(0.02, h, 0.035));
    b.box(V3(x0 + 0.072, y, z), V3(0.02, 0.035, w));
    b.box(V3(x0 + 0.07, y - h / 2 - 0.09, z), V3(0.14, 0.04, w + 0.2));  // sill
}

// A plank door on the plane x0 with trim, a handle and a hood light.
void doorX(B& b, double x0, double y0, double z, double w, double h, const V3& col, const V3& trim) {
    b.style(trim, WOOD);
    b.box(V3(x0 + 0.03, y0 + h / 2 + 0.05, z), V3(0.05, h + 0.1, w + 0.2), 0.01);
    b.style(col, WOOD);
    b.box(V3(x0 + 0.065, y0 + h / 2, z), V3(0.05, h, w), 0.01);
    b.style(col * 0.75, WOOD);
    for (int k = -1; k <= 1; k++) b.box(V3(x0 + 0.095, y0 + h / 2, z + k * w * 0.3), V3(0.012, h - 0.1, 0.04));
    b.box(V3(x0 + 0.095, y0 + h * 0.55, z), V3(0.012, 0.06, w - 0.1));
    b.style(V3(0.78, 0.78, 0.76), METAL);
    b.box(V3(x0 + 0.115, y0 + h * 0.46, z + w * 0.38), V3(0.03, 0.03, 0.14), 0.005);
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.box(V3(x0 + 0.1, y0 + h + 0.35, z + w / 2 + 0.2), V3(0.12, 0.2, 0.1), 0.01);
    b.style(V3(1.0, 0.85, 0.55), EMISSIVE);
    b.box(V3(x0 + 0.16, y0 + h + 0.35, z + w / 2 + 0.2), V3(0.02, 0.14, 0.06));
}

// 2a. LOG HQ.
void rangerLog(B& b, const P& p) {
    const V3 wc = woodCol(p.i("wood")), rc = roofCol(p.i("roof"));
    const bool lit = p.i("lit") != 0, flag = p.i("flag") != 0;
    const double fh = 0.3, r = 0.14, pitch = 0.25;
    const int courses = 11;
    const double wallTop = fh + courses * pitch + 0.03;  // ~3.08
    // Plinth: a stone course round the footprint and under the porch, stone piers.
    b.style(V3(0.4, 0.39, 0.37), ROCK);
    b.box(V3(0, 0.15, 5.0), V3(6.4, 0.3, 0.5), 0.02);
    b.box(V3(0, 0.15, -5.0), V3(6.4, 0.3, 0.5), 0.02);
    b.box(V3(-2.9, 0.15, 0), V3(0.5, 0.3, 10.0), 0.02);
    b.box(V3(2.9, 0.15, 0), V3(0.5, 0.3, 10.0), 0.02);
    for (int k = 0; k < 24; k++) {
        const double v = hash01(7, k), w = hash01(8, k);
        b.style(mix(V3(0.42, 0.40, 0.37), V3(0.52, 0.47, 0.4), w) * (0.8 + 0.4 * v), ROCK);
        const int side = k % 4;
        const double t = (k / 4) / 5.0 - 0.5;
        if (side == 0) b.blob(V3(3.18, 0.14, t * 9.8), V3(0.18, 0.14, 0.45), 0.2, 40 + k, 0, 0.5, 0.0);
        else if (side == 1) b.blob(V3(-3.18, 0.14, t * 9.8), V3(0.18, 0.14, 0.45), 0.2, 40 + k, 0, 0.5, 0.0);
        else if (side == 2) b.blob(V3(t * 5.8, 0.14, 5.22), V3(0.45, 0.14, 0.18), 0.2, 40 + k, 0, 0.5, 0.0);
        else b.blob(V3(t * 5.8, 0.14, -5.22), V3(0.45, 0.14, 0.18), 0.2, 40 + k, 0, 0.5, 0.0);
    }
    // Log courses: long (front/back) logs through the corners on even courses, short ones on odd.
    for (int i = 0; i < courses; i++) {
        const double y = fh + r + i * pitch;
        b.style(wc * (0.85 + 0.25 * hash01(80, i)), WOOD);
        const bool even = i % 2 == 0;
        const double lz = even ? 5.3 : 4.9, sx = even ? 2.9 : 3.3;
        for (int s = -1; s <= 1; s += 2) {
            b.cyl(V3(s * 2.95, y, -lz), V3(s * 2.95, y, lz), r, 8);
            b.cyl(V3(-sx, y, s * 5.0), V3(sx, y, s * 5.0), r, 8);
        }
    }
    // Gable ends of boards.
    b.style(wc * 0.9, WOOD);
    for (int s = -1; s <= 1; s += 2) {
        b.extrude({V2(-3.1, wallTop - 0.05), V2(3.1, wallTop - 0.05), V2(0, 4.65)}, 0.16, s * 5.0);
    }
    // Front: door, windows, notice board, bench, wood, sign. (Applied to the log face at x ~ 3.08.)
    const double fx = 3.08;
    doorX(b, fx, 0.3, 0.0, 1.0, 2.05, wc * 0.55, wc * 0.4);
    windowX(b, fx, 1.35, 2.3, 1.1, 1.0, wc * 0.4, lit);
    windowX(b, fx, 1.35, -2.3, 1.1, 1.0, wc * 0.4, lit);
    bulletin(b, fx, 1.5, -3.9, wc);
    bench(b, fx, 3.75, 0.3, wc);
    // Side and back windows.
    for (int s = -1; s <= 1; s += 2) {
        b.push();
        b.move(0, 0, s * 5.08);
        b.yaw(s > 0 ? -90 : 90);
        windowX(b, 0.0, 1.5, -1.2, 1.1, 1.0, wc * 0.4, lit);
        windowX(b, 0.0, 1.5, 1.2, 1.1, 1.0, wc * 0.4, false);
        b.pop();
    }
    b.push();
    b.move(-3.03, 0, 0);
    b.yaw(180);
    for (int k = -1; k <= 1; k += 2) windowX(b, 0.0, 1.5, k * 2.6, 1.1, 1.0, wc * 0.4, false);
    b.pop();
    // Porch: deck, posts, rails, steps.
    b.style(wc * 0.8, WOOD);
    b.box(V3(4.25, 0.26, 0), V3(2.5, 0.08, 10.2), 0.01);
    b.style(wc * 0.55, WOOD);
    for (int k = 0; k < 14; k++) b.box(V3(4.25, 0.31, -4.8 + k * 0.74), V3(2.46, 0.015, 0.025));
    for (int e = -1; e <= 1; e += 2) b.box(V3(5.52, 0.26, e * 2.0), V3(0.06, 0.1, 6.0));
    b.style(wc * 0.8, WOOD);
    const double pz[6] = {-4.9, -3.3, -1.7, 1.7, 3.3, 4.9};
    for (int k = 0; k < 6; k++) b.cyl(V3(5.35, 0.3, pz[k]), V3(5.35, 2.85, pz[k]), 0.09, 8);
    b.style(wc * 0.7, WOOD);
    for (int s = -1; s <= 1; s += 2) {
        const double za = s * 1.7, zb = s * 4.9;
        b.box(V3(5.35, 0.95, (za + zb) / 2), V3(0.07, 0.06, std::fabs(zb - za)), 0.006);
        b.box(V3(5.35, 0.6, (za + zb) / 2), V3(0.05, 0.05, std::fabs(zb - za)));
    }
    for (int k = 0; k < 3; k++) b.box(V3(5.75 + k * 0.28, 0.25 - k * 0.08 - 0.02 + 0.0, 0), V3(0.3, 0.1 + 0.0, 3.0), 0.006);
    // Porch beam, roof edges and the sign.
    b.style(wc * 0.6, WOOD);
    b.box(V3(5.35, 2.9, 0), V3(0.2, 0.2, 10.2), 0.01);
    b.style(V3(0.14, 0.24, 0.17), PAINT);
    b.box(V3(5.47, 2.72, 0), V3(0.03, 0.4, 2.9), 0.006);
    b.style(V3(0.92, 0.9, 0.82), MATTE);
    b.letters("RANGER STATION", V3(5.485, 2.72, 0), 0.0285, 0.004);
    // Roofs: main gable, the porch shed roof running on from its front slope, ribbed tin.
    const double yr = 4.7, ye = 3.0, xe = 3.3, xp = 5.55, yp = 2.95;
    for (int s = -1; s <= 1; s += 2) {
        const V3 a(0, yr, 0), c(s * xe, ye, 0);
        const V3 d = c - a;
        V3 nn(-d.y, d.x, 0);
        if (nn.y < 0) nn = nn * -1.0;
        nn = norm(nn);
        b.style(rc * 0.88, PAINT);
        b.beam(a, c, 10.9, 0.06, V3(0, 1, 0));
        b.style(rc * 1.1, PAINT);
        for (int k = 0; k < 31; k++) {
            const double z = -5.4 + k * 0.36;
            b.beam(a + V3(0, 0, z) + nn * 0.045, c + V3(0, 0, z) + nn * 0.045, 0.05, 0.035, V3(0, 1, 0));
        }
    }
    {
        const V3 a(xe - 0.05, ye + 0.0, 0), c(xp, yp - 0.5, 0);
        const V3 d = c - a;
        V3 nn(-d.y, d.x, 0);
        if (nn.y < 0) nn = nn * -1.0;
        nn = norm(nn);
        b.style(rc * 0.88, PAINT);
        b.beam(a, c, 10.9, 0.06, V3(0, 1, 0));
        b.style(rc * 1.1, PAINT);
        for (int k = 0; k < 31; k++) {
            const double z = -5.4 + k * 0.36;
            b.beam(a + V3(0, 0, z) + nn * 0.045, c + V3(0, 0, z) + nn * 0.045, 0.05, 0.035, V3(0, 1, 0));
        }
    }
    b.style(rc * 0.7, PAINT);
    b.beam(V3(0, yr + 0.07, -5.45), V3(0, yr + 0.07, 5.45), 0.22, 0.06, V3(0, 1, 0));
    // Rafter tails along the eaves.
    b.style(wc * 0.6, WOOD);
    for (int k = 0; k < 11; k++)
        for (int s = -1; s <= 1; s += 2) b.beam(V3(s * 3.0, 3.0, -5.0 + k), V3(s * 3.5, 2.88, -5.0 + k), 0.08, 0.1, V3(0, 1, 0));
    // Stovepipe through the roof, with a cap.
    b.style(V3(0.2, 0.2, 0.21), METAL);
    b.cyl(V3(-1.2, 2.5, -2.0), V3(-1.2, 5.4, -2.0), 0.085, 8);
    b.cone(V3(-1.2, 5.4, -2.0), V3(-1.2, 5.55, -2.0), 0.14, 0.02, 8);
    // Flagpole, mast, wood stack.
    if (flag) flagpole(b, 7.0, -4.0, 8.0);
    radioMast(b, V3(-5.2, 0, 2.5), 9.0, true, true, 150);
    woodStack(b, 3.12, 0.3, -4.85, 6, 5);
}

// 2b. PREFAB HUT with a lean-to shop bay.
void rangerPrefab(B& b, const P& p) {
    const V3 pc = paint(p.i("paint")), trim = V3(0.28, 0.3, 0.27);
    const bool lit = p.i("lit") != 0, flag = p.i("flag") != 0;
    const double fh = 0.35, H = 2.95;
    // Concrete pad round everything, piers under the porch.
    b.style(V3(0.52, 0.51, 0.49), CONCRETE);
    b.box(V3(0.0, 0.15, 0), V3(6.4, 0.3, 10.4), 0.02);
    b.box(V3(0.0, 0.15, 7.0), V3(6.4, 0.3, 4.2), 0.02);
    b.box(V3(4.4, 0.14, 0), V3(2.8, 0.28, 10.4), 0.02);
    // Main body: two modules side by side (seam), cladding in panel colour with a darker base band.
    b.style(pc, PAINT);
    b.box(V3(0, fh + (H - fh) / 2, 0), V3(5.9, H - fh, 9.9), 0.03);
    b.style(pc * 0.7, PAINT);
    b.box(V3(0, 0.55, 0), V3(5.96, 0.5, 9.96), 0.02);
    b.style(trim, PAINT);
    b.box(V3(0, H + 0.02, 0), V3(6.1, 0.1, 10.1), 0.015);
    for (int k = 0; k < 9; k++) b.box(V3(3.0, 1.7, -4.4 + k * 1.1), V3(0.02, 2.3, 0.05));
    b.box(V3(3.0, 1.7, 0), V3(0.025, 2.5, 0.1));
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k < 4; k++) b.box(V3(-2.2 + k * 1.5, 1.7, s * 4.97), V3(0.05, 2.3, 0.02));
    }
    // Roof: mono-pitch membrane, higher at the back, running forward over the porch; parapet at the back.
    const V3 a(-3.2, 3.5, 0), c(5.55, 3.0, 0);
    b.style(V3(0.36, 0.37, 0.38), MATTE);
    b.beam(a, c, 10.7, 0.12, V3(0, 1, 0), 0.02);
    b.style(trim, PAINT);
    b.beam(V3(5.55, 2.9, -5.35), V3(5.55, 2.9, 5.35), 0.04, 0.42);  // front fascia
    for (int s = -1; s <= 1; s += 2) b.beam(V3(-3.2, 3.62, s * 5.35), V3(5.55, 3.12, s * 5.35), 0.04, 0.2, V3(0, 1, 0));
    b.beam(V3(-3.2, 3.58, -5.35), V3(-3.2, 3.58, 5.35), 0.05, 0.2);
    // Rooftop: unit, vents.
    b.style(V3(0.62, 0.64, 0.64), METAL);
    b.box(V3(-0.5, 3.55 + 0.25, 3.0), V3(1.3, 0.5, 1.0), 0.02);
    b.style(V3(0.15, 0.15, 0.15), METAL);
    b.cyl(V3(-0.5, 4.06, 3.0), V3(-0.5, 4.1, 3.0), 0.36, 12);
    for (int k = 0; k < 2; k++) b.cyl(V3(-1.8 + k * 0.5, 3.5, -3.5), V3(-1.8 + k * 0.5, 4.2, -3.5), 0.06, 6);
    // Front: door, windows (a long band each side), notice board, bench, wood.
    const double fx = 3.0;
    b.style(trim, PAINT);
    b.box(V3(fx + 0.04, fh + 1.1, 0), V3(0.06, 2.2, 1.25), 0.01);
    b.style(V3(0.6, 0.62, 0.58), PAINT);
    b.box(V3(fx + 0.09, fh + 1.05, 0), V3(0.04, 2.05, 1.0), 0.01);
    b.style(V3(0.14, 0.2, 0.24), GLASS);
    b.box(V3(fx + 0.115, fh + 1.5, 0), V3(0.01, 0.5, 0.4));
    b.style(V3(0.8, 0.8, 0.8), METAL);
    b.box(V3(fx + 0.13, fh + 1.0, 0.38), V3(0.03, 0.03, 0.18), 0.005);
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k < 2; k++) {
            const double z = s * (1.8 + 1.35 * k + (k ? 0.1 : 0.0));
            b.style(trim, PAINT);
            b.box(V3(fx + 0.035, 1.55, z), V3(0.05, 1.0, 1.3), 0.008);
            b.style(lit && k == 0 ? V3(1.0, 0.82, 0.5) : V3(0.14, 0.2, 0.24), lit && k == 0 ? EMISSIVE : GLASS);
            b.box(V3(fx + 0.065, 1.55, z), V3(0.012, 0.86, 1.14));
            b.style(trim, PAINT);
            b.box(V3(fx + 0.075, 1.55, z), V3(0.02, 0.86, 0.04));
        }
    }
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.box(V3(fx + 0.1, 2.75, 0.85), V3(0.1, 0.18, 0.18), 0.01);
    b.style(V3(1.0, 0.85, 0.55), EMISSIVE);
    b.box(V3(fx + 0.15, 2.75, 0.85), V3(0.02, 0.12, 0.12));
    bulletin(b, fx + 0.0, 1.55, -4.0, V3(0.35, 0.33, 0.3));
    bench(b, fx, 3.9 - 0.0, 0.3, woodCol(0));
    // Porch: steel posts and rails, deck planks, steps.
    b.style(V3(0.5, 0.42, 0.34), WOOD);
    for (int k = 0; k < 14; k++) b.box(V3(4.4, 0.305, -4.8 + k * 0.74), V3(2.76, 0.03, 0.68));
    b.style(trim, METAL);
    const double pz[6] = {-5.1, -3.4, -1.7, 1.7, 3.4, 5.1};
    for (int k = 0; k < 6; k++) b.box(V3(5.5, 1.6, pz[k]), V3(0.1, 2.6, 0.1), 0.008);
    b.style(V3(0.6, 0.62, 0.62), METAL);
    for (int s = -1; s <= 1; s += 2) {
        const double za = s * 1.7, zb = s * 5.1;
        for (int k = 0; k < 2; k++) b.beam(V3(5.5, 0.6 + k * 0.4, za), V3(5.5, 0.6 + k * 0.4, zb), 0.04, 0.04);
        b.beam(V3(3.1, 1.0, s * 5.1), V3(5.5, 1.0, s * 5.1), 0.04, 0.04);
        b.beam(V3(3.1, 0.65, s * 5.1), V3(5.5, 0.65, s * 5.1), 0.04, 0.04);
    }
    for (int k = 0; k < 3; k++) b.box(V3(5.95 + k * 0.28, 0.17 + (2 - k) * 0.0 - k * 0.02, 0), V3(0.3, 0.3 - k * 0.1, 2.9));
    b.style(V3(0.9, 0.7, 0.1), PAINT);
    b.box(V3(5.55, 0.31, 0), V3(0.06, 0.01, 3.0));
    // Sign on the fascia.
    b.style(V3(0.16, 0.26, 0.2), PAINT);
    b.box(V3(5.585, 2.72, 0), V3(0.025, 0.4, 2.9), 0.005);
    b.style(V3(0.92, 0.9, 0.82), MATTE);
    b.letters("RANGER STATION", V3(5.6, 2.72, 0), 0.0285, 0.004);
    // Lean-to shop bay on the +Z side: open +X face, rolled-up door coil, workbench, drum, tyres.
    b.style(pc * 0.95, PAINT);
    b.box(V3(-2.9, 1.5, 7.0), V3(0.12, 2.6, 4.0), 0.01);
    b.box(V3(0.0, 1.5, 9.05), V3(5.9, 2.6, 0.12), 0.01);
    b.style(pc * 1.1, PAINT);
    for (int k = 0; k < 14; k++) b.box(V3(-0.2 + (k - 6.5) * 0.4, 1.5, 9.12), V3(0.06, 2.5, 0.02));
    b.style(trim, PAINT);
    b.beam(V3(3.0, 2.72, 5.0), V3(3.0, 2.72, 9.1), 0.14, 0.3);  // header
    for (int s = 0; s < 2; s++) b.box(V3(3.0, 1.4, s ? 9.0 : 5.12), V3(0.14, 2.8, 0.14), 0.01);
    b.style(V3(0.32, 0.34, 0.34), METAL);
    b.cyl(V3(2.85, 2.5, 5.2), V3(2.85, 2.5, 8.9), 0.14, 12);  // the door's coil
    b.style(pc * 0.85, METAL);
    b.box(V3(2.88, 2.62, 7.0), V3(0.04, 0.12, 3.6));
    b.style(V3(0.36, 0.37, 0.38), MATTE);
    b.box(V3(0.2, 3.0, 7.05), V3(6.7, 0.1, 4.3), 0.02);
    b.style(trim, PAINT);
    b.box(V3(3.6, 2.98, 7.05), V3(0.06, 0.28, 4.35), 0.005);
    // Bay contents.
    b.style(V3(0.5, 0.42, 0.3), WOOD);
    b.box(V3(-2.4, 0.9, 7.0), V3(0.6, 0.06, 2.6), 0.01);
    b.style(V3(0.35, 0.36, 0.36), METAL);
    for (int e = -1; e <= 1; e += 2) b.box(V3(-2.4, 0.45, 7.0 + e * 1.2), V3(0.5, 0.9, 0.06));
    b.style(V3(0.6, 0.12, 0.1), PAINT);
    b.box(V3(-2.4, 1.0, 6.5), V3(0.25, 0.14, 0.5), 0.015);
    b.style(V3(0.2, 0.28, 0.4), PAINT);
    b.cyl(V3(-2.2, 0.0, 8.5), V3(-2.2, 0.88, 8.5), 0.26, 12);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    for (int k = 0; k < 3; k++) b.cyl(V3(0.5 + 0.0, 0.0 + k * 0.2, 8.4), V3(0.5, 0.18 + k * 0.2, 8.4), 0.34, 10);
    b.style(V3(0.2, 0.22, 0.24), METAL);
    b.beam(V3(2.9, 2.55, 7.0), V3(-2.9, 2.55, 7.0), 0.12, 0.2);  // monorail beam
    b.style(V3(0.8, 0.65, 0.1), PAINT);
    b.box(V3(0.8, 2.2, 7.0), V3(0.3, 0.4, 0.25), 0.02);
    b.style(V3(0.2, 0.2, 0.2), METAL);
    b.tube({V3(0.8, 2.0, 7.0), V3(0.8, 1.3, 7.0)}, 0.01, 4);
    // Propane tank behind, flag and mast.
    b.style(V3(0.82, 0.82, 0.8), PAINT);
    b.push();
    b.move(-1.5, 0.45, -5.9);
    b.roll(90);
    b.lathe(V3(0, -0.8, 0), {V2(0, 0), V2(0.25, 0.04), V2(0.34, 0.2), V2(0.36, 0.8), V2(0.34, 1.4), V2(0.25, 1.56), V2(0, 1.6)}, 14, true);
    b.pop();
    b.style(V3(0.4, 0.4, 0.4), METAL);
    for (int e = -1; e <= 1; e += 2) b.box(V3(-1.5 + e * 0.7, 0.15, -5.9), V3(0.1, 0.3, 0.6));
    if (flag) flagpole(b, 7.2, -4.2, 8.0);
    radioMast(b, V3(-1.5, 3.5, -3.8), 5.5, false, false, 20);
}

Reg rangerReg({"ranger_station", "RANGER STATION", "BUILDINGS", "The backcountry ranger HQ: porch, notice board, flagpole and radio mast (front +X)", 13.0,
               {
                   {"LOG HQ", "Round-log hall, green tin roof, deep porch, stone plinth",
                    {choice("wood", "WOOD", 0, kWoodNames), choice("roof", "ROOF", 0, kRoofNames), choice("lit", "WINDOWS LIT", 1, {"OFF", "ON"}), choice("flag", "FLAGPOLE", 1, {"OFF", "ON"})}, rangerLog},
                   {"PREFAB HUT", "Modular cladding, flat roof over the porch, lean-to shop bay",
                    {paintParam(2), choice("lit", "WINDOWS LIT", 1, {"OFF", "ON"}), choice("flag", "FLAGPOLE", 1, {"OFF", "ON"})}, rangerPrefab},
               }});


// ---- 4. MINE HEADFRAME --------------------------------------------------------------------------------------------
// The shaft's axis is the vertical line x = 0, z = 0 (collar at the origin). The frame stands behind it (-X) carrying the sheave wheel
// (axle along Z at x = -1.15, y = 10.4, radius 1.15); the hoist cable runs down the shaft from the wheel's front and back to the
// hoist house at -X. The ore bin and its chute are out front (+X).

const double kWheelR = 1.15, kWheelX = -1.15, kWheelY = 10.4;

V3 weatherWood(double rust) { return mix(V3(0.44, 0.33, 0.22), V3(0.5, 0.47, 0.42), rust * 0.75) * (1.0 - 0.28 * rust); }
V3 weatherSteel(double rust, double k = 1.0) { return mix(V3(0.46, 0.48, 0.5), V3(0.45, 0.22, 0.1), rust * 0.85) * k; }

// The sheave wheel on its axle (axle along Z at (cx, cy)), spoked, with flanges and bearing blocks.
void sheave(B& b, double cx, double cy, double rust) {
    b.push();
    b.move(cx, cy, 0);
    b.style(weatherSteel(rust, 0.7), METAL);
    b.push();
    b.roll(90);
    b.torus(V3(0, 0, 0), kWheelR, 0.075, 20, 6);
    b.torus(V3(0, 0.15, 0), kWheelR + 0.05, 0.035, 20, 4);
    b.torus(V3(0, -0.15, 0), kWheelR + 0.05, 0.035, 20, 4);
    b.cyl(V3(0, -0.2, 0), V3(0, 0.2, 0), 0.2, 12);
    for (int k = 0; k < 10; k++) {
        const double a = k * 2 * PI_ / 10;
        b.beam(V3(0, 0, 0), V3(std::cos(a) * (kWheelR - 0.05), 0, std::sin(a) * (kWheelR - 0.05)), 0.07, 0.1, V3(0, 1, 0));
    }
    b.pop();
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.cyl(V3(0, 0, -0.75), V3(0, 0, 0.75), 0.05, 8);
    for (int e = -1; e <= 1; e += 2) b.box(V3(0, -0.12, e * 0.6), V3(0.35, 0.28, 0.2), 0.02);
    b.pop();
}

// The cable: down from the wheel's front into the shaft, and from the back over to the hoist house's roof.
void cables(B& b, double rust, const V3& houseEnd) {
    b.style(V3(0.12, 0.1, 0.09) + V3(0.1, 0.05, 0.02) * rust, MATTE);
    std::vector<V3> pf;
    pf.push_back(V3(0, 0.0, 0));
    for (int k = 0; k <= 8; k++) {
        const double a = k * (PI_ / 8);  // from the front tangent over the top to the back tangent
        pf.push_back(V3(kWheelX + std::cos(a) * (kWheelR + 0.03), kWheelY + std::sin(a) * (kWheelR + 0.03), 0));
    }
    // Back tangent aims at the house.
    pf.push_back(houseEnd);
    std::vector<V3> path;
    path.push_back(pf[0]);
    for (size_t i = 1; i < pf.size(); i++) path.push_back(pf[i]);
    b.tube(path, 0.04, 6, false, false);
}

// Timber cribbing round the shaft's mouth, a dark hole inside.
void shaftCollar(B& b, const V3& wood, double rust) {
    b.style(V3(0.02, 0.02, 0.02), MATTE);
    b.box(V3(0, 0.012, 0), V3(1.7, 0.024, 1.7));
    for (int i = 0; i < 4; i++) {
        b.style(wood * (0.85 + 0.2 * hash01(2, i)), WOOD);
        const double y = 0.1 + i * 0.2, ex = (i % 2) ? 0.15 : 0.0;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            b.box(V3(0, y, sgn * 1.0), V3(2.4 + ex, 0.19, 0.22), 0.01);
            b.box(V3(sgn * 1.0, y, 0), V3(0.22, 0.19, 2.4 - ex), 0.01);
        }
    }
    // A plank guard rail across the +X side and warning tape.
    b.style(wood * 0.8, WOOD);
    b.box(V3(1.0, 1.0, 0), V3(0.06, 0.1, 2.2));
    b.box(V3(1.0, 0.75, 0), V3(0.06, 0.1, 2.2));
    for (int e = -1; e <= 1; e += 2) b.box(V3(1.0, 0.55, e * 1.1), V3(0.1, 0.9, 0.1));
    (void)rust;
}

// A ladder up a slope from a to c (rails along it, rungs across z).
void ladder(B& b, const V3& a, const V3& c, double w, double spacing, const V3& col, Mat m) {
    b.style(col, m);
    for (int e = -1; e <= 1; e += 2) b.beam(a + V3(0, 0, e * w / 2), c + V3(0, 0, e * w / 2), 0.05, 0.03, V3(1, 0, 0));
    const double L = len(c - a);
    const int n = (int)(L / spacing);
    for (int k = 1; k < n; k++) {
        const V3 q = mix(a, c, (double)k / n);
        b.cyl(q + V3(0.03, 0, -w / 2), q + V3(0.03, 0, w / 2), 0.015, 5, false);
    }
}

// The hoist house at the back: sheeted walls, mono-pitch roof, a stovepipe, a door and a window; returns the cable's entry point.
V3 hoistHouse(B& b, const V3& wall, const V3& roof, double rust, bool steelWalls) {
    const double x0 = -9.4, x1 = -5.6, hz = 1.7;
    b.style(V3(0.45, 0.44, 0.42), CONCRETE);
    b.box(V3((x0 + x1) / 2, 0.1, 0), V3(x1 - x0 + 0.3, 0.2, 2 * hz + 0.3), 0.02);
    b.style(wall, steelWalls ? METAL : WOOD);
    b.box(V3((x0 + x1) / 2, 1.75, -hz), V3(x1 - x0, 3.1, 0.14), 0.01);
    b.box(V3((x0 + x1) / 2, 1.75, hz), V3(x1 - x0, 3.1, 0.14), 0.01);
    b.box(V3(x0, 1.75, 0), V3(0.14, 3.1, 2 * hz), 0.01);
    b.box(V3(x1, 1.75, 0), V3(0.14, 3.1, 2 * hz), 0.01);
    b.style(wall * (steelWalls ? 1.15 : 0.75), steelWalls ? METAL : WOOD);
    for (int k = 0; k < (steelWalls ? 16 : 10); k++) {
        const double x = x0 + 0.1 + k * (x1 - x0 - 0.2) / (steelWalls ? 15 : 9);
        for (int e = -1; e <= 1; e += 2) b.box(V3(x, 1.75, e * (hz + 0.085)), V3(0.07, 3.05, 0.025));
    }
    for (int k = 0; k < 8; k++) b.box(V3(x1 + 0.085, 1.75, -1.5 + k * 0.43), V3(0.025, 3.05, 0.07));
    // Door and a lit-glass window on the +Z side, a hatch on the front wall.
    b.style(wall * 0.55, WOOD);
    b.box(V3(-7.0, 1.0, hz + 0.1), V3(0.95, 2.0, 0.05), 0.01);
    b.style(V3(0.9, 0.78, 0.5), EMISSIVE);
    b.box(V3(-8.5, 1.9, hz + 0.09), V3(0.6, 0.5, 0.02));
    b.style(wall * 0.5, WOOD);
    b.box(V3(-8.5, 1.9, hz + 0.1), V3(0.7, 0.04, 0.03));
    b.box(V3(-8.5, 1.9, hz + 0.1), V3(0.04, 0.6, 0.03));
    // Roof: one pitch, tin, with the stack through it.
    const V3 a(-9.7, 4.0, 0), c(-5.3, 3.3, 0);
    b.style(roof, METAL);
    b.beam(a, c, 4.0, 0.07, V3(0, 1, 0));
    b.style(roof * 1.12, METAL);
    for (int k = 0; k < 11; k++) {
        const double z = -1.8 + k * 0.36;
        b.beam(a + V3(0, 0.045, z), c + V3(0, 0.045, z), 0.045, 0.03, V3(0, 1, 0));
    }
    b.style(V3(0.2, 0.18, 0.17) + V3(0.18, 0.08, 0.02) * rust, METAL);
    b.cyl(V3(-9.0, 3.0, -0.8), V3(-9.0, 5.8, -0.8), 0.17, 10);
    b.cone(V3(-9.0, 5.8, -0.8), V3(-9.0, 5.95, -0.8), 0.24, 0.12, 10);
    // A bit of clutter: split logs for the boiler, a barrel.
    b.style(V3(0.2, 0.28, 0.3), PAINT);
    b.cyl(V3(-5.0, 0.0, 1.2), V3(-5.0, 0.85, 1.2), 0.26, 10);
    const double yE = a.y + (c.y - a.y) * (-7.2 - a.x) / (c.x - a.x);
    return V3(-7.2, yE + 0.08, 0);
}

// The ore bin out front: a hopper on legs with a chute and a heap of ore. steel: sheet-steel hopper on a steel frame; else timber.
void oreBin(B& b, bool steel, const V3& col, double rust) {
    const double bx = 4.6, bz = -3.0;
    const Mat m = steel ? METAL : WOOD;
    b.style(col, m);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.beam(V3(bx + sx * 0.9, 0, bz + sz * 0.9), V3(bx + sx * 0.9, 3.0, bz + sz * 0.9), 0.22, 0.22);
    b.style(col * 0.85, m);
    for (int k = 0; k < 2; k++) {
        b.beam(V3(bx - 0.9, 0.6 + k * 1.5, bz - 0.9), V3(bx + 0.9, 1.8 + k * 1.1 - 1.2 * (1 - k) + 0.0, bz - 0.9), 0.08, 0.14);
        b.beam(V3(bx - 0.9, 0.6 + k * 1.5, bz + 0.9), V3(bx + 0.9, 1.8 + k * 1.1 - 1.2 * (1 - k) + 0.0, bz + 0.9), 0.08, 0.14);
    }
    // The hopper body: upper box, then a funnel to the chute mouth on +X.
    b.style(col * 1.0, m);
    b.box(V3(bx, 3.7, bz), V3(2.1, 1.2, 2.1), 0.03);
    b.style(col * 0.7, m);
    b.push();
    b.move(bx, 3.1, bz);
    b.extrude({V2(-1.05, 0.0), V2(1.05, 0.0), V2(0.55, -0.9), V2(-0.4, -0.9)}, 2.1, 0.0);
    b.pop();
    b.style(V3(0.1, 0.09, 0.08), MATTE);
    b.box(V3(bx, 4.31, bz), V3(1.7, 0.02, 1.7));
    // Chute: two side boards and a floor from the hopper's mouth out and down to the heap.
    b.style(col * 0.8, m);
    b.beam(V3(bx + 0.5, 2.3, bz), V3(bx + 2.4, 1.3, bz), 0.7, 0.07, V3(0, 1, 0));
    for (int e = -1; e <= 1; e += 2) b.beam(V3(bx + 0.5, 2.48, bz + e * 0.37), V3(bx + 2.4, 1.48, bz + e * 0.37), 0.06, 0.25, V3(0, 1, 0));
    // Ore heap: dark rock lumps.
    for (int k = 0; k < 6; k++) {
        const double h = hash01(91, k), w = hash01(92, k);
        b.style(mix(V3(0.3, 0.26, 0.22), V3(0.45, 0.3, 0.2), w), ROCK);
        b.blob(V3(bx + 2.6 + (h - 0.5) * 1.2, 0.3 + 0.1 * w, bz + (w - 0.5) * 1.4), V3(0.55 + 0.2 * h, 0.4 + 0.2 * w, 0.5), 0.35, 100 + k, 1, 0.7, 0.0);
    }
    (void)rust;
}

// 4a. TIMBER A-FRAME: the old lost mine.
void headframeTimber(B& b, const P& p) {
    const double rust = p("rust");
    const V3 wc = weatherWood(rust), steelc = weatherSteel(rust);
    const double Ht = 11.5;
    auto leg = [&](bool front, int s, double y) {
        const double t = y / Ht;
        const double x0 = front ? 1.9 : -4.4, z0 = 2.4;
        return V3(x0 * (1 - t) + kWheelX * t, y, s * (z0 * (1 - t) + 0.75 * t));
    };
    b.style(wc * 0.7, WOOD);
    for (int f = 0; f < 2; f++)
        for (int s = -1; s <= 1; s += 2) {
            const V3 g = leg(f == 0, s, 0.0);
            b.box(V3(g.x, 0.12, g.z), V3(0.9, 0.24, 0.6), 0.02);
        }
    // Four leaning legs.
    b.style(wc, WOOD);
    for (int f = 0; f < 2; f++)
        for (int s = -1; s <= 1; s += 2) b.beam(leg(f == 0, s, 0.1), leg(f == 0, s, Ht), 0.42, 0.42, V3(0, 1, 0), 0.02);
    // Cross ties at three levels: across the frame (Z), and fore-and-aft (X), with diagonals.
    const double lv[4] = {2.4, 5.0, 7.5, 9.6};
    for (int i = 0; i < 4; i++) {
        b.style(wc * (0.9 + 0.15 * hash01(4, i)), WOOD);
        for (int f = 0; f < 2; f++) b.beam(leg(f == 0, -1, lv[i]), leg(f == 0, 1, lv[i]), 0.28, 0.3);
        for (int s = -1; s <= 1; s += 2) b.beam(leg(true, s, lv[i]), leg(false, s, lv[i]), 0.26, 0.28);
    }
    b.style(wc * 0.85, WOOD);
    for (int i = 0; i < 3; i++)
        for (int s = -1; s <= 1; s += 2) {
            b.beam(leg(true, s, lv[i]), leg(false, s, lv[i + 1]), 0.2, 0.22);
            b.beam(leg(false, s, lv[i]), leg(true, s, lv[i + 1]), 0.2, 0.22);
        }
    for (int i = 0; i < 3; i++)
        for (int f = 0; f < 2; f++) {
            b.beam(leg(f == 0, -1, lv[i]), leg(f == 0, 1, lv[i + 1]), 0.18, 0.2);
            b.beam(leg(f == 0, 1, lv[i]), leg(f == 0, -1, lv[i + 1]), 0.18, 0.2);
        }
    // Head: cap beams, the platform and a railing, iron straps at the joints.
    b.style(wc * 1.0, WOOD);
    b.box(V3(kWheelX, Ht + 0.2, 0), V3(1.4, 0.4, 2.4), 0.02);
    b.box(V3(kWheelX, Ht - 0.7, 0), V3(1.2, 0.3, 2.0), 0.02);
    b.style(wc * 0.9, WOOD);
    for (int k = 0; k < 6; k++) b.box(V3(kWheelX + 1.6, Ht - 1.3, -1.05 + k * 0.42), V3(1.8, 0.07, 0.38));
    b.style(steelc, METAL);
    for (int k = 0; k < 12; k++) {
        const int f = k % 2, s = (k / 2) % 2 ? 1 : -1;
        const double y = lv[k / 3];
        const V3 g = leg(f == 0, s, y);
        b.box(V3(g.x, g.y, g.z), V3(0.46, 0.12, 0.46), 0.01);
    }
    for (int s = -1; s <= 1; s += 2) b.box(V3(kWheelX, Ht - 0.1, s * 0.78), V3(0.5, 0.35, 0.05));
    ladder(b, leg(true, 1, 0.3) + V3(0.3, 0, 0.0), leg(true, 1, Ht - 1.4) + V3(0.3, 0, 0.0), 0.5, 0.45, wc * 0.7, WOOD);
    sheave(b, kWheelX, kWheelY, rust);
    shaftCollar(b, wc, rust);
    const V3 end = hoistHouse(b, wc * 0.9, weatherSteel(rust * 0.9 + 0.1, 0.9), rust, false);
    cables(b, rust, end);
    oreBin(b, false, wc * 0.85, rust);
}

// 4b. STEEL LATTICE: a rusty braced steel tower, concrete footings and collar.
void headframeSteel(B& b, const P& p) {
    const double rust = p("rust");
    const V3 st = weatherSteel(rust), st2 = weatherSteel(rust, 0.85);
    const double Ht = 11.2;
    // Tower: centre x = -1.1, base half-extents (2.5 x 2.0), top (1.45 x 0.95).
    auto corner = [&](int sx, int sz, double y) {
        const double t = y / Ht;
        const double hx = 2.5 * (1 - t) + 1.45 * t, hz = 2.0 * (1 - t) + 0.95 * t;
        return V3(-1.1 + sx * hx, y, sz * hz);
    };
    // Concrete footings and the collar.
    b.style(V3(0.52, 0.5, 0.47), CONCRETE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const V3 g = corner(sx, sz, 0);
            b.box(V3(g.x, 0.25, g.z), V3(1.0, 0.5, 1.0), 0.04);
            b.style(V3(0.2, 0.2, 0.2), METAL);
            b.box(V3(g.x, 0.55, g.z), V3(0.5, 0.1, 0.5), 0.01);
            b.style(V3(0.52, 0.5, 0.47), CONCRETE);
        }
    b.style(V3(0.02, 0.02, 0.02), MATTE);
    b.box(V3(0, 0.012, 0), V3(1.7, 0.024, 1.7));
    b.style(V3(0.52, 0.5, 0.47), CONCRETE);
    b.box(V3(0, 0.3, 1.1), V3(2.6, 0.6, 0.6), 0.04);
    b.box(V3(0, 0.3, -1.1), V3(2.6, 0.6, 0.6), 0.04);
    b.box(V3(1.1, 0.3, 0), V3(0.6, 0.6, 1.6), 0.04);
    b.box(V3(-1.1, 0.3, 0), V3(0.6, 0.6, 1.6), 0.04);
    // Legs and the lattice of each face.
    b.style(st, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.beam(corner(sx, sz, 0.5), corner(sx, sz, Ht), 0.26, 0.26, V3(0, 1, 0), 0.01);
    const int bays = 5;
    for (int i = 0; i <= bays; i++) {
        const double y = 0.5 + (Ht - 0.5) * i / bays;
        b.style(st2, METAL);
        for (int sx = -1; sx <= 1; sx += 2) b.beam(corner(sx, -1, y), corner(sx, 1, y), 0.12, 0.12);
        for (int sz = -1; sz <= 1; sz += 2) b.beam(corner(-1, sz, y), corner(1, sz, y), 0.12, 0.12);
    }
    for (int i = 0; i < bays; i++) {
        const double y0 = 0.5 + (Ht - 0.5) * i / bays, y1 = 0.5 + (Ht - 0.5) * (i + 1) / bays;
        b.style(st2 * 0.92, METAL);
        for (int sx = -1; sx <= 1; sx += 2) {
            b.beam(corner(sx, -1, y0), corner(sx, 1, y1), 0.09, 0.09);
            b.beam(corner(sx, 1, y0), corner(sx, -1, y1), 0.09, 0.09);
        }
        for (int sz = -1; sz <= 1; sz += 2) {
            b.beam(corner(-1, sz, y0), corner(1, sz, y1), 0.09, 0.09);
            b.beam(corner(1, sz, y0), corner(-1, sz, y1), 0.09, 0.09);
        }
    }
    // Head: deck with a railing, the pillow-block beams, gusset plates.
    b.style(st * 0.9, METAL);
    b.box(V3(-1.1, Ht + 0.1, 0), V3(3.4, 0.14, 2.3), 0.02);
    b.box(V3(kWheelX, Ht + 0.35, 0.78), V3(0.4, 0.4, 0.12), 0.01);
    b.box(V3(kWheelX, Ht + 0.35, -0.78), V3(0.4, 0.4, 0.12), 0.01);
    b.style(st2, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.beam(V3(-1.1 + sx * 1.6, Ht + 0.15, sz * 1.1), V3(-1.1 + sx * 1.6, Ht + 1.15, sz * 1.1), 0.05, 0.05);
    for (int k = 0; k < 2; k++) {
        const double y = Ht + 0.65 + k * 0.45 - 0.2;
        for (int sx = -1; sx <= 1; sx += 2) b.beam(V3(-1.1 + sx * 1.6, y, -1.1), V3(-1.1 + sx * 1.6, y, 1.1), 0.04, 0.04);
        for (int sz = -1; sz <= 1; sz += 2) b.beam(V3(-2.7, y, sz * 1.1), V3(0.5, y, sz * 1.1), 0.04, 0.04);
    }
    // The backstay: two heavy inclined struts down to a concrete anchor behind.
    b.style(st, METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.beam(corner(-1, sz, Ht - 0.4), V3(-8.2, 0.4, sz * 1.5), 0.2, 0.2, V3(0, 1, 0));
        b.beam(corner(-1, sz, Ht - 2.8), V3(-6.4, 2.4, sz * 1.5), 0.12, 0.12, V3(0, 1, 0));
    }
    b.style(V3(0.52, 0.5, 0.47), CONCRETE);
    b.box(V3(-8.4, 0.25, 0), V3(1.2, 0.5, 3.8), 0.04);
    ladder(b, corner(1, 1, 0.5) + V3(0.2, 0, -0.2), corner(1, 1, Ht - 0.3) + V3(0.2, 0, -0.2), 0.5, 0.45, st2, METAL);
    sheave(b, kWheelX, kWheelY, rust);
    const V3 end = hoistHouse(b, weatherSteel(rust * 0.5, 0.95), weatherSteel(rust * 0.9 + 0.1, 0.85), rust, true);
    cables(b, rust, end);
    oreBin(b, true, weatherSteel(rust, 0.9), rust);
}

Reg headframeReg({"mine_headframe", "MINE HEADFRAME", "BUILDINGS", "The structure over a mine shaft: shaft axis at x=0,z=0, frame and hoist house behind (-X), ore bin ahead (+X) (instance param rust 0..1)", 13.0,
                  {
                      {"TIMBER A-FRAME", "Weathered timber legs and ties, wooden hoist house: the old lost mine",
                       {num("rust", "WEATHERING", defOf("OUT_RUST", 0.6), 0, 1, 0.05)}, headframeTimber},
                      {"STEEL LATTICE", "Braced steel tower on concrete footings, sheet-steel hoist house",
                       {num("rust", "RUST", defOf("OUT_RUST", 0.6), 0, 1, 0.05)}, headframeSteel},
                  }});

}  // namespace
