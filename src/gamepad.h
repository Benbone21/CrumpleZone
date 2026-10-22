// Game controller support: Xbox, PlayStation and generic pads through SDL2's GameController API.
//
// Two halves:
//   gamepad.cpp      the logic, with no SDL or GL in it (so the test runner links it): deadzones and
//                    curves, speed-sensitive steering, trigger pedals (with the keyboard's brake-to-
//                    reverse), button bindings, rumble levels, glyph names.
//   gamepad_sdl.cpp  the device: hot-plugging, polling, rumble (the game links it, the tests don't).
//
// main.cpp only talks to `Device` (raw state in) and `Logic` (intent and key presses out). A pad
// button press becomes the same key press the keyboard would give, so every action is handled by
// the one existing switch; sticks and triggers are analog and set the car directly.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Car;
union SDL_Event;

namespace pad {

// Same order as SDL_GameControllerButton for the ones we use.
enum Button { B_A, B_B, B_X, B_Y, B_BACK, B_GUIDE, B_START, B_LSTICK, B_RSTICK, B_LB, B_RB, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_COUNT };
inline uint32_t bit(Button b) { return 1u << b; }

// One poll of a controller: sticks -1..1 (+x right, +y DOWN, as SDL reports them), triggers 0..1.
struct Raw {
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    uint32_t buttons = 0;
};

// The player's settings (Settings::pad*): indices into the small tables below.
struct Tuning {
    int steering = 1;  // 0 SOFT, 1 NORMAL, 2 SHARP
    int deadzone = 1;  // 0 SMALL, 1 MEDIUM, 2 LARGE
    int rumble = 1;    // 0 OFF, 1 LOW, 2 HIGH
    float throttleScale = 1;  // the throttle limit when it applies to the pad (throttle.h): RT's throttle is scaled by it
};
extern const char* const kSteeringNames[3];
extern const char* const kDeadzoneNames[3];
extern const char* const kRumbleNames[3];
float deadzoneRadius(int index);  // radial stick deadzone (0.12 at MEDIUM)
double steerExponent(int index);  // response curve: |x|^p, flatter near the centre for SOFT
double steerGain(int index);      // how much of the tire-limited lock the stick may use

// ---- Pure shaping --------------------------------------------------------------------------
struct Stick {
    float x = 0, y = 0;
};
// Radial deadzone: inside `dz` nothing, outside it the magnitude is rescaled so the edge of the
// deadzone is 0 and the rim is 1 (direction kept, so diagonals don't snap to the axes).
Stick radialDeadzone(float x, float y, float dz);
float expoCurve(float v, double exponent);  // sign(v) * |v|^exponent
// A trigger 0..1: dead below `dead`, then a gentle curve (finer at the bottom for crawling),
// and the last few percent count as fully pressed.
float triggerShape(float raw, float dead = 0.05f, double gamma = 1.4);

// Steering. A key steers to what the tires can hold at this speed (Sim::drive); the stick
// scales that. `steerLimit` is the road-wheel angle (rad) the tires hold: full lock (0.6) when
// crawling, a few degrees at 130 km/h. `catchLock` is extra room to catch a slide.
constexpr double kMaxLock = 0.6;
double steerLimit(double wheelbase, double aLatMax, double speed, double catchLock = 0);
double steerLimit(const Car& c, double wanted = 0);  // for this car, now: grip, downforce, speed; `wanted` (+ = left) says which way a slide would be caught
// The share of full lock a full stick gives (1 at a crawl, small at speed).
double steerRatio(double limit, double gain);

struct Pedals {
    double throttle = 0, brake = 0;
    int gear = 1;
};
// RT and LT after shaping, the car's speed along its heading and its gear: throttle and brake as
// the keyboard's W and S would set them, including reverse (LT at a stop selects R and drives
// backwards; RT while rolling backwards brakes, then selects D).
Pedals pedals(double rt, double lt, double vf, int gear, bool manual = false);

// ---- What the bindings make of a pad ------------------------------------------------------
enum class Mode {  // what the pad is for right now
    Off,    // the pad does nothing (editor, chat, scenario playback)
    Drive,  // in the vehicle (or on foot)
    Menu,   // the pause menu, title screen, garage: D-pad / stick move, A select, B back
    Map,    // the map: stick pans, triggers zoom, A sets the waypoint
    Aim,    // picking a winch anchor
};

struct Tap {  // a key the pad "pressed"
    int scancode;
    bool shift;
};

struct Intent {
    bool anyInput = false;       // a button, stick or trigger is in use
    // Driving.
    double steerTarget = 0;      // -1..1 shaped stick, + = left
    bool steerStick = false;     // the stick is off centre
    double throttle = 0, brake = 0;  // 0..1 shaped triggers (zero while LB holds the winch)
    bool handbrake = false;
    bool horn = false;           // LB + R3 held
    int winch = 0;               // +1 reel in, -1 pay out (LB + RT / LT)
    int airing = 0;              // -1 let air out, +1 pump up (LB + D-pad left / right)
    // Camera and walking.
    float lookX = 0, lookY = 0;  // right stick, shaped, + = right / down
    bool lookBack = false;       // R3 held
    bool resetView = false;      // R3 let go (one frame)
    float walkForward = 0, walkTurn = 0;  // left stick (+ = forward, + = left)
    // The map.
    float mapPanX = 0, mapPanY = 0;  // stick, + = right / down
    int mapZoom = 0;                  // +1 in, -1 out (per frame held)
    bool mapSelect = false;           // A pressed: set the waypoint at the centre
    bool modifier = false;       // LB held
    // On foot.
    bool interact = false;       // X held: the hold-E job in front of you
    bool drop = false;           // B pressed: put down what you carry
    bool inventory = false;      // Y pressed (on foot): the list of what's in the truck
};

class Logic {
public:
    Tuning tune;
    Intent in;
    std::vector<Tap> taps;  // pressed since the last update(); main feeds them in as key presses

    // `buttons` includes presses that came and went inside one frame. Fills `in` and `taps`.
    void update(const Raw& raw, Mode ctx, double dt);
    // Puts the intent's steering and pedals on the player's car (call after Sim::drive; the
    // keyboard path is untouched while the pad's controls are centred). Returns whether it did.
    bool apply(Car& c, double dt, bool steeringAssist = true);
    double steerState = 0;  // the smoothed steering the car is being given (-1..1, + = left)

private:
    uint32_t prev_ = 0;
    float held_[4] = {0, 0, 0, 0};  // menu auto-repeat timers (up, down, left, right)
    bool dir_[4] = {false, false, false, false};
    bool flick_ = false;
};

// ---- Rumble --------------------------------------------------------------------------------
struct RumbleOut {
    float low = 0, high = 0;  // 0..1: the heavy (low-frequency) and light motors
};
// Rumble is an event signal, not a hum: silent at idle and cruising (road, gravel, dirt), a
// short pulse for a hit (`impactJ`: the deformation energy the car took this frame), a hard
// landing or the suspension bottoming out, a part breaking (shaft, tire, wheel, panel, engine),
// the engine on its limiter, and a sparse light tick on really rough ground only (a deadband on
// the smoothed suspension speed). `level` is Tuning::rumble (0 OFF, 1 LOW, 2 HIGH).
class Rumbler {
public:
    RumbleOut update(const Car& c, int level, double dt, double impactJ = 0);
    void reset() { *this = Rumbler(); }

private:
    struct Env {  // a pulse: held a moment at its peak, then dying away
        float v = 0;
        double hold = 0, decay = 3;
        void hit(double x, double holdFor, double decayRate) {
            if (x < v) return;
            v = (float)x, hold = std::max(hold, holdFor), decay = decayRate;
        }
        void step(double dt) {
            if (hold > 0) hold -= dt;
            else v = (float)std::max(0.0, v - decay * dt);
        }
    } low_, high_;
    void pulse(double low, double high, double holdFor, double decayRate) {
        low_.hit(low, holdFor, decayRate);
        high_.hit(high, holdFor, decayRate);
    }
    const Car* car_ = nullptr;  // ...whose state the break flags below belong to
    int build_ = -1;
    std::vector<uint8_t> flags_;  // per wheel: shaft snapped, debeaded, popped, off its hub
    int panels_ = 0, chains_ = 0;
    bool dead_ = false;
    double speed_ = 0;     // suspension speed (m/s), smoothed
    double tickWait_ = 0;  // the next rough-ground tick is allowed after this long
    double bumpWin_ = 0;   // after a bottoming-out starts, the window in which its peak is still rising
    bool armed_ = true;    // ...and the suspension has to come off its stops before the next one
    double limiter_ = 0, limitWait_ = 0;
};

// ---- Names for the HUD ---------------------------------------------------------------------
enum class Style { Xbox, PlayStation };
Style styleFromName(const std::string& deviceName);
const char* buttonName(Button b, Style s);
// What the HUD says right now: whether the pad was the last thing used, and its style.
struct Ui {
    bool last = false;
    Style style = Style::Xbox;
};
extern Ui ui;
// `keyboard` normally; the pad button's name when the pad was the last input used.
const char* hint(const char* keyboard, Button b);
// The button map for the help overlay: (button, what it does) rows.
std::vector<std::pair<std::string, std::string>> helpRows(Style s);

// ---- The device (gamepad_sdl.cpp) ------------------------------------------------------------
class Device {
public:
    ~Device() { close(); }
    // Starts the controller subsystem (controllers already plugged in arrive as ADDED events).
    bool start();
    // Takes controller events (hot-plug, buttons). Returns true when it was one; `toast` gets a
    // line for the HUD when a pad comes or goes.
    bool handleEvent(const SDL_Event& e, std::string& toast);
    // The active pad's state. Zero when none is connected, or when `focused` is false (the game's
    // window doesn't have the input focus: never act on a pad the player is using elsewhere).
    Raw read(bool focused);
    bool connected() const { return count() > 0; }
    int count() const;
    const std::string& name() const { return name_; }
    // Motors, 0..1 each, for ~150 ms (call every ~100 ms to keep it going; 0, 0 stops).
    void rumble(float low, float high);
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string name_;
    bool started_ = false;
    bool rumbling_ = false;
};

}  // namespace pad
