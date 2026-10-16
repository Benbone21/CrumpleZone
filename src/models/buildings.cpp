// Buildings and camp: repair shed, cabin, outhouse, tent, campfire, woodpile, picnic table,
// ore cart, mine portal, fire tower, wrecked pickup. (See modelkit.h for the frame and primitives.)
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

constexpr double PI = M_PI;


// ---- Colour helpers --------------------------------------------------------------------

V3 jit(const V3& c, uint32_t seed, uint32_t i, double a = 0.1) { return c * (1.0 + (hash01(seed, i) - 0.5) * 2 * a); }

// Weathered timber: 0 brown, 1 silver-grey, 2 fresh pine, 3 dark old, 4 red-brown
V3 timber(int kind, uint32_t seed, uint32_t i, double a = 0.14) {
    static const V3 base[] = {V3(0.40, 0.30, 0.20), V3(0.50, 0.45, 0.38), V3(0.62, 0.46, 0.28), V3(0.24, 0.17, 0.11), V3(0.48, 0.27, 0.17)};
    const V3 c = base[kind % 5];
    const double v = hash01(seed, i), w = hash01(seed + 77, i);
    return V3(c.x * (1 + (v - 0.5) * 2 * a + (w - 0.5) * 0.06), c.y * (1 + (v - 0.5) * 2 * a), c.z * (1 + (v - 0.5) * 2 * a - (w - 0.5) * 0.06));
}
V3 rustCol(uint32_t seed, uint32_t i) {
    const double v = hash01(seed, i);
    return mix(V3(0.30, 0.15, 0.09), V3(0.52, 0.27, 0.12), v);
}
V3 muted(const V3& c, double k = 0.25) { return mix(c, V3(0.45, 0.45, 0.43), k); }
const V3 kZinc(0.58, 0.60, 0.61);
const V3 kSteelDark(0.22, 0.23, 0.25);

// ---- Shape helpers ---------------------------------------------------------------------

// An I-section beam from a to e. side: direction of the web depth (perpendicular to the beam).
void ibeam(B& b, const V3& a, const V3& e, double depth, double flange, const V3& side, double tw = 0.012, double tf = 0.016) {
    const V3 d = norm(e - a);
    const V3 s = norm(side - d * dot(side, d));
    b.beam(a, e, tw, depth - 2 * tf + 0.004, s);
    b.beam(a + s * (depth / 2 - tf / 2), e + s * (depth / 2 - tf / 2), flange, tf, s);
    b.beam(a - s * (depth / 2 - tf / 2), e - s * (depth / 2 - tf / 2), flange, tf, s);
}

// A corrugated sheet: from corner A, U across (the wave runs along it), Vv along the ribs. A flat
// underside sits just below. sub: facets per wave (4: smooth round ribs, 2: zig-zag).
void corrug(B& b, const V3& A, const V3& U, const V3& Vv, double period, double amp, const V3& hint, int sub = 4, bool under = true) {
    V3 n = norm(cross(U, Vv));
    if (dot(n, hint) < 0) n = -n;
    const double Lu = len(U);
    const int periods = std::max(1, (int)std::lround(Lu / period));
    const int N = periods * sub;
    const V3 uh = norm(U);
    auto H = [&](int i) { return amp * std::cos(2 * PI * i / sub); };
    auto dH = [&](int i) { return -amp * std::sin(2 * PI * i / sub) * 2 * PI * periods / Lu; };
    for (int i = 0; i < N; i++) {
        const double t0 = (double)i / N, t1 = (double)(i + 1) / N;
        const V3 p0 = A + U * t0 + n * H(i), p1 = A + U * t1 + n * H(i + 1);
        V3 n0, n1;
        if (sub >= 4) {
            n0 = norm(n - uh * dH(i)), n1 = norm(n - uh * dH(i + 1));
        } else {
            n0 = n1 = norm(n - uh * ((H(i + 1) - H(i)) / (Lu / N)));
        }
        b.triN(p0, p1, p1 + Vv, n0, n1, n1);
        b.triN(p0, p1 + Vv, p0 + Vv, n0, n1, n0);
    }
    if (under) {
        const V3 o = n * (-(amp + 0.012));
        b.triN(A + o, A + U + o, A + U + Vv + o, -n, -n, -n);
        b.triN(A + o, A + U + Vv + o, A + Vv + o, -n, -n, -n);
    }
}

// A wrench symbol (open-ended spanner) lying in the front/up plane, at the origin of the
// frame, turned by `ang` degrees; size: overall length.
void wrench(B& b, double size, double ang, double thick = 0.012) {
    const double s = size / 0.76;
    std::vector<V2> poly;
    const double rh = 0.16 * s, cy = 0.30 * s, hw = 0.05 * s, nw = 0.055 * s;
    auto arc = [&](double a0, double a1) {
        const int n = 8;
        for (int i = 0; i <= n; i++) {
            const double a = (a0 + (a1 - a0) * i / n) * PI / 180;
            poly.push_back(V2(rh * std::cos(a), cy + rh * std::sin(a)));
        }
    };
    poly.push_back(V2(hw, -0.45 * s));
    arc(-71.8, 70);
    poly.push_back(V2(nw, cy));
    poly.push_back(V2(-nw, cy));
    arc(110, 251.8);
    poly.push_back(V2(-hw, -0.45 * s));
    b.push();
    b.pitch(ang);
    b.extrude(poly, thick);
    b.pop();
}

// A turned lamp shade (floodlight): hung on a stem, a hot lens under it.
void floodlight(B& b, const V3& at, double r, const V3& shade, double tilt = 0) {
    b.push();
    b.move(at);
    b.roll(tilt);
    b.style(shade, PAINT);
    b.lathe(V3(0, 0, 0), {V2(0.0, 0.12 * r / 0.15), V2(r * 0.45, 0.115 * r / 0.15), V2(r * 0.55, 0.07 * r / 0.15), V2(r, 0.0)}, 14);
    b.style(V3(1.0, 0.92, 0.7), EMISSIVE);
    b.cyl(V3(0, -0.006, 0), V3(0, 0.002, 0), r * 0.92, 14);
    b.pop();
}

// A tyre (torus) lying flat or standing.
void tyre(B& b, const V3& c, double R, double r, bool flat = true, double ang = 0) {
    b.push();
    b.move(c);
    if (!flat) b.roll(90);
    if (ang != 0) b.yaw(ang);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.torus(V3(0, 0, 0), R, r, 14, 6);
    b.pop();
}

// A 200 L oil drum standing on the ground at the origin.
void drum(B& b, const V3& col, double rustAmt, uint32_t seed) {
    const double r = 0.29, h = 0.86;
    b.style(mix(col, rustCol(seed, 1), rustAmt), PAINT);
    b.lathe(V3(0, 0, 0), {V2(r - 0.02, 0.0), V2(r, 0.03), V2(r, 0.26), V2(r + 0.012, 0.27), V2(r + 0.012, 0.29), V2(r, 0.30), V2(r, 0.56), V2(r + 0.012, 0.57), V2(r + 0.012, 0.59),
                          V2(r, 0.6), V2(r, h - 0.03), V2(r - 0.02, h), V2(r - 0.05, h), V2(r - 0.05, h - 0.02), V2(0, h - 0.02)}, 10, false);
    b.style(V3(0.2, 0.2, 0.2), METAL);
    b.cyl(V3(0.1, h - 0.018, 0.08), V3(0.1, h + 0.01, 0.08), 0.03, 6);
    b.cyl(V3(-0.1, h - 0.018, -0.05), V3(-0.1, h + 0.008, -0.05), 0.022, 6);
}

// A roll-around tool chest, drawers facing +x.
void toolChest(B& b, const V3& col) {
    b.style(col, PAINT);
    b.box(V3(0, 0.56, 0), V3(0.48, 0.82, 0.82), 0.02);
    b.color(col * 0.7);
    b.box(V3(-0.01, 1.0, 0), V3(0.5, 0.05, 0.85));  // lid
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    for (int k = 0; k < 5; k++) b.box(V3(0.243, 0.2 + k * 0.145, 0), V3(0.004, 0.006, 0.74));  // drawer seams
    b.style(V3(0.7, 0.7, 0.72), METAL);
    for (int k = 0; k < 5; k++) b.box(V3(0.255, 0.27 + k * 0.145, 0), V3(0.016, 0.018, 0.4));
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.17, 0.06, sz * 0.34), V3(0.08, 0.12, 0.05));
    b.style(V3(0.7, 0.7, 0.7), METAL);
    b.box(V3(-0.27, 0.9, 0), V3(0.025, 0.02, 0.6));  // pull bar
}

// Air compressor: horizontal tank on a wheeled frame, a motor on top.
void compressor(B& b, const V3& col) {
    b.style(V3(0.22, 0.23, 0.25), PAINT);
    b.box(V3(0.2, 0.14, 0), V3(0.05, 0.04, 1.0));
    b.box(V3(-0.2, 0.14, 0), V3(0.05, 0.04, 1.0));
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.2, 0.17, sz * 0.4), V3(0.04, 0.12, 0.04));
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(0, 0.1, sz * 0.5), V3(0, 0.1, sz * 0.5 + sz * 0.05), 0.1, 10);
    b.style(col, PAINT);
    b.push();
    b.move(0, 0.38, 0);
    b.roll(90);
    b.lathe(V3(0, 0, 0), {V2(0, -0.5), V2(0.1, -0.49), V2(0.18, -0.45), V2(0.2, -0.38), V2(0.2, 0.38), V2(0.18, 0.45), V2(0.1, 0.49), V2(0, 0.5)}, 12);
    b.pop();
    b.style(V3(0.2, 0.2, 0.22), PAINT);
    b.box(V3(0, 0.66, 0.05), V3(0.34, 0.16, 0.5), 0.025);  // motor
    for (int k = 0; k < 4; k++) b.box(V3(0, 0.66, -0.15 + k * 0.12), V3(0.36, 0.17, 0.02));
    b.style(V3(0.62, 0.64, 0.65), METAL);
    b.cyl(V3(0.12, 0.74, 0.3), V3(0.12, 0.82, 0.3), 0.045, 8);
    b.cyl(V3(-0.1, 0.74, -0.2), V3(-0.1, 0.8, -0.2), 0.05, 8);
    b.style(V3(0.9, 0.9, 0.85), MATTE);
    b.cyl(V3(0.2, 0.42, 0.28), V3(0.226, 0.42, 0.28), 0.035, 8);  // gauge
    b.style(V3(0.05, 0.05, 0.05), MATTE);
    std::vector<V3> hose;
    for (int i = 0; i <= 10; i++) {
        const double a = i * 2.2;
        hose.push_back(V3(0.22 + 0.01 * std::sin(a), 0.38 + 0.02 * std::cos(a), -0.3 + i * 0.06));
    }
    b.tube(hose, 0.012, 5);
}

// A steel-framed bench with a timber top, vice and clutter, facing +x (its front), back at x=-0.2.
void workbench(B& b, uint32_t seed) {
    b.style(V3(0.3, 0.32, 0.34), PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.17, 0.46, sz * 0.9), V3(0.05, 0.92, 0.05));
    b.box(V3(0, 0.2, 0), V3(0.36, 0.03, 1.8));  // low shelf
    b.beam(V3(-0.17, 0.4, -0.9), V3(-0.17, 0.4, 0.9), 0.04, 0.04, V3(0, 1, 0));
    for (int k = 0; k < 3; k++) {
        b.style(timber(2, seed, 20 + k), WOOD);
        b.box(V3(-0.13 + k * 0.13, 0.94, 0), V3(0.125, 0.05, 2.0));
    }
    b.style(V3(0.2, 0.28, 0.4), PAINT);
    b.box(V3(0.2, 1.0, 0.7), V3(0.12, 0.09, 0.14));  // vice
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.cyl(V3(0.26, 1.0, 0.7), V3(0.4, 1.0, 0.7), 0.012, 5);
    b.style(V3(0.35, 0.5, 0.5), GLASS);
    b.cyl(V3(0.0, 0.97, -0.6), V3(0.0, 1.1, -0.6), 0.05, 8);  // jar of bolts
    b.style(timber(0, seed, 3), WOOD);
    b.beam(V3(0.05, 0.99, 0.1), V3(0.26, 0.99, -0.12), 0.025, 0.025);  // hammer shaft
    b.style(V3(0.35, 0.35, 0.37), METAL);
    b.box(V3(0.22, 1.01, -0.14), V3(0.07, 0.04, 0.05));
    b.style(V3(0.55, 0.5, 0.42), MATTE);
    b.box(V3(-0.08, 1.0, 0.35), V3(0.22, 0.06, 0.28));  // rags
    b.style(paint(0) * 0.8, PAINT);
    b.cyl(V3(0.1, 0.965, -0.2), V3(0.1, 1.05, -0.2), 0.04, 8);  // oil can
    b.style(V3(0.7, 0.7, 0.7), METAL);
    b.cone(V3(0.1, 1.05, -0.2), V3(0.1, 1.09, -0.2), 0.04, 0.015, 8);
    b.style(timber(2, seed, 31) * 0.9, WOOD);
    b.box(V3(-0.2, 1.55, 0), V3(0.025, 0.8, 1.9));  // pegboard
    b.style(V3(0.55, 0.57, 0.6), METAL);
    for (int k = 0; k < 5; k++) {
        const double z = -0.7 + k * 0.35 + 0.1 * hash01(seed, k);
        b.beam(V3(-0.17, 1.32, z), V3(-0.17, 1.72 - 0.15 * hash01(seed, k + 9), z), 0.02, 0.012);
    }
    b.style(paint(0) * 0.75, PAINT);
    b.box(V3(-0.17, 1.8, 0.1), V3(0.02, 0.08, 0.15));
    b.style(paint(8) * 0.8, PAINT);
    b.box(V3(-0.17, 1.8, -0.4), V3(0.02, 0.08, 0.15));
}

// Steel shelving with boxes and cans (back to -x).
void shelving(B& b, uint32_t seed) {
    b.style(V3(0.25, 0.3, 0.4), PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.17, 0.95, sz * 0.9), V3(0.05, 1.9, 0.05));
    for (int k = 0; k < 4; k++) {
        b.style(timber(2, seed, 40 + k), WOOD);
        b.box(V3(0, 0.15 + k * 0.58, 0), V3(0.4, 0.035, 1.84));
    }
    for (int k = 0; k < 3; k++) {
        const double y = 0.17 + k * 0.58;
        b.style(mix(paint(8), V3(0.4, 0.4, 0.4), 0.2) * 0.9, MATTE);
        b.box(V3(0, y + 0.1, -0.65), V3(0.3, 0.2, 0.4));
        b.style(V3(0.7, 0.62, 0.45), MATTE);
        b.box(V3(0, y + 0.07, -0.1), V3(0.3, 0.14, 0.3));
        b.style(paint(5) * 0.8, PAINT);
        for (int c = 0; c < 2; c++) b.cyl(V3(0.0, y, 0.35 + c * 0.25), V3(0.0, y + 0.22, 0.35 + c * 0.25), 0.06, 7);
    }
}

// A fire extinguisher.
void extinguisher(B& b) {
    b.style(paint(0), PAINT);
    b.lathe(V3(0, 0, 0), {V2(0.05, 0.0), V2(0.062, 0.03), V2(0.062, 0.3), V2(0.045, 0.38), V2(0.0, 0.4)}, 8);
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    b.cyl(V3(0, 0.4, 0), V3(0, 0.45, 0), 0.02, 5);
}

// Pad, expansion joints, oil stains, hazard sleeves round the posts, and the outside clutter.
void shedGround(B& b, const P& p, uint32_t seed, bool backWall) {
    (void)backWall;
    const double padY = 0.025;
    b.style(V3(0.56, 0.55, 0.52), CONCRETE);
    b.box(V3(0, padY - 0.0325, 0), V3(12.4, 0.065, 8.8), 0.01);
    b.color(V3(0.34, 0.33, 0.31));
    for (double x : {-3.1, 0.0, 3.1}) b.box(V3(x, padY - 0.001, 0), V3(0.025, 0.004, 8.7));
    for (double z : {-2.2, 2.2}) b.box(V3(0, padY - 0.001, z), V3(12.3, 0.004, 0.025));
    // Oil and tyre marks where the bay sees traffic.
    b.color(V3(0.2, 0.2, 0.21));
    b.extrudeUp(circle(0.55, 10, -0.6, 0.5), 0.0015, padY);
    b.extrudeUp(circle(0.35, 8, 1.4, -0.9), 0.0015, padY);
    b.extrudeUp(circle(0.25, 8, 2.9, 0.7), 0.0015, padY);
    b.color(V3(0.4, 0.39, 0.37));
    for (int k = 0; k < 2; k++) b.extrudeUp({V2(-5.8, -1.05 + k * 2.1), V2(5.8, -1.05 + k * 2.1), V2(5.8, -0.9 + k * 2.1), V2(-5.8, -0.9 + k * 2.1)}, 0.0012, padY);
    // Hazard sleeves round the four posts.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(V3(0.82, 0.62, 0.08), PAINT);
            b.box(V3(sx * 5.0, 0.55, sz * 3.7), V3(0.3, 1.1, 0.3), 0.02);
            b.style(V3(0.08, 0.08, 0.08), PAINT);
            for (int k = 0; k < 3; k++) b.box(V3(sx * 5.0, 0.2 + k * 0.38, sz * 3.7), V3(0.305, 0.12, 0.305));
        }
    if (!p.i("stuff")) return;
    // Equipment, all outside the bay (|z| > 3.9).
    b.at(V3(-3.9, padY, -4.1), -90, [&] { workbench(b, seed); });
    b.at(V3(-2.1, padY, -4.1), -90, [&] { toolChest(b, paint(0) * 0.85); });
    b.at(V3(3.6, padY, -4.08), -90, [&] { shelving(b, seed); });
    b.at(V3(-5.4, padY, -4.1), -90, [&] { extinguisher(b); });
    b.at(V3(-3.3, padY, 4.25), 90, [&] { compressor(b, paint(8) * 0.85); });
    b.at(V3(-0.6, padY, 4.3), 0, [&] { drum(b, muted(paint(5)), 0.2, seed); });
    b.at(V3(0.2, padY, 4.7), 0, [&] { drum(b, muted(paint(0)), 0.5, seed + 3); });
    for (int k = 0; k < 3; k++) tyre(b, V3(2.8, padY + 0.1 + k * 0.2, 4.35), 0.22, 0.1, true, k * 37);
    tyre(b, V3(3.4, padY + 0.32, 4.6), 0.22, 0.1, false, 90);
    tyre(b, V3(4.4, padY + 0.1, 4.2), 0.22, 0.1, true, 11);
    b.at(V3(-5.45, padY, 4.1), 0, [&] { extinguisher(b); });
}

// The big sign over an entrance: board, lettering, wrench. Faces +x in the current frame, at x.
void repairSign(B& b, double x, double y, double w, double h, const V3& board, const V3& ink, double px) {
    b.style(board, PAINT);
    b.box(V3(x, y, 0), V3(0.06, h, w), 0.015);
    b.color(board * 0.6);
    b.box(V3(x + 0.03, y + h / 2 - 0.02, 0), V3(0.012, 0.03, w - 0.05));  // trim lines
    b.box(V3(x + 0.03, y - h / 2 + 0.02, 0), V3(0.012, 0.03, w - 0.05));
    b.style(ink, PAINT);
    const double tw = 35 * px;
    b.letters("REPAIR", V3(x + 0.03, y, -0.2 * 0 - (w / 2 - tw / 2 - 0.12)), px, 0.012);
    // wrench at the viewer's left (+z), 45 degrees
    b.push();
    b.move(x + 0.036, y, w / 2 - 0.28);
    b.yaw(90);
    b.style(ink, PAINT);
    wrench(b, h * 0.85, 45, 0.016);
    b.pop();
}


// ============================================================================================
// REPAIR SHED
// ============================================================================================

// A: steel portal frame, pitched corrugated roof, part-clad back wall.
const double kRiseA = 1.6 / 3.7;
double roofA(double z) { return 6.0 - std::fabs(z) * kRiseA; }

void shedSteel(B& b, const P& p) {
    const uint32_t seed = 11;
    const V3 col = muted(paint(p.i("paint")), 0.3);
    const double wear = p("wear");
    shedGround(b, p, seed, true);
    const V3 steel = col * 0.75;
    // Columns (the four collider posts), with base plates and anchor bolts.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const double x = sx * 5.0, z = sz * 3.7;
            b.style(steel, PAINT);
            ibeam(b, V3(x, 0.05, z), V3(x, 4.4, z), 0.26, 0.2, V3(0, 0, 1), 0.014, 0.018);
            b.style(V3(0.3, 0.3, 0.32), METAL);
            b.box(V3(x, 0.04, z), V3(0.4, 0.03, 0.4));
            for (int a = -1; a <= 1; a += 2)
                for (int c = -1; c <= 1; c += 2) b.cyl(V3(x + a * 0.15, 0.05, z + c * 0.15), V3(x + a * 0.15, 0.09, z + c * 0.15), 0.014, 6);
            // haunch plate at the eave
            b.style(steel * 0.9, PAINT);
            b.at(V3(x, 0, z), sz > 0 ? 90 : -90, [&] {
                b.extrude({V2(0, 3.95), V2(0, 4.4), V2(0.9, 4.4), V2(0.9, 4.14)}, 0.014);
            });
        }
    // Eave beams and the header across each end.
    b.style(steel, PAINT);
    for (int sz = -1; sz <= 1; sz += 2) ibeam(b, V3(-5.95, 4.28, sz * 3.7), V3(5.95, 4.28, sz * 3.7), 0.28, 0.15, V3(0, 1, 0));
    for (int sx = -1; sx <= 1; sx += 2) ibeam(b, V3(sx * 5.0, 4.28, -3.7), V3(sx * 5.0, 4.28, 3.7), 0.28, 0.16, V3(0, 1, 0));
    // Rafters: heavy at the end frames, lighter between.
    for (double x : {-5.0, -2.5, 0.0, 2.5, 5.0}) {
        const bool main = std::fabs(x) > 4.9;
        for (int sz = -1; sz <= 1; sz += 2) {
            const V3 a(x, 4.3, sz * 3.7), e(x, roofA(0) - 0.1, 0);
            const V3 d = norm(e - a), n(0, std::fabs(d.z), sz * d.y * -1);
            (void)n;
            const V3 nn = norm(V3(0, 3.7, sz * 1.6));
            b.style(steel * (main ? 1.0 : 0.9), PAINT);
            ibeam(b, V3(x, roofA(3.7) - 0.14, sz * 3.7), V3(x, roofA(0) - 0.14, 0), main ? 0.3 : 0.2, main ? 0.15 : 0.1, nn);
        }
    }
    // Purlins carrying the roof.
    b.style(V3(0.4, 0.42, 0.44), METAL);
    for (int sz = -1; sz <= 1; sz += 2)
        for (double z : {0.45, 1.5, 2.6, 3.7, 4.75}) {
            const V3 nn = norm(V3(0, 3.7, sz * 1.6));
            const double y = roofA(z) + 0.07;
            b.beam(V3(-5.95, y, sz * z), V3(5.95, y, sz * z), 0.075, 0.12, nn);
        }
    // Roof sheets.
    for (int sz = -1; sz <= 1; sz += 2)
        for (int k = 0; k < 12; k++) {
            const double x0 = -5.9 + k * 0.98;
            const double yE = roofA(4.8) + 0.14, yR = roofA(0) + 0.14;
            V3 c = kZinc;
            c = mix(c * (0.9 + 0.2 * hash01(seed, k + 5 * (sz + 1))), rustCol(seed, k + 10 * (sz + 2)), wear * hash01(seed + 1, k + 20 * (sz + 1)) * 0.8);
            b.style(c, METAL);
            corrug(b, V3(x0, yE, sz * 4.8), V3(0.96, 0, 0), V3(0, yR - yE, -sz * 4.8), 0.19, 0.02, V3(0, 1, 0), 4, true);
        }
    // Rust and dirt patches on the roof.
    for (int k = 0; k < (int)std::lround(wear * 16); k++) {
        const int sz = k % 2 ? 1 : -1;
        const double z = 0.8 + 3.6 * hash01(seed + 5, k), x = -5.5 + 11 * hash01(seed + 6, k * 7);
        const V3 nn = norm(V3(0, 3.7, sz * 1.6));
        b.color(rustCol(seed, k) * 0.9);
        b.beam(V3(x - 0.3, roofA(z) + 0.172, sz * z), V3(x + 0.3, roofA(z) + 0.172, sz * z), 0.5, 0.004, nn);
    }
    // Ridge cap and gutters.
    b.style(kZinc * 0.85, METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        const V3 nn = norm(V3(0, 3.7, sz * 1.6));
        b.beam(V3(-5.98, roofA(0) + 0.13, sz * 0.25), V3(5.98, roofA(0) + 0.13, sz * 0.25), 0.5, 0.02, nn);
    }
    b.style(V3(0.35, 0.37, 0.38), METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0, roofA(4.85) + 0.02, sz * 4.88), V3(11.9, 0.1, 0.1), 0.012);
        b.cyl(V3(5.7, 0.0, sz * 4.9), V3(5.7, roofA(4.85), sz * 4.9), 0.04, 8);
    }
    // Both entrances: gable fill, sign, bargeboard, and the lights.
    for (int e = 0; e < 2; e++) {
        b.at(V3(0, 0, 0), e ? 180 : 0, [&] {
            b.style(col, PAINT);
            b.push();
            b.move(5.12, 0, 0);
            b.yaw(90);
            b.extrude({V2(-3.7, 4.42), V2(3.7, 4.42), V2(0.0, roofA(0) - 0.02)}, 0.04);
            b.pop();
            // ribs on the gable fill
            b.color(col * 0.85);
            for (int k = -7; k <= 7; k++) {
                const double z = k * 0.4;
                const double top = roofA(z) - 0.05;
                if (top - 4.45 > 0.05) b.box(V3(5.148, (4.45 + top) / 2, z), V3(0.012, top - 4.45, 0.04));
            }
            repairSign(b, 5.2, 4.98, 2.9, 0.72, V3(0.09, 0.11, 0.14), V3(0.9, 0.7, 0.1) * 0.95, 0.055);
            b.style(V3(0.3, 0.32, 0.34), PAINT);
            for (int sz = -1; sz <= 1; sz += 2) {
                const V3 nn = norm(V3(0, 3.7, sz * 1.6));
                b.beam(V3(5.96, roofA(4.8) + 0.02, sz * 4.8), V3(5.96, roofA(0) + 0.16, 0), 0.04, 0.22, nn);  // barge board
            }
        });
    }
    // Lights under the roof.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const double x = sx * 2.9, z = sz * 1.4;
            b.style(V3(0.2, 0.2, 0.22), METAL);
            b.cyl(V3(x, roofA(z) - 0.3, z), V3(x, 5.02, z), 0.012, 6);
            floodlight(b, V3(x, 4.85, z), 0.2, V3(0.28, 0.3, 0.3));
            floodlight(b, V3(x * 1.0, 4.08, sz * 3.55), 0.14, V3(0.28, 0.3, 0.3), -sz * 22);
        }
    // Fluorescent tubes under the ridge.
    b.style(V3(0.9, 0.95, 1.0), EMISSIVE);
    for (double x : {-1.2, 1.2}) b.box(V3(x, roofA(0) - 0.45, 0), V3(1.2, 0.05, 0.1), 0.01);
    // The back wall (part clad), its posts and girts.
    b.style(V3(0.62, 0.6, 0.57), CONCRETE);
    b.box(V3(0, 0.09, -4.5), V3(12.0, 0.18, 0.24), 0.015);
    for (double x : {-5.6, -2.8, 0.0, 2.8, 5.6}) {
        b.style(steel, PAINT);
        ibeam(b, V3(x, 0.15, -4.4), V3(x, 3.95, -4.4), 0.2, 0.12, V3(0, 0, 1), 0.01, 0.012);
    }
    b.style(V3(0.4, 0.42, 0.44), METAL);
    for (double y : {0.9, 1.8, 2.7, 3.6}) b.beam(V3(-5.95, y, -4.49), V3(5.95, y, -4.49), 0.06, 0.1, V3(0, 0, 1));
    for (int k = 0; k < 12; k++) {
        const double x0 = -5.9 + k * 0.98;
        const bool low = k >= 8;
        b.style(mix(col * (0.88 + 0.2 * hash01(seed, 50 + k)), rustCol(seed, 60 + k), wear * 0.3 * hash01(seed, 70 + k)), METAL);
        corrug(b, V3(x0, 0.18, -4.54), V3(0.96, 0, 0), V3(0, low ? 1.15 : 3.74, 0), 0.19, 0.018, V3(0, 0, -1), 4, true);
    }
    b.style(steel * 0.8, PAINT);
    b.box(V3(4.0, 1.36, -4.54), V3(3.9, 0.07, 0.08), 0.01);  // cap rail on the low part
    // A window band of translucent sheeting over the clad part (reads as skylight glass).
    b.style(V3(0.55, 0.65, 0.7), GLASS);
    b.box(V3(-2.0, 3.35, -4.585), V3(6.5, 0.38, 0.01));
}

// B: timber pole barn, shake or tin roof, board-and-batten back wall.
const double kRiseB = 1.45 / 3.95;
double roofB(double z) { return 5.95 - std::fabs(z) * kRiseB; }

void shedBarn(B& b, const P& p) {
    const uint32_t seed = 23;
    const double wear = p("wear");
    const bool tin = p.i("roof") == 1;
    shedGround(b, p, seed, true);
    // Poles.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const double x = sx * 5.0, z = sz * 3.7;
            b.style(timber(3, seed, 1 + sx * 3 + sz) * 0.95, WOOD);
            b.lathe(V3(x, 0, z), {V2(0, 0.0), V2(0.17, 0.0), V2(0.16, 0.12), V2(0.14, 0.3), V2(0.135, 4.0), V2(0.14, 4.4), V2(0, 4.4)}, 10, false);
            b.style(V3(0.22, 0.22, 0.24), METAL);
            for (int k = 0; k < 2; k++) b.cyl(V3(x + 0.1, 3.9 - k * 0.12, z - sz * 0.09), V3(x + 0.1, 3.9 - k * 0.12, z - sz * 0.18), 0.016, 6);  // through-bolts
            b.box(V3(x, 4.42, z), V3(0.3, 0.02, 0.3));
        }
    // Girders either side of the poles, tie beams across, the trusses above.
    for (int sz = -1; sz <= 1; sz += 2)
        for (int k = 0; k < 2; k++) {
            b.style(timber(0, seed, 10 + k + sz * 3), WOOD);
            b.box(V3(0, 4.15, sz * 3.7 + (k ? 1 : -1) * 0.19), V3(12.5, 0.3, 0.07), 0.008);
        }
    for (double x : {-5.0, 0.0, 5.0}) {
        const bool main = std::fabs(x) > 4;
        const double sy = 4.3, ty = 4.45;
        b.style(timber(0, seed, 20 + (int)x), WOOD);
        b.box(V3(x, 4.42, 0), V3(0.16, 0.2, 7.9), 0.012);  // tie beam
        b.beam(V3(x, 5.9, 0), V3(x, 4.52, 0), 0.14, 0.14, V3(1, 0, 0), 0.01);  // king post
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(timber(0, seed, 30 + sz + (int)x), WOOD);
            b.beam(V3(x, 4.5, sz * 3.9), V3(x, roofB(0) - 0.07, 0), 0.12, 0.2, V3(1, 0, 0), 0.01);  // rafter (up: x so depth is along x? see below)
            b.beam(V3(x, 4.55, sz * 1.5), V3(x, roofB(1.0) - 0.4, sz * 0.95), 0.09, 0.14, V3(1, 0, 0), 0.008);  // strut
        }
        (void)main; (void)sy; (void)ty;
    }
    // Purlins and roofing.
    for (int sz = -1; sz <= 1; sz += 2) {
        const V3 nn = norm(V3(0, 3.95, sz * 1.45));
        for (double z : {0.05, 1.2, 2.4, 3.6, 4.8}) {
            b.style(timber(0, seed, 40 + (int)(z * 10)), WOOD);
            b.beam(V3(-6.1, roofB(z) + 0.05, sz * z), V3(6.1, roofB(z) + 0.05, sz * z), 0.1, 0.1, nn);
        }
        if (!tin) {
            // Shake courses, eave to ridge.
            const int rows = 28;
            const double zE = 5.0;
            for (int r = 0; r < rows; r++) {
                const double z = zE - r * (zE - 0.05) / rows;
                const double y = roofB(z) + 0.12;
                const double sh = 0.85 + 0.3 * hash01(seed + 3, r + 40 * (sz + 1));
                b.style(timber(1, seed, 100 + r + 40 * (sz + 1), 0.1) * sh, WOOD);
                b.beam(V3(-6.15, y, sz * z), V3(6.15, y, sz * z), 0.2, 0.028, nn);
                b.color(timber(3, seed, 150 + r) * 0.5);
                b.beam(V3(-6.15, y - 0.012, sz * (z + 0.1)), V3(6.15, y - 0.012, sz * (z + 0.1)), 0.03, 0.014, nn);
            }
        } else {
            for (int k = 0; k < 13; k++) {
                const double x0 = -6.15 + k * 0.96;
                const double zE = 5.0;
                const V3 c = mix(mix(V3(0.5, 0.5, 0.5), kZinc, 0.4), rustCol(seed, k + 7 * sz), 0.35 + 0.5 * wear * hash01(seed, k + 30 * (sz + 1)));
                b.style(c, METAL);
                corrug(b, V3(x0, roofB(zE) + 0.2, sz * zE), V3(0.94, 0, 0), V3(0, roofB(0) - roofB(zE), -sz * zE), 0.19, 0.02, V3(0, 1, 0), 4, true);
            }
        }
    }
    // Ridge board.
    b.style(timber(3, seed, 200), WOOD);
    for (int sz = -1; sz <= 1; sz += 2) {
        const V3 nn = norm(V3(0, 3.95, sz * 1.45));
        b.beam(V3(-6.2, roofB(0) + 0.15, sz * 0.15), V3(6.2, roofB(0) + 0.15, sz * 0.15), 0.32, 0.03, nn);
    }
    // Gable ends: vertical boards, the sign, lights.
    for (int e = 0; e < 2; e++)
        b.at(V3(0, 0, 0), e ? 180 : 0, [&] {
            for (int k = -18; k <= 18; k++) {
                const double z = k * 0.215;
                const double top = roofB(std::fabs(z) + 0.1) + 0.02;
                if (top - 4.65 < 0.05) continue;
                b.style(timber(e ? 1 : 3, seed, 300 + k + 40 * e, 0.16), WOOD);
                b.box(V3(5.2, (4.62 + top) / 2, z), V3(0.025, top - 4.62, 0.2));
            }
            b.style(timber(0, seed, 99), WOOD);
            b.box(V3(5.2, 4.58, 0), V3(0.05, 0.08, 7.9));
            // Sign: planks painted white on dark, hung by chains from the tie beam.
            repairSign(b, 5.32, 5.0, 2.9, 0.72, V3(0.14, 0.12, 0.1), V3(0.85, 0.82, 0.7), 0.055);
            b.style(V3(0.2, 0.2, 0.2), METAL);
            for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(5.32, 4.64, sz * 1.1), V3(5.32, 5.62, sz * 1.1), 0.01, 5);
        });
    // Hanging work lamps.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(V3(0.1, 0.1, 0.1), MATTE);
            b.cyl(V3(sx * 2.9, 5.5, sz * 1.0), V3(sx * 2.9, 4.95, sz * 1.0), 0.01, 5);
            floodlight(b, V3(sx * 2.9, 4.75, sz * 1.0), 0.18, V3(0.24, 0.3, 0.26));
        }
    // Back wall: board and batten (part), on poles.
    for (double x : {-5.6, -2.8, 0.0, 2.8, 5.6}) {
        b.style(timber(3, seed, 400 + (int)(x * 3)), WOOD);
        b.lathe(V3(x, 0, -4.4), {V2(0, 0), V2(0.11, 0), V2(0.1, 0.2), V2(0.095, 3.9), V2(0.1, 4.0), V2(0, 4.0)}, 8, false);
    }
    b.style(timber(0, seed, 410), WOOD);
    for (double y : {1.0, 2.0, 3.0}) b.box(V3(0, y, -4.48), V3(12.0, 0.1, 0.05));
    for (int k = 0; k < 60; k++) {
        const double x = -5.9 + k * 0.205;
        if (x > 5.9) break;
        const bool low = x > 2.0;
        const double top = low ? 1.5 : 3.95 + 0.0;
        const double y0 = 0.05;
        b.style(timber(k % 5 == 0 ? 3 : 1, seed, 500 + k, 0.2), WOOD);
        b.box(V3(x, (y0 + top) / 2, -4.53), V3(0.19, top - y0, 0.03));
        if (k % 4 == 0) {
            b.style(timber(3, seed, 600 + k), WOOD);
            b.box(V3(x + 0.1, (y0 + top) / 2, -4.555), V3(0.05, top - y0, 0.02));
        }
    }
}

// C: quonset: steel pipe posts carrying a rolled arch of corrugated sheet.
void shedQuonset(B& b, const P& p) {
    const uint32_t seed = 37;
    const V3 col = muted(paint(p.i("paint")), 0.3);
    const double wear = p("wear");
    shedGround(b, p, seed, true);
    const double R = 4.88, yc = 4.4 + 1.7 - R;
    auto archY = [&](double z) { return yc + std::sqrt(R * R - z * z); };
    // Posts with caps.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const double x = sx * 5.0, z = sz * 3.7;
            b.style(V3(0.35, 0.37, 0.4), METAL);
            b.lathe(V3(x, 0, z), {V2(0, 0.03), V2(0.2, 0.03), V2(0.2, 0.06), V2(0.12, 0.07), V2(0.12, 4.38), V2(0.17, 4.4), V2(0.17, 4.44), V2(0, 4.44)}, 12, false);
        }
    // Arch ribs (pipe), under the sheets: end ribs heavier.
    const double phiMax = std::asin(4.5 / R);
    auto rib = [&](double x, double r) {
        std::vector<V3> path;
        for (int i = 0; i <= 16; i++) {
            const double phi = -phiMax + 2 * phiMax * i / 16;
            path.push_back(V3(x, yc + (R - 0.06) * std::cos(phi), (R - 0.06) * std::sin(phi)));
        }
        b.tube(path, r, 6);
    };
    b.style(V3(0.38, 0.4, 0.42), METAL);
    for (double x : {-5.0, -2.5, 0.0, 2.5, 5.0}) rib(x, std::fabs(x) > 4.9 ? 0.07 : 0.05);
    // Purlin pipes along the arch.
    b.style(V3(0.35, 0.36, 0.38), METAL);
    for (int k = -4; k <= 4; k++) {
        const double phi = k * phiMax / 4.4;
        b.cyl(V3(-5.95, yc + (R - 0.1) * std::cos(phi), (R - 0.1) * std::sin(phi)), V3(5.95, yc + (R - 0.1) * std::cos(phi), (R - 0.1) * std::sin(phi)), 0.025, 6);
    }
    // Corrugated sheets, a strip per panel per arc segment.
    const int segs = 16;
    for (int k = 0; k < 12; k++) {
        const double x0 = -5.9 + k * 0.98;
        for (int i = 0; i < segs; i++) {
            const double a0 = -phiMax + 2 * phiMax * i / segs, a1 = -phiMax + 2 * phiMax * (i + 1) / segs;
            const V3 q0(0, yc + R * std::cos(a0), R * std::sin(a0)), q1(0, yc + R * std::cos(a1), R * std::sin(a1));
            const V3 am = (q0 + q1) * 0.5;
            V3 c = mix(mix(V3(0.5, 0.52, 0.53), kZinc, 0.5), rustCol(seed, k * 3 + i), wear * 0.5 * hash01(seed, k * 31 + i / 4));
            c = c * (0.93 + 0.14 * hash01(seed + 2, k)) * (0.9 + 0.1 * std::sin(i * 0.4));
            b.style(c, METAL);
            corrug(b, V3(x0, q0.y, q0.z), V3(0.96, 0, 0), V3(0, q1.y - q0.y, q1.z - q0.z), 0.32, 0.03, V3(0, am.y - yc, am.z), 2, true);
        }
    }
    // Rolled edge at each end.
    b.style(col * 0.85, PAINT);
    for (int e = -1; e <= 1; e += 2) {
        std::vector<V3> path;
        for (int i = 0; i <= 16; i++) {
            const double phi = -phiMax + 2 * phiMax * i / 16;
            path.push_back(V3(e * 5.97, yc + (R + 0.03) * std::cos(phi), (R + 0.03) * std::sin(phi)));
        }
        b.tube(path, 0.045, 6);
    }
    // Entrance signs, hung from the edge.
    for (int e = 0; e < 2; e++)
        b.at(V3(0, 0, 0), e ? 180 : 0, [&] {
            repairSign(b, 6.1, 5.0, 2.9, 0.74, col * 0.55, V3(0.92, 0.9, 0.82), 0.055);
            b.style(V3(0.25, 0.25, 0.27), METAL);
            for (int sz = -1; sz <= 1; sz += 2) b.box(V3(6.03, 5.62, sz * 1.15), V3(0.04, 0.5, 0.04));
        });
    // Lights hung from the ribs.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            const double x = sx * 2.5, z = sz * 1.5;
            b.style(V3(0.2, 0.2, 0.22), METAL);
            b.cyl(V3(x, archY(z) - 0.15, z), V3(x, archY(z) - 0.55, z), 0.012, 6);
            floodlight(b, V3(x, archY(z) - 0.75, z), 0.2, V3(0.28, 0.3, 0.3));
        }
    // Back wall: vertical corrugated panels up to the arch, on a footing.
    b.style(V3(0.62, 0.6, 0.57), CONCRETE);
    b.box(V3(0, 0.09, -4.5), V3(12.0, 0.18, 0.24), 0.015);
    for (double x : {-5.6, -2.8, 0.0, 2.8, 5.6}) {
        b.style(V3(0.35, 0.37, 0.4), METAL);
        b.cyl(V3(x, 0.15, -4.4), V3(x, archY(-4.4) - 0.05, -4.4), 0.06, 8);
    }
    for (int k = 0; k < 12; k++) {
        const double x0 = -5.9 + k * 0.98;
        const bool low = k >= 8;
        b.style(mix(col * (0.88 + 0.2 * hash01(seed, 50 + k)), rustCol(seed, 60 + k), wear * 0.3 * hash01(seed, 70 + k)), METAL);
        corrug(b, V3(x0, 0.18, -4.52), V3(0.96, 0, 0), V3(0, low ? 1.15 : archY(4.5) - 0.2, 0), 0.19, 0.018, V3(0, 0, -1), 4, true);
    }
}

Reg repairShed({"repair_shed", "REPAIR SHED", "BUILDINGS", "The repair station on every map: drive through the bay along X to be repaired", 12.4,
                {
                    {"STEEL PORTAL", "Steel portal frame, pitched corrugated roof, clad back wall",
                     {paintParam(7), num("wear", "WEAR", 0.4, 0, 1, 0.05), choice("stuff", "WORKSHOP CLUTTER", 1, {"OFF", "ON"})}, shedSteel},
                    {"POLE BARN", "Round timber poles, king-post trusses, shake or tin roof",
                     {choice("roof", "ROOF", 0, {"SHAKE", "TIN"}), num("wear", "WEAR", 0.4, 0, 1, 0.05), choice("stuff", "WORKSHOP CLUTTER", 1, {"OFF", "ON"})}, shedBarn},
                    {"QUONSET", "Rolled corrugated arch on pipe posts",
                     {paintParam(9), num("wear", "WEAR", 0.4, 0, 1, 0.05), choice("stuff", "WORKSHOP CLUTTER", 1, {"OFF", "ON"})}, shedQuonset},
                }});


// ============================================================================================
// CABIN
// ============================================================================================

struct Fix {
    int face;           // 0 front +x, 1 back, 2 left +z, 3 right -z
    double t, w, y0, y1;  // along the wall (local z), width, bottom, top
    bool door;
};
const double kFx = 2.08, kFz = 2.58;  // wall centre lines
void faceFrame(int f, V3& pos, double& yaw, double& len) {
    switch (f) {
        case 0: pos = V3(kFx, 0, 0), yaw = 0, len = 2 * kFz; break;
        case 1: pos = V3(-kFx, 0, 0), yaw = 180, len = 2 * kFz; break;
        case 2: pos = V3(0, 0, kFz), yaw = -90, len = 2 * kFx; break;
        default: pos = V3(0, 0, -kFz), yaw = 90, len = 2 * kFx; break;
    }
}

V3 stoneCol(uint32_t seed, uint32_t i) {
    const double v = hash01(seed, i), w = hash01(seed + 9, i);
    const V3 c = mix(V3(0.42, 0.40, 0.37), V3(0.50, 0.46, 0.40), w);
    return c * (0.78 + 0.4 * v);
}

// Rows of dry-laid stones on a wall face: local x outward from x=0, along z (centred), y0..y1.
void stoneWall(B& b, double width, double y0, double y1, double sz, uint32_t seed, double depth = 0.07, double c0 = 0, double c1 = 0, double cTop = 0) {
    int row = 0;
    for (double y = y0; y < y1 - 0.03; row++) {
        double h = sz * (0.55 + 0.35 * hash01(seed, row * 17));
        if (y + h > y1) h = y1 - y;
        double z = -width / 2 - sz * 0.5 * hash01(seed + 1, row);
        int k = 0;
        while (z < width / 2 - 0.01) {
            double w = sz * (0.75 + 0.7 * hash01(seed + 2, row * 31 + k));
            const double zl = std::max(z, -width / 2), zr = std::min(z + w, width / 2);
            const bool cut = c1 > c0 && y < cTop && zr > c0 && zl < c1;
            if (zr - zl > 0.06 && !cut) {
                b.style(stoneCol(seed, row * 53 + k), ROCK);
                const double d = depth * (0.7 + 0.6 * hash01(seed + 3, row * 29 + k));
                b.box(V3(d / 2, y + h / 2, (zl + zr) / 2), V3(d, h - 0.012, zr - zl - 0.014));
            }
            z += w;
            k++;
        }
        y += h;
    }
}

// A window facing +x in the local frame (xs: wall surface).
void window(B& b, double xs, double zc, double yc, double w, double h, const V3& frame, const V3& shut, bool shutters) {
    const double ft = 0.055;
    b.style(V3(0.03, 0.035, 0.04), MATTE);
    b.box(V3(xs - 0.05, yc, zc), V3(0.02, h, w));  // dark room behind
    b.style(V3(0.32, 0.42, 0.48), GLASS);
    b.box(V3(xs - 0.01, yc, zc), V3(0.012, h, w));
    b.style(frame, WOOD);
    b.box(V3(xs + 0.01, yc + h / 2 + ft / 2, zc), V3(0.07, ft, w + 2 * ft));
    b.box(V3(xs + 0.01, yc - h / 2 - ft / 2, zc), V3(0.07, ft, w + 2 * ft));
    b.box(V3(xs + 0.01, yc, zc + w / 2 + ft / 2), V3(0.07, h, ft));
    b.box(V3(xs + 0.01, yc, zc - w / 2 - ft / 2), V3(0.07, h, ft));
    b.box(V3(xs + 0.015, yc, zc), V3(0.03, h, 0.025));  // muntins
    b.box(V3(xs + 0.015, yc, zc), V3(0.03, 0.025, w));
    b.box(V3(xs + 0.07, yc - h / 2 - ft - 0.02, zc), V3(0.15, 0.04, w + 2 * ft + 0.12));  // sill
    if (shutters)
        for (int s = -1; s <= 1; s += 2) {
            b.style(shut, WOOD);
            const double zs = zc + s * (w / 2 + ft + w * 0.25 + 0.01);
            b.box(V3(xs + 0.04, yc, zs), V3(0.035, h + 0.1, w * 0.5 - 0.02));
            b.color(shut * 0.7);
            for (int k = 0; k < 3; k++) b.box(V3(xs + 0.062, yc - h * 0.3 + k * h * 0.3, zs), V3(0.012, 0.035, w * 0.5 - 0.04));
        }
}

// A plank door facing +x, standing on the floor level y0.
void plankDoor(B& b, double xs, double zc, double y0, double w, double h, const V3& col, uint32_t seed) {
    const int n = 5;
    for (int i = 0; i < n; i++) {
        b.style(jit(col, seed, i, 0.12), WOOD);
        b.box(V3(xs, y0 + h / 2, zc - w / 2 + (i + 0.5) * w / n), V3(0.045, h, w / n - 0.006));
    }
    b.style(col * 0.7, WOOD);
    b.beam(V3(xs + 0.03, y0 + 0.2, zc - w * 0.4), V3(xs + 0.03, y0 + h - 0.2, zc + w * 0.4), 0.09, 0.03);  // Z brace
    b.box(V3(xs + 0.03, y0 + 0.2, zc), V3(0.03, 0.12, w * 0.95));
    b.box(V3(xs + 0.03, y0 + h - 0.2, zc), V3(0.03, 0.12, w * 0.95));
    b.style(col * 0.6, WOOD);  // frame
    b.box(V3(xs + 0.0, y0 + h + 0.06, zc), V3(0.1, 0.12, w + 0.24));
    b.box(V3(xs + 0.0, y0 + h / 2, zc + w / 2 + 0.06), V3(0.1, h, 0.12));
    b.box(V3(xs + 0.0, y0 + h / 2, zc - w / 2 - 0.06), V3(0.1, h, 0.12));
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.cyl(V3(xs + 0.025, y0 + h * 0.48, zc + w * 0.36), V3(xs + 0.1, y0 + h * 0.48, zc + w * 0.36), 0.016, 6);  // handle
    b.box(V3(xs + 0.04, y0 + h * 0.48, zc + w * 0.36), V3(0.012, 0.16, 0.04));
    for (double y : {0.25, 0.75}) b.box(V3(xs + 0.035, y0 + h * y, zc - w / 2 + 0.1), V3(0.01, 0.04, 0.2));  // hinge straps
}

// Roofing over a plane: A is the low corner, U along the eave, Vv up the slope.
// mat: 0 green tin, 1 red tin, 2 rust tin, 3 shakes, 4 slate
void roofPlane(B& b, const V3& A, const V3& U, const V3& Vv, int mat, uint32_t seed, const V3& hint) {
    V3 n = norm(cross(U, Vv));
    if (dot(n, hint) < 0) n = -n;
    const double Lu = len(U), Lv = len(Vv);
    if (mat <= 2) {
        const int panels = std::max(1, (int)std::lround(Lu / 1.0));
        for (int k = 0; k < panels; k++) {
            V3 base = mat == 0 ? V3(0.2, 0.36, 0.25) : mat == 1 ? V3(0.5, 0.2, 0.15) : V3(0.42, 0.27, 0.19);
            base = base * (0.88 + 0.24 * hash01(seed, k)) ;
            base = mix(base, rustCol(seed, k), mat == 2 ? 0.25 * hash01(seed + 1, k) : 0.08);
            b.style(base, METAL);
            corrug(b, A + U * ((double)k / panels) + n * 0.03, U / panels * 0.985, Vv, 0.19, 0.02, n, 4, true);
        }
    } else {
        b.style(timber(3, seed, 1) * 0.8, WOOD);
        b.beam(A + Vv * 0.5 + n * 0.015, A + U + Vv * 0.5 + n * 0.015, Lv, 0.03, n);
        const int rows = std::max(2, (int)std::lround(Lv / (mat == 3 ? 0.2 : 0.17)));
        const V3 ud = norm(U);
        for (int i = 0; i < rows; i++) {
            const V3 c = A + Vv * ((i + 0.5) / rows) + n * (0.035 + i * 0.0035);
            V3 col = mat == 3 ? timber(1, seed, 10 + i, 0.16) * (i % 3 == 0 ? 0.8 : 1.0) : mix(V3(0.3, 0.32, 0.33), V3(0.4, 0.38, 0.34), hash01(seed, i)) * (0.8 + 0.3 * hash01(seed + 1, i));
            b.style(col, mat == 3 ? WOOD : ROCK);
            b.beam(c - ud * (Lu * 0.0), c + U, Lv / rows * 1.35, 0.03, n);
            b.color(col * 0.55);
            b.beam(c + Vv * (-0.5 / rows * 1.0) - Vv * (0.5 / rows * 0.0), c + U + Vv * (-0.5 / rows), 0.03, 0.02, n);
        }
    }
}

// A gable roof, ridge along local z (length L), eaves at x = +-s at height y0, rise, overhang ov.
void gableRoof(B& b, double s, double L, double y0, double rise, double ov, int mat, const V3& trim, uint32_t seed) {
    const double sl = rise / s;
    for (int sx = -1; sx <= 1; sx += 2) {
        const V3 A(sx * (s + ov), y0 - ov * sl, -L / 2), U(0, 0, L), Vv(-sx * (s + ov), rise + ov * sl, 0);
        roofPlane(b, A, U, Vv, mat, seed + sx, V3(sx * sl, 1, 0));
    }
    // Ridge cap, fascia and barge boards.
    const V3 nn(0, 1, 0);
    b.style(trim, WOOD);
    for (int sx = -1; sx <= 1; sx += 2) {
        const V3 nrm = norm(V3(sx * rise, s, 0));
        b.beam(V3(sx * 0.2, y0 + rise + 0.05, -L / 2 - 0.02), V3(sx * 0.2, y0 + rise + 0.05, L / 2 + 0.02), 0.45, 0.03, nrm);
        b.beam(V3(sx * (s + ov), y0 - ov * sl - 0.04, -L / 2), V3(sx * (s + ov), y0 - ov * sl - 0.04, L / 2), 0.03, 0.14, V3(0, 1, 0));
        for (int e = -1; e <= 1; e += 2)
            b.beam(V3(sx * (s + ov), y0 - ov * sl + 0.0, e * L / 2), V3(0, y0 + rise + 0.03, e * L / 2), 0.035, 0.17, nrm);
    }
    (void)nn;
}

// A pipe through a roof: a stovepipe with collar and rain cap.
void stovepipe(B& b, const V3& at, double h, const V3& col) {
    b.style(col, METAL);
    b.lathe(at, {V2(0.0, 0.0), V2(0.13, 0.0), V2(0.12, 0.06), V2(0.07, 0.08), V2(0.065, h - 0.1), V2(0.09, h - 0.08), V2(0.085, h - 0.02), V2(0.0, h)}, 10, false);
    b.color(col * 0.7);
    b.box(at + V3(0, h * 0.4, 0), V3(0.2, 0.025, 0.2));
    b.cone(at + V3(0, h + 0.04, 0), at + V3(0, h + 0.14, 0), 0.14, 0.0, 10);
    for (int k = 0; k < 3; k++) {
        const double a = k * 2 * PI / 3;
        b.cyl(at + V3(0.06 * std::cos(a), h - 0.03, 0.06 * std::sin(a)), at + V3(0.06 * std::cos(a), h + 0.045, 0.06 * std::sin(a)), 0.008, 4);
    }
}

// A stone chimney against a wall (local x outward from the wall surface at 0, centred z=0).
void chimney(B& b, double width, double depth, double top, uint32_t seed) {
    b.style(V3(0.3, 0.29, 0.27), ROCK);
    b.box(V3(depth / 2 - 0.05, top / 2, 0), V3(depth - 0.1, top, width - 0.1));
    b.style(V3(0.3, 0.28, 0.26), ROCK);
    b.box(V3(depth / 2 - 0.05, 0.12, 0), V3(depth + 0.08, 0.24, width + 0.1));  // footing
    // stones on the three outer faces
    b.at(V3(depth - 0.1, 0, 0), 0, [&] { stoneWall(b, width, 0.0, top, 0.5, seed); });
    for (int s = -1; s <= 1; s += 2) b.at(V3(depth / 2 - 0.05, 0, s * (width / 2 - 0.05)), s > 0 ? -90 : 90, [&] { stoneWall(b, depth - 0.1, 0.0, top, 0.5, seed + 7 + s); });
    // cap and flue
    b.style(V3(0.36, 0.35, 0.33), CONCRETE);
    b.box(V3(depth / 2 - 0.05, top + 0.04, 0), V3(depth + 0.04, 0.08, width + 0.04), 0.01);
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    b.box(V3(depth / 2 - 0.05, top + 0.081, 0), V3(depth * 0.45, 0.01, width * 0.45));
}

// A low porch deck out from the front wall to x = 3.2, with steps; posts hold `roof`.
void porch(B& b, const V3& wood, bool post, double yRoof, uint32_t seed) {
    for (int i = 0; i < 6; i++) {
        b.style(jit(wood, seed, i, 0.12), WOOD);
        b.box(V3(2.2 + 0.5 * 0 + 0.0 + i * 0.165 + 0.083, 0.24, 0), V3(0.16, 0.05, 4.2));
    }
    b.style(wood * 0.7, WOOD);
    b.box(V3(2.7, 0.14, 2.05), V3(1.0, 0.2, 0.1));
    b.box(V3(2.7, 0.14, -2.05), V3(1.0, 0.2, 0.1));
    b.box(V3(3.14, 0.14, 0), V3(0.1, 0.2, 4.2));
    b.style(V3(0.45, 0.43, 0.4), ROCK);
    for (int sz = -1; sz <= 1; sz += 2)
        for (double x : {2.5, 3.1}) b.box(V3(x, 0.07, sz * 1.9), V3(0.3, 0.14, 0.3));
    if (post) {
        b.style(wood * 0.85, WOOD);
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(3.06, (0.26 + yRoof) / 2, sz * 1.95), V3(0.14, yRoof - 0.26, 0.14), 0.01);
    }
}

// -------- A: round-log cabin with a stone chimney --------

void logCabin(B& b, const P& p) {
    const uint32_t seed = 5;
    const int kind = p.i("wood");
    const int mat = p.i("roof");
    const bool chim = p.i("chimney"), por = p.i("porch");
    const std::vector<Fix> fix = {{0, 0.0, 0.95, 0.27, 2.3, true},  {0, 1.65, 0.8, 1.15, 1.95, false}, {0, -1.65, 0.8, 1.15, 1.95, false},
                                  {3, 0.4, 0.8, 1.15, 1.95, false}, {1, 0.0, 0.7, 1.2, 1.8, false}};
    // Footing stones.
    b.style(V3(0.42, 0.4, 0.37), ROCK);
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * kFx, 0.08, 0), V3(0.42, 0.17, 5.3));
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, 0.08, sz * kFz), V3(4.3, 0.17, 0.42));
    // Walls.
    const double r = 0.135, pitch = 0.245;
    const int courses = 10;
    for (int f = 0; f < 4; f++) {
        V3 pos;
        double yaw, L;
        faceFrame(f, pos, yaw, L);
        const bool longWall = f < 2;
        const double ext = 0.15;
        b.at(pos, yaw, [&] {
            for (int i = 0; i < courses; i++) {
                const double y = 0.3 + i * pitch + (longWall ? 0 : pitch / 2);
                // cut the openings
                std::vector<std::pair<double, double>> segs = {{-L / 2 - ext, L / 2 + ext}};
                for (const Fix& fx : fix) {
                    if (fx.face != f) continue;
                    if (!(y + r > fx.y0 + 0.02 && y - r < fx.y1 - 0.02)) continue;
                    std::vector<std::pair<double, double>> ns;
                    for (auto& sg : segs) {
                        const double a = fx.t - fx.w / 2, e = fx.t + fx.w / 2;
                        if (e <= sg.first || a >= sg.second) ns.push_back(sg);
                        else {
                            if (a - sg.first > 0.08) ns.push_back({sg.first, a});
                            if (sg.second - e > 0.08) ns.push_back({e, sg.second});
                        }
                    }
                    segs = ns;
                }
                int k = 0;
                for (auto& sg : segs) {
                    b.style(timber(kind, seed + f, i * 7 + k, 0.13), WOOD);
                    b.cyl(V3(0, y, sg.first), V3(0, y, sg.second), r, 8);
                    // the notched crossing: a darker saddle where the log lies on the one below
                    k++;
                }
            }
            for (const Fix& fx : fix) {
                if (fx.face != f) continue;
                if (fx.door) plankDoor(b, r - 0.02, fx.t, fx.y0, fx.w, fx.y1 - fx.y0, timber(kind == 3 ? 4 : 3, seed, 70), seed);
                else window(b, r - 0.01, fx.t, (fx.y0 + fx.y1) / 2, fx.w, fx.y1 - fx.y0, timber(2, seed, 80), V3(0.2, 0.3, 0.22), f == 0);
            }
        });
    }
    // Gable logs, shrinking up to the ridge.
    const double y0 = 2.72, rise = 1.34, s = 2.2;
    for (int e = -1; e <= 1; e += 2) {
        for (int i = 0; i < 6; i++) {
            const double y = 0.3 + courses * pitch + (courses % 2 ? pitch / 2 : 0) + i * pitch - (courses % 2 ? 0 : 0);
            const double yy = y + (e > 0 ? 0 : 0);
            const double half = std::min(s + 0.12, s - (yy + r + 0.1 - y0) / (rise / s));
            if (half < 0.25) continue;
            b.style(timber(kind, seed + 9, i + 5 * (e + 1), 0.13), WOOD);
            b.cyl(V3(-half, yy, e * kFz), V3(half, yy, e * kFz), r, 8);
        }
    }
    // Roof.
    gableRoof(b, s, 6.0, y0, rise, 0.38, mat, timber(3, seed, 90), seed);
    b.style(timber(kind, seed, 95), WOOD);
    b.cyl(V3(0, y0 + rise - 0.05, -3.0), V3(0, y0 + rise - 0.05, 3.0), 0.1, 8);  // ridge log
    // Chimney on the +z gable end.
    if (chim) b.at(V3(0, 0, kFz + r - 0.02), -90, [&] { chimney(b, 0.95, 0.5, y0 + rise + 0.5, seed); });
    // Porch.
    if (por) {
        porch(b, timber(kind, seed, 100), true, 2.55, seed);
        b.style(V3(0.2, 0.36, 0.25), METAL);
        // porch roof: a lean-to sloping down from the front wall
        const V3 A(3.2, 2.28, -2.3), U(0, 0, 4.6), Vv(-1.05, 0.28, 0);
        roofPlane(b, A, U, Vv, mat, seed + 3, V3(0.3, 1, 0));
    }
}

// -------- B: board-and-batten shack, mono-pitch tin roof, stovepipe --------

void shack(B& b, const P& p) {
    const uint32_t seed = 17;
    const int kind = p.i("wood");
    const int mat = p.i("roof");
    const bool pipe = p.i("chimney"), por = p.i("porch");
    auto yTop = [](double x) { return 2.35 + (x + 2.2) / 4.4 * 0.65; };  // wall top, rising to the front
    const std::vector<Fix> fix = {{0, 0.0, 0.9, 0.22, 2.2, true}, {0, -1.7, 0.7, 1.2, 1.9, false}, {3, 0.3, 0.7, 1.2, 1.9, false}, {2, -0.3, 0.7, 1.2, 1.9, false}};
    // Floor/sill frame and posts.
    b.style(timber(3, seed, 1), WOOD);
    b.box(V3(0, 0.1, 0), V3(4.36, 0.2, 5.36));
    // Dark core (so the gaps between boards read dark).
    b.style(V3(0.1, 0.08, 0.06), MATTE);
    b.box(V3(0, 1.25, 0), V3(4.1, 2.1, 5.1));
    // Walls: vertical boards.
    for (int f = 0; f < 4; f++) {
        V3 pos;
        double yaw, L;
        faceFrame(f, pos, yaw, L);
        b.at(pos, yaw, [&] {
            b.push();
            b.move(0.06, 0, 0);
            const double Lw = f < 2 ? 5.3 : 4.4;
            const int n = (int)std::lround(Lw / 0.195);
            const double bw = Lw / n;
            for (int i = 0; i < n; i++) {
                const double z = -Lw / 2 + (i + 0.5) * bw;
                // height at this board
                double wx;  // world x of the board
                if (f == 0) wx = 2.2;
                else if (f == 1) wx = -2.2;
                else wx = (f == 2 ? -1 : 1) * z * -1.0;  // local z -> world x: f==2 (yaw -90): z -> -x ; f==3 (yaw 90): z -> +x
                if (f == 2) wx = -z;
                if (f == 3) wx = z;
                const double top = yTop(wx);
                double yb = 0.2;
                std::vector<std::pair<double, double>> pieces = {{yb, top}};
                for (const Fix& fx : fix) {
                    if (fx.face != f) continue;
                    if (z + bw / 2 <= fx.t - fx.w / 2 || z - bw / 2 >= fx.t + fx.w / 2) continue;
                    std::vector<std::pair<double, double>> ns;
                    for (auto& pc : pieces) {
                        if (fx.y1 <= pc.first || fx.y0 >= pc.second) ns.push_back(pc);
                        else {
                            if (fx.y0 - pc.first > 0.05) ns.push_back({pc.first, fx.y0});
                            if (pc.second - fx.y1 > 0.05) ns.push_back({fx.y1, pc.second});
                        }
                    }
                    pieces = ns;
                }
                for (auto& pc : pieces) {
                    b.style(timber(kind, seed + f, i * 3 + (int)pc.first, 0.17), WOOD);
                    b.box(V3(0.0, (pc.first + pc.second) / 2, z), V3(0.032, pc.second - pc.first, bw - 0.012));
                }
                if (i % 3 == 1 && pieces.size() == 1) {
                    b.style(timber(kind, seed + 20, i, 0.12) * 0.82, WOOD);
                    b.box(V3(0.026, (pieces[0].first + pieces[0].second) / 2, z + bw / 2), V3(0.025, pieces[0].second - pieces[0].first, 0.06));
                }
            }
            // corner trim
            b.style(timber(3, seed, 50 + f) * 1.1, WOOD);
            for (int e = -1; e <= 1; e += 2) b.box(V3(0.02, 1.4, e * (Lw / 2 + 0.02)), V3(0.05, 2.5, 0.09));
            for (const Fix& fx : fix) {
                if (fx.face != f) continue;
                if (fx.door) plankDoor(b, 0.0, fx.t, fx.y0, fx.w, fx.y1 - fx.y0, timber(3, seed, 60), seed);
                else window(b, 0.02, fx.t, (fx.y0 + fx.y1) / 2, fx.w, fx.y1 - fx.y0, timber(1, seed, 61) * 1.15, timber(4, seed, 62) * 0.9, true);
            }
            b.pop();
        });
    }
    // Mono-pitch roof, falling to the back.
    const double ov = 0.38;
    const double xF = 2.2 + ov, xB = -2.2 - ov;
    const double yF = yTop(2.2) + 0.1, yB = yTop(-2.2) + 0.1;
    const double slope = (yF - yB) / 4.4;
    const double yFe = yF + ov * slope, yBe = yB - ov * slope;
    roofPlane(b, V3(xB, yBe, -3.0), V3(0, 0, 6.0), V3(xF - xB, yFe - yBe, 0), mat, seed, V3(-slope, 1, 0));
    b.style(timber(3, seed, 70), WOOD);
    const V3 nrm = norm(V3(-(yFe - yBe), xF - xB, 0));
    b.beam(V3(xF, yFe - 0.05, -3.0), V3(xF, yFe - 0.05, 3.0), 0.03, 0.16, V3(0, 1, 0));
    b.beam(V3(xB, yBe - 0.05, -3.0), V3(xB, yBe - 0.05, 3.0), 0.03, 0.16, V3(0, 1, 0));
    for (int e = -1; e <= 1; e += 2) b.beam(V3(xB, yBe - 0.02, e * 3.0), V3(xF, yFe - 0.02, e * 3.0), 0.035, 0.17, nrm);
    // The top of the side walls under the roof: fill the wedge with the same boards (done by yTop); stovepipe.
    if (pipe) {
        const double x = -1.0, z = -1.1;
        const double yR = yB + (x + 2.2) * slope + 0.14;
        stovepipe(b, V3(x, yR - 0.1, z), 1.4, V3(0.14, 0.14, 0.15));
    }
    if (por) {
        porch(b, timber(kind, seed, 100), true, 2.9, seed);
        // roof extension over the porch
        roofPlane(b, V3(3.2, yFe - 0.45, -2.3), V3(0, 0, 4.6), V3(-0.9, 0.45, 0), mat, seed + 1, V3(0.3, 1, 0));
    }
    // a plank step and a barrel/stack: a leaning firewood log bundle by the door? (kept off: cabin collider)
}

// -------- C: stone-and-timber alpine hut --------

void alpineHut(B& b, const P& p) {
    const uint32_t seed = 29;
    const int kind = p.i("wood");
    const int mat = p.i("roof");
    const bool chim = p.i("chimney"), por = p.i("porch");
    const std::vector<Fix> fix = {{0, 0.0, 0.95, 0.2, 2.3, true}, {0, 1.65, 0.7, 1.35, 2.05, false}, {0, -1.65, 0.7, 1.35, 2.05, false},
                                  {2, 0.4, 0.7, 1.35, 2.05, false}, {3, -0.4, 0.7, 1.35, 2.05, false}, {1, 0.0, 0.6, 1.3, 1.9, false}};
    // Stone base 1.1 m: a core, and rows of stones on the faces. The door sits on a stone step.
    const double bh = 1.1;
    b.style(V3(0.27, 0.26, 0.24), ROCK);
    b.box(V3(0, bh / 2, 0), V3(4.34, bh, 5.34));
    for (int f = 0; f < 4; f++) {
        V3 pos;
        double yaw, L;
        faceFrame(f, pos, yaw, L);
        const double off = f < 2 ? 2.2 - kFx : 2.7 - kFz;
        b.at(pos + V3(0, 0, 0), yaw, [&] {
            b.push();
            b.move(0.12 + off - 0.12, 0, 0);
            // the door and the windows' cuts through the base are small: the door rises from y=1.1 anyway
            stoneWall(b, f < 2 ? 5.34 : 4.34, 0.0, bh, 0.55, seed + f, 0.09, f == 0 ? -0.62 : 0, f == 0 ? 0.62 : 0, 0.0 + (f == 0 ? 1.2 : 0));
            b.pop();
        });
    }
    // Squared timber courses above.
    const double ch = 0.19, th = 0.2;
    const int courses = 6;
    for (int f = 0; f < 4; f++) {
        V3 pos;
        double yaw, L;
        faceFrame(f, pos, yaw, L);
        const bool longWall = f < 2;
        const double ext = 0.1;
        b.at(pos + V3(0, 0, 0), yaw, [&] {
            for (int i = 0; i < courses; i++) {
                const double y = bh + 0.1 + i * 0.205 + (longWall ? 0 : 0.1);
                std::vector<std::pair<double, double>> segs = {{-L / 2 - ext, L / 2 + ext}};
                for (const Fix& fx : fix) {
                    if (fx.face != f) continue;
                    if (!(y + ch / 2 > fx.y0 + 0.02 && y - ch / 2 < fx.y1 - 0.02)) continue;
                    std::vector<std::pair<double, double>> ns;
                    for (auto& sg : segs) {
                        const double a = fx.t - fx.w / 2, e = fx.t + fx.w / 2;
                        if (e <= sg.first || a >= sg.second) ns.push_back(sg);
                        else {
                            if (a - sg.first > 0.08) ns.push_back({sg.first, a});
                            if (sg.second - e > 0.08) ns.push_back({e, sg.second});
                        }
                    }
                    segs = ns;
                }
                int k = 0;
                for (auto& sg : segs) {
                    b.style(timber(kind, seed + f, i * 7 + k, 0.12), WOOD);
                    b.box(V3(0.0, y, (sg.first + sg.second) / 2), V3(th, ch, sg.second - sg.first));
                    k++;
                }
            }
            for (const Fix& fx : fix) {
                if (fx.face != f) continue;
                if (fx.door) {
                    plankDoor(b, th / 2 - 0.03, fx.t, fx.y0, fx.w, fx.y1 - fx.y0, timber(3, seed, 70), seed);
                } else
                    window(b, th / 2, fx.t, (fx.y0 + fx.y1) / 2, fx.w, fx.y1 - fx.y0, V3(0.8, 0.78, 0.72), V3(0.22, 0.4, 0.28), true);
            }
        });
    }
    // Door step stone.
    b.style(V3(0.4, 0.38, 0.35), ROCK);
    b.box(V3(2.4, 0.05, 0), V3(0.5, 0.1, 1.5), 0.01);
    // Gables: vertical boards up to the ridge (ridge along x): front/back faces.
    const double s = 2.7, y0 = 2.45, rise = 1.75;
    const double top0 = bh + 0.1 + courses * 0.205 + 0.1;
    for (int e = -1; e <= 1; e += 2) {
        b.at(V3(0, 0, 0), e > 0 ? 0 : 180, [&] {
            for (int k = -13; k <= 13; k++) {
                const double z = k * 0.2;
                const double top = y0 + rise * std::max(0.0, 1 - std::fabs(z) / s) + 0.0;
                if (top - top0 < 0.06) continue;
                b.style(timber(kind, seed + 30, k + 20 * (e + 1), 0.15), WOOD);
                b.box(V3(2.1, (top0 + top) / 2, z), V3(0.03, top - top0, 0.19));
            }
            // a little round window in the gable
            b.style(V3(0.8, 0.78, 0.72), WOOD);
            b.cyl(V3(2.13, y0 + 0.65, 0), V3(2.17, y0 + 0.65, 0), 0.2, 12);
            b.style(V3(0.3, 0.4, 0.45), GLASS);
            b.cyl(V3(2.15, y0 + 0.65, 0), V3(2.185, y0 + 0.65, 0), 0.15, 12);
            // heavy beam under the gable
            b.style(timber(3, seed, 88), WOOD);
            b.box(V3(2.15, top0 - 0.02, 0), V3(0.1, 0.12, 5.5));
        });
    }
    // Roof: ridge along x, so build in a frame turned a quarter (local z = world x).
    b.push();
    b.yaw(90);  // local x -> -z, local z -> +x
    gableRoof(b, s, 5.2, y0, rise, 0.4, mat, timber(3, seed, 90), seed);
    b.pop();
    // Chimney on the back gable.
    if (chim) b.at(V3(-2.2 + 0.0, 0, 0), 180, [&] { chimney(b, 0.95, 0.5, y0 + rise + 0.45, seed + 4); });
    if (por) porch(b, timber(kind, seed, 100), false, 2.5, seed);
    // flower boxes
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(timber(3, seed, 120), WOOD);
        b.box(V3(2.3, 1.18, sz * 1.65), V3(0.18, 0.12, 0.8));
        b.style(V3(0.2, 0.4, 0.15), FOLIAGE);
        b.sphere(V3(2.3, 1.3, sz * 1.65 + 0.2), V3(0.1, 0.07, 0.15), 1);
        b.sphere(V3(2.3, 1.3, sz * 1.65 - 0.2), V3(0.1, 0.07, 0.15), 1);
        b.style(V3(0.75, 0.2, 0.2), FOLIAGE);
        for (int k = 0; k < 3; k++) b.sphere(V3(2.34, 1.36, sz * 1.65 - 0.25 + k * 0.25), V3(0.035, 0.035, 0.035), 0);
    }
}

Reg cabin({"cabin", "CABIN", "BUILDINGS", "Ranger cabins, a fishing shack, a lodge, a mountain hut, a mine office", 5.4,
           {
               {"LOG CABIN", "Round logs with saddle corners, stone chimney, tin or shake roof",
                {choice("roof", "ROOF", 3, {"GREEN TIN", "RED TIN", "RUST TIN", "SHAKES", "SLATE"}), choice("chimney", "CHIMNEY", 1, {"OFF", "ON"}),
                 choice("porch", "PORCH", 1, {"OFF", "ON"}), choice("wood", "WOOD", 0, {"BROWN", "SILVER", "PINE", "DARK", "RED"})},
                logCabin},
               {"PLANK SHACK", "Board-and-batten, mono-pitch tin roof and a stovepipe",
                {choice("roof", "ROOF", 2, {"GREEN TIN", "RED TIN", "RUST TIN", "SHAKES", "SLATE"}), choice("chimney", "STOVEPIPE", 1, {"OFF", "ON"}),
                 choice("porch", "PORCH", 0, {"OFF", "ON"}), choice("wood", "WOOD", 1, {"BROWN", "SILVER", "PINE", "DARK", "RED"})},
                shack},
               {"ALPINE HUT", "Stone base, squared timbers, steep slate roof, shuttered windows",
                {choice("roof", "ROOF", 4, {"GREEN TIN", "RED TIN", "RUST TIN", "SHAKES", "SLATE"}), choice("chimney", "CHIMNEY", 1, {"OFF", "ON"}),
                 choice("porch", "PORCH", 0, {"OFF", "ON"}), choice("wood", "WOOD", 3, {"BROWN", "SILVER", "PINE", "DARK", "RED"})},
                alpineHut},
           }});


// ============================================================================================
// OUTHOUSE
// ============================================================================================

// A crescent moon cut-out, painted black, drawn in the front/up plane (x along the viewer's right).
void moonCutout(B& b, double x, double y, double z, double r, double depth = 0.012) {
    const double d = 0.45 * r, r2 = 0.8 * r;
    const double xs = (r * r - r2 * r2 + d * d) / (2 * d), ys = std::sqrt(r * r - xs * xs);
    const double a1 = std::atan2(ys, xs) * 180 / PI;
    const double a2 = std::atan2(ys, xs - d) * 180 / PI;
    std::vector<V2> poly;
    for (int i = 0; i <= 12; i++) {
        const double a = (a1 + (360 - 2 * a1) * i / 12) * PI / 180;
        poly.push_back(V2(r * std::cos(a), r * std::sin(a)));
    }
    for (int i = 0; i <= 10; i++) {
        const double a = ((360 - a2) - (360 - 2 * a2) * i / 10) * PI / 180;
        poly.push_back(V2(d + r2 * std::cos(a), r2 * std::sin(a)));
    }
    b.push();
    b.move(x, y, z);
    b.yaw(90);
    b.pitch(90);
    b.style(V3(0.02, 0.02, 0.02), MATTE);
    b.extrude(poly, depth);
    b.pop();
}

// A: classic plank privy, mono-pitch roof falling to the back.
void outhousePlank(B& b, const P& p) {
    const uint32_t seed = 41;
    const int kind = p.i("wood");
    const double lean = p("lean");
    b.push();
    b.roll(lean);
    // skids and a step
    b.style(timber(3, seed, 1), WOOD);
    b.box(V3(0, 0.05, 0.5), V3(1.3, 0.1, 0.12));
    b.box(V3(0, 0.05, -0.5), V3(1.3, 0.1, 0.12));
    b.box(V3(0.78, 0.04, 0), V3(0.22, 0.08, 0.7));
    // corner posts
    const double hF = 2.05, hB = 1.85;
    auto topAt = [&](double x) { return hB + (x + 0.6) / 1.2 * (hF - hB); };
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(timber(kind, seed, 3 + sx + sz * 3), WOOD);
            b.box(V3(sx * 0.585, topAt(sx * 0.6) / 2 + 0.05, sz * 0.585), V3(0.08, topAt(sx * 0.6) - 0.05, 0.08));
        }
    // dark core
    b.style(V3(0.06, 0.05, 0.04), MATTE);
    b.box(V3(0, 0.95, 0), V3(1.12, 1.7, 1.12));
    // boards: front and back, sides (sloped tops)
    for (int f = 0; f < 4; f++) {
        for (int i = 0; i < 8; i++) {
            const double t = -0.5 + (i + 0.5) * 1.0 / 8;  // along the wall
            double x, z, top;
            const bool fb = f < 2;
            if (fb) x = f == 0 ? 0.6 : -0.6, z = t, top = f == 0 ? hF : hB;
            else x = t, z = f == 2 ? 0.6 : -0.6, top = topAt(t);
            const double bw = 0.115;
            const double wy0 = 0.1;
            std::vector<std::pair<double, double>> pcs = {{wy0, top}};
            if (f == 0 && std::fabs(t) < 0.4) pcs = {{1.88, top}};  // above the door
            for (auto& pc : pcs) {
                b.style(timber(kind, seed + f, i * 5 + (int)(pc.first * 10), 0.17), WOOD);
                if (fb) b.box(V3(x, (pc.first + pc.second) / 2, z), V3(0.028, pc.second - pc.first, bw));
                else b.box(V3(x, (pc.first + pc.second) / 2, z), V3(bw, pc.second - pc.first, 0.028));
            }
            if (f == 1 || f == 2 || f == 3) {  // battens on the other three
                b.style(timber(kind, seed + 7, i, 0.1) * 0.85, WOOD);
                if (i % 2 == 0) {
                    if (fb) b.box(V3(x * 1.03, (wy0 + top) / 2, z + bw / 2), V3(0.012, top - wy0, 0.04));
                    else b.box(V3(x + bw / 2, (wy0 + top) / 2, z * 1.03), V3(0.04, top - wy0, 0.012));
                }
            }
        }
    }
    // door: planks, Z-brace, moon, latch
    b.style(timber(kind, seed, 90) * 0.85, WOOD);
    for (int i = 0; i < 5; i++) {
        b.style(timber(kind, seed + 3, 20 + i, 0.15) * 0.92, WOOD);
        b.box(V3(0.625, 0.99, -0.4 + (i + 0.5) * 0.16), V3(0.03, 1.78, 0.155));
    }
    b.style(timber(3, seed, 95), WOOD);
    b.beam(V3(0.65, 0.3, -0.33), V3(0.65, 1.65, 0.33), 0.1, 0.025);
    b.box(V3(0.65, 0.3, 0), V3(0.025, 0.1, 0.78));
    b.box(V3(0.65, 1.65, 0), V3(0.025, 0.1, 0.78));
    moonCutout(b, 0.642, 1.42, 0.0, 0.1);
    b.style(V3(0.15, 0.15, 0.15), METAL);
    b.box(V3(0.67, 1.0, 0.3), V3(0.012, 0.14, 0.03));
    b.cyl(V3(0.64, 1.0, 0.3), V3(0.69, 1.0, 0.3), 0.012, 5);
    b.box(V3(0.66, 1.0, -0.32), V3(0.012, 0.03, 0.12));  // wooden latch bar
    // roof: planks with tin, overhanging
    const double ov = 0.18;
    const double yF = hF + 0.1, yB = hB + 0.1;
    const double sl = (yF - yB) / 1.2;
    const V3 A(-0.6 - ov, yB - ov * sl, -0.78), Un(0, 0, 1.56), Vv(1.2 + 2 * ov, (1.2 + 2 * ov) * sl, 0);
    b.style(timber(3, seed, 99), WOOD);
    b.beam(V3(-0.6 - ov, yB - ov * sl - 0.02, -0.78), V3(-0.6 - ov, yB - ov * sl - 0.02, 0.78), 0.03, 0.1);
    b.beam(V3(0.6 + ov, yF + ov * sl - 0.02, -0.78), V3(0.6 + ov, yF + ov * sl - 0.02, 0.78), 0.03, 0.1);
    b.style(kZinc * 0.85, METAL);
    corrug(b, A, Un, Vv, 0.1, 0.012, V3(-sl, 1, 0), 4, true);
    // vent pipe
    b.style(V3(0.25, 0.25, 0.27), METAL);
    b.cyl(V3(-0.35, yB + 0.05, -0.3), V3(-0.35, yB + 0.3, -0.3), 0.035, 7);
    b.cone(V3(-0.35, yB + 0.3, -0.3), V3(-0.35, yB + 0.37, -0.3), 0.06, 0.0, 7);
    b.pop();
}

// B: corrugated tin-clad privy with a small gabled tin roof.
void outhouseTin(B& b, const P& p) {
    const uint32_t seed = 43;
    const V3 col = muted(paint(p.i("paint")), 0.2);
    const double rust = p("rust");
    b.style(V3(0.5, 0.5, 0.52), CONCRETE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.5, 0.06, sz * 0.5), V3(0.2, 0.12, 0.2));
    b.style(timber(3, seed, 1), WOOD);
    b.box(V3(0, 0.15, 0), V3(1.2, 0.06, 1.2));
    // frame posts
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.57, 0.98, sz * 0.57), V3(0.07, 1.7, 0.07));
    const double y0 = 0.17, y1 = 1.78;
    for (int f = 0; f < 4; f++) {
        for (int k = 0; k < 6; k++) {
            const double t = -0.6 + k * 0.2 + 0.0;
            const V3 c = mix(col * (0.9 + 0.2 * hash01(seed, f * 6 + k)), rustCol(seed, f * 6 + k), rust * hash01(seed + 5, f * 6 + k) * 0.8);
            b.style(c, METAL);
            if (f == 0) {
                if (std::fabs(t + 0.1) < 0.41) continue;  // door opening (a plank door goes there)
                corrug(b, V3(0.6, y0, t), V3(0, 0, 0.19), V3(0, y1 - y0, 0), 0.095, 0.012, V3(1, 0, 0), 4, true);
            } else if (f == 1) corrug(b, V3(-0.6, y0, t), V3(0, 0, 0.19), V3(0, y1 - y0, 0), 0.095, 0.012, V3(-1, 0, 0), 4, true);
            else if (f == 2) corrug(b, V3(t, y0, 0.6), V3(0.19, 0, 0), V3(0, y1 - y0, 0), 0.095, 0.012, V3(0, 0, 1), 4, true);
            else corrug(b, V3(t, y0, -0.6), V3(0.19, 0, 0), V3(0, y1 - y0, 0), 0.095, 0.012, V3(0, 0, -1), 4, true);
        }
    }
    // door
    for (int i = 0; i < 4; i++) {
        b.style(timber(0, seed + 1, 10 + i, 0.15), WOOD);
        b.box(V3(0.62, 0.97, -0.5 + (i + 0.5) * 0.2), V3(0.03, 1.62, 0.195));
    }
    b.style(timber(3, seed, 12), WOOD);
    b.box(V3(0.645, 0.4, -0.1), V3(0.02, 0.1, 0.84));
    b.box(V3(0.645, 1.6, -0.1), V3(0.02, 0.1, 0.84));
    b.beam(V3(0.645, 0.4, -0.45), V3(0.645, 1.6, 0.25), 0.09, 0.02);
    moonCutout(b, 0.637, 1.38, -0.1, 0.1);
    b.style(V3(0.15, 0.15, 0.15), METAL);
    b.cyl(V3(0.64, 0.95, 0.2), V3(0.69, 0.95, 0.2), 0.014, 5);
    b.box(V3(0.66, 0.95, 0.2), V3(0.012, 0.14, 0.03));
    // gable roof, ridge along z
    const double s = 0.6, rise = 0.27, ov = 0.14;
    b.push();
    b.move(0, 1.8, 0);
    for (int sx = -1; sx <= 1; sx += 2) {
        const V3 c = mix(kZinc * 0.8, rustCol(seed, 40 + sx), 0.3 + rust * 0.4);
        b.style(c, METAL);
        const double sl = rise / s;
        corrug(b, V3(sx * (s + ov), -ov * sl, -0.76), V3(0, 0, 1.52), V3(-sx * (s + ov), rise + ov * sl, 0), 0.1, 0.012, V3(sx * sl, 1, 0), 4, true);
    }
    b.style(kZinc * 0.7, METAL);
    for (int sx = -1; sx <= 1; sx += 2) b.beam(V3(sx * 0.12, rise + 0.015, -0.78), V3(sx * 0.12, rise + 0.015, 0.78), 0.3, 0.015, V3(sx * rise, s, 0));
    b.pop();
    // roof vent
    b.style(V3(0.25, 0.25, 0.27), METAL);
    b.lathe(V3(-0.2, 2.02, 0.2), {V2(0.04, 0.0), V2(0.04, 0.12), V2(0.09, 0.14), V2(0.09, 0.17), V2(0.0, 0.21)}, 8, false);
}

// C: rustic round-log privy with a shake gable roof.
void outhouseLog(B& b, const P& p) {
    const uint32_t seed = 40 + p.i("seed");
    const double r = 0.07, pitch = 0.118;
    b.style(V3(0.45, 0.43, 0.4), ROCK);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.5, 0.05, sz * 0.5), V3(0.26, 0.1, 0.26));
    const int courses = 13;
    for (int i = 0; i < courses; i++) {
        const double y = 0.18 + i * pitch;
        for (int f = 0; f < 4; f++) {
            const bool fb = f < 2;
            const double off = fb ? 0.0 : pitch / 2;
            const double line = (f % 2 == 0 ? 1 : -1) * 0.53;
            const double ext = 0.1;
            std::vector<std::pair<double, double>> segs = {{-0.6 - ext, 0.6 + ext}};
            if (f == 0 && y < 1.6) segs = {{-0.6 - ext, -0.38}, {0.38, 0.6 + ext}};
            for (auto& sg : segs) {
                b.style(timber(i % 3 == 0 ? 0 : 1, seed + f, i * 5 + (int)(sg.first * 10), 0.16) * 0.95, WOOD);
                const double yy = y + off + 0.0;
                if (fb) b.cyl(V3(line, yy, sg.first), V3(line, yy, sg.second), r * (0.92 + 0.16 * hash01(seed, i * 4 + f)), 6);
                else b.cyl(V3(sg.first, yy, line), V3(sg.second, yy, line), r * (0.92 + 0.16 * hash01(seed, i * 4 + f)), 6);
            }
        }
    }
    // door in the opening
    for (int i = 0; i < 4; i++) {
        b.style(timber(3, seed + 1, 30 + i, 0.18), WOOD);
        b.box(V3(0.55, 0.92, -0.3 + (i + 0.5) * 0.15), V3(0.03, 1.62, 0.145));
    }
    b.style(timber(3, seed, 33) * 0.8, WOOD);
    b.box(V3(0.58, 0.4, 0), V3(0.025, 0.09, 0.7));
    b.box(V3(0.58, 1.5, 0), V3(0.025, 0.09, 0.7));
    b.beam(V3(0.58, 0.4, -0.3), V3(0.58, 1.5, 0.3), 0.08, 0.02);
    moonCutout(b, 0.572, 1.3, 0.0, 0.1);
    b.style(V3(0.12, 0.12, 0.12), METAL);
    b.box(V3(0.6, 0.98, 0.24), V3(0.012, 0.12, 0.03));
    // gable above the logs (boards), shingle roof ridge along z
    const double top = 0.18 + courses * pitch + 0.02, rise = 0.4, s = 0.62;
    b.style(timber(1, seed, 50) * 0.8, WOOD);
    for (int e = -1; e <= 1; e += 2) {
        b.push();
        b.move(0, 0, e * 0.53);
        b.extrude({V2(-0.6, top), V2(0.6, top), V2(0, top + rise - 0.02)}, 0.04);
        b.pop();
    }
    const double ov = 0.14;
    gableRoof(b, s, 1.5, top + 0.02, rise, ov, 3, timber(3, seed, 60), seed);
    // cut ends of the ridge pole and a finial
    b.style(timber(0, seed, 70), WOOD);
    b.cyl(V3(0, top + rise, -0.78), V3(0, top + rise, 0.78), 0.04, 6);
}

Reg outhouse({"outhouse", "OUTHOUSE", "BUILDINGS", "The camp privy, door and moon facing +X", 2.3,
              {
                  {"PLANK PRIVY", "Weathered boards and battens, tin shed roof, moon door",
                   {choice("wood", "WOOD", 1, {"BROWN", "SILVER", "PINE", "DARK", "RED"}), num("lean", "LEAN", 1.2, 0, 5, 0.1)}, outhousePlank},
                  {"TIN PRIVY", "Corrugated iron walls, gabled tin roof, roof vent",
                   {paintParam(9), num("rust", "RUST", 0.35, 0, 1, 0.05)}, outhouseTin},
                  {"LOG PRIVY", "Rustic round logs, shake roof", {seedParam(3)}, outhouseLog},
              }});

// ============================================================================================
// TENT
// ============================================================================================

// A stake with a bit of guy line to a point on the tent.
void guyLine(B& b, const V3& from, const V3& stake, const V3& cord) {
    b.style(cord, MATTE);
    const V3 mid = (from + stake) * 0.5 - V3(0, 0.015, 0);
    b.tube({from, mid, stake + V3(0, 0.04, 0)}, 0.006, 4);
    b.style(V3(0.55, 0.55, 0.58), METAL);
    const V3 d = norm(stake - from);
    b.cone(stake + d * 0.05 + V3(0, 0.1, 0), stake - d * 0.02 - V3(0, 0.02, 0), 0.008, 0.002, 4);
}

// A: dome tent with a fly, a front vestibule and guy lines.
void tentDome(B& b, const P& p) {
    const V3 col = muted(paint(p.i("paint")), 0.25);
    // ground sheet
    b.style(V3(0.25, 0.27, 0.22), MATTE);
    b.extrudeUp(roundedRect(2.6, 1.9, 0.3, 4), 0.01);
    // a hemisphere profile
    std::vector<V2> hemi;
    for (int i = 0; i <= 8; i++) {
        const double a = PI / 2 * i / 8;
        hemi.push_back(V2(std::cos(a), std::sin(a)));
    }
    // fly: the dome, with a darker skirt
    b.push();
    b.scale(V3(1.12, 0.98, 0.88));
    b.style(col, MATTE);
    b.lathe(V3(0, -0.02, 0), hemi, 20);
    b.pop();
    b.style(col * 0.6, MATTE);
    b.push();
    b.scale(V3(1.125, 1.0, 0.885));
    b.lathe(V3(0, -0.02, 0), {V2(1.0, 0.0), V2(0.99, 0.09), V2(0.995, 0.0)}, 20);
    b.pop();
    // vestibule: a short tunnel out of the front, lathed about +x, with a dark door
    b.push();
    b.move(0.85, 0.0, 0);
    b.pitch(-90);
    b.scale(V3(0.85, 1.0, 1.0));
    b.style(col * 0.95, MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.5, 0.0), V2(0.5, 0.2), V2(0.46, 0.4), V2(0.36, 0.56), V2(0.27, 0.62)}, 14);
    b.style(V3(0.02, 0.02, 0.025), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.27, 0.62), V2(0.0, 0.62)}, 14);
    b.pop();
    // poles: two crossing hoops over the fly
    b.style(V3(0.12, 0.12, 0.13), METAL);
    for (int k = 0; k < 2; k++) {
        std::vector<V3> hp;
        for (int i = 0; i <= 16; i++) {
            const double t = PI * i / 16;
            if (k == 0) hp.push_back(V3(-0.1, -0.02 + 0.985 * std::sin(t) * 1.0, 0.88 * 1.005 * std::cos(t) * std::sqrt(std::max(0.0, 1 - 0.0))));
            else hp.push_back(V3(1.12 * 1.005 * std::cos(t), -0.02 + 0.985 * std::sin(t), 0.0));
        }
        b.tube(hp, 0.012, 5);
    }
    // guys and stakes
    const V3 cord(0.9, 0.5, 0.1);
    guyLine(b, V3(-0.95, 0.4, 0.0), V3(-1.9, 0.0, 0.0), cord);
    guyLine(b, V3(0.5, 0.55, 0.65), V3(0.9, 0.0, 1.65), cord);
    guyLine(b, V3(0.5, 0.55, -0.65), V3(0.9, 0.0, -1.65), cord);
    guyLine(b, V3(-0.55, 0.45, 0.65), V3(-1.1, 0.0, 1.6), cord);
    guyLine(b, V3(-0.55, 0.45, -0.65), V3(-1.1, 0.0, -1.6), cord);
    // a boot or two by the door
    b.style(V3(0.2, 0.15, 0.1), MATTE);
    b.box(V3(1.62, 0.06, 0.42), V3(0.26, 0.12, 0.09), 0.02);
    b.box(V3(1.66, 0.06, 0.58), V3(0.26, 0.12, 0.09), 0.02);
}

// B: canvas ridge tent: a house-shaped prism, poles at the ends, open flaps.
void tentRidge(B& b, const P& p) {
    const uint32_t seed = 53;
    const V3 col = mix(muted(paint(p.i("paint")), 0.5), V3(0.62, 0.56, 0.42), 0.55);
    const double L = 2.1, W = 0.8, wallH = 0.3, H = 1.3;
    b.style(V3(0.22, 0.22, 0.2), MATTE);
    b.extrudeUp(roundedRect(L + 0.4, 2 * W + 0.4, 0.06, 2), 0.01);
    // the prism: canvas
    b.style(col, MATTE);
    b.push();
    b.yaw(90);  // local z -> +x: the extrusion runs along the ridge
    b.extrude({V2(-W, 0.0), V2(W, 0.0), V2(W, wallH), V2(0.0, H), V2(-W, wallH)}, L);
    b.pop();
    // seams along the roof, a darker hem on the walls
    b.style(col * 0.82, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.beam(V3(-L / 2, wallH + 0.02, sz * (W + 0.003)), V3(L / 2, wallH + 0.02, sz * (W + 0.003)), 0.03, 0.03);
        for (int k = -2; k <= 2; k++) {
            const double x = k * 0.4;
            b.beam(V3(x, wallH + 0.01, sz * (W + 0.002)), V3(x, wallH + 0.02, sz * 0.0 + sz * 0.02), 0.02, 0.012);
            b.beam(V3(x, wallH + (H - wallH) * 0.02, sz * (W * 0.98)), V3(x, H - 0.03, sz * 0.03), 0.02, 0.01, V3(0, 1, 0));
        }
    }
    // front: open doorway with two flaps tied back
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.push();
    b.move(L / 2 + 0.006, 0, 0);
    b.yaw(90);
    b.extrude({V2(-0.4, 0.0), V2(0.4, 0.0), V2(0.4, 0.3), V2(0.0, 1.05), V2(-0.4, 0.3)}, 0.012);
    b.pop();
    b.style(col * 0.95, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.push();
        b.move(L / 2 + 0.02, 0, sz * 0.38);
        b.yaw(90);
        // flap: a triangle pulled to the side
        b.extrude({V2(0.0, 0.0), V2(sz * 0.14, 0.0), V2(sz * 0.2, 0.28), V2(0.0, 1.0)}, 0.012);
        b.pop();
    }
    // ridge pole and A-poles at both ends
    b.style(timber(0, seed, 1), WOOD);
    b.cyl(V3(-L / 2 - 0.25, H + 0.02, 0), V3(L / 2 + 0.25, H + 0.02, 0), 0.025, 6);
    for (int e = -1; e <= 1; e += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.cyl(V3(e * (L / 2 + 0.22), 0.0, sz * 0.38), V3(e * (L / 2 + 0.22), H + 0.04, 0), 0.02, 5);
        }
    // guys and stakes
    const V3 cord(0.62, 0.52, 0.34);
    for (int e = -1; e <= 1; e += 2) {
        guyLine(b, V3(e * (L / 2 + 0.25), H, 0), V3(e * 1.95, 0.0, 0), cord);
        for (int sz = -1; sz <= 1; sz += 2) guyLine(b, V3(e * 0.6, wallH + 0.05, sz * W), V3(e * 0.9, 0.0, sz * 1.5), cord);
    }
    // bedroll visible through the door
    b.style(V3(0.35, 0.18, 0.12), MATTE);
    b.cyl(V3(L / 2 - 0.1, 0.1, -0.25), V3(L / 2 - 0.1, 0.1, 0.25), 0.1, 8);
    // wooden mallet at the door
    b.style(timber(0, seed, 5), WOOD);
    b.cyl(V3(L / 2 + 0.5, 0.03, 0.6), V3(L / 2 + 0.75, 0.03, 0.7), 0.012, 5);
    b.box(V3(L / 2 + 0.72, 0.045, 0.69), V3(0.05, 0.06, 0.05));
}

// C: a-frame tarp shelter between two poles with a ridge line.
void tentTarp(B& b, const P& p) {
    const uint32_t seed = 57;
    const V3 col = muted(paint(p.i("paint")), 0.1);
    const double L = 2.3, H = 1.05, W = 0.85;
    b.style(V3(0.25, 0.27, 0.22), MATTE);
    b.extrudeUp(roundedRect(2.6, 1.9, 0.2, 3), 0.01);
    // poles
    b.style(timber(0, seed, 1), WOOD);
    for (int e = -1; e <= 1; e += 2) {
        b.cyl(V3(e * (L / 2 + 0.1), 0.0, 0), V3(e * (L / 2 + 0.06), H + 0.08, 0), 0.025, 6);
        // back-stay
        b.cyl(V3(e * (L / 2 + 0.06), H + 0.0, 0), V3(e * (L / 2 + 0.7), 0.0, 0), 0.012, 5);
    }
    // the ridge line
    b.style(V3(0.85, 0.5, 0.1), MATTE);
    b.tube({V3(-L / 2 - 0.06, H + 0.06, 0), V3(0.0, H + 0.04, 0), V3(L / 2 + 0.06, H + 0.06, 0)}, 0.008, 4);
    // the tarp: two panels
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(col * (sz > 0 ? 1.0 : 0.9), MATTE);
        const V3 top(0, H + 0.05, sz * 0.015), bot(0, 0.07, sz * W);
        const V3 nrm = norm(V3(0, W, sz * (H - 0.02)));
        b.beam(mix(top, bot, 0.5) - V3(L / 2, 0, 0), mix(top, bot, 0.5) + V3(L / 2, 0, 0), len(bot - top), 0.014, nrm, 0.003);
        // hem seam
        b.color(col * 0.7);
        b.beam(mix(top, bot, 0.97) - V3(L / 2, 0, 0) + nrm * 0.004, mix(top, bot, 0.97) + V3(L / 2, 0, 0) + nrm * 0.004, 0.05, 0.016, nrm);
    }
    // eyelets and pegs
    const V3 cord(0.9, 0.5, 0.1);
    for (int sz = -1; sz <= 1; sz += 2)
        for (int e = -1; e <= 1; e += 2) guyLine(b, V3(e * (L / 2 - 0.05), 0.08, sz * W), V3(e * (L / 2 + 0.4), 0.0, sz * (W + 0.65)), cord);
    // bedroll, pack, billy can
    b.style(V3(0.3, 0.3, 0.22), MATTE);
    b.box(V3(0.3, 0.07, 0.0), V3(1.4, 0.06, 0.55), 0.03);
    b.style(V3(0.35, 0.18, 0.12), MATTE);
    b.cyl(V3(-0.4, 0.13, -0.15), V3(-0.4, 0.13, 0.25), 0.1, 8);
    b.style(paint(1) * 0.9, MATTE);
    b.box(V3(-1.55, 0.25, -0.55), V3(0.3, 0.45, 0.28), 0.05);
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.cyl(V3(0.5, 0.0, 1.2), V3(0.5, 0.12, 1.2), 0.07, 8);
}

Reg tent({"tent", "TENT", "BUILDINGS", "A camper's tent at the camp spots", 2.4,
          {
              {"DOME TENT", "Dome with a fly, vestibule, crossing poles and guy lines", {paintParam(4)}, tentDome},
              {"RIDGE TENT", "Canvas ridge tent on a pole, flaps tied back", {paintParam(2)}, tentRidge},
              {"TARP SHELTER", "A-frame tarp strung on a ridge line", {paintParam(1)}, tentTarp},
          }});


// ============================================================================================
// LOG HELPERS
// ============================================================================================

// A log from a to e: bark sides, pale cut ends.
void logPiece(B& b, const V3& a, const V3& e, double r, const V3& bark, const V3& endc, int segs = 6, Mat m = WOOD) {
    b.style(bark, m);
    b.cyl(a, e, r, segs, false);
    b.style(endc, m);
    const V3 ax = norm(e - a);
    const V3 u = norm(cross(ax, std::fabs(ax.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), w = cross(ax, u);
    for (int end = 0; end < 2; end++) {
        const V3 c = end ? e : a, n = end ? ax : -ax;
        for (int i = 0; i < segs; i++) {
            const double a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
            b.triN(c, c + (u * std::cos(a0) + w * std::sin(a0)) * r, c + (u * std::cos(a1) + w * std::sin(a1)) * r, n, n, n);
        }
    }
}

// ============================================================================================
// CAMPFIRE
// ============================================================================================

// The log teepee: n logs leaning on a point above the middle; the lower part charred.
void teepee(B& b, int n, uint32_t seed, double charred) {
    for (int k = 0; k < n; k++) {
        const double a = 2 * PI * (k + 0.4 * hash01(seed, k)) / n;
        const double rb = 0.36 + 0.1 * hash01(seed + 1, k);
        const V3 base(rb * std::cos(a), 0.045, rb * std::sin(a));
        const V3 top(0.05 * std::cos(a * 3), 0.62 + 0.1 * hash01(seed + 2, k), 0.05 * std::sin(a * 2));
        const double r0 = 0.04 + 0.015 * hash01(seed + 3, k);
        const V3 mid = mix(base, top, 0.4);
        b.style(mix(V3(0.3, 0.2, 0.12), V3(0.06, 0.055, 0.05), charred), WOOD);
        b.cone(base, mid, r0, r0 * 0.93, 6);
        b.style(mix(V3(0.34, 0.24, 0.15), V3(0.07, 0.06, 0.055), charred * 0.6) * (0.9 + 0.2 * hash01(seed + 4, k)), WOOD);
        b.cone(mid, top, r0 * 0.93, r0 * 0.7, 6);
    }
}

void embers(B& b, uint32_t seed, int n, double size, double spread) {
    b.style(V3(0.75, 0.2, 0.03), EMISSIVE);
    for (int k = 0; k < n; k++) {
        const double a = 2 * PI * hash01(seed, k), r = spread * std::sqrt(hash01(seed + 1, k));
        const double sz = size * (0.012 + 0.02 * hash01(seed + 2, k));
        b.color(mix(V3(0.55, 0.12, 0.02), V3(0.9, 0.35, 0.05), hash01(seed + 3, k)));
        b.box(V3(r * std::cos(a), 0.03 + sz * 0.5, r * std::sin(a)), V3(sz * 1.6, sz, sz * 1.3), 0.0);
    }
    // glowing coals low among the logs
    b.color(V3(0.62, 0.14, 0.02));
    for (int k = 0; k < 3; k++) {
        const double a = k * 2.1 + hash01(seed + 7, 0) * 3;
        b.sphere(V3(0.17 * std::cos(a), 0.04, 0.17 * std::sin(a)), V3(0.08 * size, 0.022 * size, 0.06 * size), 1);
    }
}

void campfireRing(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed") * 7 + 100;
    const double glow = p("glow");
    // scorched ground, ash bed
    b.style(V3(0.2, 0.18, 0.15), MATTE);
    b.extrudeUp(circle(1.0, 20), 0.004);
    b.style(V3(0.16, 0.15, 0.14), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.0, 0.06), V2(0.4, 0.05), V2(0.66, 0.03), V2(0.7, 0.0)}, 14);
    // the ring: 11 stones
    for (int k = 0; k < 11; k++) {
        const double a = 2 * PI * (k + 0.15 * (hash01(seed, k) - 0.5)) / 11;
        const double rr = 0.75 + 0.05 * hash01(seed + 1, k);
        const V3 rad(0.2 + 0.07 * hash01(seed + 2, k), 0.13 + 0.07 * hash01(seed + 3, k), 0.16 + 0.06 * hash01(seed + 4, k));
        b.style(stoneCol(seed, k) * 0.95, ROCK);
        b.push();
        b.move(rr * std::cos(a), 0, rr * std::sin(a));
        b.yaw(-a * 180 / PI);
        b.blob(V3(0, rad.y * 0.55, 0), rad, 0.22, seed + k, 1, 0.75, 0.0);
        b.pop();
    }
    teepee(b, p.i("logs"), seed, 0.35);
    if (glow > 0.01) embers(b, seed + 9, 14, glow, 0.5);
    // log benches
    const int nb = p.i("benches");
    for (int k = 0; k < nb; k++) {
        const double a = (k ? 0.9 : -2.2) * PI;
        b.push();
        b.move(1.7 * std::cos(a), 0, 1.7 * std::sin(a));
        b.yaw(-a * 180 / PI - 90);
        b.style(timber(0, seed, 70 + k), WOOD);
        logPiece(b, V3(-0.7, 0.2, 0), V3(0.7, 0.2, 0), 0.14, timber(0, seed, 71 + k), timber(2, seed, 72 + k) * 1.1, 8);
        b.style(V3(0.4, 0.38, 0.35), ROCK);
        for (int e = -1; e <= 1; e += 2) b.box(V3(e * 0.5, 0.06, 0), V3(0.26, 0.12, 0.26));
        b.pop();
    }
}

void campfirePit(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed") * 7 + 200;
    const double glow = p("glow");
    const V3 steel = mix(V3(0.2, 0.2, 0.21), rustCol(seed, 1) * 0.8, p("rust"));
    // gravel pad
    b.style(V3(0.46, 0.44, 0.4), MATTE);
    b.extrudeUp(circle(0.95, 20), 0.012);
    b.style(V3(0.4, 0.38, 0.35), ROCK);
    for (int k = 0; k < 14; k++) {
        const double a = 2 * PI * hash01(seed, k), r = 0.7 + 0.22 * hash01(seed + 1, k);
        b.blob(V3(r * std::cos(a), 0.03, r * std::sin(a)), V3(0.06, 0.04, 0.05), 0.2, seed + k, 0, 0.6, 0.0);
    }
    // the ring: a rolled steel band with vent slots and a rim
    b.style(steel, METAL);
    b.lathe(V3(0, 0, 0), {V2(0.59, 0.02), V2(0.62, 0.02), V2(0.62, 0.3), V2(0.64, 0.32), V2(0.64, 0.36), V2(0.6, 0.36), V2(0.58, 0.32), V2(0.585, 0.02)}, 20, false);
    b.style(V3(0.02, 0.02, 0.02), MATTE);
    for (int k = 0; k < 8; k++) {
        const double a = 2 * PI * k / 8 + 0.2;
        b.push();
        b.yaw(-a * 180 / PI);
        b.box(V3(0.622, 0.1, 0), V3(0.012, 0.08, 0.14));
        b.pop();
    }
    // ash and fuel
    b.style(V3(0.2, 0.19, 0.18), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.0, 0.1), V2(0.35, 0.09), V2(0.57, 0.05), V2(0.58, 0.02)}, 14);
    b.push();
    b.move(0, 0.06, 0);
    b.scale(0.9);
    teepee(b, p.i("logs"), seed, 0.4);
    b.pop();
    if (glow > 0.01) embers(b, seed + 5, 12, glow, 0.45);
    // swing-arm grill: a post, an arm, a hanging kettle
    b.style(steel * 0.8, METAL);
    b.cyl(V3(0.88, 0.0, -0.15), V3(0.88, 1.05, -0.15), 0.025, 8);
    b.box(V3(0.88, 0.02, -0.15), V3(0.2, 0.04, 0.2));
    b.tube({V3(0.88, 0.98, -0.15), V3(0.5, 1.0, -0.08), V3(0.0, 1.0, 0.1)}, 0.015, 6);
    b.cyl(V3(0.88, 0.8, -0.15), V3(0.5, 0.98, -0.08), 0.01, 5);
    b.style(V3(0.05, 0.05, 0.055), METAL);
    b.tube({V3(0.1, 1.0, 0.08), V3(0.1, 0.8, 0.08)}, 0.007, 4);
    b.lathe(V3(0.1, 0.5, 0.08), {V2(0.0, 0.0), V2(0.1, 0.0), V2(0.13, 0.08), V2(0.12, 0.2), V2(0.09, 0.26), V2(0.07, 0.3)}, 10);
    b.tube({V3(-0.02, 0.76, 0.08), V3(0.1, 0.84, 0.08), V3(0.22, 0.76, 0.08)}, 0.006, 4);
    // a grate across the top
    b.style(steel * 0.7, METAL);
    for (int k = -2; k <= 2; k++) b.cyl(V3(-0.5, 0.3, k * 0.2), V3(0.5, 0.3, k * 0.2), 0.008, 4);
}

void campfireCold(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed") * 7 + 300;
    // heavy soot on the ground, wide ash
    b.style(V3(0.12, 0.11, 0.1), MATTE);
    b.extrudeUp(circle(1.15, 20), 0.004);
    b.style(V3(0.42, 0.4, 0.38), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.0, 0.07), V2(0.3, 0.06), V2(0.6, 0.035), V2(0.72, 0.0)}, 14);
    b.style(V3(0.14, 0.13, 0.12), MATTE);
    for (int k = 0; k < 5; k++) {
        const double a = 2 * PI * hash01(seed, k), r = 0.5 * hash01(seed + 1, k);
        b.blob(V3(r * std::cos(a), 0.06, r * std::sin(a)), V3(0.16, 0.025, 0.12), 0.3, seed + k, 1, 0.5, 0.0);
    }
    // the ring: sooted stones, one or two knocked out of line
    for (int k = 0; k < 11; k++) {
        const double a = 2 * PI * (k + 0.3 * (hash01(seed, k + 20) - 0.5)) / 11;
        const double rr = 0.76 + 0.08 * hash01(seed + 1, k + 20);
        const V3 rad(0.2 + 0.07 * hash01(seed + 2, k), 0.13 + 0.07 * hash01(seed + 3, k), 0.16 + 0.06 * hash01(seed + 4, k));
        b.style(stoneCol(seed, k) * 0.55, ROCK);
        b.push();
        b.move(rr * std::cos(a), 0, rr * std::sin(a));
        b.yaw(-a * 180 / PI);
        b.blob(V3(0, rad.y * 0.55, 0), rad, 0.22, seed + k, 1, 0.75, 0.0);
        b.pop();
    }
    // charred logs fallen in
    for (int k = 0; k < 5; k++) {
        const double a = 2 * PI * hash01(seed + 30, k), r = 0.15 + 0.3 * hash01(seed + 31, k);
        const V3 c(r * std::cos(a), 0.08 + 0.05 * (k % 2), r * std::sin(a));
        const double la = 2 * PI * hash01(seed + 32, k);
        const V3 d(std::cos(la), 0.0, std::sin(la));
        const double L = 0.3 + 0.25 * hash01(seed + 33, k);
        logPiece(b, c - d * L, c + d * L + V3(0, 0.1 * (k % 3), 0), 0.04 + 0.015 * hash01(seed + 34, k), V3(0.05, 0.045, 0.04), V3(0.1, 0.09, 0.08), 6);
    }
    // two ends of a log leaning on the ring and a tin can
    logPiece(b, V3(0.35, 0.1, -0.25), V3(0.8, 0.28, -0.5), 0.045, V3(0.08, 0.07, 0.06), V3(0.2, 0.15, 0.1), 6);
    b.style(V3(0.25, 0.22, 0.2) , METAL);
    b.cyl(V3(-0.2, 0.08, 0.35), V3(-0.2, 0.2, 0.35), 0.05, 8);
    b.style(rustCol(seed, 3) * 0.7, METAL);
    b.cyl(V3(-0.2, 0.2, 0.35), V3(-0.2, 0.202, 0.35), 0.045, 8);
}

Reg campfire({"campfire", "CAMPFIRE", "BUILDINGS", "Fire rings at the camp spots; the embers glow at night", 2.0,
              {
                  {"STONE RING", "11 stones round a log teepee with glowing embers",
                   {seedParam(1), num("logs", "LOGS", 7, 4, 9, 1), choice("benches", "LOG BENCHES", 0, {"NONE", "ONE", "TWO"}), num("glow", "EMBER GLOW", 1.0, 0, 2, 0.1)}, campfireRing},
                  {"STEEL PIT", "Rusting steel fire ring with a swing-arm kettle on gravel",
                   {seedParam(1), num("logs", "LOGS", 6, 4, 9, 1), num("rust", "RUST", 0.5, 0, 1, 0.05), num("glow", "EMBER GLOW", 1.0, 0, 2, 0.1)}, campfirePit},
                  {"COLD FIRE", "Burnt out: sooty stones, ash, charred logs, no glow", {seedParam(1)}, campfireCold},
              }});

// ============================================================================================
// WOODPILE
// ============================================================================================

// Neat stack of split logs lying along z between x = -w/2..w/2, to a height; length along z.
void logStack(B& b, double w, double rows, double len, uint32_t seed, double r, double yBase) {
    const double dx = r * 2.05, dy = r * 1.82;
    const int nr = (int)rows;
    for (int j = 0; j < nr; j++) {
        const int cols = (int)std::floor((w - r * 2 * (j % 2 ? 0.0 : 0.0)) / dx) - (j % 2);
        const double x0 = -(cols - 1) * dx / 2;
        for (int i = 0; i < cols; i++) {
            const double rj = r * (0.88 + 0.22 * hash01(seed, j * 31 + i));
            const double x = x0 + i * dx + (hash01(seed + 1, j * 31 + i) - 0.5) * 0.02;
            const double y = yBase + r + j * dy;
            const double zoff = (hash01(seed + 2, j * 31 + i) - 0.5) * 0.06;
            const V3 bark = timber(hash01(seed + 3, j * 31 + i) > 0.7 ? 3 : 0, seed + 4, j * 31 + i, 0.2);
            logPiece(b, V3(x, y, -len / 2 + zoff), V3(x, y, len / 2 + zoff), rj, bark, timber(2, seed + 5, j * 31 + i, 0.15) * 1.05, 5);
        }
    }
}

void woodpileStack(B& b, const P& p) {
    const uint32_t seed = 61;
    const double rust = p("rust");
    // sleepers and end posts
    b.style(timber(3, seed, 1), WOOD);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, 0.06, sz * 0.4), V3(3.0, 0.12, 0.12));
    for (int sx = -1; sx <= 1; sx += 2) {
        b.box(V3(sx * 1.5, 0.06, 0), V3(0.12, 0.12, 1.0));
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(timber(0, seed, 5 + sx + sz * 3), WOOD);
            b.box(V3(sx * 1.45, 0.6, sz * 0.5), V3(0.1, 1.05, 0.1));
        }
        b.beam(V3(sx * 1.45, 0.3, 0.5), V3(sx * 1.45, 0.95, -0.5), 0.06, 0.04);
    }
    logStack(b, 2.7, 5, 0.88, seed, 0.088, 0.12);
    // lean-to tin roof, high at the front (+z), falling to the back
    const double yF = 1.4, yB = 1.2;
    b.style(timber(3, seed, 20), WOOD);
    for (int sx = -1; sx <= 1; sx += 2) b.beam(V3(sx * 1.45, yF - 0.05, 0.52), V3(sx * 1.45, yB - 0.05, -0.52), 0.09, 0.07);
    b.beam(V3(-1.55, yF - 0.04, 0.52), V3(1.55, yF - 0.04, 0.52), 0.07, 0.09);
    b.beam(V3(-1.55, yB - 0.04, -0.52), V3(1.55, yB - 0.04, -0.52), 0.07, 0.09);
    const double sl = (yF - yB) / 1.04;
    const V3 nrm = norm(V3(0, 1, -sl));
    for (int k = 0; k < 3; k++) {
        const double x0 = -1.62 + k * 1.08;
        b.style(mix(V3(0.45, 0.46, 0.46), rustCol(seed, k), 0.3 + rust * 0.5), METAL);
        corrug(b, V3(x0, yB + 0.02, -0.66), V3(1.06, 0, 0), V3(0, (yF - yB) + 0.2 * sl, 1.32), 0.1, 0.014, V3(0, 1, 0), 4, true);
    }
    b.style(V3(0.4, 0.4, 0.4), METAL);
    (void)nrm;
}

void woodpileHeap(B& b, const P& p) {
    const uint32_t seed = 63 + (uint32_t)p.i("seed");
    // a loose heap of logs
    const int n = 62;
    for (int k = 0; k < n; k++) {
        const double a = 2 * PI * hash01(seed, k), rr = std::sqrt(hash01(seed + 1, k));
        const double x = -0.45 + 1.15 * rr * std::cos(a), z = 0.78 * rr * std::sin(a);
        const double mound = (1.0 - rr * rr) * 0.62;
        const double y = 0.08 + mound * (0.4 + 0.6 * hash01(seed + 2, k));
        const double la = PI * hash01(seed + 3, k) + (k % 2 ? 0.0 : 1.3);
        const double L = 0.22 + 0.2 * hash01(seed + 4, k);
        const V3 d(std::cos(la), 0.12 * (hash01(seed + 5, k) - 0.5), std::sin(la));
        const double r = 0.05 + 0.04 * hash01(seed + 6, k);
        logPiece(b, V3(x, y, z) - d * L, V3(x, y, z) + d * L, r, timber(hash01(seed + 7, k) > 0.6 ? 3 : 0, seed, k, 0.2), timber(2, seed + 8, k, 0.18) * 1.05, 5);
    }
    // chopping block, with an axe stuck in it
    const V3 bc(1.45, 0, 0.1);
    b.style(timber(1, seed, 90) * 0.7, WOOD);
    b.lathe(bc, {V2(0.0, 0.0), V2(0.27, 0.0), V2(0.26, 0.1), V2(0.23, 0.46), V2(0.24, 0.5), V2(0.0, 0.5)}, 10, false);
    b.style(timber(2, seed, 91), WOOD);
    b.cyl(bc + V3(0, 0.495, 0), bc + V3(0, 0.505, 0), 0.236, 10);
    b.style(V3(0.3, 0.2, 0.12), WOOD);
    for (int k = -1; k <= 1; k++) b.box(bc + V3(0.0, 0.508, 0.06 * k), V3(0.44, 0.003, 0.012));  // the chop marks
    // axe: haft up and out, head buried
    const V3 hs = bc + V3(0.0, 0.52, 0.0);
    b.style(timber(0, seed, 92), WOOD);
    b.beam(hs + V3(-0.04, 0.0, 0.0), hs + V3(0.26, 0.5, -0.1), 0.032, 0.026, V3(0, 0, 1));
    b.style(V3(0.35, 0.36, 0.38), METAL);
    b.push();
    b.move(hs.x - 0.02, hs.y - 0.02, hs.z - 0.0);
    b.pitch(-12);
    b.extrude({V2(0.02, 0.02), V2(0.07, 0.06), V2(0.07, -0.07), V2(0.02, -0.07), V2(-0.04, -0.02), V2(-0.04, 0.02)}, 0.02);
    b.pop();
    // split halves and chips by the block
    for (int k = 0; k < 4; k++) {
        const double a = 2 * PI * hash01(seed + 20, k), r = 0.35 + 0.35 * hash01(seed + 21, k);
        logPiece(b, bc + V3(r * std::cos(a) - 0.12, 0.05, r * std::sin(a) + 0.35 * 0), bc + V3(r * std::cos(a) + 0.12, 0.05, r * std::sin(a)), 0.05, timber(0, seed, 30 + k), timber(2, seed, 31 + k), 5);
    }
    b.style(timber(2, seed, 40), WOOD);
    for (int k = 0; k < 14; k++) {
        const double a = 2 * PI * hash01(seed + 22, k), r = 0.3 + 0.55 * hash01(seed + 23, k);
        b.box(bc + V3(r * std::cos(a), 0.008, r * std::sin(a)), V3(0.05 * (0.5 + hash01(seed, k)), 0.015, 0.03), 0.0);
    }
    // a bundle of kindling at the foot of the heap
    for (int k = 0; k < 7; k++) logPiece(b, V3(-1.45, 0.03 + 0.02 * (k % 3), -0.4 + 0.035 * k), V3(-0.85, 0.03 + 0.02 * (k % 3), -0.38 + 0.035 * k), 0.017, timber(0, seed, 50 + k), timber(2, seed, 60 + k), 5);
    b.style(V3(0.3, 0.25, 0.15), MATTE);
    b.tube({V3(-1.2, 0.0, -0.5), V3(-1.2, 0.08, -0.4), V3(-1.2, 0.0, -0.27)}, 0.006, 4);
}

void woodpileRack(B& b, const P& p) {
    const uint32_t seed = 67;
    const bool tarp = p.i("tarp");
    // timber cord rack: sleepers, four uprights, a tie rail each end
    b.style(timber(3, seed, 1), WOOD);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, 0.07, sz * 0.4), V3(3.0, 0.14, 0.14));
    b.style(timber(0, seed, 2), WOOD);
    for (int x : {-1, 1})
        for (int sz = -1; sz <= 1; sz += 2) {
            b.box(V3(x * 1.45, 0.62, sz * 0.5), V3(0.12, 1.1, 0.1));
            b.box(V3(x * 1.45, 1.2, sz * 0.5), V3(0.2, 0.06, 0.1));
        }
    for (int x : {-1, 1}) b.beam(V3(x * 1.45, 1.2, 0.5), V3(x * 1.45, 1.2, -0.5), 0.1, 0.06);
    // steel straps across the front of the stack
    b.style(V3(0.2, 0.2, 0.22), METAL);
    logStack(b, 2.75, 5, 0.88, seed, 0.092, 0.14);
    for (int sz = -1; sz <= 1; sz += 2)
        for (double y : {0.4, 0.8}) b.box(V3(0, y, sz * 0.455), V3(2.82, 0.02, 0.008), 0.0);
    // the tarp over part of the top
    if (tarp) {
        b.style(V3(0.2, 0.32, 0.42), MATTE);
        b.beam(V3(-1.5, 1.05, 0.0), V3(0.1, 1.07, 0.0), 1.2, 0.012, V3(0, 1, 0));
        b.beam(V3(-1.5, 0.82, 0.58), V3(0.1, 1.04, 0.5), 0.3, 0.01, V3(0, 1, 0), 0.0);
        b.beam(V3(-1.5, 0.82, -0.58), V3(0.1, 1.04, -0.5), 0.3, 0.01, V3(0, 1, 0), 0.0);
        b.style(V3(0.45, 0.43, 0.4), ROCK);
        for (int k = 0; k < 3; k++) b.blob(V3(-1.2 + k * 0.55, 1.1, (k % 2 ? 0.3 : -0.3)), V3(0.1, 0.06, 0.08), 0.25, 5 + k, 1, 0.7, 0.0);
    }
}

Reg woodpile({"woodpile", "WOODPILE", "BUILDINGS", "Split firewood at the cabins and camps", 2.9,
              {
                  {"STACK AND ROOF", "Tight stack between end posts under a little tin roof", {num("rust", "ROOF RUST", 0.5, 0, 1, 0.05)}, woodpileStack},
                  {"LOOSE HEAP", "A heap of logs and a chopping block with an axe", {seedParam(1)}, woodpileHeap},
                  {"CORD RACK", "Timber rack with a neat stack and a weighted tarp", {choice("tarp", "TARP", 1, {"OFF", "ON"})}, woodpileRack},
              }});

// ============================================================================================
// PICNIC TABLE
// ============================================================================================

// A: classic timber A-frame table with attached benches (length along z).
void tableTimber(B& b, const P& p) {
    const uint32_t seed = 71;
    const int kind = p.i("wood");
    const double L = 1.8;
    // top: four boards
    for (int i = 0; i < 4; i++) {
        b.style(timber(kind, seed, i, 0.14), WOOD);
        b.box(V3(-0.33 + i * 0.22, 0.745, 0), V3(0.2, 0.04, L), 0.006);
    }
    // benches: two boards each side
    for (int sx = -1; sx <= 1; sx += 2)
        for (int i = 0; i < 2; i++) {
            b.style(timber(kind, seed, 20 + i + sx * 3, 0.14), WOOD);
            b.box(V3(sx * (0.64 + i * 0.145), 0.44, 0), V3(0.135, 0.04, L - 0.1), 0.006);
        }
    // A-frames at each end
    for (int e = -1; e <= 1; e += 2) {
        const double z = e * 0.7;
        b.style(timber(3, seed, 30 + e), WOOD);
        for (int sx = -1; sx <= 1; sx += 2) {
            b.beam(V3(sx * 0.82, 0.0, z), V3(sx * 0.06, 0.72, z), 0.07, 0.045, V3(0, 0, 1), 0.004);
        }
        b.box(V3(0, 0.44 - 0.07, z), V3(1.4, 0.07, 0.045), 0.0);  // seat cross-bar
        b.box(V3(0, 0.695, z), V3(0.8, 0.07, 0.05), 0.0);  // table bearer
        b.style(V3(0.2, 0.2, 0.22), METAL);
        for (int sx = -1; sx <= 1; sx += 2) {
            b.cyl(V3(sx * 0.45, 0.375, z - 0.03), V3(sx * 0.45, 0.375, z + 0.03), 0.012, 5);
            b.cyl(V3(sx * 0.25, 0.7, z - 0.03), V3(sx * 0.25, 0.7, z + 0.03), 0.012, 5);
        }
    }
    // long stretcher
    b.style(timber(3, seed, 40), WOOD);
    b.box(V3(0, 0.28, 0), V3(0.06, 0.06, 1.4), 0.0);
}

// B: steel tube frame with planks.
void tableSteel(B& b, const P& p) {
    const uint32_t seed = 73;
    const V3 steel = muted(paint(p.i("paint")), 0.3) * 0.85;
    const double L = 1.8;
    for (int i = 0; i < 4; i++) {
        b.style(timber(2, seed, i, 0.14) * 0.9, WOOD);
        b.box(V3(-0.33 + i * 0.22, 0.745, 0), V3(0.2, 0.038, L), 0.006);
    }
    for (int sx = -1; sx <= 1; sx += 2)
        for (int i = 0; i < 2; i++) {
            b.style(timber(2, seed, 20 + i + sx * 3, 0.14) * 0.9, WOOD);
            b.box(V3(sx * (0.68 + i * 0.15), 0.44, 0), V3(0.14, 0.038, L - 0.1), 0.006);
        }
    for (int e = -1; e <= 1; e += 2) {
        const double z = e * 0.76;
        b.style(steel, PAINT);
        // a bent-tube end frame: legs splay from the bench rail to the ground, a hoop under the top
        for (int sx = -1; sx <= 1; sx += 2) {
            b.tube({V3(sx * 0.9, 0.0, z), V3(sx * 0.85, 0.2, z), V3(sx * 0.78, 0.4, z), V3(sx * 0.58, 0.55, z), V3(sx * 0.4, 0.68, z)}, 0.022, 7);
            b.tube({V3(sx * 0.4, 0.68, z), V3(sx * 0.3, 0.7, z)}, 0.022, 7);
        }
        b.tube({V3(-0.3, 0.7, z), V3(0.3, 0.7, z)}, 0.022, 7);
        b.tube({V3(-0.84, 0.405, z), V3(0.84, 0.405, z)}, 0.02, 7);
        // feet
        b.style(V3(0.06, 0.06, 0.06), MATTE);
        for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 0.9, 0.012, z), V3(0.1, 0.024, 0.1), 0.004);
    }
    b.style(steel * 0.9, PAINT);
    b.tube({V3(0, 0.25, -0.76), V3(0, 0.25, 0.76)}, 0.017, 6);
    b.style(V3(0.6, 0.6, 0.62), METAL);
    for (int e = -1; e <= 1; e += 2)
        for (int sx = -1; sx <= 1; sx += 2) {
            b.cyl(V3(sx * 0.7, 0.44, e * 0.76 - 0.05), V3(sx * 0.7, 0.46, e * 0.76 - 0.05), 0.014, 6);
            b.cyl(V3(sx * 0.7, 0.44, e * 0.76 + 0.05), V3(sx * 0.7, 0.46, e * 0.76 + 0.05), 0.014, 6);
        }
}

// C: rustic log table: whole small logs for the top, half-logs for the benches.
void tableLog(B& b, const P& p) {
    const uint32_t seed = 75 + (uint32_t)p.i("seed");
    const double L = 1.8;
    for (int i = 0; i < 5; i++) {
        const double r = 0.075 + 0.008 * hash01(seed, i);
        logPiece(b, V3(-0.34 + i * 0.17, 0.72 + 0.0, -L / 2 - 0.05 * hash01(seed + 1, i)), V3(-0.34 + i * 0.17, 0.72, L / 2 + 0.05 * hash01(seed + 2, i)), r,
                 timber(0, seed, i, 0.2), timber(2, seed, 10 + i), 7);
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        // half-log bench: a D-section
        b.style(timber(0, seed, 20 + sx), WOOD);
        std::vector<V2> d;
        for (int i = 0; i <= 8; i++) d.push_back(V2(0.15 * std::cos(PI + PI * i / 8), 0.12 * std::sin(PI + PI * i / 8)));
        b.push();
        b.move(sx * 0.78, 0.32, 0);
        // (shape x -> across, y -> up, thickness along the length)
        b.extrude(d, L - 0.1);
        b.pop();
        b.style(timber(1, seed, 25 + sx) * 1.05, WOOD);
        b.box(V3(sx * 0.78, 0.322, 0), V3(0.3, 0.012, L - 0.1));
    }
    // log legs: crossed pairs, sunk into the ground
    for (int e = -1; e <= 1; e += 2) {
        const double z = e * 0.65;
        for (int sx = -1; sx <= 1; sx += 2)
            logPiece(b, V3(sx * 0.9, -0.05, z + 0.02 * sx), V3(sx * 0.1, 0.72, z - 0.02 * sx), 0.055, timber(3, seed, 40 + e + sx * 2), timber(2, seed, 41), 6);
        logPiece(b, V3(-0.85, 0.2, z + 0.1), V3(0.85, 0.2, z + 0.1), 0.04, timber(3, seed, 50 + e), timber(2, seed, 51), 6);
    }
}

Reg picnicTable({"picnic_table", "PICNIC TABLE", "BUILDINGS", "Picnic tables at the camps and trailheads", 2.0,
                 {
                     {"A-FRAME TIMBER", "Classic timber table with attached benches", {choice("wood", "WOOD", 1, {"BROWN", "SILVER", "PINE", "DARK", "RED"})}, tableTimber},
                     {"STEEL FRAME", "Painted tube frame, timber planks", {paintParam(7)}, tableSteel},
                     {"LOG TABLE", "Rustic round-log top and half-log benches", {seedParam(1)}, tableLog},
                 }});


// ============================================================================================
// ORE CART
// ============================================================================================

V3 steelRust(double rust, uint32_t seed, uint32_t i) {
    return mix(V3(0.26, 0.26, 0.28), rustCol(seed, i), std::clamp(rust * (0.55 + 0.7 * hash01(seed + 3, i)), 0.0, 1.0));
}

// Short track along x: sleepers and two rails on a 0.6 m gauge; rail top at y = 0.17.
void trackPiece(B& b, uint32_t seed, double x0, double x1) {
    const int n = (int)std::lround((x1 - x0) / 0.55) + 1;
    for (int i = 0; i < n; i++) {
        const double x = x0 + 0.12 + (x1 - x0 - 0.24) * i / std::max(1, n - 1);
        b.style(timber(i % 2 ? 3 : 1, seed, i, 0.2) * 0.85, WOOD);
        b.box(V3(x, 0.045, 0), V3(0.13, 0.09, 1.2), 0.008);
    }
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(V3(0.3, 0.2, 0.15), METAL);
        b.box(V3((x0 + x1) / 2, 0.097, sz * 0.3), V3(x1 - x0, 0.015, 0.09));  // base
        b.box(V3((x0 + x1) / 2, 0.128, sz * 0.3), V3(x1 - x0, 0.05, 0.022));  // web
        b.style(mix(V3(0.42, 0.32, 0.26), V3(0.55, 0.55, 0.56), 0.5), METAL);
        b.box(V3((x0 + x1) / 2, 0.158, sz * 0.3), V3(x1 - x0, 0.026, 0.055), 0.004);  // worn bright head
        b.style(V3(0.18, 0.13, 0.1), METAL);
        for (int i = 0; i < n; i++) {
            const double x = x0 + 0.12 + (x1 - x0 - 0.24) * i / std::max(1, n - 1);
            b.box(V3(x, 0.115, sz * 0.3 + 0.045), V3(0.03, 0.03, 0.02));  // spikes
            b.box(V3(x, 0.115, sz * 0.3 - 0.045), V3(0.03, 0.03, 0.02));
        }
    }
    // a low bed of ballast under it
    b.style(V3(0.4, 0.37, 0.33), ROCK);
    b.box(V3((x0 + x1) / 2, 0.012, 0), V3(x1 - x0 + 0.2, 0.05, 1.45), 0.02);
}

// A flanged mine wheel, axis along z, centre c, flange on the inside.
void minewheel(B& b, const V3& c, double side, const V3& col) {
    std::vector<V2> pr = {V2(0.06, -0.045), V2(0.175, -0.032), V2(0.172, -0.012), V2(0.147, -0.01), V2(0.147, 0.03), V2(0.07, 0.034), V2(0.05, 0.05), V2(0.0, 0.05)};
    if (side < 0) {
        for (V2& q : pr) q.y = -q.y;
        std::reverse(pr.begin(), pr.end());
    }
    b.push();
    b.move(c);
    b.roll(90);
    b.style(col, METAL);
    b.lathe(V3(0, 0, 0), pr, 12, false);
    b.pop();
    b.style(V3(0.12, 0.1, 0.09), METAL);
    b.cyl(c + V3(0, 0, side * 0.04), c + V3(0, 0, side * 0.08), 0.04, 6);
}

// Undercarriage: the frame, axles, journal boxes, wheels, couplings. Rail top at y = 0.17.
void cartRunning(B& b, uint32_t seed, double rust) {
    const V3 st = steelRust(rust, seed, 1) * 0.9;
    b.style(st, METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, 0.47, sz * 0.3), V3(1.78, 0.1, 0.07));
    for (double x : {-0.5, 0.5}) {
        b.box(V3(x, 0.47, 0), V3(0.07, 0.1, 0.6));
        b.style(V3(0.15, 0.12, 0.1), METAL);
        b.cyl(V3(x, 0.32, -0.36), V3(x, 0.32, 0.36), 0.025, 6);
        for (int sz = -1; sz <= 1; sz += 2) {
            b.box(V3(x, 0.4, sz * 0.395), V3(0.1, 0.18, 0.06));
            minewheel(b, V3(x, 0.32, sz * 0.3), sz, steelRust(rust * 0.7, seed, 5 + (int)(x * 4) + sz));
            b.style(st, METAL);
            b.box(V3(x, 0.48, sz * 0.32), V3(0.04, 0.1, 0.07));  // hanger
        }
    }
    // draw hooks at both ends
    b.style(V3(0.18, 0.15, 0.13), METAL);
    for (int e = -1; e <= 1; e += 2) {
        b.tube({V3(e * 0.88, 0.47, 0), V3(e * 1.0, 0.47, 0), V3(e * 1.07, 0.42, 0.03), V3(e * 1.04, 0.36, 0.0)}, 0.016, 5);
    }
}

void orePile(B& b, double y, double rx, double rz, uint32_t seed) {
    b.style(V3(0.33, 0.29, 0.26), ROCK);
    b.blob(V3(0, y, 0), V3(rx, 0.22, rz), 0.28, seed, 2, 0.6, y - 0.12);
    b.style(V3(0.4, 0.33, 0.25), ROCK);
    for (int k = 0; k < 5; k++)
        b.blob(V3(rx * 0.6 * (hash01(seed, k) - 0.5), y + 0.15 + 0.06 * hash01(seed + 1, k), rz * 0.8 * (hash01(seed + 2, k) - 0.5)), V3(0.07, 0.05, 0.06), 0.3, seed + k, 1, 0.7);
}

// A: V-tipper skip.
void cartSkip(B& b, const P& p) {
    const uint32_t seed = 81;
    const double rust = p("rust");
    const bool track = p.i("track");
    if (track) trackPiece(b, seed, -1.5, 1.5);
    b.push();
    if (!track) b.move(0, -0.17, 0);
    cartRunning(b, seed, rust);
    // trunnion frame: the pivot the skip tips on
    const V3 st = steelRust(rust, seed, 2);
    b.style(st * 0.8, METAL);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * 0.3, 0.56, 0), V3(0.08, 0.14, 0.24));
    // the V body
    const double zb = 0.17, zt = 0.46, yb = 0.62, yt = 1.2;
    const double dz = zt - zb, dy = yt - yb, sl = std::hypot(dz, dy);
    for (int sz = -1; sz <= 1; sz += 2) {
        const V3 mid(0, (yb + yt) / 2, sz * (zb + zt) / 2);
        const V3 nrm = norm(V3(0, -dz, sz * dy));
        const V3 outn = nrm * (dot(nrm, V3(0, 0, sz)) < 0 ? -1.0 : 1.0);
        b.style(steelRust(rust, seed, 10 + sz), METAL);
        b.beam(mid - V3(0.8, 0, 0), mid + V3(0.8, 0, 0), sl + 0.02, 0.03, outn);
        // patches of rust and a couple of dents
        for (int k = 0; k < 6; k++) {
            b.color(rustCol(seed, 20 + k + sz * 5) * (rust + 0.3 > 0.5 ? 1.0 : 0.8));
            const double t = hash01(seed + 5, k + 7 * (sz + 1)), x = -0.6 + 1.2 * hash01(seed + 6, k + 9 * (sz + 1));
            const V3 q = V3(x, yb + (yt - yb) * (0.15 + 0.7 * t), sz * (zb + dz * (0.15 + 0.7 * t))) + outn * 0.017;
            b.beam(q - V3(0.07, 0, 0), q + V3(0.07, 0, 0), 0.09, 0.006, outn);
        }
        // rivets along the top band
        b.style(steelRust(rust * 1.2, seed, 40), METAL);
        for (int k = 0; k < 10; k++) {
            const double x = -0.7 + k * 0.155;
            const V3 q(x, yt - 0.07, sz * (zt - 0.07 * dz / sl));
            b.cone(q + outn * 0.02, q + outn * 0.015 * 0.0, 0.016, 0.006, 4);
        }
    }
    for (int e = -1; e <= 1; e += 2) {
        b.at(V3(e * 0.8, 0, 0), 90, [&] {
            b.style(steelRust(rust, seed, 50 + e), METAL);
            b.extrude({V2(-zt - 0.02, yt), V2(zt + 0.02, yt), V2(zb, yb), V2(-zb, yb)}, 0.035);
        });
    }
    // bottom plate and rim angles
    b.style(steelRust(rust, seed, 60), METAL);
    b.box(V3(0, yb - 0.005, 0), V3(1.62, 0.03, 2 * zb + 0.02));
    b.style(steelRust(rust * 0.8, seed, 61), METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, yt + 0.005, sz * (zt + 0.005)), V3(1.66, 0.045, 0.07), 0.006);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * 0.82, yt + 0.005, 0), V3(0.07, 0.045, 2 * zt), 0.006);
    // stiffening straps over the sides
    for (double x : {-0.4, 0.4})
        for (int sz = -1; sz <= 1; sz += 2) {
            const V3 mid(x, (yb + yt) / 2, sz * (zb + zt) / 2);
            const V3 nrm = norm(V3(0, -dz, sz * dy));
            const V3 outn = nrm * (dot(nrm, V3(0, 0, sz)) < 0 ? -1.0 : 1.0);
            b.beam(mid + outn * 0.03, mid + outn * 0.03 + V3(0.001, 0, 0), sl, 0.0, outn);
            b.beam(mid + outn * 0.025 - V3(0.03, 0, 0), mid + outn * 0.025 + V3(0.03, 0, 0), sl, 0.012, outn);
        }
    // the tipping lever
    b.style(V3(0.15, 0.13, 0.12), METAL);
    b.tube({V3(0.84, 0.66, 0.05), V3(0.92, 0.85, 0.15), V3(0.96, 1.02, 0.18)}, 0.014, 5);
    b.sphere(V3(0.96, 1.05, 0.18), V3(0.03, 0.03, 0.03), 1);
    if (p.i("ore")) b.at(V3(0, 0, 0), 0, [&] { orePile(b, 1.06, 0.65, 0.3, seed); });
    b.pop();
}

// B: riveted box cart with a drop end.
void cartBox(B& b, const P& p) {
    const uint32_t seed = 83;
    const double rust = p("rust");
    const bool track = p.i("track");
    if (track) trackPiece(b, seed, -1.5, 1.5);
    b.push();
    if (!track) b.move(0, -0.17, 0);
    cartRunning(b, seed, rust);
    const double yb = 0.56, yt = 1.2, hx = 0.82, hz = 0.45;
    const double h = yt - yb, ym = (yb + yt) / 2;
    // side and end plates
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(steelRust(rust, seed, 70 + sz), METAL);
        b.box(V3(0, ym, sz * hz), V3(2 * hx, h, 0.03), 0.008);
        // lower and upper riveted bands
        b.style(steelRust(rust * 1.1, seed, 72 + sz), METAL);
        for (double y : {yb + 0.08, yt - 0.08}) b.box(V3(0, y, sz * (hz + 0.02)), V3(2 * hx - 0.02, 0.07, 0.014));
        for (double x : {-0.5, 0.0, 0.5}) b.box(V3(x, ym, sz * (hz + 0.02)), V3(0.07, h - 0.02, 0.014));
        b.style(steelRust(rust * 1.4, seed, 74), METAL);
        for (double y : {yb + 0.08, yt - 0.08})
            for (int k = 0; k < 15; k++) {
                const double x = -0.76 + k * 0.109;
                b.cone(V3(x, y, sz * (hz + 0.027)), V3(x, y, sz * (hz + 0.02)), 0.016, 0.01, 4);
            }
        for (double x : {-0.5, 0.0, 0.5})
            for (int k = 0; k < 4; k++) b.cone(V3(x, yb + 0.16 + k * 0.14, sz * (hz + 0.027)), V3(x, yb + 0.16 + k * 0.14, sz * (hz + 0.02)), 0.016, 0.01, 4);
        // rust patches
        for (int k = 0; k < 5; k++) {
            b.color(rustCol(seed, 80 + k + 6 * sz) * (0.8 + rust * 0.3));
            const double x = -0.7 + 1.4 * hash01(seed + 6, k + 9 * (sz + 1)), y = yb + 0.1 + (h - 0.2) * hash01(seed + 7, k + 11 * (sz + 1));
            b.box(V3(x, y, sz * (hz + 0.034)), V3(0.07 + 0.1 * hash01(seed, k), 0.05 + 0.07 * hash01(seed + 1, k), 0.003));
        }
    }
    for (int e = -1; e <= 1; e += 2) {
        b.style(steelRust(rust, seed, 76 + e), METAL);
        b.box(V3(e * hx, ym, 0), V3(0.03, h, 2 * hz - 0.03), 0.008);
        b.style(steelRust(rust * 1.1, seed, 78), METAL);
        b.box(V3(e * (hx + 0.02), ym, 0), V3(0.014, 0.08, 2 * hz), 0.0);
    }
    // rim angle and corner posts
    b.style(steelRust(rust * 0.7, seed, 90), METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, yt + 0.01, sz * hz), V3(2 * hx + 0.06, 0.05, 0.08), 0.008);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * hx, yt + 0.01, 0), V3(0.08, 0.05, 2 * hz), 0.008);
    for (int e = -1; e <= 1; e += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(e * hx, ym, sz * hz), V3(0.07, h, 0.07), 0.006);
    // floor and the drop-end's latch
    b.style(steelRust(rust, seed, 91), METAL);
    b.box(V3(0, yb + 0.02, 0), V3(2 * hx - 0.02, 0.04, 2 * hz - 0.02));
    b.style(V3(0.16, 0.14, 0.13), METAL);
    b.box(V3(hx + 0.045, yt - 0.12, 0), V3(0.03, 0.04, 0.3));
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(hx + 0.03, yb + 0.05, sz * 0.38), V3(hx + 0.08, yb + 0.05, sz * 0.38), 0.025, 6);  // hinge pins
    if (p.i("ore")) orePile(b, 1.02, 0.62, 0.3, seed);
    b.pop();
}

// C: flat wagon, a few timbers chained on.
void cartWagon(B& b, const P& p) {
    const uint32_t seed = 85;
    const double rust = p("rust");
    const bool track = p.i("track");
    if (track) trackPiece(b, seed, -1.5, 1.5);
    b.push();
    if (!track) b.move(0, -0.17, 0);
    cartRunning(b, seed, rust);
    // deck of planks
    for (int i = 0; i < 6; i++) {
        b.style(timber(i % 2, seed, i, 0.2), WOOD);
        b.box(V3(0, 0.58, -0.4 + (i + 0.5) * 0.8 / 6), V3(1.72, 0.05, 0.8 / 6 - 0.008), 0.004);
    }
    b.style(timber(3, seed, 20), WOOD);
    for (double x : {-0.7, 0.0, 0.7}) b.box(V3(x, 0.545, 0), V3(0.1, 0.03, 0.84));
    // stakes at the corners
    b.style(steelRust(rust * 0.8, seed, 30), METAL);
    for (int e = -1; e <= 1; e += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.box(V3(e * 0.82, 0.88, sz * 0.43), V3(0.05, 0.6, 0.05), 0.004);
            b.box(V3(e * 0.82, 0.62, sz * 0.43), V3(0.1, 0.03, 0.1));
        }
    if (p.i("load")) {
        // squared timbers and a round post
        const double xs[4] = {0.1, 0.3, 0.12, -0.2};
        for (int i = 0; i < 3; i++) {
            b.style(timber(i % 2 ? 0 : 1, seed, 40 + i, 0.2), WOOD);
            b.box(V3(xs[i] * 0.4, 0.69, -0.28 + i * 0.28), V3(1.9 - 0.1 * i, 0.15, 0.17), 0.012);
        }
        b.style(timber(0, seed, 50), WOOD);
        b.box(V3(0.0, 0.84, -0.14), V3(1.7, 0.14, 0.16), 0.012);
        logPiece(b, V3(-0.9, 0.84, 0.14), V3(0.85, 0.85, 0.15), 0.08, timber(3, seed, 51), timber(2, seed, 52), 7);
        // chains over the top
        b.style(V3(0.12, 0.12, 0.12), METAL);
        for (double x : {-0.45, 0.45}) b.tube({V3(x, 0.58, -0.36), V3(x, 0.9, -0.3), V3(x, 0.95, 0.0), V3(x, 0.93, 0.3), V3(x, 0.58, 0.36)}, 0.01, 4);
    }
    b.pop();
}

Reg oreCart({"ore_cart", "ORE CART", "BUILDINGS", "An old mine cart, on its piece of track by the mine", 3.0,
             {
                 {"V-TIPPER SKIP", "Rusty V-shaped tipping skip on a frame",
                  {num("rust", "RUST", 0.7, 0, 1, 0.05), choice("track", "TRACK", 1, {"OFF", "ON"}), choice("ore", "ORE LOAD", 1, {"EMPTY", "LOADED"})}, cartSkip},
                 {"BOX CART", "Riveted box with a drop end",
                  {num("rust", "RUST", 0.6, 0, 1, 0.05), choice("track", "TRACK", 1, {"OFF", "ON"}), choice("ore", "ORE LOAD", 1, {"EMPTY", "LOADED"})}, cartBox},
                 {"FLAT WAGON", "Plank deck and stakes, a few timbers chained on",
                  {num("rust", "RUST", 0.6, 0, 1, 0.05), choice("track", "TRACK", 1, {"OFF", "ON"}), choice("load", "LOAD", 1, {"EMPTY", "TIMBERS"})}, cartWagon},
             }});


// ============================================================================================
// MINE PORTAL
// ============================================================================================

// The way in: a dark tunnel ~3 m deep behind the frame at x = 0 (opening 3.2 wide, 2.4 tall).
void adit(B& b, double depth = 3.0, double hw = 1.5, double h = 2.35) {
    const int sec = 3;
    for (int i = 0; i < sec; i++) {
        const double x0 = -depth * i / sec, x1 = -depth * (i + 1) / sec;
        const double xm = (x0 + x1) / 2, L = x0 - x1 + 0.01;
        const double k = 0.075 - 0.028 * i;
        b.style(V3(k, k * 0.9, k * 0.8), MATTE);
        b.box(V3(xm, 0.0, 0), V3(L, 0.04, 2 * hw + 0.1));         // floor
        b.box(V3(xm, h + 0.02, 0), V3(L, 0.05, 2 * hw + 0.1));      // roof
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(xm, h / 2, sz * (hw + 0.025)), V3(L, h, 0.05));
    }
    b.style(V3(0.004, 0.004, 0.004), MATTE);
    b.box(V3(-depth - 0.02, h / 2, 0), V3(0.05, h + 0.1, 2 * hw + 0.1));
}

// A hanging lantern with a lit lens (the lens glows at night).
void lantern(B& b, const V3& at, bool lit) {
    b.style(V3(0.08, 0.08, 0.08), METAL);
    b.cyl(at + V3(0, 0.05, 0), at + V3(0, 0.3, 0), 0.006, 4);
    b.lathe(at, {V2(0.0, 0.0), V2(0.05, 0.0), V2(0.055, 0.02), V2(0.055, 0.18), V2(0.03, 0.2), V2(0.0, 0.22)}, 8, false);
    b.style(lit ? V3(0.95, 0.55, 0.15) : V3(0.3, 0.25, 0.18), lit ? EMISSIVE : GLASS);
    b.cyl(at + V3(0, 0.03, 0), at + V3(0, 0.17, 0), 0.047, 8);
}

// A weathered sign board: planks with painted letters, facing +x at plane x.
void signBoard(B& b, double x, double y, double z, const std::string& text, double w, double h, double px, const V3& board, const V3& ink, uint32_t seed) {
    const int n = std::max(2, (int)std::lround(h / 0.12));
    for (int i = 0; i < n; i++) {
        b.style(jit(board, seed, i, 0.1), WOOD);
        b.box(V3(x, y + h / 2 - (i + 0.5) * h / n, z), V3(0.03, h / n - 0.006, w));
    }
    b.style(board * 0.6, WOOD);
    for (int e = -1; e <= 1; e += 2) b.box(V3(x + 0.025, y, z + e * (w / 2 - 0.1)), V3(0.025, h + 0.02, 0.07));
    b.style(ink, MATTE);
    b.letters(text, V3(x + 0.015, y, z), px, 0.008);
}

// Rubble and boulders at the edges, spilled in front.
void minerubble(B& b, uint32_t seed, double zspan, int n) {
    for (int k = 0; k < n; k++) {
        const double side = k % 2 ? 1 : -1;
        const double z = side * (zspan + 0.8 * hash01(seed, k)), x = 0.2 + 1.4 * hash01(seed + 1, k);
        const double r = 0.12 + 0.35 * hash01(seed + 2, k) * hash01(seed + 3, k);
        b.style(stoneCol(seed, k) * 0.9, ROCK);
        b.blob(V3(x, r * 0.5, z), V3(r * 1.2, r * 0.7, r), 0.3, seed + k, 1, 0.7, 0.0);
    }
}

// A: timber set with lagging boards.
void portalTimber(B& b, const P& p) {
    const uint32_t seed = 91;
    const int kind = p.i("wood");
    // backing earth so gaps read dark
    b.style(V3(0.1, 0.08, 0.06), MATTE);
    b.box(V3(-0.3, 1.7, 0), V3(0.2, 3.4, 6.3));
    adit(b);
    // sills and posts
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(timber(3, seed, 1 + sz), WOOD);
        b.box(V3(0.0, 0.07, sz * 1.75), V3(0.75, 0.14, 0.5), 0.01);
        b.style(timber(kind, seed, 3 + sz), WOOD);
        b.box(V3(0.0, 1.32, sz * 1.76), V3(0.34, 2.36, 0.34), 0.02);
    }
    // header cap and the braces
    b.style(timber(kind, seed, 8), WOOD);
    b.box(V3(0.0, 2.68, 0), V3(0.36, 0.36, 4.5), 0.025);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(timber(kind, seed, 9 + sz), WOOD);
        b.beam(V3(0.0, 1.85, sz * 1.62), V3(0.0, 2.52, sz * 1.0), 0.14, 0.14, V3(1, 0, 0), 0.01);
        b.style(V3(0.2, 0.2, 0.22), METAL);
        for (int k = 0; k < 2; k++) b.cyl(V3(0.18, 2.68 + (k ? 0.08 : -0.08), sz * (2.0 - 0.3 * k)), V3(0.22, 2.68 + (k ? 0.08 : -0.08), sz * (2.0 - 0.3 * k)), 0.022, 6);  // bolt heads
    }
    // lagging: horizontal planks in the wings and above the header
    const double lx = -0.17;
    for (int row = 0; row < 16; row++) {
        const double y = 0.12 + row * 0.215;
        const double ph = 0.2;
        const bool above = y > 2.9;
        const double zs[2][2] = {{-3.05, -1.95}, {1.95, 3.05}};
        const int wings = above ? 1 : 2;
        for (int wg = 0; wg < wings; wg++) {
            const double z0 = above ? -3.05 : zs[wg][0], z1 = above ? 3.05 : zs[wg][1];
            double z = z0;
            int k = 0;
            while (z < z1 - 0.05) {
                const double len = std::min(z1 - z, 0.8 + 1.4 * hash01(seed + row, wg * 17 + k));
                if (hash01(seed + 3, row * 11 + wg * 3 + k) > 0.1) {
                    b.style(timber(k % 3 == 0 ? 3 : (row % 2 ? 1 : kind), seed + 2, row * 13 + wg * 5 + k, 0.2), WOOD);
                    b.box(V3(lx + 0.012 * (hash01(seed + 4, row + k) - 0.5), y + ph / 2, (z + z + len) / 2), V3(0.05, ph, len - 0.012));
                }
                z += len;
                k++;
            }
        }
        if (y > 3.5) break;
    }
    // posts' flanking uprights holding the wings
    b.style(timber(3, seed, 20), WOOD);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(-0.1, 1.9, sz * 3.1), V3(0.14, 3.8, 0.14));
    // boarded across (optional)
    if (p.i("boarded")) {
        b.style(timber(1, seed, 30), WOOD);
        b.beam(V3(0.28, 0.3, -1.4), V3(0.28, 2.3, 1.1), 0.22, 0.04, V3(1, 0, 0));
        b.beam(V3(0.3, 2.1, -1.4), V3(0.3, 0.5, 1.4), 0.22, 0.04, V3(1, 0, 0));
        b.beam(V3(0.27, 1.2, -1.6), V3(0.27, 1.2, 1.6), 0.2, 0.035, V3(1, 0, 0));
    }
    // signs
    signBoard(b, 0.2, 3.28, 0.0, "NO 3 ADIT", 2.3, 0.36, 0.036, V3(0.52, 0.5, 0.42), V3(0.12, 0.1, 0.08), seed);
    signBoard(b, 0.0, 1.5, 2.5, "KEEP OUT", 1.22, 0.3, 0.023, V3(0.7, 0.68, 0.6), V3(0.55, 0.1, 0.08), seed + 7);
    if (p.i("lamp")) lantern(b, V3(-0.4, 1.95, 0), true);
    minerubble(b, seed, 3.2, 9);
}

// B: concrete headwall with splayed wings and a swung-open steel gate.
void gateLeaf(B& b, double w, double h, const V3& col) {
    b.style(col, METAL);
    b.box(V3(w / 2, 0.1, 0), V3(w, 0.06, 0.04));
    b.box(V3(w / 2, h - 0.1, 0), V3(w, 0.06, 0.04));
    for (double z : {0.03, w - 0.03}) b.box(V3(z, h / 2, 0), V3(0.06, h, 0.05));
    b.box(V3(w / 2, h / 2, 0), V3(w, 0.05, 0.03));
    const int n = 11;
    for (int i = 1; i < n; i++) b.cyl(V3(i * w / n, 0.1, 0), V3(i * w / n, h - 0.1, 0), 0.011, 5);
    b.beam(V3(0.05, 0.12, 0.0), V3(w - 0.05, h - 0.12, 0.0), 0.03, 0.025);
    // hinges
    b.style(V3(0.1, 0.1, 0.1), METAL);
    for (double y : {0.25, h - 0.25}) b.cyl(V3(0.0, y - 0.06, 0), V3(0.0, y + 0.06, 0), 0.025, 6);
}

void portalConcrete(B& b, const P& p) {
    const uint32_t seed = 93;
    const double wear = p("wear");
    adit(b);
    const V3 conc(0.55, 0.54, 0.51);
    b.style(conc, CONCRETE);
    // headwall: two jambs and the lintel wall above, depth 0.7
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(-0.3, 1.75, sz * 2.4), V3(0.7, 3.5, 1.6), 0.04);
    b.box(V3(-0.3, 3.0, 0), V3(0.7, 1.2, 6.4), 0.04);   // above the opening: y 2.4..3.6
    b.style(conc * 1.05, CONCRETE);
    b.box(V3(-0.25, 3.62, 0), V3(0.9, 0.14, 6.6), 0.03);  // coping slab
    // reveal chamfers round the opening
    b.style(conc * 0.9, CONCRETE);
    b.box(V3(0.04, 2.42, 0), V3(0.08, 0.1, 3.4), 0.0);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0.04, 1.2, sz * 1.65), V3(0.08, 2.4, 0.1), 0.0);
    // wing walls splayed back, stepping down
    for (int sz = -1; sz <= 1; sz += 2) {
        b.at(V3(-0.5, 0, sz * 3.2), sz * -150 + (sz > 0 ? 0 : 0) , [&] {
            b.style(conc * 0.97, CONCRETE);
            b.extrude({V2(0, 0), V2(2.6, 0), V2(2.6, 0.9), V2(1.2, 2.2), V2(0, 3.5)}, 0.45);
        });
    }
    // form-tie holes and weathering streaks
    b.style(V3(0.18, 0.18, 0.17), CONCRETE);
    for (int r = 0; r < 4; r++)
        for (int c = -6; c <= 6; c++) {
            const double z = c * 0.5 + 0.25;
            if (std::fabs(z) < 1.9 && r * 0.6 + 0.5 < 2.5) continue;
            const double y = 0.5 + r * 0.7;
            if (y > 3.4) continue;
            b.cyl(V3(0.05, y, z), V3(0.056, y, z), 0.025, 6);
        }
    for (int k = 0; k < 9; k++) {
        b.color(mix(conc * 0.7, V3(0.36, 0.28, 0.2), 0.5 * wear));
        const double z = -3.0 + 6.0 * hash01(seed, k), h = 0.6 + 1.6 * hash01(seed + 1, k);
        if (std::fabs(z) < 1.75 && h < 2.5) continue;
        b.box(V3(0.052, 3.45 - h / 2, z), V3(0.006, h, 0.1 + 0.1 * hash01(seed + 2, k)));
    }
    // cracks
    b.color(V3(0.1, 0.1, 0.1));
    for (int k = 0; k < 3; k++) {
        const double z = (k - 1) * 2.4 + 0.5, y = 2.6;
        std::vector<V3> cr;
        for (int i = 0; i < 6; i++) cr.push_back(V3(0.056, y + i * 0.12 + 0.0, z + 0.08 * std::sin(i * 1.7 + k)));
        b.tube(cr, 0.004, 3);
    }
    // gate: the left leaf open against the wall, the right ajar; a chain and padlock
    b.at(V3(0.12, 0.06, 1.62), 100, [&] { gateLeaf(b, 1.6, 2.28, V3(0.22, 0.2, 0.18)); });
    b.at(V3(0.12, 0.06, -1.62), -40, [&] { b.mirrorZ(); gateLeaf(b, 1.6, 2.28, V3(0.22, 0.2, 0.18)); });
    b.style(V3(0.15, 0.15, 0.15), METAL);
    b.tube({V3(0.4, 1.2, -0.8), V3(0.35, 1.1, -0.95), V3(0.5, 1.05, -1.2)}, 0.012, 4);
    // lettering on the lintel and the warning plate
    b.style(V3(0.1, 0.1, 0.1), MATTE);
    b.letters("NO 3 ADIT", V3(0.05, 3.1, 0.0), 0.06, 0.012);
    b.style(V3(0.82, 0.82, 0.78), MATTE);
    b.box(V3(0.06, 1.65, 2.45), V3(0.02, 0.8, 1.2), 0.004);
    b.style(V3(0.62, 0.08, 0.06), MATTE);
    b.box(V3(0.075, 1.9, 2.45), V3(0.012, 0.22, 1.12));
    b.style(V3(0.95, 0.95, 0.92), MATTE);
    b.letters("DANGER", V3(0.082, 1.9, 2.45), 0.025, 0.006);
    b.style(V3(0.12, 0.1, 0.1), MATTE);
    b.letters("KEEP OUT", V3(0.07, 1.55, 2.45), 0.022, 0.006);
    // warning chevrons on the jambs
    for (int sz = -1; sz <= 1; sz += 2)
        for (int k = 0; k < 8; k++) {
            b.style(k % 2 ? V3(0.1, 0.1, 0.1) : V3(0.85, 0.65, 0.08), MATTE);
            b.box(V3(0.052, 0.15 + k * 0.28, sz * 1.72), V3(0.008, 0.13, 0.18));
        }
    if (p.i("lamp")) lantern(b, V3(-0.4, 1.95, 0), true);
    minerubble(b, seed, 3.8, 6);
}

// C: log crib portal: round-log posts, cribbed wings, log lagging above.
void portalLog(B& b, const P& p) {
    const uint32_t seed = 95 + (uint32_t)p.i("seed");
    b.style(V3(0.1, 0.08, 0.06), MATTE);
    b.box(V3(-0.5, 1.8, 0), V3(0.2, 3.6, 6.8));
    adit(b);
    // posts and cap, round logs
    for (int sz = -1; sz <= 1; sz += 2) {
        logPiece(b, V3(0.0, 0.0, sz * 1.78), V3(0.0, 2.55, sz * 1.78), 0.2, timber(0, seed, 1 + sz, 0.15), timber(2, seed, 2) * 1.05, 8);
        b.style(V3(0.4, 0.38, 0.35), ROCK);
        b.box(V3(0.0, 0.07, sz * 1.78), V3(0.6, 0.14, 0.6), 0.02);
    }
    logPiece(b, V3(0.0, 2.72, -2.25), V3(0.0, 2.72, 2.25), 0.22, timber(0, seed, 5, 0.15), timber(2, seed, 6) * 1.05, 8);
    for (int sz = -1; sz <= 1; sz += 2) logPiece(b, V3(0.0, 1.9, sz * 1.6), V3(0.0, 2.55, sz * 1.0), 0.09, timber(3, seed, 7 + sz), timber(2, seed, 8), 6);
    // cribbing: alternate courses of logs along z and along x, both sides, stepping up toward the hill
    for (int sz = -1; sz <= 1; sz += 2) {
        for (int c = 0; c < 15; c++) {
            const double y = 0.15 + c * 0.23;
            const double zs = sz * 2.7;
            // the crib steps in a little as it rises
            const double inset = c > 10 ? (c - 10) * 0.12 : 0.0;
            if (c % 2 == 0) {
                for (double x : {0.12, -0.75}) {
                    const double len = 1.5 - inset;
                    logPiece(b, V3(x, y, zs - sz * len / 2 + sz * 0.45 * 0 + sz * 0.0 + (sz > 0 ? 0.0 : 0.0)), V3(x, y, zs + sz * len / 2 + sz * 0.1), 0.115 + 0.012 * hash01(seed, c * 3),
                             timber(c % 3 == 0 ? 3 : 0, seed + sz, c * 7, 0.15), timber(2, seed, c) * 1.05, 6);
                }
            } else {
                for (double z : {zs - sz * (0.55 - inset * 0.5), zs + sz * (0.55 - inset * 0.5)}) {
                    logPiece(b, V3(0.5 - inset * 0.3, y, z), V3(-1.0, y, z), 0.115 + 0.012 * hash01(seed + 1, c * 3 + 1), timber(c % 4 == 1 ? 3 : 0, seed + sz, c * 7 + 3, 0.15), timber(2, seed, c + 20) * 1.05, 6);
                }
            }
        }
    }
    // log lagging over the header
    for (int r = 0; r < 4; r++) {
        const double y = 3.0 + r * 0.24;
        logPiece(b, V3(-0.12, y, -3.1), V3(-0.12, y, 3.1), 0.12, timber(r % 2 ? 3 : 0, seed, 40 + r, 0.15), timber(2, seed, 44 + r) * 1.05, 6);
    }
    // hung sign
    b.style(V3(0.15, 0.15, 0.15), METAL);
    for (int e = -1; e <= 1; e += 2) b.cyl(V3(0.22, 2.9, e * 1.0), V3(0.22, 3.15, e * 1.0), 0.008, 4);
    signBoard(b, 0.26, 2.65, 0.0, "NO 3 ADIT", 2.2, 0.34, 0.034, V3(0.22, 0.18, 0.14), V3(0.78, 0.75, 0.65), seed);
    signBoard(b, 0.24, 1.2, 2.5, "KEEP OUT", 1.15, 0.28, 0.022, V3(0.68, 0.66, 0.58), V3(0.5, 0.1, 0.08), seed + 3);
    if (p.i("lamp")) lantern(b, V3(-0.4, 1.95, 0), true);
    minerubble(b, seed, 3.6, 8);
}

Reg minePortal({"mine_portal", "MINE PORTAL", "BUILDINGS", "The old mine's entrance, set into the hillside, facing +X", 6.5,
                {
                    {"TIMBER SET", "Square posts, header and knee braces, lagging boards",
                     {choice("wood", "WOOD", 0, {"BROWN", "SILVER", "PINE", "DARK", "RED"}), choice("boarded", "BOARDED UP", 0, {"OFF", "ON"}), choice("lamp", "LANTERN", 0, {"OFF", "ON"})},
                     portalTimber},
                    {"CONCRETE PORTAL", "Stained concrete headwall, splayed wings, steel gate ajar",
                     {num("wear", "STAINING", 0.6, 0, 1, 0.05), choice("lamp", "LANTERN", 0, {"OFF", "ON"})}, portalConcrete},
                    {"LOG CRIB", "Round-log posts, cribbed wing walls, hung sign", {seedParam(1), choice("lamp", "LANTERN", 0, {"OFF", "ON"})}, portalLog},
                }});


// ============================================================================================
// FIRE TOWER
// ============================================================================================

// A leg's centre line at height y: feet at (+-1.75, +-1.75), leaning in 0.2 m by the platform.
V3 legAt(int sx, int sz, double y) {
    const double k = 1.75 - 0.2 * y / 14.1;
    return V3(sx * k, y, sz * k);
}

const double kLevels[5] = {0.7, 4.2, 7.6, 11.0, 13.7};

// Footing piers.
void towerFeet(B& b, bool steel) {
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(V3(0.52, 0.51, 0.48), CONCRETE);
            b.box(V3(sx * 1.75, 0.15, sz * 1.75), V3(0.6, 0.3, 0.6), 0.03);
            if (steel) {
                b.style(V3(0.35, 0.36, 0.38), METAL);
                b.box(V3(sx * 1.75, 0.32, sz * 1.75), V3(0.4, 0.03, 0.4));
                for (int a = -1; a <= 1; a += 2)
                    for (int c = -1; c <= 1; c += 2) b.cyl(V3(sx * 1.75 + a * 0.15, 0.3, sz * 1.75 + c * 0.15), V3(sx * 1.75 + a * 0.15, 0.4, sz * 1.75 + c * 0.15), 0.014, 6);
            }
        }
}

// The zig-zag stair inside the legs, landings, rails, and a short ladder through the hatch.
void towerStairs(B& b, const V3& wood, bool steel, uint32_t seed) {
    const double lanes[4] = {-1.05, -0.35, 0.35, 1.05};
    const double rise = 3.3, y0 = 0.35, xa = 1.4, xb = -1.4;
    const V3 stringer = steel ? V3(0.3, 0.32, 0.34) : wood * 0.8;
    for (int f = 0; f < 4; f++) {
        const double dir = (f % 2 == 0) ? -1 : 1;  // flight 0 goes toward -x
        const double xs = dir < 0 ? xa : xb, xe = dir < 0 ? xb : xa;
        const double ys = y0 + f * rise, ye = ys + rise;
        const double z = lanes[f];
        const int n = 17;
        // stringers
        b.style(stringer, steel ? METAL : WOOD);
        for (int e = -1; e <= 1; e += 2) b.beam(V3(xs, ys - 0.05, z + e * 0.3), V3(xe, ye - 0.05, z + e * 0.3), 0.05, 0.2, V3(0, 1, 0));
        // treads
        for (int i = 0; i < n; i++) {
            const double t = (i + 0.5) / n;
            const double x = xs + (xe - xs) * t, y = ys + (ye - ys) * (i + 1.0) / n;
            if (steel) b.style(V3(0.38, 0.4, 0.42), METAL);
            else b.style(timber(2, seed, f * 20 + i, 0.15) * 0.85, WOOD);
            b.box(V3(x, y - 0.015, z), V3(std::fabs(xe - xs) / n + 0.03, 0.035, 0.62));
        }
        // handrails each side
        b.style(steel ? V3(0.35, 0.37, 0.4) : wood * 0.9, steel ? METAL : WOOD);
        for (int e = -1; e <= 1; e += 2) {
            if ((f == 0 && e < 0) || (f == 3 && e > 0)) {}  // outer sides get rails too
            const double zr = z + e * 0.3;
            b.beam(V3(xs, ys + 0.95, zr), V3(xe, ye + 0.95, zr), 0.05, 0.05, V3(0, 1, 0));
            for (int k = 0; k <= 3; k++) {
                const double t = k / 3.0;
                const double x = xs + (xe - xs) * t, y = ys + (ye - ys) * t;
                b.box(V3(x, y + 0.5, zr), V3(0.04, 0.9, 0.04));
            }
        }
        // landing at the end of the flight, spanning this lane and the next
        const double zl0 = z - 0.35, zl1 = (f < 3 ? lanes[f + 1] : z) + 0.35;
        b.style(steel ? V3(0.36, 0.38, 0.4) : timber(0, seed, 90 + f, 0.12), steel ? METAL : WOOD);
        const double lx = f == 3 ? 1.8 : xe + dir * 0.4;
        b.box(V3(lx, ye - 0.04, (zl0 + zl1) / 2), V3(f == 3 ? 1.0 : 0.8, 0.06, zl1 - zl0));
        // landing rails (the open end)
        b.style(steel ? V3(0.35, 0.37, 0.4) : wood * 0.9, steel ? METAL : WOOD);
        if (f < 3) {
            const double xr = xe + dir * 0.78;
            b.beam(V3(xr, ye + 0.95, zl0), V3(xr, ye + 0.95, zl1), 0.05, 0.05, V3(0, 1, 0));
            for (int k = 0; k <= 2; k++) b.box(V3(xr, ye + 0.5, zl0 + (zl1 - zl0) * k / 2.0), V3(0.04, 0.95, 0.04));
        }
        // posts down from the landing
        b.style(steel ? V3(0.32, 0.34, 0.36) : timber(3, seed, 95 + f), steel ? METAL : WOOD);
        for (int e = -1; e <= 1; e += 2) b.box(V3(xe + dir * 0.75, (ye - 0.08) / 2, e > 0 ? zl1 - 0.05 : zl0 + 0.05), V3(0.09, ye - 0.08, 0.09));
    }
    // a short ladder from the top landing up through the hatch
    const double yt = y0 + 3 * rise + rise;
    b.style(steel ? V3(0.35, 0.37, 0.4) : wood * 0.9, steel ? METAL : WOOD);
    for (int e = -1; e <= 1; e += 2) b.box(V3(2.0, (yt + 14.7) / 2, 1.05 + e * 0.22), V3(0.05, 14.7 - yt, 0.05));
    for (int k = 0; k < 4; k++) b.box(V3(2.0, yt + 0.15 + k * 0.27, 1.05), V3(0.04, 0.035, 0.44));
}

// Cross bracing and girts: all four faces, X braces in each bay.
void towerBracing(B& b, bool steel, uint32_t seed) {
    for (int lv = 0; lv < 5; lv++) {
        const double y = kLevels[lv];
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                b.style(steel ? V3(0.4, 0.42, 0.44) : timber(lv % 2 ? 0 : 1, seed, lv * 9 + sx + 2 * sz, 0.12), steel ? METAL : WOOD);
                const V3 a = legAt(sx, sz, y), c = legAt(-sx, sz, y), d = legAt(sx, -sz, y);
                if (sx > 0) b.beam(a + V3(0, 0, sz * 0.0), c, 0.1, steel ? 0.08 : 0.14, V3(0, 1, 0));
                if (sz > 0) b.beam(a, d, 0.1, steel ? 0.08 : 0.14, V3(0, 1, 0));
            }
    }
    for (int bay = 0; bay < 4; bay++) {
        const double ya = kLevels[bay], yb = kLevels[bay + 1];
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                // face z = sz*L (between legs of the same sz): braces in x
                const V3 a0 = legAt(sx, sz, ya), a1 = legAt(-sx, sz, yb), b0 = legAt(-sx, sz, ya), b1 = legAt(sx, sz, yb);
                const V3 c0 = legAt(sx, sz, ya), c1 = legAt(sx, -sz, yb), d0 = legAt(sx, -sz, ya), d1 = legAt(sx, sz, yb);
                if (sx > 0) {
                    b.style(steel ? V3(0.42, 0.44, 0.46) : timber(0, seed, 50 + bay * 4 + sz, 0.14), steel ? METAL : WOOD);
                    const V3 off(0, 0, sz * 0.045);
                    if (steel) {
                        // lattice: a warren of shorter diagonals
                        const int m = 3;
                        for (int k = 0; k < 2 * m; k++) {
                            const double t0 = (double)k / (2 * m), t1 = (double)(k + 1) / (2 * m);
                            const V3 p0 = mix(a0, b0, t0), p1 = mix(a0, b0, t1);
                            const V3 q0 = mix(b1, a1, 1 - t0), q1 = mix(b1, a1, 1 - t1);
                            (void)q0; (void)q1;
                            const V3 lo0 = mix(legAt(sx, sz, ya), legAt(-sx, sz, ya), t0), lo1 = mix(legAt(sx, sz, ya), legAt(-sx, sz, ya), t1);
                            const V3 hi0 = mix(legAt(sx, sz, yb), legAt(-sx, sz, yb), t0), hi1 = mix(legAt(sx, sz, yb), legAt(-sx, sz, yb), t1);
                            (void)p0; (void)p1;
                            if (k % 2 == 0) b.beam(lo0 + off, hi1 + off, 0.05, 0.05, V3(0, 1, 0));
                            else b.beam(hi0 + off, lo1 + off, 0.05, 0.05, V3(0, 1, 0));
                        }
                    } else {
                        b.beam(a0 + off, a1 + off, 0.09, 0.07, V3(0, 0, 1));
                        b.beam(b0 + off * 1.8, b1 + off * 1.8, 0.09, 0.07, V3(0, 0, 1));
                    }
                } else {
                    // handled in the other direction below
                }
                (void)c0; (void)c1; (void)d0; (void)d1;
            }
        // the faces at x = +-L, braces running in z
        for (int sx = -1; sx <= 1; sx += 2) {
            const V3 off(sx * 0.045, 0, 0);
            b.style(steel ? V3(0.42, 0.44, 0.46) : timber(0, seed, 70 + bay * 4 + sx, 0.14), steel ? METAL : WOOD);
            if (steel) {
                const int m = 3;
                for (int k = 0; k < 2 * m; k++) {
                    const double t0 = (double)k / (2 * m), t1 = (double)(k + 1) / (2 * m);
                    const V3 lo0 = mix(legAt(sx, -1, ya), legAt(sx, 1, ya), t0), lo1 = mix(legAt(sx, -1, ya), legAt(sx, 1, ya), t1);
                    const V3 hi0 = mix(legAt(sx, -1, yb), legAt(sx, 1, yb), t0), hi1 = mix(legAt(sx, -1, yb), legAt(sx, 1, yb), t1);
                    if (k % 2 == 0) b.beam(lo0 + off, hi1 + off, 0.05, 0.05, V3(1, 0, 0));
                    else b.beam(hi0 + off, lo1 + off, 0.05, 0.05, V3(1, 0, 0));
                }
            } else {
                b.beam(legAt(sx, -1, ya) + off, legAt(sx, 1, yb) + off, 0.09, 0.07, V3(1, 0, 0));
                b.beam(legAt(sx, 1, ya) + off * 1.8, legAt(sx, -1, yb) + off * 1.8, 0.09, 0.07, V3(1, 0, 0));
            }
        }
    }
}

// The platform: joists, deck, hatch and its door, railing.
void towerDeck(B& b, bool steel, uint32_t seed) {
    const double hy = 14.1;
    // outriggers from the leg tops and joists
    b.style(steel ? V3(0.36, 0.38, 0.4) : timber(3, seed, 120), steel ? METAL : WOOD);
    for (int k = -4; k <= 4; k++) {
        const double x = k * 0.5;
        if (steel) ibeam(b, V3(x, hy - 0.2, -2.3), V3(x, hy - 0.2, 2.3), 0.16, 0.08, V3(0, 1, 0), 0.01, 0.012);
        else b.box(V3(x, hy - 0.14, 0), V3(0.1, 0.16, 4.7));
    }
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 2.25, hy - 0.14, 0), V3(0.14, 0.2, 4.6));
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, hy - 0.14, sz * 2.25), V3(4.6, 0.2, 0.14));
    // deck: boards along z, with the hatch opening cut
    const double hx0 = 1.72, hx1 = 2.28, hz0 = 0.68, hz1 = 1.42;
    const int nb = 29;
    for (int i = 0; i < nb; i++) {
        const double x = -2.3 + (i + 0.5) * 4.6 / nb;
        const double bw = 4.6 / nb - 0.012;
        if (steel) b.style(V3(0.34, 0.36, 0.38) * (0.9 + 0.2 * hash01(seed, i)), METAL);
        else b.style(timber(1, seed, 130 + i, 0.14), WOOD);
        auto seg = [&](double z0, double z1) { b.box(V3(x, hy - 0.025, (z0 + z1) / 2), V3(bw, 0.05, z1 - z0), 0.003 * 0); };
        if (x + bw / 2 > hx0 && x - bw / 2 < hx1) {
            seg(-2.3, hz0);
            seg(hz1, 2.3);
        } else seg(-2.3, 2.3);
    }
    // the hatch door standing open against its rail
    b.style(steel ? V3(0.4, 0.42, 0.45) : timber(0, seed, 160), steel ? METAL : WOOD);
    b.box(V3(2.28, hy + 0.35, 1.05), V3(0.04, 0.7, 0.74));
    // railing: posts every ~0.77 m round the edge, two rails and a toe board
    const double ry = hy + 1.05;
    b.style(steel ? V3(0.4, 0.42, 0.45) : timber(0, seed, 170), steel ? METAL : WOOD);
    for (int k = 0; k <= 6; k++) {
        const double t = -2.3 + k * 4.6 / 6;
        for (int s = -1; s <= 1; s += 2) {
            if (steel) {
                b.cyl(V3(s * 2.3, hy, t), V3(s * 2.3, ry, t), 0.025, 6);
                if (k > 0 && k < 6) b.cyl(V3(t, hy, s * 2.3), V3(t, ry, s * 2.3), 0.025, 6);
            } else {
                b.box(V3(s * 2.3, hy + 0.52, t), V3(0.08, 1.05, 0.08), 0.005);
                if (k > 0 && k < 6) b.box(V3(t, hy + 0.52, s * 2.3), V3(0.08, 1.05, 0.08), 0.005);
            }
        }
    }
    for (int s = -1; s <= 1; s += 2) {
        for (double y : {ry, hy + 0.55}) {
            if (steel) {
                b.tube({V3(s * 2.3, y, -2.3), V3(s * 2.3, y, 2.3)}, 0.02, 6);
                b.tube({V3(-2.3, y, s * 2.3), V3(2.3, y, s * 2.3)}, 0.02, 6);
            } else {
                b.box(V3(s * 2.3, y, 0), V3(0.05, 0.06, 4.6));
                b.box(V3(0, y, s * 2.3), V3(4.6, 0.06, 0.05));
            }
        }
        b.box(V3(s * 2.3, hy + 0.08, 0), V3(0.02, 0.14, 4.6));
        b.box(V3(0, hy + 0.08, s * 2.3), V3(4.6, 0.14, 0.02));
    }
}

// The cab: windows all round, a door on the front, a hipped roof with a lightning rod.
void towerCab(B& b, const V3& paintCol, bool steel, uint32_t seed) {
    const double fy = 14.1, h = 2.0, hw = 1.7;
    const V3 trim = steel ? V3(0.82, 0.82, 0.78) : V3(0.85, 0.82, 0.74);
    // sill beam and corner posts
    b.style(trim * 0.9, steel ? PAINT : WOOD);
    b.box(V3(0, fy + 0.08, 0), V3(2 * hw + 0.1, 0.12, 2 * hw + 0.1), 0.01);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * hw, fy + h / 2 + 0.08, sz * hw), V3(0.14, h, 0.14), 0.01);
    for (int s = 0; s < 4; s++) {
        b.at(V3(0, 0, 0), s * 90, [&] {
            // lower panel: boards
            const int nboards = 7;
            for (int i = 0; i < nboards; i++) {
                const double y = fy + 0.18 + i * 0.12;
                if (steel) b.style(mix(paintCol, V3(0.4, 0.4, 0.4), 0.2) * (0.9 + 0.2 * hash01(seed + s, i)), PAINT);
                else b.style(jit(mix(paintCol, V3(0.5, 0.45, 0.4), 0.25), seed + s, i, 0.1), WOOD);
                if (s == 0) {
                    // the door in the middle of the front lower panel: open the wall behind it
                    b.box(V3(hw, y, 0.0), V3(0.05, 0.115, 2 * hw - 0.2));
                } else b.box(V3(hw, y, 0.0), V3(0.05, 0.115, 2 * hw - 0.2));
            }
            // window band: sill rail, head rail, mullions, glass
            b.style(trim, steel ? PAINT : WOOD);
            b.box(V3(hw + 0.01, fy + 1.07, 0), V3(0.08, 0.06, 2 * hw - 0.1));
            b.box(V3(hw + 0.01, fy + h - 0.1, 0), V3(0.08, 0.1, 2 * hw - 0.1));
            for (int k = -2; k <= 2; k++) b.box(V3(hw + 0.01, fy + 1.55, k * 0.57 * 1.0), V3(0.07, 0.9, 0.05));
            b.style(V3(0.28, 0.4, 0.45), GLASS);
            b.box(V3(hw - 0.005, fy + 1.55, 0), V3(0.012, 0.88, 2 * hw - 0.28));
            if (s == 0) {
                // door: planks over the lower panel and the glass line
                b.style(timber(3, seed, 180), WOOD);
                b.box(V3(hw + 0.045, fy + 0.62, 0.0), V3(0.045, 1.0, 0.92));
                b.style(V3(0.3, 0.42, 0.46), GLASS);
                b.box(V3(hw + 0.07, fy + 0.8, 0.0), V3(0.012, 0.5, 0.5));
                b.style(V3(0.7, 0.7, 0.7), METAL);
                b.cyl(V3(hw + 0.07, fy + 0.45, 0.32), V3(hw + 0.13, fy + 0.45, 0.32), 0.018, 6);
            }
        });
    }
    // soffit and the roof
    const double ry = fy + h + 0.1;
    b.style(trim * 0.7, WOOD);
    b.box(V3(0, ry - 0.04, 0), V3(2 * hw + 0.9, 0.08, 2 * hw + 0.9), 0.01);
    b.push();
    b.move(0, ry, 0);
    b.yaw(45);
    const double rr = (hw + 0.45) * std::sqrt(2.0);
    if (steel) b.style(V3(0.5, 0.52, 0.54), METAL);
    else b.style(V3(0.32, 0.25, 0.18), WOOD);
    b.lathe(V3(0, 0, 0), {V2(rr, 0.0), V2(0.0, 1.0)}, 4, false);
    b.pop();
    // hips and a finial
    b.style(steel ? V3(0.35, 0.37, 0.4) : timber(3, seed, 190), steel ? METAL : WOOD);
    for (int k = 0; k < 4; k++) {
        const double a = (k * 90 + 45) * PI / 180;
        const V3 c(std::cos(a), 0, std::sin(a));
        b.beam(V3(0, ry + 1.0, 0) + c * 0.05, V3(c.x * rr, ry + 0.0, c.z * rr) * 0.99 + V3(0, 0.0, 0), 0.1, 0.05, V3(0, 1, 0));
    }
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * (hw + 0.45), ry + 0.02, 0), V3(0.05, 0.1, 2 * (hw + 0.45)));
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, ry + 0.02, sz * (hw + 0.45)), V3(2 * (hw + 0.45), 0.1, 0.05));
    b.style(V3(0.2, 0.2, 0.22), METAL);
    b.cyl(V3(0, ry + 0.95, 0), V3(0, ry + 1.8, 0), 0.014, 5);
    b.sphere(V3(0, ry + 1.45, 0), V3(0.05, 0.05, 0.05), 1);
    b.style(V3(0.5, 0.5, 0.52), METAL);
    b.cyl(V3(0, ry + 1.7, -0.25), V3(0, ry + 1.7, 0.25), 0.008, 4);  // a little crossbar for a radio dipole
}

void fireTowerTimber(B& b, const P& p) {
    const uint32_t seed = 101;
    const int kind = p.i("wood");
    const V3 wood = timber(kind, seed, 1);
    const V3 cab = muted(paint(p.i("paint")), 0.35);
    towerFeet(b, false);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(timber(kind, seed, 3 + sx + 2 * sz, 0.12), WOOD);
            b.beam(legAt(sx, sz, 0.3), legAt(sx, sz, 14.0), 0.26, 0.26, V3(sx, 0, 0), 0.02);
            // bolt plates at each girt level
            b.style(V3(0.22, 0.22, 0.24), METAL);
            for (int lv = 0; lv < 5; lv++) {
                const V3 c = legAt(sx, sz, kLevels[lv]);
                b.box(V3(c.x - sx * 0.136, c.y, c.z), V3(0.012, 0.18, 0.18));
                b.box(V3(c.x, c.y, c.z - sz * 0.136), V3(0.18, 0.18, 0.012));
            }
        }
    towerBracing(b, false, seed);
    towerStairs(b, wood, false, seed);
    towerDeck(b, false, seed);
    towerCab(b, cab, false, seed);
}

void fireTowerSteel(B& b, const P& p) {
    const uint32_t seed = 103;
    const V3 cab = muted(paint(p.i("paint")), 0.2);
    const double rust = p("rust");
    towerFeet(b, true);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(mix(V3(0.45, 0.47, 0.5), rustCol(seed, sx + sz * 3) * 0.9, rust * 0.5), METAL);
            // angle-iron legs: two plates making an L
            const V3 a = legAt(sx, sz, 0.34), e = legAt(sx, sz, 14.0);
            b.beam(a + V3(sx * 0.06, 0, 0), e + V3(sx * 0.06, 0, 0), 0.016, 0.16, V3(1, 0, 0));
            b.beam(a + V3(0, 0, sz * 0.06), e + V3(0, 0, sz * 0.06), 0.16, 0.016, V3(1, 0, 0));
            b.beam(a - V3(sx * 0.0, 0, sz * 0.0), e, 0.07, 0.07, V3(1, 0, 0));
        }
    towerBracing(b, true, seed);
    towerStairs(b, V3(0.4, 0.4, 0.4), true, seed);
    towerDeck(b, true, seed);
    towerCab(b, cab, true, seed);
}

Reg fireTower({"fire_tower", "FIRE TOWER", "BUILDINGS", "The lookout on its knoll: legs, stair, platform and cab", 17.0,
               {
                   {"TIMBER TOWER", "Square timber legs and X-bracing, shingled cab", {choice("wood", "WOOD", 1, {"BROWN", "SILVER", "PINE", "DARK", "RED"}), paintParam(7)}, fireTowerTimber},
                   {"STEEL LATTICE", "Angle-iron legs and lattice, steel deck and a tin cab",
                    {paintParam(0), num("rust", "RUST", 0.3, 0, 1, 0.05)}, fireTowerSteel},
               }});


// ============================================================================================
// WRECKED PICKUP
// ============================================================================================

struct TruckOpts {
    V3 paint{0.28, 0.45, 0.43};
    double rust = 0.6;
    bool wheels[4] = {true, true, true, true};  // FL, FR, RL, RR (front is +x, left +z)
    int hood = 1;        // 0 off, 1 closed, 2 ajar
    double doorL = 0, doorR = 0;  // degrees open; < 0: missing
    bool glass = true;   // some glass left in the frames
    bool engine = true;
    bool seat = true;
    int tailgate = 1;    // 0 off, 1 closed, 2 ajar
    double crush = 0;    // the roof pressed down (m)
    uint32_t seed = 7;
};

// The wreck's paint, worn: a panel colour and its material.
void panelStyle(B& b, const TruckOpts& o, int i, double extra = 0) {
    const double h = hash01(o.seed, i);
    const double t = std::clamp(o.rust * 0.95 * h * h + extra, 0.0, 1.0);
    const V3 base = mix(o.paint, V3(0.5, 0.48, 0.44), 0.18 + 0.25 * o.rust);
    b.style(mix(base * (0.85 + 0.25 * hash01(o.seed + 1, i)), rustCol(o.seed + 2, i), t), t > 0.38 ? METAL : PAINT);
}

// Dark rust-through holes and brown patches over a rectangle on the plane x=0 / z=0 faces of the frame:
// plane: 'z' faces (normal +-z) at zc, spanning x in [x0,x1], y in [y0,y1].
void truckPatches(B& b, const TruckOpts& o, int n, double x0, double x1, double y0, double y1, double zc, double nz, uint32_t sd) {
    for (int k = 0; k < n; k++) {
        const double x = x0 + (x1 - x0) * hash01(sd, k * 3), y = y0 + (y1 - y0) * hash01(sd, k * 3 + 1);
        const double w = 0.04 + 0.1 * hash01(sd, k * 3 + 2), h = 0.03 + 0.06 * hash01(sd + 1, k);
        const bool hole = hash01(sd + 2, k) > 0.85;
        b.color(hole ? V3(0.06, 0.05, 0.045) : rustCol(o.seed, 40 + k + (int)sd) * (0.75 + 0.4 * o.rust));
        b.style(b.curColor(), METAL);
        b.box(V3(x, y, zc + nz * 0.003), V3(w, h, 0.004));
    }
}

void tireAndRim(B& b, const TruckOpts& o, const V3& c, bool flat, int wi) {
    b.push();
    b.move(c);
    b.roll(90);  // axis along z
    if (flat) b.scale(V3(1.0, 1.0, 1.0));
    b.style(V3(0.045, 0.045, 0.05), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.21, -0.11), V2(0.31, -0.112), V2(0.37, -0.09), V2(0.39, -0.05), V2(0.395, -0.02), V2(0.395, 0.02), V2(0.39, 0.05), V2(0.37, 0.09), V2(0.31, 0.112), V2(0.21, 0.11)}, 14);
    // tread blocks: a few raised lugs
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    for (int k = 0; k < 14; k++) {
        const double a = 2 * PI * k / 14;
        b.push();
        b.yaw(-a * 180 / PI);
        b.box(V3(0.395, 0.0, 0), V3(0.014, 0.1, 0.07));
        b.pop();
    }
    // wheel rim: dished steel, rusty
    b.style(steelRust(0.7, o.seed, wi + 10), METAL);
    b.lathe(V3(0, 0, 0), {V2(0.0, -0.06), V2(0.2, -0.06), V2(0.215, -0.02), V2(0.215, 0.075), V2(0.19, 0.08), V2(0.1, 0.05), V2(0.0, 0.05)}, 14, false);
    b.style(V3(0.1, 0.09, 0.08), METAL);
    for (int k = 0; k < 5; k++) {
        const double a = 2 * PI * k / 5 + 0.3;
        b.box(V3(0.095 * std::cos(a), 0.058, -0.095 * std::sin(a)), V3(0.028, 0.02, 0.028));
    }
    b.style(steelRust(0.4, o.seed, wi + 20) * 1.2, CHROME);
    b.cyl(V3(0, 0.05, 0), V3(0, 0.085, 0), 0.06, 10);
    b.pop();
}

void truckDoor(B& b, const TruckOpts& o, int i0) {
    // local frame: hinge at x=0 (front edge), the door runs to -x
    panelStyle(b, o, i0);
    b.box(V3(-0.6, 0.9, 0), V3(1.2, 0.52, 0.045), 0.012);
    panelStyle(b, o, i0 + 1);
    b.box(V3(-0.6, 1.84, 0), V3(1.18, 0.045, 0.04));
    b.beam(V3(-0.035, 1.16, 0), V3(-0.07, 1.84, 0), 0.04, 0.05, V3(1, 0, 0));
    b.box(V3(-1.15, 1.5, 0), V3(0.05, 0.7, 0.04));
    // window crank, handle, a dent
    b.style(V3(0.5, 0.5, 0.48), CHROME);
    b.box(V3(-1.0, 1.1, 0.03), V3(0.12, 0.03, 0.03), 0.006);
    b.cyl(V3(-0.9, 1.0, 0.025), V3(-0.9, 1.0, 0.05), 0.012, 6);
    b.style(V3(0.08, 0.07, 0.06), METAL);
    b.box(V3(-0.6, 0.66, 0.0), V3(1.18, 0.012, 0.05));  // seam at the bottom
    truckPatches(b, o, 6, -1.1, -0.1, 0.7, 1.12, 0.0, 1.0, o.seed + i0);
    truckPatches(b, o, 4, -1.1, -0.1, 0.7, 1.12, 0.0, -1.0, o.seed + i0 + 5);
    if (o.glass) {
        b.style(V3(0.3, 0.42, 0.46), GLASS);
        b.tri(V3(-1.1, 1.18, 0.0), V3(-0.7, 1.18, 0.0), V3(-1.1, 1.5, 0.0));
        b.tri(V3(-0.1, 1.82, 0.0), V3(-0.4, 1.82, 0.0), V3(-0.1, 1.55, 0.0));
    }
}

// The whole truck, upright, wheels' bottoms at y = 0, front +x, left (driver) +z.
void buildTruck(B& b, const TruckOpts& o) {
    int pi = 0;
    // ---- chassis
    b.style(V3(0.12, 0.11, 0.1), METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(steelRust(0.8, o.seed, 100 + sz), METAL);
        b.box(V3(-0.1, 0.56, sz * 0.45), V3(4.95, 0.14, 0.07));
        b.box(V3(-0.1, 0.62, sz * 0.485), V3(4.95, 0.025, 0.05));
        b.box(V3(-0.1, 0.5, sz * 0.485), V3(4.95, 0.025, 0.05));
    }
    b.style(steelRust(0.7, o.seed, 105), METAL);
    for (double x : {2.15, 1.0, -0.2, -1.4, -2.45}) b.box(V3(x, 0.55, 0), V3(0.07, 0.09, 0.94));
    // axles, diff, springs, drive shaft, tank, exhaust
    b.style(V3(0.14, 0.12, 0.1), METAL);
    for (double x : {1.55, -1.5}) b.cyl(V3(x, 0.38, -0.8), V3(x, 0.38, 0.8), 0.04, 8);
    b.style(steelRust(0.6, o.seed, 106), METAL);
    b.sphere(V3(-1.5, 0.38, 0), V3(0.17, 0.15, 0.2), 1);
    b.cyl(V3(-1.4, 0.43, 0), V3(0.4, 0.52, 0), 0.03, 6);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(V3(0.16, 0.13, 0.11), METAL);
        b.tube({V3(-2.25, 0.55, sz * 0.58), V3(-1.5, 0.46, sz * 0.58), V3(-0.8, 0.55, sz * 0.58)}, 0.025, 5);
        b.tube({V3(1.0, 0.54, sz * 0.58), V3(1.55, 0.46, sz * 0.58), V3(2.1, 0.54, sz * 0.58)}, 0.025, 5);
    }
    b.style(steelRust(0.9, o.seed, 107), METAL);
    b.box(V3(-0.6, 0.38, 0.05), V3(0.62, 0.24, 0.62), 0.03);  // fuel tank
    b.tube({V3(1.35, 0.46, -0.3), V3(0.2, 0.42, -0.34), V3(-1.2, 0.4, -0.42), V3(-2.5, 0.4, -0.44)}, 0.026, 6);
    b.lathe(V3(-1.0, 0.42, -0.4), {V2(0.0, -0.3), V2(0.07, -0.28), V2(0.09, -0.2), V2(0.09, 0.2), V2(0.07, 0.28), V2(0.0, 0.3)}, 8);  // (muffler: the lathe stands up; turned below)
    // ---- engine
    if (o.engine) {
        b.style(V3(0.2, 0.22, 0.24), METAL);
        b.box(V3(1.15, 0.86, 0), V3(0.75, 0.42, 0.6), 0.04);
        b.style(steelRust(0.7, o.seed, 110), METAL);
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(1.17, 1.1, sz * 0.2), V3(0.52, 0.07, 0.17), 0.015);
        b.style(V3(0.18, 0.17, 0.16), METAL);
        b.lathe(V3(1.2, 1.12, 0), {V2(0.0, 0.0), V2(0.14, 0.0), V2(0.15, 0.04), V2(0.14, 0.07), V2(0.0, 0.07)}, 10);  // air cleaner
        b.style(steelRust(0.5, o.seed, 111), METAL);
        b.box(V3(1.98, 0.84, 0), V3(0.1, 0.66, 0.84), 0.015);  // radiator
        b.style(V3(0.1, 0.1, 0.1), MATTE);
        b.tube({V3(1.9, 1.05, 0.25), V3(1.6, 1.1, 0.2), V3(1.5, 1.0, 0.2)}, 0.02, 5);
        b.box(V3(1.7, 0.98, 0.52), V3(0.24, 0.2, 0.2), 0.02);  // battery
        b.style(V3(0.16, 0.16, 0.18), METAL);
        for (int k = 0; k < 4; k++) b.beam(V3(1.83, 0.84, 0), V3(1.83, 0.84, 0) + V3(0, 0.24 * std::cos(k * PI / 2 + 0.4), 0.24 * std::sin(k * PI / 2 + 0.4)), 0.04, 0.012, V3(1, 0, 0));
    }
    // ---- wheels
    const V3 wc[4] = {V3(1.55, 0.39, 0.8), V3(1.55, 0.39, -0.8), V3(-1.5, 0.39, 0.8), V3(-1.5, 0.39, -0.8)};
    for (int k = 0; k < 4; k++) {
        if (o.wheels[k]) tireAndRim(b, o, wc[k], false, k);
        else {  // a bare hub
            b.style(steelRust(0.8, o.seed, 120 + k), METAL);
            const double sz = wc[k].z > 0 ? 1 : -1;
            b.cyl(wc[k] + V3(0, 0, sz * 0.02), wc[k] + V3(0, 0, sz * 0.1), 0.09, 8);
            b.style(V3(0.1, 0.09, 0.08), METAL);
            b.cyl(wc[k] + V3(0, 0, sz * 0.1), wc[k] + V3(0, 0, sz * 0.14), 0.045, 6);
        }
    }
    // ---- front sheet metal: fender sides with arch, tops, hood, grille, bumper, lamps
    auto archPoly = [&](double xc, double x0, double x1, double yTop, double yFrontTop, bool front) {
        std::vector<V2> poly;
        poly.push_back(V2(x0, 0.58));
        const double yb = 0.58, r = 0.47, cy = 0.38;
        const double phi0 = std::asin((yb - cy) / r);
        for (int i = 0; i <= 12; i++) {
            const double a = PI - phi0 - (PI - 2 * phi0) * i / 12;
            poly.push_back(V2(xc + r * std::cos(a), cy + r * std::sin(a)));
        }
        poly.push_back(V2(x1, yb));
        if (front) {
            poly.push_back(V2(x1, yFrontTop));
            poly.push_back(V2(x1 - 0.2, yTop));
            poly.push_back(V2(x0, yTop));
        } else {
            poly.push_back(V2(x1, yTop));
            poly.push_back(V2(x0, yTop));
        }
        return poly;
    };
    for (int sz = -1; sz <= 1; sz += 2) {
        panelStyle(b, o, pi++);
        b.push();
        b.move(0, 0, sz * 0.915);
        b.extrude(archPoly(1.55, 0.55, 2.3, 1.04, 0.96, true), 0.035);
        b.pop();
        // fender tops
        panelStyle(b, o, pi++);
        b.box(V3(1.4, 1.04, sz * 0.74), V3(1.72, 0.03, 0.38), 0.008);
        // inner aprons beside the engine
        panelStyle(b, o, pi++, 0.15);
        b.box(V3(1.4, 0.78, sz * 0.54), V3(1.7, 0.5, 0.025));
        // rolled lip round the arch
        b.style(steelRust(0.6, o.seed, 130 + sz), METAL);
        std::vector<V3> arc;
        for (int i = 0; i <= 12; i++) {
            const double a = 0.43 + (PI - 0.86) * i / 12;
            arc.push_back(V3(1.55 + 0.47 * std::cos(a), 0.38 + 0.47 * std::sin(a), sz * 0.94));
        }
        b.tube(arc, 0.014, 5);
        truckPatches(b, o, 6, 0.6, 2.2, 0.62, 1.0, sz * 0.935, sz, o.seed + 20 + sz);
        // headlight: a chrome bucket, the glass gone
        b.style(V3(0.52, 0.52, 0.5), CHROME);
        b.lathe(V3(2.13, 0.97, sz * 0.7) , {V2(0.0, 0.0)}, 3);
        b.push();
        b.move(2.2, 0.97, sz * 0.7);
        b.pitch(-90);
        b.lathe(V3(0, 0, 0), {V2(0.0, -0.12), V2(0.1, -0.1), V2(0.115, -0.03), V2(0.115, 0.0), V2(0.1, 0.0), V2(0.0, -0.06)}, 12);
        b.style(V3(0.9, 0.85, 0.7) * 0.5, GLASS);
        if (hash01(o.seed, 77 + sz) > 0.5) b.cyl(V3(0, -0.06, 0), V3(0, -0.04, 0), 0.09, 10);
        b.pop();
    }
    // hood
    if (o.hood) {
        b.push();
        if (o.hood == 2) {
            b.move(0.62, 1.05, 0);
            b.pitch(34);
            b.move(-0.62, -1.05, 0);
        }
        panelStyle(b, o, pi++);
        b.box(V3(1.38, 1.08, 0), V3(1.5, 0.07, 1.06), 0.035);
        panelStyle(b, o, pi++);
        b.box(V3(1.38, 1.11, 0), V3(1.1, 0.02, 0.5), 0.01);  // the centre crease
        truckPatches(b, o, 8, 0.7, 2.05, 1.1, 1.13, 0.0, 0.0, o.seed + 31);
        b.pop();
    }
    // grille shell with chrome bars, bumper
    panelStyle(b, o, pi++, 0.2);
    b.box(V3(2.22, 0.79, 0), V3(0.07, 0.5, 1.02), 0.02);
    b.style(V3(0.55, 0.55, 0.52), CHROME);
    for (int k = 0; k < 5; k++) b.box(V3(2.27, 0.58 + 0.095 * k, 0), V3(0.025, 0.035, 0.98), 0.006);
    b.box(V3(2.27, 0.78, 0), V3(0.03, 0.5, 0.04), 0.006);
    panelStyle(b, o, pi++);
    b.box(V3(2.28, 1.0, 0), V3(0.08, 0.1, 1.04), 0.02);  // grille top / hood latch panel
    b.style(V3(0.55, 0.55, 0.53), CHROME);
    b.box(V3(2.47, 0.52, 0), V3(0.1, 0.15, 1.7), 0.04);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(2.4, 0.52, sz * 0.92), V3(0.22, 0.15, 0.1), 0.04);
        b.box(V3(2.5, 0.6, sz * 0.4), V3(0.06, 0.14, 0.05), 0.01);  // bumper guards
    }
    b.style(rustCol(o.seed, 140) * 0.9, METAL);
    for (int k = 0; k < 4; k++) b.box(V3(2.525, 0.5 + 0.03 * (k % 2), -0.7 + k * 0.45), V3(0.006, 0.07, 0.1 + 0.05 * k));
    // ---- cab
    const double roofY = 1.88 - o.crush;
    panelStyle(b, o, pi++, 0.1);
    b.box(V3(-0.07, 0.645, 0), V3(1.55, 0.04, 1.74), 0.01);   // floor
    panelStyle(b, o, pi++);
    b.box(V3(0.72, 1.0, 0), V3(0.04, 0.78, 1.74), 0.01);       // firewall / cowl
    panelStyle(b, o, pi++);
    b.box(V3(-0.84, 0.92, 0), V3(0.04, 0.56, 1.74), 0.01);     // rear panel, lower
    for (int sz = -1; sz <= 1; sz += 2) {
        panelStyle(b, o, pi++);
        b.box(V3(-0.84, 1.52, sz * 0.8), V3(0.05, 0.7, 0.14), 0.01);   // rear pillars
        b.beam(V3(-0.84, 1.17, sz * 0.8), V3(-0.84, roofY - 0.02, sz * 0.8), 0.14, 0.05, V3(1, 0, 0), 0.005);
    }
    panelStyle(b, o, pi++);
    b.box(V3(-0.84, roofY - 0.06, 0), V3(0.05, 0.1, 1.74), 0.01);  // rear header
    // roof
    panelStyle(b, o, pi++, 0.1);
    b.box(V3(-0.07, roofY, 0), V3(1.62, 0.05, 1.82), 0.03);
    b.style(V3(0.1, 0.09, 0.08), MATTE);
    b.box(V3(-0.07, roofY - 0.04, 0), V3(1.5, 0.03, 1.7));  // headliner
    truckPatches(b, o, 9, -0.8, 0.7, roofY + 0.025, roofY + 0.028, 0.0, 0.0, o.seed + 41);
    // the roof's rust blotches (on top: boxes in the x/z plane)
    for (int k = 0; k < 7; k++) {
        b.style(rustCol(o.seed, 150 + k) * (0.7 + o.rust * 0.4), METAL);
        b.box(V3(-0.75 + 1.4 * hash01(o.seed + 50, k), roofY + 0.027, -0.8 + 1.6 * hash01(o.seed + 51, k)), V3(0.1 + 0.2 * hash01(o.seed + 52, k), 0.004, 0.1 + 0.25 * hash01(o.seed + 53, k)));
    }
    // A pillars and the windscreen frame; header
    for (int sz = -1; sz <= 1; sz += 2) {
        panelStyle(b, o, pi++);
        b.beam(V3(0.7, 1.15, sz * 0.86), V3(0.52, roofY - 0.02, sz * 0.86), 0.05, 0.075, V3(1, 0, 0), 0.005);
    }
    panelStyle(b, o, pi++);
    b.beam(V3(0.7, 1.15, 0), V3(0.7, 1.15, 0.001), 0.01, 0.01, V3(1, 0, 0));
    b.box(V3(0.53, roofY - 0.04, 0), V3(0.08, 0.08, 1.74), 0.01);
    b.box(V3(0.69, 1.14, 0), V3(0.06, 0.05, 1.74));   // windscreen sill
    // side walls: rocker, rear quarter
    for (int sz = -1; sz <= 1; sz += 2) {
        panelStyle(b, o, pi++, 0.15);
        b.box(V3(0.08, 0.64, sz * 0.865), V3(1.5, 0.08, 0.05), 0.01);
        panelStyle(b, o, pi++);
        b.box(V3(-0.7, 1.24, sz * 0.865), V3(0.27, 1.2, 0.04), 0.008);
        truckPatches(b, o, 6, -0.82, -0.58, 0.7, 1.8, sz * 0.865, sz, o.seed + 60 + sz);
    }
    // doors (the left is +z)
    const double ang[2] = {o.doorL, o.doorR};
    for (int k = 0; k < 2; k++) {
        const int sz = k == 0 ? 1 : -1;
        if (ang[k] < 0) continue;
        b.push();
        b.move(0.63, 0, sz * 0.865);
        b.yaw(sz * ang[k]);
        truckDoor(b, o, 200 + k * 7);
        b.pop();
    }
    // windscreen and back glass remains
    if (o.glass) {
        b.style(V3(0.3, 0.42, 0.46), GLASS);
        b.tri(V3(0.66, 1.19, 0.86), V3(0.66, 1.19, 0.4), V3(0.55, 1.62, 0.86));
        b.tri(V3(0.56, 1.8, -0.86), V3(0.56, 1.8, -0.45), V3(0.64, 1.45, -0.86));
        b.tri(V3(-0.82, 1.2, 0.8), V3(-0.82, 1.2, 0.3), V3(-0.82, 1.5, 0.8));
        b.tri(V3(-0.82, roofY - 0.1, -0.8), V3(-0.82, roofY - 0.1, -0.2), V3(-0.82, roofY - 0.45, -0.8));
    }
    // ---- interior
    b.style(V3(0.14, 0.12, 0.1), MATTE);
    b.box(V3(0.55, 1.1, 0), V3(0.25, 0.12, 1.66), 0.03);   // dash
    b.box(V3(0.62, 0.9, 0), V3(0.12, 0.4, 1.6), 0.02);
    // steering wheel and column
    b.style(V3(0.08, 0.08, 0.08), METAL);
    b.tube({V3(0.6, 0.95, 0.36), V3(0.42, 1.2, 0.36)}, 0.02, 5);
    b.push();
    b.move(0.38, 1.25, 0.36);
    b.roll(0);
    b.pitch(-20);
    b.roll(90);
    b.torus(V3(0, 0, 0), 0.19, 0.016, 16, 6);
    b.pop();
    if (o.seat) {
        b.style(V3(0.32, 0.18, 0.12) * 0.8, MATTE);
        b.box(V3(-0.42, 0.84, 0), V3(0.5, 0.2, 1.5), 0.05);
        b.box(V3(-0.68, 1.16, 0), V3(0.14, 0.5, 1.5), 0.05);
        b.style(V3(0.08, 0.06, 0.05), MATTE);
        b.box(V3(-0.42, 0.945, 0.3), V3(0.3, 0.012, 0.22));  // a split in the cover
    } else {
        b.style(V3(0.2, 0.17, 0.14), METAL);
        for (int k = 0; k < 6; k++) b.box(V3(-0.42, 0.78, -0.65 + k * 0.26), V3(0.5, 0.025, 0.02));
        b.box(V3(-0.42, 0.74, 0), V3(0.52, 0.03, 1.5));
    }
    // ---- bed
    panelStyle(b, o, pi++, 0.1);
    b.box(V3(-1.7, 0.63, 0), V3(1.7, 0.04, 1.72), 0.01);  // floor
    b.style(steelRust(0.8, o.seed, 160), METAL);
    for (int k = 0; k < 6; k++) b.box(V3(-2.45 + k * 0.32, 0.6, 0), V3(0.05, 0.05, 1.5));  // cross sills
    for (int sz = -1; sz <= 1; sz += 2) {
        panelStyle(b, o, pi++);
        b.push();
        b.move(0, 0, sz * 0.9);
        b.extrude(archPoly(-1.5, -2.52, -0.85, 1.19, 1.19, false), 0.035);
        b.pop();
        panelStyle(b, o, pi++, 0.1);
        b.box(V3(-1.7, 1.2, sz * 0.9), V3(1.68, 0.05, 0.075), 0.01);  // rail
        // wheel house inside
        panelStyle(b, o, pi++, 0.15);
        b.box(V3(-1.5, 0.82, sz * 0.62), V3(0.98, 0.4, 0.26), 0.05);
        b.style(steelRust(0.8, o.seed, 170 + sz), METAL);
        std::vector<V3> arc;
        for (int i = 0; i <= 12; i++) {
            const double a = 0.43 + (PI - 0.86) * i / 12;
            arc.push_back(V3(-1.5 + 0.47 * std::cos(a), 0.38 + 0.47 * std::sin(a), sz * 0.93));
        }
        b.tube(arc, 0.014, 5);
        truckPatches(b, o, 14, -2.45, -0.9, 0.62, 1.15, sz * 0.92, sz, o.seed + 80 + sz);
        // tail lamp
        b.style(V3(0.5, 0.09, 0.07), PAINT);
        b.box(V3(-2.53, 1.0, sz * 0.84), V3(0.05, 0.14, 0.1), 0.01);
    }
    panelStyle(b, o, pi++);
    b.box(V3(-0.9, 1.08, 0), V3(0.045, 0.85, 1.72), 0.01);  // headboard
    b.style(V3(0.3, 0.3, 0.28), METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(-0.93, 1.45, sz * 0.5), V3(0.02, 0.1, 0.08));  // ladder-rack stubs
    // tailgate
    if (o.tailgate) {
        b.push();
        b.move(-2.5, 0.65, 0);
        if (o.tailgate == 2) b.pitch(-18);
        panelStyle(b, o, pi++);
        b.box(V3(0.0, 0.27, 0), V3(0.045, 0.52, 1.72), 0.012);
        b.style(V3(0.3, 0.28, 0.26), METAL);
        b.box(V3(-0.03, 0.46, 0), V3(0.02, 0.07, 1.4), 0.006);
        truckPatches(b, o, 7, 0.0, 0.0, 0.05, 0.45, 0.0, 0.0, o.seed + 90);
        b.pop();
    }
    // rear bumper and a step
    b.style(V3(0.5, 0.5, 0.48), CHROME);
    b.box(V3(-2.62, 0.5, 0), V3(0.1, 0.14, 1.85), 0.04);
    b.style(rustCol(o.seed, 180) * 0.8, METAL);
    b.box(V3(-2.67, 0.505, 0.2), V3(0.01, 0.1, 0.4));
}

void truckGlassShards(B& b, uint32_t seed, const V3& c, double rad, int n) {
    for (int k = 0; k < n; k++) {
        const double a = 2 * PI * hash01(seed, k), r = rad * std::sqrt(hash01(seed + 1, k));
        const V3 p(c.x + r * std::cos(a), c.y, c.z + r * std::sin(a));
        const double s = 0.03 + 0.06 * hash01(seed + 2, k), ra = 2 * PI * hash01(seed + 3, k);
        V3 v0 = p, v1 = p + V3(s * std::cos(ra), 0.002, s * std::sin(ra)), v2 = p + V3(s * 0.6 * std::cos(ra + 2.0), 0.004, s * 0.6 * std::sin(ra + 2.0));
        V3 n3 = cross(v1 - v0, v2 - v0);
        b.style(V3(0.4, 0.55, 0.58), GLASS);
        if (n3.y < 0) std::swap(v1, v2);
        b.tri(v0, v1, v2);
    }
}

// loose bits: a hubcap, a chrome strip, a mirror, a licence plate
void truckBits(B& b, const V3& at) {
    b.style(V3(0.55, 0.55, 0.52), CHROME);
    b.lathe(at + V3(0, 0.01, 0), {V2(0.0, 0.0), V2(0.09, 0.0), V2(0.1, 0.02), V2(0.08, 0.035), V2(0.0, 0.04)}, 10);
    b.tube({at + V3(0.4, 0.02, 0.5), at + V3(0.65, 0.025, 0.55), at + V3(0.9, 0.02, 0.5)}, 0.011, 4);
    b.style(V3(0.1, 0.1, 0.1), METAL);
    b.tube({at + V3(-0.6, 0.03, 0.9), at + V3(-0.5, 0.06, 0.8), at + V3(-0.4, 0.03, 0.75)}, 0.008, 4);
    b.style(V3(0.5, 0.5, 0.48), CHROME);
    b.cyl(at + V3(-0.4, 0.02, 0.75), at + V3(-0.4, 0.035, 0.75), 0.05, 8);
    b.style(V3(0.7, 0.62, 0.3), PAINT);
    b.box(at + V3(0.2, 0.01, -0.5), V3(0.3, 0.012, 0.15));
}

V3 truckPaint(int i) {
    static const V3 c[] = {V3(0.28, 0.45, 0.43), V3(0.55, 0.14, 0.1), V3(0.22, 0.3, 0.5), V3(0.62, 0.55, 0.38), V3(0.3, 0.38, 0.22)};
    return c[((i % 5) + 5) % 5];
}

const std::vector<std::string> kTruckPaints = {"FADED TEAL", "BRICK RED", "DENIM BLUE", "CREAM", "OLIVE"};

// A: on its side, driver side down, the passenger door hanging open.
void wreckSide(B& b, const P& p) {
    TruckOpts o;
    o.paint = truckPaint(p.i("paint"));
    o.rust = p("rust");
    o.seed = 7 + (uint32_t)p.i("seed");
    o.wheels[0] = true, o.wheels[1] = true, o.wheels[2] = false, o.wheels[3] = true;  // the driver-side rear wheel is off
    o.hood = 1;
    o.doorL = 0, o.doorR = 75;
    o.tailgate = 2;
    o.glass = true;
    b.push();
    b.move(0, 0.93, -0.95);
    b.roll(90);
    buildTruck(b, o);
    b.pop();
    // the missing wheel, lying flat nearby (axis up)
    b.push();
    b.move(-1.2, 0.115, -2.0);
    b.roll(90);
    b.pitch(8);
    tireAndRim(b, o, V3(0, 0, 0), false, 5);
    b.pop();
    // scatter: glass, hubcap, trim
    truckGlassShards(b, o.seed, V3(0.3, 0.004, 1.4), 1.2, 22);
    truckGlassShards(b, o.seed + 9, V3(1.3, 0.004, -1.3), 0.7, 10);
    truckBits(b, V3(1.2, 0.0, -2.0));
    // dirt dug up at the nose
    b.style(V3(0.3, 0.25, 0.2), ROCK);
    for (int k = 0; k < 5; k++) b.blob(V3(2.3 + 0.35 * hash01(o.seed, k), 0.06, -0.8 + 0.4 * k), V3(0.4, 0.14, 0.3), 0.3, o.seed + k, 1, 0.6, 0.0);
}

// B: upside down on its crushed roof, wheels in the air.
void wreckRoof(B& b, const P& p) {
    TruckOpts o;
    o.paint = truckPaint(p.i("paint"));
    o.rust = p("rust");
    o.seed = 17 + (uint32_t)p.i("seed");
    o.hood = 0;
    o.doorL = 40, o.doorR = -1;
    o.tailgate = 0;
    o.crush = 0.28;
    o.wheels[1] = false;  // one tyre gone
    b.push();
    b.move(0, 1.62, 0);
    b.pitch(-4);
    b.roll(180);
    buildTruck(b, o);
    b.pop();
    // the missing wheel and the hood torn off
    b.push();
    b.move(2.4, 0.115, 2.3);
    b.roll(90);
    tireAndRim(b, o, V3(0, 0, 0), false, 5);
    b.pop();
    b.push();
    b.move(3.0, 0.25, -1.9);
    b.yaw(35);
    b.roll(12);
    panelStyle(b, o, 3, 0.2);
    b.box(V3(0, 0.0, 0), V3(1.5, 0.07, 1.06), 0.035);
    b.pop();
    truckGlassShards(b, o.seed, V3(0.0, 0.004, 1.4), 1.2, 20);
    truckBits(b, V3(-1.8, 0.0, -1.8));
}

// C: a stripped hulk on cinder blocks.
void wreckHulk(B& b, const P& p) {
    TruckOpts o;
    o.paint = truckPaint(p.i("paint"));
    o.rust = p("rust");
    o.seed = 27 + (uint32_t)p.i("seed");
    o.hood = 0, o.doorL = -1, o.doorR = -1, o.tailgate = 0, o.glass = false, o.engine = false, o.seat = false;
    for (bool& w : o.wheels) w = false;
    b.push();
    b.move(0, 0.3, 0);
    buildTruck(b, o);
    b.pop();
    // blocks under the frame
    for (double x : {1.55, -1.5, 2.05, -2.15})
        for (int sz = -1; sz <= 1; sz += 2)
            for (int c = 0; c < 4; c++) {
                b.style(V3(0.5, 0.49, 0.46) * (0.85 + 0.2 * hash01(o.seed, c + (int)(x * 3) + sz)), CONCRETE);
                const double jx = 0.03 * (hash01(o.seed + 1, c + (int)(x * 5) + sz) - 0.5);
                b.box(V3(x + jx, 0.1 + c * 0.2, sz * 0.45), V3(c % 2 ? 0.2 : 0.4, 0.19, c % 2 ? 0.4 : 0.2));
            }
    // hood leaning on the front wheel arch, a door on the ground, a stack of tyres, the engine on a pallet
    b.push();
    b.move(1.5, 0.66, 1.5);
    b.pitch(0);
    b.roll(-18);
    panelStyle(b, o, 3, 0.25);
    b.box(V3(0, 0.0, 0), V3(1.5, 0.07, 1.06), 0.035);
    b.pop();
    for (int k = 0; k < 3; k++) tyre(b, V3(-3.3, 0.1 + k * 0.2, -1.0), 0.28, 0.12, true, k * 40);
    tyre(b, V3(-3.3, 0.7, -1.0), 0.28, 0.12, true, 5);
    b.style(timber(3, o.seed, 1), WOOD);
    for (int k = 0; k < 3; k++) b.box(V3(3.7, 0.05, 1.6 - 0.0 + (k - 1) * 0.45), V3(1.0, 0.1, 0.12));
    for (int k = 0; k < 5; k++) b.box(V3(3.7 + (k - 2) * 0.2, 0.12, 1.6), V3(0.15, 0.025, 1.1));
    b.style(V3(0.2, 0.22, 0.24), METAL);
    b.box(V3(3.7, 0.4, 1.6), V3(0.75, 0.42, 0.6), 0.04);
    b.style(steelRust(0.7, o.seed, 5), METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(3.72, 0.66, 1.6 + sz * 0.2), V3(0.52, 0.07, 0.17), 0.015);
    truckGlassShards(b, o.seed, V3(0.0, 0.004, -1.5), 1.2, 6);
    truckBits(b, V3(1.5, 0.0, 2.2));
}

Reg wreckPickup({"wreck_pickup", "WRECKED PICKUP", "BUILDINGS", "An old pickup wrecked in the canyon, parts thrown about", 5.5,
                 {
                     {"ON ITS SIDE", "Driver side down, passenger door hanging open, one wheel off",
                      {choice("paint", "PAINT", 0, kTruckPaints), num("rust", "RUST", 0.65, 0, 1, 0.05), seedParam(1)}, wreckSide},
                     {"UPSIDE DOWN", "On its crushed roof, wheels in the air, hood torn off",
                      {choice("paint", "PAINT", 1, kTruckPaints), num("rust", "RUST", 0.6, 0, 1, 0.05), seedParam(1)}, wreckRoof},
                     {"STRIPPED HULK", "Wheels, hood, doors and engine gone: up on cinder blocks",
                      {choice("paint", "PAINT", 2, kTruckPaints), num("rust", "RUST", 0.75, 0, 1, 0.05), seedParam(1)}, wreckHulk},
                 }});

}  // namespace
