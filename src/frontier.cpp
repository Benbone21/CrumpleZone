// The Frontier: everything in one map.
//
//  - The middle: a 1.4 km paved race circuit (kerbs; corners from a 20 m hairpin to 120 m
//    sweepers), its paddock with a wet skidpan and a repair shop. You start in the paddock.
//  - West: desert. Wind-rippled dunes of soft sand, cracked hardpan, a red-rock mesa with
//    a ledge staircase up its east face (MESA CLIMB), a butte, a slickrock bowl and a dry
//    boulder wash. THE WALL: twelve rock ledges up a boulder-lined gully in the hills off the
//    rally stage (the site is searched for when the map is built), to a lookout.
//  - North: a pine-covered range. A switchback trail climbs ~140 m to the summit through a
//    boulder garden, scree, snow above ~140 m and a frozen tarn under the top.
//  - East: Snake River runs down out of the range into Echo Lake (sandy beach); a second,
//    muddier river drains the lake west across the south and spreads into a mud delta at the
//    desert's edge. Both have current: deep water stalls you or floods the engine, and
//    the rally stage crosses them at four shallow gravel fords.
//  - A 4 km rally stage of gravel, sand, mud and dirt links it all: paddock, desert, foothill
//    forest, both rivers, the lakeshore and a mud bog in the south meadows.
//  - South-east: BLACKWATER BAYOU, a swamp below Echo Lake with a 740 m mud trail round it
//    (puddles, two wallows a 2WD car can't climb out of, a log corduroy) and MUD HILL.
//  - North-east: COLD CREEK, a glacial valley under snow with a snow road up it from the
//    stage (packed snow, black ice, the frozen creek), snow-laden pines, a frozen pond and
//    the lodge at its head.
//
// Coordinates: x east, z north, metres. Water lives on the height grid (World::waterY and
// the flow vectors), so the rivers can run downhill.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "world.h"

namespace {

// The race circuit, laid out as a real one is: straights joined by arcs of set radius. Each
// corner is where two straights would meet and the radius of the arc that rounds it off
// (y holds the radius). From the end of the paddock straight, anticlockwise from above:
// a medium right-hander off the straight, a fast sweeper, a tight left, a long right, the
// hairpin, a fast curve back south, two medium corners and the fast west bend onto the
// straight. Corner speeds for a car pulling 1.2 g: R 20 -> 55 km/h, 40 -> 78, 90 -> 117,
// 120 -> 135.
const std::vector<V3> kTrackCorners = {{160, 45, -118}, {190, 90, 30},   {110, 28, 95},   {140, 60, 175},  {20, 20, 190},
                                       {-5, 75, 60},    {-90, 38, 125},  {-205, 32, 160}, {-268, 120, 40}, {-258, 45, -118}};
constexpr double kTrackHalf = 6.5;
// Banking (degrees) in each corner above: the fast ones are banked so they can be taken
// faster (the outside of the turn raised), the slow and technical ones are flat.
const double kTrackBank[] = {0, 14, 0, 8, 0, 12, 0, 0, 16, 6};
// Points every ~2 m along it, starting halfway down the paddock straight; `apex` gets the
// middle of each corner's arc.
// `bank` gets each point's banking as a cross-slope: metres up per metre left of the line
// (a left-hander's outside is on the right, so it's negative there).
std::vector<V3> trackPoints(std::vector<V3>* apex = nullptr, std::vector<double>* bank = nullptr) {
    const std::vector<V3>& v = kTrackCorners;
    const int n = (int)v.size();
    auto flat = [](const V3& p) { return V3(p.x, 0, p.z); };
    struct Arc {
        V3 a, b, c;
        double r, a0, sweep;
    };
    std::vector<Arc> arcs(n);
    for (int i = 0; i < n; i++) {
        const V3 P = flat(v[(i - 1 + n) % n]), C = flat(v[i]), Q = flat(v[(i + 1) % n]);
        const V3 d1 = norm(C - P), d2 = norm(Q - C);
        const double turn = std::acos(clampd(dot(d1, d2), -1, 1)), sgn = d1.x * d2.z - d1.z * d2.x > 0 ? 1 : -1;
        // The radius asked for, unless the arc wouldn't fit between its neighbours.
        double r = v[i].y;
        const double room = 0.5 * std::min(len(C - P), len(Q - C));
        if (r * std::tan(turn / 2) > room) r = room / std::tan(turn / 2);
        const double T = r * std::tan(turn / 2);
        Arc& A = arcs[i];
        A.r = r, A.a = C - d1 * T, A.b = C + d2 * T;
        A.c = A.a + V3(-d1.z * sgn, 0, d1.x * sgn) * r;
        A.a0 = std::atan2(A.a.z - A.c.z, A.a.x - A.c.x);
        A.sweep = sgn * turn;
    }
    std::vector<V3> out;
    std::vector<double> slope;  // the banking each point asks for (smoothed in and out below)
    auto line = [&](const V3& from, const V3& to) {
        const int steps = std::max(1, (int)std::round(len(to - from) / 2.0));
        for (int k = 0; k < steps; k++) out.push_back(from + (to - from) * ((double)k / steps)), slope.push_back(0);
    };
    const V3 mid = (arcs[n - 1].b + arcs[0].a) * 0.5;
    if (apex) apex->clear();
    line(mid, arcs[0].a);
    for (int i = 0; i < n; i++) {
        const Arc& A = arcs[i];
        const int steps = std::max(2, (int)std::round(A.r * std::fabs(A.sweep) / 2.0));
        const double cross = -(A.sweep > 0 ? 1 : -1) * std::tan(kTrackBank[i % 10] * M_PI / 180);
        for (int k = 0; k < steps; k++) {
            const double ang = A.a0 + A.sweep * k / steps;
            out.push_back(A.c + V3(std::cos(ang), 0, std::sin(ang)) * A.r);
            slope.push_back(cross);
        }
        if (apex) {
            const double ang = A.a0 + A.sweep * 0.5;
            apex->push_back(A.c + V3(std::cos(ang), 0, std::sin(ang)) * A.r);
        }
        line(A.b, i + 1 < n ? arcs[i + 1].a : mid);
    }
    if (bank) {
        // Eased in and out over ~50 m either side of each corner (a cosine window), as a real
        // banked turn's transitions are. (Over 20 m, the road untwisted 16 degrees in a second
        // at 130 km/h coming off the fast banked corner: a car lifted a wheel and rocked.)
        const int m = (int)out.size(), win = 26;
        bank->assign(m, 0);
        for (int k = 0; k < m; k++) {
            double sum = 0, wsum = 0;
            for (int o = -win; o <= win; o++) {
                const double w = 0.5 + 0.5 * std::cos(M_PI * o / (win + 1));
                sum += w * slope[((k + o) % m + m) % m];
                wsum += w;
            }
            (*bank)[k] = sum / wsum;
        }
    }
    return out;
}
// The rally stage (closed): paddock -> desert -> foothills -> Snake River (twice) ->
// south across the Muddy (twice) -> back to the paddock.
const std::vector<V3> kRallyCtl = {{-110, 0, -165}, {-200, 0, -200}, {-300, 0, -228}, {-398, 0, -150}, {-330, 0, -20},  {-318, 0, 118},
                                   {-356, 0, 230},  {-250, 0, 296},  {-100, 0, 282},  {40, 0, 238},    {150, 0, 232},  {262, 0, 214},
                                   {360, 0, 196},   {372, 0, 118},   {378, 0, 22},    {306, 0, -42},   {222, 0, -100}, {178, 0, -190},
                                   {160, 0, -306},  {120, 0, -398},  {0, 0, -432},    {-80, 0, -398},  {-92, 0, -318}, {-112, 0, -240}};
constexpr double kRallyHalf = 3.6;
// The summit trail is laid out on the slope itself (see summitTrail): it leaves the rally
// stage here.
const V3 kTrailFoot(40, 0, 240);
constexpr double kTrailHalf = 2.8;
// Where the build puts these (it searches the land for them): kept here as data, exactly, so the features can be looked up
// without building the map (the built-in levels, the autopilot's places). The build lays them out again into the g* below and
// frontier/feature-table checks it lands on these; after a change to the land, update them from what it prints.
const V3 kWallFootAt(-0x1.3ap+8, 0, 0x1.2ap+8), kWallTopAt(-0x1.55p+8, 0, 0x1.58c3ef6815016p+8),
    kWallFromAt(-0x1.2ee32d1f75af1p+8, 0, 0x1.155cebe6879e3p+8), kSummitAt(0x1.594cf6ed0cf16p+3, 0, 0x1.976b638edadb9p+8),
    kGardenAt(0x1.2a2ba873be38fp+7, 0, 0x1.12d918940d245p+8);
V3 gWallFoot = kWallFootAt, gWallTop = kWallTopAt, gWallFrom = kWallFromAt, gSummit = kSummitAt, gGarden = kGardenAt;
// Rivers: from source to mouth.
const std::vector<V3> kSnakeCtl = {{330, 0, 500}, {300, 0, 384}, {262, 0, 292}, {272, 0, 202}, {242, 0, 112},
                                   {270, 0, 22},  {320, 0, -58}, {362, 0, -118}, {392, 0, -150}};
const std::vector<V3> kMuddyCtl = {{350, 0, -250}, {300, 0, -280}, {220, 0, -302}, {100, 0, -322}, {-20, 0, -312},
                                   {-140, 0, -340}, {-240, 0, -312}, {-300, 0, -290}};
const V3 kEchoLake(410, 0, -200);
constexpr double kEchoR = 72;
const V3 kTarn(-96, 0, 432);
const V3 kMesa(-440, 0, 70), kButte(-372, 0, -92), kBowl(-440, 0, -260);
constexpr double kMesaR = 45, kStairTread = 4.0, kStairRiser = 0.32;
constexpr int kMesaSteps = 14;
const V3 kBog(122, 0, -420), kDelta(-318, 0, -296);

// BLACKWATER BAYOU: a swamp in the south-east lowlands below Echo Lake (its backwater, at
// the lake's level), and a mud trail round it. The trail leaves the rally stage just south
// of the third ford, runs east along the swamp's north side (THE WALLOW on the way), round
// the far end and back west along the south side (THE SINK), then out through the old bog
// onto the stage again. MUD HILL is an island in the middle: its climb cuts across the loop.
const std::vector<V3> kBayouCtl = {{150.5, 0, -343.3}, {185, 0, -347}, {222, 0, -349}, {262, 0, -347}, {305, 0, -357}, {350, 0, -372},
                                   {395, 0, -383},     {432, 0, -399}, {457, 0, -426}, {455, 0, -458}, {420, 0, -476}, {370, 0, -475},
                                   {320, 0, -467},     {270, 0, -468}, {222, 0, -459}, {182, 0, -446}, {150, 0, -434}, {122, 0, -425},
                                   {93.7, 0, -412.6}};
const V3 kBayou(318, 0, -415);           // the swamp's middle (an ellipse round it, plus a fringe along the trail)
constexpr double kBayouRX = 148, kBayouRZ = 78;
constexpr double kBayouY = 2.0;          // the backwater's level
constexpr double kBayouHalf = 2.4;       // a one-truck trail
// The wallows: where on the trail, and how deep the water stands over the bottom.
const V3 kWallowAt[2] = {{246, 0, -348}, {298, 0, -467}};
constexpr double kWallowDepth[2] = {0.42, 0.5}, kWallowLen[2] = {14, 17};
// MUD HILL: a straight line over the island from the north leg (kHillFrom) to the south
// leg (kHillTo); the top is kHillTopAt of the way across.
const V3 kHillFrom(347, 0, -371), kHillTo(372, 0, -475);
constexpr double kHillH = 6.5, kHillTopAt = 0.4;

// COLD CREEK: a glacial valley cut into the range east of Snake River, under snow. The
// valley floor (kColdCtl, mouth to head) climbs from the stage's north-east corner to a
// cirque; the snow road (kSnowRoadCtl) winds up it to the lodge at the head, crossing the
// frozen creek on the way. Steep rock walls, snowfields and drifts, snow-laden pines, a
// frozen pond.
const std::vector<V3> kColdCtl = {{405, 0, 215}, {420, 0, 262}, {421, 0, 322}, {433, 0, 380}, {440, 0, 432}, {440, 0, 486}};
constexpr double kColdHalf = 40, kColdY0 = 13, kColdY1 = 33;  // floor half width; its height at the mouth and the head
const std::vector<V3> kSnowRoadCtl = {{363.8, 0, 192.0}, {380, 0, 205}, {398, 0, 226}, {414, 0, 256}, {406, 0, 292}, {400, 0, 328},
                                      {412, 0, 362},     {415, 0, 398}, {428, 0, 428}, {436, 0, 452}, {440, 0, 462}};
const V3 kLodge(440, 0, 471), kIcePond(452, 0, 352);
constexpr double kSnowHalf = 3.2, kIcePondR = 11;

// A polyline resampled every `step` metres (so profiles laid along it are smooth).
std::vector<V3> resample(const std::vector<V3>& pts, double step) {
    std::vector<V3> out{pts[0]};
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
// The point `s` metres along a polyline sampled every metre (clamped to its ends).
V3 pointAlong(const std::vector<V3>& pts, double s) {
    const int k = (int)clampd(std::floor(s), 0, (double)pts.size() - 2);
    return pts[k] + (pts[k + 1] - pts[k]) * clampd(s - k, 0, 1);
}
const std::vector<V3>& bayouTrail() {
    static const std::vector<V3> t = resample(splinePoints(kBayouCtl, false), 1.0);
    return t;
}
const std::vector<V3>& snowRoad() {
    static const std::vector<V3> t = resample(splinePoints(kSnowRoadCtl, false), 1.0);
    return t;
}

// Distance from p to a polyline (and the segment index, parameter and signed lateral).
struct Near {
    double d = 1e18, lat = 0, along = 0;
    int seg = 0;
    double t = 0;
};
Near nearest(const std::vector<V3>& pts, bool closed, double x, double z) {
    Near best;
    double along = 0;
    const int n = (int)pts.size();
    for (int s = 0; s < (closed ? n : n - 1); s++) {
        const V3& a = pts[s];
        const V3& b = pts[(s + 1) % n];
        const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, L = std::sqrt(L2);
        if (L2 < 1e-9) continue;
        const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
        const double cx = a.x + dx * t, cz = a.z + dz * t, d = std::hypot(x - cx, z - cz);
        if (d < best.d) {
            best.d = d, best.seg = s, best.t = t, best.along = along + L * t;
            best.lat = (dx * (z - a.z) - dz * (x - a.x)) / L;
        }
        along += L;
    }
    return best;
}

// How far along the bayou trail the point nearest to p is (m).
double bayouAlong(const V3& p) { return nearest(bayouTrail(), false, p.x, p.z).along; }
// The MUD HILL line, its ends snapped onto the trail.
V3 hillEnd(bool south) {
    const std::vector<V3>& t = bayouTrail();
    return t[(size_t)std::lround(bayouAlong(south ? kHillTo : kHillFrom))];
}
// The swamp: 1 well inside, 0 outside. An ellipse round its middle, and a fringe along the
// trail where it runs through (fading out west of x ~200: its first stretch off the stage, the way out by the bog).
double bayouMask(double x, double z) {
    const double n = 0.12 * (valueNoise(x * 0.03 + 4.0, z * 0.03 - 7.0) - 0.5);
    const double r = std::hypot((x - kBayou.x) / kBayouRX, (z - kBayou.z) / kBayouRZ) + n;
    double m = smoothstep(1.02, 0.84, r);
    if (m < 1 && x > 165 && x < 490 && z < -320 && z > -500) {
        const Near t = nearest(bayouTrail(), false, x, z);
        const V3& p = bayouTrail()[std::min(bayouTrail().size() - 1, (size_t)t.along)];
        m = std::max(m, smoothstep(24, 9, t.d + 8 * n) * smoothstep(172, 215, p.x));
    }
    return m;
}
// Cold Creek: how deep in the snow (0 outside, 1 in it). The valley and the mountains round
// it, north-east of the stage; a ragged snowline at the valley mouth.
double coldMask(double x, double z) {
    const double n = valueNoise(x * 0.05 - 3.0, z * 0.05 + 1.0) - 0.5;
    const double west = std::max(318.0, 312 + 0.3 * (z - 300));  // clear of Snake River, which comes down west of it
    return smoothstep(west - 12, west + 12, x + 22 * n) * smoothstep(228, 262, z + 26 * n);
}

double desertness(double x, double z) { return smoothstep(-235, -330, x + 25 * std::sin(z * 0.012)); }
double mountainness(double z) { return smoothstep(150, 430, z); }

// The summit trail, laid out like a trail crew would: traverse the slope at a steady ~14%
// grade, hairpin back up at each end of the traverse, until the top.
std::vector<V3> summitTrail(const World& w) {
    std::vector<V3> pts{kTrailFoot};
    V3 pos = kTrailFoot;
    int dir = 1;  // +1 traversing east, -1 west
    const double xMin = -170, xMax = 170, grade = 0.14, step = 2.0;
    auto h = [&](const V3& p) { return w.frontierHeight(p.x, p.z); };
    for (int n = 0; n < 1200 && h(pos) < 150 && pos.z < 480; n++) {
        if ((dir > 0 && pos.x > xMax) || (dir < 0 && pos.x < xMin)) {
            // Hairpin: a half circle (10 m radius) round toward the uphill side.
            const V3 c = pos + V3(0, 0, 10);
            const double a0 = std::atan2(pos.z - c.z, pos.x - c.x);
            for (int k = 1; k <= 15; k++) {
                const double a = a0 + dir * M_PI * k / 15.0;
                pos = c + V3(std::cos(a), 0, std::sin(a)) * 10.0;
                pts.push_back(pos);
            }
            dir = -dir;
            continue;
        }
        // The heading (within 75 degrees of the traverse, never downhill-north) whose grade
        // is closest to the target.
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

}  // namespace

double World::frontierHeight(double x, double z) const {
    // The plains: gentle rolls.
    double h = 4 + 2.5 * std::sin(x * 0.013 + 1.0) * std::cos(z * 0.011) + 1.2 * std::sin((x + z) * 0.03);
    // Foothills, then the range to the north.
    const double m = mountainness(z);
    h += smoothstep(110, 260, z) * 14 * fbmNoise(x * 0.012 + 3.0, z * 0.012, 3);
    h += m * m * 150 * (0.72 + 0.5 * ridgedNoise(x * 0.0045 + 1.7, z * 0.0045 - 0.6, 5));
    // Desert to the west: lower, dunes on it.
    const double dw = desertness(x, z);
    if (dw > 0) {
        const double ridge = std::pow(1 - std::fabs(std::sin(0.034 * (0.8 * x + 0.6 * z) + 1.4 * std::sin(0.009 * z + 0.004 * x))), 2.2);
        const double dunes = 7.5 * ridge * (0.55 + 0.45 * valueNoise(x * 0.01, z * 0.01)) + 1.5 * valueNoise(x * 0.05, z * 0.05);
        h = h * (1 - 0.6 * dw) + dw * dunes * (1 - m);
        // The mesa and the butte: flat tops on steep red walls.
        auto table = [&](const V3& c, double r, double top) {
            const double d = std::hypot(x - c.x, z - c.z) + 4 * (valueNoise(x * 0.06, z * 0.06) - 0.5);
            return top * smoothstep(r + 7, r, d);
        };
        h += dw * (table(kMesa, kMesaR, 10) + table(kButte, 20, 12));
    }
    // Rim mountains keep you on the map.
    const double ex = std::max(0.0, std::fabs(x) - 520), ez = std::max(0.0, std::fabs(z) - 520);
    h += (ex * ex + ez * ez) * 0.02;
    return h;
}

void World::buildFrontier() {
    map = MAP_FRONTIER;
    crashProps = false;
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -half + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    hgrid.resize(N);
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) hgrid[i * hN + j] = (float)frontierHeight(X(i), X(j));
    paint.assign(N, PAINT_NONE);
    surf.assign(N, SURF_GRASS);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    waterY.reset(hN, -1e9f);
    flowX.reset(hN, 0);
    flowZ.reset(hN, 0);
    auto at = [&](double x, double z) -> int { return cellI(x) * hN + cellI(z); };
    // Everything that mustn't get trees, rocks or bushes on it.
    std::vector<uint8_t> keep(N, 0);

    // ---- Paths: the circuit, the rally stage, the summit trail ----------------------------
    std::vector<double> trackBank;
    const std::vector<V3> track = trackPoints(nullptr, &trackBank), rally = splinePoints(kRallyCtl, true), trail = summitTrail(*this);
    gSummit = trail.back();
    struct PathDef {
        const std::vector<V3>* pts;
        bool closed;
        double half, blend;
        uint8_t paint;
        int window;      // smoothing of its height profile (samples ~2 m apart)
        double maxGrade; // steepest it may run (cut and fill where the land's steeper)
        const std::vector<double>* bank = nullptr;  // its banking at each point (cross-slope), if any
    };
    const PathDef paths[] = {{&rally, true, kRallyHalf, 10, PAINT_GRAVEL, 12, 0.3},
                             {&trail, false, kTrailHalf, 9, PAINT_TRAIL, 4, 0.2},
                             {&track, true, kTrackHalf, 14, PAINT_TRACK, 25, 0.12, &trackBank}};
    std::vector<float> edge(N, 1e9f), pathY(N, 0);
    std::vector<uint8_t> pathPaint(N, 0);
    for (const PathDef& p : paths) {
        // Height along it: the terrain, smoothed (and the circuit kept fairly flat).
        std::vector<V3> pts = *p.pts;
        const int n = (int)pts.size();
        std::vector<double> raw(n);
        for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
        for (int k = 0; k < n; k++) {
            double sum = 0;
            int cnt = 0;
            for (int o = -p.window; o <= p.window; o++) {
                int q = k + o;
                if (p.closed) q = ((q % n) + n) % n;
                else if (q < 0 || q >= n) continue;
                sum += raw[q];
                cnt++;
            }
            pts[k].y = sum / cnt;
        }
        // Grade cap: back and forth until no stretch is steeper than it may be.
        for (int pass = 0; pass < 4; pass++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < n; q++) {
                    const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                    if (prev < 0 || prev >= n) continue;
                    const double run = std::hypot(pts[k].x - pts[prev].x, pts[k].z - pts[prev].z), lim = p.maxGrade * run;
                    pts[k].y = clampd(pts[k].y, pts[prev].y - lim, pts[prev].y + lim);
                }
        const double reach = p.half + p.blend;
        double along = 0;
        for (int s = 0; s < (p.closed ? n : n - 1); s++) {
            const V3& a = pts[s];
            const V3& b = pts[(s + 1) % n];
            const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
            if (L < 1e-6) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / (L * L), 0, 1);
                    const double dd = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    const int id = i * hN + j;
                    if (dd - p.half >= edge[id]) continue;
                    edge[id] = (float)(dd - p.half);
                    const double lat = (dx * (z - a.z) - dz * (x - a.x)) / L;
                    double y = a.y + (b.y - a.y) * t;
                    if (p.bank) {  // banked: the surface tilts across, the outside of the turn up (on into the verge)
                        const double cs = (*p.bank)[s] + ((*p.bank)[(s + 1) % n] - (*p.bank)[s]) * t;
                        y += cs * clampd(lat, -p.half - 4, p.half + 4);
                    }
                    pathY[id] = (float)y;
                    pathPaint[id] = p.paint;
                    roadLat[id] = (float)lat;
                    roadAlong[id] = (float)(along + L * t);
                }
            along += L;
        }
    }
    for (int id = 0; id < N; id++) {
        if (edge[id] > 14) continue;
        const double w = edge[id] < 1.0 ? 1.0 : smoothstep(14, 1.0, edge[id]);
        hgrid[id] = (float)(hgrid[id] + (pathY[id] - hgrid[id]) * w);
        if (edge[id] < 0.5) {
            paint[id] = pathPaint[id];
            surf[id] = pathPaint[id] == PAINT_TRACK ? SURF_PAVED : pathPaint[id] == PAINT_GRAVEL ? SURF_GRAVEL : SURF_DIRT;
            roadNear[id] = 1;
        }
        if (edge[id] < 4) keep[id] = 1;
    }

    // ---- Level pads: the paddock (pits, the shop), the outposts ----------------------------
    auto levelRect = [&](double x0, double x1, double z0, double z1, double y, double blend, uint8_t pnt, uint8_t sf) {
        for (int i = cellI(x0 - blend); i <= cellI(x1 + blend); i++)
            for (int j = cellI(z0 - blend); j <= cellI(z1 + blend); j++) {
                const double x = X(i), z = X(j);
                const double d = std::hypot(std::max({x0 - x, x - x1, 0.0}), std::max({z0 - z, z - z1, 0.0}));
                const int id = i * hN + j;
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(blend, 0, d));
                if (d < 0.5) paint[id] = pnt, surf[id] = sf, keep[id] = 1;
                if (d < blend) keep[id] = 1;
            }
    };
    // (Clear of the circuit: levelling across the straight would leave a ridge in it.)
    const double paddockY = height(-70, -118);
    levelRect(-150, 10, -162, -132, paddockY, 6, PAINT_PAD, SURF_PAVED);
    // The east end of the paddock: a wet skidpan (sprinklers on) to feel the grip go.
    for (int i = cellI(-30); i <= cellI(8); i++)
        for (int j = cellI(-160); j <= cellI(-136); j++) paint[i * hN + j] = PAINT_WET, surf[i * hN + j] = SURF_WET;
    auto pad = [&](const V3& c, double r) {
        levelRect(c.x - r, c.x + r, c.z - r, c.z + r, height(c.x, c.z), 10, PAINT_PAD, SURF_PAVED);
    };
    const V3 outpost(-292, 0, -50), lakeside(332, 0, 120), hut(92, 0, 214);
    pad(outpost, 9);
    pad(lakeside, 9);
    pad(hut, 9);

    // ---- Rock: staircases up walls (ledge boxes on a ramp carved into the slope) -------------
    // From `start` heading `yaw` (0 = east, 90 = north... the car convention: forward
    // (cos, -sin)), climbing `steps` ledges of `riser` over `tread` each, `width` wide, to
    // the height `top`. The ramp under the ledges takes up the rest; its sides meet the
    // ground around over `bank` metres (a short bank: steep rock walls, a gully).
    auto staircase = [&](const V3& start, double yaw, int steps, double tread, double riser, double width, double top, double bank) {
        const V3 f(std::cos(yaw), 0, -std::sin(yaw)), l(-f.z, 0, f.x);
        const double y0 = height(start.x, start.z), L = steps * tread;
        const double slope = std::max(0.0, (top - y0 - steps * riser) / L);
        const double reach = L + 12;
        for (int i = cellI(start.x - reach); i <= cellI(start.x + reach); i++)
            for (int j = cellI(start.z - reach); j <= cellI(start.z + reach); j++) {
                const V3 d(X(i) - start.x, 0, X(j) - start.z);
                const double s = dot(d, f), u = std::fabs(dot(d, l));
                if (s < -6 || s > L + 6 || u > width / 2 + bank) continue;
                const int id = i * hN + j;
                // The ground under the ledges rises along the line of their front edges, so
                // each ledge stands one riser above it at its lip and meets it at the back
                // (the boxes stay thin, and no step is more than a riser).
                const double ramp = y0 + (slope + riser / tread) * clampd(s, 0, L);
                const double w = smoothstep(width / 2 + bank, width / 2, u) * smoothstep(-6, 0, s) * smoothstep(L + 6, L, s);
                hgrid[id] = (float)(hgrid[id] + (ramp - hgrid[id]) * w);
                if (u < width / 2 + std::max(1.0, bank) && s > -1 && s < L + 1) paint[id] = PAINT_REDROCK, surf[id] = SURF_ROCK, keep[id] = 1;
            }
        const double pitch = std::atan(slope), hyp = std::sqrt(1 + slope * slope);
        for (int k = 0; k < steps; k++) {
            const double sc = (k + 0.5) * tread, thick = riser * 1.5 + 0.4;
            const V3 c = start + f * sc;
            const double yTop = y0 + slope * sc + riser * (k + 1);
            addBox(V3(c.x, yTop - thick / 2, c.z), V3(tread / 2 * hyp, thick / 2, width / 2), yaw, pitch, 0, V3(0.58, 0.34, 0.24));
            boxes.back().rock = true;
        }
        return y0 + slope * L + steps * riser;
    };
    // MESA CLIMB: fourteen ledges up the mesa's east face onto its top.
    {
        const double top = height(kMesa.x, kMesa.z);
        staircase(V3(kMesa.x + kMesaR + kMesaSteps * kStairTread, 0, kMesa.z), M_PI, kMesaSteps, kStairTread, kStairRiser, 10, top, 8);
    }
    // THE WALL: a spur off the rally stage straight up the foothills, twelve ledges to a
    // lookout. Put where the hillside's about 30%, so the ramp under the ledges is gentle
    // and the ledges do the climbing.
    double wallYaw = -M_PI / 2;
    V3 wallFrom = gWallFoot;  // where its approach leaves the rally stage
    {
        double bestScore = 1e9;
        for (double x = -330; x <= 330; x += 8)
            for (double z = 130; z <= 330; z += 8) {
                const V3 a(x, 0, z);
                for (int k = 0; k < 12; k++) {
                    const double yaw = k * M_PI / 6;
                    const V3 f(std::cos(yaw), 0, -std::sin(yaw));
                    // The approach leaves the stage just off it and comes into a run-up 12 m
                    // before the first ledge from more or less straight behind (no hairpin).
                    const V3 run = a - f * 12;
                    const Near r = nearest(rally, true, run.x, run.z);
                    if (r.d < 8 || r.d > 30) continue;
                    const V3 &q0 = rally[r.seg], &q1 = rally[(r.seg + 1) % rally.size()];
                    const V3 rp = q0 + (q1 - q0) * r.t;
                    if (dot(norm(V3(run.x - rp.x, 0, run.z - rp.z)), f) < 0.4) continue;
                    const V3 b = a + f * 45;
                    const double g = (height(b.x, b.z) - height(a.x, a.z)) / 45;
                    if (g < 0.18 || g > 0.5) continue;
                    // Clear of the roads, trails, water and the desert's mesa.
                    bool clear = true;
                    for (double t = -8; t <= 52 && clear; t += 4) {
                        const V3 q = a + f * t;
                        const int id = at(q.x, q.z);
                        clear = edge[id] > 8 && waterY[id] < -1e8 && std::fabs(q.x) < 500 && std::fabs(q.z) < 500;
                    }
                    if (!clear) continue;
                    const double score = std::fabs(g - 0.3) + 0.004 * r.d;
                    if (score < bestScore) {
                        bestScore = score, gWallFoot = a, gWallTop = b, wallYaw = yaw;
                        wallFrom = rp;
                    }
                }
            }
    }
    {
        const int steps = 12;
        const double riser = kStairRiser;
        // The approach: a short track from the stage to a run-up lined up with the stairs,
        // then straight at the first ledge; no steeper than ~18% (the foot is cut down into
        // the hillside to suit).
        const V3 wf(std::cos(wallYaw), 0, -std::sin(wallYaw));
        const V3 runUp = gWallFoot - wf * 12;
        const double yFrom = height(wallFrom.x, wallFrom.z);
        const double L1 = len(runUp - wallFrom), L2 = 12;
        const double y0 = std::min(height(gWallFoot.x, gWallFoot.z), yFrom + 0.18 * (L1 + L2));
        const double yRun = yFrom + (y0 - yFrom) * L1 / (L1 + L2);
        auto track = [&](const V3& from, const V3& to, double ya, double yb) {
            const V3 d = to - from;
            const double L = std::max(1.0, len(d));
            for (int i = cellI(std::min(from.x, to.x) - 12); i <= cellI(std::max(from.x, to.x) + 12); i++)
                for (int j = cellI(std::min(from.z, to.z) - 12); j <= cellI(std::max(from.z, to.z) + 12); j++) {
                    const V3 q(X(i) - from.x, 0, X(j) - from.z);
                    const double t = clampd(dot(q, d) / (L * L), 0, 1);
                    const double dd = len(q - d * t);
                    if (dd > 12) continue;
                    const int id = i * hN + j;
                    const double y = ya + (yb - ya) * t;
                    hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(12, 4, dd));
                    if (dd < 3.2) paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT, roadNear[id] = 1, keep[id] = 1;
                }
        };
        track(wallFrom, runUp, yFrom, yRun);
        track(runUp, gWallFoot, yRun, y0);
        // ~20% overall (the ledges do half of it): cut into the hillside as a stair gully.
        const double top = y0 + steps * riser + 0.11 * steps * kStairTread;
        staircase(gWallFoot, wallYaw, steps, kStairTread, riser, 9, top, 2.5);
        // The lookout at the top: a flat rock shelf from the last ledge on (lined up with the
        // stairs, so it doesn't bury the top ones), blending into the hill past it.
        const double L = steps * kStairTread;
        const V3 look = gWallFoot + wf * (L + 6);
        {
            const V3 wl(-wf.z, 0, wf.x);
            for (int i = cellI(look.x - 16); i <= cellI(look.x + 16); i++)
                for (int j = cellI(look.z - 16); j <= cellI(look.z + 16); j++) {
                    const V3 d(X(i) - gWallFoot.x, 0, X(j) - gWallFoot.z);
                    const double sa = dot(d, wf), u = std::fabs(dot(d, wl));
                    if (sa < L) continue;
                    const double out = std::hypot(std::max(0.0, sa - (L + 12)), std::max(0.0, u - 7));
                    if (out > 5) continue;
                    const int id = i * hN + j;
                    hgrid[id] = (float)(hgrid[id] + (top - hgrid[id]) * smoothstep(5, 0, out));
                    if (out < 0.5) paint[id] = PAINT_REDROCK, surf[id] = SURF_ROCK;
                    keep[id] = 1;
                }
        }
        gWallTop = look;
        gWallFrom = wallFrom;
    }
    // ---- Desert: slickrock bowl, dry wash ----------------------------------------------------
    for (int i = cellI(kBowl.x - 36); i <= cellI(kBowl.x + 36); i++)
        for (int j = cellI(kBowl.z - 36); j <= cellI(kBowl.z + 36); j++) {
            const double d = std::hypot(X(i) - kBowl.x, X(j) - kBowl.z) / 34;
            if (d >= 1) continue;
            const int id = i * hN + j;
            hgrid[id] += (float)(-6 * (1 - d * d) * (1 - d * d) + 7 * smoothstep(0.55, 0.95, d) * (1 - smoothstep(0.95, 1.0, d)));
            paint[id] = PAINT_REDROCK, surf[id] = SURF_ROCK, keep[id] = 1;
        }

    // ---- Lakes ---------------------------------------------------------------------------
    addLake(kEchoLake.x, kEchoLake.z, kEchoR, 3.0);
    const double echoY = waters.back().y;
    // The frozen tarn under the summit: a flat sheet of ice.
    {
        const double y = height(kTarn.x, kTarn.z) - 0.5;
        for (int i = cellI(kTarn.x - 40); i <= cellI(kTarn.x + 40); i++)
            for (int j = cellI(kTarn.z - 40); j <= cellI(kTarn.z + 40); j++) {
                const double d = std::hypot(X(i) - kTarn.x, X(j) - kTarn.z);
                const int id = i * hN + j;
                const double w = smoothstep(36, 22, d);
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * w);
                if (d < 22 + 2 * valueNoise(X(i) * 0.2, X(j) * 0.2)) paint[id] = PAINT_ICE, surf[id] = SURF_ICE, keep[id] = 1;
            }
    }

    // ---- Rivers: carved channels, water on the grid with its flow -----------------------------
    auto river = [&](const std::vector<V3>& ctl, double half, double deep, double speed, double mouthY, bool muddy) {
        std::vector<V3> pts = splinePoints(ctl, false);
        const int n = (int)pts.size();
        // Water level: the land along the bank, smoothed, always falling toward the mouth.
        std::vector<double> lvl(n);
        for (int k = 0; k < n; k++) {
            double lo = 1e9;
            for (int o = -1; o <= 1; o += 2) {
                const V3 d = norm(pts[std::min(n - 1, k + 1)] - pts[std::max(0, k - 1)]);
                lo = std::min(lo, height(pts[k].x - d.z * o * (half + 3), pts[k].z + d.x * o * (half + 3)));
            }
            lvl[k] = lo - 0.6;
        }
        for (int pass = 0; pass < 6; pass++)
            for (int k = 1; k + 1 < n; k++) lvl[k] = 0.25 * lvl[k - 1] + 0.5 * lvl[k] + 0.25 * lvl[k + 1];
        for (int k = 1; k < n; k++) lvl[k] = std::min(lvl[k], lvl[k - 1] - 0.004);
        if (mouthY > -1e8) {
            for (int k = 0; k < n; k++) lvl[k] = std::max(lvl[k], mouthY);
            // Ease into the lake's level over the last 60 m.
            for (int k = 0; k < n; k++) {
                const double toEnd = (n - 1 - k) * 2.0;
                if (toEnd < 60) lvl[k] = lvl[k] + (mouthY - lvl[k]) * smoothstep(60, 0, toEnd);
            }
        }
        // Fords: where the rally stage crosses, it's shallow with a firm gravel bed.
        std::vector<double> depth(n, deep);
        for (int k = 0; k < n; k++) {
            const Near r = nearest(rally, true, pts[k].x, pts[k].z);
            if (r.d < 34) depth[k] = deep + (0.35 - deep) * smoothstep(34, 16, r.d);
        }
        const double reach = half + 10;
        std::vector<float> nearestD(N, 1e9f);  // each cell takes its water from the nearest stretch
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
                    const double wq = half * (0.85 + 0.3 * valueNoise(x * 0.05, z * 0.05));
                    if (dd < wq) {
                        const double u = dd / wq;
                        hgrid[id] = (float)std::min((double)hgrid[id], level - dep * (1 - u * u) - 0.05);
                        const bool ford = dep < 0.6;
                        surf[id] = ford ? SURF_GRAVEL : muddy ? SURF_MUD : SURF_GRAVEL;
                        paint[id] = ford ? PAINT_GRAVEL : muddy ? PAINT_MUD : PAINT_GRAVEL;
                        if (dd < nearestD[id]) {
                            nearestD[id] = (float)dd;
                            waterY[id] = (float)level;
                            // Faster where it runs downhill, slower spread over a ford.
                            const double fall = (lvl[s] - lvl[s + 1]) / L;
                            const double sp = std::min(3.0, speed * (0.6 + 12 * fall)) * (1 - 0.6 * u * u) * (ford ? 0.7 : 1.0);
                            flowX[id] = (float)(dx / L * sp);
                            flowZ[id] = (float)(dz / L * sp);
                        }
                        keep[id] = 1;
                    } else {
                        // Banks: down to just above the water, sandy or muddy at the edge.
                        const double bank = level + 0.2 + (dd - wq) * 0.45;
                        if (hgrid[id] > bank) hgrid[id] = (float)(hgrid[id] + (bank - hgrid[id]) * smoothstep(reach, wq, dd));
                        if (dd < wq + 2.5 && paint[id] == PAINT_NONE) {
                            surf[id] = muddy ? SURF_MUD : SURF_GRAVEL;
                            paint[id] = muddy ? PAINT_MUD : PAINT_GRAVEL;
                        }
                        if (dd < wq + 4) keep[id] = 1;
                    }
                }
        }
        return lvl;
    };
    river(kSnakeCtl, 7.5, 1.3, 2.4, echoY, false);
    const std::vector<double> muddyLvl = river(kMuddyCtl, 6.5, 1.0, 0.9, -1e9, true);
    // The lake: its water on the grid too, and a sandy beach round it.
    for (int i = cellI(kEchoLake.x - kEchoR * 1.3); i <= cellI(kEchoLake.x + kEchoR * 1.3); i++)
        for (int j = cellI(kEchoLake.z - kEchoR * 1.3); j <= cellI(kEchoLake.z + kEchoR * 1.3); j++) {
            const int id = i * hN + j;
            if (std::hypot(X(i) - kEchoLake.x, X(j) - kEchoLake.z) > kEchoR * 1.25) continue;
            const double above = hgrid[id] - echoY;
            if (above < 0) waterY[id] = std::max((float)waterY[id], (float)echoY), keep[id] = 1;
            if (above > -0.8 && above < 1.2 && paint[id] == PAINT_NONE) paint[id] = PAINT_SAND, surf[id] = SURF_SAND, keep[id] = 1;
        }
    // The delta: where the Muddy spreads out and soaks into the desert, a wide mud flat
    // with a skin of water in the low spots.
    {
        const double y = muddyLvl.back();
        for (int i = cellI(kDelta.x - 50); i <= cellI(kDelta.x + 50); i++)
            for (int j = cellI(kDelta.z - 50); j <= cellI(kDelta.z + 50); j++) {
                const double d = std::hypot(X(i) - kDelta.x, X(j) - kDelta.z);
                if (d > 48) continue;
                const int id = i * hN + j;
                const double target = y - 0.15 + 0.5 * valueNoise(X(i) * 0.08, X(j) * 0.08);
                hgrid[id] = (float)(hgrid[id] + (target - hgrid[id]) * smoothstep(48, 30, d));
                if (d < 40) {
                    surf[id] = SURF_MUD, paint[id] = PAINT_MUD, keep[id] = 1;
                    if (hgrid[id] < y) waterY[id] = (float)y;
                }
            }
    }
    // Mud bog in the south meadows: a hollow of sticky mud with hummocks.
    for (int i = cellI(kBog.x - 34); i <= cellI(kBog.x + 34); i++)
        for (int j = cellI(kBog.z - 34); j <= cellI(kBog.z + 34); j++) {
            const double d = std::hypot((X(i) - kBog.x) / 1.4, X(j) - kBog.z);
            if (d > 24) continue;
            const int id = i * hN + j;
            hgrid[id] -= (float)(0.9 * smoothstep(24, 12, d) - 0.45 * (valueNoise(X(i) * 0.25, X(j) * 0.25) - 0.5));
            if (d < 21) surf[id] = SURF_MUD, paint[id] = PAINT_MUD, keep[id] = 1;
        }

    // ---- Trails laid to a set profile: Cold Creek road, the bayou's mud trail ----------------
    // A profile along a polyline (sampled every metre): the land under it, smoothed over
    // `window` metres either side, then no steeper than `maxGrade` (cut and fill). The first
    // stretch keeps to the land, so it meets the road it leaves.
    auto groundProfile = [&](const std::vector<V3>& pts, int window, double maxGrade) {
        const int n = (int)pts.size();
        std::vector<double> raw(n), y(n);
        for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
        for (int k = 0; k < n; k++) {
            double sum = 0;
            int cnt = 0;
            for (int q = std::max(0, k - window); q <= std::min(n - 1, k + window); q++) sum += raw[q], cnt++;
            y[k] = sum / cnt;
        }
        for (int k = 0; k < std::min(n, 12); k++) y[k] = raw[k] + (y[k] - raw[k]) * k / 12.0;
        for (int pass = 0; pass < 4; pass++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < n; q++) {
                    const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                    const double run = std::hypot(pts[k].x - pts[prev].x, pts[k].z - pts[prev].z);
                    y[k] = clampd(y[k], y[prev] - maxGrade * run, y[prev] + maxGrade * run);
                }
        return y;
    };
    // Lays a trail `half` wide along `pts` at the heights `y` (one per point): the tread is
    // set to them and painted by `paintTread(id)`, and the land within `blend` past its edge
    // eases into it. Where an earlier road is nearer (at a junction) that road is left alone.
    auto layTrail = [&](const std::vector<V3>& pts, const std::vector<double>& y, double half, double blend, auto paintTread) {
        const int n = (int)pts.size();
        const double reach = half + blend;
        std::vector<int> cells;
        std::vector<float> e(N, 1e9f), ty(N, 0), lat(N, 0), al(N, 0);
        double along = 0;
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
            if (L < 1e-6) continue;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / (L * L), 0, 1);
                    const double dd = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t)) - half;
                    if (dd > blend) continue;
                    const int id = i * hN + j;
                    if (e[id] > 1e8f) cells.push_back(id);
                    if (dd >= e[id]) continue;
                    e[id] = (float)dd, ty[id] = (float)(y[s] + (y[s + 1] - y[s]) * t);
                    lat[id] = (float)((dx * (z - a.z) - dz * (x - a.x)) / L), al[id] = (float)(along + L * t);
                }
            along += L;
        }
        for (int id : cells) {
            if (edge[id] < e[id]) continue;
            const double w = e[id] < 1.0 ? 1.0 : smoothstep(blend, 1.0, e[id]);
            hgrid[id] = (float)(hgrid[id] + (ty[id] - hgrid[id]) * w);
            edge[id] = e[id];
            roadLat[id] = lat[id], roadAlong[id] = al[id];  // (out past the edge too: the wheel tracks drawn on it stay straight)
            if (e[id] < 0.5) paintTread(id), roadNear[id] = 1;
            if (e[id] < 3) keep[id] = 1;
        }
    };

    // ---- Cold Creek: the valley, its frozen creek and pond, the snow road ----------------------
    const std::vector<V3> cold = resample(splinePoints(kColdCtl, false), 1.0);
    const double coldL = (double)cold.size() - 1;
    auto coldFloor = [&](double s) { return kColdY0 + (kColdY1 - kColdY0) * clampd(s / coldL, 0, 1); };
    {
        const V3 d0 = norm(cold[1] - cold[0]);
        const std::vector<V3> snake = splinePoints(kSnakeCtl, false);  // (the walls leave its banks alone)
        for (int i = cellI(330); i <= cellI(520); i++)
            for (int j = cellI(170); j <= cellI(530); j++) {
                const double x = X(i), z = X(j);
                const Near v = nearest(cold, false, x, z);
                if (v.d > 160) continue;
                const int id = i * hN + j;
                // How far past the floor's edge (a ragged one), and the floor's height there.
                const double e = v.d - kColdHalf - 14 * (fbmNoise(x * 0.02 + 9.0, z * 0.02 - 4.0, 3) - 0.5);
                const double F = coldFloor(v.along), drift = 2.2;  // snow drifted up against the walls
                if (e > 0) {
                    // The walls: a U, steepening to ~1:1 within 25 m of the floor, ribbed.
                    const double rise = e < 25 ? e * e / 50 : 12.5 + (e - 25);
                    const double wall = F + drift + rise * (0.75 + 0.55 * ridgedNoise(x * 0.025 + 2.0, z * 0.025, 3));
                    if (hgrid[id] > wall && nearest(snake, false, x, z).d > 24) hgrid[id] = (float)wall;
                } else {
                    // The floor: snowfields rolling in long drifts across the valley, banked up at
                    // the walls; it eases into the land at the mouth.
                    // (Long and low: in snow, ~10% is about all a truck can climb.)
                    const double rel = 1.0 * (valueNoise(x * 0.035 + 2.0, z * 0.06) - 0.5) + 1.4 * (fbmNoise(x * 0.012, z * 0.012, 2) - 0.5);
                    const double target = F + drift * smoothstep(-16, 0, e) + rel * smoothstep(0, -8, e);
                    const double s = v.along > 0.5 ? v.along : dot(V3(x, 0, z) - cold[0], d0);
                    hgrid[id] = (float)(hgrid[id] + (target - hgrid[id]) * smoothstep(0, 40, s));
                }
            }
    }
    // The frozen creek: a sunken ribbon of ice snaking down the east side of the floor from
    // under the head, past the pond, then across to the west side and out of the mouth (the
    // road crosses it there).
    std::vector<V3> creek;
    for (double s = 25; s <= coldL - 50; s += 1) {
        const V3 p = pointAlong(cold, s), q = pointAlong(cold, s + 1);
        const V3 d = norm(V3(q.x - p.x, 0, q.z - p.z)), l(-d.z, 0, d.x);
        const double off = -17 + 26 * smoothstep(125, 55, s) + 5 * std::sin(s * 0.06) + 2.5 * std::sin(s * 0.17);
        creek.push_back(p + l * off);
        creek.back().y = height(creek.back().x, creek.back().z);  // its bed follows the floor
    }
    auto iceCreek = [&](bool sink) {
        for (size_t k = 0; k < creek.size(); k++) {
            const V3& p = creek[k];
            for (int i = cellI(p.x - 4); i <= cellI(p.x + 4); i++)
                for (int j = cellI(p.z - 4); j <= cellI(p.z + 4); j++) {
                    const double d = std::hypot(X(i) - p.x, X(j) - p.z), w = 1.5 + 0.6 * valueNoise(X(i) * 0.3, X(j) * 0.3);
                    const int id = i * hN + j;
                    // Sunk a little into the snow, with banks gentle enough to drive out of (it's ice).
                    if (sink && d < 4) hgrid[id] = (float)std::min((double)hgrid[id], p.y - 0.15 * smoothstep(4, 1.2, d));
                    if (d < w) paint[id] = PAINT_ICE, surf[id] = SURF_ICE, keep[id] = 1;
                }
        }
    };
    iceCreek(true);
    // The frozen pond on the floor's east side.
    {
        const double y = height(kIcePond.x, kIcePond.z) - 0.4;
        for (int i = cellI(kIcePond.x - kIcePondR - 10); i <= cellI(kIcePond.x + kIcePondR + 10); i++)
            for (int j = cellI(kIcePond.z - kIcePondR - 10); j <= cellI(kIcePond.z + kIcePondR + 10); j++) {
                const double d = std::hypot(X(i) - kIcePond.x, (X(j) - kIcePond.z) * 1.3) + 2.5 * valueNoise(X(i) * 0.15, X(j) * 0.15);
                const int id = i * hN + j;
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(kIcePondR + 8, kIcePondR, d));
                if (d < kIcePondR + 1.5) paint[id] = PAINT_ICE, surf[id] = SURF_ICE, keep[id] = 1;
            }
    }
    // The snow road: off the stage's north-east corner, up the valley floor in long bends to
    // the lodge at the head, ~8% at the most.
    {
        const std::vector<V3>& road = snowRoad();
        layTrail(road, groundProfile(road, 10, 0.085), kSnowHalf, 9, [&](int id) { paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT; });
        // The lodge's yard at the head: packed snow, level.
        levelRect(kLodge.x - 9, kLodge.x + 22, kLodge.z - 8, kLodge.z + 8, height(road.back().x, road.back().z), 8, PAINT_SNOW, SURF_SNOW);
    }

    // ---- Blackwater Bayou: the swamp, its mud trail and MUD HILL --------------------------------
    // The swamp floor: hummocks and mud flats round the backwater's level, a third of it under
    // a hand's depth or two of still water.
    std::vector<float> swamp(N, 0);
    for (int i = cellI(158); i <= cellI(500); i++)
        for (int j = cellI(-505); j <= cellI(-315); j++) {
            const double x = X(i), z = X(j);
            const double m = bayouMask(x, z);
            if (m <= 0) continue;
            const int id = i * hN + j;
            swamp[id] = (float)m;
            double rel = 1.7 * (fbmNoise(x * 0.022 + 5.0, z * 0.022 - 2.0, 3) - 0.5) + 0.55 * (valueNoise(x * 0.12, z * 0.12) - 0.5) + 0.02;
            rel += 0.2 * std::tanh(rel / 0.03);  // the pools have banks (a cut edge, not a long wet slope)
            const double floorY = std::max(kBayouY - 0.6, kBayouY + rel);
            hgrid[id] = (float)(hgrid[id] + (floorY - hgrid[id]) * m);
            if (m > 0.05 && waterY[id] < -1e8) waterY[id] = (float)kBayouY, flowX[id] = flowZ[id] = 0;
            else if (m <= 0.05) hgrid[id] = std::max(hgrid[id], (float)kBayouY + 0.1f);  // its rim holds the water in
        }
    // MUD HILL: an island in the middle, a long whaleback across the loop. Its north face
    // averages 20% (~30% at the steepest), the south one 16%.
    const V3 hillA = hillEnd(false), hillB = hillEnd(true);
    const V3 hillDir = norm(V3(hillB.x - hillA.x, 0, hillB.z - hillA.z)), hillLeft(-hillDir.z, 0, hillDir.x);
    const double hillL = std::hypot(hillB.x - hillA.x, hillB.z - hillA.z), hillTop = kHillTopAt * hillL;
    auto hillRise = [&](double u) {
        const double up = 1.5 * kHillH / 0.3, down = 1.5 * kHillH / 0.24;  // a smoothstep's steepest is 1.5x its mean
        return smoothstep(hillTop - up - 3, hillTop - 3, u) * smoothstep(hillTop + down + 3, hillTop + 3, u);
    };
    for (int i = cellI(std::min(hillA.x, hillB.x) - 40); i <= cellI(std::max(hillA.x, hillB.x) + 40); i++)
        for (int j = cellI(std::min(hillA.z, hillB.z) - 10); j <= cellI(std::max(hillA.z, hillB.z) + 10); j++) {
            const V3 d(X(i) - hillA.x, 0, X(j) - hillA.z);
            const double u = dot(d, hillDir), v = std::fabs(dot(d, hillLeft)) + 4 * (valueNoise(X(i) * 0.08, X(j) * 0.08) - 0.5);
            const double r = hillRise(u) * smoothstep(30, 6, v);
            if (r <= 0) continue;
            hgrid[i * hN + j] += (float)(kHillH * r);
        }
    // The trail: a churned trough at the swamp's level, mud waves along it, puddles of every
    // depth, two wallows a car can't climb out of, and a corduroy of logs over the first wet
    // stretch.
    const std::vector<V3>& bt = bayouTrail();
    const int btN = (int)bt.size();
    const double sWallow[2] = {bayouAlong(kWallowAt[0]), bayouAlong(kWallowAt[1])};
    const double sHill[2] = {bayouAlong(hillA), bayouAlong(hillB)};
    double sCord = 0;  // where the corduroy starts
    std::vector<double> by = groundProfile(bt, 8, 0.2);
    {
        uint32_t ps = 0x2545f491u;
        auto prnd = [&]() {
            ps ^= ps << 13;
            ps ^= ps >> 17;
            ps ^= ps << 5;
            return (ps & 0xFFFFFF) / double(0x1000000);
        };
        struct Dip {
            double s, len, y, ramp;
        };
        std::vector<Dip> dips;
        for (int w = 0; w < 2; w++) dips.push_back({sWallow[w], kWallowLen[w], kBayouY - kWallowDepth[w], 4.0});
        for (int k = 0; k < btN; k++)
            if (sCord == 0 && bayouMask(bt[k].x, bt[k].z) > 0.7) sCord = k + 4;
        dips.push_back({sCord + 4, 11, kBayouY - 0.06, 2.0});  // the wet stretch under the corduroy
        for (double s = sCord + 30; s < btN - 60;) {
            bool clear = bayouMask(bt[(int)s].x, bt[(int)s].z) > 0.85;
            for (const Dip& d : dips) clear = clear && std::fabs(s - d.s) > d.len / 2 + 14;
            for (double h : sHill) clear = clear && std::fabs(s - h) > 14;
            if (clear) dips.push_back({s, 5 + 6 * prnd(), kBayouY - 0.04 - 0.3 * prnd() * prnd(), 2.0});
            s += clear ? 18 + 22 * prnd() : 3;
        }
        for (int k = 0; k < btN; k++) {
            const double m = smoothstep(0.3, 0.9, bayouMask(bt[k].x, bt[k].z));
            if (m <= 0) continue;
            double y = kBayouY + 0.3 + 0.07 * std::sin(k * 1.15) * (0.5 + valueNoise(k * 0.05, 3.0));
            for (const Dip& d : dips) {
                const double u = k - d.s;
                const double f = smoothstep(-d.len / 2, -d.len / 2 + d.ramp, u) * smoothstep(d.len / 2, d.len / 2 - d.ramp, u);
                y += (std::min(y, d.y) - y) * f;
            }
            by[k] += (y - by[k]) * m;
        }
    }
    auto mudTread = [&](int id) { paint[id] = PAINT_MUD, surf[id] = SURF_MUD; };
    layTrail(bt, by, kBayouHalf, 5, mudTread);
    // The wallows: pits wider than the trail, so there's no driving round them.
    for (int w = 0; w < 2; w++) {
        const V3 c = pointAlong(bt, sWallow[w]);
        const double R = kWallowLen[w] / 2 + 8;
        for (int i = cellI(c.x - R); i <= cellI(c.x + R); i++)
            for (int j = cellI(c.z - R); j <= cellI(c.z + R); j++) {
                const Near t = nearest(bt, false, X(i), X(j));
                const double u = t.along - sWallow[w], L = kWallowLen[w];
                // Ragged at the sides (churned out wider in places); the ends, where you go in and
                // climb out, are left as laid.
                const double side = t.d - 2.5 * (valueNoise(X(i) * 0.3 + w * 7, X(j) * 0.3) - 0.5) - 1.5 * smoothstep(L / 2, 0, std::fabs(u));
                const double f = smoothstep(-L / 2, -L / 2 + 4, u) * smoothstep(L / 2, L / 2 - 4, u) * smoothstep(kBayouHalf + 5, kBayouHalf + 1.5, side);
                if (f <= 0) continue;
                const int id = i * hN + j;
                const double y = kBayouY - kWallowDepth[w] + 0.15 * smoothstep(kBayouHalf, kBayouHalf + 4, t.d);
                hgrid[id] = (float)(hgrid[id] + (std::min((double)hgrid[id], y) - hgrid[id]) * f);
                mudTread(id), keep[id] = 1;
            }
    }
    // The hill line, straight over the top.
    std::vector<V3> hillLine;
    for (double u = 0; u <= hillL; u += 1) hillLine.push_back(hillA + hillDir * u);
    layTrail(hillLine, groundProfile(hillLine, 2, 0.32), kBayouHalf, 5, mudTread);
    // Mud on the swamp floor (grass on the hummocks), and down the hill's flanks.
    for (int i = cellI(158); i <= cellI(500); i++)
        for (int j = cellI(-505); j <= cellI(-315); j++) {
            const int id = i * hN + j;
            if (swamp[id] < 0.2 || paint[id] != PAINT_NONE) continue;
            const double x = X(i), z = X(j);
            const double v = std::fabs(dot(V3(x - hillA.x, 0, z - hillA.z), hillLeft));
            const double u = dot(V3(x - hillA.x, 0, z - hillA.z), hillDir);
            const bool flank = u > 0 && u < hillL && v < 9 + 3 * valueNoise(x * 0.2, z * 0.2) && hillRise(u) > 0.05;
            if (flank || hgrid[id] < kBayouY + 0.42 + 0.4 * (valueNoise(x * 0.17, z * 0.17) - 0.5)) paint[id] = PAINT_MUD, surf[id] = SURF_MUD;
        }

    // ---- Paint the land: desert, rock, snow, scree ----------------------------------------
    for (int i = 1; i < hN - 1; i++)
        for (int j = 1; j < hN - 1; j++) {
            const int id = i * hN + j;
            const double x = X(i), z = X(j);
            const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / (2 * kRes), gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / (2 * kRes);
            const double slope = std::hypot(gx, gz), h = hgrid[id];
            const double dw = desertness(x, z);
            const bool onPath = paint[id] == PAINT_GRAVEL || paint[id] == PAINT_TRAIL || paint[id] == PAINT_TRACK || paint[id] == PAINT_WET;
            // The desert: the stage's gravel gives way to sand, the land to sand and hardpan.
            if (dw > 0.5 && paint[id] == PAINT_GRAVEL && edge[id] < 0.5 && waterY[id] < -1e8) paint[id] = PAINT_SAND, surf[id] = SURF_SAND;
            // Snow on the high trail.
            if (paint[id] == PAINT_TRAIL && h > 142) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            if (paint[id] != PAINT_NONE || onPath) continue;
            if (dw > 0.5) {
                const double dune = valueNoise(x * 0.03, z * 0.03);
                if (slope > 0.9) paint[id] = PAINT_REDROCK, surf[id] = SURF_ROCK;
                else if (dune > 0.42 || h > 7) paint[id] = PAINT_SAND, surf[id] = SURF_SAND;
                else paint[id] = PAINT_DESERT, surf[id] = SURF_DIRT;
                continue;
            }
            if (h > 140 + 12 * fbmNoise(x * 0.02, z * 0.02, 2) && slope < 0.75) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            else if (slope > 0.85 || h > 165) surf[id] = SURF_ROCK;
            else if (h > 95 && slope > 0.45 && valueNoise(x * 0.04, z * 0.04) > 0.55) paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
        }
    // Cold Creek under snow: everything but the cliffs, the ice and the lodge's yard, the road
    // too (packed snow). The snowline at the valley mouth is ragged.
    for (int i = cellI(330); i <= cellI(530); i++)
        for (int j = cellI(200); j <= cellI(530); j++) {
            const int id = i * hN + j;
            const double x = X(i), z = X(j);
            const double cm = coldMask(x, z);
            if (cm < 0.5 + 0.4 * (valueNoise(x * 0.09 + 5.0, z * 0.09) - 0.5) + 0.3 * (valueNoise(x * 0.3, z * 0.3) - 0.5)) continue;
            if (paint[id] == PAINT_ICE || paint[id] == PAINT_PAD || paint[id] == PAINT_GRAVEL || waterY[id] > -1e8) continue;
            const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / 2, gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / 2;
            const double slope = std::hypot(gx, gz);
            // Snow sticks up to ~50 degrees; past that, grey rock.
            if (paint[id] == PAINT_TRAIL || slope < 1.15 + 0.35 * (valueNoise(x * 0.07, z * 0.07) - 0.5)) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            else paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
        }
    // Ploughed snow banked up along both sides of the road in the valley.
    {
        const std::vector<V3>& road = snowRoad();
        for (int i = cellI(350); i <= cellI(470); i++)
            for (int j = cellI(190); j <= cellI(480); j++) {
                const int id = i * hN + j;
                if (paint[id] != PAINT_SNOW) continue;
                const Near r = nearest(road, false, X(i), X(j));
                const double u = r.d - kSnowHalf - 0.4;
                if (u <= 0 || u >= 2.8) continue;
                hgrid[id] += (float)(0.55 * std::sin(M_PI * u / 2.8) * smoothstep(80, 110, r.along) * (0.8 + 0.4 * valueNoise(X(i) * 0.2, X(j) * 0.2)));
            }
    }
    // Ice on the road: where it crosses the frozen creek, and two patches of black ice.
    iceCreek(false);
    for (const double s0 : {156.0, 236.0})
        for (double s = s0; s < s0 + 8; s += 0.5) {
            const V3 p = pointAlong(snowRoad(), s);
            for (int i = cellI(p.x - 3); i <= cellI(p.x + 3); i++)
                for (int j = cellI(p.z - 3); j <= cellI(p.z + 3); j++)
                    if (std::hypot(X(i) - p.x, X(j) - p.z) < kSnowHalf * (0.6 + 0.4 * valueNoise(X(i) * 0.4, X(j) * 0.4)))
                        paint[i * hN + j] = PAINT_ICE, surf[i * hN + j] = SURF_ICE;
        }

    // ---- Repair shops, the start ---------------------------------------------------------------
    addStation(V3(-60, paddockY, -150), 0, "PADDOCK SHOP");
    addStation(V3(outpost.x, height(outpost.x, outpost.z), outpost.z), M_PI / 2, "DESERT OUTPOST");
    addStation(V3(lakeside.x, height(lakeside.x, lakeside.z), lakeside.z), M_PI / 2, "LAKESIDE GARAGE");
    addStation(V3(hut.x, height(hut.x, hut.z), hut.z), 0, "MOUNTAIN HUT");
    addStation(V3(kLodge.x, height(kLodge.x, kLodge.z), kLodge.z), -M_PI / 2, "COLD CREEK LODGE");
    spawn = V3(-118, paddockY, -146);
    spawnYaw = 0;
    parking = {V3(-118, 0, -138), V3(-118, 0, -154), V3(-104, 0, -138)};
    parkingYaw = 0;

    // ---- Boulders ----------------------------------------------------------------------------
    uint32_t rs = 0x9e3779b9u;
    auto rnd = [&]() {
        rs ^= rs << 13;
        rs ^= rs >> 17;
        rs ^= rs << 5;
        return (rs & 0xFFFFFF) / double(0x1000000);
    };
    // The rock garden: a stretch of the summit trail's first traverse strewn with boulders
    // to crawl over (each one climbable by a set-up truck: up to ~0.4 m in the tread),
    // bigger ones along the edges.
    for (int k = 0; k < 44 && trail.size() > 120; k++) {
        const size_t idx = 40 + (size_t)(rnd() * 70);
        const V3 a = trail[idx], b = trail[idx + 1];
        const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z)), l(-dir.z, 0, dir.x);
        const bool edgeRock = k % 4 == 0;
        const double side = edgeRock ? (rnd() < 0.5 ? -1 : 1) * (3.8 + rnd()) : (rnd() - 0.5) * 5.0;
        const V3 q = a + l * side;
        const double r = edgeRock ? 0.8 + rnd() * 0.6 : 0.3 + rnd() * 0.45;
        const double top = edgeRock ? r * 0.8 : std::min(0.4, r * (0.45 + 0.35 * rnd()));
        addRock(q.x, q.z, V3(r * 1.2, r * 0.8, r), top, (uint32_t)(5000 + k), (float)(0.85 + rnd() * 0.3));
    }
    if (trail.size() > 80) gGarden = trail[60];
    // THE WALL's gully: boulders heaped along both banks, too big to climb (the stairs are
    // the way up).
    {
        const V3 f(std::cos(wallYaw), 0, -std::sin(wallYaw)), l(-f.z, 0, f.x);
        const double L = 12 * kStairTread;
        int k = 0;
        for (double s = -3; s <= L + 3; s += 2.4)
            for (int side = -1; side <= 1; side += 2) {
                const double r = 0.9 + rnd() * 0.5;
                const V3 q = gWallFoot + f * (s + (rnd() - 0.5)) + l * (side * (4.5 + 0.6 * r + rnd() * 0.5));
                addRock(q.x, q.z, V3(r * 1.25, r, r * 1.1), r * (1.0 + 0.3 * rnd()), (uint32_t)(6000 + k++), 0.95f);
            }
    }
    // The dry wash: a gravel channel through the desert south of the mesa, choked with boulders.
    {
        const std::vector<V3> wash = splinePoints({{-470, 0, 260}, {-430, 0, 180}, {-460, 0, -20}, {-420, 0, -120}, {-470, 0, -200}}, false);
        for (size_t k = 0; k < wash.size(); k++) {
            const V3& p = wash[k];
            for (int i = cellI(p.x - 7); i <= cellI(p.x + 7); i++)
                for (int j = cellI(p.z - 7); j <= cellI(p.z + 7); j++) {
                    const double d = std::hypot(X(i) - p.x, X(j) - p.z);
                    if (d > 6.5) continue;
                    const int id = i * hN + j;
                    hgrid[id] = (float)std::min((double)hgrid[id], height(p.x, p.z) - 0.9 * smoothstep(6.5, 2, d) + 0.2);
                    if (d < 4.5) paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL, keep[id] = 1;
                }
            if (k % 3 == 0) {
                const double r = 0.4 + rnd() * 0.9;
                addRock(p.x + (rnd() - 0.5) * 6, p.z + (rnd() - 0.5) * 6, V3(r * 1.1, r * 0.75, r), r * 0.6, (uint32_t)(7000 + k), 1.1f);
            }
        }
    }
    // Scattered: desert rocks on the hardpan, granite boulders through the range.
    for (int attempt = 0; attempt < 30000 && rocks.size() < 420; attempt++) {
        const double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8) continue;
        const double dw = desertness(x, z), m = mountainness(z);
        if (dw < 0.5 && m < 0.15 && rnd() > 0.15) continue;
        const double r = 0.4 + rnd() * rnd() * 1.8, flat = 0.5 + 0.4 * rnd();
        addRock(x, z, V3(r, r * flat, r * (0.75 + 0.3 * rnd())), r * flat * (0.5 + 0.5 * rnd()), (uint32_t)(9000 + attempt),
                dw > 0.5 ? 1.15f : (float)(0.85 + rnd() * 0.2));
    }

    // ---- Trees: pines through the range up to the treeline, broadleaf along the water and
    // the south meadows, a few dead snags on the desert edge ----------------------------------
    for (int attempt = 0; attempt < 60000 && trees.size() < 1400; attempt++) {
        const double x = (rnd() * 2 - 1) * 515, z = (rnd() * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE) continue;
        const double h = hgrid[id], dw = desertness(x, z), m = smoothstep(120, 220, z);
        if (h > 132 || surf[id] == SURF_ROCK) continue;
        // Keep the circuit's run-off clear.
        if (nearest(track, true, x, z).d < 22) continue;
        Tree t;
        double p;
        if (dw > 0.6) {
            p = 0.01;
            t.kind = 2;
        } else if (m > 0.2) {
            p = 0.75 * m;  // pine forest
            t.kind = rnd() < 0.08 ? 2 : 0;
        } else {
            p = z < -260 ? 0.12 : 0.04;
            t.kind = rnd() < 0.7 ? 1 : 0;
        }
        if (rnd() > p) continue;
        t.base = V3(x, h - 0.2, z);
        t.height = t.kind == 0 ? 9 + rnd() * 9 : 7 + rnd() * 7;
        t.radius = t.height * (t.kind == 0 ? 0.2 + rnd() * 0.06 : 0.26 + rnd() * 0.08);
        t.tint = (float)(0.8 + rnd() * 0.4);
        plant(t);
    }
    for (int attempt = 0; attempt < 20000 && bushes.size() < 1200; attempt++) {
        const double x = (rnd() * 2 - 1) * 510, z = (rnd() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || hgrid[id] > 120) continue;
        const double dw = desertness(x, z);
        if (dw > 0.5 && rnd() > 0.2) continue;  // scrub in the desert, sparse
        const double u = rnd(), m = mountainness(z);
        const int habitat = dw > 0.5 ? HAB_DESERT : m > 0.2 ? (hgrid[id] > 70 ? HAB_UPLAND : HAB_FOREST) : HAB_MEADOW;
        bushes.push_back(growBush(V3(x, hgrid[id], z), habitat, m > 0.05, u, dw > 0.5 ? 0.7f : (float)(0.8 + rnd() * 0.4)));
    }
    // Signs at the features.
    auto sign = [&](double x, double z, double yaw, float r, float g, float b) {
        Prop s;
        s.kind = PROP_SIGN;
        s.a = V3(x, height(x, z), z);
        s.yaw = yaw;
        s.col[0] = r, s.col[1] = g, s.col[2] = b;
        fixtures.push_back(s);
    };
    sign(kMesa.x + kMesaR + kMesaSteps * kStairTread + 6, kMesa.z + 7, M_PI, 0.1f, 0.1f, 0.1f);  // MESA CLIMB: black
    sign(gWallFoot.x + 6, gWallFoot.z - 4, -M_PI / 2, 0.1f, 0.1f, 0.1f);  // THE WALL: black
    sign(gGarden.x, gGarden.z - 5, M_PI, 0.85f, 0.15f, 0.12f);          // the rock garden: red

    // ---- Blackwater Bayou and Cold Creek: what grows there, what lies about, the markers ----
    // (Their own random numbers, so everything above stays where it was.)
    uint32_t rs2 = 0x51ed2701u;
    auto rnd2 = [&]() {
        rs2 ^= rs2 << 13;
        rs2 ^= rs2 >> 17;
        rs2 ^= rs2 << 5;
        return (rs2 & 0xFFFFFF) / double(0x1000000);
    };
    // A sign beside a trail `s` metres along it, on the side `side` (+1 left), facing whoever
    // drives up it.
    auto trailSign = [&](const std::vector<V3>& pts, double s, double side, float r, float g, float b) {
        const V3 p = pointAlong(pts, s), d = norm(pointAlong(pts, s + 2) - p), l(-d.z, 0, d.x);
        const V3 q = p + l * side;
        sign(q.x, q.z, std::atan2(d.z, -d.x), r, g, b);
    };
    auto marker = [&](const V3& p) {
        Prop m;
        m.kind = PROP_MARKER;
        m.a = V3(p.x, height(p.x, p.z), p.z);
        fixtures.push_back(m);
    };
    auto plant = [&](double x, double z, int kind, double hgt, double tint) {
        Tree t;
        t.kind = kind;
        t.base = V3(x, height(x, z) - 0.2, z);
        t.height = hgt;
        t.radius = hgt * (kind == 0 ? 0.2 + rnd2() * 0.06 : 0.26 + rnd2() * 0.08);
        t.tint = (float)tint;
        World::plant(t);
    };
    {
        // The swamp: dark, mossy broadleaf on the hummocks, drowned snags standing in the
        // water, the odd pine; brush; fallen logs and stumps.
        const std::vector<V3>& bt = bayouTrail();
        int planted = 0;
        for (int attempt = 0; attempt < 20000 && planted < 420; attempt++) {
            const double x = 160 + rnd2() * 335, z = -500 + rnd2() * 180;
            const int id = at(x, z);
            if (swamp[id] < 0.45 || keep[id]) continue;
            const double h = hgrid[id];
            const bool wet = h < kBayouY + 0.02;
            const double grove = smoothstep(0.35, 0.6, valueNoise(x * 0.025 + 7.0, z * 0.025));
            if (rnd2() > (wet ? 0.1 : 0.1 + 0.5 * grove)) continue;
            const V3 dh(x - hillA.x, 0, z - hillA.z);
            const bool onHillLine = std::fabs(dot(dh, hillLeft)) < 6.5 && dot(dh, hillDir) > -4 && dot(dh, hillDir) < hillL + 4;
            if (onHillLine || nearest(bt, false, x, z).d < 6.5) continue;
            const double k = rnd2();
            if (wet) plant(x, z, k < 0.8 ? 2 : 1, 6 + rnd2() * 7, 0.45 + rnd2() * 0.15);
            else plant(x, z, k < 0.66 ? 1 : k < 0.84 ? 2 : 0, 7 + rnd2() * 7, 0.45 + rnd2() * 0.2);
            planted++;
        }
        for (int attempt = 0; attempt < 6000; attempt++) {
            const double x = 160 + rnd2() * 335, z = -500 + rnd2() * 180;
            const int id = at(x, z);
            if (swamp[id] < 0.4 || keep[id] || hgrid[id] < kBayouY + 0.15 || rnd2() > 0.35) continue;
            const double u = rnd2();
            bushes.push_back(growBush(V3(x, hgrid[id], z), HAB_WETLAND, false, u, (float)(0.65 + rnd2() * 0.25)));
        }
        // Fallen logs alongside the trail and out in the water, and stumps by it.
        for (int k = 0, placed = 0; k < 400 && placed < 34; k++) {
            const double s = 40 + rnd2() * (bt.size() - 120);
            const V3 p = pointAlong(bt, s), d = norm(pointAlong(bt, s + 2) - p), l(-d.z, 0, d.x);
            const V3 c = p + l * ((rnd2() < 0.5 ? -1 : 1) * (kBayouHalf + 3.5 + rnd2() * 9));
            if (swamp[at(c.x, c.z)] < 0.5 || keep[at(c.x, c.z)]) continue;
            const double yaw = std::atan2(d.z, d.x) + (rnd2() - 0.5) * 1.2, L = 3 + rnd2() * 4, r = 0.17 + rnd2() * 0.15;
            const V3 ax(std::cos(yaw) * L / 2, 0, std::sin(yaw) * L / 2);
            V3 a = c - ax, b = c + ax;
            if (keep[at(a.x, a.z)] || keep[at(b.x, b.z)]) continue;
            a.y = height(a.x, a.z) + r * 0.55, b.y = height(b.x, b.z) + r * 0.55;
            logs.push_back({a, b, r});
            placed++;
            if (placed % 3 == 0) {  // a stump where it fell from
                const V3 st = a - ax * (0.25 + 0.1 * rnd2());
                if (keep[at(st.x, st.z)]) continue;
                Prop sp{PROP_STUMP, V3(st.x, height(st.x, st.z), st.z), V3(r * 1.3, 0.5 + rnd2() * 0.6, 0), rnd2() * 6.28};
                fixtures.push_back(sp);
                poles.push_back({st.x, st.z, sp.a.y - 0.4, sp.a.y + sp.b.y, sp.b.x});
            }
        }
        // The corduroy: logs laid side by side across the trail over its first wet stretch.
        for (double s = sCord; s < sCord + 9; s += 0.42) {
            const V3 p = pointAlong(bt, s), d = norm(pointAlong(bt, s + 1) - p), l(-d.z, 0, d.x);
            const double r = 0.1 + 0.02 * rnd2(), half = kBayouHalf + 0.5 + 0.3 * rnd2();
            V3 a = p + l * half, b = p - l * (half + 0.3 * rnd2());
            a.y = height(a.x, a.z) + r * 0.7, b.y = height(b.x, b.z) + r * 0.7;
            logs.push_back({a, b, r});
        }
        // Orange-topped posts mark the line, every 22 m, side to side; signs at the way in,
        // the wallows and the hill.
        for (double s = 14, side = 1; s < bt.size() - 10; s += 22, side = -side) {
            const V3 p = pointAlong(bt, s), d = norm(pointAlong(bt, s + 1) - p), l(-d.z, 0, d.x);
            marker(p + l * (side * (kBayouHalf + 1.4)));
        }
        trailSign(bt, 10, kBayouHalf + 2.5, 0.03f, 0.03f, 0.03f);          // BLACKWATER BAYOU: black
        for (int w = 0; w < 2; w++) trailSign(bt, sWallow[w] - kWallowLen[w] / 2 - 10, -(kBayouHalf + 2), 0.03f, 0.03f, 0.03f);  // the wallows
        trailSign(hillLine, 6, kBayouHalf + 2, 0.85f, 0.15f, 0.12f);       // MUD HILL: red
        // A fishing shack at the way in.
        {
            const V3 p = pointAlong(bt, 24), d = norm(pointAlong(bt, 26) - p), l(-d.z, 0, d.x);
            const V3 c = p - l * 13;
            const double y = height(c.x, c.z), yaw = std::atan2(-l.z, l.x) + M_PI;  // facing the trail
            fixtures.push_back({PROP_CABIN, V3(c.x, y, c.z), V3(), yaw});
            addBox(V3(c.x, y + 1.7, c.z), V3(2.3, 1.9, 2.8), yaw, 0, 0, V3(0.4, 0.3, 0.2));
            boxes.back().hidden = true;
        }
    }
    {
        // Cold Creek: snow-laden pines in groves on the floor and up the gentler walls, thinning
        // with height; snowy boulders fallen from the walls; snow poles along the road.
        const std::vector<V3>& road = snowRoad();
        int planted = 0;
        for (int attempt = 0; attempt < 24000 && planted < 520; attempt++) {
            const double x = 335 + rnd2() * 175, z = 225 + rnd2() * 285;
            const int i = cellI(x), j = cellI(z), id = i * hN + j;
            if (keep[id] || paint[id] != PAINT_SNOW || hgrid[id] > 150) continue;
            const double slope = std::hypot(hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j], hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / 2;
            if (slope > 0.75) continue;
            const double grove = smoothstep(0.4, 0.62, valueNoise(x * 0.03 + 1.0, z * 0.03 + 8.0));
            const double p = (0.06 + 0.7 * grove) * (1 - smoothstep(95, 150, hgrid[id]));
            if (rnd2() > p || nearest(road, false, x, z).d < 7.5 || std::hypot(x - kLodge.x, z - kLodge.z) < 22) continue;
            plant(x, z, rnd2() < 0.06 ? 2 : 0, 7 + rnd2() * 9 * (1 - 0.5 * smoothstep(60, 140, hgrid[id])), 0.75 + rnd2() * 0.25);
            planted++;
        }
        for (int attempt = 0, placed = 0; attempt < 3000 && placed < 60; attempt++) {
            const double s = rnd2() * (cold.size() - 1);
            const V3 p = pointAlong(cold, s), d = norm(pointAlong(cold, s + 2) - p), l(-d.z, 0, d.x);
            const V3 c = p + l * ((rnd2() < 0.5 ? -1 : 1) * (kColdHalf - 12 + rnd2() * 22));
            if (keep[at(c.x, c.z)] || nearest(road, false, c.x, c.z).d < 7) continue;
            const double r = 0.6 + rnd2() * rnd2() * 2.2;
            addRock(c.x, c.z, V3(r * 1.2, r * 0.8, r), r * (0.5 + 0.3 * rnd2()), (uint32_t)(12000 + attempt), (float)(0.85 + rnd2() * 0.15));
            placed++;
        }
        for (double s = 40; s < road.size() - 6; s += 26)
            for (int side = -1; side <= 1; side += 2) {
                const V3 p = pointAlong(road, s), d = norm(pointAlong(road, s + 1) - p), l(-d.z, 0, d.x);
                marker(p + l * (side * (kSnowHalf + 1.6)));
            }
        trailSign(road, 14, kSnowHalf + 2.5, 0.1f, 0.3f, 0.8f);  // COLD CREEK: blue
        // The lodge: a log cabin beside the repair shed.
        const V3 c = kLodge + V3(15, 0, 3);
        const double y = height(c.x, c.z);
        fixtures.push_back({PROP_CABIN, V3(c.x, y, c.z), V3(), M_PI});
        addBox(V3(c.x, y + 1.7, c.z), V3(2.3, 1.9, 2.8), M_PI, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    }
}

V3 frontierFeature(const char* name) {
    const std::string n = name;
    if (n == "start") return V3(-118, 0, -146);
    if (n == "mesa") return kMesa;
    if (n == "mesaclimb") return kMesa + V3(kMesaR + kMesaSteps * kStairTread, 0, 0);
    if (n == "wall") return gWallFoot;
    if (n == "walltop") return gWallTop;
    if (n == "wallfrom") return gWallFrom;
    if (n == "garden") return gGarden;
    if (n == "summit") return gSummit;
    if (n == "tarn") return kTarn;
    if (n == "lake") return kEchoLake;
    if (n == "bog") return kBog;
    if (n == "delta") return kDelta;
    if (n == "bowl") return kBowl;
    // Blackwater Bayou and Cold Creek. "bayou:S" / "snowroad:S" are the points S metres along
    // the mud trail / the snow road (from where they leave the rally stage).
    if (n == "bayou") return kBayou;
    if (n == "bayoufrom") return bayouTrail()[0];
    if (n == "wallow1" || n == "wallow2") return pointAlong(bayouTrail(), bayouAlong(kWallowAt[n[6] - '1']));
    if (n == "mudhill") {
        const V3 a = hillEnd(false), b = hillEnd(true);
        return a + (b - a) * kHillTopAt;
    }
    if (n == "mudhillfrom") return hillEnd(false);
    if (n == "mudhillto") return hillEnd(true);
    if (n == "snowroad") return snowRoad()[0];
    if (n == "snowfield") return pointAlong(snowRoad(), 170);
    if (n == "lodge") return kLodge;
    if (n == "icepond") return kIcePond;
    if (n == "coldcreek") return pointAlong(resample(splinePoints(kColdCtl, false), 1.0), 150);
    if (n.rfind("bayou:", 0) == 0) return pointAlong(bayouTrail(), std::atof(n.c_str() + 6));
    if (n.rfind("snowroad:", 0) == 0) return pointAlong(snowRoad(), std::atof(n.c_str() + 9));
    if (n.rfind("ford", 0) == 0) {
        // Where the rally stage crosses the rivers: Snake River twice, then the Muddy twice.
        static const std::vector<V3> fords = [] {
            std::vector<V3> out;
            const std::vector<V3> rally = splinePoints(kRallyCtl, true);
            for (const auto* ctl : {&kSnakeCtl, &kMuddyCtl}) {
                const std::vector<V3> riv = splinePoints(*ctl, false);
                std::vector<V3> found;
                for (size_t k = 0; k < rally.size(); k++) {
                    const Near r = nearest(riv, false, rally[k].x, rally[k].z);
                    if (r.d > 3) continue;
                    bool fresh = true;
                    for (V3& f : found)
                        if (std::hypot(f.x - rally[k].x, f.z - rally[k].z) < 40) fresh = false;
                    if (fresh) found.push_back(rally[k]);
                }
                out.insert(out.end(), found.begin(), found.end());
            }
            return out;
        }();
        const int k = std::atoi(n.c_str() + 4) - 1;
        return k >= 0 && k < (int)fords.size() ? fords[k] : V3();
    }
    if (n == "track") return frontierTrack()[0];
    if (n == "outpost") return V3(-292, 0, -50);
    if (n == "lakeside") return V3(332, 0, 120);
    if (n == "hut") return V3(92, 0, 214);
    return V3();
}

bool frontierFeatureTable(std::string* why) {
    const struct {
        const char* name;
        V3 built, table;
    } all[] = {{"wall", gWallFoot, kWallFootAt}, {"walltop", gWallTop, kWallTopAt}, {"wallfrom", gWallFrom, kWallFromAt},
               {"summit", gSummit, kSummitAt}, {"garden", gGarden, kGardenAt}};
    bool ok = true;
    for (const auto& f : all)
        if (f.built.x != f.table.x || f.built.y != f.table.y || f.built.z != f.table.z) {
            ok = false;
            if (why) {
                char b[160];
                std::snprintf(b, sizeof b, "%s built at (%a, %a, %a); ", f.name, f.built.x, f.built.y, f.built.z);
                *why += b;
            }
        }
    return ok;
}

const std::vector<V3>& frontierTrack() {
    static const std::vector<V3> t = trackPoints();
    return t;
}
