// Drawing the pause menu, the settings and the title's classic pages (menu.h has the logic).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gamepad.h"
#include "hudkit.h"
#include "menu.h"
#include "menuids.h"
#include "net.h"
#include "sdl_min.h"
#include "vehicles.h"

using namespace hud;


// One row in a box from x to x + w, its top at cy: returns its height (and remembers where
// it went, for the mouse).
float PauseMenu::drawRow(Renderer& r, const Row& row, int k, float x, float w, float cy, const RowStyle& st) {
    const float ts = st.ts, small = st.small, u = st.u, rowH = st.rowH, headH = st.headH, a = st.a, pad = 22 * u;
    if (row.kind == HEADER) {
        if (row.id == -1)  // an information line
            r.text(x + pad + 6 * u, cy + headH - 8 * small - 6 * u, small, row.label, 0.82f, 0.84f, 0.88f, 0.95f * a);
        else
            r.text(x + pad, cy + headH - 8 * small - 6 * u, small, row.label, 1.0f, 0.75f, 0.3f, 0.85f * a);
        return headH;
    }
    const float valueW = st.valueW, valueX = x + w - pad - valueW;
    const bool hasHint = row.kind == CHOICE && !row.hint.empty();
    const float hh = rowH + (hasHint ? 8 * small + 6 * u : 0);
    const bool on = k == sel;
    if (on) {
        r.rect(x + pad * 0.5f, cy, w - pad, hh, 1, 1, 1, 0.07f * a);
        r.rect(x + pad * 0.5f, cy, 3 * u, hh, 1.0f, 0.75f, 0.3f, a);
    }
    const float ty = cy + (rowH - 8 * ts) / 2;
    const float dimK = row.dim ? 0.45f : 1.0f;
    const float lr = (on ? 1.0f : 0.86f) * dimK, lg = (on ? 1.0f : 0.87f) * dimK, lb = (on ? 1.0f : 0.9f) * dimK;
    {
        std::string lab = row.label;  // (a long one, a game's name: cut to fit beside its value)
        const float room = (row.kind == BUTTON ? w - 2 * pad - 12 * u - (row.hint.empty() ? 0 : r.textWidth(row.hint, small) + 12 * u)
                                                : valueX - x - pad - 18 * u);
        while (lab.size() > 3 && r.textWidth(lab, ts) > room) lab.pop_back();
        r.text(x + pad + 6 * u, ty, ts, lab, lr, lg, lb, a);
    }
    Hit hit{x + pad * 0.5f, cy, w - pad, hh, valueX, valueW, k};
    if (row.kind == BUTTON && !row.hint.empty())
        r.text(x + w - pad - r.textWidth(row.hint, small), cy + (rowH - 8 * small) / 2, small, row.hint, 0.55f * dimK + (row.dim ? 0.3f : 0), 0.57f * dimK,
               0.62f * dimK, a);
    if (row.kind == CHOICE) {
        r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.35f * a);
        const float ar = on ? 1.0f : 0.6f, ag = on ? 0.75f : 0.6f, ab = on ? 0.3f : 0.62f;
        r.text(valueX + 8 * u, ty, ts, "<", ar, ag, ab, a);
        r.text(valueX + valueW - 8 * u - r.textWidth(">", ts), ty, ts, ">", ar, ag, ab, a);
        {  // (a long value: smaller, to fit between the arrows)
            const float vs = r.textWidth(row.value, ts) > valueW - 44 * u ? small : ts;
            std::string v = row.value;
            while (!v.empty() && r.textWidth(v, vs) > valueW - 44 * u) v.pop_back();
            r.text(valueX + (valueW - r.textWidth(v, vs)) / 2, cy + (rowH - 8 * vs) / 2, vs, v, 1, 1, 1, a);
        }
        if (hasHint) {
            std::string hint = row.hint;
            const float maxW = w - 2 * pad - 12 * u;
            while (!hint.empty() && r.textWidth(hint, small) > maxW) hint.pop_back();
            r.text(x + pad + 6 * u, cy + rowH, small, hint, 0.6f, 0.62f, 0.66f, a);
        }
        if ((row.id == DEST_ROW || row.id == LEVEL_PICK || row.id == DEVSEC_ROW || row.id == DEVPLACE_ROW) && on && !this->title) {
            const std::string go = std::string(pad::hint("ENTER", pad::B_A)) + (row.id == DEST_ROW || row.id == DEVSEC_ROW || row.id == DEVPLACE_ROW ? " TO GO" : " TO PLAY");
            r.text(valueX - r.textWidth(go, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, go, 1.0f, 0.75f, 0.3f, a);
        }
    }
    if (row.kind == TEXT) {
        r.rect(valueX, cy + 5 * u, valueW, rowH - 10 * u, 0, 0, 0, 0.5f * a);
        if (on) r.rect(valueX, cy + rowH - 7 * u, valueW, 2 * u, 1.0f, 0.75f, 0.3f, 0.6f * a);
        // The window of the text that shows: the cursor stays in it (the front scrolls off, as it did).
        const bool mine = on && tedRow == row.id;
        const size_t cpos = mine ? te::cursorOf(row.value, ted) : row.value.size();
        const float room = valueW - 20 * u;
        size_t off = 0;
        while (off < cpos && r.textWidth(row.value.substr(off, cpos - off), ts) > room) off++;
        std::string v = row.value.substr(off);
        while (!v.empty() && r.textWidth(v, ts) > room) v.pop_back();
        size_t sf, st2;
        if (mine && te::selection(row.value, ted, sf, st2)) {
            sf = std::max(sf, off), st2 = std::min(st2, off + v.size());
            if (st2 > sf) {
                const float x0 = r.textWidth(row.value.substr(off, sf - off), ts), x1 = r.textWidth(row.value.substr(off, st2 - off), ts);
                r.rect(valueX + 8 * u + x0, cy + 9 * u, x1 - x0, rowH - 18 * u, 0.3f, 0.5f, 0.95f, 0.6f * a);
            }
        }
        if (v.empty() && !row.placeholder.empty()) {
            std::string ph = row.placeholder;
            while (!ph.empty() && r.textWidth(ph, ts) > valueW - 20 * u) ph.pop_back();
            r.text(valueX + 8 * u, ty, ts, ph, 0.42f, 0.44f, 0.48f, a);
        } else {
            r.text(valueX + 8 * u, ty, ts, v, 1, 1, 1, a);
        }
        if (on && std::fmod(caret, 1.0f) < 0.55f)
            r.rect(valueX + 9 * u + r.textWidth(row.value.substr(off, cpos - off), ts), cy + 9 * u, 2 * u, rowH - 18 * u, 1.0f, 0.75f, 0.3f, a);
        if (on && valueX - x > r.textWidth(row.label, ts) + r.textWidth(row.hint, small) + 60 * u)
            r.text(valueX - r.textWidth(row.hint, small) - 12 * u, cy + (rowH - 8 * small) / 2, small, row.hint, 0.6f, 0.62f, 0.66f, a);
    }
    if (row.kind == SLIDER) {
        const float textW = 70 * u, barW = valueW - textW - 8 * u, barY = cy + rowH / 2 - 3 * u;
        r.rect(valueX, barY, barW, 6 * u, 1, 1, 1, 0.15f * a);
        r.rect(valueX, barY, barW * row.frac, 6 * u, 1.0f, 0.75f, 0.3f, 0.9f * a);
        r.rect(valueX + barW * row.frac - 3 * u, barY - 5 * u, 6 * u, 16 * u, on ? 1.0f : 0.85f, on ? 1.0f : 0.85f, on ? 1.0f : 0.88f, a);
        r.text(valueX + valueW - r.textWidth(row.value, ts), ty, ts, row.value, 1, 1, 1, a);
        hit.barW = barW;
    }
    if (!row.dim) hits.push_back(hit);
    return hh;
}

// The main menu's own screen: a dark slate background with faint contour lines (a topo map)
// and an amber rule: no world behind it.
void PauseMenu::drawBackdrop(Renderer& r, int W, int H, float a) const {
    (void)a;
    const int bands = 24;
    for (int i = 0; i < bands; i++) {  // a vertical gradient, lighter toward the middle
        const float t = (i + 0.5f) / bands, k = 1.0f - std::fabs(t - 0.45f) * 1.4f;
        r.rect(0, H * (float)i / bands, (float)W, H / (float)bands + 1, 0.035f + 0.03f * k, 0.042f + 0.032f * k, 0.052f + 0.035f * k, 1.0f);
    }
    // Contour lines: a few hills' worth of wobbly rings, drawn as short thin segments.
    const float u = std::max(0.75f, H / 720.0f);
    struct Hill {
        float x, y, r0;
    };
    const Hill hills[3] = {{0.18f, 0.72f, 0.05f}, {0.82f, 0.28f, 0.04f}, {0.62f, 0.95f, 0.08f}};
    for (const Hill& h : hills)
        for (int ring = 0; ring < 14; ring++) {
            const float rad = (h.r0 + ring * 0.045f) * W;
            const int segs = 90;
            float px0 = 0, py0 = 0;
            for (int k = 0; k <= segs; k++) {
                const float ang = k * 6.2831853f / segs;
                const float wob = 1 + 0.10f * std::sin(ang * 3 + ring * 0.7f + h.x * 9) + 0.05f * std::sin(ang * 7 - ring * 1.3f);
                const float px1 = h.x * W + std::cos(ang) * rad * wob, py1 = h.y * H + std::sin(ang) * rad * wob * 0.8f;
                if (k > 0) {
                    const float dx = px1 - px0, dy = py1 - py0, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
                    const float nx = -dy / l * 0.6f * u, ny = dx / l * 0.6f * u;
                    const float al = ring % 5 == 0 ? 0.07f : 0.035f;
                    r.tri(px0 + nx, py0 + ny, px1 + nx, py1 + ny, px1 - nx, py1 - ny, 0.75f, 0.8f, 0.85f, al);
                    r.tri(px0 + nx, py0 + ny, px1 - nx, py1 - ny, px0 - nx, py0 - ny, 0.75f, 0.8f, 0.85f, al);
                }
                px0 = px1, py0 = py1;
            }
        }
    r.rect(0, 0, (float)W, 3 * u, 1.0f, 0.75f, 0.3f, 0.9f);
}

void PauseMenu::draw(Renderer& r, const Settings& s, int W, int H, double dt) {
    if (!open) return;
    fade = std::min(1.0f, fade + (float)dt * 7.0f);
    caret += (float)dt;
    spin += (float)dt;
    const float a = fade;
    if (this->title && sceneBehind) r.rect(0, 0, (float)W, (float)H, 0.0f, 0.01f, 0.02f, 0.38f * a);  // the camp shows through, dimmed
    else if (this->title) drawBackdrop(r, W, H, a);
    else r.rect(0, 0, (float)W, (float)H, 0.0f, 0.01f, 0.02f, 0.35f * a);  // darken the (blurred) scene
    if (page == 2) {
        drawMulti(r, s, W, H, a);
        return;
    }
    if (page == 1) {
        drawSettings(r, s, W, H, a);
        return;
    }
    const std::vector<Row> all = rows(s);
    std::vector<Row> rs = all;
    std::vector<int> idx(all.size());  // (which row of the page each drawn row is)
    for (int k = 0; k < (int)all.size(); k++) idx[k] = k;
    const float u = std::max(0.75f, H / 720.0f);
    const bool titleMain = this->title && page == 0;
    const float ts = px(1.6f * u), small = px(1.2f * u), title = px((titleMain ? 2.0f : 3.0f) * u);
    const float pad = 22 * u;
    // The main menu: the name big above the panel.
    float top = 0;
    if (titleMain) {
        const float big = px(7.0f * u), tag = px(1.6f * u);
        const std::string name = "CRUMPLE ZONE", line = "SOFT-BODY DRIVING: CRASH IT, CRAWL IT, RACE IT";
        top = std::max(24 * u, H * 0.08f);
        label(r, (W - r.textWidth(name, big)) / 2, top, big, name, 1.0f, 0.75f, 0.3f, a);
        r.text((W - r.textWidth(line, tag)) / 2, top + 8 * big + 12 * u, tag, line, 0.62f, 0.64f, 0.68f, a);
        top += 8 * big + 12 * u + 8 * tag + 22 * u;
    }
    // (The settings page is taller than a 720p screen: squeeze the space between rows to fit, keeping the text size.)
    auto pageHeight = [&](float k) {
        float contentH = 0;
        for (const Row& row : rs)
            contentH += row.kind == HEADER ? 8 * small + 16 * u * k : 8 * ts + 14 * u * k + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
        return pad * 2 + 8 * title + 18 * u + contentH + 8 * small + 16 * u;
    };
    const float avail = H - top - 16 * u;
    const float h0 = pageHeight(0), h1 = pageHeight(1);
    const float squeeze = h1 > avail ? std::min(1.0f, std::max(0.2f, (avail - h0) / (h1 - h0))) : 1.0f;
    const float headH = 8 * small + 16 * u * squeeze;
    float rowH = 8 * ts + 14 * u * squeeze;

    float contentH = 0, h = 0;
    for (int pass = 0; pass < 2; pass++) {
        contentH = 0;
        int n = 0;
        for (const Row& row : rs) {
            contentH += row.kind == HEADER ? headH : rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
            n += row.kind != HEADER;
        }
        h = pad * 2 + 8 * title + 18 * u + contentH + 8 * small + 16 * u;
        // A long page (settings at 720p) packs its rows closer rather than run off the screen.
        if (pass == 0 && h > avail - 4 * u && n > 0) rowH = std::max(8 * ts + 6 * u, rowH - (h - (avail - 4 * u)) / n);
        else break;
    }
    const float w = std::min((float)W - 40 * u, 640 * u);
    const float x = (W - w) / 2, y = titleMain ? std::max(top, top + (avail - h) * 0.25f) : std::max(10 * u, (H - h) / 2);
    pill(r, x, y, w, h, 0.03f, 0.035f, 0.045f, (this->title ? 0.82f : 0.9f) * a);
    r.rect(x + pad, y + pad + 8 * title + 8 * u, w - 2 * pad, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    label(r, x + pad, y + pad, title,
          page == 0 ? (this->title ? "MAIN MENU" : "PAUSED") : page == 3 ? "AUTOPILOT" : page == 4 ? "LEVELS" : page == 5 ? "JOBS" : "CONTROLS",
          1.0f, 0.75f, 0.3f, a);
    if (page == 0 && !this->title) {
        const char* tag = "CRUMPLE ZONE";
        r.text(x + w - pad - r.textWidth(tag, small), y + pad + 8 * title - 8 * small, small, tag, 0.55f, 0.57f, 0.62f, a);
    }

    hits.clear();
    const RowStyle st{ts, small, u, rowH, headH, a, 250 * u};
    float cy = y + pad + 8 * title + 18 * u;
    for (int k = 0; k < (int)rs.size(); k++) cy += drawRow(r, rs[k], idx[k], x, w, cy, st);
    std::string keys = page == 0 && this->title ? "W S SELECT   A D CHANGE   ENTER OK"
                       : page == 0                ? "W S SELECT   A D CHANGE   ENTER OK   ESC RESUME"
                       : standalone && !this->title ? "W S SELECT   A D CHANGE   ENTER OK   ESC CLOSE"
                                                  : "W S SELECT   A D CHANGE   ENTER OK   ESC BACK";
    if (pad::ui.last) {  // the controller was the last thing used
        keys = "D-PAD SELECT   LEFT RIGHT CHANGE   " + std::string(pad::hint("", pad::B_A)) + " OK";
        if (!(page == 0 && this->title)) keys += "   " + std::string(pad::hint("", pad::B_B)) + (page == 0 || (standalone && !this->title) ? " RESUME" : " BACK");
    }
    r.text(x + (w - r.textWidth(keys, small)) / 2, y + h - pad - 8 * small + 6 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}

// The SETTINGS page: the title, the tab strip (Q and E, or a click, or the pad's shoulder buttons switch), then the tab's rows. The
// panel is the same size for every tab so the strip stays where it is. A tab with more rows than fit (CONTROLS, DEV) scrolls with the focus.
void PauseMenu::drawSettings(Renderer& r, const Settings& s, int W, int H, float a) {
    viewW = W, viewH = H;
    const Geom g = settingsGeom(W, H, s);
    const float u = g.u, ts = px(1.6f * u), small = px(1.2f * u), titleS = px(3.0f * u), pad = 22 * u;
    const std::vector<Row> all = rows(s);
    hits.clear();
    pill(r, g.x, g.y, g.w, g.h, 0.03f, 0.035f, 0.045f, (this->title ? 0.82f : 0.9f) * a);
    r.rect(g.x + pad, g.y + pad + 8 * titleS + 8 * u, g.w - 2 * pad, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    label(r, g.x + pad, g.y + pad, titleS, "SETTINGS", 1.0f, 0.75f, 0.3f, a);
    if (!this->title) {
        const char* tag = "CRUMPLE ZONE";
        r.text(g.x + g.w - pad - r.textWidth(tag, small), g.y + pad + 8 * titleS - 8 * small, small, tag, 0.55f, 0.57f, 0.62f, a);
    }
    // The tab strip.
    for (int t = 0; t < tabCount(); t++) {
        const Rect c = tabRect(t, s);
        const bool on = t == tab;
        if (on) r.rect(c.x, c.y, c.w, c.h, 1, 1, 1, 0.07f * a);
        r.rect(c.x, c.y + c.h - 2 * u, c.w, 2 * u, on ? 1.0f : 0.5f, on ? 0.75f : 0.52f, on ? 0.3f : 0.56f, (on ? 1.0f : 0.35f) * a);
        const char* nm = tabName(t);
        r.text(c.x + (c.w - r.textWidth(nm, ts)) / 2, c.y + (c.h - 2 * u - 8 * ts) / 2, ts, nm, on ? 1.0f : 0.6f, on ? 0.85f : 0.62f, on ? 0.45f : 0.66f, a);
    }
    // Which rows show, and how tightly they pack.
    auto height = [&](const Row& row, float k, float rowH) {
        return row.kind == HEADER ? 8 * small + 16 * u * k : rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
    };
    const float rowNormal = 8 * ts + 14 * u, rowMin = 8 * ts + 6 * u;
    auto total = [&](float k, float rowH) {
        float c = 0;
        for (const Row& row : all) c += height(row, k, rowH);
        return c;
    };
    std::vector<int> idx;
    float k = 1, rowH = rowNormal;
    if (total(0, rowMin) > g.rowsH) {  // a window of the rows, scrolling with the focus, under its group's header
        const float rh = 8 * ts + 9 * u;
        const int n = (int)all.size();
        auto lastFrom = [&](int from, std::vector<int>* pick) {
            std::vector<int> p;
            float used = 0;
            int start = from;
            if (start > 0 && all[start].kind != HEADER)
                for (int q = start - 1; q >= 0; q--)
                    if (all[q].kind == HEADER) {
                        p.push_back(q);
                        used += height(all[q], 0.6f, rh);
                        break;
                    }
            int last = from;
            for (int q = from; q < n; q++) {
                const float hq = height(all[q], 0.6f, rh);
                if (used + hq > g.rowsH) break;
                used += hq, last = q, p.push_back(q);
            }
            if (pick) *pick = p;
            return last;
        };
        scroll = std::min(std::max(scroll, 0), std::max(0, n - 1));
        if (sel >= 0 && sel < n) {
            if (sel - 1 < scroll) scroll = std::max(0, sel - 1);
            while (scroll < n - 1 && lastFrom(scroll, nullptr) < sel) scroll++;
        }
        lastFrom(scroll, &idx);
        k = 0.6f, rowH = rh;
    } else {
        scroll = 0;
        for (int q = 0; q < (int)all.size(); q++) idx.push_back(q);
        const float t1 = total(1, rowNormal), t0 = total(0, rowNormal);
        if (t1 > g.rowsH) k = t1 - t0 > 0 ? std::min(1.0f, std::max(0.45f, (g.rowsH - t0) / (t1 - t0))) : 0.45f;
        if (total(k, rowNormal) > g.rowsH + 0.5f) {
            int nrows = 0;
            for (const Row& row : all) nrows += row.kind != HEADER;
            rowH = std::max(rowMin, rowNormal - (total(k, rowNormal) - g.rowsH) / std::max(1, nrows));
        }
    }
    const RowStyle st{ts, small, u, rowH, 8 * small + 16 * u * k, a, 250 * u};
    float cy = g.rowsY;
    for (int q : idx) cy += drawRow(r, all[q], q, g.x, g.w, cy, st);
    if ((int)idx.size() < (int)all.size()) {
        const char* more = "MORE: W S SCROLLS";
        r.text(g.x + g.w - pad - r.textWidth(more, small), g.y + g.h - pad - 8 * small - 14 * u, small, more, 0.55f, 0.57f, 0.62f, a);
    }
    // The hint line.
    const bool back = this->title || settingsFromPause;
    std::string keys = std::string("Q E TABS   W S SELECT   A D CHANGE   ENTER OK   ESC ") + (back ? "BACK" : "CLOSE");
    if (pad::ui.last) {
        keys = std::string(pad::hint("", pad::B_LB)) + " " + pad::hint("", pad::B_RB) + " TABS   D-PAD SELECT   LEFT RIGHT CHANGE   " + pad::hint("", pad::B_A) + " OK   " +
               pad::hint("", pad::B_B) + (back ? " BACK" : " CLOSE");
    }
    r.text(g.x + (g.w - r.textWidth(keys, small)) / 2, g.y + g.h - pad - 8 * small + 6 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}

// The multiplayer page: YOUR NAME across the top; the HOST A GAME and JOIN A GAME cards side
// by side (stacked on a narrow window); BACK below. In a session, or while joining, one card.
void PauseMenu::drawMulti(Renderer& r, const Settings& s, int W, int H, float a) {
    std::vector<Row> rs = rows(s);
    if (sel < 0 || sel >= (int)rs.size() || rs[sel].kind == HEADER) {
        sel = 0;
        while (sel < (int)rs.size() - 1 && rs[sel].kind == HEADER) sel++;
    }
    const float u = std::max(0.75f, H / 720.0f);
    const float ts = px(1.6f * u), small = px(1.2f * u), big = px(3.0f * u), cardT = px(2.0f * u);
    const float pad = 22 * u;
    const RowStyle base{ts, small, u, 8 * ts + 14 * u, 8 * small + 12 * u, a, 230 * u};
    auto colHeight = [&](int c) {
        float h = 0;
        for (const Row& row : rs)
            if (row.col == c) h += row.kind == HEADER ? base.headH : base.rowH + (row.kind == CHOICE && !row.hint.empty() ? 8 * small + 6 * u : 0);
        return h;
    };
    bool hasJoin = false;
    for (const Row& row : rs) hasJoin |= row.col == 2;
    const bool wide = W >= 900 * u * 0.85f && hasJoin;
    const float totalW = std::min((float)W - 30 * u, hasJoin ? 1180 * u : 700 * u);
    const float gap = 20 * u;
    const float cardW = hasJoin && wide ? (totalW - gap) / 2 : totalW;
    const float cardHead = pad + 8 * cardT + 16 * u;
    const float h1 = cardHead + colHeight(1) + pad, h2 = hasJoin ? cardHead + colHeight(2) + pad : 0;
    const float cardsH = wide ? std::max(h1, h2) : h1 + (hasJoin ? gap + h2 : 0);
    float top0 = 0, bottom0 = 0;
    for (const Row& row : rs)
        if (row.col == 0) (&row < &rs[rs.size() / 2] ? top0 : bottom0) += base.rowH;
    const float headerH = 8 * big + 20 * u;
    const float total = headerH + top0 + (top0 > 0 ? 12 * u : 0) + cardsH + 12 * u + bottom0 + 8 * small + 20 * u;
    const float x0 = (W - totalW) / 2;
    float y = std::max(12 * u, (H - total) / 2);

    hits.clear();
    // Title, and what's going on, on the right.
    label(r, x0, y, big, "MULTIPLAYER", 1.0f, 0.75f, 0.3f, a);
    {
        const std::string st = joining ? (joinError.empty() ? "CONNECTING..." : "COULDN'T JOIN") : netStatus;
        r.text(x0 + totalW - r.textWidth(st, small), y + 8 * big - 8 * small, small, st, 0.6f, 0.62f, 0.66f, a);
    }
    r.rect(x0, y + 8 * big + 8 * u, totalW, 2 * u, 1.0f, 0.75f, 0.3f, 0.8f * a);
    y += headerH;
    // Your name (offline), across the top.
    const int n = (int)rs.size();
    int k = 0;
    if (top0 > 0) {
        const float nameW = std::min(totalW, 620 * u);
        pill(r, x0, y - 4 * u, nameW, top0 + 8 * u, 0.03f, 0.035f, 0.045f, 0.85f * a);  // (the name, and the password)
        for (; k < n && rs[k].col == 0; k++) y += drawRow(r, rs[k], k, x0, nameW, y, base);
        y += 12 * u;
    }
    // The cards.
    auto card = [&](int c, float cx, float cy, float cw, float ch, const char* titleText, const char* sub) {
        pill(r, cx, cy, cw, ch, 0.03f, 0.035f, 0.045f, 0.9f * a);
        r.rect(cx, cy, cw, 3 * u, c == 1 ? 1.0f : 0.35f, c == 1 ? 0.75f : 0.75f, c == 1 ? 0.3f : 1.0f, 0.9f * a);
        label(r, cx + pad, cy + pad, cardT, titleText, 1, 1, 1, a);
        if (sub && r.textWidth(titleText, cardT) + r.textWidth(sub, small) + 24 * u < cw - 2 * pad)  // (only if it fits beside the title)
            r.text(cx + cw - pad - r.textWidth(sub, small), cy + pad + 8 * cardT - 8 * small, small, sub, 0.55f, 0.57f, 0.62f, a);
        float ry = cy + cardHead;
        RowStyle st = base;
        st.valueW = std::min(230 * u, cw * 0.46f);
        for (int i = 0; i < n; i++)
            if (rs[i].col == c) ry += drawRow(r, rs[i], i, cx, cw, ry, st);
    };
    const char* t1 = joining ? (joinError.empty() ? "JOINING" : "NOT CONNECTED") : netActive ? sessionTitle.c_str() : "HOST A GAME";
    const char* s1 = joining || netActive ? nullptr : "YOUR WORLD: OTHERS JOIN YOU";
    if (joining && joinError.empty()) {  // a spinner by the title
        const float cx = x0 + totalW - pad - 10 * u, cy = y + pad + 4 * cardT;
        for (int i = 0; i < 8; i++) {
            const float ang = i * 0.785398f + spin * 6.0f;
            const float al = 0.15f + 0.85f * (float)i / 8;
            r.rect(cx + std::cos(ang) * 10 * u - 2 * u, cy + std::sin(ang) * 10 * u - 2 * u, 4 * u, 4 * u, 1.0f, 0.75f, 0.3f, al * a);
        }
    }
    if (wide) {
        card(1, x0, y, cardW, cardsH, t1, s1);
        if (hasJoin) card(2, x0 + cardW + gap, y, cardW, cardsH, "JOIN A GAME", "PLAY IN SOMEONE ELSE'S WORLD");
    } else {
        card(1, x0, y, cardW, h1, t1, s1);
        if (hasJoin) card(2, x0, y + h1 + gap, cardW, h2, "JOIN A GAME", "PLAY IN SOMEONE ELSE'S WORLD");
    }
    y += cardsH + 12 * u;
    // BACK (and anything else across the bottom).
    for (k = n / 2; k < n; k++)
        if (rs[k].col == 0) {
            const float bw = std::min(totalW, 300 * u);
            pill(r, x0, y - 4 * u, bw, base.rowH + 8 * u, 0.03f, 0.035f, 0.045f, 0.85f * a);
            y += drawRow(r, rs[k], k, x0, bw, y, base);
        }
    std::string keys = joining ? "ESC CANCEL" : "W S SELECT   A D CARD / CHANGE   ENTER OK   ESC BACK";
    if (pad::ui.last)
        keys = joining ? std::string(pad::hint("", pad::B_B)) + " CANCEL"
                       : "D-PAD SELECT   LEFT RIGHT CARD   " + std::string(pad::hint("", pad::B_A)) + " OK   " + std::string(pad::hint("", pad::B_B)) + " BACK";
    r.text(x0 + totalW - r.textWidth(keys, small), y + 4 * u, small, keys, 0.55f, 0.57f, 0.62f, a);
}
