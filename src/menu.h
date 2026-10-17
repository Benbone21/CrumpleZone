// The game's menus: the title screen (on launch), and the pause menu (ESC): resume, pick a
// car, levels, the garage, settings, controls, quit.
// Settings persist in ~/.config/crumplezone/settings.txt.
#pragma once
#include <string>
#include <vector>

#include "render.h"
#include "keybinds.h"
#include "sdl_min.h"

struct Settings {
    float ambience = 0.6f;      // outdoor wind, foliage, birds and night insects
    bool mph = false;           // speed in mph instead of km/h
    float volume = 0.8f;        // master volume 0..1
    float music = 0.4f;         // background music, under the master volume (0 = off)
    float fovDeg = 66;          // vertical field of view of the chase and orbit cameras
    bool fullscreen = false, vsync = true, showFps = false;
    int physicsThreads = 0;     // 0 AUTO; 1 serial; otherwise up to the available logical CPUs
    bool steeringAssist = true; // limit steering angle at high speeds (false = simulation mode)
    int map = 0;                // the map you were on last
    bool realRecovery = true;   // damage stays until a repair station (or arcade: T fixes it)
    bool career = false;        // career (start stock, find, win and buy parts) or sandbox (every part)
    double winchSpeed = 4.0;    // saved gameplay speed multiplier; 1x is rated line speed
    int fuelMode = 2;           // 0 off, 1 real use, 2 fast (4x: so a session's driving matters)
    std::string playerName;     // shown to the others in multiplayer (default: your login)
    std::string joinAddress;    // the last address you typed to join
    std::string sessionName;    // the game you host, as others' lists show it ("" = "<NAME>'S GAME")
    std::string netPassword;    // asked of whoever joins your game, and offered to the one you join ("" = none)
    int maxPlayers = 8;         // the most in a game you host (2..8)
    int hostPort = 27015;       // the port you host on, TCP and UDP (1024..65535; net::kDefaultPort)
    int pilotDest = 0;          // autopilot: which place (index into the menu's list)
    float pilotSpeed = 30;      // km/h
    int pilotRoute = 1;         // 0 roads only, 1 roads and trails, 2 anywhere
    int timeOfDay = 0;          // TimeOfDay (daylight.h): 0 the clock runs, else a fixed time
    float dayMinutes = 24;      // real minutes for a whole day when it runs
    float clockHours = 10;      // where the clock is (kept between sessions)
    // Game controller (gamepad.h): indices into pad::kSteeringNames / kDeadzoneNames / kRumbleNames.
    int padSteering = 1, padDeadzone = 1, padRumble = 1;
    RenderSettings gfx;
    bool adaptiveQuality = true;  // auto-scale quality to maintain framerate
    Keybinds keys;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    static std::string defaultPath();
};

// Graphics quality presets: LOW, MEDIUM, HIGH, ULTRA. presetOf() returns 4 (CUSTOM) when
// the options don't match one.
void applyPreset(RenderSettings& g, int preset);
int presetOf(const RenderSettings& g);

class PauseMenu {
public:
    enum Action { NONE, RESUME, DRIVE, GARAGE, CONTROLS, RESET, QUIT, CHANGED, LOAD_MAP, HOST, JOIN, LEAVE, PILOT_GO, PILOT_STOP,
                  FREE_DRIVE, PLAY_LEVEL, EDIT_LEVEL, EDITOR, LEVEL_RESTART, LEVEL_QUIT, MAIN_MENU, JOB_START, JOB_STOP, MEET_UP,
                  CANCEL_JOIN, MODEL_LAB, NEW_SECTION, EDITOR_BACK };
    struct Result {
        Action action = NONE;
        int vehicle = 0;  // for DRIVE; the map for LOAD_MAP
    };

    bool open = false;
    bool title = false;  // the title screen (nothing to resume yet)
    int vehicle = 0;  // the car shown in the VEHICLE row
    int mapPick = 0;  // the map shown in the MAP row
    // Multiplayer page: the session's state, set by the game before drawing.
    std::string netStatus = "OFFLINE";
    // Multiplayer page, set by the game before drawing.
    // JOIN: games found on the local network (net::Finder), a row each.
    struct FoundGame {
        std::string session{}, host{}, map{}, players{}, address{};
        bool ok = true;   // can be joined (not full, same version)
        std::string why{};  // when it can't
    };
    std::vector<FoundGame> found;
    double searching = 0;                 // s the list has been looking (for the "can't see it?" help)
    int sessionPort = 0;                  // hosting: the port it's on (for what to tell friends)
    int joinPort = 0;                     // joining: the port being tried (for the failed card's help)
    std::string joinPick;                 // set when one is picked (JOIN then goes there)
    std::string joinLabel;                // what JOIN connects to (for the connecting card)
    // HOST: this machine's addresses (to give a friend on another network).
    std::vector<std::string> localIps;
    // In a session: who's in it (a line each), and whether we host it.
    std::vector<std::string> sessionLines;
    std::string sessionTitle;
    bool netHost = false;
    // Joining (connecting, or it just failed: joinError).
    bool joining = false;
    std::string joinError;
    int pageNow() const { return page; }
    bool listeningForKey() const { return open && listening >= 0; }  // the next key press is a binding, not a command
    bool netActive = false;
    bool editorSuspended = false;  // a level from the editor is being tried: BACK TO THE EDITOR is offered
    std::string padName;  // the connected controller (settings page), set by the game
    bool shift = false;  // a shift key is down (typing ':' in an address)
    void showMultiplayer(int currentVehicle, int currentMap);  // opens straight onto that page
    void showPage(int page, int currentVehicle, int currentMap);  // (2 multiplayer, 3 autopilot)
    // Autopilot page: the places it can drive to (set by the game), and its state.
    std::vector<std::string> pilotPlaces;
    std::string pilotStatus = "OFF";
    bool pilotActive = false;
    // Levels page: the list (set by the game), and the one being played.
    struct LevelInfo {
        std::string name, about;
        std::vector<std::string> lines;  // mode, map, rules, medals, best
        bool section = false;            // a hand-made obstacle section (the levels page can show just those)
    };
    std::vector<LevelInfo> levels;
    int levelPick = 0;
    bool levelSections = false;  // the levels page shows only the obstacle sections
    std::vector<int> visibleLevels() const;  // indices into `levels` the page shows
    int shownLevel() const;                  // the one picked, or the first showing (-1: none)
    bool levelActive = false;  // playing one: the pause page offers restart / quit
    bool netClient = false;    // in someone else's session: the host picks the level

    // Jobs page: the jobs on offer here (set by the game), and the one under way.
    std::vector<LevelInfo> jobs;
    int jobPick = 0;
    bool jobActive = false;
    std::string jobStatus;

    void showTitle(int currentVehicle, int currentMap);

    void show(int currentVehicle, int currentMap);
    void close() { open = false; dragSlider = -1; }
    Result key(int scancode, Settings& s);
    // Mouse, in drawable pixels.
    Result mouseMove(float x, float y, bool held, Settings& s);
    Result mouseDown(float x, float y, Settings& s);
    void mouseUp() { dragSlider = -1; }
    void draw(Renderer& r, const Settings& s, int W, int H, double dt);

private:
    enum Kind { HEADER, BUTTON, CHOICE, SLIDER, TEXT };
    struct Row {
        Kind kind;
        std::string label, value, hint;
        int id = 0;           // which option or action
        int count = 0;        // CHOICE: how many values; SLIDER: steps
        float frac = 0;       // SLIDER: position 0..1
        int col = 0;          // multiplayer page: 0 across the top/bottom, 1 the HOST card, 2 the JOIN card
        bool dim = false;     // shown but can't be picked (a full game)
        std::string placeholder{};  // TEXT: shown greyed while it's empty
    };
    struct Hit {  // where a row was drawn (for the mouse)
        float x, y, w, h, barX, barW;
        int row;
    };
    int listening = -1;  // keybinds page: the KeyAction waiting for a key press (-1: none)
    std::string keyNote; // keybinds page: why the last key was refused
    int page = 0;  // 0 main (or the title), 1 settings, 2 multiplayer, 3 autopilot, 4 levels, 5 jobs, 6 controller, 7 keybinds
    int sel = 0;
    std::string portText;      // the HOST card's PORT field while it's being typed in (it can be a half-typed number)
    bool portEdited = false, portFresh = false;  // (fresh: untouched, the first digit replaces it)
    int dragSlider = -1;
    float fade = 0;
    float caret = 0;  // text-field cursor blink
    std::vector<Hit> hits;

    std::vector<Row> rows(const Settings& s) const;
    Result activate(int row, Settings& s);
    Result change(int row, int dir, Settings& s);
    Result setSlider(int row, float frac, Settings& s);
    void moveSel(int dir, const Settings& s);
    int hitAt(float x, float y) const;
    // Drawing pieces: one row in a box at (x, w); the multiplayer page's cards; the main
    // menu's own background (no world behind it).
    struct RowStyle {
        float ts, small, u, rowH, headH, a, valueW;
    };
    float drawRow(Renderer& r, const Row& row, int k, float x, float w, float cy, const RowStyle& st);
    void drawMulti(Renderer& r, const Settings& s, int W, int H, float a);
    void drawBackdrop(Renderer& r, int W, int H, float a) const;
    float spin = 0;  // the connecting card's spinner
};
