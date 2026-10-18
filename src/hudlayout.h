// Where the HUD's pieces sit (pixel rects, origin top-left), worked out from the one UI scale
// u (= H / 720, at least 0.75) and the screen size, with no GL: main.cpp draws into these
// rects and tests/hudlayout.cpp checks that none of them touch. Text widths come in as
// numbers (the HUD font is 5x7: width = chars * 6 * scale), so any text length is covered.
#pragma once
#include <algorithm>
#include <cmath>

namespace hudlayout {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float r() const { return x + w; }
    float b() const { return y + h; }
};
inline bool overlap(const Rect& a, const Rect& b) { return a.x < b.r() && b.x < a.r() && a.y < b.b() && b.y < a.b(); }

inline float uiScale(int H) { return std::max(0.75f, H / 720.0f); }
inline float px(float x) { return std::max(1.0f, std::round(x)); }  // (hudkit.h's px: whole-number font scales)
inline float textW(float chars, float scale) { return chars * 6 * scale; }
// The biggest whole-number font scale, at most `want`, at which `chars` characters fit `maxW`.
inline float fitScale(float chars, float want, float maxW) {
    float s = px(want);
    while (s > 1 && textW(chars, s) > maxW) s -= 1;
    return s;
}
// How many characters of that size fit (for clipping what's still too long at scale 1).
inline int fitChars(float scale, float maxW) { return std::max(1, (int)(maxW / (6 * scale))); }

// ---- bottom right: the dial, the 4x4 schematic beside it, and the column above the dial.
constexpr float kDialR = 100;  // (x u) the dial's radius: 200u across, speed being what's read most
inline Rect dial(float u, int W, int H) {
    const float R = kDialR * u;
    return {W - 2 * R - 26 * u, H - 2 * R - 18 * u, 2 * R, 2 * R};
}
// (the schematic's pill, with the PSI and SWAY OFF lines above it)
inline Rect driveline(float u, int W, int H) {
    const float R = kDialR * u, gaugeX = W - 2 * R - 26 * u, w = 46 * u, h = 70 * u, x = gaugeX - w - 26 * u, y = H - h - 40 * u;
    const float s = px(1.1f * u), top = y - 13 * u - 16 * s;
    return {x - 10 * u, top, w + 20 * u, y + h + 24 * u - top};
}
// The terrain tag, the fuel gauge and the anchor line stack up above the dial, right-aligned to
// the dial's edge, each with its own slot so they never move over each other. kColW caps them.
constexpr float kColW = 400;  // (x u)
struct Column {
    Rect tag, fuel, anchors;
    float tagScale, fuelScale, anchorScale;  // font scales (the fuel and anchor lines shrink to fit kColW)
    float fuelBarW;
};
inline Column rightColumn(float u, int W, int H, float tagChars, float fuelChars, float anchorChars) {
    Column c;
    const Rect d = dial(u, W, H);
    const float right = W - 26 * u + 8 * u, maxW = kColW * u, gap = 5 * u;
    // (Slot heights use the nominal font scales, so a slot never moves with its text; a long line shrinks inside its own slot.)
    const float th = 10 * px(1.0f * u) + 4 * u, fh = 8 * px(1.2f * u) + 22 * u, ah = 8 * px(1.1f * u) + 10 * u;
    c.tagScale = fitScale(tagChars, 1.0f * u, maxW - 12 * u);
    const float tw = std::min(textW(tagChars, c.tagScale), maxW - 12 * u) + 12 * u;
    c.tag = {right - tw, d.y - gap - th, tw, th};
    c.fuelScale = fitScale(fuelChars, 1.2f * u, maxW - 16 * u);
    c.fuelBarW = std::max(150 * u, std::min(textW(fuelChars, c.fuelScale), maxW - 16 * u));
    c.fuel = {right - (c.fuelBarW + 16 * u), c.tag.y - gap - fh, c.fuelBarW + 16 * u, fh};
    c.anchorScale = fitScale(anchorChars, 1.1f * u, maxW - 16 * u);
    const float aw = std::min(textW(anchorChars, c.anchorScale), maxW - 16 * u) + 16 * u;
    c.anchors = {right - aw, c.fuel.y - gap - ah, aw, ah};
    return c;
}
// Chat (bottom left, above the vehicle card) stops short of that column.
inline float chatMaxW(float u, int W) { return W - 14 * u - (kColW + 26) * u - 12 * u; }

// ---- bottom left: the vehicle card. Its widest line (the mod summary) shrinks to stay left
// of the schematic (or of the dial).
inline float cardScale(float u, float chars, int W, int H, bool schematic) {
    const float avail = (schematic ? driveline(u, W, H).x : dial(u, W, H).x) - 12 * u - 22 * u;
    return fitScale(chars, 1.6f * u, avail);
}
inline Rect card(float u, int W, int H, float modChars, bool schematic, bool prop) {
    const float s = px(2.0f * u), sm = cardScale(u, modChars, W, H, schematic);
    const float y = H - 26 * u - 8 * s - 10 * sm - (prop ? 0 : 8 * sm + 5 * u) - (modChars > 0 ? 8 * sm + 5 * u : 0);
    const float w = std::max({150 * u, textW(modChars, sm), textW(20, s)});
    return {22 * u, y, w, H - 12 * u - y};
}

// ---- top: the compass strip (and the labels under it), and the panels under it.
inline Rect compass(float u, int W) { return {W / 2.0f - 180 * u, 8 * u, 360 * u, 22 * u + 4 * u + 8 * px(1.1f * u)}; }
// The level run's clock panel, under the compass.
inline Rect levelPanel(float u, int W) {
    const float s = px(1.2f * u), big = px(2.4f * u);
    return {W / 2.0f - 165 * u, 52 * u, 330 * u, 16 * s + 8 * big + 30 * u};
}

// Width the top-left panels may use: they stop short of the compass.
inline float topLeftMaxW(float u, int W) { return W / 2.0f - 180 * u - 12 * u - 8 * u; }

// ---- top left: playtest banner, career cash, the job, the winch panel: stacked, in that
// order, whichever are showing, below the DEV KEYS pill when it's up.
enum TopLeft { TL_PLAYTEST, TL_CASH, TL_JOB, TL_WINCH, TL_COUNT };
struct TopLeftState {
    bool on[TL_COUNT] = {false, false, false, false};
    bool devKeys = false;
};
inline float topLeftHeight(int slot, float u) {
    switch (slot) {
        case TL_PLAYTEST: return 8 * px(1.5f * u) + 18 * u;
        case TL_CASH: return 8 * px(1.4f * u) + 10 * u;
        case TL_JOB: return 16 * px(1.2f * u) + 22 * u;
        default: return 16 * px(1.4f * u) + 22 * u + 4 * u + 2 * (8 * px(1.1f * u) + 2 * u);  // (the winch panel, and its key hints under it: up to two lines)
    }
}
// Top edge of a slot (row 0 is where the first one goes).
inline float topLeftY(const TopLeftState& st, int slot, float u) {
    float y = st.devKeys ? 8 * u + 12 * px(1.2f * u) + 4 * u : 12 * u;
    for (int k = 0; k < slot; k++)
        if (st.on[k]) y += topLeftHeight(k, u) + 6 * u;
    return y;
}

// ---- bottom centre: the prompts (repair station, what you carry, winch aiming) share one stack of
// slots just above the vehicle card's tallest case (the biggest text scale); slot 0 is the lowest. They are as wide as the
// gap left of the 4x4 schematic allows, and shrink (then clip) to fit it.
inline float promptPitch(float u) { return 12 * px(1.6f * u) + 8 * u; }
inline float promptMaxW(float u, int W, int H) {
    const float half = driveline(u, W, H).x - W / 2.0f - 8 * u;
    return std::max(300 * u, std::min(2 * half, 0.7f * W));
}
// The pill for a prompt of `chars` characters: its scale (<= 1.6u) is the one the text is drawn at.
inline Rect prompt(float u, int W, int H, int slot, float chars, float* scaleOut = nullptr) {
    const float maxW = promptMaxW(u, W, H);
    const float s = fitScale(chars, 1.6f * u, maxW - 14 * 1.6f * u);
    const float w = std::min(textW(chars, s) + 14 * s, maxW), h = 12 * s;
    const float top = card(u, W, H, 1, true, false).y - 10 * u - (slot + 1) * promptPitch(u);
    if (scaleOut) *scaleOut = s;
    return {W / 2.0f - w / 2, top, w, h};
}

// ---- top right: the FPS block (FPS, the card, the frame times), and the players list under it.
inline Rect fpsBlock(float u, int W, float chars) {
    const float s1 = px(1.3f * u), s2 = px(1.0f * u), w = textW(std::max(chars, 20.0f), s2);
    return {W - 10 * u - w, 8 * u, w, 8 * s1 + 4 * u + 8 * s2 + 8 * s2 + 8 * u};
}
// `rows` players (the F3 screen lists them itself); right aligned, under the FPS block when that is up.
inline Rect playersList(float u, int W, int rows, float chars, bool fpsOn) {
    const float s = px(1.3f * u), lh = 8 * s + 6 * u, w = textW(chars, s) + 14 * u;
    const float y = fpsOn ? fpsBlock(u, W, 33).b() + 6 * u : 34 * u;
    return {W - 10 * u - w, y, w, std::max(0.0f, lh * rows - 2 * u)};
}

}  // namespace hudlayout
