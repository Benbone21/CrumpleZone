// Repair: hand tools (origin at the grip, working end +X, flat face up) and spare parts (origin
// at the middle, long axis along X) for hands-on trail repairs. See modelkit.h for the frame.
#include <cmath>
#include "../modelkit.h"

using namespace mk;

namespace {

#ifndef REVIEW_LIFT
#define REVIEW_LIFT 0.0
#endif
// (Contact-sheet reviews build with -DREVIEW_LIFT=0.05 so grip-centred tools clear the ground.)
BuildFn lifted(BuildFn f) {
    return [f](B& b, const P& p) {
        if (REVIEW_LIFT != 0.0) b.move(0, REVIEW_LIFT, 0);
        f(b, p);
    };
}

#ifndef OPEN_DEF
#define OPEN_DEF 0.0
#endif
const double DEG = M_PI / 180;
const V3 OIL(0.075, 0.06, 0.045);
V3 grime(const V3& c, double w) { return mix(c, OIL, clampd(w, 0, 1) * 0.65); }
Param wearParam(double def = 0.15) { return num("wear", "WEAR", def, 0, 1, 0.05); }

struct Fin {
    V3 c;
    Mat m;
};
// 0 chrome, 1 black oxide, 2 zinc, 3 bare steel.
Fin finish(int f, double wear) {
    switch (f) {
        case 0: return {mix(V3(0.84, 0.86, 0.89), V3(0.40, 0.39, 0.37), wear * 0.7), CHROME};
        case 1: return {grime(V3(0.085, 0.09, 0.10), wear * 0.6), METAL};
        case 2: return {grime(V3(0.58, 0.60, 0.58), wear), METAL};
        default: return {grime(V3(0.50, 0.51, 0.53), wear), METAL};
    }
}
void setFin(B& b, int f, double wear) {
    const Fin x = finish(f, wear);
    b.style(x.c, x.m);
}
const std::vector<std::string> kFinNames = {"CHROME", "BLACK OXIDE", "ZINC", "BARE STEEL"};
Param finParam(int def = 0) { return choice("finish", "FINISH", def, kFinNames); }

V3 darkSteel(double wear) { return grime(V3(0.20, 0.21, 0.23), wear * 0.5); }
V3 rubberCol(double wear) { return grime(V3(0.055, 0.055, 0.058), wear * 0.3); }

// A lathe about the axis a -> c: profile points (radius, distance along the axis from a).
void latheAB(B& b, const V3& a, const V3& c, const std::vector<V2>& prof, int segs = 16, bool smooth = true) {
    const V3 d = norm(c - a);
    b.push();
    b.move(a);
    if (d.y < -0.9999)
        b.rotate(V3(1, 0, 0), 180);
    else if (d.y < 0.9999)
        b.rotate(norm(cross(V3(0, 1, 0), d)), std::acos(d.y) / DEG);
    b.lathe(V3(0, 0, 0), prof, segs, smooth);
    b.pop();
}
// A chamfered round bar from a to c.
void rodAB(B& b, const V3& a, const V3& c, double r, double ch = 0.0015, int segs = 14, bool smooth = true) {
    const double L = len(c - a);
    ch = std::min(ch, r * 0.8);
    latheAB(b, a, c, {V2(0, 0), V2(r - ch, 0), V2(r, ch), V2(r, L - ch), V2(r - ch, L), V2(0, L)}, segs, smooth);
}
// A zig-zag band of knurling or grip rings (outer radius r, ridge depth d) from a to c.
void knurl(B& b, const V3& a, const V3& c, double r, double d, int n, int segs = 10, bool smooth = false) {
    const double L = len(c - a);
    std::vector<V2> pr = {V2(0, 0), V2(r - d, 0)};
    for (int i = 0; i < n; i++) {
        pr.push_back(V2(r, L * (i + 0.5) / n));
        pr.push_back(V2(r - d, L * (i + 1.0) / n));
    }
    pr.push_back(V2(0, L));
    latheAB(b, a, c, pr, segs, smooth);
}
// A flat disc (a fan) at c facing n.
void discFan(B& b, const V3& c, const V3& n0, double r, int segs = 6) {
    const V3 n = norm(n0);
    const V3 t = std::fabs(n.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0);
    const V3 u = norm(cross(n, t)), v = cross(n, u);
    for (int k = 0; k < segs; k++) {
        const double a0 = 2 * M_PI * k / segs, a1 = 2 * M_PI * (k + 1) / segs;
        b.tri(c, c + u * (r * std::cos(a0)) + v * (r * std::sin(a0)), c + u * (r * std::cos(a1)) + v * (r * std::sin(a1)));
    }
}
// A dark disc (a bore, a hole) lying on the end at p, facing d.
void hole(B& b, const V3& p, const V3& d, double r, int segs = 6) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(V3(0.02, 0.02, 0.025), MATTE);
    discFan(b, p + norm(d) * 0.0006, d, r, segs);
    b.style(keep, km);
}

// Letters stamped on an up-facing flat at `at`, reading along +X (letter tops towards the right, -Z).
void stampTop(B& b, const std::string& s, const V3& at, double px, double depth = 0.0010) {
    b.push();
    b.move(at);
    b.pitch(90);
    b.roll(-90);
    b.letters(s, V3(0, 0, 0), px, depth);
    b.pop();
}
// Letters stamped on the left (+Z) face reading along +X, or the right face (dir -1) reading along -X.
void stampSide(B& b, const std::string& s, const V3& at, double px, int dir = 1, double depth = 0.0010) {
    b.push();
    b.move(at);
    b.yaw(dir > 0 ? -90 : 90);
    b.letters(s, V3(0, 0, 0), px, depth);
    b.pop();
}

// Grease / grime blobs scattered along a -> c (for the used variants).
void grease(B& b, const V3& a, const V3& c, double r, int n, double wear, uint32_t seed, double spread = 0.0) {
    const int k = (int)std::lround(n * wear);
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(V3(0.05, 0.04, 0.03), MATTE);
    for (int i = 0; i < k; i++) {
        const double t = hash01(seed, i * 3), s = 0.6 + 0.8 * hash01(seed, i * 3 + 1);
        const double ang = 6.283 * hash01(seed, i * 3 + 2);
        const V3 p = a + (c - a) * t + V3(0, std::cos(ang), std::sin(ang)) * spread;
        b.sphere(p, V3(r * s * 1.6, r * s * 0.5, r * s), 0);
    }
    b.style(keep, km);
}

// Outline helpers for flat (extrudeUp) parts: points in (x, z).
std::vector<V2> xform(const std::vector<V2>& pts, double angDeg, double tx, double tz) {
    const double c = std::cos(angDeg * DEG), s = std::sin(angDeg * DEG);
    std::vector<V2> o;
    for (const V2& q : pts) o.push_back(V2(tx + q.x * c - q.y * s, tz + q.x * s + q.y * c));
    return o;
}
// An open-end head: round of radius Ro about the origin, a slot gap g wide opening towards +u.
std::vector<V2> openHead(double Ro, double g, double floorU) {
    std::vector<V2> pts;
    const double a0 = std::asin(g / 2 / Ro);
    const int n = 14;
    for (int i = 0; i <= n; i++) {
        const double t = a0 + (2 * M_PI - 2 * a0) * i / n;
        pts.push_back(V2(Ro * std::cos(t), Ro * std::sin(t)));
    }
    pts.push_back(V2(floorU, -g / 2));
    pts.push_back(V2(floorU - g * 0.35, -g * 0.3));
    pts.push_back(V2(floorU - g * 0.5, 0));
    pts.push_back(V2(floorU - g * 0.35, g * 0.3));
    pts.push_back(V2(floorU, g / 2));
    return pts;
}
// A flat ring (annulus) round the Y axis through c: inner / outer radius, thickness, chamfered.
void flatRing(B& b, const V3& c, double ri, double ro, double t, int segs = 24, bool smooth = false) {
    const double h = t / 2, ch = std::min(0.0012, (ro - ri) * 0.25);
    b.lathe(c, {V2(ri, -h + ch), V2(ro - ch, -h), V2(ro, -h + ch), V2(ro, h - ch), V2(ro - ch, h), V2(ri, h - ch), V2(ri, -h + ch)}, segs, smooth);
}
// A socket seen as a turned tube along a -> c (a at the back): body radius r, hex bore at the open end c.
void socketAB(B& b, const V3& a, const V3& c, double r, double bore, int segs = 14) {
    const double L = len(c - a);
    latheAB(b, a, c, {V2(0, 0), V2(r * 0.8, 0), V2(r * 0.8, L * 0.12), V2(r * 0.92, L * 0.2), V2(r, L * 0.3), V2(r, L - 0.003), V2(r - 0.0014, L), V2(0, L)}, segs, true);
    hole(b, c, c - a, bore, 6);
}

// ======================================================================================
// 9. DRIVE SHAFT
// ======================================================================================

// The outline of a yoke ear in (x, w): flat at the base xa, a round nose about the bore at xc.
std::vector<V2> earPoly(double xa, double xc, double W = 0.023) {
    const double s = xc > xa ? 1 : -1;
    std::vector<V2> pts = {V2(xa, -W)};
    for (int i = 0; i < 5; i++) {
        const double t = (-90 + 45 * i) * DEG;
        pts.push_back(V2(xc - s * W + s * W * std::cos(t), W * std::sin(t)));
    }
    pts.push_back(V2(xa, W));
    return pts;
}
// Ears lying flat (plates at +/-Y).
void earsY(B& b, double xa, double xc) {
    b.extrudeUp(earPoly(xa, xc), 0.011, 0.0145);
    b.extrudeUp(earPoly(xa, xc), 0.011, -0.0255);
}
// Ears standing (plates at +/-Z).
void earsZ(B& b, double xa, double xc) {
    b.extrude(earPoly(xa, xc), 0.011, 0.0200);
    b.extrude(earPoly(xa, xc), 0.011, -0.0200);
}
// A universal joint's cross at x: four bearing caps and a block.
void cross4(B& b, double x, double w, const V3& cap, const V3& cap2) {
    for (int k = 0; k < 4; k++) {
        const V3 d = k == 0 ? V3(0, 1, 0) : k == 1 ? V3(0, -1, 0) : k == 2 ? V3(0, 0, 1) : V3(0, 0, -1);
        b.style(grime(cap, w), METAL);
        b.cyl(V3(x, 0, 0) + d * 0.0070, V3(x, 0, 0) + d * 0.0300, 0.0135, 7, true);
        b.style(grime(cap2, w) * 0.55, METAL);
        discFan(b, V3(x, 0, 0) + d * 0.0302, d, 0.0078, 6);  // the dark bearing cup end
    }
    b.style(grime(cap, w) * 0.7, METAL);
    b.box(V3(x, 0, 0), V3(0.020, 0.020, 0.020), 0);
}
// A flange yoke: a bolted disc, a neck, ears set by the caller. dir -1 = disc at the -X end.
void flangeYoke(B& b, double xBase, int dir, double w, const V3& col) {
    b.style(grime(col, w), METAL);
    const V3 a(xBase + dir * 0.057, 0, 0), c(xBase, 0, 0);
    latheAB(b, a, c, {V2(0, 0), V2(0.026, 0), V2(0.026, 0.006), V2(0.0505, 0.006), V2(0.0505, 0.018), V2(0.036, 0.018), V2(0.031, 0.024), V2(0.031, 0.057)}, 8, false);
    b.style(grime(col, w) * 1.5, METAL);
    for (int k = 0; k < 4; k++) {
        const double t = 2 * M_PI * k / 4 + M_PI / 4;
        const V3 p(xBase + dir * (0.057 - 0.018), std::cos(t) * 0.039, std::sin(t) * 0.039);
        b.cyl(p, p + V3(1, 0, 0) * (dir * 0.007) * -1.0, 0.0065, 6);
    }
}
// A welded tube yoke: collar + a swaged cone into the tube of radius R; base at xBase, tube towards the middle.
void tubeYoke(B& b, double xBase, int dir, double R, double w, const V3& col) {
    b.style(grime(col, w), METAL);
    latheAB(b, V3(xBase, 0, 0), V3(xBase + dir * 0.08, 0, 0), {V2(0, 0), V2(0.031, 0), V2(0.031, 0.012), V2(0.034, 0.016), V2(R * 0.82, 0.034), V2(R, 0.056), V2(R, 0.08)}, 9, true);
}

struct DS {
    double R;
    V3 tube;
    Mat tubeMat;
    V3 yoke;
    int mode;  // 0 slip yoke, 1 slip yoke (big tube), 2 double cardan
};

void driveShaft(B& b, const P& p, const DS& s) {
    const double w = p("wear");
    const double X1 = 0.45;
    b.move(s.mode == 2 ? -0.03 : -0.04, 0, 0);
    const V3 yoke = s.yoke, cap = V3(0.66, 0.67, 0.70);
    // -X end: flange yoke, joint, tube yoke.
    flangeYoke(b, -X1 - 0.050, -1, w, yoke);
    b.style(grime(yoke, w), METAL);
    earsY(b, -X1 - 0.050, -X1);
    earsZ(b, -X1 + 0.080, -X1);
    cross4(b, -X1, w, cap, cap);
    tubeYoke(b, -X1 + 0.080, 1, s.R, w, yoke);
    const double xt0 = -X1 + 0.160;
    double xt1;
    if (s.mode == 2) {
        xt1 = 0.21;
        const double x2 = 0.37, x3 = 0.445;
        tubeYoke(b, x2 - 0.08, -1, s.R, w, yoke);
        b.style(grime(yoke, w), METAL);
        earsZ(b, 0.29, x2);
        cross4(b, x2, w, cap, cap);
        // The centring yoke: a body with an ear pair either side, a ball-and-socket in the middle.
        b.style(grime(yoke, w) * 1.1, METAL);
        earsY(b, 0.408, x2);
        earsY(b, 0.410, x3);
        latheAB(b, V3(0.395, 0, 0), V3(0.425, 0, 0), {V2(0, 0), V2(0.030, 0), V2(0.036, 0.005), V2(0.036, 0.025), V2(0.030, 0.030), V2(0, 0.030)}, 9, true);
        cross4(b, x3, w, cap, cap);
        b.style(grime(yoke, w), METAL);
        earsZ(b, 0.505, x3);
        flangeYoke(b, 0.505, 1, w, yoke);
    } else {
        xt1 = X1 - 0.160;
        tubeYoke(b, X1 - 0.080, -1, s.R, w, yoke);
        b.style(grime(yoke, w), METAL);
        earsZ(b, X1 - 0.080, X1);
        cross4(b, X1, w, cap, cap);
        // The slip yoke: ears, body, dust cap, splined sleeve.
        b.style(grime(yoke, w), METAL);
        earsY(b, X1 + 0.060, X1);
        latheAB(b, V3(X1 + 0.060, 0, 0), V3(X1 + 0.190, 0, 0),
                {V2(0, 0), V2(0.031, 0), V2(0.0335, 0.012), V2(0.0335, 0.040), V2(0.0385, 0.044), V2(0.0385, 0.062), V2(0.0345, 0.066), V2(0.0272, 0.070), V2(0.0272, 0.130)}, 10, true);
        b.style(grime(yoke, w) * 0.5, METAL);
        knurl(b, V3(X1 + 0.145, 0, 0), V3(X1 + 0.188, 0, 0), 0.0275, 0.0008, 3, 10, false);
        hole(b, V3(X1 + 0.190, 0, 0), V3(1, 0, 0), 0.0205, 10);
    }
    // The tube itself, with a little balance weights on it.
    b.style(grime(s.tube, w * 0.6), s.tubeMat);
    const double xe = xt1;
    latheAB(b, V3(xt0 - 0.002, 0, 0), V3(xe + 0.002, 0, 0), {V2(s.R * 0.99, 0), V2(s.R, 0.002), V2(s.R, xe - xt0), V2(s.R * 0.99, xe - xt0 + 0.002)}, 10, true);
    b.style(grime(V3(0.30, 0.30, 0.31), w), METAL);
    b.box(V3(xt0 + 0.07, s.R + 0.0015, 0.010), V3(0.026, 0.004, 0.018), 0.0);
    b.box(V3(xe - 0.10, 0.004, s.R + 0.0015), V3(0.022, 0.016, 0.004), 0.0);
    grease(b, V3(xt0, s.R, 0), V3(xe, s.R, 0), 0.006, 4, w, 121, 0.0);
}

void dsSteel(B& b, const P& p) { driveShaft(b, p, DS{0.0445, paint(p.i("paint")), PAINT, V3(0.36, 0.36, 0.38), 0}); }
void dsAlu(B& b, const P& p) {
    static const V3 cols[3] = {V3(0.74, 0.76, 0.78), V3(0.12, 0.12, 0.13), V3(0.10, 0.26, 0.58)};
    driveShaft(b, p, DS{0.0575, cols[std::clamp(p.i("tube"), 0, 2)], METAL, V3(0.42, 0.42, 0.45), 1});
}
void dsCardan(B& b, const P& p) { driveShaft(b, p, DS{0.0445, paint(p.i("paint")), PAINT, V3(0.36, 0.36, 0.38), 2}); }

Reg driveShaftReg({"drive_shaft", "DRIVE SHAFT", "REPAIR", "Spare part: propeller shaft with U-joints, ~1.2 m; origin at the middle, long axis X (flange yoke at -X)", 1.3,
                   {
                       {"STEEL TUBE", "Painted steel tube, two U-joints, slip yoke", {paintParam(9, "paint", "TUBE"), wearParam(0.25)}, lifted(dsSteel)},
                       {"ALUMINIUM BIG TUBE", "Big-diameter aluminium tube, fat yokes, slip yoke", {choice("tube", "TUBE", 0, {"BARE", "BLACK", "BLUE"}), wearParam(0.2)}, lifted(dsAlu)},
                       {"DOUBLE CARDAN", "CV shaft: a double-cardan joint with a centring yoke", {paintParam(9, "paint", "TUBE"), wearParam(0.25)}, lifted(dsCardan)},
                   }});

// ======================================================================================
// 10. TIE ROD
// ======================================================================================

// A ball-joint tie-rod end at (x, 0, z): socket housing, a dust boot, a tapered stud pointing up with a
// castle nut and cotter pin, a collar where the rod threads in. dir: the rod runs this way (+1/-1 along X).
void tieRodEnd(B& b, double x, double z, int dir, double s, double w, const V3& housing, bool zerk) {
    b.style(grime(housing, w), METAL);
    b.lathe(V3(x, 0, z), {V2(0, -0.0165 * s), V2(0.0150 * s, -0.0165 * s), V2(0.0192 * s, -0.0105 * s), V2(0.0198 * s, 0.0), V2(0.0185 * s, 0.0085 * s), V2(0.0140 * s, 0.0135 * s), V2(0.0, 0.0145 * s)}, 12, true);
    // The collar and neck towards the rod.
    b.cyl(V3(x, 0, z), V3(x + dir * 0.030 * s, 0, z), 0.0128 * s, 10, true);
    b.style(grime(housing, w) * 0.8, METAL);
    b.cyl(V3(x + dir * 0.026 * s, 0, z), V3(x + dir * 0.034 * s, 0, z), 0.0152 * s, 6, true);  // wrench flats
    // Stud, boot, castle nut.
    b.style(grime(V3(0.50, 0.50, 0.52), w), METAL);
    latheAB(b, V3(x, 0.010 * s, z), V3(x, 0.070 * s, z), {V2(0.0098 * s, 0), V2(0.0062 * s, 0.040 * s), V2(0.0055 * s, 0.054 * s), V2(0.0040 * s, 0.059 * s)}, 10, true);
    b.style(rubberCol(w), MATTE);
    latheAB(b, V3(x, 0.006 * s, z), V3(x, 0.038 * s, z), {V2(0.0165 * s, 0), V2(0.0140 * s, 0.006 * s), V2(0.0108 * s, 0.016 * s), V2(0.0085 * s, 0.028 * s), V2(0.0078 * s, 0.032 * s)}, 12, true);
    b.style(grime(V3(0.45, 0.45, 0.47), w), METAL);
    b.cyl(V3(x, 0.042 * s, z), V3(x, 0.0615 * s, z), 0.0118 * s, 6, true);
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.box(V3(x, 0.0600 * s, z), V3(0.0036 * s, 0.0040 * s, 0.0204 * s), 0);
    b.style(V3(0.62, 0.50, 0.14), METAL);
    b.cyl(V3(x - 0.0001, 0.0560 * s, z - 0.0112 * s), V3(x - 0.0001, 0.0560 * s, z + 0.0112 * s), 0.0012 * s, 4, true);
    if (zerk) {
        b.style(V3(0.7, 0.7, 0.7), METAL);
        b.cyl(V3(x - dir * 0.004 * s, 0.0, z + 0.016 * s), V3(x - dir * 0.004 * s, 0.0, z + 0.030 * s), 0.0035 * s, 6, true);
    }
    // Black dirt on the housing.
    grease(b, V3(x - 0.012, 0.0, z), V3(x + 0.012, 0.0, z), 0.0035 * s, 4, w, 131 + (uint32_t)(x * 100), 0.015 * s);
}

// A rod-end (heim joint) at (x, 0, z): an eye with a spherical ball, a bolt and anodised spacers.
void heimEnd(B& b, double x, double z, int dir, double w, const V3& anod) {
    b.style(grime(V3(0.30, 0.30, 0.32), w), METAL);
    b.lathe(V3(x, 0, z), {V2(0.0135, -0.0105), V2(0.0215, -0.0105), V2(0.0235, -0.0075), V2(0.0235, 0.0075), V2(0.0215, 0.0105), V2(0.0135, 0.0105)}, 14, false);
    b.cyl(V3(x, 0, z), V3(x + dir * 0.034, 0, z), 0.0112, 10, true);
    b.style(grime(V3(0.62, 0.63, 0.66), w), CHROME);
    b.lathe(V3(x, 0, z), {V2(0.0088, -0.0125), V2(0.0150, -0.0075), V2(0.0162, 0.0), V2(0.0150, 0.0075), V2(0.0088, 0.0125)}, 10, true);
    // The bolt and the spacers either side of the eye.
    b.style(grime(V3(0.40, 0.40, 0.43), w), METAL);
    b.cyl(V3(x, -0.034, z), V3(x, 0.034, z), 0.0082, 8, true);
    b.cyl(V3(x, 0.034, z), V3(x, 0.0435, z), 0.0135, 6, true);
    b.style(grime(anod, w * 0.5), METAL);
    b.cyl(V3(x, 0.0125, z), V3(x, 0.0245, z), 0.0125, 10, true);
    b.cyl(V3(x, -0.0245, z), V3(x, -0.0125, z), 0.0125, 10, true);
    b.style(grime(V3(0.40, 0.40, 0.43), w), METAL);
    b.cyl(V3(x, -0.034, z), V3(x, -0.0435, z), 0.0135, 6, true);
}

void hexNut(B& b, double x, double z, double len, double r, const V3& col, double w) {
    b.style(grime(col, w), METAL);
    b.cyl(V3(x - len / 2, 0, z), V3(x + len / 2, 0, z), r, 6, true);
}

// The adjuster sleeve: a tube with hex ends, a slit and two clamp bolts.
void adjuster(B& b, double x0, double x1, double r, double w, const V3& col) {
    b.style(grime(col, w), METAL);
    rodAB(b, V3(x0, 0, 0), V3(x1, 0, 0), r, 0.0016, 10);
    b.cyl(V3(x0, 0, 0), V3(x0 + 0.022, 0, 0), r * 1.14, 6, true);
    b.cyl(V3(x1 - 0.022, 0, 0), V3(x1, 0, 0), r * 1.14, 6, true);
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.box(V3((x0 + x1) / 2, r * 0.98, 0), V3((x1 - x0) * 0.6, 0.0008, 0.0032), 0);
    b.style(grime(col, w) * 0.6, METAL);
    for (int k = -1; k <= 1; k += 2) {
        const double xc = (x0 + x1) / 2 + k * (x1 - x0) * 0.22;
        b.cyl(V3(xc - 0.005, 0, 0), V3(xc + 0.005, 0, 0), r * 1.1, 10, false);
        b.cyl(V3(xc, r * 1.05, 0), V3(xc, r * 1.05 + 0.006, 0), 0.0055, 6, true);
    }
}

void tieRodStock(B& b, const P& p) {
    const double w = p("wear");
    const double E = 0.52;
    const V3 bar = grime(V3(0.20, 0.20, 0.22), w * 0.6);
    for (int k = -1; k <= 1; k += 2) tieRodEnd(b, k * E, 0, -k, 1.0, w, V3(0.46, 0.46, 0.48), true);
    b.style(bar, METAL);
    rodAB(b, V3(-E + 0.03, 0, 0), V3(E - 0.03, 0, 0), 0.0105, 0.001, 10);
    // Threaded runs either side of the sleeve with lock nuts.
    b.style(grime(V3(0.34, 0.34, 0.36), w), METAL);
    for (int k = -1; k <= 1; k += 2) {
        knurl(b, V3(k * 0.112, 0, 0), V3(k * 0.20, 0, 0), 0.0114, 0.0009, 3, 8, false);
        hexNut(b, k * 0.116, 0, 0.013, 0.0150, V3(0.40, 0.40, 0.42), w);
    }
    adjuster(b, -0.10, 0.10, 0.0168, w, V3(0.52, 0.53, 0.50));
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    stampTop(b, "L", V3(-0.07, 0.0180, 0), 0.0014, 0.0004);
}

void tieRodHeavy(B& b, const P& p) {
    const double w = p("wear");
    const V3 anod = paint(p.i("colour"));
    const double E = 0.52;
    for (int k = -1; k <= 1; k += 2) {
        heimEnd(b, k * E, 0, -k, w, anod);
        hexNut(b, k * (E - 0.050), 0, 0.011, 0.0140, anod, w);
    }
    // The chromoly tube with threaded bungs.
    b.style(paint(p.i("tube")) * (1 - w * 0.4), PAINT);
    rodAB(b, V3(-E + 0.055, 0, 0), V3(E - 0.055, 0, 0), 0.0128, 0.0010, 12);
    b.style(grime(V3(0.34, 0.34, 0.36), w), METAL);
    for (int k = -1; k <= 1; k += 2) {
        b.cyl(V3(k * (E - 0.034), 0, 0), V3(k * (E - 0.068), 0, 0), 0.0098, 8, true);
        hexNut(b, k * (E - 0.072), 0, 0.012, 0.0154, V3(0.30, 0.30, 0.32), w);
    }
    // A slim anodised sleeve and the tube's reinforcing collars.
    b.style(grime(anod, w * 0.5), METAL);
    b.cyl(V3(-0.012, 0, 0), V3(0.012, 0, 0), 0.0158, 10, true);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    stampTop(b, "4130", V3(-0.12, 0.0131, 0), 0.0012, 0.0004);
}

void dragLink(B& b, const P& p) {
    const double w = p("wear");
    const double E = 0.53, off = 0.05;
    auto zOf = [&](double x) { return off * smoothstep(-0.20, 0.14, x); };
    tieRodEnd(b, -E, zOf(-E), 1, 1.35, w, V3(0.42, 0.42, 0.44), true);
    tieRodEnd(b, E, zOf(E), -1, 1.35, w, V3(0.42, 0.42, 0.44), true);
    // A heavy bar with a dog-leg.
    b.style(paint(p.i("paint")) * (1 - w * 0.4), PAINT);
    std::vector<V3> path = {V3(-E + 0.040, 0, 0), V3(-0.28, 0, 0), V3(-0.20, 0, zOf(-0.20))};
    for (int i = -3; i <= 3; i++) path.push_back(V3(i * 0.045, 0, zOf(i * 0.045)));
    path.push_back(V3(0.20, 0, zOf(0.20)));
    path.push_back(V3(0.28, 0, zOf(0.28)));
    path.push_back(V3(E - 0.040, 0, zOf(E - 0.040)));
    b.tube(path, 0.0160, 10, false, true);
    // The threaded adjuster clamp near one end.
    b.push();
    b.move(0.34, 0, zOf(0.34));
    adjuster(b, -0.05, 0.05, 0.0210, w, V3(0.50, 0.50, 0.52));
    b.pop();
    b.style(grime(V3(0.4, 0.4, 0.42), w), METAL);
    hexNut(b, -E + 0.065, 0, 0.016, 0.0215, V3(0.38, 0.38, 0.40), w);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
}

Reg tieRodReg({"tie_rod", "TIE ROD", "REPAIR", "Spare part: steering tie rod / drag link with ball ends, ~1.1 m; origin at the middle, long axis X, studs point up", 1.1,
               {
                   {"STOCK", "Stock tie rod: ball-joint ends with castle nuts, adjuster sleeve", {wearParam(0.2)}, lifted(tieRodStock)},
                   {"HEIM CHROMOLY", "Heavy-duty chromoly tube with heim joints and anodised spacers", {paintParam(3, "tube", "TUBE"), paintParam(5, "colour", "ANODISING"), wearParam(0.1)}, lifted(tieRodHeavy)},
                   {"DRAG LINK", "Heavy bent drag link with big ball ends and a clamp", {paintParam(3, "paint", "PAINT"), wearParam(0.25)}, lifted(dragLink)},
               }});

// ======================================================================================
// 11. CONTROL ARM
// ======================================================================================

const std::vector<std::string> kBush = {"BLACK RUBBER", "PURPLE POLY", "RED POLY"};
V3 bushCol(int i, double w) {
    static const V3 c[3] = {V3(0.045, 0.045, 0.05), V3(0.36, 0.12, 0.50), V3(0.70, 0.08, 0.06)};
    return grime(c[std::clamp(i, 0, 2)], w * 0.5);
}

// A pivot bushing with its axis along Z at (x, y, z): outer sleeve, rubber/poly, inner steel sleeve.
void bushing(B& b, double x, double y, double z, double rOut, double len, const V3& rubber, double w, const V3& steelCol) {
    const V3 a(x, y, z - len / 2), c(x, y, z + len / 2);
    b.style(grime(steelCol, w), METAL);
    b.cyl(a, c, rOut, 12, false);
    b.style(rubber, MATTE);
    b.cyl(a - V3(0, 0, 0.002), c + V3(0, 0, 0.002), rOut - 0.0035, 12, true);
    b.style(grime(V3(0.46, 0.47, 0.50), w), METAL);
    b.cyl(a - V3(0, 0, 0.008), c + V3(0, 0, 0.008), 0.0088, 8, true);
    hole(b, a - V3(0, 0, 0.0081), V3(0, 0, -1), 0.0055, 6);
    hole(b, c + V3(0, 0, 0.0081), V3(0, 0, 1), 0.0055, 6);
}

// A suspension ball joint under the arm at (x, 0, z): housing, boot, tapered stud up through the arm, castle nut.
void ballJoint(B& b, double x, double z, double w, const V3& housing) {
    b.style(grime(housing, w), METAL);
    b.lathe(V3(x, 0, z), {V2(0, -0.060), V2(0.0150, -0.060), V2(0.0230, -0.052), V2(0.0285, -0.036), V2(0.0285, -0.020), V2(0.0235, -0.012), V2(0.0200, -0.004), V2(0, -0.004)}, 12, true);
    b.style(rubberCol(w), MATTE);
    latheAB(b, V3(x, -0.034, z), V3(x, 0.012, z), {V2(0.0285, 0), V2(0.0255, 0.008), V2(0.0190, 0.022), V2(0.0150, 0.040), V2(0.0140, 0.046)}, 12, true);
    b.style(grime(V3(0.50, 0.50, 0.53), w), METAL);
    latheAB(b, V3(x, 0.0, z), V3(x, 0.090, z), {V2(0.0125, 0), V2(0.0092, 0.060), V2(0.0072, 0.085), V2(0.0055, 0.090)}, 10, true);
    b.style(grime(V3(0.46, 0.46, 0.48), w), METAL);
    b.cyl(V3(x, 0.050, z), V3(x, 0.072, z), 0.0150, 6, true);
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.box(V3(x, 0.0702, z), V3(0.0045, 0.0040, 0.0300), 0);
    b.style(V3(0.62, 0.50, 0.14), METAL);
    b.cyl(V3(x - 0.0001, 0.0645, z - 0.0165), V3(x - 0.0001, 0.0645, z + 0.0165), 0.0015, 4, true);
    grease(b, V3(x - 0.02, -0.03, z), V3(x + 0.02, -0.03, z), 0.004, 4, w, 141, 0.022);
}

// The stamped steel wishbone: two box-section legs with a pressed channel, a cross-brace and a gusset.
void armStamped(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = grime(paint(p.i("paint")) * 0.9, w * 0.6);
    const V3 bush = bushCol(p.i("bush"), w);
    const double xb = -0.19, zb = 0.135, xa = 0.17, za = 0.022;
    b.style(col, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.beam(V3(xb, 0, s * zb), V3(xa, 0, s * za), 0.040, 0.030, V3(0, 1, 0), 0.0065);
        b.push();
        b.style(V3(0.02, 0.02, 0.022), MATTE);
        b.beam(V3(xb + 0.03, 0.0153, s * (zb - 0.0075)), V3(xa - 0.03, 0.0153, s * (za + 0.0015)), 0.014, 0.0008, V3(0, 1, 0), 0);
        b.pop();
        b.style(col, PAINT);
    }
    b.beam(V3(-0.06, 0, -0.0929), V3(-0.06, 0, 0.0929), 0.030, 0.024, V3(0, 1, 0), 0.005);
    // Gusset plate at the apex with a lightening hole.
    b.extrudeUp({V2(0.205, -0.034), V2(0.205, 0.034), V2(0.09, 0.060), V2(0.02, 0.045), V2(0.02, -0.045), V2(0.09, -0.060)}, 0.006, 0.0125);
    b.style(V3(0.02, 0.02, 0.022), MATTE);
    discFan(b, V3(0.075, 0.0186, 0.0), V3(0, 1, 0), 0.016, 10);
    ballJoint(b, 0.185, 0, w, V3(0.34, 0.34, 0.37));
    for (int s = -1; s <= 1; s += 2) bushing(b, xb - 0.005, 0, s * zb, 0.0245, 0.052, bush, w, V3(0.30, 0.30, 0.32));
    // Weld seams at the bushing sleeves.
    b.style(grime(V3(0.30, 0.30, 0.31), w), METAL);
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(xb - 0.004, 0, s * zb - 0.023), V3(xb - 0.004, 0, s * zb + 0.023), 0.0262, 10, false);
    stampSide(b, "R", V3(-0.10, 0, 0.1059), 0.0028, 1, 0.0005);
    grease(b, V3(-0.15, 0.015, 0.1), V3(0.1, 0.015, 0.03), 0.004, 8, w, 142, 0.0);
}

// The tubular chromoly A-arm: round tube legs welded to bushing tubes, a brace tube, a ball-joint plate.
void armTubular(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = grime(paint(p.i("paint")), w * 0.5);
    const V3 bush = bushCol(p.i("bush"), w);
    const double xb = -0.19, zb = 0.135, xa = 0.165, za = 0.024;
    b.style(col, PAINT);
    for (int s = -1; s <= 1; s += 2) b.tube({V3(xb, 0, s * zb), V3(xa, 0, s * za)}, 0.0135, 10, false, true);
    b.tube({V3(-0.055, 0, -0.0935), V3(-0.055, 0, 0.0935)}, 0.0115, 10, false, true);
    // Apex plate and gusset tabs.
    b.extrudeUp({V2(0.205, -0.030), V2(0.205, 0.030), V2(0.12, 0.034), V2(0.10, 0.010), V2(0.10, -0.010), V2(0.12, -0.034)}, 0.008, -0.004);
    for (int s = -1; s <= 1; s += 2) {
        b.extrudeUp({V2(xb + 0.01, s * (zb - 0.02)), V2(xb + 0.07, s * (zb - 0.034)), V2(xb + 0.07, s * (zb - 0.014)), V2(xb + 0.01, s * (zb + 0.0))}, 0.006, -0.003);
    }
    ballJoint(b, 0.185, 0, w, V3(0.34, 0.34, 0.37));
    for (int s = -1; s <= 1; s += 2) {
        bushing(b, xb - 0.002, 0, s * zb, 0.0235, 0.050, bush, w, col * 1.2);
    }
    b.style(grime(V3(0.30, 0.30, 0.31), w), METAL);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    grease(b, V3(-0.15, 0.012, 0.1), V3(0.1, 0.012, 0.03), 0.004, 6, w, 143, 0.0);
}

// A straight trailing link / radius arm, a bushing eye at each end.
void armLink(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = grime(paint(p.i("paint")) * 0.9, w * 0.6);
    const V3 bush = bushCol(p.i("bush"), w);
    const double xe = 0.255;
    b.style(col, PAINT);
    b.beam(V3(-xe + 0.03, 0, 0), V3(xe - 0.03, 0, 0), 0.044, 0.036, V3(0, 1, 0), 0.0085);
    // Pressed stiffening ribs on top and an indent.
    b.style(col * 0.7, PAINT);
    b.beam(V3(-0.17, 0.0185, 0), V3(0.17, 0.0185, 0), 0.016, 0.002, V3(0, 1, 0), 0.0005);
    // Eye sleeves, with gusset plates tying them to the body.
    b.style(col, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        const double x = s * xe;
        b.cyl(V3(x, 0, -0.033), V3(x, 0, 0.033), 0.0345, 12, true);
        b.extrudeUp({V2(x - s * 0.01, -0.022), V2(x - s * 0.065, -0.022), V2(x - s * 0.065, 0.022), V2(x - s * 0.01, 0.022)}, 0.004, 0.0185);
        b.extrudeUp({V2(x - s * 0.01, -0.022), V2(x - s * 0.065, -0.022), V2(x - s * 0.065, 0.022), V2(x - s * 0.01, 0.022)}, 0.004, -0.0225);
        b.style(rubberCol(w), MATTE);
        b.style(bush, MATTE);
        b.cyl(V3(x, 0, -0.0355), V3(x, 0, 0.0355), 0.0270, 12, true);
        b.style(grime(V3(0.46, 0.47, 0.5), w), METAL);
        b.cyl(V3(x, 0, -0.0445), V3(x, 0, 0.0445), 0.0108, 8, true);
        hole(b, V3(x, 0, -0.0447), V3(0, 0, -1), 0.0068, 6);
        hole(b, V3(x, 0, 0.0447), V3(0, 0, 1), 0.0068, 6);
        b.style(col, PAINT);
    }
    // A shock/sway-bar bracket on the side.
    b.extrude({V2(0.04, -0.018), V2(0.09, -0.018), V2(0.09, 0.05), V2(0.07, 0.065), V2(0.05, 0.065), V2(0.04, 0.05)}, 0.006, -0.026);
    b.style(V3(0.02, 0.02, 0.022), MATTE);
    b.cyl(V3(0.065, 0.045, -0.0285), V3(0.065, 0.045, -0.0225), 0.008, 8, true);
    grease(b, V3(-0.2, 0.019, 0), V3(0.2, 0.019, 0), 0.004, 8, w, 144, 0.0);
}

Reg controlArmReg({"control_arm", "CONTROL ARM", "REPAIR", "Spare part: suspension link; origin at the middle, ball joint / stud at +X, pivot bushings at -X, stud points up", 0.45,
                   {
                       {"STAMPED A-ARM", "Stamped steel wishbone with rubber bushings and a ball joint", {paintParam(9, "paint", "PAINT"), choice("bush", "BUSHINGS", 0, kBush), wearParam(0.3)}, lifted(armStamped)},
                       {"TUBULAR A-ARM", "Powder-coated chromoly tube A-arm with poly bushings", {paintParam(5, "paint", "COATING"), choice("bush", "BUSHINGS", 1, kBush), wearParam(0.1)}, lifted(armTubular)},
                       {"RADIUS ARM", "Straight trailing link with a bushing eye at each end", {paintParam(9, "paint", "PAINT"), choice("bush", "BUSHINGS", 0, kBush), wearParam(0.3)}, lifted(armLink)},
                   }});

// ======================================================================================
// 12. SPARK PLUG
// ======================================================================================

// One plug along X (terminal at -X, tip at +X) centred at `at`. detail 0 (cheap, for packs) .. 2.
void plugAt(B& b, const V3& at, int detail, double fouled, int shell, int reach, double w) {
    b.push();
    b.move(at);
    b.move(-0.048, 0, 0);
    const double tl = reach ? 0.019 : 0.0125;  // thread length
    const double xHex0 = 0.052, xHex1 = 0.0675, xTh0 = 0.0693, xTh1 = xTh0 + tl;
    const int sg = detail >= 2 ? 12 : detail == 1 ? 9 : 6;
    const V3 shellCol = shell == 0 ? V3(0.80, 0.82, 0.84) : shell == 1 ? V3(0.58, 0.60, 0.58) : V3(0.09, 0.09, 0.10);
    const Mat shellMat = shell == 0 ? CHROME : METAL;
    const V3 ceramic = mix(V3(0.92, 0.91, 0.86), V3(0.30, 0.27, 0.22), w * 0.35);
    // Terminal nut and stud.
    b.style(grime(V3(0.55, 0.55, 0.57), w), METAL);
    rodAB(b, V3(0.0, 0, 0), V3(0.016, 0, 0), 0.0021, 0.0004, 6);
    b.cyl(V3(0.0, 0, 0), V3(0.0045, 0, 0), 0.0046, 6, true);
    // The white insulator: terminal shoulder, ribs, then the broad lower body.
    b.style(ceramic, PAINT);
    std::vector<V2> ins = {V2(0.0045, 0.012), V2(0.0058, 0.0125), V2(0.0064, 0.017)};
    if (detail >= 1) {
        for (int i = 0; i < 4; i++) {
            const double h0 = 0.018 + i * 0.0065;
            ins.push_back(V2(0.0082, h0)), ins.push_back(V2(0.0084, h0 + 0.0042)), ins.push_back(V2(0.0066, h0 + 0.0052));
        }
    }
    ins.push_back(V2(0.0066, 0.0445));
    ins.push_back(V2(0.0092, 0.0472));
    ins.push_back(V2(0.0092, xHex0 + 0.004));
    latheAB(b, V3(0.0, 0, 0), V3(xHex0 + 0.004, 0, 0), ins, sg, true);
    // A printed colour band on the ceramic.
    b.style(paint(5), MATTE);
    b.cyl(V3(0.0432, 0, 0), V3(0.0462, 0, 0), 0.0070, sg, false);
    // The shell: rolled crimp, hex, crush washer, threads.
    b.style(grime(shellCol, w * 0.8), shellMat);
    if (detail >= 1) b.cyl(V3(xHex0 - 0.003, 0, 0), V3(xHex0, 0, 0), 0.0080, sg, true);
    b.cyl(V3(xHex0, 0, 0), V3(xHex1, 0, 0), 0.0093, 6, true);
    b.style(grime(V3(0.30, 0.30, 0.32), w), METAL);
    b.cyl(V3(xHex1, 0, 0), V3(xTh0, 0, 0), 0.0098, sg, true);
    b.style(grime(shellCol, w * 0.8) * 0.9, shell == 0 ? METAL : shellMat);
    knurl(b, V3(xTh0, 0, 0), V3(xTh1, 0, 0), 0.0071, 0.0009, detail >= 2 ? (reach ? 9 : 6) : (detail == 1 ? 4 : 2), sg, false);
    // The nose and the electrodes; soot builds up on them as the plug fouls.
    const V3 soot = V3(0.05, 0.045, 0.04);
    b.style(mix(ceramic, soot, fouled), MATTE);
    latheAB(b, V3(xTh1 - 0.002, 0, 0), V3(xTh1 + 0.0045, 0, 0), {V2(0.0050, 0), V2(0.0046, 0.0035), V2(0.0030, 0.0065)}, std::min(sg, 10), true);
    b.style(mix(V3(0.62, 0.62, 0.64), soot, fouled * 0.85), METAL);
    b.cyl(V3(xTh1 + 0.0040, 0, 0), V3(xTh1 + 0.0072, 0, 0), 0.0013, 5, true);
    const double xe = xTh1 + 0.0075;
    b.box(V3((xTh1 + xe) / 2 + 0.0006, 0.0070, 0), V3(xe - xTh1 + 0.0012, 0.0013, 0.0030), 0);
    b.box(V3(xe + 0.0012, 0.0039, 0), V3(0.0013, 0.0066, 0.0030), 0);
    if (fouled > 0.3 && detail >= 1) {
        b.style(soot, MATTE);
        b.sphere(V3(xTh1 + 0.003, 0.0035, 0.0015), V3(0.0042, 0.0020, 0.0030), 0);
    }
    b.pop();
}

void plugSingle(B& b, const P& p) {
    plugAt(b, V3(0, 0, 0), 2, p("fouled"), p.i("shell"), p.i("reach"), p("wear"));
}

// Four plugs lying side by side in a cardboard sleeve.
void plugPack(B& b, const P& p) {
    const double w = p("wear");
    for (int k = 0; k < 4; k++) plugAt(b, V3(0, 0, (k - 1.5) * 0.0215), 0, p("fouled"), p.i("shell"), p.i("reach"), w);
    const double x0 = -0.048 + 0.022, x1 = -0.048 + 0.074, wz = 0.092, h = 0.0236;
    const V3 card = grime(V3(0.66, 0.54, 0.34), w * 0.5);
    b.style(card, MATTE);
    b.box(V3((x0 + x1) / 2, h / 2, 0), V3(x1 - x0, 0.0016, wz), 0);
    b.box(V3((x0 + x1) / 2, -h / 2, 0), V3(x1 - x0, 0.0016, wz), 0);
    for (int s = -1; s <= 1; s += 2) b.box(V3((x0 + x1) / 2, 0, s * wz / 2), V3(x1 - x0, h, 0.0016), 0);
    // The printed top: a blue label panel with the name.
    b.style(paint(5) * 0.9, MATTE);
    b.box(V3((x0 + x1) / 2, h / 2 + 0.0008, 0), V3(x1 - x0 - 0.008, 0.0006, wz - 0.012), 0);
    b.style(V3(0.92, 0.90, 0.80), MATTE);
    stampTop(b, "PLUGS", V3((x0 + x1) / 2 + 0.0, h / 2 + 0.0014, 0.0), 0.0017, 0.0005);
}

// A plug beside a thin-wall plug socket with a rubber insert and a 3/8 in drive.
void plugSocket(B& b, const P& p) {
    const double w = p("wear");
    plugAt(b, V3(0, 0, 0.022), 1, p("fouled"), p.i("shell"), p.i("reach"), w);
    const double L = 0.070, x0 = -L / 2 - 0.004, zc = -0.022;
    setFin(b, 0, w);
    // The socket: an open thin-wall tube, necked down to the drive square.
    latheAB(b, V3(x0, 0, zc), V3(x0 + L, 0, zc),
            {V2(0, 0), V2(0.0085, 0), V2(0.0112, 0.006), V2(0.0118, 0.014), V2(0.0118, 0.042), V2(0.0124, 0.046), V2(0.0124, L - 0.001), V2(0.0108, L)}, 14, true);
    b.style(rubberCol(w), MATTE);
    latheAB(b, V3(x0 + 0.040, 0, zc), V3(x0 + L, 0, zc), {V2(0.0106, 0), V2(0.0106, 0.027), V2(0.0098, 0.0297), V2(0.0098, 0.0), V2(0.0106, 0)}, 12, false);
    // Looking in: dark interior and the end of the hex.
    hole(b, V3(x0 + L, 0, zc), V3(1, 0, 0), 0.0097, 10);
    hole(b, V3(x0 - 0.0005, 0, zc), V3(-1, 0, 0), 0.0036, 4);
    setFin(b, 0, w);
    stampTop(b, "16", V3(x0 + 0.021, 0.0122, zc), 0.0009, 0.0004);
}

Reg sparkPlugReg({"spark_plug", "SPARK PLUG", "REPAIR", "Spare part: a plug for the flooded-engine fix; lies along X, terminal -X, electrode +X, origin at the middle", 0.11,
                  {
                      {"STANDARD", "A single plug: white insulator, hex, threads, bent ground electrode",
                       {choice("shell", "SHELL", 0, {"NICKEL", "ZINC", "BLACK"}), choice("reach", "REACH", 1, {"SHORT", "LONG"}), num("fouled", "FOULED", 0.0, 0, 1, 0.05), wearParam(0.1)}, lifted(plugSingle)},
                      {"PACK OF FOUR", "Four plugs in a cardboard sleeve",
                       {choice("shell", "SHELL", 0, {"NICKEL", "ZINC", "BLACK"}), choice("reach", "REACH", 1, {"SHORT", "LONG"}), num("fouled", "FOULED", 0.0, 0, 1, 0.05), wearParam(0.1)}, lifted(plugPack)},
                      {"PLUG AND SOCKET", "A plug with its thin-wall plug socket",
                       {choice("shell", "SHELL", 0, {"NICKEL", "ZINC", "BLACK"}), choice("reach", "REACH", 1, {"SHORT", "LONG"}), num("fouled", "FOULED", 0.0, 0, 1, 0.05), wearParam(0.1)}, lifted(plugSocket)},
                  }});

// ======================================================================================
// 15. LUG NUT
// ======================================================================================

// Axis along X: the conical seat towards -X (the wheel), the hex, the nose towards +X.
void lugAcorn(B& b, const P& p) {
    const double w = p("wear");
    setFin(b, p.i("finish"), w);
    const double hx = 0.016, seat = 0.0105, dome = 0.0055;
    const double x0 = -(hx + seat + dome) / 2;
    latheAB(b, V3(x0, 0, 0), V3(x0 + seat, 0, 0), {V2(0.0060, 0), V2(0.0108, seat)}, 8, true);   // the 60 degree seat
    b.cyl(V3(x0 + seat, 0, 0), V3(x0 + seat + hx, 0, 0), 0.0124, 6, true);
    latheAB(b, V3(x0 + seat + hx, 0, 0), V3(x0 + seat + hx + dome, 0, 0), {V2(0.0104, 0), V2(0.0092, 0.0028), V2(0.0056, 0.0050), V2(0, 0.0055)}, 8, true);
    hole(b, V3(x0, 0, 0), V3(-1, 0, 0), 0.0040, 6);
}

// A slim spline-drive (tuner) nut: a long fluted body with a flared seat.
void lugSpline(B& b, const P& p) {
    const double w = p("wear");
    setFin(b, p.i("finish"), w);
    const double L = 0.034;
    const double x0 = -L / 2;
    latheAB(b, V3(x0, 0, 0), V3(x0 + L, 0, 0),
            {V2(0.0058, 0), V2(0.0124, 0.0085), V2(0.0124, 0.0100), V2(0.0086, 0.0112), V2(0.0086, L - 0.0025), V2(0.0078, L - 0.0008), V2(0, L)}, 8, false);
    hole(b, V3(x0, 0, 0), V3(-1, 0, 0), 0.0040, 6);
    b.style(darkSteel(w), METAL);
    b.cyl(V3(x0 + L - 0.0003, 0, 0), V3(x0 + L + 0.0004, 0, 0), 0.0030, 5, true);
}

Reg lugNutReg({"lug_nut", "LUG NUT", "REPAIR", "Spare part: one wheel lug nut, ~3 cm; axis along X, conical seat towards -X, origin at its centre", 0.04,
               {
                   {"ACORN NUT", "Closed acorn lug nut with a 60 degree conical seat", {finParam(0), wearParam(0.1)}, lifted(lugAcorn)},
                   {"SPLINE NUT", "Slim black spline-drive lug nut with a flared seat", {finParam(1), wearParam(0.1)}, lifted(lugSpline)},
               }});

// ======================================================================================
// 14. FIRST-PERSON HAND (the player's right forearm and gloved hand, gripping a tool)
// ======================================================================================

// kind 0 leather work glove, 1 mechanic's nitrile glove, 2 bare hand.
void fpHandBuild(B& b, const P& p, int kind) {
    const double w = p("wear");
    const V3 jacket = paint(p.i("jacket")) * 0.85;
    V3 skin;
    if (kind == 0)
        skin = grime(V3(0.66, 0.48, 0.27), w * 0.8);
    else if (kind == 1)
        skin = p.i("glove") ? V3(0.06, 0.06, 0.07) : V3(0.10, 0.26, 0.64);
    else {
        static const V3 tones[4] = {V3(0.86, 0.66, 0.52), V3(0.76, 0.55, 0.38), V3(0.55, 0.37, 0.24), V3(0.36, 0.24, 0.16)};
        skin = grime(tones[std::clamp(p.i("skin"), 0, 3)], w * 0.4);
    }
    if (kind == 1) skin = grime(skin, w * 0.5);
    b.style(skin, MATTE);
    // The four fingers, wrapped round the grip from the knuckles over the right side.
    const double fx[4] = {0.0335, 0.0125, -0.0085, -0.0270}, fr[4] = {0.0098, 0.0100, 0.0094, 0.0082};
    for (int k = 0; k < 4; k++) {
        std::vector<V3> path;
        for (int j = 0; j <= 6; j++) {
            const double t = j / 6.0, phi = (8 + 182 * t) * DEG;
            const double R = 0.0305 - 0.0070 * t * t - (k == 3 ? 0.002 : 0);
            path.push_back(V3(fx[k], R * std::cos(phi), -R * std::sin(phi)));
        }
        b.tube(path, fr[k], 6, false, true);
        b.sphere(path.back(), V3(fr[k], fr[k], fr[k]), 0);
    }
    // Thumb along the left of the grip; the palm under it; the back of the hand over the knuckles.
    b.tube({V3(-0.030, -0.014, 0.030), V3(0.0, -0.007, 0.0345), V3(0.030, 0.0, 0.0350), V3(0.056, 0.006, 0.0305)}, 0.0112, 6, false, true);
    b.sphere(V3(0.056, 0.006, 0.0305), V3(0.0112, 0.0112, 0.0112), 0);
    b.sphere(V3(-0.014, -0.010, 0.026), V3(0.040, 0.026, 0.020), 1);
    b.sphere(V3(-0.034, 0.022, -0.006), V3(0.040, 0.016, 0.026), 1);
    for (int k = 0; k < 4; k++) b.sphere(V3(fx[k], 0.0325, -0.0045), V3(fr[k] * 1.05, fr[k] * 0.95, fr[k] * 1.1), 0);  // knuckles
    // Glove details.
    if (kind == 0) {
        b.style(grime(V3(0.36, 0.25, 0.13), w * 0.8), MATTE);
        b.box(V3(0.004, 0.0414, -0.006), V3(0.080, 0.0055, 0.034), 0.0022);  // the knuckle pad
        b.style(grime(V3(0.30, 0.20, 0.10), w), MATTE);
        for (int k = 0; k < 3; k++) b.box(V3((fx[k] + fx[k + 1]) / 2, 0.0392, -0.002), V3(0.0012, 0.0012, 0.024), 0);  // finger seams
    }
    // The wrist/cuff of the glove (or the bare wrist), then the jacket sleeve running back and down.
    const V3 a(-0.050, -0.004, 0.004), d = norm(V3(-1, -0.24, -0.10));
    b.style(skin, MATTE);
    latheAB(b, a, a + d * 0.075,
            kind == 0 ? std::vector<V2>{V2(0.030, 0), V2(0.0345, 0.030), V2(0.0465, 0.052), V2(0.0450, 0.075)} : std::vector<V2>{V2(0.030, 0), V2(0.0325, 0.030), V2(0.0340, 0.052), V2(0.0345, 0.075)}, 12, true);
    if (kind == 1) {  // the nitrile cuff roll
        b.style(skin * 0.85, MATTE);
        latheAB(b, a + d * 0.050, a + d * 0.066, {V2(0.0340, 0), V2(0.0372, 0.004), V2(0.0372, 0.012), V2(0.0340, 0.016)}, 12, true);
    }
    // Knit cuff and the sleeve with a couple of folds.
    b.style(jacket * 0.62, MATTE);
    latheAB(b, a + d * 0.058, a + d * 0.098, {V2(0.0420, 0), V2(0.0455, 0.003), V2(0.0455, 0.037), V2(0.0480, 0.040)}, 12, true);
    b.style(jacket, MATTE);
    latheAB(b, a + d * 0.092, a + d * 0.34,
            {V2(0.0495, 0), V2(0.0535, 0.030), V2(0.0515, 0.060), V2(0.0575, 0.095), V2(0.0555, 0.130), V2(0.0600, 0.180), V2(0.0605, 0.230), V2(0.0600, 0.248), V2(0, 0.248)}, 12, true);
    // Dirt and grease.
    grease(b, V3(-0.04, 0.036, -0.012), V3(0.04, 0.036, -0.012), 0.004, 5, w, 151, 0.0);
    grease(b, a + d * 0.12 + V3(0, 0.05, 0), a + d * 0.3 + V3(0, 0.05, 0), 0.006, 4, w, 152, 0.0);
}
void fpHandLeather(B& b, const P& p) { fpHandBuild(b, p, 0); }
void fpHandNitrile(B& b, const P& p) { fpHandBuild(b, p, 1); }
void fpHandBare(B& b, const P& p) { fpHandBuild(b, p, 2); }

Reg fpHandReg({"fp_hand", "FP HAND", "REPAIR", "First-person right forearm and hand gripping a tool: fist round the X axis at the origin, knuckles up (+Y), thumb on +Z, forearm back along -X and slightly down", 0.5,
               {
                   {"WORK GLOVE", "Tan leather work glove, jacket sleeve", {paintParam(4, "jacket", "JACKET"), wearParam(0.25)}, lifted(fpHandLeather)},
                   {"NITRILE", "Mechanic's nitrile glove", {paintParam(4, "jacket", "JACKET"), choice("glove", "GLOVE", 0, {"BLUE", "BLACK"}), wearParam(0.2)}, lifted(fpHandNitrile)},
                   {"BARE HAND", "Bare hand", {paintParam(4, "jacket", "JACKET"), choice("skin", "SKIN", 1, {"LIGHT", "TAN", "BROWN", "DARK"}), wearParam(0.1)}, lifted(fpHandBare)},
               }});

// ======================================================================================
// 1. LUG WRENCH
// ======================================================================================

// The 4-way: two crossed bars with a socket on each end, a grip sleeve on one arm.
void crossWrench(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    b.move(0.1175, 0, 0);  // the grip (the sleeve on the -X arm) sits at the origin
    const double arm = 0.19, sl = 0.044;
    setFin(b, fin, w);
    b.lathe(V3(0, 0, 0), {V2(0, -0.0115), V2(0.0215, -0.0115), V2(0.026, -0.0075), V2(0.026, 0.0075), V2(0.0215, 0.0115), V2(0, 0.0115)}, 14, false);
    const double rs[4] = {0.0125, 0.0135, 0.0145, 0.0152};
    const V3 dirs[4] = {V3(1, 0, 0), V3(0, 0, 1), V3(-1, 0, 0), V3(0, 0, -1)};
    for (int k = 0; k < 4; k++) {
        setFin(b, fin, w);
        // (Each socket stands on its arm's end, open side down: the way it goes onto a nut.)
        const V3 at = dirs[k] * (arm - rs[k]);
        rodAB(b, dirs[k] * 0.02, at, 0.0085, 0.001, 8);
        socketAB(b, at + V3(0, 0.013, 0), at + V3(0, 0.013 - sl, 0), rs[k], rs[k] * 0.72, 10);
    }
    // The grip sleeve on the butt arm.
    b.style(paint(p.i("grip")), MATTE);
    knurl(b, V3(-0.065, 0, 0), V3(-0.172, 0, 0), 0.0128, 0.0014, 7, 8, false);
    rodAB(b, V3(-0.172, 0, 0), V3(-0.18, 0, 0), 0.0128, 0.0025, 10);
    b.style(darkSteel(w), METAL);
    stampTop(b, "4-WAY", V3(0, 0.0118, 0), 0.0011);
    grease(b, V3(0.05, 0.005, 0), V3(0.17, 0.005, 0), 0.0025, 8, w, 11);
    grease(b, V3(0, 0.005, 0.05), V3(0, 0.005, -0.15), 0.0025, 8, w, 12);
}

// The factory L: a bent bar with a socket at the end of the short leg and a prying spade at the butt.
void lWrench(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    setFin(b, fin, w);
    const double x0 = -0.19, xb = 0.17, R = 0.032, r = 0.0088;
    // Long leg (a lathe, tapering into the spade), the bend, the short leg.
    latheAB(b, V3(xb, 0, 0), V3(x0, 0, 0), {V2(0, 0), V2(r, 0), V2(r, xb - x0 - 0.03), V2(0.0055, xb - x0 - 0.002), V2(0, xb - x0)}, 12, true);
    std::vector<V3> path = {V3(xb - 0.002, 0, 0)};
    for (int k = 0; k <= 6; k++) {
        const double t = 90.0 * k / 6 * DEG;
        path.push_back(V3(xb + R * std::sin(t), -(R - R * std::cos(t)), 0));
    }
    path.push_back(V3(xb + R, -(R + 0.03), 0));
    b.tube(path, r, 10, false, true);
    socketAB(b, V3(xb + R, -0.082, 0), V3(xb + R, -0.134, 0), 0.0152, 0.011, 14);  // (open side down, like the rest)
    // The spade: flat, with a hubcap-lifting lip.
    b.extrudeUp({V2(-0.18, -0.0085), V2(-0.215, -0.0155), V2(-0.255, -0.0165), V2(-0.276, -0.011), V2(-0.279, -0.003), V2(-0.262, 0.001), V2(-0.262, 0.0075), V2(-0.25, 0.0095), V2(-0.215, 0.0105), V2(-0.18, 0.0085)}, 0.0075, -0.00375);
    // A dipped grip over the long leg.
    b.style(paint(p.i("grip")), MATTE);
    knurl(b, V3(0.075, 0, 0), V3(-0.105, 0, 0), 0.0112, 0.0012, 8, 8, false);
    b.style(darkSteel(w), METAL);
    stampTop(b, "19", V3(-0.228, 0.00395, -0.002), 0.0014, 0.0008);
    grease(b, V3(0.1, 0.006, 0.0), V3(0.17, 0.006, 0.0), 0.0025, 6, w, 21);
}

// The breaker bar: a long knurled bar, a flex head with a square drive and a deep socket.
void breakerBar(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    static const double Ls[3] = {0.46, 0.60, 0.76};
    const double L = Ls[std::clamp(p.i("length"), 0, 2)];
    const double xh = 0.22, x0 = xh - L, r = 0.0108;
    setFin(b, fin, w);
    // The bar, its neck up to the head, a rounded butt.
    latheAB(b, V3(x0, 0, 0), V3(xh - 0.012, 0, 0),
            {V2(0, 0), V2(r - 0.004, 0), V2(r, 0.004), V2(r, L - 0.05), V2(r + 0.0025, L - 0.036), V2(0.0185, L - 0.013), V2(0.0195, L - 0.012)}, 12, true);
    b.style(rubberCol(w), MATTE);
    knurl(b, V3(x0 + 0.045, 0, 0), V3(x0 + 0.3 + (L - 0.6) * 0.5, 0, 0), r + 0.0008, 0.0014, 12, 10, false);
    setFin(b, 0, w);
    // The flex head: a pivot block, its drive and a deep socket underneath.
    b.lathe(V3(xh, 0, 0), {V2(0, -0.0165), V2(0.0185, -0.0165), V2(0.0215, -0.0135), V2(0.0215, 0.0135), V2(0.0185, 0.0165), V2(0, 0.0165)}, 18, false);
    b.cyl(V3(xh, 0.0165, 0), V3(xh, 0.0215, 0), 0.0072, 6);
    b.style(darkSteel(w), METAL);
    b.cyl(V3(xh, -0.0165, 0), V3(xh, -0.03, 0), 0.0093, 4);
    setFin(b, 0, w);
    b.lathe(V3(xh, -0.082, 0), {V2(0, 0), V2(0.0128, 0), V2(0.0148, 0.004), V2(0.0148, 0.036), V2(0.0132, 0.044), V2(0.0122, 0.05), V2(0.0122, 0.0545), V2(0, 0.0545)}, 14, true);
    hole(b, V3(xh, -0.082, 0), V3(0, -1, 0), 0.0105, 6);
    b.style(darkSteel(w), METAL);
    stampTop(b, "1/2", V3(xh, 0.0168, 0), 0.0016, 0.0008);
    grease(b, V3(xh - 0.03, 0.01, 0), V3(xh - 0.2, 0.01, 0), 0.0028, 7, w, 31, 0.008);
}

Reg lugWrench({"lug_wrench", "LUG WRENCH", "REPAIR", "Hand tool: wheel nuts on and off; grip at the origin, the working socket at the +X end, open side down (-Y)", 0.45,
               {
                   {"4-WAY CROSS", "Chrome cross wrench, four sockets, one grip sleeve", {finParam(0), paintParam(8, "grip", "GRIP SLEEVE"), wearParam()}, lifted(crossWrench)},
                   {"FACTORY L", "Bent bar, socket on the short leg, hubcap spade", {finParam(2), paintParam(3, "grip", "GRIP DIP"), wearParam()}, lifted(lWrench)},
                   {"BREAKER BAR", "Long knurled bar, flex head, deep socket",
                    {finParam(1), choice("length", "LENGTH", 1, {"18 IN", "24 IN", "30 IN"}), wearParam()}, lifted(breakerBar)},
               }});

// ======================================================================================
// 2. WRENCH (spanners)
// ======================================================================================

const std::vector<std::string> kMM = {"10 MM", "13 MM", "17 MM", "19 MM", "22 MM"};
const double kMMv[5] = {10, 13, 17, 19, 22};

// Open end at +X, 12-point ring at -X, a tilted head and a waisted shaft: the chrome combination spanner.
void comboWrench(B& b, const P& p) {
    const double w = p("wear");
    const double mm = kMMv[std::clamp(p.i("size"), 0, 4)];
    const double g = mm / 1000, L = 0.06 + 0.00947 * mm;
    const double Ro = g * 0.62 + 0.0105, hr = mm * 0.5176 / 1000, Rr = hr + 0.0066;
    const double xr = -L / 2 + Rr, xh = L / 2 - Ro * 0.96, tilt = 15;
    setFin(b, p.i("finish"), w);
    // Ring end.
    flatRing(b, V3(xr, 0, 0), hr, Rr, 0.0100, 24, false);
    // Waisted shaft, as a stepped (bevelled) pair of plates.
    const double hw0 = Rr * 0.62, hw1 = 0.0078, hw2 = Ro * 0.58;
    b.extrudeUp({V2(xr, hw0), V2(xr * 0.35, hw1 + 0.0006), V2(0, hw1), V2(xh * 0.5, hw1 + 0.0006), V2(xh, hw2), V2(xh, -hw2), V2(xh * 0.5, -hw1 - 0.0006), V2(0, -hw1), V2(xr * 0.35, -hw1 - 0.0006), V2(xr, -hw0)},
                0.0072, -0.0036);
    b.extrudeUp({V2(xr, hw0 - 0.0015), V2(0, hw1 - 0.0016), V2(xh, hw2 - 0.0015), V2(xh, -hw2 + 0.0015), V2(0, -hw1 + 0.0016), V2(xr, -hw0 + 0.0015)}, 0.0088, -0.0044);
    // The open-end head, tilted off the shaft.
    b.extrudeUp(xform(openHead(Ro, g, 0.0), tilt, xh, 0), 0.0090, -0.0045);
    b.extrudeUp(xform(openHead(Ro * 0.93, g, 0.0), tilt, xh, 0), 0.0104, -0.0052);
    b.style(darkSteel(w), METAL);
    stampTop(b, std::to_string((int)mm), V3(-0.012, 0.0045, 0), 0.0013, 0.0007);
    grease(b, V3(xr, 0.0055, 0), V3(xh, 0.0055, 0), 0.0022, 7, w, 41, 0.004);
}

// The crescent: a fat head with a fixed jaw and a sliding jaw, the worm thumbwheel behind it, a dipped grip.
void crescent(B& b, const P& p) {
    const double w = p("wear");
    static const double Ss[3] = {0.75, 1.0, 1.25};
    const int si = std::clamp(p.i("size"), 0, 2);
    const double s = Ss[si], L = 0.2 * s;
    const double Ro = 0.0255 * s, g = (0.007 + 0.016 * p("open")) * s;
    const double xh = L / 2 - Ro, x0 = -L / 2, th = 0.0105 * s;
    b.move(0.015 * s, 0, 0);
    setFin(b, p.i("finish"), w);
    // Neck + head: thick forged body.
    b.extrudeUp({V2(xh - Ro * 1.9, 0.0075 * s), V2(xh - Ro * 0.4, 0.0135 * s), V2(xh - Ro * 0.4, -0.0135 * s), V2(xh - Ro * 1.9, -0.0075 * s)}, th * 0.8, -th * 0.4);
    b.extrudeUp(xform(openHead(Ro, g, 0.0), 0, xh, 0), th, -th / 2);
    b.extrudeUp(xform(openHead(Ro * 0.94, g, 0.0), 0, xh, 0), th + 0.0018, -(th + 0.0018) / 2);
    // The sliding jaw on the left side, a different tone and standing proud.
    std::vector<V2> jp;
    const double a0 = std::asin(g / 2 / Ro);
    for (int i = 0; i <= 5; i++) {
        const double t = a0 + (78 * DEG - a0) * i / 5;
        jp.push_back(V2(xh + (Ro + 0.0010) * std::cos(t), (Ro + 0.0010) * std::sin(t)));
    }
    jp.push_back(V2(xh + 0.003 * s, Ro + 0.001));
    jp.push_back(V2(xh + 0.003 * s, g / 2));
    b.color(finish(p.i("finish"), w).c * 0.78);
    b.extrudeUp(jp, th + 0.0045, -(th + 0.0045) / 2);
    // The thumbwheel (axis across the jaws), poking out above and below the head.
    b.style(darkSteel(w), METAL);
    knurl(b, V3(xh - 0.0125 * s, 0, -0.0085 * s), V3(xh - 0.0125 * s, 0, 0.0085 * s), 0.0098 * s, 0.0013, 9, 12, false);
    // The handle: a steel core inside a dipped grip.
    setFin(b, p.i("finish"), w);
    b.extrudeUp({V2(xh - Ro * 1.6, 0.0072 * s), V2(x0 + 0.01 * s, 0.0062 * s), V2(x0, 0), V2(x0 + 0.01 * s, -0.0062 * s), V2(xh - Ro * 1.6, -0.0072 * s)}, th * 0.7, -th * 0.35);
    b.style(paint(p.i("grip")), MATTE);
    const double gx0 = x0 + 0.002, gx1 = xh - Ro - 0.008 * s;
    b.beam(V3(gx0 + 0.006, 0, 0), V3(gx1, 0, 0), 0.0215 * s, 0.0125 * s, V3(0, 1, 0), 0.0045 * s);
    b.cyl(V3(gx0 + 0.001, 0, 0), V3(gx0 + 0.01, 0, 0), 0.0085 * s, 10);
    grease(b, V3(xh - 0.03, 0.007, 0), V3(xh, 0.007, 0), 0.0022, 6, w, 43, 0.004);
}

// The offset double ring spanner: a 12-point ring at each end, cranked down from the shaft.
void ringSpanner(B& b, const P& p) {
    const double w = p("wear");
    static const double A[3] = {10, 13, 17}, B2[3] = {13, 17, 19}, Ls[3] = {0.17, 0.22, 0.27};
    const int k = std::clamp(p.i("size"), 0, 2);
    const double m1 = A[k], m2 = B2[k], L = Ls[k];
    const double hr1 = m1 * 0.5176 / 1000, hr2 = m2 * 0.5176 / 1000, R1 = hr1 + 0.0064, R2 = hr2 + 0.0066;
    const double x1 = -L / 2 + R1, x2 = L / 2 - R2, crank = 0.0115;
    setFin(b, p.i("finish"), w);
    flatRing(b, V3(x1, -crank, 0), hr1, R1, 0.0095, 24, false);
    flatRing(b, V3(x2, -crank, 0), hr2, R2, 0.0100, 24, false);
    const double xs1 = x1 + R1 + 0.022, xs2 = x2 - R2 - 0.022;
    b.beam(V3(xs1, 0, 0), V3(xs2, 0, 0), 0.0155, 0.0078, V3(0, 1, 0), 0.0022);
    b.beam(V3(xs1 + 0.002, 0, 0), V3(x1 + R1 * 0.7, -crank, 0), 0.0150, 0.0078, V3(0, 1, 0), 0.0022);
    b.beam(V3(xs2 - 0.002, 0, 0), V3(x2 - R2 * 0.7, -crank, 0), 0.0155, 0.0078, V3(0, 1, 0), 0.0022);
    b.style(darkSteel(w), METAL);
    stampTop(b, std::to_string((int)m1), V3(xs1 + 0.012, 0.0040, 0), 0.0012, 0.0007);
    stampTop(b, std::to_string((int)m2), V3(xs2 - 0.032, 0.0040, 0), 0.0012, 0.0007);
    grease(b, V3(xs1, 0.004, 0), V3(xs2, 0.004, 0), 0.0022, 6, w, 47, 0.004);
}

Reg wrenchReg({"wrench", "WRENCH", "REPAIR", "Hand tool: spanners for bolts and nuts; right hand, grip at the origin, jaws +X", 0.28,
               {
                   {"COMBINATION", "Chrome combination spanner: open end and 12-point ring", {finParam(0), choice("size", "SIZE", 3, kMM), wearParam()}, lifted(comboWrench)},
                   {"ADJUSTABLE", "Crescent wrench with a worm-screw jaw", {finParam(0), paintParam(4, "grip", "GRIP DIP"), choice("size", "SIZE", 1, {"6 IN", "8 IN", "10 IN"}), num("open", "JAW OPEN", 0.6, 0, 1), wearParam()}, lifted(crescent)},
                   {"RING SPANNER", "Offset double box-end spanner", {finParam(0), choice("size", "SIZE", 1, {"10 + 13", "13 + 17", "17 + 19"}), wearParam()}, lifted(ringSpanner)},
               }});

// ======================================================================================
// 3. HAMMER
// ======================================================================================

// A curved strip along a centre line (in x, z), offset from wi to wo on one side: a claw prong.
std::vector<V2> curvedStrip(const std::vector<V2>& c, const std::vector<double>& wi, const std::vector<double>& wo, double side) {
    const int n = (int)c.size();
    std::vector<V2> out, back;
    for (int i = 0; i < n; i++) {
        const V2 a = c[std::max(0, i - 1)], d = c[std::min(n - 1, i + 1)];
        double tx = d.x - a.x, tz = d.y - a.y;
        const double l = std::hypot(tx, tz);
        tx /= l, tz /= l;
        const double nx = -tz * side, nz = tx * side;
        out.push_back(V2(c[i].x + nx * wo[i], c[i].y + nz * wo[i]));
        back.push_back(V2(c[i].x + nx * wi[i], c[i].y + nz * wi[i]));
    }
    for (int i = n - 1; i >= 0; i--) out.push_back(back[i]);
    return out;
}

void handleWood(B& b, int kind, double w) {
    if (kind == 0)
        b.style(grime(V3(0.54, 0.38, 0.21), w * 0.8), WOOD);
    else if (kind == 1)
        b.style(grime(V3(0.92, 0.45, 0.04), w * 0.8), PAINT);
    else
        b.style(rubberCol(w), MATTE);
}

void clawHammer(B& b, const P& p) {
    const double w = p("wear");
    const int hk = p.i("handle");
    const double hs = p.i("size") ? 1.12 : 1.0;
    const double xh = 0.19, x0 = -0.14;
    // Handle: an oval hickory (or fibreglass / rubber-covered) shaft, flared at the butt.
    handleWood(b, hk, w);
    b.beam(V3(x0 + 0.008, 0, 0), V3(x0 + 0.06, 0, 0), 0.033, 0.025, V3(0, 1, 0), 0.0085);
    b.beam(V3(x0 + 0.06, 0, 0), V3(0.06, 0, 0), 0.0275, 0.0205, V3(0, 1, 0), 0.0075);
    b.beam(V3(0.06, 0, 0), V3(xh + 0.0176 * hs, 0, 0), 0.0230, 0.0185, V3(0, 1, 0), 0.0065);
    if (hk == 1) {  // black rubber grip over the fibreglass
        b.style(rubberCol(w), MATTE);
        knurl(b, V3(x0 + 0.03, 0, 0), V3(0.045, 0, 0), 0.0148, 0.0012, 12, 8, false);
        b.box(V3(x0 + 0.004, 0, 0), V3(0.008, 0.026, 0.034), 0.008);
    }
    if (hk == 0) {  // tape-wrapped grip suggestion: a band of fine rings
        b.style(grime(V3(0.30, 0.20, 0.12), w), MATTE);
        knurl(b, V3(x0 + 0.03, 0, 0), V3(0.035, 0, 0), 0.0132, 0.0007, 9, 8, false);
    }
    // The head: forged steel, eye round the handle, a bell face on +Z and a curved claw on -Z.
    const double c = hs;
    setFin(b, 3, w * 0.7);
    b.color(grime(V3(0.46, 0.47, 0.49), w * 0.6));
    b.box(V3(xh, 0, 0), V3(0.032 * c, 0.034 * c, 0.036 * c), 0.009 * c);
    latheAB(b, V3(xh, 0, 0.012 * c), V3(xh, 0, 0.056 * c),
            {V2(0, 0), V2(0.0150 * c, 0), V2(0.0155 * c, 0.012 * c), V2(0.0148 * c, 0.034 * c), V2(0.0135 * c, 0.042 * c), V2(0.0112 * c, 0.044 * c), V2(0, 0.044 * c)}, 14, true);
    // The claw: two prongs side by side (stacked in Y), curving back towards the handle.
    std::vector<V2> cl;
    std::vector<double> wi, wo;
    for (int i = 0; i <= 9; i++) {
        const double t = i / 9.0;
        cl.push_back(V2(xh - 0.050 * c * t * t, -0.010 * c - 0.092 * c * t));
        wo.push_back(0.0135 * c * (1 - 0.72 * t) + 0.0012);
        wi.push_back(-wo.back());
    }
    const double gap = 0.0048 * c, ph = 0.0100 * c;
    b.extrudeUp(curvedStrip(cl, wi, wo, 1), ph, gap / 2);
    b.extrudeUp(curvedStrip(cl, wi, wo, 1), ph, -gap / 2 - ph);
    // The wedge across the handle end, and a steel ring collar.
    b.style(darkSteel(w), METAL);
    b.box(V3(xh + 0.0182 * c, 0, 0), V3(0.0016, 0.019, 0.0032), 0.0004);
    stampSide(b, "16 OZ", V3(xh, 0, 0.0181 * c), 0.0017 * c, 1, 0.0006);
    grease(b, V3(xh - 0.03, 0.008, 0), V3(xh + 0.01, 0.008, 0), 0.0022, 5, w, 51, 0.005);
}

void clubHammer(B& b, const P& p) {
    const double w = p("wear");
    const int hk = p.i("handle");
    static const double Hs[3] = {0.9, 1.0, 1.14};
    const double hs = Hs[std::clamp(p.i("weight"), 0, 2)];
    const double xh = 0.175, x0 = -0.125;
    handleWood(b, hk, w);
    b.beam(V3(x0 + 0.008, 0, 0), V3(x0 + 0.05, 0, 0), 0.036, 0.029, V3(0, 1, 0), 0.01);
    b.beam(V3(x0 + 0.05, 0, 0), V3(xh + 0.0215 * hs, 0, 0), 0.0300, 0.0245, V3(0, 1, 0), 0.0085);
    b.style(rubberCol(w), MATTE);
    knurl(b, V3(x0 + 0.03, 0, 0), V3(0.03, 0, 0), 0.0170, 0.0012, 12, 8, false);
    // Double-faced octagonal head, flared at both faces.
    setFin(b, p.i("finish"), w);
    const double L = 0.11 * hs, a = 0.0;
    latheAB(b, V3(xh, 0, -L / 2), V3(xh, 0, L / 2),
            {V2(0, a), V2(0.0185 * hs, a), V2(0.0215 * hs, 0.006), V2(0.0182 * hs, 0.020 * hs), V2(0.0162 * hs, 0.040 * hs), V2(0.0162 * hs, L - 0.040 * hs), V2(0.0182 * hs, L - 0.020 * hs),
             V2(0.0215 * hs, L - 0.006), V2(0.0185 * hs, L), V2(0, L)},
            8, false);
    b.box(V3(xh, 0, 0), V3(0.036 * hs, 0.034 * hs, 0.05 * hs), 0.008);  // the eye block
    b.style(darkSteel(w), METAL);
    b.box(V3(xh + 0.0228 * hs, 0, 0), V3(0.0016, 0.024, 0.0032), 0.0004);  // the wedge
    grease(b, V3(xh - 0.02, 0.012, 0), V3(xh + 0.02, 0.012, 0), 0.0022, 4, w, 52, 0.004);
}

void deadBlow(B& b, const P& p) {
    const double w = p("wear");
    const double hs = p.i("size") ? 1.18 : 1.0;
    const double xh = 0.175, x0 = -0.14;
    // A steel shaft inside a rubber grip, flared butt with a lanyard hole.
    setFin(b, 3, w);
    rodAB(b, V3(0.04, 0, 0), V3(xh - 0.03, 0, 0), 0.0105, 0.001, 12);
    b.style(paint(p.i("grip")) * (1 - w * 0.4), MATTE);
    knurl(b, V3(x0 + 0.02, 0, 0), V3(0.045, 0, 0), 0.0158, 0.0014, 14, 10, false);
    latheAB(b, V3(x0, 0, 0), V3(x0 + 0.025, 0, 0), {V2(0, 0), V2(0.0185, 0), V2(0.0198, 0.006), V2(0.0172, 0.022), V2(0.0158, 0.025)}, 12, true);
    hole(b, V3(x0, 0, 0), V3(-1, 0, 0), 0.006, 8);
    b.style(rubberCol(w), MATTE);
        // The head: an orange polyurethane body, black rubber faces.
    const double L = 0.145 * hs;
    b.style(paint(p.i("head")) * (1 - w * 0.45), MATTE);
    latheAB(b, V3(xh, 0, -L / 2 + 0.036 * hs), V3(xh, 0, L / 2 - 0.036 * hs),
            {V2(0, 0), V2(0.0338 * hs, 0), V2(0.0348 * hs, 0.003), V2(0.0348 * hs, L - 0.072 * hs - 0.003), V2(0.0338 * hs, L - 0.072 * hs), V2(0, L - 0.072 * hs)}, 18, true);
    b.style(rubberCol(w), MATTE);
    for (int s = -1; s <= 1; s += 2) {
        const V3 a(xh, 0, s * (L / 2 - 0.037 * hs)), c(xh, 0, s * L / 2);
        latheAB(b, a, c, {V2(0, 0), V2(0.0345 * hs, 0), V2(0.0368 * hs, 0.004), V2(0.0368 * hs, 0.032 * hs), V2(0.0345 * hs, 0.037 * hs), V2(0, 0.037 * hs)}, 18, true);
    }
    // The rubber boss where the handle enters the head.
    b.style(rubberCol(w), MATTE);
    latheAB(b, V3(xh - 0.038 * hs, 0, 0), V3(xh - 0.030 * hs, 0, 0), {V2(0, 0), V2(0.0185, 0), V2(0.0185, 0.008), V2(0, 0.008)}, 12, false);
}

Reg hammerReg({"hammer", "HAMMER", "REPAIR", "Hand tool: bang things straight or loose; right hand, grip at the origin, head at +X (face on +Z)", 0.4,
               {
                   {"CLAW HAMMER", "Forged claw hammer on a flared hickory handle",
                    {choice("handle", "HANDLE", 0, {"HICKORY", "FIBREGLASS", "RUBBER"}), choice("size", "WEIGHT", 0, {"16 OZ", "20 OZ"}), wearParam()}, lifted(clawHammer)},
                   {"CLUB HAMMER", "Short, heavy double-faced club hammer",
                    {finParam(3), choice("handle", "HANDLE", 0, {"HICKORY", "FIBREGLASS", "RUBBER"}), choice("weight", "WEIGHT", 1, {"2 LB", "3 LB", "4 LB"}), wearParam()}, lifted(clubHammer)},
                   {"DEAD-BLOW MALLET", "Orange polyurethane head, black rubber faces, shot-loaded",
                    {paintParam(4, "head", "HEAD"), paintParam(3, "grip", "GRIP"), choice("size", "SIZE", 0, {"16 OZ", "32 OZ"}), wearParam()}, lifted(deadBlow)},
               }});

// ======================================================================================
// 4. RATCHET
// ======================================================================================

// A ratchet head at (xh,0,0) with its reversing lever and release button, the drive, a socket under it.
void ratchetHead(B& b, double xh, double R, double T, double w, int fin, double sockR, double sockL, double driveW) {
    setFin(b, fin, w);
    b.lathe(V3(xh, 0, 0), {V2(0, -T / 2), V2(R * 0.84, -T / 2), V2(R, -T / 2 + 0.003), V2(R, T / 2 - 0.003), V2(R * 0.84, T / 2), V2(0, T / 2)}, 16, true);
    // Release button and lever on top.
    b.style(darkSteel(w), METAL);
    b.cyl(V3(xh, T / 2, 0), V3(xh, T / 2 + 0.0035, 0), R * 0.34, 10);
    b.box(V3(xh + R * 0.18, T / 2 + 0.0012, R * 0.1), V3(R * 0.65, 0.0026, R * 0.62), 0.001);
    b.cyl(V3(xh + R * 0.18, T / 2 + 0.0, R * 0.62), V3(xh + R * 0.18, T / 2 + 0.0045, R * 0.62), R * 0.16, 6);
    // The drive and the socket fitted on it.
    b.style(darkSteel(w), METAL);
    b.box(V3(xh, -T / 2 - 0.004, 0), V3(driveW, 0.008, driveW), 0.0007);
    setFin(b, 0, w);
    const double y1 = -T / 2 - 0.003;
    b.lathe(V3(xh, y1 - sockL, 0),
            {V2(0, 0), V2(sockR * 0.85, 0), V2(sockR, 0.0035), V2(sockR, sockL * 0.55), V2(sockR * 0.93, sockL * 0.6), V2(sockR * 0.93, sockL * 0.8), V2(sockR * 0.82, sockL * 0.9), V2(sockR * 0.82, sockL), V2(0, sockL)}, 12, true);
    hole(b, V3(xh, y1 - sockL, 0), V3(0, -1, 0), sockR * 0.66, 6);
}

void ratchetStd(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    const double xh = 0.094, x0 = -0.098;
    ratchetHead(b, xh, 0.0185, 0.0170, w, fin, 0.0108, 0.028, 0.0095);
    setFin(b, fin, w);
    b.beam(V3(xh - 0.03, 0, 0), V3(xh - 0.009, 0, 0), 0.0245, 0.0150, V3(0, 1, 0), 0.005);
    b.beam(V3(0.002, 0, 0), V3(xh - 0.028, 0, 0), 0.0235, 0.0108, V3(0, 1, 0), 0.0042);
    // Over-moulded grip.
    b.style(paint(p.i("grip")) * (1 - w * 0.4), MATTE);
    b.beam(V3(x0 + 0.004, 0, 0), V3(0.012, 0, 0), 0.0262, 0.0148, V3(0, 1, 0), 0.0065);
    b.style(rubberCol(w), MATTE);
    b.box(V3(-0.02, 0.0074, 0), V3(0.05, 0.0018, 0.0112), 0.0008);  // a black inset strip
    hole(b, V3(x0 + 0.0125, 0.0075, 0), V3(0, 1, 0), 0.0045, 8);
    b.style(darkSteel(w), METAL);
    stampTop(b, "3/8", V3(0.036, 0.0054, 0), 0.0014, 0.0007);
    grease(b, V3(0.02, 0.006, 0), V3(xh, 0.006, 0), 0.0022, 6, w, 61, 0.006);
}

void ratchetLong(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    const double xh = 0.205, x0 = -0.205;
    ratchetHead(b, xh, 0.0230, 0.0210, w, fin, 0.0148, 0.040, 0.0127);
    setFin(b, fin, w);
    // Neck, then a long round handle.
    latheAB(b, V3(xh - 0.016, 0, 0), V3(x0, 0, 0), {V2(0, 0), V2(0.0150, 0), V2(0.0125, 0.020), V2(0.0108, 0.05), V2(0.0108, -x0 + xh - 0.016 - 0.008), V2(0.0138, -x0 + xh - 0.016 - 0.004), V2(0, -x0 + xh - 0.016)}, 12, true);
    b.style(paint(p.i("grip")) * (1 - w * 0.4), MATTE);
    knurl(b, V3(x0 + 0.008, 0, 0), V3(-0.01, 0, 0), 0.0138, 0.0012, 13, 10, false);
    setFin(b, fin, w);
    b.style(darkSteel(w), METAL);
    stampTop(b, "1/2", V3(0.075, 0.0108, 0), 0.0016, 0.0007);
    grease(b, V3(0.0, 0.009, 0), V3(xh, 0.009, 0), 0.0028, 8, w, 62, 0.007);
}

// A socket-set case lying open: tray with rows of sockets, a ratchet and an extension; the lid propped back.
void socketCase(B& b, const P& p) {
    const double w = p("wear");
    const V3 cc = paint(p.i("paint")) * (1 - w * 0.35);
    const double LX = 0.38, LZ = 0.23, H = 0.047, wall = 0.008;
    b.style(cc, MATTE);
    b.box(V3(0, 0.006, 0), V3(LX, 0.012, LZ), 0.004);
    b.box(V3(0, H / 2, LZ / 2 - wall / 2), V3(LX, H, wall), 0.0);
    b.box(V3(0, H / 2, -LZ / 2 + wall / 2), V3(LX, H, wall), 0.0);
    b.box(V3(LX / 2 - wall / 2, H / 2, 0), V3(wall, H, LZ - 2 * wall), 0.0);
    b.box(V3(-LX / 2 + wall / 2, H / 2, 0), V3(wall, H, LZ - 2 * wall), 0.0);
    // The tray insert and rails the sockets stand on.
    b.style(cc * 0.55, MATTE);
    b.box(V3(0, 0.0145, 0), V3(LX - 2 * wall, 0.005, LZ - 2 * wall), 0);
    b.style(cc * 0.75, MATTE);
    const double zr[3] = {0.068, 0.020, -0.028};
    for (int r = 0; r < 3; r++) b.box(V3(0, 0.0195, zr[r] - 0.011), V3(LX - 0.05, 0.0055, 0.005), 0);
    // Sockets: 7 per row, shrinking along the row; deep ones at the back.
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < (r == 0 ? 7 : 6); i++) {
            const double t = i / (r == 0 ? 6.0 : 5.0);
            const double rad = (r == 2 ? 0.0105 : 0.0100) - 0.0034 * t + 0.0008 * (r == 0);
            const double h = (r == 0 ? 0.040 : r == 1 ? 0.032 : 0.026) - 0.004 * t;
            const V3 a(-0.135 + i * (r == 0 ? 0.045 : 0.054), 0.0172, zr[r]), c(a.x, 0.0172 + h, zr[r]);
            setFin(b, 0, w);
            b.cyl(a, c, rad, 6, true);
            b.style(darkSteel(w), METAL);
            hole(b, c + V3(0, 0.0001, 0), V3(0, 1, 0), rad * 0.62, 6);
        }
    // Ratchet and extension bar lying in the front slot.
    const double zf = -0.082, yb = 0.0172 + 0.0045;
    setFin(b, 0, w);
    b.beam(V3(-0.165, yb, zf), V3(0.0, yb, zf), 0.0185, 0.0085, V3(0, 1, 0), 0.0035);
    b.lathe(V3(0.027, yb, zf), {V2(0, -0.0044), V2(0.0145, -0.0044), V2(0.0165, -0.0020), V2(0.0165, 0.0020), V2(0.0145, 0.0044), V2(0, 0.0044)}, 14, true);
    b.beam(V3(-0.006, yb, zf), V3(0.02, yb, zf), 0.0185, 0.0085, V3(0, 1, 0), 0.0035);
    b.style(paint(p.i("grip")), MATTE);
    b.beam(V3(-0.165, yb, zf), V3(-0.075, yb, zf), 0.0205, 0.0108, V3(0, 1, 0), 0.0045);
    setFin(b, 0, w);
    rodAB(b, V3(0.062, yb, zf), V3(0.178, yb, zf), 0.0058, 0.0012, 10);
    b.style(darkSteel(w), METAL);
    b.box(V3(0.178, yb, zf), V3(0.01, 0.0086, 0.0086), 0.0008);
    // Hinge barrels at the back, a couple of latches in front.
    b.style(V3(0.55, 0.56, 0.58), METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.cyl(V3(s * 0.12 - 0.02, H, -LZ / 2 - 0.001), V3(s * 0.12 + 0.02, H, -LZ / 2 - 0.001), 0.0055, 8);
        b.box(V3(s * 0.12, H - 0.006, LZ / 2 + 0.0015), V3(0.032, 0.020, 0.0035), 0.0);  // latch
        b.cyl(V3(s * 0.12 - 0.006, H - 0.009, LZ / 2 + 0.0035), V3(s * 0.12 + 0.006, H - 0.009, LZ / 2 + 0.0035), 0.0022, 6);
    }
    // The lid, propped open past upright.
    b.push();
    b.move(0, H, -LZ / 2);
    b.roll(-104);
    b.style(cc, MATTE);
    b.box(V3(0, 0.0105, LZ / 2), V3(LX, 0.021, LZ), 0.004);
    b.style(cc * 0.7, MATTE);
    b.box(V3(0, -0.0003, LZ / 2), V3(LX - 0.026, 0.0036, LZ - 0.026), 0.001);
    b.style(V3(0.85, 0.84, 0.78), MATTE);
    b.box(V3(0, -0.0024, LZ / 2), V3(LX - 0.1, 0.0012, 0.09), 0.0003);  // a printed size chart
    b.push();
    b.move(0, -0.0036, LZ / 2);
    b.pitch(-90);
    b.roll(90);
    b.style(V3(0.1, 0.1, 0.12), MATTE);
    b.letters("SOCKET SET", V3(0, 0, 0.0), 0.0040, 0.0008);
    b.pop();
    b.pop();
}

Reg ratchetReg({"ratchet", "RATCHET", "REPAIR", "Hand tool: ratchet with a socket fitted, driven by the right hand; grip at the origin, socket at +X end pointing down (the case lies flat, origin at its middle)", 0.3,
                {
                    {"STANDARD", "3/8 in ratchet, chrome, over-moulded grip, 13 mm socket", {finParam(0), paintParam(0, "grip", "GRIP"), wearParam()}, lifted(ratchetStd)},
                    {"LONG HANDLE", "1/2 in long-handle ratchet with a deep socket", {finParam(0), paintParam(5, "grip", "GRIP"), wearParam()}, lifted(ratchetLong)},
                    {"SOCKET CASE", "Blow-moulded socket set case lying open", {paintParam(0, "paint", "CASE"), paintParam(3, "grip", "RATCHET GRIP"), wearParam()}, lifted(socketCase)},
                }});

// ======================================================================================
// 5. PRY BAR
// ======================================================================================

const std::vector<std::string> kPryFin = {"BARE STEEL", "BLACK OXIDE", "RED PAINT", "YELLOW PAINT"};
void pryFin(B& b, int f, double w) {
    switch (f) {
        case 0: setFin(b, 3, w); break;
        case 1: setFin(b, 1, w); break;
        case 2: b.style(grime(paint(0) * 0.9, w * 0.7), PAINT); break;
        default: b.style(grime(paint(8) * 0.95, w * 0.7), PAINT); break;
    }
}

// The flat bar: a side-profile extrusion with both ends curving up, a chisel lip and nail slots.
void flatBar(B& b, const P& p) {
    const double w = p("wear");
    static const double Ls[3] = {0.30, 0.45, 0.60};
    const double L = Ls[std::clamp(p.i("length"), 0, 2)], hl = L / 2, t = 0.0070, W = 0.034;
    pryFin(b, p.i("finish"), w);
    const double rise = 0.012 + 0.03 * L, x0 = hl * 0.55;
    auto yc = [&](double x) {
        const double u = std::max(0.0, (std::fabs(x) - x0) / (hl - x0));
        return rise * u * u;
    };
    std::vector<V2> top, bot;
    const int n = 14;
    for (int i = 0; i <= n; i++) {
        const double x = -hl + L * i / n;
        const bool tipR = i == n, tipL = i == 0;
        top.push_back(V2(x, yc(x) + t / 2));
        bot.push_back(V2(x, yc(x) - t / 2 + (tipR ? t * 0.65 : tipL ? t * 0.35 : 0)));
    }
    std::vector<V2> poly = top;
    for (int i = n; i >= 0; i--) poly.push_back(bot[i]);
    b.extrude(poly, W, 0);
    // Nail slots near each end and the stamped size.
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    for (int s = -1; s <= 1; s += 2) {
        const double x = s * (hl - 0.045);
        b.push();
        b.move(x, yc(x) + t / 2 + 0.0004, 0);
        b.pitch(s * std::atan2(2 * rise * (std::fabs(x) - x0) / ((hl - x0) * (hl - x0)), 1) / DEG);
        b.box(V3(0, 0, 0), V3(0.020, 0.0008, 0.0052), 0);
        b.pop();
    }
    b.style(darkSteel(w) * 1.6, METAL);
    stampTop(b, std::to_string((int)std::lround(L / 0.0254)) + " IN", V3(0.004, t / 2 + 0.0003, 0), 0.0016, 0.0006);
    grease(b, V3(-hl * 0.4, t / 2, 0), V3(hl * 0.4, t / 2, 0), 0.0028, 7, w, 71, 0.008);
}

// The gooseneck crowbar: hex bar, a flat chisel at the butt, a J-curve with a slotted claw at the front.
void crowbar(B& b, const P& p) {
    const double w = p("wear");
    static const double Ls[3] = {0.60, 0.76, 0.92};
    const double Lb = Ls[std::clamp(p.i("length"), 0, 2)];
    const double xa = 0.30, R = 0.058, r = 0.0118, x0 = xa - (Lb - 0.12);
    pryFin(b, p.i("finish"), w);
    b.cyl(V3(x0 + 0.045, 0, 0), V3(xa, 0, 0), r, 6, true);
    std::vector<V3> path;
    for (int i = 0; i <= 10; i++) {
        const double a = (-90 + 200.0 * i / 10) * DEG;
        path.push_back(V3(xa + R * std::cos(a), R + R * std::sin(a), 0));
    }
    b.tube(path, r, 6, false, true);
    // The flattened claw tip, with its slot.
    const double ae = 110 * DEG, ao = (-90 + 200.0 * 8 / 10) * DEG;
    const V3 pe(xa + R * std::cos(ae), R + R * std::sin(ae), 0), po(xa + R * std::cos(ao), R + R * std::sin(ao), 0);
    const V3 d = norm(pe - po), nrm(-d.y, d.x, 0);
    b.beam(po, pe + d * 0.034, 0.030, 0.0085, nrm, 0.002);
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.beam(pe + d * 0.012 + nrm * 0.0045, pe + d * 0.036 + nrm * 0.0045, 0.0052, 0.0008, nrm, 0);
    // The chisel at the butt: forged flat and bevelled.
    pryFin(b, p.i("finish"), w);
    b.extrudeUp({V2(x0 + 0.06, -0.0105), V2(x0 + 0.03, -0.0165), V2(x0, -0.0200), V2(x0 - 0.002, 0), V2(x0, 0.0200), V2(x0 + 0.03, 0.0165), V2(x0 + 0.06, 0.0105)}, 0.0085, -0.00425);
    b.style(darkSteel(w) * 1.5, METAL);
    stampTop(b, std::to_string((int)std::lround(Lb / 0.0254)), V3(x0 + 0.05, 0.0088, 0), 0.0017, 0.0006);
    grease(b, V3(x0, 0.009, 0), V3(xa, 0.009, 0), 0.0030, 8, w, 72, 0.007);
}

// The long pinch bar: a round bar, a pointed butt, a heeled chisel at the other end.
void pinchBar(B& b, const P& p) {
    const double w = p("wear");
    static const double Ls[3] = {1.0, 1.2, 1.5};
    const double L = Ls[std::clamp(p.i("length"), 0, 2)], hl = L / 2, r = 0.0125;
    const double xs = hl - 0.17, Rb = 0.17, bend = 22;
    pryFin(b, p.i("finish"), w);
    latheAB(b, V3(-hl, 0, 0), V3(xs, 0, 0), {V2(0, 0), V2(0.0030, 0), V2(0.0085, 0.05), V2(r, 0.11), V2(r, xs + hl)}, 12, true);
    std::vector<V3> path;
    for (int i = 0; i <= 5; i++) {
        const double a = bend * i / 5 * DEG;
        path.push_back(V3(xs + Rb * std::sin(a), Rb * (1 - std::cos(a)), 0));
    }
    b.tube(path, r, 12, false, true);
    const V3 pe = path.back();
    b.push();
    b.move(pe);
    b.pitch(bend);
    latheAB(b, V3(0, 0, 0), V3(0.03, 0, 0), {V2(0, 0), V2(r, 0), V2(0.0085, 0.028)}, 12, true);
    // The chisel: flat, broad, bevelled at the cutting edge, with a heel under it.
    b.extrudeUp({V2(0.018, -0.0095), V2(0.045, -0.0185), V2(0.080, -0.0235), V2(0.097, -0.0225), V2(0.1, -0.020), V2(0.1, 0.020), V2(0.097, 0.0225), V2(0.080, 0.0235), V2(0.045, 0.0185), V2(0.018, 0.0095)}, 0.0098, -0.0049);
    b.style(darkSteel(w) * 1.7, METAL);
    b.extrudeUp({V2(0.082, -0.0230), V2(0.098, -0.0220), V2(0.0995, -0.019), V2(0.0995, 0.019), V2(0.098, 0.0220), V2(0.082, 0.0230)}, 0.0004, 0.0049);
    b.pop();
    b.style(darkSteel(w) * 1.5, METAL);
    grease(b, V3(-hl * 0.9, r, 0), V3(xs, r, 0), 0.0035, 9, w, 73, 0.004);
}

Reg pryReg({"pry_bar", "PRY BAR", "REPAIR", "Hand tool: prise, lever, wedge; grip at the origin (bar middle), claw/chisel at +X, flat face up", 0.5,
            {
                {"FLAT BAR", "Flat wonder bar with nail slots, ends curved up", {choice("finish", "FINISH", 1, kPryFin), choice("length", "LENGTH", 1, {"12 IN", "18 IN", "24 IN"}), wearParam()}, lifted(flatBar)},
                {"CROWBAR", "Hex gooseneck crowbar, chisel butt", {choice("finish", "FINISH", 0, kPryFin), choice("length", "LENGTH", 1, {"24 IN", "30 IN", "36 IN"}), wearParam()}, lifted(crowbar)},
                {"PINCH BAR", "Long round pinch bar with a heeled chisel", {choice("finish", "FINISH", 2, kPryFin), choice("length", "LENGTH", 1, {"40 IN", "48 IN", "60 IN"}), wearParam()}, lifted(pinchBar)},
            }});

// ======================================================================================
// 6. PLIERS  (instance param "open": 0 closed .. 1 wide open)
// ======================================================================================

std::vector<V2> rotPts(const std::vector<V2>& pts, double deg, double px, double pz) {
    const double c = std::cos(deg * DEG), s = std::sin(deg * DEG);
    std::vector<V2> o;
    for (const V2& q : pts) o.push_back(V2(px + (q.x - px) * c - (q.y - pz) * s, pz + (q.x - px) * s + (q.y - pz) * c));
    return o;
}

struct Pl {
    double jawX0, jawLen, jawW0, jawW1, jawH;  // the jaw plate: from the pivot, width at the base and at the tip, thickness
    int teeth;
    bool cutter;
    double handleLen, handleW, handleH, hx0;  // handle: starts hx0 behind the pivot
    double pivotR;
};

// One half of a crossed-member pair of pliers, in its own frame: pivot at the origin, jaw out to +X on the -Z
// side (the other half is this one mirrored). ny: the height its neck crosses the pivot at.
void plHalf(B& b, const Pl& c, double ny, double w, int fin, const V3& grip, bool gripOn) {
    setFin(b, fin, w);
    const double zi = c.cutter ? 0.0006 : 0.0008;
    const double x0 = c.jawX0, x1 = c.jawX0 + c.jawLen;
    // The jaw plate.
    std::vector<V2> jaw;
    if (c.cutter) {
        jaw = {V2(x0, -(zi + c.jawW0)), V2(x1 - 0.006, -(zi + c.jawW1)), V2(x1, -(zi + c.jawW1 * 0.45)), V2(x1, -zi), V2(x0, -zi)};
    } else {
        jaw = {V2(x0, -(zi + c.jawW0)), V2(x1, -(zi + c.jawW1)), V2(x1, -zi - 0.0013)};
        const double dx = (x1 - x0) / c.teeth;
        for (int i = c.teeth - 1; i >= 0; i--) {
            jaw.push_back(V2(x0 + (i + 0.5) * dx, -zi));
            jaw.push_back(V2(x0 + i * dx, -zi - 0.0013));
        }
    }
    b.extrudeUp(jaw, c.jawH, -c.jawH / 2);
    // The neck crossing to the handle on the other side.
    const double zh = c.handleW / 2 + 0.0016;
    b.beam(V3(x0 + 0.006, ny, -(zi + c.jawW0 * 0.5)), V3(-c.hx0, ny, zh), c.jawW0 * 0.95, 0.0046, V3(0, 1, 0), 0.0012);
    // The handle: forged steel, with a dipped grip over most of it.
    b.beam(V3(-c.hx0, 0, zh), V3(-c.hx0 - c.handleLen, 0, zh), c.handleW, c.handleH, V3(0, 1, 0), 0.0032);
    if (gripOn) {
        b.style(grip, MATTE);
        b.beam(V3(-c.hx0 - 0.022, 0, zh), V3(-c.hx0 - c.handleLen - 0.002, 0, zh), c.handleW + 0.0048, c.handleH + 0.0034, V3(0, 1, 0), 0.0045);
        // Grip ridges at the butt end.
        b.style(grip * 0.8, MATTE);
        for (int i = 0; i < 3; i++) b.box(V3(-c.hx0 - c.handleLen + 0.009 + i * 0.0085, c.handleH / 2 + 0.0019, zh), V3(0.0035, 0.0012, c.handleW * 0.7), 0.0004);
    }
}

void pliersBuild(B& b, const Pl& c, const P& p, bool gripOn, double maxOpen) {
    const double w = p("wear");
    const int fin = p.i("finish");
    const double th = p("open") * maxOpen;
    const V3 grip = paint(p.i("grip")) * (1 - w * 0.35);
    b.move(c.hx0 + c.handleLen * 0.45, 0, 0);  // the grip sits at the origin
    b.push();
    b.yaw(th);
    plHalf(b, c, 0.0030, w, fin, grip, gripOn);
    b.pop();
    b.push();
    b.mirrorZ();
    b.yaw(th);
    plHalf(b, c, -0.0030, w, fin, grip, gripOn);
    b.pop();
    // The pivot: a fat rivet and washers.
    setFin(b, fin, w);
    b.lathe(V3(0, 0, 0), {V2(0, -c.jawH / 2 - 0.0008), V2(c.pivotR * 0.8, -c.jawH / 2 - 0.0008), V2(c.pivotR, -c.jawH / 2 + 0.0015), V2(c.pivotR, c.jawH / 2 - 0.0015), V2(c.pivotR * 0.8, c.jawH / 2 + 0.0008), V2(0, c.jawH / 2 + 0.0008)}, 14, true);
    b.style(darkSteel(w), METAL);
    b.cyl(V3(0, c.jawH / 2 + 0.0008, 0), V3(0, c.jawH / 2 + 0.0022, 0), c.pivotR * 0.45, 8);
    grease(b, V3(c.jawX0, c.jawH / 2, -0.01), V3(-c.hx0, c.jawH / 2, 0.01), 0.0022, 8, w, 81, 0.004);
}

const Pl kSlip = {0.010, 0.046, 0.0185, 0.0115, 0.0105, 7, false, 0.112, 0.0150, 0.0092, 0.018, 0.0125};
const Pl kCut = {0.008, 0.024, 0.0175, 0.0150, 0.0140, 0, true, 0.098, 0.0140, 0.0088, 0.016, 0.0120};

void slipJoint(B& b, const P& p) { pliersBuild(b, kSlip, p, true, 15); }
void sideCutters(B& b, const P& p) { pliersBuild(b, kCut, p, true, 17); }

// Locking pliers: fixed jaw and channel handle with the adjuster knob, a swinging jaw, the lower handle,
// a toggle link and the release lever.
void lockingPliers(B& b, const P& p) {
    const double w = p("wear");
    const int fin = p.i("finish");
    const double op = p("open");
    const double thJ = op * 15, thH = op * 17;
    b.move(0.05, 0, 0);
    setFin(b, fin, w);
    const double tj = 0.023, th = 0.0185;
    // Fixed member: curved-nose jaw with teeth, the channel handle.
    std::vector<V2> A = {V2(0.126, 0.0150), V2(0.100, 0.0200), V2(0.070, 0.0245), V2(0.045, 0.0285), V2(0.0, 0.0290), V2(-0.100, 0.0275), V2(-0.121, 0.0245), V2(-0.121, 0.0075), V2(-0.100, 0.0045), V2(0.0, 0.0050), V2(0.030, 0.0050), V2(0.048, 0.0020)};
    for (int i = 0; i < 8; i++) {
        A.push_back(V2(0.052 + i * 0.0092 + 0.0046, 0.0020));
        A.push_back(V2(0.052 + i * 0.0092 + 0.0092, 0.0008));
    }
    // (the teeth run up to the nose; close the loop at the tip)
    b.extrudeUp(A, tj, -tj / 2);
    // Handle channel: a dark groove down the top.
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.box(V3(-0.055, tj / 2 + 0.0003, 0.0165), V3(0.13, 0.0006, 0.0075), 0);
    setFin(b, fin, w);
    // Swinging jaw, pivoting on its rivet at (0.048, -0.015).
    std::vector<V2> J = {V2(0.123, -0.0135), V2(0.100, -0.0190), V2(0.070, -0.0245), V2(0.040, -0.0265), V2(0.025, -0.0140), V2(0.030, -0.0040), V2(0.048, -0.0020)};
    for (int i = 0; i < 8; i++) {
        J.push_back(V2(0.052 + i * 0.0092 + 0.0046, -0.0020));
        J.push_back(V2(0.052 + i * 0.0092 + 0.0092, -0.0008));
    }
    // (the jaw's tooth run starts at its tip: reorder tip first)
    std::vector<V2> J2;
    J2.push_back(V2(0.123, -0.0135));
    for (int i = 7; i >= 0; i--) {
        J2.push_back(V2(0.052 + i * 0.0092 + 0.0092, -0.0008));
        J2.push_back(V2(0.052 + i * 0.0092 + 0.0046, -0.0020));
    }
    J2.push_back(V2(0.048, -0.0020));
    J2.push_back(V2(0.030, -0.0040));
    J2.push_back(V2(0.025, -0.0140));
    J2.push_back(V2(0.040, -0.0265));
    J2.push_back(V2(0.070, -0.0245));
    J2.push_back(V2(0.100, -0.0190));
    const V2 pj(0.045, -0.0150), ph(-0.015, -0.0170);
    b.color(finish(fin, w).c * 0.86);
    b.extrudeUp(rotPts(J2, -thJ, pj.x, pj.y), tj, -tj / 2);
    // Lower handle, pivoting at the front of the channel.
    std::vector<V2> H = {V2(0.018, -0.0070), V2(0.012, -0.0045), V2(-0.100, -0.0045), V2(-0.119, -0.0075), V2(-0.119, -0.0260), V2(-0.100, -0.0290), V2(0.0, -0.0285), V2(0.016, -0.0250)};
    b.extrudeUp(rotPts(H, thH, ph.x, ph.y), th, -th / 2);
    // (a darker stripe down the lower handle)
    b.color(finish(fin, w).c * 0.6);
    {
        const V2 a = rotPts({V2(-0.10, -0.0165)}, thH, ph.x, ph.y)[0], c2 = rotPts({V2(-0.005, -0.0165)}, thH, ph.x, ph.y)[0];
        b.beam(V3(a.x, 0, a.y), V3(c2.x, 0, c2.y), 0.0075, th + 0.0007, V3(0, 1, 0), 0.0);
    }
    // Handle ridge grooves (grip) on the lower handle.
    // The toggle link from the jaw rear pin to the handle front pin.
    auto rp = [&](V2 q, double deg, V2 pv) {
        const V2 r = rotPts({q}, deg, pv.x, pv.y)[0];
        return V3(r.x, 0, r.y);
    };
    const V3 pinJ = rp(V2(0.028, -0.0090), -thJ, pj), pinH = rp(V2(0.010, -0.0150), thH, ph);
    b.style(darkSteel(w), METAL);
    b.beam(pinJ + V3(0, tj / 2 + 0.0009, 0), pinH + V3(0, tj / 2 + 0.0009, 0), 0.0075, 0.0018, V3(0, 1, 0), 0.0005);
    b.cyl(pinJ + V3(0, -tj / 2 - 0.0008, 0), pinJ + V3(0, tj / 2 + 0.0026, 0), 0.0035, 8);
    b.cyl(pinH + V3(0, -th / 2 - 0.0008, 0), pinH + V3(0, tj / 2 + 0.0026, 0), 0.0035, 8);
    // The release lever, riding on the lower handle.
    {
        const V3 a = rp(V2(-0.060, -0.0050), thH, ph), c = rp(V2(0.012, -0.0050), thH, ph);
        b.style(V3(0.78, 0.30, 0.04) * (1 - w * 0.4), PAINT);
        b.beam(a + V3(0, 0, 0), c + V3(0, 0, 0), 0.0036, 0.0155, V3(0, 1, 0), 0.0008);
        setFin(b, fin, w);
    }
    // Pivot rivets.
    b.style(darkSteel(w), METAL);
    const V3 pjw = V3(pj.x, 0, pj.y);
    b.cyl(pjw + V3(0, -tj / 2 - 0.0008, 0), pjw + V3(0, tj / 2 + 0.0018, 0), 0.0058, 10);
    b.cyl(V3(ph.x, -th / 2 - 0.0008, ph.y), V3(ph.x, tj / 2 + 0.0018, ph.y), 0.0050, 10);
    // The adjusting screw's knurled knob at the butt of the fixed handle.
    b.style(darkSteel(w) * 1.3, METAL);
    knurl(b, V3(-0.121, 0, 0.0165), V3(-0.153, 0, 0.0165), 0.0105, 0.0012, 7, 8, false);
    rodAB(b, V3(-0.110, 0, 0.0165), V3(-0.122, 0, 0.0165), 0.0045, 0.0005, 8);
    b.style(darkSteel(w) * 1.6, METAL);
    stampTop(b, "10R", V3(-0.040, tj / 2 + 0.0008, 0.0165), 0.0011, 0.0006);
    grease(b, V3(-0.1, tj / 2, 0.015), V3(0.1, tj / 2, 0.015), 0.0024, 7, w, 82, 0.004);
}

Reg pliersReg({"pliers", "PLIERS", "REPAIR", "Hand tool: grips, pulls, cuts; grip at the origin, jaws +X, instance param open 0..1", 0.28,
               {
                   {"SLIP-JOINT", "Slip-joint pliers, toothed jaws, dipped handles", {finParam(2), paintParam(5, "grip", "GRIPS"), wearParam(), num("open", "OPEN", OPEN_DEF, 0, 1)}, lifted(slipJoint)},
                   {"LOCKING", "Vise-grip style locking pliers with adjuster knob and release lever", {finParam(2), wearParam(), num("open", "OPEN", OPEN_DEF, 0, 1)}, lifted(lockingPliers)},
                   {"SIDE CUTTERS", "Diagonal cutters, insulated grips", {finParam(3), paintParam(8, "grip", "GRIPS"), wearParam(), num("open", "OPEN", OPEN_DEF, 0, 1)}, lifted(sideCutters)},
               }});

// ======================================================================================
// 7. SCREWDRIVER
// ======================================================================================

// The business end starting at x (shaft radius r): a flat blade or a Phillips cross.
void sdTip(B& b, double x, double r, int kind, double w) {
    b.style(darkSteel(w) * 1.5, METAL);
    if (kind == 0) {
        // Flat: the round shaft flattens out into a thin blade with a ground tip.
        const double bw = r * 1.15, L = 0.020 + r * 2;
        latheAB(b, V3(x - 0.012, 0, 0), V3(x + 0.004, 0, 0), {V2(0, 0), V2(r, 0), V2(r * 0.9, 0.016)}, 10, true);
        b.extrudeUp({V2(x, -r * 0.9), V2(x + 0.006, -bw), V2(x + L - 0.004, -bw * 1.02), V2(x + L, -bw * 0.85), V2(x + L, bw * 0.85), V2(x + L - 0.004, bw * 1.02), V2(x + 0.006, bw), V2(x, r * 0.9)}, r * 0.55, -r * 0.275);
    } else {
        const double L = 0.016 + r;
        latheAB(b, V3(x - 0.012, 0, 0), V3(x + 0.003, 0, 0), {V2(0, 0), V2(r, 0), V2(r * 0.95, 0.015)}, 10, true);
        const std::vector<V2> wing = {V2(x, -r * 0.95), V2(x + L - 0.002, -r * 0.20), V2(x + L, 0), V2(x + L - 0.002, r * 0.20), V2(x, r * 0.95)};
        b.extrudeUp(wing, r * 0.40, -r * 0.20);
        b.push();
        b.roll(90);
        b.extrude(wing, r * 0.40, 0);
        b.pop();
    }
}

void screwdriver(B& b, const P& p) {
    const double w = p("wear");
    const int tip = p.i("tip");
    const V3 hc = paint(p.i("handle")) * (1 - w * 0.4), gc = paint(p.i("grip")) * (1 - w * 0.3);
    const double hl = 0.055, hr = 0.0165;  // half-length and radius of the handle
    // Handle: a core in the body colour, an over-moulded fluted grip in the second colour.
    b.style(hc, MATTE);
    latheAB(b, V3(-hl, 0, 0), V3(hl, 0, 0),
            {V2(0, 0), V2(0.0098, 0), V2(0.0138, 0.005), V2(0.0152, 0.016), V2(0.0150, 0.060), V2(0.0138, 0.095), V2(0.0108, 0.108), V2(0.0094, 0.1095), V2(0.0, 0.1095)}, 14, true);
    b.style(gc, MATTE);
    knurl(b, V3(-0.034, 0, 0), V3(0.032, 0, 0), hr + 0.0006, 0.0016, 8, 12, false);
    // The ferrule and the shaft.
    setFin(b, 0, w);
    rodAB(b, V3(hl - 0.002, 0, 0), V3(hl + 0.014, 0, 0), 0.0086, 0.0015, 12);
    const double r = 0.0034, xs = hl + 0.014;
    rodAB(b, V3(xs - 0.001, 0, 0), V3(xs + 0.135, 0, 0), r, 0.0004, 10);
    sdTip(b, xs + 0.135, r, tip, w);
    // Stamped label on the butt.
    b.push();
    b.move(-hl - 0.0003, 0, 0);
    b.yaw(180);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    b.letters(tip ? "PH2" : "6.5", V3(0, 0, 0), 0.0010, 0.0005);
    b.pop();
    grease(b, V3(xs + 0.02, 0.003, 0), V3(xs + 0.12, 0.003, 0), 0.0016, 5, w, 91, 0.002);
}

void stubby(B& b, const P& p) {
    const double w = p("wear");
    const int tip = p.i("tip");
    const V3 hc = paint(p.i("handle")) * (1 - w * 0.4), gc = paint(p.i("grip")) * (1 - w * 0.3);
    const double hl = 0.030;
    b.style(hc, MATTE);
    latheAB(b, V3(-hl, 0, 0), V3(hl, 0, 0), {V2(0, 0), V2(0.0140, 0), V2(0.0195, 0.007), V2(0.0215, 0.016), V2(0.0215, 0.044), V2(0.0195, 0.052), V2(0.0150, 0.058), V2(0.0125, 0.0605), V2(0, 0.0605)}, 14, true);
    b.style(gc, MATTE);
    knurl(b, V3(-0.020, 0, 0), V3(0.020, 0, 0), 0.0222, 0.0020, 7, 12, false);
    setFin(b, 0, w);
    rodAB(b, V3(hl - 0.002, 0, 0), V3(hl + 0.010, 0, 0), 0.0105, 0.0015, 12);
    const double r = 0.0044, xs = hl + 0.010;
    rodAB(b, V3(xs - 0.001, 0, 0), V3(xs + 0.040, 0, 0), r, 0.0004, 10);
    sdTip(b, xs + 0.040, r, tip, w);
    b.push();
    b.move(-hl - 0.0003, 0, 0);
    b.yaw(180);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    b.letters(tip ? "PH2" : "8.0", V3(0, 0, 0), 0.0012, 0.0005);
    b.pop();
    grease(b, V3(xs, 0.004, 0), V3(xs + 0.04, 0.004, 0), 0.0016, 3, w, 92, 0.002);
}

void multiBit(B& b, const P& p) {
    const double w = p("wear");
    const int tip = p.i("tip");
    const V3 hc = paint(p.i("handle")) * (1 - w * 0.4), gc = paint(p.i("grip")) * (1 - w * 0.3);
    const double hl = 0.064;
    b.style(hc, MATTE);
    latheAB(b, V3(-hl, 0, 0), V3(hl, 0, 0),
            {V2(0, 0), V2(0.0130, 0), V2(0.0165, 0.004), V2(0.0175, 0.014), V2(0.0178, 0.070), V2(0.0160, 0.095), V2(0.0128, 0.112), V2(0.0100, 0.1275), V2(0, 0.1275)}, 14, true);
    b.style(gc, MATTE);
    knurl(b, V3(-0.048, 0, 0), V3(0.020, 0, 0), 0.0184, 0.0016, 7, 10, false);
    // The ratchet collar and the hinged bit-store cap at the butt.
    setFin(b, 0, w);
    knurl(b, V3(0.026, 0, 0), V3(0.044, 0, 0), 0.0150, 0.0010, 5, 10, false);
    b.style(gc * 0.7, MATTE);
    b.cyl(V3(-hl - 0.004, 0, 0), V3(-hl + 0.003, 0, 0), 0.0128, 12);
    b.style(V3(0.03, 0.03, 0.035), MATTE);
    b.box(V3(-hl - 0.0045, 0, 0), V3(0.0012, 0.0045, 0.0125), 0.0003);  // the cap's thumb notch
    setFin(b, 0, w);
    // Magnetic bit holder and the bit.
    rodAB(b, V3(hl - 0.002, 0, 0), V3(hl + 0.050, 0, 0), 0.0058, 0.0010, 12);
    b.style(darkSteel(w) * 1.4, METAL);
    b.cyl(V3(hl + 0.049, 0, 0), V3(hl + 0.068, 0, 0), 0.0034, 6);
    sdTip(b, hl + 0.068, 0.0034, tip, w);
    b.push();
    b.move(-hl - 0.0052, 0, 0);
    b.yaw(180);
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    b.letters("11 IN 1", V3(0, 0, 0), 0.0009, 0.0004);
    b.pop();
    // Bit-size band printed on the collar side.
    grease(b, V3(hl + 0.01, 0.005, 0), V3(hl + 0.05, 0.005, 0), 0.0016, 3, w, 93, 0.002);
}

Reg screwdriverReg({"screwdriver", "SCREWDRIVER", "REPAIR", "Hand tool: screws and prying small things; grip at the origin (handle middle), tip +X", 0.25,
                    {
                        {"STANDARD", "Long-shaft screwdriver, two-tone fluted handle", {choice("tip", "TIP", 0, {"FLAT", "PHILLIPS"}), paintParam(8, "handle", "HANDLE"), paintParam(3, "grip", "GRIP"), wearParam()}, lifted(screwdriver)},
                        {"STUBBY", "Fat short stubby for tight spots", {choice("tip", "TIP", 1, {"FLAT", "PHILLIPS"}), paintParam(0, "handle", "HANDLE"), paintParam(3, "grip", "GRIP"), wearParam()}, lifted(stubby)},
                        {"MULTI-BIT", "Ratcheting multi-bit driver with a bit-store cap", {choice("tip", "TIP", 1, {"FLAT", "PHILLIPS"}), paintParam(4, "handle", "HANDLE"), paintParam(3, "grip", "GRIP"), wearParam()}, lifted(multiBit)},
                    }});

// ======================================================================================
// 13. TIRE IRON  (levers and the bead breaker)
// ======================================================================================

// One tyre spoon lying along X at z, rotated by yaw: a flat bar curving up into the spoon, a hooked tail.
void tireSpoon(B& b, double L, double z, double yawDeg, double xoff, const V3& coat, double w, int fin) {
    b.push();
    b.move(xoff, 0, z);
    b.yaw(yawDeg);
    const double hl = L / 2, t = 0.0090, W = 0.027, xs = hl - 0.075, rise = 0.024;
    auto yc = [&](double x) {
        if (x > xs) {
            const double u = (x - xs) / (hl - xs);
            return rise * u * u;
        }
        if (x < -hl + 0.05) {
            const double u = (-hl + 0.05 - x) / 0.05;
            return -0.006 * u * u;
        }
        return 0.0;
    };
    setFin(b, fin, w);
    std::vector<V2> top, bot;
    const int n = 16;
    for (int i = 0; i <= n; i++) {
        const double x = -hl + L * i / n;
        const double tt = x > xs ? t * (1 - 0.35 * (x - xs) / (hl - xs)) : t;
        top.push_back(V2(x, yc(x) + t / 2));
        bot.push_back(V2(x, yc(x) + t / 2 - tt));
    }
    std::vector<V2> poly = top;
    for (int i = n; i >= 0; i--) poly.push_back(bot[i]);
    b.extrude(poly, W, 0);
    // The spoon end: a slightly wider scoop plate on top of the curve.
    b.style(darkSteel(w) * 1.4, METAL);
    b.box(V3(hl - 0.04, yc(hl - 0.04) + t / 2 + 0.0004, 0), V3(0.012, 0.0008, 0.010), 0);
    // A grip dip over the middle of the bar.
    b.style(coat * (1 - w * 0.4), MATTE);
    b.beam(V3(-hl + 0.08, 0.0, 0), V3(xs - 0.02, 0.0, 0), W + 0.006, t + 0.0055, V3(0, 1, 0), 0.0035);
    b.pop();
}

void spoons(B& b, const P& p) {
    const double w = p("wear");
    static const double Ls[3] = {0.30, 0.40, 0.50};
    const double L = Ls[std::clamp(p.i("length"), 0, 2)];
    const V3 coat = paint(p.i("coat"));
    tireSpoon(b, L, 0.030, 0, 0.0, coat, w, p.i("finish"));
    tireSpoon(b, L, -0.030, -5, 0.025, coat, w, p.i("finish"));
    b.style(darkSteel(w) * 1.7, METAL);
    stampTop(b, std::to_string((int)std::lround(L / 0.0254)) + " IN", V3(L / 2 - 0.1, 0.0105, 0.030), 0.0012, 0.0005);
    grease(b, V3(-L / 2, 0.006, 0.03), V3(L / 2, 0.006, 0.03), 0.0025, 6, w, 101, 0.004);
}

void beadBreaker(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = grime(paint(p.i("paint")) * 0.92, w * 0.7);
    b.move(0.2, 0, 0);  // grip centre at the origin
    const double r = 0.0145;
    // Hex shaft, painted; rubber grip over the back half.
    b.style(col, PAINT);
    b.cyl(V3(-0.42, 0, 0), V3(0.31, 0, 0), r, 6, true);
    b.style(rubberCol(w), MATTE);
    knurl(b, V3(-0.38, 0, 0), V3(-0.06, 0, 0), r + 0.0030, 0.0016, 14, 10, false);
    b.cyl(V3(-0.395, 0, 0), V3(-0.38, 0, 0), r + 0.0045, 10);
    // The back end: a flattened, upturned spoon.
    b.style(col, PAINT);
    b.beam(V3(-0.415, 0, 0), V3(-0.468, 0.014, 0), 0.034, 0.0085, V3(0, 1, 0), 0.0025);
    b.beam(V3(-0.468, 0.014, 0), V3(-0.485, 0.030, 0), 0.034, 0.0085, V3(-0.5, 0.86, 0), 0.0025);
    // The duckbill foot, angled down to wedge between bead and rim.
    b.push();
    b.move(0.30, 0, 0);
    b.pitch(-14);
    b.extrudeUp({V2(0, -0.0145), V2(0.026, -0.032), V2(0.062, -0.047), V2(0.095, -0.042), V2(0.113, -0.021), V2(0.117, 0), V2(0.113, 0.021), V2(0.095, 0.042), V2(0.062, 0.047), V2(0.026, 0.032), V2(0, 0.0145)}, 0.0125, -0.0105);
    b.style(darkSteel(w) * 1.4, METAL);
    b.extrudeUp({V2(0.085, -0.040), V2(0.108, -0.020), V2(0.112, 0), V2(0.108, 0.020), V2(0.085, 0.040), V2(0.080, 0.0)}, 0.0004, 0.0020);  // a bright worn lip
    b.pop();
    // The stomp pad welded across the shaft, with ribs and gussets.
    b.style(darkSteel(w), METAL);
    b.box(V3(0.24, 0.0165, 0), V3(0.075, 0.006, 0.12), 0.0015);
    for (int i = 0; i < 4; i++) b.box(V3(0.215 + i * 0.017, 0.0215, 0), V3(0.005, 0.0032, 0.112), 0.0008);
    b.style(col, PAINT);
    for (int s = -1; s <= 1; s += 2) b.extrude({V2(0.205, 0.012), V2(0.275, 0.012), V2(0.275, 0.0135), V2(0.2, 0.0135)}, 0.006, s * 0.038);
    b.style(darkSteel(w) * 1.7, METAL);
    stampTop(b, "BEAD BREAKER", V3(0.0, r * 0.87, 0), 0.0015, 0.0004);
    grease(b, V3(-0.1, 0.012, 0), V3(0.3, 0.012, 0), 0.003, 8, w, 102, 0.008);
}

Reg tireIronReg({"tire_iron", "TIRE IRON", "REPAIR", "Hand tool: levers a tyre bead on and off the rim; origin at the grip (spoons: the middle of the pair)", 0.5,
                 {
                     {"TYRE SPOONS", "A pair of spoon levers with dipped grips", {finParam(3), paintParam(4, "coat", "GRIP DIP"), choice("length", "LENGTH", 1, {"12 IN", "16 IN", "20 IN"}), wearParam()}, lifted(spoons)},
                     {"BEAD BREAKER", "Long hex bar with a duckbill foot, stomp pad and spoon tail", {paintParam(0, "paint", "PAINT"), wearParam()}, lifted(beadBreaker)},
                 }});

// ======================================================================================
// 8. CV AXLE
// ======================================================================================

// An accordion boot along X from x0 (radius rA) to x1 (radius rB), n pleats, collars at both ends.
void accordion(B& b, double x0, double x1, double rA, double rB, int n, int segs = 9) {
    const double L = std::fabs(x1 - x0), cw = 0.007;
    const double dir = x1 >= x0 ? 1 : -1;
    std::vector<V2> pr = {V2(rA - 0.0015, 0), V2(rA + 0.0008, 0.0015), V2(rA + 0.0008, cw)};
    const double span = L - 2 * cw;
    for (int i = 0; i < n; i++) {
        const double t0 = (i + 0.5) / n, t1 = (i + 1.0) / n;
        const double e0 = rA + (rB - rA) * t0, e1 = rA + (rB - rA) * t1;
        pr.push_back(V2(e0 + 0.0058 + 0.0004 * i, cw + span * t0));
        pr.push_back(V2(e1 - 0.0034, cw + span * t1));
    }
    pr.push_back(V2(rB + 0.0008, L - cw));
    pr.push_back(V2(rB + 0.0008, L - 0.0015));
    pr.push_back(V2(rB - 0.0015, L));
    latheAB(b, V3(x0, 0, 0), V3(x0 + dir * L, 0, 0), pr, segs, true);
}

// A splined stub along a -> c (round bar with longitudinal ridges and a snap-ring groove).
void splineStub(B& b, const V3& a, const V3& c, double r, int ridges, double w, bool threaded) {
    const V3 d = norm(c - a);
    const double L = len(c - a);
    rodAB(b, a, c, r * 0.92, 0.0012, 10, false);
    b.color(finish(2, w).c * 0.9);
    for (int k = 0; k < ridges; k++) {
        const double t = 2 * M_PI * k / ridges;
        const V3 rad(0, std::cos(t), std::sin(t));
        b.beam(a + d * 0.003 + rad * (r * 0.93), c - d * 0.003 + rad * (r * 0.93), 0.0022, 0.0020, rad, 0);
    }
    if (threaded) knurl(b, c - d * (L * 0.28), c, r * 0.95, 0.0012, 3, 8, false);
}

struct CvStyle {
    V3 shaft;
    Mat shaftMat;
    V3 housing;
    Mat housingMat;
    V3 boot;
    V3 clamp;
    double shaftR, stubR;
};

void cvAxle(B& b, const P& p, const CvStyle& st, int pleatsIn, int pleatsOut, double greaseAmt) {
    const double w = p("wear");
    const double L = p("length"), hl = L / 2;
    const double rs = st.shaftR;
    const V3 boot = st.boot * (1 - w * 0.2);
    const double stubL = 0.058;
    // Inner (tripod) end at -X.
    const double xi0 = -hl + stubL;
    // Inner housing: a short barrel with a lip where the boot clamps on.
    const double hi = 0.066;
    b.style(grime(st.housing, w * 0.8), st.housingMat);
    latheAB(b, V3(xi0 - 0.004, 0, 0), V3(xi0 - 0.004 + hi, 0, 0),
            {V2(0.008, 0), V2(0.020, 0.0), V2(0.024, 0.006), V2(0.0335, 0.014), V2(0.0345, 0.020), V2(0.0345, 0.046), V2(0.0325, 0.052), V2(0.0265, 0.056), V2(0.0225, 0.060), V2(0.0225, hi)}, 9, true);
    b.style(grime(st.housing, w) * 0.7, st.housingMat);
    for (int k = 0; k < 3; k++) {  // the tripod lobes' bulges
        const double t = 2 * M_PI * k / 3 + 0.5;
        b.box(V3(xi0 + 0.0215, std::cos(t) * 0.0348, std::sin(t) * 0.0348), V3(0.026, 0.008, 0.008), 0.0);
    }
    splineStub(b, V3(-hl, 0, 0), V3(xi0, 0, 0), st.stubR, 6, w, false);
    // Outer (wheel) end at +X: a big bell housing, stub shaft with threads.
    const double xo1 = hl - stubL - 0.012;
    const double ho = 0.074;
    b.style(grime(st.housing, w * 0.8), st.housingMat);
    latheAB(b, V3(xo1 + 0.006, 0, 0), V3(xo1 + 0.006 - ho, 0, 0),
            {V2(0.012, 0), V2(0.020, 0), V2(0.026, 0.006), V2(0.0385, 0.016), V2(0.0425, 0.028), V2(0.0425, 0.050), V2(0.0385, 0.058), V2(0.0295, 0.0635), V2(0.0245, 0.0665), V2(0.0245, ho)}, 9, true);
    b.style(grime(st.housing, w) * 0.65, st.housingMat);
    for (int k = 0; k < 4; k++) {  // the bell's cast ribs
        const double t = 2 * M_PI * k / 4 + 0.78;
        b.beam(V3(xo1 - 0.044 + 0.016, std::cos(t) * 0.0405, std::sin(t) * 0.0405), V3(xo1 - 0.044 - 0.006, std::cos(t) * 0.0405, std::sin(t) * 0.0405), 0.005, 0.0022, V3(0, std::cos(t), std::sin(t)), 0);
    }
    splineStub(b, V3(xo1 + 0.004, 0, 0), V3(hl, 0, 0), st.stubR, 6, w, true);
    // Shaft.
    b.style(grime(st.shaft, w * 0.7), st.shaftMat);
    rodAB(b, V3(xi0 + hi - 0.012, 0, 0), V3(xo1 - ho + 0.012, 0, 0), rs, 0.001, 8);
    // Boots.
    b.style(boot, MATTE);
    const double bi0 = xi0 - 0.004 + hi - 0.006, bo0 = xo1 + 0.006 - ho + 0.006;
    const double bLen = (bo0 - bi0);
    const double lIn = std::min(0.115, bLen * 0.36), lOut = std::min(0.125, bLen * 0.38);
    accordion(b, bi0, bi0 + lIn, 0.0235, rs + 0.0015, pleatsIn);
    accordion(b, bo0, bo0 - lOut, 0.0255, rs + 0.0015, pleatsOut);
    // Clamps.
    b.style(st.clamp, METAL);
    b.cyl(V3(bi0 + 0.0, 0, 0), V3(bi0 + 0.007, 0, 0), 0.0262, 10, false);
    b.cyl(V3(bi0 + lIn - 0.007, 0, 0), V3(bi0 + lIn, 0, 0), rs + 0.0070, 10, false);
    b.cyl(V3(bo0 - 0.007, 0, 0), V3(bo0, 0, 0), 0.0282, 10, false);
    b.cyl(V3(bo0 - lOut, 0, 0), V3(bo0 - lOut + 0.007, 0, 0), rs + 0.0070, 10, false);
    // Grease and dirt.
    grease(b, V3(bi0, 0.0, 0), V3(bi0 + lIn, 0.0, 0), 0.0045, (int)(2 * greaseAmt / std::max(0.01, w) + 0.5), w, 111, 0.022);
    grease(b, V3(bo0, 0.0, 0), V3(bo0 - lOut, 0.0, 0), 0.0050, (int)(2 * greaseAmt / std::max(0.01, w) + 0.5), w, 112, 0.024);
    if (greaseAmt > 0.5) {
        b.style(V3(0.35, 0.16, 0.06), MATTE);  // rust bloom on the housings
        for (int i = 0; i < 4; i++) b.box(V3(xi0 + 0.01 + 0.01 * i, 0.0338 * (i % 2 ? 1 : -1) * 0.6, 0.03 - 0.012 * i), V3(0.007, 0.0025, 0.006), 0.0);
    }
}

void cvNew(B& b, const P& p) {
    CvStyle st{V3(0.76, 0.78, 0.80), CHROME, V3(0.60, 0.62, 0.62), METAL, V3(0.04, 0.04, 0.045), V3(0.78, 0.8, 0.82), 0.0125, 0.0128};
    cvAxle(b, p, st, 4, 4, 0.0);
}
void cvUsed(B& b, const P& p) {
    CvStyle st{V3(0.34, 0.33, 0.32), METAL, V3(0.30, 0.29, 0.27), METAL, V3(0.05, 0.045, 0.035), V3(0.25, 0.24, 0.23), 0.0125, 0.0128};
    cvAxle(b, p, st, 3, 3, 1.0);
}
void cvHeavy(B& b, const P& p) {
    CvStyle st{V3(0.82, 0.62, 0.10), METAL, V3(0.10, 0.25, 0.62), PAINT, V3(0.03, 0.03, 0.035), V3(0.10, 0.25, 0.62), 0.0165, 0.0150};
    cvAxle(b, p, st, 4, 4, 0.0);
}

Reg cvAxleReg({"cv_axle", "CV AXLE", "REPAIR", "Spare part: half shaft with two CV joints in boots; origin at the middle, long axis X (inner end -X)", 0.75,
               {
                   {"NEW", "Bright steel shaft, black boots, fresh clamps", {num("length", "LENGTH", 0.70, 0.60, 0.80), wearParam(0.0)}, lifted(cvNew)},
                   {"USED GREASY", "Pulled from a wreck: greasy, dark, rusty", {num("length", "LENGTH", 0.70, 0.60, 0.80), wearParam(0.8)}, lifted(cvUsed)},
                   {"HEAVY DUTY", "Chromoly shaft (gold) with blue anodised joints", {num("length", "LENGTH", 0.68, 0.60, 0.80), wearParam(0.0)}, lifted(cvHeavy)},
               }});

}  // namespace
