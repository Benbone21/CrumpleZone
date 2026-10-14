// Generated WAV fixtures test the recording path. They are not shipped engine recordings.
#include "../src/audio.h"
#include "../src/mods.h"
#include "../src/vehicles.h"
#include "../src/softbody.h"
#include "../src/sdl_min.h"
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <vector>

namespace {
void le16(std::ostream& f, uint16_t n) { f.put(n & 255); f.put(n >> 8); }
void le32(std::ostream& f, uint32_t n) { le16(f, n & 65535); le16(f, n >> 16); }
void fixture(const std::filesystem::path& path, int bits, float hz, float amplitude, bool invalid = false) {
    constexpr int rate = 48000, frames = rate;
    std::ofstream f(path, std::ios::binary);
    const int align = bits / 8 * 2;
    f.write("RIFF", 4); le32(f, 36 + frames * align); f.write("WAVEfmt ", 8);
    le32(f, 16); le16(f, 1); le16(f, 2); le32(f, rate); le32(f, rate * align); le16(f, align); le16(f, bits);
    f.write("data", 4); le32(f, frames * align + (invalid ? 16 : 0));
    for (int i = 0; i < frames; i++) for (int c = 0; c < 2; c++) {
        const double x = amplitude * std::sin(2 * M_PI * hz * i / rate) + 0.02; // loader removes DC
        const int32_t n = (int32_t)std::lround(x * (bits == 16 ? 32767 : 8388607));
        if (bits == 16) le16(f, (uint16_t)n);
        else { f.put(n & 255); f.put((n >> 8) & 255); f.put((n >> 16) & 255); }
    }
}
void manifest(const std::filesystem::path& dir, const std::string& replacement = "") {
    std::ofstream f(dir / "bank.txt");
    f << "source generated-test-fixture\nauthor AudioTest\nlicense CC0-1.0\n";
    f << "loop 1500 0 1 " << (replacement.empty() ? "coast-low.wav" : replacement) << "\n"
      << "loop 3000 0 1 coast-high.wav\nloop 1500 1 1 load-low.wav\nloop 3000 1 1 load-high.wav\n";
}
std::vector<float> render(const engineSamples::Bank& bank, float rpm, float load) {
    engineSamples::Player player; EngineInput input; input.rpm = rpm; input.load = load;
    for (int i = 0; i < 24000; i++) player.sample(bank, input, 1.0f / 48000);
    std::vector<float> out(48000);
    for (float& x : out) x = player.sample(bank, input, 1.0f / 48000);
    return out;
}
double energy(const std::vector<float>& x) { double e = 0; for (float s : x) e += s * s; return e; }
double tone(const std::vector<float>& x, float hz) {
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); i++) { re += x[i] * std::cos(2 * M_PI * hz * i / 48000); im += x[i] * std::sin(2 * M_PI * hz * i / 48000); }
    return re * re + im * im;
}
}
void recordedEngineTests(const std::function<void(const char*, bool)>& check, void (*mix)(Audio&, float*, int)) {
    using namespace engineSamples;
    const auto root = std::filesystem::temp_directory_path() / ("cz-engine-samples-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto dir = root / "honda_d16a6";
    std::filesystem::create_directories(dir);
    fixture(dir / "coast-low.wav", 16, 120, 0.10);
    fixture(dir / "coast-high.wav", 24, 240, 0.10);
    fixture(dir / "load-low.wav", 24, 120, 0.25);
    fixture(dir / "load-high.wav", 16, 240, 0.25);
    manifest(dir);
    Library library; std::vector<std::string> errors;
    const int count = library.load(root.string(), &errors);
    const Bank* bank = library.bank(id("honda_d16a6"));
    check("recorded bank loads stereo PCM16/24 and records provenance", count == 1 && errors.empty() && bank && bank->loops.size() == 4 && bank->license == "CC0-1.0");
    if (bank) {
        const auto idle = render(*bank, 1500, 0), high = render(*bank, 3000, 0), power = render(*bank, 1500, 1);
        check("recording pitch follows RPM", tone(idle, 120) > tone(idle, 240) * 8 && tone(high, 240) > tone(high, 120) * 8);
        check("throttle blends actual on-load and off-load recordings", energy(power) > energy(idle) * 4);
        double dc = 0; for (float x : idle) dc += x;
        check("decoded recordings have no sustained DC", std::fabs(dc / idle.size()) < 0.002);
        Player player; EngineInput input; input.rpm = 1500; float previous = 0; bool smooth = true;
        for (int i = 0; i < 144000; i++) {
            if (i == 24000) input.rpm = 2900, input.load = 1;
            if (i == 96000) input.rpm = 0;
            const float x = player.sample(*bank, input, 1.0f / 48000);
            smooth &= std::isfinite(x) && std::fabs(x) < 1 && std::fabs(x - previous) < 0.12;
            if (i > 140000) smooth &= std::fabs(x) < 1e-6;
            previous = x;
        }
        check("recorded loop wraps, RPM transitions and stalls remain smooth", smooth);
        EngineVoice voice; EngineSynth fallback; EngineInput in; in.rpm = 2500; bool identical = true;
        for (int i = 0; i < 12000; i++) identical &= voice.sample(in, 1.0f / 48000, nullptr) == fallback.sample(in, 1.0f / 48000);
        check("missing recordings preserve the existing engine voice exactly", identical);
        bool bounded = true;
        for (int i = 0; i < 48000; i++) {
            const float x = voice.sample(in, 1.0f / 48000, i < 24000 ? bank : nullptr);
            bounded &= std::isfinite(x) && std::fabs(x) < 1;
        }
        check("switching between recordings and fallback crossfades safely", bounded);
    }
    bool mapped = true;
    for (int v = 0; v < 7; v++) {
        const auto& options = modOptions(v, MOD_ENGINE);
        for (int m = 0; m < (int)options.size(); m++) {
            ModSet mods; mods.pick[MOD_ENGINE] = m;
            const auto& spec = tunedSpec(v, mods);
            const int recording = forVehicle(spec);
            mapped &= recording > 0 && recording < kBanks;
        }
    }
    ModSet b16; b16.pick[MOD_ENGINE] = 2;
    ModSet raptor; raptor.pick[MOD_ENGINE] = 3;
    check("all catalog engines and swaps map to engine-specific recording banks", mapped && forVehicle(tunedSpec(0, b16)) == id("honda_b16a") && forVehicle(tunedSpec(2, raptor)) == id("ford_30_raptor") && forVehicle(vehicleCatalog()[3]) == id("toyota_5vz_supercharged"));
    auto capture = [&](Audio& audio) {
        std::vector<float> output(48000); float block[1024];
        for (int i = 0; i < 48; i++) mix(audio, block, 512);
        for (int i = 0; i < 48000; i += 512) {
            const int n = std::min(512, 48000 - i); mix(audio, block, n);
            for (int k = 0; k < n; k++) output[i + k] = block[2 * k + 1];
        }
        return output;
    };
    Audio local;
    const bool localLoaded = local.loadEngineRecordings(root.string()) == 1;
    local.setEngine(3000, 1); local.setRecording(id("honda_d16a6"));
    const auto localWave = capture(local);
    check("driver's audio plays its loaded recording instead of procedural firing pulses", localLoaded && tone(localWave, 240) > tone(localWave, 100) * 8);
    Sim sim; sim.setupEmpty(); sim.player = -1;
    sim.addCar(vehicleCatalog()[0], V3(-40, 0.05, -35), 0, V3(0.5, 0.5, 0.5));
    Car& remote = sim.cars[0]; remote.remote = true; remote.netId = 65537; remote.rpm = 3000; remote.throttle = 1;
    Audio nearby;
    const bool remoteLoaded = nearby.loadEngineRecordings(root.string()) == 1;
    nearby.setEngine(0, 0); nearby.setNearbyEngines(sim, V3(-40, 0.7, -40), V3(0, 0, 1), 1);
    const auto remoteWave = capture(nearby);
    check("nearby multiplayer car uses the same engine-specific recording", remoteLoaded && tone(remoteWave, 240) > tone(remoteWave, 100) * 8);
    if (std::getenv("CZ_AUDIO_DEVICE_TEST")) {
        const bool initialized = SDL_Init(SDL_INIT_AUDIO) == 0;
        Audio device;
        device.loadEngineRecordings(root.string());
        device.setEngine(3000, 1); device.setRecording(id("honda_d16a6"));
        const bool started = initialized && device.start();
        check("device startup preserves preloaded recordings and refuses live buffer replacement", started && device.recordingBankCount() == 1 && device.loadEngineRecordings(root.string()) == -1);
        device.stop(); SDL_Quit();
    }
    manifest(dir, "../not-an-engine.wav");
    errors.clear(); check("manifest rejects paths outside the bank", library.load(root.string(), &errors) == 0 && !errors.empty());
    manifest(dir, "broken.wav"); fixture(dir / "broken.wav", 16, 120, 0.1, true);
    errors.clear(); check("truncated WAV leaves the entire bank on fallback", library.load(root.string(), &errors) == 0 && !library.bank(id("honda_d16a6")) && !errors.empty());
    std::filesystem::remove_all(root);
}
