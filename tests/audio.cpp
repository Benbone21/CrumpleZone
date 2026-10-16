// Offline audio checks: no device, speakers or window required.
#include "../src/audio.h"
#include "../src/mods.h"
#include "../src/softbody.h"
#include "../src/sdl_min.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>
#include <limits>
#include <functional>
#include <new>

// Trace C++ allocations only around offline calls to the actual callback mixer.
static std::atomic<bool> traceAllocations{false};
static std::atomic<unsigned> callbackAllocations{0};
void* operator new(std::size_t bytes) {
    if (traceAllocations.load(std::memory_order_relaxed)) callbackAllocations++;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void recordedEngineTests(const std::function<void(const char*, bool)>& check, void (*render)(Audio&, float*, int));
void soundscapeTests(const std::function<void(const char*, bool)>& check, void (*render)(Audio&, float*, int));

struct AudioTest {
    static void render(Audio& audio, float* out, int frames) { audio.render(out, frames); }
    static bool hissRates() {
        for (int rate : {8000, 22050, 44100, 48000, 192000}) {
            Audio audio;
            audio.rate_ = rate;
            audio.setEngine(0, 0); audio.setHiss(1);
            std::vector<float> samples(rate * 2);
            audio.render(samples.data(), rate);
            double energy = 0;
            for (float value : samples) {
                if (!std::isfinite(value) || std::fabs(value) > 1) return false;
                energy += value * value;
            }
            if (energy < 1e-5) return false;
        }
        return true;
    }
    static bool coherent(Audio& audio) {
        audio.updateRemoteVoices();
        const auto& frame = audio.sources_[audio.frontSources_];
        return frame.count == 0 || (frame.count == 1 && frame.engines[0].engine.rpm == frame.engines[0].id);
    }
};

namespace {
struct Energy { double left = 0, right = 0; bool finite = true; };
std::vector<float> engineWave(EngineInput input, int sampleRate = 48000) {
    EngineSynth engine;
    const float dt = 1.0f / sampleRate;
    for (int i = 0; i < sampleRate / 2; i++) engine.sample(input, dt);
    std::vector<float> wave(sampleRate);
    for (float& x : wave) x = engine.sample(input, dt);
    return wave;
}
double rms(const std::vector<float>& wave) {
    double e = 0; for (float x : wave) e += x * x;
    return std::sqrt(e / wave.size());
}
double tone(const std::vector<float>& wave, double hz, int sampleRate = 48000) {
    double re = 0, im = 0;
    for (size_t i = 0; i < wave.size(); i++) {
        const double phase = 2 * M_PI * hz * i / sampleRate;
        re += wave[i] * std::cos(phase); im += wave[i] * std::sin(phase);
    }
    return (re * re + im * im) / (wave.size() * wave.size());
}
Energy measure(Audio& audio) {
    float block[1024];
    Energy energy;
    for (int k = 0; k < 80; k++) {
        AudioTest::render(audio, block, 512);
        for (int i = 0; i < 512; i++) {
            energy.finite &= std::isfinite(block[2 * i]) && std::isfinite(block[2 * i + 1]) &&
                             std::fabs(block[2 * i]) <= 1 && std::fabs(block[2 * i + 1]) <= 1;
            if (k > 40) { energy.left += block[2 * i] * block[2 * i]; energy.right += block[2 * i + 1] * block[2 * i + 1]; }
        }
    }
    return energy;
}
void setup(Sim& sim, const V3& position) {
    sim.setupEmpty();
    sim.player = -1;
    sim.addCar(vehicleCatalog()[2], position, 0, V3(0.4, 0.5, 0.6));
    Car& car = sim.cars[0];
    car.remote = true; car.netId = 65537; car.rpm = 4500; car.throttle = 0.7; car.boost = 0.5;
}
}

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); failures += !ok; };
    const V3 listener(-40, 0.7, -40), right(0, 0, 1);
    Sim sim;
    setup(sim, V3(-40, 0.05, -35));
    Audio rightSide;
    rightSide.setEngine(0, 0);
    rightSide.setNearbyEngines(sim, listener, right, 1);
    const Energy rightEnergy = measure(rightSide);
    check("nearby remote engine is audible", rightEnergy.left + rightEnergy.right > 1);
    check("right-side source is louder in the right channel", rightEnergy.right > rightEnergy.left * 10);
    sim.cars[0].translate(V3(0, 0, -10));
    Audio leftSide;
    leftSide.setEngine(0, 0);
    leftSide.setNearbyEngines(sim, listener, right, 1);
    const Energy leftEnergy = measure(leftSide);
    check("left-side source is louder in the left channel", leftEnergy.left > leftEnergy.right * 10);
    sim.cars[0].translate(V3(0, 0, -95));
    Audio distant;
    distant.setEngine(0, 0);
    distant.setNearbyEngines(sim, listener, right, 1);
    const Energy distantEnergy = measure(distant);
    check("distance attenuates remote engines", distantEnergy.left + distantEnergy.right < (leftEnergy.left + leftEnergy.right) * 0.02);
    sim.cars.clear();
    leftSide.setNearbyEngines(sim, listener, right, 1);
    measure(leftSide);  // let a departing source fade rather than click off
    const Energy gone = measure(leftSide);
    check("departing player fades to silence", gone.left + gone.right < 1e-8);
    check("stereo mix is finite and bounded", rightEnergy.finite && leftEnergy.finite && distantEnergy.finite && gone.finite);
    bool finiteEngines = true;
    for (const VehicleSpec& v : vehicleCatalog()) {
        EngineSynth engine;
        const EngineInput input{(float)v.redline * 0.8f, 0.9f, 0.8f, v.cylinders, (int)v.induction,
                                (float)v.displacement, (float)v.redline, v.flatPlane};
        double energy = 0;
        for (int i = 0; i < 12000; i++) { const float s = engine.sample(input, 1.0f / 48000); finiteEngines &= std::isfinite(s); energy += s * s; }
        finiteEngines &= energy > 0.1;
    }
    check("shared engine synth covers all seven vehicle voices", finiteEngines);
    bool clean = true, responds = true;
    for (const VehicleSpec& v : vehicleCatalog()) {
        EngineInput input{3000, 0.05f, 0, v.cylinders, (int)v.induction, (float)v.displacement, (float)v.redline, v.flatPlane};
        const auto quiet = engineWave(input);
        input.load = 0.9f; input.boost = 0.7f;
        const auto loaded = engineWave(input);
        responds &= rms(loaded) > rms(quiet) * 1.3;
        double sum = 0, e = 0, bright = 0; float low = 0, peak = 0;
        const float a = 1 - std::exp(-2 * (float)M_PI * 4000 / 48000);
        for (float x : loaded) {
            clean &= std::isfinite(x) && std::fabs(x) < 0.8;
            sum += x; e += x * x; low += (x - low) * a; bright += (x - low) * (x - low);
            peak = std::max(peak, std::fabs(x));
        }
        clean &= std::fabs(sum / loaded.size()) < 0.002 && bright / e < 0.06;
        if (peak >= 0.8 || std::fabs(sum / loaded.size()) >= 0.002 || bright / e >= 0.06)
            std::printf("  %s peak %.4f DC %.6f brightness %.4f\n", v.id, peak, sum / loaded.size(), bright / e);
    }
    check("all engine layouts remain centered and avoid harsh high-frequency noise", clean);
    check("engine load produces a stronger combustion note for every vehicle", responds);
    EngineInput four{3000, 0.8f, 0, 4, 0, 1.6f, 6500, false};
    const auto lowRpm = engineWave(four);
    four.rpm = 6000; const auto highRpm = engineWave(four);
    check("four-cylinder firing note follows RPM (100 to 200 Hz)", tone(lowRpm, 100) > tone(lowRpm, 200) && tone(highRpm, 200) > tone(highRpm, 100) * 5);
    EngineInput eight{3000, 0.8f, 0, 8, 0, 5, 8500, false};
    const auto cross = engineWave(eight);
    eight.flatPlane = true; const auto flat = engineWave(eight);
    // This bank grouping's third cycle order is below its 200 Hz firing note.
    check("cross-plane V8 retains bank rumble distinct from flat-plane", tone(cross, 75) > tone(flat, 75) * 10);
    bool stable = true;
    for (int rate : {22050, 44100, 48000, 96000}) {
        EngineSynth engine; EngineInput input = eight; float previous = 0;
        for (int i = 0; i < rate * 2; i++) {
            input.rpm = i < rate / 2 ? 950 : i < rate ? 8200 : 0;
            input.load = i < rate ? 1 : 0;
            const float x = engine.sample(input, 1.0f / rate);
            stable &= std::isfinite(x) && std::fabs(x) < 1 && std::fabs(x - previous) < 0.6f;
            if (i > rate * 1.9) stable &= std::fabs(x) < 1e-6f;
            previous = x;
        }
    }
    check("RPM jumps and engine stalls stay smooth across device sample rates", stable);
    setup(sim, V3(-40, 0.05, -35));
    Audio exchange;
    std::atomic<bool> done{false};
    bool coherent = true;
    std::thread consumer([&] {
        while (!done.load(std::memory_order_acquire)) coherent &= AudioTest::coherent(exchange);
        coherent &= AudioTest::coherent(exchange);
    });
    for (uint32_t i = 1; i <= 10000; i++) {
        sim.cars[0].netId = i; sim.cars[0].rpm = i;
        exchange.setNearbyEngines(sim, listener, right, 1);
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    check("audio callback receives coherent frames during concurrent publication", coherent);
    recordedEngineTests(check, AudioTest::render);
    soundscapeTests(check, AudioTest::render);
    check("tire hiss stays finite and audible from 8 to 192 kHz", AudioTest::hissRates());
    Audio allocationCheck;
    allocationCheck.setEngine(3000, 0.8f);
    allocationCheck.setEnvironment(4, 8, 0.6f);
    float callbackBlock[1024];
    traceAllocations = true;
    for (int k = 0; k < 500; k++) {
        allocationCheck.setMusic(k < 50 ? 1 : 4, 0.5f);
        AudioTest::render(allocationCheck, callbackBlock, 512);
    }
    traceAllocations = false;
    check("mixer and music map changes perform no C++ allocations in the callback", callbackAllocations == 0);
    if (std::getenv("CZ_AUDIO_DEVICE_TEST")) {
        // With SDL_AUDIODRIVER=dummy this exercises SDL's real callback lifecycle silently.
        const bool initialized = SDL_Init(SDL_INIT_AUDIO) == 0;
        Audio device;
        device.setNearbyEngines(sim, listener, right, 1);
        const bool started = initialized && device.start();
        if (started) std::this_thread::sleep_for(std::chrono::milliseconds(40));
        device.stop();
        SDL_Quit();
        check("SDL callback starts, mixes nearby engines and shuts down", started);
    }
    {
        // Every engine in the garage sounds like itself: its spectrum at idle and at 3,000 rpm
        // under load (48 bands, 40 Hz to 8 kHz) differs from every other engine's, even ones
        // with the same cylinder count (exhaust, cam, intake, valvetrain, its own pulses).
        struct Engine { std::string name; EngineInput in; float idle; };
        std::vector<Engine> engines;
        for (int v = 0; v < (int)vehicleCatalog().size(); v++)
            for (int o = 0; o < (int)modOptions(v, MOD_ENGINE).size(); o++) {
                ModSet ms; ms.pick[MOD_ENGINE] = o;
                const VehicleSpec& s = tunedSpec(v, ms);
                bool seen = false;
                for (const Engine& e : engines) seen |= e.name == s.engineName;
                if (seen) continue;
                EngineInput in{0, 0, 0, s.cylinders, (int)s.induction, (float)s.displacement, (float)s.redline, s.flatPlane};
                in.voice = voiceFor(s);
                engines.push_back({s.engineName, in, (float)s.idleRpm});
            }
        const int kBands = 48;
        auto spectrum = [&](EngineInput in, float rpm, float load) {
            EngineSynth synth;
            in.rpm = rpm, in.load = load, in.boost = load;
            const int n = 48000, skip = 12000;
            std::vector<float> x(n);
            for (int i = 0; i < skip; i++) synth.sample(in, 1.0f / 48000);
            for (int i = 0; i < n; i++) x[i] = synth.sample(in, 1.0f / 48000);
            std::vector<double> band(kBands);
            double total = 1e-12;
            for (int b = 0; b < kBands; b++) {
                const double f = 40 * std::pow(200.0, (b + 0.5) / kBands), w = 2 * M_PI * f / 48000, c = 2 * std::cos(w);
                double s0 = 0, s1 = 0, s2 = 0;
                for (int i = 0; i < n; i++) s0 = x[i] + c * s1 - s2, s2 = s1, s1 = s0;
                band[b] = s1 * s1 + s2 * s2 - c * s1 * s2;
                total += band[b];
            }
            for (double& v : band) v = std::log10(v / total + 1e-9);
            return band;
        };
        std::vector<std::vector<double>> idle, load;
        for (const Engine& e : engines) idle.push_back(spectrum(e.in, e.idle, 0.05f)), load.push_back(spectrum(e.in, 3000, 0.8f));
        double closest = 1e9;
        std::string pair;
        for (size_t a = 0; a < engines.size(); a++)
            for (size_t b = a + 1; b < engines.size(); b++) {
                double d = 0;
                for (int k = 0; k < kBands; k++) d += std::fabs(idle[a][k] - idle[b][k]) + std::fabs(load[a][k] - load[b][k]);
                d /= 2 * kBands;
                if (d < closest) closest = d, pair = engines[a].name + " / " + engines[b].name;
            }
        std::printf("  %zu engines; the closest two: %s (%.3f)\n", engines.size(), pair.c_str(), closest);
        if (std::getenv("TONE_DEBUG")) {
            std::vector<std::pair<double, std::string>> all;
            std::vector<std::vector<double>> idle0, load0;
            for (const Engine& e : engines) {
                EngineInput plain = e.in;
                plain.voice = EngineTone{};
                idle0.push_back(spectrum(plain, e.idle, 0.05f)), load0.push_back(spectrum(plain, 3000, 0.8f));
            }
            double closest0 = 1e9;
            for (size_t a = 0; a < engines.size(); a++)
                for (size_t b = a + 1; b < engines.size(); b++) {
                    double d = 0, d0 = 0;
                    for (int k = 0; k < kBands; k++) d += std::fabs(idle[a][k] - idle[b][k]) + std::fabs(load[a][k] - load[b][k]),
                                                      d0 += std::fabs(idle0[a][k] - idle0[b][k]) + std::fabs(load0[a][k] - load0[b][k]);
                    all.push_back({d / (2 * kBands), engines[a].name + " / " + engines[b].name});
                    closest0 = std::min(closest0, d0 / (2 * kBands));
                }
            std::sort(all.begin(), all.end());
            for (int k = 0; k < 8; k++) std::printf("    %.3f %s\n", all[k].first, all[k].second.c_str());
            std::printf("  without the voices (cylinders, size and induction only): closest %.3f\n", closest0);
        }
        check("every engine in the garage has its own sound", engines.size() > 20 && closest > 0.05);
    }
    return failures ? 1 : 0;
}
