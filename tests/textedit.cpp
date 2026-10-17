// The shared text-field helper (textedit.h), no SDL window or GL: insert at the cursor, the per-field
// filters, length, trimming, selection, paste / copy / cut, and what a hidden password refuses.
#include <string>

#include "../src/sdl_min.h"
#include "../src/textedit.h"
#include "harness.h"

namespace {
using namespace te;

struct Clip {  // (the clipboard as a string)
    std::string text;
    Clip() { fakeClipboard = &text; }
    ~Clip() { fakeClipboard = nullptr; }
};
KeyResult press(std::string& v, Edit& e, int sc, Kind k, size_t max, bool ctrl = false, bool shift = false, bool canCopy = true) {
    return key(v, e, sc, ctrl, shift, {k, max}, canCopy);
}
}  // namespace

SCENARIO(teInsert, "textedit/insert-and-edit", "typed text goes in at the cursor; Backspace, Delete, Home, End and the arrows edit it") {
    std::string v = "ABEF";
    Edit e;
    e.cur = 2;
    ctx.check("inserted, in capitals", insert(v, e, "cd", {Kind::Name, 16}) && v == "ABCDEF" && e.cur == 4);
    press(v, e, SC_LEFT, Kind::Name, 16);
    press(v, e, SC_BACKSPACE, Kind::Name, 16);
    ctx.check("backspace before the cursor", v == "ABDEF");
    press(v, e, SC_HOME, Kind::Name, 16);
    press(v, e, SC_DELETE, Kind::Name, 16);
    ctx.check("home, then delete", v == "BDEF");
    press(v, e, SC_END, Kind::Name, 16);
    insert(v, e, "Z", {Kind::Name, 16});
    ctx.check("end, then typing", v == "BDEFZ");
    ctx.check("no leading space, and the cursor drawn in", [&] {
        std::string s;
        Edit f;
        insert(s, f, "  A B", {Kind::Session, 24});
        f.cur = 1;
        return s == "A B" && withCursor(s, f, true) == "A_ B" && withCursor(s, f, false) == "A  B";
    }());
}

SCENARIO(teFilters, "textedit/filters", "each kind of field takes what it can show and no more; a paste is trimmed and has no newlines") {
    ctx.check("port: digits", clean("12ab-3", Kind::Port, true) == "123");
    ctx.check("address: lowercase host:port, trimmed", clean("  Play.Example.com:25565 \n", Kind::Address, true) == "play.example.com:25565");
    ctx.check("address: no spaces inside", clean("a b", Kind::Address, true) == "ab");
    ctx.check("game name: capitals, spaces kept", clean("my game !", Kind::Session, false) == "MY GAME ");
    ctx.check("password: exact case and symbols, the copied newline gone", clean("Pa$$w0rd!\n", Kind::Password, true) == "Pa$$w0rd!");
    ctx.check("password: no spaces (the settings file is read by word)", clean("Pa ss", Kind::Password, true) == "Pass");
    ctx.check("chat: a newline is a space", clean("line one\nline two", Kind::Chat, true) == "LINE ONE LINE TWO");
    ctx.check("note: control characters go", clean("tab\there\x01", Kind::Note, true) == "TAB HERE");
    ctx.check("what the font can't draw goes", clean("caf\xc3\xa9", Kind::Name, false) == "CAF");
    std::string v = "ABC";
    Edit e;
    insert(v, e, "DEFGHIJ", {Kind::Name, 5});
    ctx.check("clipped to the field's length", v == "ABCDE" && !insert(v, e, "X", {Kind::Name, 5}));
    v = "12";
    e.reset();
    insert(v, e, "123456789", {Kind::Port, 5});
    ctx.check("the port takes five digits", v == "12123");
}

SCENARIO(tePaste, "textedit/paste", "Ctrl+V and Shift+Insert paste at the cursor or over the selection") {
    Clip c;
    c.text = "  host.example:7777\r\n";
    std::string v = "x";
    Edit e;
    e.cur = 0;
    KeyResult k = press(v, e, SC_V, Kind::Address, 60, true);
    ctx.check("Ctrl+V: at the cursor, cleaned", k.handled && k.changed && v == "host.example:7777x");
    v.clear();
    e.reset();
    press(v, e, SC_INSERT, Kind::Address, 60, false, true);
    ctx.check("Shift+Insert", v == "host.example:7777");
    c.text = "123456789";
    v.clear();
    e.reset();
    press(v, e, SC_V, Kind::Port, 5, true);
    ctx.check("a port paste: digits, five", v == "12345");
    c.text.clear();
    ctx.check("an empty clipboard changes nothing", !press(v, e, SC_V, Kind::Port, 5, true).changed && v == "12345");
    c.text = "NEW";
    v = "OLD VALUE";
    e.reset();
    press(v, e, SC_A, Kind::Session, 24, true);
    const bool sel = e.hasSel();
    press(v, e, SC_V, Kind::Session, 24, true);
    ctx.check("Ctrl+A then paste replaces it all", sel && v == "NEW" && !e.hasSel());
    c.text = "Hunter2!x\n";
    v.clear();
    e.reset();
    press(v, e, SC_V, Kind::Password, 32, true);
    ctx.check("a password paste keeps its characters, loses the newline", v == "Hunter2!x");
}

SCENARIO(teCopy, "textedit/copy-cut-secret", "Ctrl+C and Ctrl+X use the clipboard; a hidden password is never copied") {
    Clip c;
    std::string v = "HELLO";
    Edit e;
    ctx.check("Ctrl+C with nothing selected copies the field", press(v, e, SC_C, Kind::Name, 16, true).handled && c.text == "HELLO");
    e.cur = 5;
    press(v, e, SC_LEFT, Kind::Name, 16, false, true);
    press(v, e, SC_LEFT, Kind::Name, 16, false, true);
    press(v, e, SC_C, Kind::Name, 16, true);
    ctx.check("Shift+Left selects, Ctrl+C copies the selection", c.text == "LO");
    press(v, e, SC_X, Kind::Name, 16, true);
    ctx.check("Ctrl+X cuts it", v == "HEL" && c.text == "LO");
    press(v, e, SC_A, Kind::Name, 16, true);
    press(v, e, SC_X, Kind::Name, 16, true);
    ctx.check("select all, cut", v.empty() && c.text == "HEL");
    c.text = "before";
    v = "secret";
    e.reset();
    press(v, e, SC_A, Kind::Password, 32, true, false, false);
    const bool h = press(v, e, SC_C, Kind::Password, 32, true, false, false).handled;
    press(v, e, SC_X, Kind::Password, 32, true, false, false);
    ctx.check("a hidden password: copy and cut do nothing (and are the field's)", h && c.text == "before" && v == "secret");
    press(v, e, SC_C, Kind::Password, 32, true, false, true);
    ctx.check("with SHOW PASSWORD on it copies", c.text == "secret");
}

SCENARIO(teChords, "textedit/keys-stay-in-the-field", "a Ctrl chord in a field is the field's (never a game key); Enter, Esc and Up are left to the page") {
    std::string v = "A";
    Edit e;
    ctx.check("Ctrl+W is swallowed", press(v, e, SC_W, Kind::Name, 16, true).handled);
    ctx.check("Ctrl+V with nothing to paste is swallowed too", press(v, e, SC_V, Kind::Name, 16, true).handled);
    ctx.check("Enter, Esc and Up are not", !press(v, e, SC_RETURN, Kind::Name, 16).handled && !press(v, e, SC_ESCAPE, Kind::Name, 16).handled && !press(v, e, SC_UP, Kind::Name, 16).handled);
}
