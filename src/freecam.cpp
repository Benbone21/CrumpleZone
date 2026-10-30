#include "freecam.h"

#include <algorithm>
#include <cmath>

#include "softbody.h"
#include "vehicles.h"

namespace freecam {

const std::vector<int>& keys() {
    static const std::vector<int> k = {K_FORWARD, K_BACK, K_LEFT, K_RIGHT, K_DOWN, K_UP, K_GRAB, K_LET_GO, K_TURN,
                                       K_SPAWN, K_DRIVE, K_THROUGH, K_SLOWER, K_FASTER, K_EXIT};
    return k;
}

bool passesThrough(int sc) {
    switch (sc) {
        case 60:  // F3: debug info
        case 62:  // F5: load the new build
        case 66:  // F9: the full HUD (held)
        case 67:  // F10: hide the HUD
        case 68:  // F11: fullscreen
        case 69:  // F12: the dev menu
        case 53:  // grave: DEV KEYS / GAMEPLAY KEYS
            return true;
        default: return false;
    }
}

int pickVehicle(const Sim& sim, const V3& eye, const V3& dir, double maxDist, int skip, double* at) {
    int best = -1;
    double bestT = maxDist;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (i == skip || c.isProp() || c.nodes.empty()) continue;
        // (The ray against its box, slab by slab.)
        double t0 = 0, t1 = bestT;
        const double o[3] = {eye.x, eye.y, eye.z}, d[3] = {dir.x, dir.y, dir.z};
        const double lo[3] = {c.bbMin.x, c.bbMin.y, c.bbMin.z}, hi[3] = {c.bbMax.x, c.bbMax.y, c.bbMax.z};
        bool hit = true;
        for (int a = 0; a < 3 && hit; a++) {
            if (std::fabs(d[a]) < 1e-12) {
                hit = o[a] >= lo[a] && o[a] <= hi[a];
                continue;
            }
            double ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta), t1 = std::min(t1, tb);
            hit = t0 <= t1;
        }
        if (hit && t0 < bestT) best = i, bestT = t0;
    }
    if (at) *at = bestT;
    return best;
}

bool groundHit(const World& w, const V3& eye, const V3& dir, double maxDist, V3& out) {
    double prev = 0;
    if (eye.y < w.height(eye.x, eye.z)) {  // (flying under the ground: the first way back out of it)
        for (double t = 0.25; t < maxDist; t += std::max(0.25, t * 0.01)) {
            const V3 p = eye + dir * t;
            if (p.y >= w.height(p.x, p.z)) return out = p, true;
        }
        return false;
    }
    for (double t = 0.25; t < maxDist; t += std::max(0.25, t * 0.01)) {
        const V3 p = eye + dir * t;
        if (p.y < w.height(p.x, p.z)) {
            double a = prev, b = t;  // (between the last step above it and this one below: halved down to a centimetre)
            while (b - a > 0.01) {
                const double m = (a + b) * 0.5;
                const V3 q = eye + dir * m;
                (q.y < w.height(q.x, q.z) ? b : a) = m;
            }
            out = eye + dir * b;
            out.y = w.height(out.x, out.z);
            return true;
        }
        prev = t;
    }
    return false;
}

bool carriable(const Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size()) return false;
    const Car& c = sim.cars[car];
    return !c.isProp() && !c.remote && !c.nodes.empty() && c.finite();
}

namespace {
Quat yawQuat(double yaw) { return Quat::axisAngle(V3(0, 1, 0), yaw); }
Quat inverse(const Quat& q) { return Quat{q.w, -q.x, -q.y, -q.z}; }
}  // namespace

bool pickUp(Sim& sim, int car, Carry& cr) {
    if (!carriable(sim, car)) return false;
    Car& c = sim.cars[car];
    c.wake();  // (a traffic proxy goes back to being a whole body first)
    sim.dropRopes(car);
    const Frame fr = c.frame();
    double yaw = std::atan2(-fr.fwd.z, fr.fwd.x);
    if (!std::isfinite(yaw)) yaw = 0;
    // Its attitude now (as placeUpright takes it): the body's best-fit turn, or just its heading without one.
    const Quat cur = c.chassis.nodes.empty() ? yawQuat(yaw) : c.chassis.rot;
    const Quat inv = inverse(cur);
    cr = Carry();
    cr.car = car;
    cr.yaw = yaw;
    cr.spec = c.spec;
    cr.nodes = c.nodes.size();
    cr.torn.assign(c.nodes.size(), 0);
    for (const Panel& p : c.panels)
        if (p.state == 2)
            for (int i : p.panelNodes)
                if (i >= 0 && i < (int)cr.torn.size()) cr.torn[i] = 1;
    cr.local.resize(c.nodes.size());
    for (size_t i = 0; i < c.nodes.size(); i++) cr.local[i] = inv.rotate(c.nodes[i].p - fr.com);
    for (int k = 0; k < 2; k++) cr.railRel[k] = inv * c.railFrame[k].rot;
    cr.knuckleRel.clear();
    for (const Car::Knuckle& kn : c.knuckles) cr.knuckleRel.push_back(inv * kn.frame.rot);
    c.throttle = c.brake = 0;
    return true;
}

bool valid(const Sim& sim, const Carry& cr) {
    return cr.on() && cr.car < (int)sim.cars.size() && sim.cars[cr.car].spec == cr.spec && sim.cars[cr.car].nodes.size() == cr.nodes;
}

void hold(Sim& sim, const Carry& cr, const V3& at) {
    if (!valid(sim, cr)) return;
    Car& c = sim.cars[cr.car];
    const Quat q = yawQuat(cr.yaw);
    // (translate first: the wheels' contact points, its autopilot's route and the contact grids' stamps go with it)
    c.translate(at - c.frame().com);
    for (size_t i = 0; i < c.nodes.size(); i++) {
        Node& n = c.nodes[i];
        if (!cr.torn[i]) n.p = at + q.rotate(cr.local[i]);
        n.v = V3(), n.f = V3();
    }
    if (!c.chassis.nodes.empty()) {
        c.chassis.rot = q;
        for (int k = 0; k < 2; k++) c.railFrame[k].rot = q * cr.railRel[k], c.railFrame[k].rot.normalize();
        for (size_t k = 0; k < c.knuckles.size() && k < cr.knuckleRel.size(); k++) c.knuckles[k].frame.rot = q * cr.knuckleRel[k], c.knuckles[k].frame.rot.normalize();
    }
    for (Wheel& wh : c.wheels) wh.omega = 0, wh.stickX = wh.stickY = 0;
    c.sleeping = false, c.stillTime = 0;
    c.updateBounds();
}

V3 hangPoint(const V3& eye, double lookYaw, double ahead, double below) {
    return eye + V3(std::cos(lookYaw), 0, -std::sin(lookYaw)) * ahead - V3(0, below, 0);
}

void dropAt(Sim& sim, Carry& cr, double x, double z) {
    if (valid(sim, cr)) sim.teleportCar(cr.car, x, z, cr.yaw);
    cr = Carry();
}

void letGo(Sim& sim, Carry& cr) {
    if (valid(sim, cr)) {
        Car& c = sim.cars[cr.car];
        for (Node& n : c.nodes) n.v = V3(), n.f = V3();
        c.wake();
    }
    cr = Carry();
}

int spawnAt(Sim& sim, const VehicleSpec& vs, double x, double z, double yaw) {
    if (sim.vehicleCount() >= Sim::kMaxVehicles || (int)sim.cars.size() >= Sim::kMaxBodies) return -1;
    sim.addCar(vs, V3(x, sim.world.clearY(x, z) + 0.05, z), yaw, sim.nextColor());
    const int i = (int)sim.cars.size() - 1;
    sim.cars[i].handbrake = 1;
    sim.teleportCar(i, x, z, yaw);  // (upright on whatever is there, clear of trunks)
    return i;
}

}  // namespace freecam
