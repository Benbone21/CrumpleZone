// The driver's seat and its steering wheel (people.h): split from people.cpp so the posed body builds
// without the car visuals (the tests link people.cpp alone).
#include <cmath>
#include <vector>

#include "carvisual.h"
#include "people.h"

namespace people {

namespace {
const double kSeatSlide = 0.09;  // a driver would sit this far ahead of the seat's hip point (up to the wheel): the eyes are over that
}

// ---- In the seat ----

bool seatOf(const Car& c, const VisualTemplate& t, Seat& s) {
    if (!t.cockpit.on) return false;
    const Frame f = c.frame();
    s.fwd = f.fwd, s.up = f.up, s.right = f.left * -1.0;
    s.hip = boundPoint(c, t.cockpit.hip);
    s.wheel = boundPoint(c, t.cockpit.wheel);
    s.wheelR = t.cockpit.wheelR, s.tilt = t.cockpit.tilt;
    return std::isfinite(s.hip.x + s.wheel.x);
}

V3 seatEye(const Seat& s) { return s.hip + s.up * 0.735 + s.fwd * (kSeatSlide + 0.05); }

double wheelTurnOf(const Car& c) { return clampd(c.steerInput, -1.0, 1.0) * 4.2; }  // (about 1.3 turns to full lock)

namespace {
struct WheelFrame {
    V3 c, axis, u, r;  // middle; toward the driver; 12 o'clock; 3 o'clock (seen from the seat)
};
WheelFrame wheelFrame(const Seat& s) {
    WheelFrame w;
    w.c = s.wheel;
    w.axis = norm(s.fwd * -std::cos(s.tilt) + s.up * std::sin(s.tilt));
    w.u = norm(s.fwd * std::sin(s.tilt) + s.up * std::cos(s.tilt));
    w.r = s.right;
    return w;
}
}  // namespace

void steeringWheel(const Seat& s, double wheelTurn, mk::Mesh& out) {
    const WheelFrame w = wheelFrame(s);
    // Turned: 12 o'clock moves to the left (counter-clockwise from the seat) for a left turn.
    const V3 u = rotateAxis(w.u, w.axis, wheelTurn), r = cross(u, w.axis);
    // (u, r: up and right on the wheel's face as the driver sees it; axis toward the driver.)
    const size_t start = out.v.size();
    mk::B b(out);
    const double R = s.wheelR;
    std::vector<V3> rim;
    for (int k = 0; k < 24; k++) {
        const double a = 2 * M_PI * k / 24;
        rim.push_back(w.c + (u * std::cos(a) + r * std::sin(a)) * R);
    }
    b.style(V3(0.035, 0.035, 0.038), mk::MATTE);
    b.tube(rim, 0.017, 7, true, false);
    // Three spokes dished back to the hub, the hub and its pad.
    const V3 hub = w.c - w.axis * 0.045;
    b.style(V3(0.16, 0.16, 0.17), mk::METAL);
    for (double a : {-M_PI / 2, M_PI / 2, M_PI}) {
        const V3 dir = u * std::cos(a) + r * std::sin(a);
        b.beam(hub + dir * 0.035, w.c + dir * (R - 0.012), 0.034, 0.008, w.axis);
    }
    b.style(V3(0.05, 0.05, 0.055), mk::MATTE);
    b.cone(hub - w.axis * 0.03, hub + w.axis * 0.025, 0.05, 0.046, 10, true);
    b.style(V3(0.55, 0.55, 0.57), mk::METAL);
    b.cone(hub + w.axis * 0.025, hub + w.axis * 0.03, 0.02, 0.018, 8, true);  // the badge
    lastTris += (int)((out.v.size() - start) / 3);
}

}  // namespace people
