// The solver's SIMD paths: every instruction-set variant must agree to the last bit (clients on
// different CPUs have to simulate alike), and the worker pool must hand out every job once.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

#include "../src/simdkern.h"
#include "harness.h"

namespace {
bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }
}  // namespace

SCENARIO(simdKernelAgree, "engine/simd-kernel", "The beam-geometry kernel gives identical bits on every instruction set, and the right numbers") {
    Sim& sim = ctx.sim;
    Car& car = ctx.add(vehicleCatalog()[3], -100, -60, 0);
    ctx.run(0.3);
    const int saved = simdLevel();
    const int n = (int)car.beams.size();
    ctx.check("a car with beams to sweep", n > 100);
    std::vector<double> buf[2];
    for (int lv = 0; lv < 2; lv++) {
        simdSetLevel(lv);
        buf[lv].assign(5 * n, 0);
        BeamGeo g = {buf[lv].data(), buf[lv].data() + n, buf[lv].data() + 2 * n, buf[lv].data() + 3 * n, buf[lv].data() + 4 * n};
        beamGeometry(car.beams.data(), n, car.nodes.data(), g);
    }
    simdSetLevel(saved);
    bool bits = true, close = true;
    for (int i = 0; i < 5 * n; i++) bits &= same(buf[0][i], buf[1][i]);
    // Against plain maths (the test build may fuse, so not bit for bit).
    for (int i = 0; i < n; i++) {
        const Node& a = car.nodes[car.beams[i].a];
        const Node& b = car.nodes[car.beams[i].b];
        const V3 d = b.p - a.p;
        const double L = len(d);
        close &= std::fabs(buf[1][i] - L) <= 1e-12 * (1 + L);
        if (L > 1e-9) close &= std::fabs(buf[1][n + i] - d.x / L) < 1e-12 && std::fabs(buf[1][4 * n + i] - dot(b.v - a.v, d / L)) < 1e-9;
    }
    ctx.check("AVX2 and baseline kernels agree bit for bit (" + std::string(simdName()) + " in use)", bits);
    ctx.check("lengths, directions and closing speeds match plain maths", close);
    (void)sim;
}

SCENARIO(simdSimAgree, "engine/simd-sim", "A crash simulated on the SIMD path and on the baseline path stays bit identical") {
    Sim a, b;
    a.setupEmpty();
    b.setupEmpty();
    for (Sim* sim : {&a, &b})
        for (int i = 0; i < 6; i++) {
            const double x = -40 + (i % 2) * 3, z = -40 + (i / 2) * 1.7;
            sim->addCar(vehicleCatalog()[i % 7], V3(x, sim->world.clearY(x, z) + 0.05, z), i % 2 ? M_PI : 0, V3(0.4, 0.5, 0.6));
            Car& c = sim->cars.back();
            c.throttle = 0.5;
            for (Node& n : c.nodes) n.v = V3(i % 2 ? -6 : 6, 0, 0);
        }
    const int saved = simdLevel();
    bool equal = true, damaged = false;
    for (int step = 0; step < 600; step++) {
        simdSetLevel(0);
        a.substep();
        simdSetLevel(1);
        b.substep();
        for (size_t i = 0; i < a.cars.size() && equal; i++) {
            const Car& x = a.cars[i];
            const Car& y = b.cars[i];
            equal &= same(x.impactEnergy, y.impactEnergy);
            for (size_t k = 0; k < x.nodes.size(); k++)
                equal &= same(x.nodes[k].p.x, y.nodes[k].p.x) && same(x.nodes[k].p.y, y.nodes[k].p.y) &&
                         same(x.nodes[k].p.z, y.nodes[k].p.z) && same(x.nodes[k].f.y, y.nodes[k].f.y);
            for (size_t k = 0; k < x.beams.size(); k++) equal &= same(x.beams[k].L0, y.beams[k].L0) && x.beams[k].broken == y.beams[k].broken;
        }
    }
    simdSetLevel(saved);
    for (const Car& c : a.cars) damaged |= c.impactEnergy > 1000;
    ctx.check("the head-on pileup does bend metal (so the plastic path ran)", damaged);
    ctx.check("both paths agree on every node, beam and energy, every substep", equal);
}

SCENARIO(poolEveryJobOnce, "engine/pool-jobs", "The spinning worker pool runs every job exactly once, however the batches are sized or spaced") {
    PhysicsWorkers pool(4);
    std::vector<std::atomic<int>> hits(64);
    bool once = true;
    for (int round = 0; round < 3000; round++) {
        const int n = 1 + round % 37;
        for (auto& h : hits) h = 0;
        pool.run(n, [&](int i) { hits[i]++; });
        for (int i = 0; i < 64; i++) once &= hits[i] == (i < n ? 1 : 0);
        if (round % 500 == 499) {  // idle long enough for the workers to go to sleep, then wake them
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    ctx.check("3000 batches of 1..37 jobs: each index exactly once", once);
    int calls = 0;
    pool.run(0, [&](int) { calls++; });
    ctx.check("an empty batch does nothing", calls == 0);
}

SCENARIO(poolStress, "engine/pool-stress", "Pools hammered from many threads, new pools used at once, one pool shared: no lost wakeups") {
    std::atomic<long> total{0}, expected{0};
    std::atomic<bool> bad{false};
    auto churn = [&](int seed) {  // fresh pools, used the moment they exist
        for (int round = 0; round < 150; round++) {
            PhysicsWorkers pool(2 + (seed + round) % 4);
            for (int b = 0; b < 40; b++) {
                const int n = 1 + (b * 7 + seed) % 23;
                pool.run(n, [&](int i) { total += i + 1; });
                expected += n * (n + 1) / 2;
            }
        }
    };
    PhysicsWorkers shared(4);
    auto share = [&](int seed) {  // one pool, several callers
        for (int b = 0; b < 3000; b++) {
            const int n = 1 + (b + seed) % 16;
            std::atomic<int> sum{0};
            shared.run(n, [&](int i) { sum += i + 1; });
            if (sum != n * (n + 1) / 2) bad = true;
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) threads.emplace_back(churn, t);
    for (int t = 0; t < 4; t++) threads.emplace_back(share, t);
    for (auto& t : threads) t.join();
    ctx.check("every job of every batch ran, from every caller", total == expected && !bad);
}

SCENARIO(simdFacesAgree, "engine/simd-faces", "The contact face sift gives the same faces on every instruction set, and the scalar test's") {
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> U(-1, 1);
    const int words = 6, S = 64 * words;
    std::vector<double> rows(16 * S);
    for (int t = 0; t < S; t++) {
        const V3 a(U(rng), U(rng), U(rng)), e0(U(rng), U(rng), U(rng));
        V3 e1(U(rng), U(rng), U(rng));
        if (t % 17 == 0) e1 = e0 * 2.0;  // degenerate: den ~ 0
        const V3 n = norm(cross(e0, e1));
        const double d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1);
        double row[16] = {a.x, a.y, a.z, n.x, n.y, n.z, e0.x, e0.y, e0.z, e1.x, e1.y, e1.z, d00, d01, d11, d00 * d11 - d01 * d01};
        if (t % 29 == 0) row[3] = NAN;  // a NaN normal passes, as in the scalar test
        for (int k = 0; k < 16; k++) rows[k * S + t] = row[k];
    }
    const FaceGeo f = FaceGeo::rows(rows.data(), S);
    const int saved = simdLevel();
    bool agree = true, scalar = true;
    long kept = 0;
    for (int q = 0; q < 20000; q++) {
        uint64_t cand[words], keep[2][words];
        for (uint64_t& c : cand) c = (uint64_t)rng() << 32 | rng();
        if (q % 3 == 0) cand[q % words] = 0;
        const V3 p(U(rng), U(rng), U(rng));
        const double radius = q % 2 ? 0 : 0.35;
        for (int lv = 0; lv < 2; lv++) {
            simdSetLevel(lv);
            faceCandidates(f, words, cand, p, radius, keep[lv]);
        }
        for (int w = 0; w < words; w++) {
            agree &= keep[0][w] == keep[1][w];
            kept += __builtin_popcountll(keep[1][w]);
            for (int b = 0; b < 64; b++) {
                // The contact test as written in pointsVsSkin.
                const int t = 64 * w + b;
                const V3 a(f.ax[t], f.ay[t], f.az[t]), nrm(f.nx[t], f.ny[t], f.nz[t]);
                const double d = dot(p - a, nrm), pen = radius - d;
                const V3 q3 = p - a;
                const double d20 = dot(q3, V3(f.e0x[t], f.e0y[t], f.e0z[t])), d21 = dot(q3, V3(f.e1x[t], f.e1y[t], f.e1z[t]));
                const double den = f.den[t];
                const double v = (f.d11[t] * d20 - f.d01[t] * d21) / den, ww = (f.d00[t] * d21 - f.d01[t] * d20) / den, u = 1 - v - ww;
                const bool want = (cand[w] >> b & 1) && !(pen <= 0 || d < -0.4) && !(den < 1e-12) &&
                                  !(u < -0.02 || v < -0.02 || ww < -0.02);
                scalar &= want == (keep[1][w] >> b & 1);
            }
        }
    }
    simdSetLevel(saved);
    ctx.check("some faces kept (" + std::to_string(kept) + ")", kept > 1000);
    ctx.check("AVX2 and baseline sifts agree (" + std::string(simdName()) + " in use)", agree);
    ctx.check("the sift keeps exactly the faces the scalar test would", scalar);
}
