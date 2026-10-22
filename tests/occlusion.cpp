// Terrain-horizon occlusion (occlusion.h): a box behind a hill is culled, a box that peeks over the ridge never is,
// and on real maps nothing it calls hidden can be seen (every sampled point of the box is under the ground from the eye).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../src/occlusion.h"
#include "../src/world.h"
#include "harness.h"

namespace {
struct Field {
    int n;
    double sp, origin;
    std::vector<float> h;
    double at(double x, double z) const {  // the drawn triangles (diagonal (i,j)-(i+1,j+1))
        const double u = (x - origin) / sp, v = (z - origin) / sp;
        if (u < 0 || v < 0 || u >= n - 1 || v >= n - 1) return -1e9;
        const int i = (int)u, j = (int)v;
        const double fu = u - i, fv = v - j;
        auto H = [&](int a, int b) { return (double)h[(size_t)a * n + b]; };
        if (fu >= fv) return H(i, j) + fu * (H(i + 1, j) - H(i, j)) + fv * (H(i + 1, j + 1) - H(i + 1, j));
        return H(i, j) + fv * (H(i, j + 1) - H(i, j)) + fu * (H(i + 1, j + 1) - H(i, j + 1));
    }
};
Field fromWorld(const World& w, double sp) {
    Field f;
    f.sp = sp, f.origin = -w.halfSize(), f.n = (int)(2 * w.halfSize() / sp) + 1;
    f.h.resize((size_t)f.n * f.n);
    for (int i = 0; i < f.n; i++)
        for (int j = 0; j < f.n; j++) f.h[(size_t)i * f.n + j] = (float)w.height(f.origin + i * sp, f.origin + j * sp);
    return f;
}
RenderBounds box(double x0, double y0, double z0, double x1, double y1, double z1) {
    RenderBounds b;
    b.add(V3(x0, y0, z0)), b.add(V3(x1, y1, z1));
    return b;
}
// Brute force: is `p` hidden from `eye` by the surface (0.2 m steps)?
bool blocked(const Field& f, const V3& eye, const V3& p) {
    const double dist = len(p - eye);
    for (double t = 0.2; t < dist; t += 0.2) {
        const V3 q = eye + (p - eye) * (t / dist);
        if (f.at(q.x, q.z) > q.y) return true;
    }
    return false;
}
unsigned rngState = 12345;
double rnd() { rngState = rngState * 1664525u + 1013904223u; return (rngState >> 8) / 16777216.0; }
}  // namespace

// A plain with a ridge at x = 0 (20 m high, 40 m wide, 200 m long) and a pit: the geometry cases.
SCENARIO(occlusionRidge, "occlusion/ridge", "Behind a ridge is culled; a box that peeks over it, one beside it, the eye inside a box, the eye under the ground, and a stale pyramid are never culled") {
    Field f;
    f.n = 601, f.sp = 2, f.origin = -600;
    f.h.assign((size_t)f.n * f.n, 0.0f);
    for (int i = 0; i < f.n; i++)
        for (int j = 0; j < f.n; j++) {
            const double x = f.origin + i * f.sp, z = f.origin + j * f.sp;
            f.h[(size_t)i * f.n + j] = std::fabs(z) < 100 ? (float)(20.0 * std::max(0.0, 1 - std::fabs(x) / 20.0)) : 0.0f;
        }
    TerrainOcclusion o;
    o.build(f.h, f.n, f.sp, f.origin);
    o.setEye(V3(-150, 3, 0));
    ctx.check("a box behind the ridge is culled", o.occluded(box(100, 0, -10, 130, 8, 10)));
    ctx.check("so is a wide one across the ridge's width", o.occluded(box(100, 0, -60, 130, 8, 60)));
    ctx.check("the same box in front of the ridge is not", !o.occluded(box(-130, 0, -10, -100, 8, 10)));
    ctx.check("a tall box behind the ridge peeks over it: not culled", !o.occluded(box(100, 0, -10, 130, 80, 10)));
    ctx.check("a box past the ridge's end (beside it) is not culled", !o.occluded(box(100, 0, 500, 130, 8, 520)));
    ctx.check("a box on the near slope, in plain view, is not culled", !o.occluded(box(-15, 0, -5, -5, 15, 5)));
    ctx.check("the eye inside a box: not culled", !o.occluded(box(-170, -5, -20, -130, 30, 20)));
    ctx.check("the eye in the box's column but below it: not culled", !o.occluded(box(-160, 40, -5, -140, 60, 5)));
    o.setEye(V3(-150, -5, 0));
    ctx.check("the eye under the ground culls nothing", !o.occluded(box(100, 0, -10, 130, 8, 10)));
    o.setEye(V3(-150, 3, 0));
    ctx.check("(and back above it, it culls again)", o.occluded(box(100, 0, -10, 130, 8, 10)));
    o.patch(50, 50, 5.0f);
    ctx.check("a patched (stale) pyramid culls nothing", !o.occluded(box(100, 0, -10, 130, 8, 10)));
    o.refresh(true);
    ctx.check("until it is rebuilt", o.occluded(box(100, 0, -10, 130, 8, 10)));
    o.setEye(V3(900, 3, 0));  // off the map
    ctx.check("an eye off the map never crashes or culls what is near", !o.occluded(box(880, 0, -10, 890, 8, 10)));
    o.enable(false);
    o.setEye(V3(-150, 3, 0));
    ctx.check("switched off, nothing is culled", !o.occluded(box(100, 0, -10, 130, 8, 10)));
}

// The real maps: random eyes and boxes; whatever is called hidden has every sampled point under the ground.
static void soundOn(Ctx& ctx, const char* name, World& w, double sp, int eyes) {
    const Field f = fromWorld(w, sp);
    TerrainOcclusion o;
    o.build(f.h, f.n, f.sp, f.origin);
    long tests = 0, hid = 0, wrong = 0, checked = 0;
    for (int e = 0; e < eyes; e++) {
        const double ex = (rnd() - 0.5) * 900, ez = (rnd() - 0.5) * 900;
        const V3 eye(ex, f.at(ex, ez) + 0.3 + rnd() * 25, ez);
        o.setEye(eye);
        for (int b = 0; b < 160; b++) {
            const bool chunk = b % 2 == 0;
            const double w0 = chunk ? 64 : 2 + rnd() * 10;
            const double d = 20 + rnd() * 450, a = rnd() * 6.2832;
            const double x0 = ex + std::cos(a) * d, z0 = ez + std::sin(a) * d;
            if (std::fabs(x0) > 560 || std::fabs(z0) > 560) continue;
            RenderBounds r;
            double ylo = 1e9, yhi = -1e9;
            for (int k = 0; k <= 8; k++)
                for (int m = 0; m <= 8; m++) {
                    const double y = f.at(std::min(x0 + w0 * k / 8, 590.0), std::min(z0 + w0 * m / 8, 590.0));
                    ylo = std::min(ylo, y), yhi = std::max(yhi, y);
                }
            r = box(x0, chunk ? ylo : ylo, z0, x0 + w0, chunk ? yhi : yhi + rnd() * 6, z0 + w0);
            ++tests;
            if (!o.occluded(r)) continue;
            ++hid;
            if (checked > 1500) continue;
            ++checked;
            for (int k = 0; k <= 4; k++)
                for (int m = 0; m <= 4; m++)
                    for (int y = 0; y < 3; y++) {
                        const V3 p(r.lo.x + (r.hi.x - r.lo.x) * k / 4, r.lo.y + (r.hi.y - r.lo.y) * (y == 2 ? 1.0 : y * 0.5), r.lo.z + (r.hi.z - r.lo.z) * m / 4);
                        if (!blocked(f, eye, p)) ++wrong;
                    }
        }
    }
    ctx.check(std::string(name) + ": nothing called hidden has a visible sampled point", wrong == 0, std::to_string(wrong) + " visible points in " + std::to_string(checked) + " culled boxes");
    ctx.metric(std::string(name) + ": boxes culled", 100.0 * hid / std::max(1L, tests), "%", 1, 100);
}

SCENARIO(occlusionSound, "occlusion/sound", "On Frontier, the canyon wilds and the mountain, nothing the horizon test culls can be seen from the eye") {
    {
        World w(MAP_FRONTIER);
        soundOn(ctx, "frontier", w, 1.0, 60);
    }
    {
        World::wildsKind = WILDS_CANYON;
        World w(MAP_WILDS);
        soundOn(ctx, "canyon", w, 1.0, 60);
        World::wildsKind = WILDS_MEADOW;
    }
    {
        World w(MAP_MOUNTAIN);
        soundOn(ctx, "mountain", w, 1.0, 60);
    }
}
