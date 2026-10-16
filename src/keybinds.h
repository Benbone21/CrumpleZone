// Remappable keyboard controls, with no SDL or GL in them (the test runner links this file).
//
// How it fits the input code in main.cpp: every in-game key press is looked at as a *default*
// key (the W/E/Q/V/O/Esc... the switch in main.cpp has always been written against). The
// player's bindings sit in front of that switch: `gameKey()` turns the physical key into the
// default key of the action it is bound to, so the switch never needs to know about
// remapping. Gamepad button presses come in as the default keys directly (see gamepad.h), so
// they are never remapped: Start is the pause menu whatever the keyboard menu key is.
//
// Esc is the menu key. The MENU action can be given a second key, but Esc itself can't be taken
// by another action: whatever the player binds, Esc still opens (and closes) the pause menu, so
// the menu can't be lost.
#pragma once
#include <string>

enum KeyAction {
    KA_THROTTLE,
    KA_BRAKE,
    KA_STEER_LEFT,
    KA_STEER_RIGHT,
    KA_HANDBRAKE,
    KA_GEAR_UP,
    KA_GEAR_DOWN,
    KA_MENU,
    KA_STEER_ASSIST,
    KA_WINCH,
    KA_STRAP,
    KA_COUNT
};

int defaultKey(KeyAction a);           // the scancode an action is bound to out of the box
const char* actionLabel(KeyAction a);  // "THROTTLE"... (the keybinds page)
const char* settingsName(KeyAction a); // "key_throttle"... (settings.txt)
bool isHeldAction(KeyAction a);        // read from the keyboard state each frame (pedals, steering,
                                       // handbrake) rather than from a key press
bool bindable(int scancode);           // false: Esc, Enter, Tab, arrows, modifiers, anything unknown
std::string keyName(int scancode);     // "W", "SPACE", "F4"... for the menu

struct Keybinds {
    int code[KA_COUNT];

    Keybinds() { resetDefaults(); }
    void resetDefaults();
    int key(KeyAction a) const { return code[a]; }

    // Bind `a` to `scancode`. Refused (false) for keys that can't be bound. A key another
    // action holds is swapped over to this action's old key, so no key ever has two jobs.
    bool bind(KeyAction a, int scancode);
    // The action a physical key is bound to, or -1.
    int actionFor(int scancode) const;
    // A physical key press as the game's key switch should see it (`fromPad`: a gamepad button
    // standing in for a key, which is already a default key):
    //  - bound to an action with a key press: that action's default key;
    //  - bound to a held action (throttle...): 0, the press means nothing here;
    //  - the default key of an action that was moved elsewhere: 0 (it is free, not doing both);
    //  - Esc: always Esc (the menu), whatever is bound.
    int gameKey(int scancode, bool fromPad) const;
    // settings.txt: a "key_*" line. True if `name` was one of ours.
    bool loadSetting(const std::string& name, int value);
    // After loading: unusable or duplicated codes fall back to the defaults.
    void sanitize();
    bool isDefault() const;
};
