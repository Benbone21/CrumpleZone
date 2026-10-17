// The dev menu (devmenu.h): the debug and test keys do nothing by themselves with DEV HOTKEYS OFF, every one of
// their actions is a dev menu entry that works whatever the setting is, and the player's keys are never dev keys.
// No SDL or GL: the same lookup main.cpp runs for a key press, and a recorder standing in for what it does.
#include <cmath>
#include <set>
#include <vector>

#include "../src/devmenu.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "../src/vehicles.h"
#include "harness.h"

namespace {

struct Rec {
    std::vector<devmenu::Action> got;
};
void record(void* user, const devmenu::Action& a) { static_cast<Rec*>(user)->got.push_back(a); }

// The dev actions a key press does (what main.cpp runs): the key, as the game's key switch sees it.
devmenu::Action press(const Keybinds& kb, int sc, bool devKeys, unsigned contexts = KCTX_DRIVE, bool shift = false) {
    Rec r;
    devmenu::dispatchKey(kb, sc, shift, devKeys, contexts, record, &r);
    return r.got.empty() ? devmenu::Action() : r.got[0];
}

const int kDevKeys[] = {SC_1, SC_2, SC_3, SC_4, SC_5, SC_I, SC_K, SC_N, SC_DELETE, SC_LEFTBRACKET, SC_RIGHTBRACKET, SC_F2};

}  // namespace

SCENARIO(devKeysOff, "devmenu/hotkeys-off", "With the GAMEPLAY KEYS mode (DEV HOTKEYS OFF) the dev keys do nothing; in DEV KEYS mode they do what they always did") {
    const Keybinds kb;
    Rec off, on;
    for (int sc : kDevKeys) {
        devmenu::dispatchKey(kb, sc, false, false, KCTX_DRIVE, record, &off);
        devmenu::dispatchKey(kb, sc, false, true, KCTX_DRIVE, record, &on);
    }
    ctx.check("GAMEPLAY KEYS: no dev key reaches a dev action", off.got.empty());
    ctx.check("DEV KEYS: every dev key does", on.got.size() == sizeof kDevKeys / sizeof kDevKeys[0]);
    ctx.check("DEV KEYS: 1 is the first listed vehicle, 5 the fifth", press(kb, SC_1, true).act == devmenu::SWAP_VEHICLE && press(kb, SC_1, true).arg == 0 && press(kb, SC_5, true).arg == 4);
    ctx.check("DEV KEYS: I spawns the next parked vehicle, K AI traffic, N cycles the beam view",
              press(kb, SC_I, true).act == devmenu::SPAWN_PARKED && press(kb, SC_I, true).arg == -1 && press(kb, SC_K, true).act == devmenu::SPAWN_TRAFFIC &&
                  press(kb, SC_N, true).act == devmenu::BEAM_VIEW && press(kb, SC_N, true).arg == -1);
    ctx.check("DEV KEYS: DEL removes a vehicle, [ ] slow motion, F2 the editor",
              press(kb, SC_DELETE, true).act == devmenu::REMOVE_VEHICLE && press(kb, SC_LEFTBRACKET, true).act == devmenu::TIME_SLOWER &&
                  press(kb, SC_RIGHTBRACKET, true).act == devmenu::TIME_FASTER && press(kb, SC_F2, true).act == devmenu::EDITOR);
    ctx.check("F3 (debug info) and F5 (load the new build) are always on, not dev keys: the same key press in either mode",
              !press(kb, SC_F3, true) && !press(kb, SC_F5, true) && kb.resolve(SC_F3, false, false, KCTX_DRIVE, false).action == KA_DEBUG_INFO &&
                  kb.resolve(SC_F3, false, false, KCTX_DRIVE, false).key == SC_F3 && kb.resolve(SC_F5, false, false, KCTX_DRIVE, false).action == KA_DEV_RELOAD &&
                  kb.resolve(SC_F5, false, false, KCTX_DRIVE, false).key == SC_F5 && !isDevAction(KA_DEBUG_INFO) && !isDevAction(KA_DEV_RELOAD) &&
                  devmenu::forKeyAction(KA_DEBUG_INFO).act == devmenu::FPS && devmenu::forKeyAction(KA_DEV_RELOAD).act == devmenu::RELOAD);
    ctx.check("I on foot is the truck inventory, never a spawn (even in DEV KEYS mode)", !press(kb, SC_I, true, KCTX_FOOT) &&
                                                                                          kb.resolve(SC_I, false, false, KCTX_FOOT, true).action == KA_INVENTORY);
    ctx.check("J: the scenario's checks list only while one plays and in DEV KEYS mode; otherwise get out / in",
              !press(kb, SC_J, true) && !press(kb, SC_J, false, KCTX_DRIVE | KCTX_SCENARIO) && press(kb, SC_J, true, KCTX_DRIVE | KCTX_SCENARIO).act == devmenu::SCENARIO_CHECKS &&
                  kb.resolve(SC_J, false, false, KCTX_DRIVE, true).action == KA_GET_IN_OUT);
}

SCENARIO(devKeysPlayer, "devmenu/player-keys", "The player's keys are never dev tools, even in DEV KEYS mode; the dev menu key and the mode toggle always work") {
    const Keybinds kb;
    const int player[] = {SC_W, SC_A, SC_S, SC_D, SC_SPACE, SC_R, SC_T, SC_G, SC_C, SC_M, SC_L, SC_H, SC_P, SC_X, SC_E, SC_Q, SC_V, SC_O, SC_Y, SC_U, SC_Z, SC_F, SC_B,
                          SC_J, SC_TAB, SC_ESCAPE, SC_RETURN, SC_F1, SC_F4, SC_F10, SC_F11, SC_MINUS, SC_EQUALS, SC_8, SC_9, SC_0};
    bool any = false;
    for (int sc : player)
        for (unsigned c : {(unsigned)KCTX_DRIVE, (unsigned)KCTX_FOOT}) any |= (bool)press(kb, sc, true, c);
    ctx.check("no player key is a dev tool", !any);
    ctx.check("F12 is the dev menu's, the grave key flips the mode, F1 backs out of a playtest: dev actions that always answer",
              kb.key(KA_DEV_MENU) == SC_F12 && kb.key(KA_DEV_TOGGLE) == 53 && kb.key(KA_PLAYTEST_BACK) == SC_F1 && !isDevAction(KA_DEV_MENU) && !isDevAction(KA_DEV_TOGGLE) &&
                  !isDevAction(KA_PLAYTEST_BACK) && kb.resolve(SC_F1, false, false, KCTX_DRIVE, false).action == KA_PLAYTEST_BACK &&
                  kb.resolve(SC_F1, false, false, KCTX_DRIVE, false).key == SC_F1);
    ctx.check("they are in the DEV group of the keybinds page", actionGroup(KA_DEV_MENU) == KG_DEV && actionGroup(KA_DEV_VEHICLE1) == KG_DEV && actionGroup(KA_THROTTLE) == KG_DRIVING);
    ctx.check("and key names", keyName(SC_F12) == "F12" && keyName(53) == "GRAVE");
}

SCENARIO(devMenuReach, "devmenu/menu-reaches-everything", "Every dev key's action is a dev menu entry that fires with the setting OFF; offline-only ones are marked") {
    using namespace devmenu;
    std::set<int> inMenu;  // acts the menu has
    int entries = 0;
    for (int g = 0; g < G_COUNT; g++)
        for (const Item& it : items((Group)g)) {
            inMenu.insert(it.action.act);
            entries++;
        }
    ctx.check("every group has entries", [&] {
        for (int g = 0; g < G_COUNT; g++)
            if (items((Group)g).empty()) return false;
        return true;
    }());
    bool reach = true;
    for (int a = KA_DEV_VEHICLE1; a <= KA_DEV_SCENARIO; a++) reach &= inMenu.count(forKeyAction((KeyAction)a).act) > 0;
    reach &= inMenu.count(SCENARIO_CHECKS) > 0;
    ctx.check("each hotkey's action has a menu entry", reach);

    // The spawn rows: one per listed vehicle, in the order the number keys have.
    const std::vector<Item> swap = items(G_SPAWN_VEHICLE);
    bool order = swap.size() == listedVehicles().size();
    for (size_t k = 0; order && k < swap.size(); k++)
        order = swap[k].action.act == SWAP_VEHICLE && swap[k].action.arg == (int)k && swap[k].label == vehicleCatalog()[listedVehicles()[k]].name &&
                forKeyAction((KeyAction)(KA_DEV_VEHICLE1 + (int)k)) == swap[k].action;
    ctx.check("SPAWN VEHICLE lists the listed vehicles in the number keys' order", order);
    bool named = false;
    for (const Item& it : swap) named |= it.label.find("BLAZER") != std::string::npos;
    ctx.check("the Blazer is among them", named);

    // Menu entries fire with the hotkeys OFF (the setting never reaches dispatchMenu).
    Rec viaMenu;
    int fired = 0;
    for (int g = 0; g < G_COUNT; g++)
        for (const Item& it : items((Group)g)) fired += dispatchMenu(it.action, record, &viaMenu);
    ctx.check("every entry dispatches", fired == entries && (int)viaMenu.got.size() == entries);
    Rec none;
    ctx.check("a NONE action is ignored", !dispatchMenu(Action(), record, &none) && none.got.empty());

    // The same action by key and by menu is the same thing to the code that does it.
    Rec key, menu;
    dispatchKey(Keybinds(), SC_N, false, true, KCTX_DRIVE, record, &key);
    dispatchMenu({BEAM_VIEW, -1}, record, &menu);
    ctx.check("key and menu give one Action value", key.got.size() == 1 && menu.got.size() == 1 && key.got[0].act == menu.got[0].act && key.got[0].arg == menu.got[0].arg);

    ctx.check("teleports, reset world and refuel are refused in a multiplayer session; the old keys' actions are not",
              offlineOnly(TELEPORT_SPAWN) && offlineOnly(TELEPORT_SECTION) && offlineOnly(TELEPORT_PLACE) && offlineOnly(RESET_WORLD) && offlineOnly(REFUEL) &&
                  !offlineOnly(SWAP_VEHICLE) && !offlineOnly(SPAWN_PARKED) && !offlineOnly(SPAWN_TRAFFIC) && !offlineOnly(REMOVE_VEHICLE) && !offlineOnly(REPAIR));
    ctx.check("the groups have names", std::string(groupName(G_SPAWN_VEHICLE)) == "SPAWN VEHICLE" && std::string(groupName(G_TOOLS)) == "TOOLS");
    Keybinds moved;
    moved.bind(KA_DEV_VEHICLE1, SC_8);  // (a dev tool may share a key with a gameplay action: the mode decides)
    bool hint1 = false, hintN = false;
    for (const Item& it : items(G_SPAWN_VEHICLE, &moved)) hint1 |= it.action.arg == 0 && it.hint == "KEY 8";
    for (const Item& it : items(G_VIEW, &moved)) hintN |= it.action.act == BEAM_VIEW && it.hint.find("KEY N") == 0;
    ctx.check("the menu hints name the key as it is bound now", hint1 && hintN);
    ctx.check("every dev key action maps to a dev tool and back", [&] {
        for (int a = KA_DEV_VEHICLE1; a <= KA_DEV_SCENARIO; a++)
            if (!isDevAction((KeyAction)a) || !forKeyAction((KeyAction)a) || keyActionOf(forKeyAction((KeyAction)a)) != a) return false;
        return !forKeyAction(KA_THROTTLE) && !forKeyAction(KA_DEV_MENU);
    }());
    ctx.check("EXPORT KEYBINDS is in the TOOLS group", [&] {
        for (const Item& it : items(G_TOOLS))
            if (it.action.act == EXPORT_KEYBINDS) return true;
        return false;
    }());
}

SCENARIO(devTeleport, "devmenu/teleport", "A dev teleport sets the player's car down upright at the spot, facing the way asked, without a jolt") {
    Sim& sim = ctx.sim;
    Car& c = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    sim.player = (int)sim.cars.size() - 1;
    ctx.run(1.0);
    const double x = 40, z = -25, yaw = 1.0;
    sim.teleportPlayer(x, z, yaw);
    const Frame f = sim.cars[sim.player].frame();
    ctx.check("at the spot", std::hypot(f.com.x - x, f.com.z - z) < 1.0);
    ctx.check("facing the asked way", std::fabs(std::atan2(-f.fwd.z, f.fwd.x) - yaw) < 0.15);
    ctx.check("level", f.up.y > 0.95);
    ctx.check("at rest", len(f.vel) < 0.5);
    (void)c;
    ctx.run(1.5);
    ctx.check("and stays sensible after settling", std::isfinite(sim.cars[sim.player].frame().com.y) && sim.cars[sim.player].frame().up.y > 0.9);
}
