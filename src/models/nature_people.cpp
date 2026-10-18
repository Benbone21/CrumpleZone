// Nature and people: trees, bushes, stumps, and the person on foot.
// Trees are placed by the thousand, so they are modelled with a few big, cheap, flat-shaded
// pieces (drooping bough tiers, lumpy crown blobs) rather than lots of small ones.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

const V3 SNOW(0.86, 0.88, 0.92);
const V3 UP(0, 1, 0);

inline double R01(uint32_t seed, int i) { return hash01(seed, (uint32_t)(i < 0 ? i + 100000 : i)); }
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// A flat triangle with its own colour at each corner (the kit paints a primitive one colour).
void triC(B& b, const V3& a, const V3& bb, const V3& c, const V3& na, const V3& nb, const V3& nc, const V3& ca, const V3& cb, const V3& cc) {
    auto& v = b.out().v;
    const size_t n = v.size();
    b.triN(a, bb, c, na, nb, nc);
    if (v.size() != n + 3) return;  // degenerate: nothing was added
    const V3* cs[3] = {&ca, &cb, &cc};
    for (int k = 0; k < 3; k++) {
        v[n + k].c[0] = (float)cs[k]->x;
        v[n + k].c[1] = (float)cs[k]->y;
        v[n + k].c[2] = (float)cs[k]->z;
    }
}

// Snow on the tops: every triangle from `from` on that faces up becomes a white matte skin
// (costs no triangles). thr: how flat a face must be. leafy: the snow is on foliage, so it fades with it
// (a snag's or trunk's snow doesn't: only leaves dissolve for the camera).
void snowTops(B& b, size_t from, double thr, uint32_t seed, bool leafy = false) {
    auto& v = b.out().v;
    for (size_t i = from; i + 2 < v.size(); i += 3) {
        const V3 p0(v[i].p[0], v[i].p[1], v[i].p[2]), p1(v[i + 1].p[0], v[i + 1].p[1], v[i + 1].p[2]), p2(v[i + 2].p[0], v[i + 2].p[1], v[i + 2].p[2]);
        V3 n = cross(p1 - p0, p2 - p0);
        if (len(n) < 1e-12) continue;
        n = norm(n);
        const V3 vn(v[i].n[0] + v[i + 1].n[0] + v[i + 2].n[0], v[i].n[1] + v[i + 1].n[1] + v[i + 2].n[1], v[i].n[2] + v[i + 1].n[2] + v[i + 2].n[2]);
        if (dot(n, vn) < 0) n = -n;
        const double jit = (R01(seed, (int)(i / 3)) - 0.5) * 0.3;
        if (n.y < thr + jit) continue;
        const double s = 0.9 + 0.1 * R01(seed + 5, (int)(i / 3));
        for (int k = 0; k < 3; k++) {
            v[i + k].c[0] = (float)(SNOW.x * s), v[i + k].c[1] = (float)(SNOW.y * s), v[i + k].c[2] = (float)(SNOW.z * s);
            v[i + k].mat = leafy ? SNOWLEAF : MATTE;
        }
    }
}

// A tapered, slightly rough limb along a path: a ring of `sides` at each point. `jag` lifts the
// last ring's corners (a broken, splintered end); the end cap is a fan sunk by `capDip`.
void limb(B& b, const std::vector<V3>& pts, const std::vector<double>& rad, int sides, const V3& c0, const V3& c1, bool capTop = false,
          uint32_t seed = 0, double rough = 0, const V3& capCol = V3(0.62, 0.5, 0.34), const std::vector<double>& jag = {}, double capDip = 0) {
    const int n = (int)pts.size();
    if (n < 2) return;
    std::vector<V3> T(n), U(n);
    for (int i = 0; i < n; i++) {
        const V3 t = pts[std::min(i + 1, n - 1)] - pts[std::max(i - 1, 0)];
        T[i] = len(t) > 1e-12 ? norm(t) : UP;
    }
    U[0] = norm(cross(T[0], std::fabs(T[0].y) < 0.9 ? UP : V3(1, 0, 0)));
    for (int i = 1; i < n; i++) {
        const V3 u = U[i - 1] - T[i] * dot(U[i - 1], T[i]);
        U[i] = len(u) > 1e-9 ? norm(u) : norm(cross(T[i], UP));
    }
    std::vector<std::vector<V3>> pos(n, std::vector<V3>(sides)), nor(n, std::vector<V3>(sides));
    for (int i = 0; i < n; i++) {
        const V3 w = cross(T[i], U[i]);
        for (int j = 0; j < sides; j++) {
            const double a = 2 * M_PI * j / sides;
            const V3 d = U[i] * std::cos(a) + w * std::sin(a);
            const double r = rad[i] * (1 + rough * (R01(seed, i * 31 + j) - 0.5));
            pos[i][j] = pts[i] + d * r;
            nor[i][j] = d;
            if (i == n - 1 && !jag.empty()) pos[i][j] = pos[i][j] + UP * jag[j % jag.size()];
        }
    }
    auto colAt = [&](int i, int j) { return mix(c0, c1, (double)i / (n - 1)) * (0.92 + 0.16 * R01(seed + 3, i * 17 + j)); };
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j < sides; j++) {
            const int k = (j + 1) % sides;
            triC(b, pos[i][j], pos[i][k], pos[i + 1][k], nor[i][j], nor[i][k], nor[i + 1][k], colAt(i, j), colAt(i, k), colAt(i + 1, k));
            triC(b, pos[i][j], pos[i + 1][k], pos[i + 1][j], nor[i][j], nor[i + 1][k], nor[i + 1][j], colAt(i, j), colAt(i + 1, k), colAt(i + 1, j));
        }
    if (capTop) {
        const V3 ctr = pts[n - 1] - UP * capDip;
        for (int j = 0; j < sides; j++) {
            const int k = (j + 1) % sides;
            const V3 cn = norm(T[n - 1] + UP * 0.5);
            triC(b, ctr, pos[n - 1][j], pos[n - 1][k], cn, cn, cn, capCol * 0.85, capCol, capCol);
        }
    }
}

// Roots flaring out of a trunk base: pointed cones sunk into the ground.
void roots(B& b, uint32_t sd, int n, double r, double len0, int sides = 4) {
    for (int k = 0; k < n; k++) {
        const double a = 2 * M_PI * k / n + R01(sd, k + 200) * 0.8, L = len0 * (0.8 + 0.5 * R01(sd, k + 210));
        const V3 d(std::cos(a), 0, std::sin(a));
        b.cone(d * (r * 0.6) + V3(0, 0.55, 0), d * (r + L) + V3(0, -0.12, 0), r * 0.38, 0.0, sides, false);
    }
}

// ---- Conifer boughs ----------------------------------------------------------------------

struct Bough {
    double cx = 0, cz = 0, edgeY = 0, apexY = 0, r = 1;
    int sides = 9;
    double rag = 0.4, droop = 0.25, apexDx = 0, apexDz = 0;
    double band = 0, shade = 1;  // a ring `band` of the way up from the rim to the apex, `shade` darker (in under the tier above: tiers read as layers)
    int lod = 0;  // far copies (the "lod" param, forest.h): 1 no underside or sprigs, 2 fewer sides, no tips or band either
};

// One drooping tier of boughs: a ragged cone (top surface), its dark underside, and (with
// snow) a white skin over the upper part of the top surface, ragged where it slides off. Sprigs
// stick out of the cone part way down (the boughs of the tier, layered), and its rim hangs in tips.
void tier(B& b, const Bough& t, uint32_t seed, int idx, const V3& top, const V3& edgeTop, const V3& under, bool snow, double cover = 0.55) {
    const uint32_t s = seed * 131u + (uint32_t)idx * 17u;
    const int sides = t.lod >= 2 ? std::max(5, t.sides / 2 + 1) : t.sides;
    std::vector<V3> e(sides);
    std::vector<V3> d(sides);
    const double phase = R01(s, 99) * 6.2832;
    // (the tier's rim tilts a little: no two tiers hang level)
    const double tilt = 0.12 * t.r * R01(s, 98), tiltA = R01(s, 97) * 6.2832;
    for (int i = 0; i < sides; i++) {
        const double a = phase + 2 * M_PI * i / sides + (R01(s, i + 40) - 0.5) * 0.35;
        d[i] = V3(std::cos(a), 0, std::sin(a));
        const double rr = t.r * (1 + t.rag * ((i & 1) ? 0.3 : -0.28) + t.rag * 0.5 * (R01(s, i) - 0.5));
        e[i] = V3(t.cx, t.edgeY - t.droop * rr * (0.25 + 0.75 * R01(s, i + 20)) + tilt * std::cos(a - tiltA), t.cz) + d[i] * rr;
    }
    const V3 A(t.cx + t.apexDx, t.apexY, t.cz + t.apexDz), H(t.cx, t.edgeY + 0.2 * t.r, t.cz);
    const double steep = std::max(0.3, (t.apexY - t.edgeY) / t.r);
    for (int i = 0; i < sides; i++) {
        const int k = (i + 1) % sides;
        const V3 n0 = norm(d[i] * (steep * 0.75) + UP), n1 = norm(d[k] * (steep * 0.75) + UP);
        const double sh0 = 0.9 + 0.2 * R01(s, i + 60), sh1 = 0.9 + 0.2 * R01(s, k + 60);
        // Snow lies on the top surface: white matte at the apex fading to the ragged edge.
        V3 c0 = edgeTop * sh0, c1 = edgeTop * sh1, ca = top, cm = edgeTop * t.shade;
        if (snow) {
            c0 = mix(c0, SNOW * 0.9, cover * (0.5 + 0.9 * R01(s + 3, i)));
            c1 = mix(c1, SNOW * 0.9, cover * (0.5 + 0.9 * R01(s + 3, k)));
            ca = SNOW, cm = mix(cm, SNOW * 0.85, std::min(1.0, cover * 1.4));
            b.mat(SNOWLEAF);
        }
        const V3 na = norm(n0 + n1 + UP * 1.5);
        if (t.band > 0 && t.lod < 2) {
            const V3 m0 = e[i] + (A - e[i]) * t.band, m1 = e[k] + (A - e[k]) * t.band;
            const V3 nm0 = norm(n0 + na), nm1 = norm(n1 + na);
            triC(b, e[i], m0, m1, n0, nm0, nm1, c0, cm, cm);
            triC(b, e[i], m1, e[k], n0, nm1, n1, c0, cm, c1);
            triC(b, m0, A, m1, nm0, na, nm1, cm, ca, cm);
        } else {
            triC(b, e[i], A, e[k], n0, na, n1, c0, ca, c1);
        }
        b.mat(FOLIAGE);
        if (t.lod >= 2) continue;
        // A dangling bough tip hung from the rim between the two points.
        const V3 dm = norm(d[i] + d[k]);
        const double rm = (len(V3(e[i].x - t.cx, 0, e[i].z - t.cz)) + len(V3(e[k].x - t.cx, 0, e[k].z - t.cz))) * 0.5;
        const V3 tip = V3(t.cx, std::min(e[i].y, e[k].y) - 0.18 * t.r - t.droop * 0.4 * t.r * (0.6 + 0.8 * R01(s, i + 70)), t.cz) + dm * (rm * (1.02 + 0.12 * R01(s, i + 80)));
        const V3 fn = norm(dm * 1.0 + UP * 0.3);
        triC(b, e[i], tip, e[k], norm(n0 * 0.5 + fn), fn, norm(n1 * 0.5 + fn), edgeTop * 0.8 * sh0, edgeTop * 0.62, edgeTop * 0.8 * sh1);
        if (t.lod >= 1) continue;
        // The underside: dark, up to the trunk (seen from under the boughs, and between the tiers).
        const V3 dn = norm(V3(0, -1, 0) + (d[i] + d[k]) * 0.25);
        triC(b, e[k], H, e[i], dn, V3(0, -1, 0), dn, under * (0.9 + 0.2 * R01(s, i + 110)), under * 0.7, under * (0.9 + 0.2 * R01(s, k + 110)));
        // A sprig out of the cone part way down, lit on top (the bough under the needles), drooping.
        if (t.r > 0.45 && R01(s, i + 120) < 0.8) {
            const double f = 0.42 + 0.22 * R01(s, i + 130), w = 0.18 + 0.12 * R01(s, i + 140);
            const V3 m0 = A + (e[i] - A) * f, m1 = A + (e[k] - A) * f;
            const V3 p0 = m0 + (m1 - m0) * (0.5 - w), p1 = m0 + (m1 - m0) * (0.5 + w);
            const V3 st = (m0 + m1) * 0.5 + dm * (t.r * (0.22 + 0.14 * R01(s, i + 150))) - UP * (t.r * (0.08 + 0.2 * t.droop));
            const V3 sn = norm(dm * (steep * 0.5) + UP);
            V3 cs = edgeTop * (1.05 + 0.15 * R01(s, i + 160)), cb = mix(edgeTop, top, f) * 0.85;
            if (snow) cs = mix(cs, SNOW * 0.9, cover * 0.8), cb = mix(cb, SNOW * 0.85, cover * 0.6), b.mat(SNOWLEAF);
            triC(b, p0, st, p1, sn, norm(sn + dm * 0.5), sn, cb, cs, cb);
            b.mat(FOLIAGE);
        }
    }
}

double pathX(const std::vector<V3>& pts, double y, bool z = false) {
    for (size_t i = 0; i + 1 < pts.size(); i++)
        if (y <= pts[i + 1].y || i + 2 == pts.size()) {
            const double t = clamp01((y - pts[i].y) / std::max(1e-6, pts[i + 1].y - pts[i].y));
            return lerp(z ? pts[i].z : pts[i].x, z ? pts[i + 1].z : pts[i + 1].x, t);
        }
    return 0;
}

const V3 BARK_LO(0.24, 0.17, 0.12), BARK_HI(0.34, 0.25, 0.17);

// A conifer's trunk: flared roots at the foot, a little crooked, rough bark, and a few bare dead
// limbs low down where the tiers have died back (they read as dark sticks under the boughs).
std::vector<V3> conTrunk(B& b, uint32_t sd, double r0, double top, int deadLimbs, double limbY0, double limbY1, int lod) {
    const double lx = (R01(sd, 300) - 0.5) * 0.16, lz = (R01(sd, 301) - 0.5) * 0.16;
    const std::vector<V3> tr = {V3(0, -0.15, 0), V3(0, 0.45, 0), V3(lx * 0.3, 1.6, lz * 0.3), V3(lx, 0.42 * top, lz), V3(lx * 0.5, 0.72 * top, lz * 0.5), V3(0, top, 0)};
    b.style(BARK_LO, WOOD);
    limb(b, tr, {r0, r0 * 0.68, r0 * 0.54, r0 * 0.36, r0 * 0.2, 0.02}, 7 - 2 * std::min(lod, 1) - (lod >= 2), BARK_LO * 0.9, BARK_HI, false, sd, 0.16);
    if (lod >= 1) return tr;
    b.color(BARK_LO * 0.85);
    roots(b, sd, 5, r0 * 0.7, r0 * 0.95, 4);
    for (int k = 0; k < deadLimbs; k++) {
        const double y = limbY0 + (limbY1 - limbY0) * (k + R01(sd, k + 310)) / deadLimbs, a = k * 2.4 + R01(sd, k + 320) * 1.2;
        const double L = 0.35 + 0.5 * R01(sd, k + 330);
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), dir(std::cos(a), -0.1 + 0.3 * R01(sd, k + 340), std::sin(a));
        b.color(mix(BARK_LO, V3(0.36, 0.32, 0.27), 0.5));
        b.cone(o, o + dir * L, 0.035, 0.006, 3, false);
    }
    return tr;
}

// Classic tiered fir: drooping, ragged tiers, darker and bluer low down.
void pineClassic(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const int N = std::clamp(p.i("tiers"), 5, 10);
    const bool snow = p.i("snow") != 0;
    const double droop = p("droop"), fat = p("width");
    const int lod = p.i("lod");
    conTrunk(b, sd, 0.3, 9.4, 4, 0.9, 2.2, lod);
    b.mat(FOLIAGE);
    for (int k = 0; k < N; k++) {
        const double t = N > 1 ? (double)k / (N - 1) : 0;
        Bough g;
        g.edgeY = 1.75 + t * 6.2;
        g.apexY = (k == N - 1) ? 10.0 : g.edgeY + 3.1 - 0.9 * t;
        g.r = (2.1 - 1.7 * t) * fat * (0.92 + 0.16 * R01(sd, k));
        g.sides = 9 + (int)std::lround(4 * (1 - t));
        g.rag = 0.55;
        g.droop = droop;
        g.apexDx = (R01(sd, k + 400) - 0.5) * 0.2, g.apexDz = (R01(sd, k + 410) - 0.5) * 0.2;
        g.lod = lod;
        if (k < N - 1) g.band = std::clamp(0.95 * 6.2 / (N - 1) / (g.apexY - g.edgeY), 0.2, 0.6), g.shade = 0.6;
        const V3 lowC(0.08, 0.19, 0.16), highC(0.18, 0.35, 0.11);
        // (each tier a shade off the next: some sunnier, some bluer)
        const double j = R01(sd, k + 420) - 0.5;
        const V3 c = mix(lowC, highC, t) * (0.94 + 0.12 * R01(sd, k + 430)) + V3(0.02 * j, 0.01 * j, -0.015 * j);
        tier(b, g, sd, k, c * 1.2, c * 1.05, c * 0.45, snow, 0.5 + 0.1 * (1 - t));
    }
}

// Tall narrow spruce: lots of small steeply-drooping tiers, dark blue-green.
void pineSpruce(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const int N = std::clamp(p.i("tiers"), 7, 14);
    const bool snow = p.i("snow") != 0;
    const double fat = p("width");
    const int lod = p.i("lod");
    conTrunk(b, sd, 0.26, 9.6, 3, 0.7, 1.6, lod);
    b.mat(FOLIAGE);
    for (int k = 0; k < N; k++) {
        const double t = (double)k / (N - 1);
        Bough g;
        g.edgeY = 1.0 + t * 7.6;
        g.apexY = (k == N - 1) ? 10.0 : g.edgeY + 2.3 - 0.5 * t;
        g.r = (1.25 - 1.0 * t) * fat * (0.9 + 0.2 * R01(sd, k));
        g.sides = 9;
        g.rag = 0.5;
        g.droop = 0.55;
        g.apexDx = (R01(sd, k + 400) - 0.5) * 0.12, g.apexDz = (R01(sd, k + 410) - 0.5) * 0.12;
        g.lod = lod;
        if (k < N - 1) g.band = std::clamp(0.95 * 7.6 / (N - 1) / (g.apexY - g.edgeY), 0.2, 0.6), g.shade = 0.66;
        const V3 lowC(0.08, 0.19, 0.17), highC(0.15, 0.31, 0.18);
        const V3 c = mix(lowC, highC, t) * (0.94 + 0.12 * R01(sd, k + 430));
        tier(b, g, sd, k, c * 1.15, c * 0.85, c * 0.4, snow, 0.5 + 0.1 * (1 - t));
    }
}

// Scraggly lodgepole: a long bare crooked trunk with dead stubs, a high thin crown of
// lopsided tufts.
void pineLodgepole(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool snow = p.i("snow") != 0;
    const double lean = p("lean");
    const int lod = p.i("lod");
    const std::vector<V3> trunk = {V3(0, -0.15, 0), V3(0.10 * lean, 2.5, 0.05 * lean), V3(0.30 * lean, 5.2, -0.10 * lean), V3(0.24 * lean, 7.8, 0.03), V3(0.05 * lean, 10.0, 0)};
    b.style(V3(0.30, 0.23, 0.18), WOOD);
    limb(b, trunk, {0.2, 0.15, 0.105, 0.065, 0.012}, lod ? 4 : 5, V3(0.26, 0.20, 0.16), V3(0.42, 0.28, 0.16), false, sd, 0.18);
    b.color(0.24, 0.19, 0.15);
    if (!lod) roots(b, sd, 4, 0.16, 0.28, 3);
    // Dead branch stubs on the bare lower trunk.
    b.color(0.27, 0.21, 0.16);
    for (int k = 0; k < (lod ? 0 : 5); k++) {
        const double y = 1.2 + k * 1.1 + R01(sd, k) * 0.4, a = R01(sd, k + 10) * 6.28, L = 0.3 + 0.35 * R01(sd, k + 20);
        const V3 o(pathX(trunk, y), y, pathX(trunk, y, true)), dir(std::cos(a), 0.15 + 0.25 * R01(sd, k + 30), std::sin(a));
        b.cone(o, o + dir * L, 0.04, 0.008, 3, false);
    }
    b.mat(FOLIAGE);
    // The crown: tufts hung off the upper trunk, some on bare side branches.
    const V3 needles(0.19, 0.27, 0.09);
    for (int k = 0; k < 6; k++) {
        const double t = k / 5.0;
        const double y = 5.4 + t * 3.2;
        const double a = k * 2.4 + R01(sd, k + 50) * 1.0;
        const double off = (0.35 + 0.65 * (1 - t) * (0.5 + R01(sd, k + 60))) * 1.0;
        Bough g;
        g.cx = pathX(trunk, y) + std::cos(a) * off;
        g.cz = pathX(trunk, y, true) + std::sin(a) * off;
        g.edgeY = y - 0.15 * R01(sd, k + 70);
        g.apexY = g.edgeY + 1.4 + 0.4 * R01(sd, k + 80);
        g.r = (1.15 - 0.55 * t) * (0.8 + 0.4 * R01(sd, k + 90));
        g.sides = 6;
        g.rag = 0.7;
        g.droop = 0.5;
        g.apexDx = -std::cos(a) * 0.15;
        g.apexDz = -std::sin(a) * 0.15;
        g.lod = lod;
        const V3 c = needles * (0.85 + 0.3 * R01(sd, k + 100));
        if (off > 0.45 && lod < 2) {  // a bare branch out to the tuft
            b.style(V3(0.27, 0.21, 0.16), WOOD);
            const V3 o(pathX(trunk, y - 0.4), y - 0.4, pathX(trunk, y - 0.4, true));
            b.cone(o, V3(g.cx, g.edgeY + 0.1, g.cz), 0.05, 0.02, 4, false);
            b.mat(FOLIAGE);
        }
        tier(b, g, sd, k + 10, c * 1.25, c, c * 0.4, snow, 0.55);
    }
    // The leader.
    Bough g;
    g.cx = trunk.back().x, g.cz = 0, g.edgeY = 8.7, g.apexY = 10.0, g.r = 0.5, g.sides = 6, g.rag = 0.5, g.droop = 0.4, g.lod = lod;
    tier(b, g, sd, 30, needles * 1.3, needles, needles * 0.4, snow, 0.55);
}

Reg treePine({"tree_pine", "PINE TREE", "NATURE AND PEOPLE", "Conifer: the forest trees of the mountain maps (placed by the thousand; scaled per tree)", 16,
              {
                  {"CLASSIC FIR", "Tiered fir, drooping ragged boughs, bluer and darker low down",
                   {seedParam(3), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("tiers", "TIERS", 8, 5, 10, 1), num("droop", "DROOP", 0.3, 0.1, 0.7, 0.05), num("width", "WIDTH", 1, 0.7, 1.3, 0.05)},
                   pineClassic},
                  {"TALL SPRUCE", "Tall narrow spruce, many small steep tiers, near black-green",
                   {seedParam(5), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("tiers", "TIERS", 11, 7, 14, 1), num("width", "WIDTH", 1, 0.7, 1.4, 0.05)},
                   pineSpruce},
                  {"LODGEPOLE", "Scraggly pine: bare crooked trunk, a high thin crown",
                   {seedParam(7), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("lean", "LEAN", 1, 0, 2.5, 0.1)},
                   pineLodgepole},
              }});

// ---- Broadleaf trees ---------------------------------------------------------------------

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

// kind: 0 oak, 1 aspen, 2 willow. season: 0 green, 1 late summer, 2 autumn.
void leafPalette(int kind, int season, uint32_t seed, int blobIdx, V3& A, V3& B) {
    if (kind == 0) A = V3(0.12, 0.25, 0.07), B = V3(0.21, 0.37, 0.09);
    if (kind == 1) A = V3(0.17, 0.31, 0.09), B = V3(0.29, 0.43, 0.12);
    if (kind == 2) A = V3(0.20, 0.34, 0.10), B = V3(0.36, 0.48, 0.14);
    if (season == 1) {
        A = mix(A, V3(0.34, 0.34, 0.07), 0.55);
        B = mix(B, V3(0.52, 0.46, 0.10), 0.6);
    } else if (season == 2) {
        const double h = R01(seed + 77, blobIdx);
        if (kind == 1) A = V3(0.80, 0.60, 0.07), B = V3(0.92, 0.76, 0.12);
        else if (h < 0.4) A = V3(0.68, 0.26, 0.05), B = V3(0.84, 0.50, 0.08);
        else if (h < 0.75) A = V3(0.80, 0.50, 0.07), B = V3(0.90, 0.68, 0.12);
        else A = V3(0.50, 0.14, 0.06), B = V3(0.72, 0.30, 0.07);
        if (kind == 2) A = mix(A, V3(0.5, 0.5, 0.1), 0.3);
    }
}

// A leaf lobe's two colours: the kind's palette, each lobe a shade off the next (sunnier on top).
void lobePalette(int kind, int season, uint32_t sd, int k, double up, V3& A, V3& B) {
    leafPalette(kind, season, sd, k, A, B);
    const double j = R01(sd + 31, k) - 0.5, lit = 0.92 + 0.12 * up + 0.1 * R01(sd + 37, k);
    A = (A + V3(0.03 * j, 0.015 * j, -0.01 * j)) * lit, B = (B + V3(0.03 * j, 0.015 * j, -0.01 * j)) * lit;
}

// A spreading oak: a stout flared trunk forking at about 3 m into three or four limbs, each forking
// again; the crown is clumps of leaves at the ends of the limbs, round a high middle.
void oak(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const int season = p.i("season");
    const double wide = p("width");
    const bool snow = p.i("snow") != 0;
    const int lod = p.i("lod");
    const V3 barkLo(0.21, 0.16, 0.12), barkHi(0.33, 0.26, 0.20);
    b.style(barkLo, WOOD);
    const V3 fork(0.08, 2.9, 0.04);
    limb(b, {V3(0, -0.15, 0), V3(0, 0.6, 0), V3(0.03, 1.8, 0.02), fork}, {0.46, 0.31, 0.27, 0.25}, lod ? 5 : 7, barkLo, barkHi, false, sd, 0.18);
    b.color(barkLo * 0.95);
    if (!lod) roots(b, sd, 5, 0.34, 0.5, 4);
    struct Lump { V3 c; double rx, ry; };
    std::vector<Lump> L;
    const int nl = 3 + (int)(sd % 2);
    for (int k = 0; k < nl; k++) {
        const double a = 2 * M_PI * k / nl + (R01(sd, k + 50) - 0.5) * 0.9, out = (2.0 + 0.5 * R01(sd, k + 55)) * wide;
        const V3 dir(std::cos(a), 0, std::sin(a)), side(-dir.z, 0, dir.x);
        const V3 m = fork + dir * (out * 0.45) + V3(0, 1.3 + 0.3 * R01(sd, k + 60), 0);
        const V3 e = fork + dir * out + V3(0, 2.7 + 0.7 * R01(sd, k + 65), 0);
        limb(b, {fork - dir * 0.05, m, e}, {0.22, 0.15, 0.08}, lod ? 4 : 5, barkLo * 1.1, barkHi, false, sd + (uint32_t)k * 7, 0.12);
        // A fork off the limb, out to one side.
        const double sgn = (k & 1) ? 1 : -1;
        const V3 f = m + norm(dir * 0.6 + side * sgn) * (1.1 * wide) + V3(0, 1.0 + 0.4 * R01(sd, k + 70), 0);
        if (!lod) limb(b, {m, f}, {0.1, 0.045}, 4, barkLo * 1.1, barkHi, false, sd + (uint32_t)k * 7 + 3, 0.1);
        const double big = lod >= 2 ? 1.12 : 1.0;  // (far: the side clumps left out, the end ones a little bigger)
        L.push_back({e + V3(0, 0.6, 0), (1.7 + 0.35 * R01(sd, k + 30)) * big, (1.35 + 0.3 * R01(sd, k + 40)) * big});
        if (lod < 2) L.push_back({f + V3(0, 0.3, 0), 1.2 + 0.3 * R01(sd, k + 80), 1.0 + 0.25 * R01(sd, k + 90)});
    }
    // The middle and top of the crown, over the fork.
    L.push_back({V3(0, 7.6, 0), 2.1, 1.7});
    L.push_back({V3(0.4, 8.7, -0.2), 1.5, 1.2});
    if (lod < 2) L.push_back({V3(-0.5 * wide, 6.4, 0.6 * wide), 1.5, 1.2});
    const size_t from = b.out().v.size();
    for (size_t k = 0; k < L.size(); k++) {
        V3 A, B;
        lobePalette(0, season, sd, (int)k, (L[k].c.y - 6.0) / 3.0, A, B);
        const V3 c(L[k].c.x, std::min(L[k].c.y, 9.0), L[k].c.z);
        leafBlob(b, c, V3(L[k].rx * wide, L[k].ry, L[k].rx * wide), sd * 7 + (uint32_t)k, A, B, 0.22, 0);
    }
    if (snow) snowTops(b, from, 0.45, sd, true);
}

void poplar(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const int season = p.i("season");
    const double lean = p("lean");
    const int lod = p.i("lod");
    // Pale aspen bark, darker at the foot, with the black scars where branches fell.
    const std::vector<V3> tr = {V3(0, -0.15, 0), V3(0, 0.5, 0), V3(0.03 * lean, 1.6, 0), V3(0.1 * lean, 4.2, 0), V3(0.2 * lean, 7.5, 0)};
    b.style(V3(0.62, 0.60, 0.52), WOOD);
    limb(b, tr, {0.26, 0.19, 0.16, 0.1, 0.04}, lod ? 4 : 6, V3(0.40, 0.38, 0.33), V3(0.70, 0.68, 0.60), false, sd, 0.1);
    b.color(0.42, 0.40, 0.35);
    if (!lod) roots(b, sd, 4, 0.18, 0.25, 3);
    b.color(0.10, 0.09, 0.08);
    for (int k = 0; k < (lod ? 0 : 7); k++) {
        const double y = 0.8 + 3.0 * (k + R01(sd, k + 500)) / 7, a = R01(sd, k + 510) * 6.2832;
        const double r = (y < 1.6 ? lerp(0.19, 0.16, (y - 0.5) / 1.1) : lerp(0.16, 0.1, (y - 1.6) / 2.6)) + 0.012;
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), d(std::cos(a), 0, std::sin(a)), t(-std::sin(a), 0, std::cos(a));
        const double w = 0.05 + 0.04 * R01(sd, k + 520);
        b.tri(o + d * r - t * w, o + d * r + UP * 0.05, o + d * r + t * w);
        b.tri(o + d * r - t * w, o + d * r + t * w, o + d * r - UP * 0.06);
    }
    // A few thin branches up into the crown.
    b.style(V3(0.55, 0.53, 0.46), WOOD);
    for (int k = 0; k < (lod ? 0 : 3); k++) {
        const double y = 4.0 + 0.9 * k, a = k * 2.1 + R01(sd, k + 530);
        const V3 o(pathX(tr, y), y, pathX(tr, y, true));
        b.cone(o, o + V3(std::cos(a) * 0.8, 1.1, std::sin(a) * 0.8), 0.05, 0.015, 3, false);
    }
    const size_t from = b.out().v.size();
    const double ys[] = {3.9, 5.0, 6.3, 7.6, 8.8, 9.6};
    const double rs[] = {0.9, 1.25, 1.35, 1.2, 0.95, 0.6};
    const double ry[] = {1.2, 1.5, 1.6, 1.5, 1.3, 0.55};
    for (int k = 0; k < 6; k++) {
        V3 A, B;
        lobePalette(1, season, sd, k, k / 5.0, A, B);
        const double x = (R01(sd, k) - 0.5) * 0.4 + lean * 0.05 * ys[k], z = (R01(sd, k + 10) - 0.5) * 0.4;
        leafBlob(b, V3(x, ys[k], z), V3(rs[k], ry[k], rs[k] * 0.95), sd * 11 + (uint32_t)k, A, B, 0.2, 0);
        // A smaller clump hung off the side of each.
        if (k < 5 && lod < 2) {
            const double a = R01(sd, k + 540) * 6.2832, o = rs[k] * 0.8;
            lobePalette(1, season, sd, k + 10, k / 5.0, A, B);
            leafBlob(b, V3(x + std::cos(a) * o, ys[k] - 0.3, z + std::sin(a) * o), V3(rs[k] * 0.55, ry[k] * 0.5, rs[k] * 0.55), sd * 13 + (uint32_t)k, A, B, 0.2, 0);
        }
    }
    if (p.i("snow")) snowTops(b, from, 0.45, sd, true);
}

void willow(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const int season = p.i("season");
    const double drop = p("droop");
    b.style(V3(0.25, 0.20, 0.15), WOOD);
    limb(b, {V3(0, -0.15, 0), V3(0.2, 1.4, 0.1), V3(0.5, 3.2, 0)}, {0.58, 0.40, 0.30}, 6, V3(0.2, 0.16, 0.12), V3(0.30, 0.24, 0.18), false, sd, 0.15);
    limb(b, {V3(0.4, 3.0, 0), V3(-0.6, 5.2, 0.7)}, {0.2, 0.1}, 4, V3(0.3, 0.23, 0.17), V3(0.32, 0.25, 0.19), false, sd);
    limb(b, {V3(0.4, 3.0, 0), V3(1.6, 5.5, -0.5)}, {0.2, 0.1}, 4, V3(0.3, 0.23, 0.17), V3(0.32, 0.25, 0.19), false, sd + 1);
    const size_t from = b.out().v.size();
    const double bx[] = {0.3, -1.6, 1.8, 0.4, -0.2}, bz[] = {0, 0.6, -0.6, -1.4, 1.5}, by[] = {7.9, 7.0, 7.1, 6.8, 6.9}, br[] = {2.4, 1.9, 1.9, 1.8, 1.8};
    for (int k = 0; k < 5; k++) {
        V3 A, B;
        leafPalette(2, season, sd, k, A, B);
        leafBlob(b, V3(bx[k], by[k], bz[k]), V3(br[k], br[k] * 0.62, br[k]), sd * 5 + (uint32_t)k, A, B, 0.18, 0);
    }
    if (p.i("snow")) snowTops(b, from, 0.45, sd, true);
    // Long curtains of leaves hanging from the crown.
    b.mat(FOLIAGE);
    const int strands = p.i("lod") >= 2 ? 8 : 14;
    for (int k = 0; k < strands; k++) {
        const double a = 2 * M_PI * k / strands + R01(sd, k) * 0.3;
        const double R = 1.9 + 1.0 * R01(sd, k + 10), top = 6.9 - 0.2 * R01(sd, k + 20), bot = std::max(0.7, top - (3.2 + 2.6 * drop) * (0.7 + 0.5 * R01(sd, k + 30)) - 1.0 * drop);
        const V3 dir(std::cos(a), 0, std::sin(a)), tg(-std::sin(a), 0, std::cos(a));
        const V3 cx(0.3, 0, 0);
        const V3 T = cx + dir * R + V3(0, top, 0), M = cx + dir * (R + 0.3) + V3(0, (top + bot) / 2, 0), Bt = cx + dir * (R + 0.15) + V3(0, bot, 0);
        V3 A, Bc;
        leafPalette(2, season, sd, k, A, Bc);
        const V3 ct = A * 0.8, cm = mix(A, Bc, 0.5), cb = Bc * 1.15;
        const double w0 = 0.75, w1 = 0.6, w2 = 0.12;
        const V3 n = norm(dir + UP * 0.25);
        triC(b, T - tg * w0, T + tg * w0, M + tg * w1, n, n, n, ct, ct, cm);
        triC(b, T - tg * w0, M + tg * w1, M - tg * w1, n, n, n, ct, cm, cm);
        triC(b, M - tg * w1, M + tg * w1, Bt + tg * w2, n, n, n, cm, cm, cb);
        triC(b, M - tg * w1, Bt + tg * w2, Bt - tg * w2, n, n, n, cm, cb, cb);
    }
}

Reg treeBroadleaf({"tree_broadleaf", "BROADLEAF TREE", "NATURE AND PEOPLE", "Deciduous tree for the lowland and valley maps (placed by the thousand; scaled per tree)", 16,
                   {
                       {"OAK", "Round lumpy crown on a forked trunk",
                        {seedParam(4), choice("season", "SEASON", 0, {"GREEN", "LATE SUMMER", "AUTUMN"}), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("width", "CROWN WIDTH", 1, 0.7, 1.3, 0.05)},
                        oak},
                       {"POPLAR", "Tall narrow aspen-like column with pale bark",
                        {seedParam(9), choice("season", "SEASON", 0, {"GREEN", "LATE SUMMER", "AUTUMN"}), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("lean", "LEAN", 0.5, -2, 2, 0.1)},
                        poplar},
                       {"WILLOW", "Wide dome with long drooping curtains of leaves",
                        {seedParam(2), choice("season", "SEASON", 0, {"GREEN", "LATE SUMMER", "AUTUMN"}), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("droop", "DROOP", 0.5, 0, 1, 0.05)},
                        willow},
                   }});

// ---- Snags -------------------------------------------------------------------------------

// Bark still clinging to a dead trunk: dark plates round it between y0 and y1, ragged at the edges.
void barkPlates(B& b, uint32_t sd, const std::vector<V3>& tr, const std::vector<double>& rad, double y0, double y1, int n, const V3& col) {
    auto radAt = [&](double y) {
        for (size_t i = 0; i + 1 < tr.size(); i++)
            if (y <= tr[i + 1].y || i + 2 == tr.size()) return lerp(rad[i], rad[i + 1], clamp01((y - tr[i].y) / std::max(1e-6, tr[i + 1].y - tr[i].y)));
        return rad.back();
    };
    for (int k = 0; k < n; k++) {
        const double a = R01(sd, k + 600) * 6.2832, ya = y0 + (y1 - y0) * R01(sd, k + 610), h = 0.4 + 0.9 * R01(sd, k + 620);
        const double yb = std::min(y1, ya + h), w = 0.35 + 0.35 * R01(sd, k + 630);
        auto at = [&](double y, double da) {
            const double r = radAt(y) * 1.06 + 0.012;
            return V3(pathX(tr, y) + std::cos(a + da) * r, y, pathX(tr, y, true) + std::sin(a + da) * r);
        };
        b.color(col * (0.85 + 0.3 * R01(sd, k + 640)));
        b.quad(at(ya, -w), at(ya, w), at(yb, w * 0.7), at(yb - 0.15, -w * 0.8));
    }
}

void snagGrey(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool snow = p.i("snow") != 0;
    const int lod = p.i("lod");
    const V3 lo(0.34, 0.31, 0.27), hi(0.62, 0.59, 0.54);
    b.style(lo, WOOD);
    const std::vector<V3> tr = {V3(0, -0.15, 0), V3(0, 0.6, 0), V3(0.04, 2.7, 0), V3(-0.06, 5.5, 0.05), V3(0.05, 8.0, 0)};
    const std::vector<double> rad = {0.32, 0.25, 0.2, 0.14, 0.05};
    limb(b, tr, rad, lod ? 4 : 6, lo * 1.05, hi * 0.95, true, sd, 0.14, V3(0.66, 0.6, 0.5));
    b.color(lo * 1.1);
    if (!lod) roots(b, sd, 4, 0.28, 0.5, 3);
    // The last of the bark, dark and low down; the wood above it bleached.
    if (!lod) barkPlates(b, sd, tr, rad, 0.2, 3.2, 7, V3(0.22, 0.18, 0.14));
    b.style(lo, WOOD);
    const int nb = p.i("branches");
    for (int k = 0; k < nb; k++) {
        const double y = 2.9 + k * (3.6 / std::max(1, nb)) + 0.3 * R01(sd, k), a = k * 2.4 + R01(sd, k + 10), L = 1.9 - 0.18 * k + 0.4 * R01(sd, k + 20);
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), dir(std::cos(a), 0.0, std::sin(a));
        const V3 m = o + dir * (L * 0.55) + V3(0, 0.45 * L, 0), e = o + dir * L + V3(0, 0.55 * L + 0.2, 0);
        const bool broken = k % 3 == 1;
        limb(b, {o - dir * 0.05, m, broken ? m + (e - m) * 0.55 : e}, {0.09, 0.05, broken ? 0.035 : 0.012}, lod ? 3 : 4, lo * 1.1, hi * 0.9, broken, sd + (uint32_t)k, 0.1, V3(0.66, 0.6, 0.5), broken ? std::vector<double>{0.05, -0.04, 0.08, -0.02} : std::vector<double>{});
        if (!broken && !lod) {
            b.color(hi * 0.85);
            b.cone(m, m + V3(-dir.z * 0.7, 0.45, dir.x * 0.7), 0.03, 0.0, 3, false);
            b.cone(m + (e - m) * 0.6, m + (e - m) * 0.6 + V3(dir.z * 0.45, 0.35, -dir.x * 0.45), 0.02, 0.0, 3, false);
        }
    }
    // Snapped-off stubs where the lower branches were.
    b.color(lo * 1.15);
    for (int k = 0; k < (lod ? 0 : 4); k++) {
        const double y = 1.4 + 0.45 * k + 0.2 * R01(sd, k + 650), a = R01(sd, k + 660) * 6.2832;
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), dir(std::cos(a), 0.15, std::sin(a));
        b.cone(o, o + dir * (0.18 + 0.15 * R01(sd, k + 670)), 0.045, 0.025, 3, false);
    }
    if (snow) {
        // A cap of snow on the broken top and along the branches' upper faces.
        snowTops(b, 0, 0.7, sd);
    }
}

void snagBurnt(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool snow = p.i("snow") != 0;
    const double ash = p("ash");
    const V3 lo(0.09, 0.08, 0.075), hi(0.05, 0.045, 0.045);
    b.style(lo, WOOD);
    const std::vector<V3> tr = {V3(0, -0.15, 0), V3(0.03, 3.0, 0), V3(-0.04, 5.8, 0.03), V3(0.0, 7.4, 0)};
    limb(b, tr, {0.38, 0.27, 0.2, 0.13}, 6, mix(lo, V3(0.32, 0.31, 0.3), ash), hi, true, sd, 0.2, V3(0.14, 0.1, 0.07), {0, -0.9, -0.1, -1.2, -0.05, -0.5}, 0.25);
    b.style(hi * 1.2, WOOD);
    roots(b, sd, 4, 0.4, 0.6, 4);
    // Ash-grey streaks where the char has flaked.
    b.color(0.22, 0.21, 0.2);
    for (int k = 0; k < 2; k++) {
        const double a = k * 2.1 + 0.4, y0 = 0.6 + 1.7 * k, y1 = y0 + 1.5;
        const V3 d(std::cos(a), 0, std::sin(a)), t(-std::sin(a), 0, std::cos(a));
        const V3 p0 = d * (pathX({V3(0, 0, 0), V3(0, 7.4, 0)}, y0) + (0.38 - 0.25 * y0 / 7.4) * 1.02) + V3(0, y0, 0), p1 = d * ((0.38 - 0.25 * y1 / 7.4) * 0.97) + V3(0, y1, 0);
        b.quad(p0 - t * 0.09, p0 + t * 0.09, p1 + t * 0.06, p1 - t * 0.06);
    }
    for (int k = 0; k < 4; k++) {
        const double y = 2.8 + k * 0.85 + 0.3 * R01(sd, k), a = k * 2.2 + R01(sd, k + 10), L = 0.7 + 0.8 * R01(sd, k + 20);
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), dir(std::cos(a), 0.25, std::sin(a));
        b.cone(o - dir * 0.05, o + dir * L, 0.08, 0.025, 4, false);
    }
    // One long charred limb.
    {
        const double a = 1.3, y = 4.6;
        const V3 o(pathX(tr, y), y, pathX(tr, y, true)), dir(std::cos(a), 0, std::sin(a));
        limb(b, {o, o + dir * 1.1 + V3(0, 0.8, 0), o + dir * 1.7 + V3(0, 1.0, 0)}, {0.12, 0.07, 0.04}, 3, hi * 1.5, hi, true, sd, 0.1, V3(0.14, 0.1, 0.07));
    }
    if (snow) snowTops(b, 0, 0.7, sd);
}

void snagBroken(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const bool snow = p.i("snow") != 0;
    const double height = p("height");
    const V3 lo(0.32, 0.27, 0.21), hi(0.50, 0.44, 0.36);
    b.style(lo, WOOD);
    const std::vector<V3> tr = {V3(0, -0.15, 0), V3(0, 2.5, 0), V3(0.03, height - 1.4, 0), V3(0.03, height, 0)};
    limb(b, tr, {0.44, 0.32, 0.26, 0.23}, 6, lo, hi, true, sd, 0.14, V3(0.66, 0.54, 0.38), {0, -1.5, -0.3, -1.1, -0.6, -1.3}, 0.3);
    b.color(lo * 0.9);
    roots(b, sd, 4, 0.5, 0.6, 4);
    // A long splinter peeled off and hanging from the break.
    b.color(0.62, 0.52, 0.38);
    b.tri(V3(0.2, height - 0.2, 0.2), V3(0.27, height - 0.3, 0.0), V3(0.5, height - 2.4, 0.25));
    // A strip where the bark has fallen away, and a splinter hanging off the break.
    b.color(0.60, 0.52, 0.40);
    {
        const double a = 0.6;
        const V3 d(std::cos(a), 0, std::sin(a)), t(-std::sin(a), 0, std::cos(a));
        const V3 p0 = d * 0.335 + V3(0, 1.8, 0), p1 = d * 0.3 + V3(0, 4.3, 0);
        b.quad(p0 - t * 0.14, p0 + t * 0.14, p1 + t * 0.1, p1 - t * 0.1);
    }
    b.style(lo * 1.1, WOOD);
    b.cone(V3(0.3, 4.2, 0.1), V3(1.1, 4.9, 0.4), 0.12, 0.03, 4, false);  // a stub of branch
    if (snow) snowTops(b, 0, 0.7, sd);
}

Reg treeSnag({"tree_snag", "DEAD SNAG", "NATURE AND PEOPLE", "Dead standing tree for the burnt and dry maps (scaled per tree)", 20,
              {
                  {"GREY SNAG", "Bleached bare snag with a few broken branches",
                   {seedParam(6), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("branches", "BRANCHES", 4, 2, 5, 1)},
                   snagGrey},
                  {"BURNT", "Charred black trunk, split jagged top, scorched stubs",
                   {seedParam(3), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("ash", "ASH ON BASE", 0.35, 0, 1, 0.05)},
                   snagBurnt},
                  {"BROKEN TRUNK", "Thick trunk snapped off in splinters, bark peeling",
                   {seedParam(8), choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"}), num("height", "HEIGHT", 7.2, 4.5, 8, 0.1)},
                   snagBroken},
              }});

// ---- Stumps ------------------------------------------------------------------------------

const V3 SAP(0.66, 0.54, 0.37), HEART(0.46, 0.33, 0.20);

void stumpSawn(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const double h = 0.6, R = 0.34 * p("size");
    const int sg = 7;
    b.style(V3(0.30, 0.22, 0.15), WOOD);
    b.lathe(V3(0, 0, 0), {V2(R * 1.45, -0.1), V2(R * 1.06, 0.16), V2(R, h)}, sg, true);
    // The cut top: lighter outer wood, a darker ring, a lighter heart.
    b.color(SAP);
    b.lathe(V3(0, 0, 0), {V2(R, h), V2(R * 0.78, h)}, sg, false);
    b.color(HEART);
    b.lathe(V3(0, 0, 0), {V2(R * 0.78, h), V2(R * 0.56, h)}, sg, false);
    b.color(SAP * 1.05);
    b.lathe(V3(0, 0, 0), {V2(R * 0.56, h), V2(0, h)}, sg, false);
    b.color(0.28, 0.2, 0.14);
    roots(b, sd, 3, R, 0.3, 3);
}

void stumpBroken(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const double R = 0.33 * p("size"), h = p("height");
    b.style(V3(0.31, 0.23, 0.16), WOOD);
    limb(b, {V3(0, -0.1, 0), V3(0, 0.2, 0), V3(0, 0.45, 0), V3(0, h, 0)}, {R * 1.4, R * 1.05, R * 0.98, R * 0.85}, 7, V3(0.27, 0.20, 0.14), V3(0.36, 0.27, 0.19), true, sd, 0.15, HEART,
         {0, -0.32, -0.04, -0.28, -0.1, -0.36, -0.2}, 0.1);
    // A tall splinter of fresh pale wood.
    b.color(SAP);
    b.tri(V3(R * 0.5, h - 0.25, R * 0.3), V3(R * 0.1, h - 0.2, -R * 0.5), V3(R * 0.25, h + 0.32, -R * 0.1));
    b.color(0.28, 0.2, 0.14);
    roots(b, sd, 3, R, 0.3, 3);
}

void stumpMossy(B& b, const P& p) {
    const uint32_t sd = (uint32_t)p.i("seed");
    const double R = 0.37 * p("size"), h = 0.52;
    b.style(V3(0.25, 0.21, 0.17), WOOD);
    limb(b, {V3(0, -0.1, 0), V3(0, 0.15, 0), V3(0, h, 0)}, {R * 1.45, R * 1.05, R * 0.95}, 6, V3(0.22, 0.19, 0.15), V3(0.31, 0.26, 0.2), true, sd, 0.2, V3(0.34, 0.29, 0.23), {0, -0.04, 0.03, -0.1, 0.02, -0.06}, 0.05);
    b.color(0.24, 0.19, 0.14);
    roots(b, sd, 2, R, 0.2, 3);
    // Moss over the top edge and round the base.
    leafBlob(b, V3(R * 0.35, h - 0.02, -R * 0.2), V3(R * 0.62, 0.11, R * 0.52), sd, V3(0.14, 0.28, 0.08), V3(0.26, 0.40, 0.12), 0.25, 0, h - 0.1);
    leafBlob(b, V3(-R * 0.75, 0.12, R * 0.55), V3(0.2, 0.17, 0.2), sd + 1, V3(0.13, 0.26, 0.08), V3(0.24, 0.38, 0.11), 0.25, 0, 0.0);
    // Bracket fungus.
    b.mat(MATTE);
    b.color(0.72, 0.62, 0.45);
    for (int k = 0; k < 1; k++) {
        const double a = 2.4 + k * 1.1, y = 0.2 + 0.14 * k, r = R * 1.0 + 0.02;
        const V3 d(std::cos(a), 0, std::sin(a)), t(-std::sin(a), 0, std::cos(a));
        const V3 c = d * r + V3(0, y, 0);
        b.tri(c - t * 0.07, c + t * 0.07, c + d * 0.1 - V3(0, 0.02, 0));
        b.tri(c - t * 0.07, c + t * 0.07, c + d * 0.09 - V3(0, 0.05, 0));
    }
}

Reg stump({"stump", "TREE STUMP", "NATURE AND PEOPLE", "Cut tree stump in the logging and forest scenery (scaled per stump)", 1.6,
           {
               {"SAW CUT", "Flat saw-cut top with rings, roots flaring",
                {seedParam(1), num("size", "SIZE", 1, 0.7, 1.4, 0.05)},
                stumpSawn},
               {"BROKEN", "Snapped off in jagged splinters",
                {seedParam(2), num("size", "SIZE", 1, 0.7, 1.4, 0.05), num("height", "HEIGHT", 0.65, 0.4, 0.9, 0.05)},
                stumpBroken},
               {"MOSSY OLD", "Rotten old stump under moss and fungus",
                {seedParam(3), num("size", "SIZE", 1, 0.7, 1.4, 0.05)},
                stumpMossy},
           }});

// ---- The person --------------------------------------------------------------------------
// Facing +X, left is +Z, origin between the feet. Limbs hang from the hips and shoulders and
// are swung with pitch(): positive pitch swings a hanging limb forward (+X).

enum PersonStyle { RANGER = 0, MECHANIC = 1, OFFROADER = 2 };

V3 skinTone(int i) {
    static const V3 c[] = {V3(0.86, 0.68, 0.55), V3(0.80, 0.60, 0.45), V3(0.66, 0.46, 0.32), V3(0.42, 0.28, 0.20)};
    return c[std::clamp(i, 0, 3)];
}
V3 hairTone(int i) {
    static const V3 c[] = {V3(0.07, 0.06, 0.055), V3(0.22, 0.14, 0.09), V3(0.62, 0.48, 0.24), V3(0.50, 0.22, 0.10), V3(0.55, 0.55, 0.54)};
    return c[std::clamp(i, 0, 4)];
}

void person(B& b, const P& p, int style) {
    const V3 jacket = paint(p.i("jacket"));
    const double s = std::clamp(p("swing"), -1.0, 1.0);
    const V3 skin = skinTone(p.i("skin")), hair = hairTone(p.i("hair"));
    const V3 shade = jacket * 0.78;  // pockets, cuffs, seams
    // Style colours.
    V3 pants, boots, soles(0.12, 0.11, 0.1), hat(0.15, 0.2, 0.15);
    if (style == RANGER) pants = V3(0.36, 0.33, 0.22), boots = V3(0.30, 0.20, 0.12), hat = paint(1) * 0.75;
    else if (style == MECHANIC) pants = jacket, boots = V3(0.10, 0.09, 0.08);
    else pants = V3(0.17, 0.22, 0.34), boots = V3(0.80, 0.80, 0.77), soles = V3(0.88, 0.87, 0.82), hat = paint(p.i("beanie"));
    const bool coveralls = style == MECHANIC;

    // Walk cycle: stride about the hips; the body drops a little as the legs spread.
    const double stride = 0.45 * s * 180 / M_PI;  // degrees at the hips
    const double drop = 0.83 * (1 - std::cos(0.45 * std::fabs(s)));
    b.push();
    b.move(0, -drop, 0);

    // ---- Legs ----
    const double HIP = 0.93, THIGH = 0.45, SHIN = 0.38;
    auto leg = [&](double side) {  // side +1 left (+Z), -1 right
        const double a = side * stride;                         // left leg forward when swing > 0
        const double kb = a < 0 ? a * 1.0 : 3.0 * (a / 26.0);   // back leg folds at the knee
        b.push();
        b.move(0, HIP, side * 0.088);
        b.pitch(a);
        b.style(pants, MATTE);
        b.cone(V3(0, 0.02, 0), V3(0, -THIGH, 0), 0.084, 0.064, 9, false);
        if (coveralls) {  // thigh pocket with a screwdriver
            b.color(shade);
            b.box(V3(0.082, -0.2, side * 0.025), V3(0.02, 0.13, 0.09), 0.006);
            b.style(V3(0.78, 0.55, 0.05), MATTE);
            b.cyl(V3(0.09, -0.15, side * 0.03), V3(0.1, -0.1, side * 0.03), 0.009, 5, true);
            b.style(pants, MATTE);
        }
        b.move(0, -THIGH, 0);
        b.pitch(kb);
        b.sphere(V3(0, 0, 0), V3(0.066, 0.066, 0.066), 0);
        b.cone(V3(0, 0, 0), V3(0, -SHIN, 0), 0.066, 0.05, 9, false);
        if (coveralls) {
            b.color(shade);
            b.box(V3(0.06, -0.01, 0), V3(0.02, 0.1, 0.1), 0.008);  // knee patch
            b.color(pants);
        }
        b.move(0, -SHIN, 0);
        b.pitch(-(a + kb) * 0.55);  // the foot stays flatter than the leg
        // Boot: shaft, rounded foot, sole.
        const bool sneaker = style == OFFROADER;
        if (sneaker) {
            b.style(pants * 0.9, MATTE);  // jeans cuff over the shoe
            b.cyl(V3(0, 0.03, 0), V3(0, -0.035, 0), 0.058, 9, false);
        } else {
            b.style(boots, MATTE);
            b.cyl(V3(0, 0.1, 0), V3(0, -0.04, 0), 0.062, 9, false);
        }
        b.style(boots, MATTE);
        b.box(V3(0.05, -0.058, 0), V3(0.26, 0.07, 0.104), 0.028);
        if (!sneaker) {
            b.box(V3(-0.03, -0.045, 0), V3(0.1, 0.06, 0.105), 0.02);  // heel counter
        } else {
            b.color(boots * 0.92);
            b.box(V3(0.1, -0.035, 0), V3(0.1, 0.03, 0.095), 0.012);
        }
        b.style(soles, MATTE);
        b.box(V3(0.051, -0.0905, 0), V3(0.268, 0.019, 0.108), 0.008);
        b.pop();
    };
    leg(1);
    leg(-1);

    // ---- Pelvis and torso ----
    b.style(pants, MATTE);
    b.push();
    b.scale(V3(0.66, 1, 1));
    b.lathe(V3(0, 0, 0), {V2(0.150, 0.80), V2(0.158, 0.88), V2(0.15, 0.97)}, 12, true);
    b.pop();
    b.style(jacket, MATTE);
    b.push();
    b.scale(V3(0.64, 1, 1));
    b.lathe(V3(0, 0, 0), {V2(0.176, 0.84), V2(0.182, 0.90), V2(0.168, 1.05), V2(0.187, 1.25), V2(0.19, 1.36), V2(0.15, 1.44), V2(0.075, 1.495)}, 12, true);
    b.pop();
    // Collar / hem detail by style.
    b.style(shade, MATTE);
    if (style == OFFROADER) {
        b.push();  // ribbed hem
        b.scale(V3(0.64, 1, 1));
        b.cyl(V3(0, 0.835, 0), V3(0, 0.885, 0), 0.19, 12, false);
        b.pop();
        b.box(V3(0.113, 0.99, 0), V3(0.045, 0.12, 0.27), 0.02);  // kangaroo pocket
        b.style(jacket * 1.15, MATTE);
        b.blob(V3(-0.075, 1.475, 0), V3(0.085, 0.065, 0.125), 0.12, 5, 1, 0.2, 1.46);  // the hood, bunched behind the neck
        b.torus(V3(0.0, 1.49, 0), 0.075, 0.026, 10, 5);  // hood rim round the neck
        b.style(V3(0.88, 0.88, 0.85), MATTE);  // drawstrings
        for (int k = -1; k <= 1; k += 2) {
            b.box(V3(0.125, 1.35, k * 0.04), V3(0.008, 0.2, 0.008));
            b.box(V3(0.128, 1.245, k * 0.04), V3(0.012, 0.02, 0.012));
        }
    } else if (style == MECHANIC) {
        b.torus(V3(0.0, 1.495, 0), 0.08, 0.022, 10, 5);  // collar
        b.style(V3(0.10, 0.1, 0.11), MATTE);
        b.box(V3(0.114, 1.14, 0), V3(0.006, 0.38, 0.016));  // zip
        b.style(shade, MATTE);
        b.box(V3(0.103, 1.27, 0.085), V3(0.02, 0.085, 0.1), 0.006);  // chest pocket
        b.style(V3(0.88, 0.88, 0.85), MATTE);
        b.box(V3(0.106, 1.27, -0.075), V3(0.016, 0.045, 0.1), 0.004);  // name patch
        b.style(V3(0.62, 0.1, 0.08), MATTE);
        b.box(V3(-0.1, 0.96, -0.09), V3(0.03, 0.14, 0.08), 0.01);  // rag in the back pocket
    } else {
        b.push();  // belt
        b.scale(V3(0.64, 1, 1));
        b.cyl(V3(0, 0.975, 0), V3(0, 1.03, 0), 0.172, 12, false);
        b.pop();
        b.style(V3(0.7, 0.62, 0.3), METAL);
        b.box(V3(0.112, 1.0, 0), V3(0.01, 0.045, 0.05), 0.004);
        b.style(shade, MATTE);
        for (int k = -1; k <= 1; k += 2) b.box(V3(0.1, 1.28, k * 0.085), V3(0.02, 0.07, 0.085), 0.006);  // chest pockets
        b.style(V3(0.95, 0.72, 0.06), MATTE);
        b.box(V3(0.11, 1.3, 0.085), V3(0.006, 0.04, 0.04), 0.002);  // badge
    }
    // Backpack straps and pack (ranger).
    if (style == RANGER && p.i("pack")) {
        const V3 packCol(0.30, 0.25, 0.16);
        b.style(packCol * 0.7, MATTE);
        for (int k = -1; k <= 1; k += 2) {
            b.beam(V3(0.0, 1.455, k * 0.105), V3(0.105, 1.35, k * 0.1), 0.045, 0.012, UP);
            b.beam(V3(0.108, 1.35, k * 0.1), V3(0.118, 1.1, k * 0.095), 0.045, 0.012, UP);
            b.beam(V3(0.0, 1.455, k * 0.105), V3(-0.125, 1.35, k * 0.1), 0.045, 0.012, UP);
        }
        b.style(packCol, MATTE);
        b.box(V3(-0.2, 1.2, 0), V3(0.15, 0.36, 0.27), 0.05);
        b.style(packCol * 0.85, MATTE);
        b.box(V3(-0.205, 1.4, 0), V3(0.16, 0.08, 0.28), 0.03);  // flap
        b.box(V3(-0.2, 1.1, 0.145), V3(0.09, 0.2, 0.04), 0.015);  // side pockets
        b.box(V3(-0.2, 1.1, -0.145), V3(0.09, 0.2, 0.04), 0.015);
        b.style(paint(0) * 0.9, MATTE);
        b.box(V3(-0.275, 1.2, 0), V3(0.02, 0.2, 0.2), 0.008);  // rolled mat
    }

    // ---- Arms ----
    auto arm = [&](double side) {
        const double a = -side * 0.5 * s * 180 / M_PI;           // the right arm goes with the left leg
        const double elbow = 12 + 22 * std::max(0.0, a) / 28.6;  // the forward arm bends more
        b.push();
        b.move(0, 1.385, side * 0.2);
        b.style(jacket, MATTE);
        b.sphere(V3(0, 0, 0), V3(0.058, 0.058, 0.058), 1);  // the shoulder
        b.roll(-side * 5);
        b.pitch(a);
        b.cone(V3(0, 0.0, 0), V3(0, -0.29, 0), 0.058, 0.048, 8, false);
        b.move(0, -0.29, 0);
        b.sphere(V3(0, 0, 0), V3(0.048, 0.048, 0.048), 0);
        b.pitch(elbow);
        b.cone(V3(0, 0, 0), V3(0, -0.25, 0), 0.048, 0.04, 8, false);
        b.style(coveralls ? shade : (style == OFFROADER ? shade : V3(0.14, 0.14, 0.15)), MATTE);
        b.cyl(V3(0, -0.215, 0), V3(0, -0.265, 0), 0.047, 8, false);  // cuff
        if (coveralls) {
            b.style(shade * 0.55, MATTE);  // grease on the forearm
            b.box(V3(0.044, -0.13, 0), V3(0.012, 0.07, 0.05), 0.004);
        }
        b.style(skin, MATTE);
        b.sphere(V3(0.004, -0.31, 0), V3(0.03, 0.056, 0.026), 1);  // the hand
        b.cone(V3(0.012, -0.285, -side * 0.02), V3(0.04, -0.33, -side * 0.018), 0.014, 0.008, 4, true);  // thumb
        b.pop();
    };
    arm(1);
    arm(-1);

    // ---- Head ----
    b.style(skin, MATTE);
    b.cyl(V3(0, 1.47, 0), V3(0, 1.6, 0), 0.044, 8, false);  // neck
    b.sphere(V3(0.008, 1.675, 0), V3(0.1, 0.115, 0.09), 2);
    b.sphere(V3(0.032, 1.6, 0), V3(0.066, 0.058, 0.062), 1);  // chin and jaw
    for (int k = -1; k <= 1; k += 2) b.sphere(V3(-0.005, 1.668, k * 0.09), V3(0.02, 0.034, 0.013), 0);  // ears
    b.cone(V3(0.092, 1.668, 0), V3(0.128, 1.652, 0), 0.02, 0.006, 6, true);  // nose
    b.style(V3(0.07, 0.05, 0.04), MATTE);
    for (int k = -1; k <= 1; k += 2) {
        b.sphere(V3(0.093, 1.69, k * 0.037), V3(0.012, 0.013, 0.012), 0);  // eyes
        b.box(V3(0.09, 1.717, k * 0.038), V3(0.012, 0.009, 0.04), 0.002);  // brows
    }
    b.style(V3(skin.x * 0.8, skin.y * 0.55, skin.z * 0.55), MATTE);
    b.box(V3(0.092, 1.622, 0), V3(0.01, 0.007, 0.042), 0.002);  // mouth
    // Hair and hats.
    const double HY = 1.675;
    if (style == RANGER) {
        b.style(hair, MATTE);
        b.blob(V3(-0.03, HY, 0), V3(0.1, 0.113, 0.096), 0.04, 3, 1, 0.3, HY - 0.07);  // hair at the back and sides
        b.style(hat, MATTE);  // cap
        b.blob(V3(0.0, HY + 0.045, 0), V3(0.108, 0.082, 0.098), 0.0, 4, 1, 0.3, HY + 0.035);
        b.push();
        b.scale(V3(1, 1, 0.9));
        b.cyl(V3(0.0, HY + 0.03, 0), V3(0.0, HY + 0.056, 0), 0.107, 12, false);
        b.pop();
        b.push();
        b.move(0.09, HY + 0.036, 0);
        b.pitch(-12);
        b.extrudeUp({V2(0.0, 0.092), V2(0.06, 0.082), V2(0.1, 0.045), V2(0.115, 0.0), V2(0.1, -0.045), V2(0.06, -0.082), V2(0.0, -0.092)}, 0.008, -0.004);
        b.pop();
        b.style(V3(0.95, 0.72, 0.06), MATTE);
        b.box(V3(0.106, HY + 0.07, 0), V3(0.006, 0.026, 0.026), 0.002);  // badge
    } else if (style == MECHANIC) {
        b.style(hair, MATTE);
        b.push();
        b.move(0, HY, 0);
        b.pitch(35);
        b.blob(V3(-0.012, 0.004, 0), V3(0.114, 0.13, 0.103), 0.025, 3, 1, 0.3, -0.055);
        b.pop();
        if (p.i("cap")) {
            b.style(hat, MATTE);
            b.blob(V3(0.0, HY + 0.05, 0), V3(0.108, 0.078, 0.098), 0.0, 4, 1, 0.3, HY + 0.035);
            b.push();
            b.move(0.085, HY + 0.04, 0);
            b.pitch(-8);
            b.extrudeUp({V2(0.0, 0.09), V2(0.05, 0.08), V2(0.085, 0.04), V2(0.095, 0.0), V2(0.085, -0.04), V2(0.05, -0.08), V2(0.0, -0.09)}, 0.008, -0.004);
            b.pop();
        }
    } else {
        // Beanie with a turned-up band, hair poking out round the back.
        b.style(hair, MATTE);
        b.blob(V3(-0.03, HY - 0.01, 0), V3(0.1, 0.105, 0.094), 0.05, 3, 1, 0.3, HY - 0.075);
        b.push();
        b.move(0, HY, 0);
        b.pitch(10);
        b.style(hat, MATTE);
        b.blob(V3(-0.005, 0.012, 0), V3(0.109, 0.112, 0.099), 0.0, 4, 1, 0.3, 0.03);
        b.style(hat * 0.8, MATTE);
        b.push();
        b.scale(V3(1, 1, 0.91));
        b.cyl(V3(-0.005, 0.03, 0), V3(-0.005, 0.07, 0), 0.111, 12, false);
        b.pop();
        b.style(hat * 1.15, MATTE);
        b.sphere(V3(-0.01, 0.125, 0), V3(0.026, 0.026, 0.026), 1);  // pom-pom
        b.pop();
    }
    b.pop();  // the drop
}

Variant personVariant(const std::string& name, const std::string& blurb, int style, std::vector<Param> extra) {
    std::vector<Param> ps = {paintParam(4, "jacket", "JACKET"), num("swing", "WALK SWING", 0, -1, 1, 0.05),
                             choice("skin", "SKIN", 1, {"PALE", "MEDIUM", "TAN", "DARK"}), choice("hair", "HAIR", 1, {"BLACK", "BROWN", "BLONDE", "GINGER", "GREY"})};
    for (const Param& e : extra) ps.push_back(e);
    return Variant{name, blurb, ps, [style](B& b, const P& p) { person(b, p, style); }};
}

Reg personReg({"person", "PERSON", "NATURE AND PEOPLE", "The player on foot (third person) and other players; swing is the walk cycle", 2.2,
               {
                   personVariant("TRAIL RANGER", "Jacket, cap, daypack and khaki trousers", RANGER, {choice("pack", "DAYPACK", 1, {"NONE", "DAYPACK"})}),
                   personVariant("MECHANIC", "Coveralls, work boots, a rag in the pocket", MECHANIC, {choice("cap", "CAP", 0, {"BARE HEAD", "CAP"})}),
                   personVariant("OFF-ROADER", "Hoodie, beanie, jeans and sneakers", OFFROADER, {paintParam(5, "beanie", "BEANIE")}),
               }});

// @@END
}  // namespace
