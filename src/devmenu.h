// The dev menu: the debug and test tools that used to be loose keys (1-7 spawning, I, K, N, F2...),
// with no SDL or GL in it (the test runner links this file).
//
// Two ways in to the same actions. The DEV MENU (F12, or the pause menu's DEV MENU row) lists them
// in groups; the old hotkeys work while the key mode is DEV KEYS (keybinds.h: the dev key actions, flipped by KA_DEV_TOGGLE). Both end in one
// `Action`, and main.cpp has one place that does it (so a menu entry and its old key can't drift
// apart). The player keys (driving, camera, winch, map, pause, help...) are not in here.
#pragma once
#include <string>
#include <vector>

#include "keybinds.h"

namespace devmenu {

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
    FPS,              // the debug screen (key F3; Shift: its frame graph)
    EDITOR,           // the level editor, here (key F2)
    MODEL_LAB,        // the model yard
    NEW_SECTION,      // a fresh obstacle section in the editor
    RELOAD,           // load the new build (key F5)
    SCENARIO_CHECKS,  // a scenario playback's checks list (key J)
    EXPORT_KEYBINDS,  // the keybinds as the new defaults, to build/keybinds-defaults.txt
    FREE_CAM,         // the dev free cam (freecam.h): fly anywhere, carry and spawn vehicles (key F8)
};

struct Action {
    Act act = NONE;
    int arg = 0;
    explicit operator bool() const { return act != NONE; }
    bool operator==(const Action& o) const { return act == o.act && arg == o.arg; }
};

// The dev tool a dev key action (keybinds.h: KA_DEV_*) stands for; NONE for any other action.
Action forKeyAction(KeyAction a);
// The dev key action that runs this tool, or -1 (the tools with no key).
int keyActionOf(const Action& a);
// Changes the world other players share or hands out things a guest couldn't have before: refused
// in a multiplayer session.
bool offlineOnly(Act a);

struct Item {
    Action action;
    std::string label, hint;  // hint: the old key ("KEY N"), or a note
};
// The rows of a group (the teleport page adds its own SECTION and PLACE pickers); with the keybinds, the
// hints name each tool's key as it is bound now.
std::vector<Item> items(Group g, const Keybinds* kb = nullptr);

// A recorder for the tests: who gets called with what, so menu entries and keys can be compared.
using Handler = void (*)(void* user, const Action& a);
// Run what a key press does (nothing for a dev key with the DEV KEYS mode off, or a key that isn't a dev tool): true when it was
// a dev tool that ran. `contexts`: keybinds.h KeyContext bits.
bool dispatchKey(const Keybinds& kb, int scancode, bool shift, bool devKeys, unsigned contexts, Handler h, void* user);
// Run a menu entry (always: the menu is the way in with the hotkeys off).
bool dispatchMenu(const Action& a, Handler h, void* user);

}  // namespace devmenu
