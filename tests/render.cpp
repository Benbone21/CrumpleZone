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
        if (!renderer.init(sim.world, error)) throw std::runtime_error(error);
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
        renderer.flushHud(960, 720);
        clean();
        std::puts("PASS physics threads menu: AUTO, all logical CPUs, saved and restored");
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
        view.eye = V3(-46, 3, -46); view.target = V3(-40, 0.7, -40); view.up = V3(0, 1, 0);
        view.width = 160; view.height = 90;
        for (int quality = 0; quality < 4; quality++) {
            applyPreset(renderer.settings, quality);
            renderer.drawScene(sim, view, DEBUG_OFF, nullptr);
            renderer.flushHud(view.width, view.height);
            clean();
            std::printf("PASS quality %d, shaders and framebuffer operations\n", quality);
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
