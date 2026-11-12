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

    // (Its rocks from the scans it names: Moab's sandstone-coloured ones, no grey granite. Only while it's built.)
    rockScanFilter = sp.rockScans;
    struct ClearFilter {
        World* w;
        ~ClearFilter() { w->rockScanFilter.clear(); }
    } clearFilter{this};
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
        // (A band of rows at a time, read from a copy of the band and the 3 rows either side as they were: not a second whole grid,
        // which is 1 GB at 16 km. The same arithmetic as from a whole copy.)
        const double step = sp.ledgeStep;
        const int band = 512;
        std::vector<float> tail;  // (the 3 rows before the band, as they were)
        for (int r0 = 0; r0 < hN; r0 += band) {
            const int r1 = std::min(hN, r0 + band), lo = std::max(0, r0 - 3), hi = std::min(hN, r1 + 3);
            std::vector<float> src((size_t)(hi - lo) * hN);
            std::copy(tail.begin(), tail.end(), src.begin());
            std::copy(hgrid.begin() + (size_t)r0 * hN, hgrid.begin() + (size_t)hi * hN, src.begin() + (size_t)(r0 - lo) * hN);
            auto S = [&](int i, int j) { return src[(size_t)(i - lo) * hN + j]; };
            parallelRows(r1 - r0, [&](int ii) {
                const int i = r0 + ii;
                for (int j = 0; j < hN; j++) {
                    const int a = std::max(0, i - 3), b = std::min(hN - 1, i + 3), c = std::max(0, j - 3), d = std::min(hN - 1, j + 3);
                    const double hx = (S(b, j) - S(a, j)) / std::max(1, b - a), hz = (S(i, d) - S(i, c)) / std::max(1, d - c);
                    const double s = std::sqrt(hx * hx + hz * hz);
                    double w = smoothstep(0.18, 0.45, s) * sp.ledgeStrength * (1 - smoothstep(1.4, 2.2, s)) * (t.rimAt(X(i), X(j)) > 0 ? 0 : 1);
                    if (w <= 0) continue;
                    const double x = X(i), z = X(j), h = S(i, j);
                    double py, pw = 0;
                    if (!t.patches.empty() && t.patchAt(x, z, py, pw) && pw >= 1) continue;  // (lidar: its own ledges, sharpened below)
                    w *= 1 - pw;
                    const double jit = step * 0.4 * (fbmNoise(x * 0.015 + 7.0, z * 0.015 - 3.0, 2) - 0.5);
                    const double hs = (h + jit) / step, q = std::floor(hs), f = hs - q;
                    const double stair = (q + smoothstep(0.7, 1.0, f)) * step - jit;
                    hgrid[i * hN + j] = (float)(h + w * (stair - h));
                }
            });
            tail.assign(src.begin() + (size_t)(std::max(lo, r1 - 3) - lo) * hN, src.begin() + (size_t)(r1 - lo) * hN);
        }
    }
    // ---- Ledges in the lidar: its 1 m surface rounds a sandstone step over two or three cells; where a step stands out (steeper
    // than the ground round it), its riser is pulled toward a stair between the tread below and the tread above -------------------
    if (sp.ledgeSharpen > 0 && !t.patches.empty()) {
        steps.next("real.sharpen");
        const int band = 512, M = 4;
        std::vector<float> tail;
        for (int r0 = 0; r0 < hN; r0 += band) {
            const int r1 = std::min(hN, r0 + band), lo = std::max(0, r0 - M), hi = std::min(hN, r1 + M);
            std::vector<float> src((size_t)(hi - lo) * hN);
            std::copy(tail.begin(), tail.end(), src.begin());
            std::copy(hgrid.begin() + (size_t)r0 * hN, hgrid.begin() + (size_t)hi * hN, src.begin() + (size_t)(r0 - lo) * hN);
            auto S = [&](int i, int j) {
                i = std::min(hi - 1, std::max(lo, i)), j = std::min(hN - 1, std::max(0, j));
                return (double)src[(size_t)(i - lo) * hN + j];
            };
            parallelRows(r1 - r0, [&](int ii) {
                const int i = r0 + ii;
                for (int j = 0; j < hN; j++) {
                    const double x = X(i), z = X(j);
                    double py, pw = 0;
                    if (!t.patchAt(x, z, py, pw) || pw <= 0) continue;
                    const double gx = S(i + 1, j) - S(i - 1, j), gz = S(i, j + 1) - S(i, j - 1);
                    const double local = 0.5 * std::sqrt(gx * gx + gz * gz);
                    const double wx = S(i + M, j) - S(i - M, j), wz = S(i, j + M) - S(i, j - M);
                    const double wide = std::sqrt(wx * wx + wz * wz) / (2 * M);
                    const double stepness = smoothstep(1.25, 2.2, local / (wide + 0.04)) * smoothstep(0.3, 0.7, local) * (1 - smoothstep(2.5, 4.0, local));
                    if (stepness <= 0) continue;
                    double a = 1e9, b = -1e9;
                    for (int di = -2; di <= 2; di++)
                        for (int dj = -2; dj <= 2; dj++) a = std::min(a, S(i + di, j + dj)), b = std::max(b, S(i + di, j + dj));
                    if (b - a < 0.3) continue;
                    const double h = S(i, j), f = (h - a) / (b - a);
                    const double target = a + (b - a) * smoothstep(0.3, 0.7, f);
                    hgrid[i * hN + j] = (float)(h + sp.ledgeSharpen * pw * stepness * (target - h));
                }
            });
            tail.assign(src.begin() + (size_t)(std::max(lo, r1 - M) - lo) * hN, src.begin() + (size_t)(r1 - lo) * hN);
        }
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
            if (pr.halfWidth > 0) hw[k] = pr.halfWidth, dep[k] = pr.depth;  // (a sidecar's big river: as wide and deep as it says)
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
        rec.name = !pr.name.empty() ? pr.name : pr.wet ? sp.name + " RIVER " + std::to_string(r + 1) : "";
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

    // ---- Bridges (a sidecar's): a deck from bank to bank at the banks' height, on piers; the roads are laid onto it below ---------
    struct Deck {
        V3 a, b;  // its ends (y: the deck's top)
        double hw;
    };
    std::vector<Deck> decks;
    for (const realterrain::PlanBridge& pb : P.bridges) {
        Deck d{V3(pb.a.x, height(pb.a.x, pb.a.z), pb.a.z), V3(pb.b.x, height(pb.b.x, pb.b.z), pb.b.z), pb.halfWidth};
        const double dx = d.b.x - d.a.x, dz = d.b.z - d.a.z, L = std::sqrt(dx * dx + dz * dz);
        if (L < 4) continue;
        const double yaw = std::atan2(-dz, dx);
        const V3 grey(0.62, 0.6, 0.57), dark(0.45, 0.44, 0.42);
        const int segs = std::max(1, (int)std::ceil(L / 12));
        for (int k = 0; k < segs; k++) {
            const double u0 = (double)k / segs, u1 = (double)(k + 1) / segs, um = 0.5 * (u0 + u1);
            const V3 c = d.a + (d.b - d.a) * um;
            const double pitch = std::atan2(d.b.y - d.a.y, L);
            addBox(V3(c.x, c.y - 0.45, c.z), V3(0.5 * L / segs + 0.05, 0.45, d.hw), yaw, pitch, 0, grey);
            for (int side = -1; side <= 1; side += 2) {  // (the rails)
                const V3 r = c + V3(-dz / L, 0, dx / L) * (side * (d.hw - 0.2));
                addBox(V3(r.x, c.y + 0.45, r.z), V3(0.5 * L / segs, 0.45, 0.18), yaw, pitch, 0, dark);
            }
            if (k > 0 && k % 3 == 0) {  // (a pier every 36 m, down to the bed)
                const V3 q = d.a + (d.b - d.a) * u0;
                const double bed = height(q.x, q.z);
                if (q.y - 1 > bed) addBox(V3(q.x, 0.5 * (q.y - 0.9 + bed - 1), q.z), V3(1.2, 0.5 * (q.y - 0.9 - bed + 1), d.hw * 0.7), yaw, 0, 0, dark);
            }
        }
        decks.push_back(d);
    }
    // The deck's top over (x, z) (false: not over a deck, `margin` m in from its ends and sides).
    auto deckAt = [&](double x, double z, double margin, double& y) {
        for (const Deck& d : decks) {
            const double dx = d.b.x - d.a.x, dz = d.b.z - d.a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
            const double u = ((x - d.a.x) * dx + (z - d.a.z) * dz) / L2, v = std::fabs((dx * (z - d.a.z) - dz * (x - d.a.x)) / L);
            if (u * L < margin || (1 - u) * L < margin || v > d.hw - margin) continue;
            y = d.a.y + (d.b.y - d.a.y) * u;
            return true;
        }
        return false;
    };

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
        std::vector<V3> pts = realterrain::smoothLine(tr.pts, 1.0);
        // (Onto a bridge: along its middle. The plan's cells a deck crosses are 12 m; its line can be off the deck by half a cell.)
        for (V3& q : pts)
            for (const Deck& d : decks) {
                const double dx = d.b.x - d.a.x, dz = d.b.z - d.a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
                const double u = ((q.x - d.a.x) * dx + (q.z - d.a.z) * dz) / L2, v = (dx * (q.z - d.a.z) - dz * (q.x - d.a.x)) / L;
                if (u < -12 / L || u > 1 + 12 / L || std::fabs(v) > d.hw + 9) continue;
                const double pull = smoothstep(-12 / L, 0, u) * (1 - smoothstep(1, 1 + 12 / L, u));  // (eased on over 12 m)
                q.x -= -dz / L * v * pull, q.z -= dx / L * v * pull;
            }
        const int m = (int)pts.size();
        if (m < 2) continue;
        std::vector<double> raw(m), y(m);
        std::vector<uint8_t> pinned(m, 0);
        for (int k = 0; k < m; k++) raw[k] = height(pts[k].x, pts[k].z);
        // (A named hard trail follows the rock's own steps closer: its profile averaged over +-3 m, not +-10.)
        const int avg = tr.rating >= 3 ? 3 : tr.rating == 2 ? 6 : 10;
        for (int k = 0; k < m; k++) {
            double s = 0;
            int c = 0;
            for (int q = std::max(0, k - avg); q <= std::min(m - 1, k + avg); q++) s += raw[q], c++;
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
            double dy;
            if (deckAt(pts[k].x, pts[k].z, -1.0, dy)) {  // (over a bridge: on its deck)
                y[k] = dy, pinned[k] = 1;
                continue;
            }
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
        // A named hard trail is a little wider (it runs over open rock, not a cut track), and on the slickrock isn't painted a dirt
        // track: the ground under it stays bare rock (cairns mark the way instead, below).
        const bool bare = tr.rating >= 2 || tr.bypass;
        const double halfW = tr.road ? 3.2 : bare ? 3.0 : 2.2, blend = tr.road ? 7 : bare ? 6 : 5;
        const double keepRough = 0;
        // (A hard trail over the lidar, where rock rises beside it: only the tread a truck needs is levelled, about 4.4 m, eased into
        // the survey over 3 m, so a slot keeps its walls and its deep V; elsewhere the full cut.)
        const bool narrowOnLidar = !tr.road && (tr.rating >= 3 || tr.bypass) && !t.patches.empty();
        scanNear(pts, halfW + blend, [&](int id, double d, double k, double lat) {
            const int i0 = (int)clampd(std::floor(k), 0, m - 2);
            const double ty = y[i0] + (y[i0 + 1] - y[i0]) * clampd(k - i0, 0, 1);
            double hw = halfW, bl = blend;
            // (only where the rock rises beside the tread, a wall or a slot's side; where it falls away, a crest or a fin's top, the
            // full width stays, so a truck has room on it)
            if (narrowOnLidar && hgrid[id] > ty + 0.3) {
                double py, pw = 0;
                if (t.patchAt(X(id / hN), X(id % hN), py, pw) && pw > 0) hw += (1.2 - hw) * pw, bl += (3.0 - bl) * pw;
                if (d > hw + bl) return;
            }
            const double ex = d - hw;
            if (edge[id] < ex) return;
            double under;
            if (!decks.empty() && deckAt(X(id / hN), X(id % hN), -1.5, under) && waterY[id] > -1e8f) return;  // (the river under a deck stays)
            const double w = (ex < 1.0 ? 1.0 : smoothstep(std::max(bl, 1.01), 1.0, ex)) * (1 - keepRough);
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
                else if (!bare) surf[id] = SURF_DIRT, paint[id] = PAINT_TRAIL;
            }
            if (ex < 3) keep[id] = 1;
        });
    }
    // The named trails on the M map, and cairns along the bare-rock ones every 50 m (each side of the tread, by turns).
    for (const realterrain::PlanTrail& tr : P.trails) {
        if (tr.rating < 0) continue;
        MarkedTrail mt;
        mt.name = tr.name, mt.rating = tr.rating;
        const std::vector<V3> line = realterrain::smoothLine(tr.pts, 8.0);
        for (const V3& q : line) mt.pts.push_back(V3(q.x, height(q.x, q.z), q.z));
        trails.push_back(mt);
        if (tr.rating < 2) continue;
        for (size_t k = 6; k + 1 < line.size(); k += 6) {
            const V3 d = norm(V3(line[k + 1].x - line[k - 1].x, 0, line[k + 1].z - line[k - 1].z));
            const double side = (k / 6) % 2 ? 3.4 : -3.4;
            const V3 q(line[k].x - d.z * side, 0, line[k].z + d.x * side);
            if (waterLevel(q.x, q.z) > -1e8) continue;
            fixtures.push_back({PROP_CAIRN, V3(q.x, height(q.x, q.z), q.z), V3(), 0.5});
        }
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

    // ---- The named obstacles (a sidecar's `obstacle` lines): the tread reshaped through each and scanned rocks laid on it ----------
    // Along the trail's line through the point: the profile as laid, then the obstacle's own (a pitch, ledges, a bowl...) from its start,
    // and past its end a run-out back down (or up) to the trail at no more than 30%. Across: its tread `width` either side, tipped by the
    // camber, walls raised beyond it, eased into the land round it. The ledges' lips are lines of flattened scanned rocks laid across the
    // tread with their tops at the upper tread (the 1 m grid can't make a square edge; the rocks are it). Bare rock, nothing grows on it.
    if (!P.obstacles.empty()) steps.next("real.obstacles");
    for (const realterrain::PlanObstacle& po : P.obstacles) {
        realterrain::SpecObstacle o = po.spec;
        // On lidar (a patch's full weight over its stretch) the land already has the obstacle's real form: its walls, fins, pitch
        // and tilt are the measured ones, not authored. Only what a 1 m grid can't hold is added: the ledges' faces (as slabs, on
        // the land's own slope), and a crack or pothole the survey smooths.
        bool onLidar = !t.patches.empty();
        double minW = 1;
        int seen = 0;
        if (onLidar) {
            const std::vector<V3> Lp = realterrain::smoothLine(P.trails[po.trail].pts, 1.0);
            double acc = 0;
            for (size_t k = 0; k < Lp.size(); k++) {
                acc += k ? std::hypot(Lp[k].x - Lp[k - 1].x, Lp[k].z - Lp[k - 1].z) : 0;
                if (acc < po.from - 6 || acc > po.to + 6) continue;
                double py, pw = 0;
                seen++;
                if (!t.patchAt(Lp[k].x, Lp[k].z, py, pw)) pw = 0;
                minW = std::min(minW, pw);
            }
            onLidar = seen > 0 && minW >= 0.98;
        }
        if (std::getenv("CZ_DEBUG_OBSTACLES"))
            std::fprintf(stderr, "obstacle %s: %s (%d points, least weight %.2f, from %.0f to %.0f, %zu patches)\n", o.name.c_str(), onLidar ? "on lidar" : "authored", seen, minW, po.from, po.to, t.patches.size());
        // (a notch on lidar keeps a narrow tread, so its walls stay the survey's; a fin or a climb keeps its width: a truck must
        // stay on its crest)
        if (onLidar) o.width = o.walls > 0 ? std::min(o.width, 2.2) : o.width, o.walls = 0, o.climbLen = 0, o.climbGrade = 0, o.camber = 0;
        const std::vector<V3> L = realterrain::smoothLine(P.trails[po.trail].pts, 1.0);
        const int m = (int)L.size();
        if (m < 4) continue;
        std::vector<double> at(m, 0);
        for (int k = 1; k < m; k++) at[k] = at[k - 1] + std::hypot(L[k].x - L[k - 1].x, L[k].z - L[k - 1].z);
        auto idxAt = [&](double sAt) {
            int k = (int)(std::lower_bound(at.begin(), at.end(), sAt) - at.begin());
            return std::max(1, std::min(m - 1, k));
        };
        auto pointAlong = [&](double sAt) {
            const int k = idxAt(sAt);
            const double u = clampd((sAt - at[k - 1]) / std::max(1e-9, at[k] - at[k - 1]), 0, 1);
            V3 q = L[k - 1] + (L[k] - L[k - 1]) * u;
            q.y = 0;
            return q;
        };
        auto dirAlong = [&](double sAt) {
            const int a = idxAt(sAt - 2), b = idxAt(sAt + 2);
            return norm(V3(L[b].x - L[a - 1].x, 0, L[b].z - L[a - 1].z));
        };
        auto leftAlong = [&](double sAt) {
            const V3 d = dirAlong(sAt);
            return V3(-d.z, 0, d.x);
        };
        const uint32_t oseed = hmix((uint32_t)std::lround(po.at.x * 7 + 1e5) ^ hmix((uint32_t)std::lround(po.at.z * 13 + 1e5)));
        auto roll01 = [&](int k) { return cellRoll(k, 77, 5, oseed); };
        // The profile, 0.5 m a sample: the obstacle's own rise (its pitch and ledges) from its foot, dug a little into the trail, and a
        // run-in and a run-out joining it to the trail at no more than 30%.
        const double s0 = po.from, s1 = po.to;
        const double W = o.width;
        const double ds = 0.5, sA = std::max(0.0, s0 - 80);
        std::vector<double> base, prof;
        for (double sAt = sA; sAt <= std::min(at.back(), s1 + 160); sAt += ds) {
            const V3 q = pointAlong(sAt);
            base.push_back(height(q.x, q.z));
        }
        const int nS = (int)base.size();
        const int i0 = (int)std::lround((s0 - sA) / ds), i1 = std::min(nS - 1, (int)std::lround((s1 - sA) / ds));
        if (nS < 4 || i1 <= i0) continue;
        prof = base;
        const double Lo = std::max(1.0, s1 - s0), natural = base[i1] - base[i0];
        const double pitch = o.climbLen > 0 ? o.climbGrade * Lo : natural;
        const double stepRise = o.steps * o.rise;
        const double stepSign = o.climbLen > 0 ? (o.climbGrade < 0 ? -1 : 1) : onLidar && natural < 0 ? -1 : 1;
        // (the ledges take their share of the pitch; with no pitch the treads follow the land, but never against the steps; on lidar
        // the ledges are cut into the land's own slope, up or down)
        const double treadRise = o.climbLen > 0 || onLidar ? pitch - stepSign * std::min(std::fabs(pitch), stepRise) : std::max(0.0, natural - stepRise);
        // (m along each step's face, and its rise: not evenly spaced nor all alike, as a real run of ledges isn't; the rises still add
        // up to what's authored, none more than about a fifth over it)
        std::vector<double> lips, riseK;
        double riseSum = 0;
        for (int k = 0; k < o.steps; k++) {
            lips.push_back(s0 + Lo * (k + 0.5 + 0.32 * (roll01(500 + k) - 0.5)) / o.steps);
            riseK.push_back(0.82 + 0.36 * roll01(600 + k)), riseSum += riseK.back();
        }
        for (double& r : riseK) r *= o.rise * o.steps / std::max(1e-9, riseSum);
        auto rel = [&](double sAt) {
            double y = treadRise * (sAt - s0) / Lo;
            for (size_t k = 0; k < lips.size(); k++) y += stepSign * riseK[k] * smoothstep(lips[k] - 0.45, lips[k] + 0.45, sAt);
            return y;
        };
        // (its foot dug at most 1 m into the trail: the rest stands up out of it, a dome or a fin, the far side run out at 30%)
        const double excess = rel(s1) - natural, lift = excess > 0 ? -std::min(1.0, 0.25 * excess) : std::min(1.0, -0.25 * excess);
        for (int i = i0; i <= i1; i++) prof[i] = base[i0] + lift + rel(sA + i * ds);
        int iEnd = i1, iBeg = i0;
        for (int i = i1 + 1; i < nS; i++) {  // (the run-out)
            const double gap = prof[i - 1] - base[i];
            if (std::fabs(gap) < 0.05) break;
            prof[i] = base[i] + (gap > 0 ? std::max(0.0, gap - 0.30 * ds) : std::min(0.0, gap + 0.30 * ds));
            iEnd = i;
        }
        for (int i = i0 - 1; i >= 0; i--) {  // (the run-in)
            const double gap = prof[i + 1] - base[i];
            if (std::fabs(gap) < 0.05) break;
            prof[i] = base[i] + (gap > 0 ? std::max(0.0, gap - 0.30 * ds) : std::min(0.0, gap + 0.30 * ds));
            iBeg = i;
        }
        auto profAt = [&](double sAt) {
            const double f = clampd((sAt - sA) / ds, iBeg, iEnd);
            const int i = std::min(std::max(iBeg, iEnd - 1), std::max(iBeg, (int)std::floor(f)));
            const double u = f - i;
            return i + 1 <= iEnd ? prof[i] + (prof[i + 1] - prof[i]) * u : prof[i];
        };
        const double sBeg = sA + iBeg * ds;
        const double sEnd = sA + iEnd * ds;
        const double tanC = std::tan(o.camber * M_PI / 180);
        auto baseAt = [&](double sAt) { return base[std::min(nS - 1, std::max(0, (int)std::lround((sAt - sA) / ds)))]; };
        double maxDev = 0;
        for (int i = iBeg; i <= iEnd; i++) maxDev = std::max(maxDev, std::fabs(prof[i] - base[i]));
        const double reach = W + 4 + std::max(0.0, 1.6 * maxDev - 3) + (o.walls != 0 ? 6 + 5.0 * std::fabs(o.walls) : 0);
        const double bowlR = 0.5 * o.bowlSize;
        const double ext = o.walls > 2.5 ? 1.5 * o.walls + 3 : 0.0;  // (a tall wall's rock runs on past the profile's ends)
        std::vector<V3> sub;
        for (int k = idxAt(sBeg - 4 - ext) - 1; k <= idxAt(sEnd + 4 + ext); k++) sub.push_back(L[std::max(0, std::min(m - 1, k))]);
        std::vector<double> subAt(sub.size(), at[std::max(0, idxAt(sBeg - 4 - ext) - 1)]);
        for (size_t k = 1; k < sub.size(); k++) subAt[k] = subAt[k - 1] + std::hypot(sub[k].x - sub[k - 1].x, sub[k].z - sub[k - 1].z);
        scanNear(sub, reach, [&](int id, double d, double kk, double lat) {
            const int k0 = (int)clampd(std::floor(kk), 0, (double)sub.size() - 2);
            const double sAt = subAt[k0] + (subAt[k0 + 1] - subAt[k0]) * clampd(kk - k0, 0, 1);
            if (sAt < sBeg - 3 - ext || sAt > sEnd + 3 + ext) return;
            const double a = std::fabs(lat);
            // (a wall rises over a run that grows with its height: a 2 m wall over 1.7 m, a 7 m one over about 5 m, a steep V)
            const double wallRun = std::max(1.7, 0.7 * std::fabs(o.walls));
            // (a tall wall is the edge of a mass of rock the notch is cut through: it runs on along the line and out to the sides)
            const double wallOut = o.walls > 2.5 ? 1.5 * o.walls : 0.0;
            const double wallH = o.walls == 0 ? 0.0 : o.walls * smoothstep(W + 0.3, W + 0.3 + wallRun, a) * smoothstep(s0 - 3 - wallOut, s0 + 2, sAt) * (1 - smoothstep(s1 - 2, s1 + 3 + wallOut, sAt));
            const double along = smoothstep(sBeg - 3, sBeg, sAt) * (1 - smoothstep(sEnd, sEnd + 3, sAt));
            // (the walls: added on, so their rock runs on smoothly past the profile's ends, where the tread is left as it is)
            const double side = o.walls > 2.5 ? smoothstep(W + 0.3 + wallRun + 4.6 * o.walls, W + 0.3 + wallRun + 0.6 * o.walls, a) : 0.0;  // (a rounded top)
            if (along <= 0) {
                if (a < W + 0.3 || wallH <= 0 || side <= 0) return;
                hgrid[id] = (float)(hgrid[id] + wallH * side);
                surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK, keep[id] = 1;
                return;
            }
            double target = profAt(sAt) + tanC * lat + (o.walls > 2.5 ? 0.0 : wallH);
            if (o.bowlDepth > 0) {  // (a pothole across the line, round, its lips steep)
                const double r = std::hypot(sAt - 0.5 * (s0 + s1), lat) / std::max(1.0, bowlR);
                if (r < 1) target -= o.bowlDepth * (1 - r * r);
            }
            if (o.crackDepth > 0 && sAt > s0 && sAt < s1) {  // (a crack: along the right wheel track, or diagonally across)
                const double off = o.crackAcross ? (sAt - 0.5 * (s0 + s1)) * 0.8 - lat * 0.6 : lat - 1.9;  // (along: just left of the left wheels)
                const double c = std::fabs(off) / std::max(0.3, 0.5 * o.crackWidth + 0.5);
                if (c < 1) target -= o.crackDepth * (1 - c * c);
            }
            // (eased out over 3 m, or more where it stands well above or below the land: a slope, not a wall, to its sides)
            const double dev = std::fabs(profAt(sAt) - baseAt(sAt));
            const double inner = o.walls != 0 ? W + 0.3 + wallRun + (o.walls > 2.5 ? 0.6 * o.walls : 0.0) : W + 0.5, outer = inner + (onLidar ? std::max(2.5, 1.2 * dev) : std::max({o.walls != 0 ? 4.0 : o.steps > 0 ? 5.0 : 3.0, 1.6 * dev, (o.walls > 2.5 ? 4.0 : 1.2) * std::fabs(o.walls)}));
            const double w = along * (a <= inner ? 1.0 : smoothstep(outer, inner, a));
            if (w <= 0 && side <= 0) return;
            hgrid[id] = (float)(hgrid[id] + (target - hgrid[id]) * w + (o.walls > 2.5 ? wallH * side : 0.0));
            if (a < W + 1.5) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
            keep[id] = 1;
        });
        int rk = 0;
        // A ledge's face: the riser between the treads is a slab of the rock itself (code-made, drawn as the sandstone round it),
        // its top level with the tread above and its lip a little rounded, its face near vertical, buried below the tread under it.
        // Pieces of about 2 m across the tread, each convex, so it follows the line and the camber. (The 1 m grid can only ramp a
        // step over a cell; the slab is the square edge a tire meets.)
        for (size_t li = 0; li < lips.size(); li++) {
            const double lp = lips[li];
            const double sUp = stepSign > 0 ? lp + 0.45 : lp - 0.45;    // (the tread above starts here)
            const double sFace = stepSign > 0 ? lp - 0.05 : lp + 0.05;  // (the face)
            const double upper = profAt(stepSign > 0 ? lp + 1.0 : lp - 1.0), lower = profAt(stepSign > 0 ? lp - 1.0 : lp + 1.0);
            const double back = stepSign > 0 ? 1.4 : -1.4;
            // (The ledge runs on past the tread, as a bed of the rock does, dying out into the slope: each piece's top and foot are the
            // land as now built above and below the lip, and a piece where the step has faded under 0.15 m isn't laid.)
            // (On lidar, where the land is the survey's, a ledge is a shorter, off-centre lip that dies into the slope at its ends,
            // well rounded over: it reads as the rock's own bed, not a stair.)
            // (`slabs`: thick tabular beds, the full tread and SPREAD m either side of it on the land as it is, undercut, a little
            // ragged in length: they read as a stack of ledges from afar)
            const bool tab = o.slabOverhang > 0;
            const double span = tab ? W + 0.6 + o.slabSpread * (0.7 + 0.6 * roll01(700 + (int)li))
                                    : onLidar ? W * (0.6 + 0.4 * roll01(700 + (int)li)) : W + 0.6 + (o.walls > 0 ? 0.0 : 3.0);
            const double off = !tab && onLidar ? 0.5 * W * (roll01(800 + (int)li) - 0.5) : tab ? 0.8 * (roll01(800 + (int)li) - 0.5) : 0.0;
            const int pieces = std::max(onLidar ? 2 : 3, (int)std::ceil(2 * span / 1.3));
            for (int pc = 0; pc < pieces; pc++) {
                // (each piece set back or forward a little and a little lower or higher: a broken, weathered lip, not a kerb)
                const double v0 = off - span + 2 * span * pc / pieces - 0.05, v1 = off - span + 2 * span * (pc + 1) / pieces + 0.05;
                const double setback = 0.26 * (roll01(rk++) - 0.5), drop = -0.07 * roll01(rk++);
                const double sg = stepSign > 0 ? 1.0 : -1.0;
                std::vector<V3> pts;
                bool weak = false;
                // (a tabular bed off the tread is broken into blocks: now and then a gap, each block set back its own way and its top
                // flat, not a ribbon following every bump of the land)
                const bool offT = tab && std::fabs(0.5 * (v0 + v1)) > W + 0.6;
                if (offT && roll01(900 + (int)li * 31 + pc) < 0.22) continue;
                if (offT) {  // (a bed doesn't climb a steep side: where the land across the piece is steeper than ~27 degrees, none)
                    auto gAt = [&](double sAt, double v) {
                        const V3 q = pointAlong(sAt) + leftAlong(sAt) * v;
                        return height(q.x, q.z);
                    };
                    const double vm = 0.5 * (v0 + v1);
                    if (std::fabs(gAt(sFace - sg * 0.7, v1) - gAt(sFace - sg * 0.7, v0)) > 0.5 * (v1 - v0)) continue;
                    if (std::fabs(gAt(sUp + sg * 0.6, vm) - gAt(sFace - sg * 0.7, vm)) > 2.5 * riseK[li]) continue;  // (nor up a steep face)
                }
                const double blockBack = offT ? 0.6 * (roll01(950 + (int)li * 31 + pc) - 0.5) : 0.0;
                std::vector<double> tops, adj;
                for (int e = 0; e <= 3; e++) {
                    const double v = v0 + (v1 - v0) * e / 3.0;
                    auto at = [&](double sAt, double y) {
                        const V3 q = pointAlong(sAt) + leftAlong(sAt) * v;
                        return V3(q.x, y, q.z);
                    };
                    auto ground = [&](double sAt) {
                        const V3 q = at(sAt, 0);
                        return height(q.x, q.z);
                    };
                    const double inTread = std::fabs(v) <= W + 0.6 ? 1.0 : 0.0;
                    const double lowY = inTread ? lower + tanC * v : ground(sFace - sg * 0.7);
                    // (a tabular bed off the tread stands proud of the land, thicker than the tread's own step: off the line it can be)
                    const double upperY = inTread ? upper + tanC * v : tab ? std::max(ground(sUp + sg * 0.6), lowY + 1.6 * riseK[li]) : ground(sUp + sg * 0.6);
                    if (upperY - lowY < 0.15) weak = true;
                    const double jit = 0.08 * (roll01(rk++) - 0.5) + setback;
                    const double taper = tab ? 1 - smoothstep(span - 1.2, span, std::fabs(v - off)) : onLidar ? 1 - smoothstep(0.45 * span, span, std::fabs(v - off)) : 1.0;
                    const double topY = lowY + (upperY + 0.02 + drop + 0.05 * (roll01(rk++) - 0.5) - lowY) * taper, botY = lowY - 0.45;
                    const double rise = std::max(0.1, topY - lowY);
                    // (a block's ends rounded down and back; its lip's rounding and thickness a little different all along it)
                    const bool endE = offT && (e == 0 || e == 3);
                    const double fs = sFace + sg * (jit + blockBack - (endE ? 0.15 + 0.15 * roll01(rk++) : 0.0));
                    tops.push_back(topY);
                    adj.push_back(-(endE ? 0.12 + 0.15 * roll01(rk++) : 0.0) + (tab ? 0.08 * (roll01(rk++) - 0.5) : 0.0));
                    const double round = tab ? 0.7 + 0.9 * roll01(rk++) : onLidar ? 1.6 : 1.0;  // (on lidar the lip rounds over further back)
                    const double under = tab ? o.slabOverhang : 0.06;           // (how far the face is cut back under the lip)
                    pts.push_back(at(fs - sg * std::max(0.04, under), botY));  // (the face's foot, buried)
                    pts.push_back(at(fs - sg * under, lowY + 0.25 * rise));    // (its lower face, undercut)
                    pts.push_back(at(fs - sg * 0.5 * under, lowY + 0.6 * rise));
                    pts.push_back(at(fs + sg * 0.05 * round, topY - std::min(0.6, 0.22 * round) * rise));  // (the lip, rounded over)
                    pts.push_back(at(fs + sg * 0.16 * round, topY - 0.06 * round * rise));
                    pts.push_back(at(fs + sg * 0.34 * round, topY));
                    pts.push_back(at(sUp + back, std::min(topY, (inTread ? profAt(sUp + back) + tanC * v : ground(sUp + back)) - 0.03)));  // (back under the tread)
                    pts.push_back(at(sUp + back, botY));
                }
                if (weak) continue;
                if (offT) {  // (its top flat: the lip and top corners brought to the block's mean, a little up or down)
                    double mean = 0;
                    for (double t : tops) mean += t / tops.size();
                    mean += 0.2 * (roll01(990 + (int)li * 31 + pc) - 0.5);
                    for (size_t q = 0; q < pts.size(); q++) {
                        const int r = (int)(q % 8), e = (int)(q / 8);
                        if (r >= 3 && r <= 5) pts[q].y += mean + adj[e] - tops[e];
                        else if (r == 6) pts[q].y = mean + adj[e] - 0.02;  // (the block's top runs on back to its rear edge)
                    }
                }
                if (onLidar || tab) {  // (a piece the taper has brought down to nothing isn't laid)
                    const double vm = 0.5 * (v0 + v1);
                    if ((tab ? 1 - smoothstep(span - 1.2, span, std::fabs(vm - off)) : 1 - smoothstep(0.45 * span, span, std::fabs(vm - off))) < 0.15) continue;
                }
                addHullRock(pts, hmix(oseed ^ (uint32_t)(li * 977 + pc * 131 + 7)), (float)(0.92 + 0.14 * roll01(rk++)));
            }
        }
        // (A few fallen blocks at the foot of the walls, not a row of them: the walls are the land's.)
        const double wallStep = o.walls > 0 ? 6.0 : 9.0;
        for (double sAt = s0 + 1; sAt < s1 - 1; sAt += wallStep) {
            for (int side = -1; side <= 1; side += 2) {
                if (roll01(rk++) < (o.walls > 0 ? 0.5 : 0.65)) continue;
                const double r = o.walls > 0 ? 1.1 + 1.0 * roll01(rk++) : 0.6 + 0.6 * roll01(rk++);
                const V3 q = pointAlong(sAt) + leftAlong(sAt) * (side * (W + 0.6 + r * 0.7));
                addRock(q.x, q.z, V3(r, r * 0.75, r * 0.9), r * (o.walls > 0 ? 1.1 : 0.6), hmix(oseed ^ (uint32_t)(sAt * 53 + side * 7)), (float)(0.85 + 0.2 * roll01(rk++)));
            }
        }
        // Boulders to pick a line through (in the tread, off its very middle by turns), and a crack's edges.
        for (int b = 0; b < o.boulders; b++) {
            const double sAt = s0 + (s1 - s0) * (b + 0.5) / o.boulders;
            const double v = (b % 2 ? 1 : -1) * (0.6 + (W - 1.2) * roll01(rk++));
            const double r = 0.45 + 0.5 * roll01(rk++);
            const V3 q = pointAlong(sAt) + leftAlong(sAt) * v;
            addRock(q.x, q.z, V3(r, r * 0.7, r * 0.85), std::min(0.55, r * 0.8), hmix(oseed ^ (uint32_t)(b * 131 + 17)), (float)(0.88 + 0.2 * roll01(rk++)));
        }
        tags.push_back({o.name, V3(po.at.x, height(po.at.x, po.at.z) + 3.2, po.at.z)});
    }
    for (const auto& lm : P.landmarks) {  // (a sidecar's landmarks: named over them, a cairn at each)
        tags.push_back({lm.name, V3(lm.at.x, height(lm.at.x, lm.at.z) + 4.0, lm.at.z)});
        fixtures.push_back({PROP_CAIRN, V3(lm.at.x, height(lm.at.x, lm.at.z), lm.at.z), V3(), 0.7});
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
                        if (e > sp.slickLo && e < sp.slickHi) {
                            // Slickrock country: bare sandstone over all but the flats and the hollows, which hold red sand. (ROCK: a
                            // sticky tire on dry sandstone, hardly any rolling loss: what slickrock is to drive on.)
                            double around = 0;
                            for (int q = 0; q < 4; q++) {
                                const int a = std::min(hN - 1, std::max(0, i + (q == 0 ? 6 : q == 1 ? -6 : 0))), b = std::min(hN - 1, std::max(0, j + (q == 2 ? 6 : q == 3 ? -6 : 0)));
                                around += 0.25 * g[a * hN + b];
                            }
                            const double hollow = around - g[id];  // (> 0: lower than round it)
                            // (Where the sidecar has a ground cover from the aerial photos, it says which is rock and which sand; the
                            // steep is rock whatever it says, the photo's few metres of misregistration on a face.)
                            if (const int cv = t.coverAt(x, z)) {
                                if (s > 0.4 || (cv == 1 && !(hollow > 0.3 && s < 0.08))) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
                                else if (cv == 2 || hollow > 0.05) surf[id] = SURF_SAND, paint[id] = PAINT_SAND;
                                else surf[id] = SURF_DIRT, paint[id] = n1 < 0.45 ? PAINT_SAND : PAINT_DESERT;
                            } else if (s > 0.14 || (s > 0.05 && hollow < 0.1 && n1 > 0.3) || (hollow < -0.25 && n1 > 0.2)) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
                            else if (hollow > 0.05 || n1 < 0.38) surf[id] = SURF_SAND, paint[id] = PAINT_SAND;
                            else surf[id] = SURF_DIRT, paint[id] = PAINT_DESERT;
                        } else if (s > 0.5 || (s > 0.2 && n1 > 0.55) || (sp.ledgeStep > 0 && s > 0.25)) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
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
                for (const realterrain::SpecPlace& sp2 : sp.places)
                    if (sp2.town && sp2.name == pl.name) {
                        // A town (a sidecar's): low blocks along its main street, which runs the way the town faces, either side
                        // of it from 60 m to 330 m out: a motel, stores, a gas station, houses. None on a road, the water or a slope.
                        const V3 fw(std::cos(yw), 0, -std::sin(yw)), lf(std::sin(yw), 0, std::cos(yw));
                        int made = 0;
                        for (int k = 0; k < 28; k++) {
                            const double along = (k / 2 % 2 ? -1 : 1) * (60 + 22 * (k / 4)), off = (k % 2 ? 1 : -1) * (16 + 4 * cellRoll(k, 1, 7, sp.seed));
                            const double big = cellRoll(k, 2, 7, sp.seed);
                            const V3 hs(5 + 9 * big, 2.2 + 2.5 * cellRoll(k, 3, 7, sp.seed), 4 + 5 * big);
                            const V3 c = Pp + fw * along + lf * (off + (off > 0 ? hs.z : -hs.z));
                            bool ok = true;
                            double lo = 1e9, hi = -1e9;
                            for (int a = -1; a <= 1 && ok; a++)
                                for (int b = -1; b <= 1 && ok; b++) {
                                    const V3 q = c + fw * (a * (hs.x + 3)) + lf * (b * (hs.z + 3));
                                    const double h = height(q.x, q.z);
                                    lo = std::min(lo, h), hi = std::max(hi, h);
                                    if (roadAt(q.x, q.z).near || waterLevel(q.x, q.z) > -1e8 || std::fabs(q.x) > half - 200 || std::fabs(q.z) > half - 200) ok = false;
                                }
                            if (!ok || hi - lo > 2.5) continue;
                            const float t = (float)cellRoll(k, 4, 7, sp.seed);
                            const V3 col(0.72 + 0.12 * t, 0.62 + 0.08 * t, 0.5 + 0.06 * t);
                            addBox(V3(c.x, lo + hs.y - 0.3, c.z), V3(hs.x, hs.y + 0.3, hs.z), yw, 0, 0, col);
                            for (int i2 = cellI(c.x - 20); i2 <= cellI(c.x + 20); i2++)
                                for (int j2 = cellI(c.z - 20); j2 <= cellI(c.z + 20); j2++) keep[i2 * hN + j2] = 1;
                            made++;
                        }
                        (void)made;
                    }
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
                const bool covered = !t.cover.c.empty();
                const int nbAll = covered ? count(eB * 8.0) : nb;  // (with a ground cover: bushes where the photos show brush)
                for (int q = 0; q < nbAll; q++) {
                    const double x = x0 + cellRoll(ci, cj, 2000 + 3 * q, seed) * cellM, z = z0 + cellRoll(ci, cj, 2001 + 3 * q, seed) * cellM;
                    if (!clear(x, z) || slopeAt(x, z) > 0.7) continue;
                    if (covered) {
                        const int cv = t.coverAt(x, z);
                        if (cv == 1 || (cv != 3 && cv != 0)) continue;
                    }
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
                    const double big = (climate == realterrain::CLIMATE_DESERT ? 2.2 : 1.6) * sp.rockSize;
                    const double rr = 0.45 + u * u * big * (0.6 + s), flat = 0.5 + 0.4 * v;
                    // (scenery off the trails: a whole hull, not cut into its convex pieces (world.h kWholeRockSeeds): Moab's 20 k scanned
                    // rocks cut were 1.5 GB of the game's 4.7)
                    addRock(x, z, V3(rr, rr * flat, rr * (0.75 + 0.3 * v)), rr * flat * (0.5 + 0.5 * u),
                            kWholeRockSeeds + 0xA00000u + (hmix((uint32_t)(ci * 7919 + cj * 104729 + q) ^ seed) & 0x1FFFFFu),
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
