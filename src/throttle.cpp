#include "throttle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace throttle {

const float kPreset[kPresets] = {1.0f, 0.5f, 0.25f, 0.1f};
const char* const kResponseNames[kResponses] = {"INSTANT", "SMOOTH", "GENTLE"};

double riseTime(int response) { return response <= 0 ? 0.0 : response == 1 ? 0.4 : 1.2; }

void Tuning::sanitize() {
    if (!std::isfinite(cap)) cap = 1;
    cap = std::min(std::max(cap, kMinCap), 1.0f);
    response = std::min(std::max(response, 0), kResponses - 1);
}

void Tuning::cyclePreset() {
    // The first preset below the cap (so from 40% the key goes to 25%, from 10% back to 100%).
    float next = kPreset[0];
    for (int k = 0; k < kPresets; k++)
        if (kPreset[k] < cap - 0.005f) {
            next = kPreset[k];
            break;
        }
    cap = next;
}

bool Tuning::loadSetting(const std::string& name, double v) {
    if (name == "throttlecap") cap = (float)(v / 100.0);
    else if (name == "throttleresp") response = (int)v;
    else if (name == "throttlepad") padToo = v != 0;
    else return false;
    sanitize();
    return true;
}

std::string Tuning::saveText() const {
    char b[96];
    std::snprintf(b, sizeof b, "throttlecap %.0f\nthrottleresp %d\nthrottlepad %d\n", cap * 100.0, response, padToo ? 1 : 0);
    return b;
}

double Pedal::step(bool held, const Tuning& t, double dt) {
    const double rise = riseTime(t.response);
    if (held) fill = rise <= 0 ? 1.0 : std::min(1.0, fill + dt / rise);
    else fill = rise <= 0 ? 0.0 : std::max(0.0, fill - dt / kFallTime);
    if (t.isDefault()) return held ? 1.0 : 0.0;  // (the key's own demand, untouched)
    return fill * t.cap;
}

std::string percent(float cap) { return std::to_string((int)std::lround(cap * 100)) + "%"; }

}  // namespace throttle
