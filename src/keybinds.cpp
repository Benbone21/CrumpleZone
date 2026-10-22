#include "keybinds.h"

#include <cstdio>

#include "sdl_min.h"

namespace {

// One row per action, in the enum's order. THE place for defaults: `def` and `shift` are what the
// dev menu's EXPORT KEYBINDS AS DEFAULTS prints, to paste over the first two fields of every row.
struct Info {
    int def;
    bool shift;
    const char* label;
    const char* setting;
    KeyGroup group;
    unsigned ctx;  // KeyContext bits: where it applies
    bool held;     // read from the keyboard state each frame
    bool gated;    // a dev tool: answers only in DEV KEYS mode (and clashes only with other dev tools)
};

constexpr unsigned D = KCTX_DRIVE, F = KCTX_FOOT, ANY = KCTX_ANY, SCN = KCTX_SCENARIO;
constexpr int GRAVE = 53;

// The steering assist toggle can't be P (the game's pause) or any letter (they're all taken):
// F4 it is.
const Info kInfo[KA_COUNT] = {
    // DRIVING
    {SC_W, false, "THROTTLE", "key_throttle", KG_DRIVING, ANY, true, false},
    {SC_S, false, "BRAKE", "key_brake", KG_DRIVING, ANY, true, false},
    {SC_A, false, "STEER LEFT", "key_steerleft", KG_DRIVING, ANY, true, false},
    {SC_D, false, "STEER RIGHT", "key_steerright", KG_DRIVING, ANY, true, false},
    {SC_SPACE, false, "HANDBRAKE", "key_handbrake", KG_DRIVING, ANY, true, false},
    {SC_P, false, "PARKING BRAKE", "key_parkbrake", KG_DRIVING, ANY, false, false},
    {SC_7, false, "HORN (HOLD)", "key_horn", KG_DRIVING, ANY, true, false},
    {SC_E, false, "GEAR UP", "key_gearup", KG_DRIVING, D, false, false},
    {SC_Q, false, "GEAR DOWN", "key_geardown", KG_DRIVING, D, false, false},
    {SC_M, true, "MANUAL / AUTOMATIC GEARBOX", "key_manual", KG_DRIVING, D, false, false},
    {SC_L, false, "TRANSFER CASE (4X4)", "key_transfer", KG_DRIVING, D, false, false},
    {SC_L, true, "HEADLIGHTS", "key_lights", KG_DRIVING, ANY, false, false},
    {SC_F, false, "FRONT LOCKER", "key_lockfront", KG_DRIVING, D, false, false},
    {SC_B, false, "REAR LOCKER", "key_lockrear", KG_DRIVING, D, false, false},
    {SC_Y, false, "FRONT SWAY BAR DISCONNECT", "key_sway", KG_DRIVING, D, false, false},
    {SC_X, false, "TRACTION CONTROL", "key_traction", KG_DRIVING, D, false, false},
    {SC_F4, false, "STEERING ASSIST", "key_steeringassist", KG_DRIVING, ANY, false, false},
    {SC_9, false, "AIR DOWN (HOLD, STOPPED)", "key_airdown", KG_DRIVING, D, true, false},
    {SC_0, false, "AIR UP (HOLD, STOPPED)", "key_airup", KG_DRIVING, D, true, false},
    {SC_8, false, "THROTTLE LIMIT", "key_throttlecap", KG_DRIVING, ANY, false, false},
    {SC_G, false, "GARAGE", "key_garage", KG_DRIVING, D, false, false},
    {SC_TAB, false, "NEXT VEHICLE", "key_nextvehicle", KG_DRIVING, ANY, false, false},
    // RECOVERY
    {SC_V, false, "WINCH", "key_winch", KG_RECOVERY, ANY, false, false},
    {SC_Z, false, "WINCH IN (HOLD)", "key_winchin", KG_RECOVERY, ANY, true, false},
    {SC_Z, true, "WINCH OUT (HOLD)", "key_winchout", KG_RECOVERY, ANY, true, false},
    {SC_MINUS, false, "WINCH SPEED DOWN", "key_winchslower", KG_RECOVERY, ANY, false, false},
    {SC_EQUALS, false, "WINCH SPEED UP", "key_winchfaster", KG_RECOVERY, ANY, false, false},
    {SC_O, false, "TOW STRAP", "key_strap", KG_RECOVERY, ANY, false, false},
    {SC_R, false, "REPAIR (TWICE: RESPAWN)", "key_repair", KG_RECOVERY, ANY, false, false},
    {SC_T, false, "BACK ON ITS WHEELS", "key_flip", KG_RECOVERY, ANY, false, false},
    {SC_U, false, "AUTOPILOT", "key_autopilot", KG_RECOVERY, ANY, false, false},
    // ON FOOT
    {SC_J, false, "GET OUT / IN", "key_getinout", KG_FOOT, ANY, false, false},
    {SC_E, false, "INTERACT (HOLD)", "key_interact", KG_FOOT, F, true, false},
    {SC_I, false, "TRUCK INVENTORY", "key_inventory", KG_FOOT, F, false, false},
    {SC_G, false, "PUT DOWN WHAT YOU CARRY", "key_putdown", KG_FOOT, F, false, false},
    {SC_F, false, "FUEL CAN: POUR", "key_fuelcan", KG_FOOT, F, false, false},
    {SC_LCTRL, false, "CROUCH", "key_crouch", KG_FOOT, F, true, false},
    // CAMERA AND VIEW
    {SC_C, false, "CAMERA", "key_camera", KG_VIEW, ANY, false, false},
    {SC_M, false, "MAP", "key_map", KG_VIEW, ANY, false, false},
    {SC_F10, false, "HIDE THE HUD (PHOTOS)", "key_photo", KG_VIEW, ANY, false, false},
    {SC_F9, false, "SHOW THE FULL HUD (HOLD)", "key_hudall", KG_VIEW, ANY, true, false},
    {SC_F3, false, "DEBUG INFO", "key_devfps", KG_VIEW, ANY, false, false},
    // GAME
    {SC_ESCAPE, false, "MENU", "key_menu", KG_GAME, ANY, false, false},
    {SC_F6, false, "PAUSE", "key_pause", KG_GAME, ANY, false, false},
    {SC_H, false, "CONTROLS OVERLAY", "key_help", KG_GAME, ANY, false, false},
    {SC_F11, false, "FULLSCREEN", "key_fullscreen", KG_GAME, ANY, false, false},
    {SC_RETURN, false, "CHAT", "key_chat", KG_GAME, ANY, false, false},
    // DEV (the first four always work; the rest only in DEV KEYS mode)
    {SC_F12, false, "DEV MENU", "key_devmenu", KG_DEV, ANY, false, false},
    {GRAVE, false, "DEV KEYS / GAMEPLAY KEYS", "key_devtoggle", KG_DEV, ANY, false, false},
    {SC_F1, false, "PLAYTEST: BACK TO THE EDITOR", "key_playtestback", KG_DEV, ANY, false, false},
    {SC_F5, false, "LOAD THE NEW BUILD", "key_devreload", KG_DEV, ANY, false, false},
    {SC_1, false, "SWAP TO VEHICLE 1", "key_devvehicle1", KG_DEV, ANY, false, true},
    {SC_2, false, "SWAP TO VEHICLE 2", "key_devvehicle2", KG_DEV, ANY, false, true},
    {SC_3, false, "SWAP TO VEHICLE 3", "key_devvehicle3", KG_DEV, ANY, false, true},
    {SC_4, false, "SWAP TO VEHICLE 4", "key_devvehicle4", KG_DEV, ANY, false, true},
    {SC_5, false, "SWAP TO VEHICLE 5", "key_devvehicle5", KG_DEV, ANY, false, true},
    {SC_6, false, "SWAP TO VEHICLE 6", "key_devvehicle6", KG_DEV, ANY, false, true},
    {SC_I, false, "SPAWN PARKED VEHICLE AHEAD", "key_devspawn", KG_DEV, D, false, true},
    {SC_K, false, "SPAWN AI TRAFFIC", "key_devtraffic", KG_DEV, ANY, false, true},
    {SC_DELETE, false, "REMOVE THIS VEHICLE", "key_devremove", KG_DEV, ANY, false, true},
    {SC_N, false, "BEAM VIEW", "key_devbeam", KG_DEV, ANY, false, true},
    {SC_LEFTBRACKET, false, "SLOW MOTION: SLOWER", "key_devslower", KG_DEV, ANY, false, true},
    {SC_RIGHTBRACKET, false, "SLOW MOTION: FASTER", "key_devfaster", KG_DEV, ANY, false, true},
    {SC_F2, false, "LEVEL EDITOR HERE", "key_deveditor", KG_DEV, ANY, false, true},
    {SC_J, false, "SCENARIO CHECKS LIST", "key_devscenario", KG_DEV, SCN, false, true},
};
static_assert(sizeof kInfo / sizeof kInfo[0] == KA_COUNT, "one kInfo row per KeyAction");

// Two bindings clash when they are the same key press in a context both apply in, on the same side of the
// DEV KEYS toggle.
bool clashes(KeyAction a, int codeA, bool shiftA, KeyAction b, int codeB, bool shiftB) {
    return a != b && codeA != 0 && codeA == codeB && shiftA == shiftB && (kInfo[a].ctx & kInfo[b].ctx) != 0 && kInfo[a].gated == kInfo[b].gated;
}

int popcount(unsigned v) {
    int n = 0;
    for (; v; v >>= 1) n += v & 1;
    return n;
}
}  // namespace

const char* groupLabel(KeyGroup g) {
    static const char* const n[KG_COUNT] = {"DRIVING", "RECOVERY", "ON FOOT", "CAMERA AND VIEW", "GAME", "DEV"};
    return g >= 0 && g < KG_COUNT ? n[g] : "";
}

int defaultKey(KeyAction a) { return kInfo[a].def; }
bool defaultShift(KeyAction a) { return kInfo[a].shift; }
const char* actionLabel(KeyAction a) { return kInfo[a].label; }
const char* settingsName(KeyAction a) { return kInfo[a].setting; }
KeyGroup actionGroup(KeyAction a) { return kInfo[a].group; }
unsigned actionContexts(KeyAction a) { return kInfo[a].ctx; }
bool isHeldAction(KeyAction a) { return kInfo[a].held; }
bool isDevAction(KeyAction a) { return kInfo[a].gated; }

bool bindable(int sc) {
    if (sc < 4 || sc >= 232) return false;  // (letters are 4.. ; 232 and up are media keys)
    if (sc == SC_ESCAPE || sc == SC_KP_ENTER) return false;
    if (sc >= SC_RIGHT && sc <= SC_UP) return false;  // arrows: the second set of driving keys
    if (sc >= 224 && sc != SC_LCTRL) return false;    // shift, alt, gui, right ctrl (left Ctrl: the crouch's default)
    return true;
}

std::string keyName(int sc) {
    static const char* const f[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    if (sc >= SC_A && sc <= SC_A + 25) return std::string(1, (char)('A' + (sc - SC_A)));
    if (sc >= SC_1 && sc <= SC_1 + 8) return std::string(1, (char)('1' + (sc - SC_1)));
    if (sc == SC_0) return "0";
    if (sc >= 58 && sc <= 69) return f[sc - 58];
    switch (sc) {
        case SC_SPACE: return "SPACE";
        case SC_ESCAPE: return "ESC";
        case SC_RETURN: return "ENTER";
        case SC_TAB: return "TAB";
        case SC_BACKSPACE: return "BACKSPACE";
        case SC_MINUS: return "-";
        case SC_EQUALS: return "=";
        case SC_LEFTBRACKET: return "[";
        case SC_RIGHTBRACKET: return "]";
        case 49: return "\\";
        case 51: return ";";
        case 52: return "'";
        case 53: return "GRAVE";  // (the key under Esc: the font has no backtick)
        case 54: return ",";
        case 55: return ".";
        case 56: return "/";
        case 57: return "CAPS LOCK";
        case 73: return "INSERT";
        case 74: return "HOME";
        case 75: return "PAGE UP";
        case SC_DELETE: return "DELETE";
        case 77: return "END";
        case 78: return "PAGE DOWN";
        case SC_RIGHT: return "RIGHT";
        case SC_LEFT: return "LEFT";
        case SC_DOWN: return "DOWN";
        case SC_UP: return "UP";
        case SC_LCTRL: return "LEFT CTRL";
        case SC_LSHIFT: return "LEFT SHIFT";
        case SC_RSHIFT: return "RIGHT SHIFT";
        case 0: return "NONE";
    }
    if (sc >= 89 && sc <= 97) return "KEYPAD " + std::string(1, (char)('1' + (sc - 89)));
    if (sc == 98) return "KEYPAD 0";
    char buf[16];
    std::snprintf(buf, sizeof buf, "KEY %d", sc);
    return buf;
}

void Keybinds::resetDefaults() {
    for (int a = 0; a < KA_COUNT; a++) code[a] = kInfo[a].def, shift[a] = kInfo[a].shift, loaded[a] = false;
}

bool Keybinds::isDefault(KeyAction a) const { return code[a] == kInfo[a].def && shift[a] == kInfo[a].shift; }

bool Keybinds::isDefault() const {
    for (int a = 0; a < KA_COUNT; a++)
        if (!isDefault((KeyAction)a)) return false;
    return true;
}

std::string Keybinds::name(KeyAction a) const { return (shift[a] && code[a] ? "SHIFT+" : "") + keyName(code[a]); }

bool Keybinds::matches(KeyAction a, int sc, bool withShift) const {
    return code[a] == sc && code[a] != 0 && (shift[a] ? withShift : true);
}

int Keybinds::actionFor(int sc, bool withShift, unsigned contexts, bool devKeys) const {
    int best = -1, bestScore = -1;
    for (int a = 0; a < KA_COUNT; a++) {
        if (!matches((KeyAction)a, sc, withShift) || !(kInfo[a].ctx & contexts) || (kInfo[a].gated && !devKeys)) continue;
        // An exact Shift match first; then a dev tool over a gameplay action; then the narrowest context.
        const int score = (shift[a] == withShift ? 100 : 0) + (kInfo[a].gated ? 10 : 0) + (8 - popcount(kInfo[a].ctx));
        if (score > bestScore) best = a, bestScore = score;
    }
    return best;
}

bool Keybinds::held(KeyAction a, const uint8_t* keys, bool shiftHeld) const {
    if (code[a] <= 0 || code[a] >= 512 || !keys[code[a]]) return false;
    if (shift[a]) return shiftHeld;
    if (!shiftHeld) return true;
    for (int b = 0; b < KA_COUNT; b++)  // (Shift is down: a Shift combo on this very key takes the press)
        if (b != a && code[b] == code[a] && shift[b] && (kInfo[b].ctx & kInfo[a].ctx)) return false;
    return true;
}

bool Keybinds::bind(KeyAction a, int sc, bool withShift) {
    if (a == KA_MENU && sc == SC_ESCAPE) {  // (back to plain Esc)
        code[a] = sc, shift[a] = false;
        return true;
    }
    if (!bindable(sc)) return false;
    if (a == KA_MENU) withShift = false;
    const int oldCode = code[a];
    const bool oldShift = shift[a];
    bool first = true;
    for (int b = 0; b < KA_COUNT; b++) {
        if (!clashes(a, sc, withShift, (KeyAction)b, code[b], shift[b])) continue;
        if (b == KA_MENU && (oldShift || !bindable(oldCode))) return false;  // (the menu key is still Esc: pick a free key for it)
    }
    for (int b = 0; b < KA_COUNT; b++) {
        if (!clashes(a, sc, withShift, (KeyAction)b, code[b], shift[b])) continue;
        if (first) code[b] = oldCode, shift[b] = oldShift, first = false;
        else code[b] = 0, shift[b] = false;
    }
    code[a] = sc;
    shift[a] = withShift;
    return true;
}

Keybinds::Press Keybinds::resolve(int sc, bool withShift, bool fromPad, unsigned contexts, bool devKeys) const {
    Press p;
    if (fromPad) {
        p.key = sc;
        return p;
    }
    if (sc == SC_ESCAPE) {
        p.action = KA_MENU, p.key = SC_ESCAPE;
        return p;
    }
    const int a = actionFor(sc, withShift, contexts, devKeys);
    if (a >= 0) {
        p.action = a;
        p.key = kInfo[a].held ? 0 : kInfo[a].def;
        p.shift = kInfo[a].shift;
        return p;
    }
    if (sc == SC_RETURN || sc == SC_KP_ENTER || sc == SC_BACKSPACE) p.key = sc;  // (the winch picker's own keys)
    return p;
}

bool Keybinds::loadSetting(const std::string& name, int value) {
    for (int a = 0; a < KA_COUNT; a++) {
        if (name != kInfo[a].setting) continue;
        code[a] = value & 4095;
        shift[a] = (value & 4096) != 0;
        loaded[a] = true;
        return true;
    }
    return false;
}

void Keybinds::sanitize() {
    auto usable = [&](int a) { return (code[a] == 0 && loaded[a]) || bindable(code[a]) || (a == KA_MENU && code[a] == SC_ESCAPE); };
    // An action added since the file was saved (its key is a default) whose key a saved binding took moves aside
    // to a free F key, rather than throw away every binding.
    for (int a = 0; a < KA_COUNT; a++) {
        if (loaded[a] || !usable(a)) continue;
        bool clash = false;
        for (int b = 0; b < KA_COUNT; b++) clash |= clashes((KeyAction)a, code[a], shift[a], (KeyAction)b, code[b], shift[b]) && loaded[b] && usable(b);
        if (!clash) continue;
        code[a] = 0, shift[a] = false;
        for (int f : {64, 65, 66}) {  // F7 F8 F9 (not the dev menu's F12)
            bool used = false;
            for (int b = 0; b < KA_COUNT; b++) used |= code[b] == f && b != a;
            if (!used) {
                code[a] = f;
                break;
            }
        }
    }
    // A saved binding that can't be used, or clashes: that action goes back to its default.
    for (int a = 0; a < KA_COUNT; a++) {
        if (!usable(a) || (code[a] == 0 && !loaded[a])) {
            code[a] = kInfo[a].def, shift[a] = kInfo[a].shift;
            continue;
        }
        if (a == KA_MENU) shift[a] = false;
        for (int b = 0; b < a; b++)
            if (clashes((KeyAction)a, code[a], shift[a], (KeyAction)b, code[b], shift[b])) {
                code[a] = kInfo[a].def, shift[a] = kInfo[a].shift;
                break;
            }
    }
    bool bad = false;  // (defaults that still clash with what was kept: all of them, as a last resort)
    for (int a = 0; a < KA_COUNT && !bad; a++)
        for (int b = a + 1; b < KA_COUNT && !bad; b++) bad = clashes((KeyAction)a, code[a], shift[a], (KeyAction)b, code[b], shift[b]);
    if (bad) resetDefaults();
}

std::string Keybinds::exportDefaults() const {
    std::string out = "// Paste over the first two fields of each kInfo row in src/keybinds.cpp (same order). {key, shift}\n";
    char buf[160];
    for (int a = 0; a < KA_COUNT; a++) {
        std::snprintf(buf, sizeof buf, "{%d, %s},  // %s: %s\n", code[a], shift[a] ? "true" : "false", kInfo[a].setting, name((KeyAction)a).c_str());
        out += buf;
    }
    return out;
}
