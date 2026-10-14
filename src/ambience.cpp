#include "ambience.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double tau = 6.283185307179586;
float safe(float value, float fallback, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
struct Habitat { float wind, leaves, birds, insects; };
constexpr Habitat habitats[] = {
    {0.025f, 0.006f, 0.25f, 0.1f}, // proving ground
    {0.050f, 0.028f, 0.75f, 0.25f}, // mountain
    {0.040f, 0.002f, 0, 0},        // freeway
    {0.055f, 0.016f, 0.45f, 0.6f}, // frontier
    {0.030f, 0.040f, 1, 1},        // forest / backcountry
};
}
float Ambience::random() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ >> 8) * (1.0f / 16777216.0f);
}
void Ambience::render(float* out, int frames, int rate, const AmbienceInput& input) {
    if (!out || frames <= 0 || rate < 8000 || rate > 192000) return;
    const float wanted = input.map >= 0 && input.map < 5 ? safe(input.volume, 0, 0, 1) : 0;
    if (wanted == 0 && volume_ < 1e-6f) { volume_ = 0; return; }
    const Habitat habitat = input.map >= 0 && input.map < 5 ? habitats[input.map] : Habitat{};
    const float hours = safe(input.hours, 10, 0, 24);
    // Smooth sunrise/sunset, with a little more birdsong near dawn and dusk.
    const float daylight = std::clamp(std::min((hours - 5) / 2, (21 - hours) / 2), 0.0f, 1.0f);
    const float chorus = 0.65f + 0.35f * std::max(std::exp(-std::pow((hours - 7) / 2, 2)),
                                               std::exp(-std::pow((hours - 18) / 2, 2)));
    const float exposure = safe(input.exposure, 1, 0, 1);
    const float dt = 1.0f / rate;
    const float gainA = 1 - std::exp(-dt / 0.35f), habitatA = 1 - std::exp(-dt / 2);
    const float lowA = 1 - std::exp(-(float)tau * 230 * dt);
    const float leafA = 1 - std::exp(-(float)tau * 1600 * dt);
    if (!initialized_) {
        for (auto& bird : birds_) bird.wait = 1 + random() * 8;
        initialized_ = true;
    }
    for (int i = 0; i < frames; i++) {
        volume_ += (wanted * exposure - volume_) * gainA;
        wind_ += (habitat.wind - wind_) * habitatA;
        leaves_ += (habitat.leaves - leaves_) * habitatA;
        birdsGain_ += (habitat.birds * daylight * chorus - birdsGain_) * habitatA;
        insects_ += (habitat.insects * (1 - daylight) - insects_) * habitatA;
        if ((gustWait_ -= dt) <= 0) {
            gustTarget_ = 0.2f + 0.8f * random();
            gustWait_ = 3 + 5 * random();
        }
        gust_ += (gustTarget_ - gust_) * dt * 0.5f;
        const float a = random() * 2 - 1, b = random() * 2 - 1;
        lowL_ += (a - lowL_) * lowA; lowR_ += (b - lowR_) * lowA;
        rustleL_ += (a - rustleL_) * leafA; rustleR_ += (b - rustleR_) * leafA;
        const float common = (lowL_ + lowR_) * 0.5f;
        float left = (common * 0.7f + lowL_ * 0.3f) * wind_ * (0.4f + gust_);
        float right = (common * 0.7f + lowR_ * 0.3f) * wind_ * (0.4f + gust_);
        left += (rustleL_ - lowL_) * leaves_ * gust_;
        right += (rustleR_ - lowR_) * leaves_ * gust_;
        for (auto& bird : birds_) {
            if (bird.wait > 0) { bird.wait -= dt; continue; }
            if (bird.duration <= 0) {
                if (bird.calls == 0) {
                    if (birdsGain_ < 0.05f) { bird.wait = 2; continue; }
                    bird.calls = 2 + (int)(random() * 4);
                    const float pan = 0.1f + random() * 0.8f;
                    bird.panL = std::sqrt(1 - pan); bird.panR = std::sqrt(pan);
                    bird.hz = 1700 + random() * 2100;
                    bird.gain = 0.012f + random() * 0.015f;
                }
                bird.age = 0; bird.duration = 0.09f + random() * 0.15f;
                bird.sweep = (random() - 0.45f) * 1500;
            }
            const float u = bird.age / bird.duration;
            const float envelope = std::pow(std::sin((float)tau * 0.5f * u), 2);
            const float hz = std::min(rate * 0.2f, bird.hz + bird.sweep * u + 90 * std::sin((float)tau * 30 * bird.age));
            bird.phase += hz * dt; bird.phase -= std::floor(bird.phase);
            const float call = (float)std::sin(tau * bird.phase) * envelope * bird.gain * birdsGain_;
            left += call * bird.panL; right += call * bird.panR;
            bird.age += dt;
            if (bird.age >= bird.duration) {
                bird.duration = 0;
                bird.wait = --bird.calls > 0 ? 0.08f + random() * 0.2f : 6 + random() * 15;
            }
        }
        cricketPhase_ += std::min(3900.0f, rate * 0.2f) * dt;
        cricketPhase_ -= std::floor(cricketPhase_);
        cricketPulse_ += 13 * dt; cricketPulse_ -= std::floor(cricketPulse_);
        const float chirp = (float)(std::sin(tau * cricketPhase_) *
            std::pow(std::max(0.0, std::sin(tau * cricketPulse_)), 6)) * insects_ * 0.004f * (0.5f + gust_);
        out[2 * i] += (left + chirp * 0.6f) * volume_;
        out[2 * i + 1] += (right + chirp) * volume_;
    }
}
