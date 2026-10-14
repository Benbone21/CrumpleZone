// Export auditions silently; no SDL device, speakers or window.
#include "../src/music.h"
#include "../src/ambience.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
void u16(FILE* f, uint16_t v) { std::fputc(v & 255, f); std::fputc(v >> 8, f); }
void u32(FILE* f, uint32_t v) { u16(f, v & 65535); u16(f, v >> 16); }
bool write(const std::string& path, const std::vector<float>& wave) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    std::fwrite("RIFF", 1, 4, f); u32(f, 36 + wave.size() * 2); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16); u16(f, 1); u16(f, 2); u32(f, 48000); u32(f, 48000 * 4); u16(f, 4); u16(f, 16);
    std::fwrite("data", 1, 4, f); u32(f, wave.size() * 2);
    double energy = 0, sum = 0; float peak = 0;
    for (float x : wave) {
        if (!std::isfinite(x)) { std::fclose(f); return false; }
        energy += x * x; sum += x; peak = std::max(peak, std::fabs(x));
        u16(f, (uint16_t)(int16_t)std::lround(std::clamp(x, -1.0f, 1.0f) * 32767));
    }
    const bool ok = !std::ferror(f); std::fclose(f);
    std::printf("%s RMS %.5f peak %.5f DC %.7f\n", path.c_str(), std::sqrt(energy / wave.size()), peak, sum / wave.size());
    return ok && peak <= 1;
}
}
int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : "build/review/soundscape";
    std::filesystem::create_directories(root);
    FILE* html = std::fopen((root + "/index.html").c_str(), "w"); if (!html) return 1;
    std::fprintf(html, "<!doctype html><meta charset='utf-8'><title>Crumple Zone audio auditions</title><style>body{max-width:850px;margin:40px auto;font:17px system-ui;background:#15201e;color:#e7eee7}audio{width:100%%}section{margin:28px 0}</style><h1>Audio auditions</h1><p>Original synthesized music and outdoor ambience. These are not real vehicle or wildlife recordings. Engine recordings remain pending. Audio does not play automatically.</p>");
    const char* names[] = {"proving-ground", "mountain", "freeway", "frontier", "backcountry"};
    for (int map = 0; map < 5; map++) {
        Music music; music.setSeed(701);
        std::vector<float> wave(45 * 48000 * 2, 0);
        const auto start = std::chrono::steady_clock::now();
        for (int at = 0; at < 45 * 48000; at += 512)
            music.render(wave.data() + at * 2, std::min(512, 45 * 48000 - at), map, 0.65f);
        const std::string name = std::string(names[map]) + "-music.wav";
        if (!write(root + "/" + name, wave)) return 1;
        std::printf("music map %d: %.2f%% of one CPU core\n", map,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 45 * 100);
        std::fprintf(html, "<section><h2>%s music</h2><audio controls preload='none' src='%s'></audio></section>", names[map], name.c_str());
    }
    for (int variant = 0; variant < 3; variant++) {
        Ambience ambience;
        const AmbienceInput input{4, variant == 1 ? 23.0f : 8.0f, 1, variant == 2 ? 0.3f : 1.0f};
        std::vector<float> wave(25 * 48000 * 2, 0);
        for (int at = 0; at < 25 * 48000; at += 512)
            ambience.render(wave.data() + at * 2, std::min(512, 25 * 48000 - at), 48000, input);
        const char* name = variant == 0 ? "forest-day.wav" : variant == 1 ? "forest-night.wav" : "forest-cockpit.wav";
        if (!write(root + "/" + name, wave)) return 1;
        std::fprintf(html, "<section><h2>%s</h2><audio controls preload='none' src='%s'></audio></section>", name, name);
    }
    std::fclose(html); return 0;
}
