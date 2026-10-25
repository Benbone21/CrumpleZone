#pragma once
#include <cstdint>

// What kind of engine it is: its firing pattern (cross-plane, flat-plane, odd-fire, boxer),
// its cams (VTEC's switchover, a big cam's lope) and its forced induction's layers (a turbo's
// spool, whistle, blow-off or flutter; a blower's whine). EK_AUTO picks one from the
// cylinder count, induction and crank (engineKindFor).
enum EngineKind : uint8_t {
    EK_AUTO, EK_I4, EK_I4_VTEC, EK_I4_TURBO, EK_I3_TURBO, EK_FLAT4_AIR, EK_V6, EK_V6_TWIN_TURBO, EK_V6_SUPERCHARGED,
    EK_V8_SMALL_BLOCK, EK_V8_BIG_BLOCK, EK_V8_LS, EK_V8_LS_SUPERCHARGED, EK_V8_FLAT_PLANE_TT, EK_KINDS
};
const char* engineKindName(int kind);
int engineKindFor(int cylinders, int induction, bool flatPlane, float liters);  // (never EK_AUTO)

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
    uint8_t kind = EK_AUTO; // EngineKind
    float vtecRpm = 0;     // VTEC's cam switchover (rpm); 0 = 5,500 on an EK_I4_VTEC
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
    // The forced-induction layers' levels in the last sample (tests): turbo whistle and air,
    // blow-off and flutter, supercharger whine. All zero on a naturally aspirated engine.
    struct Layers { float turbo = 0, dump = 0, whine = 0, vtec = 0; };
    const Layers& layers() const { return layers_; }
private:
    struct Resonator {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void tune(float frequency, float q, float dt);
        float run(float x) { const float y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    } exhaust_, exhaust2_, intake_, scream_, whoosh_, dumpRes_, flutterRes_;
    double cycle_ = 0, whinePhase_ = 0, spoolPhase_ = 0;
    float sRpm_ = 0, sLoad_ = 0, sBoost_ = 0;
    int cylinders_ = 4, controlTick_ = 0;
    float prevLoad_ = 0, prevBoost_ = 0;
    float breath_ = 0, breathLow_ = 0, wobble_ = 0, engLp_ = 0, engLp2_ = 0;
    float dcIn_ = 0, dcOut_ = 0;
    // Its own character (EngineTone): cylinder-to-cylinder differences, the cam's lope from one
    // cycle to the next, the valvetrain's tick, pops on the overrun.
    uint32_t seed_ = 0xFFFFFFFFu;  // (none yet: set up on the first sample)
    float pulseA1_ = 0, pulseA2_ = 0, pulseP1_ = 0, pulseP2_ = 0;
    float lopeGain_ = 1, tickEnv_ = 0, popEnv_ = 0, popAmp_ = 0, popLp_ = 0;
    float cylGain_[12] = {}, cylGainS_ = 1, roarLp_ = 0, roarLp2_ = 0, pulseEnv_ = 0;
    double prevCycle_ = 0, tickPhase_ = 0;
    float sampleDt_ = -1, smooth_ = 0, breathRate_ = 0, wobbleRate_ = 0, dcRate_ = 0, exhaustRate_ = 0;
    // Forced induction and cams: the turbo's shaft speed (spools up, winds down), the
    // blow-off's and flutter's envelopes, VTEC's switchover.
    float spool_ = 0, dumpEnv_ = 0, flutterEnv_ = 0, vtecK_ = 0, whistleHz_ = 0, whineOrder_ = 0;
    double whistlePhase_ = 0, flutterPhase_ = 0, lobePhase_ = 0;
    float spoolUp_ = 0, spoolDown_ = 0, dumpDecay_ = 0, flutterDecay_ = 0, vtecRate_ = 0;
    float peakEnv_ = 0, limitGain_ = 1, limitAttack_ = 0, limitRelease_ = 0;
    Layers layers_{};
    uint32_t rng_ = 0x12345678;
    float noise() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }
};
