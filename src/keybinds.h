// Remappable keyboard controls, with no SDL or GL in them (the test runner links this file).
//
// Every keyboard action in the game is a KeyAction: driving, recovery, on foot, camera, the game's own keys
// (menu, pause, help, chat...) and the dev tools. The table in keybinds.cpp (kInfo) is the one place with
// each action's label, settings name, group, where it applies and its DEFAULT key (and whether that default
// is a Shift combo, like Shift+L for the lights).
//
// How it fits the input code in main.cpp: every in-game key press is looked at as a *default*
// key (the W/E/Q/V/O/Esc... the switch in main.cpp has always been written against). The
// player's bindings sit in front of that switch: `resolve()` turns the physical key (and Shift) into the
// action it is bound to and gives back that action's default key (and whether it is a Shift combo), so the
// switch never needs to know about remapping. Gamepad button presses come in as the default keys directly (see
// gamepad.h), so they are never remapped: Start is the pause menu whatever the keyboard menu key is.
//
// Contexts: the same key may be bound in different contexts (E is the gear shift in the truck and "interact"
// on foot; G the garage or put down what you carry) without it being a clash. A key press is looked up in
// the context the game is in (driving or on foot, plus "a scenario is playing"). Dev actions (`gated`) are
// a side of their own: they only fire in DEV KEYS mode, may share a key with a gameplay action (the mode
// decides which one answers) and clash only with each other.
//
// Esc is the menu key. The MENU action can be given a second key, but Esc itself can't be taken
// by another action: whatever the player binds, Esc still opens (and closes) the pause menu, so
// the menu can't be lost.
//
// Turning a saved layout into the new defaults: the dev menu's EXPORT KEYBINDS AS DEFAULTS writes
// build/keybinds-defaults.txt, the first two fields (key, shift) of every kInfo row in order, to paste over
// the table's.
#pragma once
#include <cstdint>
#include <string>

enum KeyAction {
    // DRIVING
    KA_THROTTLE,
    KA_BRAKE,
    KA_STEER_LEFT,
    KA_STEER_RIGHT,
    KA_HANDBRAKE,
    KA_PARK_BRAKE,
    KA_GEAR_UP,
    KA_GEAR_DOWN,
    KA_MANUAL,
    KA_TRANSFER,
    KA_LIGHTS,
    KA_LOCK_FRONT,
    KA_LOCK_REAR,
    KA_SWAY,
    KA_TRACTION,
    KA_STEER_ASSIST,
    KA_AIR_DOWN,
    KA_AIR_UP,
    KA_THROTTLE_CAP,
    KA_GARAGE,
    KA_NEXT_VEHICLE,
    // RECOVERY
    KA_WINCH,
    KA_WINCH_IN,
    KA_WINCH_OUT,
    KA_WINCH_SLOWER,
    KA_WINCH_FASTER,
    KA_STRAP,
    KA_REPAIR,
    KA_FLIP,
    KA_AUTOPILOT,
    // ON FOOT
    KA_GET_IN_OUT,
    KA_INTERACT,
    KA_INVENTORY,
    KA_PUT_DOWN,
    KA_FUEL_CAN,
    KA_CROUCH,  // hold (or toggle, a setting): lower, slower, look under things
    // CAMERA AND VIEW
    KA_CAMERA,
    KA_MAP,
    KA_PHOTO,
    KA_HUD_ALL,  // hold: the whole HUD, whatever the contextual HUD is hiding
    KA_DEBUG_INFO,  // F3: the debug info screen (was the FPS counter)
    // GAME
    KA_MENU,
    KA_PAUSE,
    KA_HELP,
    KA_FULLSCREEN,
    KA_CHAT,
    // DEV
    KA_DEV_MENU,
    KA_DEV_TOGGLE,
    KA_PLAYTEST_BACK,
    KA_DEV_RELOAD,  // F5: load the new build (always on, like the menu key)
    KA_DEV_VEHICLE1,
    KA_DEV_VEHICLE2,
    KA_DEV_VEHICLE3,
    KA_DEV_VEHICLE4,
    KA_DEV_VEHICLE5,
    KA_DEV_VEHICLE6,
    KA_DEV_SPAWN,
    KA_DEV_TRAFFIC,
    KA_DEV_REMOVE,
    KA_DEV_BEAM,
    KA_DEV_SLOWER,
    KA_DEV_FASTER,
    KA_DEV_EDITOR,
    KA_DEV_SCENARIO,
    KA_COUNT
};

enum KeyGroup { KG_DRIVING, KG_RECOVERY, KG_FOOT, KG_VIEW, KG_GAME, KG_DEV, KG_COUNT };
const char* groupLabel(KeyGroup g);  // "DRIVING", "RECOVERY", "ON FOOT", "CAMERA AND VIEW", "GAME", "DEV"

// Where an action applies: a press is looked up in the contexts the game is in right now.
enum KeyContext : unsigned { KCTX_DRIVE = 1, KCTX_FOOT = 2, KCTX_SCENARIO = 4, KCTX_ANY = KCTX_DRIVE | KCTX_FOOT };

int defaultKey(KeyAction a);           // the scancode an action is bound to out of the box
bool defaultShift(KeyAction a);        // ... and whether that is a Shift combo (Shift+L: the lights)
const char* actionLabel(KeyAction a);  // "THROTTLE"... (the keybinds page)
const char* settingsName(KeyAction a); // "key_throttle"... (settings.txt)
KeyGroup actionGroup(KeyAction a);
unsigned actionContexts(KeyAction a);  // KeyContext bits
bool isHeldAction(KeyAction a);        // read from the keyboard state each frame (pedals, steering,
                                       // handbrake, winch, air) rather than from a key press
bool isDevAction(KeyAction a);         // gated by DEV KEYS mode: the dev tools (not the dev menu key, the toggle or the playtest key)
bool bindable(int scancode);           // false: Esc, keypad Enter, arrows, modifiers, anything unknown
std::string keyName(int scancode);     // "W", "SPACE", "F4"... for the menu

struct Keybinds {
    int code[KA_COUNT];        // 0: unbound
    bool shift[KA_COUNT];      // bound to Shift+key
    bool loaded[KA_COUNT];     // came from the settings file (an action added since, whose key a saved binding took, moves aside)

    Keybinds() { resetDefaults(); }
    void resetDefaults();
    int key(KeyAction a) const { return code[a]; }
    bool shiftOf(KeyAction a) const { return shift[a]; }
    std::string name(KeyAction a) const;  // "L", "SHIFT+L", "NONE": what the hints and the keybinds page show

    // Bind `a` to `scancode` (with Shift). Refused (false) for keys that can't be bound. An action that
    // holds that key in a context `a` shares is swapped over to this action's old key (any more are
    // unbound), so no key ever has two jobs in one context.
    bool bind(KeyAction a, int scancode, bool withShift = false);
    void unbind(KeyAction a) { code[a] = 0, shift[a] = false; }
    // The action a physical key is bound to in these contexts (dev actions only in DEV KEYS mode), or -1.
    int actionFor(int scancode, bool withShift = false, unsigned contexts = KCTX_ANY, bool devKeys = true) const;
    // Is this physical press `a`'s binding? Exact Shift, or a plain binding with Shift incidentally down.
    bool matches(KeyAction a, int scancode, bool withShift) const;
    // Is a held action down, given SDL's keyboard state (a plain binding still counts with Shift down, unless
    // another action has that key as a Shift combo: Z winches in, Shift+Z pays out).
    bool held(KeyAction a, const uint8_t* keyState, bool shiftHeld) const;

    // A physical key press, resolved for the game's key switch: the action, its default key (0: a held action,
    // the press means nothing here) and whether that is a Shift combo. `fromPad`: a gamepad button standing in
    // for a key, which is already a default key. Esc is always the menu. Keys bound to nothing are 0, except
    // Enter, keypad Enter and Backspace (the winch picker's own keys).
    struct Press {
        int action = -1;
        int key = 0;
        bool shift = false;
    };
    Press resolve(int scancode, bool withShift, bool fromPad, unsigned contexts, bool devKeys) const;
    // (the old name: driving, no Shift, DEV KEYS off; for the tests)
    int gameKey(int scancode, bool fromPad) const { return resolve(scancode, false, fromPad, KCTX_DRIVE, false).key; }

    // settings.txt: a "key_*" line (value: the scancode, plus 4096 for Shift). True if `name` was one of ours.
    bool loadSetting(const std::string& name, int value);
    int saveValue(KeyAction a) const { return code[a] | (shift[a] ? 4096 : 0); }
    // After loading: unusable or clashing bindings fall back to the action's default.
    void sanitize();
    bool isDefault() const;
    bool isDefault(KeyAction a) const;
    // The default-table text for build/keybinds-defaults.txt: one `{key, shift}` line per action, in order.
    std::string exportDefaults() const;
};
