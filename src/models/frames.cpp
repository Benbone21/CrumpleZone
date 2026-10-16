// Frames: the pieces of a body-on-frame truck's ladder frame (carframe.cpp places them on the car's
// frame nodes, so a bent frame draws bent): the rail section, a crossmember, a spring hanger, a
// shackle mount, a shock tower, a stay (outrigger), a body mount, a recovery point, a skid plate and
// the jack pad.
//
// Every one is built for the car's LEFT side (+Z is outboard) with its own origin where it bolts on;
// "mir" = 1 builds it for the right (carframe.cpp sets it). Sizes come in as params (the car's rail
// section, the gap between its rails) so a part is drawn to the frame it sits on.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

const V3 kSteel(0.56, 0.57, 0.58);
const V3 kBolt(0.30, 0.30, 0.32);
const V3 kRust(0.36, 0.17, 0.08);
const V3 kRubber(0.05, 0.05, 0.055);
const V3 kWeld(0.20, 0.19, 0.18);
const V3 kHiVis(0.88, 0.66, 0.08);

// Frame coatings: what the rails and brackets are painted (worn toward rust by `wear`).
const std::vector<std::string>& coatNames() {
    static const std::vector<std::string> n = {"BLACK", "DARK GREY", "RUSTY BLACK", "PRIMER GREY"};
    return n;
}
V3 coat(const P& p) {
    static const V3 c[4] = {V3(0.050, 0.050, 0.055), V3(0.135, 0.14, 0.145), V3(0.075, 0.055, 0.05), V3(0.25, 0.25, 0.26)};
    const V3 base = c[std::clamp(p.i("coat"), 0, 3)];
    return mix(base, kRust * 0.7 + base * 0.3, std::clamp(p("wear"), 0.0, 1.0) * 0.45);
}
Param coatParam(int def = 0) { return choice("coat", "COATING", def, coatNames()); }
Param wearParam(double def = 0.2) { return num("wear", "WEAR", def, 0, 1); }

// A bolt head or rivet: a short cylinder standing out of a face along `dir`.
void rivet(B& b, const V3& at, const V3& dir, double r = 0.008, double h = 0.005) {
    const V3 keep = b.curColor();
    const Mat km = b.curMat();
    b.style(kBolt, METAL);
    b.cyl(at, at + norm(dir) * h, r, 5);
    b.style(keep, km);
}

// A part fitted in a car (carframe.cpp sets "bare") is built about its own origin; shown alone (the lab) it
// is lifted to stand on the ground, with a stub of the rail it bolts to.
bool bare(const P& p) { return p("bare") > 0.5; }
void labLift(B& b, const P& p, double dy) {
    if (!bare(p)) b.move(0, dy, 0);
}
void labRail(B& b, const P& p, const V3& at, double L, double h, double w) {
    if (bare(p)) return;
    b.style(coat(p) * 1.4, PAINT);
    b.box(at, V3(L, h, w), 0.01);
}

// Builds for the right side when "mir" is set.
void side(B& b, const P& p) {
    if (p("mir") > 0.5) b.mirrorZ();
}

// A flat plate with rounded corners, lying in the X/Z plane (thickness up), centred at c.
void plate(B& b, const V3& c, double lx, double lz, double t, double r = 0.012) {
    b.push();
    b.move(c.x, c.y - t / 2, c.z);
    b.extrudeUp(roundedRect(lx, lz, std::min(r, std::min(lx, lz) * 0.4), 2), t);
    b.pop();
}

// ======================================================================================================
// 1. FRAME RAIL: one station of a rail (0.42 m), centred on the origin. Joined end to end by the car.
// ======================================================================================================

constexpr double kRailLen = 0.42;

// A boxed rail: two stamped halves welded into a tube, the seam beaded along the lower outside edge,
// a swage down the web, the odd weld spot.
void railBoxed(B& b, const P& p) {
    side(b, p);
    labLift(b, p, p("h") * 0.5);
    const double h = p("h"), w = p("w"), L = kRailLen;
    const V3 c = coat(p);
    b.style(c, PAINT);
    b.box(V3(0, 0, 0), V3(L, h, w), std::min(0.012, w * 0.2));
    // The seam: a weld bead along the outside lower edge.
    b.style(mix(c, kWeld, 0.6), MATTE);
    b.box(V3(0, -h * 0.5 + 0.018, w * 0.5 + 0.0012), V3(L, 0.008, 0.003));
    // A stamped swage down the web, and weld spots at the ends of it.
    b.style(c * 1.18, PAINT);
    b.box(V3(0, h * 0.06, w * 0.5 + 0.0012), V3(L * 0.62, 0.022, 0.003));
    b.style(mix(c, kWeld, 0.7), MATTE);
    for (int s = -1; s <= 1; s += 2)
        for (int u = -1; u <= 1; u += 2) b.box(V3(s * L * 0.38, u * h * 0.3, w * 0.5 + 0.001), V3(0.008, 0.008, 0.0026));
    // A rusty scab or two when it's worn.
    const int scabs = (int)std::lround(p("wear") * 4);
    b.style(kRust, MATTE);
    for (int i = 0; i < scabs; i++) {
        const double u = hash01(11, i), v = hash01(12, i);
        b.box(V3((u - 0.5) * L * 0.8, (v - 0.5) * h * 0.7, w * 0.5 + 0.0016), V3(0.02 + 0.02 * u, 0.014 + 0.016 * v, 0.0016));
    }
}

// An open C-channel rail: a web on the outside, a flange top and bottom reaching inboard, riveted
// crossmembers' gussets landing on the web. Open side toward the middle of the truck.
void railChannel(B& b, const P& p) {
    side(b, p);
    labLift(b, p, p("h") * 0.5);
    const double h = p("h"), w = p("w"), L = kRailLen, t = 0.0055, lip = 0.016;
    const V3 c = coat(p);
    b.style(c, PAINT);
    b.box(V3(0, 0, w * 0.5 - t * 0.5), V3(L, h, t));  // web (outboard)
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, s * (h * 0.5 - t * 0.5), 0), V3(L, t, w));  // flanges
        b.box(V3(0, s * (h * 0.5 - lip * 0.5), -w * 0.5 + t * 0.5), V3(L, lip, t));  // the lip turned in on each
    }
    // Rivets along the web's edges, in the heavy 1970s way.
    for (int s = -1; s <= 1; s += 2)
        for (int i = -2; i <= 2; i++) rivet(b, V3(i * 0.085, s * (h * 0.5 - 0.022), w * 0.5), V3(0, 0, 1), 0.0065, 0.004);
    // The web's lightening hole: a darker oval.
    b.style(c * 0.25, MATTE);
    b.push();
    b.move(0, 0, w * 0.5 + 0.0009);
    b.extrude(circle(0.04, 8), 0.0012, 0);
    b.pop();
    const int scabs = (int)std::lround(p("wear") * 4);
    b.style(kRust, MATTE);
    for (int i = 0; i < scabs; i++) {
        const double u = hash01(21, i), v = hash01(22, i);
        b.box(V3((u - 0.5) * L * 0.8, (v - 0.5) * h * 0.6, w * 0.5 + 0.0015), V3(0.02 + 0.02 * u, 0.012 + 0.018 * v, 0.0016));
    }
}

Reg frameRail({"frame_rail", "FRAME RAIL", "PARTS", "A truck's ladder frame rail, one 0.42 m station (carframe.cpp strings them along the frame's nodes; h and w are the car's section)", 0.5,
               {
                   {"BOXED RAIL", "Welded box section: seam bead, a swage down the web", {num("h", "HEIGHT", 0.18, 0.1, 0.3, 0.005), num("w", "WIDTH", 0.08, 0.05, 0.14, 0.005), coatParam(), wearParam()}, railBoxed},
                   {"C-CHANNEL RAIL", "Open channel, web out, riveted, a lightening hole", {num("h", "HEIGHT", 0.18, 0.1, 0.3, 0.005), num("w", "WIDTH", 0.065, 0.05, 0.14, 0.005), coatParam(), wearParam()}, railChannel},
               }});

// ======================================================================================================
// 2. CROSSMEMBER: between the rails at one station. Centred on the origin: X fore and aft, Z across the
// span, Y the middle of the rails' height. `span` is the gap between the rails' inside faces, `h` the rail.
// ======================================================================================================

// A gusset plate that laps a rail's inside face at one end, riveted: in the plane x = 0, `z0` the rail's
// face, reaching inboard `reach`, `hh` tall.
void gusset(B& b, double z0, double reach, double hh, double thick, double x, const V3& col) {
    b.style(col, PAINT);
    const double zs = z0 > 0 ? -1 : 1;  // inboard
    b.box(V3(x, 0, z0 + zs * reach * 0.5), V3(thick, hh, reach + 0.012));
    for (int r = -1; r <= 1; r += 2)
        for (int u = 0; u < 2; u++) rivet(b, V3(x + thick * 0.5, r * hh * 0.32, z0 + zs * (0.025 + u * 0.05)), V3(1, 0, 0), 0.0065, 0.004);
}

// A flat steel channel across, open to the rear, riveted gussets at both ends.
void crossChannel(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.06);
    const double span = p("span"), dp = 0.11, dx = 0.075, t = 0.005;
    const V3 c = coat(p);
    b.style(c, PAINT);
    const double len = span + 0.016;  // a little into each rail
    b.box(V3(dx * 0.5 - t * 0.5, 0, 0), V3(t, dp, len));  // the web, forward
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, s * (dp * 0.5 - t * 0.5), 0), V3(dx, t, len));  // flanges, reaching aft
    for (int s = -1; s <= 1; s += 2) gusset(b, s * span * 0.5, 0.11, dp + 0.05, 0.005, dx * 0.5 + 0.003, c * 1.08);
    b.style(c * 0.3, MATTE);
    b.box(V3(dx * 0.5 + 0.0012, 0, 0), V3(0.001, 0.045, 0.16));  // a lightening slot in the web
}

// A round tube with a pressed gusset plate at each end (the tube: seam-welded, hydroformed look).
void crossTube(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.06);
    const double span = p("span"), r = p("rad");
    const V3 c = coat(p);
    b.style(c, PAINT);
    b.cyl(V3(0, 0, -span * 0.5 - 0.01), V3(0, 0, span * 0.5 + 0.01), r, 14);
    // The seam, along the front of the tube.
    b.style(mix(c, kWeld, 0.6), MATTE);
    b.box(V3(r * 0.97, 0, 0), V3(0.004, 0.006, span * 0.9));
    for (int s = -1; s <= 1; s += 2) {
        // The end plate: a gusset in the X/Y plane bolted to the rail's inside face, with a collar round the tube.
        b.style(c * 1.1, PAINT);
        const double z = s * (span * 0.5 - 0.006);
        b.extrude(roundedRect(0.16, 0.12, 0.03, 3), 0.007, z);
        b.style(c, PAINT);
        b.cyl(V3(0, 0, z - s * 0.006), V3(0, 0, z - s * 0.03), r + 0.008, 14);
        for (int k = 0; k < 4; k++) rivet(b, V3((k & 1 ? 1 : -1) * 0.055, (k & 2 ? 1 : -1) * 0.04, z + s * 0.0035), V3(0, 0, s), 0.007, 0.005);
    }
}

// The transmission crossmember: dropped in the middle under the gearbox, a mount pad on it, legs up to the rails.
void crossDrop(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.2);
    const double span = p("span"), h = p("h"), drop = 0.10, dp = 0.09, dx = 0.08, t = 0.006;
    const V3 c = coat(p);
    const double yTop = -h * 0.5 + dp * 0.5 + 0.03;  // the legs meet the rails near their lower half
    const double yLow = yTop - drop;
    b.style(c, PAINT);
    const double mid = span * 0.5 - 0.17;
    // The dropped middle, and a leg sloping up at each end.
    b.box(V3(0, yLow, 0), V3(dx, dp, mid * 2));
    for (int s = -1; s <= 1; s += 2) {
        b.beam(V3(0, yLow, s * mid), V3(0, yTop, s * (span * 0.5 + 0.005)), dx, dp, V3(1, 0, 0), 0.005);
        gusset(b, s * span * 0.5, 0.075, dp + 0.05, 0.005, dx * 0.5 + 0.003, c * 1.08);
    }
    // The mount pad: a rubber isolator on a plate, two studs for the gearbox's tail mount.
    b.style(c * 1.2, PAINT);
    plate(b, V3(0, yLow + dp * 0.5 + 0.004, 0), 0.12, 0.13, 0.008, 0.02);
    b.style(kRubber, MATTE);
    b.cyl(V3(0, yLow + dp * 0.5 + 0.008, 0), V3(0, yLow + dp * 0.5 + 0.045, 0), 0.04, 12);
    b.style(kSteel * 0.7, METAL);
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(0, yLow + dp * 0.5 + 0.04, s * 0.05), V3(0, yLow + dp * 0.5 + 0.075, s * 0.05), 0.007, 6);
    b.style(c * 0.9, PAINT);
    for (int s = -1; s <= 1; s += 2) rivet(b, V3(dx * 0.5, yLow + 0.02, s * (mid - 0.03)), V3(1, 0, 0), 0.007, 0.004);
    (void)t;
}

Reg frameCross({"frame_cross", "CROSSMEMBER", "PARTS", "A ladder frame crossmember between the rails (carframe.cpp: span is the gap between the rails' inside faces)", 0.9,
                {
                    {"CHANNEL", "Flat riveted channel with gusset plates at both ends", {num("span", "SPAN", 0.80, 0.5, 1.2, 0.01), num("h", "RAIL HEIGHT", 0.18, 0.1, 0.3, 0.005), coatParam(), wearParam()}, crossChannel},
                    {"TUBE", "Seam-welded round tube, pressed end plates", {num("span", "SPAN", 0.80, 0.5, 1.2, 0.01), num("rad", "TUBE RADIUS", 0.036, 0.025, 0.05, 0.002), num("h", "RAIL HEIGHT", 0.18, 0.1, 0.3, 0.005), coatParam(), wearParam()}, crossTube},
                    {"DROPPED", "Gearbox crossmember: dropped middle, mount pad, legs up to the rails", {num("span", "SPAN", 0.80, 0.5, 1.2, 0.01), num("h", "RAIL HEIGHT", 0.18, 0.1, 0.3, 0.005), coatParam(), wearParam()}, crossDrop},
                }});

// ======================================================================================================
// 3-4. SPRING HANGER and SHACKLE MOUNT: a bracket hung under the rail round a pin. Origin: the pin's
// centre; the bracket reaches UP `rise` to the rail's underside. `gap` is the width between its cheeks
// (a leaf's width and the leaf's own hanger plates).
// ======================================================================================================

// The cheeks, a mounting flange across the top and the pin through it all.
void bracketCheeks(B& b, const P& p, const V3& c, double rise, double gap, double xHalf, double tilt) {
    const double t = 0.009;
    const double zc = gap * 0.5 + t * 0.5;
    b.style(c, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.push();
        b.move(tilt * 0.0, 0, 0);
        std::vector<V2> poly = {V2(-xHalf + tilt, rise), V2(xHalf + tilt, rise), V2(xHalf * 0.6, 0.02), V2(0.036, 0.0), V2(0.022, -0.026), V2(-0.022, -0.026), V2(-0.036, 0.0), V2(-xHalf * 0.6, 0.02)};
        b.extrude(poly, t, s * zc);
        b.pop();
    }
    // The flange that bolts up under the rail.
    b.style(c * 1.1, PAINT);
    b.box(V3(tilt * 0.5, rise + 0.005, 0), V3(xHalf * 2 + 0.03, 0.01, gap + 2 * t + 0.045));
    // The pin, its nut and its head.
    b.style(kSteel * 0.8, METAL);
    b.cyl(V3(0, 0, -gap * 0.5 - t - 0.012), V3(0, 0, gap * 0.5 + t + 0.012), 0.0095, 8);
    b.style(kBolt, METAL);
    b.box(V3(0, 0, gap * 0.5 + t + 0.014), V3(0.026, 0.026, 0.01));
    b.cyl(V3(0, 0, -gap * 0.5 - t - 0.012), V3(0, 0, -gap * 0.5 - t - 0.02), 0.016, 6);
    // Rivets across the flange.
    for (int s = -1; s <= 1; s += 2)
        for (int k = -1; k <= 1; k += 2) rivet(b, V3(tilt * 0.5 + k * xHalf * 0.8, rise - 0.0, s * (gap * 0.5 + t + 0.02)), V3(0, -1, 0), 0.007, 0.005);
    (void)p;
}

// The pressed bracket: cheeks stamped with a swaged rib, a gusset between them (front eye hanger).
void hangerStamped(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.04);
    labRail(b, p, V3(0, p("rise") + 0.045, 0), 0.4, 0.09, p("gap") + 0.12);
    const double rise = p("rise"), gap = p("gap");
    const V3 c = coat(p);
    bracketCheeks(b, p, c, rise, gap, 0.065, 0.0);
    // The gusset: a web between the cheeks at the back, toward the rail.
    b.style(c * 0.95, PAINT);
    b.box(V3(-0.052, rise * 0.62, 0), V3(0.008, rise * 0.7, gap));
    // A swage down each cheek.
    b.style(c * 1.2, PAINT);
    for (int s = -1; s <= 1; s += 2) b.box(V3(0, rise * 0.5, s * (gap * 0.5 + 0.0165)), V3(0.012, rise * 0.8, 0.002));
}

// The cast hanger: a pocket with thick cheeks, a rib and a boss round the pin.
void hangerCast(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.04);
    labRail(b, p, V3(0, p("rise") + 0.045, 0), 0.4, 0.09, p("gap") + 0.12);
    const double rise = p("rise"), gap = p("gap");
    const V3 c = coat(p) * 1.15;
    bracketCheeks(b, p, c, rise, gap, 0.07, 0.0);
    b.style(c, PAINT);
    for (int s = -1; s <= 1; s += 2) {
        b.cyl(V3(0, 0, s * (gap * 0.5 + 0.009)), V3(0, 0, s * (gap * 0.5 + 0.024)), 0.03, 12);  // the boss round the pin
        b.box(V3(0.0, rise * 0.55, s * (gap * 0.5 + 0.0175)), V3(0.016, rise * 0.8, 0.01));  // a rib up the cheek
    }
    b.box(V3(0.052, rise * 0.55, 0), V3(0.012, rise * 0.9, gap));  // the front web
}

// The shackle's frame mount: a bracket swept back from the rail, the pin carried in slotted ears.
void shackleStamped(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.04);
    labRail(b, p, V3(0, p("rise") + 0.045, 0), 0.4, 0.09, p("gap") + 0.12);
    const double rise = p("rise"), gap = p("gap");
    const V3 c = coat(p);
    bracketCheeks(b, p, c, rise, gap, 0.07, -0.03);
    b.style(c * 0.95, PAINT);
    b.box(V3(0.04, rise * 0.6, 0), V3(0.008, rise * 0.7, gap));  // web, ahead
    // Slotted ears' keeper: a cotter at the pin's end.
    b.style(kSteel * 0.6, METAL);
    b.torus(V3(0, 0, gap * 0.5 + 0.06), 0.008, 0.0016, 10, 5, 360);
}

void shackleCast(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.04);
    labRail(b, p, V3(0, p("rise") + 0.045, 0), 0.4, 0.09, p("gap") + 0.12);
    const double rise = p("rise"), gap = p("gap");
    const V3 c = coat(p) * 1.12;
    bracketCheeks(b, p, c, rise, gap, 0.06, -0.015);
    b.style(c, PAINT);
    // A heavy rear web and a stop block the shackle swings against.
    b.box(V3(-0.075, rise * 0.6, 0), V3(0.014, rise * 0.8, gap));
    b.box(V3(-0.045, 0.04, 0), V3(0.03, 0.03, gap));
    for (int s = -1; s <= 1; s += 2) b.cyl(V3(0, 0, s * (gap * 0.5 + 0.009)), V3(0, 0, s * (gap * 0.5 + 0.02)), 0.026, 10);
}

Reg frameHanger({"frame_hanger", "SPRING HANGER", "PARTS", "Frame bracket the front eye of a leaf spring (or a control arm) pins to; origin the pin, reaches up to the rail", 0.3,
                 {
                     {"STAMPED", "Pressed cheeks, a gusset web and a swage", {num("rise", "REACH UP", 0.12, 0.04, 0.4, 0.01), num("gap", "GAP", 0.14, 0.06, 0.22, 0.005), coatParam(), wearParam()}, hangerStamped},
                     {"CAST", "Cast pocket with bosses and a rib", {num("rise", "REACH UP", 0.12, 0.04, 0.4, 0.01), num("gap", "GAP", 0.14, 0.06, 0.22, 0.005), coatParam(), wearParam()}, hangerCast},
                 }});
Reg frameShackle({"frame_shackle", "SHACKLE MOUNT", "PARTS", "Frame bracket the rear shackle's pin goes through; origin the pin, reaches up to the rail", 0.3,
                  {
                      {"STAMPED", "Swept-back pressed bracket, a cotter on the pin", {num("rise", "REACH UP", 0.1, 0.04, 0.4, 0.01), num("gap", "GAP", 0.14, 0.06, 0.22, 0.005), coatParam(), wearParam()}, shackleStamped},
                      {"CAST", "Cast ear with a stop block", {num("rise", "REACH UP", 0.1, 0.04, 0.4, 0.01), num("gap", "GAP", 0.14, 0.06, 0.22, 0.005), coatParam(), wearParam()}, shackleCast},
                  }});

// ======================================================================================================
// 5. SHOCK TOWER: a pedestal standing up from the rail's top to a pivot or spring seat. Origin: the
// pivot; the tower reaches DOWN `drop` to the rail's top.
// ======================================================================================================

void towerBucket(B& b, const P& p) {
    side(b, p);
    labLift(b, p, p("drop"));
    labRail(b, p, V3(0, -p("drop") - 0.045, 0), 0.4, 0.09, 0.1);
    const double drop = p("drop"), wd = 0.13;
    const V3 c = coat(p);
    b.style(c, PAINT);
    // A tapered box: the foot on the rail wider than the top; a cap with the seat.
    const double yb = -drop;
    for (int s = -1; s <= 1; s += 2) {
        b.extrude({V2(-wd * 0.62, yb), V2(wd * 0.62, yb), V2(wd * 0.4, -0.02), V2(-wd * 0.4, -0.02)}, 0.008, s * 0.052);
    }
    b.box(V3(wd * 0.5 - 0.005, yb * 0.5 - 0.01, 0), V3(0.008, drop - 0.02, 0.1));   // front and back webs
    b.box(V3(-wd * 0.5 + 0.005, yb * 0.5 - 0.01, 0), V3(0.008, drop - 0.02, 0.1));
    b.style(c * 1.15, PAINT);
    plate(b, V3(0, -0.01, 0), 0.17, 0.14, 0.012, 0.02);                                  // the cap
    b.style(c, PAINT);
    plate(b, V3(0, yb + 0.004, 0), wd * 1.5, 0.11, 0.008, 0.015);                        // the foot
    b.style(kSteel * 0.6, METAL);
    b.cyl(V3(0, -0.018, 0), V3(0, 0.014, 0), 0.016, 8);                                  // the stud
    for (int s = -1; s <= 1; s += 2) rivet(b, V3(s * wd * 0.62, yb + 0.012, 0.04), V3(0, 1, 0), 0.007, 0.005);
}

// Two tabs and a pin: a control arm's upper pivot welded to a pedestal.
void towerTabs(B& b, const P& p) {
    side(b, p);
    labLift(b, p, p("drop"));
    labRail(b, p, V3(0, -p("drop") - 0.045, 0), 0.4, 0.09, 0.1);
    const double drop = p("drop");
    const V3 c = coat(p);
    b.style(c, PAINT);
    const double yb = -drop;
    for (int s = -1; s <= 1; s += 2) {
        b.extrude({V2(-0.07, yb), V2(0.07, yb), V2(0.03, -0.02), V2(0.026, 0.02), V2(-0.026, 0.02), V2(-0.03, -0.02)}, 0.01, s * 0.04);
    }
    b.style(c * 1.1, PAINT);
    plate(b, V3(0, yb + 0.005, 0), 0.17, 0.12, 0.01, 0.015);
    b.box(V3(0, yb * 0.5, 0), V3(0.012, drop * 0.9, 0.07));  // a centre web between the tabs
    b.style(kSteel * 0.8, METAL);
    b.cyl(V3(0, 0, -0.058), V3(0, 0, 0.058), 0.0095, 8);
    b.style(kBolt, METAL);
    b.box(V3(0, 0, 0.062), V3(0.026, 0.026, 0.008));
    b.cyl(V3(0, 0, -0.058), V3(0, 0, -0.066), 0.015, 6);
}

Reg frameTower({"frame_tower", "SHOCK TOWER", "PARTS", "Frame pedestal from the rail's top up to a spring seat or an upper arm's pivot; origin the pivot, reaches down to the rail", 0.4,
                {
                    {"STAMPED BUCKET", "Tapered pressed box, a cap with the spring seat stud", {num("drop", "REACH DOWN", 0.2, 0.05, 0.5, 0.01), coatParam(), wearParam()}, towerBucket},
                    {"PIVOT TABS", "Two welded tabs on a gusseted pedestal", {num("drop", "REACH DOWN", 0.2, 0.05, 0.5, 0.01), coatParam(), wearParam()}, towerTabs},
                }});

// ======================================================================================================
// 6. STAY: a bar from the rail out to something hung beyond it (an outrigger). Origin: its middle, X
// along the bar, `length` long.
// ======================================================================================================

void stayFlat(B& b, const P& p) {
    const double L = p("length");
    const V3 c = coat(p);
    b.style(c, PAINT);
    b.box(V3(0, 0, 0), V3(L, 0.012, 0.05));       // a flat bar, flat side up
    b.box(V3(0, 0.014, 0), V3(L * 0.9, 0.012, 0.012));  // a stiffening rib along it
    for (int s = -1; s <= 1; s += 2)
        for (int k = -1; k <= 1; k += 2) rivet(b, V3(s * (L * 0.5 - 0.03), 0.006, k * 0.017), V3(0, 1, 0), 0.007, 0.005);
}

void stayTube(B& b, const P& p) {
    const double L = p("length");
    const V3 c = coat(p);
    b.style(c, PAINT);
    b.cyl(V3(-L * 0.5, 0, 0), V3(L * 0.5, 0, 0), 0.018, 10);
    for (int s = -1; s <= 1; s += 2) {  // flattened, drilled ends
        b.box(V3(s * (L * 0.5 - 0.025), 0, 0), V3(0.06, 0.012, 0.052));
        rivet(b, V3(s * (L * 0.5 - 0.025), 0.006, 0), V3(0, 1, 0), 0.008, 0.006);
    }
}

Reg frameStay({"frame_stay", "FRAME STAY", "PARTS", "An outrigger bar from the rail out to a bracket hung beyond it (carframe.cpp: length is set to the gap)", 0.4,
               {
                   {"FLAT BAR", "Flat steel bar with a stiffening rib", {num("length", "LENGTH", 0.2, 0.05, 0.6, 0.01), coatParam(), wearParam()}, stayFlat},
                   {"TUBE", "Round tube with flattened, drilled ends", {num("length", "LENGTH", 0.2, 0.05, 0.6, 0.01), coatParam(), wearParam()}, stayTube},
               }});

// ======================================================================================================
// 7. BODY MOUNT: an outrigger welded on the rail's outside, a rubber puck on it. Origin: the rail's
// top outside corner; the bracket reaches out (+Z) and the puck stands on it.
// ======================================================================================================

void mountPuck(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.12);
    labRail(b, p, V3(0, -0.09, -0.04), 0.3, 0.18, 0.08);
    const V3 c = coat(p);
    b.style(c, PAINT);
    plate(b, V3(0, -0.02, 0.045), 0.1, 0.1, 0.008, 0.02);  // the outrigger plate, level a little under the rail's top
    b.extrude({V2(-0.04, -0.022), V2(0.04, -0.022), V2(0.0, -0.1)}, 0.008, 0.02);  // a gusset down the rail's side
    b.style(kSteel * 0.7, METAL);
    plate(b, V3(0, -0.012, 0.055), 0.06, 0.06, 0.003, 0.012);  // a washer plate
    b.style(kRubber, MATTE);
    b.cyl(V3(0, -0.011, 0.055), V3(0, 0.03, 0.055), 0.03, 12);  // the puck
    b.style(kSteel * 0.8, METAL);
    b.cyl(V3(0, 0.03, 0.055), V3(0, 0.034, 0.055), 0.026, 10);
    b.style(kBolt, METAL);
    b.cyl(V3(0, 0.034, 0.055), V3(0, 0.05, 0.055), 0.009, 6);
}

void mountCage(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.12);
    labRail(b, p, V3(0, -0.09, -0.04), 0.3, 0.18, 0.08);
    const V3 c = coat(p);
    b.style(c, PAINT);
    // A U-shaped cage over the rail's top edge, with the rubber in it.
    b.box(V3(0, -0.012, 0.03), V3(0.09, 0.008, 0.08));
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * 0.045, 0.012, 0.055), V3(0.007, 0.05, 0.05));
    b.style(kRubber, MATTE);
    b.box(V3(0, 0.014, 0.055), V3(0.082, 0.032, 0.045), 0.01);
    b.style(kSteel * 0.8, METAL);
    b.box(V3(0, 0.032, 0.055), V3(0.05, 0.004, 0.04));
    b.style(kBolt, METAL);
    b.cyl(V3(0, 0.034, 0.055), V3(0, 0.052, 0.055), 0.009, 6);
}

Reg frameMount({"frame_bodymount", "BODY MOUNT", "PARTS", "A rubber body mount on its frame outrigger; origin the rail's top outside corner", 0.2,
                {
                    {"PUCK", "Rubber puck on a welded outrigger, gusseted down the rail", {coatParam(), wearParam()}, mountPuck},
                    {"CAGE", "Stamped cage with a block of rubber", {coatParam(), wearParam()}, mountCage},
                }});

// ======================================================================================================
// 8. RECOVERY POINT: bolted to the end of a rail. Origin: the middle of the rail's end face; +X out of it
// (carframe.cpp turns it round for the tail).
// ======================================================================================================

void hookRing(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.12);
    labRail(b, p, V3(-0.15, 0, 0), 0.3, p("h"), p("w"));
    const V3 c = coat(p), zinc(0.6, 0.62, 0.62);
    const double h = p("h"), w = p("w");
    // A plate over the rail's end, a lug out of it, a D-ring shackle through the lug.
    b.style(c * 1.2, PAINT);
    b.box(V3(0.006, 0, 0), V3(0.012, h + 0.03, w + 0.03));
    b.extrude({V2(0.0, -0.05), V2(0.07, -0.03), V2(0.07, 0.03), V2(0.0, 0.05)}, 0.016, 0);
    b.style(zinc, METAL);
    b.cyl(V3(0.055, 0, -0.02), V3(0.055, 0, 0.02), 0.011, 8);  // the pin
    std::vector<V3> path;
    for (int i = 0; i <= 12; i++) {  // the D: a half round, forward and down
        const double a = M_PI * i / 12;
        path.push_back(V3(0.055 + 0.07 * std::sin(a) * 0.9, -0.04 - 0.05 * (1 - std::cos(a)) * 0.5 - 0.0, 0.032 * std::cos(a) * -1 + 0.0));
    }
    b.tube({V3(0.055, 0.0, 0.025), V3(0.1, 0.0, 0.034), V3(0.15, -0.0, 0.034), V3(0.15, -0.0, -0.034), V3(0.1, 0.0, -0.034), V3(0.055, 0.0, -0.025)}, 0.0085, 6);
    b.style(c * 2.0, METAL);
    for (int k = 0; k < 4; k++) rivet(b, V3(0.012, (k & 1 ? 1 : -1) * h * 0.4, (k & 2 ? 1 : -1) * w * 0.4), V3(1, 0, 0), 0.008, 0.006);
}

void hookForged(B& b, const P& p) {
    side(b, p);
    labLift(b, p, 0.12);
    labRail(b, p, V3(-0.15, 0, 0), 0.3, p("h"), p("w"));
    const V3 c = coat(p);
    const V3 red(0.62, 0.08, 0.06);
    const double h = p("h"), w = p("w");
    b.style(c * 1.2, PAINT);
    b.box(V3(0.006, 0, 0), V3(0.012, h + 0.03, w + 0.03));
    // The hook: a forged J, painted red, out of the plate and curling up.
    b.style(red, PAINT);
    std::vector<V2> j = {V2(0, -0.035), V2(0.09, -0.03), V2(0.14, -0.012), V2(0.165, 0.025), V2(0.15, 0.065), V2(0.115, 0.075), V2(0.115, 0.05), V2(0.135, 0.04), V2(0.135, 0.022), V2(0.115, 0.005), V2(0.09, 0.012), V2(0, 0.035)};
    b.extrude(j, 0.026, 0);
    b.style(c * 2.0, METAL);
    for (int k = 0; k < 4; k++) rivet(b, V3(0.012, (k & 1 ? 1 : -1) * h * 0.4, (k & 2 ? 1 : -1) * w * 0.4), V3(1, 0, 0), 0.008, 0.006);
}

Reg frameHook({"frame_hook", "RECOVERY POINT", "PARTS", "Tow hook or D-ring lug bolted to the end of a frame rail (origin: its end face; +X out)", 0.3,
               {
                   {"D-RING", "Bolt-on lug plate with a zinc D-ring shackle", {num("h", "RAIL HEIGHT", 0.18, 0.1, 0.3, 0.005), num("w", "RAIL WIDTH", 0.08, 0.05, 0.14, 0.005), coatParam(), wearParam()}, hookRing},
                   {"TOW HOOK", "Forged red hook on a plate", {num("h", "RAIL HEIGHT", 0.18, 0.1, 0.3, 0.005), num("w", "RAIL WIDTH", 0.08, 0.05, 0.14, 0.005), coatParam(), wearParam()}, hookForged},
               }});

// ======================================================================================================
// 9. SKID PLATE: under the transfer case, across the frame. Origin: the middle of its top face; X fore and
// aft. `span` the gap between the rails' inside faces, `rise` up to the rails' undersides.
// ======================================================================================================

void skidFlat(B& b, const P& p) {
    labLift(b, p, 0.03);
    const double span = p("span"), rise = p("rise"), len = p("len"), t = 0.006;
    const V3 c = coat(p) * 1.3;
    b.style(c, PAINT);
    const double W = span - 0.03;
    // The plate, its front edge bent up into a lip, flanges down the sides.
    b.box(V3(0, -t * 0.5, 0), V3(len, t, W));
    b.extrude({V2(len * 0.5, -t), V2(len * 0.5 + 0.06, 0.035), V2(len * 0.5 + 0.06, 0.04), V2(len * 0.5, 0.0)}, W, 0);
    // Side straps up to the rails, bolted to their inside faces.
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, rise * 0.5, s * (span * 0.5 - 0.004)), V3(len * 0.7, rise, 0.007));
        for (int k = -1; k <= 1; k += 2) rivet(b, V3(k * len * 0.25, rise * 0.6, s * (span * 0.5 - 0.004)), V3(0, 0, s), 0.01, 0.008);
    }
    b.style(kBolt, METAL);
    for (int k = 0; k < 4; k++) b.cyl(V3((k & 1 ? 1 : -1) * len * 0.36, 0.0, (k & 2 ? 1 : -1) * (W * 0.5 - 0.04)), V3((k & 1 ? 1 : -1) * len * 0.36, 0.006, (k & 2 ? 1 : -1) * (W * 0.5 - 0.04)), 0.012, 6);
}

void skidRibbed(B& b, const P& p) {
    labLift(b, p, 0.03);
    const double span = p("span"), rise = p("rise"), len = p("len"), t = 0.008;
    const V3 c = coat(p) * 1.5;
    b.style(c, PAINT);
    const double W = span - 0.03;
    b.box(V3(0, -t * 0.5, 0), V3(len, t, W));
    // Stamped ribs lengthwise, down the plate.
    b.style(c * 1.15, PAINT);
    for (int k = -3; k <= 3; k++) b.box(V3(0, 0.004, k * W * 0.13), V3(len * 0.8, 0.008, 0.02));
    b.style(c, PAINT);
    b.extrude({V2(len * 0.5, -t), V2(len * 0.5 + 0.07, 0.04), V2(len * 0.5 + 0.07, 0.048), V2(len * 0.5, 0.0)}, W, 0);
    b.extrude({V2(-len * 0.5, -t), V2(-len * 0.5 - 0.04, 0.02), V2(-len * 0.5 - 0.04, 0.028), V2(-len * 0.5, 0.0)}, W, 0);
    for (int s = -1; s <= 1; s += 2) {
        b.box(V3(0, rise * 0.5, s * (span * 0.5 - 0.004)), V3(len * 0.9, rise, 0.008));
        for (int k = -2; k <= 2; k++) rivet(b, V3(k * len * 0.2, rise * 0.55, s * (span * 0.5 - 0.004)), V3(0, 0, s), 0.01, 0.008);
    }
}

Reg frameSkid({"frame_skid", "SKID PLATE", "PARTS", "Transfer case skid plate across the rails (carframe.cpp: span is the gap between the rails, rise the height up to them)", 0.6,
               {
                   {"FLAT PLATE", "Steel plate, bent lip, bolted side straps", {num("span", "SPAN", 0.80, 0.5, 1.2, 0.01), num("rise", "REACH UP", 0.15, 0.05, 0.4, 0.01), num("len", "LENGTH", 0.5, 0.3, 0.8, 0.01), coatParam(), wearParam()}, skidFlat},
                   {"RIBBED PLATE", "Stamped ribs, a lip at both ends", {num("span", "SPAN", 0.80, 0.5, 1.2, 0.01), num("rise", "REACH UP", 0.15, 0.05, 0.4, 0.01), num("len", "LENGTH", 0.5, 0.3, 0.8, 0.01), coatParam(), wearParam()}, skidRibbed},
               }});

// ======================================================================================================
// 10. JACK PAD: a flat plate welded under the outside edge of a rail, hanging out past it, where a jack's
// saddle goes. Origin: the middle of the plate's underside (the saddle touches there); the rail runs
// along X above it, its outside face 0.04 inboard of the origin (-Z), its underside 0.012 up.
// ======================================================================================================

constexpr double kPadT = 0.02;    // the pad's depth under the rail: the rail's underside sits this far over the saddle spot
constexpr double kPadIn = 0.04;   // how far the saddle spot is out past the rail's outside face
constexpr double kPlateY = 0.008; // the plate's own underside (the tread or boss under it reaches the saddle)

// The plate itself, its gussets, the lip and the weld: shared by the two designs.
void padBase(B& b, const P& p, double w, double zOut, double lx, const V3& steel) {
    const double zIn = -kPadIn - w;
    b.style(steel, METAL);
    plate(b, V3(0, kPlateY + 0.006, (zIn + zOut) * 0.5), lx, zOut - zIn, 0.012, 0.012);
    // Gussets standing on the plate against the rail's outside face (the web is at z = -kPadIn).
    for (int s = -1; s <= 1; s += 2) {
        b.push();
        b.move(s * 0.05, 0, -kPadIn);
        b.yaw(-90);
        b.extrude({V2(0, kPadT), V2(0.05, kPadT), V2(0, kPadT + 0.06)}, 0.008, 0);
        b.pop();
    }
    // The raised lip round the outer edge, hi-vis paint so you can find it.
    if (p("mark") > 0.5) b.style(kHiVis, PAINT);
    b.box(V3(0, kPadT + 0.006, zOut - 0.004), V3(lx, 0.012, 0.008));
    b.style(steel * 0.85, METAL);
    for (int s = -1; s <= 1; s += 2) b.box(V3(s * (lx * 0.5 - 0.004), kPadT + 0.006, (zIn + zOut) * 0.5 + 0.01), V3(0.008, 0.012, zOut - zIn - 0.03));
    // A weld bead along the foot where it meets the rail.
    b.style(kWeld, MATTE);
    b.box(V3(0, kPadT + 0.003, -kPadIn + 0.003), V3(lx, 0.006, 0.006));
}

// A plate with welded cross-bar tread under it.
void padPlate(B& b, const P& p) {
    side(b, p);
    const double w = p("w");
    labLift(b, p, 0.2);
    labRail(b, p, V3(0, kPadT + 0.09, -kPadIn - w * 0.5), 0.4, 0.18, w);
    const V3 steel = mix(V3(0.34, 0.34, 0.35), kRust, p("wear") * 0.4);
    const double zOut = kPadIn + 0.055;
    padBase(b, p, w, zOut, 0.16, steel);
    b.style(steel * 0.75, METAL);
    for (int i = -3; i <= 3; i++) b.box(V3(i * 0.02, kPlateY * 0.5, 0.02), V3(0.007, kPlateY, 0.09));
}

// A plate with a round boss welded under it: the saddle's seat.
void padCup(B& b, const P& p) {
    side(b, p);
    const double w = p("w");
    labLift(b, p, 0.2);
    labRail(b, p, V3(0, kPadT + 0.09, -kPadIn - w * 0.5), 0.4, 0.18, w);
    const V3 steel = mix(V3(0.30, 0.30, 0.31), kRust, p("wear") * 0.4);
    const double zOut = kPadIn + 0.05;
    padBase(b, p, w, zOut, 0.15, steel);
    b.style(steel * 0.9, METAL);
    b.cyl(V3(0, kPlateY, 0.0), V3(0, 0.0, 0.0), 0.05, 18);
    b.style(steel * 0.7, METAL);
    b.torus(V3(0, 0.0015, 0), 0.036, 0.0025, 20, 4);  // a ring turned in the boss's face
}

Reg frameJackPad({"frame_jackpad", "JACK PAD", "PARTS", "Flat plate welded under a frame rail's outside edge, where a jack's saddle goes (origin: underside, the saddle spot; rail above, inboard)", 0.3,
                  {
                      {"PLATE", "Flat plate with a hi-vis lip and tread, welded on", {num("w", "RAIL WIDTH", 0.08, 0.05, 0.14, 0.005), num("mark", "HI-VIS LIP", 1, 0, 1, 1), wearParam()}, padPlate},
                      {"PLATE AND SEAT", "Plate with a round boss for the saddle", {num("w", "RAIL WIDTH", 0.08, 0.05, 0.14, 0.005), num("mark", "HI-VIS RING", 1, 0, 1, 1), wearParam()}, padCup},
                  }});

}  // namespace
