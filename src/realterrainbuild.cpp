// MAP_REAL: the active real-world terrain (realterrain.h) built into a World. The 1 m grid from the DEM (bicubic, fine detail,
// ledges on the rock), the plan's lakes and rivers carved in with their water and current, its places' pads levelled, its roads
// and trails laid (cut and fill to their grade, fording the rivers), the ground by slope and height, the buildings, and trees,
// bushes and rocks scattered by slope, height and climate. Everything the land hash sees comes from plain arithmetic.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "loadprof.h"
#include "mathx.h"
#include "parallel.h"
#include "plants.h"
#include "realterrain.h"
#include "rivers.h"
#include "world.h"

namespace {
uint32_t hmix(uint32_t h) {
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
    return h;
}
// A number in [0, 1) for cell (i, j), draw k (no shared random stream: a cell's growth doesn't move when another's changes).
double cellRoll(int i, int j, int k, uint32_t seed) {
    return (hmix((uint32_t)i * 73856093u ^ hmix((uint32_t)j * 19349663u ^ hmix((uint32_t)k * 83492791u ^ seed))) & 0xffffff) / double(0x1000000);
}
}  // namespace

void World::buildReal() {
    map = MAP_REAL;
    crashProps = false;
    loadprof::Steps steps("real.start");
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -half + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    waterY.reset(hN, -1e9f);
    flowX.reset(hN, 0);
    flowZ.reset(hN, 0);
    hgrid.assign(N, 0.0f);
    const std::shared_ptr<const realterrain::Terrain> T = realterrain::active();
    if (!T || std::fabs(T->half - half) > 0.5) {
        // No real terrain (or a test's map size): a plain field with the rim round it.
        parallelRows(hN, [&](int i) {
            for (int j = 0; j < hN; j++) {
                const double x = X(i), z = X(j);
                const double ex = std::max(0.0, std::fabs(x) - (half - 130)), ez = std::max(0.0, std::fabs(z) - (half - 130));
                const double d = std::sqrt(ex * ex + ez * ez);
                hgrid[i * hN + j] = (float)(0.0045 * d * d + 0.02 * d);
            }
        });
        spawn = V3(0, 0, 0), spawnYaw = 0;
        return;
    }
    const realterrain::Terrain& t = *T;
    const realterrain::Spec& sp = t.spec;
    const realterrain::Terrain::Plan& P = t.plan();
    const int climate = sp.climate;

    steps.next("real.land");
    parallelRows(hN, [&](int i) {
        for (int j = 0; j < hN; j++) hgrid[i * hN + j] = (float)t.heightAt(X(i), X(j));
    });
    // Slope over +-3 m (the ledges' treads and risers together read as the face they're cut in).
    auto wideSlope = [&](const std::vector<float>& g, int i, int j) {
        const int a = std::max(0, i - 3), b = std::min(hN - 1, i + 3), c = std::max(0, j - 3), d = std::min(hN - 1, j + 3);
        const double hx = (g[b * hN + j] - g[a * hN + j]) / std::max(1, b - a), hz = (g[i * hN + d] - g[i * hN + c]) / std::max(1, d - c);
        return std::sqrt(hx * hx + hz * hz);
    };
    // ---- Ledges on the rock: 10-30 m data smooths sandstone's steps into ramps; this stairs them again ---------------------
    if (sp.ledgeStep > 0) {
        steps.next("real.ledges");
        const std::vector<float> src = hgrid;
        const double step = sp.ledgeStep;
        parallelRows(hN, [&](int i) {
            for (int j = 0; j < hN; j++) {
                const double s = wideSlope(src, i, j);
                const double w = smoothstep(0.18, 0.45, s) * sp.ledgeStrength * (1 - smoothstep(1.4, 2.2, s)) * (t.rimAt(X(i), X(j)) > 0 ? 0 : 1);
                if (w <= 0) continue;
                const double x = X(i), z = X(j), h = src[i * hN + j];
                const double jit = step * 0.4 * (fbmNoise(x * 0.015 + 7.0, z * 0.015 - 3.0, 2) - 0.5);
                const double hs = (h + jit) / step, q = std::floor(hs), f = hs - q;
                const double stair = (q + smoothstep(0.7, 1.0, f)) * step - jit;
                hgrid[i * hN + j] = (float)(h + w * (stair - h));
            }
        });
    }
    CellTiles<uint8_t> keep;  // no trees, rocks or bushes here
    CellTiles<float> edge;    // distance past the nearest trail's tread
    keep.reset(hN, 0), edge.reset(hN, 1e9f);

    // ---- Lakes and the sea ------------------------------------------------------------------------------------------------
    steps.next("real.lakes");
    const int n = P.n;
    {
        // How deep the lake is at a coarse cell: by its distance from the shore (4-neighbour layers), 0.3 m to 3 m.
        std::vector<float> depth((size_t)n * n, 0.0f);
        std::vector<int> front;
        std::vector<int> dist((size_t)n * n, -1);
        for (int id = 0; id < n * n; id++)
            if (P.lake[id] != -1) {
                bool shore = false;
                for (int k = 0; k < 4; k++) {
                    const int i = id / n + (k == 0) - (k == 1), j = id % n + (k == 2) - (k == 3);
                    if (i < 0 || j < 0 || i >= n || j >= n || P.lake[i * n + j] == -1) shore = true;
                }
                if (shore) dist[id] = 0, front.push_back(id);
            }
        for (size_t f = 0; f < front.size(); f++) {
            const int id = front[f];
            for (int k = 0; k < 4; k++) {
                const int i = id / n + (k == 0) - (k == 1), j = id % n + (k == 2) - (k == 3);
                if (i < 0 || j < 0 || i >= n || j >= n) continue;
                const int nb = i * n + j;
                if (P.lake[nb] == -1 || dist[nb] >= 0) continue;
                dist[nb] = dist[id] + 1, front.push_back(nb);
            }
        }
        for (int id = 0; id < n * n; id++)
            if (P.lake[id] != -1) depth[id] = (float)std::min(3.0, 0.3 + 0.05 * (dist[id] + 0.5) * P.cs);
        const double seaY = sp.seaLevel > -1e8 ? t.mapHeight(sp.seaLevel) : -1e9;
        auto lakeRow = [&](int i) {  // (one thread: CellTiles makes its tiles as they are first written)
            const double x = X(i);
            const double fi = (x + half) / P.cs;
            const int i0 = std::min(n - 2, std::max(0, (int)std::floor(fi)));
            const double ti = clampd(fi - i0, 0, 1);
            for (int j = 0; j < hN; j++) {
                const double z = X(j), fj = (z + half) / P.cs;
                const int j0 = std::min(n - 2, std::max(0, (int)std::floor(fj)));
                const double tj = clampd(fj - j0, 0, 1);
                const int ids[4] = {i0 * n + j0, (i0 + 1) * n + j0, i0 * n + j0 + 1, (i0 + 1) * n + j0 + 1};
                const double wts[4] = {(1 - ti) * (1 - tj), ti * (1 - tj), (1 - ti) * tj, ti * tj};
                int L = -1;
                double dep = 0, best = 0;
                for (int k = 0; k < 4; k++) {
                    dep += wts[k] * depth[ids[k]];
                    if (P.lake[ids[k]] != -1 && wts[k] > best) best = wts[k], L = P.lake[ids[k]];
                }
                const int id = i * hN + j;
                const double h = hgrid[id];
                double lvl = -1e9;
                if (L >= 0) lvl = P.lakes[L].level;
                if (seaY > lvl && (L == -2 || h <= seaY)) lvl = seaY;
                if (lvl < -1e8 || h > lvl + 0.02) continue;
                hgrid[id] = (float)std::min(h, lvl - std::max(0.15, dep));
                waterY[id] = (float)lvl;
                keep[id] = 1;
                if (dep < 0.6) surf[id] = climate == realterrain::CLIMATE_DESERT ? SURF_SAND : SURF_MUD, paint[id] = climate == realterrain::CLIMATE_DESERT ? PAINT_SAND : PAINT_MUD;
            }
        };
        for (int i = 0; i < hN; i++) lakeRow(i);
    }

    // ---- Rivers and dry washes ---------------------------------------------------------------------------------------------
    steps.next("real.rivers");
    CellTiles<float> nearestD;
    nearestD.reset(hN, 1e9f);
    std::vector<std::vector<V3>> fords;  // (where the roads cross: filled in below, the spots made fords after)
    for (size_t r = 0; r < P.rivers.size(); r++) {
        const realterrain::PlanRiver& pr = P.rivers[r];
        // The drainage line runs cell to cell (straight runs and 45-degree jogs, worst on flats): eased over +-20 m, its ends kept,
        // then left to wander a little where the land is flat (a confined channel in a slope stays where it is).
        std::vector<V3> pts = realterrain::smoothLine(pr.pts, 2.0);
        for (int pass = 0; pass < 2 && pts.size() > 4; pass++) {
            std::vector<V3> s = pts;
            const int m0 = (int)pts.size();
            for (int k = 1; k + 1 < m0; k++) {
                const int r = std::min(10, std::min(k, m0 - 1 - k));
                V3 sum(0, 0, 0);
                for (int q = k - r; q <= k + r; q++) sum = sum + pts[q];
                s[k] = sum * (1.0 / (2 * r + 1));
            }
            pts.swap(s);
        }
        {
            const int m0 = (int)pts.size();
            std::vector<V3> s = pts;
            double along = 0;
            for (int k = 1; k + 1 < m0; k++) {
                along += std::sqrt((pts[k].x - pts[k - 1].x) * (pts[k].x - pts[k - 1].x) + (pts[k].z - pts[k - 1].z) * (pts[k].z - pts[k - 1].z));
                const int a = std::max(0, k - 15), b = std::min(m0 - 1, k + 15);
                const double run = std::max(1.0, (double)(b - a) * 2.0), fall = std::fabs(pts[a].y - pts[b].y) / run;
                const double flat = 1 - smoothstep(0.004, 0.02, fall), ends = std::min(1.0, std::min(k, m0 - 1 - k) / 20.0);
                const double amp = 7.0 * flat * ends * (2 * fbmNoise(along * 0.012 + r * 13.7, 0.5 + r * 3.1, 2) - 1);
                const V3 d = pts[k + 1] - pts[k - 1];
                const double L = std::sqrt(d.x * d.x + d.z * d.z);
                if (L > 1e-9) s[k] = pts[k] + V3(-d.z / L, 0, d.x / L) * amp;
            }
            pts = realterrain::smoothLine(s, 2.0);
        }
        const int m = (int)pts.size();
        if (m < 3) continue;
        std::vector<double> area(m), lvl(m), dep(m), hw(m);
        for (int k = 0; k < m; k++) area[k] = pr.area[std::min(pr.area.size() - 1, (size_t)((double)k * (pr.area.size() - 1) / (m - 1) + 0.5))];
        for (int k = 0; k < m; k++) {
            const double s = std::sqrt(area[k] / (pr.wet ? sp.riverArea : sp.washArea));
            hw[k] = pr.wet ? std::min(12.0, 2.5 + 1.2 * s) : std::min(6.0, 1.5 + 0.8 * s);
            dep[k] = pr.wet ? std::min(1.3, 0.45 + 0.2 * s) : 0.3;
        }
        for (int k = 0; k < m; k++) {
            const V3 a = pts[std::max(0, k - 1)], b = pts[std::min(m - 1, k + 1)];
            V3 d(b.x - a.x, 0, b.z - a.z);
            const double L = std::sqrt(d.x * d.x + d.z * d.z);
            d = L > 1e-9 ? d * (1 / L) : V3(1, 0, 0);
            double lo = height(pts[k].x, pts[k].z);
            for (int o = -1; o <= 1; o += 2) lo = std::min(lo, height(pts[k].x - d.z * o * (hw[k] + 3), pts[k].z + d.x * o * (hw[k] + 3)));
            lvl[k] = lo - (pr.wet ? 0.3 : 0.05);
        }
        for (int pass = 0; pass < 30; pass++) {
            std::vector<double> s = lvl;
            for (int k = 1; k + 1 < m; k++) s[k] = 0.25 * lvl[k - 1] + 0.5 * lvl[k] + 0.25 * lvl[k + 1];
            lvl.swap(s);
        }
        for (int k = 1; k < m; k++) lvl[k] = std::min(lvl[k], lvl[k - 1] - 0.001);
        River rec;
        rec.name = pr.wet ? sp.name + " RIVER " + std::to_string(r + 1) : "";
        for (int s = 0; s + 1 < m; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L = std::sqrt(dx * dx + dz * dz);
            if (L < 1e-6) continue;
            const double reach = std::max(hw[s], hw[s + 1]) + 10;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double tt = clampd(((x - a.x) * dx + (z - a.z) * dz) / (L * L), 0, 1);
                    const double ex = x - (a.x + dx * tt), ez = z - (a.z + dz * tt), dd = std::sqrt(ex * ex + ez * ez);
                    if (dd > reach) continue;
                    const int id = i * hN + j;
                    const double level = lvl[s] + (lvl[s + 1] - lvl[s]) * tt, de = dep[s] + (dep[s + 1] - dep[s]) * tt;
                    const double wq = (hw[s] + (hw[s + 1] - hw[s]) * tt) * (0.85 + 0.3 * valueNoise(x * 0.05, z * 0.05));
                    if (dd < wq) {
                        const double u = dd / wq;
                        hgrid[id] = (float)std::min((double)hgrid[id], level - de * (1 - u * u) - 0.05);
                        const bool sandy = climate == realterrain::CLIMATE_DESERT;
                        if (pr.wet) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        else surf[id] = sandy ? SURF_SAND : SURF_GRAVEL, paint[id] = sandy ? PAINT_SAND : PAINT_GRAVEL;
                        if (pr.wet && dd < nearestD[id]) {
                            nearestD[id] = (float)dd;
                            waterY[id] = (float)std::max((double)waterY[id], level);
                            const double fall = (lvl[s] - lvl[s + 1]) / L;
                            const double spd = std::min(2.5, 0.4 + 30 * fall) * (1 - 0.6 * u * u);
                            flowX[id] = (float)(dx / L * spd), flowZ[id] = (float)(dz / L * spd);
                        }
                        keep[id] = 1;
                    } else {
                        const double bank = level + (pr.wet ? 0.2 : 0.3) + (dd - wq) * 0.35;
                        if (hgrid[id] > bank) hgrid[id] = (float)(hgrid[id] + (bank - hgrid[id]) * smoothstep(reach, wq, dd));
                        if (pr.wet && dd < wq + 2.5 && paint[id] == PAINT_NONE) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        if (dd < wq + 3) keep[id] = 1;
                    }
                }
        }
        if (pr.wet) {
            for (int k = 0; k < m; k += 2) {
                RiverSpot q;
                q.p = V3(pts[k].x, lvl[k], pts[k].z);
                q.depth = (float)dep[k], q.halfWidth = (float)hw[k];
                q.speed = (float)std::sqrt(flowX[cellI(pts[k].x) * hN + cellI(pts[k].z)] * flowX[cellI(pts[k].x) * hN + cellI(pts[k].z)] +
                                           flowZ[cellI(pts[k].x) * hN + cellI(pts[k].z)] * flowZ[cellI(pts[k].x) * hN + cellI(pts[k].z)]);
                q.cls = (uint8_t)riverClassForDepth(dep[k]);
                q.bed = BED_GRAVEL;
                rec.spots.push_back(q);
            }
            rivers.push_back(std::move(rec));
        }
    }

    // ---- The places' pads ----------------------------------------------------------------------------------------------------
    steps.next("real.pads");
    auto levelDisc = [&](const V3& c, double r, double y, double blend) {
        for (int i = cellI(c.x - r - blend); i <= cellI(c.x + r + blend); i++)
            for (int j = cellI(c.z - r - blend); j <= cellI(c.z + r + blend); j++) {
                const double dx = X(i) - c.x, dz = X(j) - c.z, d = std::sqrt(dx * dx + dz * dz);
                if (d > r + blend) continue;
                const int id = i * hN + j;
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(r + blend, r, d));
                if (d < r + blend * 0.5) keep[id] = 1;
                if (d < r - 0.5) {
                    surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                    waterY[id] = -1e9f, flowX[id] = 0, flowZ[id] = 0;
                }
            }
    };
    for (size_t k = 0; k < P.places.size(); k++)
        if (P.places[k].pad > 0) levelDisc(P.places[k].p, P.places[k].pad, P.padY[k], 10);

    // ---- Roads and trails --------------------------------------------------------------------------------------------------
    steps.next("real.trails");
    CellTiles<float> sd, sk, sl;
    sd.reset(hN, 1e9f), sk.reset(hN, 0), sl.reset(hN, 0);
    std::vector<int> cells;
    auto scanNear = [&](const std::vector<V3>& pts, double reach, auto fn) {
        cells.clear();
        const int m = (int)pts.size();
        for (int s = 0; s + 1 < m; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
            if (L2 < 1e-9) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double tt = clampd(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
                    const double ex = x - (a.x + dx * tt), ez = z - (a.z + dz * tt), d = std::sqrt(ex * ex + ez * ez);
                    if (d > reach) continue;
                    const int id = i * hN + j;
                    if (sd[id] > 1e8f) cells.push_back(id);
                    if (d >= sd[id]) continue;
                    sd[id] = (float)d, sk[id] = (float)(s + tt), sl[id] = (float)((dx * (z - a.z) - dz * (x - a.x)) / L);
                }
        }
        for (int id : cells) {
            fn(id, (double)sd[id], (double)sk[id], (double)sl[id]);
            sd[id] = 1e9f;
        }
    };
    for (const realterrain::PlanTrail& tr : P.trails) {
        const std::vector<V3> pts = realterrain::smoothLine(tr.pts, 1.0);
        const int m = (int)pts.size();
        if (m < 2) continue;
        std::vector<double> raw(m), y(m);
        std::vector<uint8_t> pinned(m, 0);
        for (int k = 0; k < m; k++) raw[k] = height(pts[k].x, pts[k].z);
        for (int k = 0; k < m; k++) {
            double s = 0;
            int c = 0;
            for (int q = std::max(0, k - 10); q <= std::min(m - 1, k + 10); q++) s += raw[q], c++;
            y[k] = s / c;
        }
        // Pinned: the ends on their pads (or the ground where a spur leaves the network), and under the water where it fords.
        auto padOf = [&](int idx, double& out) {
            if (idx < 0 || P.places[idx].pad <= 0) return false;
            out = P.padY[idx];
            return true;
        };
        double e;
        y[0] = padOf(tr.from, e) ? e : raw[0], pinned[0] = 1;
        y[m - 1] = padOf(tr.to, e) ? e : raw[m - 1], pinned[m - 1] = 1;
        std::vector<V3> fordAt;
        for (int k = 1; k + 1 < m; k++) {
            const double wl = waterLevel(pts[k].x, pts[k].z);
            if (wl > -1e8) y[k] = wl - 0.35, pinned[k] = 1, fordAt.push_back(pts[k]);
        }
        if (!fordAt.empty()) fords.push_back(fordAt);
        // No steeper than its grade from the point before, nor (where the line wanders) from the ones 4 and 10 m back as the crow flies.
        for (int pass = 0; pass < 8; pass++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < m; q++) {
                    const int k = dirn ? m - 1 - q : q;
                    if (pinned[k]) continue;
                    for (int back : {1, 4, 10}) {
                        const int prev = dirn ? k + back : k - back;
                        if (prev < 0 || prev >= m) continue;
                        const double dx = pts[k].x - pts[prev].x, dz = pts[k].z - pts[prev].z, run = std::sqrt(dx * dx + dz * dz);
                        y[k] = clampd(y[k], y[prev] - tr.maxGrade * run, y[prev] + tr.maxGrade * run);
                    }
                }
        const double halfW = tr.road ? 3.2 : 2.2, blend = tr.road ? 7 : 5;
        scanNear(pts, halfW + blend, [&](int id, double d, double k, double lat) {
            const double ex = d - halfW;
            if (edge[id] < ex) return;
            const int i0 = (int)clampd(std::floor(k), 0, m - 2);
            const double ty = y[i0] + (y[i0 + 1] - y[i0]) * clampd(k - i0, 0, 1);
            const double w = ex < 1.0 ? 1.0 : smoothstep(blend, 1.0, ex);
            const bool wet = waterY[id] > -1e8f;
            double nh = hgrid[id] + (ty - hgrid[id]) * w;
            if (wet) nh = std::min(nh, (double)waterY[id] - 0.3);
            hgrid[id] = (float)nh;
            edge[id] = (float)ex;
            roadLat[id] = (float)lat, roadAlong[id] = (float)k;
            if (ex < 0.5) {
                roadNear[id] = 1;
                if (wet) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL, flowX[id] = flowX[id] * 0.5f, flowZ[id] = flowZ[id] * 0.5f;
                else if (tr.road) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                else surf[id] = SURF_DIRT, paint[id] = PAINT_TRAIL;
            }
            if (ex < 3) keep[id] = 1;
        });
    }
    // The river spots at a crossing: fords.
    for (const std::vector<V3>& f : fords)
        for (River& rv : rivers)
            for (RiverSpot& q : rv.spots)
                for (const V3& c : f)
                    if ((q.p.x - c.x) * (q.p.x - c.x) + (q.p.z - c.z) * (q.p.z - c.z) < 144) {
                        q.cls = RIVER_FORD, q.depth = 0.35f, q.bed = BED_GRAVEL;
                        break;
                    }

    // ---- The ground ----------------------------------------------------------------------------------------------------------
    steps.next("real.ground");
    {
        const std::vector<float>& g = hgrid;
        parallelRows(hN, [&](int i) {
            for (int j = 0; j < hN; j++) {
                const int id = i * hN + j;
                if (paint[id] != PAINT_NONE) continue;
                const double x = X(i), z = X(j), s = wideSlope(g, i, j), e = t.realElevation(g[id]);
                const double n1 = fbmNoise(x * 0.011 + 3.0, z * 0.011 - 2.0, 3);
                switch (climate) {
                    case realterrain::CLIMATE_DESERT:
                        if (s > 0.5 || (s > 0.2 && n1 > 0.55) || (sp.ledgeStep > 0 && s > 0.25)) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
                        else if (s < 0.08 && n1 < 0.32) surf[id] = SURF_SAND, paint[id] = PAINT_SAND;
                        else surf[id] = SURF_DIRT, paint[id] = PAINT_DESERT;
                        break;
                    case realterrain::CLIMATE_ALPINE:
                        if (s > 0.8) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
                        else if (s > 0.45) surf[id] = SURF_SCREE, paint[id] = PAINT_SCREE;
                        else if (e > sp.snowLine) surf[id] = SURF_SNOW, paint[id] = PAINT_SNOW;
                        break;
                    default:
                        if (s > 0.85) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
                        else if (s > 0.55) surf[id] = SURF_SCREE, paint[id] = PAINT_SCREE;
                        else if (e > sp.snowLine && s < 0.75) surf[id] = SURF_SNOW, paint[id] = PAINT_SNOW;
                        break;
                }
            }
        });
    }

    // ---- The places' buildings ------------------------------------------------------------------------------------------------
    steps.next("real.places");
    const V3 brown(0.4, 0.3, 0.2);
    auto grounded = [&](V3 q) {
        q.y = height(q.x, q.z);
        return q;
    };
    auto rel = [&](const V3& o, double yaw, double ahead, double left) {
        return grounded(V3(o.x + std::cos(yaw) * ahead + std::sin(yaw) * left, 0, o.z - std::sin(yaw) * ahead + std::cos(yaw) * left));
    };
    using Set = std::vector<std::pair<std::string, double>>;
    auto build = [&](const char* id, const V3& at, double yaw, const V3& hs, Set set = {}, int variant = -1) {
        const size_t from = boxes.size();
        addBox(V3(at.x, at.y + hs.y, at.z), hs, yaw, 0, 0, brown);
        standIn(from, id, at, yaw, std::move(set));
        if (variant >= 0 && !models.empty() && models.back().id == id) models.back().variant = variant;
    };
    auto decor = [&](const char* id, const V3& at, double yaw) { placeModel(id, at, yaw); };
    auto cairn = [&](const V3& q) { fixtures.push_back({PROP_CAIRN, grounded(q), V3(), 0.7}); };
    auto table = [&](const V3& q, double yaw) {
        const V3 g = grounded(q);
        fixtures.push_back({PROP_TABLE, g, V3(), yaw});
        addBox(V3(g.x, g.y + 0.4, g.z), V3(0.95, 0.4, 0.8), yaw, 0, 0, brown);
        boxes.back().hidden = true;
    };
    auto mapboard = [&](const V3& q, double yaw) {
        Prop sg{PROP_MAPBOARD, grounded(q), V3(), yaw};
        sg.col[0] = 0.3f, sg.col[1] = 0.22f, sg.col[2] = 0.14f;
        fixtures.push_back(sg);
        const V3 sd2(std::sin(yaw), 0, std::cos(yaw));
        for (int k = -1; k <= 1; k += 2) poles.push_back({sg.a.x + sd2.x * 0.8 * k, sg.a.z + sd2.z * 0.8 * k, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
    };
    auto post = [&](const V3& q, double h, double r = 0.1) { poles.push_back({q.x, q.z, q.y - 0.3, q.y + h, r}); };
    for (const MapPlace& pl : P.places) {
        const V3 Pp = grounded(pl.p);
        const double yw = pl.yaw;
        switch (pl.kind) {
            case PK_RANGER_STATION:
                addStation(Pp, yw, pl.name.c_str());
                build("ranger_station", rel(Pp, yw, 0, 16), yw + M_PI / 2, V3(5.2, 1.8, 3.4));
                build("fuel_pump", rel(Pp, yw, 7, -14), yw - M_PI / 2, V3(3.0, 0.6, 1.8), {}, 1);
                build("woodpile", rel(Pp, yw, -10, 20), yw, V3(1.4, 0.5, 0.5));
                build("outhouse", rel(Pp, yw, 12, 21), yw + M_PI / 2, V3(0.6, 1.1, 0.6));
                table(rel(Pp, yw, -6, 12), yw);
                mapboard(rel(Pp, yw, 14, 8), yw);
                break;
            case PK_FUEL_STOP: {
                const V3 I = rel(Pp, yw, 0, 6);
                build("fuel_pump", I, yw, V3(2.6, 0.5, 0.6), {}, 1);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) post(rel(I, yw, a * 2.7, b * 1.4), 3.7, 0.1);
                cairn(rel(Pp, yw, -6, -6));
                break;
            }
            case PK_CABIN:
                build("cabin", rel(Pp, yw, -5, 0), yw, V3(2.7, 1.9, 2.2));
                build("woodpile", rel(Pp, yw, 0, 4.6), yw, V3(1.4, 0.5, 0.5));
                table(rel(Pp, yw, 5, 4), yw);
                break;
            case PK_OLD_CAMP:
                addStation(Pp, yw, pl.name.c_str());
                decor("tent", rel(Pp, yw, 2, 8), yw + 0.3);
                decor("campfire", rel(Pp, yw, -4, 8), 0);
                table(rel(Pp, yw, -6, -7), yw);
                break;
            case PK_LOOKOUT:
                cairn(rel(Pp, yw, 2, 2));
                table(rel(Pp, yw, -2, -2), yw + M_PI / 2);
                mapboard(rel(Pp, yw, -3, 3), yw + M_PI);
                break;
            case PK_SUMMIT: cairn(Pp); break;
            default: break;
        }
        for (int i = cellI(pl.p.x - 12); i <= cellI(pl.p.x + 12); i++)
            for (int j = cellI(pl.p.z - 12); j <= cellI(pl.p.z + 12); j++) keep[i * hN + j] = 1;
    }

    // ---- Growth: trees, bushes and rocks, 32 m cell by cell, by slope, height and climate ----------------------------------------
    steps.next("real.growth");
    {
        auto slopeAt = [&](double x, double z) { return wideSlope(hgrid, cellI(x), cellI(z)); };
        auto clear = [&](double x, double z) {
            const int id = cellI(x) * hN + cellI(z);
            return !keep[id] && waterY[id] < -1e8f && roadNear[id] == 0;
        };
        const double cellM = 32;
        const int cn = (int)std::ceil(2 * half / cellM);
        // (Low ground: the bottom third of the map's relief, where broadleaf grows.)
        double hlo = 1e9, hhi = -1e9;
        for (float v : P.h) hlo = std::min(hlo, (double)v), hhi = std::max(hhi, (double)v);
        const double lowLine = hlo + 0.33 * (hhi - hlo);
        const uint32_t seed = sp.seed * 2654435761u;
        for (int ci = 0; ci < cn; ci++)
            for (int cj = 0; cj < cn; cj++) {
                const double x0 = -half + ci * cellM, z0 = -half + cj * cellM;
                if (x0 > half - 140 || z0 > half - 140 || x0 + cellM < -half + 140 || z0 + cellM < -half + 140) continue;  // (not up the rim)
                const double mx = x0 + cellM / 2, mz = z0 + cellM / 2;
                const double wood = fbmNoise(mx * 0.004 + 7.0, mz * 0.004 - 3.0, 3);
                double steep = 0;
                for (int a = 0; a < 3; a++)
                    for (int b = 0; b < 3; b++) steep += slopeAt(x0 + 5 + a * 11, z0 + 5 + b * 11) > 0.45 ? 1.0 / 9 : 0;
                const double eMid = t.realElevation(height(mx, mz));
                const double treeOK = 1 - smoothstep(sp.treeLine - 150, sp.treeLine, eMid);
                double eT = 0, eB = 0, eR = 0;
                switch (climate) {
                    case realterrain::CLIMATE_DESERT: eT = 0.25 + 0.6 * smoothstep(0.5, 0.7, wood), eB = 2.2, eR = 0.25 + 2.5 * steep; break;
                    case realterrain::CLIMATE_ALPINE: eT = 2.5 * smoothstep(0.4, 0.6, wood), eB = 1.2, eR = 0.3 + 2.0 * steep; break;
                    default: eT = 0.3 + 3.2 * smoothstep(0.38, 0.6, wood), eB = 2.5, eR = 0.12 + 1.6 * steep; break;
                }
                eT *= sp.trees * treeOK, eB *= sp.bushes, eR *= sp.rocks;
                int k = 0;
                auto count = [&](double expect) { return (int)std::floor(expect + cellRoll(ci, cj, k++, seed)); };
                const int nt = count(eT), nb = count(eB), nr = count(eR);
                for (int q = 0; q < nt; q++) {
                    const double x = x0 + cellRoll(ci, cj, 100 + 3 * q, seed) * cellM, z = z0 + cellRoll(ci, cj, 101 + 3 * q, seed) * cellM;
                    if (!clear(x, z)) continue;
                    const double s = slopeAt(x, z);
                    const int sf = surf[cellI(x) * hN + cellI(z)];
                    if (s > 0.6 || sf == SURF_ROCK || sf == SURF_SNOW || sf == SURF_ICE || (sf == SURF_SAND && climate != realterrain::CLIMATE_DESERT)) continue;
                    const double u = cellRoll(ci, cj, 102 + 3 * q, seed), hgt = height(x, z);
                    int kind = 0;
                    double hMin = 8, hMax = 16;
                    if (climate == realterrain::CLIMATE_DESERT) kind = u < 0.75 ? 0 : 2, hMin = 3, hMax = 6.5;
                    else if (climate == realterrain::CLIMATE_ALPINE) kind = u < 0.92 ? 0 : 2, hMin = 6, hMax = 13;
                    else kind = u < 0.06 ? 2 : (hgt < lowLine ? (u < 0.7 ? 1 : 0) : (u < 0.15 ? 1 : 0));
                    addTree(x, z, hMin + (hMax - hMin) * cellRoll(ci, cj, 900 + q, seed), kind, (float)(0.8 + 0.4 * cellRoll(ci, cj, 950 + q, seed)));
                }
                const int habitat = climate == realterrain::CLIMATE_DESERT ? HAB_DESERT : climate == realterrain::CLIMATE_ALPINE ? HAB_UPLAND : wood > 0.5 ? HAB_FOREST : HAB_MEADOW;
                for (int q = 0; q < nb; q++) {
                    const double x = x0 + cellRoll(ci, cj, 2000 + 3 * q, seed) * cellM, z = z0 + cellRoll(ci, cj, 2001 + 3 * q, seed) * cellM;
                    if (!clear(x, z) || slopeAt(x, z) > 0.7) continue;
                    const int sf = surf[cellI(x) * hN + cellI(z)];
                    if (sf == SURF_SNOW || sf == SURF_ICE) continue;
                    const int hab = climate == realterrain::CLIMATE_DESERT && sf == SURF_ROCK ? HAB_CANYON : habitat;
                    bushes.push_back(growBush(V3(x, height(x, z), z), hab, wood > 0.55, cellRoll(ci, cj, 2002 + 3 * q, seed), (float)(0.8 + 0.4 * cellRoll(ci, cj, 2900 + q, seed))));
                }
                for (int q = 0; q < nr; q++) {
                    const double x = x0 + cellRoll(ci, cj, 4000 + 4 * q, seed) * cellM, z = z0 + cellRoll(ci, cj, 4001 + 4 * q, seed) * cellM;
                    if (!clear(x, z)) continue;
                    const double s = slopeAt(x, z);
                    if (s > 1.0) continue;
                    const double u = cellRoll(ci, cj, 4002 + 4 * q, seed), v = cellRoll(ci, cj, 4003 + 4 * q, seed);
                    const double big = climate == realterrain::CLIMATE_DESERT ? 2.2 : 1.6;
                    const double rr = 0.45 + u * u * big * (0.6 + s), flat = 0.5 + 0.4 * v;
                    addRock(x, z, V3(rr, rr * flat, rr * (0.75 + 0.3 * v)), rr * flat * (0.5 + 0.5 * u), hmix((uint32_t)(ci * 7919 + cj * 104729 + q) ^ seed),
                            (float)(0.85 + 0.3 * v));
                }
            }
    }

    // ---- Start ---------------------------------------------------------------------------------------------------------------
    steps.next("real.start");
    if (!P.places.empty() && P.places[0].pad > 0) {
        const MapPlace& m = P.places[0];
        const V3 c = grounded(m.p);
        spawn = rel(c, m.yaw, -12, -12);
        spawnYaw = m.yaw;
        parking = {rel(c, m.yaw, -12, -18), rel(c, m.yaw, -12, -24), rel(c, m.yaw, -18, -18)};
        parkingYaw = m.yaw;
    } else {
        spawn = V3(0, height(0, 0), 0), spawnYaw = 0;
    }
}
