// Offscreen OpenGL integration checks, including an emulated driver rejecting HDR MSAA.
#include <cstring>
#include "../src/render.h"
#include "../src/carparts.h"
#include "../src/carvisual.h"
#include "../src/gpu.h"
#include "../src/menu.h"
#include "../src/sdl_min.h"
#include "../src/procstream.h"
#include "../src/procworld.h"
#include <algorithm>
#include <vector>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <stdexcept>

namespace {
gl::PFN_CheckFramebufferStatus originalCheck;
void (*attachment)(GLenum, GLenum, GLenum, GLint*);
const char* extension = nullptr;
int anisotropyReads = 0;
void extensionCount(GLenum, GLint* count) { *count = 1; }
const GLubyte* extensionName(GLenum, GLuint) { return (const GLubyte*)extension; }
void anisotropyLimit(GLenum, GLfloat* value) { anisotropyReads++; *value = 16; }
const GLubyte* noExtensions(GLenum, GLuint) { return (const GLubyte*)""; }
bool rejectMsaa = false;
int rejected = 0;
GLenum checkFramebuffer(GLenum target) {
    const GLenum status = originalCheck(target);
    if (rejectMsaa && status == GL_FRAMEBUFFER_COMPLETE) {
        GLint type = 0;
        attachment(target, GL_COLOR_ATTACHMENT0, 0x8CD0 /* OBJECT_TYPE */, &type);
        if (type == (GLint)GL_RENDERBUFFER) { rejected++; return 0x8D56; }
    }
    return status;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("Render check: creating OpenGL context");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::puts(SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    SDL_Window* window = SDL_CreateWindow("render check", 0, 0, 160, 90, SDL_WINDOW_OPENGL);
    if (!window) { std::puts(SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    int result = 0;
    try {
        if (!context) throw std::runtime_error(SDL_GetError());
        if (const char* missing = gl::load(SDL_GL_GetProcAddress)) throw std::runtime_error(missing);
        std::printf("Render check: %s / %s\n", gl::GetString(GL_VENDOR), gl::GetString(GL_RENDERER));
        auto getError = reinterpret_cast<GLenum(*)()>(SDL_GL_GetProcAddress("glGetError"));
        attachment = reinterpret_cast<decltype(attachment)>(SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv"));
        if (!getError || !attachment) throw std::runtime_error("missing core GL test function");
        auto clean = [&] {
            const GLenum error = getError();
            if (error) { char s[64]; std::snprintf(s, sizeof s, "OpenGL error 0x%x", error); throw std::runtime_error(s); }
        };
        struct GpuCase { const char* vendor; const char* renderer; GpuVendor expected; };
        const GpuCase gpuCases[] = {
            {nullptr, nullptr, GpuVendor::Unknown},
            {"NVIDIA Corporation", "GeForce RTX 4090", GpuVendor::NVIDIA},
            {"ATI Technologies Inc.", "AMD Radeon RX 7900", GpuVendor::AMD},
            {"Apple", "AMD Radeon Pro 5500M OpenGL Engine", GpuVendor::AMD},
            {"Apple", "Intel Iris Pro OpenGL Engine", GpuVendor::Intel},
            {"Apple", "Apple M4", GpuVendor::Apple},
            {"Mesa", "INTEL UHD Graphics", GpuVendor::Intel},
            {"Mesa", "llvmpipe (LLVM 18.1, 256 bits)", GpuVendor::Mesa},
            {"VMware, Inc.", "SVGA3D virtualization", GpuVendor::Unknown},
            {nullptr, "nvidia", GpuVendor::NVIDIA},
        };
        for (const auto& c : gpuCases)
            if (detectGpuVendor(c.vendor, c.renderer) != c.expected)
                throw std::runtime_error("GPU diagnostic misidentified the renderer");
        std::puts("PASS GPU diagnostics: discrete, integrated, Apple, software and unknown renderers");
        const auto originalInteger = gl::GetIntegerv;
        const auto originalStringi = gl::GetStringi;
        const auto originalFloat = gl::GetFloatv;
        gl::GetIntegerv = extensionCount;
        gl::GetStringi = extensionName;
        gl::GetFloatv = anisotropyLimit;
        bool filteringOk = gl::maxTextureAnisotropy() == 1 && anisotropyReads == 0;
        extension = "GL_EXT_texture_filter_anisotropic_suffix";
        filteringOk &= gl::maxTextureAnisotropy() == 1 && anisotropyReads == 0;
        extension = "GL_EXT_texture_filter_anisotropic";
        filteringOk &= gl::maxTextureAnisotropy() == 16 && anisotropyReads == 1;
        extension = "GL_ARB_texture_filter_anisotropic";
        filteringOk &= gl::maxTextureAnisotropy() == 16 && anisotropyReads == 2;
        gl::GetIntegerv = originalInteger;
        gl::GetStringi = originalStringi;
        gl::GetFloatv = originalFloat;
        if (!filteringOk) throw std::runtime_error("optional anisotropy queried without support");
        clean();
        std::puts("PASS optional anisotropy: EXT, ARB and unsupported drivers");
        originalCheck = gl::CheckFramebufferStatus;
        gl::CheckFramebufferStatus = checkFramebuffer;
        Sim sim;
        sim.setupEmpty();
        sim.addCar(vehicleCatalog()[0], V3(-40, 0.05, -40), 0, V3(0.4, 0.5, 0.6));
        Renderer renderer;
        std::string error;
        std::puts("Render check: initializing renderer");
        if (!renderer.init(sim.world, error)) throw std::runtime_error(error);
        std::puts("Render check: renderer initialized");
        clean();
        {  // A map switch without a frozen frame: made on a worker, uploaded in slices, built a stage a call; no call is a hitch.
            World next(MAP_MOUNTAIN);
            std::shared_ptr<Renderer::Prepared> prep;
            std::thread([&] { prep = Renderer::prepareWorld(next); }).join();
            renderer.beginUpload(prep);
            prep.reset();
            auto ms = [](std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); };
            double worst = 0;
            int calls = 0;
            for (bool done = false; !done; calls++) {
                const auto t0 = std::chrono::steady_clock::now();
                done = renderer.pumpUpload(3.0);
                worst = std::max(worst, ms(t0));
                if (calls > 100000) throw std::runtime_error("the terrain upload never finishes");
            }
            clean();
            const auto t1 = std::chrono::steady_clock::now();
            renderer.adoptWorld(next);
            double adopt = ms(t1), worstStep = 0;
            int steps = 0;
            for (bool done = false; !done; steps++) {
                const auto t2 = std::chrono::steady_clock::now();
                done = renderer.stepWorld(next);
                worstStep = std::max(worstStep, ms(t2));
            }
            clean();
            std::printf("Render check: map switch in %d upload slices (worst %.1f ms), adopt %.1f ms, %d stages (worst %.1f ms)\n", calls, worst, adopt, steps, worstStep);
            // (A software rasterizer on a shared CI runner gets twice the room: the limits are for real GPUs.)
            const char* gr = (const char*)gl::GetString(GL_RENDERER);
            // (Apple Software Renderer on a hosted Mac runner swung 470-800 ms for the worst stage: four times.)
            const double room = gr && std::strstr(gr, "Software Renderer") ? 4.0 : gr && (std::strstr(gr, "llvmpipe") || std::strstr(gr, "softpipe") || std::strstr(gr, "SwiftShader")) ? 2.0 : 1.0;
            if (worst > 25 * room || adopt > 60 * room || worstStep > 400 * room) throw std::runtime_error("a map switch step took long enough to hitch a frame");
            renderer.setWorld(sim.world);  // (the first world again: `next` goes out of scope)
            clean();
            std::puts("PASS map switch: terrain uploaded in slices, scenery built a stage at a time, none long enough to hitch");
        }
        // Exercise the actual menu through keyboard input, plus disk persistence.
        Settings settings;
        PauseMenu menu;
        menu.showPage(1, 0, 0);
        bool foundThreads = false;
        for (int row = 0; row < 64; row++) {
            const int previous = settings.physicsThreads;
            menu.key(SC_RIGHT, settings);
            if (settings.physicsThreads != previous) { foundThreads = true; break; }
            menu.key(SC_DOWN, settings);
        }
        if (!foundThreads) throw std::runtime_error("physics thread setting cannot be reached");
        settings.physicsThreads = 0;
        menu.key(SC_LEFT, settings);
        if (settings.physicsThreads != physicsThreadLimit()) throw std::runtime_error("menu cannot select all CPU threads");
        const char* settingsPath = "build/test-thread-settings.txt";
        Settings loaded;
        if (!settings.save(settingsPath) || !loaded.load(settingsPath) || loaded.physicsThreads != settings.physicsThreads)
            throw std::runtime_error("thread budget did not persist");
        std::remove(settingsPath);
        menu.key(SC_RIGHT, settings);
        if (settings.physicsThreads != 0) throw std::runtime_error("menu cannot select automatic threads");
        menu.draw(renderer, settings, 960, 720, 0.1);
        std::puts("Render check: drawing settings HUD");
        renderer.flushHud(960, 720);
        clean();
        std::puts("PASS physics threads menu: AUTO, all logical CPUs, saved and restored");
        PauseMenu adaptiveMenu;
        adaptiveMenu.showPage(1, 0, 0);
        bool foundAdaptive = false;
        for (int row = 0; row < 64; row++) {
            const bool before = settings.adaptiveQuality;
            const bool fullscreen = settings.fullscreen;
            adaptiveMenu.key(SC_RIGHT, settings);
            if (settings.adaptiveQuality != before) {
                if (settings.fullscreen != fullscreen) throw std::runtime_error("adaptive control changed fullscreen");
                foundAdaptive = true; break;
            }
            adaptiveMenu.key(SC_DOWN, settings);
        }
        if (!foundAdaptive) throw std::runtime_error("adaptive setting cannot be reached");
        settings.adaptiveQuality = false;
        if (!settings.save(settingsPath) || !loaded.load(settingsPath) || loaded.adaptiveQuality)
            throw std::runtime_error("adaptive toggle did not persist");
        std::remove(settingsPath);
        std::puts("PASS adaptive menu: independent toggle and persistence");
        {  // the multiplayer page's PORT field: digits only, saved, and the default shown
            Settings ps;
            if (ps.hostPort != 27015) throw std::runtime_error("host port should default to 27015");
            PauseMenu portMenu;
            portMenu.showMultiplayer(0, 0);
            bool onPort = false;
            for (int row = 0; row < 12 && !onPort; row++) {  // (down through the cards, typing 28123 on each row: only PORT takes it)
                portMenu.textInput("28123", ps);
                if (ps.hostPort == 28123) onPort = true;
                else portMenu.key(SC_DOWN, ps);
            }
            if (!onPort) throw std::runtime_error("the PORT row cannot be reached, or typing in it does not reach the setting");
            portMenu.textInput("a", ps);  // letters ignored
            portMenu.key(SC_BACKSPACE, ps);
            portMenu.key(SC_BACKSPACE, ps);
            portMenu.key(SC_BACKSPACE, ps);  // "2812" is a port; "281" and "28" are below 1024: the setting stays the last good one
            if (ps.hostPort != 2812) throw std::runtime_error("a half-typed port must not change the setting");
            portMenu.textInput("999", ps);  // "28999"
            if (ps.hostPort != 28999) throw std::runtime_error("port retyped");
            if (!ps.save(settingsPath) || !loaded.load(settingsPath) || loaded.hostPort != 28999) throw std::runtime_error("host port did not persist");
            std::remove(settingsPath);
            if (FILE* f = std::fopen(settingsPath, "w")) {
                std::fputs("hostport 80\n", f);
                std::fclose(f);
            }
            Settings low;
            low.hostPort = 31000;
            low.load(settingsPath);
            if (low.hostPort != 27015) throw std::runtime_error("an out-of-range hostport should fall back to the default");
            if (FILE* f = std::fopen(settingsPath, "w")) {
                std::fputs("hostport 70000\nhostport abc\n", f);
                std::fclose(f);
            }
            low.hostPort = 31000;
            low.load(settingsPath);
            if (low.hostPort != 27015) throw std::runtime_error("a garbage hostport should fall back to the default");
            std::remove(settingsPath);
            std::puts("PASS multiplayer menu: PORT field digits only, saved, out-of-range falls back to 27015");
        }
        PauseMenu ambientMenu;
        ambientMenu.showPage(1, 0, 0);
        bool foundAmbience = false;
        for (int row = 0; row < 64; row++) {
            const float previous = settings.ambience;
            ambientMenu.key(SC_RIGHT, settings);
            if (settings.ambience != previous) { foundAmbience = true; break; }
            ambientMenu.key(SC_DOWN, settings);
        }
        if (!foundAmbience) throw std::runtime_error("ambience slider cannot be reached");
        for (int k = 0; k < 24; k++) ambientMenu.key(SC_LEFT, settings);
        if (settings.ambience != 0) throw std::runtime_error("ambience slider cannot mute nature");
        settings.ambience = 0.65f;
        if (!settings.save(settingsPath) || !loaded.load(settingsPath) || std::fabs(loaded.ambience - 0.65f) > 0.001f)
            throw std::runtime_error("ambience level did not persist");
        std::remove(settingsPath);
        std::puts("PASS ambience menu: reachable, muted and saved/restored");
        View view;
        renderer.daylight = computeDaylight(10);
        view.eye = V3(-46, 3, -46); view.target = V3(-40, 0.7, -40); view.up = V3(0, 1, 0);
        view.width = 160; view.height = 90;
        sim.cars[0].tirePsi = 12;
        for (int quality = 0; quality < 4; quality++) {
            for (Wheel& wheel : sim.cars[0].wheels) { wheel.angle = quality * 0.7; wheel.load = 3000; }
            std::printf("Render check: drawing quality %d\n", quality);
            applyPreset(renderer.settings, quality);
            renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
            const auto geometry = renderer.geometryStats();
            if (!geometry.terrainTriangles || geometry.terrainTriangles >= geometry.terrainFullTriangles / 2 ||
                geometry.terrainVisibleChunks >= geometry.terrainChunks)
                throw std::runtime_error("terrain culling/LOD did not reduce submitted geometry");
            std::printf("Terrain: %u/%u triangles, %u/%u chunks; scenery %u vertices, shadows %u\n",
                        geometry.terrainTriangles, geometry.terrainFullTriangles,
                        geometry.terrainVisibleChunks, geometry.terrainChunks,
                        geometry.sceneryVertices, geometry.shadowSceneryVertices);
            renderer.flushHud(view.width, view.height);
            clean();
            std::printf("PASS quality %d, shaders and framebuffer operations\n", quality);
        }
        sim.cars[0].tirePsi = sim.cars[0].spec->tirePsi;
        const View groundView = view;
        view.eye = V3(0, 10000, 0); view.target = V3(0, 11000, 0); view.up = V3(0, 0, 1);
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        if (renderer.geometryStats().terrainTriangles || renderer.geometryStats().sceneryVertices)
            throw std::runtime_error("camera facing away still submits world geometry");
        view.eye = V3(-100, 3003, -100); view.target = V3(-88, 3000, -88); view.up = V3(0, 1, 0);
        view.fovy = 0.1f;
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        if (renderer.geometryStats().terrainTriangles)
            throw std::runtime_error("high camera should not see unedited ground");
        std::vector<std::pair<int, float>> oldHeights;
        for (int i = 508; i <= 516; ++i)
            for (int j = 508; j <= 516; ++j) {
                const int id = i * sim.world.gridSize() + j;
                oldHeights.push_back({id, sim.world.cellHeight(id)});
                sim.world.setCellHeight(id, 3000);
            }
        renderer.updateTerrainRegion(sim.world, 508, 508, 516, 516);
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        if (!renderer.geometryStats().terrainTriangles)
            throw std::runtime_error("raised editor terrain was culled with stale height bounds");
        for (const auto& h : oldHeights) sim.world.setCellHeight(h.first, h.second);
        renderer.updateTerrainRegion(sim.world, 508, 508, 516, 516);
        view = groundView;
        renderer.updateTerrainRegion(sim.world, 500, 500, 520, 520);
        renderer.updateObjects(sim.world);
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        if (!renderer.geometryStats().terrainTriangles)
            throw std::runtime_error("editor updates lost terrain batches");
        std::puts("PASS culling rejects world behind camera; editor updates retain terrain and scenery batches");
        {  // Night, MSAA: thin leaves, needles and blades only partly over a pixel must not shade to NaN (it blooms into coloured sparks).
            const View keep = view;
            const int msaa = renderer.settings.msaa;
            renderer.daylight = computeDaylight(23);
            renderer.settings.msaa = 4;
            view.width = 480; view.height = 270;
            int bad = 0;
            for (int k = 0; k < 8; k++) {
                const double a = k * M_PI / 4;
                view.eye = V3(40, sim.world.height(40, 40) + 1.6, 40);
                view.target = view.eye + V3(std::cos(a) * 10, -1.5, std::sin(a) * 10);
                renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
                clean();
                bad += renderer.badScenePixels();
            }
            std::printf("Render check: night scene, %d bad HDR pixels\n", bad);
            if (bad) throw std::runtime_error("NaN, infinite or negative pixels in the night scene");
            renderer.daylight = computeDaylight(10);
            renderer.settings.msaa = msaa;
            view = keep;
            std::puts("PASS night scene under MSAA: no NaN or negative pixels to bloom");
        }
        // Render photo ground with extension support hidden, to exercise actual texture setup.
        gl::GetStringi = noExtensions;
        GroundTextures fallbackGround;
        std::string fallbackError;
        const bool fallbackLoaded = loadGroundTextures(fallbackGround, 512, fallbackError);
        freeGroundTextures(fallbackGround);
        renderer.settings.ground = 1;
        view.width = 159;
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        gl::GetStringi = originalStringi;
        clean();
        if (!fallbackLoaded) throw std::runtime_error(fallbackError);
        std::puts("PASS photo ground without optional anisotropic filtering");
        rejectMsaa = true;
        view.width = 158; view.height = 88;
        renderer.settings.msaa = 4;
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        if (!rejected) throw std::runtime_error("MSAA rejection was not exercised");
        const int tries = rejected;
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        if (rejected != tries) throw std::runtime_error("unsupported MSAA retried every frame");
        std::puts("PASS unsupported HDR MSAA falls back and stays cached");
        renderer.settings.renderScale = 0.5f;
        renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
        clean();
        std::puts("PASS resizing and render scale after MSAA fallback");
        {  // Vehicles (Renderer::CarSlot): kept skins, merged parts, far copies, culling; draws and triangles per car
            rejectMsaa = false;
            blockOnTemplates = true;  // (the car drawn from its first frame, not when a worker has made its mesh)
            renderer.settings.renderScale = 1.0f;
            renderer.settings.msaa = 0;
            view.width = 1280, view.height = 720;
            sim.cars.clear();
            sim.addCar(vehicleCatalog()[7], V3(-40, 0.05, -40), 0, V3(0.6, 0.3, 0.1));
            sim.player = 0;
            for (int k = 0; k < 120; k++) sim.substep();  // (settled on its wheels)
            Car& car = sim.cars[0];
            auto look = [&](double d, bool once = false) {
                const V3 c = car.frame().com;
                view.target = c, view.eye = c + V3(d * 0.7, d * 0.25, d * 0.7), view.up = V3(0, 1, 0);
                renderer.carModels.assign(1, Renderer::CarModels());
                renderer.carModels[0].car = 0;
                if (d < 40) carPartModels(car, renderer.carModels[0].parts, d < 20 ? PARTS_ALL : PARTS_BIG);
                renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
                if (!once) renderer.drawScene(sim, view, DEBUG_OFF, nullptr);  // (the second frame: what it costs from then on)
                clean();
                return renderer.geometryStats();
            };
            struct Budget { double d; unsigned draws, tris, shadowTris; };
            // (draws: skin, bound parts, running gear, parts, four tires, rims and calipers, the spare)
            const Budget budgets[] = {{8, 22, 70000, 110000}, {60, 16, 13000, 24000}, {200, 16, 13000, 24000}};
            for (const Budget& b : budgets) {
                const Renderer::GeometryStats g = look(b.d);
                std::printf("Render check: a Blazer %.0f m off: %u draws, %u triangles; shadows %u draws, %u triangles\n", b.d, g.carDraws, g.carTris,
                            g.carShadowDraws, g.carShadowTris);
                if (g.carDraws > b.draws || g.carTris > b.tris || g.carShadowTris > b.shadowTris)
                    throw std::runtime_error("a vehicle " + std::to_string((int)b.d) + " m off is over its draw or triangle budget");
            }
            const Renderer::GeometryStats near = look(8), far = look(200);
            if (far.carTris * 4 > near.carTris) throw std::runtime_error("a vehicle far off should draw under a quarter of the triangles it does close up");
            if (look(8, true).carRebuilds) throw std::runtime_error("a vehicle standing still was built again");
            car.translate(V3(1.5, 0, 0));
            if (look(8, true).carRebuilds) throw std::runtime_error("a vehicle moved whole (not bent) was built again");
            car.nodes[car.quads[car.quads.size() / 2].v[0]].p += V3(0, 0.05, 0);  // a dent
            if (!look(8, true).carRebuilds) throw std::runtime_error("a dent didn't show the frame it happened");
            view.target = car.frame().com + V3(0, 0, 0), view.eye = view.target + V3(0, 3, 30);  // (looking at the car...)
            renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
            auto drawn = [&] { const auto& g = renderer.geometryStats(); return g.carKindTris[0] + g.carKindTris[2] + g.carKindTris[4]; };  // (skin, bound parts, wheels)
            const unsigned seen = drawn();
            view.target = view.eye + V3(0, 0, 10);  // (...and away from it)
            renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
            if (!seen || drawn()) throw std::runtime_error("a vehicle behind the camera was drawn");
            // What each vehicle more costs the CPU when nothing about it changed: 1 against 9 standing.
            auto cpu = [&](int n) {
                sim.cars.resize(1);
                for (int k = 1; k < n; k++) sim.addCar(vehicleCatalog()[k % 2 ? 2 : 6], V3(-40 + 5 * k, 0.05, -30), 0, V3(0.3, 0.3, 0.3));
                renderer.carModels.clear();
                view.target = V3(-20, 1, -35), view.eye = V3(-20, 8, 0);
                double best = 1e9;
                for (int f = 0; f < 12; f++) {
                    renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
                    if (f >= 2) best = std::min(best, renderer.geometryStats().carPrepMs);
                }
                return best;
            };
            const double one = cpu(1), nine = cpu(9), each = (nine - one) / 8;
            std::printf("Render check: car meshes %.3f ms CPU for one, %.3f ms for nine: %.3f ms each more\n", one, nine, each);
            if (each > 0.25) throw std::runtime_error("each vehicle standing costs the CPU over 0.25 ms a frame");
            clean();
            std::puts("PASS vehicles: per-car draw and triangle budgets near and far, kept meshes, dents shown at once, culled, cheap to add");
        }
        {
            // The procedural world's window sliding (procstream.h): no frame from the build's start to after the swap costs the game
            // thread over twice the median frame (plus 2 ms), and the swap frame's own work is a few ms.
            World::procSeed = 4242, World::procOriginSet = false;
            sim.cars.resize(1);
            sim.world = World(MAP_PROCEDURAL);
            proc::setActiveWindow(sim.world);
            applyPreset(renderer.settings, 2);
            renderer.setWorld(sim.world);
            View pv;
            pv.eye = sim.world.spawn + V3(-20, 12, -20), pv.target = sim.world.spawn, pv.up = V3(0, 1, 0), pv.width = 160, pv.height = 90;
            ProcStream ps;
            // (Apple's software renderer, on the hosted Macs, draws this 60k-tree scene at seconds a frame: the 160 calm frames and up to
            // 3 x 4000 sliding ones took over 6 minutes of a 40-minute budget and the run timed out. Frame times there say nothing about a
            // GPU anyway, so it only runs the slide itself - the build, the staged upload, the swap and its cost - and doesn't draw or time frames.)
            const char* slideGr = (const char*)gl::GetString(GL_RENDERER);
            const bool drawFrames = !(slideGr && std::strstr(slideGr, "Software Renderer"));
            // (Each frame timed from an idle GPU: a GPU-bound machine's queue filling up stalls the CPU every few frames, slide or not.)
            auto finish = reinterpret_cast<void (*)()>(SDL_GL_GetProcAddress("glFinish"));
            auto frame = [&](bool slide) {
                if (finish) finish();
                const auto t0 = std::chrono::steady_clock::now();
                V3 d;
                if (slide && ps.tick(sim, renderer, V3(), false, d)) pv.eye += d, pv.target += d;
                if (drawFrames) renderer.drawScene(sim, pv, DEBUG_OFF, nullptr);
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            };
            std::vector<double> calm;
            double calmWorst = 0;
            for (int f = 0; f < (drawFrames ? 160 : 12); f++) {
                const double ms = frame(false);
                if (f >= 10) calm.push_back(ms), calmWorst = std::max(calmWorst, ms);
                if (f >= 10 && std::getenv("CZ_SLIDE_LOG") && ms > 11) std::printf("  calm frame %d: %.2f ms\n", f, ms);
            }
            std::printf("Render check: procedural, no slide: worst of 150 frames %.2f ms\n", calmWorst);
            std::vector<double> sorted = calm;
            std::sort(sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            // (Up to three slides: on a busy machine other work lands in some frames; the slide's own cost shows in the best of them.)
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok; attempt++) {
                const int before = ps.swaps;
                ps.want(sim.world.proc.seed, sim.world.proc.ox + 64, sim.world.proc.oz + 128);
                double worst = 0;
                int frames = 0;
                std::vector<double> sliding;
                for (; frames < 4000 && ps.swaps == before; frames++) {
                    const double ms = frame(true);
                    if (ms > 1.5 * median && std::getenv("CZ_SLIDE_LOG")) std::printf("  slide frame %d: %.2f ms\n", frames, ms);
                    worst = std::max(worst, ms), sliding.push_back(ms);
                    if (ps.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));  // (the worker's build goes on meanwhile)
                }
                for (int f = 0; f < 3; f++) {
                    const double ms = frame(true);
                    if (std::getenv("CZ_SLIDE_LOG")) std::printf("  after the swap %d: %.2f ms\n", f, ms);
                    worst = std::max(worst, ms), sliding.push_back(ms);
                }
                std::sort(sliding.begin(), sliding.end());
                const double p95 = sliding[sliding.size() * 95 / 100];
                std::printf("Render check: procedural slide %d: median frame %.2f ms CPU (worst with no slide %.2f); over %d frames of build, upload and swap: 95%% under "
                            "%.2f ms, worst %.2f ms; the swap's own work %.2f ms\n", attempt + 1, median, calmWorst, frames, p95, worst, ps.lastSwapMs);
                if (ps.swaps != before + 1) throw std::runtime_error("the procedural window didn't slide");
                if (ps.lastSwapMs > (drawFrames ? 2.0 : 20.0)) throw std::runtime_error("the procedural slide's swap did more than swap");
                ok = !drawFrames || p95 <= 2 * median + 2.0;
            }
            if (!ok) throw std::runtime_error("the procedural slide's frames cost over twice the median (three tries)");
            clean();
            std::puts("PASS procedural slide: staged a piece a frame, swapped at no cost");
        }
        {
            // Far trees (treedraw.h pictures, farforest.h): looking out over the procedural world, the trees past 200 m are pictures
            // (a couple of triangles each, not a mesh), and past the window's trees the far forest draws to the fog; the frame with them
            // costs about what it did with meshes and bare far land. (Apple's software renderer takes seconds a frame: there the frames
            // are drawn to check what's drawn, not timed.)
            const char* gr = (const char*)gl::GetString(GL_RENDERER);
            const bool timeFrames = !(gr && std::strstr(gr, "Software Renderer"));
            View fv;
            fv.eye = sim.world.spawn + V3(0, 60, 0), fv.target = sim.world.spawn + V3(1000, 20, 400), fv.up = V3(0, 1, 0), fv.width = 160, fv.height = 90;
            auto finish = reinterpret_cast<void (*)()>(SDL_GL_GetProcAddress("glFinish"));
            auto frames = [&](bool on, int n) {
                renderer.treePictures = renderer.farForestOn = on;
                std::vector<double> t;
                for (int f = 0; f < n; f++) {
                    if (finish) finish();
                    const auto t0 = std::chrono::steady_clock::now();
                    renderer.drawScene(sim, fv, DEBUG_OFF, nullptr);
                    if (finish) finish();
                    t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                }
                std::sort(t.begin(), t.end());
                return t[t.size() / 2];
            };
            const int n = timeFrames ? 40 : 3;
            frames(true, 3);
            const double on = frames(true, n);
            const VegStats vs = renderer.vegStats();
            const int tiles = renderer.farForestTiles();
            const double off = frames(false, n);
            const VegStats vsOff = renderer.vegStats();
            renderer.treePictures = renderer.farForestOn = true;
            std::printf("Render check: far trees: %d trees drawn (%lldk triangles) with pictures, %d (%lldk) as meshes; far forest %d tiles; median frame %.2f ms "
                        "with, %.2f without%s\n", vs.trees.inst, vs.trees.tris / 1000, vsOff.trees.inst, vsOff.trees.tris / 1000, tiles, on, off,
                        timeFrames ? "" : " (not timed: a software renderer)");
            if (tiles <= 0) throw std::runtime_error("the far forest drew nothing looking out over the land");
            if (vsOff.trees.inst > 0 && vs.trees.tris >= vsOff.trees.tris) throw std::runtime_error("the far trees as pictures didn't cut the trees' triangles");
            if (timeFrames && on > 1.5 * off + 1.0) throw std::runtime_error("the far trees cost the frame over half as much again");
            clean();
            std::puts("PASS far trees: pictures past the meshes, the far forest to the fog, the frame no dearer");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL render: %s\n", error.what());
        result = 1;
    }
    gl::CheckFramebufferStatus = originalCheck;
    if (context) SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
