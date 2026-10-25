// Offline engine audition: write WAV files, never open a sound device.
#include "../src/engineaudio.h"
#include "../src/engine_samples.h"
#include "../src/mods.h"
#include "../src/vehicles.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

void u16(FILE* f, uint16_t x) { std::fputc(x & 255, f); std::fputc(x >> 8, f); }
void u32(FILE* f, uint32_t x) { u16(f, x & 65535); u16(f, x >> 16); }
bool wav(const std::string& path, const std::vector<float>& samples) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    std::fwrite("RIFF", 1, 4, f); u32(f, 36 + samples.size() * 2); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16); u16(f, 1); u16(f, 1); u32(f, 48000); u32(f, 96000); u16(f, 2); u16(f, 16);
    std::fwrite("data", 1, 4, f); u32(f, samples.size() * 2);
    for (float x : samples) u16(f, (uint16_t)(int16_t)std::lround(std::clamp(x, -1.0f, 1.0f) * 32767));
    const bool ok = !std::ferror(f); std::fclose(f); return ok;
}
int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "build/review/engine-audio";
    std::filesystem::create_directories(out);
    std::vector<float> combined;
    engineSamples::Library recordings;
    std::vector<std::string> errors;
    const int count = recordings.load(argc > 2 ? argv[2] : "assets/audio/engines", &errors);
    std::printf("Recorded banks loaded: %d; absent banks remain procedural\n", count);
    for (const auto& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
    // Every engine in the garage (each vehicle's own and its swaps), once each.
    std::vector<VehicleSpec> engines;
    for (int vi = 0; vi < (int)vehicleCatalog().size(); vi++)
        for (int o = 0; o < modOwnCount(vi, MOD_ENGINE); o++) {
            ModSet ms;
            ms.pick[MOD_ENGINE] = o;
            const VehicleSpec& s = tunedSpec(vi, ms);
            bool seen = false;
            for (const VehicleSpec& e : engines) seen |= std::string(e.engineName) == s.engineName;
            if (!seen) engines.push_back(s);
        }
    // Each engine three ways: idling (4 s), revved in neutral (three blips), and a pull through
    // the revs to the redline, flat out, then a lift (a turbo's blow-off or flutter, pops) and
    // back to idle. Boost builds and leaks as in the game (its turbo lag).
    struct Clip { const char* name; float seconds; };
    const Clip clips[] = {{"idle", 4}, {"rev", 6}, {"pull", 10}};
    FILE* index = std::fopen((out + "/index.txt").c_str(), "w");
    if (index) std::fprintf(index, "engine | kind | files (48 kHz mono): idle 4 s, rev (three blips) 6 s, pull (idle to redline flat out, lift, coast) 10 s\n");
    for (const auto& v : engines) {
        std::string name = std::string(v.id) + "-" + v.engineName;
        for (char& ch : name) ch = std::isalnum((unsigned char)ch) ? (char)std::tolower((unsigned char)ch) : '-';
        const EngineTone tone = voiceFor(v);
        std::printf("%s (%s)\n", name.c_str(), engineKindName(tone.kind));
        if (index) std::fprintf(index, "%s | %s | %s-idle.wav %s-rev.wav %s-pull.wav\n", v.engineName, engineKindName(tone.kind), name.c_str(), name.c_str(), name.c_str());
        for (const Clip& clip : clips) {
            EngineVoice synth;
            EngineInput in{(float)v.idleRpm, 0, 0, v.cylinders, (int)v.induction, (float)v.displacement, (float)v.redline, v.flatPlane};
            in.recording = (uint8_t)engineSamples::forVehicle(v);
            in.voice = tone;
            const auto* bank = recordings.bank(in.recording);
            std::vector<float> samples((size_t)(clip.seconds * 48000));
            double energy = 0, sum = 0; float peak = 0, boost = 0;
            const float idle = (float)v.idleRpm, red = (float)v.redline;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < (int)samples.size(); i++) {
                const float t = i / 48000.0f;
                float rpm = idle, load = 0.04f;
                if (clip.name[0] == 'r') {  // three blips: up fast under throttle, down slower without it
                    const float c = std::fmod(t, 2.0f);
                    if (c > 0.5f && c < 0.85f) load = 1, rpm = idle + (red * 0.85f - idle) * (c - 0.5f) / 0.35f;
                    else if (c >= 0.85f) load = 0, rpm = std::max(idle, red * 0.85f - (red * 0.85f - idle) * (c - 0.85f) / 0.9f);
                } else if (clip.name[0] == 'p') {
                    if (t >= 1 && t < 7) load = 1, rpm = idle + (red - idle) * std::pow((t - 1) / 6, 0.8f);
                    else if (t >= 7) load = 0, rpm = std::max(idle, red - (red - idle) * (t - 7) / 2.5f);
                }
                if (t > clip.seconds - 0.4f) rpm = idle, load = 0.04f;
                if (v.induction == VehicleSpec::TURBO) {  // (as Car: lags up, leaks away)
                    const float tau = load > boost ? (float)v.turboLag * std::clamp(3000 / std::max(rpm, 1.0f), 0.5f, 2.0f) : 0.25f;
                    boost += (load - boost) * std::min(1.0f, 1.0f / 48000 / std::max(tau, 1e-3f));
                } else boost = 0;
                in.rpm = rpm, in.load = load, in.boost = boost;
                samples[i] = synth.sample(in, 1.0f / 48000, bank) * 0.9f;
                peak = std::max(peak, std::fabs(samples[i])); energy += samples[i] * samples[i]; sum += samples[i];
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const std::string path = out + "/" + name + "-" + clip.name + ".wav";
            if (!wav(path, samples)) return 1;
            std::printf("  %-5s RMS %.4f peak %.4f DC %.6f CPU %.2f%% of one core\n", clip.name, std::sqrt(energy / samples.size()), peak, sum / samples.size(), seconds / clip.seconds * 100);
            if (clip.name[0] == 'p') {
                combined.insert(combined.end(), samples.begin(), samples.end());
                combined.resize(combined.size() + 24000, 0);
            }
        }
    }
    if (index) std::fclose(index);
    return wav(out + "/all-engines-pull.wav", combined) ? 0 : 1;  // (every pull, one after another)
}
