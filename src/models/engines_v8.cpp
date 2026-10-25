// V8 engines: the pushrod family (small block, big block, LS) and the DOHC flat-plane twin-turbo.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

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
    if (bare(p)) return v8(b, e);
    engineStand(b, 0.5, -0.335, V3(0.2, 0.22, 0.26), [&] { v8(b, e); });
}
void v8Skid(B& b, const P& p) {
    const Eng e = v8Opts(p);
    if (bare(p)) return v8(b, e);
    engineSkid(b, V3(0.85, 0.62, 0.1), [&] { v8(b, e); }, 0.263, {V2(0.27, -0.14), V2(0.335, 0.0), V2(0.32, 0.14), V2(0.22, 0.30), V2(0.07, 0.345)});
}
void v8Junk(B& b, const P& p) {
    Eng e = v8Opts(p, p("wear"));
    e.seed = (uint32_t)p.i("seed");
    if (bare(p)) return v8(b, e);
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

}  // namespace
