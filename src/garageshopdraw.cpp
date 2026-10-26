#include "garageshopdraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hudkit.h"
#include "vehicles.h"

using namespace hud;

namespace shop {

namespace {
const float kAmber[3] = {1.0f, 0.75f, 0.3f};
const float kGrey[3] = {0.62f, 0.64f, 0.68f};
const float kPanel[3] = {0.03f, 0.035f, 0.045f};
const float kGood[3] = {0.45f, 0.95f, 0.5f};
const float kBad[3] = {1.0f, 0.45f, 0.38f};

// The text that fits `w` px at scale `s` (an ellipsis-ish ".." when cut).
std::string fit(const std::string& t, float w, float s) {
    const int n = std::max(0, (int)(w / (6 * s)));
    if ((int)t.size() <= n) return t;
    return n > 2 ? t.substr(0, n - 2) + ".." : t.substr(0, n);
}

std::string num(double v, int decimals) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

// The dark down the left (the menu's side), fading out to the right, and along the bottom for the hints.
void shade(Renderer& r, int W, int H, float widthFrac, float peak, float a) {
    const int bands = 96;
    const float w = W * widthFrac;
    for (int i = 0; i < bands; i++) {
        const float t = (i + 0.5f) / bands, al = peak * (1 - t) * (1 - t);
        const float x0 = std::floor(w * i / bands), x1 = std::floor(w * (i + 1) / bands);
        r.rect(x0, 0, x1 - x0, (float)H, 0.02f, 0.02f, 0.03f, al * a);
    }
    for (int i = 0; i < 10; i++) r.rect(0, H - H * 0.12f * (10 - i) / 10.0f, (float)W, H * 0.12f / 10 + 1, 0.02f, 0.02f, 0.03f, 0.42f * a / 10.0f);
}

const float* stateColor(const Item& it) {
    static const float white[3] = {0.9f, 0.9f, 0.92f};
    switch (it.state) {
        case ST_FITTED: return kGood;
        case ST_FOR_SALE: return kAmber;
        case ST_LOCKED: return kBad;
        default: return white;
    }
}

std::string stateText(const Shop& s, const Item& it) {
    if (it.group == G_PAINT) return "";
    if (it.group == G_GEAR) {
        const std::string have = it.carried < 0 ? "" : it.tool ? (it.carried ? "YOU HAVE ONE" : "") : (it.carried ? "YOU CARRY " + std::to_string(it.carried) : "");
        if (!s.career) return have;
        return "$" + std::to_string(it.price) + (have.empty() ? "" : "   " + have);
    }
    switch (it.state) {
        case ST_FITTED: return "FITTED";
        case ST_OWNED: return s.career ? "OWNED" : "FREE";
        case ST_FOR_SALE: return "$" + std::to_string(it.price);
        case ST_LOCKED: return "LOCKED";
        default: return "";
    }
}
}  // namespace

// A frame round a rectangle.
static void outline(Renderer& r, float x, float y, float w, float h, float t, const float* c, float a) {
    r.rect(x, y, w, t, c[0], c[1], c[2], a);
    r.rect(x, y + h - t, w, t, c[0], c[1], c[2], a);
    r.rect(x, y, t, h, c[0], c[1], c[2], a);
    r.rect(x + w - t, y, t, h, c[0], c[1], c[2], a);
}

// The PAINT page's card: the swatches (16 presets), the hue / saturation / brightness sliders, the finish.
static void drawPaintCard(Renderer& r, Shop& s, int prow, float cx, float y0, float pw, float u, float a, bool padLast) {
    const float small = px(1.2f * u), ns = px(2.2f * u), lh = 8 * small + 7 * u;
    const bool picker = prow == PR_BODY || prow == PR_ACCENT;
    const bool editing = s.focus == F_PICK;
    const int which = picker ? prow : s.pickRow;
    const bool set = which == PR_BODY ? s.paint.bodySet : s.paint.accentSet;
    const uint8_t* cur = which == PR_BODY ? s.paint.body : s.paint.accent;
    const float sw = (pw - 12 * u) / 8.0f, swH = sw * 0.8f;
    const float sliderH = 14 * u, gap = 10 * u;
    const float cardH = picker ? 16 * u + 8 * ns + lh * 2 + 2 * swH + 3 * (sliderH + lh + gap) + lh * 2 + 10 * u : 16 * u + 8 * ns + lh * 4 + 10 * u;
    pill(r, cx - 8 * u, y0 - 8 * u, pw + 16 * u, cardH, kPanel[0], kPanel[1], kPanel[2], 0.82f * a);
    float y = y0 + 6 * u;
    std::string name = prow >= 0 ? s.list[s.sel].name : std::string("PAINT");
    label(r, cx + 6 * u, y, ns, fit(name, pw - 12 * u, ns), kAmber[0], kAmber[1], kAmber[2], a);
    y += 8 * ns + 10 * u;
    if (!picker) {
        const std::string d = prow >= 0 ? s.list[s.sel].detail : std::string("BODY COLOUR, ACCENT AND FINISH");
        r.text(cx + 6 * u, y, small, fit(d, pw - 12 * u, small), 0.85f, 0.85f, 0.88f, a);
        y += lh * 1.4f;
        const std::string how = prow == PR_FINISH ? "ENTER OR LEFT AND RIGHT CHANGES THE FINISH" : prow == PR_FACTORY ? "ENTER PUTS THE FACTORY PAINT BACK" : "PICK A ROW";
        r.text(cx + 6 * u, y, small, fit(how, pw - 12 * u, small), kGrey[0], kGrey[1], kGrey[2], a);
        y += lh * 1.4f;
        r.text(cx + 6 * u, y, small, "SAVED WITH YOUR GARAGE; EVERYONE IN A SESSION SEES IT", kGrey[0], kGrey[1], kGrey[2], a * 0.8f);
        return;
    }
    // The colour now, and its name.
    double hr, hg, hb;
    hsvToRgb(s.hsv[0], s.hsv[1], s.hsv[2], hr, hg, hb);
    const float cr = set ? cur[0] / 255.0f : (float)hr, cg = set ? cur[1] / 255.0f : (float)hg, cb = set ? cur[2] / 255.0f : (float)hb;
    r.rect(cx + 6 * u, y, 28 * u, lh * 1.7f, cr, cg, cb, a * (set ? 1.0f : 0.45f));
    outline(r, cx + 6 * u, y, 28 * u, lh * 1.7f, 1.5f * u, kGrey, a);
    const std::string pname = set ? (paintPresetOf(cur) >= 0 ? std::string(paintPresets()[paintPresetOf(cur)].name) : std::string("CUSTOM")) : std::string("FACTORY");
    char hex[16];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X", (int)std::lround(cr * 255), (int)std::lround(cg * 255), (int)std::lround(cb * 255));
    r.text(cx + 42 * u, y + 2 * u, small, fit(pname, pw - 60 * u, small), 0.92f, 0.92f, 0.95f, a);
    r.text(cx + 42 * u, y + 2 * u + lh * 0.85f, small, set ? std::string("#") + hex : std::string("PICK TO REPAINT"), kGrey[0], kGrey[1], kGrey[2], a);
    y += lh * 2;
    // The 16 presets, 8 across.
    const auto& pr = paintPresets();
    const int at = set ? paintPresetOf(cur) : -1;
    for (int i = 0; i < (int)pr.size(); i++) {
        const float x = cx + 6 * u + (i % 8) * sw, yy = y + (i / 8) * swH;
        r.rect(x + 1.5f * u, yy + 1.5f * u, sw - 3 * u, swH - 3 * u, pr[i].rgb[0] / 255.0f, pr[i].rgb[1] / 255.0f, pr[i].rgb[2] / 255.0f, a);
        if (i == at) outline(r, x, yy, sw, swH, 2 * u, editing && s.ctl == PC_PRESET ? kAmber : kGrey, a);
        s.hits.push_back({x, yy, sw, swH, 2, i});
    }
    if (editing && s.ctl == PC_PRESET) outline(r, cx + 3 * u, y - 3 * u, 8 * sw + 6 * u, 2 * swH + 6 * u, 1.5f * u, kAmber, 0.6f * a);
    y += 2 * swH + gap;
    // The sliders.
    const char* lab[3] = {"HUE", "SATURATION", "BRIGHTNESS"};
    const float trackX = cx + 6 * u, trackW = pw - 12 * u;
    for (int k = 0; k < 3; k++) {
        const bool on = editing && s.ctl == PC_HUE + k;
        const double v = s.hsv[k];
        char val[24];
        std::snprintf(val, sizeof val, k == 0 ? "%d DEG" : "%d%%", (int)std::lround(k == 0 ? v * 360 : v * 100));
        r.text(trackX, y, small, lab[k], on ? kAmber[0] : kGrey[0], on ? kAmber[1] : kGrey[1], on ? kAmber[2] : kGrey[2], a);
        r.text(trackX + trackW - r.textWidth(val, small), y, small, val, 0.9f, 0.9f, 0.92f, a);
        y += lh - 2 * u;
        const int segs = 32;
        for (int g = 0; g < segs; g++) {
            const double t = (g + 0.5) / segs;
            double R, G, B;
            if (k == 0) hsvToRgb(t, 1, 1, R, G, B);
            else if (k == 1) hsvToRgb(s.hsv[0], t, s.hsv[2], R, G, B);
            else hsvToRgb(s.hsv[0], s.hsv[1], t, R, G, B);
            r.rect(trackX + trackW * g / segs, y, trackW / segs + 1, sliderH, (float)R, (float)G, (float)B, a * (set || editing ? 1.0f : 0.6f));
        }
        const float hx = trackX + (float)v * trackW;
        r.rect(hx - 2 * u, y - 3 * u, 4 * u, sliderH + 6 * u, 1, 1, 1, a);
        outline(r, trackX, y, trackW, sliderH, 1.2f * u, on ? kAmber : kGrey, a * (on ? 1.0f : 0.6f));
        s.sliders[k] = {trackX, trackW};
        s.hits.push_back({trackX, y - 4 * u, trackW, sliderH + 8 * u, 3, k});
        y += sliderH + gap;
    }
    r.text(cx + 6 * u, y, small, fit(editing ? (padLast ? "UP DOWN PICK A CONTROL, LEFT RIGHT CHANGE IT, A DONE" : "W S PICK A CONTROL, A D CHANGE IT, ENTER DONE") : (padLast ? "A TO EDIT THE COLOUR" : "ENTER OR CLICK TO EDIT THE COLOUR"), pw - 12 * u, small), kGrey[0], kGrey[1], kGrey[2], a);
}

void draw(Renderer& r, Shop& s, const Career& c, const std::string& where, int W, int H, float a, bool padLast, bool pending, const std::string& toast, float toastA) {
    s.hits.clear();
    const float u = std::max(0.75f, H / 720.0f);
    shade(r, W, H, 0.62f, 0.74f, a);
    const float x0 = std::max(30 * u, W * 0.04f), small = px(1.2f * u), mid = px(1.6f * u);
    // Title and the money.
    const float tsz = px(3.0f * u), ty = std::max(18 * u, H * 0.05f);
    label(r, x0, ty, tsz, s.title(), kAmber[0], kAmber[1], kAmber[2], a);
    r.rect(x0 + 2 * u, ty + 8 * tsz + 4 * u, std::min(W * 0.4f, r.textWidth(s.title(), tsz)), 2 * u, kAmber[0], kAmber[1], kAmber[2], 0.8f * a);
    {
        const std::string top = s.career ? "CASH $" + std::to_string(c.cash) + (where.empty() ? "" : "   AT " + where) : std::string("SANDBOX: EVERY PART IS YOURS, FREE");
        const float ms = px(1.6f * u);
        label(r, W - x0 - r.textWidth(top, ms), ty + 4 * u, ms, top, s.career ? 0.55f : 0.85f, s.career ? 0.95f : 0.85f, s.career ? 0.6f : 0.88f, a);
    }
    r.text(x0, H - 28 * u, small, s.hint(padLast), 0.72f, 0.74f, 0.78f, a * 0.9f);
    if (!toast.empty() && toastA > 0) {  // what the last press said, over the hint line
        const float ts = px(1.6f * u), w = r.textWidth(toast, ts) + 24 * u;
        pill(r, x0 - 4 * u, H - 62 * u, w, 8 * ts + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a * toastA);
        r.text(x0 + 8 * u, H - 56 * u, ts, toast, 0.95f, 0.92f, 0.85f, a * toastA);
    }

    const float top = ty + 8 * tsz + 22 * u, bottom = H - 50 * u;
    const auto& cats = categories();
    // Column 0: the categories.
    const float cw = std::min(W * 0.21f, 270 * u), ch = std::min(36 * u, (bottom - top) / std::max<size_t>(1, cats.size()) - 5 * u), cgap = 5 * u;
    const float cts = px(1.4f * u);
    for (int i = 0; i < (int)cats.size(); i++) {
        const float y = top + i * (ch + cgap);
        const bool focus = i == s.cat;
        const bool active = focus && s.focus == F_CATS;
        if (focus) {
            pill(r, x0, y, cw, ch, kPanel[0], kPanel[1], kPanel[2], (active ? 0.85f : 0.6f) * a);
            r.rect(x0, y + 2 * u, 4 * u, ch - 4 * u, kAmber[0], kAmber[1], kAmber[2], (active ? 1.0f : 0.5f) * a);
        } else {
            r.rect(x0, y, cw, ch, kPanel[0], kPanel[1], kPanel[2], 0.34f * a);
        }
        const float lit = focus ? 1 : 0;
        label(r, x0 + 14 * u, y + (ch - 8 * cts) / 2, cts, fit(cats[i].name, cw - 30 * u, cts), 0.9f + 0.1f * lit, 0.9f - 0.15f * lit, 0.92f - 0.62f * lit, a);
        if (focus && s.focus == F_ITEMS) r.text(x0 + cw - 14 * u, y + (ch - 8 * cts) / 2, cts, ">", kAmber[0], kAmber[1], kAmber[2], a);
        s.hits.push_back({x0, y, cw, ch, 0, i});
    }
    // Column 1: the focused category's parts (shown dimmed while the categories have the focus).
    const float x1 = x0 + cw + 12 * u, iw = std::min(W * 0.3f, 440 * u), ih = 40 * u, igap = 4 * u;
    const int vis = std::max(1, (int)((bottom - top + igap) / (ih + igap)));
    const int n = (int)s.list.size();
    int first = s.listFirst < 0 ? s.sel - vis / 2 : s.listFirst;
    if (s.sel < first) first = s.sel;
    else if (s.sel >= first + vis) first = s.sel - vis + 1;
    first = std::max(0, std::min(n - vis, first));
    s.listFirst = first;
    const bool itemsOn = s.focus == F_ITEMS;
    const float its = px(1.4f * u);
    for (int k = 0; k < std::min(vis, n - first); k++) {
        const int i = first + k;
        const Item& it = s.list[i];
        const float y = top + k * (ih + igap);
        const bool focus = itemsOn && i == s.sel;
        const float da = itemsOn ? 1.0f : 0.75f;
        if (focus) {
            pill(r, x1, y, iw, ih, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
            r.rect(x1, y + 2 * u, 4 * u, ih - 4 * u, kAmber[0], kAmber[1], kAmber[2], a);
        } else {
            r.rect(x1, y, iw, ih, kPanel[0], kPanel[1], kPanel[2], 0.34f * a * da);
        }
        const std::string st = stateText(s, it);
        const float* sc = stateColor(it);
        const float stw = r.textWidth(st, small);
        const float nameW = iw - 30 * u;
        const bool dim = it.state == ST_LOCKED;
        label(r, x1 + 14 * u, y + 7 * u, its, fit(it.name, nameW, its), focus ? kAmber[0] : dim ? 0.6f : 0.9f, focus ? kAmber[1] : dim ? 0.6f : 0.9f,
              focus ? kAmber[2] : dim ? 0.62f : 0.92f, a * da);
        const std::string sub = it.group == G_GEAR ? std::string() : it.state == ST_LOCKED ? std::string("FIND IT OR WIN IT") : it.detail;
        r.text(x1 + 14 * u, y + ih - 8 * small - 7 * u, small, fit(sub, iw - 40 * u - stw, small), kGrey[0], kGrey[1], kGrey[2], a * da);
        r.text(x1 + iw - 12 * u - stw, y + ih - 8 * small - 7 * u, small, st, sc[0], sc[1], sc[2], a * da);
        s.hits.push_back({x1, y, iw, ih, 1, i});
    }
    if (n > vis) {  // (a scroll bar)
        const float bh = bottom - top, th = bh * vis / n, tyy = top + bh * first / n;
        r.rect(x1 + iw + 4 * u, top, 3 * u, bh, 1, 1, 1, 0.08f * a);
        r.rect(x1 + iw + 4 * u, tyy, 3 * u, th, kAmber[0], kAmber[1], kAmber[2], 0.7f * a);
    }
    s.turntableX = x1 + iw + 12 * u;

    // The card: the part and its numbers, top right.
    const float px0 = x1 + iw + 26 * u, pw = std::min(440 * u, W - px0 - x0);
    if (pw < 180 * u) return;
    const float cx = W - x0 - pw;
    if (const int prow = s.paintRow(); prow >= 0 || s.category().group == G_PAINT) {
        drawPaintCard(r, s, prow, cx, top, pw, u, a, padLast);
        return;
    }
    const Item* it = s.item();
    float y = top;
    std::vector<std::pair<std::string, const float*>> lines;  // (built first: the panel's height)
    const float ns = px(2.2f * u);
    std::string name, detail, state, action;
    const float* stateCol = kGrey;
    std::vector<Stat> stats;
    if (!it) {
        const Category& cat = s.category();
        name = cat.name;
        detail = cat.blurb;
        if (cat.group == G_MOD) {
            for (const Item& x : s.list)
                if (x.state == ST_FITTED) state = "FITTED: " + x.name;
            int owned = 0, sale = 0;
            for (const Item& x : s.list) owned += x.state == ST_OWNED || x.state == ST_FITTED, sale += x.state == ST_FOR_SALE;
            action = std::to_string(s.list.size()) + " PARTS" + (s.career ? "   " + std::to_string(owned) + " YOURS   " + std::to_string(sale) + " FOR SALE" : "");
        } else {
            action = std::to_string(s.list.size()) + " ON THE RACK";
        }
    } else {
        name = it->name;
        detail = it->detail;
        stateCol = stateColor(*it);
        state = stateText(s, *it);
        if (it->state == ST_LOCKED) state = "LOCKED: " + (it->where.empty() ? std::string("FIND IT OUT THERE, OR WIN IT") : it->where);
        if (it->group == G_MOD) {
            stats = compare(s.vehicle, s.fitted, withItem(s.fitted, *it));
            if (s.career && it->state != ST_LOCKED) state += "   TIER " + std::to_string(it->tier);
        }
        action = s.actionLabel(c);
    }
    const float lh = 8 * small + 7 * u;
    const float cardH = 16 * u + 8 * ns + 10 * u + lh * 2 + (stats.empty() ? 0 : lh * (stats.size() + 0.6f)) + lh * 1.6f + 10 * u;
    pill(r, cx - 8 * u, y - 8 * u, pw + 16 * u, cardH, kPanel[0], kPanel[1], kPanel[2], 0.82f * a);
    y += 6 * u;
    label(r, cx + 6 * u, y, ns, fit(name, pw - 12 * u, ns), kAmber[0], kAmber[1], kAmber[2], a);
    y += 8 * ns + 10 * u;
    r.text(cx + 6 * u, y, small, fit(detail, pw - 12 * u, small), 0.85f, 0.85f, 0.88f, a);
    y += lh;
    r.text(cx + 6 * u, y, small, fit(state, pw - 12 * u, small), stateCol[0], stateCol[1], stateCol[2], a);
    y += lh;
    if (!stats.empty()) {
        y += lh * 0.3f;
        const float c1 = cx + 6 * u, c2 = cx + 6 * u + 13 * 6 * small, c3 = cx + pw - 6 * u;
        for (const Stat& st : stats) {
            const double d = st.with - st.now;
            const bool same = std::fabs(d) < (st.decimals >= 2 ? 0.005 : st.decimals == 1 ? 0.05 : 0.5);
            const int sign = same ? 0 : d > 0 ? 1 : -1;
            const float* col = st.better == 0 || sign == 0 ? kGrey : sign * st.better > 0 ? kGood : kBad;
            r.text(c1, y, small, st.label, kGrey[0], kGrey[1], kGrey[2], a);
            const std::string vals = num(st.now, st.decimals) + " > " + num(st.with, st.decimals) + (st.unit.empty() ? "" : " " + st.unit);
            r.text(c2, y, small, vals, 0.9f, 0.9f, 0.92f, a);
            if (!same) {
                const std::string ds = (d > 0 ? "+" : "") + num(d, st.decimals);
                r.text(c3 - r.textWidth(ds, small), y, small, ds, col[0], col[1], col[2], a);
            }
            y += lh;
        }
    }
    y += lh * 0.3f;
    if (!action.empty()) {
        const std::string key = !it ? (padLast ? "A " : "ENTER ") : (padLast ? "A " : "ENTER ");
        const bool afford = it && c.cash >= it->price;
        const bool can = it ? (it->state == ST_OWNED || (it->state == ST_FOR_SALE && afford) || (it->group == G_GEAR && s.career && afford && !(it->tool && it->carried > 0))) : true;
        r.text(cx + 6 * u, y, mid, (can && it ? key : std::string()) + action, can ? kAmber[0] : kGrey[0], can ? kAmber[1] : kGrey[1], can ? kAmber[2] : kGrey[2], a);
    }

    // Trying a part on: say so over the truck.
    if (s.previewing && it) {
        const std::string t = (pending ? "FITTING TO TRY: " : "TRYING ON: ") + it->name + (padLast ? "   X TAKE IT OFF" : "   T TAKE IT OFF");
        const float ts = px(1.6f * u);
        const float w = r.textWidth(t, ts) + 28 * u, bx = std::min((float)W - x0 - w, std::max(px0, (W - w) * 0.5f + W * 0.15f)), by = H - 80 * u;
        pill(r, bx, by, w, 8 * ts + 16 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        r.rect(bx, by + 8 * ts + 13 * u, w, 3 * u, kAmber[0], kAmber[1], kAmber[2], a);
        r.text(bx + 14 * u, by + 8 * u, ts, t, 0.95f, 0.92f, 0.85f, a);
    }
}

}  // namespace shop
