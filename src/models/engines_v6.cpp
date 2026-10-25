// V6 engines: twin-turbo EcoBoost, Toyota 5VZ-FE, Toyota GR.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

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

void v6Stand(B& b, const P& p) {
    const Eng e = optsFor(p);
    if (bare(p)) return v6(b, e);
    engineStand(b, 0.5, -0.30, V3(0.2, 0.22, 0.26), [&] { v6(b, e); });
}
void v6Skid(B& b, const P& p) {
    const Eng e = optsFor(p);
    if (bare(p)) return v6(b, e);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { v6(b, e); }, 0.232, {V2(0.255, -0.14), V2(0.30, 0.0), V2(0.28, 0.14), V2(0.17, 0.30), V2(0.06, 0.37)}, 0.8);
}
void v6Junk(B& b, const P& p) {
    const Eng e = optsFor(p, p("wear"));
    if (bare(p)) return v6(b, e);
    engineGround(b, [&] { v6(b, e); }, 0.75);
}

Reg crateEngineV6({"crate_engine_v6", "CRATE ENGINE V6", "PARTS", "Career upgrade: a modern V6 crate engine with a plastic engine cover", 0.8,
                   {
                       {"ON A STAND", "Plastic-covered V6 bolted to a rolling engine stand", {enginePaintParam(2)}, v6Stand},
                       {"ON A SKID", "Fresh crate V6 strapped to a shipping skid", {enginePaintParam(2)}, v6Skid},
                       {"JUNKYARD PULL", "Dirty used pull sitting on scrap timber", {enginePaintParam(2), num("wear", "GREASE", 0.65, 0, 1, 0.05), seedParam()}, v6Junk},
                   }});

}  // namespace
