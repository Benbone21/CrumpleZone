// The Backcountry: forested hill country for driving with buddies.
//
// A trailhead camp in the south and a network of trails radiating from it, each its own
// challenge (the sign colours are the trail ratings: green easy, blue, black hard, red extreme):
//
//  - THE MUD RUN (black): a long forest track east of camp with deep ruts, mud stretches and
//    four bog holes. A 2WD gets stuck; a 4x4 with lockers makes it. Ends at Sawmill Camp.
//  - CREEK CRAWL (red): up a creek bed of boulders and rock ledges (lockers needed) to the
//    old mine.
//  - SLICKROCK RIDGE (black): from the mine along a bare rock spine with off-camber slabs, down
//    to the DEEP CROSSING of Elk River, and on to Far Side Camp.
//  - RIDGE ROAD (green): Sawmill Camp to the shallow FORD and Far Side Camp; HEARTBREAK HILL
//    (red) climbs straight over the hill beside it.
//  - SAND WASH (blue): from Sawmill Camp east down a dry wash and round the dune bowl, out to
//    the Mud Run.
//  - SHELF ROAD (black): from Far Side Camp north along a canyon rim, a one-truck shelf with a
//    real drop; the wreck site lies in the canyon below (WRECK SPUR, green).
//  - HIGH PASS (blue): snow road over the saddle to the pass hut; the TOWER ROAD (green) takes
//    the other side down past the fire tower and back to the ford; the cabin spur leaves it.
//
// Loops: camp -> Mud Run -> Sawmill -> ford -> Far Side, and camp -> Creek Crawl -> mine ->
// Slickrock Ridge -> deep crossing -> Far Side meet at Far Side Camp; the north trails make a
// loop of their own. Coordinates: x east, z north, metres; Elk River runs east to west across
// the middle (riverZ). Water lives on the height grid (World::waterY and the flow vectors).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "world.h"

namespace {

using Poly = std::vector<V3>;

double sq(double v) { return v * v; }

// Elk River's centre line: z as a function of x (it flows west).
double riverZ(double x) { return 14 + 36 * std::sin(x * 0.0068 + 0.9) + 12 * std::sin(x * 0.019 + 2.3); }
constexpr double kFordX = 90, kDeepX = -100;
constexpr double kRiverHalf = 8.0;

// ---- Places ----------------------------------------------------------------------------------
const V3 kTrailhead(0, 0, -440), kSawmill(170, 0, -68), kFarSide(20, 0, 105);
const V3 kMine(-304, 0, -198), kCabin(338, 0, 92);
const V3 kDunes(388, 0, -225);  // the dune bowl's middle
constexpr double kDuneR = 30;   // the ring trail's radius
const V3 kHill(92, 0, -74);     // Heartbreak Hill's top

// ---- Trails (control points; the first is where it leaves camp) ----------------------------
const std::vector<V3> kMudCtl = {{22, 0, -436},  {90, 0, -428},  {158, 0, -414}, {220, 0, -388}, {268, 0, -346}, {292, 0, -296},
                                 {286, 0, -246}, {262, 0, -200}, {240, 0, -160}, {222, 0, -122}, {200, 0, -92},  {182, 0, -73}};
const std::vector<V3> kCreekCtl = {{-16, 0, -434}, {-52, 0, -418},  {-92, 0, -388},  {-122, 0, -346}, {-150, 0, -306},
                                   {-176, 0, -270}, {-206, 0, -240}, {-240, 0, -218}, {-274, 0, -206}, {-294, 0, -200}};
// The ridge: mine -> down to the river -> across the deep crossing -> Far Side Camp.
const std::vector<V3> kRidgeCtl = {{-300, 0, -188}, {-282, 0, -162}, {-258, 0, -134}, {-230, 0, -108}, {-200, 0, -84}, {-172, 0, -58},
                                   {-150, 0, -34},  {-130, 0, -10},  {-112, 0, 12},   {-104, 0, 34},   {-98, 0, 52},   {-80, 0, 70},
                                   {-56, 0, 86},    {-28, 0, 98},    {-2, 0, 105},    {11, 0, 105}};
const std::vector<V3> kRidgeRoadCtl = {{178, 0, -62}, {158, 0, -44}, {136, 0, -28}, {116, 0, -8}, {100, 0, 16}, {92, 0, 38},
                                       {86, 0, 60},   {74, 0, 78},   {54, 0, 92},   {34, 0, 102}, {29, 0, 105}};
const std::vector<V3> kHillCtl = {{160, 0, -70}, {138, 0, -76}, {114, 0, -78}, {92, 0, -72}, {72, 0, -56}, {68, 0, -36}, {78, 0, -20}, {96, 0, -10}, {114, 0, -8}};
const std::vector<V3> kShelfCtl = {{22, 0, 116}, {12, 0, 138}, {-2, 0, 170}, {-16, 0, 210}, {-26, 0, 252}, {-24, 0, 296}, {-8, 0, 334}, {14, 0, 362}};
// The north's land as the trail crew sees it: a ramp climbing with z (gentle to the shelf road's end,
// then the steep mountainside to the pass), the valley's flanks rising either side. The real
// terrain adds noise to this; the high trails are laid out on it so they follow the land.
double softRamp(double v, double w) { return 0.5 * (std::sqrt(v * v + w * w) + v); }
double macroHeight(double x, double z) {
    return 2.6 + 0.135 * softRamp(z - 110, 30) + 0.215 * softRamp(z - 362, 30) + 0.07 * softRamp(z - 470, 30) + 42 * smoothstep(160, 460, std::fabs(x - 90));
}
// A switchback trail laid out like a trail crew would: traverse the slope at a steady `grade` (+
// climbing, - descending), a half-circle hairpin at each end of the traverse, until `stop`.
template <class Stop>
std::vector<V3> crewTrail(V3 pos, double grade, double xMin, double xMax, int dir, Stop stop) {
    std::vector<V3> pts{pos};
    const double step = 2.0;
    auto h = [](const V3& p) { return macroHeight(p.x, p.z); };
    for (int n = 0; n < 1200 && !stop(pos); n++) {
        if ((dir > 0 && pos.x > xMax) || (dir < 0 && pos.x < xMin)) {
            // Hairpin: a half circle (11 m radius) round toward the uphill side (climbing) or the downhill one.
            const V3 c = pos + V3(0, 0, grade > 0 ? 11 : -11);
            const double a0 = std::atan2(pos.z - c.z, pos.x - c.x), turn = (grade > 0 ? 1 : -1) * dir;
            for (int k = 1; k <= 15; k++) {
                const double a = a0 + turn * M_PI * k / 15.0;
                pos = c + V3(std::cos(a), 0, std::sin(a)) * 11.0;
                pts.push_back(pos);
            }
            dir = -dir;
            continue;
        }
        double best = 1e9;
        V3 next = pos;
        for (int k = 0; k <= 30; k++) {
            const double ang = (k - 15) * (75.0 / 15) * M_PI / 180;
            const V3 d(dir * std::cos(ang), 0, std::fabs(std::sin(ang)) * (k >= 15 ? 1 : -1));
            const V3 q = pos + d * step;
            const double g = (h(q) - h(pos)) / step;
            const double score = std::fabs(g - grade) + 0.05 * std::fabs(ang);
            if (score < best) best = score, next = q;
        }
        pos = next;
        pts.push_back(pos);
    }
    return pts;
}
// The pass road: climbs from the shelf road's end to the hut at the saddle.
const std::vector<V3>& passCtl() {
    static const std::vector<V3> t = [] {
        std::vector<V3> p = crewTrail(V3(14, 0, 362), 0.07, -30, 130, 1, [](const V3& q) { return macroHeight(q.x, q.z) >= 62 || q.z > 470; });
        // thin it to every 6th point (the spline rounds it off)
        std::vector<V3> out;
        for (size_t k = 0; k < p.size(); k += 6) out.push_back(p[k]);
        if (std::hypot(out.back().x - p.back().x, out.back().z - p.back().z) > 1) out.push_back(p.back());
        return out;
    }();
    return t;
}
V3 passHutAt() { return passCtl().back(); }
// The tower road: from the hut down the east side in long switchbacks, out to the ford.
const std::vector<V3>& towerCtl() {
    static const std::vector<V3> t = [] {
        const V3 hut = passHutAt();
        std::vector<V3> p = crewTrail(V3(hut.x + 8, 0, hut.z - 2), -0.12, 20, 215, 1, [](const V3& q) { return macroHeight(q.x, q.z) < 9; });
        std::vector<V3> out{V3(hut.x + 8, 0, hut.z - 2)};
        for (size_t k = 6; k < p.size(); k += 6) out.push_back(p[k]);
        // And on to the ford's north bank.
        out.push_back(V3(100, 0, 88));
        out.push_back(V3(88, 0, 74));
        return out;
    }();
    return t;
}
// The fire tower stands at the tower road's first hairpin (its eastern end), a little beyond it.
V3 towerSite() {
    const std::vector<V3>& c = towerCtl();
    V3 best = c[0];
    for (const V3& q : c)
        if (q.x > best.x) best = q;
    return best + V3(12, 0, 6);
}

// The cabin spur leaves the tower road (where it's nearest (150, 135)) for the river's north bank.
const std::vector<V3>& cabinCtl() {
    static const std::vector<V3> t = [] {
        const std::vector<V3>& c = towerCtl();
        V3 best = c[0];
        for (const V3& q : c)
            if (std::hypot(q.x - 150, q.z - 135) < std::hypot(best.x - 150, best.z - 135)) best = q;
        return std::vector<V3>{best, V3(190, 0, 128), V3(226, 0, 120), V3(262, 0, 114), V3(298, 0, 106), V3(326, 0, 96)};
    }();
    return t;
}

// Where things are along the trails (metres from the start of each).
constexpr double kBogS[4] = {120, 372, 440, 520}, kBogLen[4] = {14, 16, 12, 18}, kBogDepth[4] = {0.40, 0.48, 0.56, 0.45};
// The mud stretches of the Mud Run.
constexpr double kMudZone[5][2] = {{96, 150}, {232, 330}, {350, 398}, {418, 470}, {494, 548}};
constexpr double kCreekLedge[8][2] = {{70, 0.20}, {112, 0.24}, {150, 0.28}, {188, 0.28}, {224, 0.31}, {262, 0.33}, {300, 0.35}, {334, 0.36}};  // s, rise
constexpr double kRidgeStep[3][2] = {{105, -0.30}, {150, -0.34}, {192, -0.38}};  // (drops going down)
constexpr double kSlabS = 150;       // Slab Point, along the ridge
constexpr double kOverlookS = 168;   // Cliff Overlook, along the shelf road
constexpr double kSnowCornerS = 170; // the pass's long bend

// A polyline resampled every `step` metres.
Poly resample(const Poly& pts, double step) {
    Poly out{pts[0]};
    double carry = 0;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        const V3 a = pts[k], b = pts[k + 1];
        const double L = std::hypot(b.x - a.x, b.z - a.z);
        double s = step - carry;
        for (; s <= L; s += step) out.push_back(a + (b - a) * (s / L));
        carry = L - (s - step);
    }
    if (std::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.2 * step) out.push_back(pts.back());
    return out;
}
Poly smooth(const std::vector<V3>& ctl) { return resample(splinePoints(ctl, false), 1.0); }
// The point `s` metres along a polyline sampled every metre (clamped to its ends).
V3 pointAlong(const Poly& pts, double s) {
    const int k = (int)clampd(std::floor(s), 0, (double)pts.size() - 2);
    return pts[k] + (pts[k + 1] - pts[k]) * clampd(s - k, 0, 1);
}
V3 dirAlong(const Poly& pts, double s) {
    const V3 d = pointAlong(pts, s + 1.5) - pointAlong(pts, s - 1.5);
    return norm(V3(d.x, 0, d.z));
}

// The sand wash: out of Sawmill Camp, down the wash to the dune bowl, once round the bowl on
// the ring trail and out to the Mud Run.
Poly washPoly() {
    std::vector<V3> ctl = {{182, 0, -66}, {214, 0, -52}, {256, 0, -56}, {298, 0, -84}, {330, 0, -124}, {350, 0, -168}};
    const double a0 = std::atan2(ctl.back().z - kDunes.z, ctl.back().x - kDunes.x);
    for (int k = 0; k <= 29; k++) {  // clockwise round the bowl, ~290 degrees
        const double a = a0 - k * (290.0 / 29) * M_PI / 180;
        const double r = kDuneR + 6 * (1 - k / 29.0) + 3 * std::sin(k * 0.7);
        ctl.push_back(V3(kDunes.x + r * std::cos(a), 0, kDunes.z + r * std::sin(a)));
    }
    const std::vector<V3> out = {{332, 0, -252}, {314, 0, -274}, {298, 0, -292}};
    ctl.insert(ctl.end(), out.begin(), out.end());
    return smooth(ctl);
}
const Poly& mudTrail() { static const Poly t = smooth(kMudCtl); return t; }
const Poly& creekTrail() { static const Poly t = smooth(kCreekCtl); return t; }
const Poly& ridgeTrail() { static const Poly t = smooth(kRidgeCtl); return t; }
const Poly& ridgeRoadTrail() { static const Poly t = smooth(kRidgeRoadCtl); return t; }
const Poly& hillTrail() { static const Poly t = smooth(kHillCtl); return t; }
const Poly& washTrail() { static const Poly t = washPoly(); return t; }
const Poly& shelfTrail() { static const Poly t = smooth(kShelfCtl); return t; }
const Poly& passTrail() { static const Poly t = smooth(passCtl()); return t; }
const Poly& towerTrail() { static const Poly t = smooth(towerCtl()); return t; }
const Poly& cabinTrail() { static const Poly t = smooth(cabinCtl()); return t; }
// The canyon floor under the shelf road: the shelf offset to its right (east).
constexpr double kCanyonOff = 34;
const Poly& canyon() {
    static const Poly t = [] {
        const Poly& s = shelfTrail();
        Poly out;
        for (size_t k = 0; k < s.size(); k++) {
            const V3 d = dirAlong(s, (double)k), r(d.z, 0, -d.x);
            out.push_back(s[k] + r * kCanyonOff);
        }
        return out;
    }();
    return t;
}
const Poly& riverLine() {
    static const Poly t = [] {
        std::vector<V3> ctl;
        for (double x = 540; x >= -540; x -= 30) ctl.push_back(V3(x, 0, riverZ(x)));
        return smooth(ctl);
    }();
    return t;
}
// The wreck spur: from the ridge road up the canyon floor to the wreck.
const Poly& wreckTrail() {
    static const Poly t = [] {
        const Poly& c = canyon();
        std::vector<V3> ctl = {{60, 0, 90}};
        for (size_t k = 24; k < 200 && k < c.size(); k += 22) ctl.push_back(c[k]);
        return smooth(ctl);
    }();
    return t;
}

}  // namespace

// ================================================================================================
void World::buildBackcountry() {
    map = MAP_BACKCOUNTRY;
    crashProps = false;
    hN = (int)(2 * kHalfSize / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -kHalfSize + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + kHalfSize) / kRes), 0), hN - 1); };
    auto at = [&](double x, double z) -> int { return cellI(x) * hN + cellI(z); };
    (void)at;

    // ---- The land -----------------------------------------------------------------------------
    hgrid.resize(N);
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j);
            const double d = z - riverZ(x);
            // Rolling country everywhere (weaker on the flood plain)...
            const double plain = smoothstep(15, 120, std::fabs(d));
            double h = 4 + 1.2 * std::sin((x + z) * 0.03) + plain * (22 * (fbmNoise(x * 0.011 + 2.0, z * 0.011 - 1.0, 4) - 0.35));
            // South of the river: the uplands climb away from it, knobbly with rock.
            const double up = smoothstep(-10, -470, d);
            h += up * (12 + 32 * (fbmNoise(x * 0.006 + 9.0, z * 0.006 + 4.0, 4) - 0.4) + 18 * ridgedNoise(x * 0.012 + 3.0, z * 0.012 - 5.0, 3));
            // North: the ramp up to the range and the high pass (see macroHeight), with noise on it and
            // crags up in the far north.
            if (z > 90) {
                const double nz = smoothstep(90, 200, z);
                h = h * (1 - nz) + nz * (macroHeight(x, z) + 14 * (fbmNoise(x * 0.008 + 1.7, z * 0.008 - 0.6, 3) - 0.45));
                h += smoothstep(485, 530, z) * 30 * ridgedNoise(x * 0.006 + 1.7, z * 0.006 - 0.6, 4);
            }
            // The river's flood plain: flat and low, the land climbing away from it.
            h = 2.6 + 0.7 * valueNoise(x * 0.04, z * 0.04) + (h - 2.6) * smoothstep(32, 150, std::fabs(d));
            // Rim mountains keep you on the map.
            const double ex = std::max(0.0, std::fabs(x) - 520), ez = std::max(0.0, std::fabs(z) - 520);
            h += (ex * ex + ez * ez) * 0.02;
            hgrid[i * hN + j] = (float)h;
        }
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    roadLat.assign(N, 0);
    roadAlong.assign(N, 0);
    roadNear.assign(N, 0);
    waterY.assign(N, -1e9f);
    flowX.assign(N, 0);
    flowZ.assign(N, 0);
    std::vector<uint8_t> keep(N, 0);   // no trees, rocks or bushes here
    std::vector<float> edge(N, 1e9f);  // distance past the nearest trail's tread

    // Scratch for scanning cells near a polyline.
    std::vector<float> sd(N, 1e9f), sk(N, 0), sl(N, 0);
    std::vector<int> cells;
    // Calls fn(id, distance, index along (fractional), signed lateral (+ left)) for every cell
    // within `reach` of the polyline, with its nearest stretch.
    auto scanNear = [&](const Poly& pts, double reach, auto fn) {
        cells.clear();
        const int n = (int)pts.size();
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
            if (L2 < 1e-9) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
                    const double d = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    if (d > reach) continue;
                    const int id = i * hN + j;
                    if (sd[id] > 1e8f) cells.push_back(id);
                    if (d >= sd[id]) continue;
                    sd[id] = (float)d, sk[id] = (float)(s + t), sl[id] = (float)((dx * (z - a.z) - dz * (x - a.x)) / L);
                }
        }
        for (int id : cells) {
            fn(id, (double)sd[id], (double)sk[id], (double)sl[id]);
            sd[id] = 1e9f;
        }
    };
    auto yAt = [&](const std::vector<double>& y, double k) {
        const int i = (int)clampd(std::floor(k), 0, (double)y.size() - 2);
        return y[i] + (y[i + 1] - y[i]) * clampd(k - i, 0, 1);
    };

    // A height profile along a polyline: the land under it smoothed over `window` metres either
    // side, then no steeper than `maxGrade` (cut and fill). `pins` hold heights fixed (the ends
    // of a trail that meets a camp or another trail); the profile eases into them.
    struct Pin {
        int idx;
        double y;
    };
    auto groundProfile = [&](const Poly& pts, int window, double maxGrade, const std::vector<Pin>& pins) {
        const int n = (int)pts.size();
        std::vector<double> raw(n), y(n);
        for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
        for (int k = 0; k < n; k++) {
            double sum = 0;
            int cnt = 0;
            for (int q = std::max(0, k - window); q <= std::min(n - 1, k + window); q++) sum += raw[q], cnt++;
            y[k] = sum / cnt;
        }
        for (const Pin& p : pins) {
            const double off = p.y - y[p.idx];
            for (int k = 0; k < n; k++) y[k] += off * smoothstep(30, 0, std::fabs((double)(k - p.idx)));
        }
        for (int pass2 = 0; pass2 < 6; pass2++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < n; q++) {
                    const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                    bool pinned = false;
                    for (const Pin& p : pins) pinned |= p.idx == k;
                    if (pinned) continue;
                    const double run = std::hypot(pts[k].x - pts[prev].x, pts[k].z - pts[prev].z);
                    y[k] = clampd(y[k], y[prev] - maxGrade * run, y[prev] + maxGrade * run);
                }
        if (std::getenv("BC_DEBUG")) {
            double fill = 0, cut = 0;
            int kf = 0, kc = 0;
            for (int k = 0; k < n; k++) {
                if (y[k] - raw[k] > fill) fill = y[k] - raw[k], kf = k;
                if (raw[k] - y[k] > cut) cut = raw[k] - y[k], kc = k;
            }
            std::fprintf(stderr, "profile from (%.0f,%.0f) %d pts: max fill %.1f at %d, max cut %.1f at %d\n", pts[0].x, pts[0].z, n, fill, kf, cut, kc);
        }
        return y;
    };
    // Lays a trail: the tread (`half` wide, at the heights `y`, one per point) is painted by
    // paintTread(id, lateral, index); the land within `blend` past its edge eases into it. `tilt`
    // gives the tread a cross slope (rise per metre to the left) at an index along it. Where an
    // earlier trail is nearer, that trail is left alone.
    auto layTrail = [&](const Poly& pts, const std::vector<double>& y, double half, double blend, auto paintTread, auto tilt) {
        scanNear(pts, half + blend, [&](int id, double d, double k, double lat) {
            const double e = d - half;
            if (edge[id] < e) return;
            const double w = e < 1.0 ? 1.0 : smoothstep(blend, 1.0, e);
            const double ty = yAt(y, k) + tilt(k) * clampd(lat, -half, half);
            hgrid[id] = (float)(hgrid[id] + (ty - hgrid[id]) * w);
            edge[id] = (float)e;
            roadLat[id] = (float)lat, roadAlong[id] = (float)k;
            if (e < 0.5) paintTread(id, lat, k), roadNear[id] = 1;
            if (e < 3) keep[id] = 1;
        });
    };
    auto noTilt = [](double) { return 0.0; };
    auto levelDisc = [&](const V3& c, double r, double y, double blend, uint8_t pnt, uint8_t sf) {
        for (int i = cellI(c.x - r - blend); i <= cellI(c.x + r + blend); i++)
            for (int j = cellI(c.z - r - blend); j <= cellI(c.z + r + blend); j++) {
                const double d = std::hypot(X(i) - c.x, X(j) - c.z);
                if (d > r + blend) continue;
                const int id = i * hN + j;
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(r + blend, r, d));
                if (d < r - 0.5 && pnt != 255) paint[id] = pnt, surf[id] = sf;
                if (d < r + blend * 0.5) keep[id] = 1;
            }
    };
    // Seeded random numbers.
    uint32_t rs = 0x9e3779b9u;
    auto rnd = [&]() {
        rs ^= rs << 13;
        rs ^= rs >> 17;
        rs ^= rs << 5;
        return (rs & 0xFFFFFF) / double(0x1000000);
    };
    auto between = [&](double a, double b) { return a + (b - a) * rnd(); };
    (void)between;

    const Poly &mud = mudTrail(), &creek = creekTrail(), &ridge = ridgeTrail(), &rroad = ridgeRoadTrail(), &hill = hillTrail(), &wash = washTrail(),
               &shelf = shelfTrail(), &pass = passTrail(), &tower = towerTrail(), &cabinT = cabinTrail(), &rivr = riverLine(), &wreckT = wreckTrail(), &cany = canyon();

    // ---- Landforms: hills, the dune bowl --------------------------------------------------------
    for (int i = cellI(kHill.x - 190); i <= cellI(kHill.x + 190); i++)  // Heartbreak Hill
        for (int j = cellI(kHill.z - 190); j <= cellI(kHill.z + 190); j++)
            hgrid[i * hN + j] += (float)(26 * std::exp(-(sq(X(i) - kHill.x) + sq(X(j) - kHill.z)) / sq(46)));
    {
        // The mine's hillside: the land climbs from the trailhead toward the mine, so the creek crawl
        // climbs the whole way.
        const V3 a = kTrailhead, b = kMine;
        const double L = std::hypot(b.x - a.x, b.z - a.z);
        const V3 ax((b.x - a.x) / L, 0, (b.z - a.z) / L);
        for (int i = cellI(-420); i <= cellI(120); i++)
            for (int j = cellI(-520); j <= cellI(-100); j++) {
                const V3 d(X(i) - a.x, 0, X(j) - a.z);
                const double t = dot(d, ax) / L, lat = d.x * ax.z - d.z * ax.x;
                hgrid[i * hN + j] += (float)(20 * smoothstep(0.05, 1.0, t) * std::exp(-sq(lat / 120)) * smoothstep(1.35, 1.0, t));
            }
    }
    // The dune bowl: a basin in the foothills with big sand dunes in it.
    for (int i = cellI(kDunes.x - 100); i <= cellI(kDunes.x + 100); i++)
        for (int j = cellI(kDunes.z - 100); j <= cellI(kDunes.z + 100); j++) {
            const double x = X(i), z = X(j), d = std::hypot(x - kDunes.x, z - kDunes.z);
            if (d > 95) continue;
            const int id = i * hN + j;
            const double ridgeN = std::pow(1 - std::fabs(std::sin(0.055 * (0.8 * x + 0.6 * z) + 1.5 * std::sin(0.013 * z))), 2.0);
            const double dunes = 4.5 * ridgeN * (0.5 + 0.5 * valueNoise(x * 0.02, z * 0.02)) + 1.0 * valueNoise(x * 0.07, z * 0.07);
            const double floorY = hgrid[id] - 6 * smoothstep(92, 50, d);
            hgrid[id] = (float)(floorY + dunes * smoothstep(90, 60, d));
        }

    // ---- Camps: level pads ---------------------------------------------------------------------
    auto padHeight = [&](const V3& c) {
        double sum = 0;
        int cnt = 0;
        for (double dx = -12; dx <= 12; dx += 4)
            for (double dz = -12; dz <= 12; dz += 4) sum += height(c.x + dx, c.z + dz), cnt++;
        return sum / cnt;
    };
    struct Pad {
        V3 c;
        double r;
        double y;
        double yaw;
        const char* name;
    };
    std::vector<Pad> pads = {{kTrailhead, 15, 0, 0, "TRAILHEAD CAMP"},
                             {kSawmill, 10, 0, M_PI / 2, "SAWMILL CAMP"},
                             {kFarSide, 10, 0, 0, "FAR SIDE CAMP"},
                             {passHutAt(), 10, 0, M_PI / 2, "PASS HUT"}};
    for (Pad& p : pads) {
        p.y = padHeight(p.c);
        levelDisc(p.c, p.r + 3, p.y, 12, PAINT_PAD, SURF_PAVED);
    }
    auto padY = [&](const V3& c) {
        for (const Pad& p : pads)
            if (std::hypot(p.c.x - c.x, p.c.z - c.z) < 20) return p.y;
        return 0.0;
    };

    // ---- Rock steps across a trail's bed. A rise (h > 0): the profile steps up over 1.5 m and a rock slab
    // stands on the lip, so the face is square. A drop (h < 0): the profile falls over half a metre and
    // the slab holds the level above it ---------------------------------------------------------------
    auto addSteps = [&](const Poly& pts, std::vector<double>& y, const std::vector<std::pair<double, double>>& steps, double halfW, const V3& col) {
        const std::vector<double> y0 = y;
        for (const auto& st : steps) {
            const double ramp = st.second >= 0 ? 1.5 : 0.5;
            for (size_t k = 0; k < y.size(); k++) y[k] += st.second * clampd((k - st.first) / ramp, 0, 1);
        }
        for (const auto& st : steps) {
            const double s = st.first, g = (yAt(y0, s + 3) - yAt(y0, s)) / 3, pitch = std::atan(g), thick = 1.4;
            if (st.second >= 0) {
                const V3 c = pointAlong(pts, s + 0.75), d = dirAlong(pts, s + 0.75);
                const double top = yAt(y, s + 1.5) - g * 0.75;
                addBox(V3(c.x, top - thick / 2 * std::cos(pitch), c.z), V3(0.85, thick / 2, halfW), std::atan2(-d.z, d.x), pitch, 0, col);
            } else {
                const V3 c = pointAlong(pts, s - 0.35), d = dirAlong(pts, s - 0.35);
                const double top = yAt(y, s) - g * 0.35;
                addBox(V3(c.x, top - thick / 2 * std::cos(pitch), c.z), V3(0.85, thick / 2, halfW), std::atan2(-d.z, d.x), pitch, 0, col);
            }
            boxes.back().rock = true;
        }
    };
    auto dirtTread = [&](int id, double, double) { paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT; };
    auto rockTread = [&](int id, double, double) { paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK; };
    auto gravelTread = [&](int id, double, double) { paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL; };
    auto sandTread = [&](int id, double, double) { paint[id] = PAINT_SAND, surf[id] = SURF_SAND; };

    // ---- THE MUD RUN ---------------------------------------------------------------------------
    // A forest track with four bog holes dug into it, ruts worn along it and its mud stretches
    // painted.
    {
        const int n = (int)mud.size();
        std::vector<double> y = groundProfile(mud, 8, 0.12, {{0, padY(kTrailhead)}, {n - 1, padY(kSawmill)}});
        layTrail(mud, y, 2.6, 14, dirtTread, noTilt);
        // The bog holes: pits wider than the trail (no driving round them), with standing water
        // over the mud bottom.
        for (int b = 0; b < 4; b++) {
            const double R = kBogLen[b] / 2 + 8, L = kBogLen[b];
            const double rim = yAt(y, kBogS[b]);
            scanNear(mud, R, [&](int id, double d, double k, double) {
                const double u = k - kBogS[b];
                const double side = d - 2.0 * (valueNoise(X(id / hN) * 0.3 + b * 7, X(id % hN) * 0.3) - 0.5) - 1.2 * smoothstep(L / 2, 0, std::fabs(u));
                const double f = smoothstep(-L / 2, -L / 2 + 4, u) * smoothstep(L / 2, L / 2 - 4, u) * smoothstep(7.5, 4.0, side);
                if (f <= 0) return;
                const double ybot = rim - kBogDepth[b] + 0.12 * smoothstep(2.6, 6, d);
                hgrid[id] = (float)(hgrid[id] + (std::min((double)hgrid[id], ybot) - hgrid[id]) * f);
                paint[id] = PAINT_MUD, surf[id] = SURF_MUD, keep[id] = 1;
                if (f > 0.6 && hgrid[id] < rim - 0.08) waterY[id] = (float)(rim - 0.07), flowX[id] = flowZ[id] = 0;
            });
        }
        // Ruts (worn two-track grooves), mud stretches painted.
        scanNear(mud, 5.0, [&](int id, double d, double k, double lat) {
            if (d > 3.4 || k < 22 || k > n - 28 || edge[id] > 0.6) return;
            double mudness = 0;
            for (const auto& z : kMudZone) mudness = std::max(mudness, smoothstep(z[0] - 6, z[0] + 3, k) * smoothstep(z[1] + 6, z[1] - 3, k));
            const double depth = 0.07 + 0.11 * mudness;
            const double g = std::exp(-sq((std::fabs(lat) - 1.0) / 0.55)) * smoothstep(3.4, 2.0, d);
            if (waterY[id] < -1e8) hgrid[id] -= (float)(depth * g);
            if (mudness > 0.5 && d < 3.0) paint[id] = PAINT_MUD, surf[id] = SURF_MUD;
        });
    }
    // Shapes the land round a trail to a designed profile: within `fullW` of it the ground is the profile
    // plus off(|lateral|) (a valley's banks rising, a spine's flanks falling), easing back to the land
    // as it was by `outW`; faded out `fade` m from each end.
    auto corridor = [&](const Poly& pts, const std::vector<double>& yc, double fullW, double outW, auto off, double k0, double k1, double fade) {
        scanNear(pts, outW, [&](int id, double d, double k, double lat) {
            if (k < k0 || k > k1 || edge[id] < 6) return;  // (not over another trail's tread)
            const double w = smoothstep(outW, fullW, d) * smoothstep(k0, k0 + fade, k) * smoothstep(k1, k1 - fade, k);
            hgrid[id] += (float)((yAt(yc, k) + off(std::fabs(lat)) - hgrid[id]) * w);
        });
    };
    // ---- CREEK CRAWL ---------------------------------------------------------------------------
    // A gully climbing the hillside from the trailhead to the mine, ledge after ledge.
    std::vector<double> creekY(creek.size());
    {
        const double yT = padY(kTrailhead);
        for (size_t k = 0; k < creekY.size(); k++) creekY[k] = yT + 9.0 * k / (double)creekY.size() + 1.0 * std::sin(k * 0.045) * smoothstep(0, 40, (double)k);
        corridor(creek, creekY, 38, 80, [](double a) { return a < 3 ? 0.0 : 0.9 * (std::min(a, 5.5) - 3) + 0.3 * std::max(0.0, a - 5.5); }, 0, (double)creek.size(), 30);
        std::vector<std::pair<double, double>> steps;
        for (const auto& l : kCreekLedge) steps.push_back({l[0], l[1]});
        addSteps(creek, creekY, steps, 3.8, V3(0.42, 0.38, 0.34));
        layTrail(creek, creekY, 2.9, 3.0, rockTread, noTilt);
    }
    // The mine's yard sits where the creek ends; the ridge starts from it.
    const double mineY = creekY.back();
    levelDisc(kMine, 9, mineY, 7, PAINT_GRAVEL, SURF_GRAVEL);
    // ---- SLICKROCK RIDGE -------------------------------------------------------------------------
    const int ridgeN = (int)ridge.size();
    int ridgeDown = ridgeN - 1;  // where it comes down onto the flood plain
    for (int k = 0; k < ridgeN; k++)
        if (std::fabs(ridge[k].z - riverZ(ridge[k].x)) < 42) {
            ridgeDown = k;
            break;
        }
    {
        // The crest: a spine from the mine's yard, arching over Slab Point and down to the river,
        // its flanks falling away steeply either side.
        const double yEnd = height(ridge[ridgeDown].x, ridge[ridgeDown].z);
        std::vector<double> y(ridgeN);
        for (int k = 0; k < ridgeN; k++) {
            const double u = std::min(1.0, k / (double)ridgeDown);
            y[k] = mineY + (yEnd - mineY) * u + 5 * std::pow(std::sin(M_PI * u), 0.8);
        }
        corridor(ridge, y, 26, 60, [](double a) { return -(0.8 * std::min(std::max(0.0, a - 3), 8.0) + 0.45 * std::max(0.0, a - 11)); }, 26, (double)ridgeDown + 10, 16);
        // Past the river it runs on the flat to Far Side Camp.
        const std::vector<double> rest = groundProfile(ridge, 5, 0.2, {{ridgeDown, y[ridgeDown]}, {ridgeN - 1, padY(kFarSide)}});
        for (int k = ridgeDown + 1; k < ridgeN; k++) y[k] = rest[k];
        std::vector<std::pair<double, double>> steps;
        for (const auto& l : kRidgeStep) steps.push_back({l[0], l[1]});
        addSteps(ridge, y, steps, 3.0, V3(0.4, 0.38, 0.35));
        // Off-camber slabs: the tread leans toward the drop, one way and then the other.
        auto lean = [](double k) {
            auto bump = [](double k, double a, double b) { return smoothstep(a, a + 10, k) * smoothstep(b, b - 10, k); };
            return 0.15 * bump(k, 90, 132) - 0.15 * bump(k, 176, 222);
        };
        layTrail(ridge, y, 3.0, 6, rockTread, lean);
        // Bare slickrock on the ridge's flanks.
        scanNear(ridge, 14, [&](int id, double d, double k, double) {
            if (k > ridgeDown + 4 || d > 9 + 3 * valueNoise(X(id / hN) * 0.2, X(id % hN) * 0.2)) return;
            if (paint[id] == PAINT_NONE) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
            keep[id] = 1;
        });
    }
    // ---- The rest of the network ---------------------------------------------------------------
    layTrail(rroad, groundProfile(rroad, 10, 0.1, {{0, padY(kSawmill)}, {(int)rroad.size() - 1, padY(kFarSide)}}), 3.2, 14, dirtTread, noTilt);
    {
        // HEARTBREAK HILL: straight over the top; loose gravel up the steep face.
        std::vector<double> y = groundProfile(hill, 3, 0.34, {{0, padY(kSawmill)}, {(int)hill.size() - 1, height(hill.back().x, hill.back().z)}});
        layTrail(hill, y, 2.8, 8, [&](int id, double, double k) {
            if (k > 24 && k < 78) paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL;
            else paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT;
        }, noTilt);
    }
    layTrail(wash, groundProfile(wash, 4, 0.2, {{0, padY(kSawmill)}, {(int)wash.size() - 1, height(wash.back().x, wash.back().z)}}), 3.2, 12, sandTread, noTilt);
    {
        // The wash: sand wall to wall (about 12 m of it), a dry channel; the bowl all sand.
        scanNear(wash, 8, [&](int id, double d, double, double) {
            if (d < 6.5 && paint[id] != PAINT_SAND && edge[id] > 0.5) paint[id] = PAINT_SAND, surf[id] = SURF_SAND, keep[id] = 1;
        });
        for (int i = cellI(kDunes.x - 90); i <= cellI(kDunes.x + 90); i++)
            for (int j = cellI(kDunes.z - 90); j <= cellI(kDunes.z + 90); j++) {
                const double x = X(i), z = X(j), d = std::hypot(x - kDunes.x, z - kDunes.z) + 8 * (valueNoise(x * 0.05, z * 0.05) - 0.5);
                if (d < 72) paint[i * hN + j] = PAINT_SAND, surf[i * hN + j] = SURF_SAND, keep[i * hN + j] = 1;
            }
    }
    layTrail(tower, groundProfile(tower, 10, 0.15, {{0, padY(passHutAt())}, {(int)tower.size() - 1, height(tower.back().x, tower.back().z)}}), 3.0, 18, dirtTread, noTilt);
    layTrail(cabinT, groundProfile(cabinT, 6, 0.12, {{0, height(cabinT[0].x, cabinT[0].z)}}), 2.6, 12, dirtTread, noTilt);
    // ---- SHELF ROAD ----------------------------------------------------------------------------
    std::vector<double> shelfY = groundProfile(shelf, 8, 0.13, {{0, padY(kFarSide)}});
    layTrail(shelf, shelfY, 2.6, 2.5, dirtTread, noTilt);
    {
        // The canyon east of the road: a plateau at the road's level, cut by a canyon whose rim
        // drops almost sheer to a flat floor, deeper the further on.
        const int n = (int)shelf.size();
        scanNear(shelf, 130, [&](int id, double d, double k, double lat) {
            if (lat > 0) return;  // west of the road is hillside
            const double o = d - 2.6;
            if (o < 0.4) return;
            const double sy = yAt(shelfY, k), fy = 4 + 0.015 * k, D = clampd(sy - fy, 0, 36);
            const double Dd = D / 3.0, floorW = 22;
            // The land before the cut: raised to the road's level for 90 m (then easing back).
            const double nat = hgrid[id];
            const double plateau = nat + (std::max(nat, sy - 1.0) - nat) * smoothstep(130, 90, o);
            const double T = o <= Dd ? sy - 3.0 * o : o <= Dd + floorW ? fy : fy + 0.9 * (o - Dd - floorW);
            const double w = smoothstep(2, 10, k) * smoothstep(8, 70, n - 1 - k);
            const double target = std::min(plateau, T);
            hgrid[id] = (float)(nat + (target - nat) * w);
            if (o < Dd + floorW + 6 && hgrid[id] < sy - 3) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
            if (o < Dd + floorW + 8) keep[id] = 1;
        });
    }
    layTrail(pass, groundProfile(pass, 8, 0.12, {{0, shelfY.back()}, {(int)pass.size() - 1, padY(passHutAt())}}), 2.8, 16, dirtTread, noTilt);
    // The wreck spur runs along the canyon floor.
    layTrail(wreckT, groundProfile(wreckT, 6, 0.13, {{0, height(wreckT[0].x, wreckT[0].z)}}), 2.6, 5, gravelTread, noTilt);

    // The camps, flat again where the trails ran into them.
    for (const Pad& p : pads) levelDisc(p.c, p.r + 3, p.y, 9, PAINT_PAD, SURF_PAVED);

    // ---- Elk River: a carved channel with water on the grid and its flow --------------------------
    // Water level: the land along the bank, smoothed, always falling toward the mouth (west).
    // Two crossings: the FORD (shallow gravel) and the DEEP CROSSING (about a metre of water).
    {
        const Poly& pts = rivr;
        const int n = (int)pts.size();
        const V3 ford(kFordX, 0, riverZ(kFordX)), deepX(kDeepX, 0, riverZ(kDeepX));
        std::vector<double> lvl(n), depth(n), bankSlope(n);
        for (int k = 0; k < n; k++) {
            double lo = 1e9;
            for (int o = -1; o <= 1; o += 2) {
                const V3 d = norm(pts[std::min(n - 1, k + 1)] - pts[std::max(0, k - 1)]);
                lo = std::min(lo, height(pts[k].x - d.z * o * (kRiverHalf + 3), pts[k].z + d.x * o * (kRiverHalf + 3)));
            }
            lvl[k] = lo - 0.6;
        }
        for (int pass2 = 0; pass2 < 40; pass2++)
            for (int k = 1; k + 1 < n; k++) lvl[k] = 0.25 * lvl[k - 1] + 0.5 * lvl[k] + 0.25 * lvl[k + 1];
        for (int k = 1; k < n; k++) lvl[k] = std::min(lvl[k], lvl[k - 1] - 0.002);
        for (int k = 0; k < n; k++) {
            const double df = std::hypot(pts[k].x - ford.x, pts[k].z - ford.z), dd = std::hypot(pts[k].x - deepX.x, pts[k].z - deepX.z);
            depth[k] = 1.7;
            depth[k] += (0.34 - depth[k]) * smoothstep(34, 15, df);
            depth[k] += (0.88 - depth[k]) * smoothstep(26, 9, dd);
            bankSlope[k] = 0.45 + (0.16 - 0.45) * std::max(smoothstep(60, 30, df), smoothstep(50, 26, dd));
        }
        const double reach = kRiverHalf + 10;
        std::vector<float> nearestD(N, 1e9f);
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
            if (L < 1e-6) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / (L * L), 0, 1);
                    const double dd = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    if (dd > reach) continue;
                    const int id = i * hN + j;
                    const double level = lvl[s] + (lvl[s + 1] - lvl[s]) * t, dep = depth[s] + (depth[s + 1] - depth[s]) * t;
                    const double wq = kRiverHalf * (0.85 + 0.3 * valueNoise(x * 0.05, z * 0.05));
                    if (dd < wq) {
                        const double u = dd / wq;
                        hgrid[id] = (float)std::min((double)hgrid[id], level - dep * (1 - u * u) - 0.05);
                        surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        if (dd < nearestD[id]) {
                            nearestD[id] = (float)dd;
                            waterY[id] = (float)level;
                            // Faster where it runs downhill, slower spread over a crossing.
                            const double fall = (lvl[s] - lvl[s + 1]) / L;
                            const double sp = std::min(2.6, 1.8 * (0.6 + 40 * fall)) * (1 - 0.6 * u * u) * (dep < 0.6 ? 0.7 : 1.0);
                            flowX[id] = (float)(dx / L * sp);
                            flowZ[id] = (float)(dz / L * sp);
                        }
                        keep[id] = 1;
                    } else {
                        // Banks: down to just above the water, sandy gravel at the edge.
                        const double bank = level + 0.2 + (dd - wq) * bankSlope[s];
                        if (hgrid[id] > bank) hgrid[id] = (float)(hgrid[id] + (bank - hgrid[id]) * smoothstep(reach, wq, dd));
                        if (dd < wq + 2.5 && paint[id] == PAINT_NONE) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        if (dd < wq + 4) keep[id] = 1;
                    }
                }
        }
    }

    // ---- Paint the land: bare rock on the steep and the knobs, scree, snow in the north ----------------
    auto slopeAtCell = [&](int i, int j) {
        const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / (2 * kRes), gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / (2 * kRes);
        return std::hypot(gx, gz);
    };
    auto snowline = [&](double x, double z) { return 48 + 8 * (fbmNoise(x * 0.02 + 3.0, z * 0.02 - 8.0, 3) - 0.5); };
    for (int i = 1; i < hN - 1; i++)
        for (int j = 1; j < hN - 1; j++) {
            const int id = i * hN + j;
            const double x = X(i), z = X(j), h = hgrid[id];
            const double north = smoothstep(150, 260, z - riverZ(x));
            const bool snowy = north > 0.5 && h > snowline(x, z);
            if (paint[id] == PAINT_TRAIL && snowy) {  // the high trails are packed snow
                paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
                continue;
            }
            if (paint[id] != PAINT_NONE || surf[id] != SURF_GRASS) continue;
            const double slope = slopeAtCell(i, j);
            if (snowy && slope < 0.85) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            else if (slope > 0.85 || (h > 95 && north > 0.5)) surf[id] = SURF_ROCK;
            else if (h > 42 && slope > 0.55 && valueNoise(x * 0.04, z * 0.04) > 0.6) paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
        }

    // The wash-down bay at the trailhead (wet pavement), black ice on the pass road, a frozen pond by
    // the hut.
    for (int i = cellI(kTrailhead.x + 4); i <= cellI(kTrailhead.x + 16); i++)
        for (int j = cellI(kTrailhead.z + 8); j <= cellI(kTrailhead.z + 16); j++) paint[i * hN + j] = PAINT_WET, surf[i * hN + j] = SURF_WET;
    for (const double k0 : {0.55, 0.8}) {
        const double k = k0 * (double)pass.size();
        for (double q = k; q < k + 9; q += 0.5) {
            const V3 p = pointAlong(pass, q);
            for (int i = cellI(p.x - 3.5); i <= cellI(p.x + 3.5); i++)
                for (int j = cellI(p.z - 3.5); j <= cellI(p.z + 3.5); j++)
                    if (std::hypot(X(i) - p.x, X(j) - p.z) < 2.9 * (0.6 + 0.4 * valueNoise(X(i) * 0.4, X(j) * 0.4)) && paint[i * hN + j] == PAINT_SNOW)
                        paint[i * hN + j] = PAINT_ICE, surf[i * hN + j] = SURF_ICE;
        }
    }
    {
        const V3 c = passHutAt() + V3(18, 0, -22);
        levelDisc(c, 11, height(c.x, c.z) - 0.3, 7, PAINT_ICE, SURF_ICE);
    }

    // ---- Water: the creek down the crawl, the canyon's stream --------------------------------------
    auto stream = [&](const Poly& pts, double k0, double k1, double halfW, double depth, double speed, bool downToStart) {
        scanNear(pts, halfW + 1, [&](int id, double d, double k, double) {
            if (d > halfW || k < k0 || k > k1 || waterY[id] > -1e8) return;
            const V3 dir = dirAlong(pts, k);
            const double sgn = downToStart ? -1 : 1, u = d / halfW;
            hgrid[id] = (float)(hgrid[id] - 0.12 * (1 - u * u));
            waterY[id] = (float)(hgrid[id] + depth * (1 - 0.6 * u * u));
            flowX[id] = (float)(sgn * dir.x * speed), flowZ[id] = (float)(sgn * dir.z * speed);
            if (paint[id] == PAINT_ROCK || paint[id] == PAINT_GRAVEL) paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL;
        });
    };
    stream(creek, 24, (double)creek.size() - 12, 1.1, 0.1, 0.8, true);
    stream(cany, 20, (double)cany.size() - 40, 1.6, 0.14, 1.0, true);
    stream(wreckT, 20, (double)wreckT.size() - 1, 1.3, 0.12, 0.9, true);

    // ---- Boulders, ledges and the like on the trails ---------------------------------------------
    uint32_t seedCounter = 40000;
    auto boulder = [&](const V3& p, double r, double top, float tint = 1.0f) {
        const double flat = between(0.6, 1.0);
        addRock(p.x, p.z, V3(r, r * flat, r * between(0.7, 1.0)), std::min(top, r * flat * 1.7), seedCounter++, tint);
    };
    auto leftOf = [&](const Poly& pts, double k) {
        const V3 d = dirAlong(pts, k);
        return V3(-d.z, 0, d.x);
    };
    // The creek: a boulder garden in the bed (rocks to crawl over, never taller than a set-up
    // truck climbs), big ones along the banks.
    {
        const int n = (int)creek.size();
        for (double k = 14; k < n - 10; k += 1.0) {
            bool nearLedge = false;
            for (const auto& l : kCreekLedge) nearLedge |= k > l[0] - 1.5 && k < l[0] + 3.5;
            const bool garden = (k > 28 && k < 64) || (k > 196 && k < 222);
            const V3 p = pointAlong(creek, k), l = leftOf(creek, k);
            if (!nearLedge && rnd() < (garden ? 0.34 : 0.15)) {
                const V3 q = p + l * between(-2.3, 2.3);
                boulder(q, between(0.34, 0.62), between(0.16, 0.34), (float)between(0.9, 1.1));
            }
            if (rnd() < 0.34) {
                const double side = rnd() < 0.5 ? -1 : 1;
                const V3 q = p + l * (side * between(3.3, 4.8));
                const double r = between(0.8, 1.6);
                boulder(q, r, r * between(0.7, 1.15), 0.95f);
            }
        }
    }
    // The ridge: a few rocks on the crest (a line to pick), bigger ones along the flanks.
    {
        for (double k = 20; k < ridgeDown; k += 1.0) {
            const V3 p = pointAlong(ridge, k), l = leftOf(ridge, k);
            if (rnd() < 0.07) boulder(p + l * between(-1.8, 1.8), between(0.3, 0.5), between(0.14, 0.28), 1.1f);
            if (rnd() < 0.15) {
                const double side = rnd() < 0.5 ? -1 : 1, r = between(0.7, 1.7);
                boulder(p + l * (side * between(4.2, 6.5)), r, r * between(0.6, 1.0), 1.05f);
            }
        }
    }
    // Heartbreak Hill: loose rock in the tread on the way up.
    for (double k = 30; k < 78; k += 1.0)
        if (rnd() < 0.16) {
            const V3 p = pointAlong(hill, k), l = leftOf(hill, k);
            boulder(p + l * between(-1.8, 1.8), between(0.25, 0.42), between(0.12, 0.26), 1.1f);
        }
    // The sand wash: boulders along its sides, a few in it.
    for (double k = 10; k < (double)wash.size() - 10; k += 2.0)
        if (rnd() < 0.4) {
            const V3 p = pointAlong(wash, k), l = leftOf(wash, k);
            if (std::hypot(p.x - kDunes.x, p.z - kDunes.z) < 78) continue;  // the bowl is clear
            const double side = rnd() < 0.5 ? -1 : 1, r = between(0.5, 1.4);
            boulder(p + l * (side * between(3.8, 6.5)), r, r * between(0.5, 0.9), 1.2f);
        }
    // The Mud Run: logs lying along it, and across it at the bogs' lips.
    for (int b = 0; b < 4; b++)
        for (int side = -1; side <= 1; side += 2) {
            const double k = kBogS[b] - kBogLen[b] / 2 - 5 + (side > 0 ? 0 : kBogLen[b] + 10);
            const V3 p = pointAlong(mud, k), l = leftOf(mud, k), d = dirAlong(mud, k);
            const double r = between(0.14, 0.2);
            V3 a = p + l * 3.4 + d * 0.5, c = p + l * 5.8 - d * 1.4;
            a.y = height(a.x, a.z) + r * 0.7, c.y = height(c.x, c.z) + r * 0.7;
            logs.push_back({a, c, r});
        }
    for (double k = 40; k < (double)mud.size() - 40; k += 1.0)
        if (rnd() < 0.035) {
            const V3 p = pointAlong(mud, k), l = leftOf(mud, k), d = dirAlong(mud, k);
            const double side = rnd() < 0.5 ? -1 : 1, r = between(0.14, 0.26), L = between(3, 6);
            V3 a = p + l * (side * between(3.4, 5.5)) - d * (L / 2), c = p + l * (side * between(3.4, 5.5)) + d * (L / 2);
            a.y = height(a.x, a.z) + r * 0.7, c.y = height(c.x, c.z) + r * 0.7;
            logs.push_back({a, c, r});
        }

    // ---- Rock steps up Heartbreak Hill are in the profile; the lookouts and the yards -----------------
    auto cairn = [&](const V3& p) { fixtures.push_back({PROP_CAIRN, V3(p.x, height(p.x, p.z), p.z), V3(), between(0, 6.28)}); };
    auto marker = [&](const V3& p) { fixtures.push_back({PROP_MARKER, V3(p.x, height(p.x, p.z), p.z), V3(), 0}); };
    const V3 green(0.1, 0.55, 0.2), blue(0.1, 0.3, 0.8), black(0.03, 0.03, 0.03), red(0.75, 0.08, 0.06);
    auto sign = [&](double x, double z, double yaw, const V3& col, PropKind kind = PROP_SIGN) {
        Prop sg{kind, V3(x, height(x, z), z), V3(), yaw};
        sg.col[0] = (float)col.x, sg.col[1] = (float)col.y, sg.col[2] = (float)col.z;
        fixtures.push_back(sg);
        const double half = kind == PROP_MAPBOARD ? 0.8 : 0.0;
        const V3 sd2(std::sin(yaw), 0, std::cos(yaw));
        for (int k = kind == PROP_MAPBOARD ? -1 : 1; k <= 1; k += 2)
            poles.push_back({x + sd2.x * half * k, z + sd2.z * half * k, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
    };
    // A rating sign `s` metres along a trail, `side` metres to the left of it (negative: right), facing
    // whoever drives up it.
    auto trailSign = [&](const Poly& pts, double k, double side, const V3& col) {
        const V3 p = pointAlong(pts, k), d = dirAlong(pts, k), l(-d.z, 0, d.x);
        const V3 q = p + l * side;
        sign(q.x, q.z, std::atan2(d.z, -d.x), col);
    };
    auto cabinAt = [&](const V3& c, double yaw) {
        const double y = height(c.x, c.z);
        fixtures.push_back({PROP_CABIN, V3(c.x, y, c.z), V3(), yaw});
        addBox(V3(c.x, y + 1.7, c.z), V3(2.7, 1.9, 2.2), yaw, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    };
    auto table = [&](double x, double z, double yaw) {
        const double y = height(x, z);
        fixtures.push_back({PROP_TABLE, V3(x, y, z), V3(), yaw});
        addBox(V3(x, y + 0.4, z), V3(0.95, 0.4, 0.8), yaw, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    };
    auto fence = [&](double x0, double z0, double x1, double z1) {
        V3 a(x0, height(x0, z0), z0), b(x1, height(x1, z1), z1);
        fixtures.push_back({PROP_FENCE, a, b});
        const V3 d = b - a;
        const double L = std::hypot(d.x, d.z), yaw = std::atan2(-d.z, d.x);
        addBox((a + b) * 0.5 + V3(0, 0.62, 0), V3(L / 2, 0.3, 0.06), yaw, std::atan2(d.y, L), 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
        for (double t = 0; t <= L + 0.01; t += 2.5) {
            const V3 p = a + d * (t / L);
            poles.push_back({p.x, p.z, p.y - 0.4, p.y + 1.1, 0.08});
        }
    };
    auto fireRing = [&](const V3& c) {
        for (int k = 0; k < 11; k++) {
            const double a = k * 2 * M_PI / 11;
            addRock(c.x + 0.95 * std::cos(a), c.z + 0.95 * std::sin(a), V3(0.24, 0.17, 0.2), 0.18, 900 + k, 0.8f);
        }
    };
    // A coloured box (a piece of old timber, rusty iron...) standing or lying.
    auto piece = [&](const V3& c, const V3& half, double yaw, double pitch, double roll, const V3& col, bool solid = true) {
        addBox(c, half, yaw, pitch, roll, col);
        boxes.back().solid = solid;
    };

    // TRAILHEAD CAMP: a ranger cabin, the trail map, picnic tables, a fire ring, a fence.
    {
        const V3 t = kTrailhead;
        cabinAt(t + V3(14, 0, 10), 2.0);
        sign(t.x - 14, t.z + 14, M_PI / 2, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
        table(t.x - 24, t.z + 6, 0.2);
        table(t.x - 24, t.z + 14, -0.3);
        fireRing(t + V3(-30, 0, 10));
        fence(t.x - 20, t.z - 22, t.x + 20, t.z - 22);
        fence(t.x - 20, t.z - 22, t.x - 20, t.z - 12);
        fence(t.x + 20, t.z - 22, t.x + 20, t.z - 14);
        trailSign(creek, 6, 6.5, red);   // CREEK CRAWL
        trailSign(mud, 8, 5.5, black);   // THE MUD RUN
    }
    // The trails' ratings, at their starts and where it matters.
    trailSign(ridge, 8, 5.5, black);                                       // SLICKROCK RIDGE
    trailSign(ridge, ridgeDown + 4.0, 5.0, black);                         // the DEEP CROSSING, south bank
    trailSign(rroad, 8, 5.5, green);                                       // RIDGE ROAD
    trailSign(rroad, 118, 5.0, blue);                                      // the FORD, south bank
    trailSign(hill, 8, 5.0, red);                                          // HEARTBREAK HILL
    trailSign(wash, 8, 5.5, blue);                                         // SAND WASH
    trailSign(shelf, 8, 5.0, black);                                       // SHELF ROAD
    trailSign(pass, 6, 5.0, blue);                                         // HIGH PASS
    trailSign(tower, 8, 5.5, green);                                       // TOWER ROAD
    trailSign(cabinT, 8, 5.0, green);                                      // TRAPPER'S CABIN
    trailSign(wreckT, 8, 5.0, green);                                      // WRECK SPUR
    for (int b = 0; b < 4; b++) trailSign(mud, kBogS[b] - kBogLen[b] / 2 - 14, -4.5, black);
    // Posts mark the fords, both banks.
    for (const double fx : {kFordX, kDeepX}) {
        const V3 c(fx, 0, riverZ(fx));
        for (int side = -1; side <= 1; side += 2)
            for (int k = -1; k <= 1; k += 2) marker(c + V3(k * 3.2, 0, side * 14.5));
    }
    // OLD MINE: a timber portal in the hillside, ore cart on rails, tailings, an office.
    {
        const V3 m = kMine + V3(-14, 0, -4);
        const double y = height(m.x, m.z);
        const V3 timber(0.30, 0.20, 0.12);
        piece(V3(m.x, y + 1.1, m.z - 1.7), V3(0.2, 1.1, 0.2), 0, 0, 0, timber);
        piece(V3(m.x, y + 1.1, m.z + 1.7), V3(0.2, 1.1, 0.2), 0, 0, 0, timber);
        piece(V3(m.x, y + 2.35, m.z), V3(0.3, 0.2, 2.1), 0, 0, 0, timber);
        piece(V3(m.x - 0.5, y + 1.0, m.z), V3(0.1, 1.0, 1.55), 0, 0, 0, V3(0.01, 0.01, 0.01));  // the dark way in
        piece(V3(m.x + 6, y + 0.25, m.z - 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);  // rails
        piece(V3(m.x + 6, y + 0.25, m.z + 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);
        piece(V3(m.x + 3.2, y + 0.65, m.z), V3(0.85, 0.35, 0.6), 0.1, 0, 0, V3(0.38, 0.17, 0.1));       // the ore cart
        for (int k = 0; k < 14; k++) {
            const double a = k * 0.9;
            boulder(V3(m.x + 3 + 5 * std::cos(a) * between(0.5, 1.2), 0, m.z + 9 + 3.5 * std::sin(a) * between(0.5, 1.2)), between(0.4, 1.0), between(0.2, 0.6), 1.15f);
        }
        cabinAt(kMine + V3(6, 0, -12), M_PI);
        cairn(kMine + V3(-4, 0, 5));
    }
    // SLAB POINT: a flat rock shelf on the crest with a cairn and a bench.
    {
        const V3 p = pointAlong(ridge, kSlabS), l = leftOf(ridge, kSlabS);
        cairn(p + l * 3.8);
        table(p.x - l.x * 4.6, p.z - l.z * 4.6, std::atan2(-l.z, l.x));
    }
    // CLIFF OVERLOOK: a pull-out over the drop, with a cairn and the trail map.
    {
        const V3 p = pointAlong(shelf, kOverlookS), d = dirAlong(shelf, kOverlookS), r(d.z, 0, -d.x);  // r: toward the canyon
        const double y = yAt(shelfY, kOverlookS);
        levelDisc(p + r * 5.5, 3.2, y, 2.4, PAINT_ROCK, SURF_ROCK);
        cairn(p + r * 8.0);
        sign(p.x - r.x * 4.0, p.z - r.z * 4.0, std::atan2(-r.z, r.x), V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
        // The shelf's outer edge is marked with posts.
        for (double k = 8; k < (double)shelf.size() - 6; k += 13) {
            const V3 q = pointAlong(shelf, k), dd = dirAlong(shelf, k), rr(dd.z, 0, -dd.x);
            marker(q + rr * 3.3);
        }
    }
    // FIRE TOWER: four legs, a platform and a cab, on its knoll.
    {
        const V3 t = towerSite();
        levelDisc(t, 5.5, height(t.x, t.z), 6, PAINT_GRAVEL, SURF_GRAVEL);
        const double y0 = height(t.x, t.z);
        const V3 wood(0.36, 0.30, 0.22), tin(0.45, 0.46, 0.46);
        for (int a = -1; a <= 1; a += 2)
            for (int b = -1; b <= 1; b += 2) {
                const double tilt = 0.075;
                piece(V3(t.x + a * 1.75, y0 + 7.0, t.z + b * 1.75), V3(0.14, 7.2, 0.14), 0, -a * tilt, -b * tilt, wood);
            }
        for (const double hh : {4.5, 9.2}) {
            const double w = 2.0 - hh / 14.0 * 0.5;
            for (int a = -1; a <= 1; a += 2) {
                piece(V3(t.x + a * w, y0 + hh, t.z), V3(0.07, 0.07, w), 0, 0, 0, wood, false);
                piece(V3(t.x, y0 + hh, t.z + a * w), V3(w, 0.07, 0.07), 0, 0, 0, wood, false);
            }
        }
        piece(V3(t.x, y0 + 14.1, t.z), V3(2.3, 0.12, 2.3), 0, 0, 0, wood);
        piece(V3(t.x, y0 + 15.2, t.z), V3(1.7, 1.0, 1.7), 0, 0, 0, V3(0.45, 0.2, 0.12));
        piece(V3(t.x, y0 + 15.3, t.z), V3(1.74, 0.35, 1.74), 0, 0, 0, V3(0.04, 0.06, 0.08), false);  // the windows
        piece(V3(t.x, y0 + 16.35, t.z), V3(2.1, 0.1, 2.1), 0, 0, 0, tin);
        poles.push_back({t.x + 1.75, t.z + 1.75, y0 - 0.5, y0 + 14, 0.16});
        poles.push_back({t.x - 1.75, t.z + 1.75, y0 - 0.5, y0 + 14, 0.16});
        poles.push_back({t.x + 1.75, t.z - 1.75, y0 - 0.5, y0 + 14, 0.16});
        poles.push_back({t.x - 1.75, t.z - 1.75, y0 - 0.5, y0 + 14, 0.16});
    }
    // TRAPPER'S CABIN at the end of its spur, with a woodpile and a table by the fire ring.
    {
        const V3 c = kCabin;
        cabinAt(c + V3(8, 0, 6), 3.5);
        table(c.x - 2, c.z - 7, 0.4);
        fireRing(c + V3(-9, 0, -2));
        piece(V3(c.x + 8, height(c.x + 8, c.z - 4) + 0.5, c.z - 4), V3(1.4, 0.5, 0.5), 0, 0, 0, V3(0.4, 0.28, 0.16));
    }
    // THE WRECK: an old pickup on its side in the canyon, parts thrown about.
    {
        const V3 w = wreckT.back();
        const double y = height(w.x, w.z);
        const V3 rust(0.45, 0.22, 0.1), dark(0.22, 0.15, 0.1), steel(0.32, 0.3, 0.28);
        piece(V3(w.x, y + 0.9, w.z), V3(2.0, 0.08, 0.8), 0.5, 0, 1.45, rust);                       // the bed's side
        piece(V3(w.x + 1.2, y + 0.55, w.z + 0.9), V3(2.3, 0.1, 0.8), 0.5, 0, 0.1, dark);                   // the frame
        piece(V3(w.x - 2.0, y + 0.8, w.z - 1.2), V3(0.9, 0.7, 0.8), 0.5, 0.1, 1.2, rust);                 // the cab
        piece(V3(w.x - 3.0, y + 0.5, w.z - 0.6), V3(0.7, 0.4, 0.75), 0.4, 0.3, 0.2, steel);               // the engine
        for (int k = 0; k < 4; k++) piece(V3(w.x + (k % 2 ? 1.6 : -0.9), y + 0.4, w.z + (k < 2 ? 1.8 : 3.4)), V3(0.35, 0.35, 0.14), 0.5, 0, 1.4, V3(0.03, 0.03, 0.03));
        for (int k = 0; k < 7; k++)
            piece(V3(w.x + between(-5, 5), y + 0.12, w.z + between(-5, 5)), V3(between(0.3, 0.9), 0.05, between(0.2, 0.6)), between(0, 6.28), between(-0.2, 0.2), between(-0.2, 0.2), k % 2 ? rust : steel);
        // a scorched snag beside it
        Tree t;
        t.base = V3(w.x + 4.5, y - 0.2, w.z - 3.5);
        t.height = 9, t.radius = 2, t.tint = 0.5f, t.kind = 2;
        trees.push_back(t);
        poles.push_back({t.base.x, t.base.z, t.base.y - 0.5, t.base.y + 5, 0.22});
        cairn(w + V3(-6, 0, 4));
    }
    // PASS HUT's neighbourhood and the snow poles along the pass and the first of the tower road.
    for (double k = 12; k < (double)pass.size() - 4; k += 18)
        for (int side = -1; side <= 1; side += 2) {
            const V3 p = pointAlong(pass, k), l = leftOf(pass, k);
            if (height(p.x, p.z) > snowline(p.x, p.z) - 6) marker(p + l * (side * 3.9));
        }
    for (double k = 12; k < 200; k += 20)
        for (int side = -1; side <= 1; side += 2) {
            const V3 p = pointAlong(tower, k), l = leftOf(tower, k);
            if (height(p.x, p.z) > snowline(p.x, p.z) - 6) marker(p + l * (side * 4.1));
        }
    cairn(passHutAt() + V3(-10, 0, 8));
    cairn(pointAlong(pass, kSnowCornerS) + leftOf(pass, kSnowCornerS) * 4.5);
    cairn(pointAlong(hill, 66) + leftOf(hill, 66) * 3.5);   // the top of Heartbreak Hill
    // Dune bowl: a post at the ring trail's start.
    marker(kDunes + V3(-kDuneR - 3, 0, 22));

    // ---- Boulders scattered over the hills ------------------------------------------------------------
    for (int attempt = 0; attempt < 40000 && rocks.size() < 900; attempt++) {
        const double x = (rnd() * 2 - 1) * 515, z = (rnd() * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8) continue;
        const int i = cellI(x), j = cellI(z);
        if (i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1) continue;
        const double slope = slopeAtCell(i, j), h = hgrid[id];
        // More on steep ground and up high; a few big outcrops where it's steepest.
        if (rnd() > 0.1 + 0.9 * smoothstep(0.2, 0.7, slope) + 0.2 * smoothstep(30, 90, h)) continue;
        const bool big = slope > 0.55 && rnd() < 0.2;
        const double r = big ? between(2.0, 4.2) : 0.4 + rnd() * rnd() * 1.7, flat = between(0.5, 0.9);
        addRock(x, z, V3(r, r * flat, r * between(0.7, 1.0)), r * flat * between(0.5, 1.0), 20000 + attempt, (float)between(0.85, 1.1));
    }

    // ---- Trees: broadleaf along the river and in the lowlands, pine through the hills up to the
    // treeline, the odd dead snag; meadows between ---------------------------------------------------
    for (int attempt = 0; attempt < 220000 && trees.size() < 3400; attempt++) {
        const double x = (rnd() * 2 - 1) * 520, z = (rnd() * 2 - 1) * 520;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE) continue;
        const int i = cellI(x), j = cellI(z);
        if (i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1) continue;
        const double h = hgrid[id], d = z - riverZ(x), north = smoothstep(150, 260, d);
        const double treeline = north > 0.5 ? snowline(x, z) + 4 : 85;
        if (h > treeline || surf[id] != SURF_GRASS || slopeAtCell(i, j) > 0.8) continue;
        const double forest = fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3);
        double p = 0.04 + 0.95 * smoothstep(0.36, 0.56, forest);
        p *= 1 - 0.55 * smoothstep(60, 20, std::fabs(d));  // open flood plain
        p *= 1 - 0.6 * smoothstep(treeline - 14, treeline, h);
        if (rnd() > p) continue;
        Tree t;
        const double q = rnd();
        if (std::fabs(d) < 120 && rnd() < 0.7) t.kind = 1;
        else t.kind = q < 0.06 ? 2 : (q < 0.3 && h < 40 ? 1 : 0);
        t.base = V3(x, h - 0.2, z);
        t.height = t.kind == 0 ? 9 + rnd() * 9 : t.kind == 1 ? 7 + rnd() * 7 : 6 + rnd() * 5;
        t.height *= 1 - 0.3 * smoothstep(40, 80, h);
        t.radius = t.height * (t.kind == 0 ? 0.2 + rnd() * 0.06 : 0.26 + rnd() * 0.08);
        t.tint = (float)(0.8 + rnd() * 0.4);
        trees.push_back(t);
        poles.push_back({x, z, t.base.y - 0.5, t.base.y + t.height * 0.6, t.kind == 1 ? 0.3 : 0.22});
    }
    // Fallen logs, stumps, bushes.
    for (int attempt = 0; attempt < 30000 && logs.size() < 200; attempt++) {
        const double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3) < 0.5) continue;
        const double r = between(0.18, 0.36), L = between(4, 10), a = between(0, 2 * M_PI);
        V3 p0(x - std::cos(a) * L / 2, 0, z - std::sin(a) * L / 2), p1(x + std::cos(a) * L / 2, 0, z + std::sin(a) * L / 2);
        if (keep[at(p0.x, p0.z)] || keep[at(p1.x, p1.z)]) continue;
        p0.y = height(p0.x, p0.z) + r * 0.8, p1.y = height(p1.x, p1.z) + r * 0.8;
        logs.push_back({p0, p1, r});
    }
    for (int attempt = 0; attempt < 30000 && fixtures.size() < 230; attempt++) {
        const double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3) < 0.48) continue;
        const double r = between(0.22, 0.4), hh = between(0.3, 0.7), y = hgrid[id];
        fixtures.push_back({PROP_STUMP, V3(x, y, z), V3(r, hh, 0), between(0, 6.28)});
        poles.push_back({x, z, y - 0.3, y + hh, r});
    }
    for (int attempt = 0; attempt < 60000 && bushes.size() < 2600; attempt++) {
        const double x = (rnd() * 2 - 1) * 515, z = (rnd() * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || surf[id] != SURF_GRASS || hgrid[id] > 70) continue;
        const double forest = fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3);
        if (rnd() > 0.15 + 0.6 * smoothstep(0.3, 0.55, forest)) continue;
        bushes.push_back({V3(x, hgrid[id], z), between(0.4, 1.3), (float)between(0.75, 1.2)});
    }

    // ---- Stations, start ----------------------------------------------------------------------
    for (const Pad& p : pads) addStation(V3(p.c.x, p.y, p.c.z), p.yaw, p.name);
    spawn = V3(kTrailhead.x - 8, pads[0].y, kTrailhead.z + 7);
    spawnYaw = 0;
    parking = {V3(kTrailhead.x - 8, 0, kTrailhead.z + 13), V3(kTrailhead.x - 8, 0, kTrailhead.z + 19), V3(kTrailhead.x - 16, 0, kTrailhead.z + 7)};
    parkingYaw = 0;
}

V3 backcountryFeature(const char* name) {
    const std::string n = name;
    if (n == "trailhead" || n == "start") return kTrailhead;
    if (n == "sawmill") return kSawmill;
    if (n == "farside") return kFarSide;
    if (n == "passhut") return passHutAt();
    if (n == "mine") return kMine;
    if (n == "firetower") return towerSite();
    if (n == "cabin") return kCabin;
    if (n == "dunes") return kDunes;
    if (n == "hilltop") return kHill;
    if (n == "ford") return V3(kFordX, 0, riverZ(kFordX));
    if (n == "deepcrossing") return V3(kDeepX, 0, riverZ(kDeepX));
    if (n == "wreck") return wreckTrail().back();
    if (n == "slab") return pointAlong(ridgeTrail(), kSlabS);
    if (n == "overlook") return pointAlong(shelfTrail(), kOverlookS);
    if (n == "snowcorner") return pointAlong(passTrail(), kSnowCornerS);
    if (n == "creekfoot") return pointAlong(creekTrail(), 6);
    if (n == "mudstart") return pointAlong(mudTrail(), 8);
    if (n == "shelf") return pointAlong(shelfTrail(), 6);
    if (n.rfind("bog", 0) == 0) {
        const int b = std::atoi(n.c_str() + 3) - 1;
        return b >= 0 && b < 4 ? pointAlong(mudTrail(), kBogS[b]) : V3();
    }
    return V3();
}

const std::vector<V3>& backcountryTrail(const char* name) {
    const std::string n = name;
    if (n == "mud") return mudTrail();
    if (n == "creek") return creekTrail();
    if (n == "ridge") return ridgeTrail();
    if (n == "ridgeroad") return ridgeRoadTrail();
    if (n == "hill") return hillTrail();
    if (n == "wash") return washTrail();
    if (n == "shelf") return shelfTrail();
    if (n == "pass") return passTrail();
    if (n == "tower") return towerTrail();
    if (n == "cabin") return cabinTrail();
    if (n == "wreck") return wreckTrail();
    if (n == "river") return riverLine();
    static const Poly none;
    return none;
}
