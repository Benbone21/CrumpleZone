// Autopilot perception and cost: the lidar sees every solid thing (signs, posts, gates, low
// beams, other cars), the planner uses the vehicle's real footprint, and eight autopilots
// together cost little per frame.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <string>

#include "../src/autopilot.h"
#include "../src/pilotsense.h"
#include "harness.h"

namespace {

// CPU time of this thread (ms): immune to other processes taking the core.
double cpuMs() {
#ifdef CLOCK_THREAD_CPUTIME_ID
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
#endif
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double headingTo(const V3& a, const V3& b) { return std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI; }

struct PerfStart {
    V3 from, to;
};

// Eight autopilots at once: time every pilotStep (the whole autopilot: planning, seeing,
// driving) for `seconds` of play, and report the cost per frame for all eight together.
void eightPilots(Ctx& ctx, const std::vector<PerfStart>& starts, double seconds, double budgetMs) {
    std::vector<int> idx;
    const VehicleSpec* specs[] = {&vehicleCatalog()[2], &vehicleCatalog()[3], &vehicleCatalog()[6], &vehicleCatalog()[5]};
    int k = 0;
    for (const PerfStart& s : starts) {
        Car& c = ctx.add(*specs[k % 4], s.from.x, s.from.z, headingTo(s.from, s.to));
        idx.push_back((int)(&c - ctx.sim.cars.data()));
        k++;
    }
    ctx.focus(ctx.sim.cars[idx[0]]);
    ctx.run(1.0);
    int started = 0;
    for (size_t i = 0; i < starts.size(); i++) {
        Car& c = ctx.sim.cars[idx[i]];
        pilotStart(c, ctx.sim.world, starts[i].to, "THE END", 35 / 3.6, PILOT_TRAILS);
        started += c.pilot.active;
    }
    ctx.check("all eight routes planned", started == (int)starts.size(), std::to_string(started) + " of " + std::to_string(starts.size()));
    std::vector<double> frameMs;
    int moving = 0;
    const double frames = seconds / Ctx::kFrame;
    for (int f = 0; f < frames; f++) {
        const double t0 = cpuMs();
        for (int i : idx)
            if (ctx.sim.cars[i].pilot.active) ctx.sim.pilotStep(i, Ctx::kFrame);
        frameMs.push_back(cpuMs() - t0);
        ctx.step();
    }
    for (int i : idx) moving += len(ctx.sim.cars[i].frame().vel) > 1.0;
    // The first second holds one-off costs (route grids for the map); steady state after.
    std::vector<double> steady(frameMs.begin() + std::min<size_t>(120, frameMs.size() / 2), frameMs.end());
    double sum = 0, worst = 0;
    for (double v : steady) sum += v, worst = std::max(worst, v);
    std::sort(steady.begin(), steady.end());
    const double mean = sum / std::max<size_t>(1, steady.size()), p99 = steady[steady.size() * 99 / 100];
    double first = 0;
    for (size_t f = 0; f < std::min<size_t>(120, frameMs.size()); f++) first = std::max(first, frameMs[f]);
    std::printf("PILOT COST: mean %.3f ms/frame, p99 %.3f, worst %.3f, worst of the first second %.3f (8 autopilots)\n", mean, p99, worst, first);
    ctx.metric("autopilot time per frame, 8 autopilots, mean", mean, "ms", 0, budgetMs);
    ctx.metric("autopilot time per frame, 8 autopilots, 99th percentile", p99, "ms", 0, budgetMs * 4);
    ctx.metric("autopilot time per frame, worst frame", worst, "ms", 0, 15);
    ctx.check("most of them are driving", moving >= 6, std::to_string(moving) + " moving");
}

}  // namespace

SCENARIO(pilotPerfBackcountry, "autopilot/perf-backcountry", "Eight autopilots at once on the Backcountry: what they cost per frame") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    std::vector<PerfStart> s;
    const char* trails[] = {"ridgeroad", "pass", "shelf", "wash", "hill", "cabin", "tower", "mud"};
    for (const char* n : trails) {
        const std::vector<V3>& t = backcountryTrail(n);
        const size_t a = std::min<size_t>(40, t.size() / 4), b = std::min(t.size() - 1, a + 320);
        s.push_back({t[a], t[b]});
    }
    eightPilots(ctx, s, 25, 2.0);
}

SCENARIO(pilotPerfFrontier, "autopilot/perf-frontier", "Eight autopilots at once on the Frontier (the race circuit and the desert): what they cost per frame") {
    ctx.sim.world = World(MAP_FRONTIER);
    std::vector<PerfStart> s;
    const std::vector<V3>& tr = frontierTrack();
    for (int k = 0; k < 8; k++) {
        const size_t a = (tr.size() * k / 8 + 5) % tr.size(), b = (a + 120) % tr.size();
        s.push_back({tr[a], tr[b]});
    }
    eightPilots(ctx, s, 25, 2.0);
}

SCENARIO(dbgDims, "zz/dims", "debug") {
    for (const VehicleSpec& vs : vehicleCatalog()) {
        Car& c = ctx.add(vs, 0, 0, 0);
        double zmin = 1e9, zmax = -1e9, ymax = -1e9, ymin = 1e9, xmin = 1e9, xmax = -1e9;
        double bzmax = 0, bymax = 0;
        std::vector<char> hub(c.nodes.size(), 0);
        for (const Wheel& wh : c.wheels) hub[wh.inner] = hub[wh.outer] = 1;
        for (size_t i = 0; i < c.nodes.size(); i++) {
            const V3& p = c.restLocal[i];
            zmin = std::min(zmin, p.z), zmax = std::max(zmax, p.z), ymax = std::max(ymax, p.y), ymin = std::min(ymin, p.y);
            xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x);
            if (!hub[i]) bzmax = std::max(bzmax, std::fabs(p.z)), bymax = std::max(bymax, p.y);
        }
        const Wheel& w0 = c.wheels[0];
        std::printf("%-18s spec %.2f x %.2f  nodes z [%.2f %.2f] bodyZ %.2f y [%.2f %.2f] bodyYmax %.2f x [%.2f %.2f] R %.2f wheelw %.2f hubspan %.2f\n", vs.name, vs.length, vs.width, zmin, zmax, bzmax, ymin, ymax, bymax, xmin, xmax,
                    w0.R, w0.width, len(c.restLocal[w0.inner] - c.restLocal[w0.outer]));
        c.nodes.clear();
        ctx.sim.cars.pop_back();
    }
}
