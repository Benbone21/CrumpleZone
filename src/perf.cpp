#include "perf.h"
#include <algorithm>
#include <cmath>

void AdaptiveQuality::init(const RenderSettings& settings, float targetFps) {
    targetMs_ = 1000.0f / (std::isfinite(targetFps) && targetFps > 0 ? targetFps : 60);
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

const int kFpsTargets[4] = {60, 120, 144, 0};

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

