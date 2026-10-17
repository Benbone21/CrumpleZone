// Offscreen OpenGL integration checks, including an emulated driver rejecting HDR MSAA.
#include "../src/render.h"
#include "../src/gpu.h"
#include "../src/menu.h"
#include "../src/sdl_min.h"
#include <cstdio>
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
