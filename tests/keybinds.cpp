// Keyboard bindings and the Esc / pause-menu key path, with no SDL or GL: synthetic SDL key
// events go through the same lookup main.cpp uses (Keybinds::gameKey), and the gamepad's Start
// button through the real pad::Logic. Guards the regression where Esc stopped opening the menu
// after the keybinds system was added (see docs/input-fix.md).
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../src/gamepad.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

// Two bindings that would be the same press in a context both apply in, on the same side of the DEV KEYS toggle.
bool anyClash(const Keybinds& kb) {
    for (int a = 0; a < KA_COUNT; a++)
        for (int b = a + 1; b < KA_COUNT; b++)
            if (kb.code[a] != 0 && kb.code[a] == kb.code[b] && kb.shift[a] == kb.shift[b] && (actionContexts((KeyAction)a) & actionContexts((KeyAction)b)) &&
                isDevAction((KeyAction)a) == isDevAction((KeyAction)b))
                return true;
    return false;
}

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
    ctx.check("defaults never clash in a context, are bindable (but Esc) and usable as keyboard-state indices", [&] {
        for (int a = 0; a < KA_COUNT; a++)
            if (kb.key((KeyAction)a) < 4 || kb.key((KeyAction)a) >= 512 || (a != KA_MENU && !bindable(kb.key((KeyAction)a)))) return false;
        return !anyClash(kb);
    }());
    ctx.check("default keys are the ones the switch uses: E Q V O Esc and F4 for the steering assist",
              kb.gameKey(SC_E, false) == SC_E && kb.gameKey(SC_Q, false) == SC_Q && kb.gameKey(SC_V, false) == SC_V &&
                  kb.gameKey(SC_O, false) == SC_O && kb.gameKey(SC_F4, false) == SC_F4);
    ctx.check("the horn is a held key on 7 (every letter is taken), free in every context, with a pad button of its own", [&] {
        Keybinds h;
        return h.key(KA_HORN) == SC_7 && !h.shift[KA_HORN] && actionContexts(KA_HORN) == KCTX_ANY && h.held(KA_HORN, [] { static uint8_t k[512]; k[SC_7] = 1; return k; }(), false);
    }());
    ctx.check("the steering assist default isn't a key the game already uses (P pauses)", defaultKey(KA_STEER_ASSIST) != SC_P);

    ctx.check("gear up to X is accepted", kb.bind(KA_GEAR_UP, SC_X));
    ctx.check("X is now the gear-up key E (the switch's)", kb.gameKey(SC_X, false) == SC_E);
    ctx.check("E, which gear up gave up, does what X did (the traction control: they swapped)", kb.gameKey(SC_E, false) == SC_X && kb.key(KA_TRACTION) == SC_E);
    ctx.check("but the pad's E (its Y button) still shifts up", kb.gameKey(SC_E, true) == SC_E);

    ctx.check("binding gear down to X swaps: gear up takes the key gear down had", kb.bind(KA_GEAR_DOWN, SC_X) && kb.key(KA_GEAR_UP) == SC_Q && kb.key(KA_GEAR_DOWN) == SC_X);
    ctx.check("still no key with two jobs", !anyClash(kb));

    ctx.check("held actions: the key in the event stream means nothing", kb.bind(KA_THROTTLE, SC_I) && kb.gameKey(SC_I, false) == 0 && kb.key(KA_THROTTLE) == SC_I);
    ctx.check("arrows, keypad Enter, Esc and shift can't be bound", !kb.bind(KA_BRAKE, SC_UP) && !kb.bind(KA_BRAKE, SC_KP_ENTER) && !kb.bind(KA_BRAKE, SC_ESCAPE) &&
                                                                     !kb.bind(KA_BRAKE, SC_LSHIFT) && !kb.bind(KA_BRAKE, 0) && !kb.bind(KA_BRAKE, 600));
    ctx.check("Enter and Tab are ordinary keys now (chat, next vehicle)", bindable(SC_RETURN) && bindable(SC_TAB) && kb.key(KA_CHAT) == SC_RETURN && kb.key(KA_NEXT_VEHICLE) == SC_TAB);
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
    for (int k = 0; k < KA_COUNT; k++) b.loadSetting(settingsName((KeyAction)k), a.saveValue((KeyAction)k));
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
    twice.loadSetting("key_throttle", SC_W);  // (a real file has every line the game knew when it was saved)
    twice.loadSetting("key_brake", SC_W);
    twice.sanitize();
    ctx.check("two actions on one key are thrown out", twice.isDefault());
}

SCENARIO(keysTable, "keybinds/table", "Every keyboard action is in the table once, with a label, a settings name and a group; no clashes among the defaults") {
    std::set<std::string> names, settings;
    bool labels = true, groups[KG_COUNT] = {};
    for (int a = 0; a < KA_COUNT; a++) {
        const std::string l = actionLabel((KeyAction)a), n = settingsName((KeyAction)a);
        labels &= !l.empty() && l.find('&') == std::string::npos && n.rfind("key_", 0) == 0;
        names.insert(l);
        settings.insert(n);
        groups[actionGroup((KeyAction)a)] = true;
    }
    ctx.check("labels and settings names are unique, the font's alphabet, and key_*", labels && (int)names.size() == KA_COUNT && (int)settings.size() == KA_COUNT);
    bool all = true;
    for (bool g : groups) all &= g;
    ctx.check("DRIVING, RECOVERY, ON FOOT, CAMERA AND VIEW, GAME and DEV all have actions",
              all && std::string(groupLabel(KG_FOOT)) == "ON FOOT" && std::string(groupLabel(KG_DEV)) == "DEV");
    ctx.check("there are plenty (every key in the game): over 50", KA_COUNT > 50);
    const Keybinds kb;
    ctx.check("every default is unique within its context and its side of the DEV KEYS toggle", !anyClash(kb));
    ctx.check("each default key resolves to its own action in its own context", [&] {
        for (int a = 0; a < KA_COUNT; a++) {
            const KeyAction ka = (KeyAction)a;
            const unsigned c = actionContexts(ka);
            const unsigned one = c & KCTX_DRIVE ? KCTX_DRIVE : c & KCTX_FOOT ? KCTX_FOOT : KCTX_SCENARIO;
            if (kb.resolve(defaultKey(ka), defaultShift(ka), false, one, true).action != a) return false;
        }
        return true;
    }());
    ctx.check("the winch's own Z and Shift+Z, M map and Shift+M gearbox, L transfer and Shift+L lights are told apart",
              kb.resolve(SC_Z, false, false, KCTX_DRIVE, false).action == KA_WINCH_IN && kb.resolve(SC_Z, true, false, KCTX_DRIVE, false).action == KA_WINCH_OUT &&
                  kb.resolve(SC_M, false, false, KCTX_DRIVE, false).action == KA_MAP && kb.resolve(SC_M, true, false, KCTX_DRIVE, false).action == KA_MANUAL &&
                  kb.resolve(SC_L, false, false, KCTX_DRIVE, false).action == KA_TRANSFER && kb.resolve(SC_L, true, false, KCTX_DRIVE, false).action == KA_LIGHTS);
    const Keybinds::Press lights = kb.resolve(SC_L, true, false, KCTX_DRIVE, false);
    ctx.check("a Shift combo reaches the key switch as its default key with Shift on; a plain one with Shift off",
              lights.key == SC_L && lights.shift && !kb.resolve(SC_L, false, false, KCTX_DRIVE, false).shift && kb.resolve(SC_L, false, false, KCTX_DRIVE, false).key == SC_L);
    ctx.check("a plain binding still answers with Shift incidentally down (G with Shift is the garage)", kb.resolve(SC_G, true, false, KCTX_DRIVE, false).action == KA_GARAGE);
    ctx.check("keys bound to nothing mean nothing; the winch picker's Enter and Backspace pass through",
              kb.resolve(SC_RIGHTBRACKET, false, false, KCTX_DRIVE, false).key == 0 && kb.resolve(SC_BACKSPACE, false, false, KCTX_DRIVE, false).key == SC_BACKSPACE &&
                  kb.resolve(SC_RETURN, false, false, KCTX_DRIVE, false).action == KA_CHAT);
}

SCENARIO(keysContexts, "keybinds/contexts", "One key may do different jobs in the truck and on foot; a swap stays inside a context") {
    Keybinds kb;
    ctx.check("E shifts up in the truck and is interact on foot",
              kb.resolve(SC_E, false, false, KCTX_DRIVE, false).action == KA_GEAR_UP && kb.resolve(SC_E, false, false, KCTX_FOOT, false).action == KA_INTERACT);
    ctx.check("G is the garage in the truck and puts down what you carry on foot; F the front locker or the fuel can",
              kb.resolve(SC_G, false, false, KCTX_DRIVE, false).action == KA_GARAGE && kb.resolve(SC_G, false, false, KCTX_FOOT, false).action == KA_PUT_DOWN &&
                  kb.resolve(SC_F, false, false, KCTX_DRIVE, false).action == KA_LOCK_FRONT && kb.resolve(SC_F, false, false, KCTX_FOOT, false).action == KA_FUEL_CAN);
    ctx.check("the on-foot inventory onto E swaps with interact (same context) and leaves the gear up alone",
              kb.bind(KA_INVENTORY, SC_E) && kb.key(KA_INTERACT) == SC_I && kb.key(KA_GEAR_UP) == SC_E);
    Keybinds a;
    ctx.check("the garage can be put on the key the fuel can has: no clash (truck against foot)", a.bind(KA_GARAGE, SC_F) ? a.key(KA_LOCK_FRONT) == SC_G && a.key(KA_FUEL_CAN) == SC_F : false);
    Keybinds b;
    ctx.check("two actions that share a context swap: the fuel can takes the inventory's key, the inventory the can's", b.bind(KA_FUEL_CAN, SC_I) && b.key(KA_INVENTORY) == SC_F && b.key(KA_FUEL_CAN) == SC_I);
    ctx.check("...and nothing clashes after", !anyClash(a) && !anyClash(b));
    Keybinds c;
    ctx.check("a dev tool and a gameplay action may share a key (the mode decides); two dev tools may not",
              c.bind(KA_DEV_BEAM, SC_C) && c.key(KA_CAMERA) == SC_C && c.bind(KA_DEV_REMOVE, SC_C) && c.key(KA_DEV_BEAM) == SC_DELETE && !anyClash(c));
    ctx.check("DEV KEYS mode: C is the dev tool; GAMEPLAY KEYS mode: the camera",
              c.resolve(SC_C, false, false, KCTX_DRIVE, true).action == KA_DEV_REMOVE && c.resolve(SC_C, false, false, KCTX_DRIVE, false).action == KA_CAMERA);
}

SCENARIO(keysShiftAndHeld, "keybinds/shift-and-held", "Shift combos bind, show and persist; held actions read the keyboard state with Shift in mind") {
    Keybinds kb;
    ctx.check("lights are Shift+L by default and say so", kb.name(KA_LIGHTS) == "SHIFT+L" && kb.name(KA_TRANSFER) == "L" && defaultShift(KA_LIGHTS));
    ctx.check("lights to Shift+K: the dev traffic key is not in the way (dev side)", kb.bind(KA_LIGHTS, SC_K, true) && kb.name(KA_LIGHTS) == "SHIFT+K" && kb.key(KA_DEV_TRAFFIC) == SC_K);
    ctx.check("a press of Shift+K is the lights, plain K the dev traffic in DEV KEYS mode and nothing otherwise",
              kb.resolve(SC_K, true, false, KCTX_DRIVE, true).action == KA_LIGHTS && kb.resolve(SC_K, false, false, KCTX_DRIVE, true).action == KA_DEV_TRAFFIC &&
                  kb.resolve(SC_K, false, false, KCTX_DRIVE, false).action == -1);
    ctx.check("the transfer case onto plain K is no clash with Shift+K", kb.bind(KA_TRANSFER, SC_K) && kb.key(KA_LIGHTS) == SC_K && kb.shift[KA_LIGHTS]);
    ctx.check("the saved value carries the Shift bit and loads back", kb.saveValue(KA_LIGHTS) == (SC_K | 4096) && [&] {
        Keybinds r;
        r.loadSetting("key_lights", kb.saveValue(KA_LIGHTS));
        return r.key(KA_LIGHTS) == SC_K && r.shift[KA_LIGHTS];
    }());

    Keybinds d;
    uint8_t keys[512] = {};
    keys[SC_Z] = 1;
    ctx.check("Z held: winch in, not out", d.held(KA_WINCH_IN, keys, false) && !d.held(KA_WINCH_OUT, keys, false));
    ctx.check("Shift+Z held: winch out, not in", d.held(KA_WINCH_OUT, keys, true) && !d.held(KA_WINCH_IN, keys, true));
    keys[SC_Z] = 0;
    keys[SC_W] = 1;
    ctx.check("W held with Shift down (the drone's fast): still the throttle", d.held(KA_THROTTLE, keys, true) && d.held(KA_THROTTLE, keys, false));
    keys[SC_9] = 1;
    ctx.check("9 held: air down; a rebound one follows its key", d.held(KA_AIR_DOWN, keys, false) && d.bind(KA_AIR_DOWN, SC_8) && !d.held(KA_AIR_DOWN, keys, false));
}

SCENARIO(keysResetExport, "keybinds/reset-export", "Reset all gives the defaults back; unbinding works; the export names every action with its current key") {
    Keybinds kb;
    kb.bind(KA_WINCH, SC_K);
    kb.bind(KA_LIGHTS, SC_B, false);
    kb.unbind(KA_PAUSE);
    ctx.check("unbound shows as NONE and resolves to nothing", kb.name(KA_PAUSE) == "NONE" && kb.resolve(SC_F6, false, false, KCTX_DRIVE, false).action == -1);
    ctx.check("nothing clashes after the rebinds", !anyClash(kb) && !kb.isDefault());
    const std::string text = kb.exportDefaults();
    int lines = 0;
    for (char ch : text) lines += ch == '\n';
    ctx.check("the export is a comment line and one {key, shift} line per action", lines == KA_COUNT + 1 && text.find("{25, false},  // key_winch") == std::string::npos &&
                                                                                  text.find("key_winch: K") != std::string::npos && text.find("key_pause: NONE") != std::string::npos &&
                                                                                  text.find("{0, false},  // key_pause") != std::string::npos);
    kb.resetDefaults();
    ctx.check("reset all: the defaults, every action", kb.isDefault() && kb.name(KA_LIGHTS) == "SHIFT+L" && kb.key(KA_PAUSE) == SC_F6);
    const std::string def = kb.exportDefaults();
    ctx.check("the default export lists the defaults", def.find("key_throttle: W") != std::string::npos && def.find("key_lights: SHIFT+L") != std::string::npos &&
                                                         def.find("key_devmenu: F12") != std::string::npos);
}

SCENARIO(keysFile, "keybinds/settings-file", "A custom layout (Shift combos, unbound, swapped) round-trips through a scratch settings file; an old file keeps its bindings") {
    const char* path = "build/test-keybinds-settings.txt";  // (a scratch file: never the config dir)
    Keybinds a;
    a.bind(KA_GEAR_UP, SC_X);
    a.bind(KA_LIGHTS, SC_K, true);
    a.bind(KA_DEV_VEHICLE3, SC_F6);
    a.unbind(KA_HELP);
    a.bind(KA_CHAT, SC_T);
    {
        FILE* f = std::fopen(path, "w");
        ctx.check("scratch file written", f != nullptr);
        if (!f) return;
        std::fputs("mph 1\n", f);
        for (int k = 0; k < KA_COUNT; k++) std::fprintf(f, "%s %d\n", settingsName((KeyAction)k), a.saveValue((KeyAction)k));
        std::fclose(f);
    }
    Keybinds b;
    {
        FILE* f = std::fopen(path, "r");
        ctx.check("scratch file read", f != nullptr);
        if (!f) return;
        char line[128], key[64];
        int v = 0;
        while (std::fgets(line, sizeof line, f))
            if (std::sscanf(line, "%63s %d", key, &v) == 2) b.loadSetting(key, v);
        std::fclose(f);
        b.sanitize();
    }
    bool same = true;
    for (int k = 0; k < KA_COUNT; k++) same &= a.key((KeyAction)k) == b.key((KeyAction)k) && a.shift[k] == b.shift[k];
    ctx.check("every binding survives, Shift combos and the unbound help key included", same && b.key(KA_HELP) == 0 && b.shift[KA_LIGHTS]);

    Keybinds old;  // a file from before most of the actions: only the old lines, one moved onto a key a new action has as its default
    old.loadSetting("key_throttle", SC_W);
    old.loadSetting("key_winch", SC_F);  // (F is the new front locker's default)
    old.sanitize();
    ctx.check("the saved winch key stays; the newer front locker moves aside to a free key", old.key(KA_WINCH) == SC_F && old.key(KA_LOCK_FRONT) != SC_F && old.key(KA_LOCK_FRONT) != 0 && !anyClash(old));
    ctx.check("everything else of the old file is just the defaults", old.key(KA_CAMERA) == SC_C && old.key(KA_LIGHTS) == SC_L && old.shift[KA_LIGHTS]);
}

SCENARIO(keysHints, "keybinds/hints", "The names the hints show follow the bindings") {
    Keybinds kb;
    ctx.check("defaults", kb.name(KA_REPAIR) == "R" && kb.name(KA_INTERACT) == "E" && kb.name(KA_PLAYTEST_BACK) == "F1" && kb.name(KA_HELP) == "H");
    kb.bind(KA_REPAIR, SC_F6 + 1);
    kb.bind(KA_INTERACT, SC_G);
    kb.bind(KA_PLAYTEST_BACK, SC_F6);
    ctx.check("rebound", kb.name(KA_REPAIR) == "F7" && kb.name(KA_INTERACT) == "G" && kb.name(KA_PLAYTEST_BACK) == "F6");
    ctx.check("a combo shows its Shift", (kb.bind(KA_HELP, SC_F, true), kb.name(KA_HELP) == "SHIFT+F"));
}
