// The world editor: fly over a map, sculpt and paint its ground, flood hollows, place rocks,
// trees, logs, ramps and repair shops, lay out a course (start and gates), set the rules, and
// save it as a level (.czl) to play, alone or with friends.
//
// The working state (and undo) is EditCore (editcore.h); this is its camera, mouse and
// keyboard, and what's drawn over the view. The game hands it the events while it's open.
#pragma once
#include <string>
#include <vector>

#include "editcore.h"
#include "render.h"
#include "sdl_min.h"

class Editor {
public:
    enum Tool { SCULPT, PAINT, WATER, OBJECTS, COURSE, SETTINGS, TOOL_COUNT };
    bool active = false;
    EditCore core;
    // Set when the player asks to try the level (the game plays core.level, then may come back).
    bool wantPlay = false;
    // Set when the editor is closed (the game puts the player back on the world as edited).
    bool wantClose = false;

    // Starts editing `lv`. The world must be the plain map (World(lv.map)).
    void open(World& w, Renderer& r, const Level& lv);
    void close() { active = false; }
    // An SDL event (mouse in window coordinates; `scale` turns them into drawable pixels).
    void handleEvent(const SDL_Event& e, float scale);
    // Every frame: flies the camera, keeps brushing while the button's held, and catches the
    // renderer up with what changed.
    void update(double dt, const uint8_t* keys, Renderer& r, int W, int H);
    View view(int W, int H) const;
    void draw(Renderer& r, int W, int H, double dt);

private:
    // Camera.
    V3 eye;
    double yaw = 0, pitch = -0.5;  // yaw: 0 = looking +x (the car convention); pitch down < 0
    int W = 1, H = 1;
    float mx = 0, my = 0;          // mouse (drawable pixels)
    bool lmb = false, rmb = false;
    bool overUi = false;           // the mouse is over a panel (clicks go to it)
    bool haveHit = false;
    V3 hit;                        // where the mouse meets the ground
    EditPick hover;                // what's under the mouse
    // Tools.
    Tool tool = SCULPT;
    BrushOp op = BRUSH_RAISE;
    double radius = 8, strength = 0.5, flatY = 0;
    bool flatSet = false;
    int material = 0;
    bool softPaint = true;
    double floodDepth = 1.0;
    int objKind = OBJ_ROCK;
    double rockSize = 1.2, treeHeight = 10, boxLen = 6, boxWidth = 4, boxHeight = 0.6, boxPitch = 0, objYaw = 0, logR = 0.3, logLen = 6;
    int treeKind = 0;
    bool slab = false;
    uint32_t nextSeed = 1;
    double gateWidth = 14;
    EditPick sel;                  // selected object / gate
    bool dragging = false;         // moving the selection
    V3 dragOffset;
    bool stroke = false;           // a brush or paint stroke in progress (one undo step)
    // Settings page.
    int setRow = 0;
    bool typing = false;           // typing the level's name
    // Files.
    bool fileOpen = false;         // the open dialog
    int fileSel = 0;
    std::vector<Level> files;
    int newMap = 0;
    bool confirmClose = false;
    std::string toast;
    double toastTime = 0;
    double clock = 0;
    bool shift = false, ctrl = false;

    struct Button {
        float x, y, w, h;
        int id;
    };
    std::vector<Button> buttons;

    void say(const std::string& s, double t = 2.5) { toast = s, toastTime = t; }
    void ray(float sx, float sy, V3& o, V3& d) const;
    void click();
    void release();
    void key(int sc);
    void wheel(int dy);
    void doButton(int id);
    void applyBrush(double dt);
    void save();
    void openFile(const Level& lv);
    void settingChange(int row, int dir);
    LevelObject makeObject(const V3& at) const;
    void drawWorldMarks(Renderer& r);
    void drawToolbar(Renderer& r, float u);
    void drawOptions(Renderer& r, float u);
    void drawStatus(Renderer& r, float u);
    void drawFiles(Renderer& r, float u);
    float button(Renderer& r, float x, float y, float w, float h, const std::string& label, int id, bool on, float u);
    Renderer* rend = nullptr;
};
