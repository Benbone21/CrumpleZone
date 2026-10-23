#include "watermesh.h"
#include "meshopt.h"
#include "modelkit.h"
#include <algorithm>
#include <cmath>

namespace {
Vtx waterVtx(const V3& p, float fx, float fz) {
    return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {0, 1, 0}, {fx, fz, 0, 0}};
}
}  // namespace

// The falls: the "waterfall" model built to each one's size and set down at its foot, its water materials in the colour's alpha
// (the water shader draws them: modelkit.h WATERFALL..RIPPLE).
void appendFalls(const World& world, std::vector<Vtx>& wv) {
    const mk::ModelDef* def = mk::findModel("waterfall");
    if (!def) return;
    for (const Falls& f : world.falls) {
        const int v = std::clamp(f.variant, 0, (int)def->variants.size() - 1);
        mk::P p = mk::defaults(def->variants[v]);
        p.v["height"] = f.height, p.v["width"] = f.width, p.v["lip"] = f.throwOut, p.v["seed"] = (double)(f.seed % 999 + 1);
        mk::Mesh m;
        mk::build(*def, v, p, m);
        const double c = std::cos(f.yaw), s = std::sin(f.yaw);
        const V3 fw(c, 0, -s), lf(s, 0, c), o(f.lip.x, f.lip.y - f.height, f.lip.z);
        for (const mk::MVert& q : m.v) {
            const V3 pw = o + fw * q.p[0] + V3(0, q.p[1], 0) + lf * q.p[2], nw = fw * q.n[0] + V3(0, q.n[1], 0) + lf * q.n[2];
            wv.push_back(Vtx{{(float)pw.x, (float)pw.y, (float)pw.z}, {(float)nw.x, (float)nw.y, (float)nw.z}, {q.c[0], q.c[1], q.c[2], (float)q.mat}});
        }
    }
}

void buildWaterMesh(const World& world, std::vector<Vtx>& wv) {
    if (!world.waterY.empty()) {
        const int n = world.gridSize(), step = 2;
        auto lvl = [&](int i, int j) { return world.waterY[std::min(i, n - 1) * n + std::min(j, n - 1)]; };
        // A big map (its lakes are big): a block of 8 x 8 cells all under still water at one level is one quad, not sixteen.
        const int big = world.halfSize() > 1000 ? 8 : 0;
        auto stillBlock = [&](int i0, int j0) {
            if (!big || i0 + big >= n || j0 + big >= n) return false;
            const float l = lvl(i0, j0);
            if (l < -1e8f) return false;
            for (int i = i0; i <= i0 + big; i += step)
                for (int j = j0; j <= j0 + big; j += step) {
                    if (lvl(i, j) != l) return false;
                    const V3 f = world.waterFlow(-world.halfSize() + i, -world.halfSize() + j);
                    if (f.x != 0 || f.z != 0) return false;
                }
            return true;
        };
        std::vector<char> merged;
        if (big) {
            const int bn = (n - 1) / big;
            merged.assign((size_t)bn * bn, 0);
            for (int bi = 0; bi < bn; bi++)
                for (int bj = 0; bj < bn; bj++)
                    if (stillBlock(bi * big, bj * big)) {
                        merged[(size_t)bi * bn + bj] = 1;
                        const double x0 = -world.halfSize() + bi * big, z0 = -world.halfSize() + bj * big, y = lvl(bi * big, bj * big);
                        const V3 q[4] = {V3(x0, y, z0), V3(x0 + big, y, z0), V3(x0 + big, y, z0 + big), V3(x0, y, z0 + big)};
                        for (int k : {0, 1, 2, 0, 2, 3}) wv.push_back(waterVtx(q[k], 0, 0));
                    }
        }
        for (int i = 0; i + step < n; i += step)
            for (int j = 0; j + step < n; j += step) {
                if (big) {
                    const int bn = (n - 1) / big, bi = i / big, bj = j / big;
                    if (bi < bn && bj < bn && merged[(size_t)bi * bn + bj]) continue;
                }
                float c[4] = {lvl(i, j), lvl(i + step, j), lvl(i + step, j + step), lvl(i, j + step)};
                float top = -1e9f;
                for (float v : c) top = std::max(top, v);
                if (top < -1e8f) continue;
                bool lip = false;  // (a dry corner far below: the edge of a falls, whose model carries the water over it)
                for (int k = 0; k < 4 && !world.falls.empty(); k++)
                    if (c[k] < -1e8f) {
                        const int ii = k == 1 || k == 2 ? i + step : i, jj = k >= 2 ? j + step : j;
                        lip |= world.height(-world.halfSize() + ii, -world.halfSize() + jj) < top - 1.5;
                    }
                if (lip) continue;
                for (float& v : c)
                    if (v < -1e8f) v = top;  // a dry corner: carry the level over (the shader cuts at the shore)
                const int ci[4][2] = {{i, j}, {i + step, j}, {i + step, j + step}, {i, j + step}};
                V3 q[4];
                float fx[4], fz[4];
                for (int k = 0; k < 4; k++) {
                    const double x = -world.halfSize() + ci[k][0], z = -world.halfSize() + ci[k][1];
                    q[k] = V3(x, c[k], z);
                    const V3 fl = world.waterFlow(x, z);
                    fx[k] = (float)fl.x, fz[k] = (float)fl.z;
                }
                for (int k : {0, 1, 2, 0, 2, 3}) wv.push_back(waterVtx(q[k], fx[k], fz[k]));
            }
        MeshOptOptions o;
        o.weldEps = 1e-4;  // (world-sized floats)
        optimizeVtx(wv, o);
    } else {
        // Lakes: a flat disc at the water level, a fan from the middle (the shader cuts it at the shore).
        for (const Water& wt : world.waters) {
            const int segs = 96;
            auto P = [&](int si) {
                const double rr = wt.r + 1.0, a = 2 * M_PI * si / segs;
                return V3(wt.x + rr * std::cos(a), wt.y, wt.z + rr * std::sin(a));
            };
            const V3 mid(wt.x, wt.y, wt.z);
            for (int si = 0; si < segs; si++)
                for (const V3& q : {mid, P(si), P(si + 1)}) wv.push_back(waterVtx(q, 0, 0));
        }
    }
    appendFalls(world, wv);
}
