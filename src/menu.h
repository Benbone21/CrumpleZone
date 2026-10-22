// The game's menus: the title screen (on launch), and the pause menu (ESC): resume, pick a
// car, levels, the garage, settings, controls, quit.
// Settings persist in ~/.config/crumplezone/settings.txt.
#pragma once
#include <string>
#include "textedit.h"
#include <vector>

#include "render.h"
#include "perf.h"
#include "devmenu.h"
#include "keybinds.h"
#include "throttle.h"
#include "sdl_min.h"

struct Settings {
    float ambience = 0.6f;      // outdoor wind, foliage, birds and night insects
    bool mph = false;           // speed in mph instead of km/h
    float volume = 0.8f;        // master volume 0..1
    float music = 0.4f;         // background music, under the master volume (0 = off)
    float fovDeg = 66;          // vertical field of view of the chase and orbit cameras
    bool fullscreen = false, vsync = true, showFps = false;
    bool fullHud = false;       // SHOW FULL HUD: always (false: contextual, the HUD shows what matters now; hudcontext.h)
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
    std::string joinPassword;   // offered to the game you join (kept in settings.txt, as plain text, only with rememberPassword)
    bool rememberPassword = false;
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
    bool padInvertLookX = false, padInvertLookY = false;  // invert right-stick look axes
    RenderSettings gfx;
    bool adaptiveQuality = true;  // auto-scale quality to maintain framerate
    int fpsTarget = kFpsMatchDisplay;  // kFpsTargets: adaptive quality's aim and, VSync off, the frame cap (match display, or 0: uncapped, aiming for 60)
    bool gfxChosen = false;       // a quality has been chosen (by the player or the first-launch detection); a saved file without it counts as chosen
    bool devHotkeys = false;      // the debug and test keys (1-7, I, K, N, DEL, [ ], F2, F3, F5) work directly; OFF: only through the dev menu (F12)
    Keybinds keys;
    throttle::Tuning throttle;  // the throttle limit (throttle.h): the keyboard's cap, its ramp, whether the pad obeys it

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    static std::string defaultPath();
};

// (The graphics quality presets: perf.h.)

class PauseMenu {
public:
    enum Action { NONE, RESUME, DRIVE, GARAGE, CONTROLS, RESET, QUIT, CHANGED, LOAD_MAP, HOST, JOIN, LEAVE, PILOT_GO, PILOT_STOP,
                  DETECT_GFX, FREE_DRIVE, PLAY_LEVEL, EDIT_LEVEL, EDITOR, LEVEL_RESTART, LEVEL_QUIT, MAIN_MENU, JOB_START, JOB_STOP, MEET_UP,
                  CANCEL_JOIN, MODEL_LAB, NEW_SECTION, EDITOR_BACK, DEV_RUN };
    struct Result {
        Action action = NONE;
        int vehicle = 0;  // for DRIVE; the map for LOAD_MAP; DEV_RUN: the devmenu::Act
        int arg = 0;      // DEV_RUN: its argument
    };

    bool open = false;
    int selected() const { return sel; }  // the focused row (menu sounds)
    bool overRow(float x, float y) const { return hitAt(x, y) >= 0; }
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
    std::string joinAddr;                 // the address being tried (RETRY goes back to it)
    bool joinNeedsPw = false;             // the failed card asks for the password (none, or the wrong one)
    bool showPw = false;                  // the join password shows as typed, not as stars
    void askPassword(const Settings& s);  // joining failed over the password: the card asks again, cursor in the field
    Result textInput(const char* utf8, Settings& s);  // typed text (a password: any case, any symbol)
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
    bool shift = false;  // a shift key is down (extending a selection)
    bool ctrl = false;   // a control key is down (Ctrl+V pastes into the field)
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
    void showTitlePage(int page, int currentVehicle, int currentMap);  // a page (settings, multiplayer, levels) of the title screen: BACK returns to its page 0
    bool sceneBehind = false;  // the title is drawn over the main menu's camp scene (no gradient backdrop, the scene shows through)
    // The dev menu (devmenu.h): pages 8 (the groups) and 9 (one group's rows). The game fills the lists before drawing.
    void showDev(int currentVehicle, int currentMap, bool fromPause = false);  // opens straight onto the groups
    bool devOpen() const { return open && page >= 8; }
    bool onPausePage() const { return open && !title && page == 0; }
    std::vector<std::string> devSections;  // the map's obstacle sections (TELEPORT)
    std::vector<std::string> devPlaces;    // and its places
    bool devScenario = false;              // a scenario is playing (its checks list is offered)

    void show(int currentVehicle, int currentMap);
    void close() { open = false; dragSlider = -1; }
    Result key(int scancode, Settings& s);
    // Mouse, in drawable pixels.
    Result mouseMove(float x, float y, bool held, Settings& s);
    Result mouseDown(float x, float y, Settings& s);
    void mouseUp() { dragSlider = -1; }
    void wheel(int dir, const Settings& s) { moveSel(dir, s); }  // (the mouse wheel: up the rows)
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
    int scroll = 0;                      // keybinds page: the first row showing
    int devGroup = 0;                    // dev page 9: which group
    int devSection = 0, devPlace = 0;    // the TELEPORT page's pickers
    bool devFromPause = false;           // the dev menu was opened from the pause menu's row (BACK returns there)
    std::vector<devmenu::Item> devItems(const Keybinds& kb) const;  // the rows of the group shown (the scenario one only while a scenario plays)
    int listening = -1;  // keybinds page: the KeyAction waiting for a key press (-1: none)
    std::string keyNote; // keybinds page: why the last key was refused
    int page = 0;  // 0 main (or the title), 1 settings, 2 multiplayer, 3 autopilot, 4 levels, 5 jobs, 6 controller, 7 keybinds, 8 dev menu, 9 one dev group
    int sel = 0;
    std::string portText;      // the HOST card's PORT field while it's being typed in (it can be a half-typed number)
    bool portEdited = false, portFresh = false;  // (fresh: untouched, the first digit replaces it)
    te::Edit ted;              // the cursor and selection in the text field that has focus
    int tedRow = -1;           // ...which field that is (a row id)
    bool textField(int id, Settings& s, std::string*& v, te::Rules& r);  // the text a field row edits, and what it takes
    void focusText(int id, Settings& s);                                 // a new field gets the cursor at its end (the port: its number selected)
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
