// Headless, repeatable CPU benchmark. No rendering, AI or invariant-check overhead.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "../src/softbody.h"
#include "../src/simdkern.h"

int main(int argc, char** argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 12;
    const bool pileup = argc > 2 && std::strcmp(argv[2], "pileup") == 0;
    const bool traffic = argc > 2 && std::strcmp(argv[2], "traffic") == 0;  // bench_sim N traffic STEPS THREADS LOD(0/1)
    const int steps = argc > 3 ? std::atoi(argv[3]) : 4000;
    const int threads = resolvePhysicsThreads(argc > 4 ? std::atoi(argv[4]) : 1);
    if (count < 1 || count > 256 || steps < 1) return 1;
    Sim sim;
    sim.setPhysicsThreads(threads);
    if (getenv("CZ_SIMD0")) simdSetLevel(0);
    sim.setupEmpty();
    const auto& cat = vehicleCatalog();
    for (int i = 0; i < (traffic ? 1 : count); i++) {
        // On the proving ground's flat pad. Pileups use overlapping opposing rows.
        const double x = pileup ? -35 + (i % 2) * 3.5 : -100 + (i % 8) * 12;
        const double z = pileup ? -40 + (i / 2) * 1.5 : -60 + (i / 8) * 12;
        sim.addCar(cat[i % 4], V3(x, sim.world.clearY(x, z) + 0.05, z),
                   pileup && i % 2 ? M_PI : 0, V3(0.4, 0.5, 0.6));
        Car& c = sim.cars.back();
        c.throttle = 0.6;
        if (pileup)
            for (Node& n : c.nodes) n.v = V3(i % 2 ? -6 : 6, 0, 0);
    }
    if (traffic) {  // AI cars spread round the loop road, far from the one parked player: 60 Hz frames (AI step + physics), after 12 s of warm-up
        sim.trafficLod = argc > 5 && std::atoi(argv[5]) != 0;
        const auto& road = sim.world.loopRoad;
        const int n = (int)road.size();
        sim.cars[0].handbrake = 1;
        for (int i = 0; i < count; i++) {
            const int idx = (i * n / count + n / 3) % n, dir = i % 2 ? -1 : 1;
            const V3 d = road[((idx + dir) % n + n) % n] - road[idx];
            const V3 t = norm(V3(d.x, 0, d.z));
            const V3 p = road[idx] + V3(-t.z, 0, t.x) * 2.6;
            sim.addCar(cat[i % 4], V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), std::atan2(-t.z, t.x), V3(0.4, 0.5, 0.6));
            Car& c = sim.cars.back();
            c.ai = true, c.aiDir = dir, c.aiIndex = idx;
        }
        auto frame = [&] {
            sim.updateAI(1.0 / 60);
            sim.advance(1.0 / 60, 1000);
        };
        for (int k = 0; k < 720; k++) frame();
        const int frames = std::max(1, steps / 33);
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < frames; k++) frame();
        const double w = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%d traffic cars, LOD %s (%d proxies): %.3f ms/60Hz frame\n", count, sim.trafficLod ? "on" : "off", sim.proxyCount(), w / frames * 1000);
        return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    for (int k = 0; k < steps; k++) sim.substep();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Bitwise fingerprint catches changes in force accumulation/order as well as position.
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&](double d) {
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof bits);
        hash = (hash ^ bits) * 1099511628211ull;
    };
    for (const Car& c : sim.cars) {
        for (const Node& n : c.nodes) {
            mix(n.p.x); mix(n.p.y); mix(n.p.z);
            mix(n.v.x); mix(n.v.y); mix(n.v.z);
        }
        for (const Beam& b : c.beams) { mix(b.L0); mix(b.broken); }
    }
    std::printf("%d cars %s (%d threads): %.3f s wall, %.2fx real time, %.3f ms/60Hz frame, state %016llx\n",
                count, pileup ? "pileup" : "separated", threads, wall, steps * sim.h / wall,
                wall / (steps * sim.h) * 1000 / 60, (unsigned long long)hash);
}
