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
#include <atomic>
#include <queue>
#include <thread>
#include <unordered_map>

#include "detmath.h"
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
                // (The Mountain's snow is the ground shader's again, from 205 m: the big map draws each region with its own look,
                // groundLookAt.)
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
                if (rockCutsIntoPieces(m)) m.parts.clear(), m.partsDue = true;
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
    for (size_t ri = 0; ri < regionRoads.size() && ri < linkRoads; ri++) {
        const std::vector<V3>& road = regionRoads[ri];
        if (road.size() < 20) continue;
        for (int end = 0; end < 2; end++) {
            const size_t k = end ? road.size() - 9 : 8, k2 = end ? k - 4 : k + 4;
            const V3 d = norm(V3(road[k2].x - road[k].x, 0, road[k2].z - road[k].z)), right(-d.z, 0, d.x);
            const V3 q = road[k] - right * 5.5;
            Prop sg{PROP_SIGN, V3(q.x, height(q.x, q.z), q.z), V3(), dm::atan2(d.z, -d.x)};
            sg.col[0] = 0.1f, sg.col[1] = 0.55f, sg.col[2] = 0.2f;
            fixtures.push_back(sg);
            poles.push_back({q.x, q.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
        }
    }
    dressRegionTrails();
    roadMask.clear();
    // A start of its own where the old map had none (the Open Country is played as levels: theirs had the pad).
    for (RegionStart& s : regionStarts) {
        bool padded = false;
        for (const SpawnPad& sp : spawnPads) padded |= dm::hypot(sp.p.x - s.spawn.x, sp.p.z - s.spawn.z) < 0.5;
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

std::vector<V3> findWay(const World& w, const V3& a, const V3& b, const std::function<bool(double, double)>& wetLater, double margin = 500, double greed = 1.0) {
    constexpr double C = 8;
    const double H = w.halfSize() - 20;
    const double x0 = std::max(-H, std::min(a.x, b.x) - margin), x1 = std::min(H, std::max(a.x, b.x) + margin);
    const double z0 = std::max(-H, std::min(a.z, b.z) - margin), z1 = std::min(H, std::max(a.z, b.z) + margin);
    const int nx = (int)((x1 - x0) / C) + 1, nz = (int)((z1 - z0) / C) + 1;
    auto X = [&](int i) { return x0 + i * C; };
    auto Z = [&](int j) { return z0 + j * C; };
    std::vector<float> h((size_t)nx * nz);
    std::vector<uint8_t> wet((size_t)nx * nz), core((size_t)nx * nz);
    const std::vector<MapRegion>& rs = mapRegions();
    double cb[3][4];  // (each region's core, in its own coordinates: inside it is coreOutside <= 0)
    for (int q = 0; q < 3; q++) rs[q].core(cb[q][0], cb[q][1], cb[q][2], cb[q][3]);
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < nz; j++) {
            const double x = X(i), z = Z(j);
            const size_t k = (size_t)i * nz + j;
            h[k] = (float)w.height(x, z);
            wet[k] = w.waterLevel(x, z) > h[k] - 0.6 || wetLater(x, z);
            for (int q = 0; q < 3; q++) core[k] |= x - rs[q].mx >= cb[q][0] && x - rs[q].mx <= cb[q][1] && z - rs[q].mz >= cb[q][2] && z - rs[q].mz <= cb[q][3];
        }
    // (The step to each of the eight cells round one, and as the steepness below divides by it: the same numbers as working them out each time.)
    double stepLen[3][3];
    float roughLen[3][3];
    for (int di = -1; di <= 1; di++)
        for (int dj = -1; dj <= 1; dj++) stepLen[di + 1][dj + 1] = C * dm::hypot(di, dj), roughLen[di + 1][dj + 1] = (float)(C * dm::hypot(di, dj) + 1e-9);
    // How steep the land round each cell is, any way (a road along the foot of a cliff would have to cut into it: kept clear of).
    std::vector<float> rough((size_t)nx * nz, 0.0f);
    for (int i = 1; i + 1 < nx; i++)
        for (int j = 1; j + 1 < nz; j++) {
            const size_t k = (size_t)i * nz + j;
            float m = 0;
            for (int di = -1; di <= 1; di++)
                for (int dj = -1; dj <= 1; dj++) m = std::max(m, std::fabs(h[k] - h[(size_t)(i + di) * nz + j + dj]) / roughLen[di + 1][dj + 1]);
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
    // (`greed` over 1 trusts the straight line more: far fewer cells looked at, a way a little longer than the best. The trails use it.)
    auto guess = [&](int k) { return (float)(greed * dm::hypot(X(k / nz) - b.x, Z(k % nz) - b.z)); };
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
                const double L = stepLen[di + 1][dj + 1], grade = std::fabs(h[q] - h[k]) / L;
                if (grade > 1.0) continue;  // (past 30%: a cliff, only where there is no other way (onto a mesa): cut into a ramp when laid)
                // Gentle grades first; steeper than 15% only where there's no other way; through a region's own land (its trails,
                // its trees) only for the last of the way in.
                const double cost = L * (1 + 40 * grade * grade + (grade > 0.15 ? 4 : 0) + 8 * rough[q] * rough[q]) * (core[q] ? 2.5 : 1.0) * (grade > 0.3 ? 60 + 400 * (grade - 0.3) : 1.0);
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
        const double L = dm::hypot(d.x, d.z);
        double s = step - carry;
        for (; s <= L; s += step) out.push_back(a + d * (s / L));
        carry = L - (s - step);
    }
    if (dm::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.3) out.push_back(pts.back());
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
                if (dm::hypot(q.x - want.x, q.z - want.z) < dm::hypot(best.x - want.x, best.z - want.z)) best = q;
            plans.push_back({r.map, placeAt("BC.FUEL_LAKE") + V3(0, 0, -15), best + t});
        } else if (r.map == MAP_FRONTIER) {  // the Lost Mine's wash, south to the Cold Creek Lodge
            V3 lodge = V3(440, 0, 471);
            for (const Station& st : R.stations)
                if (std::string(st.name) == "COLD CREEK LODGE") lodge = st.p;
            plans.push_back({r.map, V3(2445, 0, -1762), lodge + t + V3(0, 0, 28)});
        }
    }
    std::vector<V3> pads;
    for (const RegionBuild* b : builds)
        for (const Station& st : b->world->stations) pads.push_back(st.p + V3(b->region->mx, 0, b->region->mz));
    for (const LinkPlan& plan : plans) {
        const std::vector<V3> way = findWay(*this, plan.from, plan.to, wetLater);
        if (way.size() < 2) continue;
        regionRoads.push_back(layWay(way, WayStyle(), keep, pads, 4));
    }
    linkRoads = regionRoads.size();
}

std::vector<V3> World::layWay(const std::vector<V3>& way, const WayStyle& st, CellTiles<uint8_t>& keep, const std::vector<V3>& pads, int thin,
                              const std::function<void(std::vector<double>&)>& shape, const std::function<double(double)>& tilt) {
    const double kHalf = st.half, kBlend = st.blend;
    // A curve through every `thin`th cell of the way.
    std::vector<V3> ctl;
    for (size_t k = 0; k < way.size(); k += thin) ctl.push_back(way[k]);
    if (dm::hypot(ctl.back().x - way.back().x, ctl.back().z - way.back().z) > 1) ctl.push_back(way.back());
    std::vector<V3> pts = resampled(splinePoints(ctl, false), 1.0);
    const int n = (int)pts.size();
    if (n < 3) return {};
    // Its heights: the land under it smoothed over `window` m either side, eased into the ground at both ends, no steeper than its grade.
    std::vector<double> raw(n), y(n);
    for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
    for (int k = 0; k < n; k++) {
        double sum = 0;
        int c = 0;
        for (int q = std::max(0, k - st.window); q <= std::min(n - 1, k + st.window); q++) sum += raw[q], c++;
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
                y[k] = clampd(y[k], y[prev] - st.maxGrade, y[prev] + st.maxGrade);
            }
    if (shape) shape(y);
    // The nearest stretch of it to every cell nearby.
    struct Near {
        double d, k, lat;
    };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    // How far its sides ease into the land: `blend` m, more where it's cut deep or built up high (a bank no steeper than about 1 in 1.6).
    std::vector<double> blend(n);
    for (int k = 0; k < n; k++) blend[k] = std::max(kBlend, 2.6 * std::fabs(y[k] - raw[k]));
    for (int pass = 0; pass < 2; pass++)  // (and no sudden change along it)
        for (int k = 1; k < n; k++) blend[k] = std::max(blend[k], blend[k - 1] - 0.5), blend[n - 1 - k] = std::max(blend[n - 1 - k], blend[n - k] - 0.5);
    // (Kept in 16-cell tiles over its bounds, made as they're reached: a hash map of the cells was most of this pass's time.)
    double maxReach = 0, bx0 = 1e30, bx1 = -1e30, bz0 = 1e30, bz1 = -1e30;
    for (int k = 0; k < n; k++)
        maxReach = std::max(maxReach, kHalf + blend[k]), bx0 = std::min(bx0, pts[k].x), bx1 = std::max(bx1, pts[k].x), bz0 = std::min(bz0, pts[k].z),
        bz1 = std::max(bz1, pts[k].z);
    const int ti0 = cellI(bx0 - maxReach - 1) >> 4, tj0 = cellI(bz0 - maxReach - 1) >> 4, tw = (cellI(bx1 + maxReach + 1) >> 4) - ti0 + 1,
              th = (cellI(bz1 + maxReach + 1) >> 4) - tj0 + 1;
    std::vector<int> tileOf((size_t)tw * th, -1);
    std::vector<std::array<Near, 256>> tiles;
    auto slot = [&](int i, int j) -> Near& {
        int& t = tileOf[(size_t)((i >> 4) - ti0) * th + ((j >> 4) - tj0)];
        if (t < 0) {
            t = (int)tiles.size();
            tiles.emplace_back();
            for (Near& q : tiles.back()) q.d = 1e30;
        }
        return tiles[t][((i & 15) << 4) | (j & 15)];
    };
    for (int s = 0; s + 1 < n; s += 2) {  // (2 m chords of the 1 m points: each cell is looked at half as often, a few mm off the curve)
        const int s2 = std::min(n - 1, s + 2);
        const V3 &a = pts[s], &b = pts[s2];
        const double reach = kHalf + std::max({blend[s], blend[s2], blend[std::min(n - 1, s + 1)]}), reach2 = reach * reach;
        const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
        if (L2 < 1e-9) continue;
        for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
            for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                const double x = -half + i * kRes, z = -half + j * kRes;
                const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
                const double ex = x - (a.x + dx * t), ez = z - (a.z + dz * t), dd = ex * ex + ez * ez;
                if (dd > reach2) continue;
                const double d = std::sqrt(dd);
                Near& c = slot(i, j);
                if (d < c.d) c = {d, s + t * (s2 - s), (dx * (z - a.z) - dz * (x - a.x)) / L};
            }
    }
    // (In a fixed order: by cell.)
    std::vector<std::pair<int, Near>> cells;
    for (int a = 0; a < tw; a++)
        for (int b = 0; b < th; b++) {
            const int t = tileOf[(size_t)a * th + b];
            if (t < 0) continue;
            for (int q = 0; q < 256; q++)
                if (tiles[t][q].d < 1e29) cells.push_back({((ti0 + a) * 16 + (q >> 4)) * hN + (tj0 + b) * 16 + (q & 15), tiles[t][q]});
        }
    std::sort(cells.begin(), cells.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
    for (const auto& [id, c] : cells) {
        const double e = c.d - kHalf;
        const V3 at = cellCenter(id);
        double padNear = 1;  // (a region's repair pad stays as flat as it was, the road easing off round it)
        for (const V3& p : pads) {
            const double dd = (at.x - p.x) * (at.x - p.x) + (at.z - p.z) * (at.z - p.z);
            if (dd < 24 * 24) padNear = std::min(padNear, smoothstep(11, 24, std::sqrt(dd)));
        }
        if (padNear <= 0) continue;
        if (roadMask[id] && (e > 0.5 || roadNear[id])) continue;  // (a way laid before keeps its own tread and banks)
        const int k = std::min(n - 2, (int)c.k);
        double ty = y[k] + (y[k + 1] - y[k]) * clampd(c.k - k, 0, 1);
        if (tilt) ty += tilt(c.k) * clampd(c.lat, -kHalf, kHalf);
        const double wgt = (e < 1.0 ? 1.0 : smoothstep(blend[k], 1.0, e)) * padNear;
        hgrid[id] = (float)(hgrid[id] + (ty - hgrid[id]) * wgt);
        roadMask.set(id, 1);
        if (e < 3) keep.set(id, 1);
        if (e < 0.5 && padNear >= 1) {
            paint[id] = st.paint, surf[id] = st.surface;
            roadNear.set(id, 1), roadLat.set(id, (float)c.lat), roadAlong.set(id, (float)c.k);
        }
    }
    std::vector<V3> laid;
    for (int k = 0; k < n; k++) laid.push_back(V3(pts[k].x, y[k], pts[k].z));
    return laid;
}

// ---- The regions' trails (docs/plans/regions2.md) ---------------------------------------------------------------------------------
// In each region's grown country, marked trails between its places and from its old map's edge, two-tracks out to its caches, a back
// road from the Mountain to the Frontier, and the Mountain's pass: from West Fork Fuel up past Snowline Cabin over the High Pass and
// down to Table Rock in the Open Country, with ledges, rock gardens, off-camber shelves, a boulder choke and a scree climb on it.
namespace {
struct TrailPlan {
    std::vector<V3> stops;  // through these (a place, or the edge of a region's old map)
    int kind;               // 0 trail, 1 harder trail, 2 the pass, 3 two-track, 4 back road
};
}  // namespace

void World::layRegionTrails(const std::vector<RegionBuild*>& builds, CellTiles<uint8_t>& keep, const std::function<bool(double, double)>& wetLater) {
    loadprof::Scope lp("world.regions trails");
    auto P = [](const char* id) {
        const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
        return p ? V3(p->p.x, 0, p->p.z) : V3();
    };
    // Just outside a region's old map, on the side toward `to` (where a trail leaves its own country).
    auto edge = [](int map, const V3& to) {
        const MapRegion* r = regionOf(map);
        double a, b, c, d;
        r->core(a, b, c, d);
        const double lx = to.x - r->mx, lz = to.z - r->mz;
        double px = clampd(lx, a, b), pz = clampd(lz, c, d);
        // (On its nearest side to `to`, 8 m out.)
        const double gx = std::min(px - a, b - px), gz = std::min(pz - c, d - pz);
        if (gx <= gz) px = px - a < b - px ? a - 8 : b + 8;
        else pz = pz - c < d - pz ? c - 8 : d + 8;
        return V3(px + r->mx, 0, pz + r->mz);
    };
    const int OC = MAP_WILDS, MT = MAP_MOUNTAIN, FR = MAP_FRONTIER;
    const std::vector<TrailPlan> plans = {
        // The Open Country: a loop out of its meadow by the fuel stop, the northern camp, the fire lookout and the homestead; south to
        // Table Rock; two-tracks to its caches.
        {{edge(OC, P("OC.PRAIRIE_FUEL")), P("OC.PRAIRIE_FUEL")}, 0},
        {{P("OC.PRAIRIE_FUEL"), P("OC.NORTHWOODS_CAMP")}, 0},
        {{P("OC.NORTHWOODS_CAMP"), P("OC.FIRE_TOWER")}, 1},
        {{P("OC.FIRE_TOWER"), P("OC.HOMESTEAD")}, 0},
        {{P("OC.HOMESTEAD"), edge(OC, P("OC.HOMESTEAD"))}, 0},
        {{edge(OC, P("OC.CACHE.04")), P("OC.CACHE.04")}, 1},  // (and on up to Table Rock by its two-track: straight up onto the mesa costs the search a lot)
        {{P("OC.PRAIRIE_FUEL"), P("OC.CACHE.01")}, 3},
        {{edge(OC, P("OC.CACHE.02")), P("OC.CACHE.02")}, 3},
        {{P("OC.HOMESTEAD"), P("OC.CACHE.03")}, 3},
        {{P("OC.TABLE_ROCK"), P("OC.CACHE.04")}, 3},
        // The Mountain: West Fork Fuel off its valley, the pass, the south range's fire lookout and Eagle Ledge, two-tracks.
        {{edge(MT, P("MT.WEST_FORK_FUEL")), P("MT.WEST_FORK_FUEL")}, 0},
        {{P("MT.WEST_FORK_FUEL"), P("MT.SNOWLINE_CABIN"), P("MT.HIGH_PASS"), P("OC.TABLE_ROCK")}, 2},
        {{edge(MT, P("MT.FIRE_TOWER")), P("MT.FIRE_TOWER")}, 1},
        {{P("MT.FIRE_TOWER"), P("MT.EAGLE_LEDGE")}, 1},
        {{edge(MT, P("MT.CACHE.03")), P("MT.CACHE.03")}, 3},
        {{P("MT.WEST_FORK_FUEL"), P("MT.CACHE.01")}, 3},
        {{edge(MT, P("MT.CACHE.02")), P("MT.CACHE.02")}, 3},
        {{P("MT.SNOWLINE_CABIN"), P("MT.CACHE.04")}, 3},
        // The back road between the Mountain's valley and the Frontier's badlands.
        {{edge(MT, P("FR.BADLANDS_FUEL")), P("FR.BADLANDS_FUEL")}, 4},
        // The Frontier: Badlands Fuel and Tabletop Mesa in the west, the canyon's line shack, the shelf and the fire lookout in the east.
        {{edge(FR, P("FR.BADLANDS_FUEL")), P("FR.BADLANDS_FUEL")}, 0},
        {{P("FR.BADLANDS_FUEL"), P("FR.TABLETOP_MESA")}, 1},
        {{edge(FR, P("FR.LINE_SHACK")), P("FR.LINE_SHACK")}, 0},
        {{P("FR.LINE_SHACK"), P("FR.SLICKROCK_SHELF")}, 1},
        {{P("FR.SLICKROCK_SHELF"), P("FR.FIRE_TOWER")}, 0},
        {{P("FR.BADLANDS_FUEL"), P("FR.CACHE.01")}, 3},
        {{P("FR.TABLETOP_MESA"), P("FR.CACHE.02")}, 3},
        {{P("FR.FIRE_TOWER"), P("FR.CACHE.03")}, 3},
        {{edge(FR, P("FR.CACHE.04")), P("FR.CACHE.04")}, 3},
    };
    std::vector<V3> pads;
    for (const RegionBuild* b : builds)
        for (const Station& st : b->world->stations) pads.push_back(st.p + V3(b->region->mx, 0, b->region->mz));
    // (Every leg's way found at once, on the land as the roads left it, on a few threads; then laid one after another.)
    std::vector<std::pair<size_t, size_t>> legIds;
    for (size_t p = 0; p < plans.size(); p++)
        for (size_t s = 0; s + 1 < plans[p].stops.size(); s++) legIds.push_back({p, s});
    std::vector<std::vector<V3>> legs(legIds.size());
    {
        loadprof::Scope lpFind("world.regions trails find");
        // (A leg to a thread at a time, longest first: the pass's legs take the most.)
        std::vector<size_t> order(legIds.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        auto span = [&](size_t i) {
            const TrailPlan& pl = plans[legIds[i].first];
            const V3 &a = pl.stops[legIds[i].second], &b = pl.stops[legIds[i].second + 1];
            return std::fabs(a.x - b.x) + std::fabs(a.z - b.z);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return span(a) > span(b); });
        std::atomic<size_t> next{0};
        auto work = [&] {
            for (size_t n; (n = next++) < order.size();) {
                const size_t i = order[n];
                const TrailPlan& pl = plans[legIds[i].first];
                legs[i] = findWay(*this, pl.stops[legIds[i].second], pl.stops[legIds[i].second + 1], wetLater, 300, 1.4);
            }
        };
        int threads = (int)std::max(1u, std::thread::hardware_concurrency());
        if (parallelRowsCap > 0) threads = std::min(threads, parallelRowsCap);
        threads = std::min(threads, 12);
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; t++) pool.emplace_back(work);
        work();
        for (std::thread& t : pool) t.join();
    }
    size_t legAt = 0;
    for (const TrailPlan& plan : plans) {
        std::vector<V3> way;
        const size_t firstLeg = legAt;
        legAt += plan.stops.size() - 1;
        for (size_t s = 0; s + 1 < plan.stops.size(); s++) {
            const std::vector<V3>& leg = legs[firstLeg + s];
            if (leg.size() < 2) {
                if (std::getenv("BC_DEBUG")) std::fprintf(stderr, "regions: no way from (%.0f, %.0f) to (%.0f, %.0f) (kind %d)\n", plan.stops[s].x, plan.stops[s].z, plan.stops[s + 1].x, plan.stops[s + 1].z, plan.kind);
                way.clear();
                break;
            }
            way.insert(way.end(), leg.begin() + (way.empty() ? 0 : 1), leg.end());
        }
        if (way.size() < 3) continue;
        WayStyle st;
        RegionWay rw;
        rw.kind = plan.kind;
        int thin = 3;
        if (plan.kind == 0) st.half = 2.6, st.blend = 9, st.maxGrade = 0.14, st.window = 10, st.paint = PAINT_TRAIL, st.surface = SURF_DIRT;
        if (plan.kind == 1) st.half = 2.4, st.blend = 7, st.maxGrade = 0.18, st.window = 7, st.paint = PAINT_TRAIL, st.surface = SURF_DIRT;
        if (plan.kind == 2) st.half = 2.3, st.blend = 6, st.maxGrade = 0.22, st.window = 5, st.paint = PAINT_TRAIL, st.surface = SURF_DIRT, thin = 2;
        if (plan.kind == 3) st.half = 1.7, st.blend = 4, st.maxGrade = 0.25, st.window = 3, st.paint = PAINT_TRAIL, st.surface = SURF_DIRT;
        if (plan.kind == 4) st.half = 3.0, st.blend = 10, st.maxGrade = 0.12, st.window = 12, st.paint = PAINT_GRAVEL, st.surface = SURF_GRAVEL, thin = 4;
        std::function<void(std::vector<double>&)> shape;
        std::function<double(double)> tilt;
        if (plan.kind == 2) {
            // The pass's ledges: three flights on the way up, two on the way down (drops, going over), each of three steps.
            shape = [&rw](std::vector<double>& y) {
                const int n = (int)y.size();
                int top = 0;
                for (int k = 0; k < n; k++)
                    if (y[k] > y[top]) top = k;
                auto grade = [&](int k) { return (y[std::min(n - 1, k + 20)] - y[k]) / 20.0; };
                auto flight = [&](int from, int to, double sign, int count) {
                    int placed = 0, last = -1000;
                    for (int k = from; k < to - 30 && placed < count; k++) {
                        if (k - last < 150 || sign * grade(k) < 0.07) continue;
                        for (int q = 0; q < 3; q++) rw.steps.push_back({k + 7.0 * q, sign * (0.36 + 0.06 * q)});
                        last = k, placed++;
                    }
                };
                flight(60, top, 1, 3);
                flight(top + 40, n - 60, -1, 2);
                const std::vector<double> y0 = y;
                for (const auto& s : rw.steps) {
                    const double ramp = s.second >= 0 ? 1.5 : 0.5;
                    for (int k = 0; k < n; k++) y[k] += s.second * clampd((k - s.first) / ramp, 0, 1);
                }
                // Off-camber shelves: two stretches, clear of the ledges.
                for (double f : {0.33, 0.78}) {
                    double a = f * n;
                    bool clear = false;
                    for (int tries = 0; tries < 40 && !clear; tries++, a += 13) {
                        clear = a + 40 < n - 30;
                        for (const auto& s : rw.steps) clear &= s.first < a - 10 || s.first > a + 50;
                    }
                    if (clear) rw.camber.push_back({a, a + 40});
                }
                // The scree climb: its steepest 60 m.
                double best = 0;
                for (int k = 30; k + 60 < n - 30; k++) {
                    const double g = std::fabs(y0[k + 60] - y0[k]) / 60;
                    if (g > best) best = g, rw.screeFrom = k, rw.screeTo = k + 60;
                }
            };
            tilt = [&rw](double k) {
                for (size_t c = 0; c < rw.camber.size(); c++) {
                    const auto& cs = rw.camber[c];
                    if (k > cs.first && k < cs.second) return (c % 2 ? -0.14 : 0.14) * smoothstep(cs.first, cs.first + 6, k) * smoothstep(cs.second, cs.second - 6, k);
                }
                return 0.0;
            };
        }
        rw.pts = layWay(way, st, keep, pads, thin, shape, tilt);
        if (rw.pts.size() < 3) continue;
        regionRoads.push_back(rw.pts);
        regionWays.push_back(std::move(rw));
    }
}

void World::dressRegionTrails() {
    loadprof::Scope lp("world.regions trails dress");
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    auto dirAt = [](const std::vector<V3>& p, size_t k) {
        const size_t a = std::min(k, p.size() - 2);
        return norm(V3(p[a + 1].x - p[a].x, 0, p[a + 1].z - p[a].z));
    };
    uint32_t seed = 0x5eed2u;
    auto rnd = [&]() {
        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
        return (seed & 0xFFFFFF) / double(0x1000000);
    };
    auto sign = [&](const V3& q, double yaw, const V3& col) {
        Prop sg{PROP_SIGN, V3(q.x, height(q.x, q.z), q.z), V3(), yaw};
        sg.col[0] = (float)col.x, sg.col[1] = (float)col.y, sg.col[2] = (float)col.z;
        fixtures.push_back(sg);
        poles.push_back({q.x, q.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
    };
    const V3 colours[5] = {V3(0.1, 0.55, 0.2), V3(0.1, 0.3, 0.8), V3(0.03, 0.03, 0.03), V3(0.45, 0.35, 0.2), V3(0.1, 0.55, 0.2)};
    for (size_t w = 0; w < regionWays.size(); w++) {
        const RegionWay& rw = regionWays[w];
        const std::vector<V3>& p = rw.pts;
        const size_t n = p.size();
        const double half = rw.kind == 3 ? 1.7 : rw.kind == 4 ? 3.0 : 2.5;
        // Signs at both ends (the trail's rating), on the right going in.
        if (n > 24)
            for (int end = 0; end < 2; end++) {
                const size_t k = end ? n - 10 : 9;
                V3 d = dirAt(p, k);
                if (end) d = d * -1.0;
                const V3 right(-d.z, 0, d.x);
                const V3 q = p[k] - right * (half + 2.0);
                sign(q, dm::atan2(d.z, -d.x), colours[rw.kind]);
            }
        // Markers along it: posts, cairns where it's rocky or high.
        const double every = rw.kind == 3 ? 140 : rw.kind == 4 ? 200 : 70;
        for (double s = every * 0.5; s + 20 < n; s += every) {
            const size_t k = (size_t)s;
            const V3 d = dirAt(p, k), l(-d.z, 0, d.x);
            const V3 q = p[k] + l * ((((int)(s / every)) % 2 ? 1 : -1) * (half + 1.3));
            const int id = cellIndex(q.x, q.z);
            const bool rocky = p[k].y > 140 || (id >= 0 && (cellSurface(id) == SURF_ROCK || cellSurface(id) == SURF_SNOW || cellPaint(id) == PAINT_DESERT));
            fixtures.push_back({rocky ? PROP_CAIRN : PROP_MARKER, V3(q.x, height(q.x, q.z), q.z), V3(), rnd() * 6.28});
        }
        // Rock walls along its sides here and there (the harder trails and the pass): a row of boulders each side for 20-40 m.
        if (rw.kind == 1 || rw.kind == 2)
            for (double s = 90 + 120 * rnd(); s + 60 < n; s += 260 + 160 * rnd()) {
                const double len = 20 + 20 * rnd();
                for (double t = s; t < s + len; t += 3.2 + rnd())
                    for (int sd = -1; sd <= 1; sd += 2) {
                        if (rnd() < 0.25) continue;
                        const size_t k = (size_t)t;
                        const V3 d = dirAt(p, k), l(-d.z, 0, d.x);
                        const V3 q = p[k] + l * (sd * (half + 1.6 + 1.4 * rnd()));
                        const double r = 0.5 + 0.6 * rnd();
                        addRock(q.x, q.z, V3(r, r * 0.8, r * 0.9), r * (0.6 + 0.4 * rnd()), 3100000u + (uint32_t)(w * 997 + k), (float)(0.85 + 0.25 * rnd()));
                    }
            }
        if (rw.kind != 2) continue;
        // ---- The pass's obstacles ----
        // Its ledges: a rock slab on each lip (the profile already steps there), the face square.
        auto yAt = [&](double s) {
            const size_t a = (size_t)clampd(std::floor(s), 0, n - 2);
            return p[a].y + (p[a + 1].y - p[a].y) * clampd(s - a, 0, 1);
        };
        for (const auto& st : rw.steps) {
            const double s = st.first, thick = 1.4;
            const double g = (yAt(s + 3) - yAt(s - 3) - st.second) / 6, pitch = dm::atan(g);
            const double at = st.second >= 0 ? s + 0.75 : s - 0.35;
            const V3 c = p[(size_t)clampd(at, 0, n - 1)], d = dirAt(p, (size_t)at);
            const double top = st.second >= 0 ? yAt(s + 1.5) - g * 0.75 : yAt(s) - g * 0.35;
            addBox(V3(c.x, top - thick / 2 * dm::cos(pitch), c.z), V3(0.85, thick / 2, 2.6), dm::atan2(-d.z, d.x), pitch, 0, V3(0.48, 0.46, 0.43));
            boxes.back().rock = true;
            // (Boulders at each end of the ledge, so it isn't driven round.)
            const V3 l(-d.z, 0, d.x);
            for (int sd = -1; sd <= 1; sd += 2) {
                const V3 q = c + l * (sd * 3.6);
                addRock(q.x, q.z, V3(1.1, 0.9, 1.0), 1.0, 3200000u + (uint32_t)(s * 7) + (sd > 0), 0.95f);
            }
        }
        // Rock gardens: three stretches of 25 m with boulders on the tread, from fist-size to half a metre high.
        for (double f : {0.18, 0.47, 0.88}) {
            const double s0 = f * n;
            bool clear = true;
            for (const auto& st : rw.steps) clear &= std::fabs(st.first - s0 - 12) > 25;
            if (!clear || s0 + 30 > n) continue;
            for (int k = 0; k < 11; k++) {
                const double s = s0 + 25 * rnd();
                const size_t i = (size_t)s;
                const V3 d = dirAt(p, i), l(-d.z, 0, d.x);
                const V3 q = p[i] + l * ((rnd() * 2 - 1) * 2.0);
                const double r = 0.28 + 0.4 * rnd();
                addRock(q.x, q.z, V3(r, r * 0.8, r * 0.9), r * (0.5 + 0.5 * rnd()), 3300000u + (uint32_t)(f * 100) * 64 + k, (float)(0.85 + 0.2 * rnd()));
            }
        }
        // The boulder choke, a little past the top: two big boulders either side, a gap a truck's width and a bit.
        {
            size_t top = 0;
            for (size_t k = 0; k < n; k++)
                if (p[k].y > p[top].y) top = k;
            const size_t k = std::min(n - 30, top + 45);
            const V3 d = dirAt(p, k), l(-d.z, 0, d.x);
            for (int sd = -1; sd <= 1; sd += 2) {
                const V3 q = p[k] + l * (sd * 3.5);
                addRock(q.x, q.z, V3(2.2, 1.9, 2.0), 2.1, 3400000u + (sd > 0), 0.9f);
            }
            const V3 q = p[k + 8] + l * 0.9;
            addRock(q.x, q.z, V3(0.7, 0.55, 0.6), 0.5, 3400002u, 0.9f);
        }
        // The scree climb: loose stone on the tread.
        if (rw.screeFrom >= 0)
            for (size_t k = (size_t)rw.screeFrom; k < (size_t)rw.screeTo && k < n; k++) {
                const V3 d = dirAt(p, k), l(-d.z, 0, d.x);
                for (double lat = -2.6; lat <= 2.6; lat += 0.8) {
                    const V3 q = p[k] + l * lat;
                    const int id = cellI(q.x) * hN + cellI(q.z);
                    paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
                }
            }
    }
    regionWays.clear();
}

// ---- The regions' growth and rock (docs/plans/regions2.md) -----------------------------------------------------------------------
// The 64 m cells past the Backcountry's 6 km square (whose own growth stops there), each its own random numbers from where it is: in
// each region's country what grows there and its rock (lone trees, groves and woods, bush clumps, logs, boulders scattered and big
// standalone ones, and now and then a feature: a boulder field, an outcrop, a tor, a slab); out on the big map's natural edge, the
// same thinner.
void World::growRegionCountry(CellTiles<uint8_t>& keep) {
    loadprof::Scope lp("world.regions growth");
    constexpr double C = 64;
    const int cn = (int)std::ceil(2 * half / C);
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    auto slopeAt = [&](int i, int j) {
        const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / 2, gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / 2;
        return dm::hypot(gx, gz);
    };
    // (On several threads: each cell's growth into a list of its own, in four passes so no two cells side by side grow at once (a tree
    // keeps its distance from the ones in the cells round it that grew before); then all of it onto the map, cell by cell in order.)
    struct Grown {
        std::vector<Tree> trees;
        std::vector<Rock> rocks;
        std::vector<Log> logs;
        std::vector<Bush> bushes;
    };
    std::vector<Grown> grown((size_t)cn * cn);
    auto growCell = [&](int ci, int cj) {
            Grown& out = grown[(size_t)ci * cn + cj];
            const double x0 = -half + ci * C, z0 = -half + cj * C, cx = x0 + C / 2, cz = z0 + C / 2;
            if (std::max(std::fabs(cx), std::fabs(cz)) < kOldBackcountryHalf) return;  // (the Backcountry's own growth)
            const MapRegion* rg = regionAround(cx, cz);
            const int style = !rg ? 3 : rg->map == MAP_MOUNTAIN ? 0 : rg->map == MAP_FRONTIER ? 1 : 2;  // 3: the natural edge
            uint32_t r32 = (uint32_t)ci * 83492791u ^ (uint32_t)(cj * 2971215073u) ^ 0x7e91a3c5u;
            r32 = r32 * 2654435761u + 0x6a09e667u;
            auto rr = [&]() {
                r32 ^= r32 << 13, r32 ^= r32 >> 17, r32 ^= r32 << 5;
                return (r32 & 0xFFFFFF) / double(0x1000000);
            };
            auto open = [&](double x, double z, int& id, int& i, int& j) {
                if (std::max(std::fabs(x), std::fabs(z)) > half - 60) return false;
                i = cellI(x), j = cellI(z), id = i * hN + j;
                if (i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1) return false;
                return !keep[id] && waterY[id] < -1e8;
            };
            const double grove = smoothstep(0.36, 0.62, fbmNoise(cx * 0.0042 + 3.0, cz * 0.0042 - 8.0, 3));
            const double woods = style == 2 ? openWoods(cx, cz) : 0, canyon = style == 2 ? openCanyon(cx, cz) : 0;
            const double cold = style == 1 ? frontierCold(cx, cz) : 0;
            const uint32_t cellSeed = kWholeRockSeeds + (uint32_t)(ci * cn + cj) * 64u;  // (scenery: whole hulls, world.h)
            // ---- Trees ----
            double want = 0;
            if (style == 0) want = 3 + 24 * grove;
            else if (style == 1) want = cold > 0.5 ? 5 + 16 * grove : 0.6 + 3.5 * grove;
            else if (style == 2) want = (1 - woods) * (0.8 + 18 * grove * grove) * (1 - 0.8 * canyon) + woods * (16 + 16 * grove);
            else want = 2 + 9 * grove;
            const int wantTrees = (int)std::lround(want);
            int planted = 0;
            for (int k = 0; k < wantTrees * 2 && planted < wantTrees; k++) {
                const double x = x0 + C * rr(), z = z0 + C * rr();
                int id, i, j;
                if (!open(x, z, id, i, j)) continue;
                const bool desert = paint[id] == PAINT_DESERT;
                if ((paint[id] != PAINT_NONE && !desert) || (surf[id] != SURF_GRASS && !desert)) continue;
                const double h = hgrid[id];
                const double treeline = (style == 0 ? 175 : style == 1 ? 150 : style == 2 ? 120 : 190) + 14 * (valueNoise(x * 0.02, z * 0.02) - 0.5);
                if (h > treeline || slopeAt(i, j) > 0.78) continue;
                bool spaced = true;
                for (int a = std::max(0, ci - 1); a <= std::min(cn - 1, ci + 1) && spaced; a++)
                    for (int b = std::max(0, cj - 1); b <= std::min(cn - 1, cj + 1) && spaced; b++)
                        for (const Tree& o : grown[(size_t)a * cn + b].trees)
                            if ((o.base.x - x) * (o.base.x - x) + (o.base.z - z) * (o.base.z - z) < 3.5 * 3.5) {
                                spaced = false;
                                break;
                            }
                if (!spaced) continue;
                Tree t;
                const double q = rr();
                if (q < 0.05) t.kind = 2;
                else if (style == 0) t.kind = h < 40 && q < 0.45 ? 1 : 0;
                else if (style == 1) t.kind = cold > 0.5 ? (q < 0.12 ? 1 : 0) : 1;
                else if (style == 2) t.kind = q < 0.55 * (1 - woods) + 0.35 * woods ? 1 : 0;
                else t.kind = 0;
                t.base = V3(x, h - 0.2, z);
                t.height = t.kind == 0 ? 9 + rr() * 10 : t.kind == 1 ? 7 + rr() * 7 : 6 + rr() * 6;
                if (style == 1 && cold <= 0.5) t.height *= 0.6;  // (the badlands' few trees are small)
                t.height *= 1 - 0.3 * smoothstep(treeline - 70, treeline, h);
                t.radius = t.height * (t.kind == 0 ? 0.2 + rr() * 0.06 : 0.26 + rr() * 0.08);
                t.tint = (float)(0.8 + rr() * 0.4);
                out.trees.push_back(t);
                planted++;
            }
            // ---- Boulders: scattered, more on the steep and up high; a big standalone one (car-sized and up) in many cells ----
            const int rockTries = style == 0 ? 6 : style == 1 ? (cold > 0.5 ? 4 : 7) : style == 2 ? 2 + (int)std::lround(5 * canyon) : 1;
            for (int k = 0; k < rockTries; k++) {
                const double x = x0 + C * rr(), z = z0 + C * rr();
                int id, i, j;
                if (!open(x, z, id, i, j)) continue;
                const double slope = slopeAt(i, j), h = hgrid[id];
                if (rr() > 0.25 + 0.6 * smoothstep(0.15, 0.6, slope) + 0.2 * smoothstep(60, 200, h)) continue;
                const double rad = 0.35 + rr() * rr() * 1.0, flat = 0.5 + 0.4 * rr();
                out.rocks.push_back(makeRock(x, z, V3(rad, rad * flat, rad * (0.7 + 0.3 * rr())), rad * flat * (0.5 + 0.5 * rr()), cellSeed + (uint32_t)k, (float)(0.85 + 0.25 * rr())));
            }
            const double bigChance = style == 0 ? 0.55 : style == 1 ? 0.5 : style == 2 ? 0.25 + 0.4 * canyon : 0.12;
            if (rr() < bigChance) {
                const double x = x0 + 8 + (C - 16) * rr(), z = z0 + 8 + (C - 16) * rr();
                int id, i, j;
                if (open(x, z, id, i, j) && slopeAt(i, j) < 0.7) {
                    const double rad = 1.8 + 1.8 * rr();
                    out.rocks.push_back(makeRock(x, z, V3(rad, rad * (0.65 + 0.25 * rr()), rad * (0.75 + 0.25 * rr())), rad * (0.75 + 0.3 * rr()), cellSeed + 20, (float)(0.85 + 0.25 * rr())));
                }
            }
            // ---- Now and then a rock feature ----
            if (rr() < (style == 2 ? 0.08 + 0.12 * canyon : style == 3 ? 0.03 : 0.13)) {
                const double fx = x0 + 16 + (C - 32) * rr(), fz = z0 + 16 + (C - 32) * rr();
                int id, i, j;
                if (open(fx, fz, id, i, j)) {
                    const double pick = rr();
                    const double slope = slopeAt(i, j);
                    if (pick < 0.35) {  // a boulder field
                        for (int k = 0; k < 18; k++) {
                            const double a = rr() * 6.2832, d = 18 * std::sqrt(rr());
                            const double x = fx + dm::cos(a) * d, z = fz + dm::sin(a) * d;
                            if (!open(x, z, id, i, j)) continue;
                            const double rad = 0.3 + rr() * rr() * 1.3;
                            out.rocks.push_back(makeRock(x, z, V3(rad, rad * 0.75, rad * 0.9), rad * (0.5 + 0.4 * rr()), cellSeed + 30 + (uint32_t)k, (float)(0.85 + 0.2 * rr())));
                        }
                    } else if (pick < 0.65 || style == 1) {  // an outcrop: big rocks shouldering out of the ground together
                        const int m = 3 + (int)(rr() * 4);
                        for (int k = 0; k < m; k++) {
                            const double x = fx + (rr() * 2 - 1) * 7, z = fz + (rr() * 2 - 1) * 7;
                            if (!open(x, z, id, i, j)) continue;
                            const double rad = 2.0 + 2.0 * rr();
                            out.rocks.push_back(makeRock(x, z, V3(rad, rad * (0.6 + 0.3 * rr()), rad * 0.8), rad * (0.5 + 0.3 * rr()), cellSeed + 50 + (uint32_t)k, (float)(0.85 + 0.2 * rr())));
                        }
                    } else if (pick < 0.85 && slope < 0.4) {  // a tor: a big rock with others stacked on it
                        const double rad = 2.6 + 1.2 * rr(), top = rad * 1.1;
                        out.rocks.push_back(makeRock(fx, fz, V3(rad, rad * 0.75, rad * 0.9), top, cellSeed + 60, 0.9f));
                        double y = top;
                        for (int k = 0; k < 2; k++) {
                            const double r2 = rad * (0.55 - 0.15 * k);
                            y += r2 * 1.2;
                            out.rocks.push_back(makeRock(fx + (rr() - 0.5) * 0.8, fz + (rr() - 0.5) * 0.8, V3(r2, r2 * 0.7, r2 * 0.85), y, cellSeed + 61 + (uint32_t)k, 0.9f));
                        }
                    } else {  // a slab: a wide flat rock lying with the slope, to drive up onto
                        const V3 nr = normal(fx, fz);
                        const double hl = dm::hypot(nr.x, nr.z);
                        RockPose pose;
                        pose.yaw = hl > 1e-6 ? dm::atan2(nr.z, -nr.x) : rr() * 6.28;  // (uphill: against the normal's lean)
                        pose.pitch = dm::atan2(hl, nr.y) * 0.85, pose.roll = 0;
                        const double L = 5 + 3 * rr();
                        out.rocks.push_back(makeRock(fx, fz, V3(L, 0.7, L * (0.6 + 0.2 * rr())), 0.6, cellSeed + 70, (float)(0.9 + 0.15 * rr()), &pose));
                    }
                }
            }
            // ---- A fallen log in the woods ----
            if (grove > 0.5 && style != 1 && rr() < 0.55 * grove) {
                const double x = x0 + C * rr(), z = z0 + C * rr();
                const double r = 0.18 + 0.18 * rr(), L = 4 + 6 * rr(), a = 6.2832 * rr();
                V3 p0(x - dm::cos(a) * L / 2, 0, z - dm::sin(a) * L / 2), p1(x + dm::cos(a) * L / 2, 0, z + dm::sin(a) * L / 2);
                int id, i, j, id0, i0, j0, id1, i1, j1;
                if (open(x, z, id, i, j) && paint[id] == PAINT_NONE && open(p0.x, p0.z, id0, i0, j0) && open(p1.x, p1.z, id1, i1, j1) && hgrid[id] < 175) {
                    p0.y = height(p0.x, p0.z) + r * 0.8, p1.y = height(p1.x, p1.z) + r * 0.8;
                    out.logs.push_back({p0, p1, r});
                }
            }
            // ---- Bushes, in clumps round a few middles ----
            const int bushes0 = style == 0 ? 12 : style == 1 ? (cold > 0.5 ? 7 : 14) : style == 2 ? 13 + (int)(5 * woods) : 3;
            double mx[3], mz[3];
            for (int c = 0; c < 3; c++) mx[c] = x0 + C * rr(), mz[c] = z0 + C * rr();
            for (int k = 0; k < bushes0; k++) {
                const int c = k % 3;
                const double a = rr() * 6.2832, d = 9 * std::sqrt(rr());
                const double x = mx[c] + dm::cos(a) * d, z = mz[c] + dm::sin(a) * d;
                int id, i, j;
                if (!open(x, z, id, i, j)) continue;
                const bool desert = paint[id] == PAINT_DESERT || paint[id] == PAINT_SAND;
                if ((paint[id] != PAINT_NONE && !desert) || (surf[id] != SURF_GRASS && !desert) || hgrid[id] > 215) continue;
                const int habitat = desert || (style == 1 && cold <= 0.5) ? HAB_DESERT : style == 2 && canyon > 0.5 ? HAB_CANYON
                                    : hgrid[id] > 110 || style == 3 ? HAB_UPLAND : grove > 0.5 || woods > 0.5 ? HAB_FOREST : HAB_MEADOW;
                out.bushes.push_back(growBush(V3(x, hgrid[id], z), habitat, grove > 0.35, rr(), (float)(0.75 + 0.45 * rr())));
            }
    };
    const props::HullLibrary* hulls = props::activeRockScans();  // (each thread makes its rocks from the builder's scans)
    for (int phase = 0; phase < 4; phase++)
        parallelRows((cn + 1) / 2, [&](int h) {
            props::setActiveRockScans(hulls);
            const int ci = 2 * h + (phase & 1);
            if (ci >= cn) return;
            for (int cj = phase >> 1; cj < cn; cj += 2) growCell(ci, cj);
        }, 2);
    for (Grown& g : grown) {
        for (const Tree& t : g.trees) plant(t);
        for (Rock& k : g.rocks) rocks.push_back(std::move(k));
        logs.insert(logs.end(), g.logs.begin(), g.logs.end());
        bushes.insert(bushes.end(), g.bushes.begin(), g.bushes.end());
    }
}

World worldForPick(int pick) {
    if (!isRegionMap(pick)) return World(pick);
    World w(MAP_BACKCOUNTRY);
    w.startAtRegion(pick);
    return w;
}
