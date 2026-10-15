#pragma once
#include "render.h"

// Runtime graphics reductions are separate from the user's saved settings.
class AdaptiveQuality {
public:
    void init(const RenderSettings& settings, float targetFps = 60);
    void reset(const RenderSettings& settings);
    bool evaluate(float frameTimeMs, RenderSettings& settings);
private:
    bool adjust(RenderSettings& settings, bool improve);
    RenderSettings maximum_;
    float targetMs_ = 1000.0f / 60, sum_ = 0, slowMs_ = 0, fastMs_ = 0;
    float history_[60] = {};
    int count_ = 0, index_ = 0;
};
