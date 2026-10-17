// The player's throttle limit, for rock crawling: a cap on what the keyboard's W gives (and
// optionally the pad's trigger), and how fast the pedal gets there. Local input shaping only:
// no SDL or GL in it (the test runner links throttle.cpp), and nothing here touches the AI, the
// autopilot, scenario playback or other players' cars. At the defaults (100%, INSTANT) it
// returns the key's own 0 and 1, so nothing moves.
#pragma once
#include <string>

namespace throttle {

constexpr float kMinCap = 0.05f, kStep = 0.05f;     // the menu's slider: 5% .. 100% in 5% steps
constexpr int kPresets = 4;
extern const float kPreset[kPresets];               // the in-drive key cycles these: 100 50 25 10 %
constexpr int kResponses = 3;
extern const char* const kResponseNames[kResponses];  // INSTANT, SMOOTH, GENTLE
// Seconds for the pedal to go from 0 to the cap, and from the cap to 0 (letting go is quick).
double riseTime(int response);
constexpr double kFallTime = 0.12;

struct Tuning {
    float cap = 1;          // the most throttle the limited input gives, 0.05..1
    int response = 0;       // 0 INSTANT, 1 SMOOTH, 2 GENTLE: how the keyboard pedal builds
    bool padToo = false;    // the cap applies to the pad's trigger as well (otherwise keyboard only)

    bool isDefault() const { return cap >= 1 && response == 0 && !padToo; }
    float padScale() const { return padToo ? cap : 1.0f; }
    // The next preset down (100 -> 50 -> 25 -> 10 -> 100) from wherever the cap is now.
    void cyclePreset();
    // settings.txt: "throttlecap" (percent), "throttleresp", "throttlepad". True if `name` was ours.
    bool loadSetting(const std::string& name, double value);
    std::string saveText() const;  // those lines
    void sanitize();
};

// The keyboard pedal: `held` is whether W (the car's own full-throttle demand) is down.
// Returns the throttle to put on the car, 0..cap.
struct Pedal {
    double fill = 0;  // 0..1: how far along the ramp
    double step(bool held, const Tuning& t, double dt);
    void reset() { fill = 0; }
};

std::string percent(float cap);  // "25%"

}  // namespace throttle
