#include "mainmenu.h"

#include <algorithm>

#include "vehicles.h"
#include "world.h"

namespace mainmenu {

const std::vector<DevEntry>& devEntries() {
    static const std::vector<DevEntry> e = {
        {A_DEV_EDITOR, "LEVEL EDITOR", "SCULPT A MAP, PLACE OBJECTS, MAKE LEVELS"},
        {A_DEV_SECTION, "SECTION EDITOR", "HAND-BUILD AN OBSTACLE SECTION"},
        {A_DEV_LAB, "MODEL LAB", "PICK, TWEAK OR REDO THE GAME'S MODELS"},
        {A_DEV_LEVELS, "LEVELS", "PLAY OR EDIT THE RACES, CLIMBS AND SECTIONS"},
        {A_DEV_TOOLS, "SPAWN, TELEPORT AND TOOLS", "THE DEV MENU: VEHICLES, TIME, VIEW, WORLD"},
        {A_DEV_HOTKEYS, "DEV HOTKEYS", "THE DEBUG KEYS (1-7, I, K, N, F2...) WORK DIRECTLY"},
        {A_DEV_DEBUG, "DEBUG SCREEN", "THE F3 STATS: FRAME TIMES, MEMORY, THE WORLD"},
        {A_DEV_CLASSIC, "CLASSIC MENU", "THE OLD TITLE PAGE: EVERY ROW, THE MAP PICKER"},
    };
    return e;
}

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}

void Nav::open(int map, int vehicle) {
    page = P_FRONT;
    sel = F_SANDBOX;
    career = false;
    mapIdx = 0;
    const std::vector<int>& maps = listedMaps();
    for (int i = 0; i < (int)maps.size(); i++)
        if (maps[i] == map) mapIdx = i;
    vehIdx = 0;
    const std::vector<int>& vs = listedVehicles();
    for (int i = 0; i < (int)vs.size(); i++)
        if (vs[i] == vehicle) vehIdx = i;
    hits.clear();
}

int Nav::count() const {
    switch (page) {
        case P_FRONT: return F_COUNT;
        case P_MAP: return (int)listedMaps().size();
        case P_VEHICLE: return (int)listedVehicles().size();
        case P_DEV: return (int)devEntries().size();
    }
    return 0;
}

int Nav::map() const { return listedMaps()[clampi(mapIdx, 0, (int)listedMaps().size() - 1)]; }
int Nav::vehicle() const { return listedVehicles()[clampi(vehIdx, 0, (int)listedVehicles().size() - 1)]; }

std::string Nav::title() const {
    switch (page) {
        case P_FRONT: return "CRUMPLE ZONE";
        case P_MAP: return career ? "CAREER: PICK A MAP" : "SANDBOX: PICK A MAP";
        case P_VEHICLE: return career ? "CAREER: PICK A VEHICLE" : "SANDBOX: PICK A VEHICLE";
        case P_DEV: return "DEVELOPER TOOLS";
    }
    return "";
}

void Nav::setSel(int i) {
    sel = clampi(i, 0, std::max(0, count() - 1));
    if (page == P_MAP) mapIdx = sel;
    if (page == P_VEHICLE) vehIdx = sel;
}

void Nav::move(int dir) {
    const int n = count();
    if (n <= 0) return;
    setSel((sel + dir + n) % n);
}

Result Nav::back() {
    switch (page) {
        case P_FRONT: return {};  // (the front page is the bottom: nothing to go back to)
        case P_MAP:
            page = P_FRONT, sel = career ? F_CAREER : F_SANDBOX;
            return {};
        case P_VEHICLE:
            page = P_MAP, sel = mapIdx;
            return {};
        case P_DEV:
            page = P_FRONT, sel = F_DEV;
            return {};
    }
    return {};
}

Result Nav::choose(int i) {
    setSel(i);
    switch (page) {
        case P_FRONT: {
            switch (sel) {
                case F_SANDBOX:
                case F_CAREER:
                    career = sel == F_CAREER;
                    page = P_MAP, sel = mapIdx;
                    return {};
                case F_MULTI: return {A_MULTIPLAYER};
                case F_DEV:
                    page = P_DEV, sel = 0;
                    return {};
                case F_QUIT: return {A_QUIT};
                case F_SETTINGS: return {A_SETTINGS};
            }
            return {};
        }
        case P_MAP:
            page = P_VEHICLE, sel = vehIdx;
            return {};
        case P_VEHICLE: {
            Result r;
            r.act = A_START, r.career = career, r.map = map(), r.vehicle = vehicle();
            return r;
        }
        case P_DEV: {
            Result r;
            r.act = devEntries()[sel].act;
            return r;
        }
    }
    return {};
}

Result Nav::key(int sc) {
    if (sc == K_ESC || sc == K_BACKSPACE) return back();
    if (sc == K_ENTER || sc == K_KPENTER || sc == K_SPACE) return choose(sel);
    const bool up = sc == K_W || sc == K_UP, down = sc == K_S || sc == K_DOWN, left = sc == K_A || sc == K_LEFT, right = sc == K_D || sc == K_RIGHT;
    if (up) move(-1);
    else if (down) move(1);
    else if (left || right) {
        // Sideways: along the front page's bottom row (DEV, QUIT); on the pick pages, nothing (rows are a column).
        if (page == P_FRONT && sel >= F_DEV) setSel(F_DEV + (sel - F_DEV + 1) % 2);
        else if (page == P_DEV && sel >= 0) {  // (a toggle row: left or right flips it, like the old menu)
            const Act a = devEntries()[sel].act;
            if (a == A_DEV_HOTKEYS || a == A_DEV_DEBUG) return choose(sel);
        }
    }
    return {};
}

int Nav::hitAt(float x, float y) const {
    for (const Hit& h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.row;
    return -1;
}

std::string Nav::hint(bool pad) const {
    const char* ok = pad ? "A OK" : "ENTER OK";
    const char* bk = pad ? "B BACK" : "ESC BACK";
    const char* mv = pad ? "D-PAD MOVE" : "W S MOVE";
    switch (page) {
        case P_FRONT: return std::string(mv) + "   " + ok;
        case P_MAP: return std::string(mv) + "   " + ok + " PICK THE VEHICLE   " + bk;
        case P_VEHICLE: return std::string(mv) + "   " + ok + " START   " + bk;
        case P_DEV: return std::string(mv) + "   " + ok + "   " + bk;
    }
    return "";
}

}  // namespace mainmenu
