#include "walker.h"

#include <algorithm>
#include <cmath>

namespace {

const double kFootR = 0.25, kChestR = 0.3, kChestH = 1.25;

// Keeps a sphere (center c, radius r) out of the world's solid things; returns the push.
V3 pushOut(const World& w, const V3& c, double r, bool* ground) {
    V3 push;
    Contact ct;
    if (w.contact(c, r, nullptr, 0, ct) && ct.pen > 0) {
        push += ct.n * ct.pen;
        if (ground && ct.n.y > 0.55) *ground = true;
    }
    // Tree trunks and posts.
    std::vector<int> near;
    w.polesNear(c - V3(r, r, r), c + V3(r, r, r), near);
    for (int k : near) {
        const Pole& p = w.poles[k];
        if (c.y < p.y0 || c.y > p.y1) continue;
        const V3 d(c.x - p.x, 0, c.z - p.z);
        const double dist = len(d), minD = p.r + r;
        if (dist < minD && dist > 1e-6) push += d * ((minD - dist) / dist);
    }
    return push;
}

}  // namespace

void Walker::exitCar(const World& w, const Car& c) {
    const Frame f = c.frame();
    const V3 fwd = norm(V3(f.fwd.x, 0, f.fwd.z)), left(fwd.z, 0, -fwd.x);  // (up x forward)
    // The driver's side, past the mirror; if that's blocked (a rock face), the other side.
    for (int side : {1, -1}) {
        const V3 at = f.com + left * (side * (c.spec->width / 2 + 0.7)) + fwd * 0.3;
        V3 q(at.x, w.clearY(at.x, at.z), at.z);
        Contact ct;
        if (!w.contact(q + V3(0, kChestH, 0), kChestR, nullptr, 0, ct) || side == -1) {
            p = V3(at.x, w.height(at.x, at.z) + 0.02, at.z);
            break;
        }
    }
    yaw = std::atan2(-fwd.z, fwd.x);
    v = f.vel * 0.5;
    active = true;
    grounded = false;
}

int Walker::carToEnter(const Sim& sim, double reach) const {
    int best = -1;
    double bestD = reach;
    for (int i = 0; i < (int)sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if (c.remote || c.isProp()) continue;
        const Frame f = c.frame();
        // Distance to its outline (a box the size of the car), not its middle.
        const V3 d = p + V3(0, 1, 0) - f.com;
        const double ax = std::max(0.0, std::fabs(dot(d, f.fwd)) - c.spec->length / 2), az = std::max(0.0, std::fabs(dot(d, f.left)) - c.spec->width / 2);
        const double dist = std::hypot(ax, az);
        if (dist < bestD) bestD = dist, best = i;
    }
    return best;
}

void Walker::step(const World& w, const Sim& sim, double dt, double fwd, double side, bool run, bool jump, double viewYaw) {
    if (!active) return;
    // Where the legs want to go.
    // (Left is up x forward: with +x ahead and +y up, -z is on the left. A and D were swapped.)
    const V3 f(std::cos(viewYaw), 0, -std::sin(viewYaw)), l(f.z, 0, -f.x);
    V3 want = f * fwd + l * side;
    if (len(want) > 1) want = norm(want);
    const double ground = w.height(p.x, p.z);
    const double water = w.waterLevel(p.x, p.z);
    inWater = std::max(0.0, water - std::max(ground, p.y));
    const bool swimming = inWater > 1.2;
    if (carrying) run = jump = false;  // (a 40 kg wheel in your arms)
    double speed = carrying ? 1.2 : run ? 4.6 : 1.7;
    if (inWater > 0.3) speed *= clampd(1.0 - (inWater - 0.3) * 0.5, 0.35, 1.0);  // wading
    if (swimming) speed = 1.0;
    // Soft ground drags at the boots.
    const Surface s = w.surfaceAt(p.x, p.z);
    if (s == SURF_MUD) speed *= 0.6;
    else if (s == SURF_SAND || s == SURF_SNOW) speed *= 0.8;
    // Steering the body round to the way it walks.
    if (len(want) > 0.05) {
        const double target = std::atan2(-want.z, want.x);
        yaw += wrapAngle(target - yaw) * std::min(1.0, dt * 10);
    }
    // Ground: the legs set the speed (quickly, less so in the air or ice); off it, gravity.
    const double grip = !grounded ? 0.15 : s == SURF_ICE ? 0.12 : 1.0;
    const V3 vh(v.x, 0, v.z), target = want * speed;
    const V3 dv = (target - vh) * std::min(1.0, dt * 10 * grip);
    v.x += dv.x, v.z += dv.z;
    if (swimming) {
        v.y += ((water - 1.3) - p.y) * dt * 4 - v.y * dt * 3;  // float with the head out
        const V3 flow = w.waterFlow(p.x, p.z);
        v.x += (flow.x - v.x) * dt * 0.8, v.z += (flow.z - v.z) * dt * 0.8;
    } else {
        v.y -= 9.81 * dt;
    }
    if (jump && grounded && !swimming) v.y = 4.2;
    p += v * dt;
    // The ground and everything solid: the feet stay on top, the chest is kept out.
    grounded = false;
    const double gy = w.height(p.x, p.z);
    if (p.y < gy) {
        // Steep ground: you slide down it instead of standing.
        const V3 n = w.normal(p.x, p.z);
        p.y = gy;
        if (n.y > 0.7) {
            grounded = true;
            if (v.y < 0) v.y = 0;
        } else {
            v += V3(n.x, 0, n.z) * (9.81 * dt);
            if (v.y < 0) v.y *= 0.3;
            grounded = n.y > 0.5;
        }
    }
    for (int it = 0; it < 3; it++) {
        bool g = false;
        const V3 feet = pushOut(w, p + V3(0, kFootR, 0), kFootR, &g), chest = pushOut(w, p + V3(0, kChestH, 0), kChestR, nullptr);
        // Feet pushed up (onto a rock or log): step up onto it; sideways: blocked.
        p += feet + V3(chest.x, 0, chest.z);
        if (g) {
            grounded = true;
            if (v.y < 0) v.y = 0;
        }
        if (len(feet) + len(chest) < 1e-4) break;
    }
    // Trucks: keep out of their outlines (walk round them).
    for (const Car& c : sim.cars) {
        const Frame cf = c.frame();
        const V3 d = p + V3(0, 0.9, 0) - cf.com;
        if (std::fabs(dot(d, cf.up)) > 1.6) continue;
        const double hl = c.spec->length / 2 + 0.25, hw = c.spec->width / 2 + 0.25;
        const double x = dot(d, cf.fwd), z = dot(d, cf.left);
        if (std::fabs(x) < hl && std::fabs(z) < hw) {
            const double ox = hl - std::fabs(x), oz = hw - std::fabs(z);
            if (ox < oz) p += cf.fwd * (x > 0 ? ox : -ox);
            else p += cf.left * (z > 0 ? oz : -oz);
        }
    }
    p.x = clampd(p.x, -World::kHalfSize + 2, World::kHalfSize - 2);
    p.z = clampd(p.z, -World::kHalfSize + 2, World::kHalfSize - 2);
    const double moving = len(V3(v.x, 0, v.z));
    if (grounded || swimming) stride += moving * dt * 3.2;
}
