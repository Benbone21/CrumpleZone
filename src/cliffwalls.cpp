// Cliff walls carved into the rock (cliffwalls.h).
#include "cliffwalls.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "mathx.h"
#include "world.h"

namespace cliffwalls {
namespace {

constexpr int kSub = 2;              // lattice steps a 1 m cell across
constexpr double kS = 1.0 / kSub;    // ...so half a metre
constexpr double kSV = 0.5;          // and half a metre up
constexpr int kReach = 4;            // a wall's foot and rim: the lowest and highest ground within this many metres
constexpr double kMinDrop = 1.5;     // a cell is a face where its corners differ by this much (about 56 degrees)...
constexpr double kMinWall = 4.0;     // ...on a wall at least this high
constexpr int kAround = 3;           // the carving reaches this many cells into the rock behind a face

double hash1(double n) {
    const double s = std::sin(n * 12.9898 + 4.1414) * 43758.5453;
    return s - std::floor(s);
}
double hash2(double x, double z) { return hash1(x * 1.731 + z * 57.137); }
double vn1(double x) {
    const double i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
    return hash1(i) + (hash1(i + 1) - hash1(i)) * u;
}
double vn2(double x, double z) {
    const double ix = std::floor(x), iz = std::floor(z), fx = x - ix, fz = z - iz;
    const double ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
    const double a = hash2(ix, iz), b = hash2(ix + 1, iz), c = hash2(ix, iz + 1), d = hash2(ix + 1, iz + 1);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
}

double faultShift(const Beds& b, double x, double z) {
    if (b.shift == 0) return 0;
    const double dx = b.bx - b.ax, dz = b.bz - b.az, L = std::max(1e-3, std::sqrt(dx * dx + dz * dz));
    const double side = (dx * (z - b.az) - dz * (x - b.ax)) / L;  // (> 0: left of it)
    return b.shift * smoothstep(-0.5 * b.width, 0.5 * b.width, side);
}

}  // namespace

double carve(const Beds& beds, double x, double y, double z, double hb, double ht, double wallH) {
    // Nothing a tire can reach (the foot's first couple of metres), nothing at the very rim, nothing on a low wall.
    const double m = smoothstep(2.0, 3.5, hb) * smoothstep(0.4, 1.5, ht) * smoothstep(kMinWall, kMinWall + 2.0, wallH);
    if (m <= 0) return 0;
    // The beds, level across the face (offset across a fault), 2-3 m thick: the softer ones (under half) weathered back under the
    // harder ones above, a rounded notch up to 1.5 m deep that comes and goes along the face.
    const double yb = y - faultShift(beds, x, z) + 2.5 * (vn2(x * 0.012 + 3.1, z * 0.012 - 1.7) - 0.5);
    const double u = yb * 0.42 + 0.8 * vn1(yb * 0.13 + 2.0);
    const double k = std::floor(u), f = u - k;
    double notch = 0;
    if (hash1(k) < 0.5) {
        const double pinch = smoothstep(0.3, 0.65, vn2(x * 0.06 + k * 3.7, z * 0.06 - k * 1.3));
        // (deeper and shallower along the face, and a bed's notch not always its middle: under its top or over its foot)
        const double along = 0.35 + 0.9 * vn2(x * 0.035 + k * 1.9, z * 0.035 - k * 0.7);
        const double skew = 0.3 + 0.4 * hash1(k + 13.7), g = f < skew ? 0.5 * f / skew : 0.5 + 0.5 * (f - skew) / (1 - skew);
        notch = std::pow(std::sin(M_PI * g), 0.7) * (0.25 + 1.2 * hash1(k + 71.3)) * pinch * along;
    }
    // Under the rim the face is set back under the caprock's lip (on a tall wall).
    const double cap = 1.0 * smoothstep(1.0, 2.5, ht) * (1 - smoothstep(4.0, 9.0, ht)) * smoothstep(8.0, 14.0, wallH);
    // Alcoves: arched hollows, flat-floored, 10-24 m across and 3.5-9 m high, 1.5-3.3 m deep, here and there along a tall face, from a
    // few metres above its foot.
    double alc = 0;
    if (wallH > 8) {
        constexpr double G = 22;
        const double foot = y - hb;
        const int gx = (int)std::floor(x / G), gz = (int)std::floor(z / G);
        for (int a = gx - 1; a <= gx + 1; a++)
            for (int b = gz - 1; b <= gz + 1; b++) {
                const double h0 = hash2(a + 0.5, b - 0.5);
                if (h0 > 0.6) continue;
                const double h1 = hash2(a - 3.3, b + 7.1), h2 = hash2(a + 9.7, b + 2.9), h3 = hash2(a * 1.3 - 5.0, b * 0.7 + 1.0);
                const double cx = (a + 0.2 + 0.6 * h1) * G, cz = (b + 0.2 + 0.6 * h2) * G;
                const double ra = 5.0 + 7.0 * h3, rb = 3.5 + 5.5 * h1, depth = 1.5 + 1.8 * h2;
                const double cy = foot + 3.0 + 0.5 * rb + h3 * std::max(0.0, wallH - 6.0 - 1.5 * rb) * 0.6;
                const double dy = (y - cy) / (y < cy ? 0.45 * rb : rb);
                const double r = std::sqrt(((x - cx) * (x - cx) + (z - cz) * (z - cz)) / (ra * ra) + dy * dy);
                alc = std::max(alc, depth * smoothstep(1.0, 0.5, r));
            }
    }
    // A rough skin: the rock's own lumps, a few centimetres to a hand deep.
    const double rough = 0.16 * vn2(x * 0.9 + y * 0.7, z * 0.9 - y * 0.55) + 0.06 * vn2(x * 2.7 - y * 2.1, z * 2.7 + y * 1.9);
    double rec = m * (std::max(notch, cap) + alc + rough);
    return std::min({rec, 3.2, 0.45 * ht + 0.1, 0.3 * wallH});
}

void build(const World& world, const Beds& beds, int i0, int j0, int w, int h, float material, std::vector<Vert>& verts, std::vector<unsigned>& idx) {
    const double half = world.halfSize();
    const int N = world.gridSize();
    // The land's grid round the chunk (heights at grid points, exact), with room for the foot and rim search and the carving.
    const int A = kReach + kAround + 2;
    const int gi0 = std::max(0, i0 - A), gj0 = std::max(0, j0 - A), gi1 = std::min(N - 1, i0 + w + A), gj1 = std::min(N - 1, j0 + h + A);
    const int gw = gi1 - gi0 + 1, gh = gj1 - gj0 + 1;
    if (gw < 4 || gh < 4) return;
    std::vector<float> G((size_t)gw * gh), lo((size_t)gw * gh), hi((size_t)gw * gh), tmp((size_t)gw * gh);
    for (int a = 0; a < gw; a++)
        for (int b = 0; b < gh; b++) G[(size_t)a * gh + b] = (float)world.height(-half + gi0 + a, -half + gj0 + b);
    // (the foot and the rim: lowest and highest within kReach, separably)
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float>& out = pass == 0 ? lo : hi;
        for (int a = 0; a < gw; a++)
            for (int b = 0; b < gh; b++) {
                float v = G[(size_t)a * gh + b];
                for (int d = std::max(0, b - kReach); d <= std::min(gh - 1, b + kReach); d++) v = pass == 0 ? std::min(v, G[(size_t)a * gh + d]) : std::max(v, G[(size_t)a * gh + d]);
                tmp[(size_t)a * gh + b] = v;
            }
        for (int a = 0; a < gw; a++)
            for (int b = 0; b < gh; b++) {
                float v = tmp[(size_t)a * gh + b];
                for (int d = std::max(0, a - kReach); d <= std::min(gw - 1, a + kReach); d++) v = pass == 0 ? std::min(v, tmp[(size_t)d * gh + b]) : std::max(v, tmp[(size_t)d * gh + b]);
                out[(size_t)a * gh + b] = v;
            }
    }
    // Bilinear over a grid (as World::height), in local grid units.
    auto bil = [&](const std::vector<float>& F, double x, double z) {
        double fx = clampd(x + half - gi0, 0, gw - 1.001), fz = clampd(z + half - gj0, 0, gh - 1.001);
        const int a = (int)fx, b = (int)fz;
        fx -= a, fz -= b;
        const float* r0 = &F[(size_t)a * gh + b];
        const float* r1 = &F[(size_t)(a + 1) * gh + b];
        return (r0[0] * (1 - fx) + r1[0] * fx) * (1 - fz) + (r0[1] * (1 - fx) + r1[1] * fx) * fz;
    };
    auto H = [&](double x, double z) { return bil(G, x, z); };
    // The faces: cells whose corners differ by kMinDrop on a wall at least kMinWall high; and round them, kAround cells, where
    // the carving may reach.
    const int ci0 = i0 - kAround - 1, cj0 = j0 - kAround - 1, cw = w + 2 * kAround + 2, ch = h + 2 * kAround + 2;
    std::vector<uint8_t> face((size_t)cw * ch, 0), near((size_t)cw * ch, 0);
    bool any = false;
    for (int a = 0; a < cw; a++)
        for (int b = 0; b < ch; b++) {
            const int i = ci0 + a, j = cj0 + b;
            if (i < gi0 || j < gj0 || i + 1 > gi1 || j + 1 > gj1) continue;
            const float* r0 = &G[(size_t)(i - gi0) * gh + (j - gj0)];
            const float* r1 = &G[(size_t)(i + 1 - gi0) * gh + (j - gj0)];
            const float mn = std::min({r0[0], r0[1], r1[0], r1[1]}), mx = std::max({r0[0], r0[1], r1[0], r1[1]});
            const size_t g = (size_t)(i - gi0) * gh + (j - gj0);
            if (mx - mn >= kMinDrop && hi[g] - lo[g] >= kMinWall) face[(size_t)a * ch + b] = 1, any = true;
        }
    if (!any) return;
    for (int a = 0; a < cw; a++)
        for (int b = 0; b < ch; b++) {
            if (!face[(size_t)a * ch + b]) continue;
            for (int c = std::max(0, a - kAround); c <= std::min(cw - 1, a + kAround); c++)
                for (int d = std::max(0, b - kAround); d <= std::min(ch - 1, b + kAround); d++) near[(size_t)c * ch + d] = 1;
        }
    // The lattice's columns: over the cells near a face, from a little above the foot to above the rim.
    const int lx0 = (i0 - kAround) * kSub - 1, lz0 = (j0 - kAround) * kSub - 1;
    const int lw = (w + 2 * kAround) * kSub + 3, lh = (h + 2 * kAround) * kSub + 3;
    struct Col {
        int y0 = 0, n = 0;   // first lattice level, how many
        size_t at = 0;       // into vals
    };
    std::vector<Col> cols((size_t)lw * lh);
    std::vector<float> vals;
    auto cellNear = [&](int ix, int iz) {
        // (a lattice point on a cell's edge belongs to the cells either side)
        for (int da = -1; da <= 0; da++)
            for (int db = -1; db <= 0; db++) {
                if (da == -1 && ix % kSub != 0) continue;
                if (db == -1 && iz % kSub != 0) continue;
                const int a = (int)std::floor((double)ix / kSub) + da - ci0, b = (int)std::floor((double)iz / kSub) + db - cj0;
                if (a >= 0 && b >= 0 && a < cw && b < ch && near[(size_t)a * ch + b]) return true;
            }
        return false;
    };
    // The nearest air at height y from (x, z) within `reach`, any way round (the face, or a hollow's): how far, and which way.
    static const double dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071, 0.7071}, {-0.7071, 0.7071}, {0.7071, -0.7071}, {-0.7071, -0.7071}};
    auto airNear = [&](double x, double y, double z, double reach, double& dist, double& ux, double& uz) {
        dist = reach;
        bool found = false;
        const double h0 = H(x, z) - y;
        if (h0 < 0) return dist = 0, ux = 1, uz = 0, true;
        for (const auto& d : dirs) {
            double prev = h0;
            for (double t = kS; t <= reach + 1e-9; t += kS) {
                if (t - kS >= dist) break;
                const double v = H(x + d[0] * t, z + d[1] * t) - y;
                if (v < 0) {
                    const double at = t - kS + kS * prev / (prev - v);
                    if (at < dist) dist = at, ux = d[0], uz = d[1], found = true;
                    break;
                }
                prev = v;
            }
        }
        return found;
    };
    // (how steep the land's face is where that air begins: the carving is the faces', not a slope's under a wall)
    auto steepAt = [&](double x, double z) {
        const double gx = H(x + 0.5, z) - H(x - 0.5, z), gz = H(x, z + 0.5) - H(x, z - 0.5);
        return smoothstep(1.3, 2.2, std::sqrt(gx * gx + gz * gz));
    };
    for (int a = 0; a < lw; a++)
        for (int b = 0; b < lh; b++) {
            const int ix = lx0 + a, iz = lz0 + b;
            if (!cellNear(ix, iz)) continue;
            const double x = -half + ix * kS, z = -half + iz * kS;
            const double foot = bil(lo, x, z), top = bil(hi, x, z), wallH = top - foot;
            if (wallH < kMinWall) continue;
            const double h0 = H(x, z);
            const double gx = (H(x + 1, z) - H(x - 1, z)) * 0.5, gz = (H(x, z + 1) - H(x, z - 1)) * 0.5, nf = std::sqrt(1 + gx * gx + gz * gz);
            Col& c = cols[(size_t)a * lh + b];
            c.y0 = (int)std::floor((foot + 1.2) / kSV);
            c.n = std::max(0, (int)std::ceil((top + 0.6) / kSV) - c.y0 + 1);
            c.at = vals.size();
            for (int k = 0; k < c.n; k++) {
                const double y = (c.y0 + k) * kSV;
                double D = (h0 - y) / nf;  // (the distance to the land's surface, to first order: + in the rock)
                if (D > 0) {
                    const double rec = carve(beds, x, y, z, y - foot, top - y, wallH);
                    if (rec > 0) {
                        // In the rock and carved: the distance to the air at this height less the carving, where that air is
                        // beyond a steep face.
                        double dist, ux = 0, uz = 0;
                        D = airNear(x, y, z, rec + 0.6, dist, ux, uz) ? dist - rec * steepAt(x + ux * dist, z + uz * dist) : rec + 0.6 - rec;
                    }
                }
                vals.push_back((float)D);
            }
        }
    // Surface nets: a vertex in each lattice voxel the surface crosses (the mean of its edges' crossings), a quad across each
    // lattice edge it crosses, made of the four voxels round that edge.
    auto colAt = [&](int ix, int iz) -> const Col* {
        const int a = ix - lx0, b = iz - lz0;
        if (a < 0 || b < 0 || a >= lw || b >= lh) return nullptr;
        const Col& c = cols[(size_t)a * lh + b];
        return c.n > 0 ? &c : nullptr;
    };
    auto valAt = [&](int ix, int iy, int iz, float& v) {
        const Col* c = colAt(ix, iz);
        if (!c || iy < c->y0 || iy >= c->y0 + c->n) return false;
        v = vals[c->at + (iy - c->y0)];
        return true;
    };
    std::unordered_map<uint64_t, int> vox;  // voxel -> its vertex (-1: none)
    auto key = [](int ix, int iy, int iz) { return ((uint64_t)(uint32_t)(ix + (1 << 20)) << 42) ^ ((uint64_t)(uint32_t)(iy + (1 << 20)) << 21) ^ (uint64_t)(uint32_t)(iz + (1 << 20)); };
    const size_t base = verts.size();
    std::vector<V3> acc;  // (normals, summed)
    auto vertexOf = [&](int ix, int iy, int iz) -> int {
        const uint64_t kk = key(ix, iy, iz);
        auto it = vox.find(kk);
        if (it != vox.end()) return it->second;
        float c[8];
        int r = -1;
        bool ok = true;
        for (int q = 0; q < 8 && ok; q++) ok = valAt(ix + (q & 1), iy + ((q >> 1) & 1), iz + ((q >> 2) & 1), c[q]);
        if (ok) {
            V3 sum(0, 0, 0);
            int n = 0;
            static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : E) {
                const float p = c[e[0]], q = c[e[1]];
                if ((p > 0) == (q > 0)) continue;
                const double t = p / (p - q);
                const V3 A((e[0] & 1), (e[0] >> 1) & 1, (e[0] >> 2) & 1), B((e[1] & 1), (e[1] >> 1) & 1, (e[1] >> 2) & 1);
                sum = sum + (A + (B - A) * t);
                n++;
            }
            if (n) {
                const V3 l = sum * (1.0 / n);
                const double x = -half + (ix + l.x) * kS, y = (iy + l.y) * kSV, z = -half + (iz + l.z) * kS;
                Vert v{};
                v.p[0] = (float)x, v.p[1] = (float)y, v.p[2] = (float)z;
                // (how deep in the rock it is, to the land's face, and which way that is)
                double dist = 0, ux = 1, uz = 0;
                if (!airNear(x, y, z, 4.0, dist, ux, uz) || dist < 0.03) {
                    const double gx = H(x + 1, z) - H(x - 1, z), gz = H(x, z + 1) - H(x, z - 1), gl = std::sqrt(gx * gx + gz * gz);
                    if (dist < 0.03) dist = 0;
                    ux = gl > 1e-6 ? -gx / gl : 1, uz = gl > 1e-6 ? -gz / gl : 0;
                }
                v.c[0] = 0;
                v.c[1] = (float)dist;
                v.c[2] = (float)std::atan2(uz, ux);
                v.c[3] = material;
                r = (int)(verts.size() - base);
                verts.push_back(v);
                acc.push_back(V3(0, 0, 0));
            }
        }
        vox.emplace(kk, r);
        return r;
    };
    // (a quad round a lattice edge, the four voxels in turn round it; `out`: from the edge's rock end to its air end, which way its
    // face must look)
    auto quad = [&](int a, int b, int c, int d, const V3& out) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        unsigned A = (unsigned)(base + a), B = (unsigned)(base + b), C = (unsigned)(base + c), D = (unsigned)(base + d);
        auto P = [&](unsigned q) { return V3(verts[q].p[0], verts[q].p[1], verts[q].p[2]); };
        if (dot(cross(P(C) - P(A), P(D) - P(B)), out) < 0) std::swap(B, D);
        idx.insert(idx.end(), {A, B, C, A, C, D});
    };
    // (the chunk's own edges: base corner in its cells; and only where no tire reaches: above the foot's first metres, below the
    // rim, and not on the land's own surface where it's gentle enough to stand on, a ledge's shelf: only in the rock there)
    auto reachable = [&](double x, double y, double z) {
        const double foot = bil(lo, x, z), top = bil(hi, x, z);
        if (y - foot < 1.8 || top - y < 0.25) return true;
        const double gx = (H(x + 0.5, z) - H(x - 0.5, z)), gz = (H(x, z + 0.5) - H(x, z - 0.5));
        return std::fabs(y - H(x, z)) < 0.4 && gx * gx + gz * gz < 1.3 * 1.3;
    };
    for (int ix = i0 * kSub; ix < (i0 + w) * kSub; ix++)
        for (int iz = j0 * kSub; iz < (j0 + h) * kSub; iz++) {
            const Col* c0 = colAt(ix, iz);
            if (!c0) continue;
            for (int iy = c0->y0; iy < c0->y0 + c0->n; iy++) {
                const float v0 = vals[c0->at + (iy - c0->y0)];
                const double x = -half + ix * kS, y = iy * kSV, z = -half + iz * kS;
                float v1;
                // (+x edge: the voxels round it are (ix, iy-1..iy, iz-1..iz); rock on the low side faces +x)
                if (valAt(ix + 1, iy, iz, v1) && (v0 > 0) != (v1 > 0) && !reachable(x + kS * 0.5, y, z)) {
                    quad(vertexOf(ix, iy - 1, iz - 1), vertexOf(ix, iy, iz - 1), vertexOf(ix, iy, iz), vertexOf(ix, iy - 1, iz), V3(v0 > 0 ? 1 : -1, 0, 0));
                }
                if (valAt(ix, iy + 1, iz, v1) && (v0 > 0) != (v1 > 0) && !reachable(x, y + kSV * 0.5, z)) {
                    quad(vertexOf(ix - 1, iy, iz - 1), vertexOf(ix, iy, iz - 1), vertexOf(ix, iy, iz), vertexOf(ix - 1, iy, iz), V3(0, v0 > 0 ? 1 : -1, 0));
                }
                if (valAt(ix, iy, iz + 1, v1) && (v0 > 0) != (v1 > 0) && !reachable(x, y, z + kS * 0.5)) {
                    quad(vertexOf(ix - 1, iy - 1, iz), vertexOf(ix, iy - 1, iz), vertexOf(ix, iy, iz), vertexOf(ix - 1, iy, iz), V3(0, 0, v0 > 0 ? 1 : -1));
                }
            }
        }
    // Nothing outside the land: a vertex the mesh put a little out of it (where it folds into a crease) is moved back in, into the
    // rock up a steep face, down onto the land where it's gentle.
    for (size_t q = 0; q < acc.size(); q++) {
        Vert& v = verts[base + q];
        const double x = v.p[0], y = v.p[1], z = v.p[2];
        if (y <= H(x, z)) continue;
        const double gx = (H(x + 0.5, z) - H(x - 0.5, z)), gz = (H(x, z + 0.5) - H(x, z - 0.5)), gl = std::sqrt(gx * gx + gz * gz);
        bool moved = false;
        if (gl > 1.0)
            for (double t = 0.05; t <= 1.5 && !moved; t += 0.05)
                if (H(x + gx / gl * t, z + gz / gl * t) >= y + 0.01) v.p[0] = (float)(x + gx / gl * t), v.p[2] = (float)(z + gz / gl * t), moved = true;
        if (!moved) v.p[1] = (float)(H(x, z) - 0.01);
    }
    for (size_t k = 0; k < idx.size(); k += 3) {
        if (idx[k] < base) continue;
        auto P = [&](unsigned q) { return V3(verts[q].p[0], verts[q].p[1], verts[q].p[2]); };
        const V3 n = cross(P(idx[k + 1]) - P(idx[k]), P(idx[k + 2]) - P(idx[k]));
        for (int e = 0; e < 3; e++) acc[idx[k + e] - base] += n;
    }
    for (size_t q = 0; q < acc.size(); q++) {
        V3 n = acc[q];
        const double l = len(n);
        n = l > 1e-9 ? n * (1.0 / l) : V3(0, 1, 0);
        Vert& v = verts[base + q];
        v.n[0] = (float)n.x, v.n[1] = (float)n.y, v.n[2] = (float)n.z;
    }
    // (vertices no quad used: dropped, the indices moved down)
    std::vector<int> remap(acc.size(), -1);
    std::vector<uint8_t> used(acc.size(), 0);
    for (size_t k = 0; k < idx.size(); k++)
        if (idx[k] >= base) used[idx[k] - base] = 1;
    size_t o = base;
    for (size_t q = 0; q < acc.size(); q++)
        if (used[q]) remap[q] = (int)(o - base), verts[o++] = verts[base + q];
    verts.resize(o);
    for (size_t k = 0; k < idx.size(); k++)
        if (idx[k] >= base) idx[k] = (unsigned)(base + remap[idx[k] - base]);
}

}  // namespace cliffwalls
