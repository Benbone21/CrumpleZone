#include "devmenu.h"

#include "sdl_min.h"
#include "vehicles.h"

namespace devmenu {

static_assert(kMenuKey == SC_F12, "the dev menu key");

const char* groupName(Group g) {
    static const char* const n[G_COUNT] = {"SPAWN VEHICLE", "SPAWN AHEAD", "TELEPORT", "WORLD", "VEHICLE", "VIEW", "TOOLS"};
    return g >= 0 && g < G_COUNT ? n[g] : "";
}

const char* groupHint(Group g) {
    static const char* const n[G_COUNT] = {"DRIVE A DIFFERENT VEHICLE", "PARKED OR AI VEHICLES IN FRONT OF YOU", "SPAWN, SECTIONS, PLACES",
                                           "TIME OF DAY, SLOW MOTION, RELOAD THE MAP", "REPAIR, FLIP, REFUEL", "BEAM VIEWS, FPS",
                                           "EDITOR, MODEL LAB, RELOAD"};
    return g >= 0 && g < G_COUNT ? n[g] : "";
}

namespace {
// Which key does what on each side of the DEV KEYS / GAMEPLAY KEYS toggle. A key is a dev key while it has a row here
// (in DEV KEYS mode the row's action runs and the gameplay action is skipped; in GAMEPLAY mode the row is ignored and the
// key's gameplay action in main.cpp's key switch runs, or nothing if it has none). Moving a key to the gameplay side is
// deleting its row; moving one to the dev side is adding a row.
//   when: 0 always, 1 only while driving (on foot it keeps its gameplay job), 2 only while a scenario plays
struct KeyRow {
    int key;
    Action dev;
    int when;
    const char* gameplay;  // what it does in GAMEPLAY mode (for people: the help text and the tests)
};
const KeyRow kKeys[] = {
    {SC_1, {SWAP_VEHICLE, 0}, 0, ""}, {SC_2, {SWAP_VEHICLE, 1}, 0, ""}, {SC_3, {SWAP_VEHICLE, 2}, 0, ""}, {SC_4, {SWAP_VEHICLE, 3}, 0, ""},
    {SC_5, {SWAP_VEHICLE, 4}, 0, ""}, {SC_6, {SWAP_VEHICLE, 5}, 0, ""}, {SC_7, {SWAP_VEHICLE, 6}, 0, ""},
    {SC_I, {SPAWN_PARKED, -1}, 1, "TRUCK INVENTORY (ON FOOT)"},
    {SC_K, {SPAWN_TRAFFIC, 0}, 0, ""},
    {SC_DELETE, {REMOVE_VEHICLE, 0}, 0, ""},
    {SC_BACKSPACE, {REMOVE_VEHICLE, 0}, 0, ""},
    {SC_LEFTBRACKET, {TIME_SLOWER, 0}, 0, ""},
    {SC_RIGHTBRACKET, {TIME_FASTER, 0}, 0, ""},
    {SC_N, {BEAM_VIEW, -1}, 0, ""},
    {SC_F3, {FPS, 0}, 0, ""},
    {SC_F2, {EDITOR, 0}, 0, ""},
    {SC_F5, {RELOAD, 0}, 0, ""},
    {SC_J, {SCENARIO_CHECKS, 0}, 2, "GET OUT / IN"},
};
}  // namespace

Action hotkeyAction(int sc, const KeyCtx& ctx) {
    for (const KeyRow& k : kKeys)
        if (k.key == sc && (k.when == 0 || (k.when == 1 && !ctx.onFoot) || (k.when == 2 && ctx.scenario))) return k.dev;
    return {};
}

const char* gameplayJob(int sc) {
    for (const KeyRow& k : kKeys)
        if (k.key == sc) return k.gameplay;
    return "";
}

Action fromKey(int sc, bool devHotkeys, const KeyCtx& ctx) { return devHotkeys ? hotkeyAction(sc, ctx) : Action(); }

std::string hotkeyName(const Action& a) {
    switch (a.act) {
        case SWAP_VEHICLE: return std::string("KEY ") + (char)('1' + a.arg);
        case SPAWN_PARKED: return a.arg < 0 ? "KEY I" : "";
        case SPAWN_TRAFFIC: return "KEY K";
        case REMOVE_VEHICLE: return "KEY DEL";
        case TIME_SLOWER: return "KEY [";
        case TIME_FASTER: return "KEY ]";
        case BEAM_VIEW: return a.arg < 0 ? "KEY N" : "";
        case FPS: return "KEY F3";
        case EDITOR: return "KEY F2";
        case RELOAD: return "KEY F5";
        case SCENARIO_CHECKS: return "KEY J";
        default: return "";
    }
}

bool offlineOnly(Act a) {
    switch (a) {
        case TELEPORT_SPAWN:
        case TELEPORT_SECTION:
        case TELEPORT_PLACE:
        case RESET_WORLD:
        case REFUEL: return true;  // (new powers: the old keys had no teleport, reload or fuel cheat for a guest to use)
        default: return false;
    }
}

std::vector<Item> items(Group g) {
    std::vector<Item> r;
    auto add = [&](Act a, int arg, const std::string& label, const std::string& hint = "") {
        const Action act{a, arg};
        r.push_back({act, label, hint.empty() ? hotkeyName(act) : hint});
    };
    const auto& cat = vehicleCatalog();
    const auto& listed = listedVehicles();
    switch (g) {
        case G_SPAWN_VEHICLE:  // the vehicle you're in becomes the one picked
            for (int k = 0; k < (int)listed.size(); k++) add(SWAP_VEHICLE, k, cat[listed[k]].name);
            break;
        case G_SPAWN_AHEAD:
            for (int k = 0; k < (int)listed.size(); k++) add(SPAWN_PARKED, k, std::string("PARKED ") + cat[listed[k]].name);
            add(SPAWN_PARKED, -1, "PARKED: NEXT ON THE LIST");
            add(SPAWN_TRAFFIC, 0, "AI TRAFFIC: NEXT ON THE LIST", "KEY K");
            add(REMOVE_VEHICLE, 0, "REMOVE THIS VEHICLE");
            break;
        case G_TELEPORT:
            add(TELEPORT_SPAWN, 0, "MAP SPAWN", "BACK TO THE START, DAMAGE STAYS");
            break;
        case G_WORLD:
            {
                static const char* const tod[5] = {"TIME: LET THE CLOCK RUN", "TIME: MORNING", "TIME: NOON", "TIME: EVENING", "TIME: NIGHT"};  // (TimeOfDay, daylight.h)
                for (int t = 0; t < 5; t++) add(TIME_OF_DAY, t, tod[t]);
            }
            add(TIME_SLOWER, 0, "SLOW MOTION: SLOWER");
            add(TIME_FASTER, 0, "SLOW MOTION: FASTER");
            add(RESET_WORLD, 0, "RESET THE WORLD", "RELOADS THE MAP");
            break;
        case G_VEHICLE:
            add(REPAIR, 0, "REPAIR WHERE IT STANDS", "KEY R");
            add(RESPAWN, 0, "RESPAWN AT THE START", "KEY R R");
            add(FLIP_UPRIGHT, 0, "FLIP UPRIGHT", "KEY T");
            add(REFUEL, 0, "REFUEL", "TANK AND SPARE CAN");
            break;
        case G_VIEW: {
            static const char* const v[4] = {"BEAM VIEW: OFF", "BEAM VIEW: X-RAY", "BEAM VIEW: BEAMS ONLY", "BEAM VIEW: PARTS"};  // (DebugView, render.h)
            for (int k = 0; k < 4; k++) add(BEAM_VIEW, k, v[k], k == 1 ? "KEY N CYCLES THEM" : "");
            add(FPS, 0, "FRAME RATE COUNTER");
            break;
        }
        case G_TOOLS:
            add(EDITOR, 0, "LEVEL EDITOR");
            add(NEW_SECTION, 0, "NEW OBSTACLE SECTION", "THE EDITOR, A BLANK FIELD");
            add(MODEL_LAB, 0, "MODEL LAB", "THE MODEL YARD");
            add(RELOAD, 0, "RELOAD THE NEW BUILD");
            add(SCENARIO_CHECKS, 0, "SCENARIO CHECKS LIST", "WHILE A SCENARIO PLAYS");
            break;
        default: break;
    }
    return r;
}

bool dispatchKey(int sc, bool devHotkeys, const KeyCtx& ctx, Handler h, void* user) {
    const Action a = fromKey(sc, devHotkeys, ctx);
    if (!a) return false;
    h(user, a);
    return true;
}

bool dispatchMenu(const Action& a, Handler h, void* user) {
    if (!a) return false;
    h(user, a);
    return true;
}

}  // namespace devmenu
