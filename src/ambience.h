// Lightweight outdoor soundscape. Synthetic wind, foliage, bird phrases and insects;
// no downloaded recordings or species claims. All voice storage is fixed-size.
#pragma once
#include <array>
#include <cstdint>

struct AmbienceInput {
    int map = -1;
    float hours = 10, volume = 0, exposure = 1;
};

class Ambience {
public:
    void render(float* stereo, int frames, int rate, const AmbienceInput& input);
private:
    struct Bird {
        float wait = 0, age = 0, duration = 0, hz = 0, sweep = 0, phase = 0;
        float panL = 0.7f, panR = 0.7f, gain = 0;
        int calls = 0;
    };
    std::array<Bird, 3> birds_{};
    uint32_t rng_ = 0x9a03c76b;
    float volume_ = 0, wind_ = 0, leaves_ = 0, birdsGain_ = 0, insects_ = 0;
    float lowL_ = 0, lowR_ = 0, rustleL_ = 0, rustleR_ = 0;
    float gust_ = 0.5f, gustTarget_ = 0.5f, gustWait_ = 0;
    double cricketPhase_ = 0, cricketPulse_ = 0;
    bool initialized_ = false;
    float random();
};
