#include "showroom.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "builds.h"
#include "career.h"
#include "carframe.h"
#include "carparts.h"
#include "carvisual.h"
#include "daylight.h"
#include "hudkit.h"
#include "lights.h"
#include "render.h"
#include "sdl_min.h"

namespace {

using hud::label;
using hud::px;

constexpr double kDist = 1.25;   // orbit distance, car lengths (the whole car in the middle of the screen)
constexpr double kIdle = 4.0;    // s without input before the turntable turns
constexpr double kSpin = 0.18;   // rad/s, the model lab's turn
constexpr double kEase = 0.6;    // s to ease to a slot's framing
constexpr double kHours = 13.5;  // the studio's light: the model lab's hour
constexpr double kFovy = 0.9;    // (the lab's)

// Per slot: yaw (rad, from the car's +x), pitch and zoom. Engine and gearbox: front three-quarter, slightly high;
// suspension, wheels and tires: low three-quarter at wheel height, closer, so a stance change shows. (BUILDS: the engine's.)
struct Framing { double yaw, pitch, zoom; };
Framing framing(int slot) { return slot == MOD_ENGINE || slot == MOD_TRANSMISSION ? Framing{0.6, 0.25, 1.0} : Framing{0.9, 0.05, 0.8}; }

// The look (the old garage's): panel, amber titles, grey labels, light values.
constexpr float kPanel[4] = {0.02f, 0.025f, 0.035f, 0.88f};
constexpr float kAmber[3] = {1.0f, 0.75f, 0.3f};
constexpr float kGrey[3] = {0.6f, 0.62f, 0.66f};
constexpr float kValue[3] = {0.85f, 0.87f, 0.9f};

void panel(Renderer& r, const Rect& p) { r.rect(p.x, p.y, p.w, p.h, kPanel[0], kPanel[1], kPanel[2], kPanel[3]); }
bool inside(const Rect& p, float x, float y) { return x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h; }

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

// Word-wrapped text: returns the height used.
float wrap(Renderer& r, float x, float y, float width, float s, float lh, const std::string& t, const float* c) {
    const int perLine = std::max(8, (int)(width / (6 * s)));
    std::string line, word;
    float yy = y;
    auto flush = [&] {
        r.text(x, yy, s, line, c[0], c[1], c[2]);
        yy += lh;
        line.clear();
    };
    for (size_t i = 0; i <= t.size(); i++) {
        const char ch = i < t.size() ? t[i] : ' ';
        if (ch != ' ') {
            word += ch;
            continue;
        }
        if (word.empty()) continue;
        if ((int)(line.size() + word.size() + (line.empty() ? 0 : 1)) > perLine && !line.empty()) flush();
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    }
    if (!line.empty()) flush();
    return yy - y;
}

const char* const kTabs[TAB_COUNT] = {"ENGINE", "SUSP", "TRANS", "WHEELS", "TIRES", "BUILDS"};  // (modSlotName's, short enough for a tab)

// A donor's tile title: its catalog name (the id, as the option names use), or SWAP for the vehicles the game doesn't offer.
std::string donorTitle(int donor) {
    if (!vehicleListed(donor)) return "SWAP";
    std::string n = vehicleCatalog()[donor].id;
    for (char& ch : n) ch = (char)std::toupper((unsigned char)ch);
    return n;
}

// The Task 10 hook: a tile's picture, the donor's stock car as a GL texture (WHEEL: its front wheel, close), or 0 while
// there isn't one, when the tile is text only. The thumbnails (Renderer::carThumb) plug in here and in drawTile.
GLuint tilePicture(int vehicle, bool wheel) {
    (void)vehicle, (void)wheel;
    return 0;
}

// A row's name without the "WHO: " the breadcrumb now says (the vehicle's own parts have none).
std::string rowName(const ModOption& o, int vehicle) {
    if (o.fromVehicle == vehicle) return o.name;
    const size_t at = o.name.find(": ");
    return at == std::string::npos ? o.name : o.name.substr(at + 2);
}

}  // namespace

struct Showroom::TileInfo {
    std::string title, head, tags;  // tags: YOURS / FITTED, right-aligned on the second caption line
    int count = 1;                  // options in the group (a badge when more than one)
    int pictureOf = -1;             // the vehicle whose stock car is the picture
    bool wheel = false, build = false;
};

Showroom::Showroom(Renderer& rr, std::function<void()> restoreGameWorld) : r(rr), restore(std::move(restoreGameWorld)) {
    VisualHooks h;
    h.prebuild = [](const VehicleSpec& s, const V3& c) { prebuildVisual(s, c); };
    h.ready = [](const VehicleSpec& s) { return visualReady(s); };
    // Every cache keyed by the spec's address lets go of it before the core frees it: the renderer's buffers for its
    // template (and the template itself), then its lamp layout. Called from update, never while drawing.
    h.evict = [this](const VehicleSpec& s) {
        if (!evictVisual(s, [this](const VisualTemplate& t) { r.forgetTemplate(&t); })) return false;  // still building
        forgetLampLayout(&s);
        return true;
    };
    core = std::make_unique<ShowroomCore>(std::move(h));
    // A build previews on top of the base it is given (YOUR PICK: the parts a build doesn't name stay as picked).
    nav.resolve = [this](int b, const ModSet& base) {
        ModSet m = base;
        resolveBuild(core->vehicle(), builds()[b], m);
        return m;
    };
}

Showroom::~Showroom() {
    if (core->isOpen()) core->close();
    // A spec whose template a worker is still making can't go yet (evict: false): wait for it, up to 3 s.
    for (int k = 0; k < 300 && core->liveSpecs() > 0; k++) {
        core->update(0);
        if (core->liveSpecs() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (core->liveSpecs() > 0) (void)core.release();  // (left to the worker rather than freed under it)
}

void Showroom::open(int vehicle, const ModSet& fitted, const V3& color, bool career) {
    if (phase != CLOSED) return;
    core->open(vehicle, fitted, color);  // (the preview is built behind the fade)
    career_ = career;
    phase = OPENING, scene = false, recal = fitWanted = buyWanted = closeWanted = orbiting = swapWait = false;
    fade.start();
    scrollAdj = 0, idle = 0, toastT = 0, blackT = 0, hover = -1;
    heads.clear();
    nav.open(vehicle, core->fitted(), (int)builds().size());
    const ShowroomLayout l = showroomLayout(W, H);
    nav.setView(l.tileCols, l.tileRows, std::max(1, l.listRows));
    frameTab(nav.tabIndex(), true);
}

bool Showroom::isOpen() const { return phase != CLOSED; }
bool Showroom::drawsScene() const { return scene; }
const Car* Showroom::previewCar() const { return core->preview(); }

void Showroom::requestClose() {
    if (phase == OPENING || phase == OPEN) closeWanted = true;
}

void Showroom::fitDone() { swapWait = false; }

void Showroom::touched() { idle = 0; }

int Showroom::hitAt(float x, float y) const {
    for (int i = (int)hits.size() - 1; i >= 0; i--)
        if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) return i;
    return -1;
}

void Showroom::frameTab(int tab, bool now) {
    slot = tab == TAB_BUILDS ? MOD_ENGINE : tab;
    const Framing f = framing(slot);
    if (now) yaw = f.yaw, pitch = f.pitch, zoom = f.zoom, ease.t = -1;
    else ease = {0, yaw, pitch, zoom, f.yaw, f.pitch, f.zoom};
}

void Showroom::tabBy(int d) {
    navDone(nav.tab(d));  // (never onto the open tab: that would take YOUR PICK again)
    frameTab(nav.tabIndex());
}

void Showroom::backToTiles() {
    bool close = false;
    navDone(nav.back(close));  // (at the tiles it only says close: ESC does that, not a click)
}

void Showroom::navDone(bool pickChanged) {
    scrollAdj = 0;
    if (pickChanged) core->setPicks(nav.pick());
}

// What the group's remembered variant does to this vehicle alone (applyMods on a scratch spec), made once per (slot, option).
std::string Showroom::headline(int s, int option) {
    const int key = s * 1000 + option;
    if (const auto it = heads.find(key); it != heads.end()) return it->second;
    const int v = core->vehicle();
    ModSet m;
    m.pick[s] = option;
    VehicleSpec vs;
    applyMods(v, m, vs);
    char b[64];
    switch (s) {
    case MOD_ENGINE: std::snprintf(b, sizeof b, "%.0f HP", peakPower(vs) / 745.7); break;
    case MOD_TRANSMISSION: std::snprintf(b, sizeof b, "%d SPD %.2f", (int)vs.gears.size(), vs.finalDrive); break;
    case MOD_SUSPENSION: {  // the same body-height change the detail text calls RIDE
        const long mm = std::lround((vs.layerY[0] - vehicleCatalog()[v].layerY[0]) * 1000);
        if (mm == 0) std::snprintf(b, sizeof b, "0 MM");
        else std::snprintf(b, sizeof b, "%+ld MM", mm);
        break;
    }
    case MOD_WHEELS: std::snprintf(b, sizeof b, "%.0f MM %.0f IN", vs.wheelRadius * 2000, vs.rimRadius * 2 / 0.0254); break;
    default: std::snprintf(b, sizeof b, "%s", tireTypeName(vs.tireType)); break;
    }
    return heads[key] = b;
}

Showroom::Ui Showroom::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONUP && evButton(e) == 1) orbiting = false;
    if (phase != OPEN || closeWanted) return Ui::NONE;
    const float k = 1 / std::clamp(r.dpiFactor, 0.25f, 1.0f);  // (window points to drawable pixels)
    Ui ui = Ui::NONE;
    switch (e.type) {
    case SDL_KEYDOWN: {
        const int sc = evKeyScancode(e);
        touched();
        const int dx = sc == SC_D || sc == SC_RIGHT ? 1 : sc == SC_A || sc == SC_LEFT ? -1 : 0;  // (the pad's menu mode taps the arrows)
        const int dy = sc == SC_S || sc == SC_DOWN ? 1 : sc == SC_W || sc == SC_UP ? -1 : 0;
        if (dx || dy) {
            const int was = nav.cursor();
            navDone(nav.move(dx, dy));
            if (nav.cursor() != was) ui = Ui::HOVER;
        } else if (evKeyRepeat(e)) break;
        else if (sc == SC_Q || sc == SC_E) tabBy(sc == SC_E ? 1 : -1), ui = Ui::HOVER;
        else if (sc == SC_RETURN || sc == SC_KP_ENTER) {  // opens a tile, or backs out of one (the pad's A); never fits
            const int was = nav.level();
            navDone(nav.enter());
            if (nav.level() != was) ui = Ui::CLICK;
        } else if (sc == SC_BACKSPACE) {
            if (nav.level() == 2) backToTiles(), ui = Ui::CLICK;
        } else if (sc == SC_ESCAPE) {  // (the pad's B)
            bool close = false;
            navDone(nav.back(close));
            if (close) requestClose();
            else ui = Ui::CLICK;
        } else if (sc == SC_G) requestClose();
        else if (sc == SC_F) fitWanted = true, ui = Ui::CLICK;  // (the pad's X)
        else if (sc == SC_C) recal = !recal, ui = Ui::CLICK;
        else if (sc == SC_B) buyWanted = true, ui = Ui::CLICK;  // (the pad's Y)
        break;  // (R revs in phase 3)
    }
    case SDL_MOUSEMOTION: {
        mouseX = evMotionX(e) * k, mouseY = evMotionY(e) * k;
        if (orbiting) {
            touched(), ease.t = -1;
            yaw -= evMotionXrel(e) * k * 0.006, pitch = std::clamp(pitch + evMotionYrel(e) * k * 0.005, -0.1, 1.45);  // (the lab's rates)
        }
        const int h = orbiting ? -1 : hitAt(mouseX, mouseY);
        if (h >= 0 && h != hover) ui = Ui::HOVER;  // (onto a tab, a row or the button, as the menus do)
        hover = h;
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        mouseX = evButtonX(e) * k, mouseY = evButtonY(e) * k;
        if (evButton(e) == 3 && nav.level() == 2 && inside(showroomLayout(W, H).left, mouseX, mouseY)) {  // right-click: back to the tiles
            backToTiles(), touched();
            return Ui::CLICK;
        }
        if (evButton(e) != 1) break;
        touched();
        if (const int h = hitAt(mouseX, mouseY); h >= 0) {
            auto fn = hits[h].click;  // (may change what the hits are)
            fn();
            return Ui::CLICK;
        }
        orbiting = inside(showroomLayout(W, H).car, mouseX, mouseY);
        break;
    }
    case SDL_MOUSEWHEEL: {
        touched();
        const ShowroomLayout l = showroomLayout(W, H);
        if (inside(l.left, mouseX, mouseY)) scrollAdj -= evWheelY(e);  // (drawLeft keeps it in range)
        else if (inside(l.car, mouseX, mouseY)) ease.t = -1, zoom = std::clamp(zoom * std::pow(0.88, evWheelY(e)), 0.15, 4.0);
        break;
    }
    default: break;
    }
    return ui;
}

Showroom::Ui Showroom::pad(const pad::Raw& raw, double dt) {
    const uint32_t pressed = raw.buttons & ~padPrev;
    padPrev = raw.buttons;
    rev = raw.rt;
    if (phase != OPEN || closeWanted) return Ui::NONE;
    Ui ui = Ui::NONE;
    if (pressed & pad::bit(pad::B_LB)) touched(), tabBy(-1), ui = Ui::HOVER;
    if (pressed & pad::bit(pad::B_RB)) touched(), tabBy(1), ui = Ui::HOVER;
    if (pressed & pad::bit(pad::B_X)) touched(), fitWanted = true, ui = Ui::CLICK;  // (A opens tiles in the menus, so FIT is X)
    const pad::Stick s = pad::radialDeadzone(raw.rx, raw.ry, pad::deadzoneRadius(1));
    if (s.x == 0 && s.y == 0) return ui;
    touched(), ease.t = -1;
    // Full stick: a turn in ~3 s (2 rad/s, an estimate that keeps the car readable while it swings), the pitch at
    // half that; with LT held, up-down zooms (full stick: x4.5 a second, so the whole range in ~2 s).
    yaw -= s.x * 2.0 * dt;
    if (raw.lt > 0.5f) zoom = std::clamp(zoom * std::exp(s.y * 1.5 * dt), 0.15, 4.0);
    else pitch = std::clamp(pitch + s.y * 1.0 * dt, -0.1, 1.45);
    return ui;
}

ShowroomResult Showroom::update(double dt, Career& career, bool station) {
    ShowroomResult res;
    careerNow = &career, atStation = station;
    clock += dt;
    toastT = std::max(0.0, toastT - dt);
    // The fade holds black: opening, until the preview is in (build it, then fade in); closing after a FIT, until main
    // has the fitted car in (fitDone: the swap happens behind it). At most kBuildTimeout, when the core gives up too.
    const bool wait = phase == OPEN ? !core->preview() && !core->failed() : phase == CLOSING && swapWait;
    const bool hold = wait && blackT < ShowroomCore::kBuildTimeout;
    if (hold && fade.isBlack()) blackT += dt;
    if (fade.update(dt, hold)) {  // black: swap the worlds
        if (phase == OPENING) {
            r.frameModels.clear();  // (the game's jack, tools and frames from its last frame aren't in the studio)
            r.carModels.clear();
            r.setWorld(core->studio().world);
            scene = true, phase = OPEN;
        } else if (phase == CLOSING) {
            core->close();
            scene = false;
            restore();
        }
    }
    if (phase == OPEN && buyWanted && !closeWanted && nav.tabIndex() == TAB_BUILDS) say("BUY PARTS ONE AT A TIME, ON THEIR OWN TABS");
    else if (phase == OPEN && buyWanted && !closeWanted) {
        std::string said;
        res.bought = showroomBuy(career, core->vehicle(), slot, core->pick().pick[slot], atStation, said);
        if (!said.empty()) say(said);
    }
    buyWanted = false;
    if (phase == OPEN && fitWanted && !closeWanted) {
        const int v = core->vehicle();
        const ModSet& pick = core->pick();
        bool same = true, owned = true;
        int locked = -1;
        for (int k = 0; k < MOD_SLOTS; k++) {
            same = same && pick.pick[k] == core->fitted().pick[k];
            if (career_ && !career.owns(v, k, pick.pick[k]) && locked < 0) owned = false, locked = k;
        }
        if (same && !recal) requestClose();  // nothing to fit: back to driving (with recal on, the same mods refit to recalibrate)
        else if (career_ && !atStation && !same) say("DRIVE TO A REPAIR STATION TO FIT PARTS");  // (a recal alone changes no part: anywhere)
        else if (!owned) say(std::string("LOCKED: ") + modOptions(v, locked)[pick.pick[locked]].name);
        else {
            res.act = ShowroomResult::FIT, res.mods = pick, res.recal = recal;
            swapWait = true;
            requestClose();
        }
    }
    fitWanted = false;
    if (closeWanted && phase == OPEN && fade.start()) phase = CLOSING, closeWanted = false, orbiting = false, blackT = 0;
    if (phase == CLOSING && !fade.busy()) {
        phase = CLOSED;
        if (res.act == ShowroomResult::NONE) res.act = ShowroomResult::CLOSED;
    }
    if (phase == OPEN || phase == OPENING) {  // the camera: easing to a slot's framing, or turning by itself once left alone
        idle += dt;
        if (ease.t >= 0) {
            ease.t += dt;
            const double a = std::min(1.0, ease.t / kEase), e = a * a * (3 - 2 * a);
            yaw = ease.yaw0 + std::remainder(ease.yaw1 - ease.yaw0, 2 * M_PI) * e;
            pitch = ease.pitch0 + (ease.pitch1 - ease.pitch0) * e;
            zoom = ease.zoom0 + (ease.zoom1 - ease.zoom0) * e;
            if (a >= 1) ease.t = -1;
        } else if (idle >= kIdle && !orbiting) yaw += kSpin * dt;
    }
    core->update(dt);  // (builds, swaps the preview, steps the studio, evicts: here, never inside drawScene)
    return res;
}

void Showroom::camera(View& v) const {
    const ShowroomLayout l = showroomLayout(W, H);
    V3 target(0, 0.8, 0);
    double length = vehicleCatalog()[core->vehicle()].length, carYaw = 0;
    if (const Car* c = core->preview()) {
        const Frame f = c->frame();
        target = f.com, length = c->spec->length, carYaw = std::atan2(-f.fwd.z, f.fwd.x);
    }
    v.fovy = (float)kFovy;
    v.width = W, v.height = H;
    v.time = (float)clock;
    const double dist = kDist * length * zoom, a = carYaw + yaw;
    const V3 dir(std::cos(a) * std::cos(pitch), std::sin(pitch), -std::sin(a) * std::cos(pitch));
    V3 eye = target + dir * dist;
    if (eye.y < 0.3) eye.y = 0.3;
    // Slide the view so the car sits in the middle of the space between the panels (as the lab does).
    const V3 fwd = norm(target - eye), right = norm(cross(fwd, V3(0, 1, 0))), up = cross(right, fwd);
    const double f = H / 2.0 / std::tan(kFovy / 2), d = len(target - eye);
    const double dx = (l.car.x + l.car.w / 2) - W / 2.0, dy = (l.car.y + l.car.h / 2) - H / 2.0;
    const V3 shift = right * (-dx * d / f) + up * (dy * d / f);
    v.eye = eye + shift, v.target = target + shift, v.up = V3(0, 1, 0);
}

void Showroom::draw(int w, int h) {
    W = w, H = h;
    hits.clear();
    const float keepAlpha = r.hudAlpha;
    r.hudAlpha = 1;
    if (scene) {
        View v;
        camera(v);
        Sim& studio = core->studio();
        r.daylight = computeDaylight(kHours);
        r.figures.clear();
        r.people.v.clear();
        r.frameModels.clear();
        r.carModels.clear();
        r.xrayRange = 0, r.wheelWork = Renderer::WheelWork(), r.carried.on = false;
        r.carBeams.assign(studio.cars.size(), 0);
        if (const Car* c = core->preview()) {  // its engine, gearbox and running gear, as the game draws a car up close
            r.carModels.resize(1);
            r.carModels[0].car = 0;
            carPartModels(*c, r.carModels[0].parts, PARTS_ALL);
            carFrameModels(*c, r.carModels[0].parts, PARTS_ALL);
        }
        r.drawScene(studio, v, DEBUG_OFF, nullptr);
        const ShowroomLayout l = showroomLayout(W, H);
        drawOverCar(l);
        drawLeft(l);
        drawRight(l);
        drawBottom(l);
    }
    if (fade.busy()) r.rect(0, 0, (float)W, (float)H, 0, 0, 0, fade.alpha());
    r.hudAlpha = keepAlpha;
}

// A tile: a 4:3 picture over two caption lines. With no picture (tilePicture: 0) the plate holds the headline instead.
void Showroom::drawTile(const Rect& t, const TileInfo& ti, bool cursor, bool inPick, float u) {
    const float s = px(1.4f * u), pic = t.w * 0.75f, in = 4 * u;
    r.rect(t.x, t.y, t.w, t.h, 1, 1, 1, cursor ? 0.07f : 0.03f);
    const bool picture = tilePicture(ti.pictureOf, ti.wheel) != 0;
    if (!picture) {  // (a text tile: the Task 10 picture draws over this plate, in the same rect)
        r.rect(t.x, t.y, t.w, pic, 0, 0, 0, 0.25f);
        if (ti.build) wrap(r, t.x + in, t.y + in, t.w - 2 * in, s, 8 * s + 2 * u, ti.head, kGrey);
        else {
            float bs = px(2.5f * u);
            while (bs > 1 && r.textWidth(ti.head, bs) > t.w - 2 * in) bs -= 1;
            r.text(t.x + (t.w - r.textWidth(ti.head, bs)) / 2, t.y + (pic - 7 * bs) / 2, bs, ti.head, kValue[0], kValue[1], kValue[2]);
        }
    }
    if (ti.count > 1) {  // the variant count badge, top right of the plate
        const std::string n = std::to_string(ti.count);
        const float bw = r.textWidth(n, s) + 6 * u, bh = 7 * s + 4 * u;
        r.rect(t.x + t.w - bw - in, t.y + in, bw, bh, 0, 0, 0, 0.6f);
        r.text(t.x + t.w - bw - in + 3 * u, t.y + in + 2 * u, s, n, kAmber[0], kAmber[1], kAmber[2]);
    }
    const float y1 = t.y + pic + 2 * u, y2 = y1 + 7 * s + 4 * u;
    auto fitText = [&](const std::string& x, float width) {
        const int chars = std::max(3, (int)(width / (6 * s)));
        return (int)x.size() > chars ? x.substr(0, chars - 2) + ".." : x;
    };
    r.text(t.x + in, y1, s, fitText(ti.title, t.w - 2 * in), kAmber[0], kAmber[1], kAmber[2]);
    const float tagW = r.textWidth(ti.tags, s);
    if (!ti.tags.empty()) r.text(t.x + t.w - in - tagW, y2, s, ti.tags, kGrey[0], kGrey[1], kGrey[2]);
    if (picture && !ti.build) r.text(t.x + in, y2, s, fitText(ti.head, t.w - 3 * in - tagW), kValue[0], kValue[1], kValue[2]);  // (a text tile has it on the plate)
    if (inPick) {  // a 1u amber outline on the group holding the pick
        const float o = std::max(1.0f, u);
        r.rect(t.x, t.y, t.w, o, kAmber[0], kAmber[1], kAmber[2], 1);
        r.rect(t.x, t.y + t.h - o, t.w, o, kAmber[0], kAmber[1], kAmber[2], 1);
        r.rect(t.x, t.y, o, t.h, kAmber[0], kAmber[1], kAmber[2], 1);
        r.rect(t.x + t.w - o, t.y, o, t.h, kAmber[0], kAmber[1], kAmber[2], 1);
    }
}

// Tabs, then level 1: the tab's groups as tiles; level 2: a breadcrumb and the open group's variants as rows (the pick
// highlighted, the fitted one marked).
void Showroom::drawLeft(const ShowroomLayout& l) {
    const float u = l.u, s = px(1.4f * u), big = px(2.0f * u), pad = 12 * u;
    const Rect& p = l.left;
    const int v = core->vehicle();
    panel(r, p);
    label(r, p.x + pad, p.y + 12 * u, big, vehicleCatalog()[v].name, kAmber[0], kAmber[1], kAmber[2]);
    const float gap = 3 * u, tw = (p.w - 2 * pad - (TAB_COUNT - 1) * gap) / TAB_COUNT, ty = p.y + 34 * u, th = 18 * u;
    for (int k = 0; k < TAB_COUNT; k++) {
        const float x = p.x + pad + k * (tw + gap);
        const bool on = k == nav.tabIndex();
        r.rect(x, ty, tw, th, 1, 1, 1, on ? 0.12f : 0.04f);
        if (on) r.rect(x, ty + th - 2 * u, tw, 2 * u, kAmber[0], kAmber[1], kAmber[2], 1);
        float ts = s;
        while (ts > 1 && r.textWidth(kTabs[k], ts) > tw - 4 * u) ts -= 1;
        const float* c = on ? kAmber : kGrey;
        r.text(x + (tw - r.textWidth(kTabs[k], ts)) / 2, ty + (th - 7 * ts) / 2, ts, kTabs[k], c[0], c[1], c[2]);
        hits.push_back({x, ty, tw, th, [this, k] {
                            if (k != nav.tabIndex()) tabBy(k - nav.tabIndex());
                            else backToTiles();
                        }});
    }
    const int t = nav.tabIndex(), rows = std::max(1, l.listRows);
    nav.setView(l.tileCols, l.tileRows, rows);
    const ModSet& pick = core->pick();
    const float top = p.y + 60 * u;
    if (nav.level() == 1) {
        const int n = nav.tileCount(), total = (n + l.tileCols - 1) / l.tileCols;
        const int first = std::clamp(nav.scroll() + scrollAdj, 0, std::max(0, total - l.tileRows));
        scrollAdj = first - nav.scroll();  // (the wheel stops at the ends)
        const bool builds_ = t == TAB_BUILDS;
        const int fitted = builds_ ? -1 : nav.tree().groupOf(t, core->fitted().pick[t]);
        const int inPick = builds_ ? nav.cursor() : nav.tree().groupOf(t, pick.pick[t]);
        for (int row = 0; row < l.tileRows; row++)
            for (int col = 0; col < l.tileCols; col++) {
                const int i = (first + row) * l.tileCols + col;
                if (i >= n) break;
                TileInfo ti;
                if (builds_) {
                    ti.build = true;
                    ti.pictureOf = v;
                    if (i == 0) ti.title = "YOUR PICK", ti.head = "THE PARTS YOU HAD WHEN YOU CAME HERE";
                    else {
                        const Build& b = builds()[i - 1];
                        ModSet m = nav.yourPick();
                        resolveBuild(v, b, m);
                        ti.title = b.name, ti.head = b.detail;
                        ti.pictureOf = modOptions(v, MOD_ENGINE)[m.pick[MOD_ENGINE]].fromVehicle;  // the build's engine's donor (or this vehicle)
                    }
                } else {
                    const PartGroup& g = nav.tree().groups[t][i];
                    ti.count = g.count;
                    ti.pictureOf = g.donor >= 0 ? g.donor : v;
                    ti.wheel = t == MOD_WHEELS;
                    ti.head = headline(t, nav.remembered(t, i));
                    ti.title = t == MOD_TIRES ? (i == 0 ? donorTitle(v) : "TREAD") : donorTitle(g.donor);
                    if (g.donor == v) ti.tags = "YOURS";
                    if (i == fitted) ti.tags += std::string(ti.tags.empty() ? "" : " ") + "FITTED";
                }
                const Rect rc = l.tileRect(col, row);
                if (hover == (int)hits.size()) r.rect(rc.x, rc.y, rc.w, rc.h, 1, 1, 1, 0.04f);
                drawTile(rc, ti, i == nav.cursor(), i == inPick, u);
                hits.push_back({rc.x, rc.y, rc.w, rc.h, [this, i] {
                                    navDone(nav.clickTile(i));
                                    navDone(nav.enter());  // (a tile with variants opens; a build or a single part does not)
                                }});
            }
        return;
    }
    // Level 2.
    const PartGroup& g = nav.tree().groups[t][nav.openGroup()];
    const auto& opts = modOptions(v, t);
    const std::string crumb = std::string(kTabs[t]) + " > " + donorTitle(g.donor);
    label(r, p.x + pad, top + (18 * u - 2 * u - 7 * s) / 2, s, crumb, kAmber[0], kAmber[1], kAmber[2]);
    hits.push_back({p.x, top, p.w, 18 * u, [this] { backToTiles(); }});
    const int fitted = core->fitted().pick[t], cur = pick.pick[t], n = g.count;
    const int first = std::clamp(nav.scroll() + scrollAdj, 0, std::max(0, n - rows));
    scrollAdj = first - nav.scroll();
    for (int i = first; i < n && i < first + rows; i++) {
        const int o = g.first + i;
        const Rect rr = l.rowRect(i - first);
        const float y = rr.y, rowH = rr.h, ty2 = y + 2 * u;
        if (o == cur) r.rect(rr.x, y, rr.w, rowH - 2 * u, 1, 1, 1, 0.07f);
        else if (hover == (int)hits.size()) r.rect(rr.x, y, rr.w, rowH - 2 * u, 1, 1, 1, 0.04f);
        float end = p.x + p.w - pad;
        if (o == fitted) {
            end -= r.textWidth("FITTED", s);
            r.text(end, ty2, s, "FITTED", kGrey[0], kGrey[1], kGrey[2]);
            end -= 8 * u;
        }
        std::string name = rowName(opts[o], v);
        const int room = std::max(3, (int)((end - p.x - pad) / (6 * s)));
        if ((int)name.size() > room) name = name.substr(0, room - 2) + "..";
        if (o == cur) r.text(p.x + pad, ty2, s, name, 1.0f, 0.8f, 0.35f);
        else r.text(p.x + pad, ty2, s, name, 0.9f, 0.9f, 0.92f);
        std::string detail = opts[o].detail;  // under the name, in the label grey
        const int dRoom = std::max(3, (int)((p.w - 2 * pad) / (6 * s)));
        if ((int)detail.size() > dRoom) detail = detail.substr(0, dRoom - 2) + "..";
        r.text(p.x + pad, ty2 + 7 * s + 2 * u, s, detail, kGrey[0], kGrey[1], kGrey[2]);
        hits.push_back({p.x, y, p.w, rowH, [this, i] { navDone(nav.clickRow(i)); }});
    }
}

// Phase 1: the old garage's detail lines (the pick's part, and what the combination adds up to).
void Showroom::drawRight(const ShowroomLayout& l) {
    const float u = l.u, s = px(1.4f * u), big = px(2.0f * u), lh = 8 * s + 6 * u, pad = 12 * u;
    const Rect& p = l.right;
    const float x = p.x + pad, w = p.w - 2 * pad;
    const int v = core->vehicle();
    const ModSet& pick = core->pick();
    if (detailVehicle != v || !std::equal(pick.pick, pick.pick + MOD_SLOTS, detailOf.pick)) {  // (made again only when the pick changes)
        applyMods(v, pick, detail);
        detailVehicle = v, detailOf = pick;
    }
    const VehicleSpec& stock = vehicleCatalog()[v];
    const VehicleSpec& vs = detail;
    panel(r, p);
    float y = p.y + 12 * u;
    const bool onBuild = nav.tabIndex() == TAB_BUILDS;
    const Build* build = onBuild && nav.cursor() > 0 ? &builds()[nav.cursor() - 1] : nullptr;
    r.text(x, y, s, onBuild ? "BUILD" : modSlotName(slot), kGrey[0], kGrey[1], kGrey[2]);
    y += lh;
    const ModOption& o = modOptions(v, slot)[pick.pick[slot]];
    y += wrap(r, x, y, w, big, 8 * big + 4 * u, onBuild ? (build ? build->name : "YOUR PICK") : o.name, kAmber);
    std::string what = onBuild ? (build ? build->detail : "THE PARTS YOU HAD PICKED WHEN YOU CAME HERE") : o.detail;
    const bool have = onBuild || !careerNow || careerNow->owns(v, slot, pick.pick[slot]);
    if (!have) {
        const int price = Career::price(v, slot, pick.pick[slot]);
        what = price > 0 ? "$" + std::to_string(price) + (atStation ? (pad::ui.last ? "  Y: BUY IT" : "  B: BUY IT") : "  AT ANY REPAIR STATION")
                         : "NOT SOLD: FIND IT OUT THERE, OR WIN IT";
    }
    static const float kWant[3] = {1.0f, 0.7f, 0.35f};
    y += wrap(r, x, y, w, s, lh, what, have ? kValue : kWant) + lh * 0.6f;
    // What the combination adds up to.
    double rpmP = 0, rpmT = 0;
    const double P = peakPower(vs, &rpmP), T = peakTorque(vs, &rpmT);
    const double lift = (vs.layerY[0] - vs.wheelRadius) - (stock.layerY[0] - stock.wheelRadius);  // body vs axle
    const double ride = vs.layerY[0] - stock.layerY[0];
    const double tire = vs.wheelRadius / stock.wheelRadius - 1;
    const std::string speedo = recal ? "RECALIBRATED TO THESE TIRES: READS TRUE"
                               : std::fabs(tire) < 0.005 ? "FACTORY: READS TRUE"
                                                         : fmt("FACTORY: READS %.0f%%", std::fabs(tire) * 100) + (tire > 0 ? " LOW" : " HIGH");
    const std::vector<std::pair<const char*, std::vector<std::string>>> groups = {
        {"ENGINE", {fmt("%.0f HP AT %.0f RPM", P / 745.7, rpmP), fmt("%.0f NM AT %.0f RPM", T, rpmT), std::string(vs.engineName), fmt("REDLINE %.0f", vs.redline)}},
        {"GEARBOX", {std::string(vs.gearboxName), fmt("FINAL DRIVE %.2f", vs.finalDrive) + (vs.lowRange > 0 ? fmt("   LOW RANGE %.2f", vs.lowRange) : std::string())}},
        {"TIRES", {fmt("%.0f MM TALL  %.0f MM WIDE", vs.wheelRadius * 2000, vs.wheelWidth * 1000), fmt("GRIP %.2f / %.2f", vs.tireGrip, vs.tireGrip * vs.lateralGrip)}},
        {"SPEEDO", {speedo}},
        {"RIDE", {fmt("%+.0f MM BODY HEIGHT", ride * 1000), fmt("%+.0f MM LIFT", lift * 1000), fmt("BOUNCE %.2f / %.2f HZ", vs.rideFreqFront, vs.rideFreqRear)}},
    };
    for (const auto& g : groups) {
        if (y > p.y + p.h - 2 * lh) break;
        r.text(x, y, s, g.first, kGrey[0], kGrey[1], kGrey[2]);
        y += lh;
        for (const std::string& line : g.second) y += wrap(r, x + 8 * u, y, w - 8 * u, s, lh, line, kValue);
        y += lh * 0.4f;
    }
}

// Key or pad hints, cash (career) and the FIT button.
void Showroom::drawBottom(const ShowroomLayout& l) {
    const float u = l.u, big = px(2.0f * u), pad = 12 * u;
    const Rect& p = l.bottom;
    panel(r, p);
    const ModSet& pick = core->pick();
    const bool change = recal || !std::equal(pick.pick, pick.pick + MOD_SLOTS, core->fitted().pick);
    const std::string fit = pad::ui.last ? std::string(pad::buttonName(pad::B_X, pad::ui.style)) + "  FIT" : "F  FIT";  // (A and ENTER open tiles now)
    const float bw = r.textWidth(fit, big) + 28 * u, bh = p.h - 12 * u, bx = p.x + p.w - pad - bw, by = p.y + 6 * u;
    if (change) hud::pill(r, bx, by, bw, bh, 0.8f, 0.55f, 0.2f, 0.95f);
    else hud::pill(r, bx, by, bw, bh, 0.25f, 0.26f, 0.28f, 0.9f);
    r.text(bx + 14 * u, by + (bh - 7 * big) / 2, big, fit, 1, 1, 1, change ? 1.0f : 0.6f);
    hits.push_back({bx, by, bw, bh, [this] { fitWanted = true; }});
    float end = bx - 16 * u;
    float s = px(1.4f * u);
    if (career_ && careerNow) {
        const std::string cash = "CASH $" + std::to_string(careerNow->cash);
        end -= r.textWidth(cash, s);
        r.text(end, p.y + (p.h - 7 * s) / 2, s, cash, 0.55f, 0.9f, 0.6f);
        end -= 16 * u;
    }
    auto name = [](pad::Button b) { return std::string(pad::buttonName(b, pad::ui.style)); };
    const std::string keys = pad::ui.last ? name(pad::B_LB) + " " + name(pad::B_RB) + " TAB   D-PAD MOVE   " + name(pad::B_A) + " OPEN   " + name(pad::B_B) + " BACK   " +
                                                name(pad::B_X) + " FIT"
                                          : "Q E TAB   ARROWS MOVE   ENTER OPEN   ESC BACK   F FIT";
    while (s > 1 && r.textWidth(keys, s) > end - p.x - pad) s -= 1;
    r.text(p.x + pad, p.y + (p.h - 7 * s) / 2, s, keys, kGrey[0], kGrey[1], kGrey[2]);
}

// Over the car: a newer pick building, or one that couldn't be built; and the refusals.
void Showroom::drawOverCar(const ShowroomLayout& l) {
    const float u = l.u, s = px(1.4f * u);
    const Rect& p = l.car;
    const float cx = p.x + p.w / 2;
    if (core->failed()) hud::centeredPill(r, cx, p.y + 14 * u, s, "COULDN'T BUILD THIS COMBINATION", 0.35f, 0.08f, 0.06f, 1, 1.0f, 0.6f, 0.5f);
    else if (core->fitting()) hud::centeredPill(r, cx, p.y + 14 * u, s, "FITTING...", kPanel[0], kPanel[1], kPanel[2], 1, kAmber[0], kAmber[1], kAmber[2]);
    if (toastT > 0) hud::centeredPill(r, cx, p.y + p.h - 40 * u, s, toast, kPanel[0], kPanel[1], kPanel[2], (float)std::min(1.0, toastT));
}
