// Gear: off-road recovery kit: traction boards, winch, winch hook, shackles, straps,
// tree savers, ground anchors and the anchor gear the winch uses (stakes, bolts, pickets, a chain
// sling). (Built with the model kit: see modelkit.h.)
#include <algorithm>
#include <cmath>
#include <functional>

#include "../modelkit.h"

using namespace mk;

namespace {

const double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180; }

// ---- Shared helpers ---------------------------------------------------------------------

// Outline polygon of a ribbon along a centre line (x, y) with a width at each point; round caps.
std::vector<V2> ribbonPoly(const std::vector<V2>& c, const std::vector<double>& w, bool capS = true, bool capE = true, int capSegs = 5) {
    const int n = (int)c.size();
    std::vector<V2> L(n), R(n), T(n), N(n);
    for (int i = 0; i < n; i++) {
        const V2 a = c[std::max(i - 1, 0)], bb = c[std::min(i + 1, n - 1)];
        double tx = bb.x - a.x, ty = bb.y - a.y;
        const double l = std::hypot(tx, ty);
        tx /= l, ty /= l;
        T[i] = V2(tx, ty), N[i] = V2(-ty, tx);
        L[i] = V2(c[i].x + N[i].x * w[i] / 2, c[i].y + N[i].y * w[i] / 2);
        R[i] = V2(c[i].x - N[i].x * w[i] / 2, c[i].y - N[i].y * w[i] / 2);
    }
    std::vector<V2> out;
    for (int i = 0; i < n; i++) out.push_back(L[i]);
    if (capE)
        for (int k = 1; k < capSegs; k++) {
            const double ph = kPi * k / capSegs, r = w[n - 1] / 2;
            out.push_back(V2(c[n - 1].x + (N[n - 1].x * std::cos(ph) + T[n - 1].x * std::sin(ph)) * r,
                             c[n - 1].y + (N[n - 1].y * std::cos(ph) + T[n - 1].y * std::sin(ph)) * r));
        }
    for (int i = n - 1; i >= 0; i--) out.push_back(R[i]);
    if (capS)
        for (int k = 1; k < capSegs; k++) {
            const double ph = kPi * k / capSegs, r = w[0] / 2;
            out.push_back(V2(c[0].x - (N[0].x * std::cos(ph) + T[0].x * std::sin(ph)) * r, c[0].y - (N[0].y * std::cos(ph) + T[0].y * std::sin(ph)) * r));
        }
    return out;
}
std::vector<V2> ribbonPoly(const std::vector<V2>& c, double w, bool capS = true, bool capE = true, int capSegs = 5) {
    return ribbonPoly(c, std::vector<double>(c.size(), w), capS, capE, capSegs);
}

// Points on an ellipse in the plane (u, v) round c, from a0 to a1 degrees (n+1 points).
std::vector<V3> arcPath(const V3& c, const V3& u, const V3& v, double ru, double rv, double a0, double a1, int n) {
    std::vector<V3> p;
    for (int i = 0; i <= n; i++) {
        const double a = rad(a0 + (a1 - a0) * i / n);
        p.push_back(c + u * (ru * std::cos(a)) + v * (rv * std::sin(a)));
    }
    return p;
}
void cat(std::vector<V3>& a, const std::vector<V3>& b) { a.insert(a.end(), b.begin(), b.end()); }
// Catmull-Rom through the points, `sub` pieces per span: smooth bent tubes from a few control points.
std::vector<V3> spline(const std::vector<V3>& p, int sub) {
    std::vector<V3> o;
    const int n = (int)p.size();
    for (int i = 0; i + 1 < n; i++) {
        const V3 &p0 = p[std::max(i - 1, 0)], &p1 = p[i], &p2 = p[i + 1], &p3 = p[std::min(i + 2, n - 1)];
        for (int k = 0; k < sub; k++) {
            const double t = (double)k / sub, t2 = t * t, t3 = t2 * t;
            o.push_back((p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5);
        }
    }
    o.push_back(p.back());
    return o;
}
// A closed stadium (slot) outline in the plane (u, v): length along u.
std::vector<V3> stadium(const V3& c, const V3& u, const V3& v, double length, double width, int nArc = 5) {
    const double L = std::max(0.0, (length - width) / 2), r = width / 2;
    std::vector<V3> p = arcPath(c + u * L, u, v, r, r, -90, 90, nArc);
    cat(p, arcPath(c - u * L, u, v, r, r, 90, 270, nArc));
    return p;
}

// Turn the frame so the local up axis points along an axis: 'x','y','z' (positive), 'X','Z' (negative).
void axisFrame(B& b, char axis) {
    if (axis == 'z') b.roll(90);
    else if (axis == 'Z') b.roll(-90);
    else if (axis == 'x') b.pitch(-90);
    else if (axis == 'X') b.pitch(90);
}
void latheAxis(B& b, const V3& base, char axis, const std::vector<V2>& prof, int segs = 18, bool smooth = true) {
    b.push();
    b.move(base);
    axisFrame(b, axis);
    b.lathe(V3(), prof, segs, smooth);
    b.pop();
}
void ringAxis(B& b, const V3& c, char axis, double R, double r, int segs = 18, int sides = 6, double arc = 360) {
    b.push();
    b.move(c);
    axisFrame(b, axis);
    b.torus(V3(), R, r, segs, sides, arc);
    b.pop();
}
void cylAxis(B& b, const V3& base, char axis, double len, double r, int segs = 14, bool caps = true) {
    b.push();
    b.move(base);
    axisFrame(b, axis);
    b.cyl(V3(), V3(0, len, 0), r, segs, caps);
    b.pop();
}
void hexBolt(B& b, const V3& p, char axis, double r, double h) {
    b.push();
    b.move(p);
    axisFrame(b, axis);
    b.cyl(V3(), V3(0, h, 0), r, 6);
    b.pop();
}
// A closed-profile ring (rectangle with rounded corners) turned about local up.
std::vector<V2> ringProfile(double rin, double rout, double y0, double y1, double bev) {
    std::vector<V2> p;
    p.push_back(V2(rin + bev, y0)), p.push_back(V2(rout - bev, y0)), p.push_back(V2(rout - bev * 0.3, y0 + bev * 0.3)), p.push_back(V2(rout, y0 + bev));
    p.push_back(V2(rout, y1 - bev)), p.push_back(V2(rout - bev * 0.3, y1 - bev * 0.3)), p.push_back(V2(rout - bev, y1));
    p.push_back(V2(rin + bev, y1)), p.push_back(V2(rin + bev * 0.3, y1 - bev * 0.3)), p.push_back(V2(rin, y1 - bev));
    p.push_back(V2(rin, y0 + bev)), p.push_back(V2(rin + bev * 0.3, y0 + bev * 0.3)), p.push_back(V2(rin + bev, y0));
    return p;
}

V3 lerp3(const V3& a, const V3& b, double t) { return mix(a, b, t); }
const V3 kSteel(0.50, 0.51, 0.52), kGalv(0.60, 0.62, 0.63), kDark(0.12, 0.12, 0.13), kRust(0.45, 0.22, 0.09), kDirt(0.30, 0.25, 0.18);

// Rope / line colours shared by the winches: 0 steel, then fibre colours.
const std::vector<std::string>& ropeNames() {
    static const std::vector<std::string> n = {"STEEL", "RED", "YELLOW", "GREY", "ORANGE", "BLACK"};
    return n;
}
V3 ropeCol(int i) {
    static const V3 c[] = {V3(0.34, 0.35, 0.36), V3(0.70, 0.08, 0.07), V3(0.92, 0.74, 0.08), V3(0.50, 0.51, 0.52), V3(0.90, 0.38, 0.06), V3(0.10, 0.10, 0.11)};
    return c[std::clamp(i, 0, 5)];
}
Mat ropeMat(int i) { return i == 0 ? METAL : MATTE; }

// Scatters little dirt / scuff marks on a flat top: n boxes in a (x0..x1, z0..z1) patch at height y.
void scuffs(B& b, const V3& col, int n, double x0, double x1, double z0, double z1, double y, uint32_t seed, double size = 0.03) {
    b.color(col);
    for (int i = 0; i < n; i++) {
        const double x = x0 + (x1 - x0) * hash01(seed, i * 3 + 1), z = z0 + (z1 - z0) * hash01(seed, i * 3 + 2);
        const double s = size * (0.4 + hash01(seed, i * 3 + 3));
        b.box(V3(x, y, z), V3(s, 0.002, s * (0.4 + 0.8 * hash01(seed, i + 77))), 0.0005);
    }
}

// Stacks `count` things loosely: each one `step` higher, a touch off.
void stackOf(B& b, int count, double step, const std::function<void(int)>& f) {
    for (int i = 0; i < count; i++) {
        b.push();
        b.move(0.012 * i, i * step, -0.014 * i);
        b.yaw(2.4 * i);
        f(i);
        b.pop();
    }
}

// ---- 1. Traction boards ------------------------------------------------------------------

// Moulded nylon board: a perforated plate, a ladder of chunky cleats, handle cut-outs.
void nylonBoard(B& b, const V3& col, double wear, int idx) {
    const double L = 1.1, W = 0.33, T = 0.035, hl = L / 2, hw = W / 2, cut = 0.075;
    std::vector<V2> pts;
    const double xs[3] = {-0.32, 0.0, 0.32};
    pts.push_back(V2(-hl + cut, -hw));
    for (double xc : xs) {
        pts.push_back(V2(xc - 0.07, -hw)), pts.push_back(V2(xc - 0.055, -hw + 0.04)), pts.push_back(V2(xc + 0.055, -hw + 0.04)), pts.push_back(V2(xc + 0.07, -hw));
    }
    pts.push_back(V2(hl - cut, -hw)), pts.push_back(V2(hl, -hw + cut));
    pts.push_back(V2(hl, -0.075)), pts.push_back(V2(hl - 0.05, -0.058)), pts.push_back(V2(hl - 0.05, 0.058)), pts.push_back(V2(hl, 0.075));
    pts.push_back(V2(hl, hw - cut)), pts.push_back(V2(hl - cut, hw));
    for (int k = 2; k >= 0; k--) {
        const double xc = xs[k];
        pts.push_back(V2(xc + 0.07, hw)), pts.push_back(V2(xc + 0.055, hw - 0.04)), pts.push_back(V2(xc - 0.055, hw - 0.04)), pts.push_back(V2(xc - 0.07, hw));
    }
    pts.push_back(V2(-hl + cut, hw)), pts.push_back(V2(-hl, hw - cut));
    pts.push_back(V2(-hl, 0.075)), pts.push_back(V2(-hl + 0.05, 0.058)), pts.push_back(V2(-hl + 0.05, -0.058)), pts.push_back(V2(-hl, -0.075));
    pts.push_back(V2(-hl, -hw + cut));
    const double dirtK = wear;
    b.style(col * (0.9 - 0.12 * dirtK), MATTE);
    b.extrudeUp(pts, T, 0);
    // The cleats: one extrusion of trapezoids along the length.
    std::vector<V2> prof;
    const int nC = 17;
    const double pitch = 0.0585, x00 = -0.5;
    prof.push_back(V2(x00 - 0.002, 0.03));
    for (int k = 0; k < nC; k++) {
        const double x0 = x00 + k * pitch;
        prof.push_back(V2(x0, 0.0375)), prof.push_back(V2(x0 + 0.015, 0.075)), prof.push_back(V2(x0 + 0.037, 0.075)), prof.push_back(V2(x0 + 0.052, 0.0375));
    }
    prof.push_back(V2(x00 + (nC - 1) * pitch + 0.054, 0.03));
    b.color(col);
    b.extrude(prof, 0.215, 0);
    // Raised side rails each side of the cleat field, and end ribs.
    b.color(col * 0.86);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 0.0445, s * 0.1255), V3(0.93, 0.019, 0.022));
    }
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.505, 0.0445, 0), V3(0.03, 0.019, 0.2));
    // Dark slots (the inner handle holes) in the margins.
    b.color(col * 0.28);
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 2; k++) b.box(V3(-0.16 + k * 0.32, 0.0356, s * 0.1425), V3(0.14, 0.0012, 0.018));
    if (wear > 0.02) {
        scuffs(b, kDirt * 0.8, (int)(wear * 24), -0.48, 0.48, -0.1, 0.1, 0.0752, 100 + idx * 9, 0.03);
        scuffs(b, col * 1.2, (int)(wear * 10), -0.48, 0.48, -0.1, 0.1, 0.0752, 300 + idx * 9, 0.02);
    }
}

// Aluminium ladder: two slotted rails and a run of cross rungs with a gripping ridge.
void ladderBoard(B& b, const V3& col, double wear, int idx) {
    const double L = 1.1, H = 0.06;
    const V3 metal = col * (1 - 0.22 * wear);
    b.style(metal, METAL);
    b.both([&] {
        b.box(V3(0, H - 0.007, 0.15), V3(L, 0.014, 0.034), 0.004);
        b.box(V3(0, 0.0055, 0.15), V3(L, 0.011, 0.034), 0.004);
        for (int k = 0; k < 11; k++) b.box(V3(-0.5 + k * 0.1, H / 2, 0.15), V3(0.052, H - 0.02, 0.006));
        // end caps
        b.color(metal * 0.85);
        b.box(V3(L / 2 - 0.004, H / 2, 0.15), V3(0.012, H, 0.034));
        b.box(V3(-L / 2 + 0.004, H / 2, 0.15), V3(0.012, H, 0.034));
        b.color(metal);
    });
    for (int k = 0; k < 9; k++) {
        const double x = -0.48 + k * 0.12;
        b.color(metal);
        b.box(V3(x, 0.045, 0), V3(0.05, 0.014, 0.275), 0.0035);
        b.color(metal * 1.08);
        b.box(V3(x + 0.015, 0.0585, 0), V3(0.012, 0.012, 0.268));
    }
    // Cross braces under the rungs and a hand-hold stamp.
    b.color(metal * 0.8);
    b.box(V3(0, 0.014, 0), V3(0.03, 0.014, 0.275));
    b.box(V3(0.36, 0.014, 0), V3(0.03, 0.014, 0.275));
    b.box(V3(-0.36, 0.014, 0), V3(0.03, 0.014, 0.275));
    if (wear > 0.02) scuffs(b, V3(0.28, 0.29, 0.3), (int)(wear * 16), -0.48, 0.48, -0.12, 0.12, 0.0652, 500 + idx * 7, 0.025);
}

// Old planks: three boards, nailed-on cross cleats, rope handles through the ends.
void woodBoard(B& b, int tone, double wear, int idx) {
    static const V3 tones[] = {V3(0.62, 0.48, 0.30), V3(0.72, 0.60, 0.38), V3(0.50, 0.46, 0.40), V3(0.36, 0.27, 0.18)};
    const V3 base = tones[std::clamp(tone, 0, 3)] * (1 - 0.25 * wear);
    const double T = 0.034, pw = 0.104;
    b.mat(WOOD);
    for (int k = 0; k < 3; k++) {
        const double len = 1.1 - wear * 0.1 * hash01(900 + idx, k);
        const double z = (k - 1) * (pw + 0.009);
        b.color(base * (0.88 + 0.22 * hash01(910 + idx, k)));
        b.box(V3((1.1 - len) * (k - 1) * 0.3, T / 2, z), V3(len, T, pw), 0.003);
    }
    // Cross cleats on top.
    for (int k = 0; k < 7; k++) {
        const double x = -0.45 + k * 0.15;
        b.color(base * (0.8 + 0.2 * hash01(930 + idx, k)));
        b.box(V3(x, T + 0.012, 0), V3(0.045, 0.026, 0.322), 0.004);
        b.style(kRust, METAL);
        for (int j = -1; j <= 1; j++) b.box(V3(x, T + 0.0255, j * 0.113), V3(0.011, 0.0035, 0.011));
        b.mat(WOOD);
    }
    // Rope handles at both ends, through drilled holes.
    for (int e = -1; e <= 1; e += 2) {
        const double x = e * 0.5;
        b.style(V3(0.05, 0.04, 0.03), MATTE);
        b.box(V3(x, T + 0.0008, 0), V3(0.024, 0.0016, 0.1));
        b.style(V3(0.66, 0.56, 0.36), MATTE);
        b.tube({V3(x, T - 0.004, -0.05), V3(x + e * 0.004, T + 0.025, -0.045), V3(x + e * 0.02, T + 0.055, -0.02), V3(x + e * 0.022, T + 0.058, 0), V3(x + e * 0.02, T + 0.055, 0.02),
                V3(x + e * 0.004, T + 0.025, 0.045), V3(x, T - 0.004, 0.05)}, 0.0085, 6);
        b.sphere(V3(x - e * 0.001, T + 0.004, -0.05), V3(0.014, 0.012, 0.014), 0);
        b.sphere(V3(x - e * 0.001, T + 0.004, 0.05), V3(0.014, 0.012, 0.014), 0);
        b.mat(WOOD);
    }
    // Cracks and a chipped corner.
    if (wear > 0.1) {
        b.style(V3(0.08, 0.06, 0.04), MATTE);
        for (int k = 0; k < 3; k++)
            b.box(V3(-0.2 + 0.3 * k, T + 0.0006, (k - 1) * 0.108 + 0.02 * k), V3(0.12 + 0.1 * wear, 0.0012, 0.003));
    }
}

Reg tractionBoards({"traction_boards", "TRACTION BOARDS", "RECOVERY", "A pair of traction boards: carried on the roof rack, laid under a stuck wheel; a career find", 1.1,
                    {
                        {"MOULDED NYLON", "Chunky-cleated nylon boards with handle cut-outs", {paintParam(4), num("count", "COUNT", 2, 1, 2, 1), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                         [](B& b, const P& p) {
                             const V3 col = paint(p.i("paint")) * 0.95;
                             stackOf(b, std::max(1, p.i("count")), 0.076, [&](int i) { nylonBoard(b, col, p("wear"), i); });
                         }},
                        {"ALUMINIUM LADDER", "Slotted alloy rails and gripping rungs", {choice("finish", "FINISH", 0, {"BARE", "RED", "BLUE", "BLACK"}), num("count", "COUNT", 2, 1, 2, 1), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                         [](B& b, const P& p) {
                             static const V3 fin[] = {V3(0.74, 0.76, 0.78), V3(0.62, 0.10, 0.08), V3(0.12, 0.28, 0.58), V3(0.10, 0.10, 0.11)};
                             const V3 col = fin[std::clamp(p.i("finish"), 0, 3)];
                             stackOf(b, std::max(1, p.i("count")), 0.066, [&](int i) { ladderBoard(b, col, p("wear"), i); });
                         }},
                        {"OLD PLANKS", "Nailed timber with rope handles", {choice("wood", "WOOD", 0, {"OAK", "PINE", "WEATHERED", "DARK"}), num("count", "COUNT", 2, 1, 2, 1), num("wear", "WEAR", 0.4, 0, 1, 0.05)},
                         [](B& b, const P& p) { stackOf(b, std::max(1, p.i("count")), 0.07, [&](int i) { woodBoard(b, p.i("wood"), p("wear"), i); }); }},
                    }});

// ---- Hooks (shared by the winch hook and the winches) -----------------------------------------

// The teardrop outline of a rope eye round a pin: round end at `c` (radius r), tip `d` behind it.
std::vector<V3> teardrop(const V3& c, double r, double d, int n = 10) {
    const double a = std::acos(std::min(0.95, r / d)) * 180 / kPi;  // tangent angle from the tip
    std::vector<V3> p;
    p.push_back(c - V3(d, 0, 0));
    cat(p, arcPath(c, V3(1, 0, 0), V3(0, 0, 1), r, r, 180 - a, -(180 - a), n));
    return p;
}

// A clevis hook with its rope eye, lying flat: the pin (vertical) at x = 0, the hook curling toward
// +z with its tip back toward -x, the rope running off along -x for `tail` metres.
// style 0: the tall winch hook; style 1: the stubby grab hook with a bigger throat.
void clevisHook(B& b, const V3& col, const V3& latchCol, int ropeIdx, double wear, int style, double tail, bool ferrule = true, bool lo = false) {
    const double T = 0.03, y0 = 0.006;
    const double R = style ? 0.045 : 0.041, xs = style ? 0.078 : 0.086, bodyW = style ? 0.036 : 0.03, tipW = style ? 0.02 : 0.015, tipA = style ? 165 : 150;
    // Body: shank, bowl, tapering tip.
    std::vector<V2> cl;
    std::vector<double> w;
    cl.push_back(V2(0.047, 0)), w.push_back(bodyW);
    cl.push_back(V2((0.047 + xs) / 2, 0)), w.push_back(bodyW);
    const int nA = lo ? 7 : 12;
    for (int i = 0; i <= nA; i++) {
        const double a = rad(-90 + (tipA + 90) * i / nA), t = (double)i / nA;
        cl.push_back(V2(xs + R * std::cos(a), R + R * std::sin(a)));
        w.push_back(bodyW - (bodyW - tipW) * std::pow(t, 1.4));
    }
    b.style(col * (1 - 0.15 * wear), PAINT);
    const std::vector<V2> body = ribbonPoly(cl, w, true, true, lo ? 3 : 5);
    b.extrudeUp(body, T, y0);
    // Clevis ears (a fork: the thimble sits between them).
    const std::vector<V2> ear = ribbonPoly({V2(-0.03, 0), V2(0.05, 0)}, 0.042, true, true, lo ? 3 : 6);
    b.extrudeUp(ear, 0.0085, y0);
    b.extrudeUp(ear, 0.0085, y0 + T - 0.0085);
    // The forged rib along the top of the bowl.
    std::vector<double> w2;
    for (double x : w) w2.push_back(x * 0.4);
    b.color(col * 1.18);
    if (!lo) b.extrudeUp(ribbonPoly(std::vector<V2>(cl.begin() + 1, cl.end()), std::vector<double>(w2.begin() + 1, w2.end()), false, true, 4), 0.0035, y0 + T);
    // Pin, nut, cotter.
    b.style(kSteel * 1.1, METAL);
    b.cyl(V3(0, y0 - 0.001, 0), V3(0, y0 + T + 0.002, 0), 0.0065, 6);
    b.cyl(V3(0, 0, 0), V3(0, y0 + 0.001, 0), 0.011, 6);
    if (!lo) b.cyl(V3(0, y0 + T, 0), V3(0, y0 + T + 0.008, 0), 0.0105, 6);
    // Latch: a spring-loaded gate across the throat, pivoting on a rivet in the shank.
    const double pz = bodyW / 2 + 0.002;
    const V3 piv(0.04, 0, pz), tipP(0.0 + (style ? 0.008 : 0.0), 0, R + R * std::sin(rad(tipA)) - tipW / 2 - 0.004);
    b.style(latchCol * 0.92, PAINT);
    b.beam(V3(piv.x, y0 + T * 0.55, piv.z), V3(tipP.x, y0 + T * 0.55, tipP.z), 0.008, 0.011, V3(0, 1, 0), lo ? 0.0 : 0.0015);
    b.style(kSteel, METAL);
    b.cyl(V3(piv.x, y0 + 0.003, piv.z), V3(piv.x, y0 + T + 0.004, piv.z), 0.0058, 7);
    // Rope eye round a thimble.
    const double ye = y0 + T / 2;
    b.style(kGalv, METAL);
    if (!lo) b.tube(arcPath(V3(0, ye, 0), V3(1, 0, 0), V3(0, 0, 1), 0.0108, 0.0108, -150, 150, lo ? 6 : 12), 0.0042, lo ? 5 : 6);
    const V3 rc = ropeCol(ropeIdx);
    b.style(rc, ropeIdx == 0 ? METAL : MATTE);
    std::vector<V3> eye = teardrop(V3(0, ye, 0), 0.0185, 0.098, lo ? 6 : 10);
    b.tube(eye, 0.0058, lo ? 5 : 6, true);
    if (ferrule) {
        b.style(kGalv, METAL);
        cylAxis(b, V3(-0.128, ye, 0), 'x', 0.034, 0.0105, lo ? 7 : 10);
        b.style(rc, ropeIdx == 0 ? METAL : MATTE);
        b.tube({V3(-0.126, ye, 0), V3(-0.126 - tail, ye, 0)}, 0.0062, 7);
    }
    if (wear > 0.05 && !lo) {
        b.style(kRust, MATTE);
        for (int i = 0; i < (int)(wear * 8); i++) {
            const double x = 0.05 + 0.08 * hash01(41, i), z = (hash01(42, i) - 0.3) * 0.05;
            b.box(V3(x, y0 + T + 0.0043, z), V3(0.008, 0.001, 0.006));
        }
    }
}

// ---- 3. Winch hook -------------------------------------------------------------------------

// A soft shackle linked through a spliced eye: a Dyneema line, a stopper-knotted loop.
void softShackleLine(B& b, const V3& lineC, const V3& loopC, double wear) {
    const double r = 0.0065;
    b.style(lineC, MATTE);
    // The line and its eye splice (a teardrop that rises over the loop's lower strand).
    std::vector<V3> eye = teardrop(V3(-0.052, 0.0065, 0), 0.016, 0.075);
    for (V3& q : eye) q.y = 0.0065 + 0.013 * std::clamp((q.x + 0.127) / 0.07, 0.0, 1.0);
    b.tube(eye, r, 6, true);
    b.tube({V3(-0.127, 0.0065, 0), V3(-0.2, 0.0065, 0)}, r, 6);
    b.style(V3(0.10, 0.10, 0.11), MATTE);  // chafe sleeve over the splice
    cylAxis(b, V3(-0.138, 0.0066, 0), 'x', 0.034, 0.0098, 10);
    // The soft shackle: a loop standing through the eye, with a diamond stopper knot.
    b.style(loopC, MATTE);
    std::vector<V3> lp = stadium(V3(-0.002, 0.0205, 0), V3(1, 0, 0), V3(0, 1, 0), 0.128, 0.034, 6);
    b.tube(lp, r, 6, true);
    b.sphere(V3(0.058, 0.0205, 0), V3(0.016, 0.0135, 0.0135), 1);
    b.color(loopC * 0.8);
    b.sphere(V3(0.065, 0.0205, 0), V3(0.009, 0.01, 0.01), 1);
    if (wear > 0.05) {
        b.color(kDirt * 0.8);
        for (int i = 0; i < (int)(wear * 7); i++)
            b.box(V3(-0.1 + 0.15 * hash01(61, i), 0.0125, (hash01(62, i) - 0.5) * 0.02), V3(0.014, 0.002, 0.006));
    }
}

Reg winchHook({"winch_hook", "WINCH HOOK", "RECOVERY", "The hook end of a winch line: clipped to the fairlead, or carried; a career find", 0.27,
               {
                   {"FORGED CLEVIS", "Forged clevis hook with a sprung latch on steel wire rope",
                    {paintParam(8), choice("rope", "LINE", 0, ropeNames()), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) { clevisHook(b, paint(p.i("paint")), V3(0.55, 0.56, 0.58), p.i("rope"), p("wear"), 0, 0.06); }},
                   {"SOFT SHACKLE", "Synthetic line spliced to an eye, a knotted soft shackle through it",
                    {paintParam(4, "line", "LINE"), paintParam(8, "loop", "SOFT SHACKLE"), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) { softShackleLine(b, paint(p.i("line")), paint(p.i("loop")), p("wear")); }},
                   {"STRAP HOOK", "Stubby grab hook with a red safety strap loop",
                    {paintParam(3), paintParam(0, "strap", "STRAP"), choice("rope", "LINE", 0, ropeNames()), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) {
                        clevisHook(b, paint(p.i("paint")), paint(8), p.i("rope"), p("wear"), 1, 0.02);
                        const V3 sc = paint(p.i("strap"));
                        // The strap: a band round the shank, two runs back, a hand loop at the end.
                        b.style(sc, MATTE);
                        b.box(V3(0.06, 0.021, 0), V3(0.024, 0.038, 0.048), 0.004);
                        std::vector<V2> c = {V2(0.056, 0.036), V2(0.0, 0.036), V2(-0.12, 0.036), V2(-0.2, 0.036)};
                        for (int i = 0; i <= 8; i++) {
                            const double a = rad(90 + 180.0 * i / 8);
                            c.push_back(V2(-0.2 + 0.036 * std::cos(a), 0.036 * std::sin(a)));
                        }
                        for (V2 q : std::vector<V2>{V2(-0.12, -0.036), V2(0.0, -0.036), V2(0.056, -0.036)}) c.push_back(q);
                        b.extrudeUp(ribbonPoly(c, 0.018, false, false), 0.004, 0.0);
                        b.color(V3(0.08, 0.07, 0.07));
                        for (int s = -1; s <= 1; s += 2) b.box(V3(0.06, 0.0405, s * 0.018), V3(0.018, 0.0012, 0.003));
                        b.color(sc * 0.75);
                        b.box(V3(-0.16, 0.0042, 0.036), V3(0.04, 0.0012, 0.017));
                        b.box(V3(-0.16, 0.0042, -0.036), V3(0.04, 0.0012, 0.017));
                    }},
               }});

// ---- 2. Winch ---------------------------------------------------------------------------------

// The drum: a steel core between two flanges with one layer of line wound on it (a zig-zag lathe).
void woundDrum(B& b, double yc, double zh, double core, double rr, int ropeIdx, double flangeR, double flangeT, const V3& flangeCol, int segs) {
    b.style(kSteel * 0.9, METAL);
    cylAxis(b, V3(0, yc, -zh), 'z', 2 * zh, core, 8);
    b.style(flangeCol, PAINT);
    cylAxis(b, V3(0, yc, zh), 'z', flangeT, flangeR, 16);
    cylAxis(b, V3(0, yc, -zh - flangeT), 'z', flangeT, flangeR, 16);
    const int turns = std::max(3, (int)(2 * zh / (2 * rr)));
    std::vector<V2> pr;
    pr.push_back(V2(core, 0));
    for (int k = 0; k < turns; k++) pr.push_back(V2(core + rr, k * 2 * rr)), pr.push_back(V2(core + 2 * rr, k * 2 * rr + rr));
    pr.push_back(V2(core + rr, turns * 2 * rr)), pr.push_back(V2(core, turns * 2 * rr));
    b.style(ropeCol(ropeIdx), ropeMat(ropeIdx));
    latheAxis(b, V3(0, yc, -turns * rr), 'z', pr, segs);
}

// Four rollers in a frame: the standard fairlead for steel cable.
void rollerFairlead(B& b, double x, double yc, const V3& paintC) {
    b.style(paintC, PAINT);
    for (int s = -1; s <= 1; s += 2) b.box(V3(x, 0.07, s * 0.131), V3(0.07, 0.14, 0.012));
    b.box(V3(x, yc + 0.0525, 0), V3(0.07, 0.012, 0.274));
    b.box(V3(x, yc - 0.0525, 0), V3(0.07, 0.012, 0.274));
    b.style(kSteel * 1.25, METAL);
    for (int s = -1; s <= 1; s += 2) {
        cylAxis(b, V3(x + 0.006, yc - 0.0465, s * 0.061), 'y', 0.093, 0.0125, 9);
        cylAxis(b, V3(x - 0.004, yc + s * 0.032, -0.049), 'z', 0.098, 0.011, 9);
    }
    b.style(kDark * 1.3, METAL);
    for (int s = -1; s <= 1; s += 2) hexBolt(b, V3(x, yc + 0.0585, s * 0.11), 'y', 0.007, 0.005);
}

// A slab of machined aluminium with a rounded window: the hawse fairlead for synthetic rope.
void hawseFairlead(B& b, double x, double yc) {
    const V3 alu(0.72, 0.74, 0.77);
    b.style(alu * 0.92, METAL);
    const double t = 0.028, wy = 0.0215, wz = 0.05;
    b.box(V3(x, (yc - wy) / 2, 0), V3(t, yc - wy, 0.21), 0.004);
    b.box(V3(x, yc + wy + 0.0155, 0), V3(t, 0.031, 0.21), 0.004);
    for (int s = -1; s <= 1; s += 2) b.box(V3(x, yc, s * (wz + 0.0275)), V3(t, 2 * wy + 0.0001, 0.055));
    b.style(alu * 1.1, CHROME);
    b.tube(stadium(V3(x, yc, 0), V3(0, 0, 1), V3(0, 1, 0), 2 * wz + 0.01, 2 * wy + 0.008, 4), 0.0075, 5, true);
    b.style(kDark, METAL);
    for (int s = -1; s <= 1; s += 2) {
        hexBolt(b, V3(x + t / 2, 0.025, s * 0.085), 'x', 0.0065, 0.005);
    }
    b.color(V3(0.62, 0.08, 0.06));  // a red anodised hook-stop plate
    b.box(V3(x + t / 2 + 0.002, yc - wy - 0.014, 0), V3(0.004, 0.016, 0.07));
}

// The contactor box on the motor, with its four terminals and an invented brand plate.
void contactor(B& b, double x, double y, double z, double w, double d, double h, const V3& body, const char* brand, const V3& plate) {
    b.style(body, PAINT);
    b.box(V3(x, y + h / 2, z), V3(d, h, w), 0.008);
    b.style(plate, MATTE);
    b.box(V3(x + d / 2 + 0.0012, y + h / 2, z), V3(0.0025, h * 0.62, w * 0.82));
    b.style(V3(0.08, 0.08, 0.09), MATTE);  // the brand's slash mark
    for (int k = -1; k <= 1; k++)
        b.beam(V3(x + d / 2 + 0.003, y + h * 0.2, z + k * w * 0.16 - w * 0.06), V3(x + d / 2 + 0.003, y + h * 0.8, z + k * w * 0.16 + w * 0.06), 0.012, 0.0016, V3(1, 0, 0));
    (void)brand;
    for (int i = 0; i < 4; i++) {
        const double tx = x + ((i & 1) ? 1 : -1) * d * 0.3, tz = z + ((i & 2) ? 1 : -1) * w * 0.34;
        b.style((i == 0 || i == 3) ? V3(0.7, 0.08, 0.06) : V3(0.08, 0.08, 0.09), MATTE);
        b.cyl(V3(tx, y + h - 0.002, tz), V3(tx, y + h + 0.012, tz), 0.0072, 6);
    }
}

void compactWinch(B& b, const P& p, bool synth) {
    const V3 body = paint(p.i("paint"));
    const int rope = p.i("rope");
    const bool hook = p.i("hook") != 0;
    const double YC = 0.118, zh = 0.092;
    b.move(-0.04, 0, 0);
    // The base plate and the two end brackets.
    b.style(body * 0.75, PAINT);
    b.box(V3(0, 0.007, 0), V3(0.24, 0.014, 0.25), 0.004);
    std::vector<V2> br = {V2(-0.092, 0.014), V2(0.092, 0.014), V2(0.092, 0.1)};
    for (int a = 15; a <= 165; a += 15) br.push_back(V2(0.098 * std::cos(rad(a)), YC + 0.098 * std::sin(rad(a))));
    br.push_back(V2(-0.092, 0.1));
    b.style(body, PAINT);
    b.extrude(br, 0.02, 0.11);
    b.extrude(br, 0.02, -0.11);
    b.style(kDark * 1.4, METAL);  // bolts on the base plate
    for (int s = -1; s <= 1; s += 2)
        for (int e = -1; e <= 1; e += 2) hexBolt(b, V3(e * 0.108, 0.014, s * 0.1), 'y', 0.0075, 0.007);
    // Tie bars.
    b.style(kSteel * 1.1, METAL);
    for (int e = -1; e <= 1; e += 2) {
        cylAxis(b, V3(e * 0.072, 0.034, -0.138), 'z', 0.276, 0.0095, 7);

    }
    // Drum with its wound line.
    if (synth) woundDrum(b, YC, zh, 0.032, 0.0092, rope, 0.07, 0.008, body * 0.85, 10);
    else woundDrum(b, YC, zh, 0.032, 0.0082, rope, 0.07, 0.008, body * 0.85, 10);
    // Motor and contactor.
    if (!synth) {
        b.style(V3(0.1, 0.1, 0.11), PAINT);
        cylAxis(b, V3(0, YC, -0.12), 'Z', 0.135, 0.056, 16);
        b.style(body, PAINT);
        latheAxis(b, V3(0, YC, -0.255), 'Z', {V2(0, 0), V2(0.05, 0), V2(0.056, 0.006), V2(0.056, 0.012)}, 14);
        b.style(kSteel * 1.1, METAL);
        cylAxis(b, V3(0, YC, -0.2), 'z', 0.008, 0.0585, 16);
        hexBolt(b, V3(0, YC, -0.2559), 'Z', 0.012, 0.006);
        contactor(b, 0.0, YC + 0.056 - 0.004, -0.19, 0.1, 0.075, 0.034, V3(0.08, 0.08, 0.085), "MULE", paint(8) * 0.9);
    } else {
        b.style(V3(0.2, 0.21, 0.23), PAINT);
        cylAxis(b, V3(0, YC, -0.12), 'Z', 0.152, 0.052, 12);
        b.style(kSteel * 1.2, METAL);  // cooling fins
        for (int k = 0; k < 4; k++) cylAxis(b, V3(0, YC, -0.14 - k * 0.032), 'Z', 0.008, 0.0605, 10);
        b.style(body, PAINT);
        latheAxis(b, V3(0, YC, -0.272), 'Z', {V2(0, 0), V2(0.044, 0), V2(0.052, 0.008), V2(0.054, 0.016), V2(0.05, 0.022), V2(0.03, 0.028), V2(0, 0.03)}, 10);
        contactor(b, 0.0, YC + 0.06 - 0.004, -0.19, 0.1, 0.07, 0.034, V3(0.82, 0.82, 0.8), "VOLT", V3(0.62, 0.08, 0.06));
    }
    // Gearbox.
    b.style(body, PAINT);
    if (!synth) {
        cylAxis(b, V3(0, YC, 0.12), 'z', 0.085, 0.052, 14);
        cylAxis(b, V3(0, YC, 0.2), 'z', 0.014, 0.063, 14);
        cylAxis(b, V3(0, YC, 0.213), 'z', 0.05, 0.047, 14);
        b.style(kDark * 1.4, METAL);
        for (int k = 0; k < 4; k++) {
            const double a = rad(45 + 90 * k);
            hexBolt(b, V3(0.056 * std::cos(a), YC + 0.056 * std::sin(a), 0.2135), 'z', 0.0065, 0.007);
        }
        b.style(kSteel * 1.2, METAL);
        cylAxis(b, V3(0, YC, 0.262), 'z', 0.016, 0.022, 14);  // the clutch knob
        b.style(V3(0.7, 0.08, 0.06), PAINT);
        b.box(V3(0, YC + 0.022, 0.274), V3(0.012, 0.04, 0.008), 0.003);
    } else {
        latheAxis(b, V3(0, YC, 0.12), 'z', {V2(0, 0), V2(0.062, 0), V2(0.064, 0.01), V2(0.06, 0.07), V2(0.05, 0.12), V2(0.046, 0.15), V2(0, 0.152)}, 12);
        b.style(kSteel * 1.25, METAL);
        cylAxis(b, V3(0, YC, 0.268), 'z', 0.008, 0.026, 12);
        b.style(V3(0.62, 0.08, 0.06), PAINT);  // clutch ring
        cylAxis(b, V3(0, YC, 0.253), 'z', 0.016, 0.034, 12);
        b.style(kDark * 1.4, METAL);
        for (int k = 0; k < 4; k++) {
            const double a = rad(45 + 90 * k);
            hexBolt(b, V3(0.052 * std::cos(a), YC + 0.052 * std::sin(a), 0.19), 'z', 0.0065, 0.008);
        }
    }
    // Fairlead and the line out through it.
    const double yR = YC - 0.032 - (synth ? 0.0092 : 0.0082);
    const double fx = 0.12;
    if (synth) hawseFairlead(b, fx, yR);
    else rollerFairlead(b, fx, yR, body);
    const double rr = synth ? 0.0092 : 0.0082, ff = fx + 0.035;
    b.style(ropeCol(rope), ropeMat(rope));
    const double endX = hook ? 0.158 : 0.2;
    b.tube({V3(0.0, yR, zh * 0.8), V3(0.04, yR, zh * 0.5), V3(0.08, yR, 0.0), V3(0.0 + endX * 0.6, yR, 0.0), V3(endX, yR, 0.0)}, rr, 7);
    (void)ff;
    if (hook) {
        b.push();
        b.move(0.158 + 0.128 * 0.88, yR, 0);
        b.roll(90);
        b.move(0, -0.021 * 0.88, 0);
        b.scale(0.88);
        clevisHook(b, synth ? V3(0.2, 0.2, 0.21) : V3(0.14, 0.14, 0.15), synth ? V3(0.7, 0.1, 0.08) : paint(8), rope, 0.15, synth ? 1 : 0, 0.0, true, true);
        b.pop();
    } else {
        b.style(kGalv, METAL);  // bare rope end: a swaged stop
        cylAxis(b, V3(0.2, yR, 0), 'x', 0.03, 0.0105, 10);
    }
}

void bigDrumWinch(B& b, const P& p);
Reg winch({"winch", "WINCH", "RECOVERY", "Bumper-mount electric winch: pulls the truck out of trouble; a career buy", 0.6,
           {
               {"STEEL CABLE", "Black winch, steel cable, four-roller fairlead",
                {paintParam(3), choice("rope", "ROPE", 0, ropeNames()), choice("hook", "HOOK", 1, {"NO", "YES"})},
                [](B& b, const P& p) { compactWinch(b, p, false); }},
               {"SYNTHETIC ROPE", "Compact grey winch, red synthetic rope, aluminium hawse",
                {paintParam(9), choice("rope", "ROPE", 1, ropeNames()), choice("hook", "HOOK", 1, {"NO", "YES"})},
                [](B& b, const P& p) { compactWinch(b, p, true); }},
               {"BIG DRUM", "Old-school hydraulic/PTO big-drum winch on plate sides",
                {paintParam(1), choice("rope", "ROPE", 0, ropeNames()), choice("hook", "HOOK", 1, {"NO", "YES"})},
                [](B& b, const P& p) { bigDrumWinch(b, p); }},
           }});

// The old-school big-drum winch: heavy plate sides, a hydraulic motor one end, a PTO gear case the other.
void bigDrumWinch(B& b, const P& p) {
    const V3 body = paint(p.i("paint"));
    const int rope = p.i("rope");
    const bool hook = p.i("hook") != 0;
    const double YC = 0.125, zh = 0.13, core = 0.05, rr = 0.0125;
    b.move(-0.07, 0, 0);
    b.style(body * 0.7, PAINT);
    b.box(V3(0, 0.008, 0), V3(0.27, 0.016, 0.37), 0.004);
    b.style(kDark * 1.4, METAL);
    for (int s = -1; s <= 1; s += 2)
        for (int e = -1; e <= 1; e += 2) hexBolt(b, V3(e * 0.115, 0.016, s * 0.165), 'y', 0.008, 0.008);
    // Side plates with their bearing bosses.
    const std::vector<V2> plate = {V2(-0.115, 0.016), V2(0.115, 0.016), V2(0.115, 0.08), V2(0.098, 0.15), V2(0.05, 0.2), V2(-0.05, 0.2), V2(-0.098, 0.15), V2(-0.115, 0.08)};
    b.style(body, PAINT);
    b.extrude(plate, 0.016, 0.15);
    b.extrude(plate, 0.016, -0.15);
    b.style(kSteel * 1.1, METAL);
    cylAxis(b, V3(0, YC, 0.158), 'z', 0.006, 0.042, 12);
    cylAxis(b, V3(0, YC, -0.164), 'z', 0.006, 0.042, 12);
    // Tie bars.
    for (int e = -1; e <= 1; e += 2) cylAxis(b, V3(e * 0.092, 0.055, -0.17), 'z', 0.34, 0.011, 7);
    // Drum.
    woundDrum(b, YC, zh, core, rr, rope, 0.097, 0.012, body * 0.9, 10);
    // Hydraulic motor with its two hoses.
    b.style(V3(0.30, 0.31, 0.32), PAINT);
    cylAxis(b, V3(0, YC, -0.164), 'Z', 0.014, 0.056, 12);
    b.box(V3(0, YC, -0.2), V3(0.1, 0.1, 0.07), 0.014);
    b.color(V3(0.22, 0.23, 0.24));
    cylAxis(b, V3(0, YC, -0.235), 'Z', 0.036, 0.034, 12);
    b.style(kSteel * 1.2, METAL);
    for (int s = -1; s <= 1; s += 2) {
        hexBolt(b, V3(0.026, YC + 0.05 - 0.002, s * 0.0 - 0.2 + s * 0.022), 'y', 0.0115, 0.026);
    }
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    b.tube(spline({V3(0.026, YC + 0.06, -0.222), V3(0.026, YC + 0.095, -0.226), V3(0.02, YC + 0.082, -0.25), V3(0.0, YC + 0.03, -0.272), V3(-0.03, 0.06, -0.282), V3(-0.06, 0.02, -0.272)}, 2), 0.0105, 6);
    b.tube(spline({V3(0.026, YC + 0.06, -0.178), V3(0.026, YC + 0.085, -0.176), V3(0.05, YC + 0.08, -0.2), V3(0.075, 0.1, -0.24), V3(0.085, 0.05, -0.262), V3(0.085, 0.02, -0.272)}, 2), 0.0105, 6);
    // PTO gear case, clutch lever and the input shaft.
    b.style(body * 0.9, PAINT);
    cylAxis(b, V3(0, YC, 0.164), 'z', 0.06, 0.078, 14);
    b.color(body);
    cylAxis(b, V3(0, YC, 0.222), 'z', 0.012, 0.066, 14);
    b.style(kDark * 1.4, METAL);
    for (int k = 0; k < 4; k++) {
        const double a = rad(45 + 90 * k);
        hexBolt(b, V3(0.07 * std::cos(a), YC + 0.07 * std::sin(a), 0.224), 'z', 0.0075, 0.008);
    }
    b.style(kSteel * 1.15, METAL);
    cylAxis(b, V3(0, YC, 0.234), 'z', 0.012, 0.03, 12);
    cylAxis(b, V3(0, YC, 0.244), 'z', 0.036, 0.0175, 8);
    b.style(kDark * 1.6, METAL);
    b.box(V3(0.0, YC + 0.0175, 0.262), V3(0.008, 0.008, 0.016));
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    b.beam(V3(0.0, YC + 0.075, 0.19), V3(-0.045, YC + 0.14, 0.19), 0.012, 0.012, V3(0, 0, 1));
    b.style(V3(0.7, 0.08, 0.06), PAINT);
    b.sphere(V3(-0.045, YC + 0.145, 0.19), V3(0.016, 0.016, 0.016), 0);
    // Heavy fairlead: two bollard rollers between a base and a cap plate.
    const double yR = YC - core - rr, fx = 0.17;
    b.style(body * 0.8, PAINT);
    b.box(V3(fx, 0.008, 0), V3(0.1, 0.016, 0.22));
    b.box(V3(fx, yR + 0.052, 0), V3(0.1, 0.014, 0.22));
    b.style(kSteel * 1.25, METAL);
    for (int s = -1; s <= 1; s += 2) cylAxis(b, V3(fx, 0.016, s * 0.06), 'y', yR + 0.047 - 0.016, 0.022, 10);
    b.style(kDark * 1.4, METAL);
    for (int s = -1; s <= 1; s += 2) hexBolt(b, V3(fx, yR + 0.059, s * 0.095), 'y', 0.0075, 0.006);
    const double endX = hook ? fx + 0.05 : fx + 0.12;
    b.style(ropeCol(rope), ropeMat(rope));
    b.tube({V3(0.0, yR, zh * 0.75), V3(0.05, yR, zh * 0.5), V3(0.1, yR, 0.0), V3(fx, yR, 0.0), V3(endX, yR, 0.0)}, rr * 0.8, 7);
    if (hook) {
        const double sc = 1.1;
        b.push();
        b.move(endX + 0.128 * sc, yR, 0);
        b.roll(90);
        b.move(0, -0.021 * sc, 0);
        b.scale(sc);
        clevisHook(b, V3(0.14, 0.14, 0.15), paint(8), rope, 0.15, 0, 0.0, true, true);
        b.pop();
    } else {
        b.style(kGalv, METAL);
        cylAxis(b, V3(endX, yR, 0), 'x', 0.035, 0.012, 10);
    }
}

// ---- 4. Shackle -------------------------------------------------------------------------------

// A bow shackle lying flat: the bow behind (-x), the screw pin across the ears at +x.
void bowShackle(B& b, const V3& col, const V3& pinCol, double wear, bool stamp, double s = 1.0, bool lo = false) {
    const double r = 0.0095, yc = 0.0105, ex = 0.04, ez = 0.0225;
    b.push();
    b.scale(s);
    std::vector<V3> half = {V3(ex, yc, ez), V3(0.02, yc, 0.0235), V3(-0.005, yc, 0.03), V3(-0.03, yc, 0.0375), V3(-0.055, yc, 0.034), V3(-0.072, yc, 0.02), V3(-0.077, yc, 0)};
    std::vector<V3> path = half;
    for (int i = (int)half.size() - 2; i >= 0; i--) path.push_back(V3(half[i].x, yc, -half[i].z));
    b.style(col, PAINT);
    b.tube(lo ? path : spline(path, 2), r, lo ? 6 : 8, false, false);
    // Ear bosses, pin, head.
    cylAxis(b, V3(ex, yc, ez - 0.006), 'z', 0.012, 0.0165, lo ? 8 : 14);
    cylAxis(b, V3(ex, yc, -ez - 0.006), 'z', 0.012, 0.0165, lo ? 8 : 14);
    b.style(pinCol, PAINT);
    cylAxis(b, V3(ex, yc, -0.0292), 'z', 0.0577, 0.0105, lo ? 6 : 10);
    cylAxis(b, V3(ex, yc, 0.0285), 'z', 0.013, 0.0155, lo ? 8 : 12);
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    b.box(V3(ex, yc, 0.0418), V3(0.0022, 0.016, 0.002));  // the eye in the pin head
    b.box(V3(ex, yc, 0.0418), V3(0.0022, 0.002, 0.016));
    if (wear > 0.05) {
        b.style(kRust, MATTE);
        for (int i = 0; i < (int)(wear * 9); i++) {
            const V3 q = path[1 + (int)(hash01(11, i) * (path.size() - 2))];
            b.box(V3(q.x + (hash01(12, i) - 0.5) * 0.004, yc + r + 0.0002, q.z), V3(0.005, 0.0008, 0.004));
        }
    }
    if (stamp) {  // a stamped rating plate on the back of the bow
        b.push();
        b.yaw(180);
        b.style(kGalv, METAL);
        b.box(V3(0.0857, yc, 0), V3(0.002, 0.014, 0.022));
        b.style(V3(0.08, 0.08, 0.08), MATTE);
        b.letters("3T", V3(0.0868, yc, 0), 0.0013, 0.0007);
        b.pop();
    }
    b.pop();
}

void softShackleAlone(B& b, const V3& colIn, double wear) {
    const double r = 0.0058, y = 0.0058;
    const V3 col = mix(colIn, kDirt, wear * 0.35);
    b.style(col, MATTE);
    // The body: a long loop of braided line; the knot at the +x end.
    std::vector<V3> lp = stadium(V3(-0.01, y, 0), V3(1, 0, 0), V3(0, 0, 1), 0.12, 0.034, 6);
    b.tube(lp, r, 6, true);
    // A second turn of the same line, laid on top at a slant (the braid's doubled strands).
    b.color(col * 0.82);
    b.tube(stadium(V3(-0.01, y + 0.0105, 0), V3(1, 0, 0), V3(0, 0, 1), 0.108, 0.024, 6), r * 0.85, 6, true);
    // The diamond knot.
    b.color(col * 0.7);
    b.blob(V3(0.052, 0.011, 0), V3(0.0165, 0.0125, 0.0125), 0.07, 5, 1, 0.3);
    b.color(col * 1.1);
    b.push();
    b.move(0.04, 0.0105, 0);
    b.pitch(90);
    b.torus(V3(), 0.0105, 0.0035, 12, 5);  // the loop that closes over the knot
    b.pop();
}

void recoveryRing(B& b, const V3& colIn, double wear) {
    const V3 col = colIn * (1 - 0.25 * wear);
    const double rin = 0.034, rout = 0.058, h = 0.03, bv = 0.007;
    b.style(col, PAINT);
    b.lathe(V3(0, 0, 0), {V2(rin + bv, 0), V2(rout - bv, 0), V2(rout, bv), V2(rout, h - bv), V2(rout - bv, h), V2(rin + bv, h), V2(rin, h - bv), V2(rin, bv), V2(rin + bv, 0)}, 28, true);
    // A groove round the outside, and a rating plate.
    b.color(col * 0.55);
    b.lathe(V3(0, 0, 0), {V2(rout + 0.0008, 0.011), V2(rout + 0.0008, 0.019)}, 26, false);
    b.lathe(V3(0, 0, 0), {V2(rin - 0.0008, 0.011), V2(rin - 0.0008, 0.019)}, 26, false);
    b.style(kGalv, METAL);
    b.box(V3(rout + 0.0012, h / 2, 0), V3(0.003, 0.02, 0.034), 0.0006);
    b.style(V3(0.07, 0.07, 0.07), MATTE);
    b.letters("9T", V3(rout + 0.0028, h / 2, 0), 0.0017, 0.0008);
}

Reg shackle({"shackle", "SHACKLE", "RECOVERY", "Connects strap, winch line and anchor: carried in the recovery kit; a career find", 0.16,
             {
                 {"BOW SHACKLE", "Painted forged bow shackle with a screw pin",
                  {paintParam(0), choice("pin", "PIN", 1, {"GALVANISED", "MATCHING", "BLACK"}), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                  [](B& b, const P& p) {
                      const V3 c = paint(p.i("paint"));
                      const int pn = p.i("pin");
                      bowShackle(b, c, pn == 0 ? kGalv : pn == 1 ? c * 0.9 : V3(0.08, 0.08, 0.085), p("wear"), true, 0.85);
                  }},
                 {"SOFT SHACKLE", "A knotted loop of braided synthetic line",
                  {paintParam(4), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                  [](B& b, const P& p) { softShackleAlone(b, paint(p.i("paint")), p("wear")); }},
                 {"RECOVERY RING", "A flat machined snatch ring, anodised",
                  {paintParam(0), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                  [](B& b, const P& p) { recoveryRing(b, paint(p.i("paint")), p("wear")); }},
             }});

// ---- 5. Tow strap -----------------------------------------------------------------------------

// A flat strap rolled up like a pancake (a spiral), held by a hook-and-loop band, a sewn eye trailing.
void rolledStrap(B& b, const V3& col, double wear) {
    const double r0 = 0.03, pitch = 0.0125, th = 0.0095, H = 0.055;
    const int turns = 5, ppt = 12, N = turns * ppt;
    const double tEnd = -kPi / 2, tStart = tEnd - 2 * kPi * turns;
    std::vector<V2> outer, inner;
    for (int i = 0; i <= N; i++) {
        const double t = tStart + (tEnd - tStart) * i / N, rr = r0 + pitch * (t - tStart) / (2 * kPi);
        inner.push_back(V2(rr * std::cos(t), rr * std::sin(t)));
        outer.push_back(V2((rr + th) * std::cos(t), (rr + th) * std::sin(t)));
    }
    std::vector<V2> poly = outer;
    for (int i = N; i >= 0; i--) poly.push_back(inner[i]);
    const V3 c = mix(col, kDirt, wear * 0.3);
    b.style(c, MATTE);
    b.extrudeUp(poly, H, 0);
    const double Ro = r0 + pitch * turns + th, rc = Ro - th / 2;
    // The tail and its sewn eye (a stadium of strap).
    std::vector<V2> tail = {V2(0.0, -rc), V2(0.08, -rc), V2(0.14, -rc)};
    const double ew = 0.05, ecx = 0.14 + 0.0, ecz = -rc + ew / 2;
    for (int i = 1; i <= 8; i++) {
        const double a = rad(-90 + 180.0 * i / 8);
        tail.push_back(V2(ecx + 0.05 + (ew / 2) * std::cos(a) * 1.0, ecz + (ew / 2) * std::sin(a)));
    }
    // (straight run out, a rounded end, back to the stitching)
    tail.insert(tail.begin() + 3, V2(0.19, -rc));
    tail.push_back(V2(0.14, -rc + ew));
    tail.push_back(V2(0.1, -rc + ew));
    b.extrudeUp(ribbonPoly(tail, th, false, false), H, 0);
    b.color(c * 0.7);  // the bar-tack stitching across the eye
    b.box(V3(0.115, H / 2, -rc + ew / 2), V3(0.03, H * 0.8, ew + th + 0.002));
    b.color(c * 0.82);
    b.box(V3(0.1, H / 2, -rc + ew), V3(0.014, H + 0.002, th + 0.002));
    // The hook-and-loop keeper band, a loop tab and a label.
    b.style(V3(0.1, 0.1, 0.105), MATTE);
    b.lathe(V3(0, 0, 0), ringProfile(Ro - 0.002, Ro + 0.003, 0.014, 0.041, 0.001), 28, false);
    b.color(V3(0.18, 0.18, 0.19));
    b.box(V3(-0.02, 0.0275, Ro + 0.004), V3(0.05, 0.026, 0.004), 0.001);
    b.style(V3(0.9, 0.9, 0.86), MATTE);
    b.box(V3(Ro + 0.0045, 0.0275, 0.0), V3(0.003, 0.024, 0.046));
    b.style(V3(0.08, 0.08, 0.08), MATTE);
    b.letters("8T", V3(Ro + 0.006, 0.0275, 0.0), 0.0022, 0.0008);
}

// Kinetic rope coiled in a figure of eight, tied at the waist, a protected eye at each end.
void kineticCoil(B& b, const V3& col, double wear) {
    const double r = 0.0115;
    const V3 c = mix(col, kDirt, wear * 0.3);
    b.style(c, MATTE);
    for (int k = 0; k < 3; k++) {
        std::vector<V3> path;
        const int n = 26;
        const double A = 0.215 - 0.004 * k, Bz = 0.2 - 0.01 * k, y0 = r + 0.003 + k * 0.0235;
        for (int i = 0; i < n; i++) {
            const double t = 2 * kPi * i / n, sq = std::sin(t / 2);
            path.push_back(V3(A * std::sin(t), y0 + 0.0235 * sq * sq, Bz * std::sin(t) * std::cos(t)));
        }
        b.color(c * (1.0 - 0.06 * k));
        b.push();
        b.yaw(2.5 * k);
        b.tube(path, r, 6, true);
        b.pop();
    }
    // The tie at the waist.
    b.style(V3(0.07, 0.07, 0.075), MATTE);
    b.tube(arcPath(V3(0, 0.043, 0), V3(0, 1, 0), V3(0, 0, 1), 0.052, 0.034, 0, 360, 9), 0.0045, 5, true);
    // The two eyes, each with a black protector sleeve.
    for (int s = -1; s <= 1; s += 2) {
        b.push();
        b.move(0.2, 0, s * 0.036);
        b.yaw(s * 10);
        b.style(c, MATTE);
        b.tube(stadium(V3(0.075, r, 0), V3(1, 0, 0), V3(0, 0, 1), 0.15, 0.052, 6), r * 0.92, 6, true);
        b.style(V3(0.08, 0.08, 0.085), MATTE);
        cylAxis(b, V3(0.0, r, 0), 'x', 0.05, 0.0175, 10);
        b.pop();
    }
}

// A zipped duffel for the kit: rounded body, two handles, a front pocket with a reflective strip.
void recoveryBag(B& b, const V3& col, double wear) {
    const V3 c = mix(col, kDirt, wear * 0.25);
    b.style(c * 0.45, MATTE);
    b.box(V3(0, 0.011, 0), V3(0.17, 0.022, 0.4), 0.008);  // rubber base
    b.style(c, MATTE);
    b.box(V3(0, 0.1365, 0), V3(0.25, 0.227, 0.45), 0.065);
    // End panels with their own patch pockets.
    b.color(c * 0.8);
    for (int e = -1; e <= 1; e += 2) b.box(V3(0, 0.1365, e * 0.2265), V3(0.108, 0.088, 0.004));
    // The zip along the top and its pull.
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    b.box(V3(0, 0.2535, 0), V3(0.014, 0.0026, 0.4));
    b.style(kGalv, METAL);
    b.box(V3(0, 0.256, 0.17), V3(0.012, 0.004, 0.02), 0.001);
    b.tube({V3(0, 0.258, 0.18), V3(0, 0.272, 0.19), V3(0, 0.266, 0.205), V3(0, 0.256, 0.2)}, 0.0016, 4);
    // Handles over the top.
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    for (int s = -1; s <= 1; s += 2)
        b.tube(spline({V3(-0.075, 0.249, s * 0.09), V3(-0.065, 0.285, s * 0.09), V3(0, 0.3, s * 0.09), V3(0.065, 0.285, s * 0.09), V3(0.075, 0.249, s * 0.09)}, 3), 0.0075, 6);
    // Front pocket, reflective strip and a label patch.
    b.style(c * 0.9, MATTE);
    b.box(V3(0.1255, 0.12, 0), V3(0.012, 0.1, 0.34), 0.003);
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    b.box(V3(0.1325, 0.168, 0), V3(0.002, 0.0026, 0.32));  // pocket zip
    b.style(V3(0.78, 0.8, 0.72), EMISSIVE);
    b.box(V3(0.1325, 0.092, 0), V3(0.003, 0.026, 0.33));
    b.style(paint(8) * 0.95, MATTE);
    b.box(V3(0.1325, 0.133, 0.0), V3(0.003, 0.03, 0.082));
    b.style(V3(0.07, 0.07, 0.07), MATTE);
    b.letters("KIT", V3(0.1345, 0.133, 0.0), 0.0033, 0.0008);
}

Reg towStrap({"tow_strap", "TOW STRAP", "RECOVERY", "A recovery strap or kinetic rope as carried: in the truck's kit, used to pull another vehicle out; a career find", 0.45,
              {
                  {"ROLLED STRAP", "Flat strap rolled up with a hook-and-loop band and a sewn eye", {paintParam(8), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                   [](B& b, const P& p) { rolledStrap(b, paint(p.i("paint")), p("wear")); }},
                  {"KINETIC ROPE", "Stretchy rope coiled in a figure of eight, two protected eyes", {paintParam(0), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                   [](B& b, const P& p) { kineticCoil(b, paint(p.i("paint")), p("wear")); }},
                  {"RECOVERY BAG", "A zipped duffel with handles and a reflective strip", {paintParam(1), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                   [](B& b, const P& p) { recoveryBag(b, paint(p.i("paint")), p("wear")); }},
              }});

// ---- 6. Tree saver ----------------------------------------------------------------------------

struct P2 {
    double x, z;
};

// A vertical band standing on a plan-view path (x, z): the wall of a strap, sleeve or loop.
// Counter-clockwise paths (seen from above, x to z) have the outer face on the right of travel.
void bandLoop(B& b, const std::vector<P2>& p, bool closed, double thick, double y0, double y1) {
    const int n = (int)p.size();
    std::vector<P2> o(n), in(n), nr(n);
    for (int i = 0; i < n; i++) {
        const P2 &a = p[closed ? (i + n - 1) % n : std::max(i - 1, 0)], &c = p[closed ? (i + 1) % n : std::min(i + 1, n - 1)];
        double tx = c.x - a.x, tz = c.z - a.z;
        const double l = std::hypot(tx, tz);
        tx /= l, tz /= l;
        nr[i] = {tz, -tx};
        o[i] = {p[i].x + nr[i].x * thick / 2, p[i].z + nr[i].z * thick / 2};
        in[i] = {p[i].x - nr[i].x * thick / 2, p[i].z - nr[i].z * thick / 2};
    }
    auto V = [](const P2& q, double y) { return V3(q.x, y, q.z); };
    const int m = closed ? n : n - 1;
    for (int i = 0; i < m; i++) {
        const int j = (i + 1) % n;
        const V3 n0(nr[i].x, 0, nr[i].z), n1(nr[j].x, 0, nr[j].z);
        b.triN(V(o[i], y0), V(o[j], y0), V(o[j], y1), n0, n1, n1), b.triN(V(o[i], y0), V(o[j], y1), V(o[i], y1), n0, n1, n0);
        b.triN(V(in[j], y0), V(in[i], y0), V(in[i], y1), -n1, -n0, -n0), b.triN(V(in[j], y0), V(in[i], y1), V(in[j], y1), -n1, -n0, -n1);
        b.quad(V(o[i], y1), V(o[j], y1), V(in[j], y1), V(in[i], y1));
        b.quad(V(in[i], y0), V(in[j], y0), V(o[j], y0), V(o[i], y0));
    }
    if (!closed)
        for (int e = 0; e < 2; e++) {
            const int i = e ? n - 1 : 0;
            b.quad(V(o[i], y0), V(in[i], y0), V(in[i], y1), V(o[i], y1));
        }
}
std::vector<P2> arcPlan(double R, double a0, double a1, int n) {
    std::vector<P2> p;
    for (int i = 0; i <= n; i++) {
        const double a = rad(a0 + (a1 - a0) * i / n);
        p.push_back({R * std::cos(a), R * std::sin(a)});
    }
    return p;
}
std::vector<P2> stadiumPlan(double cx, double cz, double len, double wid, int n = 5) {  // counter-clockwise
    std::vector<P2> p;
    const double L = (len - wid) / 2, r = wid / 2;
    for (int i = 0; i <= n; i++) {
        const double a = rad(-90 + 180.0 * i / n);
        p.push_back({cx + L + r * std::cos(a), cz + r * std::sin(a)});
    }
    for (int i = 0; i <= n; i++) {
        const double a = rad(90 + 180.0 * i / n);
        p.push_back({cx - L + r * std::cos(a), cz + r * std::sin(a)});
    }
    return p;
}

const double kTreeY = 0.42;

// Where the strap leaves the trunk: the tangent from the meeting point.
double leaveAngle(double Rc, double xm) { return std::acos(std::min(0.98, Rc / xm)) * 180 / kPi; }

// Webbing strap (optionally over a padded sleeve) with sewn eyes, joined by a shackle or a ring.
// (`outline`: not round a trunk but along a path in the model's own frame, x out of the thing at the shackle, z across, y 0: a
// boulder's cross-section, mk::pathHint. The two ends of the path are the strap's tails.)
void webTreeSaver(B& b, const V3& col, double R, bool pad, int join, const V3& joinCol, double wear, const std::vector<V3>* outline = nullptr) {
    const double t = 0.007, h = 0.045, yc = outline ? 0.0 : kTreeY;
    const V3 c = mix(col, kDirt, wear * 0.3);
    double Rin = R + 0.002;
    if (pad && !outline) {
        const double ph = 0.16, pt = 0.014;
        b.style(mix(V3(0.20, 0.21, 0.19), kDirt, wear * 0.3), MATTE);
        std::vector<P2> circ = arcPlan(Rin + pt / 2, 0, 360, 28);
        circ.pop_back();
        bandLoop(b, circ, true, pt, yc - ph / 2, yc + ph / 2);
        b.color(V3(0.07, 0.07, 0.07));  // stitched edges
        for (int e = -1; e <= 1; e += 2) {
            std::vector<P2> sc = arcPlan(Rin + pt / 2, 0, 360, 28);
            sc.pop_back();
            bandLoop(b, sc, true, pt + 0.004, yc + e * (ph / 2 - 0.012), yc + e * (ph / 2 - 0.008));
        }
        b.color(V3(0.3, 0.3, 0.28));  // the hook-and-loop seam at the back
        b.box(V3(-(Rin + pt + 0.001), yc, 0), V3(0.004, ph * 0.92, 0.07));
        Rin += pt + 0.001;
    }
    const double Rc = Rin + t / 2, xm = outline ? 0.17 : Rc + 0.17;
    const double phi = leaveAngle(Rc, xm);
    std::vector<P2> plan;
    if (outline) {
        for (const V3& q : *outline) plan.push_back({q.x, q.z});
    } else {
        plan = arcPlan(Rc, phi, 360 - phi, 24);
    }
    b.style(c, MATTE);
    bandLoop(b, plan, false, t, yc - h / 2, yc + h / 2);
    b.color(c * 0.6);
    for (int e = -1; e <= 1; e += 2) bandLoop(b, plan, false, t + 0.004, yc + e * (h / 2 - 0.0125), yc + e * (h / 2 - 0.0085));
    const double ex = xm - 0.035;
    for (int s = -1; s <= 1; s += 2) {
        const P2 tp = s > 0 ? plan.front() : plan.back();
        const V3 T(tp.x, yc, tp.z), E(ex, yc, s * 0.012);
        b.color(c);
        b.beam(T, E, t, h, V3(0, 1, 0));
        b.color(c * 0.6);
        for (int e = -1; e <= 1; e += 2) b.beam(T, E, t + 0.004, 0.004, V3(0, 1, 0));
    }
    // The two sewn eyes (loops of the same strap), each with its stitched patch.
    for (int s = -1; s <= 1; s += 2) {
        b.color(c * (s > 0 ? 1.0 : 0.93));
        bandLoop(b, stadiumPlan(xm + (s > 0 ? 0.0 : 0.008), s * 0.005, 0.1, 0.046), true, t, yc - h / 2 + (s > 0 ? 0.0 : 0.0015), yc + h / 2 - (s > 0 ? 0.0 : 0.0015));
        b.color(V3(0.07, 0.07, 0.07));
        b.box(V3(xm - 0.04 + (s > 0 ? 0 : 0.008), yc + (s > 0 ? 0 : 0), s * 0.005 + (s > 0 ? 0.0235 : -0.0235)), V3(0.026, h * 0.55, t + 0.003));
    }
    // The join: a bow shackle with a vertical pin, or a ring.
    if (join == 0) {
        b.push();
        b.move(xm + 0.022, yc, 0);
        b.yaw(180);
        b.roll(90);
        b.scale(1.35);
        b.move(-0.04, -0.0105, 0);
        bowShackle(b, joinCol, kGalv, wear, false, 1.0, true);
        b.pop();
    } else {
        b.style(joinCol, PAINT);
        ringAxis(b, V3(xm + 0.045, yc, 0), 'z', 0.045, 0.0105, 20, 7);
    }
}

// A round sling wrapped twice and choked, with a soft shackle through its two eyes.
void roundSlingSaver(B& b, const V3& col, double R, const V3& loopCol, double wear) {
    const double r = 0.0155, yc = kTreeY, ya = yc - 0.016, yb = yc + 0.016;
    const V3 c = mix(col, kDirt, wear * 0.3);
    const double Rc = R + 0.002 + r, xm = Rc + 0.17;
    const double phi = leaveAngle(Rc, xm);
    std::vector<V3> path;
    path.push_back(V3(xm - 0.03, ya, 0.0));
    const double a1 = 720 - phi, n = std::ceil((a1 - phi) / 20);
    for (int i = 0; i <= n; i++) {
        const double a = rad(phi + (a1 - phi) * i / n), k = (double)i / n;
        path.push_back(V3(Rc * std::cos(a), ya + (yb - ya) * k, Rc * std::sin(a)));
    }
    path.push_back(V3(xm - 0.03, yb, 0.0));
    b.style(c, MATTE);
    b.tube(path, r, 6);
    b.color(c * 0.8);
    for (int s = 0; s < 2; s++) b.tube(stadium(V3(xm, s ? yb : ya, 0), V3(1, 0, 0), V3(0, 0, 1), 0.1, 0.05, 5), 0.0105, 6, true);
    b.style(V3(0.08, 0.08, 0.09), MATTE);  // the label sleeve
    cylAxis(b, V3(xm - 0.03, ya, 0), 'x', 0.02, 0.0185, 8);
    // The soft shackle standing through both eyes, knot on top.
    b.style(loopCol, MATTE);
    b.tube(stadium(V3(xm, yc, 0), V3(0, 1, 0), V3(1, 0, 0), 0.13, 0.034, 6), 0.0058, 6, true);
    b.color(loopCol * 0.75);
    b.blob(V3(xm, yc + 0.07, 0), V3(0.016, 0.014, 0.016), 0.07, 9, 1, 0.3);
}

Reg treeSaver({"tree_saver", "TREE SAVER", "RECOVERY", "Trunk-protector strap drawn round an anchor tree: +X is the pull direction; the eyes meet at its shackle", 0.8,
               {
                   {"WEBBING STRAP", "Flat webbing round the trunk, two sewn eyes, a bow shackle",
                    {paintParam(8), paintParam(0, "shackle", "SHACKLE"), num("radius", "TRUNK RADIUS", 0.2, 0.1, 0.4, 0.01), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) { webTreeSaver(b, paint(p.i("paint")), p("radius"), false, 0, paint(p.i("shackle")), p("wear")); }},
                   {"ROUND SLING", "Soft round sling wrapped twice, a soft shackle through the eyes",
                    {paintParam(4), paintParam(8, "loop", "SOFT SHACKLE"), num("radius", "TRUNK RADIUS", 0.2, 0.1, 0.4, 0.01), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) { roundSlingSaver(b, paint(p.i("paint")), p("radius"), paint(p.i("loop")), p("wear")); }},
                   {"PADDED WRAP", "A padded bark sleeve under the strap, a ring joining the eyes",
                    {paintParam(0), paintParam(4, "ring", "RING"), num("radius", "TRUNK RADIUS", 0.2, 0.1, 0.4, 0.01), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                    [](B& b, const P& p) { webTreeSaver(b, paint(p.i("paint")), p("radius"), true, 1, paint(p.i("ring")), p("wear")); }},
               }});

// ---- 7. Ground anchor -------------------------------------------------------------------------

// Gear out of the ground (in the back of a truck, in your hands: Placed::bare): none of the soil, snow,
// ice or drill scar it's shown set in.
thread_local bool tLoose = false;
struct Loose {
    explicit Loose(const P& p) { tLoose = p("bare") > 0; }
    ~Loose() { tLoose = false; }
};

V3 soilCol(int i) {
    static const V3 c[] = {V3(0.38, 0.30, 0.21), V3(0.24, 0.19, 0.14), V3(0.55, 0.46, 0.32)};
    return c[std::clamp(i, 0, 2)];
}
void soilMound(B& b, const V3& c, const V3& radii, uint32_t seed, const V3& soil, int detail = 1) {
    if (tLoose) return;
    b.style(soil * (0.88 + 0.24 * hash01(seed, 4)), MATTE);
    b.blob(c, radii, 0.22, seed, detail, 0.5, 0.0);
}

// Chain laid along a polyline: links every `pitch`, alternately flat and on edge.
void chainRun(B& b, const std::vector<V3>& poly, double pitch, double wear, uint32_t seed) {
    std::vector<double> cum(poly.size(), 0.0);
    for (size_t i = 1; i < poly.size(); i++) cum[i] = cum[i - 1] + len(poly[i] - poly[i - 1]);
    int k = 0;
    for (double s = pitch * 0.5; s < cum.back() - pitch * 0.2; s += pitch, k++) {
        size_t i = 1;
        while (i + 1 < poly.size() && cum[i] < s) i++;
        const double f = (s - cum[i - 1]) / std::max(1e-9, cum[i] - cum[i - 1]);
        const V3 pt = poly[i - 1] + (poly[i] - poly[i - 1]) * f, d = norm(poly[i] - poly[i - 1]);
        b.push();
        b.move(pt);
        b.yaw(std::atan2(-d.z, d.x) * 180 / kPi);
        b.pitch(std::asin(std::clamp(d.y, -1.0, 1.0)) * 180 / kPi);
        b.roll((k & 1) ? 90 : 0);
        b.style(mix(kSteel * 0.62, kRust, wear * hash01(seed, k)), METAL);
        b.tube(stadium(V3(), V3(1, 0, 0), V3(0, 1, 0), 0.066, 0.036, 2), 0.0078, 4, true);
        b.pop();
    }
}

// Two steel stakes driven in, a chain bridle from their eyes to a master ring, a tail and a shackle.
void stakePair(B& b, int finish, double lean, double wear, const V3& soil) {
    static const V3 fin[] = {V3(0.42, 0.43, 0.44), V3(0.62, 0.10, 0.07), V3(0.90, 0.68, 0.06)};
    const V3 sc = mix(fin[std::clamp(finish, 0, 2)], kRust, wear * 0.35);
    const double a = rad(lean);
    const V3 d(-std::sin(a), std::cos(a), 0);
    std::vector<V3> eyes;
    for (int s = -1; s <= 1; s += 2) {
        const V3 base(0, 0, s * 0.17);
        const V3 p0 = base - d * 0.16, p1 = base + d * 0.33;
        b.style(sc, finish ? PAINT : METAL);
        b.cyl(p0, p1, 0.0145, 8);
        b.cone(p0 - d * 0.04, p0, 0.0, 0.0145, 8);
        b.style(kSteel * 0.75, METAL);  // the mushroomed driving head
        b.cone(p1, p1 + d * 0.012, 0.0145, 0.021, 10);
        b.cyl(p1 + d * 0.012, p1 + d * 0.026, 0.021, 10);
        // The welded eye.
        const V3 e = base + d * 0.25 + V3(0.024, 0, 0);
        b.style(kSteel * 0.8, METAL);
        ringAxis(b, e, 'z', 0.026, 0.0065, 12, 5);
        b.box(base + d * 0.25 + V3(0.008, 0, 0), V3(0.02, 0.012, 0.014));
        eyes.push_back(e + V3(0.026 * 0.9, 0, 0));
        soilMound(b, base + V3(0.01, 0.0, 0), V3(0.07, 0.03, 0.07), 70 + s + 1, soil);
    }
    // Master ring on the ground ahead, the bridle legs, the tail and the shackle.
    const V3 mr(0.36, 0.0075, 0);
    b.style(kSteel * 0.8, METAL);
    ringAxis(b, mr, 'y', 0.036, 0.0075, 14, 5);
    for (int s = 0; s < 2; s++) {
        std::vector<V3> leg;
        const V3 e0 = eyes[s], e1 = mr + V3(-0.03, 0, (s ? 1 : -1) * 0.014);
        for (int i = 0; i <= 14; i++) {
            const double t = i / 14.0, w = std::pow(1 - t, 2.2);
            leg.push_back(V3(e0.x + (e1.x - e0.x) * t, e1.y + (e0.y - e1.y) * w, e0.z + (e1.z - e0.z) * t));
        }
        chainRun(b, leg, 0.052, wear, 200 + s * 50);
    }
    std::vector<V3> tail = {V3(mr.x + 0.034, 0.0075, 0), V3(mr.x + 0.34, 0.0075, 0)};
    chainRun(b, tail, 0.052, wear, 400);
    b.push();
    b.move(mr.x + 0.34 + 0.04 * 0.85 - 0.02, 0, 0);
    b.yaw(180);
    bowShackle(b, paint(0), kGalv, wear, false, 0.85, true);
    b.pop();
}

// A folding plough anchor: a V-bladed spade dug in at its nose, an A-frame up to the pull eye.
void ploughAnchor(B& b, const V3& col, double wear, const V3& soil) {
    const V3 steel = mix(V3(0.36, 0.37, 0.38), kRust, wear * 0.5);
    const double L = 0.5, W = 0.27;
    // The spade: two wings folded about a ridge, a pointed nose buried in the ground.
    b.push();
    b.move(-0.3, -0.12, 0);
    b.pitch(32);
    b.style(steel, METAL);
    b.both([&] {
        b.push();
        b.roll(-16);
        b.extrudeUp({V2(0, 0), V2(L, 0), V2(L, W), V2(0.22, W), V2(0.0, 0.03)}, 0.014, -0.007);
        b.color(steel * 0.75);  // a stiffening rib along each wing
        b.extrudeUp({V2(0.12, 0.1), V2(L - 0.02, 0.1), V2(L - 0.02, 0.114), V2(0.12, 0.108)}, 0.03, 0.0);
        b.color(steel);
        b.pop();
    });
    b.pop();
    // The front angle iron, the hinge knuckles and pins, the A-frame arms.
    const V3 apex(0.4, 0.36, 0);
    for (int s = -1; s <= 1; s += 2) {
        const V3 corner(0.085, 0.208, s * 0.25);
        b.style(col, PAINT);
        b.beam(V3(0.122, 0.147, 0), corner, 0.036, 0.03, V3(0, 1, 0), 0.005);
        b.style(kSteel * 0.9, METAL);
        cylAxis(b, corner + V3(0, 0, -0.025), 'z', 0.05, 0.02, 10);
        b.style(kDark * 1.4, METAL);
        cylAxis(b, corner + V3(0, 0, -0.035), 'z', 0.07, 0.0075, 6);
        b.style(col, PAINT);
        b.beam(corner + V3(0.02, 0.01, 0), apex + V3(-0.02, -0.02, s * 0.02), 0.034, 0.034, V3(0, 0, 1), 0.005);
    }
    b.beam(V3(0.235, 0.285, 0.14), V3(0.235, 0.285, -0.14), 0.024, 0.024, V3(0, 1, 0), 0.004);
    b.extrude({V2(0.34, 0.3), V2(0.45, 0.33), V2(0.45, 0.4), V2(0.37, 0.42), V2(0.33, 0.37)}, 0.024, 0.0);  // gusset at the apex
    b.style(kSteel * 0.95, METAL);
    ringAxis(b, V3(0.45, 0.375, 0), 'z', 0.034, 0.0085, 14, 6);
    // Spoil heaped round the nose where the blade goes in.
    soilMound(b, V3(-0.22, 0.0, 0.0), V3(0.2, 0.06, 0.26), 90, soil, 2);
    soilMound(b, V3(-0.08, 0.0, 0.22), V3(0.1, 0.035, 0.09), 91, soil);
    soilMound(b, V3(-0.08, 0.0, -0.22), V3(0.1, 0.03, 0.08), 92, soil);
}

// A spare tyre buried on edge as a deadman: the top of the tread, the heaped soil, a strap up the trench.
void deadmanTyre(B& b, int soilIdx, int sizeIdx, const V3& strapCol, double wear) {
    const V3 soil = soilCol(soilIdx);
    const double k = sizeIdx ? 1.0 : 0.85, R = 0.36 * k, yc = 0.1 - R;
    // The top of the tyre: an arc of a fat torus round the X axis (scaled flat across the tread).
    b.style(V3(0.07, 0.07, 0.075), MATTE);
    b.push();
    b.move(0, yc, 0);
    b.pitch(-90);
    b.yaw(122);
    b.scale(V3(1, 1.45, 1));
    b.torus(V3(), R - 0.075 * k, 0.075 * k, 14, 8, 116);
    b.pop();
    // Tread blocks along the exposed top.
    b.color(V3(0.09, 0.09, 0.095));
    for (int i = -3; i <= 3; i++) {
        const double aa = i * 8.0;
        b.push();
        b.move(0, yc + (R + 0.002) * std::cos(rad(aa)), (R + 0.002) * std::sin(rad(aa)));
        b.roll(-aa);
        b.box(V3(0, 0, 0), V3(0.15, 0.012, 0.03));
        b.pop();
    }
    // Heaped soil: the filled trench behind and beside the tyre, the berms beside the strap slot.
    soilMound(b, V3(-0.2, 0.0, 0.0), V3(0.16, 0.06, 0.4 * k), 11, soil);
    soilMound(b, V3(0.0, 0.0, 0.3 * k), V3(0.2, 0.06, 0.17), 12, soil);
    soilMound(b, V3(0.0, 0.0, -0.3 * k), V3(0.2, 0.055, 0.17), 13, soil);
    soilMound(b, V3(0.2, 0.0, 0.1), V3(0.2, 0.03, 0.065), 14, soil);
    soilMound(b, V3(0.2, 0.0, -0.1), V3(0.2, 0.028, 0.065), 15, soil);
    // The strap up out of the trench, along the ground, round a shackle pin.
    const V3 sc = mix(strapCol, kDirt, wear * 0.35);
    b.style(sc, MATTE);
    b.beam(V3(0.0, 0.052, 0), V3(0.24, 0.006, 0), 0.05, 0.007, V3(0, 1, 0));
    b.beam(V3(0.24, 0.0045, 0), V3(0.58, 0.0045, 0), 0.05, 0.007, V3(0, 1, 0));
    b.color(sc * 0.62);
    b.box(V3(0.49, 0.0095, 0), V3(0.05, 0.002, 0.052));  // stitched patch
    b.color(sc);
    cylAxis(b, V3(0.592, 0.0125, -0.025), 'z', 0.05, 0.0125, 10);
    b.push();
    b.move(0.592 + 0.04 * 0.85, 0, 0);
    b.yaw(180);
    bowShackle(b, paint(0), kGalv, wear, false, 0.85);
    b.pop();
}

Reg groundAnchor({"ground_anchor", "GROUND ANCHOR", "RECOVERY", "What you set up in open ground to winch from when there is no tree: origin at ground, pull toward +X; a career find", 0.7,
                  {
                      {"STAKE PAIR", "Two driven steel stakes, a chain bridle and a shackle",
                       {choice("finish", "STAKES", 0, {"RAW STEEL", "RED", "YELLOW"}), num("lean", "LEAN", 18, 0, 35, 1), num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("soil", "SOIL", 0, {"LOAM", "DARK", "SAND"})},
                       [](B& b, const P& p) { stakePair(b, p.i("finish"), p("lean"), p("wear"), soilCol(p.i("soil"))); }},
                      {"PLOUGH ANCHOR", "A folding spade anchor with an A-frame, dug in at the nose",
                       {paintParam(4), num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("soil", "SOIL", 0, {"LOAM", "DARK", "SAND"})},
                       [](B& b, const P& p) { ploughAnchor(b, paint(p.i("paint")), p("wear"), soilCol(p.i("soil"))); }},
                      {"BURIED TYRE", "A spare tyre dug in on edge as a deadman, the strap out of its trench",
                       {choice("soil", "SOIL", 0, {"LOAM", "DARK", "SAND"}), choice("tyre", "TYRE", 1, {"CAR", "4X4"}), paintParam(8, "strap", "STRAP"), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                       [](B& b, const P& p) { deadmanTyre(b, p.i("soil"), p.i("tyre"), paint(p.i("strap")), p("wear")); }},
                  }});

// ---- 8. Anchor gear: what the winch's line is set to (anchors.h) ---------------------------------
// Origin on the ground at the anchor point, pull toward +X; the eye the line ties to is ~10 cm up.

V3 snowCol(int i) {
    static const V3 c[] = {V3(0.86, 0.88, 0.92), V3(0.72, 0.79, 0.88), V3(0.80, 0.77, 0.70)};
    return c[std::clamp(i, 0, 2)];
}

// A bow shackle through an eye at `eye` (ring axis along z): the bow out toward the pull.
void shackleThrough(B& b, const V3& eye, double s, const V3& col, double wear) {
    b.push();
    b.move(eye.x, eye.y - 0.0105 * s, 0);
    b.scale(s);
    b.yaw(180);
    b.move(-0.04, 0, 0);
    bowShackle(b, col, kGalv, wear, false, 1.0, true);
    b.pop();
}

// A plate anchor (a pull-pal, a deadman): a steel plate dug in at a slant, its front edge and the
// pad-eye out of the ground, the spoil heaped over the rest.
void plateStake(B& b, int finish, double wear, const V3& soil) {
    static const V3 fin[] = {V3(0.42, 0.43, 0.44), V3(0.90, 0.68, 0.06), V3(0.62, 0.10, 0.07)};
    const V3 sc = mix(fin[std::clamp(finish, 0, 2)], kRust, wear * 0.4);
    b.push();
    b.move(-0.30, -0.16, 0);
    b.pitch(24);
    b.style(sc, finish ? PAINT : METAL);
    b.extrudeUp({V2(0, -0.17), V2(0.5, -0.10), V2(0.5, 0.10), V2(0, 0.17)}, 0.012, 0);
    b.color(sc * 0.78);  // a stiffening rib along the middle
    b.extrudeUp({V2(0.04, -0.016), V2(0.48, -0.016), V2(0.48, 0.016), V2(0.04, 0.016)}, 0.03, 0.012);
    b.color(sc);
    b.extrude({V2(0.40, 0.012), V2(0.53, 0.012), V2(0.54, 0.06), V2(0.48, 0.1), V2(0.40, 0.06)}, 0.014, 0);  // the pad-eye lug
    b.style(kSteel * 0.85, METAL);
    ringAxis(b, V3(0.5, 0.078, 0), 'z', 0.026, 0.0065, 14, 5);
    b.pop();
    soilMound(b, V3(-0.17, 0, 0), V3(0.26, 0.055, 0.2), 71, soil, 2);
    soilMound(b, V3(0.0, 0, 0.17), V3(0.12, 0.03, 0.09), 72, soil);
    soilMound(b, V3(0.0, 0, -0.17), V3(0.12, 0.03, 0.09), 73, soil);
    shackleThrough(b, V3(0.145, 0.108, 0), 1.0, paint(0), wear);
}

// A steel stake driven in leaning back from the pull: a mushroomed head, a welded eye, a shackle.
void drivenStake(B& b, int finish, double lean, double wear, const V3& soil) {
    static const V3 fin[] = {V3(0.42, 0.43, 0.44), V3(0.62, 0.10, 0.07), V3(0.90, 0.68, 0.06)};
    const V3 sc = mix(fin[std::clamp(finish, 0, 2)], kRust, wear * 0.35);
    const double a = rad(lean), r = 0.0165;
    const V3 d(-std::sin(a), std::cos(a), 0);
    const V3 p0 = d * -0.5, p1 = d * 0.3;
    b.style(sc, finish ? PAINT : METAL);
    b.cyl(p0, p1, r, 10);
    b.cone(p0 - d * 0.07, p0, 0.0, r, 10);
    b.style(kSteel * 0.75, METAL);  // the head, mushroomed by the sledge
    b.cone(p1, p1 + d * 0.014, r, 0.0235, 10);
    b.cyl(p1 + d * 0.014, p1 + d * 0.03, 0.0235, 10);
    const V3 e = d * 0.17 + V3(r + 0.036 - 0.004, 0, 0);  // the eye, welded on the pull side
    b.style(kSteel * 0.8, METAL);
    ringAxis(b, e, 'z', 0.036, 0.0085, 14, 5);
    b.style(kDark * 1.6, METAL);
    b.box(d * 0.17 + V3(r, 0, 0), V3(0.014, 0.03, 0.02));  // the weld
    shackleThrough(b, e + V3(0.036 - 0.01, 0, 0), 1.0, paint(0), wear);
    soilMound(b, V3(0.0, 0, 0), V3(0.13, 0.04, 0.12), 74, soil);
    soilMound(b, V3(-0.07, 0, 0.06), V3(0.07, 0.03, 0.06), 75, soil);
}

Reg groundStake({"ground_stake", "GROUND STAKE", "RECOVERY", "A stake or plate anchor set in loose ground for the winch line when there is no tree: origin at ground, pull toward +X; gear you carry", 0.6,
                 {
                     {"PLATE ANCHOR", "A pull-pal plate dug in at a slant, a pad-eye and a shackle",
                      {choice("finish", "PLATE", 0, {"RAW STEEL", "YELLOW", "RED"}), num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("soil", "SOIL", 0, {"LOAM", "DARK", "SAND"})},
                      [](B& b, const P& p) { Loose l(p); plateStake(b, p.i("finish"), p("wear"), soilCol(p.i("soil"))); }},
                     {"DRIVEN STAKE", "A steel stake sledged in leaning back, a welded eye and a shackle",
                      {choice("finish", "STAKE", 0, {"RAW STEEL", "RED", "YELLOW"}), num("lean", "LEAN", 20, 0, 35, 1), num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("soil", "SOIL", 0, {"LOAM", "DARK", "SAND"})},
                      [](B& b, const P& p) { Loose l(p); drivenStake(b, p.i("finish"), p("lean"), p("wear"), soilCol(p.i("soil"))); }},
                 }});

// ---- Rock bolt: drilled into bare rock ----

// The pale scar where the drill bit chewed the rock, and the dust round the hole.
void drillScar(B& b, double R, uint32_t seed) {
    if (tLoose) return;
    b.style(V3(0.66, 0.62, 0.55), ROCK);
    b.blob(V3(0, 0.002, 0), V3(R, 0.008, R * 0.92), 0.25, seed, 1, 0.6, 0.0);
    b.style(V3(0.58, 0.54, 0.48), MATTE);
    for (int i = 0; i < 5; i++) {
        const double a = rad(72 * i + 20 * hash01(seed, i)), rr = R * (0.75 + 0.35 * hash01(seed, 9 + i));
        b.blob(V3(rr * std::cos(a), 0.002, rr * std::sin(a)), V3(0.018, 0.006, 0.014), 0.3, seed + i, 0, 0.5, 0.0);
    }
}

// An eye bolt in an expansion sleeve: washer, nut, the forged eye, a shackle.
void eyeBolt(B& b, double wear, const V3& shackleCol) {
    drillScar(b, 0.15, 31);
    b.style(mix(kGalv, kRust, wear * 0.5), METAL);
    b.cyl(V3(0, 0.001, 0), V3(0, 0.009, 0), 0.042, 14);
    hexBolt(b, V3(0, 0.009, 0), 'y', 0.03, 0.024);
    b.style(mix(kGalv, kRust, wear * 0.3) * 0.95, METAL);
    b.cyl(V3(0, 0.033, 0), V3(0, 0.062, 0), 0.0135, 10);
    ringAxis(b, V3(0, 0.092, 0), 'z', 0.034, 0.0095, 16, 6);
    shackleThrough(b, V3(0.03, 0.092, 0), 0.9, shackleCol, wear);
}

// A pad-eye plate bolted down with four anchors, a welded lug and a shackle.
void padEye(B& b, double wear, const V3& shackleCol, bool painted) {
    drillScar(b, 0.2, 47);
    const V3 sc = mix(painted ? V3(0.62, 0.10, 0.07) : kSteel * 0.9, kRust, wear * 0.4);
    b.style(sc, painted ? PAINT : METAL);
    b.box(V3(0, 0.006, 0), V3(0.15, 0.012, 0.15), 0.003);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(kGalv, METAL);
            hexBolt(b, V3(sx * 0.052, 0.012, sz * 0.052), 'y', 0.0105, 0.012);
        }
    b.style(sc * 0.9, painted ? PAINT : METAL);
    b.extrude({V2(-0.04, 0.012), V2(0.04, 0.012), V2(0.035, 0.07), V2(0.0, 0.09), V2(-0.035, 0.07)}, 0.016, 0);
    b.style(kSteel * 0.75, METAL);
    ringAxis(b, V3(0, 0.07, 0), 'z', 0.0235, 0.007, 14, 5);
    shackleThrough(b, V3(0.0, 0.07, 0), 0.8, shackleCol, wear);
}

Reg rockBolt({"rock_bolt", "ROCK BOLT", "RECOVERY", "An eye bolt or pad-eye drilled into bare rock for the winch line; used up (left in the rock); origin at ground, pull toward +X; gear you carry", 0.3,
              {
                  {"EYE BOLT", "A forged eye bolt in an expansion sleeve, a shackle through it",
                   {paintParam(0, "shackle", "SHACKLE"), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                   [](B& b, const P& p) { Loose l(p); eyeBolt(b, p("wear"), paint(p.i("shackle"))); }},
                  {"PAD-EYE", "A plate bolted down with four anchors, a welded lug and a shackle",
                   {paintParam(8, "shackle", "SHACKLE"), choice("finish", "PLATE", 0, {"RAW STEEL", "RED"}), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                   [](B& b, const P& p) { Loose l(p); padEye(b, p("wear"), paint(p.i("shackle")), p.i("finish") == 1); }},
              }});

// ---- Ice anchor: a snow picket or an ice screw ----

// A snow picket (an aluminium angle) buried at a slant, a cable loop through its top.
void snowPicket(B& b, double lean, double wear, const V3& snow) {
    const double a = rad(lean);
    const V3 d(-std::sin(a), std::cos(a), 0);
    const V3 p0 = d * -0.45, p1 = d * 0.3;
    const V3 al = mix(V3(0.70, 0.72, 0.75), kRust, wear * 0.15);
    b.style(al, METAL);
    b.beam(p0, p1, 0.056, 0.004, V3(0, 0, 1), 0.001);  // the angle section: two flanges
    b.beam(p0, p1, 0.004, 0.056, V3(0, 0, 1), 0.001);
    b.style(al * 0.7, METAL);  // the lightening holes
    for (int i = 0; i < 3; i++) b.box(d * (0.08 + 0.07 * i) + V3(0.001, 0, 0), V3(0.012, 0.03, 0.012));
    // The cable loop through the top hole, and a carabiner on it.
    const V3 top = d * 0.25;
    std::vector<V3> loop = {top + V3(0.01, 0, 0), top + V3(0.07, 0.015, 0.02), top + V3(0.13, 0.0, 0.0), top + V3(0.07, -0.01, -0.02)};
    b.style(V3(0.26, 0.27, 0.28), METAL);
    b.tube(spline(loop, 3), 0.0035, 5, true);
    b.style(V3(0.78, 0.80, 0.82), METAL);
    ringAxis(b, V3(0.12, 0.075, 0), 'z', 0.028, 0.0065, 14, 5);
    if (!tLoose) {
        b.style(snow * 0.97, MATTE);
        b.blob(V3(0.02, 0, 0), V3(0.22, 0.06, 0.2), 0.25, 61, 2, 0.45, 0.0);
        b.blob(V3(-0.1, 0, 0.1), V3(0.1, 0.04, 0.09), 0.3, 62, 1, 0.5, 0.0);
    }
}

// A tubular ice screw wound in, its folded hanger and a clip, the crank handle laid beside it.
void iceScrew(B& b, double wear, const V3& ice) {
    b.style(V3(0.74, 0.76, 0.78), METAL);
    b.cyl(V3(0, 0.0, 0), V3(0, 0.07, 0), 0.0125, 12);
    b.style(V3(0.30, 0.31, 0.33), METAL);
    b.cyl(V3(0, 0.07, 0), V3(0, 0.082, 0), 0.0185, 12);
    // The hanger: a folded lug on top, a ring through it.
    b.style(V3(0.78, 0.30, 0.10), PAINT);
    b.extrude({V2(-0.03, 0.082), V2(0.03, 0.082), V2(0.03, 0.12), V2(0.0, 0.14), V2(-0.03, 0.12)}, 0.01, 0);
    b.style(V3(0.78, 0.80, 0.82), METAL);
    ringAxis(b, V3(0.0, 0.118, 0), 'z', 0.026, 0.0055, 14, 5);
    shackleThrough(b, V3(0.026, 0.118, 0), 0.8, mix(V3(0.78, 0.30, 0.10), kRust, wear * 0.2), wear);
    // The crank, laid on the ice.
    b.style(V3(0.30, 0.31, 0.33), METAL);
    b.tube({V3(-0.12, 0.012, 0.22), V3(-0.12, 0.012, 0.06), V3(-0.15, 0.012, 0.02), V3(-0.25, 0.012, 0.02)}, 0.0075, 6);
    if (!tLoose) {
        // The crust cracked round the hole, chips of ice.
        b.style(ice, MATTE);
        b.blob(V3(0, 0.003, 0), V3(0.12, 0.012, 0.11), 0.3, 63, 1, 0.6, 0.0);
        for (int i = 0; i < 6; i++) {
            const double ang = rad(60 * i + 15), rr = 0.1 + 0.07 * hash01(64, i);
            b.blob(V3(rr * std::cos(ang), 0.003, rr * std::sin(ang)), V3(0.02, 0.008, 0.016), 0.3, 65 + i, 0, 0.5, 0.0);
        }
    }
}

Reg iceAnchor({"ice_anchor", "ICE ANCHOR", "RECOVERY", "A snow picket or an ice screw for the winch line on snow and ice; used up (left in); origin at ground, pull toward +X; gear you carry", 0.5,
               {
                   {"SNOW PICKET", "An aluminium picket buried at a slant, a cable loop and a clip",
                    {num("lean", "LEAN", 30, 0, 45, 1), num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("snow", "SNOW", 0, {"FRESH", "BLUE", "DIRTY"})},
                    [](B& b, const P& p) { Loose l(p); snowPicket(b, p("lean"), p("wear"), snowCol(p.i("snow"))); }},
                   {"ICE SCREW", "A tubular screw wound into the ice, a hanger, a clip and the crank beside it",
                    {num("wear", "WEAR", 0.3, 0, 1, 0.05), choice("ice", "ICE", 0, {"FRESH", "BLUE", "DIRTY"})},
                    [](B& b, const P& p) { Loose l(p); iceScrew(b, p("wear"), snowCol(p.i("ice"))); }},
               }});

// ---- Chain sling: choked round a boulder ----

// Chain links along a path: heavier than chainRun's, and cheap (a diamond for each link).
void towChain(B& b, const std::vector<V3>& poly, double wear, uint32_t seed) {
    const double pitch = 0.072;
    std::vector<double> cum(poly.size(), 0.0);
    for (size_t i = 1; i < poly.size(); i++) cum[i] = cum[i - 1] + len(poly[i] - poly[i - 1]);
    int k = 0;
    for (double s = pitch * 0.5; s < cum.back() - pitch * 0.2; s += pitch, k++) {
        size_t i = 1;
        while (i + 1 < poly.size() && cum[i] < s) i++;
        const double f = (s - cum[i - 1]) / std::max(1e-9, cum[i] - cum[i - 1]);
        const V3 pt = poly[i - 1] + (poly[i] - poly[i - 1]) * f, d = norm(poly[i] - poly[i - 1]);
        b.push();
        b.move(pt);
        b.yaw(std::atan2(-d.z, d.x) * 180 / kPi);
        b.pitch(std::asin(std::clamp(d.y, -1.0, 1.0)) * 180 / kPi);
        b.roll((k & 1) ? 90 : 0);
        b.style(mix(kSteel * 0.55, kRust, wear * hash01(seed, k)), METAL);
        b.tube(stadium(V3(), V3(1, 0, 0), V3(0, 1, 0), 0.098, 0.05, 1), 0.0105, 4, true);
        b.pop();
    }
}

// A chain choked once round the boulder's belly, its two ends to a master ring and a shackle.
void chainChoker(B& b, double R, double wear, const V3& shackleCol) {
    const std::vector<V3>& outline = mk::pathHint();  // (along a boulder's cross-section, not round a circle: see webTreeSaver)
    const bool along = outline.size() >= 3;
    const double yc = along ? 0.0 : std::max(0.2, 0.45 * R), Rc = 0.9 * R, xm = along ? 0.16 : Rc + 0.3;
    const double phi = leaveAngle(Rc, xm);
    std::vector<V3> path;
    path.push_back(V3(xm - 0.04, yc, 0.012));
    if (along) {
        for (const V3& q : outline) path.push_back(V3(q.x, yc, q.z));
    } else {
        const int n = std::max(10, (int)(R * 40));
        for (int i = 0; i <= n; i++) {
            const double a = rad(phi + (360 - 2 * phi) * i / n);
            path.push_back(V3(Rc * std::cos(a), yc, Rc * std::sin(a)));
        }
    }
    path.push_back(V3(xm - 0.04, yc, -0.012));
    towChain(b, path, wear, 300);
    b.style(kSteel * 0.8, METAL);
    ringAxis(b, V3(xm, yc, 0), 'z', 0.05, 0.0105, 16, 5);
    shackleThrough(b, V3(xm + 0.05, yc, 0), 1.5, shackleCol, wear);
}

Reg chainSling({"chain_sling", "CHAIN SLING", "RECOVERY", "Choked round a boulder to winch from it: +X is the pull direction, `radius` the boulder's; carried, comes off with the line", 1.5,
                {
                    {"CHAIN CHOKER", "Tow chain once round the rock, a master ring and a shackle",
                     {num("radius", "BOULDER RADIUS", 0.6, 0.3, 1.5, 0.05), paintParam(0, "shackle", "SHACKLE"), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                     [](B& b, const P& p) { chainChoker(b, p("radius"), p("wear"), paint(p.i("shackle"))); }},
                    {"WEB CHOKER", "A wide recovery web round the rock, a steel ring through its eyes",
                     {num("radius", "BOULDER RADIUS", 0.6, 0.3, 1.5, 0.05), paintParam(4, "paint", "WEB"), num("wear", "WEAR", 0.3, 0, 1, 0.05)},
                     [](B& b, const P& p) {
                         const std::vector<V3>& outline = mk::pathHint();
                         const bool along = outline.size() >= 3;
                         b.push();
                         if (!along) b.move(0, std::max(0.2, 0.45 * p("radius")) - kTreeY, 0);
                         webTreeSaver(b, paint(p.i("paint")), p("radius") * 0.9, false, 1, kSteel * 0.8, p("wear"), along ? &outline : nullptr);
                         b.pop();
                     }},
                }});

}  // namespace
