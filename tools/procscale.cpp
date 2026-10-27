// How mountainous the procedural world is round its start (dev tool): over a square round the start, the share of the land that is
// lowland, hills and mountains (by the local relief: the highest less the lowest ground within 500 m), the peaks, and how far you
// drive from the start in 16 directions before the hills and the real mountains. Writes a hillshaded map with the start marked.
//   build/procscale SEED OUTDIR [SPAN_M] [STEP_M]
// The land is the unworn one (erosion off: quick; the wear moves the ground by tens of metres, not hundreds).
#include <algorithm>
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

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: procscale SEED OUTDIR [SPAN_M] [STEP_M]\n");
        return 1;
    }
    parallelRowsCap = std::getenv("CZ_GEN_THREADS") ? std::max(1, std::atoi(std::getenv("CZ_GEN_THREADS"))) : 2;
    const uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    const std::string dir = argv[2];
    const double span = argc > 3 ? std::atof(argv[3]) : 12000, step = argc > 4 ? std::atof(argv[4]) : 40;
    loadShippedRules();
    if (const char* extra = std::getenv("PS_RULES")) {  // (a file of rules read over the shipped ones: for tuning)
        std::vector<std::string> w;
        loadRules(extra, &w);
        for (const std::string& m : w) std::fprintf(stderr, "%s\n", m.c_str());
    }
    {
        Rules r = rules();
        r.erosion.on = 0;
        const std::string path = dir + "/.rules_off.txt";
        FILE* f = std::fopen(path.c_str(), "wb");
        const std::string t = rulesText(r);
        std::fwrite(t.data(), 1, t.size(), f);
        std::fclose(f);
        std::vector<std::string> w;
        loadRules(path, &w);
        clearChunkCache();
    }
    const ProcWorld& G = ProcWorld::forSeed(seed);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    const int n = (int)(span / step) + 1;
    const double x0 = sx - span / 2, z0 = sz - span / 2;
    std::vector<double> H((size_t)n * n), alp((size_t)n * n), lake((size_t)n * n, 0), river((size_t)n * n, 0);
    parallelRows(n, [&](int a) {
        for (int b = 0; b < n; b++) {
            const Macro m = G.macro(x0 + a * step, z0 + b * step);
            const size_t i = (size_t)a * n + b;
            H[i] = G.groundHeight(x0 + a * step, z0 + b * step, m);
            alp[i] = m.w[PB_MOUNTAINS];
            if (m.lakeLevel > H[i]) lake[i] = 1;
            if (m.riverS < m.riverHW && m.riverOn > 0) river[i] = m.bigRiver ? 2 : 1;
        }
    }, 4);
    // The local relief: the highest less the lowest within 500 m (a square), separably.
    const int r = std::max(1, (int)std::lround(500 / step));
    std::vector<double> mx1((size_t)n * n), mn1((size_t)n * n), rel((size_t)n * n);
    for (int a = 0; a < n; a++)
        for (int b = 0; b < n; b++) {
            double hi = -1e9, lo = 1e9;
            for (int q = std::max(0, b - r); q <= std::min(n - 1, b + r); q++) hi = std::max(hi, H[(size_t)a * n + q]), lo = std::min(lo, H[(size_t)a * n + q]);
            mx1[(size_t)a * n + b] = hi, mn1[(size_t)a * n + b] = lo;
        }
    for (int a = 0; a < n; a++)
        for (int b = 0; b < n; b++) {
            double hi = -1e9, lo = 1e9;
            for (int q = std::max(0, a - r); q <= std::min(n - 1, a + r); q++) hi = std::max(hi, mx1[(size_t)q * n + b]), lo = std::min(lo, mn1[(size_t)q * n + b]);
            rel[(size_t)a * n + b] = hi - lo;
        }
    long low = 0, hill = 0, mtn = 0, cnt = 0;
    double alpS = 0, lo = 1e9, hi = -1e9;
    for (size_t i = 0; i < H.size(); i++) {
        cnt++;
        (rel[i] < 150 ? low : rel[i] < 500 ? hill : mtn)++;
        alpS += alp[i] > 0.5;
        lo = std::min(lo, H[i]), hi = std::max(hi, H[i]);
    }
    // Steepness (over the grid's step: coarse): the share steeper than 1:1 and than 2:1 (walls).
    double st1 = 0, st2 = 0;
    for (int a = 1; a < n - 1; a++)
        for (int b = 1; b < n - 1; b++) {
            const double gx = (H[(size_t)(a + 1) * n + b] - H[(size_t)(a - 1) * n + b]) / (2 * step), gz = (H[(size_t)a * n + b + 1] - H[(size_t)a * n + b - 1]) / (2 * step);
            const double g = std::sqrt(gx * gx + gz * gz);
            st1 += g > 1, st2 += g > 2;
        }
    st1 /= (double)(n - 2) * (n - 2), st2 /= (double)(n - 2) * (n - 2);
    // Peaks: the highest point within 1 km, over 1500 m.
    const int pr = (int)std::lround(1000 / step);
    int peaks = 0, peaks1k = 0;
    for (int a = pr; a < n - pr; a++)
        for (int b = pr; b < n - pr; b++) {
            const double h = H[(size_t)a * n + b];
            if (h < 1000) continue;
            bool top = true;
            for (int p = -pr; p <= pr && top; p++)
                for (int q = -pr; q <= pr; q++)
                    if ((p || q) && p * p + q * q <= pr * pr && H[(size_t)(a + p) * n + b + q] >= h) { top = false; break; }
            if (top) peaks1k++, peaks += h >= 1500;
        }
    // From the start: in 16 directions, how far to ground with 150 m (hills) and 500 m (mountains) of relief round it.
    const int c = n / 2;
    std::vector<double> dh, dm;
    for (int k = 0; k < 16; k++) {
        const double ang = k * 2 * M_PI / 16, ux = std::cos(ang), uz = std::sin(ang);
        double fh = -1, fm = -1;
        for (double d = 0; d < span / 2 - step; d += step) {
            const int a = (int)std::lround(c + ux * d / step), b = (int)std::lround(c + uz * d / step);
            if (a < 0 || b < 0 || a >= n || b >= n) break;
            const double v = rel[(size_t)a * n + b];
            if (fh < 0 && v >= 150) fh = d;
            if (fm < 0 && v >= 500) { fm = d; break; }
        }
        dh.push_back(fh < 0 ? 99000 : fh), dm.push_back(fm < 0 ? 99000 : fm);
    }
    // Over the whole square: how far each point is from the nearest hills and mountains (a two-pass chamfer distance).
    auto distTo = [&](double thr) {
        std::vector<double> D((size_t)n * n);
        for (size_t i = 0; i < D.size(); i++) D[i] = rel[i] >= thr ? 0 : 1e9;
        const double s1 = step, s2 = step * std::sqrt(2.0);
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < n * n; k++) {
                const int idx = pass == 0 ? k : n * n - 1 - k, a = idx / n, b = idx % n, sg = pass == 0 ? -1 : 1;
                double& v = D[(size_t)idx];
                auto take = [&](int aa, int bb, double w) { if (aa >= 0 && bb >= 0 && aa < n && bb < n) v = std::min(v, D[(size_t)aa * n + bb] + w); };
                take(a + sg, b, s1), take(a, b + sg, s1), take(a + sg, b + sg, s2), take(a + sg, b - sg, s2);
            }
        return D;
    };
    const std::vector<double> DH = distTo(150), DM = distTo(500);
    double inH1 = 0, inM2 = 0, meanM = 0;
    for (size_t i = 0; i < DH.size(); i++) inH1 += DH[i] <= 1000, inM2 += DM[i] <= 2000, meanM += std::min(DM[i], 12000.0);
    inH1 /= DH.size(), inM2 /= DM.size(), meanM /= DM.size();
    auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return 0.5 * (v[7] + v[8]); };
    auto mxv = [](const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); };
    auto mnv = [](const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); };
    int dmMiss = 0;
    for (double v : dm) dmMiss += v > 98000;
    double lakeHa = 0, bigRiv = 0;
    for (size_t i = 0; i < H.size(); i++) lakeHa += lake[i] * step * step / 1e4, bigRiv += river[i] == 2;
    std::printf("seed %llu: start (%lld, %lld), its ground %.0f m, relief round it %.0f m; %.0f km square at %.0f m: land %.0f .. %.0f m\n",
                (unsigned long long)seed, (long long)sx, (long long)sz, H[(size_t)c * n + c], rel[(size_t)c * n + c], span / 1000, step, lo, hi);
    std::printf("  shares: lowland (relief < 150 m) %.0f%%, hills (150-500) %.0f%%, mountains (500+) %.0f%%; alpine biome %.0f%%\n", 100.0 * low / cnt, 100.0 * hill / cnt,
                100.0 * mtn / cnt, 100.0 * alpS / cnt);
    std::printf("  peaks (the highest within 1 km): %d over 1000 m, %d over 1500 m; steeper than 1:1 %.1f%%, than 2:1 %.2f%% (at the grid step)\n", peaks1k, peaks, 100 * st1, 100 * st2);
    std::printf("  from the start, 16 directions: hills at %.1f km median (%.1f .. %.1f), mountains at %.1f km median (%.1f .. %s)%s\n", med(dh) / 1000, mnv(dh) / 1000,
                mxv(dh) / 1000, med(dm) / 1000, mnv(dm) / 1000,
                dmMiss ? "none" : std::to_string(mxv(dm) / 1000).substr(0, 4).c_str(), dmMiss ? (" (" + std::to_string(dmMiss) + " directions reach none)").c_str() : "");
    std::printf("  anywhere in the square: hills within 1 km of %.0f%% of it, mountains within 2 km of %.0f%%; the mean way to mountains %.1f km\n", 100 * inH1, 100 * inM2, meanM / 1000, 100 * st1, 100 * st2, hi);
    std::printf("  lake water %.0f ha; big rivers' water %.0f ha\n", lakeHa, bigRiv * step * step / 1e4);
    std::printf("CSV,%llu,%.0f,%.0f,%.1f,%.1f,%.1f,%.1f,%d,%d,%.2f,%.2f,%.2f,%d,%.0f,%.0f,%.2f,%.2f,%.3f,%.0f\n", (unsigned long long)seed, H[(size_t)c * n + c], rel[(size_t)c * n + c], 100.0 * low / cnt,
                100.0 * hill / cnt, 100.0 * mtn / cnt, 100.0 * alpS / cnt, peaks1k, peaks, med(dh) / 1000, med(dm) / 1000, mxv(dm) / 1000, dmMiss, 100 * inH1, 100 * inM2, meanM / 1000, 100 * st1, 100 * st2, hi);
    // The map: hillshade over a fixed height ramp (0 .. 2800 m, so before and after compare), lakes and big rivers blue, the start red.
    std::vector<unsigned char> img((size_t)n * n * 3);
    const double lx = -0.6, ly = 0.65, lz = 0.45, ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    for (int py = 0; py < n; py++)
        for (int px = 0; px < n; px++) {
            const int a = px, b = n - 1 - py;
            auto at = [&](int aa, int bb) { aa = std::max(0, std::min(n - 1, aa)), bb = std::max(0, std::min(n - 1, bb)); return H[(size_t)aa * n + bb]; };
            const double dx = (at(a + 1, b) - at(a - 1, b)) / (2 * step), dz = (at(a, b + 1) - at(a, b - 1)) / (2 * step);
            double nx = -dx, ny = 1, nz = -dz;
            const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
            const double d = std::max(0.0, (nx * lx + ny * ly + nz * lz) / (l * ll));
            const double t = std::min(1.0, std::max(0.0, at(a, b) / 2800));
            double rr, g, bl;
            if (t < 0.15) rr = 0.35 + 0.4 * t / 0.15, g = 0.5 + 0.15 * t / 0.15, bl = 0.3;
            else if (t < 0.45) rr = 0.75 - 0.15 * (t - 0.15) / 0.3, g = 0.65 - 0.1 * (t - 0.15) / 0.3, bl = 0.3 + 0.25 * (t - 0.15) / 0.3;
            else rr = 0.6 + 0.4 * (t - 0.45) / 0.55, g = 0.55 + 0.45 * (t - 0.45) / 0.55, bl = 0.55 + 0.45 * (t - 0.45) / 0.55;
            const double s = 0.25 + 0.85 * d;
            unsigned char* o = &img[((size_t)py * n + px) * 3];
            const size_t i = (size_t)a * n + b;
            if (lake[i] > 0 || river[i] == 2) o[0] = 40, o[1] = 100, o[2] = 190;
            else o[0] = (unsigned char)std::min(255.0, 255 * rr * s), o[1] = (unsigned char)std::min(255.0, 255 * g * s), o[2] = (unsigned char)std::min(255.0, 255 * bl * s);
            if (std::abs(px - c) + std::abs(n - 1 - py - c) <= 4) o[0] = 230, o[1] = 20, o[2] = 20;
        }
    const std::string name = dir + "/scale_" + std::to_string(seed) + (std::getenv("EV_NAME") ? std::string("_") + std::getenv("EV_NAME") : std::string()) + ".ppm";
    writePPM(name, n, n, img);
    std::printf("  wrote %s\n", name.c_str());
    return 0;
}
