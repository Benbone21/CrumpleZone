// The procedural world's biomes (src/procworld.h; owner's notes 2026-10-27): the country near a world's start, how clearly the biomes
// are themselves, the mesa country, and the land going on for ever as you drive out.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "../src/procworld.h"
#include "../src/procrules.h"
#include "harness.h"

using namespace proc;

namespace {
// The dominant biome at a point and its weight (the land not worn: cheap, never waits on an erosion tile).
int domBiome(const ProcWorld& G, double x, double z, double& share) {
    const Macro m = G.macroCached(x, z);
    int best = 0;
    for (int b = 1; b < PB_COUNT; b++)
        if (m.w[b] > m.w[best]) best = b;
    share = m.w[best];
    return best;
}
void writePpm(const std::string& path, int S, const std::vector<unsigned char>& img) {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", S, S);
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
    }
}
}  // namespace

SCENARIO(procBiomeMap, "procworld/biome-map", "(with CZ_PROC_MAPS=dir) a map of the biomes round a seed's start (CZ_MAP_SEED, CZ_MAP_SPAN), for looking at") {
    const char* dir = std::getenv("CZ_PROC_MAPS");
    if (!dir) {
        ctx.note("CZ_PROC_MAPS not set: nothing drawn");
        return;
    }
    const uint64_t seed = std::getenv("CZ_MAP_SEED") ? std::strtoull(std::getenv("CZ_MAP_SEED"), nullptr, 10) : 326904175ull;
    const double span = std::getenv("CZ_MAP_SPAN") ? std::atof(std::getenv("CZ_MAP_SPAN")) : 16000;
    const ProcWorld& G = ProcWorld::forSeed(seed);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    const int S = 640;
    const double step = span / S;
    std::vector<unsigned char> img((size_t)S * S * 3);
    std::vector<double> H((size_t)S * S), W((size_t)S * S);
    std::vector<int> B((size_t)S * S);
    std::vector<std::thread> th;
    for (int t = 0; t < 4; t++)
        th.emplace_back([&, t] {
            for (int py = t; py < S; py += 4)
                for (int px = 0; px < S; px++) {
                    const double x = (double)sx - span / 2 + (px + 0.5) * step, z = (double)sz + span / 2 - (py + 0.5) * step;
                    const Macro m = G.macroCached(x, z);
                    int best = 0;
                    for (int b = 1; b < PB_COUNT; b++)
                        if (m.w[b] > m.w[best]) best = b;
                    const size_t k = (size_t)py * S + px;
                    H[k] = m.h, B[k] = best, W[k] = m.w[best];
                }
        });
    for (auto& t : th) t.join();
    const double col[PB_COUNT][3] = {{0.55, 0.75, 0.3}, {0.15, 0.45, 0.15}, {0.78, 0.3, 0.16}, {0.6, 0.6, 0.65}, {0.95, 0.82, 0.5}};
    for (int py = 0; py < S; py++)
        for (int px = 0; px < S; px++) {
            const size_t k = (size_t)py * S + px;
            const double hx = H[(size_t)py * S + std::min(S - 1, px + 1)], hz = H[(size_t)std::max(0, py - 1) * S + px];
            const double shade = std::clamp(0.85 + 1.0 * ((hx - H[k]) - (hz - H[k])) / step, 0.4, 1.3);
            for (int q = 0; q < 3; q++) {
                double c = col[B[k]][q] * shade;
                if (W[k] < 0.7) c = c * 0.5 + 0.5;  // (a mixed band: washed out)
                img[k * 3 + q] = (unsigned char)std::clamp(c * 255, 0.0, 255.0);
            }
        }
    for (int a = -5; a <= 5; a++)  // (the start: a black cross; white rings every 2 km)
        for (int q = 0; q < 3; q++) img[((size_t)(S / 2) * S + S / 2 + a) * 3 + q] = 0, img[((size_t)(S / 2 + a) * S + S / 2) * 3 + q] = 0;
    for (double r = 2000; r < span / 2; r += 2000)
        for (int k = 0; k < 4000; k++) {
            const double t = k * 2 * 3.14159265 / 4000;
            const int px = S / 2 + (int)(r * std::cos(t) / step), py = S / 2 + (int)(r * std::sin(t) / step);
            if (px >= 0 && py >= 0 && px < S && py < S)
                for (int q = 0; q < 3; q++) img[((size_t)py * S + px) * 3 + q] = 255;
        }
    const std::string path = std::string(dir) + "/biomes_seed" + std::to_string(seed) + ".ppm";
    writePpm(path, S, img);
    ctx.note("start " + std::to_string(sx) + "," + std::to_string(sz));
    ctx.note("wrote " + path + " (meadow light green, forest dark green, mesa red, alpine grey, desert sand; washed out: mixed; rings every 2 km)");
}

SCENARIO(procEndless, "procworld/endless", "The world goes on for ever (owner: \"worlds are just a big square\"): 30 km out in six directions the window holds land, plants, rocks and water like home; its land runs on past its edge into the far land with no step or drop; a window a slide on holds the same land; and a truck set down out there rests on the ground") {
    const uint64_t seed = 326904175;
    const ProcWorld& G = ProcWorld::forSeed(seed);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    const double R = 30000;
    const double dirs[6][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071, 0.7071}, {-0.7071, -0.5}};
    int withWater = 0, withPlants = 0, onGround = 0;
    double worstEdge = 0, worstSlide = 0, worstLocal = 0;
    long edgeN = 0, edgeBad = 0;
    for (int k = 0; k < 6; k++) {
        const double gx = (double)sx + R * dirs[k][0], gz = (double)sz + R * dirs[k][1];
        World w = [&] {
            World::procSeed = seed, World::procOriginSet = true, World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
            World ww(MAP_PROCEDURAL);
            World::procOriginSet = false;
            return ww;
        }();
        const double half = w.halfSize();
        // (Local coordinates stay small however far out: the window's middle is the origin.)
        worstLocal = std::max({worstLocal, std::fabs(w.spawn.x), std::fabs(w.spawn.z)});
        long water = 0;
        double lo = 1e9, hi = -1e9;
        bool finite = true;
        for (double x = -half + 1; x < half - 1; x += 8)
            for (double z = -half + 1; z < half - 1; z += 8) {
                const double h = w.height(x, z);
                finite &= std::isfinite(h);
                lo = std::min(lo, h), hi = std::max(hi, h);
                water += w.waterLevel(x, z) > h + 0.05;
            }
        withWater += water > 50;
        withPlants += w.trees.size() + w.bushes.size() > 500 && w.rocks.size() > 200;
        // (The edge: the window's last metre against the far land's first (the backdrop's), all four sides, every 16 m. The far land is
        // the generator's ground without the trails' earthworks and the places' levelled lots: where one of those crosses the window's
        // edge the two differ by its cut or fill, and the backdrop eases between them over its first 32 m.)
        for (double t = -half + 8; t < half - 8; t += 16)
            for (int side = 0; side < 4; side++) {
                const double ix = side == 0 ? half - 0.5 : side == 1 ? -half + 0.5 : t, iz = side == 2 ? half - 0.5 : side == 3 ? -half + 0.5 : t;
                const double ox = side == 0 ? half + 1.5 : side == 1 ? -half - 1.5 : t, oz = side == 2 ? half + 1.5 : side == 3 ? -half - 1.5 : t;
                const double d = std::fabs(w.height(ix, iz) - farHeight(w, ox, oz));
                worstEdge = std::max(worstEdge, d), edgeN++, edgeBad += d > 3;
            }
        // (A truck set down on level ground near the window's middle comes to rest on it.)
        {
            Sim s;
            s.world = std::move(w);
            V3 at(0, 0, 0);
            double flattest = 1e9;
            for (double x = -300; x <= 300; x += 20)
                for (double z = -300; z <= 300; z += 20) {
                    const double h = s.world.height(x, z);
                    double worst = s.world.waterLevel(x, z) > h - 1 ? 1e6 : 0;
                    for (double a = -8; a <= 8; a += 4)
                        for (double b = -8; b <= 8; b += 4) worst = std::max(worst, std::fabs(s.world.height(x + a, z + b) - h));
                    if (worst < flattest) flattest = worst, at = V3(x, 0, z);
                }
            s.addCar(vehicleCatalog()[3], V3(at.x, s.world.clearY(at.x, at.z) + 0.6, at.z), 0, V3(0.5, 0.5, 0.5));
            Ctx c(s);
            s.cars[0].brake = 1, s.cars[0].handbrake = 1;
            c.run(3.0);
            const V3 p = s.cars[0].frame().com;
            const bool ok = std::isfinite(p.x) && p.y > s.world.height(p.x, p.z) && p.y < s.world.height(p.x, p.z) + 3 && len(s.cars[0].frame().vel) < 1.5;
            onGround += ok;
            if (!ok) ctx.note("truck at " + std::to_string((int)at.x) + "," + std::to_string((int)at.z) + " (level within " + std::to_string(flattest) + " m): " + std::to_string(p.y - s.world.height(p.x, p.z)) + " m over the ground, " + std::to_string(len(s.cars[0].frame().vel)) + " m/s");
            w = std::move(s.world);
        }
        // (The next window a slide on, in two of the directions: the same land and water where they overlap.)
        if (k < 2) {
            World::procSeed = seed, World::procOriginSet = true, World::procOriginX = w.proc.ox + 512 * (k == 0 ? 1 : -1), World::procOriginZ = w.proc.oz + 512;
            World n(MAP_PROCEDURAL);
            World::procOriginSet = false;
            const double dx = (double)(n.proc.ox - w.proc.ox), dz = (double)(n.proc.oz - w.proc.oz);
            for (double x = -900; x <= 900; x += 9)
                for (double z = -900; z <= 900; z += 9) {
                    worstSlide = std::max(worstSlide, std::fabs(w.height(x, z) - n.height(x - dx, z - dz)));
                    worstSlide = std::max(worstSlide, std::fabs(std::max(w.waterLevel(x, z), -1.0) - std::max(n.waterLevel(x - dx, z - dz), -1.0)));
                }
        }
        char line[220];
        std::snprintf(line, sizeof line, "%.0f,%.0f: land %.0f-%.0f m, %ld water cells (8 m), %zu trees, %zu rocks, %zu bushes%s", gx, gz, lo, hi, water, w.trees.size(), w.rocks.size(),
                      w.bushes.size(), finite ? "" : ", NOT FINITE");
        ctx.note(line);
        if (!finite) withPlants = -100;
    }
    ctx.check("every window 30 km out has land and plants and rocks (6 directions)", withPlants == 6, std::to_string(withPlants) + " of 6");
    ctx.check("most of them have water (rivers, creeks, lakes)", withWater >= 3, std::to_string(withWater) + " of 6");
    ctx.metric("the worst step from a far window's edge to the land past it (a lot's or a trail's earthworks: a place levelled on a mountainside)", worstEdge, "m", 0, 80);
    ctx.metric("share of the edge stepping over 3 m", (double)edgeBad / std::max(1L, edgeN), "", 0, 0.12);
    ctx.metric("two far windows a slide apart: the worst difference in land or water where they overlap", worstSlide, "m", 0, 1e-6);
    ctx.metric("the farthest local coordinate of a far window's spawn", worstLocal, "m", 0, 10);
    ctx.check("a truck set down 30 km out rests on the ground (6 of 6)", onGround == 6, std::to_string(onGround) + " of 6");
}

namespace {
// Where each biome is clearly itself (its weight 0.8 or more right across a 300 m patch round a point), nearest a point: rings out
// every 100 m, 72 ways round. (-1: none within `maxR`.)
void nearestClear(const ProcWorld& G, double cx, double cz, double maxR, double out[PB_COUNT]) {
    for (int b = 0; b < PB_COUNT; b++) out[b] = -1;
    for (double r = 100; r <= maxR; r += 100)
        for (int k = 0; k < 72; k++) {
            const double t = k * (2 * 3.14159265358979 / 72), x = cx + r * std::cos(t), z = cz + r * std::sin(t);
            double share;
            const int b = domBiome(G, x, z, share);
            if (out[b] >= 0 || share < 0.8) continue;
            bool clear = true;
            for (int q = 0; q < 8 && clear; q++) {
                double s2;
                clear = domBiome(G, x + 150 * std::cos(q * 0.785398), z + 150 * std::sin(q * 0.785398), s2) == b && s2 >= 0.8;
            }
            if (clear) out[b] = r;
        }
}
}  // namespace

SCENARIO(procHomeBiomes, "procworld/home-biomes", "Plains, desert and mesa country within ~2-4 km of a world's start (owner: today desert and mesa are 8-10 km off), each clearly itself with a short edge, and the start itself in none of them") {
    int good = 0;
    const uint64_t seeds[] = {326904175ull, 1ull, 2ull, 99ull, 123456789ull};
    double worst = 0;
    for (uint64_t seed : seeds) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        int64_t sx, sz;
        double yaw;
        G.start(sx, sz, yaw);
        double d[PB_COUNT];
        nearestClear(G, (double)sx, (double)sz, 8000, d);
        const double far = std::max({d[PB_MEADOW] < 0 ? 1e9 : d[PB_MEADOW], d[PB_DUNES] < 0 ? 1e9 : d[PB_DUNES], d[PB_BADLANDS] < 0 ? 1e9 : d[PB_BADLANDS]});
        worst = std::max(worst, far);
        good += far <= 4200;
        char line[200];
        std::snprintf(line, sizeof line, "seed %llu (start %lld,%lld): clear plains %.0f m, forest %.0f, mesa %.0f, alpine %.0f, desert %.0f", (unsigned long long)seed, (long long)sx,
                      (long long)sz, d[0], d[1], d[2], d[3], d[4]);
        ctx.note(line);
    }
    ctx.check("every seed has plains, desert and mesa country clearly themselves within 4.2 km of its start", good == 5, std::to_string(good) + " of 5");
    ctx.metric("the farthest of the three from a start (5 seeds)", worst, "m", 0, 4200);
    // (Clear edges: along lines out from seed 326904175's start, how far the land is a mix (no biome over 0.7) across each border.)
    const ProcWorld& G = ProcWorld::forSeed(326904175ull);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    long mixed = 0, all = 0;
    for (int k = 0; k < 36; k++)
        for (double r = 0; r < 6000; r += 25) {
            double share;
            domBiome(G, (double)sx + r * std::cos(k * 0.1745329), (double)sz + r * std::sin(k * 0.1745329), share);
            all++, mixed += share < 0.7;
        }
    ctx.metric("share of the land within 6 km of the start that's a mix (no biome over 0.7)", (double)mixed / all, "", 0, 0.12);
}

SCENARIO(procMesaCountry, "procworld/mesa-country", "Mesa country (owner: Monument Valley, greatly expanded): regions several km across, many big mesas and buttes on a flat desert floor, sheer bands over talus, spires; canyons cut deep through them") {
    const ProcWorld& G = ProcWorld::forSeed(326904175ull);
    // (How much of a 40 km square is mesa country, and the biggest region (by flood over a 250 m grid).)
    const int N = 160;
    const double step = 250, x0 = -20000, z0 = -20000;
    std::vector<uint8_t> mesa((size_t)N * N);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            double share;
            mesa[(size_t)i * N + j] = domBiome(G, x0 + i * step, z0 + j * step, share) == PB_BADLANDS && share > 0.7;
        }
    long cells = 0, biggest = 0;
    std::vector<uint8_t> seen((size_t)N * N);
    std::vector<int> stack;
    for (int s = 0; s < N * N; s++) {
        cells += mesa[s];
        if (!mesa[s] || seen[s]) continue;
        long n = 0;
        stack.assign(1, s), seen[s] = 1;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back(), n++;
            const int i = c / N, j = c % N;
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& o : nb) {
                const int a = i + o[0], b = j + o[1];
                if (a < 0 || b < 0 || a >= N || b >= N) continue;
                const int q = a * N + b;
                if (mesa[q] && !seen[q]) seen[q] = 1, stack.push_back(q);
            }
        }
        biggest = std::max(biggest, n);
    }
    ctx.metric("share of 40 km square that's mesa country", (double)cells / (N * N), "", 0.06, 0.4);
    ctx.metric("the biggest region of it", biggest * step * step / 1e6, "km2", 9, 1e9);
    // (Inside the biggest stretch near the start: the floor, the mesas' tops over it, the cliffs.)
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    long floorN = 0, tall = 0, sheer = 0, n = 0;
    double highest = 0;
    for (int i = -60; i <= 60; i++)
        for (int j = -60; j <= 60; j++) {
            const double x = (double)sx + i * 70, z = (double)sz + j * 70;
            double share;
            if (domBiome(G, x, z, share) != PB_BADLANDS || share < 0.9) continue;
            const Macro m = G.macroCached(x, z);
            const double over = m.h - m.mass;  // (over the desert floor's mass)
            n++;
            floorN += over < 25;
            tall += over > 120;
            highest = std::max(highest, over);
            const double s = std::max(std::fabs(G.macroCached(x + 4, z).h - G.macroCached(x - 4, z).h), std::fabs(G.macroCached(x, z + 4).h - G.macroCached(x, z - 4).h)) / 8;
            sheer += s > 2;
        }
    char line[200];
    std::snprintf(line, sizeof line, "%ld points of mesa country within 4 km of the start: %ld on the floor, %ld over 120 m up, %ld sheer (over 2:1), the highest %.0f m over the floor", n, floorN, tall, sheer,
                  highest);
    ctx.note(line);
    ctx.check("there is mesa country within 4 km of the start", n > 200, std::to_string(n));
    ctx.metric("share of it that's open floor", (double)floorN / std::max(1L, n), "", 0.3, 0.85);
    ctx.metric("share of it that's mesa or butte top or side over 120 m up", (double)tall / std::max(1L, n), "", 0.06, 0.6);
    ctx.metric("share of it in sheer cliff bands (over 2:1)", (double)sheer / std::max(1L, n), "", 0.01, 0.3);
    ctx.metric("the tallest mesa over its floor", highest, "m", 200, 500);
    // (Trails to the tops: the trail nodes up on mesas within ~6 km of the start, and the trails that reach them.)
    const int64_t ci = floorDiv(sx, kCell), cj = floorDiv(sz, kCell);
    int tops = 0, reached = 0;
    double steepest = 0;
    for (int64_t i = ci - 12; i <= ci + 12; i++)
        for (int64_t j = cj - 12; j <= cj + 12; j++) {
            const TrailNode nd = G.node(i, j);
            if (!nd.top || nd.h - G.macroCached(nd.x, nd.z).mass < 80) continue;
            tops++;
            bool on = false;
            for (auto e : {G.edge(i, j, 0), G.edge(i, j, 1), G.edge(i - 1, j, 0), G.edge(i, j - 1, 1)})
                if (e->on) {
                    on = true;
                    double es = 0;
                    size_t ek = 0;
                    for (size_t k = 5; k + 5 < e->y.size(); k += 5) {
                        const double g = std::fabs(e->y[k + 5] - e->y[k]) / std::max(1e-3f, e->along[k + 5] - e->along[k]);
                        if (g > es) es = g, ek = k;
                    }
                    steepest = std::max(steepest, es);
                    if (std::getenv("CZ_TOP_DBG")) {
                        std::printf("edge from %.0f,%.0f rating %d, %zu samples, %.0f m long, climbs %.0f m; steepest %.2f at %.0f m:", e->ax, e->az, e->rating, e->y.size(), e->along.back(),
                                    e->y.back() - e->y.front(), es, e->along[ek]);
                        for (size_t k = ek >= 15 ? ek - 15 : 0; k < std::min(e->y.size(), ek + 20); k += 5) std::printf(" %.0f(%.0f)", e->y[k], e->fill[k]);
                        std::printf("\n");
                    }
                }
            reached += on;
        }
    ctx.note(std::to_string(tops) + " trail nodes up on mesas (80 m and more over the floor) within ~6 km, " + std::to_string(reached) + " with a trail up; steepest 10 m of those trails " +
             std::to_string(steepest));
    ctx.check("some mesa tops have trails up to them", reached >= 2, std::to_string(reached));
}

SCENARIO(procSwitchbacks, "procworld/switchbacks", "Trails switchback up steep climbs (rules 6; owner: switchback trails): where the land between a trail's ends climbs past its rating's grade, the trail zigzags up it in hairpins and keeps nearer its grade") {
    const double lim[3] = {0.15, 0.22, 0.32};
    double len = 0, over = 0, climbLen = 0, climbOver = 0;
    long hairpins = 0, climbs = 0, climbsWithHairpins = 0;
    for (uint64_t seed : {1ull, 2ull, 123456789ull}) {
        const ProcWorld& G = ProcWorld::forSeed(seed);
        for (int64_t i = -6; i < 6; i++)
            for (int64_t j = -6; j < 6; j++)
                for (int dir = 0; dir < 2; dir++) {
                    auto e = G.edge(i, j, dir);
                    if (!e->on || e->road || e->y.size() < 30) continue;
                    const double g = lim[std::min(2, std::max(0, e->rating))];
                    // (A steep climb: a stretch of the trail whose ends, 200 m apart as the crow flies, differ by more than the grade allows.)
                    const size_t n = e->y.size();
                    std::vector<char> steep(n, 0);
                    for (size_t k = 0; k < n; k++)
                        for (size_t q = k + 1; q < n; q++) {
                            const double d = std::hypot(e->x[q] - e->x[k], e->z[q] - e->z[k]);
                            if (d < 200) continue;
                            if (std::fabs(e->y[q] - e->y[k]) > g * d * 1.3)
                                for (size_t r = k; r <= q; r++) steep[r] = 1;
                            break;
                        }
                    bool anyClimb = false, anyPin = false;
                    // (A hairpin: the heading turns more than 120 degrees within 40 m of the trail.)
                    for (size_t k = 10; k + 10 < n; k += 5) {
                        const double ax = e->x[k] - e->x[k - 10], az = e->z[k] - e->z[k - 10], bx = e->x[k + 10] - e->x[k], bz = e->z[k + 10] - e->z[k];
                        const double c = (ax * bx + az * bz) / std::max(1e-6, std::hypot(ax, az) * std::hypot(bx, bz));
                        if (c < -0.5) {
                            hairpins++, anyPin |= steep[k] != 0;
                            k += 15;
                        }
                    }
                    for (size_t k = 0; k + 10 < n; k++) {
                        const double run = e->along[k + 10] - e->along[k], gr = std::fabs(e->y[k + 10] - e->y[k]) / std::max(1e-6, run), seg = e->along[k + 1] - e->along[k];
                        len += seg, over += gr > g ? seg : 0;
                        if (steep[k]) climbLen += seg, climbOver += gr > g ? seg : 0, anyClimb = true;
                    }
                    climbs += anyClimb, climbsWithHairpins += anyClimb && anyPin;
                }
    }
    char b[300];
    std::snprintf(b, sizeof b, "trails: %.0f km, %.1f%% over their grade; steep climbs %.1f km on %ld trails, %.1f%% over their grade; %ld hairpins, on %ld of the climbing trails", len / 1000,
                  100 * over / std::max(1.0, len), climbLen / 1000, climbs, 100 * climbOver / std::max(1.0, climbLen), hairpins, climbsWithHairpins);
    ctx.note(b);
    ctx.metric("share of the trails over their grade (v8: 10.4%)", over / std::max(1.0, len), "", 0, 0.12);
    ctx.metric("share of the steep climbs over their grade", climbOver / std::max(1.0, climbLen), "", 0, 0.3);
    ctx.metric("share of climbing trails with hairpins on their climbs (v8: 61%)", (double)climbsWithHairpins / std::max(1L, climbs), "", 0.62, 1);
}

SCENARIO(procRiverBankDbg,"procworld/river-bank-dbg", "(with CZ_BANK_DBG=seed) where the land beside a wet river lies under its water, and what made it so, for looking at") {
    const char* spec = std::getenv("CZ_BANK_DBG");
    if (!spec) {
        ctx.note("CZ_BANK_DBG not set");
        return;
    }
    const ProcWorld& G = ProcWorld::forSeed(std::strtoull(spec, nullptr, 10));
    long n = 0, under = 0, byCreek = 0, byFloor = 0, byErosion = 0, other = 0;
    double worstDrop = 0;
    for (double x = -6000; x < 6000; x += 6)
        for (double z = -6000; z < 6000; z += 6) {
            const Macro m = G.land(x, z);
            if (!(m.riverOn > 0.5 && m.riverS > m.riverHW && m.riverS < m.riverHW + 12)) continue;
            n++;
            const double drop = m.riverLevel - m.h;
            if (drop < 1) continue;
            worstDrop = std::max(worstDrop, drop);
            under++;
            const double D = G.erosionAt(x, z), pre = m.h - D;
            const bool creek = m.creekS < m.creekHW + 30 && m.creekLevel < m.riverLevel - 1;
            const bool floor = m.floor < m.riverLevel - 1;
            const bool eros = pre >= m.riverLevel - 0.5;
            byCreek += creek, byFloor += floor && !creek, byErosion += eros && !creek && !floor, other += !creek && !floor && !eros;
            if (drop > 2.5)
                std::printf("bank %.0f,%.0f: river %.1f (S %.1f hw %.1f A %.0f big %d) land %.1f (pre-erosion %.1f, D %.1f) floor %.1f floorArea %.0f creek S %.0f level %.1f vale %.1f mass %.1f\n", x, z,
                            m.riverLevel, m.riverS, m.riverHW, m.riverArea, (int)m.bigRiver, m.h, pre, D, m.floor, m.floorArea, m.creekS, m.creekLevel, m.vale, m.mass);
        }
    std::printf("bank: %ld points beside wet rivers, %ld over 1 m under the water: %ld by a creek's notch, %ld by a lower valley's floor, %ld worn down by erosion, %ld other; the deepest %.1f m\n", n, under, byCreek,
                byFloor, byErosion, other, worstDrop);
}

SCENARIO(procProfile,"procworld/profile", "(with CZ_PROFILE=x0,z0,x1,z1[,seed]) the land along a line every 50 m: height, mass, climate, for looking at") {
    const char* spec = std::getenv("CZ_PROFILE");
    double x0, z0, x1, z1;
    unsigned long long seed = 326904175ull;
    if (!spec || std::sscanf(spec, "%lf,%lf,%lf,%lf,%llu", &x0, &z0, &x1, &z1, &seed) < 4) {
        ctx.note("CZ_PROFILE not set: nothing printed");
        return;
    }
    const ProcWorld& G = ProcWorld::forSeed(seed);
    const double L = std::hypot(x1 - x0, z1 - z0);
    const double stepM = std::getenv("CZ_PROFILE_STEP") ? std::atof(std::getenv("CZ_PROFILE_STEP")) : 50;
    for (double t = 0; t <= L; t += stepM) {
        const double x = x0 + (x1 - x0) * t / L, z = z0 + (z1 - z0) * t / L;
        const Macro m = G.macroCached(x, z);
        std::printf("%6.0f m: chunk %7.1f ground %7.1f  h %7.1f mass %7.1f vale %7.1f floor %7.1f rug %.2f wet %.2f heat %.2f  w %.2f %.2f %.2f %.2f %.2f\n", t, [&] { const int64_t ix = (int64_t)std::floor(x), iz = (int64_t)std::floor(z); const auto c = G.chunk(floorDiv(ix, kChunk), floorDiv(iz, kChunk)); return (double)c->h[(ix - c->cx * kChunk) * kChunk + (iz - c->cz * kChunk)]; }(), G.groundHeight(x, z, m), m.h, m.mass,
                    m.vale, m.floor, m.rug, m.wet, m.heat, m.w[0], m.w[1], m.w[2], m.w[3], m.w[4]);
    }
}
