// The world editor: fly over a map, sculpt and paint its ground, flood hollows, place rocks,
// trees, logs, ramps and repair shops, lay out a course (start and gates), set the rules, and
// save it as a level (.czl) to play, alone or with friends.
//
// The working state (and undo) is EditCore (editcore.h); this is its camera, mouse and
// keyboard, and what's drawn over the view. The game hands it the events while it's open.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "editcore.h"
#include "render.h"
#include "sdl_min.h"

class Editor {
public:
    enum Tool { SCULPT, PAINT, WATER, OBJECTS, COURSE, SETTINGS, SECTION, TOOL_COUNT };
    bool active = false;
    EditCore core;
    // Set when the player asks to try the level (the game plays core.levelToPlay(playHere) at `spot`, then comes back to this
    // editor as it was: the camera, the undo history and anything unsaved). playHere: from where the camera looks, else the start.
    bool wantPlay = false, playHere = false;
    EditCore::PlaySpot spot;
    // Recovery copies of the level being edited go in this folder (every minute while there are unsaved changes, and when a
    // playtest starts); "" turns it off.
    std::string autosaveDir;
    // Code-made structures to draw as the ghost this frame (the game adds them to the renderer's).
    std::vector<Placed> ghostModels;
    // Set when the editor is closed (the game puts the player back on the world as edited).
    bool wantClose = false;

    // Starts editing `lv`. The world must be the plain map (World(lv.map)).
    void open(World& w, Renderer& r, const Level& lv);
    // Back after a playtest: `w` is the level's plain map again (levelWorld(core.level)); everything else is as it was left.
    void resume(World& w, Renderer& r);
    bool started() const { return core.started(); }
    // Where the middle of the view meets the ground, and the way the camera faces (a playtest from here starts there).
    V3 viewPoint() const { return haveViewHit ? viewHit : eye; }
    double heading() const { return yaw; }
    // Writes the recovery copy now (the game does this as a playtest starts).
    void autosaveNow();
    // Starts a new obstacle section on a fresh proving ground: the canvas flattened and cleared, the
    // entry and exit set (EditCore::newSection). `w` is replaced.
    void newSection(World& w, Renderer& r);
    // The camera (for screenshots); and which tool is up.
    void setCamera(const V3& at, double yawRad, double pitchRad) { eye = at, yaw = yawRad, pitch = pitchRad; }
    void setTool(Tool t) { tool = t, sel = EditPick(), selection.clear(); }
    void setMouse(float x, float y) { mx = x, my = y; }
    void setLedge(int riseIdx, double depth) { ledgeRise = riseIdx, ledgeDepth = depth, ledgeBrush = true, pointer = false; }
    // The object palette: what the next click places (an index into the palette), or the pointer for selecting and moving.
    void setPalette(int index);
    void setPointer(bool on) { pointer = on; }
    int paletteSize() const;
    LevelObject makeForScript(const V3& at) { return makeObject(at); }
    int findPalette(const std::string& name);  // the first entry whose name has this in it (-1: none)
    void requestPlay(bool here);               // as the buttons: TEST FROM HERE / FROM START
    const char* paletteName(int index) const;
    // Selecting (for scripts and tests): the level's objects by index.
    void selectObjects(const std::vector<int>& ks);
    // For screenshots: the ghost's transform and the hand-held keys' effect.
    void applyKey(int sc, bool shiftHeld = false, bool ctrlHeld = false) {
        scriptMods = true, shift = shiftHeld, ctrl = ctrlHeld;
        key(sc);
        scriptMods = false, shift = ctrl = false;
    }
    // A row of rock slabs along the line a-b (the slab-ledge brush).
    void placeLedge(const V3& a, const V3& b);
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
    // Objects: the palette entry the next click places (the ghost), as it will be placed (`proto`: turned, tipped, scaled, stretched),
    // or the pointer for selecting, moving and changing what's there. The palette is built on first use (buildPalette).
    struct PalEntry {
        std::string name, tab, icon;
        LevelObject proto;  // (a log: size.x radius, size.y half-length, yaw, pitch and lift; its ends are worked out where it goes)
        bool ledge = false;
    };
    std::vector<PalEntry> palette;
    std::vector<std::string> tabs;
    int palTab = 1, palPage = 0, palPick = 0;
    bool pointer = false, ledgeBrush = false;
    LevelObject proto;
    uint32_t nextSeed = 1;
    std::vector<EditPick> selection;   // the objects tool: everything selected (sel is the last)
    bool marquee = false;              // dragging a box over the view to select what's inside
    float marqueeX = 0, marqueeY = 0;
    V3 dragFrom;                       // where the drag began on the ground
    V3 viewHit;                        // where the middle of the view meets the ground
    bool haveViewHit = false;
    bool scriptMods = false;
    double lastAuto = 0;
    std::map<std::string, mk::Mesh> iconMeshes;
    std::vector<bool> fileRecovery;    // per files[i]: a recovery copy (opened as an unsaved edit of the saved level)
    // Section authoring: grid snap for what's placed (0 off, then 0.5 / 1 / 2 m); the slab-ledge brush
    // (drag a line: a row of rock slabs along it, `ledgeRise` high, `ledgeDepth` deep); the generator to stamp.
    int snapIdx = 0;
    int ledgeRise = 2;
    double ledgeDepth = 3.0;
    bool ledgeOn = false;
    V3 ledgeA;
    int genType = 3;
    uint32_t genSeed = 3;
    bool confirmNew = false;
    double gateWidth = 14;
    EditPick sel;                  // selected object / gate
    bool dragging = false;         // moving the selection
    V3 dragOffset, dragApplied;
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
    void openLevel(const Level& lv, bool recovered);
    void settingChange(int row, int dir);
    void sectionAction(int code);
    V3 snapped(const V3& p) const;
    LevelObject makeObject(const V3& at) const;
    void buildPalette();
    void choose(int index);
    const mk::Mesh& iconMesh(const PalEntry& e);
    // Selection and the ops on it (or, with nothing selected in placing mode, on the ghost).
    std::vector<int> selectedObjects();    // adopts the map's own among them first (one undo step with the op that follows)
    void applyOp(ObjOp op, double amount, const std::string& key);
    void nudge(double dx, double dz);
    void deleteSelection();
    void objectKey(int sc);
    void selectInBox(float x0, float y0, float x1, float y1, bool add);
    bool isSelected(const EditPick& p) const;
    void drawShape(Renderer& r, const Shape& s, bool fill, float cr, float cg, float cb, float a, float size);
    void drawPalette(Renderer& r, float u);
    void refreshRecoveries();
    void openFile(int index);
    void drawWorldMarks(Renderer& r);
    void drawToolbar(Renderer& r, float u);
    void drawOptions(Renderer& r, float u);
    void drawStatus(Renderer& r, float u);
    void drawFiles(Renderer& r, float u);
    float button(Renderer& r, float x, float y, float w, float h, const std::string& label, int id, bool on, float u);
    Renderer* rend = nullptr;
};
