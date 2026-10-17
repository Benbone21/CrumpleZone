#pragma once
#include "render.h"

// Runtime graphics reductions are separate from the user's saved settings. They only ever cut cosmetic costs, in this
// order: ambient occlusion, bloom, clouds (those three fade out in the renderer, they don't pop), grass density (to LOW,
// never off), shadow quality (to LOW, never off), MSAA, then render scale. The scanned props and the ground shading are
// never touched: they're what you drive on and into. Slowness is judged on the window's median frame, so a hitch
// (a load, a compile) doesn't count; after a change it waits a moment before judging again, so one bad patch costs one step.
class AdaptiveQuality {
public:
    void init(const RenderSettings& settings, float targetFps = 60);
    void reset(const RenderSettings& settings);
    bool evaluate(float frameTimeMs, RenderSettings& settings);
    static constexpr float kSlowHoldMs = 750;     // this long slow (median over target): one step down
    static constexpr float kFastHoldMs = 1500;    // this long with room to spare: one step up (doubling after a step up had to be undone)
    static constexpr float kSettleMs = 1000;      // after any change: frames ignored while it settles
private:
    bool adjust(RenderSettings& settings, bool improve);
    void clearWindow();
    RenderSettings maximum_;
    float targetMs_ = 1000.0f / 60, slowMs_ = 0, fastMs_ = 0, settleMs_ = 0, sinceImproveMs_ = 1e9f, fastNeedMs_ = kFastHoldMs;
    float history_[60] = {};
    int count_ = 0, index_ = 0;
};

// Graphics quality presets: LOW, MEDIUM, HIGH, ULTRA, and POTATO (below LOW, for the weakest laptops: no shadows or
// effects, half resolution sharpened, 300 m of view in fog). presetOf() returns Q_CUSTOM when the options don't match one.
enum QualityPreset { Q_LOW, Q_MEDIUM, Q_HIGH, Q_ULTRA, Q_POTATO, Q_CUSTOM };
void applyPreset(RenderSettings& g, int preset);
int presetOf(const RenderSettings& g);
const char* presetName(int preset);  // "LOW" ... "POTATO", "CUSTOM"
// The FPS TARGET choices (Settings::fpsTarget): what adaptive quality aims for, and (VSync off) the frame cap; 0 uncapped.
extern const int kFpsTargets[4];
