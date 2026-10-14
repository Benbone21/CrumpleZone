#include "../src/audio.h"
#include "../src/softbody.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace {
double energy(const std::vector<float>& x) { double sum = 0; for (float s : x) sum += s * s; return sum / x.size(); }
bool clean(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float s) { return std::isfinite(s) && std::fabs(s) <= 1; });
}
std::vector<float> nature(AmbienceInput input, int rate = 48000, int seconds = 12, int block = 512) {
    Ambience ambience;
    std::vector<float> wave(rate * seconds * 2, 0);
    for (int at = 0; at < rate * seconds; at += block)
        ambience.render(wave.data() + at * 2, std::min(block, rate * seconds - at), rate, input);
    return wave;
}
}

void soundscapeTests(const std::function<void(const char*, bool)>& check, void (*render)(Audio&, float*, int)) {
    const auto day = nature({4, 8, 1, 1}), night = nature({4, 23, 1, 1});
    check("forest ambience is audible, bounded and different between day and night", clean(day) && clean(night) &&
          energy(day) > 1e-6 && energy(night) > 1e-6 && day != night);
    const auto cabin = nature({4, 8, 1, 0.3f});
    check("cockpit attenuates outdoor ambience", energy(cabin) < energy(day) * 0.15);
    const auto freeway = nature({2, 8, 1, 1});
    check("freeway uses a distinct wind-only habitat", clean(freeway) && energy(freeway) > 1e-7 && day != freeway);
    const auto chunked = nature({4, 8, 1, 1}, 48000, 12, 137);
    check("nature scheduling is independent of callback block size", day == chunked);
    check("ambience off and invalid map are silent", energy(nature({4, 8, 0, 1}, 8000, 1)) == 0 &&
          energy(nature({99, 8, 1, 1}, 8000, 1)) == 0);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    bool stable = true;
    for (int rate : {8000, 22050, 44100, 96000}) {
        const auto wave = nature({4, 8, 1, 1}, rate, 10);
        stable &= clean(wave) && energy(wave) > 1e-6;
    }
    stable &= clean(nature({4, nan, nan, nan}, 8000, 1));
    check("outdoor voices stay finite across sample rates and invalid inputs", stable);

    Audio full;
    full.setEngine(2000, 0.6f); full.setMusic(4, 0.6f); full.setEnvironment(4, 8, 0.8f);
    std::vector<float> block(1024);
    for (int k = 0; k < 80; k++) render(full, block.data(), 512);
    check("engine, music and nature mix stays bounded", clean(block) && energy(block) > 1e-5);
    full.setVolume(0);
    for (int k = 0; k < 100; k++) render(full, block.data(), 512);
    check("master mute silences music, engine and ambience together", energy(block) == 0);
    full.setVolume(1);
    for (int k = 0; k < 80; k++) render(full, block.data(), 512);
    check("master unmute restores the soundscape", clean(block) && energy(block) > 1e-5);

    Sim sim; sim.setupEmpty(); sim.player = 0;
    sim.addCar(vehicleCatalog()[2], V3(-40, 0.05, -40), 0, V3(0.4, 0.5, 0.6));
    Audio local; local.setEngine(3000, 0.8f);
    local.setNearbyEngines(sim, V3(-40, 1, -40), V3(0, 0, 1), 1);
    for (int k = 0; k < 80; k++) render(local, block.data(), 512);
    const double close = energy(block);
    local.setNearbyEngines(sim, V3(-40, 1, 60), V3(0, 0, 1), 1);
    for (int k = 0; k < 120; k++) render(local, block.data(), 512);
    check("walking away attenuates the player's own engine", energy(block) < close * 0.01);
    EngineVoice changed;
    EngineInput engine{4500, 0.8f, 0, 4, 0, 1.6f, 6500, false};
    float previous = 0;
    for (int i = 0; i < 48000; i++) previous = changed.sample(engine, 1.0f / 48000, nullptr);
    engine = EngineInput{850, 0.05f, 0, 8, 0, 6.2f, 6500, false};
    bool smoothChange = true;
    for (int i = 0; i < 12000; i++) {
        const float sample = changed.sample(engine, 1.0f / 48000, nullptr);
        smoothChange &= std::isfinite(sample) && std::fabs(sample - previous) < 0.08f;
        previous = sample;
    }
    check("switching vehicle engine layouts crossfades without a waveform jump", smoothChange);

    Music music; music.setSeed(701);
    bool musical = true;
    for (int map = 0; map < 5; map++) {
        music.setRate(48000);
        double total = 0;
        for (int k = 0; k < 500; k++) {
            std::fill(block.begin(), block.end(), 0); music.render(block.data(), 512, map, 0.7f);
            musical &= clean(block); total += energy(block);
        }
        musical &= total > 0.001;
    }
    check("all five map arrangements produce finite audible music", musical);
    music.setRate(48000);
    bool transitions = true;
    for (int k = 0; k < 1200; k++) {
        std::fill(block.begin(), block.end(), 0);
        music.render(block.data(), 512, k < 450 ? 1 : 4, k < 900 ? 0.8f : 0);
        transitions &= clean(block);
    }
    check("music map changes and mute tails remain bounded", transitions && energy(block) < 1e-9);
}
