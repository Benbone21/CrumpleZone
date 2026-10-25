// The new main menu (shown on launch): a front page (SANDBOX, CAREER, MULTIPLAYER, SETTINGS, small DEV
// and QUIT), the pick pages SANDBOX goes through (the map, then the vehicle, then start), CAREER's world list with its New World and Delete pages, and the DEV page that lists
// the developer tools. The navigation here is plain state with no GL or SDL, so the tests drive it; mainmenudraw.cpp
// paints it over the live camp scene (menuscene.h) and main.cpp acts on what it returns.
#pragma once
#include <string>
#include <vector>

#include "careerworld.h"
#include "textedit.h"

namespace mainmenu {

enum Page { P_FRONT, P_MAP, P_VEHICLE, P_DEV, P_WORLDS, P_NEW_WORLD, P_DELETE_WORLD };

// What a press asks main.cpp to do. (Moves between pages are done inside; they return A_NONE.)
enum Act {
    A_NONE,
    A_START,         // map + vehicle + career chosen: free drive there
    A_MULTIPLAYER,   // the join / host page
    A_SETTINGS,      // the settings page (the SETTINGS button)
    A_QUIT,
    A_CAREER_CONTINUE,  // a saved world chosen: Result::world is its slug
    A_CAREER_NEW,       // CREATE on the New World page: worldName, map, difficulty, mode and vehicle are the picks
    A_CAREER_DELETE,    // YES on the delete page: Result::world is the slug (the game calls deleteWorld; the Nav has already dropped it from its list)
    A_DEV_EDITOR,    // the world editor
    A_DEV_SECTION,   // a new obstacle section in the editor
    A_DEV_LAB,       // the model lab
    A_DEV_MENUSCENE, // the editor on this menu's own camp scene (menucamp.h)
    A_DEV_LEVELS,    // the levels page (play or edit the levels and sections)
    A_DEV_TOOLS,     // the dev menu: spawn, teleport, time, view and the other tools
    A_DEV_HOTKEYS,   // toggles DEV HOTKEYS
    A_DEV_DEBUG,     // toggles the F3 debug screen
    A_DEV_CLASSIC,   // the old title page with every row
};

struct Result {
    Result(Act a = A_NONE) : act(a) {}
    Act act;
    bool career = false;
    int map = 0;      // A_START: the map (a MapId)
    int vehicle = 0;  // A_START / A_CAREER_NEW: the vehicle (an index into vehicleCatalog())
    std::string world;      // A_CAREER_CONTINUE / A_CAREER_DELETE: the world's slug
    int difficulty = 1;     // A_CAREER_NEW: a Difficulty
    int mode = 0;           // A_CAREER_NEW: a WorldMode
    std::string worldName;  // A_CAREER_NEW: the name typed
    bool operator==(const Result& o) const {
        return act == o.act && career == o.career && map == o.map && vehicle == o.vehicle && world == o.world && difficulty == o.difficulty &&
               mode == o.mode && worldName == o.worldName;
    }
};

// The New World page's rows.
enum NewRow { N_NAME, N_MAP, N_DIFF, N_MODE, N_VEH, N_CREATE, N_COUNT };

// The catalog indices a career can start in: listedVehicles() without the ones startableVehicle() refuses.
std::vector<int> startVehicles();

// The front page's focus order.
enum Front { F_SANDBOX, F_CAREER, F_MULTI, F_SETTINGS, F_DEV, F_QUIT, F_COUNT };

struct DevEntry {
    Act act;
    const char* label;
    const char* hint;
};
// The DEV page's rows, in order.
const std::vector<DevEntry>& devEntries();

// Keys the menu reads (SDL scancodes; kept here so the tests need no SDL).
enum Key { K_ESC = 41, K_ENTER = 40, K_KPENTER = 88, K_SPACE = 44, K_UP = 82, K_DOWN = 81, K_LEFT = 80, K_RIGHT = 79,
           K_W = 26, K_S = 22, K_A = 4, K_D = 7, K_BACKSPACE = 42, K_DELETE = 76 };

class Nav {
public:
    Page page = P_FRONT;
    bool career = false;   // the mode being picked for
    int sel = 0;           // the focused row of the page (front: a Front value)
    int mapIdx = 0;        // index into listedMaps()
    int vehIdx = 0;        // index into listedVehicles()
    bool devHotkeys = false, debugOn = false;  // what the DEV page's toggles show (set by the game)
    std::vector<WorldSave> worlds;  // the saved worlds, set by the game (listWorlds()) before the CAREER button is used
    int delIdx = -1;                // P_DELETE_WORLD: the world asked about (an index into worlds)
    // The New World page's picks.
    std::string newName;
    te::Edit nameEdit;
    bool typing = false;            // the NAME row is taking keys and text
    int diffIdx = DIFF_MEDIUM, modeIdx = MODE_PERSONAL;
    int startIdx = 0;               // index into startVehicles()

    // Opens on the front page, the pickers on the map and vehicle last used (a MapId, a catalog index).
    void open(int map, int vehicle);
    int count() const;     // rows of the current page
    Result key(int scancode, bool ctrl = false, bool shift = false);
    // Typed text (SDL's text input) while the NAME row is being edited; false when nothing takes it.
    bool text(const std::string& s);
    // A click or tap on row `i` (drawn rows; the front page's Front values).
    Result choose(int i);
    Result back();
    void move(int dir);
    void cycle(int row, int dir);  // the New World page: step a pick (MAP, DIFFICULTY, MODE, VEHICLE) by dir
    void setSel(int i);    // the mouse hovering a row
    int map() const;       // the MapId picked
    int vehicle() const;   // the catalog index picked
    std::string title() const;
    int startVehicle() const;  // the catalog index picked on the New World page

    // Where each row was drawn (drawable pixels), for the mouse: set by the painter.
    struct Hit { float x, y, w, h; int row; };
    std::vector<Hit> hits;
    int hitAt(float x, float y) const;

    // The pad-or-keyboard hint line for the page.
    std::string hint(bool pad) const;
};

}  // namespace mainmenu
