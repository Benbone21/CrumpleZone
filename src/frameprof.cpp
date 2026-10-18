#include "frameprof.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

const char* framePassName(FramePass p) {
    static const char* kNames[FP_COUNT] = {"GAME", "SIM", "SHADOWS", "SKY", "TERRAIN", "OPAQUE", "VEGETATION", "PROPS", "FX", "POST", "UI", "SWAP"};
    return p < FP_COUNT ? kNames[p] : "?";
}

void FrameProfile::beginFrame() {
    const int64_t t = nowNs();
    if (open_) {
        cur_[pass_] += (t - since_) * 1e-6;
        const double k = frames_ < 2 ? 1.0 : smoothing;
        for (int i = 0; i < FP_COUNT; i++) {
            last[i] = cur_[i];
            cpu[i] += (cur_[i] - cpu[i]) * k;
        }
        frames_++;
    }
    std::fill(cur_, cur_ + FP_COUNT, 0.0);
    pass_ = FP_GAME;
    since_ = t;
    open_ = true;
}

void FrameProfile::enter(FramePass p) {
    if (p >= FP_COUNT) return;
    const int64_t t = nowNs();
    if (open_) cur_[pass_] += (t - since_) * 1e-6;
    pass_ = p;
    since_ = t;
}

void FrameProfile::setGpu(const double* ms) {
    const double k = gpuValid ? smoothing : 1.0;
    for (int i = 0; i < FP_COUNT; i++) {
        lastGpu[i] = ms[i];
        if (ms[i] >= 0) gpu[i] += (ms[i] - gpu[i]) * k;
    }
    gpuValid = true;
}

FrameProfile& frameProfile() {
    static FrameProfile p;
    return p;
}

void BenchStats::add(double frameMs, const double* cpuMs, const double* gpuMs) {
    if (!std::isfinite(frameMs) || frameMs < 0) return;
    frames_.push_back((float)frameMs);
    for (int i = 0; i < FP_COUNT; i++) {
        if (cpuMs) cpuSum_[i] += cpuMs[i];
        if (gpuMs && gpuMs[i] >= 0) gpuSum_[i] += gpuMs[i], gpuN_[i]++;
    }
}

BenchStats::Result BenchStats::result() const {
    Result r;
    r.frames = (int)frames_.size();
    for (int i = 0; i < FP_COUNT; i++) r.gpu[i] = -1;
    if (!r.frames) return r;
    double sum = 0;
    for (float f : frames_) sum += f, r.worstMs = std::max(r.worstMs, (double)f);
    r.avgMs = sum / r.frames;
    std::vector<float> sorted(frames_);
    const int k = std::min(r.frames - 1, (int)std::floor(r.frames * 0.99));
    std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
    r.lowMs = sorted[k];
    std::nth_element(sorted.begin(), sorted.begin() + r.frames / 2, sorted.end());
    const double median = sorted[r.frames / 2];
    for (float f : frames_) {
        r.over30 += f > 30;
        r.hitches += f > 2 * median && f > median + 8;
    }
    r.avgFps = r.avgMs > 0 ? 1000.0 / r.avgMs : 0;
    r.lowFps = r.lowMs > 0 ? 1000.0 / r.lowMs : 0;
    for (int i = 0; i < FP_COUNT; i++) {
        r.cpu[i] = cpuSum_[i] / r.frames;
        if (gpuN_[i]) r.gpu[i] = gpuSum_[i] / gpuN_[i];
    }
    return r;
}

std::string BenchStats::csvLine(const Result& r) {
    char b[128];
    std::snprintf(b, sizeof b, "avg=%.2f,low1=%.2f,hitches=%d", r.avgMs, r.lowMs, r.hitches);
    std::string csv = b;
    for (int i = 0; i < FP_COUNT; i++) {
        std::snprintf(b, sizeof b, ",%s=%.2f", framePassName((FramePass)i), r.cpu[i]);
        csv += b;
        if (framePassDraws((FramePass)i) && r.gpu[i] >= 0) {
            std::snprintf(b, sizeof b, "/%.2f", r.gpu[i]);
            csv += b;
        }
    }
    return csv;
}

std::string BenchStats::report(const Result& r, const std::string& title) {
    std::string out;
    char b[256];
    std::snprintf(b, sizeof b, "bench: %s\n", title.c_str());
    out += b;
    std::snprintf(b, sizeof b, "bench: %d frames: avg %.2f ms (%.1f fps), 1%% low %.2f ms (%.1f fps), worst %.2f ms\n", r.frames, r.avgMs, r.avgFps,
                  r.lowMs, r.lowFps, r.worstMs);
    out += b;
    std::snprintf(b, sizeof b, "bench: %d frames over 30 ms, %d hitches (over twice the median)\n", r.over30, r.hitches);
    out += b;
    out += "bench:   stage        cpu ms   gpu ms\n";
    double cpuSum = 0, gpuSum = 0;
    for (int i = 0; i < FP_COUNT; i++) {
        const FramePass p = (FramePass)i;
        cpuSum += r.cpu[i];
        if (framePassDraws(p) && r.gpu[i] >= 0) {
            gpuSum += r.gpu[i];
            std::snprintf(b, sizeof b, "bench:   %-10s %8.2f %8.2f\n", framePassName(p), r.cpu[i], r.gpu[i]);
        } else std::snprintf(b, sizeof b, "bench:   %-10s %8.2f        -\n", framePassName(p), r.cpu[i]);
        out += b;
    }
    std::snprintf(b, sizeof b, "bench:   %-10s %8.2f %8.2f\n", "TOTAL", cpuSum, gpuSum);
    out += b;
    out += "bench-csv: " + csvLine(r) + "\n";
    return out;
}

BenchPose benchPose(double cx, double cz, double t, double radius, double h, double (*heightAt)(const void*, double, double), const void* ctx) {
    const double a = 2 * M_PI * (t - std::floor(t));
    BenchPose p;
    p.x = cx + radius * std::cos(a);
    p.z = cz + radius * std::sin(a);
    // Along the lap (anticlockwise seen from above: x, z grow as cos, sin), turned 25 degrees in towards the centre.
    const double heading = a + M_PI / 2 + 25 * M_PI / 180;
    const double dx = std::cos(heading), dz = std::sin(heading);
    double ground = heightAt ? heightAt(ctx, p.x, p.z) : 0;
    if (heightAt)  // (over a crest just ahead: keep clear of it)
        for (double s = 2; s <= 8; s += 2) ground = std::max(ground, heightAt(ctx, p.x + dx * s, p.z + dz * s) - 0.15 * s);
    p.y = ground + h;
    p.yaw = std::atan2(-dz, dx);
    p.pitch = -0.08;
    return p;
}

bool frameSpike(double frameMs, double typicalMs) { return frameMs > 30 && frameMs > 2 * typicalMs; }

void describeSpike(char* line, int n, int frame, double frameMs, double typicalMs, const double* stageMs) {
    int a = 0, b = 1;
    for (int i = 0; i < FP_COUNT; i++)
        if (stageMs[i] > stageMs[a]) a = i;
    if (b == a) b = 0;
    for (int i = 0; i < FP_COUNT; i++)
        if (i != a && stageMs[i] > stageMs[b]) b = i;
    std::snprintf(line, n, "spike: frame %d took %.1f ms (typical %.1f): %s %.1f ms, %s %.1f ms", frame, frameMs, typicalMs, framePassName((FramePass)a),
                  stageMs[a], framePassName((FramePass)b), stageMs[b]);
}

double simCatchUp(double dt, double& usualDt) {
    if (!(dt > 0)) return 0;
    const double run = dt > 0.05 && dt > 2.5 * usualDt ? usualDt : dt;
    usualDt += (std::min(dt, 3 * usualDt) - usualDt) * 0.05;
    return run;
}
