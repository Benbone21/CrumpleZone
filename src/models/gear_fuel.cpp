// Gear: fuel. (The model kit's worked example: see modelkit.h for the frame and the primitives.)
#include "../modelkit.h"

using namespace mk;

namespace {

// The classic pressed-steel can: X-stiffened faces, the welded seam round its edge, three
// handles across the top, the spout under a clamp cap.
void nato(B& b, const P& p) {
    const double h = p.i("size") ? 0.47 : 0.36, w = p.i("size") ? 0.345 : 0.27, t = 0.165;
    const V3 col = paint(p.i("paint"));
    b.style(col, PAINT);
    b.box(V3(0, h / 2 - 0.01, 0), V3(t, h - 0.02, w), 0.025);
    b.box(V3(0, h / 2 - 0.01, 0), V3(0.012, h + 0.004, w + 0.01), 0.004);  // the seam
    // The X on each face, pressed out of it.
    for (int s = -1; s <= 1; s += 2)
        for (int d = -1; d <= 1; d += 2) {
            const double x = s * (t / 2 + 0.003);
            b.beam(V3(x, 0.05, -d * (w / 2 - 0.05)), V3(x, h - 0.09, d * (w / 2 - 0.05)), 0.028, 0.006, V3(s, 0, 0), 0.002);
        }
    // Handles: three bars over the top, front to back.
    for (int k = -1; k <= 1; k++) {
        const double z = k * w * 0.2 - w * 0.12;
        b.tube({V3(-t * 0.3, h - 0.03, z), V3(-t * 0.3, h + 0.035, z), V3(t * 0.3, h + 0.035, z), V3(t * 0.3, h - 0.03, z)}, 0.009, 6);
    }
    // Spout and clamp cap at the front corner.
    b.cyl(V3(0, h - 0.03, w / 2 - 0.06), V3(0, h + 0.02, w / 2 - 0.035), 0.025, 14);
    b.style(paint(9) * 0.8, METAL);
    b.cyl(V3(0, h + 0.012, w / 2 - 0.036), V3(0, h + 0.032, w / 2 - 0.03), 0.03, 14);
    b.box(V3(0, h + 0.03, w / 2 - 0.075), V3(0.02, 0.012, 0.07), 0.003);  // clamp lever
}

// A moulded flat pack that bolts to a rack: thin, rounded, a handle cut through the top.
void flatPack(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const double W = 0.42, H = 0.40, T = p.i("size") ? 0.13 : 0.09;
    b.style(col, MATTE);
    b.push();
    b.yaw(90);  // the shape faces front
    std::vector<V2> body = roundedRect(W, H - 0.1, 0.07, 5);
    for (V2& q : body) q.y += (H - 0.1) / 2;
    b.extrude(body, T);
    // The handle: an arch over the top.
    b.extrude({V2(-0.13, H - 0.1), V2(-0.09, H - 0.1), V2(-0.09, H - 0.04), V2(0.09, H - 0.04), V2(0.09, H - 0.1), V2(0.13, H - 0.1), V2(0.13, H), V2(-0.13, H)},
              T * 0.55);
    b.pop();
    // Moulded ribs and the mounting hub on each face.
    for (int s = -1; s <= 1; s += 2) {
        b.color(col * 0.85);
        b.cyl(V3(s * T / 2, 0.15, 0), V3(s * (T / 2 + 0.008), 0.15, 0), 0.075, 20);
        b.color(paint(3));
        b.cyl(V3(s * (T / 2 + 0.008), 0.15, 0), V3(s * (T / 2 + 0.012), 0.15, 0), 0.03, 6);  // the lock
        b.color(col * 0.92);
        for (int k = -1; k <= 1; k += 2) b.box(V3(s * T / 2, 0.15, k * 0.15), V3(0.008, 0.2, 0.03), 0.004);
    }
    // Spout cap.
    b.style(paint(3), MATTE);
    b.cyl(V3(0, H - 0.1, W / 2 - 0.07), V3(0, H - 0.05, W / 2 - 0.07), 0.032, 12);
}

// The everyday plastic can: a fat rounded body, a handle along the top, a long spout.
void plastic(B& b, const P& p) {
    const V3 col = paint(p.i("paint"));
    const double s = p.i("size") ? 1.0 : 0.82;
    b.scale(s);
    b.style(col, MATTE);
    b.box(V3(0, 0.18, 0), V3(0.22, 0.36, 0.36), 0.055);
    b.box(V3(0, 0.37, -0.02), V3(0.16, 0.04, 0.26), 0.02);  // shoulder
    // The handle: a loop along the top.
    b.tube({V3(0, 0.38, -0.14), V3(0, 0.46, -0.11), V3(0, 0.47, 0.0), V3(0, 0.44, 0.08), V3(0, 0.39, 0.1)}, 0.016, 8);
    // Spout: out of the front corner, a bend, a yellow nozzle.
    b.color(col * 0.9);
    b.cyl(V3(0, 0.36, 0.13), V3(0, 0.40, 0.15), 0.03, 14);
    b.style(V3(0.12, 0.12, 0.12), MATTE);
    b.tube({V3(0, 0.40, 0.15), V3(0, 0.47, 0.19), V3(0, 0.50, 0.26), V3(0, 0.49, 0.32)}, 0.015, 8);
    b.style(paint(8), MATTE);
    b.cone(V3(0, 0.49, 0.32), V3(0, 0.475, 0.37), 0.019, 0.011, 10);
    b.style(paint(3), MATTE);
    b.cyl(V3(0, 0.39, -0.15), V3(0, 0.41, -0.15), 0.012, 8);  // vent
}

Reg jerryCan({"jerry_can", "JERRY CAN", "GEAR", "Spare fuel: carried on a truck, poured in with F on foot; a career find", 0.6,
              {
                  {"NATO STEEL", "Pressed steel, X-stiffened, three handles", {paintParam(1), choice("size", "SIZE", 1, {"10 L", "20 L"})}, nato},
                  {"FLAT PACK", "Moulded flat pack that locks onto a rack", {paintParam(0), choice("size", "SIZE", 0, {"7 L", "11 L"})}, flatPack},
                  {"PLASTIC CAN", "The everyday red can with a long spout", {paintParam(0), choice("size", "SIZE", 1, {"10 L", "20 L"})}, plastic},
              }});

}  // namespace
