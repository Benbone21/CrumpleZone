// Exact queries on the world's object shapes (objshape.h).
#include "objshape.h"

#include <algorithm>
#include <cmath>

namespace objshape {

namespace {

const double kInf = 1e18;

// The nearest point of triangle abc to p (Ericson, Real-Time Collision Detection).
V3 closestOnTri(const V3& p, const V3& a, const V3& b, const V3& c) {
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const V3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const V3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

// Roots of |o + t d - c|^2 = r^2 in the plane (ignoring y when flat): t0 <= t1. False if none.
bool circleRoots(double ox, double oz, double dx, double dz, double cx, double cz, double r, double& t0, double& t1) {
    const double fx = ox - cx, fz = oz - cz;
    const double a = dx * dx + dz * dz, b = fx * dx + fz * dz, c = fx * fx + fz * fz - r * r;
    if (a < 1e-12) return false;
    const double disc = b * b - a * c;
    if (disc < 0) return false;
    const double s = std::sqrt(disc);
    t0 = (-b - s) / a, t1 = (-b + s) / a;
    return true;
}

// Slab test over a set of (normal, offset) planes: the ray's interval inside, and which plane it entered through.
struct Interval {
    double in = -kInf, out = kInf;
    V3 n;
    bool empty = false;
    // A plane dot(n, p) <= off bounds the inside.
    void clip(const V3& pn, double off, const V3& o, const V3& d) {
        const double den = dot(pn, d), num = off - dot(pn, o);
        if (std::fabs(den) < 1e-12) {
            if (num < 0) empty = true;
            return;
        }
        const double t = num / den;
        if (den < 0) {
            if (t > in) in = t, n = pn;
        } else {
            out = std::min(out, t);
        }
    }
    bool hit(double tMax, const V3& o, const V3& d, Hit& h) const {
        if (empty || in > out || out < 0 || in > tMax) return false;
        if (in <= 0) {
            h.t = 0, h.p = o, h.n = -d;
            return true;
        }
        h.t = in, h.p = o + d * in, h.n = n;
        return true;
    }
};

// Nearest solid point of a vertical cylinder, and its surface normal.
Near cylinderNear(double cx, double cz, double y0, double y1, double r, const V3& q) {
    Near out;
    const double dx = q.x - cx, dz = q.z - cz, rd = std::hypot(dx, dz);
    const V3 radial = rd > 1e-9 ? V3(dx / rd, 0, dz / rd) : V3(1, 0, 0);
    const bool inR = rd <= r, inY = q.y >= y0 && q.y <= y1;
    if (inR && inY) {  // inside: out through the nearest of side, top and bottom
        const double side = r - rd, up = y1 - q.y, down = q.y - y0;
        if (side <= up && side <= down) out.p = V3(cx, q.y, cz) + radial * r, out.n = radial;
        else if (up <= down) out.p = V3(q.x, y1, q.z), out.n = V3(0, 1, 0);
        else out.p = V3(q.x, y0, q.z), out.n = V3(0, -1, 0);
        out.dist = 0;
        return out;
    }
    out.p = V3(cx, 0, cz) + radial * std::min(rd, r);
    out.p.y = clampd(q.y, y0, y1);
    const V3 d = q - out.p;
    out.dist = len(d);
    out.n = out.dist > 1e-9 ? d / out.dist : radial;
    return out;
}

}  // namespace

// ---- Rays -------------------------------------------------------------------------------------

bool rayRock(const Rock& k, const V3& o, const V3& d, double tMax, Hit& out) {
    // The bounding sphere only rejects: a ray that misses it misses the rock.
    const V3 oc = o - k.c;
    const double b = dot(oc, d), c = dot(oc, oc) - k.r * k.r;
    if (c > 0 && (b > 0 || b * b - c < 0)) return false;
    if (!k.parts.empty()) {  // (the nearest of its pieces)
        bool any = false;
        double best = tMax;
        for (const Rock& p : k.parts) {
            Hit h;
            if (rayRock(p, o, d, best, h) && (!any || h.t < out.t)) out = h, best = h.t, any = true;
        }
        return any;
    }
    Interval iv;
    for (size_t f = 0; f < k.f.size() && !iv.empty; f++) iv.clip(k.fn[f], k.fd[f], o, d);
    return iv.hit(tMax, o, d, out);
}

bool rayBox(const Box& bx, const V3& o, const V3& d, double tMax, Hit& out) {
    Interval iv;
    const double he[3] = {bx.h.x, bx.h.y, bx.h.z};
    for (int i = 0; i < 3; i++) {
        const double c = dot(bx.ax[i], bx.c);
        iv.clip(bx.ax[i], c + he[i], o, d);
        iv.clip(bx.ax[i] * -1.0, -c + he[i], o, d);
    }
    return iv.hit(tMax, o, d, out);
}

bool rayPole(const Pole& p, const V3& o, const V3& d, double tMax, Hit& out) {
    Interval iv;
    iv.clip(V3(0, 1, 0), p.y1, o, d);
    iv.clip(V3(0, -1, 0), -p.y0, o, d);
    double t0, t1;
    if (circleRoots(o.x, o.z, d.x, d.z, p.x, p.z, p.r, t0, t1)) {
        if (t0 > iv.in) iv.in = t0, iv.n = V3(o.x + d.x * t0 - p.x, 0, o.z + d.z * t0 - p.z) / p.r;
        iv.out = std::min(iv.out, t1);
    } else if ((o.x - p.x) * (o.x - p.x) + (o.z - p.z) * (o.z - p.z) > p.r * p.r || std::hypot(d.x, d.z) > 1e-6) {
        return false;  // (straight up or down, outside the circle: or a miss)
    }
    return iv.hit(tMax, o, d, out);
}

bool rayLog(const Log& g, const V3& o, const V3& d, double tMax, Hit& out) {
    const V3 ab = g.b - g.a;
    const double L2 = std::max(dot(ab, ab), 1e-12);
    {
        const double s = clampd(dot(o - g.a, ab) / L2, 0, 1);
        if (len(o - (g.a + ab * s)) <= g.r) return out.t = 0, out.p = o, out.n = -d, true;
    }
    bool hit = false;
    double best = tMax;
    auto take = [&](double t, const V3& at, const V3& n) {
        if (t >= 0 && t <= best) best = t, out.t = t, out.p = at, out.n = n, hit = true;
    };
    // The cylinder part: the ray against the infinite cylinder round the axis, kept where it's between the ends.
    const V3 u = ab / std::sqrt(L2);
    const V3 oa = o - g.a;
    const V3 dp = d - u * dot(d, u), op = oa - u * dot(oa, u);
    const double A = dot(dp, dp), B = dot(op, dp), C = dot(op, op) - g.r * g.r;
    if (A > 1e-12) {
        const double disc = B * B - A * C;
        if (disc >= 0) {
            const double t = (-B - std::sqrt(disc)) / A;
            const V3 at = o + d * t;
            const double s = dot(at - g.a, ab) / L2;
            if (s >= 0 && s <= 1) take(t, at, norm(at - (g.a + ab * s)));
        }
    }
    // The two rounded ends.
    for (int e = 0; e < 2; e++) {
        const V3 c = e ? g.b : g.a;
        const V3 oc = o - c;
        const double b = dot(oc, d), cc = dot(oc, oc) - g.r * g.r, disc = b * b - cc;
        if (disc < 0) continue;
        const double t = -b - std::sqrt(disc);
        const V3 at = o + d * t;
        const double s = dot(at - g.a, ab) / L2;
        if (e == 0 ? s <= 0 : s >= 1) take(t, at, norm(at - c));
    }
    return hit;
}

bool rayWorld(const World& w, const V3& o, const V3& d, double tMax, WorldHit& out) {
    const V3 e = o + d * tMax;
    World::NearbySolids near;
    w.solidsNear(std::min(o.x, e.x) - 0.5, std::min(o.z, e.z) - 0.5, std::max(o.x, e.x) + 0.5, std::max(o.z, e.z) + 0.5, near);
    out = WorldHit();
    double best = tMax;
    auto take = [&](Kind kind, int i, const Hit& h) {
        if (h.t <= best) best = h.t, out.kind = kind, out.index = i, out.hit = h;
    };
    Hit h;
    for (int i : near.rocks) if (rayRock(w.rocks[i], o, d, best, h)) take(SH_ROCK, i, h);
    for (int i : near.logs) if (rayLog(w.logs[i], o, d, best, h)) take(SH_LOG, i, h);
    for (int i : near.poles) if (rayPole(w.poles[i], o, d, best, h)) take(SH_POLE, i, h);
    for (int i : near.boxes) if (w.boxes[i].solid && rayBox(w.boxes[i], o, d, best, h)) take(SH_BOX, i, h);
    return out.kind != SH_NONE;
}

// ---- Nearest point on a surface -----------------------------------------------------------------

Near closestOnRock(const Rock& k, const V3& q) {
    if (!k.parts.empty()) {  // (the nearest of its pieces)
        Near best;
        best.dist = kInf;
        for (const Rock& p : k.parts) {
            if (len(q - p.c) - p.r > best.dist) continue;
            const Near n = closestOnRock(p, q);
            if (n.dist < best.dist) best = n;
            if (best.dist <= 0) break;
        }
        return best;
    }
    Near out;
    double far = -kInf;
    int fi = 0;
    for (size_t f = 0; f < k.f.size(); f++) {
        const double s = dot(k.fn[f], q) - k.fd[f];
        if (s > far) far = s, fi = (int)f;
    }
    if (far <= 0) {  // inside: out through the nearest face
        out.n = k.fn[fi], out.p = q - k.fn[fi] * far, out.dist = 0;
        return out;
    }
    double best = kInf;
    for (size_t f = 0; f < k.f.size(); f++) {
        if (dot(k.fn[f], q) - k.fd[f] <= 0) continue;  // (a face it can't see holds nothing nearer)
        const V3 t = closestOnTri(q, k.v[k.f[f][0]], k.v[k.f[f][1]], k.v[k.f[f][2]]);
        const double e = dot(q - t, q - t);
        if (e < best) best = e, out.p = t;
    }
    out.dist = std::sqrt(best);
    out.n = out.dist > 1e-9 ? (q - out.p) / out.dist : k.fn[fi];
    return out;
}

Near closestOnLog(const Log& g, const V3& q) {
    Near out;
    const V3 ab = g.b - g.a;
    const double s = clampd(dot(q - g.a, ab) / std::max(dot(ab, ab), 1e-12), 0, 1);
    const V3 c = g.a + ab * s, d = q - c;
    const double l = len(d);
    out.n = l > 1e-9 ? d / l : V3(0, 1, 0);
    out.p = c + out.n * g.r;
    out.dist = std::max(0.0, l - g.r);
    return out;
}

Near closestOnPole(const Pole& p, const V3& q) { return cylinderNear(p.x, p.z, p.y0, p.y1, p.r, q); }

Near closestOnBox(const Box& b, const V3& q) {
    Near out;
    const V3 d = q - b.c;
    const double he[3] = {b.h.x, b.h.y, b.h.z};
    double l[3];
    bool inside = true;
    V3 p = b.c;
    for (int i = 0; i < 3; i++) {
        l[i] = dot(d, b.ax[i]);
        const double c = clampd(l[i], -he[i], he[i]);
        if (c != l[i]) inside = false;
        p += b.ax[i] * c;
    }
    if (!inside) {
        const V3 df = q - p;
        out.p = p, out.dist = len(df), out.n = df / std::max(out.dist, 1e-12);
        return out;
    }
    int best = 0;
    double bd = kInf;
    for (int i = 0; i < 3; i++)
        if (he[i] - std::fabs(l[i]) < bd) bd = he[i] - std::fabs(l[i]), best = i;
    out.n = b.ax[best] * (l[best] >= 0 ? 1.0 : -1.0);
    out.p = q + out.n * bd;
    return out;
}

bool nearestSolid(const World& w, const V3& q, double maxDist, Kind& kind, int& index, Near& out) {
    World::NearbySolids near;
    w.solidsNear(q.x - maxDist, q.z - maxDist, q.x + maxDist, q.z + maxDist, near);
    double best = maxDist;
    kind = SH_NONE, index = -1;
    auto take = [&](Kind kd, int i, const Near& n) {
        if (n.dist <= best) best = n.dist, kind = kd, index = i, out = n;
    };
    for (int i : near.rocks)
        if (len(q - w.rocks[i].c) - w.rocks[i].r <= best) take(SH_ROCK, i, closestOnRock(w.rocks[i], q));
    for (int i : near.logs) take(SH_LOG, i, closestOnLog(w.logs[i], q));
    for (int i : near.poles) take(SH_POLE, i, closestOnPole(w.poles[i], q));
    for (int i : near.boxes)
        if (w.boxes[i].solid) take(SH_BOX, i, closestOnBox(w.boxes[i], q));
    return kind != SH_NONE;
}

// ---- Cross-sections --------------------------------------------------------------------------------

namespace {

double cross2(const P2& o, const P2& a, const P2& b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); }

// Convex hull of plan points, counter-clockwise (monotone chain).
std::vector<P2> hull2(std::vector<P2> pts) {
    std::sort(pts.begin(), pts.end(), [](const P2& a, const P2& b) { return a.x < b.x || (a.x == b.x && a.z < b.z); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const P2& a, const P2& b) { return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.z - b.z) < 1e-9; }), pts.end());
    if (pts.size() < 3) return pts;
    std::vector<P2> h(2 * pts.size());
    size_t n = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        while (n >= 2 && cross2(h[n - 2], h[n - 1], pts[i]) <= 1e-12) n--;
        h[n++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lo = n + 1; i > 0; i--) {
        while (n >= lo && cross2(h[n - 2], h[n - 1], pts[i - 1]) <= 1e-12) n--;
        h[n++] = pts[i - 1];
    }
    h.resize(n - 1);
    return h;
}

// The polygon (counter-clockwise) grown by d with rounded corners, a vertex every `stepDeg` of turn.
std::vector<P2> grow(const std::vector<P2>& poly, double d, double stepDeg) {
    const int n = (int)poly.size();
    std::vector<P2> out;
    std::vector<P2> nrm(n);
    for (int i = 0; i < n; i++) {
        const P2 &a = poly[i], &b = poly[(i + 1) % n];
        const double dx = b.x - a.x, dz = b.z - a.z, l = std::max(std::hypot(dx, dz), 1e-12);
        nrm[i] = {dz / l, -dx / l};  // (right of travel: out, for a counter-clockwise outline)
    }
    for (int i = 0; i < n; i++) {
        const P2& nA = nrm[(i + n - 1) % n];
        const P2& nB = nrm[i];
        const double a0 = std::atan2(nA.z, nA.x);
        double sweep = std::atan2(nB.z, nB.x) - a0;
        while (sweep < 0) sweep += 2 * M_PI;
        while (sweep > 2 * M_PI) sweep -= 2 * M_PI;
        if (sweep > M_PI) sweep = 0;  // (a numerically straight join)
        const int steps = std::max(1, (int)std::ceil(sweep / (stepDeg * M_PI / 180)));
        for (int s = 0; s <= steps; s++) {
            const double a = a0 + sweep * s / steps;
            out.push_back({poly[i].x + d * std::cos(a), poly[i].z + d * std::sin(a)});
        }
    }
    return out;
}

// The point of segment ab nearest p.
P2 nearOnSeg(const P2& p, const P2& a, const P2& b) {
    const double dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
    const double t = l2 < 1e-18 ? 0 : clampd(((p.x - a.x) * dx + (p.z - a.z) * dz) / l2, 0, 1);
    return {a.x + dx * t, a.z + dz * t};
}

}  // namespace

double rockTop(const Rock& k) {
    double t = -kInf;
    for (const V3& v : k.v) t = std::max(t, v.y);
    return t;
}
double rockBottom(const Rock& k) {
    double t = kInf;
    for (const V3& v : k.v) t = std::min(t, v.y);
    return t;
}

double polyArea(const std::vector<P2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) a += p[i].x * p[(i + 1) % p.size()].z - p[(i + 1) % p.size()].x * p[i].z;
    return a * 0.5;
}

std::vector<P2> rockSection(const Rock& k, double y) {
    std::vector<P2> pts;
    for (const auto& f : k.f)
        for (int e = 0; e < 3; e++) {
            const V3 &a = k.v[f[e]], &b = k.v[f[(e + 1) % 3]];
            if ((a.y < y) == (b.y < y)) continue;
            const double t = (y - a.y) / (b.y - a.y);
            pts.push_back({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t});
        }
    std::vector<P2> h = hull2(pts);
    return h.size() >= 3 ? h : std::vector<P2>();
}

Wrap wrapRock(const Rock& k, double y, const V3& from, double standoff) {
    Wrap w;
    const std::vector<P2> sec = rockSection(k, y);
    if (sec.size() < 3) return w;
    const int n = (int)sec.size();
    const P2 f{from.x, from.z};
    // The nearest point of the section to `from`, and which way out of it that is.
    P2 q = sec[0];
    double bd = kInf;
    for (int i = 0; i < n; i++) {
        const P2 c = nearOnSeg(f, sec[i], sec[(i + 1) % n]);
        const double e = (c.x - f.x) * (c.x - f.x) + (c.z - f.z) * (c.z - f.z);
        if (e < bd) bd = e, q = c;
    }
    bool inside = true;
    for (int i = 0; i < n && inside; i++) inside = cross2(sec[i], sec[(i + 1) % n], f) >= 0;
    double ux = f.x - q.x, uz = f.z - q.z;
    if (inside) ux = -ux, uz = -uz;
    double ul = std::hypot(ux, uz);
    if (ul < 1e-9) {  // (right on it: away from the middle)
        P2 c{0, 0};
        for (const P2& s : sec) c.x += s.x / n, c.z += s.z / n;
        ux = q.x - c.x, uz = q.z - c.z, ul = std::max(std::hypot(ux, uz), 1e-9);
    }
    ux /= ul, uz /= ul;
    std::vector<P2> loop = standoff > 1e-9 ? grow(sec, standoff, 12) : sec;
    const P2 A{q.x + ux * standoff, q.z + uz * standoff};
    // Put the attach point in the loop where it lies (nearest segment), and start the loop there.
    const int m = (int)loop.size();
    int seg = 0;
    bd = kInf;
    for (int i = 0; i < m; i++) {
        const P2 c = nearOnSeg(A, loop[i], loop[(i + 1) % m]);
        const double e = (c.x - A.x) * (c.x - A.x) + (c.z - A.z) * (c.z - A.z);
        if (e < bd) bd = e, seg = i;
    }
    w.loop.push_back(V3(A.x, y, A.z));
    for (int s = 1; s <= m; s++) {  // (A, then on round to the start of the segment it's in)
        const P2& p = loop[(seg + s) % m];
        if (std::hypot(p.x - A.x, p.z - A.z) < 1e-6) continue;
        w.loop.push_back(V3(p.x, y, p.z));
    }
    w.attach = w.loop[0];
    w.out = V3(ux, 0, uz);
    w.y = y;
    w.ok = true;
    return w;
}

Wrap wrapTrunk(const Pole& p, double y, const V3& from, double standoff) {
    Wrap w;
    const double R = trunkRadius(p, y) + standoff;
    double a0 = std::atan2(from.z - p.z, from.x - p.x);
    if (!std::isfinite(a0)) a0 = 0;
    const int n = 36;
    for (int i = 0; i < n; i++) {
        const double a = a0 + 2 * M_PI * i / n;
        w.loop.push_back(V3(p.x + R * std::cos(a), y, p.z + R * std::sin(a)));
    }
    w.attach = w.loop[0];
    w.out = V3(std::cos(a0), 0, std::sin(a0));
    w.y = y;
    w.ok = true;
    return w;
}

bool rockStrapHeight(const World& w, const Rock& k, double& yOut) {
    const double top = rockTop(k), bottom = rockBottom(k);
    const double step = 0.04, clear = 0.08;
    struct Sample {
        double y, area;
    };
    std::vector<Sample> ok;  // (top down)
    for (double y = top - 0.05; y > bottom + 0.02; y -= step) {
        const std::vector<P2> sec = rockSection(k, y);
        if (sec.size() < 3) continue;
        // The strap must clear the ground all the way round (a rock sunk into a slope).
        double ground = -kInf;
        for (size_t i = 0; i < sec.size(); i++) {
            const P2 &a = sec[i], &b = sec[(i + 1) % sec.size()];
            ground = std::max(ground, std::max(w.height(a.x, a.z), w.height((a.x + b.x) * 0.5, (a.z + b.z) * 0.5)));
        }
        if (y - ground >= clear) ok.push_back({y, polyArea(sec)});
    }
    if (ok.empty()) return yOut = top - 0.05, false;
    size_t widest = 0;
    for (size_t i = 0; i < ok.size(); i++)
        if (ok[i].area > ok[widest].area * 1.0001) widest = i;
    const double maxArea = ok[widest].area;
    if (widest + 1 < ok.size()) {  // the widest part is above the lowest the strap can go: a bit under it, where it won't ride up
        yOut = std::max(ok.back().y, ok[widest].y - 0.05);
    } else {  // the widest is as low as it goes (a cone sunk in the ground): up to where it's nearly as wide, so it sits on the visible rock
        size_t pick = widest;
        for (size_t i = 0; i < ok.size(); i++)
            if (ok[i].area >= 0.85 * maxArea) {
                pick = i;
                break;
            }
        yOut = ok[pick].y;
    }
    return true;
}

Wrap wrapRockForPull(const World& w, const Rock& k, const V3& from) {
    double y;
    rockStrapHeight(w, k, y);
    return wrapRock(k, y, from);
}

std::vector<V3> localPath(const Wrap& w, double gap) {
    std::vector<V3> out;
    const int n = (int)w.loop.size();
    if (n < 3) return out;
    const V3 A = w.loop[0];
    const V3 left(-w.out.z, 0, w.out.x);
    auto local = [&](const V3& p) { return V3(dot(p - A, w.out), 0, dot(p - A, left)); };
    std::vector<double> cum(n + 1, 0.0);
    for (int i = 0; i < n; i++) cum[i + 1] = cum[i] + len(w.loop[(i + 1) % n] - w.loop[i]);
    const double L = cum[n];
    if (L < 2.5 * gap) return out;
    auto at = [&](double s) {
        int i = 0;
        while (i + 1 < n && cum[i + 1] < s) i++;
        const double f = (s - cum[i]) / std::max(1e-12, cum[i + 1] - cum[i]);
        return w.loop[i] + (w.loop[(i + 1) % n] - w.loop[i]) * f;
    };
    out.push_back(local(at(gap)));
    for (int i = 1; i < n; i++)
        if (cum[i] > gap + 1e-6 && cum[i] < L - gap - 1e-6) out.push_back(local(w.loop[i]));
    out.push_back(local(at(L - gap)));
    return out;
}

int rockAt(const World& w, const V3& q, double maxDist) {
    World::NearbySolids near;
    w.solidsNear(q.x - maxDist, q.z - maxDist, q.x + maxDist, q.z + maxDist, near);
    int best = -1;
    double bd = maxDist;
    for (int i : near.rocks) {
        const Rock& k = w.rocks[i];
        if (len(q - k.c) - k.r > bd) continue;
        const double d = closestOnRock(k, q).dist;
        if (d <= bd) bd = d, best = i;
    }
    return best;
}

}  // namespace objshape
