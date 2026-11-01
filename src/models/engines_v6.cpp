// V6 engines (crate_engine_v6): one 60-degree V6 family, three designs.
//   TWIN-TURBO V6   the 2.7 / 3.0 twin-turbo (Bronco): iron block, a big ribbed plastic cover, a turbo low on each side.
//   TOYOTA 24-VALVE the 3.4 5VZ-FE: painted iron block, long cam covers, a cast "24 VALVE" plenum; a roots blower on top.
//   TOYOTA GR       the 1GR 4.0 / 2GR 3.5: aluminium block, black plastic intake or cover; an intercooled blower, a cold-air kit.
// Bare frame (drivetrain.h): crank on the X axis, +X the accessory drive, the flywheel's back face the rearmost point.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain.h"
#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

// ---- Frame -------------------------------------------------------------------------------------
constexpr double FX = -0.27;   // the block's rear flange face (the flywheel's back face is FX - 0.033: the rearmost point)
constexpr double XF = 0.20;    // the block's front face
constexpr double BANK = 30;    // each bank's lean from upright (60 degrees between them)

// Colours.
const V3 kBlack(0.055, 0.055, 0.06), kRubber(0.035, 0.035, 0.04), kBolt(0.13, 0.13, 0.14);
const V3 kAlu(0.60, 0.61, 0.63), kCastAlu(0.53, 0.54, 0.55), kIron(0.30, 0.30, 0.31), kManifold(0.29, 0.25, 0.22);

// A prism along X: `poly` is its (z, y) cross-section, from x0 to x1.
void xprism(B& b, const std::vector<V2>& poly, double x0, double x1) {
    std::vector<V2> q;
    for (const V2& v : poly) q.push_back(V2(-v.x, v.y));  // (yaw 90: local x is world -z, local z world x)
    b.push();
    b.yaw(90);
    b.extrude(q, x1 - x0, (x0 + x1) / 2);
    b.pop();
}
// A cross-section symmetric about z = 0, given as its +z half from bottom to top (z, y); mirrored to close it.
std::vector<V2> symm(const std::vector<V2>& half) {
    std::vector<V2> out;
    for (const V2& v : half) out.push_back(v);
    for (int i = (int)half.size() - 1; i >= 0; i--)
        if (half[i].x > 1e-9) out.push_back(V2(-half[i].x, half[i].y));
    return out;
}
// Rounded corners on a path (one Chaikin pass, the ends kept).
std::vector<V3> smooth(const std::vector<V3>& p, int passes = 1) {
    std::vector<V3> a = p;
    for (int k = 0; k < passes; k++) {
        std::vector<V3> o;
        o.push_back(a.front());
        for (size_t i = 0; i + 1 < a.size(); i++) {
            o.push_back(a[i] * 0.75 + a[i + 1] * 0.25);
            o.push_back(a[i] * 0.25 + a[i + 1] * 0.75);
        }
        o.push_back(a.back());
        a = o;
    }
    return a;
}
// Letters lying on a level top face at height y, read from the front (+X): the line's top toward the back.
void topText(B& b, const std::string& s, double x, double y, double z, double px) {
    b.push();
    b.move(x, y, z);
    b.pitch(90);
    b.letters(s, V3(0, 0, 0), px, 0.002);
    b.pop();
}
// A turned clamp ring (hose clamp / coupler) on a pipe running along `dir` at p.
void coupler(B& b, const V3& p, const V3& dir, double r, double w) {
    const V3 d = norm(dir) * (w / 2);
    b.cyl(p - d, p + d, r, 10);
}

// ---- The family ----------------------------------------------------------------------------------
enum CamStyle { CAM_PLAIN, CAM_5VZ, CAM_GR };
struct Spec {
    V3 block;
    Mat blockMat = METAL;
    V3 head = kAlu;
    CamStyle cams = CAM_PLAIN;
    V3 camCol = kAlu;
    Mat camMat = METAL;
    bool turbos = false;      // turbo manifolds (no downpipes off the heads: they go through the turbos)
    bool beltCover = false;   // a tall black plastic cam-belt cover over the front (5VZ); else a cast chain cover
    bool coils = true;        // coil-on-plug on the cam covers
};

// A plain pulley (the belt hides its groove): a short closed cylinder on the X axis from x = c.x forward by w.
void pul(B& b, const V3& c, double r, double w, int segs = 10) { xcyl(b, c.x, c.x + w, c.y, c.z, r, segs); }

// One bank, built in its own frame (rolled BANK degrees out; +y up the bore, +z outboard).
void bank(B& b, const Eng& e, const Spec& s, bool first) {
    const V3 block = grime(e, s.block, 0.8), head = grime(e, s.head, 0.8);
    b.style(block, s.blockMat);
    b.box(V3(-0.02, 0.17, 0), V3(0.43, 0.20, 0.165), 0.012);
    for (int i = 0; i < 3; i++) b.box(V3(-0.155 + 0.13 * i, 0.16, 0.084), V3(0.03, 0.17, 0.008));  // bore bosses on the outside
    b.style(head, METAL);
    b.box(V3(-0.02, 0.315, 0), V3(0.42, 0.085, 0.18), 0.012);
    // Exhaust side: the manifold log on the outboard face of the head, three port flanges.
    b.style(grime(e, kManifold, 0.6), METAL);
    b.box(V3(-0.03, 0.30, 0.112), V3(0.34, 0.055, 0.045), 0.016);
    for (int i = 0; i < 3; i++) b.box(V3(-0.165 + 0.13 * i, 0.305, 0.097), V3(0.05, 0.065, 0.03));
    if (e.wear > 0.1) rustSpots(b, e.seed + (first ? 0 : 50), V3(-0.03, 0.30, 0.0), V3(0.3, 0.04, 0), (int)(2 + 4 * e.wear), 2, 0.136);
    // Cam cover.
    const V3 cc = grime(e, s.camCol, 0.5);
    switch (s.cams) {
        case CAM_PLAIN:  // (mostly under the big plastic cover)
            b.style(cc, s.camMat);
            b.box(V3(-0.02, 0.385, 0), V3(0.40, 0.06, 0.155), 0.02);
            break;
        case CAM_5VZ: {  // long cast covers: a raised spine between the cams with cast ribs, plug boots, coil packs on one bank
            b.style(cc, s.camMat);
            b.box(V3(-0.02, 0.38, 0), V3(0.41, 0.05, 0.165), 0.018);
            b.box(V3(-0.02, 0.415, -0.03), V3(0.39, 0.03, 0.085), 0.012);
            b.style(grime(e, cc * 0.78, 0.5), METAL);
            for (int i = 0; i < 4; i++) b.box(V3(-0.17 + 0.1 * i, 0.433, -0.03), V3(0.01, 0.006, 0.07));
            b.style(kBlack, MATTE);
            for (int i = 0; i < 3; i++) b.cyl(V3(-0.155 + 0.13 * i, 0.40, 0.05), V3(-0.155 + 0.13 * i, 0.44, 0.055), 0.013, 6);
            if (first) {
                b.style(grime(e, V3(0.10, 0.10, 0.11), 0.3), PAINT);
                b.box(V3(-0.02, 0.455, 0.05), V3(0.32, 0.016, 0.07));  // coil pack bracket
                b.style(kBlack, MATTE);
                for (int i = 0; i < 3; i++) b.box(V3(-0.13 + 0.11 * i, 0.48, 0.05), V3(0.075, 0.035, 0.065));
            }
            break;
        }
        case CAM_GR: {  // slim DOHC covers, a black coil on every plug down the middle
            b.style(cc, s.camMat);
            b.box(V3(-0.02, 0.38, 0), V3(0.41, 0.05, 0.165), 0.02);
            b.box(V3(-0.02, 0.408, 0.045), V3(0.38, 0.016, 0.05));
            b.box(V3(-0.02, 0.408, -0.05), V3(0.38, 0.016, 0.045));
            if (s.coils) {
                b.style(kBlack, MATTE);
                for (int i = 0; i < 3; i++) {
                    const double x = -0.155 + 0.13 * i;
                    b.box(V3(x, 0.425, -0.0), V3(0.05, 0.04, 0.05));
                    b.box(V3(x + 0.034, 0.432, -0.0), V3(0.02, 0.022, 0.03));
                }
                b.tube({V3(-0.22, 0.448, 0.0), V3(0.2, 0.448, 0.0)}, 0.007, 4);  // harness
            }
            if (first) {
                b.style(kBlack, MATTE);
                b.cyl(V3(0.15, 0.40, 0.05), V3(0.15, 0.44, 0.05), 0.022, 8);
            }
            break;
        }
    }
}

// The whole engine but its top-of-valley parts: block, banks, pan, front drive, flywheel, starter, filter, exhausts.
void core(B& b, const Eng& e, const Spec& s) {
    const V3 block = grime(e, s.block, 0.8);
    // Crankcase and skirt, its main-cap webs.
    b.style(block, s.blockMat);
    xprism(b, symm({V2(0.165, -0.13), V2(0.19, -0.02), V2(0.17, 0.08), V2(0.0, 0.12)}), -0.24, XF);
    b.both([&] {
        for (int i = 0; i < 3; i++) b.box(V3(-0.15 + 0.15 * i, -0.06, 0.18), V3(0.022, 0.12, 0.024));
    });
    // Banks.
    int side = 0;
    b.both([&] {
        const bool first = side++ == 0;
        b.push();
        b.roll(BANK);
        bank(b, e, s, first);
        b.pop();
    });
    // Valley floor.
    b.style(block, s.blockMat);
    b.box(V3(-0.02, 0.20, 0), V3(0.40, 0.04, 0.12));
    // Pan and its rail.
    oilPan(b, grime(e, s.cams == CAM_5VZ ? V3(0.2, 0.2, 0.21) : kCastAlu, 0.9), s.cams == CAM_5VZ ? PAINT : METAL, 0.19, -0.235, -0.125, -0.165, -0.235, 0.27);
    b.style(block, s.blockMat);
    b.box(V3(-0.02, -0.128, 0), V3(0.44, 0.012, 0.31));
    b.style(grime(e, V3(0.45, 0.45, 0.47), 0.6), METAL);
    b.cyl(V3(-0.17, -0.236, 0.05), V3(-0.17, -0.25, 0.05), 0.014, 6);  // drain plug
    // Rear flange and flywheel.
    rearFace(b, e, FX, 0.04, 0.40, 0.36, 0.145, block, s.blockMat);
    // Front cover: a cast chain cover, or the tall black plastic belt cover with its cam-pulley bulges.
    if (s.beltCover) {
        b.style(grime(e, s.block, 0.8), s.blockMat);
        xprism(b, symm({V2(0.15, -0.13), V2(0.18, 0.02), V2(0.10, 0.12), V2(0.0, 0.14)}), XF, XF + 0.035);
        b.style(grime(e, V3(0.07, 0.07, 0.075), 0.3), MATTE);
        xprism(b, symm({V2(0.11, 0.08), V2(0.20, 0.16), V2(0.27, 0.25), V2(0.26, 0.35), V2(0.19, 0.40), V2(0.09, 0.34), V2(0.0, 0.31)}), XF, XF + 0.055);
        b.style(grime(e, V3(0.1, 0.1, 0.105), 0.3), MATTE);
        b.both([&] { b.cyl(V3(XF + 0.055, 0.31, 0.19), V3(XF + 0.07, 0.31, 0.19), 0.06, 10); });
    } else {
        b.style(grime(e, kCastAlu, 0.8), METAL);
        xprism(b, symm({V2(0.15, -0.13), V2(0.19, 0.0), V2(0.245, 0.19), V2(0.25, 0.25), V2(0.2, 0.31), V2(0.14, 0.31), V2(0.07, 0.25), V2(0.0, 0.22)}), XF, XF + 0.035);
    }
    // Crank damper and pulley; water pump; accessories on a serpentine belt.
    const double fx = XF + 0.035, px = fx + 0.035, pw = 0.026;
    const V3 pulleyCol = grime(e, V3(0.18, 0.18, 0.19), 0.4);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    xlathe(b, V3(fx, 0, 0), {V2(0, 0), V2(0.085, 0), V2(0.085, 0.035), V2(0, 0.035)}, 12);
    b.style(pulleyCol, METAL);
    pul(b, V3(px, 0, 0), 0.075, pw, 10);
    // Alternator (left, low), A/C compressor (right, low), power steering (right, high), tensioner.
    b.style(grime(e, V3(0.62, 0.62, 0.6), 0.7), METAL);
    xcyl(b, 0.12, px, 0.06, 0.215, 0.055, 8);
    b.style(grime(e, V3(0.42, 0.43, 0.45), 0.7), METAL);
    xcyl(b, 0.08, px, -0.07, -0.21, 0.06, 8);
    b.style(grime(e, kBlack, 0.3), PAINT);
    xcyl(b, 0.18, px, 0.17, -0.18, 0.045, 8);
    b.style(pulleyCol, METAL);
    pul(b, V3(fx, 0.17, 0), 0.05, pw + 0.035, 8);  // water pump pulley on its boss
    pul(b, V3(px, 0.06, 0.215), 0.035, pw, 8);
    pul(b, V3(px, -0.07, -0.21), 0.055, pw, 8);
    pul(b, V3(px, 0.17, -0.18), 0.05, pw, 8);
    pul(b, V3(px, 0.1, 0.1), 0.03, pw, 6);
    beltX(b, px + 0.013, {{0, 0, 0.075}, {0.215, 0.06, 0.035}, {-0.21, -0.07, 0.055}, {-0.18, 0.17, 0.05}, {0, 0.17, 0.05}, {0.1, 0.1, 0.03}}, 0.02, V3(0.04, 0.04, 0.042));
    // Oil filter and its boss (left), starter (right, at the flywheel), dipstick.
    b.style(grime(e, kCastAlu, 0.8), METAL);
    zcyl(b, 0.06, -0.08, 0.15, 0.20, 0.05, 8);
    b.style(grime(e, V3(0.08, 0.16, 0.36), 0.5), PAINT);
    zcyl(b, 0.06, -0.08, 0.2, 0.29, 0.045, 10);
    b.style(grime(e, V3(0.32, 0.32, 0.33), 0.7), METAL);
    xcyl(b, -0.235, -0.10, -0.1, -0.19, 0.048, 8);
    b.style(grime(e, V3(0.1, 0.1, 0.11), 0.4), PAINT);
    b.box(V3(-0.15, -0.05, -0.19), V3(0.07, 0.035, 0.04));  // solenoid
    b.style(V3(0.75, 0.55, 0.1), MATTE);
    b.tube({V3(0.14, -0.1, 0.17), V3(0.15, 0.15, 0.22), V3(0.16, 0.3, 0.29)}, 0.006, 4);
    b.cyl(V3(0.16, 0.3, 0.29), V3(0.163, 0.33, 0.30), 0.014, 6);
    // Engine mounts on the block's sides.
    b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), PAINT);
    b.both([&] { b.box(V3(0.04, 0.0, 0.2), V3(0.1, 0.08, 0.04)); });
    // Natural exhaust: each manifold's outlet down the side to a pipe running back, a heat shield over the log.
    if (!s.turbos) {
        b.both([&] {
            b.style(grime(e, kManifold, 0.7), METAL);
            b.tube(smooth({V3(-0.17, 0.24, 0.265), V3(-0.19, 0.04, 0.29), V3(-0.25, -0.16, 0.24)}), 0.03, 7);
            b.style(grime(e, V3(0.62, 0.62, 0.64), 0.6), METAL);
            b.push();
            b.roll(BANK);
            b.box(V3(-0.03, 0.30, 0.138), V3(0.30, 0.07, 0.005));
            b.pop();
        });
    }
    if (e.wear > 0.2) rustSpots(b, e.seed + 9, V3(-0.02, -0.2, 0), V3(0.36, 0.0, 0.22), (int)(3 + 6 * e.wear), 1, -0.237);
}

// ---- Turbos (TWIN-TURBO V6) ------------------------------------------------------------------------
// A turbo with its shaft along X at c (on the +z side; `s` scales it): compressor ahead, turbine behind.
void turbo(B& b, const Eng& e, const V3& c, double s) {
    // Turbine housing (cast iron, behind).
    b.style(grime(e, V3(0.27, 0.22, 0.19), 0.6), METAL);
    xlathe(b, V3(c.x - 0.09 * s, c.y, c.z), {V2(0, 0), V2(0.045 * s, 0), V2(0.07 * s, 0.03 * s), V2(0.045 * s, 0.065 * s), V2(0, 0.065 * s)}, 10);
    // Centre cartridge.
    b.style(grime(e, V3(0.42, 0.42, 0.44), 0.5), METAL);
    xcyl(b, c.x - 0.03 * s, c.x + 0.02 * s, c.y, c.z, 0.035 * s, 8);
    // Compressor housing (cast aluminium): the scroll, the inlet bell, and the outlet neck.
    b.style(grime(e, V3(0.70, 0.70, 0.72), 0.7), METAL);
    xlathe(b, V3(c.x + 0.02 * s, c.y, c.z), {V2(0, 0), V2(0.07 * s, 0), V2(0.075 * s, 0.025 * s), V2(0.044 * s, 0.05 * s), V2(0.038 * s, 0.085 * s), V2(0, 0.085 * s)}, 10);
    b.tube({V3(c.x + 0.045 * s, c.y + 0.04 * s, c.z + 0.035 * s), V3(c.x + 0.045 * s, c.y + 0.1 * s, c.z + 0.035 * s)}, 0.026 * s, 8);
    // Heat shield: a pressed hood over the turbine.
    b.style(grime(e, V3(0.6, 0.6, 0.62), 0.5), METAL);
    b.push();
    b.move(c.x - 0.06 * s, c.y + 0.06 * s, c.z + 0.035 * s);
    b.roll(35);
    b.box(V3(0, 0, 0), V3(0.075 * s, 0.006, 0.1 * s));
    b.pop();
}

void twinTurbos(B& b, const Eng& e, double s) {
    const double tx = -0.12, ty = 0.045, tz = 0.285;
    b.both([&] {
        turbo(b, e, V3(tx, ty, tz), s);
        // Manifold drop into the turbine, and the downpipe off it, under and back.
        b.style(grime(e, kManifold, 0.7), METAL);
        b.tube({V3(-0.13, 0.25, 0.24), V3(-0.15, 0.17, 0.26), V3(-0.16, ty + 0.06 * s, tz)}, 0.03, 7);
        b.tube({V3(tx - 0.085 * s, ty - 0.02, tz - 0.01), V3(tx - 0.11, -0.11, tz - 0.03), V3(-0.25, -0.2, 0.22)}, 0.032, 7);
        // Charge pipe: up off the compressor outlet, forward along the block, down to the intercooler at the front.
        const double py = ty + 0.1 * s;
        b.style(grime(e, V3(0.6, 0.61, 0.63), 0.6), METAL);
        b.tube(smooth({V3(tx + 0.045 * s, py - 0.01, tz + 0.035 * s), V3(tx + 0.05 * s, py + 0.06, tz + 0.03 * s), V3(0.12, py + 0.02, 0.31), V3(0.34, -0.14, 0.28)}), 0.027, 7);
        b.style(kRubber, MATTE);
        coupler(b, V3(0.31, -0.13, 0.287), V3(1, -0.45, -0.15), 0.033, 0.05);  // hose to the intercooler
        // Compressor inlet: a black hose forward and up to the inlet duct under the cover's edge.
        b.tube(smooth({V3(tx + 0.095 * s, ty, tz), V3(tx + 0.17, ty + 0.01, tz), V3(0.1, 0.2, 0.25), V3(0.17, 0.3, 0.2)}), 0.034 * s, 7);
    });
}

// ---- Designs ---------------------------------------------------------------------------------------
const std::vector<std::string>& coverNames() {
    static const std::vector<std::string> n = {"BLACK", "GRAPHITE", "SILVER", "RED"};
    return n;
}
V3 coverColor(int i) {
    static const V3 c[] = {V3(0.06, 0.06, 0.065), V3(0.17, 0.175, 0.19), V3(0.52, 0.53, 0.55), V3(0.42, 0.06, 0.05)};
    return c[std::clamp(i, 0, 3)];
}

// TWIN-TURBO V6: the 2.7 / 3.0 (Raptor).
void ecoboost(B& b, const Eng& e, const P& p) {
    const bool raptor = p.i("raptor") == 1;
    Spec s;
    s.block = V3(0.31, 0.31, 0.32);  // compacted graphite iron
    s.blockMat = METAL;
    s.cams = CAM_PLAIN;
    s.camCol = V3(0.12, 0.12, 0.13);
    s.camMat = PAINT;
    s.turbos = true;
    core(b, e, s);
    // Plastic intake in the valley, the throttle body on its nose, the hose back from the intercooler.
    b.style(grime(e, V3(0.09, 0.09, 0.1), 0.3), MATTE);
    b.box(V3(-0.02, 0.33, 0), V3(0.40, 0.08, 0.2));
    b.box(V3(0.2, 0.37, 0), V3(0.06, 0.09, 0.11), 0.015);
    b.style(grime(e, kCastAlu, 0.6), METAL);
    xcyl(b, 0.22, 0.27, 0.38, 0, 0.042, 10);
    b.style(kBlack, MATTE);
    b.box(V3(0.24, 0.43, -0.03), V3(0.04, 0.03, 0.04));  // throttle motor
    b.style(kRubber, MATTE);
    b.tube(smooth({V3(0.27, 0.38, 0), V3(0.32, 0.37, 0), V3(0.34, 0.17, -0.1)}), 0.04, 8);
    twinTurbos(b, e, raptor ? 1.08 : 1.0);
    // The big ribbed engine cover over the valley and the cam covers' inner halves.
    const V3 cov = grime(e, coverColor(p.i("cover")), 0.4);
    const V3 accent = raptor ? V3(0.82, 0.34, 0.07) : V3(0.5, 0.51, 0.53);
    const double x0 = -0.22, x1 = 0.19;
    b.style(cov, MATTE);
    if (!raptor) {
        // 2.7: a smooth rounded hump, ribbed shoulders, the lettering on its flat top.
        xprism(b, symm({V2(0.215, 0.33), V2(0.235, 0.37), V2(0.21, 0.42), V2(0.15, 0.455), V2(0.0, 0.465)}), x0, x1);
        xprism(b, symm({V2(0.2, 0.35), V2(0.19, 0.41), V2(0.13, 0.445), V2(0.0, 0.455)}), x1, x1 + 0.03);  // front bevel
        b.style(mix(cov, V3(0.3, 0.3, 0.32), 0.25), MATTE);
        b.both([&] {
            for (int i = 0; i < 5; i++) {
                b.push();
                b.move(x0 + 0.05 + 0.075 * i, 0.427, 0.19);
                b.roll(35);
                b.box(V3(0, 0, 0), V3(0.03, 0.012, 0.075));
                b.pop();
            }
        });
        b.style(V3(0.78, 0.78, 0.8), CHROME);
        topText(b, "2.7", -0.10, 0.465, 0, 0.013);
        topText(b, "TWIN TURBO", 0.09, 0.465, 0, 0.0055);
    } else {
        // 3.0: angular, a raised spine with sharp ridges and orange accents, vented wings.
        xprism(b, symm({V2(0.22, 0.32), V2(0.24, 0.36), V2(0.2, 0.43), V2(0.12, 0.45), V2(0.09, 0.485), V2(0.0, 0.49)}), x0, x1);
        xprism(b, symm({V2(0.2, 0.34), V2(0.16, 0.43), V2(0.08, 0.455), V2(0.06, 0.47), V2(0.0, 0.475)}), x1, x1 + 0.035);
        b.style(accent, PAINT);
        b.both([&] {
            b.push();
            b.move(-0.02, 0.437, 0.16);
            b.roll(-16);
            b.box(V3(0, 0, 0), V3(0.40, 0.01, 0.03));
            b.pop();
            b.box(V3(-0.02, 0.468, 0.106), V3(0.4, 0.03, 0.01));  // the spine's flanks
        });
        b.style(V3(0.78, 0.78, 0.8), CHROME);
        topText(b, "3.0", -0.10, 0.49, 0, 0.0095);
        topText(b, "TWIN TURBO", 0.07, 0.49, 0, 0.0028);
        b.style(accent, PAINT);
        topText(b, "RAPTOR", 0.207, 0.475, 0, 0.0028);
    }
    // Oil filler through the cover.
    b.style(kBlack, MATTE);
    b.cyl(V3(0.1, 0.41, 0.17), V3(0.1, 0.46, 0.18), 0.024, 8);
}

// 5VZ: the cast plenum, or the roots blower on top.
void fiveVz(B& b, const Eng& e, const P& p) {
    const bool blower = p.i("blower") == 1;
    Spec s;
    s.block = V3(0.22, 0.23, 0.245);  // painted iron
    s.blockMat = PAINT;
    s.cams = CAM_5VZ;
    s.camCol = V3(0.56, 0.57, 0.58);
    s.beltCover = true;
    core(b, e, s);
    const V3 alu = grime(e, V3(0.62, 0.63, 0.64), 0.7), dark = grime(e, V3(0.44, 0.45, 0.46), 0.7);
    // Lower intake manifold in the valley and six runner necks up to the plenum.
    b.style(alu, METAL);
    b.box(V3(-0.03, 0.31, 0), V3(0.40, 0.07, 0.17));
    b.both([&] {
        for (int i = 0; i < 3; i++) b.beam(V3(-0.155 + 0.13 * i, 0.30, 0.13), V3(-0.155 + 0.13 * i, 0.41, 0.07), 0.05, 0.04, V3(1, 0, 0));
    });
    if (!blower) {
        // The big cast plenum front to back, its lettering, the throttle body at the front.
        b.style(alu, METAL);
        xprism(b, symm({V2(0.10, 0.37), V2(0.12, 0.42), V2(0.105, 0.47), V2(0.07, 0.485), V2(0.0, 0.488)}), -0.22, 0.17);
        b.style(dark, METAL);
        for (int i = 0; i < 3; i++) xprism(b, symm({V2(0.105, 0.38), V2(0.126, 0.42), V2(0.11, 0.474), V2(0.0, 0.49)}), -0.2 + 0.15 * i, -0.188 + 0.15 * i);  // cast ribs
        b.style(alu, METAL);
        xlathe(b, V3(0.17, 0.43, 0), {V2(0, 0), V2(0.058, 0), V2(0.045, 0.035), V2(0, 0.035)}, 10);  // the plenum's rounded nose
        b.style(V3(0.2, 0.2, 0.22), PAINT);
        topText(b, "24 VALVE", -0.06, 0.489, 0, 0.0055);
        b.style(grime(e, V3(0.66, 0.66, 0.68), 0.6), METAL);
        xcyl(b, 0.2, 0.26, 0.43, 0, 0.042, 10);  // throttle body
        b.style(kBlack, MATTE);
        b.box(V3(0.235, 0.43, 0.055), V3(0.04, 0.05, 0.02));  // throttle position sensor
        b.style(kRubber, MATTE);
        b.tube(smooth({V3(0.26, 0.43, 0), V3(0.32, 0.43, 0), V3(0.34, 0.39, 0.12), V3(0.35, 0.35, 0.22)}), 0.042, 8);  // to the air box
        coupler(b, V3(0.272, 0.43, 0), V3(1, 0, 0), 0.045, 0.01);
        return;
    }
    // Roots blower: the outlet plenum it sits on, the ribbed twin-lobe case, the snout and drive at the front,
    // the throttle body on its inlet.
    b.style(alu, METAL);
    b.box(V3(-0.04, 0.4, 0), V3(0.38, 0.05, 0.22), 0.012);  // outlet plenum / adapter
    const V3 bc = grime(e, V3(0.70, 0.71, 0.72), 0.6);
    b.style(bc, METAL);
    xprism(b, symm({V2(0.10, 0.425), V2(0.135, 0.46), V2(0.14, 0.52), V2(0.12, 0.565), V2(0.07, 0.59), V2(0.03, 0.585), V2(0.0, 0.575)}), -0.2, 0.14);
    b.style(dark, METAL);
    for (int i = 0; i < 6; i++) {  // the case's ribs, round both lobes
        const double x = -0.175 + 0.058 * i;
        xprism(b, symm({V2(0.106, 0.43), V2(0.146, 0.465), V2(0.15, 0.52), V2(0.128, 0.57), V2(0.07, 0.6), V2(0.0, 0.585)}), x - 0.007, x + 0.007);
    }
    // The case's front cover and the snout to the drive pulley.
    b.style(bc, METAL);
    xprism(b, symm({V2(0.10, 0.43), V2(0.13, 0.47), V2(0.13, 0.52), V2(0.1, 0.565), V2(0.0, 0.58)}), 0.14, 0.17);
    const double by = 0.53, bz = -0.045;
    xlathe(b, V3(0.17, by, bz), {V2(0, 0), V2(0.05, 0), V2(0.03, 0.09), V2(0, 0.09)}, 10);
    b.style(grime(e, V3(0.16, 0.16, 0.17), 0.4), METAL);
    pul(b, V3(0.26, by, bz), 0.052, 0.03, 12);
    // Blower belt: a second groove on the crank up to the blower pulley, round an idler.
    pul(b, V3(0.296, 0, 0), 0.07, 0.022, 12);
    pul(b, V3(0.268, 0.3, -0.13), 0.035, 0.03, 8);
    b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), PAINT);
    b.box(V3(0.255, 0.18, -0.1), V3(0.02, 0.3, 0.04));  // idler bracket
    beltX(b, 0.284, {{0, 0, 0.07}, {bz, by, 0.052}, {-0.13, 0.3, 0.035}}, 0.022, V3(0.04, 0.04, 0.042));
    // Inlet and throttle body: on the left of the snout, facing forward, the hose off it to the air box.
    b.style(alu, METAL);
    b.box(V3(0.16, 0.54, 0.065), V3(0.06, 0.08, 0.1), 0.012);
    b.style(grime(e, V3(0.66, 0.66, 0.68), 0.6), METAL);
    xcyl(b, 0.19, 0.25, 0.55, 0.07, 0.042, 10);
    b.style(kBlack, MATTE);
    b.box(V3(0.225, 0.55, 0.12), V3(0.04, 0.05, 0.02));
    b.style(kRubber, MATTE);
    b.tube(smooth({V3(0.25, 0.55, 0.07), V3(0.31, 0.55, 0.075), V3(0.34, 0.49, 0.16), V3(0.35, 0.42, 0.24)}), 0.045, 8);
    coupler(b, V3(0.262, 0.55, 0.07), V3(1, 0, 0), 0.048, 0.01);
}

// GR: 1GR's plastic plenum or 2GR's cover; the intercooled blower; the cold-air intake.
void grV6(B& b, const Eng& e, const P& p) {
    const bool gen2 = p.i("gen") == 1, blower = p.i("blower") == 1, cai = p.i("intake") == 1;
    Spec s;
    s.block = V3(0.57, 0.58, 0.59);
    s.cams = CAM_GR;
    s.camCol = gen2 ? V3(0.13, 0.13, 0.14) : V3(0.6, 0.61, 0.62);
    s.camMat = gen2 ? PAINT : METAL;
    core(b, e, s);
    const V3 plastic = grime(e, V3(0.07, 0.07, 0.075), 0.3);
    // The throttle body sits at the front of whatever is on top; `tb` is its face, and the intake runs from there.
    V3 tb;
    if (blower) {
        // Magnuson-style: a low, sleek ribbed case filling the valley, an intercooler brick lid on top, the drive at the front.
        const V3 case_ = grime(e, V3(0.68, 0.69, 0.71), 0.6);
        b.style(case_, METAL);
        xprism(b, symm({V2(0.10, 0.3), V2(0.16, 0.36), V2(0.165, 0.43), V2(0.15, 0.47), V2(0.0, 0.48)}), -0.21, 0.16);
        b.style(grime(e, V3(0.54, 0.55, 0.57), 0.6), METAL);
        b.both([&] {
            for (int i = 0; i < 7; i++) b.box(V3(-0.18 + 0.05 * i, 0.40, 0.164), V3(0.014, 0.07, 0.012));
        });
        // Intercooler brick: a black lid with its core's fins showing through, coolant hoses.
        b.style(grime(e, V3(0.1, 0.1, 0.11), 0.3), PAINT);
        b.box(V3(-0.03, 0.495, 0), V3(0.34, 0.035, 0.27), 0.01);
        b.style(grime(e, V3(0.38, 0.39, 0.41), 0.5), METAL);
        for (int i = 0; i < 9; i++) b.box(V3(-0.16 + 0.03 * i, 0.514, 0), V3(0.01, 0.006, 0.2));
        b.style(V3(0.78, 0.78, 0.8), CHROME);
        topText(b, "SUPERCHARGED", 0.135, 0.5125, 0, 0.0033);
        b.style(kRubber, MATTE);
        b.tube({V3(-0.17, 0.5, -0.13), V3(-0.17, 0.52, -0.18), V3(-0.12, 0.45, -0.23)}, 0.012, 5);
        b.tube({V3(-0.10, 0.5, -0.13), V3(-0.10, 0.52, -0.18), V3(-0.05, 0.45, -0.23)}, 0.012, 5);
        // Front: snout, pulley, its own belt round the crank's second groove.
        b.style(case_, METAL);
        xlathe(b, V3(0.16, 0.42, -0.06), {V2(0, 0), V2(0.055, 0), V2(0.03, 0.09), V2(0, 0.09)}, 10);
        b.style(grime(e, V3(0.16, 0.16, 0.17), 0.4), METAL);
        pul(b, V3(0.25, 0.42, -0.06), 0.045, 0.03, 12);
        pul(b, V3(0.296, 0, 0), 0.07, 0.022, 12);
        pul(b, V3(0.268, 0.27, -0.14), 0.035, 0.03, 8);
        b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), PAINT);
        b.box(V3(0.255, 0.16, -0.11), V3(0.02, 0.28, 0.04));
        beltX(b, 0.27, {{0, 0, 0.07}, {-0.06, 0.42, 0.045}, {-0.14, 0.27, 0.035}}, 0.022, V3(0.04, 0.04, 0.042));
        // Inlet: the throttle body on the case's front-left.
        b.style(case_, METAL);
        b.box(V3(0.18, 0.42, 0.07), V3(0.05, 0.1, 0.11), 0.012);
        b.style(grime(e, V3(0.6, 0.6, 0.62), 0.6), METAL);
        xcyl(b, 0.2, 0.25, 0.43, 0.075, 0.042, 10);
        tb = V3(0.25, 0.43, 0.075);
    } else if (!gen2) {
        // 1GR: a wide, flat black plastic plenum between the cam covers, runner ribs, the throttle body ahead.
        b.style(plastic, MATTE);
        xprism(b, symm({V2(0.13, 0.33), V2(0.165, 0.38), V2(0.16, 0.43), V2(0.13, 0.455), V2(0.0, 0.46)}), -0.22, 0.13);
        xprism(b, symm({V2(0.09, 0.35), V2(0.11, 0.41), V2(0.09, 0.44), V2(0.0, 0.445)}), 0.13, 0.2);
        b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), MATTE);
        b.both([&] {
            for (int i = 0; i < 3; i++) b.box(V3(-0.155 + 0.13 * i, 0.455, 0.11), V3(0.05, 0.012, 0.08));  // runner tops
        });
        b.style(V3(0.62, 0.62, 0.64), METAL);
        topText(b, "VVT-I", -0.06, 0.4605, 0, 0.0075);
        topText(b, "4.0", 0.07, 0.4605, 0, 0.0075);
        b.style(grime(e, kCastAlu, 0.6), METAL);
        xcyl(b, 0.2, 0.25, 0.4, 0, 0.042, 10);
        b.style(kBlack, MATTE);
        b.box(V3(0.225, 0.4, -0.055), V3(0.04, 0.05, 0.022));
        tb = V3(0.25, 0.4, 0);
    } else {
        // 2GR-FKS: a sculpted plastic engine cover between the cam covers, its fresh-air snorkel off the front.
        const V3 cov = grime(e, V3(0.15, 0.155, 0.17), 0.3);
        b.style(plastic, MATTE);
        b.box(V3(-0.02, 0.36, 0), V3(0.40, 0.07, 0.24));  // plastic intake under it
        b.style(cov, PAINT);
        xprism(b, symm({V2(0.16, 0.38), V2(0.2, 0.43), V2(0.17, 0.475), V2(0.07, 0.495), V2(0.0, 0.497)}), -0.2, 0.12);
        xprism(b, symm({V2(0.12, 0.39), V2(0.15, 0.43), V2(0.12, 0.46), V2(0.05, 0.475), V2(0.0, 0.477)}), 0.12, 0.17);  // stepped nose
        b.style(grime(e, V3(0.58, 0.59, 0.61), 0.5), METAL);
        b.both([&] {
            b.push();
            b.move(-0.04, 0.455, 0.185);
            b.roll(48);
            b.box(V3(0, 0, 0), V3(0.3, 0.008, 0.02));  // silver trim down the flanks
            b.pop();
        });
        b.style(V3(0.78, 0.78, 0.8), CHROME);
        topText(b, "3.5", -0.06, 0.4975, 0, 0.011);
        topText(b, "V6", 0.07, 0.4975, 0, 0.006);
        b.style(grime(e, kCastAlu, 0.6), METAL);
        xcyl(b, 0.17, 0.22, 0.4, -0.06, 0.04, 10);
        tb = V3(0.22, 0.4, -0.06);
        if (!cai) {
            // Fresh-air snorkel: a flat duct off the cover's front-left, forward to the grille.
            b.style(plastic, MATTE);
            xprism(b, {V2(0.03, 0.43), V2(0.15, 0.43), V2(0.15, 0.47), V2(0.03, 0.47)}, 0.12, 0.33);
            xprism(b, {V2(0.02, 0.425), V2(0.16, 0.425), V2(0.16, 0.475), V2(0.02, 0.475)}, 0.33, 0.35);
            b.style(V3(0.02, 0.02, 0.022), MATTE);
            b.box(V3(0.351, 0.45, 0.09), V3(0.004, 0.032, 0.11));  // its mouth
        }
    }
    // Intake air: the stock hose toward the air box at the right fender, or a cold-air kit's tube and big cone filter.
    if (!cai) {
        b.style(kRubber, MATTE);
        b.tube(smooth({tb, tb + V3(0.05, 0, 0), V3(0.33, tb.y - 0.03, -0.12), V3(0.34, tb.y - 0.08, -0.25)}), 0.042, 8);
        coupler(b, tb + V3(0.015, 0, 0), V3(1, 0, 0), 0.045, 0.01);
    } else {
        b.style(grime(e, V3(0.14, 0.2, 0.42), 0.4), PAINT);  // anodised blue tube
        b.tube(smooth({tb, tb + V3(0.05, 0, 0), V3(0.3, tb.y, tb.z + 0.08), V3(0.32, tb.y - 0.02, 0.22)}), 0.042, 10);
        b.style(kBolt, METAL);
        coupler(b, tb + V3(0.02, 0, 0), V3(1, 0, 0), 0.047, 0.016);
        // The cone filter, pointing out to the left fender.
        const double cy = tb.y - 0.02;
        b.style(grime(e, V3(0.62, 0.62, 0.64), 0.5), METAL);
        zlathe(b, V3(0.32, cy, 0.21), {V2(0, 0), V2(0.074, 0), V2(0.074, 0.016), V2(0, 0.016)}, 12);
        b.style(grime(e, V3(0.52, 0.12, 0.1), 0.6), MATTE);
        zlathe(b, V3(0.32, cy, 0.226), {V2(0, 0), V2(0.072, 0), V2(0.07, 0.04), V2(0.072, 0.045), V2(0.067, 0.09), V2(0.069, 0.095), V2(0.06, 0.135), V2(0, 0.135)}, 12);
    }
}

// ---- Presentation --------------------------------------------------------------------------------
using Build = std::function<void(B&, const Eng&, const P&)>;

// Fitted ("bare"), on the stand, strapped to a skid or pulled onto timber. `top`: the top of the engine (for the strap).
void present(B& b, const P& p, const Build& fn, double top) {
    const int mount = p.i("mount");
    Eng e;
    e.seed = (uint32_t)std::max(1, p.i("seed"));
    e.wear = std::clamp(p("wear"), 0.0, 1.0);
    if (!bare(p) && mount == MOUNT_PULLED) e.wear = std::max(e.wear, 0.35);
    auto eng = [&] { fn(b, e, p); };
    if (bare(p)) return eng();
    switch (mount) {
        case MOUNT_SKID:
            engineSkid(b, V3(0.85, 0.62, 0.1), eng, 0.0, {V2(0.21, -0.176), V2(0.345, -0.09), V2(0.36, 0.1), V2(0.31, 0.3), V2(0.18, top + 0.008), V2(0.06, top + 0.012)}, 0.82);
            break;
        case MOUNT_PULLED: engineGround(b, eng, 0.78); break;
        default: engineStand(b, 0.52, FX - 0.04, V3(0.2, 0.22, 0.26), eng); break;
    }
}

const std::vector<Param> tail = {mountParam(), wearParam(), seedParam()};
std::vector<Param> with(std::vector<Param> a) {
    a.insert(a.end(), tail.begin(), tail.end());
    return a;
}

Reg crateEngineV6({"crate_engine_v6", "CRATE ENGINE V6", "PARTS", "A modern V6: twin-turbo, the Toyota 24-valve (supercharged) or a Toyota GR", 0.8,
                   {
                       {"TWIN-TURBO V6", "2.7 or 3.0 twin-turbo: iron block, big plastic cover, a turbo low on each side",
                        with({choice("raptor", "VERSION", 0, {"2.7", "3.0 RAPTOR"}), choice("cover", "COVER", 0, coverNames())}),
                        [](B& b, const P& p) { present(b, p, ecoboost, p.i("raptor") ? 0.49 : 0.47); }},
                       {"TOYOTA 24-VALVE", "3.4 DOHC 24-valve: cast plenum, belt cover; a roots blower on top",
                        with({choice("blower", "BLOWER", 1, {"NONE", "ROOTS"})}),
                        [](B& b, const P& p) { present(b, p, fiveVz, p.i("blower") ? 0.6 : 0.49); }},
                       {"TOYOTA GR", "1GR 4.0 or 2GR 3.5: aluminium block, black plastic intake; blower and cold-air options",
                        with({choice("gen", "ENGINE", 0, {"1GR 4.0", "2GR 3.5"}), choice("blower", "BLOWER", 0, {"NONE", "INTERCOOLED"}),
                              choice("intake", "INTAKE", 0, {"STOCK", "COLD AIR"})}),
                        [](B& b, const P& p) { present(b, p, grV6, p.i("blower") ? 0.52 : p.i("gen") ? 0.5 : 0.47); }},
                   }});

}  // namespace
