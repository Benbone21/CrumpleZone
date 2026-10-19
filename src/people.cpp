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
const double kSeatSlide = 0.09;               // a driver would sit this far ahead of the seat's hip point (up to the wheel): the eyes are over that

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
    // Cut in at the waist, out over the chest and the shoulder blades, the shoulders sloping to the collar.
    b.style(L.jacket, mk::MATTE);
    b.push();
    b.scale(V3(0.58, 1, 1));
    b.lathe(V3(0, 0, 0), {mk::V2(0.0, -0.075), mk::V2(0.162, -0.07), mk::V2(0.166, -0.02), mk::V2(0.158, 0.08), mk::V2(0.172, 0.2), mk::V2(0.186, 0.29), mk::V2(0.184, 0.345),
                          mk::V2(0.16, 0.39), mk::V2(0.11, 0.425), mk::V2(0.06, 0.445), mk::V2(0.0, 0.448)},
            sg, true);
    b.style(L.jacketDark, mk::MATTE);
    b.cyl(V3(0, -0.075, 0), V3(0, -0.032, 0), 0.169, sg, false);  // hem band
    b.pop();
    // Chest and back: a little fuller in front over the pecs, the shoulder blades behind.
    b.style(L.jacket, mk::MATTE);
    for (int s = -1; s <= 1; s += 2) {
        b.sphere(V3(0.06, 0.265, s * 0.07), V3(0.05, 0.07, 0.085), detail >= 2 ? 1 : 0);
        b.sphere(V3(-0.062, 0.29, s * 0.075), V3(0.042, 0.085, 0.085), detail >= 2 ? 1 : 0);
    }
    // Collar standing up round the neck (open at the throat), the zip and its pull.
    b.style(L.jacketDark, mk::MATTE);
    b.cone(V3(-0.004, 0.41, 0), V3(-0.012, 0.49, 0), 0.074, 0.066, sg, false);
    b.torus(V3(-0.012, 0.49, 0), 0.066, 0.008, sg, 3);
    b.style(V3(0.13, 0.13, 0.14), mk::METAL);
    b.beam(V3(0.098, -0.07, 0), V3(0.104, 0.08, 0), 0.008, 0.007, V3(1, 0, 0));
    b.beam(V3(0.104, 0.08, 0), V3(0.112, 0.27, 0), 0.008, 0.007, V3(1, 0, 0));
    b.beam(V3(0.112, 0.27, 0), V3(0.07, 0.43, 0), 0.008, 0.007, V3(1, 0, 0));
    b.box(V3(0.117, 0.21, 0), V3(0.006, 0.026, 0.012), 0.002);
    if (detail >= 1) {
        for (int s = -1; s <= 1; s += 2) {
            // Chest pockets with flaps, hand-warmer pocket openings.
            b.style(L.jacketDark, mk::MATTE);
            b.box(V3(0.113, 0.25, s * 0.07), V3(0.014, 0.09, 0.08), 0.006);
            b.style(L.jacket * 0.92, mk::MATTE);
            b.box(V3(0.121, 0.292, s * 0.07), V3(0.012, 0.024, 0.086), 0.004);
            b.style(L.jacketDark * 0.9, mk::MATTE);
            b.beam(V3(0.094, -0.04, s * 0.06), V3(0.09, 0.08, s * 0.095), 0.006, 0.006, V3(1, 0, 0));
            // Seams that read at a distance: down each side, over each shoulder to the collar.
            b.style(L.jacketDark * 0.95, mk::MATTE);
            b.beam(V3(0, -0.06, s * 0.164), V3(0, 0.08, s * 0.157), 0.006, 0.006, V3(1, 0, 0));
            b.beam(V3(0, 0.08, s * 0.157), V3(0, 0.29, s * 0.187), 0.006, 0.006, V3(1, 0, 0));
            b.beam(V3(0, 0.355, s * 0.18), V3(-0.004, 0.43, s * 0.075), 0.006, 0.006, V3(0, 1, 0));
        }
        b.style(L.jacketDark * 0.85, mk::MATTE);  // shoulder yoke across the back
        b.box(V3(-0.1, 0.335, 0), V3(0.014, 0.008, 0.22), 0.003);
    }
}

// The head: its middle at the origin, the face toward +x.
void buildHead(mk::B& b, const Look& L, int detail) {
    const int d = detail >= 2 ? 2 : 1, d0 = detail >= 2 ? 1 : 0;
    const V3 skin = L.skin, shade = skin * 0.86;
    b.style(skin, mk::MATTE);
    b.sphere(V3(-0.006, 0.012, 0), V3(0.096, 0.108, 0.086), d);                 // skull
    b.sphere(V3(0.026, -0.05, 0), V3(0.068, 0.052, 0.068), d0);                // cheeks and upper jaw
    b.style(L.stubble ? skin * 0.72 : skin, mk::MATTE);
    b.sphere(V3(0.03, -0.074, 0), V3(0.06, 0.042, 0.058), d0);                 // jaw
    b.sphere(V3(0.074, -0.092, 0), V3(0.026, 0.024, 0.032), 0);                // chin
    b.style(skin, mk::MATTE);
    b.sphere(V3(0.078, 0.033, 0), V3(0.022, 0.016, 0.07), d0);                 // brow ridge
    for (int k = -1; k <= 1; k += 2) b.sphere(V3(0.06, -0.01, k * 0.044), V3(0.026, 0.02, 0.022), 0);  // cheekbones
    // Nose: bridge, tip and wings.
    b.beam(V3(0.09, 0.022, 0), V3(0.113, -0.024, 0), 0.017, 0.014, V3(1, 0.4, 0), 0.004);
    b.sphere(V3(0.112, -0.028, 0), V3(0.014, 0.013, 0.014), 0);
    for (int k = -1; k <= 1; k += 2) b.sphere(V3(0.101, -0.033, k * 0.012), V3(0.011, 0.009, 0.009), 0);
    // Ears: rim and hollow.
    for (int k = -1; k <= 1; k += 2) {
        b.style(skin, mk::MATTE);
        b.sphere(V3(-0.012, -0.004, k * 0.086), V3(0.024, 0.034, 0.011), 0);
        b.style(shade * 0.85, mk::MATTE);
        b.sphere(V3(-0.008, -0.006, k * 0.092), V3(0.012, 0.02, 0.005), 0);
    }
    // Eyes under the brow: whites, irises, lids; brows.
    for (int k = -1; k <= 1; k += 2) {
        b.style(V3(0.62, 0.6, 0.57), mk::MATTE);
        b.sphere(V3(0.073, 0.009, k * 0.033), V3(0.011, 0.0068, 0.0095), 0);
        b.style(L.eyes, mk::MATTE);
        b.sphere(V3(0.082, 0.009, k * 0.033), V3(0.005, 0.0068, 0.0068), 0);
        b.style(shade, mk::MATTE);
        b.box(V3(0.081, 0.018, k * 0.033), V3(0.01, 0.005, 0.03), 0.002);  // upper lid
        b.style(L.hair * 0.85, mk::MATTE);
        b.push();
        b.move(0.094, 0.034, k * 0.034);
        b.roll(k * -8.0);
        b.box(V3(0, 0, 0), V3(0.01, 0.008, 0.038), 0.002);
        b.pop();
    }
    // Lips.
    b.style(L.lips, mk::MATTE);
    b.box(V3(0.094, -0.054, 0), V3(0.012, 0.007, 0.04), 0.003);
    b.style(L.lips * 1.08, mk::MATTE);
    b.box(V3(0.091, -0.063, 0), V3(0.012, 0.008, 0.034), 0.003);
    // Hair (short at the back and sides, sideburns) and hat.
    b.style(L.hair, mk::MATTE);
    b.blob(V3(-0.016, 0.024, 0), V3(0.103, 0.11, 0.094), 0.03, 3, d0 + 1, 0.3, -0.05);
    for (int k = -1; k <= 1; k += 2) b.box(V3(0.03, -0.012, k * 0.083), V3(0.022, 0.04, 0.008), 0.003);
    if (L.hat_ == 0) {  // ball cap, peak forward, curved
        b.style(L.hat, mk::MATTE);
        b.blob(V3(0.0, 0.05, 0), V3(0.105, 0.08, 0.097), 0.0, 4, 1, 0.3, 0.035);
        b.push();
        b.scale(V3(1, 1, 0.92));
        b.cyl(V3(0.0, 0.03, 0), V3(0.0, 0.058, 0), 0.104, 12, false);
        b.pop();
        b.push();
        b.move(0.085, 0.038, 0);
        b.pitch(-12);
        b.extrudeUp({mk::V2(0.0, 0.09), mk::V2(0.055, 0.08), mk::V2(0.095, 0.042), mk::V2(0.108, 0.0), mk::V2(0.095, -0.042), mk::V2(0.055, -0.08), mk::V2(0.0, -0.09)}, 0.007, -0.0035);
        b.pop();
        b.style(L.hat * 0.7, mk::MATTE);
        b.sphere(V3(0.0, 0.128, 0), V3(0.012, 0.007, 0.012), 0);  // button
        b.box(V3(0.098, 0.075, 0), V3(0.004, 0.026, 0.04), 0.002);  // patch
    } else if (L.hat_ == 1) {  // beanie, turned-up band
        b.style(L.hat, mk::MATTE);
        b.blob(V3(-0.005, 0.032, 0), V3(0.107, 0.11, 0.098), 0.0, 4, 1, 0.3, 0.02);
        b.style(L.hat * 0.8, mk::MATTE);
        b.push();
        b.scale(V3(1, 1, 0.91));
        b.cyl(V3(-0.005, 0.03, 0), V3(-0.005, 0.07, 0), 0.109, 12, false);
        b.pop();
    }
}

// Colour by height: darker toward a hem, a jacket's shadowed sides (soft, as dirt and shade gather there).
void shadeByHeight(mk::Mesh& m, size_t from, double y0, double y1, double k0, double k1) {
    for (size_t i = from; i < m.v.size(); i++) {
        mk::MVert& q = m.v[i];
        const double t = clampd((q.p[1] - y0) / (y1 - y0), 0, 1), k = k0 + (k1 - k0) * t * t * (3 - 2 * t);
        // (...and a touch darker where it faces down or away round the sides)
        const double side = 0.94 + 0.06 * clampd(0.5 + 0.5 * q.n[1] + 0.3 * std::fabs(q.n[0]), 0, 1);
        for (int c = 0; c < 3; c++) q.c[c] = (float)(q.c[c] * k * side);
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
    case P_SEAT: buildSeat(b, L, detail), shadeByHeight(m, 0, -0.1, 0.15, 0.88, 1.0); break;
    case P_CHEST: buildChest(b, L, detail), shadeByHeight(m, 0, -0.07, 0.36, 0.84, 1.04); break;
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
    return c.o + c.y * (kShoulderY + 0.006 * p.breath) + c.z * (side == 0 ? -kShoulderW : kShoulderW);
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
    L.pantsWorn = L.pants * 1.18 + V3(0.03, 0.03, 0.03);  // (jeans faded at the knees and down the thighs)
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
    static const V3 eyes[] = {V3(0.25, 0.16, 0.08), V3(0.2, 0.3, 0.42), V3(0.28, 0.32, 0.18), V3(0.12, 0.08, 0.05)};
    L.eyes = eyes[(int)(h(8) * 4) % 4];
    L.lips = V3(L.skin.x * 0.8, L.skin.y * 0.62, L.skin.z * 0.6);
    L.stubble = h(9) < 0.35;
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

namespace {
// A limb's tube along a polyline of (point, radius) stations.
void limb(mk::B& b, std::initializer_list<std::pair<V3, double>> st, int sg) {
    const std::pair<V3, double>* prev = nullptr;
    for (const auto& q : st) {
        if (prev) b.cone(prev->first, q.first, prev->second, q.second, sg, false);
        prev = &q;
    }
}
// A thin band round a limb at `c` (axis `ax`): a fold or a seam that reads at a distance.
void band(mk::B& b, const V3& c, const V3& ax, double r, double w, int sg) { b.cone(c - ax * (w * 0.5), c + ax * (w * 0.5), r, r, sg, false); }
}  // namespace

const double kClipR = 0.15;  // first person: nothing of you nearer the eyes than this

void build(const Pose& p, const Look& L, mk::Mesh& out) {
    const size_t start = out.v.size();
    const int det = p.detail, sg = det >= 2 ? 10 : det == 1 ? 7 : 5;
    mk::B b(out);
    const Basis pel = pelvisBasis(p), ch = chestBasis(p);
    place(out, piece(P_SEAT, L, det), pel);
    place(out, piece(P_CHEST, L, det), ch);
    // Legs: the thigh tapering to the knee, the calf swelling below it and slimming to the ankle; the jeans faded
    // at the knee, an outside seam, folds stacked over the boot.
    for (int s = 0; s < 2; s++) {
        const V3 hip = hipOf(p, s), out_ = pel.z * (s == 0 ? -1.0 : 1.0);
        V3 ankle;
        const V3 knee = solve(hip, p.ankle[s], kThigh, kShin, p.kneePole[s], &ankle);
        const V3 td = norm(knee - hip), sd = norm(ankle - knee);
        b.style(L.pants, mk::MATTE);
        limb(b, {{hip - td * 0.02, 0.083}, {hip + (knee - hip) * 0.45, 0.071}, {knee - td * 0.03, 0.054}}, sg);
        b.style(L.pantsWorn, mk::MATTE);
        b.sphere(knee, V3(0.054, 0.054, 0.054), det >= 2 ? 1 : 0);
        b.style(L.pants, mk::MATTE);
        limb(b, {{knee + sd * 0.01, 0.052}, {knee + (ankle - knee) * 0.3, 0.054}, {ankle + sd * 0.0, 0.046}}, sg);
        if (det >= 1) {
            b.style(L.pantsDark, mk::MATTE);
            const V3 so = perpTo(out_, td), ss = perpTo(out_, sd);
            b.beam(hip + so * 0.08, knee + so * 0.052, 0.006, 0.005, so);  // outside seams
            b.beam(knee + ss * 0.051, ankle + ss * 0.045, 0.006, 0.005, ss);
            band(b, knee - sd * 0.035, sd, 0.053, 0.008, sg);  // a fold behind the knee
            band(b, ankle - sd * 0.06, sd, 0.049, 0.01, sg);   // folds stacking over the boot
        }
        b.style(L.pantsDark, mk::MATTE);
        b.cone(ankle - sd * 0.035, ankle + sd * 0.045, 0.048, 0.058, sg, true);  // the hem, breaking over the boot
        Basis f;
        f.x = norm(p.footF[s]);
        f.y = perpTo(p.footU[s], f.x);
        f.z = cross(f.x, f.y);
        f.o = ankle;
        place(out, piece(P_BOOT, L, det), f);
    }
    // Arms: a rounded shoulder, the upper arm slimming to a defined elbow, the forearm full below it and slim at the
    // wrist; the sleeve creased at the elbow and bunched over the cuff, then the glove.
    for (int s = 0; s < 2; s++) {
        const V3 sh = shoulderOf(p, s);
        double l1 = kUpperArm;
        const double reach = len(p.hand[s] - sh);
        if (p.stretch && reach > (kUpperArm + kForearm) * 0.97) l1 = reach - kForearm * 0.93;
        V3 wrist;
        const V3 elbow = solve(sh, p.hand[s], l1, kForearm, p.elbowPole[s], &wrist);
        const V3 ud = norm(elbow - sh), fd = norm(wrist - elbow);
        b.style(L.jacket, mk::MATTE);
        b.sphere(sh - ch.y * 0.006 + ch.z * (s == 0 ? -0.004 : 0.004), V3(0.046, 0.044, 0.047), det >= 2 ? 1 : 0);  // the deltoid
        limb(b, {{sh, 0.047}, {sh + (elbow - sh) * 0.45, 0.043}, {elbow, 0.036}}, sg);
        b.sphere(elbow, V3(0.036, 0.036, 0.036), 0);
        limb(b, {{elbow, 0.036}, {elbow + (wrist - elbow) * 0.28, 0.038}, {wrist - fd * 0.05, 0.032}}, sg);
        b.style(L.jacketDark, mk::MATTE);
        if (det >= 1) {
            band(b, elbow + ud * -0.03, ud, 0.041, 0.007, sg);   // creases at the inside of the elbow
            band(b, elbow + fd * 0.035, fd, 0.039, 0.007, sg);
            band(b, wrist - fd * 0.075, fd, 0.035, 0.008, sg);   // bunched over the cuff
        }
        b.cone(wrist - fd * 0.055, wrist + fd * 0.004, 0.034, 0.033, sg, true);  // cuff
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
        hd.z = cross(hd.x, hd.y);
        hd.y = cross(hd.z, hd.x);
        hd.o = neck + ch.y * 0.115 + hd.y * 0.06 + ch.x * 0.012;
        b.style(L.skin, mk::MATTE);
        const V3 nb = neck - ch.y * 0.03, nt = hd.o - hd.y * 0.065 - hd.x * 0.018;
        limb(b, {{nb, 0.047}, {(nb + nt) * 0.5, 0.041}, {nt, 0.043}}, sg);
        b.style(L.skin * 0.9, mk::MATTE);
        b.sphere(nb + (nt - nb) * 0.45 + hd.x * 0.03, V3(0.012, 0.014, 0.012), 0);  // Adam's apple
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
    const double inAir = smooth01(m.air / 0.15);
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
    const double travel = D * beta * amp, back = 0.05 + 0.06 * run;
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
            const double hgt = (0.09 + 0.26 * run) * amp;
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
    double hipY = 0.935 - 0.035 * run * mv + bob - 0.13 * smooth01(m.land) - 0.004 * idle * (1 + breath);
    V3 ankle[2], fwd[2];
    for (int sd = 0; sd < 2; sd++) {
        ankle[sd] = m.planted ? stAnkle[sd] + (cyAnkle[sd] - stAnkle[sd]) * mv : cyAnkle[sd];
        fwd[sd] = m.planted ? norm(stFwd[sd] + (cyFwd[sd] - stFwd[sd]) * mv) : cyFwd[sd];
    }
    const V3 pelH = g.p + R * sway;
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
    const double lean = (0.05 + 0.24 * run) * amp * mv + clampd(m.accel * 0.05, -0.08, 0.12) * mv + 0.3 * smooth01(m.land) - 0.012 * breath * idle;
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
    const double pitch = g.lookPitch - lean * 0.6;
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
    Gait gg = g;
    gg.yaw = viewYaw;
    Pose p = walking(gg, m, dt);
    p.head = false;
    p.stretch = true;
    p.clip = true, p.eye = eye;
    // Set back as you look down (and a little anyway: the eyes are over the front of the neck); hands a touch
    // lower and in, so swinging they stay down in the corners.
    const V3 F(std::cos(viewYaw), 0, -std::sin(viewYaw)), R = cross(F, kUp);
    const double down = std::max(0.0, -std::sin(viewPitch));
    const double drop = -std::min(0.0, lean.y);
    const double leaning = clampd(len(lean) / 0.15, 0, 1);  // (leaning in, the chest's under the eyes: no setting back)
    const V3 off = F * -(0.06 + 0.1 * down) * (1 - leaning);
    p.pelvis += off;
    for (int s = 0; s < 2; s++) p.hand[s] += off - kUp * 0.02, p.ankle[s] += off;
    // Crouched and leaning in: the chest goes with the eyes, the hips less (knees bend, feet stay).
    if (drop > 0.01 || len(lean) > 0.01) {
        const V3 sh0[2] = {shoulderOf(p, 0), shoulderOf(p, 1)};
        const V3 hip0 = p.pelvis;
        p.pelvis += kUp * (-drop * 0.95) + F * (dot(lean, F) * 0.35) + R * (dot(lean, R) * 0.5);
        p.pelvis.y = std::max(p.pelvis.y, g.p.y + 0.42);
        const V3 sh = hip0 + kUp * (kWaist + kShoulderY) + lean;  // where the shoulders' middle goes
        V3 up = sh - p.pelvis;
        up = len(up) > 1e-6 ? norm(up) : kUp;
        p.chestU = up, p.chestF = perpTo(F, up);
        p.pelvisU = norm(kUp + up), p.pelvisF = perpTo(F, p.pelvisU);
        const double k = clampd(drop / 0.3, 0, 1);
        for (int s = 0; s < 2; s++) {  // feet set apart, one a little ahead, knees out over them
            const double sgn = s == 0 ? -1.0 : 1.0;
            const V3 a = g.p + off + R * (sgn * 0.16) + F * (s == 0 ? 0.12 : -0.05) + kUp * kAnkle;
            p.ankle[s] = p.ankle[s] + (a - p.ankle[s]) * k;
            p.footF[s] = norm(p.footF[s] + (norm(F + R * (sgn * 0.25)) - p.footF[s]) * k), p.footU[s] = perpTo(kUp, p.footF[s]);
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
    s.wheelR = t.cockpit.wheelR, s.tilt = t.cockpit.tilt;
    return std::isfinite(s.hip.x + s.wheel.x);
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
