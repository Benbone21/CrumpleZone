#include "editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "hudkit.h"
#include "props.h"
#include "levelrun.h"
#include "sectionfile.h"
#include "vehicles.h"

using namespace hud;

namespace {

const char* kToolNames[Editor::TOOL_COUNT] = {"SCULPT", "PAINT", "WATER", "OBJECTS", "COURSE", "SETTINGS", "SECTION"};
const char* kToolHints[Editor::TOOL_COUNT] = {"RAISE, LOWER, SMOOTH, FLATTEN, ROUGHEN, TERRACE", "PAINT MUD, SAND, SNOW, ROCK...",
                                              "FLOOD A HOLLOW, OR DRAIN IT", "ROCKS, TREES, LOGS, RAMPS, SHOPS",
                                              "START AND GATES", "NAME, MODE, RULES, MEDALS", "TYPE, DIFFICULTY, KIT, STAMP A GENERATOR"};
const char* kOpNames[BRUSH_OP_COUNT] = {"RAISE", "LOWER", "SMOOTH", "FLATTEN", "ROUGHEN", "TERRACE"};
const double kTerraces[4] = {0.25, 0.5, 1, 2};  // m: terrace step heights
const char* kWaterModes[3] = {"FILL HOLLOW", "DRAIN BRUSH", "DRAIN WHOLE POOL"};
const char* kObjNames[OBJ_KIND_COUNT] = {"ROCK", "RAMP / BLOCK", "LOG", "TREE", "REPAIR SHOP", "STRUCTURE", "FENCE", "BUSH"};
const int SC_PAGEUP_ = 75, SC_PAGEDOWN_ = 78, SC_F6_ = 63, SC_COMMA_ = 54, SC_PERIOD_ = 55;
const double kRisers[5] = {0.3, 0.45, 0.6, 0.9, 1.2};  // m: the step a ledge makes (a notch step stays under 0.4)
const double kSnaps[4] = {0, 0.5, 1, 2};               // m
const char* kTestShort[ST_COUNT] = {"SKILL", "PROBLEMS", "COMMITMENT", "ATTRITION", "CO-OP"};

// Button ids.
enum {
    B_TOOL = 100,            // + tool
    B_OP = 200,              // + brush op
    B_MAT = 300,             // + material
    B_SAVE = 500, B_OPEN, B_PLAY, B_UNDO, B_REDO, B_EXIT, B_NEWMAP, B_NEWSEC, B_PLAY_START,
    B_RAD_DN = 600, B_RAD_UP, B_STR_DN, B_STR_UP, B_DEPTH_DN, B_DEPTH_UP,
    B_SNAP_DN, B_SNAP_UP, B_RISE_DN, B_RISE_UP, B_LDEPTH_DN, B_LDEPTH_UP,
    B_BR = 650,              // brush rows: shape, falloff, pattern, density, terrace step (each < and >)
    B_SHAPE_DN = 650, B_SHAPE_UP, B_FALL_DN, B_FALL_UP, B_STYLE_DN, B_STYLE_UP, B_DENS_DN, B_DENS_UP, B_TERR_DN, B_TERR_UP,
    B_WMODE = 670,           // + water mode
    B_SET = 700,             // + settings row (click selects it)
    B_FILE = 800,            // + file index
    B_GATEW_DN = 900, B_GATEW_UP, B_DELETE, B_EARLIER, B_LATER, B_POINTER,
    B_SEC = 1000,            // + section page action (SecAct); to 1099
    B_XF = 1100,             // + op * 2 (+ 1 to raise it): the transform rows, to 1115
    B_NUDGE = 1120,          // + 0 west, 1 east, 2 north, 3 south (camera-relative: -x, +x, ahead, back)
    B_PALTAB = 1200,         // + tab, to 1219
    B_PAL_PREV = 1230, B_PAL_NEXT,
    B_SEAT = 1238,
    B_ASK_SAVE = 1240, B_ASK_DISCARD, B_ASK_CANCEL, B_NAME_OK, B_NAME_CANCEL, B_SAVEAS,
    B_PAL = 2000,            // + palette index
};

// The section page's actions.
enum SecAct { S_NAME, S_KIND_DN, S_KIND_UP, S_DIFF_DN, S_DIFF_UP, S_GEN_DN, S_GEN_UP, S_SEED_DN, S_SEED_UP, S_STAMP, S_TEST = 20, S_KIT = 30 };

bool screenPoint(const View& v, int W, int H, const V3& p, float& sx, float& sy) {
    const V3 f = norm(v.target - v.eye), rt = norm(cross(f, v.up)), up = cross(rt, f);
    const V3 d = p - v.eye;
    const double z = dot(d, f);
    if (z < 0.3) return false;
    const double k = 1.0 / std::tan(v.fovy / 2);
    sx = (float)(W / 2.0 + dot(d, rt) / z * k * H / 2.0);
    sy = (float)(H / 2.0 - dot(d, up) / z * k * H / 2.0);
    return true;
}

std::string fmt1(double v, const char* unit = "") {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%s", v, unit);
    return b;
}

// The ground's slope at (x, z) in degrees, over 2 m.
double slopeDeg(const World& w, double x, double z) {
    const double gx = (w.height(x + 1, z) - w.height(x - 1, z)) / 2, gz = (w.height(x, z + 1) - w.height(x, z - 1)) / 2;
    return std::atan(std::hypot(gx, gz)) * 180 / M_PI;
}

// What a slope means for the trucks (the handoff's measurements: a set-up Bronco on a side slope holds
// 35 and rolls at 40; stock slides at 30 on dirt; a grid wall seals a pass from 65).
const char* slopeNote(double deg, float& r, float& g, float& b) {
    r = 0.7f, g = 1.0f, b = 0.6f;
    if (deg < 25) return "ANYTHING DRIVES IT";
    r = 0.9f, g = 1.0f, b = 0.5f;
    if (deg < 30) return "STOCK HOLDS ON ROCK, SLIDES ON DIRT AT 30";
    r = 1.0f, g = 0.85f, b = 0.4f;
    if (deg < 35) return "SIDE SLOPE: STOCK SLIDES ON DIRT, SET-UP HOLDS";
    r = 1.0f, g = 0.65f, b = 0.3f;
    if (deg < 40) return "SET-UP TRUCK ONLY (4L, LOCKERS, AIR DOWN)";
    r = 1.0f, g = 0.45f, b = 0.3f;
    if (deg < 55) return "SIDE SLOPE ROLLS OR SLIDES. A WALL: ROCK BUGGY STILL CLIMBS";
    r = 1.0f, g = 0.35f, b = 0.35f;
    if (deg < 65) return "A WALL: ONLY THE BEST SET-UPS GET UP A STEP OR TWO";
    return "SEALS THE PASS (65+ STOPS EVEN THE ROCK BUGGY)";
}

std::string uniqueSectionName() {
    for (int n = 1; n < 1000; n++) {
        const std::string name = "MY SECTION " + std::to_string(n);
        std::ifstream f(userLevelFolder() + "/" + levelFileName(name) + ".czl");
        if (!f) return name;
    }
    return "MY SECTION";
}

}  // namespace

bool Editor::project(const V3& p, float& sx, float& sy, int w, int h) const { return screenPoint(view(w, h), w, h, p, sx, sy); }

V3 Editor::snapped(const V3& p) const {
    const double s = kSnaps[snapIdx];
    return s > 0 ? V3(std::round(p.x / s) * s, p.y, std::round(p.z / s) * s) : p;
}

bool Editor::project(const V3& p, float& sx, float& sy) const { return screenPoint(view(W, H), W, H, p, sx, sy); }

std::string Editor::debugState() const {
    std::string s = std::string("tool=") + kToolNames[tool] + " pointer=" + (pointer ? "1" : "0") + " pal=" + (palPick >= 0 && palPick < (int)palette.size() ? palette[palPick].name : "?") +
                    " cells=" + std::to_string(core.cellEdits()) + " radius=" + std::to_string(radius) + " op=" + std::to_string((int)op) + " unsaved=" + (core.unsaved() ? "1" : "0") + " objs=" + std::to_string(core.level.objects.size()) + " removed=" + std::to_string(core.level.removed.size()) + " sel=[";
    for (const EditPick& p : selection) s += std::to_string(p.kind) + ":" + std::to_string(p.index) + ":" + std::to_string(p.baseKind) + ":" + p.name + " ";
    s += "] hover=" + std::to_string(hover.kind) + ":" + std::to_string(hover.index) + ":" + hover.name + " eye=" + std::to_string(eye.x) + "," + std::to_string(eye.y) + "," + std::to_string(eye.z) +
         " ground=" + (haveHit ? std::to_string(hit.x) + "," + std::to_string(hit.y) + "," + std::to_string(hit.z) : std::string("none")) + " overUi=" + (overUi ? "1" : "0") + " mouse=" + std::to_string((int)mx) + "," + std::to_string((int)my) + " WH=" + std::to_string(W) + "x" + std::to_string(H) + " fileOpen=" + (fileOpen ? "1" : "0") + " files=" + std::to_string(files.size()) + " fileSel=" + std::to_string(fileSel) + " flow=" + std::to_string((int)flow.stage) + " toast=\"" + toast + "\" undo=" + core.undoName();
    return s;
}

void Editor::newSection(World& w, Renderer& r) {
    rend = &r;
    Level l;
    l.map = MAP_WILDS;  // (open country, nothing laid out on it: what you make here looks the way it plays)
    l.name = uniqueSectionName();
    l.mode = LEVEL_SPRINT;
    w = levelWorld(l);
    open(w, r, l);
    core.newSection(wildsSectionCanvas());
    const SectionInfo& c = core.level.section;
    const V3 f(std::cos(c.heading), 0, -std::sin(c.heading));
    yaw = c.heading, pitch = -0.55;
    eye = core.level.start - f * 78 + V3(0, 64, 0);
    tool = SECTION;
    say("NEW SECTION: ENTRY AND EXIT ARE SET. BUILD BETWEEN THEM, THEN SAVE (CTRL+S)", 6);
}

void Editor::open(World& w, Renderer& r, const Level& lv) {
    rend = &r;
    core.begin(w, lv);
    active = true;
    wantPlay = wantClose = false;
    fileOpen = typing = confirmClose = false;
    sel = EditPick();
    selection.clear();
    marquee = dragging = false;
    pointer = false;
    buildPalette();
    choose(palPick);
    lastAuto = clock;
    // Start above the level's start (or the map's spawn), looking ahead and down.
    const V3 s = lv.hasStart ? lv.start : w.spawn;
    yaw = lv.hasStart ? lv.startYaw : w.spawnYaw;
    pitch = -0.55;
    eye = V3(s.x, w.height(s.x, s.z), s.z) - V3(std::cos(yaw), 0, -std::sin(yaw)) * 30 + V3(0, 22, 0);
    newMap = lv.map;
    r.setWorld(w);
    core.dirtyCells.clear();
    core.dirtyWater = core.dirtyObjects = false;
    say("WORLD EDITOR: " + lv.name, 4);
}

void Editor::resume(World& w, Renderer& r) {
    rend = &r;
    core.resume(w);
    active = true;
    wantPlay = wantClose = false;
    fileOpen = typing = confirmClose = marquee = dragging = false;
    sel = EditPick();
    selection.clear();
    r.setWorld(w);
    core.dirtyCells.clear();
    core.dirtyWater = core.dirtyObjects = false;
    say("BACK IN THE EDITOR: YOUR EDITS ARE HERE" + std::string(core.unsaved() ? " (NOT SAVED YET: CTRL+S)" : ""), 4);
}

void Editor::autosaveNow() {
    if (autosaveDir.empty() || !core.started() || !core.unsaved() || core.autosavedChanges == core.changes) return;
    core.autosave(autosaveDir);
    lastAuto = clock;
}

// Asks for a playtest. From here: where the middle of the view meets the ground, facing the way the camera faces. From the start:
// the level's start (on its spawn pad); a level without one says so, rather than dropping you somewhere on the map.
void Editor::requestPlay(bool here) {
    if (!here) {
        if (!core.level.hasStart) {
            say("NO START YET: SET ONE WITH THE COURSE TOOL (5), OR TEST FROM HERE (F5)", 5);
            return;
        }
        if (core.level.mode != LEVEL_FREE && core.level.gates.empty()) {
            say("A RACE OR SPRINT NEEDS GATES (COURSE TOOL), OR SET THE MODE TO FREE DRIVE. TEST FROM HERE (F5) WORKS ANYWAY", 5);
            return;
        }
    }
    spot = core.playSpot(here, haveViewHit ? viewHit : eye, yaw);
    playHere = here;
    autosaveNow();
    wantPlay = true;
}

void Editor::ray(float sx, float sy, V3& o, V3& d) const {
    const View v = view(W, H);
    const V3 f = norm(v.target - v.eye), rt = norm(cross(f, v.up)), up = cross(rt, f);
    const double k = std::tan(v.fovy / 2);
    const double dx = (sx - W / 2.0) / (H / 2.0) * k, dy = (H / 2.0 - sy) / (H / 2.0) * k;
    o = v.eye;
    d = norm(f + rt * dx + up * dy);
}

View Editor::view(int w, int h) const {
    View v;
    v.eye = eye;
    const V3 f(std::cos(yaw) * std::cos(pitch), std::sin(pitch), -std::sin(yaw) * std::cos(pitch));
    v.target = eye + f * 10;
    v.up = V3(0, 1, 0);
    v.fovy = 1.1f;
    v.width = w, v.height = h;
    return v;
}

// ---- Input -------------------------------------------------------------------------------

void Editor::handleEvent(const SDL_Event& e, float scale) {
    switch (e.type) {
        case SDL_KEYDOWN: key(evKeyScancode(e)); break;
        case SDL_MOUSEMOTION:
            mx = evMotionX(e) * scale, my = evMotionY(e) * scale;
            if (rmb) {
                yaw -= evMotionXrel(e) * 0.004;
                pitch = clampd(pitch - evMotionYrel(e) * 0.004, -1.5, 1.2);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            mx = evButtonX(e) * scale, my = evButtonY(e) * scale;
            if (evButton(e) == 3) rmb = true;
            if (evButton(e) == 1) {
                for (const Button& b : buttons)
                    if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                        doButton(b.id);
                        return;
                    }
                if (overUi || fileOpen) return;
                lmb = true;
                click();
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (evButton(e) == 3) rmb = false;
            if (evButton(e) == 1 && lmb) {
                lmb = false;
                release();
            }
            break;
        case SDL_MOUSEWHEEL: wheel(evWheelY(e)); break;
    }
}

void Editor::wheel(int dy) {
    if (fileOpen) {
        fileSel = std::min(std::max(fileSel - dy, 0), std::max(0, (int)files.size() - 1));
        return;
    }
    // Ctrl+wheel: brush size (sculpt, paint, water); Shift+wheel: the camera up / down (strength is - =).
    if (ctrl && dy != 0 && (tool == SCULPT || tool == PAINT || tool == WATER)) {
        radius = brushStep(radius, dy > 0 ? 1 : -1);
        return;
    }
    if (shift && dy != 0) {
        eye.y = std::max(eye.y + dy * 3.0, core.world->height(eye.x, eye.z) + 1.5);
        return;
    }
    // Mouse wheel zooms (flies in the direction looked).
    const View v = view(W, H);
    eye += norm(v.target - v.eye) * (dy * 4.0);
}

// ---- The object palette --------------------------------------------------------------------

namespace {

// "rock_moss1_a" -> "MOSS 1 A"
std::string prettyName(std::string n) {
    for (const char* pre : {"rock_", "nama_"})
        if (n.rfind(pre, 0) == 0 && n.size() > 5) {
            if (std::string(pre) == "nama_") n = "n_" + n.substr(5);
            else n = n.substr(5);
        }
    std::string out;
    for (size_t i = 0; i < n.size(); i++) {
        const char c = n[i];
        if (c == '_') out += ' ';
        else {
            if (i > 0 && std::isdigit((unsigned char)c) && std::isalpha((unsigned char)n[i - 1])) out += ' ';
            out += (char)std::toupper((unsigned char)c);
        }
    }
    return out;
}

LevelObject rockProto(double a, double b, double c, double buried = 0.78) {
    LevelObject o;
    o.kind = OBJ_ROCK, o.sub = 1, o.size = V3(a, b, c), o.top = 2 * b * buried, o.col[0] = 1.0f;
    return o;
}

LevelObject boxProto(double len, double thick, double wid, bool slab, double pitch = 0) {
    LevelObject o;
    o.kind = OBJ_BOX, o.size = V3(len / 2, thick / 2, wid / 2), o.pitch = pitch, o.sub = slab ? 1 : 0;
    o.col[0] = slab ? 0.55f : 0.62f, o.col[1] = slab ? 0.5f : 0.6f, o.col[2] = slab ? 0.45f : 0.56f;
    return o;
}

LevelObject logProto(double len, double r) {
    LevelObject o;
    o.kind = OBJ_LOG, o.size = V3(r, len / 2, 0);
    return o;
}

LevelObject treeProto(int kind, double h) {
    LevelObject o;
    o.kind = OBJ_TREE, o.sub = kind, o.size = V3(0, h, 0), o.col[0] = 1.0f;
    return o;
}

LevelObject modelProto(const std::string& id) {
    LevelObject o;
    o.kind = OBJ_MODEL, o.name = id, o.size = V3(1, 1, 1);
    return o;
}

}  // namespace

void Editor::buildPalette() {
    if (!palette.empty()) return;
    tabs = {"POINTER", "ROCKS", "SCANS", "BLOCKS", "LOGS", "TREES", "BUILD", "LEDGE"};
    auto add = [&](const std::string& name, int tab, const LevelObject& o, const std::string& icon) {
        PalEntry e;
        e.name = name, e.tab = tabs[tab], e.proto = o, e.icon = icon;
        palette.push_back(e);
    };
    // Rocks that fit a size (the scan nearest those proportions), and every scanned rock as itself.
    add("SMALL ROCK", 1, rockProto(0.6, 0.45, 0.55), "auto:0");
    add("ROCK", 1, rockProto(1.2, 0.85, 1.05), "auto:1");
    add("BOULDER", 1, rockProto(2.2, 1.5, 1.9), "auto:2");
    add("BIG BOULDER", 1, rockProto(3.6, 2.4, 3.1), "auto:3");
    add("FLAT ROCK", 1, rockProto(1.9, 0.4, 1.5), "auto:4");
    add("TALL ROCK", 1, rockProto(0.9, 2.0, 0.9), "auto:5");
    const props::HullLibrary& lib = props::sharedHullLibrary();
    for (int i = 0; i < (int)lib.hulls.size(); i++) {
        LevelObject o;
        o.kind = OBJ_ROCK, o.sub = 1, o.scan = i, o.size = lib.hulls[i].half(), o.top = o.size.y * 1.7, o.col[0] = 1.0f;
        add(prettyName(lib.hulls[i].name), 2, o, "scan:" + lib.hulls[i].name);
    }
    add("CONCRETE BLOCK", 3, boxProto(4, 0.8, 2.5, false), "box:block");
    add("ROCK SLAB", 3, boxProto(5, 0.6, 3.5, true), "box:slab");
    add("RAMP", 3, boxProto(8, 0.5, 4, false, 0.22), "box:ramp");
    add("STEEP RAMP", 3, boxProto(6, 0.5, 4, false, 0.45), "box:ramp");
    add("WALL", 3, boxProto(8, 2.2, 0.6, false), "box:wall");
    add("ROCK WALL", 3, boxProto(8, 2.5, 1.2, true), "box:rockwall");
    add("SHORT LOG", 4, logProto(3.5, 0.25), "log");
    add("LOG", 4, logProto(6, 0.3), "log");
    add("LONG LOG", 4, logProto(10, 0.35), "log");
    add("THICK LOG", 4, logProto(6, 0.55), "log");
    add("YOUNG PINE", 5, treeProto(0, 6), "model:tree_pine");
    add("PINE", 5, treeProto(0, 12), "model:tree_pine");
    add("TALL PINE", 5, treeProto(0, 19), "model:tree_pine");
    add("BROADLEAF", 5, treeProto(1, 10), "model:tree_broadleaf");
    add("BIG BROADLEAF", 5, treeProto(1, 15), "model:tree_broadleaf");
    add("DEAD SNAG", 5, treeProto(2, 10), "model:tree_snag");
    for (const auto& b : {std::pair<const char*, double>{"BUSH", 0.9}, {"BIG BUSH", 1.6}}) {
        LevelObject o;
        o.kind = OBJ_BUSH, o.size = V3(b.second, 0, 0), o.col[0] = 1.0f;
        add(b.first, 5, o, "model:bush");
    }
    {
        LevelObject o;
        o.kind = OBJ_STATION, o.name = "REPAIR SHOP";
        add("REPAIR SHOP", 6, o, "model:repair_shed");
    }
    static const char* structures[][2] = {{"cabin", "CABIN"},          {"outhouse", "OUTHOUSE"},       {"tent", "TENT"},          {"picnic_table", "PICNIC TABLE"},
                                          {"woodpile", "WOODPILE"},    {"campfire", "CAMPFIRE"},       {"stump", "TREE STUMP"},   {"cairn", "CAIRN"},
                                          {"fire_tower", "FIRE TOWER"}, {"mine_portal", "MINE PORTAL"}, {"ore_cart", "ORE CART"}, {"wreck_pickup", "WRECKED PICKUP"},
                                          {"oil_drum", "OIL DRUM"},    {"traffic_cone", "TRAFFIC CONE"}, {"jersey_barrier", "BARRIER"}, {"guardrail", "GUARDRAIL"},
                                          {"lamp_post", "LAMP POST"},  {"trail_sign", "TRAIL SIGN"},  {"map_board", "MAP BOARD"}, {"marker_post", "MARKER POST"},
                                          {"sign_gantry", "SIGN GANTRY"}};
    for (const auto& st : structures)
        if (mk::findModel(st[0])) add(st[1], 6, modelProto(st[0]), std::string("model:") + st[0]);
    {
        PalEntry e;
        e.name = "SLAB LEDGE", e.tab = "LEDGE", e.icon = "box:slab", e.ledge = true;
        palette.push_back(e);
    }
    proto = palette[1].proto;
    palPick = 1;
}

int Editor::findPalette(const std::string& name) {
    buildPalette();
    for (int i = 0; i < (int)palette.size(); i++)
        if (palette[i].name.find(name) != std::string::npos) return i;
    return -1;
}

int Editor::paletteSize() const { return (int)palette.size(); }
const char* Editor::paletteName(int i) const { return i >= 0 && i < (int)palette.size() ? palette[i].name.c_str() : ""; }

void Editor::choose(int index) {
    buildPalette();
    if (index < 0 || index >= (int)palette.size()) return;
    palPick = index;
    const PalEntry& e = palette[index];
    ledgeBrush = e.ledge;
    pointer = false;
    selection.clear();
    sel = EditPick();
    if (!e.ledge) proto = e.proto;
    for (int t = 0; t < (int)tabs.size(); t++)
        if (tabs[t] == e.tab) palTab = t, palPage = 0;
    // (The page that holds it.)
    int n = 0;
    for (int i = 0; i < index; i++) n += palette[i].tab == e.tab;
    palPage = n / 8;
}

void Editor::setPalette(int index) { choose(index); }

// The picture on a card: the model itself where there is one, else a small mesh built for it.
const mk::Mesh& Editor::iconMesh(const PalEntry& e) {
    auto it = iconMeshes.find(e.icon);
    if (it != iconMeshes.end()) return it->second;
    mk::Mesh m;
    auto fromProp = [&](const std::string& name) {
        props::Model pm;
        if (props::assetDir().empty() || !props::loadModel(props::assetDir() + "/" + name + ".prop", pm) || pm.lods.empty()) return false;
        const props::Lod& lod = pm.lods.back();
        for (const props::Sub& sub : lod.subs)
            for (size_t i = 0; i + 2 < sub.idx.size(); i += 3)
                for (int k = 0; k < 3; k++) {
                    const props::Vert& v = sub.v[sub.idx[i + k]];
                    mk::MVert mv;
                    for (int c = 0; c < 3; c++) mv.p[c] = v.p[c], mv.n[c] = v.n[c];
                    mv.c[0] = 0.27f, mv.c[1] = 0.245f, mv.c[2] = 0.22f;
                    mv.mat = mk::ROCK;
                    m.v.push_back(mv);
                }
        return true;
    };
    const std::string& k = e.icon;
    if (k.rfind("scan:", 0) == 0) {
        fromProp(k.substr(5));
    } else if (k.rfind("auto:", 0) == 0) {  // (the scan nearest those proportions)
        const props::HullLibrary& lib = props::sharedHullLibrary();
        double best = 1e9;
        int bi = 0;
        for (int i = 0; i < (int)lib.hulls.size(); i++) {
            const V3 h = lib.hulls[i].half();
            const double sc = std::fabs(std::log((h.x / h.y) / (e.proto.size.x / e.proto.size.y))) + std::fabs(std::log((h.z / h.y) / (e.proto.size.z / e.proto.size.y)));
            if (sc < best) best = sc, bi = i;
        }
        if (!lib.hulls.empty()) fromProp(lib.hulls[bi].name);
    } else if (k.rfind("model:", 0) == 0) {
        m = mk::modelMesh(k.substr(6));
    } else {
        mk::B b(m);
        if (k == "log") {
            b.color(0.42, 0.3, 0.18).mat(mk::WOOD);
            b.cyl(V3(-1.6, 0.3, 0), V3(1.6, 0.3, 0), 0.3, 14);
        } else if (k == "box:block") {
            b.color(0.62, 0.6, 0.56).mat(mk::CONCRETE);
            b.box(V3(0, 0.4, 0), V3(2.4, 0.8, 1.5), 0.04);
        } else if (k == "box:slab") {
            b.color(0.55, 0.5, 0.45).mat(mk::ROCK);
            b.box(V3(0, 0.3, 0), V3(2.6, 0.6, 1.8), 0.1);
        } else if (k == "box:ramp") {
            b.color(0.62, 0.6, 0.56).mat(mk::CONCRETE);
            b.beam(V3(-1.6, 0.05, 0), V3(1.6, 0.9, 0), 1.6, 0.18);
            b.box(V3(1.55, 0.4, 0), V3(0.2, 0.8, 1.6));
        } else if (k == "box:wall") {
            b.color(0.62, 0.6, 0.56).mat(mk::CONCRETE);
            b.box(V3(0, 0.9, 0), V3(3.2, 1.8, 0.4), 0.03);
        } else {
            b.color(0.55, 0.5, 0.45).mat(mk::ROCK);
            b.box(V3(0, 0.9, 0), V3(3.2, 1.8, 0.8), 0.1);
        }
    }
    m.finish();
    return iconMeshes.emplace(e.icon, std::move(m)).first->second;
}

double Editor::placeRise() const {
    if (!placeOn) return 0;
    const V3 sp = snapped(placeAt);
    return std::max(0.0, placeAt.y - core.world->height(sp.x, sp.z));
}

LevelObject Editor::makeObject(const V3& at, double rise) const {
    LevelObject o = proto;
    o.p = V3(at.x, 0, at.z);
    if (o.kind == OBJ_ROCK) o.top += rise;  // (on top of something: the same burial, higher)
    else if (o.kind != OBJ_LOG) o.lift += rise;
    switch (o.kind) {
        case OBJ_ROCK:
            o.seed = nextSeed;
            o.sub = 1;
            break;
        case OBJ_LOG: {
            // The ends: along the log's turn and tilt either way from `at`, each lying on the ground it's over (plus the lift).
            const double r = proto.size.x, hl = proto.size.y;
            const V3 f(std::cos(proto.yaw) * std::cos(proto.pitch), std::sin(proto.pitch), -std::sin(proto.yaw) * std::cos(proto.pitch));
            const V3 a = V3(at.x, 0, at.z) - V3(f.x, 0, f.z) * hl, b = V3(at.x, 0, at.z) + V3(f.x, 0, f.z) * hl;
            const World& w = *core.world;
            const double mid = (w.height(a.x, a.z) + w.height(b.x, b.z)) / 2 + r + proto.lift + rise;
            o.p = V3(a.x, mid - f.y * hl, a.z), o.p2 = V3(b.x, mid + f.y * hl, b.z);
            o.size = V3(r, 0, 0);
            o.yaw = o.pitch = 0, o.lift = 0;
            return o;
        }
        default: break;
    }
    return core.grounded(o);
}

void Editor::click() {
    if (!haveHit && hover.kind == EditPick::NONE) return;
    switch (tool) {
        case SCULPT:
        case PAINT:
            core.beginEdit(tool == SCULPT ? std::string(kOpNames[op]) : std::string("PAINT ") + paintMaterials()[material].name);
            stroke = true;
            strokeSeed = strokeSeed * 1664525u + 1013904223u;  // (a new scatter each stroke)
            brushTick = 0, seedAt = hit, seedClock = clock;
            strokeAngle = std::round((M_PI / 2 - yaw) / (M_PI / 12)) * (M_PI / 12);
            if (tool == SCULPT && op == BRUSH_FLATTEN) flatY = hit.y, flatSet = true;
            applyBrush(1.0 / 60);
            break;
        case WATER: {
            if (!haveHit) break;
            const int mode = shift && waterMode == 0 ? 1 : waterMode;  // (Shift+click fills as a drain)
            if (mode == 1) {
                core.beginEdit("DRAIN");
                stroke = true;
                strokeSeed = strokeSeed * 1664525u + 1013904223u;
                strokeAngle = std::round((M_PI / 2 - yaw) / (M_PI / 12)) * (M_PI / 12);
                applyBrush(1.0 / 60);
                break;
            }
            if (mode == 2) {
                const int n = core.drain(hit.x, hit.z);
                say(n ? "DRAINED THE POOL: " + std::to_string(n) + " CELLS" : "NO WATER THERE (CLICK ON THE WATER)");
                break;
            }
            bool spilled = false;
            const int n = core.flood(hit.x, hit.z, floodDepth, std::max(radius * 4, 40.0), &spilled);
            say(spilled ? "IT RAN OUT OVER LOWER GROUND (" + std::to_string(n) + " CELLS): DIG A DEEPER HOLLOW, OR DRAIN IT AND TRY AGAIN" : "FLOODED " + std::to_string(n) + " CELLS");
            break;
        }
        case OBJECTS:
            if (ledgeBrush) {  // (drag a line: the row of slabs goes along it)
                if (haveHit) ledgeOn = true, ledgeA = snapped(hit);
                break;
            }
            if (pointer) {
                if (hover.kind == EditPick::OBJECT || hover.kind == EditPick::BASE) {
                    const bool already = isSelected(hover);
                    if (shift) {
                        if (already) selection.erase(std::remove(selection.begin(), selection.end(), hover), selection.end());
                        else selection.push_back(hover);
                    } else if (!already) {
                        selection.assign(1, hover);
                    }
                    sel = selection.empty() ? EditPick() : selection.back();
                    dragging = !selection.empty() && isSelected(hover);
                    dragStarted = false, dragApplied = V3();
                    if (!selection.empty())
                        say(selection.size() > 1 ? std::to_string(selection.size()) + " SELECTED: DRAG TO MOVE, DELETE" : std::string(hover.kind == EditPick::BASE ? "THE MAP'S " : "") + hover.name + ": DRAG TO MOVE, KEYS TURN AND SCALE");
                } else if (haveHit) {  // empty space: a box to select what's inside
                    if (!shift) selection.clear(), sel = EditPick();
                    marquee = true, marqueeX = mx, marqueeY = my;
                }
                break;
            }
            if (haveHit) {
                sel = EditPick();
                LevelObject o = makeObject(snapped(placeAt), placeRise());
                core.addObject(o);
                nextSeed = nextSeed * 1103515245u + 12345u;
                say(std::string("PLACED ") + (palPick >= 0 && palPick < (int)palette.size() ? palette[palPick].name : std::string("OBJECT")) + "  (CTRL+Z UNDOES)", 1.6);
            }
            break;
        case COURSE:
            if (hover.kind == EditPick::GATE || hover.kind == EditPick::START) {
                sel = hover;
                dragging = true;
                const V3 at = sel.kind == EditPick::GATE ? core.level.gates[sel.index].p : core.level.start;
                dragOffset = V3(at.x - hit.x, 0, at.z - hit.z);
            } else if (haveHit) {
                if (!core.level.hasStart) {
                    core.setStart(hit.x, hit.z, yaw);
                    sel = EditPick();
                    sel.kind = EditPick::START;
                    say(core.level.section.on ? "ENTRY SET. CLICK AGAIN FOR THE EXIT (THE LAST GATE)" : "START SET (FACING THE WAY YOU LOOK). CLICK AGAIN FOR GATES");
                } else {
                    Gate g;
                    g.p = V3(hit.x, 0, hit.z);
                    g.yaw = core.level.section.on ? core.level.section.heading : yaw;
                    g.width = gateWidth;
                    // After the selected gate if there is one, else at the end.
                    const int at = sel.kind == EditPick::GATE ? sel.index + 1 : -1;
                    const int k = core.addGate(g, at);
                    sel = EditPick();
                    sel.kind = EditPick::GATE, sel.index = k;
                    say("GATE " + std::to_string(k + 1) + " OF " + std::to_string(core.level.gates.size()));
                }
            }
            break;
        default: break;
    }
}

void Editor::release() {
    if (marquee) {
        marquee = false;
        if (std::fabs(mx - marqueeX) > 4 && std::fabs(my - marqueeY) > 4) selectInBox(std::min(mx, marqueeX), std::min(my, marqueeY), std::max(mx, marqueeX), std::max(my, marqueeY), shift);
    }
    if (ledgeOn) {
        ledgeOn = false;
        if (haveHit) placeLedge(ledgeA, snapped(hit));
    }
    if (stroke) {
        core.endEdit();
        stroke = false;
    }
    dragging = false;
}

// A row of rock slabs along the line a-b, their tops `rise` m over the ground at the line, the ledge
// rising away from where you look: one rock-slab box per ~2.4 m, overlapping, sunk 0.8 m (the handoff:
// slab boxes are the primitive for ledges; a notch step wants to stay under 0.4 m).
void Editor::placeLedge(const V3& a, const V3& b) {
    const V3 d(b.x - a.x, 0, b.z - a.z);
    const double L = len(d);
    if (L < 1.5) {
        say("DRAG A LINE AT LEAST 1.5 M LONG");
        return;
    }
    const V3 dir = d * (1 / L);
    V3 n(dir.z, 0, -dir.x);
    if (dot(n, V3(std::cos(yaw), 0, -std::sin(yaw))) < 0) n = n * -1;
    const double rise = kRisers[ledgeRise], thick = rise + 0.8, depth = ledgeDepth;
    const int count = std::max(1, (int)std::ceil(L / 2.4));
    const double seg = L / count, boxYaw = std::atan2(-dir.z, dir.x);
    std::vector<LevelObject> row;
    for (int k = 0; k < count; k++) {
        const V3 lip = a + dir * (seg * (k + 0.5)), c = lip + n * (depth / 2);
        LevelObject o;
        o.kind = OBJ_BOX, o.sub = 1;
        o.size = V3(seg / 2 + 0.15, thick / 2, depth / 2);
        o.yaw = boxYaw;
        o.p = V3(c.x, core.world->height(lip.x, lip.z) + rise - thick / 2, c.z);
        const float v = 0.02f * (float)((k * 7) % 5 - 2);
        o.col[0] = 0.70f + v, o.col[1] = 0.45f + v, o.col[2] = 0.32f + v;
        row.push_back(o);
    }
    core.addObjects(row, "SLAB LEDGE");
    say("LEDGE: " + std::to_string(count) + " SLABS, " + fmt1(rise, " M") + " RISE" + (rise > 0.4 ? " (OVER THE 0.4 M NOTCH STEP)" : ""));
}

Brush Editor::curBrush() const {
    Brush b;
    b.shape = shape, b.fall = fall, b.style = style, b.radius = radius, b.strength = strength, b.density = density;
    b.angle = lmb && stroke ? strokeAngle : std::round((M_PI / 2 - yaw) / (M_PI / 12)) * (M_PI / 12);
    b.seed = strokeSeed * 7919u + brushTick;
    return b;
}

void Editor::applyBrush(double dt) {
    if (!haveHit) return;
    Brush b = curBrush();
    if (tool == SCULPT) {
        const double rate = strength * 4.0;  // m/s at full strength
        const double fine = shift ? 0.25 : 1;
        switch (op) {
            case BRUSH_RAISE:
            case BRUSH_LOWER: core.brush(op, b, hit.x, hit.z, rate * dt * fine); break;
            case BRUSH_SMOOTH: core.brush(op, b, hit.x, hit.z, std::min(1.0, strength * dt * 6)); break;
            case BRUSH_FLATTEN: core.brush(op, b, hit.x, hit.z, std::min(1.0, strength * dt * 8), flatSet ? flatY : hit.y); break;
            case BRUSH_ROUGHEN: core.brush(op, b, hit.x, hit.z, rate * dt * 0.5 * fine); break;
            case BRUSH_TERRACE: core.brush(op, b, hit.x, hit.z, std::min(1.0, strength * dt * 8), kTerraces[terraceIdx]); break;
            default: break;
        }
    } else if (tool == PAINT) {
        // A patterned or light brush sprays: a new scatter once you've moved a bit (or held a moment), so
        // dragging builds the coverage up; a solid hard brush needs none.
        const bool spray = style != STYLE_SOLID || fall != FALL_HARD || strength < 1;
        if (spray && (len(hit - seedAt) > std::max(1.0, radius * 0.35) || clock - seedClock > 0.3)) {
            brushTick++, seedAt = hit, seedClock = clock;
            b.seed = strokeSeed * 7919u + brushTick;
        }
        core.paint(b, hit.x, hit.z, material);
    } else if (tool == WATER) {
        core.dryBrush(b, hit.x, hit.z);
    }
}

// B shape, F falloff, N pattern, - = strength, , . density (sculpt, paint, water drain).
void Editor::brushKey(int sc) {
    auto step = [&](int cur, int n) { return (cur + (shift ? n - 1 : 1)) % n; };
    switch (sc) {
        case SC_B: shape = (BrushShape)step(shape, SHAPE_COUNT); break;
        case SC_F: fall = (BrushFall)step(fall, FALL_COUNT); break;
        case SC_N: style = (BrushStyle)step(style, STYLE_COUNT); break;
        case SC_MINUS: strength = clampd(strength - 0.1, 0.1, 1.0); break;
        case SC_EQUALS: strength = clampd(strength + 0.1, 0.1, 1.0); break;
        case 54: density = clampd(density - 0.1, 0.1, 1.0); break;  // (comma, period)
        case 55: density = clampd(density + 0.1, 0.1, 1.0); break;
        default: return;
    }
    say(std::string("BRUSH: ") + brushShapeName(shape) + ", " + brushFallName(fall) + " EDGE, " + brushStyleName(style), 1.2);
}

static char keyChar(int sc) {
    if (sc >= SC_A && sc <= SC_A + 25) return (char)('A' + (sc - SC_A));
    if (sc >= SC_1 && sc <= SC_1 + 8) return (char)('1' + (sc - SC_1));
    if (sc == SC_0) return '0';
    if (sc == SC_SPACE) return ' ';
    if (sc == SC_MINUS) return '-';
    return 0;
}

// Ctrl+S: saves it, asking for a name first if it hasn't one (SAVE AS: always asks).
void Editor::save() {
    flow.requestSave(core, false);
    afterFlow();
}

// What the save prompts decided: tell the player, and go on to what was asked (leave, open another level, start a new one).
void Editor::afterFlow() {
    if (!flow.note.empty()) say(flow.note, flow.stage != SaveFlow::IDLE ? 30 : 4);
    flow.note.clear();
    if (flow.saved) {
        savedPing = true;
        flow.saved = false;
        lastAuto = clock;
    }
    const SaveFlow::After a = flow.done;
    const int arg = flow.doneArg;
    flow.done = SaveFlow::NOTHING;
    if (flow.stage == SaveFlow::IDLE && toastTime > 25) toastTime = 4;
    if (a == SaveFlow::LEAVE) wantClose = true;
    else if (a == SaveFlow::OPEN) doOpen(arg);
    else if (a == SaveFlow::NEWMAP) {
        newMap = nextListedMap(newMap, 1);
        Level l;
        l.map = newMap;
        l.name = "UNTITLED";
        World& w = *core.world;
        w = World(newMap);
        open(w, *rend, l);
        say(std::string("NEW LEVEL ON ") + mapName(newMap));
    }
}

// The keys while a prompt is up: the name, or SAVE / DISCARD / CANCEL.
void Editor::flowKey(int sc) {
    if (flow.stage == SaveFlow::NAMING) {
        const char ch = keyChar(sc);
        if (ch) flow.type(ch);
        else if (sc == SC_BACKSPACE) flow.backspace();
        else if (sc == SC_RETURN || sc == SC_KP_ENTER) flow.enter(core);
        else if (sc == SC_ESCAPE) flow.escape();
    } else if (flow.stage == SaveFlow::ASKING) {
        if (sc == SC_S || sc == SC_RETURN || sc == SC_KP_ENTER) flow.choose(core, SaveFlow::SAVE);
        else if (sc == SC_D) flow.choose(core, SaveFlow::DISCARD);
        else if (sc == SC_ESCAPE || sc == SC_C) flow.choose(core, SaveFlow::CANCEL);
    }
    afterFlow();
}

// The open list: the game's and the player's levels, and first any recovery copy newer than the level it came from.
void Editor::refreshRecoveries() {
    files = listLevels();
    fileRecovery.assign(files.size(), false);
    if (autosaveDir.empty()) return;
    namespace fs = std::filesystem;
    std::vector<Level> rec;
    for (const std::string& path : listLevelFiles(autosaveDir, ".czl")) {
        Level l;
        if (!loadLevelFile(path, l)) continue;
        std::error_code ec;
        const std::string saved = userLevelFolder() + "/" + levelFileName(l.name) + ".czl";
        const auto at = fs::last_write_time(path, ec);
        const bool exists = fs::exists(saved, ec);
        if (exists && !ec && fs::last_write_time(saved, ec) >= at) continue;  // (what was saved is the later work)
        l.file = saved;
        l.builtin = false;
        rec.push_back(l);
    }
    files.insert(files.begin(), rec.begin(), rec.end());
    fileRecovery.insert(fileRecovery.begin(), rec.size(), true);
}

void Editor::openFile(int index) {
    if (index < 0 || index >= (int)files.size()) return;
    fileOpen = false;
    flow.requestLeave(core, SaveFlow::OPEN, index);  // (unsaved work asks first)
    afterFlow();
}

void Editor::doOpen(int index) {
    if (index < 0 || index >= (int)files.size()) return;
    openLevel(files[index], index < (int)fileRecovery.size() && fileRecovery[index]);
}

void Editor::openLevel(const Level& lv, bool recovered) {
    // A fresh copy of its map, then the level on it.
    World& w = *core.world;
    w = levelWorld(lv);
    Level l = lv;
    if (l.builtin) l.builtin = false, l.file.clear(), l.name = l.name + " (MINE)";
    open(w, *rend, l);
    fileOpen = false;
    if (recovered) {  // (it's unsaved work: saving writes the level's own file, which is still the old one)
        core.changes = 1;
        say("RECOVERED FROM THE AUTOSAVE: CTRL+S TO KEEP IT", 6);
    }
}

void Editor::key(int sc) {
    const uint8_t* ks = SDL_GetKeyboardState(nullptr);
    if (!scriptMods) {
        shift = ks[SC_LSHIFT] || ks[SC_RSHIFT];
        ctrl = ks[224] || ks[228];
    }
    if (flow.stage != SaveFlow::IDLE) {
        flowKey(sc);
        return;
    }
    // The open dialog.
    if (fileOpen) {
        if (sc == SC_ESCAPE) fileOpen = false;
        else if (sc == SC_UP || sc == SC_W) fileSel = std::max(0, fileSel - 1);
        else if (sc == SC_DOWN || sc == SC_S) fileSel = std::min((int)files.size() - 1, fileSel + 1);
        else if ((sc == SC_RETURN || sc == SC_KP_ENTER) && fileSel >= 0 && fileSel < (int)files.size()) openFile(fileSel);
        return;
    }
    // Typing the name.
    if (typing) {
        std::string& n = core.level.name;
        char ch = 0;
        if (sc >= SC_A && sc <= SC_A + 25) ch = (char)('A' + (sc - SC_A));
        else if (sc >= SC_1 && sc <= SC_1 + 8) ch = (char)('1' + (sc - SC_1));
        else if (sc == SC_0) ch = '0';
        else if (sc == SC_SPACE) ch = ' ';
        else if (sc == SC_MINUS) ch = '-';
        if (ch && n.size() < 28) core.settings("name", [&](Level& l) { l.name += ch; });
        else if (sc == SC_BACKSPACE && !n.empty()) core.settings("name", [](Level& l) { l.name.pop_back(); });
        else if (sc == SC_RETURN || sc == SC_KP_ENTER || sc == SC_ESCAPE) typing = false;
        return;
    }
    if (ctrl && sc == SC_Z) {
        say(core.undo() ? "UNDONE" : "NOTHING TO UNDO", 1.2);
        sel = EditPick(), selection.clear();
        return;
    }
    if (ctrl && sc == SC_Y) {
        say(core.redo() ? "REDONE" : "NOTHING TO REDO", 1.2);
        return;
    }
    if (ctrl && sc == SC_S) {
        if (shift) flow.requestSave(core, true), afterFlow();  // SAVE AS
        else save();
        return;
    }
    if (sc >= SC_1 && sc < SC_1 + TOOL_COUNT) {
        tool = (Tool)(sc - SC_1);
        sel = EditPick(), selection.clear();
        return;
    }
    switch (sc) {
        case SC_ESCAPE:
            if (sel.kind != EditPick::NONE || !selection.empty()) {
                sel = EditPick(), selection.clear();
                return;
            }
            flow.requestLeave(core, SaveFlow::LEAVE);
            afterFlow();
            return;
        case SC_TAB:
            if (tool == SCULPT) op = (BrushOp)((op + (shift ? BRUSH_OP_COUNT - 1 : 1)) % BRUSH_OP_COUNT);
            else if (tool == PAINT) material = (material + 1) % (int)paintMaterials().size();
            else if (tool == OBJECTS) {  // pointer <-> placing
                buildPalette();
                pointer = !pointer;
                ledgeBrush = !pointer && palette[palPick].ledge;
                if (!pointer) choose(palPick);
                else palTab = 0;
                say(pointer ? "POINTER: SELECT, MOVE AND CHANGE WHAT'S THERE (TAB: BACK TO PLACING)" : "PLACING (TAB: POINTER)", 2);
            }
            return;
        case SC_LEFTBRACKET: radius = brushStep(radius, -1), gateWidth = std::max(4.0, gateWidth - 1); return;
        case SC_RIGHTBRACKET: radius = brushStep(radius, 1), gateWidth = std::min(40.0, gateWidth + 1); return;
        case SC_DELETE:
        case SC_BACKSPACE: doButton(B_DELETE); return;
        case SC_F5: doButton(B_PLAY); return;
        case SC_F6_: doButton(B_PLAY_START); return;
        case SC_F3: doButton(B_OPEN); return;
        default: break;
    }
    if (tool == SETTINGS) {
        if (sc == SC_UP) setRow = std::max(0, setRow - 1);
        else if (sc == SC_DOWN) setRow = std::min(10, setRow + 1);
        else if (sc == SC_LEFT) settingChange(setRow, -1);
        else if (sc == SC_RIGHT) settingChange(setRow, 1);
        else if (sc == SC_RETURN && setRow == 0) typing = true;
        return;
    }
    if (tool == SCULPT || tool == PAINT || tool == WATER) brushKey(sc);
    if (tool == OBJECTS) {
        objectKey(sc);
        return;
    }
    // Q/E: brush size (Shift+Q/E: turn the selected gate or the start). Never the camera: that's Space and Shift+wheel.
    if (sc == SC_Q || sc == SC_E) {
        if (tool == SCULPT || tool == PAINT || tool == WATER) {
            radius = brushStep(radius, sc == SC_Q ? -1 : 1);
        } else if (shift) {
            const double turn = M_PI / 12;
            const double d = sc == SC_Q ? turn : -turn;
            if (tool == COURSE && sel.kind == EditPick::GATE) {
                Gate g = core.level.gates[sel.index];
                g.yaw += d;
                core.setGate(sel.index, g, "turn");
            } else if (tool == COURSE && sel.kind == EditPick::START) {
                core.setStart(core.level.start.x, core.level.start.z, core.level.startYaw + d);
            }
        }
    }
}

// The object keys: with something selected they change it, else the ghost that the next click places.
//   Shift+Q E or , .   turn          R F   tip        C V   roll       Z X or PgDn PgUp   lower, raise
//   - =   scale it all               T G   longer, shorter    Y H   taller, thinner    U J   wider, narrower
//   Ctrl+arrows   nudge the selection (Shift: a metre)   Shift: coarser steps   Delete   delete
void Editor::objectKey(int sc) {
    const double k = shift ? 3.0 : 1.0;
    const double yawStep = (shift ? 15.0 : 5.0) * M_PI / 180, tipStep = 5.0 * M_PI / 180 * k, grow = shift ? 1.4 : 1.1;
    switch (sc) {
        case SC_Q:  // (Q E: the object's size; Shift+Q E: turn it. Never the camera.)
            if (shift) applyOp(OP_YAW, M_PI / 12, "yaw");
            else applyOp(OP_SCALE, 1 / 1.15, "scale");
            return;
        case SC_E:
            if (shift) applyOp(OP_YAW, -M_PI / 12, "yaw");
            else applyOp(OP_SCALE, 1.15, "scale");
            return;
        case SC_COMMA_: applyOp(OP_YAW, yawStep, "yaw"); return;
        case SC_PERIOD_: applyOp(OP_YAW, -yawStep, "yaw"); return;
        case SC_R: applyOp(OP_PITCH, tipStep, "pitch"); return;
        case SC_F: applyOp(OP_PITCH, -tipStep, "pitch"); return;
        case SC_C: applyOp(OP_ROLL, tipStep, "roll"); return;
        case SC_V: applyOp(OP_ROLL, -tipStep, "roll"); return;
        case SC_X:
        case SC_PAGEUP_: applyOp(OP_LIFT, shift ? 1.0 : 0.25, "lift"); return;
        case SC_Z:
        case SC_PAGEDOWN_: applyOp(OP_LIFT, shift ? -1.0 : -0.25, "lift"); return;
        case SC_B: applyOp(OP_SEAT, 0, "seat"); return;
        case SC_EQUALS: applyOp(OP_SCALE, grow, "scale"); return;
        case SC_MINUS: applyOp(OP_SCALE, 1 / grow, "scale"); return;
        case SC_T: applyOp(OP_STRETCH_X, grow, "stretchx"); return;
        case SC_G: applyOp(OP_STRETCH_X, 1 / grow, "stretchx"); return;
        case SC_Y: applyOp(OP_STRETCH_Y, grow, "stretchy"); return;
        case SC_H: applyOp(OP_STRETCH_Y, 1 / grow, "stretchy"); return;
        case SC_U: applyOp(OP_STRETCH_Z, grow, "stretchz"); return;
        case SC_J: applyOp(OP_STRETCH_Z, 1 / grow, "stretchz"); return;
        case SC_LEFT:
        case SC_RIGHT:
        case SC_UP:
        case SC_DOWN:
            if (ctrl) {
                const double step = shift ? 1.0 : 0.25;
                nudge(sc == SC_RIGHT ? step : sc == SC_LEFT ? -step : 0, sc == SC_UP ? step : sc == SC_DOWN ? -step : 0);
            }
            return;
        default: return;
    }
}

// What the object keys and buttons change: the selection (adopting the map's own among it), else the ghost.
void Editor::applyOp(ObjOp op, double amount, const std::string& key) {
    if (tool != OBJECTS) return;
    buildPalette();
    static const char* names[] = {"TURN", "TIP", "ROLL", "RAISE", "SCALE", "STRETCH", "STRETCH", "STRETCH", "SEAT"};
    if (!selection.empty()) {
        core.beginEdit(names[op], key);
        const std::vector<int> ks = selectedObjects();
        core.transformObjects(ks, op, amount, key);
        core.endEdit();
        return;
    }
    if (pointer || ledgeBrush) {
        say("SELECT SOMETHING FIRST (CLICK IT, OR DRAG A BOX OVER SEVERAL)", 2);
        return;
    }
    if (proto.kind == OBJ_LOG) {  // (the ghost log: its half-length and radius, turn, tilt and lift)
        switch (op) {
            case OP_YAW: proto.yaw += amount; break;
            case OP_PITCH: proto.pitch = clampd(proto.pitch + amount, -1.2, 1.2); break;
            case OP_LIFT: proto.lift += amount; break;
            case OP_SCALE: proto.size.x *= amount, proto.size.y *= amount; break;
            case OP_STRETCH_X: proto.size.y *= amount; break;
            case OP_STRETCH_Y:
            case OP_STRETCH_Z: proto.size.x *= amount; break;
            default: break;
        }
        proto.size.x = clampd(proto.size.x, 0.05, 2.0), proto.size.y = clampd(proto.size.y, 0.25, 20);
    } else {
        proto = core.transformed(proto, op, amount);
    }
}

void Editor::nudge(double right, double ahead) {
    if (selection.empty()) return;
    const V3 f(std::cos(yaw), 0, -std::sin(yaw)), rt(-f.z, 0, f.x);
    const V3 d = rt * right + f * ahead;
    core.beginEdit("NUDGE", "nudge");
    const std::vector<int> ks = selectedObjects();
    core.moveObjects(ks, d.x, d.z, "nudge");
    core.endEdit();
}

void Editor::deleteSelection() {
    if (selection.empty()) {
        say("NOTHING SELECTED: POINTER (TAB), THEN CLICK IT", 2);
        return;
    }
    const size_t n = selection.size();
    core.removePicks(selection);
    selection.clear();
    sel = EditPick();
    say(n > 1 ? "DELETED " + std::to_string(n) + " OBJECTS" : "DELETED", 1.5);
}

void Editor::selectObjects(const std::vector<int>& ks) {
    selection.clear();
    for (int k : ks)
        if (k >= 0 && k < (int)core.level.objects.size()) {
            EditPick p;
            p.kind = EditPick::OBJECT, p.index = k;
            p.name = kObjNames[std::min(std::max(core.level.objects[k].kind, 0), OBJ_KIND_COUNT - 1)];
            selection.push_back(p);
        }
    sel = selection.empty() ? EditPick() : selection.back();
}

bool Editor::isSelected(const EditPick& p) const {
    for (const EditPick& q : selection)
        if (q == p) return true;
    return false;
}

std::vector<int> Editor::selectedObjects() {
    bool base = false;
    for (const EditPick& p : selection) base |= p.kind == EditPick::BASE;
    if (base) {
        const std::vector<int> ks = core.adopt(selection);
        std::vector<EditPick> next;
        int lost = 0;
        for (size_t i = 0; i < selection.size(); i++) {
            if (ks[i] < 0) {
                lost++;
                continue;
            }
            EditPick o;
            o.kind = EditPick::OBJECT, o.index = ks[i];
            o.name = kObjNames[std::min(std::max(core.level.objects[ks[i]].kind, 0), OBJ_KIND_COUNT - 1)];
            next.push_back(o);
        }
        selection = next;
        if (lost) say(std::to_string(lost) + " OF THEM CAN'T BE MOVED (CABINS, FENCES...): ONLY DELETED", 3);
    }
    sel = selection.empty() ? EditPick() : selection.back();
    std::vector<int> out;
    for (const EditPick& p : selection)
        if (p.kind == EditPick::OBJECT) out.push_back(p.index);
    return out;
}

// Selects what's inside a box on the screen: the level's objects and the map's own (a thing counts if its middle is inside).
void Editor::selectInBox(float x0, float y0, float x1, float y1, bool add) {
    const View v = view(W, H);
    const World& w = *core.world;
    std::vector<EditPick> found;
    auto inside = [&](const V3& p) {
        float sx, sy;
        return screenPoint(v, W, H, p, sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
    };
    auto take = [&](int baseKind, int index, const V3& at, const V3& look, const std::string& name) {
        if (found.size() > 800 || !inside(look)) return;
        EditPick p;
        const int ow = core.ownerOf(baseKind, index);
        p.kind = ow >= 0 ? EditPick::OBJECT : EditPick::BASE;
        p.index = ow >= 0 ? ow : index, p.baseKind = ow >= 0 ? -1 : baseKind, p.at = at, p.name = name;
        if (ow >= 0 && std::find(found.begin(), found.end(), p) != found.end()) return;
        if (ow < 0 && !core.canAdopt(p)) return;
        found.push_back(p);
    };
    for (int i = 0; i < (int)w.rocks.size(); i++) take(0, i, w.rocks[i].c, w.rocks[i].c, "ROCK");
    for (int i = 0; i < (int)w.trees.size(); i++) take(1, i, w.trees[i].base, w.trees[i].base + V3(0, w.trees[i].height * 0.4, 0), "TREE");
    for (int i = 0; i < (int)w.logs.size(); i++) take(2, i, (w.logs[i].a + w.logs[i].b) * 0.5, (w.logs[i].a + w.logs[i].b) * 0.5, "LOG");
    for (int i = 0; i < (int)w.boxes.size(); i++)
        if (!w.boxes[i].hidden) take(3, i, w.boxes[i].c, w.boxes[i].c, w.boxes[i].rock ? "ROCK SLAB" : "BLOCK");
    for (int i = 0; i < (int)w.fixtures.size(); i++) {
        const Prop& f = w.fixtures[i];
        take(4, i, f.a, f.kind == PROP_FENCE ? (f.a + f.b) * 0.5 + V3(0, 0.6, 0) : f.a + V3(0, f.kind == PROP_GARAGE ? 2 : 1, 0), f.kind == PROP_GARAGE ? "REPAIR SHOP" : f.kind == PROP_FENCE ? "FENCE" : "PROP");
    }
    for (int i = 0; i < (int)w.bushes.size(); i++) take(6, i, w.bushes[i].p, w.bushes[i].p + V3(0, w.bushes[i].r * 0.5, 0), "BUSH");
    for (int i = 0; i < (int)w.models.size(); i++)
        if (core.ownerOf(5, i) >= 0) take(5, i, w.models[i].p, w.models[i].p + V3(0, 1, 0), "STRUCTURE");
    if (!add) selection.clear();
    for (const EditPick& p : found)
        if (!isSelected(p)) selection.push_back(p);
    sel = selection.empty() ? EditPick() : selection.back();
    say(selection.empty() ? "NOTHING IN THAT BOX" : std::to_string(selection.size()) + " SELECTED: DRAG TO MOVE, KEYS CHANGE THEM, DELETE", 2.5);
}

void Editor::settingChange(int row, int dir) {
    core.settings("set" + std::to_string(row), [&](Level& l) {
        switch (row) {
            case 1: l.mode = (l.mode + dir + LEVEL_MODE_COUNT) % LEVEL_MODE_COUNT; break;
            case 2: l.laps = std::min(std::max(l.laps + dir, 1), 20); break;
            case 3: {  // (ANY, then the listed vehicles)
                const std::vector<int>& lv = listedVehicles();
                const int n = (int)lv.size() + 1;
                int at = 0;
                for (int i = 0; i < (int)lv.size(); i++)
                    if (lv[i] == l.vehicle) at = i + 1;
                at = ((at + dir) % n + n) % n;
                l.vehicle = at ? lv[at - 1] : -1;
                break;
            }
            case 4: l.timeOfDay = l.timeOfDay < 0 && dir > 0 ? 6 : l.timeOfDay + dir < 0 ? -1 : std::fmod(l.timeOfDay + dir, 24.0); break;
            case 5: l.resets = !l.resets; break;
            case 6: l.maxDamage = std::min(std::max(l.maxDamage + dir * 5, 0.0), 95.0); break;
            case 7: l.timeLimit = std::max(0.0, l.timeLimit + dir * 15); break;
            case 8:
            case 9:
            case 10: l.medals[row - 8] = std::max(0.0, l.medals[row - 8] + dir * 5); break;
        }
    });
}

void Editor::sectionAction(int code) {
    if (!core.level.section.on) return;
    auto step = [&](int cur, int dir, int n) { return ((cur + dir) % n + n) % n; };
    switch (code) {
        case S_NAME: typing = true; return;
        case S_KIND_DN:
        case S_KIND_UP: core.settings("seckind", [&](Level& l) { l.section.kind = step(l.section.kind, code == S_KIND_UP ? 1 : -1, SK_COUNT); }); return;
        case S_DIFF_DN:
        case S_DIFF_UP: core.settings("secdiff", [&](Level& l) { l.section.difficulty = std::min(5, std::max(1, l.section.difficulty + (code == S_DIFF_UP ? 1 : -1))); }); return;
        case S_GEN_DN:
        case S_GEN_UP: genType = step(genType, code == S_GEN_UP ? 1 : -1, SEC_COUNT); return;
        case S_SEED_DN:
        case S_SEED_UP: genSeed = (uint32_t)std::min(99, std::max(1, (int)genSeed + (code == S_SEED_UP ? 1 : -1))); return;
        case S_STAMP: {
            if (!core.level.hasStart) {
                say("SET THE ENTRY FIRST (COURSE TOOL)", 4);
                return;
            }
            const int n = core.stampSection(genType, genSeed);
            sel = EditPick();
            say(n < 0 ? "CAN'T STAMP HERE" : std::string("STAMPED ") + sectionName((SectionType)genType) + " " + std::to_string(genSeed) + ": " + std::to_string(n) + " OBJECTS. EDIT IT BY HAND", 5);
            return;
        }
        default: break;
    }
    if (code >= S_TEST && code < S_TEST + ST_COUNT) core.settings("sectest" + std::to_string(code), [&](Level& l) { l.section.tests ^= 1 << (code - S_TEST); });
    else if (code >= S_KIT && code < S_KIT + SKIT_COUNT) core.settings("seckit" + std::to_string(code), [&](Level& l) { l.section.kit ^= 1 << (code - S_KIT); });
}

void Editor::doButton(int id) {
    if (id != B_NEWSEC) confirmNew = false;
    confirmClose = false;
    if (id >= B_SEC && id < B_SEC + 100) {
        sectionAction(id - B_SEC);
        return;
    }
    if (id >= B_TOOL && id < B_TOOL + TOOL_COUNT) {
        tool = (Tool)(id - B_TOOL);
        sel = EditPick(), selection.clear();
        return;
    }
    if (id >= B_OP && id < B_OP + BRUSH_OP_COUNT) {
        op = (BrushOp)(id - B_OP);
        return;
    }
    if (id >= B_WMODE && id < B_WMODE + 3) {
        waterMode = id - B_WMODE;
        return;
    }
    if (id >= B_MAT && id < B_MAT + (int)paintMaterials().size()) {
        material = id - B_MAT;
        return;
    }
    if (id >= B_PAL && id < B_PAL + 1000) {
        choose(id - B_PAL);
        return;
    }
    if (id >= B_PALTAB && id < B_PALTAB + 20) {
        buildPalette();
        const int t = id - B_PALTAB;
        if (t == 0) {
            pointer = true, ledgeBrush = false, palTab = 0;
        } else {
            for (int i = 0; i < (int)palette.size(); i++)
                if (palette[i].tab == tabs[t]) {
                    choose(i);
                    break;
                }
        }
        return;
    }
    if (id == B_PAL_PREV || id == B_PAL_NEXT) {
        palPage = std::max(0, palPage + (id == B_PAL_NEXT ? 1 : -1));
        return;
    }
    if (id >= B_XF && id < B_XF + 16) {
        static const double steps[8] = {M_PI / 12, M_PI / 36, M_PI / 36, 0.25, 1.1, 1.1, 1.1, 1.1};
        const int op = (id - B_XF) / 2;
        const bool up = (id - B_XF) % 2 == 1;
        const double st = steps[op];
        const bool factor = op >= OP_SCALE;
        applyOp((ObjOp)op, factor ? (up ? st : 1 / st) : (up ? st : -st), "btn" + std::to_string(op));
        return;
    }
    if (id >= B_NUDGE && id < B_NUDGE + 4) {
        const int k = id - B_NUDGE;
        nudge(k == 0 ? -0.5 : k == 1 ? 0.5 : 0, k == 2 ? 0.5 : k == 3 ? -0.5 : 0);
        return;
    }
    if (id >= B_SET && id < B_SET + 11) {
        setRow = id - B_SET;
        if (setRow == 0) typing = true;
        else settingChange(setRow, 1);
        return;
    }
    if (id >= B_FILE && id < B_FILE + (int)files.size()) {
        openFile(id - B_FILE);
        return;
    }
    auto bump = [](double& v, double f, double lo, double hi) { v = clampd(v * f, lo, hi); };
    switch (id) {
        case B_SAVE: save(); break;
        case B_OPEN:
            refreshRecoveries();
            fileSel = 0;
            fileOpen = true;
            break;
        case B_NEWMAP:
            flow.requestLeave(core, SaveFlow::NEWMAP);  // (unsaved work asks first)
            afterFlow();
            break;
        case B_SEAT: applyOp(OP_SEAT, 0, "seat"); break;
        case B_SAVEAS:
            flow.requestSave(core, true);
            afterFlow();
            break;
        case B_ASK_SAVE:
        case B_ASK_DISCARD:
        case B_ASK_CANCEL:
            flow.choose(core, id == B_ASK_SAVE ? SaveFlow::SAVE : id == B_ASK_DISCARD ? SaveFlow::DISCARD : SaveFlow::CANCEL);
            afterFlow();
            break;
        case B_NAME_OK:
            flow.enter(core);
            afterFlow();
            break;
        case B_NAME_CANCEL:
            flow.escape();
            afterFlow();
            break;
        case B_NEWSEC:
            if (core.unsaved() && !confirmNew) {
                confirmNew = true;
                say("NOT SAVED: SAVE FIRST, OR CLICK NEW SECTION AGAIN TO DISCARD", 4);
                return;
            }
            sel = EditPick();
            newSection(*core.world, *rend);
            break;
        case B_PLAY: requestPlay(true); break;
        case B_PLAY_START: requestPlay(false); break;
        case B_UNDO: say(core.undo() ? "UNDONE " : "NOTHING TO UNDO", 1.2), sel = EditPick(), selection.clear(); break;
        case B_REDO: say(core.redo() ? "REDONE" : "NOTHING TO REDO", 1.2); break;
        case B_EXIT:
            flow.requestLeave(core, SaveFlow::LEAVE);
            afterFlow();
            break;
        case B_RAD_DN: radius = brushStep(radius, -1); break;
        case B_RAD_UP: radius = brushStep(radius, 1); break;
        case B_SHAPE_DN:
        case B_SHAPE_UP: shape = (BrushShape)((shape + (id == B_SHAPE_UP ? 1 : SHAPE_COUNT - 1)) % SHAPE_COUNT); break;
        case B_FALL_DN:
        case B_FALL_UP: fall = (BrushFall)((fall + (id == B_FALL_UP ? 1 : FALL_COUNT - 1)) % FALL_COUNT); break;
        case B_STYLE_DN:
        case B_STYLE_UP: style = (BrushStyle)((style + (id == B_STYLE_UP ? 1 : STYLE_COUNT - 1)) % STYLE_COUNT); break;
        case B_DENS_DN: density = clampd(density - 0.1, 0.1, 1.0); break;
        case B_DENS_UP: density = clampd(density + 0.1, 0.1, 1.0); break;
        case B_TERR_DN: terraceIdx = std::max(0, terraceIdx - 1); break;
        case B_TERR_UP: terraceIdx = std::min(3, terraceIdx + 1); break;
        case B_STR_DN: strength = clampd(strength - 0.1, 0.1, 1.0); break;
        case B_STR_UP: strength = clampd(strength + 0.1, 0.1, 1.0); break;
        case B_DEPTH_DN: floodDepth = clampd(floodDepth - 0.25, 0.25, 6.0); break;
        case B_DEPTH_UP: floodDepth = clampd(floodDepth + 0.25, 0.25, 6.0); break;
        case B_SNAP_DN: snapIdx = (snapIdx + 3) % 4; break;
        case B_SNAP_UP: snapIdx = (snapIdx + 1) % 4; break;
        case B_RISE_DN: ledgeRise = std::max(0, ledgeRise - 1); break;
        case B_RISE_UP: ledgeRise = std::min(4, ledgeRise + 1); break;
        case B_LDEPTH_DN: bump(ledgeDepth, 1 / 1.2, 1, 12); break;
        case B_LDEPTH_UP: bump(ledgeDepth, 1.2, 1, 12); break;
        case B_GATEW_DN:
        case B_GATEW_UP: {
            gateWidth = clampd(gateWidth + (id == B_GATEW_UP ? 2 : -2), 4, 40);
            if (sel.kind == EditPick::GATE) {
                Gate g = core.level.gates[sel.index];
                g.width = gateWidth;
                core.setGate(sel.index, g, "width");
            }
            break;
        }
        case B_DELETE:
            if (tool == OBJECTS) deleteSelection();
            else if (sel.kind == EditPick::GATE) core.removeGate(sel.index), say("GATE DELETED");
            sel = EditPick();
            break;
        case B_EARLIER:
        case B_LATER:
            if (sel.kind == EditPick::GATE) {
                const int to = sel.index + (id == B_LATER ? 1 : -1);
                if (to >= 0 && to < (int)core.level.gates.size()) {
                    core.moveGate(sel.index, id == B_LATER ? 1 : -1);
                    sel.index = to;
                }
            }
            break;
    }
}

// ---- Every frame ---------------------------------------------------------------------------

void Editor::update(double dt, const uint8_t* keys, Renderer& r, int w, int h) {
    rend = &r;
    W = w, H = h;
    clock += dt;
    core.clock = clock;
    toastTime = std::max(0.0, toastTime - dt);
    if (!scriptMods) {
        shift = keys[SC_LSHIFT] || keys[SC_RSHIFT];
        ctrl = keys[224] || keys[228];
    }
    alt = keys[226] || keys[230];
    // Fly (WASD, Space up, Shift fast, higher = faster; Shift+wheel is up / down).
    if (!typing && !fileOpen && flow.stage == SaveFlow::IDLE && !ctrl && tool != SETTINGS) {
        const double ground = core.world->height(eye.x, eye.z);
        const double speed = (12 + std::max(0.0, eye.y - ground) * 0.8) * (shift ? 3 : 1);
        const V3 f(std::cos(yaw), 0, -std::sin(yaw)), l(-f.z, 0, f.x);
        V3 move;
        if (keys[SC_W] || keys[SC_UP]) move += f;
        if (keys[SC_S] || keys[SC_DOWN]) move -= f;
        if (keys[SC_A] || keys[SC_LEFT]) move -= l;
        if (keys[SC_D] || keys[SC_RIGHT]) move += l;
        if (keys[SC_SPACE]) move.y += 1;
        if (len(move) > 0) eye += norm(move) * (speed * dt);
        eye.y = std::max(eye.y, core.world->height(eye.x, eye.z) + 1.5);
        eye.x = clampd(eye.x, -World::kHalfSize + 5, World::kHalfSize - 5);
        eye.z = clampd(eye.z, -World::kHalfSize + 5, World::kHalfSize - 5);
    }
    // What's under the mouse.
    V3 o, d;
    ray(mx, my, o, d);
    haveHit = core.rayGround(o, d, 2000, hit);
    const double hitT = haveHit ? len(hit - o) : 1e9;
    hover = EditPick();
    if ((tool == OBJECTS && pointer && !marquee && !overUi) || tool == COURSE) {  // (placing: a click always places, so nothing is "under" the mouse)
        const EditPick p = core.pick(o, d, hitT, tool == OBJECTS);
        const bool wanted = tool == OBJECTS ? (p.kind == EditPick::OBJECT || p.kind == EditPick::BASE) : (p.kind == EditPick::GATE || p.kind == EditPick::START);
        if (wanted) hover = p;
    }
    // Where a click would put something: the ground at the mouse, or the top of an object in the way (so rocks stack).
    placeAt = hit, placeOn = false;
    if (tool == OBJECTS && haveHit && !overUi) {
        std::vector<int> skip;
        for (const EditPick& p : selection)
            if (p.kind == EditPick::OBJECT) skip.push_back(p.index);
        V3 pp, nn;
        if (core.rayObjects(o, d, hitT - 0.02, skip, pp, nn) && nn.y > 0.25) placeAt = pp, placeOn = true;
    }
    // Where the middle of the view meets the ground (a playtest "from here" starts there).
    {
        V3 co, cd;
        ray(W / 2.0f, H / 2.0f, co, cd);
        haveViewHit = core.rayGround(co, cd, 3000, viewHit);
        if (!haveViewHit) {
            const V3 f(std::cos(yaw), 0, -std::sin(yaw));
            viewHit = eye + f * 20;
            viewHit.y = core.world->height(viewHit.x, viewHit.z);
        }
    }
    // Holding the button.
    if (lmb && stroke) applyBrush(dt);
    if (lmb && dragging && haveHit && tool == OBJECTS && !selection.empty()) {
        // (Moving the selection: by how far the mouse has come, on the snap grid; over another object it rides on its surface,
        // and with Alt held it keeps its height. Each keeps its own height over where it stands.)
        if (!dragStarted) dragFrom = placeAt, dragRide = placeRise(), dragApplied = V3(), dragStarted = true;
        double dx = placeAt.x - dragFrom.x, dz = placeAt.z - dragFrom.z;
        const double sn = kSnaps[snapIdx];
        if (sn > 0) dx = std::round(dx / sn) * sn, dz = std::round(dz / sn) * sn;
        const double ix = dx - dragApplied.x, iz = dz - dragApplied.z;
        const double ride = alt ? dragRide : placeRise(), dl = ride - dragRide;
        if (std::fabs(ix) > 1e-6 || std::fabs(iz) > 1e-6 || std::fabs(dl) > 1e-3) {
            core.beginEdit("MOVE", "drag");
            const std::vector<int> ks = selectedObjects();
            if (std::fabs(ix) > 1e-6 || std::fabs(iz) > 1e-6) core.moveObjects(ks, ix, iz, "drag");
            if (std::fabs(dl) > 1e-3) core.transformObjects(ks, OP_LIFT, dl, "drag"), dragRide = ride;
            core.endEdit();
            dragApplied.x = dx, dragApplied.z = dz;
        }
    } else if (lmb && dragging && haveHit) {
        if (sel.kind == EditPick::GATE && sel.index < (int)core.level.gates.size()) {
            Gate g = core.level.gates[sel.index];
            g.p = V3(hit.x + dragOffset.x, 0, hit.z + dragOffset.z);
            core.setGate(sel.index, g, "drag");
        } else if (sel.kind == EditPick::START) {
            core.setStart(hit.x + dragOffset.x, hit.z + dragOffset.z, core.level.startYaw);
        }
    }
    // The ghost of a structure, drawn as itself (the game puts it in the renderer's models for the frame).
    ghostModels.clear();
    if (tool == OBJECTS && !pointer && !ledgeBrush && haveHit && !overUi && proto.kind == OBJ_MODEL && mk::findModel(proto.name)) {
        const V3 at = snapped(hit);
        Placed pl;
        pl.id = proto.name, pl.p = V3(at.x, core.world->height(at.x, at.z) + proto.lift, at.z), pl.yaw = proto.yaw;
        pl.scale = V3(proto.size.x > 0 ? proto.size.x : 1, proto.size.y > 0 ? proto.size.y : 1, proto.size.z > 0 ? proto.size.z : 1);
        ghostModels.push_back(pl);
    }
    // Recovery copy: every minute there are unsaved changes the copy doesn't have.
    if (!autosaveDir.empty() && core.unsaved() && core.autosavedChanges != core.changes && clock - lastAuto > 60 && !lmb) {
        lastAuto = clock;
        core.autosave(autosaveDir);
    }
    // Catch the picture up.
    if (!core.dirtyCells.empty()) {
        r.updateTerrainRegion(*core.world, core.dirtyCells.i0, core.dirtyCells.j0, core.dirtyCells.i1, core.dirtyCells.j1);
        core.dirtyCells.clear();
    }
    if (core.dirtyWater) {
        r.updateWater(*core.world);
        core.dirtyWater = false;
    }
    if (core.dirtyObjects) {
        r.updateObjects(*core.world);
        core.dirtyObjects = false;
    }
}

// ---- Drawing ---------------------------------------------------------------------------

float Editor::button(Renderer& r, float x, float y, float w, float h, const std::string& lbl, int id, bool on, float u) {
    const bool hot = mx >= x && mx < x + w && my >= y && my < y + h;
    r.rect(x, y, w, h, on ? 1.0f : 1.0f, on ? 0.75f : 1.0f, on ? 0.3f : 1.0f, on ? 0.85f : hot ? 0.16f : 0.08f);
    const float s = px(1.2f * u);
    r.text(x + (w - r.textWidth(lbl, s)) / 2, y + (h - 8 * s) / 2, s, lbl, on ? 0.05f : 0.92f, on ? 0.05f : 0.93f, on ? 0.05f : 0.95f, 1);
    buttons.push_back({x, y, w, h, id});
    if (hot) overUi = true;
    return y + h + 4 * u;
}

void Editor::drawWorldMarks(Renderer& r) {
    const View v = view(W, H);
    const float u = std::max(0.75f, H / 720.0f), s = px(1.3f * u);
    auto dotAt = [&](const V3& p, float size, float cr, float cg, float cb, float a) {
        float sx, sy;
        if (screenPoint(v, W, H, p, sx, sy)) r.rect(sx - size / 2, sy - size / 2, size, size, cr, cg, cb, a);
    };
    auto line = [&](const V3& a, const V3& b, int n, float size, float cr, float cg, float cb, float al) {
        for (int k = 0; k <= n; k++) dotAt(a + (b - a) * ((double)k / n), size, cr, cg, cb, al);
    };
    auto tag = [&](const V3& p, const std::string& t, float cr, float cg, float cb) {
        float sx, sy;
        if (screenPoint(v, W, H, p, sx, sy)) label(r, sx - r.textWidth(t, s) / 2, sy - 8 * s - 6 * u, s, t, cr, cg, cb, 1);
    };
    const World& w = *core.world;
    // A section's canvas: its outline (and the blend beyond it), distance ticks from the entry.
    if (core.level.section.on) {
        const SectionInfo& c = core.level.section;
        const double ch = std::cos(c.heading), sh = std::sin(c.heading);
        const V3 f(ch, 0, -sh), l(sh, 0, ch), o(c.x, 0, c.z);
        auto at = [&](double uu, double vv, double lift) {
            const V3 p = o + f * uu + l * vv;
            return V3(p.x, w.height(p.x, p.z) + lift, p.z);
        };
        auto rect = [&](double grow, double step, float size, float cr, float cg, float cb, float al) {
            const double u0 = -grow, u1 = c.length + grow, v0 = -c.half - grow, v1 = c.half + grow;
            for (double uu = u0; uu <= u1; uu += step) dotAt(at(uu, v0, 0.3), size, cr, cg, cb, al), dotAt(at(uu, v1, 0.3), size, cr, cg, cb, al);
            for (double vv = v0; vv <= v1; vv += step) dotAt(at(u0, vv, 0.3), size, cr, cg, cb, al), dotAt(at(u1, vv, 0.3), size, cr, cg, cb, al);
        };
        rect(24, 6, 3 * u, 0.7f, 0.6f, 0.4f, 0.55f);  // (where the ground blends back into the land)
        rect(0, 2.5, 5 * u, 1.0f, 0.85f, 0.3f, 1);
        const double eu = core.level.hasStart ? (core.level.start.x - c.x) * ch - (core.level.start.z - c.z) * sh : 40;
        // Ticks every 20 m from the entry, and their distances along the near edge.
        for (int k = (int)std::ceil(-eu / 20); eu + k * 20 <= c.length; k++) {
            const double uu = eu + k * 20;
            for (double up = 0; up <= 2; up += 0.5) dotAt(at(uu, -c.half, up + 0.3), 3 * u, 1.0f, 0.85f, 0.3f, 0.9f);
            if (k != 0 && k % 2 == 0) tag(at(uu, -c.half, 3), (k > 0 ? "+" : "") + std::to_string(k * 20), 1.0f, 0.9f, 0.5f);
        }
        tag(at(0, c.half, 2), "CANVAS " + std::to_string((int)std::lround(c.length)) + " X " + std::to_string((int)std::lround(c.half * 2)) + " M", 1.0f, 0.85f, 0.3f);
        if (core.level.hasStart) {  // the entry's line across the track
            const V3 e = core.level.start;
            for (double vv = -7; vv <= 7; vv += 0.7) {
                const V3 p = e + l * vv;
                dotAt(V3(p.x, w.height(p.x, p.z) + 0.4, p.z), 4 * u, 0.3f, 1.0f, 0.4f, 1);
            }
        }
    }
    // The brush cursor, lying on the ground: its outline in the brush's shape (dotted for a pattern), a ring
    // where a soft edge is half strength, and for a pattern the cells it reaches.
    if (haveHit && (tool == SCULPT || tool == PAINT || tool == WATER)) {
        const bool fill = tool == WATER && waterMode != 1 && !shift;
        float cr = 1.0f, cg = 0.75f, cb = 0.3f;
        if (tool == PAINT) cr = paintMaterials()[material].col[0], cg = paintMaterials()[material].col[1], cb = paintMaterials()[material].col[2];
        if (tool == WATER) cr = fill ? 0.35f : 0.4f, cg = fill ? 0.7f : 0.9f, cb = fill ? 1.0f : 0.95f;
        // How many pixels a meter is where the brush is: the dots grow with it so the pattern reads near and far.
        float pxm = 6 * u, ax, ay, bx2, by2, cx2, cy2;
        if (screenPoint(v, W, H, hit, ax, ay) && screenPoint(v, W, H, hit + V3(1, 0, 0), bx2, by2) && screenPoint(v, W, H, hit + V3(0, 0, 1), cx2, cy2))
            pxm = std::max(std::hypot(bx2 - ax, by2 - ay), std::hypot(cx2 - ax, cy2 - ay));
        const float ringDot = clampd(pxm * 0.8, 4 * u, 9 * u), cellDot = pxm;
        auto onGround = [&](double x, double z, float size, float al) { dotAt(V3(x, w.height(x, z) + 0.15, z), size, cr, cg, cb, al); };
        auto outline = [&](BrushShape sh, double R, double cx, double cz, bool dotted, float size, float al) {
            const int n = std::min(240, std::max(32, (int)(R * 6)));
            for (int k = 0; k < n; k++) {
                if (dotted && (k & 1)) continue;
                const double t = (double)k / n;
                double ox, oz;
                if (sh == SHAPE_ROUND) {
                    ox = std::cos(2 * M_PI * t) * R, oz = std::sin(2 * M_PI * t) * R;
                } else {  // (walk the four sides: the square's corners, or the diamond's tips)
                    const double u4 = t * 4, f = u4 - std::floor(u4);
                    const int side = (int)u4, nx = (side + 1) % 4;
                    const double cxs[4] = {1, 1, -1, -1}, czs[4] = {-1, 1, 1, -1}, txs[4] = {1, 0, -1, 0}, tzs[4] = {0, 1, 0, -1};
                    const double* px = sh == SHAPE_SQUARE ? cxs : txs;
                    const double* pz = sh == SHAPE_SQUARE ? czs : tzs;
                    ox = (px[side] + (px[nx] - px[side]) * f) * R, oz = (pz[side] + (pz[nx] - pz[side]) * f) * R;
                }
                onGround(cx + ox, cz + oz, size, al);
            }
        };
        if (fill) {  // (filling floods from a point: a small ring)
            outline(SHAPE_ROUND, 2, hit.x, hit.z, false, ringDot, 0.95f);
        } else {
            const Brush b = curBrush();
            const double R = std::max(radius, 0.5), bx = R < 1 ? std::round(hit.x) : hit.x, bz = R < 1 ? std::round(hit.z) : hit.z;
            if (R < 1) {  // one grid cell: its square
                outline(SHAPE_SQUARE, 0.5, bx, bz, false, std::max(3 * u, std::min(ringDot, pxm * 0.3f)), 0.95f);
            } else {
                outline(shape, R, bx, bz, style != STYLE_SOLID, ringDot, 0.95f);
                if (fall != FALL_HARD) outline(shape, R * 0.5, bx, bz, true, ringDot * 0.7f, 0.5f);  // (half strength about here)
            }
            if (style != STYLE_SOLID) {  // the cells it reaches
                const int st = std::max(1, (int)std::lround(R / 12));
                for (int ix = (int)std::floor(bx - R); ix <= (int)std::ceil(bx + R); ix += st)
                    for (int iz = (int)std::floor(bz - R); iz <= (int)std::ceil(bz + R); iz += st)
                        if (brushMask(b, hit.x, hit.z, ix, iz) >= 0.5) onGround(ix, iz, clampd(cellDot * st * 0.6, 3 * u, 16 * u), 0.55f);
            }
        }
        dotAt(V3(hit.x, hit.y + 0.15, hit.z), 6 * u, 1, 1, 1, 0.9f);
    }
    // The course: start (an arrow) and gates (posts, a bar, the number).
    if (core.level.hasStart) {
        const V3 s0 = core.level.start;
        const double y = w.height(s0.x, s0.z) + 0.3;
        const V3 f(std::cos(core.level.startYaw), 0, -std::sin(core.level.startYaw)), l(-f.z, 0, f.x);
        const V3 p(s0.x, y, s0.z);
        const bool on = sel.kind == EditPick::START || hover.kind == EditPick::START;
        line(p - f * 4, p + f * 4, 16, 5 * u, 0.3f, 1.0f, 0.4f, 1);
        line(p + f * 4, p + f * 2 + l * 1.5, 6, 5 * u, 0.3f, 1.0f, 0.4f, 1);
        line(p + f * 4, p + f * 2 - l * 1.5, 6, 5 * u, 0.3f, 1.0f, 0.4f, 1);
        const char* nm = core.level.section.on ? "ENTRY" : "START";
        tag(p + V3(0, 3, 0), on ? std::string(nm) + " (SELECTED)" : std::string(nm), 0.3f, 1.0f, 0.4f);
    }
    for (int k = 0; k < (int)core.level.gates.size(); k++) {
        const Gate& g = core.level.gates[k];
        const V3 l(std::sin(g.yaw), 0, std::cos(g.yaw));  // across (left of its heading)
        const V3 a = g.p + l * (g.width / 2), b = g.p - l * (g.width / 2);
        const double ya = w.height(a.x, a.z), yb = w.height(b.x, b.z);
        const bool on = (sel.kind == EditPick::GATE && sel.index == k) || (hover.kind == EditPick::GATE && hover.index == k);
        const bool last = k + 1 == (int)core.level.gates.size();
        const float cr = on ? 1.0f : last ? 0.95f : 1.0f, cg = on ? 1.0f : last ? 0.95f : 0.55f, cb = on ? 0.4f : last ? 0.95f : 0.15f;
        line(V3(a.x, ya, a.z), V3(a.x, ya + 4, a.z), 10, 4 * u, cr, cg, cb, 1);
        line(V3(b.x, yb, b.z), V3(b.x, yb + 4, b.z), 10, 4 * u, cr, cg, cb, 1);
        line(V3(a.x, ya + 4, a.z), V3(b.x, yb + 4, b.z), 24, 4 * u, cr, cg, cb, 1);
        // Which way it's driven through.
        const V3 f(std::cos(g.yaw), 0, -std::sin(g.yaw)), m(g.p.x, (ya + yb) / 2 + 0.4, g.p.z);
        line(m - f * 2, m + f * 2, 8, 4 * u, cr, cg, cb, 0.8f);
        tag(V3(g.p.x, std::max(ya, yb) + 5, g.p.z),
            core.level.section.on && last ? "EXIT" : last && core.level.mode != LEVEL_FREE ? std::to_string(k + 1) + " FINISH" : std::to_string(k + 1), cr, cg, cb);
    }
    // What's picked or about to be: the outline of each selected object (amber), of the one under the mouse (white), and the ghost
    // of what the next click places (a translucent shape, lying on the ground at the mouse, as it will be placed).
    if (tool == OBJECTS) {
        auto outline = [&](const EditPick& p, float cr, float cg, float cb, float size) {
            Shape sh;
            if (p.kind == EditPick::OBJECT && p.index < (int)core.level.objects.size()) {
                for (const auto& part : core.partsOf(p.index)) sh.add(core.shapeOf(part.first, part.second));
                if (sh.edges.empty()) {  // (a station's pieces are fixtures; anything left without a shape: a ring)
                    const LevelObject& ob = core.level.objects[p.index];
                    sh.edges.push_back({ob.p, ob.p + V3(0, 2, 0)});
                }
            } else if (p.kind == EditPick::BASE) {
                sh = core.shapeOf(p.baseKind, core.resolveIndex(p));
            }
            drawShape(r, sh, false, cr, cg, cb, 1, size);
        };
        for (const EditPick& p : selection) outline(p, 1.0f, 0.8f, 0.25f, 4 * u);
        if (hover.kind != EditPick::NONE && !isSelected(hover)) outline(hover, 1.0f, 1.0f, 1.0f, 3 * u);
        if (!selection.empty()) {
            const EditPick& p = selection.back();
            std::string nm = p.name;
            V3 at = p.at;
            if (p.kind == EditPick::OBJECT && p.index < (int)core.level.objects.size()) {
                const LevelObject& ob = core.level.objects[p.index];
                at = (ob.kind == OBJ_LOG || ob.kind == OBJ_FENCE) ? (ob.p + ob.p2) * 0.5 : ob.p;
                nm = kObjNames[std::min(std::max(ob.kind, 0), OBJ_KIND_COUNT - 1)];
            }
            tag(at + V3(0, 3, 0), (selection.size() > 1 ? std::to_string(selection.size()) + " SELECTED" : nm + " (SELECTED)"), 1.0f, 0.85f, 0.3f);
        }
        if (!pointer && !ledgeBrush && haveHit && !overUi && !marquee) {
            const Shape ghost = core.previewShape(makeObject(snapped(placeAt), placeRise()));
            drawShape(r, ghost, true, 0.5f, 0.85f, 1.0f, 0.42f, 2.0f * u);
        }
        if (marquee) {
            const float x0 = std::min(mx, marqueeX), y0 = std::min(my, marqueeY), x1 = std::max(mx, marqueeX), y1 = std::max(my, marqueeY);
            r.rect(x0, y0, x1 - x0, y1 - y0, 1, 0.8f, 0.3f, 0.10f);
            const float t = std::max(1.0f, u);
            r.rect(x0, y0, x1 - x0, t, 1, 0.8f, 0.3f, 0.9f), r.rect(x0, y1 - t, x1 - x0, t, 1, 0.8f, 0.3f, 0.9f);
            r.rect(x0, y0, t, y1 - y0, 1, 0.8f, 0.3f, 0.9f), r.rect(x1 - t, y0, t, y1 - y0, 1, 0.8f, 0.3f, 0.9f);
        }
    }
}

// An outline drawn over the view: its edges as dots, and (for the ghost) its faces turned toward the eye, see-through.
void Editor::drawShape(Renderer& r, const Shape& sh, bool fill, float cr, float cg, float cb, float a, float size) {
    const View v = view(W, H);
    if (fill) {
        const V3 light = norm(V3(-0.4, 0.8, 0.45));
        for (const auto& t : sh.tris) {
            const V3 n = norm(cross(t[1] - t[0], t[2] - t[0]));
            if (dot(n, (t[0] + t[1] + t[2]) * (1.0 / 3) - v.eye) >= 0) continue;  // (a back face)
            float sx[3], sy[3];
            bool ok = true;
            for (int k = 0; k < 3; k++) ok &= screenPoint(v, W, H, t[k], sx[k], sy[k]);
            if (!ok) continue;
            const float lit = 0.55f + 0.45f * (float)std::max(0.0, dot(n, light));
            r.tri(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], cr * lit, cg * lit, cb * lit, a);
        }
    }
    // The edges as thin lines (each a strip of two triangles, `size` pixels across).
    const float half = std::max(0.8f, size * 0.4f);
    for (const auto& e : sh.edges) {
        float ax, ay, bx, by;
        if (!screenPoint(v, W, H, e.first, ax, ay) || !screenPoint(v, W, H, e.second, bx, by)) continue;
        const float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
        if (l < 0.5f) {
            r.rect(ax - half, ay - half, 2 * half, 2 * half, cr, cg, cb, 0.95f);
            continue;
        }
        const float nx = -dy / l * half, ny = dx / l * half;
        r.tri(ax - nx, ay - ny, ax + nx, ay + ny, bx + nx, by + ny, cr, cg, cb, 0.95f);
        r.tri(ax - nx, ay - ny, bx + nx, by + ny, bx - nx, by - ny, cr, cg, cb, 0.95f);
    }
}

void Editor::drawToolbar(Renderer& r, float u) {
    const float x = 12 * u, w = 128 * u, bh = 28 * u;
    float y = 12 * u;
    pill(r, x - 6 * u, y - 6 * u, w + 12 * u, (bh + 4 * u) * TOOL_COUNT + 8 * u, 0, 0, 0, 0.55f);
    for (int t = 0; t < TOOL_COUNT; t++) y = button(r, x, y, w, bh, std::to_string(t + 1) + "  " + kToolNames[t], B_TOOL + t, tool == t, u);
    // File and history.
    y += 14 * u;
    pill(r, x - 6 * u, y - 6 * u, w + 12 * u, (bh + 4 * u) * 10 + 8 * u, 0, 0, 0, 0.55f);
    y = button(r, x, y, w, bh, "SAVE  CTRL+S", B_SAVE, false, u);
    y = button(r, x, y, w, bh, "SAVE AS", B_SAVEAS, false, u);
    y = button(r, x, y, w, bh, "OPEN...", B_OPEN, false, u);
    y = button(r, x, y, w, bh, std::string("NEW: ") + mapName(nextListedMap(newMap, 1)), B_NEWMAP, false, u);
    y = button(r, x, y, w, bh, confirmNew ? "SURE? CLICK AGAIN" : "NEW SECTION", B_NEWSEC, confirmNew, u);
    y = button(r, x, y, w, bh, "UNDO", B_UNDO, false, u);
    y = button(r, x, y, w, bh, "REDO", B_REDO, false, u);
    y = button(r, x, y, w, bh, "TEST HERE  F5", B_PLAY, false, u);
    y = button(r, x, y, w, bh, "TEST START F6", B_PLAY_START, false, u);
    button(r, x, y, w, bh, confirmClose ? "EXIT: SURE?" : "EXIT", B_EXIT, confirmClose, u);
}

void Editor::drawOptions(Renderer& r, float u) {
    const float w = 290 * u, x = W - w - 12 * u, bh = 26 * u, s = px(1.2f * u), half = (w - 4 * u) / 2;
    float y = 12 * u;
    const float top = y;
    std::vector<std::string> info;
    // (Drawn in two passes: the panel's height isn't known till the end, so draw into a
    // scratch list of rows first. Simpler: a fixed tall panel per tool.)
    const float panelH = tool == PAINT ? 640 * u : tool == SCULPT ? 560 * u : tool == WATER ? (waterMode == 1 ? 520 * u : 330 * u)
                         : tool == SETTINGS ? 420 * u : tool == OBJECTS ? (selection.empty() ? 520.0f : 590.0f) * u : tool == SECTION ? 600 * u : 300 * u;
    pill(r, x - 8 * u, top - 6 * u, w + 16 * u, panelH, 0, 0, 0, 0.55f);
    label(r, x, y, px(1.6f * u), kToolNames[tool], 1.0f, 0.75f, 0.3f, 1);
    y += 8 * px(1.6f * u) + 6 * u;
    r.text(x, y, s, kToolHints[tool], 0.7f, 0.72f, 0.76f, 1);
    y += 8 * s + 10 * u;
    auto row = [&](const std::string& name, const std::string& value, int dn, int up) {
        r.text(x, y + (bh - 8 * s) / 2, s, name, 0.85f, 0.86f, 0.9f, 1);
        const float bx = x + w - 130 * u;
        button(r, bx, y, 26 * u, bh, "<", dn, false, u);
        r.text(bx + 30 * u + (70 * u - r.textWidth(value, s)) / 2, y + (bh - 8 * s) / 2, s, value, 1, 1, 1, 1);
        button(r, bx + 104 * u, y, 26 * u, bh, ">", up, false, u);
        y += bh + 4 * u;
    };
    auto note = [&](const std::string& t) {
        r.text(x, y, s, t, 0.62f, 0.64f, 0.68f, 1);
        y += 8 * s + 6 * u;
    };
    // The brush rows every ground tool shares: shape, edge, pattern (and how much it covers), size, strength.
    auto brushRows = [&](bool withStrength, const char* strengthName) {
        row("SHAPE", brushShapeName(shape), B_SHAPE_DN, B_SHAPE_UP);
        row("EDGE", brushFallName(fall), B_FALL_DN, B_FALL_UP);
        row("PATTERN", brushStyleName(style), B_STYLE_DN, B_STYLE_UP);
        if (style != STYLE_SOLID) row("COVERAGE", std::to_string((int)std::lround(density * 100)) + "%", B_DENS_DN, B_DENS_UP);
        row("RADIUS", radius < 1 ? std::string("1 CELL") : fmt1(radius, " M"), B_RAD_DN, B_RAD_UP);
        if (withStrength) row(strengthName, std::to_string((int)std::lround(strength * 100)) + "%", B_STR_DN, B_STR_UP);
    };
    const char* brushKeys = "B F N: SHAPE EDGE PATTERN   , . COVERAGE";
    switch (tool) {
        case SCULPT:
            for (int k = 0; k < BRUSH_OP_COUNT; k++)
                button(r, x + (k % 2) * (half + 4 * u), y + (k / 2) * (bh + 4 * u), half, bh, kOpNames[k], B_OP + k, op == k, u);
            y += (BRUSH_OP_COUNT / 2) * (bh + 4 * u) + 6 * u;
            brushRows(true, "STRENGTH");
            if (op == BRUSH_TERRACE) row("STEP", fmt1(kTerraces[terraceIdx], " M"), B_TERR_DN, B_TERR_UP);
            y += 6 * u;
            note("HOLD LEFT BUTTON: BRUSH   SHIFT: FINE");
            note("Q E OR [ ] OR CTRL+WHEEL: SIZE");
            note("- =: STRENGTH   SHIFT+WHEEL: CAMERA UP DOWN");
            note(brushKeys);
            note("TAB: NEXT BRUSH  FLATTEN: TO CLICKED HEIGHT");
            note("ROUGHEN: BUMPS  TERRACE: LEVEL STEPS");
            break;
        case PAINT: {
            const auto& m = paintMaterials();
            for (int k = 0; k < (int)m.size(); k++) {
                const float bx = x + (k % 2) * (half + 4 * u), by = y + (k / 2) * (bh + 4 * u);
                button(r, bx + 22 * u, by, half - 22 * u, bh, m[k].name, B_MAT + k, material == k, u);
                r.rect(bx, by + 3 * u, 18 * u, bh - 6 * u, m[k].col[0], m[k].col[1], m[k].col[2], 1);
            }
            y += ((int)m.size() + 1) / 2 * (bh + 4 * u) + 6 * u;
            brushRows(true, "OPACITY");
            y += 6 * u;
            note("HOLD LEFT BUTTON TO PAINT. TAB: NEXT");
            note("Q E OR [ ] OR CTRL+WHEEL: SIZE");
            note("- =: OPACITY   SHIFT+WHEEL: CAMERA UP DOWN");
            note(brushKeys);
            note("SPLATTER: SCATTERED DABS. NOISE: MOTTLED");
            note("IT DRIVES LIKE IT LOOKS: MUD BOGS,");
            note("SAND SINKS, ICE SLIDES.");
            break;
        }
        case WATER:
            for (int k = 0; k < 2; k++) button(r, x + k * (half + 4 * u), y, half, bh, k ? kWaterModes[1] : kWaterModes[0], B_WMODE + k, waterMode == k, u);
            y += bh + 4 * u;
            y = button(r, x, y, w, bh, kWaterModes[2], B_WMODE + 2, waterMode == 2, u) + 6 * u;
            if (waterMode == 0) {
                row("DEPTH", fmt1(floodDepth, " M"), B_DEPTH_DN, B_DEPTH_UP);
                y += 6 * u;
                note("CLICK IN A HOLLOW: FILLS IT THIS DEEP");
                note("(IT SPREADS OVER LOWER GROUND, SO DIG");
                note("A BASIN WITH SCULPT > LOWER FIRST)");
                note("SHIFT+CLICK: DRAIN WITH THE BRUSH");
            } else if (waterMode == 1) {
                brushRows(false, "");
                y += 6 * u;
                note("HOLD LEFT BUTTON: DRAIN UNDER THE BRUSH");
                note("Q E OR [ ] OR CTRL+WHEEL: SIZE");
                note(brushKeys);
            } else {
                note("CLICK ON WATER: THE WHOLE POOL DRAINS,");
                note("SHALLOW EDGES TOO. CTRL+Z BRINGS IT BACK.");
            }
            break;
        case OBJECTS: {
            buildPalette();
            if (ledgeBrush) {
                const double rise = kRisers[ledgeRise];
                row("RISER", fmt1(rise, " M"), B_RISE_DN, B_RISE_UP);
                row("DEPTH", fmt1(ledgeDepth, " M"), B_LDEPTH_DN, B_LDEPTH_UP);
                row("SNAP", kSnaps[snapIdx] > 0 ? fmt1(kSnaps[snapIdx], " M") : "OFF", B_SNAP_DN, B_SNAP_UP);
                y += 6 * u;
                note("DRAG A LINE ON THE GROUND: A ROW OF ROCK");
                note("SLABS GOES ALONG IT, RISING AWAY FROM YOU.");
                note(rise > 0.4 ? "THIS RISE IS TOO BIG FOR A NOTCH STEP" : "A STEP THIS SIZE IS FINE FOR A NOTCH (0.4 M MAX)");
                note("A BRONCO CLIMBS A SQUARE LEDGE UP TO 0.45 M");
                note("FACES 0.75-1.3 M NEED A LINE OR A WINCH");
                note("UNDO TAKES THE WHOLE ROW BACK");
                break;
            }
            // What the rows change: the selected object (or a group), else the ghost.
            LevelObject tgt;
            bool haveTgt = false;
            std::string head;
            if (selection.size() == 1) {
                const EditPick& p = selection[0];
                if (p.kind == EditPick::OBJECT && p.index < (int)core.level.objects.size()) tgt = core.level.objects[p.index], haveTgt = true;
                else if (p.kind == EditPick::BASE) haveTgt = core.asObject(p, tgt);
                head = std::string(p.kind == EditPick::BASE ? "THE MAP'S " : "") + (haveTgt ? kObjNames[std::min(std::max(tgt.kind, 0), OBJ_KIND_COUNT - 1)] : p.name.c_str());
            } else if (selection.size() > 1) {
                head = std::to_string(selection.size()) + " OBJECTS";
            } else if (!pointer) {
                tgt = proto, haveTgt = true;
                head = "PLACING: " + (palPick < (int)palette.size() ? palette[palPick].name : std::string());
            } else {
                head = "NOTHING SELECTED";
            }
            r.text(x, y, s, head, 1.0f, 0.85f, 0.4f, 1);
            y += 8 * s + 6 * u;
            auto deg = [](double rad) { return std::to_string((int)std::lround(std::fmod(rad * 180 / M_PI + 3600 + 180, 360) - 180)) + " DEG"; };
            auto xf = [&](ObjOp op, const char* name, const std::string& val) { row(name, val, B_XF + op * 2, B_XF + op * 2 + 1); };
            if (haveTgt && selection.size() <= 1) {
                double yawV = tgt.yaw, pitchV = tgt.pitch, liftV = tgt.lift;
                if (tgt.kind == OBJ_LOG && !selection.empty() && len(tgt.p2 - tgt.p) > 1e-6) {
                    const V3 hv = tgt.p2 - tgt.p;
                    yawV = std::atan2(-hv.z, hv.x), pitchV = std::asin(clampd(hv.y / len(hv), -1, 1));
                }
                if (tgt.kind == OBJ_ROCK) liftV = tgt.top;
                const bool isModel = tgt.kind == OBJ_MODEL;
                auto pct = [](double v) { return std::to_string((int)std::lround(v * 100)) + "%"; };
                switch (tgt.kind) {
                    case OBJ_ROCK:
                        xf(OP_YAW, "TURN", deg(yawV)), xf(OP_PITCH, "TIP", deg(pitchV)), xf(OP_ROLL, "ROLL", deg(tgt.roll));
                        xf(OP_LIFT, "SHOWS ABOVE GROUND", fmt1(liftV, " M")), xf(OP_SCALE, "SCALE ALL", "");
                        xf(OP_STRETCH_X, "ALONG", fmt1(tgt.size.x * 2, " M")), xf(OP_STRETCH_Y, "HEIGHT", fmt1(tgt.size.y * 2, " M")), xf(OP_STRETCH_Z, "ACROSS", fmt1(tgt.size.z * 2, " M"));
                        break;
                    case OBJ_BOX:
                        xf(OP_YAW, "TURN", deg(yawV)), xf(OP_PITCH, "TILT (RAMP)", deg(pitchV)), xf(OP_ROLL, "ROLL", deg(tgt.roll));
                        xf(OP_LIFT, "RAISE", fmt1(liftV, " M")), xf(OP_SCALE, "SCALE ALL", "");
                        xf(OP_STRETCH_X, "LENGTH", fmt1(tgt.size.x * 2, " M")), xf(OP_STRETCH_Y, "THICKNESS", fmt1(tgt.size.y * 2, " M")), xf(OP_STRETCH_Z, "WIDTH", fmt1(tgt.size.z * 2, " M"));
                        break;
                    case OBJ_LOG: {
                        const bool ghostLog = selection.empty();
                        xf(OP_YAW, "TURN", deg(yawV)), xf(OP_PITCH, "TILT", deg(pitchV));
                        xf(OP_LIFT, "RAISE", ghostLog ? fmt1(tgt.lift, " M") : "");
                        xf(OP_SCALE, "SCALE ALL", "");
                        xf(OP_STRETCH_X, "LENGTH", ghostLog ? fmt1(tgt.size.y * 2, " M") : fmt1(len(tgt.p2 - tgt.p), " M"));
                        xf(OP_STRETCH_Y, "THICKNESS", fmt1(tgt.size.x * 2, " M"));
                        break;
                    }
                    case OBJ_TREE:
                        xf(OP_LIFT, "RAISE", fmt1(tgt.lift, " M")), xf(OP_SCALE, "SCALE ALL", ""), xf(OP_STRETCH_Y, "HEIGHT", fmt1(tgt.size.y, " M"));
                        if (selection.empty()) {
                            // (the ghost tree's kind is the palette's: pine, broadleaf, snag)
                        }
                        break;
                    case OBJ_STATION: xf(OP_YAW, "TURN", deg(yawV)); break;
                    case OBJ_FENCE:
                        xf(OP_YAW, "TURN", deg(std::atan2(-(tgt.p2.z - tgt.p.z), tgt.p2.x - tgt.p.x))), xf(OP_STRETCH_X, "LENGTH", fmt1(len(tgt.p2 - tgt.p), " M"));
                        break;
                    case OBJ_BUSH: xf(OP_LIFT, "RAISE", fmt1(tgt.lift, " M")), xf(OP_SCALE, "SIZE", fmt1(tgt.size.x * 2, " M")); break;
                    case OBJ_MODEL:
                        xf(OP_YAW, "TURN", deg(yawV)), xf(OP_LIFT, "RAISE", fmt1(liftV, " M")), xf(OP_SCALE, "SCALE ALL", "");
                        xf(OP_STRETCH_X, "LENGTH", pct(tgt.size.x)), xf(OP_STRETCH_Y, "HEIGHT", pct(tgt.size.y)), xf(OP_STRETCH_Z, "WIDTH", pct(tgt.size.z));
                        break;
                    default: break;
                }
                (void)isModel;
            } else if (selection.size() > 1) {
                xf(OP_YAW, "TURN TOGETHER", ""), xf(OP_LIFT, "RAISE", ""), xf(OP_SCALE, "SCALE EACH", "");
            }
            if (!selection.empty()) {
                const float qw = (w - 3 * 4 * u) / 4;
                r.text(x, y + (bh - 8 * s) / 2, s, "MOVE", 0.85f, 0.86f, 0.9f, 1);
                (void)qw;
                y += 0;
                const char* lbl[4] = {"LEFT", "RIGHT", "FAR", "NEAR"};
                const float bx = x + 52 * u, bw = (w - 52 * u - 3 * 4 * u) / 4;
                for (int k = 0; k < 4; k++) button(r, bx + k * (bw + 4 * u), y, bw, bh, lbl[k], B_NUDGE + k, false, u);
                y += bh + 4 * u;
                button(r, x, y, w, bh, "DELETE SELECTED  (DEL)", B_DELETE, false, u);
                y += bh + 4 * u;
            }
            if (!selection.empty() || !pointer) {
                button(r, x, y, w, bh, "SNAP TO GROUND  (B)", B_SEAT, false, u);
                y += bh + 4 * u;
            }
            row("GRID SNAP", kSnaps[snapIdx] > 0 ? fmt1(kSnaps[snapIdx], " M") : "OFF", B_SNAP_DN, B_SNAP_UP);
            y += 2 * u;
            if (pointer) {
                note("CLICK: SELECT   SHIFT+CLICK: ADD");
                note("DRAG EMPTY GROUND: BOX SELECT");
                note("DRAG ONE: MOVE (IT RIDES ON ROCKS)");
                note("ALT+DRAG: KEEP ITS HEIGHT");
            } else {
                note("CLICK: PLACE (ON TOP OF A ROCK: STACKS)");
            }
            note("Q E  SIZE   SHIFT+Q E OR , .  TURN");
            note("R F  TIP   C V  ROLL   Z X  LOWER, RAISE");
            note("T G  LONGER   Y H  TALLER   U J  WIDER");
            note("CTRL+ARROWS  NUDGE   SHIFT  COARSER");
            note("TAB  POINTER / PLACING   DEL  DELETE");
            break;
        }
        case COURSE: {
            const int n = (int)core.level.gates.size();
            note(core.level.section.on ? (core.level.hasStart ? std::to_string(n) + (n == 1 ? " GATE: THE LAST IS THE EXIT" : " GATES: THE LAST IS THE EXIT") : "FIRST CLICK: THE ENTRY")
                                       : core.level.hasStart ? (std::to_string(n) + (n == 1 ? " GATE" : " GATES") + " IN ORDER") : "FIRST CLICK: THE START");
            row("GATE WIDTH", fmt1(gateWidth, " M"), B_GATEW_DN, B_GATEW_UP);
            if (sel.kind == EditPick::GATE) {
                button(r, x, y, half, bh, "EARLIER", B_EARLIER, false, u);
                button(r, x + half + 4 * u, y, half, bh, "LATER", B_LATER, false, u);
                y += bh + 4 * u;
                button(r, x, y, w, bh, "DELETE GATE " + std::to_string(sel.index + 1), B_DELETE, false, u);
                y += bh + 4 * u;
            }
            y += 6 * u;
            if (core.level.section.on) {
                note("ENTRY = THE START: IT SNAPS TO A WHOLE");
                note("METRE AND FACES ALONG THE CANVAS.");
                note("EXIT = THE LAST GATE. KEEP BOTH INSIDE");
                note("THE CANVAS. DRAG EITHER TO MOVE IT.");
                note("[ ]: GATE WIDTH");
                break;
            }
            note("CLICK THE GROUND: NEW GATE, FACING");
            note("THE WAY YOU LOOK (AFTER THE SELECTED)");
            note("DRAG A GATE OR THE START TO MOVE IT");
            note("SHIFT+Q E: TURN IT    [ ]: WIDTH");
            note("THE LAST GATE IS THE FINISH (A RACE:");
            note("THE START/FINISH LINE, LAPPED)");
            break;
        }
        case SETTINGS: {
            const Level& l = core.level;
            const auto& cat = vehicleCatalog();
            const std::string rows[11][2] = {
                {"NAME", l.name + (typing && std::fmod(clock, 1.0) < 0.5 ? "_" : "")},
                {"MODE", levelModeName(l.mode)},
                {"LAPS", std::to_string(l.laps)},
                {"VEHICLE", l.vehicle >= 0 && l.vehicle < (int)cat.size() ? cat[l.vehicle].name : "ANY"},
                {"TIME OF DAY", l.timeOfDay < 0 ? "ANY" : fmt1(l.timeOfDay, " H")},
                {"R REPAIRS", l.resets ? "ALLOWED" : "NO"},
                {"OUT AT DAMAGE", l.maxDamage > 0 ? std::to_string((int)l.maxDamage) + "%" : "NEVER"},
                {"TIME LIMIT", l.timeLimit > 0 ? formatRaceTime(l.timeLimit) : "NONE"},
                {"GOLD", l.medals[0] > 0 ? formatRaceTime(l.medals[0]) : "-"},
                {"SILVER", l.medals[1] > 0 ? formatRaceTime(l.medals[1]) : "-"},
                {"BRONZE", l.medals[2] > 0 ? formatRaceTime(l.medals[2]) : "-"},
            };
            for (int k = 0; k < 11; k++) {
                const bool on = k == setRow;
                if (on) r.rect(x - 4 * u, y, w + 8 * u, bh, 1, 1, 1, 0.08f);
                buttons.push_back({x - 4 * u, y, w + 8 * u, bh, B_SET + k});
                r.text(x, y + (bh - 8 * s) / 2, s, rows[k][0], on ? 1.0f : 0.8f, on ? 0.8f : 0.82f, on ? 0.4f : 0.86f, 1);
                std::string v = rows[k][1];
                while (!v.empty() && r.textWidth(v, s) > w - 110 * u) v.erase(v.begin());
                r.text(x + w - r.textWidth(v, s), y + (bh - 8 * s) / 2, s, v, 1, 1, 1, 1);
                y += bh + 2 * u;
            }
            y += 8 * u;
            note(typing ? "TYPE THE NAME, ENTER WHEN DONE" : "UP DOWN: PICK   LEFT RIGHT: CHANGE");
            note("ENTER ON NAME: TYPE IT");
            break;
        }
        case SECTION: {
            const SectionInfo& c = core.level.section;
            if (!c.on) {
                note("THIS LEVEL ISN'T A SECTION.");
                note("TOOLBAR: NEW SECTION STARTS ONE ON A");
                note("CLEARED CANVAS, WITH AN ENTRY AND AN EXIT.");
                break;
            }
            const std::string nm = core.level.name + (typing && std::fmod(clock, 1.0) < 0.5 ? "_" : "");
            button(r, x, y, w, bh, "NAME: " + nm, B_SEC + S_NAME, typing, u);
            y += bh + 4 * u;
            row("TYPE", sectionKindName(c.kind), B_SEC + S_KIND_DN, B_SEC + S_KIND_UP);
            row("DIFFICULTY", std::to_string(c.difficulty) + " OF 5", B_SEC + S_DIFF_DN, B_SEC + S_DIFF_UP);
            note("WHAT IT TESTS");
            for (int k = 0; k < ST_COUNT; k++)
                button(r, x + (k % 2) * (half + 4 * u), y + (k / 2) * (bh + 4 * u), half, bh, std::string(kTestShort[k]) + (c.tests >> k & 1 ? ": ON" : ""), B_SEC + S_TEST + k, c.tests >> k & 1, u);
            y += 3 * (bh + 4 * u) + 2 * u;
            note("KIT IT WANTS");
            for (int k = 0; k < SKIT_COUNT; k++)
                button(r, x + (k % 2) * (half + 4 * u), y + (k / 2) * (bh + 4 * u), half, bh, std::string(sectionKitName(k)) + (c.kit >> k & 1 ? ": ON" : ""), B_SEC + S_KIT + k, c.kit >> k & 1, u);
            y += 3 * (bh + 4 * u) + 2 * u;
            note(c.gen >= 0 ? std::string("STAMPED FROM ") + sectionName((SectionType)c.gen) + " " + std::to_string(c.genSeed) : "START FROM A GENERATOR (OPTIONAL)");
            row("GENERATOR", sectionName((SectionType)genType), B_SEC + S_GEN_DN, B_SEC + S_GEN_UP);
            row("SEED", std::to_string(genSeed), B_SEC + S_SEED_DN, B_SEC + S_SEED_UP);
            button(r, x, y, w, bh, "STAMP IT AT THE ENTRY", B_SEC + S_STAMP, false, u);
            y += bh + 8 * u;
            note("SAVE WRITES THE LEVEL AND A .CZS SECTION");
            note("FILE (MEASURED FROM THE ENTRY) FOR THE STITCHER");
            break;
        }
        default: break;
    }
    if (mx >= x - 8 * u && my >= top - 6 * u && my < top - 6 * u + panelH) overUi = true;
}

// The palette: a strip along the bottom, tabs over a row of cards (each with the model as its picture).
void Editor::drawPalette(Renderer& r, float u) {
    buildPalette();
    const float s = px(1.0f * u), x0 = 156 * u, x1 = W - 310 * u, statusTop = H - (8 * px(1.2f * u) + 14 * u);
    const float cardW = 88 * u, cardH = 92 * u, tabH = 22 * u, gap = 4 * u;
    const float stripH = tabH + cardH + 14 * u, y0 = statusTop - 2 * (8 * px(1.2f * u) + 6 * u) - 14 * u - stripH;
    pill(r, x0 - 6 * u, y0 - 4 * u, x1 - x0 + 12 * u, stripH + 8 * u, 0, 0, 0, 0.6f);
    // Tabs.
    const float tw = std::min(92 * u, (x1 - x0) / (float)tabs.size());
    for (int t = 0; t < (int)tabs.size(); t++) {
        const bool on = pointer ? t == 0 : tabs[t] == (palPick < (int)palette.size() ? palette[palPick].tab : std::string()) && !pointer;
        button(r, x0 + t * tw, y0, tw - gap, tabH, tabs[t], B_PALTAB + t, on, u);
    }
    const float cy = y0 + tabH + 6 * u;
    if (pointer) {
        r.text(x0 + 8 * u, cy + 6 * u, s, "POINTER: SELECT, MOVE AND CHANGE ANYTHING IN THE WORLD, THE MAP'S OWN TOO", 0.9f, 0.9f, 0.95f, 1);
        r.text(x0 + 8 * u, cy + 24 * u, s, "CLICK AN OBJECT, OR DRAG A BOX OVER SEVERAL (SHIFT ADDS). DRAG ONE TO MOVE IT; THE KEYS AT THE RIGHT TURN,", 0.7f, 0.72f, 0.76f, 1);
        r.text(x0 + 8 * u, cy + 38 * u, s, "TIP, RAISE, SCALE AND STRETCH IT. DEL DELETES. PICK A TAB ABOVE (OR TAB) TO PLACE NEW THINGS.", 0.7f, 0.72f, 0.76f, 1);
        if (mx >= x0 - 6 * u && mx < x1 + 6 * u && my >= y0 - 4 * u && my < y0 + stripH + 4 * u) overUi = true;
        return;
    }
    std::vector<int> idx;
    const std::string& tabName = palette[palPick].tab;
    for (int i = 0; i < (int)palette.size(); i++)
        if (palette[i].tab == tabName) idx.push_back(i);
    const float arrowW = 20 * u;
    const int per = std::max(1, (int)((x1 - x0 - 2 * arrowW - 2 * gap) / (cardW + gap)));
    const int pages = std::max(1, ((int)idx.size() + per - 1) / per);
    palPage = std::min(palPage, pages - 1);
    if (pages > 1) {
        button(r, x0, cy, arrowW, cardH, "<", B_PAL_PREV, false, u);
        button(r, x1 - arrowW, cy, arrowW, cardH, ">", B_PAL_NEXT, false, u);
        const std::string pg = std::to_string(palPage + 1) + "/" + std::to_string(pages);
        r.text(x1 - arrowW + (arrowW - r.textWidth(pg, s)) / 2, cy + cardH + 2 * u, s, pg, 0.7f, 0.72f, 0.76f, 1);
    }
    for (int k = 0; k < per; k++) {
        const int at = palPage * per + k;
        if (at >= (int)idx.size()) break;
        const int pi = idx[at];
        const PalEntry& e = palette[pi];
        const float cx = x0 + arrowW + gap + k * (cardW + gap);
        const bool on = pi == palPick, hot = mx >= cx && mx < cx + cardW && my >= cy && my < cy + cardH;
        pill(r, cx, cy, cardW, cardH, on ? 0.17f : hot ? 0.13f : 0.075f, on ? 0.15f : hot ? 0.12f : 0.085f, on ? 0.09f : hot ? 0.1f : 0.10f, 0.96f);
        if (on) {
            const float t = std::max(2.0f, std::round(2 * u));
            r.rect(cx, cy, cardW, t, 1, 0.75f, 0.3f, 1), r.rect(cx, cy + cardH - t, cardW, t, 1, 0.75f, 0.3f, 1);
            r.rect(cx, cy, t, cardH, 1, 0.75f, 0.3f, 1), r.rect(cx + cardW - t, cy, t, cardH, 1, 0.75f, 0.3f, 1);
        }
        const float side = cardH - 22 * u;
        r.icon(e.icon, iconMesh(e), cx + (cardW - side) / 2, cy + 3 * u, side, side);
        std::string nm = e.name;
        while (!nm.empty() && r.textWidth(nm, s) > cardW - 6 * u) nm.pop_back();
        r.text(cx + (cardW - r.textWidth(nm, s)) / 2, cy + cardH - 14 * u, s, nm, on ? 1.0f : 0.85f, on ? 0.85f : 0.86f, on ? 0.5f : 0.9f, 1);
        buttons.push_back({cx, cy, cardW, cardH, B_PAL + pi});
        if (hot) overUi = true;
    }
    if (mx >= x0 - 6 * u && mx < x1 + 6 * u && my >= y0 - 4 * u && my < y0 + stripH + 4 * u) overUi = true;
}

void Editor::drawStatus(Renderer& r, float u) {
    const float s = px(1.2f * u), h = 8 * s + 14 * u, y = H - h;
    r.rect(0, y, (float)W, h, 0, 0, 0, 0.6f);
    const Level& l = core.level;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s%s  -  %s  -  %s  -  %zu CELLS, %zu OBJECTS, %zu GATES  -  UNDO: %s", l.name.c_str(), core.unsaved() ? " *" : "",
                  mapName(l.map), levelModeName(l.mode), core.cellEdits(), l.objects.size(), l.gates.size(),
                  core.canUndo() ? core.undoName().c_str() : "-");
    r.text(12 * u, y + 7 * u, s, buf, 0.9f, 0.91f, 0.94f, 1);
    const char* keys = "WASD FLY  SPACE OR SHIFT+WHEEL UP DOWN  WHEEL ZOOM  Q E SIZE  RIGHT-DRAG LOOK  SHIFT FAST  1-7 TOOLS  CTRL+Z/Y UNDO/REDO  CTRL+S SAVE  F5 TEST HERE  F6 TEST START  ESC EXIT";
    const float ks = px(1.0f * u);
    r.rect(W - r.textWidth(keys, ks) - 20 * u, y - 8 * ks - 10 * u, r.textWidth(keys, ks) + 20 * u, 8 * ks + 10 * u, 0, 0, 0, 0.45f);
    r.text(W - r.textWidth(keys, ks) - 10 * u, y - 8 * ks - 5 * u, ks, keys, 0.7f, 0.72f, 0.76f, 1);
    if (toastTime > 0 && !toast.empty()) centeredPill(r, W / 2.0f, y - 40 * u, px(1.4f * u), toast, 0, 0, 0, (float)std::min(1.0, toastTime * 2), 1, 0.85f, 0.5f);
    if (haveHit) {
        std::snprintf(buf, sizeof buf, "%.0f, %.0f  H %.1f  %s", hit.x, hit.z, hit.y, surfaceInfo(core.world->surfaceAt(hit.x, hit.z)).name);
        r.text(12 * u, y - 8 * s - 6 * u, s, buf, 0.8f, 0.82f, 0.86f, 0.9f);
        // The slope, and what the trucks make of it.
        float cr, cg, cb;
        const double deg = slopeDeg(*core.world, hit.x, hit.z);
        const char* note = slopeNote(deg, cr, cg, cb);
        std::snprintf(buf, sizeof buf, "SLOPE %.0f DEG  -  %s", deg, note);
        r.text(12 * u, y - 2 * (8 * s + 6 * u), s, buf, cr, cg, cb, 0.95f);
    }
    if (ledgeOn && haveHit) {  // (the line being dragged)
        const V3 a = ledgeA, b = snapped(hit);
        const int n = std::max(2, (int)(len(b - a) / 1.0));
        const View v = view(W, H);
        for (int k = 0; k <= n; k++) {
            const V3 p = a + (b - a) * ((double)k / n);
            float sx, sy;
            if (screenPoint(v, W, H, V3(p.x, core.world->height(p.x, p.z) + 0.4, p.z), sx, sy)) r.rect(sx - 2 * u, sy - 2 * u, 4 * u, 4 * u, 1, 0.7f, 0.3f, 1);
        }
    }
}

// The save prompts: over everything else, taking all clicks.
void Editor::drawFlow(Renderer& r, float u) {
    buttons.clear();
    r.rect(0, 0, (float)W, (float)H, 0, 0, 0, 0.45f);
    const float w = 520 * u, x = (W - w) / 2, s = px(1.4f * u), bh = 30 * u;
    overUi = true;
    if (flow.stage == SaveFlow::NAMING) {
        const float h = 168 * u, y = (H - h) / 2;
        pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, 0.97f);
        label(r, x + 16 * u, y + 14 * u, px(2.0f * u), "NAME THE LEVEL", 1.0f, 0.75f, 0.3f, 1);
        const float fy = y + 56 * u;
        r.rect(x + 16 * u, fy, w - 32 * u, 34 * u, 1, 1, 1, 0.1f);
        const std::string t = flow.text + (std::fmod(clock, 1.0) < 0.5 ? "_" : " ");
        r.text(x + 26 * u, fy + (34 * u - 8 * s) / 2, s, t, 1, 1, 1, 1);
        r.text(x + 16 * u, fy + 44 * u, px(1.1f * u), "TYPE A NAME, ENTER SAVES IT, ESC CANCELS", 0.65f, 0.67f, 0.72f, 1);
        button(r, x + w - 16 * u - 230 * u, y + h - 16 * u - bh, 110 * u, bh, "SAVE", B_NAME_OK, true, u);
        button(r, x + w - 16 * u - 110 * u, y + h - 16 * u - bh, 110 * u, bh, "CANCEL", B_NAME_CANCEL, false, u);
    } else if (flow.stage == SaveFlow::ASKING) {
        const float h = 150 * u, y = (H - h) / 2;
        pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, 0.97f);
        label(r, x + 16 * u, y + 14 * u, px(2.0f * u), "UNSAVED CHANGES", 1.0f, 0.75f, 0.3f, 1);
        r.text(x + 16 * u, y + 52 * u, px(1.2f * u), core.level.name + " HAS CHANGES THAT ARE NOT SAVED.", 0.85f, 0.86f, 0.9f, 1);
        const float bw = (w - 32 * u - 2 * 8 * u) / 3, by = y + h - 16 * u - bh;
        button(r, x + 16 * u, by, bw, bh, "SAVE (S)", B_ASK_SAVE, true, u);
        button(r, x + 16 * u + bw + 8 * u, by, bw, bh, "DISCARD (D)", B_ASK_DISCARD, false, u);
        button(r, x + 16 * u + 2 * (bw + 8 * u), by, bw, bh, "CANCEL (ESC)", B_ASK_CANCEL, false, u);
    }
}

void Editor::drawFiles(Renderer& r, float u) {
    const float w = 560 * u, x = (W - w) / 2, rowH = 26 * u, s = px(1.3f * u);
    const int show = std::min((int)files.size(), 14);
    const float h = 70 * u + rowH * show, y = (H - h) / 2;
    r.rect(0, 0, (float)W, (float)H, 0, 0, 0, 0.4f);
    pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, 0.95f);
    label(r, x + 16 * u, y + 12 * u, px(2.0f * u), "OPEN A LEVEL", 1.0f, 0.75f, 0.3f, 1);
    const int first = std::max(0, std::min(fileSel - show / 2, (int)files.size() - show));
    for (int k = 0; k < show; k++) {
        const int i = first + k;
        const Level& lv = files[i];
        const float ry = y + 50 * u + k * rowH;
        const bool on = i == fileSel;
        if (on) r.rect(x + 8 * u, ry, w - 16 * u, rowH, 1, 1, 1, 0.1f);
        buttons.push_back({x + 8 * u, ry, w - 16 * u, rowH, B_FILE + i});
        if (mx >= x + 8 * u && mx < x + w - 8 * u && my >= ry && my < ry + rowH) fileSel = i;
        const bool recovered = i < (int)fileRecovery.size() && fileRecovery[i];
        r.text(x + 16 * u, ry + (rowH - 8 * s) / 2, s, recovered ? "RECOVER AUTOSAVE: " + lv.name : lv.name, recovered ? 1.0f : 1, recovered ? 0.8f : 1, recovered ? 0.4f : 1, 1);
        const std::string tagText = std::string(mapName(lv.map)) + "  " + levelModeName(lv.mode) + (lv.section.on ? std::string("  SECTION ") + sectionKindName(lv.section.kind) + " " + std::to_string(lv.section.difficulty) : std::string()) +
                                   (lv.builtin ? "  (GAME'S: OPENS A COPY)" : "");
        r.text(x + w - 16 * u - r.textWidth(tagText, px(1.0f * u)), ry + (rowH - 8 * px(1.0f * u)) / 2, px(1.0f * u), tagText, 0.6f, 0.62f, 0.66f, 1);
    }
    overUi = true;
}

void Editor::draw(Renderer& r, int w, int h, double) {
    W = w, H = h;
    buttons.clear();
    overUi = false;
    const float u = std::max(0.75f, H / 720.0f);
    drawWorldMarks(r);
    drawToolbar(r, u);
    drawOptions(r, u);
    if (tool == OBJECTS) drawPalette(r, u);
    drawStatus(r, u);
    if (fileOpen) {
        buttons.clear();  // (only the dialog takes clicks)
        drawFiles(r, u);
    }
    if (flow.stage != SaveFlow::IDLE) drawFlow(r, u);
}
