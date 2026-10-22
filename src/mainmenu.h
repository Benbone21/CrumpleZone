// The new main menu (shown on launch): a front page (SANDBOX, CAREER, MULTIPLAYER, SETTINGS, small DEV
// and QUIT), the pick pages SANDBOX/CAREER go through (the map, then the vehicle, then start), and the DEV page that lists
// the developer tools. The navigation here is plain state with no GL or SDL, so the tests drive it; mainmenudraw.cpp
// paints it over the live camp scene (menuscene.h) and main.cpp acts on what it returns.
#pragma once
#include <string>
#include <vector>

namespace mainmenu {

enum Page { P_FRONT, P_MAP, P_VEHICLE, P_DEV };

// What a press asks main.cpp to do. (Moves between pages are done inside; they return A_NONE.)
enum Act {
    A_NONE,
    A_START,         // map + vehicle + career chosen: free drive there
    A_MULTIPLAYER,   // the join / host page
    A_SETTINGS,      // the settings page (the SETTINGS button)
    A_QUIT,
    A_DEV_EDITOR,    // the world editor
    A_DEV_SECTION,   // a new obstacle section in the editor
    A_DEV_LAB,       // the model lab
    A_DEV_LEVELS,    // the levels page (play or edit the levels and sections)
    A_DEV_TOOLS,     // the dev menu: spawn, teleport, time, view and the other tools
    A_DEV_HOTKEYS,   // toggles DEV HOTKEYS
    A_DEV_DEBUG,     // toggles the F3 debug screen
    A_DEV_CLASSIC,   // the old title page with every row
};

struct Result {
    Act act = A_NONE;
    bool career = false;
    int map = 0;      // A_START: the map (a MapId)
    int vehicle = 0;  // A_START: the vehicle (an index into vehicleCatalog())
    bool operator==(const Result& o) const { return act == o.act && career == o.career && map == o.map && vehicle == o.vehicle; }
};

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
           K_W = 26, K_S = 22, K_A = 4, K_D = 7, K_BACKSPACE = 42 };

class Nav {
public:
    Page page = P_FRONT;
    bool career = false;   // the mode being picked for
    int sel = 0;           // the focused row of the page (front: a Front value)
    int mapIdx = 0;        // index into listedMaps()
    int vehIdx = 0;        // index into listedVehicles()
    bool devHotkeys = false, debugOn = false;  // what the DEV page's toggles show (set by the game)

    // Opens on the front page, the pickers on the map and vehicle last used (a MapId, a catalog index).
    void open(int map, int vehicle);
    int count() const;     // rows of the current page
    Result key(int scancode);
    // A click or tap on row `i` (drawn rows; the front page's Front values).
    Result choose(int i);
    Result back();
    void move(int dir);
    void setSel(int i);    // the mouse hovering a row
    int map() const;       // the MapId picked
    int vehicle() const;   // the catalog index picked
    std::string title() const;

    // Where each row was drawn (drawable pixels), for the mouse: set by the painter.
    struct Hit { float x, y, w, h; int row; };
    std::vector<Hit> hits;
    int hitAt(float x, float y) const;

    // The pad-or-keyboard hint line for the page.
    std::string hint(bool pad) const;
};

}  // namespace mainmenu
