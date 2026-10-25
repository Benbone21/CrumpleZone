// Gear: power. The jump pack and jumper cables (battery.h), and the single clamp drawn on a post while they are used.
#include <cmath>

#include "../modelkit.h"

using namespace mk;

namespace {

const V3 kBlack(0.07, 0.07, 0.075), kDark(0.14, 0.14, 0.15), kRed(0.72, 0.08, 0.06), kCopper(0.72, 0.45, 0.25), kSteel(0.62, 0.63, 0.65);

// One spring clamp, jaws toward +X (origin at the jaw tips' root), handle grips in the `col` insulation.
void clampAt(B& b, const V3& at, const V3& col, double yawDeg = 0) {
    b.push();
    b.move(at);
    b.yaw(yawDeg);
    b.style(col, MATTE);
    b.box(V3(-0.035, 0.0, 0), V3(0.075, 0.022, 0.026), 0.006);       // the insulated handles
    b.box(V3(-0.035, 0.016, 0), V3(0.07, 0.012, 0.02), 0.004);
    b.style(kSteel, METAL);
    b.box(V3(0.018, -0.006, 0), V3(0.035, 0.008, 0.02), 0.002);      // lower jaw
    b.box(V3(0.018, 0.012, 0), V3(0.035, 0.008, 0.02), 0.002);       // upper jaw
    for (int k = 0; k < 4; k++) {                                   // teeth
        b.box(V3(0.034, 0.0, -0.0075 + 0.005 * k), V3(0.004, 0.012, 0.0025), 0.0005);
    }
    b.cyl(V3(-0.002, 0.003, -0.011), V3(-0.002, 0.003, 0.011), 0.004, 8);  // pivot
    b.pop();
}

// The pack: a squat brick with a carry handle, a charge gauge, and two leads wound round its back.
void brick(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    b.style(col, MATTE);
    b.box(V3(0, 0.085, 0), V3(0.2, 0.17, 0.11), 0.02);
    b.style(col * 0.7, MATTE);
    b.box(V3(0, 0.172, 0), V3(0.17, 0.012, 0.09), 0.004);
    b.style(kBlack, MATTE);
    b.tube({V3(-0.07, 0.17, 0), V3(-0.07, 0.225, 0), V3(0.07, 0.225, 0), V3(0.07, 0.17, 0)}, 0.012, 8);  // the handle
    b.box(V3(0.101, 0.1, 0), V3(0.004, 0.09, 0.08), 0.002);                                               // the face plate
    b.style(V3(0.1, 0.9, 0.3), EMISSIVE);
    for (int k = 0; k < 4; k++) b.box(V3(0.104, 0.07 + 0.017 * k, 0.03), V3(0.004, 0.011, 0.014), 0.001);  // charge lights
    b.style(kRed, PAINT);
    b.cyl(V3(0.102, 0.045, -0.025), V3(0.108, 0.045, -0.025), 0.011, 10);
    b.style(kDark, METAL);
    b.cyl(V3(0.102, 0.045, 0.025), V3(0.108, 0.045, 0.025), 0.011, 10);
    // The leads coiled on the back, a clamp each side.
    b.style(kRed, MATTE);
    b.tube({V3(-0.1, 0.06, 0.03), V3(-0.13, 0.08, 0.03), V3(-0.13, 0.12, 0.03), V3(-0.1, 0.14, 0.03)}, 0.006, 5);
    b.style(kBlack, MATTE);
    b.tube({V3(-0.1, 0.06, -0.03), V3(-0.13, 0.08, -0.03), V3(-0.13, 0.12, -0.03), V3(-0.1, 0.14, -0.03)}, 0.006, 5);
    clampAt(b, V3(-0.13, 0.04, 0.03), kRed, 180);
    clampAt(b, V3(-0.13, 0.04, -0.03), kBlack, 180);
    b.style(kSteel, METAL);
    b.letters("JUMP", V3(0.103, 0.16, 0), 0.0045, 0.002);
}

void flat(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    b.style(col, MATTE);
    b.box(V3(0, 0.05, 0), V3(0.3, 0.1, 0.14), 0.03);
    b.style(kBlack, MATTE);
    b.box(V3(0, 0.1, 0), V3(0.26, 0.012, 0.1), 0.004);
    b.tube({V3(-0.06, 0.1, -0.05), V3(-0.06, 0.13, 0), V3(-0.06, 0.1, 0.05)}, 0.008, 6);
    b.style(V3(1.0, 0.85, 0.2), EMISSIVE);
    b.box(V3(0.151, 0.06, 0), V3(0.004, 0.02, 0.05), 0.001);  // work light
    clampAt(b, V3(-0.15, 0.04, 0.04), kRed, 180);
    clampAt(b, V3(-0.15, 0.04, -0.04), kBlack, 180);
    b.style(kRed, MATTE);
    b.tube({V3(-0.14, 0.04, 0.04), V3(-0.12, 0.02, 0.06), V3(-0.08, 0.06, 0.07)}, 0.005, 5);
    b.style(kBlack, MATTE);
    b.tube({V3(-0.14, 0.04, -0.04), V3(-0.12, 0.02, -0.06), V3(-0.08, 0.06, -0.07)}, 0.005, 5);
}

Reg jumpPack({"jump_pack", "JUMP PACK", "TOOLS", "A portable starter: its clamps on a dead battery and the engine turns over. A career item", 0.3,
              {
                  {"BRICK", "A squat brick with a handle, charge lights and wound leads", {paintParam(8)}, brick},
                  {"FLAT PACK", "A slim, wide pack with a work light", {paintParam(3)}, flat},
              }});

// The cables: a coil of red and black lead, a clamp at each of the four ends, in a bundle tied with a strap.
void coiled(B& b, const P& p) {
    const double turns = p.i("coils") ? 5 : 3;
    for (int w = 0; w < 2; w++) {
        const V3 col = w == 0 ? kRed : kBlack;
        b.style(col, MATTE);
        for (int k = 0; k < (int)turns; k++) {
            const double R = 0.11 + 0.007 * k + 0.004 * w;
            std::vector<V3> path;
            for (int i = 0; i <= 16; i++) {
                const double a = 2 * M_PI * i / 16;
                path.push_back(V3(R * std::cos(a), 0.016 + 0.011 * k + 0.005 * w, R * std::sin(a)));
            }
            b.tube(path, 0.0055, 4);
        }
        clampAt(b, V3(0.12 + 0.01 * w, 0.03 + 0.02 * w, 0.05 - 0.1 * w), col, 20 - 40 * w);
        clampAt(b, V3(-0.12 - 0.01 * w, 0.03 + 0.02 * w, 0.05 - 0.1 * w), col, 160 + 40 * w);
    }
    b.style(V3(0.7, 0.55, 0.15), MATTE);
    b.box(V3(0, 0.05, 0.11), V3(0.02, 0.1, 0.02), 0.004);  // the tie
}

void bagged(B& b, const P& p) {
    (void)p;
    b.style(V3(0.1, 0.1, 0.11), MATTE);
    b.box(V3(0, 0.07, 0), V3(0.34, 0.14, 0.13), 0.03);  // a zipped bag
    b.style(kSteel, METAL);
    b.box(V3(0, 0.141, 0), V3(0.3, 0.004, 0.012), 0.001);
    b.style(kRed, MATTE);
    b.tube({V3(-0.15, 0.1, 0.07), V3(-0.19, 0.08, 0.09), V3(-0.2, 0.03, 0.09)}, 0.006, 5);
    clampAt(b, V3(-0.2, 0.02, 0.09), kRed, 160);
    b.style(kBlack, MATTE);
    b.tube({V3(-0.15, 0.1, -0.07), V3(-0.19, 0.08, -0.09), V3(-0.2, 0.03, -0.09)}, 0.006, 5);
    clampAt(b, V3(-0.2, 0.02, -0.09), kBlack, 200);
    b.style(V3(0.75, 0.75, 0.78), MATTE);
    b.letters("JUMPER", V3(0.171, 0.07, 0), 0.0055, 0.002);
}

Reg cables({"jumper_cables", "JUMPER CABLES", "TOOLS", "Heavy leads with a clamp at each end: from a running vehicle's battery to a dead one. A career item", 0.34,
            {
                {"COILED", "A coil of red and black lead, clamps hanging off it", {choice("coils", "TURNS", 0, {"3", "5"})}, coiled},
                {"BAGGED", "In a zipped bag, a pair of clamps hanging out", {}, bagged},
            }});

// A single clamp (with a stub of lead) as drawn on a battery post while the job's being done.
void oneClamp(B& b, const P& p) {
    (void)p;
    clampAt(b, V3(0, 0.0, 0), kRed);
    b.style(kRed, MATTE);
    b.tube({V3(-0.07, 0.0, 0), V3(-0.14, -0.01, 0), V3(-0.2, -0.04, 0.02)}, 0.006, 5);
}

Reg clampModel({"cable_clamp", "CABLE CLAMP", "TOOLS", "One jumper clamp with a stub of lead, drawn on a battery post in use", 0.2, {{"CLAMP", "A spring clamp", {}, oneClamp}}});

}  // namespace
