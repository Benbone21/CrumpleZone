// The chase / cockpit camera's auto-recentre (camhold.h): a view the player turned is held while
// the car is parked or crawling, and eases back (not snaps) only while driving.
#include <cmath>

#include "../src/camhold.h"
#include "harness.h"

namespace {
// Runs `seconds` at 60 Hz with the look control let go long ago; returns the yaw left.
double run(camhold::Hold& h, double seconds, double speed, double throttle, double yaw0, double* pitch = nullptr, double* firstStep = nullptr) {
    double yaw = yaw0, p0 = 0.0, &pp = pitch ? *pitch : p0;
    for (int i = 0; i < (int)std::lround(seconds * 60); i++) {
        h.step(1.0 / 60, camhold::driving(speed, throttle), true, true, true, yaw, pp, -0.05);
        if (i == 0 && firstStep) *firstStep = yaw;
    }
    return yaw;
}
}  // namespace

SCENARIO(camHold, "camera/hold", "Chase camera turned by the player: held indefinitely while parked or crawling (under 3 km/h, no throttle), eased back gradually once driving, pitch the same") {
    camhold::Hold h;
    double pitch = -0.6;
    ctx.check("parked: 60 s later the yaw is exactly as set", run(h, 60, 0, 0, 2.0, &pitch) == 2.0);
    ctx.check("parked: pitch held too", pitch == -0.6);
    ctx.check("crawling at 2 km/h, no throttle: still held", run(h, 20, 2.0 / 3.6, 0, 2.0) == 2.0);
    camhold::Hold g;
    double first = 0, p2 = -0.6;
    const double after = run(g, 0.5, 10.0, 0.5, 2.0, &p2, &first);
    ctx.check("driving: does not snap on the first frame", first > 1.99, std::to_string(first));
    ctx.check("driving: has eased partway after 0.5 s", after < 2.0 && after > 1.0, std::to_string(after));
    ctx.check("driving: pitch comes back up", p2 > -0.6);
    ctx.check("driving: fully recentred after 10 s", std::fabs(run(g, 10, 10.0, 0.5, after)) < 0.01);
    camhold::Hold t;
    ctx.check("throttle at a standstill counts as driving", run(t, 5, 0, 0.5, 2.0) < 1.9);
    camhold::Hold s;
    run(s, 5, 10.0, 0.5, 2.0);
    run(s, 1, 0, 0, 1.0);
    ctx.check("stopping resets the pull", s.weight == 0);
    ctx.check("look control still held: no recentring even when driving", [] {
        camhold::Hold k;
        double y = 2.0, p = 0.2;
        for (int i = 0; i < 300; i++) k.step(1.0 / 60, true, false, true, true, y, p, -0.05);
        return y == 2.0;
    }());
}
