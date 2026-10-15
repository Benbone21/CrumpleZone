// Wheels as things (tires.h): shedding a wheel or a tire, wheels lying about the world,
// and the on-foot jobs that move them between hubs, carriers and hands.
#include "tires.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "softbody.h"

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

// Takes `kg` off (or with kg < 0, puts it back on) the nodes carrying the spare, never more
// than half of what a node weighs (the stability pass sized them).
void shiftSpareMass(Car& c, double kg) {
    for (size_t i = 0; i < c.spareNodes.size(); i++) {
        Node& n = c.nodes[c.spareNodes[i]];
        double d = kg * c.spareWeights[i];
        if (d > 0) d = std::min(d, 0.5 * n.m);
        n.m -= d;
        n.invm = 1.0 / n.m;
    }
}

// Spin inertia of a hub and rotor alone (kg m^2).
constexpr double kBareHubI = 0.3;

}  // namespace

V3 Car::spareAt() const {
    V3 p;
    for (size_t i = 0; i < spareNodes.size(); i++) p += nodes[spareNodes[i]].p * spareWeights[i];
    return p;
}

void Car::setJack(bool has) {
    hasJack = has;
    if (!has) {
        jackPlaced = false;
        jackWheel = -1;
        jackHeight = 0;
    }
}

V3 Car::spareAxis() const {
    const Frame f = frame();
    return spec->spareFlat ? f.up : -f.fwd;
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
