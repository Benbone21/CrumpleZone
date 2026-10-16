// The Wilderness: a world generated from a seed (World::seed). The same seed always makes the
// same world; every seed makes a different one. (After OpenCode's first draft of the map.)
//
// Every world has the same cast, laid out afresh:
//  - BASE CAMP on the map's edge, where you start.
//  - A mountain massif across the map from it: a summit trail spirals up from the MOUNTAIN HUT at
//    its foot, through the forest, onto rock and snow, to a lookout.
//  - A desert basin of sand dunes, with the DESERT OUTPOST on its edge and the DUNE RUN looping
//    through it (a whoops section in the middle of it).
//  - A river from the mountain's flank down to a lake, with real water and current. Wherever a
//    road crosses it there's a FORD (shallow, gravel bed, posts either side).
//  - A lake with LAKESIDE LODGE on its shore; RIVER BOTTOMS, a mud trail with bog holes, runs up
//    the river from it.
//  - A mesa: cliffs all round, the MESA CLIMB up a gully of rock ledges to its flat top.
//  - A gravel road loop linking the four stations (the old paved highway between camp and the hut),
//    graded, round the lake and the mesa rather than through them.
//  - Forest, meadows, boulders on the steep ground, logs and stumps; signs, map boards, cabins.
// Its named places (World::landmarks) feed the autopilot's list, discoveries and a career's caches.
// Coordinates: x east, z north, metres.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "world.h"

namespace {

using Poly = std::vector<V3>;

double sq(double v) { return v * v; }

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

// A polyline resampled every `step` metres (as backcountry.cpp).
Poly resample(const Poly& pts, double step) {
    Poly out{pts[0]};
    double carry = 0;
    for (size_t k = 0; k + 1 < pts.size(); k++) {
        const V3 a = pts[k], b = pts[k + 1];
        const double L = std::hypot(b.x - a.x, b.z - a.z);
        if (L < 1e-9) continue;
        double s = step - carry;
        for (; s <= L; s += step) out.push_back(a + (b - a) * (s / L));
        carry = L - (s - step);
    }
    if (std::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > 0.2 * step) out.push_back(pts.back());
    return out;
}
Poly smooth(const Poly& ctl) { return resample(splinePoints(ctl, false), 1.0); }
V3 pointAlong(const Poly& pts, double s) {
    const int k = (int)clampd(std::floor(s), 0, (double)pts.size() - 2);
    return pts[k] + (pts[k + 1] - pts[k]) * clampd(s - k, 0, 1);
}
V3 dirAlong(const Poly& pts, double s) {
    const V3 d = pointAlong(pts, s + 1.5) - pointAlong(pts, s - 1.5);
    const double L = std::hypot(d.x, d.z);
    return L > 1e-9 ? V3(d.x / L, 0, d.z / L) : V3(1, 0, 0);
}
double flatDist(const V3& a, const V3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
V3 flatDir(const V3& from, const V3& to) {
    const double L = flatDist(from, to);
    return L > 1e-9 ? V3((to.x - from.x) / L, 0, (to.z - from.z) / L) : V3(1, 0, 0);
}

}  // namespace

// ================================================================================================
void World::buildWilderness() {
    map = MAP_WILDERNESS;
    crashProps = false;
    hN = (int)(2 * kHalfSize / kRes) + 1;
    const int N = hN * hN;
    auto X = [&](int i) { return -kHalfSize + i * kRes; };
    auto cellI = [&](double v) { return std::min(std::max((int)std::lround((v + kHalfSize) / kRes), 0), hN - 1); };
    auto at = [&](double x, double z) -> int { return cellI(x) * hN + cellI(z); };

    Rng rng{seed * 2654435761u + 0x9E3779B9u};
    for (int k = 0; k < 8; k++) rng();
    auto between = [&](double a, double b) { return rng.range(a, b); };
    // Where in the noise this world's land comes from.
    const double ox = between(-900, 900), oz = between(-900, 900), ox2 = between(-900, 900), oz2 = between(-900, 900);

    // ---- The layout: where everything goes -------------------------------------------------------
    // Base camp near the edge; the mountain across the map from it; desert, lake and mesa wherever
    // they fit with room between them.
    const double a0 = between(0, 2 * M_PI);
    const V3 camp(clampd(415 * std::cos(a0), -430, 430), 0, clampd(415 * std::sin(a0), -430, 430));
    const double aM = a0 + M_PI + between(-0.6, 0.6), dM = between(110, 200);
    const V3 M(dM * std::cos(aM), 0, dM * std::sin(aM));
    const double Rm = between(230, 280), Hm = between(120, 165);
    // A site at least `room` from each of the others (the most room it can find in 400 tries).
    auto site = [&](double lim, const std::vector<std::pair<V3, double>>& away) {
        V3 best;
        double bestSlack = -1e9;
        for (int t = 0; t < 400; t++) {
            const V3 p(between(-lim, lim), 0, between(-lim, lim));
            double slack = 1e9;
            for (const auto& a : away) slack = std::min(slack, flatDist(p, a.first) - a.second);
            if (slack > bestSlack) bestSlack = slack, best = p;
            if (slack > 40) break;
        }
        return best;
    };
    const double Rd = between(125, 160);
    const V3 D = site(360, {{M, Rm + Rd * 0.6}, {camp, 230}});
    const double Rl = between(44, 62);
    const V3 L = site(380, {{M, Rm + 50}, {D, Rd + 90}, {camp, 170}});
    const double Rp = between(52, 72), Hp = between(22, 32);
    const V3 P = site(390, {{M, Rm + 60}, {D, Rd + 40}, {L, Rl * 2 + 90}, {camp, 170}});
    const double windA = between(0, M_PI);

    // The mountain's outline wanders with the bearing (no perfect cones).
    auto mtnR = [&](double x, double z) {
        const double ang = std::atan2(z - M.z, x - M.x);
        return Rm * (0.8 + 0.4 * valueNoise(std::cos(ang) * 1.5 + ox, std::sin(ang) * 1.5 + oz));
    };

    // ---- The land ---------------------------------------------------------------------------------
    hgrid.resize(N);
    for (int i = 0; i < hN; i++)
        for (int j = 0; j < hN; j++) {
            const double x = X(i), z = X(j);
            // Rolling country, with hills in it.
            double h = 8 + 16 * (fbmNoise(x * 0.005 + ox, z * 0.005 + oz, 4) - 0.5) + 4 * (fbmNoise(x * 0.02 + ox2, z * 0.02 + oz2, 3) - 0.5);
            h += 10 * smoothstep(0.55, 0.8, fbmNoise(x * 0.008 + oz, z * 0.008 - ox, 3)) * ridgedNoise(x * 0.012 + ox2, z * 0.012 + oz2, 3);
            // The mountain: ridged crests on a broad base, foothills round it.
            const double dm = std::hypot(x - M.x, z - M.z), rr = mtnR(x, z);
            const double cone = std::max(0.0, 1 - dm / rr);
            h += Hm * std::pow(cone, 1.35) * (0.72 + 0.56 * ridgedNoise(x * 0.006 + ox, z * 0.006 + oz, 4));
            h += 16 * smoothstep(rr * 1.7, rr * 0.9, dm) * ridgedNoise(x * 0.011 + oz2, z * 0.011 + ox2, 3);
            // The desert: a shallow basin, dunes running across the wind.
            const double dd = std::hypot(x - D.x, z - D.z);
            if (dd < Rd * 1.3) {
                const double u = x * std::cos(windA) + z * std::sin(windA), v = -x * std::sin(windA) + z * std::cos(windA);
                const double crest = std::pow(1 - std::fabs(std::sin(0.05 * u + 1.4 * std::sin(0.012 * v))), 2.0);
                const double dunes = 4.2 * crest * (0.5 + 0.5 * valueNoise(x * 0.02 + ox, z * 0.02)) + 0.8 * valueNoise(x * 0.07, z * 0.07 + oz);
                h += -5 * smoothstep(Rd * 1.25, Rd * 0.6, dd) + dunes * smoothstep(Rd * 0.95, Rd * 0.55, dd);
            }
            // The mesa: a flat top, cliffs all round, a skirt of talus.
            const double dp = std::hypot(x - P.x, z - P.z) + 9 * (valueNoise(x * 0.03 + ox2, z * 0.03) - 0.5);
            h += Hp * smoothstep(Rp, Rp - 7, dp) + 4 * smoothstep(Rp + 22, Rp, dp) * smoothstep(Rp - 9, Rp, dp);
            // The lake's hollow: the land falls to it.
            h -= 8 * smoothstep(Rl * 3.2, Rl * 0.8, std::hypot(x - L.x, z - L.z));
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

    // ---- Tools (as backcountry.cpp) ---------------------------------------------------------------
    std::vector<float> sd(N, 1e9f), sk(N, 0), sl(N, 0);
    std::vector<int> cells;
    auto scanNear = [&](const Poly& pts, double reach, auto fn) {
        cells.clear();
        const int n = (int)pts.size();
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = pts[s], &b = pts[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz, Ls = std::sqrt(L2);
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
                    sd[id] = (float)d, sk[id] = (float)(s + t), sl[id] = (float)((dx * (z - a.z) - dz * (x - a.x)) / Ls);
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
    struct Pin {
        int idx;
        double y;
    };
    // The land under a polyline smoothed over `window` points either side, then no steeper than
    // `maxGrade` (cut and fill); pinned heights eased into.
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
        for (int pass = 0; pass < 6; pass++)
            for (int dirn = 0; dirn < 2; dirn++)
                for (int q = 1; q < n; q++) {
                    const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                    bool pinned = false;
                    for (const Pin& p : pins) pinned |= p.idx == k;
                    if (pinned) continue;
                    const double run = std::hypot(pts[k].x - pts[prev].x, pts[k].z - pts[prev].z);
                    y[k] = clampd(y[k], y[prev] - maxGrade * run, y[prev] + maxGrade * run);
                }
        return y;
    };
    // Lays a trail: the tread at the heights `y` (painted by paintTread), the land within `blend`
    // past its edge easing into it. Where an earlier trail is nearer, that one is left alone.
    auto layTrail = [&](const Poly& pts, const std::vector<double>& y, double half, double blend, auto paintTread) {
        scanNear(pts, half + blend, [&](int id, double d, double k, double lat) {
            const double e = d - half;
            if (edge[id] < e || waterY[id] > -1e8) return;  // (a trail laid after the river leaves its water alone)
            const double w = e < 1.0 ? 1.0 : smoothstep(blend, 1.0, e);
            hgrid[id] = (float)(hgrid[id] + (yAt(y, k) - hgrid[id]) * w);
            edge[id] = (float)e;
            roadLat[id] = (float)lat, roadAlong[id] = (float)k;
            if (e < 0.5) paintTread(id, lat, k), roadNear[id] = 1;
            if (e < 3) keep[id] = 1;
        });
    };
    auto levelDisc = [&](const V3& c, double r, double y, double blend, uint8_t pnt, uint8_t sf, bool keepTreads = false) {
        for (int i = cellI(c.x - r - blend); i <= cellI(c.x + r + blend); i++)
            for (int j = cellI(c.z - r - blend); j <= cellI(c.z + r + blend); j++) {
                const double d = std::hypot(X(i) - c.x, X(j) - c.z);
                if (d > r + blend) continue;
                const int id = i * hN + j;
                if (keepTreads && d > r && edge[id] < 3.5) continue;
                hgrid[id] = (float)(hgrid[id] + (y - hgrid[id]) * smoothstep(r + blend, r, d));
                if (d < r - 0.5 && pnt != 255) paint[id] = pnt, surf[id] = sf;
                if (d < r + blend * 0.5) keep[id] = 1;
            }
    };
    auto padHeight = [&](const V3& c) {
        double sum = 0;
        int cnt = 0;
        for (double dx = -12; dx <= 12; dx += 4)
            for (double dz = -12; dz <= 12; dz += 4) sum += height(c.x + dx, c.z + dz), cnt++;
        return sum / cnt;
    };
    // Rock steps up a trail's bed: the profile rises `h` over 1.5 m and a slab stands on the lip.
    auto addSteps = [&](const Poly& pts, std::vector<double>& y, const std::vector<std::pair<double, double>>& steps, double halfW, const V3& col) {
        const std::vector<double> y0 = y;
        for (const auto& st : steps)
            for (size_t k = 0; k < y.size(); k++) y[k] += st.second * clampd((k - st.first) / 1.5, 0, 1);
        for (const auto& st : steps) {
            const double s = st.first, g = (yAt(y0, s + 3) - yAt(y0, s)) / 3, pitch = std::atan(g), thick = 1.4;
            const V3 c = pointAlong(pts, s + 0.75), d = dirAlong(pts, s + 0.75);
            const double top = yAt(y, s + 1.5) - g * 0.75;
            addBox(V3(c.x, top - thick / 2 * std::cos(pitch), c.z), V3(0.85, thick / 2, halfW), std::atan2(-d.z, d.x), pitch, 0, col);
            boxes.back().rock = true;
        }
    };
    auto dirtTread = [&](int id, double, double) { paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT; };
    auto rockTread = [&](int id, double, double) { paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK; };
    auto gravelTread = [&](int id, double, double) { paint[id] = PAINT_GRAVEL, surf[id] = SURF_GRAVEL; };
    auto pavedTread = [&](int id, double, double) { paint[id] = PAINT_ROAD, surf[id] = SURF_PAVED; };
    auto sandTread = [&](int id, double, double) { paint[id] = PAINT_SAND, surf[id] = SURF_SAND; };
    auto slopeAtCell = [&](int i, int j) {
        if (i < 1 || j < 1 || i >= hN - 1 || j >= hN - 1) return 0.0;
        const double gx = (hgrid[(i + 1) * hN + j] - hgrid[(i - 1) * hN + j]) / (2 * kRes), gz = (hgrid[i * hN + j + 1] - hgrid[i * hN + j - 1]) / (2 * kRes);
        return std::hypot(gx, gz);
    };

    // ---- The stations: level pads ------------------------------------------------------------------
    // The desert outpost on the basin's edge toward camp; the lodge on the lake shore; the hut at the
    // mountain's foot facing camp.
    struct Pad {
        V3 c;
        double r, y, yaw;
        const char* name;
    };
    const V3 outpost = D + flatDir(D, camp) * (Rd + 22);
    const double lodgeA = std::atan2(camp.z - L.z, camp.x - L.x) + between(-0.9, 0.9);
    const V3 lodge = L + V3(std::cos(lodgeA), 0, std::sin(lodgeA)) * (Rl * 1.25 + 26);
    const V3 hut = M + flatDir(M, camp) * (mtnR(M.x + (camp.x - M.x) * 0.01, M.z + (camp.z - M.z) * 0.01) * 0.86);
    std::vector<Pad> pads = {{camp, 15, 0, std::atan2(-(M.z - camp.z), M.x - camp.x), "BASE CAMP"},
                             {outpost, 10, 0, 0, "DESERT OUTPOST"},
                             {lodge, 10, 0, M_PI / 2, "LAKESIDE LODGE"},
                             {hut, 10, 0, 0, "MOUNTAIN HUT"}};
    for (Pad& p : pads) {
        p.y = padHeight(p.c);
        levelDisc(p.c, p.r + 3, p.y, 14, PAINT_PAD, SURF_PAVED);
    }
    auto padY = [&](const V3& c) {
        for (const Pad& p : pads)
            if (flatDist(p.c, c) < 25) return p.y;
        return height(c.x, c.z);
    };

    // ---- The lake: its basin dug, its water on the grid ---------------------------------------------
    addLake(L.x, L.z, Rl, 3.0);
    {
        const Water& w = waters.back();
        for (int i = cellI(L.x - w.r - 10); i <= cellI(L.x + w.r + 10); i++)
            for (int j = cellI(L.z - w.r - 10); j <= cellI(L.z + w.r + 10); j++) {
                const int id = i * hN + j;
                if (hgrid[id] < w.y && std::hypot(X(i) - L.x, X(j) - L.z) < w.r + 8) waterY[id] = (float)w.y, flowX[id] = flowZ[id] = 0, keep[id] = 1;
                else if (hgrid[id] < w.y + 0.6 && std::hypot(X(i) - L.x, X(j) - L.z) < w.r + 12 && paint[id] == PAINT_NONE) paint[id] = PAINT_SAND, surf[id] = SURF_SAND, keep[id] = 1;
            }
    }

    // ---- The roads: a loop through the four stations ------------------------------------------------
    // Each leg a wandering line between its ends, kept out of the lake and off the mesa and the
    // mountain's steep core, then graded.
    auto route = [&](const V3& a, const V3& b, double wander) {
        Poly ctl{a};
        const double len = flatDist(a, b);
        const int n = std::max(2, (int)(len / 70));
        const V3 d = flatDir(a, b), side(-d.z, 0, d.x);
        for (int k = 1; k < n; k++) {
            const double t = k / (double)n;
            V3 p = a + (b - a) * t + side * (wander * (valueNoise(t * 3 + ox, oz) * 2 - 1) * std::sin(M_PI * t));
            auto pushOut = [&](const V3& c, double r) {
                if (flatDist(p, c) < r) p = c + flatDir(c, p) * r;
            };
            pushOut(L, Rl * 1.3 + 24);
            pushOut(P, Rp + 26);
            const double mr = mtnR(p.x, p.z) * 0.62;
            if (flatDist(b, M) > mr + 20 && flatDist(a, M) > mr + 20) pushOut(M, mr);
            ctl.push_back(p);
        }
        ctl.push_back(b);
        return smooth(ctl);
    };
    const Poly legDesert = route(camp, outpost, 60), legLake = route(outpost, lodge, 70), legHut = route(lodge, hut, 60), legHome = route(hut, camp, 50);
    struct Road {
        const Poly* pts;
        V3 a, b;
        bool paved;
    };
    const Road roads[] = {{&legDesert, camp, outpost, false}, {&legLake, outpost, lodge, false}, {&legHut, lodge, hut, false}, {&legHome, hut, camp, true}};
    for (const Road& r : roads) {
        const std::vector<double> y = groundProfile(*r.pts, 14, 0.1, {{0, padY(r.a)}, {(int)r.pts->size() - 1, padY(r.b)}});
        if (r.paved) layTrail(*r.pts, y, 3.6, 16, pavedTread);
        else layTrail(*r.pts, y, 3.4, 16, gravelTread);
    }

    // ---- The desert: sand over the basin floor, hardpan round its edge -----------------------------
    for (int i = cellI(D.x - Rd * 1.2); i <= cellI(D.x + Rd * 1.2); i++)
        for (int j = cellI(D.z - Rd * 1.2); j <= cellI(D.z + Rd * 1.2); j++) {
            const int id = i * hN + j;
            if (paint[id] != PAINT_NONE) continue;
            const double x = X(i), z = X(j), dd = std::hypot(x - D.x, z - D.z) + 18 * (valueNoise(x * 0.03 + ox, z * 0.03 + oz) - 0.5);
            if (dd < Rd * 0.9) paint[id] = PAINT_SAND, surf[id] = SURF_SAND, keep[id] = dd < Rd * 0.8;
            else if (dd < Rd * 1.12) paint[id] = PAINT_DESERT, surf[id] = SURF_DIRT;
        }

    // ---- DUNE RUN: a loop out of the outpost through the dunes, a whoops section on the far side ------
    Poly dunes;
    std::vector<double> dunesY;
    {
        const double aIn = std::atan2(outpost.z - D.z, outpost.x - D.x);
        Poly ctl{outpost};
        const int n = 11;
        for (int k = 0; k <= n; k++) {
            const double a = aIn + 0.25 + (2 * M_PI - 0.5) * k / n, r = Rd * (0.55 + 0.12 * std::sin(k * 1.7 + ox));
            ctl.push_back(D + V3(std::cos(a), 0, std::sin(a)) * r);
        }
        ctl.push_back(outpost + V3(std::cos(aIn + 0.4), 0, std::sin(aIn + 0.4)) * 8);
        dunes = smooth(ctl);
        dunesY = groundProfile(dunes, 3, 0.24, {{0, padY(outpost)}, {(int)dunes.size() - 1, padY(outpost)}});
        // The whoops: a run of rollers 7 m apart, half a metre tall.
        const double k0 = dunes.size() * 0.42, k1 = dunes.size() * 0.58;
        for (size_t k = 0; k < dunesY.size(); k++)
            if (k > k0 && k < k1) dunesY[k] += 0.5 * 0.5 * (1 - std::cos((k - k0) * 2 * M_PI / 7.0)) * smoothstep(k0, k0 + 10, k) * smoothstep(k1, k1 - 10, k);
        layTrail(dunes, dunesY, 3.0, 6, sandTread);
    }

    // ---- The river: from the mountain's flank down to the lake ------------------------------------
    // Found by walking downhill with an eye on the lake; water on the grid, levelled from its banks and
    // always falling, flowing toward the lake. A ford wherever a road crosses it.
    Poly river;
    {
        const V3 toLake = flatDir(M, L);
        V3 p = M + toLake * (mtnR(M.x + toLake.x, M.z + toLake.z) * 0.42) + V3(-toLake.z, 0, toLake.x) * between(-40, 40);
        V3 heading = flatDir(p, L);
        Poly walk{p};
        for (int n = 0; n < 700 && flatDist(p, L) > Rl * 0.9 && waterLevel(p.x, p.z) < height(p.x, p.z); n++) {
            const double e = 3;
            const double gx = (height(p.x + e, p.z) - height(p.x - e, p.z)) / (2 * e), gz = (height(p.x, p.z + e) - height(p.x, p.z - e)) / (2 * e);
            V3 down(-gx, 0, -gz);
            const double dl = std::hypot(down.x, down.z);
            if (dl > 1e-6) down = down / dl;
            const V3 want = flatDir(p, L) * (0.6 + 0.004 * n) + down * 1.0 + heading * 1.2;
            const V3 h2 = flatDir(V3(), want);
            heading = h2;
            p = p + h2 * 4;
            walk.push_back(p);
        }
        walk.push_back(p + flatDir(p, L) * 6);  // (into the lake)
        Poly ctl;
        for (size_t k = 0; k < walk.size(); k += 5) ctl.push_back(walk[k]);
        ctl.push_back(walk.back());
        river = smooth(ctl);
    }
    std::vector<V3> fords;
    {
        // Wherever a road meets it (crossing it, or running along its bed for a stretch): a ford,
        // shallow the whole way.
        for (const Road& r : roads)
            for (size_t k = 0; k < r.pts->size(); k++) {
                const V3 q = (*r.pts)[k];
                double best = 1e9;
                V3 bp;
                for (size_t m = 0; m < river.size(); m++)
                    if (flatDist(q, river[m]) < best) best = flatDist(q, river[m]), bp = river[m];
                if (best > 12) continue;
                bool fresh = true;
                for (const V3& f : fords) fresh &= flatDist(f, bp) > 16;
                if (fresh) fords.push_back(bp);
            }
        const int n = (int)river.size();
        std::vector<double> lvl(n), depth(n), halfW(n), bankSlope(n);
        for (int k = 0; k < n; k++) {
            const double t = k / (double)(n - 1);
            halfW[k] = 2.2 + 5.0 * smoothstep(0, 0.7, t);
            double lo = 1e9;
            const V3 d = dirAlong(river, k);
            for (int o = -1; o <= 1; o += 2) lo = std::min(lo, height(river[k].x - d.z * o * (halfW[k] + 3), river[k].z + d.x * o * (halfW[k] + 3)));
            lvl[k] = lo - 0.5;
            depth[k] = 0.25 + 1.2 * smoothstep(0, 0.6, t);
            bankSlope[k] = 0.45;
            for (const V3& f : fords) {
                const double df = flatDist(river[k], f);
                depth[k] += (0.32 - depth[k]) * smoothstep(30, 12, df);
                bankSlope[k] = std::min(bankSlope[k], 0.45 + (0.15 - 0.45) * smoothstep(55, 25, df));
            }
        }
        for (int pass = 0; pass < 40; pass++)
            for (int k = 1; k + 1 < n; k++) lvl[k] = 0.25 * lvl[k - 1] + 0.5 * lvl[k] + 0.25 * lvl[k + 1];
        for (int k = 1; k < n; k++) lvl[k] = std::min(lvl[k], lvl[k - 1] - 0.004);
        // It runs out into the lake: its last stretch eases down to the lake's level, never below it.
        const double lakeY = waters.back().y;
        for (int k = 0; k < n; k++) {
            const double toEnd = (n - 1 - k) * 1.0;
            lvl[k] = std::max(lvl[k], lakeY);
            lvl[k] += (lakeY - lvl[k]) * smoothstep(30, 0, toEnd);
        }
        // Each cell takes the stretch of river it's nearest (on a steep reach, the next stretch down
        // is lower: carved by it too, the bed sank below its own water).
        struct Near {
            float d = 1e9f, level = 0, dep = 0, wq = 0, slope = 0, fx = 0, fz = 0, reach = 0;
        };
        std::vector<Near> near(N);
        std::vector<int> touched;
        for (int s = 0; s + 1 < n; s++) {
            const V3 &a = river[s], &b = river[s + 1];
            const double dx = b.x - a.x, dz = b.z - a.z, Ls = std::hypot(dx, dz);
            if (Ls < 1e-6) continue;
            const double reach = halfW[s] + 12;
            for (int i = cellI(std::min(a.x, b.x) - reach); i <= cellI(std::max(a.x, b.x) + reach); i++)
                for (int j = cellI(std::min(a.z, b.z) - reach); j <= cellI(std::max(a.z, b.z) + reach); j++) {
                    const double x = X(i), z = X(j);
                    const double t = clampd(((x - a.x) * dx + (z - a.z) * dz) / (Ls * Ls), 0, 1);
                    const double dd = std::hypot(x - (a.x + dx * t), z - (a.z + dz * t));
                    const int id = i * hN + j;
                    if (dd > reach || dd >= near[id].d) continue;
                    if (near[id].d > 1e8f) touched.push_back(id);
                    Near& q = near[id];
                    q.d = (float)dd, q.reach = (float)reach;
                    q.level = (float)(lvl[s] + (lvl[s + 1] - lvl[s]) * t), q.dep = (float)(depth[s] + (depth[s + 1] - depth[s]) * t);
                    q.wq = (float)(halfW[s] * (0.85 + 0.3 * valueNoise(x * 0.05, z * 0.05))), q.slope = (float)bankSlope[s];
                    const double fall = (lvl[s] - lvl[s + 1]) / Ls, u = std::min(1.0, dd / q.wq);
                    const double sp = std::min(2.4, 1.6 * (0.6 + 40 * fall)) * (1 - 0.6 * u * u) * (q.dep < 0.6 ? 0.7 : 1.0);
                    q.fx = (float)(dx / Ls * sp), q.fz = (float)(dz / Ls * sp);
                }
        }
        for (int id : touched) {
            const Near& q = near[id];
            if (q.d < q.wq) {
                const double u = q.d / q.wq;
                hgrid[id] = (float)std::min((double)hgrid[id], q.level - q.dep * (1 - u * u) - 0.05);
                surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                waterY[id] = q.level, flowX[id] = q.fx, flowZ[id] = q.fz;
                keep[id] = 1;
            } else {
                const double bank = q.level + 0.2 + (q.d - q.wq) * q.slope;
                if (hgrid[id] > bank) hgrid[id] = (float)(hgrid[id] + (bank - hgrid[id]) * smoothstep(q.reach, q.wq, q.d));
                if (q.d < q.wq + 2.5 && paint[id] == PAINT_NONE) surf[id] = SURF_GRAVEL, paint[id] = PAINT_GRAVEL;
                if (q.d < q.wq + 4) keep[id] = 1;
            }
        }
    }

    // ---- RIVER BOTTOMS: a mud trail up the river's bank from the lodge, bog holes in it -------------
    Poly bottoms;
    std::vector<double> bogS;
    {
        // Upstream from where the river meets the lake, 16 m off its bank on the lodge's side.
        const int n = (int)river.size();
        int k0 = n - 1;
        while (k0 > 0 && flatDist(river[k0], L) < Rl * 1.4 + 14) k0--;
        const int k1 = std::max(0, k0 - 300);
        const V3 d0 = dirAlong(river, k0), l0(-d0.z, 0, d0.x);
        const double side = dot(lodge - river[k0], l0) >= 0 ? 1 : -1;
        Poly ctl{lodge};
        for (int k = k0; k >= k1; k -= 30) {
            const V3 d = dirAlong(river, k), l(-d.z, 0, d.x);
            ctl.push_back(river[k] + l * (side * (2.2 + 5.0 * smoothstep(0, 0.7, k / (double)(n - 1)) + 14)));
        }
        bottoms = smooth(ctl);
        std::vector<double> y = groundProfile(bottoms, 6, 0.14, {{0, padY(lodge)}});
        layTrail(bottoms, y, 2.6, 12, dirtTread);
        const int m = (int)bottoms.size();
        for (int b = 0; b < 3; b++) bogS.push_back(m * (0.3 + 0.22 * b));
        for (int b = 0; b < 3; b++) {
            const double Lb = 9 + 3 * b, R = Lb / 2 + 8, rim = yAt(y, bogS[b]), depthB = 0.45 + 0.15 * b;
            scanNear(bottoms, R, [&](int id, double d, double k, double) {
                const double u = k - bogS[b];
                const double sideD = d - 2.0 * (valueNoise(X(id / hN) * 0.3 + b * 7, X(id % hN) * 0.3) - 0.5) - 1.2 * smoothstep(Lb / 2, 0, std::fabs(u));
                const double f = smoothstep(-Lb / 2, -Lb / 2 + 4, u) * smoothstep(Lb / 2, Lb / 2 - 4, u) * smoothstep(7.5, 4.0, sideD);
                if (f <= 0 || len(V3(flowX[id], 0, flowZ[id])) > 0) return;
                const double ybot = rim - depthB + 0.12 * smoothstep(2.6, 6, d);
                hgrid[id] = (float)(hgrid[id] + (std::min((double)hgrid[id], ybot) - hgrid[id]) * f);
                paint[id] = PAINT_MUD, surf[id] = SURF_MUD, keep[id] = 1;
                if (f > 0.6 && hgrid[id] < rim - 0.08 && waterY[id] < -1e8) waterY[id] = (float)(rim - 0.07), flowX[id] = flowZ[id] = 0;
            });
        }
        // Mud the whole way, ruts in it.
        scanNear(bottoms, 4.0, [&](int id, double d, double k, double lat) {
            if (d > 3.2 || k < 15 || edge[id] > 0.6 || waterY[id] > -1e8) return;
            const double g = std::exp(-sq((std::fabs(lat) - 1.0) / 0.55)) * smoothstep(3.2, 2.0, d);
            hgrid[id] -= (float)(0.12 * g);
            if (valueNoise(k * 0.05 + ox, 1.5) > 0.35) paint[id] = PAINT_MUD, surf[id] = SURF_MUD;
        });
    }

    // ---- SUMMIT TRAIL: spirals up from the hut at a steady grade to a lookout -----------------------
    Poly summit;
    {
        V3 pos = hut + flatDir(hut, M) * 14;
        const double turn = rng() < 0.5 ? 1 : -1;
        const double peak = height(M.x, M.z), target = std::min(peak - 6, height(pos.x, pos.z) + Hm * 0.7);
        Poly walk{hut, pos};
        for (int n = 0; n < 1600 && height(pos.x, pos.z) < target; n++) {
            const V3 in = flatDir(pos, M), tangent(-in.z * turn, 0, in.x * turn);
            double best = 1e9;
            V3 next = pos;
            for (int k = -12; k <= 12; k++) {
                const double ang = k * (65.0 / 12) * M_PI / 180;
                const V3 d(tangent.x * std::cos(ang) - tangent.z * std::sin(ang), 0, tangent.x * std::sin(ang) + tangent.z * std::cos(ang));
                const V3 q = pos + d * 2.0;
                const double g = (height(q.x, q.z) - height(pos.x, pos.z)) / 2.0;
                const double score = std::fabs(g - 0.15) + 0.03 * std::fabs(ang) + (flatDist(q, M) > flatDist(pos, M) + 0.5 ? 0.05 : 0);
                if (score < best) best = score, next = q;
            }
            pos = next;
            walk.push_back(pos);
        }
        Poly ctl;
        for (size_t k = 0; k < walk.size(); k += 6) ctl.push_back(walk[k]);
        ctl.push_back(walk.back());
        summit = smooth(ctl);
        std::vector<double> y = groundProfile(summit, 6, 0.2, {{0, padY(hut)}});
        layTrail(summit, y, 2.8, 10, [&](int id, double, double k) {
            if (k > summit.size() * 0.45) paint[id] = PAINT_ROCK, surf[id] = SURF_ROCK;
            else paint[id] = PAINT_TRAIL, surf[id] = SURF_DIRT;
        });
        levelDisc(summit.back(), 6, y.back(), 4, PAINT_ROCK, SURF_ROCK, true);
    }

    // ---- MESA CLIMB: from the nearest road up a gully of rock ledges to the flat top ----------------
    Poly mesa;
    {
        V3 from = camp;
        double best = 1e9;
        for (const Road& r : roads)
            for (const V3& q : *r.pts)
                if (flatDist(q, P) < best) best = flatDist(q, P), from = q;
        const V3 d = flatDir(from, P);
        const V3 top = P - d * (Rp * 0.35);
        mesa = smooth({from, from + (top - from) * 0.5 + V3(-d.z, 0, d.x) * 6, top});
        const int n = (int)mesa.size();
        const double y0 = height(from.x, from.z), y1 = height(top.x, top.z);
        std::vector<double> y = groundProfile(mesa, 2, 0.32, {{0, y0}});
        // The cliff band: wherever the ramp can't keep up with the land, ledges make up the rest.
        std::vector<std::pair<double, double>> steps;
        double owed = 0;
        for (int k = 1; k < n; k++) {
            const double land = height(mesa[k].x, mesa[k].z) + owed;
            if (land - y[k] > 0.34 && owed < y1 - y0) {
                steps.push_back({(double)k, 0.32});
                owed += 0.32;
                k += 3;
            }
        }
        for (int k = 0; k < n; k++) y[k] = std::min(y[k], y1);
        addSteps(mesa, y, steps, 3.2, V3(0.5, 0.36, 0.28));
        for (double& v : y) v = std::min(v, y1 + 0.05);
        layTrail(mesa, y, 3.0, 4, rockTread);
        levelDisc(top, 7, y1, 6, PAINT_ROCK, SURF_ROCK, true);
    }

    // ---- ROCK GARDEN: a spur off the summit trail, strewn with boulders ----------------------------
    Poly garden;
    {
        const double ks = std::min(60.0, summit.size() * 0.2);
        const V3 s0 = pointAlong(summit, ks), d = dirAlong(summit, ks), out(d.z, 0, -d.x);
        garden = smooth({s0, s0 + out * 40 + d * 15, s0 + out * 75 + d * 10, s0 + out * 100 - d * 10});
        layTrail(garden, groundProfile(garden, 4, 0.22, {{0, height(s0.x, s0.z)}}), 2.8, 6, rockTread);
    }

    // The stations, flat again where the roads ran into them.
    for (const Pad& p : pads) levelDisc(p.c, p.r + 3, p.y, 9, PAINT_PAD, SURF_PAVED, true);

    // ---- Paint the land: snow up high, bare rock on the steep, scree, mud by the water -----------------
    const double snowAt = height(M.x, M.z) - Hm * 0.35;
    for (int i = 1; i < hN - 1; i++)
        for (int j = 1; j < hN - 1; j++) {
            const int id = i * hN + j;
            const double x = X(i), z = X(j), h = hgrid[id];
            const double snowline = snowAt + 10 * (fbmNoise(x * 0.02 + ox, z * 0.02 + oz, 3) - 0.5);
            if ((paint[id] == PAINT_TRAIL || paint[id] == PAINT_ROCK) && h > snowline && flatDist(V3(x, 0, z), M) < Rm) {
                if (paint[id] == PAINT_TRAIL || valueNoise(x * 0.1, z * 0.1) > 0.4) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
                continue;
            }
            if (paint[id] != PAINT_NONE || surf[id] != SURF_GRASS || waterY[id] > -1e8) continue;
            const double slope = slopeAtCell(i, j);
            if (h > snowline && slope < 0.85) paint[id] = PAINT_SNOW, surf[id] = SURF_SNOW;
            else if (slope > 0.85) surf[id] = SURF_ROCK, paint[id] = PAINT_ROCK;
            else if (h > 45 && slope > 0.5 && valueNoise(x * 0.04 + ox, z * 0.04) > 0.55) paint[id] = PAINT_SCREE, surf[id] = SURF_SCREE;
        }

    // ---- Boulders: the rock garden's, the summit's, the mesa's talus, the desert's outcrops, the hills' ---
    uint32_t rockSeed = seed * 7919u + 3000u;
    auto boulder = [&](const V3& p, double r, double top, float tint = 1.0f) {
        const double flat = between(0.6, 1.0);
        addRock(p.x, p.z, V3(r, r * flat, r * between(0.7, 1.0)), std::min(top, r * flat * 1.7), rockSeed++, tint);
    };
    for (double k = 10; k < (double)garden.size() - 4; k += 1.0) {
        const V3 p = pointAlong(garden, k), d = dirAlong(garden, k), l(-d.z, 0, d.x);
        if (rng() < 0.35) boulder(p + l * between(-2.2, 2.2), between(0.35, 0.65), between(0.16, 0.36), (float)between(0.9, 1.1));
        if (rng() < 0.4) boulder(p + l * ((rng() < 0.5 ? -1 : 1) * between(3.4, 6)), between(0.8, 1.8), 1.4, 0.95f);
    }
    for (int k = 0; k < 9; k++) {  // the summit's cairn-ringed top
        const double a = k * 2 * M_PI / 9 + between(-0.2, 0.2);
        boulder(summit.back() + V3(std::cos(a), 0, std::sin(a)) * between(7, 10), between(0.8, 1.6), 1.2, 1.05f);
    }
    for (int attempt = 0; attempt < 4000; attempt++) {  // the mesa's talus
        const double a = between(0, 2 * M_PI), r = Rp + between(-2, 16);
        const V3 p = P + V3(std::cos(a), 0, std::sin(a)) * r;
        const int id = at(p.x, p.z);
        if (keep[id] || waterY[id] > -1e8 || rng() > 0.03) continue;
        boulder(p, between(0.6, 2.4), 1.8, (float)between(1.0, 1.2));
    }
    for (int attempt = 0; attempt < 3000; attempt++) {  // desert outcrops
        const V3 p = D + V3(between(-Rd, Rd), 0, between(-Rd, Rd));
        const int id = at(p.x, p.z);
        if (keep[id] || flatDist(p, D) > Rd || rng() > 0.025) continue;
        boulder(p, between(0.6, 2.0), 1.6, (float)between(1.1, 1.3));
    }
    for (int attempt = 0; attempt < 40000 && rocks.size() < 900; attempt++) {  // over the hills
        const double x = (rng() * 2 - 1) * 515, z = (rng() * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || edge[id] < 4) continue;
        const double slope = slopeAtCell(cellI(x), cellI(z)), h = hgrid[id];
        if (rng() > 0.08 + 0.9 * smoothstep(0.2, 0.7, slope) + 0.2 * smoothstep(30, 90, h)) continue;
        const bool big = slope > 0.55 && rng() < 0.2;
        const double r = big ? between(2.0, 4.0) : 0.4 + rng() * rng() * 1.7, flat = between(0.5, 0.9);
        addRock(x, z, V3(r, r * flat, r * between(0.7, 1.0)), r * flat * between(0.5, 1.0), rockSeed++, (float)between(0.85, 1.1));
    }

    // ---- Trees: forest by the noise, pines up the mountain to the treeline, broadleaf by the water,
    // the odd snag in the desert; meadows between ---------------------------------------------------
    const double treeline = snowAt - 18;
    for (int attempt = 0; attempt < 220000 && trees.size() < 3200; attempt++) {
        const double x = (rng() * 2 - 1) * 520, z = (rng() * 2 - 1) * 520;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || edge[id] < 4) continue;
        const int i = cellI(x), j = cellI(z);
        const double h = hgrid[id], slope = slopeAtCell(i, j);
        if (h > treeline || slope > 0.8) continue;
        const bool desert = flatDist(V3(x, 0, z), D) < Rd * 1.1;
        if (paint[id] != PAINT_NONE && !(desert && paint[id] == PAINT_DESERT)) continue;
        const double forest = fbmNoise(x * 0.009 + ox2, z * 0.009 + oz2, 3);
        const double nearWater = smoothstep(Rl * 3, Rl * 1.3, flatDist(V3(x, 0, z), L));
        double p = desert ? 0.015 : 0.05 + 0.95 * smoothstep(0.38, 0.58, forest);
        p *= 1 - 0.6 * smoothstep(treeline - 15, treeline, h);
        if (rng() > p) continue;
        Tree t;
        if (desert) t.kind = 2;
        else if (rng() < 0.6 * nearWater + (h < 25 ? 0.3 : 0.05)) t.kind = 1;
        else t.kind = rng() < 0.04 ? 2 : 0;
        t.base = V3(x, h - 0.2, z);
        t.height = t.kind == 0 ? 9 + rng() * 9 : t.kind == 1 ? 7 + rng() * 7 : 5 + rng() * 4;
        t.height *= 1 - 0.3 * smoothstep(40, treeline, h);
        t.radius = t.height * (t.kind == 0 ? 0.2 + rng() * 0.06 : t.kind == 1 ? 0.26 + rng() * 0.08 : 0.15 + rng() * 0.06);
        t.tint = (float)(0.8 + rng() * 0.4);
        trees.push_back(t);
        poles.push_back({x, z, t.base.y - 0.5, t.base.y + t.height * 0.6, t.kind == 1 ? 0.3 : 0.22});
    }
    // Fallen logs and stumps in the woods, bushes everywhere green.
    for (int attempt = 0; attempt < 30000 && logs.size() < 180; attempt++) {
        const double x = (rng() * 2 - 1) * 510, z = (rng() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || fbmNoise(x * 0.009 + ox2, z * 0.009 + oz2, 3) < 0.5) continue;
        const double r = between(0.18, 0.34), Lg = between(4, 9), a = between(0, 2 * M_PI);
        V3 p0(x - std::cos(a) * Lg / 2, 0, z - std::sin(a) * Lg / 2), p1(x + std::cos(a) * Lg / 2, 0, z + std::sin(a) * Lg / 2);
        if (keep[at(p0.x, p0.z)] || keep[at(p1.x, p1.z)]) continue;
        p0.y = height(p0.x, p0.z) + r * 0.8, p1.y = height(p1.x, p1.z) + r * 0.8;
        logs.push_back({p0, p1, r});
    }
    for (int attempt = 0; attempt < 30000 && fixtures.size() < 160; attempt++) {
        const double x = (rng() * 2 - 1) * 510, z = (rng() * 2 - 1) * 510;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || fbmNoise(x * 0.009 + ox2, z * 0.009 + oz2, 3) < 0.48) continue;
        const double r = between(0.22, 0.4), hh = between(0.3, 0.7), y = hgrid[id];
        fixtures.push_back({PROP_STUMP, V3(x, y, z), V3(r, hh, 0), between(0, 6.28)});
        poles.push_back({x, z, y - 0.3, y + hh, r});
    }
    for (int attempt = 0; attempt < 60000 && bushes.size() < 2500; attempt++) {
        const double x = (rng() * 2 - 1) * 515, z = (rng() * 2 - 1) * 515;
        const int id = at(x, z);
        if (keep[id] || waterY[id] > -1e8 || paint[id] != PAINT_NONE || surf[id] != SURF_GRASS || hgrid[id] > treeline) continue;
        if (rng() > 0.15 + 0.6 * smoothstep(0.3, 0.55, fbmNoise(x * 0.009 + ox2, z * 0.009 + oz2, 3))) continue;
        bushes.push_back({V3(x, hgrid[id], z), between(0.4, 1.3), (float)between(0.75, 1.2)});
    }

    // ---- Props: map boards, signs at the trails (green easy .. red extreme), cabins, posts at fords -----
    const V3 green(0.1, 0.55, 0.2), blue(0.1, 0.3, 0.8), black(0.03, 0.03, 0.03), red(0.75, 0.08, 0.06), board(0.3, 0.22, 0.14);
    auto sign = [&](double x, double z, double yaw, const V3& col, PropKind kind = PROP_SIGN) {
        Prop sg{kind, V3(x, height(x, z), z), V3(), yaw};
        sg.col[0] = (float)col.x, sg.col[1] = (float)col.y, sg.col[2] = (float)col.z;
        fixtures.push_back(sg);
        const double half = kind == PROP_MAPBOARD ? 0.8 : 0.0;
        const V3 sd2(std::sin(yaw), 0, std::cos(yaw));
        for (int k = kind == PROP_MAPBOARD ? -1 : 1; k <= 1; k += 2) poles.push_back({x + sd2.x * half * k, z + sd2.z * half * k, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
    };
    auto trailSign = [&](const Poly& pts, double k, double side, const V3& col) {
        const V3 p = pointAlong(pts, k), d = dirAlong(pts, k), l(-d.z, 0, d.x), q = p + l * side;
        sign(q.x, q.z, std::atan2(d.z, -d.x), col);
    };
    auto marker = [&](const V3& p) { fixtures.push_back({PROP_MARKER, V3(p.x, height(p.x, p.z), p.z), V3(), 0}); };
    auto cairn = [&](const V3& p) { fixtures.push_back({PROP_CAIRN, V3(p.x, height(p.x, p.z), p.z), V3(), between(0, 6.28)}); };
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
    for (const Pad& p : pads) {  // each station: a map board; camp gets a cabin and tables too
        const V3 d = flatDir(p.c, &p == &pads[0] ? M : camp), s(-d.z, 0, d.x);
        const V3 b = p.c + s * (p.r + 5);
        sign(b.x, b.z, std::atan2(d.z, -d.x), board, PROP_MAPBOARD);
    }
    {
        const V3 d = flatDir(camp, M), s(-d.z, 0, d.x);
        cabinAt(camp - s * 24 - d * 6, std::atan2(-d.z, d.x));
        table(camp.x - d.x * 20 + s.x * 14, camp.z - d.z * 20 + s.z * 14, 0.3);
        table(camp.x - d.x * 24 + s.x * 18, camp.z - d.z * 24 + s.z * 18, -0.4);
        cabinAt(hut - flatDir(hut, camp) * 14 + V3(flatDir(hut, camp).z, 0, -flatDir(hut, camp).x) * 12, 0.8);
        cabinAt(lodge + flatDir(L, lodge) * 16, std::atan2(lodge.z - L.z, L.x - lodge.x));
    }
    trailSign(summit, 8, 5.0, black);   // SUMMIT TRAIL
    trailSign(dunes, 8, 5.0, blue);     // DUNE RUN
    trailSign(bottoms, 8, 5.0, black);  // RIVER BOTTOMS
    trailSign(mesa, 6, 5.0, red);       // MESA CLIMB
    trailSign(garden, 6, 5.0, red);     // ROCK GARDEN
    for (double s : bogS) trailSign(bottoms, s - 14, -4.5, black);
    std::vector<V3> posted;
    for (const V3& f : fords) {
        bool apart = true;
        for (const V3& q : posted) apart &= flatDist(q, f) > 40;
        if (!apart) continue;
        posted.push_back(f);
        double best = 1e9;
        int k = 0;
        for (int m = 0; m < (int)river.size(); m++)
            if (flatDist(river[m], f) < best) best = flatDist(river[m], f), k = m;
        const V3 d = dirAlong(river, k), l(-d.z, 0, d.x);
        for (int side = -1; side <= 1; side += 2)
            for (int s2 = -1; s2 <= 1; s2 += 2) marker(f + l * (side * 13.5) + d * (s2 * 3.5));
    }
    cairn(summit.back() + V3(2.5, 0, 1.5));
    cairn(mesa.back() + V3(3, 0, -2));

    // ---- Stations, start, places ----------------------------------------------------------------
    routes.push_back({"CAMP ROAD", legDesert, true});
    routes.push_back({"DESERT ROAD", legLake, true});
    routes.push_back({"LAKE ROAD", legHut, true});
    routes.push_back({"OLD HIGHWAY", legHome, true});
    routes.push_back({"SUMMIT TRAIL", summit, false});
    routes.push_back({"DUNE RUN", dunes, false});
    routes.push_back({"RIVER BOTTOMS", bottoms, false});
    routes.push_back({"MESA CLIMB", mesa, false});
    routes.push_back({"ROCK GARDEN", garden, false});
    for (const Pad& p : pads) addStation(V3(p.c.x, p.y, p.c.z), p.yaw, p.name);
    const V3 fwd = flatDir(camp, M), side(-fwd.z, 0, fwd.x);
    spawn = camp - fwd * 6 + side * 8;
    spawn.y = height(spawn.x, spawn.z);
    spawnYaw = std::atan2(-fwd.z, fwd.x);
    parking = {camp - fwd * 6 + side * 14, camp - fwd * 6 + side * 20, camp - fwd * 12 + side * 8};
    parkingYaw = spawnYaw;
    // Named places (the autopilot's list, discoveries), and where a career's caches lie: the
    // harder to reach, the better the find.
    auto place = [&](const char* name, const V3& p, int cache) { landmarks.push_back({name, V3(p.x, height(p.x, p.z), p.z), cache}); };
    place("SUMMIT LOOKOUT", summit.back() + V3(3, 0, 3), 3);
    place("MESA TOP", mesa.back() + flatDir(mesa.front(), mesa.back()) * 12, 3);
    place("THE DUNES", pointAlong(dunes, dunes.size() * 0.5) + V3(6, 0, 4), 2);
    place("WHOOP ALLEY", pointAlong(dunes, dunes.size() * 0.42), -1);
    place("THE BOG", pointAlong(bottoms, bogS.back()) + dirAlong(bottoms, bogS.back()) * 14, 2);
    place("ROCK GARDEN", garden.back(), 1);
    {
        // The river's headwaters, up on the mountain where it starts.
        const V3 d = dirAlong(river, 4), l(-d.z, 0, d.x);
        place("HEADWATERS", river[std::min((size_t)4, river.size() - 1)] + l * 9, 2);
    }
    if (!fords.empty()) {  // (on the bank, where you'd stop to look)
        int k = 0;
        double best = 1e9;
        for (int m = 0; m < (int)river.size(); m++)
            if (flatDist(river[m], fords[0]) < best) best = flatDist(river[m], fords[0]), k = m;
        const V3 d = dirAlong(river, k), l(-d.z, 0, d.x);
        V3 bank = fords[0] + l * 16;
        if (waterLevel(bank.x, bank.z) > height(bank.x, bank.z) - 0.05) bank = fords[0] - l * 16;
        place("THE FORD", bank, -1);
    }
    {
        // The lake's beach, and a few spots out in the woods, well off any road.
        place("LAKE BEACH", L + flatDir(L, lodge) * -1.0 * (Rl * 1.25 + 8), 0);
        const char* woods[] = {"HUNTER'S STAND", "OLD CAMPSITE", "LONE PINE"};
        int found = 0;
        for (int attempt = 0; attempt < 4000 && found < 3; attempt++) {
            const V3 p(between(-470, 470), 0, between(-470, 470));
            const int id = at(p.x, p.z);
            if (keep[id] || waterY[id] > -1e8 || edge[id] < 60 || hgrid[id] > treeline || slopeAtCell(cellI(p.x), cellI(p.z)) > 0.4) continue;
            bool apart = true;
            for (const Landmark& lm : landmarks) apart &= flatDist(lm.p, p) > 90;
            if (!apart) continue;
            place(woods[found], p, found == 0 ? 1 : 0);
            found++;
        }
    }
    for (const Pad& p : pads) landmarks.push_back({p.name, V3(p.c.x, p.y, p.c.z), -1});
}
