#include "keybinds.h"

#include <cstdio>

#include "sdl_min.h"

namespace {

struct Info {
    int def;
    const char* label;
    const char* setting;
    bool held;
};

// The steering assist toggle can't be P (the game's pause) or any letter (they're all taken):
// F4 it is.
const Info kInfo[KA_COUNT] = {
    {SC_W, "THROTTLE", "key_throttle", true},
    {SC_S, "BRAKE", "key_brake", true},
    {SC_A, "STEER LEFT", "key_steerleft", true},
    {SC_D, "STEER RIGHT", "key_steerright", true},
    {SC_SPACE, "HANDBRAKE", "key_handbrake", true},
    {SC_E, "GEAR UP", "key_gearup", false},
    {SC_Q, "GEAR DOWN", "key_geardown", false},
    {SC_ESCAPE, "MENU", "key_menu", false},
    {SC_F4, "STEERING ASSIST", "key_steeringassist", false},
    {SC_V, "WINCH", "key_winch", false},
    {SC_O, "TOW STRAP", "key_strap", false},
    {SC_8, "THROTTLE LIMIT", "key_throttlecap", false},
};
}  // namespace

int defaultKey(KeyAction a) { return kInfo[a].def; }
const char* actionLabel(KeyAction a) { return kInfo[a].label; }
const char* settingsName(KeyAction a) { return kInfo[a].setting; }
bool isHeldAction(KeyAction a) { return kInfo[a].held; }

bool bindable(int sc) {
    if (sc < 4 || sc >= 232) return false;  // (letters are 4.. ; 232 and up are media keys)
    if (sc == SC_ESCAPE || sc == SC_RETURN || sc == SC_KP_ENTER || sc == SC_TAB) return false;
    if (sc >= SC_RIGHT && sc <= SC_UP) return false;  // arrows: the second set of driving keys
    if (sc >= 224) return false;                      // ctrl, shift, alt, gui
    if (sc == SC_F12 || sc == 53) return false;       // the dev menu's key, and the dev keys / gameplay keys toggle (`)
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
        case 53: return "`";
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
    for (int a = 0; a < KA_COUNT; a++) code[a] = kInfo[a].def;
}

bool Keybinds::isDefault() const {
    for (int a = 0; a < KA_COUNT; a++)
        if (code[a] != kInfo[a].def) return false;
    return true;
}

int Keybinds::actionFor(int sc) const {
    for (int a = 0; a < KA_COUNT; a++)
        if (code[a] == sc) return a;
    return -1;
}

bool Keybinds::bind(KeyAction a, int sc) {
    if (a == KA_MENU && sc == SC_ESCAPE) {  // (back to plain Esc)
        code[a] = sc;
        return true;
    }
    if (!bindable(sc)) return false;
    const int other = actionFor(sc);
    if (other >= 0 && other != a) {
        if (!bindable(code[a])) return false;  // (the menu key is still Esc: pick a free key for it)
        code[other] = code[a];
    }
    code[a] = sc;
    return true;
}

int Keybinds::gameKey(int sc, bool fromPad) const {
    if (fromPad || sc == SC_ESCAPE) return sc;
    const int a = actionFor(sc);
    if (a >= 0) return isHeldAction((KeyAction)a) ? 0 : kInfo[a].def;
    for (int k = 0; k < KA_COUNT; k++)
        if (!kInfo[k].held && kInfo[k].def == sc && code[k] != sc) return 0;  // (moved to another key)
    return sc;
}

bool Keybinds::loadSetting(const std::string& name, int value) {
    for (int a = 0; a < KA_COUNT; a++) {
        if (name != kInfo[a].setting) continue;
        code[a] = value;
        return true;
    }
    return false;
}

void Keybinds::sanitize() {
    // (A settings file from before the throttle limit key, with that key bound to something else:
    // the new action moves aside rather than throw away every binding.)
    for (int a = 0; a < KA_COUNT; a++)
        if (a != KA_THROTTLE_CAP && code[a] == code[KA_THROTTLE_CAP]) {
            for (int f : {64, 65, 66}) {  // F7 F8 F9 (not the dev menu's F12)
                bool used = false;
                for (int b = 0; b < KA_COUNT; b++) used |= code[b] == f;
                if (!used) {
                    code[KA_THROTTLE_CAP] = f;
                    break;
                }
            }
            break;
        }
    bool bad = false;
    for (int a = 0; a < KA_COUNT && !bad; a++) {
        if (!bindable(code[a]) && !(a == KA_MENU && code[a] == SC_ESCAPE)) bad = true;
        for (int b = a + 1; b < KA_COUNT && !bad; b++)
            if (code[a] == code[b]) bad = true;
    }
    if (bad) resetDefaults();
}
