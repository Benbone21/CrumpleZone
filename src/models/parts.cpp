// Parts: crate engines, gearboxes, driveline, suspension and the career supply crates.
// (Career mode upgrade parts: station shop, supply caches, rewards.)
#include <algorithm>
#include <cmath>
#include <functional>

#include "../modelkit.h"

using namespace mk;

namespace {

const double PI = 3.14159265358979;

// ---- Shared helpers -----------------------------------------------------------------------

// A cylinder along X / Z.
void xcyl(B& b, double x0, double x1, double y, double z, double r, int segs = 12, bool caps = true) {
    b.cyl(V3(x0, y, z), V3(x1, y, z), r, segs, caps);
}
void zcyl(B& b, double x, double y, double z0, double z1, double r, int segs = 12, bool caps = true) {
    b.cyl(V3(x, y, z0), V3(x, y, z1), r, segs, caps);
}
// A lathe turned about the X axis: `prof` is (radius, distance along +X) from `at`.
void xlathe(B& b, const V3& at, const std::vector<V2>& prof, int segs = 14) {
    b.push();
    b.move(at);
    b.pitch(-90);
    b.lathe(V3(), prof, segs);
    b.pop();
}
// A lathe turned about the Z axis (profile height runs along +Z).
void zlathe(B& b, const V3& at, const std::vector<V2>& prof, int segs = 14) {
    b.push();
    b.move(at);
    b.roll(90);
    b.lathe(V3(), prof, segs);
    b.pop();
}
// A bolt head / nut seen along `axis` (0 x, 1 y, 2 z): a small flat box.
void nut(B& b, const V3& c, int axis, double s = 0.016, double t = 0.008) {
    b.box(c, axis == 0 ? V3(t, s, s) : axis == 1 ? V3(s, t, s) : V3(s, s, t));
}
// A ring of bolt heads round the X axis, standing on the plane x = c.x.
void boltRingX(B& b, const V3& c, double R, int n, double s = 0.016, double t = 0.008, double a0 = 0) {
    for (int i = 0; i < n; i++) {
        const double a = a0 + 2 * PI * i / n;
        nut(b, c + V3(0, R * std::cos(a), R * std::sin(a)), 0, s, t);
    }
}
// A V-groove pulley on the X axis, from x = c.x forward by w.
void pulleyX(B& b, const V3& c, double r, double w, int segs = 12) {
    xlathe(b, c, {V2(0, 0), V2(r, 0), V2(r * 0.84, w * 0.5), V2(r, w), V2(0, w)}, segs);
}
// A rusty patch: small dark-orange boxes scattered over a region (local frame).
void rustSpots(B& b, uint32_t seed, const V3& c, const V3& size, int n, int faceAxis, double face) {
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
void beltX(B& b, double x, const std::vector<Circ>& cs, double w, const V3& col) {
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

const std::vector<std::string>& enginePaintNames() {
    static const std::vector<std::string> n = {"ORANGE", "BLUE", "BLACK", "SILVER", "RED"};
    return n;
}
V3 enginePaint(int i) {
    static const V3 c[] = {V3(0.86, 0.35, 0.05), V3(0.09, 0.22, 0.56), V3(0.07, 0.07, 0.08), V3(0.62, 0.63, 0.66), V3(0.62, 0.08, 0.06)};
    return c[((i % 5) + 5) % 5];
}
Param enginePaintParam(int def = 0) { return choice("paint", "PAINT", def, enginePaintNames()); }

struct Eng {
    V3 paint;
    bool aircleaner = true, chrome = true;
    double wear = 0;  // 0 new .. 1 filthy
    uint32_t seed = 1;
};
V3 grime(const Eng& e, const V3& c, double k = 1) { return mix(c, V3(0.12, 0.095, 0.07), std::min(1.0, e.wear * k)); }

// The chevy-style V8: crank axis on the origin, +X front (pulleys), -X the bellhousing face.
// The pan bottom is 0.28 below the axis.
void v8(B& b, const Eng& e) {
    const V3 body = grime(e, e.paint, 0.8), dark = grime(e, V3(0.07, 0.07, 0.075), 0.3), iron = grime(e, V3(0.24, 0.23, 0.23), 0.6);
    const V3 alu = grime(e, V3(0.62, 0.63, 0.66), 0.9), black = V3(0.05, 0.05, 0.055);
    const V3 coverCol = e.chrome ? V3(0.82, 0.83, 0.85) : body;
    const Mat coverMat = e.chrome ? CHROME : PAINT;

    // Crankcase and the rear (bellhousing) face with the flywheel.
    b.style(body, PAINT);
    b.box(V3(0, -0.02, 0), V3(0.52, 0.28, 0.30), 0.02);
    b.box(V3(-0.285, -0.03, 0), V3(0.03, 0.34, 0.36));
    b.style(iron, METAL);
    for (int i = 0; i < 4; i++) {
        const double a = 2 * PI * (i + 0.5) / 4;
        nut(b, V3(-0.304, -0.03 + 0.15 * std::cos(a), 0.165 * std::sin(a)), 0, 0.022, 0.008);
    }
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
    xlathe(b, V3(-0.335, 0, 0), {V2(0, 0), V2(0.15, 0), V2(0.15, 0.022), V2(0.05, 0.033), V2(0, 0.033)}, 10);
    b.style(black, METAL);
    xcyl(b, -0.345, -0.335, 0, 0, 0.032, 8);

    // Oil pan: shallow at the front, deep sump at the rear; rail flange with bolts.
    b.style(dark, PAINT);
    b.push();
    b.extrude({V2(0.25, -0.145), V2(0.25, -0.20), V2(0.10, -0.205), V2(0.06, -0.278), V2(-0.20, -0.278), V2(-0.245, -0.22), V2(-0.245, -0.145)}, 0.27);
    b.pop();
    b.style(iron, METAL);
    b.box(V3(0.0, -0.147, 0), V3(0.52, 0.012, 0.31));
    for (int i = 0; i < 3; i++)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(-0.1 + 0.1 * i, -0.139, s * 0.147), 1, 0.018, 0.008);
    b.box(V3(-0.1, -0.282, 0), V3(0.025, 0.006, 0.025));  // drain plug

    // The two banks, 90 degrees apart.
    int side = 0;
    b.both([&] {
        const bool first = side++ == 0;
        b.push();
        b.roll(45);
        b.style(body, PAINT);
        b.box(V3(0, 0.165, 0), V3(0.50, 0.19, 0.17));   // cylinder deck
        b.box(V3(0, 0.318, 0), V3(0.465, 0.11, 0.155), 0.012);  // head
        b.style(dark, MATTE);
        b.box(V3(0, 0.262, 0), V3(0.505, 0.010, 0.175));           // gasket line
        b.box(V3(0, 0.381, 0), V3(0.43, 0.012, 0.125));            // cover rail
        b.style(coverCol, coverMat);
        b.box(V3(0, 0.417, 0), V3(0.40, 0.06, 0.105), 0.022);   // valve cover
        b.style(e.chrome ? V3(0.72, 0.73, 0.76) : body * 0.88, e.chrome ? CHROME : PAINT);
        b.box(V3(0, 0.451, 0), V3(0.30, 0.012, 0.05));   // pressed centre panel
        b.style(black, MATTE);
        if (first) b.cyl(V3(0.09, 0.445, 0), V3(0.09, 0.468, 0), 0.02, 6);  // oil filler cap
        // Plug boots and a wire loom.
        b.style(black, MATTE);
        for (int i = 0; i < 2; i++) b.box(V3(-0.1 + 0.2 * i, 0.335, 0.094), V3(0.09, 0.034, 0.022));
        // Cast exhaust manifold: four port stubs, a long body, the dump flange at the rear.
        b.style(iron, METAL);
        for (int i = 0; i < 2; i++) b.box(V3(-0.1 + 0.2 * i, 0.285, 0.105), V3(0.11, 0.075, 0.06));
        b.box(V3(0, 0.285, 0.14), V3(0.40, 0.075, 0.06), 0.022);
        b.cyl(V3(-0.19, 0.285, 0.14), V3(-0.262, 0.285, 0.14), 0.034, 8);
        b.box(V3(-0.268, 0.285, 0.14), V3(0.014, 0.088, 0.088));
        if (e.wear > 0.1) rustSpots(b, e.seed + (first ? 0 : 50), V3(0, 0.285, 0.172), V3(0.38, 0.05, 0.0), (int)(3 + 4 * e.wear), 2, 0.172);
        b.pop();
    });
    // Brand name on each valve cover (read from outside; built per side so it is not mirrored).
    b.style(V3(0.08, 0.08, 0.09), MATTE);
    for (int sd = -1; sd <= 1; sd += 2) {
        b.push();
        b.roll(sd * 45);
        b.move(0, 0.457, 0);
        b.yaw(-sd * 90);
        b.pitch(90);
        b.letters("BRUTE", V3(0, 0, 0), 0.0065, 0.001);
        b.pop();
    }

    // Intake manifold in the valley, with the four-barrel carb.
    b.style(alu, METAL);
    b.box(V3(0, 0.225, 0), V3(0.40, 0.12, 0.23), 0.035);
    b.box(V3(0, 0.292, 0), V3(0.20, 0.025, 0.20));
    b.style(grime(e, V3(0.74, 0.62, 0.30), 0.6), METAL);
    b.box(V3(0, 0.311, 0), V3(0.15, 0.01, 0.15), 0.003);
    b.box(V3(0, 0.343, 0), V3(0.13, 0.054, 0.13), 0.014);
    b.box(V3(0.095, 0.335, 0), V3(0.045, 0.036, 0.07));   // fuel bowls
    b.box(V3(-0.095, 0.335, 0), V3(0.045, 0.036, 0.07));
    b.style(black, MATTE);
    if (e.aircleaner) {
        b.style(e.chrome ? V3(0.82, 0.83, 0.85) : grime(e, V3(0.55, 0.56, 0.58), 0.8), e.chrome ? CHROME : METAL);
        b.lathe(V3(0, 0.372, 0), {V2(0, 0), V2(0.13, 0), V2(0.145, 0.006), V2(0.145, 0.032), V2(0.13, 0.044), V2(0, 0.05)}, 10);
        b.style(black, METAL);
        b.cyl(V3(0, 0.42, 0), V3(0, 0.445, 0), 0.009, 6);
        b.box(V3(0, 0.447, 0), V3(0.04, 0.012, 0.014));
    } else {
        b.style(V3(0.02, 0.02, 0.02), MATTE);
        for (int i = 0; i < 4; i++) b.cyl(V3((i & 1 ? 1 : -1) * 0.03, 0.369, (i & 2 ? 1 : -1) * 0.03), V3((i & 1 ? 1 : -1) * 0.03, 0.372, (i & 2 ? 1 : -1) * 0.03), 0.02, 8);
    }
    // Distributor at the back of the manifold, the water neck at the front.
    b.style(black, MATTE);
    b.cyl(V3(-0.205, 0.27, 0), V3(-0.205, 0.335, 0), 0.034, 6, false);
    b.cyl(V3(-0.205, 0.335, 0), V3(-0.205, 0.372, 0), 0.046, 10);
    b.style(iron, METAL);
    b.cone(V3(0.17, 0.285, 0), V3(0.24, 0.31, 0), 0.036, 0.027, 8);
    b.style(black, MATTE);
    b.tube({V3(0.24, 0.31, 0), V3(0.31, 0.335, 0.02), V3(0.36, 0.34, 0.075)}, 0.021, 6);

    // The front: timing cover, balancer, water pump and the accessory belt.
    b.style(alu, METAL);
    b.box(V3(0.283, -0.01, 0), V3(0.045, 0.30, 0.27), 0.015);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    xlathe(b, V3(0.305, 0, 0), {V2(0, 0), V2(0.078, 0), V2(0.07, 0.03), V2(0, 0.03)}, 10);
    pulleyX(b, V3(0.335, 0, 0), 0.056, 0.026, 8);
    b.style(black, METAL);
    nut(b, V3(0.364, 0, 0), 0, 0.02, 0.008);
    b.style(alu, METAL);
    xlathe(b, V3(0.305, 0.17, 0), {V2(0, 0), V2(0.075, 0), V2(0.075, 0.012), V2(0.05, 0.03), V2(0, 0.03)}, 10);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    pulleyX(b, V3(0.335, 0.17, 0), 0.052, 0.024, 8);
    // Alternator on the right front.
    b.style(alu, METAL);
    xcyl(b, 0.10, 0.31, 0.0, -0.205, 0.05, 10);
    b.style(black, MATTE);
    xcyl(b, 0.31, 0.335, 0.0, -0.205, 0.032, 8);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    pulleyX(b, V3(0.335, 0, -0.205), 0.042, 0.022, 6);
    beltX(b, 0.346, {{0, 0, 0.056}, {0, 0.17, 0.052}, {-0.205, 0.0, 0.042}}, 0.02, V3(0.04, 0.04, 0.04));

    // Starter (right, low at the rear) and oil filter (left).
    b.style(grime(e, V3(0.3, 0.3, 0.31), 0.7), METAL);
    xcyl(b, -0.31, -0.12, -0.085, -0.19, 0.048, 8);
    b.style(e.chrome ? enginePaint(1) : V3(0.1, 0.22, 0.5), PAINT);
    zcyl(b, 0.1, -0.1, 0.15, 0.255, 0.046, 8);
    if (e.wear > 0.1) {
        // Dangling cut wiring on the front.
        b.style(V3(0.05, 0.05, 0.05), MATTE);
        b.tube({V3(0.30, 0.28, -0.1), V3(0.34, 0.2, -0.23), V3(0.31, 0.05, -0.27)}, 0.01, 4);
        b.tube({V3(0.30, 0.28, -0.11), V3(0.37, 0.22, -0.2), V3(0.35, 0.0, -0.22)}, 0.008, 4);
    }
}

// ---- Presentation: on an engine stand / strapped to a skid / bare on the ground ---------------

using EngineFn = std::function<void()>;

void engineStand(B& b, double H, double rearX, const V3& col, const EngineFn& eng) {
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

void engineSkid(B& b, const V3& strapCol, const EngineFn& eng, double strapX, const std::vector<V2>& strap, double length = 0.85) {
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

void engineGround(B& b, const EngineFn& eng, double length = 0.8) {
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

// ---- crate_engine_v8 -------------------------------------------------------------------------

Eng v8Opts(const P& p, double wear = 0) {
    Eng e;
    e.paint = enginePaint(p.i("paint"));
    e.aircleaner = p.i("aircleaner") != 0;
    e.chrome = p.i("chrome") != 0;
    e.wear = wear;
    return e;
}

void v8Stand(B& b, const P& p) {
    const Eng e = v8Opts(p);
    engineStand(b, 0.5, -0.335, V3(0.2, 0.22, 0.26), [&] { v8(b, e); });
}
void v8Skid(B& b, const P& p) {
    const Eng e = v8Opts(p);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { v8(b, e); }, 0.263, {V2(0.27, -0.14), V2(0.335, 0.0), V2(0.32, 0.14), V2(0.22, 0.30), V2(0.07, 0.345)});
}
void v8Junk(B& b, const P& p) {
    Eng e = v8Opts(p, p("wear"));
    e.seed = (uint32_t)p.i("seed");
    engineGround(b, [&] { v8(b, e); });
}

Reg crateEngineV8({"crate_engine_v8", "CRATE ENGINE V8", "PARTS", "Career upgrade: a big V8 crate engine (shop, supply cache, reward)", 0.9,
                   {
                       {"ON A STAND", "Show-finish V8 bolted to a rolling engine stand",
                        {enginePaintParam(0), choice("aircleaner", "AIR CLEANER", 1, {"OFF", "ON"}), choice("chrome", "CHROME COVERS", 1, {"OFF", "ON"})}, v8Stand},
                       {"ON A SKID", "Fresh crate engine strapped to a shipping skid",
                        {enginePaintParam(0), choice("aircleaner", "AIR CLEANER", 1, {"OFF", "ON"}), choice("chrome", "CHROME COVERS", 1, {"OFF", "ON"})}, v8Skid},
                       {"JUNKYARD PULL", "Greasy, rusty pull sitting on scrap timber",
                        {enginePaintParam(0), num("wear", "GREASE", 0.65, 0, 1, 0.05), choice("aircleaner", "AIR CLEANER", 0, {"OFF", "ON"}),
                         choice("chrome", "CHROME COVERS", 0, {"OFF", "ON"}), seedParam()},
                        v8Junk},
                   }});

// ---- crate_engine_v6 -------------------------------------------------------------------------

// The flange and flywheel at the back of a block (axis at the origin, face at x = fx).
void rearFace(B& b, const Eng& e, double fx, double cy, double w, double h, double flyR, const V3& blockCol, Mat blockMat) {
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
void oilPan(B& b, const V3& col, Mat m, double xf, double xr, double rail, double front, double bottom, double width) {
    b.style(col, m);
    b.extrude({V2(xf, rail), V2(xf, front), V2(xf - 0.14, front - 0.005), V2(xf - 0.18, bottom), V2(xr + 0.04, bottom), V2(xr, bottom + 0.05), V2(xr, rail)}, width);
}

void v6(B& b, const Eng& e) {
    const V3 blockAlu = grime(e, V3(0.52, 0.53, 0.56), 0.9), alu = grime(e, V3(0.62, 0.63, 0.66), 0.9);
    const V3 black = V3(0.05, 0.05, 0.055), cover = grime(e, e.paint, 0.55), camCol = grime(e, V3(0.3, 0.3, 0.32), 0.4);
    const V3 steel = grime(e, V3(0.36, 0.34, 0.33), 0.6);
    b.style(blockAlu, METAL);
    b.box(V3(0, -0.02, 0), V3(0.46, 0.26, 0.26), 0.02);
    rearFace(b, e, -0.26, -0.03, 0.32, 0.30, 0.13, blockAlu, METAL);
    for (int i = 0; i < 4; i++)
        for (int sd = -1; sd <= 1; sd += 2) b.box(V3(-0.15 + 0.1 * i, -0.04, sd * 0.133), V3(0.02, 0.2, 0.012));  // block ribs
    // Pan and its rail.
    oilPan(b, alu, METAL, 0.23, -0.23, -0.14, -0.19, -0.245, 0.25);
    b.style(blockAlu, METAL);
    b.box(V3(0, -0.142, 0), V3(0.47, 0.012, 0.29));
    for (int i = 0; i < 4; i++)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(-0.15 + 0.1 * i, -0.134, s * 0.137), 1, 0.018, 0.008);
    // Banks, 60 degrees apart.
    int side = 0;
    b.both([&] {
        const bool first = side++ == 0;
        b.push();
        b.roll(30);
        b.style(blockAlu, METAL);
        b.box(V3(0, 0.155, 0), V3(0.44, 0.17, 0.15));
        b.style(alu, METAL);
        b.box(V3(0, 0.29, 0), V3(0.42, 0.10, 0.14), 0.01);
        b.style(camCol, PAINT);
        b.box(V3(0, 0.3675, 0), V3(0.40, 0.055, 0.115), 0.018);   // cam cover
        b.style(black, MATTE);
        for (int i = -1; i <= 1; i++) {
            b.box(V3(i * 0.125, 0.425, 0), V3(0.04, 0.06, 0.04));  // coil on plug
            b.box(V3(i * 0.125 + 0.0, 0.43, 0.026), V3(0.022, 0.018, 0.014));  // its connector
        }
        if (first) b.cyl(V3(0.15, 0.395, 0.0), V3(0.15, 0.42, 0.0), 0.018, 8);  // oil cap
        // Stamped manifold and its heat shield.
        b.style(steel, METAL);
        b.box(V3(0, 0.275, 0.115), V3(0.34, 0.08, 0.05), 0.02);
        b.box(V3(-0.13, 0.275, 0.09), V3(0.06, 0.07, 0.05));
        b.box(V3(0.13, 0.275, 0.09), V3(0.06, 0.07, 0.05));
        b.cyl(V3(-0.17, 0.275, 0.115), V3(-0.23, 0.275, 0.115), 0.032, 8);
        b.style(grime(e, V3(0.42, 0.42, 0.44), 0.7), METAL);
        b.box(V3(0.0, 0.29, 0.143), V3(0.30, 0.085, 0.006));
        if (e.wear > 0.1) rustSpots(b, e.seed + (first ? 0 : 50), V3(0, 0.275, 0.148), V3(0.3, 0.05, 0.0), (int)(3 + 6 * e.wear), 2, 0.1475);
        b.pop();
    });
    // The plastic engine cover over the valley (a trapezoid prism), ribs and a chrome V.
    b.style(cover, MATTE);
    b.push();
    b.yaw(90);
    b.extrude({V2(-0.15, 0.30), V2(-0.09, 0.425), V2(0.09, 0.425), V2(0.15, 0.30)}, 0.36);
    b.pop();
    b.style(cover * 0.8, MATTE);
    for (int i = -1; i <= 1; i += 2) b.box(V3(i * 0.11, 0.429, 0), V3(0.012, 0.008, 0.16));
    b.style(V3(0.8, 0.8, 0.83), CHROME);
    b.push();
    b.move(-0.02, 0.4295, 0);
    b.yaw(-90);
    b.pitch(90);
    b.letters("V6", V3(0, 0, 0), 0.02, 0.001);
    b.pop();
    // Air intake tube and the throttle body.
    b.style(black, MATTE);
    b.tube({V3(0.2, 0.40, 0.08), V3(0.27, 0.41, 0.12), V3(0.31, 0.37, 0.19), V3(0.30, 0.30, 0.235)}, 0.03, 8);
    b.style(alu, METAL);
    xcyl(b, 0.15, 0.2, 0.4, 0.08, 0.04, 8);
    // Front: cover, damper, accessories on a serpentine belt.
    b.style(alu, METAL);
    b.box(V3(0.26, -0.01, 0), V3(0.05, 0.28, 0.25), 0.015);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    xlathe(b, V3(0.285, 0, 0), {V2(0, 0), V2(0.07, 0), V2(0.062, 0.03), V2(0, 0.03)}, 12);
    pulleyX(b, V3(0.315, 0, 0), 0.054, 0.026, 10);
    b.style(blockAlu, METAL);
    xcyl(b, 0.285, 0.32, 0.17, 0.17, 0.045, 10);     // alternator
    xcyl(b, 0.285, 0.32, -0.02, -0.165, 0.058, 10);  // A/C compressor
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    pulleyX(b, V3(0.32, 0.17, 0.17), 0.04, 0.022, 8);
    pulleyX(b, V3(0.32, -0.02, -0.165), 0.05, 0.022, 8);
    pulleyX(b, V3(0.32, 0.2, -0.09), 0.03, 0.022, 8);
    beltX(b, 0.331, {{0, 0, 0.054}, {0.17, 0.17, 0.04}, {-0.165, -0.02, 0.05}, {-0.09, 0.2, 0.03}}, 0.018, V3(0.04, 0.04, 0.04));
    // Oil filter, starter.
    b.style(black, PAINT);
    zcyl(b, 0.05, -0.09, 0.13, 0.215, 0.042, 8);
    b.style(grime(e, V3(0.3, 0.3, 0.31), 0.7), METAL);
    xcyl(b, -0.25, -0.1, -0.08, 0.15, 0.045, 8);
}

// ---- crate_engine_i4 -------------------------------------------------------------------------

void i4(B& b, const Eng& e) {
    const V3 blockAlu = grime(e, V3(0.52, 0.53, 0.56), 0.9), alu = grime(e, V3(0.62, 0.63, 0.66), 0.9);
    const V3 black = V3(0.05, 0.05, 0.055), cam = grime(e, e.paint, 0.55);
    const V3 steel = grime(e, V3(0.55, 0.52, 0.5), 0.5);
    b.style(blockAlu, METAL);
    b.box(V3(0, 0.035, 0), V3(0.38, 0.27, 0.22), 0.02);
    rearFace(b, e, -0.23, 0.02, 0.27, 0.28, 0.12, blockAlu, METAL);
    for (int i = 0; i < 3; i++)
        for (int sd = -1; sd <= 1; sd += 2) b.box(V3(-0.12 + 0.12 * i, 0.0, sd * 0.113), V3(0.02, 0.2, 0.012));  // block ribs
    oilPan(b, alu, METAL, 0.2, -0.2, -0.1, -0.15, -0.21, 0.21);
    b.style(blockAlu, METAL);
    b.box(V3(0, -0.102, 0), V3(0.40, 0.012, 0.24));
    for (int i = 0; i < 3; i++)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(-0.11 + 0.11 * i, -0.094, s * 0.113), 1, 0.018, 0.008);
    // Head and the tall DOHC cam cover.
    b.style(alu, METAL);
    b.box(V3(0, 0.22, 0), V3(0.38, 0.10, 0.21), 0.01);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(0, 0.1705, 0), V3(0.385, 0.008, 0.215));   // gasket
    b.style(cam, PAINT);
    b.box(V3(0, 0.31, 0), V3(0.35, 0.085, 0.15), 0.02);
    b.style(V3(0.05, 0.05, 0.055), PAINT);
    b.box(V3(0, 0.366, 0), V3(0.30, 0.03, 0.06), 0.008);   // spark plug well
    b.style(black, MATTE);
    for (int i = 0; i < 4; i++) b.cyl(V3(-0.1125 + 0.075 * i, 0.375, 0), V3(-0.1125 + 0.075 * i, 0.42, 0), 0.017, 6);
    b.style(V3(0.9, 0.9, 0.92), MATTE);
    b.push();
    b.move(0.1755, 0.31, 0);
    b.letters("16V", V3(0, 0, 0), 0.0085, 0.001);   // on the front of the cam cover
    b.pop();
    b.style(V3(0.78, 0.79, 0.82), CHROME);
    b.box(V3(0.05, 0.3525, 0.0), V3(0.12, 0.004, 0.012));   // badge strips
    b.box(V3(0.05, 0.3525, 0.03), V3(0.12, 0.004, 0.012));
    b.box(V3(0.05, 0.3525, -0.03), V3(0.12, 0.004, 0.012));
    b.style(black, MATTE);
    b.cyl(V3(0.14, 0.352, 0.045), V3(0.14, 0.375, 0.045), 0.02, 8);   // oil cap
    // Intake runners curve up from the head on the left to the plenum and throttle body.
    b.style(grime(e, V3(0.2, 0.2, 0.22), 0.3), METAL);
    for (int i = 0; i < 4; i++) {
        const double x = -0.12 + 0.08 * i;
        b.tube({V3(x, 0.215, 0.1), V3(x, 0.21, 0.155), V3(x, 0.255, 0.2), V3(x, 0.325, 0.205), V3(x, 0.375, 0.185)}, 0.02, 6, false, false);
    }
    b.style(blockAlu, METAL);
    xcyl(b, -0.19, 0.2, 0.395, 0.185, 0.042, 10);
    xcyl(b, 0.2, 0.255, 0.395, 0.185, 0.034, 8);
    b.style(black, MATTE);
    b.tube({V3(0.255, 0.395, 0.185), V3(0.30, 0.39, 0.17), V3(0.32, 0.33, 0.12)}, 0.032, 8);
    b.style(V3(0.8, 0.62, 0.15), METAL);
    b.cyl(V3(-0.17, 0.255, 0.14), V3(0.17, 0.255, 0.14), 0.01, 6);   // fuel rail
    // The header on the right: four primaries joining one collector.
    b.style(steel, METAL);
    b.box(V3(0, 0.22, -0.112), V3(0.36, 0.07, 0.014));
    const V3 C(-0.04, 0.02, -0.2);
    for (int i = 0; i < 4; i++) {
        const double x = -0.12 + 0.08 * i;
        b.tube({V3(x, 0.22, -0.118), V3(x, 0.21, -0.16), V3(x * 0.4 + C.x * 0.6, 0.11, -0.2), C}, 0.015, 6, false, false);
    }
    b.cone(C, C + V3(-0.12, -0.03, 0), 0.032, 0.026, 8);
    b.box(C + V3(-0.125, -0.03, 0), V3(0.012, 0.075, 0.075));
    // Front: belt cover, crank pulley, alternator and idler on the belt.
    b.style(black, MATTE);
    b.box(V3(0.215, 0.09, 0), V3(0.04, 0.36, 0.17), 0.012);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    pulleyX(b, V3(0.235, 0, 0), 0.055, 0.03, 10);
    xcyl(b, 0.235, 0.27, 0.0, 0.17, 0.045, 8);
    b.style(blockAlu, METAL);
    xcyl(b, 0.09, 0.235, 0.0, 0.17, 0.044, 10);
    b.style(grime(e, V3(0.14, 0.14, 0.15), 0.4), METAL);
    pulleyX(b, V3(0.27, 0.0, 0.17), 0.04, 0.022, 8);
    pulleyX(b, V3(0.235, 0.2, 0.0), 0.032, 0.022, 8);
    beltX(b, 0.25, {{0, 0, 0.055}, {0.17, 0.0, 0.04}, {0.0, 0.2, 0.032}}, 0.018, V3(0.04, 0.04, 0.04));
    // Oil filter and starter.
    b.style(enginePaint(1), PAINT);
    zcyl(b, 0.09, -0.04, -0.115, -0.2, 0.04, 8);
    b.style(grime(e, V3(0.3, 0.3, 0.31), 0.7), METAL);
    xcyl(b, -0.22, -0.08, -0.07, 0.14, 0.04, 8);
}

Eng optsFor(const P& p, double wear = 0) {
    Eng e;
    e.paint = enginePaint(p.i("paint"));
    e.wear = wear;
    e.seed = (uint32_t)std::max(1, p.i("seed"));
    return e;
}

void v6Stand(B& b, const P& p) {
    const Eng e = optsFor(p);
    engineStand(b, 0.5, -0.30, V3(0.2, 0.22, 0.26), [&] { v6(b, e); });
}
void v6Skid(B& b, const P& p) {
    const Eng e = optsFor(p);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { v6(b, e); }, 0.232, {V2(0.255, -0.14), V2(0.30, 0.0), V2(0.28, 0.14), V2(0.17, 0.30), V2(0.06, 0.37)}, 0.8);
}
void v6Junk(B& b, const P& p) {
    const Eng e = optsFor(p, p("wear"));
    engineGround(b, [&] { v6(b, e); }, 0.75);
}

void i4Stand(B& b, const P& p) {
    const Eng e = optsFor(p);
    engineStand(b, 0.46, -0.27, V3(0.2, 0.22, 0.26), [&] { i4(b, e); });
}
void i4Skid(B& b, const P& p) {
    const Eng e = optsFor(p);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { i4(b, e); }, 0.222, {V2(0.2, -0.14), V2(0.23, 0.0), V2(0.21, 0.16), V2(0.15, 0.31), V2(0.05, 0.4)}, 0.7);
}
void i4Junk(B& b, const P& p) {
    const Eng e = optsFor(p, p("wear"));
    engineGround(b, [&] { i4(b, e); }, 0.65);
}

Reg crateEngineV6({"crate_engine_v6", "CRATE ENGINE V6", "PARTS", "Career upgrade: a modern V6 crate engine with a plastic engine cover", 0.8,
                   {
                       {"ON A STAND", "Plastic-covered V6 bolted to a rolling engine stand", {enginePaintParam(2)}, v6Stand},
                       {"ON A SKID", "Fresh crate V6 strapped to a shipping skid", {enginePaintParam(2)}, v6Skid},
                       {"JUNKYARD PULL", "Dirty used pull sitting on scrap timber", {enginePaintParam(2), num("wear", "GREASE", 0.65, 0, 1, 0.05), seedParam()}, v6Junk},
                   }});

Reg crateEngineI4({"crate_engine_i4", "CRATE ENGINE I4", "PARTS", "Career upgrade: a high-revving DOHC inline-four crate engine", 0.75,
                   {
                       {"ON A STAND", "Red-cammed four on a rolling engine stand", {enginePaintParam(4)}, i4Stand},
                       {"ON A SKID", "Fresh crate four strapped to a shipping skid", {enginePaintParam(4)}, i4Skid},
                       {"JUNKYARD PULL", "Greasy imported pull sitting on scrap timber", {enginePaintParam(4), num("wear", "GREASE", 0.65, 0, 1, 0.05), seedParam()}, i4Junk},
                   }});

// ---- Gearboxes: manual, automatic, transfer case ---------------------------------------------

const std::vector<std::string>& gbFinishNames() {
    static const std::vector<std::string> n = {"ALUMINIUM", "RAW IRON", "BLACK", "ORANGE", "BLUE", "RED"};
    return n;
}
V3 gbFinish(int i, Mat& m) {
    switch (((i % 6) + 6) % 6) {
        case 0: m = METAL; return V3(0.62, 0.63, 0.66);
        case 1: m = METAL; return V3(0.36, 0.36, 0.38);
        case 2: m = PAINT; return V3(0.07, 0.07, 0.08);
        case 3: m = PAINT; return enginePaint(0);
        case 4: m = PAINT; return enginePaint(1);
        default: m = PAINT; return enginePaint(4);
    }
}
Param gbFinishParam(int def = 0) { return choice("finish", "FINISH", def, gbFinishNames()); }

// Two timber rails it rests on (along X).
void gbRails(B& b, double x0, double x1, double h, double zs = 0.1) {
    b.style(V3(0.5, 0.38, 0.23), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3((x0 + x1) / 2, h / 2, s * zs), V3(x1 - x0, h, 0.08), 0.006);
}

// A cast bellhousing about the X axis: neck at x0, flange at x0 + len, a hollow cone between.
void bellHousing(B& b, double x0, double len, double R, double rn, int bolts) {
    xlathe(b, V3(x0, 0, 0), {V2(rn - 0.035, 0), V2(rn, 0), V2(R - 0.03, len * 0.62), V2(R, len * 0.78), V2(R, len), V2(R - 0.045, len), V2(R - 0.07, len * 0.7), V2(rn - 0.035, 0)}, 16);
    const V3 sc = b.curColor();
    const Mat sm = b.curMat();
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingX(b, V3(x0 + len + 0.002, 0, 0), R - 0.023, bolts, 0.02, 0.008, 0.2);
    b.style(sc, sm);
}

// A universal-joint yoke: collar and two ears, on the X axis, reaching from x0 in direction dir.
void yoke(B& b, double x0, double y, double z, double dir, double r) {
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

// -- gearbox_manual ----------------------------------------------------------------------------

void manualAlu(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.33, 0.33, 0.35);
    const double Y0 = 0.245;
    gbRails(b, -0.12, 0.42, 0.07);
    b.style(V3(0.5, 0.38, 0.23), WOOD);
    b.box(V3(0.02, 0.05, 0), V3(0.22, 0.1, 0.18));  // block under the case
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    bellHousing(b, 0.18, 0.22, 0.2, 0.135, 8);
    b.box(V3(0.27, -0.06, -0.185), V3(0.14, 0.14, 0.07), 0.02);   // starter pocket
    b.box(V3(0.02, 0, 0), V3(0.32, 0.25, 0.21), 0.04);              // main case
    for (int s = -1; s <= 1; s += 2)
        for (int i = 0; i < 5; i++) b.box(V3(0.13 - 0.06 * i, -0.01, s * 0.108), V3(0.014, 0.19, 0.012));
    // Side cover plates.
    b.style(steel, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0.04, 0.0, s * 0.113), V3(0.11, 0.13, 0.008), 0.003);
        for (int i = 0; i < 4; i++) nut(b, V3(0.04 + (i & 1 ? 0.045 : -0.045), (i & 2 ? 0.052 : -0.052), s * 0.119), 2, 0.016, 0.006);
    }
    // Top cover.
    b.style(c, fm);
    b.box(V3(-0.03, 0.135, 0), V3(0.15, 0.024, 0.14), 0.008);
    b.style(steel, METAL);
    for (int i = 0; i < 6; i++) nut(b, V3(-0.03 + (i % 3 - 1) * 0.055, 0.149, (i / 3 ? 1 : -1) * 0.052), 1, 0.016, 0.006);
    // Tail housing, ribbed, and the output shaft.
    b.style(c, fm);
    xlathe(b, V3(-0.34, 0, 0), {V2(0, 0), V2(0.055, 0), V2(0.055, 0.03), V2(0.062, 0.04), V2(0.07, 0.1), V2(0.083, 0.115), V2(0.092, 0.115), V2(0.092, 0.127), V2(0.083, 0.127),
                                V2(0.086, 0.16), V2(0.105, 0.2), V2(0, 0.2)}, 14);
    b.style(V3(0.35, 0.35, 0.37), METAL);
    xcyl(b, -0.41, -0.33, 0, 0, 0.022, 8);
    yoke(b, -0.41, 0, 0, -1, 0.03);
    b.style(c, fm);
    zcyl(b, -0.22, 0.03, 0.06, 0.1, 0.018, 8);    // speed sensor boss
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.22, 0.03, 0.1, 0.125, 0.012, 6);
    b.box(V3(-0.24, -0.095, 0), V3(0.09, 0.035, 0.09));   // rubber mount
    b.style(steel, METAL);
    b.box(V3(-0.24, -0.116, 0), V3(0.13, 0.012, 0.11));
    // Clutch release: sleeve and shaft in the bell, fork arm outside.
    b.style(steel, METAL);
    xcyl(b, 0.2, 0.34, 0, 0, 0.038, 10);
    b.style(V3(0.45, 0.45, 0.47), METAL);
    xcyl(b, 0.34, 0.46, 0, 0, 0.014, 6);
    b.box(V3(0.285, -0.12, -0.222), V3(0.016, 0.1, 0.014));
    // Shifter.
    if (p.i("shifter")) {
        b.style(V3(0.05, 0.05, 0.055), MATTE);
        b.cone(V3(-0.03, 0.145, 0), V3(-0.03, 0.2, 0), 0.05, 0.022, 10);
        b.style(V3(0.6, 0.6, 0.63), CHROME);
        b.tube({V3(-0.03, 0.19, 0), V3(-0.03, 0.27, 0), V3(-0.01, 0.34, 0)}, 0.009, 6);
        b.style(V3(0.05, 0.05, 0.055), MATTE);
        b.lathe(V3(-0.01, 0.34, 0), {V2(0, 0), V2(0.02, 0.008), V2(0.027, 0.035), V2(0.02, 0.06), V2(0, 0.066)}, 10);
    } else {
        b.style(steel, METAL);
        b.box(V3(-0.03, 0.15, 0), V3(0.07, 0.01, 0.07));
    }
    b.pop();
}

void manualIron(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32);
    const double Y0 = 0.24;
    gbRails(b, -0.1, 0.4, 0.07);
    b.style(V3(0.5, 0.38, 0.23), WOOD);
    b.box(V3(0.05, 0.05, 0), V3(0.22, 0.1, 0.18));
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    bellHousing(b, 0.16, 0.22, 0.19, 0.125, 8);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0.26, 0.0, s * 0.2), V3(0.04, 0.2, 0.025));   // cast bell lugs
    b.box(V3(0.04, 0, 0), V3(0.30, 0.22, 0.22), 0.025);
    b.box(V3(0.185, 0, 0), V3(0.03, 0.26, 0.26), 0.012);   // case flange to the bell
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingX(b, V3(0.2, 0, 0), 0.115, 6, 0.02, 0.008, 0.52);
    b.style(c, fm);
    for (int s = -1; s <= 1; s += 2) {   // side cover plates (cast, bolted)
        b.box(V3(0.04, 0.0, s * 0.114), V3(0.2, 0.15, 0.012), 0.004);
        b.style(steel, METAL);
        for (int i = 0; i < 6; i++) nut(b, V3(0.04 + (i % 3 - 1) * 0.085, (i / 3 ? 0.058 : -0.058), s * 0.123), 2, 0.016, 0.008);
        b.style(c, fm);
    }
    // Top-loader cover, the shifter housing and the stick.
    b.box(V3(0.0, 0.125, 0), V3(0.2, 0.03, 0.16), 0.01);
    b.style(steel, METAL);
    for (int i = 0; i < 6; i++) nut(b, V3((i % 3 - 1) * 0.075, 0.143, (i / 3 ? 1 : -1) * 0.062), 1, 0.016, 0.008);
    b.style(c, fm);
    b.box(V3(-0.02, 0.165, 0), V3(0.09, 0.05, 0.09), 0.015);
    // Long extension housing and the slip yoke.
    xlathe(b, V3(-0.42, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.04), V2(0.066, 0.05), V2(0.07, 0.15), V2(0.082, 0.17), V2(0.09, 0.2), V2(0.106, 0.26), V2(0, 0.26)}, 14);
    b.style(V3(0.35, 0.35, 0.37), METAL);
    xcyl(b, -0.5, -0.42, 0, 0, 0.022, 8);
    yoke(b, -0.5, 0, 0, -1, 0.032);
    b.style(c, fm);
    b.box(V3(-0.3, -0.085, 0), V3(0.1, 0.03, 0.1));
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(-0.3, -0.108, 0), V3(0.12, 0.016, 0.12));
    if (p.i("shifter")) {
        b.style(V3(0.7, 0.7, 0.73), CHROME);
        b.tube({V3(-0.02, 0.19, 0), V3(-0.02, 0.28, 0), V3(0.0, 0.36, 0)}, 0.011, 6);
        b.lathe(V3(0.0, 0.36, 0), {V2(0, 0), V2(0.018, 0.006), V2(0.03, 0.03), V2(0.018, 0.058), V2(0, 0.062)}, 10);
    }
    // Input shaft and release bearing.
    b.style(steel, METAL);
    xcyl(b, 0.18, 0.31, 0, 0, 0.036, 10);
    xcyl(b, 0.31, 0.42, 0, 0, 0.014, 6);
    b.pop();
}

Reg gearboxManual({"gearbox_manual", "GEARBOX MANUAL", "PARTS", "Career upgrade: manual transmission", 0.8,
                   {
                       {"ALLOY 6-SPEED", "Modern ribbed alloy box, short tail, floor shifter", {gbFinishParam(0), choice("shifter", "SHIFTER", 1, {"REMOVED", "FITTED"})}, manualAlu},
                       {"IRON TOP-LOADER", "Old-school cast iron four-speed, side plates and a long tail", {gbFinishParam(1), choice("shifter", "SHIFTER", 1, {"REMOVED", "FITTED"})}, manualIron},
                   }});

// -- gearbox_auto ------------------------------------------------------------------------------

void autoClassic(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32), dark(0.07, 0.07, 0.075);
    const double Y0 = 0.245;
    gbRails(b, -0.1, 0.42, 0.07);
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    bellHousing(b, 0.18, 0.22, 0.2, 0.14, 8);
    b.box(V3(0.27, -0.06, -0.185), V3(0.14, 0.14, 0.07), 0.02);   // starter pocket
    // Round main case with its cooling-fin rings, and the pan under it.
    xlathe(b, V3(-0.12, 0, 0), {V2(0, 0), V2(0.12, 0), V2(0.127, 0.02), V2(0.127, 0.08), V2(0.138, 0.09), V2(0.138, 0.105), V2(0.127, 0.115), V2(0.127, 0.2), V2(0.135, 0.215),
                                V2(0.135, 0.25), V2(0.14, 0.27), V2(0.14, 0.3), V2(0, 0.3)}, 16);
    b.box(V3(0.04, -0.152, 0), V3(0.3, 0.04, 0.23), 0.012);   // valve body pan
    b.style(steel, METAL);
    b.box(V3(0.04, -0.127, 0), V3(0.32, 0.01, 0.25));
    for (int i = 0; i < 5; i++)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(-0.07 + 0.055 * i, -0.12, s * 0.12), 1, 0.017, 0.008);
    nut(b, V3(-0.12, -0.12, 0), 1, 0.017, 0.008);
    nut(b, V3(0.2, -0.12, 0), 1, 0.017, 0.008);
    b.style(c, fm);
    b.box(V3(0.04, -0.178, 0), V3(0.05, 0.012, 0.05));   // drain plug boss
    // Tail housing and the output shaft.
    xlathe(b, V3(-0.34, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.03), V2(0.06, 0.045), V2(0.075, 0.12), V2(0.092, 0.2), V2(0.12, 0.22), V2(0, 0.22)}, 14);
    b.style(V3(0.35, 0.35, 0.37), METAL);
    xcyl(b, -0.41, -0.33, 0, 0, 0.022, 8);
    yoke(b, -0.41, 0, 0, -1, 0.03);
    b.style(c, fm);
    b.box(V3(-0.24, -0.088, 0), V3(0.09, 0.03, 0.09));
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(-0.24, -0.108, 0), V3(0.12, 0.016, 0.11));
    // Servo cover, vacuum modulator, dipstick tube, oil cooler lines and the shift lever on the side.
    b.style(steel, METAL);
    zcyl(b, 0.02, 0.01, 0.13, 0.15, 0.042, 10);
    nut(b, V3(0.02, 0.01, 0.152), 2, 0.03, 0.006);
    b.style(c, fm);
    b.cyl(V3(-0.05, 0.12, 0.05), V3(-0.12, 0.17, 0.09), 0.015, 8);
    b.style(V3(0.7, 0.7, 0.72), CHROME);
    b.tube({V3(0.09, 0.03, 0.128), V3(0.14, 0.03, 0.175), V3(0.26, 0.02, 0.185)}, 0.009, 6);
    b.tube({V3(0.07, -0.01, 0.128), V3(0.12, -0.01, 0.164), V3(0.26, -0.015, 0.167)}, 0.009, 6);
    b.style(steel, METAL);
    nut(b, V3(0.265, 0.02, 0.185), 0, 0.026, 0.02);
    nut(b, V3(0.265, -0.015, 0.167), 0, 0.026, 0.02);
    b.tube({V3(-0.05, -0.12, -0.1), V3(-0.05, -0.02, -0.15), V3(-0.05, 0.14, -0.15)}, 0.008, 6);
    b.cyl(V3(-0.05, 0.14, -0.15), V3(-0.05, 0.16, -0.15), 0.014, 6);
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    b.beam(V3(-0.01, 0.04, -0.14), V3(-0.08, 0.1, -0.15), 0.02, 0.008, V3(0, 0, 1));
    b.cyl(V3(-0.01, 0.04, -0.14), V3(-0.01, 0.04, -0.17), 0.011, 6);
    b.tube({V3(-0.08, 0.1, -0.16), V3(-0.12, 0.05, -0.165), V3(-0.14, -0.04, -0.16)}, 0.004, 4);
    // Torque converter nose in the bell.
    if (p.i("converter")) {
        b.style(V3(0.45, 0.45, 0.48), METAL);
        xlathe(b, V3(0.3, 0, 0), {V2(0, 0), V2(0.15, 0), V2(0.155, 0.02), V2(0.13, 0.08), V2(0.06, 0.11), V2(0, 0.115)}, 14);
        b.style(V3(0.05, 0.05, 0.055), METAL);
        boltRingX(b, V3(0.31, 0, 0), 0.135, 3, 0.022, 0.012, 0.3);
    }
    b.pop();
}

void autoModern(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32), dark(0.06, 0.06, 0.065);
    const double Y0 = 0.245;
    gbRails(b, -0.14, 0.42, 0.065);
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    bellHousing(b, 0.14, 0.26, 0.2, 0.15, 8);
    b.box(V3(0.27, -0.06, -0.185), V3(0.14, 0.14, 0.07), 0.02);
    for (int i = 0; i < 4; i++) {   // bell ribs
        const double a = PI * (0.75 + 0.12 * i);
        b.beam(V3(0.2, 0.15 * std::sin(a), 0.15 * std::cos(a)), V3(0.34, 0.185 * std::sin(a), 0.185 * std::cos(a)), 0.016, 0.012, V3(1, 0, 0));
    }
    // Ribbed square-shouldered case with pan and a grid of stiffeners.
    b.box(V3(-0.02, 0, 0), V3(0.34, 0.25, 0.23), 0.05);
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 6; i++) b.box(V3(0.1 - 0.05 * i, 0.0, s * 0.117), V3(0.012, 0.2, 0.012));
        b.box(V3(-0.02, 0.04, s * 0.117), V3(0.30, 0.012, 0.012));
        b.box(V3(-0.02, -0.04, s * 0.117), V3(0.30, 0.012, 0.012));
    }
    b.style(dark, PAINT);
    b.box(V3(-0.02, -0.155, 0), V3(0.3, 0.05, 0.2), 0.015);
    b.style(steel, METAL);
    b.box(V3(-0.02, -0.128, 0), V3(0.32, 0.008, 0.22));
    for (int i = 0; i < 6; i++)
        for (int s = -1; s <= 1; s += 2) nut(b, V3(-0.13 + 0.055 * i, -0.121, s * 0.105), 1, 0.016, 0.008);
    // Tail housing with an electronic speed sensor.
    b.style(c, fm);
    xlathe(b, V3(-0.36, 0, 0), {V2(0, 0), V2(0.055, 0), V2(0.055, 0.04), V2(0.065, 0.06), V2(0.075, 0.12), V2(0.1, 0.19), V2(0.12, 0.22), V2(0, 0.22)}, 14);
    b.style(V3(0.35, 0.35, 0.37), METAL);
    xcyl(b, -0.43, -0.35, 0, 0, 0.022, 8);
    yoke(b, -0.43, 0, 0, -1, 0.03);
    b.style(c, fm);
    b.box(V3(-0.26, -0.09, 0), V3(0.09, 0.03, 0.09));
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(-0.26, -0.108, 0), V3(0.12, 0.016, 0.11));
    // Connector on the side, with its wiring loom over the top; breather; cooler lines.
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(0.02, 0.06, 0.135), V3(0.07, 0.05, 0.04), 0.008);
    b.tube({V3(0.02, 0.09, 0.145), V3(0.0, 0.16, 0.12), V3(-0.02, 0.17, 0.0), V3(-0.04, 0.16, -0.12), V3(-0.08, 0.12, -0.135)}, 0.012, 6);
    b.box(V3(-0.08, 0.1, -0.14), V3(0.045, 0.04, 0.03));
    b.tube({V3(-0.12, 0.125, 0.04), V3(-0.12, 0.17, 0.04), V3(-0.16, 0.2, 0.04)}, 0.007, 5);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    b.tube({V3(0.1, 0.02, 0.118), V3(0.16, 0.02, 0.17), V3(0.27, 0.01, 0.18)}, 0.01, 6);
    b.tube({V3(0.08, -0.03, 0.118), V3(0.14, -0.03, 0.155), V3(0.27, -0.035, 0.16)}, 0.01, 6);
    b.style(V3(0.9, 0.55, 0.1), MATTE);
    nut(b, V3(0.275, 0.01, 0.18), 0, 0.028, 0.02);
    nut(b, V3(0.275, -0.035, 0.16), 0, 0.028, 0.02);
    // Input shaft in the bell.
    b.style(steel, METAL);
    xcyl(b, 0.15, 0.32, 0, 0, 0.04, 10);
    xcyl(b, 0.32, 0.44, 0, 0, 0.014, 6);
    b.pop();
}

Reg gearboxAuto({"gearbox_auto", "GEARBOX AUTO", "PARTS", "Career upgrade: automatic transmission", 0.8,
                 {
                     {"CLASSIC 3-SPEED", "Round heavy case, deep pan, converter in the bell", {gbFinishParam(0), choice("converter", "CONVERTER", 1, {"REMOVED", "FITTED"})}, autoClassic},
                     {"MODERN 6-SPEED", "Ribbed square case, wiring loom and quick-fit cooler lines", {gbFinishParam(0)}, autoModern},
                 }});

// -- transfer_case -----------------------------------------------------------------------------

// A short shift lever on a turned boss (at local x, y): rises, then leans, with a ball knob.
void shortLever(B& b, double x, double y, double z, const V3& col, double lean) {
    b.style(col, METAL);
    b.cyl(V3(x, y, z), V3(x, y + 0.05, z), 0.028, 10);
    b.style(V3(0.1, 0.1, 0.11), PAINT);
    b.tube({V3(x, y + 0.04, z), V3(x, y + 0.12, z), V3(x + lean, y + 0.17, z + lean * 0.4)}, 0.01, 6);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.sphere(V3(x + lean, y + 0.185, z + lean * 0.4), V3(0.022, 0.022, 0.022), 1);
}

void transferGear(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32);
    const double Y0 = 0.21;
    gbRails(b, -0.2, 0.2, 0.07, 0.11);
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    b.box(V3(0.0, 0, 0), V3(0.31, 0.27, 0.27), 0.05);
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 5; i++) b.box(V3(0.11 - 0.055 * i, 0.0, s * 0.137), V3(0.014, 0.2, 0.012));
        b.box(V3(-0.01, 0.0, s * 0.14), V3(0.18, 0.012, 0.008));
    }
    // Bearing retainers: input (front, centre), rear output, front output (low, to the right).
    xlathe(b, V3(0.15, 0, 0), {V2(0.03, 0), V2(0.11, 0), V2(0.11, 0.025), V2(0.085, 0.03), V2(0.085, 0.06), V2(0.03, 0.065)}, 14);
    xlathe(b, V3(-0.15, 0, 0), {V2(0.03, 0), V2(0.085, 0), V2(0.085, -0.03), V2(0.11, -0.03), V2(0.11, -0.055), V2(0.03, -0.055)}, 14);
    xlathe(b, V3(0.15, -0.1, -0.09), {V2(0.02, 0), V2(0.065, 0), V2(0.065, 0.04), V2(0.02, 0.04)}, 12);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingX(b, V3(0.2, 0, 0), 0.098, 6, 0.018, 0.008, 0.2);
    boltRingX(b, V3(-0.208, 0, 0), 0.098, 6, 0.018, 0.008, 0.5);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, 0.2, 0.26, 0, 0, 0.024, 8);
    xcyl(b, -0.3, -0.2, 0, 0, 0.024, 8);
    xcyl(b, 0.19, 0.28, -0.1, -0.09, 0.022, 8);
    yoke(b, -0.3, 0, 0, -1, 0.032);
    yoke(b, 0.28, -0.1, -0.09, 1, 0.03);
    // Side cover plates, top shift cover with the lever, mounting feet.
    b.style(steel, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(-0.01, 0.0, s * 0.141), V3(0.17, 0.17, 0.008), 0.003);
        for (int i = 0; i < 6; i++) nut(b, V3(-0.01 + (i % 3 - 1) * 0.07, (i / 3 ? 0.068 : -0.068), s * 0.147), 2, 0.016, 0.006);
    }
    b.style(V3(0.9, 0.85, 0.5), MATTE);
    for (int s = -1; s <= 1; s += 2) {   // "4X4" stencilled on both cover plates
        b.push();
        b.move(-0.01, 0.0, s * 0.1455);
        b.yaw(-s * 90);
        b.letters("4X4", V3(0, 0, 0), 0.0085, 0.001);
        b.pop();
    }
    b.style(c, fm);
    b.box(V3(-0.02, 0.14, 0), V3(0.12, 0.03, 0.12), 0.01);
    for (int i = 0; i < 4; i++) {
        b.style(steel, METAL);
        nut(b, V3(-0.02 + (i & 1 ? 0.045 : -0.045), 0.157, (i & 2 ? 0.045 : -0.045)), 1, 0.016, 0.006);
    }
    if (p.i("lever")) shortLever(b, -0.02, 0.155, 0.0, c, 0.02);
    b.style(c, fm);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.1, -0.15, s * 0.12), V3(0.08, 0.05, 0.07), 0.01);
    b.pop();
}

void transferChain(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32);
    const double Y0 = 0.19;
    gbRails(b, -0.25, 0.2, 0.07, 0.11);
    b.push();
    b.move(0, Y0, 0);
    b.style(c, fm);
    b.box(V3(0.0, 0, 0), V3(0.34, 0.24, 0.23), 0.055);
    b.box(V3(0.0, 0, 0), V3(0.355, 0.25, 0.014), 0.004);   // the seam flange between the halves
    b.style(V3(0.1, 0.1, 0.11), MATTE);
    for (int s = -1; s <= 1; s += 2) {   // "4WD" cast into both halves
        b.push();
        b.move(0.0, -0.01, s * 0.1155);
        b.yaw(-s * 90);
        b.letters("4WD", V3(0, 0, 0), 0.011, 0.001);
        b.pop();
    }
    b.style(steel, METAL);
    b.style(steel, METAL);
    for (int i = 0; i < 7; i++) {
        nut(b, V3(-0.15 + 0.05 * i, 0.128, 0), 1, 0.016, 0.008);
        nut(b, V3(-0.15 + 0.05 * i, -0.128, 0), 1, 0.016, 0.008);
    }
    for (int sx = -1; sx <= 1; sx += 2)
        for (int i = 0; i < 3; i++) nut(b, V3(sx * 0.178, (i - 1) * 0.07, 0), 0, 0.016, 0.008);
    b.style(c, fm);
    // Front input (centre), rear output with a long tail, front output low on the left.
    xlathe(b, V3(0.17, 0, 0), {V2(0.03, 0), V2(0.1, 0), V2(0.1, 0.03), V2(0.075, 0.035), V2(0.075, 0.06), V2(0.03, 0.065)}, 14);
    xlathe(b, V3(-0.17, 0, 0), {V2(0.03, 0), V2(0.08, 0), V2(0.07, -0.05), V2(0.058, -0.1), V2(0.058, -0.13), V2(0.03, -0.13)}, 14);
    xlathe(b, V3(0.17, -0.085, 0.085), {V2(0.02, 0), V2(0.06, 0), V2(0.06, 0.04), V2(0.02, 0.04)}, 12);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingX(b, V3(0.232, 0, 0), 0.088, 6, 0.018, 0.008, 0.2);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, 0.23, 0.28, 0, 0, 0.024, 8);
    xcyl(b, -0.37, -0.3, 0, 0, 0.024, 8);
    xcyl(b, 0.2, 0.28, -0.085, 0.085, 0.022, 8);
    yoke(b, -0.37, 0, 0, -1, 0.032);
    yoke(b, 0.28, -0.085, 0.085, 1, 0.03);
    // Range sleeve and a shift motor box on top; lever on the side.
    b.style(c, fm);
    b.box(V3(-0.05, 0.14, -0.03), V3(0.11, 0.04, 0.12), 0.015);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(-0.07, 0.185, -0.03), V3(0.07, 0.04, 0.06), 0.008);
    if (p.i("lever")) shortLever(b, 0.06, 0.12, 0.05, c, -0.03);
    b.style(steel, METAL);
    for (int i = 0; i < 4; i++) nut(b, V3(-0.05 + (i & 1 ? 0.04 : -0.04), 0.162, -0.03 + (i & 2 ? 0.045 : -0.045)), 1, 0.016, 0.006);
    b.style(c, fm);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.11, -0.135, s * 0.115), V3(0.08, 0.04, 0.07), 0.01);
    b.pop();
}

Reg transferCase({"transfer_case", "TRANSFER CASE", "PARTS", "Career upgrade: 4x4 transfer case", 0.6,
                  {
                      {"GEAR-DRIVEN HD", "Tall cast-iron two-speed box with a short lever", {gbFinishParam(1), choice("lever", "LEVER", 1, {"REMOVED", "FITTED"})}, transferGear},
                      {"CHAIN-DRIVEN", "Light alloy low-slung case with a split seam and long tail", {gbFinishParam(0), choice("lever", "LEVER", 1, {"REMOVED", "FITTED"})}, transferChain},
                  }});

// ---- differential ----------------------------------------------------------------------------

std::vector<V2> ovalPoly(double a, double bb, int n) {
    std::vector<V2> p;
    for (int i = 0; i < n; i++) p.push_back(V2(a * std::cos(2 * PI * i / n), bb * std::sin(2 * PI * i / n)));
    return p;
}
// A ring of bolt heads round the Z axis on the plane z = c.z.
void boltRingZ(B& b, const V3& c, double R, int n, double s = 0.016, double t = 0.008, double a0 = 0) {
    for (int i = 0; i < n; i++) {
        const double a = a0 + 2 * PI * i / n;
        nut(b, c + V3(R * std::cos(a), R * std::sin(a), 0), 2, s, t);
    }
}

// Axle centre section: cast pumpkin, cover and its bolt ring facing front, pinion yoke at the back,
// axle tubes out each side. The locker's air line runs out along the tube.
void diffAxle(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const V3 steel(0.24, 0.24, 0.255);
    const double Y0 = 0.17;
    b.style(V3(0.45, 0.35, 0.22), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.0625, s * 0.27), V3(0.14, 0.125, 0.1), 0.006);
    b.push();
    b.move(0, Y0, 0);
    b.style(steel, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.cone(V3(0, 0, s * 0.12), V3(0, 0, s * 0.2), 0.082, 0.05, 12, false);
        b.cyl(V3(0, 0, s * 0.2), V3(0, 0, s * 0.35), 0.05, 12, true);
        // Bearing flange at the end, axle stub beyond.
        b.cyl(V3(0, 0, s * 0.35), V3(0, 0, s * 0.364), 0.09, 12);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        boltRingZ(b, V3(0, 0, s * 0.366), 0.072, 6, 0.018, 0.006, 0.3);
        b.style(V3(0.5, 0.5, 0.53), METAL);
        b.cyl(V3(0, 0, s * 0.364), V3(0, 0, s * 0.43), 0.024, 8);
        b.style(col * 0.9, PAINT);
        b.box(V3(0.0, 0.062, s * 0.265), V3(0.11, 0.02, 0.1), 0.004);   // spring perch
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int i = 0; i < 4; i++) nut(b, V3((i & 1 ? 0.04 : -0.04), 0.075, s * 0.265 + (i & 2 ? 0.035 : -0.035)), 1, 0.018, 0.008);
        b.style(steel, METAL);
    }
    // The housing: a flat-faced pumpkin with a domed back.
    b.style(col, PAINT);
    b.push();
    b.yaw(90);
    b.extrude(ovalPoly(0.155, 0.13, 20), 0.1, 0.015);
    b.pop();
    b.sphere(V3(-0.035, 0, 0), V3(0.115, 0.128, 0.152), 2);
    b.style(col * 0.82, PAINT);
    b.push();
    b.yaw(90);
    b.extrude(ovalPoly(0.15, 0.125, 20), 0.02, 0.075);   // the ring-gear cover
    b.pop();
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int i = 0; i < 10; i++) {
        const double a = 2 * PI * (i + 0.5) / 10;
        nut(b, V3(0.0875, 0.113 * std::sin(a), 0.141 * std::cos(a)), 0, 0.02, 0.008);
    }
    nut(b, V3(0.0875, -0.07, 0.0), 0, 0.024, 0.01);   // fill plug
    b.style(col * 0.9, PAINT);
    b.box(V3(0.0885, 0.0, 0.0), V3(0.008, 0.02, 0.27));   // cover ribs
    b.box(V3(0.0885, 0.0, 0.0), V3(0.008, 0.22, 0.02));
    // Pinion nose and yoke.
    b.style(col, PAINT);
    xlathe(b, V3(-0.27, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.03), V2(0.062, 0.05), V2(0.08, 0.1), V2(0.09, 0.125), V2(0, 0.125)}, 14);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, -0.31, -0.27, 0, 0, 0.026, 8);
    yoke(b, -0.31, 0, 0, -1, 0.033);
    nut(b, V3(-0.405, 0, 0), 0, 0.02, 0.01);
    // Locker: its cast boss on the side, the air solenoid, a hose running out along the tube.
    if (p.i("locker")) {
        b.style(V3(0.78, 0.6, 0.15), METAL);
        zcyl(b, -0.035, 0.0, 0.145, 0.185, 0.04, 10);
        b.style(V3(0.1, 0.28, 0.6), PAINT);
        b.box(V3(-0.035, 0.03, 0.19), V3(0.05, 0.07, 0.03), 0.006);
        b.style(V3(0.05, 0.05, 0.055), MATTE);
        b.tube({V3(-0.035, 0.05, 0.19), V3(-0.06, 0.09, 0.2), V3(-0.07, 0.06, 0.26), V3(-0.065, 0.055, 0.34)}, 0.007, 5);
    }
    // Breather.
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.tube({V3(-0.05, 0.12, 0.0), V3(-0.05, 0.2, 0.0), V3(-0.1, 0.235, 0.02)}, 0.006, 5);
    b.style(V3(0.7, 0.55, 0.12), METAL);
    b.cyl(V3(-0.1, 0.235, 0.02), V3(-0.1, 0.25, 0.02), 0.011, 6);
    b.pop();
}

// A bare third member: the housing cut open at the front to show the ring gear on its carrier and
// the locker drum, the pinion coming in from the back.
void diffBare(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const double Y0 = 0.18;
    gbRails(b, -0.3, 0.1, 0.03, 0.09);
    b.push();
    b.move(0, Y0, 0);
    // Cast bowl, open to the front.
    b.style(col, PAINT);
    xlathe(b, V3(-0.19, 0, 0), {V2(0.05, 0), V2(0.07, 0.03), V2(0.12, 0.1), V2(0.15, 0.17), V2(0.15, 0.245), V2(0.122, 0.245), V2(0.122, 0.17), V2(0.1, 0.1), V2(0.04, 0.03), V2(0.05, 0)}, 18);
    // Pinion nose, bearing cap and yoke.
    xlathe(b, V3(-0.33, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.04), V2(0.065, 0.06), V2(0.075, 0.14), V2(0, 0.14)}, 14);
    b.style(V3(0.38, 0.38, 0.4), METAL);
    xcyl(b, -0.37, -0.33, 0, 0, 0.026, 8);
    yoke(b, -0.37, 0, 0, -1, 0.033);
    // Pinion shaft and head inside, ring gear (teeth) on the carrier, the locker drum.
    b.style(V3(0.4, 0.4, 0.43), METAL);
    xcyl(b, -0.19, -0.02, -0.02, 0, 0.027, 8);
    b.cone(V3(-0.02, -0.02, 0), V3(0.03, -0.02, 0), 0.027, 0.05, 8);
    std::vector<V2> gear;
    const int teeth = 20;
    for (int i = 0; i < teeth; i++) {
        const double a0 = 2 * PI * i / teeth, da = 2 * PI / teeth;
        gear.push_back(V2(0.098 * std::cos(a0), 0.098 * std::sin(a0)));
        gear.push_back(V2(0.118 * std::cos(a0 + da * 0.2), 0.118 * std::sin(a0 + da * 0.2)));
        gear.push_back(V2(0.118 * std::cos(a0 + da * 0.55), 0.118 * std::sin(a0 + da * 0.55)));
        gear.push_back(V2(0.098 * std::cos(a0 + da * 0.75), 0.098 * std::sin(a0 + da * 0.75)));
    }
    b.style(V3(0.5, 0.5, 0.53), METAL);
    b.push();
    b.move(0.02, 0, 0);
    b.extrude(gear, 0.03, -0.045);
    b.pop();
    b.style(V3(0.78, 0.6, 0.15), METAL);
    zcyl(b, 0.02, 0.0, -0.095, 0.07, 0.068, 14);   // locker drum
    b.style(V3(0.45, 0.45, 0.48), METAL);
    zcyl(b, 0.02, 0.0, -0.14, 0.14, 0.04, 10);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    boltRingZ(b, V3(0.02, 0, -0.016), 0.083, 6, 0.014, 0.008, 0.2);   // ring gear bolts
    // Bearing caps on the housing rim.
    b.style(col * 0.9, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0.05, 0.0, s * 0.13), V3(0.07, 0.06, 0.03), 0.006);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        nut(b, V3(0.05, 0.02, s * 0.147), 2, 0.016, 0.006);
        nut(b, V3(0.05, -0.02, s * 0.147), 2, 0.016, 0.006);
        b.style(col * 0.9, PAINT);
    }
    b.pop();
}

Reg differential({"differential", "DIFFERENTIAL", "PARTS", "Career upgrade: axle centre section with its locker", 0.8,
                  {
                      {"AXLE WITH COVER", "Cast pumpkin, bolted cover, axle tubes and an air locker", {paintParam(9), choice("locker", "LOCKER", 1, {"NONE", "AIR LOCKER"})}, diffAxle},
                      {"BARE THIRD MEMBER", "Cutaway centre section: ring gear and locker drum showing", {paintParam(9)}, diffBare},
                  }});

// ---- coilover --------------------------------------------------------------------------------

// A coil spring along X (centre line at (yc, zc), coil radius R, wire radius w).
void helix(B& b, double x0, double x1, double yc, double zc, double R, double w, double turns, int per = 7, int sides = 5) {
    const int n = (int)(turns * per) + 1;
    std::vector<V3> path;
    for (int i = 0; i < n; i++) {
        const double t = (double)i / (n - 1), a = 2 * PI * turns * t;
        path.push_back(V3(x0 + (x1 - x0) * t, yc + R * std::cos(a), zc + R * std::sin(a)));
    }
    b.tube(path, w, sides, false, true);
}
// A ring eye with its axis along Z, lying in the X-Y plane at (x, y, z).
void eyeZ(B& b, double x, double y, double z, double R, double r) {
    b.push();
    b.move(x, y, z);
    b.roll(90);
    b.torus(V3(), R, r, 10, 5);
    b.pop();
}

V3 anodize(int i) {
    static const V3 c[] = {V3(0.74, 0.58, 0.16), V3(0.10, 0.25, 0.62), V3(0.09, 0.09, 0.10), V3(0.66, 0.67, 0.70)};
    return c[((i % 4) + 4) % 4];
}
const std::vector<std::string>& anodizeNames() {
    static const std::vector<std::string> n = {"GOLD", "BLUE", "BLACK", "SILVER"};
    return n;
}

void coilRemote(B& b, const P& p) {
    const V3 an = anodize(p.i("body")), sp = paint(p.i("spring"));
    const double R = 0.0635, w = 0.0075, Y = 0.0785;
    b.push();
    b.move(0, Y, 0);
    // Spring, seats and the lock ring.
    b.style(sp, PAINT);
    helix(b, -0.17, 0.2, 0, 0, R, w, 8, 10, 5);
    b.style(V3(0.5, 0.5, 0.53), METAL);
    xcyl(b, -0.186, -0.172, 0, 0, 0.078, 12);
    xcyl(b, 0.2, 0.214, 0, 0, 0.078, 12);
    b.style(an * 0.9, METAL);
    xcyl(b, -0.172, -0.15, 0, 0, 0.044, 6);   // lock ring
    // Body with its threaded section, the shaft and the eyes.
    b.style(an, METAL);
    xlathe(b, V3(-0.3, 0, 0), {V2(0, 0), V2(0.03, 0), V2(0.034, 0.012), V2(0.034, 0.1), V2(0.037, 0.103), V2(0.037, 0.25), V2(0.034, 0.253), V2(0.034, 0.31), V2(0.03, 0.322), V2(0.012, 0.325)}, 12);
    b.style(an * 0.7, METAL);
    for (int i = 0; i < 5; i++) xcyl(b, -0.12 + 0.025 * i, -0.115 + 0.025 * i, 0, 0, 0.0385, 10, false);   // thread rings
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.01, 0.3, 0, 0, 0.0095, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.21, 0.25, 0, 0, 0.03, 10);
    xcyl(b, -0.345, -0.3, 0, 0, 0.022, 8);
    eyeZ(b, -0.362, 0, 0, 0.018, 0.009);
    xcyl(b, 0.3, 0.335, 0, 0, 0.015, 8);
    eyeZ(b, 0.352, 0, 0, 0.018, 0.009);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.362, 0, -0.016, 0.016, 0.013, 8);
    zcyl(b, 0.352, 0, -0.016, 0.016, 0.013, 8);
    b.pop();
    // Remote reservoir lying on the ground, clamped to a plate, with a braided hose.
    const double rz = 0.18, ry = 0.03;
    b.style(an, METAL);
    xlathe(b, V3(-0.07, ry, rz), {V2(0, 0), V2(0.026, 0), V2(0.028, 0.005), V2(0.028, 0.175), V2(0.026, 0.18), V2(0, 0.18)}, 10);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.11, 0.135, ry, rz, 0.008, 6);
    b.style(V3(0.8, 0.1, 0.08), METAL);
    b.cyl(V3(0.0, ry + 0.027, rz), V3(0.0, ry + 0.045, rz), 0.013, 8);   // adjuster knob
    b.style(V3(0.12, 0.12, 0.13), PAINT);
    b.box(V3(0.02, 0.005, rz), V3(0.17, 0.01, 0.05));
    xcyl(b, -0.005, 0.005, ry, rz, 0.031, 8);
    xcyl(b, 0.045, 0.055, ry, rz, 0.031, 8);
    b.style(V3(0.62, 0.64, 0.68), METAL);
    b.tube({V3(-0.12, Y, 0.034), V3(-0.16, Y - 0.01, 0.1), V3(-0.15, 0.04, 0.16), V3(-0.08, ry, rz)}, 0.0065, 5);
    b.style(an, METAL);
    zcyl(b, -0.12, Y, 0.03, 0.05, 0.011, 6);
}

void coilPlain(B& b, const P& p) {
    const V3 sp = paint(p.i("spring"));
    const double R = 0.0635, w = 0.0075;
    // The loose coil spring with a rubber isolator at one end.
    b.style(sp, PAINT);
    helix(b, -0.18, 0.2, 0.0755, 0.1, R, w, 8, 10, 5);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    xcyl(b, 0.2, 0.212, 0.0755, 0.1, 0.0745, 12);
    xcyl(b, -0.192, -0.18, 0.0755, 0.1, 0.0745, 12);
    // The shock: dust boot, body, shaft, bushed eyes.
    const double Y = 0.046, Z = -0.1;
    b.style(V3(0.1, 0.2, 0.48), PAINT);
    xlathe(b, V3(-0.26, Y, Z), {V2(0, 0), V2(0.027, 0), V2(0.027, 0.2), V2(0, 0.2)}, 12);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    std::vector<V2> boot = {V2(0, 0.0), V2(0.02, 0.0)};
    for (int i = 0; i < 6; i++) {
        boot.push_back(V2(0.034, 0.012 + 0.02 * i));
        boot.push_back(V2(0.021, 0.022 + 0.02 * i));
    }
    boot.push_back(V2(0.021, 0.14));
    boot.push_back(V2(0, 0.14));
    xlathe(b, V3(0.0, Y, Z), boot, 10);
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.14, 0.24, Y, Z, 0.0095, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, 0.14, 0.2, Y, Z, 0.015, 8);
    xcyl(b, -0.3, -0.26, Y, Z, 0.02, 8);
    b.box(V3(-0.31, Y, Z), V3(0.04, 0.034, 0.03));
    for (int e = 0; e < 2; e++) {
        const double x = e ? 0.285 : -0.335;
        b.style(V3(0.1, 0.1, 0.11), METAL);
        zcyl(b, x, Y, Z - 0.025, Z + 0.025, 0.026, 10);
        b.style(V3(0.04, 0.04, 0.045), MATTE);
        zcyl(b, x, Y, Z - 0.03, Z + 0.03, 0.019, 10);
        b.style(V3(0.7, 0.7, 0.73), METAL);
        zcyl(b, x, Y, Z - 0.034, Z + 0.034, 0.009, 6);
    }
    b.style(V3(0.1, 0.1, 0.11), METAL);
    b.box(V3(0.265, Y, Z), V3(0.04, 0.03, 0.03));
    // Spring seat cup on the shock.
    b.style(V3(0.2, 0.2, 0.21), METAL);
    xcyl(b, 0.0, 0.01, Y, Z, 0.045, 10);
}

void coilBypass(B& b, const P& p) {
    const V3 an = anodize(p.i("body")), sp = paint(p.i("spring")), tube = anodize(p.i("tubes"));
    const double R = 0.08, w = 0.009, Y = 0.0925;
    b.push();
    b.move(0, Y, 0);
    b.style(sp, PAINT);
    helix(b, -0.04, 0.2, 0, 0, R, w, 5.5, 10, 5);
    b.style(V3(0.5, 0.5, 0.53), METAL);
    xcyl(b, -0.054, -0.04, 0, 0, 0.092, 12);
    xcyl(b, 0.2, 0.214, 0, 0, 0.092, 12);
    // Big body with its bypass tubes.
    b.style(an, METAL);
    xlathe(b, V3(-0.3, 0, 0), {V2(0, 0), V2(0.046, 0), V2(0.05, 0.015), V2(0.05, 0.33), V2(0.046, 0.345), V2(0.02, 0.35)}, 14);
    for (int k = 0; k < 3; k++) {
        const double a = PI / 2 + k * 2 * PI / 3;
        const double ty = 0.064 * std::sin(a), tz = 0.064 * std::cos(a);
        b.style(tube, METAL);
        xcyl(b, -0.22, 0.03, ty, tz, 0.0125, 8);
        b.style(V3(0.12, 0.12, 0.13), METAL);
        b.box(V3(-0.22, ty * 0.9, tz * 0.9), V3(0.04, 0.03, 0.03));
        b.box(V3(0.03, ty * 0.9, tz * 0.9), V3(0.04, 0.03, 0.03));
        static const V3 knob[] = {V3(0.8, 0.1, 0.08), V3(0.1, 0.3, 0.7), V3(0.7, 0.7, 0.72)};
        b.style(knob[k], METAL);
        b.cyl(V3(-0.02, ty + std::sin(a) * 0.01, tz + std::cos(a) * 0.01), V3(-0.02, ty + std::sin(a) * 0.035, tz + std::cos(a) * 0.035), 0.012, 8);
    }
    b.style(V3(0.12, 0.12, 0.13), METAL);
    xcyl(b, -0.2, -0.19, 0, 0, 0.06, 10);   // clamp rings round the tubes
    xcyl(b, 0.0, 0.01, 0, 0, 0.06, 10);
    b.style(V3(0.78, 0.78, 0.82), CHROME);
    xcyl(b, 0.04, 0.3, 0, 0, 0.0127, 8);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, -0.35, -0.3, 0, 0, 0.026, 8);
    xcyl(b, 0.3, 0.335, 0, 0, 0.018, 8);
    eyeZ(b, -0.375, 0, 0, 0.022, 0.011);
    eyeZ(b, 0.358, 0, 0, 0.022, 0.011);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    zcyl(b, -0.375, 0, -0.02, 0.02, 0.016, 8);
    zcyl(b, 0.358, 0, -0.02, 0.02, 0.016, 8);
    b.pop();
}

Reg coilover({"coilover", "COILOVER", "PARTS", "Career upgrade: coilover shock and spring", 0.7,
              {
                  {"REMOTE RESERVOIR", "Anodised coilover, threaded body, hosed remote reservoir",
                   {choice("body", "BODY", 0, anodizeNames()), paintParam(0, "spring", "SPRING")}, coilRemote},
                  {"SHOCK AND SPRING", "Plain bushed shock with a separate coil spring",
                   {paintParam(8, "spring", "SPRING")}, coilPlain},
                  {"BYPASS TRIPLE", "Fat bypass shock with three external tubes and a coil",
                   {choice("body", "BODY", 2, anodizeNames()), choice("tubes", "TUBES", 1, anodizeNames()), paintParam(8, "spring", "SPRING")}, coilBypass},
              }});

// ---- leaf_pack -------------------------------------------------------------------------------

struct LeafPack {
    int n = 7;                 // leaves
    double h1 = 0.65;          // main leaf half length
    double base = 0.045;       // top surface at the main leaf's ends
    double camber = 0.1, dc = 0.002;  // arch height; extra arch per leaf down the stack
    double tC = 0.009, tE = 0.009;    // leaf thickness centre / end
    double wz = 0.065;         // leaf width
    double pitch() const { return tC + dc; }
    double halfLen(int k) const { return n <= 1 ? h1 : h1 * (1.0 - 0.75 * k / (double)std::max(1, n - 1) * (n > 3 ? 1.0 : 0.82)); }
    double camberK(int k) const { return camber + k * dc; }
    double topCentre(int k) const { return base + camber + k * (dc - pitch()); }
    double top(int k, double x) const { const double u = x / h1; return topCentre(k) - camberK(k) * u * u; }
    double thick(int k, double x) const {
        const double h = halfLen(k), q = std::min(1.0, std::fabs(x) / h);
        return tE + (tC - tE) * (1 - q * q);
    }
};

void leafStack(B& b, const LeafPack& L, const V3& col, Mat mat, const V3& edge) {
    for (int k = 0; k < L.n; k++) {
        const double h = L.halfLen(k);
        const int np = k == 0 ? 11 : 7;
        std::vector<V2> top, bot;
        for (int i = 0; i < np; i++) {
            const double x = -h + 2 * h * i / (np - 1);
            top.push_back(V2(x, L.top(k, x)));
            bot.push_back(V2(x, L.top(k, x) - L.thick(k, x)));
        }
        std::vector<V2> poly = top;
        for (int i = np - 1; i >= 0; i--) poly.push_back(bot[i]);
        b.style(mix(col, edge, k & 1 ? 0.0 : 0.35), mat);
        b.extrude(poly, L.wz, 0);
    }
}

// An eye rolled at the end of the main leaf: a hollow cylinder with a rubber bushing.
void leafEye(B& b, double x, double y, double wz, const V3& col, Mat mat) {
    b.style(col, mat);
    zlathe(b, V3(x, y, -wz / 2), {V2(0.015, 0), V2(0.027, 0), V2(0.027, wz), V2(0.015, wz), V2(0.015, 0)}, 12);
    b.style(V3(0.04, 0.04, 0.045), MATTE);
    zcyl(b, x, y, -wz / 2 - 0.008, wz / 2 + 0.008, 0.016, 8, false);
    b.style(V3(0.6, 0.6, 0.63), METAL);
    zcyl(b, x, y, -wz / 2 - 0.012, wz / 2 + 0.012, 0.0085, 6, true);
}

void leafHardware(B& b, const LeafPack& L, const V3& metal, double pad) {
    const double wz = L.wz;
    const int last = L.n - 1;
    const double yt0 = L.top(0, 0), yb0 = L.top(last, 0) - L.thick(last, 0);
    // Axle pad on top, plate underneath, two U-bolts and the centre bolt.
    b.style(metal, METAL);
    b.box(V3(0, yt0 + 0.008, 0), V3(0.2, 0.016, 0.09), 0.003);
    b.box(V3(0, yb0 - 0.008, 0), V3(0.2, 0.016, 0.1), 0.003);
    for (int s = -1; s <= 1; s += 2) {
        const double x = s * 0.075;
        const double yTop = yt0 + 0.016 + 0.022, yBot = yb0 - 0.016 - 0.03;
        const double z = 0.0475;
        b.style(V3(0.32, 0.32, 0.34), METAL);
        b.tube({V3(x, yBot, z), V3(x, yTop - 0.012, z), V3(x, yTop, z - 0.012), V3(x, yTop, -z + 0.012), V3(x, yTop - 0.012, -z), V3(x, yBot, -z)}, 0.0075, 6);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int t = -1; t <= 1; t += 2) nut(b, V3(x, yb0 - 0.016 - 0.007, t * z), 1, 0.022, 0.012);
    }
    b.style(V3(0.1, 0.1, 0.11), METAL);
    nut(b, V3(0, yt0 + 0.022, 0), 1, 0.03, 0.014);
    b.cyl(V3(0, yb0 - 0.016, 0), V3(0, yb0 - 0.04, 0), 0.008, 6);
    // Rebound clips.
    static const double cx[] = {0.24, 0.38};
    for (double x0 : cx)
        for (int s = -1; s <= 1; s += 2) {
            const double x = s * x0;
            int cnt = 0;
            for (int k = 0; k < L.n; k++)
                if (L.halfLen(k) > std::fabs(x) + 0.02) cnt = k + 1;
            if (cnt < 2) continue;
            const double yt = L.top(0, x), yb = L.top(cnt - 1, x) - L.thick(cnt - 1, x);
            b.style(metal * 0.9, METAL);
            b.box(V3(x, yt + 0.003, 0), V3(0.032, 0.006, wz + 0.016));
            for (int t = -1; t <= 1; t += 2) b.box(V3(x, (yt + yb) / 2 - 0.003, t * (wz / 2 + 0.005)), V3(0.032, yt - yb + 0.01, 0.006));
            b.box(V3(x, yb - 0.003, 0), V3(0.032, 0.006, wz + 0.016));
        }
    (void)pad;
}

LeafPack leafOpts(const P& p) {
    LeafPack L;
    L.n = std::max(2, p.i("leaves"));
    L.camber = p("camber");
    return L;
}

void leafClassic(B& b, const P& p) {
    LeafPack L = leafOpts(p);
    L.dc = 0.0025;
    const int fin = p.i("finish");
    V3 col(0.11, 0.11, 0.12), edge(0.36, 0.36, 0.38);
    Mat mat = PAINT;
    if (fin == 1) col = V3(0.26, 0.17, 0.11), edge = V3(0.36, 0.22, 0.12), mat = MATTE;   // rusty
    if (fin == 2) col = V3(0.38, 0.38, 0.4), edge = V3(0.55, 0.55, 0.58), mat = METAL;     // bare steel
    leafStack(b, L, col, mat, edge);
    const double ye = L.base - 0.015;
    leafEye(b, L.h1 + 0.012, ye, L.wz, col, mat);
    leafEye(b, -L.h1 - 0.012, ye, L.wz, col, mat);
    leafHardware(b, L, V3(0.2, 0.2, 0.21), 0);
    if (fin == 1) {
        b.style(V3(0.4, 0.18, 0.06), MATTE);
        for (int i = 0; i < 14; i++) {
            const double x = (hash01(7, i) - 0.5) * 1.1;
            b.box(V3(x, L.top(0, x) + 0.002, (hash01(8, i) - 0.5) * 0.04), V3(0.02 + 0.03 * hash01(9, i), 0.004, 0.02));
        }
    }
}

void leafParabolic(B& b, const P& p) {
    LeafPack L;
    L.n = std::max(2, p.i("leaves"));
    L.camber = p("camber");
    L.dc = 0.014;
    L.tC = 0.016;
    L.tE = 0.0045;
    L.wz = 0.075;
    L.h1 = 0.64;
    const V3 col = paint(p.i("paint")) * 0.6, edge = paint(p.i("paint"));
    leafStack(b, L, col, PAINT, edge);
    // Spacer pads between the leaves out toward the ends.
    b.style(V3(0.06, 0.06, 0.065), MATTE);
    for (int k = 1; k < L.n; k++)
        for (int s = -1; s <= 1; s += 2) {
            const double x = s * (L.halfLen(k) - 0.06);
            const double yTop = L.top(k - 1, x) - L.thick(k - 1, x), yBot = L.top(k, x);
            b.box(V3(x, (yTop + yBot) / 2, 0), V3(0.07, std::max(0.004, yTop - yBot), L.wz * 0.9));
        }
    const double ye = L.base - 0.01;
    leafEye(b, L.h1 + 0.012, ye, L.wz, col, PAINT);
    leafEye(b, -L.h1 - 0.012, ye, L.wz, col, PAINT);
    // Shackle plates on the front eye and a hanger bracket on the back one.
    b.style(V3(0.35, 0.35, 0.37), METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(L.h1 + 0.052, ye + 0.012, s * (L.wz / 2 + 0.014)), V3(0.1, 0.034, 0.008), 0.004);
        b.box(V3(-L.h1 - 0.012, ye + 0.03, s * (L.wz / 2 + 0.017)), V3(0.05, 0.075, 0.008), 0.004);
    }
    b.style(V3(0.1, 0.1, 0.11), METAL);
    for (int s = -1; s <= 1; s += 2) {
        nut(b, V3(L.h1 + 0.01, ye, s * (L.wz / 2 + 0.022)), 2, 0.026, 0.01);
        nut(b, V3(L.h1 + 0.092, ye + 0.012, s * (L.wz / 2 + 0.022)), 2, 0.026, 0.01);
        nut(b, V3(-L.h1 - 0.012, ye, s * (L.wz / 2 + 0.024)), 2, 0.026, 0.01);
    }
    b.style(V3(0.32, 0.32, 0.34), METAL);
    zcyl(b, L.h1 + 0.092, ye + 0.012, -L.wz / 2 - 0.02, L.wz / 2 + 0.02, 0.009, 6);
    b.box(V3(-L.h1 - 0.012, ye + 0.075, 0), V3(0.06, 0.012, L.wz + 0.05), 0.003);
    leafHardware(b, L, V3(0.36, 0.36, 0.38), 1);
}

Reg leafPack({"leaf_pack", "LEAF PACK", "PARTS", "Career upgrade: leaf spring pack", 1.3,
              {
                  {"CLASSIC MULTI-LEAF", "Stacked steel leaves, rolled eyes, U-bolts and rebound clips",
                   {num("leaves", "LEAVES", 7, 3, 10, 1), num("camber", "ARCH", 0.1, 0.05, 0.14, 0.01), choice("finish", "FINISH", 0, {"BLACK", "RUSTY", "BARE STEEL"})}, leafClassic},
                  {"PARABOLIC TAPER", "Three tapered leaves on rubber spacers, with shackle and hanger",
                   {num("leaves", "LEAVES", 3, 2, 5, 1), num("camber", "ARCH", 0.1, 0.05, 0.14, 0.01), paintParam(9)}, leafParabolic},
              }});

// ---- supply_crate ------------------------------------------------------------------------------
// The career supply cache. 1.1 m (X) x 0.76 m (Z) x 0.66 m. Instance param "open": 0 closed,
// 1 opened and looted (lid off, leaning against it, empty inside).

Param openParam() { return choice("open", "OPEN", 0, {"CLOSED", "OPEN"}); }

const std::vector<std::string>& labelNames() {
    static const std::vector<std::string> n = {"CZ-07", "CZ-12", "CZ-31"};
    return n;
}
Param labelParam() { return choice("label", "LABEL", 0, labelNames()); }
const char* labelText(const P& p) { return labelNames()[(size_t)std::clamp(p.i("label"), 0, 2)].c_str(); }

// Stencil letters on the +Z long face (read from the left side) and on the -Z face (from the right).
void sideStencil(B& b, const std::string& text, double y, double zFace, double px, const V3& col) {
    b.style(col, MATTE);
    b.push();
    b.move(0, y, zFace);
    b.yaw(-90);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
    b.push();
    b.move(0, y, -zFace);
    b.yaw(90);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
}

// The same on the two short ends (+X and -X).
void endStencil(B& b, const std::string& text, double y, double xFace, double px, const V3& col) {
    b.style(col, MATTE);
    b.push();
    b.move(xFace, y, 0);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
    b.push();
    b.move(-xFace, y, 0);
    b.yaw(180);
    b.letters(text, V3(0, 0, 0), px, 0.0025);
    b.pop();
}

// A plank with its own slight tone.
void plank(B& b, const V3& c, const V3& size, const V3& base, uint32_t seed, double bevel = 0) {
    const double t = 0.86 + 0.26 * hash01(seed, 5);
    b.color(base * t).mat(WOOD);
    b.box(c, size, bevel);
}

// Where a lid of depth `thick` (its underside the local y = 0 plane) leans against the crate's +Z side.
void leanLid(B& b, double topY, double halfZ, double halfLen, double lift = 0) {
    const double phi = 57.0 * PI / 180;
    const double ym = halfLen * std::sin(phi) + 0.002 + lift;
    const double s = (ym - topY) / std::sin(phi);
    const double zm = halfZ - s * std::cos(phi);
    b.move(0, ym, zm);
    b.roll(57);
}

// -- A: wooden military crate ---------------------------------------------------------------

void woodLid(B& b, const P& p, const V3& lidCol, const V3& metal) {
    // Origin: centre of the underside; spans x +-0.55, z +-0.38, y 0..0.035.
    for (int i = 0; i < 5; i++) plank(b, V3(0, 0.0175, -0.304 + 0.152 * i), V3(1.1, 0.035, 0.148), lidCol, 100 + i);
    b.style(V3(0.42, 0.32, 0.19), WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.45, -0.015, 0), V3(0.07, 0.03, 0.66));
    b.style(metal, METAL);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(s * 0.31, 0.0365, 0), V3(0.05, 0.003, 0.762));
        for (int t = -1; t <= 1; t += 2) b.box(V3(s * 0.31, 0.0175, t * 0.3815), V3(0.05, 0.035, 0.005));
    }
    b.style(V3(0.1, 0.09, 0.07), MATTE);
    b.push();
    b.move(0, 0.0345, 0.17);
    b.yaw(-90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.024, 0.005);
    b.pop();
    b.push();   // and again, the other way up, for anyone walking up from the far side
    b.move(0, 0.0345, -0.17);
    b.yaw(90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.024, 0.005);
    b.pop();
    (void)p;
}

void crateWood(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    const V3 base = paint(p.i("paint")), lidCol = paint(8) * 0.95, metal(0.2, 0.2, 0.21), raw(0.45, 0.34, 0.2);
    const V3 rope = p.i("rope") == 0 ? V3(0.88, 0.42, 0.07) : p.i("rope") == 1 ? V3(0.55, 0.45, 0.28) : V3(0.07, 0.07, 0.075);
    // Runners and floor.
    b.style(raw * 0.9, WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.025, s * 0.25), V3(1.1, 0.05, 0.09));
    b.box(V3(0, 0.06, 0), V3(1.06, 0.02, 0.72));
    // Planked walls: four courses on the long sides and the ends.
    for (int k = 0; k < 4; k++) {
        const double y = 0.07 + 0.0645 + k * 0.1335;
        for (int s = -1; s <= 1; s += 2) {
            plank(b, V3(0, y, s * 0.3475), V3(1.06, 0.129, 0.025), base, 10 + k * 2 + (s > 0));
            plank(b, V3(s * 0.5175, y, 0), V3(0.025, 0.129, 0.67), base, 30 + k * 2 + (s > 0));
        }
    }
    // Corner posts, end battens, metal corner plates.
    b.style(raw, WOOD);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.5075, 0.335, s * 0.3375), V3(0.045, 0.55, 0.045));
    b.style(metal, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2)
            for (int lv = 0; lv < 2; lv++) {
                const double y = lv ? 0.09 : 0.575;
                b.box(V3(sx * 0.53, y, s * 0.31), V3(0.006, 0.1, 0.1));
                b.box(V3(sx * 0.475, y, s * 0.363), V3(0.1, 0.1, 0.006));
            }
    // Stripe and stencil on the long sides, in front of the planks.
    b.style(paint(8) * 0.9, MATTE);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.34, s * 0.3625), V3(1.0, 0.14, 0.004));
    sideStencil(b, labelText(p), 0.34, 0.3645, 0.026, V3(0.07, 0.065, 0.055));
    b.style(paint(8) * 0.9, MATTE);
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.5325, 0.2, 0), V3(0.004, 0.1, 0.62));
    endStencil(b, labelText(p), 0.2, 0.5345, 0.018, V3(0.07, 0.065, 0.055));
    // Rope handles through holes in the ends.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(V3(0.05, 0.04, 0.03), MATTE);
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.5315, 0.4, s * 0.115), V3(0.004, 0.045, 0.05));
        b.style(rope, MATTE);
        b.tube({V3(sx * 0.53, 0.405, -0.115), V3(sx * 0.56, 0.415, -0.12), V3(sx * 0.58, 0.385, -0.06), V3(sx * 0.58, 0.375, 0.0), V3(sx * 0.58, 0.385, 0.06), V3(sx * 0.56, 0.415, 0.12), V3(sx * 0.53, 0.405, 0.115)},
               0.013, 6, false, true);
    }
    // The lid: on, or leaning against the +Z side with the crate empty.
    if (!open) {
        b.push();
        b.move(0, 0.60, 0);
        woodLid(b, p, lidCol, metal);
        b.pop();
    } else {
        b.push();
        leanLid(b, 0.60, 0.36, 0.38);
        woodLid(b, p, lidCol, metal);
        b.pop();
        // Straw and a crushed wrapper left inside.
        b.style(V3(0.7, 0.6, 0.28), MATTE);
        for (int i = 0; i < 5; i++) {
            const double x = (hash01(3, i) - 0.5) * 0.8, z = (hash01(4, i) - 0.5) * 0.5, a = hash01(5, i) * PI;
            b.beam(V3(x - 0.1 * std::cos(a), 0.0745, z - 0.1 * std::sin(a)), V3(x + 0.1 * std::cos(a), 0.0745, z + 0.1 * std::sin(a)), 0.012, 0.006, V3(0, 1, 0));
        }
        b.style(V3(0.62, 0.6, 0.52), MATTE);
        b.box(V3(0.15, 0.085, -0.1), V3(0.22, 0.02, 0.16));
    }
}

// -- B: steel expedition box ----------------------------------------------------------------

// The lid, built about the hinge axis (z = 0 at the hinge, the lid reaching to +Z), its underside y = 0.
void steelLid(B& b, const P& p, const V3& col, const V3& trim) {
    b.style(col, PAINT);
    b.box(V3(0, 0.12, 0.38), V3(1.1, 0.24, 0.76), 0.035);
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    b.box(V3(0, -0.004, 0.38), V3(0.98, 0.012, 0.66));   // foam liner
    b.style(col * 0.85, PAINT);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.244, 0.38 + s * 0.22), V3(0.92, 0.01, 0.05));   // pressed ribs
    b.style(trim, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.46, 0.247, 0.38 + s * 0.3), V3(0.1, 0.014, 0.08));   // stacking pads
    // Latch levers on the +Z side, hinge leaves on the back.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(paint(4) * 0.95, PAINT);
        b.box(V3(sx * 0.43, 0.0, 0.7665), V3(0.04, 0.13, 0.01), 0.003);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        xcyl(b, sx * 0.43 - 0.024, sx * 0.43 + 0.024, 0.055, 0.772, 0.009, 6);
        b.style(trim, METAL);
    }
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 0.3, 0.07, -0.004), V3(0.1, 0.1, 0.006));
    // Amber reflectors on the front corners.
    b.style(V3(0.95, 0.55, 0.1), EMISSIVE);
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * 0.5, 0.12, 0.7625), V3(0.05, 0.05, 0.004));
    // Stencil on the front of the lid and the top.
    b.style(paint(8) * 0.95, MATTE);
    b.push();
    b.move(0, 0.12, 0.761);
    b.yaw(-90);
    b.letters(labelText(p), V3(0, 0, 0), 0.022, 0.004);
    b.pop();
    b.push();
    b.move(0, 0.2495, 0.38);
    b.yaw(-90);
    b.pitch(90);
    b.letters("SUPPLY", V3(0, 0, 0), 0.021, 0.004);
    b.pop();
    b.push();
    b.move(0, 0.12, -0.001);
    b.yaw(90);
    b.letters(labelText(p), V3(0, 0, 0), 0.022, 0.003);
    b.pop();
}

void crateSteel(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    const V3 col = paint(p.i("paint")), trim = col * 0.55 + V3(0.04, 0.04, 0.04);
    // Feet, floor, four walls and corner posts of the tray (y 0.03 .. 0.40).
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.44, 0.015, s * 0.3), V3(0.14, 0.03, 0.12));
    b.style(col, PAINT);
    b.box(V3(0, 0.045, 0), V3(1.08, 0.03, 0.74), 0.012);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 0.215, s * 0.355), V3(1.08, 0.37, 0.03), 0.012);
        b.box(V3(s * 0.525, 0.215, 0), V3(0.03, 0.37, 0.68), 0.012);
    }
    b.style(trim, PAINT);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.515, 0.215, s * 0.345), V3(0.06, 0.37, 0.06), 0.01);
    // Pressed ribs on the long sides and ends.
    b.style(col * 0.88, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 4; i++) b.box(V3(-0.36 + 0.24 * i, 0.21, s * 0.3735), V3(0.04, 0.26, 0.007));
        for (int e = -1; e <= 1; e += 2) b.box(V3(s * 0.5435, 0.21, e * 0.19), V3(0.007, 0.26, 0.04));
    }
    endStencil(b, labelText(p), 0.13, 0.5455, 0.013, paint(8) * 0.95);
    // Gasket, tray latch catches, hinge leaves and barrels.
    b.style(V3(0.05, 0.05, 0.055), MATTE);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 0.395, s * 0.3725), V3(1.09, 0.012, 0.025));
        b.box(V3(s * 0.5275, 0.395, 0), V3(0.025, 0.012, 0.72));
    }
    b.style(trim, METAL);
    for (int sx = -1; sx <= 1; sx += 2) {
        b.box(V3(sx * 0.43, 0.33, 0.378), V3(0.07, 0.08, 0.014));
        b.box(V3(sx * 0.3, 0.34, -0.375), V3(0.1, 0.1, 0.006));
        xcyl(b, sx * 0.3 - 0.05, sx * 0.3 + 0.05, 0.4, -0.38, 0.014, 8);
    }
    // Carry handles on the ends.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(V3(0.1, 0.1, 0.11), METAL);
        for (int s = -1; s <= 1; s += 2) b.box(V3(sx * 0.548, 0.27, s * 0.1), V3(0.012, 0.05, 0.04));
        b.style(paint(4) * 0.9, MATTE);
        b.tube({V3(sx * 0.55, 0.27, -0.1), V3(sx * 0.585, 0.27, -0.1), V3(sx * 0.585, 0.27, 0.1), V3(sx * 0.55, 0.27, 0.1)}, 0.011, 6);
    }
    // The lid: hinged shut, or off and leaning against the front (+Z) side.
    if (!open) {
        b.push();
        b.move(0, 0.40, -0.38);
        steelLid(b, p, col, trim);
        b.pop();
    } else {
        b.push();
        leanLid(b, 0.40, 0.37, 0.38, 0.04);
        b.move(0, 0, -0.38);
        steelLid(b, p, col, trim);
        b.pop();
        b.style(V3(0.62, 0.6, 0.52), MATTE);
        b.box(V3(-0.2, 0.067, 0.1), V3(0.3, 0.012, 0.2), 0.004);   // a crushed wrapper
    }
}

// -- C: pallet with a strapped tarp-covered load -----------------------------------------------

void cratePallet(B& b, const P& p) {
    const bool open = p.i("open") != 0;
    static const V3 tarps[] = {V3(0.14, 0.32, 0.62), V3(0.85, 0.42, 0.08), V3(0.3, 0.34, 0.2), V3(0.58, 0.6, 0.62)};
    const V3 tarp = tarps[std::clamp(p.i("tarp"), 0, 3)], strapCol(0.95, 0.74, 0.08);
    const double top = 0.122;
    // The pallet: bottom boards, three stringers, top boards.
    b.style(V3(0.55, 0.42, 0.26), WOOD);
    for (int i = -1; i <= 1; i++) b.box(V3(i * 0.5, 0.01, 0), V3(0.1, 0.02, 0.76));
    for (int i = -1; i <= 1; i++) b.box(V3(0, 0.06, i * 0.3), V3(1.1, 0.08, 0.08));
    for (int i = 0; i < 7; i++) plank(b, V3(-0.49 + i * 0.1633, 0.111, 0), V3(0.12, 0.022, 0.76), V3(0.55, 0.42, 0.26), 60 + i);
    if (!open) {
        // The load under its tarp: a flared skirt, the body, lumps from what is underneath.
        b.style(tarp * 0.9, MATTE);
        b.box(V3(0, top + 0.07, 0), V3(1.04, 0.14, 0.72), 0.05);
        b.style(tarp, MATTE);
        b.box(V3(0, top + 0.27, 0), V3(0.98, 0.45, 0.66), 0.09);
        b.blob(V3(-0.15, top + 0.5, 0.02), V3(0.3, 0.1, 0.24), 0.25, 5, 2, 0.5);
        b.blob(V3(0.2, top + 0.49, -0.05), V3(0.22, 0.09, 0.2), 0.25, 9, 2, 0.5);
        b.style(tarp * 0.72, MATTE);
        b.beam(V3(-0.44, top + 0.495, 0.1), V3(0.4, top + 0.505, 0.18), 0.02, 0.012, V3(0, 1, 0), 0.004);   // fold lines
        b.beam(V3(-0.4, top + 0.5, -0.2), V3(0.42, top + 0.5, -0.14), 0.02, 0.012, V3(0, 1, 0), 0.004);
        // Ratchet straps over the top, round the pallet.
        b.style(strapCol, MATTE);
        for (int sx = -1; sx <= 1; sx += 2) {
            const double x = sx * 0.4;
            const double pts[][2] = {{0.36, top + 0.0}, {0.37, top + 0.1}, {0.355, top + 0.3}, {0.32, top + 0.5}, {0.22, top + 0.545}};
            for (int s = -1; s <= 1; s += 2)
                for (int i = 0; i + 1 < 5; i++) {
                    const V3 a(x, pts[i][1], s * pts[i][0]), c(x, pts[i + 1][1], s * pts[i + 1][0]);
                    const V3 d = c - a;
                    b.beam(a, c, 0.045, 0.008, V3(0, -d.z, d.y) * -s);
                }
            b.beam(V3(x, top + 0.545, 0.22), V3(x, top + 0.545, -0.22), 0.045, 0.008, V3(0, 1, 0));
            b.style(V3(0.62, 0.64, 0.68), METAL);
            b.box(V3(x, top + 0.33, 0.362), V3(0.05, 0.075, 0.025), 0.005);   // ratchet
            b.style(strapCol, MATTE);
        }
        sideStencil(b, labelText(p), top + 0.27, 0.331, 0.021, V3(0.1, 0.09, 0.07));
        endStencil(b, labelText(p), top + 0.27, 0.491, 0.016, V3(0.1, 0.09, 0.07));
    } else {
        // Tarp dragged off into a heap, straps cut, a couple of broken planks left on the pallet.
        b.style(tarp, MATTE);
        b.blob(V3(0.0, 0.115, 0.62), V3(0.46, 0.09, 0.24), 0.55, 11, 2, 0.5, 0.0);
        b.blob(V3(0.28, 0.095, 0.8), V3(0.26, 0.06, 0.14), 0.5, 4, 2, 0.5, 0.0);
        b.push();
        b.move(0.1, top + 0.0, 0.38);
        b.roll(-14);
        b.box(V3(0, 0.006, -0.12), V3(0.75, 0.01, 0.26), 0.003);   // a corner still draped over the pallet
        b.pop();
        b.style(strapCol, MATTE);
        b.beam(V3(-0.4, top + 0.004, 0.2), V3(-0.4, top + 0.004, -0.34), 0.045, 0.006, V3(0, 1, 0));
        b.beam(V3(0.4, 0.004, 0.5), V3(0.5, 0.004, 0.72), 0.045, 0.006, V3(0, 1, 0));
        b.style(V3(0.62, 0.64, 0.68), METAL);
        b.box(V3(0.5, 0.008, 0.52), V3(0.05, 0.016, 0.04));
        b.style(V3(0.42, 0.32, 0.19), WOOD);
        b.beam(V3(-0.3, top + 0.011, -0.2), V3(0.1, top + 0.011, 0.1), 0.1, 0.02, V3(0, 1, 0));
        b.beam(V3(0.35, top + 0.011, -0.25), V3(0.0, top + 0.03, -0.1), 0.08, 0.02, V3(0, 1, 0));
    }
}

Reg supplyCrate({"supply_crate", "SUPPLY CRATE", "PARTS", "THE career supply cache found in the world: open it for parts (instance param open: 0 closed, 1 looted)", 1.2,
                 {
                     {"WOODEN MILITARY", "Banded plank crate, rope handles, yellow lid and stencil",
                      {paintParam(1), labelParam(), choice("rope", "ROPE", 0, {"ORANGE", "MANILA", "BLACK"}), openParam()}, crateWood},
                     {"STEEL EXPEDITION", "Pressed-steel box with latches, hinges and end handles",
                      {paintParam(1), labelParam(), openParam()}, crateSteel},
                     {"TARP PALLET", "Strapped tarp-covered load on a wooden pallet",
                      {choice("tarp", "TARP", 1, {"BLUE", "ORANGE", "OLIVE", "GREY"}), labelParam(), openParam()}, cratePallet},
                 }});

//@@END
}  // namespace
