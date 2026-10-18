#include "harness.h"
#include "../src/collision.h"
#include "../src/thread_cache.h"
#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace {
std::atomic<int> cacheDestroyed{0};
struct CacheProbe {
    std::vector<int> values;
    ~CacheProbe() { cacheDestroyed++; }
};
}

SCENARIO(threadCaches, "engine/thread-cache", "Scratch caches stay isolated and destroy their storage safely at thread exit") {
    const int before = cacheDestroyed.load();
    std::atomic<bool> isolated{true};
    for (int batch = 0; batch < 32; batch++) {
        std::vector<std::thread> threads;
        for (int id = 0; id < 4; id++) threads.emplace_back([&, id] {
            for (int use = 0; use < 100; use++) {
                CZ_THREAD_CACHE(CacheProbe, first);
                CZ_THREAD_CACHE(CacheProbe, second);
                if (use == 0) {
                    if (!first.values.empty() || !second.values.empty()) isolated = false;
                    first.values.assign(100, id);
                    second.values.assign(100, id + 10);
                }
                if (&first == &second || first.values[99] != id || second.values[99] != id + 10) isolated = false;
            }
        });
        for (auto& thread : threads) thread.join();
    }
    ctx.check("independent caches retain only their own thread's data", isolated);
    ctx.check("both caches destroyed for all 128 joined threads", cacheDestroyed.load() - before == 256);
}

SCENARIO(collisionBroadphase, "engine/collision-bounds", "Swept bounds exactly match all-pairs, including touching bounds, crossings and body removal") {
    std::vector<Car> cars(80);
    std::vector<std::pair<int, int>> got, expected;
    std::vector<int> order;
    std::mt19937 rng(74291);
    std::uniform_real_distribution<double> pos(-20, 20), size(0, 8);
    bool same = true;
    for (int frame = 0; frame < 80; frame++) {
        const int count = frame == 20 ? 12 : frame == 40 ? 31 : 80;
        cars.resize(count);
        for (Car& c : cars) {
            c.bbMin = V3(pos(rng), pos(rng), pos(rng));
            c.bbMax = c.bbMin + V3(size(rng), size(rng), size(rng));
        }
        // Exact contact, equal minima, contained bounds, and zero-size bounds.
        cars[0].bbMin = V3(-2, -2, -2); cars[0].bbMax = V3(0, 0, 0);
        cars[1].bbMin = V3(0, 0, 0); cars[1].bbMax = V3(2, 2, 2);
        cars[2].bbMin = cars[2].bbMax = V3(0, 0, 0);
        expected.clear();
        for (int i = 0; i < count; i++)
            for (int j = i + 1; j < count; j++) {
                const Car& a = cars[i]; const Car& b = cars[j];
                if (a.bbMin.x <= b.bbMax.x && b.bbMin.x <= a.bbMax.x &&
                    a.bbMin.y <= b.bbMax.y && b.bbMin.y <= a.bbMax.y &&
                    a.bbMin.z <= b.bbMax.z && b.bbMin.z <= a.bbMax.z) expected.emplace_back(i, j);
            }
        collision::candidates(cars, got, order);
        same &= got == expected;
    }
    ctx.check("same pairs and force order across 80 changing scenes", same);
    cars.clear();
    collision::candidates(cars, got, order);
    ctx.check("empty scene", got.empty());
}

SCENARIO(collisionSleepCache, "engine/collision-cache", "A parked car reuses collision geometry and invalidates it on movement and rebuild") {
    Sim& sim = ctx.sim;
    ctx.add(vehicleCatalog()[0], -40, -40, 0);
    ctx.add(vehicleCatalog()[0], -37, -40, 180);
    // Kinematic probe against a stationary support: they overlap, without integrating it.
    Car& probe = sim.cars[0];
    Car& support = sim.cars[1];
    probe.remote = true;
    support.sleeping = true;
    support.stillTime = 2;
    sim.substep();
    const long stamp = support.grid->stamp, nodeStamp = support.nodeGridStamp;
    sim.substep();
    ctx.check("sleeping geometry was reused", stamp > 0 && stamp == support.grid->stamp && nodeStamp == support.nodeGridStamp);
    support.translate(V3(0.01, 0, 0));
    ctx.check("translation invalidates both grids", support.grid->stamp < 0 && support.nodeGridStamp < 0);
    sim.substep();
    ctx.check("geometry rebuilt after translation", support.grid->stamp > stamp);
    support.nodes[0].p.x += 0.01;
    support.updateBounds();
    ctx.check("bounds update invalidates both grids", support.grid->stamp < 0 && support.nodeGridStamp < 0);
    sim.substep();
    support.respawn(support.spawnPos, support.spawnYaw);
    ctx.check("rebuild invalidates both grids", support.grid->stamp < 0 && support.nodeGridStamp < 0);
}

SCENARIO(collisionWorkers, "engine/physics-workers", "Parallel forces and collision preparation match serial driving and pileups exactly") {
    bool same = true, finite = true;
    for (bool pileup : {false, true}) {
        Sim parallel;
        Sim& serial = ctx.sim;
        serial.setupEmpty();
        parallel.setupEmpty();
        const int requested = std::min(5, physicsThreadLimit());
        parallel.setPhysicsThreads(requested);
        ctx.check("thread budget above four is honored when available", parallel.physicsThreads() == requested);
        for (Sim* sim : {&serial, &parallel})
            for (int i = 0; i < 12; i++) {
                const double x = pileup ? -40 + (i % 2) * 3 : -70 + (i % 4) * 12;
                const double z = pileup ? -40 + (i / 2) * 1.7 : -60 + (i / 4) * 12;
                sim->addCar(vehicleCatalog()[i % 7], V3(x, sim->world.clearY(x, z) + 0.05, z),
                            i % 2 ? M_PI : 0, V3(0.4, 0.5, 0.6));
                Car& c = sim->cars.back();
                c.throttle = 0.5;
                if (pileup) for (Node& n : c.nodes) n.v = V3(i % 2 ? -6 : 6, 0, 0);
            }
        for (int step = 0; step < 800; step++) {
            if (step == 200) parallel.setPhysicsThreads(1);
            if (step == 400) parallel.setPhysicsThreads(requested);
            serial.substep();
            parallel.substep();
            // Compare every step: differences can vanish at the final frame.
            for (size_t i = 0; i < serial.cars.size(); i++) {
                const Car& a = serial.cars[i]; const Car& b = parallel.cars[i];
                same &= a.impactEnergy == b.impactEnergy && a.sleeping == b.sleeping;
                for (size_t k = 0; k < a.nodes.size(); k++) {
                    const Node& x = a.nodes[k]; const Node& y = b.nodes[k];
                    same &= x.p.x == y.p.x && x.p.y == y.p.y && x.p.z == y.p.z &&
                            x.v.x == y.v.x && x.v.y == y.v.y && x.v.z == y.v.z &&
                            x.f.x == y.f.x && x.f.y == y.f.y && x.f.z == y.f.z;
                    finite &= std::isfinite(y.p.x + y.p.y + y.p.z);
                }
                for (size_t k = 0; k < a.beams.size(); k++)
                    same &= a.beams[k].L0 == b.beams[k].L0 && a.beams[k].broken == b.beams[k].broken;
                for (size_t k = 0; k < a.wheels.size(); k++)
                    same &= a.wheels[k].omega == b.wheels[k].omega && a.wheels[k].shaftBroken == b.wheels[k].shaftBroken;
            }
        }
        parallel.setPhysicsThreads(1);  // Also exercises teardown after a workload.
    }
    ctx.check("serial and configurable-thread states identical, including live changes", same);
    ctx.check("all positions finite", finite);
    PhysicsWorkers workers(3);
    bool caught = false;
    try { workers.run(12, [](int i) { if (i == 5) throw std::runtime_error("worker probe"); }); }
    catch (const std::runtime_error&) { caught = true; }
    ctx.check("worker exceptions return to the caller", caught);
    std::atomic<int> completed{0};
    workers.run(23, [&](int) { completed++; });
    ctx.check("pool still works after an exception", completed == 23);
    ctx.check("explicit counts can use all logical CPUs", resolvePhysicsThreads(24, 24) == 24);
    ctx.check("counts adapt after CPU changes", resolvePhysicsThreads(24, 8) == 8);
    ctx.check("unknown CPU count falls back to one", resolvePhysicsThreads(20, 0) == 1);
    ctx.check("automatic retains the measured conservative budget", resolvePhysicsThreads(0, 24) == 4);
}

// The bitset grid has to hand back exactly what the old chained hash did (contact sums run in
// that order): every item covering a cell of p's bucket, in insertion order, once per such cell.
SCENARIO(triGridChains, "engine/tri-grid", "The contact grid returns what chained hash buckets would, collisions and repeats included") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(-3, 3), R(0.05, 0.9);
    bool same = true, sets = true;
    int slabRuns = 0, chainRuns = 0, repeats = 0;
    std::vector<int> got;
    std::vector<uint64_t> mask;
    for (int run = 0; run < 40; run++) {
        TriGrid g;
        std::vector<std::array<int, 6>> boxes;
        std::vector<int> vals;
        g.clear();
        const int n = 50 + run * 13;
        for (int i = 0; i < n; i++) {
            V3 c(U(rng), U(rng) * 0.5, U(rng)), r(R(rng), R(rng), R(rng));
            if (run % 4 == 3 && i == n / 2) c = V3(400, 0, -300);  // one far-flung piece: chains
            V3 mn = c - r, mx = c + r;
            if (run % 4 == 1 && i == n / 3) mn = V3(-1, -1, -1), mx = V3(9.9, 9.9, 9.9);  // spans cells that share buckets

            const int v = (i * 7919) % 5000;
            g.insert(v, mn, mx);
            vals.push_back(v);
            boxes.push_back({(int)std::floor(mn.x / TriGrid::kCell), (int)std::floor(mx.x / TriGrid::kCell),
                             (int)std::floor(mn.y / TriGrid::kCell), (int)std::floor(mx.y / TriGrid::kCell),
                             (int)std::floor(mn.z / TriGrid::kCell), (int)std::floor(mx.z / TriGrid::kCell)});
        }
        g.finish();
        (g.slabs ? slabRuns : chainRuns)++;
        mask.assign(g.words(), 0);
        for (int q = 0; q < 400; q++) {
            const V3 p(U(rng) * 1.5, U(rng) * 0.8, U(rng) * 1.5);
            const int key = TriGrid::key((int)std::floor(p.x / TriGrid::kCell), (int)std::floor(p.y / TriGrid::kCell),
                                         (int)std::floor(p.z / TriGrid::kCell));
            std::vector<int> want;
            std::vector<uint64_t> wantMask(g.words(), 0);
            for (int i = 0; i < n; i++) {
                const auto& b = boxes[i];
                int hits = 0;
                for (int x = b[0]; x <= b[1]; x++)
                    for (int y = b[2]; y <= b[3]; y++)
                        for (int z = b[4]; z <= b[5]; z++)
                            if (TriGrid::key(x, y, z) == key) want.push_back(vals[i]), hits++;
                if (hits) wantMask[i >> 6] |= 1ull << (i & 63);
                repeats += hits > 1;
            }
            const int k = g.collect(p, got);
            same &= k == (int)want.size() && std::equal(want.begin(), want.end(), got.begin());
            g.meets(p, mask.data());
            sets &= mask == wantMask;
        }
    }
    ctx.check("both layouts exercised (bitsets " + std::to_string(slabRuns) + ", chains " + std::to_string(chainRuns) + ")",
              slabRuns > 0 && chainRuns > 0);
    ctx.check("hash collisions exercised (" + std::to_string(repeats) + " repeats)", repeats > 0);
    ctx.check("collect matches the chained buckets item for item", same);
    ctx.check("meets marks exactly the items met", sets);
}
