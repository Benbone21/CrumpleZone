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

const int kDevKeys[] = {SC_1, SC_2, SC_3, SC_4, SC_5, SC_6, SC_7, SC_I, SC_K, SC_N, SC_DELETE, SC_BACKSPACE, SC_LEFTBRACKET, SC_RIGHTBRACKET, SC_F2, SC_F3, SC_F5};

}  // namespace

SCENARIO(devKeysOff, "devmenu/hotkeys-off", "With DEV HOTKEYS OFF the dev keys do nothing; with it ON they do what they always did") {
    Rec off, on;
    for (int sc : kDevKeys) {
        devmenu::dispatchKey(sc, false, devmenu::KeyCtx(), record, &off);
        devmenu::dispatchKey(sc, true, devmenu::KeyCtx(), record, &on);
    }
    ctx.check("OFF: no dev key reaches a dev action", off.got.empty());
    ctx.check("ON: every dev key does", on.got.size() == sizeof kDevKeys / sizeof kDevKeys[0]);
    ctx.check("ON: 1 is the first listed vehicle, 5 the fifth", devmenu::hotkeyAction(SC_1).act == devmenu::SWAP_VEHICLE && devmenu::hotkeyAction(SC_1).arg == 0 &&
                                                                  devmenu::hotkeyAction(SC_5).arg == 4);
    ctx.check("ON: I spawns the next parked vehicle, K AI traffic, N cycles the beam view",
              devmenu::fromKey(SC_I, true).act == devmenu::SPAWN_PARKED && devmenu::fromKey(SC_I, true).arg == -1 &&
                  devmenu::fromKey(SC_K, true).act == devmenu::SPAWN_TRAFFIC && devmenu::fromKey(SC_N, true).act == devmenu::BEAM_VIEW &&
                  devmenu::fromKey(SC_N, true).arg == -1);
    ctx.check("ON: DEL and Backspace remove a vehicle, [ ] slow motion, F2 the editor, F3 the FPS counter, F5 the reload",
              devmenu::fromKey(SC_DELETE, true).act == devmenu::REMOVE_VEHICLE && devmenu::fromKey(SC_BACKSPACE, true).act == devmenu::REMOVE_VEHICLE &&
                  devmenu::fromKey(SC_LEFTBRACKET, true).act == devmenu::TIME_SLOWER && devmenu::fromKey(SC_RIGHTBRACKET, true).act == devmenu::TIME_FASTER &&
                  devmenu::fromKey(SC_F2, true).act == devmenu::EDITOR && devmenu::fromKey(SC_F3, true).act == devmenu::FPS &&
                  devmenu::fromKey(SC_F5, true).act == devmenu::RELOAD);

    devmenu::KeyCtx foot;
    foot.onFoot = true;
    devmenu::KeyCtx scenario;
    scenario.scenario = true;
    ctx.check("I on foot is the truck inventory, never a spawn (even with the hotkeys on)", !devmenu::fromKey(SC_I, true, foot));
    ctx.check("J is the scenario's checks list only while one plays, and only with the hotkeys on",
              !devmenu::fromKey(SC_J, true) && !devmenu::fromKey(SC_J, false, scenario) && devmenu::fromKey(SC_J, true, scenario).act == devmenu::SCENARIO_CHECKS);
}

SCENARIO(devKeysPlayer, "devmenu/player-keys", "The player's keys are never dev keys, and F12 is the dev menu's, which can't be rebound") {
    const int player[] = {SC_W, SC_A, SC_S, SC_D, SC_SPACE, SC_R, SC_T, SC_G, SC_C, SC_M, SC_L, SC_H, SC_P, SC_X, SC_E, SC_Q, SC_V, SC_O, SC_Y, SC_U, SC_Z, SC_F, SC_B,
                          SC_J, SC_TAB, SC_ESCAPE, SC_RETURN, SC_F1, SC_F4, SC_F10, SC_F11, SC_MINUS, SC_EQUALS, SC_9, SC_0, SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT};
    bool any = false;
    for (int sc : player) any |= devmenu::hotkeyAction(sc).act != devmenu::NONE;
    ctx.check("no player key is a dev hotkey", !any);
    ctx.check("F12 is the menu key and is no hotkey action", devmenu::kMenuKey == SC_F12 && !devmenu::hotkeyAction(SC_F12));
    Keybinds kb;
    ctx.check("F12 can't be bound to a driving action", !bindable(SC_F12) && !kb.bind(KA_WINCH, SC_F12) && kb.isDefault());
    ctx.check("and it names itself F12", keyName(SC_F12) == "F12");
    ctx.check("the key mode toggle (`) is no driving key, no dev hotkey and can't be bound",
              !bindable(devmenu::kModeKey) && !devmenu::hotkeyAction(devmenu::kModeKey) && keyName(devmenu::kModeKey) == "`");
    ctx.check("keys with a gameplay job say so: I is the inventory, J get out/in; the pure dev keys have none",
              std::string(devmenu::gameplayJob(SC_I)) != "" && std::string(devmenu::gameplayJob(SC_J)) != "" && std::string(devmenu::gameplayJob(SC_N)) == "");
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
    for (int sc : kDevKeys) reach &= inMenu.count(hotkeyAction(sc).act) > 0;
    reach &= inMenu.count(SCENARIO_CHECKS) > 0;
    ctx.check("each hotkey's action has a menu entry", reach);

    // The spawn rows: one per listed vehicle, in the order the number keys have.
    const std::vector<Item> swap = items(G_SPAWN_VEHICLE);
    bool order = swap.size() == listedVehicles().size();
    for (size_t k = 0; order && k < swap.size(); k++)
        order = swap[k].action.act == SWAP_VEHICLE && swap[k].action.arg == (int)k && swap[k].label == vehicleCatalog()[listedVehicles()[k]].name &&
                hotkeyAction(SC_1 + (int)k) == swap[k].action;
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
    dispatchKey(SC_N, true, KeyCtx(), record, &key);
    dispatchMenu({BEAM_VIEW, -1}, record, &menu);
    ctx.check("key and menu give one Action value", key.got.size() == 1 && menu.got.size() == 1 && key.got[0].act == menu.got[0].act && key.got[0].arg == menu.got[0].arg);

    ctx.check("teleports, reset world and refuel are refused in a multiplayer session; the old keys' actions are not",
              offlineOnly(TELEPORT_SPAWN) && offlineOnly(TELEPORT_SECTION) && offlineOnly(TELEPORT_PLACE) && offlineOnly(RESET_WORLD) && offlineOnly(REFUEL) &&
                  !offlineOnly(SWAP_VEHICLE) && !offlineOnly(SPAWN_PARKED) && !offlineOnly(SPAWN_TRAFFIC) && !offlineOnly(REMOVE_VEHICLE) && !offlineOnly(REPAIR));
    ctx.check("the groups have names", std::string(groupName(G_SPAWN_VEHICLE)) == "SPAWN VEHICLE" && std::string(groupName(G_TOOLS)) == "TOOLS");
    ctx.check("old keys are named beside their entries", hotkeyName({BEAM_VIEW, -1}) == "KEY N" && hotkeyName({SWAP_VEHICLE, 0}) == "KEY 1");
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
