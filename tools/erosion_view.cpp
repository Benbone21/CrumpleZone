// The procedural world's erosion (src/proceros.h) on one tile, for looking at: hillshaded maps of the land before and after, what
// changed, the tile's cost and the seam with its east and north neighbours.
//   build/erosion_view SEED TI TJ OUTDIR [STEP]
// writes OUTDIR/eros_<seed>_<ti>_<tj>_{before,after,change}.ppm
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/parallel.h"
#include "../src/procrules.h"
#include "../src/procworld.h"

using namespace proc;

static void writePPM(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

// Hillshade (sun from the north-west, 40 degrees up) over a height ramp. Image x is east (+x), image y is south (-z up the page).
static std::vector<unsigned char> shade(const std::vector<double>& H, int n, double cell) {
    std::vector<unsigned char> out((size_t)n * n * 3);
    double lo = 1e9, hi = -1e9;
    for (double v : H) lo = std::min(lo, v), hi = std::max(hi, v);
    const double lx = -0.6, ly = 0.65, lz = 0.45;  // (x east, y up, z north): from the north-west
    for (int py = 0; py < n; py++)
        for (int px = 0; px < n; px++) {
            const int a = px, b = n - 1 - py;  // (b along z: north at the top)
            auto at = [&](int aa, int bb) { aa = std::max(0, std::min(n - 1, aa)), bb = std::max(0, std::min(n - 1, bb)); return H[(size_t)aa * n + bb]; };
            const double dx = (at(a + 1, b) - at(a - 1, b)) / (2 * cell), dz = (at(a, b + 1) - at(a, b - 1)) / (2 * cell);
            double nx = -dx, ny = 1, nz = -dz;
            const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= l, ny /= l, nz /= l;
            const double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
            double d = (nx * lx + ny * ly + nz * lz) / ll;
            d = std::max(0.0, d);
            const double t = (at(a, b) - lo) / std::max(1.0, hi - lo);
            // (green low, tan, grey, white high)
            double r, g, bl;
            if (t < 0.33) r = 0.35 + 0.4 * t / 0.33, g = 0.5 + 0.15 * t / 0.33, bl = 0.3;
            else if (t < 0.66) r = 0.75 - 0.15 * (t - 0.33) / 0.33, g = 0.65 - 0.1 * (t - 0.33) / 0.33, bl = 0.3 + 0.25 * (t - 0.33) / 0.33;
            else r = 0.6 + 0.4 * (t - 0.66) / 0.34, g = 0.55 + 0.45 * (t - 0.66) / 0.34, bl = 0.55 + 0.45 * (t - 0.66) / 0.34;
            const double s = 0.25 + 0.85 * d;
            unsigned char* o = &out[((size_t)py * n + px) * 3];
            o[0] = (unsigned char)std::min(255.0, 255 * r * s), o[1] = (unsigned char)std::min(255.0, 255 * g * s), o[2] = (unsigned char)std::min(255.0, 255 * bl * s);
        }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: erosion_view SEED TI TJ OUTDIR [STEP]\n");
        return 1;
    }
    const uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    const int64_t ti = std::atoll(argv[2]), tj = std::atoll(argv[3]);
    const std::string dir = argv[4];
    double step = argc > 5 ? std::atof(argv[5]) : 4;
    loadShippedRules();
    const ProcWorld& G = ProcWorld::forSeed(seed);
    const int S = ProcWorld::erosionTileSize();
    int n = (int)(S / step) + 1;
    double x0 = (double)ti * S, z0 = (double)tj * S;
    if (const char* c = std::getenv("EV_AREA")) {  // (cx,cz,span: a square round a point instead of the tile)
        double cx, cz, span;
        if (std::sscanf(c, "%lf,%lf,%lf", &cx, &cz, &span) == 3) n = 600, step = span / 599, x0 = cx - span / 2, z0 = cz - span / 2;
    }
    // The tile alone, timed (a cold one).
    double ox, oz, cell, ms;
    int en;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<float> d = G.erosionTile(ti, tj, ox, oz, en, cell, ms);
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("tile %lld,%lld: erosion %.0f ms (wall %.0f ms with the land's sampling), change grid %d x %d (%.1f MB)\n", (long long)ti, (long long)tj, ms, wall, en, en,
                en * en * 4 / 1048576.0);
    double cut = 0, fill = 0, mx = 0, mn = 0;
    for (float v : d) (v < 0 ? cut : fill) += std::fabs(v) * cell * cell, mx = std::max(mx, (double)v), mn = std::min(mn, (double)v);
    std::printf("  worn away %.2f Mm3, silted %.2f Mm3; change %.1f .. %.1f m\n", cut / 1e6, fill / 1e6, mn, mx);
    // The seams: the tile and its east and north neighbours over the blend band (where both count), and the blended change's
    // steepest step there against inside the tile.
    for (int side = 0; side < 2; side++) {
        double ox2, oz2, cell2, ms2;
        int en2;
        std::vector<float> d2 = G.erosionTile(ti + (side == 0), tj + (side == 1), ox2, oz2, en2, cell2, ms2);
        double sq = 0, mxd = 0;
        long cnt = 0;
        for (int a = 0; a < en; a++)
            for (int b = 0; b < en; b++) {
                const double x = ox + a * cell, z = oz + b * cell;
                const double fa = (x - ox2) / cell2, fb = (z - oz2) / cell2;
                if (fa < 0 || fb < 0 || fa > en2 - 1 || fb > en2 - 1) continue;
                const double v = d[(size_t)a * en + b] - d2[(size_t)(int)fa * en2 + (int)fb];
                sq += v * v, mxd = std::max(mxd, std::fabs(v)), cnt++;
            }
        // (the blended field's change from one metre to the next, across the seam and inside)
        double stepSeam = 0, stepIn = 0;
        for (int k = 0; k < 400; k++) {
            const double along = (side == 0 ? z0 : x0) + 64 + k * 4.8;
            for (int q = -140; q < 140; q++) {
                const double across = (side == 0 ? x0 : z0) + S + q;
                const double e0 = side == 0 ? G.erosionAt(across, along) : G.erosionAt(along, across);
                const double e1 = side == 0 ? G.erosionAt(across + 1, along) : G.erosionAt(along, across + 1);
                stepSeam = std::max(stepSeam, std::fabs(e1 - e0));
                const double in = (side == 0 ? x0 : z0) + S / 2 + q;
                const double i0 = side == 0 ? G.erosionAt(in, along) : G.erosionAt(along, in);
                const double i1 = side == 0 ? G.erosionAt(in + 1, along) : G.erosionAt(along, in + 1);
                stepIn = std::max(stepIn, std::fabs(i1 - i0));
            }
        }
        std::printf("  seam with the %s tile: %ld points both wore, the two differ by %.2f m RMS (%.1f at most); the blend's steepest metre step %.2f m across the seam, %.2f inside\n",
                    side == 0 ? "east" : "north", cnt, std::sqrt(sq / std::max(1L, cnt)), mxd, stepSeam, stepIn);
    }
    // The land before and after (the ground: valleys and beds, no bumps or trails).
    std::vector<double> after((size_t)n * n), before((size_t)n * n), change((size_t)n * n), lakeA((size_t)n * n, -1e9), lakeB((size_t)n * n, -1e9);
    parallelRows(n, [&](int a) {
        for (int b = 0; b < n; b++) {
            const double x = x0 + a * step, z = z0 + b * step;
            const double hA = G.groundHeight(x, z), e = G.erosionAt(x, z);
            after[(size_t)a * n + b] = hA, change[(size_t)a * n + b] = e;
            const double lv = G.erosionLake(x, z);
            if (lv > hA) lakeA[(size_t)a * n + b] = lv;
        }
    });
    {
        std::vector<std::string> w;
        Rules r = rules();
        r.erosion.on = 0;
        const std::string path = dir + "/.rules_off.txt";
        FILE* f = std::fopen(path.c_str(), "wb");
        const std::string t = rulesText(r);
        std::fwrite(t.data(), 1, t.size(), f);
        std::fclose(f);
        loadRules(path, &w);
        clearChunkCache();
    }
    parallelRows(n, [&](int a) {
        for (int b = 0; b < n; b++) {
            const Macro m = G.macro(x0 + a * step, z0 + b * step);
            const double h = G.groundHeight(x0 + a * step, z0 + b * step, m);
            before[(size_t)a * n + b] = h;
            if (m.lakeLevel > h) lakeB[(size_t)a * n + b] = m.lakeLevel;
        }
    });
    const std::string stem = dir + "/eros_" + std::to_string(seed) + "_" + (std::getenv("EV_NAME") ? std::string(std::getenv("EV_NAME")) : std::to_string(ti) + "_" + std::to_string(tj));
    auto water = [&](std::vector<unsigned char> img, const std::vector<double>& H, const std::vector<double>& W) {
        long cells = 0;
        for (int py = 0; py < n; py++)
            for (int px = 0; px < n; px++) {
                const size_t i = (size_t)px * n + (n - 1 - py);
                if (W[i] < -1e8) continue;
                cells++;
                const double dep = std::min(1.0, (W[i] - H[i]) / 6.0);
                unsigned char* o = &img[((size_t)py * n + px) * 3];
                o[0] = (unsigned char)(40 - 25 * dep), o[1] = (unsigned char)(110 - 50 * dep), o[2] = (unsigned char)(170 - 40 * dep);
            }
        std::printf("  lake water: %.1f ha\n", cells * step * step / 1e4);
        return img;
    };
    writePPM(stem + "_before.ppm", n, n, water(shade(before, n, step), before, lakeB));
    writePPM(stem + "_after.ppm", n, n, water(shade(after, n, step), after, lakeA));
    std::vector<unsigned char> cimg((size_t)n * n * 3);
    for (int py = 0; py < n; py++)
        for (int px = 0; px < n; px++) {
            const double v = change[(size_t)px * n + (n - 1 - py)];
            const double t = std::min(1.0, std::fabs(v) / 10);
            unsigned char* o = &cimg[((size_t)py * n + px) * 3];
            if (v < 0) o[0] = 255, o[1] = o[2] = (unsigned char)(255 * (1 - t));
            else o[2] = 255, o[0] = o[1] = (unsigned char)(255 * (1 - t));
        }
    writePPM(stem + "_change.ppm", n, n, cimg);
    double lo = 1e9, hi = -1e9;
    for (double v : before) lo = std::min(lo, v), hi = std::max(hi, v);
    std::printf("  land %.0f .. %.0f m; wrote %s_{before,after,change}.ppm\n", lo, hi, stem.c_str());
    return 0;
}
