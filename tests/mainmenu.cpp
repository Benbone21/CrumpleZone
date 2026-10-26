// The new main menu's navigation (mainmenu.h): front -> SANDBOX/CAREER -> map -> vehicle -> start, the ways back, the front page's other
// buttons, and the DEV page's entries. No SDL or GL: the keys are scancodes.
#include "../src/mainmenu.h"
#include "../src/vehicles.h"
#include "../src/world.h"
#include "harness.h"

using namespace mainmenu;

SCENARIO(menuFlow, "mainmenu/flow", "SANDBOX goes front -> map -> vehicle -> start with the picks (CAREER has its own world pages: mainmenu/career-worlds)") {
    for (int career = 0; career < 1; career++) {
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

SCENARIO(menuCareerWorlds, "mainmenu/career-worlds", "CAREER lists the worlds (a damaged one can't be played) then NEW WORLD; the New World page makes A_CAREER_NEW; delete asks first") {
    WorldSave good, bad;
    good.name = "ALPHA", good.slug = "alpha", good.map = listedMaps()[1], good.diff = DIFF_HARDCORE, good.mode = MODE_CREW;
    bad.name = "bad-slug", bad.slug = "bad-slug", bad.damaged = true;
    Nav n;
    n.open(listedMaps()[0], listedVehicles()[0]);
    n.worlds = {bad, good};
    n.choose(F_CAREER);
    ctx.check("CAREER opens the world list", n.page == P_WORLDS && n.career);
    ctx.check("two worlds and NEW WORLD: three rows", n.count() == 3);
    ctx.check("Enter on the damaged world does nothing", n.key(K_ENTER).act == A_NONE && n.page == P_WORLDS);
    n.key(K_S);
    const Result c = n.key(K_ENTER);
    ctx.check("Enter on the good one continues it, by slug", c.act == A_CAREER_CONTINUE && c.world == "alpha");

    // Delete: the key asks, NO is the default and keeps it, YES returns the slug.
    n.open(0, 0);
    n.worlds = {bad, good};
    n.choose(F_CAREER);
    n.key(K_DELETE);
    ctx.check("Delete asks (on NO)", n.page == P_DELETE_WORLD && n.sel == 1 && n.title() == "DELETE bad-slug?");
    ctx.check("Enter on NO goes back to the list, nothing deleted", n.key(K_ENTER).act == A_NONE && n.page == P_WORLDS && n.worlds.size() == 2);
    n.key(K_DELETE);
    n.key(K_ESC);
    ctx.check("Esc on the question goes back too", n.page == P_WORLDS && n.worlds.size() == 2);
    n.key(K_DELETE);
    n.key(K_A);
    ctx.check("A moves to YES", n.sel == 0);
    const Result d = n.key(K_ENTER);
    ctx.check("YES asks the game to delete the slug and returns to the list", d.act == A_CAREER_DELETE && d.world == "bad-slug" && n.page == P_WORLDS && n.worlds.size() == 1 && n.count() == 2);
    n.setSel(1);
    n.key(K_DELETE);
    ctx.check("Delete on NEW WORLD does nothing", n.page == P_WORLDS);

    // New World.
    n.setSel(1);
    n.key(K_ENTER);
    ctx.check("NEW WORLD opens the page with a default name", n.page == P_NEW_WORLD && n.newName == "WORLD 1" && n.count() == N_COUNT);
    ctx.check("difficulty starts on MEDIUM", n.diffIdx == DIFF_MEDIUM);
    n.setSel(N_NAME);
    n.key(K_ENTER);
    ctx.check("Enter on NAME starts typing", n.typing);
    for (int i = 0; i < 7; i++) n.key(K_BACKSPACE);
    ctx.check("Backspace edits the name (it doesn't leave the page)", n.page == P_NEW_WORLD && n.newName.empty());
    n.text("my rig");
    ctx.check("typed text is capitalised into the name", n.newName == "MY RIG");
    n.key(K_ENTER);
    ctx.check("Enter ends typing", !n.typing && n.sel == N_NAME);
    ctx.check("text is refused when not typing", !n.text("X") && n.newName == "MY RIG");
    n.setSel(N_MAP);
    n.key(K_D);
    ctx.check("D cycles the map", n.worldMapIdx == 1 || careerMaps().size() == 1);
    bool openEnded = false;
    for (size_t i = 0; i <= careerMaps().size(); i++) {
        openEnded = openEnded || n.worldMap() == MAP_PROCEDURAL || n.worldMap() == MAP_REAL;
        n.key(K_D);
    }
    ctx.check("a full cycle of maps never lands on the procedural or a real terrain (not career maps yet)", !openEnded && !careerMaps().empty());
    ctx.check("the career maps are the listed ones less those", careerMaps().size() + 1 + (mapListed(MAP_REAL) ? 1 : 0) == listedMaps().size() && !careerMap(MAP_PROCEDURAL) &&
                                                                     !careerMap(MAP_REAL) && careerMap(MAP_BACKCOUNTRY));
    n.worldMapIdx = 1 % (int)careerMaps().size();
    n.setSel(N_DIFF);
    n.key(K_D);
    ctx.check("D: MEDIUM -> HARDCORE", n.diffIdx == DIFF_HARDCORE && std::string(difficultyName((Difficulty)n.diffIdx)) == "HARDCORE");
    n.key(K_D);
    ctx.check("...and wraps to LIGHT", n.diffIdx == DIFF_LIGHT);
    n.key(K_A);
    ctx.check("A wraps back to HARDCORE", n.diffIdx == DIFF_HARDCORE);
    n.setSel(N_MODE);
    n.key(K_D);
    ctx.check("D: SHARED CREW", n.modeIdx == MODE_CREW);
    n.setSel(N_VEH);
    const std::vector<int> sv = startVehicles();
    bool jesko = false;
    for (size_t i = 0; i <= sv.size(); i++) {
        jesko = jesko || !startableVehicle(n.startVehicle());
        n.key(K_D);
    }
    ctx.check("a full cycle of vehicles never lands on the Jesko", !jesko && sv.size() < listedVehicles().size() + 1 && !sv.empty());
    n.setSel(N_CREATE);
    const Result r = n.key(K_ENTER);
    ctx.check("CREATE returns the picks", r.act == A_CAREER_NEW && r.worldName == "MY RIG" && r.map == n.worldMap() && careerMap(r.map) && r.difficulty == 2 && r.mode == MODE_CREW &&
                                              r.vehicle == n.startVehicle() && startableVehicle(r.vehicle));
    n.key(K_ESC);
    ctx.check("Esc goes back to the list, on NEW WORLD", n.page == P_WORLDS && n.sel == (int)n.worlds.size());
    n.key(K_ESC);
    ctx.check("Esc again: the front page, on CAREER", n.page == P_FRONT && n.sel == F_CAREER);

    // The default name skips the ones taken; an empty name falls back to it.
    WorldSave w1;
    w1.name = "WORLD 1", w1.slug = "world";
    n.worlds = {w1};
    n.choose(F_CAREER);
    n.choose(1);
    ctx.check("default name skips WORLD 1", n.newName == "WORLD 2");
    n.newName = "  ";
    n.setSel(N_CREATE);
    ctx.check("a blank name creates as the default", n.key(K_ENTER).worldName == "WORLD 2");
    // Esc while typing stops typing, not the page.
    n.setSel(N_NAME);
    n.key(K_ENTER);
    n.key(K_ESC);
    ctx.check("Esc while typing only stops typing", n.page == P_NEW_WORLD && !n.typing);
    // SANDBOX still goes by the map and vehicle pages.
    n.open(0, 0);
    n.choose(F_SANDBOX);
    ctx.check("SANDBOX keeps its map page", n.page == P_MAP && !n.career);
}
