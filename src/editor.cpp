#include "editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hudkit.h"
#include "levelrun.h"
#include "vehicles.h"

using namespace hud;

namespace {

const char* kToolNames[Editor::TOOL_COUNT] = {"SCULPT", "PAINT", "WATER", "OBJECTS", "COURSE", "SETTINGS"};
const char* kToolHints[Editor::TOOL_COUNT] = {"RAISE, LOWER, SMOOTH, FLATTEN THE GROUND", "PAINT MUD, SAND, SNOW, ROCK...",
                                              "FLOOD A HOLLOW, OR DRY IT", "ROCKS, TREES, LOGS, RAMPS, SHOPS",
                                              "START AND GATES", "NAME, MODE, RULES, MEDALS"};
const char* kOpNames[4] = {"RAISE", "LOWER", "SMOOTH", "FLATTEN"};
const char* kObjNames[OBJ_KIND_COUNT] = {"ROCK", "RAMP / BLOCK", "LOG", "TREE", "REPAIR SHOP"};
const char* kTreeNames[3] = {"PINE", "BROADLEAF", "DEAD SNAG"};

// Button ids.
enum {
    B_TOOL = 100,            // + tool
    B_OP = 200,              // + brush op
    B_MAT = 300,             // + material
    B_OBJ = 400,             // + object kind
    B_SAVE = 500, B_OPEN, B_PLAY, B_UNDO, B_REDO, B_EXIT, B_NEWMAP,
    B_RAD_DN = 600, B_RAD_UP, B_STR_DN, B_STR_UP, B_SOFT, B_DEPTH_DN, B_DEPTH_UP, B_DRY,
    B_SIZE_DN, B_SIZE_UP, B_TREE, B_YAW_DN, B_YAW_UP, B_PITCH_DN, B_PITCH_UP, B_SLAB, B_W_DN, B_W_UP, B_LEN_DN, B_LEN_UP,
    B_OFFSET_DN, B_OFFSET_UP,
    B_OBJPROP = 900,    // + property index for object editor
    B_GATEW_DN, B_GATEW_UP, B_DELETE, B_EARLIER, B_LATER,
    B_SET = 700,             // + settings row (click selects it)
    B_FILE = 800,            // + file index
};

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

}  // namespace

void Editor::open(World& w, Renderer& r, const Level& lv) {
    rend = &r;
    core.begin(w, lv);
    active = true;
    wantPlay = wantClose = false;
    fileOpen = typing = confirmClose = false;
    sel = EditPick();
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
    // Mouse wheel zooms (flies in the direction looked).
    const View v = view(W, H);
    eye += norm(v.target - v.eye) * (dy * 4.0);
}

LevelObject Editor::makeObject(const V3& at) const {
    LevelObject o;
    o.kind = objKind;
    o.p = V3(at.x, 0, at.z);
    o.yaw = objYaw;
    switch (objKind) {
        case OBJ_ROCK:
            o.size = V3(rockSize * 1.15, rockSize * 0.8, rockSize);
            o.top = rockSize * 0.7;
            o.seed = nextSeed;
            o.col[0] = 1.0f;
            break;
        case OBJ_BOX:
            o.size = V3(boxLen / 2, boxHeight / 2, boxWidth / 2);
            o.pitch = boxPitch;
            o.sub = slab ? 1 : 0;
            o.col[0] = slab ? 0.55f : 0.62f, o.col[1] = slab ? 0.5f : 0.6f, o.col[2] = slab ? 0.45f : 0.56f;
            break;
        case OBJ_LOG: {
            const V3 f(std::cos(objYaw), 0, -std::sin(objYaw));
            o.p = V3(at.x, 0, at.z) - f * (logLen / 2);
            o.p2 = V3(at.x, 0, at.z) + f * (logLen / 2);
            o.size = V3(logR, 0, 0);
            break;
        }
        case OBJ_TREE:
            o.size = V3(0, treeHeight, 0);
            o.sub = treeKind;
            o.col[0] = 1.0f;
            break;
        case OBJ_STATION: o.name = "REPAIR SHOP"; break;
    }
    o = core.grounded(o);
    // Apply vertical offset (positive = up into the sky, negative = down into the ground).
    o.p.y += objOffset;
    return o;
}

void Editor::click() {
    if (!haveHit && hover.kind == EditPick::NONE) return;
    switch (tool) {
        case SCULPT:
        case PAINT:
            core.beginEdit(tool == SCULPT ? std::string(kOpNames[op]) : std::string("PAINT ") + paintMaterials()[material].name);
            stroke = true;
            if (tool == SCULPT && op == BRUSH_FLATTEN) flatY = hit.y, flatSet = true;
            applyBrush(1.0 / 60);
            break;
        case WATER: {
            if (!haveHit) break;
            if (shift) {
                core.dry(hit.x, hit.z, radius);
                say("DRIED");
                break;
            }
            bool spilled = false;
            const int n = core.flood(hit.x, hit.z, floodDepth, std::max(radius * 4, 40.0), &spilled);
            say(spilled ? "IT RAN OUT OVER LOWER GROUND (" + std::to_string(n) + " CELLS): DIG A DEEPER HOLLOW" : "FLOODED " + std::to_string(n) + " CELLS");
            break;
        }
        case OBJECTS:
            if (hover.kind == EditPick::OBJECT || hover.kind == EditPick::BASE) {
                sel = hover;
                if (sel.kind == EditPick::BASE) {  // a map's repair shop: picked up to move like your own
                    const int k = core.adoptBase(sel);
                    if (k >= 0) sel.kind = EditPick::OBJECT, sel.index = k, sel.baseKind = -1;
                }
                if (sel.kind == EditPick::OBJECT) {
                    dragging = true;
                    const LevelObject& o = core.level.objects[sel.index];
                    dragOffset = V3(o.p.x - hit.x, 0, o.p.z - hit.z);
                }
                say(sel.kind == EditPick::BASE ? "THE MAP'S OWN " + sel.name + ": DELETE TAKES IT OUT" : sel.name + ": DRAG TO MOVE, Q E TURN, DELETE");
            } else if (haveHit) {
                sel = EditPick();
                LevelObject o = makeObject(hit);
                const int k = core.addObject(o);
                nextSeed = nextSeed * 1103515245u + 12345u;
                sel.kind = EditPick::OBJECT, sel.index = k, sel.name = kObjNames[objKind];
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
                    say("START SET (FACING THE WAY YOU LOOK). CLICK AGAIN FOR GATES");
                } else {
                    Gate g;
                    g.p = V3(hit.x, 0, hit.z);
                    g.yaw = yaw;
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
    if (stroke) {
        core.endEdit();
        stroke = false;
    }
    dragging = false;
}

void Editor::applyBrush(double dt) {
    if (!haveHit) return;
    if (tool == SCULPT) {
        const double rate = strength * 4.0;  // m/s at full strength
        switch (op) {
            case BRUSH_RAISE: core.brush(BRUSH_RAISE, hit.x, hit.z, radius, rate * dt * (shift ? 0.25 : 1)); break;
            case BRUSH_LOWER: core.brush(BRUSH_LOWER, hit.x, hit.z, radius, rate * dt * (shift ? 0.25 : 1)); break;
            case BRUSH_SMOOTH: core.brush(BRUSH_SMOOTH, hit.x, hit.z, radius, std::min(1.0, strength * dt * 6)); break;
            case BRUSH_FLATTEN: core.brush(BRUSH_FLATTEN, hit.x, hit.z, radius, std::min(1.0, strength * dt * 8), flatSet ? flatY : hit.y); break;
        }
    } else if (tool == PAINT) {
        core.paint(hit.x, hit.z, radius, material, softPaint);
    }
}

void Editor::save() {
    if (core.level.name.empty() || core.level.name == "UNTITLED") {
        say("NAME IT FIRST: SETTINGS (6), THE NAME ROW");
        tool = SETTINGS;
        setRow = 0;
        return;
    }
    std::string path = core.level.file;
    if (path.empty() || core.level.builtin || path.find(userLevelFolder()) != 0) path = userLevelFolder() + "/" + levelFileName(core.level.name) + ".czl";
    std::string err;
    if (core.save(path, &err)) {
        core.level.file = path;
        core.level.builtin = false;
        say("SAVED " + path);
    } else {
        say("CAN'T SAVE: " + err, 4);
    }
}

void Editor::openFile(const Level& lv) {
    // A fresh copy of its map, then the level on it.
    World& w = *core.world;
    w = World(lv.map);
    Level l = lv;
    if (l.builtin) l.builtin = false, l.file.clear(), l.name = l.name + " (MINE)";
    open(w, *rend, l);
    fileOpen = false;
}

void Editor::key(int sc) {
    const uint8_t* ks = SDL_GetKeyboardState(nullptr);
    shift = ks[SC_LSHIFT] || ks[SC_RSHIFT];
    ctrl = ks[224] || ks[228];
    // The open dialog.
    if (fileOpen) {
        if (sc == SC_ESCAPE) fileOpen = false;
        else if (sc == SC_UP || sc == SC_W) fileSel = std::max(0, fileSel - 1);
        else if (sc == SC_DOWN || sc == SC_S) fileSel = std::min((int)files.size() - 1, fileSel + 1);
        else if ((sc == SC_RETURN || sc == SC_KP_ENTER) && fileSel >= 0 && fileSel < (int)files.size()) openFile(files[fileSel]);
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
        sel = EditPick();
        return;
    }
    if (ctrl && sc == SC_Y) {
        say(core.redo() ? "REDONE" : "NOTHING TO REDO", 1.2);
        return;
    }
    if (ctrl && sc == SC_S) {
        save();
        return;
    }
    if (sc >= SC_1 && sc < SC_1 + TOOL_COUNT) {
        tool = (Tool)(sc - SC_1);
        sel = EditPick();
        return;
    }
    switch (sc) {
        case SC_ESCAPE:
            if (sel.kind != EditPick::NONE) {
                sel = EditPick();
                return;
            }
            if (core.unsaved() && !confirmClose) {
                confirmClose = true;
                say("NOT SAVED: CTRL+S TO SAVE, ESC AGAIN TO LEAVE ANYWAY", 4);
                return;
            }
            wantClose = true;
            return;
        case SC_TAB:
            if (tool == SCULPT) op = (BrushOp)((op + 1) % 4);
            else if (tool == PAINT) material = (material + 1) % (int)paintMaterials().size();
            else if (tool == OBJECTS) objKind = (objKind + 1) % OBJ_KIND_COUNT;
            return;
        case SC_LEFTBRACKET: radius = std::max(1.0, radius / 1.2), gateWidth = std::max(4.0, gateWidth - 1); return;
        case SC_RIGHTBRACKET: radius = std::min(60.0, radius * 1.2), gateWidth = std::min(40.0, gateWidth + 1); return;
        case SC_DELETE:
        case SC_BACKSPACE: doButton(B_DELETE); return;
        case SC_F5: doButton(B_PLAY); return;
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
    // Q/E: brush/object size (Shift+Q/E: turn the selection).
    if (sc == SC_Q || sc == SC_E) {
        if (shift) {
            // Shift+Q/E: turn the selection or the next thing placed.
            const double turn = M_PI / 12;
            const double d = sc == SC_Q ? turn : -turn;
            if (tool == OBJECTS && sel.kind == EditPick::OBJECT) {
                LevelObject o = core.level.objects[sel.index];
                if (o.kind == OBJ_LOG) {
                    const V3 m = (o.p + o.p2) * 0.5, half = o.p2 - m;
                    const V3 r(half.x * std::cos(d) + half.z * std::sin(d), 0, -half.x * std::sin(d) + half.z * std::cos(d));
                    o.p = m - r, o.p2 = m + r;
                }
                o.yaw += d;
                core.setObject(sel.index, core.grounded(o), "turn");
            } else if (tool == COURSE && sel.kind == EditPick::GATE) {
                Gate g = core.level.gates[sel.index];
                g.yaw += d;
                core.setGate(sel.index, g, "turn");
            } else if (tool == COURSE && sel.kind == EditPick::START) {
                core.setStart(core.level.start.x, core.level.start.z, core.level.startYaw + d);
            } else {
                objYaw += d;
            }
        } else {
            // Q/E: brush size (sculpt/paint/water) or object size.
            if (tool == SCULPT || tool == PAINT || tool == WATER) {
                radius = clampd(radius * (sc == SC_Q ? 0.85 : 1.18), 1.0, 60.0);
            } else if (tool == OBJECTS) {
                if (objKind == OBJ_ROCK) {
                    rockSize = clampd(rockSize * (sc == SC_Q ? 0.85 : 1.15), 0.3, 4.0);
                } else if (objKind == OBJ_TREE) {
                    treeHeight = clampd(treeHeight * (sc == SC_Q ? 0.85 : 1.15), 3.0, 30.0);
                } else if (objKind == OBJ_LOG) {
                    logR = clampd(logR * (sc == SC_Q ? 0.85 : 1.15), 0.1, 0.8);
                } else if (objKind == OBJ_BOX) {
                    boxLen = clampd(boxLen * (sc == SC_Q ? 0.85 : 1.15), 0.5, 40.0);
                    boxWidth = clampd(boxWidth * (sc == SC_Q ? 0.85 : 1.15), 0.5, 30.0);
                    boxHeight = clampd(boxHeight * (sc == SC_Q ? 0.85 : 1.15), 0.1, 4.0);
                }
            }
        }
    }
}

void Editor::settingChange(int row, int dir) {
    const int nv = (int)vehicleCatalog().size();
    core.settings("set" + std::to_string(row), [&](Level& l) {
        switch (row) {
            case 1: l.mode = (l.mode + dir + LEVEL_MODE_COUNT) % LEVEL_MODE_COUNT; break;
            case 2: l.laps = std::min(std::max(l.laps + dir, 1), 20); break;
            case 3: l.vehicle = (l.vehicle + 1 + dir + nv + 1) % (nv + 1) - 1; break;
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

void Editor::doButton(int id) {
    confirmClose = false;
    if (id >= B_TOOL && id < B_TOOL + TOOL_COUNT) {
        tool = (Tool)(id - B_TOOL);
        sel = EditPick();
        return;
    }
    if (id >= B_OP && id < B_OP + 4) {
        op = (BrushOp)(id - B_OP);
        return;
    }
    if (id >= B_MAT && id < B_MAT + (int)paintMaterials().size()) {
        material = id - B_MAT;
        return;
    }
    if (id >= B_OBJ && id < B_OBJ + OBJ_KIND_COUNT) {
        objKind = id - B_OBJ;
        sel = EditPick();
        return;
    }
    if (id >= B_SET && id < B_SET + 11) {
        setRow = id - B_SET;
        if (setRow == 0) typing = true;
        else settingChange(setRow, 1);
        return;
    }
    if (id >= B_FILE && id < B_FILE + (int)files.size()) {
        openFile(files[id - B_FILE]);
        return;
    }
    // Object editor properties.
    if (id >= B_OBJPROP && sel.kind == EditPick::OBJECT && sel.index < (int)core.level.objects.size()) {
        LevelObject ob = core.level.objects[sel.index];
        const int prop = id - B_OBJPROP;
        switch (prop) {
            case 1: ob.p.x -= 0.5; break;
            case 2: ob.p.x += 0.5; break;
            case 3: ob.p.y -= 0.5; break;
            case 4: ob.p.y += 0.5; break;
            case 5: ob.p.z -= 0.5; break;
            case 6: ob.p.z += 0.5; break;
            case 7: ob.size.z = clampd(ob.size.z - 0.25, 0.25, 15.0); break;
            case 8: ob.size.z = clampd(ob.size.z + 0.25, 0.25, 15.0); break;
            case 9: ob.size.y = clampd(ob.size.y - 0.25, 0.05, 5.0); break;
            case 10: ob.size.y = clampd(ob.size.y + 0.25, 0.05, 5.0); break;
            case 11: ob.size.x = clampd(ob.size.x - 0.25, 0.25, 20.0); break;
            case 12: ob.size.x = clampd(ob.size.x + 0.25, 0.25, 20.0); break;
            case 13: ob.yaw -= M_PI / 36; break;
            case 14: ob.yaw += M_PI / 36; break;
            case 15: ob.pitch = clampd(ob.pitch - M_PI / 36, -0.6, 0.6); break;
            case 16: ob.pitch = clampd(ob.pitch + M_PI / 36, -0.6, 0.6); break;
        }
        core.setObject(sel.index, ob, "edit");
        return;
    }
    auto bump = [](double& v, double f, double lo, double hi) { v = clampd(v * f, lo, hi); };
    switch (id) {
        case B_SAVE: save(); break;
        case B_OPEN:
            files = listLevels();
            fileSel = 0;
            fileOpen = true;
            break;
        case B_NEWMAP: {
            newMap = nextListedMap(newMap, 1);
            Level l;
            l.map = newMap;
            l.name = "UNTITLED";
            World& w = *core.world;
            w = World(newMap);
            open(w, *rend, l);
            say(std::string("NEW LEVEL ON ") + mapName(newMap));
            break;
        }
        case B_PLAY:
            if (core.level.mode != LEVEL_FREE && (!core.level.hasStart || core.level.gates.empty())) {
                say("A RACE OR SPRINT NEEDS A START AND GATES (COURSE TOOL), OR SET THE MODE TO FREE DRIVE", 4);
                break;
            }
            wantPlay = true;
            break;
        case B_UNDO: say(core.undo() ? "UNDONE " : "NOTHING TO UNDO", 1.2), sel = EditPick(); break;
        case B_REDO: say(core.redo() ? "REDONE" : "NOTHING TO REDO", 1.2); break;
        case B_EXIT:
            if (core.unsaved() && !confirmClose) {
                confirmClose = true;
                say("NOT SAVED: SAVE FIRST, OR CLICK EXIT AGAIN", 4);
                return;
            }
            wantClose = true;
            break;
        case B_RAD_DN: bump(radius, 1 / 1.2, 1, 60); break;
        case B_RAD_UP: bump(radius, 1.2, 1, 60); break;
        case B_STR_DN: strength = clampd(strength - 0.1, 0.1, 1.0); break;
        case B_STR_UP: strength = clampd(strength + 0.1, 0.1, 1.0); break;
        case B_SOFT: softPaint = !softPaint; break;
        case B_DEPTH_DN: floodDepth = clampd(floodDepth - 0.25, 0.25, 6.0); break;
        case B_DEPTH_UP: floodDepth = clampd(floodDepth + 0.25, 0.25, 6.0); break;
        case B_DRY:
            if (haveHit) core.dry(hit.x, hit.z, radius);
            break;
        case B_SIZE_DN:
            if (objKind == OBJ_ROCK) bump(rockSize, 1 / 1.15, 0.3, 4);
            else if (objKind == OBJ_TREE) bump(treeHeight, 1 / 1.15, 3, 30);
            else if (objKind == OBJ_LOG) bump(logR, 1 / 1.15, 0.1, 0.8);
            else if (objKind == OBJ_BOX) bump(boxHeight, 1 / 1.25, 0.1, 4);
            break;
        case B_SIZE_UP:
            if (objKind == OBJ_ROCK) bump(rockSize, 1.15, 0.3, 4);
            else if (objKind == OBJ_TREE) bump(treeHeight, 1.15, 3, 30);
            else if (objKind == OBJ_LOG) bump(logR, 1.15, 0.1, 0.8);
            else if (objKind == OBJ_BOX) bump(boxHeight, 1.25, 0.1, 4);
            break;
        case B_TREE: treeKind = (treeKind + 1) % 3; break;
        case B_YAW_DN: objYaw -= M_PI / 12; break;
        case B_YAW_UP: objYaw += M_PI / 12; break;
        case B_PITCH_DN: boxPitch = clampd(boxPitch - M_PI / 36, -0.6, 0.6); break;
        case B_PITCH_UP: boxPitch = clampd(boxPitch + M_PI / 36, -0.6, 0.6); break;
        case B_SLAB: slab = !slab; break;
        case B_W_DN: bump(boxWidth, 1 / 1.2, 0.5, 30); break;
        case B_W_UP: bump(boxWidth, 1.2, 0.5, 30); break;
        case B_LEN_DN:
            if (objKind == OBJ_LOG) bump(logLen, 1 / 1.2, 1, 20);
            else bump(boxLen, 1 / 1.2, 0.5, 40);
            break;
        case B_LEN_UP:
            if (objKind == OBJ_LOG) bump(logLen, 1.2, 1, 20);
            else bump(boxLen, 1.2, 0.5, 40);
            break;
        case B_OFFSET_DN:
            objOffset = clampd(objOffset - 0.5, -5.0, 15.0);
            break;
        case B_OFFSET_UP:
            objOffset = clampd(objOffset + 0.5, -5.0, 15.0);
            break;
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
            if (sel.kind == EditPick::OBJECT) core.removeObject(sel.index), say("DELETED");
            else if (sel.kind == EditPick::BASE) say(core.removeBase(sel) ? "TOOK THE MAP'S " + sel.name + " OUT" : "THAT ONE CAN'T BE TAKEN OUT");
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
    shift = keys[SC_LSHIFT] || keys[SC_RSHIFT];
    ctrl = keys[224] || keys[228];
    // Fly (WASD, Q/E turn, Space up, Shift fast, higher = faster).
    if (!typing && !fileOpen && !ctrl && tool != SETTINGS) {
        const double ground = core.world->height(eye.x, eye.z);
        const double speed = (12 + std::max(0.0, eye.y - ground) * 0.8) * (shift ? 3 : 1);
        const V3 f(std::cos(yaw), 0, -std::sin(yaw)), l(-f.z, 0, f.x);
        V3 move;
        if (keys[SC_W] || keys[SC_UP]) move += f;
        if (keys[SC_S] || keys[SC_DOWN]) move -= f;
        if (keys[SC_A] || keys[SC_LEFT]) move -= l;
        if (keys[SC_D] || keys[SC_RIGHT]) move += l;
        if (keys[SC_E] || keys[SC_SPACE]) move.y += 1;
        if (keys[SC_Q]) move.y -= 1;
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
    if (tool == OBJECTS || tool == COURSE) {
        const EditPick p = core.pick(o, d, hitT, tool == OBJECTS);
        const bool wanted = tool == OBJECTS ? (p.kind == EditPick::OBJECT || p.kind == EditPick::BASE) : (p.kind == EditPick::GATE || p.kind == EditPick::START);
        if (wanted) hover = p;
    }
    // Holding the button.
    if (lmb && stroke) applyBrush(dt);
    if (lmb && dragging && haveHit) {
        if (sel.kind == EditPick::OBJECT && sel.index < (int)core.level.objects.size()) {
            LevelObject ob = core.level.objects[sel.index];
            const V3 to(hit.x + dragOffset.x, 0, hit.z + dragOffset.z);
            if (ob.kind == OBJ_LOG) {
                const V3 m = (ob.p + ob.p2) * 0.5, sh = V3(to.x - m.x, 0, to.z - m.z);
                ob.p += sh, ob.p2 += sh;
            } else {
                ob.p.x = to.x, ob.p.z = to.z;
            }
            core.setObject(sel.index, core.grounded(ob), "drag");
        } else if (sel.kind == EditPick::GATE && sel.index < (int)core.level.gates.size()) {
            Gate g = core.level.gates[sel.index];
            g.p = V3(hit.x + dragOffset.x, 0, hit.z + dragOffset.z);
            core.setGate(sel.index, g, "drag");
        } else if (sel.kind == EditPick::START) {
            core.setStart(hit.x + dragOffset.x, hit.z + dragOffset.z, core.level.startYaw);
        }
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
    // The brush ring, lying on the ground.
    if (haveHit && (tool == SCULPT || tool == PAINT || tool == WATER)) {
        const int n = 72;
        const float cr = tool == PAINT ? paintMaterials()[material].col[0] : 1.0f, cg = tool == PAINT ? paintMaterials()[material].col[1] : 0.75f,
                    cb = tool == PAINT ? paintMaterials()[material].col[2] : 0.3f;
        for (int k = 0; k < n; k++) {
            const double a = 2 * M_PI * k / n;
            const double x = hit.x + std::cos(a) * radius, z = hit.z + std::sin(a) * radius;
            dotAt(V3(x, w.height(x, z) + 0.15, z), 4 * u, cr, cg, cb, 0.95f);
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
        tag(p + V3(0, 3, 0), on ? "START (SELECTED)" : "START", 0.3f, 1.0f, 0.4f);
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
        tag(V3(g.p.x, std::max(ya, yb) + 5, g.p.z), last && core.level.mode != LEVEL_FREE ? std::to_string(k + 1) + " FINISH" : std::to_string(k + 1), cr, cg, cb);
    }
    // Hovered / selected objects.
    auto mark = [&](const EditPick& p, bool selected) {
        if (p.kind != EditPick::OBJECT && p.kind != EditPick::BASE) return;
        tag(p.at + V3(0, 2.5, 0), (p.kind == EditPick::BASE ? "MAP " : "") + p.name + (selected ? " (SELECTED)" : ""), selected ? 1.0f : 0.8f,
            selected ? 0.85f : 0.9f, selected ? 0.3f : 1.0f);
        for (int k = 0; k < 24; k++) {
            const double a = 2 * M_PI * k / 24;
            const double x = p.at.x + std::cos(a) * 1.8, z = p.at.z + std::sin(a) * 1.8;
            dotAt(V3(x, w.height(x, z) + 0.2, z), 4 * u, selected ? 1.0f : 0.8f, selected ? 0.8f : 0.9f, selected ? 0.3f : 1.0f, 0.9f);
        }
    };
    if (sel.kind == EditPick::OBJECT && sel.index < (int)core.level.objects.size()) {
        EditPick p = sel;
        const LevelObject& ob = core.level.objects[sel.index];
        p.at = ob.kind == OBJ_LOG ? (ob.p + ob.p2) * 0.5 : ob.p;
        p.name = kObjNames[ob.kind];
        mark(p, true);
    } else if (sel.kind == EditPick::BASE) {
        mark(sel, true);
    }
    if (hover != sel) mark(hover, false);
}

void Editor::drawToolbar(Renderer& r, float u) {
    const float x = 12 * u, w = 128 * u, bh = 28 * u;
    float y = 12 * u;
    pill(r, x - 6 * u, y - 6 * u, w + 12 * u, (bh + 4 * u) * TOOL_COUNT + 8 * u, 0, 0, 0, 0.55f);
    for (int t = 0; t < TOOL_COUNT; t++) y = button(r, x, y, w, bh, std::to_string(t + 1) + "  " + kToolNames[t], B_TOOL + t, tool == t, u);
    // File and history.
    y += 14 * u;
    pill(r, x - 6 * u, y - 6 * u, w + 12 * u, (bh + 4 * u) * 7 + 8 * u, 0, 0, 0, 0.55f);
    y = button(r, x, y, w, bh, "SAVE", B_SAVE, false, u);
    y = button(r, x, y, w, bh, "OPEN...", B_OPEN, false, u);
    y = button(r, x, y, w, bh, std::string("NEW: ") + mapName(nextListedMap(newMap, 1)), B_NEWMAP, false, u);
    y = button(r, x, y, w, bh, "UNDO", B_UNDO, false, u);
    y = button(r, x, y, w, bh, "REDO", B_REDO, false, u);
    y = button(r, x, y, w, bh, "PLAY IT", B_PLAY, false, u);
    button(r, x, y, w, bh, confirmClose ? "EXIT: SURE?" : "EXIT", B_EXIT, confirmClose, u);
}

void Editor::drawOptions(Renderer& r, float u) {
    const float w = 290 * u, x = W - w - 12 * u, bh = 26 * u, s = px(1.2f * u), half = (w - 4 * u) / 2;
    float y = 12 * u;
    const float top = y;
    std::vector<std::string> info;
    // (Drawn in two passes: the panel's height isn't known till the end, so draw into a
    // scratch list of rows first. Simpler: a fixed tall panel per tool.)
    const float panelH = tool == PAINT ? 560 * u : tool == SETTINGS ? 420 * u : tool == OBJECTS ? 420 * u : 300 * u;
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
    switch (tool) {
        case SCULPT:
            for (int k = 0; k < 4; k++) button(r, x + (k % 2) * (half + 4 * u), y + (k / 2) * (bh + 4 * u), half, bh, kOpNames[k], B_OP + k, op == k, u);
            y += 2 * (bh + 4 * u) + 6 * u;
            row("SIZE", fmt1(radius, " M"), B_RAD_DN, B_RAD_UP);
            row("STRENGTH", std::to_string((int)std::lround(strength * 100)) + "%", B_STR_DN, B_STR_UP);
            y += 6 * u;
            note("HOLD LEFT BUTTON: BRUSH");
            note("WHEEL OR [ ]: SIZE   TAB: NEXT BRUSH");
            note("SHIFT: FINE");
            note("FLATTEN: TO THE HEIGHT YOU CLICK");
            break;
        case PAINT: {
            const auto& m = paintMaterials();
            for (int k = 0; k < (int)m.size(); k++) {
                const float bx = x + (k % 2) * (half + 4 * u), by = y + (k / 2) * (bh + 4 * u);
                button(r, bx + 22 * u, by, half - 22 * u, bh, m[k].name, B_MAT + k, material == k, u);
                r.rect(bx, by + 3 * u, 18 * u, bh - 6 * u, m[k].col[0], m[k].col[1], m[k].col[2], 1);
            }
            y += ((int)m.size() + 1) / 2 * (bh + 4 * u) + 6 * u;
            row("SIZE", fmt1(radius, " M"), B_RAD_DN, B_RAD_UP);
            button(r, x, y, w, bh, softPaint ? "EDGES: SOFT (SPECKLED)" : "EDGES: HARD", B_SOFT, false, u);
            y += bh + 10 * u;
            note("HOLD LEFT BUTTON TO PAINT. TAB: NEXT");
            note("IT DRIVES LIKE IT LOOKS: MUD BOGS,");
            note("SAND SINKS, ICE SLIDES.");
            break;
        }
        case WATER:
            row("DEPTH", fmt1(floodDepth, " M"), B_DEPTH_DN, B_DEPTH_UP);
            row("DRY SIZE", fmt1(radius, " M"), B_RAD_DN, B_RAD_UP);
            button(r, x, y, w, bh, "DRY UNDER THE MOUSE", B_DRY, false, u);
            y += bh + 10 * u;
            note("CLICK IN A HOLLOW: FILLS IT THIS DEEP");
            note("(IT SPREADS OVER LOWER GROUND, SO DIG");
            note("A BASIN WITH SCULPT > LOWER FIRST)");
            note("SHIFT+CLICK: DRY");
            break;
        case OBJECTS:
            for (int k = 0; k < OBJ_KIND_COUNT; k++) button(r, x + (k % 2) * (half + 4 * u), y + (k / 2) * (bh + 4 * u), half, bh, kObjNames[k], B_OBJ + k, objKind == k, u);
            y += 3 * (bh + 4 * u) + 6 * u;
            if (objKind == OBJ_ROCK) row("SIZE", fmt1(rockSize * 2, " M"), B_SIZE_DN, B_SIZE_UP);
            if (objKind == OBJ_TREE) {
                row("HEIGHT", fmt1(treeHeight, " M"), B_SIZE_DN, B_SIZE_UP);
                button(r, x, y, w, bh, std::string("KIND: ") + kTreeNames[treeKind], B_TREE, false, u);
                y += bh + 4 * u;
            }
            if (objKind == OBJ_LOG) {
                row("THICKNESS", fmt1(logR * 2, " M"), B_SIZE_DN, B_SIZE_UP);
                row("LENGTH", fmt1(logLen, " M"), B_LEN_DN, B_LEN_UP);
            }
            if (objKind == OBJ_BOX) {
                row("LENGTH", fmt1(boxLen, " M"), B_LEN_DN, B_LEN_UP);
                row("WIDTH", fmt1(boxWidth, " M"), B_W_DN, B_W_UP);
                row("THICKNESS", fmt1(boxHeight, " M"), B_SIZE_DN, B_SIZE_UP);
                row("TILT (RAMP)", std::to_string((int)std::lround(boxPitch * 180 / M_PI)) + " DEG", B_PITCH_DN, B_PITCH_UP);
                button(r, x, y, w, bh, slab ? "ROCK SLAB (GRIPS LIKE ROCK)" : "CONCRETE BLOCK", B_SLAB, false, u);
                y += bh + 4 * u;
            }
            row("TURN", std::to_string((int)std::lround(std::fmod(objYaw * 180 / M_PI + 3600, 360))) + " DEG", B_YAW_DN, B_YAW_UP);
            row("OFFSET (UP/DOWN)", fmt1(objOffset, " M"), B_OFFSET_DN, B_OFFSET_UP);
            if (sel.kind == EditPick::OBJECT && sel.index < (int)core.level.objects.size()) {
                // Object editor panel.
                const LevelObject& ob = core.level.objects[sel.index];
                note("EDITING: " + std::string(kObjNames[ob.kind]));
                row("POS X", fmt1(ob.p.x, " M"), B_OBJPROP + 1, B_OBJPROP + 2);
                row("POS Y", fmt1(ob.p.y, " M"), B_OBJPROP + 3, B_OBJPROP + 4);
                row("POS Z", fmt1(ob.p.z, " M"), B_OBJPROP + 5, B_OBJPROP + 6);
                if (ob.kind == OBJ_BOX) {
                    row("SIZE W", fmt1(ob.size.z * 2, " M"), B_OBJPROP + 7, B_OBJPROP + 8);
                    row("SIZE H", fmt1(ob.size.y * 2, " M"), B_OBJPROP + 9, B_OBJPROP + 10);
                    row("SIZE D", fmt1(ob.size.x * 2, " M"), B_OBJPROP + 11, B_OBJPROP + 12);
                }
                row("ROT YAW", std::to_string((int)std::lround(std::fmod(ob.yaw * 180 / M_PI + 3600, 360))) + " DEG", B_OBJPROP + 13, B_OBJPROP + 14);
                row("ROT PITCH", std::to_string((int)std::lround(std::fmod(ob.pitch * 180 / M_PI + 3600, 360))) + " DEG", B_OBJPROP + 15, B_OBJPROP + 16);
                button(r, x, y, w, bh, "DELETE SELECTED (DEL)", B_DELETE, false, u);
                y += bh + 4 * u;
            }
            y += 6 * u;
            note("CLICK: PLACE   CLICK ONE: SELECT");
            note("DRAG: MOVE   Q E: SIZE   SHIFT+Q E: TURN");
            note("OFFSET ROW: PLACE IN SKY OR UNDERGROUND");
            note("DEL: DELETE (THE MAP'S OWN TOO)");
            break;
        case COURSE: {
            const int n = (int)core.level.gates.size();
            note(core.level.hasStart ? (std::to_string(n) + (n == 1 ? " GATE" : " GATES") + " IN ORDER") : "FIRST CLICK: THE START");
            row("GATE WIDTH", fmt1(gateWidth, " M"), B_GATEW_DN, B_GATEW_UP);
            if (sel.kind == EditPick::GATE) {
                button(r, x, y, half, bh, "EARLIER", B_EARLIER, false, u);
                button(r, x + half + 4 * u, y, half, bh, "LATER", B_LATER, false, u);
                y += bh + 4 * u;
                button(r, x, y, w, bh, "DELETE GATE " + std::to_string(sel.index + 1), B_DELETE, false, u);
                y += bh + 4 * u;
            }
            y += 6 * u;
            note("CLICK THE GROUND: NEW GATE, FACING");
            note("THE WAY YOU LOOK (AFTER THE SELECTED)");
            note("DRAG A GATE OR THE START TO MOVE IT");
            note("Q E: TURN IT    [ ]: WIDTH");
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
        default: break;
    }
    if (mx >= x - 8 * u && my >= top - 6 * u && my < top - 6 * u + panelH) overUi = true;
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
    const char* keys = "WASD FLY  MOUSE WHEEL ZOOM  Q E SIZE/UP DOWN  RIGHT-DRAG LOOK  SHIFT FAST  1-6 TOOLS  CTRL+Z/Y UNDO/REDO  CTRL+S SAVE  F5 PLAY  ESC EXIT";
    const float ks = px(1.0f * u);
    r.rect(W - r.textWidth(keys, ks) - 20 * u, y - 8 * ks - 10 * u, r.textWidth(keys, ks) + 20 * u, 8 * ks + 10 * u, 0, 0, 0, 0.45f);
    r.text(W - r.textWidth(keys, ks) - 10 * u, y - 8 * ks - 5 * u, ks, keys, 0.7f, 0.72f, 0.76f, 1);
    if (toastTime > 0 && !toast.empty()) centeredPill(r, W / 2.0f, y - 40 * u, px(1.4f * u), toast, 0, 0, 0, (float)std::min(1.0, toastTime * 2), 1, 0.85f, 0.5f);
    if (haveHit) {
        std::snprintf(buf, sizeof buf, "%.0f, %.0f  H %.1f  %s", hit.x, hit.z, hit.y, surfaceInfo(core.world->surfaceAt(hit.x, hit.z)).name);
        r.text(12 * u, y - 8 * s - 6 * u, s, buf, 0.8f, 0.82f, 0.86f, 0.9f);
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
        r.text(x + 16 * u, ry + (rowH - 8 * s) / 2, s, lv.name, 1, 1, 1, 1);
        const std::string tagText = std::string(mapName(lv.map)) + "  " + levelModeName(lv.mode) + (lv.builtin ? "  (GAME'S: OPENS A COPY)" : "");
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
    drawStatus(r, u);
    if (fileOpen) {
        buttons.clear();  // (only the dialog takes clicks)
        drawFiles(r, u);
    }
}
