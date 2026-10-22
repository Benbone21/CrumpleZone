#include "showroom.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

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
// suspension, wheels and tires: low three-quarter at wheel height, closer, so a stance change shows.
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

}  // namespace

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
    phase = OPENING, scene = false, fitWanted = closeWanted = orbiting = false;
    fade.start();
    listScroll = 0, follow = true, idle = 0, toastT = 0;
    setSlot(0, true);
}

bool Showroom::isOpen() const { return phase != CLOSED; }
bool Showroom::drawsScene() const { return scene; }
const Car* Showroom::previewCar() const { return core->preview(); }

void Showroom::requestClose() {
    if (phase == OPENING || phase == OPEN) closeWanted = true;
}

void Showroom::touched() { idle = 0; }

void Showroom::setSlot(int s, bool now) {
    slot = (s % MOD_SLOTS + MOD_SLOTS) % MOD_SLOTS;
    follow = true;
    const Framing f = framing(slot);
    if (now) yaw = f.yaw, pitch = f.pitch, zoom = f.zoom, ease.t = -1;
    else ease = {0, yaw, pitch, zoom, f.yaw, f.pitch, f.zoom};
}

void Showroom::step(int d) {
    const int n = (int)modOptions(core->vehicle(), slot).size();
    core->setPick(slot, (core->pick().pick[slot] + d + n) % n);
    follow = true;
}

void Showroom::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONUP && evButton(e) == 1) orbiting = false;
    if (phase != OPEN || closeWanted) return;
    const float k = 1 / std::clamp(r.dpiFactor, 0.25f, 1.0f);  // (window points to drawable pixels)
    switch (e.type) {
    case SDL_KEYDOWN: {
        const int sc = evKeyScancode(e);
        touched();
        if (sc == SC_W || sc == SC_UP) setSlot(slot - 1);  // (the pad's menu mode taps the arrows)
        else if (sc == SC_S || sc == SC_DOWN) setSlot(slot + 1);
        else if (sc == SC_A || sc == SC_LEFT) step(-1);
        else if (sc == SC_D || sc == SC_RIGHT) step(1);
        else if (evKeyRepeat(e)) break;
        else if (sc == SC_C) recal = !recal;
        else if (sc == SC_RETURN || sc == SC_KP_ENTER) fitWanted = true;  // (the pad's A)
        else if (sc == SC_ESCAPE || sc == SC_G) requestClose();          // (the pad's B)
        break;  // (R revs and B buys in later phases)
    }
    case SDL_MOUSEMOTION:
        mouseX = evMotionX(e) * k, mouseY = evMotionY(e) * k;
        if (orbiting) {
            touched(), ease.t = -1;
            yaw -= evMotionXrel(e) * k * 0.006, pitch = std::clamp(pitch + evMotionYrel(e) * k * 0.005, -0.1, 1.45);  // (the lab's rates)
        }
        break;
    case SDL_MOUSEBUTTONDOWN: {
        mouseX = evButtonX(e) * k, mouseY = evButtonY(e) * k;
        if (evButton(e) != 1) break;
        touched();
        for (int i = (int)hits.size() - 1; i >= 0; i--) {
            const Hit& h = hits[i];
            if (mouseX >= h.x && mouseX < h.x + h.w && mouseY >= h.y && mouseY < h.y + h.h) {
                auto fn = h.click;  // (may change what the hits are)
                fn();
                return;
            }
        }
        orbiting = inside(showroomLayout(W, H).car, mouseX, mouseY);
        break;
    }
    case SDL_MOUSEWHEEL: {
        touched();
        const ShowroomLayout l = showroomLayout(W, H);
        if (inside(l.left, mouseX, mouseY)) listScroll -= evWheelY(e), follow = false;
        else if (inside(l.car, mouseX, mouseY)) ease.t = -1, zoom = std::clamp(zoom * std::pow(0.88, evWheelY(e)), 0.15, 4.0);
        break;
    }
    default: break;
    }
}

void Showroom::pad(const pad::Raw& raw, double dt) {
    const uint32_t pressed = raw.buttons & ~padPrev;
    padPrev = raw.buttons;
    rev = raw.rt;
    if (phase != OPEN || closeWanted) return;
    if (pressed & pad::bit(pad::B_LB)) touched(), setSlot(slot - 1);
    if (pressed & pad::bit(pad::B_RB)) touched(), setSlot(slot + 1);
    const pad::Stick s = pad::radialDeadzone(raw.rx, raw.ry, pad::deadzoneRadius(1));
    if (s.x == 0 && s.y == 0) return;
    touched(), ease.t = -1;
    // Full stick: a turn in ~3 s (2 rad/s, an estimate that keeps the car readable while it swings), the pitch at
    // half that; with LT held, up-down zooms (full stick: x4.5 a second, so the whole range in ~2 s).
    yaw -= s.x * 2.0 * dt;
    if (raw.lt > 0.5f) zoom = std::clamp(zoom * std::exp(s.y * 1.5 * dt), 0.15, 4.0);
    else pitch = std::clamp(pitch + s.y * 1.0 * dt, -0.1, 1.45);
}

ShowroomResult Showroom::update(double dt, Career& career, bool station) {
    ShowroomResult res;
    careerNow = &career, atStation = station;
    clock += dt;
    toastT = std::max(0.0, toastT - dt);
    if (fade.update(dt)) {  // black: swap the worlds
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
    if (phase == OPEN && fitWanted && !closeWanted) {
        const int v = core->vehicle();
        const ModSet& pick = core->pick();
        bool same = true, owned = true;
        int locked = -1;
        for (int k = 0; k < MOD_SLOTS; k++) {
            same = same && pick.pick[k] == core->fitted().pick[k];
            if (career_ && !career.owns(v, k, pick.pick[k]) && locked < 0) owned = false, locked = k;
        }
        if (same) requestClose();  // nothing to fit: back to driving
        else if (career_ && !atStation) say("DRIVE TO A REPAIR STATION TO FIT PARTS");
        else if (!owned) say(std::string("LOCKED: ") + modOptions(v, locked)[pick.pick[locked]].name);
        else {
            res.act = ShowroomResult::FIT, res.mods = pick, res.recal = recal;
            requestClose();
        }
    }
    fitWanted = false;
    if (closeWanted && phase == OPEN && fade.start()) phase = CLOSING, closeWanted = false, orbiting = false;
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

// Slot tabs, then the slot's options: the pick highlighted, the fitted one marked.
void Showroom::drawLeft(const ShowroomLayout& l) {
    static const char* const kTabs[MOD_SLOTS] = {"ENGINE", "SUSP", "TRANS", "WHEELS", "TIRES"};  // (modSlotName's, short enough for a tab)
    const float u = l.u, s = px(1.4f * u), big = px(2.0f * u), pad = 12 * u;
    const Rect& p = l.left;
    const int v = core->vehicle();
    panel(r, p);
    label(r, p.x + pad, p.y + 12 * u, big, vehicleCatalog()[v].name, kAmber[0], kAmber[1], kAmber[2]);
    const float gap = 3 * u, tw = (p.w - 2 * pad - (MOD_SLOTS - 1) * gap) / MOD_SLOTS, ty = p.y + 34 * u, th = 18 * u;
    for (int k = 0; k < MOD_SLOTS; k++) {
        const float x = p.x + pad + k * (tw + gap);
        const bool on = k == slot;
        r.rect(x, ty, tw, th, 1, 1, 1, on ? 0.12f : 0.04f);
        if (on) r.rect(x, ty + th - 2 * u, tw, 2 * u, kAmber[0], kAmber[1], kAmber[2], 1);
        float ts = s;
        while (ts > 1 && r.textWidth(kTabs[k], ts) > tw - 4 * u) ts -= 1;
        const float* c = on ? kAmber : kGrey;
        r.text(x + (tw - r.textWidth(kTabs[k], ts)) / 2, ty + (th - 7 * ts) / 2, ts, kTabs[k], c[0], c[1], c[2]);
        hits.push_back({x, ty, tw, th, [this, k] { setSlot(k); }});
    }
    const auto& opts = modOptions(v, slot);
    const int n = (int)opts.size(), pick = core->pick().pick[slot], fitted = core->fitted().pick[slot], rows = std::max(1, l.listRows);
    if (follow) listScroll = std::clamp(listScroll, pick - rows + 1, pick);
    listScroll = std::clamp(listScroll, 0, std::max(0, n - rows));
    const float top = p.y + 60 * u, rowH = 18 * u;
    for (int i = listScroll; i < n && i < listScroll + rows; i++) {
        const float y = top + (i - listScroll) * rowH, ty2 = y + (rowH - 2 * u - 7 * s) / 2;
        if (i == pick) r.rect(p.x + pad * 0.5f, y, p.w - pad, rowH - 2 * u, 1, 1, 1, 0.07f);
        float end = p.x + p.w - pad;
        if (i == fitted) {
            end -= r.textWidth("FITTED", s);
            r.text(end, ty2, s, "FITTED", kGrey[0], kGrey[1], kGrey[2]);
            end -= 8 * u;
        }
        std::string name = opts[i].name;
        const int room = std::max(3, (int)((end - p.x - pad) / (6 * s)));
        if ((int)name.size() > room) name = name.substr(0, room - 2) + "..";
        if (i == pick) r.text(p.x + pad, ty2, s, name, 1.0f, 0.8f, 0.35f);
        else r.text(p.x + pad, ty2, s, name, 0.9f, 0.9f, 0.92f);
        hits.push_back({p.x, y, p.w, rowH, [this, i] { core->setPick(slot, i); }});
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
    r.text(x, y, s, modSlotName(slot), kGrey[0], kGrey[1], kGrey[2]);
    y += lh;
    const ModOption& o = modOptions(v, slot)[pick.pick[slot]];
    y += wrap(r, x, y, w, big, 8 * big + 4 * u, o.name, kAmber);
    std::string what = o.detail;
    const bool have = !careerNow || careerNow->owns(v, slot, pick.pick[slot]);
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
    const bool change = !std::equal(pick.pick, pick.pick + MOD_SLOTS, core->fitted().pick);
    const std::string fit = pad::ui.last ? std::string(pad::buttonName(pad::B_A, pad::ui.style)) + "  FIT" : "ENTER  FIT";
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
    const std::string keys = pad::ui.last ? name(pad::B_LB) + " " + name(pad::B_RB) + " PART   D-PAD OPTION   RIGHT STICK ORBIT   " + "LT + RIGHT STICK ZOOM   " +
                                                name(pad::B_B) + " CLOSE"
                                          : "W S PART   A D OPTION   DRAG ORBIT   WHEEL ZOOM   C SPEEDO   ESC CLOSE";
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
