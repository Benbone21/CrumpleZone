// The multiplayer pages (mpmenu.h) with no window: the keys a keyboard or a pad sends (the pad's A
// arrives as Enter, B as Esc, the D-pad as arrows), typed text, the code box, the joining card's
// plain words, FRIENDS in the game.
#include "../src/mpmenu.h"
#include "../src/net.h"
#include "../src/rendezvous.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {
using namespace mpmenu;
int rowOf(const Nav& n, const View& v, int id) {
    const std::vector<Row> r = n.rows(v);
    for (int i = 0; i < (int)r.size(); i++)
        if (r[i].id == id) return i;
    return -1;
}
bool focused(const Nav& n, const View& v, int id) { return rowOf(n, v, id) == n.sel; }
Result press(Nav& n, const View& v, int sc, bool ctrl = false) { return n.key(sc, ctrl, false, v); }
struct Clip {
    std::string text;
    Clip() { te::fakeClipboard = &text; }
    ~Clip() { te::fakeClipboard = nullptr; }
};
}  // namespace

SCENARIO(mpHome, "ui/mpmenu-home", "the multiplayer page from the main menu: the rows, down and Enter into JOIN WITH CODE, Esc back, your name typed") {
    View v;
    Nav n;
    n.open(P_HOME, v);
    ctx.check("opens on JOIN WITH CODE", focused(n, v, R_CODE_PAGE));
    ctx.check("all four ways in, your name, back", rowOf(n, v, R_ADDRESS_PAGE) >= 0 && rowOf(n, v, R_LAN_PAGE) >= 0 && rowOf(n, v, R_HOST_PAGE) >= 0 && rowOf(n, v, R_NAME) >= 0 &&
                                                       rowOf(n, v, R_BACK) >= 0);
    press(n, v, SC_RETURN);
    ctx.check("Enter: the code page", n.page == P_CODE && focused(n, v, R_CODEBOX));
    press(n, v, SC_ESCAPE);
    ctx.check("Esc: back, on the row it came from", n.page == P_HOME && focused(n, v, R_CODE_PAGE));
    ctx.check("Esc on the home page leaves", press(n, v, SC_ESCAPE).act == A_BACK);
    // Your name.
    n.setSel(rowOf(n, v, R_NAME), v);
    n.f.name.clear();
    ctx.check("typed into YOUR NAME, in capitals", n.text("bread_2", v).act == A_CHANGED && n.f.name == "BREAD_2");
    press(n, v, SC_BACKSPACE);
    ctx.check("Backspace edits it (not back)", n.page == P_HOME && n.f.name == "BREAD_");
    ctx.check("W and S type in a field (not move)", n.text("ws", v).act == A_CHANGED && n.f.name == "BREAD_WS" && focused(n, v, R_NAME));
    press(n, v, SC_DOWN);
    ctx.check("Down moves on", !focused(n, v, R_NAME));
    View off;
    off.codesAvailable = false;
    n.open(P_HOME, off);
    ctx.check("no join-code server: the row says so and does nothing", n.rows(off)[0].dim && press(n, off, SC_RETURN).act == A_NONE && n.page == P_HOME);
    View rec;
    rec.recent = {"KX7-M4Q", "10.0.0.5:27015"};
    n.open(P_HOME, rec);
    const Result r1 = n.choose(rowOf(n, rec, R_RECENT0), rec), r2 = n.choose(rowOf(n, rec, R_RECENT0 + 1), rec);
    ctx.check("recent: a code joins by code, an address by address", r1.act == A_JOIN_CODE && r1.text == "KX7M4Q" && r2.act == A_JOIN_ADDRESS && r2.text == "10.0.0.5:27015");
}

SCENARIO(mpCode, "ui/mpmenu-code", "the code box: typing ignores case, dashes and the confusable letters; paste; a pad spins the letters; JOIN only when whole") {
    View v;
    Nav n;
    Clip clip;
    n.open(P_CODE, v);
    n.text("kx7-m", v);
    ctx.check("typed, dash dropped, capitals", n.f.code == "KX7M");
    n.text("O", v);
    ctx.check("an O isn't taken (no O in codes)", n.f.code == "KX7M");
    n.setSel(rowOf(n, v, R_GO), v);
    ctx.check("JOIN does nothing until it's whole", press(n, v, SC_RETURN).act == A_NONE && n.rows(v)[rowOf(n, v, R_GO)].dim);
    n.setSel(rowOf(n, v, R_CODEBOX), v);
    n.text("4qz", v);
    ctx.check("six at most", n.f.code == "KX7M4Q");
    const Result r = press(n, v, SC_RETURN);
    ctx.check("Enter in a whole box joins", r.act == A_JOIN_CODE && r.text == "KX7M4Q" && r.label == "KX7-M4Q");
    press(n, v, SC_BACKSPACE);
    ctx.check("Backspace takes the last back (and stays on the page)", n.f.code == "KX7M4" && n.page == P_CODE);
    clip.text = "  abc-def\n";
    press(n, v, SC_V, true);
    ctx.check("Ctrl+V pastes a code", n.f.code == "ABCDEF");
    clip.text = "xyz 234";
    n.choose(rowOf(n, v, R_PASTE), v);
    ctx.check("PASTE too, and then JOIN has the focus", n.f.code == "XYZ234" && focused(n, v, R_GO));
    // A pad: Enter on a part code: up and down spin the letter, left and right pick the box.
    n.f.code.clear();
    n.setSel(rowOf(n, v, R_CODEBOX), v);
    press(n, v, SC_RETURN);
    ctx.check("Enter on an empty box: letter mode", n.codeEdit);
    press(n, v, SC_UP);
    press(n, v, SC_UP);
    ctx.check("up: the letters in order", n.f.code == std::string(1, rv::kAlphabet[2]));
    press(n, v, SC_DOWN);
    ctx.check("down: back one", n.f.code == std::string(1, rv::kAlphabet[1]));
    press(n, v, SC_RIGHT);
    press(n, v, SC_DOWN);
    ctx.check("right: the next box, down from A wraps to 9", n.f.code.size() == 2 && n.f.code[1] == '9');
    press(n, v, SC_RETURN);
    ctx.check("Enter: done", !n.codeEdit && n.page == P_CODE);
    ctx.check("Esc then leaves the page", press(n, v, SC_ESCAPE).act == A_NONE && n.page == P_HOME);
}

SCENARIO(mpJoining, "ui/mpmenu-joining", "the joining card: what's happening, then what went wrong in plain words, a password box when that's it, RETRY and BACK") {
    View v;
    Nav n;
    n.open(P_CODE, v);
    n.showJoining();
    v.joining = true, v.byCode = true, v.joinLabel = "CODE KX7-M4Q", v.joinStep = NetSession::JS_RELAY;
    ctx.check("connecting: says how", n.rows(v)[0].sub == "CONNECTING THROUGH THE RELAY..." && rowOf(n, v, R_CANCEL) >= 0);
    ctx.check("Esc cancels", press(n, v, SC_ESCAPE).act == A_CANCEL_JOIN);
    v.joinError = "no game has that code";
    v.joinFail = NetSession::JF_NOT_FOUND;
    ctx.check("expired code: plain words", failText(v).find("NO GAME HAS THAT CODE") == 0 && rowOf(n, v, R_RETRY) >= 0);
    v.joinFail = NetSession::JF_FULL, v.failPlayers = 8, v.failMax = 8;
    ctx.check("full: with how full", failText(v) == "THAT GAME IS FULL (8 OF 8)");
    v.joinFail = NetSession::JF_OLDER;
    ctx.check("wrong version: update", failText(v).find("UPDATE TO") != std::string::npos);
    v.joinFail = NetSession::JF_OTHER, v.joinError = "rejected: version mismatch: update to 0.4.1 or newer", v.byCode = false;
    ctx.check("wrong version by address too", failText(v) == "WRONG VERSION: UPDATE TO 0.4.1 OR NEWER");
    v.needPassword = true, v.joinError = "WRONG PASSWORD";
    n.sel = 0;
    n.setSel(rowOf(n, v, R_PASSWORD), v);
    ctx.check("a password: the card asks for it", focused(n, v, R_PASSWORD) && failText(v) == "WRONG PASSWORD");
    n.text("Hunter 2", v);
    ctx.check("typed as typed (no spaces in passwords)", n.f.password == "Hunter2");
    ctx.check("Enter in it retries", press(n, v, SC_RETURN).act == A_RETRY);
    v.joining = false;
    press(n, v, SC_ESCAPE);
    ctx.check("back to the page it came from", n.page == P_CODE);
}

SCENARIO(mpFriends, "ui/mpmenu-friends", "in the game: OPEN TO FRIENDS, then the code with COPY, players with ping and direct or relayed, a kick takes two presses") {
    View v;
    v.inGame = true;
    Nav n;
    Clip clip;
    n.open(P_FRIENDS, v);
    ctx.check("offline: OPEN TO FRIENDS first", focused(n, v, R_OPEN) && press(n, v, SC_RETURN).act == A_OPEN_FRIENDS);
    ctx.check("and a way to join someone instead", rowOf(n, v, R_JOIN_PAGE) >= 0);
    v.role = 1, v.codeState = NetSession::CODE_OPEN, v.code = "KX7M4Q";
    v.players = {{0, "BREAD", 0, 0, true}, {1, "BOB", 42, NetSession::LINK_DIRECT, false}, {2, "ANN", 120, NetSession::LINK_RELAYED, false}};
    n.open(P_FRIENDS, v);
    ctx.check("open: COPY THE CODE first, with the code", focused(n, v, R_COPY) && n.rows(v)[n.sel].sub == "KX7-M4Q");
    ctx.check("COPY puts it on the clipboard", press(n, v, SC_RETURN).act == A_COPY_CODE && clip.text == "KX7-M4Q" && n.copied);
    const int bob = rowOf(n, v, R_PLAYER0 + 1), ann = rowOf(n, v, R_PLAYER0 + 2);
    ctx.check("players: ping and link", bob >= 0 && n.rows(v)[bob].sub.find("42 MS  DIRECT") == 0 && n.rows(v)[ann].sub.find("120 MS  RELAYED") == 0);
    ctx.check("the host isn't kickable (not a selectable row)", n.rows(v)[rowOf(n, v, R_PLAYER0)].kind == INFO);
    ctx.check("kick: the first press asks", n.choose(bob, v).act == A_NONE && n.kickArm == 1);
    const Result k = n.choose(bob, v);
    ctx.check("the second kicks", k.act == A_KICK && k.arg == 1);
    n.choose(ann, v);
    n.choose(rowOf(n, v, R_COPY), v);
    ctx.check("anything else in between disarms it", n.kickArm == -1);
    n.setSel(rowOf(n, v, R_MAXP), v);
    n.f.maxPlayers = 8;
    const Result mp = press(n, v, SC_LEFT);
    ctx.check("max players: left lowers it, live", mp.act == A_SETTINGS_LIVE && n.f.maxPlayers == 7);
    for (int i = 0; i < 10; i++) press(n, v, SC_LEFT);
    ctx.check("never below who's in", n.f.maxPlayers == 3);
    n.setSel(rowOf(n, v, R_PWON), v);
    ctx.check("password on: a field for it shows", press(n, v, SC_RETURN).act == A_SETTINGS_LIVE && n.f.passwordOn && rowOf(n, v, R_HOSTPW) >= 0);
    ctx.check("stop hosting is there", rowOf(n, v, R_STOP) >= 0 && rowOf(n, v, R_CLOSE) >= 0);
    View g;
    g.inGame = true, g.role = 2, g.players = v.players;
    n.open(P_FRIENDS, g);
    ctx.check("a guest: no code, no kick, LEAVE", rowOf(n, g, R_OPEN) < 0 && rowOf(n, g, R_COPY) < 0 && rowOf(n, g, R_LEAVE) >= 0 && n.rows(g)[rowOf(n, g, R_PLAYER0 + 1)].kind == INFO);
    ctx.check("Esc leaves", press(n, g, SC_ESCAPE).act == A_BACK);
}
