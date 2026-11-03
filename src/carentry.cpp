#include "carentry.h"

#include <cmath>
#include <string>

#include "interact.h"

namespace carentry {

Verdict verdict(const Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return NONE;
    const Car& c = sim.cars[car];
    if (c.isProp()) return NONE;
    const double speed = len(c.frame().vel);
    if (c.remote) return c.occupied ? RIDE : speed > 1.0 ? WAIT : TAKE;
    if (c.ai && speed > 0.5) return WAIT;  // (traffic: only once it's stopped)
    if (car != sim.player && speed > 2.0) return WAIT;
    return DRIVE;
}

int nearest(const Sim& sim, const V3& feet, double reach) {
    int best = -1;
    double bestD = reach;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (c.isProp()) continue;
        const Frame f = c.frame();
        const V3 d = feet + V3(0, 1, 0) - f.com;  // (to its outline, a box the size of the car)
        const double ax = std::max(0.0, std::fabs(dot(d, f.fwd)) - c.spec->length / 2), az = std::max(0.0, std::fabs(dot(d, f.left)) - c.spec->width / 2);
        const double dist = std::hypot(ax, az);
        if (dist < bestD) bestD = dist, best = i;
    }
    return best;
}

V3 door(const Car& c, bool passenger) {
    const Frame f = c.frame();
    const double side = passenger ? -1 : 1;  // (+ the frame's left: the driver's)
    // The front door on that side (carbuild: "LEFT DOOR" / "RIGHT DOOR", whichever sits on this side): the handle near its
    // back edge, a hand below the window line, on its outer skin. Torn off: the opening it left (its body nodes).
    int best = -1;
    double bestLat = 0;
    for (size_t i = 0; i < c.panels.size(); i++) {
        const Panel& p = c.panels[i];
        const std::string nm = p.name ? p.name : "";
        if (nm.find("DOOR") == std::string::npos || nm.find("REAR") != std::string::npos || p.panelNodes.empty()) continue;
        V3 mid;
        for (int n : p.panelNodes) mid += c.nodes[n].p;
        mid = mid * (1.0 / p.panelNodes.size());
        const double lat = dot(mid - f.com, f.left) * side;
        if (lat > bestLat) bestLat = lat, best = (int)i;
    }
    if (best >= 0) {
        const Panel& p = c.panels[best];
        const bool torn = p.state == 2;
        const std::vector<int>& ns = torn ? p.bodyNodes : p.panelNodes;
        // The window frame (its nodes pressed on the seal: PIN_SEAL) is above the handle: the door's skin below it is what counts.
        std::vector<char> frame(ns.size(), 0);
        for (const Pin& pn : c.pins)
            if (pn.kind == PIN_SEAL)
                for (size_t k = 0; k < p.panelNodes.size(); k++)
                    if (p.panelNodes[k] == pn.panel) frame[k] = 1;
        double back = 1e9, front = -1e9, lo = 1e9, hi = -1e9, out = -1e9;
        for (size_t k = 0; k < ns.size(); k++) {
            if (frame[k]) continue;
            const int n = ns[k];
            const V3 d = c.nodes[n].p - f.com;
            back = std::min(back, dot(d, f.fwd)), front = std::max(front, dot(d, f.fwd));
            lo = std::min(lo, dot(d, f.up)), hi = std::max(hi, dot(d, f.up)), out = std::max(out, dot(d, f.left) * side);
        }
        const double x = back + std::min(0.12, (front - back) * 0.2), y = std::max(lo + (hi - lo) * 0.55, hi - 0.1);
        return f.com + f.fwd * x + f.up * y + f.left * (side * (out + 0.03));
    }
    // No doors (a buggy, a side-by-side): the cage's side by the seat, at the rail you step in over.
    double out = c.spec->width / 2, top = 0.1;
    bool any = false;
    for (const Node& n : c.nodes) {
        const V3 d = n.p - f.com;
        if (std::fabs(dot(d, f.fwd)) > 0.45 || dot(d, f.up) > 0.6 || dot(d, f.left) * side < 0.2) continue;
        const double o = dot(d, f.left) * side;
        if (!any || o > out) out = o, any = true;
        top = std::max(top, dot(d, f.up));
    }
    return f.com + f.fwd * (-0.1) + f.up * std::min(top, 0.35) + f.left * (side * (out + 0.03));
}

V3 acrossCab(const Car& c, const V3& p) {
    const Frame f = c.frame();
    return p - f.left * (2 * dot(p - f.com, f.left));
}

bool option(const Sim& sim, int ci, InteractOption& o) {
    const Verdict v = verdict(sim, ci);
    if (v == NONE) return false;
    const Car& c = sim.cars[ci];
    o = InteractOption();
    o.car = ci;
    o.hold = 0;  // (a tap, J or E: prompts.cpp keys it on J)
    o.kind = v == RIDE ? IA_RIDE_ALONG : IA_ENTER_CAR;
    o.at = door(c, v == RIDE);
    const std::string name = c.spec->name;
    o.prompt = v == RIDE ? "RIDE ALONG IN THE " + name : "GET IN THE " + name;
    if (v == WAIT) o.blocked = true, o.prompt += ": WAIT FOR IT TO STOP";
    return true;
}

}  // namespace carentry
