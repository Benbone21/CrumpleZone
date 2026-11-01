// Paints the multiplayer pages (mpmenu.h) in the main menu's look (mainmenudraw.cpp): the dark
// shade down the left, a column of rows with the amber focus bar, a panel on the right that
// explains the focused row (or, in FRIENDS, shows the code big), the key hints along the bottom.
#include "mpmenudraw.h"

#include <algorithm>
#include <cmath>

#include "hudkit.h"
#include "net.h"
#include "rendezvous.h"

using namespace hud;

namespace mpmenu {

namespace {
const float kAmber[3] = {1.0f, 0.75f, 0.3f};
const float kGrey[3] = {0.62f, 0.64f, 0.68f};
const float kPanel[3] = {0.03f, 0.035f, 0.045f};

void shade(Renderer& r, int W, int H, float a, bool overGame) {
    const int bands = 96;
    const float w = W * 0.66f, peak = overGame ? 0.82f : 0.7f;
    for (int i = 0; i < bands; i++) {
        const float t = (i + 0.5f) / bands, al = peak * (1 - t * t * 0.85f) * (1 - t);
        const float x0 = std::floor(w * i / bands), x1 = std::floor(w * (i + 1) / bands);
        r.rect(x0, 0, x1 - x0, (float)H, 0.02f, 0.02f, 0.03f, al * a);
    }
    if (overGame) r.rect(0, 0, (float)W, (float)H, 0.0f, 0.0f, 0.0f, 0.25f * a);
    for (int i = 0; i < 10; i++) r.rect(0, H - H * 0.14f * (10 - i) / 10.0f, (float)W, H * 0.14f / 10 + 1, 0.02f, 0.02f, 0.03f, 0.42f * a / 10.0f);
}

// One row: a panel (amber bar when focused), the label, a value on the right, a small second line.
void drawRow(Renderer& r, Nav& nav, const Row& row, int index, float x, float y, float w, float h, float u, float a, bool focus, double clock) {
    const float ts = px(2.0f * u), small = px(1.2f * u);
    if (row.kind == INFO && !(row.id >= R_PLAYER0 && row.id <= R_PLAYER7)) {
        label(r, x + 4 * u, y + 4 * u, px(1.6f * u), row.label, kAmber[0], kAmber[1], kAmber[2], 0.9f * a);
        if (!row.sub.empty()) r.text(x + 4 * u, y + 4 * u + 8 * px(1.6f * u) + 6 * u, small, row.sub, 0.85f, 0.85f, 0.88f, a);
        return;
    }
    if (focus) {
        pill(r, x, y, w, h, kPanel[0], kPanel[1], kPanel[2], 0.82f * a);
        r.rect(x, y + 2 * u, 4 * u, h - 4 * u, kAmber[0], kAmber[1], kAmber[2], a);
    } else {
        r.rect(x, y, w, h, kPanel[0], kPanel[1], kPanel[2], 0.36f * a);
    }
    const float tx = x + 16 * u;
    const bool two = !row.sub.empty();
    const float th = 8 * ts, sh = 8 * small, ty = y + (two ? (h - th - sh - 5 * u) / 2 : (h - th) / 2);
    const float dimA = row.dim ? 0.5f : 1.0f;
    const float lr = focus ? kAmber[0] : 0.9f, lg = focus ? kAmber[1] : 0.9f, lb = focus ? kAmber[2] : 0.92f;
    label(r, tx, ty, ts, row.label, lr, lg, lb, dimA * a);
    if (two) r.text(tx, ty + th + 5 * u, small, row.sub, kGrey[0], kGrey[1], kGrey[2], a);
    // The value: a field (with its cursor when it has focus), or a choice (< VALUE >).
    if (row.kind == TEXT && row.id != R_CODEBOX) {
        const bool blink = std::fmod(clock, 1.0) < 0.55;
        std::string v = row.value;
        if (focus) v += blink ? "_" : " ";
        if (v.empty() || v == " ") v = focus ? v : "-";
        const float vw = r.textWidth(v, ts), vx = x + w - vw - 14 * u;
        r.rect(vx - 8 * u, y + 6 * u, vw + 16 * u, h - 12 * u, 0, 0, 0, (focus ? 0.5f : 0.25f) * a);
        r.text(vx, y + (h - th) / 2, ts, v, 0.95f, 0.95f, 0.97f, a);
    } else if (row.kind == CHOICE) {
        const std::string v = focus ? "< " + row.value + " >" : row.value;
        r.text(x + w - r.textWidth(v, ts) - 14 * u, y + (h - th) / 2, ts, v, focus ? 1.0f : 0.85f, focus ? 0.9f : 0.85f, focus ? 0.7f : 0.88f, a);
    } else if (row.id == R_CODEBOX) {
        // Six boxes, 3 - 3, the cursor's box lit.
        const float bs = std::min(h - 10 * u, 44 * u), gap = 6 * u, dash = 16 * u;
        const float total = 6 * bs + 5 * gap + dash;
        float bx = x + w - total - 14 * u;
        const float by = y + (h - bs) / 2, bt = px(2.6f * u);
        for (int i = 0; i < rv::kCodeLen; i++) {
            if (i == 3) {
                r.rect(bx - gap / 2 + 3 * u, by + bs / 2 - u, dash - 6 * u, 2 * u, 0.8f, 0.8f, 0.82f, a);
                bx += dash;
            }
            const bool cur = focus && (nav.codeEdit ? i == nav.codeCursor : i == (int)std::min<size_t>(row.value.size(), rv::kCodeLen - 1));
            r.rect(bx, by, bs, bs, 0.0f, 0.0f, 0.0f, 0.55f * a);
            r.rect(bx, by + bs - 3 * u, bs, 3 * u, cur ? kAmber[0] : 0.4f, cur ? kAmber[1] : 0.4f, cur ? kAmber[2] : 0.45f, a);
            if (i < (int)row.value.size()) {
                const std::string c(1, row.value[i]);
                r.text(bx + (bs - r.textWidth(c, bt)) / 2, by + (bs - 8 * bt) / 2, bt, c, 0.97f, 0.97f, 1.0f, a);
            }
            bx += bs + gap;
        }
    }
    nav.hits.push_back({x, y, w, h, index});
}

// The right-hand panel: a heading and a few lines.
void panel(Renderer& r, float x, float y, float w, float u, float a, const std::string& head, const std::vector<std::string>& lines) {
    const float hs = px(2.4f * u), ls = px(1.6f * u), lh = 8 * ls + 8 * u;
    const float h = 24 * u + 8 * hs + 12 * u + lines.size() * lh;
    pill(r, x - 6 * u, y - 6 * u, w + 12 * u, h, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
    label(r, x + 10 * u, y + 10 * u, hs, head, kAmber[0], kAmber[1], kAmber[2], a);
    float ly = y + 10 * u + 8 * hs + 14 * u;
    for (const std::string& l : lines) {
        r.text(x + 10 * u, ly, ls, l, 0.86f, 0.86f, 0.89f, a);
        ly += lh;
    }
}

std::vector<std::string> about(const Nav& nav, const Row* row, const View& v) {
    const int id = row ? row->id : -1;
    switch (id) {
        case R_CODE_PAGE: return {"YOUR FRIEND OPENS THEIR GAME:", "ESC > MULTIPLAYER > OPEN TO FRIENDS,", "AND SENDS YOU THE CODE.", "NO PORTS, NO ADDRESSES."};
        case R_ADDRESS_PAGE: return {"A DEDICATED SERVER, OR A FRIEND", "WHO FORWARDED THEIR PORT (27015,", "TCP AND UDP). IP OR IP:PORT."};
        case R_LAN_PAGE: return {"GAMES HOSTED ON THIS NETWORK:", "SAME WI-FI OR CABLE."};
        case R_HOST_PAGE: return {"YOUR WORLD ON A FRESH MAP, WITH", "THE PORT AND PASSWORD YOU PICK.", "IN A GAME ALREADY? ESC > MULTIPLAYER", "> OPEN TO FRIENDS SHARES THAT ONE."};
        case R_NAME: return {"16 LETTERS, DIGITS, . - _"};
        case R_CODEBOX: return {"SIX LETTERS AND DIGITS: NO 0 O 1 I L U", "(TYPE THEM AS YOU SEE THEM: CASE", "AND DASHES DON'T MATTER).", "CTRL+V PASTES."};
        case R_PASSWORD: return {"THE HOST CHOSE IT. IT NEVER CROSSES", "THE NETWORK (A PROOF DOES)."};
        case R_PORT: return {"FRIENDS BY ADDRESS NEED IT FORWARDED", "ON YOUR ROUTER, TCP AND UDP.", "WITH A JOIN CODE THEY DON'T."};
        case R_ALSOCODE: return {"A CODE TOO: FRIENDS ON OTHER", "NETWORKS JOIN WITHOUT PORT FORWARDING."};
        default: break;
    }
    (void)nav;
    (void)v;
    return {};
}
}  // namespace

void draw(Renderer& r, Nav& nav, const View& v, int W, int H, float a, double clock, bool pad, bool overGame) {
    nav.hits.clear();
    const float u = std::max(0.75f, H / 720.0f);
    shade(r, W, H, a, overGame);
    const float x0 = std::max(40 * u, W * 0.06f), small = px(1.2f * u), mid = px(1.6f * u);
    r.text(x0, H - 30 * u, small, nav.hint(pad, v), 0.72f, 0.74f, 0.78f, a * 0.9f);
    const float tsz = px(3.0f * u), ty = std::max(24 * u, H * 0.07f);
    label(r, x0, ty, tsz, nav.title(v), kAmber[0], kAmber[1], kAmber[2], a);
    r.rect(x0 + 2 * u, ty + 8 * tsz + 4 * u, std::min(W * 0.3f, 360 * u), 2 * u, kAmber[0], kAmber[1], kAmber[2], 0.8f * a);
    const float top = ty + 8 * tsz + 26 * u;
    const std::vector<Row> rows = nav.rows(v);
    const float lw = std::min(W * 0.46f, 600 * u), gap = 6 * u;
    const float avail = H - top - 70 * u;
    float rowH = 50 * u;
    float need = 0;
    for (const Row& row : rows) need += (row.kind == INFO ? 0.8f : 1.0f) * rowH + gap;
    if (need > avail) rowH *= avail / need;
    float y = top;
    const Row* focused = nullptr;
    for (int i = 0; i < (int)rows.size(); i++) {
        const Row& row = rows[i];
        const float h = row.kind == INFO ? rowH * 0.8f : rowH;
        const bool foc = i == nav.sel;
        if (foc) focused = &row;
        drawRow(r, nav, row, i, x0, y, lw, h, u, a, foc, clock);
        if (row.kind == INFO && nav.page == P_JOINING && v.joining && v.joinError.empty()) {  // a spinner by the step
            const float cx = x0 + lw - 30 * u, cy = y + h / 2;
            for (int k = 0; k < 8; k++) {
                const double ang = clock * 6 + k * M_PI / 4;
                const float al = (float)(0.15 + 0.85 * ((k + (int)(clock * 8)) % 8) / 7.0);
                r.rect(cx + (float)std::cos(ang) * 10 * u - 2 * u, cy + (float)std::sin(ang) * 10 * u - 2 * u, 4 * u, 4 * u, kAmber[0], kAmber[1], kAmber[2], al * a);
            }
        }
        y += h + gap;
    }
    // The right-hand side.
    const float px0 = x0 + lw + 36 * u, pw = W - px0 - x0;
    if (pw < 220 * u) return;
    if (nav.page == P_FRIENDS && v.role == 1 && v.codeState == NetSession::CODE_OPEN) {
        // The code, big, to read out or copy.
        const float big = px(6.5f * u);
        const std::string code = rv::formatCode(v.code);
        const float bh = 8 * big + 150 * u;
        pill(r, px0 - 6 * u, top - 6 * u, pw + 12 * u, bh, kPanel[0], kPanel[1], kPanel[2], 0.88f * a);
        label(r, px0 + 10 * u, top + 8 * u, px(1.6f * u), "JOIN CODE", kGrey[0], kGrey[1], kGrey[2], a);
        label(r, px0 + 10 * u, top + 8 * u + 8 * px(1.6f * u) + 12 * u, big, code, 1.0f, 0.92f, 0.7f, a);
        float ly = top + 8 * u + 8 * px(1.6f * u) + 12 * u + 8 * big + 18 * u;
        r.text(px0 + 10 * u, ly, mid, nav.copied ? "COPIED: PASTE IT TO A FRIEND" : "SEND IT TO A FRIEND: THEY PICK", 0.86f, 0.86f, 0.89f, a);
        ly += 8 * mid + 8 * u;
        r.text(px0 + 10 * u, ly, mid, nav.copied ? "" : "MULTIPLAYER > JOIN WITH CODE", 0.86f, 0.86f, 0.89f, a);
        ly += 8 * mid + 16 * u;
        std::string lan = "ON YOUR NETWORK OR BY ADDRESS:";
        r.text(px0 + 10 * u, ly, small, lan, kGrey[0], kGrey[1], kGrey[2], a);
        ly += 8 * small + 6 * u;
        for (size_t i = 0; i < v.localIps.size() && i < 2; i++, ly += 8 * small + 6 * u)
            r.text(px0 + 10 * u, ly, small, v.localIps[i] + ":" + std::to_string(v.hostPort), 0.86f, 0.86f, 0.89f, a);
        return;
    }
    if (nav.page == P_FRIENDS) {
        std::vector<std::string> lines;
        if (v.role == 2) lines = {"IN " + v.sessionTitle + ".", "THE HOST'S WORLD: THEY PICK THE MAP", "AND THE LEVELS."};
        else if (v.role == 1) lines = {"YOU'RE HOSTING. FRIENDS ON YOUR NETWORK", "SEE IT. OPEN TO FRIENDS GIVES A CODE", "FOR EVERYONE ELSE."};
        else lines = {"OPEN TO FRIENDS SHARES THIS WORLD:", "THE MAP, THE LEVEL YOU'RE ON, YOUR", "EDITED SECTIONS AND YOUR TRUCKS.", "YOU GET A CODE TO SEND THEM."};
        panel(r, px0, top, pw, u, a, v.role == 2 ? "IN A GAME" : "FRIENDS", lines);
        return;
    }
    if (nav.page == P_JOINING) {
        std::vector<std::string> lines;
        if (v.joining && v.joinError.empty()) {
            lines = {v.byCode ? "FINDING YOUR FRIEND'S GAME, THEN A WAY" : "CONNECTING TO " + v.joinLabel + ".", v.byCode ? "THROUGH BOTH ROUTERS (OR THE RELAY)." : ""};
        } else if (!v.byCode) {
            lines = {"IS THE GAME STILL HOSTED? ON ANOTHER", "NETWORK THE HOST MAY NEED TO FORWARD", "PORT 27015 (TCP AND UDP), OR GIVE YOU", "A JOIN CODE INSTEAD."};
        } else {
            lines = {"CHECK THE CODE WITH YOUR FRIEND.", "A NEW OPEN TO FRIENDS GIVES A NEW CODE."};
        }
        panel(r, px0, top, pw, u, a, v.joining && v.joinError.empty() ? stepText(v) : "WHAT NOW", lines);
        return;
    }
    const std::vector<std::string> lines = about(nav, focused, v);
    if (!lines.empty() && focused) panel(r, px0, top, pw, u, a, focused->label, lines);
}

}  // namespace mpmenu
