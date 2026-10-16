// Trail and road: trail signs, map boards, markers, cairns, race gates, cones, drums,
// barriers, guardrails, lamp posts and sign gantries.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "../modelkit.h"

using namespace mk;

namespace {

using Poly = std::vector<V2>;

V3 operator*(const V3& a, const V3& b) { return V3(a.x * b.x, a.y * b.y, a.z * b.z); }

// ---- Shared colours and helpers --------------------------------------------------------

const V3 kWoodNew(0.50, 0.37, 0.22), kWoodMid(0.41, 0.30, 0.19), kWoodOld(0.33, 0.26, 0.19), kWoodDark(0.24, 0.17, 0.11);
const V3 kGalv(0.62, 0.64, 0.65), kSteelDk(0.26, 0.28, 0.30), kRust(0.46, 0.22, 0.09);
const V3 kPaper(0.82, 0.78, 0.62), kWhitePlate(0.90, 0.89, 0.84);
const V3 kConcrete(0.60, 0.59, 0.56), kSignGreen(0.05, 0.33, 0.17);

// Weathering: darker and a touch greyer.
V3 aged(const V3& c, double w) { return mix(c, c * 0.68 + V3(0.04, 0.036, 0.025), std::clamp(w, 0.0, 1.0)); }

Poly shifted(Poly q, double du, double dv) {
    for (V2& p : q) p.x += du, p.y += dv;
    return q;
}

// A flat shape standing in the front plane. The polygon is (right, up) as seen from +X (right is
// -Z); thickness t along X, centred on x0.
void plate(B& b, const Poly& poly, double x0, double t) {
    b.push();
    b.yaw(90);
    b.extrude(poly, t, x0);
    b.pop();
}

// A flat shape lying across the line of travel: polygon (z-ish, up) the same way, but extruded
// along X over [xa, xb]. (Same as plate, for long barriers running along X.)
void profileAlongX(B& b, const Poly& poly, double xa, double xb) { plate(b, poly, (xa + xb) / 2, xb - xa); }

// The ski-style difficulty mark, standing on the plane x = x (face toward +X), centred (y, z).
// s is its half-height.
void skiSymbol(B& b, int rating, double x, double y, double z, double s, double t = 0.006) {
    static const V3 col[4] = {V3(0.07, 0.48, 0.20), V3(0.10, 0.28, 0.66), V3(0.05, 0.05, 0.06), V3(0.74, 0.09, 0.07)};
    rating = std::clamp(rating, 0, 3);
    b.style(col[rating], MATTE);
    const double u = -z;
    switch (rating) {
    case 0: b.cyl(V3(x, y, z), V3(x + t, y, z), s, 12); break;
    case 1: b.box(V3(x + t / 2, y, z), V3(t, 1.7 * s, 1.7 * s)); break;
    case 2: {
        const double d = 1.25 * s;
        plate(b, {V2(u, y - d), V2(u + d, y), V2(u, y + d), V2(u - d, y)}, x + t / 2, t);
        break;
    }
    default: {
        const double d = 0.98 * s;
        for (int k = -1; k <= 1; k += 2)
            plate(b, {V2(u + k * d * 1.05, y - d), V2(u + k * d * 1.05 + d, y), V2(u + k * d * 1.05, y + d), V2(u + k * d * 1.05 - d, y)}, x + t / 2, t);
        break;
    }
    }
}

// ---- 1. Trail sign ---------------------------------------------------------------------

const std::vector<std::string> kRatingNames = {"GREEN CIRCLE", "BLUE SQUARE", "BLACK DIAMOND", "RED DOUBLE DIAMOND"};

void stones3(B& b, double r, uint32_t seed, double y = 0) {
    for (int k = 0; k < 3; k++) {
        const double a = k * 2.1 + hash01(seed, k) * 1.5, d = r * (1.0 + 0.4 * hash01(seed, 5 + k));
        const double s = 0.05 + 0.04 * hash01(seed, 9 + k);
        b.color(V3(0.44, 0.42, 0.38) * (0.85 + 0.3 * hash01(seed, 13 + k))).mat(ROCK);
        b.blob(V3(std::cos(a) * d, y + s * 0.3, std::sin(a) * d), V3(s * 1.4, s, s * 1.2), 0.12, seed + k, 0, 0.7, y);
    }
}

// A timber post with a routed backing board and a white plate.
void signTimber(B& b, const P& p) {
    const double w = p("wear");
    b.style(aged(kWoodMid, w), WOOD);
    b.box(V3(0, 0.85, 0), V3(0.14, 1.70, 0.14), 0.015);
    b.box(V3(0, 1.72, 0), V3(0.19, 0.04, 0.19), 0.012);  // weathered cap
    b.style(aged(kWoodNew, w), WOOD);
    b.box(V3(0.092, 1.40, 0), V3(0.045, 0.62, 0.50), 0.012);  // backing board
    b.style(aged(kWoodDark, w * 0.5), WOOD);
    b.box(V3(0.1165, 1.42, 0), V3(0.004, 0.47, 0.41), 0.0);  // the routed recess
    b.style(aged(kWhitePlate, w), MATTE);
    b.box(V3(0.1225, 1.42, 0), V3(0.008, 0.43, 0.37), 0.003);  // the plate
    skiSymbol(b, p.i("rating"), 0.1265, 1.42, 0, 0.13);
    stones3(b, 0.12, 71);
}

// A square-cornered metal sign on a steel U-channel post, with a little trail plaque under it.
void signMetal(B& b, const P& p) {
    const double w = p("wear");
    b.style(aged(kGalv, w), METAL);
    // The U-channel: web toward the sign, flanges back.
    b.extrudeUp({V2(0.0, 0.025), V2(-0.04, 0.025), V2(-0.04, 0.02), V2(-0.005, 0.02), V2(-0.005, -0.02), V2(-0.04, -0.02), V2(-0.04, -0.025), V2(0.0, -0.025)}, 2.05);
    // Clamp brackets.
    for (double y : {1.30, 1.84}) b.box(V3(0.006, y, 0), V3(0.014, 0.03, 0.07), 0.003);
    // The sign: dark rim, white face.
    b.style(V3(0.08, 0.08, 0.09), PAINT);
    plate(b, shifted(roundedRect(0.52, 0.52, 0.05, 2), 0, 1.84), 0.012, 0.006);
    b.style(aged(V3(0.93, 0.93, 0.90), w), PAINT);
    plate(b, shifted(roundedRect(0.47, 0.47, 0.035, 2), 0, 1.84), 0.0165, 0.004);
    skiSymbol(b, p.i("rating"), 0.0185, 1.84, 0, 0.14);
    if (p.i("plaque")) {
        b.style(V3(0.30, 0.18, 0.10), PAINT);
        b.box(V3(0.012, 1.45, 0), V3(0.006, 0.15, 0.52), 0.004);
        b.style(V3(0.93, 0.93, 0.88), MATTE);
        plate(b, shifted({V2(-0.18, -0.02), V2(0.06, -0.02), V2(0.06, -0.065), V2(0.19, 0.0), V2(0.06, 0.065), V2(0.06, 0.02), V2(-0.18, 0.02)}, 0, 1.45), 0.0165, 0.004);
    }
    // Rust spots down the post.
    b.style(kRust * (0.7 + 0.5 * w), MATTE);
    b.box(V3(-0.041, 0.12, 0.012), V3(0.003, 0.05, 0.016));
    b.box(V3(-0.041, 0.52, -0.012), V3(0.003, 0.03, 0.018));
}

// A carved wooden arrow on a turned post, the symbol on a pale roundel.
void signArrow(B& b, const P& p) {
    const double w = p("wear");
    const double dir = p.i("dir") ? 1 : -1;  // 1: points to the right
    b.style(aged(kWoodMid, w), WOOD);
    b.lathe(V3(0, 0, 0), {V2(0.085, 0), V2(0.078, 0.06), V2(0.066, 1.35), V2(0.074, 1.40), V2(0.052, 1.48), V2(0, 1.52)}, 9);
    b.style(aged(kWoodNew, w), WOOD);
    Poly arrow = {V2(-0.36, -0.17), V2(0.24, -0.17), V2(0.43, 0), V2(0.24, 0.17), V2(-0.36, 0.17)};
    for (V2& q : arrow) q.x *= dir, q.y += 1.22;
    plate(b, arrow, 0.105, 0.045);
    // The carved rim: a darker groove line round the arrow.
    b.style(aged(kWoodDark, w), WOOD);
    const double u0 = -0.11 * dir;
    b.cyl(V3(0.1285, 1.22, -u0), V3(0.1325, 1.22, -u0), 0.138, 14);  // the groove under the roundel
    b.style(aged(V3(0.88, 0.84, 0.70), w), MATTE);
    b.cyl(V3(0.1325, 1.22, -u0), V3(0.1365, 1.22, -u0), 0.125, 14);
    skiSymbol(b, p.i("rating"), 0.1365, 1.22, -u0, 0.08, 0.005);
    b.style(V3(0.2, 0.2, 0.22), METAL);
    b.box(V3(0.085, 1.22, 0), V3(0.05, 0.02, 0.2), 0.004);  // iron strap
    stones3(b, 0.13, 33);
}

Reg trailSign({"trail_sign", "TRAIL SIGN", "TRAIL AND ROAD", "Trail difficulty sign on a post: green circle, blue square, black diamond, red double diamond", 2.2,
               {
                   {"ROUTED TIMBER", "Squared timber post, routed board, white plate", {choice("rating", "RATING", 1, kRatingNames), num("wear", "WEAR", 0.3, 0, 1)}, signTimber},
                   {"STEEL U-POST", "Metal sign on a steel U-channel, trail plaque below",
                    {choice("rating", "RATING", 1, kRatingNames), num("wear", "WEAR", 0.25, 0, 1), choice("plaque", "PLAQUE", 1, {"NO", "YES"})}, signMetal},
                   {"CARVED ARROW", "Carved wooden arrow with the symbol on a roundel",
                    {choice("rating", "RATING", 1, kRatingNames), num("wear", "WEAR", 0.3, 0, 1), choice("dir", "ARROW", 1, {"LEFT", "RIGHT"})}, signArrow},
               }});

// ---- 2. Map board ----------------------------------------------------------------------

// The map, drawn in flat layers on paper whose front face is at x = xf, centred (zc, yc), w by h.
void mapArt(B& b, double xf, double zc, double yc, double w, double h) {
    auto at = [&](double nu, double nv, int k) { return V3(xf + 0.0042 + 0.006 * k, yc + nv * h, zc - nu * w); };
    auto layerX = [&](int k) { return xf + 0.0042 + 0.006 * k; };
    auto shape = [&](std::initializer_list<V2> pts, int k) {
        Poly q;
        for (const V2& s : pts) q.push_back(V2(s.x * w - zc, s.y * h + yc));
        plate(b, q, layerX(k), 0.004);
    };
    auto line = [&](std::initializer_list<V2> pts, double wd, int k) {
        const std::vector<V2> v(pts);
        for (size_t i = 0; i + 1 < v.size(); i++) b.beam(at(v[i].x, v[i].y, k), at(v[i + 1].x, v[i + 1].y, k), wd, 0.004, V3(1, 0, 0));
    };
    b.style(kPaper, MATTE);
    b.box(V3(xf - 0.003, yc, zc), V3(0.006, h, w));
    // Land, forest, the rock, the lake, the river.
    b.color(0.50, 0.60, 0.34);
    shape({V2(-0.48, -0.46), V2(0.48, -0.47), V2(0.49, 0.22), V2(0.40, 0.47), V2(-0.10, 0.48), V2(-0.47, 0.40), V2(-0.49, 0.0)}, 0);
    b.color(0.33, 0.47, 0.24);
    shape({V2(-0.45, -0.40), V2(-0.12, -0.38), V2(0.03, -0.20), V2(-0.10, 0.0), V2(-0.40, -0.05)}, 1);
    shape({V2(0.20, 0.10), V2(0.45, 0.05), V2(0.46, 0.40), V2(0.28, 0.42), V2(0.15, 0.28)}, 1);
    b.color(0.60, 0.54, 0.44);
    shape({V2(-0.40, 0.18), V2(-0.30, 0.42), V2(-0.21, 0.26), V2(-0.12, 0.40), V2(-0.02, 0.15)}, 1);
    b.color(0.92, 0.92, 0.9);
    shape({V2(-0.33, 0.35), V2(-0.30, 0.42), V2(-0.27, 0.35)}, 2);
    shape({V2(-0.15, 0.34), V2(-0.12, 0.40), V2(-0.09, 0.34)}, 2);
    b.color(0.26, 0.48, 0.74);
    Poly lk;
    for (int i = 0; i < 12; i++) lk.push_back(V2((0.16 + 0.19 * std::cos(i * M_PI / 6)) * w - zc, (-0.20 + 0.10 * std::sin(i * M_PI / 6)) * h + yc));
    plate(b, lk, layerX(1), 0.004);
    line({V2(0.16, -0.29), V2(0.12, -0.38), V2(0.2, -0.48)}, 0.018 * w / 1.4, 1);
    // The trails: brown, with a branch round the lake.
    b.color(0.44, 0.25, 0.10);
    const double tw = 0.014 * w / 1.4;
    line({V2(-0.22, -0.40), V2(-0.20, -0.25), V2(-0.30, -0.10), V2(-0.20, 0.05), V2(0.05, 0.12), V2(0.20, 0.22), V2(0.30, 0.38)}, tw, 3);
    line({V2(-0.20, 0.05), V2(-0.36, 0.14), V2(-0.40, 0.30)}, tw, 3);
    line({V2(-0.20, -0.25), V2(0.0, -0.10), V2(0.14, -0.02), V2(0.30, -0.08), V2(0.38, -0.2)}, tw, 3);
    // You are here.
    const double rr = 0.034 * h / 0.85;
    b.color(0.95, 0.95, 0.92);
    b.cyl(at(-0.22, -0.40, 4), at(-0.22, -0.40, 4) + V3(0.004, 0, 0), rr * 1.35, 14);
    b.color(0.80, 0.08, 0.06);
    b.cyl(at(-0.22, -0.40, 5), at(-0.22, -0.40, 5) + V3(0.004, 0, 0), rr, 14);
}

const V3 kLetter(0.92, 0.88, 0.74);

// Timber kiosk: two posts, a title plank, a framed map under a gable roof, a leaflet box.
void boardKiosk(B& b, const P& p) {
    const double w = p("wear");
    const V3 wood = aged(kWoodMid, w), wood2 = aged(kWoodNew, w), dark = aged(kWoodDark, w);
    const V3 roof = p.i("roof") ? V3(0.22, 0.34, 0.26) : aged(V3(0.36, 0.26, 0.17), w);
    b.style(wood, WOOD);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, 0.975, s * 0.88), V3(0.15, 1.95, 0.15), 0.012);
    b.box(V3(0, 1.96, 0), V3(0.16, 0.12, 2.0), 0.01);  // top beam
    b.box(V3(0.0, 0.58, 0), V3(0.08, 0.1, 1.62), 0.008);  // lower rail
    // The panel.
    b.style(dark, WOOD);
    b.box(V3(0.0, 1.17, 0), V3(0.05, 0.96, 1.56), 0.006);
    b.style(wood2, WOOD);
    b.box(V3(0.040, 1.17 + 0.465, 0), V3(0.03, 0.06, 1.56), 0.006);
    b.box(V3(0.040, 1.17 - 0.465, 0), V3(0.03, 0.06, 1.56), 0.006);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0.040, 1.17, s * 0.75), V3(0.03, 0.96, 0.06), 0.006);
    mapArt(b, 0.03, 0, 1.17, 1.44, 0.84);
    // Title plank.
    b.style(dark, WOOD);
    b.box(V3(0.02, 1.80, 0), V3(0.05, 0.26, 1.64), 0.008);
    b.style(kLetter, MATTE);
    b.letters("TRAIL MAP", V3(0.045, 1.80, 0), 0.022, 0.012);
    // Leaflet box under the panel.
    b.style(wood2, WOOD);
    b.box(V3(0.09, 0.80 - 0.40, -0.45), V3(0.12, 0.2, 0.36), 0.008);
    b.style(dark, WOOD);
    b.beam(V3(0.03, 0.57, -0.45), V3(0.17, 0.50, -0.45), 0.4, 0.02, V3(0, 1, 0), 0.004);
    b.style(V3(0.85, 0.82, 0.70), MATTE);
    b.box(V3(0.152, 0.44, -0.45), V3(0.006, 0.12, 0.3));
    // The gable roof: ridge along the width.
    b.style(roof, p.i("roof") ? METAL : WOOD);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(s * 0.64, 2.03, 0), V3(0, 2.31, 0), 2.34, 0.04, V3(0, 1, 0), 0.006);
    b.box(V3(0, 2.32, 0), V3(0.14, 0.045, 2.36), 0.008);
    b.style(dark, WOOD);
    for (int s = -1; s <= 1; s += 2) b.extrude({V2(-0.60, 2.0), V2(0.60, 2.0), V2(0, 2.28)}, 0.03, s * 0.99);
    if (!p.i("roof")) {
        b.style(roof * 0.78, WOOD);
        for (int k = 1; k <= 3; k++) {
            const double t = k * 0.24;  // along the slope, eave to ridge
            const double x = 0.64 * (1 - t), y = 2.03 + 0.28 * t;
            b.beam(V3(x + 0.045, y + 0.0275, 0), V3(x - 0.045, y + 0.0675, 0), 2.30, 0.008, V3(0, 1, 0));
        }
    }
}

// Steel frame: square-tube posts, aluminium-framed map, painted header and a sloping cover.
void boardSteel(B& b, const P& p) {
    const double w = p("wear");
    const V3 steel = aged(kSteelDk, w * 0.5), head = aged(paint(p.i("paint")), w);
    b.style(steel, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, 1.1, s * 0.95), V3(0.09, 2.2, 0.09), 0.008);
        b.box(V3(0, 0.008, s * 0.95), V3(0.22, 0.016, 0.22), 0.003);  // base plate
        b.style(kGalv, METAL);
        for (int dx = -1; dx <= 1; dx += 2)
            for (int dz = -1; dz <= 1; dz += 2) b.box(V3(dx * 0.085, 0.022, s * 0.95 + dz * 0.085), V3(0.026, 0.014, 0.026), 0.003);
        b.color(steel).mat(PAINT);
    }
    b.box(V3(0, 0.55, 0), V3(0.07, 0.06, 1.9), 0.005);
    b.box(V3(0, 1.70, 0), V3(0.07, 0.06, 1.9), 0.005);
    // Back plate and aluminium frame.
    b.style(V3(0.16, 0.17, 0.18), PAINT);
    b.box(V3(0.0, 1.12, 0), V3(0.03, 1.0, 1.60), 0.004);
    b.style(kGalv * 1.1, METAL);
    b.box(V3(0.035, 1.12 + 0.495, 0), V3(0.03, 0.045, 1.64), 0.005);
    b.box(V3(0.035, 1.12 - 0.495, 0), V3(0.03, 0.045, 1.64), 0.005);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0.035, 1.12, s * 0.80), V3(0.03, 1.0, 0.045), 0.005);
    mapArt(b, 0.02, 0, 1.12, 1.52, 0.92);
    // Header.
    b.style(head, PAINT);
    b.box(V3(0.02, 1.9, 0), V3(0.05, 0.30, 1.8), 0.008);
    b.style(V3(0.93, 0.93, 0.9), MATTE);
    b.letters("TRAIL MAP", V3(0.045, 1.9, 0), 0.025, 0.012);
    // Legend strip with colour chips.
    b.style(V3(0.9, 0.88, 0.8), MATTE);
    b.box(V3(0.04, 0.38, 0), V3(0.02, 0.14, 1.2), 0.004);
    const V3 chips[4] = {V3(0.33, 0.47, 0.24), V3(0.26, 0.48, 0.74), V3(0.44, 0.25, 0.10), V3(0.80, 0.08, 0.06)};
    for (int k = 0; k < 4; k++) {
        b.color(chips[k]);
        b.box(V3(0.052, 0.38, -0.45 + k * 0.3), V3(0.006, 0.07, 0.07));
        b.color(0.35, 0.35, 0.35);
        b.box(V3(0.052, 0.38, -0.45 + k * 0.3 + 0.12), V3(0.006, 0.014, 0.1));
    }
    // The cover: a thin sloping roof on two arms.
    b.style(steel, PAINT);
    b.extrude({V2(-0.2, 2.34), V2(0.55, 2.2), V2(0.55, 2.245), V2(-0.2, 2.385)}, 2.2);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(0, 2.1, s * 0.95), V3(0.5, 2.21, s * 0.95), 0.05, 0.05, V3(0, 1, 0), 0.004);
    b.style(kGalv, METAL);
    b.box(V3(0.56, 2.215, 0), V3(0.02, 0.04, 2.2), 0.004);
}

// A single log post with a hung plank board and a small shingled roof.
void boardRustic(B& b, const P& p) {
    const double w = p("wear");
    const V3 log = aged(V3(0.38, 0.27, 0.17), w), plank = aged(kWoodNew, w), dark = aged(kWoodDark, w);
    b.style(log, WOOD);
    b.lathe(V3(0, 0, 0), {V2(0.15, 0), V2(0.135, 0.15), V2(0.12, 2.1), V2(0.135, 2.18), V2(0.08, 2.28), V2(0, 2.31)}, 10);
    // Two rails at the back of the board.
    for (double y : {0.98, 1.74}) b.box(V3(0.07, y, 0), V3(0.05, 0.09, 1.92), 0.008);
    // The board: vertical planks, a little uneven.
    for (int k = 0; k < 6; k++) {
        const double z = -0.75 + k * 0.30, t = hash01(5, k);
        b.color(plank * (0.85 + 0.25 * t));
        b.box(V3(0.14 + 0.006 * t, 1.36, z), V3(0.04, 1.0 - 0.02 * hash01(6, k), 0.292), 0.005);
    }
    b.style(dark, WOOD);
    b.box(V3(0.164, 1.82, 0), V3(0.02, 0.12, 1.78), 0.004);  // title batten
    b.style(V3(0.10, 0.06, 0.035), MATTE);
    b.letters("TRAIL MAP", V3(0.174, 1.82, 0), 0.0165, 0.008);
    mapArt(b, 0.163, 0, 1.28, 1.30, 0.70);
    // Four pins.
    b.style(V3(0.7, 0.1, 0.08), MATTE);
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.sphere(V3(0.168, 1.28 + sy * 0.33, sz * 0.62), V3(0.012, 0.012, 0.012), 0);
    // Little split-shingle roof.
    b.style(aged(V3(0.34, 0.25, 0.16), w), WOOD);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(s * 0.55, 2.10, 0), V3(0, 2.38, 0), 1.9, 0.04, V3(0, 1, 0), 0.006);
    b.style(aged(V3(0.26, 0.19, 0.12), w), WOOD);
    for (int k = 1; k <= 2; k++) {
        const double t = k * 0.3, x = 0.55 * (1 - t), y = 2.10 + 0.28 * t;
        b.beam(V3(x + 0.05, y + 0.0275, 0), V3(x - 0.05, y + 0.0725, 0), 1.86, 0.008, V3(0, 1, 0));
    }
    b.style(log, WOOD);
    for (int s = -1; s <= 1; s += 2) b.beam(V3(0.05, 1.98, s * 0.0), V3(0.38, 2.14, s * 0.0), 0.06, 0.06, V3(0, 1, 0), 0.006);
    // Stones round the foot.
    stones3(b, 0.2, 91);
}

Reg mapBoard({"map_board", "MAP BOARD", "TRAIL AND ROAD", "Trailhead information board with a coloured map and a you-are-here dot", 2.4,
              {
                  {"TIMBER KIOSK", "Two timber posts, gable roof, framed map, leaflet box",
                   {num("wear", "WEAR", 0.3, 0, 1), choice("roof", "ROOF", 0, {"CEDAR SHINGLE", "GREEN METAL"})}, boardKiosk},
                  {"STEEL FRAME", "Steel posts, aluminium frame, painted header and cover",
                   {paintParam(7), num("wear", "WEAR", 0.2, 0, 1)}, boardSteel},
                  {"RUSTIC POST", "One log post, hung plank board, shingle cap", {num("wear", "WEAR", 0.35, 0, 1)}, boardRustic},
              }});

// ---- 3. Marker post --------------------------------------------------------------------

// A reflector: a small painted housing and an emissive face (a single quad), on +X at height y.
void reflector(B& b, double x, double y, double z, const V3& col, double w = 0.05, double h = 0.1) {
    b.style(col * 0.9, PAINT);
    b.box(V3(x, y, z), V3(0.02, h + 0.02, w + 0.01));
    b.style(col, EMISSIVE);
    const double fx = x + 0.0105;
    b.quad(V3(fx, y - h / 2, z + w / 2), V3(fx, y - h / 2, z - w / 2), V3(fx, y + h / 2, z - w / 2), V3(fx, y + h / 2, z + w / 2));
}

void markerFlex(B& b, const P& p) {
    b.style(aged(V3(0.90, 0.90, 0.88), p("wear")), MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.045, 0), V2(0.045, 0.05), V2(0.03, 0.07), V2(0.03, 1.09), V2(0.012, 1.125)}, 6);
    reflector(b, 0.035, 0.92, 0, paint(p.i("colour") ? 8 : 4), 0.04, 0.1);
}

void markerTimber(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = paint(p.i("colour") ? 8 : 4);
    b.style(aged(kWoodMid, w), WOOD);
    b.box(V3(0, 0.42, 0), V3(0.075, 0.84, 0.075), 0.004);
    b.style(col * (1.0 - 0.3 * w), PAINT);  // the painted top
    b.box(V3(0, 0.935, 0), V3(0.078, 0.19, 0.078));
    b.cone(V3(0, 1.03, 0), V3(0, 1.10, 0), 0.056, 0.004, 4);
    reflector(b, 0.046, 0.76, 0, V3(0.95, 0.95, 0.9), 0.03, 0.045);
}

void markerRock(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed");
    b.color(V3(0.46, 0.44, 0.40) * (0.9 + 0.2 * hash01(seed, 1))).mat(ROCK);
    b.blob(V3(-0.05, 0.1, 0.05), V3(0.34, 0.2, 0.30), 0.14, seed, 0, 0.7, 0.0);
    b.blob(V3(0.17, 0.1, -0.12), V3(0.2, 0.13, 0.18), 0.14, seed + 3, 0, 0.7, 0.0);
    b.color(V3(0.40, 0.39, 0.36));
    b.blob(V3(0.0, 0.21, 0.0), V3(0.14, 0.1, 0.13), 0.12, seed + 9, 0, 0.7);
    b.style(aged(kWoodOld, p("wear")), WOOD);
    b.cyl(V3(0, 0.15, 0.0), V3(0, 1.08, 0.0), 0.024, 4);
    reflector(b, 0.03, 0.95, 0.0, paint(p.i("colour") ? 8 : 4), 0.04, 0.09);
}

Reg markerPost({"marker_post", "MARKER POST", "TRAIL AND ROAD", "Trail-edge marker with a reflector, set along routes", 1.2,
                {
                    {"FLEX POST", "White flexible post with an orange reflector",
                     {choice("colour", "REFLECTOR", 0, {"ORANGE", "YELLOW"}), num("wear", "WEAR", 0.2, 0, 1)}, markerFlex},
                    {"TIMBER STAKE", "Timber stake with a painted top and tacked reflector",
                     {choice("colour", "PAINT", 0, {"ORANGE", "YELLOW"}), num("wear", "WEAR", 0.3, 0, 1)}, markerTimber},
                    {"ROCK AND POLE", "Little rock pile with a pole and reflector",
                     {seedParam(7), choice("colour", "REFLECTOR", 0, {"ORANGE", "YELLOW"}), num("wear", "WEAR", 0.3, 0, 1)}, markerRock},
                }});

// ---- 4. Cairn ---------------------------------------------------------------------------

V3 stoneCol(uint32_t seed, int k) {
    const double t = hash01(seed, 40 + k);
    return mix(V3(0.43, 0.41, 0.38), V3(0.52, 0.47, 0.40), hash01(seed, 70 + k)) * (0.84 + 0.3 * t);
}

void cairnFlat(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed");
    const int n = std::clamp(p.i("stones"), 4, 9);
    double y = 0;
    for (int k = 0; k < n; k++) {
        const double f = k / double(n - 1);
        const double r = 0.34 * (1 - 0.66 * f) * (0.9 + 0.2 * hash01(seed, k));
        const double th = 0.07 + 0.012 * hash01(seed, 20 + k);
        const double j = 0.045 * (1 - f);
        b.color(stoneCol(seed, k)).mat(ROCK);
        b.at(V3((hash01(seed, 30 + k) - 0.5) * 2 * j, y, (hash01(seed, 50 + k) - 0.5) * 2 * j), hash01(seed, 60 + k) * 360, [&] {
            b.blob(V3(0, th - 0.015 * (k == 0), 0), V3(r, th, r * (0.78 + 0.2 * hash01(seed, 80 + k))), 0.08, seed + k * 7, k < 1 ? 1 : 0, 0.75, k == 0 ? 0.0 : -1e9);
        });
        y += th * 1.7;
    }
}

void cairnRough(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed");
    int k = 0;
    auto rock = [&](double x, double y, double z, double rx, double ry, double rz, int det) {
        b.color(stoneCol(seed, k)).mat(ROCK);
        b.blob(V3(x, y, z), V3(rx, ry, rz), 0.16, seed + 11 * k, det, 0.7, y < 0.2 ? 0.0 : -1e9);
        k++;
    };
    // A heap: a ring of big stones, a second ring, a cap.
    for (int i = 0; i < 4; i++) {
        const double a = i * 1.5708 + hash01(seed, i) * 0.7, d = 0.22 + 0.05 * hash01(seed, 5 + i);
        rock(std::cos(a) * d, 0.12, std::sin(a) * d, 0.2 + 0.05 * hash01(seed, 9 + i), 0.14 + 0.04 * hash01(seed, 14 + i), 0.18 + 0.05 * hash01(seed, 19 + i), 0);
    }
    rock(0.02, 0.14, 0.0, 0.26, 0.17, 0.24, 1);
    for (int i = 0; i < 2; i++) {
        const double a = i * 3.1 + 0.8 + hash01(seed, 25 + i), d = 0.13;
        rock(std::cos(a) * d, 0.30, std::sin(a) * d, 0.17, 0.12, 0.16, 0);
    }
    rock(0, 0.40, 0, 0.15, 0.11, 0.13, 0);
    // The pole stuck in the heap, with a rag.
    b.style(aged(kWoodOld, p("wear")), WOOD);
    b.cyl(V3(0.03, 0.3, 0.02), V3(0.04, 1.2, 0.0), 0.026, 4);
    b.style(V3(0.78, 0.12, 0.08), MATTE);
    b.quad(V3(0.05, 1.16, 0.0), V3(0.05, 1.03, -0.24), V3(0.05, 1.15, -0.28), V3(0.05, 1.19, -0.1));
}

void cairnTall(B& b, const P& p) {
    const uint32_t seed = (uint32_t)p.i("seed");
    const int n = std::clamp(p.i("stones"), 4, 8);
    double y = 0;
    for (int k = 0; k < n; k++) {
        const double f = k / double(n - 1);
        const double r = 0.88 * (0.22 - 0.13 * f) * (0.88 + 0.24 * hash01(seed, k));
        const double ry = r * (0.55 + 0.35 * hash01(seed, 10 + k)) * (k == 0 ? 1.1 : 1.0);
        const double off = k ? (hash01(seed, 20 + k) - 0.5) * 0.04 : 0;
        b.color(stoneCol(seed, k)).mat(ROCK);
        b.at(V3(off, y, (hash01(seed, 30 + k) - 0.5) * 0.04), hash01(seed, 40 + k) * 360, [&] {
            b.blob(V3(0, ry - (k == 0 ? 0.03 : 0.0), 0), V3(r, ry, r * (0.82 + 0.2 * hash01(seed, 50 + k))), 0.06, seed + k * 5, k < 2 ? 1 : 0, 0.6, k == 0 ? 0.0 : -1e9);
        });
        y += ry * 1.78;
    }
}

Reg cairn({"cairn", "CAIRN", "TRAIL AND ROAD", "Stacked stones that mark a route above the trees", 1.1,
           {
               {"FLAT STACK", "Tidy pile of flat stones, biggest at the bottom", {seedParam(3), num("stones", "STONES", 7, 5, 9, 1)}, cairnFlat},
               {"ROUGH HEAP", "Rough heap of rocks with a pole and a red rag", {seedParam(5), num("wear", "WEAR", 0.3, 0, 1)}, cairnRough},
               {"BALANCED TOWER", "One tall column of balanced round stones", {seedParam(2), num("stones", "STONES", 6, 4, 8, 1)}, cairnTall},
           }});


// ---- 5. Race gate ---------------------------------------------------------------------------

const char* kSponsor = "CRUMPLE ZONE";

// A banner across the gate, standing on x = 0 between z = +-bw/2 from y0 to y0 + bh, lettered both
// ways round, with a chequered strip along the bottom at the finish.
void bannerPanel(B& b, double bw, double y0, double bh, double px, const V3& base, const V3& ink, bool fin) {
    const double yc = y0 + bh / 2;
    b.style(base, MATTE);
    b.box(V3(0, yc, 0), V3(0.04, bh, bw), 0.004);
    const double depth = std::clamp(px * 0.18, 0.006, 0.03);
    b.color(ink);
    b.letters(kSponsor, V3(0.02, yc + (fin ? px * 0.35 : 0), 0), px, depth);
    b.push();
    b.yaw(180);
    b.letters(kSponsor, V3(0.02, yc + (fin ? px * 0.35 : 0), 0), px, depth);
    b.pop();
    if (fin) {  // a row of squares along the bottom edge
        const double sq = std::max(px * 1.4, bw / 36);
        const int n = std::max(2, (int)std::lround(bw / sq));
        const double w = bw / n;
        const double h = std::min(px * 1.5, bh * 0.2);
        b.color(0.05, 0.05, 0.06);
        for (int k = 0; k < n; k += 2)
            for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.0225, y0 + h / 2 + 0.01, -bw / 2 + (k + 0.5) * w), V3(0.005, h, w));
    }
}

// Checks (alternate squares) along the front and back of a flat strip, for the finish.
void checks(B& b, double xFace, double yc, double zc, double len, double h, const V3& c) {
    const double sq = std::max(h, len / 36);
    const int n = std::max(2, (int)std::lround(len / sq));
    const double w = len / n;
    b.color(c);
    for (int k = 0; k < n; k += 2)
        for (int s = -1; s <= 1; s += 2) b.box(V3(s * (xFace + 0.002), yc, zc - len / 2 + (k + 0.5) * w), V3(0.004, h, w));
}

// ---- A: inflatable arch.
void gateArch(B& b, const P& p) {
    const double W = std::clamp(p("width"), 4.0, 40.0), a = W / 2;
    const bool fin = p.i("finish") != 0;
    const double r = std::clamp(0.28 + 0.012 * W, 0.3, 0.7), legH = 4.65 + r, rise = 0.9 + 0.05 * W;
    const V3 col = fin ? V3(0.90, 0.90, 0.88) : paint(p.i("paint"));
    const V3 alt = fin ? V3(0.05, 0.05, 0.06) : V3(0.90, 0.90, 0.88);
    // The arch path: up one leg, over, down the other.
    std::vector<V3> pts;
    const int legN = std::max(3, (int)std::ceil(legH / 0.45));
    for (int i = 0; i < legN; i++) pts.push_back(V3(0, legH * i / legN, a));
    const int N = std::clamp((int)std::lround(M_PI * (a + rise) / 2 / 0.5), 20, 90);
    for (int i = 0; i <= N; i++) {
        const double t = M_PI * i / N;
        pts.push_back(V3(0, legH + rise * std::sin(t), a * std::cos(t)));
    }
    for (int i = 1; i <= legN; i++) pts.push_back(V3(0, legH * (1.0 - (double)i / legN), -a));
    // One continuous tube in the main colour, with the bands slipped over it as slightly fatter sleeves.
    b.style(col, MATTE);
    b.tube(pts, r, 12, false, true);
    const double band = fin ? 1.7 * r : 2.4 * r;
    double s = 0;
    b.color(alt);
    for (size_t i = 1; i < pts.size(); i++) {
        const double s0 = s;
        s += len(pts[i] - pts[i - 1]);
        const int chunk = (int)std::floor((s0 + s) / 2 / band);
        const bool odd = fin ? (chunk % 2 == 1) : (chunk % 8 == 7);
        if (odd) b.cyl(pts[i - 1], pts[i], r * 1.06, 12, false);
    }
    // Feet: wide flares and a dark ballast skirt.
    for (int sd = -1; sd <= 1; sd += 2) {
        b.style(col * 0.85, MATTE);
        b.cone(V3(0, 0, sd * a), V3(0, 0.35, sd * a), r * 1.4, r, 14, false);
        b.style(V3(0.07, 0.07, 0.08), MATTE);
        b.cyl(V3(0, 0, sd * a), V3(0, 0.05, sd * a), r * 1.45, 14);
    }
    // The banner, hung from the arch on four straps.
    const double yBot = 4.62;
    double bw = std::min(W - 2 * r - 0.5, 4.0 + 0.32 * W);
    const double yUnder = legH - r + rise * std::sqrt(1 - std::pow(bw / 2 / a, 2));
    double px = std::min(0.2, bw * 0.86 / 71);
    px = std::min(px, (yUnder - 0.15 - yBot) / 10.0);
    const double bh = 10 * px;
    bannerPanel(b, bw, yBot, bh, px, fin ? V3(0.93, 0.93, 0.90) : V3(0.10, 0.11, 0.14), fin ? V3(0.06, 0.06, 0.07) : V3(0.95, 0.93, 0.88), fin);
    b.style(V3(0.08, 0.08, 0.09), MATTE);
    for (int sd = -1; sd <= 1; sd += 2)
        for (double f : {0.97, 0.03}) {
            const double z = sd * bw / 2 * f;
            const double yTop = legH + rise * std::sqrt(std::max(0.0, 1 - std::pow(z / a, 2)));
            b.tube({V3(0, yBot + bh, z), V3(0, yTop, z)}, 0.012, 4, false, false);
        }
}

// ---- B: two flag towers and a banner.
void flag(B& b, double dir, double zRoot, double y0, double L, double h, const V3& c1, const V3& c2, bool fin) {
    // A swallow-tailed flag streaming outward from the mast: (u = -z).
    auto U = [&](double s) { return -dir * (zRoot + s); };
    Poly top, bot;
    const int n = 7;
    for (int i = 0; i <= n; i++) {
        const double s = L * i / n, f = (double)i / n;
        top.push_back(V2(U(s), y0 + h * (1 - 0.25 * f) + 0.07 * std::sin(f * 9) * f));
    }
    for (int i = n; i >= 0; i--) {
        const double s = L * i / n, f = (double)i / n;
        bot.push_back(V2(U(s), y0 + h * 0.25 * f + 0.07 * std::sin(f * 9 + 0.8) * f));
    }
    Poly poly = top;
    poly.insert(poly.end(), bot.begin(), bot.end());
    b.color(c1);
    plate(b, poly, 0, 0.02);
    if (fin) {  // black squares on the white
        b.color(c2);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 3; j++)
                if ((i + j) % 2 == 0) {
                    const double s = L * (i + 0.5) / 4.8, f = (double)i / 6;
                    b.box(V3(0.0, y0 + h * (0.17 + 0.28 * j) * (1 - 0.2 * f) + 0.07 * std::sin(f * 9) * f * 0.0, dir * (zRoot + s)), V3(0.024, h * 0.24, L / 4.8 * 0.98));
                }
    } else {  // a pale stripe
        b.color(c2);
        b.box(V3(0.0, y0 + h * 0.5, dir * (zRoot + L * 0.45)), V3(0.024, h * 0.14, L * 0.8));
    }
}

void gateFlags(B& b, const P& p) {
    const double W = std::clamp(p("width"), 4.0, 40.0), a = W / 2;
    const bool fin = p.i("finish") != 0;
    const double bw = W - 0.7;
    double px = std::min(0.2, bw * 0.86 / 71);
    px = std::max(px, 0.03);
    const double bh = 10 * px, yBot = 4.62, yTop = yBot + bh;
    const double Ht = yTop + 1.0 + 0.05 * W * 0.3;
    const double r0 = std::clamp(0.10 + 0.004 * W, 0.12, 0.22);
    const V3 steel = V3(0.72, 0.73, 0.74);
    const V3 fcol = fin ? V3(0.92, 0.92, 0.9) : paint(p.i("paint"));
    for (int sd = -1; sd <= 1; sd += 2) {
        const double z = sd * a;
        b.style(V3(0.55, 0.54, 0.52), CONCRETE);
        b.box(V3(0, 0.12, z), V3(0.8, 0.24, 0.8), 0.03);
        b.style(steel, METAL);
        b.lathe(V3(0, 0.24, z), {V2(r0 * 1.7, 0), V2(r0 * 1.7, 0.03), V2(r0 * 1.15, 0.08), V2(r0, 0.2), V2(r0 * 0.55, Ht - 0.24 - 0.2), V2(r0 * 0.6, Ht - 0.24 - 0.1), V2(0.0, Ht - 0.24 + 0.0)}, 12);
        // The ball at the top.
        b.style(V3(0.9, 0.78, 0.2), PAINT);
        b.sphere(V3(0, Ht + 0.02, z), V3(0.09, 0.09, 0.09), 1);
        // The flag, streaming outward.
        flag(b, sd, a + r0 * 0.5, Ht - 1.15 - 0.01 * W, std::clamp(0.7 + 0.06 * W, 0.9, 2.6), std::clamp(0.55 + 0.02 * W, 0.6, 1.1), fcol, fin ? V3(0.05, 0.05, 0.06) : V3(0.93, 0.93, 0.9), fin);
        // Guy wires.
        b.style(V3(0.1, 0.1, 0.11), MATTE);
        for (int sx = -1; sx <= 1; sx += 2) b.tube({V3(0, Ht * 0.78, z + sd * r0), V3(sx * Ht * 0.45, 0.0, z + sd * Ht * 0.5)}, 0.012, 4, false, false);
        b.style(V3(0.9, 0.5, 0.1), PAINT);
        b.box(V3(0, 1.0, z + sd * (r0 + 0.01)), V3(0.04, 1.2, 0.01));  // a hazard stripe
        b.style(V3(0.05, 0.05, 0.06), PAINT);
        for (int k = 0; k < 5; k++) b.box(V3(0, 0.55 + k * 0.2 + 0.1, z + sd * (r0 + 0.012)), V3(0.042, 0.1, 0.012));
    }
    // The rope and the hung banner.
    b.style(V3(0.1, 0.1, 0.11), MATTE);
    b.tube({V3(0, yTop + 0.06, -a + r0 * 0.6), V3(0, yTop + 0.06, 0), V3(0, yTop + 0.06, a - r0 * 0.6)}, 0.018, 4, false, false);
    const double bw2 = W - 2 * r0 - 0.15;
    bannerPanel(b, bw2, yBot, bh, std::min(px, 0.2), fin ? V3(0.93, 0.93, 0.90) : paint(p.i("paint")),
                fin ? V3(0.06, 0.06, 0.07) : V3(0.06, 0.06, 0.08), fin);
}

// ---- C: steel truss timing gantry.
void trussZ(B& b, double z0, double z1, double yb, double d, double dx, double c, int n) {
    const double L = (z1 - z0) / n;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = 0; sy <= 1; sy++) b.box(V3(sx * dx / 2, yb + sy * d, (z0 + z1) / 2), V3(c, c, z1 - z0), 0.004);
    const double c2 = c * 0.55;
    for (int i = 0; i < n; i++) {
        const double za = z0 + i * L, zb = za + L;
        const bool up = i % 2 == 0;
        for (int sx = -1; sx <= 1; sx += 2) b.beam(V3(sx * dx / 2, yb + (up ? 0 : d), za), V3(sx * dx / 2, yb + (up ? d : 0), zb), c2, c2);
        b.beam(V3(-dx / 2, yb + d, za), V3(dx / 2, yb + d, zb), c2, c2);  // top bracing
        b.beam(V3(dx / 2, yb, za), V3(-dx / 2, yb, zb), c2, c2);          // bottom bracing
    }
    for (int i = 0; i <= n; i++)
        for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * dx / 2, yb + d / 2, z0 + i * L), V3(c2, d, c2));
}

void trussY(B& b, double cx, double cz, double s, double h, double c, int n) {
    const double L = h / n, c2 = c * 0.55;
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dz = -1; dz <= 1; dz += 2) b.box(V3(cx + dx * s / 2, h / 2, cz + dz * s / 2), V3(c, h, c), 0.004);
    for (int i = 0; i < n; i++) {
        const double ya = i * L, yb2 = ya + L;
        for (int k = 0; k < 4; k++) {  // four faces, alternate diagonals
            const double fx = (k == 0 ? 1 : k == 1 ? -1 : 0), fz = (k == 2 ? 1 : k == 3 ? -1 : 0);
            const double ux = fz != 0 ? 1 : 0, uz = fx != 0 ? 1 : 0;  // along the face
            const double sg = ((i + k) % 2 ? 1 : -1) * s / 2;
            b.beam(V3(cx + fx * s / 2 - ux * sg, ya, cz + fz * s / 2 - uz * sg), V3(cx + fx * s / 2 + ux * sg, yb2, cz + fz * s / 2 + uz * sg), c2, c2);
        }
    }
    for (int i = 0; i <= n; i += (n > 3 ? 2 : 1))
        for (int k = 0; k < 4; k++) {
            const double fx = (k == 0 ? 1 : k == 1 ? -1 : 0), fz = (k == 2 ? 1 : k == 3 ? -1 : 0);
            b.box(V3(cx + fx * s / 2, i * L, cz + fz * s / 2), V3(fz != 0 ? s : c2, c2, fx != 0 ? s : c2));
        }
}

void gateTruss(B& b, const P& p) {
    const double W = std::clamp(p("width"), 4.0, 40.0), a = W / 2;
    const bool fin = p.i("finish") != 0;
    const double s = std::clamp(0.55 + 0.008 * W, 0.6, 0.85), d = std::clamp(0.7 + 0.025 * W, 0.8, 1.7), yb = 4.7;
    const double c = 0.07 + 0.0012 * W;
    const V3 steel = aged(paint(p.i("paint")) , 0.0);
    b.style(steel, PAINT);
    const int bays = std::max(2, (int)std::lround((W + s) / 1.5));
    trussZ(b, -a - s / 2, a + s / 2, yb, d, s, c, bays);
    const int vb = std::max(3, (int)std::lround((yb + d) / 1.1));
    for (int sd = -1; sd <= 1; sd += 2) {
        trussY(b, 0, sd * a, s, yb + d, c, vb);
        b.style(V3(0.5, 0.5, 0.5), METAL);
        b.box(V3(0, 0.012, sd * a), V3(s + 0.35, 0.024, s + 0.35), 0.004);
        // The timing eye: a little housing on each column.
        b.style(V3(0.95, 0.72, 0.06), PAINT);
        b.box(V3(s / 2 + 0.05, 0.8, sd * (a - s / 2 + 0.05)), V3(0.1, 0.22, 0.1), 0.01);
        b.style(steel, PAINT);
    }
    // The strip along the truss: orange, or checks at the finish.
    const double sw = W + s;
    b.style(fin ? V3(0.92, 0.92, 0.9) : V3(0.95, 0.45, 0.08), PAINT);
    b.box(V3(0, yb + d * 0.5 * 0.0 + 0.19, 0), V3(s + 0.06, 0.26, sw), 0.006);
    if (fin) checks(b, s / 2 + 0.03, yb + 0.19, 0, sw, 0.26, V3(0.05, 0.05, 0.06));
    // The clock board on the front, with its brackets.
    const double bwB = std::clamp(W * 0.25, 1.9, 5.0);
    const double px = std::min(0.12, bwB * 0.86 / 29);
    const double bhB = 10.5 * px;
    const double yc = yb + d * 0.5 + (d > 1.0 ? 0.0 : 0.0);
    const double xF = s / 2 + 0.2;
    b.style(V3(0.10, 0.10, 0.11), PAINT);
    b.box(V3(xF, yc, 0), V3(0.16, bhB, bwB), 0.012);
    b.style(steel, PAINT);
    for (int k = -1; k <= 1; k += 2) b.box(V3((xF + s / 2) / 2, yc, k * bwB * 0.35), V3(xF - s / 2, 0.06, 0.07), 0.004);
    b.style(V3(0.02, 0.02, 0.03), MATTE);
    b.box(V3(xF + 0.082, yc, 0), V3(0.004, bhB - 0.1, bwB - 0.1));
    b.style(V3(1.0, 0.16, 0.05), EMISSIVE);
    b.letters("00:00", V3(xF + 0.084, yc, 0), px, 0.01);
    // The back of the board, lettered too, so it reads from behind.
    b.style(V3(0.9, 0.9, 0.9), MATTE);
    b.push();
    b.yaw(180);
    b.letters("CZ", V3(-(xF - 0.082) + 0.0, yc, 0), px * 0.8, 0.01);
    b.pop();
    // A beacon on top.
    b.style(V3(0.95, 0.5, 0.05), EMISSIVE);
    b.cyl(V3(0, yb + d + 0.08, 0), V3(0, yb + d + 0.22, 0), 0.08, 10);
}

Reg raceGate({"race_gate", "RACE GATE", "TRAIL AND ROAD", "The gate cars drive through in race and sprint levels; the start/finish one is checkered", 14,
              {
                  {"INFLATABLE ARCH", "Fat orange tube arch with a hung CRUMPLE ZONE banner",
                   {num("width", "WIDTH", 14, 4, 40, 1), choice("finish", "FINISH", 0, {"GATE", "START/FINISH"}), paintParam(4)}, gateArch},
                  {"FLAG TOWERS", "Two guyed masts with flags and a banner between",
                   {num("width", "WIDTH", 14, 4, 40, 1), choice("finish", "FINISH", 0, {"GATE", "START/FINISH"}), paintParam(4)}, gateFlags},
                  {"TIMING TRUSS", "Steel truss gantry with a clock board",
                   {num("width", "WIDTH", 14, 4, 40, 1), choice("finish", "FINISH", 0, {"GATE", "START/FINISH"}), paintParam(9)}, gateTruss},
              }});

// ---- 6. Traffic cone ------------------------------------------------------------------------

const V3 kConeOrange(0.95, 0.38, 0.05);

// A cone with its white bands: radius falls linearly from r0 at h0 to r1 at h1.
void coneBody(B& b, double h0, double h1, double r0, double r1, const V3& col, std::vector<std::pair<double, double>> bands, int segs = 14) {
    auto R = [&](double h) { return r0 + (r1 - r0) * (h - h0) / (h1 - h0); };
    b.style(col, MATTE);
    b.lathe(V3(0, 0, 0), {V2(r0, h0), V2(r1, h1 - 0.03), V2(r1 * 0.7, h1), V2(0, h1)}, segs);
    b.color(0.92, 0.92, 0.9);
    for (const auto& bd : bands) b.lathe(V3(0, 0, 0), {V2(R(bd.first) + 0.0025, bd.first), V2(R(bd.second) + 0.0025, bd.second)}, segs, false);
}

void coneTall(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = aged(kConeOrange, w);
    b.style(p.i("base") ? col * 0.9 : V3(0.07, 0.07, 0.075), MATTE);
    b.extrudeUp(roundedRect(0.38, 0.38, 0.05, 2), 0.035);
    b.box(V3(0, 0.043, 0), V3(0.3, 0.016, 0.3), 0.006);
    coneBody(b, 0.04, 0.70, 0.145, 0.026, col, {{0.38, 0.50}, {0.27, 0.32}});
}

void coneShort(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = aged(paint(p.i("colour") ? 8 : 4) * 0.98, w);
    b.style(col * 0.95, MATTE);
    b.extrudeUp(roundedRect(0.32, 0.32, 0.12, 4), 0.03);  // moulded square-round base
    coneBody(b, 0.03, 0.46, 0.115, 0.022, col, {{0.24, 0.34}}, 12);
}

void delineator(B& b, const P& p) {
    const double w = p("wear");
    b.style(V3(0.07, 0.07, 0.075), MATTE);
    b.extrudeUp(roundedRect(0.30, 0.30, 0.07, 3), 0.045);
    b.lathe(V3(0, 0, 0), {V2(0.1, 0.045), V2(0.075, 0.09), V2(0.05, 0.12)}, 14);
    const V3 col = aged(p.i("colour") == 1 ? V3(0.90, 0.90, 0.88) : p.i("colour") == 2 ? paint(8) : kConeOrange, w);
    b.style(col, MATTE);
    b.lathe(V3(0, 0, 0), {V2(0.048, 0.12), V2(0.04, 0.9), V2(0.034, 1.0), V2(0, 1.02)}, 10);
    auto pr = [](double h) { return 0.048 - 0.008 * (h - 0.12) / 0.78 + 0.003; };
    b.color(0.92, 0.92, 0.9);
    b.lathe(V3(0, 0, 0), {V2(pr(0.56), 0.56), V2(pr(0.72), 0.72)}, 10, false);
    b.color(0.07, 0.07, 0.075);
    b.lathe(V3(0, 0, 0), {V2(pr(0.84), 0.84), V2(pr(0.9), 0.9)}, 10, false);
    // Reflector tab.
    b.style(V3(0.95, 0.95, 0.92), EMISSIVE);
    b.box(V3(0.046, 0.8, 0), V3(0.006, 0.07, 0.03));
}

Reg trafficCone({"traffic_cone", "TRAFFIC CONE", "TRAIL AND ROAD", "Orange cone with white bands, set out round hazards and in races", 0.8,
                 {
                     {"TALL CONE", "0.7 m cone on a heavy square base, three bands",
                      {num("wear", "WEAR", 0.25, 0, 1), choice("base", "BASE", 0, {"BLACK", "ORANGE"})}, coneTall},
                     {"SHORT CONE", "0.45 m moulded cone, one band", {choice("colour", "COLOUR", 0, {"ORANGE", "YELLOW"}), num("wear", "WEAR", 0.25, 0, 1)}, coneShort},
                     {"DELINEATOR", "Flexible bollard post on a rubber base",
                      {choice("colour", "COLOUR", 0, {"ORANGE", "WHITE", "YELLOW"}), num("wear", "WEAR", 0.2, 0, 1)}, delineator},
                 }});

// ---- 7. Oil drum ----------------------------------------------------------------------------

// A drum body turned on a lathe with an optional dent pressed into its side. `colour(h, angle)`
// gives the tint of each little patch, so rust and scuffs can vary over it.
void drumBody(B& b, std::vector<V2> prof, int segs, double dent, double dentAng, const std::function<V3(double, double)>& colour) {
    // Extra rows through the dented band.
    if (dent > 0) {
        std::vector<V2> q;
        for (size_t i = 0; i + 1 < prof.size(); i++) {
            q.push_back(prof[i]);
            const double span = prof[i + 1].y - prof[i].y;
            if (prof[i].y >= 0.26 && prof[i + 1].y <= 0.64 && span > 0.08) {
                const int m = (int)std::ceil(span / 0.07);
                for (int k = 1; k < m; k++) q.push_back(V2(prof[i].x + (prof[i + 1].x - prof[i].x) * k / m, prof[i].y + span * k / m));
            }
        }
        q.push_back(prof.back());
        prof = q;
    }
    const int n = (int)prof.size();
    std::vector<V3> P((size_t)n * segs);
    auto at = [&](int i, int j) -> V3& { return P[(size_t)i * segs + ((j % segs) + segs) % segs]; };
    for (int i = 0; i < n; i++)
        for (int j = 0; j < segs; j++) {
            const double t = 2 * M_PI * j / segs;
            double ad = std::fabs(std::remainder(t - dentAng, 2 * M_PI));
            const double h = prof[i].y;
            const double dp = dent * 0.045 * std::exp(-std::pow((h - 0.45) / 0.12, 2) - std::pow(ad * prof[i].x / 0.14, 2));
            const double r = std::max(0.0, prof[i].x - dp * (prof[i].x > 0.05 ? 1 : 0));
            at(i, j) = V3(std::cos(t) * r, h, -std::sin(t) * r);
        }
    auto nrm = [&](int i, int j) {
        const V3 dth = at(i, j + 1) - at(i, j - 1);
        const V3 dh = at(std::min(i + 1, n - 1), j) - at(std::max(i - 1, 0), j);
        V3 nn = cross(dth, dh);
        if (len(nn) < 1e-12) return V3(0, i == 0 ? -1 : 1, 0);
        return norm(nn);
    };
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j < segs; j++) {
            const double t = 2 * M_PI * (j + 0.5) / segs;
            b.color(colour((prof[i].y + prof[i + 1].y) / 2, t));
            const V3 a0 = at(i, j), a1 = at(i, j + 1), b1 = at(i + 1, j + 1), b0 = at(i + 1, j);
            b.triN(a0, a1, b1, nrm(i, j), nrm(i, j + 1), nrm(i + 1, j + 1));
            b.triN(a0, b1, b0, nrm(i, j), nrm(i + 1, j + 1), nrm(i + 1, j));
        }
}

const std::vector<V2> kSteelDrum = {V2(0, 0), V2(0.262, 0), V2(0.282, 0.012), V2(0.288, 0.04), V2(0.288, 0.22), V2(0.298, 0.235), V2(0.298, 0.265), V2(0.288, 0.28),
                                    V2(0.288, 0.60), V2(0.298, 0.615), V2(0.298, 0.645), V2(0.288, 0.66), V2(0.288, 0.838), V2(0.294, 0.85), V2(0.294, 0.878),
                                    V2(0.272, 0.88), V2(0.266, 0.862), V2(0, 0.862)};

void bungs(B& b, double yTop, const V3& cap, Mat m, double bigR = 0.034) {
    b.style(cap * 0.6, METAL);
    b.cyl(V3(0.12, yTop - 0.002, 0.10), V3(0.12, yTop + 0.01, 0.10), bigR + 0.004, 12);
    b.style(cap, m);
    b.cyl(V3(0.12, yTop + 0.01, 0.10), V3(0.12, yTop + 0.024, 0.10), bigR - 0.006, 12);
    b.style(cap * 0.6, METAL);
    b.cyl(V3(-0.11, yTop - 0.002, -0.12), V3(-0.11, yTop + 0.008, -0.12), bigR * 0.6, 10);
    b.style(cap, m);
    b.cyl(V3(-0.11, yTop + 0.008, -0.12), V3(-0.11, yTop + 0.018, -0.12), bigR * 0.42, 10);
}

void drumPaint(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = paint(p.i("paint"));
    const V3 edge = mix(col, V3(0.75, 0.74, 0.7), 0.25), dirt = V3(0.25, 0.2, 0.15);
    b.mat(PAINT);
    drumBody(b, kSteelDrum, 18, p("dent"), 0.3, [&](double h, double t) {
        V3 c = (std::fabs(h - 0.25) < 0.05 || std::fabs(h - 0.63) < 0.05 || h > 0.84 || h < 0.04) ? edge : col;  // worn edges on the hoops and chimes
        c = mix(c, dirt, w * (0.6 * std::max(0.0, 0.4 - h) + 0.15 * hash01((uint32_t)(h * 100), (uint32_t)(t * 10))));
        return c;
    });
    bungs(b, 0.862, V3(0.55, 0.56, 0.58), METAL);
    if (p.i("label")) {
        b.style(col.x + col.y + col.z > 1.6 ? V3(0.08, 0.08, 0.09) : V3(0.9, 0.88, 0.8), MATTE);
        b.letters("OIL", V3(0.268, 0.44, 0), 0.013, 0.02);
    }
}

void drumRust(B& b, const P& p) {
    const double w = p("wear");
    const V3 faded = paint(p.i("paint")) * 0.75;
    const V3 rustA(0.34, 0.15, 0.06), rustB(0.50, 0.25, 0.09), rustC(0.28, 0.11, 0.05);
    b.mat(METAL);
    const uint32_t seed = (uint32_t)p.i("seed");
    drumBody(b, kSteelDrum, 18, p("dent"), 2.2, [&](double h, double t) mutable {
        if (h > 0.855) t = 0.7;
        const double n1 = 0.5 + 0.5 * std::sin(t * 3 + h * 7 + seed * 1.7) * std::sin(t * 2 - h * 11 + seed);
        const double n2 = hash01(seed + (uint32_t)(h * 60), (uint32_t)(t * 9));
        const double rust = std::clamp(0.42 + 0.38 * w + 0.35 * (0.5 - h) + (n1 - 0.5) * 0.8, 0.0, 1.0);
        V3 c = mix(mix(rustA, rustB, 0.25 + 0.5 * n1), rustC, 0.5 * n2 * n2);
        return mix(faded, c, std::clamp(rust + (n2 - 0.5) * 0.4, 0.12, 1.0));
    });
    b.style(V3(0.30, 0.19, 0.12), METAL);
    bungs(b, 0.862, V3(0.40, 0.25, 0.14), METAL);
}

void drumPlastic(B& b, const P& p) {
    const V3 col = paint(p.i("paint")) * 0.95;
    const std::vector<V2> prof = {V2(0, 0), V2(0.275, 0), V2(0.292, 0.02), V2(0.292, 0.12), V2(0.298, 0.14), V2(0.298, 0.17), V2(0.292, 0.19), V2(0.292, 0.40),
                                  V2(0.298, 0.42), V2(0.298, 0.45), V2(0.292, 0.47), V2(0.292, 0.66), V2(0.298, 0.68), V2(0.298, 0.71), V2(0.292, 0.73), V2(0.292, 0.83),
                                  V2(0.298, 0.855), V2(0.298, 0.88), V2(0.27, 0.88), V2(0.262, 0.86), V2(0.0, 0.86)};
    b.mat(MATTE);
    drumBody(b, prof, 18, p("dent") * 0.5, 0.3, [&](double h, double t) { return col * (0.96 + 0.08 * hash01((uint32_t)(h * 80), (uint32_t)(t * 7))); });
    // Raised rim ring on the lid and the two threaded fittings.
    b.style(col * 0.9, MATTE);
    b.cyl(V3(0, 0.86, 0), V3(0, 0.868, 0), 0.25, 18);
    bungs(b, 0.868, V3(0.08, 0.08, 0.09), MATTE, 0.04);
    b.style(V3(0.9, 0.9, 0.88), MATTE);
    b.cyl(V3(0.12, 0.882, 0.10), V3(0.12, 0.886, 0.10), 0.014, 8);
}

Reg oilDrum({"oil_drum", "OIL DRUM", "TRAIL AND ROAD", "200 L drum: roadside clutter, track furniture, barricades", 0.9,
             {
                 {"PAINTED STEEL", "Painted drum with rolling hoops, bungs and a stencil",
                  {paintParam(5), num("wear", "WEAR", 0.3, 0, 1), num("dent", "DENT", 0.3, 0, 1), choice("label", "STENCIL", 1, {"NO", "YES"})}, drumPaint},
                 {"RUSTY DRUM", "Old drum, paint flaking to rust",
                  {seedParam(4), paintParam(1), num("wear", "RUST", 0.5, 0, 1), num("dent", "DENT", 0.5, 0, 1)}, drumRust},
                 {"PLASTIC BLUE", "Closed-head plastic drum, ribbed, screw fittings",
                  {paintParam(5), num("dent", "DENT", 0.0, 0, 1)}, drumPlastic},
             }});

// ---- 8. Jersey barrier ------------------------------------------------------------------------

using HalfW = std::function<double(double)>;

// The barrier cross-section between heights ys.front() and ys.back(): (right, up) pairs.
Poly bandPoly(const HalfW& hw, const std::vector<double>& cuts, double y0, double y1) {
    std::vector<double> ys{y0};
    for (double c : cuts)
        if (c > y0 + 1e-6 && c < y1 - 1e-6) ys.push_back(c);
    ys.push_back(y1);
    Poly q;
    for (double y : ys) q.push_back(V2(hw(y), y));
    for (int i = (int)ys.size() - 1; i >= 0; i--) q.push_back(V2(-hw(ys[i]), ys[i]));
    return q;
}

// Piece along the run: calls f(xa, xb, i) for each of n pieces with gaps.
template <class F>
void pieces(double L, int n, double gap, F f) {
    const double pl = (L - (n - 1) * gap) / n;
    for (int i = 0; i < n; i++) {
        const double xa = -L / 2 + i * (pl + gap);
        f(xa, xa + pl, i);
    }
}

double njHalf(double y) {  // the New Jersey profile
    if (y <= 0.075) return 0.30;
    if (y <= 0.33) return 0.30 - 0.085 * (y - 0.075) / 0.255;
    return 0.215 - 0.14 * (y - 0.33) / 0.47;
}

void jerseyConcrete(B& b, const P& p) {
    const double L = std::clamp(p("length"), 1.0, 30.0), w = p("wear");
    const int n = std::max(1, (int)std::lround(L / 3.0));
    const double gap = n > 1 ? 0.014 : 0.0;
    pieces(L, n, gap, [&](double xa, double xb, int i) {
        const double j = 0.92 + 0.12 * hash01(77, i);
        const V3 base = aged(kConcrete, w * 0.4) * j;
        b.mat(CONCRETE);
        b.color(base * V3(0.78, 0.76, 0.72));
        profileAlongX(b, bandPoly(njHalf, {0.075, 0.33}, 0, 0.17), xa, xb);
        b.color(base * V3(0.93, 0.92, 0.9));
        profileAlongX(b, bandPoly(njHalf, {0.075, 0.33}, 0.17, 0.46), xa, xb);
        b.color(base);
        profileAlongX(b, bandPoly(njHalf, {0.075, 0.33}, 0.46, 0.8), xa, xb);
        // Forklift slots through the toe.
        b.style(V3(0.1, 0.1, 0.1), MATTE);
        for (int sd = -1; sd <= 1; sd += 2)
            for (double f : {0.2, 0.8}) b.box(V3(xa + (xb - xa) * f, 0.035, sd * 0.2985), V3(0.3, 0.04, 0.005));
        // Lifting pins on the top.
        b.style(kSteelDk, METAL);
        for (double f : {0.18, 0.82}) b.box(V3(xa + (xb - xa) * f, 0.803, 0), V3(0.05, 0.012, 0.09), 0.003);
        // The cast date and form line.
        b.style(base * 0.8, CONCRETE);
        for (int sd = -1; sd <= 1; sd += 2) b.box(V3((xa + xb) / 2, 0.6, sd * njHalf(0.6)), V3(xb - xa - 0.1, 0.004, 0.004));
    });
    // Steel tie plates across each joint, on top.
    b.style(kSteelDk, METAL);
    for (int i = 0; i + 1 < n; i++) {
        const double x = -L / 2 + (i + 1) * ((L - (n - 1) * gap) / n) + i * gap + gap / 2;
        b.box(V3(x, 0.805, 0), V3(0.2, 0.01, 0.12), 0.003);
    }
}

double plasticHalf(double y) {
    static const double ys[] = {0.0, 0.08, 0.3, 0.46, 0.6, 0.72, 0.80};
    static const double ws[] = {0.30, 0.30, 0.27, 0.215, 0.20, 0.16, 0.13};
    for (int i = 0; i < 6; i++)
        if (y <= ys[i + 1]) return ws[i] + (ws[i + 1] - ws[i]) * (y - ys[i]) / (ys[i + 1] - ys[i]);
    return ws[6];
}

void jerseyPlastic(B& b, const P& p) {
    const double L = std::clamp(p("length"), 1.0, 30.0), w = p("wear");
    const int n = std::max(1, (int)std::lround(L / 2.0));
    const double gap = n > 1 ? 0.03 : 0.0;
    const V3 org = aged(V3(0.95, 0.40, 0.05), w), wht = aged(V3(0.92, 0.92, 0.9), w);
    const std::vector<double> cuts = {0.08, 0.3, 0.46, 0.6, 0.72};
    pieces(L, n, gap, [&](double xa, double xb, int i) {
        b.mat(MATTE);
        b.color(org);
        profileAlongX(b, bandPoly(plasticHalf, cuts, 0, 0.30), xa, xb);
        b.color(wht);
        profileAlongX(b, bandPoly(plasticHalf, cuts, 0.30, 0.48), xa, xb);
        b.color(org);
        profileAlongX(b, bandPoly(plasticHalf, cuts, 0.48, 0.64), xa, xb);
        b.color(wht);
        profileAlongX(b, bandPoly(plasticHalf, cuts, 0.64, 0.8), xa, xb);
        const double xm = (xa + xb) / 2;
        // Fill cap and drain plug.
        b.color(0.9, 0.9, 0.88);
        b.cyl(V3(xm, 0.8, 0), V3(xm, 0.822, 0), 0.045, 12);
        b.color(0.1, 0.1, 0.11);
        b.cyl(V3(xm, 0.822, 0), V3(xm, 0.832, 0), 0.028, 10);
        for (int sd = -1; sd <= 1; sd += 2) {
            b.color(0.1, 0.1, 0.11);
            b.cyl(V3(xm + 0.4, 0.08, sd * 0.304), V3(xm + 0.4, 0.08, sd * 0.318), 0.026, 8);  // drain plug
            // Moulded hand-hold slots in the foot.
            b.color(org * 0.65);
            b.box(V3(xm - 0.35, 0.12, sd * 0.294), V3(0.3, 0.07, 0.006));
        }
        // Interlocking end lugs.
        b.color(org * 0.85);
        for (int e = 0; e < 2; e++)
            if ((e == 0 && i > 0) || (e == 1 && i + 1 < n)) {
                const double x = e ? xb + gap / 2 : xa - gap / 2;
                b.box(V3(x, 0.55, 0), V3(gap + 0.06, 0.12, 0.2), 0.01);
            }
        if (n == 1)
            for (int e = -1; e <= 1; e += 2) b.box(V3(e * (xb - xa) / 2, 0.55, 0), V3(0.04, 0.12, 0.2), 0.01);
        // A reflector on the top slope.
        b.style(V3(1.0, 0.7, 0.05), EMISSIVE);
        b.box(V3(xm - 0.55, 0.7, 0.1432), V3(0.1, 0.05, 0.004));
    });
}

void jerseyMedian(B& b, const P& p) {
    const double L = std::clamp(p("length"), 1.0, 30.0), w = p("wear");
    const int n = std::max(1, (int)std::lround(L / 3.0));
    const double gap = n > 1 ? 0.014 : 0.0;
    // A taller, steeper-sided F-shape, steel cap on top.
    auto hw = [](double y) {
        if (y <= 0.07) return 0.30;
        if (y <= 0.30) return 0.30 - 0.09 * (y - 0.07) / 0.23;
        return 0.21 - 0.12 * (y - 0.30) / 0.42;
    };
    pieces(L, n, gap, [&](double xa, double xb, int i) {
        const double j = 0.92 + 0.12 * hash01(41, i);
        const V3 base = aged(kConcrete * V3(0.97, 0.97, 0.98), w * 0.4) * j;
        b.mat(CONCRETE);
        b.color(base * V3(0.8, 0.78, 0.74));
        profileAlongX(b, bandPoly(hw, {0.07, 0.30}, 0, 0.18), xa, xb);
        b.color(base * V3(0.95, 0.95, 0.95));
        profileAlongX(b, bandPoly(hw, {0.07, 0.30}, 0.18, 0.72), xa, xb);
        // The steel cap: an inverted channel over the top, in 2 m lengths.
        const double c0 = xa, c1 = xb;
        const int m = std::max(1, (int)std::lround((c1 - c0) / 2.0));
        const double cg = 0.01, cl = ((c1 - c0) - (m - 1) * cg) / m;
        for (int k = 0; k < m; k++) {
            const double ca = c0 + k * (cl + cg), cb = ca + cl;
            b.style(aged(kGalv, w * 0.5) * (0.94 + 0.1 * hash01(5, i * 4 + k)), METAL);
            profileAlongX(b, {V2(-0.145, 0.70), V2(0.145, 0.70), V2(0.14, 0.76), V2(0.11, 0.80), V2(-0.11, 0.80), V2(-0.14, 0.76)}, ca, cb);
            b.style(kSteelDk, METAL);
            for (int sd = -1; sd <= 1; sd += 2) b.box(V3((ca + cb) / 2, 0.76, sd * 0.1405), V3(0.035, 0.035, 0.004), 0.002);  // bolt heads
            b.style(V3(1.0, 0.7, 0.05), EMISSIVE);
            if (k == 0 && i % 2 == 0) b.box(V3((ca + cb) / 2, 0.755, 0.1425 * 0 + 0.1425), V3(0.09, 0.03, 0.004));  // an amber reflector on the cap side
        }
        // Base angle and dirt.
        b.style(V3(0.1, 0.1, 0.1), MATTE);
        for (int sd = -1; sd <= 1; sd += 2) b.box(V3((xa + xb) / 2, 0.035, sd * 0.2985), V3(0.3, 0.04, 0.005));
    });
}

Reg jerseyBarrier({"jersey_barrier", "JERSEY BARRIER", "TRAIL AND ROAD", "Concrete or plastic road barrier, a run of pieces along X", 4,
                   {
                       {"CONCRETE JERSEY", "Cast New-Jersey profile pieces with tie plates",
                        {num("length", "LENGTH", 4, 1, 24, 0.5), num("wear", "WEAR", 0.35, 0, 1)}, jerseyConcrete},
                       {"WATER-FILLED", "Orange and white plastic water-filled blocks",
                        {num("length", "LENGTH", 4, 1, 24, 0.5), num("wear", "WEAR", 0.25, 0, 1)}, jerseyPlastic},
                       {"STEEL-CAPPED MEDIAN", "Tall concrete median with a galvanised cap",
                        {num("length", "LENGTH", 4, 1, 24, 0.5), num("wear", "WEAR", 0.3, 0, 1)}, jerseyMedian},
                   }});


// ---- 9. Guardrail -----------------------------------------------------------------------------
//
// Runs along X, centred on the origin; the posts stand on z = 0 and the rail faces +Z (the
// traffic side). Posts every 2 m; each end turns down into the ground.

const double kRailY = 0.55;  // rail centre height

// The W-beam cross-section: the sheet's centre-line (y, z) from the bottom edge to the top edge.
const std::vector<V2>& wProfile() {
    static const std::vector<V2> w = {V2(-0.155, 0.006), V2(-0.125, 0.0), V2(-0.100, 0.083), V2(-0.056, 0.083), V2(-0.031, 0.0), V2(0.0, 0.0),
                                      V2(0.031, 0.0), V2(0.056, 0.083), V2(0.100, 0.083), V2(0.125, 0.0), V2(0.155, 0.006)};
    return w;
}

// One W-beam sheet over x in [xa, xb], its back plane at z = zBack, centre height yc (in the current frame).
void wSheet(B& b, double xa, double xb, double yc, double zBack) {
    const double t = 0.0045;
    Poly q;
    const auto& w = wProfile();
    for (const V2& s : w) q.push_back(V2(-(zBack + s.y), yc + s.x));
    for (int i = (int)w.size() - 1; i >= 0; i--) q.push_back(V2(-(zBack + w[i].y + t), yc + w[i].x));
    profileAlongX(b, q, xa, xb);
}

// The H-section steel post, web to the traffic.
void hPost(B& b, double x, double h, double zc = 0) {
    b.push();
    b.move(x, 0, zc);
    b.extrudeUp({V2(-0.075, -0.05), V2(-0.066, -0.05), V2(-0.066, -0.0035), V2(0.066, -0.0035), V2(0.066, -0.05), V2(0.075, -0.05), V2(0.075, 0.05),
                 V2(0.066, 0.05), V2(0.066, 0.0035), V2(-0.066, 0.0035), V2(-0.066, 0.05), V2(-0.075, 0.05)},
                h);
    b.pop();
}

// The standard run: posts every 2 m between the terminal turn-downs. `rail(xa, xb)` draws a straight
// stretch of rail at rail height; `post(x, h)` a post; `fix(x)` the hardware at a post. The turned-down
// ends are the same rail, pitched, so `rail` is called inside a pitched frame there.
template <class Rail, class Post, class Fix>
void railRun(B& b, double L, Rail rail, Post post, Fix fix, double backZ, double endLen = 2.0, double dropTo = 0.14) {
    const int n = std::max(2, (int)std::floor(L / 2.0 + 1e-6)) + 1;  // posts
    const double sp = (L - 2 * 0.0) / (n - 1);
    // Straight rail between the hinge points, sheets 2 spans long (a splice at every second post).
    const double xe = L / 2 - endLen;
    const int sheets = std::max(1, (int)std::lround(2 * xe / 4.0));
    const double sl = 2 * xe / sheets;
    for (int k = 0; k < sheets; k++) rail(-xe + k * sl + (k ? 0.004 : 0), -xe + (k + 1) * sl - (k + 1 < sheets ? 0.004 : 0));
    // The turned-down ends.
    const double ang = std::atan2(kRailY - dropTo, endLen) * 180 / M_PI;
    const double ln = endLen / std::cos(ang * M_PI / 180);
    for (int e = -1; e <= 1; e += 2) {
        b.push();
        b.move(e * xe, kRailY, 0);
        b.pitch(e > 0 ? -ang : ang);
        if (e > 0) {
            b.move(0, -kRailY, 0);
            rail(0.004, ln);
        } else {
            b.move(0, -kRailY, 0);
            rail(-ln, -0.004);
        }
        b.pop();
    }
    // Posts: full height along the run, shorter into the turn-down.
    for (int i = 0; i < n; i++) {
        const double x = -L / 2 + i * sp;
        const double ex = std::max(0.0, std::fabs(x) - xe);  // distance into the turn-down
        const double railAt = kRailY - ex * std::tan(ang * M_PI / 180);
        post(x, railAt + 0.12);
        fix(x, railAt);
    }
    (void)backZ;
}

void railGalv(B& b, const P& p) {
    const double L = std::clamp(p("length"), 6.0, 80.0), w = p("wear");
    const V3 galv = aged(kGalv, w * 0.6);
    const double zb = 0.21;  // the back plane of the rail
    railRun(
        b, L,
        [&](double xa, double xb) {
            b.style(galv * (0.96 + 0.08 * hash01(3, (uint32_t)(xa * 10 + 500))), METAL);
            wSheet(b, xa, xb, kRailY, zb);
        },
        [&](double x, double h) {
            b.style(aged(kSteelDk, w * 0.4) * 1.2, METAL);
            hPost(b, x, h);
            b.style(aged(kWoodMid, w), WOOD);
            b.box(V3(x, h - 0.2, 0.11), V3(0.1, 0.27, 0.205), 0.008);  // the blocking block
        },
        [&](double x, double railAt) {
            b.style(kSteelDk, METAL);
            b.cyl(V3(x, railAt, zb + 0.004), V3(x, railAt, zb + 0.012), 0.018, 6);  // bolt head at the centre valley
        },
        zb);
    // Terminal heads: a yellow end cap on each end of the turned-down rail.
    b.style(V3(0.92, 0.72, 0.08), PAINT);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * (L / 2 + 0.01), 0.14, zb + 0.04), V3(0.025, 0.28, 0.14), 0.006);
}

void railBox(B& b, const P& p) {
    const double L = std::clamp(p("length"), 6.0, 80.0), w = p("wear");
    const V3 cols[3] = {V3(0.40, 0.22, 0.12), V3(0.62, 0.64, 0.65), V3(0.12, 0.28, 0.17)};
    const V3 col = aged(cols[std::clamp(p.i("finish"), 0, 2)], w * 0.5);
    const double zc = 0.17;
    auto box = [&](double xa, double xb) {
        b.style(col * (0.95 + 0.1 * hash01(9, (uint32_t)(xa * 10 + 500))), METAL);
        b.box(V3((xa + xb) / 2, kRailY, zc), V3(xb - xa, 0.17, 0.13), 0.015);
        b.style(col * 0.8, METAL);  // the splice sleeve
        b.box(V3(xb, kRailY, zc), V3(0.26, 0.18, 0.14), 0.012);
    };
    railRun(
        b, L, box,
        [&](double x, double h) {
            b.style(col * 0.7, METAL);
            b.box(V3(x, h / 2 - 0.05, 0.0), V3(0.1, h - 0.1, 0.1), 0.01);
            b.box(V3(x, h - 0.2, 0.085), V3(0.08, 0.2, 0.1), 0.01);  // the stand-off bracket
        },
        [&](double, double) {}, zc);
    b.style(V3(0.92, 0.72, 0.08), PAINT);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * (L / 2 + 0.01), 0.14, zc), V3(0.025, 0.28, 0.2), 0.006);
}

void railTimber(B& b, const P& p) {
    const double L = std::clamp(p("length"), 6.0, 80.0), w = p("wear");
    const V3 wood = aged(kWoodMid, w), steelC = aged(kGalv, w * 0.5);
    const double zr = 0.17;  // the rail's rear face
    railRun(
        b, L,
        [&](double xa, double xb) {
            b.style(wood * (0.94 + 0.12 * hash01(4, (uint32_t)(xa * 10 + 500))), WOOD);
            b.box(V3((xa + xb) / 2, kRailY, zr + 0.06), V3(xb - xa, 0.28, 0.10), 0.01);  // the timber rail
            b.style(steelC, METAL);
            b.box(V3((xa + xb) / 2, kRailY, zr - 0.005), V3(xb - xa, 0.26, 0.008), 0.002);  // the steel backing strap
        },
        [&](double x, double h) {
            b.style(aged(kWoodOld, w), WOOD);
            b.lathe(V3(x, 0, 0), {V2(0.1, 0), V2(0.095, h + 0.1), V2(0.07, h + 0.16), V2(0, h + 0.18)}, 8);
            b.style(wood, WOOD);
            b.box(V3(x, kRailY, 0.1), V3(0.12, 0.30, 0.14), 0.01);  // block
        },
        [&](double x, double railAt) {
            b.style(kSteelDk, METAL);
            for (double dy : {-0.07, 0.07}) b.cyl(V3(x, railAt + dy, zr + 0.11), V3(x, railAt + dy, zr + 0.122), 0.02, 6);  // bolt heads and washers
        },
        zr);
    b.style(V3(0.92, 0.72, 0.08), PAINT);
    for (int e = -1; e <= 1; e += 2) b.box(V3(e * (L / 2 + 0.01), 0.14, zr + 0.06), V3(0.025, 0.28, 0.14), 0.006);
}

Reg guardrail({"guardrail", "GUARDRAIL", "TRAIL AND ROAD", "Roadside rail, posts every 2 m, traffic side toward +Z", 20,
               {
                   {"GALVANISED W-BEAM", "Galvanised W-beam on steel H posts and timber blocks",
                    {num("length", "LENGTH", 20, 6, 60, 2), num("wear", "WEAR", 0.3, 0, 1)}, railGalv},
                   {"STEEL BOX BEAM", "Hollow box-section rail on stand-off brackets",
                    {num("length", "LENGTH", 20, 6, 60, 2), choice("finish", "FINISH", 0, {"CORTEN", "GALVANISED", "GREEN"}), num("wear", "WEAR", 0.3, 0, 1)}, railBox},
                   {"TIMBER AND STEEL", "Round timber posts, steel-backed timber rail",
                    {num("length", "LENGTH", 20, 6, 60, 2), num("wear", "WEAR", 0.35, 0, 1)}, railTimber},
               }});

// ---- 10. Lamp post -------------------------------------------------------------------------

// A cobra-head luminaire centred (cx, cy) on the pole side of the arm; its long axis along X
// (nose toward +X when dir = 1), with the lens face underneath.
void cobraHead(B& b, double cx, double cy, double dir, const V3& body) {
    Poly plan = {V2(-0.45, -0.075), V2(-0.12, -0.13), V2(0.28, -0.17), V2(0.44, -0.12), V2(0.5, 0.0), V2(0.44, 0.12), V2(0.28, 0.17), V2(-0.12, 0.13), V2(-0.45, 0.075)};
    for (V2& q : plan) q.x *= dir;
    auto shifted2 = [&](Poly q, double s) {
        for (V2& v : q) v.x = v.x * s + cx, v.y *= s;
        return q;
    };
    b.style(body, PAINT);
    b.push();
    b.extrudeUp(shifted2(plan, 1.0), 0.12, cy - 0.06);
    b.extrudeUp(shifted2(plan, 0.72), 0.07, cy + 0.05);
    b.pop();
    b.style(V3(1.0, 0.86, 0.55), EMISSIVE);
    b.extrudeUp(shifted2(plan, 0.84), 0.012, cy - 0.072);
    b.style(V3(0.1, 0.1, 0.11), MATTE);
    b.cyl(V3(cx + dir * 0.1, cy + 0.12, 0), V3(cx + dir * 0.1, cy + 0.16, 0), 0.04, 8);  // photocell
}

void lampModern(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = aged(paint(p.i("paint")), w * 0.5);
    b.style(col, PAINT);
    b.lathe(V3(0, 0, 0), {V2(0.20, 0), V2(0.20, 0.07), V2(0.15, 0.13), V2(0.15, 0.55), V2(0.121, 0.68), V2(0.058, 9.6), V2(0.052, 9.85), V2(0, 9.9)}, 12);
    b.style(col * 0.6, PAINT);
    b.box(V3(0.149, 0.36, 0), V3(0.01, 0.34, 0.12), 0.004);  // hand-hole door
    b.style(kSteelDk, METAL);
    for (int k = 0; k < 4; k++) b.box(V3(std::cos(k * M_PI / 2 + 0.78) * 0.17, 0.02, std::sin(k * M_PI / 2 + 0.78) * 0.17), V3(0.04, 0.04, 0.04), 0.004);
    // The swept arm.
    b.style(col, PAINT);
    b.tube({V3(0, 9.3, 0), V3(0.14, 9.58, 0), V3(0.45, 9.82, 0), V3(1.0, 9.96, 0), V3(1.7, 10.0, 0), V3(2.25, 9.98, 0)}, 0.045, 8, false, true);
    cobraHead(b, 2.45, 9.93, 1, col * 1.05);
}

void lampTwin(B& b, const P& p) {
    const double w = p("wear");
    const V3 col = aged(paint(p.i("paint")), w * 0.5);
    b.style(col, PAINT);
    // Octagonal, in two stepped sections.
    b.lathe(V3(0, 0, 0), {V2(0.19, 0), V2(0.19, 0.08), V2(0.14, 0.14), V2(0.14, 0.5), V2(0.125, 0.56), V2(0.096, 5.6), V2(0.11, 5.62), V2(0.11, 5.75), V2(0.094, 5.8), V2(0.062, 9.82), V2(0.0, 9.88)}, 8, false);
    b.style(col * 0.9, PAINT);
    b.lathe(V3(0, 0, 0), {V2(0.1, 5.5), V2(0.118, 5.55), V2(0.118, 5.8), V2(0.1, 5.85)}, 8, false);  // the slip-joint collar
    b.style(kSteelDk, METAL);
    for (int k = 0; k < 4; k++) b.box(V3(std::cos(k * M_PI / 2 + 0.78) * 0.17, 0.02, std::sin(k * M_PI / 2 + 0.78) * 0.17), V3(0.04, 0.04, 0.04), 0.004);
    for (int e = -1; e <= 1; e += 2) {
        b.style(col, PAINT);
        // A straight arm rising to the head, with a brace beneath.
        b.beam(V3(e * 0.05, 9.4, 0), V3(e * 2.2, 10.0, 0), 0.09, 0.09, V3(0, 1, 0), 0.01);
        b.beam(V3(e * 0.05, 9.0, 0), V3(e * 1.3, 9.78, 0), 0.05, 0.05, V3(0, 1, 0), 0.006);
        // A flat LED luminaire.
        b.style(col * 1.05, PAINT);
        b.box(V3(e * 2.45, 10.04, 0), V3(0.95, 0.12, 0.42), 0.04);
        b.box(V3(e * 2.45, 10.12, 0), V3(0.7, 0.03, 0.3), 0.01);
        b.style(V3(1.0, 0.95, 0.8), EMISSIVE);
        b.box(V3(e * 2.45, 9.975, 0), V3(0.85, 0.012, 0.32));
    }
}

void lampWood(B& b, const P& p) {
    const double w = p("wear");
    const V3 pole = aged(V3(0.36, 0.27, 0.17), w);
    b.style(pole, WOOD);
    b.lathe(V3(0, 0, 0), {V2(0.135, 0), V2(0.128, 0.3), V2(0.108, 5.0), V2(0.092, 9.8), V2(0.075, 10.0), V2(0, 10.04)}, 9);
    b.style(V3(0.10, 0.07, 0.05), MATTE);  // creosoted foot
    b.lathe(V3(0, 0, 0), {V2(0.137, 0), V2(0.131, 0.3), V2(0.12, 1.4)}, 9, false);
    // The cross arm, braced both ways.
    b.style(aged(kWoodNew, w), WOOD);
    b.box(V3(0, 9.55, 0), V3(0.12, 0.14, 0.16), 0.01);  // (the arm runs along X)
    b.box(V3(0, 9.55, 0), V3(2.3, 0.1, 0.1), 0.008);
    for (int e = -1; e <= 1; e += 2) b.beam(V3(e * 0.05, 9.0, 0), V3(e * 0.7, 9.5, 0), 0.07, 0.07, V3(0, 1, 0), 0.006);
    // Pins and glass insulators on top of the arm.
    for (int k = -2; k <= 2; k += 2) {
        b.style(V3(0.35, 0.25, 0.16), WOOD);
        b.cyl(V3(k * 0.5, 9.6, 0), V3(k * 0.5, 9.7, 0), 0.016, 6);
        b.style(V3(0.55, 0.78, 0.7), GLASS);
        b.lathe(V3(k * 0.5, 9.68, 0), {V2(0.04, 0), V2(0.05, 0.03), V2(0.035, 0.07), V2(0.025, 0.1), V2(0, 0.11)}, 8);
    }
    // Steel bolts through the arm and pole.
    b.style(kSteelDk, METAL);
    b.cyl(V3(0.105, 9.55, 0), V3(0.12, 9.55, 0), 0.02, 6);
    // The lamp: a bracket pipe out to +X, ending in an enamel shade over a bulb.
    b.tube({V3(0.05, 9.2, 0), V3(0.5, 9.2, 0), V3(1.4, 9.3, 0), V3(2.1, 9.55, 0), V3(2.45, 9.5, 0), V3(2.5, 9.38, 0)}, 0.03, 6, false, true);
    b.beam(V3(0.1, 9.05, 0), V3(1.0, 9.26, 0), 0.03, 0.03, V3(0, 1, 0), 0.004);
    b.style(p.i("shade") ? V3(0.12, 0.28, 0.18) : V3(0.20, 0.20, 0.21), PAINT);
    b.lathe(V3(2.5, 9.0, 0), {V2(0.0, 0.40), V2(0.07, 0.40), V2(0.22, 0.28), V2(0.36, 0.16), V2(0.38, 0.13), V2(0.34, 0.14), V2(0.2, 0.25), V2(0.0, 0.33)}, 16);
    b.style(V3(0.95, 0.95, 0.9), PAINT);  // the white-enamel underside
    b.lathe(V3(2.5, 9.0, 0), {V2(0.34, 0.14), V2(0.2, 0.25), V2(0.0, 0.33)}, 16);
    b.style(V3(1.0, 0.88, 0.6), EMISSIVE);
    b.sphere(V3(2.5, 9.2, 0), V3(0.075, 0.1, 0.075), 1);
}

Reg lampPost({"lamp_post", "LAMP POST", "TRAIL AND ROAD", "Freeway street light: pole on the origin, arm and lamp toward +X", 10,
              {
                  {"TAPERED COBRA", "Tapered steel pole, swept arm, cobra-head lamp",
                   {paintParam(9), num("wear", "WEAR", 0.3, 0, 1)}, lampModern},
                  {"TWIN ARM", "Octagonal pole, a straight arm and LED head each way",
                   {paintParam(9), num("wear", "WEAR", 0.3, 0, 1)}, lampTwin},
                  {"WOODEN POLE", "Old timber pole with a cross arm and an enamel-shade lamp",
                   {num("wear", "WEAR", 0.4, 0, 1), choice("shade", "SHADE", 1, {"GREY", "GREEN"})}, lampWood},
              }});

// ---- 11. Sign gantry ------------------------------------------------------------------------

const char* kSignTop[4] = {"CRUMPLE CITY", "FREEWAY", "EXIT 4", "BACKCOUNTRY"};
const char* kSignBot[4] = {"12", "EAST", "FRONTIER", "30"};
const std::vector<std::string> kSignNames = {"CRUMPLE CITY 12", "FREEWAY EAST", "EXIT 4 FRONTIER", "BACKCOUNTRY 30"};

// A green freeway sign facing +X, back face at xb, centre (yc, zc).
void greenBoard(B& b, double xb, double yc, double zc, double bw, double bh, int sign) {
    sign = ((sign % 4) + 4) % 4;
    b.style(V3(0.38, 0.40, 0.41), METAL);
    b.box(V3(xb + 0.03, yc, zc), V3(0.06, bh, bw), 0.02);
    b.style(kSignGreen, MATTE);
    b.box(V3(xb + 0.0675, yc, zc), V3(0.015, bh - 0.05, bw - 0.05), 0.006);
    const double xf = xb + 0.075;
    b.style(V3(0.93, 0.93, 0.9), MATTE);
    const double m = 0.11, t = 0.065;  // inset and width of the border
    b.box(V3(xf + 0.002, yc + bh / 2 - m, zc), V3(0.005, t, bw - 2 * m + t), 0.0);
    b.box(V3(xf + 0.002, yc - bh / 2 + m, zc), V3(0.005, t, bw - 2 * m + t), 0.0);
    for (int s = -1; s <= 1; s += 2) b.box(V3(xf + 0.002, yc, zc + s * (bw / 2 - m)), V3(0.005, bh - 2 * m - t, t), 0.0);
    // The lettering.
    const int c1 = (int)std::strlen(kSignTop[sign]), c2 = (int)std::strlen(kSignBot[sign]);
    const int cm = std::max(c1, c2);
    const double px = std::min((bh - 0.5) * 0.9 / 17, (bw - 0.6) / (6.0 * cm - 1));
    b.letters(kSignTop[sign], V3(xf, yc + 5 * px, zc), px, 0.012);
    b.letters(kSignBot[sign], V3(xf, yc - 5 * px, zc), px, 0.012);
    (void)c2;
}

struct BoardLayout {
    double bw, bh;
    std::vector<double> z;
};
BoardLayout boards(double span, int count, double bwMax = 10.5) {
    BoardLayout L;
    L.bh = 2.6;
    L.bw = std::clamp(span * (count == 1 ? 0.30 : 0.26), 5.0, bwMax);
    if (count == 1) L.z = {0.0};
    else L.z = {-span * 0.25, span * 0.25};
    return L;
}

void gantryTruss(B& b, const P& p) {
    const double span = std::clamp(p("span"), 12.0, 60.0), a = span / 2;
    const int sign = p.i("sign"), cnt = p.i("boards") ? 2 : 1;
    const double s = 1.0, d = 1.3, yb = 6.65, dx = 1.0, c = 0.11;
    const V3 steel = aged(kGalv * 0.95, p("wear") * 0.5);
    b.style(steel, METAL);
    const int bays = std::max(4, (int)std::lround((span + s) / 2.0));
    trussZ(b, -a - s / 2, a + s / 2, yb, d, dx, c, bays);
    const int vb = std::max(5, (int)std::lround((yb + d) / 1.2));
    for (int sd = -1; sd <= 1; sd += 2) {
        trussY(b, 0, sd * a, s, yb + d, c, vb);
        b.style(kSteelDk, METAL);
        b.box(V3(0, 0.02, sd * a), V3(s + 0.4, 0.04, s + 0.4), 0.006);
        b.style(steel, METAL);
    }
    // A catwalk rail along the top.
    b.style(kSteelDk, METAL);
    b.box(V3(0.55, yb + d + 0.5, 0), V3(0.04, 0.04, span), 0.005);
    for (int k = 0; k <= bays; k++) b.box(V3(0.55, yb + d + 0.25, -a - s / 2 + 0.5 + k * ((span + s - 1.0) / bays)), V3(0.04, 0.5, 0.04), 0.003);
    const BoardLayout L = boards(span, cnt);
    for (size_t k = 0; k < L.z.size(); k++) {
        const double zc = L.z[k], yc = 7.45;
        greenBoard(b, dx / 2 + 0.07, yc, zc, L.bw, L.bh, sign + (int)k);
        b.style(kSteelDk, METAL);
        for (int e = -1; e <= 1; e += 2)
            for (double f : {-0.3, 0.3}) b.box(V3(dx / 2 + 0.035, yc + f * 0.9, zc + e * L.bw * 0.38), V3(0.07, 0.05, 0.08), 0.004);
    }
}

void gantryTube(B& b, const P& p) {
    const double span = std::clamp(p("span"), 12.0, 60.0), a = span / 2;
    const int sign = p.i("sign"), cnt = p.i("boards") ? 2 : 1;
    const double y0 = 7.3, R = 0.8, r = 0.3;
    const V3 col = aged(paint(p.i("paint")), p("wear") * 0.5);
    std::vector<V3> pts;
    pts.push_back(V3(0, 0.0, -a));
    pts.push_back(V3(0, (y0 - R) * 0.5, -a));
    for (int i = 0; i <= 6; i++) {
        const double t = M_PI / 2 * i / 6;
        pts.push_back(V3(0, y0 - R + R * std::sin(t), -a + R - R * std::cos(t)));
    }
    const int mid = std::max(2, (int)std::ceil((span - 2 * R) / 6.0));
    for (int i = 1; i < mid; i++) pts.push_back(V3(0, y0, -a + R + (span - 2 * R) * i / mid));
    for (int i = 0; i <= 6; i++) {
        const double t = M_PI / 2 * i / 6;
        pts.push_back(V3(0, y0 - R + R * std::cos(t), a - R + R * std::sin(t)));
    }
    pts.push_back(V3(0, (y0 - R) * 0.5, a));
    pts.push_back(V3(0, 0.0, a));
    b.style(col, PAINT);
    b.tube(pts, r, 14, false, true);
    for (int sd = -1; sd <= 1; sd += 2) {
        b.style(kSteelDk, METAL);
        b.cyl(V3(0, 0, sd * a), V3(0, 0.07, sd * a), 0.62, 14);
        b.style(col * 0.9, PAINT);
        b.cone(V3(0, 0.07, sd * a), V3(0, 0.5, sd * a), 0.46, r, 14, false);  // the base cover
        b.style(kSteelDk, METAL);
        for (int k = 0; k < 4; k++) b.cyl(V3(std::cos(k * M_PI / 2 + 0.78) * 0.5, 0.07, sd * a + std::sin(k * M_PI / 2 + 0.78) * 0.5), V3(std::cos(k * M_PI / 2 + 0.78) * 0.5, 0.17, sd * a + std::sin(k * M_PI / 2 + 0.78) * 0.5), 0.035, 6);
    }
    // A ladder up the back of one leg.
    b.style(kSteelDk, METAL);
    for (int e = -1; e <= 1; e += 2) b.box(V3(-0.36, 3.2, a + e * 0.2), V3(0.04, 5.2, 0.04), 0.003);
    for (int k = 0; k < 14; k++) b.box(V3(-0.36, 1.2 + k * 0.38, a), V3(0.04, 0.03, 0.4), 0.003);
    // The boards, hung from the tube on U-bolts.
    const BoardLayout L = boards(span, cnt, 11.5);
    for (size_t k = 0; k < L.z.size(); k++) {
        const double zc = L.z[k], yc = 7.4;
        greenBoard(b, r + 0.14, yc, zc, L.bw, L.bh, sign + (int)k);
        b.style(kSteelDk, METAL);
        for (int e = -1; e <= 1; e += 2)
            for (double f : {-0.25, 0.25}) b.box(V3(r + 0.07, y0 + f * 0.0 + (f < 0 ? -0.08 : 0.08), zc + e * L.bw * 0.36), V3(0.14, 0.05, 0.1), 0.004);
    }
}

// A tapered box truss along Z from zRoot (deep) to zTip (shallow).
void taperTruss(B& b, double zRoot, double zTip, double yb, double dRoot, double dTip, double dx, double c, int n) {
    auto D = [&](double f) { return dRoot + (dTip - dRoot) * f; };
    auto Z = [&](double f) { return zRoot + (zTip - zRoot) * f; };
    const double c2 = c * 0.55;
    for (int i = 0; i < n; i++) {
        const double f0 = (double)i / n, f1 = (double)(i + 1) / n;
        for (int sx = -1; sx <= 1; sx += 2) {
            b.beam(V3(sx * dx / 2, yb, Z(f0)), V3(sx * dx / 2, yb, Z(f1)), c, c, V3(0, 1, 0), 0.004);
            b.beam(V3(sx * dx / 2, yb + D(f0), Z(f0)), V3(sx * dx / 2, yb + D(f1), Z(f1)), c, c, V3(0, 1, 0), 0.004);
            const bool up = i % 2 == 0;
            b.beam(V3(sx * dx / 2, yb + (up ? 0 : D(f0)), Z(f0)), V3(sx * dx / 2, yb + (up ? D(f1) : 0), Z(f1)), c2, c2);
        }
        b.beam(V3(-dx / 2, yb + D(f1), Z(f0)), V3(dx / 2, yb + D(f1), Z(f1)), c2, c2);
        b.beam(V3(dx / 2, yb, Z(f0)), V3(-dx / 2, yb, Z(f1)), c2, c2);
    }
    for (int i = 0; i <= n; i++) {
        const double f = (double)i / n;
        for (int sx = -1; sx <= 1; sx += 2) b.box(V3(sx * dx / 2, yb + D(f) / 2, Z(f)), V3(c2, D(f), c2));
    }
}

void gantryCantilever(B& b, const P& p) {
    const double span = std::clamp(p("span"), 12.0, 60.0), a = span / 2;
    const int sign = p.i("sign"), cnt = p.i("boards") ? 2 : 1;
    const double yb = 6.5, dx = 0.8, c = 0.12;
    const V3 col = aged(paint(p.i("paint")), p("wear") * 0.5);
    b.style(col, METAL);
    // The column: a tapering tube, flanged at the foot.
    b.lathe(V3(0, 0, a), {V2(0.5, 0.0), V2(0.5, 0.12), V2(0.44, 0.2), V2(0.43, 0.9), V2(0.3, 8.6), V2(0.0, 8.7)}, 14);
    b.style(kSteelDk, METAL);
    b.cyl(V3(0, 0.0, a), V3(0, 0.12, a), 0.75, 14);
    for (int k = 0; k < 6; k++) b.cyl(V3(std::cos(k * 1.047) * 0.6, 0.12, a + std::sin(k * 1.047) * 0.6), V3(std::cos(k * 1.047) * 0.6, 0.24, a + std::sin(k * 1.047) * 0.6), 0.035, 6);
    // The arm: a tapering truss reaching across.
    b.style(col, METAL);
    const int bays = std::max(6, (int)std::lround(span / 2.0));
    taperTruss(b, a - 0.2, -a, yb, 2.0, 0.7, dx, c, bays);
    // Knee braces from the column to the arm.
    for (int e = -1; e <= 1; e += 2) b.beam(V3(e * 0.28, 4.9, a - 0.2), V3(e * 0.28, yb, a - 0.2 - std::min(9.0, span * 0.3)), 0.16, 0.16, V3(0, 1, 0), 0.008);
    // The boards on the arm.
    const BoardLayout L = boards(span, cnt);
    for (size_t k = 0; k < L.z.size(); k++) {
        const double zc = cnt == 1 ? -a * 0.38 : L.z[k] - span * 0.07, yc = 7.4;
        greenBoard(b, dx / 2 + 0.07, yc, zc, L.bw, L.bh, sign + (int)k);
        b.style(kSteelDk, METAL);
        for (int e = -1; e <= 1; e += 2)
            for (double f : {-0.3, 0.3}) b.box(V3(dx / 2 + 0.035, yc + f * 0.9, zc + e * L.bw * 0.38), V3(0.07, 0.05, 0.08), 0.004);
    }
}

Reg signGantry({"sign_gantry", "SIGN GANTRY", "TRAIL AND ROAD", "Overhead freeway sign bridge spanning along Z, signs facing +X", 38,
                {
                    {"STEEL TRUSS", "Truss legs and a truss bridge carrying two signs",
                     {num("span", "SPAN", 37.8, 12, 60, 0.1), choice("sign", "SIGN", 0, kSignNames), choice("boards", "BOARDS", 1, {"ONE", "TWO"}), num("wear", "WEAR", 0.3, 0, 1)}, gantryTruss},
                    {"TUBE PORTAL", "A single round tube bent into a portal, signs on U-bolts",
                     {num("span", "SPAN", 37.8, 12, 60, 0.1), choice("sign", "SIGN", 0, kSignNames), choice("boards", "BOARDS", 1, {"ONE", "TWO"}), paintParam(9), num("wear", "WEAR", 0.3, 0, 1)}, gantryTube},
                    {"CANTILEVER", "One tall column with a tapering truss arm",
                     {num("span", "SPAN", 37.8, 12, 60, 0.1), choice("sign", "SIGN", 0, kSignNames), choice("boards", "BOARDS", 0, {"ONE", "TWO"}), paintParam(9), num("wear", "WEAR", 0.3, 0, 1)},
                     gantryCantilever},
                }});

}  // namespace
