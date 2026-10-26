// Trail routing for the Backcountry's trail network (docs/plans/big-maps.md 3.6): proposes each trail's way over the land, the way a
// trail crew would walk it (an A* over 4 m cells: gentle grades first, steeper only where the rating allows, off the cliffs, out of
// the water except at its fords), and prints the control points as the C++ table src/bctrails_routes.inc. The game never routes:
// it carves the fixed table, so every machine builds the same map. Run it again after changing the land or the list below, look at
// the picture, adjust the via points, and paste.
//   build/trailroute [--png-dir DIR] > src/bctrails_routes.inc
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

#include "../src/onemap.h"
#include "../src/parallel.h"
#include "../src/places.h"
#include "../src/props.h"
#include "../src/rivers.h"
#include "../src/world.h"

namespace {

struct Via {
    double x, z;
    bool ford = false;  // a ford of Elk River: the trail crosses it straight, north-south, through (x, its middle line)
    int hairpin = 0;    // a switchback's hairpin here, turning round to the east (+1) or the west (-1)
    double r = 9;       // (its radius; a ford: how far either side of the river it runs straight)
    bool line = false;  // straight on from here to the next via (no search: a shelf cut along a cliff)
    const char* join = nullptr;  // the point of this earlier trail nearest (x, z): where this one meets it
};
Via join(const char* trail, double x, double z) {
    Via v{x, z};
    v.join = trail;
    return v;
}
Via ford(double x, double half) { return {x, elkRiverZ(x), true, 0, half}; }
Via pin(double x, double z, int turn, double r = 9, bool line = false) { return {x, z, false, turn, r, line}; }
Via shelf(double x, double z) { return {x, z, false, 0, 9, true}; }
struct Spec {
    const char* id;
    int rating;  // 0 green, 1 blue, 2 black, 3 red
    double maxGrade;
    std::vector<Via> via;
};

V3 place(const char* id) {
    const MapPlace* p = findPlace(MAP_BACKCOUNTRY, id);
    if (!p) std::fprintf(stderr, "no place %s\n", id);
    return p ? p->p : V3();
}
Via at(const char* id, double dx = 0, double dz = 0) {
    const V3 p = place(id);
    return {p.x + dx, p.z + dz};
}

// The trails, in the order they're laid (a later one meeting an earlier one leaves it alone).
std::vector<Spec> specs() {
    return {
        // ---- Red: COPPER PASS, from Pass Hut over the Notch, across Copper Creek, up the switchbacks and the Shelf to the saddle
        // between Copper Peak and the Thumb, and down the north side to the Surveyor's Cabin ----
        {"COPPER PASS", 3, 0.23, {{91, 496}, {70, 630}, {60, 860}, {60, 905}, pin(250, 950, 1), pin(30, 1010, -1), pin(250, 1070, 1), pin(40, 1130, -1),
                                  pin(255, 1185, 1, 9, true), shelf(210, 1216), shelf(176, 1227), shelf(143, 1242), shelf(112, 1260), shelf(82, 1281), {62, 1312},
                                  {40, 1350}, {33, 1403}, {-20, 1600}, at("BC.SURVEYOR_CABIN", 10, -16)}},
        {"COPPER PEAK SPUR", 2, 0.27, {{40, 1408}, {300, 1545}}},
        // ---- Green: the gravel two-tracks that link the districts ----
        {"RANGER ROAD", 0, 0.11, {{-22, -452}, {-560, -430}, {-790, -120}, ford(-800, 100), at("BC.RANGER", 0, -28)}},
        {"TIMBER ROAD", 0, 0.11, {at("BC.RANGER", 0, 30), {-1180, 800}, at("BC.FUEL_TIMBER", 0, -22), at("BC.HERMIT_CABIN", 22, -10)}},
        {"COPPER VALLEY ROAD", 0, 0.11, {at("BC.RANGER", 18, 24), {-640, 820}, {-200, 870}, {60, 880}, {300, 890}, {800, 1000}, at("BC.HIGH_LAKES_CAMP", -20, -16)}},
        {"LAKESHORE ROAD", 0, 0.11, {{-10, -458}, {-60, -560}, {-300, -800}, at("BC.HOMESTEAD", 20, 10), at("BC.FUEL_LAKE", 24, 6)}},
        {"SOUTH FORK ROAD", 0, 0.11, {{10, -458}, {40, -560}, {120, -900}, at("BC.FUEL_SOUTH", 0, 22)}},
        {"RIM ROAD", 0, 0.11, {{306, -300}, {700, -300}, at("BC.BEAR_CREEK_CAMP", -20, 0), at("BC.FUEL_RIM", -22, 0)}},
        // ---- Blue: moderate trails ----
        {"LAKESHORE CUTOFF", 1, 0.17, {{-790, -120}, {-1000, -600}, at("BC.FUEL_LAKE", 0, 22)}},
        {"FISHERMAN'S TRAIL", 1, 0.17, {at("BC.FUEL_LAKE", -10, -20), at("BC.FISHING_SHACK", 16, 10)}},
        {"BADLANDS TRAIL", 1, 0.17, {at("BC.FUEL_SOUTH", 22, 0), {800, -1500}, {1500, -1450}, {2100, -1650}, {2238, -1720}}},
        {"BEAR CREEK FORD TRAIL", 1, 0.17, {{340, 110}, {600, 80}, ford(796, 50), at("BC.BEAR_CREEK_CAMP", -10, 20)}},
        {"FALLS TRAIL", 1, 0.17, {at("BC.FUEL_RIM", 22, 0), {1700, -150}, {1790, -80}}},
        {"NORTH RIM TRAIL", 1, 0.17, {{796, 100}, {1100, 150}, at("BC.RIM_OVERLOOK", -20, 0)}},
        {"HIGH LAKES TRAIL", 1, 0.17, {at("BC.RIM_OVERLOOK", 0, 20), {1500, 700}, {1500, 1200}, at("BC.HIGH_LAKES_CAMP", 0, -22)}},
        {"LOGGERS TRAIL", 1, 0.17, {at("BC.RANGER", -26, 18), {-1600, 600}, at("BC.LOGGERS_CAMP", 18, 0)}},
        // ---- Black: hard trails ----
        {"COPPER RIDGE TRAIL", 2, 0.24, {at("BC.SURVEYOR_CABIN", -16, -10), at("BC.COPPER_VISTA", 16, 0), at("BC.TIMBER_KNOB", 14, 0)}},
        {"SUMMIT RIDGE TRAIL", 2, 0.24, {at("BC.SURVEYOR_CABIN", 16, -10), {300, 1750}, at("BC.COPPER_SUMMIT", 0, 12)}},
        {"HIGH LAKES PEAK TRAIL", 2, 0.24, {at("BC.HIGH_LAKES_CAMP", 22, 0), at("BC.HIGH_LAKES_PEAK", -12, 0)}},
        // ---- The ways to the obstacle trails (bcSetPieces), the fire lookout and the cabins ----
        {"TIMBER TRAIL WAY IN", 1, 0.17, {at("BC.LOGGERS_CAMP", -10, 18), {-2050, 978}}},
        {"TIMBER TRAIL WAY OUT", 1, 0.17, {{-2050, 1628}, join("TIMBER ROAD", -1300, 1600)}},
        {"GULLY ROAD", 1, 0.17, {join("RIM ROAD", 700, -300), {700, -598}}},
        {"GULLY WAY OUT", 1, 0.17, {{700, -1238}, {808, -1080}}},
        {"CRAWL WAY OUT", 1, 0.17, {{1508, -1080}, at("BC.PROSPECTOR_CABIN", -10, -18)}},
        {"PROSPECTOR TRAIL", 1, 0.17, {at("BC.FUEL_RIM", 0, -22), at("BC.PROSPECTOR_CABIN", 0, 22)}},
        {"BALD KNOB TRAIL", 2, 0.24, {join("COPPER VALLEY ROAD", -680, 900), at("BC.BALD_KNOB", 0, -16)}},
        // ---- Faint two-tracks: everywhere else, so there's always a way to follow ----
        {"SLAB CITY TRACK", 4, 0.2, {join("COPPER VALLEY ROAD", -842, 551), {-842, 514}}},
        {"BALANCED ROCK TRACK", 4, 0.2, {join("COPPER VALLEY ROAD", -411, 889), {-411, 874}}},
        {"TORS TRACK", 4, 0.2, {join("SOUTH FORK ROAD", -150, -1050), {-122, -1050}}},
        {"LINE SHACK TRACK", 4, 0.2, {at("BC.LOGGERS_CAMP", -18, -10), at("BC.LINE_SHACK", 18, 0)}},
        {"BEAVER POND TRACK", 4, 0.2, {join("TIMBER ROAD", -1600, 1150), at("BC.BEAVER_POND_CABIN", 18, 0)}},
        {"MOUNTAIN HUT TRACK", 4, 0.2, {join("SUMMIT RIDGE TRAIL", 349, 1994), at("BC.MOUNTAIN_HUT", 0, 16)}},
        {"STOCK TRAIL", 4, 0.2, {join("RANGER ROAD", -790, -200), {-1500, -250}, {-2000, -400}, {-2350, -550}}},
        {"OLD SKID ROAD", 4, 0.2, {at("BC.HERMIT_CABIN", -22, 10), {-1700, 1750}, {-2100, 1850}}},
        {"BADLANDS TWO-TRACK", 4, 0.2, {join("BADLANDS TRAIL", 1500, -1450), {1950, -1150}, at("BC.BEAR_CREEK_CAMP", 10, -20)}},
        {"HIGH LAKES TWO-TRACK", 4, 0.2, {at("BC.HIGH_LAKES_CAMP", 0, 22), {1700, 1900}, {2200, 2150}}},
        {"SOUTH UPLANDS TWO-TRACK", 4, 0.2, {join("SOUTH FORK ROAD", 120, -1100), {-200, -1500}, {-600, -1900}, {-1000, -2200}}},
        {"LAKE LOOP TRACK", 4, 0.2, {at("BC.FISHING_SHACK", -16, 0), {-1500, -2150}, {-2000, -2250}}},
        {"COPPER RIDGE TWO-TRACK", 4, 0.2, {at("BC.COPPER_VISTA", 0, 20), {-700, 1900}, {-1000, 2300}}},
        {"CANYON RIM TRACK", 4, 0.2, {at("BC.RIM_OVERLOOK", 20, 0), {2000, 300}, {2400, 500}}},
    };
}

struct Grid {
    double x0, z0, C;
    int nx, nz;
    std::vector<float> h;
    std::vector<uint8_t> wet, banned, easy;
    std::vector<float> rough;
    int idx(int i, int j) const { return i * nz + j; }
    double X(int i) const { return x0 + i * C; }
    double Z(int j) const { return z0 + j * C; }
    int cellOf(double x, double z) const {
        const int i = std::min(nx - 1, std::max(0, (int)std::lround((x - x0) / C))), j = std::min(nz - 1, std::max(0, (int)std::lround((z - z0) / C)));
        return idx(i, j);
    }
};

// Within the ground an obstacle trail's sections take (and a margin), except at its two ends (where trails lead to it).
bool inSetPiece(double x, double z, bool endsOk) {
    for (const BcSetPiece& p : bcSetPieces()) {
        const V3 d = norm(p.exit - p.entry), l(-d.z, 0, d.x), q(x - p.entry.x, 0, z - p.entry.z);
        const double a = dot(q, d), b = std::fabs(dot(q, l)), L = len(p.exit - p.entry);
        if (a > -30 && a < L + 30 && b < 70) {
            if (endsOk && (std::hypot(x - p.entry.x, z - p.entry.z) < 60 || std::hypot(x - p.exit.x, z - p.exit.z) < 60)) return false;
            return true;
        }
    }
    return false;
}
bool nearRoad(const World& w, double x, double z, double r) {
    for (const std::vector<V3>& road : w.regionRoads)
        for (size_t k = 0; k < road.size(); k += 4)
            if (std::fabs(road[k].x - x) < r && std::fabs(road[k].z - z) < r && std::hypot(road[k].x - x, road[k].z - z) < r) return true;
    return false;
}

std::vector<V3> route(const World& w, const Spec& sp, const Via& a, const Via& b, std::vector<V3>& all) {
    Grid g;
    g.C = 4;
    const double M = 350, H = 2620;
    g.x0 = std::max(-H, std::min(a.x, b.x) - M);
    const double x1 = std::min(H, std::max(a.x, b.x) + M);
    g.z0 = std::max(-H, std::min(a.z, b.z) - M);
    const double z1 = std::min(H, std::max(a.z, b.z) + M);
    g.nx = (int)((x1 - g.x0) / g.C) + 1, g.nz = (int)((z1 - g.z0) / g.C) + 1;
    const size_t N = (size_t)g.nx * g.nz;
    g.h.resize(N), g.wet.resize(N), g.banned.resize(N), g.easy.resize(N), g.rough.assign(N, 0);
    for (int i = 0; i < g.nx; i++)
        for (int j = 0; j < g.nz; j++) {
            const double x = g.X(i), z = g.Z(j);
            const int k = g.idx(i, j);
            g.h[k] = (float)w.height(x, z);
            const bool fordNear = (a.ford && std::hypot(x - a.x, z - a.z) < 30) || (b.ford && std::hypot(x - b.x, z - b.z) < 30);
            g.wet[k] = w.waterLevel(x, z) > g.h[k] - 0.3 && !fordNear;
            const bool oldMap = std::max(std::fabs(x), std::fabs(z)) < 535;
            const bool viaNear = std::hypot(x - a.x, z - a.z) < 90 || std::hypot(x - b.x, z - b.z) < 90;
            const bool viaClose = std::hypot(x - a.x, z - a.z) < 60 || std::hypot(x - b.x, z - b.z) < 60;
            g.banned[k] = (oldMap && !viaNear && w.roadAt(x, z).near) || (!viaClose && nearRoad(w, x, z, 70)) || inSetPiece(x, z, viaClose);
            g.easy[k] = std::hypot(x - a.x, z - a.z) < 45 || std::hypot(x - b.x, z - b.z) < 45;
        }
    for (int i = 1; i + 1 < g.nx; i++)
        for (int j = 1; j + 1 < g.nz; j++) {
            float m = 0;
            const int k = g.idx(i, j);
            for (int di = -1; di <= 1; di++)
                for (int dj = -1; dj <= 1; dj++) m = std::max(m, std::fabs(g.h[k] - g.h[g.idx(i + di, j + dj)]) / (float)(g.C * std::hypot(di, dj) + 1e-9));
            g.rough[k] = m;
        }
    // Where earlier trails run: a little cheaper to share, so the network doesn't run two trails side by side.
    std::vector<uint8_t> used(N, 0);
    for (const V3& p : all)
        if (p.x >= g.x0 && p.x <= x1 && p.z >= g.z0 && p.z <= z1) used[g.cellOf(p.x, p.z)] = 1;
    const int start = g.cellOf(a.x, a.z), goal = g.cellOf(b.x, b.z);
    std::vector<float> cost(N, 1e30f);
    std::vector<int> from(N, -1);
    using Q = std::pair<float, int>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> open;
    auto guess = [&](int k) { return (float)std::hypot(g.X(k / g.nz) - b.x, g.Z(k % g.nz) - b.z); };
    cost[start] = 0, open.push({guess(start), start});
    const double mg = sp.maxGrade;
    while (!open.empty()) {
        const auto [f, k] = open.top();
        open.pop();
        if (k == goal) break;
        if (f > cost[k] + guess(k) + 1e-3f) continue;
        const int i = k / g.nz, j = k % g.nz;
        for (int di = -2; di <= 2; di++)
            for (int dj = -2; dj <= 2; dj++) {
                if (!di && !dj) continue;
                if (std::abs(di) == 2 && std::abs(dj) == 2) continue;
                if ((std::abs(di) == 2 || std::abs(dj) == 2) && (std::abs(di) + std::abs(dj)) % 2 == 0) continue;  // (16 directions)
                const int ii = i + di, jj = j + dj;
                if (ii < 0 || jj < 0 || ii >= g.nx || jj >= g.nz) continue;
                const int q = g.idx(ii, jj);
                if ((g.wet[q] || g.banned[q]) && q != goal) continue;
                const double L = g.C * std::hypot(di, dj), grade = std::fabs(g.h[q] - g.h[k]) / L;
                if (grade > (g.easy[q] ? 0.4 : mg * 1.5)) continue;
                const double over = std::max(0.0, grade - mg);
                const double c = L * (1 + 25 * grade * grade / (mg * mg) * 0.15 + 400 * over * over + 3 * g.rough[q] * g.rough[q] / (mg * mg) * 0.1 - (used[q] ? 0.3 : 0));
                if (cost[k] + c < cost[q]) cost[q] = (float)(cost[k] + c), from[q] = k, open.push({cost[q] + guess(q), q});
            }
    }
    std::vector<V3> path;
    if (from[goal] < 0) {
        std::fprintf(stderr, "%s: no way from %.0f,%.0f to %.0f,%.0f\n", sp.id, a.x, a.z, b.x, b.z);
        return {V3(a.x, 0, a.z), V3(b.x, 0, b.z)};
    }
    for (int k = goal; k >= 0; k = from[k]) path.push_back(V3(g.X(k / g.nz), g.h[k], g.Z(k % g.nz)));
    std::reverse(path.begin(), path.end());
    path.front() = V3(a.x, w.height(a.x, a.z), a.z), path.back() = V3(b.x, w.height(b.x, b.z), b.z);
    return path;
}

}  // namespace

int main(int argc, char** argv) {
    const char* png = nullptr;
    for (int k = 1; k + 1 < argc; k++)
        if (!std::strcmp(argv[k], "--ppm")) png = argv[k + 1];
    props::ScopedRockScans none(nullptr);
    parallelRowsCap = 2;  // (a dev tool: it needn't take the whole machine)
    World w(MAP_BACKCOUNTRY);
    for (int k = 1; k + 3 < argc; k++)
        if (!std::strcmp(argv[k], "--settle")) {  // --settle x z clear [x z clear ...]: the nearest open, gentle, dry spot `clear` m from any trail
            for (int a = k + 1; a + 2 < argc; a += 3) {
                const double x0 = std::atof(argv[a]), z0 = std::atof(argv[a + 1]), clear = std::atof(argv[a + 2]);
                double bx = x0, bz = z0, best = 1e18;
                for (double r = 0; r <= 300; r += 6)
                    for (int s = 0; s < std::max(1, (int)(r * 6.283 / 6)); s++) {
                        const double ang = s * 6.283 / std::max(1, (int)(r * 6.283 / 6)), x = x0 + r * std::cos(ang), z = z0 + r * std::sin(ang);
                        bool ok = w.waterLevel(x, z) < w.height(x, z) - 1;
                        double steep = 0;
                        for (double dx = -clear; ok && dx <= clear; dx += 4)
                            for (double dz = -clear; ok && dz <= clear; dz += 4) {
                                if (dx * dx + dz * dz > clear * clear) continue;
                                if (w.roadAt(x + dx, z + dz).near || w.waterLevel(x + dx, z + dz) > w.height(x + dx, z + dz) - 0.5) ok = false;
                                steep = std::max(steep, std::fabs(w.height(x + dx, z + dz) - w.height(x, z)) / std::max(1.0, std::hypot(dx, dz)));
                            }
                        if (!ok || steep > 0.35) continue;
                        const double score = r + 150 * steep;
                        if (score < best) best = score, bx = x, bz = z;
                    }
                std::fprintf(stderr, "settle %.0f,%.0f -> %.0f,%.0f  (h %.1f)\n", x0, z0, bx, bz, w.height(bx, bz));
            }
            return 0;
        }
    for (int k = 1; k + 4 < argc; k++)
        if (!std::strcmp(argv[k], "--line")) {  // --line x0 z0 x1 z1: the land along it every 4 m
            const double x0 = std::atof(argv[k + 1]), z0 = std::atof(argv[k + 2]), x1 = std::atof(argv[k + 3]), z1 = std::atof(argv[k + 4]);
            const int n = (int)(std::hypot(x1 - x0, z1 - z0) / 4);
            for (int i = 0; i <= n; i++) {
                const double x = x0 + (x1 - x0) * i / n, z = z0 + (z1 - z0) * i / n;
                std::fprintf(stderr, "%6.0f %6.0f  h %6.1f water %6.1f\n", x, z, w.height(x, z), w.waterLevel(x, z));
            }
            return 0;
        }
    std::vector<V3> all;
    std::vector<std::vector<V3>> laid;
    std::printf("// The Backcountry's trail network: control points (x, z) every ~20 m, from tools/trailroute.cpp (generated: re-run it and\n");
    std::printf("// paste; hand edits are fine). Each: id, rating (0 green, 1 blue, 2 black, 3 red), max grade, points.\n");
    std::vector<std::pair<std::string, std::vector<V3>>> done;  // (the trails laid so far, for joins)
    for (Spec sp : specs()) {
        for (Via& q : sp.via)
            if (q.join) {
                bool found = false;
                for (const auto& d : done)
                    if (d.first == q.join) {
                        V3 best = d.second[0];
                        for (const V3& p : d.second)
                            if (std::hypot(p.x - q.x, p.z - q.z) < std::hypot(best.x - q.x, best.z - q.z)) best = p;
                        q.x = best.x, q.z = best.z, found = true;
                    }
                if (!found) std::fprintf(stderr, "%s: no trail %s to join\n", sp.id, q.join);
            }
        // Each via as the stretch it fixes (a ford straight across, a hairpin's half circle; else just its point), the A* between.
        std::vector<std::vector<V3>> fixedAt;
        for (size_t v = 0; v < sp.via.size(); v++) {
            const Via& q = sp.via[v];
            std::vector<V3> f;
            const double prevZ = v ? sp.via[v - 1].z : q.z - 1, nextZ = v + 1 < sp.via.size() ? sp.via[v + 1].z : q.z + 1;
            const double dir = nextZ >= prevZ ? 1 : -1;  // (north or south)
            if (q.ford) {
                for (double t = -q.r; t <= q.r + 0.1; t += 4) f.push_back(V3(q.x, 0, q.z + dir * t));
            } else if (q.hairpin) {
                for (int k = 0; k <= 8; k++) {
                    const double a = -M_PI / 2 + M_PI * k / 8;  // (from below round the far side to above, climbing north)
                    f.push_back(V3(q.x + q.hairpin * q.r * std::cos(a), 0, q.z + q.r * std::sin(a)));
                }
                if (dir < 0) std::reverse(f.begin(), f.end());
            } else
                f.push_back(V3(q.x, 0, q.z));
            for (V3& p : f) p.y = w.height(p.x, p.z);
            fixedAt.push_back(f);
        }
        std::vector<V3> way = fixedAt[0];
        std::vector<uint8_t> held(way.size(), 1);
        for (size_t v = 0; v + 1 < sp.via.size(); v++) {
            const V3 a = fixedAt[v].back(), b = fixedAt[v + 1].front();
            std::vector<V3> leg = sp.via[v].line ? std::vector<V3>{a, b} : route(w, sp, {a.x, a.z}, {b.x, b.z}, all);
            for (size_t k = 1; k + 1 < leg.size(); k++) way.push_back(leg[k]), held.push_back(0);
            for (size_t k = 0; k < fixedAt[v + 1].size(); k++) way.push_back(fixedAt[v + 1][k]), held.push_back(1);
        }
        // The cells' stair-steps smoothed out (the vias' stretches held as they are).
        for (int pass = 0; pass < 4; pass++) {
            const std::vector<V3> was = way;
            for (size_t k = 1; k + 1 < way.size(); k++)
                if (!held[k]) way[k].x = 0.25 * was[k - 1].x + 0.5 * was[k].x + 0.25 * was[k + 1].x, way[k].z = 0.25 * was[k - 1].z + 0.5 * was[k].z + 0.25 * was[k + 1].z;
        }
        // Every 5th cell (about 20 m) and the vias' own points.
        std::vector<V3> ctl;
        double len = 0, climb = 0, steep = 0;
        for (size_t k = 0; k < way.size(); k++) {
            if (k) {
                const double L = std::hypot(way[k].x - way[k - 1].x, way[k].z - way[k - 1].z);
                len += L, climb += std::max(0.0, way[k].y - way[k - 1].y);
                if (L > 0) steep = std::max(steep, std::fabs(way[k].y - way[k - 1].y) / L);
            }
            bool isVia = false;
            for (const Via& v : sp.via) isVia |= std::hypot(way[k].x - v.x, way[k].z - v.z) < 0.5 || (v.hairpin && std::hypot(way[k].x - v.x, way[k].z - v.z) < v.r + 0.5);
            if (k % 5 == 0 || k + 1 == way.size() || isVia) ctl.push_back(way[k]);
        }
        all.insert(all.end(), way.begin(), way.end());
        done.push_back({sp.id, way});
        laid.push_back(way);
        std::fprintf(stderr, "%-24s %5.0f m, climbs %4.0f m, steepest cell step %.2f\n", sp.id, len, climb, steep);
        std::printf("{\"%s\", %d, %.2f, {", sp.id, sp.rating, sp.maxGrade);
        for (size_t k = 0; k < ctl.size(); k++) std::printf("%s{%.0f, %.0f}", k ? ", " : "", ctl[k].x, ctl[k].z);
        std::printf("}},\n");
    }
    if (png) {
        const double R = 2700, C = 4;
        const int n = (int)(2 * R / C);
        std::vector<unsigned char> img((size_t)n * n * 3);
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) {
                const double x = -R + (i + 0.5) * C, z = R - (j + 0.5) * C, h = w.height(x, z);
                const double dx = w.height(x + 2, z) - w.height(x - 2, z), dz = w.height(x, z + 2) - w.height(x, z - 2), s = std::hypot(dx, dz) / 4;
                double c[3] = {0.25 + 0.6 * std::min(1.0, h / 240), 0.45 + 0.4 * std::min(1.0, h / 240), 0.25 + 0.6 * std::min(1.0, h / 240)};
                const double sh = std::max(0.4, std::min(1.4, 1.0 + (-dx + dz) * 0.3));
                for (double& v : c) v *= sh;
                if (s > 0.6) c[0] = 0.45, c[1] = 0.12, c[2] = 0.1;
                if (w.waterLevel(x, z) > h + 0.05) c[0] = 0.1, c[1] = 0.3, c[2] = 0.9;
                if (std::fmod(h + 1000, 25) < 0.6) c[0] *= 0.6, c[1] *= 0.6, c[2] *= 0.6;
                unsigned char* o = &img[((size_t)j * n + i) * 3];
                for (int k = 0; k < 3; k++) o[k] = (unsigned char)std::min(255.0, 255 * c[k]);
            }
        static const unsigned char col[5][3] = {{40, 255, 60}, {40, 120, 255}, {10, 10, 10}, {255, 30, 20}, {230, 200, 140}};
        const std::vector<Spec> sps = specs();
        for (size_t t = 0; t < laid.size(); t++)
            for (const V3& p : laid[t]) {
                const int i = (int)((p.x + R) / C), j = (int)((R - p.z) / C);
                for (int a = -1; a <= 1; a++)
                    for (int b = -1; b <= 1; b++)
                        if (i + a >= 0 && j + b >= 0 && i + a < n && j + b < n) std::memcpy(&img[((size_t)(j + b) * n + i + a) * 3], col[sps[t].rating], 3);
            }
        for (const MapPlace& p : mapPlaces(MAP_BACKCOUNTRY)) {
            const int i = (int)((p.p.x + R) / C), j = (int)((R - p.p.z) / C);
            for (int a = -3; a <= 3; a++)
                for (int b = -3; b <= 3; b++)
                    if (i + a >= 0 && j + b >= 0 && i + a < n && j + b < n) {
                        unsigned char* o = &img[((size_t)(j + b) * n + i + a) * 3];
                        o[0] = 255, o[1] = 255, o[2] = p.kind == PK_CACHE ? 0 : 255;
                    }
        }
        FILE* f = std::fopen(png, "wb");
        std::fprintf(f, "P6 %d %d 255\n", n, n);
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
    }
}
