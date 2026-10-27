// Erosion lab (dev tool): one tile's land sampled once (cached in OUTDIR), then worn with the erosion's knobs from the environment,
// hillshaded before and after (a crop), timed. For tuning src/proceros.cpp quickly.
//   build/erosion_lab SEED TI TJ OUTDIR [CROP_A CROP_B CROP_N]
//   env: ER_IT ER_CUT ER_DEP ER_TALUS ER_CREEP ER_MAXCUT (override every biome's), ER_TAG (the output's name)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/proceros.h"
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


static double envd(const char* k, double def) {
    const char* v = std::getenv(k);
    return v && *v ? std::atof(v) : def;
}

int main(int argc, char** argv) {
    if (argc < 5) return 1;
    const uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    const int64_t ti = std::atoll(argv[2]), tj = std::atoll(argv[3]);
    const std::string dir = argv[4];
    loadShippedRules();
    if (std::getenv("ER_SCAN")) {  // (what each tile round (ti, tj) is: its land's range and biomes)
        {
            Rules r = rules();
            r.erosion.on = 0;
            FILE* f = std::fopen((dir + "/.off.txt").c_str(), "wb");
            const std::string t = rulesText(r);
            std::fwrite(t.data(), 1, t.size(), f);
            std::fclose(f);
            loadRules(dir + "/.off.txt");
        }
        const ProcWorld& G = ProcWorld::forSeed(seed);
        const int S = ProcWorld::erosionTileSize();
        if (std::getenv("ER_LAKES")) {
            for (const auto& f : G.waterFeatures((ti - 2) * S, (tj - 2) * S, (ti + 3) * S, (tj + 3) * S))
                if (f.kind == 0) std::printf("lake sub %d at %.0f,%.0f level %.1f size %.0f\n", f.sub, f.x, f.z, f.level, f.size);
            return 0;
        }
        for (int64_t j = tj + 3; j >= tj - 3; j--) {
            for (int64_t i = ti - 3; i <= ti + 3; i++) {
                double lo = 1e9, hi = -1e9, w[PB_COUNT] = {};
                for (int a = 0; a < 8; a++)
                    for (int b = 0; b < 8; b++) {
                        const Macro m = G.land(i * S + a * S / 8.0 + 128, j * S + b * S / 8.0 + 128);
                        lo = std::min(lo, m.h), hi = std::max(hi, m.h);
                        for (int k = 0; k < PB_COUNT; k++) w[k] += m.w[k] / 64;
                    }
                std::printf("%3lld,%3lld %4.0f-%4.0f M%.1f F%.1f B%.1f A%.1f D%.1f | ", (long long)i, (long long)j, lo, hi, w[0], w[1], w[2], w[3], w[4]);
            }
            std::printf("\n");
        }
        return 0;
    }
    ErosionGrid g;
    const std::string cache = dir + "/lab_" + std::to_string(seed) + "_" + std::to_string(ti) + "_" + std::to_string(tj) + ".bin";
    if (FILE* f = std::fopen(cache.c_str(), "rb")) {
        std::fread(&g.n, 4, 1, f);
        const size_t N = (size_t)g.n * g.n;
        g.h.resize(N), g.kErode.resize(N), g.gDep.resize(N), g.talus.resize(N), g.creep.resize(N), g.fixed.resize(N);
        std::fread(g.h.data(), 4, N, f), std::fread(g.kErode.data(), 4, N, f), std::fread(g.gDep.data(), 4, N, f), std::fread(g.talus.data(), 4, N, f);
        std::fread(g.creep.data(), 4, N, f), std::fread(g.fixed.data(), 1, N, f);
        g.floor.resize(N), std::fread(g.floor.data(), 4, N, f);
        std::fclose(f);
    } else {
        double ox, oz;
        const auto t0 = std::chrono::steady_clock::now();
        ProcWorld::forSeed(seed).erosionInput(ti, tj, g, ox, oz);
        std::printf("sampled in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        FILE* fo = std::fopen(cache.c_str(), "wb");
        const size_t N = (size_t)g.n * g.n;
        std::fwrite(&g.n, 4, 1, fo);
        std::fwrite(g.h.data(), 4, N, fo), std::fwrite(g.kErode.data(), 4, N, fo), std::fwrite(g.gDep.data(), 4, N, fo), std::fwrite(g.talus.data(), 4, N, fo);
        std::fwrite(g.creep.data(), 4, N, fo), std::fwrite(g.fixed.data(), 1, N, fo), std::fwrite(g.floor.data(), 4, N, fo);
        std::fclose(fo);
    }
    const int n = g.n;
    const size_t N = (size_t)n * n;
    // (scale the biomes' knobs)
    for (size_t i = 0; i < N; i++) {
        g.kErode[i] *= (float)envd("ER_CUT", 1), g.gDep[i] *= (float)envd("ER_DEP", 1), g.talus[i] *= (float)envd("ER_TALUS", 1), g.creep[i] *= (float)envd("ER_CREEP", 1);
    }
    const std::vector<float> base = g.h;
    ErosionRun run;
    run.iterations = (int)envd("ER_IT", rules().erosion.iterations);
    run.coarseIterations = (int)envd("ER_CIT", rules().erosion.coarseIterations);
    run.glacierIterations = (int)envd("ER_GIT", rules().erosion.glacierIterations);
    run.glacierLine = envd("ER_GLINE", rules().erosion.glacierLine);
    run.glacierCut = envd("ER_GCUT", rules().erosion.glacierCut);
    run.pitFill = envd("ER_PIT", rules().erosion.pitFill);
    run.maxCut = envd("ER_MAXCUT", rules().erosion.maxCut);
    const auto t0 = std::chrono::steady_clock::now();
    erodeGrid(g, run);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double cut = 0, fill = 0, mn = 0, mx = 0;
    for (size_t i = 0; i < N; i++) {
        const double d = g.h[i] - base[i];
        (d < 0 ? cut : fill) += std::fabs(d) * 16, mn = std::min(mn, d), mx = std::max(mx, d);
    }
    std::printf("eroded %d x %d, %d iterations in %.0f ms; worn %.2f Mm3, silted %.2f Mm3, change %.1f .. %.1f m\n", n, n, run.iterations, ms, cut / 1e6, fill / 1e6, mn, mx);
    // The closed hollows (where lakes could stand: filled to their spill point), before and after: those over 0.5 ha and 1 m deep.
    for (int pass = 0; pass < 2; pass++) {
        const std::vector<float>& H = pass ? g.h : base;
        std::vector<float> F;
        fillDepressions(n, H, F);
        std::vector<int> lab(N, -1);
        int big = 0;
        double bigArea = 0, deepest = 0;
        for (size_t s0 = 0; s0 < N; s0++) {
            if (lab[s0] >= 0 || F[s0] - H[s0] < 0.05f) continue;
            std::vector<size_t> st{s0};
            lab[s0] = 1;
            long cells = 0;
            double dmax = 0;
            while (!st.empty()) {
                const size_t i = st.back();
                st.pop_back();
                cells++, dmax = std::max(dmax, (double)(F[i] - H[i]));
                const int a = (int)(i / n), b = (int)(i % n);
                for (int da = -1; da <= 1; da++)
                    for (int db = -1; db <= 1; db++) {
                        const int na = a + da, nb = b + db;
                        if (na < 0 || nb < 0 || na >= n || nb >= n) continue;
                        const size_t j = (size_t)na * n + nb;
                        if (lab[j] < 0 && F[j] - H[j] >= 0.05f) lab[j] = 1, st.push_back(j);
                    }
            }
            if (cells * 16 >= 5000 && dmax >= 1) big++, bigArea += cells * 16, deepest = std::max(deepest, dmax);
        }
        std::printf("%s: %d closed hollows over 0.5 ha and 1 m deep (%.1f ha in all, deepest %.1f m)\n", pass ? "after" : "before", big, bigArea / 1e4, deepest);
    }
    const int ca = argc > 7 ? std::atoi(argv[5]) : 0, cb = argc > 7 ? std::atoi(argv[6]) : 0, cn = argc > 7 ? std::atoi(argv[7]) : n;
    std::vector<double> A((size_t)cn * cn), B((size_t)cn * cn);
    for (int a = 0; a < cn; a++)
        for (int b = 0; b < cn; b++) A[(size_t)a * cn + b] = base[(size_t)(a + ca) * n + b + cb], B[(size_t)a * cn + b] = g.h[(size_t)(a + ca) * n + b + cb];
    const char* tag = std::getenv("ER_TAG");
    const std::string stem = dir + "/lab_" + (tag ? tag : "x");
    writePPM(stem + "_before.ppm", cn, cn, shade(A, cn, 4));
    std::vector<unsigned char> aimg = shade(B, cn, 4);
    {
        std::vector<float> lv;
        const int lakes = findLakes(n, 4, g.h, lv, 3000, 1.0);
        double area = 0;
        for (float v : lv) area += v > -1e29f ? 16 : 0;
        std::printf("lakes on the worn land: %d (%.1f ha)\n", lakes, area / 1e4);
        for (int py = 0; py < cn; py++)
            for (int px = 0; px < cn; px++) {
                const size_t i = (size_t)(px + ca) * n + (cn - 1 - py + cb);
                if (lv[i] < -1e29f) continue;
                const double dep = std::min(1.0, (lv[i] - g.h[i]) / 6.0);
                unsigned char* o = &aimg[((size_t)py * cn + px) * 3];
                o[0] = (unsigned char)(40 - 25 * dep), o[1] = (unsigned char)(110 - 50 * dep), o[2] = (unsigned char)(170 - 40 * dep);
            }
    }
    writePPM(stem + "_after.ppm", cn, cn, aimg);
    std::vector<unsigned char> cimg((size_t)cn * cn * 3);
    for (int py = 0; py < cn; py++)
        for (int px = 0; px < cn; px++) {
            const size_t i = (size_t)px * cn + (cn - 1 - py);
            const double v = B[i] - A[i], t = std::min(1.0, std::fabs(v) / 8);
            unsigned char* o = &cimg[((size_t)py * cn + px) * 3];
            if (v < 0) o[0] = 255, o[1] = o[2] = (unsigned char)(255 * (1 - t));
            else o[2] = 255, o[0] = o[1] = (unsigned char)(255 * (1 - t));
            if (g.fixed[(size_t)(px + ca) * n + (cn - 1 - py + cb)]) o[0] = o[2] = 0, o[1] = 160;
        }
    writePPM(stem + "_change.ppm", cn, cn, cimg);
    return 0;
}
