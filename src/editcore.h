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
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "level.h"
#include "sections.h"

enum BrushOp { BRUSH_RAISE, BRUSH_LOWER, BRUSH_SMOOTH, BRUSH_FLATTEN };

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
    int baseKind = -1;  // BASE: 0 rock, 1 tree, 2 log, 3 box, 4 fixture (5: a model the level placed)
    V3 at;              // where it stands (on the ground); for BASE, where removeNear will find it
    double t = 1e18;    // distance along the ray
    std::string name;   // "ROCK", "GATE 3", ...
    bool operator==(const EditPick& o) const { return kind == o.kind && index == o.index && baseKind == o.baseKind; }
    bool operator!=(const EditPick& o) const { return !(*this == o); }
};

// What can be done to an object (EditCore::transformed): turn it, tip it, lift it, scale it as a whole, stretch it along one of
// its own axes (rock: along, up, across; box: its extents; log: length, thickness; tree: height; model: front, up, left).
enum ObjOp { OP_YAW, OP_PITCH, OP_ROLL, OP_LIFT, OP_SCALE, OP_STRETCH_X, OP_STRETCH_Y, OP_STRETCH_Z };

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
    void brush(BrushOp op, double cx, double cz, double r, double amount, double target = 0);
    // A straight ramp of ground from a to b (their heights: a.y, b.y), `width` across, with
    // shoulders blending into the land around it.
    void ramp(const V3& a, const V3& b, double width);
    // Flattens a rectangle (half-length hl along yaw, half-width hw) to height y, blending
    // over `margin` meters round it (a pad for a repair station).
    void levelPad(const V3& c, double yaw, double hl, double hw, double y, double margin);
    // Paints a material (index into paintMaterials) within r; soft edges dither.
    void paint(double cx, double cz, double r, int material, bool soft);
    // Fills the hollow at (cx, cz) with water to `depth` above the ground there (or above the
    // water already there), spreading over lower ground no further than maxR. Returns how many
    // cells it wet; spilled is set if it ran up to maxR (it would have kept going).
    int flood(double cx, double cz, double depth, double maxR, bool* spilled = nullptr);
    void dry(double cx, double cz, double r);   // takes water away within r

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
    // on the ground, logs lie on it).
    LevelObject grounded(LevelObject o) const;
    // The map's own objects become the level's, so they can be moved, turned, scaled and deleted like the ones you place:
    // each is taken out of the map (level.removed, with its kind) and put back as an equal LevelObject. One undo step.
    // Returns their object indices, in order (-1 for one that can't be: a cabin, a fence...). Picks that already are objects pass through.
    std::vector<int> adopt(const std::vector<EditPick>& picks);
    bool canAdopt(const EditPick& p) const;
    // Deletes a set of objects and the map's own as one undo step.
    void removePicks(const std::vector<EditPick>& picks);
    // Moves objects by (dx, dz) on the ground, keeping how high each stands above it. One undo step (quick repeats merge on `key`).
    void moveObjects(const std::vector<int>& ks, double dx, double dz, const std::string& key = "move");
    // Applies an op to objects: each about itself, except a turn of several, which swings them all round their middle.
    void transformObjects(const std::vector<int>& ks, ObjOp op, double amount, const std::string& key = "xform");
    // One object after an op (it must be normalized: see below), re-grounded; the pure part of transformObjects.
    LevelObject transformed(LevelObject o, ObjOp op, double amount) const;
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
    // The nearest object, gate or start marker on the ray (and in front of hitT, the ground).
    EditPick pick(const V3& o, const V3& d, double hitT, bool base = true) const;
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
        V3 spawn;
        double spawnYaw = 0;
    } base;
    int baseCount[6] = {0, 0, 0, 0, 0, 0};    // after removals: rocks, trees, logs, boxes, fixtures, models
    std::vector<int> owner[6];                // world index -> level object (-1 the map's)
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
