#pragma once
#include <cstdint>

// What makes one engine sound like itself beyond its cylinder count and size: its exhaust
// (the pipe's resonance, how open it is), its cam, its intake, its valvetrain, and a few small
// cylinder-to-cylinder differences of its own (seeded by its name).
struct EngineTone {
    float exhaustHz = 0;   // main exhaust resonance (Hz); 0 = from its displacement
    float muffler = 0.35f; // 0 quiet factory exhaust .. 1 open pipes
    float lope = 0;        // a big cam's lumpy idle, 0..1
    float intake = 0.4f;   // intake roar, a carburettor's or VTEC's honk, 0..1
    float mech = 0.25f;    // valvetrain tick and gear noise, 0..1
    float rasp = 0.3f;     // 0 deep and round .. 1 thin and raspy (small cylinders, high revs)
    float crackle = 0;     // pops and bangs off the throttle at high revs, 0..1
    uint32_t seed = 0;     // its own cylinder-to-cylinder differences
    float rumble = 0;      // big, uneven exhaust pulses: a big V8's burble and chest thump, 0..1
    float roar = 0.3f;     // exhaust gas roaring through the pipes under load, 0..1
};

struct EngineInput {
    float rpm = 0, load = 0, boost = 0;
    int cylinders = 4, induction = 0;
    float liters = 1.6f, redline = 6500;
    bool flatPlane = false;
    uint8_t recording = 0; // engineSamples bank ID; 0 has no recorded bank
    EngineTone voice{};
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
    } exhaust_, exhaust2_, intake_;
    double cycle_ = 0, whinePhase_ = 0, spoolPhase_ = 0;
    float sRpm_ = 0, sLoad_ = 0, sBoost_ = 0;
    int cylinders_ = 4, controlTick_ = 0;
    float bovEnv_ = 0, prevLoad_ = 0, prevBoost_ = 0;
    float breath_ = 0, breathLow_ = 0, wobble_ = 0, engLp_ = 0, engLp2_ = 0;
    float dcIn_ = 0, dcOut_ = 0;
    // Its own character (EngineTone): cylinder-to-cylinder differences, the cam's lope from one
    // cycle to the next, the valvetrain's tick, pops on the overrun.
    uint32_t seed_ = 0xFFFFFFFFu;  // (none yet: set up on the first sample)
    float pulseA1_ = 0, pulseA2_ = 0, pulseP1_ = 0, pulseP2_ = 0;
    float lopeGain_ = 1, tickEnv_ = 0, popEnv_ = 0, popAmp_ = 0, popLp_ = 0;
    float cylGain_[12] = {}, cylGainS_ = 1, roarLp_ = 0, roarLp2_ = 0, pulseEnv_ = 0;
    double prevCycle_ = 0, tickPhase_ = 0;
    float sampleDt_ = -1, smooth_ = 0, breathRate_ = 0, wobbleRate_ = 0, bovDecay_ = 0, dcRate_ = 0, exhaustRate_ = 0;
    uint32_t rng_ = 0x12345678;
    float noise() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }
};
