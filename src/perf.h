#pragma once
#include "render.h"
#include <string>

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
// kFpsMatchDisplay (the default) follows the display's refresh rate, uncapped when that's unknown.
constexpr int kFpsMatchDisplay = -1;
extern const int kFpsTargets[7];  // MATCH DISPLAY, 60, 120, 144, 240, 360, uncapped
int fpsTargetFromSaved(float v);  // a saved value to a choice: <0 match display, 0 uncapped, else the highest choice not above it (60 at least)
int resolveFpsTarget(int setting, int displayHz);  // the fps to cap at: 0 uncapped
// What adaptive quality and the detection probe aim for: the target, but never above 144, so a 240 or 360 target on a machine
// that can't reach it doesn't cut quality for ever (it still cuts below 144). Uncapped aims for 60.
float adaptiveAim(int resolvedTarget);

// ---- First-launch detection ------------------------------------------------------------------------------
// A guess from the hardware (a table over GL_VENDOR + GL_RENDERER, then VRAM, cores and RAM can only lower it), confirmed by a
// short measurement in the running game that steps the preset toward the FPS target. ULTRA is never picked automatically.
struct HardwareInfo {
    std::string vendor, renderer;  // GL_VENDOR, GL_RENDERER
    long vramMb = -1;              // dedicated video memory where the driver says (-1: unknown)
    int cores = 0;                 // logical CPU cores (0: unknown)
    long ramMb = 0;                // system RAM (0: unknown)
};
int presetForRenderer(const std::string& vendor, const std::string& renderer);  // the table alone; Q_MEDIUM for a string it doesn't know
int guessPreset(const HardwareInfo& hw);
void queryHardware(HardwareInfo& hw);  // cores and RAM (the caller fills in the GL strings and VRAM)
// First launch: no settings file, or one that has no quality chosen yet. A saved choice is never overridden.
inline bool shouldAutoDetect(bool settingsFileLoaded, bool qualityChosen) { return !settingsFileLoaded || !qualityChosen; }

// Measures median frame time on a preset, a few seconds at a time, stepping down while it's slow and up while it has lots of room
// (HIGH at most), until it settles. feed() each frame once the world is drawing; apply preset() to the settings after CHANGED or DONE.
class QualityProbe {
public:
    enum Step { NONE, CHANGED, DONE };
    void start(int preset, float targetFps = 60);
    void cancel() { active_ = false; }
    bool active() const { return active_; }
    int preset() const;
    Step feed(float frameTimeMs);
    static constexpr float kSettleMs = 1200, kMeasureMs = 3000;
    static constexpr int kMaxRounds = 4;
private:
    bool active_ = false, wentDown_ = false, wentUp_ = false;
    int level_ = 2, rounds_ = 0, count_ = 0;
    float targetMs_ = 1000.0f / 60, settleMs_ = 0, elapsedMs_ = 0;
    float samples_[512];
};
