#include "mainmenudraw.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "gamepad.h"
#include "hudkit.h"
#include "menuscene.h"
#include "vehiclepreview.h"
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

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string playedText(double secs) {
    char b[48];
    const int m = (int)(secs / 60);
    if (m < 60) std::snprintf(b, sizeof b, "%d MIN PLAYED", m);
    else std::snprintf(b, sizeof b, "%d H %d MIN PLAYED", m / 60, m % 60);
    return b;
}

const char* mapAbout(int m) {
    return m == MAP_MOUNTAIN       ? "START AT THE MOUNTAIN: SUMMIT TRAIL, CRAWL PARK, VALLEY LOOP"
           : m == MAP_FREEWAY      ? "THREE LANES EACH WAY THAT NEVER END: TOP SPEED"
           : m == MAP_FRONTIER     ? "START AT THE FRONTIER: DESERT, RIVERS, A RANGE, A RACE CIRCUIT"
           : m == MAP_BACKCOUNTRY  ? "8 KM: TRAILS, RIVERS AND PLACES, THE MOUNTAIN, FRONTIER AND OPEN COUNTRY ROUND IT"
           : m == MAP_WILDS        ? "START IN THE OPEN COUNTRY: ROOM TO TRY ANYTHING"
           : m == MAP_PROCEDURAL   ? "A BRAND-NEW ENDLESS WORLD EVERY TIME: DRIVE AS FAR AS YOU LIKE"
                                   : "THE PROVING GROUND: A LOOP, A MODEL YARD AND OBSTACLE SECTIONS";
}

// When a world was last played, from its save's time.
std::string agoText(long long t) {
    if (t <= 0) return "";
    const long long d = (long long)std::time(nullptr) - t;
    char b[64];
    if (d < 120) return "LAST PLAYED JUST NOW";
    if (d < 3600) std::snprintf(b, sizeof b, "LAST PLAYED %lld MIN AGO", d / 60);
    else if (d < 86400) std::snprintf(b, sizeof b, "LAST PLAYED %lld H AGO", d / 3600);
    else if (d < 2 * 86400) return "LAST PLAYED YESTERDAY";
    else if (d < 30 * 86400) std::snprintf(b, sizeof b, "LAST PLAYED %lld DAYS AGO", d / 86400);
    else {
        const std::time_t tt = (std::time_t)t;
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        static const char* kMon[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        std::snprintf(b, sizeof b, "LAST PLAYED %d %s %d", tm.tm_mday, kMon[tm.tm_mon % 12], tm.tm_year + 1900);
    }
    return b;
}

// The map a world is on, as the list says it: a built map's name, or the custom world and its seed.
std::string worldWhere(const WorldSave& w) {
    if (w.map == MAP_PROCEDURAL) return "CUSTOM WORLD" + (w.seed ? "  SEED " + std::to_string(w.seed) : std::string());
    return mapName(w.map);
}

// A vehicle's numbers for the picker: power, weight, drive.
std::string vehicleStats(int vi) {
    const VehicleSpec& vs = vehicleCatalog()[vi];
    const VehiclePreview* pv = vehiclePreview(vi);
    const double hp = peakPower(vs) / 745.7;
    const char* drive = vs.transfer != XFER_NONE ? "4X4" : vs.drive == VehicleSpec::AWD ? "AWD" : vs.drive == VehicleSpec::FWD ? "FWD" : "RWD";
    char b[96];
    if (pv && pv->massKg > 0) std::snprintf(b, sizeof b, "%d HP   %d KG   %s", (int)std::lround(hp), (int)std::lround(pv->massKg / 10) * 10, drive);
    else std::snprintf(b, sizeof b, "%d HP   %s", (int)std::lround(hp), drive);
    return b;
}

// What a New World row shows: its name, its pick and the second line.
void newWorldRow(const Nav& nav, int i, bool blink, std::string& lab, std::string& sub) {
    const bool custom = nav.customPick();
    switch (i) {
        case N_NAME: {
            lab = "NAME:  " + (nav.typing && nav.sel == N_NAME ? te::withCursor(nav.newName, nav.nameEdit, blink) : nav.newName);
            sub = nav.typing && nav.sel == N_NAME ? "" : "ENTER TO TYPE";
            break;
        }
        case N_MAP:
            lab = custom ? std::string("MAP:  CUSTOM WORLD") : std::string("MAP:  ") + mapName(nav.worldMap());
            sub = custom ? "ENDLESS LAND MADE FROM A SEED" : "BUILT MAP   ENTER TO SEE ALL";
            break;
        case N_SEED:
            if (!custom) {
                lab = "SEED:  -", sub = "BUILT MAPS HAVE NONE";
                break;
            }
            lab = "SEED:  " + (nav.typing && nav.sel == N_SEED ? te::withCursor(nav.seedText, nav.seedEdit, blink) : nav.seedText.empty() ? std::string("RANDOM") : nav.seedText);
            sub = nav.typing && nav.sel == N_SEED ? "" : "ENTER TO TYPE ONE   A D FOR A NEW ONE";
            break;
        case N_DIFF: lab = std::string("DIFFICULTY:  ") + difficultyName((Difficulty)nav.diffIdx); break;
        case N_MODE: lab = std::string("MODE:  ") + modeName((WorldMode)nav.modeIdx); break;
        case N_VEH:
            lab = std::string("VEHICLE:  ") + vehicleCatalog()[nav.startVehicle()].name;
            sub = "ENTER TO SEE THEM";
            break;
        default: lab = "CREATE WORLD"; break;
    }
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

    const float tsz = px(3.0f * u);
    const float titleY = std::max(24 * u, H * 0.07f);
    label(r, x0, titleY, tsz, nav.title(), kAmber[0], kAmber[1], kAmber[2], a);
    const float top = titleY + 8 * tsz + 22 * u;

    // A map's picture (the thumbnail), w wide at 16:9.
    auto mapPicture = [&](int m, float x, float y, float w, float h) {
        pill(r, x - 6 * u, y - 6 * u, w + 12 * u, h + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        const unsigned char* img = mapPreview(m);
        if (!img) {
            r.rect(x, y, w, h, 0.12f, 0.14f, 0.12f, a);
            return;
        }
        const float cw = w / kPreviewW, ch = h / kPreviewH;
        for (int j = 0; j < kPreviewH; j++)  // (runs of a colour along a row are one rect)
            for (int i = 0; i < kPreviewW;) {
                const unsigned char* p = img + (j * kPreviewW + i) * 3;
                int k = i + 1;
                while (k < kPreviewW && std::abs(img[(j * kPreviewW + k) * 3] - p[0]) + std::abs(img[(j * kPreviewW + k) * 3 + 1] - p[1]) + std::abs(img[(j * kPreviewW + k) * 3 + 2] - p[2]) < 6) k++;
                r.rect(x + i * cw, y + j * ch, (k - i) * cw + 0.6f, ch + 0.6f, p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, a);
                i = k;
            }
    };
    // A vehicle's picture: its real body, three-quarters from the front (vehiclepreview.h); a plain panel while it's being made.
    auto vehiclePicture = [&](int vi, float x, float y, float side, float shadeK) {
        const VehiclePreview* pv = vehiclePreview(vi);
        if (pv) r.icon(std::string("veh:") + vehicleCatalog()[vi].id, pv->mesh, x, y, side, side, a, shadeK);
        else r.text(x + side / 2 - r.textWidth("...", small) / 2, y + side / 2, small, "...", kGrey[0], kGrey[1], kGrey[2], a);
    };

    // NEW WORLD: PICK YOUR FIRST VEHICLE: the starters side by side as cards, each its picture, name and numbers.
    if (nav.page == P_START_VEHICLE) {
        r.rect(0, 0, (float)W, (float)H, 0.02f, 0.02f, 0.03f, 0.35f * a);
        const std::vector<int> sv = startVehicles();
        const int n = std::max(1, (int)sv.size());
        const float gap = 18 * u, avail = W - 2 * x0;
        const float cw = std::min((avail - (n - 1) * gap) / n, 300 * u);
        const float pic = cw - 20 * u, ch = pic + 96 * u;
        const float rowW = n * cw + (n - 1) * gap, cx0 = (W - rowW) / 2;
        const float cy = std::max(top + 10 * u, (H - ch) / 2 - 20 * u);
        for (int i = 0; i < (int)sv.size(); i++) {
            const int vi = sv[i];
            const VehicleSpec& vs = vehicleCatalog()[vi];
            const float x = cx0 + i * (cw + gap);
            const bool foc = nav.sel == i, picked = nav.startIdx == i;
            pill(r, x, cy, cw, ch, kPanel[0], kPanel[1], kPanel[2], (foc ? 0.88f : 0.6f) * a);
            if (foc) {  // (an amber frame round the one under the cursor)
                r.rect(x, cy, cw, 3 * u, kAmber[0], kAmber[1], kAmber[2], a);
                r.rect(x, cy + ch - 3 * u, cw, 3 * u, kAmber[0], kAmber[1], kAmber[2], a);
                r.rect(x, cy, 3 * u, ch, kAmber[0], kAmber[1], kAmber[2], a);
                r.rect(x + cw - 3 * u, cy, 3 * u, ch, kAmber[0], kAmber[1], kAmber[2], a);
            }
            vehiclePicture(vi, x + 10 * u, cy + 8 * u, pic, foc ? 1.0f : 0.72f);
            const float nt = px(2.0f * u);
            float ty = cy + 8 * u + pic + 6 * u;
            const std::string name = vs.name;
            hud::label(r, x + (cw - r.textWidth(name, nt)) / 2, ty, nt, name, foc ? kAmber[0] : 0.92f, foc ? kAmber[1] : 0.92f, foc ? kAmber[2] : 0.94f, a);
            ty += 8 * nt + 10 * u;
            const std::string st = vehicleStats(vi);
            r.text(x + (cw - r.textWidth(st, small)) / 2, ty, small, st, 0.85f, 0.85f, 0.88f, a);
            ty += 8 * small + 8 * u;
            char dims[64];
            std::snprintf(dims, sizeof dims, "%.1f M LONG   %.0f IN TIRES", vs.length, vs.wheelRadius * 2 / 0.0254);
            r.text(x + (cw - r.textWidth(dims, small)) / 2, ty, small, dims, kGrey[0], kGrey[1], kGrey[2], a);
            if (picked) {  // (the one New World has now)
                const std::string tag = "PICKED";
                r.text(x + cw - r.textWidth(tag, small) - 12 * u, cy + 12 * u, small, tag, 0.6f, 0.95f, 0.6f, a);
            }
            nav.hits.push_back({x, cy, cw, ch, i});
        }
        if (nav.sel >= 0 && nav.sel < (int)sv.size()) {  // the focused one's line about the real vehicle, under the row
            const std::string bl = vehicleCatalog()[sv[nav.sel]].blurb;
            r.text((W - r.textWidth(bl, mid)) / 2, cy + ch + 22 * u, mid, bl, 0.9f, 0.9f, 0.92f, a);
            const std::string tip = "CLICK ONE TO START YOUR CAREER IN IT";
            r.text((W - r.textWidth(tip, small)) / 2, cy + ch + 22 * u + 8 * mid + 12 * u, small, tip, kGrey[0], kGrey[1], kGrey[2], a);
        }
        return;
    }

    // The list pages and the DEV page: a list on the left, a panel on the right.
    const float lw = std::min(W * 0.36f, 470 * u), rh = 46 * u, gap = 6 * u;
    const int n = nav.count();
    // NEW WORLD: WHERE?: the built maps, then the custom world, each group under its heading.
    const int nBuilt = (int)careerMaps().size();
    const float headH = nav.page == P_WORLD_MAP ? 8 * px(1.6f * u) + 14 * u : nav.page == P_WORLDS ? 8 * px(1.6f * u) + 12 * u : 0;
    const int heads = nav.page == P_WORLD_MAP ? (n > nBuilt ? 2 : 1) : nav.page == P_WORLDS ? 1 : 0;
    const float avail = H - top - 70 * u - heads * headH;
    const float rowH = std::min(rh, (avail - (n - 1) * gap) / std::max(1, n));
    const float ts = px(2.0f * u);
    float y = top;
    auto heading = [&](const std::string& t, const std::string& note) {
        r.text(x0 + 2 * u, y, px(1.6f * u), t, kAmber[0], kAmber[1], kAmber[2], 0.9f * a);
        if (!note.empty()) r.text(x0 + 2 * u + r.textWidth(t, px(1.6f * u)) + 14 * u, y + 8 * px(1.6f * u) - 8 * small, small, note, kGrey[0], kGrey[1], kGrey[2], a);
        y += headH;
    };
    if (nav.page == P_WORLDS) heading("SAVED WORLDS", nav.worlds.empty() ? "NONE YET: MAKE ONE BELOW" : "");
    for (int i = 0; i < n; i++) {
        if (nav.page == P_WORLD_MAP && i == 0) heading("BUILT MAPS", "HAND-MADE, THE SAME FOR EVERYONE");
        if (nav.page == P_WORLD_MAP && i == nBuilt) y += 8 * u, heading("CUSTOM WORLD", "ENDLESS LAND MADE FROM A SEED");
        if (nav.page == P_WORLDS) {
            if (i < (int)nav.worlds.size()) {
                const WorldSave& w = nav.worlds[i];
                if (w.damaged) row(c, i, x0, y, lw, rowH, ts, upper(w.name), "DAMAGED SAVE", nav.sel == i, true);
                else {
                    std::string sub = w.map == MAP_PROCEDURAL ? "CUSTOM WORLD" : mapName(w.map);  // (the seed: on the panel)
                    if (w.lastVehicle >= 0 && w.lastVehicle < (int)vehicleCatalog().size()) sub += "   " + upper(vehicleCatalog()[w.lastVehicle].id);
                    const std::string ago = agoText(w.savedAt);
                    if (!ago.empty()) sub += "   " + ago.substr(std::string("LAST PLAYED ").size());
                    row(c, i, x0, y, lw, rowH, ts, upper(w.name), sub, nav.sel == i);
                }
            } else {
                if (!nav.worlds.empty()) y += 8 * u;
                row(c, i, x0, y, lw, rowH, ts, "+ NEW WORLD", "", nav.sel == i);
            }
        } else if (nav.page == P_NEW_WORLD) {
            std::string lab, sub;
            newWorldRow(nav, i, std::fmod(clock, 1.0) < 0.55, lab, sub);
            row(c, i, x0, y, lw, rowH, ts, lab, sub, nav.sel == i, i == N_SEED && !nav.customPick());
        } else if (nav.page == P_DELETE_WORLD) {
            row(c, i, x0, y, lw, rowH, ts, i == 0 ? "YES, DELETE IT" : "NO, KEEP IT", "", nav.sel == i);
        } else if (nav.page == P_WORLD_MAP) {
            const int m = newWorldMaps()[i];
            row(c, i, x0, y, lw, rowH, ts, customWorld(m) ? "ENDLESS WORLD" : mapPickName(m), i == nav.worldMapIdx ? "PICKED" : "", nav.sel == i);
        } else if (nav.page == P_MAP) {
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
        y += rowH + gap;
    }
    // The right-hand panel.
    const float px0 = x0 + lw + 36 * u, pw = W - px0 - x0;
    if (pw < 200 * u) return;
    float py = top;
    const float big = px(2.4f * u);
    auto lines = [&](float x, float& yy, std::initializer_list<std::pair<std::string, int>> ls) {  // (int: 0 bright mid, 1 grey small, 2 warm mid)
        for (const auto& l : ls) {
            if (l.first.empty()) continue;
            const float sc = l.second == 1 ? small : mid;
            const float* col = l.second == 1 ? kGrey : nullptr;
            if (l.second == 2) r.text(x, yy, sc, l.first, 0.95f, 0.85f, 0.6f, a);
            else r.text(x, yy, sc, l.first, col ? col[0] : 0.85f, col ? col[1] : 0.85f, col ? col[2] : 0.88f, a);
            yy += 8 * sc + 10 * u;
        }
    };
    // A button on the panel (a hit row of its own).
    auto button = [&](float x, float yy, const std::string& t, int hitRow, bool warm) {
        const float st = px(1.8f * u), bw = r.textWidth(t, st) + 28 * u, bh = 30 * u;
        pill(r, x, yy, bw, bh, warm ? 0.35f : kPanel[0], warm ? 0.08f : kPanel[1], warm ? 0.06f : kPanel[2], 0.9f * a);
        r.rect(x, yy + bh - 3 * u, bw, 3 * u, warm ? 0.95f : kAmber[0], warm ? 0.45f : kAmber[1], warm ? 0.35f : kAmber[2], a);
        r.text(x + 14 * u, yy + (bh - 8 * st) / 2, st, t, 0.95f, 0.93f, 0.9f, a);
        nav.hits.push_back({x, yy, bw, bh, hitRow});
        return bw;
    };
    if (nav.page == P_WORLDS || nav.page == P_DELETE_WORLD) {
        const int wi = nav.page == P_DELETE_WORLD ? nav.delIdx : nav.sel;
        const bool isWorld = wi >= 0 && wi < (int)nav.worlds.size();
        const float bh = isWorld && nav.page == P_WORLDS && !nav.worlds[wi].damaged ? 236 * u : 130 * u;
        pill(r, px0 - 6 * u, py - 6 * u, pw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        float yy = py + 12 * u;
        const float tx = px0 + 10 * u;
        if (isWorld) {
            const WorldSave& w = nav.worlds[wi];
            label(r, tx, yy, big, upper(w.name), kAmber[0], kAmber[1], kAmber[2], a);
            yy += 8 * big + 14 * u;
            if (nav.page == P_DELETE_WORLD) {
                r.text(tx, yy, mid, "THE WORLD AND EVERY CAR LEFT IN IT GO FOR GOOD", 0.95f, 0.6f, 0.5f, a);
            } else if (w.damaged) {
                r.text(tx, yy, mid, "THIS SAVE CAN'T BE READ. IT CAN ONLY BE DELETED (DEL).", 0.85f, 0.85f, 0.88f, a);
                button(tx, py + bh - 36 * u, "DELETE", kHitDelete, true);
            } else {
                const std::string truck = w.lastVehicle >= 0 && w.lastVehicle < (int)vehicleCatalog().size() ? std::string("IN THE ") + vehicleCatalog()[w.lastVehicle].name : "";
                lines(tx, yy, {{worldWhere(w), 0},
                               {std::string(difficultyName(w.diff)) + "   " + modeName(w.mode), 1},
                               {playedText(w.playedSeconds) + (agoText(w.savedAt).empty() ? "" : "   " + agoText(w.savedAt)), 1},
                               {truck, 2},
                               {std::to_string(w.left.size()) + (w.left.size() == 1 ? " CAR" : " CARS") + " LEFT IN THE WORLD", 1}});
                const float by = py + bh - 40 * u;
                const float cw2 = button(tx, by, "CONTINUE", wi, false);
                button(tx + cw2 + 12 * u, by, "DELETE", kHitDelete, true);
            }
        } else {
            label(r, tx, yy, big, "NEW WORLD", kAmber[0], kAmber[1], kAmber[2], a);
            yy += 8 * big + 14 * u;
            lines(tx, yy, {{"START A CAREER: PICK A MAP, A DIFFICULTY AND A STARTER", 0},
                           {"A BUILT MAP, OR A CUSTOM WORLD MADE FROM A SEED", 1},
                           {"IT'S SAVED AS SOON AS IT'S MADE, AND WHEN YOU LEAVE IT", 1}});
        }
    } else if (nav.page == P_WORLD_MAP) {
        const int m = newWorldMaps()[std::min(std::max(nav.sel, 0), n - 1)];
        if (customWorld(m)) {
            const float bh = 190 * u;
            pill(r, px0 - 6 * u, py - 6 * u, pw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
            float yy = py + 12 * u;
            label(r, px0 + 10 * u, yy, big, "CUSTOM WORLD", kAmber[0], kAmber[1], kAmber[2], a);
            yy += 8 * big + 14 * u;
            lines(px0 + 10 * u, yy, {{"ENDLESS LAND MADE FROM A SEED", 0},
                                      {"MOUNTAINS, RIVERS, LAKES, TRAILS AND OUTPOSTS, AS FAR AS YOU DRIVE", 1},
                                      {"EVERY NEW WORLD GETS ITS OWN RANDOM SEED: A NEW LAND EACH TIME", 1},
                                      {"CONTINUING IT BRINGS BACK THE SAME LAND, WHERE YOU LEFT IT", 1},
                                      {"TYPE A FRIEND'S SEED TO PLAY ON THEIR LAND", 2}});
        } else {
            const float ph = std::min(pw * 9.0f / 16.0f, H - top - 170 * u), pwid = ph * 16.0f / 9.0f;
            mapPicture(m, px0, py, pwid, ph);
            py += ph + 20 * u;
            pill(r, px0 - 6 * u, py - 8 * u, pwid + 12 * u, 8 * big + 2 * (8 * mid + 10 * u) + 18 * u, kPanel[0], kPanel[1], kPanel[2], 0.8f * a);  // (the words over the camp)
            label(r, px0, py, big, mapPickName(m), kAmber[0], kAmber[1], kAmber[2], a);
            py += 8 * big + 8 * u;
            lines(px0, py, {{mapAbout(m), 0}, {"A BUILT MAP: HAND-MADE, THE SAME LAND FOR EVERYONE", 1}});
        }
    } else if (nav.page == P_NEW_WORLD) {
        const bool vehRow = nav.sel == N_VEH || nav.sel == N_CREATE;
        const float bh = vehRow ? std::min(pw * 0.62f, H - top - 110 * u) : 150 * u;
        pill(r, px0 - 6 * u, py - 6 * u, pw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        float yy = py + 12 * u;
        std::string head, l1, l2, l3;
        char b[96];
        switch (nav.sel) {
            case N_NAME: head = "NAME", l1 = "WHAT THIS WORLD IS CALLED IN THE LIST"; break;
            case N_MAP:
                if (nav.customPick()) head = "CUSTOM WORLD", l1 = "ENDLESS LAND MADE FROM A SEED", l2 = "ENTER: THE BUILT MAPS AND THE CUSTOM WORLD";
                else head = mapName(nav.worldMap()), l1 = mapAbout(nav.worldMap()), l2 = "A BUILT MAP.  ENTER: ALL THE MAPS, AND THE CUSTOM WORLD";
                break;
            case N_SEED:
                head = "SEED " + (nav.seedText.empty() ? std::string("RANDOM") : nav.seedText);
                l1 = "THE SAME SEED IS THE SAME LAND: THIS WORLD KEEPS IT";
                l2 = "A NEW RANDOM ONE EACH NEW WORLD. TYPE ONE TO SHARE A LAND";
                l3 = "DIGITS, OR WORDS: RED CANYON IS A SEED TOO";
                break;
            case N_DIFF:
                head = difficultyName((Difficulty)nav.diffIdx);
                std::snprintf(b, sizeof b, "START CASH $%d", startCash((Difficulty)nav.diffIdx));
                l1 = b, l2 = "LESS CASH, HARDER PARTS AND REPAIRS";
                break;
            case N_MODE:
                head = modeName((WorldMode)nav.modeIdx);
                l1 = nav.modeIdx == MODE_CREW ? "ONE GARAGE AND ONE POT OF CASH FOR EVERYONE" : "EACH PLAYER KEEPS THEIR OWN GARAGE AND CASH";
                break;
            default: break;
        }
        if (vehRow) {  // the starter itself, as the picker shows it
            const int vi = nav.startVehicle();
            const VehicleSpec& vs = vehicleCatalog()[vi];
            label(r, px0 + 10 * u, yy, big, nav.sel == N_CREATE ? "CREATE " + upper(nav.newName.empty() ? "A NEW WORLD" : nav.newName) : std::string(vs.name), kAmber[0], kAmber[1], kAmber[2], a);
            yy += 8 * big + 10 * u;
            const float side = std::min(bh - (yy - py) - 70 * u, pw - 20 * u);
            vehiclePicture(vi, px0 + (pw - side) / 2, yy, side, 1.0f);
            if (nav.sel == N_VEH) nav.hits.push_back({px0 + (pw - side) / 2, yy, side, side, N_VEH});
            yy += side + 8 * u;
            lines(px0 + 10 * u, yy, {{nav.sel == N_CREATE ? std::string("IN THE ") + vs.name + ", ON " + (nav.customPick() ? std::string("A CUSTOM WORLD") : std::string(mapName(nav.worldMap()))) : vehicleStats(vi), 0},
                                      {nav.sel == N_CREATE ? std::string(difficultyName((Difficulty)nav.diffIdx)) + "   " + modeName((WorldMode)nav.modeIdx) : std::string(vs.blurb), 1}});
        } else {
            label(r, px0 + 10 * u, yy, big, head, kAmber[0], kAmber[1], kAmber[2], a);
            yy += 8 * big + 14 * u;
            lines(px0 + 10 * u, yy, {{l1, 0}, {l2, 1}, {l3, 1}});
        }
    } else if (nav.page == P_MAP) {
        const int m = nav.map();
        const float ph = std::min(pw * 9.0f / 16.0f, H - top - 150 * u), pwid = ph * 16.0f / 9.0f;
        mapPicture(m, px0, py, pwid, ph);
        py += ph + 20 * u;
        pill(r, px0 - 6 * u, py - 8 * u, pwid + 12 * u, 8 * big + 8 * mid + 26 * u, kPanel[0], kPanel[1], kPanel[2], 0.8f * a);
        label(r, px0, py, big, mapPickName(m), kAmber[0], kAmber[1], kAmber[2], a);
        py += 8 * big + 8 * u;
        r.text(px0, py, mid, mapAbout(m), 0.85f, 0.85f, 0.88f, a);
    } else if (nav.page == P_VEHICLE) {
        const VehicleSpec& vs = vehicleCatalog()[nav.vehicle()];
        const float bw = pw, bh = 190 * u;
        pill(r, px0 - 6 * u, py - 6 * u, bw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        float yy = py + 12 * u;
        label(r, px0 + 10 * u, yy, px(3.0f * u), vs.name, kAmber[0], kAmber[1], kAmber[2], a);
        yy += 8 * px(3.0f * u) + 12 * u;
        r.text(px0 + 10 * u, yy, mid, vs.blurb, 0.85f, 0.85f, 0.88f, a);
        yy += 8 * mid + 14 * u;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.1f M LONG   %.1f M WIDE", vs.length, vs.width);
        r.text(px0 + 10 * u, yy, small, buf, kGrey[0], kGrey[1], kGrey[2], a);
        yy += 8 * small + 18 * u;
        r.text(px0 + 10 * u, yy, mid, std::string(nav.career ? "CAREER" : "SANDBOX") + " ON " + mapPickName(nav.map()), 0.95f, 0.85f, 0.6f, a);
        yy += 8 * mid + 10 * u;
        r.text(px0 + 10 * u, yy, small, nav.career ? "STOCK PARTS: EARN AND BUY THE REST" : "EVERY PART IS YOURS TO FIT", kGrey[0], kGrey[1], kGrey[2], a);
    } else {
        const DevEntry& e = devEntries()[std::min(nav.sel, (int)devEntries().size() - 1)];
        const float bw = pw, bh = 110 * u;
        pill(r, px0 - 6 * u, py - 6 * u, bw + 12 * u, bh + 12 * u, kPanel[0], kPanel[1], kPanel[2], 0.85f * a);
        label(r, px0 + 10 * u, py + 12 * u, big, e.label, kAmber[0], kAmber[1], kAmber[2], a);
        r.text(px0 + 10 * u, py + 12 * u + 8 * big + 14 * u, mid, e.hint, 0.85f, 0.85f, 0.88f, a);
    }
}

}  // namespace mainmenu
