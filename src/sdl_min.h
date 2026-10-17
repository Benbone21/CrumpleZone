// Minimal SDL2 declarations so the game builds without libsdl2-dev.
// Only the stable SDL2 ABI subset we use is declared; we link straight
// against the SDL2 runtime library (libSDL2-2.0.so.0).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

extern "C" {
typedef struct SDL_Window SDL_Window;
typedef void* SDL_GLContext;
typedef uint32_t SDL_AudioDeviceID;
typedef void (*SDL_AudioCallback)(void* userdata, uint8_t* stream, int len);

struct SDL_AudioSpec {
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint16_t padding;
    uint32_t size;
    SDL_AudioCallback callback;
    void* userdata;
};

union SDL_Event {
    uint32_t type;
    uint8_t padding[56];
};

int SDL_Init(uint32_t flags);
void SDL_Quit(void);
const char* SDL_GetError(void);
SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags);
void SDL_DestroyWindow(SDL_Window* window);
int SDL_SetWindowFullscreen(SDL_Window* window, uint32_t flags);
uint32_t SDL_GetWindowFlags(SDL_Window* window);
int SDL_GL_SetAttribute(int attr, int value);
SDL_GLContext SDL_GL_CreateContext(SDL_Window* window);
void SDL_GL_DeleteContext(SDL_GLContext context);
int SDL_GL_SetSwapInterval(int interval);
void SDL_GL_SwapWindow(SDL_Window* window);
void* SDL_GL_GetProcAddress(const char* proc);
void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h);
void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
int SDL_PollEvent(SDL_Event* event);
int SDL_PushEvent(SDL_Event* event);
int SDL_SetHintWithPriority(const char* name, const char* value, int priority);  // (priority 0: a default, the player's environment variables still win)
void SDL_StartTextInput(void);  // typed text arrives as SDL_TEXTINPUT
void SDL_StopTextInput(void);
char* SDL_GetClipboardText(void);  // free with SDL_free
int SDL_SetClipboardText(const char* text);
void SDL_free(void* mem);
int SDL_SetRelativeMouseMode(int enabled);  // SDL_bool: the mouse turns the view, no cursor
const uint8_t* SDL_GetKeyboardState(int* numkeys);
uint64_t SDL_GetPerformanceCounter(void);
uint64_t SDL_GetPerformanceFrequency(void);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device, int iscapture, const SDL_AudioSpec* desired,
                                      SDL_AudioSpec* obtained, int allowed_changes);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
int SDL_ShowSimpleMessageBox(uint32_t flags, const char* title, const char* message, SDL_Window* window);

// Game controllers (SDL_gamecontroller.h / SDL_joystick.h). Handles are opaque.
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;
typedef int32_t SDL_JoystickID;
int SDL_InitSubSystem(uint32_t flags);
int SDL_NumJoysticks(void);
int SDL_IsGameController(int joystick_index);  // SDL_bool
SDL_GameController* SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController* gamecontroller);
const char* SDL_GameControllerName(SDL_GameController* gamecontroller);
SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* gamecontroller);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* joystick);
int16_t SDL_GameControllerGetAxis(SDL_GameController* gamecontroller, int axis);        // SDL_GameControllerAxis
uint8_t SDL_GameControllerGetButton(SDL_GameController* gamecontroller, int button);     // SDL_GameControllerButton
int SDL_GameControllerRumble(SDL_GameController* gamecontroller, uint16_t low_frequency_rumble, uint16_t high_frequency_rumble,
                             uint32_t duration_ms);  // SDL 2.0.9+
}

enum : uint32_t {
    SDL_INIT_AUDIO = 0x10,
    SDL_INIT_VIDEO = 0x20,
    SDL_INIT_EVENTS = 0x4000,
    SDL_INIT_JOYSTICK = 0x200,
    SDL_INIT_GAMECONTROLLER = 0x2000,

    SDL_WINDOW_OPENGL = 0x2,
    SDL_WINDOW_INPUT_FOCUS = 0x200,
    SDL_WINDOW_RESIZABLE = 0x20,
    SDL_WINDOW_ALLOW_HIGHDPI = 0x2000,
    SDL_WINDOW_FULLSCREEN_DESKTOP = 0x1001,
    SDL_WINDOWPOS_CENTERED = 0x2FFF0000u,

    SDL_QUIT = 0x100,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP = 0x301,
    SDL_TEXTINPUT = 0x303,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP = 0x402,
    SDL_MOUSEWHEEL = 0x403,
    SDL_CONTROLLERAXISMOTION = 0x650,
    SDL_CONTROLLERBUTTONDOWN = 0x651,
    SDL_CONTROLLERBUTTONUP = 0x652,
    SDL_CONTROLLERDEVICEADDED = 0x653,
    SDL_CONTROLLERDEVICEREMOVED = 0x654,
    SDL_CONTROLLERDEVICEREMAPPED = 0x655,

    SDL_MESSAGEBOX_ERROR = 0x10,
    AUDIO_F32SYS = 0x8120,
};

enum {
    SDL_GL_RED_SIZE = 0,
    SDL_GL_GREEN_SIZE = 1,
    SDL_GL_BLUE_SIZE = 2,
    SDL_GL_DOUBLEBUFFER = 5,
    SDL_GL_DEPTH_SIZE = 6,
    SDL_GL_MULTISAMPLEBUFFERS = 13,
    SDL_GL_MULTISAMPLESAMPLES = 14,
    SDL_GL_CONTEXT_MAJOR_VERSION = 17,
    SDL_GL_CONTEXT_MINOR_VERSION = 18,
    SDL_GL_CONTEXT_FLAGS = 20,
    SDL_GL_CONTEXT_PROFILE_MASK = 21,
    SDL_GL_CONTEXT_PROFILE_CORE = 1,
    SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG = 2,
};

// SDL_GameControllerAxis / SDL_GameControllerButton values.
enum {
    SDL_CONTROLLER_AXIS_LEFTX = 0, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_BUTTON_A = 0, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
};

// Scancodes (USB HID usage ids, stable across SDL2 versions).
enum {
    SC_A = 4, SC_C = 6, SC_D = 7, SC_H = 11, SC_I = 12, SC_J = 13, SC_K = 14, SC_N = 17, SC_P = 19, SC_R = 21, SC_S = 22, SC_T = 23, SC_U = 24, SC_W = 26, SC_X = 27,
    SC_1 = 30, SC_2 = 31, SC_3 = 32, SC_4 = 33, SC_5 = 34, SC_6 = 35, SC_7 = 36, SC_BACKSPACE = 42, SC_DELETE = 76,
    SC_ESCAPE = 41, SC_TAB = 43, SC_SPACE = 44, SC_LEFTBRACKET = 47, SC_RIGHTBRACKET = 48,
    SC_F1 = 58, SC_F2 = 59, SC_F3 = 60, SC_F4 = 61, SC_F5 = 62, SC_F6 = 63, SC_F7 = 64, SC_F8 = 65, SC_F9 = 66, SC_F10 = 67, SC_F11 = 68, SC_F12 = 69, SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82,
    SC_B = 5, SC_E = 8, SC_F = 9, SC_G = 10, SC_L = 15, SC_M = 16, SC_Q = 20, SC_RETURN = 40, SC_KP_ENTER = 88,
    SC_INSERT = 73, SC_HOME = 74, SC_END = 77, SC_MINUS = 45, SC_EQUALS = 46, SC_O = 18, SC_V = 25, SC_Y = 28, SC_Z = 29, SC_8 = 37, SC_9 = 38, SC_0 = 39, SC_LSHIFT = 225, SC_RSHIFT = 229,
};

// Field accessors for the SDL_Event variants we read (offsets from SDL2's SDL_events.h).
inline int32_t evI32(const SDL_Event& e, int off) { int32_t v; std::memcpy(&v, e.padding + off, 4); return v; }
inline uint32_t evU32(const SDL_Event& e, int off) { uint32_t v; std::memcpy(&v, e.padding + off, 4); return v; }
inline int evKeyScancode(const SDL_Event& e) { return evI32(e, 16); }
inline bool evKeyRepeat(const SDL_Event& e) { return e.padding[13] != 0; }
inline uint32_t evMotionState(const SDL_Event& e) { return evU32(e, 16); }
inline int evMotionXrel(const SDL_Event& e) { return evI32(e, 28); }
inline int evMotionYrel(const SDL_Event& e) { return evI32(e, 32); }
inline int evWheelY(const SDL_Event& e) { return evI32(e, 20); }
inline int evMotionX(const SDL_Event& e) { return evI32(e, 20); }
inline int evMotionY(const SDL_Event& e) { return evI32(e, 24); }
inline std::string evText(const SDL_Event& e) {  // SDL_TEXTINPUT: up to 32 bytes of UTF-8 at 12
    char t[33];
    std::memcpy(t, e.padding + 12, 32);
    t[32] = 0;
    return t;
}
inline int evButton(const SDL_Event& e) { return e.padding[16]; }  // 1 left, 2 middle, 3 right
inline int evButtonX(const SDL_Event& e) { return evI32(e, 20); }
inline int evButtonY(const SDL_Event& e) { return evI32(e, 24); }

// Controller events (SDL_ControllerAxisEvent / ButtonEvent / DeviceEvent): type, timestamp, then
// `which` at 8 (the instance id; for DEVICEADDED the joystick index), axis or button at 12.
inline int32_t evCtrlWhich(const SDL_Event& e) { return evI32(e, 8); }
inline int evCtrlAxis(const SDL_Event& e) { return e.padding[12]; }
inline int evCtrlAxisValue(const SDL_Event& e) { int16_t v; std::memcpy(&v, e.padding + 16, 2); return v; }
inline int evCtrlButton(const SDL_Event& e) { return e.padding[12]; }
