#include "modelkit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include "font.h"
#include "platform.h"

namespace mk {

namespace {
// The picks and the mesh cache are shared by every thread that builds a world (the test runner builds several at once; a
// server may too). One lock covers loading the picks, the cache and the lookups on them. Recursive: loadChoices drops the cache.
std::recursive_mutex& picksLock() {
    static std::recursive_mutex m;
    return m;
}
}  // namespace

// ---- Mesh, params, catalog ----------------------------------------------------------

void Mesh::finish() {
    lo = V3(1e9, 1e9, 1e9), hi = V3(-1e9, -1e9, -1e9);
    for (const MVert& m : v) {
        lo.x = std::min(lo.x, (double)m.p[0]), lo.y = std::min(lo.y, (double)m.p[1]), lo.z = std::min(lo.z, (double)m.p[2]);
        hi.x = std::max(hi.x, (double)m.p[0]), hi.y = std::max(hi.y, (double)m.p[1]), hi.z = std::max(hi.z, (double)m.p[2]);
    }
    if (v.empty()) lo = hi = V3();
}

const std::vector<std::string>& paintNames() {
    static const std::vector<std::string> n = {"RED", "OLIVE", "SAND", "BLACK", "ORANGE", "BLUE", "WHITE", "GREEN", "YELLOW", "GREY"};
    return n;
}

V3 paint(int i) {
    static const V3 c[] = {V3(0.62, 0.08, 0.06), V3(0.28, 0.31, 0.17), V3(0.70, 0.60, 0.42), V3(0.07, 0.07, 0.075), V3(0.92, 0.38, 0.05),
                           V3(0.10, 0.24, 0.55), V3(0.88, 0.88, 0.85), V3(0.10, 0.36, 0.17), V3(0.95, 0.72, 0.06), V3(0.42, 0.43, 0.44)};
    const int n = (int)(sizeof c / sizeof c[0]);
    return c[((i % n) + n) % n];
}

double P::operator()(const std::string& key) const {
    auto it = v.find(key);
    return it == v.end() ? 0.0 : it->second;
}

namespace {
std::vector<ModelDef>& registry() {
    static std::vector<ModelDef> r;
    return r;
}
}  // namespace

Reg::Reg(ModelDef def) { registry().push_back(std::move(def)); }

const std::vector<ModelDef>& catalog() { return registry(); }

const ModelDef* findModel(const std::string& id) {
    for (const ModelDef& d : registry())
        if (d.id == id) return &d;
    return nullptr;
}

P defaults(const Variant& v) {
    P p;
    for (const Param& q : v.params) p.v[q.key] = q.def;
    return p;
}

void build(const ModelDef& def, int variant, const P& p, Mesh& out) {
    out.v.clear();
    if (def.variants.empty()) return;
    const Variant& var = def.variants[std::clamp(variant, 0, (int)def.variants.size() - 1)];
    P full = defaults(var);
    for (const auto& kv : p.v) full.v[kv.first] = kv.second;
    B b(out);
    if (var.build) var.build(b, full);
    out.finish();
}

// ---- Helpers ----------------------------------------------------------------------------

V3 mix(const V3& a, const V3& b, double t) { return a + (b - a) * t; }

double hash01(uint32_t seed, uint32_t i) {
    uint32_t h = seed * 0x9E3779B1u ^ (i + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
    return (h & 0xFFFFFF) / double(0x1000000);
}

std::vector<V2> roundedRect(double w, double h, double r, int segs) {
    r = std::clamp(r, 0.0, std::min(w, h) / 2);
    std::vector<V2> pts;
    const double cx[4] = {w / 2 - r, -w / 2 + r, -w / 2 + r, w / 2 - r}, cy[4] = {h / 2 - r, h / 2 - r, -h / 2 + r, -h / 2 + r};
    for (int k = 0; k < 4; k++) {
        if (r <= 1e-6) {
            pts.push_back(V2(cx[k], cy[k]));
            continue;
        }
        for (int s = 0; s <= segs; s++) {
            const double a = (k + s / (double)segs) * M_PI / 2;
            pts.push_back(V2(cx[k] + r * std::cos(a), cy[k] + r * std::sin(a)));
        }
    }
    return pts;
}

std::vector<V2> circle(double r, int segs, double cx, double cy) {
    std::vector<V2> pts;
    for (int s = 0; s < segs; s++) pts.push_back(V2(cx + r * std::cos(2 * M_PI * s / segs), cy + r * std::sin(2 * M_PI * s / segs)));
    return pts;
}

namespace {

double noise3(const V3& p) {  // smooth value noise, ~0..1
    auto h = [](int x, int y, int z) {
        uint32_t n = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
        n = (n << 13) ^ n;
        return ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffff) / 2147483647.0;
    };
    const int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
    const double fx = p.x - xi, fy = p.y - yi, fz = p.z - zi;
    const double u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy), w = fz * fz * (3 - 2 * fz);
    double r = 0;
    for (int k = 0; k < 8; k++) {
        const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        r += h(xi + dx, yi + dy, zi + dz) * (dx ? u : 1 - u) * (dy ? v : 1 - v) * (dz ? w : 1 - w);
    }
    return r;
}

struct Ico {
    std::vector<V3> v;
    std::vector<std::array<int, 3>> f;
};

const Ico& ico(int sub) {
    static std::unique_ptr<Ico> cache[4];
    static std::mutex made;  // (builders run on several threads; each table is made once and never changes)
    std::lock_guard<std::mutex> lk(made);
    sub = std::clamp(sub, 0, 3);
    if (cache[sub]) return *cache[sub];
    Ico m;
    const double t = (1 + std::sqrt(5.0)) / 2;
    const double pv[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& q : pv) m.v.push_back(norm(V3(q[0], q[1], q[2])));
    m.f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
           {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (int s = 0; s < sub; s++) {
        std::map<std::pair<int, int>, int> mid;
        auto midpoint = [&](int a, int b) {
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            m.v.push_back(norm(m.v[a] + m.v[b]));
            return mid[key] = (int)m.v.size() - 1;
        };
        std::vector<std::array<int, 3>> nf;
        for (auto& f : m.f) {
            const int a = midpoint(f[0], f[1]), b = midpoint(f[1], f[2]), c = midpoint(f[2], f[0]);
            nf.push_back({f[0], a, c}), nf.push_back({f[1], b, a}), nf.push_back({f[2], c, b}), nf.push_back({a, b, c});
        }
        m.f = nf;
    }
    cache[sub] = std::make_unique<Ico>(m);
    return *cache[sub];
}

double area2(const std::vector<V2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const V2 &u = p[i], &w = p[(i + 1) % p.size()];
        a += u.x * w.y - w.x * u.y;
    }
    return a;
}

// Ear clipping: triangles (indices) of a simple polygon, made counter-clockwise first.
std::vector<std::array<int, 3>> triangulate(const std::vector<V2>& poly) {
    std::vector<std::array<int, 3>> tris;
    const int n = (int)poly.size();
    if (n < 3) return tris;
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    if (area2(poly) < 0) std::reverse(idx.begin(), idx.end());
    auto cross2 = [](const V2& a, const V2& b, const V2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        const int m = (int)idx.size();
        for (int i = 0; i < m; i++) {
            const int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
            const V2 &a = poly[ia], &b = poly[ib], &c = poly[ic];
            if (cross2(a, b, c) <= 1e-12) continue;  // reflex (or flat)
            bool inside = false;
            for (int j = 0; j < m && !inside; j++) {
                const int ij = idx[j];
                if (ij == ia || ij == ib || ij == ic) continue;
                const V2& p = poly[ij];
                inside = cross2(a, b, p) >= 0 && cross2(b, c, p) >= 0 && cross2(c, a, p) >= 0;
            }
            if (inside) continue;
            tris.push_back({ia, ib, ic});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;  // (not simple: give up on the rest)
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    return tris;
}

}  // namespace

// ---- Builder: frame ----------------------------------------------------------------------

void B::push() { stack_.push_back(f_); }
void B::pop() {
    if (!stack_.empty()) f_ = stack_.back(), stack_.pop_back();
}
B& B::move(const V3& d) {
    f_.o += f_.x * d.x + f_.y * d.y + f_.z * d.z;
    return *this;
}
B& B::rotate(const V3& axis, double deg) {
    const double a = deg * M_PI / 180;
    const V3 k = norm(axis);
    const V3 ex = rotateAxis(V3(1, 0, 0), k, a), ey = rotateAxis(V3(0, 1, 0), k, a), ez = rotateAxis(V3(0, 0, 1), k, a);
    const Frame o = f_;
    f_.x = o.x * ex.x + o.y * ex.y + o.z * ex.z;
    f_.y = o.x * ey.x + o.y * ey.y + o.z * ey.z;
    f_.z = o.x * ez.x + o.y * ez.y + o.z * ez.z;
    return *this;
}
B& B::yaw(double deg) { return rotate(V3(0, 1, 0), deg); }
B& B::pitch(double deg) { return rotate(V3(0, 0, 1), deg); }
B& B::roll(double deg) { return rotate(V3(1, 0, 0), deg); }
B& B::scale(const V3& s) {
    f_.x *= s.x, f_.y *= s.y, f_.z *= s.z;
    return *this;
}
B& B::mirrorZ() {
    f_.z = -f_.z;
    return *this;
}
void B::at(const V3& p, double yawDeg, const std::function<void()>& fn) {
    push();
    move(p);
    if (yawDeg != 0) yaw(yawDeg);
    fn();
    pop();
}
void B::both(const std::function<void()>& fn) {
    fn();
    push();
    mirrorZ();
    fn();
    pop();
}
V3 B::world(const V3& p) const { return f_.o + f_.x * p.x + f_.y * p.y + f_.z * p.z; }

V3 B::tnormal(const V3& n) const {
    const V3 cx = cross(f_.y, f_.z), cy = cross(f_.z, f_.x), cz = cross(f_.x, f_.y);
    const double det = dot(f_.x, cx);
    const V3 r = (cx * n.x + cy * n.y + cz * n.z) * (det < 0 ? -1.0 : 1.0);
    const double l = len(r);
    return l > 1e-12 ? r / l : V3(0, 1, 0);
}

void B::emit(const V3& p, const V3& n) {
    const V3 w = world(p), wn = tnormal(n);
    MVert m;
    m.p[0] = (float)w.x, m.p[1] = (float)w.y, m.p[2] = (float)w.z;
    m.n[0] = (float)wn.x, m.n[1] = (float)wn.y, m.n[2] = (float)wn.z;
    m.c[0] = (float)col_.x, m.c[1] = (float)col_.y, m.c[2] = (float)col_.z;
    m.mat = mat_;
    out_.v.push_back(m);
}

// ---- Builder: primitives ---------------------------------------------------------------

void B::triN(const V3& a, const V3& b, const V3& c, const V3& na, const V3& nb, const V3& nc) {
    if (len(cross(b - a, c - a)) < 1e-12) return;  // degenerate
    emit(a, na), emit(b, nb), emit(c, nc);
}

void B::tri(const V3& a, const V3& b, const V3& c) {
    const V3 n = cross(b - a, c - a);
    if (len(n) < 1e-12) return;
    const V3 nn = norm(n);
    emit(a, nn), emit(b, nn), emit(c, nn);
}

void B::quad(const V3& a, const V3& b, const V3& c, const V3& d) {
    V3 n = cross(c - a, d - b);
    if (len(n) < 1e-12) n = cross(b - a, c - a);
    if (len(n) < 1e-12) return;
    n = norm(n);
    triN(a, b, c, n, n, n), triN(a, c, d, n, n, n);
}

void B::box(const V3& c, const V3& size, double bevel) {
    const double h[3] = {size.x / 2, size.y / 2, size.z / 2};
    const double r = std::clamp(bevel, 0.0, std::min({h[0], h[1], h[2]}) * 0.98);
    const double in[3] = {h[0] - r, h[1] - r, h[2] - r};
    auto e = [](int a) { return a == 0 ? V3(1, 0, 0) : a == 1 ? V3(0, 1, 0) : V3(0, 0, 1); };
    // Faces.
    for (int a = 0; a < 3; a++)
        for (int s = -1; s <= 1; s += 2) {
            const int u = (a + 1) % 3, w = (a + 2) % 3;
            const V3 n = e(a) * s, fc = c + n * h[a], du = e(u) * in[u], dw = e(w) * in[w];
            const V3 q[4] = {fc - du - dw, fc + du - dw, fc + du + dw, fc - du + dw};
            triN(q[0], q[1], q[2], n, n, n), triN(q[0], q[2], q[3], n, n, n);
        }
    if (r <= 0) return;
    // Edge strips (smooth: each side keeps its face's normal, so the edge looks rounded).
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++) {
            const int k = 3 - a - b;
            for (int sa = -1; sa <= 1; sa += 2)
                for (int sb = -1; sb <= 1; sb += 2) {
                    const V3 na = e(a) * sa, nb = e(b) * sb;
                    const V3 p1 = c + na * h[a] + nb * in[b], p2 = c + na * in[a] + nb * h[b];
                    const V3 dk = e(k) * in[k];
                    triN(p1 - dk, p2 - dk, p2 + dk, na, nb, nb), triN(p1 - dk, p2 + dk, p1 + dk, na, nb, na);
                }
        }
    // Corners.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const V3 p1 = c + V3(sx * h[0], sy * in[1], sz * in[2]), p2 = c + V3(sx * in[0], sy * h[1], sz * in[2]),
                         p3 = c + V3(sx * in[0], sy * in[1], sz * h[2]);
                triN(p1, p2, p3, V3(sx, 0, 0), V3(0, sy, 0), V3(0, 0, sz));
            }
}

void B::beam(const V3& a, const V3& b, double w, double h, const V3& up, double bevel) {
    const V3 d = b - a;
    const double L = len(d);
    if (L < 1e-9) return;
    const V3 x = d / L;
    V3 u = up - x * dot(up, x);
    if (len(u) < 1e-6) u = std::fabs(x.y) < 0.9 ? V3(0, 1, 0) - x * x.y : V3(1, 0, 0) - x * x.x;
    const V3 y = norm(u), z = cross(x, y);
    push();
    const Frame o = f_;
    f_.o = world((a + b) * 0.5);
    f_.x = o.x * x.x + o.y * x.y + o.z * x.z;
    f_.y = o.x * y.x + o.y * y.y + o.z * y.z;
    f_.z = o.x * z.x + o.y * z.y + o.z * z.z;
    box(V3(), V3(L, h, w), bevel);
    pop();
}

void B::cone(const V3& a, const V3& b, double ra, double rb, int segs, bool caps) {
    const V3 d = b - a;
    const double L = len(d);
    if (L < 1e-9) return;
    segs = std::max(3, segs);
    const V3 ax = d / L;
    const V3 u = norm(cross(ax, std::fabs(ax.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), w = cross(ax, u);
    const double slope = (ra - rb) / L;
    for (int s = 0; s < segs; s++) {
        const double a0 = 2 * M_PI * s / segs, a1 = 2 * M_PI * (s + 1) / segs;
        const V3 d0 = u * std::cos(a0) + w * std::sin(a0), d1 = u * std::cos(a1) + w * std::sin(a1);
        const V3 n0 = norm(d0 + ax * slope), n1 = norm(d1 + ax * slope);
        const V3 p0 = a + d0 * ra, p1 = a + d1 * ra, q0 = b + d0 * rb, q1 = b + d1 * rb;
        triN(p0, p1, q1, n0, n1, n1), triN(p0, q1, q0, n0, n1, n0);
        if (caps) {
            if (rb > 1e-9) triN(b, q0, q1, ax, ax, ax);
            if (ra > 1e-9) triN(a, p1, p0, -ax, -ax, -ax);
        }
    }
}

void B::cyl(const V3& a, const V3& b, double r, int segs, bool caps) { cone(a, b, r, r, segs, caps); }

void B::sphere(const V3& c, const V3& radii, int detail) {
    const Ico& m = ico(detail);
    auto P = [&](const V3& d) { return c + V3(d.x * radii.x, d.y * radii.y, d.z * radii.z); };
    auto N = [&](const V3& d) { return norm(V3(d.x / radii.x, d.y / radii.y, d.z / radii.z)); };
    for (auto& f : m.f) triN(P(m.v[f[0]]), P(m.v[f[1]]), P(m.v[f[2]]), N(m.v[f[0]]), N(m.v[f[1]]), N(m.v[f[2]]));
}

void B::blob(const V3& c, const V3& radii, double bump, uint32_t seed, int detail, double facet, double cutBelow) {
    const Ico& m = ico(detail);
    const V3 off(hash01(seed, 1) * 50, hash01(seed, 2) * 50, hash01(seed, 3) * 50);
    std::vector<V3> pos(m.v.size()), vn(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        const V3& d = m.v[i];
        const double nz = noise3(d * 1.7 + off) - 0.5 + 0.5 * (noise3(d * 3.9 + off * 1.3) - 0.5);
        const double k = 1 + bump * nz * 2;
        pos[i] = c + V3(d.x * radii.x, d.y * radii.y, d.z * radii.z) * k;
    }
    std::vector<V3> fn(m.f.size());
    for (size_t i = 0; i < m.f.size(); i++) {
        const auto& f = m.f[i];
        fn[i] = norm(cross(pos[f[1]] - pos[f[0]], pos[f[2]] - pos[f[0]]));
        if (dot(fn[i], (pos[f[0]] + pos[f[1]] + pos[f[2]]) / 3 - c) < 0) fn[i] = -fn[i];
        for (int k = 0; k < 3; k++) vn[f[k]] += fn[i];
    }
    for (size_t i = 0; i < m.f.size(); i++) {
        const auto& f = m.f[i];
        if (pos[f[0]].y < cutBelow && pos[f[1]].y < cutBelow && pos[f[2]].y < cutBelow) continue;
        V3 n[3];
        for (int k = 0; k < 3; k++) n[k] = norm(mix(norm(vn[f[k]]), fn[i], facet));
        triN(pos[f[0]], pos[f[1]], pos[f[2]], n[0], n[1], n[2]);
    }
}

void B::lathe(const V3& base, const std::vector<V2>& prof, int segs, bool smooth) {
    const int n = (int)prof.size();
    if (n < 2) return;
    segs = std::max(3, segs);
    // Each profile segment's outward normal in the (r, h) plane; at each joint, smooth if the
    // turn is gentle, else a crease.
    std::vector<V2> sn(n - 1);
    for (int i = 0; i + 1 < n; i++) {
        const double dr = prof[i + 1].x - prof[i].x, dh = prof[i + 1].y - prof[i].y, l = std::hypot(dr, dh);
        sn[i] = l > 1e-12 ? V2(dh / l, -dr / l) : V2(1, 0);
    }
    const double creaseCos = smooth ? std::cos(40 * M_PI / 180) : 2.0;
    auto jointN = [&](int seg, int end) {  // normal at segment seg's start (end 0) or end (1)
        const int j = seg + end;           // the profile point
        const int other = end ? seg + 1 : seg - 1;
        if (other < 0 || other >= n - 1) return sn[seg];
        const double c = sn[seg].x * sn[other].x + sn[seg].y * sn[other].y;
        if (c < creaseCos) return sn[seg];
        (void)j;
        const double x = sn[seg].x + sn[other].x, y = sn[seg].y + sn[other].y, l = std::hypot(x, y);
        return l > 1e-12 ? V2(x / l, y / l) : sn[seg];
    };
    for (int i = 0; i + 1 < n; i++) {
        const V2 a = prof[i], b = prof[i + 1];
        const V2 na = jointN(i, 0), nb = jointN(i, 1);
        for (int s = 0; s < segs; s++) {
            const double t0 = 2 * M_PI * s / segs, t1 = 2 * M_PI * (s + 1) / segs;
            const V3 d0(std::cos(t0), 0, -std::sin(t0)), d1(std::cos(t1), 0, -std::sin(t1));
            const V3 A0 = base + d0 * a.x + V3(0, a.y, 0), A1 = base + d1 * a.x + V3(0, a.y, 0);
            const V3 B0 = base + d0 * b.x + V3(0, b.y, 0), B1 = base + d1 * b.x + V3(0, b.y, 0);
            if (smooth) {
                const V3 nA0 = d0 * na.x + V3(0, na.y, 0), nA1 = d1 * na.x + V3(0, na.y, 0);
                const V3 nB0 = d0 * nb.x + V3(0, nb.y, 0), nB1 = d1 * nb.x + V3(0, nb.y, 0);
                triN(A0, A1, B1, nA0, nA1, nB1), triN(A0, B1, B0, nA0, nB1, nB0);
            } else {
                const V3 dm = norm(d0 + d1);
                const V3 fnn = norm(dm * sn[i].x + V3(0, sn[i].y, 0));
                triN(A0, A1, B1, fnn, fnn, fnn), triN(A0, B1, B0, fnn, fnn, fnn);
            }
        }
    }
}

void B::extrude(const std::vector<V2>& poly, double t, double z0) {
    const int n = (int)poly.size();
    if (n < 3) return;
    const auto tris = triangulate(poly);
    const double zf = z0 + t / 2, zb = z0 - t / 2;
    for (auto& tr : tris) {
        const V2 &a = poly[tr[0]], &b = poly[tr[1]], &c = poly[tr[2]];
        triN(V3(a.x, a.y, zf), V3(b.x, b.y, zf), V3(c.x, c.y, zf), V3(0, 0, 1), V3(0, 0, 1), V3(0, 0, 1));
        triN(V3(a.x, a.y, zb), V3(c.x, c.y, zb), V3(b.x, b.y, zb), V3(0, 0, -1), V3(0, 0, -1), V3(0, 0, -1));
    }
    const double sgn = area2(poly) >= 0 ? 1 : -1;
    for (int i = 0; i < n; i++) {
        const V2 &p = poly[i], &q = poly[(i + 1) % n];
        const double dx = q.x - p.x, dy = q.y - p.y, l = std::hypot(dx, dy);
        if (l < 1e-12) continue;
        const V3 nn(sgn * dy / l, -sgn * dx / l, 0);
        triN(V3(p.x, p.y, zb), V3(q.x, q.y, zb), V3(q.x, q.y, zf), nn, nn, nn);
        triN(V3(p.x, p.y, zb), V3(q.x, q.y, zf), V3(p.x, p.y, zf), nn, nn, nn);
    }
}

void B::extrudeUp(const std::vector<V2>& poly, double height, double y0) {
    push();
    const Frame o = f_;
    f_.y = o.z, f_.z = o.y;  // shape (x, y) -> (front, left), thickness -> up
    extrude(poly, height, y0 + height / 2);
    pop();
}

void B::torus(const V3& c, double R, double r, int segs, int sides, double arcDeg) {
    segs = std::max(3, segs), sides = std::max(3, sides);
    const double arc = std::clamp(arcDeg, 1.0, 360.0) * M_PI / 180;
    auto pt = [&](int i, int j, V3& p, V3& nn) {
        const double t = arc * i / segs, s = 2 * M_PI * j / sides;
        const V3 d(std::cos(t), 0, -std::sin(t));
        nn = d * std::cos(s) + V3(0, std::sin(s), 0);
        p = c + d * R + nn * r;
    };
    for (int i = 0; i < segs; i++)
        for (int j = 0; j < sides; j++) {
            V3 p00, p10, p11, p01, n00, n10, n11, n01;
            pt(i, j, p00, n00), pt(i + 1, j, p10, n10), pt(i + 1, j + 1, p11, n11), pt(i, j + 1, p01, n01);
            triN(p00, p10, p11, n00, n10, n11), triN(p00, p11, p01, n00, n11, n01);
        }
}

void B::tube(const std::vector<V3>& path, double r, int sides, bool closed, bool caps) {
    const int n = (int)path.size();
    if (n < 2) return;
    sides = std::max(3, sides);
    std::vector<V3> T(n), U(n);
    for (int i = 0; i < n; i++) {
        V3 t;
        if (closed) t = path[(i + 1) % n] - path[(i + n - 1) % n];
        else t = path[std::min(i + 1, n - 1)] - path[std::max(i - 1, 0)];
        T[i] = len(t) > 1e-12 ? norm(t) : V3(1, 0, 0);
    }
    U[0] = norm(cross(T[0], std::fabs(T[0].y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0)));
    for (int i = 1; i < n; i++) {  // parallel transport
        V3 u = U[i - 1] - T[i] * dot(U[i - 1], T[i]);
        U[i] = len(u) > 1e-9 ? norm(u) : norm(cross(T[i], V3(0, 1, 0)));
    }
    auto ring = [&](int i, int j, V3& p, V3& nn) {
        const double a = 2 * M_PI * j / sides;
        const V3 w = cross(T[i], U[i]);
        nn = U[i] * std::cos(a) + w * std::sin(a);
        p = path[i] + nn * r;
    };
    const int segsN = closed ? n : n - 1;
    for (int i = 0; i < segsN; i++) {
        const int k = (i + 1) % n;
        for (int j = 0; j < sides; j++) {
            V3 a, b, c, d, na, nb, nc, nd;
            ring(i, j, a, na), ring(i, j + 1, b, nb), ring(k, j + 1, c, nc), ring(k, j, d, nd);
            triN(a, b, c, na, nb, nc), triN(a, c, d, na, nc, nd);
        }
    }
    if (!closed && caps)
        for (int e = 0; e < 2; e++) {
            const int i = e ? n - 1 : 0;
            const V3 nn = e ? T[i] : -T[i];
            for (int j = 0; j < sides; j++) {
                V3 a, b, na, nb;
                ring(i, j, a, na), ring(i, j + 1, b, nb);
                triN(path[i], a, b, nn, nn, nn);
            }
        }
}

void B::letters(const std::string& s, const V3& c, double px, double depth) {
    const int n = (int)s.size();
    const double W = (6 * n - 1) * px;
    for (int k = 0; k < n; k++) {
        const char up = (char)std::toupper((unsigned char)s[k]);
        const Glyph* g = nullptr;
        for (const Glyph& gl : kGlyphs)
            if (gl.c == up) g = &gl;
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int c0 = 0; c0 < 5; c0++) {
                if (g->rows[r][c0] != '#' || (c0 > 0 && g->rows[r][c0 - 1] == '#')) continue;
                int c1 = c0;
                while (c1 + 1 < 5 && g->rows[r][c1 + 1] == '#') c1++;
                const double zl = c.z + W / 2 - (k * 6 + c0) * px, zr = c.z + W / 2 - (k * 6 + c1 + 1) * px;
                // (A flat face `depth` proud of the plane: the sides of something so thin don't show.)
                const double x = c.x + depth, yt = c.y + 3.5 * px - r * px, yb = yt - px;
                const V3 n(1, 0, 0);
                triN(V3(x, yb, zl), V3(x, yb, zr), V3(x, yt, zr), n, n, n), triN(V3(x, yb, zl), V3(x, yt, zr), V3(x, yt, zl), n, n, n);
            }
    }
}

// ---- Picks ---------------------------------------------------------------------------------

std::map<std::string, Choice>& choices() {
    static std::map<std::string, Choice> c;
    return c;
}

std::string choicesPath() {
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    static std::string found;
    if (!found.empty()) return found;
    if (const char* env = std::getenv("CZ_MODEL_PICKS")) return found = env;
    std::vector<std::string> bases = {"."};
    if (const std::string exe = exeDir(); !exe.empty()) bases.push_back(exe), bases.push_back(exe + "/../Resources");
    const char* rel[] = {"/assets", "/../assets", "/../../assets", "/../../../assets"};
    for (const std::string& b : bases)
        for (const char* r : rel)
            if (FILE* f = std::fopen((b + r + "/models/choices.txt").c_str(), "rb")) {
                std::fclose(f);
                return found = b + r + "/models/choices.txt";
            }
    for (const std::string& b : bases)  // none yet: next to the ground textures
        for (const char* r : rel)
            if (FILE* f = std::fopen((b + r + "/ground/brown_mud_03_diff_1k.jpg").c_str(), "rb")) {
                std::fclose(f);
                return found = b + r + "/models/choices.txt";
            }
    return "";
}

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
int variantByName(const ModelDef& d, const std::string& name) {
    for (int i = 0; i < (int)d.variants.size(); i++)
        if (d.variants[i].name == name) return i;
    return -1;
}
bool loaded = false;
}  // namespace

bool loadChoices(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    loaded = true;
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
    if (!f) return false;
    choices().clear();
    char buf[4096];
    std::string id;
    while (std::fgets(buf, sizeof buf, f)) {
        const std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            id = trim(line.substr(1, line.find(']') - 1));
            choices()[id];
            continue;
        }
        const ModelDef* d = findModel(id);
        if (!d) continue;  // (a model since dropped)
        Choice& c = choices()[id];
        if (line.rfind("tweak ", 0) == 0) {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const int v = variantByName(*d, trim(line.substr(6, colon - 6)));
            if (v < 0) continue;
            std::string rest = line.substr(colon + 1);
            size_t at = 0;
            while (at < rest.size()) {
                size_t comma = rest.find(',', at);
                if (comma == std::string::npos) comma = rest.size();
                const std::string kv = rest.substr(at, comma - at);
                const size_t eq = kv.find('=');
                if (eq != std::string::npos) c.tweaks[v][trim(kv.substr(0, eq))] = std::atof(kv.substr(eq + 1).c_str());
                at = comma + 1;
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "pick") c.pick = std::max(0, variantByName(*d, val));
        else if (key == "status") c.status = val == "liked" ? LIKED : val == "redo" ? REDO : NEW;
        else if (key == "note") c.note = val;
    }
    std::fclose(f);
    forgetMeshes();
    return true;
}

bool saveChoices(const std::string& path) {
    if (path.empty()) return false;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) makeDir(path.substr(0, slash));
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# Crumple Zone model picks, written by the model lab (crumplezone --lab).\n"
                    "# pick: the design the game uses. status: new, liked, or redo (with a note on what to change).\n"
                    "# tweak <design>: settings moved off their defaults.\n\n");
    for (const ModelDef& d : catalog()) {
        auto it = choices().find(d.id);
        if (it == choices().end()) continue;
        const Choice& c = it->second;
        std::fprintf(f, "[%s]\n", d.id.c_str());
        if (c.pick >= 0 && c.pick < (int)d.variants.size()) std::fprintf(f, "pick = %s\n", d.variants[c.pick].name.c_str());
        std::fprintf(f, "status = %s\n", c.status == LIKED ? "liked" : c.status == REDO ? "redo" : "new");
        if (!c.note.empty()) std::fprintf(f, "note = %s\n", c.note.c_str());
        for (const auto& tv : c.tweaks) {
            if (tv.first < 0 || tv.first >= (int)d.variants.size() || tv.second.empty()) continue;
            std::string kvs;
            for (const auto& kv : tv.second) {
                char num[64];
                std::snprintf(num, sizeof num, "%g", kv.second);
                kvs += (kvs.empty() ? " " : ", ") + kv.first + " = " + num;
            }
            std::fprintf(f, "tweak %s:%s\n", d.variants[tv.first].name.c_str(), kvs.c_str());
        }
        std::fprintf(f, "\n");
    }
    const bool ok = std::fclose(f) == 0;
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

int pickedVariant(const ModelDef& def) {
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    if (!loaded) loadChoices(choicesPath());
    auto it = choices().find(def.id);
    const int v = it == choices().end() ? 0 : it->second.pick;
    return std::clamp(v, 0, std::max(0, (int)def.variants.size() - 1));
}

P pickedParams(const ModelDef& def, int variant) {
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    if (!loaded) loadChoices(choicesPath());
    P p;
    if (def.variants.empty()) return p;
    p = defaults(def.variants[std::clamp(variant, 0, (int)def.variants.size() - 1)]);
    auto it = choices().find(def.id);
    if (it != choices().end()) {
        auto tv = it->second.tweaks.find(variant);
        if (tv != it->second.tweaks.end())
            for (const auto& kv : tv->second) p.v[kv.first] = kv.second;
    }
    return p;
}

namespace {
std::map<std::string, Mesh>& meshCache() {
    static std::map<std::string, Mesh> m;
    return m;
}
}  // namespace

namespace {
uint64_t version = 1;
}
void forgetMeshes() {
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    meshCache().clear(), version++;
}
uint64_t picksVersion() { return version; }

const Mesh& modelMesh(const std::string& id) {
    // (Locked for the whole build: another thread must not copy a mesh that's still being filled in. And the picks are loaded
    // first: loading them drops the cache, which would pull `m` out from under us.)
    std::lock_guard<std::recursive_mutex> lk(picksLock());
    if (!loaded) loadChoices(choicesPath());
    auto it = meshCache().find(id);
    if (it != meshCache().end()) return it->second;
    Mesh& m = meshCache()[id];
    if (const ModelDef* d = findModel(id)) {
        const int v = pickedVariant(*d);
        build(*d, v, pickedParams(*d, v), m);
    }
    return m;
}

std::vector<V3>& pathHint() {
    static thread_local std::vector<V3> hint;
    return hint;
}

Mesh modelMesh(const std::string& id, const std::vector<std::pair<std::string, double>>& over) {
    Mesh m;
    if (const ModelDef* d = findModel(id)) {
        const int v = pickedVariant(*d);
        P p = pickedParams(*d, v);
        for (const auto& kv : over) p.v[kv.first] = kv.second;
        build(*d, v, p, m);
    }
    return m;
}

}  // namespace mk
