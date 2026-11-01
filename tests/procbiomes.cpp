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
    ctx.metric("the worst step from a far window's edge to the land past it (a lot's or a trail's earthworks)", worstEdge, "m", 0, 40);
    ctx.metric("share of the edge stepping over 3 m", (double)edgeBad / std::max(1L, edgeN), "", 0, 0.08);
    ctx.metric("two far windows a slide apart: the worst difference in land or water where they overlap", worstSlide, "m", 0, 1e-6);
    ctx.metric("the farthest local coordinate of a far window's spawn", worstLocal, "m", 0, 10);
    ctx.check("a truck set down 30 km out rests on the ground (6 of 6)", onGround == 6, std::to_string(onGround) + " of 6");
}
