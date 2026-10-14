#pragma once
#include <cstdint>

struct EngineInput {
    float rpm = 0, load = 0, boost = 0;
    int cylinders = 4, induction = 0;
    float liters = 1.6f, redline = 6500;
    bool flatPlane = false;
    uint8_t recording = 0; // engineSamples bank ID; 0 has no recorded bank
};

// Shared by local and nearby engines; no allocation or device access in sample().
class EngineSynth {
public:
    float sample(const EngineInput& input, float dt);
    float phase() const { return (float)(cycle_ * cylinders_ - (int)(cycle_ * cylinders_)); }
private:
    struct Resonator {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void tune(float frequency, float q, float dt);
        float run(float x) { const float y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    } exhaust_, intake_;
    double cycle_ = 0, whinePhase_ = 0, spoolPhase_ = 0;
    float sRpm_ = 0, sLoad_ = 0, sBoost_ = 0;
    int cylinders_ = 4, controlTick_ = 0;
    float bovEnv_ = 0, prevLoad_ = 0, prevBoost_ = 0;
    float breath_ = 0, breathLow_ = 0, wobble_ = 0, engLp_ = 0, engLp2_ = 0;
    float dcIn_ = 0, dcOut_ = 0;
    float sampleDt_ = -1, smooth_ = 0, breathRate_ = 0, wobbleRate_ = 0, bovDecay_ = 0, dcRate_ = 0, exhaustRate_ = 0;
    uint32_t rng_ = 0x12345678;
    float noise() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }
};
