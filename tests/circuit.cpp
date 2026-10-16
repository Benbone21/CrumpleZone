// The Frontier race circuit: how fast the cars get round it, driven on the limit.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "harness.h"

namespace {

// Signed curvature of a closed polyline at each point (1/m, + = left).
std::vector<double> curvature(const std::vector<V3>& p) {
    const int n = (int)p.size();
    std::vector<double> k(n);
    for (int i = 0; i < n; i++) {
        const V3 &a = p[(i - 3 + n) % n], &b = p[i], &c = p[(i + 3) % n];
        const double abx = b.x - a.x, abz = b.z - a.z, bcx = c.x - b.x, bcz = c.z - b.z, acx = c.x - a.x, acz = c.z - a.z;
        const double cr = abx * bcz - abz * bcx;
        const double den = std::hypot(abx, abz) * std::hypot(bcx, bcz) * std::hypot(acx, acz);
        k[i] = den > 1e-9 ? -2 * cr / den : 0;  // x east, z north: a left turn has cross < 0
    }
    return k;
}

// Arc length from point i to i+1.
double segLen(const std::vector<V3>& p, int i) {
    const V3& a = p[i];
    const V3& b = p[(i + 1) % p.size()];
    return std::hypot(b.x - a.x, b.z - a.z);
}

struct Lap {
    double time = -1, peakG = 0, worstOff = 0, minCornerKmh = 1e9;
    bool spun = false;
};

// One flying lap: a speed profile for `latG` in the corners and `brakeG` braking, followed by
// pure pursuit; the lap is timed from the second pass of the start.
Lap lap(Ctx& ctx, const VehicleSpec& spec, double latG, double brakeG, bool keys) {
    const std::vector<V3>& t = frontierTrack();
    const int n = (int)t.size();
    const std::vector<double> k = curvature(t);
    std::vector<double> vmax(n);
    // (Less on a wet patch: the grip there over the dry pavement's.)
    const World& w = ctx.sim.world;
    auto gripAt = [&](int i) { return surfaceGrip(w.surfaceAt(t[i].x, t[i].z)) / surfaceGrip(SURF_PAVED); };
    for (int i = 0; i < n; i++) vmax[i] = std::min(120.0, std::sqrt(latG * gripAt(i) * 9.81 / std::max(1e-4, std::fabs(k[i]))));
    // Back from each corner: how fast it can arrive, braking.
    for (int pass = 0; pass < 3; pass++)
        for (int q = 2 * n; q > 0; q--) {
            const int i = q % n, j = (q + 1) % n;
            vmax[i] = std::min(vmax[i], std::sqrt(vmax[j] * vmax[j] + 2 * brakeG * gripAt(i) * 9.81 * segLen(t, i)));
        }
    const int startIdx = n / 6;
    const V3 s0 = t[startIdx], s1 = t[(startIdx + 1) % n];
    Car& c = ctx.add(spec, s0.x, s0.z, std::atan2(-(s1.z - s0.z), s1.x - s0.x) * 180 / M_PI);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, std::min(vmax[startIdx], 30.0));
    Lap out;
    int idx = startIdx, passes = 0;
    double travelled = 0, tStart = -1;
    V3 prevVel = c.frame().vel;
    double smooth = 0;
    const bool dbg = std::getenv("CZ_DEBUG");
    int fr = 0;
    ctx.runUntil(400, [&] { return passes >= 2 || out.spun; }, [&] {
        const Frame f = c.frame();
        // Nearest point ahead.
        for (int s = 0; s < 20; s++) {
            const int j = (idx + 1) % n;
            const double dj = std::hypot(t[j].x - f.com.x, t[j].z - f.com.z), di = std::hypot(t[idx].x - f.com.x, t[idx].z - f.com.z);
            if (dj > di) break;
            travelled += segLen(t, idx);
            idx = j;
            if (idx == startIdx) {
                passes++;
                if (passes == 1) tStart = ctx.t;
                if (passes == 2) out.time = ctx.t - tStart;
            }
        }
        const double off = std::hypot(t[idx].x - f.com.x, t[idx].z - f.com.z);
        if (passes >= 1) out.worstOff = std::max(out.worstOff, off);
        const double v = len(f.vel);
        const double look = std::max(6.0, 0.55 * v);
        int a = idx;
        double acc = 0;
        while (acc < look) acc += segLen(t, a), a = (a + 1) % n;
        const V3 aim = t[a] - f.com;
        const double alpha = std::atan2(dot(aim, f.left), dot(aim, f.fwd));
        const double want = std::atan(2 * c.wheelbase * std::sin(alpha) / look);
        // Speed: the profile a little ahead (brake points come early enough).
        int b = idx;
        acc = 0;
        double vt = 1e9;
        while (acc < std::max(4.0, v * 0.4)) vt = std::min(vt, vmax[b]), acc += segLen(t, b), b = (b + 1) % n;
        if (keys) {
            // As a player on the keyboard: left/right held or not, and the throttle/brake keys.
            // Feathering the keys (as a practised player does): a held key asks for the
            // grip limit, so tap to hold the input at the share of it the line needs.
            const double aMax = 9.81 * spec.tireGrip * spec.lateralGrip * 0.85;
            const double full = std::atan(c.wheelbase * aMax / std::max(1.0, v * v)) + 0.045;
            const double frac = clampd(want / std::min(0.6, full), -1, 1);
            const bool left = c.steerInput < frac - 0.06, right = c.steerInput > frac + 0.06;
            ctx.sim.drive(v < vt - 0.3, v > vt + 0.5, left, right, false, Ctx::kFrame);
        } else {
            c.steer = clampd(want, -0.6, 0.6);
            c.throttle = clampd((vt - v) * 0.5, 0, 1);
            c.brake = v > vt + 0.5 ? clampd((v - vt) * 0.3, 0, 1) : 0;
            c.handbrake = 0;
        }
        const V3 accel = (f.vel - prevVel) / Ctx::kFrame;
        prevVel = f.vel;
        const V3 side = norm(cross(V3(0, 1, 0), norm(f.vel + V3(1e-9, 0, 0))));
        smooth += (std::fabs(dot(accel, side)) / 9.81 - smooth) * 0.1;
        if (passes >= 1) {
            out.peakG = std::max(out.peakG, smooth);
            if (std::fabs(k[idx]) > 1.0 / 80) out.minCornerKmh = std::min(out.minCornerKmh, v * 3.6);
        }
        const double slip = std::fabs(std::atan2(dot(f.vel, f.left), std::max(0.5, dot(f.vel, f.fwd))));
        out.spun |= (slip > 0.6 && v > 5) || f.up.y < 0.5;
        if (dbg && ++fr % 30 == 0)
            std::printf("LAP%s t %.1f idx %d v %.0f km/h vt %.0f k %.4f (R %.0f) steer %.3f off %.1f latG %.2f slip %.2f surf %s pos (%.0f %.0f) thr %.2f tc %.2f\n", keys ? "K" : "", ctx.t, idx, v * 3.6, vt * 3.6, k[idx],
                        1 / std::max(1e-4, std::fabs(k[idx])), c.steer, off, smooth, slip, surfaceInfo(w.surfaceAt(f.com.x, f.com.z)).name, f.com.x, f.com.z, c.throttle, c.tcCut);
    });
    return out;
}

}  // namespace

SCENARIO(circuitCorners, "frontier/circuit-corners", "The Frontier circuit's corners: radius of each one (a race track's tightest are ~15-25 m, its sweepers 60 m+)") {
    const std::vector<V3>& t = frontierTrack();
    const std::vector<double> k = curvature(t);
    double total = 0, tightest = 1e9;
    for (int i = 0; i < (int)t.size(); i++) {
        total += segLen(t, i);
        tightest = std::min(tightest, 1 / std::max(1e-5, std::fabs(k[i])));
    }
    std::printf("CIRCUIT %zu points, %.0f m, tightest radius %.1f m\n", t.size(), total, tightest);
    if (std::getenv("CZ_DEBUG"))
        for (int i = 0; i < (int)t.size(); i += 4) std::printf("  %4d (%.0f, %.0f) R %.0f\n", i, t[i].x, t[i].z, 1 / std::max(1e-5, std::fabs(k[i])));
    ctx.metric("length", total, "m", 1300, 2000);
    ctx.metric("tightest corner radius", tightest, "m", 15, 40);
}

SCENARIO(circuitJesko, "frontier/circuit-jesko", "Jesko, a flying lap of the Frontier circuit on the limit (1.25 g corners): stays on, corners at real-car speeds") {
    ctx.sim.world = World(MAP_FRONTIER);
    const Lap L = lap(ctx, vehicleCatalog()[1], std::getenv("CZ_LATG") ? std::atof(std::getenv("CZ_LATG")) : 1.25, 1.2, false);
    std::printf("JESKO lap %.1f s, peak %.2f g, slowest corner %.0f km/h, worst off line %.1f m, spun %d\n", L.time, L.peakG, L.minCornerKmh, L.worstOff, L.spun);
    ctx.check("didn't spin", !L.spun);
    ctx.metric("lap time", L.time, "s", 45, 58);
    ctx.metric("peak lateral acceleration", L.peakG, "g", 1.1, 1.8);
    ctx.metric("furthest off the racing line", L.worstOff, "m", 0, 5);
}

SCENARIO(circuitJeskoKeys, "frontier/circuit-jesko-keys", "Jesko on the keyboard (steer keys held or not): feathering the keys, a lap at 1.15 g") {
    ctx.sim.world = World(MAP_FRONTIER);
    const double g = std::getenv("CZ_LATG") ? std::atof(std::getenv("CZ_LATG")) : 1.15;
    const Lap L = lap(ctx, vehicleCatalog()[1], g, 1.0, true);
    std::printf("JESKO KEYS lap %.1f s, peak %.2f g, slowest corner %.0f km/h, worst off line %.1f m, spun %d\n", L.time, L.peakG, L.minCornerKmh, L.worstOff, L.spun);
    ctx.check("didn't spin", !L.spun);
    ctx.metric("lap time", L.time, "s", 45, 60);
    ctx.metric("furthest off the racing line", L.worstOff, "m", 0, 6);
}

SCENARIO(circuitBanking, "frontier/circuit-banking", "The Frontier circuit's fast corners are banked (outside of the turn up, 6-16 degrees), eased in and out gently; the slow ones and the straights are flat") {
    ctx.sim.world = World(MAP_FRONTIER);
    const World& w = ctx.sim.world;
    const std::vector<V3>& t = frontierTrack();
    const int n = (int)t.size();
    double steepest = 0, wrongWay = 0, straightMax = 0, twist = 0;
    int banked = 0;
    // How far each point is from a bend (points, ~2 m apart): a banked corner's transition runs
    // ~40 m out onto the straight either side of it.
    auto turnAt = [&](int k) {
        const V3 p = t[k], c = t[(k + 6) % n], d = t[(k - 6 + n) % n];
        const V3 u = norm(V3(p.x - d.x, 0, p.z - d.z)), v = norm(V3(c.x - p.x, 0, c.z - p.z));
        return u.x * v.z - u.z * v.x;
    };
    std::vector<int> fromBend(n, n);
    for (int k = 0; k < n; k++)
        if (std::fabs(turnAt(k)) >= 0.01)
            for (int o = -30; o <= 30; o++) fromBend[((k + o) % n + n) % n] = std::min(fromBend[((k + o) % n + n) % n], std::abs(o));
    double prevSlope = 0;
    for (int k = 0; k <= n; k++) {
        const V3 a = t[(k - 2 + n) % n], b = t[(k + 2) % n], p = t[k % n];
        const V3 fwd = norm(V3(b.x - a.x, 0, b.z - a.z)), left(-fwd.z, 0, fwd.x);
        const double slope = (w.height(p.x + left.x * 4.5, p.z + left.z * 4.5) - w.height(p.x - left.x * 4.5, p.z - left.z * 4.5)) / 9.0;
        // The road's twist: how fast the banking comes in or goes, per metre of track.
        if (k > 0) twist = std::max(twist, std::fabs(std::atan(slope) - std::atan(prevSlope)) * 180 / M_PI / std::max(0.5, len(t[k % n] - t[k - 1])));
        prevSlope = slope;
    }
    for (int k = 0; k < n; k++) {
        const V3 a = t[(k - 2 + n) % n], b = t[(k + 2) % n], p = t[k];
        const V3 fwd = norm(V3(b.x - a.x, 0, b.z - a.z)), left(-fwd.z, 0, fwd.x);
        // Which way it turns here (+ left), and the surface's slope across it (+ up to the left).
        const V3 c = t[(k + 6) % n], d = t[(k - 6 + n) % n];
        const V3 u = norm(V3(p.x - d.x, 0, p.z - d.z)), v = norm(V3(c.x - p.x, 0, c.z - p.z));
        const double turn = u.x * v.z - u.z * v.x;  // (+: turning toward `left`)
        const double hl = w.height(p.x + left.x * 4.5, p.z + left.z * 4.5), hr = w.height(p.x - left.x * 4.5, p.z - left.z * 4.5);
        const double slope = (hl - hr) / 9.0;
        if (std::fabs(turn) > 0.08) {
            if (std::fabs(slope) > std::tan(4 * M_PI / 180)) {
                banked++;
                // The outside of the turn up: a left turn's right side higher (slope < 0).
                if (slope * turn > 0) wrongWay = std::max(wrongWay, std::fabs(slope));
            }
            steepest = std::max(steepest, std::fabs(slope));
        } else if (fromBend[k] > 22) {
            straightMax = std::max(straightMax, std::fabs(slope));
        }
    }
    ctx.metric("steepest banking", std::atan(steepest) * 180 / M_PI, "deg", 12, 17);
    ctx.metric("points in a banked corner (~2 m apart)", banked, "", 60, 2000);
    ctx.metric("banking the wrong way (inside up)", std::atan(wrongWay) * 180 / M_PI, "deg", 0, 1);
    ctx.metric("cross-slope on the straights (past the transitions)", std::atan(straightMax) * 180 / M_PI, "deg", 0, 3.5);
    // (At 130 km/h, 0.3 deg/m is the road rolling ~11 deg a second under the car. At 0.7, coming
    // off the 16 degree corner, a Jesko on slicks lifted a wheel and rocked.)
    ctx.metric("steepest twist in a transition", twist, "deg/m", 0, 0.35);
}
