#include "menu.h"
#include "sdl_min.h"
#include <cstdio>
#include <string>

namespace {

std::string keyLabel(int code) {
    if (code == SC_W) return "W";
    if (code == SC_A) return "A";
    if (code == SC_S) return "S";
    if (code == SC_D) return "D";
    if (code == SC_SPACE) return "SPACE";
    if (code == SC_ESCAPE) return "ESC";
    if (code == SC_UP) return "UP";
    if (code == SC_DOWN) return "DOWN";
    if (code == SC_LEFT) return "LEFT";
    if (code == SC_RIGHT) return "RIGHT";
    if (code == SC_E) return "E";
    if (code == SC_Q) return "Q";
    if (code == SC_P) return "P";
    if (code == SC_V) return "V";
    if (code == SC_O) return "O";
    char buf[16];
    std::snprintf(buf, sizeof buf, "SC_%d", code);
    return std::string(buf);
}

}  // namespace

std::vector<PauseMenu::Row> PauseMenu::keybindsRows(const Settings& s) const {
    std::vector<Row> r;
    auto button = [&](int id, const char* label, const std::string& hint = "") { r.push_back({BUTTON, label, "", hint, id}); };
    auto header = [&](const char* label) { r.push_back({HEADER, label, "", "", 0}); };
    header("KEYBINDS");
    button(KEY_THROTTLE, "THROTTLE", keyLabel(s.keys.throttle));
    button(KEY_BRAKE, "BRAKE", keyLabel(s.keys.brake));
    button(KEY_STEER_LEFT, "STEER LEFT", keyLabel(s.keys.steerLeft));
    button(KEY_STEER_RIGHT, "STEER RIGHT", keyLabel(s.keys.steerRight));
    button(KEY_HANDBRAKE, "HANDBRAKE", keyLabel(s.keys.handbrake));
    button(KEY_GEAR_UP, "GEAR UP", keyLabel(s.keys.gearUp));
    button(KEY_GEAR_DOWN, "GEAR DOWN", keyLabel(s.keys.gearDown));
    button(KEY_MENU, "MENU", keyLabel(s.keys.menu));
    button(KEY_STEER_ASSIST, "STEERING ASSIST", keyLabel(s.keys.steeringAssist));
    button(KEY_WINCH, "WINCH", keyLabel(s.keys.winch));
    button(KEY_STRAP, "TOW STRAP", keyLabel(s.keys.strap));
    button(BACK_ROW, "BACK");
    return r;
}