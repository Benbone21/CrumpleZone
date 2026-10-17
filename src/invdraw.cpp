#include "invdraw.h"

#include <algorithm>
#include <cmath>

#include "gamepad.h"
#include "hudkit.h"
#include "trunk.h"

using namespace hud;

namespace {

// A rect with a border of `t` px.
void frame(Renderer& r, const InvRect& b, float t, float cr, float cg, float cb, float a) {
    r.rect(b.x, b.y, b.w, t, cr, cg, cb, a);
    r.rect(b.x, b.y + b.h - t, b.w, t, cr, cg, cb, a);
    r.rect(b.x, b.y + t, t, b.h - 2 * t, cr, cg, cb, a);
    r.rect(b.x + b.w - t, b.y + t, t, b.h - 2 * t, cr, cg, cb, a);
}

float textW(const std::string& t, float s) { return (float)t.size() * 6 * s; }

// Each group's tint: tools steel, parts green, recovery gear brown.
const float kTint[3][3] = {{0.115f, 0.135f, 0.17f}, {0.10f, 0.15f, 0.115f}, {0.17f, 0.13f, 0.09f}};

}  // namespace

void drawInvMenu(Renderer& r, const InvMenu& m, const InvLayout& L, int W, int H, bool padLast) {
    const float u = L.u;
    const float line = std::max(1.0f, std::round(u));
    r.rect(0, 0, (float)W, (float)H, 0.0f, 0.0f, 0.02f, 0.30f);  // (the world behind, dimmed a little)
    pill(r, L.panel.x, L.panel.y, L.panel.w, L.panel.h, 0.02f, 0.025f, 0.035f, 0.96f);
    frame(r, L.panel, line, 1.0f, 0.75f, 0.3f, 0.35f);
    label(r, L.title.x, L.title.y, (float)L.titleScale, "INVENTORY", 1.0f, 0.75f, 0.3f);

    const float bs = (float)L.bodyScale;
    for (int i = 0; i < (int)m.slots.size() && i < (int)L.cards.size(); i++) {
        const InvSlot& s = m.slots[i];
        const InvRect& c = L.cards[i];
        const bool sel = m.sel == i, dim = !s.takable && !s.inHand;
        const float* t = kTint[std::min(std::max(s.group, 0), 2)];
        const float k = sel ? 1.7f : s.inHand ? 1.35f : 1.0f;
        r.rect(c.x, c.y, c.w, c.h, t[0] * k, t[1] * k, t[2] * k, 1.0f);
        // A soft light behind the icon, so the dark things (tires, steel) stand off the slot.
        {
            const float gx = c.cx(), gy = c.cy(), gr = c.w * 0.46f;
            for (int j = 0; j < 7; j++) r.arc(gx, gy, 0, gr * (1.0f - j * 0.11f), 0, 6.2831853f, 1, 1, 1, (sel ? 0.020f : 0.014f) * (dim ? 0.5f : 1.0f), 24);
        }
        const float in = std::max(2.0f, std::round(2 * u));
        const float side = c.w - 2 * in;
        r.icon("inv:" + s.model, trunk::itemMesh(s.model), c.x + in, c.y + in, side, side, dim ? 0.5f : 1.0f, dim ? 0.55f : 1.0f);
        // How many are in the back: a mark in the lower corner (the sandbox's supply never runs out).
        const bool none = s.left <= 0 && !s.inHand;
        const std::string count = s.endless ? "INF" : s.left > 0 ? "X" + std::to_string(s.left) : s.inHand ? "HELD" : "X0";
        {
            const float tw = textW(count, bs) + 2 * std::round(2 * u), th = 8 * bs + 2 * std::round(1.5f * u);
            const float tx = c.x + c.w - tw - 1, ty = c.y + c.h - th - 1;
            r.rect(tx, ty, tw, th, 0.0f, 0.0f, 0.0f, 0.72f);
            r.text(tx + std::round(2 * u), ty + std::round(1.5f * u), bs, count, none ? 0.7f : s.endless ? 0.6f : 1.0f, none ? 0.4f : s.endless ? 0.9f : 0.85f, none ? 0.4f : s.endless ? 0.65f : 0.4f);
        }
        // Its state, as a dot in the top corner: amber in your hand, blue out working (on the truck, in the ground, on a line).
        if (s.inHand || !s.state.empty()) {
            const float d = std::max(5.0f, std::round(7 * u)), dx = c.x + std::round(3 * u), dy = c.y + std::round(3 * u);
            r.rect(dx - 1, dy - 1, d + 2, d + 2, 0.0f, 0.0f, 0.0f, 0.8f);
            if (s.inHand) r.rect(dx, dy, d, d, 1.0f, 0.78f, 0.25f, 1.0f);
            else r.rect(dx, dy, d, d, 0.35f, 0.65f, 1.0f, 1.0f);
        }
        if (sel) frame(r, c, std::max(2.0f, std::round(2 * u)), 1.0f, 0.75f, 0.3f, 1.0f);
        else if (s.inHand) frame(r, c, line, 1.0f, 0.75f, 0.3f, 0.8f);
    }
    if (m.slots.empty()) r.text(L.desc.x, L.desc.y, bs, "NOTHING IN THE BACK", 0.7f, 0.72f, 0.76f);

    // What the chosen one is: its name, what it's for, and what choosing it does.
    r.rect(L.desc.x - std::round(4 * u), L.desc.y, L.desc.w + std::round(8 * u), L.desc.h, 0.0f, 0.0f, 0.0f, 0.35f);
    if (const InvSlot* s = m.chosen()) {
        const float ls = 8 * bs + std::max(1.0f, std::round(2 * u)), pd = std::max(3.0f, std::round(4 * u));
        float y = L.desc.y + pd;
        r.text(L.desc.x, y, (float)L.nameScale, s->name, 1.0f, 0.78f, 0.3f);
        y += 8.0f * L.nameScale + std::max(2.0f, std::round(3 * u));
        for (const std::string& t : invWrap(InvMenu::blurb(*s), bs, L.desc.w)) r.text(L.desc.x, y, bs, t, 0.9f, 0.92f, 0.95f), y += ls;
        y = L.desc.y + L.desc.h - pd - ls + std::max(1.0f, std::round(2 * u));
        const std::string act = padLast ? pad::hint("", pad::B_A) : "E";
        if (s->inHand) r.text(L.desc.x, y, bs, act + ": PUT BACK", 1.0f, 0.78f, 0.3f);
        else if (s->takable) r.text(L.desc.x, y, bs, act + ": TAKE" + (s->state.empty() ? "" : "   (" + s->state + ")"), 0.6f, 0.95f, 0.65f);
        else r.text(L.desc.x, y, bs, s->state.empty() ? "NONE LEFT IN THE BACK" : s->state, 0.45f, 0.7f, 1.0f);
    }
    const float ls = 8 * bs + std::max(1.0f, std::round(2 * u));
    float fy = L.foot.y;
    for (const std::string& t : invWrap(InvMenu::keys(padLast), bs, L.foot.w)) r.text(L.foot.x, fy, bs, t, 0.62f, 0.58f, 0.4f), fy += ls;
}
