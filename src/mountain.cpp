// The mountain map: a trailhead in a valley under a big massif.
//
//  - A dirt trail spirals 1.3 km round the massif to a lookout near the summit, with
//    rocks along it and a ford through the dry creek.
//  - A gravel loop road runs round the valley (AI traffic drives it).
//  - West of the trailhead, a crawl park: a twister (offset mounds that lift diagonal
//    wheels), a boulder field, rock ledges, log crossings, a V-notch and a slickrock dome.
//  - East of it, a whoops lane.
//  - Pine forest up to the treeline, broadleaf trees and bushes in the valley, boulders
//    on the slopes, bare rock and snow up top.
//
// Coordinates: x east, z north, the trailhead at (-20, -330). Physics resolution is the
// usual 1 m height grid; small rock features are boulders (spheres), slabs (boxes) and
// logs (capsules) on top of it.
#include <cmath>
#include <string>

#include "world.h"

namespace {
const double kPeakX = 40, kPeakZ = 260, kMassifR = 440, kMassifH = 205;
const double kLotX = -20, kLotZ = -330;
const double kParkX0 = -235, kParkX1 = -52, kParkZ0 = -432, kParkZ1 = -322;  // crawl park floor
const double kDomeX = -205, kDomeZ = -380, kDomeR = 26, kDomeH = 10;
const double kTwisterX = -70, kBoulderX = -95, kLedgeX = -120, kLogX = -145, kNotchX = -168;
const double kLaneZ0 = -425, kLaneZ1 = -335;

// The summit trail's spiral: radius r0 e^(-k t) at angle -90 deg + t, one full turn.
const double kSpiralR0 = kPeakZ + 125, kSpiralR1 = 92, kSpiralK = std::log(kSpiralR0 / kSpiralR1) / (2 * M_PI);
V3 spiralAt(double t) {
    const double th = -M_PI / 2 + t, r = kSpiralR0 * std::exp(-kSpiralK * t);
    return V3(kPeakX + r * std::cos(th), 0, kPeakZ + r * std::sin(th));
}
V3 spiralDir(double t) {  // uphill along the trail
    const double th = -M_PI / 2 + t;
    return norm(V3(-kSpiralK * std::cos(th) - std::sin(th), 0, -kSpiralK * std::sin(th) + std::cos(th)));
}

// Widowmaker Spur: a knife-edge ridge running out east from the summit trail (where it
// passes the east face, ~270 m from the peak) to a pinnacle. It holds its height while the
// mountain falls away under it, so both sides drop further the further out you go.
const double kSpurT = 13 * 0.12;   // a spiral control point, so the trail passes exactly through it
const double kSpurLen = 160;       // to the pinnacle's center
const double kPinnacleR = 10;
V3 spurOut() {  // outward from the peak at the junction
    const double th = -M_PI / 2 + kSpurT;
    return V3(std::cos(th), 0, std::sin(th));
}
V3 spurStart() { return spiralAt(kSpurT) + spurOut() * 2.4; }  // the trail's outer edge
// Crest height above the junction, flat half width, and the crest's cross-slope (rise
// per meter to the left of the way out) at s meters out.
struct SpurAt {
    double dh, half, tilt;
    uint8_t surf;
};
SpurAt spurAt(double s) {
    struct K { double s, dh; };
    static const K keys[] = {{0, 0},      {12, -0.5},  {40, -6.0},  {54, -6.0},  {78, 1.5},   {80, 1.5},  {80.01, 1.95},
                             {88, 1.95}, {88.01, 2.45}, {100, 1.0}, {128, -3.5}, {150, 0.5},  {200, 0.5}};
    SpurAt a{0, 2.0, 0, SURF_ROCK};
    for (size_t k = 0; k + 1 < sizeof(keys) / sizeof(keys[0]); k++)
        if (s <= keys[k + 1].s || k + 2 == sizeof(keys) / sizeof(keys[0])) {
            const double u = clampd((s - keys[k].s) / (keys[k + 1].s - keys[k].s), 0, 1);
            a.dh = keys[k].dh + (keys[k + 1].dh - keys[k].dh) * u;
            break;
        }
    if (s > 41 && s < 58) a.half = 1.6;                  // the knife edge: 3.2 m of rock
    if (s > 56 && s < 76) a.tilt = 0.06;                 // off-camber slab
    if (s > 76 && s < 100) a.tilt = -0.13;               // and the other way
    if ((s > 15 && s < 40) || (s > 104 && s < 116)) a.surf = SURF_SCREE;  // loose going down
    return a;
}
const double kSpurSteps[2] = {80, 88};  // rock ledges across the crest (going out: up)

// Boulder Gully: a ravine choked with boulders and ledges, up the west peak from the
// crawl park.
const V3 kWestPeak(-330, 0, 140);
V3 gullyUp() { return norm(kWestPeak - V3(-235, 0, -322)); }
V3 gullyFoot() { return kWestPeak - gullyUp() * 235; }
const double kGullyLen = 110;
const double kGullySteps[3] = {25, 55, 88};
const double kGullyStepH[3] = {0.4, 0.45, 0.4};

// Repair stations.
const V3 kTrailheadShop(-36, 0, -342), kParkPit(-222, 0, -338);
const double kHutT = kSpurT - 0.11;  // ~30 m before the spur, on the outside of the trail
V3 hutOut() { return V3(std::cos(-M_PI / 2 + kHutT), 0, std::sin(-M_PI / 2 + kHutT)); }
V3 halfwayHut() { return spiralAt(kHutT) + hutOut() * 9.6; }

struct Rng {
    uint32_t s;
    double operator()() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xFFFFFF) / double(0x1000000);
    }
    double range(double a, double b) { return a + (b - a) * (*this)(); }
};

// A path carved into the terrain: flat across its width, blending into the slopes.
struct Path {
    std::vector<V3> pts;
    bool closed;
    double half;     // flat half width (m)
    uint8_t paint;
};
}  // namespace

// The twister's mounds, used for the height and by the tests.
static double twisterBump(double x, double z) {
    double h = 0;
    for (int k = 0; k < 14; k++) {
        double mz = kLaneZ0 + 12 + 3.2 * k, mx = kTwisterX + (k % 2 ? 1.15 : -1.15);
        double amp = 0.5 + 0.022 * k;  // 0.5 m rising to 0.79 m
        double dd = ((x - mx) * (x - mx) + (z - mz) * (z - mz)) / (1.35 * 1.35);
        if (dd < 9) h += amp * std::exp(-dd);
    }
    return h;
}

double World::mountainHeight(double x, double z) const {
    // Valley floor: gentle rolls.
    double h = 3.5 * std::sin(x * 0.011 + 0.4) * std::cos(z * 0.013 - 1.1) + 1.5 * std::sin((x - z) * 0.027);
    // The massif: a cone with ridged flanks...
    double d = std::hypot(x - kPeakX, z - kPeakZ);
    double cone = std::max(0.0, 1 - d / kMassifR);
    h += kMassifH * std::pow(cone, 1.4) * (0.85 + 0.3 * ridgedNoise(x * 0.006 + 2.0, z * 0.006 - 1.0, 4));
    // ...and a lower peak to the west.
    double d2 = std::hypot(x + 330, z - 140);
    double cone2 = std::max(0.0, 1 - d2 / 260);
    h += 140 * std::pow(cone2, 1.8) * (0.8 + 0.4 * ridgedNoise(x * 0.008 - 4.0, z * 0.008 + 3.0, 4));
    // Rim mountains keep you on the map.
    double ex = std::max(0.0, std::fabs(x) - 520), ez = std::max(0.0, std::fabs(z) - 520);
    h += (ex * ex + ez * ez) * 0.02;
    return h;
}

void World::buildMountain() {
    map = MAP_MOUNTAIN;
    crashProps = false;
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    hgrid.resize(N);
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) hgrid[i * hN + j] = (float)mountainHeight(-half + i * kRes, -half + j * kRes);
    auto X = [&](int i) { return -half + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };

    // ---- Paths ----
    // The summit trail: from the trailhead north to the foot of the massif, then a
    // logarithmic spiral (constant share of the radius per radian) round it, so the grade
    // grows from ~5% to ~18% as the cone steepens.
    std::vector<V3> trailCtl = {{kLotX + 10, 0, kLotZ + 26}, {0, 0, -260}, {25, 0, -185}, {38, 0, -140}};
    const double r0 = kPeakZ + 125, r1 = 92, turns = 2 * M_PI, k = std::log(r0 / r1) / turns;
    for (double t = 0.12; t <= turns; t += 0.12) {
        double th = -M_PI / 2 + t, r = r0 * std::exp(-k * t);
        trailCtl.push_back(V3(kPeakX + r * std::cos(th), 0, kPeakZ + r * std::sin(th)));
    }
    Path trail{splinePoints(trailCtl, false), false, 2.4, PAINT_TRAIL};
    // The valley loop road (gravel; traffic drives it).
    std::vector<V3> loopCtl;
    for (int s = 0; s < 16; s++) {
        double a = 2 * M_PI * s / 16;
        loopCtl.push_back(V3(-10 + 290 * std::cos(a), 0, -365 + 135 * std::sin(a)));
    }
    Path loop{splinePoints(loopCtl, true), true, kRoadHalfWidth, PAINT_ROAD};
    // The track from the crawl park's north edge, across the creek, to the foot of the gully.
    Path gullyTrack{splinePoints({{-205, 0, -324}, {-238, 0, -268}, {-262, 0, -205}, {-276, 0, -145}, gullyFoot()}, false), false, 2.4,
                    PAINT_TRAIL};

    // Heights: the terrain along the path, smoothed, with the grade limited.
    auto profile = [&](Path& p, int window, double maxGrade) {
        const int n = (int)p.pts.size();
        std::vector<double> raw(n);
        for (int i = 0; i < n; i++) raw[i] = height(p.pts[i].x, p.pts[i].z);
        for (int i = 0; i < n; i++) {
            double sum = 0;
            int cnt = 0;
            for (int w = -window; w <= window; w++) {
                int idx = i + w;
                if (p.closed) idx = ((idx % n) + n) % n;
                else if (idx < 0 || idx >= n) continue;
                sum += raw[idx];
                cnt++;
            }
            p.pts[i].y = sum / cnt;
        }
        for (int pass = 0; pass < 2; pass++)
            for (int s = 1; s < n; s++) {
                int i = pass ? n - 1 - s : s, prev = pass ? i + 1 : i - 1;
                double ds = std::hypot(p.pts[i].x - p.pts[prev].x, p.pts[i].z - p.pts[prev].z);
                p.pts[i].y = clampd(p.pts[i].y, p.pts[prev].y - maxGrade * ds, p.pts[prev].y + maxGrade * ds);
            }
    };
    profile(loop, 30, 0.10);
    profile(trail, 12, 0.24);
    profile(gullyTrack, 10, 0.2);
    // The trail starts level with the parking lot.
    const double lotY = height(kLotX, kLotZ);
    for (size_t i = 0; i < trail.pts.size(); i++)
        trail.pts[i].y += (lotY - trail.pts[i].y) * (1 - smoothstep(0, 30, (double)i));

    // Carve: each cell takes the path whose edge is nearest, flat across the width and
    // blending into the terrain over ~14 m.
    std::vector<float> edge(N, 1e9f), pathY(N, 0);
    std::vector<uint8_t> pathPaint(N, 0);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    std::vector<float> trailDist(N, 1e9f);
    auto carve = [&](const Path& p) {
        const double reach = p.half + 15;
        const int n = (int)p.pts.size();
        double along = 0;
        for (int s = 0; s < (p.closed ? n : n - 1); s++) {
            const V3& a = p.pts[s];
            const V3& b = p.pts[(s + 1) % n];
            V3 d(b.x - a.x, 0, b.z - a.z);
            double L = len(d);
            if (L < 1e-6) continue;
            V3 dir = d / L;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    double x = X(i), z = X(j);
                    double t = clampd(((x - a.x) * dir.x + (z - a.z) * dir.z) / L, 0, 1);
                    double cx = a.x + d.x * t, cz = a.z + d.z * t;
                    double dd = std::hypot(x - cx, z - cz);
                    int id = i * hN + j;
                    if (p.paint == PAINT_TRAIL) trailDist[id] = std::min(trailDist[id], (float)dd);
                    if (dd - p.half >= edge[id]) continue;
                    edge[id] = (float)(dd - p.half);
                    pathY[id] = (float)(a.y + (b.y - a.y) * t);
                    pathPaint[id] = p.paint;
                    roadLat[id] = (float)(dir.x * (z - a.z) - dir.z * (x - a.x));
                    roadAlong[id] = (float)(along + L * t);
                }
            along += L;
        }
    };
    carve(loop);
    carve(trail);
    carve(gullyTrack);
    for (int id = 0; id < N; id++) {
        if (edge[id] > 15) continue;
        double w = edge[id] < 1.5 ? 1.0 : smoothstep(15, 1.5, edge[id]);
        hgrid[id] = (float)(hgrid[id] + (pathY[id] - hgrid[id]) * w);
        if (edge[id] < 0.6) {
            paint[id] = pathPaint[id];
            surf[id] = SURF_DIRT;
            roadNear[id] = pathPaint[id] == PAINT_ROAD;
        }
    }

    // ---- Trailhead lot and crawl park: level gravel and dirt pads ----
    auto levelRect = [&](double x0, double x1, double z0, double z1, double y, double blend, uint8_t pnt) {
        for (int i = cellI(x0 - blend); i <= cellI(x1 + blend); i++)
            for (int j = cellI(z0 - blend); j <= cellI(z1 + blend); j++) {
                double x = X(i), z = X(j);
                double dx = std::max({x0 - x, x - x1, 0.0}), dz = std::max({z0 - z, z - z1, 0.0});
                double d = std::hypot(dx, dz);
                int id = i * hN + j;
                double w = smoothstep(blend, 0, d);
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * w);
                if (d < 0.5) {
                    paint[id] = pnt;
                    surf[id] = SURF_DIRT;
                }
            }
    };
    levelRect(kLotX - 30, kLotX + 30, kLotZ - 22, kLotZ + 22, lotY, 14, PAINT_GRAVEL);
    const double parkY = lotY - 0.3;
    levelRect(kParkX0, kParkX1, kParkZ0, kParkZ1, parkY, 18, PAINT_TRAIL);

    // ---- Crawl features on the park floor ----
    // Slickrock dome: smooth granite, up to ~30 degrees.
    for (int i = cellI(kDomeX - kDomeR); i <= cellI(kDomeX + kDomeR); i++)
        for (int j = cellI(kDomeZ - kDomeR); j <= cellI(kDomeZ + kDomeR); j++) {
            double d = std::hypot(X(i) - kDomeX, X(j) - kDomeZ) / kDomeR;
            if (d >= 1) continue;
            int id = i * hN + j;
            double q = 1 - d * d;
            hgrid[id] += (float)(kDomeH * q * q * (1 + 0.04 * (valueNoise(X(i) * 0.3, X(j) * 0.3) - 0.5)));
            if (d < 0.97) {
                paint[id] = PAINT_ROCK;
                surf[id] = SURF_ROCK;
            }
        }
    // Twister: offset mounds that put diagonal wheels in the air.
    for (int i = cellI(kTwisterX - 6); i <= cellI(kTwisterX + 6); i++)
        for (int j = cellI(kLaneZ0); j <= cellI(kLaneZ1 + 10); j++) hgrid[i * hN + j] += (float)twisterBump(X(i), X(j));
    // V-notch: a trench between two rows of big rocks (below).
    for (int i = cellI(kNotchX - 4); i <= cellI(kNotchX + 4); i++)
        for (int j = cellI(kLaneZ0 + 10); j <= cellI(kLaneZ0 + 50); j++) {
            double u = std::fabs(X(i) - kNotchX), v = X(j) - (kLaneZ0 + 10);
            double ramp = smoothstep(0, 6, v) * smoothstep(40, 34, v);
            hgrid[i * hN + j] -= (float)(1.0 * smoothstep(3.5, 0.5, u) * ramp);
        }
    // Whoops lane east of the lot: rolling bumps to fly over.
    for (int i = cellI(60); i <= cellI(74); i++)
        for (int j = cellI(-430); j <= cellI(-300); j++) {
            double x = X(i), z = X(j);
            double across = smoothstep(7, 4, std::fabs(x - 67)), along = smoothstep(-430, -420, z) * smoothstep(-300, -310, z);
            int id = i * hN + j;
            hgrid[id] += (float)(0.55 * (1 - std::cos((z + 430) * 2 * M_PI / 9.0)) * 0.5 * across * along);
            if (across > 0.5) {
                paint[id] = PAINT_TRAIL;
                surf[id] = SURF_DIRT;
            }
        }

    // ---- Dry creek across the valley: a rocky channel (the trail fords it) ----
    std::vector<V3> creek = splinePoints({{-560, 0, -150}, {-380, 0, -190}, {-250, 0, -215}, {-100, 0, -190},
                                          {20, 0, -200}, {150, 0, -235}, {300, 0, -215}, {560, 0, -170}},
                                         false);
    std::vector<float> creekDist(N, 1e9f);
    for (size_t s = 0; s + 1 < creek.size(); s++) {
        const V3& a = creek[s];
        const V3& b = creek[s + 1];
        V3 d(b.x - a.x, 0, b.z - a.z);
        double L = len(d);
        for (int i = cellI(std::min(a.x, b.x) - 9); i <= cellI(std::max(a.x, b.x) + 9); i++)
            for (int j = cellI(std::min(a.z, b.z) - 9); j <= cellI(std::max(a.z, b.z) + 9); j++) {
                double t = clampd(((X(i) - a.x) * d.x + (X(j) - a.z) * d.z) / (L * L), 0, 1);
                double dd = std::hypot(X(i) - (a.x + d.x * t), X(j) - (a.z + d.z * t));
                creekDist[i * hN + j] = std::min(creekDist[i * hN + j], (float)dd);
            }
    }
    for (int id = 0; id < N; id++) {
        if (creekDist[id] > 8.5 || roadNear[id]) continue;  // the loop road bridges it (a culvert)
        hgrid[id] -= (float)(1.4 * smoothstep(8.5, 3.0, creekDist[id]));
        if (creekDist[id] < 4.0) {
            surf[id] = SURF_GRAVEL;
            if (paint[id] == PAINT_NONE) paint[id] = PAINT_GRAVEL;
            // The middle of the bed holds the wet: sticky mud (the ford is packed gravel).
            if (creekDist[id] < 2.6 && trailDist[id] > 7 && paint[id] == PAINT_GRAVEL) surf[id] = SURF_MUD, paint[id] = PAINT_MUD;
        }
    }

    // A lake in the valley east of the trailhead, with a sandy beach round it.
    addLake(195, -385, 45, 2.2);
    {
        const Water& lake = waters.back();
        for (int i = 0; i < hN; i++)
            for (int j = 0; j < hN; j++) {
                const int id = i * hN + j;
                if (std::hypot(X(i) - lake.x, X(j) - lake.z) > lake.r + 25 || paint[id] != PAINT_NONE) continue;
                const double above = hgrid[id] - lake.y;
                if (above > -0.6 && above < 0.9 + 0.5 * valueNoise(X(i) * 0.15, X(j) * 0.15)) surf[id] = SURF_SAND, paint[id] = PAINT_SAND;
            }
    }

    // ---- The hard lines ----
    // Cells the scenery scatter must leave alone (crests, gully floor, pads).
    std::vector<uint8_t> keep(N, 0);
    auto levelDisc = [&](const V3& c, double r, double y, double blend, uint8_t pnt, uint8_t sf) {
        for (int i = cellI(c.x - r - blend); i <= cellI(c.x + r + blend); i++)
            for (int j = cellI(c.z - r - blend); j <= cellI(c.z + r + blend); j++) {
                const double d = std::hypot(X(i) - c.x, X(j) - c.z);
                const int id = i * hN + j;
                const double w = smoothstep(r + blend, r, d);
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * w);
                if (d < r) {
                    keep[id] = 1;
                    if (pnt != PAINT_NONE) paint[id] = pnt, surf[id] = sf;
                }
            }
    };
    // The trail turns loose and rocky up high: stretches of scree.
    for (int id = 0; id < N; id++)
        if (edge[id] < 0.6 && pathPaint[id] == PAINT_TRAIL && hgrid[id] > 95 && std::fmod((double)roadAlong[id], 85.0) < 22) {
            paint[id] = PAINT_SCREE;
            surf[id] = SURF_SCREE;
        }
    // The halfway hut: a level pull-off on the outside of the trail.
    const double hutY = height(spiralAt(kHutT).x, spiralAt(kHutT).z);
    levelDisc(halfwayHut(), 9.5, hutY, 9, PAINT_NONE, SURF_DIRT);
    // Widowmaker Spur: crest, steep faces, pinnacle.
    const V3 s0 = spurStart(), out = spurOut(), spurLeft(-out.z, 0, out.x);
    const double spurY = height(s0.x, s0.z);
    const V3 pin = s0 + out * kSpurLen;
    for (int i = cellI(s0.x - 80); i <= cellI(s0.x + kSpurLen + 90); i++)
        for (int j = cellI(std::min(s0.z, pin.z) - 90); j <= cellI(std::max(s0.z, pin.z) + 90); j++) {
            const double x = X(i), z = X(j);
            const V3 d(x - s0.x, 0, z - s0.z);
            const double sAlong = dot(d, out), lat = dot(d, spurLeft);
            const int id = i * hN + j;
            double h = -1e9;
            bool crest = false;
            uint8_t sf = SURF_ROCK;
            if (sAlong > -2 && sAlong < kSpurLen) {
                const SpurAt a = spurAt(std::max(0.0, sAlong));
                const double fade = smoothstep(-2, 1.5, sAlong);  // the faces start at the trail's edge
                const double edgeY = spurY + a.dh + a.tilt * clampd(lat, -a.half, a.half);
                const double off = std::fabs(lat) - a.half;
                h = off <= 0 ? edgeY : edgeY - off * 1.75 - (1 - fade) * 50;
                crest = off < 0.3 && sAlong > 0.5;
                sf = a.surf;
            }
            // The pinnacle: a flat-topped knob to turn round on.
            const double dp = std::hypot(x - pin.x, z - pin.z);
            const double ph = spurY + 0.5 - std::max(0.0, dp - kPinnacleR) * 1.75;
            if (ph > h) {
                h = ph;
                crest = dp < kPinnacleR + 0.3;
                sf = SURF_ROCK;
            }
            if (h > hgrid[id]) hgrid[id] = (float)h;
            if (crest) {
                keep[id] = 1;
                surf[id] = sf;
                paint[id] = sf == SURF_SCREE ? PAINT_SCREE : PAINT_ROCK;
            }
        }
    // Boulder Gully: a trench cut up the west peak's flank.
    const V3 gFoot = gullyFoot(), gUp = gullyUp(), gLeft(-gUp.z, 0, gUp.x);
    std::vector<double> gFloor(kGullyLen + 1);
    {
        std::vector<double> raw(kGullyLen + 1);
        for (int k = 0; k <= kGullyLen; k++) raw[k] = height(gFoot.x + gUp.x * k, gFoot.z + gUp.z * k);
        double lo = -1e9;
        for (int k = 0; k <= kGullyLen; k++) {
            double sum = 0;
            int cnt = 0;
            for (int w = -8; w <= 8; w++)
                if (k + w >= 0 && k + w <= kGullyLen) sum += raw[k + w], cnt++;
            const double depth = 1.0 + 4.0 * std::pow(std::sin(M_PI * k / kGullyLen), 0.7);
            lo = std::max(lo, sum / cnt - depth);  // never dips: water ran down it
            // ...and no steeper than 40%: near the head, where the peak steepens, it
            // cuts deeper instead.
            if (k > 0) lo = std::min(lo, gFloor[k - 1] + 0.4);
            gFloor[k] = lo;
        }
        // Rock ledges: the floor steps up at each (rock boxes make the risers square).
        for (int k = 0; k <= kGullyLen; k++)
            for (int st = 0; st < 3; st++)
                if (k > kGullySteps[st]) gFloor[k] += kGullyStepH[st];
    }
    auto gullyFloorAt = [&](double sAlong) {
        const double c = clampd(sAlong, 0, kGullyLen);
        const int k = std::min((int)c, (int)kGullyLen - 1);
        return gFloor[k] + (gFloor[k + 1] - gFloor[k]) * (c - k);
    };
    for (int i = cellI(gFoot.x - 40); i <= cellI(gFoot.x + 40); i++)
        for (int j = cellI(gFoot.z - 10); j <= cellI(gFoot.z + kGullyLen + 30); j++) {
            const V3 d(X(i) - gFoot.x, 0, X(j) - gFoot.z);
            const double sAlong = dot(d, gUp), lat = dot(d, gLeft);
            if (sAlong < -6 || sAlong > kGullyLen + 2) continue;
            const double half = 3.2, fl = gullyFloorAt(sAlong) + 0.08 * std::fabs(lat);
            const double wall = fl + 1.4 * std::max(0.0, std::fabs(lat) - half);
            const int id = i * hN + j;
            const double blend = smoothstep(-6, 0, sAlong);
            const double want = std::min((double)hgrid[id], wall);
            hgrid[id] = (float)(hgrid[id] + (want - hgrid[id]) * blend);
            if (std::fabs(lat) < half + 0.5 && sAlong > -1) {
                keep[id] = 1;
                const bool loose = (sAlong > 32 && sAlong < 46) || (sAlong > 64 && sAlong < 73);
                surf[id] = loose ? SURF_SCREE : SURF_ROCK;
                paint[id] = loose ? PAINT_SCREE : PAINT_ROCK;
            }
        }
    const V3 gTop = gFoot + gUp * (kGullyLen + 6);
    levelDisc(gTop, 8, gFloor[kGullyLen], 6, PAINT_ROCK, SURF_ROCK);
    // Repair-station pads.
    levelDisc(kParkPit, 9, height(kParkPit.x, kParkPit.z), 4, PAINT_NONE, SURF_DIRT);

    // Bare rock on the steep faces and up high.
    for (int i = 1; i < hN - 1; i++)
        for (int j = 1; j < hN - 1; j++) {
            int id = i * hN + j;
            if (paint[id] != PAINT_NONE) continue;
            double gx = (gridAt(i + 1, j) - gridAt(i - 1, j)) / (2 * kRes), gz = (gridAt(i, j + 1) - gridAt(i, j - 1)) / (2 * kRes);
            if (std::hypot(gx, gz) > 0.85 || hgrid[id] > 195) surf[id] = SURF_ROCK;
            // Snow lies on the gentler ground up high (above the lookout).
            if (std::hypot(gx, gz) < 0.6 && hgrid[id] > 150 + 22 * fbmNoise(X(i) * 0.02 + 5.0, X(j) * 0.02 - 2.0, 3))
                surf[id] = SURF_SNOW, paint[id] = PAINT_SNOW;
        }
    // Gravel lots and the creek's banks: loose gravel, not packed dirt.
    for (int id = 0; id < N; id++)
        if (paint[id] == PAINT_GRAVEL && surf[id] == SURF_DIRT) surf[id] = SURF_GRAVEL;

    // ---- Rock features (colliders) ----
    Rng rng{0x6a09e667u};
    auto groundAt = [&](double x, double z) { return height(x, z); };
    // A boulder about r across with `top` of it showing above the ground: chunky,
    // flatter or taller at random.
    auto boulder = [&](double x, double z, double r, double top) {
        double flat = rng.range(0.6, 1.0);
        addRock(x, z, V3(r, r * flat, r * rng.range(0.7, 1.0)), std::min(top, r * flat * 1.6), rng.s, (float)rng.range(0.85, 1.15));
    };
    // Boulder field.
    for (int attempt = 0; attempt < 4000 && rocks.size() < 75; attempt++) {
        double x = kBoulderX + rng.range(-4.8, 4.8), z = rng.range(kLaneZ0 + 8, kLaneZ1 - 8);
        double r = rng.range(0.45, 0.95);
        bool clear = true;
        for (const Rock& o : rocks) clear &= std::hypot(o.c.x - x, o.c.z - z) > (o.r + r) * 0.75;
        if (clear) boulder(x, z, r, r * rng.range(0.55, 0.95));
    }
    // V-notch walls.
    for (double z = kLaneZ0 + 10; z < kLaneZ0 + 52; z += 2.2)
        for (int side = -1; side <= 1; side += 2) {
            double r = rng.range(1.15, 1.6);
            boulder(kNotchX + side * (1.55 + r * 0.7), z + rng.range(-0.4, 0.4), r, r * rng.range(0.9, 1.25));
        }
    // Ledges: rock steps up and back down.
    const double stepTop[] = {0.3, 0.6, 0.9, 1.2, 1.5, 1.2, 0.9, 0.6, 0.3};
    for (int s = 0; s < 9; s++) {
        double zc = kLaneZ0 + 12 + 4.0 * s + 2.0;
        double base = parkY - 1.0, top = parkY + stepTop[s];
        addBox(V3(kLedgeX, (base + top) / 2, zc), V3(5.0, (top - base) / 2, 2.0), rng.range(-0.03, 0.03), 0,
               rng.range(-0.04, 0.04), V3(0.38, 0.35, 0.32));
        boxes.back().rock = true;
    }
    // Logs across the log lane, getting bigger.
    for (int g = 0; g < 7; g++) {
        double z = kLaneZ0 + 12 + 10 * g, r = 0.17 + 0.025 * g, ang = rng.range(-0.25, 0.25);
        V3 c(kLogX, 0, z), half(4.6 * std::cos(ang), 0, 4.6 * std::sin(ang));
        V3 a = c - half, b = c + half;
        a.y = groundAt(a.x, a.z) + r * 0.85;
        b.y = groundAt(b.x, b.z) + r * 0.85;
        logs.push_back({a, b, r});
    }
    // Widowmaker Spur: square rock ledges across the crest, boulders in the tread on the
    // loose way down to the last saddle.
    {
        const double yaw = std::atan2(-out.z, out.x);
        for (double st : kSpurSteps) {
            const SpurAt a = spurAt(st + 0.9);
            const V3 c = s0 + out * (st + 0.8);
            const double top = spurY + a.dh;
            addBox(V3(c.x, top - 0.9, c.z), V3(0.8, 0.9, a.half + 0.5), yaw, 0, -std::atan(a.tilt), V3(0.4, 0.38, 0.35));
            boxes.back().rock = true;
        }
        for (int n = 0, attempt = 0; n < 7 && attempt < 200; attempt++) {
            // Staggered off the center: there's a line, but you have to hold it.
            const double sa = rng.range(103, 126), lat = (n % 2 ? 1 : -1) * rng.range(0.6, 1.25), r = rng.range(0.3, 0.45);
            const V3 p = s0 + out * sa + spurLeft * lat;
            bool clear = true;
            for (size_t k = rocks.size() - n; k < rocks.size(); k++) clear &= std::hypot(rocks[k].c.x - p.x, rocks[k].c.z - p.z) > 1.8;
            if (!clear) continue;
            boulder(p.x, p.z, r, rng.range(0.2, 0.32));
            n++;
        }
    }
    // Rock breaking out of the spur's faces (below the crest, so the line stays clear).
    for (int n = 0, attempt = 0; n < 30 && attempt < 400; attempt++) {
        const double sa = rng.range(15, kSpurLen - 5), sd = rng() < 0.5 ? -1 : 1, r = rng.range(1.2, 2.6);
        const SpurAt a = spurAt(sa);
        const double off = rng.range(3.0, 9.0) + r;
        const V3 p = s0 + out * sa + spurLeft * (sd * (a.half + off));
        if (std::hypot(p.x - pin.x, p.z - pin.z) < kPinnacleR + r + 2) continue;
        addRock(p.x, p.z, V3(r, r * rng.range(0.5, 0.8), r * rng.range(0.6, 0.9)), r * 0.45, rng.s, (float)rng.range(0.8, 1.05));
        n++;
    }
    // Boulder Gully: ledge risers and a choke of boulders to pick a line through.
    {
        const double yaw = std::atan2(-gUp.z, gUp.x);
        for (int st = 0; st < 3; st++) {
            // The riser is square; the top follows the floor's slope beyond it.
            const double s1 = kGullySteps[st] + 1, slope = (gullyFloorAt(s1 + 2) - gullyFloorAt(s1)) / 2, pitch = std::atan(slope);
            const double sc = kGullySteps[st] + 0.8, top = gullyFloorAt(s1) - slope * (s1 - sc);
            const V3 c = gFoot + gUp * sc;
            addBox(V3(c.x, top - 1.0 / std::cos(pitch), c.z), V3(0.8, 1.0, 3.6), yaw, pitch, 0, V3(0.4, 0.37, 0.34));
            boxes.back().rock = true;
        }
        const size_t first = rocks.size();
        for (int attempt = 0; attempt < 3000 && rocks.size() < first + 42; attempt++) {
            const double sa = rng.range(10, kGullyLen - 3), lat = rng.range(-2.7, 2.7);
            // The big ones lie toward the walls; mid-channel rocks are ones you can crawl over.
            const double r = std::fabs(lat) > 1.4 ? rng.range(0.6, 1.25) : rng.range(0.35, 0.55);
            bool clear = true;
            for (double st : kGullySteps) clear &= std::fabs(sa - st - 0.8) > 2.2;
            const V3 p = gFoot + gUp * sa + gLeft * lat;
            for (size_t k = first; k < rocks.size() && clear; k++) clear = std::hypot(rocks[k].c.x - p.x, rocks[k].c.z - p.z) > (rocks[k].r + r) * 0.8;
            if (clear) boulder(p.x, p.z, r, r * rng.range(0.5, 0.9));
        }
    }
    // Trail: rocks along the edges, and some in the tread to pick a line through.
    {
        double along = 0;
        for (size_t s = 1; s < trail.pts.size(); s++) {
            const V3& a = trail.pts[s - 1];
            const V3& b = trail.pts[s];
            along += std::hypot(b.x - a.x, b.z - a.z);
            if (along < 120) continue;  // the first stretch from the lot is easy
            V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z)), side(-dir.z, 0, dir.x);
            if (rng() < 0.22) {  // edge boulder
                double r = rng.range(0.4, 1.1), lat = (rng() < 0.5 ? -1 : 1) * rng.range(2.6, 3.6);
                V3 p = b + side * lat;
                boulder(p.x, p.z, r, r * rng.range(0.5, 1.0));
            }
            if (rng() < 0.06) {  // in the tread
                double r = rng.range(0.25, 0.42), lat = rng.range(-1.6, 1.6);
                V3 p = b + side * lat;
                boulder(p.x, p.z, r, rng.range(0.16, 0.3));
            }
        }
    }
    // Creek bed: river rocks (not in the ford).
    for (int attempt = 0; attempt < 6000; attempt++) {
        const V3& p = creek[(size_t)(rng() * (creek.size() - 1))];
        double x = p.x + rng.range(-3.5, 3.5), z = p.z + rng.range(-3.5, 3.5);
        int id = cellI(x) * hN + cellI(z);
        if (creekDist[id] > 3.5 || trailDist[id] < 6 || roadNear[id]) continue;
        if (rng() < 0.75) continue;
        double r = rng.range(0.2, 0.6);
        boulder(x, z, r, r * rng.range(0.4, 0.8));
    }

    // ---- Trees, scattered boulders, bushes ----
    auto clearOfPaths = [&](double x, double z, double margin) {
        int id = cellI(x) * hN + cellI(z);
        if (edge[id] < margin || creekDist[id] < 3.5 || keep[id]) return false;
        if (x > kParkX0 - 10 && x < kParkX1 + 10 && z > kParkZ0 - 10 && z < kParkZ1 + 10) return false;
        if (std::fabs(x - kLotX) < 45 && std::fabs(z - kLotZ) < 35) return false;
        if (std::hypot(x - 26, z - 318) < 12 || std::hypot(x - 155, z + 383) < 12) return false;  // cabin, picnic spot
        if (x > 52 && x < 82 && z > -440 && z < -290) return false;  // whoops
        for (const Water& wt : waters)
            if (std::hypot(x - wt.x, z - wt.z) < wt.r + margin) return false;
        return true;
    };
    auto slopeAt = [&](double x, double z) {
        V3 n = normal(x, z);
        return std::sqrt(std::max(0.0, 1 - n.y * n.y)) / std::max(n.y, 0.1);
    };
    for (int attempt = 0; attempt < 90000 && trees.size() < 2400; attempt++) {
        double x = rng.range(-540, 540), z = rng.range(-540, 540);
        if (!clearOfPaths(x, z, 4)) continue;
        double y = groundAt(x, z), slope = slopeAt(x, z);
        if (slope > 0.95 || y > 178) continue;
        // Forest on the mountain flanks, groves in the valley, thinning to the treeline.
        double forest = fbmNoise(x * 0.012 + 7, z * 0.012 - 3, 3);
        double want = y > 18 ? 0.25 + 0.9 * forest : 0.6 * forest * forest;
        want *= 1 - smoothstep(150, 178, y);
        if (rng() > want) continue;
        Tree t;
        t.base = V3(x, y - 0.2, z);
        t.kind = y < 30 && rng() < 0.45 ? 1 : rng() < 0.04 ? 2 : 0;
        t.height = t.kind == 1 ? rng.range(7, 12) : t.kind == 2 ? rng.range(6, 10) : rng.range(8, 17) * (1 - 0.35 * smoothstep(110, 178, y));
        t.radius = t.kind == 1 ? t.height * rng.range(0.32, 0.42) : t.height * rng.range(0.2, 0.27);
        t.tint = (float)rng.range(0.8, 1.2);
        plant(t);
    }
    // Fallen trees and stumps in the woods.
    const size_t crawlLogs = logs.size();
    for (int attempt = 0; attempt < 20000 && logs.size() < crawlLogs + 90; attempt++) {
        double x = rng.range(-540, 540), z = rng.range(-540, 540);
        if (!clearOfPaths(x, z, 5)) continue;
        double y = groundAt(x, z);
        if (y < 18 || y > 170 || fbmNoise(x * 0.012 + 7, z * 0.012 - 3, 3) < 0.5 || slopeAt(x, z) > 0.7) continue;
        double r = rng.range(0.18, 0.38), L = rng.range(4, 11), a = rng.range(0, 2 * M_PI);
        V3 p0(x - std::cos(a) * L / 2, 0, z - std::sin(a) * L / 2), p1(x + std::cos(a) * L / 2, 0, z + std::sin(a) * L / 2);
        if (!clearOfPaths(p0.x, p0.z, 5) || !clearOfPaths(p1.x, p1.z, 5)) continue;
        p0.y = groundAt(p0.x, p0.z) + r * 0.8;
        p1.y = groundAt(p1.x, p1.z) + r * 0.8;
        logs.push_back({p0, p1, r});
    }
    for (int attempt = 0; attempt < 20000 && fixtures.size() < 140; attempt++) {
        double x = rng.range(-540, 540), z = rng.range(-540, 540);
        if (!clearOfPaths(x, z, 4)) continue;
        double y = groundAt(x, z);
        if (y < 10 || y > 175 || fbmNoise(x * 0.012 + 7, z * 0.012 - 3, 3) < 0.45) continue;
        double r = rng.range(0.22, 0.4), h = rng.range(0.3, 0.7);
        Prop s{PROP_STUMP, V3(x, y, z), V3(r, h, 0), rng.range(0, 6.28)};
        fixtures.push_back(s);
        poles.push_back({x, z, y - 0.3, y + h, r});
    }
    // Outcrops: big rocks breaking out of the mountainsides, and scree up top.
    size_t rocksBefore = rocks.size();
    for (int attempt = 0; attempt < 20000 && rocks.size() < rocksBefore + 80; attempt++) {
        double x = rng.range(-540, 540), z = rng.range(-540, 540);
        if (!clearOfPaths(x, z, 10)) continue;
        double y = groundAt(x, z);
        if (y < 35 || slopeAt(x, z) < 0.35) continue;
        double r = rng.range(3, 8), flat = rng.range(0.4, 0.75);
        addRock(x, z, V3(r, r * flat, r * rng.range(0.55, 0.9)), r * flat * rng.range(0.5, 1.0), rng.s, (float)rng.range(0.8, 1.1));
    }
    rocksBefore = rocks.size();
    for (int attempt = 0; attempt < 20000 && rocks.size() < rocksBefore + 220; attempt++) {
        double a = rng.range(0, 2 * M_PI), d = rng.range(30, 140);
        double x = kPeakX + d * std::cos(a), z = kPeakZ + d * std::sin(a);
        if (!clearOfPaths(x, z, 3) || groundAt(x, z) < 150) continue;
        double r = rng.range(0.25, 0.6);
        boulder(x, z, r, r * rng.range(0.3, 0.7));
    }
    rocksBefore = rocks.size();
    for (int attempt = 0; attempt < 30000 && rocks.size() < rocksBefore + 950; attempt++) {
        double x = rng.range(-560, 560), z = rng.range(-560, 560);
        if (!clearOfPaths(x, z, 3)) continue;
        double y = groundAt(x, z);
        if (rng() > 0.15 + 0.8 * smoothstep(20, 160, y)) continue;  // more up high
        double r = rng.range(0.5, 1.2) + 2.2 * rng() * rng() * rng();
        boulder(x, z, r, r * rng.range(0.4, 1.1));
    }
    // A pile of big rocks on the summit.
    for (int s = 0; s < 22; s++) {
        double a = rng.range(0, 2 * M_PI), d = rng.range(0, 30), r = rng.range(1.2, 3.2);
        boulder(kPeakX + d * std::cos(a), kPeakZ + d * std::sin(a), r, r * rng.range(0.6, 1.2));
    }
    for (int attempt = 0; attempt < 40000 && bushes.size() < 3200; attempt++) {
        double x = rng.range(-540, 540), z = rng.range(-540, 540);
        if (!clearOfPaths(x, z, 2.5)) continue;
        double y = groundAt(x, z);
        if (y > 165 || rng() > 0.55 * (1 - smoothstep(100, 165, y))) continue;
        const double u = rng.range(0, 1), forest = fbmNoise(x * 0.012 + 7, z * 0.012 - 3, 3);
        const int habitat = y > 118 ? HAB_UPLAND : forest > 0.5 ? HAB_FOREST : HAB_MEADOW;
        bushes.push_back(growBush(V3(x, y, z), habitat, forest > 0.36, u, (float)rng.range(0.75, 1.2)));
    }

    // ---- The trailhead: fence, cabin, trail map, lane signs, picnic tables ----
    auto fence = [&](double x0, double z0, double x1, double z1) {
        V3 a(x0, groundAt(x0, z0), z0), b(x1, groundAt(x1, z1), z1);
        fixtures.push_back({PROP_FENCE, a, b});
        V3 d = b - a;
        double L = std::hypot(d.x, d.z), yaw = std::atan2(-d.z, d.x);
        addBox((a + b) * 0.5 + V3(0, 0.62, 0), V3(L / 2, 0.3, 0.06), yaw, std::atan2(d.y, L), 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
        for (double s = 0; s <= L + 0.01; s += 2.5) {
            V3 p = a + d * (s / L);
            poles.push_back({p.x, p.z, p.y - 0.4, p.y + 1.1, 0.08});
        }
    };
    const double lx0 = kLotX - 30, lx1 = kLotX + 30, lz0 = kLotZ - 22, lz1 = kLotZ + 22;
    fence(lx0, lz0, lx1, lz0);                    // south
    fence(lx1, lz0, lx1, kLotZ - 5);              // east, with a gate to the whoops and the lake
    fence(lx1, kLotZ + 5, lx1, lz1);
    fence(lx0, lz1, kLotX - 3, lz1);              // north, open for the trail
    fence(kLotX + 15, lz1, lx1, lz1);
    fence(lx0, lz0, lx0, kLotZ - 14);             // west, open to the crawl park
    fence(lx0, kLotZ + 14, lx0, lz1);
    {
        double cy = groundAt(26, -318);
        fixtures.push_back({PROP_CABIN, V3(26, cy, -318), V3(), M_PI});  // door facing the lot
        addBox(V3(26, cy + 1.7, -318), V3(2.7, 1.9, 2.2), 0, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    }
    auto sign = [&](double x, double z, double yaw, V3 col, PropKind kind = PROP_SIGN) {
        Prop s{kind, V3(x, groundAt(x, z), z), V3(), yaw};
        s.col[0] = (float)col.x, s.col[1] = (float)col.y, s.col[2] = (float)col.z;
        fixtures.push_back(s);
        double half = kind == PROP_MAPBOARD ? 0.8 : 0.0;
        V3 side(std::sin(yaw), 0, std::cos(yaw));
        for (int k = kind == PROP_MAPBOARD ? -1 : 1; k <= 1; k += 2)
            poles.push_back({x + side.x * half * k, z + side.z * half * k, s.a.y - 0.4, s.a.y + 2.0, 0.07});
    };
    sign(kLotX + 2, lz1 + 3, M_PI / 2, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);  // trail map, facing the lot
    // Lane signs: green easy, blue harder, black hard, red extreme (the trail-rating colors).
    const V3 green(0.1, 0.55, 0.2), blue(0.1, 0.3, 0.8), black(0.03, 0.03, 0.03), red(0.75, 0.08, 0.06);
    sign(kTwisterX + 6, kLaneZ0 - 3, M_PI / 2, blue);
    sign(kBoulderX + 6.5, kLaneZ0 - 3, M_PI / 2, black);
    sign(kLedgeX + 6.5, kLaneZ0 - 3, M_PI / 2, black);
    sign(kLogX + 6.5, kLaneZ0 - 3, M_PI / 2, green);
    sign(kNotchX + 5.5, kLaneZ0 - 3, M_PI / 2, red);
    sign(kDomeX + 4, kDomeZ - kDomeR - 3, M_PI / 2, blue);
    sign(78, -432, M_PI / 2, green);  // whoops
    auto table = [&](double x, double z, double yaw) {
        double y = groundAt(x, z);
        fixtures.push_back({PROP_TABLE, V3(x, y, z), V3(), yaw});
        addBox(V3(x, y + 0.4, z), V3(0.95, 0.4, 0.8), yaw, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    };
    table(150, -366, 0.3);
    table(147, -398, -0.4);
    table(kLotX + 24, kLotZ + 14, 0);
    // A fire ring by the lake.
    for (int k = 0; k < 11; k++) {
        double a = k * 2 * M_PI / 11;
        addRock(155 + 0.95 * std::cos(a), -383 + 0.95 * std::sin(a), V3(0.24, 0.17, 0.2), 0.18, 900 + k, 0.8f);
    }

    // ---- Repair stations ----
    auto station = [&](const V3& c, double yaw, const char* name) {
        const V3 f(std::cos(yaw), 0, -std::sin(yaw)), sd(std::sin(yaw), 0, std::cos(yaw));
        for (int i = cellI(c.x - 9); i <= cellI(c.x + 9); i++)
            for (int j = cellI(c.z - 9); j <= cellI(c.z + 9); j++) {
                const V3 d(X(i) - c.x, 0, X(j) - c.z);
                if (std::fabs(dot(d, f)) < 6.4 && std::fabs(dot(d, sd)) < 5.3) {
                    paint[i * hN + j] = PAINT_PAD;
                    surf[i * hN + j] = SURF_PAVED;
                }
            }
        addStation(V3(c.x, groundAt(c.x, c.z), c.z), yaw, name);
    };
    station(kTrailheadShop, 0, "TRAILHEAD GARAGE");
    station(kParkPit, 0, "CRAWL PARK PIT");
    {
        const V3 t = spiralDir(kHutT);
        station(halfwayHut(), std::atan2(-t.z, t.x), "HALFWAY HUT");
    }
    // The hard lines: rating signs where they start, cairns where they finish, reflector
    // posts along the spur's edges.
    sign(spiralAt(kSpurT).x - out.x * 3.4, spiralAt(kSpurT).z - out.z * 3.4, std::atan2(-out.z, out.x), black);
    {
        const V3 p = gFoot - gUp * 3 + gLeft * 4.6;
        sign(p.x, p.z, std::atan2(gUp.z, -gUp.x), red);
        sign(-200, -318, M_PI / 2, red);  // and where its track leaves the park
    }
    auto cairn = [&](const V3& p) { fixtures.push_back({PROP_CAIRN, V3(p.x, groundAt(p.x, p.z), p.z), V3(), rng.range(0, 6.28)}); };
    cairn(pin + out * (kPinnacleR - 1.6));
    cairn(gTop + gUp * 5);
    for (double sa = 6; sa < kSpurLen - kPinnacleR; sa += 14)
        for (int sd = -1; sd <= 1; sd += 2) {
            if ((int)(sa / 14) % 2 != (sd > 0)) continue;  // alternate sides
            const SpurAt a = spurAt(sa);
            const V3 p = s0 + out * sa + spurLeft * (sd * (a.half + 0.35));
            fixtures.push_back({PROP_MARKER, V3(p.x, groundAt(p.x, p.z), p.z), V3(), std::atan2(-out.z, out.x)});
        }

    // Traffic drives the valley loop.
    loopRoad = loop.pts;
    loopAlong.resize(loopRoad.size());
    loopLength = 0;
    for (size_t i = 0; i < loopRoad.size(); i++) {
        loopAlong[i] = loopLength;
        const V3& a = loopRoad[i];
        const V3& b = loopRoad[(i + 1) % loopRoad.size()];
        loopLength += std::hypot(b.x - a.x, b.z - a.z);
    }

    // The trailhead: you start facing the mountain, the other trucks parked beside you.
    spawn = V3(kLotX, lotY, kLotZ);
    spawnYaw = -M_PI / 2;
    parking = {V3(kLotX - 21, lotY, kLotZ + 12), V3(kLotX - 14, lotY, kLotZ + 12), V3(kLotX - 7, lotY, kLotZ + 12)};
    parkingYaw = M_PI / 2;
}

// Where the crawl park's features are (for tests and the README).
V3 mountainFeature(const char* name) {
    std::string n = name;
    double y = 0;
    if (n == "twister") return V3(kTwisterX, y, kLaneZ0);
    if (n == "boulders") return V3(kBoulderX, y, kLaneZ0);
    if (n == "ledges") return V3(kLedgeX, y, kLaneZ0);
    if (n == "logs") return V3(kLogX, y, kLaneZ0);
    if (n == "notch") return V3(kNotchX, y, kLaneZ0);
    if (n == "dome") return V3(kDomeX, y, kDomeZ);
    if (n == "whoops") return V3(67, y, -430);                           // the whoops lane's south end: it runs 130 m north (0.55 m bumps every 9 m)
    if (n == "trailhead") return V3(kLotX, y, kLotZ);
    if (n == "summit") return V3(kPeakX, y, kPeakZ);
    if (n == "lookout") return spiralAt(2 * M_PI - 0.06);             // the summit trail's end
    if (n == "spur") return spurStart();                              // where the spur leaves the trail
    if (n == "pinnacle") return spurStart() + spurOut() * kSpurLen;
    if (n == "gully") return gullyFoot();
    if (n == "gullytop") return gullyFoot() + gullyUp() * (kGullyLen + 6);
    if (n == "halfway") return halfwayHut();
    if (n == "garage") return kTrailheadShop;
    if (n == "pit") return kParkPit;
    return V3();
}

std::vector<V3> mountainTrail() {
    std::vector<V3> out;
    for (double t = 0; t <= 2 * M_PI - 0.06; t += 0.02) out.push_back(spiralAt(t));
    return out;
}
