// The multiplayer pages, in the main menu's style (docs/plans/join-codes.md has the wireframes):
// from the main menu, JOIN WITH CODE / JOIN BY ADDRESS / ON YOUR NETWORK / HOST A PRIVATE GAME and
// your name; in the game (ESC > MULTIPLAYER), FRIENDS: open your world to friends (the code, COPY),
// who's in (ping, direct or relayed, kick), the password and how many may join; and the joining
// card with what went wrong in plain words. Plain state, no GL or SDL: the tests drive it;
// mpmenudraw.cpp paints it; main.cpp fills the View each frame and acts on what it returns.
#pragma once
#include <string>
#include <vector>

#include "textedit.h"

namespace mpmenu {

enum Page { P_HOME, P_CODE, P_ADDRESS, P_LAN, P_HOST, P_JOINING, P_FRIENDS };

enum Act {
    A_NONE,
    A_CHANGED,         // a field changed (the game saves it: name, address, passwords, ...)
    A_BACK,            // leave these pages (the main menu, or the pause menu)
    A_JOIN_CODE,       // join with f.code
    A_JOIN_ADDRESS,    // join f.address
    A_JOIN_FOUND,      // join a game on the network: `text` its address, `label` its name
    A_HOST,            // host a private game: f.map (a MapId), f.port, f.session, f.hostPassword, f.maxPlayers, f.alsoCode
    A_OPEN_FRIENDS,    // in the game: open it to friends (hosting first if need be)
    A_CLOSE_FRIENDS,   // the code stops working
    A_COPY_CODE,       // (done here, through te::clipboardSet: the game shows a toast)
    A_STOP_HOSTING,
    A_LEAVE,
    A_KICK,            // arg: the player
    A_CANCEL_JOIN,
    A_RETRY,           // the last join again (with the password typed in the card)
    A_SETTINGS_LIVE,   // in the game: the password or max players changed (the host applies them at once)
};

struct Result {
    Act act = A_NONE;
    int arg = 0;
    std::string text, label;
};

// What the game tells the pages, every frame before drawing or a key.
struct View {
    bool inGame = false;          // over the game (FRIENDS), not the main menu
    int role = 0;                 // 0 offline, 1 hosting, 2 in someone's game
    bool codesAvailable = true;   // a join-code server is set up
    std::string code;             // hosting with a code: "KX7M4Q"
    int codeState = 0;            // NetSession::CodeState
    std::string codeError;
    struct Player {
        int id = 0;
        std::string name;
        int pingMs = 0;
        int link = 0;             // NetSession::Link
        bool self = false;
    };
    std::vector<Player> players;
    std::string sessionTitle, mapName;
    int maxPlayersNow = 8;
    struct Found {
        std::string title, detail, address;
        bool ok = true;
    };
    std::vector<Found> lan;
    double searching = 0;         // s the network list has looked
    std::vector<std::string> localIps;
    int hostPort = 0;
    // Joining.
    bool joining = false;         // connecting (or it failed: joinError)
    bool byCode = false;
    int joinStep = 0;             // NetSession::JoinStep
    int joinFail = 0;             // NetSession::JoinFail
    int failPlayers = 0, failMax = 0;
    std::string joinLabel, joinError;
    bool needPassword = false;
    std::vector<std::string> mapNames;  // listedMaps(), as shown
    std::vector<std::string> recent;    // the last codes or addresses joined (newest first)
};

// What the pages edit (the game copies them to and from its settings).
struct Fields {
    std::string name, code, address, password, hostPassword, session, port = "27015";
    int map = 0;                  // index into View::mapNames / listedMaps()
    int maxPlayers = 8;
    bool alsoCode = true;         // HOST A PRIVATE GAME also opens it to friends
    bool passwordOn = false;      // FRIENDS: ask joiners for the password
};

// Row ids (what a row is; drawn rows are a page's list of these).
enum RowId {
    R_CODE_PAGE, R_ADDRESS_PAGE, R_LAN_PAGE, R_HOST_PAGE, R_NAME, R_RECENT0, R_RECENT2 = R_RECENT0 + 2,
    R_CODEBOX, R_PASTE, R_ADDRESS, R_PASSWORD, R_GO,
    R_FOUND0, R_FOUND7 = R_FOUND0 + 7,
    R_MAP, R_SESSION, R_HOSTPW, R_MAXP, R_PORT, R_ALSOCODE,
    R_RETRY, R_CANCEL,
    R_OPEN, R_COPY, R_CLOSE, R_PWON, R_PLAYER0, R_PLAYER7 = R_PLAYER0 + 7, R_STOP, R_LEAVE, R_JOIN_PAGE,
    R_BACK,
};
enum Kind { BUTTON, TEXT, CHOICE, INFO, PLAYER };
struct Row {
    int id;
    Kind kind;
    std::string label, value, sub;
    bool dim = false;
};

// The plain words for a failed join (the card's big line).
std::string failText(const View& v);
// What the joining card says is happening.
std::string stepText(const View& v);

class Nav {
public:
    Page page = P_HOME;
    int sel = 0;           // index into rows()
    Fields f;
    bool codeEdit = false; // the code box: up/down change the letter at the cursor (a pad); typing works anyway
    int codeCursor = 0;
    int kickArm = -1;      // a player's KICK pressed once: again to kick
    int focusText = -1;    // the text row typing goes to (a RowId; -1 none)
    bool copied = false;   // COPY was pressed (the code shows COPIED a moment)

    void open(Page p, const View& v);
    std::vector<Row> rows(const View& v) const;
    int count(const View& v) const { return (int)rows(v).size(); }
    Result key(int scancode, bool ctrl, bool shift, const View& v);
    Result text(const std::string& typed, const View& v);
    Result choose(int i, const View& v);   // a click or Enter on row i
    Result back(const View& v);
    void move(int dir, const View& v);
    void setSel(int i, const View& v);
    std::string title(const View& v) const;
    std::string hint(bool pad, const View& v) const;
    // Joining started (from this page): the card shows; CANCEL or BACK returns here.
    void showJoining();

    struct Hit { float x, y, w, h; int row; };
    std::vector<Hit> hits;
    int hitAt(float x, float y) const;

private:
    Page joinFrom = P_HOME;
    te::Edit ted;
    void fixSel(const View& v);
    bool textField(int id, std::string*& value, te::Rules& rules);
    Result change(int id, int dir, const View& v);
    Result go(const View& v);
    Result codeKey(int sc, bool ctrl, const View& v);
};

}  // namespace mpmenu
