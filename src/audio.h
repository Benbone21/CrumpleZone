// Procedural audio: engine, tire squeal, wind and impact crunches, all synthesized, plus
// the generative background music (music.h).
#pragma once
#include <atomic>
#include <cstdint>

#include "music.h"

class Audio {
public:
    bool start();
    void stop();
    // Called once per frame from the game thread.
    void setEngine(float rpm, float load) { rpm_ = rpm; load_ = load; }
    // The engine's voice (from the vehicle spec): cylinder count, displacement in liters,
    // induction (0 natural, 1 turbo, 2 supercharger), flat-plane crank, redline.
    void setVoice(int cylinders, float liters, int induction, bool flatPlane, float redline) {
        cyl_ = cylinders;
        liters_ = liters;
        induction_ = induction;
        flatPlane_ = flatPlane;
        redline_ = redline;
    }
    void setBoost(float boost) { boost_ = boost; }  // turbo boost built up, 0..1
    void setTires(float slip) { slip_ = slip; }
    void setSpeed(float metersPerSecond) { speed_ = metersPerSecond; }
    void setVolume(float v) { master_ = v; }
    // Background music: which map's piece, and its volume (0 = off). Changing map crossfades.
    void setMusic(int map, float volume) {
        musicMap_ = map;
        musicVol_ = volume;
    }
    void impact(float strength);  // 0..1
    void glass(float strength);   // window shattering, 0..1
    void pop();                   // tire blowout

private:
    static void callback(void* user, uint8_t* stream, int len);
    void render(float* out, int frames);

    uint32_t device_ = 0;
    int rate_ = 48000;
    std::atomic<float> rpm_{850}, load_{0}, slip_{0}, speed_{0}, master_{0.8f};
    std::atomic<int> cyl_{4}, induction_{0};
    std::atomic<bool> flatPlane_{false};
    std::atomic<float> liters_{1.6f}, redline_{6500}, boost_{0};
    std::atomic<float> pendingImpact_{0}, pendingGlass_{0}, pendingPop_{0};
    std::atomic<int> musicMap_{-1};
    std::atomic<float> musicVol_{0};
    Music music_;  // audio thread only (after start)

    // Synth state (audio thread only).
    double phase_ = 0, subPhase_ = 0;
    float sRpm_ = 850, sLoad_ = 0, sSlip_ = 0, sSpeed_ = 0, sBoost_ = 0;
    int fireCount_ = 0;                  // which cylinder fired last (for per-cylinder character)
    float cylGain_[12] = {};             // each cylinder's pulse strength (set by the voice)
    int voiceCyl_ = 0;                   // cylinder count the gains were made for
    double whinePhase_ = 0, spoolPhase_ = 0;
    float bovEnv_ = 0, bovBand_ = 0, bovLow_ = 0, prevLoad_ = 0;
    float intakeLp_ = 0;
    float engLp_ = 0, engLp2_ = 0, windLp_ = 0, crunchLp_ = 0;
    float bp1_ = 0, bp2_ = 0;
    float crunchEnv_ = 0, thumpEnv_ = 0, thumpPhase_ = 0;
    float glassEnv_ = 0, glassBand_ = 0, glassLow_ = 0;
    float pingEnv_[3] = {0, 0, 0}, pingPhase_[3] = {0, 0, 0}, pingFreq_[3] = {3000, 4200, 5600};
    float popEnv_ = 0, popLp_ = 0;
    uint32_t rng_ = 0x12345678;
    float noise() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }
};
