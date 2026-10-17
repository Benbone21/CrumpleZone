// The dev menu: the debug and test tools that used to be loose keys (1-7 spawning, I, K, N, F2...),
// with no SDL or GL in it (the test runner links this file).
//
// Two ways in to the same actions. The DEV MENU (F12, or the pause menu's DEV MENU row) lists them
// in groups; the old hotkeys work while the key mode is DEV KEYS (the ` key flips it, DEV HOTKEYS in the menus). Both end in one
// `Action`, and main.cpp has one place that does it (so a menu entry and its old key can't drift
// apart). The player keys (driving, camera, winch, map, pause, help...) are not in here.
#pragma once
#include <string>
#include <vector>

namespace devmenu {

constexpr int kMenuKey = 69;  // F12 (SC_F12): opens and closes the dev menu, whatever the key mode is
constexpr int kModeKey = 53;  // ` (grave): flips between DEV KEYS and GAMEPLAY KEYS (Settings::devHotkeys), in the game

enum Group { G_SPAWN_VEHICLE, G_SPAWN_AHEAD, G_TELEPORT, G_WORLD, G_VEHICLE, G_VIEW, G_TOOLS, G_COUNT };
const char* groupName(Group g);
const char* groupHint(Group g);

enum Act {
    NONE,
    SWAP_VEHICLE,     // arg: index into listedVehicles(): the vehicle you're driving becomes that one (keys 1-7)
    SPAWN_PARKED,     // arg: index into listedVehicles(), or -1 the next one on the list (key I): parked ahead
    SPAWN_TRAFFIC,    // the next listed vehicle, AI-driven on the loop road (key K)
    REMOVE_VEHICLE,   // the one you're driving (DEL)
    TELEPORT_SPAWN,   // back to the map's spawn point (upright, damage stays)
    TELEPORT_SECTION, // arg: index into the map's obstacle sections
    TELEPORT_PLACE,   // arg: index into the places list (the autopilot's destinations)
    TIME_OF_DAY,      // arg: TimeOfDay (daylight.h): 0 the clock runs, else held at that time
    TIME_SLOWER,      // slow motion a step down (key [)
    TIME_FASTER,      // and back up (key ])
    RESET_WORLD,      // reload the map: props, damage and all back to the start
    REPAIR,           // rebuilt like new where it stands (what R does)
    RESPAWN,          // back at the start, like new (R twice)
    FLIP_UPRIGHT,     // back on its wheels, damage stays (T)
    REFUEL,           // full tank and spare can
    BEAM_VIEW,        // arg: DebugView, or -1 the next one (key N)
    FPS,              // the frame counter (key F3)
    EDITOR,           // the level editor, here (key F2)
    MODEL_LAB,        // the model yard
    NEW_SECTION,      // a fresh obstacle section in the editor
    RELOAD,           // load the new build (key F5)
    SCENARIO_CHECKS,  // a scenario playback's checks list (key J)
};

struct Action {
    Act act = NONE;
    int arg = 0;
    explicit operator bool() const { return act != NONE; }
    bool operator==(const Action& o) const { return act == o.act && arg == o.arg; }
};

// What a dev key does in GAMEPLAY KEYS mode ("": nothing, or not a dev key): the other side of the toggle.
const char* gameplayJob(int scancode);

// What the keyboard is doing right now, for the keys that mean something else outside the dev tools.
struct KeyCtx {
    bool onFoot = false;    // I is the truck inventory, not a spawn
    bool scenario = false;  // a scenario is playing: J is its checks list, not get in/out
};

// The dev tool a (default) key stands for, whatever the setting is (NONE: not a dev key).
Action hotkeyAction(int scancode, const KeyCtx& ctx = KeyCtx());
// What the key switch does with a key: the action when DEV HOTKEYS is ON, nothing when it's OFF.
Action fromKey(int scancode, bool devHotkeys, const KeyCtx& ctx = KeyCtx());
// The key that does an action, as the menu shows it ("" when it has none).
std::string hotkeyName(const Action& a);
// Changes the world other players share or hands out things a guest couldn't have before: refused
// in a multiplayer session.
bool offlineOnly(Act a);

struct Item {
    Action action;
    std::string label, hint;  // hint: the old key ("KEY N"), or a note
};
// The rows of a group (the teleport page adds its own SECTION and PLACE pickers).
std::vector<Item> items(Group g);

// A recorder for the tests: who gets called with what, so menu entries and keys can be compared.
using Handler = void (*)(void* user, const Action& a);
// Run what a key does (nothing with DEV HOTKEYS off): true when it was a dev key and did something.
bool dispatchKey(int scancode, bool devHotkeys, const KeyCtx& ctx, Handler h, void* user);
// Run a menu entry (always: the menu is the way in with the hotkeys off).
bool dispatchMenu(const Action& a, Handler h, void* user);

}  // namespace devmenu
