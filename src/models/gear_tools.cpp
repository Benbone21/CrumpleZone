// Gear: tools and off-road kit. Jacks, snow chains, shovels, compressors, toolboxes, fuel cells, light bars.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

// ---- Shared bits ----------------------------------------------------------------------------

const V3 kBlack(0.07, 0.07, 0.075);
const V3 kDark(0.13, 0.13, 0.14);
const V3 kSteel(0.56, 0.57, 0.58);
const V3 kZinc(0.64, 0.66, 0.66);
const V3 kRust(0.42, 0.2, 0.09);
const V3 kRubber(0.05, 0.05, 0.055);
const V3 kWhite(0.86, 0.85, 0.8);
const V3 kBrass(0.72, 0.54, 0.2);
const V3 kAlu(0.72, 0.73, 0.75);

// Builds `f` in a frame whose up axis is `dir` at `base` (so lathe/cyl/torus/helices point any way).
void along(B& b, const V3& base, const V3& dir, const std::function<void()>& f) {
    const V3 d = norm(dir);
    b.push();
    b.move(base);
    const V3 ax = cross(V3(0, 1, 0), d);
    const double s = len(ax);
    if (s > 1e-6) b.rotate(ax, std::atan2(s, d.y) * 180 / M_PI);
    else if (d.y < 0) b.roll(180);
    f();
    b.pop();
}

void latheAlong(B& b, const V3& base, const V3& dir, const std::vector<V2>& prof, int segs = 18, bool smooth = true) {
    along(b, base, dir, [&] { b.lathe(V3(0, 0, 0), prof, segs, smooth); });
}

// A coil (hose, chain, spring) wound round `dir` from `base`: radius R, tube radius r, rising `rise` per turn.
void coil(B& b, const V3& base, const V3& dir, double R, double r, double turns, double rise, int sides = 5, double R2 = -1) {
    along(b, base, dir, [&] {
        std::vector<V3> path;
        const int n = std::max(6, (int)(turns * 10));
        for (int i = 0; i <= n; i++) {
            const double t = turns * i / n, a = t * 2 * M_PI;
            const double RR = R2 < 0 ? R : R + (R2 - R) * i / n;
            path.push_back(V3(RR * std::cos(a), rise * t, -RR * std::sin(a)));
        }
        b.tube(path, r, sides);
    });
}

// A hex bolt head standing proud of a face (pointing along dir).
void bolt(B& b, const V3& at, const V3& dir, double r, double h = -1) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    if (h < 0) h = r * 0.7;
    b.style(kSteel * 0.8, METAL);
    b.cyl(at, at + norm(dir) * h, r, 6);
    b.style(keep, km);
}

// Raised letters on a face: `face` 0 +X, 1 -X, 2 +Z (left), 3 -Z (right), 4 up (reads from the front side).
void stamp(B& b, const std::string& s, const V3& pos, int face, double px, double depth = 0.003) {
    b.push();
    b.move(pos);
    if (face == 1) b.yaw(180);
    if (face == 2) b.yaw(-90);
    if (face == 3) b.yaw(90);
    if (face == 4) b.pitch(90);
    b.letters(s, V3(0, 0, 0), px, depth);
    b.pop();
}

// Rust spots: little dark-orange scabs on a flat region (a box of size sz at c, spots on its +X, +Z, -Z faces).
void rustSpots(B& b, const V3& c, const V3& sz, int n, double amount, uint32_t seed) {
    if (amount <= 0.01) return;
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kRust, MATTE);
    const int cnt = (int)std::lround(n * amount);
    for (int i = 0; i < cnt; i++) {
        const double u = hash01(seed, i * 3), v = hash01(seed, i * 3 + 1), w = hash01(seed, i * 3 + 2);
        const double s = 0.006 + 0.012 * w;
        b.box(V3(c.x + (u - 0.5) * sz.x, c.y + (v - 0.5) * sz.y, c.z + (w > 0.5 ? 1 : -1) * (sz.z / 2 + 0.0008)), V3(s, s * 0.8, 0.0016));
    }
    b.style(keep, km);
}

V3 worn(const V3& c, double wear) { return mix(c, c * 0.55 + V3(0.12, 0.07, 0.03), wear * 0.5); }

// =============================================================================================
// 1. JACK
// =============================================================================================

// The high-lift farm jack: a tall holed bar on a cast foot, a carriage with a lifting nose that
// climbs the bar, a long pumping handle. Saddle (the top of the nose pad): 0.30 m -> 1.05 m, as high as a
// lifted truck's frame. `reach` is how far the bar stands out from the saddle (0.10 m: the plain jack under
// an axle; more, the carriage's lifting arm reaches in past the body's side to a frame rail's jack pad).
void farmJack(B& b, const P& p) {
    const double lift = p("lift"), wear = p("wear");
    const double sy = 0.30 + 0.75 * lift;
    const V3 red = worn(paint(p.i("paint")), wear), blk = kBlack * 1.2;
    const double bx = -std::max(0.10, p("reach")), H = 1.2;
    // The foot: a cast plate, ribbed, with the bar socket.
    b.style(blk, PAINT);
    b.push();
    b.move(bx + 0.10, 0, 0);  // (under the bar, whatever the reach)
    b.extrudeUp({V2(-0.24, -0.09), V2(0.20, -0.1), V2(0.23, -0.05), V2(0.23, 0.05), V2(0.20, 0.1), V2(-0.24, 0.09)}, 0.028);
    b.box(V3(-0.10, 0.05, 0), V3(0.11, 0.07, 0.12), 0.015);
    for (int s = -1; s <= 1; s += 2) {
        b.extrude({V2(-0.14, 0.028), V2(0.04, 0.028), V2(-0.06, 0.085), V2(-0.14, 0.085)}, 0.012, s * 0.052);
        b.extrude({V2(-0.15, 0.028), V2(-0.27, 0.028), V2(-0.14, 0.075)}, 0.012, s * 0.052);
    }
    b.pop();
    // The bar: a heavy section with a ladder of holes up its front, a cap and a ring.
    b.style(red, PAINT);
    b.box(V3(bx, 0.03 + H / 2 - 0.02, 0), V3(0.05, H - 0.04, 0.085), 0.006);
    b.style(red * 0.8, PAINT);
    b.box(V3(bx, H - 0.012, 0), V3(0.062, 0.03, 0.095), 0.01);  // cap
    b.style(kBlack, MATTE);
    for (int i = 0; i < 17; i++) b.box(V3(bx + 0.0255, 0.19 + i * 0.055, 0), V3(0.002, 0.034, 0.032));
    b.style(kDark, METAL);
    b.push();
    b.roll(90);
    b.torus(V3(bx, 0, -H - 0.028), 0.018, 0.005, 12, 6);
    b.pop();
    b.style(kWhite, MATTE);
    stamp(b, "TUFF", V3(bx, 0.62, 0.0432), 2, 0.0034, 0.002);
    // Carriage that rides the bar.
    const double cy = sy - 0.085;
    b.style(blk * 1.1, PAINT);
    b.box(V3(bx + 0.002, cy, 0), V3(0.085, 0.16, 0.115), 0.012);
    b.style(kSteel * 0.7, METAL);
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(bx + 0.032, cy + 0.05, s * 0.0585), V3(bx + 0.032, cy + 0.05, s * 0.066), 0.011, 8);  // pins
    // Lifting nose (steel tongue) running out to the saddle at x = 0.
    b.style(kSteel * 0.85, METAL);
    const double armL = 0.065 - (bx + 0.03);
    b.box(V3((0.065 + bx + 0.03) * 0.5, sy - 0.022, 0), V3(armL, 0.034, 0.09), 0.006);
    b.box(V3(bx + 0.04, sy - 0.07, 0), V3(0.022, 0.07, 0.07), 0.006);
    if (armL > 0.2) {  // a long arm: a brace down to the carriage, and a rib along it
        b.beam(V3(bx + 0.05, sy - 0.15, 0), V3(bx + armL * 0.7, sy - 0.04, 0), 0.02, 0.07, V3(0, 0, 1), 0.004);
        b.box(V3((0.065 + bx + 0.03) * 0.5, sy - 0.044, 0), V3(armL * 0.9, 0.012, 0.03), 0.003);
    }
    // Saddle: a ridged pad with a hooked toe.
    b.style(blk, MATTE);
    b.box(V3(0.0, sy - 0.0065, 0), V3(0.11, 0.013, 0.082), 0.003);
    for (int i = -2; i <= 2; i++) b.box(V3(i * 0.02, sy + 0.0005, 0), V3(0.008, 0.005, 0.082), 0.001);
    b.style(kSteel * 0.85, METAL);
    b.box(V3(0.06, sy + 0.008, 0), V3(0.012, 0.045, 0.09), 0.004);
    // The handle: from the carriage mechanism up to its hook near the top of the bar.
    const V3 P0(bx + 0.06, cy + 0.045, 0), P1(bx + 0.23, 1.13, 0);
    b.style(blk * 1.3, PAINT);
    b.beam(P0 + V3(0, 0, 0.0), P1, 0.026, 0.013, V3(0, 0, 1), 0.003);
    b.style(kRubber, MATTE);
    b.beam(P0 + (P1 - P0) * 0.72, P1, 0.032, 0.028, V3(0, 0, 1), 0.008);
    b.style(kSteel * 0.8, METAL);
    b.cyl(P0 + V3(0, 0, -0.07), P0 + V3(0, 0, 0.07), 0.01, 10);
    // Lifting link: the pawl rod.
    b.beam(P0 + (P1 - P0) * 0.12, V3(bx + 0.045, cy - 0.04, 0), 0.016, 0.01, V3(0, 0, 1), 0.002);
    // Rust.
    b.style(kRust, MATTE);
    rustSpots(b, V3(bx, 0.5, 0), V3(0.04, 0.8, 0.088), 8, wear, 11);
}

// A hydraulic bottle jack on a wide base: a pump alongside, a socket for the handle, a ram
// that rises with `lift`. Saddle (top of the cup): 0.26 m -> 0.61 m.
void bottleJack(B& b, const P& p) {
    const double lift = p("lift"), wear = p("wear");
    const V3 body = worn(paint(p.i("paint")), wear), yel = paint(p.i("accent"));
    const double ramTop = 0.2 + 0.35 * lift;
    // Wide steel base.
    b.style(body * 0.7, PAINT);
    std::vector<V2> bp = roundedRect(0.27, 0.19, 0.03, 4);
    b.extrudeUp(bp, 0.026);
    b.style(body * 0.8, PAINT);
    std::vector<V2> bp2 = roundedRect(0.2, 0.14, 0.025, 4);
    b.extrudeUp(bp2, 0.014, 0.026);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            b.style(kSteel * 0.7, METAL);
            b.cyl(V3(sx * 0.1, 0.026, sz * 0.065), V3(sx * 0.1, 0.034, sz * 0.065), 0.009, 6);
        }
    // The cylinder.
    b.style(body, PAINT);
    latheAlong(b, V3(0, 0.04, 0),
               V3(0, 1, 0),
               {V2(0, 0), V2(0.058, 0), V2(0.058, 0.012), V2(0.05, 0.02), V2(0.05, 0.14), V2(0.046, 0.15), V2(0.04, 0.16), V2(0.04, 0.17), V2(0.034, 0.17), V2(0, 0.17)}, 18);
    b.style(body * 0.85, PAINT);
    b.torus(V3(0, 0.125, 0), 0.052, 0.0045, 22, 6);
    // Gland collar (chrome) round the ram.
    b.style(kSteel, CHROME);
    latheAlong(b, V3(0, 0.205, 0), V3(0, 1, 0), {V2(0.034, 0), V2(0.034, 0.012), V2(0.029, 0.014), V2(0.029, 0)}, 16);
    // The ram: two bright chrome stages, up with `lift`.
    if (ramTop > 0.206) {
        b.style(kSteel, CHROME);
        b.cyl(V3(0, 0.2, 0), V3(0, 0.2 + 0.2 * lift, 0), 0.0295, 16, true);
        b.cyl(V3(0, 0.2, 0), V3(0, ramTop, 0), 0.0215, 14, true);
        b.style(kSteel * 0.8, METAL);
        b.cyl(V3(0, 0.2 + 0.2 * lift - 0.002, 0), V3(0, 0.2 + 0.2 * lift + 0.003, 0), 0.0335, 16);
        b.cyl(V3(0, ramTop - 0.002, 0), V3(0, ramTop + 0.002, 0), 0.027, 14);
    }
    // Extension screw (threaded) and the saddle cup.
    b.style(kDark, METAL);
    for (int i = 0; i < 5; i++) b.cyl(V3(0, ramTop + i * 0.0072, 0), V3(0, ramTop + i * 0.0072 + 0.0042, 0), 0.0135, 10);
    b.cyl(V3(0, ramTop, 0), V3(0, ramTop + 0.036, 0), 0.0105, 8);
    b.style(yel, PAINT);
    latheAlong(b, V3(0, ramTop + 0.032, 0), V3(0, 1, 0), {V2(0, 0), V2(0.02, 0), V2(0.032, 0.008), V2(0.038, 0.012), V2(0.038, 0.028), V2(0.03, 0.028), V2(0.03, 0.0245), V2(0, 0.0245)}, 18);
    b.style(yel * 0.8, PAINT);
    for (int i = 0; i < 6; i++) b.cyl(V3(0, ramTop + 0.0555, 0), V3(0, ramTop + 0.0565, 0), 0.03, 6);
    // Pump cylinder alongside, with the lever socket.
    b.style(kSteel * 0.85, CHROME);
    b.cyl(V3(-0.086, 0.04, 0), V3(-0.086, 0.14, 0), 0.0155, 12);
    b.style(body * 0.75, PAINT);
    b.box(V3(-0.083, 0.135, 0), V3(0.05, 0.036, 0.046), 0.01);
    b.style(kBlack, MATTE);
    b.cyl(V3(-0.103, 0.138, 0), V3(-0.108, 0.138, 0), 0.0105, 10);
    // Release screw and its knob, at the front.
    b.style(kSteel * 0.8, METAL);
    b.cyl(V3(0.045, 0.085, 0), V3(0.08, 0.085, 0), 0.007, 8);
    b.style(kBlack, MATTE);
    b.cyl(V3(0.075, 0.085, 0), V3(0.092, 0.085, 0), 0.014, 8);
    // The handle, socketed in the lever and sloping back.
    b.style(kDark, METAL);
    b.cyl(V3(-0.1, 0.138, 0), V3(-0.33, 0.30, 0), 0.0105, 10);
    b.style(kRubber, MATTE);
    b.cyl(V3(-0.27, 0.258, 0), V3(-0.33, 0.30, 0), 0.0155, 10);
    // Capacity stencil on the base lip, and rust.
    b.style(kWhite, MATTE);
    stamp(b, "12T", V3(0.1355, 0.013, 0), 0, 0.0027, 0.002);
    rustSpots(b, V3(0, 0.1, 0), V3(0.1, 0.07, 0.1), 7, wear, 23);
}

// The car scissor jack: a diamond of pressed arms on a screw, a crank on the front. The saddle
// sits on the top pivot above the origin. Saddle: 0.12 m -> 0.47 m.
void scissorJack(B& b, const P& p) {
    const double lift = p("lift"), wear = p("wear");
    const double sy = 0.12 + 0.35 * lift;
    const V3 arm = mix(kZinc, kRust, wear * 0.35), crank = paint(p.i("paint"));
    const double L = 0.24, ybot = 0.032, ytop = sy - 0.045;
    const double half = (ytop - ybot) / 2, ymid = (ytop + ybot) / 2;
    const double hx = std::sqrt(std::max(0.0004, L * L - half * half));
    // Base plate.
    b.style(arm * 0.85, METAL);
    b.extrudeUp(roundedRect(0.2, 0.1, 0.01, 3), 0.012);
    b.box(V3(0, 0.017, 0), V3(0.2, 0.012, 0.014), 0.003);
    b.box(V3(0, 0.0225, 0), V3(0.05, 0.012, 0.09), 0.003);
    // Arms: a pressed pair on each side of the diamond.
    b.style(arm, METAL);
    for (int z = -1; z <= 1; z += 2) {
        const double zz = z * 0.034;
        for (int sx = -1; sx <= 1; sx += 2) {
            b.beam(V3(sx * hx, ymid, zz), V3(0, ytop, zz), 0.014, 0.026, V3(0, 0, 1), 0.003);
            b.beam(V3(sx * hx, ymid, zz), V3(0, ybot, zz), 0.014, 0.026, V3(0, 0, 1), 0.003);
            // Rolled edge on each arm.
            b.beam(V3(sx * hx, ymid + 0.012, zz + z * 0.0), V3(0, ytop + 0.012, zz), 0.006, 0.004, V3(0, 0, 1), 0.001);
        }
        b.style(kSteel * 0.75, METAL);
        b.cyl(V3(0, ytop, zz - 0.012), V3(0, ytop, zz + 0.012), 0.011, 10);
        b.cyl(V3(0, ybot, zz - 0.012), V3(0, ybot, zz + 0.012), 0.011, 10);
        for (int sx = -1; sx <= 1; sx += 2) b.cyl(V3(sx * hx, ymid, zz - 0.014), V3(sx * hx, ymid, zz + 0.014), 0.011, 10);
        b.style(arm, METAL);
    }
    // The screw running across the diamond, with the nuts at its pivots.
    b.style(kDark * 1.4, METAL);
    const double rodA = -hx - 0.03, rodB = hx + 0.03;
    const int nt = std::max(6, (int)std::lround((rodB - rodA) / 0.028));
    std::vector<V2> prof;
    prof.push_back(V2(0, 0));
    for (int i = 0; i < nt; i++) {
        const double y = (rodB - rodA) * i / nt, dy = (rodB - rodA) / nt;
        prof.push_back(V2(0.0108, y + dy * 0.5)), prof.push_back(V2(0.0074, y + dy));
    }
    prof.push_back(V2(0, rodB - rodA));
    latheAlong(b, V3(rodA, ymid, 0), V3(1, 0, 0), prof, 8, false);
    b.style(kSteel * 0.7, METAL);
    for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * hx, ymid, 0), V3(0.026, 0.026, 0.05), 0.005);
    // Crank: a bent rod with a loop handle on the front end.
    b.style(crank, PAINT);
    b.cyl(V3(rodB, ymid, 0), V3(rodB + 0.04, ymid, 0), 0.0105, 8);
    b.tube({V3(rodB + 0.04, ymid, 0), V3(rodB + 0.07, ymid, 0), V3(rodB + 0.085, ymid + 0.012, 0), V3(rodB + 0.085, ymid + 0.07, 0.0), V3(rodB + 0.085, ymid + 0.07, 0.012), V3(rodB + 0.085, ymid + 0.12, 0.012)}, 0.0055, 6);
    b.style(kRubber, MATTE);
    b.cyl(V3(rodB + 0.085, ymid + 0.07, 0.012), V3(rodB + 0.085, ymid + 0.14, 0.012), 0.0105, 8);
    // Saddle: a shaped plate with the groove, rubber pad on top.
    b.style(arm * 0.9, METAL);
    b.box(V3(0, sy - 0.0235, 0), V3(0.11, 0.019, 0.1), 0.004);
    b.style(kRubber, MATTE);
    b.box(V3(0, sy - 0.006, 0), V3(0.095, 0.012, 0.085), 0.003);
    b.box(V3(0, sy + 0.0005, 0), V3(0.085, 0.002, 0.02), 0.0005);  // groove lip
    rustSpots(b, V3(0, 0.1, 0), V3(0.18, 0.04, 0.09), 6, wear, 31);
}

Reg jack({"jack", "JACK", "TOOLS", "Wheel jack: put under a truck's frame (or a car's axle) to change a tyre; lift 0..1 raises the saddle (the hi-lift by 0.75 m, the others 0.35 m)", 1.2,
          {
              {"FARM JACK",
               "Tall high-lift bar jack, red and black. Saddle 0.30 m at lift 0, 1.05 m at lift 1; the arm reaches in `reach` from the bar",
               {num("lift", "LIFT", 0, 0, 1), paintParam(0), num("wear", "WEAR", 0.2, 0, 1), num("reach", "ARM REACH", 0.10, 0.10, 0.9, 0.01)},
               farmJack},
              {"BOTTLE JACK",
               "Hydraulic bottle jack on a wide base, blue with a yellow saddle. Saddle 0.26 m at lift 0, 0.61 m at lift 1",
               {num("lift", "LIFT", 0, 0, 1), paintParam(5), paintParam(8, "accent", "SADDLE"), num("wear", "WEAR", 0.2, 0, 1)},
               bottleJack},
              {"SCISSOR JACK",
               "Car scissor jack on a screw with a crank. Saddle 0.12 m at lift 0, 0.47 m at lift 1",
               {num("lift", "LIFT", 0, 0, 1), paintParam(4, "paint", "CRANK"), num("wear", "WEAR", 0.2, 0, 1)},
               scissorJack},
          }});

// =============================================================================================
// 2. CHAIN BUNDLE
// =============================================================================================

// One chain link (a flattened ring) at p, long axis `dir`, its flat face toward `normal`.
void link(B& b, const V3& p, const V3& dir, const V3& normal, double L, double W, double r, int sides = 3) {
    const V3 d = norm(dir);
    V3 n = normal - d * dot(normal, d);
    n = len(n) > 1e-6 ? norm(n) : norm(cross(d, V3(0, 1, 0)));
    const V3 w = cross(d, n);
    std::vector<V3> path;
    for (int k = 0; k < 6; k++) {
        const double a = k * M_PI / 3;
        path.push_back(p + d * (std::cos(a) * L / 2) + w * (std::sin(a) * W / 2));
    }
    b.tube(path, r, sides, true);
}

// A run of links along a polyline, alternating their faces.
void linkRun(B& b, const std::vector<V3>& pts, double pitch, double L, double W, double r, const V3& up = V3(0, 1, 0)) {
    double carry = 0;
    int i = 0;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        const V3 d = pts[k + 1] - pts[k];
        const double sl = len(d);
        if (sl < 1e-9) continue;
        if (carry >= sl) {
            carry -= sl;
            continue;
        }
        const V3 t = d / sl;
        V3 u = up - t * dot(up, t);
        u = len(u) > 1e-6 ? norm(u) : V3(0, 0, 1);
        const V3 w = cross(t, u);
        for (double s = carry; s < sl; s += pitch, i++) link(b, pts[k] + t * s, t, (i & 1) ? w : u, L, W, r), carry = s + pitch - sl;
        if (carry < 0) carry = 0;
    }
}

// A curve through control points (Catmull-Rom), n samples per span.
std::vector<V3> curve(const std::vector<V3>& c, int per = 6) {
    std::vector<V3> out;
    const int n = (int)c.size();
    for (int i = 0; i + 1 < n; i++) {
        const V3 p0 = c[std::max(0, i - 1)], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(n - 1, i + 2)];
        for (int k = 0; k < per; k++) {
            const double t = (double)k / per, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5);
        }
    }
    out.push_back(c.back());
    return out;
}

// An S hook of rod, from a to b (a straight shank with a hooked end).
void sHook(B& b, const V3& a, const V3& e, double r, double hook = 0.012) {
    const V3 d = norm(e - a);
    V3 s = cross(d, V3(0, 1, 0));
    s = len(s) > 1e-6 ? norm(s) : V3(0, 0, 1);
    b.tube({a + s * hook + d * hook * 0.4, a + s * hook * 0.2 - d * hook * 0.3, a + d * hook * 0.4 - s * hook * 0.4, a + d * hook * 2.4, e - d * hook * 0.4 + s * hook * 0.0, e + s * hook * 0.5 + d * hook * 0.5,
            e + s * hook * 1.4 + d * hook * 0.1, e + s * hook * 1.4 - d * hook * 0.8},
           r, 3);
}

// The red rubber tensioner: an elastic loop with a hook at each end. Lies along `dir` at p.
void tensioner(B& b, const V3& p, const V3& dir, const V3& col, double len_ = 0.14) {
    const V3 d = norm(dir);
    V3 s = cross(d, V3(0, 1, 0));
    s = len(s) > 1e-6 ? norm(s) : V3(0, 0, 1);
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(col, MATTE);
    const double hl = len_ / 2;
    b.tube({p - d * hl, p - d * hl * 0.5 + s * 0.012, p + s * 0.014, p + d * hl * 0.5 + s * 0.012, p + d * hl, p + d * hl * 0.5 - s * 0.012, p - s * 0.014, p - d * hl * 0.5 - s * 0.012}, 0.0045, 4, true);
    b.style(kSteel * 0.9, METAL);
    sHook(b, p + d * hl, p + d * (hl + 0.04), 0.0032, 0.010);
    sHook(b, p - d * hl, p - d * (hl + 0.04), 0.0032, 0.010);
    b.style(keep, km);
}

// The loose heap: loops of cable chain piled in a mound, a ladder section laid over the top, tensioners.
void chainHeap(B& b, const P& p) {
    const double sc = p("size"), wear = p("wear");
    const uint32_t seed = (uint32_t)p.i("seed");
    b.scale(sc);
    const V3 steel = mix(kZinc * 0.85, kRust, wear * 0.55);
    b.style(steel, METAL);
    // The mound of looped chain.
    for (int k = 0; k < 3; k++) {
        const double R = 0.15 - 0.02 * k + 0.02 * (hash01(seed, k) - 0.5);
        const double cx = (hash01(seed, 10 + k) - 0.5) * 0.04, cz = (hash01(seed, 20 + k) - 0.5) * 0.04;
        const double ph = hash01(seed, 30 + k) * 6.28;
        b.push();
        b.move(cx, 0.037 + 0.019 * k, cz);
        b.yaw(hash01(seed, 40 + k) * 360);
        b.pitch((hash01(seed, 50 + k) - 0.5) * 12);
        b.roll((hash01(seed, 60 + k) - 0.5) * 12);
        std::vector<V3> path;
        for (int i = 0; i < 14; i++) {
            const double a = i * 2 * M_PI / 14;
            const double rr = R * (1 + 0.1 * std::sin(3 * a + ph));
            path.push_back(V3(rr * std::cos(a) * 1.1, 0.009 * std::sin(2 * a + ph), rr * std::sin(a) * 0.82));
        }
        b.color(k & 1 ? steel : steel * 0.88);
        b.tube(path, 0.0085, 4, true);
        b.pop();
    }
    // A loop made of proper links, sat on the others.
    b.color(steel);
    {
        std::vector<V3> ring;
        for (int i = 0; i < 16; i++) {
            const double a = i * 2 * M_PI / 16;
            ring.push_back(V3(0.1 * std::cos(a) * (1 + 0.1 * std::sin(3 * a + 1)), 0.065 + 0.012 * std::sin(2 * a), 0.075 * std::sin(a)));
        }
        ring.push_back(ring[0]);
        linkRun(b, ring, 0.026, 0.036, 0.022, 0.0036);
    }
    // Cross-bars sprawled over the loops.
    b.color(steel * 0.8);
    for (int k = 0; k < 3; k++) {
        const double a = hash01(seed, 70 + k) * 6.28, r0 = 0.02 + 0.07 * hash01(seed, 80 + k);
        const V3 c(std::cos(a) * r0, 0.04 + 0.03 * hash01(seed, 90 + k), std::sin(a) * r0);
        const V3 d = norm(V3(hash01(seed, 100 + k) - 0.5, 0.2, hash01(seed, 110 + k) - 0.5));
        b.tube({c - d * 0.045, c + d * 0.045}, 0.0055, 4);
    }
    // A ladder section draped over the top: two side chains with rungs, running off the front.
    b.color(steel);
    const std::vector<V3> c0 = {V3(-0.06, 0.08, -0.06), V3(0.0, 0.11, -0.06), V3(0.06, 0.105, -0.065), V3(0.12, 0.08, -0.075), V3(0.17, 0.035, -0.09), V3(0.22, 0.012, -0.12)};
    const std::vector<V3> c1 = {V3(-0.06, 0.08, 0.0), V3(0.0, 0.11, 0.0), V3(0.06, 0.105, -0.005), V3(0.12, 0.08, -0.015), V3(0.17, 0.035, -0.04), V3(0.22, 0.012, -0.07)};
    const std::vector<V3> p0 = curve(c0, 5), p1 = curve(c1, 5);
    linkRun(b, p0, 0.026, 0.036, 0.022, 0.0036, V3(0.2, 1, 0.3));
    linkRun(b, p1, 0.026, 0.036, 0.022, 0.0036, V3(0.2, 1, 0.3));
    for (int k = 3; k < 4; k++) {
        const V3 a = p0[k * 6 + 1], e = p1[k * 6 + 1];
        linkRun(b, {a, e}, 0.026, 0.036, 0.021, 0.0034, V3(0, 1, 0));
    }
    // Tensioners and hooks on top.
    tensioner(b, V3(-0.04, 0.115, 0.06), V3(1, 0, 0.4), paint(p.i("paint")), 0.16);
    tensioner(b, V3(0.06, 0.07, 0.11), V3(0.5, 0, 1), paint(p.i("paint")), 0.14);
}

// The chains in their bag: a canvas drawstring sack, a loop handle, chain trailing from the mouth.
void chainBag(B& b, const P& p) {
    const double sc = p("size"), wear = p("wear");
    b.scale(sc);
    const V3 canvas = mix(paint(p.i("paint")), V3(0.35, 0.3, 0.22), 0.1 + wear * 0.4);
    const V3 steel = mix(kZinc * 0.85, kRust, wear * 0.5);
    b.push();
    b.move(0, 0.013, 0);
    b.pitch(-4);
    b.roll(5);
    b.style(canvas, MATTE);
    latheAlong(b, V3(0, 0, 0), V3(0, 1, 0),
               {V2(0, 0.0), V2(0.1, 0.0), V2(0.15, 0.025), V2(0.175, 0.08), V2(0.175, 0.13), V2(0.155, 0.19), V2(0.115, 0.245), V2(0.085, 0.29), V2(0.08, 0.32), V2(0.09, 0.355), V2(0.105, 0.39), V2(0.095, 0.4), V2(0.078, 0.385), V2(0.07, 0.34), V2(0.0, 0.31)}, 14);
    b.style(kBlack * 1.5, MATTE);
    b.cyl(V3(0, 0.37, 0), V3(0, 0.372, 0), 0.075, 14);
    b.style(canvas, MATTE);
    // Lumpy contents pressing on the cloth.
    b.color(canvas * 0.93);
    b.blob(V3(0.04, 0.12, 0.08), V3(0.1, 0.1, 0.07), 0.2, 5, 0, 0.1);
    b.blob(V3(-0.02, 0.09, -0.1), V3(0.09, 0.08, 0.07), 0.2, 8, 0, 0.1);
    // Bottom edge and seams: a darker base ring, vertical seam stitches.
    b.color(canvas * 0.6);
    latheAlong(b, V3(0, 0, 0), V3(0, 1, 0), {V2(0.1, 0), V2(0.16, 0.012), V2(0.16, 0.04), V2(0.1, 0.0)}, 16);
    // Drawstring cinch round the neck, and its tails.
    b.style(kBlack * 1.8, MATTE);
    b.torus(V3(0, 0.298, 0), 0.083, 0.0055, 12, 4);
    b.tube({V3(0.082, 0.298, 0.03), V3(0.12, 0.29, 0.02), V3(0.15, 0.26, 0.02), V3(0.158, 0.22, 0.015)}, 0.004, 5);
    b.tube({V3(0.082, 0.3, -0.01), V3(0.115, 0.285, -0.04), V3(0.14, 0.25, -0.06)}, 0.004, 5);
    // Carry handle: two straps across the body.
    b.style(canvas * 0.55, MATTE);
    b.tube({V3(0.0, 0.05, 0.172), V3(0.0, 0.17, 0.18), V3(0.0, 0.26, 0.13), V3(0.0, 0.33, 0.09)}, 0.012, 4);
    b.tube({V3(0.0, 0.05, -0.172), V3(0.0, 0.17, -0.18), V3(0.0, 0.26, -0.13), V3(0.0, 0.33, -0.09)}, 0.012, 4);
    // Printed label on the front.
    b.style(kWhite * 0.9, MATTE);
    b.push();
    b.move(0.171, 0.11, 0);
    b.pitch(-4);
    b.box(V3(0, 0, 0), V3(0.004, 0.06, 0.2), 0.001);
    b.pop();
    b.style(V3(0.1, 0.15, 0.3), MATTE);
    stamp(b, "SNOW", V3(0.1735, 0.11, 0), 0, 0.0075, 0.002);
    // Chain spilling from the mouth, down the left side.
    b.style(steel, METAL);
    {
        const double th = 1.15;
        auto pt = [&](double r, double y) { return V3(r * std::cos(th), y, r * std::sin(th)); };
        const std::vector<V3> sp = curve({V3(0.0, 0.36, 0.0), V3(0.03, 0.43, 0.0), pt(0.11, 0.425), pt(0.115, 0.37), pt(0.105, 0.31), pt(0.14, 0.23), pt(0.19, 0.13), pt(0.21, 0.04)}, 4);
        linkRun(b, sp, 0.026, 0.036, 0.022, 0.0036, V3(0, 0, 1));
    }
    b.pop();
    tensioner(b, V3(0.25, 0.006, -0.1), V3(0.3, 0, 1), paint(p.i("tens")), 0.15);
}

// Hung on a bracket: a steel back plate with a hook, the bundle folded and strapped, tails dangling.
void chainHung(B& b, const P& p) {
    const double sc = p("size"), wear = p("wear");
    const V3 steel = mix(kZinc * 0.85, kRust, wear * 0.5);
    const V3 plate = worn(paint(p.i("paint")), wear);
    // Free-standing back plate with a foot.
    b.style(plate, PAINT);
    b.box(V3(-0.1, 0.27, 0), V3(0.012, 0.5, 0.15), 0.004);
    b.extrudeUp(roundedRect(0.22, 0.2, 0.01, 3), 0.012);
    b.extrude({V2(-0.095, 0.012), V2(-0.095, 0.2), V2(0.0, 0.012)}, 0.012, 0.07);
    b.extrude({V2(-0.095, 0.012), V2(-0.095, 0.2), V2(0.0, 0.012)}, 0.012, -0.07);
    for (int sz = -1; sz <= 1; sz += 2)
        for (int sy = 0; sy < 2; sy++) bolt(b, V3(-0.0935, 0.08 + sy * 0.38, sz * 0.055), V3(1, 0, 0), 0.0085);
    b.style(kWhite * 0.9, MATTE);
    b.box(V3(-0.0935, 0.22, 0), V3(0.002, 0.06, 0.09), 0.0005);
    b.style(kBlack, MATTE);
    stamp(b, "CHAIN", V3(-0.0925, 0.22, 0), 0, 0.0028, 0.001);
    // The hook: a stout rod out from the plate and curling up.
    b.style(steel * 0.8, METAL);
    b.tube({V3(-0.094, 0.4, 0), V3(-0.02, 0.4, 0), V3(0.03, 0.4, 0), V3(0.065, 0.415, 0), V3(0.075, 0.44, 0), V3(0.06, 0.465, 0)}, 0.0085, 8);
    b.box(V3(-0.094, 0.4, 0), V3(0.012, 0.05, 0.05), 0.004);
    b.sphere(V3(0.06, 0.468, 0), V3(0.0105, 0.0105, 0.0105), 1);
    // The bundle: loops of chain hanging in a fan from the hook.
    b.scale(sc);
    b.style(steel, METAL);
    for (int k = 0; k < 6; k++) {
        b.push();
        b.move(0.04, 0, 0);
        b.yaw(-80 + k * 32);
        if (k == 2) {  // this one in proper links
            std::vector<V3> ring;
            for (int i = 0; i < 12; i++) {
                const double a = i * 2 * M_PI / 12;
                ring.push_back(V3(0.04 * std::sin(a), 0.4 - 0.115 * (1 - std::cos(a)), 0.0));
            }
            ring.push_back(ring[0]);
            b.color(steel);
            linkRun(b, ring, 0.026, 0.036, 0.022, 0.0036);
            b.pop();
            continue;
        }
        std::vector<V3> path;
        const double H = 0.31 - 0.02 * (k % 3), W = 0.036 + 0.012 * (k % 2);
        for (int i = 0; i < 14; i++) {
            const double a = i * 2 * M_PI / 14;
            path.push_back(V3(W * std::sin(a), 0.4 - H / 2 * (1 - std::cos(a)) * 1.0 + 0.0, 0.0));
        }
        b.color(k & 1 ? steel : steel * 0.85);
        b.tube(path, 0.0065, 4, true);
        b.pop();
    }
    // Strap round the middle and a tensioner hooked through.
    b.style(paint(p.i("tens")), MATTE);
    b.push();
    b.move(0.04, 0.24, 0);
    b.torus(V3(0, 0, 0), 0.032, 0.0085, 14, 5);
    b.pop();
    tensioner(b, V3(0.075, 0.2, 0.0), V3(0, 1, 0.1), paint(p.i("tens")), 0.1);
    // Chain tails hanging off the bottom end.
    b.style(steel, METAL);
    linkRun(b, {V3(0.04, 0.12, -0.012), V3(0.035, 0.07, -0.02), V3(0.04, 0.03, -0.05)}, 0.026, 0.036, 0.022, 0.0036);
}

Reg chainBundle({"chain_bundle", "SNOW CHAINS", "TOOLS", "A set of snow chains as carried: loot, a shop item, kit on a rack", 0.45,
                 {
                     {"LOOSE HEAP", "Chains in a heap with a ladder section on top and two tensioners",
                      {num("size", "SIZE", 1, 0.8, 1.25), paintParam(0, "paint", "TENSIONER"), num("wear", "WEAR", 0.25, 0, 1), seedParam(3)},
                      chainHeap},
                     {"IN A BAG", "Canvas drawstring sack, label on the front, chain spilling out of the mouth",
                      {num("size", "SIZE", 1, 0.8, 1.25), paintParam(1, "paint", "CANVAS"), paintParam(4, "tens", "TENSIONER"), num("wear", "WEAR", 0.3, 0, 1)},
                      chainBag},
                     {"ON A HOOK", "Strapped bundle hanging from a bracket hook on a back plate",
                      {num("size", "SIZE", 1, 0.8, 1.25), paintParam(9, "paint", "PLATE"), paintParam(0, "tens", "STRAP"), num("wear", "WEAR", 0.25, 0, 1)},
                      chainHung},
                 }});

// =============================================================================================
// 3. SHOVEL
// =============================================================================================

// Flat scabs of rust on a horizontal face (centre c, extents sx by sz).
void rustFlat(B& b, const V3& c, double sx, double sz, int n, double amount, uint32_t seed) {
    if (amount <= 0.01) return;
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kRust, MATTE);
    const int cnt = (int)std::lround(n * amount);
    for (int i = 0; i < cnt; i++) {
        const double u = hash01(seed, i * 3), v = hash01(seed, i * 3 + 1), w = hash01(seed, i * 3 + 2);
        const double q = 0.008 + 0.016 * w;
        b.box(V3(c.x + (u - 0.5) * sx, c.y + 0.0006, c.z + (v - 0.5) * sz), V3(q, 0.0012, q * 0.7));
    }
    b.style(keep, km);
}

// The pose wrapper: lying flat as built (+X the blade end), or stood on the blade tip.
void shovelPose(B& b, const P& p, double tipX) {
    if (p.i("stand")) {
        b.move(0, tipX, 0);
        b.pitch(-90);
    }
}

// A folding entrenching tool: ribbed handle with a lanyard loop, a locking collar and hinge,
// a pointed blade with a saw edge. MODE: spade, or folded forward as a hoe.
void entrenchingTool(B& b, const P& p) {
    const double wear = p("wear");
    const bool hoe = p.i("mode") != 0;
    const V3 col = worn(paint(p.i("paint")), wear);
    const double r = 0.0165, ya = r;
    const double hx = 0.12;  // the hinge pin (x)
    if (!p.i("stand")) b.move(0, 0.008, 0);  // the collar is fatter than the handle
    shovelPose(b, p, hoe ? 0.144 : 0.335);
    // The handle: a painted tube with a black rubber grip.
    b.style(col, PAINT);
    b.cyl(V3(-0.3, ya, 0), V3(hx - 0.02, ya, 0), r, 12);
    b.style(kRubber, MATTE);
    b.cyl(V3(-0.3, ya, 0), V3(-0.12, ya, 0), 0.0195, 14);
    b.style(kBlack * 1.6, MATTE);
    for (int i = 0; i < 4; i++) {
        const double x = -0.275 + i * 0.045;
        along(b, V3(x, ya, 0), V3(1, 0, 0), [&] { b.torus(V3(0, 0, 0), 0.0196, 0.0022, 12, 4); });
    }
    b.cyl(V3(-0.303, ya, 0), V3(-0.298, ya, 0), 0.021, 14);
    // Lanyard loop through the end cap.
    b.style(kSteel * 0.8, METAL);
    b.push();
    b.roll(90);
    b.torus(V3(-0.315, 0, -ya), 0.0095, 0.0022, 10, 4);
    b.pop();
    // The locking collar (a knurled nut) and the hinge block.
    b.style(kDark * 1.5, METAL);
    latheAlong(b, V3(hx - 0.06, ya, 0), V3(1, 0, 0), {V2(0, 0), V2(0.0215, 0), V2(0.0245, 0.004), V2(0.0245, 0.03), V2(0.0215, 0.034), V2(0, 0.034)}, 12);
    b.style(kSteel * 0.7, METAL);
    b.box(V3(hx + 0.002, ya, 0), V3(0.045, 0.033, 0.05), 0.006);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(hx, ya, sz * 0.026), V3(hx, ya, sz * 0.033), 0.0095, 8);
    // The blade: pivots at the hinge, turned up 90 degrees in hoe mode.
    b.push();
    b.move(hx, ya, 0);
    if (hoe) b.pitch(90);
    b.style(col * 0.95, PAINT);
    std::vector<V2> poly = {V2(0.01, 0.036), V2(0.04, 0.05), V2(0.09, 0.078), V2(0.15, 0.078), V2(0.19, 0.05), V2(0.215, 0.0), V2(0.19, -0.05), V2(0.15, -0.078)};
    // The saw edge on the right: small teeth between x = 0.14 and 0.04.
    for (int k = 0; k < 6; k++) {
        const double x = 0.135 - k * 0.0175;
        const double w = 0.078 - (0.135 - x) * 0.0 - (x < 0.09 ? (0.09 - x) * 0.56 : 0.0);
        poly.push_back(V2(x, -w - 0.0075));
        poly.push_back(V2(x - 0.0085, -w + 0.002));
    }
    poly.push_back(V2(0.01, -0.036));
    b.extrudeUp(poly, 0.003, -ya);
    // Stiffening ridge and rolled edge on the left, and the thick steel neck.
    b.style(col * 0.8, PAINT);
    b.extrude({V2(0.0, -ya + 0.003), V2(0.19, -ya + 0.003), V2(0.2, -ya + 0.0035), V2(0.0, -ya + 0.012)}, 0.03);
    b.style(kSteel * 0.7, METAL);
    b.box(V3(0.02, -ya * 0.5, 0), V3(0.04, ya * 0.98, 0.06), 0.004);
    rustFlat(b, V3(0.1, -ya + 0.003, 0), 0.14, 0.12, 8, wear, 41);
    b.pop();
}

// A long-handled digging spade: ash shaft, steel D grip, a squared blade with foot steps.
void longSpade(B& b, const P& p) {
    const double wear = p("wear");
    const double Lt = 1.36, bl = 0.29, th = 9 * M_PI / 180;
    const double tipX = Lt / 2, r = 0.0165;
    shovelPose(b, p, tipX);
    const V3 steel = mix(kSteel * 0.65, kRust, wear * 0.4), ash = mix(V3(0.72, 0.58, 0.38), V3(0.5, 0.4, 0.3), wear * 0.4);
    auto tilted = [&](double x, double y) { return V3(tipX + x * std::cos(th) + y * std::sin(th), -x * std::sin(th) + y * std::cos(th), 0); };
    // Blade (tilted back from the tip, which rests on the ground).
    b.push();
    b.move(tipX, 0, 0);
    b.pitch(-9);
    b.style(steel, METAL);
    std::vector<V2> poly = {V2(-bl, 0.04), V2(-bl + 0.04, 0.075), V2(-bl + 0.08, 0.095), V2(-0.04, 0.095), V2(-0.007, 0.07), V2(0.0, 0.04), V2(0.0, -0.04), V2(-0.007, -0.07),
                            V2(-0.04, -0.095), V2(-bl + 0.08, -0.095), V2(-bl + 0.04, -0.075), V2(-bl, -0.04)};
    b.extrudeUp(poly, 0.0028);
    // Worn bright edge across the cutting end.
    b.style(kSteel * 1.15, METAL);
    b.box(V3(-0.012, 0.0032, 0), V3(0.024, 0.0012, 0.12), 0.0004);
    // Ridge down the middle and the folded foot steps at the shoulders.
    b.style(steel * 0.9, METAL);
    b.extrude({V2(-bl, 0.0), V2(-0.08, 0.0028), V2(-0.08, 0.0036), V2(-bl, 0.016)}, 0.034);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(-bl + 0.05, 0.008, sz * 0.082), V3(0.028, 0.014, 0.022), 0.004);
    // Socket: a rolled steel cone round the foot of the shaft, with rivets.
    b.style(steel * 0.8, METAL);
    b.cone(V3(-bl - 0.1, 0.025, 0), V3(-bl + 0.07, 0.009, 0), 0.0215, 0.0105, 12);
    b.style(kSteel * 0.6, METAL);
    for (int k = 0; k < 2; k++) b.cyl(V3(-bl - 0.045 + k * 0.05, 0.0, 0.0) + V3(0, 0.026 - k * 0.006, -0.02), V3(-bl - 0.045 + k * 0.05, 0.0, 0.0) + V3(0, 0.026 - k * 0.006, 0.02), 0.0042, 6);
    rustFlat(b, V3(-0.12, 0.0028, 0), 0.2, 0.16, 8, wear, 17);
    b.pop();
    // Shaft: ash, running from the socket down to the ground at the grip.
    const V3 S = tilted(-bl - 0.1, 0.025), Hd(-tipX + 0.17, r, 0);
    b.style(ash, WOOD);
    b.cyl(S, Hd, r, 12);
    // D grip: a steel loop lying flat, a wooden bar across its end.
    const double hx = Hd.x;
    b.style(steel * 0.8, METAL);
    std::vector<V3> loop;
    const double zs[] = {0.012, 0.05, 0.075, 0.078, 0.078};
    const double xs[] = {0.0, -0.022, -0.06, -0.11, -0.145};
    for (int i = 0; i < 5; i++) loop.push_back(V3(hx + xs[i], r, zs[i]));
    for (int i = 3; i >= 0; i--) loop.push_back(V3(hx + xs[i], r, -zs[i]));
    // (the loop is open across the end; the grip bar closes it)
    b.tube(loop, 0.0105, 6);
    b.style(ash * 0.95, WOOD);
    b.cyl(V3(hx - 0.158, r + 0.002, -0.088), V3(hx - 0.158, r + 0.002, 0.088), 0.0175, 12);
    b.style(steel, METAL);
    b.cyl(V3(hx - 0.01, r + 0.0005, -0.019), V3(hx - 0.01, r + 0.0005, 0.019), 0.0165, 8);  // the ferrule collar
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(hx - 0.152, r, sz * 0.094), V3(hx - 0.152, r, sz * 0.097), 0.011, 8);
}

// The short digging shovel: a round-point scoop blade, a shorter shaft, a moulded D grip.
void shortShovel(B& b, const P& p) {
    const double wear = p("wear");
    const double Lt = 0.98, bl = 0.31, th = 10 * M_PI / 180;
    const double tipX = Lt / 2, r = 0.0155;
    shovelPose(b, p, tipX);
    const V3 steel = mix(kDark * 1.8, kRust, wear * 0.5), ash = mix(V3(0.62, 0.45, 0.28), V3(0.4, 0.33, 0.27), wear * 0.4), grip = paint(p.i("paint"));
    auto tilted = [&](double x, double y) { return V3(tipX + x * std::cos(th) + y * std::sin(th), -x * std::sin(th) + y * std::cos(th), 0); };
    b.push();
    b.move(tipX, 0, 0);
    b.pitch(-10);
    b.style(steel, METAL);
    // Round-point blade: widest a third of the way down, coming to a point.
    std::vector<V2> poly;
    const int n = 12;
    for (int i = 0; i <= n; i++) {
        const double u = (double)i / n;  // 0 at the tip, 1 at the socket
        const double x = -bl * u;
        const double hw = u < 0.62 ? 0.118 * std::sqrt(std::sin(u / 0.62 * M_PI / 2)) : 0.118 - (u - 0.62) / 0.38 * 0.078;
        poly.push_back(V2(x, hw));
    }
    for (int i = n; i >= 0; i--) {
        const double u = (double)i / n;
        const double x = -bl * u;
        const double hw = u < 0.62 ? 0.118 * std::sqrt(std::sin(u / 0.62 * M_PI / 2)) : 0.118 - (u - 0.62) / 0.38 * 0.078;
        poly.push_back(V2(x, -hw));
    }
    poly.erase(poly.begin() + n + 1);  // the tip point is shared
    b.extrudeUp(poly, 0.0028);
    b.style(kSteel * 0.9, METAL);
    b.box(V3(-0.006, 0.0032, 0), V3(0.012, 0.0012, 0.05), 0.0004);  // worn bright point
    // Dished: a raised rib down the middle and a rolled top edge.
    b.style(steel * 0.85, METAL);
    b.extrude({V2(-bl, 0.0), V2(-0.1, 0.0028), V2(-0.1, 0.0034), V2(-bl, 0.02)}, 0.05);
    b.style(steel * 1.1, METAL);
    b.box(V3(-bl + 0.012, 0.0055, 0), V3(0.022, 0.012, 0.1), 0.004);
    // Socket and its strap.
    b.style(steel * 0.9, METAL);
    b.cone(V3(-bl - 0.1, 0.027, 0), V3(-bl + 0.07, 0.01, 0), 0.0205, 0.0105, 12);
    b.style(kSteel * 0.6, METAL);
    for (int k = 0; k < 2; k++) b.cyl(V3(-bl - 0.05 + k * 0.045, 0.029 - k * 0.006, -0.02), V3(-bl - 0.05 + k * 0.045, 0.029 - k * 0.006, 0.02), 0.0042, 6);
    // Stamped name across the blade.
    b.style(kWhite * 0.8, MATTE);
    b.push();
    b.move(-0.16, 0.0028, 0);
    b.pitch(90);
    b.letters("DIG", V3(0, 0, 0), 0.0042, 0.0014);
    b.pop();
    rustFlat(b, V3(-0.16, 0.0028, 0), 0.2, 0.18, 9, wear, 29);
    b.pop();
    // Short ash shaft and a moulded D grip.
    const V3 S = tilted(-bl - 0.1, 0.027), Hd(-tipX + 0.12, r, 0);
    b.style(ash, WOOD);
    b.cyl(S, Hd, r, 12);
    b.style(kSteel * 0.8, METAL);
    b.cyl(V3(Hd.x + 0.03, r + 0.003, 0), V3(Hd.x - 0.02, r + 0.003, 0), 0.0185, 12);  // ferrule
    b.style(grip, MATTE);
    {
        // closed D: down one side, across the grip, back up the other
        const double zs[] = {0.0, 0.05, 0.08, 0.085, 0.085, 0.05};
        const double xs[] = {0.0, -0.01, -0.055, -0.1, -0.135, -0.165};
        std::vector<V3> d2;
        for (int i = 1; i < 6; i++) d2.push_back(V3(Hd.x - 0.02 + xs[i], r + 0.002, zs[i]));
        for (int i = 5; i >= 1; i--) d2.push_back(V3(Hd.x - 0.02 + xs[i], r + 0.002, -zs[i]));
        b.tube(d2, 0.0145, 8, true);
    }
}

Reg shovel({"shovel", "SHOVEL", "TOOLS", "Digging kit: lies on the ground or stands on its blade tip (stand 0/1)", 1.0,
            {
                {"ENTRENCHING TOOL", "Folding military e-tool: ribbed handle, locking collar, saw-edged blade (spade or hoe mode)",
                 {num("stand", "STAND UP", 0, 0, 1, 1), choice("mode", "MODE", 0, {"SPADE", "HOE"}), paintParam(1), num("wear", "WEAR", 0.25, 0, 1)},
                 entrenchingTool},
                {"LONG SPADE", "1.35 m ash-handled digging spade with steel D grip and foot steps",
                 {num("stand", "STAND UP", 0, 0, 1, 1), num("wear", "WEAR", 0.3, 0, 1)},
                 longSpade},
                {"D-HANDLE SHOVEL", "Short round-point shovel, moulded D grip, stamped blade",
                 {num("stand", "STAND UP", 0, 0, 1, 1), paintParam(4, "paint", "GRIP"), num("wear", "WEAR", 0.35, 0, 1)},
                 shortShovel},
            }});

// =============================================================================================
// 4. AIR COMPRESSOR
// =============================================================================================

// A pressure gauge: dark can, chrome bezel, white dial under glass with a needle. Faces `dir`.
void gauge(B& b, const V3& c, const V3& dir, double R, double needle = 140) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    along(b, c, dir, [&] {
        b.style(kBlack * 1.3, MATTE);
        b.lathe(V3(0, -R * 0.55, 0), {V2(0, 0), V2(R * 0.93, 0), V2(R * 0.93, R * 0.55)}, 10, false);
        b.style(kSteel, CHROME);
        b.lathe(V3(0, 0, 0), {V2(R * 0.82, 0.0), V2(R, 0.0), V2(R, 0.007), V2(R * 0.84, 0.01)}, 10);
        b.style(kWhite, MATTE);
        b.cyl(V3(0, 0.001, 0), V3(0, 0.0032, 0), R * 0.83, 10);
        b.style(kBlack, MATTE);
        for (int i = 0; i < 3; i++) {
            const double a = (-75 + i * 75) * M_PI / 180;
            b.box(V3(R * 0.68 * std::cos(a), 0.0042, R * 0.68 * std::sin(a)), V3(R * 0.12, 0.0012, R * 0.05), 0);
        }
        b.style(V3(0.7, 0.05, 0.04), MATTE);
        b.push();
        b.yaw(needle);
        b.box(V3(R * 0.3, 0.0056, 0), V3(R * 0.62, 0.0012, R * 0.07), 0);
        b.pop();
        b.style(V3(0.6, 0.7, 0.8), GLASS);
        b.cyl(V3(0, 0.0078, 0), V3(0, 0.0086, 0), R * 0.83, 10);
    });
    b.style(keep, km);
}

// A brass tyre chuck on the end of a hose, pointing along dir.
void chuck(B& b, const V3& at, const V3& dir) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kBrass, METAL);
    latheAlong(b, at, dir, {V2(0, 0), V2(0.0085, 0), V2(0.0085, 0.012), V2(0.0115, 0.014), V2(0.0115, 0.04), V2(0.009, 0.044), V2(0, 0.044)}, 8);
    b.style(kBlack, MATTE);
    const V3 d = norm(dir);
    b.cyl(at + d * 0.02, at + d * 0.02 + V3(0, 0.03, 0), 0.0045, 6);
    b.style(keep, km);
}

// A cooling-fin stack: a pump head with fins, standing along dir from base.
void finnedHead(B& b, const V3& base, const V3& dir, double R, double H, int fins) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kAlu * 0.85, METAL);
    std::vector<V2> prof = {V2(0, 0)};
    for (int i = 0; i < fins; i++) {
        const double y0 = H * 0.08 + (H * 0.8) * i / fins, dy = H * 0.8 / fins;
        prof.push_back(V2(R, y0)), prof.push_back(V2(R, y0 + dy * 0.45)), prof.push_back(V2(R * 0.72, y0 + dy * 0.5));
    }
    prof.push_back(V2(R * 0.7, H * 0.93)), prof.push_back(V2(R * 0.7, H)), prof.push_back(V2(0, H));
    latheAlong(b, base, dir, prof, 10, false);
    b.style(keep, km);
}

// Hard carry case, open: a dark shell with orange latches, the lid up behind showing the maker's
// name; inside the pump, a gauge, a coiled hose, the 12 V lead and the fitting kit.
void compCase(B& b, const P& p) {
    const double wear = p("wear");
    const V3 shell = worn(kDark * 1.6, wear * 0.5), acc = paint(p.i("paint"));
    const double W = 0.40, D = 0.30, Hb = 0.115, t = 0.008;
    b.move(0, 0.004, 0);  // the feet stand on the ground
    // Base shell: floor and four walls.
    b.style(shell, MATTE);
    b.box(V3(0, 0.008, 0), V3(W, 0.016, D), 0.006);
    b.box(V3(W / 2 - t / 2, Hb / 2, 0), V3(t, Hb, D), 0.0);          // front wall
    b.box(V3(-W / 2 + t / 2, Hb / 2, 0), V3(t, Hb, D), 0.0);         // back wall
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, Hb / 2, sz * (D / 2 - t / 2)), V3(W, Hb, t), 0.0);
    b.style(shell * 0.7, MATTE);
    b.box(V3(W / 2 - 0.002, Hb + 0.001, 0), V3(0.012, 0.004, D), 0.001);  // the rim seal
    b.box(V3(-W / 2 + 0.002, Hb + 0.001, 0), V3(0.012, 0.004, D), 0.001);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, Hb + 0.001, sz * (D / 2 - 0.002)), V3(W, 0.004, 0.012), 0.001);
    // Feet.
    b.style(kRubber, MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.16, -0.002, sz * 0.11), V3(0.04, 0.004, 0.03), 0.0);
    // Foam bed with the pockets: a grey tray in two lumps.
    b.style(V3(0.2, 0.2, 0.21), MATTE);
    b.box(V3(-0.1, 0.04, 0.0), V3(0.18, 0.05, 0.27), 0.0);  // pump bed
    b.box(V3(0.12, 0.03, 0.0), V3(0.14, 0.028, 0.27), 0.0);  // hose bed (low)
    // The compressor: cylinder lying across, aluminium finned head on top.
    b.style(acc * 0.9, PAINT);
    latheAlong(b, V3(-0.12, 0.085, -0.115), V3(0, 0, 1), {V2(0, 0), V2(0.046, 0), V2(0.05, 0.01), V2(0.05, 0.19), V2(0.046, 0.2), V2(0, 0.2)}, 12);
    b.style(kBlack, MATTE);
    b.cyl(V3(-0.12, 0.085, -0.117), V3(-0.12, 0.085, -0.123), 0.03, 12);
    finnedHead(b, V3(-0.12, 0.125, -0.035), V3(0, 1, 0), 0.034, 0.05, 3);
    b.style(kAlu, METAL);
    b.box(V3(-0.12, 0.13, -0.035), V3(0.03, 0.02, 0.07), 0.004);
    // Pressure line from the head to the gauge.
    b.style(kBrass, METAL);
    b.tube({V3(-0.12, 0.17, -0.035), V3(-0.12, 0.185, 0.02), V3(-0.07, 0.185, 0.08), V3(0.0, 0.1, 0.1), V3(0.03, 0.075, 0.11)}, 0.0045, 5);
    gauge(b, V3(0.03, 0.075, 0.1), V3(1, 0, 0), 0.036);
    // Rocker switch and its housing.
    b.style(kBlack, MATTE);
    b.box(V3(-0.17, 0.08, 0.06), V3(0.04, 0.04, 0.05), 0.006);
    b.style(acc, MATTE);
    b.box(V3(-0.17, 0.103, 0.06), V3(0.02, 0.006, 0.03), 0.002);
    // Coiled hose lying flat in its bed, with the chuck out on the lip.
    b.style(acc * 0.9, MATTE);
    coil(b, V3(0.115, 0.045, -0.06), V3(0, 1, 0), 0.062, 0.0085, 2.5, 0.022, 5, 0.056);
    chuck(b, V3(0.15, 0.036, 0.075), V3(0, 0, 1));
    b.style(acc * 0.9, MATTE);
    b.tube({V3(0.115 + 0.062, 0.045, -0.06), V3(0.17, 0.045, -0.02), V3(0.16, 0.04, 0.03), V3(0.15, 0.036, 0.07)}, 0.0085, 6);
    // The 12 V lead with its plug, coiled in a loose loop.
    b.style(kBlack, MATTE);
    b.tube({V3(-0.17, 0.08, 0.085), V3(-0.19, 0.065, 0.11), V3(-0.15, 0.06, 0.13), V3(-0.1, 0.07, 0.125), V3(-0.08, 0.075, 0.1)}, 0.0045, 5);
    b.style(kSteel * 0.85, METAL);
    b.cyl(V3(-0.1, 0.07, 0.125), V3(-0.07, 0.07, 0.125), 0.009, 8);
    b.style(acc, MATTE);
    b.cyl(V3(-0.07, 0.07, 0.125), V3(-0.04, 0.07, 0.125), 0.0105, 8);
    // Nozzle kit: three cones in a row.
    b.style(kBrass, METAL);
    for (int i = 0; i < 2; i++) {
        const double z = -0.09 + i * 0.03;
        b.cone(V3(0.19 - 0.03, 0.036, z), V3(0.19 - 0.075 + 0.0, 0.036, z), 0.0095, 0.003, 7);
    }
    // Front handle and latches.
    b.style(shell * 1.2, MATTE);
    b.tube({V3(W / 2, 0.075, -0.06), V3(W / 2 + 0.03, 0.075, -0.055), V3(W / 2 + 0.03, 0.075, 0.055), V3(W / 2, 0.075, 0.06)}, 0.01, 6);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(kSteel * 0.8, METAL);
        b.box(V3(W / 2 + 0.004, 0.1, sz * 0.1), V3(0.008, 0.026, 0.03), 0.002);
        b.style(acc, MATTE);
        b.box(V3(W / 2 + 0.01, 0.092, sz * 0.1), V3(0.008, 0.012, 0.022), 0.002);
    }
    // The lid, hinged along the back edge and swung well past upright.
    b.push();
    b.move(-W / 2 + 0.004, Hb, 0);
    b.pitch(100);
    b.style(shell, MATTE);
    b.box(V3(W / 2, 0.045, 0), V3(W, 0.008, D), 0.004);
    b.box(V3(W - t / 2, 0.022, 0), V3(t, 0.044, D), 0.0);
    b.box(V3(t / 2, 0.022, 0), V3(t, 0.044, D), 0.0);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(W / 2, 0.022, sz * (D / 2 - t / 2)), V3(W, 0.044, t), 0.0);
    b.style(kDark * 0.9, MATTE);
    b.box(V3(W / 2, 0.038, 0), V3(W - 0.03, 0.01, D - 0.03), 0.0);  // lining
    // Maker's name inside the lid, and a label.
    b.style(acc, MATTE);
    b.push();
    b.move(W / 2 + 0.02, 0.0325, 0);
    b.pitch(-90);
    b.yaw(0);
    b.letters("AIR", V3(0, 0, 0), 0.0125, 0.003);
    b.move(0, 0, 0);
    b.pop();
    b.style(kWhite * 0.9, MATTE);
    b.push();
    b.move(W / 2 + 0.02, 0.0325, 0);
    b.pitch(-90);
    b.box(V3(-0.07, 0.0, 0.0), V3(0.004, 0.012, 0.2), 0.0005);
    b.pop();
    // Lid hinges.
    b.style(kSteel * 0.7, METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(-0.004, 0.0, sz * 0.1 - 0.015), V3(-0.004, 0.0, sz * 0.1 + 0.015), 0.009, 8);
    b.pop();
}

// The bare compressor on a steel base: motor and pump head, a big dial gauge, a coil of red hose.
void compBare(B& b, const P& p) {
    const double wear = p("wear");
    const V3 motor = worn(paint(p.i("paint")), wear), hose = paint(p.i("hose"));
    const double hb = 0.012;
    // Base plate and rubber feet.
    b.style(kSteel * 0.75, METAL);
    b.box(V3(0, hb / 2 + 0.008, 0), V3(0.40, hb, 0.28), 0.003);
    b.style(kRubber, MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.17, 0.004, sz * 0.11), V3(0.04, 0.008, 0.04), 0.0);
    b.style(kSteel * 0.95, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(sx * 0.18, 0.02, sz * 0.12), V3(sx * 0.18, 0.0235, sz * 0.12), 0.007, 6);
    // Motor: a painted can with ribs, a fan cover at the back.
    b.style(motor, PAINT);
    latheAlong(b, V3(-0.19, 0.095, 0.03), V3(1, 0, 0), {V2(0, 0), V2(0.054, 0), V2(0.062, 0.01), V2(0.062, 0.14), V2(0.058, 0.15), V2(0.05, 0.155), V2(0, 0.155)}, 14);
    b.style(motor * 0.75, PAINT);
    along(b, V3(-0.11, 0.095, 0.03), V3(1, 0, 0), [&] { b.torus(V3(0, 0, 0), 0.0635, 0.0035, 12, 4); });
    b.style(kDark, METAL);
    latheAlong(b, V3(-0.205, 0.095, 0.03), V3(1, 0, 0), {V2(0, 0), V2(0.056, 0), V2(0.056, 0.018), V2(0, 0.02)}, 12);
    for (int i = -1; i <= 1; i++) b.box(V3(-0.2075, 0.095, 0.03 + i * 0.022), V3(0.004, 0.09, 0.008), 0);  // fan guard slots
    // Crankcase and pump head.
    b.style(kAlu * 0.9, METAL);
    latheAlong(b, V3(-0.04, 0.095, 0.03), V3(1, 0, 0), {V2(0, 0), V2(0.044, 0), V2(0.044, 0.1), V2(0, 0.1)}, 12);
    finnedHead(b, V3(0.02, 0.095, 0.03), V3(0, 1, 0), 0.038, 0.1, 3);
    b.style(kBlack * 1.4, MATTE);
    b.box(V3(0.02, 0.2, 0.03), V3(0.07, 0.014, 0.07), 0.004);  // valve cover
    // Copper line to the gauge.
    b.style(V3(0.7, 0.4, 0.22), METAL);
    b.tube({V3(0.06, 0.19, 0.03), V3(0.1, 0.19, 0.07), V3(0.15, 0.17, 0.09), V3(0.17, 0.15, 0.09)}, 0.0045, 5);
    // Gauge on a stout bracket at the front.
    b.style(kSteel * 0.7, METAL);
    b.box(V3(0.17, 0.095, 0.09), V3(0.03, 0.15, 0.03), 0.006);
    gauge(b, V3(0.186, 0.165, 0.09), V3(1, 0, 0), 0.043, 150);
    // Pressure relief valve and the on/off switch.
    b.style(kBrass, METAL);
    b.cyl(V3(0.02, 0.21, 0.03), V3(0.02, 0.24, 0.03), 0.007, 8);
    b.sphere(V3(0.02, 0.245, 0.03), V3(0.0095, 0.0095, 0.0095), 1);
    b.style(kBlack, MATTE);
    b.box(V3(-0.1, 0.17, 0.095), V3(0.05, 0.035, 0.03), 0.005);
    b.style(paint(0), MATTE);
    b.box(V3(-0.1, 0.19, 0.095), V3(0.026, 0.006, 0.018), 0.002);
    // Carry frame: a bent steel tube over the top.
    b.style(kSteel * 0.85, METAL);
    b.tube({V3(-0.17, 0.02, -0.09), V3(-0.17, 0.2, -0.09), V3(-0.14, 0.255, -0.09), V3(-0.04, 0.265, -0.09), V3(0.0, 0.24, -0.09)}, 0.008, 6);
    b.tube({V3(-0.17, 0.02, -0.11), V3(-0.17, 0.2, -0.11), V3(-0.14, 0.255, -0.11), V3(-0.04, 0.265, -0.11), V3(0.0, 0.24, -0.11)}, 0.0001, 3);
    b.style(kRubber, MATTE);
    b.cyl(V3(-0.13, 0.264, -0.09), V3(-0.06, 0.267, -0.09), 0.012, 8);
    // The coiled hose: lying round on the base, free end with a brass chuck.
    b.style(hose, MATTE);
    coil(b, V3(0.095, 0.022, -0.075), V3(0, 1, 0), 0.05, 0.0075, 3.5, 0.017, 5, 0.046);
    b.tube({V3(0.145, 0.026, -0.075), V3(0.17, 0.03, -0.05), V3(0.19, 0.03, -0.02), V3(0.2, 0.03, 0.03)}, 0.0075, 6);
    chuck(b, V3(0.2, 0.03, 0.03), V3(0.3, 0.0, 1));
    // Power lead with its clamps.
    b.style(kBlack, MATTE);
    b.tube({V3(-0.12, 0.095, 0.085), V3(-0.14, 0.04, 0.13), V3(-0.12, 0.025, 0.15), V3(-0.05, 0.02, 0.15)}, 0.0045, 5);
    // Stencilled name on the base lip and a rust spot or two.
    b.style(kWhite * 0.9, MATTE);
    stamp(b, "PSI", V3(0.2005, 0.026, -0.0), 0, 0.003, 0.001);
    rustSpots(b, V3(0, 0.02, 0), V3(0.38, 0.01, 0.27), 5, wear, 5);
}

// The tank-style unit: a lying air receiver on steel cradles, the pump on top, hose coiled round the tank.
void compTank(B& b, const P& p) {
    const double wear = p("wear");
    const V3 tank = worn(paint(p.i("paint")), wear), hose = paint(p.i("hose"));
    const double R = 0.078, cy = 0.05 + R;
    // The receiver: a lathe about the X axis, domed ends, a weld seam.
    b.style(tank, PAINT);
    latheAlong(b, V3(-0.19, cy, 0), V3(1, 0, 0), {V2(0, 0), V2(R * 0.5, 0.012), V2(R * 0.85, 0.032), V2(R, 0.06), V2(R, 0.31), V2(R * 0.85, 0.338), V2(R * 0.5, 0.358), V2(0, 0.37)}, 16);
    b.style(tank * 0.7, PAINT);
    along(b, V3(-0.01, cy, 0), V3(1, 0, 0), [&] { b.torus(V3(0, 0, 0), R + 0.001, 0.0026, 16, 4); });
    // Steel cradles with rubber feet.
    for (int sx = -1; sx <= 1; sx += 2) {
        b.style(kSteel * 0.6, METAL);
        b.push();
        b.move(sx * 0.1, 0, 0);
        b.yaw(90);
        std::vector<V2> cr = {V2(-0.105, 0.0), V2(-0.105, 0.05), V2(-0.088, 0.075), V2(-0.07, 0.082), V2(-0.0, 0.058), V2(0.07, 0.082), V2(0.088, 0.075), V2(0.105, 0.05), V2(0.105, 0.0), V2(0.085, 0.0), V2(0.085, 0.045), V2(0.0, 0.03), V2(-0.085, 0.045), V2(-0.085, 0.0)};
        b.extrude(cr, 0.016);
        b.pop();
        b.style(kRubber, MATTE);
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.1, 0.007, sz * 0.095), V3(0.06, 0.014, 0.022), 0.0);
    }
    // Drain valve (brass) under the tank, and the end boss.
    b.style(kBrass, METAL);
    b.cyl(V3(0.02, cy - R + 0.004, 0.0), V3(0.02, cy - R - 0.026, 0.0), 0.0085, 8);
    b.cyl(V3(0.02, cy - R - 0.03, -0.02), V3(0.02, cy - R - 0.03, 0.02), 0.0055, 6);
    // The motor and pump on a mounting plate on top of the tank.
    const double top = cy + R;
    b.style(kSteel * 0.7, METAL);
    b.box(V3(-0.02, top + 0.004, 0), V3(0.2, 0.01, 0.1), 0.003);
    b.style(tank * 0.85, PAINT);
    latheAlong(b, V3(-0.12, top + 0.06, 0), V3(1, 0, 0), {V2(0, 0), V2(0.05, 0), V2(0.05, 0.1), V2(0, 0.11)}, 14);
    b.style(kDark, METAL);
    latheAlong(b, V3(-0.128, top + 0.06, 0), V3(1, 0, 0), {V2(0, 0), V2(0.044, 0), V2(0.044, 0.016), V2(0, 0.016)}, 12);
    b.style(kAlu * 0.9, METAL);
    latheAlong(b, V3(-0.01, top + 0.06, 0), V3(1, 0, 0), {V2(0, 0), V2(0.034, 0), V2(0.034, 0.06), V2(0, 0.06)}, 10);
    finnedHead(b, V3(0.03, top + 0.06, 0), V3(0, 1, 0), 0.03, 0.075, 3);
    b.style(kBlack * 1.4, MATTE);
    b.box(V3(0.03, top + 0.14, 0), V3(0.06, 0.012, 0.06), 0.004);
    // Tubing from the head down to the tank, and the carry handle.
    b.style(kBrass, METAL);
    b.tube({V3(0.06, top + 0.1, 0.0), V3(0.085, top + 0.09, 0.03), V3(0.095, top + 0.03, 0.05), V3(0.095, top - 0.01, 0.05)}, 0.0045, 5);
    b.style(kSteel * 0.8, METAL);
    b.tube({V3(-0.1, top + 0.01, 0.045), V3(-0.1, top + 0.2, 0.04), V3(-0.06, top + 0.22, 0.04), V3(-0.0, top + 0.22, 0.04), V3(0.04, top + 0.2, 0.04), V3(0.05, top + 0.12, 0.04)}, 0.0075, 6);
    b.style(kRubber, MATTE);
    b.cyl(V3(-0.07, top + 0.222, 0.04), V3(0.0, top + 0.222, 0.04), 0.012, 8);
    // Gauges and regulator on the front end.
    const double gx = 0.19 + 0.0;
    gauge(b, V3(0.17, cy + 0.05, 0.035), V3(1, 0, 0.25), 0.032, 130);
    gauge(b, V3(0.17, cy + 0.05, -0.035), V3(1, 0, -0.25), 0.032, 70);
    (void)gx;
    b.style(kBrass, METAL);
    b.cyl(V3(0.18, cy - 0.01, 0.0), V3(0.2, cy - 0.01, 0.0), 0.014, 8);
    b.style(paint(p.i("paint")) * 0.8, MATTE);
    b.cyl(V3(0.195, cy - 0.01, 0.0), V3(0.21, cy - 0.01, 0.0), 0.011, 8);
    // The hose: coiled round the tank at the back, its chuck at the front.
    b.style(hose, MATTE);
    coil(b, V3(-0.17, cy, 0), V3(1, 0, 0), R + 0.0085, 0.0073, 3.5, 0.024, 5);
    b.tube({V3(-0.09, cy + R + 0.0085, 0.0), V3(-0.04, cy + R + 0.03, -0.03), V3(0.03, cy + R + 0.02, -0.07), V3(0.1, cy + 0.02, -R - 0.03), V3(0.14, 0.03, -0.14), V3(0.18, 0.02, -0.2)}, 0.0073, 5);
    chuck(b, V3(0.18, 0.02, -0.2), V3(0.8, 0.0, -1));
    // Paint chips and rust.
    b.style(kWhite * 0.9, MATTE);
    b.push();
    b.move(0, cy, R + 0.0012);
    b.yaw(-90);
    b.letters("AIR", V3(0, 0, 0), 0.0042, 0.0015);
    b.pop();
    rustSpots(b, V3(-0.05, cy - 0.02, 0), V3(0.3, 0.1, R * 2), 8, wear, 9);
}

Reg compressor({"air_compressor", "AIR COMPRESSOR", "TOOLS", "12 V tyre compressor: airs tyres back up after the trail", 0.38,
                {
                    {"CARRY CASE", "Hard case open, lid up: pump, gauge, coiled hose, 12 V lead and fittings",
                     {paintParam(4, "paint", "ACCENT"), num("wear", "WEAR", 0.2, 0, 1)},
                     compCase},
                    {"BARE UNIT", "Compressor on a steel base: finned head, dial gauge, carry frame, coiled red hose",
                     {paintParam(5), paintParam(0, "hose", "HOSE"), num("wear", "WEAR", 0.25, 0, 1)},
                     compBare},
                    {"TANK STYLE", "Lying air receiver on cradles with the pump on top and hose wrapped round the tank",
                     {paintParam(0), paintParam(8, "hose", "HOSE"), num("wear", "WEAR", 0.3, 0, 1)},
                     compTank},
                }});

// =============================================================================================
// 5. TOOLBOX
// =============================================================================================

// An open-ended spanner lying along +x from x = 0: a handle and a jawed head.
std::vector<V2> spannerPoly(double L, double rh, double hw) {
    std::vector<V2> pts;
    const double cx = L - rh * 0.8;
    pts.push_back(V2(0, -hw * 1.1));
    pts.push_back(V2(cx - rh * 0.9, -hw));
    const double jaw = 0.34;  // half the jaw opening as a fraction of the head radius
    const double a1 = std::asin(jaw);
    for (int i = 0; i <= 4; i++) {
        const double a = -2.0 + (-a1 + 2.0) * i / 4;  // from the neck round to the jaw edge
        pts.push_back(V2(cx + rh * std::cos(a), rh * std::sin(a)));
    }
    pts.push_back(V2(cx + rh * 0.1, -rh * jaw));
    pts.push_back(V2(cx + rh * 0.1, rh * jaw));
    for (int i = 0; i <= 4; i++) {
        const double a = a1 + (2.0 - a1) * i / 4;
        pts.push_back(V2(cx + rh * std::cos(a), rh * std::sin(a)));
    }
    pts.push_back(V2(cx - rh * 0.9, hw));
    pts.push_back(V2(0, hw * 1.1));
    return pts;
}

// A screwdriver along +x from the origin: steel blade, a fat handle.
void screwdriver(B& b, double L, double r, const V3& col) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kSteel, METAL);
    b.cyl(V3(L * 0.38, 0, 0), V3(L, 0, 0), 0.0032, 6);
    b.style(col, MATTE);
    latheAlong(b, V3(0, 0, 0), V3(1, 0, 0), {V2(0, 0), V2(r * 0.7, 0), V2(r, L * 0.06), V2(r, L * 0.3), V2(r * 0.75, L * 0.38), V2(0, L * 0.38)}, 8);
    b.style(keep, km);
}

// A hammer along +x from the origin: ash shaft, forged head at the far end.
void hammer(B& b, double L) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(V3(0.62, 0.46, 0.28), WOOD);
    b.cyl(V3(0, 0, 0), V3(L, 0, 0), 0.0105, 8);
    b.style(kSteel * 0.7, METAL);
    b.cyl(V3(L, 0, -0.022), V3(L, 0, 0.022), 0.0135, 8);
    b.cone(V3(L, 0, 0.022), V3(L, 0, 0.05), 0.0135, 0.007, 8);
    b.cone(V3(L, 0, -0.022), V3(L, 0, -0.04), 0.0135, 0.0165, 8);
    b.style(keep, km);
}

// The cantilever tool chest: a steel body and lid, a swing-out tray each side on parallel arms,
// a carry handle; the trays hold spanners, screwdrivers, a hammer. `open` 0 = trays up on top,
// 1 = swung out wide.
void cantilever(B& b, const P& p) {
    const double open = p("open"), wear = p("wear");
    const V3 col = worn(paint(p.i("paint")), wear), steel = kSteel * 0.8;
    const double bx = 0.11, bz = 0.25;
    // Body and lid.
    b.style(col, PAINT);
    b.box(V3(0, 0.06, 0), V3(2 * bx, 0.12, 2 * bz), 0.012);
    b.style(col * 1.05, PAINT);
    b.box(V3(0, 0.133, 0), V3(2 * bx + 0.004, 0.026, 2 * bz + 0.004), 0.01);
    // End caps and bolts, latches, hinges.
    b.style(steel, METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0, 0.06, sz * (bz + 0.003)), V3(2 * bx - 0.02, 0.1, 0.006), 0.003);
        for (int sx = -1; sx <= 1; sx += 2) bolt(b, V3(sx * (bx - 0.025), 0.03, sz * (bz + 0.006)), V3(0, 0, sz), 0.006);
    }
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(steel, METAL);
        b.box(V3(bx + 0.005, 0.12, sz * 0.12), V3(0.01, 0.04, 0.034), 0.002);  // latch plates
        b.style(kBlack, MATTE);
        b.box(V3(bx + 0.014, 0.108, sz * 0.12), V3(0.008, 0.014, 0.022), 0.0);
        b.style(steel, METAL);
        b.box(V3(-bx - 0.004, 0.125, sz * 0.15), V3(0.01, 0.036, 0.03), 0.002);  // hinges
    }
    // The carry handle: a bar over the lid on two stubs.
    b.style(kBlack * 1.3, MATTE);
    b.tube({V3(0, 0.146, -0.1), V3(0, 0.178, -0.095), V3(0, 0.192, -0.07), V3(0, 0.192, 0.07), V3(0, 0.178, 0.095), V3(0, 0.146, 0.1)}, 0.0125, 6);
    b.style(steel, METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(0, 0.145, sz * 0.1), V3(0, 0.158, sz * 0.1), 0.017, 8);
    // Name stencilled on the lid.
    b.style(kWhite * 0.92, MATTE);
    stamp(b, "FORT", V3(0.055, 0.146, 0), 4, 0.0075, 0.002);
    // The two trays on their parallel arms.
    const double a = (100 - 80 * open) * M_PI / 180, L = 0.105;
    for (int sx = -1; sx <= 1; sx += 2) {
        const V3 dir(sx * std::cos(a), std::sin(a), 0);
        const V3 P0(sx * bx, 0.04, 0), P1(sx * bx, 0.105, 0);
        const V3 T0 = P0 + dir * L, T1 = P1 + dir * L;
        const double ty = T0.y + 0.03, floorY = ty - 0.0145;
        // Tray: pressed steel with a divider.
        b.style(col * 0.95, PAINT);
        b.box(V3(T0.x, ty - 0.0175, 0), V3(0.1, 0.006, 0.465), 0.0);
        b.box(V3(T0.x + sx * 0.047, ty, 0), V3(0.006, 0.045, 0.465), 0.0);
        b.box(V3(T0.x - sx * 0.047, ty, 0), V3(0.006, 0.045, 0.465), 0.0);
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(T0.x, ty, sz * 0.2325), V3(0.1, 0.045, 0.006), 0.0);
        b.box(V3(T0.x, ty - 0.005, 0.045), V3(0.09, 0.03, 0.005), 0.0);
        // Swing arms at both ends.
        b.style(steel, METAL);
        for (int sz = -1; sz <= 1; sz += 2) {
            const double z = sz * (bz + 0.0075);
            b.beam(V3(P0.x, P0.y, z), V3(T0.x, T0.y, z), 0.006, 0.024, V3(0, 1, 0), 0.0);
            b.beam(V3(P1.x, P1.y, z), V3(T1.x, T1.y, z), 0.006, 0.024, V3(0, 1, 0), 0.0);
            b.cyl(V3(P0.x, P0.y, z - 0.004), V3(P0.x, P0.y, z + 0.004), 0.008, 6);
            b.cyl(V3(P1.x, P1.y, z - 0.004), V3(P1.x, P1.y, z + 0.004), 0.008, 6);
            b.box(V3(T0.x, ty + 0.0, sz * (bz - 0.0075 + 0.0075)), V3(0.012, 0.075, 0.006), 0.0);
        }
        // What's in them: spanners and a screwdriver in the front tray; a hammer and drivers in the back.
        if (sx > 0) {
            const double rh[] = {0.021, 0.017, 0.014}, ln[] = {0.19, 0.17, 0.15};
            b.style(kSteel, METAL);
            for (int k = 0; k < 3; k++) {
                b.push();
                b.move(T0.x - 0.03 + k * 0.03, floorY, 0.22);
                b.yaw(90);
                b.extrudeUp(spannerPoly(ln[k], rh[k], 0.0085), 0.006);
                b.pop();
            }
            b.push();
            b.move(T0.x + 0.0, floorY + 0.0105, -0.2);
            b.yaw(-90);
            screwdriver(b, 0.15, 0.011, paint(8));
            b.pop();
        } else {
            b.push();
            b.move(T0.x + 0.0, floorY + 0.0105, 0.2);
            b.yaw(90);
            hammer(b, 0.19);
            b.pop();
            for (int k = 0; k < 2; k++) {
                b.push();
                b.move(T0.x + (k ? 0.027 : -0.027), floorY + 0.0105, -0.2);
                b.yaw(-90);
                screwdriver(b, k ? 0.13 : 0.16, 0.0105, k ? paint(0) : paint(5));
                b.pop();
            }
        }
    }
}

// A rolled canvas tool roll tied with two straps, spanners and a screwdriver poking out of the end.
void toolRoll(B& b, const P& p) {
    const double wear = p("wear");
    const int count = std::max(2, std::min(5, p.i("count")));
    const V3 canvas = mix(paint(p.i("paint")), V3(0.4, 0.34, 0.25), 0.15 + wear * 0.35);
    const double r = 0.07, hl = 0.2, cy = r + 0.007;
    // The roll, a little flared at the open end.
    b.style(canvas, MATTE);
    latheAlong(b, V3(-hl, cy, 0), V3(1, 0, 0), {V2(0, 0), V2(r * 0.88, 0), V2(r * 0.99, 0.012), V2(r, 0.05), V2(r, 0.37), V2(r * 1.02, 0.388), V2(r * 0.96, 0.4), V2(0, 0.4)}, 16);
    // The free edge of the cloth spiralling the length, and the layers showing on the ends.
    b.style(canvas * 0.7, MATTE);
    b.tube({V3(-hl + 0.01, cy + r * 0.78, r * 0.64), V3(0.0, cy + r * 0.8, r * 0.62), V3(hl - 0.01, cy + r * 0.78, r * 0.64)}, 0.0042, 4);
    b.style(canvas * 0.85, MATTE);
    along(b, V3(hl + 0.003, cy, 0), V3(1, 0, 0), [&] {
        b.torus(V3(0, 0, 0), r * 0.62, 0.0045, 14, 4);
        b.torus(V3(0, 0, 0), r * 0.34, 0.0045, 12, 4);
    });
    b.cyl(V3(hl + 0.001, cy, 0), V3(hl + 0.004, cy, 0), r * 0.2, 8);
    // Straps with buckles.
    for (int k = 0; k < 2; k++) {
        const double x = -0.095 + k * 0.17;
        b.style(V3(0.32, 0.2, 0.1), MATTE);
        latheAlong(b, V3(x - 0.013, cy, 0), V3(1, 0, 0), {V2(r + 0.0004, 0), V2(r + 0.006, 0), V2(r + 0.006, 0.026), V2(r + 0.0004, 0.026)}, 16);
        b.style(kSteel * 0.85, METAL);
        b.box(V3(x, cy + r + 0.008, 0), V3(0.024, 0.005, 0.024), 0.001);
    }
    b.style(V3(0.32, 0.2, 0.1), MATTE);
    b.beam(V3(-0.095, cy + r + 0.006, 0.01), V3(-0.095, cy + r - 0.01, 0.06), 0.013, 0.003, V3(1, 0, 0), 0.0);
    // Stitched name patch on the side, and the tools poking out of the open end.
    b.style(kWhite * 0.85, MATTE);
    b.push();
    b.move(-0.02, cy + 0.02, r * 0.99);
    b.pitch(0);
    b.box(V3(0, 0, 0.0), V3(0.11, 0.045, 0.004), 0.001);
    b.pop();
    b.style(V3(0.1, 0.12, 0.1), MATTE);
    b.push();
    b.move(-0.02, cy + 0.02, r * 0.99);
    b.yaw(-90);
    b.pop();
    stamp(b, "KIT", V3(-0.02, cy + 0.02, r * 0.99 + 0.0021), 2, 0.0055, 0.0012);
    b.style(kSteel, METAL);
    const double rh[] = {0.021, 0.017, 0.015, 0.019, 0.014}, ln[] = {0.2, 0.2, 0.19, 0.22, 0.17};
    const double zs[] = {-0.036, -0.012, 0.012, 0.036, 0.0}, ys[] = {0.0, 0.0, 0.0, 0.0, 0.032};
    for (int k = 0; k < count; k++) {
        b.push();
        b.move(hl - 0.1 - ((k % 3) * 0.015), cy + ys[k] - 0.012 * (k == 4), zs[k]);
        if (k == 4) {
            screwdriver(b, 0.2, 0.0115, paint(0));
        } else {
            b.extrude(spannerPoly(ln[k], rh[k], 0.0085), 0.005);
        }
        b.pop();
    }
}

// The black plastic case: two-part moulded shell, orange end bumpers, a swing handle, two latches.
void plasticCase(B& b, const P& p) {
    const double wear = p("wear");
    const V3 acc = paint(p.i("paint")), shell = worn(kBlack * 1.5, wear * 0.4), lid = shell * 1.25;
    const double bw = 0.2, bd = 0.5, bh = 0.125, lh = 0.085;  // body depth along z
    // Body, lid, and the moulded seam between.
    b.style(shell, MATTE);
    b.box(V3(0, bh / 2 + 0.01, 0), V3(bw, bh, bd), 0.022);
    b.style(lid, MATTE);
    b.box(V3(0, bh + 0.01 + lh / 2 - 0.005, 0), V3(bw + 0.004, lh, bd + 0.004), 0.024);
    b.style(shell * 0.55, MATTE);
    b.box(V3(0, bh + 0.0075, 0), V3(bw - 0.012, 0.01, bd - 0.012), 0.004);
    // Raised ribs on the lid top.
    b.style(lid * 1.1, MATTE);
    for (int k = -3; k <= 3; k++) b.box(V3(0, bh + lh + 0.0055, k * 0.06), V3(0.15, 0.006, 0.014), 0.0015);
    // Orange rubber end bumpers wrapped over the ends.
    b.style(acc, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0, 0.105, sz * (bd / 2 + 0.005)), V3(bw + 0.006, 0.19, 0.03), 0.018);
        b.box(V3(0, 0.018, sz * (bd / 2 - 0.02)), V3(bw - 0.02, 0.016, 0.07), 0.004);
    }
    // Feet.
    b.style(kRubber, MATTE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.box(V3(sx * 0.07, 0.005, sz * 0.2), V3(0.04, 0.01, 0.06), 0.003);
    // Swing handle across the top: a gripped bar on two arms.
    b.style(acc * 0.8, MATTE);
    b.tube({V3(0, bh + lh + 0.006, -0.12), V3(0, bh + lh + 0.05, -0.115), V3(0, bh + lh + 0.062, -0.09), V3(0, bh + lh + 0.062, 0.09), V3(0, bh + lh + 0.05, 0.115), V3(0, bh + lh + 0.006, 0.12)}, 0.0145, 6);
    b.style(kBlack, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(0, bh + lh + 0.004, sz * 0.12), V3(0, bh + lh + 0.012, sz * 0.12), 0.022, 10);
    // Latches: a steel catch on the body, a moulded lever on the lid.
    for (int sz = -1; sz <= 1; sz += 2) {
        b.style(kSteel * 0.8, METAL);
        b.box(V3(bw / 2 + 0.003, bh + 0.0075, sz * 0.15), V3(0.012, 0.075, 0.04), 0.003);
        b.style(kBlack * 0.9, MATTE);
        b.box(V3(bw / 2 + 0.01, bh + 0.0075, sz * 0.15), V3(0.012, 0.03, 0.028), 0.003);
        b.style(acc, MATTE);
        b.box(V3(bw / 2 + 0.017, bh + 0.0075, sz * 0.15), V3(0.006, 0.014, 0.02), 0.002);
    }
    // Hinges at the back.
    b.style(kBlack * 0.8, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(-bw / 2 - 0.005, bh + 0.0075, sz * 0.17 - 0.03), V3(-bw / 2 - 0.005, bh + 0.0075, sz * 0.17 + 0.03), 0.012, 8);
    // Brand name on the lid front, a label plate, scuffs.
    b.style(acc, MATTE);
    stamp(b, "PRO", V3(bw / 2 + 0.0035, bh + 0.0075 + lh * 0.5 + 0.0, -0.0), 0, 0.0075, 0.002);
    b.style(kSteel * 0.7, METAL);
    b.box(V3(bw / 2 + 0.002, 0.07, 0.0), V3(0.004, 0.026, 0.07), 0.001);
    (void)wear;
}

Reg toolbox({"toolbox", "TOOLBOX", "TOOLS", "Carried toolkit: repairs on the trail; shop and loot item", 0.55,
             {
                 {"CANTILEVER", "Red steel cantilever tool chest, trays on parallel arms, carry bar (open 0..1 swings the trays out)",
                  {num("open", "TRAYS OUT", 1, 0, 1), paintParam(0), num("wear", "WEAR", 0.25, 0, 1)},
                  cantilever},
                 {"TOOL ROLL", "Rolled canvas tool roll, strapped, spanners and a driver poking out of the end",
                  {paintParam(1, "paint", "CANVAS"), num("count", "TOOLS", 4, 2, 5, 1), num("wear", "WEAR", 0.3, 0, 1)},
                  toolRoll},
                 {"PLASTIC CASE", "Black moulded plastic case with orange bumpers, swing handle and two latches",
                  {paintParam(4, "paint", "BUMPERS"), num("wear", "WEAR", 0.2, 0, 1)},
                  plasticCase},
             }});

// =============================================================================================
// 6. FUEL CELL
// =============================================================================================

// An AN fitting: a hex nut and a collar on the end of a braided line, along dir from at.
void anFitting(B& b, const V3& at, const V3& dir, const V3& col) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    const V3 d = norm(dir);
    b.style(col, METAL);
    b.cyl(at, at + d * 0.016, 0.0125, 6);
    b.style(col * 0.85, METAL);
    b.cyl(at + d * 0.016, at + d * 0.03, 0.0095, 8);
    b.style(keep, km);
}

// Stainless braided line along a path, with a fitting each end.
void braidedLine(B& b, const std::vector<V3>& path, double r, const V3& col) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    const std::vector<V3> c = curve(path, 4);
    b.style(kZinc * 0.9, METAL);
    b.tube(c, r, 6);
    anFitting(b, c.back(), c.back() - c[c.size() - 2], col);
    b.style(keep, km);
}

// The billet-aluminium racing cell: a welded box with a bolted top flange, a flip-top filler,
// a roll-over vent, anodised AN fittings with braided lines, and two mounting straps.
void alumCell(B& b, const P& p) {
    const double wear = p("wear");
    const int straps = std::max(2, std::min(3, p.i("straps")));
    const V3 al = mix(kAlu * 0.98, kAlu * 0.65, wear * 0.4), fit = paint(p.i("paint"));
    const double D = 0.34, Z = 0.62, H = 0.27, y0 = 0.045;  // x depth, z length, height, above the ground
    // Mounting rails (feet) under the cell.
    b.style(kSteel * 0.6, METAL);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, 0.0225, sz * 0.2), V3(D + 0.04, 0.045, 0.04), 0.004);
    // The box.
    b.style(al, METAL);
    b.box(V3(0, y0 + H / 2, 0), V3(D, H, Z), 0.014);
    // Top flange plate with its bolts.
    b.style(al * 0.9, METAL);
    b.box(V3(0, y0 + H + 0.004, 0), V3(D - 0.04, 0.008, Z - 0.04), 0.004);
    for (int i = 0; i < 6; i++) {
        const double z = -0.255 + i * 0.102;
        for (int sx = -1; sx <= 1; sx += 2) bolt(b, V3(sx * (D / 2 - 0.035), y0 + H + 0.008, z), V3(0, 1, 0), 0.0065, 0.004);
    }
    // Weld beads down the corners.
    b.style(al * 0.7, METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(sx * (D / 2 - 0.003), y0 + 0.012, sz * (Z / 2 - 0.003)), V3(sx * (D / 2 - 0.003), y0 + H - 0.012, sz * (Z / 2 - 0.003)), 0.0035, 4);
    // Filler: a turned neck with a flip-top cap and lever.
    b.style(al * 1.05, METAL);
    latheAlong(b, V3(0.0, y0 + H + 0.008, 0.19), V3(0, 1, 0), {V2(0.055, 0), V2(0.055, 0.012), V2(0.04, 0.02), V2(0.04, 0.062), V2(0.046, 0.066), V2(0.046, 0.074), V2(0.0, 0.074)}, 16);
    b.style(kDark, METAL);
    b.cyl(V3(0.0, y0 + H + 0.083, 0.19), V3(0.0, y0 + H + 0.087, 0.19), 0.026, 12);
    b.style(fit, METAL);
    b.box(V3(0.0, y0 + H + 0.092, 0.19), V3(0.016, 0.008, 0.08), 0.002);
    b.box(V3(0.0, y0 + H + 0.088, 0.152), V3(0.026, 0.016, 0.014), 0.002);
    // Roll-over vent valve at the other end of the lid.
    b.style(kBrass, METAL);
    latheAlong(b, V3(0.0, y0 + H + 0.008, -0.2), V3(0, 1, 0), {V2(0.022, 0), V2(0.022, 0.01), V2(0.014, 0.014), V2(0.014, 0.03), V2(0.01, 0.036), V2(0, 0.036)}, 10);
    b.style(fit, METAL);
    b.cyl(V3(0.0, y0 + H + 0.04, -0.2), V3(0.0, y0 + H + 0.052, -0.2), 0.009, 6);
    // Outlet and return bungs on the front face with braided lines and AN fittings.
    b.style(al * 0.85, METAL);
    for (int k = 0; k < 2; k++) {
        const double z = k ? -0.1 : 0.04, y = y0 + 0.07 + k * 0.03;
        b.cyl(V3(D / 2, y, z), V3(D / 2 + 0.01, y, z), 0.026, 10);
    }
    b.style(kBlack * 1.4, MATTE);
    braidedLine(b, {V3(D / 2 + 0.01, y0 + 0.07, 0.04), V3(D / 2 + 0.07, y0 + 0.07, 0.04), V3(D / 2 + 0.14, y0 + 0.055, 0.0), V3(D / 2 + 0.18, y0 + 0.02, -0.06)}, 0.0085, fit);
    braidedLine(b, {V3(D / 2 + 0.01, y0 + 0.1, -0.1), V3(D / 2 + 0.06, y0 + 0.1, -0.1), V3(D / 2 + 0.11, y0 + 0.06, -0.16), V3(D / 2 + 0.13, y0 + 0.025, -0.2)}, 0.0085, paint(0));
    // Mounting straps over the top.
    b.style(kSteel * 0.6, METAL);
    for (int k = 0; k < straps; k++) {
        const double z = straps == 2 ? (k ? -0.18 : 0.0) + (k ? 0 : -0.0) : (k - 1) * 0.2;
        const double zz = straps == 2 ? (k ? 0.22 : -0.22) : z;
        b.box(V3(0, y0 + H + 0.0, zz), V3(D + 0.006, 0.006, 0.026), 0.0);
        b.box(V3(D / 2 + 0.003, y0 + H / 2, zz), V3(0.006, H, 0.026), 0.0);
        b.box(V3(-D / 2 - 0.003, y0 + H / 2, zz), V3(0.006, H, 0.026), 0.0);
        b.style(kDark, METAL);
        b.cyl(V3(D / 2 + 0.006, y0 + 0.02, zz - 0.012), V3(D / 2 + 0.006, y0 + 0.02, zz + 0.012), 0.008, 6);
        b.style(kSteel * 0.6, METAL);
    }
    // Maker's stencil.
    b.style(kBlack * 1.2, MATTE);
    stamp(b, "FUEL", V3(D / 2 + 0.0005, y0 + 0.19, 0.0), 0, 0.0075, 0.0018);
    (void)wear;
}

// The steel transfer tank: a heavy L-shaped bed tank, filler and gauge, a 12 V pump with filter,
// a hose holstered on the tall end, lashing rings at the corners.
void lTank(B& b, const P& p) {
    const double wear = p("wear");
    const bool pump = p.i("pump") != 0;
    const V3 col = worn(paint(p.i("paint")), wear);
    const double Z = 0.9;
    // The tank itself: L profile (tall at the back).
    b.style(col, PAINT);
    b.box(V3(0.0, 0.1, 0), V3(0.6, 0.2, Z), 0.018);
    b.box(V3(-0.2, 0.28, 0), V3(0.2, 0.56, Z), 0.018);
    b.box(V3(-0.2, 0.2, 0), V3(0.2, 0.2, Z - 0.004), 0.0);
    // Pressed ribs across the large faces.
    b.style(col * 0.82, PAINT);
    for (int k = -1; k <= 1; k += 2) {
        b.box(V3(0.3 + 0.003, 0.1, k * 0.31), V3(0.006, 0.12, 0.2), 0.002);
        b.box(V3(-0.1 + 0.003, 0.4, k * 0.3), V3(0.006, 0.3, 0.2), 0.002);
    }
    b.box(V3(0.1, 0.2 + 0.003, 0.0), V3(0.36, 0.006, 0.8), 0.002);
    // Filler neck and cap on the low top, a vent beside it, a sight gauge on the tall end.
    b.style(kAlu * 0.8, METAL);
    latheAlong(b, V3(0.14, 0.203, 0.25), V3(0, 1, 0), {V2(0.06, 0), V2(0.06, 0.01), V2(0.045, 0.016), V2(0.045, 0.05), V2(0.052, 0.054), V2(0.052, 0.07), V2(0.04, 0.074), V2(0.0, 0.074)}, 14);
    b.style(paint(0), MATTE);
    b.cyl(V3(0.14, 0.275, 0.25), V3(0.14, 0.282, 0.25), 0.03, 8);
    b.style(kBrass, METAL);
    latheAlong(b, V3(0.14, 0.203, -0.3), V3(0, 1, 0), {V2(0.02, 0), V2(0.02, 0.012), V2(0.012, 0.016), V2(0.012, 0.04), V2(0, 0.045)}, 8);
    // Sight gauge: a glass tube in a steel frame on the back.
    b.style(kSteel * 0.6, METAL);
    b.box(V3(-0.303, 0.3, 0.3), V3(0.008, 0.4, 0.05), 0.002);
    b.style(V3(0.7, 0.5, 0.2), GLASS);
    b.cyl(V3(-0.309, 0.12, 0.3), V3(-0.309, 0.48, 0.3), 0.009, 6);
    // Lashing rings at the corners.
    b.style(kSteel * 0.7, METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0.3 + 0.006, 0.1, sz * (Z / 2 - 0.06)), V3(0.012, 0.05, 0.06), 0.002);
        b.push();
        b.roll(90);
        b.torus(V3(0.3 + 0.025, 0, -sz * 0), 0.0, 0.0, 3, 3, 0);
        b.pop();
        along(b, V3(0.3 + 0.022, 0.1, sz * (Z / 2 - 0.06)), V3(0, 0, 1), [&] { b.torus(V3(0, 0, 0), 0.025, 0.0045, 12, 5); });
    }
    // Transfer pump on a plate on the tall face, with a filter, plumbed to the tank and a coiled hose.
    if (pump) {
        b.style(kSteel * 0.6, METAL);
        b.box(V3(-0.1 + 0.004, 0.4, -0.25), V3(0.008, 0.14, 0.2), 0.002);
        b.style(kBlack * 1.6, PAINT);
        latheAlong(b, V3(-0.1 + 0.008, 0.4, -0.25), V3(1, 0, 0), {V2(0, 0), V2(0.04, 0), V2(0.04, 0.12), V2(0.034, 0.13), V2(0, 0.13)}, 12);
        b.style(kAlu, METAL);
        latheAlong(b, V3(-0.1 + 0.138, 0.4, -0.25), V3(1, 0, 0), {V2(0, 0), V2(0.032, 0), V2(0.032, 0.04), V2(0, 0.04)}, 10);
        b.style(paint(0) * 0.9, PAINT);
        latheAlong(b, V3(-0.1 + 0.05, 0.33, -0.31), V3(0, 1, 0), {V2(0, 0), V2(0.032, 0), V2(0.032, 0.09), V2(0.026, 0.1), V2(0, 0.1)}, 12);  // filter can
        b.style(kBrass, METAL);
        b.tube({V3(-0.1 + 0.12, 0.4, -0.25), V3(-0.1 + 0.15, 0.4, -0.26), V3(-0.1 + 0.07, 0.43, -0.31), V3(-0.1 + 0.05, 0.43, -0.31)}, 0.0065, 5);
    }
    // The hose, coiled on a cradle on the tall face, and the nozzle in its holster.
    b.style(kBlack * 1.5, MATTE);
    coil(b, V3(-0.1 + 0.006, 0.35, 0.2), V3(1, 0, 0), 0.07, 0.0085, 3, 0.019, 5);
    b.style(kBrass, METAL);
    b.cyl(V3(-0.1 + 0.07, 0.35, 0.2), V3(-0.1 + 0.11, 0.35, 0.2), 0.011, 8);
    b.style(paint(0), MATTE);
    b.box(V3(-0.1 + 0.09, 0.337, 0.2), V3(0.04, 0.008, 0.012), 0.002);
    // Maker's stencil on the long front.
    b.style(kWhite * 0.9, MATTE);
    stamp(b, "FUEL", V3(0.3 + 0.0003, 0.1, 0.0), 0, 0.0075, 0.0018);
}

// The plastic bladder cell in its steel can: a black bag filling the can, bulging over the rim,
// strapped down, with a filler neck, vent and pickup.
void bladderCell(B& b, const P& p) {
    const double wear = p("wear");
    const double fill = p("fill");
    const V3 can = worn(paint(p.i("paint")), wear), bag = kBlack * 1.5;
    const double D = 0.34, Z = 0.62, H = 0.26;
    // The can: floor, four walls, a rolled rim, pressed ribs, skids.
    b.style(can, PAINT);
    b.box(V3(0, 0.03, 0), V3(D, 0.012, Z), 0.003);
    b.box(V3(D / 2 - 0.006, H / 2 + 0.03, 0), V3(0.012, H, Z), 0.0);
    b.box(V3(-D / 2 + 0.006, H / 2 + 0.03, 0), V3(0.012, H, Z), 0.0);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0, H / 2 + 0.03, sz * (Z / 2 - 0.006)), V3(D, H, 0.012), 0.0);
    b.style(can * 0.85, PAINT);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0, 0.012, sz * 0.2), V3(D + 0.03, 0.024, 0.045), 0.003);
        b.box(V3(D / 2 + 0.003, 0.03 + H / 2, sz * 0.23), V3(0.006, H - 0.06, 0.04), 0.002);
    }
    b.style(can * 1.15, PAINT);
    b.tube({V3(D / 2, H + 0.03, Z / 2), V3(D / 2, H + 0.03, -Z / 2), V3(-D / 2, H + 0.03, -Z / 2), V3(-D / 2, H + 0.03, Z / 2)}, 0.009, 6, true);
    // The bladder: a rounded bag, with moulded baffle seams.
    const double top = 0.03 + H + 0.03 * fill + 0.015;
    b.style(bag, MATTE);
    b.box(V3(0, (top + 0.04) / 2, 0), V3(D - 0.03, top - 0.04, Z - 0.03), 0.07);
    b.style(bag * 1.35, MATTE);
    for (int k = -2; k <= 2; k++) b.box(V3(0, top - 0.002, k * 0.1), V3(D - 0.12, 0.005, 0.012), 0.002);
    // Filler neck with its screw cap, the vent, and the pickup bulkhead.
    b.style(kAlu * 0.85, METAL);
    latheAlong(b, V3(0.04, top - 0.005, 0.18), V3(0, 1, 0), {V2(0.055, 0), V2(0.055, 0.008), V2(0.04, 0.014), V2(0.04, 0.04), V2(0.048, 0.044), V2(0.048, 0.056), V2(0.0, 0.058)}, 14);
    b.style(paint(0), MATTE);
    for (int k = 0; k < 6; k++) b.box(V3(0.04, top + 0.052, 0.18), V3(0.012, 0.008, 0.092), 0.0), (void)k;
    b.style(kBrass, METAL);
    latheAlong(b, V3(0.0, top - 0.004, -0.19), V3(0, 1, 0), {V2(0.022, 0), V2(0.022, 0.01), V2(0.012, 0.016), V2(0.012, 0.034), V2(0, 0.038)}, 8);
    b.style(kAlu * 0.8, METAL);
    b.cyl(V3(D / 2, 0.1, 0.1), V3(D / 2 + 0.014, 0.1, 0.1), 0.028, 10);
    braidedLine(b, {V3(D / 2 + 0.014, 0.1, 0.1), V3(D / 2 + 0.07, 0.1, 0.1), V3(D / 2 + 0.12, 0.07, 0.15), V3(D / 2 + 0.14, 0.03, 0.2)}, 0.0085, paint(5));
    // Black webbing straps over the bag, hooked to lugs on the can.
    for (int k = 0; k < 2; k++) {
        const double z = k ? -0.1 : -0.06;
        if (k == 1) continue;
        (void)z;
    }
    b.style(kBlack * 1.1, MATTE);
    for (int k = -1; k <= 1; k += 2) {
        const double z = k * 0.12;
        b.box(V3(0, top + 0.001, z), V3(D + 0.0, 0.006, 0.04), 0.0);
        b.box(V3(D / 2 + 0.0, 0.03 + H / 2 + 0.03, z), V3(0.006, H + 0.0, 0.04), 0.0);
        b.box(V3(-D / 2 - 0.0, 0.03 + H / 2 + 0.03, z), V3(0.006, H + 0.0, 0.04), 0.0);
        b.style(kSteel * 0.8, METAL);
        b.box(V3(D / 2 + 0.006, 0.12, z), V3(0.012, 0.03, 0.044), 0.002);
        b.style(kBlack * 1.1, MATTE);
    }
    // Stencil on the can's long face.
    b.style(kWhite * 0.9, MATTE);
    stamp(b, "FUEL", V3(D / 2 + 0.0004, 0.2, 0.0), 0, 0.0075, 0.0018);
    (void)wear;
}

Reg fuelCell({"fuel_cell", "FUEL CELL", "TOOLS", "Auxiliary tank: extra range, bought or found; filled with jerry cans", 0.9,
              {
                  {"ALUMINIUM CELL", "Welded alloy racing cell: flip-top filler, roll-over vent, braided AN lines, two straps",
                   {paintParam(5, "paint", "FITTINGS"), num("straps", "STRAPS", 2, 2, 3, 1), num("wear", "WEAR", 0.2, 0, 1)},
                   alumCell},
                  {"BED TANK", "L-shaped steel transfer tank for a pickup bed, with pump, filter and holstered hose",
                   {paintParam(0), choice("pump", "PUMP", 1, {"NO PUMP", "PUMP"}), num("wear", "WEAR", 0.3, 0, 1)},
                   lTank},
                  {"BLADDER IN CAN", "Black plastic bladder cell in a pressed steel can, strapped down, with filler and pickup",
                   {paintParam(1, "paint", "CAN"), num("fill", "BLADDER FULL", 1, 0, 1), num("wear", "WEAR", 0.3, 0, 1)},
                   bladderCell},
              }});

// =============================================================================================
// 7. LIGHT BAR
// =============================================================================================

const V3 kLedWhite(0.95, 0.97, 1.0);

V3 coverTint(int c) { return c ? V3(0.95, 0.6, 0.12) : V3(0.72, 0.8, 0.86); }

// An LED pod in a chrome reflector cup, facing +x in the current frame: the emissive chip at the
// back of the cup, ready to sit behind a glass lens. (a = back x, f = front x)
void ledPod(B& b, double a, double f, double y, double z, double r) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kSteel, CHROME);
    b.cone(V3(a + 0.003, y, z), V3(f, y, z), r * 0.4, r, 8, false);
    b.style(kLedWhite, EMISSIVE);
    b.cyl(V3(a, y, z), V3(a + 0.004, y, z), r * 0.4, 6);
    b.style(keep, km);
}

// A rigid mounting foot: base plate with two bolts, a post up to the bar.
void barFoot(B& b, double x, double z, double h) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kBlack * 1.6, PAINT);
    b.box(V3(x, 0.004, z), V3(0.06, 0.008, 0.07), 0.002);
    b.box(V3(x, 0.008 + (h - 0.008) / 2, z), V3(0.03, h - 0.008, 0.016), 0.003);
    b.style(kSteel * 0.8, METAL);
    for (int s = -1; s <= 1; s += 2) bolt(b, V3(x, 0.008, z + s * 0.024), V3(0, 1, 0), 0.0065, 0.005);
    b.style(keep, km);
}

// The straight LED bar: a black extruded housing with cooling fins, a clear lens strip, a row
// of LED pods behind it, mounting feet and a lead out of one end.
void ledBar(B& b, const P& p) {
    const double L = p("length");
    const int n = std::max(3, std::min(20, p.i("lamps")));
    const double wear = p("wear");
    const double hb = 0.064, db = 0.075, y0 = 0.05, yc = y0 + hb / 2;
    const V3 body = worn(kBlack * 1.4, wear * 0.4);
    // Housing: back plate, top and bottom, end caps.
    b.style(body, PAINT);
    b.box(V3(-db / 2 + 0.004, yc, 0), V3(0.008, hb, L), 0.002);
    b.box(V3(0.0, y0 + hb - 0.004, 0), V3(db, 0.008, L), 0.002);
    b.box(V3(0.0, y0 + 0.004, 0), V3(db, 0.008, L), 0.002);
    for (int sz = -1; sz <= 1; sz += 2) b.box(V3(0.0, yc, sz * (L / 2 - 0.006)), V3(db, hb, 0.012), 0.003);
    // Cooling fins up the back and top.
    b.style(body * 0.85, PAINT);
    const int nf = std::max(6, (int)(L / 0.038));
    for (int i = 0; i < nf; i++) {
        const double z = -L / 2 + 0.03 + (L - 0.06) * (i + 0.5) / nf;
        b.box(V3(-db / 2 - 0.008, yc, z), V3(0.016, hb - 0.014, 0.007), 0.0);
    }
    // Dark interior behind the pods.
    b.style(kBlack, MATTE);
    b.box(V3(-db / 2 + 0.01, yc, 0), V3(0.004, hb - 0.018, L - 0.03), 0.0);
    // The pods, evenly spaced; the LED chips glow.
    const double pitch = (L - 0.05) / n, pr = std::min(0.024, pitch * 0.44);
    for (int i = 0; i < n; i++) ledPod(b, -db / 2 + 0.013, db / 2 - 0.012, yc, -L / 2 + 0.025 + pitch * (i + 0.5), pr);
    // The lens: glass over the whole row, with a thin gasket frame.
    b.style(coverTint(p.i("cover")), GLASS);
    b.box(V3(db / 2 - 0.0035, yc, 0), V3(0.005, hb - 0.016, L - 0.026), 0.0);
    b.style(kBlack * 0.8, MATTE);
    b.box(V3(db / 2 - 0.0015, y0 + 0.0075, 0), V3(0.003, 0.003, L - 0.02), 0.0);
    b.style(kLedWhite, EMISSIVE);  // the daytime-running strip along the top edge
    b.box(V3(db / 2 - 0.0013, y0 + hb - 0.0062, 0), V3(0.0028, 0.0028, L - 0.03), 0.0);
    // Mounting feet and the lead.
    barFoot(b, 0.0, -L * 0.32, y0);
    barFoot(b, 0.0, L * 0.32, y0);
    b.style(kBlack, MATTE);
    b.tube({V3(0, yc, L / 2), V3(0, yc, L / 2 + 0.03), V3(0, yc - 0.015, L / 2 + 0.06), V3(0, 0.03, L / 2 + 0.08), V3(0.0, 0.01, L / 2 + 0.12)}, 0.0055, 5);
    // Name on the back of the housing.
    b.style(kSteel * 0.7, METAL);
    stamp(b, "LUX", V3(-db / 2 - 0.0165, yc, 0.0), 1, 0.005, 0.0012);
    (void)wear;
}

// One round driving lamp facing +x with its centre at the local origin: chrome bezel, black
// bowl, emissive reflector under a glass lens, a rear nut.
void roundLamp(B& b, double R, int cover) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    const double depth = R * 0.95;
    // Bowl.
    b.style(kBlack * 1.4, PAINT);
    latheAlong(b, V3(-depth, 0, 0), V3(1, 0, 0), {V2(0, 0), V2(R * 0.42, 0), V2(R * 0.8, depth * 0.3), V2(R * 0.97, depth * 0.7), V2(R * 0.97, depth * 0.98), V2(R * 0.9, depth)}, 12);
    b.cyl(V3(-depth - 0.02, 0, 0), V3(-depth, 0, 0), R * 0.2, 6);  // rear nut
    // Reflector: a bright dish with the bulb.
    b.style(kSteel, CHROME);
    latheAlong(b, V3(-depth * 0.78, 0, 0), V3(1, 0, 0), {V2(0, 0), V2(R * 0.5, depth * 0.2), V2(R * 0.82, depth * 0.62), V2(R * 0.86, depth * 0.76)}, 12);
    b.style(V3(1.0, 0.95, 0.8), EMISSIVE);
    latheAlong(b, V3(-depth * 0.76, 0, 0), V3(1, 0, 0), {V2(0, 0), V2(R * 0.22, depth * 0.06), V2(R * 0.3, depth * 0.2), V2(0, depth * 0.28)}, 10);
    // Glass lens and chrome bezel.
    b.style(coverTint(cover), GLASS);
    latheAlong(b, V3(-0.012, 0, 0), V3(1, 0, 0), {V2(0, 0.011), V2(R * 0.5, 0.0085), V2(R * 0.88, 0.0)}, 10);
    b.style(kSteel, CHROME);
    latheAlong(b, V3(-0.018, 0, 0), V3(1, 0, 0), {V2(R * 0.86, 0), V2(R * 1.0, 0), V2(R * 1.02, 0.01), V2(R * 0.97, 0.02), V2(R * 0.88, 0.016)}, 12);
    b.style(keep, km);
}

// A bar of round driving lamps: a chrome tube on two uprights, each lamp on its own steel
// yoke with a chrome bezel; the lamps are the heart of the model.
void drivingLamps(B& b, const P& p) {
    const double L = p("length");
    const int n = std::max(2, std::min(4, p.i("lamps")));
    const double wear = p("wear");
    const double R = 0.088, barY = 0.15, br = 0.0165;
    // Uprights and the bar.
    b.style(kSteel * 0.55, METAL);
    for (int sz = -1; sz <= 1; sz += 2) {
        b.box(V3(0, 0.004, sz * (L / 2 - 0.05)), V3(0.07, 0.008, 0.09), 0.002);
        b.cyl(V3(0, 0.008, sz * (L / 2 - 0.05)), V3(0, barY, sz * (L / 2 - 0.05)), 0.0125, 8);
        for (int s = -1; s <= 1; s += 2) bolt(b, V3(0, 0.008, sz * (L / 2 - 0.05) + s * 0.032), V3(0, 1, 0), 0.0065, 0.005);
    }
    b.style(mix(kSteel, kRust, wear * 0.3), CHROME);
    b.cyl(V3(0, barY, -L / 2), V3(0, barY, L / 2), br, 12);
    b.style(kBlack * 1.2, MATTE);
    for (int sz = -1; sz <= 1; sz += 2) b.cyl(V3(0, barY, sz * (L / 2)), V3(0, barY, sz * (L / 2 + 0.006)), br * 1.02, 12);
    // The lamps, each on a U yoke.
    const double pitch = n > 1 ? std::min(0.27, (L - 0.3) / (n - 1)) : 0;
    for (int i = 0; i < n; i++) {
        const double z = (i - (n - 1) / 2.0) * pitch, yc = barY + br + 0.012 + R;
        b.style(kSteel * 0.55, METAL);
        b.box(V3(-0.03, barY + br + 0.005, z), V3(0.05, 0.008, 2 * (R + 0.012) + 0.008), 0.002);
        for (int s = -1; s <= 1; s += 2) {
            b.box(V3(-0.03, (barY + br + 0.009 + yc + 0.01) / 2, z + s * (R + 0.012)), V3(0.036, yc + 0.01 - (barY + br + 0.009), 0.008), 0.0);
            b.cyl(V3(-0.03, yc, z + s * (R + 0.006)), V3(-0.03, yc, z + s * (R + 0.02)), 0.011, 8);
        }
        b.push();
        b.move(0.0, yc, z);
        roundLamp(b, R, p.i("cover"));
        b.pop();
        b.style(kLedWhite, EMISSIVE);  // little position light on top of each bezel
        b.box(V3(-0.002, yc + R * 1.0 + 0.0015, z), V3(0.012, 0.004, 0.026), 0.0);
    }
    // Wiring loom under the bar.
    b.style(kBlack, MATTE);
    b.tube({V3(-0.02, barY - 0.01, -L / 2 + 0.05), V3(-0.03, barY - 0.014, 0), V3(-0.02, barY - 0.01, L / 2 - 0.05), V3(-0.01, 0.06, L / 2 - 0.04), V3(0.02, 0.01, L / 2 + 0.02)}, 0.0045, 5);
}

// The curved roof bar: housing segments following the arc of a roof, one LED pod each, with the
// ends swept back round the corners of the cab.
void curvedBar(B& b, const P& p) {
    const double L = p("length");
    const int n = std::max(4, std::min(20, p.i("lamps")));
    const double wear = p("wear");
    const double hb = 0.058, db = 0.07, y0 = 0.055, yc = y0 + hb / 2;
    const double R = 1.5, th = std::asin(std::min(0.95, (L / 2) / R)), step = 2 * th / n;
    const V3 body = worn(kBlack * 1.4, wear * 0.4);
    const double bow = R * (1 - std::cos(th));  // the ends sit this far back from the middle
    b.move(bow * 0.5, 0, 0);  // centre the footprint on the origin
    // Segments around the arc.
    for (int i = 0; i < n; i++) {
        const double t = -th + step * (i + 0.5);
        const double x = R * std::cos(t) - R * std::cos(th) * 0.0 - R + bow * 0.0, z = R * std::sin(t);
        const double ds = R * step;
        b.push();
        b.move(x + bow * 0.0, 0, z);
        b.yaw(-t * 180 / M_PI);
        b.style(body, PAINT);
        b.box(V3(-db / 2 + 0.004, yc, 0), V3(0.008, hb, ds * 1.02), 0.0);
        b.box(V3(0, y0 + hb - 0.004, 0), V3(db, 0.008, ds * 1.02), 0.0);
        b.box(V3(0, y0 + 0.004, 0), V3(db, 0.008, ds * 1.02), 0.0);
        if (n <= 14) {
            b.style(body * 0.85, PAINT);
            b.box(V3(-db / 2 - 0.007, yc, 0), V3(0.014, hb - 0.014, ds * 0.4), 0.0);
        }
        ledPod(b, -db / 2 + 0.013, db / 2 - 0.012, yc, 0, std::min(0.024, ds * 0.42));
        b.style(coverTint(p.i("cover")), GLASS);
        b.box(V3(db / 2 - 0.0035, yc, 0), V3(0.005, hb - 0.014, ds * 0.99), 0.0);
        b.style(kLedWhite, EMISSIVE);
        b.box(V3(db / 2 - 0.0013, y0 + hb - 0.0058, 0), V3(0.0028, 0.0028, ds * 0.99), 0.0);
        b.pop();
    }
    // End caps.
    for (int sz = -1; sz <= 1; sz += 2) {
        const double t = sz * th;
        const double x = R * std::cos(t) - R, z = R * std::sin(t);
        b.push();
        b.move(x, 0, z);
        b.yaw(-t * 180 / M_PI);
        b.style(body * 1.2, PAINT);
        b.box(V3(0, yc, sz * 0.0), V3(db + 0.006, hb + 0.004, 0.014), 0.004);
        b.pop();
    }
    // Mount feet under the arc.
    for (int sz = -1; sz <= 1; sz += 2) {
        const double t = sz * th * 0.6;
        const double x = R * std::cos(t) - R, z = R * std::sin(t);
        b.push();
        b.move(x, 0, z);
        b.yaw(-t * 180 / M_PI);
        barFoot(b, 0.0, 0.0, y0);
        b.pop();
    }
    // The lead, a nameplate.
    b.style(kBlack, MATTE);
    {
        const double t = th;
        const double x = R * std::cos(t) - R, z = R * std::sin(t);
        b.tube({V3(x, yc, z), V3(x - 0.01, yc, z + 0.03), V3(x - 0.03, yc - 0.01, z + 0.06), V3(x - 0.05, 0.02, z + 0.09)}, 0.0055, 5);
    }
    b.style(kSteel * 0.7, METAL);
    {
        b.push();
        b.move(R * std::cos(0.0) - R, 0, 0);
        stamp(b, "ARC", V3(-db / 2 - 0.0145, yc, 0), 1, 0.0055, 0.0012);
        b.pop();
    }
    (void)wear;
}

Reg lightBar({"light_bar", "LIGHT BAR", "TOOLS", "Off-road lighting for night driving: bolts on the roof or bull bar; lights glow", 1.0,
              {
                  {"LED BAR", "Straight black LED bar, finned housing, a row of LED pods behind a clear lens",
                   {num("length", "LENGTH", 1.0, 0.5, 1.3, 0.01), num("lamps", "LAMPS", 10, 4, 20, 1), choice("cover", "COVER", 0, {"CLEAR", "AMBER"}), num("wear", "WEAR", 0.2, 0, 1)},
                   ledBar},
                  {"DRIVING LAMPS", "Chrome round driving lamps on a tube bar, each on a steel yoke",
                   {num("length", "LENGTH", 0.78, 0.5, 1.1, 0.01), num("lamps", "LAMPS", 2, 2, 4, 1), choice("cover", "COVER", 0, {"CLEAR", "AMBER"}), num("wear", "WEAR", 0.2, 0, 1)},
                   drivingLamps},
                  {"CURVED ROOF BAR", "Arched roof bar following the curve of a cab roof, LED pods in segments",
                   {num("length", "LENGTH", 1.0, 0.6, 1.3, 0.01), num("lamps", "LAMPS", 12, 4, 20, 1), choice("cover", "COVER", 0, {"CLEAR", "AMBER"}), num("wear", "WEAR", 0.2, 0, 1)},
                   curvedBar},
              }});

}  // namespace
