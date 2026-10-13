// Autopilot: route planning, seeing the ground ahead, picking a line and a drive setup,
// and driving (see autopilot.h).
#include "autopilot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>

#include "pilotsense.h"
#include "softbody.h"

const char* pilotRouteName(int route) { return route == PILOT_ROADS ? "ROADS ONLY" : route == PILOT_TRAILS ? "ROADS AND TRAILS" : "ANYWHERE"; }

namespace {

// ---- The map as the planner sees it: one cell per 2 m ----------------------------------

constexpr double kCell = 2.0;
constexpr int kN = (int)(2 * World::kHalfSize / kCell);

// A rock, log or ledge, and how tall it stands.
struct Lump {
    double x, z, r, height;
    bool box;
};

struct Grid {
    uint64_t stamp = 0;  // the world's, when this was built
    std::vector<float> h, slope;          // terrain height and steepness at each cell
    std::vector<float> obs;               // tallest rock / log / ledge in the cell, above the ground
    std::vector<uint8_t> surf, track;     // surface; on a road or trail
    std::vector<uint8_t> tall;            // blocked for anything (walls, trees, deep water)
    std::vector<Lump> lumps;
};

int cellOf(double v) { return std::min(std::max((int)std::floor((v + World::kHalfSize) / kCell), 0), kN - 1); }
double centerOf(int i) { return -World::kHalfSize + (i + 0.5) * kCell; }

// Marks every cell whose center is within r of (x, z).
template <class F> void disc(double x, double z, double r, F mark) {
    for (int i = cellOf(x - r); i <= cellOf(x + r); i++)
        for (int j = cellOf(z - r); j <= cellOf(z + r); j++)
            if (std::hypot(centerOf(i) - x, centerOf(j) - z) <= r) mark(i * kN + j);
}

const Grid& gridFor(const World& w) {
    static thread_local Grid g;
    if (g.stamp == w.stamp && w.stamp) return g;
    g.stamp = w.stamp;
    const int N = kN * kN;
    g.h.assign(N, 0);
    g.slope.assign(N, 0);
    g.obs.assign(N, 0);
    g.surf.assign(N, SURF_GRASS);
    g.track.assign(N, 0);
    g.tall.assign(N, 0);
    g.lumps.clear();
    for (int i = 0; i < kN; i++)
        for (int j = 0; j < kN; j++) {
            const double x = centerOf(i), z = centerOf(j);
            const int id = i * kN + j;
            g.h[id] = (float)w.height(x, z);
            const Surface s = w.surfaceAt(x, z);
            g.surf[id] = s;
            const RoadInfo r = w.roadAt(x, z);
            const bool painted = r.paint == PAINT_ROAD || r.paint == PAINT_TRAIL || r.paint == PAINT_GRAVEL || r.paint == PAINT_PAD || r.paint == PAINT_SCREE ||
                                 r.paint == PAINT_TRACK || r.paint == PAINT_FREEWAY || r.paint == PAINT_WET;
            g.track[id] = painted || s == SURF_PAVED;
            // Deep water.
            if (w.waterLevel(x, z) - g.h[id] > 0.45) g.tall[id] = 1;
        }
    for (int i = 1; i < kN - 1; i++)
        for (int j = 1; j < kN - 1; j++) {
            const double gx = (g.h[(i + 1) * kN + j] - g.h[(i - 1) * kN + j]) / (2 * kCell);
            const double gz = (g.h[i * kN + j + 1] - g.h[i * kN + j - 1]) / (2 * kCell);
            g.slope[i * kN + j] = (float)std::hypot(gx, gz);
        }
    // Keep off the banks of deep water too (a steep bank slides you in): one cell round it.
    {
        std::vector<uint8_t> wet = g.tall;
        for (int i = 1; i < kN - 1; i++)
            for (int j = 1; j < kN - 1; j++)
                if (wet[i * kN + j] && w.waterLevel(centerOf(i), centerOf(j)) > -1e8)
                    for (int di = -1; di <= 1; di++)
                        for (int dj = -1; dj <= 1; dj++) g.tall[(i + di) * kN + j + dj] = 1;
    }
    // How tall the rocks, logs and ledges stand in each cell, sampled every half meter.
    auto sampleObs = [&](double x0, double z0, double r, auto topAt) {
        double tallest = 0;
        for (double x = x0 - r; x <= x0 + r; x += 0.5)
            for (double z = z0 - r; z <= z0 + r; z += 0.5) {
                const double t = topAt(x, z);
                if (t < -1e8) continue;
                const double o = t - w.height(x, z);
                if (o <= 0) continue;
                tallest = std::max(tallest, o);
                float& c = g.obs[cellOf(x) * kN + cellOf(z)];
                c = std::max(c, (float)o);
            }
        return tallest;
    };
    for (const Rock& rk : w.rocks) {
        double reach = 0;
        for (const V3& v : rk.v) reach = std::max(reach, std::hypot(v.x - rk.c.x, v.z - rk.c.z));
        const double hgt = sampleObs(rk.c.x, rk.c.z, reach, [&](double x, double z) { return rockTop(rk, x, z); });
        if (hgt > 0.05) g.lumps.push_back({rk.c.x, rk.c.z, reach, hgt, false});
    }
    for (const Log& l : w.logs) {
        const V3 d = l.b - l.a;
        const int n = std::max(1, (int)(len(d) / 1.0));
        double hgt = 0;
        for (int k = 0; k <= n; k++) {
            const V3 p = l.a + d * ((double)k / n);
            hgt = std::max(hgt, sampleObs(p.x, p.z, l.r, [&](double x, double z) { return logTop(l, x, z); }));
        }
        const V3 m = (l.a + l.b) * 0.5;
        g.lumps.push_back({m.x, m.z, len(d) * 0.5 + l.r, hgt, false});
    }
    // Trees and posts: grown by half a truck's width plus a margin so the route keeps clear.
    const double grow = 1.3;
    for (const Pole& p : w.poles) disc(p.x, p.z, p.r + grow, [&](int id) { g.tall[id] = 1; });
    for (const Box& b : w.boxes) {
        if (!b.solid && !b.hidden) continue;
        const double reach = std::hypot(b.h.x, b.h.z);
        const double hgt = sampleObs(b.c.x, b.c.z, reach, [&](double x, double z) { return boxTop(b, x, z); });
        if (hgt < 1.6) continue;  // a ledge or a step: climbable by something, the planner decides
        // A wall: its outline on the ground (it may be turned), grown.
        const V3 ax = norm(V3(b.ax[0].x, 0, b.ax[0].z)), az = norm(V3(b.ax[2].x, 0, b.ax[2].z));
        const double hx = b.h.x * std::hypot(b.ax[0].x, b.ax[0].z) + grow;
        const double hz = b.h.z * std::hypot(b.ax[2].x, b.ax[2].z) + grow;
        const double r = std::hypot(hx, hz);
        for (int i = cellOf(b.c.x - r); i <= cellOf(b.c.x + r); i++)
            for (int j = cellOf(b.c.z - r); j <= cellOf(b.c.z + r); j++) {
                const V3 d(centerOf(i) - b.c.x, 0, centerOf(j) - b.c.z);
                if (std::fabs(dot(d, ax)) <= hx && std::fabs(dot(d, az)) <= hz) g.tall[i * kN + j] = 1;
            }
    }
    return g;
}

struct Limits {
    double grade, cross;  // steepest climb or descent, and side slope, it will take
    bool truck;
};

// Most it could climb set up as well as it can be: lowest range, every locker, tires down.
double bestCap(const VehicleSpec& vs) {
    const int L = bestLevel(vs);
    const int lockers = (vs.lockerRear ? 1 : 0) + (vs.lockerFront && L > LEVEL_2WD ? 1 : 0);
    return stepCap(vs, L, lockers, vs.transfer != XFER_NONE ? std::min(vs.tirePsi, 20.0) : vs.tirePsi);
}

}  // namespace

bool planRoute(const World& w, const VehicleSpec& vs, const V3& from, const V3& to, int route, const std::vector<V3>& avoid,
               std::vector<V3>& out, double capScale) {
    out.clear();
    const Grid& g = gridFor(w);
    const bool truck = vs.transfer != XFER_NONE;
    const Limits lim{truck ? 0.5 : 0.28, truck ? 0.45 : 0.26, truck};
    // Ledges and rocks: the tallest step it can take, and drop it will go off.
    const double cap = bestCap(vs) * capScale;
    const double dropCap = truck ? 2.2 * vs.wheelRadius : 0.6 * vs.wheelRadius;
    const int N = kN * kN;
    static thread_local std::vector<float> cost;
    static thread_local std::vector<int> parent;
    static thread_local std::vector<uint8_t> closed, extra;
    cost.assign(N, 1e30f);
    parent.assign(N, -1);
    closed.assign(N, 0);
    extra.assign(N, 0);
    for (const V3& a : avoid) disc(a.x, a.z, 4.0, [&](int id) { extra[id] = 1; });
    // Rocks and logs too tall to climb: kept clear of by half a truck's width and a margin.
    for (const Lump& l : g.lumps)
        if (l.height > cap) disc(l.x, l.z, l.r * 0.85 + 1.3, [&](int id) { extra[id] = 1; });
    auto open = [&](int id, bool start) {
        if (start) return true;
        return !g.tall[id] && !extra[id];
    };
    const int si = cellOf(from.x), sj = cellOf(from.z);
    int gi = cellOf(to.x), gj = cellOf(to.z);
    // The goal itself may sit among obstacles (a shed's posts): use the nearest open cell.
    if (!open(gi * kN + gj, false)) {
        double best = 1e9;
        int bi = gi, bj = gj;
        for (int i = gi - 6; i <= gi + 6; i++)
            for (int j = gj - 6; j <= gj + 6; j++)
                if (i >= 0 && j >= 0 && i < kN && j < kN && open(i * kN + j, false) && std::hypot(i - gi, j - gj) < best)
                    best = std::hypot(i - gi, j - gj), bi = i, bj = j;
        gi = bi, gj = bj;
    }
    const int start = si * kN + sj, goal = gi * kN + gj;
    // Cells right around the truck are always usable (it may be parked by a tree).
    std::vector<int> startArea;
    disc(from.x, from.z, 2.6, [&](int id) { startArea.push_back(id); });
    auto inStart = [&](int id) { return std::find(startArea.begin(), startArea.end(), id) != startArea.end(); };
    auto usable = [&](int id) { return open(id, false) || inStart(id); };
    const double offTrack = route == PILOT_ROADS ? 12 : route == PILOT_TRAILS ? 3 : 1;
    auto surfFactor = [&](int id) {
        if (g.track[id]) return 1.0;
        switch (g.surf[id]) {
            case SURF_PAVED: return 1.0;
            case SURF_DIRT: return 1.25;
            case SURF_ROCK: return truck ? 1.6 : 5.0;
            case SURF_SCREE: return truck ? 2.5 : 8.0;
            default: return 1.5;
        }
    };
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    cost[start] = 0;
    auto heur = [&](int id) { return (float)(std::hypot(id / kN - gi, id % kN - gj) * kCell); };
    q.push({heur(start), start});
    const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    bool found = false;
    int expanded = 0;
    while (!q.empty()) {
        const int id = q.top().second;
        q.pop();
        if (closed[id]) continue;
        closed[id] = 1;
        if (id == goal) {
            found = true;
            break;
        }
        if (++expanded > N) break;
        const int i = id / kN, j = id % kN;
        for (int k = 0; k < 8; k++) {
            const int ni = i + di[k], nj = j + dj[k];
            if (ni < 1 || nj < 1 || ni >= kN - 1 || nj >= kN - 1) continue;
            const int nid = ni * kN + nj;
            if (closed[nid] || !usable(nid)) continue;
            const double dist = k < 4 ? kCell : kCell * M_SQRT2;
            const double grade = (g.h[nid] - g.h[id]) / dist;
            const double s = g.slope[nid], cross = std::sqrt(std::max(0.0, s * s - grade * grade));
            if (std::fabs(grade) > lim.grade || cross > lim.cross) continue;
            // Up onto a ledge or rock taller than it can climb, or off one it shouldn't drop.
            const double step = g.obs[nid] - g.obs[id];
            if (!inStart(id) && (step > cap || -step > dropCap)) continue;
            double c = dist * surfFactor(nid) * (1 + 4 * grade * grade + 6 * cross * cross) * (g.track[nid] ? 1 : offTrack);
            if (g.obs[nid] > 0.05) c *= 1 + 2.0 * g.obs[nid] / cap;  // over rocks: rough, slow, and a risk
            const float nc = cost[id] + (float)c;
            if (nc < cost[nid]) {
                cost[nid] = nc;
                parent[nid] = id;
                q.push({nc + heur(nid), nid});
            }
        }
    }
    if (!found && std::getenv("CZ_PLAN")) {
        // Why not: walk the straight line from start to goal and say what blocks each step.
        int pi = si, pj = sj;
        const int steps = std::max(std::abs(gi - si), std::abs(gj - sj));
        for (int k = 1; k <= steps; k++) {
            const int ni = si + (gi - si) * k / steps, nj = sj + (gj - sj) * k / steps;
            const int id = pi * kN + pj, nid = ni * kN + nj;
            const double dist = std::hypot(ni - pi, nj - pj) * kCell;
            const double grade = dist > 0 ? (g.h[nid] - g.h[id]) / dist : 0;
            const double sl = g.slope[nid], cross = std::sqrt(std::max(0.0, sl * sl - grade * grade));
            std::printf("  cell (%.0f,%.0f) h %.1f obs %.2f tall %d extra %d grade %.2f cross %.2f step %.2f%s\n", centerOf(ni), centerOf(nj), g.h[nid], g.obs[nid],
                        g.tall[nid], extra[nid], grade, cross, g.obs[nid] - g.obs[id],
                        closed[nid] ? " reached" : "");
            pi = ni, pj = nj;
        }
        std::printf("  cap %.2f dropCap %.2f limits grade %.2f cross %.2f\n", cap, dropCap, lim.grade, lim.cross);
    }
    if (!found) return false;
    std::vector<V3> pts;
    for (int id = goal; id >= 0; id = parent[id]) pts.push_back(V3(centerOf(id / kN), 0, centerOf(id % kN)));
    std::reverse(pts.begin(), pts.end());
    pts.front() = V3(from.x, 0, from.z);
    // Smooth the grid's zigzags (a few passes of neighbour averaging, ends pinned).
    for (int pass = 0; pass < 4; pass++)
        for (size_t k = 1; k + 1 < pts.size(); k++) pts[k] = pts[k] * 0.5 + (pts[k - 1] + pts[k + 1]) * 0.25;
    // Resample every ~2 m, on the ground.
    out.push_back(pts.front());
    for (size_t k = 1; k < pts.size(); k++) {
        V3 a = out.back(), b = pts[k];
        double d = std::hypot(b.x - a.x, b.z - a.z);
        while (d >= 2.0) {
            a = a + (b - a) * (2.0 / d);
            out.push_back(a);
            d = std::hypot(b.x - a.x, b.z - a.z);
        }
    }
    if (std::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.3) out.push_back(pts.back());
    for (V3& p : out) p.y = w.height(p.x, p.z);
    return true;
}

namespace {

// ---- The vehicle as it sees itself --------------------------------------------------------

void buildBody(const Car& c, PilotBody& b) {
    b = PilotBody();
    b.build = c.buildCount;
    double xF = 0, xR = 0, zF = 0, zR = 0, ground = 0;
    int nF = 0, nR = 0;
    std::vector<V3> hubs;
    for (const Wheel& wh : c.wheels) {
        const V3 hub = (c.restLocal[wh.inner] + c.restLocal[wh.outer]) * 0.5;
        hubs.push_back(hub);
        (wh.front ? xF : xR) += hub.x;
        (wh.front ? zF : zR) += std::fabs(hub.z);
        (wh.front ? nF : nR)++;
        ground += hub.y - wh.R;
        b.R = wh.R;
        b.width = wh.width;
    }
    if (!nF || !nR) return;
    xF /= nF, xR /= nR;
    ground /= nF + nR;
    b.wb = xF - xR;
    b.trackF = 2 * zF / nF;
    b.trackR = 2 * zR / nR;
    // Underbody: the lowest part of the body over each patch (0.3 m along, 7 strips
    // across), leaving out the tires and what moves with them.
    const double W = c.spec->width;
    constexpr int kZ = 7;
    constexpr double kX = 0.3;
    const int nx = (int)std::ceil((c.spec->length + 2) / kX);
    std::vector<float> low(nx * kZ, 1e9f);
    double xmin = 1e9, xmax = -1e9, cgY = 0, M = 0;
    std::vector<char> hub(c.nodes.size(), 0);
    for (const Wheel& wh : c.wheels) hub[wh.inner] = hub[wh.outer] = 1;
    for (int i = 0; i < (int)c.nodes.size(); i++) {
        if (hub[i]) continue;
        const V3& p = c.restLocal[i];
        xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x);
        cgY += (p.y - ground) * c.nodes[i].m, M += c.nodes[i].m;
        const double clear = p.y - ground;
        if (clear > 1.0) continue;
        bool tire = false;
        for (const V3& h : hubs) tire |= std::fabs(p.x - h.x) < b.R && std::fabs(p.z - h.z) < b.width * 0.5 + 0.12;
        if (tire) continue;
        const int ix = (int)std::floor((p.x - xR + 1 + c.spec->length * 0.5) / kX);
        const int iz = std::min(kZ - 1, std::max(0, (int)std::floor((p.z + W / 2) / W * kZ)));
        if (ix < 0 || ix >= nx) continue;
        float& l = low[ix * kZ + iz];
        l = std::min(l, (float)clear);
    }
    b.front = xmax - xR;
    b.back = xmin - xR;
    for (int ix = 0; ix < nx; ix++)
        for (int iz = 0; iz < kZ; iz++) {
            if (low[ix * kZ + iz] > 1e8f) continue;
            const double x = (ix + 0.5) * kX - 1 - c.spec->length * 0.5;
            double z = ((iz + 0.5) / kZ - 0.5) * W;
            if (iz == 0 || iz == kZ - 1) z = (z > 0 ? 1 : -1) * (W * 0.5 + 0.2);  // the sides, mirrors and a margin
            b.under.push_back({(float)x, (float)z, std::max(0.04f, low[ix * kZ + iz])});
        }
    const double cgH = M > 0 ? std::max(0.3, cgY / M) : 0.6;
    b.rollMax = 0.6 * std::atan(0.5 * std::min(b.trackF, b.trackR) / cgH);
}

// ---- Trying lines --------------------------------------------------------------------------

struct Line {
    bool route = true;     // follow the route (pure pursuit) all the way
    double k1 = 0, s1 = 0; // otherwise: hold curvature k1 for s1 meters, then back to the route
    bool ok = true;
    double failAt = 1e9;
    int failThing = -1;
    const char* failWhy = "";
    double cost = 0, time = 0;
    double maxStep = 0, maxDrop = 0, minMargin = 1e9, maxRoll = 0, maxPitch = 0;
    int stepThing = -1, bellyThing = -1;
    double vCmd = 1e9;
    int need = 0;
    double needDist = 1e9, needStep = 0;
    bool needAir = false;
    std::vector<V3> pts;
    std::vector<int> tire, belly;  // things its tires climb, and things under the body
};

// What the drive setups can climb, right now and with the tires aired down.
struct Caps {
    double now[LEVEL_COUNT], aired[LEVEL_COUNT];
    bool has[LEVEL_COUNT];
    double best = 0;
    int cur = 0;
};

Caps capsFor(const Car& c, const Autopilot& ap) {
    Caps k;
    const VehicleSpec& vs = *c.spec;
    k.cur = currentLevel(c);
    const bool truck = vs.transfer != XFER_NONE;
    for (int L = 0; L < LEVEL_COUNT; L++) {
        k.has[L] = L == LEVEL_2WD || modeForLevel(vs, L) >= 0;
        const bool low = L == LEVEL_LOW;
        int lockers = 0;
        if (vs.lockerRear && (!vs.rearLockerLowOnly || low)) lockers++;
        if (vs.lockerFront && L > LEVEL_2WD && (!vs.frontLockerLowOnly || low)) lockers++;
        k.now[L] = stepCap(vs, L, lockers, c.tirePsi) * ap.capScale[L];
        k.aired[L] = truck ? stepCap(vs, L, lockers, std::min(c.tirePsi, 20.0)) * ap.capScale[L] : k.now[L];
        if (k.has[L]) k.best = std::max(k.best, k.aired[L]);
    }
    if (!truck) k.has[LEVEL_2WD] = true;
    return k;
}

// Nearest route point to p (searching from `from`), and the distance to the route there.
double routeDistance(const std::vector<V3>& path, int from, const V3& p, int* at = nullptr, double* heading = nullptr) {
    double bd = 1e18;
    int bk = from;
    for (int k = std::max(0, from); k < std::min((int)path.size(), from + 80); k++) {
        const double d = std::hypot(path[k].x - p.x, path[k].z - p.z);
        if (d < bd) bd = d, bk = k;
    }
    if (at) *at = bk;
    if (heading) {
        const int a = std::max(0, bk - 1), b = std::min((int)path.size() - 1, bk + 1);
        *heading = std::atan2(path[b].z - path[a].z, path[b].x - path[a].x);
    }
    return bd;
}

void tryLine(Line& L, Scene& sc, const Car& c, const Autopilot& ap, const Caps& caps, const V3& Q0, const V3& f0, double v, double H,
             double kPrev) {
    const PilotBody& b = ap.body;
    const double ds = 0.25, R = b.R;
    const double kmax = std::tan(0.58) / b.wb;
    const double Ld = clampd(3.5 + std::fabs(v) * 0.9, 4.5, 16);
    const double asked = ap.speed;
    const double rollMax = b.rollMax;
    const double pitchMax = caps.has[LEVEL_LOW] ? 0.6 : c.spec->transfer != XFER_NONE ? 0.45 : 0.3;
    const double dropCap = c.spec->transfer != XFER_NONE ? 2.2 * R : 0.6 * R;
    V3 Q = Q0, f = f0;
    int j = ap.idx;
    double devSum = 0;
    int devN = 0;
    const int n = (int)(H / ds);
    for (int k = 0; k <= n; k++) {
        const double s = k * ds;
        const V3 l(f.z, 0, -f.x);  // left of travel
        const bool grace = s < 0.6;  // where it is now: don't refuse to move off a scrape
        auto fail = [&](int thing, const char* why) {
            if (grace) return false;
            L.ok = false;
            L.failAt = s;
            L.failThing = thing;
            L.failWhy = why;
            return true;
        };
        // The tires: where each one sits, and the steepest step or drop just ahead of it.
        double contact[4];
        double step = 0, drop = 0;
        int stepWho = -1;
        for (int wi = 0; wi < 4; wi++) {
            const bool front = wi < 2, left = wi % 2 == 0;
            const V3 P = Q + f * (front ? b.wb : 0) + l * ((left ? 0.5 : -0.5) * (front ? b.trackF : b.trackR));
            double cmax = -1e9;
            double base[3];
            for (int e = 0; e < 3; e++) {
                const V3 q = P + l * ((e - 1) * 0.3 * b.width);
                int who;
                const double t = sc.top(q.x, q.z, &who);
                cmax = std::max(cmax, t);
                base[e] = t - sc.ground(q.x, q.z);
            }
            // A round tire rides over an edge rather than dropping off it: it rests where
            // its circle first touches whatever is under it.
            for (double u : {0.35, 0.7, 0.9})
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const double d = sgn * u * R;
                    cmax = std::max(cmax, sc.top(P.x + f.x * d, P.z + f.z * d) - R * (1 - std::sqrt(1 - u * u)));
                }
            contact[wi] = cmax;
            for (double d = 0.125; d <= R + 1e-6; d += 0.125)
                for (int e = 0; e < 3; e++) {
                    const V3 q = P + f * d + l * ((e - 1) * 0.3 * b.width);
                    int who;
                    const double o = sc.top(q.x, q.z, &who) - sc.ground(q.x, q.z);
                    const double rise = o - base[e] - 0.4 * d, fall = base[e] - o - 0.4 * d;
                    if (rise > step) step = rise, stepWho = who;
                    drop = std::max(drop, fall);
                }
        }
        if (stepWho >= 0 && step > 0.05 && std::find(L.tire.begin(), L.tire.end(), stepWho) == L.tire.end()) L.tire.push_back(stepWho);
        if (step > L.maxStep) L.maxStep = step, L.stepThing = stepWho;
        L.maxDrop = std::max(L.maxDrop, drop);
        if (step > caps.best && fail(stepWho, "step")) break;
        if (drop > dropCap && fail(-1, "drop")) break;
        // Which setup this step needs (and whether only aired-down tires will do).
        if (step > 0.04 && !grace) {
            int need = -1;
            bool air = false;
            for (int Lv = 0; Lv < LEVEL_COUNT && need < 0; Lv++)
                if (caps.has[Lv] && caps.now[Lv] >= step) need = Lv;
            for (int Lv = 0; Lv < LEVEL_COUNT && need < 0; Lv++)
                if (caps.has[Lv] && caps.aired[Lv] >= step) need = Lv, air = true;
            if (need > L.need || (need == L.need && air && !L.needAir) || (need == L.need && L.needDist > 1e8)) {
                if (need > L.need || L.needDist > 1e8) L.needDist = s;
                L.need = need, L.needAir |= air;
            }
            L.needStep = std::max(L.needStep, step);
        }
        // Lean and pitch from where the tires sit.
        const double rollF = std::atan((contact[0] - contact[1]) / b.trackF), rollR = std::atan((contact[2] - contact[3]) / b.trackR);
        const double roll = 0.5 * (rollF + rollR);
        const double pitch = std::atan((contact[0] + contact[1] - contact[2] - contact[3]) * 0.5 / b.wb);
        L.maxRoll = std::max(L.maxRoll, std::fabs(roll));
        L.maxPitch = std::max(L.maxPitch, std::fabs(pitch));
        if (std::fabs(roll) > rollMax && fail(-1, "roll")) break;
        if (std::fabs(pitch) > pitchMax && fail(-1, "pitch")) break;
        // The underbody over whatever is under it (every other step).
        double margin = 1e9;
        if (k % 2 == 0) {
            for (const PilotBody::Under& u : b.under) {
                const double hF = contact[1] + (contact[0] - contact[1]) * (u.z + b.trackF * 0.5) / b.trackF;
                const double hR = contact[3] + (contact[2] - contact[3]) * (u.z + b.trackR * 0.5) / b.trackR;
                const double plane = hR + (hF - hR) * u.x / b.wb;
                const V3 q = Q + f * u.x + l * u.z;
                int who;
                const double m = plane + u.c - sc.top(q.x, q.z, &who);
                if (m < margin) margin = m;
                if (m < 0.12 && who >= 0 && std::find(L.belly.begin(), L.belly.end(), who) == L.belly.end()) L.belly.push_back(who);
                if (m < L.minMargin) L.minMargin = m, L.bellyThing = who;
                if (m < -0.03 && fail(who, "belly")) break;
            }
            if (!L.ok) break;
        }
        // How fast it can take this bit.
        double vl = 1e9;
        if (step > 0.04) vl = std::min(vl, std::max(0.9, 9 * (1 - step / (0.55 * R))));
        if (drop > 0.1) vl = std::min(vl, std::max(0.7, 4 * (1 - drop / (1.5 * R))));
        if (margin < 0.12) vl = std::min(vl, 1.0 + 15 * std::max(0.0, margin));
        if (std::fabs(roll) > 0.12) vl = std::min(vl, std::max(1.2, 7 * (1 - std::fabs(roll) / rollMax)));
        // Steering: hold k1, or head for the route.
        double kap = L.k1;
        if (L.route || s >= L.s1) {
            while (j + 1 < (int)ap.path.size() &&
                   std::hypot(ap.path[j + 1].x - Q.x, ap.path[j + 1].z - Q.z) < std::hypot(ap.path[j].x - Q.x, ap.path[j].z - Q.z))
                j++;
            int t = j;
            double acc = 0;
            while (t + 1 < (int)ap.path.size() && acc < Ld) acc += len(ap.path[t + 1] - ap.path[t]), t++;
            const V3 d(ap.path[t].x - Q.x, 0, ap.path[t].z - Q.z);
            const double alpha = std::atan2(dot(d, l), dot(d, f));
            kap = clampd(2 * std::sin(alpha) / std::max(Ld, std::hypot(d.x, d.z)), -kmax, kmax);
        }
        if (std::fabs(kap) > 1e-4) vl = std::min(vl, std::sqrt((c.spec->transfer != XFER_NONE ? 2.5 : 4.0) / std::fabs(kap)));
        const double vs = std::min(asked, vl);
        L.time += ds / std::max(0.5, vs);
        L.vCmd = std::min(L.vCmd, std::sqrt(vs * vs + 2 * 1.3 * std::max(0.0, s - 0.5)));
        if (k % 4 == 0) {
            const V3 mid = Q + f * (b.wb * 0.5);
            L.pts.push_back(V3(mid.x, std::max(contact[2], contact[3]) + 0.15, mid.z));
            devSum += routeDistance(ap.path, ap.idx, mid);
            devN++;
        }
        f = norm(f + l * (kap * ds));
        Q += f * ds;
    }
    // Score: off the route, slow going, rough going, and changing its mind.
    double heading;
    const V3 end = Q + f * (b.wb * 0.5);
    const double dEnd = routeDistance(ap.path, ap.idx, end, nullptr, &heading);
    const double psi = std::fabs(wrapAngle(std::atan2(f.z, f.x) - heading));
    L.cost = 1.0 * dEnd + 0.5 * (devN ? devSum / devN : 0) + 1.2 * psi + L.time;
    L.cost += 3 * std::pow(L.maxStep / std::max(0.05, caps.best), 2) + 2 * std::pow(L.maxRoll / rollMax, 2);
    if (L.minMargin < 0.1) L.cost += 6 * (0.1 - L.minMargin);
    if (L.needAir) L.cost += 15;   // stopping to air down
    else if (L.need > caps.cur) L.cost += (L.need == LEVEL_LOW || caps.cur == LEVEL_LOW) ? 5 : 1;  // stopping to shift
    L.cost += 4 * std::fabs((L.route ? 0 : L.k1) - kPrev) * b.wb;
    if (!L.ok) L.cost += 100 + 100 * (1 - L.failAt / H);
}

std::string fmtThing(const Scene& sc, int who) {
    if (who == -2) return "DEEP WATER";
    if (who < 0 || who >= (int)sc.things().size()) return "THE GROUND";
    const Thing& t = sc.things()[who];
    char buf[64];
    if (t.kind == SEEN_TREE || t.kind == SEEN_VEHICLE || t.kind == SEEN_WALL) std::snprintf(buf, sizeof buf, "%s", seenName(t.kind));
    else std::snprintf(buf, sizeof buf, "%s %.2f M", seenName(t.kind), t.height);
    return buf;
}

// Which side of (x, z) a line passes (> 0: on its left, heading along f).
double sideOf(const Line& L, double x, double z, const V3& f) {
    double bd = 1e18, side = 0;
    for (const V3& p : L.pts) {
        const double d = std::hypot(p.x - x, p.z - z);
        if (d < bd) bd = d, side = (p.x - x) * f.z - (p.z - z) * f.x;
    }
    return side;
}

// Looks ahead, tries lines, and settles on one (sets ap.onRoute / kappa / lineSpeed / need...).
void sense(Car& c, Autopilot& ap, const World& w, const std::vector<Car>* cars, double v) {
    if (ap.body.build != c.buildCount) buildBody(c, ap.body);
    const PilotBody& b = ap.body;
    if (b.under.empty()) return;
    static thread_local Scene sc;
    const Frame fr = c.frame();
    V3 f = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    // The rear axle's center.
    V3 Q;
    int nR = 0;
    for (const Wheel& wh : c.wheels)
        if (!wh.front) Q += (c.nodes[wh.inner].p + c.nodes[wh.outer].p) * 0.5, nR++;
    Q = nR ? Q / nR : fr.com - f * (b.wb * 0.5);
    Q.y = 0;
    const double H = clampd(8 + 1.2 * std::fabs(v), 9, 20);
    sc.gather(w, cars, &c, Q + f * (H * 0.5), H * 0.5 + b.front + 4);
    const Caps caps = capsFor(c, ap);
    const double kPrev = ap.onRoute ? 0 : ap.kappa;
    // The lines: the route itself, and every steering angle held for a third, two thirds
    // or all of the way before heading back to it.
    static const double ks[] = {0, 0.012, -0.012, 0.03, -0.03, 0.05, -0.05, 0.075, -0.075, 0.105, -0.105, 0.14, -0.14, 0.19, -0.19, 0.25, -0.25};
    std::vector<Line> lines;
    lines.reserve(52);
    lines.push_back(Line());
    for (double frac : {0.35, 0.7, 1.0})
        for (double k : ks) {
            Line L;
            L.route = false;
            L.k1 = clampd(k, -std::tan(0.58) / b.wb, std::tan(0.58) / b.wb);
            L.s1 = frac * H;
            lines.push_back(L);
        }
    for (Line& L : lines) tryLine(L, sc, c, ap, caps, Q, f, v, H, kPrev);
    int best = 0;
    for (int k = 1; k < (int)lines.size(); k++)
        if (lines[k].cost < lines[best].cost) best = k;
    // Stick with the route unless another line is clearly better.
    if (best != 0 && lines[0].ok && lines[0].cost < lines[best].cost + 0.4) best = 0;
    const Line& L = lines[best];
    static const bool dbg = getenv("PILOT_SENSE");
    if (dbg)
        for (int k : {0, best})
            std::printf("    line %2d %s k %+.3f s1 %4.1f ok %d fail %4.1f(%d %s) cost %6.1f t %5.1f step %.2f(%d) drop %.2f margin %+.2f(%d) roll %.2f v %.1f need %d@%.1f\n", k,
                        k == best ? "*" : " ", lines[k].k1, lines[k].s1, lines[k].ok, lines[k].failAt > 1e8 ? -1 : lines[k].failAt,
                        lines[k].failThing, lines[k].failWhy, lines[k].cost, lines[k].time, lines[k].maxStep, lines[k].stepThing, lines[k].maxDrop, lines[k].minMargin,
                        lines[k].bellyThing, lines[k].maxRoll, lines[k].vCmd, lines[k].need, lines[k].needDist > 1e8 ? -1 : lines[k].needDist);
    ap.onRoute = L.route;
    ap.kappa = L.k1;
    ap.lineSpeed = L.vCmd;
    ap.needLevel = L.need;
    ap.needDist = L.needDist;
    ap.needStep = L.needStep;
    ap.needAir = L.needAir;
    ap.blocked = !L.ok && L.failAt < 3.0;
    ap.lineRoll = L.maxRoll;
    // For the HUD: the line, the others, what it sees and what it's doing about it.
    ap.line = L.pts;
    ap.options.clear();
    ap.optionOk.clear();
    for (int k = 0; k < (int)lines.size(); k++) {
        if (k == best) continue;
        ap.options.push_back(lines[k].pts);
        ap.optionOk.push_back(lines[k].ok);
    }
    const Line& straight = lines[0];
    ap.seen.clear();
    std::vector<std::pair<double, int>> order;
    for (int k = 0; k < (int)sc.things().size(); k++) {
        const Thing& t = sc.things()[k];
        const V3 d(t.x - fr.com.x, 0, t.z - fr.com.z);
        const double ahead = dot(d, f);
        if (ahead < -1 || ahead > H + 4 || std::fabs(d.x * f.z - d.z * f.x) > 7 || t.height < 0.12) continue;
        order.push_back({len(d), k});
    }
    std::sort(order.begin(), order.end());
    auto has = [](const std::vector<int>& v, int k) { return std::find(v.begin(), v.end(), k) != v.end(); };
    for (size_t n = 0; n < order.size() && ap.seen.size() < 8; n++) {
        const int k = order[n].second;
        const Thing& t = sc.things()[k];
        int verdict = VERDICT_PASSING;
        if (has(L.tire, k)) verdict = VERDICT_OVER;
        else if (has(L.belly, k)) verdict = VERDICT_STRADDLE;
        else if (best != 0 && (has(straight.tire, k) || has(straight.belly, k) || straight.failThing == k))
            verdict = sideOf(L, t.x, t.z, f) > 0 ? VERDICT_AROUND_LEFT : VERDICT_AROUND_RIGHT;
        if (ap.blocked && L.failThing == k) verdict = VERDICT_BLOCKING;
        double top = -1e9;
        for (int e = -2; e <= 2; e++) top = std::max(top, sc.top(t.x + e * 0.2, t.z));
        ap.seen.push_back({V3(t.x, std::min(top, sc.ground(t.x, t.z) + t.height), t.z), t.height, t.kind, verdict});
    }
    // In words.
    char buf[160];
    buf[0] = 0;
    if (ap.blocked) {
        std::snprintf(buf, sizeof buf, "BLOCKED BY %s", fmtThing(sc, L.failThing).c_str());
    } else if (best != 0 && (!straight.ok || straight.maxStep > L.maxStep + 0.05 || straight.minMargin < std::min(0.1, L.minMargin))) {
        const int who = !straight.ok ? straight.failThing : straight.maxStep > L.maxStep + 0.05 ? straight.stepThing : straight.bellyThing;
        const bool left = who >= 0 ? sideOf(L, sc.things()[who].x, sc.things()[who].z, f) > 0 : L.k1 > 0;
        std::snprintf(buf, sizeof buf, "%s AHEAD: GOING AROUND %s", fmtThing(sc, who).c_str(), left ? "LEFT" : "RIGHT");
    } else if (L.maxStep > 0.08 && L.stepThing >= 0) {
        std::snprintf(buf, sizeof buf, "%s: %s", fmtThing(sc, L.stepThing).c_str(),
                      L.maxStep > 0.35 * b.R ? "CLIMBING IT" : "TIRE OVER IT");
    } else if (L.minMargin < 0.25 && L.bellyThing >= 0) {
        const int k = sc.things()[L.bellyThing].kind;
        if (k == SEEN_WALL || k == SEEN_TREE || k == SEEN_VEHICLE)
            std::snprintf(buf, sizeof buf, "SQUEEZING PAST THE %s", fmtThing(sc, L.bellyThing).c_str());
        else
            std::snprintf(buf, sizeof buf, "%s: STRADDLING IT (%.0f CM UNDER)", fmtThing(sc, L.bellyThing).c_str(), std::max(0.0, L.minMargin) * 100);
    }
    ap.thought = buf;
}

// ---- Drive setup ------------------------------------------------------------------------

struct Ahead {
    double maxGrade = 0, descent = 0;  // steepest climb (+) and descent (-) along the route
    bool paved = true, loose = false, rock = false;
    double technical = 0;              // meters of rock, scree or rocks in the way in the next 200 m
};

Ahead lookAhead(const Autopilot& ap, const World& w) {
    Ahead a;
    double dist = 0;
    for (int k = ap.idx; k + 1 < (int)ap.path.size() && dist < 200; k++) {
        const V3 &p = ap.path[k], &q = ap.path[k + 1];
        const double run = std::max(0.5, std::hypot(q.x - p.x, q.z - p.z));
        const Surface s = w.surfaceAt(p.x, p.z);
        const bool hard = s == SURF_ROCK || s == SURF_SCREE;
        // Rock wants low range and air down; so does a long stretch of sand (or snow), to float.
        if (hard || s == SURF_SAND || s == SURF_SNOW) a.technical += run;
        if (dist < 40) {
            const double gr = (q.y - p.y) / run;
            a.maxGrade = std::max(a.maxGrade, gr);
            a.descent = std::min(a.descent, gr);
            if (s != SURF_PAVED && w.roadAt(p.x, p.z).paint != PAINT_ROAD) a.paved = false;
            // Loose or slippery: both axles pulling, tied together.
            a.loose |= s == SURF_SCREE || s == SURF_SAND || s == SURF_MUD || s == SURF_SNOW;
            a.rock |= hard;
        }
        dist += run;
    }
    return a;
}

// Puts the transfer case, lockers, sway bar and tires where they should be for what's
// ahead. Returns true while it needs the vehicle held still (shifting into or out of low,
// letting air out).
bool setUp(Car& c, Autopilot& ap, const World& w, double v, double dt, double& vt) {
    const VehicleSpec& vs = *c.spec;
    const bool truck = vs.transfer != XFER_NONE;
    const double R = ap.body.R;
    // Tire pressure: down for a long rocky stretch (or a ledge that needs it), back up
    // with the shop air at a repair station.
    if (truck) {
        if (ap.airTarget > 0) {
            if (c.tirePsi > ap.airTarget + 0.2) {
                c.airing = -1;
                vt = 0;
                char buf[64];
                std::snprintf(buf, sizeof buf, "AIRING DOWN TO %.0f PSI (%.0f)", ap.airTarget, c.tirePsi);
                ap.status = buf;
                return true;
            }
            ap.airTarget = 0;
        }
        for (const Station& st : w.stations)
            if (std::hypot(st.p.x - c.frame().com.x, st.p.z - c.frame().com.z) < st.r - 1.5 && c.tirePsi < vs.tirePsi - 2.5) {
                c.airing = 1;
                vt = 0;
                ap.status = "AIRING UP AT THE SHOP";
                return true;
            }
        if (c.tirePsi < 24) vt = std::min(vt, 55 / 3.6);  // aired down: easy on the sidewalls
    }
    c.airing = 0;
    if (!truck) return false;
    const Ahead a = lookAhead(ap, w);
    const int best = bestLevel(vs);
    int want = a.paved && a.maxGrade < 0.08 && a.descent > -0.08 ? LEVEL_2WD : LEVEL_4WD;
    if (a.loose || a.rock || a.maxGrade > 0.15 || a.descent < -0.15) want = LEVEL_4WD_LOCKED;
    if (a.maxGrade > 0.3 || (a.rock && a.maxGrade > 0.2) || a.descent < -0.25) want = LEVEL_LOW;
    if (ap.needDist < 25) want = std::max(want, ap.needLevel);
    if (ap.stuckCount >= 1) want = std::max(want, (int)LEVEL_4WD_LOCKED);
    if (ap.stuckCount >= 2) want = LEVEL_LOW;
    // This truck may not have that position: the next one up, or the best it has.
    while (want < best && modeForLevel(vs, want) < 0) want++;
    if (want > best) want = best;
    if (want > LEVEL_2WD && modeForLevel(vs, want) < 0) want = LEVEL_2WD;
    const int cur = currentLevel(c);
    ap.wantLevel = want;
    bool shift = false;
    if (want > cur) {
        shift = true;
        ap.easyTime = 0;
    } else if (want < cur) {
        ap.easyTime += dt;
        shift = cur == LEVEL_LOW ? ap.easyTime > 6 && ap.needDist > 30 && a.technical < 30 : ap.easyTime > 3;
    } else {
        ap.easyTime = 0;
    }
    if (shift) {
        const bool lowChange = want == LEVEL_LOW || cur == LEVEL_LOW;
        // Pins on the sway bar, or air to let out: those need a full stop.
        const bool airDown = want == LEVEL_LOW && c.tirePsi > 24 && (a.technical > 60 || ap.needAir);
        const bool pins = vs.swayDisconnect == 2;
        const double slowEnough = lowChange && (airDown || pins) ? 0.25 : lowChange ? 2.0 : 1e9;
        if (std::fabs(v) > slowEnough) {
            vt = std::min(vt, slowEnough < 1 ? 0.0 : 1.2);
            char buf[64];
            std::snprintf(buf, sizeof buf, "SLOWING TO SHIFT INTO %s", levelName(c, want));
            ap.status = buf;
            return lowChange;
        }
        c.setDriveMode(modeForLevel(vs, want));
        if (want == LEVEL_LOW) {
            if (airDown) ap.airTarget = 20;
            if (vs.swayDisconnect) c.swayDisconnected = true;
        } else if (cur == LEVEL_LOW && pins) {
            c.swayDisconnected = false;
        }
    }
    if (ap.needAir && c.lowRange && c.tirePsi > 21 && ap.needDist < 12) {
        ap.airTarget = 20;  // a ledge it can only climb aired down
        vt = 0;
        return std::fabs(v) < 0.3;
    }
    // Electronic sway-bar disconnect: off for crawling, back on for speed.
    if (vs.swayDisconnect == 1) c.swayDisconnected = c.lowRange && std::fabs(v) < 25 / 3.6;
    // Lockers.
    const double av = std::fabs(v);
    for (int axle = 0; axle < 2; axle++) {  // 0 front, 1 rear
        double spin = 0;
        for (const Wheel& wh : c.wheels)
            if (wh.front == (axle == 0) && c.wheelPowered(wh) && wh.load > 0)
                spin = std::max(spin, std::fabs(wh.omega * wh.R) - std::fabs(wh.groundSpeed));
        ap.slipTime[axle] = spin > 0.8 && av < 4 ? ap.slipTime[axle] + dt : std::max(0.0, ap.slipTime[axle] - dt);
    }
    const bool ledge = ap.needDist < 10 && ap.needStep > 0.22 * R;
    const bool bigLedge = ap.needDist < 7 && ap.needStep > 0.5 * R;
    const double steerPlan = std::fabs(ap.onRoute ? c.steer : std::atan(ap.kappa * ap.body.wb));
    const bool wantRear = (c.lowRange && (ledge || ap.lineRoll > 0.12 || ap.slipTime[1] > 0.3 || ap.stuckCount > 0)) || ap.slipTime[1] > 0.8;
    const bool wantFront = c.frontEngaged() && (bigLedge || ap.stuckCount > 0 || ap.slipTime[0] > 0.5) && steerPlan < 0.3;
    for (int axle = 0; axle < 2; axle++) {
        const bool front = axle == 0;
        ap.lockTime[axle] = (front ? wantFront : wantRear) ? 5.0 : ap.lockTime[axle] - dt;
        bool on = ap.lockTime[axle] > 0 && av < 25 / 3.6;
        if (front && (std::fabs(c.steer) > 0.4 || steerPlan > 0.4)) on = false;  // a locked front axle won't turn (and snaps CVs)
        if (on != (front ? c.lockFront : c.lockRear)) c.setLocker(front, on);
    }
    // Crawling up a ledge: let the wheels work (no traction-control throttle cuts). In low
    // range with ABS, traction control only brakes a spinning wheel, which helps: leave it on.
    c.tractionControl = c.lowRange && !c.spec->absEnabled && ap.needDist < 8 && ap.needStep > 0.15 ? false : ap.tcWas;
    return false;
}

}  // namespace

double pilotCanClimb(const Car& car, bool setUp) {
    const Caps k = capsFor(car, car.pilot);
    return setUp ? k.best : k.now[k.cur];
}

std::string pilotStart(Car& car, const World& w, const V3& goal, const std::string& name, double speed, int route) {
    Autopilot& ap = car.pilot;
    const int follow = ap.followCar;
    double scale[4];
    std::copy(ap.capScale, ap.capScale + 4, scale);
    const bool sameBuild = ap.body.build == car.buildCount;
    ap = Autopilot();
    if (sameBuild) std::copy(scale, scale + 4, ap.capScale);  // what it learned about this truck still holds
    ap.followCar = follow;
    ap.goal = goal;
    ap.goalName = name;
    ap.speed = speed;
    ap.route = route;
    ap.tcWas = car.tractionControl;
    car.ai = false;
    if (!planRoute(w, *car.spec, car.frame().com, goal, route, ap.avoid, ap.path, ap.capScale[bestLevel(*car.spec)])) {
        ap.status = "NO ROUTE TO " + name;
        return "AUTOPILOT: NO WAY THROUGH TO " + name + (route != PILOT_ANYWHERE ? " (TRY ANYWHERE)" : "");
    }
    ap.active = true;
    ap.lastPos = car.frame().com;
    double length = 0;
    for (size_t k = 1; k < ap.path.size(); k++) length += len(ap.path[k] - ap.path[k - 1]);
    char buf[128];
    std::snprintf(buf, sizeof buf, "AUTOPILOT: %s, %.0f M, %s", name.c_str(), length, pilotRouteName(route));
    return buf;
}

bool pilotDrive(Car& c, const World& w, double dt, const std::vector<Car>* cars) {
    Autopilot& ap = c.pilot;
    if (!ap.active || ap.path.size() < 2) return false;
    const Frame fr = c.frame();
    const V3 pos = fr.com;
    const double v = dot(fr.vel, fr.fwd);
    auto replan = [&] {
        std::vector<V3> np;
        if (planRoute(w, *c.spec, pos, ap.goal, ap.route, ap.avoid, np, ap.capScale[bestLevel(*c.spec)])) ap.path = np, ap.idx = 0;
    };
    // Where along the route are we? (Search ahead of the last spot.)
    {
        int best = ap.idx;
        double bd = 1e18;
        for (int k = ap.idx; k < std::min((int)ap.path.size(), ap.idx + 40); k++) {
            const double d = std::hypot(ap.path[k].x - pos.x, ap.path[k].z - pos.z);
            if (d < bd) bd = d, best = k;
        }
        ap.idx = best;
        if (bd > 15 && ap.backTime <= 0) replan();  // knocked well off the route: plan again from here
    }
    const double toGoal = std::hypot(ap.goal.x - pos.x, ap.goal.z - pos.z);
    const bool follow = ap.followCar >= 0;
    if (toGoal < (follow ? 12.0 : 5.0) || (!follow && ap.idx >= (int)ap.path.size() - 2 && toGoal < 12)) {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = std::fabs(v) < 0.5 ? 1 : 0;
        c.airing = 0;
        if (!follow) {
            ap.active = false;
            ap.arrived = true;
            ap.status = "ARRIVED: " + ap.goalName;
            ap.thought.clear();
            ap.line.clear();
            ap.options.clear();
            ap.seen.clear();
            c.tractionControl = ap.tcWas;
            return false;
        }
        ap.status = "WAITING NEAR " + ap.goalName;
        return true;
    }
    // Look ahead along the route.
    auto along = [&](double dist) {
        int k = ap.idx;
        double acc = 0;
        while (k + 1 < (int)ap.path.size() && acc < dist) {
            acc += len(ap.path[k + 1] - ap.path[k]);
            k++;
        }
        return k;
    };
    // What's ahead, and which line to take through it.
    ap.senseTime -= dt;
    if (ap.senseTime <= 0) {
        ap.senseTime = 0.1;
        sense(c, ap, w, cars, v);
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s  %.0f M", ap.goalName.c_str(), toGoal);
    ap.status = buf;
    const double Ld = clampd(3.5 + std::fabs(v) * 0.9, 4.5, 16);
    const V3 target = ap.path[along(Ld)];
    // Speed: what was asked, less for bends, steep descents, rough ground, the line ahead,
    // and to stop at the end.
    double vt = ap.speed;
    {
        double turn = 0;
        const int a = ap.idx, b = along(30);
        for (int k = a + 1; k + 1 <= b; k++) {
            const V3 d0 = ap.path[k] - ap.path[k - 1], d1 = ap.path[k + 1 < (int)ap.path.size() ? k + 1 : k] - ap.path[k];
            const double h0 = std::atan2(d0.z, d0.x), h1 = std::atan2(d1.z, d1.x);
            turn = std::max(turn, std::fabs(wrapAngle(h1 - h0)) / std::max(0.5, len(d0)));
        }
        // How much grip there is ahead (the slipperiest bit): bends and stops take that much less.
        double grip = 1;
        for (int k = ap.idx; k <= b; k++) grip = std::min(grip, surfaceGrip(w.surfaceAt(ap.path[k].x, ap.path[k].z)));
        const double gripK = clampd(grip / 0.8, 0.12, 1.0);
        if (turn > 1e-4) vt = std::min(vt, std::sqrt((c.spec->transfer != XFER_NONE ? 2.5 : 4.0) * gripK / turn));
        if (grip < 0.2) vt = std::min(vt, 30 / 3.6);        // ice
        else if (grip < 0.45) vt = std::min(vt, 45 / 3.6);  // snow, mud
        double worstGrade = 0, rough = 0;
        for (int k = ap.idx; k < b; k++) {
            const double gr = (ap.path[k + 1].y - ap.path[k].y) / std::max(0.5, std::hypot(ap.path[k + 1].x - ap.path[k].x, ap.path[k + 1].z - ap.path[k].z));
            worstGrade = std::min(worstGrade, gr);
            const Surface s = w.surfaceAt(ap.path[k].x, ap.path[k].z);
            rough = std::max(rough, s == SURF_ROCK || s == SURF_SCREE ? 1.0 : s == SURF_GRASS ? 0.4 : 0.0);
        }
        if (worstGrade < -0.12) vt = std::min(vt, 5.0);
        if (rough >= 1) vt = std::min(vt, 3.5);
        else if (rough > 0.3) vt = std::min(vt, 14.0);
        vt = std::min(vt, std::sqrt(2 * 2.0 * gripK * std::max(0.0, toGoal - 4)) + 1.0);
        vt = std::min(vt, ap.lineSpeed);
    }
    // Backing out of a stuck spot.
    if (ap.backTime > 0) {
        ap.backTime -= dt;
        if (v > 0.5) {  // still rolling forward: stop first
            c.throttle = 0, c.brake = 1;
        } else {
            // In low range (on ledges, likely): at a walk, not bouncing back down them.
            c.gear = -1;
            const double vMax = c.lowRange ? 1.3 : 4.0;
            c.brake = -v > vMax + 0.5 ? 0.4 : 0;
            c.throttle = -v > vMax ? 0 : c.lowRange ? 0.4 : 0.55;
        }
        c.handbrake = 0;
        c.steer = ap.backSteer;
        c.airing = 0;
        ap.status = "BACKING UP";
        if (ap.backTime <= 0) c.gear = 1;
        return true;
    }
    if (c.gear < 0) c.gear = 1;
    // Transfer case, lockers, sway bar, tires.
    const bool hold = setUp(c, ap, w, v, dt, vt);
    // Nothing ahead it can take: back off and find another way.
    if (ap.blocked && !hold) vt = 0;
    // Crawling: a walk up to and over each ledge, in first.
    const bool atLedge = c.lowRange && ap.needDist < 8 && ap.needStep > 0.12;
    if (atLedge) vt = std::min(vt, 1.2);
    ap.holdFirst = c.lowRange && vt < 2.6;
    // Steering: along the route (pure pursuit), or the line it picked.
    {
        const V3 d = target - pos;
        const double x = dot(d, fr.fwd), y = dot(d, fr.left);
        const double alpha = std::atan2(y, x);
        if (ap.onRoute) c.steer = clampd(std::atan2(2 * c.wheelbase * std::sin(alpha), Ld), -0.6, 0.6);
        else c.steer = clampd(std::atan(ap.kappa * ap.body.wb), -0.6, 0.6);
        if (std::fabs(alpha) > 1.2) vt = std::min(vt, 2.5);  // pointing the wrong way: turn round slowly
    }
    if (hold) {
        c.throttle = 0;
        c.brake = 1;
        c.handbrake = std::fabs(v) < 0.3 ? 1 : 0;
        ap.stuckTime = 0;
        ap.push = 0;
        return true;
    }
    const double e = vt - v;
    // Up against a ledge in a crawl: lean on the throttle until it moves.
    if (vt > 0.3 && vt < 2.5 && v < 0.4 * vt) ap.push = std::min(c.lowRange ? 0.75 : 0.5, ap.push + (c.lowRange ? 0.3 : 0.2) * dt);
    else ap.push = std::max(0.0, ap.push - 0.6 * dt);
    c.throttle = vt <= 0.05 ? 0 : clampd(0.25 + 0.35 * e + ap.push, 0, 1);
    // Brakes: gently for the speed asked, firmly coming up on something to crawl over.
    c.brake = vt <= 0.05 ? 1 : vt < 3.5 ? (e < -0.3 ? clampd(-0.6 * e, 0, 1) : 0) : (e < -1.0 ? clampd(-0.3 * e, 0, 1) : 0);
    if (c.brake > 0) c.throttle = 0;
    c.handbrake = 0;
    // Stuck? (Pushing but not moving, or nowhere to go.)
    if ((c.throttle > 0.3 && std::fabs(v) < 0.35) || ap.blocked) ap.stuckTime += dt * (ap.blocked ? 2 : 1);
    else ap.stuckTime = std::max(0.0, ap.stuckTime - dt * 0.5);
    // Against a ledge in low range on full tires: it's set up wrong rather than beaten. Air
    // down (any front locker comes in with stuckCount) and try it again before backing off.
    if (ap.stuckTime > 4.5 && atLedge && c.tirePsi > 24 && c.spec->transfer != XFER_NONE) {
        ap.stuckTime = 0;
        ap.stuckCount++;
        ap.push = 0;
        ap.airTarget = 20;
    }
    if (ap.stuckTime > (atLedge ? 4.5 : 3.0)) {
        ap.stuckTime = 0;
        ap.stuckCount++;
        ap.backTime = 2.5;
        ap.backSteer = -c.steer;
        ap.push = 0;
        // Up against a ledge it reckoned it could climb: it can't, in this setup.
        const int cur = currentLevel(c);
        if (ap.needDist < 3.5 && ap.needStep > 0.05 && cur == bestLevel(*c.spec)) {
            const Caps k = capsFor(c, ap);
            ap.capScale[cur] = std::min(ap.capScale[cur], ap.capScale[cur] * 0.92 * ap.needStep / std::max(0.05, k.now[cur]));
        }
        // Stuck here before (or blocked): plan round it.
        bool again = false;
        for (const V3& a : ap.avoid) again |= len(a - pos) < 8;
        if ((ap.stuckCount >= 2 || ap.blocked) && !again) {
            ap.avoid.push_back(ap.path[along(4)]);
            replan();
        }
    }
    if (!ap.thought.empty() && ap.status.find("SHIFT") == std::string::npos) ap.status += "   " + ap.thought;
    return true;
}
