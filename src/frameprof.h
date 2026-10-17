// Where a frame's time goes: CPU ms per stage (sim, shadows, terrain, ... swap), plus the GPU's ms per stage when the
// renderer has timer queries. No SDL or GL here (the test runner links it): the game and the renderer call enter() at
// each stage's start, the renderer hands back GPU times (setGpu), and F3 and --bench read the smoothed numbers.
// Benchmark mode (--bench N) keeps every frame's numbers in a BenchStats and prints average, 1% low and per-stage ms.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum FramePass : uint8_t {
    FP_GAME,     // input, game logic, camera, effects, building this frame's part models
    FP_SIM,      // Sim::advance (the 2 kHz physics)
    FP_SHADOWS,  // the shadow cascades
    FP_SKY,      // target clear and the sky
    FP_TERRAIN,  // the ground (and the rut walls)
    FP_OPAQUE,   // cars, wheels, parts, boxes, figures
    FP_VEG,      // trees, bushes, grass
    FP_PROPS,    // scanned props (and their culling), code-made rocks and fixtures
    FP_FX,       // skids, particles, water, glass, x-ray
    FP_POST,     // MSAA resolve, AO, bloom, composite
    FP_UI,       // HUD, menus, F3
    FP_SWAP,     // buffer swap (and, benchmarking, waiting for the GPU)
    FP_COUNT
};

const char* framePassName(FramePass p);  // "SIM", "SHADOWS"...
// Stages the GPU works in (the rest are CPU only: GAME, SIM).
inline bool framePassDraws(FramePass p) { return p >= FP_SHADOWS && p <= FP_UI; }

class FrameProfile {
public:
    // Starts a frame (closes the last one: its stage times become `last` and go into the smoothed ones).
    void beginFrame();
    // The current stage ends and `p` starts (a stage may be entered more than once a frame: the times add up).
    void enter(FramePass p);
    // The GPU's time in each stage for a past frame (ms; negative: not measured).
    void setGpu(const double* ms);
    // For tests: a stage's time this frame, as if measured.
    void addCpu(FramePass p, double ms) { cur_[p] += ms; }

    double cpu[FP_COUNT] = {};   // smoothed CPU ms
    double gpu[FP_COUNT] = {};   // smoothed GPU ms
    double last[FP_COUNT] = {};  // the last whole frame's CPU ms
    double lastGpu[FP_COUNT] = {};
    bool gpuValid = false;       // timer queries have reported
    double smoothing = 0.05;     // per frame
    FramePass current() const { return pass_; }
    int frames() const { return frames_; }

private:
    double cur_[FP_COUNT] = {};
    FramePass pass_ = FP_GAME;
    int64_t since_ = 0;  // ns, steady clock
    int frames_ = 0;
    bool open_ = false;
};

// Every frame of a benchmark: the frame time and the stage times.
class BenchStats {
public:
    void add(double frameMs, const double* cpuMs, const double* gpuMs);
    int count() const { return (int)frames_.size(); }
    struct Result {
        int frames = 0;
        double avgMs = 0, lowMs = 0, worstMs = 0;  // average; 1% low: the 99th percentile frame time (as F3's)
        double avgFps = 0, lowFps = 0;
        int over30 = 0;   // frames longer than 30 ms
        int hitches = 0;  // frames over twice the median (and 8 ms more than it): the spikes you feel
        double cpu[FP_COUNT] = {}, gpu[FP_COUNT] = {};  // per stage, averaged (gpu: over the frames that had it; -1 none)
    };
    Result result() const;
    // The lines --bench prints (each starts "bench: "), and one machine-readable line ("bench-csv: ...").
    static std::string report(const Result& r, const std::string& title);

private:
    std::vector<float> frames_;
    double cpuSum_[FP_COUNT] = {}, gpuSum_[FP_COUNT] = {};
    int gpuN_[FP_COUNT] = {};
};

// A spike: a frame well over the usual (twice the typical frame and over 30 ms). `line` gets what to log: the frame, and
// the stages that ran longest (CPU, from `stageMs`).
bool frameSpike(double frameMs, double typicalMs);
void describeSpike(char* line, int n, int frame, double frameMs, double typicalMs, const double* stageMs);

// The benchmark's camera: a drone flying a lap round `centre`, low over the ground (`h` m up), looking ahead and in.
// `t` 0..1 is the share of the lap; heightAt gives the ground. Pure, so the path is the same every run.
struct BenchPose {
    double x, y, z, yaw, pitch;  // the drone camera's (yaw 0 looks +X, 90 looks -Z; radians)
};
BenchPose benchPose(double cx, double cz, double t, double radius, double h, double (*heightAt)(const void*, double, double), const void* ctx);

// The process-wide profile (the game and the renderer share it).
FrameProfile& frameProfile();
