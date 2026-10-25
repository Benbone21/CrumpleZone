// Gearboxes and transfer cases: manual, automatic, CVT, and the 4x4 transfer cases.
#include <algorithm>
#include <cmath>
#include <functional>

#include "drivetrain.h"
#include "drivetrain_kit.h"

using namespace mk;
using namespace dk;

namespace {

// -- gearbox_manual ----------------------------------------------------------------------------

void manualAlu(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.33, 0.33, 0.35);
    const double Y0 = 0.245;
    if (!bare(p)) {
        gbRails(b, -0.12, 0.42, 0.07);
        b.style(V3(0.5, 0.38, 0.23), WOOD);
        b.box(V3(0.02, 0.05, 0), V3(0.22, 0.1, 0.18));  // block under the case
    }
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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
    if (!bare(p)) {
        gbRails(b, -0.1, 0.4, 0.07);
        b.style(V3(0.5, 0.38, 0.23), WOOD);
        b.box(V3(0.05, 0.05, 0), V3(0.22, 0.1, 0.18));
    }
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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
    if (!bare(p)) gbRails(b, -0.1, 0.42, 0.07);
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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
    if (!bare(p)) gbRails(b, -0.14, 0.42, 0.065);
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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

void transferGear(B& b, const P& p) {
    Mat fm;
    const V3 c = gbFinish(p.i("finish"), fm);
    const V3 steel(0.3, 0.3, 0.32);
    const double Y0 = 0.21;
    if (!bare(p)) gbRails(b, -0.2, 0.2, 0.07, 0.11);
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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
    if (!bare(p)) gbRails(b, -0.25, 0.2, 0.07, 0.11);
    b.push();
    b.move(0, bare(p) ? 0 : Y0, 0);
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

}  // namespace

// (Placeholder ends for the old designs, until the new ones land.)
bool drivetrain::boxEnds(const std::string& id, int variant, BoxEnds& e) {
    const double pin = 0.068;
    e = BoxEnds();
    if (id == "gearbox_manual") return e.inX = variant == 1 ? 0.38 : 0.40, e.rear = true, e.rearAt = V3((variant == 1 ? -0.5 : -0.41) - pin, 0, 0), true;
    if (id == "gearbox_auto") return e.inX = 0.40, e.rear = true, e.rearAt = V3((variant == 1 ? -0.43 : -0.41) - pin, 0, 0), true;
    if (id == "gearbox_cvt") return e.inX = 0.40, e.rear = true, e.rearAt = V3(-0.41 - pin, 0, 0), true;
    if (id == "transfer_case") {
        const bool k = variant == 1;
        e.inX = 0.2, e.rear = e.front = true;
        e.rearAt = V3((k ? -0.37 : -0.3) - pin, 0, 0);
        e.frontAt = k ? V3(0.28 + pin, -0.085, 0.085) : V3(0.28 + pin, -0.1, -0.09);
        return true;
    }
    return false;
}
