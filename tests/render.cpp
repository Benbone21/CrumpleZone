// Offscreen OpenGL integration checks, including an emulated driver rejecting HDR MSAA.
#include "../src/render.h"
#include "../src/menu.h"
#include "../src/sdl_min.h"
#include <cstdio>
#include <stdexcept>

namespace {
gl::PFN_CheckFramebufferStatus originalCheck;
void (*attachment)(GLenum, GLenum, GLenum, GLint*);
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
