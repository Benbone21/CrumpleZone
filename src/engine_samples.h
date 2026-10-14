// Recorded engine loops. Files load before the device starts; callback data is immutable.
#pragma once
#include "engineaudio.h"
#include <array>
#include <string>
#include <vector>

struct VehicleSpec;
namespace engineSamples {
constexpr int kBanks = 18, kLoops = 16, kMips = 4;
const char* key(int id);
int id(const std::string& key);
int forVehicle(const VehicleSpec& spec);

struct Loop {
    float rpm = 0, gain = 1;
    bool loaded = false; // throttle-on versus coast/idle
    int rate = 48000;
    std::array<std::vector<float>, kMips> wave;
};
struct Bank {
    std::string source, author, license;
    std::vector<Loop> loops;
};
class Library {
public:
    // No partial banks: an invalid/missing loop leaves that engine on fallback.
    int load(const std::string& directory, std::vector<std::string>* errors = nullptr);
    const Bank* bank(int id) const {
        return id > 0 && id < kBanks && !banks_[id].loops.empty() ? &banks_[id] : nullptr;
    }
private:
    std::array<Bank, kBanks> banks_{};
};
class Player {
public:
    float sample(const Bank& bank, const EngineInput& input, float dt);
    float phase() const { return (float)cycle_; }
private:
    std::array<double, kLoops> position_{};
    std::array<float, kLoops> gain_{}, target_{};
    float rpm_ = 0, load_ = 0, gate_ = 0, dt_ = -1, smooth_ = 0;
    double cycle_ = 0;
    int tick_ = 0;
};
}

// Actual recordings take precedence; a missing engine's bank keeps the existing voice.
// Crossfade bank/vehicle changes without allocating or touching the filesystem.
class EngineVoice {
public:
    float sample(const EngineInput& input, float dt, const engineSamples::Bank* bank);
    float phase() const { return bank_ ? recorded_.phase() : procedural_.phase(); }
private:
    EngineSynth procedural_, previousProcedural_;
    engineSamples::Player recorded_, previous_;
    EngineInput lastInput_, previousInput_;
    uint64_t identity_ = UINT64_MAX;
    const engineSamples::Bank* bank_ = nullptr;
    const engineSamples::Bank* previousBank_ = nullptr;
    float fade_ = 1;
};
