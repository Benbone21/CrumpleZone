#include "detmath.h"
#include "detsort.h"
#include "thread_cache.h"
#include "parallel.h"
#include "world.h"
#include "onemap.h"
#include "worldcache.h"
#include "realterrain.h"
#include "laptimer.h"
#include "loadprof.h"

#include "forest.h"
#include "modelkit.h"

#include "props.h"
#include "sectionfile.h"
#include "sections.h"
#include "spawnpad.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

namespace {
const double kPadRadius = World::kPadRadius;  // (the old round pad: kept for where the woods and scatter stand)
const V3 kConcrete(0.62, 0.62, 0.60);
const V3 kRampYellow(0.86, 0.62, 0.16);
const V3 kPoleWhite(0.85, 0.85, 0.82);
}  // namespace

// The test pad: one flat, paved rectangle with the surface course, the ramps and the model yard on it (a light grey grid floor).
// Outside it the land rises to the hills over kZoneRamp metres.
double World::padOutside(double x, double z) {
    return std::max(std::max(kZoneX0 - x, x - kZoneX1), std::max(kZoneZ0 - z, z - kZoneZ1));
}

const SurfaceInfo& surfaceInfo(Surface s) {
    // name, grip, bite, keep, rolling, sink, dust color
    static const SurfaceInfo table[SURF_COUNT] = {
        {"PAVEMENT", 1.00, 1.00, 0.71, 0.012, 0, {0.86f, 0.86f, 0.88f}},
        {"GRASS", 0.72, 0.70, 0.85, 0.030, 0, {0.45f, 0.42f, 0.28f}},
        {"DIRT", 0.82, 0.75, 0.90, 0.020, 0, {0.52f, 0.43f, 0.30f}},
        {"ROCK", 1.00, 1.00, 0.71, 0.012, 0, {0.62f, 0.60f, 0.57f}},
        {"SCREE", 0.60, 0.50, 0.92, 0.045, 0, {0.62f, 0.60f, 0.56f}},
        {"GRAVEL", 0.68, 0.55, 0.98, 0.030, 0, {0.60f, 0.56f, 0.50f}},
        {"SAND", 0.58, 0.40, 1.00, 0.060, 0.11, {0.80f, 0.70f, 0.50f}},
        {"MUD", 0.40, 0.45, 1.00, 0.050, 0.13, {0.30f, 0.22f, 0.14f}},
        {"SNOW", 0.32, 0.60, 0.95, 0.025, 0.09, {0.95f, 0.96f, 1.00f}},
        {"ICE", 0.10, 1.00, 0.80, 0.008, 0, {0.88f, 0.94f, 1.00f}},
        {"WET PAVEMENT", 0.68, 0.95, 0.62, 0.014, 0, {0.78f, 0.82f, 0.86f}},
    };
    return table[s < SURF_COUNT ? s : SURF_PAVED];
}

// The proving ground's surface course: six 8 m strips side by side north-west of the
// pad, 70 m long, running east: wet asphalt, gravel, sand, mud, snow, ice.
namespace {
constexpr double kCourseX0 = -118, kCourseX1 = -48, kCourseZ0 = 72, kStrip = 9.0;
constexpr Surface kCourse[] = {SURF_WET, SURF_GRAVEL, SURF_SAND, SURF_MUD, SURF_SNOW, SURF_ICE};
constexpr int kStrips = 6;
// How far (x, z) is outside the course's rectangle (negative inside).
double courseOutside(double x, double z) {
    const double dx = std::max(kCourseX0 - x, x - kCourseX1), dz = std::max(kCourseZ0 - z, z - (kCourseZ0 + kStrips * kStrip));
    return std::max(dx, dz);
}
}  // namespace

int World::courseSurface(double x, double z) const {
    if (map != MAP_PROVING_GROUND || courseOutside(x, z) > 0) return -1;
    const int k = (int)std::floor((z - kCourseZ0) / kStrip);
    if (k < 0 || k >= kStrips) return -1;
    if (z - kCourseZ0 - k * kStrip > 8.0) return -1;  // 1 m of concrete between strips
    return kCourse[k];
}

V3 World::courseStart(int surface) const {
    for (int k = 0; k < kStrips; k++)
        if (kCourse[k] == surface) return V3(kCourseX0 - 6, 0, kCourseZ0 + k * kStrip + 4.0);
    return V3(kCourseX0 - 6, 0, kCourseZ0 + 4.0);
}

const std::vector<int>& listedMaps() {
    // (All back on the list, 2026-10-18: the owner edits them. Since the one map (2026-10-25, docs/plans/one-map.md) the Mountain, the
    // Frontier and the Open Country are the Backcountry's regions: on the list as places to start on it, beside it.)
    // (A real terrain (realterrain.h) is on it last when there is one: given with --real, or found in maps/ at the first call.)
    static const std::vector<int> m = [] {
        std::vector<int> v = {MAP_PROVING_GROUND, MAP_BACKCOUNTRY, MAP_MOUNTAIN, MAP_FRONTIER, MAP_WILDS, MAP_FREEWAY, MAP_PROCEDURAL};
        if (realterrain::available()) v.push_back(MAP_REAL);
        return v;
    }();
    return m;
}

bool mapListed(int map) {
    for (int m : listedMaps())
        if (m == map) return true;
    return false;
}

const std::vector<int>& careerMaps() {
    static const std::vector<int> m = [] {
        std::vector<int> v;
        for (int x : listedMaps())
            if (x != MAP_PROCEDURAL && x != MAP_REAL) v.push_back(x);
        return v;
    }();
    return m;
}

bool careerMap(int map) {
    for (int m : careerMaps())
        if (m == map) return true;
    return false;
}

int nextListedMap(int map, int dir) {
    const std::vector<int>& l = listedMaps();
    int at = -1;
    for (int i = 0; i < (int)l.size(); i++)
        if (l[i] == map) at = i;
    if (at < 0) return l[0];
    const int n = (int)l.size();
    return l[((at + dir) % n + n) % n];
}

const char* mapName(int map) {
    if (map == MAP_REAL) return realterrain::displayName();
    return map == MAP_MOUNTAIN ? "MOUNTAIN" : map == MAP_FREEWAY ? "FREEWAY" : map == MAP_FRONTIER ? "FRONTIER" : map == MAP_BACKCOUNTRY ? "BACKCOUNTRY" : map == MAP_WILDS ? "OPEN COUNTRY" : map == MAP_PROCEDURAL ? "PROCEDURAL" : "PROVING GROUND";
}

std::string mapPickName(int map) {
    if (map == MAP_MOUNTAIN || map == MAP_FRONTIER || map == MAP_WILDS) return std::string(mapName(MAP_BACKCOUNTRY)) + ": " + mapName(map);
    return mapName(map);
}

const char* wildsKindName(int kind) {
    static const char* names[WILDS_COUNT] = {"MEADOW", "DESERT", "FOREST", "CANYON", "WETLAND", "UPLAND"};
    return kind >= 0 && kind < WILDS_COUNT ? names[kind] : "MEADOW";
}

std::vector<V3> splinePoints(const std::vector<V3>& pts, bool closed) {
    std::vector<V3> out;
    int n = (int)pts.size();
    int segs = closed ? n : n - 1;
    auto P = [&](int i) {
        if (closed) return pts[((i % n) + n) % n];
        return pts[std::min(std::max(i, 0), n - 1)];
    };
    for (int s = 0; s < segs; s++) {
        V3 p0 = P(s - 1), p1 = P(s), p2 = P(s + 1), p3 = P(s + 2);
        int steps = std::max(2, (int)(len(p2 - p1) / 2.0));
        for (int k = 0; k < steps; k++) {
            double t = (double)k / steps, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5);
        }
    }
    if (!closed) out.push_back(pts.back());
    return out;
}

namespace {
// Closest point to p on triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
V3 closestOnTriangle(const V3& p, const V3& a, const V3& b, const V3& c) {
    V3 ab = b - a, ac = c - a, ap = p - a;
    double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    V3 bp = p - b;
    double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    V3 cp = p - c;
    double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    double den = 1 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

// Convex hull of a point cloud (incremental: each point outside the hull so far
// replaces the faces it can see with a fan to their horizon). Faces wind outward.
std::vector<std::array<int, 3>> convexHull(const std::vector<V3>& p) {
    const int n = (int)p.size();
    // A well-spread starting tetrahedron: two far-apart points, the one farthest from
    // their line, the one farthest from that plane. Its centroid stays inside the hull.
    int t[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++)
        if (p[i].x < p[t[0]].x) t[0] = i;
    for (int i = 0; i < n; i++)
        if (len(p[i] - p[t[0]]) > len(p[t[1]] - p[t[0]])) t[1] = i;
    const V3 axis = norm(p[t[1]] - p[t[0]]);
    auto lineDist = [&](int i) { V3 d = p[i] - p[t[0]]; return len(d - axis * dot(d, axis)); };
    for (int i = 0; i < n; i++)
        if (lineDist(i) > lineDist(t[2])) t[2] = i;
    const V3 pn = norm(cross(p[t[1]] - p[t[0]], p[t[2]] - p[t[0]]));
    for (int i = 0; i < n; i++)
        if (std::fabs(dot(p[i] - p[t[0]], pn)) > std::fabs(dot(p[t[3]] - p[t[0]], pn))) t[3] = i;
    const V3 centroid = (p[t[0]] + p[t[1]] + p[t[2]] + p[t[3]]) * 0.25;
    struct F {
        std::array<int, 3> i;
        V3 nrm;
        double d;
        bool alive;
    };
    std::vector<F> faces;
    auto make = [&](int a, int b, int c) {
        V3 nrm = norm(cross(p[b] - p[a], p[c] - p[a]));
        if (dot(nrm, p[a] - centroid) < 0) {
            std::swap(b, c);
            nrm = -nrm;
        }
        faces.push_back({{a, b, c}, nrm, dot(nrm, p[a]), true});
    };
    make(t[0], t[1], t[2]), make(t[0], t[1], t[3]), make(t[0], t[2], t[3]), make(t[1], t[2], t[3]);
    for (int k = 0; k < n; k++) {
        if (k == t[0] || k == t[1] || k == t[2] || k == t[3]) continue;
        std::vector<std::pair<int, int>> edges;
        bool any = false;
        for (F& f : faces)
            if (f.alive && dot(f.nrm, p[k]) - f.d > 1e-9) {
                f.alive = false;
                any = true;
                for (int e = 0; e < 3; e++) edges.push_back({f.i[e], f.i[(e + 1) % 3]});
            }
        if (!any) continue;
        // Horizon: edges of the removed faces whose reverse wasn't removed too.
        for (const auto& e : edges) {
            bool shared = false;
            for (const auto& o : edges) shared |= o.first == e.second && o.second == e.first;
            if (!shared) make(e.first, e.second, k);
        }
    }
    std::vector<std::array<int, 3>> out;
    for (const F& f : faces)
        if (f.alive) out.push_back(f.i);
    return out;
}
}  // namespace

namespace {
// World::addRock with the scanned hulls on.
// The frame a pose asks for: (along, up, across), right-handed (along x up = across).
void poseFrame(const RockPose& p, V3 out[3]) {
    const double cy = dm::cos(p.yaw), sy = dm::sin(p.yaw), cp = dm::cos(p.pitch), sp = dm::sin(p.pitch);
    const V3 along(cy * cp, sp, -sy * cp), up0(-cy * sp, cp, sy * sp);
    out[0] = along, out[1] = dm::rotateAxis(up0, along, p.roll), out[2] = cross(along, out[1]);
}

std::vector<props::RockHull> splitRock(const World& w, const Rock& k, const props::RockHull& hull) {
    struct G {
        const World* w;
    } g{&w};
    auto ground = [](void* c, double x, double z) { return static_cast<G*>(c)->w->height(x, z); };
    return props::splitPlacedScan(hull.name, k.axis, k.origin, ground, &g, 0.02 + 0.008 * k.r, (int)std::min(160.0, 32 + 28 * k.r));
}

void attachRockParts(Rock& k, std::vector<props::RockHull>&& pieces) {
    k.parts.clear();
    k.parts.reserve(pieces.size());
    for (props::RockHull& ph : pieces) {
        Rock q;
        q.v = std::move(ph.v), q.f = std::move(ph.f);
        for (const auto& f : q.f) {
            const V3 nrm = norm(cross(q.v[f[1]] - q.v[f[0]], q.v[f[2]] - q.v[f[0]]));
            q.fn.push_back(nrm);
            q.fd.push_back(dot(nrm, q.v[f[0]]));
        }
        V3 cc;
        for (const V3& p : q.v) cc += p;
        q.c = cc / (double)q.v.size();
        for (const V3& p : q.v) q.r = std::max(q.r, len(p - q.c));
        k.parts.push_back(std::move(q));
    }
}

Rock makeScannedRock(const World& w, const props::HullLibrary& lib, double x, double z, const V3& radii, double top, uint32_t seed, float tint, const RockPose* pose) {
    uint32_t s = seed * 2246822519u + 0x85ebca6bu;
    auto rnd = [&]() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xFFFFFF) / double(0x1000000);
    };
    // The scans whose proportions are nearest what the map asked for (either way round:
    // the yaw is random anyway), one of the closest four by the seed. Then it's scaled to
    // the asked size on each axis (so tests and trails that size a rock still get that
    // rock), a few percent either way, so no two look alike.
    struct Cand {
        double score;
        int i;
        bool swap;
    };
    // The procedural world (procworld.h) still fits its rocks with the C library's log, std::sort and mathx's rotateAxis, so its
    // worlds stay as they were while it's being worked on: not the same on every machine yet (docs/plans/xplat-world.md).
    const bool cLibm = w.map == MAP_PROCEDURAL;
    auto fitLog = [cLibm](double v) { return cLibm ? std::log(v) : dm::log(v); };
    auto turn = [cLibm](const V3& v, const V3& k, double a) { return cLibm ? rotateAxis(v, k, a) : dm::rotateAxis(v, k, a); };
    std::vector<Cand> cands;
    const double ry = std::max(radii.y, 1e-3);
    for (int i = 0; i < (int)lib.hulls.size(); i++) {
        const V3 h = lib.hulls[i].half();
        const double hy = std::max(h.y, 1e-3);
        for (int sw = 0; sw < 2; sw++) {
            const double hx = sw ? h.z : h.x, hz = sw ? h.x : h.z;
            const double sc = std::fabs(fitLog((hx / hy) / (radii.x / ry))) + std::fabs(fitLog((hz / hy) / (radii.z / ry)));
            cands.push_back({sc, i, sw != 0});
        }
    }
    // (A scan as wide as it is long ties with itself turned round, and which of the two comes first decides how it is fitted: dm::sort,
    // libstdc++'s order for the ties on every machine, as the worlds were built before. The score's log is detmath's.)
    auto byScore = [](const Cand& a, const Cand& b) { return a.score < b.score || (a.score == b.score && a.i < b.i); };
    if (cLibm) std::sort(cands.begin(), cands.end(), byScore);
    else dm::sort(cands.begin(), cands.end(), byScore);
    std::vector<Cand> pick;
    for (const Cand& c : cands) {
        bool dup = false;
        for (const Cand& p : pick) dup |= p.i == c.i;
        if (!dup) pick.push_back(c);
        if (pick.size() == 4) break;
    }
    Cand c = pick[std::min((size_t)(rnd() * pick.size()), pick.size() - 1)];
    const bool forced = pose && pose->scan >= 0 && pose->scan < (int)lib.hulls.size();
    if (forced) c = {0, pose->scan, false};  // (a picked scan: its own way round, sized exactly as asked)
    const props::RockHull& hull = lib.hulls[c.i];
    const V3 h = hull.half(), mid((hull.bmin[0] + hull.bmax[0]) * 0.5, (hull.bmin[1] + hull.bmax[1]) * 0.5, (hull.bmin[2] + hull.bmax[2]) * 0.5);
    const double wantX = c.swap ? radii.z : radii.x, wantZ = c.swap ? radii.x : radii.z;
    V3 scale(wantX / std::max(h.x, 1e-3) * (0.94 + 0.12 * rnd()), radii.y / std::max(h.y, 1e-3) * (0.94 + 0.12 * rnd()),
             wantZ / std::max(h.z, 1e-3) * (0.94 + 0.12 * rnd()));
    if (forced) scale = V3(wantX / std::max(h.x, 1e-3), radii.y / std::max(h.y, 1e-3), wantZ / std::max(h.z, 1e-3));
    const double yaw = rnd() * 2 * M_PI, tipX = (rnd() - 0.5) * 0.2, tipZ = (rnd() - 0.5) * 0.2;
    // Model axes in the world: scaled, tipped, turned.
    V3 ax[3] = {V3(scale.x, 0, 0), V3(0, scale.y, 0), V3(0, 0, scale.z)};
    for (V3& a : ax) a = turn(turn(turn(a, V3(1, 0, 0), tipX), V3(0, 0, 1), tipZ), V3(0, 1, 0), yaw);
    if (pose) {  // (the scan's long axis, whichever of its two the fit gave the first half-size, along the pose's)
        V3 f[3];
        poseFrame(*pose, f);
        const V3 mx = c.swap ? f[2] * -1.0 : f[0], mz = c.swap ? f[0] : f[2];  // where the scan's own x and z axes go
        ax[0] = mx * scale.x, ax[1] = f[1] * scale.y, ax[2] = mz * scale.z;
    }
    auto local = [&](const V3& p) { return ax[0] * (p.x - mid.x) + ax[1] * (p.y - mid.y) + ax[2] * (p.z - mid.z); };
    double topY = -1e9;
    for (const V3& p : hull.v) topY = std::max(topY, local(p).y);
    const V3 at(x, w.height(x, z) + top - topY, z);
    Rock k;
    k.tint = tint;
    k.seed = seed;
    k.scan = c.i;
    for (int a = 0; a < 3; a++) k.axis[a] = ax[a];
    k.origin = at - (ax[0] * mid.x + ax[1] * mid.y + ax[2] * mid.z);
    k.at = at;
    for (const V3& p : hull.v) k.v.push_back(at + local(p));
    k.f = hull.f;
    for (const auto& f : k.f) {
        const V3 nrm = norm(cross(k.v[f[1]] - k.v[f[0]], k.v[f[2]] - k.v[f[0]]));
        k.fn.push_back(nrm);
        k.fd.push_back(dot(nrm, k.v[f[0]]));
    }
    V3 cen;
    for (const V3& q : k.v) cen += q;
    k.c = cen / (double)k.v.size();
    for (const V3& q : k.v) k.r = std::max(k.r, len(q - k.c));
    // A big rock is cut into the convex pieces that follow its drawn surface (a lone hull would stand cm to dm above it): while the
    // world is being made, all together at the end (on a few threads; nothing looks at them before then), else now.
    if (rockCutsIntoPieces(k)) {
        if (w.deferRockSplits) k.partsDue = true;
        else attachRockParts(k, splitRock(w, k, hull));
    }
    return k;
}
}  // namespace

Rock World::makeRock(double x, double z, const V3& radii, double top, uint32_t seed, float tint, const RockPose* pose) const {
    uint32_t s = seed * 2654435761u + 0x9e3779b9u;
    auto rnd = [&]() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xFFFFFF) / double(0x1000000);
    };
    if (const props::HullLibrary* lib = props::activeRockScans()) return makeScannedRock(*this, *lib, x, z, radii, top, seed, tint, pose);
    // Points spread evenly over the ellipsoid, then pulled in at random: deep pulls
    // cut facets, light ones keep it chunky.
    const int n = 16 + (int)(rnd() * 10);
    const double yaw = rnd() * 2 * M_PI, tipX = (rnd() - 0.5) * 0.35, tipZ = (rnd() - 0.5) * 0.35;
    std::vector<V3> pts;
    for (int i = 0; i < n; i++) {
        double y = 1 - 2 * (i + 0.5) / n, rr = std::sqrt(std::max(0.0, 1 - y * y));
        double a = i * 2.39996 + rnd() * 0.6;
        V3 d(dm::cos(a) * rr, y, dm::sin(a) * rr);
        double k = 0.72 + 0.3 * rnd();
        V3 q(d.x * radii.x * k, d.y * radii.y * k, d.z * radii.z * k);
        q = dm::rotateAxis(q, V3(1, 0, 0), tipX);
        q = dm::rotateAxis(q, V3(0, 0, 1), tipZ);
        q = dm::rotateAxis(q, V3(0, 1, 0), yaw);
        if (pose) {
            V3 f[3];
            poseFrame(*pose, f);
            const V3 raw(d.x * radii.x * k, d.y * radii.y * k, d.z * radii.z * k);
            q = f[0] * raw.x + f[1] * raw.y + f[2] * raw.z;
        }
        pts.push_back(q);
    }
    double topY = -1e9;
    for (const V3& q : pts) topY = std::max(topY, q.y);
    const V3 origin(x, height(x, z) + top - topY, z);
    Rock k;
    k.tint = tint;
    k.seed = seed;
    k.f = convexHull(pts);
    k.at = origin;
    for (const V3& q : pts) k.v.push_back(origin + q);
    for (const auto& f : k.f) {
        V3 nrm = norm(cross(k.v[f[1]] - k.v[f[0]], k.v[f[2]] - k.v[f[0]]));
        k.fn.push_back(nrm);
        k.fd.push_back(dot(nrm, k.v[f[0]]));
    }
    V3 c;
    for (const V3& q : k.v) c += q;
    k.c = c / (double)k.v.size();
    for (const V3& q : k.v) k.r = std::max(k.r, len(q - k.c));
    return k;
}

Rock& World::addRock(double x, double z, const V3& radii, double top, uint32_t seed, float tint, const RockPose* pose) {
    rocks.push_back(makeRock(x, z, radii, top, seed, tint, pose));
    return rocks.back();
}

void World::addLake(double x, double z, double r, double depth) {
    // An irregular shore: the basin's radius wanders with the bearing.
    auto shore = [&](double ang) {
        return r * (0.72 + 0.4 * valueNoise(dm::cos(ang) * 1.6 + x * 0.013, dm::sin(ang) * 1.6 + z * 0.013));
    };
    // Water stands just below the lowest point of the rim.
    double level = 1e9;
    for (int a = 0; a < 128; a++) {
        double ang = a * M_PI / 64, rr = shore(ang);
        level = std::min(level, height(x + rr * dm::cos(ang), z + rr * dm::sin(ang)));
    }
    level -= 0.15;
    const double reach = r * 1.15 + 40;
    for (int i = std::max(0, (int)((x - reach + half) / kRes)); i <= std::min(hN - 1, (int)((x + reach + half) / kRes)); i++)
        for (int j = std::max(0, (int)((z - reach + half) / kRes)); j <= std::min(hN - 1, (int)((z + reach + half) / kRes)); j++) {
            double px = -half + i * kRes, pz = -half + j * kRes, d = dm::hypot(px - x, pz - z);
            double rr = shore(dm::atan2(pz - z, px - x)), q = d / rr;
            double bed = d < rr ? level - depth * (1 - q * q) : level + (d - rr) * 0.25;
            float& hh = hgrid[i * hN + j];
            hh = (float)std::min((double)hh, bed);
        }
    waters.push_back({x, z, r * 1.15, level});
}

World::World(int m) : map(m) {
    half = halfSizeFor(map);
    // The big maps come from the world cache when this build of the game has built them before (worldcache.h).
    bool cached = false;
    {
        loadprof::Scope lpCache("world.cache read");
        cached = worldcache::load(*this);
    }
    if (cached) {
        cutCachedRocks();
    } else {
        loadprof::Scope lpGen("world.terrain+layout");
        deferRockSplits = true;
        if (map == MAP_MOUNTAIN) buildMountain();
        else if (map == MAP_FREEWAY) buildFreeway();
        else if (map == MAP_FRONTIER) buildFrontier();
        else if (map == MAP_BACKCOUNTRY) buildBackcountry();
        else if (map == MAP_WILDS) buildWilds();
        else if (map == MAP_PROCEDURAL) buildProcedural();
        else if (map == MAP_REAL) buildReal();
        else buildProvingGround();
        // The slab the player starts on (and the parked trucks): in the game's proving ground, as the other maps are always built.
        if ((map != MAP_PROVING_GROUND || withShowcase) && map != MAP_PROCEDURAL) addMapSpawnPads(*this);  // (the procedural world lays its own)
        splitPendingRocks();
        loadprof::Scope lpStore("world.cache write");
        worldcache::store(*this);
    }
    { loadprof::Scope lpGrid("world.grid"); buildGrid(); }
}

double World::halfSizeFor(int map) {
    if (map == MAP_PROCEDURAL) return 1536.0;  // (a 3 km window of it, whatever the tests ask: procworld.h kWindowHalf)
    if (halfSizeOverride > 0) return halfSizeOverride;
    // (The Backcountry: 6 x 6 km (docs/plans/big-maps.md), or 8 x 8 km with the regions stitched on round it (docs/plans/one-map.md).)
    if (map == MAP_BACKCOUNTRY) return stitchRegions ? kOneMapHalf : kOldBackcountryHalf;
    if (map == MAP_REAL) return realterrain::activeHalf();
    return kDefaultHalfSize;
}

void World::cutCachedRocks() {
    // (The world cache keeps each rock's hull, not its convex pieces: cut the big scanned ones again, from the scans' saved cuts.)
    for (Rock& k : rocks)
        if (rockCutsIntoPieces(k) && k.parts.empty()) k.partsDue = true;
    splitPendingRocks();
}

void World::splitPendingRocks() {
    deferRockSplits = false;
    std::vector<int> due;  // (rocks may have been taken away since: by index only now)
    for (int i = 0; i < (int)rocks.size(); i++)
        if (rocks[i].partsDue) due.push_back(i), rocks[i].partsDue = false;
    const props::HullLibrary* lib = props::activeRockScans();  // (what they were placed from: this thread's)
    if (due.empty() || !lib) return;
    loadprof::Scope lp("world.rock pieces");
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t i; (i = next++) < due.size();) {
            const Rock& k = rocks[due[i]];
            if (k.scan >= 0 && k.scan < (int)lib->hulls.size()) attachRockParts(rocks[due[i]], splitRock(*this, k, lib->hulls[k.scan]));
        }
    };
    // (Each rock on its own: the same pieces whichever thread cuts it.)
    const unsigned hc = std::thread::hardware_concurrency();
    const int extra = (int)std::min<size_t>(due.size() / 64, (size_t)std::min(3u, hc > 1 ? hc - 1 : 0u));
    std::vector<std::thread> helpers;
    for (int i = 0; i < extra; i++) helpers.emplace_back(work);
    work();
    for (std::thread& t : helpers) t.join();
    props::saveRockSplits();  // (the scanned rocks' cuts, if this world needed new ones: the next run reads them)
}

void World::buildProvingGround() {
    map = MAP_PROVING_GROUND;
    // Spawn is at the origin facing +X. Everything below sits on the flat test pad.
    spawn = V3(0, 0, 0);
    spawnYaw = 0;
    parking = {V3(25, 0, 12), V3(32, 0, 12), V3(39.5, 0, 12)};
    parkingYaw = M_PI / 2;
    crashProps = true;

    // Kicker ramp and landing ramp (jump line at z = -30).
    addRamp(V3(40, 0, -30), V3(53, 3.4, -30), 7, 3.6, kRampYellow);
    addRamp(V3(88, 3.4, -30), V3(104, 0, -30), 9, 3.6, kRampYellow);

    // Raised platform with an approach ramp: drive off the far end for an 8 m drop.
    addRamp(V3(-20, 0, 0), V3(-55, 8, 0), 9, 8.4, kRampYellow);
    addBox(V3(-70, 4, 0), V3(15, 4, 15), 0, 0, 0, kConcrete);

    // Crash wall straight ahead of spawn.
    addBox(V3(115, 2, 0), V3(1, 2, 22), 0, 0, 0, kConcrete);

    // Row of poles for wrapping cars around.
    for (int k = 0; k < 6; k++) {
        addBox(V3(30 + 20 * k, 4, 38), V3(0.22, 4, 0.22), 0, 0, 0, kPoleWhite);
        boxes.back().solid = false;
        poles.push_back({30.0 + 20 * k, 38, -1, 8, 0.24});
    }

    // Washboard of triangular bumps (z = -70) for suspension testing.
    for (int k = 0; k < 24; k++) addBox(V3(10 + 2.6 * k, -0.19, -70), V3(0.22, 0.22, 4), 0, M_PI / 4, 0, kConcrete);

    // Banked side ramp: drive up it on two wheels.
    addBox(V3(60, 0, 75), V3(20, 1.2, 4), 0, 0, 0.42, kRampYellow);

    // The shop, beside the spawn.
    addStation(V3(-10, 0, 24), 0, "PROVING GROUND SHOP");

    bakeTerrain();
    scatterScenery(777);
    if (withShowcase) {
        buildLapLine();
        { loadprof::Scope lp("world.model yard"); buildModelYard(); }
        { loadprof::Scope lp("world.section park"); buildSectionPark(); }
    }
}

// The start/finish line of the lap timer (laptimer.h): the checkered gate across the loop road's east side.
void World::buildLapLine() {
    Level lv;
    Gate g;
    LapTimer::linePose(*this, g.p, g.yaw);
    g.width = 14;
    lv.gates.push_back(g);
    addGateMarkers(lv, *this);
    tags.push_back({"START AND FINISH", g.p + V3(0, 6, 0)});
}

// The model yard, south-west on the pad: everything the game draws, every design of every model (not just the ones picked in
// the model lab), each with its name over it and the solid ones solid, to walk round, drive into and try out. Three
// columns side by side by size, the big structures nearest the spawn, then the mid-sized, then the small things, so a tent
// stands a few metres from the shed it is a tent next to and not across the pad. A model's designs stand side by side on a
// line, models follow one another along it, and each is spaced by its own bounds.
void World::buildModelYard() {
    const double gapX = 1.6, gapM = 3.2, gapZ = 3.2, gapCol = 8;  // between designs, between models on a line, between lines, between columns
    const double yardX = -32, yardZ = -35;                        // the east edge of the first column, and the north edge of all of them
    // Shown smaller than they're used, so they fit in the yard: (id, settings).
    auto settingsFor = [](const std::string& id) -> std::vector<std::pair<std::string, double>> {
        if (id == "sign_gantry") return {{"span", 16}};
        if (id == "race_gate") return {{"width", 10}};
        if (id == "guardrail") return {{"length", 8}};
        if (id == "jersey_barrier") return {{"length", 6}};
        return {};
    };
    auto see = [](const std::string& id) { return id != "fp_hand" && id != "lug_nut" && id != "waterfall" && id != "timber_bridge"; };  // (the falls: drawn by the water pass, on rivers; the bridges span rivers, 10-40 m: the procedural world's)
    // Things to walk through or round rather than bump into.
    auto ghost = [](const std::string& id) {
        return id == "repair_shed" || id == "fire_tower" || id == "race_gate" || id == "sign_gantry" || id == "mine_portal" || id == "tent" ||
               id == "person" || id == "campfire" || id == "tree_saver" || id == "ground_anchor" || id.rfind("tree_", 0) == 0 || id == "bush" ||
               id == "lamp_post" || id == "marker_post" || id == "trail_sign" || id == "map_board";
    };
    struct Design {
        int variant;
        std::string label;
        struct {
            V3 lo, hi;
        } b;
        double w, d;  // across the line (the model's z extent), and its depth (x)
    };
    struct Item {
        const mk::ModelDef* def;
        std::vector<Design> designs;
        double rowW = 0, rowD = 0, size = 0;
        std::vector<std::pair<std::string, double>> set;
    };
    std::vector<Item> items;
    for (const mk::ModelDef& d : mk::catalog()) {
        if (!see(d.id)) continue;
        Item it;
        it.def = &d;
        it.set = settingsFor(d.id);
        const int nv = std::max(1, (int)d.variants.size());
        for (int vi = 0; vi < nv; vi++) {
            mk::Mesh m;
            mk::P pp = d.variants.empty() ? mk::P() : mk::pickedParams(d, vi);
            for (const auto& kv : it.set) pp.v[kv.first] = kv.second;
            if (!d.variants.empty()) mk::build(d, vi, pp, m);
            if (m.v.empty()) continue;
            Design dg;
            dg.variant = vi;
            dg.label = d.title;
            if (nv > 1) dg.label += " " + std::to_string(vi + 1) + "/" + std::to_string(nv) + " " + d.variants[vi].name;
            dg.b.lo = m.lo, dg.b.hi = m.hi;
            dg.w = std::max(1.0, m.hi.z - m.lo.z), dg.d = std::max(1.0, m.hi.x - m.lo.x);
            it.rowW += dg.w + (it.designs.empty() ? 0 : gapX), it.rowD = std::max(it.rowD, dg.d);
            it.size = std::max({it.size, dg.w, dg.d, 0.6 * (double)m.hi.y});
            it.designs.push_back(std::move(dg));
        }
        if (!it.designs.empty()) items.push_back(std::move(it));
    }
    // The classes: large (fire towers, sheds, cabins), medium, small; each column as wide as makes it about square.
    const int kClasses = 3;
    auto classOf = [](double size) { return size >= 9 ? 0 : size >= 3.5 ? 1 : 2; };
    double area[kClasses] = {0, 0, 0}, widest[kClasses] = {0, 0, 0};
    for (const Item& it : items) {
        const int c = classOf(it.size);
        area[c] += (it.rowW + gapM) * (it.rowD + gapZ);
        for (const Design& dg : it.designs) widest[c] = std::max(widest[c], dg.w);
    }
    static const char* kClassName[kClasses] = {"LARGE STRUCTURES", "MEDIUM", "SMALL THINGS"};
    double colX = yardX;
    for (int c = 0; c < kClasses; c++) {
        if (area[c] <= 0) continue;
        const double W = std::max(widest[c], std::sqrt(area[c] * 1.3));
        const double x1 = colX - W;  // the column's west edge
        double x = colX, z = yardZ, lineD = 0;
        auto newLine = [&]() {
            if (lineD > 0) z -= lineD + gapZ;
            x = colX, lineD = 0;
        };
        tags.push_back({kClassName[c], V3(colX - W / 2, height(colX - W / 2, yardZ + 3) + 1.0, yardZ + 3)});
        for (const Item& it : items) {
            if (classOf(it.size) != c) continue;
            if (x < colX && x - gapM - it.rowW < x1 && it.rowW <= W) newLine();  // the whole row doesn't fit here: it starts a line
            else if (x < colX) x -= gapM;
            for (const Design& dg : it.designs) {
                if (x < colX && x - dg.w < x1) newLine();
                // Facing north, toward the spawn (front +X to world +Z, its left +Z to world -X): its width across the line is its z extent, its depth its x extent.
                const double yaw = -M_PI / 2;
                const double cx = x - dg.w / 2, cz = z - dg.d / 2;  // (the fronts in a line at z)
                const double gy = height(cx, cz);
                Placed pl;
                pl.id = it.def->id, pl.yaw = yaw, pl.set = it.set, pl.variant = dg.variant;
                pl.p = V3(cx + (dg.b.lo.z + dg.b.hi.z) / 2, gy, cz - (dg.b.lo.x + dg.b.hi.x) / 2);
                models.push_back(pl);
                tags.push_back({dg.label, V3(cx, gy + std::min((double)dg.b.hi.y, 7.0) + 0.5, cz)});
                if (!ghost(it.def->id) && dg.b.hi.y > 0.25) {  // its bounds as a collider
                    addBox(V3(cx, gy + (dg.b.lo.y + dg.b.hi.y) / 2, cz), V3(dg.d / 2 * 0.95, (dg.b.hi.y - dg.b.lo.y) / 2, dg.w / 2 * 0.95), yaw, 0, 0, V3(0.5, 0.5, 0.5));
                    boxes.back().hidden = true;
                }
                x -= dg.w + gapX;
                lineD = std::max(lineD, dg.d);
            }
            x += gapX;  // (the last design's gap isn't a gap)
        }
        colX = x1 - gapCol;
    }
    buildGrid();
}

// The section park, out beyond the loop road in the north-west where the land is open: two strips
// of ground, each section's cleared ground next to the last. The west strip runs north (-Z) and the
// north strip carries on east, so driving the sections in order goes round the corner. The dunes, a
// field 440 m long, have the band of open ground south of the loop road to themselves, driven east.
// Each has its name over its entry and is a place on the compass and map. Seed 1 of each.
namespace {
std::mutex gSessionMutex;
std::vector<std::pair<int, std::string>> gSessionSections;
}  // namespace

void World::setSessionSections(const std::vector<std::pair<int, std::string>>& sections) {
    std::lock_guard<std::mutex> l(gSessionMutex);
    gSessionSections = sections;
}

bool World::sessionSection(int type, SectionFile& out) {
    std::lock_guard<std::mutex> l(gSessionMutex);
    for (const auto& s : gSessionSections)
        if (s.first == type && parseSectionText(s.second, out)) return true;
    return false;
}

void World::buildSectionPark() {
    editStamp = levelFilesStamp();
    struct Strip {
        double x, z, heading;  // where the first section's cleared ground starts, and which way the strip runs
        std::vector<SectionType> types;
    };
    const double gap = 28;  // between one section's cleared ground and the next (more than the blend)
    const Strip strips[] = {
        {-465, 300, M_PI / 2, {SEC_STEEP_HILL, SEC_MUD_PIT, SEC_ROCKY_RIVER}},
        {-500, -455, 0, {SEC_ROCK_WALL, SEC_CLIFF_SHELF, SEC_TIMBER}},
        {-240, 460, 0, {SEC_DUNES}},  // (its middle line is z 460: the field fills the band south of the loop road)
    };
    for (const Strip& s : strips) {
        const V3 dir(dm::cos(s.heading), 0, -dm::sin(s.heading));
        double along = 0;  // from the strip's start
        for (SectionType t : s.types) {
            const SectionFootprint fp = sectionFootprint(t);
            // The player's own saved edit of that section's level, if they've made one (else the generator's, as shipped).
            SectionFile mine;
            const bool edited = useLocalEdits ? editedSectionFor(t, mine) : sessionSection((int)t, mine);  // (in a session: the host's edits, sent to us)
            const bool fromLevel = edited || builtinSectionFor(t, mine);  // (the player's edit, else the game's own level: both put on as files)
            SectionFrame f;
            f.entry = V3(s.x, 0, s.z) + dir * (along + (fromLevel ? std::max(fp.apron, -mine.u0) : fp.apron));
            f.entry.y = height(f.entry.x, f.entry.z);
            f.heading = s.heading;
            SectionBuilt b;
            double step = fp.length + 2 * fp.apron;
            if (fromLevel) {
                b = placeSection(*this, mine, f);
                step = std::max(step, std::max(fp.apron, -mine.u0) + mine.u1);
                parkEdited |= edited;
                if (edited) editedSections.push_back({(int)t, saveSectionText(mine)});
            } else {
                b = buildSection(*this, t, 1, f);
            }
            // (Start from a slab on the flat run-in, a little before the entry, facing along it.)
            const SpawnPad& pad = addSpawnPad(*this, b.entry - dir * 12.0, s.heading);
            sections.push_back({sectionName(t), (int)t, b.entry, b.exit, s.heading, pad.p, s.heading});
            tags.push_back({sectionName(t), b.entry + V3(0, 5, 0)});
            along += step + gap;
        }
    }
    // (In the order of the section types, whichever strip they're on: --spawn-section N and the tests count that way.)
    std::stable_sort(sections.begin(), sections.end(), [](const SectionSpot& a, const SectionSpot& b) { return a.type < b.type; });
}

Bush growBush(const V3& p, int habitat, bool edge, double u, float tint) {
    Bush b;
    b.p = p;
    b.kind = plantKindFor(habitat, edge, plantRoll(p.x, p.z, 1));
    b.r = plantRadius(b.kind, u);
    b.tint = tint;
    return b;
}

// Bushes round the groves and boulders out in the meadows (the proving ground's hills).
void World::scatterScenery(uint32_t seed) {
    uint32_t rng = seed;
    auto rnd = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFFFF) / double(0x1000000);
    };
    auto clearOfTrees = [&](double x, double z, double d) {
        for (const Tree& t : trees)
            if ((t.base.x - x) * (t.base.x - x) + (t.base.z - z) * (t.base.z - z) < d * d) return false;
        return true;
    };
    auto offRoad = [&](double x, double z, double margin) {
        RoadInfo r = roadAt(x, z);
        return !r.near && padOutside(x, z) > margin && std::sqrt(x * x + z * z) > kPadRadius + margin && courseOutside(x, z) > margin;
    };
    // A pond out in a meadow: the first open spot far from the pad, the roads and the trees.
    for (double pz = 380; pz > -400 && waters.empty(); pz -= 40)
        for (double px = -380; px < 400 && waters.empty(); px += 40) {
            if (dm::hypot(px, pz) < 260 || !clearOfTrees(px, pz, 60)) continue;
            bool clear = true;
            for (double a = 0; a < 2 * M_PI && clear; a += 0.5)
                for (double rr : {0.0, 30.0, 60.0}) clear &= !roadAt(px + rr * dm::cos(a), pz + rr * dm::sin(a)).near;
            if (clear) addLake(px, pz, 26, 1.6);
        }
    auto dry = [&](double x, double z, double margin) {
        for (const Water& wt : waters)
            if (dm::hypot(x - wt.x, z - wt.z) < wt.r + margin) return false;
        return true;
    };
    // Bushes: most at the edges of the woods, a few out in the open.
    for (int attempt = 0; attempt < 30000 && bushes.size() < 1800; attempt++) {
        double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        if (!offRoad(x, z, 12) || !dry(x, z, 2)) continue;
        bool nearWood = !clearOfTrees(x, z, 14);
        if (rnd() > (nearWood ? 0.8 : 0.08)) continue;
        const double u = rnd();
        bushes.push_back(growBush(V3(x, height(x, z), z), HAB_MEADOW, nearWood, u, (float)(0.8 + rnd() * 0.4)));
    }
    // More of the small plants, from hashes of the spot (the boulders' random numbers don't move).
    const size_t shrubs = bushes.size();
    for (int i = 0; i < 40000 && bushes.size() < shrubs + shrubs * 2 / 3; i++) {
        const double x = (plantRoll(i, 7, 31) * 2 - 1) * 510, z = (plantRoll(i, 11, 32) * 2 - 1) * 510;
        if (!offRoad(x, z, 12) || !dry(x, z, 2)) continue;
        const bool nearWood = !clearOfTrees(x, z, 14);
        if (plantRoll(x, z, 33) > (nearWood ? 0.8 : 0.12)) continue;
        bushes.push_back(growBush(V3(x, height(x, z), z), HAB_MEADOW, nearWood, plantRoll(x, z, 34), (float)(0.8 + plantRoll(x, z, 35) * 0.4)));
    }
    // Boulders in the meadows, clear of the trees (and the paths to them) and the roads.
    for (int attempt = 0; attempt < 20000 && rocks.size() < 140; attempt++) {
        double x = (rnd() * 2 - 1) * 500, z = (rnd() * 2 - 1) * 500;
        if (!offRoad(x, z, 45) || !clearOfTrees(x, z, 22) || !dry(x, z, 8)) continue;
        RoadInfo r = roadAt(x, z);
        if (std::fabs(r.lateral) < 18 && r.lateral != 0) continue;
        double rr = 0.5 + rnd() * rnd() * 1.6, flat = 0.55 + 0.4 * rnd();
        addRock(x, z, V3(rr, rr * flat, rr * (0.75 + 0.3 * rnd())), rr * flat * (0.5 + 0.6 * rnd()), rng, (float)(0.85 + rnd() * 0.3));
    }
}

// The wilds: open country with nothing laid out on it, for obstacle sections to sit in (levels.h: levelWorld).
// Rolling land, uneven underfoot, and the ground and growth of one WildsKind all the way out (so a section's
// own ground, which blends back into what it finds, meets country of its own kind): meadow and groves, sand
// and scrub, deep pine woods, terraced red rock, marsh, scree uplands. The meadow's land is the proving
// ground's own far out (the same hills), so levels made on the old map's open ground still sit right on it.
void World::buildWilds() {
    map = MAP_WILDS;
    crashProps = false;
    const int kind = std::min(std::max(wildsKind, 0), (int)WILDS_COUNT - 1);
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -half + i * kRes; };
    // How much of the hills each kind keeps (sand is flat, the uplands rough).
    static const double kHillAmp[WILDS_COUNT] = {1.0, 0.5, 1.0, 0.9, 0.45, 1.4};
    const double amp = kHillAmp[kind];
    hgrid.resize(N);
    parallelRows(hN, [&](int i) {
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j);
            const double hills = 9.0 * dm::sin(x * 0.012 + 0.7) * dm::cos(z * 0.010 - 0.3) + 4.0 * dm::sin(x * 0.027 + z * 0.021 + 1.3) +
                                 1.6 * dm::sin(x * 0.061 - z * 0.047) * dm::cos(z * 0.053 + x * 0.013);
            // (Calmer through the middle, where the sections lie: the ground there is for a run-in, not for hills.)
            const double calm = 0.35 + 0.65 * smoothstep(70, 200, dm::hypot(x * 0.45, z * 1.5));
            double h = hills * amp * calm + 6.0 * smoothstep(100, 240, dm::hypot(x, z));
            if (kind == WILDS_DESERT) h += 3.0 * (fbmNoise(x * 0.006 + 4.0, z * 0.006 - 2.0, 3) - 0.5);  // long swells of sand
            if (kind == WILDS_CANYON) {  // mesas: flat-topped, with cliffs round them, left out of the middle where the sections are
                const double rim = fbmNoise(x * 0.0065 + 11.0, z * 0.0065 - 4.0, 3) + 0.012 * (valueNoise(x * 0.05, z * 0.05) - 0.5);
                h += 17.0 * smoothstep(0.545, 0.556, rim) * smoothstep(170, 260, dm::hypot(x, z));
            }
            // Uneven underfoot: bumps a few metres across.
            const double bump = kind == WILDS_DESERT ? 0.3 : kind == WILDS_UPLAND ? 0.8 : 0.5;
            h += bump * (fbmNoise(x * 0.09 + 5.0, z * 0.09 + 1.0, 2) - 0.5) + 0.5 * bump * (valueNoise(x * 0.31, z * 0.27) - 0.5);
            // Hills close in far out, and keep you on the map.
            const double d = dm::hypot(std::max(0.0, std::fabs(x) - (half - 130)), std::max(0.0, std::fabs(z) - (half - 130)));
            h += 0.0045 * d * d + 0.02 * d;
            hgrid[i * hN + j] = (float)h;
        }
    });
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    surf.assign(N, SURF_GRASS);
    paint.assign(N, PAINT_NONE);
    auto slopeAt = [&](int i, int j) {
        const double hx = gridAt(std::min(i + 1, hN - 1), j) - gridAt(std::max(i - 1, 0), j);
        const double hz = gridAt(i, std::min(j + 1, hN - 1)) - gridAt(i, std::max(j - 1, 0));
        return std::sqrt(hx * hx + hz * hz) / (2 * kRes);
    };
    // The ground: what it's made of, kind by kind.
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j);
            const int id = i * hN + j;
            const double n1 = fbmNoise(x * 0.011 + 3.0, z * 0.011 - 2.0, 3), n2 = fbmNoise(x * 0.034 + 9.0, z * 0.034 + 1.0, 2);
            const double slope = slopeAt(i, j);
            switch (kind) {
                case WILDS_DESERT:  // hard dry ground with drifts of sand where the noise runs high
                    if (n1 + 0.25 * (n2 - 0.5) > 0.5) surf[id] = SURF_SAND, paint[id] = PAINT_SAND;
                    else surf[id] = SURF_DIRT, paint[id] = PAINT_DESERT;
                    break;
                case WILDS_CANYON:  // red rock on the risers and slabs, dry dirt on the treads
                    if (slope > 0.22 || n1 > 0.64) surf[id] = SURF_ROCK, paint[id] = PAINT_REDROCK;
                    else surf[id] = SURF_DIRT, paint[id] = PAINT_DESERT;
                    break;
                case WILDS_WETLAND:  // wet ground in the flat low spots (not in the middle, where the section's own mud is)
                    if (n1 < 0.34 && slope < 0.09 && dm::hypot(x, z * 2.2) > 200) surf[id] = SURF_MUD, paint[id] = PAINT_MUD;
                    break;
                case WILDS_UPLAND:  // scree on the steeper ground and in patches
                    if (slope > 0.3 || (n1 > 0.64 && n2 > 0.45)) surf[id] = SURF_SCREE, paint[id] = PAINT_SCREE;
                    break;
                default: break;
            }
        }
    spawn = V3(0, height(0, 0), 0);
    spawnYaw = 0;

    // ---- Growth ----
    uint32_t rng = 4242 + (uint32_t)kind * 977;
    auto rnd = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFFFF) / double(0x1000000);
    };
    auto cellOf2 = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    auto wet = [&](double x, double z) { return surf[cellOf2(x) * hN + cellOf2(z)] == SURF_MUD; };
    auto grove = [](double x, double z) { return 0.5 + 0.25 * dm::sin(x * 0.021 + 1.7) * dm::cos(z * 0.017 - 0.4) + 0.25 * dm::sin((x + z) * 0.013); };
    int treeMax = 0, bushMax = 0, rockMax = 0;
    switch (kind) {
        case WILDS_MEADOW: treeMax = 650, bushMax = 1000, rockMax = 140; break;
        case WILDS_DESERT: treeMax = 45, bushMax = 420, rockMax = 90; break;
        case WILDS_FOREST: treeMax = 6500, bushMax = 1500, rockMax = 140; break;
        case WILDS_CANYON: treeMax = 260, bushMax = 260, rockMax = 420; break;
        case WILDS_WETLAND: treeMax = 1300, bushMax = 1500, rockMax = 50; break;
        case WILDS_UPLAND: treeMax = 380, bushMax = 260, rockMax = 300; break;
    }
    for (int attempt = 0; attempt < 600000 && (int)trees.size() < treeMax; attempt++) {
        const double x = (rnd() * 2 - 1) * 500, z = (rnd() * 2 - 1) * 500;
        // (The woods are thickest round the middle, where you'll be, and thin out toward the rim.)
        if ((kind == WILDS_FOREST || kind == WILDS_WETLAND) && rnd() > 1 - 0.85 * smoothstep(160, 470, dm::hypot(x, z))) continue;
        const int i = cellOf2(x), j = cellOf2(z);
        const double slope = slopeAt(i, j);
        if (slope > (kind == WILDS_CANYON ? 0.2 : 0.5)) continue;
        double p = 1;
        const double wood = fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3);
        int tk = rnd() < 0.7 ? 0 : (rnd() < 0.85 ? 1 : 2);
        double hMin = 6, hMax = 13;
        switch (kind) {
            case WILDS_MEADOW: p = grove(x, z) * grove(x, z) * 1.3; break;
            case WILDS_FOREST:
                p = 0.1 + 0.9 * smoothstep(0.34, 0.5, wood);
                tk = rnd() < 0.8 ? 0 : (rnd() < 0.8 ? 1 : 2);
                hMin = 8, hMax = 17;
                break;
            case WILDS_UPLAND: p = 0.05 + 0.5 * smoothstep(0.5, 0.65, wood); tk = rnd() < 0.88 ? 0 : 2, hMin = 5, hMax = 11; break;
            case WILDS_DESERT: tk = rnd() < 0.7 ? 2 : 1, hMin = 3.5, hMax = 6.5; break;
            case WILDS_CANYON: tk = rnd() < 0.6 ? 0 : (rnd() < 0.5 ? 1 : 2), hMin = 3.5, hMax = 7; break;
            case WILDS_WETLAND:
                p = 0.1 + 0.9 * smoothstep(0.4, 0.55, wood);
                tk = rnd() < 0.2 ? 0 : (rnd() < 0.8 ? 1 : 2);
                hMin = 6, hMax = 12;
                break;
        }
        if (rnd() > p || wet(x, z)) continue;
        const double th = hMin + rnd() * (hMax - hMin);
        addTree(x, z, th, tk, (float)(0.8 + rnd() * 0.4));
    }
    auto clearOfTrees = [&](double x, double z, double d) {
        for (const Tree& t : trees)
            if ((t.base.x - x) * (t.base.x - x) + (t.base.z - z) * (t.base.z - z) < d * d) return false;
        return true;
    };
    // Bushes: most at the edges of the woods, a few out in the open (more in the marsh, fewer in the sand).
    const double bushOpen = kind == WILDS_WETLAND ? 0.3 : kind == WILDS_DESERT ? 0.6 : 0.08;
    for (int attempt = 0; attempt < 40000 && (int)bushes.size() < bushMax; attempt++) {
        const double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        const bool nearWood = kind == WILDS_DESERT ? false : !clearOfTrees(x, z, 14);
        if (rnd() > (nearWood ? 0.8 : bushOpen)) continue;
        const double u = rnd();
        bushes.push_back(growBush(V3(x, height(x, z), z), (int)kind, nearWood, u, (float)(0.8 + rnd() * 0.4)));  // (a habitat for each WildsKind)
    }
    // Undergrowth: more of the same (ferns in the woods, flowers and stones in the meadows, scrub in the sand), from hashes of the spot so none
    // of the random numbers the rocks use move.
    const size_t shrubs = bushes.size(), undergrowth = shrubs * 7 / 10;
    for (int i = 0; i < 60000 && bushes.size() < shrubs + undergrowth; i++) {
        const double x = (plantRoll(i, 7, 21) * 2 - 1) * 510, z = (plantRoll(i, 11, 22) * 2 - 1) * 510;
        const bool nearWood = kind == WILDS_DESERT ? false : !clearOfTrees(x, z, 14);
        if (plantRoll(x, z, 23) > (nearWood ? 0.8 : bushOpen)) continue;
        bushes.push_back(growBush(V3(x, height(x, z), z), (int)kind, nearWood, plantRoll(x, z, 24), (float)(0.8 + plantRoll(x, z, 25) * 0.4)));
    }
    // Boulders, clear of the trees; in the canyon and the uplands they gather at the feet of the steeper ground.
    for (int attempt = 0; attempt < 60000 && (int)rocks.size() < rockMax; attempt++) {
        const double x = (rnd() * 2 - 1) * 500, z = (rnd() * 2 - 1) * 500;
        if (wet(x, z) || !clearOfTrees(x, z, kind == WILDS_FOREST ? 6 : 14)) continue;
        if ((kind == WILDS_CANYON || kind == WILDS_UPLAND) && slopeAt(cellOf2(x), cellOf2(z)) < 0.1 && rnd() < 0.8) continue;
        const double big = kind == WILDS_CANYON ? 2.0 : 1.6;
        const double rr = 0.5 + rnd() * rnd() * big, flat = 0.55 + 0.4 * rnd();
        addRock(x, z, V3(rr, rr * flat, rr * (0.75 + 0.3 * rnd())), rr * flat * (0.5 + 0.6 * rnd()), rng, (float)(0.85 + rnd() * 0.3));
    }
}

double World::baseHeight(double x, double z) const {
    double h = 0;
    const double out = padOutside(x, z);
    if (out > 0) {
        double t = smoothstep(0, kZoneRamp, out);
        double hills = 9.0 * dm::sin(x * 0.012 + 0.7) * dm::cos(z * 0.010 - 0.3) +
                       4.0 * dm::sin(x * 0.027 + z * 0.021 + 1.3) +
                       1.6 * dm::sin(x * 0.061 - z * 0.047) * dm::cos(z * 0.053 + x * 0.013);
        h = t * (hills + 6.0 * t);
        // Flat under the surface course (and its run-up).
    }
    // Rim mountains keep you on the map.
    double ex = std::max(0.0, std::fabs(x) - 520), ez = std::max(0.0, std::fabs(z) - 520);
    h += (ex * ex + ez * ez) * 0.02;
    return h;
}

void World::bakeTerrain() {
    hN = (int)(2 * half / kRes) + 1;
    hgrid.resize(hN * hN);
    parallelRows(hN, [&](int i) {
        for (int j = 0; j < hN; j++) hgrid[i * hN + j] = (float)baseHeight(-half + i * kRes, -half + j * kRes);
    });

    // Roads: a loop through the hills and a link road from the north side of the pad.
    const std::vector<V3> loopPts = {
        {300, 0, 0},     {330, 0, 150},   {230, 0, 290},   {60, 0, 340},    {-120, 0, 300},
        {-280, 0, 330},  {-380, 0, 180},  {-330, 0, 0},    {-380, 0, -170}, {-260, 0, -320},
        {-60, 0, -300},  {100, 0, -360},  {280, 0, -280},  {360, 0, -130},
    };
    const std::vector<V3> linkPts = {{0, 0, 90}, {0, 0, 180}, {25, 0, 265}, {60, 0, 340}};
    std::vector<V3> loop = splinePoints(loopPts, true), link = splinePoints(linkPts, false);

    // Road height follows the hills, smoothed so grades stay drivable.
    auto profile = [&](std::vector<V3>& road, bool closed, int window) {
        int n = (int)road.size();
        std::vector<double> raw(n);
        for (int i = 0; i < n; i++) raw[i] = baseHeight(road[i].x, road[i].z);
        for (int i = 0; i < n; i++) {
            double sum = 0;
            int cnt = 0;
            for (int k = -window; k <= window; k++) {
                int idx = i + k;
                if (closed) idx = ((idx % n) + n) % n;
                else if (idx < 0 || idx >= n) continue;
                sum += raw[idx];
                cnt++;
            }
            road[i].y = sum / cnt;
        }
    };
    profile(loop, true, 30);
    profile(link, false, 30);
    // The link road starts flush with the pad and meets the loop at the loop's height.
    double joinY = loop[0].y, best = 1e18;
    for (const V3& p : loop) {
        double d = len(V3(p.x - link.back().x, 0, p.z - link.back().z));
        if (d < best) {
            best = d;
            joinY = p.y;
        }
    }
    for (size_t i = 0; i < link.size(); i++) {
        V3& p = link[i];
        p.y *= smoothstep(0, 50, padOutside(p.x, p.z));
        double tEnd = smoothstep(link.size() - 25.0, link.size() - 1.0, (double)i);
        p.y += (joinY - p.y) * tEnd;
    }

    // Carve: for each cell near a road keep the closest road sample, then blend.
    const double flat = kRoadHalfWidth + 2.0, blend = kRoadHalfWidth + 17.0;
    std::vector<float> dist(hN * hN, 1e9f), roadY(hN * hN, 0);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    auto carve = [&](const std::vector<V3>& road, bool closed) {
        int n = (int)road.size();
        double along = 0;
        for (int s = 0; s < (closed ? n : n - 1); s++) {
            const V3& a = road[s];
            const V3& b = road[(s + 1) % n];
            V3 d(b.x - a.x, 0, b.z - a.z);
            double L = len(d);
            if (L < 1e-6) continue;
            V3 dir = d / L;
            int i0 = std::max(0, (int)((std::min(a.x, b.x) - blend + half) / kRes));
            int i1 = std::min(hN - 1, (int)((std::max(a.x, b.x) + blend + half) / kRes) + 1);
            int j0 = std::max(0, (int)((std::min(a.z, b.z) - blend + half) / kRes));
            int j1 = std::min(hN - 1, (int)((std::max(a.z, b.z) + blend + half) / kRes) + 1);
            for (int i = i0; i <= i1; i++)
                for (int j = j0; j <= j1; j++) {
                    double x = -half + i * kRes, z = -half + j * kRes;
                    double t = clampd(((x - a.x) * dir.x + (z - a.z) * dir.z) / L, 0, 1);
                    double cx = a.x + d.x * t, cz = a.z + d.z * t;
                    double dd = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
                    int id = i * hN + j;
                    if (dd >= dist[id]) continue;
                    dist[id] = (float)dd;
                    roadY[id] = (float)(a.y + (b.y - a.y) * t);
                    roadLat[id] = (float)(dir.x * (z - a.z) - dir.z * (x - a.x));
                    roadAlong[id] = (float)(along + L * t);
                }
            along += L;
        }
    };
    carve(loop, true);
    carve(link, false);
    loopRoad = loop;
    loopAlong.resize(loop.size());
    loopLength = 0;
    for (size_t i = 0; i < loop.size(); i++) {
        loopAlong[i] = loopLength;
        const V3& a = loop[i];
        const V3& b = loop[(i + 1) % loop.size()];
        loopLength += len(V3(b.x - a.x, 0, b.z - a.z));
    }

    // Trees in groves on the hills: clear of the pad, the roads and steep slopes.
    uint32_t rng = 12345;
    auto rnd = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFFFF) / double(0x1000000);
    };
    auto grove = [](double x, double z) {  // smooth 0..1 field deciding where woods grow
        return 0.5 + 0.25 * dm::sin(x * 0.021 + 1.7) * dm::cos(z * 0.017 - 0.4) + 0.25 * dm::sin((x + z) * 0.013);
    };
    for (int attempt = 0; attempt < 20000 && trees.size() < 650; attempt++) {
        double x = (rnd() * 2 - 1) * 500, z = (rnd() * 2 - 1) * 500;
        if (padOutside(x, z) < 25 || x * x + z * z < (kPadRadius + 25) * (kPadRadius + 25)) continue;  // (the old round pad too: the same woods as ever round it)
        if (rnd() > grove(x, z) * grove(x, z) * 1.3) continue;
        int i = (int)std::lround((x + half) / kRes), j = (int)std::lround((z + half) / kRes);
        if (dist[i * hN + j] < kRoadHalfWidth + 9) continue;
        double hx = gridAt(std::min(i + 1, hN - 1), j) - gridAt(std::max(i - 1, 0), j);
        double hz = gridAt(i, std::min(j + 1, hN - 1)) - gridAt(i, std::max(j - 1, 0));
        if (std::sqrt(hx * hx + hz * hz) / (2 * kRes) > 0.55) continue;
        Tree t;
        t.base = V3(x, gridAt(i, j) - 0.2, z);
        t.height = 6 + rnd() * 7;
        t.radius = t.height * (0.22 + rnd() * 0.08);
        t.tint = (float)(0.8 + rnd() * 0.4);
        plant(t);
    }
    for (int id = 0; id < hN * hN; id++) {
        if (dist[id] >= blend) continue;
        double w = dist[id] < flat ? 1.0 : smoothstep(blend, flat, dist[id]);
        hgrid[id] = (float)(hgrid[id] + (roadY[id] - hgrid[id]) * w);
        roadNear[id] = dist[id] < kRoadHalfWidth + 4.0;
    }
}

double World::height(double x, double z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) return 0;  // (an exploded vehicle: the game resets it after the frame)
    // (The big map grew round the 6 km Backcountry (onemap.h): there the cell and the fraction across it are worked out from that map's
    // corner, as they were on it, then moved by the whole cells it grew by, so the land under a wheel is the same to the last bit. On
    // every other map `base` is its own half-size and this is as it always was.)
    const double base = map == MAP_BACKCOUNTRY && half > kOldBackcountryHalf + 1 ? kOldBackcountryHalf : half;
    const int shift = (int)std::lround((half - base) / kRes);
    double fx = (x + base) / kRes, fz = (z + base) / kRes;
    fx = clampd(fx, -shift, hN - 1.001 - shift);
    fz = clampd(fz, -shift, hN - 1.001 - shift);
    const double flx = std::floor(fx), flz = std::floor(fz);
    const int i = (int)flx + shift, j = (int)flz + shift;
    const double tx = fx - flx, tz = fz - flz;
    double a = gridAt(i, j), b = gridAt(i + 1, j), c = gridAt(i, j + 1), d = gridAt(i + 1, j + 1);
    return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
}

RoadInfo World::roadAt(double x, double z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) return RoadInfo();
    int i = (int)std::lround((x + half) / kRes), j = (int)std::lround((z + half) / kRes);
    RoadInfo r;
    if (i < 0 || j < 0 || i >= hN || j >= hN) return r;
    int id = i * hN + j;
    r.paint = paint.empty() ? (uint8_t)(roadNear[id] ? PAINT_ROAD : PAINT_NONE) : paint[id];
    r.near = roadNear[id] || r.paint != PAINT_NONE;
    if (paint.empty()) {  // the proving ground: its surface course is painted on
        const int c = courseSurface(x, z);
        static const uint8_t look[SURF_COUNT] = {PAINT_PAD, PAINT_NONE, PAINT_TRAIL, PAINT_ROCK, PAINT_SCREE, PAINT_GRAVEL,
                                                 PAINT_SAND, PAINT_MUD, PAINT_SNOW, PAINT_ICE, PAINT_WET};
        if (c >= 0) r.paint = look[c], r.near = false;
        else if (courseOutside(x, z) < 4 || (!r.near && padOutside(x, z) < 0)) r.paint = PAINT_PAD, r.near = false;
    }
    r.lateral = roadLat[id];
    r.along = roadAlong[id];
    return r;
}

Surface World::surfaceAt(double x, double z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) return SURF_PAVED;
    if (!surf.empty()) {
        int i = (int)std::lround((x + half) / kRes), j = (int)std::lround((z + half) / kRes);
        if (i < 0 || j < 0 || i >= hN || j >= hN) return SURF_ROCK;
        return (Surface)surf[i * hN + j];
    }
    const int course = courseSurface(x, z);
    if (course >= 0) return (Surface)course;
    if (padOutside(x, z) < 0 || courseOutside(x, z) < 4) return SURF_PAVED;
    RoadInfo r = roadAt(x, z);
    return r.near && std::fabs(r.lateral) < kRoadHalfWidth + 1.5 ? SURF_PAVED : SURF_GRASS;
}

float World::grassDensity(double x, double z) const {
    if (surfaceAt(x, z) != SURF_GRASS || height(x, z) < waterLevel(x, z) + 0.25) return 0;
    RoadInfo r = roadAt(x, z);
    if (r.near && r.paint != PAINT_NONE) return 0;
    double h = height(x, z);
    const double nx = map == MAP_PROCEDURAL ? x + (double)proc.ox : x, nz = map == MAP_PROCEDURAL ? z + (double)proc.oz : z;  // (its window's: where in the world)
    float d = (float)(0.35 + 0.65 * fbmNoise(nx * 0.03 + 11.0, nz * 0.03 - 4.0, 3));
    if (r.near) d *= (float)smoothstep(kRoadHalfWidth + 1.5, kRoadHalfWidth + 4.5, std::fabs(r.lateral));  // thins out toward the verge
    if (map == MAP_PROVING_GROUND) d *= (float)smoothstep(2, 12, padOutside(x, z));
    return d * (float)(1 - smoothstep(120, 190, h));  // thins out up the mountains
}

V3 World::normal(double x, double z) const {
    if (map == MAP_PROVING_GROUND && padOutside(x, z) < -5) return V3(0, 1, 0);
    const double e = 0.5;
    double dx = (height(x + e, z) - height(x - e, z)) / (2 * e);
    double dz = (height(x, z + e) - height(x, z - e)) / (2 * e);
    return norm(V3(-dx, 1, -dz));
}

bool World::contact(const V3& c, double R, const V3* axis, double hw, Contact& out, bool withPoles) const {
    if (!std::isfinite(c.x + c.y + c.z)) return false;
    // Support distance of the shape along -n (how far it reaches toward the surface).
    auto support = [&](const V3& n) {
        if (!axis) return R;
        double na = dot(n, *axis);
        return R * std::sqrt(std::max(0.0, 1 - na * na)) + hw * std::fabs(na);
    };
    bool hit = false;
    out.pen = 0;

    // Terrain.
    double h = height(c.x, c.z);
    if (c.y - h < R + hw + 0.5) {
        V3 n = normal(c.x, c.z);
        double dist = (c.y - h) * n.y;
        double pen = support(n) - dist;
        if (pen > out.pen) {
            out.pen = pen;
            out.n = n;
            out.surface = surfaceAt(c.x, c.z);
            hit = true;
        }
    }

    // Boxes.
    const double reach = R + hw;
    // Poles and trunks (vertical cylinders).
    if (withPoles)
    for (int pi : poleGrid[cellOf(c.x) * gridN + cellOf(c.z)]) {
        const Pole& p = poles[pi];
        if (c.y < p.y0 - reach || c.y > p.y1 + reach) continue;
        double hx = c.x - p.x, hz = c.z - p.z, d = std::sqrt(hx * hx + hz * hz);
        if (d > p.r + reach) continue;
        V3 n = d > 1e-9 ? V3(hx / d, 0, hz / d) : V3(1, 0, 0);
        double pen = support(n) - (d - p.r);
        if (pen > out.pen) {
            out.pen = pen;
            out.n = n;
            out.surface = SURF_PAVED;
            hit = true;
        }
    }

    for (int bi : grid[cellOf(c.x) * gridN + cellOf(c.z)]) {
        const Box& b = boxes[bi];
        if (!b.solid) continue;
        V3 d = c - b.c;
        if (dot(d, d) > (b.bound + reach) * (b.bound + reach)) continue;
        double l[3] = {dot(d, b.ax[0]), dot(d, b.ax[1]), dot(d, b.ax[2])};
        double he[3] = {b.h.x, b.h.y, b.h.z};
        bool inside = true;
        V3 q = b.c;
        for (int i = 0; i < 3; i++) {
            double ci = clampd(l[i], -he[i], he[i]);
            if (ci != l[i]) inside = false;
            q += b.ax[i] * ci;
        }
        V3 n;
        double dist;
        if (!inside) {
            V3 diff = c - q;
            dist = len(diff);
            if (dist > reach) continue;
            n = diff / dist;
        } else {
            // Center is inside: push out through the nearest face.
            int best = 0;
            double bestDepth = 1e9;
            for (int i = 0; i < 3; i++) {
                double depth = he[i] - std::fabs(l[i]);
                if (depth < bestDepth) {
                    bestDepth = depth;
                    best = i;
                }
            }
            n = b.ax[best] * (l[best] >= 0 ? 1.0 : -1.0);
            dist = -bestDepth;
        }
        double pen = support(n) - dist;
        if (pen > out.pen) {
            out.pen = pen;
            out.n = n;
            out.surface = b.rock ? SURF_ROCK : SURF_PAVED;
            hit = true;
        }
    }

    // Boulders (spheres) and logs (capsules).
    if (!rockGrid.empty()) {
        const int cell = rockCellOf(c.x) * rockN + rockCellOf(c.z);
        for (int ri : rockGrid[cell]) {
            const Rock& k = rocks[ri];
            V3 d = c - k.c;
            double dd = dot(d, d), lim = k.r + reach;
            if (dd > lim * lim) continue;
            // (A rock made of pieces: each in turn, as a rock of its own.)
            const size_t np = std::max<size_t>(1, k.parts.size());
            for (size_t pi = 0; pi < np; pi++) {
            const Rock& kp = k.parts.empty() ? k : k.parts[pi];
            if (!k.parts.empty()) {
                const V3 dp = c - kp.c;
                const double lp = kp.r + reach;
                if (dot(dp, dp) > lp * lp) continue;
            }
            // Farthest face plane: the true distance is never less, so it rejects cheaply;
            // inside the rock it's the way out.
            double far = -1e9;
            int fi = 0;
            for (size_t f = 0; f < kp.f.size(); f++) {
                double s = dot(kp.fn[f], c) - kp.fd[f];
                if (s > far) far = s, fi = (int)f;
            }
            if (far > reach) continue;
            V3 n;
            double dist;
            if (far <= 0) {
                n = kp.fn[fi];
                dist = far;
            } else {  // outside: the nearest point is on a face it can see
                double best = 1e18;
                V3 q;
                for (size_t f = 0; f < kp.f.size(); f++) {
                    if (dot(kp.fn[f], c) - kp.fd[f] <= 0) continue;
                    V3 t = closestOnTriangle(c, kp.v[kp.f[f][0]], kp.v[kp.f[f][1]], kp.v[kp.f[f][2]]);
                    double e = dot(c - t, c - t);
                    if (e < best) best = e, q = t;
                }
                dist = std::sqrt(best);
                if (dist > reach) continue;
                n = dist > 1e-9 ? (c - q) / dist : kp.fn[fi];
            }
            double pen = support(n) - dist;
            if (pen > out.pen) {
                out.pen = pen;
                out.n = n;
                out.surface = SURF_ROCK;
                hit = true;
            }
            }
        }
        for (int li : logGrid[cell]) {
            const Log& g = logs[li];
            V3 ab = g.b - g.a;
            double t = clampd(dot(c - g.a, ab) / std::max(dot(ab, ab), 1e-9), 0, 1);
            V3 d = c - (g.a + ab * t);
            double dist = len(d);
            if (dist > g.r + reach) continue;
            V3 n = dist > 1e-9 ? d / dist : V3(0, 1, 0);
            double pen = support(n) - (dist - g.r);
            if (pen > out.pen) {
                out.pen = pen;
                out.n = n;
                out.surface = SURF_GRASS;  // wet bark: slippery
                hit = true;
            }
        }
    }
    return hit;
}

double World::clearY(double x, double z, bool overPoles) const {
    double y = height(x, z);
    Contact ct;
    for (int i = 0; i < 120; i++) {
        if (!contact(V3(x, y + 1.0, z), 1.0, nullptr, 0, ct, overPoles)) return y;
        y += 0.25;
    }
    return y;
}

void World::addStation(V3 p, double yaw, const char* name) {
    Station st;
    st.p = p;
    st.yaw = yaw;
    st.name = name;
    stations.push_back(st);
    Prop pr;
    pr.kind = PROP_GARAGE;
    pr.a = p;
    pr.yaw = yaw;
    fixtures.push_back(pr);
    // Steel posts at the shed's four corners, 10 m long (drive-through) and 7.4 m wide.
    const V3 fwd(dm::cos(yaw), 0, -dm::sin(yaw)), side(dm::sin(yaw), 0, dm::cos(yaw));
    for (int a = -1; a <= 1; a += 2)
        for (int b = -1; b <= 1; b += 2) {
            const V3 q = p + fwd * (5.0 * a) + side * (3.7 * b);
            poles.push_back({q.x, q.z, p.y - 0.5, p.y + 4.6, 0.11});
        }
}

bool World::placeModel(const std::string& id, V3 p, double yaw, std::vector<std::pair<std::string, double>> set, V3 scale) {
    if (!mk::findModel(id)) return false;
    Placed pl;
    pl.id = id, pl.p = p, pl.yaw = yaw, pl.scale = scale, pl.set = std::move(set);
    models.push_back(pl);
    return true;
}

bool World::standIn(size_t fromBox, const std::string& id, V3 p, double yaw, std::vector<std::pair<std::string, double>> set) {
    if (!placeModel(id, p, yaw, std::move(set))) return false;
    for (size_t i = fromBox; i < boxes.size(); i++) boxes[i].hidden = true;
    return true;
}

void World::addBox(V3 c, V3 h, double yaw, double pitch, double roll, V3 col) {
    Box b;
    b.c = c;
    b.h = h;
    V3 ax[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
    for (V3& a : ax) {
        a = dm::rotateAxis(a, V3(1, 0, 0), roll);
        a = dm::rotateAxis(a, V3(0, 0, 1), pitch);
        a = dm::rotateAxis(a, V3(0, 1, 0), yaw);
    }
    for (int i = 0; i < 3; i++) b.ax[i] = ax[i];
    b.col[0] = (float)col.x;
    b.col[1] = (float)col.y;
    b.col[2] = (float)col.z;
    b.bound = len(h);
    boxes.push_back(b);
}

void World::addRamp(V3 from, V3 to, double width, double thick, V3 col) {
    // A box whose top face runs along the line from -> to.
    V3 d = to - from;
    double horiz = std::sqrt(d.x * d.x + d.z * d.z);
    double yaw = dm::atan2(-d.z, d.x);
    double pitch = dm::atan2(d.y, horiz);
    double L = len(d);
    V3 up = dm::rotateAxis(dm::rotateAxis(V3(0, 1, 0), V3(0, 0, 1), pitch), V3(0, 1, 0), yaw);
    V3 c = (from + to) * 0.5 - up * (thick * 0.5);
    addBox(c, V3(L * 0.5, thick * 0.5, width * 0.5), yaw, pitch, 0, col);
}

int World::rockCellOf(double v) const {
    int i = (int)((v + half) / kRockCell);
    return std::min(std::max(i, 0), rockN - 1);
}

int World::cellOf(double v) const {
    int i = (int)((v + half) / kCell);
    return std::min(std::max(i, 0), gridN - 1);
}

void World::buildGrid() {
    static std::atomic<uint64_t> stamps{0};
    stamp = ++stamps;
    gridN = (int)(2 * half / kCell);
    // (Each list in the order the objects come: what's hit first stays the same.)
    grid.build(gridN * gridN, [&](auto emit) {
        for (int bi = 0; bi < (int)boxes.size(); bi++) {
            const Box& b = boxes[bi];
            double r = b.bound + 2.0;
            for (int ix = cellOf(b.c.x - r); ix <= cellOf(b.c.x + r); ix++)
                for (int iz = cellOf(b.c.z - r); iz <= cellOf(b.c.z + r); iz++) emit(ix * gridN + iz, bi);
        }
    });
    poleGrid.build(gridN * gridN, [&](auto emit) {
        for (int pi = 0; pi < (int)poles.size(); pi++) {
            const Pole& p = poles[pi];
            double r = p.r + 2.0;
            for (int ix = cellOf(p.x - r); ix <= cellOf(p.x + r); ix++)
                for (int iz = cellOf(p.z - r); iz <= cellOf(p.z + r); iz++) emit(ix * gridN + iz, pi);
        }
    });
    // Rocks and logs: a finer grid (a boulder field has a lot of them).
    rockN = (int)(2 * half / kRockCell);
    const double margin = 1.0;  // wheel radius plus half width
    rockGrid.build(rockN * rockN, [&](auto emit) {
        for (int ri = 0; ri < (int)rocks.size(); ri++) {
            const Rock& k = rocks[ri];
            double r = k.r + margin;
            for (int ix = rockCellOf(k.c.x - r); ix <= rockCellOf(k.c.x + r); ix++)
                for (int iz = rockCellOf(k.c.z - r); iz <= rockCellOf(k.c.z + r); iz++) emit(ix * rockN + iz, ri);
        }
    });
    logGrid.build(rockN * rockN, [&](auto emit) {
        for (int li = 0; li < (int)logs.size(); li++) {
            const Log& g = logs[li];
            double r = g.r + margin;
            for (int ix = rockCellOf(std::min(g.a.x, g.b.x) - r); ix <= rockCellOf(std::max(g.a.x, g.b.x) + r); ix++)
                for (int iz = rockCellOf(std::min(g.a.z, g.b.z) - r); iz <= rockCellOf(std::max(g.a.z, g.b.z) + r); iz++)
                    emit(ix * rockN + iz, li);
        }
    });
}

void World::polesNear(const V3& mn, const V3& mx, std::vector<int>& out) const {
    for (int ix = cellOf(mn.x); ix <= cellOf(mx.x); ix++)
        for (int iz = cellOf(mn.z); iz <= cellOf(mx.z); iz++)
            for (int pi : poleGrid[ix * gridN + iz]) {
                const Pole& p = poles[pi];
                if (p.x + p.r < mn.x || p.x - p.r > mx.x || p.z + p.r < mn.z || p.z - p.r > mx.z) continue;
                if (std::find(out.begin(), out.end(), pi) == out.end()) out.push_back(pi);
            }
}

bool World::poleInside(double x, double z, const V3& fwd, double a0, double a1, double l0, double l1, double y0, double y1, double margin) const {
    if (gridN == 0) return false;
    const V3 left(fwd.z, 0, -fwd.x);  // (fwd turned left: +X forward has -Z on its left, as Frame::left)
    const double reach = std::max(std::max(std::fabs(a0), std::fabs(a1)), std::max(std::fabs(l0), std::fabs(l1))) * 1.5 + margin + 1.0;
    CZ_THREAD_CACHE(std::vector<int>, near);
    near.clear();
    polesNear(V3(x - reach, 0, z - reach), V3(x + reach, 0, z + reach), near);
    for (int pi : near) {
        const Pole& p = poles[pi];
        if (p.y1 < y0 || p.y0 > y1) continue;
        const V3 d(p.x - x, 0, p.z - z);
        const double a = dot(d, fwd), l = dot(d, left), m = p.r + margin;
        if (a > a0 - m && a < a1 + m && l > l0 - m && l < l1 + m) return true;
    }
    return false;
}

void World::solidsNear(double x0, double z0, double x1, double z1, NearbySolids& out) const {
    out.clear();
    if (gridN == 0) return;
    // Each object is in the lookup lists of every cell it reaches into, so one object turns up
    // in several cells: a generation mark per object keeps the lists free of repeats.
    CZ_THREAD_CACHE(std::vector<uint32_t>, markP);
    CZ_THREAD_CACHE(std::vector<uint32_t>, markB);
    CZ_THREAD_CACHE(std::vector<uint32_t>, markR);
    CZ_THREAD_CACHE(std::vector<uint32_t>, markL);
    static thread_local uint32_t gen = 0;
    if (markP.size() < poles.size()) markP.resize(poles.size(), 0);
    if (markB.size() < boxes.size()) markB.resize(boxes.size(), 0);
    if (markR.size() < rocks.size()) markR.resize(rocks.size(), 0);
    if (markL.size() < logs.size()) markL.resize(logs.size(), 0);
    if (++gen == 0) {
        for (auto* m : {&markP, &markB, &markR, &markL}) std::fill(m->begin(), m->end(), 0u);
        gen = 1;
    }
    for (int ix = cellOf(x0); ix <= cellOf(x1); ix++)
        for (int iz = cellOf(z0); iz <= cellOf(z1); iz++) {
            for (int pi : poleGrid[ix * gridN + iz])
                if (markP[pi] != gen) markP[pi] = gen, out.poles.push_back(pi);
            for (int bi : grid[ix * gridN + iz])
                if (markB[bi] != gen) markB[bi] = gen, out.boxes.push_back(bi);
        }
    if (rockGrid.empty()) return;
    for (int ix = rockCellOf(x0); ix <= rockCellOf(x1); ix++)
        for (int iz = rockCellOf(z0); iz <= rockCellOf(z1); iz++) {
            for (int ri : rockGrid[ix * rockN + iz])
                if (markR[ri] != gen) markR[ri] = gen, out.rocks.push_back(ri);
            for (int li : logGrid[ix * rockN + iz])
                if (markL[li] != gen) markL[li] = gen, out.logs.push_back(li);
        }
}

// ---- Editing ---------------------------------------------------------------------------

int World::cellIndex(double x, double z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) return -1;
    const int i = (int)std::lround((x + half) / kRes), j = (int)std::lround((z + half) / kRes);
    if (i < 0 || j < 0 || i >= hN || j >= hN) return -1;
    return i * hN + j;
}

V3 World::cellCenter(int id) const {
    const int i = id / hN, j = id % hN;
    return V3(i * kRes - half, hgrid[id], j * kRes - half);
}

uint8_t World::cellSurface(int id) const {
    if (!surf.empty()) return surf[id];
    const V3 c = cellCenter(id);
    return (uint8_t)surfaceAt(c.x, c.z);
}

uint8_t World::cellPaint(int id) const {
    if (!paint.empty()) return paint[id];
    const V3 c = cellCenter(id);
    return roadAt(c.x, c.z).paint;
}

void World::setCellSurface(int id, uint8_t surface, uint8_t paintLook) {
    if (surf.empty() || paint.empty()) {
        // This map works its surfaces out as it goes (the proving ground): bake them all
        // first, so an edit is one more cell like the rest.
        std::vector<uint8_t> s(hN * hN), p(hN * hN);
        for (int k = 0; k < hN * hN; k++) s[k] = cellSurface(k), p[k] = cellPaint(k);
        surf.swap(s);
        paint.swap(p);
    }
    surf[id] = surface;
    paint[id] = paintLook;
    cellEdits++;
}

void World::setCellWater(int id, float level) {
    if (waterY.empty()) {
        waterY.reset(hN, -1e9f);
        flowX.reset(hN, 0.0f);
        flowZ.reset(hN, 0.0f);
        // (Lakes defined as discs move onto the grid with it.)
        for (int k = 0; k < hN * hN; k++) {
            const V3 c = cellCenter(k);
            for (const Water& wt : waters)
                if ((c.x - wt.x) * (c.x - wt.x) + (c.z - wt.z) * (c.z - wt.z) < wt.r * wt.r) waterY[k] = (float)wt.y;
        }
    }
    waterY[id] = level;
    cellEdits++;
}

void World::addTree(double x, double z, double treeHeight, int kind, float tint) {
    Tree t;
    t.base = V3(x, height(x, z) - 0.2, z);
    t.height = treeHeight;
    t.radius = treeHeight * (kind == 1 ? 0.32 : 0.25);
    t.tint = tint;
    t.kind = kind;
    plant(t);
}

void World::plant(Tree t) {
    // Its design settled (forest.h), and a trunk that stops what the drawn one would: as thick as the model's 1 m up.
    if (t.design < 0) t.design = forest::designFor(t.kind, (uint32_t)trees.size() + 1, t.base.x, t.base.z, t.height);
    poles.push_back({t.base.x, t.base.z, t.base.y - 0.5, t.base.y + t.height * 0.6, forest::trunkRadius(t.kind, t.design, t.height)});
    trees.push_back(t);
}

void World::addLog(V3 a, V3 b, double r) { logs.push_back({a, b, r}); }

std::string World::removeNear(double x, double z, double r, int onlyKind) {
    double best = r;
    int kind = -1, idx = -1;
    auto consider = [&](double d, int k, int i) {
        if (d < best) best = d, kind = k, idx = i;
    };
    const bool any = onlyKind < 0 || onlyKind > 6;
    if (any || onlyKind == 0)
        for (int i = 0; i < (int)rocks.size(); i++) {
            const double d = dm::hypot(rocks[i].c.x - x, rocks[i].c.z - z);
            consider(any ? std::max(0.0, d - rocks[i].r * 0.5) : d, 0, i);
        }
    if (any || onlyKind == 1)
        for (int i = 0; i < (int)trees.size(); i++) consider(dm::hypot(trees[i].base.x - x, trees[i].base.z - z), 1, i);
    if (any || onlyKind == 2)
        for (int i = 0; i < (int)logs.size(); i++) {
            const V3 m = (logs[i].a + logs[i].b) * 0.5;
            consider(dm::hypot(m.x - x, m.z - z), 2, i);
        }
    if (any || onlyKind == 3)
        for (int i = 0; i < (int)boxes.size(); i++)
            if (!boxes[i].hidden) consider(dm::hypot(boxes[i].c.x - x, boxes[i].c.z - z), 3, i);
    if (any || onlyKind == 4)
        for (int i = 0; i < (int)fixtures.size(); i++) consider(dm::hypot(fixtures[i].a.x - x, fixtures[i].a.z - z), 4, i);
    if (any || onlyKind == 6)  // (a bush counts only when asked for by kind: in an old file's "any" it would steal the removal)
        if (onlyKind == 6)
            for (int i = 0; i < (int)bushes.size(); i++) consider(dm::hypot(bushes[i].p.x - x, bushes[i].p.z - z), 6, i);
    switch (kind) {
        case 6: bushes.erase(bushes.begin() + idx); return "BUSH";
        case 0: rocks.erase(rocks.begin() + idx); return "ROCK";
        case 1: {
            const Tree t = trees[idx];
            trees.erase(trees.begin() + idx);
            for (size_t k = 0; k < poles.size(); k++)
                if (dm::hypot(poles[k].x - t.base.x, poles[k].z - t.base.z) < 0.05) {
                    poles.erase(poles.begin() + k);
                    break;
                }
            return "TREE";
        }
        case 2: logs.erase(logs.begin() + idx); return "LOG";
        case 3: boxes.erase(boxes.begin() + idx); return "BLOCK";
        case 4: {
            const Prop f = fixtures[idx];
            fixtures.erase(fixtures.begin() + idx);
            if (f.kind == PROP_GARAGE) {
                // A repair shed is the station, the shed and its four corner posts: take them
                // all (the posts used to stay behind, standing in the open).
                for (size_t k = 0; k < stations.size(); k++)
                    if (dm::hypot(stations[k].p.x - f.a.x, stations[k].p.z - f.a.z) < 0.05) {
                        stations.erase(stations.begin() + k);
                        break;
                    }
                const V3 fwd(dm::cos(f.yaw), 0, -dm::sin(f.yaw)), side(dm::sin(f.yaw), 0, dm::cos(f.yaw));
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) {
                        const V3 q = f.a + fwd * (5.0 * a) + side * (3.7 * b);
                        for (size_t k = 0; k < poles.size(); k++)
                            if (dm::hypot(poles[k].x - q.x, poles[k].z - q.z) < 0.05) {
                                poles.erase(poles.begin() + k);
                                break;
                            }
                    }
                return "REPAIR SHOP";
            }
            // What a map stood under it: the cabin's and table's collision box, a fence's rail box and posts, a sign's pole(s), a stump's.
            auto dropPole = [&](double px, double pz) {
                for (size_t k = 0; k < poles.size(); k++)
                    if (dm::hypot(poles[k].x - px, poles[k].z - pz) < 0.06) {
                        poles.erase(poles.begin() + k);
                        return;
                    }
            };
            auto dropBox = [&](const V3& c) {
                for (size_t k = 0; k < boxes.size(); k++)
                    if (boxes[k].hidden && dm::hypot(boxes[k].c.x - c.x, boxes[k].c.z - c.z) < 0.06 && std::fabs(boxes[k].c.y - c.y) < 0.06) {
                        boxes.erase(boxes.begin() + k);
                        return;
                    }
            };
            switch (f.kind) {
                case PROP_CABIN: dropBox(V3(f.a.x, f.a.y + 1.7, f.a.z)); break;
                case PROP_TABLE: dropBox(V3(f.a.x, f.a.y + 0.4, f.a.z)); break;
                case PROP_FENCE: {
                    const V3 d = f.b - f.a;
                    const double L = dm::hypot(d.x, d.z);
                    dropBox((f.a + f.b) * 0.5 + V3(0, 0.62, 0));
                    if (L > 0.1)
                        for (double t = 0; t <= L + 0.01; t += 2.5) dropPole(f.a.x + d.x * (t / L), f.a.z + d.z * (t / L));
                    return "FENCE";
                }
                case PROP_SIGN: dropPole(f.a.x, f.a.z); break;
                case PROP_MAPBOARD: {
                    const V3 side(dm::sin(f.yaw), 0, dm::cos(f.yaw));
                    for (int k = -1; k <= 1; k += 2) dropPole(f.a.x + side.x * 0.8 * k, f.a.z + side.z * 0.8 * k);
                    break;
                }
                case PROP_STUMP: dropPole(f.a.x, f.a.z); break;
                default: break;
            }
            return "PROP";
        }
        default: return "";
    }
}

uint64_t World::landHash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto cm = [](double v) { return (uint64_t)(int64_t)std::llround(v * 100.0); };
    mix((uint64_t)hN);
    for (int id = 0; id < hN * hN; id++) {
        mix(cm(hgrid[id]));
        mix(cellSurface(id));
        const V3 c = cellCenter(id);
        const double wl = waterLevel(c.x, c.z);
        mix(wl < -1e8 ? 0xffffu : cm(wl));
    }
    mix(trees.size()), mix(rocks.size()), mix(logs.size()), mix(bushes.size()), mix(boxes.size());
    auto at = [&](const V3& p) { mix(cm(p.x)), mix(cm(p.z)); };
    for (const Tree& t : trees) at(t.base);
    for (const Rock& r : rocks) at(r.c);
    for (const Log& l : logs) at(l.a);
    for (const Box& b : boxes) at(b.c);
    for (const Placed& m : models) at(m.p);
    for (const Station& s : stations) at(s.p);
    for (const Falls& f : falls) at(f.lip);
    return h;
}

void World::trimForServer() {
    std::vector<uint8_t>().swap(paint);
    std::vector<Bush>().swap(bushes);
    std::vector<Tag>().swap(tags);
    for (Rock& k : rocks) std::vector<Rock>().swap(k.parts);
}
