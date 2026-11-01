// Full controller support and every binding rebindable (keybinds.h, gamepad.h, the CONTROLS tab): the pad's half of
// the bind table (defaults, conflicts, saving and old files), the menu keys (keyboard and pad, with the fixed Esc /
// Enter / A / B fallbacks), and the menus driven by nothing but synthetic pad input through the real pad::Logic,
// as main.cpp feeds it. No SDL, no GL.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../src/gamepad.h"
#include "../src/garageshop.h"
#include "../src/keybinds.h"
#include "../src/mainmenu.h"
#include "../src/menu.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

bool anyPadClash(const Keybinds& kb) {
    for (int a = 0; a < KA_COUNT; a++)
        for (int b = a + 1; b < KA_COUNT; b++)
            if (kb.pad[a] != 0 && kb.pad[a] == kb.pad[b] && (actionContexts((KeyAction)a) & actionContexts((KeyAction)b)) && isDevAction((KeyAction)a) == isDevAction((KeyAction)b))
                return true;
    return false;
}
bool anyKeyClash(const Keybinds& kb) {
    for (int a = 0; a < KA_COUNT; a++)
        for (int b = a + 1; b < KA_COUNT; b++)
            if (kb.code[a] != 0 && kb.code[a] == kb.code[b] && kb.shift[a] == kb.shift[b] && (actionContexts((KeyAction)a) & actionContexts((KeyAction)b)) &&
                isDevAction((KeyAction)a) == isDevAction((KeyAction)b))
                return true;
    return false;
}
bool sameBinds(const Keybinds& a, const Keybinds& b) {
    for (int k = 0; k < KA_COUNT; k++)
        if (a.code[k] != b.code[k] || a.shift[k] != b.shift[k] || a.pad[k] != b.pad[k]) return false;
    return true;
}

// The pad, a frame at a time, as main.cpp drives it: `hold` buttons down this frame.
struct Pad {
    pad::Logic l;
    pad::Raw r;
    const std::vector<pad::Tap>& frame(uint32_t hold, pad::Mode m, float rt = 0, float lt = 0) {
        r = pad::Raw();
        r.buttons = hold, r.rt = rt, r.lt = lt;
        l.update(r, m, 1.0 / 60);
        return l.taps;
    }
    // A press and its release: the taps of both frames.
    std::vector<pad::Tap> tap(pad::Button b, pad::Mode m, uint32_t hold = 0) {
        frame(hold, m);
        std::vector<pad::Tap> t = frame(hold | pad::bit(b), m);
        const std::vector<pad::Tap>& u = frame(hold, m);
        t.insert(t.end(), u.begin(), u.end());
        return t;
    }
};
int vehicleIndex(const char* id) {
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].id) == id) return v;
    return 0;
}

bool tapped(const std::vector<pad::Tap>& t, int sc, int action = -2) {
    for (const pad::Tap& x : t)
        if (x.scancode == sc && (action == -2 || x.action == action)) return true;
    return false;
}

// The pause menu fed with the pad only: each tap as main.cpp gives it (fromPad), and while the CONTROLS tab waits
// for a binding, the pad's presses instead.
struct PadMenu {
    Settings& s;
    PauseMenu& m;
    Pad p;
    PauseMenu::Result last;
    void tap(pad::Button b, uint32_t hold = 0) {
        for (int f = 0; f < 3; f++) {
            const uint32_t bits = f == 1 ? hold | pad::bit(b) : hold;
            p.frame(bits, pad::Mode::Menu);
            if (m.listeningForKey()) {
                for (const pad::Press& pr : p.l.presses)
                    if (m.listeningForKey()) last = m.padKey(pr, s);
                continue;
            }
            for (const pad::Tap& t : p.l.taps) {
                m.fromPad = true;
                last = m.key(t.scancode, s);
                m.fromPad = false;
            }
        }
    }
    bool downTo(const std::string& label) {
        for (int k = 0; k < 300 && m.selectedLabel(s) != label; k++) tap(pad::B_DOWN);
        return m.selectedLabel(s) == label;
    }
};

}  // namespace

SCENARIO(ctlDefaults, "controls/defaults", "Every action has a default key and a default pad binding, all usable and none clashing; Start is the menu; the layers are LB and VIEW") {
    const Keybinds kb;
    bool keys = true, pads = true, usable = true;
    std::string missing;
    for (int a = 0; a < KA_COUNT; a++) {
        const KeyAction ka = (KeyAction)a;
        if (defaultKey(ka) == 0) keys = false, missing += std::string(actionLabel(ka)) + " (key) ";
        if (defaultPad(ka) == 0) pads = false, missing += std::string(actionLabel(ka)) + " (pad) ";
        if (!padBindable(ka, defaultPad(ka))) usable = false, missing += std::string(actionLabel(ka)) + " (pad unusable) ";
        if (a != KA_MENU && !bindableFor(ka, defaultKey(ka))) usable = false, missing += std::string(actionLabel(ka)) + " (key unusable) ";
    }
    ctx.check("every action has a default key" + (keys ? std::string() : ": " + missing), keys);
    ctx.check("every action has a default pad binding" + (pads ? std::string() : ": " + missing), pads);
    ctx.check("and every default is one it may have", usable);
    ctx.check("no two defaults clash (keys or pad) in a context they share", !anyKeyClash(kb) && !anyPadClash(kb));
    ctx.check("there are menu and free cam actions with their own contexts",
              actionContexts(KA_UI_UP) == KCTX_MENU && actionContexts(KA_UI_CHOICE1) == KCTX_DIALOG && actionContexts(KA_FC_GRAB) == KCTX_FREECAM && isUiAction(KA_UI_BACK) && !isUiAction(KA_FC_GRAB));
    ctx.check("labels and settings names unique (pad lines are pad_*)", [&] {
        std::set<std::string> l, n, p;
        for (int a = 0; a < KA_COUNT; a++) l.insert(actionLabel((KeyAction)a)), n.insert(settingsName((KeyAction)a)), p.insert(padSettingsName((KeyAction)a));
        return (int)l.size() == KA_COUNT && (int)n.size() == KA_COUNT && (int)p.size() == KA_COUNT && padSettingsName(KA_THROTTLE) == "pad_throttle";
    }());
    ctx.check("the classic map: RT / LT pedals, the left stick steers, A handbrake, X / Y the gears, RB camera, Start menu",
              kb.pad[KA_THROTTLE] == PI_RT && kb.pad[KA_BRAKE] == PI_LT && kb.pad[KA_STEER_LEFT] == PI_LS_LEFT && kb.pad[KA_HANDBRAKE] == PI_A && kb.pad[KA_GEAR_DOWN] == PI_X &&
                  kb.pad[KA_GEAR_UP] == PI_Y && kb.pad[KA_CAMERA] == PI_RB && kb.pad[KA_MENU] == PI_START);
    ctx.check("LB is the second layer (repair, winch...), VIEW the third (parking brake...), LB + VIEW the dev tools",
              kb.pad[KA_REPAIR] == (PM_LB | PI_A) && kb.pad[KA_WINCH_IN] == (PM_LB | PI_RT) && kb.pad[KA_PARK_BRAKE] == (PM_VIEW | PI_A) && kb.pad[KA_DEV_FREECAM] == (PM_LB | PM_VIEW | PI_L3));
    ctx.check("P stays the parking brake", kb.key(KA_PARK_BRAKE) == SC_P);
    ctx.check("names: Xbox and PlayStation", kb.padName(KA_REPAIR) == "LB + A" && kb.padName(KA_REPAIR, true) == "L1 + CROSS" && kb.padName(KA_PARK_BRAKE) == "VIEW + A" &&
                                                 kb.padName(KA_THROTTLE, true) == "R2" && kb.padName(KA_STEER_LEFT) == "L-STICK LEFT" && kb.padName(KA_DEV_FREECAM) == "LB + VIEW + L3");
    ctx.check("the career repair's choices have keys and pad buttons (1 / 2, X / Y)", kb.key(KA_UI_CHOICE1) == SC_1 && kb.key(KA_UI_CHOICE2) == SC_2 && kb.pad[KA_UI_CHOICE1] == PI_X && kb.pad[KA_UI_CHOICE2] == PI_Y);
}

SCENARIO(ctlRoundTrip, "controls/round-trip", "Keys and pad bindings, rebound, swapped and cleared, come back the same through settings.txt, the game's own save and load") {
    Settings a;
    a.keys.bind(KA_GEAR_UP, SC_X);
    a.keys.bind(KA_LIGHTS, SC_K, true);
    a.keys.bind(KA_UI_UP, SC_I);
    a.keys.unbind(KA_HELP);
    a.keys.bindPad(KA_HORN, PI_BACK);
    a.keys.bindPad(KA_GEAR_UP, PI_RB);
    a.keys.bindPad(KA_UI_TAB_NEXT, PI_RT);
    a.keys.bindPad(KA_FC_GRAB, PM_LB | PI_Y);
    a.keys.unbindPad(KA_PHOTO);
    ctx.check("the layout is not the defaults, and nothing clashes", !a.keys.isDefault() && !anyKeyClash(a.keys) && !anyPadClash(a.keys));
    const char* path = "build/test-controls-settings.txt";  // (a scratch file: never the config dir)
    ctx.check("saved", a.save(path));
    Settings b;
    ctx.check("loaded", b.load(path));
    ctx.check("every key and pad binding came back", sameBinds(a.keys, b.keys));
    ctx.check("the cleared ones stay cleared", b.keys.key(KA_HELP) == 0 && b.keys.pad[KA_PHOTO] == 0);
    ctx.check("the chords and Shift combos too", b.keys.pad[KA_FC_GRAB] == (PM_LB | PI_Y) && b.keys.shift[KA_LIGHTS] && b.keys.pad[KA_HORN] == PI_BACK);
    Keybinds c;
    for (int k = 0; k < KA_COUNT; k++) c.loadSetting(settingsName((KeyAction)k), a.keys.saveValue((KeyAction)k)), c.loadSetting(padSettingsName((KeyAction)k), a.keys.padSaveValue((KeyAction)k));
    c.sanitize();
    ctx.check("and line by line, through loadSetting", sameBinds(a.keys, c));
    std::remove(path);
}

SCENARIO(ctlMigration, "controls/migration", "Old settings files: before the pad bindings and the menu keys they get the defaults; the old pause-on-P file keeps P the parking brake; bad lines fall back") {
    Keybinds old;  // a file from before the pad was rebindable: key lines only, a player's custom layout
    for (int k = 0; k <= KA_DEV_SCENARIO; k++) old.loadSetting(settingsName((KeyAction)k), Keybinds().saveValue((KeyAction)k));
    old.loadSetting("key_gearup", SC_X);  // (moved...)
    old.loadSetting("key_traction", SC_E);  // (...swapped with the traction control, as the page would have)
    old.sanitize();
    ctx.check("its keys are kept", old.key(KA_GEAR_UP) == SC_X && old.key(KA_TRACTION) == SC_E);
    ctx.check("every pad binding is the default", [&] {
        for (int a = 0; a < KA_COUNT; a++)
            if (!old.isPadDefault((KeyAction)a)) return false;
        return true;
    }());
    ctx.check("the menu keys (new) are their defaults, the menus' own context so no clash with the old keys", old.key(KA_UI_UP) == SC_W && old.key(KA_UI_CONFIRM) == SC_SPACE && !anyKeyClash(old));

    Keybinds legacy;  // (from before the parking brake had P: pause on P, the brake moved aside to F7)
    legacy.loadSetting("key_pause", SC_P);
    legacy.loadSetting("key_parkbrake", 64);
    legacy.sanitize();
    ctx.check("legacyPauseOnP: the parking brake comes back to P", legacy.key(KA_PARK_BRAKE) == SC_P && !legacy.legacyPauseOnP);

    Keybinds bad;
    bad.loadSetting("pad_throttle", PI_START);     // (Start is the menu)
    bad.loadSetting("pad_uiup", PM_LB | PI_DUP);   // (no layers in the menus)
    bad.loadSetting("pad_brake", 9999);            // (nonsense)
    bad.loadSetting("pad_handbrake", PI_RB);       // (the camera's, whose line isn't there)
    bad.loadSetting("key_uiconfirm", SC_RETURN);   // (not a name of ours: ignored)
    bad.loadSetting("key_uiok", SC_RETURN);        // (Enter is always OK: not a binding)
    bad.sanitize();
    ctx.check("unusable pad lines go back to the defaults", bad.pad[KA_THROTTLE] == PI_RT && bad.pad[KA_UI_UP] == PI_DUP && bad.pad[KA_BRAKE] == PI_LT);
    ctx.check("a saved pad binding wins; the action whose default it took is moved off it, never two on one",
              bad.pad[KA_HANDBRAKE] == PI_RB && bad.pad[KA_CAMERA] != PI_RB && !anyPadClash(bad));
    ctx.check("Enter can't be a menu key", bad.key(KA_UI_CONFIRM) == SC_SPACE);

    Keybinds newer;  // a file with pad lines from before an action existed: the newer action's default, if a saved line took it, is cleared
    for (int k = 0; k < KA_COUNT; k++)
        if (k != KA_FUEL_CAN) newer.loadSetting(padSettingsName((KeyAction)k), Keybinds().padSaveValue((KeyAction)k));
    newer.loadSetting("pad_putdown", PI_DLEFT);  // (the fuel can's default, on foot)
    newer.sanitize();
    ctx.check("...so the saved one keeps it and nothing clashes", newer.pad[KA_PUT_DOWN] == PI_DLEFT && newer.pad[KA_FUEL_CAN] != PI_DLEFT && !anyPadClash(newer));
}

SCENARIO(ctlConflicts, "controls/conflicts", "Binding a key or a button another action has swaps them (and says which); the fixed ones are refused; reset per action and for all") {
    Keybinds kb;
    ctx.check("the clash is found before the bind", kb.padClashWith(KA_GEAR_UP, PI_RB) == KA_CAMERA && kb.clashWith(KA_GEAR_UP, SC_C, false) == KA_CAMERA);
    ctx.check("X onto the gear up swaps: the gear down gets Y", kb.bindPad(KA_GEAR_UP, PI_X) && kb.pad[KA_GEAR_UP] == PI_X && kb.pad[KA_GEAR_DOWN] == PI_Y && !anyPadClash(kb));
    ctx.check("swapped back", kb.bindPad(KA_GEAR_UP, PI_Y) && kb.pad[KA_GEAR_DOWN] == PI_X);
    ctx.check("RB onto the gear up: the camera can't take Y (on foot it's the inventory's), so it is left without one, no clash",
              kb.bindPad(KA_GEAR_UP, PI_RB) && kb.pad[KA_GEAR_UP] == PI_RB && kb.pad[KA_CAMERA] == 0 && kb.pad[KA_INVENTORY] == PI_Y && !anyPadClash(kb));
    ctx.check("the same for keys: C onto the gear up, the camera can't take E (interact, on foot)", kb.bind(KA_GEAR_UP, SC_C) && kb.key(KA_CAMERA) == 0 && kb.key(KA_INTERACT) == SC_E && !anyKeyClash(kb));
    kb.resetAction(KA_CAMERA);
    kb.resetAction(KA_GEAR_UP);
    ctx.check("reset per action puts both back", kb.isDefault(KA_CAMERA) && kb.isDefault(KA_GEAR_UP));
    kb.bindPad(KA_GEAR_UP, PI_RB);
    ctx.check("a different context is no clash: X is the gear down and interact on foot", kb.padClashWith(KA_INTERACT, PI_X) == -1 && kb.pad[KA_GEAR_DOWN] == PI_X);
    ctx.check("a chord is not its plain button (LB + X and X)", kb.padClashWith(KA_TRACTION, PI_X) == KA_GEAR_DOWN && kb.padClashWith(KA_TRACTION, PM_LB | PI_X) == -1);
    ctx.check("Start alone is refused (the menu), with a layer it's fine", !kb.bindPad(KA_HORN, PI_START) && kb.bindPad(KA_HORN, PM_VIEW | PI_START));
    ctx.check("LB on its own is refused in the game (a layer), VIEW only alone", !kb.bindPad(KA_HORN, PI_LB) && !kb.bindPad(KA_HORN, PM_LB | PI_BACK) && padBindable(KA_HORN, PI_BACK));
    ctx.check("the menu's: no layers; A only OK, B only back; LB is a button there", !kb.bindPad(KA_UI_UP, PM_LB | PI_X) && !kb.bindPad(KA_UI_SECOND, PI_A) && !kb.bindPad(KA_UI_SECOND, PI_B) &&
                                                                                     padBindable(KA_UI_TAB_PREV, PI_LB));
    ctx.check("the menu action is Start only", !kb.bindPad(KA_MENU, PI_A) && kb.pad[KA_MENU] == PI_START);
    ctx.check("keys: Enter is no menu key, Esc no key at all", !kb.bind(KA_UI_SECOND, SC_RETURN) && !kb.bind(KA_UI_UP, SC_ESCAPE) && kb.bind(KA_CHAT, SC_RETURN));
    ctx.check("a menu key swaps inside the menus only: Q onto OK swaps with the previous tab, the gear down keeps Q",
              kb.bind(KA_UI_CONFIRM, SC_Q) && kb.key(KA_UI_TAB_PREV) == SC_SPACE && kb.key(KA_GEAR_DOWN) == SC_Q && !anyKeyClash(kb));
    kb.resetAction(KA_GEAR_UP);
    ctx.check("reset one action: its button back", kb.isDefault(KA_GEAR_UP) && kb.pad[KA_GEAR_UP] == PI_Y);
    kb.unbindPad(KA_WINCH);
    kb.bind(KA_WINCH, SC_K);
    kb.resetDefaults();
    ctx.check("reset all: every key and button", kb.isDefault() && sameBinds(kb, Keybinds()));
}

SCENARIO(ctlPadHeld, "controls/pad-held", "The held actions follow their pad bindings: a chord counts with its layer held, a plain one is masked only by a chord of its own input") {
    using namespace pad;
    Settings s;
    Pad p;
    p.l.binds = &s.keys;
    p.frame(0, Mode::Drive, 1, 0);
    ctx.check("RT is the throttle", p.l.in.throttle > 0.99 && p.l.in.winch == 0);
    p.frame(bit(B_LB), Mode::Drive, 1, 0);
    ctx.check("LB + RT reels in, no throttle", p.l.in.winch == 1 && p.l.in.throttle == 0);
    p.frame(bit(B_BACK), Mode::Drive, 1, 0);
    ctx.check("VIEW + RT is no chord: still the throttle", p.l.in.throttle > 0.99);
    s.keys.bindPad(KA_THROTTLE, PI_A);
    s.keys.bindPad(KA_HANDBRAKE, PI_B);
    p.frame(bit(B_A), Mode::Drive);
    ctx.check("throttle rebound to A: full throttle on A, nothing on RT", p.l.in.throttle == 1 && (p.frame(0, Mode::Drive, 1, 0), p.l.in.throttle == 0));
    p.frame(bit(B_B), Mode::Drive);
    ctx.check("handbrake rebound to B", p.l.in.handbrake);
    s.keys.bindPad(KA_STEER_LEFT, PI_DLEFT);  // (the front locker's: they swap)
    p.frame(bit(B_LEFT), Mode::Drive);
    ctx.check("steering on the D-pad: full left", p.l.in.steerTarget > 0.99 && p.l.in.steerStick);
    s.keys.resetDefaults();
    p.frame(bit(B_LB) | bit(B_RSTICK), Mode::Drive);
    ctx.check("LB + R3 is the horn, not looking back", p.l.in.horn && !p.l.in.lookBack);
    p.frame(0, Mode::Drive);
    p.frame(bit(B_BACK) | bit(B_LSTICK), Mode::Drive);
    ctx.check("VIEW + L3 holds the full HUD", p.l.in.hudAll);
    p.l.contexts = KCTX_FOOT;
    p.frame(0, Mode::Drive);
    p.frame(bit(B_DOWN), Mode::Drive);
    ctx.check("on foot D-pad down crouches (L3 is the lights)", p.l.in.crouchHeld);
}

SCENARIO(ctlPadTaps, "controls/pad-taps", "A pad press becomes its action's default key; VIEW alone is the map when let go; a rebound button follows; the free cam has its own") {
    using namespace pad;
    Settings s;
    Pad p;
    p.l.binds = &s.keys;
    ctx.check("VIEW + A: the parking brake (P), and no map after", [&] {
        const std::vector<Tap> t = p.tap(B_A, Mode::Drive, bit(B_BACK));
        p.frame(0, Mode::Drive);
        return tapped(t, SC_P, KA_PARK_BRAKE) && !tapped(t, SC_M) && !tapped(p.l.taps, SC_M);
    }());
    ctx.check("VIEW tapped alone: the map", [&] {
        p.frame(0, Mode::Drive);
        p.frame(bit(B_BACK), Mode::Drive);
        const bool none = p.l.taps.empty();
        return none && tapped(p.frame(0, Mode::Drive), SC_M, KA_MAP);
    }());
    ctx.check("LB + VIEW + L3 is the free cam in DEV KEYS mode, nothing with gameplay keys", [&] {
        p.l.devKeys = true;
        const bool on = tapped(p.tap(B_LSTICK, Mode::Drive, bit(B_LB) | bit(B_BACK)), SC_F8, KA_DEV_FREECAM);
        p.l.devKeys = false;
        const std::vector<Tap> off = p.tap(B_LSTICK, Mode::Drive, bit(B_LB) | bit(B_BACK));
        return on && !tapped(off, SC_F8);
    }());
    s.keys.bindPad(KA_REPAIR, PI_RB);  // (the camera swaps onto LB + A)
    ctx.check("rebound: RB is the repair (R), LB + A the camera (C)", tapped(p.tap(B_RB, Mode::Drive), SC_R, KA_REPAIR) && tapped(p.tap(B_A, Mode::Drive, bit(B_LB)), SC_C, KA_CAMERA));
    s.keys.resetDefaults();
    ctx.check("the stick's a button too: LB + right stick up is the winch speed up (=)", [&] {
        p.frame(0, Mode::Drive);
        p.r = Raw();
        p.r.buttons = bit(B_LB);
        p.r.ry = -1;
        p.l.update(p.r, Mode::Drive, 1.0 / 60);
        return tapped(p.l.taps, SC_EQUALS, KA_WINCH_FASTER) && p.l.in.lookY == 0;
    }());
    p.frame(0, Mode::Drive);
    p.l.contexts = KCTX_FREECAM;
    p.l.fallback = KCTX_DRIVE;
    ctx.check("in the free cam A picks up (G), Y the spawn picker (B), RB drives (V)", tapped(p.tap(B_A, Mode::Drive), SC_G, KA_FC_GRAB) && tapped(p.tap(B_Y, Mode::Drive), SC_B, KA_FC_SPAWN) &&
                                                                                        tapped(p.tap(B_RB, Mode::Drive), SC_V, KA_FC_DRIVE));
    ctx.check("...and the game's always-on ones still answer (VIEW + R3: the debug info)", tapped(p.tap(B_RSTICK, Mode::Drive, bit(B_BACK)), SC_F3, KA_DEBUG_INFO));
    ctx.check("Start is the menu anywhere", tapped(p.tap(B_START, Mode::Drive), SC_ESCAPE, KA_MENU));
}

SCENARIO(ctlMenuKeys, "controls/menu-keys", "The menus go through the bind table: a rebound menu key works and its old key is free; Esc, Enter and the arrows always work") {
    Settings s;
    ctx.check("defaults: W up, Space OK, Backspace back, Q / E tabs, T second, R default", s.keys.uiKey(SC_W) == SC_UP && s.keys.uiKey(SC_SPACE) == SC_RETURN && s.keys.uiKey(SC_BACKSPACE) == SC_ESCAPE &&
                                                                                           s.keys.uiKey(SC_Q) == UK_TAB_PREV && s.keys.uiKey(SC_E) == UK_TAB_NEXT && s.keys.uiKey(SC_T) == UK_SECOND && s.keys.uiKey(SC_R) == UK_FACTORY);
    ctx.check("the fixed ones are themselves", s.keys.uiKey(SC_ESCAPE) == SC_ESCAPE && s.keys.uiKey(SC_RETURN) == SC_RETURN && s.keys.uiKey(SC_UP) == SC_UP && s.keys.uiKey(SC_KP_ENTER) == SC_KP_ENTER);
    ctx.check("a dialog's choices only in a dialog", s.keys.uiKey(SC_1, false, KCTX_DIALOG) == UK_CHOICE1 && s.keys.uiKey(SC_1) == SC_1);
    s.keys.bind(KA_UI_UP, SC_I);
    PauseMenu m;
    m.show(0, 0);
    m.select(s, "QUIT GAME");
    m.key(SC_I, s);
    ctx.check("UP rebound to I: I moves up", m.selectedLabel(s) == "MAIN MENU");
    m.key(SC_W, s);
    ctx.check("...and W, given up, does nothing", m.selectedLabel(s) == "MAIN MENU");
    m.key(SC_UP, s);
    ctx.check("the arrow still works", m.selectedLabel(s) == "SETTINGS");
    s.keys.bind(KA_UI_CONFIRM, SC_K);
    ctx.check("OK rebound to K: Enter still opens SETTINGS", (m.key(SC_RETURN, s), m.pageNow() == 1));
    s.keys.bind(KA_UI_BACK, SC_F6);
    ctx.check("back rebound: Backspace is free, F6 and Esc go back", (m.key(SC_BACKSPACE, s), m.pageNow() == 1) && (m.key(SC_F6, s), m.pageNow() == 0));
    s.keys.bind(KA_UI_DOWN, SC_J);
    mainmenu::Nav n;
    n.binds = &s.keys;
    n.open(0, 0);
    const int before = n.sel;
    n.key(SC_S);
    const int afterS = n.sel;
    n.key(SC_J);
    ctx.check("the main menu too: DOWN rebound to J, J moves, S doesn't", afterS == before && n.sel != before);
}

SCENARIO(ctlPadMenus, "controls/pad-menus", "Pad only: Start opens the menu, the D-pad and A get to SETTINGS > CONTROLS (RB), a button press rebinds, Y resets, X clears, B and Start back out") {
    using namespace pad;
    Settings s;
    PauseMenu m;
    PadMenu pm{s, m, {}, {}};
    pm.p.l.binds = &s.keys;
    pm.p.frame(0, Mode::Drive);
    ctx.check("Start in the game: the menu (Esc)", tapped(pm.p.tap(B_START, Mode::Drive), SC_ESCAPE, KA_MENU));
    m.show(0, 0);  // (what main.cpp does with that Esc)
    pm.tap(B_DOWN);
    pm.tap(B_DOWN);
    ctx.check("D-pad down twice: SETTINGS", m.selectedLabel(s) == "SETTINGS");
    pm.tap(B_A);
    ctx.check("A opens it", m.pageNow() == 1 && m.tabNow() == PauseMenu::T_GRAPHICS);
    pm.tap(B_RB);
    ctx.check("RB: the CONTROLS tab", m.tabNow() == PauseMenu::T_CONTROLS);
    pm.tap(B_LB);
    pm.tap(B_RB);
    ctx.check("LB back, RB again", m.tabNow() == PauseMenu::T_CONTROLS);
    ctx.check("down to GEAR UP", pm.downTo("GEAR UP"));
    pm.tap(B_A);
    ctx.check("A: waiting for a key or a button", m.listeningForKey());
    pm.tap(B_X);
    ctx.check("X pressed: GEAR UP is X, the gear down swapped onto Y", !m.listeningForKey() && s.keys.pad[KA_GEAR_UP] == PI_X && s.keys.pad[KA_GEAR_DOWN] == PI_Y && pm.last.action == PauseMenu::CHANGED);
    pm.tap(B_Y);
    ctx.check("Y on the row: its defaults back (and the gear down's)", s.keys.isDefault(KA_GEAR_UP) && s.keys.isDefault(KA_GEAR_DOWN));
    pm.tap(B_X);
    ctx.check("X on the row: the pad button cleared, the key kept", s.keys.pad[KA_GEAR_UP] == 0 && s.keys.key(KA_GEAR_UP) == SC_E);
    pm.tap(B_Y);
    ctx.check("down to TRACTION CONTROL", pm.downTo("TRACTION CONTROL"));
    pm.tap(B_A);
    pm.tap(B_UP, bit(B_LB));  // (hold LB, press D-pad up: a chord)
    ctx.check("LB held and D-pad up: the chord LB + D-PAD UP (the winch's: swapped)", s.keys.pad[KA_TRACTION] == (PM_LB | PI_DUP) && s.keys.pad[KA_WINCH] == (PM_LB | PI_X));
    ctx.check("down to HORN (HOLD)... and back up to it", pm.downTo("HORN (HOLD)") || [&] {
        for (int k = 0; k < 300 && m.selectedLabel(s) != "HORN (HOLD)"; k++) pm.tap(B_UP);
        return m.selectedLabel(s) == "HORN (HOLD)";
    }());
    pm.tap(B_A);
    pm.tap(B_BACK);  // (VIEW pressed and let go alone)
    ctx.check("VIEW tapped: HORN on VIEW, the map swapped onto LB + R3", s.keys.pad[KA_HORN] == PI_BACK && s.keys.pad[KA_MAP] == (PM_LB | PI_R3));
    pm.tap(B_A);
    pm.tap(B_LB);
    ctx.check("LB alone can't be a game binding: still waiting, with a note", m.listeningForKey());
    pm.tap(B_START);
    ctx.check("Start cancels", !m.listeningForKey() && s.keys.pad[KA_HORN] == PI_BACK);
    ctx.check("down to RESET ALL TO DEFAULTS, A", pm.downTo("RESET ALL TO DEFAULTS") && (pm.tap(B_A), s.keys.isDefault()));
    pm.tap(B_B);
    ctx.check("B: back to the pause menu", m.pageNow() == 0 && m.open);
    pm.tap(B_START);
    ctx.check("Start: resume", pm.last.action == PauseMenu::RESUME);
}

SCENARIO(ctlPadOtherMenus, "controls/pad-other-menus", "Pad only in the main menu, the garage and a dialog: D-pad, A, B, the shoulders and X / Y arrive as their menu keys") {
    using namespace pad;
    Settings s;
    Pad p;
    p.l.binds = &s.keys;
    mainmenu::Nav n;
    n.binds = &s.keys;
    n.open(0, 0);
    auto feed = [&](const std::vector<Tap>& t) {
        mainmenu::Result r;
        for (const Tap& x : t) r = n.key(x.scancode);
        return r;
    };
    const int s0 = n.sel;
    feed(p.tap(B_DOWN, Mode::Menu));
    ctx.check("main menu: D-pad down moves", n.sel != s0);
    feed(p.tap(B_UP, Mode::Menu));
    ctx.check("D-pad up comes back", n.sel == s0);
    n.page = mainmenu::P_WORLDS;
    n.worlds.push_back(WorldSave());
    n.sel = 0;
    feed(p.tap(B_X, Mode::Menu));
    ctx.check("the career worlds: X asks to delete the world", n.page == mainmenu::P_DELETE_WORLD);
    feed(p.tap(B_B, Mode::Menu));
    ctx.check("B backs out of it", n.page != mainmenu::P_DELETE_WORLD);

    Career sandbox;
    shop::Shop g;
    g.binds = &s.keys;
    g.begin(vehicleIndex("bronco"), ModSet(), sandbox, false, true);
    auto shopFeed = [&](const std::vector<Tap>& t) {
        for (const Tap& x : t) g.key(x.scancode, sandbox);
    };
    shopFeed(p.tap(B_A, Mode::Menu));
    ctx.check("garage: A opens a category", g.focus == shop::F_ITEMS);
    const int cat = g.cat;
    shopFeed(p.tap(B_RB, Mode::Menu));
    ctx.check("RB: the next category", g.cat == cat + 1);
    shopFeed(p.tap(B_LB, Mode::Menu));
    ctx.check("LB: back", g.cat == cat);
    shopFeed(p.tap(B_B, Mode::Menu));
    shopFeed(p.tap(B_B, Mode::Menu));
    ctx.check("B twice: closed", !g.open);

    p.l.menuContexts = KCTX_DIALOG | KCTX_MENU;
    ctx.check("a dialog: X is the first choice, Y the second, A OK, B back, the D-pad moves",
              tapped(p.tap(B_X, Mode::Menu), UK_CHOICE1, KA_UI_CHOICE1) && tapped(p.tap(B_Y, Mode::Menu), UK_CHOICE2, KA_UI_CHOICE2) && tapped(p.tap(B_A, Mode::Menu), SC_RETURN) &&
                  tapped(p.tap(B_B, Mode::Menu), SC_ESCAPE) && tapped(p.tap(B_DOWN, Mode::Menu), SC_DOWN));
    p.l.menuContexts = KCTX_MENU;
    ctx.check("outside a dialog X is the second action (try on / delete / clear)", tapped(p.tap(B_X, Mode::Menu), UK_SECOND, KA_UI_SECOND));
    s.keys.bindPad(KA_UI_CONFIRM, PI_X);
    ctx.check("OK rebound to X: X is OK, and A still is", tapped(p.tap(B_X, Mode::Menu), SC_RETURN, KA_UI_CONFIRM) && tapped(p.tap(B_A, Mode::Menu), SC_RETURN));
    ctx.check("the map: A sets the waypoint, RT zooms, B closes", [&] {
        p.frame(0, Mode::Map);
        p.frame(bit(B_A), Mode::Map);
        const bool sel = p.l.in.mapSelect;
        p.frame(0, Mode::Map, 1, 0);
        const bool zoom = p.l.in.mapZoom == 1;
        return sel && zoom && tapped(p.tap(B_B, Mode::Map), SC_ESCAPE);
    }());
}
