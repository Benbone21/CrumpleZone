// Wheels as things (tires.h): shedding a wheel or a tire, wheels lying about the world,
// and the on-foot jobs that move them between hubs, carriers and hands.
#include "tires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "carframe.h"
#include "softbody.h"
#include "tiretemp.h"

std::string WheelItem::sizeName() const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d IN%s", (int)std::lround(R * 2 / 0.0254), tireOnly ? " TIRE" : "");
    return buf;
}

namespace {

// The rotation taking +z to `to`.
Quat quatToAxis(const V3& to) {
    const V3 z(0, 0, 1);
    const double c = dot(z, to);
    if (c > 1 - 1e-9) return Quat{};
    if (c < -1 + 1e-9) return Quat::axisAngle(V3(1, 0, 0), M_PI);
    return Quat::axisAngle(norm(cross(z, to)), std::acos(clampd(c, -1, 1)));
}

double speedOf(const Car& c) { return len(c.frame().vel); }

// A vehicle has to be stopped (rolling under ~1 km/h) to work on it.
constexpr double kStopped = 0.3;

// Takes `kg` off the nodes carrying the spare, never more than half of what a node weighs
// (the stability pass sized them); what they can't spare comes off the rest of the body, a
// little from each. With kg < 0 a spare goes back on: what came off, scaled to its weight.
void shiftSpareMass(Car& c, double kg) {
    if (kg < 0) {
        double taken = 0;
        for (double d : c.spareShift) taken += d;
        if (taken <= 0) {  // (nothing recorded: onto the carrier's nodes)
            for (size_t i = 0; i < c.spareNodes.size(); i++) c.nodes[c.spareNodes[i]].m -= kg * c.spareWeights[i];
        } else
            for (size_t i = 0; i < c.spareShift.size() && i < c.nodes.size(); i++) c.nodes[i].m += c.spareShift[i] * (-kg / taken);
        for (Node& n : c.nodes) n.invm = 1.0 / n.m;
        c.spareShift.clear();
        return;
    }
    c.spareShift.assign(c.nodes.size(), 0);
    double left = kg;
    for (size_t i = 0; i < c.spareNodes.size(); i++) {
        const int ni = c.spareNodes[i];
        const double d = std::min(kg * c.spareWeights[i], 0.5 * c.nodes[ni].m);
        c.nodes[ni].m -= d, c.spareShift[ni] += d, left -= d;
    }
    double body = 0;
    for (size_t i = 0; i < c.nodes.size(); i++) body += c.nodes[i].m;
    if (left > 0 && body > 0)
        for (size_t i = 0; i < c.nodes.size(); i++) {
            const double d = std::min(left * c.nodes[i].m / body, 0.1 * c.nodes[i].m);
            c.nodes[i].m -= d, c.spareShift[i] += d;
        }
    for (Node& n : c.nodes) n.invm = 1.0 / n.m;
}

}  // namespace

V3 Car::spareAt() const {
    V3 p;
    for (size_t i = 0; i < spareNodes.size(); i++) p += nodes[spareNodes[i]].p * spareWeights[i];
    return gateCarried() ? gateSwung(p, true) : p;  // (a carrier swings away with the gate)
}

V3 Car::stowAt(Stow what) const {
    const Frame f = frame();
    const double side = spec->width * 0.27 * (what == STOW_CHAINS ? 1 : -1);
    return f.com - f.fwd * (spec->length * 0.5 - 0.5) + f.left * side - f.up * 0.1;
}

void Car::setJack(bool has) {
    hasJack = has;
    if (!has) {
        jackPlaced = false;
        jackWheel = -1;
        jackHeight = 0;
    }
}

V3 Car::jackOut(int k) const {
    const Wheel& wh = wheels[k];
    V3 out = nodes[wh.outer].p - nodes[wh.inner].p;
    out.y = 0;
    return len(out) > 1e-6 ? norm(out) : V3(1, 0, 0);
}

V3 Car::railPoint(int side, double xRest, int corner) const {
    if (ladderStations < 2 || ladder.empty()) return frame().com;
    auto at = [&](int st) { return nodes[ladder[st * 8 + (side > 0 ? 4 : 0) + corner]].p; };
    auto xs = [&](int st) { return restLocal[ladder[st * 8]].x; };
    int s0 = 0;  // (the stations run front to back: x falls)
    while (s0 + 2 < ladderStations && xs(s0 + 1) > xRest) s0++;
    const double x0 = xs(s0), x1 = xs(s0 + 1);
    const double t = clampd((x0 - xRest) / std::max(1e-6, x0 - x1), 0, 1);
    return at(s0) * (1 - t) + at(s0 + 1) * t;
}

// Where along the car (rest x) the pad sits: just inboard of the wheel well, behind a front wheel and ahead of a rear one.
double Car::jackPadX(int k) const {
    const Wheel& wh = wheels[k];
    double mid = 0;
    for (const Wheel& w : wheels) mid += restLocal[w.inner].x / (double)wheels.size();
    const double hub = restLocal[wh.inner].x;
    const double R = std::max(wh.R, spec->wellTireRadius);
    return hub + (hub > mid ? -1.0 : 1.0) * (R + 0.16);
}

Car::JackNodes Car::jackNodes(int k) const {
    JackNodes j;
    if (ladder.empty()) {  // (a unibody: the hub, as before)
        j.n[0] = wheels[k].inner, j.n[1] = wheels[k].outer, j.w[0] = j.w[1] = 0.5;
        return j;
    }
    const double x = jackPadX(k);
    auto xs = [&](int st) { return restLocal[ladder[st * 8]].x; };
    int s0 = 0;
    while (s0 + 2 < ladderStations && xs(s0 + 1) > x) s0++;
    const double t = clampd((xs(s0) - x) / std::max(1e-6, xs(s0) - xs(s0 + 1)), 0, 1);
    const int side = restLocal[wheels[k].inner].z > 0 ? 1 : 0;
    for (int i = 0; i < 2; i++)
        for (int c = 0; c < 2; c++) {  // (the inner bottom corner takes a third, the outer two)
            j.n[i * 2 + c] = ladder[(s0 + i) * 8 + side * 4 + c];
            j.w[i * 2 + c] = (i ? t : 1 - t) * (c ? 0.7 : 0.3);
        }
    return j;
}

V3 Car::jackPoint(int k) const {
    const Wheel& wh = wheels[k];
    if (ladder.empty()) {  // a unibody has no rail: under the axle or the lower arm, inboard of the tire
        const V3 hub = (nodes[wh.inner].p + nodes[wh.outer].p) * 0.5;
        double drop = 0.05;
        bool live = false;
        for (const Axle& ax : axles) live |= (ax.wheel[0] == k || ax.wheel[1] == k) && !ax.housing.empty();
        if (!live) {
            const double arm = wh.lower >= 0 ? hub.y - nodes[wh.lower].p.y : 0.12;
            drop = clampd(arm, 0.08, 0.2) + 0.025;
        }
        V3 at = hub - jackOut(k) * (wh.width * 0.5 + 0.12);
        at.y = hub.y - drop;
        return at;
    }
    const int side = restLocal[wh.inner].z > 0 ? 1 : -1;
    const double x = jackPadX(k);
    const V3 outer = railPoint(side, x, 1), inner = railPoint(side, x, 0);
    V3 out = outer - inner;
    out.y = 0;
    out = len(out) > 1e-6 ? norm(out) : jackOut(k);
    return outer + out * kJackPadOut - frame().up * kJackPadDepth;
}

double Car::jackDrop(int k) const {
    const Wheel& wh = wheels[k];
    return (nodes[wh.inner].p.y + nodes[wh.outer].p.y) * 0.5 - jackPoint(k).y;
}

double Car::jackReach(int k) const {
    if (ladder.empty()) return 0.10;
    const double lat = std::fabs(dot(jackPoint(k) - frame().com, frame().left));
    return clampd(spec->width * 0.5 + 0.05 - lat, 0.15, 0.9);
}

void Car::placeJack(const World& world, int k) {
    const V3 at = jackPoint(k);
    jackPlaced = true, jackWheel = k, jackHeight = 0, jackWorked = false;
    jackFoot = at;
    jackFoot.y = world.height(at.x, at.z);
    jackBase = at.y - jackFoot.y - 0.01;  // (the saddle wound up to just under it)
}

V3 Car::spareAxis() const {
    const Frame f = frame();
    const V3 ax = spec->spareFlat ? f.up : -f.fwd;
    return gateCarried() ? gateSwung(ax, false) : ax;
}

WheelItem Sim::wheelItem(const Car& c, const Wheel& wh) const {
    (void)c;
    WheelItem it = wh.item;
    it.popped = wh.popped;
    if (wh.debeaded) {  // the tire's gone: a bare rim
        it.R = wh.rimR + 0.015;
        it.popped = true;
    }
    return it;
}

int Sim::addLooseWheel(const WheelItem& it, const V3& p, const Quat& q, const V3& v, const V3& w) {
    // Too many lying about: the oldest of ours goes.
    int mine = 0, oldest = -1;
    for (int i = 0; i < (int)looseWheels.size(); i++)
        if (!looseWheels[i].remote) {
            mine++;
            if (oldest < 0 || looseWheels[i].age > looseWheels[oldest].age) oldest = i;
        }
    if (mine >= kMaxLooseWheels && oldest >= 0) looseWheels.erase(looseWheels.begin() + oldest);
    LooseWheel lw;
    lw.item = it;
    lw.p = p;
    lw.q = q;
    lw.v = v;
    lw.w = w;
    lw.netId = nextLooseId++;
    looseWheels.push_back(lw);
    return (int)looseWheels.size() - 1;
}

int Sim::shedWheel(int ci, int wi, bool tireOnly) {
    Car& c = cars[ci];
    Wheel& wh = c.wheels[wi];
    if (!wh.mounted || wh.detached || (tireOnly && wh.debeaded)) return -1;
    const Node &a = c.nodes[wh.inner], &b = c.nodes[wh.outer];
    const V3 hub = (a.p + b.p) * 0.5, out = norm(b.p - a.p), vel = (a.v + b.v) * 0.5;
    const V3 axisLeft = wh.left ? out : -out;
    WheelItem it = wheelItem(c, wh);
    if (tireOnly) {
        it = wh.item;
        it.tireOnly = true;
        it.popped = false;
        wh.debeaded = true;
        wh.popped = false;
    } else {
        wh.mounted = false;
        wh.debeaded = false;
        wh.I = kBareHubI;
    }
    wh.studLoad = wh.beadLoad = 0;
    wh.sink = wh.clog = 0;
    events.push_back(tireOnly ? "TIRE OFF THE RIM" : "WHEEL OFF");
    // Off it goes, still spinning, flung a little outboard.
    return addLooseWheel(it, hub + out * 0.05, quatToAxis(out), vel + out * 0.8, axisLeft * wh.omega);
}

bool Sim::takeSpare(int ci, WheelItem& out) {
    Car& c = cars[ci];
    if (!c.hasSpare || speedOf(c) > kStopped) return false;
    out = c.spare;
    c.hasSpare = false;
    shiftSpareMass(c, out.mass);
    c.wake();
    return true;
}

bool Sim::stowSpare(int ci, const WheelItem& it) {
    Car& c = cars[ci];
    if (c.hasSpare || c.spareNodes.empty() || it.tireOnly || speedOf(c) > kStopped) return false;
    c.spare = it;
    c.hasSpare = true;
    shiftSpareMass(c, -it.mass);
    c.wake();
    return true;
}

bool Sim::removeWheel(int ci, int wi, WheelItem& out) {
    Car& c = cars[ci];
    Wheel& wh = c.wheels[wi];
    if (!wh.mounted || wh.detached || speedOf(c) > kStopped) return false;
    out = wheelItem(c, wh);
    wh.mounted = false;
    wh.debeaded = false;
    wh.popped = false;
    wh.I = kBareHubI;
    wh.omega = 0;
    c.wake();
    return true;
}

bool Sim::fitWheel(int ci, int wi, const WheelItem& it) {
    Car& c = cars[ci];
    Wheel& wh = c.wheels[wi];
    if (wh.detached || speedOf(c) > kStopped) return false;
    if (it.tireOnly) {  // a tire back on its rim (the bead seated with the jack and a strap)
        if (!wh.mounted || !wh.debeaded) return false;
        wh.debeaded = false;
        wh.popped = it.popped;
    } else {
        if (wh.mounted) return false;
        wh.item = it;
        wh.item.popped = false;
        wh.R = it.R;
        wh.rimR = it.rimR;
        wh.width = it.width;
        wh.I = it.I;
        wh.popped = it.popped;
        wh.debeaded = false;
        wh.mounted = true;
    }
    wh.omega = 0;
    wh.sink = wh.clog = 0;
    wh.tempC = (float)c.ambientC;  // (a wheel off the ground has sat in the air)
    wh.stickX = wh.stickY = 0;
    wh.studLoad = wh.beadLoad = 0;
    wh.poppedAnim = 0;
    // It's on the jack while the wheel goes on: let it down onto the new tire rather than
    // have it spring out of the ground.
    const V3 hub = (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5;
    Contact ct;
    const V3 out = norm(c.nodes[wh.outer].p - c.nodes[wh.inner].p);
    // Pressure-dependent compliance is handled by wheelStep, not by shrinking the collision tire.
    const double rollR = wh.rollR();
    if (world.contact(hub, rollR, &out, wh.width * 0.5, ct) && ct.pen > 0) c.translate(V3(0, ct.pen * 1.05 + 0.01, 0));
    c.wake();
    return true;
}

bool Sim::pickUpWheel(int li, WheelItem& out) {
    if (li < 0 || li >= (int)looseWheels.size() || looseWheels[li].remote) return false;
    out = looseWheels[li].item;
    looseWheels.erase(looseWheels.begin() + li);
    return true;
}

int Sim::dropWheel(const WheelItem& it, const V3& at, const V3& facing) {
    // Stood on its tread in front of you, axle across your path.
    V3 f = norm(V3(facing.x, 0, facing.z));
    if (len(f) < 0.5) f = V3(1, 0, 0);
    const V3 p0 = at + f * 0.6;
    const V3 p(p0.x, world.height(p0.x, p0.z) + it.R + 0.02, p0.z);
    return addLooseWheel(it, p, quatToAxis(V3(-f.z, 0, f.x)), V3(), V3());
}

// Vehicles against loose wheels: their nodes and tires are pushed out of the wheel's
// cylinder and the wheel is pushed back. (Collected into LooseWheel::f/t; stepped below.)
void Sim::looseWheelForces() {
    for (LooseWheel& lw : looseWheels) {
        lw.f = lw.t = V3();
        if (lw.remote) continue;
        const double R = lw.item.R, hw = lw.item.width * 0.5, reach = R + hw;
        const V3 ax = lw.axis();
        // The closest point of the cylinder to q, and how deep q is inside it.
        auto closest = [&](const V3& q, double* depth) {
            const V3 d = q - lw.p;
            const double z = dot(d, ax);
            V3 rad = d - ax * z;
            const double r = len(rad);
            const double zc = clampd(z, -hw, hw), rc = std::min(r, R);
            if (depth) *depth = (std::fabs(z) <= hw && r <= R) ? std::min(hw - std::fabs(z), R - r) : 0;
            return lw.p + ax * zc + (r > 1e-9 ? rad * (rc / r) : V3());
        };
        for (Car& c : cars) {
            if (c.bbMax.x < lw.p.x - reach || c.bbMin.x > lw.p.x + reach || c.bbMax.y < lw.p.y - reach ||
                c.bbMin.y > lw.p.y + reach || c.bbMax.z < lw.p.z - reach || c.bbMin.z > lw.p.z + reach)
                continue;
            bool touched = false;
            // Its tires: a sphere the tire's size against the wheel (a tire rolling over it).
            for (Wheel& wh : c.wheels) {
                Node &a = c.nodes[wh.inner], &b = c.nodes[wh.outer];
                const V3 hc = (a.p + b.p) * 0.5;
                const double r = wh.rollR() * 0.9;
                const V3 cp = closest(hc, nullptr);
                V3 d = hc - cp;
                const double dl = len(d);
                if (dl >= r || dl < 1e-6) continue;
                const V3 n = d / dl;
                const V3 vrel = (a.v + b.v) * 0.5 - (lw.v + cross(lw.w, cp - lw.p));
                const double F = std::max(0.0, 1.5e5 * (r - dl) - 3000 * dot(vrel, n));
                a.f += n * (F * 0.5);
                b.f += n * (F * 0.5);
                lw.f -= n * F;
                lw.t += cross(cp - lw.p, -n * F);
                touched = true;
            }
            // Its body: nodes inside the wheel get pushed out the nearest way.
            for (size_t ni = 0; ni < c.nodes.size(); ni++) {
                Node& nd = c.nodes[ni];
                if (!nd.collide) continue;
                const V3 d = nd.p - lw.p;
                if (dot(d, d) > reach * reach) continue;
                const double z = dot(d, ax);
                const V3 rad = d - ax * z;
                const double r = len(rad);
                if (std::fabs(z) > hw || r > R) continue;
                const double penZ = hw - std::fabs(z), penR = R - r;
                const V3 n = penZ < penR ? ax * (z >= 0 ? 1.0 : -1.0) : (r > 1e-9 ? rad / r : V3(0, 1, 0));
                const double pen = std::min(penZ, penR);
                const V3 vrel = nd.v - (lw.v + cross(lw.w, nd.p - lw.p));
                const double F = std::max(0.0, 2e5 * pen - 2000 * dot(vrel, n));
                nd.f += n * F;
                lw.f -= n * F;
                lw.t += cross(nd.p - lw.p, -n * F);
                touched = true;
            }
            if (touched && (len(c.frame().vel) > 0.2 || !c.sleeping)) lw.sleeping = false, lw.still = 0;
        }
    }
}

// The wheels themselves: a rigid cylinder of rubber (and steel) on the ground and rocks.
void Sim::looseWheelStep() {
    const V3 g(0, -9.81, 0);
    for (LooseWheel& lw : looseWheels) {
        lw.age += h;
        if (lw.remote) {  // coasts on what its owner last sent
            lw.p += lw.v * h;
            continue;
        }
        if (lw.sleeping && dot(lw.f, lw.f) < 1) continue;
        lw.sleeping = false;
        const V3 p0 = lw.p;
        const WheelItem& it = lw.item;
        const double m = std::max(1.0, it.mass), R = it.R, hw = it.width * 0.5;
        const double Ia = std::max(0.05, it.I), Ip = 0.5 * Ia + m * it.width * it.width / 12;
        V3 F = lw.f + g * m, T = lw.t;
        const V3 ax = lw.axis();
        Contact ct;
        bool touching = false;
        if (world.contact(lw.p, R, &ax, hw, ct) && ct.pen > 0) {
            touching = true;
            const V3 n = ct.n;
            // Where it touches: the point of the cylinder deepest toward the ground.
            const double na = dot(n, ax);
            V3 radial = n - ax * na;
            const double rl = len(radial);
            V3 cp = lw.p;
            if (rl > 1e-3) cp -= radial * (R / rl);
            if (std::fabs(na) > 1e-3) cp -= ax * (hw * (na > 0 ? 1.0 : -1.0));
            const V3 arm = cp - lw.p;
            const V3 vcp = lw.v + cross(lw.w, arm);
            const double vn = dot(vcp, n);
            const double kN = it.tireOnly ? 8e4 : 1.5e5;
            const double Fn = std::max(0.0, kN * ct.pen - 2 * 0.25 * std::sqrt(kN * m) * vn);
            V3 vt = vcp - n * vn;
            const double vtl = len(vt);
            V3 Ff = n * Fn;
            if (vtl > 1e-6 && Fn > 0) {
                const double mu = 0.9 * surfaceInfo(ct.surface).grip;
                // (No more than stops the slide this step: static friction at rest.)
                const double Ft = std::min(mu * Fn, 0.25 * m * vtl / h);
                Ff -= vt * (Ft / vtl);
            }
            F += Ff;
            T += cross(arm, Ff);
        }
        lw.v += F * (h / m);
        // Inverse inertia in its own frame: Ia about the axle, Ip across it.
        V3 tl = lw.q.unrotate(T);
        tl = V3(tl.x / Ip, tl.y / Ip, tl.z / Ia);
        lw.w += lw.q.rotate(tl) * h;
        if (touching) {
            // Rolling resistance and the tread scrubbing: it slows and settles.
            lw.w *= 1 - h * 0.6;
            lw.v *= 1 - h * 0.05;
        }
        lw.p += lw.v * h;
        const double wl = len(lw.w);
        if (wl > 1e-9) {
            lw.q = Quat::axisAngle(lw.w / wl, wl * h) * lw.q;
            lw.q.normalize();
        }
        // Lying still: it sleeps until something touches it.
        if (touching && len(lw.v) < 0.08 && len(lw.w) < 0.3) {
            lw.still += h;
            if (lw.still > 0.8) lw.sleeping = true, lw.v = lw.w = V3();
        } else {
            lw.still = 0;
        }
        if (!std::isfinite(lw.p.x + lw.p.y + lw.p.z + lw.v.x + lw.w.x)) {  // (never: but if so, set it down still)
            lw.p = p0;
            lw.v = lw.w = V3();
            lw.q = Quat{};
        }
    }
}

BlowoutTuning gBlowout;

double tireBlowoutRate(double frac, double loadRatio, double heat) {
    if (frac < 1) return 0;
    const BlowoutTuning& t = gBlowout;
    return t.base * std::exp(t.steep * (frac - 1)) * std::pow(clampd(loadRatio, 0.3, 2.5), t.loadExp) * heat;
}

double wheelTempFactor(const Wheel& wh, int tread) { return 1 + 4 * tireOverheat(tread, wh.tempC); }

double blowoutDie(uint64_t seed, int wheel, uint32_t tick) {
    uint64_t z = seed * 0x9E3779B97F4A7C15ull + (uint64_t)(wheel + 1) * 0xD1B54A32D192ED03ull + (uint64_t)tick * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

double Car::tireSpeedFrac() const {
    double f = 0;
    for (const Wheel& wh : wheels) {
        if (!wh.mounted || wh.detached || wh.popped || wh.debeaded) continue;
        const double rating = spec->tireRatingKmh();
        f = std::max(f, std::fabs(wh.omega) * wh.R * 3.6 / rating);
    }
    return f;
}
