// The game's menus: the title screen (on launch), and the pause menu (ESC): resume, pick a
// car, levels, the garage, settings, controls, quit.
// Settings persist in ~/.config/crumplezone/settings.txt.
#pragma once
#include <string>
#include <vector>

#include "render.h"

struct Settings {
    bool mph = false;           // speed in mph instead of km/h
    float volume = 0.8f;        // master volume 0..1
    float music = 0.4f;         // background music, under the master volume (0 = off)
    float fovDeg = 66;          // vertical field of view of the chase and orbit cameras
    bool fullscreen = false, vsync = true, showFps = false;
    int map = 0;                // the map you were on last
    bool realRecovery = true;   // damage stays until a repair station (or arcade: T fixes it)
    int fuelMode = 2;           // 0 off, 1 real use, 2 fast (4x: so a session's driving matters)
    std::string playerName;     // shown to the others in multiplayer (default: your login)
    std::string joinAddress = "127.0.0.1";  // the last host you joined
    int pilotDest = 0;          // autopilot: which place (index into the menu's list)
    float pilotSpeed = 30;      // km/h
    int pilotRoute = 1;         // 0 roads only, 1 roads and trails, 2 anywhere
    int timeOfDay = 0;          // TimeOfDay (daylight.h): 0 the clock runs, else a fixed time
    float dayMinutes = 24;      // real minutes for a whole day when it runs
    float clockHours = 10;      // where the clock is (kept between sessions)
    RenderSettings gfx;

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
                  FREE_DRIVE, PLAY_LEVEL, EDIT_LEVEL, EDITOR, LEVEL_RESTART, LEVEL_QUIT, MAIN_MENU };
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
    bool netActive = false;
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
    };
    std::vector<LevelInfo> levels;
    int levelPick = 0;
    bool levelActive = false;  // playing one: the pause page offers restart / quit
    bool netClient = false;    // in someone else's session: the host picks the level

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
    };
    struct Hit {  // where a row was drawn (for the mouse)
        float x, y, w, h, barX, barW;
        int row;
    };
    int page = 0;  // 0 main (or the title), 1 settings, 2 multiplayer, 3 autopilot, 4 levels
    int sel = 0;
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
};
