#include "scenery.h"

#include <array>
#include <cmath>
#include <map>

namespace {

Vtx vert(const V3& p, const V3& n, const V3& c, int mat) {
    return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {(float)n.x, (float)n.y, (float)n.z}, {(float)c.x, (float)c.y, (float)c.z, (float)mat}};
}

// Unit icosphere: 0 = 20 faces, 1 = 80, 2 = 320.
struct Ico {
    std::vector<V3> v;
    std::vector<std::array<int, 3>> f;
};
Ico makeIco(int sub) {
    const double t = (1 + std::sqrt(5.0)) / 2;
    Ico m;
    m.v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (V3& p : m.v) p = norm(p);
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
            int a = midpoint(f[0], f[1]), b = midpoint(f[1], f[2]), c = midpoint(f[2], f[0]);
            nf.push_back({f[0], a, c});
            nf.push_back({f[1], b, a});
            nf.push_back({f[2], c, b});
            nf.push_back({a, b, c});
        }
        m.f = nf;
    }
    return m;
}
const Ico& ico(int sub) {
    static const Ico levels[3] = {makeIco(0), makeIco(1), makeIco(2)};
    return levels[std::min(std::max(sub, 0), 2)];
}

double noise3(const V3& p) {
    return (valueNoise(p.x, p.y) + valueNoise(p.y + 3.1, p.z - 1.3) + valueNoise(p.z - 7.7, p.x + 2.9)) / 3;
}

// A lumpy blob round c: radii (rx, ry, rz), surface pushed in and out by `bump`.
// `facet` mixes flat face normals in (rock) or keeps it soft (foliage).
void blob(std::vector<Vtx>& out, const V3& c, const V3& radii, double bump, double seed, int sub, double facet,
          const V3& col, int mat, double cutBelow = -1e9) {
    const Ico& m = ico(sub);
    std::vector<V3> p(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        const V3& d = m.v[i];
        double k = 1 + bump * (2 * noise3(d * 1.7 + V3(seed, seed * 0.7, -seed)) - 1) +
                   bump * 0.5 * (2 * noise3(d * 4.3 + V3(-seed, seed, seed * 1.3)) - 1);
        p[i] = c + V3(d.x * radii.x, d.y * radii.y, d.z * radii.z) * k;
    }
    for (const auto& f : m.f) {
        const V3 &a = p[f[0]], &b = p[f[1]], &cc = p[f[2]];
        if (a.y < cutBelow && b.y < cutBelow && cc.y < cutBelow) continue;  // buried
        V3 fn = norm(cross(b - a, cc - a));
        double shade = 0.92 + 0.16 * latticeHash((int)(seed * 97) + f[0], f[1]);
        for (int k = 0; k < 3; k++) out.push_back(vert(p[f[k]], norm(fn * facet + m.v[f[k]] * (1 - facet)), col * shade, mat));
    }
}

// Tapered tube from a to b.
void tube(std::vector<Vtx>& out, const V3& a, const V3& b, double ra, double rb, int segs, const V3& col, int mat,
          bool capTop = false, const V3& capCol = V3()) {
    V3 ax = norm(b - a);
    V3 u = norm(cross(ax, std::fabs(ax.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), w = cross(ax, u);
    for (int s = 0; s < segs; s++) {
        double a0 = 2 * M_PI * s / segs, a1 = 2 * M_PI * (s + 1) / segs;
        V3 d0 = u * std::cos(a0) + w * std::sin(a0), d1 = u * std::cos(a1) + w * std::sin(a1);
        V3 p0 = a + d0 * ra, p1 = a + d1 * ra, q0 = b + d0 * rb, q1 = b + d1 * rb;
        out.push_back(vert(p0, d0, col, mat)), out.push_back(vert(q1, d1, col, mat)), out.push_back(vert(p1, d1, col, mat));
        out.push_back(vert(p0, d0, col, mat)), out.push_back(vert(q0, d0, col, mat)), out.push_back(vert(q1, d1, col, mat));
        if (capTop) {
            out.push_back(vert(b, ax, capCol, SM_MATTE)), out.push_back(vert(q0, ax, capCol, SM_MATTE)), out.push_back(vert(q1, ax, capCol, SM_MATTE));
            out.push_back(vert(a, -ax, capCol, SM_MATTE)), out.push_back(vert(p1, -ax, capCol, SM_MATTE)), out.push_back(vert(p0, -ax, capCol, SM_MATTE));
        }
    }
}

// A rock slab: the collider box with rounded edges (inside the box, so nothing pokes
// out that a tire would sink into) and a little surface relief.
void slab(std::vector<Vtx>& out, const Box& b) {
    const double he[3] = {b.h.x, b.h.y, b.h.z};
    const double rr = std::min(0.14, std::min({he[0], he[1], he[2]}) * 0.4);
    const V3 col(b.col[0], b.col[1], b.col[2]);
    for (int axis = 0; axis < 3; axis++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const int u = (axis + 1) % 3, w = (axis + 2) % 3;
            const int G = std::max(4, (int)(std::max(he[u], he[w]) * 3));
            auto at = [&](int iu, int iw, V3& pos, V3& nrm) {
                double s[3];
                s[axis] = sgn;
                s[u] = -1 + 2.0 * iu / G;
                s[w] = -1 + 2.0 * iw / G;
                double q[3], inner[3], dq[3];
                for (int k = 0; k < 3; k++) {
                    q[k] = s[k] * he[k];
                    inner[k] = clampd(q[k], -(he[k] - rr), he[k] - rr);
                    dq[k] = q[k] - inner[k];
                }
                V3 d(dq[0], dq[1], dq[2]);
                double dl = len(d);
                V3 n = dl > 1e-9 ? d / dl : V3(axis == 0 ? sgn : 0, axis == 1 ? sgn : 0, axis == 2 ? sgn : 0);
                V3 local = V3(inner[0], inner[1], inner[2]) + n * rr;
                V3 world = b.c + b.ax[0] * local.x + b.ax[1] * local.y + b.ax[2] * local.z;
                V3 wn = norm(b.ax[0] * n.x + b.ax[1] * n.y + b.ax[2] * n.z);
                world -= wn * (0.025 * noise3(world * 1.3));  // relief, always inward
                pos = world;
                nrm = wn;
            };
            for (int iu = 0; iu < G; iu++)
                for (int iw = 0; iw < G; iw++) {
                    V3 p[4], n[4];
                    at(iu, iw, p[0], n[0]), at(iu + 1, iw, p[1], n[1]), at(iu + 1, iw + 1, p[2], n[2]), at(iu, iw + 1, p[3], n[3]);
                    const int order[6] = {0, 1, 2, 0, 2, 3};
                    V3 fn = norm(cross(p[1] - p[0], p[2] - p[0]));
                    const bool flip = dot(fn, n[0]) < 0;
                    for (int k = 0; k < 6; k++) {
                        int o = flip ? order[5 - k] : order[k];
                        out.push_back(vert(p[o], n[o], col, SM_ROCK));
                    }
                }
        }
}

}  // namespace

void buildTreeMesh(const World& world, std::vector<Vtx>& v, const ScanCover& skip) {
    const int sides = 10;
    auto put = [&](const V3& p, const V3& n, const V3& c, int mat) { v.push_back(vert(p, n, c, mat)); };
    int seed = 0;
    for (const Tree& t : world.trees) {
        double h = t.height, R = t.radius;
        seed++;
        if ((t.kind == 0 && skip.pines) || (t.kind == 2 && skip.snags)) continue;
        double turn = latticeHash(seed, 7) * 6.2832;
        if (t.kind == 1) {  // broadleaf: a forked trunk under a lumpy crown
            const V3 bark(0.28, 0.23, 0.18);
            V3 top = t.base + V3(0, h * 0.55, 0);
            tube(v, t.base, top, 0.26, 0.14, 7, bark, SM_MATTE);
            for (int k = 0; k < 3; k++) {
                double a = turn + k * 2.1;
                tube(v, top - V3(0, h * 0.08, 0), top + V3(std::cos(a) * R * 0.45, h * 0.18, std::sin(a) * R * 0.45), 0.11, 0.05, 5,
                     bark, SM_MATTE);
            }
            const int clumps = 6 + seed % 3;
            for (int k = 0; k < clumps; k++) {
                double a = turn + k * 2.39996, d = R * (0.25 + 0.45 * latticeHash(seed, k + 40));
                V3 c = t.base + V3(std::cos(a) * d, h * (0.62 + 0.22 * latticeHash(seed, k + 80)), std::sin(a) * d);
                double cr = R * (0.45 + 0.2 * latticeHash(seed, k + 120));
                V3 green = V3(0.16, 0.29, 0.07) * (t.tint * (0.85 + 0.3 * latticeHash(seed, k)));
                blob(v, c, V3(cr, cr * 0.8, cr), 0.18, seed * 1.3 + k, 1, 0.25, green, SM_FOLIAGE);
            }
            continue;
        }
        if (t.kind == 2) {  // dead snag: grey trunk and a few bare branches
            const V3 grey(0.42, 0.39, 0.35);
            tube(v, t.base, t.base + V3(0, h, 0), 0.22, 0.04, 7, grey, SM_MATTE);
            for (int k = 0; k < 6; k++) {
                double a = turn + k * 2.6, y = h * (0.35 + 0.1 * k), L = h * (0.28 - 0.03 * k);
                V3 from = t.base + V3(0, y, 0);
                tube(v, from, from + V3(std::cos(a) * L, L * 0.7, std::sin(a) * L), 0.06, 0.015, 4, grey, SM_MATTE);
            }
            continue;
        }
        // Pine: tapering trunk, four drooping tiers of boughs (ragged cones), darker and
        // bluer low down. One standing in snow carries it on its boughs.
        const bool snowy = world.surfaceAt(t.base.x, t.base.z) == SURF_SNOW;
        tube(v, t.base, t.base + V3(0, h * 0.5, 0), 0.22, 0.1, 6, V3(0.30, 0.22, 0.15), SM_MATTE);
        const double tiers[4][3] = {{0.18, 0.55, 1.0}, {0.36, 0.72, 0.8}, {0.54, 0.88, 0.6}, {0.70, 1.02, 0.38}};
        for (int c = 0; c < 4; c++) {
            V3 apex = t.base + V3(0, h * tiers[c][1], 0);
            double y0 = h * tiers[c][0], r = R * tiers[c][2] * 1.05;
            V3 green = V3(0.10 + 0.03 * c, 0.20 + 0.045 * c, 0.09 + 0.012 * c) * t.tint;
            V3 under = V3(0.05, 0.10, 0.05) * t.tint;
            for (int s = 0; s < sides; s++) {
                double a0 = turn + 2 * M_PI * s / sides, a1 = turn + 2 * M_PI * (s + 1) / sides;
                double r0 = r * (0.8 + 0.35 * latticeHash(seed * 31 + c, s)), r1 = r * (0.8 + 0.35 * latticeHash(seed * 31 + c, (s + 1) % sides));
                V3 d0(std::cos(a0), 0, std::sin(a0)), d1(std::cos(a1), 0, std::sin(a1));
                V3 p0 = t.base + V3(0, y0 - 0.12 * r0, 0) + d0 * r0, p1 = t.base + V3(0, y0 - 0.12 * r1, 0) + d1 * r1;
                V3 n0 = norm(d0 + V3(0, 0.75, 0)), n1 = norm(d1 + V3(0, 0.75, 0)), na = norm((d0 + d1) * 0.3 + V3(0, 1, 0));
                put(p0, n0, green, SM_FOLIAGE), put(apex, na, green * 1.25, SM_FOLIAGE), put(p1, n1, green, SM_FOLIAGE);
                V3 hub = t.base + V3(0, y0 + 0.25 * r, 0);
                put(p1, V3(0, -1, 0), under, SM_FOLIAGE), put(hub, V3(0, -1, 0), under, SM_FOLIAGE), put(p0, V3(0, -1, 0), under, SM_FOLIAGE);
                if (snowy) {
                    // Snow lying on the bough: a white skin over the top of the cone, a few
                    // centimetres proud of it, its lower edge ragged where it's sliding off.
                    const double f0 = 0.18 + 0.22 * latticeHash(seed * 17 + c, s), f1 = 0.18 + 0.22 * latticeHash(seed * 17 + c, (s + 1) % sides);
                    const V3 lift(0, 0.06 + 0.02 * r, 0), snow(0.80, 0.83, 0.88);
                    const V3 q0 = p0 + (apex - p0) * f0 + lift, q1 = p1 + (apex - p1) * f1 + lift;
                    put(q0, norm(n0 + V3(0, 0.6, 0)), snow * 0.92, SM_MATTE), put(apex + lift, V3(0, 1, 0), snow, SM_MATTE),
                        put(q1, norm(n1 + V3(0, 0.6, 0)), snow * 0.92, SM_MATTE);
                }
            }
        }
    }
}

void buildRockMesh(const World& world, std::vector<Vtx>& v, const ScanCover& skip) {
    for (const Rock& k : world.rocks) {
        if (k.scan >= 0 && skip.rocks) continue;
        // The collider's own facets, each split into 16 and given a little relief, always
        // pushed inward (toward the middle), so nothing drawn sticks out past what a tire hits.
        const V3 col = V3(0.34, 0.32, 0.30) * k.tint;
        const bool snowy = world.surfaceAt(k.c.x, k.c.z) == SURF_SNOW;  // snow lies on its top
        const double relief = std::min(0.05, 0.035 * k.r);
        auto sink = [&](const V3& p) {
            return p + norm(k.c - p) * (relief * noise3(p * (2.2 / std::max(k.r, 0.4)) + V3(k.seed % 97, 0, 0)));
        };
        const int S = k.r > 0.7 ? 4 : 2;
        for (size_t fi = 0; fi < k.f.size(); fi++) {
            const V3 &A = k.v[k.f[fi][0]], &B = k.v[k.f[fi][1]], &C = k.v[k.f[fi][2]];
            if (std::max({A.y, B.y, C.y}) < world.height((A.x + B.x + C.x) / 3, (A.z + B.z + C.z) / 3) - 0.2) continue;  // buried
            const V3 fn = k.fn[fi];
            const double shade = 0.9 + 0.2 * latticeHash((int)k.seed, (int)fi);
            auto at = [&](int i, int j) {  // barycentric grid point (i along AB, j along AC)
                return sink(A + (B - A) * ((double)i / S) + (C - A) * ((double)j / S));
            };
            auto tri = [&](const V3& p, const V3& q, const V3& r) {
                V3 n = cross(q - p, r - p);
                n = len(n) > 1e-12 ? norm(n) : fn;
                if (dot(n, fn) < 0) n = -n;
                V3 sn = norm(n * 0.5 + fn * 0.5);
                V3 c = col * shade;
                if (snowy) c = c + (V3(0.82, 0.84, 0.88) - c) * smoothstep(0.45, 0.7, sn.y + 0.15 * (latticeHash((int)k.seed, (int)fi + 7) - 0.5));
                v.push_back(vert(p, sn, c, SM_ROCK)), v.push_back(vert(q, sn, c, SM_ROCK)), v.push_back(vert(r, sn, c, SM_ROCK));
            };
            for (int i = 0; i < S; i++)
                for (int j = 0; i + j < S; j++) {
                    tri(at(i, j), at(i + 1, j), at(i, j + 1));
                    if (i + j + 1 < S) tri(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                }
        }
    }
    for (const Box& b : world.boxes)
        if (b.rock) slab(v, b);
    for (const Log& g : world.logs) {
        if (skip.logs) break;
        tube(v, g.a, g.b, g.r, g.r * 0.92, 12, V3(0.33, 0.25, 0.17), SM_MATTE, true, V3(0.62, 0.50, 0.34));
        // A couple of snapped-off branch stubs.
        V3 mid = (g.a + g.b) * 0.5, ax = norm(g.b - g.a);
        V3 side = norm(cross(ax, V3(0, 1, 0)));
        tube(v, mid + ax * 1.2, mid + ax * 1.5 + side * 0.4 + V3(0, 0.35, 0), g.r * 0.35, g.r * 0.15, 5, V3(0.33, 0.25, 0.17), SM_MATTE);
    }
}

void buildBushMesh(const World& world, std::vector<Vtx>& v, const ScanCover& skip) {
    // Reeds round the lakes: clumps of tall thin blades where the bank meets the water.
    int rs = 0;
    for (const Water& wt : world.waters)
        for (double ang = 0; ang < 2 * M_PI; ang += 0.045) {
            rs++;
            if (latticeHash(rs, 3) < 0.35) continue;
            // Walk out from the middle to the waterline.
            double d = 0;
            for (; d < wt.r * 1.2; d += 0.5)
                if (world.height(wt.x + d * std::cos(ang), wt.z + d * std::sin(ang)) > wt.y - 0.25) break;
            for (int b = 0; b < 14; b++) {
                double off = d + (latticeHash(rs, b) - 0.3) * 2.0, side = (latticeHash(rs, b + 40) - 0.5) * 1.6;
                double x = wt.x + off * std::cos(ang) - side * std::sin(ang), z = wt.z + off * std::sin(ang) + side * std::cos(ang);
                double gy = world.height(x, z), h = 0.9 + 0.9 * latticeHash(rs, b + 80);
                if (gy > wt.y + 0.5) continue;
                double lean = (latticeHash(rs, b + 120) - 0.5) * 0.5, a2 = latticeHash(rs, b + 160) * 6.28;
                V3 base(x, std::min(gy, wt.y) - 0.3, z), dir(std::cos(a2), 0, std::sin(a2)), across(-dir.z, 0, dir.x);
                V3 tip = base + V3(lean * dir.x, h + 0.3, lean * dir.z);
                V3 col = (b % 5 == 0) ? V3(0.38, 0.30, 0.15) : V3(0.20, 0.30, 0.10) * (0.85 + 0.3 * latticeHash(rs, b + 7));
                V3 n = norm(dir + V3(0, 0.3, 0));
                v.push_back(vert(base - across * 0.025, n, col, SM_FOLIAGE)), v.push_back(vert(base + across * 0.025, n, col, SM_FOLIAGE));
                v.push_back(vert(tip, n, col * 1.2, SM_FOLIAGE));
                if (b % 5 == 0) tube(v, tip - V3(0, 0.25, 0), tip - V3(0, 0.05, 0), 0.03, 0.03, 4, V3(0.25, 0.15, 0.08), SM_MATTE);  // cattail
            }
        }
    // Reeds in still, shallow water over mud on maps whose water is on the height grid (the
    // Frontier's swamp, delta and the Muddy's banks), clear of roads and trails.
    if (!world.waterY.empty()) {
        const int n = world.gridSize();
        for (int i = 0; i < n; i += 2)
            for (int j = 0; j < n; j += 2) {
                const double x = -World::kHalfSize + i + latticeHash(i, j + 3) - 0.5, z = -World::kHalfSize + j + latticeHash(i + 5, j) - 0.5;
                const double wl = world.waterLevel(x, z);
                if (wl < -1e8) continue;
                const double gy = world.height(x, z), depth = wl - gy;
                if (depth < -0.05 || depth > 0.3 || latticeHash(i * 7 + 1, j * 13 + 2) > 0.16) continue;
                if (world.surfaceAt(x, z) != SURF_MUD || len(world.waterFlow(x, z)) > 0.3) continue;
                const RoadInfo r = world.roadAt(x, z);
                if (r.along != 0 || r.lateral != 0) continue;
                const int clump = i * 4099 + j;
                for (int b = 0; b < 10; b++) {
                    const double a = latticeHash(clump, b) * 6.2832, d = 0.7 * latticeHash(clump, b + 20);
                    const double bx = x + d * std::cos(a), bz = z + d * std::sin(a), hgt = 0.8 + 0.9 * latticeHash(clump, b + 80);
                    const double lean = (latticeHash(clump, b + 120) - 0.5) * 0.5, a2 = latticeHash(clump, b + 160) * 6.28;
                    V3 base(bx, std::min(world.height(bx, bz), wl) - 0.3, bz), dir(std::cos(a2), 0, std::sin(a2)), across(-dir.z, 0, dir.x);
                    V3 tip = base + V3(lean * dir.x, hgt + 0.3, lean * dir.z);
                    V3 col = (b % 5 == 0) ? V3(0.38, 0.30, 0.15) : V3(0.20, 0.30, 0.10) * (0.85 + 0.3 * latticeHash(clump, b + 7));
                    V3 nn = norm(dir + V3(0, 0.3, 0));
                    v.push_back(vert(base - across * 0.025, nn, col, SM_FOLIAGE)), v.push_back(vert(base + across * 0.025, nn, col, SM_FOLIAGE));
                    v.push_back(vert(tip, nn, col * 1.2, SM_FOLIAGE));
                    if (b % 5 == 0) tube(v, tip - V3(0, 0.25, 0), tip - V3(0, 0.05, 0), 0.03, 0.03, 4, V3(0.25, 0.15, 0.08), SM_MATTE);  // cattail
                }
            }
    }
    int seed = 0;
    for (const Bush& b : world.bushes) {
        if (skip.bushes) break;
        seed++;
        const int n = 3 + seed % 3;
        for (int k = 0; k < n; k++) {
            double a = k * 2.39996 + seed, d = b.r * 0.45 * latticeHash(seed, k);
            double r = b.r * (0.45 + 0.25 * latticeHash(seed, k + 9));
            V3 c = b.p + V3(std::cos(a) * d, r * 0.6 + b.r * 0.15 * latticeHash(seed, k + 3), std::sin(a) * d);
            V3 green = V3(0.14, 0.25, 0.07) * (b.tint * (0.85 + 0.3 * latticeHash(seed, k + 5)));
            if (seed % 7 == 0) green = V3(0.30, 0.30, 0.10) * b.tint;  // a dry one
            blob(v, c, V3(r, r * 0.8, r), 0.2, seed * 0.71 + k, 1, 0.2, green, SM_FOLIAGE);
        }
    }
}

namespace {
// A box: center c, half-sizes h along (right, up, front) of a frame turned by yaw.
void obox(std::vector<Vtx>& out, const V3& c, const V3& h, double yaw, const V3& col, int mat, double pitch = 0) {
    V3 ax[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
    for (V3& a : ax) a = rotateAxis(rotateAxis(a, V3(0, 0, 1), pitch), V3(0, 1, 0), yaw);
    const double he[3] = {h.x, h.y, h.z};
    for (int axis = 0; axis < 3; axis++)
        for (int s = -1; s <= 1; s += 2) {
            int u = (axis + 1) % 3, w = (axis + 2) % 3;
            V3 n = ax[axis] * s, fc = c + n * he[axis], du = ax[u] * he[u], dw = ax[w] * he[w];
            V3 q[4] = {fc - du - dw, fc + du - dw, fc + du + dw, fc - du + dw};
            for (int k : {0, 1, 2, 0, 2, 3}) out.push_back(vert(q[k], n, col, mat));
        }
}
V3 fromYaw(double yaw) { return V3(std::cos(yaw), 0, -std::sin(yaw)); }  // the way it faces
}  // namespace

void buildPropMesh(const World& world, std::vector<Vtx>& v) {
    const V3 wood(0.45, 0.33, 0.21), darkWood(0.28, 0.20, 0.13), grey(0.42, 0.40, 0.37);
    int seed = 0;
    for (const Prop& p : world.fixtures) {
        seed++;
        const V3 f = fromYaw(p.yaw), side(-f.z, 0, f.x), up(0, 1, 0);
        switch (p.kind) {
            case PROP_FENCE: {  // split-rail: posts every 2.5 m, two rails following the ground
                V3 d = p.b - p.a;
                const double L = std::hypot(d.x, d.z);
                const int n = std::max(1, (int)std::round(L / 2.5));
                V3 prev;
                for (int k = 0; k <= n; k++) {
                    V3 q = p.a + d * ((double)k / n);
                    q.y = world.height(q.x, q.z);
                    tube(v, q - V3(0, 0.3, 0), q + V3(0, 1.15, 0), 0.075, 0.07, 6, grey * 1.1, SM_WOOD, true, grey);
                    if (k) {
                        for (double hgt : {0.45, 0.9}) {
                            double sag = 0.01 * latticeHash(seed * 7 + k, (int)(hgt * 10));
                            tube(v, prev + V3(0, hgt, 0), q + V3(0, hgt - sag, 0), 0.05, 0.05, 5, grey, SM_WOOD);
                        }
                    }
                    prev = q;
                }
                break;
            }
            case PROP_CABIN: {  // log cabin: stacked logs, plank gables, a green tin roof
                const double W = 2.5, D = 2.0, H = 2.5;  // half width (along side), half depth, wall height
                const V3 base = p.a - V3(0, 0.2, 0);
                for (int row = 0; row < 10; row++) {
                    double y = 0.13 + row * 0.25;
                    for (int s = -1; s <= 1; s += 2) {
                        V3 a = base + side * (-W - 0.2) + f * (s * D) + up * y, b = base + side * (W + 0.2) + f * (s * D) + up * y;
                        tube(v, a, b, 0.14, 0.14, 8, wood * (0.92 + 0.12 * latticeHash(seed, row * 2 + s)), SM_WOOD, true, wood * 1.4);
                        a = base + side * (s * W) + f * (-D - 0.2) + up * (y + 0.12);
                        b = base + side * (s * W) + f * (D + 0.2) + up * (y + 0.12);
                        tube(v, a, b, 0.14, 0.14, 8, wood * (0.92 + 0.12 * latticeHash(seed, row * 2 + s + 40)), SM_WOOD, true, wood * 1.4);
                    }
                }
                // Gables and roof (ridge along the side axis).
                for (int s = -1; s <= 1; s += 2) {
                    V3 e0 = base + side * (s * W) + f * -D + up * H, e1 = base + side * (s * W) + f * D + up * H,
                       top = base + side * (s * W) + up * (H + 1.3);
                    V3 n = side * s;
                    for (const V3& q : {e0, e1, top}) v.push_back(vert(q, n, darkWood, SM_WOOD));
                }
                const V3 tin(0.18, 0.32, 0.22);
                for (int s = -1; s <= 1; s += 2) {
                    V3 eave = base + f * (s * (D + 0.5)) + up * (H - 0.3), ridge = base + up * (H + 1.35);
                    V3 mid = (eave + ridge) * 0.5;
                    double slopeLen = len(ridge - eave) / 2;
                    V3 dir = norm(ridge - eave);
                    // A thin slab from eave to ridge: built as a box turned to the slope.
                    double pitch = std::atan2(dir.y, std::fabs(dot(dir, f)));
                    obox(v, mid, V3(slopeLen, 0.05, W + 0.55), std::atan2(-f.z * -s, f.x * -s), tin, SM_METAL, pitch);
                }
                // Door and two windows on the front (facing f).
                obox(v, base + f * (D + 0.16) + up * 1.05, V3(0.06, 1.0, 0.5), p.yaw, darkWood * 0.7, SM_WOOD);
                for (int s = -1; s <= 1; s += 2)
                    obox(v, base + f * (D + 0.16) + side * (s * 1.5) + up * 1.5, V3(0.04, 0.4, 0.45), p.yaw, V3(0.05, 0.07, 0.09), SM_GLASS);
                tube(v, base + side * 1.4 + up * (H + 0.5), base + side * 1.4 + up * (H + 2.0), 0.22, 0.2, 6, grey * 0.8, SM_ROCK);  // stone chimney
                break;
            }
            case PROP_SIGN:
            case PROP_MAPBOARD: {
                const bool board = p.kind == PROP_MAPBOARD;
                const V3 col(p.col[0], p.col[1], p.col[2]);
                if (board) {
                    for (int s = -1; s <= 1; s += 2)
                        obox(v, p.a + side * (s * 0.8) + up * 1.0, V3(0.07, 1.3, 0.07), p.yaw, darkWood, SM_WOOD);
                    obox(v, p.a + up * 1.45, V3(0.05, 0.55, 0.85), p.yaw, darkWood, SM_WOOD);      // frame
                    obox(v, p.a + up * 1.45 + f * 0.03, V3(0.04, 0.48, 0.77), p.yaw, V3(0.80, 0.76, 0.62), SM_MATTE);  // map
                    // The map: a green valley, the blue lake, the brown trail spiralling up.
                    obox(v, p.a + up * 1.3 + f * 0.075, V3(0.005, 0.2, 0.6), p.yaw, V3(0.35, 0.5, 0.25), SM_MATTE);
                    obox(v, p.a + up * 1.25 + side * 0.4 + f * 0.08, V3(0.005, 0.08, 0.12), p.yaw, V3(0.2, 0.4, 0.7), SM_MATTE);
                    for (int k = 0; k < 10; k++) {
                        double a = k * 0.6, r = 0.32 - k * 0.025;
                        obox(v, p.a + up * (1.5 + std::sin(a) * r * 0.6) + side * (std::cos(a) * r) + f * 0.085, V3(0.004, 0.025, 0.06),
                             p.yaw, V3(0.45, 0.28, 0.12), SM_MATTE);
                    }
                    obox(v, p.a + up * 2.15, V3(0.4, 0.04, 1.05), p.yaw, V3(0.18, 0.32, 0.22), SM_METAL, 0.0);  // little roof
                } else {
                    obox(v, p.a + up * 0.8, V3(0.06, 1.1, 0.06), p.yaw, darkWood, SM_WOOD);
                    obox(v, p.a + up * 1.65, V3(0.03, 0.3, 0.4), p.yaw, V3(0.85, 0.83, 0.78), SM_MATTE);  // white plate
                    // The rating symbol: green circle, blue square, black diamond, red double diamond.
                    V3 c = p.a + up * 1.65 + f * 0.04;
                    if (col.y > 0.4) tube(v, c - f * 0.01, c + f * 0.01, 0.17, 0.17, 16, col, SM_MATTE, true, col);
                    else if (col.z > 0.5) obox(v, c, V3(0.012, 0.16, 0.16), p.yaw, col, SM_MATTE);
                    else {
                        int n = col.x > 0.5 ? 2 : 1;
                        for (int k = 0; k < n; k++) {
                            V3 cc = c + side * (n == 2 ? (k ? 0.17 : -0.17) : 0.0);
                            // A square stood on its corner.
                            V3 q[4] = {cc + up * 0.18, cc + side * 0.15, cc - up * 0.18, cc - side * 0.15};
                            for (int t : {0, 1, 2, 0, 2, 3}) v.push_back(vert(q[t], f, col, SM_MATTE));
                        }
                    }
                }
                break;
            }
            case PROP_TABLE: {  // picnic table: top, two benches, A-frame legs
                obox(v, p.a + up * 0.76, V3(0.9, 0.035, 0.38), p.yaw, wood, SM_WOOD);
                for (int s = -1; s <= 1; s += 2) {
                    obox(v, p.a + up * 0.45 + side * (s * 0.68), V3(0.9, 0.03, 0.14), p.yaw, wood, SM_WOOD);
                    for (int e = -1; e <= 1; e += 2) {
                        V3 foot = p.a + f * (e * 0.7) + side * (s * 0.75), top = p.a + f * (e * 0.7) + up * 0.75 + side * (s * 0.15);
                        tube(v, foot, top, 0.04, 0.04, 4, darkWood, SM_WOOD);
                    }
                }
                break;
            }
            case PROP_GARAGE: {
                // Repair station: a drive-through steel shed (10 m x 7.4 m) on a concrete pad,
                // a tool bench and red tool chest, a compressor, a tire stack, the sign, lights.
                const V3 steel(0.30, 0.34, 0.38), tin(0.55, 0.57, 0.58), red(0.62, 0.08, 0.06), yellow(0.95, 0.72, 0.12);
                const double L = 5.0, Wd = 3.7, H = 4.4;
                const V3 base = p.a;
                obox(v, base - up * 0.035, V3(L + 1.2, 0.05, Wd + 1.4), p.yaw, V3(0.56, 0.55, 0.52), SM_CONCRETE);  // pad
                for (int s = -1; s <= 1; s += 2)  // yellow guide lines at the edges of the bay
                    obox(v, base + up * 0.017 + side * (s * (Wd - 0.6)), V3(L + 1.0, 0.004, 0.06), p.yaw, yellow, SM_MATTE);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) {
                        const V3 q = base + f * (a * L) + side * (b * Wd);
                        obox(v, q + up * (H / 2), V3(0.1, H / 2, 0.1), p.yaw, steel, SM_METAL);  // posts
                        obox(v, q + up * 0.5, V3(0.12, 0.5, 0.12), p.yaw, yellow, SM_MATTE);   // bump guards
                    }
                for (int b = -1; b <= 1; b += 2) obox(v, base + side * (b * Wd) + up * (H + 0.1), V3(L + 0.1, 0.12, 0.09), p.yaw, steel, SM_METAL);
                for (int a = -2; a <= 2; a++) obox(v, base + f * (a * L / 2) + up * (H + 0.25), V3(0.07, 0.1, Wd + 0.1), p.yaw, steel, SM_METAL);
                // Roof: two corrugated panels pitched to a ridge along the bay.
                for (int b = -1; b <= 1; b += 2) {
                    const double pitch = 0.12;
                    const V3 c = base + side * (b * (Wd / 2 + 0.15)) + up * (H + 0.4 + 0.22);
                    obox(v, c, V3(L + 0.6, 0.03, Wd / 2 + 0.55), p.yaw, tin, SM_METAL, 0.0);
                    (void)pitch;
                    for (int k = -6; k <= 6; k++)  // corrugation ribs
                        obox(v, c + f * (k * (L + 0.6) / 6.5) + up * 0.035, V3(0.02, 0.012, Wd / 2 + 0.55), p.yaw, tin * 0.9, SM_METAL);
                }
                // Bench and chest along the back wall side (outside the bay, by the +side posts).
                const V3 wall = base + side * (Wd + 0.75);
                obox(v, wall + up * 0.9, V3(2.0, 0.04, 0.4), p.yaw, V3(0.45, 0.33, 0.21), SM_WOOD);
                for (int a = -1; a <= 1; a += 2) obox(v, wall + f * (a * 1.9) + up * 0.45, V3(0.04, 0.45, 0.35), p.yaw, steel, SM_METAL);
                obox(v, wall + f * 2.9 + up * 0.55, V3(0.4, 0.55, 0.3), p.yaw, red, SM_PAINT);          // tool chest
                for (int k = 0; k < 5; k++) obox(v, wall + f * (2.9 + 0.405) + up * (0.2 + k * 0.2), V3(0.005, 0.01, 0.2), p.yaw, V3(0.75, 0.75, 0.78), SM_CHROME);
                tube(v, wall + f * (-2.9) + up * 0.45 - side * 0.0, wall + f * (-2.9) + up * 1.3, 0.28, 0.28, 12, red, SM_PAINT, true, red);  // compressor tank
                obox(v, wall + f * (-2.9) + up * 1.4, V3(0.25, 0.12, 0.2), p.yaw, V3(0.1, 0.1, 0.11), SM_METAL);
                for (int k = 0; k < 4; k++)  // stack of spare tires
                    tube(v, wall + f * (-1.6) + side * 0.1 + up * (0.02 + k * 0.27), wall + f * (-1.6) + side * 0.1 + up * (0.27 + k * 0.27), 0.42, 0.42, 14,
                         V3(0.04, 0.04, 0.04), SM_TIRE, true, V3(0.05, 0.05, 0.05));
                // Sign over the entrance: a yellow board with a wrench.
                const V3 sign = base + f * (L + 0.12) + up * (H + 0.75);
                obox(v, sign, V3(0.05, 0.45, 2.2), p.yaw, yellow, SM_MATTE);
                obox(v, sign + f * 0.06, V3(0.01, 0.07, 1.1), p.yaw, V3(0.08, 0.08, 0.08), SM_MATTE);  // wrench handle
                for (int s = -1; s <= 1; s += 2) obox(v, sign + f * 0.06 + side * (s * 1.15), V3(0.01, 0.2, 0.12), p.yaw, V3(0.08, 0.08, 0.08), SM_MATTE);
                const V3 sign2 = base - f * (L + 0.12) + up * (H + 0.75);
                obox(v, sign2, V3(0.05, 0.45, 2.2), p.yaw, yellow, SM_MATTE);
                // Floodlights under the roof.
                for (int a = -1; a <= 1; a += 2) obox(v, base + f * (a * 2.5) + up * (H + 0.08), V3(0.3, 0.03, 0.12), p.yaw, V3(1.0, 0.96, 0.85), SM_EMISSIVE);
                break;
            }
            case PROP_MARKER: {  // trail-edge reflector post
                obox(v, p.a + up * 0.45, V3(0.04, 0.6, 0.04), p.yaw, V3(0.85, 0.85, 0.82), SM_MATTE);
                obox(v, p.a + up * 0.95, V3(0.045, 0.08, 0.045), p.yaw, V3(1.0, 0.35, 0.05), SM_EMISSIVE);
                break;
            }
            case PROP_CAIRN: {  // stacked stones marking the line
                double r = 0.32, y = 0;
                for (int k = 0; k < 4; k++) {
                    const V3 c = p.a + up * (y + r * 0.6) + side * (0.04 * latticeHash(seed, k) - 0.02);
                    tube(v, c - up * r * 0.55, c + up * r * 0.55, r, r * 0.8, 7, V3(0.5, 0.48, 0.45) * (0.85 + 0.2 * latticeHash(seed, k + 9)), SM_ROCK, true,
                         V3(0.55, 0.53, 0.5));
                    y += r * 1.05;
                    r *= 0.75;
                }
                break;
            }
            case PROP_STUMP: {
                const double r = p.b.x, h = p.b.y;
                tube(v, p.a - V3(0, 0.2, 0), p.a + V3(0, h, 0), r * 1.1, r, 9, V3(0.30, 0.22, 0.15), SM_WOOD, true, V3(0.62, 0.5, 0.34));
                for (int k = 0; k < 3; k++) {  // roots
                    double a = p.yaw + k * 2.1;
                    V3 dir(std::cos(a), 0, std::sin(a));
                    tube(v, p.a + V3(0, h * 0.3, 0) + dir * r * 0.6, p.a + dir * (r * 2.2) - V3(0, 0.1, 0), r * 0.35, r * 0.12, 5,
                         V3(0.28, 0.21, 0.14), SM_WOOD);
                }
                break;
            }
        }
    }
}
