// Offline engine audition: write WAV files, never open a sound device.
#include "../src/engineaudio.h"
#include "../src/engine_samples.h"
#include "../src/vehicles.h"
#include <algorithm>
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
    for (const auto& v : vehicleCatalog()) {
        EngineVoice synth;
        EngineInput in{(float)v.idleRpm, 0, 0, v.cylinders, (int)v.induction, (float)v.displacement, (float)v.redline, v.flatPlane};
        in.recording = (uint8_t)engineSamples::forVehicle(v);
        in.voice = voiceFor(v);
        const auto* bank = recordings.bank(in.recording);
        std::vector<float> samples(12 * 48000);
        double energy = 0, sum = 0; float peak = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < (int)samples.size(); i++) {
            const float t = i / 48000.0f;
            if (t < 2) { in.rpm = v.idleRpm; in.load = 0.05f; }
            else if (t < 7) { in.rpm = v.idleRpm + (v.redline * 0.95f - v.idleRpm) * (t - 2) / 5; in.load = 0.85f; }
            else if (t < 9) { in.rpm = v.redline * (0.6f + 0.12f * (t - 7)); in.load = t < 7.15f ? 0 : 0.8f; }
            else if (t < 11) { in.rpm = v.idleRpm + (v.redline * 0.84f - v.idleRpm) * (11 - t) / 2; in.load = 0; }
            else { in.rpm = 0; in.load = 0; }
            in.boost = in.load;
            samples[i] = std::tanh(synth.sample(in, 1.0f / 48000, bank) * 1.2f) * 0.8f;
            peak = std::max(peak, std::fabs(samples[i])); energy += samples[i] * samples[i]; sum += samples[i];
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const std::string path = out + "/" + v.id + ".wav";
        std::printf("%s: %s\n", v.id, bank ? engineSamples::key(in.recording) : "PROCEDURAL FALLBACK");
        if (!wav(path, samples)) return 1;
        std::printf("%s: RMS %.4f peak %.4f DC %.6f CPU %.2f%% of one core\n", v.id, std::sqrt(energy / samples.size()), peak, sum / samples.size(), seconds / 12 * 100);
        combined.insert(combined.end(), samples.begin(), samples.end());
        combined.resize(combined.size() + 24000, 0);
    }
    return wav(out + "/all-vehicles.wav", combined) ? 0 : 1;
}
