// V8 engines. One 90-degree V8 family builder: a set of choices (deck height, bore spacing, heads, valve covers,
// intake, carb or throttle body, ignition, exhaust, front drive, oil system) fed to one bottom end and one top end.
//   crate_engine_v8:      the pushrod engines: a Chevy-style 350 small block, a 454 big block and a GM LS (6.2, the LS7
//                         7.0 dry sump, the blown 6.2).
//   crate_engine_v8_dohc: a flat-plane 5.0 DOHC twin-turbo hypercar V8 (the same bottom end, its own top end).
// Built bare (as fitted, drivetrain.h): crank axis on X through the origin, +X the front, +Z left, the flywheel face the
// rearmost point. Otherwise on its "mount" (stand, skid or pulled onto timber).
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "drivetrain.h"
#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

constexpr double C45 = 0.70710678118654752;

enum CoverStyle { COV_STAMPED, COV_FINNED, COV_TALL, COV_COILS, COV_CAMS };
enum IntakeStyle { IN_CARB, IN_TRUCK, IN_BLOWER, IN_PLENUM };
enum CleanerStyle { AC_ROUND, AC_OPEN };
enum ExhaustStyle { EX_LOG, EX_HEADERS, EX_TURBO };
enum FrontStyle { FR_VBELT, FR_SERP, FR_GEAR };

const V3 kBlack(0.035, 0.035, 0.04), kRubber(0.05, 0.05, 0.055), kChrome(0.82, 0.83, 0.85), kZinc(0.70, 0.62, 0.36);

// A V8's choices. Lengths in metres; "bank frame": a bank's own frame (inBank), y up its bores from the crank axis,
// z out to its exhaust side, x forward.
struct V8 {
    Eng e;
    double deck = 0.229, bs = 0.1118, stagger = 0.024, xf = 0.255, xr = -0.255;  // crank to deck, bore spacing, left bank lead, block faces
    double bankW = 0.17, skirtZ = 0.165, rail = -0.11, panDepth = 0.245, flyR = 0.17, flangeH = 0.40;
    double headH = 0.10, headW = 0.165, coverH = 0.065, coverW = 0.125;
    int cover = COV_STAMPED, intake = IN_CARB, exhaust = EX_LOG, front = FR_VBELT;
    int barrels = 2, cleaner = AC_ROUND;
    double cleanerR = 0.13, cleanerH = 0.07, intakeLift = 0, bp = 0.10;  // bp: first belt plane, ahead of xf
    bool distributor = true, drySump = false, heavyFront = false, snorkel = true, ls7 = false;
    double portScale = 1;
    V3 blockCol, headCol, coverCol, intakeCol, wireCol, cleanerCol, panCol, accent;
    Mat blockMat = PAINT, headMat = PAINT, coverMat = PAINT, intakeMat = PAINT, cleanerMat = PAINT, panMat = PAINT;
    std::string coverText, intakeText, lidText;

    double len() const { return xf - xr; }
    double cx() const { return (xf + xr) / 2; }
    double headTop() const { return deck + headH; }
    double headLen() const { return len() - stagger - 0.02; }
    double flyFace() const { return xr - 0.063; }
    double bore(int i) const { return cx() + (i - 1.5) * bs; }  // a bore's x in its bank's frame
    // The head's inner (intake) face, at a height y up the bores: world Z and Y (left bank).
    double innerZ(double y) const { return (y - headW / 2) * C45; }
    double innerY(double y) const { return (y + headW / 2) * C45; }
    double valleyTop() const { return innerY(headTop()) + 0.012 + intakeLift; }
};

// A bank-frame point in the engine's frame (s: +1 left bank, -1 right).
V3 bankPt(const V8& v, int s, const V3& l) { return V3(l.x + s * v.stagger / 2, (l.y - l.z) * C45, s * (l.y + l.z) * C45); }
// Builds `f` in bank s's frame (the right bank mirrored: z is always outward).
void inBank(B& b, const V8& v, int s, const std::function<void()>& f) {
    b.push();
    if (s < 0) b.mirrorZ();
    b.roll(45);
    b.move(s * v.stagger / 2, 0, 0);
    f();
    b.pop();
}
// Letters on an upward face in bank s, reading along the engine for someone standing beside that bank.
void bankLetters(B& b, const V8& v, int s, const std::string& t, double x, double y, double zOut, double px) {
    b.push();
    b.roll(s * 45);
    b.move(x + s * v.stagger / 2, y, s * zOut);
    b.pitch(90);
    b.roll(-s * 90);
    b.letters(t, V3(), px, 0.002);
    b.pop();
}
// Letters on an upward face, reading along +X for someone standing on the left.
void alongLetters(B& b, const std::string& t, const V3& at, double px) {
    b.push();
    b.move(at);
    b.pitch(90);
    b.roll(-90);
    b.letters(t, V3(), px, 0.002);
    b.pop();
}
// Letters on an upward face, reading from the front.
void frontTopLetters(B& b, const std::string& t, const V3& at, double px) {
    b.push();
    b.move(at);
    b.pitch(90);
    b.letters(t, V3(), px, 0.002);
    b.pop();
}
// A prism along X of cross-section `poly` (world Z, Y), from x0 to x1.
void prismX(B& b, const std::vector<V2>& poly, double x0, double x1) {
    b.push();
    b.yaw(90);
    std::vector<V2> q;
    for (const V2& p : poly) q.push_back(V2(-p.x, p.y));
    b.extrude(q, x1 - x0, (x0 + x1) / 2);
    b.pop();
}
// A pulley (a plain turned disc; the belt shows the groove) from c.x forward by w.
void pulley(B& b, const V3& c, double r, double w, int segs = 10) { xcyl(b, c.x, c.x + w, c.y, c.z, r, segs); }

// A drive belt round pulleys seen along X (a cheaper beltX: 6 points a pulley, its outer face and two sides).
void belt(B& b, double x, const std::vector<Circ>& cs, double w, const V3& col) {
    struct Pt {
        double u, v;
    };
    std::vector<Pt> pts;
    for (const Circ& c : cs)
        for (int i = 0; i < 6; i++) {
            const double a = 2 * PI * (i + 0.5) / 6;
            pts.push_back({c.z + (c.r + 0.003) / std::cos(PI / 6) * std::cos(a), c.y + (c.r + 0.003) / std::cos(PI / 6) * std::sin(a)});
        }
    std::sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& c) { return a.u < c.u || (a.u == c.u && a.v < c.v); });
    auto cr = [](const Pt& o, const Pt& a, const Pt& c) { return (a.u - o.u) * (c.v - o.v) - (a.v - o.v) * (c.u - o.u); };
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
    const V3 dx(w / 2, 0, 0);
    for (size_t i = 0; i < h.size(); i++) {
        const Pt &p = h[i], &q = h[(i + 1) % h.size()];
        const double du = q.u - p.u, dv = q.v - p.v, l = std::hypot(du, dv), nu = dv / l, nv = -du / l, t = 0.006;
        const V3 po(x, p.v + nv * t, p.u + nu * t), qo(x, q.v + nv * t, q.u + nu * t), pi(x, p.v, p.u), qi(x, q.v, q.u);
        b.quad(po - dx, qo - dx, qo + dx, po + dx);
        b.quad(po + dx, qo + dx, qi + dx, pi + dx);
        b.quad(pi - dx, qi - dx, qo - dx, po - dx);
    }
}

// A woven-carbon look on a flat face: fine diagonal stripes of the second tone (a twill), 2 mm proud. o a corner, u and w
// its edges, n outward, sp the stripe pitch.
void weave(B& b, const V3& o, const V3& u, const V3& w, const V3& n, double sp, const V3& alt) {
    const double Lu = len(u), Lw = len(w);
    const V3 eu = u / Lu, ew = w / Lw, off = n * 0.002;
    b.style(alt, PAINT);
    auto clip = [](const std::vector<V2>& in, double c, double sgn) {  // keep sgn * (x + y - c) >= 0
        std::vector<V2> out;
        for (size_t i = 0; i < in.size(); i++) {
            const V2 &p = in[i], &q = in[(i + 1) % in.size()];
            const double fp = sgn * (p.x + p.y - c), fq = sgn * (q.x + q.y - c);
            if (fp >= 0) out.push_back(p);
            if ((fp >= 0) != (fq >= 0)) {
                const double t = fp / (fp - fq);
                out.push_back(V2(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t));
            }
        }
        return out;
    };
    for (double c0 = 0; c0 < Lu + Lw; c0 += sp) {
        const std::vector<V2> poly = clip(clip({V2(0, 0), V2(Lu, 0), V2(Lu, Lw), V2(0, Lw)}, c0, 1), c0 + sp * 0.5, -1);
        auto at = [&](const V2& q) { return o + eu * q.x + ew * q.y + off; };
        for (size_t i = 1; i + 1 < poly.size(); i++) b.triN(at(poly[0]), at(poly[i]), at(poly[i + 1]), n, n, n);
    }
}

// ---- Bottom end: block, flange and flywheel, pan, starter, filter -----------------------------

void bottomEnd(B& b, const V8& v) {
    const Eng& e = v.e;
    const double L = v.len(), cx = v.cx();
    b.style(v.blockCol, v.blockMat);
    b.box(V3(cx, (v.rail + 0.09) / 2, 0), V3(L, 0.09 - v.rail, 2 * v.skirtZ), 0.016);
    for (int s = 1; s >= -1; s -= 2)
        inBank(b, v, s, [&] {
            b.style(v.blockCol, v.blockMat);
            b.box(V3(cx, (0.04 + v.deck) / 2, 0), V3(L - v.stagger, v.deck - 0.04, v.bankW), 0.012);
            b.style(grime(e, V3(0.55, 0.47, 0.3), 0.7), METAL);  // core plugs
            for (int i = 0; i < 2; i++) nut(b, V3(cx + (i - 0.5) * v.bs * 2, v.deck * 0.5, v.bankW / 2 + 0.002), 2, 0.032, 0.006);
        });
    // Bellhousing flange and the flywheel (its rear face is the engine's rearmost plane).
    rearFace(b, e, v.xr - 0.03, 0.03 + (0.40 - v.flangeH) / 2, 2 * v.skirtZ + 0.06, v.flangeH, v.flyR, v.blockCol * 0.94, v.blockMat);
    // Oil pan.
    b.style(grime(e, V3(0.2, 0.2, 0.21), 0.5), METAL);
    b.box(V3(cx, v.rail - 0.004, 0), V3(L - 0.004, 0.012, 0.29));
    if (v.drySump) {
        b.style(v.panCol, v.panMat);
        b.box(V3(cx, v.rail - 0.004 - v.panDepth / 2 + 0.006, 0), V3(L - 0.03, v.panDepth - 0.012, 0.26), 0.02);
        b.style(grime(e, V3(0.5, 0.52, 0.56), 0.6), METAL);
        for (int i = 0; i < 3; i++) zcyl(b, cx - 0.15 + 0.15 * i, v.rail - v.panDepth * 0.55, 0.12, 0.16, 0.016, 8);  // scavenge fittings
    } else {
        oilPan(b, v.panCol, v.panMat, v.xf - 0.006, v.xr + 0.006, v.rail + 0.001, v.rail - 0.075, -v.panDepth, 0.27);
        b.style(grime(e, V3(0.2, 0.2, 0.21), 0.5), METAL);
        b.box(V3(v.xr + 0.09, -v.panDepth - 0.003, 0.05), V3(0.024, 0.008, 0.024));  // drain plug
    }
    // Starter (right, at the bellhousing), oil filter (left rear), dipstick.
    b.style(grime(e, V3(0.3, 0.3, 0.31), 0.7), METAL);
    xcyl(b, v.xr + 0.002, v.xr + 0.19, -0.085, -(v.skirtZ + 0.035), 0.046, 10);
    b.style(grime(e, V3(0.62, 0.62, 0.6), 0.7), METAL);
    xcyl(b, v.xr + 0.06, v.xr + 0.17, -0.03, -(v.skirtZ + 0.03), 0.024, 8);
    if (!v.drySump) {
        b.style(grime(e, v.ls7 ? V3(0.1, 0.1, 0.11) : V3(0.12, 0.25, 0.55), 0.6), PAINT);
        b.cyl(V3(v.xr + 0.08, v.rail + 0.01, v.skirtZ - 0.01), V3(v.xr + 0.08, v.rail - 0.07, v.skirtZ + 0.08), 0.046, 10);
    }
    b.style(grime(e, V3(0.45, 0.45, 0.46), 0.5), METAL);
    b.tube({V3(v.xf - 0.12, v.rail + 0.02, v.skirtZ + 0.004), V3(v.xf - 0.1, 0.08, v.skirtZ + 0.03), V3(v.xf - 0.07, 0.2, v.skirtZ + 0.05)}, 0.005, 4);
    b.style(V3(0.85, 0.65, 0.1), MATTE);
    b.box(V3(v.xf - 0.07, 0.21, v.skirtZ + 0.05), V3(0.03, 0.012, 0.02));
    if (e.wear > 0.1) {
        rustSpots(b, e.seed + 7, V3(cx, -v.panDepth * 0.6, 0.137), V3(L * 0.8, v.panDepth * 0.5, 0), (int)(2 + 5 * e.wear), 2, 0.138);
        b.style(V3(0.1, 0.085, 0.06), MATTE);  // oil weeping down the pan
        b.box(V3(v.xr + 0.12, v.rail - 0.06, -0.137), V3(0.1, 0.09, 0.004));
    }
}

// ---- Heads and covers -----------------------------------------------------------------------

void valveCover(B& b, const V8& v) {
    const Eng& e = v.e;
    const double cx = v.cx(), cl = v.headLen() - 0.035, w = v.coverW, h = v.coverH, y0 = v.headTop();
    b.style(V3(0.08, 0.075, 0.07), MATTE);
    b.box(V3(cx, y0 + 0.004, 0), V3(cl + 0.012, 0.008, w + 0.012));
    const double yb = y0 + 0.008, yt = yb + h;
    b.style(v.coverCol, v.coverMat);
    switch (v.cover) {
    case COV_STAMPED:
        b.box(V3(cx, yb + h / 2, 0), V3(cl, h, w), 0.024);
        b.box(V3(cx, yt + 0.003, 0), V3(cl * 0.6, 0.012, w * 0.42), 0.004);
        b.style(grime(e, V3(0.6, 0.6, 0.62), 0.5), METAL);
        for (int i = -1; i <= 1; i += 2)
            for (int k = -1; k <= 1; k += 2) nut(b, V3(cx + i * cl * 0.3, yb + 0.012, k * (w / 2 + 0.003)), 2, 0.016, 0.008);
        b.style(v.coverMat == CHROME ? kChrome : kBlack, v.coverMat == CHROME ? CHROME : PAINT);
        b.cyl(V3(cx + cl * 0.36, yt - 0.01, 0), V3(cx + cl * 0.36, yt + 0.022, 0), 0.026, 8);  // breather / filler
        break;
    case COV_FINNED:
        b.box(V3(cx, yb + h / 2, 0), V3(cl, h, w), 0.012);
        for (int f = 0; f < 6; f++) b.box(V3(cx, yt + 0.005, (f - 2.5) * w * 0.14), V3(cl * 0.86, 0.014, 0.007));
        b.box(V3(cx + cl * 0.5 - 0.012, yb + h * 0.45, 0), V3(0.012, h * 0.7, w * 0.8));  // cast end lip
        b.style(kBlack, PAINT);
        b.cyl(V3(cx + cl * 0.43, yt + 0.004, 0), V3(cx + cl * 0.43, yt + 0.03, 0), 0.022, 8);
        break;
    case COV_TALL:
        b.box(V3(cx, yb + h * 0.18, 0), V3(cl, h * 0.36, w + 0.008), 0.008);
        b.box(V3(cx, yb + h / 2 + 0.004, 0), V3(cl - 0.016, h - 0.008, w * 0.9), 0.03);
        b.style(v.coverMat == CHROME ? kChrome : kBlack, v.coverMat == CHROME ? CHROME : PAINT);
        b.cyl(V3(cx - cl * 0.36, yt - 0.01, 0), V3(cx - cl * 0.36, yt + 0.028, 0), 0.03, 10);
        b.style(grime(e, V3(0.6, 0.6, 0.62), 0.5), METAL);
        for (int i = -2; i <= 2; i++) nut(b, V3(cx + i * cl * 0.2, yb + 0.012, w / 2 + 0.006), 2, 0.016, 0.008);
        break;
    case COV_COILS: {
        b.box(V3(cx, yb + h / 2, 0), V3(cl, h, w), 0.018);
        b.box(V3(cx, yt + 0.002, -w * 0.2), V3(cl * 0.8, 0.012, 0.026), 0.004);
        b.style(grime(e, V3(0.25, 0.25, 0.27), 0.5), METAL);
        const double zc = w * 0.2;
        b.box(V3(cx, yt + 0.005, zc), V3(cl * 0.86, 0.01, 0.045), 0.003);  // coil bracket
        for (int i = 0; i < 4; i++) {
            const double x = v.bore(i);
            b.style(grime(e, V3(0.07, 0.07, 0.075), 0.3), PAINT);
            b.box(V3(x, yt + 0.045, zc), V3(0.05, 0.075, 0.032));
            b.style(V3(0.2, 0.2, 0.22), MATTE);
            b.box(V3(x, yt + 0.07, zc - 0.022), V3(0.022, 0.016, 0.014));  // connector
            b.style(v.wireCol, MATTE);
            b.tube({V3(x - 0.012, yt + 0.02, zc + 0.016), V3(x - 0.016, yt - h * 0.3, w / 2 + 0.03), V3(x - 0.02, v.deck + 0.065, v.headW / 2 + 0.01)}, 0.007, 4, false, false);
        }
        break;
    }
    case COV_CAMS: {
        b.box(V3(cx, yb + h / 2, 0), V3(cl, h, w), 0.016);
        for (int k = -1; k <= 1; k += 2) b.box(V3(cx, yt + 0.004, k * w * 0.27), V3(cl * 0.94, 0.018, w * 0.3), 0.008);  // the two cams
        b.style(grime(e, V3(0.07, 0.07, 0.075), 0.3), PAINT);
        for (int i = 0; i < 4; i++) b.box(V3(v.bore(i), yt + 0.009, 0), V3(0.045, 0.022, 0.05));  // coil-on-plug tops
        b.style(V3(0.62, 0.63, 0.66), METAL);
        for (int i = 0; i < 3; i++) nut(b, V3(v.bore(i) + v.bs / 2, yt + 0.002, 0), 1, 0.014, 0.006);
        b.style(kRubber, MATTE);
        b.tube({V3(v.bore(0) - 0.03, yt + 0.024, 0.0), V3(v.bore(3) + 0.03, yt + 0.024, 0.0)}, 0.008, 4);
        break;
    }
    }
}

void heads(B& b, const V8& v) {
    const Eng& e = v.e;
    const double hl = v.headLen(), cx = v.cx();
    for (int s = 1; s >= -1; s -= 2)
        inBank(b, v, s, [&] {
            b.style(v.headCol, v.headMat);
            b.box(V3(cx, v.deck + v.headH / 2, 0), V3(hl, v.headH, v.headW), 0.014);
            b.style(grime(e, V3(0.1, 0.1, 0.1), 0.2), MATTE);
            b.box(V3(cx, v.deck + 0.003, 0), V3(hl + 0.006, 0.006, v.headW + 0.006));
            if (v.cover == COV_CAMS) {  // cam-drive housing on the front of the head
                b.style(v.headCol * 0.95, v.headMat);
                b.box(V3(cx + hl / 2 + 0.012, v.deck + v.headH * 0.55, 0), V3(0.026, v.headH * 1.05, v.headW * 0.96));
            }
            valveCover(b, v);
            if (v.distributor) {  // spark plug boots on the exhaust side
                b.style(v.wireCol, MATTE);
                for (int i = 0; i < 4; i++) b.box(V3(v.bore(i) + 0.035, v.deck + 0.075, v.headW / 2 + 0.012), V3(0.022, 0.022, 0.03));
            }
        });
    // Lettering on the covers (built unmirrored).
    if (!v.coverText.empty()) {
        b.style(v.coverMat == CHROME ? V3(0.06, 0.06, 0.07) : (v.cover == COV_CAMS ? V3(0.85, 0.85, 0.86) : V3(0.9, 0.88, 0.82)), PAINT);
        const double top = v.headTop() + 0.008 + v.coverH + (v.cover == COV_STAMPED ? 0.009 : v.cover == COV_CAMS ? 0.0 : 0.0);
        const bool cams = v.cover == COV_CAMS;
        for (int s = 1; s >= -1; s -= 2)
            bankLetters(b, v, s, v.coverText, cams ? v.cx() + v.headLen() * 0.24 : v.cx() - 0.01, cams ? top + 0.013 : top, cams ? v.coverW * 0.27 : 0.0, cams ? 0.0055 : 0.0065);
    }
}

// ---- Exhaust -------------------------------------------------------------------------------

void logManifolds(B& b, const V8& v) {
    const Eng& e = v.e;
    const V3 iron = grime(e, V3(0.25, 0.235, 0.225), 0.6);
    const double k = v.portScale, hw = v.headW / 2, y = v.deck + 0.045 * k, cx = v.cx(), hl = v.headLen();
    const double zl = hw + 0.05 * k;
    for (int s = 1; s >= -1; s -= 2) {
        inBank(b, v, s, [&] {
            b.style(iron, METAL);
            for (int i = 0; i < 4; i++) b.box(V3(v.bore(i), y, hw + 0.02), V3(0.058 * k, 0.05 * k, 0.045));
            b.box(V3(cx - 0.01, y - 0.008, zl), V3(hl * 0.86, 0.058 * k, 0.055 * k), 0.022);
            b.box(V3(cx - 0.01, y - 0.008, zl + 0.02 * k), V3(hl * 0.6, 0.012, 0.03 * k));  // casting rib
            if (e.wear > 0.1) rustSpots(b, e.seed + (s > 0 ? 0 : 50), V3(cx, y, zl + 0.029 * k), V3(hl * 0.8, 0.04, 0), (int)(3 + 5 * e.wear), 2, zl + 0.03 * k);
        });
        // The outlet, down at the rear.
        const V3 o = bankPt(v, s, V3(cx - hl * 0.3, y - 0.03 * k, zl));
        b.style(iron, METAL);
        b.cyl(o + V3(0, 0.02, 0), V3(o.x, -0.06, o.z), 0.032 * k, 10);
        b.box(V3(o.x, -0.065, o.z), V3(0.085 * k, 0.012, 0.085 * k));
    }
}

void headers(B& b, const V8& v) {
    const Eng& e = v.e;
    const double hw = v.headW / 2, yp = v.deck + 0.05;
    for (int s = 1; s >= -1; s -= 2) {
        const V3 coll(v.cx() - 0.05, -0.04, s * (v.skirtZ + 0.115));
        b.style(grime(e, V3(0.52, 0.5, 0.48), 0.6), METAL);
        for (int i = 0; i < 4; i++) {
            const V3 p0 = bankPt(v, s, V3(v.bore(i), yp, hw - 0.005)), p1 = bankPt(v, s, V3(v.bore(i), yp - 0.025, hw + 0.06));
            const V3 p2((p1.x + coll.x) / 2, p1.y - 0.07, s * (v.skirtZ + 0.135 + 0.012 * (i % 2)));
            b.tube({p0, p1, p2, coll + V3((i - 1.5) * 0.018, 0.04, s * 0.006 * (i % 2))}, 0.019, 6, false, false);
        }
        b.cone(coll + V3(0, 0.03, 0), coll - V3(0, 0.12, 0), 0.044, 0.032, 10);
        b.style(grime(e, V3(0.32, 0.32, 0.33), 0.6), METAL);
        b.box(coll - V3(0, 0.125, 0), V3(0.09, 0.012, 0.09));
    }
}

// ---- Intakes and carbs ------------------------------------------------------------------------

// The valley block between the heads (the intake's base): a prism from x0 to x1 up to `top`.
void valleyBase(B& b, const V8& v, double x0, double x1, double top) {
    const double y0 = v.deck - 0.02, y1 = v.headTop() - 0.006;
    const double z0 = v.innerZ(y0), Y0 = v.innerY(y0), z1 = v.innerZ(y1), Y1 = v.innerY(y1);
    prismX(b, {V2(-z0 + 0.05, Y0 - 0.05), V2(z0 - 0.05, Y0 - 0.05), V2(z0, Y0), V2(z1, Y1), V2(z1 - 0.012, top), V2(-z1 + 0.012, top), V2(-z1, Y1), V2(-z0, Y0)}, x0, x1);
}

void airCleaner(B& b, const V8& v, const V3& at) {
    const double R = v.cleanerR, H = v.cleanerH;
    if (v.cleaner == AC_OPEN) {
        b.style(kChrome, CHROME);
        b.lathe(at, {V2(0, 0), V2(R + 0.006, 0), V2(R + 0.01, 0.012), V2(0, 0.014)}, 16);
        b.style(grime(v.e, V3(0.78, 0.75, 0.64), 1.2), MATTE);
        b.lathe(at + V3(0, 0.012, 0), {V2(R - 0.012, 0), V2(R - 0.004, 0), V2(R - 0.004, H - 0.022)}, 18, false);
        b.style(kChrome, CHROME);
        b.lathe(at + V3(0, H - 0.012, 0), {V2(0, 0), V2(R + 0.004, 0), V2(R + 0.008, 0.008), V2(R * 0.3, 0.024), V2(0, 0.026)}, 16);
        b.cyl(at + V3(0, H + 0.012, 0), at + V3(0, H + 0.036, 0), 0.012, 6);
        b.box(at + V3(0, H + 0.03, 0), V3(0.05, 0.014, 0.012));
        return;
    }
    b.style(v.cleanerCol, v.cleanerMat);
    b.lathe(at, {V2(0, 0), V2(R - 0.01, 0), V2(R, 0.008), V2(R, H - 0.012), V2(R - 0.014, H), V2(0, H + 0.012)}, 16);
    if (v.snorkel) {
        b.box(at + V3(R + 0.04, H * 0.42, 0), V3(0.13, H * 0.62, 0.095), 0.014);
        b.style(kBlack, MATTE);
        b.box(at + V3(R + 0.106, H * 0.42, 0), V3(0.004, H * 0.5, 0.08));  // its mouth
    }
    b.style(kChrome, CHROME);
    b.cyl(at + V3(0, H + 0.008, 0), at + V3(0, H + 0.03, 0), 0.012, 6);
    b.box(at + V3(0, H + 0.026, 0), V3(0.055, 0.014, 0.012));
    if (!v.lidText.empty()) {
        b.style(v.cleanerMat == CHROME ? V3(0.06, 0.06, 0.07) : V3(0.85, 0.84, 0.8), PAINT);
        frontTopLetters(b, v.lidText, at + V3(-R * 0.45, H + 0.008, 0), 0.009);
    }
}

void carbIntake(B& b, const V8& v) {
    const Eng& e = v.e;
    const double top = v.valleyTop(), x0 = v.xr + 0.07, x1 = v.xf - 0.02, cxI = (x0 + x1) / 2 + 0.01;
    b.style(v.intakeCol, v.intakeMat);
    valleyBase(b, v, x0, x1, top);
    // Runner humps from the carb pad out to the ports.
    const double zp = v.innerZ(v.headTop()) - 0.035;
    for (int s = 1; s >= -1; s -= 2)
        for (int i = 0; i < 4; i++) {
            const double xi = v.bore(i) + s * v.stagger / 2;
            b.tube({V3(cxI + (xi - cxI) * 0.25, top - 0.004 + v.intakeLift * 0.3, s * 0.03), V3(xi, top - 0.008, s * zp)}, 0.02 + v.intakeLift * 0.3, 6, false, false);
        }
    // Carb pad, carb, then the air cleaner.
    const double padW = v.barrels == 4 ? 0.16 : 0.13;
    b.box(V3(cxI, top + 0.008 + v.intakeLift * 0.5, 0), V3(padW, 0.02 + v.intakeLift, padW));
    const double cb = top + 0.018 + v.intakeLift;
    b.style(grime(e, kZinc, 0.6), METAL);
    double ct;
    if (v.barrels == 4) {
        b.box(V3(cxI, cb + 0.004, 0), V3(0.15, 0.008, 0.15));
        b.box(V3(cxI, cb + 0.045, 0), V3(0.13, 0.075, 0.13), 0.012);
        b.box(V3(cxI + 0.07, cb + 0.04, 0), V3(0.035, 0.05, 0.11), 0.01);  // float bowl
        b.style(grime(e, V3(0.62, 0.62, 0.64), 0.5), METAL);
        b.box(V3(cxI - 0.01, cb + 0.04, 0.075), V3(0.06, 0.04, 0.012));  // linkage
        ct = cb + 0.083;
    } else {
        b.box(V3(cxI, cb + 0.004, 0), V3(0.12, 0.008, 0.12));
        b.box(V3(cxI, cb + 0.035, 0), V3(0.09, 0.06, 0.1), 0.01);
        b.style(grime(e, V3(0.62, 0.62, 0.64), 0.5), METAL);
        b.box(V3(cxI, cb + 0.03, 0.058), V3(0.05, 0.035, 0.012));
        ct = cb + 0.065;
    }
    airCleaner(b, v, V3(cxI, ct, 0));
    // Thermostat housing and the upper hose stub at the front.
    b.style(grime(e, V3(0.28, 0.27, 0.26), 0.6), METAL);
    b.cone(V3(x1 - 0.03, top - 0.01, 0.045), V3(x1 + 0.02, top + 0.025, 0.045), 0.032, 0.024, 8);
    b.style(kRubber, MATTE);
    b.tube({V3(x1 + 0.015, top + 0.022, 0.045), V3(x1 + 0.07, top + 0.035, 0.06), V3(x1 + 0.1, top + 0.03, 0.09)}, 0.021, 6);
}

void distributor(B& b, const V8& v) {
    const Eng& e = v.e;
    const double dx = v.xr + 0.045, top = v.valleyTop();
    const double capY = top + 0.035, R = 0.068, H = 0.075;
    b.style(grime(e, V3(0.32, 0.32, 0.33), 0.6), METAL);
    b.cyl(V3(dx, top - 0.08, 0), V3(dx, capY, 0), 0.034, 8);
    b.style(V3(0.06, 0.06, 0.065), PAINT);
    b.lathe(V3(dx, capY, 0), {V2(0, 0), V2(R, 0), V2(R, H * 0.55), V2(R * 0.6, H), V2(0, H)}, 10);
    b.style(V3(0.55, 0.12, 0.08), PAINT);  // coil cover on the cap
    b.box(V3(dx, capY + H + 0.006, 0), V3(0.06, 0.014, 0.06));
    // Plug wires: cap towers, back over the rear of the cover, along its outer side, down to the plug.
    const double hw = v.headW / 2, ht = v.headTop(), cl = v.headLen() - 0.035;
    b.style(v.wireCol, MATTE);
    for (int s = 1; s >= -1; s -= 2)
        for (int i = 0; i < 4; i++) {
            const double a = (s > 0 ? 0.5 : -0.5) * PI + s * (i - 1.5) * 0.36;
            const V3 tw(dx - R * 0.7 * std::cos(a) + 0.02 * (i - 1.5), capY + H * 0.6, s * R * 0.85);
            const V3 w = bankPt(v, s, V3(v.cx() - cl / 2 - 0.012, ht + v.coverH * (0.85 - 0.12 * i), v.coverW * (0.05 + 0.1 * i)));
            const V3 c = bankPt(v, s, V3(v.cx() - cl / 2 + 0.02 + i * 0.01, ht + 0.02, v.coverW / 2 + 0.012));
            const V3 c2 = bankPt(v, s, V3(v.bore(i) + 0.005, ht + 0.016, v.coverW / 2 + 0.014));
            const V3 pl = bankPt(v, s, V3(v.bore(i) + 0.035, v.deck + 0.075, hw + 0.024));
            b.tube({tw, w, c, c2, pl}, 0.0055, 4, false, false);
        }
}

void truckIntake(B& b, const V8& v) {
    const Eng& e = v.e;
    const double top = v.valleyTop(), x0 = v.xr + 0.05, x1 = v.xf - 0.015, cxI = (x0 + x1) / 2;
    const V3 body = v.intakeCol;
    b.style(body * 0.9, v.intakeMat);
    valleyBase(b, v, x0, x1, top);
    // The plenum along the middle and its eight runners arching out to the ports.
    const double pl = (x1 - x0) * 0.86, ph = 0.1, pyc = top + ph / 2 - 0.01;
    b.style(body, v.intakeMat);
    b.box(V3(cxI + 0.01, pyc, 0), V3(pl, ph, 0.15), 0.035);
    const double zp = v.innerZ(v.headTop()) - 0.02;
    for (int s = 1; s >= -1; s -= 2)
        for (int i = 0; i < 4; i++) {
            const double xi = v.bore(i) + s * v.stagger / 2;
            b.tube({V3(xi, pyc + 0.02, s * 0.03), V3(xi, top + 0.075, s * 0.1), V3(xi, top + 0.012, s * zp)}, 0.025, 6, false, false);
        }
    // Fuel rails.
    b.style(grime(e, V3(0.7, 0.71, 0.73), 0.5), METAL);
    for (int s = 1; s >= -1; s -= 2) b.tube({V3(x0 + 0.04, top + 0.018, s * (zp + 0.004)), V3(x1 - 0.03, top + 0.018, s * (zp + 0.004))}, 0.009, 6);
    if (v.ls7) {  // a silver lid on the red plenum
        b.style(grime(e, V3(0.72, 0.73, 0.75), 0.5), METAL);
        b.box(V3(cxI + 0.01, pyc + ph / 2 - 0.004, 0), V3(pl - 0.05, 0.016, 0.1), 0.006);
    }
    if (!v.intakeText.empty()) {
        b.style(v.ls7 ? V3(0.55, 0.06, 0.05) : V3(0.72, 0.72, 0.74), PAINT);
        alongLetters(b, v.intakeText, V3(cxI + 0.01, pyc + ph / 2 + (v.ls7 ? 0.004 : 0.0), 0), 0.006);
    }
    // Drive-by-wire throttle body at the front.
    const double tx = cxI + 0.01 + pl / 2;
    b.style(grime(e, V3(0.66, 0.67, 0.7), 0.6), METAL);
    xcyl(b, tx - 0.01, tx + 0.055, pyc, 0, 0.05, 12);
    b.style(kBlack, MATTE);
    xcyl(b, tx + 0.055, tx + 0.058, pyc, 0, 0.042, 10);
    b.style(V3(0.08, 0.08, 0.085), PAINT);
    b.box(V3(tx + 0.025, pyc, 0.06), V3(0.05, 0.07, 0.028), 0.008);  // throttle motor
}

void blowerTop(B& b, const V8& v) {
    const Eng& e = v.e;
    const double top = v.valleyTop(), x0 = v.xr + 0.05, x1 = v.xf - 0.01;
    b.style(grime(e, V3(0.6, 0.61, 0.63), 0.8), METAL);
    valleyBase(b, v, x0, x1, top);
    // The case: a ribbed roots casting with an intercooler lid.
    const double bl = 0.44, bx = v.cx() + 0.04, H = 0.15, W = 0.27, f0 = bx - bl / 2, f1 = bx + bl / 2;
    const V3 caseCol = grime(e, V3(0.66, 0.67, 0.69), 0.7);
    b.style(caseCol, METAL);
    b.box(V3(bx, top + 0.008, 0), V3(bl + 0.01, 0.016, W + 0.02), 0.004);
    std::vector<V2> sec;
    for (const V2& p : roundedRect(W, H, 0.045, 3)) sec.push_back(V2(p.x, p.y + top + 0.016 + H / 2));
    prismX(b, sec, f0, f1);
    b.style(caseCol * 0.92, METAL);
    for (int s = -1; s <= 1; s += 2)
        for (int r = 0; r < 2; r++) b.box(V3(bx, top + 0.045 + r * 0.085, s * (W / 2 + 0.003)), V3(bl * 0.9, 0.009, 0.009));
    const double lidY = top + 0.016 + H;
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.3), PAINT);
    b.box(V3(bx, lidY + 0.018, 0), V3(bl * 0.97, 0.036, W - 0.01), 0.012);
    b.style(grime(e, V3(0.7, 0.71, 0.73), 0.5), METAL);
    for (int f = 0; f < 7; f++) b.box(V3(bx, lidY + 0.04, (f - 3) * 0.032), V3(bl * 0.86, 0.01, 0.008));
    b.style(V3(0.08, 0.08, 0.09), PAINT);
    b.push();  // (on the left side only: letters are dear)
    b.move(bx, top + 0.1, W / 2 + 0.002);
    b.yaw(-90);
    b.letters("SUPERCHARGED", V3(), 0.0045, 0.002);
    b.pop();
    // Snout, its pulley and own belt round an idler and the crank's front pulley.
    const double bpB = v.xf + v.bp + 0.06, ys = top + 0.016 + H * 0.62;
    b.style(caseCol, METAL);
    b.cone(V3(f1 - 0.01, ys, 0), V3(bpB - 0.018, ys, 0), 0.06, 0.036, 10);
    b.style(grime(e, V3(0.15, 0.15, 0.16), 0.4), METAL);
    pulley(b, V3(bpB - 0.018, ys, 0), 0.058, 0.036, 12);
    pulley(b, V3(bpB - 0.018, 0, 0), 0.092, 0.036, 12);
    const double iz = -0.13, iy = 0.3;
    b.style(V3(0.6, 0.61, 0.63), METAL);
    b.box(V3(bpB - 0.03, iy + 0.02, iz + 0.04), V3(0.012, 0.12, 0.12));
    pulley(b, V3(bpB - 0.018, iy, iz), 0.036, 0.036, 8);
    belt(b, bpB, {{0, ys, 0.058}, {iz, iy, 0.036}, {0, 0, 0.092}}, 0.03, kBlack);
    // Throttle body at the case's front inlet.
    b.style(grime(e, V3(0.66, 0.67, 0.7), 0.6), METAL);
    xcyl(b, f1 - 0.01, f1 + 0.06, top + 0.07, 0.085, 0.046, 12);
    b.style(kBlack, MATTE);
    xcyl(b, f1 + 0.06, f1 + 0.063, top + 0.07, 0.085, 0.038, 12);
    b.style(V3(0.08, 0.08, 0.085), PAINT);
    b.box(V3(f1 + 0.03, top + 0.07, 0.14), V3(0.045, 0.06, 0.026), 0.008);
}

// ---- Front drives -----------------------------------------------------------------------------

void vbeltFront(B& b, const V8& v) {
    const Eng& e = v.e;
    const double xf = v.xf, bp = xf + v.bp, k = v.heavyFront ? 1.1 : 1.0, gw = 0.026;
    const int grooves = v.heavyFront ? 3 : 2;
    const V3 pul = grime(e, V3(0.16, 0.16, 0.17), 0.4), alu = grime(e, V3(0.6, 0.61, 0.63), 0.9);
    // Timing cover, balancer and the crank pulleys.
    b.style(v.blockCol, v.blockMat);
    b.box(V3(xf + 0.012, -0.01, 0), V3(0.024, 0.2, 0.21), 0.012);
    b.style(grime(e, V3(0.2, 0.2, 0.21), 0.4), METAL);
    xlathe(b, V3(xf + 0.024, 0, 0), {V2(0, 0), V2(0.092 * k, 0), V2(0.092 * k, 0.04), V2(0.035, 0.045), V2(0.035, bp - xf - 0.036)}, 12);
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.012, 0, 0), 0.076 * k, grooves * gw - 0.002, 12);
    // Long water pump with a double pulley and the fan hub.
    const double wy = 0.19 * k;
    b.style(v.blockCol, v.blockMat);
    xlathe(b, V3(xf, wy, 0), {V2(0, 0), V2(0.075, 0), V2(0.07, 0.03), V2(0.03, 0.06), V2(0.022, bp - xf - 0.012)}, 10);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(xf + 0.01, wy - 0.02, 0), V3(xf + 0.01, wy - 0.075, s * 0.11), 0.04, 0.035, V3(1, 0, 0));
    b.style(kRubber, MATTE);
    b.tube({V3(xf + 0.03, wy - 0.055, -0.07), V3(xf + 0.05, wy - 0.07, -0.13), V3(xf + 0.08, wy - 0.06, -0.17)}, 0.02, 6);
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.012, wy, 0), 0.07, 2 * gw - 0.002, 12);
    xcyl(b, bp + 0.04, bp + 0.058, wy, 0, 0.03, 8);
    // Alternator (right, high) on its bracket.
    const double az = -0.2 * k, ay = 0.215 * k;
    b.style(alu, METAL);
    xcyl(b, xf - 0.02, bp - 0.035, ay, az, 0.064, 12);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, bp - 0.035, bp - 0.016, ay, az, 0.052, 10, false);
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.012, ay, az), 0.034, 0.024, 8);
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.6), METAL);
    b.beam(V3(xf + 0.03, ay - 0.04, az + 0.02), V3(xf + 0.0, ay - 0.1, az + 0.1), 0.03, 0.012, V3(1, 0, 0));
    // Power steering pump (left, low), on the second groove.
    const double pz = 0.2 * k, py = 0.06;
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.3), PAINT);
    xcyl(b, xf - 0.04, bp + gw - 0.02, py, pz, 0.045, 10);
    b.cyl(V3(xf + 0.02, py + 0.03, pz), V3(xf + 0.02, py + 0.09, pz), 0.036, 8);  // reservoir
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.012 + gw, py, pz), 0.06, 0.024, 10);
    belt(b, bp, {{0, 0, 0.076 * k}, {0, wy, 0.07}, {az, ay, 0.034}}, 0.017, kBlack);
    belt(b, bp + gw, {{0, 0, 0.076 * k}, {pz, py, 0.06}, {0, wy, 0.07}}, 0.017, kBlack);
    if (v.heavyFront) {  // the big block's AC compressor, on the third groove
        const double cz = -0.215, cy = -0.06;
        b.style(grime(e, V3(0.55, 0.56, 0.58), 0.8), METAL);
        xcyl(b, xf - 0.14, bp + 2 * gw - 0.02, cy, cz, 0.068, 12);
        b.style(V3(0.1, 0.1, 0.11), METAL);
        xcyl(b, xf - 0.1, xf - 0.04, cy + 0.06, cz, 0.022, 6);
        b.style(pul, METAL);
        pulley(b, V3(bp - 0.012 + 2 * gw, cy, cz), 0.064, 0.024, 10);
        belt(b, bp + 2 * gw, {{0, 0, 0.076 * k}, {cz, cy, 0.064}}, 0.017, kBlack);
    }
}

void serpFront(B& b, const V8& v) {
    const Eng& e = v.e;
    const double xf = v.xf, bp = xf + v.bp;
    const V3 pul = grime(e, V3(0.16, 0.16, 0.17), 0.4), alu = grime(e, V3(0.6, 0.61, 0.63), 0.9);
    b.style(v.blockCol * 0.96, v.blockMat);
    b.box(V3(xf + 0.012, -0.02, 0), V3(0.024, 0.22, 0.24), 0.012);
    b.style(grime(e, V3(0.2, 0.2, 0.21), 0.4), METAL);
    xlathe(b, V3(xf + 0.024, 0, 0), {V2(0, 0), V2(0.095, 0), V2(0.095, 0.045), V2(0.04, 0.05), V2(0.04, bp - xf - 0.04)}, 12);
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.015, 0, 0), 0.085, 0.03, 12);
    // The big cast water pump across the front, its outlets up to the heads.
    const double wy = 0.17;
    b.style(alu, METAL);
    b.box(V3(xf + 0.035, wy - 0.01, 0), V3(0.07, 0.13, 0.27), 0.03);
    xlathe(b, V3(xf + 0.07, wy, 0), {V2(0.05, 0), V2(0.03, 0.03), V2(0.02, bp - xf - 0.085)}, 8);
    b.style(kRubber, MATTE);
    b.tube({V3(xf + 0.06, wy + 0.0, -0.1), V3(xf + 0.1, wy + 0.04, -0.12), V3(xf + 0.14, wy + 0.1, -0.14)}, 0.022, 6);
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.015, wy, 0), 0.07, 0.03, 12);
    // Alternator (left, high), power steering pump (right, high), idler and tensioner.
    const double az = 0.2, ay = 0.25, pz = -0.2, py = 0.24, tz = 0.1, ty = 0.08, iz = -0.09, iy = 0.08;
    b.style(alu, METAL);
    xcyl(b, xf + 0.0, bp - 0.035, ay, az, 0.064, 12);
    b.style(V3(0.1, 0.1, 0.11), METAL);
    xcyl(b, bp - 0.035, bp - 0.018, ay, az, 0.05, 10, false);
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.3), PAINT);
    xcyl(b, xf - 0.02, bp - 0.02, py, pz, 0.046, 10);
    b.cyl(V3(xf + 0.03, py + 0.03, pz), V3(xf + 0.03, py + 0.1, pz), 0.038, 8);
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.6), METAL);
    b.box(V3(bp - 0.03, (ty + ay) / 2, tz + 0.02), V3(0.012, ay - ty + 0.05, 0.05));  // brackets
    b.box(V3(bp - 0.03, (iy + py) / 2, iz - 0.02), V3(0.012, py - iy + 0.05, 0.05));
    b.style(pul, METAL);
    pulley(b, V3(bp - 0.015, ay, az), 0.034, 0.03, 8);
    pulley(b, V3(bp - 0.015, py, pz), 0.06, 0.03, 10);
    b.style(grime(e, V3(0.62, 0.63, 0.66), 0.6), METAL);
    pulley(b, V3(bp - 0.015, ty, tz), 0.036, 0.03, 8);
    pulley(b, V3(bp - 0.015, iy, iz), 0.036, 0.03, 8);
    belt(b, bp, {{0, 0, 0.085}, {tz, ty, 0.036}, {az, ay, 0.034}, {0, wy, 0.07}, {pz, py, 0.06}, {iz, iy, 0.036}}, 0.022, kBlack);
}

// ---- Dry sump (LS7) ---------------------------------------------------------------------------

void lsDrySump(B& b, const V8& v) {
    const Eng& e = v.e;
    const double xf = v.xf;
    const V3 tankAt(xf + 0.06, -0.2, 0.27);
    b.style(grime(e, V3(0.1, 0.1, 0.11), 0.3), PAINT);
    b.lathe(tankAt, {V2(0, 0), V2(0.068, 0.02), V2(0.068, 0.27), V2(0, 0.3)}, 10);
    b.style(grime(e, V3(0.7, 0.71, 0.73), 0.5), METAL);
    b.cyl(tankAt + V3(0, 0.3, 0), tankAt + V3(0, 0.325, 0), 0.03, 8);  // filler cap
    b.box(V3(xf + 0.03, -0.05, 0.22), V3(0.05, 0.03, 0.09), 0.006);    // bracket to the block
    // Pump low on the left front, its lines to the tank.
    const V3 pump(xf - 0.03, -0.1, 0.18);
    b.style(grime(e, V3(0.62, 0.63, 0.66), 0.8), METAL);
    xcyl(b, pump.x - 0.12, pump.x + 0.05, pump.y, pump.z, 0.04, 10);
    b.style(V3(0.2, 0.2, 0.22), METAL);
    for (int i = 0; i < 3; i++) xcyl(b, pump.x - 0.1 + i * 0.055, pump.x - 0.09 + i * 0.055, pump.y, pump.z, 0.045, 10, false);
    b.style(V3(0.24, 0.24, 0.26), METAL);
    b.tube({pump + V3(0.05, 0, 0.02), pump + V3(0.08, -0.02, 0.06), tankAt + V3(-0.02, 0.03, -0.05)}, 0.011, 6);
    b.tube({pump + V3(0.02, 0.03, 0.03), V3(xf + 0.03, 0.02, 0.27), tankAt + V3(-0.04, 0.2, -0.04)}, 0.011, 6);
}

// ---- The DOHC top end -------------------------------------------------------------------------

void dohcTop(B& b, const V8& v, bool carbon) {
    const Eng& e = v.e;
    const double top = v.valleyTop(), x0 = v.xr + 0.04, x1 = v.xf - 0.01, xf = v.xf;
    const V3 cfA = V3(0.085, 0.085, 0.095), cfB = V3(0.16, 0.16, 0.175);
    const V3 pol = grime(e, V3(0.8, 0.81, 0.83), 0.8);
    const V3 plenCol = carbon ? cfA : pol;
    const Mat plenMat = carbon ? PAINT : METAL;
    b.style(plenCol, plenMat);
    valleyBase(b, v, x0, x1, top);
    // The sculpted plenum: a long hump with a spine, twin throttle bodies at its front.
    const double p0 = x0 + 0.02, p1 = x1 - 0.03, Y0 = top - 0.004;
    const double zA = 0.155, zB = 0.165, zC = 0.12, zD = 0.05, yB = Y0 + 0.035, yC = Y0 + 0.085, yD = Y0 + 0.105;
    prismX(b, {V2(-zA, Y0), V2(zA, Y0), V2(zB, yB), V2(zC, yC), V2(zD, yD), V2(-zD, yD), V2(-zC, yC), V2(-zB, yB)}, p0, p1);
    const double s0 = p0 + 0.04, s1 = p1 - 0.03;
    prismX(b, {V2(-0.04, yD - 0.004), V2(0.04, yD - 0.004), V2(0.03, yD + 0.018), V2(-0.03, yD + 0.018)}, s0, s1);
    if (carbon) {
        const double L = p1 - p0, t = 0.02;
        weave(b, V3(p0, yD, -zD), V3(L, 0, 0), V3(0, 0, 2 * zD), V3(0, 1, 0), t, cfB);
        for (int s = -1; s <= 1; s += 2) {
            const V3 a(p0, yD, s * zD), c(p0, yC, s * zC), d(p0, yB, s * zB);
            V3 n1 = norm(cross(c - a, V3(1, 0, 0))), n2 = norm(cross(d - c, V3(1, 0, 0)));
            if (n1.y < 0) n1 = -n1;
            if (n2.z * s < 0) n2 = -n2;
            weave(b, a, V3(L, 0, 0), c - a, n1, t, cfB);
            (void)n2;
        }
        weave(b, V3(s0, yD + 0.018, -0.03), V3(s1 - s0, 0, 0), V3(0, 0, 0.06), V3(0, 1, 0), 0.018, cfB * 0.85);
    }
    b.style(v.accent, PAINT);
    alongLetters(b, "TWIN TURBO", V3((s0 + s1) / 2, yD + 0.018 + (carbon ? 0.002 : 0.0), 0), 0.0042);
    b.style(pol, METAL);
    for (int s = -1; s <= 1; s += 2) {
        xcyl(b, p1 - 0.01, p1 + 0.045, Y0 + 0.05, s * 0.075, 0.04, 10);
    }
    // Turbos high on each side near the rear, forward of the flywheel face.
    const double tx = v.flyFace() + 0.13, ty = 0.33, tzz = 0.31;
    const V3 hot = grime(e, V3(0.27, 0.24, 0.22), 0.4), ss = grime(e, V3(0.68, 0.65, 0.6), 0.5), tint(0.5, 0.42, 0.5);
    const V3 ti = grime(e, V3(0.56, 0.54, 0.52), 0.5);
    const double hw = v.headW / 2;
    for (int s = 1; s >= -1; s -= 2) {
        const double tz = s * tzz;
        // Turbine housing with its scroll, centre section, compressor and its inlet.
        b.style(hot, METAL);
        xlathe(b, V3(tx - 0.04, ty, tz), {V2(0, 0), V2(0.06, 0), V2(0.075, 0.03), V2(0.05, 0.075), V2(0, 0.075)}, 8);
        b.push();
        b.move(tx - 0.004, ty, tz);
        b.pitch(-90);
        b.torus(V3(), 0.07, 0.032, 9, 5, 300);
        b.pop();
        b.style(grime(e, V3(0.55, 0.5, 0.36), 0.5), METAL);
        xcyl(b, tx + 0.035, tx + 0.07, ty, tz, 0.042, 8, false);
        b.style(pol, METAL);
        xlathe(b, V3(tx + 0.07, ty, tz), {V2(0, 0), V2(0.09, 0.012), V2(0.09, 0.05), V2(0.055, 0.066), V2(0.055, 0.124), V2(0.044, 0.124)}, 10);
        b.style(kBlack, MATTE);
        xcyl(b, tx + 0.17, tx + 0.19, ty, tz, 0.045, 10);
        b.style(V3(0.62, 0.63, 0.66), METAL);
        // Compressor outlet up and forward into the plenum's side.
        b.style(pol, METAL);
        b.tube({V3(tx + 0.1, ty + 0.07, tz), V3(tx + 0.13, ty + 0.115, s * 0.3), V3(p1 - 0.13, Y0 + 0.1, s * 0.25), V3(p1 - 0.06, Y0 + 0.06, s * 0.16)}, 0.033, 8, false, false);
        // Short stainless headers from the ports up into the turbine.
        b.style(ss, METAL);
        for (int i = 0; i < 4; i++) {
            const V3 p0 = bankPt(v, s, V3(v.bore(i), v.deck + 0.055, hw - 0.005)), q1 = bankPt(v, s, V3(v.bore(i), v.deck + 0.045, hw + 0.05));
            const V3 q2(tx + 0.03 + (i - 1.5) * 0.03, ty - 0.15 + 0.01 * (i % 2), s * (tzz + 0.07));
            b.tube({p0, q1, q2, V3(tx + (i - 1.5) * 0.01, ty - 0.06, s * (tzz + 0.03))}, 0.018, 5, false, false);
        }
        b.style(mix(ss, tint, 0.6), METAL);
        b.cone(V3(tx + 0.01, ty - 0.13, s * (tzz + 0.06)), V3(tx, ty - 0.055, s * (tzz + 0.02)), 0.045, 0.04, 8, false);
        // Titanium downpipe: out of the turbine's back, straight down (never behind the flywheel face).
        b.style(mix(ti, tint, 0.5), METAL);
        b.tube({V3(tx - 0.035, ty, tz), V3(tx - 0.06, ty - 0.01, s * (tzz + 0.025))}, 0.042, 8, false, false);
        b.style(ti, METAL);
        b.tube({V3(tx - 0.06, ty - 0.01, s * (tzz + 0.025)), V3(tx - 0.075, ty - 0.12, s * (tzz + 0.05)), V3(tx - 0.07, -0.06, s * (tzz + 0.05))}, 0.042, 8);
        b.style(V3(0.6, 0.61, 0.63), METAL);
        b.cyl(V3(tx - 0.06, ty - 0.01, s * (tzz + 0.025)) + V3(0.004, 0, 0), V3(tx - 0.06, ty - 0.01, s * (tzz + 0.025)) - V3(0.004, 0, 0), 0.047, 8, false);  // v-band
    }
    // Front: the gear-drive cover, a damper and an electric water pump; no belts.
    const V3 alu = v.blockCol;
    b.style(alu * 0.97, v.blockMat);
    b.box(V3(xf + 0.016, 0.0, 0), V3(0.032, 0.25, 0.3), 0.02);
    b.style(V3(0.6, 0.61, 0.63), METAL);
    b.style(grime(e, V3(0.15, 0.15, 0.16), 0.4), METAL);
    xlathe(b, V3(xf + 0.032, 0, 0), {V2(0.08, 0), V2(0.08, 0.04), V2(0.03, 0.045), V2(0, 0.045)}, 12);
    b.style(grime(e, V3(0.62, 0.63, 0.66), 0.8), METAL);
    xcyl(b, xf + 0.03, xf + 0.09, 0.15, 0.0, 0.04, 10);
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.3), PAINT);
    xcyl(b, xf + 0.09, xf + 0.12, 0.15, 0.0, 0.034, 8);
    b.style(kRubber, MATTE);
    for (int s = -1; s <= 1; s += 2) b.tube({V3(xf + 0.06, 0.15, s * 0.03), V3(xf + 0.06, 0.19, s * 0.12), V3(xf + 0.02, 0.23, s * 0.17)}, 0.016, 6);
    // Dry-sump pump along the lower left, gear driven from the front, braided lines.
    const double pz = v.skirtZ + 0.045, py = -0.05;
    b.style(grime(e, V3(0.62, 0.63, 0.66), 0.8), METAL);
    xcyl(b, v.xr + 0.06, xf + 0.02, py, pz, 0.04, 10);
    b.style(v.accent * 0.9, PAINT);
    for (int i = 0; i < 3; i++) xcyl(b, v.xr + 0.12 + i * 0.13, v.xr + 0.132 + i * 0.13, py, pz, 0.045, 10, false);
    b.style(V3(0.26, 0.26, 0.28), METAL);
    for (int i = 0; i < 3; i++) {
        const double x = v.cx() - 0.15 + 0.15 * i;
        b.tube({V3(x, v.rail - v.panDepth * 0.55, 0.15), V3(x + 0.02, v.rail - 0.04, pz + 0.01), V3(x + 0.03, py - 0.035, pz)}, 0.011, 6);
    }
    b.tube({V3(v.xr + 0.06, py, pz + 0.03), V3(v.xr + 0.02, py + 0.06, pz + 0.06), V3(v.xr - 0.01, py + 0.12, pz + 0.07)}, 0.012, 6);
}

// ---- The whole engine and its presentation ------------------------------------------------------

void buildV8(B& b, const V8& v, bool carbon = true) {
    bottomEnd(b, v);
    heads(b, v);
    switch (v.exhaust) {
    case EX_LOG: logManifolds(b, v); break;
    case EX_HEADERS: headers(b, v); break;
    default: break;
    }
    switch (v.intake) {
    case IN_CARB: carbIntake(b, v); break;
    case IN_TRUCK: truckIntake(b, v); break;
    case IN_BLOWER: blowerTop(b, v); break;
    default: dohcTop(b, v, carbon); break;
    }
    if (v.distributor) distributor(b, v);
    if (v.front == FR_VBELT) vbeltFront(b, v);
    else if (v.front == FR_SERP) serpFront(b, v);
    if (v.ls7) lsDrySump(b, v);
    if (v.e.wear > 0.35) {  // cut wiring hanging off the front
        b.style(V3(0.05, 0.05, 0.05), MATTE);
        b.tube({V3(v.xf + 0.02, v.valleyTop(), -0.1), V3(v.xf + 0.08, v.valleyTop() - 0.1, -0.2), V3(v.xf + 0.06, 0.02, -0.25)}, 0.008, 4);
    }
}

// The skid strap's path (z, y) over the engine at the front of the covers.
std::vector<V2> strapFor(const V8& v, double topY) {
    const double ht = v.headTop() + 0.008 + v.coverH + 0.012;
    const V3 co = bankPt(v, 1, V3(0, ht, v.coverW / 2 + 0.01)), ci = bankPt(v, 1, V3(0, ht + 0.02, -v.coverW / 2));
    const V3 ex = bankPt(v, 1, V3(0, v.deck + 0.04, v.headW / 2 + (v.exhaust == EX_TURBO ? 0.06 : 0.11)));
    return {V2(v.skirtZ + 0.07, -0.14), V2(ex.z + 0.02, ex.y - 0.08), V2(std::max(ex.z, co.z) + 0.02, co.y - 0.02), V2(co.z + 0.01, co.y + 0.01),
            V2(ci.z, ci.y + 0.01), V2(0.06, topY)};
}

void present(B& b, const P& p, const V8& v, double topY, bool carbon = true) {
    auto eng = [&] { buildV8(b, v, carbon); };
    if (bare(p)) return eng();
    switch (p.i("mount")) {
    case MOUNT_SKID: engineSkid(b, V3(0.85, 0.62, 0.1), eng, v.xf - 0.03, strapFor(v, topY), 0.85); break;
    case MOUNT_PULLED: engineGround(b, eng, 0.82); break;
    default: engineStand(b, 0.5, v.flyFace(), V3(0.2, 0.22, 0.26), eng);
    }
}

Eng engOpts(const P& p) {
    Eng e;
    e.wear = std::clamp(p("wear"), 0.0, 1.0);
    if (!bare(p) && p.i("mount") == MOUNT_PULLED) e.wear = std::max(e.wear, 0.35);
    e.seed = (uint32_t)std::max(1, p.i("seed"));
    e.paint = enginePaint(p.i("paint"));
    e.chrome = p.i("chrome") != 0;
    return e;
}

// ---- crate_engine_v8 -------------------------------------------------------------------------

void smallBlock(B& b, const P& p) {
    V8 v;
    v.e = engOpts(p);
    const Eng& e = v.e;
    const bool zz4 = p.i("zz4") != 0;
    const V3 paint = grime(e, e.paint, 0.8), alu = grime(e, V3(0.64, 0.65, 0.67), 0.9);
    v.blockCol = paint;
    v.headCol = zz4 ? alu : paint;
    v.headMat = zz4 ? METAL : PAINT;
    v.panCol = paint;
    v.wireCol = zz4 ? V3(0.55, 0.1, 0.07) : V3(0.06, 0.06, 0.065);
    if (zz4) {
        v.cover = COV_FINNED;
        v.coverCol = grime(e, V3(0.72, 0.73, 0.75), 0.9);
        v.coverMat = METAL;
        v.intakeCol = alu;
        v.intakeMat = METAL;
        v.intakeLift = 0.01;
        v.barrels = 4;
        v.cleaner = AC_OPEN;
        v.cleanerR = 0.178;
        v.cleanerH = 0.07;
    } else {
        v.cover = COV_STAMPED;
        v.coverCol = e.chrome ? kChrome : paint;
        v.coverMat = e.chrome ? CHROME : PAINT;
        v.intakeCol = paint;
        v.intakeMat = PAINT;
        v.barrels = p.i("carb") ? 4 : 2;
        v.cleanerR = v.barrels == 4 ? 0.17 : 0.13;
        v.cleanerH = v.barrels == 4 ? 0.075 : 0.065;
        v.cleanerCol = e.chrome ? kChrome : grime(e, V3(0.07, 0.07, 0.075), 0.3);
        v.cleanerMat = e.chrome ? CHROME : PAINT;
    }
    present(b, p, v, v.valleyTop() + 0.07);
}

void bigBlock(B& b, const P& p) {
    V8 v;
    v.e = engOpts(p);
    const Eng& e = v.e;
    const V3 paint = grime(e, e.paint, 0.8);
    v.deck = 0.249, v.bs = 0.1229, v.stagger = 0.024, v.xf = 0.283, v.xr = -0.283;
    v.bankW = 0.19, v.skirtZ = 0.18, v.rail = -0.115, v.panDepth = 0.25, v.flyR = 0.18;
    v.headH = 0.106, v.headW = 0.178, v.coverH = 0.08, v.coverW = 0.13;
    v.cover = COV_TALL, v.barrels = 4, v.cleanerR = 0.178, v.cleanerH = 0.078, v.snorkel = false, v.heavyFront = true;
    v.portScale = 1.15, v.bp = 0.11, v.intakeLift = 0.006;
    v.blockCol = v.headCol = v.panCol = v.intakeCol = paint;
    v.coverCol = e.chrome ? kChrome : paint;
    v.coverMat = e.chrome ? CHROME : PAINT;
    v.cleanerCol = e.chrome ? kChrome : paint;
    v.cleanerMat = e.chrome ? CHROME : PAINT;
    v.wireCol = V3(0.06, 0.06, 0.065);
    v.coverText = "454";
    v.lidText = "454";
    present(b, p, v, v.valleyTop() + 0.09);
}

void lsEngine(B& b, const P& p) {
    V8 v;
    v.e = engOpts(p);
    const Eng& e = v.e;
    const bool ls7 = p.i("ls7") != 0, blown = p.i("blower") != 0;
    const V3 alu = grime(e, V3(0.6, 0.61, 0.63), 0.9);
    v.deck = 0.235, v.stagger = 0.024, v.xf = 0.25, v.xr = -0.25, v.rail = -0.125, v.panDepth = 0.24, v.skirtZ = 0.17;
    v.headH = 0.105, v.headW = 0.17, v.coverH = 0.045, v.coverW = 0.13, v.bp = 0.13;
    v.blockCol = v.headCol = alu;
    v.blockMat = v.headMat = METAL;
    v.panCol = grime(e, V3(0.56, 0.57, 0.6), 0.9);
    v.panMat = METAL;
    v.cover = COV_COILS, v.exhaust = EX_HEADERS, v.front = FR_SERP, v.distributor = false;
    v.coverCol = ls7 ? grime(e, V3(0.66, 0.67, 0.7), 0.7) : grime(e, V3(0.08, 0.08, 0.09), 0.3);
    v.coverMat = ls7 ? METAL : PAINT;
    v.wireCol = V3(0.06, 0.06, 0.065);
    v.ls7 = ls7;
    if (ls7) {
        v.drySump = true;
        v.panDepth = 0.11;
        v.panCol = grime(e, V3(0.1, 0.1, 0.11), 0.3);
        v.panMat = PAINT;
    }
    if (blown) {
        v.intake = IN_BLOWER;
    } else {
        v.intake = IN_TRUCK;
        v.intakeCol = ls7 ? grime(e, V3(0.6, 0.1, 0.08), 0.6) : grime(e, V3(0.075, 0.075, 0.08), 0.3);
        v.intakeMat = ls7 ? PAINT : MATTE;
        v.intakeText = ls7 ? "7.0 LITER" : "6.2 LITER";
    }
    present(b, p, v, v.valleyTop() + (blown ? 0.23 : 0.11));
}

Reg crateEngineV8({"crate_engine_v8", "CRATE ENGINE V8", "PARTS", "Career upgrade: a pushrod V8 crate engine (shop, supply cache, reward)", 0.9,
                   {
                       {"SMALL BLOCK", "Chevy-style 350: painted iron, stamped covers, a carb under a round air cleaner, distributor and V-belts",
                        {choice("carb", "CARB", 0, {"2-BARREL", "4-BARREL"}), choice("zz4", "ZZ4 CRATE", 0, {"OFF", "ON"}), enginePaintParam(0),
                         choice("chrome", "CHROME COVERS", 0, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()},
                        smallBlock},
                       {"BIG BLOCK", "454: tall deck, wide canted-valve heads, tall covers, a 14-inch air cleaner and a heavy front drive",
                        {enginePaintParam(0), choice("chrome", "CHROME COVERS", 1, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()}, bigBlock},
                       {"LS", "Aluminium 6.2: coil packs on flat covers, composite truck intake, drive-by-wire throttle, headers, serpentine",
                        {choice("ls7", "LS7 7.0", 0, {"OFF", "ON"}), choice("blower", "SUPERCHARGER", 0, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()},
                        lsEngine},
                   }});

// ---- crate_engine_v8_dohc --------------------------------------------------------------------

void flatPlane(B& b, const P& p) {
    V8 v;
    v.e = engOpts(p);
    const Eng& e = v.e;
    const V3 alu = grime(e, V3(0.58, 0.59, 0.61), 0.9);
    v.deck = 0.205, v.bs = 0.104, v.stagger = 0.02, v.xf = 0.235, v.xr = -0.235, v.bankW = 0.17;
    v.skirtZ = 0.16, v.rail = -0.085, v.panDepth = 0.05, v.flyR = 0.13, v.flangeH = 0.32;
    v.headH = 0.135, v.headW = 0.235, v.coverH = 0.034, v.coverW = 0.228;
    v.cover = COV_CAMS, v.intake = IN_PLENUM, v.exhaust = EX_TURBO, v.front = FR_GEAR, v.distributor = false, v.drySump = true;
    v.blockCol = v.headCol = alu;
    v.blockMat = v.headMat = METAL;
    v.panCol = grime(e, V3(0.5, 0.51, 0.54), 0.9);
    v.panMat = METAL;
    v.accent = p.i("accent") == 1 ? grime(e, V3(0.07, 0.07, 0.08), 0.3) : grime(e, V3(0.62, 0.08, 0.06), 0.6);
    v.coverCol = v.accent;
    v.coverMat = PAINT;
    v.coverText = "DOHC";
    if (p.i("accent") == 1) v.accent = V3(0.62, 0.08, 0.06);  // (lettering and sump bands stay red on black covers)
    const bool carbon = p.i("carbon") != 0;
    present(b, p, v, v.valleyTop() + 0.14, carbon);
}

Reg crateEngineV8Dohc({"crate_engine_v8_dohc", "CRATE ENGINE V8 DOHC", "PARTS", "Hypercar engine: a flat-plane 5.0 DOHC twin-turbo V8 (mid-engine build)", 0.9,
                       {
                           {"FLAT-PLANE TWIN-TURBO", "Low, wide, dry-sump 5.0: four-cam covers, carbon plenum, two big turbos at the back, gear-driven front",
                            {choice("accent", "CAM COVERS", 0, {"RED", "BLACK"}), choice("carbon", "CARBON PLENUM", 1, {"OFF", "ON"}), mountParam(), wearParam(), seedParam()},
                            flatPlane},
                       }});

}  // namespace
