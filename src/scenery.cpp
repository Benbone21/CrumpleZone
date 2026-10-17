#include "scenery.h"

#include "forest.h"

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

// A rock slab: the collider box worn into rock. The surface is a rounded box (radius ~0.2 m, bigger on a
// big slab) lying inside the collider, so nothing drawn sticks out past what a tire hits; its corners
// and edges are chipped, its faces pitted and lumpy (more on the sides than on the tread, so the top
// still reads where a wheel stands), its shading banded with the strata and darker in the pits.
// Seeded by the slab's place alone, so a slab always wears the same; built once with the world's rocks.
namespace slabwear {
struct Wear {
    V3 o1, o2, o3;  // noise offsets (this slab's seed)
    double rr;      // rounding radius
};
// Lumpiness at a point of the slab, 0..1 (before the amplitude and the edge chips).
double lumps(const Wear& w, const V3& p) {
    return 0.62 * noise3(p * 0.85 + w.o1) + 0.38 * noise3(p * 2.6 + w.o2);
}
double chipNoise(const Wear& w, const V3& p) { return noise3(p * 1.7 + w.o3); }
}  // namespace slabwear

void slab(std::vector<Vtx>& out, const Box& b) {
    using namespace slabwear;
    const double he[3] = {b.h.x, b.h.y, b.h.z};
    const uint32_t sd = (uint32_t)(std::lround(b.c.x * 8) * 73856093 ^ std::lround(b.c.y * 8) * 19349663 ^ std::lround(b.c.z * 8) * 83492791);
    auto h01 = [&](int k) { return latticeHash((int)(sd % 100003) + k * 17, k * 31 + 5); };
    Wear w;
    w.o1 = V3(h01(1) * 90, h01(2) * 90, h01(3) * 90), w.o2 = V3(h01(4) * 90, h01(5) * 90, h01(6) * 90), w.o3 = V3(h01(7) * 90, h01(8) * 90, h01(9) * 90);
    const double minHe = std::min({he[0], he[1], he[2]});
    w.rr = std::min(0.13 + 0.04 * minHe, minHe * 0.4);
    const V3 col(b.col[0], b.col[1], b.col[2]);
    const double bandPh = h01(10) * 6.28;
    for (int axis = 0; axis < 3; axis++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const bool under = axis == 1 && sgn < 0;  // (the underside is mostly buried: coarse)
            // Grid lines across each of the face's two axes: the flat middle every `step`, the rounded edge in 3 bands.
            auto lines = [&](int k) {
                std::vector<double> q;
                const double flat = he[k] - w.rr, step = under ? 1.2 : 0.38;
                const int n = std::max(1, (int)std::ceil(2 * flat / step));
                for (int i = 0; i < 3; i++) q.push_back(-he[k] + w.rr * i / 3.0);
                for (int i = 0; i <= n; i++) q.push_back(-flat + 2 * flat * i / n);
                for (int i = 1; i <= 3; i++) q.push_back(flat + w.rr * i / 3.0);
                return q;
            };
            const std::vector<double> qu = lines(u), qv = lines(v);
            const int Nu = (int)qu.size(), Nv = (int)qv.size();
            std::vector<V3> pos(Nu * Nv), nrm(Nu * Nv);
            std::vector<float> shade(Nu * Nv);
            for (int iu = 0; iu < Nu; iu++)
                for (int iv = 0; iv < Nv; iv++) {
                    double q[3], inner[3], dq[3];
                    q[axis] = sgn * he[axis], q[u] = qu[iu], q[v] = qv[iv];
                    for (int k = 0; k < 3; k++) {
                        inner[k] = clampd(q[k], -(he[k] - w.rr), he[k] - w.rr);
                        dq[k] = q[k] - inner[k];
                    }
                    const V3 d(dq[0], dq[1], dq[2]);
                    const double dl = len(d);
                    const V3 n = dl > 1e-9 ? d / dl : V3(axis == 0 ? sgn : 0, axis == 1 ? sgn : 0, axis == 2 ? sgn : 0);
                    const V3 local = V3(inner[0], inner[1], inner[2]) + n * w.rr;
                    const V3 base = b.c + b.ax[0] * local.x + b.ax[1] * local.y + b.ax[2] * local.z;
                    const V3 wn = norm(b.ax[0] * n.x + b.ax[1] * n.y + b.ax[2] * n.z);
                    // How far the point is from the nearest edge (0 on an edge) and corner of the box.
                    double edge = 1e9, corner = 0;
                    int ne = 0;
                    for (int k = 0; k < 3; k++)
                        if (k != axis) {
                            const double dk = he[k] - std::fabs(q[k]);
                            edge = std::min(edge, dk);
                            corner += dk * dk, ne++;
                        }
                    corner = std::sqrt(corner / ne);
                    // The wear: lumps (less on a tread), the sides stepped in layers (the strata), pits and corners chipped
                    // out along the edges, the rounded lips eroded a bit more. All of it inward.
                    const double up = std::max(0.0, wn.y), side = (1 - up) * (1 - up);
                    const double amp = 0.05 + 0.13 * side + 0.03 * minHe;
                    const double lump = lumps(w, base) * amp;
                    const double cn = chipNoise(w, base), cn2 = chipNoise(w, base * 0.7 + V3(11, 4, 7));
                    const double chip = 0.2 * smoothstep(0.5, 0.72, cn) * smoothstep(0.5, 0.03, edge);
                    const double broke = 0.3 * smoothstep(0.58, 0.82, cn2) * smoothstep(0.75, 0.1, corner);
                    const double erode = 0.07 * smoothstep(0.35, 0.0, edge) * (0.4 + 0.6 * lumps(w, base * 1.7 + V3(5, 5, 5)));
                    // Layers about 0.3 m thick, each stood back by its own amount (a ramp between), sides only.
                    const double ly = base.y * 3.3 + h01(11) * 5, li = std::floor(ly), lf = ly - li;
                    auto layer = [&](double i) { return 0.16 * latticeHash((int)i + (int)(sd % 977), 13) * (0.6 + 0.8 * lumps(w, base * 0.6 + V3(3, 0, 3))); };
                    const double r0 = layer(li - 1), r1 = layer(li), ramp = smoothstep(0.0, 0.22, lf);
                    const double stepped = (r0 + (r1 - r0) * ramp) * side;
                    const double dStep = (r1 - r0) * 6 * ramp * (1 - ramp) / 0.22 * 3.3 * side;  // d(stepped)/dy, roughly
                    const double depth = lump + chip + broke + erode + stepped;
                    // The surface leans with the lumps and the steps: a normal bent along their slope.
                    const double e = 0.12;
                    const V3 g(lumps(w, base + V3(e, 0, 0)) - lumps(w, base - V3(e, 0, 0)), lumps(w, base + V3(0, e, 0)) - lumps(w, base - V3(0, e, 0)),
                               lumps(w, base + V3(0, 0, e)) - lumps(w, base - V3(0, 0, e)));
                    const V3 slope = g * (amp / (2 * e) * 1.4) + V3(0, dStep, 0);
                    const V3 sn = norm(wn + (slope - wn * dot(slope, wn)));
                    pos[iu * Nv + iv] = base - wn * depth;
                    nrm[iu * Nv + iv] = sn;
                    // Strata banding and a darker pit.
                    const double y = base.y;
                    double band = 0.05 * std::sin(y * 8.0 + w.o1.x) + 0.035 * std::sin(y * 21.0 + bandPh) + 0.03 * std::sin(y * 3.3 + w.o2.y);
                    shade[iu * Nv + iv] = (float)(1.0 + band + 0.14 * (lumps(w, base * 1.3 + w.o3) - 0.5) - 1.3 * depth + 0.08 * up);
                }
            for (int iu = 0; iu + 1 < Nu; iu++)
                for (int iv = 0; iv + 1 < Nv; iv++) {
                    const int idx[4] = {iu * Nv + iv, (iu + 1) * Nv + iv, (iu + 1) * Nv + iv + 1, iu * Nv + iv + 1};
                    const V3 fn = cross(pos[idx[1]] - pos[idx[0]], pos[idx[2]] - pos[idx[0]]);
                    const bool flip = dot(fn, nrm[idx[0]]) < 0;
                    const V3 fq = len(fn) > 1e-12 ? norm(fn) * (flip ? -1.0 : 1.0) : nrm[idx[0]];  // (the facet's own normal, mixed in: a little crystalline, not pillowy)
                    const int order[6] = {0, 1, 2, 0, 2, 3};
                    for (int k = 0; k < 6; k++) {
                        const int o = idx[flip ? order[5 - k] : order[k]];
                        out.push_back(vert(pos[o], norm(nrm[o] * 0.55 + fq * 0.45), col * (double)shade[o], SM_SLAB));
                    }
                }
        }
}

}  // namespace

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
    int logSeed = 0;
    for (const Log& g : world.logs) {
        if (skip.logs) break;
        mk::Mesh m;
        forest::logMesh(world, g, (uint32_t)++logSeed, m);
        appendModel(v, m, V3(), 0.0);  // (built where it lies)
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
    const bool bushModel = mk::findModel("bush") != nullptr;
    for (const Bush& b : world.bushes) {
        if (skip.bushes) break;
        seed++;
        if (bushModel) {  // the picked model, made 0.8 m in radius
            Placed pl;
            pl.id = "bush";
            pl.set = {{"seed", 1 + seed % 6}, {"dry", seed % 7 == 0 ? 1 : 0}};
            const double k = b.r / 0.8;
            appendModel(v, placedMesh(pl), b.p, seed * 2.39996, V3(k, k * (0.8 + 0.4 * latticeHash(seed, 2)), k), (float)(0.85 + 0.15 * b.tint));
            continue;
        }
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

void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, double yaw, const V3& s, float tint) {
    const V3 f(std::cos(yaw), 0, -std::sin(yaw)), up(0, 1, 0), l(-f.z, 0, f.x);
    if (out.capacity() < out.size() + m.v.size()) out.reserve(std::max(out.capacity() * 2, out.size() + m.v.size()));  // (grow by doubling: an exact reserve per model copied the whole forest for every tree)
    for (const mk::MVert& q : m.v) {
        const V3 w = p + f * (q.p[0] * s.x) + up * (q.p[1] * s.y) + l * (q.p[2] * s.z);
        const V3 n = norm(f * (q.n[0] / s.x) + up * (q.n[1] / s.y) + l * (q.n[2] / s.z));
        out.push_back(Vtx{{(float)w.x, (float)w.y, (float)w.z}, {(float)n.x, (float)n.y, (float)n.z}, {q.c[0] * tint, q.c[1] * tint, q.c[2] * tint, (float)q.mat}});
    }
}

void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, const V3 a[3], const V3& s) {
    // (A car's parts are a few hundred thousand vertices a frame: the turn and scale as float matrices.)
    const bool flat = s.x == 1 && s.y == 1 && s.z == 1;
    const float P[9] = {(float)(a[0].x * s.x), (float)(a[1].x * s.y), (float)(a[2].x * s.z),
                        (float)(a[0].y * s.x), (float)(a[1].y * s.y), (float)(a[2].y * s.z),
                        (float)(a[0].z * s.x), (float)(a[1].z * s.y), (float)(a[2].z * s.z)};
    const float N[9] = {(float)(a[0].x / s.x), (float)(a[1].x / s.y), (float)(a[2].x / s.z),
                        (float)(a[0].y / s.x), (float)(a[1].y / s.y), (float)(a[2].y / s.z),
                        (float)(a[0].z / s.x), (float)(a[1].z / s.y), (float)(a[2].z / s.z)};
    const float px = (float)p.x, py = (float)p.y, pz = (float)p.z;
    size_t at = out.size();
    out.resize(at + m.v.size());
    for (const mk::MVert& q : m.v) {
        Vtx& o = out[at++];
        o.p[0] = px + P[0] * q.p[0] + P[1] * q.p[1] + P[2] * q.p[2];
        o.p[1] = py + P[3] * q.p[0] + P[4] * q.p[1] + P[5] * q.p[2];
        o.p[2] = pz + P[6] * q.p[0] + P[7] * q.p[1] + P[8] * q.p[2];
        float nx = N[0] * q.n[0] + N[1] * q.n[1] + N[2] * q.n[2], ny = N[3] * q.n[0] + N[4] * q.n[1] + N[5] * q.n[2],
              nz = N[6] * q.n[0] + N[7] * q.n[1] + N[8] * q.n[2];
        if (!flat) {
            const float l = 1.0f / std::sqrt(std::max(1e-12f, nx * nx + ny * ny + nz * nz));
            nx *= l, ny *= l, nz *= l;
        }
        o.n[0] = nx, o.n[1] = ny, o.n[2] = nz;
        o.c[0] = q.c[0], o.c[1] = q.c[1], o.c[2] = q.c[2], o.c[3] = (float)q.mat;
    }
}

void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, const V3 a[3], double s) { appendModel(out, m, p, a, V3(s, s, s)); }

void appendPlaced(std::vector<Vtx>& out, const Placed& pl, const mk::Mesh& m) {
    const size_t at = out.size();
    if (pl.clip) {  // (squash what lies past the cut flat onto it: a straight tube is cut clean, the rest folds to nothing)
        mk::Mesh cut;
        cut.v = m.v;
        for (mk::MVert& q : cut.v) q.p[0] = std::min(std::max(q.p[0], pl.clipLo), pl.clipHi);
        if (pl.oriented) appendModel(out, cut, pl.p, pl.ax, pl.scale);
        else appendModel(out, cut, pl.p, pl.yaw, pl.scale);
    } else if (pl.oriented) appendModel(out, m, pl.p, pl.ax, pl.scale);
    else appendModel(out, m, pl.p, pl.yaw, pl.scale);
    // (material + 100: the lit shader skips the shadow; + 200 per wear step: it tints and rims the part)
    const float add = (pl.sunlit ? 100.0f : 0.0f) + 200.0f * pl.wear;
    if (add > 0)
        for (size_t i = at; i < out.size(); i++) out[i].c[3] += add;
}

const mk::Mesh& placedMesh(const Placed& pl) {
    if (pl.set.empty() && pl.variant < 0 && !pl.bare) return mk::modelMesh(pl.id);
    static std::map<std::string, mk::Mesh> built;  // (cleared with the picks: see the lab)
    static uint64_t builtFor = 0;
    if (builtFor != mk::picksVersion()) built.clear(), builtFor = mk::picksVersion();
    std::string key = pl.id + "#" + std::to_string(pl.variant) + (pl.bare ? "|bare" : "");
    for (const auto& kv : pl.set) key += "|" + kv.first + "=" + std::to_string(kv.second);
    auto it = built.find(key);
    if (it != built.end()) return it->second;
    mk::Mesh& m = built[key];
    if (const mk::ModelDef* d = mk::findModel(pl.id)) {
        const int variant = pl.variant < 0 ? mk::pickedVariant(*d) : pl.variant;
        mk::P p = mk::pickedParams(*d, variant);
        for (const auto& kv : pl.set) p.v[kv.first] = kv.second;
        if (pl.bare) p.v["bare"] = 1;
        mk::build(*d, variant, p, m);
    }
    return m;
}

void buildPropMesh(const World& world, std::vector<Vtx>& v) {
    for (const Placed& pl : world.models) appendPlaced(v, pl, placedMesh(pl));
    const V3 wood(0.45, 0.33, 0.21), darkWood(0.28, 0.20, 0.13), grey(0.42, 0.40, 0.37);
    int seed = 0;
    for (const Prop& p : world.fixtures) {
        seed++;
        const V3 f = fromYaw(p.yaw), side(-f.z, 0, f.x), up(0, 1, 0);
        // The picked model (modelkit.h) for it, when there is one.
        {
            Placed pl;
            pl.p = p.a, pl.yaw = p.yaw;
            switch (p.kind) {
            case PROP_CABIN: pl.id = "cabin"; break;
            case PROP_GARAGE: pl.id = "repair_shed"; break;
            case PROP_TABLE: pl.id = "picnic_table"; break;
            case PROP_MAPBOARD: pl.id = "map_board"; break;
            case PROP_MARKER: pl.id = "marker_post"; break;
            case PROP_CAIRN: pl.id = "cairn", pl.set = {{"seed", 1 + seed % 9}}; break;
            case PROP_SIGN: {  // the rating from its colour: green, blue, black, red
                const double rating = p.col[1] > 0.4 ? 0 : p.col[2] > 0.5 ? 1 : p.col[0] > 0.5 ? 3 : 2;
                pl.id = "trail_sign", pl.set = {{"rating", rating}};
                break;
            }
            case PROP_STUMP:
                pl.id = "stump", pl.set = {{"seed", 1 + seed % 6}};
                pl.scale = V3(p.b.x / 0.35, p.b.y / 0.6, p.b.x / 0.35);
                break;
            default: break;
            }
            if (!pl.id.empty() && mk::findModel(pl.id)) {
                appendModel(v, placedMesh(pl), pl.p, pl.yaw, pl.scale);
                continue;
            }
        }
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
