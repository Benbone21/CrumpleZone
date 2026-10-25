#include "updatecard.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "hudkit.h"
#include "sdl_min.h"
#include "updater.h"

namespace updatecard {
namespace {

enum Btn { B_NOW, B_LATER, B_OK, B_COPY, B_CANCEL };
struct Hit {
    float x, y, w, h;
    Btn b;
};
int sel = 0;
std::vector<Hit> hits;
double copiedAt = -1;
float clockNow = 0;

std::vector<Btn> buttons(const update::Status& st) {
    switch (st.phase) {
        case update::Phase::Offer: return {B_NOW, B_LATER};
        case update::Phase::Info: return st.link.empty() ? std::vector<Btn>{B_OK} : std::vector<Btn>{B_OK, B_COPY};
        case update::Phase::Checking: return {B_LATER};
        case update::Phase::Downloading: return {B_CANCEL};
        default: return {};
    }
}
const char* label(Btn b) {
    switch (b) {
        case B_NOW: return "UPDATE NOW";
        case B_LATER: return "LATER";
        case B_OK: return "OK";
        case B_COPY: return "COPY LINK";
        case B_CANCEL: return "CANCEL";
    }
    return "";
}
void press(Btn b, const update::Status& st) {
    switch (b) {
        case B_NOW: update::accept(); break;
        case B_LATER: update::later(); break;
        case B_OK: update::dismiss(); break;
        case B_CANCEL: update::cancel(); break;
        case B_COPY:
            SDL_SetClipboardText(st.link.c_str());
            copiedAt = clockNow;
            return;
    }
    sel = 0;
}

// Words into lines no wider than `w` pixels.
std::vector<std::string> wrap(Renderer& r, const std::string& text, float s, float w) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (r.textWidth(tryLine, s) <= w || line.empty()) line = tryLine;
        else lines.push_back(line), line = word;
        while (r.textWidth(line, s) > w && line.size() > 1) {  // (a long link: break it anywhere)
            size_t n = std::max<size_t>(1, (size_t)(w / (6 * s)));
            lines.push_back(line.substr(0, n));
            line = line.substr(n);
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flushWord();
        else word += c;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace

bool shown() { return update::status().phase != update::Phase::Idle; }

void draw(Renderer& r, int W, int H, bool padLast) {
    hits.clear();
    const update::Status st = update::status();
    if (st.phase == update::Phase::Idle) return;
    clockNow += 1.0f / 60.0f;
    const float u = std::max(0.75f, H / 720.0f);
    const float big = hud::px(2.6f * u), mid = hud::px(1.6f * u), small = hud::px(1.2f * u);
    r.rect(0, 0, (float)W, (float)H, 0, 0, 0, 0.55f);
    const float bw = std::min(W - 40 * u, 760 * u), x = (W - bw) / 2, pad = 22 * u;
    const std::vector<std::string> lines = wrap(r, st.message, mid, bw - 2 * pad);
    const std::vector<Btn> bs = buttons(st);
    const bool bar = st.phase == update::Phase::Downloading;
    const float bh = pad + 8 * big + 16 * u + lines.size() * (8 * mid + 6 * u) + (bar ? 40 * u : 0) + (bs.empty() ? 0 : 64 * u) + pad;
    const float y = (H - bh) / 2;
    hud::pill(r, x, y, bw, bh, 0.08f, 0.09f, 0.1f, 0.94f);
    r.rect(x, y, bw, 3 * u, 1.0f, 0.72f, 0.2f, 1);
    float cy = y + pad;
    hud::label(r, x + pad, cy, big, st.title.empty() ? "UPDATE" : st.title, 1.0f, 0.72f, 0.2f);
    cy += 8 * big + 16 * u;
    for (const std::string& l : lines) {
        r.text(x + pad, cy, mid, l, 0.88f, 0.88f, 0.9f, 1);
        cy += 8 * mid + 6 * u;
    }
    if (bar) {
        const float w = bw - 2 * pad, h = 14 * u;
        cy += 8 * u;
        r.rect(x + pad, cy, w, h, 0.2f, 0.22f, 0.24f, 1);
        if (st.progress >= 0) r.rect(x + pad, cy, w * st.progress, h, 1.0f, 0.72f, 0.2f, 1);
        char b[96];
        if (st.total) std::snprintf(b, sizeof b, "%.1f OF %.1f MB", st.got / 1e6, st.total / 1e6);
        else std::snprintf(b, sizeof b, "%.1f MB", st.got / 1e6);
        r.text(x + pad, cy + h + 6 * u, small, b, 0.7f, 0.72f, 0.75f, 1);
        cy += 32 * u;
    }
    if (bs.empty()) return;
    if (sel >= (int)bs.size()) sel = 0;
    cy += 18 * u;
    const float bth = 40 * u, gap = 12 * u, ts = hud::px(1.8f * u);
    float bx = x + pad;
    for (size_t i = 0; i < bs.size(); i++) {
        const std::string t = bs[i] == B_COPY && copiedAt >= 0 && clockNow - copiedAt < 2 ? "COPIED" : label(bs[i]);
        const float w = std::max(140 * u, r.textWidth(t, ts) + 36 * u);
        const bool foc = (int)i == sel;
        hud::pill(r, bx, cy, w, bth, foc ? 0.95f : 0.2f, foc ? 0.68f : 0.22f, foc ? 0.18f : 0.24f, 1);
        r.text(bx + (w - r.textWidth(t, ts)) / 2, cy + (bth - 7 * ts) / 2, ts, t, foc ? 0.05f : 0.9f, foc ? 0.05f : 0.9f, foc ? 0.05f : 0.92f, 1);
        hits.push_back({bx, cy, w, bth, bs[i]});
        bx += w + gap;
    }
    const char* hint = padLast ? "D-PAD: CHOOSE   A: PRESS   B: BACK" : "ARROWS: CHOOSE   ENTER: PRESS   ESC: BACK";
    r.text(x + bw - pad - r.textWidth(hint, small), cy + bth + 10 * u, small, hint, 0.6f, 0.62f, 0.66f, 1);
}

bool key(int sc) {
    const update::Status st = update::status();
    if (st.phase == update::Phase::Idle) return false;
    const std::vector<Btn> bs = buttons(st);
    enum { ESC = 41, ENTER = 40, KPENTER = 88, SPACE = 44, LEFT = 80, RIGHT = 79, UP = 82, DOWN = 81, TAB = 43, A = 4, D = 7 };
    if (bs.empty()) return true;  // (installing: nothing to press, but the keys don't reach the game either)
    if (sc == LEFT || sc == UP || sc == A) sel = (sel + (int)bs.size() - 1) % (int)bs.size();
    else if (sc == RIGHT || sc == DOWN || sc == D || sc == TAB) sel = (sel + 1) % (int)bs.size();
    else if (sc == ENTER || sc == KPENTER || sc == SPACE) press(bs[std::min(sel, (int)bs.size() - 1)], st);
    else if (sc == ESC) press(st.phase == update::Phase::Offer || st.phase == update::Phase::Checking ? B_LATER : st.phase == update::Phase::Downloading ? B_CANCEL : B_OK, st);
    return true;
}

bool mouseDown(float x, float y) {
    const update::Status st = update::status();
    if (st.phase == update::Phase::Idle) return false;
    for (const Hit& h : hits)
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
            press(h.b, st);
            break;
        }
    return true;
}

void mouseMove(float x, float y) {
    for (size_t i = 0; i < hits.size(); i++)
        if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) sel = (int)i;
}

}  // namespace updatecard
