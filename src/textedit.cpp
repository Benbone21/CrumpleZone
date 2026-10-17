#include "textedit.h"
#include "sdl_min.h"  // (the scancodes only: no SDL calls here, the tests don't link it)
#include <algorithm>
#include <cctype>

namespace te {

std::string* fakeClipboard = nullptr;
std::string (*clipboardGetHook)() = nullptr;
void (*clipboardSetHook)(const std::string&) = nullptr;

std::string clipboardGet() {
    if (fakeClipboard) return *fakeClipboard;
    return clipboardGetHook ? clipboardGetHook() : "";
}

void clipboardSet(const std::string& s) {
    if (fakeClipboard) *fakeClipboard = s;
    else if (clipboardSetHook) clipboardSetHook(s);
}

char mapChar(char c, Kind k) {
    const unsigned char u = (unsigned char)c;
    if (u < 32 || u > 126) return 0;
    const bool letter = std::isalpha(u), digit = std::isdigit(u);
    const char up = (char)std::toupper(u);
    switch (k) {
        case Kind::Name: return letter ? up : (digit || c == '.' || c == '-' || c == '_') ? c : 0;
        case Kind::Session:
        case Kind::Level: return letter ? up : (digit || c == '.' || c == '-' || c == '_' || c == ' ') ? c : 0;
        case Kind::Address: return letter ? (char)std::tolower(u) : (digit || c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']') ? c : 0;
        case Kind::Port: return digit ? c : 0;
        case Kind::Password: return c == ' ' ? 0 : c;
        case Kind::Chat: return letter ? up : (digit || std::string(" .,:-+/%()[]!?'=<>_*#$").find(c) != std::string::npos) ? c : 0;
        case Kind::Note: return letter ? up : c;
    }
    return 0;
}

std::string clean(const std::string& in, Kind k, bool pasted) {
    size_t a = 0, b = in.size();
    if (pasted) {  // (what comes with a copy: the newline at the end, spaces around)
        while (a < b && (unsigned char)in[a] <= ' ') a++;
        while (b > a && (unsigned char)in[b - 1] <= ' ') b--;
    }
    std::string out;
    for (size_t i = a; i < b; i++) {
        const char c = pasted && (in[i] == '\n' || in[i] == '\r' || in[i] == '\t') ? (k == Kind::Password || k == Kind::Address || k == Kind::Port || k == Kind::Name ? 0 : ' ') : in[i];
        const char m = c ? mapChar(c, k) : 0;
        if (m) out += m;
    }
    return out;
}

size_t cursorOf(const std::string& v, const Edit& e) { return std::min(e.cur, v.size()); }

bool selection(const std::string& v, const Edit& e, size_t& from, size_t& to) {
    if (e.anchor == std::string::npos) return false;
    const size_t c = cursorOf(v, e), a = std::min(e.anchor, v.size());
    if (a == c) return false;
    from = std::min(a, c), to = std::max(a, c);
    return true;
}

void selectAll(const std::string& v, Edit& e) {
    e.anchor = 0;
    e.cur = v.size();
}

static bool eraseSel(std::string& v, Edit& e) {
    size_t f, t;
    if (!selection(v, e, f, t)) {
        e.anchor = std::string::npos;
        return false;
    }
    v.erase(f, t - f);
    e.cur = f;
    e.anchor = std::string::npos;
    return true;
}

bool insert(std::string& v, Edit& e, const std::string& in, const Rules& r, bool pasted) {
    std::string add = clean(in, r.kind, pasted);
    const std::string before = v;
    eraseSel(v, e);
    size_t c = cursorOf(v, e);
    if (c == 0)  // (no space first)
        while (!add.empty() && add[0] == ' ') add.erase(0, 1);
    if (v.size() < r.maxLen) add = add.substr(0, r.maxLen - v.size());
    else add.clear();
    v.insert(c, add);
    e.cur = c + add.size();
    e.anchor = std::string::npos;
    return v != before;
}

std::string withCursor(const std::string& v, const Edit& e, bool on) {
    const size_t c = cursorOf(v, e);
    return v.substr(0, c) + (on ? "_" : " ") + v.substr(c);
}

KeyResult key(std::string& v, Edit& e, int sc, bool ctrl, bool shift, const Rules& r, bool canCopy) {
    KeyResult res;
    const size_t n = v.size();
    size_t c = cursorOf(v, e);
    auto copyPart = [&] {
        size_t f, t;
        if (!selection(v, e, f, t)) f = 0, t = n;  // (nothing selected: the whole field)
        if (canCopy && t > f) clipboardSet(v.substr(f, t - f));
    };
    auto move = [&](size_t to) {
        if (shift) {
            if (e.anchor == std::string::npos) e.anchor = c;
        } else {
            e.anchor = std::string::npos;
        }
        e.cur = to;
    };
    res.handled = true;
    if (ctrl && sc == SC_A) selectAll(v, e);
    else if (ctrl && (sc == SC_C || sc == SC_INSERT)) copyPart();
    else if (ctrl && sc == SC_X) {
        if (canCopy) {
            copyPart();
            size_t f, t;
            if (!selection(v, e, f, t)) selectAll(v, e);
            res.changed = eraseSel(v, e);
        }
    } else if ((ctrl && sc == SC_V) || (shift && sc == SC_INSERT)) res.changed = insert(v, e, clipboardGet(), r, true);
    else if (ctrl) res.handled = sc != SC_RETURN && sc != SC_KP_ENTER && sc != SC_ESCAPE;  // (a Ctrl chord is never the game's)
    else if (sc == SC_BACKSPACE) {
        if (eraseSel(v, e)) res.changed = true;
        else if (c > 0) v.erase(c - 1, 1), e.cur = c - 1, res.changed = true;
    } else if (sc == SC_DELETE) {
        if (eraseSel(v, e)) res.changed = true;
        else if (c < n) v.erase(c, 1), e.cur = c, res.changed = true;
    } else if (sc == SC_LEFT) {
        size_t f, t;
        if (!shift && selection(v, e, f, t)) e.cur = f, e.anchor = std::string::npos;
        else move(c > 0 ? c - 1 : 0);
    } else if (sc == SC_RIGHT) {
        size_t f, t;
        if (!shift && selection(v, e, f, t)) e.cur = t, e.anchor = std::string::npos;
        else move(c < n ? c + 1 : n);
    } else if (sc == SC_HOME) move(0);
    else if (sc == SC_END) move(n);
    else res.handled = false;
    return res;
}

}  // namespace te
