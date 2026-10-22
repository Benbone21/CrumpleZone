// The new main menu's navigation (mainmenu.h): front -> SANDBOX/CAREER -> map -> vehicle -> start, the ways back, the front page's other
// buttons, and the DEV page's entries. No SDL or GL: the keys are scancodes.
#include "../src/mainmenu.h"
#include "../src/vehicles.h"
#include "../src/world.h"
#include "harness.h"

using namespace mainmenu;

SCENARIO(menuFlow, "mainmenu/flow", "SANDBOX goes front -> map -> vehicle -> start with the picks; CAREER the same with career set") {
    for (int career = 0; career < 2; career++) {
        Nav n;
        n.open(listedMaps()[0], listedVehicles()[0]);
        const std::string tag = career ? "CAREER: " : "SANDBOX: ";
        ctx.check(tag + "opens on the front page, SANDBOX first", n.page == P_FRONT && n.sel == F_SANDBOX);
        if (career) n.key(K_DOWN);
        ctx.check(tag + "the focus is on its button", n.sel == (career ? F_CAREER : F_SANDBOX));
        ctx.check(tag + "ENTER opens the map page", n.key(K_ENTER).act == A_NONE && n.page == P_MAP && n.career == (career != 0));
        n.key(K_DOWN);
        n.key(K_S);
        ctx.check(tag + "two steps down picks the third map", n.map() == listedMaps()[2]);
        ctx.check(tag + "ENTER opens the vehicle page", n.key(K_ENTER).act == A_NONE && n.page == P_VEHICLE);
        n.key(K_DOWN);
        const Result r = n.key(K_ENTER);
        ctx.check(tag + "ENTER starts", r.act == A_START);
        ctx.check(tag + "...with the mode, the map and the vehicle picked", r.career == (career != 0) && r.map == listedMaps()[2] && r.vehicle == listedVehicles()[1]);
    }
}

SCENARIO(menuBack, "mainmenu/back", "Esc goes back one page at a time, keeping the picks, and does nothing on the front page") {
    Nav n;
    n.open(listedMaps()[1], listedVehicles()[2]);
    ctx.check("opens with the last map and vehicle picked", n.map() == listedMaps()[1] && n.vehicle() == listedVehicles()[2]);
    n.key(K_ENTER);
    n.key(K_ENTER);
    ctx.check("two ENTERs in: the vehicle page", n.page == P_VEHICLE && n.sel == 2);
    n.key(K_ESC);
    ctx.check("Esc: back to the map page, on the map picked", n.page == P_MAP && n.sel == 1);
    n.key(K_ESC);
    ctx.check("Esc: back to the front page, on SANDBOX", n.page == P_FRONT && n.sel == F_SANDBOX);
    ctx.check("Esc on the front page does nothing", n.key(K_ESC).act == A_NONE && n.page == P_FRONT);
    ctx.check("the picks are kept", n.map() == listedMaps()[1] && n.vehicle() == listedVehicles()[2]);
    n.choose(F_CAREER);
    n.key(K_BACKSPACE);
    ctx.check("back from CAREER lands on CAREER", n.page == P_FRONT && n.sel == F_CAREER);
}

SCENARIO(menuFront, "mainmenu/front", "The front page: MULTIPLAYER, SETTINGS, QUIT and DEV answer; every button is reachable with the keys") {
    Nav n;
    n.open(0, 0);
    ctx.check("MULTIPLAYER asks for the join page", n.choose(F_MULTI).act == A_MULTIPLAYER && n.page == P_FRONT);
    ctx.check("SETTINGS asks for the settings", n.choose(F_SETTINGS).act == A_SETTINGS);
    ctx.check("QUIT quits", n.choose(F_QUIT).act == A_QUIT);
    n.open(0, 0);
    std::vector<int> seen;
    for (int i = 0; i < F_COUNT; i++) seen.push_back(n.sel), n.key(K_S);
    bool all = true;
    for (int f = 0; f < F_COUNT; f++) all = all && std::find(seen.begin(), seen.end(), f) != seen.end();
    ctx.check("W/S visit every button", all);
    ctx.check("...and wrap round", n.sel == F_SANDBOX);
    n.setSel(F_DEV);
    n.key(K_RIGHT);
    ctx.check("right from DEV: QUIT", n.sel == F_QUIT);
    n.key(K_RIGHT);
    ctx.check("right from QUIT: DEV again", n.sel == F_DEV);
    n.key(K_LEFT);
    ctx.check("left from DEV: QUIT", n.sel == F_QUIT);
    n.setSel(F_SANDBOX);
    n.key(K_S), n.key(K_S), n.key(K_S);
    ctx.check("three steps down from SANDBOX: SETTINGS, under MULTIPLAYER", n.sel == F_SETTINGS);
}

SCENARIO(menuDev, "mainmenu/dev", "The DEV page lists the existing tools, each asks for its action, and Esc comes back to DEV") {
    Nav n;
    n.open(0, 0);
    n.choose(F_DEV);
    ctx.check("DEV opens the page", n.page == P_DEV && n.count() == (int)devEntries().size());
    std::vector<Act> want = {A_DEV_EDITOR, A_DEV_SECTION, A_DEV_LAB, A_DEV_MENUSCENE, A_DEV_LEVELS, A_DEV_TOOLS, A_DEV_HOTKEYS, A_DEV_DEBUG, A_DEV_CLASSIC};
    ctx.check("the entries are the editors, the lab, the levels, the dev menu, the toggles and the classic page", devEntries().size() == want.size());
    for (int i = 0; i < (int)want.size() && i < (int)devEntries().size(); i++) {
        n.open(0, 0);
        n.choose(F_DEV);
        const Result r = n.choose(i);
        ctx.check(std::string("entry ") + devEntries()[i].label + " returns its action", r.act == want[i]);
        ctx.check(std::string("entry ") + devEntries()[i].label + " has a hint", devEntries()[i].hint[0] != 0);
    }
    n.open(0, 0);
    n.choose(F_DEV);
    n.key(K_ESC);
    ctx.check("Esc: the front page, on DEV", n.page == P_FRONT && n.sel == F_DEV);
}

SCENARIO(menuPicks, "mainmenu/picks", "Every listed map and vehicle can be picked, and what starts is a listed one") {
    Nav n;
    n.open(0, 0);
    for (int m = 0; m < (int)listedMaps().size(); m++)
        for (int v = 0; v < (int)listedVehicles().size(); v++) {
            n.open(0, 0);
            n.choose(F_SANDBOX);
            n.choose(m);
            const Result r = n.choose(v);
            if (!(r.act == A_START && r.map == listedMaps()[m] && r.vehicle == listedVehicles()[v] && mapListed(r.map) && vehicleListed(r.vehicle)))
                ctx.check("map/vehicle pick", false);
        }
    ctx.check("all combinations start", true);
    n.open(0, 0);
    n.choose(F_SANDBOX);
    n.hits = {{0, 0, 100, 40, 0}, {0, 50, 100, 40, 1}};
    ctx.check("the mouse finds a row", n.hitAt(10, 60) == 1 && n.hitAt(10, 45) == -1);
}
