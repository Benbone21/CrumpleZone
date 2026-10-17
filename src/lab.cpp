#include "lab.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "daylight.h"
#include "gl.h"
#include "modelkit.h"
#include "render.h"
#include "scenery.h"
#include "sdl_min.h"
#include "softbody.h"
#include "textedit.h"

namespace {

using mk::Choice;
using mk::ModelDef;

// A flat studio out on the proving ground: a concrete pad in the grass, nothing else.
World studioWorld() {
    const bool showcase = World::withShowcase;  // (no need to lay out the yard and park just to clear them)
    World::withShowcase = false;
    World w(MAP_PROVING_GROUND);
    World::withShowcase = showcase;
    w.boxes.clear(), w.rocks.clear(), w.trees.clear(), w.bushes.clear(), w.logs.clear(), w.poles.clear();
    w.fixtures.clear(), w.stations.clear(), w.models.clear(), w.waters.clear(), w.tags.clear();
    for (double x = -110; x <= 110; x += 1)
        for (double z = -110; z <= 110; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id < 0) continue;
            const double r = std::hypot(x, z);
            w.setCellHeight(id, 0);
            if (r < 9) w.setCellSurface(id, SURF_PAVED, PAINT_PAD);
            else w.setCellSurface(id, SURF_GRASS, PAINT_NONE);
        }
    w.buildGrid();
    return w;
}

void writePpm(const std::string& path, const std::vector<unsigned char>& px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) std::fwrite(&px[y * w * 3], 1, w * 3, f);
    std::fclose(f);
}

struct Hit {
    float x, y, w, h;
    std::function<void(float mx)> click;  // (mx: where across it, 0..1)
    bool slider = false;
};

struct Lab {
    SDL_Window* win;
    Renderer& r;
    LabOptions opt;
    Sim sim;
    std::string picksPath;
    std::vector<int> order;  // catalog indices, by category
    int sel = 0, focus = 0;
    bool compare = true, orbit = true, night = false, figure = true;
    double yaw = -0.7, pitch = 0.3, zoom = 1, spin = 0;
    V3 center;
    double radius = 1;
    std::vector<V3> slot;  // each design's origin (compare)
    int listScroll = 0;
    std::vector<Hit> hits;
    int dragSlider = -1;
    bool orbiting = false, moved = false;
    float pressX = 0, pressY = 0, mouseX = 0, mouseY = 0;
    bool typing = false;
    std::string draft;
    te::Edit draftEd;
    std::string toast;
    double toastT = 0;
    int W = 1280, H = 720;
    float u = 1;
    float LW = 300, RW = 370;

    Lab(SDL_Window* w, Renderer& rr, const LabOptions& o) : win(w), r(rr), opt(o) {}

    const ModelDef& model() const { return mk::catalog()[order[sel]]; }
    Choice& choice() { return mk::choices()[model().id]; }
    bool hasChoice() const { return mk::choices().count(model().id) > 0; }

    void say(const std::string& s) { toast = s, toastT = 2.5; }

    void save() {
        if (picksPath.empty()) return say("NO ASSETS FOLDER: PICKS NOT SAVED");
        if (!mk::saveChoices(picksPath)) say("COULD NOT SAVE THE PICKS");
    }

    // The scene: every design side by side (compare), or the one in focus.
    void rebuild() {
        mk::forgetMeshes();
        const ModelDef& d = model();
        sim.world.models.clear();
        slot.clear();
        const int n = (int)d.variants.size();
        focus = std::clamp(focus, 0, std::max(0, n - 1));
        std::vector<mk::Mesh> meshes(n);
        for (int v = 0; v < n; v++) mk::build(d, v, mk::pickedParams(d, v), meshes[v]);
        V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
        auto grow = [&](const V3& a, const V3& b) {
            lo = V3(std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z));
            hi = V3(std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z));
        };
        double z = 0;
        double gap = 0;
        for (int v = 0; v < n; v++) gap = std::max(gap, std::max(meshes[v].hi.x - meshes[v].lo.x, meshes[v].hi.z - meshes[v].lo.z));
        gap = std::max(0.3, gap * 0.35);
        for (int v = 0; v < n; v++) {
            Placed pl;
            pl.id = d.id, pl.variant = v;
            if (compare) {  // left to right as seen from the front: +Z first
                const mk::Mesh& m = meshes[v];
                const double at = z - m.hi.z;
                pl.p = V3(0, 0, at);
                z = at + m.lo.z - gap;
            } else if (v != focus) continue;
            pl.yaw = spin;
            slot.push_back(pl.p);
            sim.world.models.push_back(pl);
            grow(pl.p + meshes[v].lo, pl.p + meshes[v].hi);
        }
        if (compare) {  // centre the row
            const double mid = (lo.z + hi.z) / 2;
            for (Placed& pl : sim.world.models) pl.p.z -= mid;
            for (V3& s : slot) s.z -= mid;
            lo.z -= mid, hi.z -= mid;
        }
        if (slot.empty()) slot.push_back(V3());
        // A person for scale beside big things.
        const double big = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
        if (figure && big > 1.2 && mk::findModel("person") && d.id != "person") {
            Placed pl;
            pl.id = "person";
            pl.p = V3(0, 0, lo.z - 1.2);
            pl.yaw = 0.4;
            sim.world.models.push_back(pl);
            grow(pl.p + V3(-0.3, 0, -0.3), pl.p + V3(0.3, 1.8, 0.3));
        }
        if (sim.world.models.empty()) lo = hi = V3(0, 0.5, 0);
        center = (lo + hi) * 0.5;
        radius = std::max(0.25, len(hi - lo) * 0.5);
        r.updateObjects(sim.world);
    }

    void select(int s) {
        if (order.empty()) return;
        sel = (s % (int)order.size() + (int)order.size()) % (int)order.size();
        focus = mk::pickedVariant(model());
        zoom = 1, spin = 0;
        rebuild();
    }

    // Changes after which the picks are saved and the scene rebuilt.
    void setParam(const mk::Param& q, double v) {
        if (!q.names.empty()) v = std::round(v);
        else if (q.step > 0) v = std::round((v - q.lo) / q.step) * q.step + q.lo;
        v = std::clamp(v, q.lo, q.hi);
        auto& tw = choice().tweaks[focus];
        if (std::fabs(v - q.def) < 1e-9) tw.erase(q.key);
        else tw[q.key] = v;
        rebuild();
        save();
    }
    double paramValue(const mk::Param& q) { return mk::pickedParams(model(), focus)(q.key); }

    // The game uses every design not left out here; this leaves the one in focus out, or puts it back (never the last one).
    void toggleThis() {
        Choice& c = choice();
        const char tag = (char)('A' + focus);
        if (c.off.count(focus)) c.off.erase(focus), say(std::string(1, tag) + " IS BACK IN THE GAME");
        else if (c.off.size() + 1 >= model().variants.size()) return say("THE GAME NEEDS AT LEAST ONE DESIGN");
        else c.off.insert(focus), say(std::string(1, tag) + " IS LEFT OUT OF THE GAME");
        if (c.status == mk::NEW) c.status = mk::LIKED;
        rebuild();
        save();
    }
    void keepAll() {
        Choice& c = choice();
        c.off.clear();
        if (c.status != mk::REDO) c.status = mk::LIKED;
        rebuild();
        save();
        say("ALL " + std::to_string(model().variants.size()) + " DESIGNS ARE IN THE GAME");
    }
    void redo() {
        Choice& c = choice();
        c.status = mk::REDO;
        save();
        startNote();
    }
    void startNote() {
        typing = true;
        draft = hasChoice() ? choice().note : "";
        draftEd.reset();
        SDL_StartTextInput();
    }
    void endNote(bool keep) {
        typing = false;
        SDL_StopTextInput();
        if (keep) {
            choice().note = draft;
            save();
            say(draft.empty() ? "NOTE CLEARED" : "NOTE SAVED");
        }
    }
    void resetTweaks() {
        if (hasChoice()) choice().tweaks.erase(focus);
        rebuild();
        save();
        say("BACK TO THE DEFAULTS");
    }
    void reroll() {
        for (const mk::Param& q : model().variants[focus].params)
            if (q.key == "seed") {
                const double v = q.lo + std::floor(mk::hash01((uint32_t)SDL_GetPerformanceCounter(), 7) * (q.hi - q.lo + 1));
                setParam(q, v);
                return;
            }
    }
    void nextNew(int dir) {
        for (int k = 1; k <= (int)order.size(); k++) {
            const int s = ((sel + dir * k) % (int)order.size() + (int)order.size()) % (int)order.size();
            auto it = mk::choices().find(mk::catalog()[order[s]].id);
            if (it == mk::choices().end() || it->second.status != mk::LIKED) return select(s);
        }
        say("EVERY MODEL IS SEEN");
    }

    // ---- Camera ----
    void camera(View& v) {
        const double fy = 0.9;
        v.fovy = (float)fy;
        v.width = W, v.height = H;
        const double centreW = std::max(200.0, W - (double)LW - RW);
        const double fx = 2 * std::atan(std::tan(fy / 2) * centreW / H);
        const double fov = std::min(fy, fx);
        const double dist = radius / std::sin(fov / 2) * 1.05 * zoom;
        const V3 dir(std::cos(yaw) * std::cos(pitch), std::sin(pitch), -std::sin(yaw) * std::cos(pitch));
        V3 target = center;
        V3 eye = target + dir * dist;
        if (eye.y < 0.3) eye.y = 0.3;
        // Slide the view so the model sits in the middle of the space between the panels.
        const V3 fwd = norm(target - eye), right = norm(cross(fwd, V3(0, 1, 0)));
        const double f = H / 2.0 / std::tan(fy / 2);
        const double dx = (LW + centreW / 2) - W / 2.0;
        const V3 shift = right * (-dx * len(target - eye) / f);
        v.eye = eye + shift, v.target = target + shift, v.up = V3(0, 1, 0);
    }
    bool project(const View& v, const V3& p, float& sx, float& sy) const {
        const V3 fwd = norm(v.target - v.eye), right = norm(cross(fwd, V3(0, 1, 0))), up = cross(right, fwd);
        const V3 d = p - v.eye;
        const double z = dot(d, fwd);
        if (z < 0.05) return false;
        const double f = H / 2.0 / std::tan(v.fovy / 2);
        sx = (float)(W / 2.0 + dot(d, right) / z * f), sy = (float)(H / 2.0 - dot(d, up) / z * f);
        return true;
    }

    // ---- Drawing helpers ----
    void text(float x, float y, float s, const std::string& t, float cr, float cg, float cb, float a = 1) { r.text(x, y, s * u, t, cr, cg, cb, a); }
    float tw(const std::string& t, float s) const { return r.textWidth(t, s * u); }
    // Word-wrapped text: returns the height used.
    float wrap(float x, float y, float width, float s, const std::string& t, float cr, float cg, float cb) {
        const int perLine = std::max(8, (int)(width / (6 * s * u)));
        std::string line, word;
        float yy = y;
        auto flush = [&]() {
            text(x, yy, s, line, cr, cg, cb);
            yy += 10 * s * u;
            line.clear();
        };
        for (size_t i = 0; i <= t.size(); i++) {
            const char ch = i < t.size() ? t[i] : ' ';
            if (ch == ' ') {
                if (!word.empty()) {
                    if ((int)(line.size() + word.size() + (line.empty() ? 0 : 1)) > perLine && !line.empty()) flush();
                    line += (line.empty() ? "" : " ") + word;
                    word.clear();
                }
            } else word += ch;
        }
        if (!line.empty()) flush();
        return yy - y;
    }
    void button(float x, float y, float w, float h, const std::string& label, float cr, float cg, float cb, std::function<void()> fn, bool on = false) {
        const bool hover = mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
        r.rect(x, y, w, h, cr * (on ? 1.0f : 0.55f), cg * (on ? 1.0f : 0.55f), cb * (on ? 1.0f : 0.55f), hover ? 0.95f : 0.8f);
        text(x + (w - tw(label, 1.6f)) / 2, y + (h - 11 * u) / 2, 1.6f, label, 1, 1, 1);
        hits.push_back({x, y, w, h, [fn](float) { fn(); }});
    }

    // ---- The panels ----
    void drawList() {
        r.rect(0, 0, LW, (float)H, 0.04f, 0.05f, 0.06f, 0.86f);
        text(16 * u, 14 * u, 3, "MODEL LAB", 0.98f, 0.82f, 0.3f);
        int liked = 0, redoN = 0, fresh = 0;
        for (int i : order) {
            auto it = mk::choices().find(mk::catalog()[i].id);
            const int st = it == mk::choices().end() ? mk::NEW : it->second.status;
            liked += st == mk::LIKED, redoN += st == mk::REDO, fresh += st == mk::NEW;
        }
        char sum[96];
        std::snprintf(sum, sizeof sum, "%d SEEN  %d REDO  %d TO SEE", liked, redoN, fresh);
        text(16 * u, 44 * u, 1.5f, sum, 0.75f, 0.78f, 0.82f);
        // Rows: category headers and models.
        struct Row { bool header; int idx; std::string cat; };
        std::vector<Row> rows;
        std::string cat;
        for (int k = 0; k < (int)order.size(); k++) {
            const ModelDef& d = mk::catalog()[order[k]];
            if (d.category != cat) rows.push_back({true, -1, d.category}), cat = d.category;
            rows.push_back({false, k, ""});
        }
        const float rowH = 20 * u, top = 66 * u;
        const int visible = std::max(1, (int)((H - top - 10 * u) / rowH));
        int selRow = 0;
        for (int i = 0; i < (int)rows.size(); i++)
            if (!rows[i].header && rows[i].idx == sel) selRow = i;
        if (selRow < listScroll) listScroll = selRow - 1;
        if (selRow >= listScroll + visible) listScroll = selRow - visible + 2;
        listScroll = std::clamp(listScroll, 0, std::max(0, (int)rows.size() - visible));
        for (int i = listScroll; i < (int)rows.size() && i < listScroll + visible; i++) {
            const float y = top + (i - listScroll) * rowH;
            if (rows[i].header) {
                text(16 * u, y + 5 * u, 1.5f, rows[i].cat, 0.98f, 0.82f, 0.3f, 0.9f);
                continue;
            }
            const int k = rows[i].idx;
            const ModelDef& d = mk::catalog()[order[k]];
            auto it = mk::choices().find(d.id);
            const int st = it == mk::choices().end() ? mk::NEW : it->second.status;
            if (k == sel) r.rect(8 * u, y, LW - 16 * u, rowH - 2 * u, 0.25f, 0.42f, 0.6f, 0.85f);
            const float cr = st == mk::LIKED ? 0.3f : st == mk::REDO ? 0.95f : 0.45f, cg = st == mk::LIKED ? 0.85f : st == mk::REDO ? 0.3f : 0.47f,
                        cb = st == mk::LIKED ? 0.4f : st == mk::REDO ? 0.25f : 0.5f;
            r.rect(22 * u, y + 6 * u, 8 * u, 8 * u, cr, cg, cb, 1);
            text(38 * u, y + 5 * u, 1.5f, d.title, 1, 1, 1, k == sel ? 1.0f : 0.85f);
            if (st == mk::LIKED && !it->second.off.empty()) text(LW - 44 * u, y + 5 * u, 1.5f, std::to_string(d.variants.size() - it->second.off.size()) + "/" + std::to_string(d.variants.size()), 0.4f, 0.9f, 0.5f);
            hits.push_back({8 * u, y, LW - 16 * u, rowH, [this, k](float) { select(k); }});
        }
    }

    void drawDetail() {
        const ModelDef& d = model();
        const float x0 = W - RW, pad = 16 * u, w = RW - 2 * pad;
        r.rect(x0, 0, RW, (float)H, 0.04f, 0.05f, 0.06f, 0.86f);
        float y = 14 * u;
        text(x0 + pad, y, 2.6f, d.title, 1, 1, 1);
        y += 30 * u;
        text(x0 + pad, y, 1.4f, d.id + "  " + d.category, 0.55f, 0.6f, 0.65f);
        y += 16 * u;
        y += wrap(x0 + pad, y, w, 1.5f, d.use, 0.8f, 0.82f, 0.85f) + 8 * u;
        // Designs.
        const int n = (int)d.variants.size();
        const int status = hasChoice() ? choice().status : mk::NEW;
        const float bw = std::min(64 * u, (w - (n - 1) * 6 * u) / std::max(1, n));
        for (int v = 0; v < n; v++) {
            const float bx = x0 + pad + v * (bw + 6 * u);
            const bool on = v == focus;
            button(bx, y, bw, 26 * u, std::string(1, (char)('A' + v)), 0.3f, 0.5f, 0.75f, [this, v] {
                focus = v;
                if (!compare) rebuild();
            }, on);
            r.rect(bx, y + 27 * u, bw, 3 * u, mk::variantIncluded(d, v) ? 0.3f : 0.9f, mk::variantIncluded(d, v) ? 0.9f : 0.3f, mk::variantIncluded(d, v) ? 0.4f : 0.25f, 1);
        }
        y += 38 * u;
        const mk::Variant& var = d.variants[focus];
        text(x0 + pad, y, 2, var.name, 0.98f, 0.82f, 0.3f);
        y += 20 * u;
        y += wrap(x0 + pad, y, w, 1.5f, var.blurb, 0.8f, 0.82f, 0.85f) + 6 * u;
        const bool in = mk::variantIncluded(d, focus);
        if (status == mk::REDO) text(x0 + pad, y, 1.5f, "FLAGGED FOR A REDO", 0.95f, 0.4f, 0.3f);
        else if (in) text(x0 + pad, y, 1.5f, "IN THE GAME (ALL SHARE THE WORLD)", 0.4f, 0.9f, 0.5f);
        else text(x0 + pad, y, 1.5f, "LEFT OUT OF THE GAME", 0.95f, 0.45f, 0.3f);
        y += 22 * u;
        // Settings.
        const auto& tweaks = hasChoice() ? choice().tweaks : std::map<int, std::map<std::string, double>>();
        for (int qi = 0; qi < (int)var.params.size(); qi++) {
            const mk::Param& q = var.params[qi];
            const double val = paramValue(q);
            const bool moved = tweaks.count(focus) && tweaks.at(focus).count(q.key);
            text(x0 + pad, y, 1.5f, q.label, moved ? 0.98f : 0.85f, moved ? 0.82f : 0.87f, moved ? 0.3f : 0.9f);
            std::string vs;
            if (!q.names.empty()) vs = q.names[std::clamp((int)std::lround(val), 0, (int)q.names.size() - 1)];
            else {
                char b[32];
                std::snprintf(b, sizeof b, q.step >= 1 ? "%.0f" : q.step >= 0.1 ? "%.1f" : "%.2f", val);
                vs = b;
            }
            if (!q.names.empty()) {  // < CHOICE >
                const float ay = y + 14 * u, aw = 26 * u;
                button(x0 + pad, ay, aw, 20 * u, "<", 0.35f, 0.38f, 0.42f, [this, q, val] { setParam(q, val - 1 < q.lo ? q.hi : val - 1); });
                button(x0 + pad + w - aw, ay, aw, 20 * u, ">", 0.35f, 0.38f, 0.42f, [this, q, val] { setParam(q, val + 1 > q.hi ? q.lo : val + 1); });
                text(x0 + pad + (w - tw(vs, 1.6f)) / 2, ay + 5 * u, 1.6f, vs, 1, 1, 1);
            } else {
                text(x0 + pad + w - tw(vs, 1.5f), y, 1.5f, vs, 1, 1, 1);
                const float sy = y + 18 * u, sh = 8 * u;
                const double t = q.hi > q.lo ? (val - q.lo) / (q.hi - q.lo) : 0;
                r.rect(x0 + pad, sy, w, sh, 0.2f, 0.22f, 0.25f, 1);
                r.rect(x0 + pad, sy, (float)(w * t), sh, 0.3f, 0.5f, 0.75f, 1);
                r.rect(x0 + pad + (float)(w * t) - 3 * u, sy - 4 * u, 6 * u, sh + 8 * u, 1, 1, 1, 1);
                Hit h{x0 + pad, sy - 6 * u, w, sh + 12 * u, [this, q](float mx) { setParam(q, q.lo + (q.hi - q.lo) * std::clamp((double)mx, 0.0, 1.0)); }, true};
                hits.push_back(h);
            }
            y += 42 * u;
        }
        if (var.params.empty()) {
            text(x0 + pad, y, 1.5f, "NOTHING TO TWEAK", 0.55f, 0.6f, 0.65f);
            y += 20 * u;
        }
        // Actions.
        y = std::max(y + 6 * u, (float)H - 210 * u);
        const float half = (w - 8 * u) / 2;
        const bool inNow = mk::variantIncluded(d, focus);
        button(x0 + pad, y, half, 30 * u, inNow ? "OUT  (ENTER)" : "BACK IN  (ENTER)", inNow ? 0.8f : 0.25f, inNow ? 0.5f : 0.7f, inNow ? 0.25f : 0.35f, [this] { toggleThis(); }, true);
        button(x0 + pad + half + 8 * u, y, half, 30 * u, "KEEP ALL  (K)", 0.25f, 0.7f, 0.35f, [this] { keepAll(); }, true);
        y += 36 * u;
        button(x0 + pad, y, half, 28 * u, "REDO IT  (R)", 0.85f, 0.3f, 0.25f, [this] { redo(); }, true);
        button(x0 + pad + half + 8 * u, y, half, 28 * u, "NOTE  (N)", 0.4f, 0.45f, 0.55f, [this] { startNote(); }, true);
        y += 34 * u;
        button(x0 + pad, y, half, 28 * u, "RESET  (BKSP)", 0.4f, 0.45f, 0.55f, [this] { resetTweaks(); }, true);
        bool seeded = false;
        for (const mk::Param& q : var.params) seeded |= q.key == "seed";
        if (seeded) button(x0 + pad + half + 8 * u, y, half, 28 * u, "REROLL  (X)", 0.4f, 0.45f, 0.55f, [this] { reroll(); }, true);
        y += 36 * u;
        // The note.
        if (typing) {
            r.rect(x0 + pad - 4 * u, y - 4 * u, w + 8 * u, 64 * u, 0.12f, 0.14f, 0.18f, 1);
            text(x0 + pad, y, 1.4f, "WHAT SHOULD CHANGE?  ENTER: SAVE  ESC: CANCEL", 0.98f, 0.82f, 0.3f);
            const bool blink = (SDL_GetPerformanceCounter() / (SDL_GetPerformanceFrequency() / 2)) % 2;
            wrap(x0 + pad, y + 16 * u, w, 1.5f, te::withCursor(draft, draftEd, blink), 1, 1, 1);
        } else if (hasChoice() && !choice().note.empty()) {
            text(x0 + pad, y, 1.4f, "NOTE:", 0.98f, 0.82f, 0.3f);
            wrap(x0 + pad, y + 14 * u, w, 1.5f, choice().note, 0.9f, 0.9f, 0.9f);
        }
    }

    void drawLabels(const View& v) {
        if (!compare) return;
        const ModelDef& d = model();
        for (int k = 0; k < (int)slot.size() && k < (int)d.variants.size(); k++) {
            float sx, sy;
            if (!project(v, slot[k], sx, sy)) continue;
            const std::string tag = std::string(1, (char)('A' + k)) + "  " + d.variants[k].name;
            const float w = tw(tag, 1.6f) + 16 * u, y = std::min(sy + 14 * u, H - 70 * u);
            const bool on = k == focus;
            r.rect(sx - w / 2, y, w, 22 * u, on ? 0.3f : 0.05f, on ? 0.5f : 0.06f, on ? 0.75f : 0.08f, 0.85f);
            text(sx - w / 2 + 8 * u, y + 5 * u, 1.6f, tag, 1, 1, 1);
            const bool in = mk::variantIncluded(d, k);
            text(sx - tw(in ? "IN GAME" : "LEFT OUT", 1.3f) / 2, y + 26 * u, 1.3f, in ? "IN GAME" : "LEFT OUT", in ? 0.4f : 0.95f, in ? 0.95f : 0.45f, in ? 0.5f : 0.3f);
            const int kk = k;
            hits.push_back({sx - w / 2, y, w, 22 * u, [this, kk](float) { focus = kk; }});
        }
    }

    void drawHints() {
        const char* h = typing ? "TYPE A NOTE FOR THE NEXT ROUND" :
                                 "DRAG: TURN   WHEEL: ZOOM   SPACE: ALL / ONE   1-4: DESIGN   UP/DOWN: MODEL   TAB: NEXT UNSEEN   T: SPIN   L: NIGHT   ESC: QUIT";
        const float cx = LW + (W - LW - RW) / 2, room = W - LW - RW - 24 * u;
        const float s = std::min(1.4f, room / std::max(1.0f, tw(h, 1.0f)));
        const float w = tw(h, s) + 20 * u;
        r.rect(cx - w / 2, H - 28 * u, w, 22 * u, 0.04f, 0.05f, 0.06f, 0.75f);
        text(cx - w / 2 + 10 * u, H - 17 * u - 3.5f * s * u, s, h, 0.85f, 0.87f, 0.9f);
        if (toastT > 0) {
            const float tw2 = tw(toast, 1.8f) + 24 * u;
            r.rect(cx - tw2 / 2, 16 * u, tw2, 28 * u, 0.1f, 0.3f, 0.15f, std::min(1.0f, (float)toastT) * 0.9f);
            text(cx - tw2 / 2 + 12 * u, 24 * u, 1.8f, toast, 1, 1, 1, std::min(1.0f, (float)toastT));
        }
    }

    // ---- Input ----
    void key(int sc, bool repeat) {
        if (typing) {
            const uint8_t* ks = SDL_GetKeyboardState(nullptr);
            if (te::key(draft, draftEd, sc, ks[224] || ks[228], ks[SC_LSHIFT] || ks[SC_RSHIFT], {te::Kind::Note, 240}).handled) return;
            if (sc == SC_RETURN || sc == SC_KP_ENTER) endNote(true);
            else if (sc == SC_ESCAPE) endNote(false);
            return;
        }
        const int n = (int)model().variants.size();
        switch (sc) {
        case SC_UP: select(sel - 1); break;
        case SC_DOWN: select(sel + 1); break;
        case SC_LEFT:
            focus = (focus + n - 1) % n;
            if (!compare) rebuild();
            break;
        case SC_RIGHT:
            focus = (focus + 1) % n;
            if (!compare) rebuild();
            break;
        case SC_1: case SC_2: case SC_3: case SC_4:
            if (sc - SC_1 < n) {
                focus = sc - SC_1;
                if (!compare) rebuild();
            }
            break;
        case SC_SPACE: compare = !compare, rebuild(); break;
        case SC_TAB: nextNew(SDL_GetKeyboardState(nullptr)[SC_LSHIFT] ? -1 : 1); break;
        case SC_RETURN: case SC_KP_ENTER: if (!repeat) toggleThis(); break;
        case SC_R: if (!repeat) redo(); break;
        case SC_N: if (!repeat) startNote(); break;
        case SC_BACKSPACE: case SC_DELETE: if (!repeat) resetTweaks(); break;
        case SC_X: reroll(); break;
        case SC_K: keepAll(); break;
        case SC_T: orbit = !orbit; break;
        case SC_L: night = !night; break;
        case SC_F: figure = !figure, rebuild(); break;
        case SC_LEFTBRACKET: spin -= M_PI / 8, rebuild(); break;
        case SC_RIGHTBRACKET: spin += M_PI / 8, rebuild(); break;
        default: break;
        }
    }

    int run() {
        for (int i = 0; i < (int)mk::catalog().size(); i++) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [](int a, int b) { return mk::catalog()[a].category < mk::catalog()[b].category; });
        if (order.empty()) {
            std::fprintf(stderr, "model lab: no models\n");
            return 1;
        }
        // Approvals need a crisp, true picture: the lab may be entered from a menu (menu blur still up) or
        // while adaptive quality has the scale or MSAA down, so take the settings as they'd be at their best.
        const RenderSettings entered = r.settings;
        r.settings.blur = 0, r.settings.renderScale = std::max(1.0f, r.settings.renderScale);
        r.settings.msaa = std::max(4, r.settings.msaa);
        struct Restore { Renderer& r; RenderSettings s; ~Restore() { r.settings = s; } } restore{r, entered};
        picksPath = mk::choicesPath();
        mk::loadChoices(picksPath);
        std::printf("model lab: %zu models; picks in %s\n", order.size(), picksPath.empty() ? "(nowhere: no assets folder)" : picksPath.c_str());
        r.frameModels.clear();
        r.carModels.clear();
        sim.world = studioWorld();
        r.setWorld(sim.world);
        int start = 0;
        if (!opt.select.empty())
            for (int k = 0; k < (int)order.size(); k++)
                if (mk::catalog()[order[k]].id.find(opt.select) != std::string::npos) {
                    start = k;
                    break;
                }
        const bool scripted = !opt.shotFrames.empty();
        if (scripted) orbit = false;
        select(start);
        if (opt.variant >= 0) focus = opt.variant, compare = false, rebuild();
        const uint64_t freq = SDL_GetPerformanceFrequency();
        uint64_t last = SDL_GetPerformanceCounter();
        bool running = true;
        int frame = 0;
        while (running) {
            const uint64_t now = SDL_GetPerformanceCounter();
            const double dt = std::min(0.1, (double)(now - last) / freq);
            last = now;
            int dw = 1, dh = 1, ww = 1, wh = 1;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            SDL_GetWindowSize(win, &ww, &wh);
            W = dw, H = dh;
            u = std::max(0.75f, H / 720.0f);
            LW = 300 * u, RW = 370 * u;
            auto toPx = [&](int x, int y, float& px, float& py) { px = x * (float)dw / std::max(1, ww), py = y * (float)dh / std::max(1, wh); };
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_TEXTINPUT:
                    if (typing) {
                        te::insert(draft, draftEd, evText(e), {te::Kind::Note, 240});
                    }
                    break;
                case SDL_KEYDOWN:
                    if (evKeyScancode(e) == SC_ESCAPE && !typing) running = false;
                    else key(evKeyScancode(e), evKeyRepeat(e));
                    break;
                case SDL_MOUSEMOTION: {
                    toPx(evMotionX(e), evMotionY(e), mouseX, mouseY);
                    if (dragSlider >= 0 && dragSlider < (int)hits.size()) {
                        const Hit& h = hits[dragSlider];
                        h.click((mouseX - h.x) / h.w);
                    } else if (orbiting) {
                        const float dx = evMotionXrel(e) * (float)dw / std::max(1, ww), dy = evMotionYrel(e) * (float)dh / std::max(1, wh);
                        if (std::fabs(mouseX - pressX) + std::fabs(mouseY - pressY) > 4) moved = true;
                        yaw -= dx * 0.006, pitch = std::clamp(pitch + dy * 0.005, -0.1, 1.45);
                        orbit = false;
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: {
                    toPx(evButtonX(e), evButtonY(e), mouseX, mouseY);
                    pressX = mouseX, pressY = mouseY, moved = false;
                    bool handled = false;
                    if (evButton(e) == 1)
                        for (int i = (int)hits.size() - 1; i >= 0 && !handled; i--) {
                            const Hit& h = hits[i];
                            if (mouseX >= h.x && mouseX < h.x + h.w && mouseY >= h.y && mouseY < h.y + h.h) {
                                handled = true;
                                if (h.slider) dragSlider = i;
                                auto fn = h.click;  // (may rebuild the hits)
                                fn((mouseX - h.x) / h.w);
                            }
                        }
                    if (!handled && mouseX > LW && mouseX < W - RW) orbiting = true;
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                    if (orbiting && !moved && compare) {  // a click on a design: focus it
                        View v;
                        camera(v);
                        double best = 1e9;
                        for (int k = 0; k < (int)slot.size(); k++) {
                            float sx, sy;
                            if (project(v, slot[k] + V3(0, radius * 0.3, 0), sx, sy) && std::fabs(sx - mouseX) < best) best = std::fabs(sx - mouseX), focus = k;
                        }
                    }
                    orbiting = false, dragSlider = -1;
                    break;
                case SDL_MOUSEWHEEL:
                    if (mouseX < LW) listScroll -= evWheelY(e) * 2;
                    else zoom = std::clamp(zoom * std::pow(0.88, evWheelY(e)), 0.15, 4.0);
                    break;
                default: break;
                }
            }
            if (orbit && !orbiting) yaw += dt * 0.18;
            toastT = std::max(0.0, toastT - dt);
            r.daylight = computeDaylight(night ? 22.0 : opt.hours);
            View v;
            camera(v);
            v.time = (float)(now / (double)freq);
            r.figures.clear();
            r.frameModels.clear();  // (the game's jack, car parts and frame, left from its last frame, aren't part of the display)
            r.carModels.clear();
            r.xrayRange = 0, r.wheelWork = Renderer::WheelWork(), r.carried.on = false;
            r.drawScene(sim, v, DEBUG_OFF, nullptr);
            hits.clear();
            drawLabels(v);
            drawList();
            drawDetail();
            drawHints();
            r.flushHud(W, H);
            if (scripted && opt.shotFrames.count(frame)) {
                char path[512];
                std::snprintf(path, sizeof path, "%s_%04d.ppm", opt.shotPrefix.c_str(), frame);
                writePpm(path, r.readPixels(W, H), W, H);
                std::printf("wrote %s\n", path);
                if (frame >= *opt.shotFrames.rbegin()) running = false;
            }
            SDL_GL_SwapWindow(win);
            frame++;
        }
        if (typing) endNote(true);
        return 0;
    }
};

}  // namespace

int runModelLab(SDL_Window* win, Renderer& renderer, const LabOptions& opt) {
    Lab lab(win, renderer, opt);
    return lab.run();
}
