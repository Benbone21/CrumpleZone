#include "engineaudio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace {
constexpr int kSize = 2048, kLevels = 4, kLayouts = 18;
constexpr int kHarmonics[kLevels] = {96, 48, 24, 12};
constexpr double kTau = 6.283185307179586;
using Wave = std::array<float, kSize>;

// One combustion cycle's firing pattern: when each cylinder fires (a fraction of the 720
// degree cycle) and how strong its pulse arrives at the pipe, and how sharp the pulses are.
struct Firing { int cyl = 4; double angle[12] = {}, gain[12] = {}, shape = 1; };
Firing firing(int layout) {
    Firing f;
    const double crossBank[8] = {1.25, 0.55, 0.8, 1.1, 0.55, 1.25, 1.1, 0.8};
    const double jitter[8] = {0, 0.006, -0.004, 0.008, -0.006, 0.003, -0.007, 0.005};
    if (layout < 12) {  // even firing, 1 to 12 cylinders (flat-plane V8, inline engines, 60 degree V6)
        f.cyl = layout + 1;
        for (int c = 0; c < f.cyl; c++) f.angle[c] = (double)c / f.cyl, f.gain[c] = 1.0 + 0.025 * std::cos(c * 2.1);
    } else if (layout <= 14) {  // cross-plane V8: each bank's pulses bunch up (the burble)
        // 12 a small block's, 13 a big block's (stronger, its long unequal headers smear the
        // timing, rounder pulses), 14 an LS's (tuned headers even it out: cleaner)
        f.cyl = 8;
        const double depth = layout == 13 ? 1.6 : layout == 14 ? 0.55 : 1.0;
        for (int c = 0; c < 8; c++) f.angle[c] = c / 8.0 + (layout == 13 ? jitter[c] : 0), f.gain[c] = 1 + depth * (crossBank[c] - 1);
        f.shape = layout == 13 ? 0.78 : layout == 14 ? 1.05 : 1;
    } else if (layout == 15) {  // air-cooled boxer four: two cylinders a side into one pipe each (the chugga)
        f.cyl = 4;
        const double angle[4] = {0, 0.258, 0.5, 0.742}, gain[4] = {1.14, 0.86, 1.08, 0.92};
        for (int c = 0; c < 4; c++) f.angle[c] = angle[c], f.gain[c] = gain[c];
        f.shape = 0.9;
    } else if (layout == 16) {  // a triple, uneven: its pulses don't quite split the cycle (the rasp)
        f.cyl = 3;
        const double angle[3] = {0, 0.348, 0.676}, gain[3] = {1.1, 0.92, 1.0};
        for (int c = 0; c < 3; c++) f.angle[c] = angle[c], f.gain[c] = gain[c];
        f.shape = 1.15;
    } else {  // flat-plane V8: even, sharp pulses out of short headers (the scream)
        f.cyl = 8;
        for (int c = 0; c < 8; c++) f.angle[c] = c / 8.0, f.gain[c] = 1.0 + 0.02 * std::cos(c * 2.1);
        f.shape = 1.35;
    }
    return f;
}

// Built before main, never in the callback. A complete 720-degree combustion
// cycle retains bank differences, unlike repeating one identical click. Each
// event has a compression pulse followed by rarefaction; analytic Fourier
// coefficients roll off smoothly. Mip levels remove harmonics above Nyquist.
struct CycleWaves {
    Wave waves[kLayouts][2][kLevels];
    CycleWaves() {
        static double cosT[kSize], sinT[kSize];
        for (int i = 0; i < kSize; i++) cosT[i] = std::cos(kTau * i / kSize), sinT[i] = std::sin(kTau * i / kSize);
        for (int layout = 0; layout < kLayouts; layout++) {
            const Firing f = firing(layout);
            const int cyl = f.cyl;
            for (int load = 0; load < 2; load++) {
                std::complex<double> coefficients[97] = {};
                for (int h = 1; h <= 96; h++) {
                    std::complex<double> events = 0;
                    for (int c = 0; c < cyl; c++) events += f.gain[c] * std::polar(1.0, -kTau * h * f.angle[c]);
                    const std::complex<double> iw(0, kTau * h / cyl);
                    coefficients[h] = events * (1.0 / (double(load ? 24 : 10) * f.shape + iw) - 1.0 / (double(load ? 4 : 3) * f.shape + iw));
                }
                for (int level = 0; level < kLevels; level++) {
                    Wave& wave = waves[layout][load][level];
                    for (int i = 0; i < kSize; i++) {
                        double x = 0;
                        for (int h = 1; h <= kHarmonics[level]; h++) {
                            const int k = (h * i) & (kSize - 1);
                            x += coefficients[h].real() * cosT[k] - coefficients[h].imag() * sinT[k];
                        }
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
constexpr int kLayoutCylinders[kLayouts] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 8, 8, 8, 4, 3, 8};

// Each kind of engine's character on top of its EngineTone: its firing pattern (layout, -1:
// from its cylinders and crank), how its tone's burble, lope and roar come through, the
// howl of a firing harmonic (screamOrder times the firing note), VTEC, and its forced
// induction's layers (used only when it has that induction).
struct Profile {
    int layout;
    float rumble, lope, roar, scream, screamOrder, vtec, mech;
    float whistle, whoosh, dump, flutter;  // turbo: shaft whistle, air rush, blow-off valve, compressor flutter
    float whineOrder, whine;               // supercharger: whine at whineOrder times the crank speed
};
const Profile kProfiles[EK_KINDS] = {
    // layout rumble lope  roar scream order vtec mech   whistle whoosh dump flutter  order whine
    {-1, 1.0f, 0.0f, 1.0f, 0.00f, 3, 0, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_AUTO
    {3,  1.0f, 0.0f, 1.0f, 0.10f, 4, 0, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_I4
    {3,  1.0f, 0.0f, 1.0f, 0.12f, 4, 1, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_I4_VTEC
    {3,  1.0f, 0.0f, 0.9f, 0.04f, 4, 0, 1.0f,   1.0f, 1.0f, 1.0f, 0.0f,   15, 0.8f},  // EK_I4_TURBO
    {16, 1.0f, 0.0f, 1.0f, 0.30f, 3, 0, 1.0f,   0.8f, 0.7f, 0.8f, 0.3f,   15, 0.8f},  // EK_I3_TURBO
    {15, 1.0f, 0.1f, 1.0f, 0.00f, 4, 0, 1.4f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_FLAT4_AIR
    {5,  0.6f, 0.0f, 0.9f, 0.08f, 3, 0, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_V6
    {5,  0.5f, 0.0f, 0.8f, 0.00f, 3, 0, 1.0f,   1.2f, 1.0f, 0.2f, 1.0f,   15, 0.8f},  // EK_V6_TWIN_TURBO
    {5,  0.7f, 0.0f, 1.0f, 0.05f, 3, 0, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   24, 1.5f},  // EK_V6_SUPERCHARGED
    {12, 1.0f, 0.2f, 1.0f, 0.00f, 2, 0, 1.0f,   0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_V8_SMALL_BLOCK
    {13, 1.15f, 0.05f, 1.15f, 0.00f, 2, 0, 1.0f, 0.6f, 0.6f, 0.6f, 0.0f,  15, 0.8f},  // EK_V8_BIG_BLOCK
    {14, 0.5f, -0.2f, 0.85f, 0.08f, 2, 0, 1.0f, 0.6f, 0.6f, 0.6f, 0.0f,   15, 0.8f},  // EK_V8_LS
    {14, 0.5f, -0.15f, 0.85f, 0.06f, 2, 0, 1.0f, 0.6f, 0.6f, 0.6f, 0.0f,  26, 2.0f},  // EK_V8_LS_SUPERCHARGED
    {17, 0.0f, -1.0f, 1.0f, 0.45f, 3, 0, 1.0f,  0.45f, 0.4f, 0.6f, 0.0f,  15, 0.8f},  // EK_V8_FLAT_PLANE_TT
};
}

const char* engineKindName(int kind) {
    static const char* names[EK_KINDS] = {"generic", "I4", "I4 VTEC", "turbo I4", "turbo I3", "air-cooled flat-4", "V6", "twin-turbo V6",
                                          "supercharged V6", "small-block V8", "big-block V8", "LS V8", "supercharged LS V8", "flat-plane twin-turbo V8"};
    return kind >= 0 && kind < EK_KINDS ? names[kind] : names[0];
}

namespace {
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
        dcRate_ = std::exp(-(float)kTau * 22 * dt);
        spoolUp_ = 1 - std::exp(-dt / 0.35f);    // (the turbine spins up behind the boost...)
        spoolDown_ = 1 - std::exp(-dt / 0.9f);   // (...and winds down slowly, whistling lower)
        dumpDecay_ = std::exp(-dt / 0.22f);      // a blow-off valve's pssh
        flutterDecay_ = std::exp(-dt / 0.45f);   // compressor flutter (no blow-off valve): stu-tu-tu
        vtecRate_ = 1 - std::exp(-dt / 0.035f);  // VTEC's cam changeover: quick, not a click
        limitAttack_ = 1 - std::exp(-dt / 0.0005f), limitRelease_ = std::exp(-dt / 0.15f);
        controlTick_ = 0;
    }
    const float targetRpm = std::isfinite(input.rpm) ? std::clamp(input.rpm, 0.0f, 18000.0f) : 0;
    const float load = std::isfinite(input.load) ? std::clamp(input.load, 0.0f, 1.0f) : 0;
    const float boost = std::isfinite(input.boost) ? std::clamp(input.boost, 0.0f, 1.0f) : 0;
    sRpm_ += (targetRpm - sRpm_) * smooth_;
    sLoad_ += (load - sLoad_) * smooth_;
    sBoost_ += (boost - sBoost_) * smooth_;
    layers_ = Layers{};
    if (targetRpm == 0 && sRpm_ < 0.1f) return 0;
    cylinders_ = std::clamp(input.cylinders, 1, 12);
    const float liters = std::isfinite(input.liters) ? std::clamp(input.liters, 0.6f, 10.0f) : 1.6f;
    const float redline = std::isfinite(input.redline) ? std::max(3000.0f, input.redline) : 6500;
    const EngineTone& tone = input.voice;
    const int kind = tone.kind > EK_AUTO && tone.kind < EK_KINDS ? tone.kind : engineKindFor(cylinders_, input.induction, input.flatPlane, liters);
    const Profile& pro = kProfiles[kind];
    // (its firing pattern: the kind's own, when it has the cylinders for it)
    int layout = cylinders_ == 8 && !input.flatPlane ? 12 : cylinders_ - 1;
    if (pro.layout >= 0 && kLayoutCylinders[pro.layout] == cylinders_) layout = pro.layout;
    const float rpmK = std::min(sRpm_ / redline, 1.0f);
    // VTEC: past the switchover, under load, the high cam opens up: more intake honk, a
    // harder, thinner edge and a scream on the firing note's harmonics.
    if (pro.vtec > 0) {
        const float at = tone.vtecRpm > 0 ? tone.vtecRpm : 5500;
        vtecK_ += ((sRpm_ > at && sLoad_ > 0.25f ? pro.vtec : 0.0f) - vtecK_) * vtecRate_;
    } else vtecK_ = 0;
    const float muffler = std::clamp(tone.muffler + 0.15f * vtecK_, 0.0f, 1.0f), rasp = std::clamp(tone.rasp + 0.3f * vtecK_, 0.0f, 1.0f);
    const float intakeK = std::clamp(tone.intake + 0.55f * vtecK_, 0.0f, 1.4f);
    const float rumble = std::clamp(tone.rumble * pro.rumble, 0.0f, 1.0f), roarK = std::clamp(tone.roar * pro.roar, 0.0f, 1.0f);
    const uint32_t seedKey = tone.seed ^ (uint32_t)kind * 0x9E3779B9u;
    if (seedKey != seed_) {  // (its own pulse-to-pulse differences: a couple of slow harmonics of the cycle)
        seed_ = seedKey;
        uint32_t r = seed_ ? seed_ : 1;
        auto next = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
        pulseA1_ = 0.03f + 0.08f * next(), pulseA2_ = 0.02f + 0.06f * next();
        pulseP1_ = next(), pulseP2_ = next();
        // Each cylinder's own pulse strength: big, uneven pulses (long unequal headers, a lumpy
        // cam) are a big V8's burble; a smooth modern engine's are nearly all alike.
        const float spread = 0.02f + 0.55f * rumble;
        for (float& g : cylGain_) g = 1 + spread * (2 * next() - 1);
        // Its turbo's whistle and its blower's gearing are its own too.
        whistleHz_ = 2500 + 900 * next();
        whineOrder_ = pro.whineOrder * (0.88f + 0.24f * next());
    }
    if (controlTick_-- <= 0) {
        controlTick_ = 63;
        const float depth = std::pow(1.6f / liters, 0.32f);
        // The exhaust: its pipe's resonance (or one from its size, higher for a thin, raspy one),
        // sharper and brighter the more open it is, with the pipe's next mode above it.
        const float pipe = (tone.exhaustHz > 0 ? tone.exhaustHz : 190 * depth * (0.8f + 0.5f * rasp)) * (1 + 0.12f * vtecK_);
        exhaust_.tune(pipe, 0.85f + 1.4f * muffler, dt);
        exhaust2_.tune(pipe * 2.7f, 1.2f + 1.5f * muffler, dt);
        intake_.tune((input.flatPlane ? 950 : 620) * depth * (0.85f + 0.35f * std::min(intakeK, 1.0f)) * (1 + 0.35f * vtecK_), 1.1f + 1.2f * std::min(intakeK, 1.0f), dt);
        const float cutoff = std::min(0.20f / dt, (650 + 1400 * sLoad_ + 0.1f * sRpm_) * depth * (input.induction == 1 ? 0.72f : 1) *
                                                      (1 + 0.8f * muffler + 0.5f * rasp));
        exhaustRate_ = 1 - std::exp(-(float)kTau * cutoff * dt);
        // A firing harmonic's howl (flat-plane, VTEC, a triple's rasp) follows the revs.
        const float firingHz = sRpm_ / 120 * cylinders_;
        scream_.tune(std::max(60.0f, firingHz * pro.screamOrder * (1 + 0.5f * vtecK_)), 3.5f, dt);
        whoosh_.tune(input.induction == 2 ? std::clamp(sRpm_ / 60 * whineOrder_ * 0.5f, 300.0f, 1400.0f) : 700 + 1500 * spool_, input.induction == 2 ? 2.2f : 1.3f, dt);
        dumpRes_.tune(2300, 0.9f, dt);
        flutterRes_.tune(620, 1.6f, dt);
    }
    const float n = noise();
    wobble_ += (n - wobble_) * wobbleRate_;
    // A big cam lopes at idle: each cycle a little stronger or weaker, and the rate hunts.
    const float idleK = std::clamp((2200 - sRpm_) / 1400, 0.0f, 1.0f), lope = std::clamp(tone.lope + pro.lope, 0.0f, 1.0f) * idleK;
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
    // ...and each firing's own strength (eased from one to the next: no clicks).
    cylGainS_ += (cylGain_[std::min(11, (int)(cycle_ * cylinders_))] - cylGainS_) * 0.12f;
    raw *= cylGainS_;
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
    breath_ += (n - breath_) * breathRate_;
    breathLow_ += (breath_ - breathLow_) * (breathRate_ * 0.15f);
    // The roar: exhaust gas rushing out with every pulse, louder the harder and faster it's
    // worked, through the pipe's own resonance (an open, big-bore pipe the most).
    pulseEnv_ += (std::max(raw, 0.0f) - pulseEnv_) * 0.08f;
    roarLp_ += (n - roarLp_) * exhaustRate_;
    roarLp2_ += (roarLp_ - roarLp2_) * exhaustRate_;
    const float worked = (0.25f + 0.75f * sLoad_) * (0.35f + 0.65f * rpmK);
    const float roar = roarLp2_ * pulseEnv_ * roarK * worked * (0.6f + 0.8f * muffler) * 2.2f;
    // The thump: a big engine's uneven pulses felt as much as heard, once a cycle (half the
    // crank speed), fading as it revs out.
    const float size = std::clamp(liters / 5.0f, 0.3f, 1.5f);
    const float thump = (float)std::sin(kTau * cycle_) * rumble * size * 0.24f * (1 - 0.6f * rpmK) * (0.6f + 0.4f * sLoad_);
    const float body = exhaust_.run(raw + pop * 3.0f + roar), body2 = exhaust2_.run(raw + roar * 0.5f), intake = intake_.run(raw);
    const float screamK = pro.scream + 0.5f * vtecK_;
    const float scream = scream_.run(raw) * screamK * (0.3f + 0.7f * sLoad_) * (0.3f + 0.7f * rpmK) * 1.4f;
    // Air stays below the combustion note. No white-noise clicks at each firing
    // event and no loud continuous turbo sine.
    // (The shared combustion note is only the base: the pipe, the intake and the rasp on top of
    // it are what make two engines of the same size sound nothing alike.)
    float engine = (engLp2_ * 0.45f + body * (0.30f + 0.70f * muffler) + body2 * (0.04f + 0.40f * rasp) * (0.4f + 0.6f * muffler) +
                    intake * (0.08f + 0.25f * sLoad_) * (0.3f + 2.2f * intakeK) + raw * 0.14f * rasp * rasp + thump + scream) *
                   (0.29f + 0.36f * sLoad_) * (0.70f + 0.09f * liters) * (0.85f + 0.35f * muffler);  // (a big engine is a loud one)
    // The valvetrain: a tick at every valve event, louder up the revs.
    tickPhase_ += cycleHz * cylinders_ * 2 * dt;
    if (tickPhase_ >= 1) tickPhase_ -= std::floor(tickPhase_), tickEnv_ = 1;
    tickEnv_ *= 0.9f;
    engine += n * tickEnv_ * 0.012f * std::clamp(tone.mech * pro.mech, 0.0f, 1.0f) * (0.3f + 0.7f * rpmK);
    engine += pop * 0.06f;
    layers_.vtec = vtecK_;
    if (input.induction == 1) {
        // The turbo: its shaft speed follows the boost (and the exhaust flow), lagging behind
        // and winding down slowly. A whistle that rises with it, the rush of air into the
        // compressor, and on a lift with boost up: a blow-off valve's pssh, or (without one) the
        // compressor fluttering as the air stalls against the closed throttle.
        const float target = sBoost_ * (0.25f + 0.75f * rpmK);
        spool_ += (target - spool_) * (target > spool_ ? spoolUp_ : spoolDown_);
        whistlePhase_ += whistleHz_ * (0.45f + 0.55f * spool_) * dt;
        whistlePhase_ -= std::floor(whistlePhase_);
        const float whistle = (float)std::sin(kTau * whistlePhase_) * 0.022f * pro.whistle * spool_ * spool_;
        const float air = whoosh_.run(n) * 0.16f * pro.whoosh * spool_ * (0.35f + 0.65f * sLoad_);
        if (prevLoad_ > 0.4f && sLoad_ <= 0.4f && spool_ > 0.25f) {
            dumpEnv_ = std::max(dumpEnv_, spool_ * pro.dump), flutterEnv_ = std::max(flutterEnv_, spool_ * pro.flutter);
            spool_ *= 0.8f;  // (the boost is let go)
        }
        flutterPhase_ += (13 + 9 * flutterEnv_) * dt;
        flutterPhase_ -= std::floor(flutterPhase_);
        const float gate = std::max(0.0f, (float)std::sin(kTau * flutterPhase_));
        const float dump = dumpRes_.run(n) * 0.30f * dumpEnv_ + flutterRes_.run(n) * gate * gate * gate * 0.9f * flutterEnv_;
        dumpEnv_ *= dumpDecay_; flutterEnv_ *= flutterDecay_;
        engine += whistle + air + dump;
        layers_.turbo = std::fabs(whistle) + std::fabs(air), layers_.dump = std::fabs(dump);
    } else if (input.induction == 2) {
        // The supercharger: a belt-driven blower's whine, locked to the crank (its gearing and
        // rotor lobes times the revs), louder the harder it's pushing and the faster it turns.
        const float hz = sRpm_ / 60 * whineOrder_;
        whinePhase_ += hz * dt;
        whinePhase_ -= std::floor(whinePhase_);
        // (each rotor lobe's pass is a little different: a buzz of sidebands around the whine)
        lobePhase_ += hz * 0.25f * dt;
        lobePhase_ -= std::floor(lobePhase_);
        const float second = hz * 2 * dt < 0.45f ? 0.3f * (float)std::sin(2 * kTau * whinePhase_) : 0.0f;
        const float whine = ((float)std::sin(kTau * whinePhase_) + second) * (1 + 0.35f * (float)std::sin(kTau * lobePhase_) + 0.15f * wobble_) * 0.04f * pro.whine *
                            (0.45f + 0.55f * sLoad_) * (0.35f + 0.65f * rpmK) * (hz * dt < 0.45f ? 1.0f : 0.0f);
        // ...and the air it pumps: a hollow roar through the intake, under load.
        const float air = whoosh_.run(n) * 0.14f * pro.whine * sLoad_ * (0.3f + 0.7f * rpmK);
        engine += whine + air;
        layers_.whine = std::fabs(whine) + std::fabs(air);
        spool_ = dumpEnv_ = flutterEnv_ = 0;
    } else spool_ = dumpEnv_ = flutterEnv_ = 0;
    prevLoad_ = sLoad_; prevBoost_ = sBoost_;
    engine *= std::min(1.0f, sRpm_ / 450);
    const float dc = engine - dcIn_ + dcRate_ * dcOut_;
    dcIn_ = engine; dcOut_ = dc;
    // Its own peak limiter: the loudest engines flat out (an open LS, a VTEC four on the cam, a
    // blip) ride down smoothly to under 0.75 instead of clipping in the mixer; then a soft knee
    // for anything left over, so this voice never goes past 0.9.
    peakEnv_ = std::max(std::fabs(dc), peakEnv_ * limitRelease_);
    const float wantGain = std::min(1.0f, 0.75f / std::max(peakEnv_, 1e-6f));
    limitGain_ += (wantGain - limitGain_) * (wantGain < limitGain_ ? limitAttack_ : 1 - limitRelease_);
    float y = dc * limitGain_;
    const float a = std::fabs(y);
    if (a > 0.8f) y = std::copysign(0.8f + 0.1f * std::tanh((a - 0.8f) / 0.1f), y);
    return y;  // (and free of DC)
}
