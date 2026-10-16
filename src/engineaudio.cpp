#include "engineaudio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace {
constexpr int kSize = 2048, kLevels = 4;
constexpr int kHarmonics[kLevels] = {96, 48, 24, 12};
constexpr double kTau = 6.283185307179586;
using Wave = std::array<float, kSize>;

// Built before main, never in the callback. A complete 720-degree combustion
// cycle retains bank differences, unlike repeating one identical click. Each
// event has a compression pulse followed by rarefaction; analytic Fourier
// coefficients roll off smoothly. Mip levels remove harmonics above Nyquist.
struct CycleWaves {
    Wave waves[13][2][kLevels];
    CycleWaves() {
        const double crossBank[8] = {1.25, 0.55, 0.8, 1.1, 0.55, 1.25, 1.1, 0.8};
        for (int layout = 0; layout < 13; layout++) {
            const int cyl = layout == 12 ? 8 : layout + 1;
            for (int load = 0; load < 2; load++) {
                std::complex<double> coefficients[97] = {};
                for (int h = 1; h <= 96; h++) {
                    std::complex<double> events = 0;
                    for (int c = 0; c < cyl; c++) {
                        const double gain = layout == 12 ? crossBank[c] : 1.0 + 0.025 * std::cos(c * 2.1);
                        events += gain * std::polar(1.0, -kTau * h * c / cyl);
                    }
                    const std::complex<double> iw(0, kTau * h / cyl);
                    coefficients[h] = events * (1.0 / (double(load ? 24 : 10) + iw) - 1.0 / (double(load ? 4 : 3) + iw));
                }
                for (int level = 0; level < kLevels; level++) {
                    Wave& wave = waves[layout][load][level];
                    for (int i = 0; i < kSize; i++) {
                        double x = 0;
                        for (int h = 1; h <= kHarmonics[level]; h++)
                            x += std::real(coefficients[h] * std::polar(1.0, kTau * h * i / kSize));
                        wave[i] = (float)(x / cyl);
                    }
                }
                // Same gain across mip levels: no volume jump at a boundary.
                float peak = 0;
                for (float x : waves[layout][load][0]) peak = std::max(peak, std::fabs(x));
                for (auto& wave : waves[layout][load]) for (float& x : wave) x *= 1.0f / std::max(peak, 0.001f);
            }
        }
    }
};
const CycleWaves kWaves;

float read(const Wave& wave, double phase) {
    const float p = (float)(phase * kSize);
    const int i = (int)p;
    const float t = p - i;
    return wave[i & (kSize - 1)] + (wave[(i + 1) & (kSize - 1)] - wave[i & (kSize - 1)]) * t;
}
}

void EngineSynth::Resonator::tune(float frequency, float q, float dt) {
    const float w = (float)kTau * std::min(frequency * dt, 0.20f);
    const float alpha = std::sin(w) / (2 * q), inv = 1 / (1 + alpha);
    b0 = alpha * inv; b2 = -b0; a1 = -2 * std::cos(w) * inv; a2 = (1 - alpha) * inv;
}

float EngineSynth::sample(const EngineInput& input, float dt) {
    if (!(dt > 0) || !std::isfinite(dt)) return 0;
    if (dt != sampleDt_) {
        sampleDt_ = dt;
        smooth_ = 1 - std::exp(-dt * 22);
        breathRate_ = 1 - std::exp(-(float)kTau * 1300 * dt);
        wobbleRate_ = 1 - std::exp(-dt * 5);
        bovDecay_ = std::exp(-dt / 0.16f);
        dcRate_ = std::exp(-(float)kTau * 22 * dt);
        controlTick_ = 0;
    }
    const float targetRpm = std::isfinite(input.rpm) ? std::clamp(input.rpm, 0.0f, 18000.0f) : 0;
    const float load = std::isfinite(input.load) ? std::clamp(input.load, 0.0f, 1.0f) : 0;
    const float boost = std::isfinite(input.boost) ? std::clamp(input.boost, 0.0f, 1.0f) : 0;
    sRpm_ += (targetRpm - sRpm_) * smooth_;
    sLoad_ += (load - sLoad_) * smooth_;
    sBoost_ += (boost - sBoost_) * smooth_;
    if (targetRpm == 0 && sRpm_ < 0.1f) return 0;
    cylinders_ = std::clamp(input.cylinders, 1, 12);
    const int layout = cylinders_ == 8 && !input.flatPlane ? 12 : cylinders_ - 1;
    const float liters = std::isfinite(input.liters) ? std::clamp(input.liters, 0.6f, 10.0f) : 1.6f;
    const float redline = std::isfinite(input.redline) ? std::max(3000.0f, input.redline) : 6500;
    const EngineTone& tone = input.voice;
    const float muffler = std::clamp(tone.muffler, 0.0f, 1.0f), rasp = std::clamp(tone.rasp, 0.0f, 1.0f);
    if (tone.seed != seed_) {  // (its own pulse-to-pulse differences: a couple of slow harmonics of the cycle)
        seed_ = tone.seed;
        uint32_t r = seed_ ? seed_ : 1;
        auto next = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
        pulseA1_ = 0.03f + 0.08f * next(), pulseA2_ = 0.02f + 0.06f * next();
        pulseP1_ = next(), pulseP2_ = next();
    }
    if (controlTick_-- <= 0) {
        controlTick_ = 63;
        const float depth = std::pow(1.6f / liters, 0.32f);
        // The exhaust: its pipe's resonance (or one from its size, higher for a thin, raspy one),
        // sharper and brighter the more open it is, with the pipe's next mode above it.
        const float pipe = tone.exhaustHz > 0 ? tone.exhaustHz : 190 * depth * (0.8f + 0.5f * rasp);
        exhaust_.tune(pipe, 0.85f + 1.4f * muffler, dt);
        exhaust2_.tune(pipe * 2.7f, 1.2f + 1.5f * muffler, dt);
        intake_.tune((input.flatPlane ? 950 : 620) * depth * (0.85f + 0.35f * tone.intake), 1.1f + 1.2f * tone.intake, dt);
        const float cutoff = std::min(0.20f / dt, (650 + 1400 * sLoad_ + 0.1f * sRpm_) * depth * (input.induction == 1 ? 0.72f : 1) *
                                                      (1 + 0.8f * muffler + 0.5f * rasp));
        exhaustRate_ = 1 - std::exp(-(float)kTau * cutoff * dt);
    }
    const float n = noise();
    wobble_ += (n - wobble_) * wobbleRate_;
    // A big cam lopes at idle: each cycle a little stronger or weaker, and the rate hunts.
    const float idleK = std::clamp((2200 - sRpm_) / 1400, 0.0f, 1.0f), lope = std::clamp(tone.lope, 0.0f, 1.0f) * idleK;
    const double cycleHz = sRpm_ / 120.0 * (1 + wobble_ * (0.04f + 0.14f * lope));
    cycle_ += cycleHz * dt;
    cycle_ -= std::floor(cycle_);
    if (cycle_ < prevCycle_) lopeGain_ = 1 + lope * 0.7f * noise();  // (a new combustion cycle)
    prevCycle_ = cycle_;
    // Crossfade mip levels before their highest harmonic reaches Nyquist.
    int level = 0;
    while (level < kLevels - 1 && cycleHz * kHarmonics[level] * dt > 0.40) level++;
    const float blend = (float)std::clamp((cycleHz * kHarmonics[level] * dt - 0.30) / 0.10, 0.0, 1.0);
    auto combustion = [&](int l) {
        return read(kWaves.waves[layout][0][l], cycle_) * (1 - sLoad_) + read(kWaves.waves[layout][1][l], cycle_) * sLoad_;
    };
    float raw = combustion(level);
    if (level < kLevels - 1) raw += (combustion(level + 1) - raw) * blend;
    // Its own cylinder-to-cylinder differences (and the lope's).
    raw *= (1 + pulseA1_ * (float)std::sin(kTau * (cycle_ + pulseP1_)) + pulseA2_ * (float)std::sin(2 * kTau * (cycle_ + pulseP2_))) * lopeGain_;
    engLp_ += (raw - engLp_) * exhaustRate_;
    engLp2_ += (engLp_ - engLp2_) * exhaustRate_;
    // Off the throttle at high revs: pops and bangs out of an open exhaust (unburnt fuel
    // lighting in the pipe): a burst through the exhaust's own resonance.
    if (tone.crackle > 0 && sLoad_ < 0.12f && sRpm_ > 2200 && popEnv_ < 0.05f && noise() > 1 - tone.crackle * 0.0009f * sRpm_ / redline * (dt * 48000))
        popEnv_ = 1, popAmp_ = 0.5f + 0.5f * std::fabs(noise());
    float pop = 0;
    if (popEnv_ > 0.001f) {
        popLp_ += (n - popLp_) * 0.35f;
        pop = popLp_ * popEnv_ * popAmp_ * tone.crackle;
        popEnv_ *= std::exp(-dt / 0.012f);
    }
    const float body = exhaust_.run(raw + pop * 3.0f), body2 = exhaust2_.run(raw), intake = intake_.run(raw);
    breath_ += (n - breath_) * breathRate_;
    breathLow_ += (breath_ - breathLow_) * (breathRate_ * 0.15f);
    // Air stays below the combustion note. No white-noise clicks at each firing
    // event and no loud continuous turbo sine.
    float engine = (engLp2_ * 0.70f + body * (0.35f + 0.25f * muffler) + body2 * (0.06f + 0.18f * rasp) * (0.4f + 0.6f * muffler) +
                    intake * (0.12f + 0.20f * sLoad_) * (0.5f + 1.3f * tone.intake) + raw * 0.06f * rasp) *
                   (0.29f + 0.36f * sLoad_) * (0.85f + 0.04f * liters) * (0.85f + 0.35f * muffler);
    // The valvetrain: a tick at every valve event, louder up the revs.
    tickPhase_ += cycleHz * cylinders_ * 2 * dt;
    if (tickPhase_ >= 1) tickPhase_ -= std::floor(tickPhase_), tickEnv_ = 1;
    tickEnv_ *= 0.9f;
    engine += n * tickEnv_ * 0.012f * std::clamp(tone.mech, 0.0f, 1.0f) * (0.3f + 0.7f * std::min(sRpm_ / redline, 1.0f));
    engine += pop * 0.06f;
    if (input.induction == 1) {
        spoolPhase_ += (1800 + 3000 * sBoost_) * std::min(sRpm_ / redline, 1.2f) * dt;
        spoolPhase_ -= std::floor(spoolPhase_);
        engine += (float)std::sin(kTau * spoolPhase_) * 0.004f * sBoost_ * sBoost_;
        if (prevLoad_ > 0.5f && sLoad_ <= 0.5f && prevBoost_ > 0.35f) bovEnv_ = prevBoost_;
        engine += (breath_ - breathLow_) * 0.13f * bovEnv_;
    } else if (input.induction == 2) {
        whinePhase_ += sRpm_ / 60 * 15 * dt;
        whinePhase_ -= std::floor(whinePhase_);
        engine += (float)(std::sin(kTau * whinePhase_) + 0.20 * std::sin(2 * kTau * whinePhase_)) * 0.012f * sLoad_;
    }
    prevLoad_ = sLoad_; prevBoost_ = sBoost_;
    bovEnv_ *= bovDecay_;
    engine *= std::min(1.0f, sRpm_ / 450);
    const float dc = engine - dcIn_ + dcRate_ * dcOut_;
    dcIn_ = engine; dcOut_ = dc;
    return dc;  // the stereo mixer limits the final sum; keep this voice free of DC
}
