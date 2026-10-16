// Renders each biome's music to a WAV, silently (no SDL device, speakers or window):
//   build/biome_preview [output directory [seconds [seed]]]
// Prints RMS, peak and the piece's key and tempo, to compare levels between biomes and with the
// maps' own pieces (default-* are the proving ground's lo-fi, the mountain's and so on).
#include "../src/music.h"
#include "../src/biome.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {
void u16(FILE* f, uint16_t v) { std::fputc(v & 255, f); std::fputc(v >> 8, f); }
void u32(FILE* f, uint32_t v) { u16(f, v & 65535); u16(f, v >> 16); }
bool write(const std::string& path, const std::vector<float>& wave, const std::string& what) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    std::fwrite("RIFF", 1, 4, f); u32(f, 36 + wave.size() * 2); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16); u16(f, 1); u16(f, 2); u32(f, 48000); u32(f, 48000 * 4); u16(f, 4); u16(f, 16);
    std::fwrite("data", 1, 4, f); u32(f, wave.size() * 2);
    double energy = 0; float peak = 0;
    for (float x : wave) {
        if (!std::isfinite(x)) { std::fclose(f); return false; }
        energy += x * x; peak = std::max(peak, std::fabs(x));
        u16(f, (uint16_t)(int16_t)std::lround(std::clamp(x, -1.0f, 1.0f) * 32767));
    }
    const bool ok = !std::ferror(f); std::fclose(f);
    std::printf("%-22s RMS %.4f peak %.3f  %s\n", path.substr(path.rfind('/') + 1).c_str(), std::sqrt(energy / wave.size()), peak, what.c_str());
    return ok && peak <= 1;
}
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : "build/review/biomes";
    const int seconds = argc > 2 ? std::max(5, std::atoi(argv[2])) : 30;
    const uint32_t seed = argc > 3 ? (uint32_t)std::atoi(argv[3]) : 701;
    std::filesystem::create_directories(root);
    struct Piece { std::string name; int map, biome; };
    std::vector<Piece> pieces;
    const char* maps[] = {"proving-ground", "mountain", "freeway", "frontier", "backcountry"};
    for (int m = 0; m < 5; m++) pieces.push_back({std::string("default-") + maps[m], m, 0});
    for (int b = 1; b < BIOME_COUNT; b++) pieces.push_back({biomeName(b), 0, b});
    for (const Piece& p : pieces) {
        Music music; music.setSeed(seed);
        std::vector<float> wave((size_t)seconds * 48000 * 2, 0);
        for (int at = 0; at < seconds * 48000; at += 512)
            music.render(wave.data() + at * 2, std::min(512, seconds * 48000 - at), p.map, 0.65f, p.biome);
        if (!write(root + "/" + p.name + ".wav", wave, music.status())) return 1;
    }
    return 0;
}
