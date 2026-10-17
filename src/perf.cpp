#include "perf.h"
#include <algorithm>
#include <cmath>

void AdaptiveQuality::init(const RenderSettings& settings, float targetFps) {
    targetMs_ = 1000.0f / (std::isfinite(targetFps) && targetFps > 0 ? targetFps : 60);
    reset(settings);
}

void AdaptiveQuality::reset(const RenderSettings& settings) {
    maximum_ = settings;
    sum_ = slowMs_ = fastMs_ = 0;
    count_ = index_ = 0;
    std::fill(std::begin(history_), std::end(history_), 0);
}

bool AdaptiveQuality::evaluate(float frameTimeMs, RenderSettings& settings) {
    // Ignore discontinuities (startup, map loads, suspended windows), not normal slow frames.
    if (!std::isfinite(frameTimeMs) || frameTimeMs <= 0 || frameTimeMs > 250) return false;
    sum_ += frameTimeMs - history_[index_];
    history_[index_] = frameTimeMs;
    index_ = (index_ + 1) % 60;
    count_ = std::min(count_ + 1, 60);
    if (count_ < 30) return false;
    const float average = sum_ / count_;
    // Independent elapsed-time counters prevent alternating fast/slow frames from
    // sharing a hysteresis counter. Recovery also works under a 60 Hz VSync cap.
    slowMs_ = average > targetMs_ * 1.15f ? slowMs_ + frameTimeMs : 0;
    fastMs_ = average < targetMs_ * 1.05f ? fastMs_ + frameTimeMs : 0;
    if (slowMs_ < 750 && fastMs_ < 4000) return false;
    const bool changed = adjust(settings, fastMs_ >= 4000);
    sum_ = slowMs_ = fastMs_ = 0;
    count_ = index_ = 0;
    std::fill(std::begin(history_), std::end(history_), 0);
    return changed;
}

bool AdaptiveQuality::adjust(RenderSettings& s, bool improve) {
    if (improve) {
        if (s.shadows < maximum_.shadows) ++s.shadows;
        else if (s.msaa < maximum_.msaa) s.msaa = std::min(maximum_.msaa, s.msaa == 0 ? 2 : s.msaa < 4 ? 4 : 8);
        else if (!s.bloom && maximum_.bloom) s.bloom = true;
        else if (!s.ao && maximum_.ao) s.ao = true;
        else if (s.grass < maximum_.grass) ++s.grass;
        else if (!s.clouds && maximum_.clouds) s.clouds = true;
        else if (s.ground < maximum_.ground) ++s.ground;
        else if (s.props < maximum_.props) ++s.props;
        else if (s.renderScale < maximum_.renderScale) s.renderScale = std::min(maximum_.renderScale, s.renderScale + 0.25f);
        else return false;
    } else {
        if (s.renderScale > 0.5f) s.renderScale = std::max(0.5f, s.renderScale - 0.25f);
        else if (s.props > 0) --s.props;
        else if (s.ground > 0) --s.ground;
        else if (s.clouds) s.clouds = false;
        else if (s.grass > 0) --s.grass;
        else if (s.ao) s.ao = false;
        else if (s.bloom) s.bloom = false;
        else if (s.msaa > 0) s.msaa = s.msaa > 4 ? 4 : s.msaa > 2 ? 2 : 0;
        else if (s.shadows > 0) --s.shadows;
        else return false;
    }
    return true;
}
