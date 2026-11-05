// The Backcountry's trail network round Elk Valley: trails to follow out into the big country, marked on the ground (signs with
// their ratings at both ends, marker posts, cairns), named over their signs and drawn on the M map (World::trails).
//
//  - Green two-tracks link the districts: RANGER ROAD (the trailhead to the ranger station, down through Elk River's canyon at Ranger
//    Ford), TIMBER ROAD, COPPER VALLEY ROAD (Ranger Flats to High Lakes under Copper Peak), LAKESHORE ROAD, SOUTH FORK ROAD, RIM ROAD.
//  - Blue trails reach the places off them: the lake's fishing shack, the badlands and the Lost Mine, the Bear Creek ford, the falls,
//    the north rim and High Lakes, the loggers' camp.
//  - Black trails cross the high ground: Copper Ridge, the summit ridge, High Lakes Peak, and the spur up Copper Peak.
//  - COPPER PASS (red), the mountain pass: from Pass Hut up through THE NOTCH (a rock garden and four rock ledges in the crags), down
//    across COPPER CREEK, up a loose SCREE climb and seven tight SWITCHBACKS, through THE CHOKE (two boulders a truck's width apart and
//    a step between them), then THE SHELF: a ledge cut diagonally up the Rimrock (a 24 m cliff band round Copper Peak), the drop growing
//    beside it, with THE SLAB (one huge rock jutting over the drop) to drive out onto; over the saddle (COPPER PASS LOOKOUT) and down to
//    the Surveyor's Cabin. A set-up rig (low range, lockers, aired down) makes it; a stock truck stops at the ledges or the choke.
//
// The trails' lines are a fixed table (bctrails_routes.inc, from tools/trailroute.cpp): nothing is routed when the map is built, so
// every machine builds the same land. Everything here takes its random numbers from hashes of where it is, not from the map's stream.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_set>

#include "detmath.h"
#include "loadprof.h"
#include "onemap.h"
#include "places.h"
#include "rivers.h"
#include "sections.h"
#include "world.h"

namespace {

using Poly = std::vector<V3>;
constexpr double kSetPieceGap = 28;  // (between one section's cleared ground and the next: more than the blend)

struct Row {
    const char* name;
    int rating;
    double maxGrade;
    std::vector<std::pair<double, double>> ctl;
};
const std::vector<Row>& rows() {
    static const std::vector<Row> r = {
#include "bctrails_routes.inc"
    };
    return r;
}

double sq(double v) { return v * v; }
double h01(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
    return (h & 0xFFFFFF) / double(0x1000000);
}

Poly resample(const Poly& pts, double step) {
    Poly out{pts[0]};
    double carry = 0;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        const V3 a = pts[k], b = pts[k + 1];
        const double L = dm::hypot(b.x - a.x, b.z - a.z);
        if (L < 1e-9) continue;
        double s = step - carry;
        for (; s <= L; s += step) out.push_back(a + (b - a) * (s / L));
        carry = L - (s - step);
    }
    if (dm::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.2 * step) out.push_back(pts.back());
    return out;
}
V3 pointAlong(const Poly& pts, double s) {
    const int k = (int)clampd(std::floor(s), 0, (double)pts.size() - 2);
    return pts[k] + (pts[k + 1] - pts[k]) * clampd(s - k, 0, 1);
}
V3 dirAlong(const Poly& pts, double s) {
    const V3 d = pointAlong(pts, s + 1.5) - pointAlong(pts, s - 1.5);
    return norm(V3(d.x, 0, d.z));
}
V3 leftAlong(const Poly& pts, double s) {
    const V3 d = dirAlong(pts, s);
    return V3(-d.z, 0, d.x);
}
int nearestIdx(const Poly& pts, double x, double z) {
    int best = 0;
    double bd = 1e18;
    for (int k = 0; k < (int)pts.size(); k++) {
        const double d = sq(pts[k].x - x) + sq(pts[k].z - z);
        if (d < bd) bd = d, best = k;
    }
    return best;
}

const std::vector<Poly>& lines() {
    static const std::vector<Poly> L = [] {
        std::vector<Poly> out;
        for (const Row& r : rows()) {
            std::vector<V3> c;
            for (const auto& p : r.ctl)
                if (c.empty() || dm::hypot(c.back().x - p.first, c.back().z - p.second) > 0.5) c.push_back(V3(p.first, 0, p.second));
            out.push_back(c.size() > 1 ? resample(splinePoints(c, false), 1.0) : c);
        }
        return out;
    }();
    return L;
}
int rowOf(const std::string& name) {
    for (size_t k = 0; k < rows().size(); k++)
        if (name == rows()[k].name) return (int)k;
    return -1;
}

// ---- COPPER PASS: where its obstacles are (indices along its line, from where the table's vias put them) -----------------------
struct PassPlan {
    int notch0, notch1;          // the climb through the crags
    std::vector<int> ledges;     // rock ledges in it
    int creek;                   // where it crosses Copper Creek
    int scree0, scree1;          // the loose climb
    std::vector<int> hairpins;   // the switchbacks' turns
    int choke;                   // the two boulders
    int shelf0, shelf1, slab;    // the shelf up the Rimrock, and the slab on it
    int saddle;
};
const Poly& passLine() { return lines()[std::max(0, rowOf("COPPER PASS"))]; }
const PassPlan& passPlan() {
    static const PassPlan p = [] {
        PassPlan q;
        const Poly& L = passLine();
        auto at = [&](double x, double z) { return nearestIdx(L, x, z); };
        q.notch0 = at(80, 520), q.notch1 = at(70, 630);
        for (int k = 0; k < 4; k++) q.ledges.push_back(q.notch0 + 18 + (q.notch1 - q.notch0 - 36) * k / 3);
        q.creek = at(60, 882);
        q.scree0 = q.creek + 18, q.scree1 = std::min(q.creek + 95, at(250, 941) - 12);
        for (const auto& h : std::vector<std::pair<double, double>>{{259, 950}, {21, 1010}, {259, 1070}, {31, 1130}, {264, 1185}}) q.hairpins.push_back(at(h.first, h.second));
        q.shelf0 = at(210, 1216), q.shelf1 = at(82, 1281), q.slab = at(143, 1242);
        q.choke = q.shelf0 - 22;
        q.saddle = at(33, 1403);
        return q;
    }();
    return p;
}

}  // namespace

// The obstacle trails: the proving ground's section park's forest path, gully and rock crawl, three sections each, out in country of
// their kind (Timber Basin's woods, the south uplands, the edge of the badlands), built by the same generators (sections.h).
const std::vector<BcSetPiece>& bcSetPieces() {
    static const std::vector<BcSetPiece> s = [] {
        std::vector<BcSetPiece> v = {
            {"BC.TIMBER_TRAIL", "TIMBER TRAIL", "BC.CACHE.T9", "SAWYER'S TOOL CHEST", 1300, V3(-2050, 0, 1000), V3(), -M_PI / 2, {SEC_TIMBER, SEC_MUD_PIT, SEC_TIMBER}},
            {"BC.BOULDER_GULLY", "BOULDER GULLY", "BC.CACHE.T10", "GULLY CACHE", 1200, V3(700, 0, -620), V3(), M_PI / 2, {SEC_STEEP_HILL, SEC_ROCKY_RIVER, SEC_STEEP_HILL}},
            {"BC.SLICKROCK_CRAWL", "SLICKROCK CRAWL", "BC.CACHE.T11", "CRAWLER'S STASH", 1500, V3(830, 0, -1080), V3(), 0, {SEC_ROCK_WALL, SEC_CLIFF_SHELF, SEC_ROCK_WALL}},
        };
        for (BcSetPiece& p : v) {
            const V3 dir(dm::cos(p.heading), 0, -dm::sin(p.heading));
            double along = 0;
            for (size_t k = 0; k < p.types.size(); k++) {
                const SectionFootprint fp = sectionFootprint((SectionType)p.types[k]);
                along += fp.length + 2 * fp.apron + (k + 1 < p.types.size() ? kSetPieceGap : 0);
            }
            p.exit = p.entry + dir * along;
        }
        return v;
    }();
    return s;
}

const std::vector<BcTrailInfo>& bcTrails() {
    static const std::vector<BcTrailInfo> t = [] {
        std::vector<BcTrailInfo> v;
        for (const Row& r : rows()) v.push_back({r.name, r.rating, r.maxGrade});
        return v;
    }();
    return t;
}
const std::vector<V3>& bcTrailLine(const std::string& name) {
    static const Poly none;
    const int k = rowOf(name);
    return k < 0 ? none : lines()[k];
}
V3 bcTrailFeature(const char* name) {
    const std::string n = name;
    const Poly& L = passLine();
    if (L.size() < 2) return V3();
    const PassPlan& p = passPlan();
    if (n == "passstart") return pointAlong(L, 4);
    if (n == "notch") return pointAlong(L, p.notch0);
    if (n == "notchtop") return pointAlong(L, p.notch1 + 4);
    if (n == "creek") return pointAlong(L, p.creek);
    if (n == "creekbank") return pointAlong(L, p.creek - 14);  // (the way in, on the south bank)
    if (n == "scree") return pointAlong(L, p.scree0);
    if (n == "switchbacks") return pointAlong(L, p.hairpins.front());
    if (n == "choke") return pointAlong(L, p.choke - 6);
    if (n == "shelf") return pointAlong(L, p.shelf0);
    if (n == "slab") return pointAlong(L, p.slab);
    if (n == "shelftop") return pointAlong(L, p.shelf1 + 6);
    if (n == "saddle") return pointAlong(L, p.saddle);
    if (n == "passend") return pointAlong(L, (double)L.size() - 6);
    if (n == "peak") {
        const int k = rowOf("COPPER PEAK SPUR");
        return k < 0 ? V3() : lines()[k].back();
    }
    return V3();
}

void World::layBackcountryTrails(CellTiles<uint8_t>& keep, CellTiles<float>& edge) {
    loadprof::Scope lp("bc.trail network");
    const std::vector<Row>& R = rows();
    const std::vector<Poly>& Ls = lines();
    auto X = [&](int i) { return -half + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + half) / kRes), 0), hN - 1); };
    CellTiles<float> sd, sk, sl;
    sd.reset(hN, 1e9f), sk.reset(hN, 0), sl.reset(hN, 0);
    std::vector<int> cells;
    // fn(id, distance, index along, signed lateral (+ left)) for every cell within `reach` of the line (backcountry.cpp's scanNear).
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
                    const double d = dm::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    if (d > reach) continue;
                    const int id = i * hN + j;
                    if (sd[id] > 1e8f) cells.push_back(id);
                    if (d >= sd[id]) continue;
                    sd[id] = (float)d, sk[id] = (float)(s + t), sl[id] = (float)((dx * (z - a.z) - dz * (x - a.x)) / L);
                }
        }
        std::sort(cells.begin(), cells.end());  // (the same order on every machine)
        for (int id : cells) {
            fn(id, (double)sd[id], (double)sk[id], (double)sl[id]);
            sd[id] = 1e9f;
        }
    };
    auto yAt = [](const std::vector<double>& y, double k) {
        const int i = (int)clampd(std::floor(k), 0, (double)y.size() - 2);
        return y[i] + (y[i + 1] - y[i]) * clampd(k - i, 0, 1);
    };
    const int passRow = rowOf("COPPER PASS"), spurRow = rowOf("COPPER PEAK SPUR");
    const PassPlan& P = passPlan();
    uint32_t rockSeed = 3100000;
    // A rock of half-sizes r, `top` of it above the ground at (x, z), turned by `pose` when given.
    auto rock = [&](const V3& p, const V3& r, double top, float tint, const RockPose* pose = nullptr) -> Rock& {
        return addRock(p.x, p.z, r, top, rockSeed++, tint, pose);
    };

    // ---- The channels: Copper Creek, and the river through BOULDER GULLY's ROCKY RIVER -----------------------------------------------
    // Each is a line from its source to its outlet, a water level that only falls along it and a width that grows along it. It's cut
    // here, before the trails (they ford it at its bed, as through any water), and cut again once they're laid: their earthworks don't
    // fill it, so nothing breaks it into pieces.
    struct Channel {
        std::string name;
        Poly c;                                // source to outlet, 1 m apart
        std::vector<double> lvl, hw, dep, soft;  // water level, half-width, depth; soft: 1 at a ford (shallower, wider, easy banks)
        double speed = 0.9;
        V3 pond;  // (x, level, z): the pond it ends in; pondR 0: none (it runs into other water)
        double pondR = 0;
        int shelfK = -1;  // where it crosses a section's flat channel
    };
    CellTiles<uint8_t> channelWater;  // the cells a channel watered (not Elk River's, which one runs into)
    channelWater.reset(hN, 0);
    auto cutChannel = [&](const Channel& ch) {
        const int n = (int)ch.c.size();
        double cutMax = 0, hwMax = 0;
        for (int k = 0; k < n; k++) cutMax = std::max(cutMax, height(ch.c[k].x, ch.c[k].z) - ch.lvl[k]), hwMax = std::max(hwMax, ch.hw[k]);
        scanNear(ch.c, clampd(hwMax + 6 + cutMax / 0.7, 14.0, 44.0), [&](int id, double d, double k, double) {
            const double L = yAt(ch.lvl, k), hw = yAt(ch.hw, k), dep = yAt(ch.dep, k), soft = yAt(ch.soft, k), e = d - hw;
            // (Its banks: a short step up out of the bed, then no steeper than 1 in 1.4 (1 in 5 at a ford) up to the land.)
            const double bankRun = 1.0 + 4.0 * soft;
            const double target = e < 0 ? L - dep * (1 - sq(d / hw)) : L + std::min(e, bankRun) * 0.25 / bankRun + std::max(0.0, e - bankRun) * (0.7 - 0.5 * soft);
            if (target < hgrid[id] && (e < 1.5 || edge[id] > 1.5)) hgrid[id] = (float)target;  // (its bank doesn't bite into a tread beside it)
            if (e < 0.3) paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL;
            if (e < 4) keep[id] = 1;
            const bool other = waterY[id] > -1e8f && !channelWater[id];  // (the river it runs into)
            // (The water's edge: on the bank, but for a section's wide flat channel. A hollow beyond it, lower than the water, is filled:
            // the bank holds the water in.)
            const double edgeAt = 0.4 + (ch.shelfK < 0 ? 0.0 : 1.3 * smoothstep(46, 30, std::fabs(k - ch.shelfK)));
            if (e >= edgeAt) {
                if (e < 4 && hgrid[id] < L + 0.05 && !other) hgrid[id] = (float)(L + 0.05);
                if (channelWater[id]) waterY[id] = -1e9f, flowX[id] = flowZ[id] = 0, channelWater[id] = 0;
                return;
            }
            if (hgrid[id] >= L - 0.005) {  // (a trail's bank: no water here now, none left under it)
                if (channelWater[id]) waterY[id] = -1e9f, flowX[id] = flowZ[id] = 0, channelWater[id] = 0;
                return;
            }
            if (other) return;
            const int ki = (int)clampd(std::floor(k), 0, n - 1), k0 = std::max(0, ki - 6), k1 = std::min(n - 1, ki + 6);
            const double fall = (ch.lvl[k0] - ch.lvl[k1]) / std::max(1, k1 - k0), sp = ch.speed * (0.8 + std::min(1.2, 25 * fall)) * (1 - 0.5 * sq(std::min(1.0, d / hw)));
            const V3 dir = dirAlong(ch.c, k);
            waterY[id] = (float)L, flowX[id] = (float)(dir.x * sp), flowZ[id] = (float)(dir.z * sp), channelWater[id] = 1;
        });
        if (ch.pondR <= 0) return;
        // Its pond: still water in a dish, its shore uneven, shelving gently up to the land round it.
        const double R = ch.pondR, W = ch.pond.y;
        for (int i = cellI(ch.pond.x - R - 16); i <= cellI(ch.pond.x + R + 16); i++)
            for (int j = cellI(ch.pond.z - R - 16); j <= cellI(ch.pond.z + R + 16); j++) {
                const double x = X(i), z = X(j), d = dm::hypot(x - ch.pond.x, z - ch.pond.z);
                const double r = R * (0.72 + 0.56 * valueNoise(x * 0.035 + 7, z * 0.035 + 3)), e = d - r;
                if (e > 14) continue;
                const int id = i * hN + j;
                const double target = e < 0 ? W - 1.1 * (1 - sq(d / r)) : W + 0.3 * e;
                if (target < hgrid[id]) hgrid[id] = (float)target;
                if (e < 3) keep[id] = 1;
                if (e < 2) paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL;
                if (hgrid[id] < W - 0.005) waterY[id] = (float)W, flowX[id] = flowZ[id] = 0, channelWater[id] = 1;
            }
    };
    // Where a trail rated `maxRating` or easier crosses a channel's line, a ford: soft, by how near along it (26 m to 12 m).
    auto markFords = [&](const Poly& c, int maxRating, std::vector<double>& soft) {
        const int n = (int)c.size();
        double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
        for (const V3& p : c) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
        for (size_t t = 0; t < R.size(); t++) {
            if (R[t].rating > maxRating) continue;
            const Poly& L = Ls[t];
            for (size_t q = 0; q + 1 < L.size(); q++) {
                const V3 &a = L[q], &b = L[q + 1];
                if (std::max(a.x, b.x) < x0 || std::min(a.x, b.x) > x1 || std::max(a.z, b.z) < z0 || std::min(a.z, b.z) > z1) continue;
                for (int k = 0; k + 1 < n; k++) {  // (the two lines' segments cross: each one's ends lie on either side of the other)
                    const V3 &p = c[k], &r = c[k + 1];
                    auto side = [](const V3& u, const V3& v, const V3& w) { return (v.x - u.x) * (w.z - u.z) - (v.z - u.z) * (w.x - u.x); };
                    if (side(a, b, p) * side(a, b, r) > 0 || side(p, r, a) * side(p, r, b) > 0) continue;
                    for (int m = std::max(0, k - 26); m <= std::min(n - 1, k + 26); m++) soft[m] = std::max(soft[m], smoothstep(26, 12, std::fabs((double)(m - k))));
                }
            }
        }
    };
    auto smoothLine = [](std::vector<double>& v, int passes) {
        for (int pass = 0; pass < passes; pass++) {
            double prev = v[0];
            for (size_t k = 1; k + 1 < v.size(); k++) {
                const double here = v[k];
                v[k] = 0.25 * prev + 0.5 * here + 0.25 * v[k + 1], prev = here;
            }
        }
    };
    // (A level only ever falling, source to outlet, a whisper a metre at the least.)
    auto falling = [](std::vector<double>& v) {
        for (size_t k = 1; k < v.size(); k++) v[k] = std::min(v[k], v[k - 1] - 0.003);
    };
    std::vector<Channel> channels;

    // Copper Creek: down the valley under Copper Peak, from a spring at its head to a pond at the valley's low west end. The valley road
    // shares the floor with it, so it keeps to one side of the road, the road crossing it at two fords; the pass wades it between them.
    if (passRow >= 0) {
        // Its line: the valley's floor (the lowest ground across it, every 20 m), smoothed, flowing west.
        const V3 pondAt(-368, 0, 886);
        std::vector<V3> ctl;
        for (double x = 430; x >= -330; x -= 20) {
            double bz = 880, bh = 1e9;
            for (double z = 820; z <= 960; z += 4) {
                const double h = height(x, z);
                if (h < bh) bh = h, bz = z;
            }
            ctl.push_back(V3(x, 0, bz));
        }
        ctl.push_back(pondAt);
        for (int pass = 0; pass < 3; pass++)
            for (size_t k = 1; k + 1 < ctl.size(); k++) ctl[k].z = 0.25 * ctl[k - 1].z + 0.5 * ctl[k].z + 0.25 * ctl[k + 1].z;
        Poly c = resample(splinePoints(ctl, false), 1.0);
        // Beside the road, kClear from its middle to the creek's, south of it from one ford to the other, north of it beyond them.
        const int road = rowOf("COPPER VALLEY ROAD");
        if (road >= 0) {
            constexpr double kClear = 9.5, kFordW = -265, kFordE = 277;
            const Poly& rd = Ls[road];
            auto roadZ = [&](double x) {
                for (size_t q = 0; q + 1 < rd.size(); q++)
                    if ((rd[q].x - x) * (rd[q + 1].x - x) <= 0 && rd[q].z > 840 && rd[q].z < 960)
                        return rd[q].z + (rd[q + 1].z - rd[q].z) * (x - rd[q].x) / (std::fabs(rd[q + 1].x - rd[q].x) < 1e-6 ? 1.0 : rd[q + 1].x - rd[q].x);
                return 1e9;
            };
            auto hold = [&](V3& p) {
                const double rz = roadZ(p.x);
                if (rz > 1e8) return;
                if (std::fabs(p.x - kFordW) < 24 || std::fabs(p.x - kFordE) < 24) return;
                p.z = p.x > kFordW && p.x < kFordE ? std::max(p.z, rz + kClear) : std::min(p.z, rz - kClear);
            };
            std::vector<double> z(c.size());
            for (int pass = 0; pass < 30; pass++) {
                for (V3& p : c) hold(p);
                for (size_t k = 0; k < c.size(); k++) z[k] = c[k].z;
                smoothLine(z, 8);
                for (size_t k = 0; k < c.size(); k++) c[k].z = z[k];
            }
            for (V3& p : c) hold(p);
            for (size_t k = 0; k < c.size(); k++) z[k] = c[k].z;
            smoothLine(z, 3);
            for (size_t k = 0; k < c.size(); k++) c[k].z = z[k];
            c = resample(c, 1.0);
        }
        const int n = (int)c.size();
        Channel ch;
        ch.name = "COPPER CREEK", ch.c = c;
        // The pond's level: under the land round it. The creek's: under the land along it and under the road beside it.
        double rim = 1e9;
        for (double a = 0; a < 6.28; a += 0.2) rim = std::min(rim, height(pondAt.x + 34 * dm::cos(a), pondAt.z + 34 * dm::sin(a)));
        ch.pond = V3(pondAt.x, rim - 0.5, pondAt.z), ch.pondR = 24;
        ch.lvl.resize(n), ch.hw.resize(n), ch.dep.resize(n), ch.soft.assign(n, 0.0);
        for (int k = 0; k < n; k++) {
            ch.lvl[k] = height(c[k].x, c[k].z) - 0.35;
            if (road >= 0) {
                const V3& q = Ls[road][nearestIdx(Ls[road], c[k].x, c[k].z)];
                if (dm::hypot(q.x - c[k].x, q.z - c[k].z) < 16) ch.lvl[k] = std::min(ch.lvl[k], height(q.x, q.z) - 0.4);
            }
        }
        smoothLine(ch.lvl, 20);
        falling(ch.lvl);
        for (int k = n - 1; k >= 0; k--) ch.lvl[k] = std::max(ch.lvl[k], ch.pond.y + 0.003 * (n - 1 - k));
        markFords(c, 1, ch.soft);
        for (int k = 0; k < n; k++) {
            const double s = (double)k / (n - 1);  // (a spring at its head, widening down the valley)
            ch.hw[k] = (0.8 + 1.6 * smoothstep(0, 0.6, s)) * (1 + 0.25 * ch.soft[k]);
            ch.dep[k] = (0.1 + 0.32 * smoothstep(0, 0.4, s)) * (1 - 0.3 * ch.soft[k]);
        }
        channels.push_back(std::move(ch));
    }

    // The river through ROCKY RIVER: from a spring on the rise east of it, across the section (straight over its channel, the trail
    // wading it), then west round the foot of the hill sections and south through the knobs in a gorge, into Elk River.
    SectionFrame rockyFrame;
    int rockyPiece = -1;
    size_t rockySection = 0;
    for (size_t pi = 0; pi < bcSetPieces().size(); pi++) {
        const BcSetPiece& sp = bcSetPieces()[pi];
        double along = 0;
        const V3 dir(dm::cos(sp.heading), 0, -dm::sin(sp.heading));
        for (size_t k = 0; k < sp.types.size(); k++) {
            const SectionFootprint fp = sectionFootprint((SectionType)sp.types[k]);
            if (sp.types[k] == SEC_ROCKY_RIVER) {
                rockyPiece = (int)pi, rockySection = k;
                rockyFrame.entry = sp.entry + dir * (along + fp.apron);
                rockyFrame.heading = sp.heading;
            }
            along += fp.length + 2 * fp.apron + kSetPieceGap;
        }
    }
    if (rockyPiece >= 0) {
        rockyFrame.entry.y = height(rockyFrame.entry.x, rockyFrame.entry.z);
        const V3 dirS(dm::cos(rockyFrame.heading), 0, -dm::sin(rockyFrame.heading));
        const V3 F = rockyFrame.entry + dirS * sectionRiverAt();  // (the ford: its line runs square across the section)
        std::vector<V3> ctl = {V3(F.x + 205, 0, F.z - 40), V3(F.x + 150, 0, F.z - 22), V3(F.x + 100, 0, F.z - 8), V3(F.x + 56, 0, F.z), V3(F.x + 30, 0, F.z), V3(F.x, 0, F.z), V3(F.x - 30, 0, F.z),
                               V3(F.x - 40, 0, F.z), V3(634, 0, F.z + 8), V3(615, 0, -872), V3(603, 0, -825), V3(592, 0, -770), V3(575, 0, -700), V3(556, 0, -640), V3(556, 0, -596), V3(566, 0, -540),
                               V3(575, 0, -480), V3(572, 0, -420), V3(590, 0, -350), V3(574, 0, -280), V3(592, 0, -200), V3(574, 0, -120), V3(586, 0, -40)};
        ctl.push_back(V3(576, 0, elkRiverZ(576)));
        const Poly c = resample(splinePoints(ctl, false), 1.0);
        const int n = (int)c.size(), kf = nearestIdx(c, F.x, F.z);
        Channel ch;
        ch.name = "ROCKY RIVER", ch.c = c, ch.speed = 1.2, ch.shelfK = kf;
        ch.lvl.resize(n), ch.hw.resize(n), ch.dep.resize(n), ch.soft.assign(n, 0.0);
        constexpr double kFordDepth = 1.05;  // (the section's channel under its plane)
        const double fordL = sectionPlaneAt(*this, SEC_ROCKY_RIVER, rockyFrame, sectionRiverAt()) - kFordDepth + 0.55;
        const double mouth = waterLevel(c[n - 1].x, c[n - 1].z);
        for (int k = 0; k < n; k++) ch.lvl[k] = height(c[k].x, c[k].z) - 0.7;
        smoothLine(ch.lvl, 12);
        for (int k = 0; k < n; k++) ch.lvl[k] = k < kf ? std::max(ch.lvl[k], fordL + 0.003 * (kf - k)) : k <= kf + 40 ? fordL - 0.003 * (k - kf) : std::min(ch.lvl[k], fordL - 0.003 * (k - kf));
        falling(ch.lvl);
        for (int k = n - 1; k > kf + 40; k--) ch.lvl[k] = std::max(ch.lvl[k], mouth + 0.003 * (n - 1 - k));
        {
            std::vector<double> tail(ch.lvl.begin() + kf + 40, ch.lvl.end());
            smoothLine(tail, 30);
            std::copy(tail.begin(), tail.end(), ch.lvl.begin() + kf + 40);
        }
        markFords(c, 2, ch.soft);
        for (int k = std::max(0, kf - 40); k <= std::min(n - 1, kf + 40); k++) ch.soft[k] = 0;  // (the section's own ford)
        for (int k = 0; k < n; k++) {
            const double s = (double)k / (n - 1), wide = 1.2 + 5.8 * std::sqrt(s);
            ch.hw[k] = wide * (1 + 0.25 * ch.soft[k]);
            ch.dep[k] = (0.25 + 0.5 * smoothstep(0, 0.5, s)) * (1 - 0.45 * ch.soft[k]);
            ch.dep[k] += (0.55 - ch.dep[k]) * smoothstep(60, 36, std::abs(k - kf));
        }
        rockyFrame.riverWidth = 2 * ch.hw[kf], rockyFrame.riverDepth = kFordDepth;
        channels.push_back(std::move(ch));
    }
    for (const Channel& ch : channels) cutChannel(ch);

    // The places' yards (places.h: levelled before the trails), which a trail runs into but doesn't change.
    std::vector<std::pair<V3, double>> pads;
    for (const MapPlace& pl : mapPlaces(MAP_BACKCOUNTRY))
        if (pl.pad > 0) pads.push_back({pl.p, pl.pad + 1.5});
    auto onPad = [&](int id) {
        const double x = X(id / hN), z = X(id % hN);
        for (const auto& p : pads)
            if (std::fabs(x - p.first.x) < p.second && std::fabs(z - p.first.z) < p.second && dm::hypot(x - p.first.x, z - p.first.z) < p.second) return true;
        return false;
    };
    // ---- Each trail: its profile (the land smoothed, cut and filled to its grade, eased into the places it joins and down through the
    // fords), its tread laid and painted ----------------------------------------------------------------------------------------------
    std::vector<std::vector<double>> profiles(R.size());
    std::vector<int> lowSide;  // the shelf: which side the drop is (+1 left)
    for (size_t t = 0; t < R.size(); t++) {
        const Poly& pts = Ls[t];
        const int n = (int)pts.size();
        if (n < 4) continue;
        const int rating = R[t].rating;
        std::vector<double> raw(n), y(n);
        std::vector<char> pinned(n, 0);
        for (int k = 0; k < n; k++) raw[k] = height(pts[k].x, pts[k].z);
        const int window = rating == 0 ? 10 : rating == 1 ? 6 : rating == 4 ? 2 : 3;
        for (int k = 0; k < n; k++) {
            double sum = 0;
            int cnt = 0;
            for (int q = std::max(0, k - window); q <= std::min(n - 1, k + window); q++) sum += raw[q], cnt++;
            y[k] = sum / cnt;
        }
        // Pins: both ends (on the ground they meet), and the bed wherever it wades a river.
        std::vector<int> pins = {0, n - 1};
        for (int k = 0; k < n; k++)
            if (waterLevel(pts[k].x, pts[k].z) > raw[k] + 0.05 || edge[cellIndex(pts[k].x, pts[k].z)] < 0.3) pins.push_back(k), pinned[k] = 1;  // (and across an earlier trail's tread, at its height)
        pinned[0] = pinned[n - 1] = 1;
        for (int p : pins) {
            const double off = raw[p] - y[p];
            if (std::fabs(off) < 1e-9) continue;
            for (int k = std::max(0, p - 30); k <= std::min(n - 1, p + 30); k++)
                if (!pinned[k]) y[k] += off * smoothstep(30, 0, std::fabs((double)(k - p)));
            y[p] = raw[p];
        }
        // The Shelf: it runs along the foot of the Rimrock, so it climbs on a ledge built up against the cliff, from the foot to the top.
        if ((int)t == passRow) {
            const double y0 = y[P.shelf0], ang = dm::atan2(1281.0 - 1550.0, 82.0 - 300.0);
            const double yTop = height(300 + 334 * dm::cos(ang), 1550 + 334 * dm::sin(ang));
            for (int k = P.shelf0; k <= P.shelf1; k++) y[k] = y0 + (yTop - y0) * (k - P.shelf0) / (double)(P.shelf1 - P.shelf0), pinned[k] = 1;
        }
        // (Its grade: the rating's, but a switchback's turn is a landing, near level, the legs between taking the climb.)
        std::vector<double> gk(n, R[t].maxGrade * 1.12);
        if ((int)t == passRow)
            for (int hp : P.hairpins)
                for (int k = std::max(0, hp - 16); k <= std::min(n - 1, hp + 16); k++) gk[k] = 0.08;
        if ((int)t == passRow)  // (and the Notch's ledges stand on benches, as the Creek Crawl's do)
            for (int s0 : P.ledges)
                for (int k = std::max(0, s0 - 8); k <= std::min(n - 1, s0 + 8); k++) gk[k] = 0.06;
        for (int pass = 0; pass < 12; pass++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < n; q++) {
                    const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                    if (pinned[k]) continue;
                    const double run = dm::hypot(pts[k].x - pts[prev].x, pts[k].z - pts[prev].z), g = std::min(gk[k], gk[prev]);
                    y[k] = clampd(y[k], y[prev] - g * run, y[prev] + g * run);
                }
        // The pass's ledges: a square rise (its lip a row of rock, below), a flat on top, and easing back down to the line.
        if ((int)t == passRow)
            for (size_t l = 0; l < P.ledges.size(); l++) {
                const double s0 = P.ledges[l], h = 0.27 + 0.05 * l;
                for (int k = std::max(0, (int)s0 - 1); k < n; k++) {
                    const double u = k - s0;
                    const double rise = clampd(u / 1.5, 0, 1) * (1 - smoothstep(4, 4 + h / 0.06, u));
                    y[k] += h * rise;
                    if (u > 4 + h / 0.06) break;
                }
            }
        profiles[t] = y;
        // The tread: green a two-track (grass between the wheel tracks), blue a dirt track, black and red rougher; the banks ease into
        // the land over `blend`, further where it's cut deep or built up (no cliffs beside a trail, but on the shelf).
        const double halfW = rating == 0 ? 3.1 : rating == 1 ? 2.8 : rating == 4 ? 1.6 : 2.6;
        const double baseBlend = rating == 0 ? 12 : rating == 1 ? 10 : rating == 4 ? 3 : 7;
        std::vector<double> blend(n);
        for (int k = 0; k < n; k++) blend[k] = std::max(baseBlend, 2.4 * std::fabs(y[k] - raw[k]));
        for (int pass = 0; pass < 2; pass++)
            for (int k = 1; k < n; k++) blend[k] = std::max(blend[k], blend[k - 1] - 0.6), blend[n - 1 - k] = std::max(blend[n - 1 - k], blend[n - k] - 0.6);
        double maxBlend = 0;
        for (double b : blend) maxBlend = std::max(maxBlend, b);
        const bool isPass = (int)t == passRow;
        int drop = 0;
        std::vector<double> lowK;  // (the shelf: the land at the foot of the drop)
        if (isPass) {  // (the shelf: which side the land falls away)
            double sum = 0;
            for (int k = P.shelf0; k <= P.shelf1; k += 4) {
                const V3 p = pts[k], l = leftAlong(pts, k);
                sum += height(p.x + l.x * 8, p.z + l.z * 8) - height(p.x - l.x * 8, p.z - l.z * 8);
            }
            drop = sum < 0 ? 1 : -1;
            lowSide.push_back(drop);
            lowK.assign(n, 0.0);
            for (int k = 0; k < n; k++) {
                const V3 p = pts[k], l = leftAlong(pts, k) * (double)drop;
                double lo = 1e9;
                for (double o = 6; o <= 16; o += 2) lo = std::min(lo, height(p.x + l.x * o, p.z + l.z * o));
                lowK[k] = lo;
            }
            for (int pass = 0; pass < 6; pass++)
                for (int k = 1; k + 1 < n; k++) lowK[k] = std::min(lowK[k], 0.25 * lowK[k - 1] + 0.5 * lowK[k] + 0.25 * lowK[k + 1]);
            maxBlend = std::max(maxBlend, 16.5);
            if (std::getenv("BC_DEBUG"))
                for (int k = P.shelf0 - 40; k <= P.shelf1 + 20; k += 8) {
                    const V3 p = pts[k], l = leftAlong(pts, k);
                    std::fprintf(stderr, "shelf k %d (%.0f,%.0f) y %.1f raw %.1f  left8 %.1f right8 %.1f\n", k, p.x, p.z, y[k], raw[k], height(p.x + l.x * 8, p.z + l.z * 8),
                                 height(p.x - l.x * 8, p.z - l.z * 8));
                }
        }
        scanNear(pts, halfW + maxBlend, [&](int id, double d, double k, double lat) {
            const double e = d - halfW;
            if (edge[id] < e) return;  // (an earlier trail's tread is nearer: left as it is)
            if (onPad(id)) return;     // (and a place's yard stays as level as it was made)
            const int ki = (int)clampd(std::floor(k), 0, n - 1);
            if (e > blend[ki]) return;
            const double ty = yAt(y, k);
            double w = e < 0.5 ? 1.0 : smoothstep(blend[ki], 0.5, e);
            if (isPass && k > P.shelf0 - 6 && k < P.shelf1 + 6) {
                // The shelf: a wall cut on the high side, and on the low side the land left to fall away from the tread's edge.
                if (lat * drop > 0 && e > 0.3) {
                    // (The drop: from the tread's edge straight down to the foot of the cliff, flat there a little way, then the land.)
                    if (e > 16) return;
                    const double fade = smoothstep(P.shelf0 - 6, P.shelf0 + 12, k) * smoothstep(P.shelf1 + 6, P.shelf1 - 12, k);
                    const double target = ty - (ty - std::min(ty, yAt(lowK, k))) * smoothstep(0.3, 1.6, e) * fade;
                    if (hgrid[id] > target) hgrid[id] = (float)(target + (hgrid[id] - target) * smoothstep(9, 16, e));
                    if (e < 12) keep[id] = 1;
                    if (e < 6) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
                    return;
                }
                if (e > 0.3) w = smoothstep(2.5, 0.3, e);
            }
            hgrid[id] = (float)(hgrid[id] + (ty - hgrid[id]) * w);
            edge[id] = (float)e;
            if (e < 3) keep[id] = 1;
            if (e >= 0.5) return;
            roadLat[id] = (float)lat, roadAlong[id] = (float)k, roadNear[id] = 1;
            const double x = X(id / hN), z = X(id % hN);
            const double hh = hgrid[id];
            if (hh > 226 + 10 * (valueNoise(x * 0.05, z * 0.05) - 0.5)) {
                paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            } else if (rating == 4) {  // (a faint two-track: the wheels' ruts worn through the grass, little more)
                if (std::fabs(lat) > 0.45 && std::fabs(lat) < 1.3) paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT;
            } else if (rating == 0) {
                if (std::fabs(lat) < 0.5) paint[id] = PAINT_NONE, surf[id] = SURF_GRASS;  // (the grass down the middle)
                else paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT;
            } else if (rating >= 2 && (valueNoise(x * 0.09 + t, z * 0.09) > 0.62 || hh > 150)) {
                paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
            } else
                paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT;
        });
        // On the map: a point every 8 m at the tread's height.
        MarkedTrail mt;
        mt.name = R[t].name, mt.rating = rating;
        for (int k = 0; k < n; k += 8) mt.pts.push_back(V3(pts[k].x, y[k], pts[k].z));
        mt.pts.push_back(V3(pts[n - 1].x, y[n - 1], pts[n - 1].z));
        trails.push_back(std::move(mt));
    }

    // (Cut again: the trails' earthworks don't fill a channel, or stand over its water.)
    for (const Channel& ch : channels) cutChannel(ch);

    // ---- What grew where the trails now run (the old map's trees, rocks, logs, bushes and stumps were placed before them): gone ----
    {
        std::unordered_set<int64_t> clear;  // 2 m cells within the treads and a little more
        auto key = [](double x, double z) { return (int64_t)std::floor(x / 2 + 5000) * 100000 + (int64_t)std::floor(z / 2 + 5000); };
        for (size_t t = 0; t < R.size(); t++) {
            const Poly& pts = Ls[t];
            const double reach = (R[t].rating == 0 ? 3.1 : 2.8) + 2.5;
            for (size_t k = 0; k < pts.size(); k += 2)
                for (double dx = -reach; dx <= reach; dx += 2)
                    for (double dz = -reach; dz <= reach; dz += 2)
                        if (dx * dx + dz * dz <= reach * reach) clear.insert(key(pts[k].x + dx, pts[k].z + dz));
        }
        for (const Channel& ch : channels)  // (and in the channels)
            for (size_t k = 0; k < ch.c.size(); k += 2) {
                const double reach = ch.hw[k] + 2.5;
                for (double dx = -reach; dx <= reach; dx += 2)
                    for (double dz = -reach; dz <= reach; dz += 2)
                        if (dx * dx + dz * dz <= reach * reach) clear.insert(key(ch.c[k].x + dx, ch.c[k].z + dz));
            }
        auto gone = [&](double x, double z) { return clear.count(key(x, z)) > 0; };
        std::set<std::pair<double, double>> felled;
        trees.erase(std::remove_if(trees.begin(), trees.end(), [&](const Tree& tr) {
                        if (!gone(tr.base.x, tr.base.z)) return false;
                        felled.insert({tr.base.x, tr.base.z});
                        return true;
                    }), trees.end());
        fixtures.erase(std::remove_if(fixtures.begin(), fixtures.end(), [&](const Prop& f) {
                           if (f.kind != PROP_STUMP || !gone(f.a.x, f.a.z)) return false;
                           felled.insert({f.a.x, f.a.z});
                           return true;
                       }), fixtures.end());
        poles.erase(std::remove_if(poles.begin(), poles.end(), [&](const Pole& p) { return felled.count({p.x, p.z}) > 0; }), poles.end());
        bushes.erase(std::remove_if(bushes.begin(), bushes.end(), [&](const Bush& b) { return gone(b.p.x, b.p.z); }), bushes.end());
        logs.erase(std::remove_if(logs.begin(), logs.end(), [&](const Log& l) { return gone(l.a.x, l.a.z) || gone(l.b.x, l.b.z) || gone((l.a.x + l.b.x) / 2, (l.a.z + l.b.z) / 2); }), logs.end());
        rocks.erase(std::remove_if(rocks.begin(), rocks.end(), [&](const Rock& k) { return k.r < 3 && gone(k.c.x, k.c.z); }), rocks.end());
    }

    // ---- The channels' stones, and their records (now the trails are laid across them) ----
    for (const Channel& ch : channels) {
        const Poly& c = ch.c;
        const int n = (int)c.size();
        // Stones in the bed (low: a crossing is a step down and up, not a wall), bigger ones along the banks.
        for (int k = 4; k < n - 4; k += 3) {
            const V3 p = c[k], l = leftAlong(c, k);
            const double hw = ch.hw[k];
            if (hw < 1 || waterLevel(p.x, p.z) < height(p.x, p.z)) continue;
            if (h01((uint32_t)k, 11) < 0.35 * (1 - ch.soft[k]) && edge[cellIndex(p.x, p.z)] > 3) rock(p + l * ((h01((uint32_t)k, 12) - 0.5) * 1.5 * hw), V3(0.32, 0.2, 0.26) * (0.8 + 0.6 * h01((uint32_t)k, 13)), 0.14, 0.95f);
            if (h01((uint32_t)k, 14) < 0.3) {
                const double side = h01((uint32_t)k, 15) < 0.5 ? -1 : 1, r = 0.7 + 1.1 * h01((uint32_t)k, 16);
                const V3 q = p + l * (side * (hw + 1.6 + r));
                if (edge[cellIndex(q.x, q.z)] > r + 1.5) rock(q, V3(r, r * 0.6, r * 0.8), r * 0.55, 0.9f);
            }
        }
    }

    // ---- COPPER PASS's obstacles -----------------------------------------------------------------------------------------------------
    if (passRow >= 0) {
        const Poly& L = Ls[passRow];
        const std::vector<double>& y = profiles[passRow];
        const int n = (int)L.size();
        const int drop = lowSide.empty() ? 1 : lowSide[0];
        auto groundAt = [&](const V3& p) { return height(p.x, p.z); };
        auto paintTread = [&](int k0, int k1, uint8_t pnt, uint8_t sf, double w = 2.7) {
            scanNear(Poly(L.begin() + std::max(0, k0), L.begin() + std::min(n, k1 + 1)), w, [&](int id, double, double, double) {
                if (paint[id] != PAINT_SNOW) paint[id] = pnt, surf[id] = sf;
            });
        };
        // A rock laid across the line at index s, lateral v, its long axis across, its top at `topY` (world).
        auto laid = [&](double s, double v, V3 r, double topY, double yawJitter, float tint) {
            const V3 p = pointAlong(L, s) + leftAlong(L, s) * v, d = dirAlong(L, s);
            const double top = std::max(0.05, topY - groundAt(p));
            r.y = std::max(r.y, (top + 0.3) / 2);
            RockPose pose;
            pose.yaw = dm::atan2(-d.z, d.x) + M_PI / 2 + yawJitter;  // (its long axis across the trail)
            rock(p, r, top, tint, &pose);
        };
        // THE NOTCH: bare rock, a garden of boulders and four ledges.
        paintTread(P.notch0, P.notch1, PAINT_ROCK, SURF_ROCK, 3.2);
        for (size_t l = 0; l < P.ledges.size(); l++) {
            const double s = P.ledges[l], upper = yAt(y, s + 1.5);  // (the lip: the rocks' tops at the step's top, a square face over the ground before it)
            for (double v = -3.3; v < 3.4;) {
                const double rl = 0.95 + 0.4 * h01((uint32_t)(s * 10 + v * 7), 1), rd = 0.85 + 0.25 * h01((uint32_t)(s * 10 + v * 7), 2);
                laid(s + 0.35, v + rl * 0.8, V3(rl, 0.4, rd), upper + 0.03 + 0.06 * h01((uint32_t)(s * 10 + v * 7), 3), 0.25 * (h01((uint32_t)(s * 10 + v * 7), 4) - 0.5),
                     (float)(0.92 + 0.12 * h01((uint32_t)(s * 10 + v * 7), 5)));
                v += rl * (1.25 + 0.2 * h01((uint32_t)(s * 10 + v * 7), 6));
            }
            for (int side = -1; side <= 1; side += 2) {  // big ones where the ledge meets the crags
                const double r = 1.5 + 0.8 * h01((uint32_t)s, 20 + side);
                const V3 p = pointAlong(L, s + 1) + leftAlong(L, s + 1) * (side * (3.6 + r * 0.7));
                rock(p, V3(r, r * 0.75, r * 0.9), r * 0.8, 0.9f);
            }
        }
        for (int k = P.notch0 + 4; k < P.notch1 - 2; k++) {  // the garden, off the ledges
            bool nearLedge = false;
            for (int s : P.ledges) nearLedge |= k > s - 2 && k < s + 5;
            if (nearLedge || h01((uint32_t)k, 30) > 0.45) continue;
            const double r = 0.26 + 0.22 * h01((uint32_t)k, 31);
            rock(pointAlong(L, k) + leftAlong(L, k) * ((h01((uint32_t)k, 32) - 0.5) * 4.6), V3(r, r * 0.7, r * 0.85), std::min(0.3, r * 0.65), (float)(0.9 + 0.2 * h01((uint32_t)k, 33)));
        }
        // THE SCREE: the loose climb out of the creek.
        paintTread(P.scree0, P.scree1, PAINT_SCREE, SURF_SCREE, 3.0);
        for (int k = P.scree0; k < P.scree1; k++)
            if (h01((uint32_t)k, 40) < 0.4) {
                const double r = 0.16 + 0.16 * h01((uint32_t)k, 41);
                rock(pointAlong(L, k) + leftAlong(L, k) * ((h01((uint32_t)k, 42) - 0.5) * 5.0), V3(r, r * 0.7, r * 0.9), r * 0.6, 1.05f);
            }
        // THE SWITCHBACKS: a boulder inside each hairpin (no cutting the corner), a marker post on its outside.
        for (int hp : P.hairpins) {
            const V3 c = pointAlong(L, hp);
            // (the hairpin's middle: the inside is toward the points 12 m either way along)
            const V3 in = (pointAlong(L, hp - 12) + pointAlong(L, hp + 12)) * 0.5;
            const V3 toIn = norm(V3(in.x - c.x, 0, in.z - c.z));
            const double r = 1.3 + 0.4 * h01((uint32_t)hp, 50);  // (in the turn's middle: 4.5 m clear of its inside edge)
            rock(c + toIn * 9.0, V3(r, r * 0.8, r), r * 1.0, 0.9f);
            const V3 out = c - toIn * 4.2;
            fixtures.push_back({PROP_MARKER, V3(out.x, height(out.x, out.z), out.z), V3(), 0});
        }
        // THE CHOKE: two boulders with a truck's width (2.4 m) between them, and a rock step in the gap.
        {
            const double s = P.choke;
            for (int side = -1; side <= 1; side += 2) {
                const double r = 2.3 + 0.4 * (side > 0);
                RockPose pose;
                const V3 d = dirAlong(L, s);
                pose.yaw = dm::atan2(-d.z, d.x);
                rock(pointAlong(L, s) + leftAlong(L, s) * (side * (1.2 + r * 0.85)), V3(r, r * 0.85, r * 0.85), r * 1.35, 0.88f, &pose);
            }
            laid(s - 2.5, -0.7, V3(0.9, 0.35, 0.8), yAt(y, s - 2.5) + 0.46, 0.1, 0.95f);
            laid(s - 2.5, 0.8, V3(0.9, 0.35, 0.8), yAt(y, s - 2.5) + 0.42, -0.1, 0.95f);
            laid(s + 3.5, 0.0, V3(1.2, 0.35, 0.8), yAt(y, s + 3.5) + 0.4, 0.05, 0.95f);  // (and a second step up, between the boulders' tails)
        }
        // THE SHELF: bare rock, the cut wall lined with fallen rock, posts on the edge of the drop; THE SLAB jutting out over it.
        paintTread(P.shelf0 - 6, P.shelf1 + 6, PAINT_ROCK, SURF_ROCK, 2.9);
        scanNear(Poly(L.begin() + P.shelf0 - 8, L.begin() + P.shelf1 + 8), 30, [&](int id, double d, double, double lat) {
            if (d > 2.7 && (lat * drop < 0 ? d < 12 : d < 30) && paint[id] != PAINT_SNOW) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK, keep[id] = 1;
        });
        for (int k = P.shelf0; k < P.shelf1; k += 3) {
            const V3 p = pointAlong(L, k), l = leftAlong(L, k);
            if (std::abs(k - P.slab) > 14 && (k / 3) % 4 == 0) {
                const V3 q = p + l * (drop * 3.0);
                fixtures.push_back({PROP_MARKER, V3(q.x, yAt(y, k), q.z), V3(), 0});
            }
            if (h01((uint32_t)k, 60) < 0.5) {
                const double r = 0.6 + 1.0 * h01((uint32_t)k, 61);
                const V3 q = p - l * (drop * (2.7 + r * 0.9));
                rock(q, V3(r, r * 0.7, r * 0.8), r * 0.6, 0.85f);
            }
        }
        {
            const double s = P.slab;
            const V3 d = dirAlong(L, s), l = leftAlong(L, s), p = pointAlong(L, s) + l * (drop * 1.8);
            RockPose pose;
            pose.yaw = dm::atan2(-d.z, d.x);
            pose.pitch = dm::atan((yAt(y, s + 6) - yAt(y, s - 6)) / 12);
            const double topY = yAt(y, s) + 0.3;  // (a lip up onto it)
            rock(p, V3(9.5, 1.5, 4.0), std::max(0.1, topY - height(p.x, p.z)), 0.92f, &pose);
        }
        // Tags over the obstacles (shown near), and cairns.
        auto tag = [&](double s, const char* text) {
            const V3 p = pointAlong(L, s);
            tags.push_back({text, V3(p.x, yAt(y, s) + 3.2, p.z)});
        };
        tag(P.notch0 - 4, "THE NOTCH");
        tag(P.creek - 8, "COPPER CREEK");
        tag(P.scree0, "THE SCREE");
        tag(P.hairpins.front() - 6, "THE SWITCHBACKS");
        tag(P.choke - 8, "THE CHOKE");
        tag(P.shelf0, "THE SHELF");
        tag(P.slab - 10, "THE SLAB");
        tag(P.saddle, "COPPER PASS  205 M");
        for (int k = 30; k < n - 10; k += 60) {
            const V3 p = pointAlong(L, k) + leftAlong(L, k) * (-drop * 4.2);
            if (std::abs(k - P.slab) > 30 && edge[cellIndex(p.x, p.z)] > 1.0) fixtures.push_back({PROP_CAIRN, V3(p.x, height(p.x, p.z), p.z), V3(), h01((uint32_t)k, 70) * 6.28});
        }
        (void)spurRow;
    }

    // ---- Signs: each trail's rating at both ends (its name over it), marker posts along the easy ones, cairns along the hard ---------
    static const V3 kCol[4] = {{0.1, 0.55, 0.2}, {0.1, 0.3, 0.8}, {0.03, 0.03, 0.03}, {0.75, 0.08, 0.06}};
    static const char* kRating[4] = {"GREEN", "BLUE", "BLACK", "RED"};
    for (size_t t = 0; t < R.size(); t++) {
        const Poly& pts = Ls[t];
        const int n = (int)pts.size(), rating = std::min(3, R[t].rating);
        if (n < 20) continue;
        if (R[t].rating == 4) {
            for (int end = 0; end < 2; end++) {
                const double s = end ? n - 5.0 : 4.0;
                const V3 q = pointAlong(pts, s) + leftAlong(pts, s) * 2.6;
                if (edge[cellIndex(q.x, q.z)] > 0.8) fixtures.push_back({PROP_CAIRN, V3(q.x, height(q.x, q.z), q.z), V3(), h01((uint32_t)t, 80 + end) * 6.28});
            }
            continue;
        }
        char text[96];
        std::snprintf(text, sizeof text, "%s  (%s, %.1f KM)", R[t].name, kRating[rating], n / 1000.0);
        for (int end = 0; end < 2; end++) {
            const double s = end ? n - 8.0 : 7.0;
            const V3 p = pointAlong(pts, s), d = dirAlong(pts, s) * (end ? -1.0 : 1.0), l(-d.z, 0, d.x);
            const double side = R[t].rating == 0 ? 5.0 : 4.6;
            V3 q = p + l * side;
            if (edge[cellIndex(q.x, q.z)] < 1.0) q = p - l * side;  // (not in another trail's tread)
            Prop sg{PROP_SIGN, V3(q.x, height(q.x, q.z), q.z), V3(), dm::atan2(d.z, -d.x)};
            sg.col[0] = (float)kCol[rating].x, sg.col[1] = (float)kCol[rating].y, sg.col[2] = (float)kCol[rating].z;
            fixtures.push_back(sg);
            poles.push_back({q.x, q.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
            tags.push_back({text, V3(q.x, sg.a.y + 2.7, q.z)});
        }
        for (int k = 60; k < n - 40; k += rating <= 1 ? 110 : 70) {
            const V3 p = pointAlong(pts, k), l = leftAlong(pts, k);
            const double side = ((k / 10) % 2 ? 1 : -1) * ((R[t].rating == 0 ? 3.1 : 2.8) + 1.3);
            const V3 q = p + l * side;
            if (edge[cellIndex(q.x, q.z)] < 0.8 || waterLevel(q.x, q.z) > height(q.x, q.z)) continue;
            fixtures.push_back({rating <= 1 ? PROP_MARKER : PROP_CAIRN, V3(q.x, height(q.x, q.z), q.z), V3(), h01((uint32_t)k, (uint32_t)t) * 6.28});
        }
    }

    // ---- The obstacle trails (bcSetPieces): their sections stamped on in a row, the ground they take kept clear of the new country's
    // growth, a red sign and their name at each end ------------------------------------------------------------------------------------
    for (size_t pi = 0; pi < bcSetPieces().size(); pi++) {
        const BcSetPiece& sp = bcSetPieces()[pi];
        const V3 dir(dm::cos(sp.heading), 0, -dm::sin(sp.heading)), left(-dir.z, 0, dir.x);
        double along = 0;
        for (size_t k = 0; k < sp.types.size(); k++) {
            const SectionType t = (SectionType)sp.types[k];
            const SectionFootprint fp = sectionFootprint(t);
            SectionFrame f;
            f.entry = sp.entry + dir * (along + fp.apron);
            f.entry.y = height(f.entry.x, f.entry.z);
            f.heading = sp.heading;
            f.keepCountry = true;
            if ((int)pi == rockyPiece && k == rockySection) f.riverWidth = rockyFrame.riverWidth, f.riverDepth = rockyFrame.riverDepth;
            buildSection(*this, t, (uint32_t)(11 + pi * 7 + k), f);
            along += fp.length + 2 * fp.apron + kSetPieceGap;
        }
        // (Its ground, and the gaps between: no growth from the new country on it.)
        const double L = dm::hypot(sp.exit.x - sp.entry.x, sp.exit.z - sp.entry.z), hw = 34 + 26;
        for (double a = -26; a <= L + 26; a += 1)
            for (double b = -hw; b <= hw; b += 1) {
                const V3 q = sp.entry + dir * a + left * b;
                const int id = cellIndex(q.x, q.z);
                if (id >= 0) keep[id] = 1;
            }
        static const V3 red(0.75, 0.08, 0.06);
        for (int end = 0; end < 2; end++) {
            const V3 p = end ? sp.exit + dir * 6.0 + left * 5.0 : sp.entry - dir * 6.0 + left * 5.0;
            Prop sg{PROP_SIGN, V3(p.x, height(p.x, p.z), p.z), V3(), end ? dm::atan2(-dir.z, dir.x) : dm::atan2(dir.z, -dir.x)};
            sg.col[0] = (float)red.x, sg.col[1] = (float)red.y, sg.col[2] = (float)red.z;
            fixtures.push_back(sg);
            poles.push_back({p.x, p.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
            char text[96];
            std::snprintf(text, sizeof text, "%s  (RED, %d SECTIONS)", sp.name, (int)sp.types.size());
            tags.push_back({text, V3(p.x, sg.a.y + 2.7, p.z)});
        }
        MarkedTrail mt;
        mt.name = sp.name, mt.rating = 3;
        for (double a = 0; a <= L; a += 8) {
            const V3 q = sp.entry + dir * a;
            mt.pts.push_back(V3(q.x, height(q.x, q.z), q.z));
        }
        trails.push_back(std::move(mt));
    }

    // ---- The channels, last: ROCKY RIVER's water over the section stamped on its way, and their records (the sounds and the M map know them).
    for (const Channel& ch : channels) {
        cutChannel(ch);
        River rv;
        rv.name = ch.name;
        for (size_t k = 0; k < ch.c.size(); k += 6) {
            const V3& p = ch.c[k];
            const double lv = waterLevel(p.x, p.z), dep = lv - height(p.x, p.z);
            if (dep < 0.05) continue;  // (its spring)
            RiverSpot sp;
            sp.p = V3(p.x, lv, p.z), sp.depth = (float)dep, sp.halfWidth = (float)ch.hw[k], sp.speed = (float)ch.speed;
            sp.cls = (uint8_t)riverClassForDepth(dep), sp.bed = (uint8_t)BED_GRAVEL;
            rv.spots.push_back(sp);
        }
        rivers.push_back(std::move(rv));
    }
}

// ================================================================================================================================
// After the new country's growth: rock features and the fill between, the trail network's places, all from hashes of where they are.
void World::dressBackcountryTrails(CellTiles<uint8_t>& keep, CellTiles<float>& edge) {
    loadprof::Scope lp("bc.trail dress");
    (void)edge;
    // Every place (a cache, a photo spot, a crossing) is kept clear of the fill: 10 m round it.
    for (const MapPlace& pl : mapPlaces(MAP_BACKCOUNTRY))
        for (double dx = -10; dx <= 10; dx += 1)
            for (double dz = -10; dz <= 10; dz += 1) {
                const int id = dx * dx + dz * dz <= 100 ? cellIndex(pl.p.x + dx, pl.p.z + dz) : -1;
                if (id >= 0) keep[id] = 1;
            }
    // Where trees stand now (3 m cells), so nothing is put down in a trunk.
    std::unordered_set<int64_t> treeAt;
    auto key3 = [](double x, double z) { return (int64_t)std::floor(x / 3 + 5000) * 100000 + (int64_t)std::floor(z / 3 + 5000); };
    for (const Tree& t : trees)
        if (std::max(std::fabs(t.base.x), std::fabs(t.base.z)) < 2700) treeAt.insert(key3(t.base.x, t.base.z));
    auto treeNear = [&](double x, double z, double r) {
        for (double dx = -r; dx <= r + 2.9; dx += 3)
            for (double dz = -r; dz <= r + 2.9; dz += 3)
                if (treeAt.count(key3(x + dx, z + dz))) return true;
        return false;
    };
    auto open = [&](double x, double z, double r) {  // dry, not kept clear (a trail, a place, a river's banks), off the map's old middle
        if (std::max(std::fabs(x), std::fabs(z)) < 560 || std::max(std::fabs(x), std::fabs(z)) > 2600) return false;
        const int id = cellIndex(x, z);
        if (id < 0 || keep[id] || waterLevel(x, z) > height(x, z) - 0.3) return false;
        for (int a = 0; a < 4; a++) {
            const double ang = a * M_PI / 2;
            const int q = cellIndex(x + r * dm::cos(ang), z + r * dm::sin(ang));
            if (q < 0 || keep[q]) return false;
        }
        return true;
    };
    auto slopeAt = [&](double x, double z) { return dm::hypot(height(x + 1, z) - height(x - 1, z), height(x, z + 1) - height(x, z - 1)) / 2; };
    uint32_t seed = 4000000;
    auto rock = [&](double x, double z, const V3& r, double top, float tint, const RockPose* pose = nullptr) -> Rock& {
        return addRock(x, z, r, top, seed++, tint, pose);
    };
    // A rock raised (or lowered) whole: its hull, its frame (balanced on another).
    auto lift = [](Rock& k, double dy) {
        for (V3& p : k.v) p.y += dy;
        k.c.y += dy, k.at.y += dy, k.origin.y += dy;
        for (size_t i = 0; i < k.fd.size(); i++) k.fd[i] += k.fn[i].y * dy;
    };
    auto paintRock = [&](double x, double z, double r) {
        for (double dx = -r; dx <= r; dx += 1)
            for (double dz = -r; dz <= r; dz += 1) {
                if (dx * dx + dz * dz > r * r) continue;
                const int id = cellIndex(x + dx, z + dz);
                if (id >= 0 && paint[id] == PAINT_NONE && waterLevel(x + dx, z + dz) < -1e8) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
            }
    };

    // ---- The open country filled, 64 m cell by cell: big boulders (car-sized and more) with smaller ones round them, outcrops and tors,
    // slabs to drive up, fields of boulders, little heaps of rock, clumps of bushes, lone trees and copses in the open, fallen logs ----
    {
        constexpr double C = 64;
        const double H = kOldBackcountryHalf;
        const int cn = (int)std::ceil(2 * H / C);
        int counts[8] = {0};
        for (int ci = 0; ci < cn; ci++)
            for (int cj = 0; cj < cn; cj++) {
                const double x0 = -H + ci * C, z0 = -H + cj * C, cx = x0 + C / 2, cz = z0 + C / 2;
                const double far = std::max(std::fabs(cx), std::fabs(cz));
                if (far < 560 || far > 2580) continue;
                // (Each cell's rocks their own seeds, so one cell changing moves nothing in another; scenery, from kWholeRockSeeds' range (world.h):
                // whole hulls to the physics, for a fraction of the memory. The slabs, made to be driven on, are cut into their pieces.)
                seed = kWholeRockSeeds + 0x800000u + (uint32_t)(ci * cn + cj) * 64u;
                uint32_t r32 = (uint32_t)(ci * 83492791) ^ (uint32_t)(cj * 2971215073u) ^ 0x7a11c0deu;
                r32 = r32 * 2654435761u + 0x6d2b79f5u;
                auto rr = [&]() {
                    r32 ^= r32 << 13, r32 ^= r32 >> 17, r32 ^= r32 << 5;
                    return (r32 & 0xFFFFFF) / double(0x1000000);
                };
                const double s = smoothstep(-450, -1500, cz), e = smoothstep(450, 1500, cx), w = smoothstep(-450, -1500, cx);
                const double dry = s * e, wetW = s * w;
                const double grove = smoothstep(0.38, 0.6, fbmNoise(cx * 0.004 + 7.0, cz * 0.004 - 3.0, 3));
                const double hC = height(cx, cz), high = smoothstep(90, 160, hC);
                const double field = smoothstep(0.63, 0.72, fbmNoise(cx * 0.003 + 11.0, cz * 0.003 - 4.0, 2));
                auto spot = [&](double& x, double& z, double margin) {
                    for (int tries = 0; tries < 6; tries++) {
                        x = x0 + C * rr(), z = z0 + C * rr();
                        if (open(x, z, margin) && !treeNear(x, z, margin)) return true;
                    }
                    return false;
                };
                const float tintBase = (float)(dry > 0.4 ? 1.12 : 0.92 + 0.12 * rr());
                // A big boulder, and a few smaller ones round it.
                if (rr() < 0.16 + 0.2 * grove + 0.15 * high + 0.12 * dry) {
                    double x, z;
                    const double r = 1.9 + 2.4 * dm::pow(rr(), 1.5), flat = 0.62 + 0.25 * rr();
                    if (spot(x, z, r + 1) && slopeAt(x, z) < 0.6) {
                        rock(x, z, V3(r, r * flat, r * (0.75 + 0.2 * rr())), r * flat * (1.15 + 0.35 * rr()), tintBase);
                        counts[0]++;
                        const int sats = 1 + (int)(rr() * 2.5);
                        for (int k = 0; k < sats; k++) {
                            const double a = rr() * 6.283, d = r * (1.2 + 0.9 * rr()), q = 0.35 + 0.45 * rr();  // (under ~0.8: one hull, no pieces)
                            const double sx = x + d * dm::cos(a), sz = z + d * dm::sin(a);
                            if (open(sx, sz, q) && !treeNear(sx, sz, q)) rock(sx, sz, V3(q, q * 0.7, q * 0.85), q * 0.7, tintBase);
                        }
                    }
                }
                // An outcrop: a knot of big angular rock on bare ground (up high, on the steep, in the badlands).
                if (rr() < (0.03 + 0.12 * high + 0.12 * dry + 0.06 * smoothstep(0.2, 0.5, slopeAt(cx, cz)))) {
                    double x, z;
                    if (spot(x, z, 6)) {
                        (void)rr();  // (the land under the fill is left as it is: its paint is the map's)
                        const int m = 4 + (int)(rr() * 3);
                        for (int k = 0; k < m; k++) {
                            const double a = rr() * 6.283, d = 5 * std::sqrt(rr()), q = k < 2 ? 1.4 + 1.0 * rr() : 0.5 + 0.3 * rr();
                            rock(x + d * dm::cos(a), z + d * dm::sin(a), V3(q, q * (0.7 + 0.4 * rr()), q * 0.8), q * (0.9 + 0.6 * rr()), tintBase);
                        }
                        counts[1]++;
                    }
                }
                // Tors: pillars of rock standing up out of the high ground.
                if (hC > 60 && grove < 0.4 && rr() < 0.025 + 0.04 * high) {
                    double x, z;
                    if (spot(x, z, 5)) {
                        const int m = 2 + (int)(rr() * 3);
                        for (int k = 0; k < m; k++) {
                            const double a = rr() * 6.283, d = 3.5 * std::sqrt(rr()), rw = 1.0 + 0.6 * rr(), rh = 2.2 + 1.6 * rr();
                            RockPose pose;
                            pose.yaw = rr() * 6.283, pose.pitch = M_PI / 2 - 0.15 + 0.3 * rr();  // (its long axis standing up)
                            rock(x + d * dm::cos(a), z + d * dm::sin(a), V3(rh, rw, rw * 0.9), rh * 1.6, tintBase, &pose);
                        }
                        counts[2]++;
                    }
                }
                // A slab: a broad flat rock, a little tilted, to drive up onto.
                if (rr() < 0.03 + 0.04 * dry + 0.02 * high) {
                    double x, z;
                    if (spot(x, z, 6) && slopeAt(x, z) < 0.3) {
                        RockPose pose;
                        pose.yaw = rr() * 6.283, pose.pitch = 0.08 + 0.14 * rr();
                        const double L = 3.5 + 2.5 * rr();
                        addRock(x, z, V3(L, 0.9 + 0.3 * rr(), L * (0.6 + 0.2 * rr())), 0.5 + 0.4 * rr(), 5000000u + (uint32_t)(ci * cn + cj), tintBase, &pose);
                        counts[3]++;
                    }
                }
                // A field of boulders strewn over the whole cell.
                if (field > 0.2) {
                    const int m = (int)(field * (3 + 6 * rr()));
                    for (int k = 0; k < m; k++) {
                        double x, z;
                        const double q = 0.3 + 0.5 * dm::pow(rr(), 1.5);
                        if (spot(x, z, q)) rock(x, z, V3(q, q * (0.6 + 0.3 * rr()), q * 0.85), q * (0.6 + 0.4 * rr()), tintBase), counts[4]++;
                    }
                }
                // A little heap of rock.
                if (rr() < 0.3) {
                    double x, z;
                    if (spot(x, z, 2)) {
                        const int m = 3 + (int)(rr() * 2);
                        for (int k = 0; k < m; k++) {
                            const double a = rr() * 6.283, d = 2.5 * std::sqrt(rr()), q = 0.3 + 0.45 * rr();
                            rock(x + d * dm::cos(a), z + d * dm::sin(a), V3(q, q * 0.65, q * 0.85), q * 0.6, tintBase);
                        }
                        counts[5]++;
                    }
                }
                // Clumps of bushes (what grows there), lone trees and copses out in the open, a fallen log.
                const int clumps = 1 + (int)(rr() * 2.0);
                for (int c = 0; c < clumps; c++) {
                    double x, z;
                    if (!spot(x, z, 1)) continue;
                    const int id = cellIndex(x, z);
                    const bool desert = paint[id] == PAINT_DESERT || paint[id] == PAINT_SAND || dry > 0.45;
                    if ((paint[id] != PAINT_NONE && !desert) || height(x, z) > 215) continue;
                    const int habitat = desert ? HAB_DESERT : wetW > 0.5 && height(x, z) < 20 ? HAB_WETLAND : height(x, z) > 120 ? HAB_UPLAND : grove > 0.5 ? HAB_FOREST : HAB_MEADOW;
                    const int m = 3 + (int)(rr() * 4);
                    for (int k = 0; k < m; k++) {
                        const double a = rr() * 6.283, d = 4.5 * std::sqrt(rr()), bx = x + d * dm::cos(a), bz = z + d * dm::sin(a);
                        if (!open(bx, bz, 0.5)) continue;
                        bushes.push_back(growBush(V3(bx, height(bx, bz), bz), habitat, grove > 0.35, rr(), (float)(0.75 + 0.45 * rr())));
                        counts[6]++;
                    }
                }
                if (grove < 0.45 && height(cx, cz) < 200) {
                    const int m = rr() < 0.25 ? 3 + (int)(rr() * 4) : rr() < 0.6 ? 1 : 0;  // (a copse, or a lone tree)
                    double x, z;
                    if (m > 0 && spot(x, z, 4)) {
                        for (int k = 0; k < m; k++) {
                            const double a = rr() * 6.283, d = k ? 4 + 7 * rr() : 0, tx = x + d * dm::cos(a), tz = z + d * dm::sin(a);
                            if (!open(tx, tz, 1.5) || treeNear(tx, tz, 3) || slopeAt(tx, tz) > 0.7) continue;
                            const int id = cellIndex(tx, tz);
                            if (paint[id] != PAINT_NONE && paint[id] != PAINT_DESERT) continue;
                            Tree t;
                            const double q = rr(), h = height(tx, tz);
                            t.kind = q < 0.06 ? 2 : (dry > 0.4 || (h < 45 && q < 0.55)) ? 1 : 0;
                            t.base = V3(tx, h - 0.2, tz);
                            t.height = (t.kind == 0 ? 10 + rr() * 10 : t.kind == 1 ? 8 + rr() * 8 : 6 + rr() * 6) * (1 - 0.3 * smoothstep(120, 200, h));
                            t.radius = t.height * (t.kind == 0 ? 0.22 + rr() * 0.06 : 0.3 + rr() * 0.08);
                            t.tint = (float)(0.8 + rr() * 0.4);
                            plant(t);
                            treeAt.insert(key3(tx, tz));
                            counts[7]++;
                        }
                    }
                }
                if (rr() < 0.15 + 0.3 * grove) {
                    double x, z;
                    if (spot(x, z, 4)) {
                        const double r = 0.18 + 0.2 * rr(), L = 4 + 7 * rr(), a = rr() * 6.283;
                        V3 p0(x - dm::cos(a) * L / 2, 0, z - dm::sin(a) * L / 2), p1(x + dm::cos(a) * L / 2, 0, z + dm::sin(a) * L / 2);
                        if (open(p0.x, p0.z, 0.5) && open(p1.x, p1.z, 0.5)) {
                            p0.y = height(p0.x, p0.z) + r * 0.8, p1.y = height(p1.x, p1.z) + r * 0.8;
                            logs.push_back({p0, p1, r});
                        }
                    }
                }
            }
        if (std::getenv("BC_DEBUG"))
            std::fprintf(stderr, "fill: %d big boulders, %d outcrops, %d tors, %d slabs, %d field rocks, %d heaps, %d bushes, %d trees\n", counts[0], counts[1], counts[2],
                         counts[3], counts[4], counts[5], counts[6], counts[7]);
    }

    // ---- The Rimrock's face (and Copper Peak's crags): bare rock wherever it's cliff ------------------------------------------------
    for (double x = -260; x <= 640; x += 1)
        for (double z = 1100; z <= 1900; z += 1) {
            const int id = cellIndex(x, z);
            if (id < 0 || paint[id] != PAINT_NONE || waterLevel(x, z) > -1e8) continue;
            if (slopeAt(x, z) > 0.75) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
        }

    seed = 4700000;  // (the set pieces' rocks: their own run of seeds, whatever the fill took)
    // ---- The rock set pieces ------------------------------------------------------------------------------------------------------
    auto placeAt = [](const char* id) {
        const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
        return p ? p->p : V3();
    };
    auto keepRound = [&](const V3& c, double r) {
        for (double dx = -r; dx <= r; dx += 1)
            for (double dz = -r; dz <= r; dz += 1)
                if (dx * dx + dz * dz <= r * r) {
                    const int id = cellIndex(c.x + dx, c.z + dz);
                    if (id >= 0) keep[id] = 1;
                }
    };
    // (Anything that grew where a set piece stands goes.)
    auto clearRound = [&](const V3& c, double r) {
        std::set<std::pair<double, double>> felled;
        auto in = [&](double x, double z) { return dm::hypot(x - c.x, z - c.z) < r; };
        trees.erase(std::remove_if(trees.begin(), trees.end(), [&](const Tree& t) {
                        if (!in(t.base.x, t.base.z)) return false;
                        felled.insert({t.base.x, t.base.z});
                        return true;
                    }), trees.end());
        poles.erase(std::remove_if(poles.begin(), poles.end(), [&](const Pole& p) { return felled.count({p.x, p.z}) > 0; }), poles.end());
        bushes.erase(std::remove_if(bushes.begin(), bushes.end(), [&](const Bush& b) { return in(b.p.x, b.p.z); }), bushes.end());
        logs.erase(std::remove_if(logs.begin(), logs.end(), [&](const Log& l) { return in(l.a.x, l.a.z) || in(l.b.x, l.b.z); }), logs.end());
        rocks.erase(std::remove_if(rocks.begin(), rocks.end(), [&](const Rock& k) { return in(k.c.x, k.c.z); }), rocks.end());
        keepRound(c, r);
    };
    // SLAB CITY: a playground of slabs and boulders on bare rock: ramps up onto boulders, slabs leaning on each other, gaps to thread.
    {
        const V3 c = placeAt("BC.SLAB_CITY");
        clearRound(c, 34);
        paintRock(c.x, c.z, 30);
        uint32_t k = 0;
        auto hh = [&](uint32_t salt) { return h01(k * 31 + 7, salt); };
        for (k = 0; k < 9; k++) {  // the big ones
            const double a = k * 2.399 + 0.4, d = 8 + 20 * hh(1), r = 2.6 + 1.8 * hh(2);
            rock(c.x + d * dm::cos(a), c.z + d * dm::sin(a), V3(r, r * (0.7 + 0.2 * hh(3)), r * 0.85), r * (1.1 + 0.4 * hh(4)), 0.95f);
        }
        for (k = 0; k < 7; k++) {  // slabs: tilted ramps, some up against a boulder's side
            const double a = k * 2.0 + 1.1, d = 5 + 18 * hh(5);
            RockPose pose;
            pose.yaw = a + M_PI * hh(6), pose.pitch = 0.12 + 0.25 * hh(7), pose.roll = 0.15 * (hh(8) - 0.5);
            const double L = 4.0 + 3.0 * hh(9);
            rock(c.x + d * dm::cos(a), c.z + d * dm::sin(a), V3(L, 1.0, L * 0.65), 0.6 + 1.2 * hh(10), 1.0f, &pose);
        }
        for (k = 0; k < 30; k++) {  // rubble between
            const double a = hh(11) * 6.283, d = 30 * std::sqrt(hh(12)), q = 0.3 + 0.6 * hh(13);
            rock(c.x + d * dm::cos(a), c.z + d * dm::sin(a), V3(q, q * 0.7, q * 0.85), q * 0.7, 1.0f);
        }
    }
    // BALANCED ROCK: a great boulder on a pillar, beside the valley road.
    {
        const V3 c = placeAt("BC.BALANCED_ROCK");
        clearRound(c, 12);
        paintRock(c.x, c.z, 7);
        RockPose up;
        up.yaw = 0.7, up.pitch = M_PI / 2 - 0.05;
        const Rock& pedestal = rock(c.x, c.z, V3(2.8, 1.5, 1.3), 5.2, 0.9f, &up);
        double topY = -1e9;
        for (const V3& p : pedestal.v) topY = std::max(topY, p.y);
        RockPose lie;
        lie.yaw = 2.1, lie.pitch = 0.06;
        Rock& cap = rock(c.x + 0.3, c.z - 0.2, V3(3.6, 2.0, 2.8), 2.0, 0.95f, &lie);
        double lowY = 1e9;
        for (const V3& p : cap.v) lowY = std::min(lowY, p.y);
        lift(cap, topY - 0.35 - lowY);
        for (int k = 0; k < 6; k++) {
            const double a = k * 1.1, q = 0.6 + 0.5 * h01(k, 90);
            rock(c.x + 5.5 * dm::cos(a), c.z + 5.5 * dm::sin(a), V3(q, q * 0.7, q), q * 0.7, 0.9f);
        }
    }
    // THE TORS: a crown of granite pillars on the uplands, boulders tumbled round them.
    {
        const V3 c = placeAt("BC.THE_TORS");
        clearRound(c, 26);
        paintRock(c.x, c.z, 20);
        for (int k = 0; k < 9; k++) {
            const double a = k * 2.399, d = 4 + 13 * h01(k, 91), rh = 3.0 + 3.5 * h01(k, 92), rw = 1.3 + 0.9 * h01(k, 93);
            RockPose pose;
            pose.yaw = h01(k, 94) * 6.283, pose.pitch = M_PI / 2 - 0.2 + 0.4 * h01(k, 95);
            rock(c.x + d * dm::cos(a), c.z + d * dm::sin(a), V3(rh, rw, rw * 0.85), rh * 1.7, 1.05f, &pose);
        }
        for (int k = 0; k < 24; k++) {
            const double a = h01(k, 96) * 6.283, d = 6 + 18 * h01(k, 97), q = 0.5 + 1.5 * h01(k, 98);
            rock(c.x + d * dm::cos(a), c.z + d * dm::sin(a), V3(q, q * 0.75, q * 0.9), q * 0.8, 1.0f);
        }
    }

    // ---- The places the trails lead to: the fire lookout on Bald Knob, the cabins -----------------------------------------------------
    auto table = [&](const V3& p, double yaw) {
        const double y = height(p.x, p.z);
        fixtures.push_back({PROP_TABLE, V3(p.x, y, p.z), V3(), yaw});
        addBox(V3(p.x, y + 0.4, p.z), V3(0.95, 0.4, 0.8), yaw, 0, 0, V3(0.4, 0.3, 0.2));
        boxes.back().hidden = true;
    };
    auto build = [&](const char* id, const V3& at, double yaw, const V3& half, std::vector<std::pair<std::string, double>> set = {}, int variant = -1) {
        const size_t from = boxes.size();
        addBox(V3(at.x, at.y + half.y, at.z), half, yaw, 0, 0, V3(0.4, 0.3, 0.2));
        standIn(from, id, at, yaw, std::move(set));
        if (variant >= 0 && !models.empty() && models.back().id == id) models.back().variant = variant;
    };
    auto rel = [&](const V3& o, double yaw, double ahead, double left) {
        V3 q(o.x + dm::cos(yaw) * ahead + dm::sin(yaw) * left, 0, o.z - dm::sin(yaw) * ahead + dm::cos(yaw) * left);
        q.y = height(q.x, q.z);
        return q;
    };
    auto fireRing = [&](const V3& c) {
        placeModel("campfire", V3(c.x, height(c.x, c.z), c.z), 0);
        for (int k = 0; k < 11; k++) {
            const double a = k * 2 * M_PI / 11;
            addRock(c.x + 0.95 * dm::cos(a), c.z + 0.95 * dm::sin(a), V3(0.24, 0.17, 0.2), 0.18, 4900 + k, 0.8f);
        }
    };
    {
        // BALD KNOB: a four-legged fire lookout on the knob over Copper Valley, a cabin at its foot.
        const V3 t0 = placeAt("BC.BALD_KNOB"), t = t0 + V3(-4.5, 0, 3.5);  // (the tower to one side of the yard: you drive up into the middle)
        const double y0 = height(t.x, t.z);
        const V3 wood(0.36, 0.30, 0.22), tin(0.45, 0.46, 0.46);
        const size_t from = boxes.size();
        for (int a = -1; a <= 1; a += 2)
            for (int b = -1; b <= 1; b += 2) {
                addBox(V3(t.x + a * 1.75, y0 + 7.0, t.z + b * 1.75), V3(0.14, 7.2, 0.14), 0, -a * 0.075, -b * 0.075, wood);
                poles.push_back({t.x + a * 1.75, t.z + b * 1.75, y0 - 0.5, y0 + 14, 0.16});
            }
        addBox(V3(t.x, y0 + 14.1, t.z), V3(2.3, 0.12, 2.3), 0, 0, 0, wood);
        addBox(V3(t.x, y0 + 15.2, t.z), V3(1.7, 1.0, 1.7), 0, 0, 0, V3(0.45, 0.2, 0.12));
        addBox(V3(t.x, y0 + 16.35, t.z), V3(2.1, 0.1, 2.1), 0, 0, 0, tin);
        standIn(from, "fire_tower", V3(t.x, y0, t.z), 0);
        table(rel(t0, 0, 4, -5), 0.3);
        fixtures.push_back({PROP_CAIRN, rel(t0, 0, 5, 4), V3(), 1.0});
    }
    for (const char* id : {"BC.LINE_SHACK", "BC.PROSPECTOR_CABIN", "BC.BEAVER_POND_CABIN", "BC.MOUNTAIN_HUT"}) {
        const MapPlace* pl = findPlace(MAP_BACKCOUNTRY, id);
        if (!pl) continue;
        const V3 P(pl->p.x, height(pl->p.x, pl->p.z), pl->p.z);
        const double yw = pl->yaw;
        const int variant = id[3] == 'L' ? 0 : id[3] == 'P' ? 1 : id[3] == 'B' ? 2 : 1;
        build("cabin", rel(P, yw, -7, 0), yw, V3(2.7, 1.9, 2.2), {{"porch", 1}}, variant);
        build("woodpile", rel(P, yw, -1, 4.6), yw, V3(1.4, 0.5, 0.5));
        build("outhouse", rel(P, yw, -9, -7), yw + M_PI, V3(0.6, 1.1, 0.6));
        fireRing(rel(P, yw, 7, -5));
        table(rel(P, yw, 6, 4), yw);
    }
}
