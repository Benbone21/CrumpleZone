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
const double kGripX = 0.072, kGripY = 0.03;   // a handle in the closed hand, from the wrist
const double kSeatSlide = 0.09;               // the driver sits this far ahead of the seat's hip point (sat up to the wheel)

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

// Pieces are built in their own frame: x ahead (the toes, the face, the fingers), y up (the
// back of the hand), z to the right.

// A gloved hand: wrist at the origin, fingers along +x, the back of the hand +y. side 0 left (thumb
// toward +z), 1 right (thumb toward -z). grip 0..1 curls the fingers round a handle at
// (kGripX, -kGripY) along z. detail 0: a mitten.
void buildHand(mk::B& b, const Look& L, int side, double grip, int detail) {
    const double t = side == 0 ? 1.0 : -1.0;  // the thumb's side
    const int sg = detail >= 2 ? 6 : 4;
    // Cuff (a short gauntlet with a strap) and the palm.
    b.style(L.gloves * 0.8, mk::MATTE);
    b.push();
    b.scale(V3(1, 0.78, 1));
    b.cone(V3(-0.05, 0, 0), V3(0.012, 0, 0), 0.045, 0.041, 8, true);
    b.pop();
    b.style(L.glovePad, mk::MATTE);
    b.box(V3(-0.018, 0.031, 0), V3(0.026, 0.008, 0.07), 0.003);  // the strap's tab
    b.style(L.gloves, mk::MATTE);
    b.box(V3(0.047, 0, 0), V3(0.096, 0.032, 0.084), 0.013);
    b.style(L.glovePad, mk::MATTE);
    b.box(V3(0.06, 0.016, 0), V3(0.07, 0.008, 0.07), 0.004);  // padded back
    b.box(V3(0.093, 0.013, 0), V3(0.016, 0.01, 0.08), 0.004);  // knuckle guard
    b.style(L.gloves, mk::MATTE);
    struct Finger {
        double z, x, l0, l1, l2, r;
    };
    static const Finger fingers[4] = {{0.029, 0.092, 0.043, 0.027, 0.022, 0.0098},
                                      {0.0095, 0.096, 0.047, 0.03, 0.023, 0.0102},
                                      {-0.0105, 0.093, 0.044, 0.028, 0.022, 0.0097},
                                      {-0.029, 0.085, 0.034, 0.022, 0.019, 0.0086}};
    const double c0 = (8 + 62 * grip) * M_PI / 180, c1 = (10 + 88 * grip) * M_PI / 180, c2 = (6 + 48 * grip) * M_PI / 180;
    if (detail == 0) {  // mitten: the four as one block
        const double a1 = c0 + c1 * 0.5;
        const V3 a(0.092, 0, 0), d1(std::cos(a1), -std::sin(a1), 0);
        b.beam(a, a + d1 * 0.075, 0.075, 0.024, V3(-d1.y, d1.x, 0), 0.008);
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
                const double r0 = f.r * (1 - 0.06 * j), r1 = f.r * (1 - 0.06 * (j + 1));
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
            const double r0 = 0.0125 - 0.0012 * j, r1 = 0.0125 - 0.0012 * (j + 1);
            if (detail > 0 || j == 0) b.cone(p, q, r0, r1, sg, false);
            if (detail >= 2 || j == 2) b.sphere(q, V3(r1, r1, r1), 0);
            p = q;
            // Curls in across the palm toward the fingers' side.
            d = norm(d + V3(0.08, -0.25 * grip, -0.35 * t * (0.3 + grip)));
        }
        b.sphere(base, V3(0.017, 0.014, 0.017), 0);
    }
}

// A work boot: ankle joint at the origin, toes +x, sole at y = -kAnkle.
void buildBoot(mk::B& b, const Look& L, int detail) {
    const int sg = detail >= 2 ? 10 : 7;
    b.style(L.boots, mk::MATTE);
    b.push();
    b.scale(V3(1.12, 1, 0.95));
    b.cyl(V3(-0.006, -0.04, 0), V3(-0.006, 0.105, 0), 0.054, sg, true);  // shaft
    b.pop();
    b.box(V3(0.045, -0.052, 0), V3(0.235, 0.07, 0.104), 0.03);  // vamp
    b.sphere(V3(0.13, -0.056, 0), V3(0.062, 0.036, 0.053), detail >= 2 ? 1 : 0);  // round toe
    b.box(V3(-0.045, -0.05, 0), V3(0.085, 0.07, 0.098), 0.025);  // heel counter
    // Tongue and laces up the front.
    b.style(L.boots * 0.82, mk::MATTE);
    b.beam(V3(0.085, -0.03, 0), V3(0.035, 0.1, 0), 0.042, 0.014, V3(1, 0.3, 0), 0.004);
    if (detail >= 1) {
        b.style(L.laces, mk::MATTE);
        for (int k = 0; k < 5; k++) {
            const double s = k / 4.0;
            const V3 c = lerpV(V3(0.092, -0.022, 0), V3(0.046, 0.088, 0), s);
            b.box(c, V3(0.008, 0.006, 0.052 - 0.004 * k));
        }
        b.style(L.boots * 0.6, mk::MATTE);
        b.cyl(V3(-0.064, 0.07, 0), V3(-0.07, 0.11, 0), 0.008, 4, true);  // pull tab
    }
    // Lug sole, thicker under the heel, a welt round the top of it.
    b.style(L.soles, mk::MATTE);
    b.box(V3(0.048, -0.074, 0), V3(0.29, 0.024, 0.114), 0.008);
    b.box(V3(-0.052, -0.081, 0), V3(0.09, 0.012, 0.106), 0.004);
    b.style(L.soles * 1.6, mk::MATTE);
    if (detail >= 2)
        for (int k = 0; k < 4; k++) b.box(V3(0.12 - k * 0.05, -0.0855, 0), V3(0.022, 0.004, 0.1));
}

// The jeans' seat and belt: the hip joints' middle at the origin.
void buildSeat(mk::B& b, const Look& L, int detail) {
    const int sg = detail >= 2 ? 14 : 9;
    b.style(L.pants, mk::MATTE);
    b.push();
    b.scale(V3(0.68, 1, 1));
    b.lathe(V3(0, 0, 0), {mk::V2(0.0, -0.105), mk::V2(0.1, -0.095), mk::V2(0.15, -0.05), mk::V2(0.162, 0.03), mk::V2(0.158, 0.1), mk::V2(0.152, 0.15)}, sg, true);
    b.style(V3(0.2, 0.13, 0.08), mk::MATTE);  // belt
    b.cyl(V3(0, 0.1, 0), V3(0, 0.142, 0), 0.161, sg, false);
    b.pop();
    b.style(V3(0.62, 0.58, 0.5), mk::METAL);
    b.box(V3(0.113, 0.121, 0), V3(0.012, 0.038, 0.048), 0.004);  // buckle
    if (detail >= 1) {
        b.style(L.pantsDark, mk::MATTE);
        for (int s = -1; s <= 1; s += 2) b.box(V3(-0.1, 0.035, s * 0.062), V3(0.01, 0.075, 0.07), 0.006);  // back pockets
        b.box(V3(0.11, 0.035, 0), V3(0.006, 0.09, 0.012));  // fly
    }
}

// The jacket's body: the waist pivot at the origin, the neck at +y.
void buildChest(mk::B& b, const Look& L, int detail) {
    const int sg = detail >= 2 ? 16 : 10;
    b.style(L.jacket, mk::MATTE);
    b.push();
    b.scale(V3(0.6, 1, 1));
    b.lathe(V3(0, 0, 0), {mk::V2(0.0, -0.075), mk::V2(0.166, -0.07), mk::V2(0.172, -0.02), mk::V2(0.169, 0.1), mk::V2(0.18, 0.22), mk::V2(0.192, 0.31), mk::V2(0.19, 0.36),
                          mk::V2(0.162, 0.405), mk::V2(0.095, 0.44), mk::V2(0.0, 0.45)},
            sg, true);
    b.style(L.jacketDark, mk::MATTE);
    b.cyl(V3(0, -0.075, 0), V3(0, -0.03, 0), 0.175, sg, false);  // hem band
    b.pop();
    // Collar standing up round the neck, the zip and its pull.
    b.style(L.jacketDark, mk::MATTE);
    b.cone(V3(0, 0.418, 0), V3(-0.006, 0.495, 0), 0.078, 0.07, sg, false);
    b.torus(V3(-0.006, 0.495, 0), 0.07, 0.009, sg, 3);
    b.style(V3(0.13, 0.13, 0.14), mk::METAL);
    b.beam(V3(0.103, -0.07, 0), V3(0.111, 0.25, 0), 0.008, 0.007, V3(1, 0, 0));
    b.beam(V3(0.111, 0.25, 0), V3(0.077, 0.44, 0), 0.008, 0.007, V3(1, 0, 0));
    b.box(V3(0.116, 0.2, 0), V3(0.006, 0.026, 0.012), 0.002);
    if (detail >= 1) {
        // Chest pockets with flaps, a reflective stripe round the arms' tops (picked up by headlights).
        for (int s = -1; s <= 1; s += 2) {
            b.style(L.jacketDark, mk::MATTE);
            b.box(V3(0.104, 0.24, s * 0.077), V3(0.016, 0.1, 0.086), 0.006);
            b.style(L.jacket * 0.9, mk::MATTE);
            b.box(V3(0.113, 0.288, s * 0.077), V3(0.014, 0.026, 0.092), 0.004);
            b.box(V3(0.08, 0.02, s * 0.108), V3(0.034, 0.12, 0.012), 0.004);  // hand-warmer pocket seam
        }
        b.style(L.jacketDark * 0.85, mk::MATTE);  // shoulder yoke seam across the back
        b.push();
        b.scale(V3(0.6, 1, 1));
        b.cyl(V3(0, 0.33, 0), V3(0, 0.338, 0), 0.191, sg, false);
        b.pop();
    }
}

// The head: its middle at the origin, the face toward +x.
void buildHead(mk::B& b, const Look& L, int detail) {
    const int d = detail >= 2 ? 2 : 1;
    b.style(L.skin, mk::MATTE);
    b.sphere(V3(0.008, 0, 0), V3(0.098, 0.114, 0.088), d);
    b.sphere(V3(0.03, -0.072, 0), V3(0.066, 0.058, 0.064), 1);  // jaw
    for (int k = -1; k <= 1; k += 2) b.sphere(V3(-0.006, -0.006, k * 0.088), V3(0.02, 0.033, 0.013), 0);  // ears
    b.cone(V3(0.09, -0.006, 0), V3(0.127, -0.026, 0), 0.02, 0.008, 6, true);  // nose
    b.style(V3(0.08, 0.06, 0.05), mk::MATTE);
    for (int k = -1; k <= 1; k += 2) {
        b.sphere(V3(0.09, 0.016, k * 0.036), V3(0.011, 0.012, 0.012), 0);           // eyes
        b.style(L.hair * 0.9, mk::MATTE);
        b.box(V3(0.092, 0.041, k * 0.038), V3(0.012, 0.009, 0.042), 0.002);          // brows
        b.style(V3(0.08, 0.06, 0.05), mk::MATTE);
    }
    b.style(V3(L.skin.x * 0.75, L.skin.y * 0.5, L.skin.z * 0.5), mk::MATTE);
    b.box(V3(0.092, -0.052, 0), V3(0.01, 0.007, 0.04), 0.002);  // mouth
    // Hair and hat.
    b.style(L.hair, mk::MATTE);
    b.blob(V3(-0.018, 0.02, 0), V3(0.104, 0.113, 0.097), 0.035, 3, 1, 0.3, -0.055);
    if (L.hat_ == 0) {  // ball cap, peak forward
        b.style(L.hat, mk::MATTE);
        b.blob(V3(0.0, 0.048, 0), V3(0.106, 0.08, 0.097), 0.0, 4, 1, 0.3, 0.035);
        b.push();
        b.scale(V3(1, 1, 0.92));
        b.cyl(V3(0.0, 0.03, 0), V3(0.0, 0.058, 0), 0.105, 12, false);
        b.pop();
        b.push();
        b.move(0.085, 0.037, 0);
        b.pitch(-10);
        b.extrudeUp({mk::V2(0.0, 0.09), mk::V2(0.055, 0.08), mk::V2(0.095, 0.042), mk::V2(0.108, 0.0), mk::V2(0.095, -0.042), mk::V2(0.055, -0.08), mk::V2(0.0, -0.09)}, 0.008, -0.004);
        b.pop();
        b.style(L.hat * 0.7, mk::MATTE);
        b.sphere(V3(0.0, 0.127, 0), V3(0.012, 0.007, 0.012), 0);  // button
    } else if (L.hat_ == 1) {  // beanie, turned-up band
        b.style(L.hat, mk::MATTE);
        b.blob(V3(-0.005, 0.03, 0), V3(0.108, 0.11, 0.098), 0.0, 4, 1, 0.3, 0.02);
        b.style(L.hat * 0.8, mk::MATTE);
        b.push();
        b.scale(V3(1, 1, 0.91));
        b.cyl(V3(-0.005, 0.03, 0), V3(-0.005, 0.07, 0), 0.11, 12, false);
        b.pop();
    }
}

std::mutex cacheMu;
std::unordered_map<uint64_t, mk::Mesh>& cache() {
    static std::unordered_map<uint64_t, mk::Mesh> c;
    return c;
}
enum Piece { P_HAND = 0, P_BOOT = 1, P_SEAT = 2, P_CHEST = 3, P_HEAD = 4, P_WHEEL = 5 };
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
    case P_CHEST: buildChest(b, L, detail); break;
    case P_HEAD: buildHead(b, L, detail); break;
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

}  // namespace

V3 shoulderOf(const Pose& p, int side) {
    const Basis c = chestBasis(p);
    return c.o + c.y * kShoulderY + c.z * (side == 0 ? -kShoulderW : kShoulderW);
}
V3 hipOf(const Pose& p, int side) {
    const Basis c = pelvisBasis(p);
    return c.o + c.z * (side == 0 ? -kHipW : kHipW);
}

Look lookFor(const V3& jacket, uint32_t seed) {
    Look L;
    auto h = [&](int i) { return mk::hash01(seed, (uint32_t)i); };
    L.jacket = jacket;
    L.jacketDark = jacket * 0.62;
    static const V3 pants[] = {V3(0.16, 0.21, 0.33), V3(0.13, 0.15, 0.2), V3(0.32, 0.28, 0.2), V3(0.2, 0.2, 0.19)};
    L.pants = pants[(int)(h(1) * 4) % 4];
    L.pantsDark = L.pants * 0.7;
    static const V3 boots[] = {V3(0.33, 0.21, 0.12), V3(0.1, 0.09, 0.085), V3(0.42, 0.33, 0.2)};
    L.boots = boots[(int)(h(2) * 3) % 3];
    L.soles = V3(0.09, 0.085, 0.08);
    L.laces = V3(0.55, 0.45, 0.3);
    static const V3 gloves[] = {V3(0.6, 0.47, 0.3), V3(0.12, 0.12, 0.13), V3(0.45, 0.42, 0.38)};
    const int gi = (int)(h(3) * 3) % 3;
    L.gloves = gloves[gi];
    L.glovePad = gi == 1 ? jacket * 0.9 : L.gloves * 0.62;
    static const V3 skin[] = {V3(0.86, 0.68, 0.55), V3(0.8, 0.6, 0.45), V3(0.66, 0.46, 0.32), V3(0.42, 0.28, 0.2)};
    L.skin = skin[(int)(h(4) * 4) % 4];
    static const V3 hair[] = {V3(0.07, 0.06, 0.055), V3(0.22, 0.14, 0.09), V3(0.62, 0.48, 0.24), V3(0.5, 0.22, 0.1), V3(0.5, 0.5, 0.49)};
    L.hair = hair[(int)(h(5) * 5) % 5];
    L.hat_ = h(6) < 0.55 ? 0 : h(6) < 0.85 ? 1 : 2;
    static const V3 hats[] = {V3(0.12, 0.13, 0.14), V3(0.55, 0.12, 0.08), V3(0.2, 0.3, 0.2), V3(0.75, 0.6, 0.35)};
    L.hat = hats[(int)(h(7) * 4) % 4];
    uint64_t k = seed;
    for (double c : {jacket.x, jacket.y, jacket.z}) k = k * 1000003ull + (uint64_t)(c * 255);
    L.key = k;
    return L;
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

void build(const Pose& p, const Look& L, mk::Mesh& out) {
    const size_t start = out.v.size();
    const int det = p.detail, sg = det >= 2 ? 10 : det == 1 ? 7 : 5;
    mk::B b(out);
    const Basis pel = pelvisBasis(p), ch = chestBasis(p);
    place(out, piece(P_SEAT, L, det), pel);
    place(out, piece(P_CHEST, L, det), ch);
    // Waist: the jacket's hem over the belt (fills the gap a bend opens).
    b.style(L.jacket, mk::MATTE);
    // Legs: thigh and shin to the ankle, the jeans breaking over the boot.
    for (int s = 0; s < 2; s++) {
        const V3 hip = hipOf(p, s);
        V3 ankle;
        const V3 knee = solve(hip, p.ankle[s], kThigh, kShin, p.kneePole[s], &ankle);
        b.style(L.pants, mk::MATTE);
        b.cone(hip, knee, 0.084, 0.06, sg, false);
        b.sphere(knee, V3(0.06, 0.06, 0.06), det >= 2 ? 1 : 0);
        const V3 sd = norm(ankle - knee);
        b.cone(knee, ankle + sd * 0.035, 0.058, 0.051, sg, false);
        b.style(L.pantsDark, mk::MATTE);
        b.cone(ankle - sd * 0.02, ankle + sd * 0.045, 0.053, 0.062, sg, true);  // the hem, breaking over the boot
        // The boot on the foot's own axes (it may not reach exactly: then it's where the leg ends).
        Basis f;
        f.x = norm(p.footF[s]);
        f.y = perpTo(p.footU[s], f.x);
        f.z = cross(f.x, f.y);
        f.o = ankle;
        place(out, piece(P_BOOT, L, det), f);
    }
    // Arms: shoulder, elbow, wrist; a sleeve with a cuff, then the glove.
    for (int s = 0; s < 2; s++) {
        const V3 sh = shoulderOf(p, s);
        double l1 = kUpperArm;
        const double reach = len(p.hand[s] - sh);
        if (p.stretch && reach > (kUpperArm + kForearm) * 0.97) l1 = reach - kForearm * 0.93;
        V3 wrist;
        const V3 elbow = solve(sh, p.hand[s], l1, kForearm, p.elbowPole[s], &wrist);
        b.style(L.jacket, mk::MATTE);
        b.sphere(sh - ch.y * 0.008, V3(0.055, 0.052, 0.055), det >= 2 ? 1 : 0);  // the shoulder
        b.cone(sh, elbow, 0.054, 0.044, sg, false);
        b.sphere(elbow, V3(0.044, 0.044, 0.044), det >= 2 ? 1 : 0);
        const V3 fd = norm(wrist - elbow);
        b.cone(elbow, wrist - fd * 0.035, 0.044, 0.039, sg, false);
        b.style(L.jacketDark, mk::MATTE);
        b.cone(wrist - fd * 0.042, wrist + fd * 0.004, 0.043, 0.042, sg, true);  // cuff
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
        const V3 neck = ch.o + ch.y * kNeckY;
        Basis hd;
        hd.y = norm(ch.y * 0.6 + kUp * 0.4);
        hd.x = perpTo(p.headF, hd.y);
        // (The head turns and nods on the neck, not past what a neck does.)
        hd.z = cross(hd.x, hd.y);
        hd.y = cross(hd.z, hd.x);
        hd.o = neck + ch.y * 0.115 + hd.y * 0.06 + ch.x * 0.012;
        b.style(L.skin, mk::MATTE);
        b.cone(neck - ch.y * 0.03, hd.o - hd.y * 0.06, 0.05, 0.046, sg, false);
        place(out, piece(P_HEAD, L, det), hd);
    }
    lastTris += (int)((out.v.size() - start) / 3);
}

// ---- On foot ----

double GaitClock::step(double stride, double dt, double speedNow) {
    if (!init) {
        init = true, lastStride = stride, phase = stride * 1.45;
        return phase;
    }
    double d = stride - lastStride;
    lastStride = stride;
    if (std::fabs(d) > 2.0) d = 0;  // (a jump in the count: someone new, or it restarted)
    const double sp = speedNow >= 0 ? speedNow : (dt > 1e-4 ? d / 3.2 / dt : speed);
    speed += (sp - speed) * std::min(1.0, dt * 8);
    // A walk's cycle is 1.35 m (two steps); a run's 2.3 m.
    const double run = clampd((speed - 2.4) / 1.6, 0, 1);
    phase += d * (1.45 + (0.85 - 1.45) * run);
    return phase;
}

Pose walking(const Gait& g) {
    Pose p;
    const V3 F(std::cos(g.yaw), 0, -std::sin(g.yaw)), R = cross(F, kUp);
    const V3 vh(g.vel.x, 0, g.vel.z);
    const double s = len(vh);
    const double amp = g.grounded ? smooth01(s / 1.1) : 0.0;
    const double run = clampd((s - 2.4) / 1.6, 0, 1);
    const V3 m = s > 0.05 ? vh / s : F;
    const double beta = 0.58 + (0.36 - 0.58) * run;          // share of the cycle a foot's on the ground
    const double D = 2 * M_PI / (3.2 * (1.45 + (0.85 - 1.45) * run));  // cycle length (m)
    const double travel = D * beta * amp, back = 0.06 + 0.055 * run;
    double footX[2], lift[2], toe[2], heel[2];
    const double u0 = g.phase / (2 * M_PI);
    for (int sd = 0; sd < 2; sd++) {
        double u = u0 + (sd == 0 ? 0.0 : 0.5);
        u -= std::floor(u);
        heel[sd] = 0;
        if (u < beta) {  // stance: the foot stays put as the body passes over it, the heel coming up at the end
            const double w = u / beta;
            footX[sd] = travel * (0.5 - w) - back * amp;
            lift[sd] = 0;
            heel[sd] = smooth01((w - 0.62) / 0.38) * amp;
            toe[sd] = 0;
        } else {  // swing: up, forward and down onto the heel
            const double w = (u - beta) / (1 - beta);
            footX[sd] = -travel * 0.5 + travel * smooth01(w) - back * amp;
            const double peak = 0.3 + 0.12 * run;
            const double hgt = (0.1 + 0.2 * run) * amp;
            lift[sd] = hgt * (w < peak ? std::sin(w / peak * M_PI / 2) : std::cos((w - peak) / (1 - peak) * M_PI / 2));
            toe[sd] = (w < 0.5 ? -(0.6 - w) * 0.9 : (w - 0.5) * 0.5) * amp;  // toes down leaving, up landing
        }
    }
    // Hips bob (lowest as weight comes on: walking at the heel strikes, running mid-stance), sway over the
    // standing leg, and turn with the stride; the shoulders turn against them.
    const double ph = 2 * M_PI * u0;
    const double bob = (-0.022 * std::cos(2 * ph) * (1 - run) - 0.035 * std::cos(2 * (ph - M_PI * beta / 2)) * run) * amp;
    const double sway = 0.018 * std::sin(ph) * amp * (1 - run);
    const double twist = 0.1 * std::sin(ph) * amp;
    double hipY = 0.935 - 0.03 * run + bob;
    const V3 base = g.p;
    for (int sd = 0; sd < 2; sd++) {  // no higher than the planted legs reach
        if (lift[sd] > 0.01) continue;
        const double dx = footX[sd], ay = kAnkle + heel[sd] * 0.07;
        const double L = (kThigh + kShin) * 0.995;
        hipY = std::min(hipY, ay + std::sqrt(std::max(0.0, L * L - dx * dx)));
    }
    if (!g.grounded) {  // in the air: knees up a little, feet hanging
        for (int sd = 0; sd < 2; sd++) footX[sd] = 0.05 + (sd ? -0.08 : 0.08), lift[sd] = 0.12 + sd * 0.06, toe[sd] = -0.3, heel[sd] = 0;
        hipY = 0.95;
    }
    p.pelvis = base + kUp * hipY + R * sway;
    p.pelvisF = rotateAxis(F, kUp, twist), p.pelvisU = kUp;
    const double lean = (0.05 + 0.16 * run) * amp;
    p.chestU = norm(kUp * std::cos(lean) + F * std::sin(lean) + R * (-sway * 2));
    p.chestF = rotateAxis(F, kUp, -twist * 0.7);
    p.headF = norm(F * std::cos(g.lookPitch) + kUp * std::sin(g.lookPitch));
    for (int sd = 0; sd < 2; sd++) {
        const double sgn = sd == 0 ? -1.0 : 1.0;
        const double lat = (0.105 - 0.035 * run) * sgn;
        p.ankle[sd] = base + m * footX[sd] + R * lat + kUp * (kAnkle + lift[sd] + heel[sd] * 0.07);
        const double pitch = toe[sd] - heel[sd] * 0.45;  // + toes up
        p.footF[sd] = norm(rotateAxis(F, R, pitch) + R * (sgn * 0.08));
        p.footU[sd] = kUp;
        p.kneePole[sd] = F + R * (sgn * 0.12);
    }
    // Arms swing against the legs: hanging and easy walking, bent and pumping running.
    for (int sd = 0; sd < 2; sd++) {
        const double sgn = sd == 0 ? -1.0 : 1.0;
        const V3 sh = shoulderOf(p, sd);
        const double swing = -footX[sd] * (0.42 + 0.25 * run);
        const V3 hang = sh + kUp * -0.56 + F * (0.03 + swing) + R * (sgn * 0.05);
        const V3 pump = sh + kUp * -0.3 + F * (0.12 + swing * 0.85) + R * (sgn * -0.02);
        p.hand[sd] = lerpV(hang, pump, run);
        p.elbowPole[sd] = norm(F * -1.0 + R * (sgn * 0.4) + kUp * -0.2);
        const V3 el = solve(sh, p.hand[sd], kUpperArm, kForearm, p.elbowPole[sd]);
        const V3 x = norm(p.hand[sd] - el + F * (0.1 + 0.5 * run));
        p.handX[sd] = x;
        p.handY[sd] = perpTo(R * sgn + F * (-0.2), x);
        p.grip[sd] = 0.3 + 0.55 * run;
    }
    return p;
}

Pose firstPerson(const Gait& g, double viewYaw, double viewPitch, const V3& lean) {
    Gait gg = g;
    gg.yaw = viewYaw;
    Pose p = walking(gg);
    p.head = false;
    p.stretch = true;
    // Set back as you look down (and a little anyway: the eyes are over the front of the neck).
    const V3 F(std::cos(viewYaw), 0, -std::sin(viewYaw)), R = cross(F, kUp);
    const double down = std::max(0.0, -std::sin(viewPitch));
    const double drop = -std::min(0.0, lean.y), ahead = dot(lean, F);
    const double leaning = clampd(len(lean) / 0.15, 0, 1);  // (leaning in, the chest's under the eyes: no setting back)
    const V3 off = F * -(0.05 + 0.09 * down) * (1 - leaning);
    p.pelvis += off;
    for (int s = 0; s < 2; s++) p.hand[s] += off, p.ankle[s] += off;
    // Crouched and leaning in: the chest goes with the eyes, the hips less (knees bend, feet stay).
    if (drop > 0.01 || len(lean) > 0.01) {
        const V3 sh0[2] = {shoulderOf(p, 0), shoulderOf(p, 1)};
        const V3 hip0 = p.pelvis;
        p.pelvis += kUp * (-drop * 0.95) + F * (ahead * 0.35) + R * (dot(lean, R) * 0.5);
        p.pelvis.y = std::max(p.pelvis.y, g.p.y + 0.42);
        const V3 sh = hip0 + kUp * (kWaist + kShoulderY) + lean;  // where the shoulders' middle goes
        V3 up = sh - p.pelvis;
        up = len(up) > 1e-6 ? norm(up) : kUp;
        p.chestU = up, p.chestF = perpTo(F, up);
        p.pelvisU = norm(kUp + up), p.pelvisF = perpTo(F, p.pelvisU);
        for (int s = 0; s < 2; s++) {  // feet planted a little apart, knees out over them
            const double sgn = s == 0 ? -1.0 : 1.0;
            p.ankle[s] = g.p + off + R * (sgn * 0.16) + F * (s == 0 ? 0.12 : -0.05) + kUp * kAnkle;
            p.footF[s] = norm(F + R * (sgn * 0.25)), p.footU[s] = kUp;
            p.kneePole[s] = norm(F + R * (sgn * 0.5) + kUp * 0.3);
            p.hand[s] += shoulderOf(p, s) - sh0[s];  // (a free hand goes with its shoulder)
        }
    }
    return p;
}

// ---- In the seat ----

bool seatOf(const Car& c, const VisualTemplate& t, Seat& s) {
    if (!t.cockpit.on) return false;
    const Frame f = c.frame();
    s.fwd = f.fwd, s.up = f.up, s.right = f.left * -1.0;
    s.hip = boundPoint(c, t.cockpit.hip);
    s.wheel = boundPoint(c, t.cockpit.wheel);
    s.pedal = boundPoint(c, t.cockpit.pedal);
    s.wheelR = t.cockpit.wheelR, s.tilt = t.cockpit.tilt;
    return std::isfinite(s.hip.x + s.wheel.x + s.pedal.x);
}

V3 seatEye(const Seat& s) { return s.hip + s.up * 0.735 + s.fwd * (kSeatSlide + 0.05); }

double wheelTurnOf(const Car& c) { return clampd(c.steerInput, -1.0, 1.0) * 4.2; }  // (about 1.3 turns to full lock)

namespace {
struct WheelFrame {
    V3 c, axis, u, r;  // middle; toward the driver; 12 o'clock; 3 o'clock (seen from the seat)
};
WheelFrame wheelFrame(const Seat& s) {
    WheelFrame w;
    w.c = s.wheel;
    w.axis = norm(s.fwd * -std::cos(s.tilt) + s.up * std::sin(s.tilt));
    w.u = norm(s.fwd * std::sin(s.tilt) + s.up * std::cos(s.tilt));
    w.r = s.right;
    return w;
}
}  // namespace

Pose seated(const Seat& s, double wheelTurn, double throttle, double brake, const V3& look) {
    Pose p;
    const V3 F = s.fwd, U = s.up, R = s.right;
    p.pelvis = s.hip + F * kSeatSlide;
    const double recline = 0.1;
    p.pelvisF = norm(F * std::cos(recline * 0.5) + U * std::sin(recline * 0.5));
    p.pelvisU = norm(U * std::cos(recline * 0.5) - F * std::sin(recline * 0.5));
    p.chestU = norm(U * std::cos(recline) - F * std::sin(recline));
    p.chestF = F;
    p.headF = len(look) > 1e-6 ? norm(look) : F;
    // Feet: the right on the throttle (or across on the brake), the left back on the rest.
    const bool braking = brake > 0.08 && brake >= throttle;
    const double press = braking ? brake : throttle;
    const V3 pedalR = braking ? s.pedal - R * 0.13 + U * 0.03 : s.pedal;
    const double ang = 0.62 - 0.28 * clampd(press, 0, 1);  // toes up (rad) off the floor
    p.ankle[1] = pedalR + F * (0.03 * press);
    p.ankle[0] = s.pedal - R * 0.27 - F * 0.06 + U * 0.01;
    for (int sd = 0; sd < 2; sd++) {
        const double a = sd == 1 ? ang : 0.55;
        p.footF[sd] = norm(F * std::cos(a) + U * std::sin(a) + R * (sd == 0 ? -0.1 : 0.05));
        p.footU[sd] = perpTo(U, p.footF[sd]);
        p.kneePole[sd] = norm(U + F * 0.6 + R * (sd == 0 ? -0.15 : 0.15));
    }
    // Hands on the rim at a quarter to three, carried round with it (until it turns past them: then it's fed through).
    const WheelFrame w = wheelFrame(s);
    const double turn = clampd(-wheelTurn, -0.85, 0.85);
    for (int sd = 0; sd < 2; sd++) {
        const double th = (sd == 0 ? -1.3 : 1.3) + turn;
        const double rr = s.wheelR;
        const V3 radial = w.u * std::cos(th) + w.r * std::sin(th), tangent = w.u * -std::sin(th) + w.r * std::cos(th);
        const V3 g = w.c + radial * rr;
        gripAt(p, sd, g, sd == 0 ? tangent : tangent * -1.0, 0.85);
        p.elbowPole[sd] = norm(U * -1.0 + R * (sd == 0 ? -0.7 : 0.7) - F * 0.2);
    }
    p.stretch = true;
    return p;
}

void steeringWheel(const Seat& s, double wheelTurn, mk::Mesh& out) {
    const WheelFrame w = wheelFrame(s);
    // Turned: 12 o'clock moves to the left (counter-clockwise from the seat) for a left turn.
    const V3 u = rotateAxis(w.u, w.axis, wheelTurn), r = cross(u, w.axis);
    // (u, r: up and right on the wheel's face as the driver sees it; axis toward the driver.)
    const size_t start = out.v.size();
    mk::B b(out);
    const double R = s.wheelR;
    std::vector<V3> rim;
    for (int k = 0; k < 24; k++) {
        const double a = 2 * M_PI * k / 24;
        rim.push_back(w.c + (u * std::cos(a) + r * std::sin(a)) * R);
    }
    b.style(V3(0.035, 0.035, 0.038), mk::MATTE);
    b.tube(rim, 0.017, 7, true, false);
    // Three spokes dished back to the hub, the hub and its pad.
    const V3 hub = w.c - w.axis * 0.045;
    b.style(V3(0.16, 0.16, 0.17), mk::METAL);
    for (double a : {-M_PI / 2, M_PI / 2, M_PI}) {
        const V3 dir = u * std::cos(a) + r * std::sin(a);
        b.beam(hub + dir * 0.035, w.c + dir * (R - 0.012), 0.034, 0.008, w.axis);
    }
    b.style(V3(0.05, 0.05, 0.055), mk::MATTE);
    b.cone(hub - w.axis * 0.03, hub + w.axis * 0.025, 0.05, 0.046, 10, true);
    b.style(V3(0.55, 0.55, 0.57), mk::METAL);
    b.cone(hub + w.axis * 0.025, hub + w.axis * 0.03, 0.02, 0.018, 8, true);  // the badge
    lastTris += (int)((out.v.size() - start) / 3);
}

}  // namespace people
