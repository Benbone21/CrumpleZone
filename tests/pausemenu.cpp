// The pause menu (Esc) and the tabbed SETTINGS screen (menu.h): the five rows, the tabs and how they switch (Q / E, the mouse, the pad's
// shoulder buttons), F12's DEV tab, and the keys that took over the removed rows (autopilot, jobs). No GL: menu.cpp's logic, and the
// pad's logic in gamepad.cpp, which turns LB / RB into Q / E in a menu.
#include <algorithm>
#include <string>
#include <vector>

#include "../src/gamepad.h"
#include "../src/menu.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

SCENARIO(pauseList, "pausemenu/list", "The pause menu is RESUME, MULTIPLAYER, SETTINGS, MAIN MENU, QUIT GAME; the old rows are gone") {
    Settings s;
    PauseMenu m;
    m.show(0, 0);
    const std::vector<std::string> want = {"RESUME", "MULTIPLAYER", "SETTINGS", "MAIN MENU", "QUIT GAME"};
    ctx.check("exactly those five rows", m.labels(s) == want);
    for (const char* gone : {"VEHICLE", "MAP", "LEVELS", "JOBS", "GARAGE", "AUTOPILOT", "CONTROLS", "DEV MENU"}) ctx.check(std::string("no ") + gone + " row", !has(m.labels(s), gone));
    ctx.check("opens on RESUME", m.selectedLabel(s) == "RESUME");
    ctx.check("ENTER on RESUME resumes", m.key(SC_RETURN, s).action == PauseMenu::RESUME);
    ctx.check("Esc resumes", m.key(SC_ESCAPE, s).action == PauseMenu::RESUME);
    ctx.check("S S S S ends on QUIT GAME, ENTER quits", (m.key(SC_S, s), m.key(SC_S, s), m.key(SC_S, s), m.key(SC_S, s), m.selectedLabel(s) == "QUIT GAME" && m.key(SC_RETURN, s).action == PauseMenu::QUIT));
    m.show(0, 0);
    m.select(s, "MAIN MENU");
    ctx.check("MAIN MENU answers", m.key(SC_RETURN, s).action == PauseMenu::MAIN_MENU);
    m.show(0, 0);
    m.levelActive = true;
    ctx.check("a level in play adds RESTART LEVEL and QUIT LEVEL", has(m.labels(s), "RESTART LEVEL") && has(m.labels(s), "QUIT LEVEL"));
    m.levelActive = false;
    m.editorSuspended = true;
    ctx.check("a playtest from the editor adds BACK TO THE EDITOR", has(m.labels(s), "BACK TO THE EDITOR"));
}

SCENARIO(settingsTabs, "pausemenu/tabs", "SETTINGS has GRAPHICS, CONTROLS, AUDIO, GENERAL and DEV tabs; every option is on one; Q / E switch") {
    Settings s;
    PauseMenu m;
    m.show(0, 0);
    m.select(s, "SETTINGS");
    ctx.check("ENTER on SETTINGS opens the settings screen on GRAPHICS", m.key(SC_RETURN, s).action == PauseMenu::NONE && m.pageNow() == 1 && m.tabNow() == PauseMenu::T_GRAPHICS);
    ctx.check("five tabs in a game", m.tabCount() == 5 && std::string(PauseMenu::tabName(4)) == "DEV");
    const std::vector<std::string> g = m.labels(s);
    for (const char* l : {"QUALITY", "SHADOWS", "ANTI-ALIASING", "RENDER SCALE", "FULLSCREEN", "VSYNC", "SHOW FPS", "PHYSICS THREADS", "FPS TARGET", "GRASS"}) ctx.check(std::string("GRAPHICS has ") + l, has(g, l));
    ctx.check("GRAPHICS has no volume or units", !has(g, "VOLUME") && !has(g, "SPEED UNITS"));
    ctx.check("E: CONTROLS", (m.key(SC_E, s), m.tabNow() == PauseMenu::T_CONTROLS));
    const std::vector<std::string> c = m.labels(s);
    for (const char* l : {"PAD STEERING", "STICK DEADZONE", "RUMBLE", "STEERING ASSIST", "KEYBOARD THROTTLE", "THROTTLE", "AUTOPILOT: WHERE TO", "JOBS", "RESET ALL TO DEFAULTS"}) ctx.check(std::string("CONTROLS has ") + l, has(c, l));
    ctx.check("E: AUDIO, with every volume", (m.key(SC_E, s), m.tabNow() == PauseMenu::T_AUDIO && has(m.labels(s), "VOLUME") && has(m.labels(s), "VEHICLE VOLUME") && has(m.labels(s), "MUSIC") && has(m.labels(s), "AMBIENCE")));
    ctx.check("E: GENERAL", (m.key(SC_E, s), m.tabNow() == PauseMenu::T_GENERAL));
    const std::vector<std::string> gen = m.labels(s);
    for (const char* l : {"SPEED UNITS", "MODE", "RECOVERY", "FUEL", "TIME OF DAY", "WINCH SPEED", "SHOW FULL HUD", "FIELD OF VIEW"}) ctx.check(std::string("GENERAL has ") + l, has(gen, l));
    ctx.check("E: DEV", (m.key(SC_E, s), m.tabNow() == PauseMenu::T_DEV && has(m.labels(s), "DEV HOTKEYS")));
    ctx.check("E wraps round to GRAPHICS", (m.key(SC_E, s), m.tabNow() == PauseMenu::T_GRAPHICS));
    ctx.check("Q wraps back to DEV", (m.key(SC_Q, s), m.tabNow() == PauseMenu::T_DEV));
    ctx.check("Q: GENERAL", (m.key(SC_Q, s), m.tabNow() == PauseMenu::T_GENERAL));
    ctx.check("a tab starts on a row, not a header", m.selectedLabel(s) == "SPEED UNITS");
    ctx.check("changing a setting still works (A D on SPEED UNITS)", (m.key(SC_D, s).action == PauseMenu::CHANGED && s.mph));
    ctx.check("Esc goes back to the pause menu, on SETTINGS", m.key(SC_ESCAPE, s).action == PauseMenu::NONE && m.pageNow() == 0 && m.selectedLabel(s) == "SETTINGS");
}

SCENARIO(settingsTitle, "pausemenu/title-settings", "The main menu opens the same settings screen: four tabs (DEV needs a game), BACK returns to the main menu") {
    Settings s;
    PauseMenu m;
    m.showTitlePage(1, 0, 0);
    ctx.check("page 1 on GRAPHICS", m.pageNow() == 1 && m.tabNow() == PauseMenu::T_GRAPHICS);
    ctx.check("four tabs", m.tabCount() == 4);
    m.setTab(PauseMenu::T_GENERAL);
    m.key(SC_E, s);
    ctx.check("E from GENERAL wraps to GRAPHICS (no DEV)", m.tabNow() == PauseMenu::T_GRAPHICS);
    m.key(SC_Q, s);
    ctx.check("Q from GRAPHICS wraps to GENERAL", m.tabNow() == PauseMenu::T_GENERAL);
    m.key(SC_ESCAPE, s);
    ctx.check("Esc: the title's first page", m.pageNow() == 0 && m.title);
}

SCENARIO(settingsMouse, "pausemenu/tab-mouse", "Clicking a tab switches to it; a click on a row still works") {
    Settings s;
    PauseMenu m;
    m.showSettings(PauseMenu::T_GRAPHICS, 0, 0, true);
    for (int W : {1280, 960, 1920}) {
        const int H = W * 9 / 16;
        m.setView(W, H);
        m.setTab(PauseMenu::T_GRAPHICS);
        bool all = true;
        for (int t = 0; t < m.tabCount(); t++) {
            const PauseMenu::Rect r = m.tabRect(t, s);
            m.mouseDown(r.x + r.w / 2, r.y + r.h / 2, s);
            all = all && m.tabNow() == t;
        }
        ctx.check("every tab cell selects its tab at " + std::to_string(W), all);
        const PauseMenu::Rect a = m.tabRect(0, s), b = m.tabRect(1, s);
        ctx.check("cells sit side by side on one strip", a.y == b.y && a.x + a.w <= b.x + 0.01f && a.h > 0);
    }
    m.setView(1280, 720);
    m.setTab(PauseMenu::T_AUDIO);
    const PauseMenu::Rect r2 = m.tabRect(2, s);
    m.mouseDown(r2.x + 1, r2.y - 5, s);  // (above the strip: nothing)
    ctx.check("a click above the strip does not switch", m.tabNow() == PauseMenu::T_AUDIO);
}

SCENARIO(settingsPad, "pausemenu/tab-pad", "The pad's shoulder buttons are Q and E in a menu, and Q / E are what switch tabs") {
    using namespace pad;
    Logic l;
    Raw r;
    r.buttons = 1u << B_RB;
    l.update(r, Mode::Menu, 1.0 / 60);
    bool e = false, q = false;
    for (const Tap& t : l.taps) e |= t.scancode == SC_E;
    ctx.check("RB taps E", e);
    r = Raw();
    l.update(r, Mode::Menu, 1.0 / 60);
    r.buttons = 1u << B_LB;
    l.update(r, Mode::Menu, 1.0 / 60);
    for (const Tap& t : l.taps) q |= t.scancode == SC_Q;
    ctx.check("LB taps Q", q);
}

SCENARIO(settingsDev, "pausemenu/dev-tab", "F12 opens Settings on the DEV tab: the groups, one group's rows (FREE CAM), BACK a step at a time") {
    Settings s;
    PauseMenu m;
    m.show(0, 0);
    m.showDev(0, 0, true);  // (what main.cpp does for F12 over the pause menu)
    ctx.check("Settings on the DEV tab", m.devOpen() && m.pageNow() == 1 && m.tabNow() == PauseMenu::T_DEV);
    const std::vector<std::string> groups = m.labels(s);
    ctx.check("DEV HOTKEYS first, then every group, then BACK", groups.size() == 2 + devmenu::G_COUNT && groups[0] == "DEV HOTKEYS" && groups.back() == "BACK");
    for (int g = 0; g < devmenu::G_COUNT; g++) ctx.check(std::string("group ") + devmenu::groupName((devmenu::Group)g), has(groups, devmenu::groupName((devmenu::Group)g)));
    m.select(s, devmenu::groupName(devmenu::G_VIEW));
    m.key(SC_RETURN, s);
    ctx.check("a group shows its rows, FREE CAM among them", has(m.labels(s), "FREE CAM") && has(m.labels(s), "BACK") && !has(m.labels(s), "DEV HOTKEYS"));
    m.select(s, "FREE CAM");
    const PauseMenu::Result run = m.key(SC_RETURN, s);
    ctx.check("ENTER runs the tool (DEV_RUN FREE_CAM)", run.action == PauseMenu::DEV_RUN && run.vehicle == devmenu::FREE_CAM);
    ctx.check("Esc: back to the groups", m.key(SC_ESCAPE, s).action == PauseMenu::NONE && m.devOpen() && has(m.labels(s), "DEV HOTKEYS"));
    ctx.check("Esc again: the pause menu (it came from there)", m.key(SC_ESCAPE, s).action == PauseMenu::NONE && m.pageNow() == 0 && !m.devOpen());
    PauseMenu n;
    n.show(0, 0);
    n.showDev(0, 0, false);
    ctx.check("F12 from the game: Esc closes the menu", n.devOpen() && n.key(SC_ESCAPE, s).action == PauseMenu::RESUME);
    PauseMenu t;
    t.show(0, 0);
    t.showSettings(PauseMenu::T_AUDIO, 0, 0, true);
    t.showDev(0, 0, false);
    ctx.check("F12 inside Settings just switches to DEV and keeps where it came from", t.devOpen() && t.key(SC_ESCAPE, s).action == PauseMenu::NONE && t.pageNow() == 0);
    ctx.check("DEV HOTKEYS toggles the setting as before", (n.showDev(0, 0, false), n.key(SC_D, s).action == PauseMenu::CHANGED && s.devHotkeys));
    PauseMenu k;
    k.showDev(0, 0, false);
    k.select(s, devmenu::groupName(devmenu::G_VEHICLE));
    ctx.check("W S wraps within a tab and never lands on a header", (k.key(SC_W, s), k.key(SC_W, s), !k.selectedLabel(s).empty()));
}

SCENARIO(settingsKeysTab, "pausemenu/keys-in-controls", "Keybinds live on the CONTROLS tab: ENTER on a row waits for a key, Q / E do not switch tabs meanwhile") {
    Settings s;
    PauseMenu m;
    m.showSettings(PauseMenu::T_CONTROLS, 0, 0, true);
    m.select(s, "JOBS");
    m.key(SC_RETURN, s);
    ctx.check("listening for a key", m.listeningForKey());
    const PauseMenu::Result r = m.key(SC_Q, s);
    ctx.check("Q becomes the binding (it clashes: the key swaps) and the tab stays", r.action == PauseMenu::CHANGED && m.tabNow() == PauseMenu::T_CONTROLS && !m.listeningForKey());
    ctx.check("no clash is left in the layout", [&] {
        Keybinds kb = s.keys;
        kb.sanitize();
        for (int a = 0; a < KA_COUNT; a++)
            if (kb.key((KeyAction)a) != s.keys.key((KeyAction)a)) return false;
        return true;
    }());
}

SCENARIO(pauseStandalone, "pausemenu/key-pages", "The autopilot and jobs pages open straight from their keys and BACK returns to the game; their old rows are gone from the pause page") {
    Settings s;
    PauseMenu m;
    m.showPage(3, 0, 0);
    ctx.check("autopilot page", m.pageNow() == 3 && has(m.labels(s), "DRIVE TO") && has(m.labels(s), "GO"));
    ctx.check("Esc leaves the menu", m.key(SC_ESCAPE, s).action == PauseMenu::RESUME);
    m.showPage(5, 0, 0);
    ctx.check("jobs page", m.pageNow() == 5);
    ctx.check("BACK leaves the menu", (m.select(s, "BACK"), m.key(SC_RETURN, s).action == PauseMenu::RESUME));
}

SCENARIO(mainMenuRoutes, "pausemenu/main-menu-routes", "Every route to MAIN MENU lands on the new main menu, never the old title page; only DEV > CLASSIC MENU shows the old one") {
    Settings s;
    PauseMenu m;
    m.show(0, 0);
    m.select(s, "MAIN MENU");
    ctx.check("the pause menu's MAIN MENU row asks for the main menu", m.key(SC_RETURN, s).action == PauseMenu::MAIN_MENU);
    ctx.check("the pause menu is not the main menu", !m.newMainMenu(false));
    // (main.cpp answers that with showTitle() whether the camp could load or not: a map still loading, multiplayer, the editor.)
    m.showTitle(0, 0);
    ctx.check("showTitle: the new main menu", m.newMainMenu(false));
    ctx.check("...and only DEV > CLASSIC MENU makes it the old page", !m.newMainMenu(true));
    for (int pg : {1, 2, 4}) {
        m.showTitlePage(pg, 0, 0);
        ctx.check("a title page (" + std::to_string(pg) + ") is not the front page", !m.newMainMenu(false));
        m.key(SC_ESCAPE, s);
        if (pg == 2) m.key(SC_ESCAPE, s);
        ctx.check("Esc from title page " + std::to_string(pg) + " is back on the new main menu", m.newMainMenu(false) || m.pageNow() == 0);
    }
    PauseMenu g;
    g.show(0, 0);
    g.showSettings(PauseMenu::T_GENERAL, 0, 0, true);
    g.key(SC_ESCAPE, s);
    g.select(s, "MAIN MENU");
    ctx.check("back from Settings in a game, MAIN MENU is still the same one row", g.selectedLabel(s) == "MAIN MENU" && g.key(SC_RETURN, s).action == PauseMenu::MAIN_MENU);
    PauseMenu closed;
    ctx.check("a closed menu is no main menu", !closed.newMainMenu(false));
}
