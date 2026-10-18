// Headless, repeatable CPU benchmark. No rendering, AI or invariant-check overhead.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../src/softbody.h"
#include "../src/simdkern.h"

int main(int argc, char** argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 12;
    const bool pileup = argc > 2 && std::strcmp(argv[2], "pileup") == 0;
    const int steps = argc > 3 ? std::atoi(argv[3]) : 4000;
    const int threads = resolvePhysicsThreads(argc > 4 ? std::atoi(argv[4]) : 1);
    if (count < 1 || count > 256 || steps < 1) return 1;
    Sim sim;
    sim.setPhysicsThreads(threads);
    if (getenv("CZ_SIMD0")) simdSetLevel(0);
    sim.setupEmpty();
    const auto& cat = vehicleCatalog();
    for (int i = 0; i < count; i++) {
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
