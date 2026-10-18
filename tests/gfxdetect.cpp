#include "../src/perf.h"
#include "harness.h"
#include <limits>

SCENARIO(gfxClassify, "engine/gfx-detect-table", "GL_RENDERER strings map to the right first preset") {
    struct Row { const char* vendor; const char* renderer; int preset; };
    const Row rows[] = {
        {"NVIDIA Corporation", "NVIDIA GeForce RTX 4070/PCIe/SSE2", Q_HIGH},
        {"NVIDIA Corporation", "NVIDIA GeForce GTX 1060 6GB/PCIe/SSE2", Q_HIGH},
        {"NVIDIA Corporation", "NVIDIA GeForce MX150/PCIe/SSE2", Q_LOW},
        {"ATI Technologies Inc.", "AMD Radeon RX 6700 XT", Q_HIGH},
        {"AMD", "AMD Radeon RX 7800 XT (radeonsi, navi32, LLVM 17.0.6, DRM 3.57)", Q_HIGH},
        {"AMD", "AMD Radeon(TM) Graphics", Q_LOW},
        {"AMD", "AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 17.0.6, DRM 3.57)", Q_LOW},
        {"ATI Technologies Inc.", "AMD Radeon(TM) Vega 8 Graphics", Q_LOW},
        {"ATI Technologies Inc.", "AMD Radeon 780M Graphics", Q_MEDIUM},
        {"Intel", "Intel(R) UHD Graphics 620", Q_LOW},
        {"Intel", "Intel(R) Iris(R) Xe Graphics", Q_MEDIUM},
        {"Intel", "Intel(R) Arc(tm) A770 Graphics", Q_HIGH},
        {"Intel", "Intel(R) Arc(tm) Graphics", Q_MEDIUM},
        {"Intel", "Mesa Intel(R) HD Graphics 530 (SKL GT2)", Q_LOW},
        {"Apple", "Apple M1", Q_LOW},
        {"Apple", "Apple M2 Max", Q_HIGH},
        {"Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)", Q_POTATO},
        {"Microsoft Corporation", "GDI Generic", Q_POTATO},
        {"Microsoft", "Microsoft Basic Render Driver", Q_POTATO},
        {"Google Inc.", "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero)), SwiftShader driver)", Q_POTATO},
        {"Mystery", "Unheard Of 9000", Q_MEDIUM},
    };
    for (const Row& r : rows)
        ctx.check(std::string(r.renderer) + " -> " + presetName(r.preset), presetForRenderer(r.vendor, r.renderer) == r.preset);
}

SCENARIO(gfxGuess, "engine/gfx-detect-guess", "VRAM, cores and RAM only lower the guess; a saved choice is never overridden") {
    HardwareInfo hw;
    hw.vendor = "NVIDIA Corporation", hw.renderer = "NVIDIA GeForce RTX 3060", hw.vramMb = 12288, hw.cores = 12, hw.ramMb = 32768;
    ctx.check("discrete card with room: HIGH", guessPreset(hw) == Q_HIGH);
    hw.vramMb = 3072;
    ctx.check("3 GB of VRAM: MEDIUM", guessPreset(hw) == Q_MEDIUM);
    hw.vramMb = 1024;
    ctx.check("1 GB of VRAM: LOW", guessPreset(hw) == Q_LOW);
    hw.vramMb = -1, hw.ramMb = 6000;
    ctx.check("6 GB of RAM: MEDIUM", guessPreset(hw) == Q_MEDIUM);
    hw.ramMb = 32768, hw.cores = 2;
    ctx.check("2 cores: LOW", guessPreset(hw) == Q_LOW);
    hw.vendor = "Mesa", hw.renderer = "llvmpipe (LLVM 15.0.7, 256 bits)", hw.cores = 32;
    ctx.check("software stays POTATO", guessPreset(hw) == Q_POTATO);
    hw.vendor = "Intel", hw.renderer = "Intel(R) Iris(R) Xe Graphics";
    ctx.check("hardware is never guessed up to ULTRA", guessPreset(hw) != Q_ULTRA);
    ctx.check("no settings file: detect", shouldAutoDetect(false, false) && shouldAutoDetect(false, true));
    ctx.check("file without a quality chosen: detect", shouldAutoDetect(true, false));
    ctx.check("a saved choice is never overridden", !shouldAutoDetect(true, true));
}

namespace {
// Runs the probe on a machine whose frame time at each preset is ms[k] (ladder order POTATO, LOW, MEDIUM, HIGH, ULTRA).
int probeOn(int guess, const float ms[5], float target, int* rounds = nullptr) {
    QualityProbe p;
    p.start(guess, target);
    const int ladder[5] = {Q_POTATO, Q_LOW, Q_MEDIUM, Q_HIGH, Q_ULTRA};
    int changes = 0;
    for (int i = 0; i < 20000 && p.active(); i++) {
        int k = 0;
        for (int j = 0; j < 5; j++)
            if (ladder[j] == p.preset()) k = j;
        const QualityProbe::Step s = p.feed(i % 50 == 0 ? 400.0f : ms[k]);  // (a hitch now and then: ignored)
        if (s != QualityProbe::NONE) changes++;
    }
    if (rounds) *rounds = changes;
    return p.active() ? -1 : p.preset();
}
}  // namespace

SCENARIO(gfxProbe, "engine/gfx-detect-probe", "The warm-up measurement steps the preset toward the FPS target") {
    const float slowGpu[5] = {12, 22, 30, 50, 90}, fastGpu[5] = {3, 4, 6, 8, 20}, midGpu[5] = {6, 8, 11, 22, 40};
    ctx.check("a HIGH guess on a slow machine steps down to what holds 60", probeOn(Q_HIGH, slowGpu, 60) == Q_POTATO);
    ctx.check("MEDIUM guess that already holds 60 stays", probeOn(Q_MEDIUM, midGpu, 60) == Q_MEDIUM);
    ctx.check("a LOW guess with lots of room steps up to HIGH, not ULTRA", probeOn(Q_LOW, fastGpu, 60) == Q_HIGH);
    ctx.check("a step up that is too slow is undone", probeOn(Q_LOW, (const float[5]){3, 5, 9, 25, 40}, 60) == Q_MEDIUM);
    ctx.check("a 30 fps target keeps a heavier preset", probeOn(Q_HIGH, slowGpu, 30) == Q_MEDIUM || probeOn(Q_HIGH, slowGpu, 30) == Q_LOW);
    ctx.check("120 fps target steps down from a 60 fps machine", probeOn(Q_HIGH, midGpu, 120) == Q_LOW);
    int rounds = 0;
    probeOn(Q_HIGH, slowGpu, 60, &rounds);
    ctx.check("it finishes within the round limit", rounds <= QualityProbe::kMaxRounds);
    QualityProbe p;
    p.start(Q_HIGH);
    for (int i = 0; i < 300; i++) p.feed(std::numeric_limits<float>::quiet_NaN());
    ctx.check("garbage timings change nothing", p.active() && p.preset() == Q_HIGH);
}

#include "../src/watermesh.h"

SCENARIO(waterBudget, "render/water-mesh-budget", "The water surface stays within its triangle budget") {
    // (Before the simplification: 41468 on the frontier, 12138 on the backcountry, 4608 a lake on the other maps.)
    struct Row { int map; size_t budget; };
    for (const Row& r : {Row{MAP_FRONTIER, 12200}, Row{MAP_BACKCOUNTRY, 12200}, Row{MAP_PROVING_GROUND, 600}, Row{MAP_MOUNTAIN, 200}}) {
        World w(r.map);
        std::vector<Vtx> v;
        buildWaterMesh(w, v);
        ctx.check(std::string(mapName(r.map)) + ": " + std::to_string(v.size() / 3) + " triangles, budget " + std::to_string(r.budget),
                  v.size() % 3 == 0 && v.size() / 3 <= r.budget);
        bool flat = true;
        for (const Vtx& x : v) flat &= std::isfinite(x.p[1]) && x.n[1] == 1;
        ctx.check(std::string(mapName(r.map)) + ": every corner finite, facing up", flat);
    }
}
