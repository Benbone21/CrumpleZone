// The solver's SIMD paths: every instruction-set variant must agree to the last bit (clients on
// different CPUs have to simulate alike), and the worker pool must hand out every job once.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

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
