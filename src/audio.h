// Procedural audio: engine, tire squeal, wind and impact crunches, all synthesized, plus
// the generative background music (music.h).
#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <vector>

#include "music.h"
#include "ambience.h"
#include "engineaudio.h"
#include "engine_samples.h"
#include "mathx.h"

class Sim;

class Audio {
public:
    bool start();
    void stop();
    // Called once per frame from the game thread.
    void setEngine(float rpm, float load) { rpm_ = rpm; load_ = load; }
    // The engine's voice (from the vehicle spec): cylinder count, displacement in liters,
    // induction (0 natural, 1 turbo, 2 supercharger), flat-plane crank, redline.
    void setVoice(int cylinders, float liters, int induction, bool flatPlane, float redline, const EngineTone& voice = EngineTone{}) {
        vExhaust_ = voice.exhaustHz, vMuffler_ = voice.muffler, vLope_ = voice.lope, vIntake_ = voice.intake;
        vMech_ = voice.mech, vRasp_ = voice.rasp, vCrackle_ = voice.crackle, vSeed_ = voice.seed;
        vRumble_ = voice.rumble, vRoar_ = voice.roar;
        cyl_ = cylinders;
        liters_ = liters;
        induction_ = induction;
        flatPlane_ = flatPlane;
        redline_ = redline;
    }
    void setBoost(float boost) { boost_ = boost; }  // turbo boost built up, 0..1
    void setRecording(int recording) { recording_ = recording; }
    // The camp's recorded loops (campfire.wav, wind.wav: mono PCM16 in `directory`); before start only. A missing file leaves that layer synthesized.
    int loadAmbience(const std::string& directory);
    // Before start only; loading/decoding is never performed by the callback.
    int loadEngineRecordings(const std::string& directory);
    int recordingBankCount() const { return recordingCount_; }
    void setTires(float slip) { slip_ = slip; }
    void setSpeed(float metersPerSecond) { speed_ = metersPerSecond; }
    void setVolume(float v, float sceneGain = 1) { master_ = v; sceneGain_ = sceneGain; }
    // Cabin exposure attenuates outdoor ambience and softens the engine mix.
    void setEnvironment(int map, float hours, float volume, float exposure = 1) {
        ambienceMap_ = map; hours_ = hours; ambienceVol_ = volume; exposure_ = exposure;
    }
    // Background music: which map's piece, and its volume (0 = off). Changing map crossfades.
    // `biome` (biome.h, 0 = the map's own piece) picks that country's music instead; it should
    // already be steadied (BiomeTracker).
    void setMusic(int map, float volume, int biome = 0) {
        musicMap_ = map;
        musicVol_ = volume;
        musicBiome_ = biome;
    }
    void impact(float strength);  // 0..1
    void glass(float strength);   // window shattering, 0..1
    void pop();                   // tire blowout
    // The main menu's camp (fire crackle, stream, wind, crickets): `level` 0 silent .. 1, the game eases it in and out; the pans (-1 left .. 1 right) place the fire and the stream.
    void setCampfire(float level, float panFire = 0.2f, float panStream = -0.6f) { campfire_ = level, campPanFire_ = panFire, campPanStream_ = panStream; }
    void setHiss(float level);    // tire air hiss: 0 = off, 1 = full hiss (inflate/deflate)
    // Game thread: publish the nearest engines, with distance attenuation and stereo pan.
    void setNearbyEngines(const Sim& sim, const V3& listener, const V3& right, float timeScale);

private:
    friend struct AudioTest;
    static void callback(void* user, uint8_t* stream, int len);
    void render(float* out, int frames);

    uint32_t device_ = 0;
    int rate_ = 48000;
    std::atomic<float> rpm_{850}, load_{0}, slip_{0}, speed_{0}, master_{0.8f};
    std::atomic<int> cyl_{4}, induction_{0};
    std::atomic<int> recording_{0};
    std::atomic<bool> flatPlane_{false};
    std::atomic<float> liters_{1.6f}, redline_{6500}, boost_{0};
    // The engine's voice (EngineTone), a field at a time for the audio thread.
    std::atomic<float> vExhaust_{0}, vMuffler_{0.35f}, vLope_{0}, vIntake_{0.4f}, vMech_{0.25f}, vRasp_{0.3f}, vCrackle_{0}, vRumble_{0}, vRoar_{0.3f};
    std::atomic<uint32_t> vSeed_{0};
    std::atomic<float> pendingImpact_{0}, pendingGlass_{0}, pendingPop_{0};
    std::atomic<float> sceneGain_{1}, localGain_{1};
    std::atomic<int> ambienceMap_{-1};
    std::atomic<float> hours_{10}, ambienceVol_{0}, exposure_{1};
    Ambience ambience_;
    float sMaster_ = 0.8f, sLocalGain_ = 1, sExposure_ = 1, cabinLowL_ = 0, cabinLowR_ = 0;
    std::atomic<int> musicMap_{-1}, musicBiome_{0};
    std::atomic<float> musicVol_{0};
    Music music_;  // audio thread only (after start)

    // Synth state (audio thread only).
    engineSamples::Library recordings_;
    bool recordingsReady_ = false;
    int recordingCount_ = 0;
    EngineVoice engine_;
    float sSlip_ = 0, sSpeed_ = 0, windLp_ = 0, crunchLp_ = 0;
    static constexpr int kNearbyEngines = 8;
    struct Source {
        uint32_t id = 0;
        EngineInput engine;
        float gain = 0, pan = 0;
    };
    struct Sources { std::array<Source, kNearbyEngines> engines{}; int count = 0; };
    // Single producer/consumer triple buffer: the callback never locks or allocates,
    // and the game cannot overwrite a frame the callback is still reading.
    std::array<Sources, 3> sources_{};
    std::atomic<unsigned> middleSources_{1};
    unsigned backSources_ = 2, frontSources_ = 0;
    struct NearbyVoice {
        uint32_t id = 0;
        EngineVoice synth;
        EngineInput input;
        float gain = 0, targetGain = 0, left = 0.7f, right = 0.7f, targetLeft = 0.7f, targetRight = 0.7f;
    };
    std::array<NearbyVoice, kNearbyEngines> nearby_{};
    void updateRemoteVoices();
    float bp1_ = 0, bp2_ = 0;
    float crunchEnv_ = 0, thumpEnv_ = 0, thumpPhase_ = 0;
    float glassEnv_ = 0, glassBand_ = 0, glassLow_ = 0;
    float pingEnv_[3] = {0, 0, 0}, pingPhase_[3] = {0, 0, 0}, pingFreq_[3] = {3000, 4200, 5600};
    float popEnv_ = 0, popLp_ = 0;
    std::atomic<float> pendingHiss_{0};
    std::atomic<float> campfire_{0};
    std::atomic<float> campPanFire_{0.2f}, campPanStream_{-0.6f};
    std::vector<float> fireLoop_, windLoop_;  // recorded loops (immutable once the device runs)
    bool ambienceLoaded_ = false;
    size_t firePos_ = 0, windPos_ = 0;       // audio thread only
    float cfPop_ = 0, cfLow_ = 0, cfHp_ = 0, cfGain_ = 0, cfRoar_ = 0, cfT_ = 0, stLp1_ = 0, stLp2_ = 0, wnLp_ = 0, wnHp_ = 0, cfPhase_[3] = {0, 0, 0};
    float hissEnv_ = 0, hissBp_ = 0, hissBp2_ = 0;
    uint32_t rng_ = 0x12345678;
    float noise() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }
};
