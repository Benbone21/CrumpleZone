// The multiplayer pages: see mpmenu.h.
#include "mpmenu.h"

#include <algorithm>
#include <cctype>

#include "net.h"
#include "rendezvous.h"
#include "sdl_min.h"

namespace mpmenu {

namespace {
constexpr int K_W = 26, K_S = 22, K_A = 4, K_D = 7;
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string linkText(int link) { return link == NetSession::LINK_RELAYED ? "RELAYED" : link == NetSession::LINK_DIRECT ? "DIRECT" : "BY ADDRESS"; }
bool selectable(const Row& r) { return r.kind != INFO; }
}  // namespace

std::string failText(const View& v) {
    if (v.needPassword) return upper(v.joinError.empty() ? "THIS GAME NEEDS A PASSWORD" : v.joinError);
    switch (v.joinFail) {
        case NetSession::JF_NOT_A_CODE: return "THAT ISN'T A CODE: SIX LETTERS AND DIGITS, LIKE KX7-M4Q";
        case NetSession::JF_NOT_FOUND: return "NO GAME HAS THAT CODE. CODES STOP WORKING WHEN THE HOST CLOSES THE GAME: ASK FOR A NEW ONE";
        case NetSession::JF_FULL:
            return v.failMax > 0 ? "THAT GAME IS FULL (" + std::to_string(v.failPlayers) + " OF " + std::to_string(v.failMax) + ")" : "THAT GAME IS FULL";
        case NetSession::JF_OLDER: return std::string("YOUR GAME IS OLDER THAN YOUR FRIEND'S: UPDATE TO ") + net::kRelease + " OR NEWER";
        case NetSession::JF_NEWER: return "YOUR FRIEND'S GAME IS OLDER THAN YOURS: THEY NEED TO UPDATE";
        case NetSession::JF_BUSY: return "TOO MANY TRIES: WAIT A MINUTE";
        case NetSession::JF_BACKEND: return upper(v.joinError.empty() ? "CAN'T REACH THE JOIN-CODE SERVER. CHECK YOUR INTERNET, OR JOIN BY ADDRESS" : v.joinError);
        case NetSession::JF_NO_PATH: return "COULDN'T CONNECT TO YOUR FRIEND'S GAME";
        default: break;
    }
    std::string e = upper(v.joinError);
    if (e.find("VERSION MISMATCH") != std::string::npos) return e.find("UPDATE TO") != std::string::npos ? "WRONG VERSION: " + e.substr(e.find("UPDATE TO")) : "WRONG VERSION: THE HOST NEEDS TO UPDATE";
    if (e.find("SESSION FULL") != std::string::npos) return "THAT GAME IS FULL";
    return e.empty() ? "COULDN'T JOIN" : e;
}

std::string stepText(const View& v) {
    switch (v.joinStep) {
        case NetSession::JS_FINDING: return "FINDING THE GAME...";
        case NetSession::JS_CONNECTING: return "CONNECTING...";
        case NetSession::JS_RELAY: return "CONNECTING THROUGH THE RELAY...";
        case NetSession::JS_LOADING: return "LOADING THE WORLD...";
        default: return "CONNECTING...";
    }
}

void Nav::open(Page p, const View& v) {
    page = p;
    sel = 0;
    codeEdit = false;
    kickArm = -1;
    copied = false;
    focusText = -1;
    hits.clear();
    fixSel(v);
}

void Nav::showJoining() {
    if (page != P_JOINING) joinFrom = page;
    page = P_JOINING;
    sel = 0;
    codeEdit = false;
    focusText = -1;
}

std::vector<Row> Nav::rows(const View& v) const {
    std::vector<Row> r;
    auto button = [&](int id, const std::string& label, const std::string& sub = "", bool dim = false) { r.push_back({id, BUTTON, label, "", sub, dim}); };
    auto field = [&](int id, const std::string& label, const std::string& value, const std::string& sub = "") { r.push_back({id, TEXT, label, value, sub}); };
    auto choice = [&](int id, const std::string& label, const std::string& value, const std::string& sub = "") { r.push_back({id, CHOICE, label, value, sub}); };
    auto info = [&](const std::string& label, const std::string& sub = "") { r.push_back({-1, INFO, label, "", sub}); };
    const std::string stars(f.password.size(), '*'), hostStars(f.hostPassword.size(), '*');
    switch (page) {
        case P_HOME:
            button(R_CODE_PAGE, "JOIN WITH CODE", v.codesAvailable ? "A FRIEND'S 6-LETTER CODE" : "JOIN CODES AREN'T SET UP IN THIS GAME", !v.codesAvailable);
            button(R_ADDRESS_PAGE, "JOIN BY ADDRESS", "THE IP:PORT OF A SERVER OR A FRIEND'S GAME");
            button(R_LAN_PAGE, "ON YOUR NETWORK",
                   v.lan.empty() ? (v.searching > 0 ? "LOOKING..." : "GAMES ON THIS NETWORK") : std::to_string(v.lan.size()) + (v.lan.size() == 1 ? " GAME FOUND" : " GAMES FOUND"));
            if (!v.inGame) button(R_HOST_PAGE, "HOST A PRIVATE GAME", "PICK THE MAP, A PASSWORD AND THE PORT");
            field(R_NAME, "YOUR NAME", f.name, "WHAT THE OTHERS SEE");
            for (int i = 0; i < (int)v.recent.size() && i < 3; i++) button(R_RECENT0 + i, "RECENT: " + v.recent[i], "JOIN AGAIN");
            button(R_BACK, "BACK");
            break;
        case P_CODE:
            r.push_back({R_CODEBOX, TEXT, "CODE", f.code, codeEdit ? "UP DOWN: THE LETTER   LEFT RIGHT: THE BOX" : "TYPE OR PASTE IT"});
            field(R_PASSWORD, "PASSWORD", stars, "ONLY IF THE GAME HAS ONE");
            button(R_GO, "JOIN", f.code.size() == (size_t)rv::kCodeLen ? "" : "THE CODE HAS SIX LETTERS AND DIGITS", f.code.size() != (size_t)rv::kCodeLen);
            button(R_PASTE, "PASTE", "THE CODE FROM THE CLIPBOARD");
            button(R_BACK, "BACK");
            break;
        case P_ADDRESS:
            field(R_ADDRESS, "ADDRESS", f.address, "IP OR IP:PORT (27015 IF NONE)");
            field(R_PASSWORD, "PASSWORD", stars, "ONLY IF THE GAME HAS ONE");
            button(R_GO, "JOIN", "", f.address.empty());
            button(R_BACK, "BACK");
            break;
        case P_LAN:
            if (v.lan.empty()) info(v.searching > 4 ? "NOTHING YET" : "LOOKING...", "IS YOUR FRIEND HOSTING ON THIS NETWORK? A FIREWALL CAN HIDE IT: JOIN BY ADDRESS");
            for (int i = 0; i < (int)v.lan.size() && i < 8; i++) button(R_FOUND0 + i, v.lan[i].title, v.lan[i].detail, !v.lan[i].ok);
            field(R_PASSWORD, "PASSWORD", stars, "ONLY IF THE GAME HAS ONE");
            button(R_BACK, "BACK");
            break;
        case P_HOST:
            choice(R_MAP, "MAP", v.mapNames.empty() ? "" : v.mapNames[clampi(f.map, 0, (int)v.mapNames.size() - 1)]);
            field(R_SESSION, "GAME NAME", f.session, "WHAT THE OTHERS' LISTS CALL IT");
            field(R_HOSTPW, "PASSWORD", hostStars, f.hostPassword.empty() ? "NONE: ANYONE WITH THE ADDRESS OR CODE GETS IN" : "JOINERS MUST TYPE IT");
            choice(R_MAXP, "MAX PLAYERS", std::to_string(f.maxPlayers));
            field(R_PORT, "PORT", f.port, "TCP AND UDP: FORWARD IT FOR FRIENDS BY ADDRESS");
            choice(R_ALSOCODE, "ALSO GIVE A JOIN CODE", f.alsoCode && v.codesAvailable ? "ON" : "OFF", v.codesAvailable ? "FRIENDS JOIN WITHOUT PORT FORWARDING" : "JOIN CODES AREN'T SET UP");
            button(R_GO, "START HOSTING");
            button(R_BACK, "BACK");
            break;
        case P_JOINING:
            if (v.joining && v.joinError.empty()) {
                info("JOINING " + upper(v.joinLabel), stepText(v));
                button(R_CANCEL, "CANCEL");
            } else {
                info("COULDN'T JOIN " + upper(v.joinLabel), failText(v));
                if (v.needPassword) field(R_PASSWORD, "PASSWORD", stars, "TYPE IT, THEN RETRY");
                button(R_RETRY, "RETRY");
                button(R_BACK, "BACK");
            }
            break;
        case P_FRIENDS: {
            const bool host = v.role == 1, client = v.role == 2;
            const bool open = host && v.codeState == NetSession::CODE_OPEN;
            if (!client) {
                if (open) {
                    button(R_COPY, copied ? "COPIED" : "COPY THE CODE", rv::formatCode(v.code));
                    button(R_CLOSE, "CLOSE TO FRIENDS", "THE CODE STOPS WORKING: WHOEVER'S IN STAYS");
                } else if (v.codeState == NetSession::CODE_ASKING) {
                    info("GETTING A CODE...");
                } else {
                    button(R_OPEN, "OPEN TO FRIENDS",
                           !v.codesAvailable ? "JOIN CODES AREN'T SET UP: FRIENDS CAN JOIN BY ADDRESS"
                           : v.codeState == NetSession::CODE_FAILED ? upper(v.codeError)
                                                                    : "GET A CODE: FRIENDS JOIN THIS WORLD, EDITS AND ALL");
                }
                choice(R_PWON, "PASSWORD", f.passwordOn ? "ON" : "OFF", f.passwordOn ? "JOINERS MUST TYPE IT" : "ANYONE WITH THE CODE GETS IN");
                if (f.passwordOn) field(R_HOSTPW, "THE PASSWORD", hostStars);
                choice(R_MAXP, "MAX PLAYERS", std::to_string(f.maxPlayers));
            }
            if (host || client) {
                info("PLAYERS", upper(v.sessionTitle) + (v.mapName.empty() ? "" : "  " + upper(v.mapName)));
                for (const View::Player& p : v.players) {
                    std::string sub = p.id == 0 ? "HOST" : std::to_string(p.pingMs) + " MS  " + linkText(p.link);
                    if (host && p.id != 0) sub += kickArm == p.id ? "   PRESS AGAIN TO KICK" : "   PRESS TO KICK";
                    r.push_back({R_PLAYER0 + p.id, host && p.id != 0 ? PLAYER : INFO, upper(p.name) + (p.self ? " (YOU)" : ""), "", sub});
                }
            }
            if (host) button(R_STOP, "STOP HOSTING", "EVERYONE ELSE IS DROPPED");
            if (client) button(R_LEAVE, "LEAVE THE GAME");
            if (!host && !client) button(R_JOIN_PAGE, "JOIN A GAME", "WITH A CODE, AN ADDRESS, OR ON YOUR NETWORK");
            button(R_BACK, "BACK");
            break;
        }
    }
    return r;
}

std::string Nav::title(const View& v) const {
    switch (page) {
        case P_HOME: return "MULTIPLAYER";
        case P_CODE: return "JOIN WITH CODE";
        case P_ADDRESS: return "JOIN BY ADDRESS";
        case P_LAN: return "ON YOUR NETWORK";
        case P_HOST: return "HOST A PRIVATE GAME";
        case P_JOINING: return v.joining && v.joinError.empty() ? "JOINING" : "NOT CONNECTED";
        case P_FRIENDS: return v.role == 2 ? "IN " + upper(v.sessionTitle) : "FRIENDS";
    }
    return "";
}

std::string Nav::hint(bool pad, const View& v) const {
    const std::string ok = pad ? "A OK" : "ENTER OK", bk = pad ? "B BACK" : "ESC BACK", mv = pad ? "D-PAD MOVE" : "W S MOVE";
    if (page == P_CODE && codeEdit) return pad ? "D-PAD UP DOWN: LETTER   LEFT RIGHT: BOX   A DONE" : "TYPE THE CODE   UP DOWN: LETTER   ENTER DONE";
    const std::vector<Row> r = rows(v);
    const bool choiceRow = sel >= 0 && sel < (int)r.size() && r[sel].kind == CHOICE;
    return mv + (choiceRow ? (pad ? "   LEFT RIGHT CHANGE" : "   A D CHANGE") : "") + "   " + ok + "   " + bk;
}

void Nav::fixSel(const View& v) {
    const std::vector<Row> r = rows(v);
    if (r.empty()) {
        sel = 0;
        return;
    }
    sel = clampi(sel, 0, (int)r.size() - 1);
    for (int k = 0; k < (int)r.size() && !selectable(r[sel]); k++) sel = (sel + 1) % (int)r.size();
    focusText = r[sel].kind == TEXT ? r[sel].id : -1;
}

void Nav::setSel(int i, const View& v) {
    const std::vector<Row> r = rows(v);
    if (i < 0 || i >= (int)r.size() || !selectable(r[i])) return;
    if (i != sel) ted.reset(), codeEdit = false;
    sel = i;
    focusText = r[sel].kind == TEXT ? r[sel].id : -1;
}

void Nav::move(int dir, const View& v) {
    const std::vector<Row> r = rows(v);
    const int n = (int)r.size();
    if (!n) return;
    int s = sel;
    for (int k = 0; k < n; k++) {
        s = (s + dir + n) % n;
        if (selectable(r[s])) break;
    }
    setSel(s, v);
}

bool Nav::textField(int id, std::string*& value, te::Rules& rules) {
    switch (id) {
        case R_NAME: value = &f.name, rules = {te::Kind::Name, 16}; return true;
        case R_ADDRESS: value = &f.address, rules = {te::Kind::Address, 60}; return true;
        case R_PASSWORD: value = &f.password, rules = {te::Kind::Password, 64}; return true;
        case R_HOSTPW: value = &f.hostPassword, rules = {te::Kind::Password, 64}; return true;
        case R_SESSION: value = &f.session, rules = {te::Kind::Session, 40}; return true;
        case R_PORT: value = &f.port, rules = {te::Kind::Port, 5}; return true;
        default: return false;
    }
}

Result Nav::change(int id, int dir, const View& v) {
    switch (id) {
        case R_MAP:
            if (!v.mapNames.empty()) f.map = (f.map + dir + (int)v.mapNames.size()) % (int)v.mapNames.size();
            return {A_CHANGED};
        case R_MAXP:
            f.maxPlayers = clampi(f.maxPlayers + dir, std::max(2, (int)v.players.size()), 8);
            return {page == P_FRIENDS ? A_SETTINGS_LIVE : A_CHANGED};
        case R_ALSOCODE: f.alsoCode = !f.alsoCode; return {A_CHANGED};
        case R_PWON:
            f.passwordOn = !f.passwordOn;
            return {A_SETTINGS_LIVE};
        default: return {};
    }
}

Result Nav::go(const View& v) {
    switch (page) {
        case P_CODE:
            if (f.code.size() != (size_t)rv::kCodeLen) return {};
            return {A_JOIN_CODE, 0, f.code, rv::formatCode(f.code)};
        case P_ADDRESS:
            if (f.address.empty()) return {};
            return {A_JOIN_ADDRESS, 0, f.address, f.address};
        case P_HOST: {
            if (net::parsePort(f.port, net::kMinHostPort) < 0) f.port = std::to_string(net::kDefaultPort);
            return {A_HOST};
        }
        default: return {};
    }
    (void)v;
}

Result Nav::codeKey(int sc, bool ctrl, const View& v) {
    // The code box: typing fills it (case, spaces and dashes don't matter); Backspace takes the
    // last one back; Ctrl+V pastes; Enter joins once it's whole. With a pad (or Enter on a part
    // code), up and down change the letter in the box at the cursor.
    const std::string alpha = rv::kAlphabet;
    if (ctrl && sc == SC_V) {
        std::string c;
        for (char ch : te::clipboardGet())
            if (rv::codeChar(ch) && c.size() < (size_t)rv::kCodeLen) c += (char)std::toupper((unsigned char)ch);
        if (!c.empty()) f.code = c, codeCursor = std::min((int)c.size(), rv::kCodeLen - 1);
        return {A_CHANGED};
    }
    if (sc == SC_BACKSPACE || sc == SC_DELETE) {
        if (codeEdit && codeCursor < (int)f.code.size() && sc == SC_DELETE) f.code.erase(codeCursor, 1);
        else if (!f.code.empty()) f.code.pop_back(), codeCursor = std::min(codeCursor, std::max(0, (int)f.code.size()));
        return {A_CHANGED};
    }
    if (sc == SC_RETURN || sc == SC_KP_ENTER || sc == SC_SPACE) {
        if (codeEdit) {
            codeEdit = false;
            return {};
        }
        if (f.code.size() == (size_t)rv::kCodeLen) return go(v);
        codeEdit = true;
        codeCursor = std::min((int)f.code.size(), rv::kCodeLen - 1);
        return {};
    }
    if (codeEdit) {
        if (sc == SC_ESCAPE) {
            codeEdit = false;
            return {};
        }
        if (sc == SC_LEFT || sc == K_A) codeCursor = std::max(0, codeCursor - 1);
        else if (sc == SC_RIGHT || sc == K_D) codeCursor = std::min(std::min((int)f.code.size(), rv::kCodeLen - 1), codeCursor + 1);
        else if (sc == SC_UP || sc == SC_DOWN || sc == K_W || sc == K_S) {
            const int dir = sc == SC_UP || sc == K_W ? 1 : -1;
            while ((int)f.code.size() <= codeCursor) f.code += alpha[0];
            size_t at = alpha.find(f.code[codeCursor]);
            if (at == std::string::npos) at = 0;
            f.code[codeCursor] = alpha[(at + alpha.size() + dir) % alpha.size()];
            return {A_CHANGED};
        }
        return {};
    }
    return {A_NONE, -1};  // (not the box's: the page has it)
}

Result Nav::key(int sc, bool ctrl, bool shift, const View& v) {
    const std::vector<Row> r = rows(v);
    fixSel(v);
    const int id = r.empty() ? -1 : r[sel].id;
    if (page == P_CODE && id == R_CODEBOX) {
        const Result cr = codeKey(sc, ctrl, v);
        if (!(cr.act == A_NONE && cr.arg == -1)) return cr;
    }
    // A text field with focus takes its editing keys (arrows within the text, Home, End, Backspace, Ctrl+V...).
    std::string* value = nullptr;
    te::Rules rules;
    if (focusText >= 0 && textField(focusText, value, rules) && sc != SC_UP && sc != SC_DOWN && sc != SC_ESCAPE && sc != SC_RETURN && sc != SC_KP_ENTER &&
        sc != SC_TAB) {
        const bool hidden = focusText == R_PASSWORD || focusText == R_HOSTPW;
        const te::KeyResult k = te::key(*value, ted, sc, ctrl, shift, rules, !hidden);
        if (k.handled) return k.changed ? Result{focusText == R_HOSTPW && page == P_FRIENDS ? A_SETTINGS_LIVE : A_CHANGED} : Result{};
    }
    if (sc == SC_ESCAPE) return back(v);
    if (sc == SC_BACKSPACE && focusText < 0) return back(v);
    if (sc == SC_RETURN || sc == SC_KP_ENTER || (sc == SC_SPACE && focusText < 0)) return choose(sel, v);
    if (sc == SC_UP || (sc == K_W && focusText < 0)) move(-1, v);
    else if (sc == SC_DOWN || sc == SC_TAB || (sc == K_S && focusText < 0)) move(shift && sc == SC_TAB ? -1 : 1, v);
    else if (sc == SC_LEFT || sc == SC_RIGHT || ((sc == K_A || sc == K_D) && focusText < 0)) {
        if (!r.empty() && r[sel].kind == CHOICE) return change(id, sc == SC_LEFT || sc == K_A ? -1 : 1, v);
    }
    return {};
}

Result Nav::text(const std::string& typed, const View& v) {
    const std::vector<Row> r = rows(v);
    fixSel(v);
    if (r.empty()) return {};
    if (page == P_CODE && r[sel].id == R_CODEBOX) {
        bool changed = false;
        for (char ch : typed) {
            if (!rv::codeChar(ch)) continue;  // (spaces, dashes and the confusable letters don't go in)
            const char up = (char)std::toupper((unsigned char)ch);
            if (codeEdit && codeCursor < (int)f.code.size()) {
                f.code[codeCursor] = up;
                codeCursor = std::min(rv::kCodeLen - 1, codeCursor + 1);
            } else if (f.code.size() < (size_t)rv::kCodeLen) {
                f.code += up;
                codeCursor = std::min(rv::kCodeLen - 1, (int)f.code.size());
            }
            changed = true;
        }
        return changed ? Result{A_CHANGED} : Result{};
    }
    std::string* value = nullptr;
    te::Rules rules;
    if (focusText < 0 || !textField(focusText, value, rules)) return {};
    if (te::insert(*value, ted, typed, rules)) return {focusText == R_HOSTPW && page == P_FRIENDS ? A_SETTINGS_LIVE : A_CHANGED};
    return {};
}

Result Nav::back(const View& v) {
    codeEdit = false;
    switch (page) {
        case P_HOME:
            if (v.inGame) {
                open(P_FRIENDS, v);
                return {};
            }
            return {A_BACK};
        case P_FRIENDS: return {A_BACK};
        case P_JOINING:
            if (v.joining && v.joinError.empty()) return {A_CANCEL_JOIN};
            page = joinFrom;
            sel = 0;
            fixSel(v);
            return {A_CANCEL_JOIN};
        default: {
            const Page from = page;
            open(P_HOME, v);
            const std::vector<Row> r = rows(v);
            const int want = from == P_CODE ? R_CODE_PAGE : from == P_ADDRESS ? R_ADDRESS_PAGE : from == P_LAN ? R_LAN_PAGE : R_HOST_PAGE;
            for (int i = 0; i < (int)r.size(); i++)
                if (r[i].id == want) setSel(i, v);
            return {};
        }
    }
}

Result Nav::choose(int i, const View& v) {
    const std::vector<Row> r = rows(v);
    if (i < 0 || i >= (int)r.size() || !selectable(r[i])) return {};
    setSel(i, v);
    const Row& row = r[i];
    const int id = row.id;
    if (id != R_PLAYER0 + kickArm) kickArm = -1;
    if (row.kind == CHOICE) return change(id, 1, v);
    if (row.kind == TEXT) {
        if (page == P_CODE && id == R_CODEBOX) return codeKey(SC_RETURN, false, v);
        // Enter in a field: on to the next row (and from the last field of a join, join).
        if (page == P_JOINING && id == R_PASSWORD) return {A_RETRY};
        if ((page == P_ADDRESS || page == P_CODE) && id == R_PASSWORD) return go(v);
        if (page == P_LAN && id == R_PASSWORD) return {};
        if (page == P_ADDRESS && id == R_ADDRESS && !f.address.empty()) {
            move(1, v);
            return {};
        }
        move(1, v);
        return {};
    }
    if (row.dim && id != R_PLAYER0 + kickArm && row.kind != PLAYER) return {};
    if (id >= R_RECENT0 && id <= R_RECENT2) {
        const std::string what = v.recent[id - R_RECENT0];
        const std::string code = rv::normalizeCode(what);
        if (!code.empty() && what.find('.') == std::string::npos) {
            f.code = code;
            return {A_JOIN_CODE, 0, code, rv::formatCode(code)};
        }
        f.address = what;
        return {A_JOIN_ADDRESS, 0, what, what};
    }
    if (id >= R_FOUND0 && id <= R_FOUND7) {
        const View::Found& g = v.lan[id - R_FOUND0];
        return {A_JOIN_FOUND, 0, g.address, g.title};
    }
    if (id >= R_PLAYER0 && id <= R_PLAYER7) {
        const int who = id - R_PLAYER0;
        if (kickArm == who) {
            kickArm = -1;
            return {A_KICK, who};
        }
        kickArm = who;
        return {};
    }
    switch (id) {
        case R_CODE_PAGE: open(P_CODE, v); return {};
        case R_ADDRESS_PAGE: open(P_ADDRESS, v); return {};
        case R_LAN_PAGE: open(P_LAN, v); return {};
        case R_HOST_PAGE: open(P_HOST, v); return {};
        case R_JOIN_PAGE: open(P_HOME, v); return {};
        case R_GO: return go(v);
        case R_PASTE: {
            const Result res = codeKey(SC_V, true, v);
            for (int k = 0; k < (int)r.size(); k++)
                if (r[k].id == (f.code.size() == (size_t)rv::kCodeLen ? R_GO : R_CODEBOX)) setSel(k, v);
            return res;
        }
        case R_RETRY: return {A_RETRY};
        case R_CANCEL: return {A_CANCEL_JOIN};
        case R_OPEN: return {A_OPEN_FRIENDS};
        case R_CLOSE: return {A_CLOSE_FRIENDS};
        case R_COPY:
            te::clipboardSet(rv::formatCode(v.code));
            copied = true;
            return {A_COPY_CODE};
        case R_STOP: return {A_STOP_HOSTING};
        case R_LEAVE: return {A_LEAVE};
        case R_BACK: return back(v);
        default: return {};
    }
}

int Nav::hitAt(float x, float y) const {
    for (const Hit& h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.row;
    return -1;
}

}  // namespace mpmenu
