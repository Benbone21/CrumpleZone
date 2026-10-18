#include "perf.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void AdaptiveQuality::init(const RenderSettings& settings, float targetFps) {
    targetMs_ = 1000.0f / (std::isfinite(targetFps) && targetFps > 0 ? std::min(targetFps, 144.0f) : 60);
    reset(settings);
}

void AdaptiveQuality::clearWindow() {
    slowMs_ = fastMs_ = 0;
    count_ = index_ = 0;
    std::fill(std::begin(history_), std::end(history_), 0);
}

void AdaptiveQuality::reset(const RenderSettings& settings) {
    maximum_ = settings;
    clearWindow();
    settleMs_ = 0, sinceImproveMs_ = 1e9f, fastNeedMs_ = kFastHoldMs;
}

bool AdaptiveQuality::evaluate(float frameTimeMs, RenderSettings& settings) {
    // Ignore discontinuities (startup, map loads, suspended windows), not normal slow frames.
    if (!std::isfinite(frameTimeMs) || frameTimeMs <= 0 || frameTimeMs > 250) return false;
    sinceImproveMs_ += frameTimeMs;
    if (settleMs_ > 0) {  // (just changed: new targets, new shaders warming up)
        settleMs_ -= frameTimeMs;
        return false;
    }
    history_[index_] = frameTimeMs;
    index_ = (index_ + 1) % 60;
    count_ = std::min(count_ + 1, 60);
    if (count_ < 30) return false;
    // The median frame of the window: a few long frames don't move it.
    float sorted[60];
    std::copy(history_, history_ + count_, sorted);
    std::nth_element(sorted, sorted + count_ / 2, sorted + count_);
    const float median = sorted[count_ / 2];
    // Independent elapsed-time counters prevent alternating fast/slow frames from
    // sharing a hysteresis counter. Recovery also works under a 60 Hz VSync cap.
    slowMs_ = median > targetMs_ * 1.15f ? slowMs_ + frameTimeMs : 0;
    fastMs_ = median < targetMs_ * 1.05f ? fastMs_ + frameTimeMs : 0;
    if (slowMs_ < kSlowHoldMs && fastMs_ < fastNeedMs_) return false;
    const bool improve = slowMs_ < kSlowHoldMs;
    if (!improve && sinceImproveMs_ < 4000) fastNeedMs_ = std::min(fastNeedMs_ * 2, 30000.0f);  // (that step up was too much: wait longer to try again)
    const bool changed = adjust(settings, improve);
    if (changed && improve) sinceImproveMs_ = 0;
    clearWindow();
    if (changed) settleMs_ = kSettleMs;
    return changed;
}

bool AdaptiveQuality::adjust(RenderSettings& s, bool improve) {
    const int grassFloor = std::min(maximum_.grass, 1), shadowFloor = std::min(maximum_.shadows, 1);
    if (improve) {  // (the way back up: the reverse order)
        if (s.renderScale < maximum_.renderScale) s.renderScale = std::min(maximum_.renderScale, s.renderScale + 0.25f);
        else if (s.msaa < maximum_.msaa) s.msaa = std::min(maximum_.msaa, s.msaa == 0 ? 2 : s.msaa < 4 ? 4 : 8);
        else if (s.shadows < maximum_.shadows) ++s.shadows;
        else if (s.grass < maximum_.grass) ++s.grass;
        else if (!s.clouds && maximum_.clouds) s.clouds = true;
        else if (!s.bloom && maximum_.bloom) s.bloom = true;
        else if (!s.ao && maximum_.ao) s.ao = true;
        else return false;
    } else {
        if (s.ao) s.ao = false;
        else if (s.bloom) s.bloom = false;
        else if (s.clouds) s.clouds = false;
        else if (s.grass > grassFloor) --s.grass;
        else if (s.shadows > shadowFloor) --s.shadows;
        else if (s.msaa > 0) s.msaa = s.msaa > 4 ? 4 : s.msaa > 2 ? 2 : 0;
        else if (s.renderScale > 0.5f) s.renderScale = std::max(0.5f, s.renderScale - 0.25f);
        else return false;
    }
    // (Props and ground are never adjusted: s.props and s.ground stay as chosen.)
    s.props = maximum_.props, s.ground = maximum_.ground;
    return true;
}

// ---- Presets ---------------------------------------------------------------------

namespace {
struct Preset {
    int shadows, msaa;
    bool ao, bloom, clouds;
    int grass;  // RenderSettings::grass
    int scenery;  // photo-scanned ground and props (RenderSettings::ground and ::props)
    float scale, drawDistance;
    bool sharpen;
};
const Preset kPresets[5] = {
    {1, 0, false, false, false, 1, 1, 1.0f, 0, false},  // LOW
    {2, 2, false, true, true, 2, 2, 1.0f, 0, false},    // MEDIUM
    {3, 4, true, true, true, 2, 2, 1.0f, 0, false},     // HIGH
    {3, 8, true, true, true, 3, 3, 1.0f, 0, false},     // ULTRA
    {0, 0, false, false, false, 0, 1, 0.5f, 300, true},  // POTATO (props stay on: the scanned rocks are what you hit)
};
const int kMsaa[4] = {0, 2, 4, 8};
int msaaIndex(int samples) {
    int best = 0;
    for (int k = 0; k < 4; k++)
        if (kMsaa[k] <= samples) best = k;
    return best;
}
}  // namespace

const int kFpsTargets[7] = {kFpsMatchDisplay, 60, 120, 144, 240, 360, 0};

int fpsTargetFromSaved(float v) {
    if (!std::isfinite(v)) return kFpsMatchDisplay;
    if (v < 0) return kFpsMatchDisplay;
    if (v == 0) return 0;
    int best = 60;
    for (int k = 1; k < 6; k++)
        if (kFpsTargets[k] <= v) best = kFpsTargets[k];
    return best;
}

int resolveFpsTarget(int setting, int displayHz) {
    if (setting == kFpsMatchDisplay) return displayHz >= 30 && displayHz <= 1000 ? displayHz : 0;
    return setting > 0 ? setting : 0;
}

float adaptiveAim(int resolvedTarget) { return resolvedTarget > 0 ? (float)std::min(resolvedTarget, 144) : 60.0f; }

const char* presetName(int preset) {
    static const char* kNames[6] = {"LOW", "MEDIUM", "HIGH", "ULTRA", "POTATO", "CUSTOM"};
    return kNames[std::min(std::max(preset, 0), 5)];
}

void applyPreset(RenderSettings& g, int preset) {
    const bool wasPotato = presetOf(g) == Q_POTATO;
    const Preset& p = kPresets[std::min(std::max(preset, 0), 4)];
    // (The render scale is the player's own, except that POTATO sets half, and leaving it puts it back to full.)
    if (preset == Q_POTATO || wasPotato) g.renderScale = p.scale;
    g.drawDistance = p.drawDistance;
    g.sharpen = p.sharpen;
    g.shadows = p.shadows;
    g.msaa = p.msaa;
    g.ao = p.ao;
    g.bloom = p.bloom;
    g.clouds = p.clouds;
    g.grass = p.grass;
    g.ground = g.props = p.scenery;
}

int presetOf(const RenderSettings& g) {
    for (int k = 0; k < 5; k++) {
        const Preset& p = kPresets[k];
        if (g.shadows == p.shadows && kMsaa[msaaIndex(g.msaa)] == p.msaa && g.ao == p.ao && g.bloom == p.bloom &&
            g.clouds == p.clouds && g.grass == p.grass && g.ground == p.scenery && g.props == p.scenery &&
            g.drawDistance == p.drawDistance && g.sharpen == p.sharpen)
            return k;
    }
    return Q_CUSTOM;
}


// ---- First-launch detection ------------------------------------------------------------------------------------

namespace {
// Lowercase substrings of "vendor renderer", first match wins (so the specific entries come before the general ones).
// With two patterns, both must be present.
struct RendererRule {
    const char* a;
    const char* b;
    int preset;
};
const RendererRule kRenderers[] = {
    // software and virtual renderers
    {"llvmpipe", nullptr, Q_POTATO}, {"softpipe", nullptr, Q_POTATO}, {"swiftshader", nullptr, Q_POTATO},
    {"microsoft basic render", nullptr, Q_POTATO}, {"basic render driver", nullptr, Q_POTATO}, {"gdi generic", nullptr, Q_POTATO},
    {"software rasterizer", nullptr, Q_POTATO}, {"swrast", nullptr, Q_POTATO}, {"videocore", nullptr, Q_POTATO}, {"v3d", nullptr, Q_POTATO},
    // phones and tablets
    {"adreno", nullptr, Q_LOW}, {"mali", nullptr, Q_LOW}, {"powervr", nullptr, Q_LOW},
    // Apple silicon: Pro/Max/Ultra are strong; the base chips (M1 8 GB) manage LOW at Retina sizes
    {"apple", "ultra", Q_HIGH}, {"apple", "max", Q_HIGH}, {"apple", "pro", Q_HIGH}, {"apple", nullptr, Q_LOW},
    // Intel: Arc cards are discrete, the rest is on the CPU
    {"arc(tm) a", nullptr, Q_HIGH}, {"arc a", nullptr, Q_HIGH}, {"arc(tm) b", nullptr, Q_HIGH}, {"arc b", nullptr, Q_HIGH}, {"arc pro", nullptr, Q_HIGH},
    {"intel", "arc", Q_MEDIUM}, {"intel", "iris xe", Q_MEDIUM}, {"intel", "iris(r) xe", Q_MEDIUM}, {"intel", nullptr, Q_LOW},
    // NVIDIA
    {"geforce mx", nullptr, Q_LOW}, {"geforce", nullptr, Q_HIGH}, {"quadro", nullptr, Q_HIGH}, {"rtx", nullptr, Q_HIGH},
    {"titan", nullptr, Q_HIGH}, {"nvidia", nullptr, Q_HIGH},
    // AMD: cards first, then the APUs' integrated graphics
    {"radeon rx", nullptr, Q_HIGH}, {"radeon pro", nullptr, Q_HIGH}, {"radeon vii", nullptr, Q_HIGH}, {"rx vega", nullptr, Q_HIGH},
    {"780m", nullptr, Q_MEDIUM}, {"760m", nullptr, Q_MEDIUM}, {"680m", nullptr, Q_MEDIUM}, {"660m", nullptr, Q_MEDIUM},
    {"radeon hd", nullptr, Q_LOW}, {"vega", nullptr, Q_LOW}, {"radeon(tm) graphics", nullptr, Q_LOW}, {"radeon graphics", nullptr, Q_LOW},
    {"radeon", nullptr, Q_MEDIUM},
};

std::string lowered(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}
const int kLadder[5] = {Q_POTATO, Q_LOW, Q_MEDIUM, Q_HIGH, Q_ULTRA};
int ladderOf(int preset) {
    for (int k = 0; k < 5; k++)
        if (kLadder[k] == preset) return k;
    return 2;
}
}  // namespace

int presetForRenderer(const std::string& vendor, const std::string& renderer) {
    const std::string text = lowered(vendor + " " + renderer);
    for (const RendererRule& r : kRenderers)
        if (text.find(r.a) != std::string::npos && (!r.b || text.find(r.b) != std::string::npos)) return r.preset;
    return Q_MEDIUM;
}

int guessPreset(const HardwareInfo& hw) {
    int p = presetForRenderer(hw.vendor, hw.renderer);
    if (p == Q_POTATO) return p;
    auto cap = [&](int limit) { if (ladderOf(p) > ladderOf(limit)) p = limit; };
    if (hw.vramMb >= 0 && hw.vramMb < 2048) cap(Q_LOW);
    else if (hw.vramMb >= 0 && hw.vramMb < 4096) cap(Q_MEDIUM);
    if (hw.cores > 0 && hw.cores <= 2) cap(Q_LOW);
    else if (hw.cores > 0 && hw.cores < 4) cap(Q_MEDIUM);
    if (hw.ramMb > 0 && hw.ramMb < 4096) cap(Q_LOW);
    else if (hw.ramMb > 0 && hw.ramMb < 8192) cap(Q_MEDIUM);
    return p;
}

void queryHardware(HardwareInfo& hw) {
    hw.cores = (int)std::thread::hardware_concurrency();
#ifdef _WIN32
    MEMORYSTATUSEX m;
    m.dwLength = sizeof m;
    hw.ramMb = GlobalMemoryStatusEx(&m) ? (long)(m.ullTotalPhys >> 20) : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    hw.ramMb = pages > 0 && size > 0 ? (long)((double)pages * size / (1 << 20)) : 0;
#endif
}

void QualityProbe::start(int preset, float targetFps) {
    level_ = std::min(ladderOf(preset), (int)ladderOf(Q_HIGH));
    targetMs_ = 1000.0f / (std::isfinite(targetFps) && targetFps > 0 ? targetFps : 60);
    active_ = true, wentDown_ = wentUp_ = false;
    rounds_ = count_ = 0, elapsedMs_ = 0, settleMs_ = kSettleMs;
}

int QualityProbe::preset() const { return kLadder[level_]; }

QualityProbe::Step QualityProbe::feed(float ms) {
    if (!active_ || !std::isfinite(ms) || ms <= 0 || ms > 250) return NONE;  // (a load or a hitch isn't the preset's cost)
    if (settleMs_ > 0) {
        settleMs_ -= ms;
        return NONE;
    }
    if (count_ < 512) samples_[count_++] = ms;
    elapsedMs_ += ms;
    if (elapsedMs_ < kMeasureMs || count_ < 15) return NONE;
    std::nth_element(samples_, samples_ + count_ / 2, samples_ + count_);
    const float median = samples_[count_ / 2];
    ++rounds_, count_ = 0, elapsedMs_ = 0, settleMs_ = kSettleMs;
    const bool slow = median > targetMs_ * 1.15f, roomy = median < targetMs_ * 0.7f;
    if (slow && level_ > 0) {
        --level_;
        if (wentUp_ || rounds_ >= kMaxRounds) return active_ = false, DONE;  // (the step up was too much: back, and stop)
        wentDown_ = true;
        return CHANGED;
    }
    if (!slow && roomy && !wentDown_ && level_ < ladderOf(Q_HIGH) && rounds_ < kMaxRounds) {
        ++level_, wentUp_ = true;
        return CHANGED;
    }
    return active_ = false, DONE;
}
