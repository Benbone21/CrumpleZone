// Remappable controls, keyboard and gamepad, with no SDL or GL in them (the test runner links this file).
//
// Every action in the game is a KeyAction: driving, recovery, on foot, camera, the game's own keys (menu,
// pause, help, chat...), the menus' own keys, the free cam and the dev tools. The table in keybinds.cpp (kInfo)
// is the one place with each action's label, settings name, group, where it applies and its DEFAULT key (and
// whether that default is a Shift combo, like Shift+L for the lights) and DEFAULT pad binding.
//
// How it fits the input code in main.cpp: every in-game key press is looked at as a *default* key (the
// W/E/Q/V/O/Esc... the switch in main.cpp has always been written against). The player's bindings sit in front
// of that switch: `resolve()` turns the physical key (and Shift) into the action it is bound to and gives back
// that action's default key (and whether it is a Shift combo), so the switch never needs to know about
// remapping. The gamepad goes through the same table: every action also has a PAD binding (a button, a trigger
// or a stick direction, optionally held with LB and / or VIEW: "LB + A"), and pad::Logic (gamepad.h) turns a pad
// press into the action it is bound to and feeds in that action's default key, as a keyboard press would be.
// Start is always the pause menu (and, in the menus, back), like Esc.
//
// Menus: the MENUS group (KCTX_MENU, KCTX_DIALOG) is the menus' own keys: up, down, left, right, OK, back, the
// tabs and a few extras. `uiKey()` turns a physical key into the menu key the menus are written against (the
// arrows, Enter, Esc and the UK_* codes below); a pad press in a menu arrives as one directly. Esc, Enter and the
// arrows (and the pad's A, B and Start) always work in the menus whatever is bound, so they can never be lost.
//
// Contexts: the same key may be bound in different contexts (E is the gear shift in the truck and "interact"
// on foot; G the garage or put down what you carry) without it being a clash. A key press is looked up in
// the context the game is in (driving or on foot, plus "a scenario is playing"; a menu; the free cam). Dev
// actions (`gated`) are a side of their own: they only fire in DEV KEYS mode, may share a key with a gameplay
// action (the mode decides which one answers) and clash only with each other.
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
    KA_HORN,  // hold
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
    KA_AUTOPILOT,       // starts and stops it, to the place last picked
    KA_AUTOPILOT_PICK,  // opens the autopilot page: where to, how fast, which roads (the pause menu no longer has it)
    KA_JOBS,            // opens the jobs on offer here (the pause menu no longer has them)
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
    KA_DEV_FREECAM,  // F8: the dev free cam (freecam.h)
    KA_DEV_SCENARIO,
    // MENUS (every menu, the garage, the map, the inventory, the update card: KCTX_MENU)
    KA_UI_UP,
    KA_UI_DOWN,
    KA_UI_LEFT,
    KA_UI_RIGHT,
    KA_UI_CONFIRM,   // (Enter and the pad's A always do this too)
    KA_UI_BACK,      // (Esc and the pad's B and Start always do this too)
    KA_UI_TAB_PREV,  // settings tabs, garage categories
    KA_UI_TAB_NEXT,
    KA_UI_SECOND,    // the second action: try a part on (garage), delete (career worlds), clear a binding (controls)
    KA_UI_FACTORY,   // the factory part back (garage), a binding back to its defaults (controls)
    KA_UI_ZOOM_IN,   // the garage's model, the map
    KA_UI_ZOOM_OUT,
    KA_UI_OPTION,    // the garage's speedo recalibration
    KA_UI_CHOICE1,   // a dialog's first choice (the career repair: on the spot; KCTX_DIALOG)
    KA_UI_CHOICE2,   // ...and its second (respawn at a station)
    // FREE CAM (KCTX_FREECAM: while the dev free cam flies; it flies with the driving keys and the gears up and down)
    KA_FC_GRAB,
    KA_FC_LET_GO,
    KA_FC_TURN,
    KA_FC_TURN_BACK,
    KA_FC_SPAWN,
    KA_FC_DRIVE,
    KA_FC_THROUGH,
    KA_FC_SLOWER,
    KA_FC_FASTER,
    KA_COUNT
};

enum KeyGroup { KG_DRIVING, KG_RECOVERY, KG_FOOT, KG_VIEW, KG_GAME, KG_MENU, KG_FREECAM, KG_DEV, KG_COUNT };
const char* groupLabel(KeyGroup g);  // "DRIVING", "RECOVERY", "ON FOOT", "CAMERA AND VIEW", "GAME", "MENUS", "FREE CAM", "DEV"

// Where an action applies: a press is looked up in the contexts the game is in right now.
enum KeyContext : unsigned {
    KCTX_DRIVE = 1,
    KCTX_FOOT = 2,
    KCTX_SCENARIO = 4,
    KCTX_MENU = 8,      // any menu
    KCTX_DIALOG = 16,   // a dialog's choices (looked up before the menu keys)
    KCTX_FREECAM = 32,  // the dev free cam
    KCTX_ANY = KCTX_DRIVE | KCTX_FOOT,
    KCTX_UI = KCTX_MENU | KCTX_DIALOG,
};

// The keys the menus are written against: a menu action's press arrives as one of these (uiKey()). The four
// directions, OK and back are the arrows, Enter and Esc (which always work); the rest are codes no key has.
enum UiKey : int { UK_TAB_PREV = 0x301, UK_TAB_NEXT, UK_SECOND, UK_FACTORY, UK_ZOOM_IN, UK_ZOOM_OUT, UK_OPTION, UK_CHOICE1, UK_CHOICE2 };
int uiCanon(KeyAction a);  // the menu key a menu action arrives as (0: not a menu action)

// ---- The pad's inputs ----------------------------------------------------------------------------
// A pad binding is an input plus the modifiers held with it (PM_LB, PM_VIEW). The buttons are SDL's
// GameController buttons + 1 (pad::Button + 1); then the triggers; then each stick's four directions.
enum PadInput : int {
    PI_NONE,
    PI_A, PI_B, PI_X, PI_Y, PI_BACK, PI_GUIDE, PI_START, PI_L3, PI_R3, PI_LB, PI_RB, PI_DUP, PI_DDOWN, PI_DLEFT, PI_DRIGHT,
    PI_LT, PI_RT,
    PI_LS_LEFT, PI_LS_RIGHT, PI_LS_UP, PI_LS_DOWN,
    PI_RS_LEFT, PI_RS_RIGHT, PI_RS_UP, PI_RS_DOWN,
    PI_COUNT
};
constexpr int PM_LB = 0x100, PM_VIEW = 0x200, PM_MASK = PM_LB | PM_VIEW;  // LB held, VIEW (BACK / SELECT / TOUCHPAD) held
inline int padInput(int bind) { return bind & 0xff; }
inline int padMods(int bind) { return bind & PM_MASK; }
std::string padInputName(int input, bool playStation = false);  // "A", "LB", "RT", "LEFT STICK LEFT"... ("CROSS", "L1", "R2" on a PlayStation pad)
std::string padBindName(int bind, bool playStation = false);    // "LB + A", "VIEW + X", "LB + VIEW + A", "NONE"
int defaultPad(KeyAction a);                                     // the pad binding out of the box
// Can `a` have this pad binding? Never Start or the guide button (Start is the menu, like Esc). In the game (and the free
// cam) LB and VIEW are the modifiers: LB is no button of its own, VIEW is one only on its own (it answers when let go
// with no button pressed while it was held). In the menus there are no modifiers, and A and B are always OK and back.
bool padBindable(KeyAction a, int bind);

int defaultKey(KeyAction a);           // the scancode an action is bound to out of the box
bool defaultShift(KeyAction a);        // ... and whether that is a Shift combo (Shift+L: the lights)
const char* actionLabel(KeyAction a);  // "THROTTLE"... (the keybinds page)
const char* settingsName(KeyAction a); // "key_throttle"... (settings.txt; the pad binding's line is "pad_throttle")
std::string padSettingsName(KeyAction a);
KeyGroup actionGroup(KeyAction a);
unsigned actionContexts(KeyAction a);  // KeyContext bits
bool isHeldAction(KeyAction a);        // read from the keyboard state each frame (pedals, steering,
                                       // handbrake, winch, air) rather than from a key press
bool isDevAction(KeyAction a);         // gated by DEV KEYS mode: the dev tools (not the dev menu key, the toggle or the playtest key)
bool isUiAction(KeyAction a);          // a menu (or dialog) key
bool bindable(int scancode);           // false: Esc, keypad Enter, arrows, modifiers, anything unknown
bool bindableFor(KeyAction a, int scancode);  // ...and not Enter for a menu key (Enter is always OK in the menus)
std::string keyName(int scancode);     // "W", "SPACE", "F4"... for the menu

struct Keybinds {
    int code[KA_COUNT];        // 0: unbound
    bool shift[KA_COUNT];      // bound to Shift+key
    int pad[KA_COUNT];         // the pad binding (PadInput | PM_* modifiers; 0: unbound)
    bool legacyPauseOnP = false;  // an old file's key_pause line had P (pause is the menu now: Esc)
    bool loaded[KA_COUNT];     // came from the settings file (an action added since, whose key a saved binding took, moves aside)
    bool padLoaded[KA_COUNT];  // ...and the pad binding

    Keybinds() { resetDefaults(); }
    void resetDefaults();           // every key and every pad binding
    void resetAction(KeyAction a);  // this action's key and pad binding back to its defaults (whatever had them swaps onto its old ones)
    int key(KeyAction a) const { return code[a]; }
    bool shiftOf(KeyAction a) const { return shift[a]; }
    std::string name(KeyAction a) const;  // "L", "SHIFT+L", "NONE": what the hints and the keybinds page show
    std::string padName(KeyAction a, bool playStation = false) const;  // "LB + A", "NONE"

    // Bind `a` to `scancode` (with Shift). Refused (false) for keys that can't be bound. An action that
    // holds that key in a context `a` shares is swapped over to this action's old key (any more are
    // unbound), so no key ever has two jobs in one context.
    bool bind(KeyAction a, int scancode, bool withShift = false);
    void unbind(KeyAction a) { code[a] = 0, shift[a] = false; }
    // The pad's the same: refused when !padBindable; a clash swaps onto `a`'s old binding.
    bool bindPad(KeyAction a, int bind);
    void unbindPad(KeyAction a) { pad[a] = 0; }
    // The action that holds this key (or pad binding) in a context `a` shares, which bind would swap; -1: none.
    int clashWith(KeyAction a, int scancode, bool withShift) const;
    int padClashWith(KeyAction a, int bind) const;
    // The action a physical key is bound to in these contexts (dev actions only in DEV KEYS mode), or -1.
    int actionFor(int scancode, bool withShift = false, unsigned contexts = KCTX_ANY, bool devKeys = true) const;
    // Is this physical press `a`'s binding? Exact Shift, or a plain binding with Shift incidentally down.
    bool matches(KeyAction a, int scancode, bool withShift) const;
    // Is a held action down, given SDL's keyboard state (a plain binding still counts with Shift down, unless
    // another action has that key as a Shift combo: Z winches in, Shift+Z pays out).
    bool held(KeyAction a, const uint8_t* keyState, bool shiftHeld) const;

    // The pad: the action a press of `input` with these modifiers held is, in these contexts (an exact chord first;
    // then the plain binding of that input, as a plain key answers with Shift down), or -1.
    int padActionFor(int input, int mods, unsigned contexts, bool devKeys = true) const;
    // Is there a chord of this input with exactly these modifiers among the actions in `contexts`?
    bool padChord(int input, int mods, unsigned contexts) const;
    // A held action's value 0..1 from the pad: `values` per PadInput (buttons 0 / 1, the triggers and stick
    // directions shaped), `mods` what is held. A chord counts with exactly its modifiers held; a plain binding
    // with none held, or with some held when no chord of its input has them (LB + RT winches, RT alone drives).
    float padHeld(KeyAction a, const float* values, int mods) const;

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
    // A physical key in a menu: the menu key it is (uiCanon of the menu action it's bound to; the arrows, Enter,
    // keypad Enter and Esc as themselves; any other key as itself, for the menus' own fixed keys: Tab, Delete, the
    // text fields'...). With KCTX_DIALOG in `contexts` a dialog's choices are looked at first.
    int uiKey(int scancode, bool withShift = false, unsigned contexts = KCTX_MENU) const;

    // settings.txt: a "key_*" or "pad_*" line (value: the scancode, plus 4096 for Shift; the pad binding). True if `name` was one of ours.
    bool loadSetting(const std::string& name, int value);
    int saveValue(KeyAction a) const { return code[a] | (shift[a] ? 4096 : 0); }
    int padSaveValue(KeyAction a) const { return pad[a]; }
    // After loading: unusable or clashing bindings fall back to the action's default (an old file, from before
    // the pad bindings or the menu keys, gets their defaults).
    void sanitize();
    bool isDefault() const;
    bool isDefault(KeyAction a) const;     // key and pad binding
    bool isKeyDefault(KeyAction a) const;
    bool isPadDefault(KeyAction a) const;
    // The default-table text for build/keybinds-defaults.txt: one `{key, shift}` line per action, in order.
    std::string exportDefaults() const;
};

// The default bindings (for code that isn't handed the player's: a menu without the settings, the tests).
const Keybinds& defaultKeybinds();
