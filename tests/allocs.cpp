// Heap allocations per frame in steady state (allocstat.h): the simulation frame should not touch the heap.
#include "../src/allocstat.h"
#include <unistd.h>

#include <thread>

#include "../src/net.h"
#include "harness.h"

namespace {
// Allocations on this thread per frame of fn, after a warm-up; returns the worst frame and the mean.
struct Count {
    uint64_t worst = 0;
    double mean = 0;
};
Count measure(Ctx& ctx, int warm, int frames) {
    for (int k = 0; k < warm; k++) ctx.step();
    Count c;
    uint64_t sum = 0;
    const bool tr = std::getenv("CZ_ALLOC_TRACE") != nullptr;  // (the top call sites, for addr2line)
    allocstat::trace(tr);
    for (int k = 0; k < frames; k++) {
        const uint64_t before = allocstat::count();
        ctx.step();
        const uint64_t n = allocstat::count() - before;
        sum += n, c.worst = std::max(c.worst, n);
    }
    c.mean = (double)sum / frames;
    if (tr) allocstat::dump(stdout, 30);
    allocstat::trace(false);
    return c;
}
}  // namespace

SCENARIO(allocs_sim_drive, "allocs/sim-drive", "a car driving and three AI cars in traffic allocate nothing per frame in steady state") {
    const VehicleSpec& spec = vehicleCatalog()[2];
    Car& a = ctx.add(spec, -100, 0, 0);
    ctx.launch(a, 12);
    ctx.add(vehicleCatalog()[3], -60, 4, 0, true);
    ctx.add(vehicleCatalog()[6], -40, -4, 0, true);
    ctx.add(vehicleCatalog()[1], -80, 8, 0, true);
    ctx.focus(a);
    const Count c = measure(ctx, 240, 360);
    ctx.metric("allocations per frame (mean)", c.mean, "", 0, 0.5);
    ctx.metric("allocations in the worst frame", (double)c.worst, "", 0, 64);  // (a car meeting another car's contact grid for the first time grows it once)
}

SCENARIO(allocs_net, "allocs/net", "a host and a client over loopback allocate (almost) nothing per frame sending and receiving state in steady state") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = 41000 + (int)(::getpid() % 2000);
    if (!ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error)) return;
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    NetSession client;
    bool joined = false, joinDone = false;
    std::thread joiner([&] {
        joined = client.join("127.0.0.1:" + std::to_string(port), "BUDDY", 3.0);
        joinDone = true;
    });
    for (int k = 0; k < 400 && !joinDone; k++) {
        host.receive(ctx.sim, ctx.t);
        usleep(5000);
    }
    joiner.join();
    if (!ctx.check("client is let in", joined, client.error)) return;
    client.adopt(sim2);
    uint64_t netAllocs = 0, worst = 0;
    int counted = 0;
    const bool tr = std::getenv("CZ_ALLOC_TRACE") != nullptr;
    auto frameBoth = [&](bool count) {
        const uint64_t before = allocstat::count();
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        client.send(sim2, ctx.t);
        const uint64_t n = allocstat::count() - before;
        if (count) netAllocs += n, worst = std::max(worst, n), counted++;
        sim2.drive(false, false, false, false, false, Ctx::kFrame);
        sim2.advance(Ctx::kFrame, 1000);
        for (Car& c : sim2.cars) c.updateStats();
        usleep(300);
    };
    ctx.run(3.0, [&] { frameBoth(false); });
    allocstat::trace(tr);
    ctx.run(3.0, [&] { frameBoth(true); });
    if (tr) allocstat::dump(stdout, 30);
    allocstat::trace(false);
    ctx.metric("net allocations per frame (mean, both ends)", (double)netAllocs / std::max(1, counted), "", 0, 20);  // (was 63 before the reserves; the rest is mostly the received snapshot)
    ctx.metric("net allocations in the worst frame", (double)worst, "", 0, 64);
}
