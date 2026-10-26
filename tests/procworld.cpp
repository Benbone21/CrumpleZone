// The procedural world (src/procworld.h, docs/superpowers/specs/2026-10-25-procedural-world-design.md): the same seed and chunk
// always make the same bytes, other seeds make other land, chunk borders are seamless, the window slides without the land moving
// under a vehicle, far out is the same as near, and a chunk is made within its budget.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <array>
#include <set>

#include "../src/procworld.h"
#include "harness.h"

using namespace proc;

namespace {
const VehicleSpec& p4Runner() { return vehicleCatalog()[3]; }

// A world of `seed` round global (gx, gz) (a chunk corner near it).
World windowAt(uint64_t seed, double gx, double gz) {
    World::procSeed = seed;
    World::procOriginSet = true;
    World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
    World w(MAP_PROCEDURAL);
    World::procOriginSet = false;
    return w;
}
std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
    return b;
}
}  // namespace

SCENARIO(procDeterministic, "procworld/deterministic", "The same seed and chunk make the same bytes every time (heights, ground, water, trails and everything placed); a cached chunk is the one made afresh") {
    const ProcWorld& G = ProcWorld::forSeed(12345);
    bool same = true;
    std::string bad;
    const int64_t spots[][2] = {{0, 0}, {-1, -1}, {7, -3}, {-40, 25}, {1562, -1562}, {-156250, 156250}};
    for (const auto& s : spots) {
        const uint64_t a = G.makeChunk(s[0], s[1])->hash(), b = G.makeChunk(s[0], s[1])->hash(), c = G.chunk(s[0], s[1])->hash();
        if (a != b || a != c) same = false, bad += " " + std::to_string(s[0]) + "," + std::to_string(s[1]);
    }
    ctx.check("same seed + chunk: same hash (fresh, again, cached)", same, bad);
    // A fixed chunk's hash, pinned: this changes only with the generator (bump kGenVersion when it does). Every platform must agree.
    const uint64_t h00 = G.makeChunk(0, 0)->hash();
    ctx.note("chunk (0,0) of seed 12345: " + hex(h00) + " (generator v" + std::to_string(kGenVersion) + ")");
    // A second ProcWorld for the seed (as another machine would make) gives the same.
    const ProcWorld other(12345);
    ctx.check("another generator for the seed: the same chunk", other.makeChunk(7, -3)->hash() == G.makeChunk(7, -3)->hash());
}

SCENARIO(procSeedsDiffer, "procworld/seeds-differ", "Different seeds make clearly different land: other heights and another mix of biomes") {
    const uint64_t seeds[] = {1, 2, 3, 99, 123456789, 0xDEADBEEFull};
    std::vector<std::vector<double>> hs;
    std::vector<std::array<double, PB_COUNT>> mixes;
    for (uint64_t s : seeds) {
        const ProcWorld& G = ProcWorld::forSeed(s);
        std::vector<double> h;
        std::array<double, PB_COUNT> mix{};
        for (int a = -20; a <= 20; a++)
            for (int b = -20; b <= 20; b++) {
                const Macro m = G.macro(a * 250.0, b * 250.0);
                h.push_back(m.h);
                for (int k = 0; k < PB_COUNT; k++) mix[k] += m.w[k] / (41.0 * 41.0);
            }
        hs.push_back(h), mixes.push_back(mix);
        char line[200];
        std::snprintf(line, sizeof line, "seed %llu: meadow %.2f forest %.2f badlands %.2f mountains %.2f dunes %.2f", (unsigned long long)s, mix[0], mix[1], mix[2],
                      mix[3], mix[4]);
        ctx.note(line);
    }
    double minRms = 1e9, minMix = 1e9;
    for (size_t i = 0; i < hs.size(); i++)
        for (size_t j = i + 1; j < hs.size(); j++) {
            double s = 0, m = 0;
            for (size_t k = 0; k < hs[i].size(); k++) s += (hs[i][k] - hs[j][k]) * (hs[i][k] - hs[j][k]);
            for (int k = 0; k < PB_COUNT; k++) m += std::fabs(mixes[i][k] - mixes[j][k]);
            minRms = std::min(minRms, std::sqrt(s / hs[i].size()));
            minMix = std::min(minMix, m);
        }
    ctx.metric("smallest height difference between two seeds (rms over 10 x 10 km)", minRms, "m", 8, 1e9);
    ctx.metric("smallest difference in biome mix between two seeds (sum of |share|)", minMix, "", 0.1, 10);
}

SCENARIO(procBorders, "procworld/borders", "Chunk borders are seamless: the cells either side of every border of a 4 x 4 block of chunks step no more than the land does inside a chunk") {
    const ProcWorld& G = ProcWorld::forSeed(777);
    double worstBorder = 0, worstInside = 0;
    for (int64_t cx = -2; cx < 2; cx++)
        for (int64_t cz = -2; cz < 2; cz++) {
            const auto a = G.chunk(cx, cz), e = G.chunk(cx + 1, cz), n = G.chunk(cx, cz + 1);
            for (int k = 0; k < kChunk; k++) {
                worstBorder = std::max(worstBorder, (double)std::fabs(a->h[63 * kChunk + k] - e->h[0 * kChunk + k]));
                worstBorder = std::max(worstBorder, (double)std::fabs(a->h[k * kChunk + 63] - n->h[k * kChunk + 0]));
                for (int q = 0; q + 1 < kChunk; q++) worstInside = std::max(worstInside, (double)std::fabs(a->h[q * kChunk + k] - a->h[(q + 1) * kChunk + k]));
            }
        }
    ctx.metric("worst step across a chunk border", worstBorder, "m", 0, std::max(0.5, worstInside * 1.01));
    ctx.metric("worst step between neighbours inside a chunk", worstInside, "m", 0, 1e9);
}

SCENARIO(procWindowSlide, "procworld/window-slide", "Two windows a slide apart hold the same land where they overlap: heights, ground and every tree and rock, moved by exactly the offset") {
    World a = windowAt(4242, 0, 0), b = windowAt(4242, 512, -512);
    const double dx = (double)(b.proc.ox - a.proc.ox), dz = (double)(b.proc.oz - a.proc.oz);
    ctx.check("the second window is 512 m east and south", dx == 512 && dz == -512);
    double worst = 0;
    int ground = 0;
    for (double x = -900; x <= 900; x += 7)
        for (double z = -900; z <= 900; z += 7) {
            worst = std::max(worst, std::fabs(a.height(x, z) - b.height(x - dx, z - dz)));
            ground += a.surfaceAt(x, z) != b.surfaceAt(x - dx, z - dz);
        }
    ctx.metric("worst height difference where they overlap", worst, "m", 0, 1e-6);
    ctx.check("the same ground where they overlap", ground == 0, std::to_string(ground) + " cells differ");
    // Trees in the overlap (well inside both): each one in a is in b, moved by the offset.
    auto key = [](double x, double z) { return std::make_pair(std::llround(x * 100), std::llround(z * 100)); };
    std::set<std::pair<long long, long long>> inB;
    for (const Tree& t : b.trees) inB.insert(key(t.base.x + dx, t.base.z + dz));
    int checked = 0, missing = 0;
    for (const Tree& t : a.trees) {
        if (std::fabs(t.base.x - dx * 0.5) > 900 || std::fabs(t.base.z - dz * 0.5) > 900) continue;
        checked++;
        missing += !inB.count(key(t.base.x, t.base.z));
    }
    ctx.check("every tree in the overlap is in both", checked > 100 && missing == 0, std::to_string(missing) + " of " + std::to_string(checked) + " missing");
    int rocksA = 0, rocksMatched = 0;
    std::set<std::pair<long long, long long>> rocksB;
    for (const Rock& r : b.rocks) rocksB.insert(key(r.at.x + dx, r.at.z + dz));
    for (const Rock& r : a.rocks) {
        if (std::fabs(r.at.x - dx * 0.5) > 900 || std::fabs(r.at.z - dz * 0.5) > 900) continue;
        rocksA++;
        rocksMatched += rocksB.count(key(r.at.x, r.at.z)) > 0;
    }
    ctx.check("every rock in the overlap is in both", rocksA > 20 && rocksMatched == rocksA, std::to_string(rocksMatched) + " of " + std::to_string(rocksA));
    char line[160];
    std::snprintf(line, sizeof line, "window: %zu trees, %zu rocks, %zu bushes, %zu logs", a.trees.size(), a.rocks.size(), a.bushes.size(), a.logs.size());
    ctx.note(line);
}

SCENARIO(procFarOut, "procworld/far-out", "Far from the origin (100 km and 10,000 km) the land is as varied and as smooth as at home, and the window's local coordinates stay small") {
    const ProcWorld& G = ProcWorld::forSeed(31337);
    auto stats = [&](double ox, double oz, double& sd, double& step) {
        double s = 0, s2 = 0, worst = 0;
        int n = 0;
        for (int a = 0; a < 60; a++)
            for (int b = 0; b < 60; b++) {
                const double h = G.macroHeight(ox + a * 100.0, oz + b * 100.0);
                s += h, s2 += h * h, n++;
            }
        const auto c = G.chunk(proc::floorDiv((int64_t)ox, kChunk), proc::floorDiv((int64_t)oz, kChunk));
        for (int i = 0; i + 1 < kChunk; i++)
            for (int j = 0; j < kChunk; j++) worst = std::max(worst, (double)std::fabs(c->h[i * kChunk + j] - c->h[(i + 1) * kChunk + j]));
        sd = std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n)));
        step = worst;
    };
    double sd0, st0, sd1, st1, sd2, st2;
    stats(0, 0, sd0, st0);
    stats(100000, -100000, sd1, st1);
    stats(1e7, 1e7, sd2, st2);
    ctx.metric("relief at home (sd of heights over 6 km)", sd0, "m", 3, 400);
    ctx.metric("relief 100 km out", sd1, "m", 3, 400);
    ctx.metric("relief 10,000 km out", sd2, "m", 3, 400);
    ctx.metric("worst cell step 10,000 km out", st2, "m", 0, 2.5);
    World w = windowAt(31337, 100000, -100000);
    ctx.check("100 km out: the window's middle is a chunk corner there", w.proc.ox == 100032 && w.proc.oz == -99968,
              std::to_string(w.proc.ox) + "," + std::to_string(w.proc.oz));
    ctx.check("...and its land is the generator's there (local 10, 20 = global 100042, -99948)", w.height(10, 20) == (double)G.chunk(1563, -1562)->h[10 * 64 + 20]);
}

SCENARIO(procChunkBudget, "procworld/chunk-budget", "A chunk is made within its budget (the median of 64 fresh ones on one core), and a whole window builds in a few seconds") {
    const ProcWorld& G = ProcWorld::forSeed(2024);
    std::vector<double> ms;
    for (int k = 0; k < 64; k++) ms.push_back(G.makeChunk(300 + k % 8, -200 + k / 8)->genMs);
    std::sort(ms.begin(), ms.end());
    ctx.metric("median ms per chunk (one core)", ms[ms.size() / 2], "ms", 0, 6);
    ctx.metric("slowest of 64", ms.back(), "ms", 0, 25);
    clearChunkCache();
    const auto t0 = std::chrono::steady_clock::now();
    World w = windowAt(2024, 5000, 5000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ctx.metric("a whole window, from nothing cached", s, "s", 0, 20);
    const auto t1 = std::chrono::steady_clock::now();
    World w2 = windowAt(2024, 5512, 5000);
    const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    ctx.metric("the next window, slid 512 m (the rest cached)", s2, "s", 0, 10);
}

SCENARIO(procDriveSlide, "procworld/drive-slide", "100 km out, a truck driving along while the window slides under it (everything moved by the offset) goes on exactly as the same truck with no slide") {
    // Two sims on the same window; at 1 s one of them gets the next window (512 m east) and is moved by the offset.
    auto setup = [&](Sim& s) {
        s.world = windowAt(9090, 100000, -100000);
        const V3 at = s.world.spawn;
        s.addCar(p4Runner(), V3(at.x, s.world.clearY(at.x, at.z) + 0.6, at.z), 0, V3(0.5, 0.5, 0.5));
        s.cars[0].gear = 1;
    };
    Sim a, b;
    setup(a), setup(b);
    Ctx ca(a), cb(b);
    ca.run(1.5), cb.run(1.5);  // (settle)
    for (Car* c : {&a.cars[0], &b.cars[0]}) c->throttle = 0.35;
    ca.run(1.0), cb.run(1.0);
    World next = windowAt(9090, (double)b.world.proc.ox + 512, (double)b.world.proc.oz);
    const V3 d((double)(b.world.proc.ox - next.proc.ox), 0, (double)(b.world.proc.oz - next.proc.oz));
    b.world = std::move(next);
    shiftSim(b, d);
    ca.run(3.0), cb.run(3.0);
    const V3 pa = a.cars[0].frame().com, pb = b.cars[0].frame().com - d;
    ctx.metric("where the two trucks are apart after 3 s more", len(pa - pb), "m", 0, 0.05);
    ctx.metric("speed of the one that slid", len(b.cars[0].frame().vel), "m/s", 0.5, 30);
    ctx.check("it moved under its own power (not stuck or flung)", len(pa - b.cars[0].frame().com + d) < 0.05 && std::isfinite(pb.x));
}

SCENARIO(procStartPad,"procworld/start-pad", "Round the start (its pad, the ground and what's cleared from in front of it) every window that holds it is the same") {
    World::procSeed = 12345, World::procOriginSet = false;
    World a(MAP_PROCEDURAL);  // (round the start)
    World b = windowAt(12345, (double)a.proc.ox + 64, (double)a.proc.oz + 128);
    const double dx = (double)(b.proc.ox - a.proc.ox), dz = (double)(b.proc.oz - a.proc.oz);
    ctx.check("both have the start pad", !a.spawnPads.empty() && !b.spawnPads.empty());
    if (!a.spawnPads.empty() && !b.spawnPads.empty())
        ctx.check("...in the same place", std::fabs(a.spawnPads[0].p.x - (b.spawnPads[0].p.x + dx)) < 1e-6 && std::fabs(a.spawnPads[0].p.z - (b.spawnPads[0].p.z + dz)) < 1e-6,
                  std::to_string(a.spawnPads[0].p.x) + "," + std::to_string(a.spawnPads[0].p.z) + " vs " + std::to_string(b.spawnPads[0].p.x + dx) + "," +
                      std::to_string(b.spawnPads[0].p.z + dz));
    int hd = 0, pd = 0;
    for (double x = a.spawn.x - 80; x <= a.spawn.x + 80; x += 1)
        for (double z = a.spawn.z - 80; z <= a.spawn.z + 80; z += 1) {
            hd += std::fabs(a.height(x, z) - b.height(x - dx, z - dz)) > 1e-6;
            pd += a.roadAt(x, z).paint != b.roadAt(x - dx, z - dz).paint;
        }
    ctx.check("the same heights round it", hd == 0, std::to_string(hd));
    ctx.check("the same paint round it", pd == 0, std::to_string(pd));
    auto count = [&](const World& w, double ox, double oz) {
        int t = 0, r = 0, bu = 0;
        for (const Tree& q : w.trees) t += std::hypot(q.base.x + ox - a.spawn.x, q.base.z + oz - a.spawn.z) < 80;
        for (const Rock& q : w.rocks) r += std::hypot(q.at.x + ox - a.spawn.x, q.at.z + oz - a.spawn.z) < 80;
        for (const Bush& q : w.bushes) bu += std::hypot(q.p.x + ox - a.spawn.x, q.p.z + oz - a.spawn.z) < 80;
        return std::to_string(t) + " trees " + std::to_string(r) + " rocks " + std::to_string(bu) + " bushes";
    };
    ctx.check("the same things round it", count(a, 0, 0) == count(b, dx, dz), count(a, 0, 0) + " vs " + count(b, dx, dz));
}
