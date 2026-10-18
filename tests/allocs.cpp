// Heap allocations per frame in steady state (allocstat.h): the simulation frame should not touch the heap.
#include "../src/allocstat.h"
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
    const Count c = measure(ctx, 120, 240);
    ctx.metric("allocations per frame (mean)", c.mean, "", 0, 0.5);
    ctx.metric("allocations in the worst frame", (double)c.worst, "", 0, 4);
}
