// One text-field helper for every typed-in field in the game (menu, chat, editor, model lab): the
// cursor and selection, Ctrl+V / Shift+Insert paste, Ctrl+C / X / A, Home / End / arrows /
// Delete, and what each kind of field accepts. No GL and no SDL window: unit-testable.
#pragma once
#include <string>

namespace te {

// What a field holds decides what it takes (and whether the game's font can draw it: it has no
// lowercase, so everything but passwords and addresses is typed in capitals).
enum class Kind {
    Name,      // a player name: A-Z 0-9 . - _ (capitals)
    Session,   // a game name: the same and spaces inside (capitals)
    Address,   // host or host:port: letters digits . - _ : [ ] (lowercase)
    Port,      // digits
    Password,  // printable ASCII exactly as typed or pasted, no spaces (the settings file is read by word)
    Chat,      // what the font can draw, capitals
    Level,     // a level name: A-Z 0-9 . - _ and spaces inside (capitals)
    Note,      // printable ASCII, capitals
};

struct Rules {
    Kind kind = Kind::Chat;
    size_t maxLen = 32;
};

// Where the cursor is and what is selected. npos: the cursor at the end / nothing selected.
struct Edit {
    size_t cur = std::string::npos;
    size_t anchor = std::string::npos;
    bool hasSel() const { return anchor != std::string::npos && anchor != cur; }
    void reset() { cur = anchor = std::string::npos; }
};

// A key (or an unwanted character) mapped for the field: 0 when it doesn't take it.
char mapChar(char c, Kind k);
// Typed or pasted text made fit for the field (a paste is also trimmed, and its newlines and control characters go).
std::string clean(const std::string& in, Kind k, bool pasted);
// Put `in` at the cursor (over the selection), up to the field's length. True when the text changed.
bool insert(std::string& v, Edit& e, const std::string& in, const Rules& r, bool pasted = false);
// Select everything / put the cursor at the end.
void selectAll(const std::string& v, Edit& e);
size_t cursorOf(const std::string& v, const Edit& e);
// The selected range (cursor and anchor in order); false when nothing is selected.
bool selection(const std::string& v, const Edit& e, size_t& from, size_t& to);

// The text with the cursor drawn in at its place: an underscore, or a space when it blinks off (the same width either way).
std::string withCursor(const std::string& v, const Edit& e, bool on);

struct KeyResult {
    bool handled = false;  // the field used the key (it is not for the game's keybinds or the page)
    bool changed = false;  // the text changed
};
// An editing key while the field has focus. `canCopy`: Ctrl+C / X may put the text on the clipboard (not a hidden password).
KeyResult key(std::string& v, Edit& e, int scancode, bool ctrl, bool shift, const Rules& r, bool canCopy = true);

// The system clipboard: the game points the hooks at SDL's (main.cpp); tests point `fakeClipboard` at a string instead.
extern std::string* fakeClipboard;
extern std::string (*clipboardGetHook)();
extern void (*clipboardSetHook)(const std::string&);
std::string clipboardGet();
void clipboardSet(const std::string& s);

}  // namespace te
