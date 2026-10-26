// The big map's build (onemap.h, docs/plans/one-map.md): the regions' old maps laid into the Backcountry.
//
// The Backcountry's build starts the regions' worlds on workers (startRegionBuilds), makes its land, and lays theirs into it
// (stitchRegionLand): each region's core copied cell for cell, and round it a band where the land eases from the region's edge into the
// Backcountry's. Then the Backcountry paints and plants as it always has (the band is its country; it grows nothing in the cores) and
// at the end the rest of each region comes over (stitchRegionContent): its surfaces, paint, roads and water in the core, everything on
// it, its stations, pads and start.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <thread>
#include <unordered_map>

#include "loadprof.h"
#include "onemap.h"
#include "parallel.h"
#include "places.h"
#include "props.h"
#include "spawnpad.h"
#include "world.h"
#include "worldcache.h"

struct World::RegionBuild {
    const MapRegion* region = nullptr;
    std::unique_ptr<World> world;
    std::thread worker;
};

std::vector<World::RegionBuild*> World::startRegionBuilds() {
    std::vector<RegionBuild*> out;
    // (What a world's build reads off its thread: the scanned rocks in use, the cache switch, the tests' thread cap.)
    const props::HullLibrary* hulls = props::activeRockScans();
    const bool bypass = worldcache::bypass;
    const int cap = parallelRowsCap;
    for (const MapRegion& r : mapRegions()) {
        RegionBuild* b = new RegionBuild;
        b->region = &r;
        b->worker = std::thread([b, hulls, bypass, cap] {
            props::setActiveRockScans(hulls);
            worldcache::bypass = bypass;
            parallelRowsCap = cap;
            World::withShowcase = false, World::halfSizeOverride = 0, World::wildsKind = WILDS_MEADOW;
            b->world = std::make_unique<World>(b->region->map);
        });
        out.push_back(b);
    }
    return out;
}

void World::finishRegionBuilds(std::vector<RegionBuild*>& builds) {
    loadprof::Scope lp("world.regions wait");
    for (RegionBuild* b : builds)
        if (b->worker.joinable()) b->worker.join();
}

namespace {
// How far the region's land at its core's edge stands above the land round it (the Backcountry's, its edge ridge opened round the
// region), carried out past the core: the band round a region is that land raised or lowered by this, fading out. So the land keeps its
// own hills and hollows through the band (no ridge or gully of the region's drawn out in straight lines), and meets the region's
// exactly at the core's edge.
struct Offset {
    double cx0, cx1, cz0, cz1;
    // Each edge of the core (0 x = cx0, 1 x = cx1, 2 z = cz0, 3 z = cz1) sampled every metre along it, and the running sums of that
    // (an average over any stretch of it in two lookups).
    std::vector<double> edge[4], sum[4];
    double from[4];
    // `region(lx, lz)`: the region's height at a point of its own; `land(lx, lz)`: the land round it there, before the region was laid in.
    template <class F, class G>
    Offset(const MapRegion& r, F region, G land) {
        r.core(cx0, cx1, cz0, cz1);
        for (int e = 0; e < 4; e++) {
            const bool alongZ = e < 2;  // (the x edges run along z)
            const double a = alongZ ? cz0 : cx0, b = alongZ ? cz1 : cx1, fixed = e == 0 ? cx0 : e == 1 ? cx1 : e == 2 ? cz0 : cz1;
            from[e] = a;
            for (double t = a; t <= b + 1e-9; t += 1.0) {
                const double lx = alongZ ? fixed : t, lz = alongZ ? t : fixed;
                edge[e].push_back(region(lx, lz) - land(lx, lz));
            }
            sum[e].assign(edge[e].size() + 1, 0.0);
            for (size_t k = 0; k < edge[e].size(); k++) sum[e][k + 1] = sum[e][k] + edge[e][k];
        }
    }
    double at(int e, double t) const {
        const int n = (int)edge[e].size();
        const double u = clampd(t - from[e], 0, n - 1.0);
        const int k = std::min(n - 2, (int)u);
        return edge[e][k] + (edge[e][k + 1] - edge[e][k]) * (u - k);
    }
    // The average of edge e over [t - hw, t + hw] (clamped to the edge).
    double mean(int e, double t, double hw) const {
        const int n = (int)edge[e].size();
        const int k0 = std::max(0, (int)std::floor(t - hw - from[e])), k1 = std::min(n - 1, (int)std::ceil(t + hw - from[e]));
        return k1 >= k0 ? (sum[e][k1 + 1] - sum[e][k0]) / (k1 - k0 + 1) : edge[e][std::min(n - 1, std::max(0, k0))];
    }
    // At a point `out` past the core: the edge's offset at the nearest point of it, averaged along the edge over more of it the further
    // out (60 m a few metres from the edge, 150 m at 40 m out, 800 m far out: a cliff at the core's edge eases off as it goes).
    double carried(double lx, double lz, double out) const {
        const double px = clampd(lx, cx0, cx1), pz = clampd(lz, cz0, cz1);
        const double dx = std::fabs(lx - px), dz = std::fabs(lz - pz);
        // (Round a corner the two edges share it by direction, so nothing steps where the corner meets the side.)
        const double wx = dx + dz > 0 ? dx / (dx + dz) : 0, wz = dx + dz > 0 ? dz / (dx + dz) : 0;
        const int ex = lx > px ? 1 : 0, ez = lz > pz ? 3 : 2;
        const double hw = std::min(400.0, 30 * smoothstep(0, 30, out) + 1.2 * out);
        double v = 0;
        if (wx > 0) v += wx * (hw < 0.5 ? at(ex, pz) : mean(ex, pz, hw));
        if (wz > 0) v += wz * (hw < 0.5 ? at(ez, px) : mean(ez, px, hw));
        return v;
    }
};
}  // namespace

void World::stitchRegionLand(const std::vector<RegionBuild*>& builds, CellTiles<uint8_t>& keep) {
    loadprof::Scope lp("world.regions land");
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    for (const RegionBuild* b : builds) {
        const MapRegion& r = *b->region;
        const World& R = *b->world;
        if (R.hN < 2 || std::fabs(R.half - kRegionHalf) > 0.5) continue;
        // (The land round it as it was, read before any of it changes.)
        const Offset offset(r, [&](double lx, double lz) { return R.height(lx, lz); },
                            [&](double lx, double lz) { return (double)hgrid[(size_t)cellI(lx + r.mx) * hN + cellI(lz + r.mz)]; });
        const double fade = std::max(r.seamFade(), MapRegion::kSideFade);
        const int i0 = cellI(r.x0() - fade), i1 = cellI(r.x1() + fade), j0 = cellI(r.z0() - fade), j1 = cellI(r.z1() + fade);
        const int oi = (int)std::lround(r.x0() + half), oj = (int)std::lround(r.z0() + half);  // (the region's cell 0 on this grid)
        parallelRows(i1 - i0 + 1, [&](int di) {
            const int i = i0 + di;
            const double x = -half + i * kRes, lx = x - r.mx;
            for (int j = j0; j <= j1; j++) {
                const double z = -half + j * kRes, lz = z - r.mz;
                const int id = i * hN + j;
                const double out = r.coreOutside(lx, lz);
                if (out <= 0) {  // the core: the old map's own, cell for cell
                    hgrid[id] = R.hgrid[(size_t)(i - oi) * R.hN + (j - oj)];
                    continue;
                }
                const double len = r.fadeAt(lx, lz, out);
                if (out >= len) continue;
                hgrid[id] = (float)(hgrid[id] + offset.carried(lx, lz, out) * smoothstep(len, 0, out));
            }
        });
        // (The core grows nothing of the Backcountry's: what grows there comes with the region.)
        for (int i = std::max(0, oi); i < std::min(hN, oi + R.hN); i++)
            for (int j = std::max(0, oj); j < std::min(hN, oj + R.hN); j++)
                if (r.inCore(-half + i * kRes - r.mx, -half + j * kRes - r.mz)) keep.set(i * hN + j, 1);
    }
}

namespace {
void moveRock(Rock& k, const V3& t) {
    k.c += t, k.origin += t, k.at += t;
    for (V3& v : k.v) v += t;
    for (size_t f = 0; f < k.fn.size() && f < k.fd.size(); f++) k.fd[f] += dot(k.fn[f], t);
    for (Rock& p : k.parts) moveRock(p, t);
}
}  // namespace

void World::stitchRegionContent(const std::vector<RegionBuild*>& builds) {
    loadprof::Scope lp("world.regions content");
    for (const RegionBuild* b : builds) {
        const MapRegion& r = *b->region;
        const World& R = *b->world;
        if (R.hN < 2 || std::fabs(R.half - kRegionHalf) > 0.5) continue;
        const V3 t(r.mx, 0, r.mz);
        const int oi = (int)std::lround(r.x0() + half), oj = (int)std::lround(r.z0() + half);
        // In the region's own coordinates; and not where a road in from the Backcountry was laid.
        auto inCore = [&](double x, double z) {
            if (!r.inCore(x, z)) return false;
            if (roadMask.empty()) return true;
            const int id = cellIndex(x + r.mx, z + r.mz);
            return id < 0 || !roadMask[id];
        };
        // ---- The ground: surfaces, paint, roads and water, cell for cell in the core ----
        for (int ri = 0; ri < R.hN; ri++) {
            const int i = ri + oi;
            if (i < 0 || i >= hN) continue;
            for (int rj = 0; rj < R.hN; rj++) {
                const int j = rj + oj;
                if (j < 0 || j >= hN) continue;
                const double lx = -R.half + ri * kRes, lz = -R.half + rj * kRes;
                if (!inCore(lx, lz)) continue;
                const int id = i * hN + j, rid = ri * R.hN + rj;
                if (!roadMask.empty() && roadMask[id]) continue;  // (a road in from the Backcountry: its own)
                hgrid[id] = R.hgrid[rid];
                surf[id] = R.cellSurface(rid);
                paint[id] = R.cellPaint(rid);
                // The Mountain's snow was the ground shader's, over 205 m on its own map; the big map draws everything with the
                // Backcountry's settings, whose snow is painted on: so is the Mountain's now, about where it lay.
                if (r.map == MAP_MOUNTAIN && paint[id] == PAINT_NONE && R.hgrid[rid] > 150) {
                    const double x = -half + i * kRes, z = -half + j * kRes, hh = R.hgrid[rid];
                    const double sl = std::hypot(R.gridAt(std::min(ri + 1, R.hN - 1), rj) - R.gridAt(std::max(ri - 1, 0), rj),
                                                 R.gridAt(ri, std::min(rj + 1, R.hN - 1)) - R.gridAt(ri, std::max(rj - 1, 0))) / 2;
                    if (hh + 70 * fbmNoise(x * 0.02, z * 0.02, 3) > 262 && sl < 0.9) paint[id] = PAINT_SNOW;
                }
                roadLat.set(id, R.roadLat.empty() ? 0.0f : (float)R.roadLat[rid]);
                roadAlong.set(id, R.roadAlong.empty() ? 0.0f : (float)R.roadAlong[rid]);
                roadNear.set(id, R.roadNear.empty() ? (uint8_t)0 : (uint8_t)R.roadNear[rid]);
                float wy = -1e9f, fx = 0, fz = 0;
                if (!R.waterY.empty()) {
                    wy = R.waterY[rid];
                    if (!R.flowX.empty()) fx = R.flowX[rid], fz = R.flowZ[rid];
                } else {
                    for (const Water& wt : R.waters)  // (lakes kept as discs on the old map: onto the grid, as waterLevel had them)
                        if ((lx - wt.x) * (lx - wt.x) + (lz - wt.z) * (lz - wt.z) < wt.r * wt.r) {
                            wy = (float)wt.y;
                            break;
                        }
                }
                waterY.set(id, wy), flowX.set(id, fx), flowZ.set(id, fz);
            }
        }
        // ---- The ground round the core: the region's own ground reaching out into the band in ragged patches (more of it near the core),
        // so its country gives way to the Backcountry's over a hundred and fifty metres, not along a straight line ----
        {
            auto natural = [](uint8_t pnt) {
                return pnt == PAINT_NONE || pnt == PAINT_DESERT || pnt == PAINT_SAND || pnt == PAINT_SCREE || pnt == PAINT_SNOW || pnt == PAINT_REDROCK;
            };
            constexpr double kReach = 160;
            double cx0, cx1, cz0, cz1;
            r.core(cx0, cx1, cz0, cz1);
            auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
            for (int i = cellI(r.x0() - kReach); i <= cellI(r.x1() + kReach); i++)
                for (int j = cellI(r.z0() - kReach); j <= cellI(r.z1() + kReach); j++) {
                    const double x = -half + i * kRes, z = -half + j * kRes, lx = x - r.mx, lz = z - r.mz;
                    const double out = r.coreOutside(lx, lz);
                    if (out <= 0 || out >= kReach) continue;
                    const int id = i * hN + j;
                    if (!natural(paint[id]) || (!roadMask.empty() && roadMask[id]) || waterY[id] > -1e8f) continue;
                    const double n = fbmNoise(x * 0.025 + 3.3, z * 0.025 - 1.7, 3) + 0.2 * (valueNoise(x * 0.21, z * 0.19) - 0.5);
                    if (n < 0.3 + 0.5 * smoothstep(0, kReach, out)) continue;
                    const int rid = R.cellIndex(clampd(lx, cx0, cx1), clampd(lz, cz0, cz1));
                    if (rid < 0 || !natural(R.cellPaint(rid))) continue;
                    paint[id] = R.cellPaint(rid), surf[id] = R.cellSurface(rid);
                }
        }
        // ---- Everything on it (in the core), moved over ----
        for (const Tree& tr : R.trees)
            if (inCore(tr.base.x, tr.base.z)) {
                Tree q = tr;
                q.base += t;
                trees.push_back(q);
            } else if (r.coreOutside(tr.base.x, tr.base.z) < 40) {
                // (Just past the core, where its rim was: the tree stays, on the ground as it is now, so the woods don't end in a line.
                // Its trunk stood with it on the old map: it's stood up again here.)
                const int id = cellIndex(tr.base.x + t.x, tr.base.z + t.z);
                if (id < 0 || (!roadMask.empty() && roadMask[id]) || waterY[id] > -1e8f) continue;
                Tree q = tr;
                q.base = V3(tr.base.x + t.x, height(tr.base.x + t.x, tr.base.z + t.z) - 0.2, tr.base.z + t.z);
                plant(q);
            }
        for (const Pole& p : R.poles)
            if (inCore(p.x, p.z)) poles.push_back({p.x + t.x, p.z + t.z, p.y0, p.y1, p.r});
        for (const Rock& k : R.rocks)
            if (inCore(k.c.x, k.c.z)) {
                rocks.push_back(k);
                moveRock(rocks.back(), t);
                // (A big scanned rock is cut into its convex pieces again where it stands now, as the world cache's reading of it is:
                // the same pieces to the last bit either way.)
                Rock& m = rocks.back();
                m.partsDue = false;
                if (m.scan >= 0 && m.r > 1.2) m.parts.clear(), m.partsDue = true;
            }
        for (const Log& l : R.logs)
            if (inCore(l.a.x, l.a.z) && inCore(l.b.x, l.b.z)) logs.push_back({l.a + t, l.b + t, l.r});
        for (const Bush& bu : R.bushes)
            if (inCore(bu.p.x, bu.p.z)) {
                bushes.push_back(bu);
                bushes.back().p += t;
            } else if (r.coreOutside(bu.p.x, bu.p.z) < 40) {
                const int id = cellIndex(bu.p.x + t.x, bu.p.z + t.z);
                if (id < 0 || (!roadMask.empty() && roadMask[id]) || waterY[id] > -1e8f) continue;
                bushes.push_back(bu);
                bushes.back().p = V3(bu.p.x + t.x, height(bu.p.x + t.x, bu.p.z + t.z), bu.p.z + t.z);
            }
        for (const Box& bx : R.boxes)
            if (inCore(bx.c.x, bx.c.z)) {
                boxes.push_back(bx);
                boxes.back().c += t;
            }
        for (const Prop& f : R.fixtures)
            if (inCore(f.a.x, f.a.z)) {
                fixtures.push_back(f);
                fixtures.back().a += t;
                if (f.kind == PROP_FENCE) fixtures.back().b += t;  // (a fence's other end; a stump's b is its size)
            }
        for (const Placed& m : R.models)
            if (inCore(m.p.x, m.p.z)) {
                models.push_back(m);
                models.back().p += t;
            }
        for (const Tag& g : R.tags)
            if (inCore(g.p.x, g.p.z)) tags.push_back({g.text, g.p + t});
        for (const Water& wt : R.waters) waters.push_back({wt.x + t.x, wt.z + t.z, wt.r, wt.y});
        for (const River& rv : R.rivers) {
            rivers.push_back(rv);
            for (RiverSpot& sp : rivers.back().spots) sp.p += t;
        }
        for (const Falls& f : R.falls) {
            falls.push_back(f);
            falls.back().lip += t;
        }
        for (const Station& st : R.stations) {
            stations.push_back(st);
            stations.back().p += t;
        }
        for (const SpawnPad& sp : R.spawnPads) {
            spawnPads.push_back(sp);
            spawnPads.back().p += t;
        }
        for (const SectionSpot& s : R.sections) {
            sections.push_back(s);
            sections.back().entry += t, sections.back().exit += t, sections.back().spawn += t;
        }
        if (loopRoad.empty() && !R.loopRoad.empty()) {  // (the Mountain's valley loop, where the traffic drives)
            for (const V3& p : R.loopRoad) loopRoad.push_back(p + t);
            loopAlong = R.loopAlong, loopLength = R.loopLength;
        }
        RegionStart s;
        s.map = r.map, s.spawn = R.spawn + t, s.yaw = R.spawnYaw, s.parkingYaw = R.parkingYaw;
        for (const V3& p : R.parking) s.parking.push_back(p + t);
        regionStarts.push_back(s);
    }
    // The roads' signs: green (an easy road), at each end, on the right going in.
    for (const std::vector<V3>& road : regionRoads) {
        if (road.size() < 20) continue;
        for (int end = 0; end < 2; end++) {
            const size_t k = end ? road.size() - 9 : 8, k2 = end ? k - 4 : k + 4;
            const V3 d = norm(V3(road[k2].x - road[k].x, 0, road[k2].z - road[k].z)), right(-d.z, 0, d.x);
            const V3 q = road[k] - right * 5.5;
            Prop sg{PROP_SIGN, V3(q.x, height(q.x, q.z), q.z), V3(), std::atan2(d.z, -d.x)};
            sg.col[0] = 0.1f, sg.col[1] = 0.55f, sg.col[2] = 0.2f;
            fixtures.push_back(sg);
            poles.push_back({q.x, q.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
        }
    }
    roadMask.clear();
    // A start of its own where the old map had none (the Open Country is played as levels: theirs had the pad).
    for (RegionStart& s : regionStarts) {
        bool padded = false;
        for (const SpawnPad& sp : spawnPads) padded |= std::hypot(sp.p.x - s.spawn.x, sp.p.z - s.spawn.z) < 0.5;
        if (padded) continue;
        s.spawn.y = height(s.spawn.x, s.spawn.z);
        s.spawn = addSpawnPad(*this, s.spawn, s.yaw, 8, 3, 30).p;
    }
}

bool World::startAtRegion(int oldMap) {
    for (const RegionStart& s : regionStarts)
        if (s.map == oldMap) {
            spawn = s.spawn, spawnYaw = s.yaw, parking = s.parking, parkingYaw = s.parkingYaw;
            return true;
        }
    return false;
}

// ---- The roads in -------------------------------------------------------------------------------------------------------------------
// A gravel road from a place of the Backcountry's to each region, laid out the way a road crew would: the way over the land with the
// gentlest grades (a search over 8 m cells, keeping out of water and off the steep, round the regions' cores where it can), smoothed into
// a curve, its tread cut and filled to a grade of at most 12% and eased into the land either side.
namespace {
struct LinkPlan {
    int map;      // the region it goes to
    V3 from, to;  // where it leaves the Backcountry's place and where it arrives in the region
};

std::vector<V3> findWay(const World& w, const V3& a, const V3& b, const std::function<bool(double, double)>& wetLater) {
    constexpr double C = 8;
    const double H = w.halfSize() - 20;
    const double x0 = std::max(-H, std::min(a.x, b.x) - 500), x1 = std::min(H, std::max(a.x, b.x) + 500);
    const double z0 = std::max(-H, std::min(a.z, b.z) - 500), z1 = std::min(H, std::max(a.z, b.z) + 500);
    const int nx = (int)((x1 - x0) / C) + 1, nz = (int)((z1 - z0) / C) + 1;
    auto X = [&](int i) { return x0 + i * C; };
    auto Z = [&](int j) { return z0 + j * C; };
    std::vector<float> h((size_t)nx * nz);
    std::vector<uint8_t> wet((size_t)nx * nz), core((size_t)nx * nz);
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < nz; j++) {
            const double x = X(i), z = Z(j);
            const size_t k = (size_t)i * nz + j;
            h[k] = (float)w.height(x, z);
            wet[k] = w.waterLevel(x, z) > h[k] - 0.6 || wetLater(x, z);
            for (const MapRegion& r : mapRegions()) core[k] |= r.coreOutside(x - r.mx, z - r.mz) <= 0;
        }
    // How steep the land round each cell is, any way (a road along the foot of a cliff would have to cut into it: kept clear of).
    std::vector<float> rough((size_t)nx * nz, 0.0f);
    for (int i = 1; i + 1 < nx; i++)
        for (int j = 1; j + 1 < nz; j++) {
            const size_t k = (size_t)i * nz + j;
            float m = 0;
            for (int di = -1; di <= 1; di++)
                for (int dj = -1; dj <= 1; dj++) m = std::max(m, std::fabs(h[k] - h[(size_t)(i + di) * nz + j + dj]) / (float)(C * std::hypot(di, dj) + 1e-9));
            rough[k] = m;
        }
    auto cellOf = [&](const V3& p) {
        const int i = std::min(nx - 1, std::max(0, (int)std::lround((p.x - x0) / C))), j = std::min(nz - 1, std::max(0, (int)std::lround((p.z - z0) / C)));
        return i * nz + j;
    };
    const int start = cellOf(a), goal = cellOf(b);
    std::vector<float> g((size_t)nx * nz, 1e30f);
    std::vector<int> from((size_t)nx * nz, -1);
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> open;
    auto guess = [&](int k) { return (float)std::hypot(X(k / nz) - b.x, Z(k % nz) - b.z); };
    g[start] = 0, open.push({guess(start), start});
    while (!open.empty()) {
        const auto [f, k] = open.top();
        open.pop();
        if (k == goal) break;
        if (f > g[k] + guess(k) + 1e-3f) continue;
        const int i = k / nz, j = k % nz;
        for (int di = -1; di <= 1; di++)
            for (int dj = -1; dj <= 1; dj++) {
                if (!di && !dj) continue;
                const int ii = i + di, jj = j + dj;
                if (ii < 0 || jj < 0 || ii >= nx || jj >= nz) continue;
                const int q = ii * nz + jj;
                if (wet[q] && q != goal) continue;
                const double L = C * std::hypot(di, dj), grade = std::fabs(h[q] - h[k]) / L;
                if (grade > 0.3) continue;
                // Gentle grades first; steeper than 15% only where there's no other way; through a region's own land (its trails,
                // its trees) only for the last of the way in.
                const double cost = L * (1 + 40 * grade * grade + (grade > 0.15 ? 4 : 0) + 8 * rough[q] * rough[q]) * (core[q] ? 2.5 : 1.0);
                if (g[k] + cost < g[q]) g[q] = (float)(g[k] + cost), from[q] = k, open.push({g[q] + guess(q), q});
            }
    }
    std::vector<V3> path;
    if (from[goal] < 0) return path;
    for (int k = goal; k >= 0; k = from[k]) path.push_back(V3(X(k / nz), 0, Z(k % nz)));
    std::reverse(path.begin(), path.end());
    path.front() = V3(a.x, 0, a.z), path.back() = V3(b.x, 0, b.z);
    return path;
}

std::vector<V3> resampled(const std::vector<V3>& pts, double step) {
    std::vector<V3> out;
    if (pts.empty()) return out;
    out.push_back(pts[0]);
    double carry = 0;
    for (size_t k = 1; k < pts.size(); k++) {
        const V3 a = pts[k - 1], d = pts[k] - a;
        const double L = std::hypot(d.x, d.z);
        double s = step - carry;
        for (; s <= L; s += step) out.push_back(a + d * (s / L));
        carry = L - (s - step);
    }
    if (std::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.3) out.push_back(pts.back());
    return out;
}
}  // namespace

void World::layRegionRoads(const std::vector<RegionBuild*>& builds, CellTiles<uint8_t>& keep, const std::function<bool(double, double)>& wetLater) {
    loadprof::Scope lp("world.regions roads");
    roadMask.reset(hN, 0);
    std::vector<LinkPlan> plans;
    auto placeAt = [](const char* id) {
        const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
        return p ? p->p : V3();
    };
    for (const RegionBuild* b : builds) {
        const MapRegion& r = *b->region;
        const World& R = *b->world;
        const V3 t(r.mx, 0, r.mz);
        if (r.map == MAP_WILDS) {  // Ranger Flats: from the ranger station's yard out west to the open country's start (it faces east)
            plans.push_back({r.map, placeAt("BC.RANGER") + V3(-30, 0, 0), R.spawn + t + V3(38, 0, 0)});
        } else if (r.map == MAP_MOUNTAIN && !R.loopRoad.empty()) {  // Lakeshore's fuel stop, south to the valley loop road's east end
            const V3 want(285, 0, -260);
            V3 best = R.loopRoad[0];
            for (const V3& q : R.loopRoad)
                if (std::hypot(q.x - want.x, q.z - want.z) < std::hypot(best.x - want.x, best.z - want.z)) best = q;
            plans.push_back({r.map, placeAt("BC.FUEL_LAKE") + V3(0, 0, -15), best + t});
        } else if (r.map == MAP_FRONTIER) {  // the Lost Mine's wash, south to the Cold Creek Lodge
            V3 lodge = V3(440, 0, 471);
            for (const Station& st : R.stations)
                if (std::string(st.name) == "COLD CREEK LODGE") lodge = st.p;
            plans.push_back({r.map, V3(2445, 0, -1762), lodge + t + V3(0, 0, 28)});
        }
    }
    constexpr double kHalf = 3.5, kBlend = 12;
    std::vector<V3> pads;
    for (const RegionBuild* b : builds)
        for (const Station& st : b->world->stations) pads.push_back(st.p + V3(b->region->mx, 0, b->region->mz));
    for (const LinkPlan& plan : plans) {
        const std::vector<V3> way = findWay(*this, plan.from, plan.to, wetLater);
        if (way.size() < 2) continue;
        // A curve through every fourth cell of the way.
        std::vector<V3> ctl;
        for (size_t k = 0; k < way.size(); k += 4) ctl.push_back(way[k]);
        if (std::hypot(ctl.back().x - way.back().x, ctl.back().z - way.back().z) > 1) ctl.push_back(way.back());
        std::vector<V3> pts = resampled(splinePoints(ctl, false), 1.0);
        const int n = (int)pts.size();
        // Its heights: the land under it smoothed over 15 m either side, eased into the ground at both ends, no steeper than 12%.
        std::vector<double> raw(n), y(n);
        for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
        for (int k = 0; k < n; k++) {
            double sum = 0;
            int c = 0;
            for (int q = std::max(0, k - 15); q <= std::min(n - 1, k + 15); q++) sum += raw[q], c++;
            y[k] = sum / c;
        }
        for (int end = 0; end < 2; end++) {
            const int idx = end ? n - 1 : 0;
            const double off = raw[idx] - y[idx];
            for (int k = 0; k < n; k++) y[k] += off * smoothstep(30, 0, std::fabs((double)(k - idx)));
        }
        for (int pass = 0; pass < 8; pass++)
            for (int dir = 0; dir < 2; dir++)
                for (int q = 1; q < n; q++) {
                    const int k = dir ? n - 1 - q : q, prev = dir ? k + 1 : k - 1;
                    if (k == 0 || k == n - 1) continue;
                    y[k] = clampd(y[k], y[prev] - 0.12, y[prev] + 0.12);
                }
        // The nearest stretch of it to every cell nearby.
        struct Near {
            double d, k, lat;
        };
        std::unordered_map<int, Near> near;
        auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
        // How far its sides ease into the land: 12 m, more where it's cut deep or built up high (a bank no steeper than about 1 in 1.6).
        std::vector<double> blend(n);
        for (int k = 0; k < n; k++) blend[k] = std::max(kBlend, 2.6 * std::fabs(y[k] - raw[k]));
        for (int pass = 0; pass < 2; pass++)  // (and no sudden change along it)
            for (int k = 1; k < n; k++) blend[k] = std::max(blend[k], blend[k - 1] - 0.5), blend[n - 1 - k] = std::max(blend[n - 1 - k], blend[n - k] - 0.5);
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double reach = kHalf + std::max(blend[s], blend[s + 1]);
            const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
            if (L2 < 1e-9) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = -half + i * kRes, z = -half + j * kRes;
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
                    const double d = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    if (d > reach) continue;
                    const int id = i * hN + j;
                    auto it = near.find(id);
                    if (it == near.end() || d < it->second.d) near[id] = {d, s + t, (dx * (z - a.z) - dz * (x - a.x)) / L};
                }
        }
        // (In a fixed order, whatever the map's hashing: the same world on every machine.)
        std::vector<std::pair<int, Near>> cells(near.begin(), near.end());
        std::sort(cells.begin(), cells.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
        for (const auto& [id, c] : cells) {
            const double e = c.d - kHalf;
            const V3 at = cellCenter(id);
            double padNear = 1;  // (a region's repair pad stays as flat as it was, the road easing off round it)
            for (const V3& p : pads) padNear = std::min(padNear, smoothstep(11, 24, std::hypot(at.x - p.x, at.z - p.z)));
            if (padNear <= 0) continue;
            const int k = std::min(n - 2, (int)c.k);
            const double ty = y[k] + (y[k + 1] - y[k]) * clampd(c.k - k, 0, 1);
            const double wgt = (e < 1.0 ? 1.0 : smoothstep(blend[k], 1.0, e)) * padNear;
            hgrid[id] = (float)(hgrid[id] + (ty - hgrid[id]) * wgt);
            roadMask.set(id, 1);
            if (e < 3) keep.set(id, 1);
            if (e < 0.5 && padNear >= 1) {
                paint[id] = PAINT_ROAD, surf[id] = SURF_GRAVEL;
                roadNear.set(id, 1), roadLat.set(id, (float)c.lat), roadAlong.set(id, (float)c.k);
            }
        }
        std::vector<V3> laid;
        for (int k = 0; k < n; k++) laid.push_back(V3(pts[k].x, y[k], pts[k].z));
        regionRoads.push_back(std::move(laid));
    }
}

World worldForPick(int pick) {
    if (!isRegionMap(pick)) return World(pick);
    World w(MAP_BACKCOUNTRY);
    w.startAtRegion(pick);
    return w;
}
