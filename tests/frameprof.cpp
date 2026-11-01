// Frame timing and the benchmark (frameprof.h): stage times add up, the benchmark's average and 1% low, its report, the
// lap it flies. No SDL or GL.
#include <cmath>
#include <cstring>
#include <string>

#include "../src/frameprof.h"
#include "harness.h"

SCENARIO(frameprofStages, "perf/frame-stages", "Stage times add up per frame, re-entered stages accumulate, the smoothed ones follow") {
    FrameProfile fp;
    fp.beginFrame();
    fp.addCpu(FP_SIM, 2), fp.addCpu(FP_SHADOWS, 3), fp.addCpu(FP_SIM, 1);  // (SIM twice: they add)
    fp.beginFrame();
    ctx.check("the first frame's stages are its 'last'", std::fabs(fp.last[FP_SIM] - 3) < 0.01 && std::fabs(fp.last[FP_SHADOWS] - 3) < 0.01);
    ctx.check("the first frame sets the smoothed ones outright", std::fabs(fp.cpu[FP_SIM] - 3) < 0.01);
    for (int i = 0; i < 200; i++) {
        fp.addCpu(FP_SIM, 1);
        fp.beginFrame();
    }
    ctx.metric("smoothed SIM after 200 frames of 1 ms", fp.cpu[FP_SIM], "ms", 0.99, 1.02);
    ctx.check("a stage with nothing in it goes to zero", fp.cpu[FP_SHADOWS] < 0.01);
    // Real time: enter() hands the clock from one stage to the next.
    FrameProfile live;
    live.beginFrame();
    live.enter(FP_POST);
    double spin = 0;
    for (int i = 0; i < 2000000; i++) spin += std::sqrt((double)i);
    live.enter(FP_UI);
    live.beginFrame();
    ctx.check("the time spent goes to the stage it was spent in", live.last[FP_POST] > 0 && live.last[FP_POST] > live.last[FP_UI] && spin > 0);
    ctx.check("names", !std::strcmp(framePassName(FP_SIM), "SIM") && !std::strcmp(framePassName(FP_SWAP), "SWAP") && framePassDraws(FP_TERRAIN) &&
                           !framePassDraws(FP_SIM) && !framePassDraws(FP_SWAP));
    double g[FP_COUNT];
    for (int i = 0; i < FP_COUNT; i++) g[i] = i == FP_POST ? 4 : -1;
    fp.setGpu(g);
    ctx.check("GPU times: valid once set, unmeasured stages left alone", fp.gpuValid && std::fabs(fp.gpu[FP_POST] - 4) < 1e-9 && fp.gpu[FP_SIM] == 0);
}

SCENARIO(frameprofBench, "perf/bench-stats", "The benchmark's average, 1% low and per-stage averages, and its report") {
    BenchStats b;
    double cpu[FP_COUNT] = {}, gpu[FP_COUNT];
    for (int i = 0; i < FP_COUNT; i++) gpu[i] = -1;
    cpu[FP_SIM] = 2, cpu[FP_TERRAIN] = 1, gpu[FP_TERRAIN] = 3;
    for (int i = 0; i < 1000; i++) b.add(i % 100 == 0 ? 50.0 : 10.0, cpu, i < 500 ? gpu : nullptr);  // 10 frames of 50 ms
    b.add(NAN, cpu, gpu);
    const BenchStats::Result r = b.result();
    ctx.check("junk frames are left out", r.frames == 1000);
    ctx.metric("average", r.avgMs, "ms", 10.39, 10.41);
    ctx.metric("1% low (the 99th percentile)", r.lowMs, "ms", 49.9, 50.1);
    ctx.metric("1% low fps", r.lowFps, "fps", 19.9, 20.1);
    ctx.check("per-stage CPU averages", std::fabs(r.cpu[FP_SIM] - 2) < 1e-9 && std::fabs(r.cpu[FP_TERRAIN] - 1) < 1e-9);
    ctx.check("GPU averages over the frames that had them; none: -1", std::fabs(r.gpu[FP_TERRAIN] - 3) < 1e-9 && r.gpu[FP_SHADOWS] == -1);
    const std::string rep = BenchStats::report(r, "TEST MAP");
    ctx.check("the report has the title, the summary, every stage and a csv line",
              rep.find("bench: TEST MAP") != std::string::npos && rep.find("1% low 50.00 ms") != std::string::npos &&
                  rep.find("VEGETATION") != std::string::npos && rep.find("bench-csv: avg=10.40") != std::string::npos &&
                  rep.find("TERRAIN=1.00/3.00") != std::string::npos);
    BenchStats empty;
    ctx.check("an empty benchmark is all zeros", empty.result().frames == 0 && empty.result().avgMs == 0);
}

namespace {
double hill(const void*, double x, double z) { return 0.1 * x + 0.05 * z; }
}  // namespace

SCENARIO(frameprofPath, "perf/bench-path", "The benchmark lap: round the centre at the radius, above the ground, the same every run") {
    double worstR = 0, lowest = 1e9;
    bool same = true;
    for (int i = 0; i <= 40; i++) {
        const double t = i / 40.0;
        const BenchPose p = benchPose(100, -50, t, 30, 2.5, hill, nullptr), q = benchPose(100, -50, t, 30, 2.5, hill, nullptr);
        worstR = std::max(worstR, std::fabs(std::hypot(p.x - 100, p.z + 50) - 30));
        lowest = std::min(lowest, p.y - hill(nullptr, p.x, p.z));
        same = same && p.x == q.x && p.y == q.y && p.yaw == q.yaw;
    }
    ctx.check("on the circle", worstR < 1e-6);
    ctx.metric("lowest height over the ground", lowest, "m", 2.49, 4);
    ctx.check("repeatable", same);
    const BenchPose a = benchPose(0, 0, 0, 30, 2, nullptr, nullptr), z = benchPose(0, 0, 1, 30, 2, nullptr, nullptr);
    ctx.check("t = 1 is t = 0 (a closed lap)", std::fabs(a.x - z.x) < 1e-9 && std::fabs(a.z - z.z) < 1e-9);
    // At t = 0 it's at +X going towards +Z, turned in: it looks between +Z and -X (yaw 0 looks +X, 90 -Z).
    const double lx = std::cos(a.yaw), lz = -std::sin(a.yaw);
    ctx.check("looks along the lap, turned in", lz > 0.8 && lx < 0 && lx > -0.6);
}

SCENARIO(frameprofSpikes, "perf/spikes", "A spike is a frame over 30 ms and twice the typical one; the log line names the stages that ran long") {
    ctx.check("spikes", frameSpike(80, 16) && !frameSpike(25, 10) && !frameSpike(40, 30) && frameSpike(61, 30));
    double st[FP_COUNT] = {};
    st[FP_SIM] = 40, st[FP_TERRAIN] = 9, st[FP_UI] = 1;
    char line[200];
    describeSpike(line, sizeof line, 77, 55, 16, st);
    ctx.check("the line", std::string(line) == "spike: frame 77 took 55.0 ms (typical 16.0): SIM 40.0 ms, TERRAIN 9.0 ms", line);
    BenchStats b;
    for (int i = 0; i < 200; i++) b.add(i == 50 || i == 150 ? 70.0 : 16.0, nullptr, nullptr);
    const BenchStats::Result r = b.result();
    ctx.check("two hitches, two frames over 30 ms", r.hitches == 2 && r.over30 == 2);
}

SCENARIO(frameprofCatchUp, "perf/sim-catch-up", "Physics catch-up: slow frames run in real time, a hitch isn't caught up all at once, at most 50 ms of steps a frame (multiplayer: what's owed, within 50 ms of physics)") {
    double usual = 1.0 / 60;
    double ran = 0;
    for (int i = 0; i < 300; i++) ran += simCatchUp(1.0 / 30, usual);  // (a steady 30 fps)
    ctx.metric("30 fps for 10 s: physics time run", ran, "s", 9.99, 10.01);
    usual = 1.0 / 60;
    for (int i = 0; i < 100; i++) simCatchUp(1.0 / 60, usual);
    const double hitch = simCatchUp(0.1, usual), next = simCatchUp(1.0 / 60, usual);
    ctx.check("a 100 ms hitch at 60 fps runs one usual frame", std::fabs(hitch - 1.0 / 60) < 0.002, std::to_string(hitch));
    ctx.check("and the next frame is ordinary", std::fabs(next - 1.0 / 60) < 1e-9);
    ctx.check("50 ms of steps at 2 kHz is 100", simMaxSteps(1.0 / 2000) == 100 && simMaxSteps(0) == 1);
    ctx.check("junk dt runs nothing", simCatchUp(NAN, usual) == 0 && simCatchUp(-1, usual) == 0);
    // Multiplayer: what's owed, as far as 50 ms of physics goes; never under the 50 ms of steps.
    const double h = 1.0 / 2000;
    ctx.check("multiplayer, 12 fps, cheap physics: the whole 83 ms", simNetSteps(h, 1.0 / 12, 0.01) == 167, std::to_string(simNetSteps(h, 1.0 / 12, 0.01)));
    ctx.check("multiplayer, 0.25 ms a step: 200 steps (50 ms of physics)", simNetSteps(h, 1.0 / 8, 0.25) == 200);
    ctx.check("multiplayer, physics too dear: still 100", simNetSteps(h, 1.0 / 12, 2.0) == 100 && simNetSteps(h, NAN, -1) == 100);
    ctx.check("multiplayer, not measured yet: what's owed, to 0.35 s", simNetSteps(h, 5.0, -1) == 700);
}
