#include "devmenu.h"

#include "sdl_min.h"
#include "vehicles.h"

namespace devmenu {

const char* groupName(Group g) {
    static const char* const n[G_COUNT] = {"SPAWN VEHICLE", "SPAWN AHEAD", "TELEPORT", "WORLD", "VEHICLE", "VIEW", "TOOLS"};
    return g >= 0 && g < G_COUNT ? n[g] : "";
}

const char* groupHint(Group g) {
    static const char* const n[G_COUNT] = {"DRIVE A DIFFERENT VEHICLE", "PARKED OR AI VEHICLES IN FRONT OF YOU", "SPAWN, SECTIONS, PLACES",
                                           "TIME OF DAY, SLOW MOTION, RELOAD THE MAP", "REPAIR, FLIP, REFUEL", "FREE CAM, BEAM VIEWS, FPS",
                                           "EDITOR, MODEL LAB, RELOAD, EXPORT KEYBINDS"};
    return g >= 0 && g < G_COUNT ? n[g] : "";
}

Action forKeyAction(KeyAction a) {
    if (a >= KA_DEV_VEHICLE1 && a <= KA_DEV_VEHICLE6) return {SWAP_VEHICLE, a - KA_DEV_VEHICLE1};
    switch (a) {
        case KA_DEV_SPAWN: return {SPAWN_PARKED, -1};
        case KA_DEV_TRAFFIC: return {SPAWN_TRAFFIC, 0};
        case KA_DEV_REMOVE: return {REMOVE_VEHICLE, 0};
        case KA_DEV_SLOWER: return {TIME_SLOWER, 0};
        case KA_DEV_FASTER: return {TIME_FASTER, 0};
        case KA_DEV_BEAM: return {BEAM_VIEW, -1};
        case KA_DEBUG_INFO: return {FPS, 0};
        case KA_DEV_EDITOR: return {EDITOR, 0};
        case KA_DEV_RELOAD: return {RELOAD, 0};
        case KA_DEV_SCENARIO: return {SCENARIO_CHECKS, 0};
        case KA_DEV_FREECAM: return {FREE_CAM, 0};
        default: return {};
    }
}

int keyActionOf(const Action& act) {
    for (int a = KA_DEV_VEHICLE1; a <= KA_DEV_SCENARIO; a++)
        if (forKeyAction((KeyAction)a) == act) return a;
    for (KeyAction a : {KA_DEBUG_INFO, KA_DEV_RELOAD})  // (always on: not dev-gated, still a menu entry's key)
        if (forKeyAction(a) == act) return a;
    return -1;
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

std::vector<Item> items(Group g, const Keybinds* kb) {
    std::vector<Item> r;
    auto bound = [&](KeyAction ka) { return kb ? "KEY " + kb->name(ka) : std::string(); };
    auto add = [&](Act a, int arg, const std::string& label, const std::string& hint = "") {
        const Action act{a, arg};
        const int ka = keyActionOf(act);
        r.push_back({act, label, !hint.empty() ? hint : ka >= 0 ? bound((KeyAction)ka) : std::string()});
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
            add(SPAWN_TRAFFIC, 0, "AI TRAFFIC: NEXT ON THE LIST");
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
            add(REPAIR, 0, "REPAIR WHERE IT STANDS", bound(KA_REPAIR));
            add(RESPAWN, 0, "RESPAWN AT THE START", kb ? bound(KA_REPAIR) + " TWICE" : std::string());
            add(FLIP_UPRIGHT, 0, "FLIP UPRIGHT", bound(KA_FLIP));
            add(REFUEL, 0, "REFUEL", "TANK AND SPARE CAN");
            break;
        case G_VIEW: {
            add(FREE_CAM, 0, "FREE CAM", kb ? bound(KA_DEV_FREECAM) + ": FLY, CARRY AND SPAWN VEHICLES" : "FLY, CARRY AND SPAWN VEHICLES");
            static const char* const v[4] = {"BEAM VIEW: OFF", "BEAM VIEW: X-RAY", "BEAM VIEW: BEAMS ONLY", "BEAM VIEW: PARTS"};  // (DebugView, render.h)
            for (int k = 0; k < 4; k++) add(BEAM_VIEW, k, v[k], k == 1 && kb ? bound(KA_DEV_BEAM) + " CYCLES THEM" : "");
            add(FPS, 0, "DEBUG INFO");
            break;
        }
        case G_TOOLS:
            add(EDITOR, 0, "LEVEL EDITOR");
            add(NEW_SECTION, 0, "NEW OBSTACLE SECTION", "THE EDITOR, A BLANK FIELD");
            add(MODEL_LAB, 0, "MODEL LAB", "THE MODEL YARD");
            add(RELOAD, 0, "RELOAD THE NEW BUILD");
            add(SCENARIO_CHECKS, 0, "SCENARIO CHECKS LIST", "WHILE A SCENARIO PLAYS");
            add(EXPORT_KEYBINDS, 0, "EXPORT KEYBINDS AS DEFAULTS", "WRITES BUILD/KEYBINDS-DEFAULTS.TXT");
            break;
        default: break;
    }
    return r;
}

bool dispatchKey(const Keybinds& kb, int sc, bool shift, bool devKeys, unsigned contexts, Handler h, void* user) {
    const Keybinds::Press p = kb.resolve(sc, shift, false, contexts, devKeys);
    if (p.action < 0 || !isDevAction((KeyAction)p.action)) return false;
    const Action a = forKeyAction((KeyAction)p.action);
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
