// Keyboard bindings and the Esc / pause-menu key path, with no SDL or GL: synthetic SDL key
// events go through the same lookup main.cpp uses (Keybinds::gameKey), and the gamepad's Start
// button through the real pad::Logic. Guards the regression where Esc stopped opening the menu
// after the keybinds system was added (see docs/input-fix.md).
#include <cstring>

#include "../src/gamepad.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

// A key event the way SDL (and the game's own pad taps) lay it out.
SDL_Event keyEvent(uint32_t type, int scancode, bool repeat = false) {
    SDL_Event e;
    std::memset(&e, 0, sizeof e);
    e.type = type;
    e.padding[12] = type == SDL_KEYDOWN ? 1 : 0;
    e.padding[13] = repeat ? 1 : 0;
    std::memcpy(e.padding + 16, &scancode, 4);
    return e;
}

}  // namespace

SCENARIO(keysEsc, "keybinds/esc-opens-menu", "Esc reaches the key switch as Esc (the pause menu) with the default keys, rebound keys, and from the pad's Start") {
    const Keybinds kb;
    const SDL_Event down = keyEvent(SDL_KEYDOWN, SC_ESCAPE), up = keyEvent(SDL_KEYUP, SC_ESCAPE);
    ctx.check("the synthetic event reads back as Esc (scancode 41)", evKeyScancode(down) == SC_ESCAPE && evKeyScancode(down) == 41 && !evKeyRepeat(down));
    ctx.check("default MENU key is Esc", kb.key(KA_MENU) == SC_ESCAPE && defaultKey(KA_MENU) == SC_ESCAPE);
    ctx.check("Esc down is Esc for the switch", kb.gameKey(evKeyScancode(down), false) == SC_ESCAPE);
    ctx.check("Esc up is Esc too (the switch only acts on down events)", kb.gameKey(evKeyScancode(up), false) == SC_ESCAPE);
    ctx.check("the MENU action is bound to Esc", kb.actionFor(SC_ESCAPE) == KA_MENU);

    Keybinds rebound;
    ctx.check("MENU can be given another key", rebound.bind(KA_MENU, 59 /* F2 */));
    ctx.check("the new menu key is the menu", rebound.gameKey(59, false) == SC_ESCAPE);
    ctx.check("Esc still opens the menu after the rebind", rebound.gameKey(SC_ESCAPE, false) == SC_ESCAPE);
    ctx.check("no other action can take Esc", !rebound.bind(KA_THROTTLE, SC_ESCAPE) && !rebound.bind(KA_WINCH, SC_ESCAPE) && rebound.key(KA_THROTTLE) == SC_W);
    ctx.check("Esc can't be bound over by swapping either", !rebound.bind(KA_BRAKE, SC_ESCAPE));
    ctx.check("MENU back to Esc alone", rebound.bind(KA_MENU, SC_ESCAPE) && rebound.isDefault());

    // The pad: Start in the car is Esc, and the keyboard's bindings never touch a pad press.
    pad::Logic logic;
    pad::Raw raw;
    raw.buttons = pad::bit(pad::B_START);
    logic.update(raw, pad::Mode::Drive, Ctx::kFrame);
    bool tapped = false;
    for (const pad::Tap& t : logic.taps) tapped |= t.scancode == SC_ESCAPE && !t.shift;
    ctx.check("pad Start while driving taps Esc", tapped);
    ctx.check("the pad's Esc is Esc whatever the keyboard has bound", rebound.bind(KA_MENU, 59) && rebound.gameKey(SC_ESCAPE, true) == SC_ESCAPE);
    ctx.check("pad taps are never remapped (winch moved off V, the pad's V is still the winch)",
              rebound.bind(KA_WINCH, SC_K) && rebound.gameKey(SC_V, true) == SC_V && rebound.gameKey(SC_K, false) == SC_V);
}

SCENARIO(keysRemap, "keybinds/remap", "Rebound actions reach the key switch as their default keys; a freed default key is quiet; keys never have two jobs") {
    Keybinds kb;
    ctx.check("defaults are all distinct, bindable (but Esc) and usable as keyboard-state indices", [&] {
        for (int a = 0; a < KA_COUNT; a++) {
            if (kb.key((KeyAction)a) < 4 || kb.key((KeyAction)a) >= 512) return false;
            if (a != KA_MENU && !bindable(kb.key((KeyAction)a))) return false;
            for (int b = a + 1; b < KA_COUNT; b++)
                if (kb.key((KeyAction)a) == kb.key((KeyAction)b)) return false;
        }
        return true;
    }());
    ctx.check("default keys are the ones the switch uses: E Q V O Esc and F4 for the steering assist",
              kb.gameKey(SC_E, false) == SC_E && kb.gameKey(SC_Q, false) == SC_Q && kb.gameKey(SC_V, false) == SC_V &&
                  kb.gameKey(SC_O, false) == SC_O && kb.gameKey(SC_F4, false) == SC_F4);
    ctx.check("the steering assist default isn't a key the game already uses (P pauses)", defaultKey(KA_STEER_ASSIST) != SC_P);

    ctx.check("gear up to X is accepted", kb.bind(KA_GEAR_UP, SC_X));
    ctx.check("X is now the gear-up key E (the switch's)", kb.gameKey(SC_X, false) == SC_E);
    ctx.check("E, freed, does nothing", kb.gameKey(SC_E, false) == 0);
    ctx.check("but the pad's E (its Y button) still shifts up", kb.gameKey(SC_E, true) == SC_E);

    ctx.check("binding gear down to X swaps: gear up takes the key gear down had", kb.bind(KA_GEAR_DOWN, SC_X) && kb.key(KA_GEAR_UP) == SC_Q && kb.key(KA_GEAR_DOWN) == SC_X);
    ctx.check("still no key with two jobs", [&] {
        for (int a = 0; a < KA_COUNT; a++)
            for (int b = a + 1; b < KA_COUNT; b++)
                if (kb.key((KeyAction)a) == kb.key((KeyAction)b)) return false;
        return true;
    }());

    ctx.check("held actions: the key in the event stream means nothing", kb.bind(KA_THROTTLE, SC_I) && kb.gameKey(SC_I, false) == 0 && kb.key(KA_THROTTLE) == SC_I);
    ctx.check("Enter, Tab, arrows and shift can't be bound", !kb.bind(KA_BRAKE, SC_RETURN) && !kb.bind(KA_BRAKE, SC_TAB) && !kb.bind(KA_BRAKE, SC_UP) &&
                                                               !kb.bind(KA_BRAKE, SC_LSHIFT) && !kb.bind(KA_BRAKE, 0) && !kb.bind(KA_BRAKE, 600));
    kb.resetDefaults();
    ctx.check("reset gives the defaults back", kb.isDefault());
    ctx.check("key names", keyName(SC_W) == "W" && keyName(SC_SPACE) == "SPACE" && keyName(SC_F4) == "F4" && keyName(SC_ESCAPE) == "ESC" && keyName(SC_0) == "0");
}

SCENARIO(keysSettings, "keybinds/settings", "Bindings round-trip through settings.txt names; garbage and the old OpenCode lines fall back to the defaults") {
    Keybinds a;
    a.bind(KA_STEER_ASSIST, SC_F5 + 1);
    a.bind(KA_STRAP, SC_K);
    a.bind(KA_MENU, 59);
    Keybinds b;
    for (int k = 0; k < KA_COUNT; k++) b.loadSetting(settingsName((KeyAction)k), a.key((KeyAction)k));
    b.sanitize();
    bool same = true;
    for (int k = 0; k < KA_COUNT; k++) same &= a.key((KeyAction)k) == b.key((KeyAction)k);
    ctx.check("every binding survives a save and load", same);

    Keybinds old;  // (what 4b48b76 wrote: bare names, with 'steeringassist' clashing with the on/off setting)
    ctx.check("bare legacy names aren't ours", !old.loadSetting("menu", 41) && !old.loadSetting("throttle", 26) && !old.loadSetting("steeringassist", 19));
    ctx.check("so they leave the defaults", old.isDefault());

    Keybinds bad;
    bad.loadSetting("key_menu", -1);
    bad.sanitize();
    ctx.check("a nonsense menu key becomes Esc again", bad.isDefault());
    Keybinds dup;
    dup.loadSetting("key_throttle", SC_ESCAPE);
    dup.sanitize();
    ctx.check("Esc on a driving action is thrown out", dup.isDefault());
    Keybinds twice;
    twice.loadSetting("key_brake", SC_W);
    twice.sanitize();
    ctx.check("two actions on one key are thrown out", twice.isDefault());
}
