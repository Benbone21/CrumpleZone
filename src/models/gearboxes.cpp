// Gearboxes and transfer cases: manual (and transaxles), automatic, the side-by-side's belt CVT, and the 4x4 transfer
// cases. One family builder per model; each design's ends (the face it bolts on by, its output yokes) come from one
// small table that both the builder and drivetrain::boxEnds read.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain.h"
#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;
using namespace drivetrain;

namespace {

constexpr double PIN = 0.068;  // yoke(): the cross pins this far out from x0

// ---- Shared bits -------------------------------------------------------------------------------

using Poly = std::vector<V2>;

const V3 kSteel(0.33, 0.33, 0.35), kBolt(0.12, 0.12, 0.13), kRubber(0.05, 0.05, 0.055), kDark(0.07, 0.07, 0.075);

struct Fin {
    V3 c;
    Mat m;
};
Fin fin(const P& p) {
    Fin f;
    f.c = gbFinish(p.i("finish"), f.m);
    return f;
}
void st(B& b, const Fin& f, double k = 1) { b.style(f.c * k, f.m); }

// The convex hull of circles (z, y, r) in a section seen along X, grown by `grow`.
Poly hull(const std::vector<Circ>& cs, double grow = 0, int n = 12) {
    std::vector<V2> pts;
    for (const Circ& c : cs)
        for (int i = 0; i < n; i++) {
            const double a = 2 * PI * (i + 0.5) / n;
            pts.push_back(V2(c.z + (c.r + grow) * std::cos(a), c.y + (c.r + grow) * std::sin(a)));
        }
    std::sort(pts.begin(), pts.end(), [](const V2& a, const V2& b2) { return a.x < b2.x || (a.x == b2.x && a.y < b2.y); });
    auto cr = [](const V2& o, const V2& a, const V2& b2) { return (a.x - o.x) * (b2.y - o.y) - (a.y - o.y) * (b2.x - o.x); };
    std::vector<V2> h(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        while (k >= 2 && cr(h[k - 2], h[k - 1], pts[i]) <= 1e-12) k--;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; i--) {
        while (k >= t && cr(h[k - 2], h[k - 1], pts[i - 1]) <= 1e-12) k--;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}
// A rounded rectangle in a (z, y) section.
Poly rrect(double cz, double cy, double w, double h, double r, int segs = 3) {
    Poly q = roundedRect(w, h, r, segs);
    for (V2& v : q) v.x += cz, v.y += cy;
    return q;
}
// A section (z, y) extruded along X from x0 to x1.
void extrudeX(B& b, const Poly& zy, double x0, double x1) {
    Poly q;
    for (const V2& v : zy) q.push_back(V2(-v.x, v.y));
    b.push();
    b.move((x0 + x1) / 2, 0, 0);
    b.yaw(90);
    b.extrude(q, std::fabs(x1 - x0));
    b.pop();
}
// n points evenly spaced (by length) round a closed polygon.
std::vector<V2> alongPoly(const Poly& p, int n) {
    const size_t N = p.size();
    double L = 0;
    for (size_t i = 0; i < N; i++) L += std::hypot(p[(i + 1) % N].x - p[i].x, p[(i + 1) % N].y - p[i].y);
    std::vector<V2> out;
    size_t e = 0;
    double acc = 0;
    for (int k = 0; k < n; k++) {
        const double t = L * (k + 0.5) / n;
        for (;;) {
            const V2 &a = p[e % N], &c = p[(e + 1) % N];
            const double l = std::hypot(c.x - a.x, c.y - a.y);
            if (acc + l >= t || e > 4 * N) {
                const double u = l > 1e-9 ? (t - acc) / l : 0;
                out.push_back(V2(a.x + (c.x - a.x) * u, a.y + (c.y - a.y) * u));
                break;
            }
            acc += l, e++;
        }
    }
    return out;
}
Poly scaled(const Poly& p, double kx, double ky, double cx = 0, double cy = 0) {
    Poly q;
    for (const V2& v : p) q.push_back(V2(cx + (v.x - cx) * kx, cy + (v.y - cy) * ky));
    return q;
}
Poly shifted(const Poly& p, double dx, double dy) {
    Poly q;
    for (const V2& v : p) q.push_back(V2(v.x + dx, v.y + dy));
    return q;
}
// Bolt heads on the plane x round a (z, y) outline.
void boltsRoundX(B& b, const Poly& zy, double x, int n, double s = 0.016) {
    b.style(kBolt, METAL);
    for (const V2& v : alongPoly(zy, n)) nut(b, V3(x, v.y, v.x), 0, s, 0.007);
}
// A ring of bolt heads round the Z axis on the plane z.
void boltRingZ(B& b, double x, double y, double z, double R, int n, double s = 0.016) {
    b.style(kBolt, METAL);
    for (int i = 0; i < n; i++) {
        const double a = 2 * PI * (i + 0.25) / n;
        nut(b, V3(x + R * std::cos(a), y + R * std::sin(a), z), 2, s, 0.007);
    }
}

enum Dir { PX, NX, PZ, NZ, PY, NY };
// A frame at `at` whose +X points along `d` (caller pops).
void facing(B& b, const V3& at, int d) {
    b.push();
    b.move(at);
    switch (d) {
        case NX: b.yaw(180); break;
        case PZ: b.yaw(-90); break;
        case NZ: b.yaw(90); break;
        case PY: b.pitch(90); break;
        case NY: b.pitch(-90); break;
        default: break;
    }
}
// A CV-joint drive flange facing `d`: seal boss, flange disc and its bolt heads.
void cvFlange(B& b, const V3& at, int d, double r, int bolts = 6) {
    facing(b, at, d);
    b.style(V3(0.36, 0.36, 0.38), METAL);
    xlathe(b, V3(), {V2(r * 0.72, 0), V2(r * 0.72, 0.02), V2(r, 0.022), V2(r, 0.036), V2(r * 0.42, 0.038), V2(r * 0.42, 0.048), V2(0, 0.048)}, 10);
    b.style(kBolt, METAL);
    boltRingX(b, V3(0.038, 0, 0), r * 0.74, bolts, 0.013, 0.006, 0.3);
    b.pop();
}
// Letters on a face: side +1 the +Z face, -1 the -Z face, 2 the top (read from the left side), 0 the +X face, 3 -X face.
void stencil(B& b, const std::string& s, const V3& at, int side, double px, const V3& col, Mat m = MATTE) {
    b.style(col, m);
    b.push();
    b.move(at);
    if (side == 1) b.yaw(-90);
    else if (side == -1) b.yaw(90);
    else if (side == 2) b.pitch(90), b.roll(90);
    else if (side == 3) b.yaw(180);
    b.letters(s, V3(0, 0, 0), px, 0.0015);
    b.pop();
}
// A rubber mount pad under a tail housing whose underside is at y.
void tailMount(B& b, double x, double y, double w = 0.14) {
    b.style(kRubber, MATTE);
    b.box(V3(x, y - 0.016, 0), V3(0.07, 0.036, 0.09), 0.006);
    b.style(kSteel * 0.8, METAL);
    b.box(V3(x, y - 0.038, 0), V3(0.1, 0.008, w), 0.002);
    b.style(kBolt, METAL);
    for (int s = -1; s <= 1; s += 2) nut(b, V3(x, y - 0.044, s * (w / 2 - 0.018)), 1, 0.018, 0.006);
}
// A drain / fill plug on a face along `axis`.
void plug(B& b, const V3& c, int axis) {
    b.style(kBolt, METAL);
    nut(b, c, axis, 0.022, 0.012);
}
// A small electrical connector box with a lead running along `path` (the first point at the box).
void connector(B& b, const V3& c, const V3& size, const std::vector<V3>& path, const V3& col = V3(0.05, 0.05, 0.055)) {
    b.style(col, MATTE);
    b.box(c, size);
    if (path.size() >= 2) {
        b.style(kRubber, MATTE);
        b.tube(path, 0.006, 5);
    }
}

// The shop's turntable: two timber rails, a block under the case. Returns the lift for the part (0 when bare).
double shopLift(B& b, const P& p, double lowY, double x0, double x1, double caseBot, double bx0, double bx1, double zs = 0.1) {
    if (bare(p)) return 0;
    const double h = 0.07, Y0 = h - lowY + 0.002;
    gbRails(b, x0, x1, h, zs);
    const double top = Y0 + caseBot;
    if (top - h > 0.015) {
        b.style(V3(0.47, 0.36, 0.22), WOOD);
        b.box(V3((bx0 + bx1) / 2, (h + top) / 2, 0), V3(bx1 - bx0, top - h, 2 * zs + 0.06), 0.006);
    }
    return Y0;
}

// ---- gearbox_manual ----------------------------------------------------------------------------

// The ends of each design (bare): the bell face, the tail yoke's x0 (0: a transaxle, no prop shaft).
struct MtDim {
    double inX, yokeX;
};
MtDim mtDim(int d) {
    switch (d) {
        case MT_TOPLOADER: return {0.30, -0.27};
        case MT_ALLOY: return {0.36, -0.36};
        case MT_FWD_TRANSAXLE: return {0.16, 0};
        case MT_TYPE1: return {0.20, 0};
        default: return {0.15, 0};
    }
}

Param shifterParam() { return choice("shifter", "SHIFTER", 1, {"REMOVED", "FITTED"}); }
Param seqParam() { return choice("sequential", "SHIFT", 0, {"H-PATTERN", "SEQUENTIAL"}); }

// SM465-style: heavy iron, separate bell, tall square case, PTO covers, a tall bent stick, a short tail.
void mtToploader(B& b, const P& p) {
    const Fin f = fin(p);
    const MtDim d = mtDim(MT_TOPLOADER);
    const bool iron = p.i("finish") == 1;
    const double Y0 = shopLift(b, p, -0.205, -0.28, 0.3, -0.17, -0.2, 0.08);
    b.push();
    b.move(0, Y0, 0);
    // The separate bell, a shade apart from the case, with the clutch fork out of its side and a starter pocket.
    st(b, f, 0.9);
    bellHousing(b, 0.1, d.inX - 0.1, 0.205, 0.125, 8);
    b.box(V3(0.235, -0.08, 0.172), V3(0.12, 0.11, 0.06), 0.02);
    b.box(V3(0.18, 0.02, -0.16), V3(0.06, 0.07, 0.05), 0.012);
    b.style(kSteel * 0.85, METAL);
    b.beam(V3(0.18, 0.04, -0.185), V3(0.165, -0.13, -0.2), 0.024, 0.012, V3(0, 0, 1));
    b.style(kRubber, MATTE);
    b.box(V3(0.18, 0.02, -0.188), V3(0.05, 0.05, 0.008));
    if (!bare(p)) {  // the input shaft and release bearing, seen through the bell
        b.style(kSteel, METAL);
        xcyl(b, 0.12, 0.25, 0, 0, 0.034, 10);
        xcyl(b, 0.25, 0.33, 0, 0, 0.015, 6);
    }
    // Case: flange to the bell, tall square body with corner ribs.
    st(b, f);
    b.box(V3(0.096, -0.02, 0), V3(0.018, 0.31, 0.26), 0.01);
    b.style(kBolt, METAL);
    for (int i = 0; i < 6; i++) nut(b, V3(0.107, i < 3 ? 0.12 : -0.16, (i % 3 - 1) * 0.105), 0, 0.02, 0.008);
    st(b, f);
    b.box(V3(-0.05, -0.025, 0), V3(0.29, 0.29, 0.23), 0.025);
    for (int s = -1; s <= 1; s += 2)
        for (double x : {0.075, -0.175}) b.box(V3(x, -0.03, s * 0.118), V3(0.022, 0.24, 0.012));
    b.box(V3(-0.05, -0.172, 0), V3(0.24, 0.012, 0.16), 0.004);  // bottom rib pad
    // PTO covers on both sides.
    for (int s = -1; s <= 1; s += 2) {
        b.style(f.c * 0.82, f.m);
        b.box(V3(-0.05, -0.09, s * 0.121), V3(0.17, 0.095, 0.012), 0.004);
        b.style(kBolt, METAL);
        for (int i = 0; i < 4; i++) nut(b, V3(-0.05 + (i & 1 ? 0.07 : -0.07), -0.09 + (i & 2 ? 0.034 : -0.034), s * 0.128), 2, 0.016, 0.006);
    }
    stencil(b, "4 SPEED", V3(-0.05, 0.045, 0.1155), 1, 0.0042, f.c * 0.7, f.m);
    // Top cover and the shift tower.
    st(b, f, 1.06);
    b.box(V3(-0.05, 0.13, 0), V3(0.25, 0.024, 0.21), 0.008);
    b.style(kBolt, METAL);
    for (int i = 0; i < 6; i++) nut(b, V3(-0.05 + (i % 3 - 1) * 0.1, 0.143, (i / 3 ? 1 : -1) * 0.088), 1, 0.016, 0.007);
    st(b, f);
    b.cyl(V3(-0.04, 0.14, 0), V3(-0.04, 0.19, 0), 0.05, 12);
    if (p.i("shifter")) {
        b.style(kRubber, MATTE);
        b.lathe(V3(-0.04, 0.19, 0), {V2(0.048, 0), V2(0.04, 0.02), V2(0.03, 0.04), V2(0.02, 0.06), V2(0.014, 0.075)}, 10);
        b.style(V3(0.1, 0.1, 0.11), PAINT);
        b.tube({V3(-0.04, 0.2, 0), V3(-0.04, 0.33, 0), V3(-0.08, 0.45, 0.01), V3(-0.15, 0.6, 0.02)}, 0.012, 6);
        b.style(V3(0.08, 0.075, 0.07), MATTE);
        b.sphere(V3(-0.155, 0.625, 0.02), V3(0.036, 0.034, 0.036), 1);
    } else {
        b.style(kSteel, METAL);
        b.cyl(V3(-0.04, 0.19, 0), V3(-0.04, 0.198, 0), 0.054, 12);
    }
    // Short tail (it couples a transfer case), the output and its yoke.
    st(b, f);
    xlathe(b, V3(d.yokeX + 0.005, 0, 0), {V2(0, 0), V2(0.045, 0), V2(0.05, 0.015), V2(0.06, 0.025), V2(0.065, 0.05), V2(0.092, 0.055), V2(0.092, 0.07), V2(0, 0.07)}, 12);
    yoke(b, d.yokeX, 0, 0, -1, 0.033);
    // Mount ears, plugs, reverse-light switch.
    st(b, f);
    for (int s = -1; s <= 1; s += 2) b.box(V3(-0.17, -0.15, s * 0.13), V3(0.05, 0.03, 0.04), 0.008);
    plug(b, V3(-0.05, -0.18, 0.04), 1);
    plug(b, V3(-0.12, -0.02, 0.117), 2);
    b.style(kSteel, METAL);
    zcyl(b, 0.02, 0.07, -0.115, -0.14, 0.012, 6);
    b.style(kRubber, MATTE);
    zcyl(b, 0.02, 0.07, -0.14, -0.16, 0.009, 6);
    if (iron) {
        rustSpots(b, 11, V3(-0.05, -0.02, 0), V3(0.24, 0.22, 0), 3, 2, 0.1165);
        rustSpots(b, 23, V3(-0.05, -0.02, 0), V3(0.24, 0.22, 0), 2, 2, -0.1165);
    }
    b.pop();
}

// A modern alloy RWD box: integrated ribbed bell and case, long tail with a remote shifter turret. Sequential: finned
// dog-box case and a straight ratchet lever.
void mtAlloy(B& b, const P& p) {
    const Fin f = fin(p);
    const MtDim d = mtDim(MT_ALLOY);
    const bool seq = p.i("sequential") == 1;
    const double Y0 = shopLift(b, p, -0.205, -0.36, 0.36, -0.175, -0.17, 0.1);
    b.push();
    b.move(0, Y0, 0);
    st(b, f);
    bellHousing(b, 0.13, d.inX - 0.13, 0.205, 0.15, 8);
    for (int i = 0; i < 6; i++) {  // bell ribs
        const double a = PI * (0.12 + 0.3 * i) + 0.3;
        b.beam(V3(0.15, 0.15 * std::sin(a), 0.15 * std::cos(a)), V3(0.33, 0.196 * std::sin(a), 0.196 * std::cos(a)), 0.014, 0.014, V3(1, 0, 0));
    }
    b.box(V3(0.27, -0.07, -0.18), V3(0.12, 0.12, 0.06), 0.02);  // starter pocket
    if (!bare(p)) {
        b.style(kSteel, METAL);
        xcyl(b, 0.14, 0.3, 0, 0, 0.038, 10);
        xcyl(b, 0.3, 0.39, 0, 0, 0.015, 6);
    }
    // Case: a pear section (main shaft over the countershaft), ring ribs or cooling fins.
    const std::vector<Circ> sec = {{0, 0, 0.112}, {0, -0.075, 0.098}};
    st(b, f);
    extrudeX(b, hull(sec, 0, 8), -0.17, 0.135);
    st(b, f, 0.94);
    if (seq)
        for (int i = 0; i < 8; i++) {
            const double x = 0.105 - i * 0.036;
            extrudeX(b, hull(sec, 0.016, 8), x - 0.004, x + 0.004);
        }
    else
        for (int i = 0; i < 5; i++) {
            const double x = 0.1 - i * 0.06;
            extrudeX(b, hull(sec, 0.008, 8), x - 0.006, x + 0.006);
        }
    for (int s = -1; s <= 1; s += 2) b.box(V3(-0.02, -0.06, s * 0.104), V3(0.29, 0.014, 0.012));
    // Shift-rail cover on top, plugs, reverse switch.
    st(b, f, 1.05);
    b.box(V3(-0.03, 0.112, 0), V3(0.2, 0.02, 0.1), 0.006);
    b.style(kBolt, METAL);
    for (int i = 0; i < 6; i++) nut(b, V3(-0.03 + (i % 3 - 1) * 0.08, 0.123, (i / 3 ? 1 : -1) * 0.038), 1, 0.014, 0.006);
    plug(b, V3(-0.05, -0.175, 0), 1);
    plug(b, V3(0.03, -0.06, 0.112), 2);
    connector(b, V3(-0.12, 0.06, -0.112), V3(0.03, 0.03, 0.03), {V3(-0.12, 0.07, -0.125), V3(-0.08, 0.1, -0.13), V3(0.05, 0.12, -0.13)});
    // Clutch slave on the bell's side with its line.
    b.style(kSteel * 0.9, METAL);
    xcyl(b, 0.2, 0.29, 0.06, -0.185, 0.022, 8);
    b.style(V3(0.2, 0.2, 0.21), METAL);
    b.tube({V3(0.29, 0.06, -0.185), V3(0.32, 0.09, -0.18), V3(0.35, 0.16, -0.14)}, 0.005, 5);
    // Tail housing, output seal, the yoke, the mount.
    st(b, f);
    xlathe(b, V3(d.yokeX + 0.005, 0, 0), {V2(0, 0), V2(0.045, 0), V2(0.05, 0.03), V2(0.058, 0.06), V2(0.066, 0.11), V2(0.086, 0.16), V2(0.104, 0.18), V2(0.104, 0.195), V2(0, 0.195)}, 12);
    yoke(b, d.yokeX, 0, 0, -1, 0.031);
    tailMount(b, -0.215, -0.07);
    if (seq)
        for (int i = 0; i < 4; i++) {  // fins on the tail too
            const double x = -0.31 + i * 0.03;
            b.style(f.c * 0.94, f.m);
            xcyl(b, x - 0.004, x + 0.004, 0, 0, 0.082 + i * 0.006, 10, false);
        }
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.29, 0.02, 0.04, 0.07, 0.012, 6);  // speed sensor
    // Remote shifter turret on the tail, its rail housing running forward to the case.
    st(b, f);
    b.box(V3(-0.25, 0.07, 0), V3(0.09, 0.03, 0.075), 0.01);
    b.tube({V3(-0.24, 0.08, 0), V3(-0.17, 0.1, 0), V3(-0.1, 0.112, 0)}, 0.019, 8);
    if (seq) {
        st(b, f, 0.85);
        b.box(V3(-0.25, 0.118, 0), V3(0.12, 0.065, 0.085), 0.012);
        b.style(kBolt, METAL);
        for (int i = 0; i < 4; i++) nut(b, V3(-0.25 + (i & 1 ? 0.04 : -0.04), 0.118 + (i & 2 ? 0.018 : -0.018), 0.044), 2, 0.012, 0.005);
        stencil(b, "SEQ", V3(-0.25, 0.118, 0.0435), 1, 0.005, V3(0.85, 0.82, 0.75));
        if (p.i("shifter")) {
            b.style(V3(0.62, 0.62, 0.65), CHROME);
            b.cyl(V3(-0.25, 0.15, 0), V3(-0.25, 0.46, 0), 0.011, 8);
            b.style(V3(0.05, 0.05, 0.055), MATTE);
            b.lathe(V3(-0.25, 0.43, 0), {V2(0.016, 0), V2(0.02, 0.01), V2(0.02, 0.07), V2(0.014, 0.08), V2(0, 0.082)}, 8);
        }
    } else {
        st(b, f);
        b.cyl(V3(-0.25, 0.085, 0), V3(-0.25, 0.14, 0), 0.036, 10);
        if (p.i("shifter")) {
            b.style(kRubber, MATTE);
            b.lathe(V3(-0.25, 0.14, 0), {V2(0.036, 0), V2(0.03, 0.015), V2(0.02, 0.03), V2(0.012, 0.045)}, 10);
            b.style(V3(0.12, 0.12, 0.13), PAINT);
            b.tube({V3(-0.25, 0.15, 0), V3(-0.25, 0.27, 0), V3(-0.23, 0.36, 0)}, 0.009, 6);
            b.style(V3(0.05, 0.05, 0.055), MATTE);
            b.lathe(V3(-0.228, 0.355, 0), {V2(0, 0), V2(0.02, 0.008), V2(0.027, 0.032), V2(0.02, 0.055), V2(0, 0.06)}, 10);
        } else {
            b.style(kSteel, METAL);
            b.cyl(V3(-0.25, 0.14, 0), V3(-0.25, 0.146, 0), 0.04, 10);
        }
    }
    b.pop();
}

// Honda-style transverse FWD transaxle: bell at +X, squat ribbed case to -X, the diff low and to +Z, CV flanges along
// X, shift cables on top.
void mtFwd(B& b, const P& p) {
    const Fin f = fin(p);
    const MtDim d = mtDim(MT_FWD_TRANSAXLE);
    const bool seq = p.i("sequential") == 1;
    const double DZ = 0.2, DY = -0.1;  // the diff's axis
    const std::vector<Circ> sec = {{0, 0, 0.095}, {0.085, 0.03, 0.08}, {DZ, DY, 0.11}};
    const double Y0 = shopLift(b, p, DY - 0.122, -0.26, 0.16, DY - 0.11, -0.2, 0.02, 0.12);
    b.push();
    b.move(0, Y0, 0);
    // Bell flange (round the main shaft), clutch housing over the diff, the gear case, the end cover.
    st(b, f);
    const Poly flange = hull({{0, 0, 0.18}, {0.08, 0.03, 0.13}}, 0, 10);
    extrudeX(b, flange, 0.14, d.inX);
    boltsRoundX(b, hull({{0, 0, 0.165}, {0.08, 0.03, 0.115}}, 0, 8), 0.135, 9);
    st(b, f, 0.96);
    extrudeX(b, hull({{0, 0, 0.15}, {DZ, DY, 0.122}}, 0, 10), 0.03, 0.14);
    st(b, f);
    extrudeX(b, hull(sec, 0, 10), -0.2, 0.03);
    st(b, f, 0.92);
    for (int i = 0; i < (seq ? 6 : 3); i++) {
        const double x = seq ? 0.0 - i * 0.034 : -0.02 - i * 0.065;
        extrudeX(b, hull(sec, seq ? 0.014 : 0.007, 10), x - 0.005, x + 0.005);
    }
    extrudeX(b, hull({{0, 0, 0.152}, {DZ, DY, 0.124}}, 0.006, 10), 0.024, 0.036);  // the case seam
    st(b, f, 1.04);
    const std::vector<Circ> endc = {{0, 0, 0.085}, {0.085, 0.03, 0.07}};
    extrudeX(b, hull(endc, 0, 10), -0.245, -0.2);
    boltsRoundX(b, hull(endc, 0.012, 8), -0.205, 8, 0.014);
    // Diff outputs: a seal boss and CV flange each way along X.
    st(b, f);
    xlathe(b, V3(-0.26, DY, DZ), {V2(0, 0), V2(0.05, 0), V2(0.06, 0.02), V2(0.07, 0.06), V2(0, 0.06)}, 10);
    cvFlange(b, V3(-0.26, DY, DZ), NX, 0.05);
    st(b, f);
    xlathe(b, V3(0.14, DY, DZ), {V2(0.06, 0), V2(0.045, 0.008), V2(0, 0.008)}, 10);
    cvFlange(b, V3(0.112, DY, DZ), PX, 0.046);  // (its face just short of the bell face)
    plug(b, V3(-0.05, DY - 0.112, DZ), 1);
    // Mount bracket on the end cover, a sensor on the diff lobe.
    st(b, f);
    b.box(V3(-0.22, 0.1, 0.03), V3(0.06, 0.05, 0.08), 0.01);
    b.style(kBolt, METAL);
    b.cyl(V3(-0.22, 0.125, 0.03), V3(-0.22, 0.16, 0.03), 0.008, 6);
    connector(b, V3(-0.08, DY + 0.105, DZ + 0.03), V3(0.03, 0.03, 0.03), {V3(-0.08, DY + 0.12, DZ + 0.03), V3(-0.04, DY + 0.16, DZ + 0.06), V3(0.05, DY + 0.18, DZ + 0.08)});
    // Clutch release arm and slave on the top front.
    b.style(kSteel, METAL);
    b.cyl(V3(0.09, 0.14, 0.05), V3(0.09, 0.175, 0.05), 0.012, 6);
    b.beam(V3(0.09, 0.172, 0.05), V3(0.09, 0.176, 0.13), 0.02, 0.008, V3(0, 1, 0));
    b.style(kSteel * 0.85, METAL);
    zcyl(b, 0.09, 0.172, 0.14, 0.21, 0.018, 8);
    // Shift linkage on top: the tower, two levers and two cables (sequential: one rod and a ratchet box).
    const double tx = -0.09, ty = 0.11, tz = 0.06;
    st(b, f, 0.88);
    b.box(V3(tx, ty, tz), V3(0.09, 0.05, 0.08), 0.01);
    const bool sh = p.i("shifter") == 1;
    if (seq) {
        b.style(kDark, PAINT);
        b.box(V3(tx, ty + 0.05, tz), V3(0.07, 0.05, 0.06), 0.008);
        b.style(kSteel, METAL);
        b.beam(V3(tx, ty + 0.06, tz), V3(tx, ty + 0.1, tz + 0.06), 0.02, 0.01, V3(1, 0, 0));
        b.style(V3(0.6, 0.6, 0.62), CHROME);
        b.tube({V3(tx, ty + 0.1, tz + 0.06), V3(tx, ty + 0.11, tz + (sh ? 0.3 : 0.12))}, 0.007, 6);
    } else {
        b.style(kSteel, METAL);
        b.beam(V3(tx + 0.02, ty + 0.025, tz), V3(tx + 0.02, ty + 0.08, tz + 0.04), 0.016, 0.008, V3(1, 0, 0));
        b.beam(V3(tx - 0.025, ty + 0.025, tz), V3(tx - 0.05, ty + 0.06, tz + 0.05), 0.016, 0.008, V3(0, 1, 0));
        b.style(kSteel * 0.8, METAL);
        b.box(V3(tx - 0.01, ty + 0.03, tz + 0.07), V3(0.1, 0.012, 0.03), 0.003);  // cable bracket
        b.style(kRubber, MATTE);
        const double run = sh ? 0.32 : 0.14;
        b.tube({V3(tx + 0.02, ty + 0.08, tz + 0.04), V3(tx + 0.03, ty + 0.1, tz + 0.12), V3(tx + 0.06, ty + 0.12, tz + run)}, 0.008, 6);
        b.tube({V3(tx - 0.05, ty + 0.06, tz + 0.05), V3(tx - 0.05, ty + 0.08, tz + 0.12), V3(tx - 0.02, ty + 0.1, tz + run)}, 0.008, 6);
    }
    b.pop();
}

// VW Type 1: magnesium tunnel case, bell at +X, the diff with side plates and CV flanges to +-Z, gear carrier and the
// nose cone with the shift rod coupler at -X.
void mtType1(B& b, const P& p) {
    Fin f = fin(p);
    if (p.i("finish") == 0) f.c = V3(0.56, 0.56, 0.53);  // (magnesium: duller than aluminium)
    const MtDim d = mtDim(MT_TYPE1);
    const double DX = -0.02, DY = -0.025;
    const double Y0 = shopLift(b, p, -0.175, -0.36, 0.2, -0.155, -0.12, 0.08, 0.11);
    b.push();
    b.move(0, Y0, 0);
    st(b, f);
    bellHousing(b, 0.07, d.inX - 0.07, 0.175, 0.11, 8);
    xcyl(b, 0.11, 0.19, 0.07, -0.148, 0.04, 10);  // starter boss
    for (int s = -1; s <= 1; s += 2) b.box(V3(0.05, -0.06, s * 0.15), V3(0.06, 0.035, 0.06), 0.01);  // mount ears
    // The tunnel: one piece from the bell back to the nose.
    xlathe(b, V3(-0.3, 0, 0), {V2(0.084, 0), V2(0.09, 0.012), V2(0.096, 0.18), V2(0.104, 0.3), V2(0.112, 0.37)}, 12);
    st(b, f, 0.95);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(-0.29, -0.08, s * 0.05), V3(-0.12, -0.09, s * 0.06), 0.014, 0.02);
    // The diff barrel across, side plates, seal bosses, CV flanges.
    st(b, f);
    zlathe(b, V3(DX, DY, -0.152), {V2(0.1, 0), V2(0.122, 0.02), V2(0.13, 0.152), V2(0.122, 0.284), V2(0.1, 0.304)}, 14);
    for (int s = -1; s <= 1; s += 2) {
        st(b, f, 0.9);
        zcyl(b, DX, DY, s * 0.15, s * 0.163, 0.112, 14);
        boltRingZ(b, DX, DY, s * 0.165, 0.095, 8, 0.014);
        st(b, f);
        zcyl(b, DX, DY, s * 0.163, s * 0.19, 0.044, 10);
        cvFlange(b, V3(DX, DY, s * 0.19), s > 0 ? PZ : NZ, 0.052);
    }
    plug(b, V3(-0.2, -0.098, 0), 1);
    plug(b, V3(-0.12, 0.0, 0.1), 2);
    // Nose cone, its joint, the shift rod and coupler, the front mount.
    st(b, f, 0.97);
    xcyl(b, -0.306, -0.294, 0, 0, 0.096, 12);
    b.style(kBolt, METAL);
    boltRingX(b, V3(-0.307, 0, 0), 0.088, 6, 0.012, 0.005, 0.3);
    st(b, f);
    xlathe(b, V3(-0.4, 0, 0), {V2(0, 0), V2(0.028, 0), V2(0.034, 0.01), V2(0.05, 0.022), V2(0.074, 0.06), V2(0.086, 0.094), V2(0.086, 0.1)}, 12);
    if (p.i("shifter")) {
        b.style(kSteel, METAL);
        xcyl(b, -0.48, -0.395, 0.0, 0, 0.008, 6);
        b.style(V3(0.2, 0.2, 0.21), METAL);
        b.box(V3(-0.45, 0, 0), V3(0.045, 0.026, 0.026), 0.004);
        b.style(kBolt, METAL);
        nut(b, V3(-0.44, 0.015, 0), 1, 0.01, 0.006);
        nut(b, V3(-0.46, 0.015, 0), 1, 0.01, 0.006);
    }
    b.style(kSteel * 0.8, METAL);
    b.box(V3(-0.37, -0.075, 0), V3(0.05, 0.012, 0.2), 0.003);
    b.style(kRubber, MATTE);
    b.box(V3(-0.37, -0.095, 0), V3(0.05, 0.03, 0.1), 0.006);
    b.style(kBolt, METAL);
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(-0.37, -0.068, s * 0.085), V3(-0.37, -0.05, s * 0.085), 0.007, 6);
    // Clutch arm on the bell, its cable tube.
    b.style(kSteel, METAL);
    b.beam(V3(0.11, 0.0, -0.12), V3(0.1, -0.13, -0.15), 0.02, 0.01, V3(0, 0, 1));
    b.style(kRubber, MATTE);
    b.tube({V3(0.1, -0.13, -0.155), V3(0.0, -0.13, -0.16), V3(-0.12, -0.11, -0.13)}, 0.006, 5);
    b.pop();
}

V3 accentColor(int i, Mat& m) {
    switch (((i % 4) + 4) % 4) {
        case 1: m = PAINT; return V3(0.85, 0.66, 0.08);
        case 2: m = PAINT; return V3(0.62, 0.07, 0.06);
        case 3: m = PAINT; return V3(0.86, 0.35, 0.05);
        default: m = PAINT; return V3(0.075, 0.08, 0.085);
    }
}

// A hypercar's multi-clutch transaxle: compact, very wide, central diff with CV flanges to +-Z, an electro-hydraulic
// control unit on top with its lines and connectors, cooling lines.
void mtHyper(B& b, const P& p) {
    const Fin f = fin(p);
    const MtDim d = mtDim(MT_HYPERCAR);
    Mat am;
    const V3 acc = accentColor(p.i("accent"), am);
    const double DX = -0.17, DY = -0.04;
    const double Y0 = shopLift(b, p, -0.17, -0.34, 0.15, -0.16, -0.3, -0.02, 0.12);
    b.push();
    b.move(0, Y0, 0);
    // Bell flange (rounded square), the clutch housing.
    const Poly fl = rrect(0, -0.01, 0.36, 0.32, 0.07, 3);
    st(b, f);
    extrudeX(b, fl, 0.13, d.inX);
    boltsRoundX(b, rrect(0, -0.01, 0.33, 0.29, 0.055, 2), 0.125, 12, 0.014);
    st(b, f, 0.95);
    xlathe(b, V3(-0.02, 0, 0), {V2(0.11, 0), V2(0.135, 0.03), V2(0.14, 0.12), V2(0.15, 0.15)}, 14);
    // Main case: low and wide, ribbed.
    st(b, f);
    b.box(V3(-0.17, -0.02, 0), V3(0.31, 0.22, 0.33), 0.045);
    st(b, f, 0.92);
    for (int i = 0; i < 4; i++) b.box(V3(-0.07 - i * 0.07, 0.092, 0), V3(0.012, 0.012, 0.28));
    for (int s = -1; s <= 1; s += 2) b.box(V3(-0.17, 0.092, s * 0.09), V3(0.26, 0.012, 0.012));
    for (int i = -1; i <= 1; i++) b.box(V3(-0.326, -0.02, i * 0.08), V3(0.012, 0.17, 0.012));
    // The diff barrel right across, its CV flanges.
    st(b, f);
    zlathe(b, V3(DX, DY, -0.25), {V2(0.06, 0), V2(0.075, 0.02), V2(0.105, 0.07), V2(0.115, 0.25), V2(0.105, 0.43), V2(0.075, 0.48), V2(0.06, 0.5)}, 14);
    for (int s = -1; s <= 1; s += 2) {
        cvFlange(b, V3(DX, DY, s * 0.25), s > 0 ? PZ : NZ, 0.056);
    }
    // Electro-hydraulic control unit on top: the block, an accent cover, connectors and lines.
    b.style(V3(0.16, 0.16, 0.17), METAL);
    b.box(V3(-0.15, 0.12, 0), V3(0.22, 0.055, 0.22), 0.012);
    b.style(acc, am);
    b.box(V3(-0.15, 0.153, 0), V3(0.18, 0.012, 0.18), 0.004);
    stencil(b, "9 SPEED", V3(-0.15, 0.1595, 0), 2, 0.0042, am == PAINT && p.i("accent") == 0 ? V3(0.75, 0.75, 0.72) : V3(0.08, 0.08, 0.09));
    b.style(kBolt, METAL);
    for (int i = 0; i < 4; i++) nut(b, V3(-0.15 + (i & 1 ? 0.08 : -0.08), 0.161, (i & 2 ? 0.08 : -0.08)), 1, 0.012, 0.004);
    const V3 conn[3] = {V3(0.85, 0.55, 0.1), V3(0.12, 0.3, 0.6), V3(0.06, 0.06, 0.065)};
    for (int i = 0; i < 5; i++) {
        const double x = -0.23 + i * 0.04;
        connector(b, V3(x, 0.125, 0.118), V3(0.026, 0.024, 0.018), {V3(x, 0.13, 0.126), V3(x + 0.01, 0.15, 0.16), V3(x + 0.03, 0.12, 0.185), V3(x + 0.05, 0.06, 0.18)}, conn[i % 3]);
    }
    b.style(V3(0.42, 0.42, 0.44), METAL);
    for (int i = 0; i < 4; i++) {  // hydraulic lines down into the case
        const double x = -0.24 + i * 0.05, z = -0.11;
        b.tube({V3(x, 0.12, z), V3(x, 0.13, z - 0.04), V3(x - 0.01, 0.1, z - 0.07), V3(x - 0.02, 0.04, z - 0.068)}, 0.005, 5);
    }
    b.style(V3(0.15, 0.3, 0.65), METAL);
    for (int i = 0; i < 4; i++) b.cyl(V3(-0.24 + i * 0.05 - 0.02, 0.045, -0.178), V3(-0.24 + i * 0.05 - 0.02, 0.025, -0.178), 0.009, 6);
    // Cooling lines from the back of the case forward to the cooler.
    b.style(V3(0.25, 0.25, 0.27), METAL);
    for (int s = 0; s < 2; s++) {
        const double y = 0.0 + s * 0.04;
        b.tube({V3(-0.33, y, -0.12), V3(-0.36, y, -0.12), V3(-0.36, y + 0.02, -0.18), V3(-0.2, y + 0.03, -0.19), V3(0.0, y + 0.02, -0.19), V3(0.1, y + 0.08, -0.18)}, 0.009, 6);
        b.style(V3(0.6, 0.1, 0.08), METAL);
        b.cyl(V3(-0.322, y, -0.12), V3(-0.338, y, -0.12), 0.014, 6);
        b.style(V3(0.25, 0.25, 0.27), METAL);
    }
    if (p.i("shifter")) {  // the shift-by-wire lead up to the paddles
        b.style(kRubber, MATTE);
        b.tube({V3(-0.06, 0.13, 0.05), V3(-0.02, 0.17, 0.05), V3(0.06, 0.2, 0.04)}, 0.009, 6);
    }
    b.pop();
}

void manualBox(B& b, const P& p, int design) {
    switch (design) {
        case MT_TOPLOADER: mtToploader(b, p); break;
        case MT_ALLOY: mtAlloy(b, p); break;
        case MT_FWD_TRANSAXLE: mtFwd(b, p); break;
        case MT_TYPE1: mtType1(b, p); break;
        default: mtHyper(b, p); break;
    }
}

Reg gearboxManual({"gearbox_manual", "GEARBOX MANUAL", "PARTS", "Career upgrade: manual transmission", 0.8,
                   {
                       {"IRON TOP-LOADER", "Cast-iron 4-speed: tall square case, PTO covers, separate bell, a tall bent stick",
                        {gbFinishParam(1), shifterParam()}, [](B& b, const P& p) { manualBox(b, p, MT_TOPLOADER); }},
                       {"ALLOY 6-SPEED", "Ribbed aluminium one-piece box, long tail with a remote shifter turret",
                        {gbFinishParam(0), shifterParam(), seqParam()}, [](B& b, const P& p) { manualBox(b, p, MT_ALLOY); }},
                       {"FWD TRANSAXLE", "Transverse front-drive transaxle: squat case, diff low behind, cable shift",
                        {gbFinishParam(0), shifterParam(), seqParam()}, [](B& b, const P& p) { manualBox(b, p, MT_FWD_TRANSAXLE); }},
                       {"TYPE 1 TRANSAXLE", "Magnesium tunnel case: bell, diff with side plates and CV flanges, nose cone",
                        {gbFinishParam(0), shifterParam()}, [](B& b, const P& p) { manualBox(b, p, MT_TYPE1); }},
                       {"HYPERCAR TRANSAXLE", "Wide 9-speed multi-clutch transaxle with its electro-hydraulic control unit",
                        {gbFinishParam(2), shifterParam(), choice("accent", "ACCENT", 0, {"CARBON", "YELLOW", "RED", "ORANGE"})},
                        [](B& b, const P& p) { manualBox(b, p, MT_HYPERCAR); }},
                   }});

// ---- gearbox_auto ------------------------------------------------------------------------------

enum AtKind { K_TH350, K_TH400, K_4L80, K_A340, K_6SP, K_10SP };
struct AtDim {
    int kind;
    double inX, bellX0, bellR, neckR;  // bell from bellX0 to the face inX
    double caseX0, caseX1, caseR;      // main case from caseX0 (back) to caseX1 (into the bell)
    double odX0, odR;                  // overdrive section from odX0 to caseX0 (odX0 == caseX0: none)
    double tailR, yokeX;               // tail from the yoke back to odX0
    double panCx, panL, panW, panDepth;
};
AtDim atDim(int design, const P& p) {
    int k = K_TH350;
    if (design == AT_THM) k = p.i("th400") ? K_TH400 : K_TH350;
    else if (design == AT_OVERDRIVE) k = p.i("toyota") ? K_A340 : K_4L80;
    else k = p.i("speeds") ? K_10SP : K_6SP;
    switch (k) {
        case K_TH350: return {k, 0.3, 0.09, 0.205, 0.14, -0.17, 0.09, 0.13, -0.17, 0.13, 0.085, -0.33, -0.03, 0.28, 0.26, 0.045};
        case K_TH400: return {k, 0.32, 0.1, 0.212, 0.15, -0.23, 0.1, 0.145, -0.23, 0.145, 0.09, -0.4, -0.05, 0.34, 0.3, 0.05};
        case K_4L80: return {k, 0.34, 0.1, 0.215, 0.15, -0.15, 0.1, 0.145, -0.31, 0.128, 0.085, -0.44, -0.04, 0.36, 0.27, 0.08};
        case K_A340: return {k, 0.3, 0.12, 0.19, 0.135, -0.12, 0.12, 0.125, -0.22, 0.115, 0, -0.3, -0.02, 0.28, 0.23, 0.065};
        case K_6SP: return {k, 0.32, 0.1, 0.205, 0.14, -0.3, 0.1, 0.118, -0.3, 0.118, 0.075, -0.4, -0.1, 0.4, 0.21, 0.028};
        default: return {k, 0.32, 0.1, 0.205, 0.14, -0.36, 0.1, 0.118, -0.36, 0.118, 0.075, -0.47, -0.13, 0.46, 0.21, 0.028};
    }
}

Poly panShape(int kind, double L, double W) {
    const double l = L / 2, w = W / 2;
    switch (kind) {
        case K_TH350: return {V2(-l, -w), V2(l - 0.02, -w), V2(l, -w + 0.02), V2(l, w - 0.06), V2(l - 0.06, w), V2(-l, w)};
        case K_TH400:  // the irregular "Texas" pan
            return {V2(-l, -w), V2(0.05, -w), V2(0.1, -w + 0.05), V2(l, -w + 0.05), V2(l, w - 0.08), V2(l - 0.07, w), V2(-0.06, w), V2(-0.1, w - 0.04), V2(-l, w - 0.04)};
        case K_6SP:
        case K_10SP: return {V2(-l, -w + 0.02), V2(-l + 0.02, -w), V2(l - 0.02, -w), V2(l, -w + 0.02), V2(l, w - 0.02), V2(l - 0.02, w), V2(-l + 0.02, w), V2(-l, w - 0.02)};
        default: return {V2(-l, -w + 0.03), V2(-l + 0.03, -w), V2(l - 0.03, -w), V2(l, -w + 0.03), V2(l, w - 0.03), V2(l - 0.03, w), V2(-l + 0.03, w), V2(-l, w - 0.03)};
    }
}

// The pan: a bolted flange, the pressed body (cx: its middle along x), drain plug; flat stamped pans get embossed ribs.
void atPan(B& b, const Fin& f, const AtDim& d, double yTop) {
    const Poly q = shifted(panShape(d.kind, d.panL, d.panW), d.panCx, 0);
    const bool flat = d.kind == K_6SP || d.kind == K_10SP;
    const V3 pc = flat ? V3(0.2, 0.2, 0.21) : d.kind == K_A340 ? V3(0.4, 0.4, 0.42) : f.c * 0.92;
    const Mat pm = flat || d.kind == K_A340 ? METAL : f.m;
    b.style(pc * 0.9, pm);
    b.extrudeUp(q, 0.008, yTop - 0.008);
    const double bot = yTop - 0.008 - d.panDepth;
    b.style(pc, pm);
    b.extrudeUp(scaled(q, 0.94, 0.92, d.panCx, 0), d.panDepth, bot);
    if (flat)
        for (int i = 0; i < 5; i++) b.box(V3(d.panCx + (i - 2) * d.panL * 0.18, bot - 0.002, 0), V3(0.02, 0.006, d.panW * 0.7), 0.002);
    else if (d.kind == K_4L80 || d.kind == K_A340)
        b.extrudeUp(scaled(q, 0.6, 0.55, d.panCx - 0.03, 0), 0.015, bot - 0.012);  // the sump
    b.style(kBolt, METAL);
    for (const V2& v : alongPoly(scaled(q, 0.97, 0.95, d.panCx, 0), 10)) nut(b, V3(v.x, yTop - 0.012, v.y), 1, 0.015, 0.006);
    plug(b, V3(d.panCx - d.panL * 0.25, bot - (d.kind == K_4L80 || d.kind == K_A340 ? 0.018 : 0.006), d.panW * 0.2), 1);
}

void autoBox(B& b, const P& p, int design) {
    const Fin f = fin(p);
    const AtDim d = atDim(design, p);
    const bool thm = d.kind == K_TH350 || d.kind == K_TH400, multi = d.kind == K_6SP || d.kind == K_10SP;
    const double panTop = -d.caseR + 0.008;
    const double panBot = panTop - 0.008 - d.panDepth - (d.kind == K_4L80 || d.kind == K_A340 ? 0.015 : 0.004);
    const double lowY = std::min(-d.bellR, panBot);
    const double Y0 = shopLift(b, p, lowY, d.yokeX - 0.02, d.inX, panBot, d.panCx - d.panL * 0.3, d.panCx + d.panL * 0.3);
    b.push();
    b.move(0, Y0, 0);
    // The bell (THM: a separate-looking casting with its own bolted joint).
    st(b, f);
    bellHousing(b, d.bellX0, d.inX - d.bellX0, d.bellR, d.neckR, 8);
    b.box(V3(d.inX - 0.07, -0.07, -d.bellR + 0.02), V3(0.12, 0.12, 0.06), 0.02);  // starter pocket
    if (thm) {
        st(b, f, 0.9);
        xcyl(b, d.bellX0 - 0.012, d.bellX0 + 0.004, 0, 0, d.neckR + 0.02, 14);
        b.style(kBolt, METAL);
        boltRingX(b, V3(d.bellX0 + 0.006, 0, 0), d.neckR + 0.006, 8, 0.016, 0.007, 0.2);
    } else {
        st(b, f, 0.96);
        const int n = multi ? 8 : 4;
        for (int i = 0; i < n; i++) {
            const double a = PI * (0.1 + 1.8 * i / (n - 1)) + PI * 0.5;
            const double r0 = d.neckR + 0.002, r1 = d.bellR - 0.012;
            b.beam(V3(d.bellX0 + 0.01, r0 * std::sin(a), r0 * std::cos(a)), V3(d.inX - 0.04, r1 * std::sin(a), r1 * std::cos(a)), 0.014, 0.014, V3(1, 0, 0));
        }
    }
    if (!bare(p) && p.i("converter")) {
        b.style(V3(0.45, 0.45, 0.48), METAL);
        xlathe(b, V3(d.inX - 0.12, 0, 0), {V2(0.05, 0), V2(0.13, 0.02), V2(d.bellR - 0.05, 0.06), V2(d.bellR - 0.05, 0.08), V2(0.08, 0.11), V2(0, 0.112)}, 14);
        b.style(kBolt, METAL);
        boltRingX(b, V3(d.inX - 0.005, 0, 0), 0.13, 4, 0.02, 0.01, 0.3);
    }
    // Main case.
    st(b, f);
    const double L = d.caseX1 - d.caseX0, R = d.caseR;
    if (multi) {
        xlathe(b, V3(d.caseX0, 0, 0), {V2(0, 0), V2(R - 0.02, 0), V2(R - 0.012, 0.012), V2(R - 0.01, L * 0.5), V2(R, L - 0.03), V2(d.neckR, L)}, 12);
        st(b, f, 0.93);
        for (int i = 0; i < 5; i++) {  // long ribs over the top and sides
            const double a = PI * (0.0 + 0.25 * i);
            b.beam(V3(d.caseX0 + 0.02, (R - 0.01) * std::sin(a), (R - 0.01) * std::cos(a)), V3(d.caseX1 - 0.01, (R + 0.002) * std::sin(a), (R + 0.002) * std::cos(a)), 0.012, 0.016, V3(0, std::sin(a), std::cos(a)));
        }
        const int rings = d.kind == K_10SP ? 6 : 5;
        for (int i = 0; i < rings; i++) {
            const double x = d.caseX0 + 0.03 + i * (L - 0.06) / (rings - 1);
            xcyl(b, x - 0.006, x + 0.006, 0, 0, R + 0.002, 10, false);
        }
    } else {
        const double band = thm ? 0.01 : 0.006;
        xlathe(b, V3(d.caseX0, 0, 0), {V2(0, 0), V2(R - 0.012, 0), V2(R, 0.012), V2(R, L * 0.3), V2(R + band, L * 0.32), V2(R + band, L * 0.38), V2(R, L * 0.4), V2(R, L - 0.015),
                                        V2(R + 0.012, L - 0.012), V2(R + 0.012, L)}, 12);
    }
    // The pan rail, the pan.
    st(b, f, 0.97);
    b.box(V3(d.panCx, panTop + 0.03, 0), V3(d.panL * 0.96, 0.06, d.panW * 0.94), 0.015);
    atPan(b, f, d, panTop);
    // Overdrive section, tail / adapter, the yoke.
    st(b, f);
    if (d.odX0 < d.caseX0 - 0.001) {
        const double l = d.caseX0 - d.odX0, r = d.odR;
        xlathe(b, V3(d.odX0, 0, 0), {V2(0, 0), V2(r - 0.012, 0), V2(r, 0.012), V2(r, l * 0.45), V2(r + 0.008, l * 0.5), V2(r, l * 0.55), V2(r, l - 0.012), V2(R + 0.006, l - 0.008), V2(R + 0.006, l)}, 12);
        b.style(kBolt, METAL);
        boltRingX(b, V3(d.caseX0 - 0.004, 0, 0), R - 0.004, 8, 0.014, 0.006, 0.1);
        st(b, f);
    }
    if (d.kind == K_A340) {  // short adapter, flange plate to the transfer case
        xlathe(b, V3(-0.255, 0, 0), {V2(0, 0), V2(0.1, 0), V2(0.112, 0.035)}, 12);
        const Poly pl = rrect(0, -0.02, 0.24, 0.23, 0.05, 2);
        extrudeX(b, pl, -0.27, -0.255);
        boltsRoundX(b, rrect(0, -0.02, 0.21, 0.2, 0.04, 2), -0.272, 8, 0.016);
        b.style(kSteel, METAL);
        xcyl(b, d.yokeX + 0.004, -0.27, 0, 0, 0.045, 10);
        yoke(b, d.yokeX, 0, 0, -1, 0.03);
    } else {
        const double l = d.odX0 - d.yokeX - 0.005, r = d.tailR, rb = (d.odX0 < d.caseX0 ? d.odR : R) - 0.012;
        xlathe(b, V3(d.yokeX + 0.005, 0, 0), {V2(0, 0), V2(0.044, 0), V2(0.05, 0.02), V2(0.056, 0.04), V2(r * 0.85, l * 0.5), V2(r, l * 0.8), V2(rb, l * 0.97), V2(rb, l)}, 12);
        yoke(b, d.yokeX, 0, 0, -1, 0.031);
        tailMount(b, d.yokeX + l * 0.5, -r * 0.88);
    }
    // Per type: modulator, kickdown, connectors and looms, cooler lines, the manual shaft lever.
    const double cx = (d.caseX0 + d.caseX1) / 2;
    b.style(kSteel, METAL);
    b.beam(V3(cx, -0.02, -R - 0.006), V3(cx - 0.06, 0.04, -R - 0.012), 0.02, 0.008, V3(0, 0, 1));  // manual shaft lever
    b.cyl(V3(cx, -0.02, -R + 0.01), V3(cx, -0.02, -R - 0.012), 0.012, 6);
    if (thm) {
        b.style(V3(0.5, 0.5, 0.53), METAL);  // vacuum modulator
        zcyl(b, d.caseX0 + 0.05, -0.07, -R + 0.03, -R - 0.03, 0.016, 8);
        zcyl(b, d.caseX0 + 0.05, -0.07, -R - 0.03, -R - 0.075, 0.028, 10);
        b.style(kRubber, MATTE);
        b.tube({V3(d.caseX0 + 0.05, -0.07, -R - 0.075), V3(d.caseX0 + 0.05, -0.05, -R - 0.09), V3(d.caseX0 + 0.1, 0.08, -R - 0.06), V3(d.caseX0 + 0.2, 0.16, -R - 0.02)}, 0.004, 4);
        if (d.kind == K_TH350) {  // kickdown cable on the left
            b.style(kSteel, METAL);
            b.box(V3(d.caseX1 - 0.04, 0.07, R - 0.01), V3(0.04, 0.03, 0.03), 0.004);
            b.style(kRubber, MATTE);
            b.tube({V3(d.caseX1 - 0.04, 0.085, R + 0.005), V3(d.caseX1 - 0.02, 0.14, R + 0.02), V3(d.caseX1 + 0.1, 0.22, R + 0.03)}, 0.005, 5);
        } else {  // TH400's electric kickdown plug
            connector(b, V3(d.caseX1 - 0.06, 0.03, R + 0.005), V3(0.03, 0.03, 0.025), {V3(d.caseX1 - 0.06, 0.04, R + 0.02), V3(d.caseX1, 0.12, R + 0.04), V3(d.caseX1 + 0.12, 0.2, R + 0.04)});
        }
        b.style(V3(0.6, 0.6, 0.62), CHROME);  // steel cooler lines and dipstick tube
        for (int i = 0; i < 2; i++) {
            const double y = -0.03 + i * 0.045;
            b.tube({V3(cx, y, -R + 0.01), V3(cx + 0.03, y, -R - 0.03), V3(d.inX - 0.12, y + 0.02, -R - 0.06), V3(d.inX - 0.02, y + 0.03, -R - 0.065)}, 0.0065, 6);
            b.style(kSteel, METAL);
            nut(b, V3(cx + 0.01, y, -R - 0.004), 2, 0.022, 0.014);
            b.style(V3(0.6, 0.6, 0.62), CHROME);
        }
        b.tube({V3(d.caseX1 - 0.02, panTop + 0.02, -R + 0.02), V3(d.caseX1 + 0.02, 0.0, -R - 0.03), V3(d.inX - 0.09, 0.13, -0.17), V3(d.inX - 0.07, 0.17, -0.16)}, 0.006, 6);
        b.style(V3(0.75, 0.6, 0.12), MATTE);
        b.cyl(V3(d.inX - 0.07, 0.165, -0.16), V3(d.inX - 0.07, 0.18, -0.16), 0.012, 6);
    } else {
        // Connector and loom (4L80E / A340F: on the left; the multi-speeds' big round one).
        if (multi) {
            b.style(kRubber, MATTE);
            zcyl(b, cx + 0.03, 0.02, R - 0.01, R + 0.035, 0.034, 10);
            b.style(V3(0.85, 0.55, 0.1), MATTE);
            zcyl(b, cx + 0.03, 0.02, R + 0.012, R + 0.022, 0.038, 10, false);
            b.style(kRubber, MATTE);
            b.tube({V3(cx + 0.03, 0.02, R + 0.035), V3(cx + 0.06, 0.06, R + 0.06), V3(cx + 0.2, 0.14, R + 0.05), V3(d.inX, 0.18, R + 0.02)}, 0.014, 6);
        } else {
            b.style(kRubber, MATTE);
            zcyl(b, cx, -0.01, R - 0.01, R + 0.025, 0.026, 10);
            connector(b, V3(cx - 0.06, 0.05, R - 0.002), V3(0.025, 0.025, 0.02), {V3(cx - 0.06, 0.06, R + 0.01), V3(cx - 0.02, 0.06, R + 0.03)});
            b.style(kRubber, MATTE);
            b.tube({V3(cx, -0.01, R + 0.025), V3(cx + 0.04, 0.05, R + 0.04), V3(cx + 0.18, 0.12, R + 0.04), V3(d.inX - 0.02, 0.17, R + 0.02)}, 0.01, 6);
            if (d.kind == K_4L80) {  // output speed sensor on the tail
                connector(b, V3(d.odX0 - 0.04, 0.06, 0.04), V3(0.025, 0.03, 0.025), {V3(d.odX0 - 0.04, 0.075, 0.04), V3(d.odX0, 0.12, 0.08), V3(cx, 0.1, R + 0.03)});
            }
        }
        // Cooler lines on the right: steel tubes, or the multi-speeds' quick-fit plastic ones.
        for (int i = 0; i < 2; i++) {
            const double y = -0.02 + i * 0.045;
            if (multi) {
                b.style(V3(0.1, 0.1, 0.11), MATTE);
                b.tube({V3(cx + 0.06, y, -R + 0.01), V3(cx + 0.09, y, -R - 0.03), V3(d.inX - 0.1, y + 0.03, -R - 0.06), V3(d.inX - 0.02, y + 0.05, -R - 0.07)}, 0.008, 6);
                b.style(V3(0.25, 0.55, 0.3), MATTE);
                b.cyl(V3(cx + 0.06, y, -R + 0.002), V3(cx + 0.06, y, -R - 0.026), 0.014, 8);
            } else {
                b.style(V3(0.6, 0.6, 0.62), CHROME);
                b.tube({V3(cx, y, -R + 0.01), V3(cx + 0.03, y, -R - 0.03), V3(d.inX - 0.12, y + 0.02, -R - 0.06), V3(d.inX - 0.02, y + 0.03, -R - 0.065)}, 0.0065, 6);
                b.style(kSteel, METAL);
                nut(b, V3(cx + 0.008, y, -R - 0.004), 2, 0.022, 0.014);
            }
        }
        b.style(kRubber, MATTE);  // breather
        b.tube({V3(d.caseX0 + 0.04, R - 0.01, 0), V3(d.caseX0 + 0.04, R + 0.04, 0), V3(d.caseX0 + 0.08, R + 0.06, 0.02)}, 0.005, 4);
    }
    b.pop();
}

Param convParam() { return choice("converter", "CONVERTER", 1, {"REMOVED", "FITTED"}); }

Reg gearboxAuto({"gearbox_auto", "GEARBOX AUTO", "PARTS", "Career upgrade: automatic transmission", 0.8,
                 {
                     {"TURBO HYDRA-MATIC", "Round cast 3-speed, bolted-on bell, modulator, steel cooler lines",
                      {gbFinishParam(0), choice("th400", "MODEL", 0, {"TH350", "TH400"}), convParam()}, [](B& b, const P& p) { autoBox(b, p, AT_THM); }},
                     {"ELECTRONIC OVERDRIVE", "Long 4-speed with the overdrive section at the back, connector and loom, deep pan",
                      {gbFinishParam(0), choice("toyota", "MODEL", 0, {"4L80E", "A340F"}), convParam()}, [](B& b, const P& p) { autoBox(b, p, AT_OVERDRIVE); }},
                     {"MODERN MULTI-SPEED", "Long slim ribbed one-piece case, flat stamped pan, big round connector",
                      {gbFinishParam(0), choice("speeds", "SPEEDS", 1, {"6-SPEED", "10-SPEED"}), convParam()}, [](B& b, const P& p) { autoBox(b, p, AT_MULTI); }},
                 }});

// ---- gearbox_cvt -------------------------------------------------------------------------------

constexpr double CVT_IN = 0.22, CVT_YOKE = -0.24;

// A side-by-side's belt CVT: the compact gear case on the axis (shift selector, rear output yoke) and beside it to -Z
// the long ribbed plastic belt cover with its clutch bulges and air ducts.
void cvtBox(B& b, const P& p) {
    const Fin f = fin(p);
    const V3 cov = p.i("cover") == 0 ? V3(0.11, 0.11, 0.115) : p.i("cover") == 1 ? V3(0.2, 0.2, 0.21) : V3(0.36, 0.38, 0.3);
    const double Y0 = shopLift(b, p, -0.19, -0.25, 0.22, -0.155, -0.18, 0.08, 0.1);
    b.push();
    b.move(0, Y0, 0);
    // The gear case: split left/right, with its seam flange; the mounting face to the engine.
    st(b, f);
    b.box(V3(-0.05, -0.03, 0.01), V3(0.3, 0.25, 0.18), 0.04);
    st(b, f, 0.92);
    b.box(V3(-0.05, -0.03, 0.01), V3(0.312, 0.262, 0.012), 0.004);
    b.style(kBolt, METAL);
    for (int i = 0; i < 10; i++) {
        const double a = 2 * PI * (i + 0.5) / 10;
        nut(b, V3(-0.05 + 0.15 * std::cos(a), -0.03 + 0.124 * std::sin(a), 0.017), 2, 0.014, 0.006);
    }
    st(b, f);
    xlathe(b, V3(0.1, -0.0, 0.0), {V2(0.09, 0), V2(0.075, 0.1), V2(0.1, 0.1), V2(0.1, 0.12)}, 12);  // the nose to the engine, its face at inX
    b.style(kBolt, METAL);
    boltRingX(b, V3(CVT_IN + 0.001, 0, 0), 0.085, 6, 0.016, 0.006, 0.3);
    // Output housing, the rear yoke.
    st(b, f);
    xlathe(b, V3(CVT_YOKE + 0.005, 0, 0), {V2(0, 0), V2(0.042, 0), V2(0.05, 0.015), V2(0.06, 0.04), V2(0.07, 0.045)}, 10);
    yoke(b, CVT_YOKE, 0, 0, -1, 0.028);
    // Shift selector on top: a bell crank and the cable.
    st(b, f, 0.9);
    b.cyl(V3(-0.08, 0.095, 0.03), V3(-0.08, 0.13, 0.03), 0.026, 10);
    b.style(kSteel, METAL);
    b.beam(V3(-0.08, 0.13, 0.03), V3(-0.02, 0.135, 0.07), 0.02, 0.01, V3(0, 1, 0));
    b.style(kRubber, MATTE);
    b.tube({V3(-0.02, 0.135, 0.07), V3(0.03, 0.15, 0.09), V3(0.1, 0.2, 0.1)}, 0.006, 5);
    plug(b, V3(-0.1, -0.157, 0.01), 1);
    connector(b, V3(-0.15, 0.05, 0.1), V3(0.025, 0.025, 0.02), {V3(-0.15, 0.06, 0.11), V3(-0.1, 0.1, 0.12), V3(0.0, 0.12, 0.12)});
    // The belt cover: a long rounded peanut over the primary (+X) and secondary clutches, ribbed, bolted round.
    const double PXc = 0.07, PY = -0.01, PR = 0.13, SXc = -0.17, SY = 0.0, SR = 0.15;
    const Poly side = hull({{PXc, PY, PR}, {SXc, SY, SR}}, 0, 12);  // (here: x, y)
    b.style(cov * 0.8, MATTE);
    b.extrude(side, 0.04, -0.12);  // inner half
    b.style(cov, MATTE);
    b.extrude(hull({{PXc, PY, PR - 0.006}, {SXc, SY, SR - 0.006}}, 0, 12), 0.13, -0.205);
    b.style(cov * 1.15, MATTE);
    b.extrude(hull({{PXc, PY, PR + 0.006}, {SXc, SY, SR + 0.006}}, 0, 12), 0.012, -0.14);  // the seam
    b.style(kBolt, METAL);
    for (const V2& v : alongPoly(hull({{PXc, PY, PR + 0.003}, {SXc, SY, SR + 0.003}}, 0, 8), 14)) nut(b, V3(v.x, v.y, -0.148), 2, 0.014, 0.006);
    b.style(cov, MATTE);
    zcyl(b, PXc, PY, -0.27, -0.3, PR - 0.03, 14);  // clutch bulges
    zcyl(b, SXc, SY, -0.27, -0.31, SR - 0.025, 16);
    b.style(cov * 1.6, MATTE);
    for (int i = 0; i < 6; i++) {  // ribs over the outer face
        const double a = PI * (0.2 + i / 5.0 * 0.6);
        b.beam(V3(SXc + 0.02 * std::cos(a), SY + (SR - 0.03) * std::sin(a), -0.31), V3(PXc, PY + (PR - 0.05) * std::sin(a) * 0.8, -0.3), 0.012, 0.012, V3(0, 0, 1));
        b.beam(V3(SXc + 0.02 * std::cos(a), SY - (SR - 0.03) * std::sin(a), -0.31), V3(PXc, PY - (PR - 0.05) * std::sin(a) * 0.8, -0.3), 0.012, 0.012, V3(0, 0, 1));
    }
    b.style(cov * 0.7, MATTE);
    zcyl(b, SXc, SY, -0.31, -0.316, 0.04, 10);
    zcyl(b, PXc, PY, -0.3, -0.306, 0.035, 10);
    stencil(b, "CVT", V3(-0.05, -0.09, -0.271), -1, 0.007, cov * 2.2);
    // Intake and exhaust ducts: corrugated stubs on top.
    auto duct = [&](double x, double y, double z, double lean) {
        b.push();
        b.move(x, y, z);
        b.roll(lean);
        std::vector<V2> pr;
        for (int i = 0; i <= 8; i++) pr.push_back(V2(i & 1 ? 0.042 : 0.048, i * 0.016));
        b.style(V3(0.06, 0.06, 0.065), MATTE);
        b.lathe(V3(), pr, 10);
        b.style(V3(0.2, 0.2, 0.21), METAL);
        b.cyl(V3(0, 0.12, 0), V3(0, 0.135, 0), 0.05, 10, false);
        b.pop();
    };
    duct(PXc + 0.02, PY + PR - 0.02, -0.2, 15);
    duct(SXc - 0.03, SY + SR - 0.02, -0.2, -10);
    b.pop();
}

Reg gearboxCvt({"gearbox_cvt", "GEARBOX CVT", "PARTS", "Side-by-side belt CVT and its gear case", 0.7,
                {
                    {"BELT CVT", "Compact gear case and the long ribbed belt cover with its air ducts",
                     {gbFinishParam(0), choice("cover", "COVER", 0, {"BLACK", "GREY", "OLIVE"})}, cvtBox},
                }});

// ---- transfer_case -----------------------------------------------------------------------------

struct TcDim {
    double inX, rearYoke;
    V3 frontYoke;  // the front yoke's x0 (its pins 6.8 cm further +X), y, z
};
TcDim tcDim(int d) {
    switch (d) {
        case TC_GEAR_IRON: return {0.17, -0.2, V3(0.19, -0.11, -0.09)};
        case TC_CHAIN: return {0.13, -0.26, V3(0.14, -0.095, 0.085)};
        case TC_FULLTIME: return {0.14, -0.25, V3(0.15, -0.1, 0.09)};
        default: return {0.15, -0.18, V3(0.17, -0.12, -0.08)};
    }
}

Param leverParam() { return choice("lever", "LEVER", 1, {"REMOVED", "FITTED"}); }

// The common ends: input flange at inX, rear output housing (from the case's back x to the yoke), the front output.
void tcRear(B& b, const Fin& f, const TcDim& d, double backX, double r0, double r1, double yr) {
    st(b, f);
    const double l = backX - d.rearYoke - 0.005;
    xlathe(b, V3(d.rearYoke + 0.005, 0, 0), {V2(0, 0), V2(0.042, 0), V2(0.05, 0.015), V2(r1, 0.03), V2(r1 + (r0 - r1) * 0.6, l * 0.7), V2(r0, l)}, 12);
    yoke(b, d.rearYoke, 0, 0, -1, yr);
}
void tcFront(B& b, const Fin& f, const TcDim& d, double faceX, double r, double yr) {
    st(b, f);
    const V3 c = d.frontYoke;
    const double l = c.x - 0.005 - faceX;
    xlathe(b, V3(faceX, c.y, c.z), {V2(r, 0), V2(r, l * 0.4), V2(r * 0.75, l * 0.55), V2(0.045, l), V2(0, l)}, 12);
    yoke(b, c.x, c.y, c.z, 1, yr);
}
void tcInput(B& b, const Fin& f, const TcDim& d, double faceX, double r, int bolts) {
    st(b, f, 0.95);
    xlathe(b, V3(faceX, 0, 0), {V2(r, 0), V2(r, d.inX - faceX)}, 14);
    b.style(kBolt, METAL);
    boltRingX(b, V3(d.inX - 0.003, 0, 0), r - 0.016, bolts, 0.016, 0.006, 0.2);
}

void transferBox(B& b, const P& p, int design) {
    const Fin f = fin(p);
    const TcDim d = tcDim(design);
    const bool lever = p.i("lever") == 1;
    if (design == TC_GEAR_IRON) {
        // NP205-style: tall cast iron, a pear section (input over the front output, low on the right), flat covers.
        const std::vector<Circ> sec = {{0, 0, 0.112}, {d.frontYoke.z, d.frontYoke.y, 0.1}};
        const double Y0 = shopLift(b, p, -0.215, -0.2, 0.15, -0.21, -0.1, 0.12, 0.11);
        b.push();
        b.move(0, Y0, 0);
        st(b, f);
        extrudeX(b, hull(sec, 0, 10), -0.12, 0.15);
        st(b, f, 0.92);
        extrudeX(b, hull(sec, 0.008, 10), 0.125, 0.14);
        extrudeX(b, hull(sec, 0.008, 10), -0.11, -0.095);
        tcInput(b, f, d, 0.15, 0.1, 6);
        tcFront(b, f, d, 0.15, 0.065, 0.032);
        tcRear(b, f, d, -0.12, 0.085, 0.06, 0.033);
        // Flat bolted covers: the left side up top, the bottom, the right side down low.
        b.style(f.c * 0.85, f.m);
        b.box(V3(0.01, 0.0, 0.116), V3(0.15, 0.13, 0.012), 0.004);
        b.box(V3(0.01, -0.11, -0.193), V3(0.15, 0.1, 0.012), 0.004);
        b.style(kBolt, METAL);
        for (int i = 0; i < 6; i++) {
            nut(b, V3(0.01 + (i % 3 - 1) * 0.06, (i / 3 ? 0.05 : -0.05), 0.123), 2, 0.016, 0.007);
            nut(b, V3(0.01 + (i % 3 - 1) * 0.06, -0.11 + (i / 3 ? 0.038 : -0.038), -0.2), 2, 0.016, 0.007);
        }
        stencil(b, "4X4", V3(0.01, 0.0, 0.1225), 1, 0.009, V3(0.9, 0.85, 0.55));
        stencil(b, "LOW", V3(0.01, -0.11, -0.1995), -1, 0.007, V3(0.9, 0.85, 0.55));
        // Shift cover on top and the short lever with its big knob.
        st(b, f);
        b.box(V3(0.0, 0.118, 0), V3(0.12, 0.02, 0.1), 0.008);
        b.style(kBolt, METAL);
        for (int i = 0; i < 4; i++) nut(b, V3((i & 1 ? 0.045 : -0.045), 0.129, (i & 2 ? 0.035 : -0.035)), 1, 0.014, 0.006);
        if (lever) {
            st(b, f);
            b.cyl(V3(0, 0.125, 0), V3(0, 0.16, 0), 0.026, 10);
            b.style(V3(0.1, 0.1, 0.11), PAINT);
            b.tube({V3(0, 0.15, 0), V3(0.0, 0.26, 0.0), V3(0.04, 0.34, 0.02)}, 0.011, 6);
            b.style(V3(0.06, 0.06, 0.065), MATTE);
            b.sphere(V3(0.045, 0.37, 0.022), V3(0.033, 0.033, 0.033), 1);
        }
        for (int s = -1; s <= 1; s += 2) {  // mounting pads
            st(b, f);
            b.box(V3(-0.05, -0.03, s * 0.12), V3(0.06, 0.04, 0.03), 0.006);
        }
        plug(b, V3(-0.04, -0.21, -0.09), 1);
        if (p.i("finish") == 1) rustSpots(b, 5, V3(-0.12, -0.1, -0.05), V3(0, 0.1, 0.1), 3, 0, -0.1215);
        b.pop();
    } else if (design == TC_CHAIN || design == TC_FULLTIME) {
        // Aluminium clamshell: front and rear halves, the seam flange and its bolts.
        const bool full = design == TC_FULLTIME;
        const std::vector<Circ> sec = {{0, 0, full ? 0.105 : 0.1}, {d.frontYoke.z, d.frontYoke.y, 0.082}};
        const double fx = full ? 0.12 : 0.11, bx = full ? -0.1 : -0.09;
        const double Y0 = shopLift(b, p, d.frontYoke.y - 0.085, d.rearYoke - 0.02, fx, d.frontYoke.y - 0.082, bx, fx - 0.02, 0.1);
        b.push();
        b.move(0, Y0, 0);
        st(b, f);
        extrudeX(b, hull(sec, 0, 10), 0.008, fx);
        st(b, f, 0.95);
        extrudeX(b, hull(sec, -0.004, 10), bx, -0.008);
        st(b, f, 1.06);
        extrudeX(b, hull(sec, 0.012, 10), -0.008, 0.008);
        boltsRoundX(b, hull(sec, 0.004, 6), 0.0115, 14, 0.014);
        tcInput(b, f, d, fx, 0.088, 6);
        tcFront(b, f, d, fx, 0.058, 0.03);
        if (full) {  // the centre diff's big round bulge at the back
            st(b, f);
            xlathe(b, V3(-0.135, 0, 0), {V2(0, 0), V2(0.1, 0), V2(0.125, 0.015), V2(0.135, 0.05), V2(0.125, 0.085), V2(0.1, 0.1)}, 16);
            tcRear(b, f, d, -0.135, 0.075, 0.052, 0.032);
        } else {
            tcRear(b, f, d, bx, 0.07, 0.048, 0.031);  // long slim rear output housing
        }
        stencil(b, full ? "FULL TIME" : "4WD", V3(full ? 0.06 : -0.045, -0.01, -0.0995), -1, full ? 0.0035 : 0.007, f.c * 0.6, f.m);
        if (!full) {
            if (p.i("motor") == 0) {  // electric shift motor on top, its connector and lead
                st(b, f, 0.9);
                b.box(V3(-0.05, 0.105, 0.0), V3(0.07, 0.03, 0.08), 0.008);
                b.style(V3(0.08, 0.08, 0.09), PAINT);
                b.box(V3(-0.05, 0.13, 0.0), V3(0.075, 0.035, 0.075), 0.01);
                zcyl(b, -0.05, 0.13, 0.035, 0.13, 0.03, 10);
                b.style(V3(0.25, 0.25, 0.27), METAL);
                zcyl(b, -0.05, 0.13, 0.13, 0.138, 0.024, 10);
                connector(b, V3(-0.05, 0.168, 0.0), V3(0.03, 0.02, 0.035), {V3(-0.04, 0.175, 0.0), V3(0.0, 0.19, -0.02), V3(0.06, 0.2, -0.04), V3(0.14, 0.2, -0.04)});
                b.style(kBolt, METAL);
                for (int i = 0; i < 4; i++) nut(b, V3(-0.05 + (i & 1 ? 0.03 : -0.03), 0.122, (i & 2 ? 0.035 : -0.035)), 1, 0.012, 0.005);
            } else {
                st(b, f);
                b.box(V3(-0.04, 0.1, 0), V3(0.09, 0.03, 0.08), 0.008);
                if (lever) shortLever(b, -0.04, 0.115, 0.0, f.c, 0.03);
            }
        } else {
            // The centre-diff-lock actuator (a vacuum can on the right), its line; the floor lever linkage on the left.
            b.style(V3(0.4, 0.4, 0.42), METAL);
            zcyl(b, -0.05, 0.03, -0.09, -0.12, 0.016, 8);
            xlathe(b, V3(-0.095, 0.03, -0.155), {V2(0, 0), V2(0.04, 0), V2(0.048, 0.01), V2(0.048, 0.08), V2(0.04, 0.09), V2(0, 0.09)}, 12);
            b.style(kRubber, MATTE);
            b.tube({V3(-0.1, 0.03, -0.155), V3(-0.13, 0.06, -0.15), V3(-0.12, 0.15, -0.12), V3(0.0, 0.2, -0.1)}, 0.005, 4);
            connector(b, V3(-0.05, 0.08, -0.085), V3(0.025, 0.025, 0.02), {V3(-0.05, 0.09, -0.085), V3(0.0, 0.14, -0.06), V3(0.08, 0.17, -0.05)});
            stencil(b, "LOCK", V3(-0.05, 0.03, -0.2045), -1, 0.003, V3(0.12, 0.12, 0.13));
            b.style(kSteel, METAL);
            zcyl(b, 0.02, 0.06, 0.08, 0.11, 0.014, 8);
            b.beam(V3(0.02, 0.06, 0.11), V3(0.02, 0.13, 0.115), 0.02, 0.01, V3(1, 0, 0));
            if (lever) {
                b.tube({V3(0.02, 0.13, 0.115), V3(0.0, 0.2, 0.11), V3(-0.04, 0.24, 0.1)}, 0.006, 5);
                shortLever(b, -0.045, 0.235, 0.1, V3(0.3, 0.3, 0.32), -0.03);
            }
        }
        st(b, f);
        for (int s = -1; s <= 1; s += 2) b.box(V3(-0.04, -0.05, s * 0.098 + (s > 0 ? 0.0 : 0.0)), V3(0.05, 0.035, 0.03), 0.006);
        plug(b, V3(0.04, d.frontYoke.y - 0.083, d.frontYoke.z), 1);
        b.pop();
    } else {
        // Atlas-style: billet round-cornered gear case, machined plates, beefy yokes, twin sticks.
        const Poly sec = rrect(-0.02, -0.05, 0.27, 0.3, 0.07, 3);
        const double Y0 = shopLift(b, p, -0.2, -0.2, 0.15, -0.2, -0.1, 0.12, 0.11);
        b.push();
        b.move(0, Y0, 0);
        st(b, f, 1.05);
        extrudeX(b, rrect(-0.02, -0.05, 0.285, 0.315, 0.075, 3), 0.11, 0.13);
        extrudeX(b, rrect(-0.02, -0.05, 0.285, 0.315, 0.075, 3), -0.135, -0.115);
        st(b, f);
        extrudeX(b, sec, -0.115, 0.11);
        b.style(kBolt, METAL);
        for (int s = -1; s <= 1; s += 2)
            for (const V2& v : alongPoly(rrect(-0.02, -0.05, 0.24, 0.27, 0.05, 2), 10)) nut(b, V3(s > 0 ? 0.1315 : -0.1365, v.y, v.x), 0, 0.014, 0.006);
        st(b, f, 0.9);  // milled pockets on the sides
        for (int s = -1; s <= 1; s += 2) {
            const double z = -0.02 + s * 0.136;
            b.box(V3(0.0, 0.03, z), V3(0.17, 0.008, 0.006));
            b.box(V3(0.0, -0.13, z), V3(0.17, 0.008, 0.006));
            b.box(V3(0.08, -0.05, z), V3(0.008, 0.16, 0.006));
            b.box(V3(-0.08, -0.05, z), V3(0.008, 0.16, 0.006));
        }
        stencil(b, "4.3 LOW", V3(0.0, -0.05, 0.1175), 1, 0.0045, V3(0.1, 0.1, 0.11));
        tcInput(b, f, d, 0.13, 0.095, 8);
        tcFront(b, f, d, 0.13, 0.065, 0.04);
        tcRear(b, f, d, -0.135, 0.075, 0.058, 0.04);
        // Twin sticks on a shifter base, their linkage rods.
        st(b, f, 0.9);
        b.box(V3(-0.02, 0.115, -0.02), V3(0.09, 0.03, 0.1), 0.01);
        for (int s = -1; s <= 1; s += 2) {
            const double z = -0.02 + s * 0.03;
            b.style(kSteel, METAL);
            b.box(V3(-0.02, 0.135, z), V3(0.04, 0.02, 0.016), 0.003);
            if (lever) {
                b.style(V3(0.15, 0.15, 0.16), PAINT);
                b.tube({V3(-0.02, 0.14, z), V3(-0.02, 0.3, z + s * 0.01), V3(0.0, 0.42, z + s * 0.02)}, 0.009, 6);
                b.style(s > 0 ? V3(0.6, 0.08, 0.06) : V3(0.06, 0.06, 0.065), PAINT);
                b.sphere(V3(0.0, 0.44, z + s * 0.02), V3(0.026, 0.026, 0.026), 1);
            }
            b.style(kSteel, METAL);
            b.tube({V3(0.02, 0.13, z), V3(0.1, 0.11, z), V3(0.125, 0.07, z)}, 0.005, 5);
        }
        st(b, f);
        for (int s = -1; s <= 1; s += 2) b.box(V3(0.0, 0.1, -0.02 + s * 0.12), V3(0.06, 0.025, 0.035), 0.006);
        plug(b, V3(-0.05, -0.205, 0.04), 1);
        b.pop();
    }
}

Reg transferCase({"transfer_case", "TRANSFER CASE", "PARTS", "Career upgrade: 4x4 transfer case", 0.6,
                  {
                      {"GEAR-DRIVEN IRON", "Tall cast-iron gear box, flat covers, a short lever with a big knob",
                       {gbFinishParam(1), leverParam()}, [](B& b, const P& p) { transferBox(b, p, TC_GEAR_IRON); }},
                      {"CHAIN-DRIVEN ELECTRIC", "Alloy clamshell with a bolted seam, shift motor, long slim tail",
                       {gbFinishParam(0), leverParam(), choice("motor", "SHIFT", 0, {"MOTOR", "LEVER"})}, [](B& b, const P& p) { transferBox(b, p, TC_CHAIN); }},
                      {"FULL-TIME CENTER DIFF", "Alloy case with the centre diff bulge, diff-lock actuator, floor lever",
                       {gbFinishParam(0), leverParam()}, [](B& b, const P& p) { transferBox(b, p, TC_FULLTIME); }},
                      {"TWIN-STICK ATLAS", "Billet round-cornered gear case, twin sticks, beefy yokes",
                       {gbFinishParam(0), leverParam()}, [](B& b, const P& p) { transferBox(b, p, TC_ATLAS); }},
                  }});

}  // namespace

namespace drivetrain {

// The ends for a build with params `p` (missing ones take the design's defaults). (The automatics' lengths follow their
// model params: th400, toyota, speeds.)
bool boxEndsFor(const std::string& id, int variant, const mk::P& p, BoxEnds& e) {
    e = BoxEnds();
    const ModelDef* def = findModel(id);
    if (!def || def->variants.empty()) return false;
    const int v = std::clamp(variant, 0, (int)def->variants.size() - 1);
    P full = defaults(def->variants[v]);
    for (const auto& kv : p.v) full.v[kv.first] = kv.second;
    if (id == "gearbox_manual") {
        const MtDim d = mtDim(v);
        e.inX = d.inX;
        e.rear = v == MT_TOPLOADER || v == MT_ALLOY;
        if (e.rear) e.rearAt = V3(d.yokeX - PIN, 0, 0);
        return true;
    }
    if (id == "gearbox_auto") {
        const AtDim d = atDim(v, full);
        e.inX = d.inX, e.rear = true, e.rearAt = V3(d.yokeX - PIN, 0, 0);
        return true;
    }
    if (id == "gearbox_cvt") {
        e.inX = CVT_IN, e.rear = true, e.rearAt = V3(CVT_YOKE - PIN, 0, 0);
        return true;
    }
    if (id == "transfer_case") {
        const TcDim d = tcDim(v);
        e.inX = d.inX, e.rear = e.front = true;
        e.rearAt = V3(d.rearYoke - PIN, 0, 0);
        e.frontAt = d.frontYoke + V3(PIN, 0, 0);
        return true;
    }
    return false;
}

bool boxEnds(const std::string& id, int variant, BoxEnds& e) {
    const ModelDef* def = findModel(id);
    if (!def) return e = BoxEnds(), false;
    return boxEndsFor(id, variant, pickedParams(*def, variant), e);
}

}  // namespace drivetrain
