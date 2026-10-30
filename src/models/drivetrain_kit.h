// Shared helpers for the drivetrain part models (src/models/engines_*.cpp, gearboxes.cpp, parts.cpp): turned and
// bolted bits, belts, the engine paints and grime, the stand / skid / pulled presentations, gearbox finishes, bell
// housings and U-joint yokes. Header-only (inline), in the model kit's frame (modelkit.h).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../modelkit.h"

namespace dk {
using namespace mk;

inline constexpr double PI = 3.14159265358979;

// A part fitted in a car (carparts.cpp sets "bare"): no stand, skid or timber under it, and built about
// its own axis at the origin.
inline bool bare(const P& p) { return p("bare") > 0.5; }

// ---- Shared helpers -----------------------------------------------------------------------

// A cylinder along X / Z.
inline void xcyl(B& b, double x0, double x1, double y, double z, double r, int segs = 12, bool caps = true) {
    b.cyl(V3(x0, y, z), V3(x1, y, z), r, segs, caps);
}
inline void zcyl(B& b, double x, double y, double z0, double z1, double r, int segs = 12, bool caps = true) {
    b.cyl(V3(x, y, z0), V3(x, y, z1), r, segs, caps);
}
// A lathe turned about the X axis: `prof` is (radius, distance along +X) from `at`.
inline void xlathe(B& b, const V3& at, const std::vector<V2>& prof, int segs = 14) {
    b.push();
    b.move(at);
    b.pitch(-90);
    b.lathe(V3(), prof, segs);
    b.pop();
}
// A lathe turned about the Z axis (profile height runs along +Z).
inline void zlathe(B& b, const V3& at, const std::vector<V2>& prof, int segs = 14) {
    b.push();
    b.move(at);
    b.roll(90);
    b.lathe(V3(), prof, segs);
    b.pop();
}
// A bolt head / nut seen along `axis` (0 x, 1 y, 2 z): a small flat box.
inline void nut(B& b, const V3& c, int axis, double s = 0.016, double t = 0.008) {
    b.box(c, axis == 0 ? V3(t, s, s) : axis == 1 ? V3(s, t, s) : V3(s, s, t));
}
// A ring of bolt heads round the X axis, standing on the plane x = c.x.
inline void boltRingX(B& b, const V3& c, double R, int n, double s = 0.016, double t = 0.008, double a0 = 0) {
    for (int i = 0; i < n; i++) {
        const double a = a0 + 2 * PI * i / n;
        nut(b, c + V3(0, R * std::cos(a), R * std::sin(a)), 0, s, t);
    }
}
// A V-groove pulley on the X axis, from x = c.x forward by w.
inline void pulleyX(B& b, const V3& c, double r, double w, int segs = 12) {
    xlathe(b, c, {V2(0, 0), V2(r, 0), V2(r * 0.84, w * 0.5), V2(r, w), V2(0, w)}, segs);
}
// A rusty patch: small dark-orange boxes scattered over a region (local frame).
inline void rustSpots(B& b, uint32_t seed, const V3& c, const V3& size, int n, int faceAxis, double face) {
    b.style(V3(0.36, 0.17, 0.07), MATTE);
    for (int i = 0; i < n; i++) {
        V3 p = c + V3((hash01(seed, i * 3) - 0.5) * size.x, (hash01(seed, i * 3 + 1) - 0.5) * size.y, (hash01(seed, i * 3 + 2) - 0.5) * size.z);
        const double s = 0.012 + 0.03 * hash01(seed, i + 77);
        if (faceAxis == 0) b.box(V3(face, p.y, p.z), V3(0.004, s, s * 1.3));
        else if (faceAxis == 1) b.box(V3(p.x, face, p.z), V3(s * 1.3, 0.004, s));
        else b.box(V3(p.x, p.y, face), V3(s * 1.3, s, 0.004));
    }
}

struct Circ {
    double z, y, r;
};
// A drive belt round pulleys seen along X: a flat band over the convex hull of the circles.
inline void beltX(B& b, double x, const std::vector<Circ>& cs, double w, const V3& col) {
    struct Pt {
        double u, v;
    };
    std::vector<Pt> pts;
    for (const Circ& c : cs)
        for (int i = 0; i < 8; i++) {
            const double a = 2 * PI * i / 8;
            pts.push_back({c.z + (c.r + 0.004) * std::cos(a), c.y + (c.r + 0.004) * std::sin(a)});
        }
    std::sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b2) { return a.u < b2.u || (a.u == b2.u && a.v < b2.v); });
    auto cr = [](const Pt& o, const Pt& a, const Pt& b2) { return (a.u - o.u) * (b2.v - o.v) - (a.v - o.v) * (b2.u - o.u); };
    std::vector<Pt> h(pts.size() * 2);
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
    b.style(col, MATTE);
    for (size_t i = 0; i < h.size(); i++) {
        const Pt &p = h[i], &q = h[(i + 1) % h.size()];
        const double du = q.u - p.u, dv = q.v - p.v;
        const double l = std::hypot(du, dv), nu = dv / l, nv = -du / l, t = 0.006;  // outward normal in (z, y)
        const V3 po(x, p.v + nv * t, p.u + nu * t), qo(x, q.v + nv * t, q.u + nu * t), pi(x, p.v, p.u), qi(x, q.v, q.u);
        const V3 dx(w / 2, 0, 0);
        b.quad(po - dx, qo - dx, qo + dx, po + dx);
        b.quad(pi + dx, qi + dx, qi - dx, pi - dx);
        b.quad(po + dx, qo + dx, qi + dx, pi + dx);
        b.quad(pi - dx, qi - dx, qo - dx, po - dx);
    }
}

// ---- Engines ------------------------------------------------------------------------------

inline const std::vector<std::string>& enginePaintNames() {
    static const std::vector<std::string> n = {"ORANGE", "BLUE", "BLACK", "SILVER", "RED"};
    return n;
}
inline V3 enginePaint(int i) {
    static const V3 c[] = {V3(0.86, 0.35, 0.05), V3(0.09, 0.22, 0.56), V3(0.07, 0.07, 0.08), V3(0.62, 0.63, 0.66), V3(0.62, 0.08, 0.06)};
    return c[((i % 5) + 5) % 5];
}
inline Param enginePaintParam(int def = 0) { return choice("paint", "PAINT", def, enginePaintNames()); }

struct Eng {
    V3 paint;
    bool aircleaner = true, chrome = true;
    double wear = 0;  // 0 new .. 1 filthy
    uint32_t seed = 1;
};
inline V3 grime(const Eng& e, const V3& c, double k = 1) { return mix(c, V3(0.12, 0.095, 0.07), std::min(1.0, e.wear * k)); }


// ---- Presentation: on an engine stand / strapped to a skid / bare on the ground ---------------

using EngineFn = std::function<void()>;

// How a loose engine is shown (the shop's turntable, a supply cache): on a rolling stand, strapped to a shipping skid, or
// pulled and sitting on scrap timber. (Ignored when "bare": fitted in a car.)
enum Mount { MOUNT_STAND, MOUNT_SKID, MOUNT_PULLED };
inline Param mountParam(int def = MOUNT_STAND) { return choice("mount", "MOUNT", def, {"STAND", "SKID", "PULLED"}); }
inline Param wearParam(double def = 0) { return num("wear", "GREASE", def, 0, 1, 0.05); }

inline void engineStand(B& b, double H, double rearX, const V3& col, const EngineFn& eng) {
    b.push();
    b.move(0, H, 0);
    eng();
    b.pop();
    const double mx = rearX - 0.11;
    b.style(col, PAINT);
    b.box(V3(mx, 0.14, 0), V3(0.07, 0.07, 0.74));
    for (int s = -1; s <= 1; s += 2) b.box(V3(mx + 0.41, 0.14, s * 0.34), V3(0.84, 0.06, 0.06));
    b.box(V3(mx + 0.82, 0.14, 0), V3(0.06, 0.06, 0.74));
    const double top = H + 0.11;
    b.box(V3(mx, (top + 0.14) / 2, 0), V3(0.075, top - 0.14, 0.1), 0.012);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(mx, 0.5, 0.02 * s), V3(mx + 0.3, 0.16, s * 0.33), 0.04, 0.03, V3(0, 1, 0));
    // Head plate and rotating hub.
    b.style(col * 0.9, PAINT);
    b.box(V3(rearX - 0.025, H, 0), V3(0.03, 0.38, 0.42), 0.014);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sz = -1; sz <= 1; sz += 2) nut(b, V3(rearX - 0.044, H + sy * 0.155, sz * 0.17), 0, 0.02, 0.01);
    xcyl(b, mx + 0.03, rearX - 0.04, H, 0, 0.055, 12);
    // Hand wheel and crank on the back of the mast.
    b.style(V3(0.1, 0.1, 0.11), METAL);
    b.cyl(V3(mx - 0.037, H, 0), V3(mx - 0.075, H, 0), 0.03, 10);
    b.beam(V3(mx - 0.08, H, 0), V3(mx - 0.08, H + 0.15, 0.1), 0.02, 0.02, V3(1, 0, 0));
    b.sphere(V3(mx - 0.08, H + 0.15, 0.1), V3(0.025, 0.025, 0.025), 1);
    // Casters.
    for (int ix = 0; ix < 2; ix++)
        for (int s = -1; s <= 1; s += 2) {
            const double x = ix ? mx + 0.82 : mx, z = s * 0.34;
            b.style(V3(0.12, 0.12, 0.13), METAL);
            b.style(V3(0.04, 0.04, 0.045), MATTE);
            zcyl(b, x, 0.045, z - 0.02, z + 0.02, 0.0515, 6);
        }
}

inline void engineSkid(B& b, const V3& strapCol, const EngineFn& eng, double strapX, const std::vector<V2>& strap, double length = 0.85) {
    const double railH = 0.105, footDrop = 0.178;
    const double H = railH + footDrop;
    b.push();
    b.move(0, H, 0);
    eng();
    // Mounting feet bolting the block to the rails.
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) {
            b.box(V3(sx * 0.12, -0.09, s * 0.2), V3(0.1, 0.12, 0.07));
            b.box(V3(sx * 0.12, -0.17, s * 0.2), V3(0.12, 0.016, 0.09));
        }
    // The strap: (z, y) points from the rail up over the engine, mirrored; a ratchet buckle on one side.
    b.style(strapCol, MATTE);
    for (int s = -1; s <= 1; s += 2)
        for (size_t i = 0; i + 1 < strap.size(); i++) {
            const V3 p(strapX, strap[i].y, s * strap[i].x), q(strapX, strap[i + 1].y, s * strap[i + 1].x);
            const V3 d = q - p;
            b.beam(p, q, 0.045, 0.006, V3(0, -d.z, d.y) * -s);
        }
    const V2 top = strap.back();
    b.beam(V3(strapX, top.y, top.x), V3(strapX, top.y, -top.x), 0.045, 0.006, V3(0, 1, 0));
    b.style(V3(0.6, 0.6, 0.62), METAL);
    b.box(V3(strapX, strap[1].y, strap[1].x + 0.012), V3(0.05, 0.07, 0.03), 0.006);  // ratchet buckle
    b.pop();
    // The skid: two rails and boards under them.
    b.style(V3(0.52, 0.40, 0.25), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.03 + (railH - 0.03) / 2, s * 0.2), V3(length, railH - 0.03, 0.09), 0.006);
    b.style(V3(0.46, 0.35, 0.21), WOOD);
    for (int i = -1; i <= 1; i++) b.box(V3(i * (length / 2 - 0.05), 0.015, 0), V3(0.1, 0.03, 0.58));
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int i = -1; i <= 1; i += 2)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(i * 0.28, railH + 0.001, s * 0.2), 1, 0.026, 0.004);
}

inline void engineGround(B& b, const EngineFn& eng, double length = 0.8) {
    const double railH = 0.105, footDrop = 0.178;
    b.push();
    b.move(0, railH + footDrop, 0);
    eng();
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) {
            b.box(V3(sx * 0.12, -0.09, s * 0.2), V3(0.1, 0.12, 0.07));
            b.box(V3(sx * 0.12, -0.17, s * 0.2), V3(0.12, 0.016, 0.09));
        }
    b.pop();
    b.style(V3(0.25, 0.2, 0.15), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0.02 * s, railH / 2, s * 0.2), V3(length, railH, 0.09), 0.006);
    // The oil stain it leaks.
    b.style(V3(0.12, 0.1, 0.075), MATTE);
    b.cyl(V3(0.05, 0.002, 0.0), V3(0.05, 0.004, 0.0), 0.27, 12);
    b.cyl(V3(-0.1, 0.002, 0.17), V3(-0.1, 0.0035, 0.17), 0.14, 10);
}

// The flange and flywheel at the back of a block (axis at the origin, face at x = fx).
inline void rearFace(B& b, const Eng& e, double fx, double cy, double w, double h, double flyR, const V3& blockCol, Mat blockMat) {
    const V3 sc = b.curColor();
    const Mat sm = b.curMat();
    b.style(blockCol, blockMat);
    b.box(V3(fx + 0.015, cy, 0), V3(0.03, h, w), 0.012);
    b.style(V3(0.2, 0.2, 0.21), METAL);
    for (int i = 0; i < 6; i++) {
        const double a = 2 * PI * (i + 0.5) / 6;
        nut(b, V3(fx - 0.001, cy + (h / 2 - 0.02) * std::cos(a), (w / 2 - 0.02) * std::sin(a)), 0, 0.018, 0.008);
    }
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
    xlathe(b, V3(fx - 0.033, 0, 0), {V2(0, 0), V2(flyR, 0), V2(flyR, 0.022), V2(flyR * 0.33, 0.033), V2(0, 0.033)}, 12);
    b.style(V3(0.05, 0.05, 0.055), METAL);
    boltRingX(b, V3(fx - 0.035, 0, 0), flyR * 0.5, 4, 0.018, 0.006, 0.78);
    b.style(sc, sm);
}

// A pressed oil pan seen from the side: (front x, rail y, bottom y, sump).
inline void oilPan(B& b, const V3& col, Mat m, double xf, double xr, double rail, double front, double bottom, double width) {
    b.style(col, m);
    b.extrude({V2(xf, rail), V2(xf, front), V2(xf - 0.14, front - 0.005), V2(xf - 0.18, bottom), V2(xr + 0.04, bottom), V2(xr, bottom + 0.05), V2(xr, rail)}, width);
}

inline Eng optsFor(const P& p, double wear = 0) {
    Eng e;
    e.paint = enginePaint(p.i("paint"));
    e.wear = wear;
    e.seed = (uint32_t)std::max(1, p.i("seed"));
    return e;
}

// ---- Gearboxes: manual, automatic, transfer case ---------------------------------------------

inline const std::vector<std::string>& gbFinishNames() {
    static const std::vector<std::string> n = {"ALUMINIUM", "RAW IRON", "BLACK", "ORANGE", "BLUE", "RED"};
    return n;
}
inline V3 gbFinish(int i, Mat& m) {
    switch (((i % 6) + 6) % 6) {
        case 0: m = METAL; return V3(0.62, 0.63, 0.66);
        case 1: m = METAL; return V3(0.36, 0.36, 0.38);
        case 2: m = PAINT; return V3(0.07, 0.07, 0.08);
        case 3: m = PAINT; return enginePaint(0);
        case 4: m = PAINT; return enginePaint(1);
        default: m = PAINT; return enginePaint(4);
    }
}
inline Param gbFinishParam(int def = 0) { return choice("finish", "FINISH", def, gbFinishNames()); }

// Two timber rails it rests on (along X).
inline void gbRails(B& b, double x0, double x1, double h, double zs = 0.1) {
    b.style(V3(0.5, 0.38, 0.23), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3((x0 + x1) / 2, h / 2, s * zs), V3(x1 - x0, h, 0.08), 0.006);
}

// A cast bellhousing about the X axis: neck at x0, flange at x0 + len, a hollow cone between.
inline void bellHousing(B& b, double x0, double len, double R, double rn, int bolts) {
    xlathe(b, V3(x0, 0, 0), {V2(rn - 0.035, 0), V2(rn, 0), V2(R - 0.03, len * 0.62), V2(R, len * 0.78), V2(R, len), V2(R - 0.045, len), V2(R - 0.07, len * 0.7), V2(rn - 0.035, 0)}, 16);
    const V3 sc = b.curColor();
    const Mat sm = b.curMat();
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingX(b, V3(x0 + len + 0.002, 0, 0), R - 0.023, bolts, 0.02, 0.008, 0.2);
    b.style(sc, sm);
}

// A universal-joint yoke: collar and two ears, on the X axis, reaching from x0 in direction dir.
inline void yoke(B& b, double x0, double y, double z, double dir, double r) {
    const V3 sc = b.curColor();
    const Mat sm = b.curMat();
    b.style(V3(0.22, 0.22, 0.23), METAL);
    xlathe(b, V3(x0, y, z), {V2(0, 0), V2(r * 0.9, 0), V2(r, dir * 0.012), V2(r, dir * 0.03), V2(r * 0.8, dir * 0.04), V2(0, dir * 0.04)}, 10);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(x0 + dir * 0.062, y, z + s * (r * 0.8)), V3(0.05, r * 2.0, 0.012));
        b.style(V3(0.12, 0.12, 0.13), METAL);
        zcyl(b, x0 + dir * 0.068, y, z + s * (r * 0.8 + 0.006), z + s * (r * 0.8 + 0.012), 0.014, 8);
        b.style(V3(0.22, 0.22, 0.23), METAL);
    }
    b.style(sc, sm);
}

// A short shift lever on a turned boss (at local x, y): rises, then leans, with a ball knob.
inline void shortLever(B& b, double x, double y, double z, const V3& col, double lean) {
    b.style(col, METAL);
    b.cyl(V3(x, y, z), V3(x, y + 0.05, z), 0.028, 10);
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    b.tube({V3(x, y + 0.04, z), V3(x, y + 0.12, z), V3(x + lean, y + 0.17, z + lean * 0.4)}, 0.01, 6);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.sphere(V3(x + lean, y + 0.185, z + lean * 0.4), V3(0.022, 0.022, 0.022), 1);
}

}  // namespace dk
