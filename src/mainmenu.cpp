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
        {A_DEV_MENUSCENE, "EDIT MENU SCENE", "PLACE THE CABIN, THE TRUCKS, THE CAMERA AND THE SUN OF THIS MENU"},
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
const te::Rules kNameRules{te::Kind::Level, 24};
const te::Rules kSeedRules{te::Kind::Level, 20};

}

std::string defaultWorldName(const std::vector<WorldSave>& worlds) {
    for (int n = 1;; n++) {
        const std::string name = "WORLD " + std::to_string(n);
        bool taken = false;
        for (const WorldSave& w : worlds) taken = taken || w.name == name;
        if (!taken) return name;
    }
}

std::vector<int> newWorldMaps() {
    std::vector<int> v = careerMaps();
    for (int m : customWorldMaps()) v.push_back(m);
    return v;
}

bool customWorld(int map) {
    for (int m : customWorldMaps())
        if (m == map) return true;
    return false;
}

void Nav::rerollSeed() {
    seedText = std::to_string(freshWorldSeed(worlds));
    seedEdit.reset();
}

uint64_t Nav::seedPick() const {
    const uint64_t s = parseSeed(seedText);
    return s ? s : freshWorldSeed(worlds);
}

std::vector<int> startVehicles() {
    std::vector<int> out;
    for (int v : listedVehicles())
        if (startableVehicle(v)) out.push_back(v);
    return out;
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
    typing = false;
    delIdx = -1;
    diffIdx = DIFF_MEDIUM, modeIdx = MODE_PERSONAL;
    startIdx = 0;
    const std::vector<int> sv = startVehicles();
    for (int i = 0; i < (int)sv.size(); i++)
        if (sv[i] == vehicle) startIdx = i;
}

int Nav::count() const {
    switch (page) {
        case P_FRONT: return F_COUNT;
        case P_MAP: return (int)listedMaps().size();
        case P_VEHICLE: return (int)listedVehicles().size();
        case P_DEV: return (int)devEntries().size();
        case P_WORLDS: return (int)worlds.size() + 1;  // (then NEW WORLD)
        case P_NEW_WORLD: return N_COUNT;
        case P_DELETE_WORLD: return 2;  // YES, NO
        case P_WORLD_MAP: return (int)newWorldMaps().size();
        case P_START_VEHICLE: return (int)startVehicles().size();
    }
    return 0;
}

int Nav::map() const { return listedMaps()[clampi(mapIdx, 0, (int)listedMaps().size() - 1)]; }
int Nav::worldMap() const {
    const std::vector<int> m = newWorldMaps();
    return m[clampi(worldMapIdx, 0, (int)m.size() - 1)];
}
int Nav::vehicle() const { return listedVehicles()[clampi(vehIdx, 0, (int)listedVehicles().size() - 1)]; }
int Nav::startVehicle() const {
    const std::vector<int> sv = startVehicles();
    return sv.empty() ? 0 : sv[clampi(startIdx, 0, (int)sv.size() - 1)];
}

std::string Nav::title() const {
    switch (page) {
        case P_FRONT: return "CRUMPLE ZONE";
        case P_MAP: return career ? "CAREER: PICK A MAP" : "SANDBOX: PICK A MAP";
        case P_VEHICLE: return career ? "CAREER: PICK A VEHICLE" : "SANDBOX: PICK A VEHICLE";
        case P_DEV: return "DEVELOPER TOOLS";
        case P_WORLDS: return "CAREER: PICK A WORLD";
        case P_NEW_WORLD: return "CAREER: NEW WORLD";
        case P_DELETE_WORLD: return delIdx >= 0 && delIdx < (int)worlds.size() ? "DELETE " + worlds[delIdx].name + "?" : "DELETE WORLD?";
        case P_WORLD_MAP: return "NEW WORLD: WHERE?";
        case P_START_VEHICLE: return "NEW WORLD: PICK YOUR FIRST VEHICLE";
    }
    return "";
}

void Nav::setSel(int i) {
    if (i >= kHitDelete) return;  // (a button, not a row: hovering it moves nothing)
    if (page == P_NEW_WORLD && i == N_SEED && !customPick()) return;  // (a built map has no seed)
    const int old = sel;
    sel = clampi(i, 0, std::max(0, count() - 1));
    if (sel != old || page != P_NEW_WORLD) typing = false;
    if (page == P_MAP) mapIdx = sel;
    if (page == P_VEHICLE) vehIdx = sel;
}

void Nav::move(int dir) {
    const int n = count();
    if (n <= 0) return;
    int to = (sel + dir + n) % n;
    if (page == P_NEW_WORLD && to == N_SEED && !customPick()) to = (to + dir + n) % n;  // (skipped on a built map)
    setSel(to);
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
        case P_WORLDS:
            page = P_FRONT, sel = F_CAREER;
            return {};
        case P_NEW_WORLD:
            typing = false;
            page = P_WORLDS, sel = (int)worlds.size();
            return {};
        case P_DELETE_WORLD:
            page = P_WORLDS, sel = clampi(delIdx, 0, (int)worlds.size());
            return {};
        case P_WORLD_MAP:
            page = P_NEW_WORLD, sel = N_MAP;
            return {};
        case P_START_VEHICLE:
            page = P_NEW_WORLD, sel = N_VEH;
            return {};
    }
    return {};
}

void Nav::cycle(int row, int dir) {
    auto wrap = [&](int v, int n) { return n <= 0 ? 0 : ((v + dir) % n + n) % n; };
    switch (row) {
        case N_MAP: worldMapIdx = wrap(worldMapIdx, (int)newWorldMaps().size()); break;
        case N_SEED:
            if (customPick()) rerollSeed(), typing = false;
            break;
        case N_DIFF: diffIdx = wrap(diffIdx, DIFF_COUNT); break;
        case N_MODE: modeIdx = wrap(modeIdx, 2); break;
        case N_VEH: startIdx = wrap(startIdx, (int)startVehicles().size()); break;
    }
}

Result Nav::choose(int i) {
    setSel(i);
    switch (page) {
        case P_FRONT: {
            switch (sel) {
                case F_SANDBOX:
                    career = false;
                    page = P_MAP, sel = mapIdx;
                    return {};
                case F_CAREER:
                    career = true;
                    page = P_WORLDS, sel = 0;
                    return {};
                case F_MULTI: return {A_MULTIPLAYER};
                case F_DEV:
                    page = P_DEV, sel = 0;
                    return {};
                case F_QUIT: return {A_QUIT};
                case F_SETTINGS: return {A_SETTINGS};
                case F_LEVELS: return {A_LEVELS};
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
        case P_WORLDS: {
            if (i == kHitDelete) {  // (the DELETE button: the focused world)
                if (sel >= (int)worlds.size()) return {};
                delIdx = sel;
                page = P_DELETE_WORLD, sel = 1;  // (on NO)
                return {};
            }
            if (sel >= (int)worlds.size()) {  // NEW WORLD
                page = P_NEW_WORLD, sel = N_NAME;
                worldMapIdx = 0;  // (the map last picked, when a career world can be on it)
                const std::vector<int> maps = newWorldMaps();
                for (int k = 0; k < (int)maps.size(); k++)
                    if (maps[k] == map()) worldMapIdx = k;
                newName = defaultWorldName(worlds);
                nameEdit.reset();
                rerollSeed();  // (every new custom world its own land, unless a seed is typed)
                typing = false;
                return {};
            }
            if (worlds[sel].damaged) return {};  // (a damaged save can only be deleted)
            Result r;
            r.act = A_CAREER_CONTINUE, r.career = true, r.world = worlds[sel].slug;
            return r;
        }
        case P_NEW_WORLD: {
            if (sel == N_NAME || sel == N_SEED) {
                if (sel == N_SEED && !customPick()) return {};
                typing = !typing;
                nameEdit.reset(), seedEdit.reset();
                return {};
            }
            if (sel == N_MAP) {
                page = P_WORLD_MAP, sel = clampi(worldMapIdx, 0, count() - 1);
                return {};
            }
            if (sel == N_VEH) {
                page = P_START_VEHICLE, sel = clampi(startIdx, 0, count() - 1);
                return {};
            }
            if (sel == N_CREATE) {
                std::string name = newName;
                while (!name.empty() && name.back() == ' ') name.pop_back();
                size_t lead = 0;
                while (lead < name.size() && name[lead] == ' ') lead++;
                name.erase(0, lead);
                Result r;
                r.act = A_CAREER_NEW, r.career = true;
                r.worldName = name.empty() ? defaultWorldName(worlds) : name;
                r.map = worldMap(), r.difficulty = diffIdx, r.mode = modeIdx, r.vehicle = startVehicle();
                r.seed = customPick() ? seedPick() : 0;
                return r;
            }
            cycle(sel, 1);
            return {};
        }
        case P_DELETE_WORLD: {
            if (sel == 1 || delIdx < 0 || delIdx >= (int)worlds.size()) return back();
            Result r;
            r.act = A_CAREER_DELETE, r.career = true, r.world = worlds[delIdx].slug;
            worlds.erase(worlds.begin() + delIdx);
            page = P_WORLDS, sel = clampi(delIdx, 0, (int)worlds.size());
            delIdx = -1;
            return r;
        }
        case P_WORLD_MAP:
            worldMapIdx = sel;
            page = P_NEW_WORLD, sel = customPick() ? N_SEED : N_MAP;
            typing = false;
            return {};
        case P_START_VEHICLE:
            startIdx = sel;
            page = P_NEW_WORLD, sel = N_CREATE;
            return {};
    }
    return {};
}

bool Nav::text(const std::string& s) {
    if (page != P_NEW_WORLD || !typing) return false;
    if (sel == N_SEED) return te::insert(seedText, seedEdit, s, kSeedRules);
    return te::insert(newName, nameEdit, s, kNameRules);
}

Result Nav::key(int sc, bool ctrl, bool shift) {
    if (page == P_NEW_WORLD && typing) {
        if (sc == K_ESC || sc == K_ENTER || sc == K_KPENTER) {
            typing = false;
            return {};
        }
        if (sc == K_UP || sc == K_DOWN) {
            typing = false;
            move(sc == K_UP ? -1 : 1);
            return {};
        }
        if (sel == N_SEED) te::key(seedText, seedEdit, sc, ctrl, shift, kSeedRules);
        else te::key(newName, nameEdit, sc, ctrl, shift, kNameRules);  // (the letters arrive as text(); a key the field doesn't use does nothing)
        return {};
    }
    if (page == P_WORLDS && sc == K_DELETE && sel < (int)worlds.size()) {  // (the pad's X is mapped to this)
        delIdx = sel;
        page = P_DELETE_WORLD, sel = 1;  // (on NO)
        return {};
    }
    if (sc == K_ESC || sc == K_BACKSPACE) return back();
    if (sc == K_ENTER || sc == K_KPENTER || sc == K_SPACE) return choose(sel);
    const bool up = sc == K_W || sc == K_UP, down = sc == K_S || sc == K_DOWN, left = sc == K_A || sc == K_LEFT, right = sc == K_D || sc == K_RIGHT;
    if (page == P_START_VEHICLE && (left || right || up || down)) {  // (the cards are a row: every way steps along it)
        move(left || up ? -1 : 1);
        return {};
    }
    if (up) move(-1);
    else if (down) move(1);
    else if (left || right) {
        // Sideways: along the front page's bottom row (DEV, QUIT); on the pick pages, nothing (rows are a column).
        if (page == P_NEW_WORLD) cycle(sel, right ? 1 : -1);
        else if (page == P_DELETE_WORLD) setSel(1 - sel);
        else if (page == P_FRONT && sel >= F_DEV) setSel(F_DEV + (sel - F_DEV + 1) % 2);
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
        case P_WORLDS:
            if (sel >= (int)worlds.size()) return std::string(mv) + "   " + ok + " NEW WORLD   " + bk;
            return std::string(mv) + "   " + ok + " CONTINUE   " + (pad ? "X DELETE   " : "DEL DELETE   ") + bk;
        case P_WORLD_MAP: return std::string(mv) + "   " + ok + " PICK   " + bk;
        case P_START_VEHICLE: return std::string(pad ? "D-PAD" : "A D") + " OR CLICK A VEHICLE   " + ok + " PICK IT   " + bk;
        case P_NEW_WORLD:
            if (typing) return sel == N_SEED ? "TYPE A SEED: DIGITS OR WORDS   ENTER DONE   ESC DONE" : "TYPE A NAME   ENTER DONE   ESC DONE";
            if (sel == N_SEED) return std::string(mv) + (pad ? "   D-PAD SIDEWAYS NEW SEED   " : "   A D NEW SEED   ") + ok + " TYPE ONE   " + bk;
            if (sel == N_MAP || sel == N_VEH)
                return std::string(mv) + (pad ? "   D-PAD SIDEWAYS CHANGE   " : "   A D CHANGE   ") + ok + (sel == N_MAP ? " SEE THE MAPS   " : " SEE THE VEHICLES   ") + bk;
            return std::string(mv) + (pad ? "   D-PAD SIDEWAYS CHANGE   " : "   A D CHANGE   ") + ok + (sel == N_CREATE ? " CREATE   " : "   ") + bk;
        case P_DELETE_WORLD: return std::string(mv) + "   " + ok + "   " + bk;
    }
    return "";
}

}  // namespace mainmenu
