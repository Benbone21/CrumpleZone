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

#include "loadprof.h"
#include "onemap.h"
#include "parallel.h"
#include "places.h"
#include "regionland.h"
#include "rivers.h"
#include "world.h"

namespace {

using Poly = std::vector<V3>;

double sq(double v) { return v * v; }

// Elk River's centre line: z as a function of x (it flows west).
double riverZ(double x) { return elkRiverZ(x); }
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
    return 2.6 + 0.135 * softRamp(z - 110, 30) + 0.215 * softRamp(z - 362, 30) + 0.07 * softRamp(z - 470, 30) + 30 * smoothstep(160, 460, std::fabs(x - 90));
}
// A switchback trail laid out like a trail crew would: traverse the slope at a steady `grade` (+
// climbing, - descending), a half-circle hairpin at each end of the traverse, until `stop`.
template <class Stop>
std::vector<V3> crewTrail(V3 pos, double grade, double xMin, double xMax, int dir, Stop stop) {
    std::vector<V3> pts{pos};
    const double step = 2.0;
    auto h = [](const V3& p) { return macroHeight(p.x, p.z); };
    for (int n = 0; n < 1200 && !stop(pos, dir); n++) {
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
        std::vector<V3> p = crewTrail(V3(14, 0, 362), 0.07, -30, 110, 1, [](const V3& q, int dir) { return dir > 0 && q.x > 90 && (macroHeight(q.x, q.z) >= 60 || q.z > 470); });
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
        const V3 first(hut.x + 9, 0, hut.z - 3);
        std::vector<V3> p = crewTrail(first, -0.12, hut.x - 20, 215, 1, [](const V3& q, int) { return macroHeight(q.x, q.z) < 9; });
        std::vector<V3> out{first};
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
constexpr double kCreekLedge[8][2] = {{70, 0.20}, {112, 0.26}, {150, 0.32}, {188, 0.36}, {224, 0.40}, {262, 0.42}, {300, 0.44}, {334, 0.45}};  // s, rise
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
// ---- The country round the old map (docs/plans/big-maps.md 3.5) -------------------------------------
// The map is 6 x 6 km; the old 1.2 km Backcountry is its middle (Elk Valley), as it was out to kInner, and the new country
// takes over by kBlend. Round it, eight districts: Timber Basin (north-west), Copper Ridge (north), High Lakes (north-east),
// Ranger Flats (west), Bear Creek Canyon (east), Lakeshore (south-west), the south uplands and Badlands Wash (south-east).
constexpr double kInner = 520, kBlend = 900;
struct Districts {
    double n, s, e, w;  // how far into the north, south, east and west rows of districts (0..1; n and s never both, nor e and w)
};
Districts districtsAt(double x, double z) {
    return {smoothstep(450, 1500, z), smoothstep(-450, -1500, z), smoothstep(450, 1500, x), smoothstep(-450, -1500, x)};
}
// Elk River's level across the country: rising east into Bear Creek Canyon, falling gently west.
// In the canyon it climbs in steps besides, at Bear Falls and Upper Falls (rivers.h): riverFloor. (The land adds those steps per
// cell, sharp: the new country's land is sampled every 4 m, which would smear a cliff.)
double valleyFloor(double x) { return x > 540 ? 2.6 + 0.009 * (x - 540) : x < -540 ? 2.6 - 0.0008 * (-540 - x) : 2.6; }
double riverFloor(double x) { return valleyFloor(x) + elkFallsRise(x, riverZ(x)); }
// Elk River's mouth on the big map (onemap.h): a lake on the seam with the Open Country, west of where the old map's edge was.
const V3 kMouthLake(-2840, 0, 18);
constexpr double kMouthLakeR = 100;
// The big lake on the Lakeshore.
const V3 kLake(-1850, 0, -1650);
constexpr double kLakeR = 480, kLakeY = 1.2;
double lakeDist(double x, double z) { return std::hypot(x - kLake.x, z - kLake.z) + 60 * (valueNoise(x * 0.004 + 3.3, z * 0.004 - 1.1) - 0.5); }
// How far the land at (x, z) is out of Elk River's valley (0 on its floor, 1 up on the hills): the falls' steps rise with the floor.
double valleyOpen(double x, double z, double east) {
    const double dv = std::fabs(z - riverZ(x));
    const double open = smoothstep(40, 380, dv), gorge = smoothstep(26, 90, dv);
    return open + (gorge - open) * east;
}
// The new country's land (smooth: the build adds the metre-scale bumps). `seams`: the big map's (onemap.h), where the edge ridge opens
// into a valley wherever a region meets it.
double outerLand(double x, double z, bool seams) {
    if (seams) {
        // Wholly a region's grown country (regionland.h): the Backcountry's own land would count for nothing there (the same sum as
        // blendRegionLand's, without working it out).
        double w[3];
        regionWeights(x, z, w);
        if (w[0] + w[1] + w[2] == 1.0) {
            double h = 0;
            for (int k = 0; k < 3; k++)
                if (w[k] > 0) h += w[k] * regionLand(mapRegions()[k], x, z);
            return h + bigMapEdgeRidge(x, z) * (70 + 90 * ridgedNoise(x * 0.0024 + 0.7, z * 0.0024 + 2.9, 4));
        }
    }
    const Districts d = districtsAt(x, z);
    const double rowMid = std::max(0.0, 1 - d.n - d.s), colMid = std::max(0.0, 1 - d.e - d.w);
    const double roll = fbmNoise(x * 0.0021 + 3.1, z * 0.0021 - 7.3, 3) - 0.5;  // broad swells
    const double hill = fbmNoise(x * 0.0065 - 1.7, z * 0.0065 + 4.2, 3) - 0.5;  // hills
    const double ridge = ridgedNoise(x * 0.0024 + 0.7, z * 0.0024 + 2.9, 4);     // ridges, 0..1
    const double flats = 10 + 16 * roll + 4 * hill;
    const double timber = 45 + 40 * roll + 22 * hill;
    // (Sharp ridges in ranges, broad open valleys and saddles between them: a way through for whoever looks for it.)
    const double range = smoothstep(0.42, 0.62, fbmNoise(x * 0.0011 - 6.0, z * 0.0011 + 2.5, 2));
    const double copper = 100 + 40 * hill + range * 175 * std::pow(ridge, 2.2) + 30 * roll;
    const double lakes = 172 + 24 * roll + 9 * hill - 14 * smoothstep(0.55, 0.7, fbmNoise(x * 0.003 + 5.0, z * 0.003 + 5.0, 2));
    const double canyon = 72 + 24 * roll + 12 * hill;
    const double bad = 22 + 12 * hill + 22 * smoothstep(0.57, 0.62, fbmNoise(x * 0.004 + 9.0, z * 0.004 - 2.0, 3));  // mesas
    const double shore = 12 + 18 * roll + 6 * hill;
    const double south = 40 + 30 * roll + 18 * hill + 14 * ridge;
    const double mid = 22 + 22 * roll + 12 * hill;
    double h = d.n * (d.w * timber + colMid * copper + d.e * lakes) + d.s * (d.w * shore + colMid * south + d.e * bad) +
               rowMid * (d.w * flats + colMid * mid + d.e * canyon);
    // Elk River's valley right across: wide and gentle, a canyon in the east.
    const double floor = valleyFloor(x);
    h = floor + (h - floor) * valleyOpen(x, z, d.e);
    // The lake's basin.
    const double dl = lakeDist(x, z);
    if (dl < kLakeR + 260) {
        const double u = std::min(1.0, dl / kLakeR), bed = kLakeY - 0.4 - 6.5 * (1 - u * u);
        h = bed + (h - bed) * smoothstep(kLakeR - 30, kLakeR + 260, dl);
    }
    // The map's edge: the land rises into a ridge you won't drive over.
    const double e = std::max(std::fabs(x), std::fabs(z));
    h += smoothstep(2720, 3050, e) * (55 + 90 * ridge) * (seams ? 1 - edgeRidgeOpening(x, z) : 1.0);
    if (seams) {
        h = blendRegionLand(x, z, h);                // (the regions' grown country: regionland.h; nothing within kSeamFrom)
        h += bigMapEdgeRidge(x, z) * (70 + 90 * ridge);  // (the big map's own edge, wherever the ridge above was opened)
    }
    return h;
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

// The Backcountry's land before anything is laid on it (`n` cells a side, half-size `half`): its build starts from this, and the
// world cache (worldcache.cpp) stores a built map's heights as the difference from it.
void backcountryLand(double half, int hN, std::vector<float>& out) {
    out.assign((size_t)hN * hN, 0.0f);
    auto X = [&](int i) { return -half + i * 1.0; };
    const bool seams = half > kOldBackcountryHalf + 1;  // (the big map: onemap.h)
    // The new country, a sample every 4 m (smooth: bilinear between them, the bumps added per cell).
    const int mn = (int)std::ceil(2 * half / 4.0) + 1;
    std::vector<float> macro((size_t)mn * mn);
    parallelRows(mn, [&](int a) {
        for (int b = 0; b < mn; b++) {
            const double x = -half + a * 4.0, z = -half + b * 4.0;
            macro[(size_t)a * mn + b] = std::max(std::fabs(x), std::fabs(z)) < kInner - 8 ? 0.0f : (float)outerLand(x, z, seams);
        }
    });
    auto outerAt = [&](double x, double z) {
        const double u = (x + half) / 4.0, v = (z + half) / 4.0;
        const int a = std::min(mn - 2, (int)u), b = std::min(mn - 2, (int)v);
        const double fu = u - a, fv = v - b;
        auto M = [&](int p, int q) { return (double)macro[(size_t)p * mn + q]; };
        return M(a, b) * (1 - fu) * (1 - fv) + M(a + 1, b) * fu * (1 - fv) + M(a, b + 1) * (1 - fu) * fv + M(a + 1, b + 1) * fu * fv +
               0.9 * (valueNoise(x * 0.09 + 5.0, z * 0.09 + 1.0) - 0.5) + 0.35 * (valueNoise(x * 0.31, z * 0.27) - 0.5) +
               (x > elkFalls()[0].x - 330 ? elkFallsRise(x, z) * (1 - valleyOpen(x, z, districtsAt(x, z).e)) : 0.0);
    };
    parallelRows(hN, [&](int i) {
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j);
            const double far = std::max(std::fabs(x), std::fabs(z));
            if (far >= kBlend) {
                out[(size_t)i * hN + j] = (float)outerAt(x, z);
                continue;
            }
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
            // Past the old map's edge (where its rim mountains were), into the new country.
            if (far > kInner) h += (outerAt(x, z) - h) * smoothstep(kInner, kBlend, far);
            out[(size_t)i * hN + j] = (float)h;
        }
    });
}

// ================================================================================================
void World::buildBackcountry() {
    loadprof::Steps steps("bc.start");
    map = MAP_BACKCOUNTRY;
    crashProps = false;
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -half + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    auto at = [&](double x, double z) -> int { return cellI(x) * hN + cellI(z); };
    // The big map (onemap.h): the regions' old maps are built on workers meanwhile, and stitched on once the land is made.
    const bool stitch = half > kOldBackcountryHalf + 1;
    std::vector<RegionBuild*> regionBuilds;
    if (stitch) regionBuilds = startRegionBuilds();

    steps.next("bc.the land");
    // ---- The land -----------------------------------------------------------------------------
    backcountryLand(half, hN, hgrid);
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    waterY.reset(hN, -1e9f);
    flowX.reset(hN, 0);
    flowZ.reset(hN, 0);
    // (Kept only in the 64 m tiles where they're set: on a 6 km map these would be most of a gigabyte as plain arrays.)
    CellTiles<uint8_t> keep;  // no trees, rocks or bushes here
    CellTiles<float> edge;    // distance past the nearest trail's tread
    keep.reset(hN, 0), edge.reset(hN, 1e9f);
    if (stitch) {
        steps.next("bc.regions land");
        finishRegionBuilds(regionBuilds);
        stitchRegionLand(regionBuilds, keep);
        // (Elk River, its mouth lake and the Lakeshore's lake are laid on later: the roads keep clear of where they'll be.)
        const std::function<bool(double, double)> wetLater = [&](double x, double z) {
            return (x > kMouthLake.x && x < 3100 && std::fabs(z - riverZ(x)) < 45) || std::hypot(x - kMouthLake.x, z - kMouthLake.z) < kMouthLakeR * 1.15 + 90 ||
                   lakeDist(x, z) < kLakeR + 120;
        };
        layRegionRoads(regionBuilds, keep, wetLater);
        layRegionTrails(regionBuilds, keep, wetLater);  // (the regions' grown country: regionland.h)
    }

    // Scratch for scanning cells near a polyline.
    CellTiles<float> sd, sk, sl;
    sd.reset(hN, 1e9f), sk.reset(hN, 0), sl.reset(hN, 0);
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
    auto levelDisc = [&](const V3& c, double r, double y, double blend, uint8_t pnt, uint8_t sf, bool keepTreads = false) {
        for (int i = cellI(c.x - r - blend); i <= cellI(c.x + r + blend); i++)
            for (int j = cellI(c.z - r - blend); j <= cellI(c.z + r + blend); j++) {
                const double d = std::hypot(X(i) - c.x, X(j) - c.z);
                if (d > r + blend) continue;
                const int id = i * hN + j;
                if (keepTreads && d > r && edge[id] < 3.5) continue;  // (a trail running in keeps its own heights)
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

    const Poly &mud = mudTrail(), &creek = creekTrail(), &ridge = ridgeTrail(), &rroad = ridgeRoadTrail(), &hill = hillTrail(), &wash = washTrail(),
               &shelf = shelfTrail(), &pass = passTrail(), &tower = towerTrail(), &cabinT = cabinTrail(), &rivr = riverLine(), &wreckT = wreckTrail(), &cany = canyon();

    steps.next("bc.landforms hills the");
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

    steps.next("bc.camps level pads");
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

    steps.next("bc.rock steps across");
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

    steps.next("bc.the mud run");
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
    steps.next("bc.creek crawl");
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
    {
        // The hillside the mine is dug into: a rocky knoll west of the yard, with a level apron at its foot.
        const V3 k(kMine.x - 27, 0, kMine.z - 4);
        for (int i = cellI(k.x - 24); i <= cellI(k.x + 24); i++)
            for (int j = cellI(k.z - 24); j <= cellI(k.z + 24); j++) {
                const double d = std::hypot(X(i) - k.x, X(j) - k.z), e = d + 3 * (valueNoise(X(i) * 0.15, X(j) * 0.15) - 0.5);
                const int id = i * hN + j;
                hgrid[id] += (float)(13 * std::exp(-sq(d / 8.5)));
                if (e < 10 && paint[id] == PAINT_NONE) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK, keep[id] = 1;
            }
        levelDisc(kMine + V3(-14, 0, -3), 4.5, mineY, 3, PAINT_GRAVEL, SURF_GRAVEL);
    }
    steps.next("bc.slickrock ridge");
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
    steps.next("bc.the rest of");
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
    steps.next("bc.shelf road");
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
    for (const Pad& p : pads) levelDisc(p.c, p.r + 3, p.y, 9, PAINT_PAD, SURF_PAVED, true);

    steps.next("bc.elk river a");
    Poly eastRiver, westRiver;  // (Elk River out in the new country, kept for its rocks and falls below)
    std::vector<double> eastLevel, westLevel;
    std::vector<RiverStretch> eastStretch, westStretch;
    size_t riverRocks = 0;  // (the rocks its bed gets: the old map's scatter below counts to its cap without them)
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
        CellTiles<float> nearestD;
        nearestD.reset(hN, 1e9f);
        // st: the stretches out in the new country (rivers.h: each point's width, current and bed; through the falls, no water).
        auto carve = [&](const Poly& pts, const std::vector<double>& lvl, const std::vector<double>& depth, const std::vector<double>& bankSlope,
                         const std::vector<RiverStretch>* st = nullptr) {
        const int n = (int)pts.size();
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
            if (L < 1e-6) continue;
            const RiverStretch* sa = st ? &(*st)[s] : nullptr;
            const RiverStretch* sb = st ? &(*st)[s + 1] : nullptr;
            const double ws = st ? std::max(sa->widthScale, sb->widthScale) : 1.0;
            const bool drop = st && (sa->cls == RIVER_FALLS || sb->cls == RIVER_FALLS);
            const double reach = st ? kRiverHalf * ws + 10 : kRiverHalf + 10;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / (L * L), 0, 1);
                    const double dd = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    if (dd > reach) continue;
                    const int id = i * hN + j;
                    // (Out in the new country the falls part it: the cliff's face is only the drop's, and a reach doesn't carve or
                    // water the land beyond a falls, above it or below.)
                    if (st && (drop ? elkFallsFace(x, z) < 0 : elkFallsFace(x, z) >= 0 || elkFallsZone(x, z) != elkFallsZone(0.5 * (a.x + b.x), 0.5 * (a.z + b.z)))) continue;
                    const double level = lvl[s] + (lvl[s + 1] - lvl[s]) * t, dep = depth[s] + (depth[s + 1] - depth[s]) * t;
                    const double wq = kRiverHalf * ws * (0.85 + 0.3 * valueNoise(x * 0.05, z * 0.05));
                    const bool rockBed = st && (sa->bed == BED_ROCK || sb->bed == BED_ROCK);
                    if (drop) {  // over the lip: bare rock, and no water on the grid (the falls model is the water)
                        if (dd < wq + 2) {
                            if (dd < nearestD[id]) nearestD[id] = (float)dd, waterY[id] = -1e9f, flowX[id] = flowZ[id] = 0;
                            surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK, keep[id] = 1;
                        }
                        continue;
                    }
                    if (dd < wq) {
                        const double u = dd / wq;
                        // (Out in the new country the bed is set to the stretch's depth by the nearest of it, filled where the land
                        // lay low: a riffle is as shallow as it's meant to be.)
                        if (st) {
                            if (dd < nearestD[id]) hgrid[id] = (float)(level - dep * (1 - u * u) - 0.05);
                        } else
                            hgrid[id] = (float)std::min((double)hgrid[id], level - dep * (1 - u * u) - 0.05);
                        if (rockBed) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
                        else surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        if (dd < nearestD[id]) {
                            nearestD[id] = (float)dd;
                            waterY[id] = (float)level;
                            // Faster where it runs downhill, slower spread over a crossing (out in the new country: as its stretch is).
                            const double fall = (lvl[s] - lvl[s + 1]) / L;
                            const double sp = st ? (sa->speed + (sb->speed - sa->speed) * t) * (1 - 0.6 * u * u)
                                                 : std::min(2.6, 1.8 * (0.6 + 40 * fall)) * (1 - 0.6 * u * u) * (dep < 0.6 ? 0.7 : 1.0);
                            flowX[id] = (float)(dx / L * sp);
                            flowZ[id] = (float)(dz / L * sp);
                        }
                        keep[id] = 1;
                    } else {
                        // Banks: down to just above the water, sandy gravel at the edge (rock in the canyon).
                        const double bank = level + 0.2 + (dd - wq) * bankSlope[s];
                        if (hgrid[id] > bank) hgrid[id] = (float)(hgrid[id] + (bank - hgrid[id]) * smoothstep(reach, wq, dd));
                        if (dd < wq + 2.5 && paint[id] == PAINT_NONE) {
                            if (rockBed) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
                            else surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                        }
                        if (dd < wq + 4) keep[id] = 1;
                    }
                }
        }
        };
        carve(pts, lvl, depth, bankSlope);
        // The old channel's spots, for World::rivers.
        auto record = [&](const char* name, const Poly& o, const std::vector<double>& ol, const std::vector<double>& od,
                          const std::vector<RiverStretch>* st) {
            River r;
            r.name = name;
            for (size_t k = 0; k < o.size(); k += 4) {
                RiverSpot sp;
                sp.p = V3(o[k].x, ol[k], o[k].z);
                sp.depth = (float)od[k];
                const RiverStretch q = st ? (*st)[k] : RiverStretch();
                sp.halfWidth = (float)(kRiverHalf * (st ? q.widthScale : 1.0));
                sp.speed = (float)(st ? q.speed : std::hypot(flowX[at(o[k].x, o[k].z)], flowZ[at(o[k].x, o[k].z)]));
                sp.cls = (uint8_t)(st ? q.cls : riverClassForDepth(od[k]));
                sp.bed = (uint8_t)(st ? q.bed : BED_GRAVEL);
                r.spots.push_back(sp);
            }
            rivers.push_back(std::move(r));
        };
        record("ELK RIVER", pts, lvl, depth, nullptr);
        // Out into the new country: down Bear Creek Canyon from the east edge to the old map, and from it out by the west edge.
        // Its level follows the valley floor (stepping down at the falls), meeting the old river's at its ends. Its stretches
        // (rivers.cpp): runs of 0.8 m (a snorkel's water) between riffles, fords, pools, and the chutes and plunge pools of the falls.
        for (int side = 0; side < 2; side++) {
            std::vector<V3> ctl;
            if (side == 0)
                for (double x = 3060; x >= 540; x -= 30) ctl.push_back(V3(x, 0, riverZ(x)));
            else  // (on the big map it ends in a lake on the seam with the Open Country: onemap.h)
                for (double x = -540; x >= (stitch ? kMouthLake.x + kMouthLakeR * 0.6 : -3060); x -= 30) ctl.push_back(V3(x, 0, riverZ(x)));
            const Poly o = smooth(ctl);
            const int m = (int)o.size();
            std::vector<double> ol(m), od(m), ob(m);
            std::vector<RiverStretch> os(m);
            for (int k = 0; k < m; k++) ol[k] = riverFloor(o[k].x) - 0.3;
            if (side == 0) {  // (falling all the way, and into the old river's level at its end)
                ol[m - 1] = std::max(ol[m - 1], lvl[0] + 0.002);
                for (int k = m - 2; k >= 0; k--) ol[k] = std::max(ol[k], ol[k + 1] + 0.002);
            } else {
                ol[0] = std::min(ol[0], lvl[n - 1] - 0.002);
                for (int k = 1; k < m; k++) ol[k] = std::min(ol[k], ol[k - 1] - 0.002);
            }
            for (int k = 0; k < m; k++) {
                os[k] = elkRiverAt(o[k].x);
                od[k] = os[k].depth, ob[k] = os[k].bankSlope;
            }
            carve(o, ol, od, ob, &os);
            record(side == 0 ? "ELK RIVER (BEAR CREEK CANYON)" : "ELK RIVER (WEST)", o, ol, od, &os);
            if (side == 0) eastRiver = o, eastLevel = ol, eastStretch = os;
            else westRiver = o, westLevel = ol, westStretch = os;
        }
        // The big map: Elk River's mouth, a lake on the seam with the Open Country (where it ran off the old map's edge).
        if (stitch && !westLevel.empty()) {
            const double level = westLevel.back() - 0.01;
            const V3 c = kMouthLake;
            const double reach = kMouthLakeR * 1.15 + 100;  // (its banks rise 0.35 a metre from the water, ~30 m to the land round it)
            for (int i = cellI(c.x - reach); i <= cellI(c.x + reach); i++)
                for (int j = cellI(c.z - reach); j <= cellI(c.z + reach); j++) {
                    const double x = X(i), z = X(j), d = std::hypot(x - c.x, z - c.z);
                    const double ang = std::atan2(z - c.z, x - c.x);  // (a shore that wanders, the same all the way round)
                    const double rr = kMouthLakeR * (0.85 + 0.3 * valueNoise(std::cos(ang) * 1.3 + 4.0, std::sin(ang) * 1.3 + 0.7));
                    const int id = i * hN + j;
                    const double q = d / rr, bed = d < rr ? level - 2.4 * (1 - q * q) - 0.15 : level + 0.15 + (d - rr) * 0.35;
                    if (hgrid[id] > bed) hgrid[id] = (float)bed;
                    if (hgrid[id] < level && d < rr + 6) {
                        // (Where the river runs in, its current and its bed go on into the lake.)
                        if (waterY[id] < -1e8f) flowX[id] = flowZ[id] = 0, surf[id] = SURF_MUD, paint[id] = PAINT_MUD;
                        waterY[id] = std::max((float)waterY[id], (float)level);
                    }
                    else if (d < rr + 4 && paint[id] == PAINT_NONE) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                    if (d < rr + 8) keep[id] = 1;
                }
        }
    }

    steps.next("bc.river beds and falls");
    // ---- Elk River out in the new country: scanned rocks along its edges and in its riffles, posts at its fords; its falls ------
    // (Their own random numbers, from a hash of where they are: the map's stream, rnd() below, stays as it was.)
    {
        auto h01 = [](uint32_t a, uint32_t b) {
            uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
            h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
            return (h & 0xFFFFFF) / double(0x1000000);
        };
        uint32_t rockSeed = 71000;
        const size_t rocksBefore = rocks.size();
        // A rock of about radius r, `above` m of it out of the ground. (Under 1.2 m, so it's one hull: no cutting at load.)
        auto bedRock = [&](const V3& c, double r, double above) {
            r = std::min(r, 1.1);
            addRock(c.x, c.z, V3(r, r * 0.62, r * 0.8), above, rockSeed, 0.8f + 0.25f * (float)h01(rockSeed, 7));
            rockSeed++;
        };
        auto bigRock = [&](const V3& c, double r, double above) {
            addRock(c.x, c.z, V3(r, r * 0.7, r * 0.85), above, rockSeed, 0.85f);
            rockSeed++;
        };
        auto post = [&](const V3& p) { fixtures.push_back({PROP_MARKER, V3(p.x, height(p.x, p.z), p.z), V3(), 0}); };
        for (int side = 0; side < 2; side++) {
            const Poly& o = side ? westRiver : eastRiver;
            const std::vector<RiverStretch>& os = side ? westStretch : eastStretch;
            const int m = (int)o.size();
            for (const double fx : elkFords()) {  // fords: clear gravel, a pair of posts on each bank
                if ((fx > 0) == (side == 1)) continue;
                int k = 1;
                for (int q = 1; q + 1 < m; q++)
                    if (std::fabs(o[q].x - fx) < std::fabs(o[k].x - fx)) k = q;
                const V3 d = norm(o[k + 1] - o[k - 1]), l(-d.z, 0, d.x);
                const double hw = kRiverHalf * os[k].widthScale;
                for (int sd = -1; sd <= 1; sd += 2)
                    for (int a = -1; a <= 1; a += 2) post(o[k] + l * (sd * (hw + 4.5)) + d * (a * 3.2));
            }
            for (int k = 2; k + 2 < m; k += 4) {
                const RiverStretch& q = os[k];
                if (std::fabs(o[k].x) < 620 || q.cls == RIVER_FALLS || q.cls == RIVER_FORD) continue;
                const V3 d = norm(o[k + 1] - o[k - 1]), l(-d.z, 0, d.x);
                const double hw = kRiverHalf * q.widthScale;
                const double p = q.cls == RIVER_RIFFLE ? 0.45 : q.cls == RIVER_CHUTE ? 0.6 : q.cls == RIVER_PLUNGE ? 0.5 : q.cls == RIVER_POOL ? 0.22 : 0.14;
                const double big = q.cls == RIVER_CHUTE || q.cls == RIVER_PLUNGE ? 1.6 : 1.0;
                for (int sd = -1; sd <= 1; sd += 2) {
                    const uint32_t key = (uint32_t)(k * 2 + (sd > 0)), salt = (uint32_t)side * 16;
                    if (h01(key, salt + 1) > p) continue;
                    const double u = 0.72 + 0.4 * h01(key, salt + 2), r = (0.35 + 0.5 * h01(key, salt + 3)) * big;
                    bedRock(o[k] + l * (sd * u * hw), r, r * 0.55);
                }
                // Riffles: stones in the bed, low enough to drive over at walking pace.
                if (q.cls == RIVER_RIFFLE && h01((uint32_t)k, (uint32_t)side * 16 + 5) < 0.5) {
                    const double u = 1.2 * h01((uint32_t)k, (uint32_t)side * 16 + 6) - 0.6;
                    bedRock(o[k] + l * (u * hw), 0.22 + 0.18 * h01((uint32_t)k, (uint32_t)side * 16 + 8), 0.16);
                }
            }
        }
        // The falls: where the valley floor steps (rivers.cpp), the river goes over a lip of bare rock framed by boulders into
        // a deep plunge pool, boulders round its rim.
        const Poly& o = eastRiver;
        const int m = (int)o.size();
        for (const FallsSite& f : elkFalls()) {
            int kl = -1, kb = -1;  // the last point above the lip, the first in the pool (the line runs downstream: x falling)
            for (int k = 0; k < m; k++) {
                if (o[k].x >= f.x) kl = k;
                if (kb < 0 && o[k].x < f.x - kFallsFace - 0.5) kb = k;
            }
            if (kl < 0 || kb < 0) continue;
            Falls fl;
            fl.name = f.name;
            fl.lip = V3(f.x, eastLevel[kl], riverZ(f.x));
            fl.yaw = M_PI;  // (west, over a cliff that runs north-south across the canyon)
            fl.height = eastLevel[kl] - eastLevel[kb];
            fl.width = 2 * kRiverHalf * eastStretch[kl].widthScale * 0.92;
            fl.throwOut = f.throwOut;
            fl.variant = f.variant;
            fl.seed = (uint32_t)(f.x * 7);
            fl.poolR = kRiverHalf * 1.8;
            fl.poolDepth = f.poolDepth;
            falls.push_back(fl);
            const V3 d = norm(o[std::min(m - 1, kl + 1)] - o[std::max(0, kl - 1)]), l(-d.z, 0, d.x);
            const double hw = 0.5 * fl.width;
            // The cliff and the gorge's walls below it: bare rock wherever the floor steps.
            const double zc = riverZ(f.x);
            for (int i = cellI(f.x - 330); i <= cellI(f.x + 2); i++)
                for (int j = cellI(zc - 110); j <= cellI(zc + 110); j++) {
                    const double x = X(i), z = X(j);
                    const double g = std::fabs(elkFallsRise(x + 1, z) - elkFallsRise(x - 1, z)) + std::fabs(elkFallsRise(x, z + 1) - elkFallsRise(x, z - 1));
                    const int id = i * hN + j;
                    if (g < 1.5 || waterY[id] > -1e8) continue;
                    surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK, keep[id] = 1;
                }
            for (int sd = -1; sd <= 1; sd += 2) {
                bigRock(V3(f.x + 3.0, 0, fl.lip.z) + l * (sd * (hw + 2.6)), 1.8 + 0.4 * h01((uint32_t)f.x, 20 + sd), 1.2);  // the lip's shoulders, on top
                bigRock(V3(f.x - kFallsFace - 1.5, 0, fl.lip.z) + l * (sd * (fl.poolR + 1.0)), 1.6, 1.2);                   // at the cliff's foot
            }
            for (int k = kb + 4; k < m && k < kb + (int)f.poolLen; k += 5)
                for (int sd = -1; sd <= 1; sd += 2)
                    if (h01((uint32_t)k, 30 + sd) < 0.7) {
                        const V3 dk = norm(o[std::min(m - 1, k + 1)] - o[k - 1]), lk(-dk.z, 0, dk.x);
                        const double r = 0.9 + 0.5 * h01((uint32_t)k, 33 + sd);
                        bedRock(o[k] + lk * (sd * (fl.poolR * (0.95 + 0.15 * h01((uint32_t)k, 36 + sd)))), r, r * 0.6);
                    }
        }
        riverRocks = rocks.size() - rocksBefore;
    }

    steps.next("bc.places ground");
    // ---- Places (places.h): level the ground their buildings stand on; the Lost Mine's way up through its dry falls --------------------
    {
        auto meanHeight = [&](const V3& c, double r) {
            double sum = 0;
            int n = 0;
            for (int i = cellI(c.x - r); i <= cellI(c.x + r); i += 2)
                for (int j = cellI(c.z - r); j <= cellI(c.z + r); j += 2)
                    if (std::hypot(X(i) - c.x, X(j) - c.z) <= r) sum += hgrid[i * hN + j], n++;
            return n ? sum / n : 0.0;
        };
        for (const MapPlace& pl : mapPlaces(MAP_BACKCOUNTRY)) {
            if (pl.pad <= 0 || pl.id == "BC.LOST_MINE" || std::max(std::fabs(pl.p.x), std::fabs(pl.p.z)) > half - 60) continue;
            const double y = meanHeight(pl.p, pl.pad * 0.6);
            const bool yard = pl.kind == PK_RANGER_STATION || pl.kind == PK_FUEL_STOP;
            levelDisc(pl.p, pl.pad, y, 4 + pl.pad * 0.5, yard || pl.kind == PK_LOOKOUT || pl.kind == PK_SUMMIT ? PAINT_GRAVEL : PAINT_TRAIL, yard || pl.kind == PK_LOOKOUT || pl.kind == PK_SUMMIT ? SURF_GRAVEL : SURF_DIRT);
            if (pl.kind == PK_RANGER_STATION) levelDisc(pl.p, 9.5, y, 3, PAINT_PAD, SURF_PAVED);
        }
        // The Lost Mine: a yard on the mesa's edge, reached by a spur that climbs the scarp from the wash.
        if (const MapPlace* lm = findPlace(MAP_BACKCOUNTRY, "BC.LOST_MINE")) {
            const double yardY = meanHeight(lm->p, 8);
            levelDisc(lm->p, lm->pad, yardY, 8, PAINT_GRAVEL, SURF_GRAVEL);
            // The pit: a bowl in the yard where the shaft's spoil was dug out.
            const V3 pit = lm->p + V3(-16, 0, -11);
            for (int i = cellI(pit.x - 8); i <= cellI(pit.x + 8); i++)
                for (int j = cellI(pit.z - 8); j <= cellI(pit.z + 8); j++) {
                    const double d = std::hypot(X(i) - pit.x, X(j) - pit.z);
                    if (d > 8) continue;
                    hgrid[i * hN + j] -= (float)(2.2 * smoothstep(8, 1.5, d));
                    if (d < 6) paint[i * hN + j] = PAINT_GRAVEL, surf[i * hN + j] = SURF_GRAVEL;
                }
            Poly spur = smooth(lostMineSpur());
            const double rise = 4 * 0.55;  // the dry falls: four rock steps
            std::vector<double> sy = groundProfile(spur, 4, 0.2, {{0, height(spur[0].x, spur[0].z)}, {(int)spur.size() - 1, yardY - rise}});
            // (The steps stand where the spur meets the scarp.)
            std::vector<std::pair<double, double>> fall;
            for (int k = 0; k < 4; k++) fall.push_back({(double)spur.size() - 40 + k * 7.0, 0.55});
            addSteps(spur, sy, fall, 2.4, V3(0.5, 0.26, 0.2));
            layTrail(spur, sy, 2.6, 9, rockTread, noTilt);
        }
    }

    steps.next("bc.paint the land");
    // ---- Paint the land: bare rock on the steep and the knobs, scree, snow in the north ----------------
    auto slopeAtCell = [&](int i, int j) {
        const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / (2 * kRes), gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / (2 * kRes);
        return std::hypot(gx, gz);
    };
    auto snowline = [&](double x, double z) { return 50 + 16 * (fbmNoise(x * 0.02 + 3.0, z * 0.02 - 8.0, 3) - 0.5); };
    // The regions' weights (onemap.h) every 4 m, read between: worked out per cell they were most of this pass's time.
    const int fn = stitch ? (int)std::ceil(2 * half / 4.0) + 1 : 0;
    std::vector<float> field((size_t)fn * fn * 3, 0.0f);
    if (stitch)
        parallelRows(fn, [&](int a) {
            for (int b = 0; b < fn; b++) {
                double w[3];
                regionWeights(-half + a * 4.0, -half + b * 4.0, w);
                for (int k = 0; k < 3; k++) field[((size_t)a * fn + b) * 3 + k] = (float)w[k];
            }
        });
    auto weightsAt = [&](double x, double z, double w[3]) {
        const double u = (x + half) / 4.0, v = (z + half) / 4.0;
        const int a = std::min(fn - 2, (int)u), b = std::min(fn - 2, (int)v);
        const double fu = u - a, fv = v - b;
        for (int k = 0; k < 3; k++) {
            auto F = [&](int p, int q) { return (double)field[((size_t)p * fn + q) * 3 + k]; };
            w[k] = F(a, b) * (1 - fu) * (1 - fv) + F(a + 1, b) * fu * (1 - fv) + F(a, b + 1) * (1 - fu) * fv + F(a + 1, b + 1) * fu * fv;
        }
    };
    parallelRows(hN - 2, [&](int i0) {  // (each cell paints only itself)
        const int i = i0 + 1;
        for (int j = 1; j < hN - 1; j++) {
            const int id = i * hN + j;
            const double x = X(i), z = X(j), h = hgrid[id];
            if (std::max(std::fabs(x), std::fabs(z)) > kInner) {  // the new country: by district
                if (paint[id] != PAINT_NONE || surf[id] != SURF_GRASS) continue;
                const double slope = slopeAtCell(i, j), nz = valueNoise(x * 0.04, z * 0.04);
                if (stitch && std::max(std::fabs(x), std::fabs(z)) > kSeamFrom) {
                    // A region's grown country (regionland.h): its own kind of ground, ragged where it meets the Backcountry's.
                    double w[3];
                    weightsAt(x, z, w);
                    const int k = w[0] >= w[1] && w[0] >= w[2] ? 0 : w[1] >= w[2] ? 1 : 2;
                    if (w[k] > 0.25 + 0.5 * valueNoise(x * 0.03 + 7.0, z * 0.03 - 2.0)) {
                        uint8_t pt = PAINT_NONE, sf = SURF_GRASS;
                        if (regionGround(mapRegions()[k], x, z, h, slope, pt, sf)) paint[id] = pt, surf[id] = sf;
                        continue;
                    }
                    if (std::max(std::fabs(x), std::fabs(z)) > kOldBackcountryHalf) {
                        // The big map's natural edge past the 6 km square (north, east): the high country's ground as below, from cheaper
                        // noise (a third of the map's cells are out here).
                        const double wob = valueNoise(x * 0.02 + 3.0, z * 0.02 - 8.0) - 0.5;
                        if (h > 228 + 16 * wob && slope < 0.8) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
                        else if (slope > 0.85 || (h > 195 && slope > 0.5)) surf[id] = SURF_ROCK;
                        else if (h > 140 && slope > 0.45 && nz > 0.55) paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
                        continue;
                    }
                }
                const Districts d = districtsAt(x, z);
                const double dry = d.s * d.e;  // the badlands
                if (h > 228 + 16 * (fbmNoise(x * 0.02 + 3.0, z * 0.02 - 8.0, 3) - 0.5) && slope < 0.8) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
                else if (slope > 0.85 || (h > 195 && slope > 0.5)) {
                    if (dry > 0.4) paint[id] = PAINT_REDROCK;
                    surf[id] = SURF_ROCK;
                } else if (h > 140 && slope > 0.45 && nz > 0.55) paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
                else if (dry > 0.45) {  // (sand in the low washes, hardpan the rest)
                    if (h < 26 + 3 * nz && slope < 0.2) paint[id] = PAINT_SAND, surf[id] = SURF_SAND;
                    else paint[id] = PAINT_DESERT, surf[id] = SURF_DIRT;
                } else if (lakeDist(x, z) < kLakeR + 25 && h < kLakeY + 1.5) paint[id] = PAINT_SAND, surf[id] = SURF_SAND;
                continue;
            }
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
    });

    // The Lakeshore's lake: water over its basin.
    for (int i = cellI(kLake.x - kLakeR - 120); i <= cellI(kLake.x + kLakeR + 120); i++)
        for (int j = cellI(kLake.z - kLakeR - 120); j <= cellI(kLake.z + kLakeR + 120); j++) {
            const int id = i * hN + j;
            if (hgrid[id] < kLakeY && lakeDist(X(i), X(j)) < kLakeR + 80) waterY[id] = (float)kLakeY, keep[id] = 1;
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

    steps.next("bc.water the creek");
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

    steps.next("bc.boulders ledges and");
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
            if (rnd() < 0.5) {
                const double side = rnd() < 0.5 ? -1 : 1;
                const V3 q = p + l * (side * between(3.3, 5.5));
                const double r = between(0.8, 2.0);
                boulder(q, r, r * between(0.7, 1.15), 0.95f);
            }
            // Pines and broadleaf along the rim.
            if (rnd() < 0.3) {
                const double side = rnd() < 0.5 ? -1 : 1;
                const V3 q = p + l * (side * between(8, 18));
                const int id2 = at(q.x, q.z);
                if (!keep[id2] && waterY[id2] < -1e8 && paint[id2] == PAINT_NONE) {
                    Tree t;
                    t.kind = rnd() < 0.7 ? 0 : 1;
                    t.base = V3(q.x, height(q.x, q.z) - 0.2, q.z);
                    t.height = t.kind == 0 ? 10 + rnd() * 8 : 8 + rnd() * 6;
                    t.radius = t.height * (t.kind == 0 ? 0.2 + rnd() * 0.06 : 0.26 + rnd() * 0.08);
                    t.tint = (float)(0.8 + rnd() * 0.4);
                    plant(t);
                }
            }
        }
        // The cut banks are bare rock.
        scanNear(creek, 9, [&](int id, double d, double k, double) {
            const int i = id / hN, j = id % hN;
            if (d < 2.6 || k < 20 || i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1 || paint[id] != PAINT_NONE) return;
            if (slopeAtCell(i, j) > 0.4) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
        });
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

    steps.next("bc.props signs cairns");
    // ---- Props: signs, cairns, the camps' cabins and tables, the mine, the tower, the cabin, the wreck ---------
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
    // SAWMILL CAMP: the old mill shed, a pile of logs, a map; FAR SIDE CAMP: tables, a fire ring and a map;
    // PASS HUT: a stone-chimney cabin.
    {
        const V3 t = kSawmill;
        cabinAt(t + V3(-4, 0, 17), -0.4);
        sign(t.x + 15, t.z + 8, M_PI / 2, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
        for (int k = 0; k < 9; k++) {  // a stack of logs
            const double row = k < 4 ? 0 : k < 7 ? 1 : 2, col = k < 4 ? k : k < 7 ? k - 4 + 0.5 : k - 7 + 1.0;
            V3 a(t.x - 14 + col * 0.62, 0, t.z - 15), b(a.x, 0, a.z - 3.4);
            a.y = height(a.x, a.z) + 0.3 + row * 0.5, b.y = a.y;
            logs.push_back({a, b, 0.3});
        }
        const V3 f = kFarSide;
        sign(f.x + 4, f.z - 15, M_PI / 2, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
        table(f.x - 17, f.z - 3, 0.3);
        table(f.x - 17, f.z + 5, -0.2);
        fireRing(f + V3(-22, 0, 1));
        cabinAt(passHutAt() + V3(-3, 0, 15), 0.5);
        table(passHutAt().x + 16, passHutAt().z + 5, 1.2);
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
        const V3 m = kMine + V3(-14.5, 0, -3);
        const double y = height(m.x, m.z);
        const V3 timber(0.30, 0.20, 0.12);
        const size_t portal = boxes.size();
        piece(V3(m.x, y + 1.1, m.z - 1.7), V3(0.2, 1.1, 0.2), 0, 0, 0, timber);
        piece(V3(m.x, y + 1.1, m.z + 1.7), V3(0.2, 1.1, 0.2), 0, 0, 0, timber);
        piece(V3(m.x, y + 2.35, m.z), V3(0.3, 0.2, 2.1), 0, 0, 0, timber);
        piece(V3(m.x - 0.5, y + 1.0, m.z), V3(0.1, 1.0, 1.55), 0, 0, 0, V3(0.01, 0.01, 0.01));  // the dark way in
        standIn(portal, "mine_portal", V3(m.x, y, m.z), 0);
        const size_t cart = boxes.size();
        piece(V3(m.x + 6, y + 0.25, m.z - 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);  // rails
        piece(V3(m.x + 6, y + 0.25, m.z + 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);
        piece(V3(m.x + 3.2, y + 0.65, m.z), V3(0.85, 0.35, 0.6), 0.1, 0, 0, V3(0.38, 0.17, 0.1));       // the ore cart
        standIn(cart, "ore_cart", V3(m.x + 3.2, y + 0.2, m.z), 0.0, {{"track", 1}});
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
        const size_t tower = boxes.size();
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
        standIn(tower, "fire_tower", V3(t.x, y0, t.z), 0);
    }
    // TRAPPER'S CABIN at the end of its spur, with a woodpile and a table by the fire ring.
    {
        const V3 c = kCabin;
        cabinAt(c + V3(8, 0, 6), 3.5);
        table(c.x - 2, c.z - 7, 0.4);
        fireRing(c + V3(-9, 0, -2));
        piece(V3(c.x + 8, height(c.x + 8, c.z - 4) + 0.5, c.z - 4), V3(1.4, 0.5, 0.5), 0, 0, 0, V3(0.4, 0.28, 0.16));
        standIn(boxes.size() - 1, "woodpile", V3(c.x + 8, height(c.x + 8, c.z - 4), c.z - 4), 0);
    }
    // THE WRECK: an old pickup on its side in the canyon, parts thrown about.
    {
        const V3 w = wreckT.back();
        const double y = height(w.x, w.z);
        const V3 rust(0.45, 0.22, 0.1), dark(0.22, 0.15, 0.1), steel(0.32, 0.3, 0.28);
        const size_t wreck = boxes.size();
        piece(V3(w.x, y + 0.9, w.z), V3(2.0, 0.08, 0.8), 0.5, 0, 1.45, rust);                       // the bed's side
        piece(V3(w.x + 1.2, y + 0.55, w.z + 0.9), V3(2.3, 0.1, 0.8), 0.5, 0, 0.1, dark);                   // the frame
        piece(V3(w.x - 2.0, y + 0.8, w.z - 1.2), V3(0.9, 0.7, 0.8), 0.5, 0.1, 1.2, rust);                 // the cab
        piece(V3(w.x - 3.0, y + 0.5, w.z - 0.6), V3(0.7, 0.4, 0.75), 0.4, 0.3, 0.2, steel);               // the engine
        for (int k = 0; k < 4; k++) piece(V3(w.x + (k % 2 ? 1.6 : -0.9), y + 0.4, w.z + (k < 2 ? 1.8 : 3.4)), V3(0.35, 0.35, 0.14), 0.5, 0, 1.4, V3(0.03, 0.03, 0.03));
        standIn(wreck, "wreck_pickup", V3(w.x, y, w.z), 0.5);
        for (int k = 0; k < 7; k++)
            piece(V3(w.x + between(-5, 5), y + 0.12, w.z + between(-5, 5)), V3(between(0.3, 0.9), 0.05, between(0.2, 0.6)), between(0, 6.28), between(-0.2, 0.2), between(-0.2, 0.2), k % 2 ? rust : steel);
        // a scorched snag beside it
        Tree t;
        t.base = V3(w.x + 4.5, y - 0.2, w.z - 3.5);
        t.height = 9, t.radius = 2, t.tint = 0.5f, t.kind = 2, t.design = 1;  // (burnt)
        plant(t);
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

    steps.next("bc.boulders scattered over");
    // ---- Boulders scattered over the hills ------------------------------------------------------------
    for (int attempt = 0; attempt < 40000 && rocks.size() - riverRocks < 900; attempt++) {
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

    steps.next("bc.trees broadleaf along");
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
        plant(t);
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
        const double u = between(0, 1);
        const int habitat = hgrid[id] > 45 ? HAB_UPLAND : forest > 0.5 ? HAB_FOREST : HAB_MEADOW;
        bushes.push_back(growBush(V3(x, hgrid[id], z), habitat, forest > 0.38, u, (float)between(0.75, 1.2)));
    }
    // More of the small plants on the forest floor, from hashes of the spot (nothing after moves).
    const size_t shrubs = bushes.size();
    for (int i = 0; i < 60000 && bushes.size() < shrubs + shrubs / 3; i++) {
        const double x = (plantRoll(i, 7, 41) * 2 - 1) * 515, z = (plantRoll(i, 11, 42) * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || surf[id] != SURF_GRASS || hgrid[id] > 70) continue;
        const double forest = fbmNoise(x * 0.010 + 7.0, z * 0.010 - 3.0, 3);
        if (plantRoll(x, z, 43) > 0.15 + 0.6 * smoothstep(0.3, 0.55, forest)) continue;
        const int habitat = hgrid[id] > 45 ? HAB_UPLAND : forest > 0.5 ? HAB_FOREST : HAB_MEADOW;
        bushes.push_back(growBush(V3(x, hgrid[id], z), habitat, forest > 0.38, plantRoll(x, z, 44), (float)(0.75 + plantRoll(x, z, 45) * 0.45)));
    }

    steps.next("bc.the new country's growth");
    // ---- The new country: trees, rocks, logs and bushes, 64 m cell by cell (each cell its own random numbers, from where it is:
    // nothing in one cell moves when another changes), by district ----------------------------------------------------------
    {
        constexpr double C = 64;
        const int cn = (int)std::ceil(2 * half / C);
        // (Each cell's random numbers come from its place counted from the 6 km map's corner, as they were before the map grew to 8 km;
        // beyond that square, out on the big map's edge mountains and in the regions, nothing grows from here.)
        const int c0 = (int)std::lround((half - kOldBackcountryHalf) / C), cn0 = (int)std::ceil(2 * kOldBackcountryHalf / C);
        std::vector<std::vector<int>> nearTrees((size_t)cn * cn);
        int bigRocks = 0;
        for (int ci = 0; ci < cn; ci++)
            for (int cj = 0; cj < cn; cj++) {
                const double x0 = -half + ci * C, z0 = -half + cj * C, cx = x0 + C / 2, cz = z0 + C / 2;
                if (std::max(std::fabs(cx), std::fabs(cz)) < kInner + 40) continue;  // (the old map has its own)
                if (std::max(std::fabs(cx), std::fabs(cz)) > kOldBackcountryHalf) continue;
                const int si = ci - c0, sj = cj - c0;
                uint32_t r32 = (uint32_t)(si * 73856093) ^ (uint32_t)(sj * 19349663) ^ 0x51ed2701u;
                r32 = r32 * 2654435761u + 0x9e3779b9u;
                auto rr = [&]() {
                    r32 ^= r32 << 13, r32 ^= r32 >> 17, r32 ^= r32 << 5;
                    return (r32 & 0xFFFFFF) / double(0x1000000);
                };
                const Districts d = districtsAt(cx, cz);
                const double rowMid = std::max(0.0, 1 - d.n - d.s), colMid = std::max(0.0, 1 - d.e - d.w), dry = d.s * d.e;
                const double grove = smoothstep(0.38, 0.6, fbmNoise(cx * 0.004 + 7.0, cz * 0.004 - 3.0, 3));
                const double perCell = d.n * (d.w * 38 + colMid * 14 + d.e * 6) + d.s * (d.w * 10 + colMid * 18 + d.e * 0.6) +
                                       rowMid * (d.w * 3 + colMid * 16 + d.e * 14);
                const int wantTrees = (int)std::lround(perCell * (0.12 + 0.88 * grove));
                const double burnt = d.n * colMid * smoothstep(0.62, 0.72, fbmNoise(cx * 0.003 - 4.0, cz * 0.003 + 6.0, 2));
                auto open = [&](double x, double z, int& id, int& i, int& j) {
                    if (std::max(std::fabs(x), std::fabs(z)) < kInner + 10) return false;
                    i = cellI(x), j = cellI(z), id = i * hN + j;
                    if (i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1) return false;
                    return !keep[id] && waterY[id] < -1e8;
                };
                // Trees.
                int planted = 0;
                for (int k = 0; k < wantTrees * 2 && planted < wantTrees; k++) {
                    const double x = x0 + C * rr(), z = z0 + C * rr();
                    int id, i, j;
                    if (!open(x, z, id, i, j) || paint[id] != PAINT_NONE || surf[id] != SURF_GRASS) continue;
                    const double h = hgrid[id];
                    if (h > 205 + 12 * (valueNoise(x * 0.02, z * 0.02) - 0.5) || slopeAtCell(i, j) > 0.75) continue;
                    bool spaced = true;
                    for (int a = std::max(0, ci - 1); a <= std::min(cn - 1, ci + 1) && spaced; a++)
                        for (int b = std::max(0, cj - 1); b <= std::min(cn - 1, cj + 1) && spaced; b++)
                            for (int ti : nearTrees[(size_t)a * cn + b])
                                if (sq(trees[ti].base.x - x) + sq(trees[ti].base.z - z) < 3.5 * 3.5) {
                                    spaced = false;
                                    break;
                                }
                    if (!spaced) continue;
                    Tree t;
                    const double q = rr(), dv = std::fabs(z - riverZ(x));
                    const bool lowWet = (dv < 160 && h < 40) || lakeDist(x, z) < kLakeR + 200;
                    if (q < 0.05 + 0.5 * burnt) t.kind = 2;
                    else if ((lowWet && q < 0.75) || (d.w * rowMid > 0.5 && q < 0.5) || (dry > 0.4 && q < 0.6)) t.kind = 1;
                    else t.kind = 0;
                    t.base = V3(x, h - 0.2, z);
                    t.height = t.kind == 0 ? 9 + rr() * 10 : t.kind == 1 ? 7 + rr() * 7 : 6 + rr() * 6;
                    t.height *= 1 - 0.3 * smoothstep(120, 200, h);
                    t.radius = t.height * (t.kind == 0 ? 0.2 + rr() * 0.06 : 0.26 + rr() * 0.08);
                    t.tint = (float)(0.8 + rr() * 0.4);
                    nearTrees[(size_t)ci * cn + cj].push_back((int)trees.size());
                    plant(t);
                    planted++;
                }
                // Rocks: more on the steep and up high, and in the badlands; now and then a big outcrop.
                const int rockTries = 2 + (int)std::lround(3 * (d.n * colMid + dry));
                for (int k = 0; k < rockTries; k++) {
                    const double x = x0 + C * rr(), z = z0 + C * rr();
                    int id, i, j;
                    if (!open(x, z, id, i, j)) continue;
                    const double slope = slopeAtCell(i, j), h = hgrid[id];
                    if (rr() > 0.08 + 0.8 * smoothstep(0.2, 0.7, slope) + 0.25 * smoothstep(60, 200, h) + 0.3 * dry) continue;
                    const bool big = slope > 0.5 && rr() < 0.12 && bigRocks < 400;
                    const double rad = big ? 1.6 + 2.0 * rr() : 0.35 + rr() * rr() * 0.75, flat = 0.5 + 0.4 * rr();
                    bigRocks += big;
                    addRock(x, z, V3(rad, rad * flat, rad * (0.7 + 0.3 * rr())), rad * flat * (0.5 + 0.5 * rr()), 1000000u + (uint32_t)(si * cn0 + sj) * 8u + (uint32_t)k,
                            (float)(0.85 + 0.25 * rr()));
                }
                // A fallen log in the woods.
                if (grove > 0.5 && rr() < 0.5 * grove * (1 - dry)) {
                    const double x = x0 + C * rr(), z = z0 + C * rr();
                    int id, i, j;
                    const double r = 0.18 + 0.18 * rr(), L = 4 + 6 * rr(), a = 6.2832 * rr();
                    V3 p0(x - std::cos(a) * L / 2, 0, z - std::sin(a) * L / 2), p1(x + std::cos(a) * L / 2, 0, z + std::sin(a) * L / 2);
                    int i0, j0, id0, i1, j1, id1;
                    if (open(x, z, id, i, j) && paint[id] == PAINT_NONE && open(p0.x, p0.z, id0, i0, j0) && open(p1.x, p1.z, id1, i1, j1)) {
                        p0.y = height(p0.x, p0.z) + r * 0.8, p1.y = height(p1.x, p1.z) + r * 0.8;
                        logs.push_back({p0, p1, r});
                    }
                }
                // Bushes: what grows in each district.
                const int bushTries = (int)std::lround(12 * (0.3 + 0.7 * std::max(grove, dry)));
                for (int k = 0; k < bushTries; k++) {
                    const double x = x0 + C * rr(), z = z0 + C * rr();
                    int id, i, j;
                    if (!open(x, z, id, i, j)) continue;
                    const bool desert = paint[id] == PAINT_DESERT || (paint[id] == PAINT_SAND && dry > 0.4);
                    if ((paint[id] != PAINT_NONE && !desert) || (surf[id] != SURF_GRASS && !desert) || hgrid[id] > 210) continue;
                    const double wetDist = lakeDist(x, z) - kLakeR;
                    const int habitat = desert || dry > 0.45 ? HAB_DESERT : wetDist < 60 ? HAB_WETLAND : d.e * rowMid > 0.5 ? HAB_CANYON
                                        : hgrid[id] > 120 ? HAB_UPLAND : grove > 0.5 ? HAB_FOREST : HAB_MEADOW;
                    bushes.push_back(growBush(V3(x, hgrid[id], z), habitat, grove > 0.35, rr(), (float)(0.75 + 0.45 * rr())));
                }
            }
    }

    if (stitch) {
        steps.next("bc.regions growth");
        growRegionCountry(keep);  // (the regions' grown country and the big map's natural edge: onemapbuild.cpp)
    }
    steps.next("bc.places dress");
    // ---- Places (places.h): the ranger station, fuel stops, cabins and camps, the lost mine, the lookouts' cairns -------------------------
    {
        const V3 timber(0.30, 0.20, 0.12), iron(0.30, 0.26, 0.22), stone(0.42, 0.40, 0.36), brown(0.4, 0.3, 0.2);
        auto grounded = [&](V3 q) {
            q.y = height(q.x, q.z);
            return q;
        };
        // A spot `ahead` metres in front of and `left` of o, facing yaw (the game's yaw: front (cos, 0, -sin), left (sin, 0, cos)).
        auto rel = [&](const V3& o, double yaw, double ahead, double left) {
            return grounded(V3(o.x + std::cos(yaw) * ahead + std::sin(yaw) * left, 0, o.z - std::sin(yaw) * ahead + std::cos(yaw) * left));
        };
        using Set = std::vector<std::pair<std::string, double>>;
        // A code-made building at `at` (its footprint a hidden box of `half` for the collider; the model is drawn over it).
        auto build = [&](const char* id, const V3& at, double yaw, const V3& half, Set set = {}, int variant = -1) {
            const size_t from = boxes.size();
            addBox(V3(at.x, at.y + half.y, at.z), half, yaw, 0, 0, brown);
            standIn(from, id, at, yaw, std::move(set));
            if (variant >= 0 && !models.empty() && models.back().id == id) models.back().variant = variant;
        };
        // A model with nothing to bump into, or only a post.
        auto decor = [&](const char* id, const V3& at, double yaw, Set set = {}, int variant = -1) {
            placeModel(id, at, yaw, std::move(set));
            if (variant >= 0 && !models.empty() && models.back().id == id) models.back().variant = variant;
        };
        auto post = [&](const V3& q, double h, double r = 0.1) { poles.push_back({q.x, q.z, q.y - 0.3, q.y + h, r}); };
        // A wall of a hidden box, from local (x0, z0) to (x1, z1) of a frame, `h` tall.
        auto wall = [&](const V3& o, double yaw, double x0, double z0, double x1, double z1, double h) {
            const V3 a = rel(o, yaw, x0, z0), b = rel(o, yaw, x1, z1);
            const V3 c = (a + b) * 0.5;
            const double dx = x1 - x0, dz = z1 - z0;
            addBox(V3(c.x, o.y + h / 2, c.z), V3(std::max(std::fabs(dx), 0.25) / 2, h / 2, std::max(std::fabs(dz), 0.25) / 2), yaw, 0, 0, brown);
            boxes.back().hidden = true;
        };
        auto cabinOf = [&](const V3& at, double yaw, int variant, Set set) { build("cabin", at, yaw, V3(2.7, 1.9, 2.2), std::move(set), variant); };
        auto tent = [&](const V3& at, double yaw) { decor("tent", at, yaw); };
        auto fire = [&](const V3& at) {
            decor("campfire", at, 0);
            fireRing(at);
        };
        auto woodpile = [&](const V3& at, double yaw) { build("woodpile", at, yaw, V3(1.4, 0.5, 0.5)); };
        auto outhouse = [&](const V3& at, double yaw) { build("outhouse", at, yaw, V3(0.6, 1.1, 0.6)); };

        for (const MapPlace& pl : mapPlaces(MAP_BACKCOUNTRY)) {
            if (std::max(std::fabs(pl.p.x), std::fabs(pl.p.z)) > half - 60) continue;  // (the regions' places: not on the 6 km map)
            const V3 P = grounded(pl.p);
            const double yw = pl.yaw;
            if (pl.id == "BC.RANGER") {
                // The shed (repair, shop) in the middle, the ranger office beside it facing the drive, pumps across, a gate at the road.
                addStation(P, yw, "ELK RANGER STATION");
                build("ranger_station", rel(P, yw, 0, 18), yw + M_PI / 2, V3(5.2, 1.8, 3.4));
                build("fuel_pump", rel(P, yw, 7, -14), yw - M_PI / 2, V3(3.0, 0.6, 1.8), {}, 1);
                decor("fuel_pump", rel(P, yw, -9, -14), yw - M_PI / 2, {}, 0);
                woodpile(rel(P, yw, -10, 24), yw);
                outhouse(rel(P, yw, 12, 25), yw + M_PI / 2);
                table(rel(P, yw, -4, 22).x, rel(P, yw, -4, 22).z, yw);
                sign(rel(P, yw, 16, 8).x, rel(P, yw, 16, 8).z, yw, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
                const V3 g = rel(P, yw, -24, 0);
                decor("gate", g, yw + M_PI / 2, {{"open", 1}});
                post(rel(g, yw + M_PI / 2, 0, 2.7), 1.4), post(rel(g, yw + M_PI / 2, 0, -2.7), 1.4);
            } else if (pl.kind == PK_FUEL_STOP && pl.id.rfind("BC.", 0) == 0) {  // (the regions' outposts: below)
                // A pump island under a canopy (posts at its corners), an old upright pump and a drum rack, a cairn.
                const V3 I = rel(P, yw, 0, 7);  // (the forecourt, where you drive up, is P)
                build("fuel_pump", I, yw, V3(2.6, 0.5, 0.6), {}, 1);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) post(rel(I, yw, a * 2.7, b * 1.4), 3.7, 0.1);
                decor("fuel_pump", rel(P, yw, 3, -7), yw + 0.3, {}, 0);
                build("fuel_pump", rel(P, yw, -6, 8), yw + 0.5, V3(0.7, 0.5, 0.5), {}, 2);
                cairn(rel(P, yw, -7, -6));
                const V3 g = rel(P, yw, -20, 0);
                decor("gate", g, yw + M_PI / 2, {{"open", 1}});
                post(rel(g, yw + M_PI / 2, 0, 2.7), 1.4), post(rel(g, yw + M_PI / 2, 0, -2.7), 1.4);
            } else if (pl.id == "BC.HERMIT_CABIN") {
                // A log cabin, the garage beside it (the truck inside), a woodpile, the outhouse, a fire ring, a gate on the drive.
                cabinOf(rel(P, yw, -7, 0), yw, 0, {});
                woodpile(rel(P, yw, -1, 4.6), yw);
                outhouse(rel(P, yw, -9, -7), yw + M_PI);
                fire(rel(P, yw, 7, -5));
                table(rel(P, yw, 6, 4).x, rel(P, yw, 6, 4).z, yw);
                if (const MapPlace* t = findPlace(MAP_BACKCOUNTRY, "BC.HERMIT_TRUCK")) {
                    const V3 T = grounded(t->p);
                    const double ty = t->yaw;
                    // The garage: 7.3 x 4.2 outside, a 3.2 x 2.5 m opening in the front, a roof; it stands open.
                    decor("garage", T, ty, {{"open", 1}});
                    auto lbox = [&](double cx, double cz, double hx, double hz, double y0, double y1) {
                        const V3 c = rel(T, ty, cx, cz);
                        addBox(V3(c.x, T.y + (y0 + y1) / 2, c.z), V3(hx, (y1 - y0) / 2, hz), ty, 0, 0, brown);
                        boxes.back().hidden = true;
                    };
                    lbox(-3.55, 0, 0.1, 2.1, 0, 3.0);      // the back
                    lbox(0, -2.0, 3.65, 0.1, 0, 3.0);      // the sides
                    lbox(0, 2.0, 3.65, 0.1, 0, 3.0);
                    lbox(3.55, -1.85, 0.1, 0.25, 0, 3.0);  // the front, beside the opening and over it
                    lbox(3.55, 1.85, 0.1, 0.25, 0, 3.0);
                    lbox(3.55, 0, 0.1, 1.6, 2.5, 3.0);
                    lbox(0, 0, 4.0, 2.3, 3.0, 3.3);        // the roof
                    const V3 g = rel(T, ty, 14, 0);
                    decor("gate", g, ty + M_PI / 2, {{"open", 0.6}});
                    post(rel(g, ty + M_PI / 2, 0, 2.7), 1.4), post(rel(g, ty + M_PI / 2, 0, -2.7), 1.4);
                }
            } else if (pl.id == "BC.SURVEYOR_CABIN") {
                cabinOf(rel(P, yw, -7, 0), yw, 1, {{"porch", 1}});
                woodpile(rel(P, yw, -1, 4.6), yw);
                cairn(rel(P, yw, 8, -6));
                table(rel(P, yw, 6, 5).x, rel(P, yw, 6, 5).z, yw);
            } else if (pl.id == "BC.FISHING_SHACK") {
                cabinOf(rel(P, yw, -7, 0), yw, 1, {{"roof", 3}, {"porch", 1}});
                fire(rel(P, yw, 7, 4));
                table(rel(P, yw, 5, -5).x, rel(P, yw, 5, -5).z, yw);
                outhouse(rel(P, yw, -7, 6), yw + M_PI);
            } else if (pl.id == "BC.LOGGERS_CAMP") {
                cabinOf(rel(P, yw, -7, 0), yw, 1, {});
                for (int k = 0; k < 12; k++) {  // a deck of logs
                    const double row = k < 5 ? 0 : k < 9 ? 1 : 2, col = k < 5 ? k : k < 9 ? k - 5 + 0.5 : k - 9 + 1.0;
                    const V3 a = rel(P, yw, -6, 8 + col * 0.62);
                    V3 b = rel(P, yw, -9.4, 8 + col * 0.62);
                    V3 a2 = a, b2 = b;
                    a2.y = a.y + 0.3 + row * 0.5, b2.y = a2.y;
                    logs.push_back({a2, b2, 0.3});
                }
                fire(rel(P, yw, 6, -6));
                tent(rel(P, yw, 3, -10), yw + 0.4);
                table(rel(P, yw, 7, 2).x, rel(P, yw, 7, 2).z, yw);
            } else if (pl.id == "BC.BEAR_CREEK_CAMP") {
                tent(rel(P, yw, 0, -3), yw + 0.3);
                tent(rel(P, yw, 1, 4), yw - 0.5);
                fire(rel(P, yw, 6, 0));
                table(rel(P, yw, 8, 5).x, rel(P, yw, 8, 5).z, yw);
                woodpile(rel(P, yw, -4, 9), yw);
                post(rel(P, yw, -9, -9), 2.2), post(rel(P, yw, -9, 9), 2.2);
            } else if (pl.id == "BC.HIGH_LAKES_CAMP") {
                cabinOf(rel(P, yw, -7, 0), yw, 2, {});
                tent(rel(P, yw, 7, 7), yw + 0.4);
                fire(rel(P, yw, 8, -2));
                woodpile(rel(P, yw, -1, 4.6), yw);
            } else if (pl.id == "BC.HOMESTEAD") {
                // Ruins: a stone foundation, a chimney still standing, fallen beams.
                for (int k = 0; k < 26; k++) {
                    const double t = k / 26.0 * 4;
                    const int side = (int)t;
                    const double u = (t - side) * 2 - 1;
                    const double lx = side == 0 ? -4 + 8 * (u * 0.5 + 0.5) : side == 2 ? 4 - 8 * (u * 0.5 + 0.5) : (side == 1 ? 4 : -4);
                    const double lz = side == 1 ? -3 + 6 * (u * 0.5 + 0.5) : side == 3 ? 3 - 6 * (u * 0.5 + 0.5) : (side == 0 ? -3 : 3);
                    const V3 q = rel(P, yw, lx, lz);
                    addRock(q.x, q.z, V3(0.4 + 0.15 * (k % 3), 0.25, 0.4), 0.22, 7000 + k, 0.9f);
                }
                const V3 ch = rel(P, yw, -4, 0);
                piece(V3(ch.x, ch.y + 1.7, ch.z), V3(0.55, 1.7, 0.9), yw, 0, 0, stone);
                piece(V3(ch.x, ch.y + 3.5, ch.z), V3(0.4, 0.15, 0.7), yw, 0, 0, stone);
                for (int k = 0; k < 4; k++) {
                    const V3 q = rel(P, yw, -1 + k * 1.9, (k % 2 ? 1.5 : -1.2));
                    piece(V3(q.x, q.y + 0.18, q.z), V3(1.6, 0.1, 0.12), yw + 0.4 * (k - 1.5), 0.05, 0.1 * k, V3(0.2, 0.14, 0.1), false);
                }
                piece(rel(P, yw, 5.5, -6) + V3(0, 0.35, 0), V3(0.45, 0.35, 0.3), yw, 0, 0, V3(0.4, 0.17, 0.1));  // a rusted stove
            } else if (pl.id == "BC.COPPER_VISTA" || pl.id == "BC.RIM_OVERLOOK") {
                cairn(rel(P, yw, 3, 3));
                table(rel(P, yw, -3, -3).x, rel(P, yw, -3, -3).z, yw + M_PI / 2);
                sign(rel(P, yw, -5, 4).x, rel(P, yw, -5, 4).z, yw + M_PI, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
                if (pl.id == "BC.RIM_OVERLOOK") {  // a rail along the drop
                    const V3 a = rel(P, yw, 6, -5), b = rel(P, yw, 6, 5);
                    fence(a.x, a.z, b.x, b.z);
                }
            } else if (pl.kind == PK_SUMMIT) {
                cairn(rel(P, yw, 1.2, 0));
                cairn(rel(P, yw, -1.5, 1.5));
            } else if (pl.id == "BC.LOST_MINE") {
                // The yard on the mesa: the headframe over the shaft, a tent and a fire, tailings, the ore cart on its track, a gate at the top of the spur.
                const double Y = grounded(pl.p).y;
                const V3 H = rel(P, yw, 0, 0);
                build("mine_headframe", rel(H, yw, -7.5, 0), yw, V3(1.9, 1.8, 2.0), {{"rust", 0.8}}, 0);
                if (!models.empty() && models.back().id == "mine_headframe") models.back().p = H;  // (the model's origin is the shaft mouth)
                const V3 cart = rel(P, yw, 7, 2.5);
                piece(V3(cart.x, Y + 0.65, cart.z), V3(0.85, 0.35, 0.6), yw, 0, 0, V3(0.38, 0.17, 0.1));
                decor("ore_cart", V3(cart.x, Y + 0.2, cart.z), yw, {{"track", 1}});
                piece(V3(cart.x + 2, Y + 0.25, cart.z - 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);
                piece(V3(cart.x + 2, Y + 0.25, cart.z + 0.7), V3(4.5, 0.025, 0.05), 0, 0, 0, V3(0.35, 0.3, 0.28), false);
                tent(rel(P, yw, 8, -9), yw + 0.3);
                fire(rel(P, yw, 12, -4));
                table(rel(P, yw, 6, -10).x, rel(P, yw, 6, -10).z, yw);
                for (int k = 0; k < 10; k++) {
                    const double a = k * 0.8;
                    const V3 q = rel(P, yw, -9 + 4 * std::cos(a), 9 + 3 * std::sin(a));
                    addRock(q.x, q.z, V3(0.5 + 0.2 * (k % 3), 0.3, 0.5), 0.3, 7100 + k, 1.1f);
                }
                const V3 g = rel(P, yw, -27, 0);  // (the head of the spur, west of the yard)
                decor("gate", V3(2285, height(2285, -1745), -1745), yw + M_PI / 2, {{"open", 0.85}});
                (void)g;
                post(V3(2285, height(2285, -1745), -1745 + 2.7), 1.4), post(V3(2285, height(2285, -1745), -1745 - 2.7), 1.4);
            }
        }
        // The regions' places (docs/plans/regions2.md): their outposts' repair bays, cabins and camps, fire lookouts, and the lookouts
        // out on a ledge of rock (a slab reaching out over the drop from the lookout's yard).
        for (const MapPlace& pl : mapPlaces(MAP_BACKCOUNTRY)) {
            if (pl.id.rfind("BC.", 0) == 0 || !stitch) continue;
            const V3 P = grounded(pl.p);
            const double yw = pl.yaw;
            int variant = pl.id[0] == 'M' ? 2 : pl.id[0] == 'F' ? 1 : 0;
            if (pl.kind == PK_FUEL_STOP) {
                // An outpost: the repair bay in the middle of its yard (as the ranger station's), the pump island under its canopy
                // beside it, an old upright pump and a drum rack, a cairn, a map board and a gate on the way in.
                if (pl.services & SVC_REPAIR) addStation(P, yw, pl.name.c_str());
                const V3 I = rel(P, yw, 0, 15);
                build("fuel_pump", I, yw, V3(2.6, 0.5, 0.6), {}, 1);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) post(rel(I, yw, a * 2.7, b * 1.4), 3.7, 0.1);
                decor("fuel_pump", rel(P, yw, 6, 18), yw + 0.3, {}, 0);
                build("fuel_pump", rel(P, yw, -6, 17), yw + 0.5, V3(0.7, 0.5, 0.5), {}, 2);
                cairn(rel(P, yw, -10, -13));
                const V3 g = rel(P, yw, -22, 0);
                decor("gate", g, yw + M_PI / 2, {{"open", 1}});
                post(rel(g, yw + M_PI / 2, 0, 2.7), 1.4), post(rel(g, yw + M_PI / 2, 0, -2.7), 1.4);
                sign(rel(P, yw, -12, -6).x, rel(P, yw, -12, -6).z, yw, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
            } else if (pl.kind == PK_CABIN) {
                cabinOf(rel(P, yw, -7, 0), yw, variant, {});
                woodpile(rel(P, yw, -1, 4.6), yw);
                outhouse(rel(P, yw, -9, -7), yw + M_PI);
                fire(rel(P, yw, 7, -5));
                table(rel(P, yw, 6, 4).x, rel(P, yw, 6, 4).z, yw);
            } else if (pl.kind == PK_OLD_CAMP) {
                tent(rel(P, yw, 0, -3), yw + 0.3);
                tent(rel(P, yw, 1, 4), yw - 0.5);
                fire(rel(P, yw, 6, 0));
                table(rel(P, yw, 8, 5).x, rel(P, yw, 8, 5).z, yw);
                woodpile(rel(P, yw, -4, 9), yw);
            } else if (pl.kind == PK_LOOKOUT && pl.id.find("FIRE_TOWER") != std::string::npos) {
                // A fire lookout: four legs, a platform and a cab (the Backcountry's tower, drawn as its model).
                const double y0 = P.y;
                const V3 wood(0.36, 0.30, 0.22), tin(0.45, 0.46, 0.46);
                const size_t tower = boxes.size();
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) piece(V3(P.x + a * 1.75, y0 + 7.0, P.z + b * 1.75), V3(0.14, 7.2, 0.14), 0, -a * 0.075, -b * 0.075, wood);
                piece(V3(P.x, y0 + 14.1, P.z), V3(2.3, 0.12, 2.3), 0, 0, 0, wood);
                piece(V3(P.x, y0 + 15.2, P.z), V3(1.7, 1.0, 1.7), 0, 0, 0, V3(0.45, 0.2, 0.12));
                piece(V3(P.x, y0 + 16.35, P.z), V3(2.1, 0.1, 2.1), 0, 0, 0, tin);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) poles.push_back({P.x + a * 1.75, P.z + b * 1.75, y0 - 0.5, y0 + 14, 0.16});
                standIn(tower, "fire_tower", V3(P.x, y0, P.z), 0);
                cairn(rel(P, yw, 6, 4));
                table(rel(P, yw, -5, -5).x, rel(P, yw, -5, -5).z, yw);
            } else if (pl.kind == PK_LOOKOUT) {
                // Out over the steepest drop from the yard: a single great slab of rock, its top level with the yard, to drive out on.
                double best = 1e9, face = yw;
                for (int k = 0; k < 24; k++) {
                    const double a = k * M_PI / 12;
                    const double h = height(P.x + std::cos(a) * 26, P.z - std::sin(a) * 26);
                    if (h < best) best = h, face = a;
                }
                const V3 c = rel(P, face, pl.pad + 6.5, 0);
                RockPose pose;
                pose.yaw = face, pose.pitch = 0.04, pose.roll = 0;
                addRock(c.x, c.z, V3(9.0, 1.1, 5.5), P.y + 0.25 - height(c.x, c.z), 5100000u + (uint32_t)(int32_t)std::lround(pl.p.x * 7 + pl.p.z * 3), 1.0f, &pose);
                cairn(rel(P, face, -3, 4));
                table(rel(P, face, -4, -4).x, rel(P, face, -4, -4).z, face + M_PI / 2);
                sign(rel(P, face, -6, 5).x, rel(P, face, -6, 5).z, face + M_PI, V3(0.3, 0.22, 0.14), PROP_MAPBOARD);
            }
        }
    }

    steps.next("bc.stations start");
    // ---- Stations, start ----------------------------------------------------------------------
    for (const Pad& p : pads) addStation(V3(p.c.x, p.y, p.c.z), p.yaw, p.name);
    spawn = V3(kTrailhead.x - 8, pads[0].y, kTrailhead.z + 7);
    spawnYaw = 0;
    parking = {V3(kTrailhead.x - 8, 0, kTrailhead.z + 13), V3(kTrailhead.x - 8, 0, kTrailhead.z + 19), V3(kTrailhead.x - 16, 0, kTrailhead.z + 7)};
    parkingYaw = 0;
    if (stitch) {
        steps.next("bc.regions");
        stitchRegionContent(regionBuilds);
        for (RegionBuild* b : regionBuilds) delete b;
    }
}

V3 backcountryFeature(const char* name) {
    const std::string n = name;
    // A point on a trail nearest p.
    auto onTrail = [](const Poly& t, const V3& p) {
        V3 best = t[0];
        for (const V3& q : t)
            if (std::hypot(q.x - p.x, q.z - p.z) < std::hypot(best.x - p.x, best.z - p.z)) best = q;
        return best;
    };
    // Elk River out in the new country (rivers.cpp): its falls' lips, its fords and pools, mid-channel.
    if (double rx = 0; elkRiverSpot(n, rx)) return V3(rx, 0, riverZ(rx));
    if (n == "trailhead" || n == "start") return kTrailhead;
    if (n == "sawmill") return kSawmill;
    if (n == "farside") return kFarSide;
    if (n == "passhut") return passHutAt();
    if (n == "mine") return kMine;
    if (n == "firetower") return towerSite();
    if (n == "towerlook") return onTrail(towerTrail(), towerSite());  // the road beside the fire tower
    if (n == "cabin") return kCabin;
    if (n == "cabinend") return cabinTrail().back();
    if (n == "dunes") return kDunes;
    if (n == "dunering") return onTrail(washTrail(), kDunes + V3(-kDuneR, 0, 0));
    if (n == "washmid") return pointAlong(washTrail(), 110);
    if (n == "hilltop") return pointAlong(hillTrail(), 66);
    // The river's crossings: its centre, and a spot on the south bank on the way in.
    if (n == "ford") return V3(kFordX, 0, riverZ(kFordX));
    if (n == "deepcrossing") return V3(kDeepX, 0, riverZ(kDeepX));
    if (n == "fordbank") return onTrail(ridgeRoadTrail(), V3(kFordX + 4, 0, riverZ(kFordX) - 17));
    if (n == "deepbank") return onTrail(ridgeTrail(), V3(kDeepX - 8, 0, riverZ(kDeepX) - 16));
    if (n == "wreck") return pointAlong(wreckTrail(), (double)wreckTrail().size() - 12);  // on the spur, a little short of the wreck itself
    if (n == "wreckbody") return wreckTrail().back();
    if (n == "slab") return pointAlong(ridgeTrail(), kSlabS);
    if (n == "overlook") return pointAlong(shelfTrail(), kOverlookS);
    if (n == "snowcorner") return pointAlong(passTrail(), kSnowCornerS);
    if (n == "creekfoot") return pointAlong(creekTrail(), 36);
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
