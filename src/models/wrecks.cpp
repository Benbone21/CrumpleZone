// Wrecked vehicles left out in the country (wrecks.h places them): a sedan, a van, an old jeep or SUV, a box truck and a flatbed,
// rusted and stripped, in the style of the wrecked pickup (buildings.cpp). All sit upright with their wheels (or hubs) at y = 0,
// front +x, left +z: the pose they lie in (rolled, nose down, half buried) is the placement's, not the model's.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

constexpr double PI = M_PI;

// What a hulk is made of: its paint, how far gone it is, and a counter that gives each panel its own wear.
struct Wear {
    V3 paint{0.5, 0.2, 0.15};
    double rust = 0.6;
    uint32_t seed = 3;
    int n = 0;
};

V3 rustOf(uint32_t seed, uint32_t i) { return mix(V3(0.28, 0.14, 0.08), V3(0.50, 0.26, 0.12), hash01(seed, i)); }

// The next panel's colour: the paint, faded, some of it gone to rust.
void panel(B& b, Wear& w, double extra = 0) {
    const int i = w.n++;
    const double h = hash01(w.seed, i);
    const double t = std::clamp(w.rust * 0.95 * h * h + extra, 0.0, 1.0);
    const V3 base = mix(w.paint, V3(0.5, 0.48, 0.44), 0.18 + 0.25 * w.rust);
    b.style(mix(base * (0.85 + 0.25 * hash01(w.seed + 1, i)), rustOf(w.seed + 2, i), t), t > 0.38 ? METAL : PAINT);
}
void bare(B& b, Wear& w) { b.style(rustOf(w.seed + 3, w.n++) * 0.8, METAL); }

// Rust and holes on a side (normal +-z): patches over x0..x1, y0..y1, standing a hair off the plane z = zc.
void patches(B& b, const Wear& w, int n, double x0, double x1, double y0, double y1, double zc, uint32_t sd) {
    for (int k = 0; k < n; k++) {
        const double x = x0 + (x1 - x0) * hash01(sd, k * 3), y = y0 + (y1 - y0) * hash01(sd, k * 3 + 1);
        const double sx = 0.06 + 0.22 * hash01(sd, k * 3 + 2) * (0.5 + w.rust), sy = 0.04 + 0.14 * hash01(sd + 1, k);
        const bool hole = hash01(sd + 2, k) > 0.72;
        b.style(hole ? V3(0.05, 0.045, 0.04) : rustOf(w.seed, 40 + k + sd) * (0.75 + 0.4 * w.rust), METAL);
        b.box(V3(x, y, zc), V3(sx, sy, 0.012));
    }
}
// ...and on a top (normal up), at height y.
void topPatches(B& b, const Wear& w, int n, double x0, double x1, double z0, double z1, double y, uint32_t sd) {
    for (int k = 0; k < n; k++) {
        const double x = x0 + (x1 - x0) * hash01(sd, k * 3), z = z0 + (z1 - z0) * hash01(sd, k * 3 + 1);
        b.style(rustOf(w.seed, 90 + k + sd) * (0.7 + 0.4 * w.rust), METAL);
        b.box(V3(x, y, z), V3(0.1 + 0.3 * hash01(sd, k * 3 + 2), 0.008, 0.1 + 0.3 * hash01(sd + 1, k)));
    }
}

// A wheel on its hub: a tire (or only the rim, on the ground), the axis along z, its centre `c`.
void wheel(B& b, const Wear& w, const V3& c, double R, bool tire, double width = 0.2) {
    b.push();
    b.move(c);
    b.roll(90);
    const double hw = width / 2;
    if (tire) {
        b.style(V3(0.05, 0.05, 0.055), MATTE);
        b.lathe(V3(0, 0, 0), {V2(R * 0.56, -hw), V2(R * 0.92, -hw), V2(R, -hw * 0.55), V2(R, hw * 0.55), V2(R * 0.92, hw), V2(R * 0.56, hw)}, 14, true);
    }
    b.style(rustOf(w.seed, 300) * 0.9, METAL);
    b.lathe(V3(0, 0, 0), {V2(0.0, -hw * 0.5), V2(R * 0.56, -hw * 0.5), V2(R * 0.58, hw * 0.7), V2(R * 0.3, hw * 0.45), V2(0.0, hw * 0.45)}, 12, false);
    b.pop();
}
// A bare hub where a wheel was.
void hub(B& b, Wear& w, const V3& c, double side) {
    bare(b, w);
    b.cyl(c, c + V3(0, 0, side * 0.1), 0.1, 8);
}

// A flat-lying scatter: broken glass.
void glass(B& b, uint32_t seed, const V3& c, double rad, int n) {
    b.style(V3(0.4, 0.55, 0.58), GLASS);
    for (int k = 0; k < n; k++) {
        const double a = 2 * PI * hash01(seed, k), r = rad * std::sqrt(hash01(seed + 1, k));
        const V3 p(c.x + r * std::cos(a), c.y, c.z + r * std::sin(a));
        const double s = 0.04 + 0.07 * hash01(seed + 2, k), ra = 2 * PI * hash01(seed + 3, k);
        const V3 q = p + V3(s * std::sin(ra), 0.003, s * std::cos(ra)), u = p + V3(s * 0.6 * std::sin(ra + 2), 0.005, s * 0.6 * std::cos(ra + 2));
        b.tri(p, q, u);
    }
}

// The sill of a body side, rear to front, with its wheel arches (the top of the side is added to it).
std::vector<V2> sideSkirt(double xr, double xf, double sill, const std::vector<double>& axles, double archR, double hubY) {
    std::vector<V2> p;
    p.push_back(V2(xr, sill));
    for (double a : axles) {
        const double phi = std::asin(std::clamp((sill - hubY) / archR, -1.0, 1.0));
        for (int i = 0; i <= 8; i++) {
            const double t = PI - phi - (PI - 2 * phi) * i / 8;
            p.push_back(V2(a + archR * std::cos(t), hubY + archR * std::sin(t)));
        }
    }
    p.push_back(V2(xf, sill));
    return p;
}

// A passenger-car or van shell, rear to front along x.
struct Shell {
    double xr = -2.3, xf = 2.3, W = 0.86;    // ends, half width
    double sill = 0.36, belt = 0.92;         // the rocker, the bottom of the side windows
    double hoodF = 0.78, hoodR = 0.9;        // the hood's front and rear edges, heights
    double cowl = 0.85;                      // x of the base of the windshield
    double aTop = 0.1, roof = 1.42;          // x of its top; the roof's height
    double cTop = -1.15, cBase = -1.7;       // the rear pillar: its top and base
    double deck = 0.98;                      // the trunk lid's height (a van, a hatch: cTop = cBase = xr, full height behind)
    std::vector<double> axles{-1.4, 1.4};    // rear first
    double R = 0.31, width = 0.19;           // tires
    bool slab = false;                       // a van's side: sheet metal to the roof behind the doors
    // What's left of it.
    double crush = 0, crushRear = 0;         // the roof pressed down over the front seats, the rear
    int hood = 1;                            // 0 gone, 1 shut, 2 sprung open
    int trunk = 1;                           // 0 gone, 1 shut, 2 ajar
    bool door[2] = {true, true};             // left, right front doors still on
    double doorOpen[2] = {0, 0};             // degrees
    bool wheels[4] = {true, true, true, true};  // rear left, rear right, front left, front right
    bool tires = true;                       // the wheels that are on still wear tires
    bool engine = true, seats = true, glassLeft = true, bumpers = true;
    double noseIn = 0;                       // m the nose is pushed back
};

void buildShell(B& b, Wear& w, const Shell& s) {
    const double zc = s.W;
    const double xf = s.xf - s.noseIn;
    // Underneath: the pan, the sills, the exhaust, the axles.
    b.style(rustOf(w.seed, 1) * 0.55, METAL);
    b.box(V3((s.xr + xf) / 2, s.sill + 0.02, 0), V3(xf - s.xr - 0.1, 0.05, 2 * s.W - 0.08));
    for (double a : s.axles) b.cyl(V3(a, s.R, -s.W + 0.1), V3(a, s.R, s.W - 0.1), 0.04, 6);
    b.style(V3(0.13, 0.11, 0.1), METAL);
    b.cyl(V3(s.xr + 0.1, 0.3, 0.45), V3(xf - 1.0, 0.3, 0.3), 0.03, 6);
    // The sides.
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        std::vector<V2> p = sideSkirt(s.xr, xf, s.sill, s.axles, s.R + 0.09, s.R);
        p.push_back(V2(xf, s.hoodF - 0.12));
        p.push_back(V2(xf - 0.08, s.hoodF));
        p.push_back(V2(s.cowl, s.hoodR));
        if (s.slab) {
            const double y = s.roof - (s.crushRear + s.crush) * 0.5 - 0.02;
            p.push_back(V2(s.cowl - 0.05, s.belt));
            p.push_back(V2(s.cBase + 0.02, s.belt));
            p.push_back(V2(s.cBase, y));
            p.push_back(V2(s.xr + 0.03, y));
        } else {
            p.push_back(V2(s.cowl - 0.05, s.belt));
            p.push_back(V2(s.cBase, s.belt));
            p.push_back(V2(s.cBase - 0.2, s.deck));
            p.push_back(V2(s.xr + 0.03, s.deck));
        }
        panel(b, w);
        b.push();
        b.move(0, 0, sz * zc);
        b.extrude(p, 0.03);
        b.pop();
        // Door seams and handles: dark lines on the panel.
        b.style(V3(0.04, 0.04, 0.04), MATTE);
        const double dF = s.cowl - 0.08, dB = s.cowl - 1.12;
        for (double x : {dF, dB}) b.box(V3(x, (s.sill + s.belt) / 2 + 0.03, sz * (zc + 0.016)), V3(0.012, s.belt - s.sill - 0.1, 0.006));
        if (!s.slab) b.box(V3(dB - 0.95, (s.sill + s.belt) / 2, sz * (zc + 0.016)), V3(0.012, s.belt - s.sill - 0.1, 0.006));
        b.box(V3(dB + 0.1, s.belt - 0.1, sz * (zc + 0.018)), V3(0.12, 0.03, 0.01));
        patches(b, w, 8, s.xr + 0.2, xf - 0.2, s.sill + 0.04, s.belt - 0.04, sz * (zc + 0.012), w.seed + 20 + k);
        // A door gone leaves a dark doorway; one hanging swings off its front hinge.
        if (!s.door[k] || s.doorOpen[k] > 0) {
            b.style(V3(0.035, 0.03, 0.028), MATTE);
            b.box(V3((dF + dB) / 2, (s.sill + s.belt) / 2 + 0.03, sz * (zc + 0.02)), V3(dF - dB - 0.04, s.belt - s.sill - 0.12, 0.012));
            if (s.door[k]) {
                b.push();
                b.move(dF, 0, sz * (zc + 0.03));
                b.yaw(sz * s.doorOpen[k]);
                panel(b, w);
                b.box(V3((dB - dF) / 2, (s.sill + s.belt) / 2 + 0.03, 0), V3(dF - dB, s.belt - s.sill - 0.1, 0.05), 0.01);
                b.beam(V3(dB - dF + 0.05, s.belt, 0), V3(dB - dF + 0.1, s.roof - 0.14, 0), 0.04, 0.04, V3(1, 0, 0));
                b.beam(V3(-0.08, s.belt, 0), V3(-0.45, s.roof - 0.1, 0), 0.04, 0.04, V3(1, 0, 0));
                b.beam(V3(dB - dF + 0.1, s.roof - 0.14, 0), V3(-0.45, s.roof - 0.1, 0), 0.04, 0.04, V3(0, 0, 1));
                b.pop();
            }
        }
    }
    // The cabin floor's dark, the seats, the dash, the wheel.
    b.style(V3(0.05, 0.045, 0.04), MATTE);
    b.box(V3((s.cBase + s.cowl) / 2, s.sill + 0.09, 0), V3(s.cowl - s.cBase, 0.06, 2 * s.W - 0.1));
    b.box(V3(s.cowl - 0.04, (s.belt + s.sill) / 2 + 0.1, 0), V3(0.05, s.belt - s.sill, 2 * s.W - 0.06));  // the firewall
    b.style(V3(0.12, 0.11, 0.1), MATTE);
    b.box(V3(s.cowl - 0.25, s.belt - 0.03, 0), V3(0.38, 0.14, 2 * s.W - 0.1), 0.03);
    if (s.seats) {
        b.style(mix(V3(0.34, 0.24, 0.16), V3(0.2, 0.19, 0.18), hash01(w.seed, 7)), MATTE);
        for (int k = -1; k <= 1; k += 2) {
            b.box(V3(s.cowl - 1.05, s.sill + 0.26, k * 0.38), V3(0.48, 0.14, 0.5), 0.04);
            b.beam(V3(s.cowl - 1.28, s.sill + 0.3, k * 0.38), V3(s.cowl - 1.4, s.belt + 0.14, k * 0.38), 0.48, 0.12, V3(1, 0, 0), 0.03);
        }
        if (!s.slab) b.box(V3(s.cowl - 2.0, s.sill + 0.26, 0), V3(0.46, 0.16, 2 * s.W - 0.2), 0.04);
    } else {
        bare(b, w);
        for (int k = -1; k <= 1; k += 2) {
            b.box(V3(s.cowl - 1.05, s.sill + 0.12, k * 0.48), V3(0.44, 0.02, 0.03));
            b.box(V3(s.cowl - 1.05, s.sill + 0.12, k * 0.26), V3(0.44, 0.02, 0.03));
        }
    }
    b.style(V3(0.07, 0.07, 0.07), METAL);
    b.tube({V3(s.cowl - 0.15, s.belt - 0.02, 0.38), V3(s.cowl - 0.42, s.belt + 0.14, 0.38)}, 0.018, 5);
    b.push();
    b.move(s.cowl - 0.45, s.belt + 0.16, 0.38);
    b.pitch(-25);
    b.roll(90);
    b.torus(V3(0, 0, 0), 0.17, 0.014, 12, 5);
    b.pop();
    // The roof: a front and a rear half, pressed in where it took the weight.
    const double yA = s.roof - s.crush * 0.75, yM = s.roof - s.crush, yC = s.roof - s.crushRear, xM = (s.aTop + s.cTop) / 2;
    const double rw = 2 * s.W - 0.16;
    panel(b, w, 0.1);
    b.beam(V3(s.aTop, yA, 0), V3(xM, yM, 0), rw, 0.04, V3(0, 1, 0), 0.01);
    panel(b, w, 0.1);
    b.beam(V3(xM, yM, 0), V3(s.cTop, yC, 0), rw, 0.04, V3(0, 1, 0), 0.01);
    b.style(V3(0.07, 0.065, 0.06), MATTE);
    b.beam(V3(s.aTop, yA - 0.04, 0), V3(s.cTop, yC - 0.04, 0), rw - 0.1, 0.02);
    topPatches(b, w, 7, s.cTop + 0.2, s.aTop - 0.2, -s.W * 0.7, s.W * 0.7, (yM + yC) / 2 + 0.03, w.seed + 30);
    // The pillars, bent over with the roof.
    for (int k = -1; k <= 1; k += 2) {
        const double z = k * (s.W - 0.05);
        panel(b, w);
        b.beam(V3(s.cowl - 0.02, s.hoodR, z), V3(s.aTop, yA, z - k * s.crush * 0.25), 0.06, 0.07, V3(1, 0, 0));
        if (!s.slab) {
            const double xB = s.cowl - 1.15;
            b.tube({V3(xB, s.belt, z), V3(xB - s.crush * 0.12, (s.belt + yM) / 2, z + k * s.crush * 0.1), V3(xB, yM, z - k * s.crush * 0.1)}, 0.04, 5);
            b.beam(V3(s.cBase, s.belt, z), V3(s.cTop, yC, z), 0.12, 0.06, V3(1, 0, 0));
        }
        panel(b, w);
        b.beam(V3(s.aTop, yA, z), V3(s.cTop, yC, z), 0.05, 0.05);  // the rain rail
    }
    if (s.slab) {  // a van's roof meets its sides: the rail runs to the back
        panel(b, w, 0.15);
        b.box(V3(s.xr + 0.04, s.roof - s.crushRear - 0.2, 0), V3(0.06, 0.4, 2 * s.W), 0.01);
    }
    // What glass is left.
    if (s.glassLeft) {
        b.style(V3(0.3, 0.42, 0.46), GLASS);
        b.tri(V3(s.cowl - 0.02, s.hoodR + 0.02, s.W - 0.1), V3(s.cowl - 0.02, s.hoodR + 0.02, 0.1), V3(s.aTop + 0.1, yA - 0.06, s.W - 0.1));
        b.tri(V3(s.cowl - 0.04, s.hoodR + 0.02, -s.W + 0.1), V3(s.aTop + 0.15, yA - 0.05, -s.W + 0.1), V3(s.aTop + 0.5, (s.hoodR + yA) / 2, -0.2));
        b.tri(V3(s.cBase + 0.1, s.belt + 0.02, -s.W), V3(s.cBase + 0.65, s.belt + 0.02, -s.W), V3(s.cBase + 0.12, yC - 0.1, -s.W));
    }
    // The hood (or the engine bay).
    b.style(V3(0.05, 0.045, 0.04), MATTE);
    b.box(V3((s.cowl + xf) / 2, s.sill + 0.04, 0), V3(xf - s.cowl - 0.1, 0.06, 2 * s.W - 0.1));
    if (s.hood != 1 && s.engine) {
        b.style(V3(0.16, 0.17, 0.18), METAL);
        b.box(V3((s.cowl + xf) / 2, s.sill + 0.26, 0), V3(0.72, 0.42, 0.56), 0.04);
        b.style(rustOf(w.seed, 11), METAL);
        for (int k = -1; k <= 1; k += 2) b.box(V3((s.cowl + xf) / 2, s.sill + 0.48, k * 0.18), V3(0.5, 0.07, 0.14), 0.01);
        b.style(rustOf(w.seed, 12) * 0.8, METAL);
        b.box(V3(xf - 0.2, s.sill + 0.22, 0), V3(0.07, 0.42, 2 * s.W - 0.4), 0.01);
    }
    if (s.hood) {
        const double hx = s.cowl, hy = s.hoodR;
        b.push();
        if (s.hood == 2) {
            b.move(hx, hy, 0);
            b.pitch(-38);
            b.move(-hx, -hy, 0);
        }
        panel(b, w);
        b.beam(V3(xf - 0.08, s.hoodF + (s.noseIn > 0 ? 0.1 : 0), 0), V3((xf + hx) / 2, (s.hoodF + hy) / 2 + 0.03 + s.noseIn * 0.35, 0), 2 * s.W - 0.04, 0.035, V3(0, 1, 0), 0.01);
        panel(b, w);
        b.beam(V3((xf + hx) / 2, (s.hoodF + hy) / 2 + 0.03 + s.noseIn * 0.35, 0), V3(hx, hy, 0), 2 * s.W - 0.04, 0.035, V3(0, 1, 0), 0.01);
        b.pop();
        if (s.hood == 1) topPatches(b, w, 5, hx + 0.1, xf - 0.1, -s.W * 0.7, s.W * 0.7, (s.hoodF + hy) / 2 + 0.05, w.seed + 31);
    }
    // The nose: lamp sockets, a grille, a bumper (hanging off one end if it's still there).
    panel(b, w, 0.2);
    b.box(V3(xf, (s.sill + s.hoodF) / 2 + 0.02, 0), V3(0.05, s.hoodF - s.sill - 0.1, 2 * s.W), 0.01);
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    b.box(V3(xf + 0.02, s.hoodF - 0.17, 0), V3(0.03, 0.16, s.W), 0.005);
    for (int k = -1; k <= 1; k += 2) {
        b.style(V3(0.03, 0.03, 0.03), MATTE);
        b.box(V3(xf + 0.02, s.hoodF - 0.17, k * (s.W - 0.2)), V3(0.03, 0.15, 0.28), 0.01);
        b.style(V3(0.5, 0.48, 0.42), CHROME);
        b.box(V3(xf + 0.03, s.hoodF - 0.17, k * (s.W - 0.2)), V3(0.012, 0.16, 0.3));
    }
    if (s.bumpers) {
        b.style(V3(0.42, 0.41, 0.39), METAL);
        const double dip = s.noseIn > 0 ? 0.2 : 0.06;
        b.beam(V3(xf + 0.1, s.sill + 0.08, s.W + 0.02), V3(xf + 0.1 + (s.noseIn > 0 ? 0.1 : 0), s.sill + 0.08 - dip, -s.W - 0.02), 0.09, 0.14, V3(0, 1, 0), 0.015);
    }
    // The tail.
    panel(b, w, 0.1);
    const double tailTop = s.slab ? s.roof - s.crushRear - 0.4 : s.deck;
    b.box(V3(s.xr, (s.sill + tailTop) / 2, 0), V3(0.05, tailTop - s.sill, 2 * s.W), 0.01);
    for (int k = -1; k <= 1; k += 2) {
        b.style(V3(0.42, 0.07, 0.06), PAINT);
        b.box(V3(s.xr - 0.02, tailTop - 0.14, k * (s.W - 0.16)), V3(0.03, 0.16, 0.26));
    }
    b.style(V3(0.7, 0.62, 0.3), PAINT);
    b.box(V3(s.xr - 0.03, s.sill + 0.2, 0), V3(0.01, 0.12, 0.3));
    if (!s.slab) {
        if (s.trunk) {
            b.push();
            b.move(s.cBase - 0.2, s.deck, 0);
            if (s.trunk == 2) b.pitch(26);
            panel(b, w);
            b.beam(V3(0, 0, 0), V3(s.xr + 0.03 - (s.cBase - 0.2), 0, 0), 2 * s.W - 0.02, 0.035, V3(0, 1, 0), 0.01);
            b.pop();
        } else {
            b.style(V3(0.05, 0.045, 0.04), MATTE);
            b.box(V3((s.xr + s.cBase) / 2, s.sill + 0.25, 0), V3(s.cBase - s.xr - 0.05, 0.03, 2 * s.W - 0.1));
        }
        if (s.bumpers) {
            b.style(V3(0.42, 0.41, 0.39), METAL);
            b.box(V3(s.xr - 0.1, s.sill + 0.05, 0), V3(0.09, 0.13, 2 * s.W + 0.04), 0.015);
        }
    }
    // The wheels.
    const V3 wc[4] = {V3(s.axles[0], s.R, s.W - 0.14), V3(s.axles[0], s.R, -s.W + 0.14), V3(s.axles[1], s.R, s.W - 0.14), V3(s.axles[1], s.R, -s.W + 0.14)};
    for (int k = 0; k < 4; k++) {
        if (s.wheels[k]) wheel(b, w, wc[k], s.R, s.tires, s.width);
        else hub(b, w, wc[k], wc[k].z > 0 ? 1 : -1);
    }
}

// A truck's ladder frame, front to rear, and the cab that stands on it.
void ladder(B& b, Wear& w, double xr, double xf, double y, double rail) {
    for (int k = -1; k <= 1; k += 2) {
        b.style(rustOf(w.seed, 50 + k) * 0.7, METAL);
        b.box(V3((xr + xf) / 2, y, k * rail), V3(xf - xr, 0.2, 0.08));
    }
    for (double x = xr + 0.15; x < xf; x += 1.1) {
        b.style(rustOf(w.seed, 60 + (int)(x * 3)) * 0.65, METAL);
        b.box(V3(x, y, 0), V3(0.08, 0.14, 2 * rail));
    }
}

// A cab-forward cab: a box with a slanted screen on the frame's front end, xf its nose, the back at xb.
void truckCab(B& b, Wear& w, double xb, double xf, double W, double y0, double roof, bool doorOff, double crush) {
    const double belt = y0 + 0.75;
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        panel(b, w);
        b.push();
        b.move(0, 0, sz * W);
        b.extrude({V2(xb, y0), V2(xf, y0), V2(xf, belt + 0.05), V2(xf - 0.25, belt + 0.1), V2(xb + 0.8, belt + 0.1), V2(xb + 0.8, belt), V2(xb, belt)}, 0.03);
        b.pop();
        patches(b, w, 7, xb + 0.1, xf - 0.1, y0 + 0.1, belt - 0.05, sz * (W + 0.012), w.seed + 70 + k);
        if (k == 0 && doorOff) {
            b.style(V3(0.035, 0.03, 0.03), MATTE);
            b.box(V3(xf - 0.8, y0 + 0.42, W + 0.02), V3(0.85, 0.7, 0.012));
        }
        panel(b, w);
        b.box(V3(xb + 0.4, belt + (roof - belt) / 2, sz * W), V3(0.8, roof - belt, 0.04));          // behind the door glass
        b.beam(V3(xf - 0.12, belt + 0.08, sz * (W - 0.03)), V3(xf - 0.55, roof - crush, sz * (W - 0.03 - crush * 0.2)), 0.08, 0.06, V3(1, 0, 0));
    }
    panel(b, w, 0.1);
    b.beam(V3(xf - 0.5, roof - crush, 0), V3((xb + xf) / 2, roof - crush * 0.7, 0), 2 * W - 0.05, 0.05, V3(0, 1, 0), 0.015);
    panel(b, w, 0.1);
    b.beam(V3((xb + xf) / 2, roof - crush * 0.7, 0), V3(xb, roof, 0), 2 * W - 0.05, 0.05, V3(0, 1, 0), 0.015);
    topPatches(b, w, 6, xb + 0.2, xf - 0.7, -W * 0.7, W * 0.7, roof + 0.02 - crush * 0.5, w.seed + 75);
    panel(b, w);
    b.box(V3(xb, (y0 + roof) / 2, 0), V3(0.05, roof - y0, 2 * W), 0.01);
    b.style(V3(0.04, 0.04, 0.035), MATTE);
    b.box(V3(xf - 0.9, y0 + 0.05, 0), V3(1.6, 0.05, 2 * W - 0.1));
    b.box(V3(xf - 0.2, belt - 0.02, 0), V3(0.3, 0.2, 2 * W - 0.1), 0.02);
    // The nose: a flat panel, a grille, two lamps, a bumper hanging by a bolt.
    panel(b, w, 0.15);
    b.box(V3(xf, y0 + 0.4, 0), V3(0.05, 0.85, 2 * W), 0.01);
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    for (int k = 0; k < 5; k++) b.box(V3(xf + 0.03, y0 + 0.28 + k * 0.09, 0), V3(0.012, 0.05, W));
    for (int k = -1; k <= 1; k += 2) {
        b.style(V3(0.5, 0.48, 0.42), CHROME);
        b.cyl(V3(xf + 0.02, y0 + 0.36, k * (W - 0.22)), V3(xf + 0.05, y0 + 0.36, k * (W - 0.22)), 0.13, 12);
        b.style(V3(0.03, 0.03, 0.03), MATTE);
        b.cyl(V3(xf + 0.05, y0 + 0.36, k * (W - 0.22)), V3(xf + 0.06, y0 + 0.36, k * (W - 0.22)), 0.1, 10);
    }
    b.style(V3(0.4, 0.39, 0.37), METAL);
    b.beam(V3(xf + 0.12, y0 - 0.02, W + 0.04), V3(xf + 0.16, y0 - 0.24, -W - 0.04), 0.09, 0.16, V3(0, 1, 0), 0.015);
    b.style(V3(0.12, 0.12, 0.11), METAL);
    b.tube({V3(xb + 0.6, y0 + 1.5, W + 0.06), V3(xb + 0.6, y0 + 1.3, W + 0.2), V3(xb + 0.56, y0 + 1.05, W + 0.22)}, 0.012, 4);  // the mirror's arm
    b.box(V3(xb + 0.56, y0 + 0.95, W + 0.22), V3(0.05, 0.28, 0.14));
}

V3 faded(int i) {
    static const V3 c[] = {V3(0.46, 0.13, 0.11), V3(0.18, 0.27, 0.46), V3(0.62, 0.56, 0.4), V3(0.26, 0.33, 0.2), V3(0.6, 0.6, 0.58), V3(0.5, 0.33, 0.15), V3(0.12, 0.3, 0.3)};
    return c[((i % 7) + 7) % 7];
}
const std::vector<std::string> kPaints = {"OXBLOOD", "DENIM", "CREAM", "OLIVE", "WHITE", "OCHRE", "TEAL"};
Param paintOf(int def) { return choice("paint", "PAINT", def, kPaints); }
Param rustOfParam(double def) { return num("rust", "RUST", def, 0, 1, 0.05); }

Wear wearOf(const P& p, uint32_t seed) {
    Wear w;
    w.paint = faded(p.i("paint"));
    w.rust = p("rust");
    w.seed = seed + (uint32_t)p.i("seed");
    return w;
}

// ---- The sedan ------------------------------------------------------------------------------------------------------------

void sedanCrushed(B& b, const P& p) {
    Wear w = wearOf(p, 101);
    Shell s;
    s.crush = 0.3, s.crushRear = 0.1, s.hood = 2, s.wheels[1] = false, s.doorOpen[1] = 32;
    buildShell(b, w, s);
    glass(b, w.seed, V3(0.3, 0.004, 1.5), 1.2, 16);
}
void sedanStripped(B& b, const P& p) {
    Wear w = wearOf(p, 111);
    Shell s;
    s.hood = 0, s.trunk = 0, s.door[0] = s.door[1] = false, s.tires = false, s.engine = false, s.seats = false, s.glassLeft = false, s.bumpers = false;
    s.wheels[0] = s.wheels[3] = false;
    buildShell(b, w, s);
}
void sedanNose(B& b, const P& p) {
    Wear w = wearOf(p, 121);
    Shell s;
    s.noseIn = 0.55, s.cowl = 0.72, s.hood = 1, s.trunk = 2, s.crush = 0.1, s.wheels[2] = false, s.doorOpen[0] = 55;
    s.axles = {-1.4, 1.2};
    buildShell(b, w, s);
    glass(b, w.seed, V3(2.2, 0.004, -0.2), 1.0, 18);
}

Reg sedanReg({"wreck_sedan", "WRECKED SEDAN", "BUILDINGS", "An old four-door left to rust: the roof pressed in, or stripped to the shell, or its nose stove in", 5.0,
              {
                  {"CRUSHED ROOF", "Roof pressed in, hood sprung, one wheel gone", {paintOf(0), rustOfParam(0.6), seedParam(1)}, sedanCrushed},
                  {"STRIPPED SHELL", "Doors, hood, trunk lid, engine and glass gone, sitting on two rims", {paintOf(1), rustOfParam(0.8), seedParam(1)}, sedanStripped},
                  {"NOSE STOVE IN", "Hit something hard: front end folded, driver's door hanging", {paintOf(2), rustOfParam(0.55), seedParam(1)}, sedanNose},
              }});

// ---- The van --------------------------------------------------------------------------------------------------------------

Shell vanShell() {
    Shell s;
    s.xr = -2.4, s.xf = 2.3, s.W = 0.96, s.sill = 0.36, s.belt = 1.08, s.hoodF = 0.98, s.hoodR = 1.08;
    s.cowl = 1.45, s.aTop = 0.75, s.roof = 2.02, s.cTop = -2.38, s.cBase = 0.2;
    s.axles = {-1.45, 1.45}, s.R = 0.33, s.width = 0.2, s.slab = true;
    return s;
}
// The back doors, open or gone.
void vanBack(B& b, Wear& w, const Shell& s, double openL, double openR) {
    b.style(V3(0.04, 0.035, 0.03), MATTE);
    b.box(V3(s.xr - 0.02, (s.belt + s.roof) / 2 - 0.1, 0), V3(0.012, s.roof - s.belt - 0.1, 2 * s.W - 0.14));
    for (int k = 0; k < 2; k++) {
        const double ang = k ? openR : openL, sz = k ? -1 : 1;
        if (ang < 0) continue;
        b.push();
        b.move(s.xr - 0.04, 0, sz * (s.W - 0.03));
        b.yaw(-sz * ang);
        panel(b, w);
        b.box(V3(0, (s.belt + s.roof) / 2 - 0.12, -sz * 0.45), V3(0.04, s.roof - s.belt - 0.08, 0.9), 0.01);
        b.pop();
    }
}
void vanAbandoned(B& b, const P& p) {
    Wear w = wearOf(p, 131);
    Shell s = vanShell();
    s.crushRear = 0.06, s.hood = 1, s.door[1] = false, s.wheels[1] = false;
    buildShell(b, w, s);
    vanBack(b, w, s, 105, -1);
    // A dark load bay, and the slider that came off leaning on the side.
    b.style(V3(0.05, 0.045, 0.04), MATTE);
    b.box(V3(-1.1, s.sill + 0.1, 0), V3(2.4, 0.05, 2 * s.W - 0.1));
    for (int k = 0; k < 3; k++) {
        b.style(mix(V3(0.42, 0.34, 0.24), V3(0.3, 0.3, 0.3), hash01(w.seed, k)), MATTE);
        b.box(V3(-1.6 + k * 0.5, s.sill + 0.32, 0.3 - k * 0.3), V3(0.46, 0.38, 0.42), 0.01);
    }
    glass(b, w.seed, V3(1.0, 0.004, -1.7), 1.0, 12);
}
void vanBurned(B& b, const P& p) {
    Wear w = wearOf(p, 141);
    w.rust = 1, w.paint = V3(0.12, 0.11, 0.1);
    Shell s = vanShell();
    s.crush = 0.16, s.crushRear = 0.22, s.hood = 2, s.door[0] = s.door[1] = false, s.tires = false, s.seats = false, s.glassLeft = false, s.bumpers = false;
    buildShell(b, w, s);
    vanBack(b, w, s, -1, 34);
    // Soot licking up from the windows.
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        b.style(V3(0.03, 0.03, 0.03), MATTE);
        b.box(V3(-0.6, 1.5, sz * (s.W + 0.02)), V3(2.4, 0.5, 0.012));
    }
}

Reg vanReg({"wreck_van", "WRECKED VAN", "BUILDINGS", "A panel van, dumped with its back doors open, or burned out on its rims", 5.2,
            {
                {"ABANDONED", "Rear door swung back, a door and a wheel gone, a few boxes still in the back", {paintOf(4), rustOfParam(0.5), seedParam(1)}, vanAbandoned},
                {"BURNED OUT", "Black and rust, the roof sagging, nothing left in it", {paintOf(3), rustOfParam(1.0), seedParam(1)}, vanBurned},
            }});

// ---- The old jeep and the old SUV -----------------------------------------------------------------------------------------

// A short square tub on a ladder frame: its fenders flat, its grille slotted, round lamps.
void jeepBody(B& b, Wear& w, double xr, double xf, double W, bool hood, bool tires, int gone) {
    const double y0 = 0.52, top = 1.02, cowl = 0.45, R = 0.36;
    ladder(b, w, xr + 0.05, xf - 0.05, 0.46, 0.46);
    const V3 wc[4] = {V3(xr + 0.72, R, W - 0.02), V3(xr + 0.72, R, -W + 0.02), V3(xf - 0.62, R, W - 0.02), V3(xf - 0.62, R, -W + 0.02)};
    for (double x : {wc[0].x, wc[2].x}) {
        b.style(V3(0.13, 0.11, 0.1), METAL);
        b.cyl(V3(x, R, -W), V3(x, R, W), 0.05, 6);
    }
    for (int k = 0; k < 4; k++) {
        if (k == gone) hub(b, w, wc[k], wc[k].z > 0 ? 1 : -1);
        else wheel(b, w, wc[k], R, tires, 0.24);
    }
    // The tub's sides, with a low cut for the door; the rear corners rounded by a chamfer.
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        panel(b, w);
        b.push();
        b.move(0, 0, sz * (W - 0.16));
        b.extrude({V2(xr + 0.02, y0), V2(cowl, y0), V2(cowl, top - 0.02), V2(cowl - 0.2, top - 0.02), V2(cowl - 0.32, y0 + 0.2), V2(cowl - 0.9, y0 + 0.2), V2(cowl - 1.02, top), V2(xr + 0.08, top), V2(xr + 0.02, top - 0.06)}, 0.03);
        b.pop();
        patches(b, w, 6, xr + 0.1, cowl - 0.1, y0 + 0.05, y0 + 0.18, sz * (W - 0.145), w.seed + 80 + k);
        // The flat fenders.
        panel(b, w);
        b.box(V3(xf - 0.62, 0.84, sz * (W - 0.03)), V3(1.15, 0.03, 0.34), 0.008);
        b.beam(V3(xf - 1.2, 0.84, sz * (W - 0.03)), V3(cowl - 0.4, 0.58, sz * (W - 0.03)), 0.34, 0.03, V3(0, 1, 0));
        b.box(V3(xr + 0.72, 0.84, sz * (W - 0.03)), V3(0.95, 0.03, 0.34), 0.008);
        b.box(V3(xf - 0.62, 0.72, sz * (W - 0.19)), V3(1.1, 0.26, 0.02));  // the apron
    }
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    b.box(V3((xr + cowl) / 2, y0 + 0.04, 0), V3(cowl - xr, 0.05, 2 * (W - 0.16)));
    panel(b, w);
    b.box(V3(xr + 0.02, (y0 + top) / 2, 0), V3(0.04, top - y0, 2 * (W - 0.16)), 0.01);  // the tail
    panel(b, w, 0.1);
    b.box(V3(cowl, (y0 + top) / 2 + 0.03, 0), V3(0.04, top - y0 + 0.06, 2 * (W - 0.16)), 0.01);  // the firewall
    // The nose: the slotted grille between round lamps, the hood or the engine.
    panel(b, w, 0.15);
    b.box(V3(xf - 0.02, 0.76, 0), V3(0.05, 0.54, 2 * (W - 0.22)), 0.012);
    b.style(V3(0.04, 0.04, 0.04), MATTE);
    for (int k = -3; k <= 3; k++) b.box(V3(xf + 0.005, 0.76, k * 0.09), V3(0.012, 0.32, 0.035));
    for (int k = -1; k <= 1; k += 2) {
        b.style(V3(0.5, 0.48, 0.42), CHROME);
        b.cyl(V3(xf, 0.8, k * 0.44), V3(xf + 0.04, 0.8, k * 0.44), 0.1, 12);
        b.style(V3(0.03, 0.03, 0.03), MATTE);
        b.cyl(V3(xf + 0.04, 0.8, k * 0.44), V3(xf + 0.05, 0.8, k * 0.44), 0.08, 10);
    }
    b.style(V3(0.32, 0.31, 0.3), METAL);
    b.box(V3(xf + 0.12, 0.42, 0), V3(0.1, 0.12, 2 * W - 0.1), 0.012);
    if (hood) {
        panel(b, w);
        b.box(V3((xf + cowl) / 2, 1.03, 0), V3(xf - cowl - 0.06, 0.03, 2 * (W - 0.22)), 0.01);
        topPatches(b, w, 4, cowl + 0.1, xf - 0.15, -0.4, 0.4, 1.05, w.seed + 85);
    } else {
        b.style(V3(0.17, 0.17, 0.18), METAL);
        b.box(V3((xf + cowl) / 2 + 0.05, 0.74, 0), V3(0.7, 0.42, 0.46), 0.04);
        b.style(rustOf(w.seed, 87), METAL);
        b.box(V3((xf + cowl) / 2 + 0.05, 0.98, 0), V3(0.5, 0.08, 0.3), 0.01);
    }
    // Two seat frames.
    bare(b, w);
    for (int k = -1; k <= 1; k += 2) {
        b.box(V3(cowl - 0.65, y0 + 0.24, k * 0.36), V3(0.4, 0.04, 0.42));
        b.beam(V3(cowl - 0.85, y0 + 0.24, k * 0.36), V3(cowl - 0.98, y0 + 0.72, k * 0.36), 0.42, 0.05, V3(1, 0, 0));
    }
}

void jeepOpen(B& b, const P& p) {
    Wear w = wearOf(p, 151);
    const double xr = -1.75, xf = 1.65, W = 0.8;
    jeepBody(b, w, xr, xf, W, true, true, 3);
    // The windscreen folded down on the hood, a roll bar, the spare on the tail.
    panel(b, w);
    b.tube({V3(0.5, 1.06, 0.6), V3(1.25, 1.1, 0.6), V3(1.25, 1.1, -0.6), V3(0.5, 1.06, -0.6)}, 0.022, 5);
    bare(b, w);
    b.tube({V3(-1.0, 1.02, 0.6), V3(-1.0, 1.72, 0.56), V3(-1.0, 1.72, -0.56), V3(-1.0, 1.02, -0.6)}, 0.035, 6);
    b.push();  // (flat against the tail)
    b.move(xr - 0.15, 0.82, 0);
    b.yaw(90);
    wheel(b, w, V3(0, 0, 0), 0.36, true, 0.24);
    b.pop();
}
void jeepHardtop(B& b, const P& p) {
    Wear w = wearOf(p, 161);
    const double xr = -2.05, xf = 1.85, W = 0.86;
    jeepBody(b, w, xr, xf, W, false, false, 0);
    // The steel top: a square box down the back, sagging over the seats, the screen gone.
    const double r = 1.86;
    for (int k = -1; k <= 1; k += 2) {
        const double z = k * (W - 0.17);
        panel(b, w);
        b.beam(V3(0.45, 1.02, z), V3(0.12, r - 0.1, z), 0.06, 0.06, V3(1, 0, 0));
        b.beam(V3(-0.75, 1.02, z), V3(-0.75, r - 0.14, z), 0.07, 0.06, V3(1, 0, 0));
        panel(b, w);
        b.push();
        b.move(0, 0, z);
        b.extrude({V2(-0.85, 1.0), V2(xr + 0.05, 1.0), V2(xr + 0.05, r - 0.02), V2(-0.85, r - 0.1)}, 0.03);
        b.pop();
        b.style(V3(0.05, 0.05, 0.05), MATTE);
        b.box(V3(-1.45, 1.46, z + k * 0.02), V3(0.85, 0.42, 0.012));
    }
    panel(b, w, 0.1);
    b.beam(V3(0.12, r - 0.14, 0), V3(-0.75, r - 0.2, 0), 2 * W - 0.26, 0.04, V3(0, 1, 0), 0.01);
    panel(b, w, 0.1);
    b.beam(V3(-0.75, r - 0.2, 0), V3(xr + 0.05, r - 0.02, 0), 2 * W - 0.26, 0.04, V3(0, 1, 0), 0.01);
    topPatches(b, w, 6, xr + 0.2, 0, -0.5, 0.5, r - 0.09, w.seed + 88);
    // The tailgate down on its chains.
    panel(b, w);
    b.box(V3(xr - 0.28, 0.95, 0), V3(0.56, 0.04, 2 * W - 0.34), 0.01);
    b.style(V3(0.1, 0.1, 0.1), METAL);
    for (int k = -1; k <= 1; k += 2) b.tube({V3(xr - 0.5, 0.97, k * (W - 0.2)), V3(xr + 0.02, 1.4, k * (W - 0.2))}, 0.008, 4);
}

Reg jeepReg({"wreck_jeep", "WRECKED JEEP", "BUILDINGS", "An old flat-fender jeep with its screen folded down, or a square old SUV with a steel top", 4.6,
             {
                 {"FLAT FENDER", "Open tub, roll bar, screen folded on the hood, a wheel gone", {paintOf(3), rustOfParam(0.7), seedParam(1)}, jeepOpen},
                 {"STEEL TOP", "A square old SUV, engine bay open, on its rims, tailgate down", {paintOf(5), rustOfParam(0.75), seedParam(1)}, jeepHardtop},
             }});

// ---- The trucks -----------------------------------------------------------------------------------------------------------

// A ribbed cargo box from xb (its front) to xr, its skin torn open on one side.
void cargoBox(B& b, Wear& w, double xr, double xb, double W, double y0, double top, bool torn) {
    const V3 skin = mix(V3(0.62, 0.62, 0.6), rustOf(w.seed, 200), 0.3 * w.rust);
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    b.box(V3((xr + xb) / 2, y0 + 0.02, 0), V3(xb - xr, 0.05, 2 * W));
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        const bool open = torn && k == 1;
        for (double x = xr + 0.25; x < xb; x += 0.62) {
            b.style(rustOf(w.seed, 210 + (int)(x * 7)) * 0.75, METAL);
            b.box(V3(x, (y0 + top) / 2, sz * W), V3(0.05, top - y0, 0.05));
        }
        for (double x = xr + 0.25; x + 0.62 < xb; x += 0.62) {
            const double rip = open ? hash01(w.seed + 7, (uint32_t)(x * 11 + 40)) : 0;
            if (rip > 0.55) continue;  // (a sheet gone)
            const double y1 = open ? top - (top - y0) * 0.6 * hash01(w.seed + 8, (uint32_t)(x * 13 + 40)) : top;
            b.style(mix(skin, rustOf(w.seed + 3, (uint32_t)(x * 5 + 40)), 0.5 * hash01(w.seed + 4, (uint32_t)(x * 3 + 40 + k))), METAL);
            b.box(V3(x + 0.31, (y0 + y1) / 2, sz * W), V3(0.6, y1 - y0, 0.02));
        }
        b.style(V3(0.32, 0.3, 0.28), METAL);
        b.box(V3((xr + xb) / 2, top, sz * W), V3(xb - xr, 0.08, 0.06));
        b.box(V3((xr + xb) / 2, y0 + 0.04, sz * W), V3(xb - xr, 0.1, 0.06));
    }
    b.style(mix(skin, V3(0.05, 0.05, 0.05), 0.3), METAL);
    b.box(V3(xb, (y0 + top) / 2, 0), V3(0.05, top - y0, 2 * W));
    // The roof: sheets with one peeled back; the rolled door at the back stuck half up.
    for (double x = xr; x + 0.9 < xb; x += 0.9) {
        const bool gone = hash01(w.seed + 9, (uint32_t)(x * 9 + 40)) > 0.72;
        if (gone) continue;
        b.style(mix(skin, rustOf(w.seed, (uint32_t)(x * 9 + 40)), 0.4), METAL);
        b.box(V3(x + 0.45, top + 0.03, 0), V3(0.88, 0.02, 2 * W));
    }
    b.style(V3(0.05, 0.045, 0.04), MATTE);
    b.box(V3(xr + 0.03, (y0 + top) / 2, 0), V3(0.01, top - y0, 2 * W - 0.08));
    b.style(mix(skin, rustOf(w.seed, 230), 0.4), METAL);
    for (int k = 0; k < 6; k++) b.box(V3(xr, top - 0.12 - k * 0.14, 0), V3(0.03, 0.12, 2 * W - 0.1));
    b.style(V3(0.25, 0.25, 0.24), METAL);
    for (int k = -1; k <= 1; k += 2) b.box(V3(xr, (y0 + top) / 2, k * W), V3(0.07, top - y0, 0.07));
}

void truckGear(B& b, Wear& w, double xr, double xf, double cabBack, bool tires) {
    ladder(b, w, xr, xf - 0.15, 0.68, 0.42);
    const double R = 0.48;
    const double fx = xf - 1.0, rx = xr + 1.25;
    for (double x : {fx, rx}) {
        b.style(V3(0.13, 0.11, 0.1), METAL);
        b.cyl(V3(x, R, -0.95), V3(x, R, 0.95), 0.06, 6);
    }
    bool front[2] = {true, tires};
    for (int k = 0; k < 2; k++) {
        const double sz = k ? -1 : 1;
        if (front[k]) wheel(b, w, V3(fx, R, sz * 0.95), R, tires, 0.26);
        else hub(b, w, V3(fx, R, sz * 0.95), sz);
        wheel(b, w, V3(rx, R, sz * 0.93), R, tires, 0.24);  // duals
        wheel(b, w, V3(rx, R, sz * 0.68), R, tires, 0.24);
    }
    bare(b, w);
    b.box(V3(cabBack - 0.55, 0.62, 0.62), V3(0.8, 0.34, 0.34), 0.03);  // the tank
}

void boxTruckTorn(B& b, const P& p) {
    Wear w = wearOf(p, 171);
    const double xr = -3.5, xf = 2.9, cab = 1.0;
    truckGear(b, w, xr, xf, cab, true);
    truckCab(b, w, cab, xf, 1.02, 0.92, 2.5, false, 0.1);
    cargoBox(b, w, xr, cab - 0.14, 1.12, 0.96, 3.0, true);
}
void boxTruckGutted(B& b, const P& p) {
    Wear w = wearOf(p, 181);
    const double xr = -3.5, xf = 2.9, cab = 1.0;
    truckGear(b, w, xr, xf, cab, false);
    truckCab(b, w, cab, xf, 1.02, 0.92, 2.5, true, 0.26);
    cargoBox(b, w, xr, cab - 0.14, 1.12, 0.96, 3.0, false);
}
Reg boxTruckReg({"wreck_boxtruck", "WRECKED BOX TRUCK", "BUILDINGS", "A cab-forward delivery truck, its ribbed box torn open or its cab caved in", 7.0,
                 {
                     {"TORN BOX", "The box's sheets ripped off one side, roof peeling, roller door stuck", {paintOf(1), rustOfParam(0.6), seedParam(1)}, boxTruckTorn},
                     {"CAVED CAB", "Roof of the cab pressed down, door gone, on its rims", {paintOf(6), rustOfParam(0.8), seedParam(1)}, boxTruckGutted},
                 }});

// A stake bed: planks rotted through, a few stakes left, some of the load.
void flatDeck(B& b, Wear& w, double xr, double xb, double W, double y, int load) {
    for (double x = xr; x < xb; x += 0.24) {
        const double r = hash01(w.seed + 11, (uint32_t)(x * 17 + 60));
        if (r > 0.82) continue;  // (a plank gone)
        b.style(mix(V3(0.4, 0.33, 0.24), V3(0.24, 0.19, 0.14), hash01(w.seed + 12, (uint32_t)(x * 19 + 60))), WOOD);
        b.box(V3(x + 0.11, y, 0.03 * (r - 0.4)), V3(0.21, 0.05, 2 * W - (r > 0.6 ? 0.4 * r : 0)));
    }
    for (int k = -1; k <= 1; k += 2) {
        bare(b, w);
        b.box(V3((xr + xb) / 2, y - 0.08, k * W), V3(xb - xr, 0.12, 0.05));
        for (double x = xr + 0.15; x < xb; x += 0.8) {
            if (hash01(w.seed + 13, (uint32_t)(x * 23 + 60 + k)) > 0.55) continue;
            b.style(V3(0.28, 0.23, 0.17), WOOD);
            b.box(V3(x, y + 0.45, k * (W + 0.01)), V3(0.07, 0.95, 0.05));
        }
    }
    if (load == 1) {  // a tipped-over drum and a couple of timbers
        b.style(V3(0.22, 0.25, 0.2), PAINT);
        b.cyl(V3(-1.4, y + 0.3, -0.5), V3(-0.55, y + 0.3, -0.2), 0.28, 12);
        b.style(V3(0.36, 0.28, 0.2), WOOD);
        b.box(V3(-2.0, y + 0.1, 0.5), V3(1.8, 0.1, 0.16));
        b.box(V3(-1.9, y + 0.2, 0.34), V3(1.5, 0.1, 0.16));
    }
}
void flatbedStake(B& b, const P& p) {
    Wear w = wearOf(p, 191);
    const double xr = -3.3, xf = 2.7, cab = 0.9;
    truckGear(b, w, xr, xf, cab, true);
    truckCab(b, w, cab, xf, 1.02, 0.92, 2.5, false, 0.06);
    flatDeck(b, w, xr, cab - 0.1, 1.1, 1.0, 1);
}
void flatbedBare(B& b, const P& p) {
    Wear w = wearOf(p, 201);
    const double xr = -3.3, xf = 2.7, cab = 0.9;
    truckGear(b, w, xr, xf, cab, false);
    truckCab(b, w, cab, xf, 1.02, 0.92, 2.5, true, 0.34);
    flatDeck(b, w, xr, cab - 0.1, 1.1, 1.0, 0);
}
Reg flatbedReg({"wreck_flatbed", "WRECKED FLATBED", "BUILDINGS", "An old stake-bed truck, its planks rotting, part of its last load still on", 6.5,
                {
                    {"STAKE BED", "Rotted planks, a few stakes left, a drum and timbers on the bed", {paintOf(0), rustOfParam(0.7), seedParam(1)}, flatbedStake},
                    {"BARE DECK", "Empty deck, cab roof caved in, on its rims", {paintOf(5), rustOfParam(0.85), seedParam(1)}, flatbedBare},
                }});

}  // namespace
