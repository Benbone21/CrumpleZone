#include "people.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace people {

int lastTris = 0;

namespace {

const V3 kUp(0, 1, 0);

// Body measures (m): a 1.78 m adult in boots.
const double kAnkle = 0.085;                 // ground to ankle joint
const double kThigh = 0.44, kShin = 0.43;     // hip to knee, knee to ankle
const double kUpperArm = 0.29, kForearm = 0.255;
const double kHipW = 0.092;                   // hip joint off the middle
const double kWaist = 0.10;                   // pelvis to the chest's pivot
const double kShoulderY = 0.37, kShoulderW = 0.182;  // shoulder joints, in the chest's frame
const double kNeckY = 0.455;
const double kCrouchDrop = 0.415, kCrouchLean = 0.75;  // crouched: the hips this much lower (to about 0.52 m), the chest leaning forward (rad)
const double kHipFloor = 0.48;  // first person: the hips never lower than this (a deep crouch, on your heels; not sat on the ground)
const double kHeadDown = 0.9, kHeadUp = 0.75;       // how far the head pitches (rad) on the neck
const double kGripX = 0.072, kGripY = 0.03;   // a handle in the closed hand, from the wrist

double smooth01(double x) {
    x = clampd(x, 0, 1);
    return x * x * (3 - 2 * x);
}
V3 lerpV(const V3& a, const V3& b, double t) { return a + (b - a) * t; }
V3 perpTo(const V3& v, const V3& n) {  // v with its n part taken out, normalised (any perpendicular if nothing's left)
    V3 r = v - n * dot(v, n);
    if (len(r) < 1e-6) r = cross(n, std::fabs(n.y) < 0.9 ? kUp : V3(1, 0, 0));
    return norm(r);
}

// ---- Rigid pieces, built once per outfit (and grip) and turned onto the bones ----

struct Basis {
    V3 o, x, y, z;  // origin and axes (right-handed: x cross y = z)
};

void place(mk::Mesh& out, const mk::Mesh& m, const Basis& b) {
    const size_t at = out.v.size();
    out.v.resize(at + m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        const mk::MVert& q = m.v[i];
        mk::MVert& o = out.v[at + i];
        const V3 p = b.o + b.x * q.p[0] + b.y * q.p[1] + b.z * q.p[2], n = b.x * q.n[0] + b.y * q.n[1] + b.z * q.n[2];
        o.p[0] = (float)p.x, o.p[1] = (float)p.y, o.p[2] = (float)p.z;
        o.n[0] = (float)n.x, o.n[1] = (float)n.y, o.n[2] = (float)n.z;
        o.c[0] = q.c[0], o.c[1] = q.c[1], o.c[2] = q.c[2];
        o.mat = q.mat;
    }
}

// ---- Smooth surfaces: rows of points round a middle, joined ----

// Rows of `n` points, each row round its middle (which way is out); `col`: the band from a row to the next.
struct Rows {
    int n = 0;
    bool closed = true;  // (false: an open strip, its ends not joined)
    std::vector<V3> pts, mid, col;
    int rows() const { return (int)mid.size(); }
};

// Joins the rows into a smooth surface (each normal from its neighbours, turned outward). `tip0`/`tip1`: close the first
// or last row to that point (null: leave it open).
void skin(mk::B& b, const Rows& r, const V3* tip0 = nullptr, const V3* tip1 = nullptr, const V3& tipCol = V3()) {
    const int R = r.rows(), n = r.n;
    if (R < 2 || n < 3) return;
    std::vector<V3> nor(r.pts.size());
    for (int i = 0; i < R; i++)
        for (int k = 0; k < n; k++) {
            const V3& q = r.pts[i * n + k];
            const int k1 = r.closed ? (k + 1) % n : std::min(n - 1, k + 1), k0 = r.closed ? (k + n - 1) % n : std::max(0, k - 1);
            const V3 around = r.pts[i * n + k1] - r.pts[i * n + k0];
            const V3 along = r.pts[std::min(R - 1, i + 1) * n + k] - r.pts[std::max(0, i - 1) * n + k];
            V3 nn = cross(around, along);
            if (len(nn) < 1e-12) nn = q - r.mid[i];
            nn = len(nn) > 1e-12 ? norm(nn) : kUp;
            if (dot(nn, q - r.mid[i]) < 0) nn = nn * -1.0;
            nor[i * n + k] = nn;
        }
    for (int i = 0; i + 1 < R; i++) {
        b.color(r.col[i]);
        for (int k = 0; k < (r.closed ? n : n - 1); k++) {
            const int a = i * n + k, a1 = i * n + (k + 1) % n, c = (i + 1) * n + k, c1 = (i + 1) * n + (k + 1) % n;
            b.triN(r.pts[a], r.pts[c], r.pts[c1], nor[a], nor[c], nor[c1]);
            b.triN(r.pts[a], r.pts[c1], r.pts[a1], nor[a], nor[c1], nor[a1]);
        }
    }
    auto cap = [&](int i, const V3& tip) {
        b.color(tipCol);
        const V3 tn = norm(tip - r.mid[i]);
        for (int k = 0; k < n; k++) {
            const int a = i * n + k, a1 = i * n + (k + 1) % n;
            const V3 na = norm(nor[a] + tn), na1 = norm(nor[a1] + tn);
            b.triN(r.pts[a], r.pts[a1], tip, na, na1, tn);
        }
    };
    if (tip0) cap(0, *tip0);
    if (tip1) cap(R - 1, *tip1);
}

// A body's cross-section: rounder than an ellipse, squarer than a box (a jacket over shoulders and ribs).
const double kSquare = 2.6;
double sp(double c) { return (c < 0 ? -1.0 : 1.0) * std::pow(std::fabs(c), 2.0 / kSquare); }
// Round a ring of n: each point's cos and sin, and the same squared off (sp). Worked out once.
struct RingTab {
    double c[33], s[33], sc[33], ss[33];
};
const RingTab& ringTab(int n) {
    static const std::vector<RingTab> t = [] {
        std::vector<RingTab> v(33);
        for (int m = 3; m <= 32; m++)
            for (int k = 0; k < m; k++) {
                const double a = 2 * M_PI * k / m;
                v[m].c[k] = std::cos(a), v[m].s[k] = std::sin(a), v[m].sc[k] = sp(v[m].c[k]), v[m].ss[k] = sp(v[m].s[k]);
            }
        return v;
    }();
    return t[std::min(32, std::max(3, n))];
}
// The row round a torso at height y in `B` (half-width a, front df, back db).
void torsoRow(Rows& r, const Basis& B, double y, double a, double df, double db, const V3& col) {
    const RingTab& T = ringTab(r.n);
    const V3 o = B.o + B.y * y;
    for (int k = 0; k < r.n; k++) r.pts.push_back(o + B.x * ((T.c[k] >= 0 ? df : db) * T.sc[k]) + B.z * (a * T.ss[k]));
    r.mid.push_back(B.o + B.y * y);
    r.col.push_back(col);
}
// How far in front of (or behind, back) the middle a torso's surface is at z across, for half-width a and depth d.
double surfX(double a, double d, double z) {
    const double u = clampd(std::fabs(z) / a, 0, 0.999);
    return d * std::pow(1 - std::pow(u, kSquare), 1 / kSquare);
}

// The jacket's (or hoodie's, vest's) rows above the waist, in the chest's frame: height, half-width, front, back.
const double kUpper[][4] = {{0.03, 0.166, 0.106, 0.096}, {0.13, 0.168, 0.113, 0.098}, {0.22, 0.176, 0.123, 0.104}, {0.30, 0.181, 0.118, 0.108},
                            {0.355, 0.176, 0.106, 0.104}, {0.395, 0.152, 0.092, 0.096}, {0.43, 0.108, 0.074, 0.078}, {0.462, 0.072, 0.059, 0.061}};
const int kUpperN = 8;
// The trousers' seat, in the hips' frame: from the crotch up to the waistband (under the jacket's hem).
const double kSeat[][4] = {{-0.125, 0.118, 0.07, 0.085}, {-0.07, 0.16, 0.094, 0.118}, {0.0, 0.168, 0.098, 0.112}, {0.06, 0.163, 0.094, 0.098}, {0.1, 0.16, 0.092, 0.094}};
// Interpolated in a table (the first column the height).
void upperAt(double y, double& a, double& df, double& db) {
    int i = 0;
    while (i + 2 < kUpperN && kUpper[i + 1][0] < y) i++;
    const double t = clampd((y - kUpper[i][0]) / (kUpper[i + 1][0] - kUpper[i][0]), 0, 1);
    a = kUpper[i][1] + (kUpper[i + 1][1] - kUpper[i][1]) * t;
    df = kUpper[i][2] + (kUpper[i + 1][2] - kUpper[i][2]) * t;
    db = kUpper[i][3] + (kUpper[i + 1][3] - kUpper[i][3]) * t;
}
// Where the jacket's hem is (in the hips' frame) for each top.
double hemY(const Look& L) { return L.top == 0 ? 0.085 : L.top == 1 ? -0.02 : 0.07; }
V3 topColor(const Look& L) { return L.jacket; }
V3 sleeveColor(const Look& L) { return L.top == 2 ? L.shirt : L.jacket; }

// A limb swept smoothly from `a` through the joint `j` to `e` (the joint rounded over `round` of each bone), its radius
// along it from prof(u, r, rf, col): u 0 at a, 1 at the joint, 2 at e (and a little either side: u0..u1).
template <class Prof>
void limbSweep(mk::B& b, const V3& a, const V3& j, const V3& e, double u0, double u1, int det, int n, const V3& ref, Prof prof, const V3* capEnd = nullptr,
               const V3& capCol = V3()) {
    const double rho = 0.16;
    static const double us2[] = {0.30, 0.62, 1 - rho, 1, 1 + rho, 1.38, 1.72, 1.9};
    static const double us1[] = {0.5, 1 - rho, 1 + rho, 1.5, 1.88};
    static const double us0[] = {1.0};
    const double* us = det >= 2 ? us2 : det == 1 ? us1 : us0;
    const int nu = det >= 2 ? 8 : det == 1 ? 5 : 1;
    std::vector<double> u;
    u.push_back(u0);
    for (int i = 0; i < nu; i++) u.push_back(us[i]);
    u.push_back(u1);
    const V3 q0 = j + (a - j) * rho, q1 = j + (e - j) * rho;
    auto at = [&](double x) {
        if (x <= 1 - rho) return a + (j - a) * x;
        if (x >= 1 + rho) return j + (e - j) * (x - 1);
        const double t = (x - (1 - rho)) / (2 * rho);
        return q0 * ((1 - t) * (1 - t)) + j * (2 * t * (1 - t)) + q1 * (t * t);
    };
    Rows r;
    r.n = n;
    r.pts.reserve(u.size() * n), r.mid.reserve(u.size()), r.col.reserve(u.size());
    V3 up;
    for (size_t i = 0; i < u.size(); i++) {
        const V3 p = at(u[i]);
        const V3 t = norm(at(u[std::min(u.size() - 1, i + 1)]) - at(u[i == 0 ? 0 : i - 1]));
        up = i == 0 ? perpTo(ref, t) : perpTo(up, t);
        const V3 side = cross(t, up);
        double rr = 0.05, rf = 0.05;
        V3 col;
        prof(u[i], rr, rf, col);
        const RingTab& T = ringTab(n);
        for (int k = 0; k < n; k++) r.pts.push_back(p + up * (rf * T.c[k]) + side * (rr * T.s[k]));
        r.mid.push_back(p);
        r.col.push_back(col);
    }
    skin(b, r, nullptr, capEnd, capCol);
}
// A piecewise-straight profile: (u, value) pairs, u rising.
double tab(const double (*t)[2], int n, double u) {
    if (u <= t[0][0]) return t[0][1];
    for (int i = 0; i + 1 < n; i++)
        if (u <= t[i + 1][0]) return t[i][1] + (t[i + 1][1] - t[i][1]) * (u - t[i][0]) / (t[i + 1][0] - t[i][0]);
    return t[n - 1][1];
}

// Pieces are built in their own frame: x ahead (the toes, the face, the fingers), y up (the
// back of the hand), z to the right.

// A hand (gloved or bare): wrist at the origin, fingers along +x, the back of the hand +y. side 0 left (thumb
// toward +z), 1 right (thumb toward -z). grip 0..1 curls the fingers round a handle at
// (kGripX, -kGripY) along z. detail 0: a mitten.
void buildHand(mk::B& b, const Look& L, int side, double grip, int detail) {
    const double t = side == 0 ? 1.0 : -1.0;  // the thumb's side
    const int sg = detail >= 2 ? 7 : 4;
    const V3 main = L.gloved ? L.gloves : L.skin;
    if (L.gloved) {  // a short gauntlet with a strap
        b.style(L.gloves * 0.8, mk::MATTE);
        b.push();
        b.scale(V3(1, 0.78, 1));
        b.cone(V3(-0.05, 0, 0), V3(0.012, 0, 0), 0.045, 0.041, detail >= 2 ? 10 : 6, true);
        b.pop();
        if (detail >= 1) {
            b.style(L.glovePad, mk::MATTE);
            b.box(V3(-0.018, 0.031, 0), V3(0.026, 0.008, 0.07), 0.003);  // the strap's tab
        }
    } else {  // the wrist
        b.style(L.skin * 0.95, mk::MATTE);
        b.cone(V3(-0.04, 0, 0), V3(0.015, 0, 0), 0.026, 0.027, detail >= 2 ? 8 : 5, false);
    }
    // The palm: rounded, a little cupped.
    b.style(main, mk::MATTE);
    b.sphere(V3(0.048, 0, 0), V3(0.052, 0.019, 0.044), detail >= 2 ? 1 : 0);
    if (L.gloved && detail >= 1) {
        b.style(L.glovePad, mk::MATTE);
        b.sphere(V3(0.058, 0.012, 0), V3(0.034, 0.01, 0.036), 0);  // padded back
        b.box(V3(0.09, 0.012, 0), V3(0.014, 0.01, 0.078), 0.004);  // knuckle guard
    }
    b.style(main, mk::MATTE);
    struct Finger {
        double z, x, l0, l1, l2, r;
    };
    static const Finger fingers[4] = {{0.029, 0.092, 0.043, 0.027, 0.022, 0.0098},
                                      {0.0095, 0.096, 0.047, 0.03, 0.023, 0.0102},
                                      {-0.0105, 0.093, 0.044, 0.028, 0.022, 0.0097},
                                      {-0.029, 0.085, 0.034, 0.022, 0.019, 0.0086}};
    const double c0 = (8 + 62 * grip) * M_PI / 180, c1 = (10 + 88 * grip) * M_PI / 180, c2 = (6 + 48 * grip) * M_PI / 180;
    const double fat = L.gloved ? 1.0 : 0.88;
    if (detail == 0) {  // far off: a mitten, the four as one block, the thumb a stub
        const double a1 = c0 + c1 * 0.5;
        const V3 a(0.092, 0, 0), d1(std::cos(a1), -std::sin(a1), 0);
        b.beam(a, a + d1 * 0.075, 0.075, 0.024, V3(-d1.y, d1.x, 0), 0.008);
        b.beam(V3(0.03, -0.008, 0.03 * t), V3(0.07, -0.025, 0.045 * t), 0.022, 0.022, V3(0, 1, 0));
        return;
    } else if (detail == 1) {  // the middle distance: each finger one piece, bent as the whole
        for (int k = 0; k < 4; k++) {
            const Finger& f = fingers[k];
            const double a1 = (c0 + c1 + c2) * 0.45;
            const V3 p(f.x, 0.002, f.z * t), d1(std::cos(a1), -std::sin(a1), 0);
            b.cone(p, p + d1 * (f.l0 + f.l1 + f.l2) * 0.8, f.r * fat, f.r * fat * 0.85, 4, true);
        }
    } else {
        for (int k = 0; k < 4; k++) {
            const Finger& f = fingers[k];
            // Each joint bends the next bone down toward the palm; the fingers fan a little open.
            const double spread = (k - 1.5) * -0.05 * (1 - grip) * t;
            V3 d = norm(V3(std::cos(spread), 0, std::sin(spread)));
            V3 p(f.x, 0.002, f.z * t);
            const double ls[3] = {f.l0, f.l1, f.l2}, cs[3] = {c0, c1, c2};
            double ang = 0;
            for (int j = 0; j < 3; j++) {
                ang += cs[j];
                const V3 dj = norm(V3(d.x * std::cos(ang), -std::sin(ang), d.z * std::cos(ang)));
                const V3 q = p + dj * ls[j];
                const double r0 = f.r * fat * (1 - 0.06 * j), r1 = f.r * fat * (1 - 0.06 * (j + 1));
                b.cone(p, q, r0, r1, sg, false);
                if (detail >= 2 || j == 2) b.sphere(q, V3(r1, r1, r1), 0);  // (the knuckles round, close up; the tip always)
                p = q;
            }
        }
    }
    // The thumb: out from the heel of the hand, round the handle as it grips.
    {
        const V3 base(0.024, -0.008, 0.034 * t);
        const V3 open = norm(V3(0.62, -0.18, 0.76 * t)), shut = norm(V3(0.62, -0.62, 0.45 * t));
        V3 d = norm(lerpV(open, shut, grip));
        V3 p = base;
        const double ls[3] = {0.036, 0.03, 0.026};
        for (int j = 0; j < 3; j++) {
            const V3 q = p + d * ls[j];
            const double r0 = (0.0125 - 0.0012 * j) * fat, r1 = (0.0125 - 0.0012 * (j + 1)) * fat;
            if (detail >= 2 || j < 2) b.cone(p, q, r0, r1, sg, j == 1 && detail < 2);
            if (detail >= 2) b.sphere(q, V3(r1, r1, r1), 0);
            p = q;
            // Curls in across the palm toward the fingers' side.
            d = norm(d + V3(0.08, -0.25 * grip, -0.35 * t * (0.3 + grip)));
        }
        b.sphere(base, V3(0.018, 0.015, 0.018) * fat, 0);
    }
}

// A work boot: ankle joint at the origin, toes +x, sole at y = -kAnkle.
void buildBoot(mk::B& b, const Look& L, int detail) {
    const int sg = detail >= 2 ? 10 : detail == 1 ? 7 : 5, sd = detail >= 2 ? 1 : 0;
    if (detail == 0) {
        b.style(L.soles, mk::MATTE);
        b.box(V3(0.065, -0.072, 0), V3(0.29, 0.026, 0.104), 0.008);
        b.style(L.boots, mk::MATTE);
        b.box(V3(0.06, -0.04, 0), V3(0.24, 0.05, 0.096), 0.02);
        b.cyl(V3(-0.005, -0.05, 0), V3(-0.008, 0.1, 0), 0.05, sg, true);
        return;
    }
    // A lug sole cut to a foot's outline (narrow at the heel, wide across the ball), a welt round its top.
    static const std::vector<mk::V2> sole = {{-0.082, 0},     {-0.075, 0.035}, {-0.05, 0.046}, {0.0, 0.047},  {0.07, 0.053},   {0.13, 0.056},
                                             {0.18, 0.048},   {0.21, 0.03},    {0.219, 0.0},   {0.21, -0.03}, {0.18, -0.05},   {0.13, -0.056},
                                             {0.07, -0.052},  {0.0, -0.046},   {-0.05, -0.044}, {-0.075, -0.033}};
    b.style(L.soles, mk::MATTE);
    b.extrudeUp(sole, 0.025, -kAnkle);
    if (detail >= 2) {
        b.style(L.soles * 1.25, mk::MATTE);
        for (int k = 0; k < 5; k++) b.box(V3(0.17 - k * 0.055, -kAnkle + 0.0015, 0), V3(0.022, 0.004, 0.09));  // lugs (their faces flush with the sole's)
    }
    b.style(L.boots * 0.62, mk::MATTE);
    b.push();
    b.scale(V3(1.012, 1, 1.03));
    b.extrudeUp(sole, 0.007, -0.061);  // the welt
    b.pop();
    // The upper: a rounded toe cap, the vamp over the instep, the heel cupped; the shaft round the ankle.
    b.style(L.boots, mk::MATTE);
    b.sphere(V3(0.135, -0.052, 0), V3(0.084, 0.032, 0.05), sd);  // (each sits on the sole, none through it)
    b.sphere(V3(0.05, -0.04, 0), V3(0.11, 0.044, 0.052), sd);
    b.sphere(V3(-0.035, -0.04, 0), V3(0.052, 0.045, 0.046), sd);
    b.push();
    b.scale(V3(1.12, 1, 0.95));
    b.cone(V3(-0.005, -0.05, 0), V3(-0.008, 0.105, 0), 0.05, 0.049, sg, true);
    if (detail >= 2) {
        b.style(L.boots * 0.7, mk::MATTE);
        b.torus(V3(-0.008, 0.103, 0), 0.049, 0.007, sg, 4);  // padded collar
    }
    b.pop();
    // Tongue and laces up the front, the pull tab behind.
    b.style(L.boots * 0.82, mk::MATTE);
    b.beam(V3(0.1, -0.022, 0), V3(0.048, 0.106, 0), 0.04, 0.012, V3(1, 0.35, 0), 0.004);
    b.style(L.laces, mk::MATTE);
    for (int k = 0; k < 5; k++) {
        const double s = k / 4.0;
        const V3 c = lerpV(V3(0.104, -0.016, 0), V3(0.058, 0.09, 0), s);
        b.box(c, V3(0.007, 0.006, 0.05 - 0.003 * k));
    }
    if (detail >= 2) {
        b.style(L.boots * 0.55, mk::MATTE);
        b.cyl(V3(-0.064, 0.075, 0), V3(-0.07, 0.115, 0), 0.008, 4, true);  // pull tab
        b.style(L.boots * 1.15, mk::MATTE);
        b.sphere(V3(0.172, -0.05, 0), V3(0.04, 0.024, 0.04), 0);  // the toe scuffed lighter
    }
}

// The trousers' details round the seat (the hips' frame): the belt and buckle where the jacket shows them, back pockets,
// the fly.
void buildSeat(mk::B& b, const Look& L, int detail) {
    if (detail == 0) return;
    const double G = L.girth;
    if (L.top == 0) {  // the belt (a work jacket stops above it)
        Rows r;
        r.n = detail >= 2 ? 16 : 10;
        Basis id{V3(), V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
        const V3 belt(0.2, 0.13, 0.08);
        torsoRow(r, id, 0.052, 0.166 * G, 0.097 * G, 0.1 * G, belt);
        torsoRow(r, id, 0.088, 0.164 * G, 0.095 * G, 0.098 * G, belt);
        skin(b, r);
        b.style(V3(0.62, 0.58, 0.5), mk::METAL);
        b.box(V3(0.097 * G + 0.004, 0.07, 0), V3(0.01, 0.034, 0.046), 0.004);  // buckle
        if (detail >= 2) {
            b.style(L.pantsDark, mk::MATTE);
            for (double z : {-0.11, -0.05, 0.05, 0.11}) b.box(V3(surfX(0.166 * G, 0.097 * G, z * G) + 0.002, 0.07, z * G), V3(0.006, 0.045, 0.012));  // loops
        }
    }
    b.style(L.pantsDark, mk::MATTE);
    for (int s = -1; s <= 1; s += 2) {  // back pockets
        const double z = s * 0.068 * G;
        b.push();
        b.move(-surfX(0.165 * G, 0.115 * G, z) - 0.001, -0.02, z);
        b.roll(s * 4.0);
        b.box(V3(), V3(0.006, 0.085, 0.078), 0.003);
        b.pop();
    }
    b.box(V3(0.098 * G + 0.001, -0.02, 0.008), V3(0.004, 0.11, 0.01));  // the fly
    if (L.legs == 1 && detail >= 2) {  // a hammer loop
        b.box(V3(-0.02, -0.06, 0.168 * G + 0.003), V3(0.022, 0.08, 0.006), 0.002);
    }
}

// The top's details (the chest's frame): pockets, zip, collar or hood. `own`: your own, in first person (nothing round the
// neck: it'd be round your eyes).
void buildChest(mk::B& b, const Look& L, int detail, bool own) {
    if (detail == 0) return;
    const double G = L.girth;
    auto front = [&](double y, double z) {
        double a, df, db;
        upperAt(y, a, df, db);
        return surfX(a * G, df * G, z);
    };
    auto back = [&](double y, double z) {
        double a, df, db;
        upperAt(y, a, df, db);
        return -surfX(a * G, db * G, z);
    };
    const V3 dark = L.jacket * 0.7;
    if (L.top == 0 || L.top == 2) {  // zip up the front
        std::vector<V3> zip;
        for (double y : {-0.01, 0.08, 0.16, 0.24, 0.32, 0.38, 0.43}) zip.push_back(V3(front(y, 0) + 0.002, y, 0));
        b.style(V3(0.12, 0.12, 0.13), mk::METAL);
        b.tube(zip, 0.0045, 4, false, false);
        b.box(V3(front(0.3, 0) + 0.006, 0.3, 0.006), V3(0.004, 0.022, 0.01), 0.002);  // its pull
    }
    if (L.top == 0) {  // a work jacket: chest pockets with flaps and snaps, a yoke across the back, the collar
        for (int s = -1; s <= 1; s += 2) {
            const double z = s * 0.075 * G;
            b.style(L.jacket * 0.9, mk::MATTE);
            b.box(V3(front(0.24, z) + 0.001, 0.24, z), V3(0.008, 0.085, 0.07), 0.003);
            b.style(dark, mk::MATTE);
            b.box(V3(front(0.29, z) + 0.004, 0.288, z), V3(0.008, 0.026, 0.076), 0.003);
            if (detail >= 2) {
                b.style(V3(0.55, 0.5, 0.42), mk::METAL);
                b.sphere(V3(front(0.29, z) + 0.009, 0.284, z), V3(0.004, 0.005, 0.005), 0);
                b.style(dark * 0.9, mk::MATTE);  // hand-warmer pocket openings
                b.beam(V3(front(0.04, s * 0.06) + 0.002, 0.03, s * 0.06 * G), V3(front(0.13, s * 0.1) + 0.002, 0.13, s * 0.1 * G), 0.006, 0.004, V3(1, 0, 0));
            }
        }
        b.style(dark, mk::MATTE);
        b.box(V3(back(0.33, 0) - 0.002, 0.33, 0), V3(0.006, 0.012, 0.25 * G), 0.002);  // yoke seam
        if (!own) {  // the collar standing round the neck, folded over
            b.cone(V3(-0.006, 0.415, 0), V3(-0.014, 0.49, 0), 0.078, 0.07, detail >= 2 ? 14 : 9, false);
            b.torus(V3(-0.014, 0.49, 0), 0.07, 0.009, detail >= 2 ? 14 : 9, 3);
        }
    } else if (L.top == 1) {  // a hoodie: the kangaroo pocket, the hood bunched behind the neck, its strings
        b.style(L.jacket * 0.94, mk::MATTE);
        b.box(V3(front(0.08, 0) + 0.002, 0.075, 0), V3(0.012, 0.13, 0.2 * G), 0.01);
        b.style(dark, mk::MATTE);
        for (int s = -1; s <= 1; s += 2) b.beam(V3(front(0.02, 0.1) + 0.006, 0.02, s * 0.098 * G), V3(front(0.13, 0.08) + 0.006, 0.13, s * 0.082 * G), 0.006, 0.004, V3(1, 0, 0));
        if (!own) {
            // The hood lies folded round the back of the neck (open at the throat) and drapes down the back.
            b.style(L.jacket * 0.92, mk::MATTE);
            b.push();
            b.move(-0.012, 0.45, 0);
            b.yaw(180 - 120);
            b.scale(V3(0.9, 1, 1.12));
            b.torus(V3(), 0.085, 0.032, detail >= 2 ? 16 : 10, 6, 240);
            b.pop();
            b.sphere(V3(back(0.38, 0) - 0.012, 0.385, 0), V3(0.028, 0.075, 0.1), detail >= 2 ? 1 : 0);
            b.style(L.shirt * 0.9, mk::MATTE);
            for (int s = -1; s <= 1; s += 2) {
                b.beam(V3(0.055, 0.43, s * 0.03), V3(front(0.3, 0.03) + 0.006, 0.3, s * 0.032), 0.005, 0.005, V3(1, 0, 0));
                b.sphere(V3(front(0.3, 0.03) + 0.006, 0.296, s * 0.032), V3(0.005, 0.01, 0.005), 0);
            }
        }
    } else {  // a quilted vest over a flannel shirt: a chest pocket, the shirt's collar
        b.style(dark, mk::MATTE);
        b.box(V3(front(0.24, -0.07) + 0.002, 0.24, -0.075 * G), V3(0.006, 0.07, 0.06), 0.003);
        if (!own) {
            b.style(L.shirt, mk::MATTE);
            b.cone(V3(-0.004, 0.42, 0), V3(-0.01, 0.475, 0), 0.07, 0.064, detail >= 2 ? 12 : 8, false);
            for (int s = -1; s <= 1; s += 2) b.beam(V3(0.035, 0.468, s * 0.02), V3(0.062, 0.44, s * 0.044), 0.022, 0.005, V3(1, 1, 0), 0.002);
        }
    }
}

// ---- The head ----

// The head's shape: a direction from its middle (x the face's way, y up, z across) to the surface. A skull, the jaw
// narrowing to the chin, the brow, eye sockets and cheekbones.
V3 headShape(const V3& d) {
    double x = d.x * 0.099, y = d.y * 0.117, z = d.z * 0.078;
    const double low = smooth01((-d.y - 0.05) / 0.75);
    z *= 1 - 0.2 * low;
    x *= d.x < 0 ? 1 - 0.3 * low : 1 - 0.05 * low;
    if (d.x < 0 && d.y > -0.2) x *= 1.04;  // the back of the skull
    auto g = [](double v, double w) { return std::exp(-(v * v) / (w * w)); };
    if (d.x > 0) {
        const double az = std::fabs(d.z);
        x += 0.006 * g(d.y - 0.28, 0.12) * g(az, 0.5) * d.x;              // brow
        x -= 0.01 * g(d.y - 0.1, 0.1) * g(az - 0.36, 0.16) * d.x;          // eye sockets
        z *= 1 + 0.06 * g(d.y + 0.1, 0.15) * g(d.x - 0.6, 0.3);          // cheekbones
        x += 0.012 * g(d.y + 0.85, 0.15) * g(az, 0.3);                    // chin
        y -= 0.008 * g(d.y + 0.85, 0.2) * g(az, 0.4);
    }
    return V3(x, y, z);
}
// A point over the head's shape in the direction d, `out` beyond it, the top `lift` taller, hair `hang`ing down behind.
V3 overHead(const V3& d, double out, double lift, double hang) {
    V3 q = headShape(d) + d * out;
    if (q.y > 0) q.y *= 1 + lift;
    if (hang > 0) {
        q.y -= hang * smooth01((-d.y - 0.1) / 0.6) * smooth01(-d.x * 2 + 0.4);
        if (d.x < 0.2) q += V3(d.x, 0, d.z) * (0.25 * hang * smooth01(-d.y + 0.3));
    }
    return q;
}
// A band over the head between two heights that can change round it (lo(c), hi(c): the direction's height, -1..1, for the
// way round, c = cos of the angle off the face), `segs` round (from angle a0 to a1; all the way round: closed) and `rows`
// up; hi of 1 closes it over the crown.
template <class Lo, class Hi>
void headBand(mk::B& b, int segs, int rows, double out, double lift, double hang, Lo lo, Hi hi, const V3& col, double a0 = -M_PI, double a1 = M_PI) {
    Rows r;
    r.closed = a1 - a0 > 2 * M_PI - 1e-6;
    r.n = r.closed ? segs : segs + 1;
    for (int i = 0; i <= rows; i++) {
        const double t = (double)i / rows;
        double ym = 0;
        for (int k = 0; k < r.n; k++) {
            const double a = a0 + (a1 - a0) * k / segs, c = std::cos(a);
            const double y = clampd(lo(c) + (hi(c) - lo(c)) * t, -0.999, 0.999), h = std::sqrt(1 - y * y);
            const V3 q = overHead(V3(h * c, y, h * std::sin(a)), out, lift, hang);
            r.pts.push_back(q);
            ym += q.y / r.n;
        }
        r.mid.push_back(V3(0, ym, 0));
        r.col.push_back(col);
    }
    skin(b, r);
}
// The hairline (the height of the direction it starts at): high at the forehead, round over the ears, low at the nape.
double hairline(double c, double nape, double overEars) {
    return c > 0 ? overEars + (0.42 - overEars) * std::pow(c, 1.5) : overEars + (nape - overEars) * smooth01(-c * 1.6);
}

// The head: its middle at the origin, the face toward +x.
void buildHead(mk::B& b, const Look& L, int detail) {
    const int segs = detail >= 2 ? 18 : detail == 1 ? 10 : 7, rows = detail >= 2 ? 12 : detail == 1 ? 6 : 4, hr = detail >= 2 ? 5 : 2;
    const V3 skin = L.skin, shade = skin * 0.86, hairC = L.hair;
    auto k_ = [](double v) { return [v](double) { return v; }; };
    // The head itself, closed under the jaw and over the crown.
    headBand(b, segs, rows, 0, 0, 0, k_(-1), k_(1), skin);
    // Hair.
    const V3 hairCol = L.hairStyle == 1 ? lerpV(hairC, skin, 0.35) : hairC;
    if (L.hairStyle == 0) {
        headBand(b, segs, hr, 0.007, 0.05, 0, [](double c) { return hairline(c, -0.55, 0.06); }, k_(1), hairCol);
        if (detail >= 1) {  // sideburns
            b.style(hairCol, mk::MATTE);
            for (int s = -1; s <= 1; s += 2) b.box(V3(0.03, -0.008, s * 0.074), V3(0.02, 0.04, 0.008), 0.003);
        }
    } else if (L.hairStyle == 1)
        headBand(b, segs, hr, 0.003, 0, 0, [](double c) { return hairline(c, -0.5, 0.1); }, k_(1), hairCol);
    else if (L.hairStyle == 2)  // long, down over the ears to the collar
        headBand(b, segs, hr, 0.012, 0.06, 0.06, [](double c) { return c > 0.55 ? 0.42 : hairline(c, -1.0, -0.35); }, k_(1), hairCol);
    else  // bald, a fringe round the back
        headBand(b, segs, 2, 0.004, 0, 0, [](double c) { return -0.42 + 0.3 * smooth01(c + 0.4); }, [](double c) { return 0.18 - 0.1 * smooth01(c + 0.4); }, hairCol, 1.0, 2 * M_PI - 1.0);
    // Beard round the jaw, under the mouth (a moustache over it), or stubble.
    if (L.beard >= 1) {
        const bool full = L.beard == 2;
        headBand(b, segs, hr, full ? 0.009 : 0.0015, 0, 0, k_(-0.97), [full](double c) { return full ? -0.62 + 0.5 * smooth01((1 - c) / 0.6) : -0.66 + 0.45 * smooth01((1 - c) / 0.6); },
                 full ? hairC * 0.95 : lerpV(skin, skin * 0.6 + hairC * 0.15, 0.7), -1.75, 1.75);
        if (full) {
            b.style(hairC * 0.9, mk::MATTE);
            b.box(V3(0.094, -0.043, 0), V3(0.012, 0.011, 0.05), 0.004);
        }
    }
    if (detail >= 1) {
        // Ears: rim and hollow.
        for (int k = -1; k <= 1; k += 2) {
            b.style(skin * 0.96, mk::MATTE);
            b.sphere(V3(-0.012, -0.004, k * 0.079), V3(0.022, 0.033, 0.011), 0);
            b.style(shade * 0.8, mk::MATTE);
            b.sphere(V3(-0.008, -0.006, k * 0.086), V3(0.012, 0.02, 0.005), 0);
        }
        // Nose: bridge, tip and wings.
        b.style(skin * 0.98, mk::MATTE);
        b.beam(V3(0.093, 0.024, 0), V3(0.116, -0.026, 0), 0.016, 0.014, V3(1, 0.45, 0), 0.005);
        b.sphere(V3(0.114, -0.03, 0), V3(0.014, 0.013, 0.015), detail >= 2 ? 1 : 0);
        for (int k = -1; k <= 1; k += 2) b.sphere(V3(0.102, -0.035, k * 0.0125), V3(0.011, 0.009, 0.009), 0);
        // Eyes under the brow: whites, irises, lids; brows.
        for (int k = -1; k <= 1; k += 2) {
            b.style(V3(0.66, 0.64, 0.6), mk::MATTE);
            b.sphere(V3(0.077, 0.01, k * 0.032), V3(0.012, 0.0075, 0.0105), 0);
            b.style(L.eyes, mk::MATTE);
            b.sphere(V3(0.0875, 0.0105, k * 0.031), V3(0.003, 0.006, 0.006), 0);
            b.style(shade, mk::MATTE);
            b.box(V3(0.087, 0.019, k * 0.032), V3(0.01, 0.005, 0.028), 0.002);  // upper lid
            b.style(hairC * 0.85, mk::MATTE);
            b.push();
            b.move(0.097, 0.033, k * 0.034);
            b.roll(k * -8.0);
            b.box(V3(0, 0, 0), V3(0.01, 0.008, 0.036), 0.003);
            b.pop();
        }
        // Lips.
        if (L.beard != 2 || detail >= 2) {
            b.style(L.lips, mk::MATTE);
            b.box(V3(0.09, -0.053, 0), V3(0.012, 0.0075, 0.04), 0.0035);
            b.style(L.lips * 1.08, mk::MATTE);
            b.box(V3(0.087, -0.062, 0), V3(0.012, 0.009, 0.034), 0.004);
        }
    }
    // Hats.
    const double r = 0.117;  // (the head's half-height: a height over it as the direction's)
    if (L.hat_ == 0) {  // a ball cap: crown, the peak forward and curved, the button
        headBand(b, segs, hr, 0.016, 0.07, 0, [r](double c) { return (0.034 + 0.016 * c) / r; }, k_(1), L.hat);
        b.style(L.hat * 0.92, mk::MATTE);
        b.push();
        b.move(0.08, 0.05, 0);
        b.pitch(-10);
        b.extrudeUp({mk::V2(0.0, 0.085), mk::V2(0.05, 0.078), mk::V2(0.088, 0.048), mk::V2(0.102, 0.0), mk::V2(0.088, -0.048), mk::V2(0.05, -0.078), mk::V2(0.0, -0.085)}, 0.007, -0.0035);
        b.pop();
        if (detail >= 1) {
            b.style(L.hat * 0.7, mk::MATTE);
            b.sphere(V3(-0.004, 0.142, 0), V3(0.012, 0.007, 0.012), 0);
            b.box(V3(0.106, 0.08, 0), V3(0.004, 0.028, 0.044), 0.002);  // patch
        }
    } else if (L.hat_ == 1) {  // a beanie with a turned-up cuff
        headBand(b, segs, hr, 0.018, 0.12, 0, k_(0.0), k_(1), L.hat);
        headBand(b, segs, 1, 0.025, 0.12, 0, k_(-0.06), k_(0.36), L.hat * 0.82);
    } else if (L.hat_ == 3) {  // a bush hat: crown, band and a wide soft brim
        headBand(b, segs, hr, 0.018, 0.12, 0, k_(0.26), k_(1), L.hat);
        headBand(b, segs, 1, 0.022, 0.12, 0, k_(0.26), k_(0.44), L.hat * 0.6);
        b.style(L.hat * 0.95, mk::MATTE);
        b.push();
        b.scale(V3(1, 1, 0.86));
        b.lathe(V3(0, 0.036, 0), {mk::V2(0.098, 0.004), mk::V2(0.17, -0.016), mk::V2(0.185, -0.022), mk::V2(0.183, -0.014), mk::V2(0.168, -0.008), mk::V2(0.098, 0.012)},
                detail >= 2 ? 22 : 12, true);
        b.pop();
    } else if (L.hat_ == 4) {  // a hard hat: shell, a ridge front to back, the short peak
        headBand(b, segs, hr, 0.026, 0.14, 0, [r](double c) { return (0.03 + 0.01 * c) / r; }, k_(1), L.hat);
        b.style(L.hat * 0.95, mk::PAINT);
        b.beam(V3(-0.1, 0.13, 0), V3(0.1, 0.13, 0), 0.03, 0.03, V3(0, 1, 0), 0.01);
        b.push();
        b.move(0.1, 0.04, 0);
        b.pitch(-8);
        b.extrudeUp({mk::V2(-0.01, 0.07), mk::V2(0.03, 0.06), mk::V2(0.045, 0.0), mk::V2(0.03, -0.06), mk::V2(-0.01, -0.07)}, 0.008, -0.004);
        b.pop();
    }
}

std::mutex cacheMu;
std::unordered_map<uint64_t, mk::Mesh>& cache() {
    static std::unordered_map<uint64_t, mk::Mesh> c;
    return c;
}
enum Piece { P_HAND = 0, P_BOOT = 1, P_SEAT = 2, P_CHEST = 3, P_HEAD = 4, P_WHEEL = 5, P_CARGO = 6 };
// A work pants' cargo pocket on the thigh's outside (the thigh's frame: x ahead, y up the thigh, z right; side 0 left).
void buildCargo(mk::B& b, const Look& L, int side) {
    const double s = side == 0 ? -1.0 : 1.0, r = 0.081 * L.girth;
    b.style(L.pants * 0.9, mk::MATTE);
    b.box(V3(0.005, 0.0, s * (r + 0.004)), V3(0.085, 0.11, 0.012), 0.004);
    b.style(L.pantsDark, mk::MATTE);
    b.box(V3(0.005, 0.05, s * (r + 0.011)), V3(0.09, 0.028, 0.008), 0.003);
}
const mk::Mesh& piece(int what, const Look& L, int detail, int param = 0) {
    const uint64_t key = (L.key * 1315423911ull) ^ ((uint64_t)what << 56) ^ ((uint64_t)detail << 52) ^ ((uint64_t)(param & 0xffff) << 32);
    std::lock_guard<std::mutex> lk(cacheMu);
    auto it = cache().find(key);
    if (it != cache().end()) return it->second;
    mk::Mesh& m = cache()[key];
    mk::B b(m);
    switch (what) {
    case P_HAND: buildHand(b, L, param & 1, (param >> 1) / 4.0, detail); break;
    case P_BOOT: buildBoot(b, L, detail); break;
    case P_SEAT: buildSeat(b, L, detail); break;
    case P_CHEST: buildChest(b, L, detail, param == 1); break;
    case P_HEAD: buildHead(b, L, detail); break;
    case P_CARGO: buildCargo(b, L, param); break;
    default: break;
    }
    return m;
}

// Two bones from `a` toward `t` (lengths l1, l2), bending toward `pole`: the middle joint.
// The end reaches t if it can, else stops short along the line.
V3 solve(const V3& a, const V3& t, double l1, double l2, const V3& pole, V3* end = nullptr) {
    V3 d = t - a;
    double L = len(d);
    const V3 n = L > 1e-6 ? d / L : V3(0, -1, 0);
    L = clampd(L, std::fabs(l1 - l2) + 1e-3, (l1 + l2) * 0.9995);
    const double x = (l1 * l1 - l2 * l2 + L * L) / (2 * L), h = std::sqrt(std::max(0.0, l1 * l1 - x * x));
    if (end) *end = a + n * L;
    return a + n * x + perpTo(pole, n) * h;
}

Basis chestBasis(const Pose& p) {
    Basis c;
    c.y = norm(p.chestU);
    c.x = perpTo(p.chestF, c.y);
    c.z = cross(c.x, c.y);
    c.o = p.pelvis + norm(p.pelvisU) * kWaist;
    return c;
}
Basis pelvisBasis(const Pose& p) {
    Basis c;
    c.y = norm(p.pelvisU);
    c.x = perpTo(p.pelvisF, c.y);
    c.z = cross(c.x, c.y);
    c.o = p.pelvis;
    return c;
}
// The head: pitched on the top of the neck (looking down, it comes forward over the chest), its middle at the origin.
Basis headBasis(const Pose& p) {
    const Basis ch = chestBasis(p);
    Basis hd;
    hd.x = norm(p.headF);
    const V3 side = cross(hd.x, kUp);
    hd.z = len(side) > 1e-3 ? norm(side) : ch.z;
    hd.y = cross(hd.z, hd.x);
    // The neck bends forward for about half of a look down (the head nods the rest), so the eyes come forward of the
    // chest and down: looking at your feet, they're past the front of your jacket.
    const double nod = std::max(-0.1, -std::asin(clampd(hd.x.y, -1, 1)) * 0.55);  // (looking up, the head tips back on the neck)
    const V3 fh = perpTo(hd.x, kUp);
    const V3 neckDir = kUp * std::cos(nod) + fh * std::sin(nod);
    const V3 neckTop = ch.o + ch.y * kNeckY + neckDir * 0.088 + ch.x * 0.012;
    hd.o = neckTop + hd.y * 0.075 - hd.x * 0.01;
    return hd;
}

}  // namespace

V3 shoulderOf(const Pose& p, int side) {
    const Basis c = chestBasis(p);
    return c.o + c.y * (kShoulderY + 0.006 * p.breath) + c.z * (side == 0 ? -kShoulderW : kShoulderW);
}
V3 eyesOf(const Pose& p) {
    const Basis hd = headBasis(p);
    return hd.o + hd.x * 0.09 + hd.y * 0.01;
}
V3 hipOf(const Pose& p, int side) {
    const Basis c = pelvisBasis(p);
    return c.o + c.z * (side == 0 ? -kHipW : kHipW);
}

void legOf(const Pose& p, int side, V3& hip, V3& knee, V3& ankle) {
    hip = hipOf(p, side);
    knee = solve(hip, p.ankle[side], kThigh, kShin, p.kneePole[side], &ankle);
}

Look lookFor(const V3& jacket, uint32_t seed) {
    Look L;
    auto h = [&](int i) { return mk::hash01(seed, (uint32_t)i); };
    auto pick = [&](int i, int n) { return std::min(n - 1, (int)(h(i) * n)); };
    L.jacket = jacket;
    L.jacketDark = jacket * 0.62;
    L.top = h(10) < 0.45 ? 0 : h(10) < 0.75 ? 1 : 2;
    L.legs = h(11) < 0.55 ? 0 : 1;
    if (L.legs == 0) {  // jeans: indigo to faded, or black
        static const V3 jeans[] = {V3(0.16, 0.21, 0.34), V3(0.12, 0.14, 0.21), V3(0.27, 0.33, 0.45), V3(0.1, 0.1, 0.11)};
        L.pants = jeans[pick(1, 4)];
        L.pantsWorn = L.pants * 1.22 + V3(0.035, 0.035, 0.04);  // (faded at the knees)
    } else {  // work pants: duck canvas, khaki, olive, grey
        static const V3 work[] = {V3(0.44, 0.32, 0.19), V3(0.5, 0.44, 0.31), V3(0.29, 0.31, 0.2), V3(0.3, 0.3, 0.3)};
        L.pants = work[pick(1, 4)];
        L.pantsWorn = L.pants * 1.1;
    }
    L.pantsDark = L.pants * 0.7;
    static const V3 boots[] = {V3(0.33, 0.21, 0.12), V3(0.1, 0.09, 0.085), V3(0.42, 0.33, 0.2), V3(0.24, 0.17, 0.11)};
    L.boots = boots[pick(2, 4)];
    L.soles = V3(0.09, 0.085, 0.08);
    L.laces = V3(0.55, 0.45, 0.3);
    L.gloved = h(12) < 0.7;
    static const V3 gloves[] = {V3(0.6, 0.47, 0.3), V3(0.12, 0.12, 0.13), V3(0.45, 0.42, 0.38), V3(0.62, 0.55, 0.2)};
    const int gi = pick(3, 4);
    L.gloves = gloves[gi];
    L.glovePad = gi == 1 ? jacket * 0.9 : L.gloves * 0.62;
    static const V3 skin[] = {V3(0.88, 0.7, 0.58), V3(0.82, 0.62, 0.47), V3(0.72, 0.52, 0.37), V3(0.6, 0.42, 0.29), V3(0.45, 0.3, 0.21), V3(0.33, 0.22, 0.16)};
    L.skin = skin[pick(4, 6)];
    static const V3 hair[] = {V3(0.07, 0.06, 0.055), V3(0.22, 0.14, 0.09), V3(0.62, 0.48, 0.24), V3(0.5, 0.22, 0.1), V3(0.5, 0.5, 0.49), V3(0.32, 0.24, 0.16)};
    L.hair = hair[pick(5, 6)];
    L.hairStyle = h(14) < 0.45 ? 0 : h(14) < 0.65 ? 1 : h(14) < 0.85 ? 2 : 3;
    L.hat_ = h(6) < 0.33 ? 0 : h(6) < 0.53 ? 1 : h(6) < 0.73 ? 2 : h(6) < 0.88 ? 3 : 4;
    if (L.hat_ == 4) {  // hard hats: safety colours
        static const V3 hard[] = {V3(0.86, 0.68, 0.12), V3(0.84, 0.84, 0.8), V3(0.86, 0.42, 0.1)};
        L.hat = hard[pick(7, 3)];
    } else {
        static const V3 hats[] = {V3(0.12, 0.13, 0.14), V3(0.55, 0.12, 0.08), V3(0.2, 0.3, 0.2), V3(0.75, 0.6, 0.35), V3(0.42, 0.36, 0.26), V3(0.18, 0.24, 0.36)};
        L.hat = hats[pick(7, 6)];
    }
    static const V3 shirts[] = {V3(0.55, 0.15, 0.12), V3(0.2, 0.3, 0.45), V3(0.25, 0.35, 0.22), V3(0.45, 0.42, 0.38), V3(0.6, 0.5, 0.36)};
    L.shirt = shirts[pick(15, 5)];
    static const V3 eyes[] = {V3(0.25, 0.16, 0.08), V3(0.2, 0.3, 0.42), V3(0.28, 0.32, 0.18), V3(0.12, 0.08, 0.05)};
    L.eyes = eyes[pick(8, 4)];
    L.lips = V3(L.skin.x * 0.8, L.skin.y * 0.62, L.skin.z * 0.6);
    L.beard = h(9) < 0.3 ? 1 : h(9) < 0.48 ? 2 : 0;
    L.stubble = L.beard == 1;
    L.girth = 0.94 + 0.14 * h(13);
    uint64_t k = seed * 0x9e3779b97f4a7c15ull + 2;  // (+ the model's version: a change of look rebuilds the cached pieces)
    for (double c : {jacket.x, jacket.y, jacket.z}) k = k * 1000003ull + (uint64_t)(c * 255);
    L.key = k;
    return L;
}

Look lookFor(uint32_t seed) {
    // Muted work colours (one hi-vis now and then).
    static const V3 jackets[] = {V3(0.62, 0.3, 0.14), V3(0.33, 0.36, 0.2),  V3(0.15, 0.2, 0.32), V3(0.17, 0.3, 0.2),  V3(0.66, 0.5, 0.16), V3(0.2, 0.2, 0.22),
                                 V3(0.58, 0.16, 0.12), V3(0.62, 0.5, 0.32), V3(0.14, 0.38, 0.4), V3(0.2, 0.42, 0.62), V3(0.8, 0.72, 0.12)};
    return lookFor(jackets[std::min(10, (int)(mk::hash01(seed, 99) * 11))], seed);
}

void gripAt(Pose& p, int side, const V3& g, const V3& axis, double grip) {
    const V3 A = norm(axis), sh = shoulderOf(p, side);
    // The fingers go round the handle the way the forearm comes in to it.
    const V3 x = perpTo(g - sh + kUp * -0.15, A);
    const V3 z = side == 1 ? A * -1.0 : A;  // (the right thumb's on the hand's -z side)
    const V3 y = cross(z, x);
    p.handX[side] = x, p.handY[side] = y;
    p.hand[side] = g - x * kGripX + y * kGripY;
    p.grip[side] = grip;
}

const double kClipR = 0.15;  // first person: nothing of you nearer the eyes than this

namespace {
// The torso: the trousers' seat (closed under the crotch), a flannel shirt's hem under a vest, and the top from its hem
// up to the neck. Rows below the waist follow the hips, above it the chest, one between blending the two (so it bends).
void torso(mk::B& b, const Pose& p, const Look& L, const Basis& pel, const Basis& ch, int det, int n) {
    const double G = L.girth;
    {
        Rows r;
        r.n = n;
        for (int i = 0; i < 5; i++) {
            if (det == 0 && (i == 1 || i == 3)) continue;
            const double* s = kSeat[i];
            torsoRow(r, pel, s[0], s[1] * G, s[2] * G, s[3] * G, L.pants * (0.9 + 0.025 * i));
        }
        const V3 tip = pel.o + pel.y * -0.142;
        skin(b, r, &tip, nullptr, L.pantsDark);
    }
    if (L.top == 2) {  // the shirt's hem, untucked under the vest
        Rows r;
        r.n = n;
        torsoRow(r, pel, -0.005, 0.171 * G, 0.104 * G, 0.113 * G, L.shirt * 0.82);
        torsoRow(r, pel, 0.025, 0.169 * G, 0.1 * G, 0.108 * G, L.shirt);
        torsoRow(r, pel, 0.09, 0.165 * G, 0.096 * G, 0.1 * G, L.shirt);
        skin(b, r);
    }
    Rows r;
    r.n = n;
    const double yh = hemY(L);
    const V3 body = topColor(L), hem = L.top == 1 ? body * 0.8 : L.jacketDark;
    torsoRow(r, pel, yh, 0.174 * G, 0.108 * G, 0.11 * G, hem);
    torsoRow(r, pel, yh + 0.035, 0.171 * G, 0.105 * G, 0.105 * G, body * 0.88);
    if (yh + 0.035 < 0.07 && det > 0) torsoRow(r, pel, 0.08, 0.168 * G, 0.104 * G, 0.1 * G, body * 0.9);
    {  // the waist: halfway between the hips' frame and the chest's
        Rows a, c;
        a.n = c.n = n;
        torsoRow(a, pel, kWaist + 0.03, 0.166 * G, 0.106 * G, 0.096 * G, V3());
        torsoRow(c, ch, 0.03, 0.166 * G, 0.106 * G, 0.096 * G, V3());
        for (int k = 0; k < n; k++) r.pts.push_back((a.pts[k] + c.pts[k]) * 0.5);
        r.mid.push_back((a.mid[0] + c.mid[0]) * 0.5);
        r.col.push_back(body * 0.92);
    }
    for (int i = 1; i < kUpperN; i++) {
        if (det == 0 && i != 2 && i != 4 && i != 6) continue;
        if (!p.head && i == kUpperN - 1) continue;  // (your own: closed lower, clear of your eyes)
        const double* u = kUpper[i];
        double shade = 0.92 + 0.12 * smooth01(u[0] / 0.36);
        if (L.top == 2 && (i & 1)) shade *= 0.9;  // (quilted)
        torsoRow(r, ch, u[0], u[1] * G, u[2] * G, u[3] * G, body * shade);
    }
    // In third person the neck goes in at the top; your own, closed (the collar's opening, seen looking down).
    const V3 top = ch.o + ch.y * 0.44;
    skin(b, r, nullptr, p.head ? nullptr : &top, L.shirt * 0.55);
}
}  // namespace

void build(const Pose& p, const Look& L, mk::Mesh& out) {
    const size_t start = out.v.size();
    const int det = p.detail;
    const int nT = det >= 2 ? 16 : det == 1 ? 10 : 7, nL = det >= 2 ? 12 : det == 1 ? 8 : 5;  // round the torso, round a limb
    const double G = L.girth;
    mk::B b(out);
    b.mat(mk::MATTE);
    const Basis pel = pelvisBasis(p), ch = chestBasis(p);
    if (!p.noTorso) {
        torso(b, p, L, pel, ch, det, nT);
        place(out, piece(P_SEAT, L, det), pel);
        place(out, piece(P_CHEST, L, det, p.head ? 0 : 1), ch);
    }
    // Legs: full at the seat, the thigh tapering to a rounded knee (worn paler on jeans, patched on work pants), the calf,
    // and the hem breaking over the boot.
    for (int s = 0; s < 2; s++) {
        const V3 hip = hipOf(p, s);
        V3 ankle;
        const V3 knee = solve(hip, p.ankle[s], kThigh, kShin, p.kneePole[s], &ankle);
        static const double rs[][2] = {{-0.15, 0.094}, {0.3, 0.088}, {0.62, 0.077}, {0.84, 0.066}, {1.0, 0.064}, {1.16, 0.062}, {1.38, 0.062}, {1.72, 0.054}, {1.86, 0.058}, {2.06, 0.066}};
        static const double fs[][2] = {{-0.15, 0.09}, {0.3, 0.086}, {0.62, 0.075}, {0.84, 0.066}, {1.0, 0.069}, {1.16, 0.064}, {1.38, 0.066}, {1.72, 0.056}, {1.86, 0.06}, {2.06, 0.068}};
        auto prof = [&](double u, double& r, double& rf, V3& col) {
            r = tab(rs, 10, u) * G, rf = tab(fs, 10, u) * G;
            const bool knee_ = u > 0.9 && u < 1.12 && det >= 1;
            col = knee_ ? (L.legs == 0 ? L.pantsWorn : L.pants * 0.82) : u > 1.8 ? L.pantsDark : L.pants * (1.0 - 0.06 * clampd(u - 1, 0, 1));
        };
        limbSweep(b, hip, knee, ankle, -0.15, 2.06, det, nL, pel.x, prof);
        if (L.legs == 1 && det >= 1) {  // a cargo pocket on the outside of the thigh
            Basis f;
            const V3 td = norm(knee - hip);
            f.x = perpTo(p.kneePole[s], td);
            f.y = td * -1.0;
            f.z = cross(f.x, f.y);
            f.o = hip + (knee - hip) * 0.5;
            place(out, piece(P_CARGO, L, det, s), f);
        }
        Basis f;
        f.x = norm(p.footF[s]);
        f.y = perpTo(p.footU[s], f.x);
        f.z = cross(f.x, f.y);
        f.o = ankle;
        place(out, piece(P_BOOT, L, det), f);
    }
    // Arms: the shoulder rounded, the sleeve slimming to the elbow and round it, full in the forearm, the cuff at the
    // wrist; then the hand.
    const V3 sleeve = sleeveColor(L), cuff = L.top == 1 ? sleeve * 0.8 : L.top == 2 ? L.shirt * 0.85 : L.jacketDark;
    for (int s = 0; s < 2; s++) {
        const V3 sh = shoulderOf(p, s);
        double l1 = kUpperArm;
        const double reach = len(p.hand[s] - sh);
        if (p.stretch && reach > (kUpperArm + kForearm) * 0.97) l1 = reach - kForearm * 0.93;
        V3 wrist;
        const V3 elbow = solve(sh, p.hand[s], l1, kForearm, p.elbowPole[s], &wrist);
        b.color(sleeve);
        b.sphere(sh + ch.z * (s == 0 ? 0.004 : -0.004) - ch.y * 0.014, V3(0.06, 0.052, 0.06) * G, det >= 1 ? 1 : 0);  // the shoulder
        static const double rs[][2] = {{0.0, 0.056}, {0.35, 0.056}, {0.7, 0.051}, {1.0, 0.049}, {1.35, 0.05}, {1.75, 0.043}, {1.88, 0.046}, {2.03, 0.047}};
        static const double fs[][2] = {{0.0, 0.054}, {0.35, 0.054}, {0.7, 0.05}, {1.0, 0.05}, {1.35, 0.046}, {1.75, 0.041}, {1.88, 0.044}, {2.03, 0.045}};
        auto prof = [&](double u, double& r, double& rf, V3& col) {
            r = tab(rs, 8, u) * G, rf = tab(fs, 8, u) * G;
            col = u >= 1.85 ? cuff : (u > 0.85 && u < 1.15 && det >= 2) ? sleeve * 0.9 : sleeve;
        };
        const V3 tip = wrist + norm(wrist - elbow) * 0.004;
        limbSweep(b, sh, elbow, wrist, 0.02, 2.03, det, nL - (det >= 2 ? 2 : 0), ch.x, prof, &tip, L.shirt * 0.4);
        Basis h;
        h.x = norm(p.handX[s]);
        h.y = perpTo(p.handY[s], h.x);
        h.z = cross(h.x, h.y);
        h.o = wrist;
        const int g = (int)std::lround(clampd(p.grip[s], 0, 1) * 4);
        place(out, piece(P_HAND, L, det, s | (g << 1)), h);
    }
    // Neck and head (not your own, in first person).
    if (p.head) {
        const Basis hd = headBasis(p);
        const V3 nb = ch.o + ch.y * 0.4, nt = hd.o - hd.y * 0.066 - hd.x * 0.016;
        Rows r;
        r.n = det >= 2 ? 10 : det == 1 ? 7 : 5;
        const V3 ax = norm(nt - nb), u = perpTo(ch.x, ax), v = cross(ax, u);
        const double rad[3] = {0.056, 0.05, 0.047};
        for (int i = 0; i < 3; i++) {
            const V3 c = nb + (nt - nb) * (i * 0.5);
            for (int k = 0; k < r.n; k++) {
                const double a = 2 * M_PI * k / r.n;
                r.pts.push_back(c + u * (rad[i] * 1.05 * std::cos(a)) + v * (rad[i] * std::sin(a)));
            }
            r.mid.push_back(c);
            r.col.push_back(L.skin * 0.93);
        }
        skin(b, r);
        place(out, piece(P_HEAD, L, det), hd);
    }
    // First person: nothing within a hand's width of the eyes (pushed out round them, never through the camera).
    if (p.clip)
        for (size_t i = start; i < out.v.size(); i++) {
            mk::MVert& q = out.v[i];
            const V3 d = V3(q.p[0], q.p[1], q.p[2]) - p.eye;
            const double r = len(d);
            if (r < kClipR && r > 1e-6) {
                const V3 w = p.eye + d * (kClipR / r);
                q.p[0] = (float)w.x, q.p[1] = (float)w.y, q.p[2] = (float)w.z;
            }
        }
    lastTris += (int)((out.v.size() - start) / 3);
}

// ---- On foot ----

namespace {
// A foot rolled `pitch` (+ toes up) about its heel (toes up) or the ball of the foot (heel up): the ankle and the
// foot's forward, from where the flat foot's ankle would be.
void rollFoot(const V3& flatAnkle, const V3& F, double pitch, V3& ankle, V3& fwd) {
    const V3 R = cross(F, kUp);
    const V3 pivot = flatAnkle - kUp * kAnkle + F * (pitch > 0 ? -0.055 : 0.125);
    ankle = pivot + rotateAxis(flatAnkle - pivot, R, pitch);
    fwd = rotateAxis(F, R, pitch);
}
V3 yawDir(double y) { return V3(std::cos(y), 0, -std::sin(y)); }
double yawOf(const V3& d) { return std::atan2(-d.z, d.x); }
}  // namespace

Pose walking(const Gait& g, Mover& m, double dt) {
    dt = clampd(dt, 0, 0.1);
    Pose p;
    if (!m.init) {
        m.init = true, m.lastStride = g.stride, m.phase = g.stride * 1.45, m.lastP = g.p, m.lookNext = 1 + mk::hash01(m.seed, 3) * 2;
    }
    m.t += dt;
    // How fast: given, or (a buddy) from where they've got to.
    if (g.estimate) {
        const V3 d = g.p - m.lastP;
        if (dt > 1e-4 && len(d) < 3) m.vel += (d / dt - m.vel) * std::min(1.0, dt * 6);
    } else
        m.vel = g.vel;
    m.lastP = g.p;
    const V3 vh(m.vel.x, 0, m.vel.z);
    const double sNow = len(vh), prev = m.speed;
    m.speed += (sNow - m.speed) * std::min(1.0, dt * 8);
    if (dt > 1e-4) m.accel += ((m.speed - prev) / dt - m.accel) * std::min(1.0, dt * 5);
    double dStride = g.stride - m.lastStride;
    m.lastStride = g.stride;
    if (std::fabs(dStride) > 2.0) dStride = 0;  // (a jump in the count: someone new, or it restarted)
    const double run = clampd((m.speed - 2.4) / 1.6, 0, 1);
    m.phase += dStride * (1.45 + (0.85 - 1.45) * run);  // a walk's cycle is 1.35 m (two steps), a run's 2.3 m
    // In the air, and landing.
    if (!g.grounded) m.air += dt;
    else {
        if (m.air > 0.25) m.land = 1;
        m.air = 0;
    }
    m.land = std::max(0.0, m.land - dt * 2.6);
    const double inAir = smooth01((m.air - 0.08) / 0.2);  // (not for a moment off a bump or a step down: a jump)
    // Walking the cycle, or standing on planted feet (blended as you start and stop).
    const bool wantMove = g.grounded && m.speed > (m.move > 0.5 ? 0.16 : 0.32);
    const double movePrev = m.move;
    m.move = clampd(m.move + (wantMove ? 1 : -1) * dt * 4.5, 0, 1);
    const double mv = smooth01(m.move);

    const V3 F = yawDir(g.yaw), R = cross(F, kUp);
    const double amp = smooth01(m.speed / 1.1);
    const V3 dir = sNow > 0.05 ? vh / sNow : F;
    const double beta = 0.6 + (0.36 - 0.6) * run;  // share of the cycle a foot's on the ground
    const double D = 2 * M_PI / (3.2 * (1.45 + (0.85 - 1.45) * run));
    const double crs = clampd(g.crouch, 0, 1);
    const double travel = D * beta * amp * (1 - 0.35 * crs), back = 0.05 + 0.06 * run;  // (crouched: shorter steps)
    const double u0 = m.phase / (2 * M_PI);
    V3 cyAnkle[2], cyFwd[2];
    double lift[2];
    for (int sd = 0; sd < 2; sd++) {
        const double sgn = sd == 0 ? -1.0 : 1.0;
        double u = u0 + (sd == 0 ? 0.0 : 0.5);
        u -= std::floor(u);
        double x, pitch, h = 0;
        if (u < beta) {  // stance: heel strike, roll onto the flat foot, the heel coming up and off the toes
            const double w = u / beta;
            x = travel * (0.5 - w) - back * amp;
            const double strike = (0.28 - 0.2 * run) * amp;
            pitch = w < 0.14 ? strike * (1 - smooth01(w / 0.14)) : w > 0.55 ? -0.75 * smooth01((w - 0.55) / 0.45) * amp : 0.0;
        } else {  // swing: toes off, the knee coming through, the foot reaching out onto its heel
            const double w = (u - beta) / (1 - beta);
            x = -travel * 0.5 + travel * smooth01(w) - back * amp;
            const double peak = 0.32 - 0.08 * run;
            const double hgt = (0.09 + 0.26 * run) * amp * (1 - 0.4 * crs);
            h = hgt * (w < peak ? std::sin(w / peak * M_PI / 2) : std::cos((w - peak) / (1 - peak) * M_PI / 2));
            x -= 0.12 * run * amp * std::sin(w * M_PI) * (1 - w);  // (running, the heel kicks up behind first)
            pitch = (-0.75 + (0.28 - 0.2 * run + 0.75) * smooth01(w)) * amp;
        }
        const V3 flat = g.p + dir * x + R * (sgn * (0.1 - 0.035 * run)) + kUp * (kAnkle + h);
        const V3 fF = norm(F + R * (sgn * 0.09));
        rollFoot(flat, fF, pitch, cyAnkle[sd], cyFwd[sd]);
        lift[sd] = h;
    }
    // Planted feet when standing: put down where the cycle left them as you stop, then a step at a time back in under
    // you, and round as you turn on the spot.
    V3 stAnkle[2], stFwd[2];
    if (m.move < 1) {
        if (!m.planted || (movePrev >= 1 && m.move < 1)) {
            m.planted = true, m.stepping = -1;
            for (int sd = 0; sd < 2; sd++) {
                m.foot[sd] = V3(cyAnkle[sd].x, g.p.y + kAnkle, cyAnkle[sd].z);
                m.footYaw[sd] = yawOf(V3(cyFwd[sd].x, 0, cyFwd[sd].z));
            }
        }
        auto home = [&](int sd) { return g.p + R * ((sd == 0 ? -1.0 : 1.0) * 0.105) + F * (sd == 0 ? 0.02 : -0.02) + kUp * kAnkle; };
        auto homeYaw = [&](int sd) { return g.yaw + (sd == 0 ? 0.1 : -0.1); };
        for (int sd = 0; sd < 2; sd++) m.foot[sd].y = g.p.y + kAnkle;
        if (m.stepping < 0 && !wantMove && g.grounded) {
            int worst = -1;
            double most = 0.085;
            for (int sd = 0; sd < 2; sd++) {
                const V3 e = home(sd) - m.foot[sd];
                const double err = len(V3(e.x, 0, e.z)) + 0.16 * std::fabs(wrapAngle(homeYaw(sd) - m.footYaw[sd]));
                if (err > most) most = err, worst = sd;
            }
            if (worst >= 0) m.stepping = worst, m.stepT = 0, m.stepFrom = m.foot[worst], m.stepYawFrom = m.footYaw[worst];
        }
        for (int sd = 0; sd < 2; sd++) {
            stAnkle[sd] = m.foot[sd];
            stFwd[sd] = yawDir(m.footYaw[sd]);
        }
        if (m.stepping >= 0) {
            const int sd = m.stepping;
            m.stepT += dt / 0.3;
            const double t = smooth01(m.stepT);
            V3 to = home(sd);
            m.foot[sd] = m.stepFrom + (to - m.stepFrom) * t;
            m.foot[sd].y = g.p.y + kAnkle;
            m.footYaw[sd] = m.stepYawFrom + wrapAngle(homeYaw(sd) - m.stepYawFrom) * t;
            stAnkle[sd] = m.foot[sd] + kUp * (0.07 * std::sin(M_PI * std::min(1.0, m.stepT)));
            stFwd[sd] = rotateAxis(yawDir(m.footYaw[sd]), cross(yawDir(m.footYaw[sd]), kUp), -0.25 * std::sin(M_PI * std::min(1.0, m.stepT)));
            lift[sd] = std::max(lift[sd] * mv, stAnkle[sd].y - g.p.y - kAnkle);
            if (m.stepT >= 1) m.stepping = -1;
        }
    } else
        m.planted = false;
    // Hips: bob (walking lowest at the heel strikes, running mid-stance), over the standing leg, hip dropping on the
    // swing side; standing, a slow shift of weight from foot to foot and breathing.
    const double ph = 2 * M_PI * u0;
    const double bob = (-0.024 * std::cos(2 * ph) * (1 - run) - 0.04 * std::cos(2 * (ph - M_PI * beta / 2)) * run) * amp * mv;
    const double idle = 1 - mv;
    const double shiftIdle = 0.016 * std::sin(m.t * 0.45 + m.seed) * idle;
    const double sway = 0.028 * std::sin(ph) * amp * (1 - run) * mv + shiftIdle;
    const double twist = 0.11 * std::sin(ph) * amp * mv;
    const double breath = std::sin(m.t * 2 * M_PI / (4.2 - 1.6 * run));
    const double cr = clampd(g.crouch, 0, 1);
    double hipY = 0.935 - 0.035 * run * mv - kCrouchDrop * cr + bob - 0.13 * smooth01(m.land) - 0.004 * idle * (1 + breath);
    V3 ankle[2], fwd[2];
    for (int sd = 0; sd < 2; sd++) {
        ankle[sd] = m.planted ? stAnkle[sd] + (cyAnkle[sd] - stAnkle[sd]) * mv : cyAnkle[sd];
        fwd[sd] = m.planted ? norm(stFwd[sd] + (cyFwd[sd] - stFwd[sd]) * mv) : cyFwd[sd];
    }
    const V3 pelH = g.p + R * sway - F * (0.1 * cr);  // (crouched: the seat goes back)
    for (int sd = 0; sd < 2; sd++) {  // no higher than the planted legs reach
        if (lift[sd] > 0.015) continue;
        const V3 hip = pelH + R * ((sd == 0 ? -1.0 : 1.0) * kHipW);
        const double dx = len(V3(ankle[sd].x - hip.x, 0, ankle[sd].z - hip.z));
        const double L = (kThigh + kShin) * 0.995;
        hipY = std::min(hipY, ankle[sd].y - g.p.y + std::sqrt(std::max(0.0, L * L - dx * dx)));
    }
    // In the air: knees up, feet tucked under, arms out for balance.
    if (inAir > 0) {
        hipY += (0.95 - hipY) * inAir;
        for (int sd = 0; sd < 2; sd++) {
            const double sgn = sd == 0 ? -1.0 : 1.0;
            const V3 tuck = g.p + kUp * (0.95 - 0.7) + F * (sd == 0 ? 0.14 : -0.04) + R * (sgn * 0.11);
            ankle[sd] = ankle[sd] + (tuck - ankle[sd]) * inAir;
            fwd[sd] = norm(fwd[sd] + (rotateAxis(F, R, -0.35) - fwd[sd]) * inAir);
        }
    }
    p.pelvis = pelH + kUp * hipY;
    p.pelvisF = rotateAxis(F, kUp, twist);
    p.pelvisU = norm(kUp + R * (sway * 2.2) * (1 - run));
    const double lean = (0.05 + 0.24 * run) * amp * mv + clampd(m.accel * 0.05, -0.08, 0.12) * mv + 0.3 * smooth01(m.land) + kCrouchLean * cr - 0.012 * breath * idle;
    p.chestU = norm(kUp * std::cos(lean) + F * std::sin(lean) - R * (sway * 2.6));
    p.chestF = rotateAxis(F, kUp, -twist * 0.75);
    p.breath = breath;
    // The head: level with the eyes ahead (running, it stays up as the body leans); a buddy standing about glances round.
    if (g.lookAround && m.t > m.lookNext) {
        m.lookTo = (mk::hash01(m.seed, (uint32_t)(m.t * 7)) * 2 - 1) * (0.75 - 0.55 * mv);
        m.lookNext = m.t + 1.6 + 3.2 * mk::hash01(m.seed + 1, (uint32_t)(m.t * 5));
    }
    m.look += ((g.lookAround ? m.lookTo : 0.0) - m.look) * std::min(1.0, dt * 3.5);
    const V3 lf = rotateAxis(F, kUp, m.look);
    const double pitch = clampd(g.lookPitch, -kHeadDown, kHeadUp);  // (the head turns so far; the eyes the rest)
    p.headF = norm(lf * std::cos(pitch) + kUp * std::sin(pitch));
    for (int sd = 0; sd < 2; sd++) {
        const double sgn = sd == 0 ? -1.0 : 1.0;
        p.ankle[sd] = ankle[sd];
        p.footF[sd] = fwd[sd];
        p.footU[sd] = perpTo(kUp, fwd[sd]);
        p.kneePole[sd] = norm(F + R * (sgn * 0.12) + kUp * (0.3 * inAir));
    }
    // Arms swing against the legs walking, pump bent running, hang easy standing, go out for balance in the air.
    for (int sd = 0; sd < 2; sd++) {
        const double sgn = sd == 0 ? -1.0 : 1.0;
        const V3 sh = shoulderOf(p, sd);
        const V3 rel = ankle[sd] - g.p;
        const double footX = dot(rel, dir) * mv;
        const double swing = -footX * (0.38 + 0.3 * run);
        const V3 hang = sh + kUp * -0.56 + F * (0.035 + swing) + R * (sgn * 0.055);
        const V3 pump = sh + kUp * -0.27 + F * (0.13 + swing * 0.9) + R * (sgn * -0.035);
        const V3 rest = sh + kUp * (-0.565 + 0.006 * breath) + F * 0.035 + R * (sgn * (0.07 + 0.004 * std::sin(m.t * 0.7 + sd)));
        V3 h = rest + ((hang + (pump - hang) * run) - rest) * mv;
        const V3 out = sh + kUp * -0.32 + R * (sgn * 0.3) + F * 0.12;
        h = h + (out - h) * (inAir * 0.8);
        p.hand[sd] = h;
        p.elbowPole[sd] = norm(F * -1.0 + R * (sgn * 0.45) + kUp * -0.2);
        const V3 el = solve(sh, h, kUpperArm, kForearm, p.elbowPole[sd]);
        const V3 x = norm(h - el + F * (0.08 + 0.45 * run * mv));
        p.handX[sd] = x;
        p.handY[sd] = perpTo(R * sgn + F * (-0.25), x);
        p.grip[sd] = 0.3 + 0.55 * run * mv;
    }
    return p;
}

Pose firstPerson(const Gait& g, Mover& m, double dt, double viewYaw, double viewPitch, const V3& eye, const V3& lean) {
    // The body walks (crouched as the walker is) with its head turned the way you look; then it's put where the eyes are:
    // across (looking down, the head comes forward over the chest, so the body's a little behind the eyes), and, if the
    // eyes are lower than this body's (leaning in to work), the hips go down and the chest bends to bring them there.
    // (The crouch is the walker's, given once in g.crouch: the eyes being lower for it are already this body's.)
    Gait gg = g;
    gg.yaw = viewYaw;
    gg.lookPitch = viewPitch;
    Pose p = walking(gg, m, dt);
    p.head = false;
    p.stretch = true;
    p.clip = true, p.eye = eye;
    const V3 F(std::cos(viewYaw), 0, -std::sin(viewYaw)), R = cross(F, kUp);
    const V3 leanH(lean.x, 0, lean.z);  // (leaning in to a job: the chest goes, the feet stay)
    // Across: the whole body (feet too) under the eyes as they'd be without the lean.
    const V3 e0 = eyesOf(p), to = eye - leanH;
    const V3 across(to.x - e0.x, 0, to.z - e0.z);
    p.pelvis += across;
    for (int s = 0; s < 2; s++) p.ankle[s] += across, p.hand[s] += across - kUp * 0.02;
    // Down and in: the hips lower (to a crouch, no further), then the chest bends, so the neck comes to where the eyes want it.
    const double down = std::min(0.0, eye.y - eyesOf(p).y);
    if (down < -0.01 || len(leanH) > 0.01) {
        const V3 sh0[2] = {shoulderOf(p, 0), shoulderOf(p, 1)};
        const double Lc = kNeckY + 0.1;  // the chest's pivot to the top of the neck
        const Basis c0 = chestBasis(p);
        const V3 neckTo = c0.o + c0.y * Lc + leanH + kUp * down;
        p.pelvis += kUp * (down * 0.6) + F * (dot(leanH, F) * 0.35) + R * (dot(leanH, R) * 0.5);
        p.pelvis.y = std::max(p.pelvis.y, g.p.y + kHipFloor);
        p.pelvisU = norm(kUp * 2.5 + norm(neckTo - p.pelvis)), p.pelvisF = perpTo(F, p.pelvisU);
        V3 d = neckTo - (p.pelvis + p.pelvisU * kWaist);
        V3 dirH(d.x, 0, d.z);
        const double reachH = len(dirH);
        dirH = reachH > 1e-3 ? dirH / reachH : F;
        double a = std::asin(clampd(reachH / Lc, 0, 0.94));
        const double high = Lc * std::cos(a) - d.y;  // (the neck'd be this much over where it's wanted: the hips go lower first)
        if (high > 0) {
            const double drop = std::min(high, p.pelvis.y - (g.p.y + kHipFloor));
            p.pelvis.y -= drop, d.y += drop;
        }
        a = std::max(a, std::acos(clampd(d.y / Lc, std::cos(1.2), 1.0)));
        p.chestU = kUp * std::cos(a) + dirH * std::sin(a), p.chestF = perpTo(F, p.chestU);
        // The hips go back (or forward) under it so the neck's over where it's wanted; the feet stay.
        const V3 neck = p.pelvis + p.pelvisU * kWaist + p.chestU * Lc;
        p.pelvis += V3(neckTo.x - neck.x, 0, neckTo.z - neck.z);
        // Low down: the feet set apart, one a little ahead, the knees out over them.
        // (standing still only: walking, the feet go on stepping)
        const double k = smooth01((0.8 - (p.pelvis.y - g.p.y)) / 0.3) * (1 - smooth01(m.move));
        for (int s = 0; s < 2; s++) {
            const double sgn = s == 0 ? -1.0 : 1.0;
            const V3 a = g.p + across + R * (sgn * 0.16) + F * (s == 0 ? 0.12 : -0.05) + kUp * kAnkle;
            p.ankle[s] = p.ankle[s] + (a - p.ankle[s]) * k;
            p.footF[s] = norm(p.footF[s] + (norm(F + R * (sgn * 0.25)) - p.footF[s]) * k), p.footU[s] = perpTo(kUp, p.footF[s]);
            p.kneePole[s] = norm(p.kneePole[s] + (norm(F + R * (sgn * 0.5) + kUp * 0.3) - p.kneePole[s]) * k);
            p.hand[s] += shoulderOf(p, s) - sh0[s];  // (a free hand goes with its shoulder)
        }
        const V3 e = eye - eyesOf(p);  // (what's left: the head's own lean off the neck)
        p.pelvis += V3(e.x, 0, e.z);
        for (int s = 0; s < 2; s++) p.hand[s] += V3(e.x, 0, e.z);
    }
    return p;
}

}  // namespace people
