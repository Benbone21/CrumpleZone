// Small drawing helpers shared by the HUD and the menus (pixel coordinates, origin
// top-left; queued on the Renderer and drawn by flushHud()).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "render.h"

namespace hud {

// The HUD font is a 5x7 bitmap: keep scales whole numbers so every pixel stays crisp.
inline float px(float x) { return std::max(1.0f, std::round(x)); }

// Text with a soft drop shadow so it reads over bright sky and pavement.
inline void label(Renderer& r, float x, float y, float s, const std::string& t, float cr, float cg, float cb, float a = 1) {
    r.text(x + s * 0.6f, y + s * 0.6f, s, t, 0, 0, 0, 0.55f * a);
    r.text(x, y, s, t, cr, cg, cb, a);
}

// Rounded-ish pill background (rect with chamfered corners).
inline void pill(Renderer& r, float x, float y, float w, float h, float cr, float cg, float cb, float a) {
    float c = std::min(h * 0.3f, 6.0f);
    r.rect(x + c, y, w - 2 * c, h, cr, cg, cb, a);
    r.rect(x, y + c, c, h - 2 * c, cr, cg, cb, a);
    r.rect(x + w - c, y + c, c, h - 2 * c, cr, cg, cb, a);
    r.tri(x, y + c, x + c, y, x + c, y + c, cr, cg, cb, a);
    r.tri(x + w - c, y, x + w, y + c, x + w - c, y + c, cr, cg, cb, a);
    r.tri(x, y + h - c, x + c, y + h - c, x + c, y + h, cr, cg, cb, a);
    r.tri(x + w - c, y + h - c, x + w, y + h - c, x + w - c, y + h, cr, cg, cb, a);
}

inline void centeredPill(Renderer& r, float cx, float y, float s, const std::string& t, float br, float bg, float bb, float a,
                         float tr = 1, float tg = 1, float tb = 1) {
    float w = r.textWidth(t, s) + 14 * s, h = 12 * s;
    pill(r, cx - w / 2, y, w, h, br, bg, bb, a * 0.62f);
    r.text(cx - w / 2 + 7 * s, y + 2.2f * s, s, t, tr, tg, tb, a);
}

}  // namespace hud
