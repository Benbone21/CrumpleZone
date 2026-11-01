// Parts: driveline, suspension and the career supply crates.
// (Career mode upgrade parts: station shop, supply caches, rewards.)
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain_kit.h"

using namespace mk;

using namespace dk;

namespace {

// ---- differential ----------------------------------------------------------------------------

std::vector<V2> ovalPoly(double a, double bb, int n) {
    std::vector<V2> p;
    for (int i = 0; i < n; i++) p.push_back(V2(a * std::cos(2 * PI * i / n), bb * std::sin(2 * PI * i / n)));
    return p;
}
// A ring of bolt heads round the Z axis on the plane z = c.z.
void boltRingZ(B& b, const V3& c, double R, int n, double s = 0.016, double t = 0.008, double a0 = 0) {
    for (int i = 0; i < n; i++) {
        const double a = a0 + 2 * PI * i / n;
        nut(b, c + V3(R * std::cos(a), R * std::sin(a), 0), 2, s, t);
    }
}

// Axle centre section: cast pumpkin, cover and its bolt ring facing front, pinion yoke at the back,
// axle tubes out each side. The locker's air line runs out along the tube.
void diffAxle(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const V3 steel(0.24, 0.24, 0.255);
    const double Y0 = 0.17;
    if (!bare(p)) {
        b.style(V3(0.45, 0.35, 0.22), WOOD);
        for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.0625, s * 0.27), V3(0.14, 0.125, 0.1), 0.006);
    }
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
    b.style(steel, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.cone(V3(0, 0, s * 0.12), V3(0, 0, s * 0.2), 0.082, 0.05, 12, false);
        if (bare(p)) {  // (the car's own tubes go on from here)
            b.cyl(V3(0, 0, s * 0.2), V3(0, 0, s * 0.27), 0.05, 12, true);
            continue;
        }
        b.cyl(V3(0, 0, s * 0.2), V3(0, 0, s * 0.35), 0.05, 12, true);
        // Bearing flange at the end, axle stub beyond.
        b.cyl(V3(0, 0, s * 0.35), V3(0, 0, s * 0.364), 0.09, 12);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        boltRingZ(b, V3(0, 0, s * 0.366), 0.072, 6, 0.018, 0.006, 0.3);
        b.style(V3(0.5, 0.5, 0.53), METAL);
        b.cyl(V3(0, 0, s * 0.364), V3(0, 0, s * 0.43), 0.024, 8);
        b.style(col * 0.9, PAINT);
        b.box(V3(0.0, 0.062, s * 0.265), V3(0.11, 0.02, 0.1), 0.004);   // spring perch
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int i = 0; i < 4; i++) nut(b, V3((i & 1 ? 0.04 : -0.04), 0.075, s * 0.265 + (i & 2 ? 0.035 : -0.035)), 1, 0.018, 0.008);
        b.style(steel, METAL);
    }
    // The housing: a flat-faced pumpkin with a domed back.
    b.style(col, PAINT);
    b.push();
    b.yaw(90);
    b.extrude(ovalPoly(0.155, 0.13, 20), 0.1, 0.015);
    b.pop();
    b.sphere(V3(-0.035, 0, 0), V3(0.115, 0.128, 0.152), 2);
    b.style(col * 0.82, PAINT);
    b.push();
    b.yaw(90);
    b.extrude(ovalPoly(0.15, 0.125, 20), 0.02, 0.075);   // the ring-gear cover
    b.pop();
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int i = 0; i < 10; i++) {
        const double a = 2 * PI * (i + 0.5) / 10;
        nut(b, V3(0.0875, 0.113 * std::sin(a), 0.141 * std::cos(a)), 0, 0.02, 0.008);
    }
    nut(b, V3(0.0875, -0.07, 0.0), 0, 0.024, 0.01);   // fill plug
    b.style(col * 0.9, PAINT);
    b.box(V3(0.0885, 0.0, 0.0), V3(0.008, 0.02, 0.27));   // cover ribs
    b.box(V3(0.0885, 0.0, 0.0), V3(0.008, 0.22, 0.02));
    // Pinion nose and yoke.
    b.style(col, PAINT);
    xlathe(b, V3(-0.27, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.03), V2(0.062, 0.05), V2(0.08, 0.1), V2(0.09, 0.125), V2(0, 0.125)}, 14);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, -0.31, -0.27, 0, 0, 0.026, 8);
    yoke(b, -0.31, 0, 0, -1, 0.033);
    nut(b, V3(-0.405, 0, 0), 0, 0.02, 0.01);
    // Locker: its cast boss on the side, the air solenoid, a hose running out along the tube.
    if (p.i("locker")) {
        b.style(V3(0.78, 0.6, 0.15), METAL);
        zcyl(b, -0.035, 0.0, 0.145, 0.185, 0.04, 10);
        b.style(V3(0.1, 0.28, 0.6), PAINT);
        b.box(V3(-0.035, 0.03, 0.19), V3(0.05, 0.07, 0.03), 0.006);
        b.style(V3(0.05, 0.05, 0.055), MATTE);
        b.tube({V3(-0.035, 0.05, 0.19), V3(-0.06, 0.09, 0.2), V3(-0.07, 0.06, 0.26), V3(-0.065, 0.055, bare(p) ? 0.27 : 0.34)}, 0.007, 5);
    }
    // Breather.
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.tube({V3(-0.05, 0.12, 0.0), V3(-0.05, 0.2, 0.0), V3(-0.1, 0.235, 0.02)}, 0.006, 5);
    b.style(V3(0.7, 0.55, 0.12), METAL);
    b.cyl(V3(-0.1, 0.235, 0.02), V3(-0.1, 0.25, 0.02), 0.011, 6);
    b.pop();
}

// A bare third member: the housing cut open at the front to show the ring gear on its carrier and
// the locker drum, the pinion coming in from the back.
void diffBare(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const double Y0 = 0.18;
    if (!bare(p)) gbRails(b, -0.3, 0.1, 0.03, 0.09);
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
    // Cast bowl, open to the front.
    b.style(col, PAINT);
    xlathe(b, V3(-0.19, 0, 0), {V2(0.05, 0), V2(0.07, 0.03), V2(0.12, 0.1), V2(0.15, 0.17), V2(0.15, 0.245), V2(0.122, 0.245), V2(0.122, 0.17), V2(0.1, 0.1), V2(0.04, 0.03), V2(0.05, 0)}, 18);
    // Pinion nose, bearing cap and yoke.
    xlathe(b, V3(-0.33, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.04), V2(0.065, 0.06), V2(0.075, 0.14), V2(0, 0.14)}, 14);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, -0.37, -0.33, 0, 0, 0.026, 8);
    yoke(b, -0.37, 0, 0, -1, 0.033);
    // Pinion shaft and head inside, ring gear (teeth) on the carrier, the locker drum.
    b.style(V3(0.4, 0.4, 0.43), METAL);
    xcyl(b, -0.19, -0.02, -0.02, 0, 0.027, 8);
    b.cone(V3(-0.02, -0.02, 0), V3(0.03, -0.02, 0), 0.027, 0.05, 8);
    std::vector<V2> gear;
    const int teeth = 20;
    for (int i = 0; i < teeth; i++) {
        const double a0 = 2 * PI * i / teeth, da = 2 * PI / teeth;
        gear.push_back(V2(0.098 * std::cos(a0), 0.098 * std::sin(a0)));
        gear.push_back(V2(0.118 * std::cos(a0 + da * 0.2), 0.118 * std::sin(a0 + da * 0.2)));
        gear.push_back(V2(0.118 * std::cos(a0 + da * 0.55), 0.118 * std::sin(a0 + da * 0.55)));
        gear.push_back(V2(0.098 * std::cos(a0 + da * 0.75), 0.098 * std::sin(a0 + da * 0.75)));
    }
    b.style(V3(0.5, 0.5, 0.53), METAL);
    b.push();
    b.move(0.02, 0, 0);
    b.extrude(gear, 0.03, -0.045);
    b.pop();
    b.style(V3(0.78, 0.6, 0.15), METAL);
    zcyl(b, 0.02, 0.0, -0.095, 0.07, 0.068, 14);   // locker drum
    b.style(V3(0.45, 0.45, 0.48), METAL);
    zcyl(b, 0.02, 0.0, -0.14, 0.14, 0.04, 10);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingZ(b, V3(0.02, 0, -0.016), 0.083, 6, 0.014, 0.008, 0.2);   // ring gear bolts
    // Bearing caps on the housing rim.
    b.style(col * 0.9, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0.05, 0.0, s * 0.13), V3(0.07, 0.06, 0.03), 0.006);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        nut(b, V3(0.05, 0.02, s * 0.147), 2, 0.016, 0.006);
        nut(b, V3(0.05, -0.02, s * 0.147), 2, 0.016, 0.006);
        b.style(col * 0.9, PAINT);
    }
    b.pop();
}

Reg differential({"differential", "DIFFERENTIAL", "PARTS", "Career upgrade: axle centre section with its locker", 0.8,
                  {
                      {"AXLE WITH COVER", "Cast pumpkin, bolted cover, axle tubes and an air locker", {paintParam(9), choice("locker", "LOCKER", 1, {"NONE", "AIR LOCKER"})}, diffAxle},
                      {"BARE THIRD MEMBER", "Cutaway centre section: ring gear and locker drum showing", {paintParam(9)}, diffBare},
                  }});

// ---- coilover --------------------------------------------------------------------------------

// A coil spring along X (centre line at (yc, zc), coil radius R, wire radius w).
void helix(B& b, double x0, double x1, double yc, double zc, double R, double w, double turns, int per = 7, int sides = 5) {
    const int n = (int)(turns * per) + 1;
    std::vector<V3> path;
    for (int i = 0; i < n; i++) {
        const double t = (double)i / (n - 1), a = 2 * PI * turns * t;
        path.push_back(V3(x0 + (x1 - x0) * t, yc + R * std::cos(a), zc + R * std::sin(a)));
    }
    b.tube(path, w, sides, false, true);
}
// A ring eye with its axis along Z, lying in the X-Y plane at (x, y, z).
void eyeZ(B& b, double x, double y, double z, double R, double r) {
    b.push();
    b.move(x, y, z);
    b.roll(90);
    b.torus(V3(), R, r, 10, 5);
    b.pop();
}

V3 anodize(int i) {
    static const V3 c[] = {V3(0.74, 0.58, 0.16), V3(0.10, 0.25, 0.62), V3(0.09, 0.09, 0.10), V3(0.66, 0.67, 0.70)};
    return c[((i % 4) + 4) % 4];
}
const std::vector<std::string>& anodizeNames() {
    static const std::vector<std::string> n = {"GOLD", "BLUE", "BLACK", "SILVER"};
    return n;
}

void coilRemote(B& b, const P& p) {
    const V3 an = anodize(p.i("body")), sp = paint(p.i("spring"));
    const double R = 0.0635, w = 0.0075, Y = 0.0785;
    b.push();
    b.move(0, Y, 0);
    // Spring, seats and the lock ring.
    b.style(sp, PAINT);
    helix(b, -0.17, 0.2, 0, 0, R, w, 8, 10, 5);
    b.style(V3(0.5, 0.5, 0.53), METAL);
    xcyl(b, -0.186, -0.172, 0, 0, 0.078, 12);
    xcyl(b, 0.2, 0.214, 0, 0, 0.078, 12);
    b.style(an * 0.9, METAL);
    xcyl(b, -0.172, -0.15, 0, 0, 0.044, 6);   // lock ring
    // Body with its threaded section, the shaft and the eyes.
    b.style(an, METAL);
    xlathe(b, V3(-0.3, 0, 0), {V2(0, 0), V2(0.03, 0), V2(0.034, 0.012), V2(0.034, 0.1), V2(0.037, 0.103), V2(0.037, 0.25), V2(0.034, 0.253), V2(0.034, 0.31), V2(0.03, 0.322), V2(0.012, 0.325)}, 12);
    b.style(an * 0.7, METAL);
    for (int i = 0; i < 5; i++) xcyl(b, -0.12 + 0.025 * i, -0.115 + 0.025 * i, 0, 0, 0.0385, 10, false);   // thread rings
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.01, 0.3, 0, 0, 0.0095, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.21, 0.25, 0, 0, 0.03, 10);
    xcyl(b, -0.345, -0.3, 0, 0, 0.022, 8);
    eyeZ(b, -0.362, 0, 0, 0.018, 0.009);
    xcyl(b, 0.3, 0.335, 0, 0, 0.015, 8);
    eyeZ(b, 0.352, 0, 0, 0.018, 0.009);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.362, 0, -0.016, 0.016, 0.013, 8);
    zcyl(b, 0.352, 0, -0.016, 0.016, 0.013, 8);
    b.pop();
    // Remote reservoir lying on the ground, clamped to a plate, with a braided hose.
    const double rz = 0.18, ry = 0.03;
    b.style(an, METAL);
    xlathe(b, V3(-0.07, ry, rz), {V2(0, 0), V2(0.026, 0), V2(0.028, 0.005), V2(0.028, 0.175), V2(0.026, 0.18), V2(0, 0.18)}, 10);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.11, 0.135, ry, rz, 0.008, 6);
    b.style(V3(0.8, 0.1, 0.08), METAL);
    b.cyl(V3(0.0, ry + 0.027, rz), V3(0.0, ry + 0.045, rz), 0.013, 8);   // adjuster knob
    b.style(V3(0.12, 0.12, 0.13), PAINT);
    b.box(V3(0.02, 0.005, rz), V3(0.17, 0.01, 0.05));
    xcyl(b, -0.005, 0.005, ry, rz, 0.031, 8);
    xcyl(b, 0.045, 0.055, ry, rz, 0.031, 8);
    b.style(V3(0.62, 0.64, 0.68), METAL);
    b.tube({V3(-0.12, Y, 0.034), V3(-0.16, Y - 0.01, 0.1), V3(-0.15, 0.04, 0.16), V3(-0.08, ry, rz)}, 0.0065, 5);
    b.style(an, METAL);
    zcyl(b, -0.12, Y, 0.03, 0.05, 0.011, 6);
}

void coilPlain(B& b, const P& p) {
    const V3 sp = paint(p.i("spring"));
    const double R = 0.0635, w = 0.0075;
    // The loose coil spring with a rubber isolator at one end.
    b.style(sp, PAINT);
    helix(b, -0.18, 0.2, 0.0755, 0.1, R, w, 8, 10, 5);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    xcyl(b, 0.2, 0.212, 0.0755, 0.1, 0.0745, 12);
    xcyl(b, -0.192, -0.18, 0.0755, 0.1, 0.0745, 12);
    // The shock: dust boot, body, shaft, bushed eyes.
    const double Y = 0.046, Z = -0.1;
    b.style(V3(0.1, 0.2, 0.48), PAINT);
    xlathe(b, V3(-0.26, Y, Z), {V2(0, 0), V2(0.027, 0), V2(0.027, 0.2), V2(0, 0.2)}, 12);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    std::vector<V2> boot = {V2(0, 0.0), V2(0.02, 0.0)};
    for (int i = 0; i < 6; i++) {
        boot.push_back(V2(0.034, 0.012 + 0.02 * i));
        boot.push_back(V2(0.021, 0.022 + 0.02 * i));
    }
    boot.push_back(V2(0.021, 0.14));
    boot.push_back(V2(0, 0.14));
    xlathe(b, V3(0.0, Y, Z), boot, 10);
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.14, 0.24, Y, Z, 0.0095, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.14, 0.2, Y, Z, 0.015, 8);
    xcyl(b, -0.3, -0.26, Y, Z, 0.02, 8);
    b.box(V3(-0.31, Y, Z), V3(0.04, 0.034, 0.03));
    for (int e = 0; e < 2; e++) {
        const double x = e ? 0.285 : -0.335;
        b.style(V3(0.1, 0.1, 0.11), METAL);
        zcyl(b, x, Y, Z - 0.025, Z + 0.025, 0.026, 10);
        b.style(V3(0.04, 0.04, 0.045), MATTE);
        zcyl(b, x, Y, Z - 0.03, Z + 0.03, 0.019, 10);
        b.style(V3(0.7, 0.7, 0.73), METAL);
        zcyl(b, x, Y, Z - 0.034, Z + 0.034, 0.009, 6);
    }
    b.style(V3(0.1, 0.1, 0.11), METAL);
    b.box(V3(0.265, Y, Z), V3(0.04, 0.03, 0.03));
    // Spring seat cup on the shock.
    b.style(V3(0.2, 0.2, 0.21), METAL);
    xcyl(b, 0.0, 0.01, Y, Z, 0.045, 10);
}

void coilBypass(B& b, const P& p) {
    const V3 an = anodize(p.i("body")), sp = paint(p.i("spring")), tube = anodize(p.i("tubes"));
    const double R = 0.08, w = 0.009, Y = 0.0925;
    b.push();
    b.move(0, Y, 0);
    b.style(sp, PAINT);
    helix(b, -0.04, 0.2, 0, 0, R, w, 5.5, 10, 5);
    b.style(V3(0.5, 0.5, 0.53), METAL);
    xcyl(b, -0.054, -0.04, 0, 0, 0.092, 12);
    xcyl(b, 0.2, 0.214, 0, 0, 0.092, 12);
    // Big body with its bypass tubes.
    b.style(an, METAL);
    xlathe(b, V3(-0.3, 0, 0), {V2(0, 0), V2(0.046, 0), V2(0.05, 0.015), V2(0.05, 0.33), V2(0.046, 0.345), V2(0.02, 0.35)}, 14);
    for (int k = 0; k < 3; k++) {
        const double a = PI / 2 + k * 2 * PI / 3;
        const double ty = 0.064 * std::sin(a), tz = 0.064 * std::cos(a);
        b.style(tube, METAL);
        xcyl(b, -0.22, 0.03, ty, tz, 0.0125, 8);
        b.style(V3(0.12, 0.12, 0.13), METAL);
        b.box(V3(-0.22, ty * 0.9, tz * 0.9), V3(0.04, 0.03, 0.03));
        b.box(V3(0.03, ty * 0.9, tz * 0.9), V3(0.04, 0.03, 0.03));
        static const V3 knob[] = {V3(0.8, 0.1, 0.08), V3(0.1, 0.3, 0.7), V3(0.7, 0.7, 0.72)};
        b.style(knob[k], METAL);
        b.cyl(V3(-0.02, ty + std::sin(a) * 0.01, tz + std::cos(a) * 0.01), V3(-0.02, ty + std::sin(a) * 0.035, tz + std::cos(a) * 0.035), 0.012, 8);
    }
    b.style(V3(0.12, 0.12, 0.13), METAL);
    xcyl(b, -0.2, -0.19, 0, 0, 0.06, 10);   // clamp rings round the tubes
    xcyl(b, 0.0, 0.01, 0, 0, 0.06, 10);
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.04, 0.3, 0, 0, 0.0127, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, -0.35, -0.3, 0, 0, 0.026, 8);
    xcyl(b, 0.3, 0.335, 0, 0, 0.018, 8);
    eyeZ(b, -0.375, 0, 0, 0.022, 0.011);
    eyeZ(b, 0.358, 0, 0, 0.022, 0.011);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.375, 0, -0.02, 0.02, 0.016, 8);
    zcyl(b, 0.358, 0, -0.02, 0.02, 0.016, 8);
    b.pop();
}

Reg coilover({"coilover", "COILOVER", "PARTS", "Career upgrade: coilover shock and spring", 0.7,
              {
                  {"REMOTE RESERVOIR", "Anodised coilover, threaded body, hosed remote reservoir",
                   {choice("body", "BODY", 0, anodizeNames()), paintParam(0, "spring", "SPRING")}, coilRemote},
                  {"SHOCK AND SPRING", "Plain bushed shock with a separate coil spring",
                   {paintParam(8, "spring", "SPRING")}, coilPlain},
                  {"BYPASS TRIPLE", "Fat bypass shock with three external tubes and a coil",
                   {choice("body", "BODY", 2, anodizeNames()), choice("tubes", "TUBES", 1, anodizeNames()), paintParam(8, "spring", "SPRING")}, coilBypass},
              }});

// ---- leaf_pack -------------------------------------------------------------------------------
// +X is the front (the eye on the frame hanger), the rear eye hangs from a shackle; the main leaf is
// on top with the shorter ones stacked under it. The origin is the axle tube's axis: the pack sits
// on the tube (spring over axle), clamped by U-bolts round it. `camber` is how much higher the
// middle of the stack is than its ends: positive is the free arch (the lab's), negative is the
// way a truck sits when the hangers are above the axle (carparts.cpp fits it to the car's nodes).

constexpr double kTubeR = 0.04;                  // the axle tube it clamps to (softbody.h kAxleTubeR)
constexpr double kLeafLift = kTubeR + 0.012;     // the stack's underside at its middle: tube, then a saddle
constexpr double kShackleLen = 0.075;            // rear eye to the frame pin

struct LeafPack {
    int n = 7;                 // leaves
    double h1 = 0.65;          // main leaf half length
    double base = 0.045;       // top surface at the main leaf's ends (set by fit())
    double camber = 0.1, dc = 0.002;  // arch height; extra arch per leaf down the stack
    double tC = 0.009, tE = 0.009;    // leaf thickness centre / end
    double wz = 0.065;         // leaf width
    double pitch() const { return tC + dc; }
    // The underside of the stack at the middle sits on the tube whatever the arch.
    void fit() { base = kLeafLift - camber + n * tC; }
    double halfLen(int k) const { return n <= 1 ? h1 : h1 * (1.0 - 0.75 * k / (double)std::max(1, n - 1) * (n > 3 ? 1.0 : 0.82)); }
    double camberK(int k) const { return camber + k * dc; }
    double topCentre(int k) const { return base + camber + k * (dc - pitch()); }
    double top(int k, double x) const { const double u = x / h1; return topCentre(k) - camberK(k) * u * u; }
    double thick(int k, double x) const {
        const double h = halfLen(k), q = std::min(1.0, std::fabs(x) / h);
        return tE + (tC - tE) * (1 - q * q);
    }
};

void leafStack(B& b, const LeafPack& L, const V3& col, Mat mat, const V3& edge) {
    for (int k = 0; k < L.n; k++) {
        const double h = L.halfLen(k);
        const int np = k == 0 ? 11 : 7;
        std::vector<V2> top, bot;
        for (int i = 0; i < np; i++) {
            const double x = -h + 2 * h * i / (np - 1);
            top.push_back(V2(x, L.top(k, x)));
            bot.push_back(V2(x, L.top(k, x) - L.thick(k, x)));
        }
        std::vector<V2> poly = top;
        for (int i = np - 1; i >= 0; i--) poly.push_back(bot[i]);
        b.style(mix(col, edge, k & 1 ? 0.0 : 0.35), mat);
        b.extrude(poly, L.wz, 0);
    }
}

// An eye rolled at the end of the main leaf: a hollow cylinder with a rubber bushing.
void leafEye(B& b, double x, double y, double wz, const V3& col, Mat mat) {
    b.style(col, mat);
    zlathe(b, V3(x, y, -wz / 2), {V2(0.015, 0), V2(0.027, 0), V2(0.027, wz), V2(0.015, wz), V2(0.015, 0)}, 12);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    zcyl(b, x, y, -wz / 2 - 0.008, wz / 2 + 0.008, 0.016, 8, false);
    b.style(V3(0.6, 0.6, 0.63), METAL);
    zcyl(b, x, y, -wz / 2 - 0.012, wz / 2 + 0.012, 0.0085, 6, true);
}

// The rear eye hangs from the frame by a shackle: two plates up to a pin through the hanger.
void leafShackle(B& b, double x, double ye, double wz, const V3& metal) {
    b.style(metal, METAL);
    for (int s = -1; s <= 1; s += 2) b.box(V3(x, ye + kShackleLen / 2, s * (wz / 2 + 0.016)), V3(0.026, kShackleLen + 0.022, 0.008), 0.004);
    zcyl(b, x, ye + kShackleLen, -wz / 2 - 0.024, wz / 2 + 0.024, 0.008, 6);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int s = -1; s <= 1; s += 2) nut(b, V3(x, ye + kShackleLen, s * (wz / 2 + 0.028)), 2, 0.022, 0.008);
}

// The front eye bolts into a hanger bracket: cheeks either side, up to the frame.
void leafHanger(B& b, double x, double ye, double wz, const V3& metal) {
    b.style(metal, METAL);
    for (int s = -1; s <= 1; s += 2) b.box(V3(x, ye + 0.03, s * (wz / 2 + 0.016)), V3(0.05, 0.082, 0.008), 0.004);
    b.box(V3(x, ye + 0.07, 0), V3(0.05, 0.012, wz + 0.05), 0.003);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int s = -1; s <= 1; s += 2) nut(b, V3(x, ye, s * (wz / 2 + 0.028)), 2, 0.024, 0.008);
}

void leafHardware(B& b, const LeafPack& L, const V3& metal) {
    const double wz = L.wz;
    const int last = L.n - 1;
    const double yt0 = L.top(0, 0), yb0 = L.top(last, 0) - L.thick(last, 0);
    // Saddle on the tube, pad on top of the stack, a plate under the tube, two U-bolts round the
    // tube and the stack, and the centre bolt.
    b.style(metal, METAL);
    b.box(V3(0, yb0 - 0.006, 0), V3(0.2, 0.012, 0.09), 0.003);
    b.box(V3(0, yt0 + 0.008, 0), V3(0.2, 0.016, 0.09), 0.003);
    const double yPlate = -kTubeR - 0.008;  // (its middle)
    b.box(V3(0, yPlate, 0), V3(0.2, 0.016, 0.1), 0.003);
    for (int s = -1; s <= 1; s += 2) {
        const double x = s * 0.075;
        const double yTop = yt0 + 0.016 + 0.022, yBot = yPlate - 0.008 - 0.025;
        const double z = 0.0475;
        b.style(V3(0.32, 0.32, 0.34), METAL);
        b.tube({V3(x, yBot, z), V3(x, yTop - 0.012, z), V3(x, yTop, z - 0.012), V3(x, yTop, -z + 0.012), V3(x, yTop - 0.012, -z), V3(x, yBot, -z)}, 0.0075, 6);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int t = -1; t <= 1; t += 2) nut(b, V3(x, yPlate - 0.008 - 0.007, t * z), 1, 0.022, 0.012);
    }
    b.style(V3(0.1, 0.1, 0.11), METAL);
    nut(b, V3(0, yt0 + 0.022, 0), 1, 0.03, 0.014);
    // Rebound clips.
    static const double cx[] = {0.24, 0.38};
    for (double x0 : cx)
        for (int s = -1; s <= 1; s += 2) {
            const double x = s * x0;
            int cnt = 0;
            for (int k = 0; k < L.n; k++)
                if (L.halfLen(k) > std::fabs(x) + 0.02) cnt = k + 1;
            if (cnt < 2) continue;
            const double yt = L.top(0, x), yb = L.top(cnt - 1, x) - L.thick(cnt - 1, x);
            b.style(metal * 0.9, METAL);
            b.box(V3(x, yt + 0.003, 0), V3(0.032, 0.006, wz + 0.016));
            for (int t = -1; t <= 1; t += 2) b.box(V3(x, (yt + yb) / 2 - 0.003, t * (wz / 2 + 0.005)), V3(0.032, yt - yb + 0.01, 0.006));
            b.box(V3(x, yb - 0.003, 0), V3(0.032, 0.006, wz + 0.016));
        }
}

// On its own (the lab) the pack rests on its U-bolts, with a stub of the axle tube through them.
void leafStand(B& b, const P& p) {
    if (bare(p)) return;
    b.style(V3(0.15, 0.15, 0.16), METAL);
    zcyl(b, 0, 0, -0.22, 0.22, kTubeR, 12);
    b.move(0, kTubeR + 0.008 + 0.008 + 0.025, 0);
}

LeafPack leafOpts(const P& p) {
    LeafPack L;
    L.n = std::max(2, p.i("leaves"));
    L.camber = p("camber");
    return L;
}

void leafClassic(B& b, const P& p) {
    LeafPack L = leafOpts(p);
    L.dc = 0.0025;
    L.fit();
    const int fin = p.i("finish");
    V3 col(0.11, 0.11, 0.12), edge(0.36, 0.36, 0.38);
    Mat mat = PAINT;
    if (fin == 1) col = V3(0.26, 0.17, 0.11), edge = V3(0.36, 0.22, 0.12), mat = MATTE;   // rusty
    if (fin == 2) col = V3(0.38, 0.38, 0.4), edge = V3(0.55, 0.55, 0.58), mat = METAL;     // bare steel
    leafStack(b, L, col, mat, edge);
    const double ye = L.base - 0.015;
    leafEye(b, L.h1 + 0.012, ye, L.wz, col, mat);
    leafEye(b, -L.h1 - 0.012, ye, L.wz, col, mat);
    leafHanger(b, L.h1 + 0.012, ye, L.wz, V3(0.2, 0.2, 0.21));
    leafShackle(b, -L.h1 - 0.012, ye, L.wz, V3(0.2, 0.2, 0.21));
    leafHardware(b, L, V3(0.2, 0.2, 0.21));
    if (fin == 1) {
        b.style(V3(0.4, 0.18, 0.06), MATTE);
        for (int i = 0; i < 14; i++) {
            const double x = (hash01(7, i) - 0.5) * 1.1;
            b.box(V3(x, L.top(0, x) + 0.002, (hash01(8, i) - 0.5) * 0.04), V3(0.02 + 0.03 * hash01(9, i), 0.004, 0.02));
        }
    }
    leafStand(b, p);
}

void leafParabolic(B& b, const P& p) {
    LeafPack L;
    L.n = std::max(2, p.i("leaves"));
    L.camber = p("camber");
    L.dc = 0.014;
    L.tC = 0.016;
    L.tE = 0.0045;
    L.wz = 0.075;
    L.h1 = 0.64;
    L.fit();
    const V3 col = paint(p.i("paint")) * 0.6, edge = paint(p.i("paint"));
    leafStack(b, L, col, PAINT, edge);
    // Spacer pads between the leaves out toward the ends.
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    for (int k = 1; k < L.n; k++)
        for (int s = -1; s <= 1; s += 2) {
            const double x = s * (L.halfLen(k) - 0.06);
            const double yTop = L.top(k - 1, x) - L.thick(k - 1, x), yBot = L.top(k, x);
            b.box(V3(x, (yTop + yBot) / 2, 0), V3(0.07, std::max(0.004, yTop - yBot), L.wz * 0.9));
        }
    const double ye = L.base - 0.01;
    leafEye(b, L.h1 + 0.012, ye, L.wz, col, PAINT);
    leafEye(b, -L.h1 - 0.012, ye, L.wz, col, PAINT);
    leafHanger(b, L.h1 + 0.012, ye, L.wz, V3(0.35, 0.35, 0.37));
    leafShackle(b, -L.h1 - 0.012, ye, L.wz, V3(0.35, 0.35, 0.37));
    leafHardware(b, L, V3(0.36, 0.36, 0.38));
    leafStand(b, p);
}

Reg leafPack({"leaf_pack", "LEAF PACK", "PARTS", "Career upgrade: leaf spring pack", 1.3,
              {
                  {"CLASSIC MULTI-LEAF", "Stacked steel leaves, rolled eyes, U-bolts, rebound clips, hanger and shackle",
                   {num("leaves", "LEAVES", 7, 3, 10, 1), num("camber", "ARCH", 0.1, -0.2, 0.14, 0.01), choice("finish", "FINISH", 0, {"BLACK", "RUSTY", "BARE STEEL"})}, leafClassic},
                  {"PARABOLIC TAPER", "Three tapered leaves on rubber spacers, with hanger and shackle",
                   {num("leaves", "LEAVES", 3, 2, 5, 1), num("camber", "ARCH", 0.1, -0.2, 0.14, 0.01), paintParam(9)}, leafParabolic},
              }});

// ---- supply_crate ------------------------------------------------------------------------------
// The career supply cache. 1.1 m (X) x 0.76 m (Z) x 0.66 m. Instance param "open": 0 closed,
// 1 opened and looted (lid off, leaning against it, empty inside).

Param openParam() { return choice("open", "OPEN", 0, {"CLOSED", "OPEN"}); }

const std::vector<std::string>& labelNames() {
    static const std::vector<std::string> n = {"CZ-07", "CZ-12", "CZ-31"};
    return n;
}
Param labelParam() { return choice("label", "LABEL", 0, labelNames()); }
const char* labelText(const P& p) { return labelNames()[(size_t)std::clamp(p.i("label"), 0, 2)].c_str(); }

// Stencil letters on the +Z long face (read from the left side) and on the -Z face (from the right).
void sideStencil(B& b, const std::string& text, double y, double zFace, double px, const V3& col) {
    b.style(col, MATTE);
    b.push();
    b.move(0, y, zFace);
    b.yaw(-90);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
    b.push();
    b.move(0, y, -zFace);
    b.yaw(90);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
}

// The same on the two short ends (+X and -X).
void endStencil(B& b, const std::string& text, double y, double xFace, double px, const V3& col) {
    b.style(col, MATTE);
    b.push();
    b.move(xFace, y, 0);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
    b.push();
    b.move(-xFace, y, 0);
    b.yaw(180);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
}

// A plank with its own slight tone.
void plank(B& b, const V3& c, const V3& size, const V3& base, uint32_t seed, double bevel = 0) {
    const double t = 0.86 + 0.26 * hash01(seed, 5);
    b.color(base * t).mat(WOOD);
    b.box(c, size, bevel);
}

// Where a lid of depth `thick` (its underside the local y = 0 plane) leans against the crate's +Z side.
void leanLid(B& b, double topY, double halfZ, double halfLen, double lift = 0) {
    const double phi = 57.0 * PI / 180;
    const double ym = halfLen * std::sin(phi) + 0.002 + lift;
    const double s = (ym - topY) / std::sin(phi);
    const double zm = halfZ - s * std::cos(phi);
    b.move(0, ym, zm);
    b.roll(57);
}

// -- A: wooden military crate ---------------------------------------------------------------

void woodLid(B& b, const P& p, const V3& lidCol, const V3& metal) {
    // Origin: centre of the underside; spans x +-0.55, z +-0.38, y 0..0.035.
    for (int i = 0; i < 5; i++) plank(b, V3(0, 0.0175, -0.304 + 0.152 * i), V3(1.1, 0.035, 0.148), lidCol, 100 + i);
    b.style(V3(0.42, 0.32, 0.19), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.45, -0.015, 0), V3(0.07, 0.03, 0.66));
    b.style(metal, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(s * 0.31, 0.0365, 0), V3(0.05, 0.003, 0.762));
        for (int t = -1; t <= 1; t += 2) b.box(V3(s * 0.31, 0.0175, t * 0.3815), V3(0.05, 0.035, 0.005));
    }
    b.style(V3(0.1, 0.09, 0.07), MATTE);
    b.push();
    b.move(0, 0.0345, 0.17);
    b.yaw(-90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.024, 0.005);
    b.pop();
    b.push();   // and again, the other way up, for anyone walking up from the far side
    b.move(0, 0.0345, -0.17);
    b.yaw(90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.024, 0.005);
    b.pop();
    (void)p;
}

void crateWood(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    const V3 base = paint(p.i("paint")), lidCol = paint(8) * 0.95, metal(0.2, 0.2, 0.21), raw(0.45, 0.34, 0.2);
    const V3 rope = p.i("rope") == 0 ? V3(0.88, 0.42, 0.07) : p.i("rope") == 1 ? V3(0.55, 0.45, 0.28) : V3(0.07, 0.07, 0.075);
    // Runners and floor.
    b.style(raw * 0.9, WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.025, s * 0.25), V3(1.1, 0.05, 0.09));
    b.box(V3(0, 0.06, 0), V3(1.06, 0.02, 0.72));
    // Planked walls: four courses on the long sides and the ends.
    for (int k = 0; k < 4; k++) {
        const double y = 0.07 + 0.0645 + k * 0.1335;
        for (int s = -1; s <= 1; s += 2) {
            plank(b, V3(0, y, s * 0.3475), V3(1.06, 0.129, 0.025), base, 10 + k * 2 + (s > 0));
            plank(b, V3(s * 0.5175, y, 0), V3(0.025, 0.129, 0.67), base, 30 + k * 2 + (s > 0));
        }
    }
    // Corner posts, end battens, metal corner plates.
    b.style(raw, WOOD);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.5075, 0.335, s * 0.3375), V3(0.045, 0.55, 0.045));
    b.style(metal, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2)
            for (int lv = 0; lv < 2; lv++) {
                const double y = lv ? 0.09 : 0.575;
                b.box(V3(sx * 0.53, y, s * 0.31), V3(0.006, 0.1, 0.1));
                b.box(V3(sx * 0.475, y, s * 0.363), V3(0.1, 0.1, 0.006));
            }
    // Stripe and stencil on the long sides, in front of the planks.
    b.style(paint(8) * 0.9, MATTE);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.34, s * 0.3625), V3(1.0, 0.14, 0.004));
    sideStencil(b, labelText(p), 0.34, 0.3645, 0.026, V3(0.07, 0.065, 0.055));
    b.style(paint(8) * 0.9, MATTE);
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.5325, 0.2, 0), V3(0.004, 0.1, 0.62));
    endStencil(b, labelText(p), 0.2, 0.5345, 0.018, V3(0.07, 0.065, 0.055));
    // Rope handles through holes in the ends.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(V3(0.05, 0.04, 0.03), MATTE);
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.5315, 0.4, s * 0.115), V3(0.004, 0.045, 0.05));
        b.style(rope, MATTE);
        b.tube({V3(sx * 0.53, 0.405, -0.115), V3(sx * 0.56, 0.415, -0.12), V3(sx * 0.58, 0.385, -0.06), V3(sx * 0.58, 0.375, 0.0), V3(sx * 0.58, 0.385, 0.06), V3(sx * 0.56, 0.415, 0.12), V3(sx * 0.53, 0.405, 0.115)},
               0.013, 6, false, true);
    }
    // The lid: on, or leaning against the +Z side with the crate empty.
    if (!open) {
        b.push();
        b.move(0, 0.60, 0);
        woodLid(b, p, lidCol, metal);
        b.pop();
    } else {
        b.push();
        leanLid(b, 0.60, 0.36, 0.38);
        woodLid(b, p, lidCol, metal);
        b.pop();
        // Straw and a crushed wrapper left inside.
        b.style(V3(0.7, 0.6, 0.28), MATTE);
        for (int i = 0; i < 5; i++) {
            const double x = (hash01(3, i) - 0.5) * 0.8, z = (hash01(4, i) - 0.5) * 0.5, a = hash01(5, i) * PI;
            b.beam(V3(x - 0.1 * std::cos(a), 0.0745, z - 0.1 * std::sin(a)), V3(x + 0.1 * std::cos(a), 0.0745, z + 0.1 * std::sin(a)), 0.012, 0.006, V3(0, 1, 0));
        }
        b.style(V3(0.62, 0.6, 0.52), MATTE);
        b.box(V3(0.15, 0.085, -0.1), V3(0.22, 0.02, 0.16));
    }
}

// -- B: steel expedition box ----------------------------------------------------------------

// The lid, built about the hinge axis (z = 0 at the hinge, the lid reaching to +Z), its underside y = 0.
void steelLid(B& b, const P& p, const V3& col, const V3& trim) {
    b.style(col, PAINT);
    b.box(V3(0, 0.12, 0.38), V3(1.1, 0.24, 0.76), 0.035);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(0, -0.004, 0.38), V3(0.98, 0.012, 0.66));   // foam liner
    b.style(col * 0.85, PAINT);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.244, 0.38 + s * 0.22), V3(0.92, 0.01, 0.05));   // pressed ribs
    b.style(trim, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.46, 0.247, 0.38 + s * 0.3), V3(0.1, 0.014, 0.08));   // stacking pads
    // Latch levers on the +Z side, hinge leaves on the back.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(paint(4) * 0.95, PAINT);
        b.box(V3(sx * 0.43, 0.0, 0.7665), V3(0.04, 0.13, 0.01), 0.003);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        xcyl(b, sx * 0.43 - 0.024, sx * 0.43 + 0.024, 0.055, 0.772, 0.009, 6);
        b.style(trim, METAL);
    }
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 0.3, 0.07, -0.004), V3(0.1, 0.1, 0.006));
    // Amber reflectors on the front corners.
    b.style(V3(0.95, 0.55, 0.1), EMISSIVE);
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 0.5, 0.12, 0.7625), V3(0.05, 0.05, 0.004));
    // Stencil on the front of the lid and the top.
    b.style(paint(8) * 0.95, MATTE);
    b.push();
    b.move(0, 0.12, 0.761);
    b.yaw(-90);
    b.letters(labelText(p), V3(0, 0, 0), 0.022, 0.004);
    b.pop();
    b.push();
    b.move(0, 0.2495, 0.38);
    b.yaw(-90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.021, 0.004);
    b.pop();
    b.push();
    b.move(0, 0.12, -0.001);
    b.yaw(90);
    b.letters(labelText(p), V3(0, 0, 0), 0.022, 0.003);
    b.pop();
}

void crateSteel(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    const V3 col = paint(p.i("paint")), trim = col * 0.55 + V3(0.04, 0.04, 0.04);
    // Feet, floor, four walls and corner posts of the tray (y 0.03 .. 0.40).
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.44, 0.015, s * 0.3), V3(0.14, 0.03, 0.12));
    b.style(col, PAINT);
    b.box(V3(0, 0.045, 0), V3(1.08, 0.03, 0.74), 0.012);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 0.215, s * 0.355), V3(1.08, 0.37, 0.03), 0.012);
        b.box(V3(s * 0.525, 0.215, 0), V3(0.03, 0.37, 0.68), 0.012);
    }
    b.style(trim, PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.515, 0.215, s * 0.345), V3(0.06, 0.37, 0.06), 0.01);
    // Pressed ribs on the long sides and ends.
    b.style(col * 0.88, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 4; i++) b.box(V3(-0.36 + 0.24 * i, 0.21, s * 0.3735), V3(0.04, 0.26, 0.007));
        for (int e = -1; e <= 1; e += 2) b.box(V3(s * 0.5435, 0.21, e * 0.19), V3(0.007, 0.26, 0.04));
    }
    endStencil(b, labelText(p), 0.13, 0.5455, 0.013, paint(8) * 0.95);
    // Gasket, tray latch catches, hinge leaves and barrels.
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 0.395, s * 0.3725), V3(1.09, 0.012, 0.025));
        b.box(V3(s * 0.5275, 0.395, 0), V3(0.025, 0.012, 0.72));
    }
    b.style(trim, METAL);
    for (int sx = -1; sx <= 1; sx += 2) {
        b.box(V3(sx * 0.43, 0.33, 0.378), V3(0.07, 0.08, 0.014));
        b.box(V3(sx * 0.3, 0.34, -0.375), V3(0.1, 0.1, 0.006));
        xcyl(b, sx * 0.3 - 0.05, sx * 0.3 + 0.05, 0.4, -0.38, 0.014, 8);
    }
    // Carry handles on the ends.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.548, 0.27, s * 0.1), V3(0.012, 0.05, 0.04));
        b.style(paint(4) * 0.9, MATTE);
        b.tube({V3(sx * 0.55, 0.27, -0.1), V3(sx * 0.585, 0.27, -0.1), V3(sx * 0.585, 0.27, 0.1), V3(sx * 0.55, 0.27, 0.1)}, 0.011, 6);
    }
    // The lid: hinged shut, or off and leaning against the front (+Z) side.
    if (!open) {
        b.push();
        b.move(0, 0.40, -0.38);
        steelLid(b, p, col, trim);
        b.pop();
    } else {
        b.push();
        leanLid(b, 0.40, 0.37, 0.38, 0.04);
        b.move(0, 0, -0.38);
        steelLid(b, p, col, trim);
        b.pop();
        b.style(V3(0.62, 0.6, 0.52), MATTE);
        b.box(V3(-0.2, 0.067, 0.1), V3(0.3, 0.012, 0.2), 0.004);   // a crushed wrapper
    }
}

// -- C: pallet with a strapped tarp-covered load -----------------------------------------------

void cratePallet(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    static const V3 tarps[] = {V3(0.14, 0.32, 0.62), V3(0.85, 0.42, 0.08), V3(0.3, 0.34, 0.2), V3(0.58, 0.6, 0.62)};
    const V3 tarp = tarps[std::clamp(p.i("tarp"), 0, 3)], strapCol(0.95, 0.74, 0.08);
    const double top = 0.122;
    // The pallet: bottom boards, three stringers, top boards.
    b.style(V3(0.55, 0.42, 0.26), WOOD);
    for (int i = -1; i <= 1; i++) b.box(V3(i * 0.5, 0.01, 0), V3(0.1, 0.02, 0.76));
    for (int i = -1; i <= 1; i++) b.box(V3(0, 0.06, i * 0.3), V3(1.1, 0.08, 0.08));
    for (int i = 0; i < 7; i++) plank(b, V3(-0.49 + i * 0.1633, 0.111, 0), V3(0.12, 0.022, 0.76), V3(0.55, 0.42, 0.26), 60 + i);
    if (!open) {
        // The load under its tarp: a flared skirt, the body, lumps from what is underneath.
        b.style(tarp * 0.9, MATTE);
        b.box(V3(0, top + 0.07, 0), V3(1.04, 0.14, 0.72), 0.05);
        b.style(tarp, MATTE);
        b.box(V3(0, top + 0.27, 0), V3(0.98, 0.45, 0.66), 0.09);
        b.blob(V3(-0.15, top + 0.5, 0.02), V3(0.3, 0.1, 0.24), 0.25, 5, 2, 0.5);
        b.blob(V3(0.2, top + 0.49, -0.05), V3(0.22, 0.09, 0.2), 0.25, 9, 2, 0.5);
        b.style(tarp * 0.72, MATTE);
        b.beam(V3(-0.44, top + 0.495, 0.1), V3(0.4, top + 0.505, 0.18), 0.02, 0.012, V3(0, 1, 0), 0.004);   // fold lines
        b.beam(V3(-0.4, top + 0.5, -0.2), V3(0.42, top + 0.5, -0.14), 0.02, 0.012, V3(0, 1, 0), 0.004);
        // Ratchet straps over the top, round the pallet.
        b.style(strapCol, MATTE);
        for (int sx = -1; sx <= 1; sx += 2) {
            const double x = sx * 0.4;
            const double pts[][2] = {{0.36, top + 0.0}, {0.37, top + 0.1}, {0.355, top + 0.3}, {0.32, top + 0.5}, {0.22, top + 0.545}};
            for (int s = -1; s <= 1; s += 2)
                for (int i = 0; i + 1 < 5; i++) {
                    const V3 a(x, pts[i][1], s * pts[i][0]), c(x, pts[i + 1][1], s * pts[i + 1][0]);
                    const V3 d = c - a;
                    b.beam(a, c, 0.045, 0.008, V3(0, -d.z, d.y) * -s);
                }
            b.beam(V3(x, top + 0.545, 0.22), V3(x, top + 0.545, -0.22), 0.045, 0.008, V3(0, 1, 0));
            b.style(V3(0.62, 0.64, 0.68), METAL);
            b.box(V3(x, top + 0.33, 0.362), V3(0.05, 0.075, 0.025), 0.005);   // ratchet
            b.style(strapCol, MATTE);
        }
        sideStencil(b, labelText(p), top + 0.27, 0.331, 0.021, V3(0.1, 0.09, 0.07));
        endStencil(b, labelText(p), top + 0.27, 0.491, 0.016, V3(0.1, 0.09, 0.07));
    } else {
        // Tarp dragged off into a heap, straps cut, a couple of broken planks left on the pallet.
        b.style(tarp, MATTE);
        b.blob(V3(0.0, 0.115, 0.62), V3(0.46, 0.09, 0.24), 0.55, 11, 2, 0.5, 0.0);
        b.blob(V3(0.28, 0.095, 0.8), V3(0.26, 0.06, 0.14), 0.5, 4, 2, 0.5, 0.0);
        b.push();
        b.move(0.1, top + 0.0, 0.38);
        b.roll(-14);
        b.box(V3(0, 0.006, -0.12), V3(0.75, 0.01, 0.26), 0.003);   // a corner still draped over the pallet
        b.pop();
        b.style(strapCol, MATTE);
        b.beam(V3(-0.4, top + 0.004, 0.2), V3(-0.4, top + 0.004, -0.34), 0.045, 0.006, V3(0, 1, 0));
        b.beam(V3(0.4, 0.004, 0.5), V3(0.5, 0.004, 0.72), 0.045, 0.006, V3(0, 1, 0));
        b.style(V3(0.62, 0.64, 0.68), METAL);
        b.box(V3(0.5, 0.008, 0.52), V3(0.05, 0.016, 0.04));
        b.style(V3(0.42, 0.32, 0.19), WOOD);
        b.beam(V3(-0.3, top + 0.011, -0.2), V3(0.1, top + 0.011, 0.1), 0.1, 0.02, V3(0, 1, 0));
        b.beam(V3(0.35, top + 0.011, -0.25), V3(0.0, top + 0.03, -0.1), 0.08, 0.02, V3(0, 1, 0));
    }
}

Reg supplyCrate({"supply_crate", "SUPPLY CRATE", "PARTS", "THE career supply cache found in the world: open it for parts (instance param open: 0 closed, 1 looted)", 1.2,
                 {
                     {"WOODEN MILITARY", "Banded plank crate, rope handles, yellow lid and stencil",
                      {paintParam(1), labelParam(), choice("rope", "ROPE", 0, {"ORANGE", "MANILA", "BLACK"}), openParam()}, crateWood},
                     {"STEEL EXPEDITION", "Pressed-steel box with latches, hinges and end handles",
                      {paintParam(1), labelParam(), openParam()}, crateSteel},
                     {"TARP PALLET", "Strapped tarp-covered load on a wooden pallet",
                      {choice("tarp", "TARP", 1, {"BLUE", "ORANGE", "OLIVE", "GREY"}), labelParam(), openParam()}, cratePallet},
                 }});

//@@END
}  // namespace
