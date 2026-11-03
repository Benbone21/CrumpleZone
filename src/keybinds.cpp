#include "keybinds.h"

#include <cstdio>

#include "sdl_min.h"

namespace {

// One row per action, in the enum's order. THE place for defaults: `def` and `shift` are what the
// dev menu's EXPORT KEYBINDS AS DEFAULTS prints, to paste over the first two fields of every row;
// `pad` is the pad's default (PadInput, with PM_LB / PM_VIEW for a chord).
struct Info {
    int def;
    bool shift;
    const char* label;
    const char* setting;
    KeyGroup group;
    unsigned ctx;  // KeyContext bits: where it applies
    bool held;     // read from the keyboard state each frame
    bool gated;    // a dev tool: answers only in DEV KEYS mode (and clashes only with other dev tools)
    int pad;       // the default pad binding
};

constexpr unsigned D = KCTX_DRIVE, F = KCTX_FOOT, ANY = KCTX_ANY, SCN = KCTX_SCENARIO, MNU = KCTX_MENU, DLG = KCTX_DIALOG, FC = KCTX_FREECAM;
constexpr int GRAVE = 53;
// The pad's layers: a button alone, LB + it, VIEW + it (VIEW is the map when tapped alone), and LB + VIEW + it (the dev tools).
constexpr int LB(int in) { return PM_LB | in; }
constexpr int VW(int in) { return PM_VIEW | in; }
constexpr int LV(int in) { return PM_LB | PM_VIEW | in; }

// The steering assist toggle can't be P (the game's pause) or any letter (they're all taken):
// F4 it is.
const Info kInfo[KA_COUNT] = {
    // DRIVING
    {SC_W, false, "THROTTLE", "key_throttle", KG_DRIVING, ANY, true, false, PI_RT},
    {SC_S, false, "BRAKE", "key_brake", KG_DRIVING, ANY, true, false, PI_LT},
    {SC_A, false, "STEER LEFT", "key_steerleft", KG_DRIVING, ANY, true, false, PI_LS_LEFT},
    {SC_D, false, "STEER RIGHT", "key_steerright", KG_DRIVING, ANY, true, false, PI_LS_RIGHT},
    {SC_SPACE, false, "HANDBRAKE", "key_handbrake", KG_DRIVING, ANY, true, false, PI_A},
    {SC_P, false, "PARKING BRAKE", "key_parkbrake", KG_DRIVING, ANY, false, false, VW(PI_A)},
    {SC_7, false, "HORN (HOLD)", "key_horn", KG_DRIVING, ANY, true, false, LB(PI_R3)},
    {SC_E, false, "GEAR UP", "key_gearup", KG_DRIVING, D, false, false, PI_Y},
    {SC_Q, false, "GEAR DOWN", "key_geardown", KG_DRIVING, D, false, false, PI_X},
    {SC_M, true, "MANUAL / AUTOMATIC GEARBOX", "key_manual", KG_DRIVING, D, false, false, LB(PI_RB)},
    {SC_L, false, "TRANSFER CASE (4X4)", "key_transfer", KG_DRIVING, D, false, false, PI_DUP},
    {SC_L, true, "HEADLIGHTS", "key_lights", KG_DRIVING, ANY, false, false, PI_L3},
    {SC_F, false, "FRONT LOCKER", "key_lockfront", KG_DRIVING, D, false, false, PI_DLEFT},
    {SC_B, false, "REAR LOCKER", "key_lockrear", KG_DRIVING, D, false, false, PI_DRIGHT},
    {SC_Y, false, "FRONT SWAY BAR DISCONNECT", "key_sway", KG_DRIVING, D, false, false, PI_DDOWN},
    {SC_X, false, "TRACTION CONTROL", "key_traction", KG_DRIVING, D, false, false, LB(PI_X)},
    {SC_F4, false, "STEERING ASSIST", "key_steeringassist", KG_DRIVING, ANY, false, false, VW(PI_X)},
    {SC_9, false, "AIR DOWN (HOLD, STOPPED)", "key_airdown", KG_DRIVING, D, true, false, LB(PI_DLEFT)},
    {SC_0, false, "AIR UP (HOLD, STOPPED)", "key_airup", KG_DRIVING, D, true, false, LB(PI_DRIGHT)},
    {SC_8, false, "THROTTLE LIMIT", "key_throttlecap", KG_DRIVING, ANY, false, false, VW(PI_DUP)},
    {SC_G, false, "GARAGE", "key_garage", KG_DRIVING, D, false, false, VW(PI_B)},
    {SC_TAB, false, "NEXT VEHICLE", "key_nextvehicle", KG_DRIVING, ANY, false, false, VW(PI_RB)},
    // RECOVERY
    {SC_V, false, "WINCH", "key_winch", KG_RECOVERY, ANY, false, false, LB(PI_DUP)},
    {SC_Z, false, "WINCH IN (HOLD)", "key_winchin", KG_RECOVERY, ANY, true, false, LB(PI_RT)},
    {SC_Z, true, "WINCH OUT (HOLD)", "key_winchout", KG_RECOVERY, ANY, true, false, LB(PI_LT)},
    {SC_MINUS, false, "WINCH SPEED DOWN", "key_winchslower", KG_RECOVERY, ANY, false, false, LB(PI_RS_DOWN)},
    {SC_EQUALS, false, "WINCH SPEED UP", "key_winchfaster", KG_RECOVERY, ANY, false, false, LB(PI_RS_UP)},
    {SC_O, false, "TOW STRAP", "key_strap", KG_RECOVERY, ANY, false, false, LB(PI_DDOWN)},
    {SC_R, false, "REPAIR (TWICE: RESPAWN)", "key_repair", KG_RECOVERY, ANY, false, false, LB(PI_A)},
    {SC_T, false, "BACK ON ITS WHEELS", "key_flip", KG_RECOVERY, ANY, false, false, LB(PI_B)},
    {SC_U, false, "AUTOPILOT: START / STOP", "key_autopilot", KG_RECOVERY, ANY, false, false, LB(PI_L3)},
    {SC_U, true, "AUTOPILOT: WHERE TO", "key_autopilotpick", KG_RECOVERY, ANY, false, false, VW(PI_DDOWN)},
    {SC_J, true, "JOBS", "key_jobs", KG_RECOVERY, ANY, false, false, VW(PI_Y)},
    // ON FOOT
    {SC_J, false, "GET OUT", "key_getinout", KG_FOOT, ANY, false, false, LB(PI_Y)},
    {SC_E, false, "INTERACT (HOLD) / GET IN", "key_interact", KG_FOOT, F, true, false, PI_X},
    {SC_I, false, "TRUCK INVENTORY", "key_inventory", KG_FOOT, F, false, false, PI_Y},
    {SC_G, false, "PUT DOWN WHAT YOU CARRY", "key_putdown", KG_FOOT, F, false, false, PI_B},
    {SC_F, false, "FUEL CAN: POUR", "key_fuelcan", KG_FOOT, F, false, false, PI_DLEFT},
    {SC_LCTRL, false, "CROUCH", "key_crouch", KG_FOOT, F, true, false, PI_DDOWN},
    // CAMERA AND VIEW
    {SC_C, false, "CAMERA", "key_camera", KG_VIEW, ANY, false, false, PI_RB},
    {SC_M, false, "MAP", "key_map", KG_VIEW, ANY, false, false, PI_BACK},
    {SC_F10, false, "HIDE THE HUD (PHOTOS)", "key_photo", KG_VIEW, ANY, false, false, VW(PI_DRIGHT)},
    {SC_F9, false, "SHOW THE FULL HUD (HOLD)", "key_hudall", KG_VIEW, ANY, true, false, VW(PI_L3)},
    {SC_F3, false, "DEBUG INFO", "key_devfps", KG_VIEW, ANY, false, false, VW(PI_R3)},
    // GAME
    {SC_ESCAPE, false, "MENU", "key_menu", KG_GAME, ANY, false, false, PI_START},
    {SC_H, false, "CONTROLS OVERLAY", "key_help", KG_GAME, ANY, false, false, LB(PI_START)},
    {SC_F11, false, "FULLSCREEN", "key_fullscreen", KG_GAME, ANY, false, false, VW(PI_START)},
    {SC_RETURN, false, "CHAT", "key_chat", KG_GAME, ANY, false, false, VW(PI_DLEFT)},
    // DEV (the first four always work; the rest only in DEV KEYS mode)
    {SC_F12, false, "DEV MENU", "key_devmenu", KG_DEV, ANY, false, false, VW(PI_RS_DOWN)},
    {GRAVE, false, "DEV KEYS / GAMEPLAY KEYS", "key_devtoggle", KG_DEV, ANY, false, false, VW(PI_RS_LEFT)},
    {SC_F1, false, "PLAYTEST: BACK TO THE EDITOR", "key_playtestback", KG_DEV, ANY, false, false, VW(PI_RS_RIGHT)},
    {SC_F5, false, "LOAD THE NEW BUILD", "key_devreload", KG_DEV, ANY, false, false, VW(PI_RS_UP)},
    {SC_1, false, "SWAP TO VEHICLE 1", "key_devvehicle1", KG_DEV, ANY, false, true, LV(PI_A)},
    {SC_2, false, "SWAP TO VEHICLE 2", "key_devvehicle2", KG_DEV, ANY, false, true, LV(PI_B)},
    {SC_3, false, "SWAP TO VEHICLE 3", "key_devvehicle3", KG_DEV, ANY, false, true, LV(PI_X)},
    {SC_4, false, "SWAP TO VEHICLE 4", "key_devvehicle4", KG_DEV, ANY, false, true, LV(PI_Y)},
    {SC_5, false, "SWAP TO VEHICLE 5", "key_devvehicle5", KG_DEV, ANY, false, true, LV(PI_DLEFT)},
    {SC_6, false, "SWAP TO VEHICLE 6", "key_devvehicle6", KG_DEV, ANY, false, true, LV(PI_DRIGHT)},
    {SC_I, false, "SPAWN PARKED VEHICLE AHEAD", "key_devspawn", KG_DEV, D, false, true, LV(PI_DUP)},
    {SC_K, false, "SPAWN AI TRAFFIC", "key_devtraffic", KG_DEV, ANY, false, true, LV(PI_DDOWN)},
    {SC_DELETE, false, "REMOVE THIS VEHICLE", "key_devremove", KG_DEV, ANY, false, true, LV(PI_START)},
    {SC_N, false, "BEAM VIEW", "key_devbeam", KG_DEV, ANY, false, true, LV(PI_RB)},
    {SC_LEFTBRACKET, false, "SLOW MOTION: SLOWER", "key_devslower", KG_DEV, ANY, false, true, LV(PI_LT)},
    {SC_RIGHTBRACKET, false, "SLOW MOTION: FASTER", "key_devfaster", KG_DEV, ANY, false, true, LV(PI_RT)},
    {SC_F2, false, "LEVEL EDITOR HERE", "key_deveditor", KG_DEV, ANY, false, true, LV(PI_R3)},
    {SC_F8, false, "FREE CAM", "key_devfreecam", KG_DEV, ANY, false, true, LV(PI_L3)},
    {SC_J, false, "SCENARIO CHECKS LIST", "key_devscenario", KG_DEV, SCN, false, true, LV(PI_RS_UP)},
    // MENUS (Esc, Enter, the arrows and the pad's A, B and Start always work as well)
    {SC_W, false, "MENU: UP", "key_uiup", KG_MENU, MNU, false, false, PI_DUP},
    {SC_S, false, "MENU: DOWN", "key_uidown", KG_MENU, MNU, false, false, PI_DDOWN},
    {SC_A, false, "MENU: LEFT", "key_uileft", KG_MENU, MNU, false, false, PI_DLEFT},
    {SC_D, false, "MENU: RIGHT", "key_uiright", KG_MENU, MNU, false, false, PI_DRIGHT},
    {SC_SPACE, false, "MENU: OK", "key_uiok", KG_MENU, MNU, false, false, PI_A},
    {SC_BACKSPACE, false, "MENU: BACK", "key_uiback", KG_MENU, MNU, false, false, PI_B},
    {SC_Q, false, "MENU: PREVIOUS TAB", "key_uitabprev", KG_MENU, MNU, false, false, PI_LB},
    {SC_E, false, "MENU: NEXT TAB", "key_uitabnext", KG_MENU, MNU, false, false, PI_RB},
    {SC_T, false, "MENU: TRY ON / DELETE / CLEAR", "key_uisecond", KG_MENU, MNU, false, false, PI_X},
    {SC_R, false, "MENU: FACTORY PART / DEFAULT", "key_uifactory", KG_MENU, MNU, false, false, PI_Y},
    {SC_Z, false, "MENU: ZOOM IN", "key_uizoomin", KG_MENU, MNU, false, false, PI_RT},
    {SC_X, false, "MENU: ZOOM OUT", "key_uizoomout", KG_MENU, MNU, false, false, PI_LT},
    {SC_C, false, "GARAGE: SPEEDO RECALIBRATION", "key_uioption", KG_MENU, MNU, false, false, PI_BACK},
    {SC_1, false, "DIALOG: FIRST CHOICE", "key_uichoice1", KG_MENU, DLG, false, false, PI_X},
    {SC_2, false, "DIALOG: SECOND CHOICE", "key_uichoice2", KG_MENU, DLG, false, false, PI_Y},
    // FREE CAM
    {SC_G, false, "FREE CAM: PICK UP / SET DOWN", "key_fcgrab", KG_FREECAM, FC, false, false, PI_A},
    {SC_F, false, "FREE CAM: LET GO", "key_fcletgo", KG_FREECAM, FC, false, false, PI_B},
    {SC_R, false, "FREE CAM: TURN IT", "key_fcturn", KG_FREECAM, FC, false, false, PI_X},
    {SC_R, true, "FREE CAM: TURN IT BACK", "key_fcturnback", KG_FREECAM, FC, false, false, LB(PI_X)},
    {SC_B, false, "FREE CAM: SPAWN A VEHICLE", "key_fcspawn", KG_FREECAM, FC, false, false, PI_Y},
    {SC_V, false, "FREE CAM: DRIVE IT", "key_fcdrive", KG_FREECAM, FC, false, false, PI_RB},
    {SC_X, false, "FREE CAM: THROUGH THE GROUND", "key_fcthrough", KG_FREECAM, FC, false, false, PI_L3},
    {SC_MINUS, false, "FREE CAM: SLOWER", "key_fcslower", KG_FREECAM, FC, false, false, PI_DDOWN},
    {SC_EQUALS, false, "FREE CAM: FASTER", "key_fcfaster", KG_FREECAM, FC, false, false, PI_DUP},
};
static_assert(sizeof kInfo / sizeof kInfo[0] == KA_COUNT, "one kInfo row per KeyAction");

// Two bindings clash when they are the same key press in a context both apply in, on the same side of the
// DEV KEYS toggle.
bool clashes(KeyAction a, int codeA, bool shiftA, KeyAction b, int codeB, bool shiftB) {
    return a != b && codeA != 0 && codeA == codeB && shiftA == shiftB && (kInfo[a].ctx & kInfo[b].ctx) != 0 && kInfo[a].gated == kInfo[b].gated;
}
bool padClashes(KeyAction a, int bindA, KeyAction b, int bindB) {
    return a != b && bindA != 0 && bindA == bindB && (kInfo[a].ctx & kInfo[b].ctx) != 0 && kInfo[a].gated == kInfo[b].gated;
}

int popcount(unsigned v) {
    int n = 0;
    for (; v; v >>= 1) n += v & 1;
    return n;
}
}  // namespace

const char* groupLabel(KeyGroup g) {
    static const char* const n[KG_COUNT] = {"DRIVING", "RECOVERY", "ON FOOT", "CAMERA AND VIEW", "GAME", "MENUS", "FREE CAM", "DEV"};
    return g >= 0 && g < KG_COUNT ? n[g] : "";
}

int defaultKey(KeyAction a) { return kInfo[a].def; }
bool defaultShift(KeyAction a) { return kInfo[a].shift; }
int defaultPad(KeyAction a) { return kInfo[a].pad; }
const char* actionLabel(KeyAction a) { return kInfo[a].label; }
const char* settingsName(KeyAction a) { return kInfo[a].setting; }
std::string padSettingsName(KeyAction a) { return "pad_" + std::string(kInfo[a].setting + 4); }
KeyGroup actionGroup(KeyAction a) { return kInfo[a].group; }
unsigned actionContexts(KeyAction a) { return kInfo[a].ctx; }
bool isHeldAction(KeyAction a) { return kInfo[a].held; }
bool isDevAction(KeyAction a) { return kInfo[a].gated; }
bool isUiAction(KeyAction a) { return (kInfo[a].ctx & KCTX_UI) != 0; }

int uiCanon(KeyAction a) {
    switch (a) {
        case KA_UI_UP: return SC_UP;
        case KA_UI_DOWN: return SC_DOWN;
        case KA_UI_LEFT: return SC_LEFT;
        case KA_UI_RIGHT: return SC_RIGHT;
        case KA_UI_CONFIRM: return SC_RETURN;
        case KA_UI_BACK: return SC_ESCAPE;
        case KA_UI_TAB_PREV: return UK_TAB_PREV;
        case KA_UI_TAB_NEXT: return UK_TAB_NEXT;
        case KA_UI_SECOND: return UK_SECOND;
        case KA_UI_FACTORY: return UK_FACTORY;
        case KA_UI_ZOOM_IN: return UK_ZOOM_IN;
        case KA_UI_ZOOM_OUT: return UK_ZOOM_OUT;
        case KA_UI_OPTION: return UK_OPTION;
        case KA_UI_CHOICE1: return UK_CHOICE1;
        case KA_UI_CHOICE2: return UK_CHOICE2;
        default: return 0;
    }
}

bool bindable(int sc) {
    if (sc < 4 || sc >= 232) return false;  // (letters are 4.. ; 232 and up are media keys)
    if (sc == SC_ESCAPE || sc == SC_KP_ENTER) return false;
    if (sc >= SC_RIGHT && sc <= SC_UP) return false;  // arrows: the second set of driving keys
    if (sc >= 224 && sc != SC_LCTRL) return false;    // shift, alt, gui, right ctrl (left Ctrl: the crouch's default)
    return true;
}

bool bindableFor(KeyAction a, int sc) { return bindable(sc) && !(isUiAction(a) && sc == SC_RETURN); }

bool padBindable(KeyAction a, int b) {
    const int in = padInput(b), m = padMods(b);
    if ((b & ~(0xff | PM_MASK)) != 0 || in <= PI_NONE || in >= PI_COUNT || in == PI_GUIDE) return false;
    if (a == KA_MENU) return b == PI_START;  // (Start is the menu, always: like Esc)
    if (isUiAction(a)) {  // the menus: no layers; A and B are always OK and back
        if (m || in == PI_START) return false;
        if (in == PI_A) return a == KA_UI_CONFIRM;
        if (in == PI_B) return a == KA_UI_BACK;
        return true;
    }
    if (in == PI_START && m == 0) return false;  // (Start alone is the menu)
    if (in == PI_LB) return false;               // (LB is the second layer, never a button of its own)
    if (in == PI_BACK && m != 0) return false;   // (VIEW is the third: a button only alone)
    return true;
}

std::string padInputName(int in, bool ps) {
    static const char* const xbox[PI_COUNT] = {"NONE", "A", "B", "X", "Y", "VIEW", "HOME", "MENU", "L3", "R3", "LB", "RB", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT",
                                               "D-PAD RIGHT", "LT", "RT", "L-STICK LEFT", "L-STICK RIGHT", "L-STICK UP", "L-STICK DOWN", "R-STICK LEFT",
                                               "R-STICK RIGHT", "R-STICK UP", "R-STICK DOWN"};
    static const char* const play[PI_COUNT] = {"NONE", "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "TOUCHPAD", "PS", "OPTIONS", "L3", "R3", "L1", "R1", "D-PAD UP",
                                               "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "L2", "R2", "L-STICK LEFT", "L-STICK RIGHT", "L-STICK UP",
                                               "L-STICK DOWN", "R-STICK LEFT", "R-STICK RIGHT", "R-STICK UP", "R-STICK DOWN"};
    if (in < 0 || in >= PI_COUNT) return "NONE";
    return (ps ? play : xbox)[in];
}

std::string padBindName(int b, bool ps) {
    if (padInput(b) == PI_NONE) return "NONE";
    std::string s;
    if (b & PM_LB) s += padInputName(PI_LB, ps) + " + ";
    if (b & PM_VIEW) s += padInputName(PI_BACK, ps) + " + ";
    return s + padInputName(padInput(b), ps);
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

const Keybinds& defaultKeybinds() {
    static const Keybinds kb;
    return kb;
}

void Keybinds::resetDefaults() {
    for (int a = 0; a < KA_COUNT; a++) code[a] = kInfo[a].def, shift[a] = kInfo[a].shift, pad[a] = kInfo[a].pad, loaded[a] = padLoaded[a] = false;
}

void Keybinds::resetAction(KeyAction a) {
    if (!isKeyDefault(a)) {
        if (kInfo[a].def == 0) unbind(a);
        else bind(a, kInfo[a].def, kInfo[a].shift);
    }
    if (!isPadDefault(a)) bindPad(a, kInfo[a].pad);
}

bool Keybinds::isKeyDefault(KeyAction a) const { return code[a] == kInfo[a].def && shift[a] == kInfo[a].shift; }
bool Keybinds::isPadDefault(KeyAction a) const { return pad[a] == kInfo[a].pad; }
bool Keybinds::isDefault(KeyAction a) const { return isKeyDefault(a) && isPadDefault(a); }

bool Keybinds::isDefault() const {
    for (int a = 0; a < KA_COUNT; a++)
        if (!isDefault((KeyAction)a)) return false;
    return true;
}

std::string Keybinds::name(KeyAction a) const { return (shift[a] && code[a] ? "SHIFT+" : "") + keyName(code[a]); }
std::string Keybinds::padName(KeyAction a, bool ps) const { return padBindName(pad[a], ps); }

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

int Keybinds::clashWith(KeyAction a, int sc, bool withShift) const {
    for (int b = 0; b < KA_COUNT; b++)
        if (clashes(a, sc, withShift, (KeyAction)b, code[b], shift[b])) return b;
    return -1;
}

int Keybinds::padClashWith(KeyAction a, int bind) const {
    for (int b = 0; b < KA_COUNT; b++)
        if (padClashes(a, bind, (KeyAction)b, pad[b])) return b;
    return -1;
}

bool Keybinds::bind(KeyAction a, int sc, bool withShift) {
    if (a == KA_MENU && sc == SC_ESCAPE) {  // (back to plain Esc)
        code[a] = sc, shift[a] = false;
        return true;
    }
    if (!bindableFor(a, sc)) return false;
    if (a == KA_MENU) withShift = false;
    const int oldCode = code[a];
    const bool oldShift = shift[a];
    bool first = true;
    for (int b = 0; b < KA_COUNT; b++) {
        if (!clashes(a, sc, withShift, (KeyAction)b, code[b], shift[b])) continue;
        if (b == KA_MENU && (oldShift || !bindable(oldCode))) return false;  // (the menu key is still Esc: pick a free key for it)
    }
    // (The one that had it takes this one's old key, unless it can't (a menu key can't take Enter) or a third action has that key
    // where it applies: then it is left without one.)
    auto freeFor = [&](int b) {
        if (oldCode == 0) return true;
        if (!bindableFor((KeyAction)b, oldCode)) return false;
        for (int c = 0; c < KA_COUNT; c++)
            if (c != a && clashes((KeyAction)b, oldCode, oldShift, (KeyAction)c, code[c], shift[c])) return false;
        return true;
    };
    for (int b = 0; b < KA_COUNT; b++) {
        if (!clashes(a, sc, withShift, (KeyAction)b, code[b], shift[b])) continue;
        if (first && freeFor(b)) code[b] = oldCode, shift[b] = oldShift;
        else code[b] = 0, shift[b] = false;
        first = false;
    }
    code[a] = sc;
    shift[a] = withShift;
    return true;
}

bool Keybinds::bindPad(KeyAction a, int b) {
    if (!padBindable(a, b)) return false;
    const int old = pad[a];
    bool first = true;
    // Whatever has it takes this one's old binding, if it may have it and no third action has that where it applies; otherwise none.
    auto freeFor = [&](int c) {
        if (!padBindable((KeyAction)c, old)) return false;
        for (int d = 0; d < KA_COUNT; d++)
            if (d != a && padClashes((KeyAction)c, old, (KeyAction)d, pad[d])) return false;
        return true;
    };
    for (int c = 0; c < KA_COUNT; c++) {
        if (!padClashes(a, b, (KeyAction)c, pad[c])) continue;
        pad[c] = first && freeFor(c) ? old : 0;
        first = false;
    }
    pad[a] = b;
    return true;
}

int Keybinds::padActionFor(int input, int mods, unsigned contexts, bool devKeys) const {
    int best = -1, bestScore = -1;
    const int exact = input | mods;
    for (int a = 0; a < KA_COUNT; a++) {
        if (pad[a] == 0 || !(kInfo[a].ctx & contexts) || (kInfo[a].gated && !devKeys)) continue;
        int score;
        if (pad[a] == exact) score = 100;
        else if (mods != 0 && pad[a] == input) score = 0;  // (a plain binding, with a modifier incidentally down)
        else continue;
        score += (kInfo[a].gated ? 10 : 0) + (8 - popcount(kInfo[a].ctx));
        if (score > bestScore) best = a, bestScore = score;
    }
    return best;
}

bool Keybinds::padChord(int input, int mods, unsigned contexts) const {
    if (mods == 0) return false;
    for (int a = 0; a < KA_COUNT; a++)
        if (pad[a] == (input | mods) && (kInfo[a].ctx & contexts)) return true;
    return false;
}

float Keybinds::padHeld(KeyAction a, const float* values, int mods) const {
    const int b = pad[a], in = padInput(b), m = padMods(b);
    if (in <= PI_NONE || in >= PI_COUNT) return 0;
    const float v = values[in];
    if (v <= 0) return 0;
    if (m) return mods == m ? v : 0;
    if (mods == 0) return v;
    return padChord(in, mods, kInfo[a].ctx) ? 0 : v;
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

int Keybinds::uiKey(int sc, bool withShift, unsigned contexts) const {
    if (sc == SC_ESCAPE || sc == SC_RETURN || sc == SC_KP_ENTER || (sc >= SC_RIGHT && sc <= SC_UP)) return sc;  // (always theirs)
    if (contexts & KCTX_DIALOG) {
        const int a = actionFor(sc, withShift, KCTX_DIALOG, false);
        if (a >= 0) return uiCanon((KeyAction)a);
    }
    if (contexts & KCTX_MENU) {
        const int a = actionFor(sc, withShift, KCTX_MENU, false);
        if (a >= 0) return uiCanon((KeyAction)a);
    }
    return sc;
}

bool Keybinds::loadSetting(const std::string& name, int value) {
    if (name == "key_pause") {  // (the old pause key: gone, Esc's menu pauses; an old file's P there moved the parking brake aside)
        legacyPauseOnP = value == SC_P;
        return true;
    }
    const bool padLine = name.rfind("pad_", 0) == 0;
    if (!padLine && name.rfind("key_", 0) != 0) return false;
    for (int a = 0; a < KA_COUNT; a++) {
        if (name.compare(4, std::string::npos, kInfo[a].setting + 4) != 0) continue;
        if (padLine) {
            pad[a] = value;
            padLoaded[a] = true;
        } else {
            code[a] = value & 4095;
            shift[a] = (value & 4096) != 0;
            loaded[a] = true;
        }
        return true;
    }
    return false;
}

void Keybinds::sanitize() {
    auto usable = [&](int a) { return (code[a] == 0 && loaded[a]) || bindableFor((KeyAction)a, code[a]) || (a == KA_MENU && code[a] == SC_ESCAPE); };
    // A file saved before the parking brake had P (pause was on P): the brake went aside to an F key. It comes back to P.
    if (legacyPauseOnP && (!loaded[KA_PARK_BRAKE] || (code[KA_PARK_BRAKE] >= 64 && code[KA_PARK_BRAKE] <= 66 && !shift[KA_PARK_BRAKE]))) {
        code[KA_PARK_BRAKE] = kInfo[KA_PARK_BRAKE].def, shift[KA_PARK_BRAKE] = kInfo[KA_PARK_BRAKE].shift;
        loaded[KA_PARK_BRAKE] = true;
    }
    legacyPauseOnP = false;
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
            for (int b = 0; b < KA_COUNT; b++) used |= code[b] == f && b != a && kInfo[b].gated == kInfo[a].gated;  // (a dev key on it, FREE CAM's F8, answers only in DEV KEYS mode)
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
    if (bad)
        for (int a = 0; a < KA_COUNT; a++) code[a] = kInfo[a].def, shift[a] = kInfo[a].shift;

    // The pad, the same way. A file from before the pad bindings has none: the defaults. An action added since whose
    // default a saved binding took is left without one (there are no spare buttons to move it to).
    auto padUsable = [&](int a) { return (pad[a] == 0 && padLoaded[a]) || padBindable((KeyAction)a, pad[a]); };
    for (int a = 0; a < KA_COUNT; a++) {
        if (!padUsable(a)) pad[a] = kInfo[a].pad, padLoaded[a] = false;
        if (padLoaded[a]) continue;
        for (int b = 0; b < KA_COUNT; b++)
            if (padLoaded[b] && padClashes((KeyAction)a, pad[a], (KeyAction)b, pad[b])) {
                pad[a] = 0;
                break;
            }
    }
    for (int a = 0; a < KA_COUNT; a++)
        for (int b = 0; b < a; b++)
            if (padClashes((KeyAction)a, pad[a], (KeyAction)b, pad[b])) {
                pad[a] = padClashWith((KeyAction)a, kInfo[a].pad) < 0 ? kInfo[a].pad : 0;
                break;
            }
    bool padBad = false;
    for (int a = 0; a < KA_COUNT && !padBad; a++)
        for (int b = a + 1; b < KA_COUNT && !padBad; b++) padBad = padClashes((KeyAction)a, pad[a], (KeyAction)b, pad[b]);
    if (padBad)
        for (int a = 0; a < KA_COUNT; a++) pad[a] = kInfo[a].pad;
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
