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

// Breaks `t` into lines no wider than `w` px at scale `s`.
std::vector<std::string> wrap(const std::string& t, float s, float w) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find(' ', i);
        if (j == std::string::npos) j = t.size();
        const std::string word = t.substr(i, j - i);
        if (!line.empty() && textW(line + " " + word, s) > w) out.push_back(line), line.clear();
        line += (line.empty() ? "" : " ") + word;
        i = j + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace

void drawInvMenu(Renderer& r, const InvMenu& m, const InvLayout& L, int W, int H, bool padLast) {
    const float u = L.u;
    r.rect(0, 0, (float)W, (float)H, 0.0f, 0.0f, 0.02f, 0.42f);  // (the world behind, dimmed)
    pill(r, L.panel.x, L.panel.y, L.panel.w, L.panel.h, 0.02f, 0.025f, 0.035f, 1.0f);
    frame(r, L.panel, std::max(1.0f, std::round(u)), 1.0f, 0.75f, 0.3f, 0.35f);
    const float ts = (float)L.titleScale;
    label(r, L.title.x, L.title.y, ts, m.gate + " INVENTORY", 1.0f, 0.75f, 0.3f);

    // The buttons: PUT BACK (with what you hold) and SHUT.
    auto button = [&](const InvRect& b, bool on, const std::string& text, bool amber) {
        const float cr = amber ? 0.20f : 0.10f, cg = amber ? 0.15f : 0.12f, cb = amber ? 0.05f : 0.15f;
        pill(r, b.x, b.y, b.w, b.h, on ? cr * 1.8f : cr, on ? cg * 1.8f : cg, on ? cb * 1.8f : cb, 0.95f);
        frame(r, b, on ? std::max(2.0f, std::round(2 * u)) : 1.0f, amber ? 1.0f : 0.6f, amber ? 0.75f : 0.65f, amber ? 0.3f : 0.7f, on ? 1.0f : 0.5f);
        const float s = (float)L.bodyScale;
        r.text(b.x + b.w - 12 * u - textW(text, s), b.y + (b.h - 8 * s) / 2, s, text, on ? 1.0f : 0.88f, on ? 0.85f : 0.88f, on ? 0.5f : 0.9f);
    };
    if (m.holding && L.putBack.w > 0) {
        const bool on = m.sel == INV_PUT_BACK;
        button(L.putBack, on, "PUT BACK  " + m.heldName, true);
        if (!m.heldModel.empty()) {
            const float side = L.putBack.h - 6 * u;
            r.icon("inv:" + m.heldModel, trunk::itemMesh(m.heldModel), L.putBack.x + 4 * u, L.putBack.y + 3 * u, side, side);
        }
    }
    if (m.canShut && L.shut.w > 0) button(L.shut, m.sel == INV_SHUT, std::string(padLast ? "Y" : "C") + "  SHUT THE " + m.gate, false);

    // Section heads, then the cards.
    const float hs = (float)px(1.5f * u);
    for (const InvLayout::Head& h : L.heads) {
        r.text(h.x, h.y, hs, h.text, 0.55f, 0.62f, 0.7f);
        r.rect(h.x + textW(h.text, hs) + 8 * u, h.y + 4 * hs, L.panel.x + L.panel.w - 20 * u - (h.x + textW(h.text, hs) + 8 * u), std::max(1.0f, std::round(u * 0.8f)), 1, 1, 1, 0.10f);
    }
    for (int i = 0; i < (int)m.slots.size() && i < (int)L.cards.size(); i++) {
        const InvSlot& s = m.slots[i];
        const InvRect& c = L.cards[i];
        const bool sel = m.sel == i, dim = !s.takable && !s.inHand;
        pill(r, c.x, c.y, c.w, c.h, sel ? 0.17f : s.inHand ? 0.13f : 0.075f, sel ? 0.15f : s.inHand ? 0.11f : 0.085f, sel ? 0.09f : s.inHand ? 0.06f : 0.10f, 0.96f);
        if (sel) frame(r, c, std::max(2.0f, std::round(2 * u)), 1.0f, 0.75f, 0.3f, 1.0f);
        else if (s.inHand) frame(r, c, std::max(1.0f, std::round(u)), 1.0f, 0.75f, 0.3f, 0.55f);
        // A soft light behind the icon, so the dark things (tires, steel) stand off the card.
        {
            const float gx = c.x + c.w / 2, gy = c.y + 2 * u + (L.iconArea.h - 4 * u) / 2, gr = (L.iconArea.h - 4 * u) * 0.5f;
            for (int k = 0; k < 11; k++) r.arc(gx, gy, 0, gr * (1.0f - k * 0.08f), 0, 6.2831853f, 1, 1, 1, (sel ? 0.016f : 0.011f) * (dim ? 0.5f : 1.0f), 28);
        }
        // The icon: square, centred in the top of the card, a touch larger when chosen.
        const float side = L.iconArea.h - 4 * u, grow = sel ? 1.07f : 1.0f;
        const float isz = side * grow;
        r.icon("inv:" + s.model, trunk::itemMesh(s.model), c.x + (c.w - isz) / 2, c.y + 2 * u + (side - isz) / 2, isz, isz, dim ? 0.55f : 1.0f, dim ? 0.55f : 1.0f);
        // How many are in the back: a badge in the corner (the sandbox's supply never runs out).
        const float ns = (float)L.nameScale, ss = (float)L.smallScale;
        const bool none = s.left <= 0 && !s.inHand;
        const std::string count = s.endless ? "ENDLESS" : none ? "NONE" : s.left > 0 ? "X" + std::to_string(s.left) : "";
        if (!count.empty()) {
            const float cs = s.endless || none ? ss : ns, tw = textW(count, cs) + 8 * u;
            const float tx = c.x + c.w - tw - 4 * u, ty = c.y + 4 * u;
            pill(r, tx, ty, tw, 8 * cs + 6 * u, 0.0f, 0.0f, 0.0f, 0.72f);
            r.text(tx + 4 * u, ty + 3 * u, cs, count, none ? 0.85f : s.endless ? 0.6f : 1.0f, none ? 0.45f : s.endless ? 0.9f : 0.82f, none ? 0.4f : s.endless ? 0.65f : 0.35f);
        }
        // Its state, as a tag along the icon's foot.
        if (!s.state.empty()) {
            const float tw = textW(s.state, ss) + 8 * u;
            const float tx = c.x + (c.w - tw) / 2, ty = c.y + L.iconArea.h - 8 * ss - 8 * u;
            pill(r, tx, ty, tw, 8 * ss + 5 * u, s.inHand ? 1.0f : 0.25f, s.inHand ? 0.75f : 0.45f, s.inHand ? 0.3f : 0.65f, 0.96f);
            r.text(tx + 4 * u, ty + 2.5f * u, ss, s.state, 0.05f, 0.05f, 0.07f);
        }
        const float ny = c.y + L.iconArea.h + 4 * u;
        r.text(c.x + (c.w - textW(s.name, ns)) / 2, ny, ns, s.name, sel ? 1.0f : dim ? 0.55f : 0.92f, sel ? 0.85f : dim ? 0.56f : 0.93f, sel ? 0.5f : dim ? 0.6f : 0.95f);
    }
    if (m.slots.empty()) r.text(L.desc.x, L.desc.y - 40 * u, ts, "NOTHING IN THE BACK", 0.7f, 0.72f, 0.76f);

    // What the chosen one is.
    const float bs = (float)L.bodyScale;
    std::string line;
    if (const InvSlot* s = m.chosen()) {
        line = s->desc;
        if (s->inHand) line += "   (IN YOUR HAND: CHOOSE IT TO PUT IT BACK)";
    } else if (m.sel == INV_PUT_BACK) line = "PUT THE " + m.heldName + " BACK IN THE " + m.gate;
    else if (m.sel == INV_SHUT) line = "SHUT THE " + m.gate + " WHEN YOU'RE DONE";
    else line = "CHOOSE SOMETHING TO TAKE IT IN YOUR HAND";
    pill(r, L.desc.x - 6 * u, L.desc.y, L.desc.w + 12 * u, L.desc.h, 0.0f, 0.0f, 0.0f, 0.35f);
    const std::vector<std::string> lines = wrap(line, bs, L.desc.w - 8 * u);
    const size_t colon = line.find(':');
    size_t used = 0;
    for (size_t k = 0; k < lines.size() && k < 2; k++) {
        const float ly = L.desc.y + 8 * u + k * (8 * bs + 4 * u);
        // (the name up to its colon in amber, the rest light)
        const std::string& t = lines[k];
        const size_t head = colon != std::string::npos && used < colon ? std::min(t.size(), colon + 1 - used) : 0;
        if (head) r.text(L.desc.x, ly, bs, t.substr(0, head), 1.0f, 0.75f, 0.3f);
        r.text(L.desc.x + textW(t.substr(0, head), bs), ly, bs, t.substr(head), 0.9f, 0.92f, 0.95f);
        used += t.size() + 1;
    }

    std::string keys;
    if (padLast) keys = std::string("D-PAD: MOVE   ") + pad::hint("", pad::B_A) + ": CHOOSE" + (m.canShut ? std::string("   ") + pad::hint("", pad::B_Y) + ": SHUT THE " + m.gate : "") + "   " + pad::hint("", pad::B_B) + ": CLOSE";
    else keys = std::string("WASD OR ARROWS: MOVE   E OR CLICK: CHOOSE") + (m.canShut ? "   C: SHUT THE " + m.gate : "") + "   ESC OR TAB: CLOSE";
    r.text(L.foot.x, L.foot.y, bs, keys, 1.0f, 0.75f, 0.3f);
}
