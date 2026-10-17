// The world editor's working state, without any graphics: the level being made, the world
// it's shown on, the terrain brushes, painting, water, objects, the course (start and gates),
// and undo. The editor's UI (editor.h) drives it with the mouse; tests/editor.cpp drives it
// directly.
//
// Every edit goes into the World at once (so the physics, the autopilot and the renderer see
// it) and into the Level (so it saves). The Level only ever holds what applyLevel() would put
// on a fresh World(level.map), so what you see in the editor is what the level plays like.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "level.h"
#include "sections.h"

enum BrushOp { BRUSH_RAISE, BRUSH_LOWER, BRUSH_SMOOTH, BRUSH_FLATTEN, BRUSH_ROUGHEN, BRUSH_TERRACE, BRUSH_OP_COUNT };

// ---- The brush: one mask for every tool ----------------------------------------------------
// Terrain, surface paint, colour paint and the water drain all ask brushMask() how much of the brush
// reaches a cell. It is deterministic (the same brush and cell always give the same answer: the
// patterns hang on the world, not the cursor), so undo, redo and level files stay consistent.
enum BrushShape { SHAPE_ROUND, SHAPE_SQUARE, SHAPE_DIAMOND, SHAPE_COUNT };
enum BrushFall { FALL_HARD, FALL_SMOOTH, FALL_LINEAR, FALL_COUNT };
enum BrushStyle { STYLE_SOLID, STYLE_SPLATTER, STYLE_SPECKLE, STYLE_NOISE, STYLE_STRIPES, STYLE_COUNT };
const char* brushShapeName(int shape);
const char* brushFallName(int fall);
const char* brushStyleName(int style);

struct Brush {
    BrushShape shape = SHAPE_ROUND;
    BrushFall fall = FALL_SMOOTH;
    BrushStyle style = STYLE_SOLID;
    double radius = 8;      // m (round: radius; square: half side; diamond: tip). 0.5 is one grid cell.
    double strength = 1;    // 0..1: paint's opacity (terrain takes its rate from the caller's `amount`)
    double density = 0.5;   // 0..1: how much of the brush the pattern covers (splatter, speckle, noise, stripes)
    double angle = 0;       // radians: which way the stripes run
    uint32_t seed = 1;      // changes the pattern (new dabs, new mottling)
};
constexpr double kBrushMin = 0.5, kBrushMax = 100;
// The next size up (dir +1) or down (-1) from `r` in the brush size ladder (about x1.4 a step, from
// one cell to 100 m).
double brushStep(double r, int dir);
// How much of the brush (0..1) reaches the point (x, z) when it is centered on (cx, cz). Cells sit
// on whole meters; under 1 m radius the center snaps to the nearest cell so the brush is that one cell.
double brushMask(const Brush& b, double cx, double cz, double x, double z);
// Coherent noise, 0..1, for patterns (value noise, `scale` m between lattice points).
double brushNoise(double x, double z, double scale, uint32_t seed);

// A paint material: how it drives (surface) and how it looks (paint), and a swatch color
// for the editor's UI.
struct PaintMaterial {
    const char* name;
    uint8_t surface, paint;
    float col[3];
};
const std::vector<PaintMaterial>& paintMaterials();

// A rectangle of grid cells (inclusive) that changed.
struct CellRegion {
    int i0 = 1 << 30, j0 = 1 << 30, i1 = -1, j1 = -1;
    bool empty() const { return i1 < i0 || j1 < j0; }
    void add(int i, int j) {
        i0 = std::min(i0, i), j0 = std::min(j0, j), i1 = std::max(i1, i), j1 = std::max(j1, j);
    }
    void add(const CellRegion& o) {
        if (!o.empty()) add(o.i0, o.j0), add(o.i1, o.j1);
    }
    void clear() { *this = CellRegion(); }
};

// Something in the world the editor can point at.
struct EditPick {
    enum Kind { NONE, OBJECT, BASE, GATE, START };
    Kind kind = NONE;
    int index = -1;     // OBJECT: into level.objects; GATE: into level.gates; BASE: into its world list
    int baseKind = -1;  // BASE: 0 rock, 1 tree, 2 log, 3 box, 4 fixture (5: a model the level placed; 6: a bush)
    V3 at;              // where it stands (on the ground); for BASE, where removeNear will find it
    double t = 1e18;    // distance along the ray
    std::string name;   // "ROCK", "GATE 3", ...
    bool operator==(const EditPick& o) const { return kind == o.kind && index == o.index && baseKind == o.baseKind; }
    bool operator!=(const EditPick& o) const { return !(*this == o); }
};

// A mouse press that only becomes a drag once the pointer has moved a few pixels (so a click that jitters by one stays a click).
struct DragGate {
    float x0 = 0, y0 = 0;
    bool armed = false;
    void press(float x, float y) { x0 = x, y0 = y, armed = false; }
    // True once the pointer is `px` or more from where it went down (and stays true until the next press).
    bool moved(float x, float y, float px) {
        if (!armed && std::hypot(x - x0, y - y0) >= px) armed = true;
        return armed;
    }
};

// What can be done to an object (EditCore::transformed): turn it, tip it, lift it, scale it as a whole, stretch it along one of
// its own axes (rock: along, up, across; box: its extents; log: length, thickness; tree: height; model: front, up, left).
enum ObjOp { OP_YAW, OP_PITCH, OP_ROLL, OP_LIFT, OP_SCALE, OP_STRETCH_X, OP_STRETCH_Y, OP_STRETCH_Z, OP_SEAT };
// (OP_SEAT: sit it back on the ground, as placed; the others raise or lower it by any amount, in the air or in the ground, and it stays.)

// A world axis: what Shift (X), Alt (Y) and Ctrl (Z) lock a drag to, and what a right-drag turns about.
enum EditAxis { AXIS_NONE, AXIS_X, AXIS_Y, AXIS_Z };
V3 axisDir(EditAxis a);
const char* axisName(EditAxis a);  // "X"
// An axis-locked move is driven by the mouse going up and down: how far along the axis (m) for a mouse move of `dy` px (down is +), given the axis'
// vertical run on the screen (`sy` px per metre, down is +) and the screen's px per metre there (`pxPerM`). The thing follows the mouse; an axis that
// is nearly edge-on to the view (it runs less than 0.3 of the scale up or down the screen) falls back to mouse-up = +axis at that rate.
double axisDragMetres(double sy, double pxPerM, double dy);
// Which way a sideways mouse move turns an object about `axis` so the side facing the camera follows it (+1 or -1; +1 when the turn
// is in the screen's plane). `toCam`: unit vector from the object to the camera; `right`: the camera's right.
double turnSign(EditAxis axis, const V3& toCam, const V3& right);

// An object's outline for drawing over the view: its edges, and for the convex ones (rocks, blocks) its faces.
struct Shape {
    std::vector<std::pair<V3, V3>> edges;
    std::vector<std::array<V3, 3>> tris;
    void add(const Shape& o) {
        edges.insert(edges.end(), o.edges.begin(), o.edges.end());
        tris.insert(tris.end(), o.tris.begin(), o.tris.end());
    }
};

class EditCore {
public:
    World* world = nullptr;
    Level level;
    // What changed since the UI last caught up (it redraws, then clears these).
    CellRegion dirtyCells;
    bool dirtyWater = false, dirtyObjects = false;
    double clock = 0;  // seconds (the UI's; undo merges quick repeats of the same edit)

    // Starts editing `lv` on `w`, which must be the plain map (World(lv.map), nothing of the
    // level on it yet): remembers the map's own objects, then puts the level on it.
    void begin(World& w, const Level& lv);
    bool started() const { return world != nullptr; }

    // ---- Edits ------------------------------------------------------------------------
    // Everything between beginEdit and endEdit is one undo step (they nest). The single
    // operations below open their own when none is open.
    void beginEdit(const std::string& what, const std::string& mergeKey = "");
    void endEdit();
    bool editing() const { return depth > 0; }

    // Terrain brush at (cx, cz), radius r (m). RAISE/LOWER: `amount` meters at the center;
    // SMOOTH/FLATTEN: the share (0..1) of the way to go this time; FLATTEN goes to `target`.
    // ROUGHEN: bumps up to `amount` m of noise per pass; TERRACE: steps `target` m high (share `amount`).
    void brush(BrushOp op, double cx, double cz, double r, double amount, double target = 0);
    // The same with a full brush (shape, falloff, splatter...) centered on (cx, cz).
    void brush(BrushOp op, const Brush& b, double cx, double cz, double amount, double target = 0);
    // A straight ramp of ground from a to b (their heights: a.y, b.y), `width` across, with
    // shoulders blending into the land around it.
    void ramp(const V3& a, const V3& b, double width);
    // Flattens a rectangle (half-length hl along yaw, half-width hw) to height y, blending
    // over `margin` meters round it (a pad for a repair station).
    void levelPad(const V3& c, double yaw, double hl, double hw, double y, double margin);
    // Paints a material (index into paintMaterials) within r; soft edges dither.
    void paint(double cx, double cz, double r, int material, bool soft);
    // Paints with a full brush: a cell takes the material when the brush's mask (times its opacity)
    // beats that cell's dither, so soft edges and light strokes speckle and fill in as you go over them.
    void paint(const Brush& b, double cx, double cz, int material);
    // Fills the hollow at (cx, cz) with water to `depth` above the ground there (or above the
    // water already there), spreading over lower ground no further than maxR. Returns how many
    // cells it wet; spilled is set if it ran up to maxR (it would have kept going).
    int flood(double cx, double cz, double depth, double maxR, bool* spilled = nullptr);
    void dry(double cx, double cz, double r);   // takes water away within r
    // Takes water away where the brush reaches (mask of one half or more: clean edges, no ragged
    // fringe). Returns how many cells it dried.
    int dryBrush(const Brush& b, double cx, double cz);
    // Drains the whole body of water at (cx, cz) (or within 3 m of it): every connected wet cell,
    // shallow edges and any left under raised ground included. Returns how many cells it dried.
    int drain(double cx, double cz);

    // Objects. add returns its index in level.objects.
    int addObject(const LevelObject& o);
    // Several at once (one undo step, one rebuild): a row of ledge slabs.
    void addObjects(const std::vector<LevelObject>& objs, const std::string& what);
    void setObject(int k, const LevelObject& o, const std::string& mergeKey = "");
    void removeObject(int k);
    // Takes a base-map object out (records it in level.removed).
    bool removeBase(const EditPick& p);
    // A map's own repair shop, picked: it becomes the level's object (same place, turn and
    // name) so it can be dragged, turned or deleted like one you placed. Returns its object
    // index, or -1 if the pick isn't a map repair shop.
    int adoptBase(const EditPick& p);
    // A copy of an object with its height worked out for where it stands now (boxes sit
    // on the ground plus their lift, logs lie on it plus theirs) and kept as that offset (rel).
    LevelObject grounded(LevelObject o) const;
    // The map's own objects become the level's, so they can be moved, turned, scaled and deleted like the ones you place:
    // each is taken out of the map (level.removed, with its kind) and put back as an equal LevelObject. One undo step.
    // Returns their object indices, in order (-1 for one that can't be: a cabin, a fence...). Picks that already are objects pass through.
    // `wants` (optional) is asked of each map pick's would-be object: one it says no to is left the map's own (and stays -1 in the result;
    // `left`, if given, marks those 1), so an edit that would change nothing about it doesn't take it over.
    std::vector<int> adopt(const std::vector<EditPick>& picks, const std::function<bool(const LevelObject&)>& wants = nullptr, std::vector<char>* left = nullptr);
    bool canAdopt(const EditPick& p) const;
    int resolveIndex(const EditPick& p) const { return resolveBase(p); }  // a map pick's index in the world now
    // The map's own object as the level object it would become (for showing its size and pose).
    bool asObject(const EditPick& p, LevelObject& o) const { V3 k; return baseToObject(p, o, k); }
    // Deletes a set of objects and the map's own as one undo step.
    void removePicks(const std::vector<EditPick>& picks);
    // Moves objects by (dx, dz) on the ground, keeping how high each stands above it. One undo step (quick repeats merge on `key`).
    void moveObjects(const std::vector<int>& ks, double dx, double dz, const std::string& key = "move");
    // Applies an op to objects: each about itself, except a turn or a scale of several, which swing or spread them all round their middle.
    // Heights are offsets from the ground (LevelObject::rel), so sculpting carries every object along; tipping a block keeps the height it was set
    // at. A rock stacked on another keeps the rise it was given (its `top`); it doesn't follow if the lower rock is moved, but it's never re-seated into it.
    void transformObjects(const std::vector<int>& ks, ObjOp op, double amount, const std::string& key = "xform");
    // Turns objects about a world axis through each one's middle (a right-drag): Y is the same as a turn (a group swings round its middle); X and Z tip
    // and roll rocks, blocks and logs (each in place; other kinds only turn about Y). Returns false if none of them can turn about that axis.
    bool rotateObjects(const std::vector<int>& ks, EditAxis axis, double angle, const std::string& key = "rotate");
    // One object turned that way (normalized, re-grounded); the pure part of rotateObjects.
    LevelObject rotated(LevelObject o, EditAxis axis, double angle);
    static bool canRotate(int objKind, EditAxis axis);
    // Moves objects `amount` m along a world axis, as a locked drag does: X, Z on the ground (heights are kept as offsets from it), Y up or down
    // (it stays there: a rock's `top`, a block's or tree's `lift`, a log's two lifts).
    void moveAlong(const std::vector<int>& ks, EditAxis axis, double amount, const std::string& key = "drag");
    // One object after an op (it must be normalized: see below), re-grounded; the pure part of transformObjects.
    LevelObject transformed(LevelObject o, ObjOp op, double amount);
    // The first solid thing (a rock's hull, a solid box, a log) along a ray, skipping some level objects: where, and the surface normal.
    bool rayObjects(const V3& o, const V3& d, double tMax, const std::vector<int>& skip, V3& p, V3& n) const;
    // How much of rock `o` shows above the ground at its place when it sits on that ground, buried a little (its default).
    double seatedTop(LevelObject o);
    // Object k with everything an op needs made explicit: a rock left to chance pinned as it was built, a box's height off the ground.
    LevelObject normalized(int k) const;
    // The same object moved by (dx, dz).
    LevelObject movedBy(LevelObject o, double dx, double dz) const;
    // A rock of the world as an equal level object (a scanned one exactly; a code-made one by its bounds).
    bool rockToObject(const Rock& r, LevelObject& o) const;
    // The world's parts (baseKind, index) that object k made.
    std::vector<std::pair<int, int>> partsOf(int k) const;
    // The outline of a part of the world, and of an object that isn't placed yet (a ghost; the world is left as it was).
    Shape shapeOf(int baseKind, int index) const;
    Shape previewShape(const LevelObject& o);

    // The course.
    void setStart(double x, double z, double yaw);
    int addGate(const Gate& g, int at = -1);  // at: index to insert at (-1 the end)
    void setGate(int k, const Gate& g, const std::string& mergeKey = "");
    void removeGate(int k);
    void moveGate(int k, int dir);             // earlier (-1) or later (+1) in the order
    // ---- Sections (sectionfile.h) -------------------------------------------------------
    // Makes the level a new section on `canvas` (the world must be the proving ground, plain): flattens
    // and clears the canvas and its blend, puts the entry (the start) near the back and the exit (a
    // gate) near the far end, in sprint mode. Not undoable: it's where you start from.
    void newSection(const SectionInfo& canvas);
    // Stamps generated section `type` (a SectionType) with `seed` into the canvas at the entry, as the
    // starting point to edit by hand: its ground, and its rocks, slabs, trees and logs as the level's
    // own objects. One undo step. Returns how many objects it made (-1: no section here).
    int stampSection(int type, uint32_t seed);
    // After a save: what went wrong with the section file, or "" (the level itself saved).
    std::string saveNote;
    // Level settings: fn changes the level (its name, mode, rules...), as one undo step.
    template <typename F>
    void settings(const std::string& key, F fn) {
        beginEdit("SETTINGS", key);
        touchMeta();
        fn(level);
        endEdit();
    }

    // ---- Trying the level out ------------------------------------------------------------
    // Where a playtest starts: `here` (on the ground at `at`, facing `yaw`), else the level's start, else the map's own spawn.
    struct PlaySpot {
        V3 p;
        double yaw = 0;
        bool here = false, fromStart = false;  // fromStart: the level has a start (so a pad is built there)
    };
    PlaySpot playSpot(bool here, const V3& at, double yaw) const;
    // The level to play: as it is now (a copy; the edits stay in the editor). Testing from here, a free drive.
    Level levelToPlay(bool here) const;
    // Back to editing after a playtest, on a fresh plain map `w` (levelWorld(level)): the level, its undo history and
    // everything else are as they were left, and the world is rebuilt from them. (No reload from disk: unsaved work stays.)
    void resume(World& w);

    // ---- Recovery copies -----------------------------------------------------------------
    // Where the level's recovery copy goes in `dir`, and writing it there (the level's own file is never touched).
    std::string autosavePath(const std::string& dir) const;
    bool autosave(const std::string& dir, std::string* err = nullptr);
    int autosavedChanges = -1;  // the `changes` count the recovery copy has
    // Throwing the unsaved edits away: the recovery copies go (never the saved .czl) and this returns the level as last saved (its
    // file as it is on disk), else as it was opened: the plain map or built-in level it came from. `dir` "": no recovery copies.
    Level discard(const std::string& dir);
    Level origin;               // the level as it was opened (begin), or as setOrigin says

    // ---- Undo -------------------------------------------------------------------------
    bool undo();
    bool redo();
    bool canUndo() const { return !history.empty(); }
    bool canRedo() const { return !future.empty(); }
    std::string undoName() const { return history.empty() ? "" : history.back().what; }
    std::string redoName() const { return future.empty() ? "" : future.back().what; }
    int changes = 0, savedChanges = 0;  // unsaved while they differ
    bool unsaved() const { return changes != savedChanges; }
    bool save(const std::string& path, std::string* err = nullptr);

    // ---- Looking --------------------------------------------------------------------------
    // Where a ray from o along d (unit) first meets the ground, within maxT.
    bool rayGround(const V3& o, const V3& d, double maxT, V3& hit) const;
    // Everything whose middle `inside` accepts (a box on the screen): the level's objects, and the map's own rocks, slabs, trees, logs and
    // bushes. The map's infrastructure (plain blocks, repair shops, cabins, fences, signs...) only when `everything`. At most `cap`
    // (`capped` says it stopped there). Map picks are BASE picks that canAdopt.
    std::vector<EditPick> pickInside(const std::function<bool(const V3&)>& inside, bool everything, size_t cap, bool* capped = nullptr) const;
    // The nearest object, gate or start marker on the ray (and in front of hitT, the ground). `course`: gates and the start marker
    // count (the COURSE tool); the objects tool passes false and its rays go through them.
    EditPick pick(const V3& o, const V3& d, double hitT, bool base = true, bool course = true) const;
    // Which level object a world rock/tree/log/box/fixture index belongs to (-1: the map's own).
    int ownerOf(int baseKind, int index) const;
    // How many cells, objects... the level changes (for the status bar).
    size_t cellEdits() const { return level.heights.size() + level.surfaces.size() + level.water.size(); }

    // Rebuilds the world's objects from the map's own plus the level's (after changing
    // level.objects or level.removed by hand). The editor ops call it themselves.
    void rebuildObjects();

private:
    struct CellChange {
        int id;
        uint8_t what;  // 0 height, 1 surface, 2 water
        bool hadOld, hasNew;
        double worldOld, worldNew, lvOld, lvNew;
    };
    struct Entry {
        std::string what, mergeKey;
        double time = 0;
        std::vector<CellChange> cells;
        bool meta = false;
        Level before, after;  // (without the cell maps)
    };
    // The map's own objects, as built.
    struct Base {
        std::vector<Rock> rocks;
        std::vector<Tree> trees;
        std::vector<Pole> poles;
        std::vector<Log> logs;
        std::vector<Box> boxes;
        std::vector<Prop> fixtures;
        std::vector<Station> stations;
        std::vector<Placed> models;
        std::vector<Bush> bushes;
        V3 spawn;
        double spawnYaw = 0;
    } base;
    int baseCount[7] = {0, 0, 0, 0, 0, 0, 0}; // after removals: rocks, trees, logs, boxes, fixtures, models, bushes
    std::vector<int> owner[7];                // world index -> level object (-1 the map's)
    std::vector<Entry> history, future;
    Entry pending;
    std::map<int, size_t> pendingCells;       // (id * 4 + what) -> index in pending.cells
    int depth = 0;

    void touchHeight(int id, float h);
    void touchSurface(int id, uint16_t sp);
    void touchWater(int id, float level);
    void touchMeta();
    void setCell(int id, int what, double v, bool inLevel, double lv);
    double worldCell(int id, int what) const;
    Level metaOf();
    void restoreMeta(const Level& m);
    void syncSpawn();
    void clearBushes();  // a section's canvas has none (they're scenery the level can't take out)
    void markCell(int id);
    void trim();
    void applyCells(World& w);
    void replaceObjects(const std::vector<std::pair<int, LevelObject>>& set, const std::string& what, const std::string& key);
    int resolveBase(const EditPick& p) const;
    bool baseToObject(const EditPick& p, LevelObject& o, V3& key) const;
};

// A file name for a level name ("Dune Run!" -> "dune_run").
std::string levelFileName(const std::string& name);
