// Plants: the "bush" model, one design per plant kind (plants.h: design number == World::Bush::kind). They are
// scattered by the thousand, so each is a handful of big flat-shaded pieces: lumpy leaf clumps, stems, blades,
// flecks of colour. Built about 0.8 m in radius; the maps scale each to its own size.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

inline double R01(uint32_t seed, int i) { return hash01(seed, (uint32_t)(i < 0 ? i + 100000 : i)); }
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

uint32_t posKey(const V3& p) {
    return (uint32_t)(int)std::lround(p.x * 20) * 73856093u ^ (uint32_t)(int)std::lround(p.y * 20) * 19349663u ^ (uint32_t)(int)std::lround(p.z * 20) * 83492791u;  // (unsigned: the hash wraps)
}

// A lumpy clump of leaves, dappled between two colours, lighter on top and dark underneath.
void leafBlob(B& b, const V3& c, const V3& r, uint32_t seed, const V3& colA, const V3& colB, double bump = 0.2, int detail = 0, double cutBelow = -1e9) {
    auto& v = b.out().v;
    const size_t from = v.size();
    b.style(colA, FOLIAGE);
    b.blob(c, r, bump, seed, detail, 0.15, cutBelow);
    for (size_t i = from; i < v.size(); i++) {
        const V3 q(v[i].p[0], v[i].p[1], v[i].p[2]);
        const double dy = (q.y - c.y) / r.y;
        const double sh = 0.66 + 0.5 * clamp01(0.5 + 0.55 * dy);
        const V3 col = mix(colA, colB, R01(seed + 9, (int)(posKey(q) & 0xffff))) * sh;
        v[i].c[0] = (float)col.x, v[i].c[1] = (float)col.y, v[i].c[2] = (float)col.z;
    }
}

// Dapple the vertices from `from` on between two colours, darker low down (about centre y cy, half-height ry).
void dapple(B& b, size_t from, double cy, double ry, const V3& colA, const V3& colB, uint32_t seed, double dark = 0.66) {
    auto& v = b.out().v;
    for (size_t i = from; i < v.size(); i++) {
        const V3 q(v[i].p[0], v[i].p[1], v[i].p[2]);
        const double sh = dark + (1.16 - dark) * clamp01(0.5 + 0.55 * (q.y - cy) / ry);
        const V3 col = mix(colA, colB, R01(seed + 9, (int)(posKey(q) & 0xffff))) * sh;
        v[i].c[0] = (float)col.x, v[i].c[1] = (float)col.y, v[i].c[2] = (float)col.z;
    }
}

// The vertices from `from` on shaded from `lo` at height y0 to `hi` at y1.
void ramp(B& b, size_t from, double y0, double y1, const V3& lo, const V3& hi) {
    auto& v = b.out().v;
    for (size_t i = from; i < v.size(); i++) {
        const V3 col = mix(lo, hi, clamp01((v[i].p[1] - y0) / (y1 - y0)));
        v[i].c[0] = (float)col.x, v[i].c[1] = (float)col.y, v[i].c[2] = (float)col.z;
    }
}

V3 dryMix(const V3& c, bool dry, const V3& to) { return dry ? mix(c, to, 0.8) : c; }

// A flat triangle that faces `toward`'s side (so its light is right from either way round).
void tri(B& b, const V3& a, const V3& c, const V3& d, const V3& toward) {
    if (dot(cross(c - a, d - a), toward) < 0) b.tri(a, d, c);
    else b.tri(a, c, d);
}

// A bent blade: base centre, one bend, tip; `side` the way it is wide; widths at the base and the bend (the tip is a point).
void blade(B& b, const V3& p0, const V3& p1, const V3& p2, const V3& side, double w0, double w1) {
    const V3 out = norm(V3(p2.x - p0.x, 0.0, p2.z - p0.z) + V3(0, 0.6, 0));
    const V3 a = p0 - side * w0, c = p0 + side * w0, d = p1 + side * w1, e = p1 - side * w1;
    tri(b, a, c, d, out), tri(b, a, d, e, out), tri(b, e, d, p2, out);
}

// A small flat diamond flower head lying on a surface with normal n at p.
void fleck(B& b, const V3& p, const V3& n, double s, const V3& col, uint32_t seed) {
    b.color(col);
    const V3 u = norm(cross(n, std::fabs(n.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), w = cross(n, u);
    const double a = R01(seed, 1) * 6.28;
    const V3 e0 = u * std::cos(a) + w * std::sin(a), e1 = cross(n, e0);
    const V3 c = p + n * 0.03;
    b.tri(c + e0 * s, c + e1 * s * 0.8, c - e0 * s * 0.9);
    b.tri(c + e0 * s, c - e0 * s * 0.9, c - e1 * s * 0.8);
}

// A point on the upper part of an ellipsoid and its outward normal.
void onTop(const V3& c, const V3& r, double yaw, double lift, V3& p, V3& n) {
    const double ph = lift * M_PI / 2;
    n = norm(V3(std::cos(yaw) * std::cos(ph) / r.x, std::sin(ph) / r.y, std::sin(yaw) * std::cos(ph) / r.z));
    p = c + V3(std::cos(yaw) * std::cos(ph) * r.x, std::sin(ph) * r.y, std::sin(yaw) * std::cos(ph) * r.z);
}

// ---- 0 Leafy bush ------------------------------------------------------------------------

void bushLeafy(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const double big = p("size");
    const V3 A = dryMix(V3(0.13, 0.26, 0.07), dry, V3(0.40, 0.30, 0.12)), B = dryMix(V3(0.22, 0.38, 0.10), dry, V3(0.55, 0.43, 0.17));
    // A few bare stems rise out of the ground under the leaves.
    b.style(V3(0.27, 0.20, 0.13), WOOD);
    for (int k = 0; k < 2; k++) {
        const double a = k * 3.1 + R01(sd, k + 50) * 1.2, o = 0.16 + 0.12 * R01(sd, k + 60);
        b.cone(V3(std::cos(a) * o * 0.4 * big, -0.04, std::sin(a) * o * 0.4 * big), V3(std::cos(a) * (o + 0.28) * big, 0.28 * big, std::sin(a) * (o + 0.28) * big), 0.035, 0.015, 3, false);
    }
    struct L { double x, z, y, r; };
    const L ls[] = {{0, 0, 0.34, 0.56}, {0.40, 0.16, 0.26, 0.4}, {-0.36, 0.26, 0.25, 0.4}, {0.05, -0.40, 0.26, 0.4}};
    for (int k = 0; k < 4; k++) {
        const double r = ls[k].r * big * (0.85 + 0.3 * R01(sd, k));
        const double jx = (R01(sd, k + 10) - 0.5) * 0.2, jz = (R01(sd, k + 20) - 0.5) * 0.2;
        const V3 c(ls[k].x * big + jx, ls[k].y * big, ls[k].z * big + jz);
        leafBlob(b, c, V3(r * 1.25, r * 0.95, r * 1.2), sd * 3 + (uint32_t)k, A, B, 0.28, 0, 0.11);
    }
    // A fresher, lighter tuft on the crown.
    const double a = R01(sd, 70) * 6.28;
    leafBlob(b, V3(std::cos(a) * 0.12 * big, 0.62 * big, std::sin(a) * 0.12 * big), V3(0.3, 0.2, 0.28) * big, sd * 3 + 7, mix(A, B, 0.5), B * 1.18, 0.3, 0, 0.5 * big);
}

// ---- 1 Sagebrush ---------------------------------------------------------------------------

void bushSage(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.30, 0.37, 0.26), dry, V3(0.50, 0.40, 0.24)), B = dryMix(V3(0.50, 0.55, 0.40), dry, V3(0.66, 0.55, 0.34));
    // A low grey mound with a crown of twigs sticking up and out of it.
    leafBlob(b, V3(0, 0.22, 0), V3(0.6, 0.3, 0.58), sd, A, B, 0.26, 0, 0.08);
    leafBlob(b, V3(0.2, 0.2, -0.15), V3(0.4, 0.28, 0.4), sd + 1, A, B, 0.26, 0, 0.08);
    b.mat(FOLIAGE);
    for (int k = 0; k < 13; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.5, out = 0.2 + 0.6 * std::sqrt(R01(sd, k + 10)), top = 0.5 + 0.45 * R01(sd, k + 20) - 0.25 * out;
        const V3 o(std::cos(a) * 0.12, 0.2, std::sin(a) * 0.12), tip(std::cos(a) * out, top, std::sin(a) * out);
        const size_t from = b.out().v.size();
        b.color(mix(A, B, R01(sd, k + 30)));
        b.cone(o, tip, 0.08, 0.0, 3, false);
        dapple(b, from, 0.4, 0.5, A * 0.85, B * 1.05, sd + (uint32_t)k, 0.7);
    }
}

// ---- 2 Juniper -----------------------------------------------------------------------------

void bushJuniper(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.08, 0.19, 0.13), dry, V3(0.30, 0.22, 0.10)), B = dryMix(V3(0.17, 0.32, 0.20), dry, V3(0.42, 0.30, 0.13));
    const double big = p("size");
    // A gnarled stub of trunk the clumps grow from.
    b.style(V3(0.30, 0.22, 0.15), WOOD);
    b.cone(V3(0, -0.05, 0), V3(0.05, 0.4 * big, -0.03), 0.07, 0.04, 3, false);
    b.mat(FOLIAGE);
    struct C { double x, z, h, r; };
    const C cs[] = {{0, 0, 1.05, 0.46}, {0.40, 0.1, 0.8, 0.36}, {-0.32, 0.30, 0.72, 0.34}, {0.0, -0.40, 0.68, 0.34}, {-0.35, -0.25, 0.55, 0.28}};
    for (int k = 0; k < 4; k++) {
        const double h = cs[k].h * big * (0.9 + 0.2 * R01(sd, k)), r = cs[k].r * big * (0.9 + 0.2 * R01(sd, k + 9));
        const V3 base(cs[k].x * big, -0.05, cs[k].z * big);
        const size_t from = b.out().v.size();
        b.color(A);
        b.lathe(base, {V2(r, 0.0), V2(r * 0.72, h * 0.5), V2(0, h)}, 5, false);
        dapple(b, from, h * 0.5, h * 0.6, A, B, sd + (uint32_t)k, 0.6);
    }
    // Pale berries.
    for (int k = 0; k < 4; k++) {
        const double a = R01(sd, k + 80) * 6.28, y = 0.25 + 0.4 * R01(sd, k + 90), d = 0.34 + 0.2 * R01(sd, k + 100);
        b.color(dry ? V3(0.5, 0.45, 0.4) : V3(0.34, 0.42, 0.62));
        const V3 c(std::cos(a) * d * big, y * big, std::sin(a) * d * big);
        b.tri(c + V3(0.05, 0, 0), c + V3(-0.03, 0.05, 0.03), c + V3(-0.03, -0.01, -0.05));
    }
}

// ---- 3 Flowering shrub -----------------------------------------------------------------------

void bushFlowering(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.12, 0.25, 0.08), dry, V3(0.38, 0.30, 0.12)), B = dryMix(V3(0.21, 0.37, 0.11), dry, V3(0.52, 0.42, 0.17));
    static const V3 blooms[] = {V3(0.88, 0.40, 0.58), V3(0.94, 0.80, 0.22), V3(0.93, 0.92, 0.86), V3(0.64, 0.46, 0.84), V3(0.84, 0.20, 0.16), V3(0.95, 0.55, 0.18)};
    const V3 flower = dryMix(blooms[sd % 6], dry, V3(0.6, 0.5, 0.4));
    b.style(V3(0.27, 0.20, 0.13), WOOD);
    for (int k = 0; k < 2; k++) {
        const double a = k * 3.1 + R01(sd, k + 50) * 1.2;
        b.cone(V3(0, -0.04, 0), V3(std::cos(a) * 0.3, 0.3, std::sin(a) * 0.3), 0.035, 0.015, 3, false);
    }
    struct L { double x, z, y, r; };
    const L ls[] = {{0, 0, 0.36, 0.52}, {0.38, 0.14, 0.27, 0.36}, {-0.34, 0.24, 0.26, 0.37}};
    for (int k = 0; k < 3; k++) {
        const double r = ls[k].r * (0.9 + 0.2 * R01(sd, k));
        const V3 c(ls[k].x + (R01(sd, k + 10) - 0.5) * 0.12, ls[k].y, ls[k].z + (R01(sd, k + 20) - 0.5) * 0.12), rad(r * 1.25, r * 0.95, r * 1.2);
        leafBlob(b, c, rad, sd * 3 + (uint32_t)k, A, B, 0.24, 0, 0.11);
        // The blooms sit all over the upper half.
        for (int f = 0; f < (k == 0 ? 6 : 4); f++) {
            V3 pt, n;
            onTop(c, rad * 0.86, R01(sd, k * 9 + f + 100) * 6.28, 0.12 + 0.85 * R01(sd, k * 9 + f + 140), pt, n);
            fleck(b, pt, n, 0.09 + 0.05 * R01(sd, k * 9 + f + 180), flower * (0.85 + 0.3 * R01(sd, f + 30 + k)), sd + (uint32_t)(f + 9 * k));
        }
    }
}

// ---- 4 Fern clump -------------------------------------------------------------------------------

void bushFern(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.10, 0.25, 0.07), dry, V3(0.40, 0.28, 0.10)), B = dryMix(V3(0.22, 0.42, 0.11), dry, V3(0.58, 0.44, 0.17));
    b.mat(FOLIAGE);
    const int n = 10;
    for (int k = 0; k < n; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.6;
        const bool young = k >= 7;  // the inner ones stand up
        const double L = (young ? 0.5 : 0.85 + 0.25 * R01(sd, k + 10)) * (0.9 + 0.2 * R01(sd, k + 20));
        const V3 dir(std::cos(a), 0, std::sin(a)), side(-dir.z, 0, dir.x);
        const double rise = young ? 0.85 : 0.5 + 0.3 * R01(sd, k + 30), fall = young ? 0.1 : 0.35;
        const V3 p0 = dir * 0.05 + V3(0, 0.02, 0);
        const V3 p1 = dir * (L * 0.38) + V3(0, L * rise * 0.8, 0), p2 = dir * (L * 0.72) + V3(0, L * (rise - fall * 0.35), 0), p3 = dir * L + V3(0, L * (rise - fall) * 0.8 + 0.04, 0);
        const double w1 = 0.1 * (young ? 0.7 : 1), w2 = 0.085 * (young ? 0.7 : 1);
        const V3 out = norm(dir + V3(0, 0.9, 0));
        const size_t from = b.out().v.size();
        b.color(A);
        tri(b, p0 - side * 0.03, p0 + side * 0.03, p1 + side * w1, out), tri(b, p0 - side * 0.03, p1 + side * w1, p1 - side * w1, out);
        tri(b, p1 - side * w1, p1 + side * w1, p2 + side * w2, out), tri(b, p1 - side * w1, p2 + side * w2, p2 - side * w2, out);
        tri(b, p2 - side * w2, p2 + side * w2, p3, out);
        // Dark at the root, bright and sun-struck along the arch.
        ramp(b, from, 0.0, L * 0.7, A * 0.8, mix(A, B, 0.4 + 0.6 * R01(sd, k + 40)) * 1.05);
    }
}

// ---- 5 Reeds -----------------------------------------------------------------------------------------

void bushReeds(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.17, 0.28, 0.09), dry, V3(0.52, 0.44, 0.22)), B = dryMix(V3(0.34, 0.44, 0.14), dry, V3(0.68, 0.58, 0.30));
    b.mat(FOLIAGE);
    for (int k = 0; k < 11; k++) {
        const double a = R01(sd, k) * 6.28, d = 0.45 * std::sqrt(R01(sd, k + 10));
        const double h = 1.1 + 0.8 * R01(sd, k + 20), lean = 0.12 + 0.35 * R01(sd, k + 30), bend = a + (R01(sd, k + 40) - 0.5) * 1.5;
        const V3 base(std::cos(a) * d, -0.05, std::sin(a) * d), dir(std::cos(bend), 0, std::sin(bend)), side(-dir.z, 0, dir.x);
        const V3 mid = base + dir * (lean * 0.35) + V3(0, h * 0.55, 0), tip = base + dir * lean * 1.4 + V3(0, h, 0);
        const size_t from = b.out().v.size();
        b.color(A);
        blade(b, base, mid, tip, side, 0.035, 0.03);
        ramp(b, from, 0.0, h, A * 0.85, mix(A, B, 0.7 + 0.3 * R01(sd, k + 50)));
        // A cattail on a few: a brown sausage on a bare stem.
        if (k % 4 == 1) {
            b.color(V3(0.2, 0.3, 0.1));
            const V3 sb = tip - V3(0, 0.38, 0) - dir * 0.0;
            b.style(V3(0.36, 0.2, 0.1), WOOD);
            b.cone(sb, tip + V3(0, 0.06, 0), 0.04, 0.035, 4, false);
            b.mat(FOLIAGE);
        }
    }
}

// ---- 6 Cactus ----------------------------------------------------------------------------------------------

void bushCactus(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.14, 0.28, 0.14), dry, V3(0.42, 0.40, 0.18)), B = dryMix(V3(0.22, 0.38, 0.19), dry, V3(0.52, 0.48, 0.24));
    auto ribbed = [&](size_t from, const V3& axisFoot, int sides) {  // alternate ribs lighter and darker
        auto& v = b.out().v;
        for (size_t i = from; i < v.size(); i++) {
            const double ang = std::atan2(v[i].p[2] - axisFoot.z, v[i].p[0] - axisFoot.x);
            const int rib = (int)std::floor((ang + M_PI) / (2 * M_PI) * sides + 0.5);
            const V3 col = (rib & 1 ? B : A) * (0.8 + 0.3 * clamp01(v[i].p[1] / 2.0));
            v[i].c[0] = (float)col.x, v[i].c[1] = (float)col.y, v[i].c[2] = (float)col.z;
        }
    };
    b.mat(FOLIAGE);
    if (sd % 3 == 0) {  // a barrel cactus, squat and ribbed, with a crown of yellow flowers
        const double r = 0.42 + 0.1 * R01(sd, 1), h = 0.62 + 0.2 * R01(sd, 2);
        const size_t from = b.out().v.size();
        b.color(A);
        b.lathe(V3(0, -0.02, 0), {V2(r * 0.72, 0), V2(r, h * 0.35), V2(r * 0.85, h * 0.8), V2(r * 0.3, h), V2(0, h * 0.96)}, 8, false);
        ribbed(from, V3(0, 0, 0), 8);
        b.color(dry ? V3(0.6, 0.5, 0.3) : V3(0.95, 0.78, 0.15));
        for (int f = 0; f < 4; f++) {
            const double a = f * 1.6 + R01(sd, f + 5);
            const V3 c(std::cos(a) * r * 0.22, h * 0.97, std::sin(a) * r * 0.22);
            b.tri(c + V3(0.07, 0, 0), c + V3(-0.04, 0.09, 0.05), c + V3(-0.04, 0.02, -0.07));
        }
        return;
    }
    // A saguaro: a ribbed column, usually with two upturned arms.
    const double h = 2.2 + 0.5 * R01(sd, 1), r = 0.2 + 0.03 * R01(sd, 2);
    size_t from = b.out().v.size();
    b.color(A);
    b.lathe(V3(0, -0.05, 0), {V2(r * 1.05, 0), V2(r, h * 0.3), V2(r * 0.95, h * 0.85), V2(r * 0.55, h * 0.97), V2(0, h)}, 6, false);
    ribbed(from, V3(0, 0, 0), 6);
    const int arms = 1 + (int)(R01(sd, 3) * 2.2);
    for (int k = 0; k < arms; k++) {
        const double a = (k ? 3.14 : 0) + (R01(sd, k + 10) - 0.5) * 1.2, y = h * (0.38 + 0.18 * R01(sd, k + 20)), out = 0.62 + 0.15 * R01(sd, k + 30), up = 0.8 + 0.5 * R01(sd, k + 40);
        const V3 dir(std::cos(a), 0, std::sin(a)), e = dir * out + V3(0, y, 0);
        const double ar = r * 0.72;
        from = b.out().v.size();
        b.color(A);
        b.cone(dir * r * 0.5 + V3(0, y - 0.02, 0), e, ar, ar, 5, false);
        b.lathe(e, {V2(ar, 0), V2(ar * 0.95, up * 0.85), V2(0, up)}, 5, false);
        for (size_t i = from; i < b.out().v.size(); i++) {
            auto& v = b.out().v[i];
            const V3 col = mix(A, B, 0.5) * (0.85 + 0.2 * clamp01((v.p[1] - y) / up));
            v.c[0] = (float)col.x, v.c[1] = (float)col.y, v.c[2] = (float)col.z;
        }
    }
}

// ---- 7 Yucca --------------------------------------------------------------------------------------------------

void bushYucca(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.20, 0.34, 0.17), dry, V3(0.50, 0.46, 0.22)), B = dryMix(V3(0.38, 0.52, 0.26), dry, V3(0.62, 0.56, 0.30));
    const double th = 0.35 + 0.35 * R01(sd, 1), lean = (R01(sd, 2) - 0.5) * 0.3;
    const V3 top(lean, th, 0.0);
    b.style(V3(0.30, 0.23, 0.15), WOOD);
    b.cone(V3(0, -0.05, 0), top, 0.1, 0.07, 5, false);
    b.mat(FOLIAGE);
    // The skirt of dead, drooping leaves under the live rosette.
    for (int k = 0; k < 6; k++) {
        const double a = k * 1.047 + R01(sd, k) * 0.5, L = 0.38 + 0.12 * R01(sd, k + 10);
        const V3 dir(std::cos(a), 0, std::sin(a)), side(-dir.z, 0, dir.x), q = top + V3(0, -0.12, 0);
        b.color(V3(0.42, 0.34, 0.2));
        tri(b, q - side * 0.035, q + side * 0.035, q + dir * L + V3(0, -0.12, 0), dir);
    }
    for (int k = 0; k < 15; k++) {
        const double a = k * 2.399 + R01(sd, k + 20) * 0.5, tilt = k < 6 ? 0.9 : k < 11 ? 0.5 : 0.15;  // the inner ones stand, the outer spread
        const double L = (0.62 - 0.1 * (k < 6 ? 1 : 0)) * (0.85 + 0.3 * R01(sd, k + 40));
        const V3 dir(std::cos(a), 0, std::sin(a)), side(-dir.z, 0, dir.x);
        const V3 q = top + dir * 0.03, mid = q + dir * (L * 0.5 * (1 - tilt * 0.7)) + V3(0, L * 0.45 * (0.3 + tilt), 0);
        const V3 tip = q + dir * (L * 0.95 * (1 - tilt * 0.8)) + V3(0, L * (0.35 + tilt * 0.6) - 0.06 * (1 - tilt), 0);
        const size_t from = b.out().v.size();
        b.color(A);
        blade(b, q, mid, tip, side, 0.025, 0.04);
        ramp(b, from, top.y - 0.05, top.y + L * 0.9, A * 0.8, B);
    }
    if (sd & 1) {  // a flower spike of cream bells
        const V3 sb = top + V3(0, 0.15, 0), st = top + V3(0.08, 1.15, 0.03);
        b.style(V3(0.45, 0.38, 0.22), WOOD);
        b.cone(sb, st, 0.03, 0.015, 3, false);
        b.mat(FOLIAGE).color(dry ? V3(0.7, 0.65, 0.5) : V3(0.9, 0.88, 0.74));
        b.cone(top + V3(0.06, 0.7, 0.02), st + V3(0, 0.12, 0), 0.075, 0.0, 4, false);
    }
}

// ---- 8 Heather / alpine scrub ---------------------------------------------------------------------------------------

void bushHeather(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.16, 0.23, 0.10), dry, V3(0.42, 0.28, 0.13)), B = dryMix(V3(0.30, 0.34, 0.15), dry, V3(0.55, 0.40, 0.20));
    static const V3 blooms[] = {V3(0.62, 0.30, 0.60), V3(0.78, 0.40, 0.62), V3(0.50, 0.30, 0.68)};
    const V3 flower = dryMix(blooms[sd % 3], dry, V3(0.6, 0.45, 0.35));
    struct L { double x, z, y, rx, ry; };
    const L ls[] = {{0, 0, 0.12, 0.62, 0.27}, {0.4, 0.2, 0.1, 0.38, 0.2}, {-0.3, -0.34, 0.1, 0.38, 0.2}};
    for (int k = 0; k < 3; k++) {
        const V3 rad(ls[k].rx * (0.9 + 0.2 * R01(sd, k)), ls[k].ry, ls[k].rx * (0.9 + 0.2 * R01(sd, k + 9)));
        const V3 c(ls[k].x, ls[k].y + 0.05, ls[k].z);
        leafBlob(b, c, rad, sd * 5 + (uint32_t)k, A, B, 0.3, 0, 0.1);
        for (int f = 0; f < (k == 0 ? 6 : 3); f++) {
            V3 pt, n;
            onTop(c, rad * 0.86, R01(sd, k * 7 + f + 100) * 6.28, 0.35 + 0.6 * R01(sd, k * 7 + f + 140), pt, n);
            fleck(b, pt, n, 0.075 + 0.04 * R01(sd, k * 7 + f), flower * (0.8 + 0.4 * R01(sd, f + 60 + k)), sd + (uint32_t)(f + 7 * k));
        }
    }
}

// ---- 9 Tumbleweed / dry scrub ------------------------------------------------------------------------------------------------

void bushTumble(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.40, 0.31, 0.18), dry, V3(0.55, 0.52, 0.45)), B = dryMix(V3(0.64, 0.52, 0.32), dry, V3(0.74, 0.70, 0.60));
    b.mat(FOLIAGE);
    // An open tangle: thin twigs crossing through the middle in every direction, nothing solid inside.
    const V3 mid(0, 0.42, 0);
    for (int k = 0; k < 13; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.7, el = std::asin(-0.8 + 1.6 * ((k * 0.618 + R01(sd, k + 10) * 0.2) - std::floor(k * 0.618 + R01(sd, k + 10) * 0.2)));
        const V3 dir(std::cos(a) * std::cos(el), std::sin(el), std::sin(a) * std::cos(el));
        const double L = 0.42 + 0.1 * R01(sd, k + 20);
        const V3 off((R01(sd, k + 30) - 0.5) * 0.14, (R01(sd, k + 40) - 0.5) * 0.14, (R01(sd, k + 50) - 0.5) * 0.14);
        V3 p0 = mid + off - dir * L, p1 = mid + off + dir * L;
        if (p0.y < 0.04) p0.y = 0.04;
        if (p1.y < 0.04) p1.y = 0.04;
        const size_t from = b.out().v.size();
        b.color(mix(A, B, R01(sd, k + 60)));
        b.cone(p0, p1, 0.024, 0.024, 3, false);
        dapple(b, from, 0.42, 0.5, A * 0.8, B * 1.05, sd + (uint32_t)k, 0.6);
    }
}

// ---- 10 Mushrooms in moss ---------------------------------------------------------------------------------------------------------

void bushMushrooms(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 mossA = dryMix(V3(0.12, 0.24, 0.07), dry, V3(0.36, 0.30, 0.12)), mossB = dryMix(V3(0.22, 0.38, 0.10), dry, V3(0.50, 0.42, 0.18));
    static const V3 caps[] = {V3(0.62, 0.40, 0.22), V3(0.72, 0.20, 0.12), V3(0.74, 0.66, 0.52), V3(0.45, 0.30, 0.20)};
    leafBlob(b, V3(0, 0.0, 0), V3(0.62, 0.15, 0.58), sd, mossA, mossB, 0.28, 0, 0.0);
    const V3 cap = caps[sd % 4];
    const int n = 5;
    for (int k = 0; k < n; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.8, d = 0.06 + 0.34 * std::sqrt(R01(sd, k + 10));
        const double s = (k < 2 ? 1.0 : 0.62) * (0.85 + 0.3 * R01(sd, k + 20)), h = 0.3 * s + 0.06, r = 0.22 * s + 0.03, lean = (R01(sd, k + 30) - 0.5) * 0.12;
        const V3 foot(std::cos(a) * d, 0.06, std::sin(a) * d), top = foot + V3(lean, h, lean * 0.5);
        b.style(V3(0.82, 0.78, 0.68), MATTE);
        b.cone(foot, top, 0.04 * s + 0.01, 0.032 * s + 0.01, 3, false);
        b.color(cap * (0.85 + 0.3 * R01(sd, k + 40)));
        b.cone(top - V3(0, 0.03, 0), top + V3(0, r * 0.55, 0), r, 0.0, 5, false);
    }
}

// ---- 11 Mossy stones -----------------------------------------------------------------------------------------------------------------

void bushMossRocks(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 mossA = dryMix(V3(0.11, 0.23, 0.07), dry, V3(0.36, 0.30, 0.12)), mossB = dryMix(V3(0.22, 0.38, 0.11), dry, V3(0.50, 0.42, 0.18));
    struct S { double x, z, r, h; };
    const S ss[] = {{0.0, 0.0, 0.46, 0.38}, {0.5, 0.26, 0.28, 0.22}, {-0.38, 0.34, 0.2, 0.16}};
    for (int k = 0; k < 3; k++) {
        const double s = 0.85 + 0.3 * R01(sd, k);
        const V3 c(ss[k].x, ss[k].h * 0.5, ss[k].z), rad(ss[k].r * s, ss[k].h * s, ss[k].r * s * (0.8 + 0.3 * R01(sd, k + 5)));
        b.style(V3(0.46, 0.45, 0.42) * (0.85 + 0.25 * R01(sd, k + 10)), ROCK);
        b.blob(c, rad, 0.28, sd * 3 + (uint32_t)k, 0, 0.75, 0.05);
        // Moss over the top of the two big ones.
        if (k < 2) leafBlob(b, c + V3(0.04, rad.y * 0.6, 0.02), V3(rad.x * 0.85, rad.y * 0.42, rad.z * 0.8), sd * 7 + (uint32_t)k, mossA, mossB, 0.3, 0, c.y + rad.y * 0.25);
    }
}

// ---- 12 Wildflowers -----------------------------------------------------------------------------------------------------------------------

void bushWildflowers(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.14, 0.27, 0.08), dry, V3(0.42, 0.34, 0.14)), B = dryMix(V3(0.24, 0.40, 0.12), dry, V3(0.55, 0.46, 0.20));
    static const V3 pairs[][2] = {{V3(0.94, 0.80, 0.22), V3(0.93, 0.92, 0.86)}, {V3(0.62, 0.42, 0.84), V3(0.93, 0.92, 0.86)}, {V3(0.86, 0.22, 0.18), V3(0.94, 0.80, 0.22)},
                                  {V3(0.36, 0.50, 0.88), V3(0.93, 0.92, 0.86)}, {V3(0.92, 0.50, 0.58), V3(0.94, 0.80, 0.22)}};
    const V3* pair = pairs[sd % 5];
    leafBlob(b, V3(0, 0.05, 0), V3(0.62, 0.17, 0.58), sd, A, B, 0.3, 0, 0.0);
    b.mat(FOLIAGE);
    for (int k = 0; k < 9; k++) {
        const double a = R01(sd, k) * 6.28, d = 0.58 * std::sqrt(R01(sd, k + 10)), h = 0.3 + 0.32 * R01(sd, k + 20), lean = 0.1 * (R01(sd, k + 30) - 0.5);
        const V3 foot(std::cos(a) * d, 0.08, std::sin(a) * d), top = foot + V3(lean, h, lean * 0.6), side(-std::sin(a), 0, std::cos(a));
        b.color(A * 1.1);
        tri(b, foot - side * 0.012, foot + side * 0.012, top, V3(std::cos(a), 0.3, std::sin(a)));
        // A five-petalled head, tilted up and out.
        const V3 col = pair[R01(sd, k + 40) < 0.65 ? 0 : 1] * (0.88 + 0.24 * R01(sd, k + 50));
        const double s = 0.07 + 0.03 * R01(sd, k + 60);
        const V3 n = norm(V3(std::cos(a) * 0.35, 1, std::sin(a) * 0.35)), u = norm(cross(n, V3(1, 0, 0.2))), w = cross(n, u);
        const V3 c = top + n * 0.015;
        b.color(col);
        for (int f = 0; f < 5; f++) {
            const double a0 = f * 1.2566, a1 = (f + 1) * 1.2566;
            b.tri(c, c + (u * std::cos(a0) + w * std::sin(a0)) * s, c + (u * std::cos(a1) + w * std::sin(a1)) * s);
        }
        b.color(V3(0.9, 0.7, 0.15) * (dry ? 0.7 : 1.0));
        b.tri(c + n * 0.006 + u * 0.016, c + n * 0.006 + w * 0.016, c + n * 0.006 - u * 0.016);
    }
}

// ---- 13 Blackbrush ----------------------------------------------------------------------------------------------
// (13 on: the dry country's kinds, grown by a vegetation palette: vegetation.h)

// Thin twigs standing out of a mound: `n` of them from round (0, y0, 0), out to `reach` and up to about `top`.
void twigs(B& b, uint32_t sd, int n, double y0, double reach, double top, double thick, const V3& A, const V3& Bc) {
    b.mat(FOLIAGE);
    for (int k = 0; k < n; k++) {
        const double a = k * 2.399 + R01(sd, k + 200) * 0.6, out = reach * (0.35 + 0.65 * std::sqrt(R01(sd, k + 210))), up = top * (0.75 + 0.35 * R01(sd, k + 220)) - 0.3 * out;
        const V3 o(std::cos(a) * out * 0.3, y0, std::sin(a) * out * 0.3), tip(std::cos(a) * out, std::max(y0 + 0.08, up), std::sin(a) * out);
        b.color(mix(A, Bc, R01(sd, k + 230)) * (0.85 + 0.3 * R01(sd, k + 240)));
        b.cone(o, tip, thick, 0.0, 3, false);
    }
}

void bushBlackbrush(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    // Dark, greyish olive: from a distance a blackbrush flat reads as a dark stipple on the red ground.
    const V3 A = dryMix(V3(0.15, 0.17, 0.11), dry, V3(0.27, 0.23, 0.17)), B = dryMix(V3(0.25, 0.27, 0.17), dry, V3(0.36, 0.31, 0.23));
    leafBlob(b, V3(0, 0.2, 0), V3(0.62, 0.34, 0.6), sd, A, B, 0.34, 0, 0.05);
    leafBlob(b, V3(0.24 * (R01(sd, 1) - 0.5), 0.3, 0.2), V3(0.4, 0.3, 0.4), sd + 1, A, B, 0.34, 0, 0.08);
    // Its spiny grey twig ends standing out of the mound.
    twigs(b, sd, 9, 0.22, 0.74, 0.62, 0.035, V3(0.30, 0.28, 0.22), V3(0.42, 0.39, 0.31));
}

// ---- 14 Mormon tea -----------------------------------------------------------------------------------------------

void bushEphedra(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.30, 0.37, 0.14), dry, V3(0.46, 0.40, 0.20)), B = dryMix(V3(0.50, 0.55, 0.22), dry, V3(0.60, 0.54, 0.28));
    // A broom: bare jointed stems fanning up from a woody foot, each forking once.
    b.style(V3(0.33, 0.27, 0.19), WOOD);
    b.cone(V3(0, -0.05, 0), V3(0.02, 0.16, 0), 0.09, 0.06, 4, false);
    b.mat(FOLIAGE);
    for (int k = 0; k < 11; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.5, out = 0.18 + 0.52 * std::sqrt((k + 0.5) / 11.0), h = (1.2 - 0.55 * out) * (0.8 + 0.3 * R01(sd, k + 10));
        const V3 dir(std::cos(a), 0, std::sin(a)), o = dir * 0.05 + V3(0, 0.1, 0), m = dir * (out * 0.55) + V3(0, h * 0.55, 0), tip = dir * out + V3(0, h, 0);
        const size_t from = b.out().v.size();
        b.color(A);
        b.cone(o, m, 0.03, 0.022, 3, false);
        b.cone(m, tip, 0.022, 0.0, 3, false);
        if (k % 4 == 0) b.cone(m, m + V3(-dir.z * 0.16 + dir.x * 0.1, h * 0.38, dir.x * 0.16 + dir.z * 0.1), 0.02, 0.0, 3, false);
        ramp(b, from, 0.1, 1.1, A * 0.75, B * (0.9 + 0.2 * R01(sd, k + 20)));
    }
}

// ---- 15 Rabbitbrush ----------------------------------------------------------------------------------------------

void bushRabbitbrush(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.30, 0.36, 0.24), dry, V3(0.46, 0.41, 0.28)), B = dryMix(V3(0.46, 0.51, 0.34), dry, V3(0.60, 0.54, 0.38));
    b.style(V3(0.36, 0.31, 0.23), WOOD);
    for (int k = 0; k < 3; k++) {
        const double a = k * 2.1 + R01(sd, k + 50);
        b.cone(V3(0, -0.04, 0), V3(std::cos(a) * 0.25, 0.4, std::sin(a) * 0.25), 0.035, 0.02, 3, false);
    }
    // A rounded dome on its stems, open underneath, the top dusted with yellow flower heads (tan seed heads when dry).
    const V3 c(0, 0.62, 0), r(0.72, 0.46, 0.7);
    leafBlob(b, c, r, sd, A, B, 0.22, 0, 0.3);
    leafBlob(b, V3(0.3, 0.45, -0.25), V3(0.4, 0.3, 0.4), sd + 1, A, B, 0.24, 0, 0.22);
    const V3 flower = dry ? V3(0.66, 0.60, 0.44) : V3(0.84, 0.68, 0.13);
    for (int f = 0; f < 9; f++) {
        V3 pt, n;
        onTop(c, r * 0.9, R01(sd, f + 100) * 6.28, 0.25 + 0.75 * R01(sd, f + 140), pt, n);
        fleck(b, pt, n, 0.1 + 0.05 * R01(sd, f + 180), flower * (0.85 + 0.3 * R01(sd, f + 30)), sd + (uint32_t)f);
    }
}

// ---- 16 Saltbush -------------------------------------------------------------------------------------------------

void bushSaltbush(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    // Pale, silvery grey-green (four-wing saltbush, shadscale): lighter than the sage.
    const V3 A = dryMix(V3(0.34, 0.39, 0.30), dry, V3(0.50, 0.45, 0.34)), B = dryMix(V3(0.50, 0.54, 0.43), dry, V3(0.64, 0.58, 0.45));
    struct L { double x, z, y, r; };
    const L ls[] = {{0, 0, 0.3, 0.5}, {0.36, 0.12, 0.22, 0.34}, {-0.3, 0.26, 0.22, 0.34}, {-0.05, -0.36, 0.2, 0.32}};
    for (int k = 0; k < 4; k++) {
        const double r = ls[k].r * (0.85 + 0.3 * R01(sd, k));
        leafBlob(b, V3(ls[k].x + (R01(sd, k + 10) - 0.5) * 0.14, ls[k].y, ls[k].z + (R01(sd, k + 20) - 0.5) * 0.14), V3(r * 1.2, r * 0.9, r * 1.2), sd * 3 + (uint32_t)k, A, B, 0.3, 0, 0.07);
    }
    twigs(b, sd, 5, 0.25, 0.7, 0.66, 0.03, A * 0.9, B);
}

// ---- 17 Prickly pear ---------------------------------------------------------------------------------------------

void bushPricklyPear(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.26, 0.36, 0.22), dry, V3(0.44, 0.42, 0.26)), B = dryMix(V3(0.42, 0.50, 0.30), dry, V3(0.56, 0.52, 0.32));
    b.mat(FOLIAGE);
    // Flat oval pads, each grown off the top edge of one before it, the clump sprawling low over the ground.
    struct Pad { V3 c, up, side; double h, w; };
    std::vector<Pad> pads;
    const int n = 8;
    for (int k = 0; k < n; k++) {
        Pad q;
        q.h = 0.16 + 0.06 * R01(sd, k + 10), q.w = q.h * (0.7 + 0.2 * R01(sd, k + 20));
        const double a = R01(sd, k + 30) * 6.28, tilt = 0.25 + 0.6 * R01(sd, k + 40);
        const V3 lean(std::cos(a) * std::sin(tilt), std::cos(tilt), std::sin(a) * std::sin(tilt));
        V3 foot;
        if (k < 3) {
            const double fa = k * 2.1 + R01(sd, k + 50), fo = 0.22 + 0.2 * R01(sd, k + 60);
            foot = V3(std::cos(fa) * fo, -0.03, std::sin(fa) * fo);
        } else {
            const Pad& par = pads[(size_t)(R01(sd, k + 70) * (std::min<size_t>(pads.size(), 5) - 0.01))];
            foot = par.c + par.up * (par.h * 0.8) + par.side * ((R01(sd, k + 80) - 0.5) * par.w);
        }
        q.up = norm(lean), q.c = foot + q.up * q.h;
        const double sa = R01(sd, k + 90) * 6.28;
        q.side = norm(cross(q.up, V3(std::cos(sa), 0.2, std::sin(sa))));
        pads.push_back(q);
        const V3 nrm = cross(q.side, q.up);
        const V3 col = mix(A, B, R01(sd, k + 100)) * (0.8 + 0.25 * clamp01(q.c.y / 0.6));
        // (an oval: six corners, drawn from both sides)
        V3 ring[6];
        for (int e = 0; e < 6; e++) {
            const double t = e * M_PI / 3 + M_PI / 6;
            ring[e] = q.c + q.side * (std::cos(t) * q.w) + q.up * (std::sin(t) * q.h);
        }
        for (int side = 0; side < 2; side++) {
            const V3 tw = side ? nrm * -1.0 : nrm;
            b.color(col * (side ? 0.85 : 1.0));
            for (int e = 1; e + 1 < 6; e++) tri(b, ring[0] + tw * 0.012, ring[e] + tw * 0.012, ring[e + 1] + tw * 0.012, tw);
        }
        // A flower or a fruit on the rim of some.
        if (!dry && k >= 3 && R01(sd, k + 110) < 0.45) {
            const V3 f = q.c + q.up * q.h;
            b.color((sd + k) % 3 ? V3(0.88, 0.74, 0.18) : V3(0.72, 0.20, 0.40));
            b.tri(f + q.side * 0.05, f + q.up * 0.08 + nrm * 0.03, f - q.side * 0.05);
            b.tri(f + nrm * 0.05, f + q.up * 0.08, f - nrm * 0.05);
        }
    }
}

// ---- 18 Bunchgrass -----------------------------------------------------------------------------------------------

void bushBunchgrass(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    // Indian ricegrass: a pale straw tuft, a little green at the foot, its fine blades arching out.
    const V3 lo = dryMix(V3(0.36, 0.40, 0.22), dry, V3(0.46, 0.40, 0.26)), hi = dryMix(V3(0.72, 0.66, 0.42), dry, V3(0.76, 0.69, 0.48));
    b.mat(FOLIAGE);
    for (int k = 0; k < 16; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.6, out = 0.15 + 0.6 * std::sqrt((k + 0.5) / 16.0) * (0.8 + 0.4 * R01(sd, k + 10));
        const double h = (0.95 - 0.5 * out) * (0.75 + 0.4 * R01(sd, k + 20));
        const V3 dir(std::cos(a), 0, std::sin(a)), side(-dir.z, 0, dir.x);
        const V3 q = dir * 0.05 + V3(0, -0.03, 0), mid = dir * (out * 0.45) + V3(0, h * 0.7, 0), tip = dir * out + V3(0, h, 0);
        const size_t from = b.out().v.size();
        b.color(lo);
        blade(b, q, mid, tip, side, 0.03, 0.022);
        ramp(b, from, 0.0, 0.7, lo, hi * (0.9 + 0.2 * R01(sd, k + 30)));
    }
}

// ---- 19 Coyote willow --------------------------------------------------------------------------------------------

void bushWillow(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool dry = p.i("dry") != 0;
    const V3 A = dryMix(V3(0.20, 0.265, 0.14), dry, V3(0.40, 0.36, 0.23)), B = dryMix(V3(0.33, 0.395, 0.21), dry, V3(0.52, 0.47, 0.30));
    // A thicket: thin red-brown wands close together, their narrow grey-green plumes running into one upright mass.
    for (int k = 0; k < 5; k++) {
        const double a = k * 2.399 + R01(sd, k) * 0.7, o = k == 0 ? 0.05 : 0.3 + 0.22 * R01(sd, k + 5), h = (2.45 - 1.0 * o) * (0.85 + 0.25 * R01(sd, k + 10));
        const double lean = 0.05 + 0.2 * R01(sd, k + 20);
        const V3 dir(std::cos(a), 0, std::sin(a)), foot = dir * o + V3(0, -0.05, 0), top = foot + dir * (lean * h) + V3(0, h, 0);
        b.style(V3(0.36, 0.22, 0.15), WOOD);
        b.cone(foot, foot + (top - foot) * 0.4, 0.035, 0.022, 3, false);
        const V3 c = foot + (top - foot) * 0.62;
        leafBlob(b, c, V3(0.4 + 0.1 * R01(sd, k + 30), h * 0.42, 0.4 + 0.1 * R01(sd, k + 40)), sd * 5 + (uint32_t)k, A, B, 0.3, 0, 0.3);
    }
}

Reg bush({"bush", "BUSH", "NATURE AND PEOPLE", "Shrubs, scrub, ferns, reeds, cacti, flowers, mushrooms and mossy stones scattered through the maps (thousands: one design per plants.h kind; instance param dry = brown version)", 2.0,
          {
              {"LEAFY BUSH", "Round leafy shrub of lumpy clumps", {seedParam(5), choice("dry", "DRY", 0, {"GREEN", "DRY"}), num("size", "SIZE", 1, 0.7, 1.3, 0.05)}, bushLeafy},
              {"SAGEBRUSH", "Grey-green twiggy desert shrub", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushSage},
              {"JUNIPER", "Dark dense clump of upright foliage", {seedParam(4), choice("dry", "DRY", 0, {"GREEN", "DRY"}), num("size", "SIZE", 1, 0.7, 1.3, 0.05)}, bushJuniper},
              {"FLOWERING SHRUB", "Leafy shrub dotted with blooms", {seedParam(3), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushFlowering},
              {"FERN", "Clump of arching fronds for the forest floor", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushFern},
              {"REEDS", "Tall blades and cattails for the wet ground", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushReeds},
              {"CACTUS", "Saguaro with arms, or a squat barrel cactus", {seedParam(1), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushCactus},
              {"YUCCA", "Spiky rosette on a short trunk, a flower spike on some", {seedParam(1), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushYucca},
              {"HEATHER", "Low alpine scrub in flower", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushHeather},
              {"TUMBLEWEED", "Dry tangle of twigs", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushTumble},
              {"MUSHROOMS", "Toadstools in a patch of moss", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushMushrooms},
              {"MOSSY ROCKS", "A few small stones under moss", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushMossRocks},
              {"WILDFLOWERS", "A patch of wildflowers in the grass", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushWildflowers},
              {"BLACKBRUSH", "Low dark grey-olive mound with spiny twig ends (the Colorado Plateau's benches)", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushBlackbrush},
              {"MORMON TEA", "A broom of bare jointed yellow-green stems", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushEphedra},
              {"RABBITBRUSH", "Rounded grey-green shrub dusted with yellow flower heads", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushRabbitbrush},
              {"SALTBUSH", "Pale silver-grey saltbush or shadscale", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushSaltbush},
              {"PRICKLY PEAR", "A low sprawling clump of flat cactus pads", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushPricklyPear},
              {"BUNCHGRASS", "A pale tuft of ricegrass", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushBunchgrass},
              {"COYOTE WILLOW", "A thicket of thin upright wands in narrow grey-green leaf, by water", {seedParam(2), choice("dry", "DRY", 0, {"GREEN", "DRY"})}, bushWillow},
          }});

}  // namespace
