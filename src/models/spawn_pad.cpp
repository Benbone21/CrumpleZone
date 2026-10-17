// Spawn pad: the poured concrete slab vehicles start on (spawnpad.h), with its ramp down to the ground
// ahead. Origin on the slab's top at the spot the first vehicle starts on, +X the way it faces, +Z its
// left. The slab runs from `back` behind to `front` ahead and `half` either side; the ramp carries on
// `ramp` m from the front, `drop` m lower at its end. The ground is made level with the slab's top, which
// stands 2 cm proud of it so it never z-fights; the slab goes down below that so a raised edge shows where
// the ground falls away.
#include <algorithm>
#include <cmath>

#include "../modelkit.h"
#include "../spawnpad.h"

using namespace mk;

namespace {

const double kTop = 0.02;  // the slab's top over the ground it was levelled to
const V3 kCast(0.54, 0.53, 0.50), kJoint(0.36, 0.36, 0.35), kLine(0.86, 0.85, 0.80), kHazard(0.90, 0.70, 0.10);

// A flat painted strip on the slab's top (axis-aligned in the frame): centre (x, z), sizes along x and z.
void strip(B& b, double x, double z, double sx, double sz, double y = kTop) { b.box(V3(x, y + 0.0015, z), V3(sx, 0.003, sz)); }

// A flat painted triangle (up-facing whatever order the points come in).
void flatTri(B& b, V3 a, V3 c, V3 d, double y = kTop) {
    a.y = c.y = d.y = y + 0.003;
    if (cross(c - a, d - a).y < 0) std::swap(c, d);
    b.tri(a, c, d);
}

void slab(B& b, const P& p) {
    const double back = p("back"), front = p("front"), half = p("half"), ramp = p("ramp"), drop = p("drop");
    const int slots = std::max(1, p.i("slots")), cols = std::max(1, p.i("cols"));
    const double len = back + front;

    // The slab.
    b.style(kCast, CONCRETE);
    b.box(V3((front - back) / 2, kTop - 0.21, 0), V3(len, 0.42, 2 * half), 0.012);

    // Contraction joints every ~4.5 m, across and along (dark cuts).
    b.color(kJoint).mat(MATTE);
    const int nx = std::max(1, (int)std::lround(len / 4.5)), nz = std::max(1, (int)std::lround(2 * half / 4.5));
    for (int i = 1; i < nx; i++) strip(b, -back + len * i / nx, 0, 0.025, 2 * half - 0.04);
    for (int i = 1; i < nz; i++) strip(b, (front - back) / 2, -half + 2 * half * i / nz, len - 0.04, 0.025);

    // Paint: a hazard stripe along the edges, an inch of margin in from it; a bay for each place on the slab.
    b.color(kHazard);
    strip(b, -back + 0.45, 0, 0.14, 2 * half - 0.9);
    strip(b, (front - back) / 2, half - 0.45, len - 0.9, 0.14);
    strip(b, (front - back) / 2, -half + 0.45, len - 0.9, 0.14);
    b.color(kLine);
    for (int k = 0; k < slots && slots > 1; k++) {
        double u, v;
        padSlotUV(cols, k, u, v);
        const double L = 5.9, W = 2.7, t = 0.07;  // the bay: a truck's length and width and a bit
        strip(b, u + L / 2 - 0.2, v, t, W);       // across the front
        strip(b, u - L / 2 - 0.2, v, t, W);       // and the back
        strip(b, u - 0.2, v + W / 2, L, t);
        strip(b, u - 0.2, v - W / 2, L, t);
    }
    // A direction arrow on the front edge of the slab, between the bays and the ramp.
    const double ax = front - 2.2;
    strip(b, ax - 0.35, 0, 1.1, 0.26);
    flatTri(b, V3(ax + 0.2, 0, -0.62), V3(ax + 0.2, 0, 0.62), V3(ax + 1.35, 0, 0));
    // The line at the lip of the slab.
    strip(b, front - 0.45, 0, 0.12, 2 * half - 0.9);

    if (ramp < 0.5) return;
    // The ramp: a wedge from the slab's front edge, sloping down by `drop` over `ramp` m.
    const double yEnd = kTop - drop;
    b.style(kCast, CONCRETE);
    b.extrude({V2(front, kTop), V2(front + ramp, yEnd), V2(front + ramp, yEnd - 0.15), V2(front, kTop - 0.42)}, 2 * half, 0);
    // Its joints and edge stripes, laid along the slope.
    const double ang = std::atan2(-drop, ramp) * 180 / M_PI, slope = std::hypot(ramp, drop);
    b.push();
    b.move(front, kTop, 0);
    b.pitch(ang);
    b.color(kJoint).mat(MATTE);
    for (double x = 3.0; x < slope - 0.5; x += 3.0) b.box(V3(x, 0.0015, 0), V3(0.025, 0.003, 2 * half - 0.04));
    b.color(kHazard);
    b.box(V3(slope / 2, 0.0015, half - 0.45), V3(slope - 0.1, 0.003, 0.14));
    b.box(V3(slope / 2, 0.0015, -half + 0.45), V3(slope - 0.1, 0.003, 0.14));
    b.pop();
}

Reg spawnPad({"spawn_pad", "SPAWN PAD", "TRAIL AND ROAD", "The poured concrete slab vehicles start on, with a ramp down to the ground ahead of it", 12,
              {
                  {"POURED SLAB", "Light concrete with cut joints, a hazard edge stripe, painted bays and a ramp",
                   {num("back", "BACK", 5, 3, 40, 0.5), num("front", "FRONT", 5.5, 3, 12, 0.5), num("half", "HALF WIDTH", 3.6, 3, 16, 0.2), num("ramp", "RAMP", 8, 0, 30, 1),
                    num("drop", "RAMP DROP", 0.4, -3, 6, 0.1), num("slots", "PLACES", 1, 1, 12, 1), num("cols", "COLUMNS", 1, 1, 4, 1)},
                   slab},
              }});

}  // namespace
