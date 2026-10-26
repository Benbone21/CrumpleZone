#include "mainmenudraw.h"

#include <algorithm>
#include <cmath>

#include "gamepad.h"
#include "hudkit.h"
#include "menuscene.h"
#include "vehicles.h"
#include "world.h"

using namespace hud;

namespace mainmenu {

namespace {
const float kAmber[3] = {1.0f, 0.75f, 0.3f};
const float kGrey[3] = {0.62f, 0.64f, 0.68f};
const float kPanel[3] = {0.03f, 0.035f, 0.045f};

struct Ctx {
    Renderer& r;
    Nav& nav;
    int W, H;
    float a, u;
};

// A strip of dark across the left of the screen (the menu's side), fading out to the right.
void shade(Ctx& c, float widthFrac, float peak) {
    const int bands = 96;
    const float w = c.W * widthFrac;
    for (int i = 0; i < bands; i++) {
        const float t = (i + 0.5f) / bands, al = peak * (1 - t) * (1 - t);
        const float x0 = std::floor(w * i / bands), x1 = std::floor(w * (i + 1) / bands);
        c.r.rect(x0, 0, x1 - x0, (float)c.H, 0.02f, 0.02f, 0.03f, al * c.a);
    }
    // ...and the bottom, for the hints.
    for (int i = 0; i < 10; i++) c.r.rect(0, c.H - c.H * 0.14f * (10 - i) / 10.0f, (float)c.W, c.H * 0.14f / 10 + 1, 0.02f, 0.02f, 0.03f, 0.42f * c.a / 10.0f);
}

// One row of a list: a panel when focused with an amber bar on its left; the label, and a small second line.
void row(Ctx& c, int index, float x, float y, float w, float h, float ts, const std::string& label, const std::string& sub, bool focus, bool dim = false) {
    Renderer& r = c.r;
    const float a = c.a, u = c.u;
    if (focus) {
        pill(r, x, y, w, h, kPanel[0], kPanel[1], kPanel[2], 0.8f * a);
        r.rect(x, y + 2 * u, 4 * u, h - 4 * u, kAmber[0], kAmber[1], kAmber[2], a);
    } else {
        r.rect(x, y, w, h, kPanel[0], kPanel[1], kPanel[2], 0.34f * a);
    }
    const float small = px(1.2f * u), tx = x + 16 * u;
    const bool two = !sub.empty();
    const float th = 8 * ts, sh = 8 * small, ty = y + (two ? (h - th - sh - 5 * u) / 2 : (h - th) / 2);
    hud::label(r, tx, ty, ts, label, focus ? kAmber[0] : 0.9f, focus ? kAmber[1] : 0.9f, focus ? kAmber[2] : 0.92f, (dim ? 0.55f : 1.0f) * a);
    if (two) r.text(tx, ty + th + 5 * u, small, sub, kGrey[0], kGrey[1], kGrey[2], a);
    c.nav.hits.push_back({x, y, w, h, index});
}
}  // namespace

void draw(Renderer& r, Nav& nav, int W, int H, float a, double clock, bool padLast) {
    nav.hits.clear();
    Ctx c{r, nav, W, H, a, std::max(0.75f, H / 720.0f)};
    const float u = c.u;
    shade(c, nav.page == P_FRONT ? 0.55f : 0.62f, nav.page == P_FRONT ? 0.62f : 0.7f);
    const float x0 = std::max(40 * u, W * 0.06f), small = px(1.2f * u), mid = px(1.6f * u);
    // The bottom hint line.
    const std::string hint = nav.hint(padLast);
    r.text(x0, H - 30 * u, small, hint, 0.72f, 0.74f, 0.78f, a * 0.9f);

    if (nav.page == P_FRONT) {
        const float big = px(7.0f * u);
        const float ty = std::max(26 * u, H * 0.1f);
        label(r, x0, ty, big, "CRUMPLE ZONE", kAmber[0], kAmber[1], kAmber[2], a);
        r.text(x0 + 4 * u, ty + 8 * big + 10 * u, mid, "SOFT-BODY DRIVING: CRASH IT, CRAWL IT, RACE IT", 0.8f, 0.8f, 0.82f, a);
        r.rect(x0 + 2 * u, ty + 8 * big + 2 * u, big * 6 * 12 - 6 * big, 2 * u, kAmber[0], kAmber[1], kAmber[2], 0.8f * a);
        const float bw = std::min(W * 0.34f, 440 * u), bh = 54 * u, gap = 10 * u;
        float y = std::max(H * 0.37f, ty + 8 * big + 50 * u);
        const float ts = px(3.0f * u);
        row(c, F_SANDBOX, x0, y, bw, bh, ts, "SANDBOX", "EVERY PART ON EVERY VEHICLE, ANY MAP", nav.sel == F_SANDBOX);
        y += bh + gap;
        row(c, F_CAREER, x0, y, bw, bh, ts, "CAREER", "START STOCK: FIND, WIN AND BUY BETTER PARTS", nav.sel == F_CAREER);
        y += bh + gap;
        row(c, F_MULTI, x0, y, bw, bh, ts, "MULTIPLAYER", "JOIN A FRIEND WITH A CODE, OR HOST", nav.sel == F_MULTI);
        y += bh + gap;
        row(c, F_SETTINGS, x0, y, bw, bh, ts, "SETTINGS", "GRAPHICS, CONTROLS, AUDIO", nav.sel == F_SETTINGS);
        // Small DEV and QUIT, bottom left.
        const float sy = H - 74 * u, sw = 74 * u, sh = 28 * u, st = px(1.6f * u);
        const char* names[2] = {"DEV", "QUIT"};
        for (int i = 0; i < 2; i++) {
            const int f = F_DEV + i;
            const float sx = x0 + i * (sw + 10 * u);
            const bool foc = nav.sel == f;
            pill(r, sx, sy, sw, sh, kPanel[0], kPanel[1], kPanel[2], (foc ? 0.85f : 0.45f) * a);
            if (foc) r.rect(sx, sy + sh - 3 * u, sw, 3 * u, kAmber[0], kAmber[1], kAmber[2], a);
            r.text(sx + (sw - r.textWidth(names[i], st)) / 2, sy + (sh - 8 * st) / 2, st, names[i], foc ? kAmber[0] : 0.82f, foc ? kAmber[1] : 0.82f, foc ? kAmber[2] : 0.85f, a);
            nav.hits.push_back({sx, sy, sw, sh, f});
        }
        return;
    }

    // The pick pages and the DEV page: a title, a list on the left, a panel on the right.
    const float tsz = px(3.0f * u);
    label(r, x0, std::max(24 * u, H * 0.07f), tsz, nav.title(), kAmber[0], kAmber[1], kAmber[2], a);
    const float top = std::max(24 * u, H * 0.07f) + 8 * tsz + 22 * u;
    const float lw = std::min(W * 0.36f, 470 * u), rh = 46 * u, gap = 6 * u;
    const int n = nav.count();
    const float avail = H - top - 70 * u;
    const float rowH = std::min(rh, (avail - (n - 1) * gap) / std::max(1, n));
    const float ts = px(2.0f * u);
    for (int i = 0; i < n; i++) {
        const float y = top + i * (rowH + gap);
        if (nav.page == P_MAP) {
            const int m = listedMaps()[i];
            row(c, i, x0, y, lw, rowH, ts, mapPickName(m), "", nav.sel == i);
        } else if (nav.page == P_VEHICLE) {
            const VehicleSpec& vs = vehicleCatalog()[listedVehicles()[i]];
            row(c, i, x0, y, lw, rowH, ts, vs.name, "", nav.sel == i);
        } else {
            const DevEntry& e = devEntries()[i];
            std::string lab = e.label;
            if (e.act == A_DEV_HOTKEYS) lab += nav.devHotkeys ? ":  ON" : ":  OFF";
            if (e.act == A_DEV_DEBUG) lab += nav.debugOn ? ":  ON" : ":  OFF";
            row(c, i, x0, y, lw, rowH, ts, lab, "", nav.sel == i);
        }
    }
    // The right-hand panel.
    const float px0 = x0 + lw + 36 * u, pw = W - px0 - x0;
    if (pw < 200 * u) return;
    float py = top;
    if (nav.page == P_MAP) {
        const int m = nav.map();
        const float ph = std::min(pw * 9.0f / 16.0f, H - top - 150 * u), pwid = ph * 16.0f / 9.0f;
        pill(r, px0 - 6 * u, py - 6 * u, pwid + 12 * u, ph + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        const unsigned char* img = mapPreview(m);
        if (img) {
            const float cw = pwid / kPreviewW, ch = ph / kPreviewH;
            for (int j = 0; j < kPreviewH; j++)  // (runs of a colour along a row are one rect)
                for (int i = 0; i < kPreviewW;) {
                    const unsigned char* p = img + (j * kPreviewW + i) * 3;
                    int k = i + 1;
                    while (k < kPreviewW && std::abs(img[(j * kPreviewW + k) * 3] - p[0]) + std::abs(img[(j * kPreviewW + k) * 3 + 1] - p[1]) + std::abs(img[(j * kPreviewW + k) * 3 + 2] - p[2]) < 6) k++;
                    r.rect(px0 + i * cw, py + j * ch, (k - i) * cw + 0.6f, ch + 0.6f, p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, a);
                    i = k;
                }
        } else {
            r.rect(px0, py, pwid, ph, 0.12f, 0.14f, 0.12f, a);
        }
        py += ph + 20 * u;
        label(r, px0, py, px(2.4f * u), mapPickName(m), kAmber[0], kAmber[1], kAmber[2], a);
        py += 8 * px(2.4f * u) + 8 * u;
        const char* about = m == MAP_MOUNTAIN ? "START AT THE MOUNTAIN: SUMMIT TRAIL, CRAWL PARK, VALLEY LOOP"
                            : m == MAP_FREEWAY ? "THREE LANES EACH WAY THAT NEVER END: TOP SPEED"
                            : m == MAP_FRONTIER ? "START AT THE FRONTIER: DESERT, RIVERS, A RANGE, A RACE CIRCUIT"
                            : m == MAP_BACKCOUNTRY ? "8 KM: TRAILS, RIVERS AND PLACES, THE MOUNTAIN, FRONTIER AND OPEN COUNTRY ROUND IT"
                            : m == MAP_WILDS ? "START IN THE OPEN COUNTRY: ROOM TO TRY ANYTHING"
                                             : "THE PROVING GROUND: A LOOP, A MODEL YARD AND OBSTACLE SECTIONS";
        r.text(px0, py, mid, about, 0.85f, 0.85f, 0.88f, a);
    } else if (nav.page == P_VEHICLE) {
        const VehicleSpec& vs = vehicleCatalog()[nav.vehicle()];
        const float bw = pw, bh = 190 * u;
        pill(r, px0 - 6 * u, py - 6 * u, bw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        float y = py + 12 * u;
        label(r, px0 + 10 * u, y, px(3.0f * u), vs.name, kAmber[0], kAmber[1], kAmber[2], a);
        y += 8 * px(3.0f * u) + 12 * u;
        r.text(px0 + 10 * u, y, mid, vs.blurb, 0.85f, 0.85f, 0.88f, a);
        y += 8 * mid + 14 * u;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.1f M LONG   %.1f M WIDE", vs.length, vs.width);
        r.text(px0 + 10 * u, y, small, buf, kGrey[0], kGrey[1], kGrey[2], a);
        y += 8 * small + 18 * u;
        r.text(px0 + 10 * u, y, mid, std::string(nav.career ? "CAREER" : "SANDBOX") + " ON " + mapPickName(nav.map()), 0.95f, 0.85f, 0.6f, a);
        y += 8 * mid + 10 * u;
        r.text(px0 + 10 * u, y, small, nav.career ? "STOCK PARTS: EARN AND BUY THE REST" : "EVERY PART IS YOURS TO FIT", kGrey[0], kGrey[1], kGrey[2], a);
    } else {
        const DevEntry& e = devEntries()[std::min(nav.sel, (int)devEntries().size() - 1)];
        const float bw = pw, bh = 110 * u;
        pill(r, px0 - 6 * u, py - 6 * u, bw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        label(r, px0 + 10 * u, py + 12 * u, px(2.4f * u), e.label, kAmber[0], kAmber[1], kAmber[2], a);
        r.text(px0 + 10 * u, py + 12 * u + 8 * px(2.4f * u) + 14 * u, mid, e.hint, 0.85f, 0.85f, 0.88f, a);
    }
    (void)clock;
}

}  // namespace mainmenu
