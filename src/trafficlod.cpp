// Traffic LOD: an AI car driving the loop road with nobody near is a rigid proxy instead of a soft body. Its nodes
// are frozen in the shape they had (damage included) relative to the chassis frame, and each frame the whole frame
// is re-placed on a pose that follows the road: position along it, lane offset, heading, and pitch/roll/height from
// the terrain under it. Speed comes from the same bend limit and car-following law the driver uses. Velocities and
// wheel spin are kept right every frame, so when something comes near (any body, player, walker or camera within the
// wake radius, which grows with closing speed) wake() hands it back to the full sim with nothing to settle.
#include <algorithm>
#include <cmath>

#include "softbody.h"

namespace {
double wrapPi(double a) {
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return a;
}

// Where the proxy is on the road: segment idx..idx+dir, `off` m along it, `lat` m right of the line, heading
// `head` off the road's. The pose it gives is the car resting on the terrain there.
struct RoadPose {
    V3 pos;
    Quat q;
    double yaw = 0;
};

struct RoadWalk {
    const std::vector<V3>& road;
    int n, dir;
    RoadWalk(const std::vector<V3>& r, int d) : road(r), n((int)r.size()), dir(d) {}
    int wrap(int i) const { return ((i % n) + n) % n; }
    double segLen(int i) const {
        const V3 d = road[wrap(i + dir)] - road[i];
        return std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-6);
    }
    V3 tangent(int i) const {
        const V3 d = road[wrap(i + dir)] - road[i];
        return norm(V3(d.x, 0, d.z));
    }
    void advance(int& i, double& off, double dist) const {
        off += dist;
        for (int k = 0; k < n && off >= segLen(i); k++) off -= segLen(i), i = wrap(i + dir);
        for (int k = 0; k < n && off < 0; k++) i = wrap(i - dir), off += segLen(i);
    }
    // The road point `off` m along segment i and `lat` m right of the line (x, z), and the road's heading there.
    void at(int i, double off, double lat, double& x, double& z, double& yaw) const {
        const V3 t = tangent(i), right(-t.z, 0, t.x);
        const V3 a = road[i], b = road[wrap(i + dir)];
        const double f = off / segLen(i);
        x = a.x + (b.x - a.x) * f + right.x * lat, z = a.z + (b.z - a.z) * f + right.z * lat;
        yaw = std::atan2(-t.z, t.x);
    }
};

// The car resting on the terrain at (x, z) facing yaw: height from the ground under its wheels, tilted to its slope.
RoadPose groundPose(const World& w, double x, double z, double yaw) {
    RoadPose rp;
    rp.yaw = yaw;
    const V3 fwd(std::cos(yaw), 0, -std::sin(yaw)), left(-std::sin(yaw), 0, -std::cos(yaw));
    auto h = [&](double u, double v) { return w.height(x + fwd.x * u + left.x * v, z + fwd.z * u + left.z * v); };
    const double hF = h(1.5, 0), hR = h(-1.5, 0), hL = h(0, 0.9), hRt = h(0, -0.9);
    const V3 nrm = norm(V3(0, 1, 0) - fwd * ((hF - hR) / 3.0) - left * ((hL - hRt) / 1.8));
    const V3 ax = cross(V3(0, 1, 0), nrm);
    const double s = len(ax);
    const Quat yawQ = Quat::axisAngle(V3(0, 1, 0), yaw);
    rp.q = s < 1e-9 ? yawQ : Quat::axisAngle(ax / s, std::atan2(s, nrm.y)) * yawQ;
    rp.pos = V3(x, 0.25 * (hF + hR + hL + hRt), z);
    return rp;
}
}  // namespace

int Sim::proxyCount() const {
    int k = 0;
    for (const Car& c : cars) k += c.proxy;
    return k;
}

void Car::endProxy() {
    proxy = false;
    lod = Proxy();
    lodCalm = 0;
    aiLastAlong = -1;
    if (grid) grid->stamp = -1;
    nodeGridStamp = -1;
}

// Nothing close enough to touch it soon: every other body (proxies excepted), the on-foot position and the camera
// (lodWatch) outside the wake radius, which grows with the speed the two close at. Entering adds a margin, and
// something that must be far to be a "player" (not an AI car, which brakes for the proxy itself) is beyond kLodEnter.
bool Sim::lodClear(int self, double vSelf, bool entering) const {
    const V3 me = aiFrames[self].com;
    const double margin = entering ? kLodHyst : 0;
    auto far = [&](const V3& p, double v, double base, bool player) {
        const V3 d = p - me;
        const double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        double R = base + kLodLead * (v + vSelf) + margin;
        if (entering && player) R = std::max(R, kLodEnter);
        return dist > R;
    };
    for (size_t o = 0; o < cars.size(); o++) {
        if ((int)o == self || cars[o].proxy) continue;
        const bool ai = cars[o].ai && !cars[o].remote && !cars[o].isProp();
        if (!far(aiFrames[o].com, len(aiFrames[o].vel), ai ? kLodAiWake : kLodWake, !ai)) return false;
    }
    for (const LooseWheel& lw : looseWheels)
        if (!far(lw.p, len(lw.v), kLodWake, false)) return false;
    for (const V3& p : lodWatch)
        if (!far(p, 0, kLodWake, true)) return false;
    return true;
}

// Takes cars[self] (just driven by the AI) over as a proxy if it is a plain case: upright, in its lane and heading
// with the road, not backing up or stuck, no lines or jack on it, and clear of everything for a second.
bool Sim::lodEnter(int self, double dt) {
    Car& c = cars[self];
    const auto& road = world.loopRoad;
    const RoadWalk rw(road, c.aiDir);
    const Frame& fr = aiFrames[self];
    bool ok = !c.remote && !c.proxy && !c.sleeping && !c.pilot.active && !c.aiPassing && c.aiReverseTime <= 0 && c.aiStuckTime < 0.5 &&
              c.heldHere <= 0 && !c.jackPlaced && c.gate.dir == 0 && !c.chassis.nodes.empty() && fr.up.y > 0.85 && c.aiIndex >= 0 &&
              std::fabs(c.aiLane - 2.6) < 0.05 && self != player;
    for (const Rope& r : ropes)
        if ((r.a.kind == ANCHOR_CAR && r.a.car == self) || (r.b.kind == ANCHOR_CAR && r.b.car == self)) ok = false;
    V3 fh = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    const double v = dot(fr.vel, fh);
    ok = ok && v > -0.1 && std::isfinite(v);
    // Where it is on the road, and how it sits there.
    int idx = c.aiIndex;
    double off = 0, lat = 0, head = 0;
    if (ok) {
        const V3 t = rw.tangent(idx), right(-t.z, 0, t.x);
        rw.advance(idx, off, (fr.com.x - road[idx].x) * t.x + (fr.com.z - road[idx].z) * t.z);
        const V3 t2 = rw.tangent(idx), r2(-t2.z, 0, t2.x);
        const V3 a = road[idx], b = road[rw.wrap(idx + c.aiDir)];
        const double f = off / rw.segLen(idx);
        lat = (fr.com.x - (a.x + (b.x - a.x) * f)) * r2.x + (fr.com.z - (a.z + (b.z - a.z) * f)) * r2.z;
        head = wrapPi(std::atan2(-fr.fwd.z, fr.fwd.x) - std::atan2(-t2.z, t2.x));
        (void)right;
        ok = std::fabs(lat - 2.6) < 2.0 && std::fabs(head) < 0.5;
    }
    ok = ok && lodClear(self, std::max(v, 0.0), true);
    c.lodCalm = ok ? c.lodCalm + dt : 0;
    if (!ok || c.lodCalm < 1.0) return false;

    // Freeze it: its nodes in its own chassis frame, and that frame against the road pose it will ride.
    Car::Proxy& L = c.lod;
    const Quat A = c.chassis.rot;
    double rx, rz, ry;
    rw.at(idx, off, lat, rx, rz, ry);
    const RoadPose p0 = groundPose(world, rx, rz, ry + head);
    L.qd = Quat{p0.q.w, -p0.q.x, -p0.q.y, -p0.q.z} * A;
    L.dd = p0.q.unrotate(fr.com - p0.pos);
    L.local.resize(c.nodes.size());
    for (size_t k = 0; k < c.nodes.size(); k++) L.local[k] = A.unrotate(c.nodes[k].p - fr.com);
    L.prevCom = fr.com;
    L.prevQ = A;
    L.idx = idx, L.off = off, L.lat = lat, L.head = head, L.yaw = p0.yaw, L.fx = rx, L.fz = rz;
    L.speed = std::max(v, 0.0);
    c.proxy = c.sleeping = true;
    c.stillTime = 0;
    c.sleptControls[0] = c.throttle, c.sleptControls[1] = c.brake, c.sleptControls[2] = c.handbrake, c.sleptControls[3] = c.steer;
    c.aiLastAlong = -1;
    return true;
}

// One frame of a proxy: speed from the bends ahead and whoever is in front (as aiDrive), along the road, and the
// nodes re-placed rigidly with the velocity they imply.
void Sim::lodStep(int self, double dt) {
    Car& c = cars[self];
    Car::Proxy& L = c.lod;
    const auto& road = world.loopRoad;
    const RoadWalk rw(road, c.aiDir);
    const int n = rw.n;
    const double kLane = 2.6, Lp = world.loopLength;
    // Speed: the lowest of cruise, the bends ahead and the gap to the car in front (IDM).
    const double aLat = aiCornerAccel(c), decel = aLat * 0.75;
    auto bendSpeed = [&](int from) {
        double seg = 0;
        int j = from;
        for (int m = 0; m < 5; m++) {
            const int jn = rw.wrap(j + c.aiDir);
            seg += len(road[jn] - road[j]);
            j = jn;
        }
        const double turn = std::acos(clampd(dot(rw.tangent(from), rw.tangent(j)), -1, 1));
        return turn > 1e-3 ? std::sqrt(aLat * seg / turn) : 1e9;
    };
    double vTarget = std::min(c.aiCruise, bendSpeed(L.idx)), dist = 0;
    for (int k = 1, i = L.idx; k <= 60; k++) {
        const int nx = rw.wrap(i + c.aiDir);
        dist += len(road[nx] - road[i]);
        i = nx;
        if (k % 2 == 0) {
            const double vb = bendSpeed(i);
            vTarget = std::min(vTarget, std::sqrt(vb * vb + 2 * decel * dist));
        }
    }
    double gap = 1e9, leadSpeed = 0;
    for (size_t o = 0; o < cars.size(); o++) {
        if ((int)o == self) continue;
        const Car& other = cars[o];
        const double halfW = 0.3 * std::max(other.bbMax.x - other.bbMin.x, other.bbMax.z - other.bbMin.z);
        if (std::fabs(aiLat[o] * c.aiDir - kLane) >= 1.3 + halfW) continue;
        const double ahead = std::fmod((aiAlong[o] - aiAlong[self]) * c.aiDir + 2 * Lp, Lp);
        if (ahead > 120) continue;
        const double g = ahead - (c.spec->length + other.spec->length) * 0.5;
        if (g < gap) gap = g, leadSpeed = dot(aiFrames[o].vel, rw.tangent(L.idx));
    }
    const double aMax = 2.5, bComf = 3.0, headway = 1.4, minGap = 4.0;
    double accel = aMax * (1 - std::pow(L.speed / std::max(vTarget, 0.1), 4));
    if (gap < 1e8) {
        const double sStar = minGap + std::max(0.0, L.speed * headway + L.speed * (L.speed - leadSpeed) / (2 * std::sqrt(aMax * bComf)));
        accel -= aMax * (sStar / std::max(gap, 0.5)) * (sStar / std::max(gap, 0.5));
    }
    L.speed = std::max(0.0, L.speed + std::max(accel, -9.0) * dt);
    c.aiGap = gap;

    // Along the road, easing into its lane and heading.
    rw.advance(L.idx, L.off, L.speed * dt);
    L.lat += (kLane - L.lat) * std::min(1.0, dt / 2.0);
    L.head *= std::exp(-dt);
    // (The road is a chain of straight 2 m pieces: the pose follows it through a short lag, so the velocity it implies is smooth.)
    double rx, rz, ry;
    rw.at(L.idx, L.off, L.lat, rx, rz, ry);
    const double k = 1 - std::exp(-dt / 0.1);
    L.fx += (rx - L.fx) * k, L.fz += (rz - L.fz) * k;
    const double yaw = L.yaw + wrapPi(ry + L.head - L.yaw) * k;
    const RoadPose P = groundPose(world, L.fx, L.fz, yaw);
    L.yaw = yaw;
    const Quat q = P.q * L.qd;
    const V3 com = P.pos + P.q.rotate(L.dd);
    const V3 vCom = dt > 1e-9 ? (com - L.prevCom) / dt : V3();
    L.prevCom = com;
    // Angular velocity from how the frame turned this frame (pitch and roll with the terrain, as well as yaw).
    Quat dq = q * Quat{L.prevQ.w, -L.prevQ.x, -L.prevQ.y, -L.prevQ.z};
    if (dq.w < 0) dq = Quat{-dq.w, -dq.x, -dq.y, -dq.z};
    const V3 omega = dt > 1e-9 ? V3(dq.x, dq.y, dq.z) * (2.0 / dt) : V3();
    L.prevQ = q;
    for (size_t k = 0; k < c.nodes.size() && k < L.local.size(); k++) {
        Node& nd = c.nodes[k];
        const V3 r = q.rotate(L.local[k]);
        nd.p = com + r;
        nd.v = vCom + cross(omega, r);
        nd.f = V3();
    }
    c.chassis.rot = q;
    for (ChassisFrame& rail : c.railFrame) {  // (the frame rails' fits turn with it, so they are right when the sim takes over)
        rail.rot = dq * rail.rot;
        rail.rot.normalize();
    }
    for (Wheel& wh : c.wheels) {
        if (wh.detached) continue;
        wh.omega = L.speed / std::max(wh.rollR(), 0.05);
        wh.angle += wh.omega * dt;
        wh.groundSpeed = L.speed;
    }
    c.aiIndex = L.off < rw.segLen(L.idx) * 0.5 ? L.idx : rw.wrap(L.idx + c.aiDir);
    c.updateBounds();
    if (c.grid) c.grid->stamp = -1;
    c.nodeGridStamp = -1;
}
