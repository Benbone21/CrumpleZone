// Inline and flat engines: Honda fours, the EcoBoost four, the UTV triple, the VW flat four.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

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

void i4Stand(B& b, const P& p) {
    const Eng e = optsFor(p);
    if (bare(p)) return i4(b, e);
    engineStand(b, 0.46, -0.27, V3(0.2, 0.22, 0.26), [&] { i4(b, e); });
}
void i4Skid(B& b, const P& p) {
    const Eng e = optsFor(p);
    if (bare(p)) return i4(b, e);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { i4(b, e); }, 0.222, {V2(0.2, -0.14), V2(0.23, 0.0), V2(0.21, 0.16), V2(0.15, 0.31), V2(0.05, 0.4)}, 0.7);
}
void i4Junk(B& b, const P& p) {
    const Eng e = optsFor(p, p("wear"));
    if (bare(p)) return i4(b, e);
    engineGround(b, [&] { i4(b, e); }, 0.65);
}

// ---- crate_engine_flat4 ----------------------------------------------------------------------

// The air-cooled VW Type 1 four: crank axis on the origin, +X the pulley end (in a Beetle it
// faces the back bumper), -X the flywheel flange. A short magnesium crankcase, two finned
// barrels each side, finned heads with bail-held rocker covers, the tin "doghouse" fan
// shroud across the top with the generator stand and the fan housing, dual carbs on the
// heads under round chrome air cleaners, and the J-tube heater boxes under the heads into
// one collector. The pan (the strainer plate) is 0.15 below the axis.
void flat4(B& b, const Eng& e) {
    const V3 mag = grime(e, V3(0.58, 0.58, 0.55), 0.9), alu = grime(e, V3(0.66, 0.66, 0.64), 0.9);
    const V3 tin = grime(e, e.paint, 0.6), black = V3(0.05, 0.05, 0.055), steel = grime(e, V3(0.5, 0.47, 0.44), 0.6);
    // Crankcase, split down the middle (its seam a dark line on top).
    b.style(mag, METAL);
    b.box(V3(0, 0, 0), V3(0.36, 0.24, 0.26), 0.025);
    b.style(V3(0.2, 0.2, 0.2), METAL);
    b.box(V3(0, 0.1215, 0), V3(0.34, 0.004, 0.006));
    b.style(alu, METAL);
    b.box(V3(0.02, -0.14, 0), V3(0.14, 0.02, 0.14), 0.005);  // oil strainer plate
    rearFace(b, e, -0.20, 0.0, 0.30, 0.28, 0.13, mag, METAL);
    // Barrels: finned, two each side.
    for (int sd = -1; sd <= 1; sd += 2)
        for (int c = -1; c <= 1; c += 2) {
            const double x = c * 0.085;
            b.style(V3(0.16, 0.16, 0.17), METAL);
            zcyl(b, x, 0.0, sd * 0.13, sd * 0.29, 0.048, 8, false);
            b.style(grime(e, V3(0.24, 0.24, 0.25), 0.5), METAL);
            for (int f = 0; f < 3; f++) {
                const double z = sd * (0.16 + 0.045 * f);
                zcyl(b, x, 0.0, z - 0.004, z + 0.004, 0.062, 8);
            }
        }
    // Heads, finned, and the rocker covers held on by their bails.
    for (int sd = -1; sd <= 1; sd += 2) {
        b.style(alu, METAL);
        b.box(V3(0, 0.0, sd * 0.325), V3(0.34, 0.15, 0.07));
        for (int f = 0; f < 3; f++) b.box(V3(0, -0.045 + 0.045 * f, sd * 0.325), V3(0.355, 0.008, 0.075));
        b.style(e.chrome ? V3(0.78, 0.79, 0.82) : black, e.chrome ? CHROME : PAINT);
        b.box(V3(0, -0.005, sd * 0.38), V3(0.30, 0.10, 0.04), 0.012);
        b.style(steel, METAL);
        b.box(V3(0, -0.005, sd * 0.403), V3(0.012, 0.11, 0.006));
        // Spark plug leads up over the heads.
        b.style(black, MATTE);
        for (int c = -1; c <= 1; c += 2) b.tube({V3(c * 0.085, 0.05, sd * 0.345), V3(c * 0.07, 0.15, sd * 0.30), V3(0.05, 0.24, sd * 0.12)}, 0.006, 3, false, false);
    }
    // The fan shroud (the doghouse), its fan housing round at the pulley end, and the
    // generator stand on top of it.
    b.style(tin, PAINT);
    b.box(V3(0.0, 0.205, 0), V3(0.32, 0.17, 0.50), 0.05);
    for (int sd = -1; sd <= 1; sd += 2) b.box(V3(0.0, 0.135, sd * 0.30), V3(0.30, 0.05, 0.14));  // the tins over the heads
    xcyl(b, 0.06, 0.18, 0.21, 0, 0.14, 12);
    b.style(black, MATTE);
    xcyl(b, 0.179, 0.182, 0.21, 0, 0.115, 12, false);  // fan inlet
    b.style(grime(e, V3(0.15, 0.15, 0.16), 0.4), METAL);
    xcyl(b, -0.10, 0.17, 0.34, 0, 0.058, 10);   // generator
    b.style(grime(e, V3(0.08, 0.08, 0.09), 0.4), METAL);
    b.box(V3(-0.03, 0.29, 0), V3(0.12, 0.04, 0.09));  // generator stand
    // Distributor and oil filler beside the stand.
    b.style(black, MATTE);
    b.cyl(V3(-0.13, 0.12, 0.08), V3(-0.13, 0.20, 0.08), 0.03, 6);
    b.style(V3(0.15, 0.15, 0.16), METAL);
    b.cyl(V3(-0.15, 0.10, -0.10), V3(-0.15, 0.24, -0.10), 0.025, 6);
    // Crank and generator pulleys on one belt.
    b.style(grime(e, V3(0.3, 0.3, 0.32), 0.5), METAL);
    pulleyX(b, V3(0.19, 0, 0), 0.085, 0.03, 10);
    pulleyX(b, V3(0.19, 0.34, 0), 0.05, 0.022, 10);
    beltX(b, 0.20, {{0, 0, 0.085}, {0, 0.34, 0.05}}, 0.016, V3(0.04, 0.04, 0.04));
    // Dual carbs on the heads under round chrome air cleaners.
    for (int sd = -1; sd <= 1; sd += 2) {
        b.style(alu, METAL);
        b.cyl(V3(0, 0.08, sd * 0.30), V3(0, 0.25, sd * 0.30), 0.03, 6, false);    // manifold riser
        b.box(V3(0, 0.27, sd * 0.30), V3(0.10, 0.07, 0.08));       // carb body
        b.style(V3(0.8, 0.8, 0.82), CHROME);
        b.cyl(V3(0, 0.305, sd * 0.30), V3(0, 0.36, sd * 0.30), 0.085, 12);  // air cleaner
        b.style(V3(0.06, 0.06, 0.065), MATTE);
        b.cyl(V3(0, 0.30, sd * 0.30), V3(0, 0.307, sd * 0.30), 0.088, 12, false); // its filter band
    }
    // J-tubes from the exhaust ports under each head, and the collector back out past the pulley.
    b.style(steel, METAL);
    const V3 C(0.24, -0.17, 0.0);
    for (int sd = -1; sd <= 1; sd += 2)
        for (int c = -1; c <= 1; c += 2) {
            const double x = c * 0.085;
            b.tube({V3(x, -0.07, sd * 0.32), V3(x, -0.14, sd * 0.30), V3(x * 0.5 + 0.1, -0.17, sd * 0.20), V3(C.x, C.y, sd * 0.06)}, 0.018, 5, false, false);
        }
    b.box(C, V3(0.06, 0.07, 0.16));
    b.cone(C + V3(0.03, 0, 0), C + V3(0.13, -0.01, 0), 0.035, 0.03, 10);
}

void f4Stand(B& b, const P& p) {
    Eng e = optsFor(p);
    e.chrome = p.i("chrome") != 0;
    if (bare(p)) return flat4(b, e);
    engineStand(b, 0.5, -0.24, V3(0.2, 0.22, 0.26), [&] { flat4(b, e); });
}
void f4Skid(B& b, const P& p) {
    Eng e = optsFor(p);
    e.chrome = p.i("chrome") != 0;
    if (bare(p)) return flat4(b, e);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { flat4(b, e); }, 0.0, {V2(0.42, -0.14), V2(0.45, 0.05), V2(0.40, 0.30), V2(0.25, 0.40), V2(0.08, 0.40)}, 0.7);
}
void f4Junk(B& b, const P& p) {
    Eng e = optsFor(p, p("wear"));
    e.chrome = false;
    if (bare(p)) return flat4(b, e);
    engineGround(b, [&] { flat4(b, e); }, 0.6);
}

Reg crateEngineFlat4({"crate_engine_flat4", "CRATE ENGINE FLAT-4", "PARTS", "Career upgrade: an air-cooled VW flat four with its fan shroud and dual carbs", 0.8,
                      {
                          {"ON A STAND", "Fresh-built dual-carb Type 1 on a rolling engine stand", {enginePaintParam(2), choice("chrome", "CHROME COVERS", 1, {"OFF", "ON"})}, f4Stand},
                          {"ON A SKID", "Crate long-block strapped to a shipping skid", {enginePaintParam(2), choice("chrome", "CHROME COVERS", 0, {"OFF", "ON"})}, f4Skid},
                          {"JUNKYARD PULL", "Oily pull out of a dead Beetle on scrap timber", {enginePaintParam(2), num("wear", "GREASE", 0.7, 0, 1, 0.05), seedParam()}, f4Junk},
                      }});

Reg crateEngineI4({"crate_engine_i4", "CRATE ENGINE I4", "PARTS", "Career upgrade: a high-revving DOHC inline-four crate engine", 0.75,
                   {
                       {"ON A STAND", "Red-cammed four on a rolling engine stand", {enginePaintParam(4)}, i4Stand},
                       {"ON A SKID", "Fresh crate four strapped to a shipping skid", {enginePaintParam(4)}, i4Skid},
                       {"JUNKYARD PULL", "Greasy imported pull sitting on scrap timber", {enginePaintParam(4), num("wear", "GREASE", 0.65, 0, 1, 0.05), seedParam()}, i4Junk},
                   }});

}  // namespace
