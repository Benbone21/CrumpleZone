// Inline and flat engines: one inline family (crate_engine_i4, crate_engine_i3) and the air-cooled flat four
// (crate_engine_flat4). Designs, in drivetrain.h's order:
//   crate_engine_i4     HONDA SOHC    the D16 1.6 single-cam 16-valve: pressed "16 VALVE" cover, distributor on the head's
//                                     end, cast manifold under a heat shield (or the cam-and-header build: 4-into-1, cone filter).
//                       HONDA DOHC    the B16A / B18C: crinkle cam cover with the "DOHC VTEC" plug strip, tall curved
//                                     runners to a high plenum, 4-2-1 header; black B16A or red TYPE R.
//                       TURBO FOUR    the 2.3 turbo four: ribbed plastic cover, coil-on-plug, the turbo on the exhaust side,
//                                     intercooler pipes running forward and down.
//   crate_engine_i3     TURBO TRIPLE  the UTV's 900 cc turbo triple: compact, a big charge pipe up and over the cam cover.
//   crate_engine_flat4  DUAL CARBS    the 2.0 built Type 1 (the Baja Bug's): two carbs under round air cleaners, a 4-into-1.
//                       SINGLE CARB   the 1835 street motor: one centre carb, an oil-bath cleaner, heater boxes, stock muffler.
//                       DUAL 48 STROKER  the 2332: big twin-throat carbs on stacks, linkage bar, merged header and stinger, a
//                                     remote oil cooler and filter, wider barrels.
// Bare frame (drivetrain.h): crank on the X axis, +X the accessory drive (the VW's pulley and fan end), +Y up, +Z left;
// the flywheel's back face (the flange face - 0.033) is the rearmost point. Intakes on +Z, exhausts on -Z.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain.h"
#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;
using namespace drivetrain;

namespace {

// ---- Colours -----------------------------------------------------------------------------------
const V3 kBlack(0.055, 0.055, 0.06), kRubber(0.035, 0.035, 0.04), kBolt(0.13, 0.13, 0.14);
const V3 kAlu(0.62, 0.63, 0.65), kCastAlu(0.53, 0.54, 0.55), kIron(0.27, 0.25, 0.24), kSteel(0.58, 0.55, 0.52);
const V3 kGold(0.72, 0.58, 0.26), kShield(0.6, 0.6, 0.59), kWire(0.09, 0.09, 0.1), kChrome(0.8, 0.81, 0.84);

// ---- Helpers -----------------------------------------------------------------------------------
// A prism along X: `poly` is its (z, y) cross-section, from x0 to x1.
void xprism(B& b, const std::vector<V2>& poly, double x0, double x1) {
    std::vector<V2> q;
    for (const V2& v : poly) q.push_back(V2(-v.x, v.y));
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
// A rounded rectangle (z, y) centred at (cz, cy).
std::vector<V2> rr(double cz, double cy, double w, double h, double r) {
    std::vector<V2> p = roundedRect(w, h, r, 3);
    for (V2& v : p) v.x += cz, v.y += cy;
    return p;
}
// Rounded corners on a path (Chaikin passes, the ends kept).
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
// Letters on a level top face at height y, running along X, read from the +Z (intake) side.
void sideText(B& b, const std::string& s, double x, double y, double z, double px) {
    b.push();
    b.move(x, y, z);
    b.yaw(-90);
    b.pitch(90);
    b.letters(s, V3(0, 0, 0), px, 0.0015);
    b.pop();
}
// A clamp ring / coupler on a pipe running along `dir` at p.
void coupler(B& b, const V3& p, const V3& dir, double r, double w, int segs = 8) {
    const V3 d = norm(dir) * (w / 2);
    b.cyl(p - d, p + d, r, segs, false);
}

// A turbocharger with its shaft along X at c (s scales it): compressor ahead (+X, its inlet facing forward),
// turbine behind (its outlet facing back), the compressor's outlet neck rising (outDir +1) or dropping (-1),
// offset to -z of the shaft.
struct TurboEnds {
    V3 inlet, outlet, exhaust;
};
TurboEnds turbo(B& b, const Eng& e, const V3& c, double s, int outDir, bool shield) {
    TurboEnds t;
    // Turbine housing (cast iron) and its scroll.
    b.style(grime(e, V3(0.25, 0.21, 0.19), 0.6), METAL);
    xlathe(b, V3(c.x - 0.09 * s, c.y, c.z), {V2(0.03 * s, 0), V2(0.042 * s, 0), V2(0.066 * s, 0.03 * s), V2(0.066 * s, 0.055 * s), V2(0.04 * s, 0.068 * s), V2(0, 0.068 * s)}, 10);
    t.exhaust = V3(c.x - 0.09 * s, c.y, c.z);
    // Centre cartridge.
    b.style(grime(e, V3(0.42, 0.42, 0.44), 0.5), METAL);
    xcyl(b, c.x - 0.025 * s, c.x + 0.022 * s, c.y, c.z, 0.034 * s, 10);
    // Compressor housing (cast aluminium), its scroll, inlet and outlet neck.
    const V3 alu = grime(e, V3(0.68, 0.68, 0.7), 0.7);
    b.style(alu, METAL);
    xlathe(b, V3(c.x + 0.02 * s, c.y, c.z), {V2(0, 0), V2(0.064 * s, 0), V2(0.074 * s, 0.018 * s), V2(0.066 * s, 0.04 * s), V2(0.04 * s, 0.052 * s),
                                              V2(0.04 * s, 0.085 * s), V2(0.03 * s, 0.085 * s), V2(0, 0.075 * s)}, 10);
    t.inlet = V3(c.x + 0.105 * s, c.y, c.z);
    const V3 n0(c.x + 0.045 * s, c.y + outDir * 0.05 * s, c.z - 0.035 * s), n1 = n0 + V3(0, outDir * 0.055 * s, 0);
    b.cyl(n0, n1, 0.027 * s, 10);
    t.outlet = n1;
    // Wastegate can on a bracket off the compressor, its rod back to the turbine.
    b.style(grime(e, V3(0.62, 0.58, 0.3), 0.5), METAL);
    zlathe(b, V3(c.x + 0.03 * s, c.y - outDir * 0.075 * s, c.z - 0.05 * s), {V2(0, 0), V2(0.028 * s, 0), V2(0.03 * s, 0.02 * s), V2(0.022 * s, 0.035 * s), V2(0, 0.035 * s)}, 8);
    b.style(kBolt, METAL);
    b.cyl(V3(c.x + 0.03 * s, c.y - outDir * 0.075 * s, c.z - 0.04 * s), V3(c.x - 0.06 * s, c.y - outDir * 0.05 * s, c.z - 0.04 * s), 0.004, 4);
    if (shield) {  // a pressed, dimpled hood over the turbine
        b.style(grime(e, kShield, 0.5), METAL);
        b.box(V3(c.x - 0.055 * s, c.y + 0.078 * s, c.z), V3(0.1 * s, 0.006, 0.15 * s));
        b.box(V3(c.x - 0.055 * s, c.y + 0.02 * s, c.z - 0.078 * s), V3(0.1 * s, 0.12 * s, 0.006));
    }
    return t;
}

// A cone filter on the X axis at `at`, pointing +X (its base clamp at `at`).
void coneFilter(B& b, const V3& at, double r, double len, const V3& col) {
    b.style(col, MATTE);
    xlathe(b, at, {V2(0, 0), V2(r * 0.7, 0), V2(r, len * 0.12), V2(r * 0.78, len * 0.95), V2(0, len * 0.95)}, 12);
    b.style(kChrome, CHROME);
    xlathe(b, at + V3(len * 0.94, 0, 0), {V2(0, 0), V2(r * 0.8, 0), V2(r * 0.78, len * 0.06), V2(r * 0.3, len * 0.07), V2(0, len * 0.07)}, 12);
    coupler(b, at + V3(len * 0.06, 0, 0), V3(1, 0, 0), r * 0.85, 0.016, 12);
    b.style(col * 0.7, MATTE);
    coupler(b, at + V3(len * 0.5, 0, 0), V3(1, 0, 0), r * 0.9 + 0.002, 0.008, 12);
}

// ---- Presentation -------------------------------------------------------------------------------
struct Show {
    double fx;                 // the flange face (the flywheel behind it)
    double standH = 0.46;      // crank height on the stand
    double strapX = 0;         // the skid strap's x
    std::vector<V2> strap;     // its (z, y) path from the rail up over the top
    double skidLen = 0.75, groundLen = 0.65;
};
double wearOf(const P& p) {
    const double w = p("wear");
    return (!bare(p) && p.i("mount") == MOUNT_PULLED) ? std::max(w, 0.35) : w;
}
void present(B& b, const P& p, const Show& s, const EngineFn& f) {
    if (bare(p)) return f();
    switch (p.i("mount")) {
        case MOUNT_SKID: engineSkid(b, V3(0.85, 0.62, 0.1), f, s.strapX, s.strap, s.skidLen); break;
        case MOUNT_PULLED: engineGround(b, f, s.groundLen); break;
        default: engineStand(b, s.standH, s.fx - 0.04, V3(0.2, 0.22, 0.26), f); break;
    }
}

// ---- The inline family ---------------------------------------------------------------------------
enum HeadKind { HD_SOHC, HD_VTEC, HD_ECO, HD_TRIPLE };
enum IntakeKind { IN_SOHC, IN_VTEC, IN_ECO, IN_TRIPLE };
enum ExhaustKind { EX_CAST, EX_4_1, EX_4_2_1, EX_TURBO };
enum FrontKind { FR_BELT, FR_CHAIN, FR_TRIPLE };

struct Inl {
    int n = 4;
    double pitch = 0.09;
    double fx = -0.24, xf = 0.21;  // the rear flange face; the block's front face
    double deck = 0.2, halfDeck = 0.098, halfSkirt = 0.12, rail = -0.07, panBot = -0.18, panW = 0.21;
    double headH = 0.1, halfHead = 0.1;
    double flyR = 0.13;
    V3 block = V3(0.6, 0.61, 0.62);
    Mat blockMat = METAL;
    V3 head = kAlu;
    Mat headMat = METAL;
    V3 pan = V3(0.14, 0.14, 0.15);
    Mat panMat = PAINT;
    V3 cover = kBlack;  // cam cover / engine cover
    HeadKind headKind = HD_SOHC;
    IntakeKind intake = IN_SOHC;
    ExhaustKind exhaust = EX_CAST;
    FrontKind front = FR_BELT;
    bool typeR = false, cone = false, tank = false;
    int stage = 0;
    double x0() const { return fx + 0.03; }
    double mid() const { return (x0() + xf) / 2; }
    double cx(int i) const { return mid() + (i - (n - 1) / 2.0) * pitch; }
    double top() const { return deck + headH; }
};

// Block, pan, flange and flywheel, head.
void inlineBlock(B& b, const Eng& e, const Inl& s) {
    const V3 block = grime(e, s.block, 0.8), head = grime(e, s.head, 0.8);
    const double L = s.xf - s.x0();
    b.style(block, s.blockMat);
    xprism(b, symm({V2(s.halfSkirt, s.rail), V2(s.halfSkirt, 0.03), V2(s.halfDeck + 0.008, s.deck - 0.09), V2(s.halfDeck, s.deck - 0.01), V2(s.halfDeck - 0.01, s.deck), V2(0, s.deck)}), s.x0(), s.xf);
    // Bore bulges up the sides, ribs down the skirt between the cylinders.
    b.both([&] {
        for (int i = 0; i < s.n; i++) b.box(V3(s.cx(i), s.deck - 0.06, s.halfDeck + 0.008), V3(s.pitch - 0.022, 0.09, 0.016));
    });
    // Pan rail and pan.
    b.style(block, s.blockMat);
    b.box(V3(s.mid(), s.rail - 0.006, 0), V3(L + 0.006, 0.012, 2 * s.halfSkirt + 0.014));
    oilPan(b, grime(e, s.pan, 0.9), s.panMat, s.xf - 0.01, s.x0() + 0.01, s.rail - 0.011, s.rail - 0.055, s.panBot, s.panW);
    if (s.panMat == METAL) {  // cast pan: ribs on its sides
        b.style(grime(e, s.pan, 0.9), METAL);
        b.both([&] {
            for (int i = 0; i < 4; i++) b.box(V3(s.x0() + 0.05 + (L - 0.2) / 3 * i, (s.rail + s.panBot) / 2, s.panW / 2 + 0.004), V3(0.012, s.rail - s.panBot - 0.03, 0.01));
        });
    }
    b.style(kBolt, METAL);
    b.both([&] {
        for (int i = 0; i < 2; i++) nut(b, V3(s.x0() + 0.06 + (L - 0.12) * i, s.rail - 0.012, s.halfSkirt - 0.002), 1, 0.015, 0.006);
    });
    b.style(grime(e, V3(0.45, 0.45, 0.47), 0.6), METAL);
    xcyl(b, s.x0() + 0.02, s.x0() + 0.04, s.panBot + 0.03, 0.05, 0.012, 6);  // drain plug
    // Rear flange and flywheel.
    rearFace(b, e, s.fx, (s.rail + s.deck) / 2 - 0.02, 2 * s.halfSkirt + 0.06, s.deck - s.rail + 0.03, s.flyR, block, s.blockMat);
    // Head and gasket.
    b.style(V3(0.16, 0.15, 0.14), MATTE);
    b.box(V3(s.mid(), s.deck + 0.002, 0), V3(L - 0.004, 0.006, 2 * s.halfHead + 0.006));
    b.style(head, s.headMat);
    b.box(V3(s.mid(), s.deck + s.headH / 2 + 0.002, 0), V3(L - 0.01, s.headH - 0.004, 2 * s.halfHead), 0.01);
}

// The front drive: Honda's black belt cover, a cast chain cover, or the triple's cast end cover; pulleys and belts.
void inlineFront(B& b, const Eng& e, const Inl& s) {
    const double X = s.xf;
    const V3 dark = grime(e, V3(0.16, 0.16, 0.17), 0.4);
    switch (s.front) {
        case FR_BELT: {
            const bool dohc = s.headKind == HD_VTEC;
            b.style(grime(e, kBlack, 0.3), MATTE);
            if (dohc)
                xprism(b, symm({V2(0.08, -0.065), V2(0.1, 0.1), V2(0.112, 0.28), V2(0.108, 0.33), V2(0.09, 0.365), V2(0.05, 0.38), V2(0, 0.382)}), X, X + 0.04);
            else
                xprism(b, symm({V2(0.08, -0.065), V2(0.095, 0.1), V2(0.1, 0.25), V2(0.094, 0.29), V2(0.074, 0.325), V2(0.04, 0.348), V2(0, 0.354)}), X, X + 0.04);
            b.style(grime(e, V3(0.09, 0.09, 0.095), 0.3), MATTE);
            if (dohc)
                b.both([&] { xcyl(b, X + 0.04, X + 0.05, 0.3, 0.05, 0.055, 12); });  // the two cam-pulley bulges
            else
                xcyl(b, X + 0.04, X + 0.05, 0.27, 0, 0.075, 12);
            b.style(V3(0.02, 0.02, 0.025), MATTE);
            b.box(V3(X + 0.041, s.deck - 0.005, 0), V3(0.004, 0.006, 0.19));  // upper / lower cover seam
            b.style(kBolt, METAL);
            for (int i = 0; i < 2; i++)
                for (int sd = -1; sd <= 1; sd += 2) nut(b, V3(X + 0.042, 0.05 + 0.15 * i, sd * 0.085), 0, 0.014, 0.006);
            break;
        }
        case FR_CHAIN: {
            b.style(grime(e, kCastAlu, 0.8), METAL);
            xprism(b, symm({V2(0.12, -0.08), V2(0.142, 0.05), V2(0.136, 0.26), V2(0.13, 0.33), V2(0.11, 0.375), V2(0.06, 0.39), V2(0, 0.388)}), X, X + 0.035);
            b.style(kBolt, METAL);
            b.both([&] {
                for (int i = 0; i < 3; i++) nut(b, V3(X + 0.037, -0.05 + 0.17 * i, 0.128 - (i == 2 ? 0.02 : 0)), 0, 0.014, 0.006);
            });
            break;
        }
        case FR_TRIPLE: {
            b.style(grime(e, s.block * 0.92, 0.8), s.blockMat);
            xprism(b, symm({V2(0.088, -0.06), V2(0.104, 0.02), V2(0.1, 0.19), V2(0.075, 0.25), V2(0.03, 0.27), V2(0, 0.272)}), X, X + 0.032);
            b.style(grime(e, s.block * 1.1, 0.8), s.blockMat);
            xlathe(b, V3(X + 0.032, 0, 0), {V2(0, 0), V2(0.07, 0), V2(0.072, 0.012), V2(0.06, 0.024), V2(0, 0.026)}, 14);  // the crank-end cover
            b.style(kBolt, METAL);
            boltRingX(b, V3(X + 0.058, 0, 0), 0.05, 6, 0.013, 0.006);
            for (int i = 0; i < 5; i++) {
                const double a = PI * (0.1 + 0.2 * i);
                nut(b, V3(X + 0.034, 0.11 + 0.13 * std::sin(a), 0.085 * std::cos(a)), 0, 0.012, 0.005);
            }
            // Water pump on the front, its outlet neck and hose.
            b.style(grime(e, kCastAlu, 0.7), METAL);
            xcyl(b, X + 0.03, X + 0.06, 0.15, 0.04, 0.038, 12);
            b.style(kRubber, MATTE);
            b.tube(smooth({V3(X + 0.05, 0.18, 0.06), V3(X + 0.07, 0.22, 0.07), V3(X + 0.06, 0.26, 0.09)}), 0.014, 6);
            return;
        }
    }
    // Crank damper and pulley.
    const double px = X + (s.front == FR_BELT ? 0.042 : 0.036);
    b.style(dark, METAL);
    xcyl(b, px, px + 0.026, 0, 0, 0.075, 12, false);
    pulleyX(b, V3(px + 0.026, 0, 0), 0.066, 0.022, 12);
    b.style(kBolt, METAL);
    xcyl(b, px + 0.046, px + 0.054, 0, 0, 0.016, 6);
    // Alternator low on the intake side, the A/C compressor low on the exhaust side, an idler up top.
    const double az = s.halfSkirt + 0.06, ay = 0.015;
    b.style(grime(e, V3(0.62, 0.62, 0.6), 0.7), METAL);
    xcyl(b, X - 0.11, px + 0.02, ay, az, 0.052, 10);
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
    xcyl(b, X - 0.07, X - 0.06, ay, az, 0.055, 10, false);
    b.style(dark, METAL);
    pulleyX(b, V3(px + 0.004, ay, az), 0.032, 0.022, 10);
    pulleyX(b, V3(px + 0.004, -0.02, -0.1), 0.03, 0.022, 8);  // tensioner
    pulleyX(b, V3(px + 0.004, 0.13, 0.06), 0.026, 0.022, 8);
    beltX(b, px + 0.015, {{0, 0, 0.066}, {az, ay, 0.032}, {0.06, 0.13, 0.026}, {-0.1, -0.02, 0.03}}, 0.018, V3(0.04, 0.04, 0.042));
}

// Distributor on the head's back end (the Hondas): body, cap, plug leads into the head.
void distributor(B& b, const Eng& e, const Inl& s, double y, double z, const std::vector<V3>& plugs) {
    const double x = s.x0() + 0.005;
    b.style(grime(e, kCastAlu, 0.7), METAL);
    xcyl(b, x - 0.022, x, y, z, 0.03, 10);
    b.style(grime(e, V3(0.1, 0.1, 0.11), 0.3), MATTE);
    xlathe(b, V3(s.fx + 0.002, y, z), {V2(0, 0), V2(0.03, 0), V2(0.04, 0.004), V2(0.042, 0.02), V2(0, 0.022)}, 10);
    b.style(kWire, MATTE);
    for (size_t i = 0; i < plugs.size(); i++) {
        const double a = PI * (0.25 + 0.5 * i / std::max<size_t>(1, plugs.size() - 1));
        const V3 tower(s.fx + 0.012, y + 0.035 * std::sin(a), z + 0.035 * std::cos(a));
        b.cyl(tower, tower + V3(0, 0.012 * std::sin(a), 0.012 * std::cos(a)), 0.008, 5, false);
        const V3 p = plugs[i];
        b.tube({tower, tower + V3(0.01, 0.03, 0.0), V3(p.x - 0.04, p.y + 0.02, (p.z + tower.z) / 2), p}, 0.0045, 4, false, false);
    }
}

// ---- Cam covers ----------------------------------------------------------------------------------
void inlineHead(B& b, const Eng& e, const Inl& s) {
    const double T = s.top(), X0 = s.x0() + 0.01, X1 = s.xf - 0.01;
    const V3 cov = grime(e, s.cover, 0.5);
    switch (s.headKind) {
        case HD_SOHC: {
            // Pressed-steel cover: a flange, a rounded loaf, "16 VALVE" across its top, cap nuts down the middle.
            const bool light = s.cover.x > 0.4;
            b.style(cov * 0.9, PAINT);
            b.box(V3(s.mid(), T + 0.004, 0), V3(X1 - X0 + 0.006, 0.008, 2 * s.halfHead - 0.004));
            b.style(cov, PAINT);
            xprism(b, symm({V2(0.092, T + 0.007), V2(0.092, T + 0.03), V2(0.082, T + 0.048), V2(0.06, T + 0.056), V2(0, T + 0.058)}), X0 + 0.01, X1 - 0.01);
            b.box(V3(s.mid(), T + 0.03, 0), V3(X1 - X0, 0.04, 0.17), 0.016);  // rounded ends
            b.style(light ? V3(0.12, 0.12, 0.13) : V3(0.78, 0.79, 0.82), light ? PAINT : CHROME);
            sideText(b, "16 VALVE", s.mid() + 0.02, T + 0.058, 0.032, 0.0052);
            b.style(grime(e, V3(0.6, 0.6, 0.62), 0.5), METAL);
            for (int i = 0; i < 3; i++) nut(b, V3(s.mid() - 0.13 + 0.13 * i, T + 0.059, -0.018), 1, 0.02, 0.01);
            b.style(kBlack, MATTE);
            b.cyl(V3(s.mid() + 0.15, T + 0.05, -0.05), V3(s.mid() + 0.15, T + 0.068, -0.05), 0.022, 10);  // oil filler cap
            // Plug boots in a row on the exhaust shoulder; leads back to the distributor.
            std::vector<V3> plugs;
            b.style(kWire, MATTE);
            for (int i = 0; i < s.n; i++) {
                const V3 p(s.cx(i), T + 0.035, -0.088);
                b.cyl(p + V3(0, -0.02, -0.01), p + V3(0, 0.012, 0.004), 0.011, 5);
                plugs.push_back(p + V3(0, 0.012, 0.004));
            }
            distributor(b, e, s, s.deck + s.headH * 0.55, 0.0, plugs);
            break;
        }
        case HD_VTEC: {
            // Two crinkle-finish cam loaves, the plug strip between them.
            b.style(grime(e, V3(0.05, 0.05, 0.05), 0.3), MATTE);
            b.box(V3(s.mid(), T + 0.003, 0), V3(X1 - X0 + 0.006, 0.006, 2 * s.halfHead - 0.002));
            b.style(cov, MATTE);
            const double y0 = T + 0.005;
            xprism(b, symm({V2(0.104, y0), V2(0.104, y0 + 0.035), V2(0.098, y0 + 0.05), V2(0.088, y0 + 0.058), V2(0.075, y0 + 0.061), V2(0.046, y0 + 0.061),
                            V2(0.034, y0 + 0.057), V2(0.026, y0 + 0.047), V2(0.022, y0 + 0.032), V2(0.022, y0 + 0.022), V2(0, y0 + 0.022)}), X0 + 0.012, X1 - 0.012);
            b.both([&] { b.box(V3(s.mid(), y0 + 0.028, 0.063), V3(X1 - X0, 0.052, 0.08), 0.014); });  // the loaves' rounded ends
            // Cast ribs across each loaf's ends and its bolts.
            b.style(cov * 1.25 + V3(0.02, 0.02, 0.02), MATTE);
            b.both([&] {
                for (int k = -1; k <= 1; k += 2) b.box(V3(s.mid() + k * 0.16, y0 + 0.062, 0.06), V3(0.006, 0.004, 0.04));
            });
            b.style(kBolt, METAL);
            b.both([&] {
                for (int i = 0; i < 3; i++) nut(b, V3(s.mid() - 0.15 + 0.15 * i, y0 + 0.042, 0.1), 2, 0.014, 0.008);
            });
            // Plug strip: a silver plate with the lettering, its leads out the back to the distributor.
            b.style(grime(e, V3(0.66, 0.67, 0.7), 0.5), METAL);
            b.box(V3(s.mid() - 0.005, y0 + 0.026, 0), V3(0.34, 0.008, 0.04));
            b.style(V3(0.72, 0.1, 0.08), MATTE);
            sideText(b, "DOHC VTEC", s.mid() + 0.02, y0 + 0.03, 0, 0.0042);
            if (s.typeR) {
                b.style(V3(0.9, 0.9, 0.9), MATTE);
                sideText(b, "TYPE R", s.mid() + 0.03, y0 + 0.061, 0.0605, 0.0036);
            }
            b.style(kBlack, MATTE);
            b.cyl(V3(s.mid() + 0.13, y0 + 0.055, -0.062), V3(s.mid() + 0.13, y0 + 0.074, -0.062), 0.022, 8);  // oil filler cap
            std::vector<V3> plugs;
            for (int i = 0; i < s.n; i++) plugs.push_back(V3(s.x0() + 0.035, y0 + 0.03, -0.012 + 0.008 * i));
            distributor(b, e, s, s.deck + s.headH * 0.6, 0.05, plugs);
            // VTEC solenoid and spool valve on the back of the head, intake side.
            b.style(grime(e, kCastAlu, 0.7), METAL);
            b.box(V3(s.x0() + 0.03, s.deck + 0.075, s.halfHead + 0.012), V3(0.036, 0.04, 0.026));
            b.style(kBlack, MATTE);
            zcyl(b, s.x0() + 0.03, s.deck + 0.075, s.halfHead + 0.025, s.halfHead + 0.05, 0.015, 6);
            b.style(V3(0.2, 0.22, 0.24), MATTE);
            b.box(V3(s.x0() + 0.03, s.deck + 0.075, s.halfHead + 0.058), V3(0.022, 0.018, 0.016));
            break;
        }
        case HD_ECO: {
            // Cast cover (black), coil-on-plug down the middle, their harness; the ribbed plastic engine cover
            // over the intake side is in inlineIntake.
            b.style(grime(e, V3(0.1, 0.1, 0.11), 0.4), PAINT);
            b.box(V3(s.mid(), T + 0.022, 0), V3(X1 - X0, 0.044, 2 * s.halfHead - 0.01), 0.016);
            b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), PAINT);
            for (int i = 0; i < 4; i++) b.box(V3(s.x0() + 0.06 + 0.1 * i, T + 0.046, -0.085), V3(0.01, 0.006, 0.04));
            b.style(kBlack, MATTE);
            for (int i = 0; i < s.n; i++) {
                b.box(V3(s.cx(i), T + 0.062, 0), V3(0.05, 0.036, 0.05));
                b.box(V3(s.cx(i), T + 0.066, 0.034), V3(0.022, 0.02, 0.02));
            }
            b.style(V3(0.12, 0.12, 0.13), MATTE);
            b.tube({V3(s.x0() + 0.02, T + 0.07, 0.05), V3(s.xf - 0.03, T + 0.07, 0.05)}, 0.008, 6);  // harness
            b.style(grime(e, V3(0.1, 0.1, 0.11), 0.3), MATTE);
            b.cyl(V3(s.mid() + 0.16, T + 0.04, -0.07), V3(s.mid() + 0.16, T + 0.064, -0.07), 0.025, 10);  // oil filler cap
            b.style(kBolt, METAL);
            b.both([&] {
                for (int i = 0; i < 3; i++) nut(b, V3(s.x0() + 0.04 + 0.18 * i, T + 0.03, 0.11), 2, 0.014, 0.006);
            });
            break;
        }
        case HD_TRIPLE: {
            // A short finned cover (black), the coils down the middle, "TURBO" cast in.
            b.style(cov, PAINT);
            b.box(V3(s.mid(), T + 0.018, 0), V3(X1 - X0, 0.036, 2 * s.halfHead - 0.012), 0.012);
            b.style(grime(e, s.cover * 1.6 + V3(0.03, 0.03, 0.03), 0.4), PAINT);
            for (int k = -1; k <= 1; k += 2)
                for (int i = 0; i < 3; i++) b.box(V3(s.mid() + k * (0.105 + 0.016 * i), T + 0.038, 0), V3(0.007, 0.006, 2 * s.halfHead - 0.04));
            b.style(V3(0.7, 0.71, 0.74), MATTE);
            sideText(b, "TURBO", s.mid() - 0.062, T + 0.036, -0.052, 0.0045);
            b.style(V3(0.2, 0.2, 0.22), MATTE);
            for (int i = 0; i < s.n; i++) b.box(V3(s.cx(i), T + 0.05, 0), V3(0.036, 0.03, 0.04));
            b.style(V3(0.5, 0.12, 0.08), MATTE);
            for (int i = 0; i < s.n; i++) b.box(V3(s.cx(i), T + 0.058, 0.026), V3(0.016, 0.014, 0.014));
            b.style(kBolt, METAL);
            b.both([&] {
                for (int i = 0; i < 4; i++) nut(b, V3(s.x0() + 0.03 + 0.08 * i, T + 0.025, 0.077), 2, 0.012, 0.006);
            });
            break;
        }
    }
}

// ---- Intakes ---------------------------------------------------------------------------------------
void inlineIntake(B& b, const Eng& e, const Inl& s) {
    const double T = s.top(), Z = s.halfHead;
    switch (s.intake) {
        case IN_SOHC: {
            const double py = s.deck + 0.05, plY = 0.125, plZ = 0.22;
            const V3 alu = grime(e, kCastAlu, 0.7);
            b.style(alu, METAL);
            b.box(V3(s.mid(), py, Z + 0.006), V3(0.37, 0.045, 0.012));  // port flange
            for (int i = 0; i < s.n; i++) {
                const double x = s.cx(i);
                b.tube(smooth({V3(x, py, Z), V3(x, py + 0.005, Z + 0.07), V3(x, py - 0.03, plZ - 0.005), V3(x * 0.95, plY + 0.03, plZ)}), 0.022, 6, false, false);
            }
            // Plenum low along the side, the throttle body on its front end.
            xlathe(b, V3(s.x0() + 0.02, plY, plZ), {V2(0, 0), V2(0.03, 0.003), V2(0.046, 0.025), V2(0.048, 0.04), V2(0.048, 0.34), V2(0, 0.352)}, 10);
            b.style(alu * 0.9, METAL);
            for (int i = 0; i < 3; i++) b.box(V3(s.mid() - 0.1 + 0.1 * i, plY - 0.04, plZ + 0.025), V3(0.012, 0.03, 0.03));
            const double tb = s.x0() + 0.37;
            b.style(alu, METAL);
            xcyl(b, tb, tb + 0.04, plY, plZ, 0.034, 12);
            b.box(V3(tb + 0.004, plY, plZ), V3(0.008, 0.08, 0.08));
            b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
            zcyl(b, tb + 0.022, plY, plZ + 0.034, plZ + 0.045, 0.028, 7);  // throttle drum
            // Fuel rail and injectors over the ports.
            b.style(kGold, METAL);
            xcyl(b, s.x0() + 0.02, s.xf - 0.03, py + 0.03, Z + 0.035, 0.009, 6);
            b.style(kBlack, MATTE);
            for (int i = 0; i < s.n; i++) b.cyl(V3(s.cx(i) + 0.012, py + 0.03, Z + 0.035), V3(s.cx(i) + 0.012, py + 0.005, Z + 0.025), 0.009, 5, false);
            const V3 tbEnd(tb + 0.04, plY, plZ);
            if (s.cone) {
                b.style(kChrome, CHROME);
                xcyl(b, tbEnd.x, tbEnd.x + 0.03, plY, plZ, 0.036, 12);
                coneFilter(b, tbEnd + V3(0.03, 0, 0), 0.062, 0.13, V3(0.55, 0.09, 0.07));
            } else {
                b.style(kRubber, MATTE);
                const std::vector<V3> hose = smooth({tbEnd, tbEnd + V3(0.05, 0, 0), tbEnd + V3(0.08, 0.05, 0.0), tbEnd + V3(0.09, 0.13, -0.01)}, 1);
                b.tube(hose, 0.036, 8);
                coupler(b, hose[3], hose[4] - hose[2], 0.039, 0.006);
                b.style(kBlack, MATTE);
                b.box(tbEnd + V3(0.09, 0.15, -0.01), V3(0.1, 0.03, 0.1), 0.01);  // the air box's snout
            }
            break;
        }
        case IN_VTEC: {
            const double py = s.deck + 0.06, plY = T + 0.075, plZ = 0.2;
            const V3 alu = grime(e, s.typeR ? V3(0.76, 0.77, 0.79) : kCastAlu, 0.7);
            const V3 run = grime(e, kCastAlu * 0.95, 0.7);
            b.style(run, METAL);
            b.box(V3(s.mid(), py, Z + 0.006), V3(0.37, 0.05, 0.012));
            for (int i = 0; i < s.n; i++) {
                const double x = s.cx(i);
                b.tube(smooth({V3(x, py, Z), V3(x, py, Z + 0.07), V3(x, plY - 0.05, plZ + 0.035), V3(x, plY - 0.01, plZ)}), 0.022, 5, false, false);
            }
            // The high plenum beside the cam cover, the throttle body on its front end.
            b.style(alu, METAL);
            xprism(b, rr(plZ, plY, 0.1, 0.075, 0.03), s.x0() + 0.015, s.xf - 0.04);
            b.box(V3(s.x0() + 0.02, plY, plZ), V3(0.012, 0.065, 0.09));
            const double tb = s.xf - 0.04;
            b.style(grime(e, kCastAlu, 0.7), METAL);
            b.box(V3(tb + 0.004, plY, plZ), V3(0.008, 0.08, 0.09));
            xcyl(b, tb, tb + 0.045, plY, plZ, 0.034, 12);
            b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
            zcyl(b, tb + 0.025, plY, plZ + 0.034, plZ + 0.045, 0.028, 7);
            b.style(kGold, METAL);
            xcyl(b, s.x0() + 0.02, s.xf - 0.03, py + 0.04, Z + 0.03, 0.009, 6);
            const V3 tbEnd(tb + 0.045, plY, plZ);
            b.style(kRubber, MATTE);
            const std::vector<V3> hose = smooth({tbEnd, tbEnd + V3(0.05, 0, 0), tbEnd + V3(0.09, -0.03, -0.01), tbEnd + V3(0.11, -0.09, -0.02)}, 1);
            b.tube(hose, 0.036, 8);
            coupler(b, hose[3], hose[4] - hose[2], 0.039, 0.006);
            break;
        }
        case IN_ECO: {
            // Plastic runners and plenum; throttle body in front; the ribbed cover over the lot.
            const double py = s.deck + 0.07, plY = s.deck + 0.03, plZ = 0.23;
            const V3 pl = grime(e, V3(0.1, 0.1, 0.11), 0.3);
            b.style(pl, MATTE);
            b.box(V3(s.mid(), py, Z + 0.006), V3(0.4, 0.05, 0.012));
            for (int i = 0; i < s.n; i++) {
                const double x = s.cx(i);
                b.tube(smooth({V3(x, py, Z), V3(x, py + 0.005, Z + 0.065), V3(x, plY + 0.03, plZ)}), 0.024, 6, false, false);
            }
            b.box(V3(s.mid() - 0.01, plY, plZ), V3(0.4, 0.1, 0.1), 0.03);
            const double tb = s.mid() + 0.19;
            b.style(grime(e, kCastAlu, 0.7), METAL);
            xcyl(b, tb, tb + 0.045, plY + 0.01, plZ, 0.036, 12);
            b.style(kBlack, MATTE);
            b.box(V3(tb + 0.022, plY + 0.06, plZ), V3(0.035, 0.035, 0.05));  // throttle motor
            // The engine cover, ribbed, lettered.
            const V3 cov = grime(e, s.cover, 0.4);
            const double x0 = s.x0() + 0.01, x1 = s.xf - 0.015;
            b.style(cov, MATTE);
            xprism(b, {V2(0.055, T + 0.03), V2(0.06, T + 0.07), V2(0.09, T + 0.083), V2(0.2, T + 0.083), V2(0.26, T + 0.06), V2(0.305, T + 0.0), V2(0.31, T - 0.07),
                       V2(0.28, T - 0.075), V2(0.25, T - 0.03), V2(0.16, T + 0.02), V2(0.08, T + 0.03)}, x0, x1);
            b.style(cov * 0.75 + V3(0.02, 0.02, 0.02), MATTE);
            for (int i = 0; i < 4; i++) {  // strakes down the outer slope
                b.push();
                b.move(s.mid(), T + 0.052 - 0.028 * i, 0.255 + 0.013 * i);
                b.roll(-35 - 8 * i);
                b.box(V3(0, 0, 0), V3(x1 - x0 - 0.04, 0.008, 0.012));
                b.pop();
            }
            b.box(V3(s.mid(), T + 0.0845, 0.2), V3(x1 - x0 - 0.02, 0.004, 0.012));
            b.style(V3(0.74, 0.75, 0.78), CHROME);
            sideText(b, "2.3", s.mid() - 0.09, T + 0.083, 0.14, 0.0105);
            sideText(b, "TURBO", s.mid() + 0.1, T + 0.083, 0.145, 0.0058);
            // Orange dipstick handle.
            b.style(V3(0.85, 0.42, 0.08), MATTE);
            b.tube({V3(s.mid() + 0.12, 0.0, s.halfSkirt + 0.01), V3(s.mid() + 0.13, 0.2, 0.15), V3(s.mid() + 0.14, T - 0.08, 0.17)}, 0.005, 4, false, false);
            b.box(V3(s.mid() + 0.14, T - 0.065, 0.17), V3(0.008, 0.022, 0.034));
            break;
        }
        case IN_TRIPLE: {
            const double py = s.deck + 0.045, plY = s.deck + 0.035, plZ = 0.16;
            const V3 alu = grime(e, V3(0.2, 0.2, 0.22), 0.4);
            b.style(alu, PAINT);
            b.box(V3(s.mid(), py, Z + 0.006), V3(s.n * s.pitch, 0.045, 0.012));
            for (int i = 0; i < s.n; i++) b.tube(smooth({V3(s.cx(i), py, Z), V3(s.cx(i), py + 0.005, Z + 0.04), V3(s.cx(i), plY, plZ)}), 0.022, 6, false, false);
            xlathe(b, V3(s.x0() + 0.02, plY, plZ), {V2(0, 0), V2(0.03, 0.003), V2(0.042, 0.02), V2(0.042, 0.24), V2(0.03, 0.255), V2(0, 0.258)}, 12);
            // Throttle body standing on the plenum, the charge pipe comes down into it.
            b.style(grime(e, kCastAlu, 0.7), METAL);
            b.cyl(V3(0.04, plY + 0.03, plZ), V3(0.04, plY + 0.075, plZ), 0.032, 12);
            b.style(kBlack, MATTE);
            b.box(V3(0.075, plY + 0.055, plZ), V3(0.03, 0.03, 0.04));
            b.style(kGold, METAL);
            xcyl(b, s.x0() + 0.02, s.xf - 0.03, py + 0.03, Z + 0.03, 0.008, 6);
            break;
        }
    }
}

// ---- Exhausts --------------------------------------------------------------------------------------
void inlineExhaust(B& b, const Eng& e, const Inl& s) {
    const double Z = -s.halfHead;
    switch (s.exhaust) {
        case EX_CAST: {  // cast iron manifold under a pressed heat shield
            const double py = s.deck + 0.05;
            const V3 iron = grime(e, kIron, 0.6);
            b.style(iron, METAL);
            b.box(V3(s.mid(), py, Z - 0.006), V3(0.37, 0.045, 0.012));
            const V3 C(s.mid(), 0.06, -0.18);
            xprism(b, {V2(Z, py + 0.022), V2(Z - 0.05, py + 0.01), V2(-0.17, py - 0.06), V2(-0.18, 0.08), V2(-0.15, 0.075), V2(Z, py - 0.03)}, s.mid() - 0.17, s.mid() + 0.17);
            for (int i = 0; i < s.n; i++) b.box(V3(s.cx(i), py, Z - 0.03), V3(0.05, 0.05, 0.05));  // the runners' necks
            b.cone(C + V3(0, 0.02, 0), C + V3(0, -0.11, 0.01), 0.036, 0.03, 10);
            b.box(C + V3(0, -0.115, 0.01), V3(0.08, 0.012, 0.08));
            b.style(grime(e, V3(0.36, 0.32, 0.28), 0.6), METAL);
            b.tube(smooth({C + V3(0, -0.12, 0.01), C + V3(-0.01, -0.2, 0.02), C + V3(-0.08, -0.25, 0.04)}), 0.028, 8);
            b.style(V3(0.2, 0.2, 0.22), METAL);
            zcyl(b, s.mid() + 0.03, 0.03, -0.205, -0.235, 0.008, 6);  // O2 sensor
            // Heat shield: an angled pressed plate with a top lip, louvres and stamped ribs.
            const V3 sh = grime(e, kShield, 0.5);
            b.style(sh, METAL);
            b.push();
            b.move(s.mid(), py - 0.04, -0.175);
            b.roll(12);
            b.box(V3(0, 0, 0), V3(0.37, 0.15, 0.006));
            b.style(sh * 0.85, METAL);
            for (int i = 0; i < 3; i++) b.box(V3(-0.14 + 0.14 * i, 0.01, -0.004), V3(0.012, 0.11, 0.004));
            b.style(V3(0.08, 0.08, 0.08), MATTE);
            for (int i = 0; i < 4; i++) b.box(V3(-0.105 + 0.07 * i, -0.04, -0.0035), V3(0.04, 0.008, 0.003));
            b.pop();
            b.style(sh, METAL);
            b.box(V3(s.mid(), py + 0.04, -0.14), V3(0.37, 0.006, 0.07));
            b.style(kBolt, METAL);
            for (int i = 0; i < 3; i++) nut(b, V3(s.mid() - 0.14 + 0.14 * i, py + 0.045, -0.13), 1, 0.014, 0.006);
            if (e.wear > 0.15) rustSpots(b, e.seed + 3, V3(s.mid(), py - 0.04, 0), V3(0.3, 0.1, 0), (int)(3 + 6 * e.wear), 2, -0.19);
            break;
        }
        case EX_4_1:
        case EX_4_2_1: {  // tubular headers
            const double py = s.deck + (s.exhaust == EX_4_2_1 ? 0.06 : 0.05);
            const V3 steel = grime(e, s.exhaust == EX_4_2_1 ? V3(0.6, 0.58, 0.56) : V3(0.66, 0.62, 0.56), 0.6);
            b.style(steel, METAL);
            b.box(V3(s.mid(), py, Z - 0.006), V3(0.37, 0.045, 0.012));
            const V3 C(s.mid() - 0.01, -0.04, -0.2);
            if (s.exhaust == EX_4_1) {
                for (int i = 0; i < s.n; i++) {
                    const double x = s.cx(i), sx = i < 2 ? -1 : 1, sz = (i % 2) ? 1 : -1;
                    b.tube(smooth({V3(x, py, Z), V3(x, py - 0.005, Z - 0.075), V3(s.mid() + (x - s.mid()) * 0.55, py - 0.1, -0.215), C + V3(sx * 0.016, 0.03, sz * 0.016)}), 0.018, 6, false, false);
                }
                b.cone(C + V3(0, 0.035, 0), C + V3(0, -0.11, 0.01), 0.044, 0.03, 10);
            } else {
                const V3 A(s.mid() - 0.07, 0.07, -0.22), Bm(s.mid() + 0.05, 0.08, -0.215);
                const int pairA[2] = {0, 3}, pairB[2] = {1, 2};
                for (int k = 0; k < 2; k++) {
                    const V3 M = k ? Bm : A;
                    for (int j = 0; j < 2; j++) {
                        const int i = k ? pairB[j] : pairA[j];
                        const double x = s.cx(i);
                        b.tube(smooth({V3(x, py, Z), V3(x, py - 0.005, Z - 0.075 - 0.02 * k), M + V3((j ? 1 : -1) * 0.014, 0.05, 0)}), 0.018, 6, false, false);
                    }
                    b.cone(M + V3(0, 0.055, 0), M + V3(0, -0.01, 0), 0.032, 0.026, 8);
                    b.tube(smooth({M, M + V3(0, -0.05, 0), C + V3(k ? 0.014 : -0.014, 0.04, 0)}), 0.025, 6, false, false);
                }
                b.cone(C + V3(0, 0.045, 0), C + V3(0, -0.1, 0.01), 0.042, 0.03, 10);
            }
            b.box(C + V3(0, -0.1, 0.01), V3(0.075, 0.012, 0.075));
            b.style(grime(e, V3(0.35, 0.3, 0.27), 0.6), METAL);
            b.tube(smooth({C + V3(0, -0.105, 0.01), C + V3(-0.01, -0.17, 0.02), C + V3(-0.08, -0.21, 0.04)}), 0.026, 8);
            b.style(V3(0.2, 0.2, 0.22), METAL);
            zcyl(b, C.x + 0.02, C.y - 0.02, C.z - 0.03, C.z - 0.06, 0.008, 6);
            if (e.wear > 0.15) rustSpots(b, e.seed + 3, V3(s.mid(), 0.0, 0), V3(0.25, 0.25, 0), (int)(2 + 5 * e.wear), 2, -0.235);
            break;
        }
        case EX_TURBO: {
            // The turbo hangs off the head (the manifold is cast into it), its charge pipe forward and down.
            const bool eco = s.headKind == HD_ECO;
            const double ts = eco ? 1.0 : (s.stage ? 1.18 : 0.95);
            const V3 c = eco ? V3(s.mid() + 0.0, s.deck + 0.02, -0.235) : V3(s.mid(), s.deck + 0.02, -0.2);
            b.style(grime(e, kIron, 0.6), METAL);
            const double zin = c.z + 0.06 * ts;  // the turbine scroll's inboard edge
            b.box(V3(c.x - 0.05 * ts, c.y + 0.015, (Z + zin) / 2), V3(0.12 * ts, 0.075, Z - zin + 0.02), 0.012);
            const TurboEnds t = turbo(b, e, c, ts, eco ? -1 : 1, true);
            // Oil feed and drain.
            b.style(V3(0.6, 0.48, 0.26), METAL);
            b.tube({V3(c.x + 0.005, c.y + 0.034 * ts, c.z), V3(c.x + 0.01, c.y + 0.07, c.z + 0.04), V3(c.x + 0.03, s.deck - 0.04, -s.halfDeck - 0.012)}, 0.004, 4, false, false);
            // Downpipe: back off the turbine, down past the block (forward of the flywheel).
            b.style(grime(e, V3(0.42, 0.4, 0.38), 0.7), METAL);
            const double dz = c.z + 0.01;
            b.tube(smooth({t.exhaust, V3(s.x0() + 0.035, c.y - 0.03, dz), V3(s.x0() + 0.05, s.rail, dz + 0.02), V3(s.x0() + 0.09, s.panBot - 0.02, dz + 0.04)}), 0.032 * ts, 7);
            b.style(grime(e, V3(0.55, 0.55, 0.57), 0.6), METAL);
            for (int i = 0; i < 3; i++) coupler(b, V3(s.x0() + 0.05, s.rail + 0.04 - 0.025 * i, dz + 0.015), V3(0, 1, 0), 0.036 * ts, 0.008);  // flex section
            if (eco) {
                // Intake hose up and forward to the air box.
                b.style(kRubber, MATTE);
                const std::vector<V3> in = smooth({t.inlet, t.inlet + V3(0.06, 0, 0), V3(s.xf + 0.03, c.y + 0.06, c.z + 0.02), V3(s.xf + 0.07, s.top() + 0.04, c.z + 0.03)}, 1);
                b.tube(in, 0.04, 7);
                coupler(b, in[3], in[4] - in[2], 0.043, 0.006);
                // Charge pipe down and forward to the (front-mounted) intercooler, and back up into the throttle body.
                const V3 alu = grime(e, V3(0.6, 0.61, 0.63), 0.6);
                b.style(alu, METAL);
                const std::vector<V3> ch = smooth({t.outlet, t.outlet + V3(0.01, -0.06, -0.03), V3(c.x + 0.16, -0.02, -0.29), V3(s.xf + 0.08, -0.09, -0.25), V3(s.xf + 0.16, -0.11, -0.2)}, 1);
                b.tube(ch, 0.028, 7);
                const std::vector<V3> ret = smooth({V3(s.xf + 0.16, -0.11, 0.21), V3(s.xf + 0.12, 0.05, 0.25), V3(s.xf + 0.07, s.deck + 0.04, 0.24), V3(s.mid() + 0.235, s.deck + 0.04, 0.23)}, 1);
                b.tube(ret, 0.03, 7);
                b.style(kRubber, MATTE);
                coupler(b, ch.back(), ch.back() - ch[ch.size() - 3], 0.034, 0.04);
                coupler(b, ret.front(), ret[2] - ret.front(), 0.036, 0.04);
                coupler(b, ret[ret.size() - 2], ret.back() - ret[ret.size() - 3], 0.036, 0.03);
                b.style(kBolt, METAL);
                coupler(b, ch[4], ch[5] - ch[3], 0.032, 0.008);
            } else {
                // The triple: the charge pipe up and over the cam cover into the throttle body on the intake side.
                const double r = s.stage ? 0.035 : 0.027, T = s.top();
                const V3 tb(0.04, s.deck + 0.11, 0.16);
                b.style(grime(e, s.stage ? V3(0.12, 0.12, 0.13) : V3(0.58, 0.59, 0.61), 0.6), s.stage ? PAINT : METAL);
                const std::vector<V3> ch = smooth({t.outlet, t.outlet + V3(0.0, 0.03, 0.0), V3(0.042, T + 0.075, -0.1), V3(0.042, T + 0.075, 0.1), V3(0.04, tb.y + 0.04, tb.z),
                                                   tb + V3(0, 0.005, 0)}, 1);
                b.tube(ch, r, 10);
                b.style(s.stage ? V3(0.12, 0.3, 0.6) : kRubber, MATTE);
                coupler(b, ch[2], ch[3] - ch[1], r + 0.004, 0.04);
                coupler(b, ch[ch.size() - 3], ch[ch.size() - 2] - ch[ch.size() - 4], r + 0.004, 0.035);
                b.style(kBolt, METAL);
                coupler(b, ch[8], ch[9] - ch[7], r + 0.003, 0.008);
                // Blow-off valve on the pipe.
                b.style(grime(e, kCastAlu, 0.6), METAL);
                b.cyl(ch[9] + V3(0.0, 0.0, 0.0), ch[9] + V3(0.04, 0.02, 0), 0.016, 8);
                if (s.stage) {
                    b.style(kChrome, CHROME);
                    xcyl(b, t.inlet.x, t.inlet.x + 0.02, t.inlet.y, t.inlet.z, 0.036 * ts, 12);
                    coneFilter(b, t.inlet + V3(0.02, 0, 0), 0.055, 0.12, V3(0.1, 0.1, 0.11));
                } else {
                    b.style(kRubber, MATTE);
                    const std::vector<V3> in = smooth({t.inlet, t.inlet + V3(0.04, 0, 0), V3(s.xf + 0.03, c.y + 0.04, c.z + 0.02), V3(s.xf + 0.05, T + 0.02, c.z + 0.04)}, 1);
                    b.tube(in, 0.034, 8);
                    for (int i = 1; i < 6; i += 2) coupler(b, in[i], in[i + 1] - in[i - 1], 0.037, 0.006);
                }
            }
            if (e.wear > 0.15) rustSpots(b, e.seed + 3, V3(c.x - 0.06, c.y, 0), V3(0.06, 0.08, 0), (int)(2 + 4 * e.wear), 2, c.z - 0.07);
            break;
        }
    }
}

// Starter, oil filter, dipstick, mounts, and the triple's oil tank.
void inlineExtras(B& b, const Eng& e, const Inl& s) {
    // Starter at the flywheel, intake side.
    const double sz = s.halfSkirt + 0.045;
    b.style(grime(e, V3(0.32, 0.32, 0.33), 0.7), METAL);
    xcyl(b, s.x0(), s.x0() + 0.11, -0.04, sz, 0.042, 10);
    b.style(grime(e, V3(0.1, 0.1, 0.11), 0.4), PAINT);
    xcyl(b, s.x0() + 0.11, s.x0() + 0.135, -0.04, sz, 0.032, 10);
    b.box(V3(s.x0() + 0.06, 0.0, sz), V3(0.07, 0.032, 0.036));  // solenoid
    // Oil filter.
    if (s.headKind != HD_TRIPLE) {
        const bool honda = s.front == FR_BELT;
        b.style(grime(e, honda ? V3(0.08, 0.16, 0.4) : V3(0.12, 0.12, 0.13), 0.5), PAINT);
        zcyl(b, s.mid() + 0.0, -0.03, s.halfSkirt, s.halfSkirt + 0.09, 0.04, 10);
        if (honda) {  // yellow dipstick loop
            b.style(V3(0.85, 0.7, 0.1), MATTE);
            b.tube({V3(s.mid() + 0.09, 0.0, s.halfSkirt + 0.005), V3(s.mid() + 0.1, 0.15, 0.12), V3(s.mid() + 0.11, s.deck + 0.04, 0.125)}, 0.005, 4, false, false);
            b.box(V3(s.mid() + 0.11, s.deck + 0.05, 0.125), V3(0.006, 0.02, 0.03));
        }
    } else if (s.tank) {
        // Dry-sump oil tank low on the intake side, braided lines into the block.
        b.style(grime(e, V3(0.14, 0.14, 0.15), 0.5), PAINT);
        b.lathe(V3(s.xf - 0.06, s.panBot + 0.01, s.halfSkirt + 0.06), {V2(0, 0), V2(0.04, 0), V2(0.046, 0.012), V2(0.046, 0.2), V2(0.04, 0.212), V2(0, 0.212)}, 12);
        b.style(V3(0.85, 0.62, 0.1), MATTE);
        b.cyl(V3(s.xf - 0.06, s.panBot + 0.22, s.halfSkirt + 0.06), V3(s.xf - 0.06, s.panBot + 0.24, s.halfSkirt + 0.06), 0.02, 10);
        b.style(grime(e, V3(0.5, 0.5, 0.52), 0.6), METAL);
        b.tube(smooth({V3(s.xf - 0.06, s.panBot + 0.01, s.halfSkirt + 0.06), V3(s.xf - 0.07, s.panBot - 0.0, s.halfSkirt), V3(s.xf - 0.1, s.panBot + 0.03, s.panW / 2)}), 0.007, 5);
        b.tube(smooth({V3(s.xf - 0.06, s.panBot + 0.18, s.halfSkirt + 0.105), V3(s.xf - 0.03, s.panBot + 0.2, s.halfSkirt + 0.06), V3(s.xf + 0.01, s.panBot + 0.16, s.halfSkirt - 0.02)}), 0.007, 5);
        b.style(grime(e, V3(0.3, 0.3, 0.31), 0.5), METAL);
        for (int k = 0; k < 2; k++) b.box(V3(s.xf - 0.06, s.panBot + 0.06 + 0.1 * k, s.halfSkirt + 0.012), V3(0.02, 0.02, 0.03));
    }
    // Engine mount brackets.
    b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), PAINT);
    b.box(V3(s.mid() + 0.06, s.deck - 0.07, s.halfDeck + 0.022), V3(0.07, 0.06, 0.02));
}

void inlineEngine(B& b, const Eng& e, const Inl& s) {
    inlineBlock(b, e, s);
    inlineHead(b, e, s);
    inlineFront(b, e, s);
    inlineIntake(b, e, s);
    inlineExhaust(b, e, s);
    inlineExtras(b, e, s);
}

// ---- crate_engine_i4 designs ---------------------------------------------------------------------
Inl hondaSpec(bool dohc) {
    Inl s;
    s.n = 4, s.pitch = 0.09, s.fx = -0.24, s.xf = 0.21;
    s.deck = dohc ? 0.212 : 0.2;
    s.headH = dohc ? 0.12 : 0.1;
    s.halfHead = dohc ? 0.105 : 0.1;
    s.front = FR_BELT;
    return s;
}
Show hondaShow(double top, double fx) {
    Show sh;
    sh.fx = fx;
    sh.standH = 0.46;
    sh.strapX = -0.17;
    sh.strap = {V2(0.2, -0.16), V2(0.3, -0.02), V2(0.3, top - 0.12), V2(0.2, top + 0.02), V2(0.07, top + 0.03)};
    sh.skidLen = 0.7, sh.groundLen = 0.66;
    return sh;
}

void hondaSohc(B& b, const P& p) {
    static const V3 covers[] = {V3(0.09, 0.09, 0.1), V3(0.6, 0.61, 0.63), V3(0.1, 0.18, 0.42)};
    Inl s = hondaSpec(false);
    s.headKind = HD_SOHC, s.intake = IN_SOHC;
    s.cover = covers[std::clamp(p.i("cover"), 0, 2)];
    const bool built = p.i("header") == 1;
    s.exhaust = built ? EX_4_1 : EX_CAST;
    s.cone = built;
    const Eng e = optsFor(p, wearOf(p));
    present(b, p, hondaShow(0.36, s.fx), [&] { inlineEngine(b, e, s); });
}
void hondaVtec(B& b, const P& p) {
    Inl s = hondaSpec(true);
    s.headKind = HD_VTEC, s.intake = IN_VTEC, s.exhaust = EX_4_2_1;
    s.typeR = p.i("typer") == 1;
    s.cover = s.typeR ? V3(0.6, 0.07, 0.06) : V3(0.07, 0.07, 0.075);
    const Eng e = optsFor(p, wearOf(p));
    present(b, p, hondaShow(0.43, s.fx), [&] { inlineEngine(b, e, s); });
}
void ecoFour(B& b, const P& p) {
    static const V3 covers[] = {V3(0.07, 0.07, 0.075), V3(0.2, 0.205, 0.22), V3(0.55, 0.56, 0.58)};
    Inl s;
    s.n = 4, s.pitch = 0.094, s.fx = -0.25, s.xf = 0.22;
    s.deck = 0.235, s.halfDeck = 0.11, s.halfSkirt = 0.14, s.rail = -0.08, s.panBot = -0.215, s.panW = 0.25;
    s.headH = 0.13, s.halfHead = 0.12, s.flyR = 0.14;
    s.block = V3(0.57, 0.58, 0.59);
    s.pan = kCastAlu, s.panMat = METAL;
    s.headKind = HD_ECO, s.intake = IN_ECO, s.exhaust = EX_TURBO, s.front = FR_CHAIN;
    s.cover = covers[std::clamp(p.i("cover"), 0, 2)];
    const Eng e = optsFor(p, wearOf(p));
    Show sh;
    sh.fx = s.fx, sh.standH = 0.5;
    sh.strap = {V2(0.2, -0.16), V2(0.34, -0.02), V2(0.34, 0.35), V2(0.24, 0.5), V2(0.08, 0.51)};
    sh.strapX = 0.2, sh.skidLen = 0.8, sh.groundLen = 0.7;
    present(b, p, sh, [&] { inlineEngine(b, e, s); });
}

// ---- crate_engine_i3 -----------------------------------------------------------------------------
void turboTriple(B& b, const P& p) {
    Inl s;
    s.n = 3, s.pitch = 0.085, s.fx = -0.18, s.xf = 0.15;
    s.deck = 0.16, s.halfDeck = 0.085, s.halfSkirt = 0.105, s.rail = -0.055, s.panBot = -0.11, s.panW = 0.19;
    s.headH = 0.09, s.halfHead = 0.09, s.flyR = 0.115;
    s.block = V3(0.25, 0.26, 0.28), s.blockMat = PAINT;
    s.head = V3(0.34, 0.35, 0.37), s.headMat = METAL;
    s.pan = V3(0.3, 0.31, 0.33), s.panMat = METAL;
    s.cover = V3(0.07, 0.07, 0.075);
    s.headKind = HD_TRIPLE, s.intake = IN_TRIPLE, s.exhaust = EX_TURBO, s.front = FR_TRIPLE;
    s.stage = p.i("stage");
    s.tank = p.i("tank") == 1;
    const Eng e = optsFor(p, wearOf(p));
    Show sh;
    sh.fx = s.fx, sh.standH = 0.42;
    sh.strap = {V2(0.2, -0.16), V2(0.3, -0.03), V2(0.3, 0.2), V2(0.2, 0.36), V2(0.07, 0.37)};
    sh.strapX = -0.07, sh.skidLen = 0.6, sh.groundLen = 0.55;
    present(b, p, sh, [&] { inlineEngine(b, e, s); });
}

// ---- crate_engine_flat4 ---------------------------------------------------------------------------
// The air-cooled VW Type 1: crank on the origin, +X the pulley and fan end (in a Beetle it faces the back bumper),
// -X the flywheel flange. A short magnesium crankcase, finned barrels two a side, finned heads under bail-held rocker
// covers, the tin fan shroud over the top with the generator / alternator and the round fan housing, the carbs on the
// heads (or one in the middle), the exhaust under the heads.
enum F4Carbs { F4C_DUAL, F4C_SINGLE, F4C_IDF };
enum F4Exhaust { F4X_BAJA, F4X_STOCK, F4X_STINGER };
struct F4 {
    F4Carbs carbs = F4C_DUAL;
    F4Exhaust exhaust = F4X_BAJA;
    double headZ = 0.325, barrelR = 0.048;
    bool cooler = false, alternator = true;
};
constexpr double F4FX = -0.20;

void f4Core(B& b, const Eng& e, const F4& f) {
    const V3 mag = grime(e, V3(0.58, 0.58, 0.55), 0.9), alu = grime(e, V3(0.66, 0.66, 0.64), 0.9);
    const V3 tin = grime(e, e.paint, 0.6), black = V3(0.05, 0.05, 0.055);
    const double hz = f.headZ;
    // Crankcase: split down the middle (a dark seam on top), its oil strainer plate underneath.
    b.style(mag, METAL);
    xprism(b, symm({V2(0.08, -0.14), V2(0.13, -0.09), V2(0.135, 0.07), V2(0.1, 0.12), V2(0, 0.125)}), F4FX + 0.03, 0.17);
    b.style(V3(0.2, 0.2, 0.2), METAL);
    b.box(V3(-0.01, 0.1265, 0), V3(0.33, 0.004, 0.006));
    b.style(alu, METAL);
    b.box(V3(0.02, -0.145, 0), V3(0.14, 0.012, 0.14));
    b.style(kBolt, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) nut(b, V3(0.02 + sx * 0.055, -0.153, sz * 0.055), 1, 0.012, 0.005);
    rearFace(b, e, F4FX, 0.0, 0.3, 0.28, 0.135, mag, METAL);
    // Oil pump cover at the pulley end.
    b.style(mag, METAL);
    xcyl(b, 0.17, 0.19, -0.03, 0, 0.05, 10);
    // Barrels: finned, two each side.
    for (int sd = -1; sd <= 1; sd += 2)
        for (int c = -1; c <= 1; c += 2) {
            const double x = c * 0.085, z0 = sd * 0.135, z1 = sd * (hz - 0.035);
            b.style(V3(0.16, 0.16, 0.17), METAL);
            zcyl(b, x, 0.0, z0, z1, f.barrelR, 8, false);
            b.style(grime(e, V3(0.24, 0.24, 0.25), 0.5), METAL);
            for (int k = 0; k < 3; k++) {
                const double z = z0 + (z1 - z0) * (0.2 + 0.3 * k);
                zcyl(b, x, 0.0, z - 0.005, z + 0.005, f.barrelR + 0.016, 8);
            }
        }
    // Heads, finned; rocker covers held by their bails.
    for (int sd = -1; sd <= 1; sd += 2) {
        b.style(alu, METAL);
        b.box(V3(0, 0.0, sd * hz), V3(0.34, 0.15, 0.07), 0.01);
        for (int k = 0; k < 3; k++) b.box(V3(0, -0.05 + 0.05 * k, sd * (hz - 0.004)), V3(0.355, 0.007, 0.075));
        b.style(e.chrome ? kChrome : grime(e, black, 0.3), e.chrome ? CHROME : PAINT);
        b.box(V3(0, -0.005, sd * (hz + 0.055)), V3(0.3, 0.1, 0.04), 0.014);
        b.style(grime(e, V3(0.5, 0.47, 0.44), 0.6), METAL);
        b.box(V3(0, -0.005, sd * (hz + 0.078)), V3(0.012, 0.11, 0.006));
        // Lower cylinder tins (deflector plates under the barrels).
        b.style(grime(e, black, 0.4), PAINT);
        b.box(V3(0, -0.07, sd * (0.135 + hz - 0.035) / 2), V3(0.3, 0.005, hz - 0.17));
    }
    // The fan shroud over the barrels, the round fan housing at the pulley end, the oil cooler's doghouse hump.
    b.style(tin, PAINT);
    xprism(b, symm({V2(hz + 0.01, 0.02), V2(hz + 0.01, 0.09), V2(hz - 0.05, 0.14), V2(0.17, 0.172), V2(0.09, 0.19), V2(0, 0.192)}), -0.16, 0.16);
    b.style(tin * 0.92, PAINT);
    xlathe(b, V3(0.05, 0.235, 0), {V2(0, 0), V2(0.13, 0), V2(0.145, 0.02), V2(0.145, 0.125), V2(0.13, 0.14), V2(0, 0.14)}, 14);
    b.style(tin, PAINT);
    b.box(V3(-0.03, 0.235, -0.12), V3(0.12, 0.1, 0.09), 0.02);  // doghouse hump over the oil cooler
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    xcyl(b, 0.19, 0.192, 0.235, 0, 0.11, 12, true);  // the fan's inlet, dark
    // Generator (stock) or alternator on its stand through the fan housing.
    const double gx0 = f.carbs == F4C_SINGLE ? -0.06 : -0.11;
    b.style(grime(e, f.alternator ? V3(0.62, 0.62, 0.6) : V3(0.12, 0.12, 0.13), 0.5), f.alternator ? METAL : PAINT);
    xcyl(b, gx0, 0.195, 0.235, 0, 0.056, 12);
    if (f.alternator) {
        b.style(grime(e, V3(0.32, 0.32, 0.34), 0.5), METAL);
        for (int i = 0; i < 2; i++) xcyl(b, gx0 + 0.02 + 0.04 * i, gx0 + 0.03 + 0.04 * i, 0.235, 0, 0.059, 12, false);
    }
    b.style(grime(e, V3(0.1, 0.1, 0.11), 0.4), PAINT);
    b.box(V3(gx0 + 0.07, 0.19, 0), V3(0.09, 0.03, 0.1));  // stand
    b.box(V3(gx0 + 0.07, 0.212, 0), V3(0.025, 0.04, 0.125));  // strap
    b.style(V3(0.12, 0.12, 0.13), MATTE);
    b.cyl(V3(gx0 + 0.13, 0.19, 0.08), V3(gx0 + 0.13, 0.225, 0.08), 0.022, 8);  // oil filler
    // Crank and generator pulleys on one belt.
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
    pulleyX(b, V3(0.19, 0, 0), 0.085, 0.03, 12);
    pulleyX(b, V3(0.195, 0.235, 0), 0.05, 0.024, 10);
    beltX(b, 0.207, {{0, 0, 0.085}, {0, 0.235, 0.05}}, 0.016, V3(0.04, 0.04, 0.04));
    // Distributor (left, behind the stand), coil on the shroud, plug leads to the heads.
    b.style(black, MATTE);
    b.cyl(V3(-0.13, 0.12, 0.075), V3(-0.13, 0.2, 0.075), 0.022, 8);
    b.lathe(V3(-0.13, 0.2, 0.075), {V2(0, 0), V2(0.034, 0), V2(0.036, 0.025), V2(0.02, 0.038), V2(0, 0.038)}, 10);
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.3), PAINT);
    b.cyl(V3(-0.13, 0.205, 0.17), V3(-0.03, 0.205, 0.17), 0.026, 10);
    b.style(kWire, MATTE);
    for (int sd = -1; sd <= 1; sd += 2)
        for (int c = -1; c <= 1; c += 2)
            b.tube({V3(-0.13 + c * 0.012, 0.236, 0.075 + sd * 0.012), V3(-0.12 + c * 0.04, 0.26, 0.075 + sd * 0.08), V3(c * 0.085, 0.16, sd * (hz - 0.06)), V3(c * 0.085, 0.07, sd * (hz + 0.012))}, 0.005, 4, false, false);
}

void f4Carbs(B& b, const Eng& e, const F4& f) {
    const V3 alu = grime(e, V3(0.66, 0.66, 0.64), 0.8), zinc = grime(e, V3(0.6, 0.57, 0.48), 0.8);
    const double hz = f.headZ;
    switch (f.carbs) {
        case F4C_DUAL: {
            for (int sd = -1; sd <= 1; sd += 2) {
                const double z = sd * (hz - 0.03);
                b.style(alu, METAL);
                for (int c = -1; c <= 1; c += 2) b.tube({V3(c * 0.085, 0.07, sd * hz), V3(c * 0.065, 0.15, z), V3(c * 0.02, 0.2, z)}, 0.018, 6, false, false);
                b.cyl(V3(0, 0.18, z), V3(0, 0.23, z), 0.028, 10);
                b.style(zinc, METAL);
                b.box(V3(0, 0.255, z), V3(0.09, 0.06, 0.075), 0.01);
                b.box(V3(0.04, 0.24, z - sd * 0.03), V3(0.04, 0.05, 0.03));  // float bowl
                b.style(e.chrome ? kChrome : grime(e, V3(0.1, 0.1, 0.11), 0.3), e.chrome ? CHROME : PAINT);
                b.lathe(V3(0, 0.285, z), {V2(0, 0), V2(0.07, 0), V2(0.085, 0.01), V2(0.087, 0.05), V2(0.07, 0.062), V2(0, 0.064)}, 14);
                b.style(V3(0.06, 0.06, 0.065), MATTE);
                b.cyl(V3(0, 0.287, z), V3(0, 0.297, z), 0.0875, 14, false);
                b.style(kChrome, CHROME);
                b.cyl(V3(0, 0.349, z), V3(0, 0.357, z), 0.014, 6);  // wing nut
            }
            // Cross-bar linkage behind the stand.
            b.style(kBolt, METAL);
            b.cyl(V3(-0.135, 0.215, -(hz - 0.05)), V3(-0.135, 0.215, hz - 0.05), 0.006, 6);
            b.box(V3(-0.135, 0.205, 0), V3(0.02, 0.03, 0.02));
            b.both([&] { b.tube({V3(-0.135, 0.215, hz - 0.05), V3(-0.04, 0.25, hz - 0.03)}, 0.004, 4); });
            break;
        }
        case F4C_SINGLE: {
            const double cx = -0.105, cz = -0.12;
            b.style(alu, METAL);
            // The manifold: up from each head and across behind the stand, the riser up to the carb.
            b.both([&] {
                for (int c = -1; c <= 1; c += 2) b.tube({V3(c * 0.085, 0.07, hz), V3(c * 0.06, 0.15, hz - 0.03), V3(cx + 0.01 * c, 0.19, hz - 0.08)}, 0.016, 6, false, false);
            });
            b.tube({V3(cx, 0.19, -(hz - 0.08)), V3(cx, 0.19, hz - 0.08)}, 0.018, 8);
            b.cyl(V3(cx, 0.19, cz), V3(cx, 0.245, cz), 0.022, 8);
            b.style(zinc, METAL);
            b.box(V3(cx, 0.27, cz), V3(0.07, 0.055, 0.065), 0.01);
            b.box(V3(cx + 0.03, 0.26, cz - 0.035), V3(0.035, 0.04, 0.025));
            // Oil-bath air cleaner and its snorkel.
            const V3 ac = e.chrome ? kChrome : grime(e, V3(0.1, 0.1, 0.11), 0.3);
            b.style(ac, e.chrome ? CHROME : PAINT);
            b.lathe(V3(cx, 0.297, cz + 0.02), {V2(0, 0), V2(0.07, 0), V2(0.085, 0.015), V2(0.088, 0.075), V2(0.07, 0.093), V2(0, 0.097)}, 14);
            b.style(grime(e, V3(0.12, 0.12, 0.13), 0.3), PAINT);
            b.tube(smooth({V3(cx + 0.06, 0.35, cz - 0.04), V3(cx + 0.11, 0.35, cz - 0.08), V3(cx + 0.15, 0.33, cz - 0.1)}), 0.024, 8);
            b.style(kChrome, CHROME);
            b.cyl(V3(cx, 0.393, cz + 0.02), V3(cx, 0.402, cz + 0.02), 0.014, 6);
            b.style(kBolt, METAL);
            b.tube({V3(cx + 0.035, 0.26, cz), V3(cx + 0.06, 0.2, cz + 0.04)}, 0.004, 4);
            break;
        }
        case F4C_IDF: {
            for (int sd = -1; sd <= 1; sd += 2) {
                const double z = sd * (hz - 0.035);
                b.style(alu, METAL);
                for (int c = -1; c <= 1; c += 2) b.tube({V3(c * 0.085, 0.07, sd * hz), V3(c * 0.065, 0.15, z), V3(c * 0.045, 0.21, z)}, 0.022, 6, false, false);
                b.style(zinc, METAL);
                b.box(V3(0, 0.245, z), V3(0.16, 0.08, 0.08), 0.012);
                b.box(V3(0, 0.235, z + sd * 0.052), V3(0.07, 0.05, 0.03));  // float bowl, outboard
                b.style(zinc * 1.08, METAL);
                b.box(V3(0, 0.288, z), V3(0.15, 0.008, 0.075));  // top cover
                for (int c = -1; c <= 1; c += 2) {
                    const double x = c * 0.042;
                    b.style(e.chrome ? kChrome : grime(e, V3(0.62, 0.63, 0.66), 0.5), e.chrome ? CHROME : METAL);
                    b.lathe(V3(x, 0.29, z), {V2(0.026, 0), V2(0.028, 0.03), V2(0.04, 0.052), V2(0.03, 0.05), V2(0.022, 0.0)}, 8);
                    b.style(V3(0.03, 0.03, 0.035), MATTE);
                    b.cyl(V3(x, 0.291, z), V3(x, 0.293, z), 0.022, 8, false);
                }
            }
            // Hex-bar linkage across behind the stand, a bellcrank in the middle.
            b.style(kChrome, CHROME);
            b.beam(V3(-0.13, 0.24, -(hz - 0.03)), V3(-0.13, 0.24, hz - 0.03), 0.014, 0.014, V3(0, 1, 0), 0.004);
            b.style(kBolt, METAL);
            b.box(V3(-0.13, 0.215, 0), V3(0.03, 0.05, 0.02));
            b.both([&] { b.box(V3(-0.1, 0.24, hz - 0.035), V3(0.06, 0.012, 0.012)); });
            break;
        }
    }
}

void f4Exhaust(B& b, const Eng& e, const F4& f) {
    const double hz = f.headZ;
    const V3 steel = grime(e, V3(0.5, 0.47, 0.44), 0.6);
    switch (f.exhaust) {
        case F4X_BAJA: {
            b.style(steel, METAL);
            const V3 C(0.21, -0.2, 0);
            for (int sd = -1; sd <= 1; sd += 2)
                for (int c = -1; c <= 1; c += 2) {
                    const double x = c * 0.085;
                    b.tube(smooth({V3(x, -0.07, sd * (hz - 0.01)), V3(x, -0.14, sd * (hz - 0.02)), V3(x * 0.5 + 0.08, -0.19, sd * 0.2), V3(C.x - 0.02, C.y + c * 0.012, sd * 0.03)}), 0.018, 6, false, false);
                }
            b.cone(C + V3(-0.03, 0, 0), C + V3(0.06, 0, 0), 0.05, 0.035, 10);
            // Round quiet-pack muffler and tip.
            b.style(grime(e, V3(0.62, 0.62, 0.64), 0.6), METAL);
            xlathe(b, C + V3(0.06, 0, 0), {V2(0, 0), V2(0.035, 0), V2(0.055, 0.02), V2(0.055, 0.14), V2(0.035, 0.16), V2(0, 0.16)}, 12);
            b.style(kChrome, CHROME);
            xcyl(b, C.x + 0.2, C.x + 0.26, C.y - 0.005, 0, 0.022, 10);
            if (e.wear > 0.15) rustSpots(b, e.seed + 5, C + V3(0.13, 0, 0), V3(0.12, 0.06, 0), (int)(2 + 5 * e.wear), 2, 0.056);
            break;
        }
        case F4X_STOCK: {
            // J-tubes into the heater boxes under the heads, then forward into the box muffler across the pulley end.
            const V3 hb = grime(e, V3(0.3, 0.3, 0.31), 0.7);
            for (int sd = -1; sd <= 1; sd += 2) {
                const double z = sd * (hz - 0.06);
                b.style(steel, METAL);
                for (int c = -1; c <= 1; c += 2) b.tube(smooth({V3(c * 0.085, -0.07, sd * (hz - 0.01)), V3(c * 0.085, -0.12, sd * (hz - 0.03)), V3(c * 0.1, -0.15, z)}), 0.018, 6, false, false);
                b.style(hb, PAINT);
                xcyl(b, -0.15, 0.17, -0.165, z, 0.04, 10);
                b.style(hb * 0.7, PAINT);
                for (int k = 0; k < 3; k++) xcyl(b, -0.08 + 0.08 * k, -0.072 + 0.08 * k, -0.165, z, 0.046, 10);
                b.style(steel, METAL);
                b.tube(smooth({V3(0.17, -0.165, z), V3(0.22, -0.16, z * 0.85)}), 0.022, 7);
            }
            const V3 m(0.28, -0.13, 0);
            b.style(grime(e, V3(0.26, 0.26, 0.27), 0.7), PAINT);
            b.box(m, V3(0.1, 0.14, 2 * hz - 0.08), 0.035);
            b.style(grime(e, V3(0.2, 0.2, 0.21), 0.7), PAINT);
            b.box(m + V3(0, 0.0, 0), V3(0.104, 0.02, 2 * hz - 0.1));
            b.style(steel, METAL);
            for (int sd = -1; sd <= 1; sd += 2) xcyl(b, m.x + 0.04, m.x + 0.16, m.y - 0.035, sd * 0.11, 0.017, 8);
            if (e.wear > 0.15) rustSpots(b, e.seed + 5, m, V3(0, 0.1, 0.5), (int)(3 + 7 * e.wear), 0, m.x + 0.052);
            break;
        }
        case F4X_STINGER: {
            const V3 hs = grime(e, V3(0.62, 0.6, 0.58), 0.6);
            b.style(hs, METAL);
            const V3 C(0.12, -0.215, 0);
            for (int sd = -1; sd <= 1; sd += 2)
                for (int c = -1; c <= 1; c += 2) {
                    const double x = c * 0.085;
                    b.tube(smooth({V3(x, -0.07, sd * (hz - 0.01)), V3(x, -0.17, sd * (hz - 0.03)), V3(C.x - 0.05, C.y + c * 0.016, sd * 0.03)}), 0.022, 6, false, false);
                }
            xlathe(b, C + V3(-0.06, 0, 0), {V2(0, 0), V2(0.058, 0), V2(0.058, 0.03), V2(0.038, 0.1), V2(0, 0.1)}, 12);  // the merge collector
            // The stinger: long, rising out past the pulley end, a flared tip.
            const V3 t0 = C + V3(0.04, 0, 0), t1 = V3(0.5, -0.06, 0);
            b.tube(smooth({t0, t0 + V3(0.08, 0, 0), t1 - (t1 - t0) * 0.5, t1}), 0.036, 8);
            b.style(kChrome, CHROME);
            const V3 dir = norm(t1 - (t0 + (t1 - t0) * 0.5));
            b.cone(t1 - dir * 0.01, t1 + dir * 0.05, 0.039, 0.05, 12, false);
            break;
        }
    }
}

void f4Cooler(B& b, const Eng& e, const F4& f) {
    if (!f.cooler) return;
    // Remote oil cooler standing on the right of the shroud, a spin-on filter on the left, braided lines down to the case.
    const V3 cc(-0.01, 0.27, -0.215);
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.4), PAINT);
    b.box(cc, V3(0.17, 0.13, 0.028));
    b.style(grime(e, V3(0.45, 0.45, 0.47), 0.4), METAL);
    for (int i = 0; i < 5; i++) b.box(cc + V3(0, -0.048 + 0.024 * i, -0.0155), V3(0.15, 0.006, 0.003));
    b.style(grime(e, V3(0.62, 0.62, 0.64), 0.4), METAL);
    for (int k = -1; k <= 1; k += 2) b.box(cc + V3(k * 0.09, 0, 0), V3(0.014, 0.14, 0.036));
    b.style(grime(e, V3(0.12, 0.12, 0.13), 0.4), PAINT);
    b.box(V3(0.04, 0.23, 0.215), V3(0.05, 0.035, 0.05));
    b.style(V3(0.85, 0.42, 0.08), PAINT);
    xcyl(b, 0.065, 0.165, 0.23, 0.215, 0.042, 12);
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.tube({cc + V3(-0.08, -0.06, 0), V3(-0.1, 0.12, -0.17), V3(-0.06, 0.2, 0.0), V3(0.03, 0.23, 0.19)}, 0.007, 4, false, false);
    b.tube({cc + V3(0.08, -0.06, 0), V3(0.13, 0.12, -0.15), V3(0.175, 0.0, -0.06), V3(0.18, -0.04, -0.02)}, 0.007, 4, false, false);
}

void flat4(B& b, const Eng& e, const F4& f) {
    f4Core(b, e, f);
    f4Carbs(b, e, f);
    f4Exhaust(b, e, f);
    f4Cooler(b, e, f);
}

void f4Design(B& b, const P& p, const F4& f) {
    Eng e = optsFor(p, wearOf(p));
    e.chrome = p.i("chrome") != 0;
    Show sh;
    sh.fx = F4FX, sh.standH = 0.5;
    sh.strapX = -0.06;
    sh.strap = {V2(0.42, -0.14), V2(0.45, 0.05), V2(0.4, 0.22), V2(0.25, 0.27), V2(0.08, 0.28)};
    sh.skidLen = 0.75, sh.groundLen = 0.65;
    present(b, p, sh, [&] { flat4(b, e, f); });
}
void f4Dual(B& b, const P& p) {
    F4 f;
    f.carbs = F4C_DUAL, f.exhaust = F4X_BAJA;
    f4Design(b, p, f);
}
void f4Single(B& b, const P& p) {
    F4 f;
    f.carbs = F4C_SINGLE, f.exhaust = F4X_STOCK, f.alternator = false;
    f.barrelR = 0.046;
    f4Design(b, p, f);
}
void f4Stroker(B& b, const P& p) {
    F4 f;
    f.carbs = F4C_IDF, f.exhaust = F4X_STINGER, f.cooler = true;
    f.headZ = 0.345, f.barrelR = 0.054;
    f4Design(b, p, f);
}

std::vector<Param> f4Params(int paint, int chrome) {
    return {choice("paint", "TIN", paint, enginePaintNames()), choice("chrome", "CHROME", chrome, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()};
}

// ---- Registration (variant order: drivetrain.h) ----------------------------------------------------
Reg crateEngineI4({"crate_engine_i4", "CRATE ENGINE I4", "PARTS", "Career upgrade: an inline-four crate engine (Honda SOHC or DOHC VTEC, or a 2.3 turbo four)", 0.75,
                   {
                       {"HONDA SOHC", "D16 1.6 single-cam 16-valve: pressed cover, distributor, cast manifold under a heat shield",
                        {choice("header", "BUILD", 0, {"STOCK", "CAM AND HEADER"}), choice("cover", "CAM COVER", 0, {"BLACK", "SILVER", "BLUE"}), mountParam(), wearParam(), seedParam()},
                        hondaSohc},
                       {"HONDA DOHC", "B16A / B18C DOHC VTEC: crinkle cam cover, tall runners to a high plenum, 4-2-1 header",
                        {choice("typer", "TRIM", 0, {"B16A", "TYPE R"}), mountParam(), wearParam(), seedParam()}, hondaVtec},
                       {"TURBO FOUR", "2.3 turbo four: ribbed plastic cover, coil-on-plug, turbo on the exhaust side, intercooler pipes",
                        {choice("cover", "ENGINE COVER", 0, {"BLACK", "GRAPHITE", "SILVER"}), mountParam(), wearParam(), seedParam()}, ecoFour},
                   }});

Reg crateEngineI3({"crate_engine_i3", "CRATE ENGINE I3", "PARTS", "The UTV's 900 cc turbo triple", 0.6,
                   {
                       {"TURBO TRIPLE", "Compact DOHC turbo triple, its charge pipe up and over the cam cover",
                        {choice("stage", "STAGE", 0, {"STAGE 1", "STAGE 2"}), choice("tank", "OIL TANK", 1, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()}, turboTriple},
                   }});

Reg crateEngineFlat4({"crate_engine_flat4", "CRATE ENGINE FLAT-4", "PARTS", "An air-cooled VW flat four with its fan shroud", 0.8,
                      {
                          {"DUAL CARBS", "2.0 built Type 1: two carbs under round chrome air cleaners, a 4-into-1 and quiet pack", f4Params(2, 1), f4Dual},
                          {"SINGLE CARB", "1835 street motor: one centre carb, oil-bath cleaner, heater boxes, stock muffler", f4Params(2, 0), f4Single},
                          {"DUAL 48 STROKER", "2332 stroker: twin-throat 48s on stacks, merged header and stinger, remote oil cooler", f4Params(2, 1), f4Stroker},
                      }});

}  // namespace
