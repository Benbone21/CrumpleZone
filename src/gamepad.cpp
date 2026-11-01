// Game controller logic: shaping, steering, pedals, bindings, rumble levels, glyph names.
// No SDL calls and no GL here, so the test runner links it (the device is gamepad_sdl.cpp).
#include "gamepad.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "sdl_min.h"  // (scancodes only)
#include "softbody.h"

namespace pad {

Ui ui;

const char* const kSteeringNames[3] = {"SOFT", "NORMAL", "SHARP"};
const char* const kDeadzoneNames[3] = {"SMALL", "MEDIUM", "LARGE"};
const char* const kRumbleNames[3] = {"OFF", "LOW", "HIGH"};

float deadzoneRadius(int i) {
    static const float dz[3] = {0.07f, 0.12f, 0.18f};
    return dz[std::min(std::max(i, 0), 2)];
}
double steerExponent(int i) {
    static const double e[3] = {1.8, 1.4, 1.1};
    return e[std::min(std::max(i, 0), 2)];
}
double steerGain(int i) {
    static const double g[3] = {1.0, 1.2, 1.4};
    return g[std::min(std::max(i, 0), 2)];
}

// ---- Shaping --------------------------------------------------------------------------------

Stick radialDeadzone(float x, float y, float dz) {
    const float m = std::sqrt(x * x + y * y);
    if (m <= dz || m <= 1e-6f) return {};
    const float k = std::min(1.0f, (m - dz) / (1.0f - dz)) / m;
    return {x * k, y * k};
}

float expoCurve(float v, double exponent) {
    const float a = std::fabs(v);
    return std::copysign((float)std::pow(a, exponent), v);
}

float triggerShape(float raw, float dead, double gamma) {
    if (raw <= dead) return 0;
    const float v = std::min(1.0f, (raw - dead) / (0.97f - dead));  // (the last 3% is "all the way")
    return (float)std::pow(v, gamma);
}

// ---- Steering -------------------------------------------------------------------------------

double steerLimit(double wheelbase, double aLat, double speed, double catchLock) {
    // The road-wheel angle that makes aLat at this speed (a = v^2 tan(d) / L), plus ~2.6 degrees
    // for the tires' slip, plus the room to catch a slide. At parking speeds it's past full lock.
    return std::atan(wheelbase * aLat / std::max(1.0, speed * speed)) + 0.045 + catchLock;
}

double steerLimit(const Car& c, double wanted) {
    // (The same figure Sim::drive works out for the keyboard: ~85% of what the front tires hold
    // on this ground, downforce included.)
    const Frame fr = c.frame();
    const double speed = len(fr.vel), vf = dot(fr.vel, fr.fwd);
    double grip = 0;
    int n = 0;
    for (const Wheel& wh : c.wheels)
        if (wh.front && wh.load > 0) grip += surfaceGrip(wh.surface), n++;
    grip = n ? grip / n : 1.0;
    double df = c.spec->downforce * vf * vf;
    if (c.spec->downforceMax > 0) df = std::min(df, c.spec->downforceMax);
    const double aMax = 9.81 * c.spec->tireMu() * c.tempGrip() * c.spec->lateralGrip * grip * (1 + df / std::max(1.0, c.mass * 9.81)) * 0.85;
    // (Extra lock only when steering into a slide, to catch it: not to wind on more of the turn.)
    const double beta = std::atan2(dot(fr.vel, fr.left), std::max(1.0, std::fabs(vf)));
    const double catchLock = wanted * beta > 0 ? std::fabs(beta) : 0.0;
    return steerLimit(c.wheelbase, aMax, speed, catchLock);
}

double steerRatio(double limit, double gain) { return std::min(kMaxLock, limit * gain) / kMaxLock; }

// ---- Pedals ---------------------------------------------------------------------------------

Pedals pedals(double rt, double lt, double vf, int gear, bool manual) {
    Pedals p;
    p.gear = gear;
    if (manual || gear == 0) { p.throttle = rt; p.brake = lt; return p; }
    if (gear >= 1) {
        p.throttle = rt;
        if (lt > 0) {
            if (vf > 0.8 || rt > 0) p.brake = lt;  // rolling forward (or on both pedals): LT brakes
            else {                                 // stopped: LT is reverse
                p.gear = -1;
                p.throttle = lt;
            }
        }
    } else {
        p.throttle = lt;  // in reverse LT is the gas
        if (rt > 0) {
            if (vf < -0.8 || lt > 0) p.brake = rt;  // rolling backwards (or both): RT brakes
            else {
                p.gear = 1;
                p.throttle = rt;
            }
        }
    }
    return p;
}

// ---- Bindings -------------------------------------------------------------------------------

void Logic::update(const Raw& raw, Mode ctx, double dt) {
    taps.clear();
    presses.clear();
    const Keybinds& kb = binds ? *binds : defaultKeybinds();
    const uint32_t down = raw.buttons, pressed = down & ~prev_, released = prev_ & ~down;
    prev_ = down;
    in = Intent();
    const float dz = deadzoneRadius(tune.deadzone);
    const Stick L = radialDeadzone(raw.lx, raw.ly, dz), R = radialDeadzone(raw.rx, raw.ry, dz);
    const float rt = triggerShape(raw.rt), lt = triggerShape(raw.lt);
    const bool stickUse = std::fabs(L.x) + std::fabs(L.y) + std::fabs(R.x) + std::fabs(R.y) > 0.4f;
    in.anyInput = down != 0 || stickUse || rt > 0.2f || lt > 0.2f;
    // (The HUD switches to button names on a deliberate press, not a drifting stick.)
    if (pressed != 0 || rt > 0.3f || lt > 0.3f || std::hypot(L.x, L.y) > 0.6f || std::hypot(R.x, R.y) > 0.6f) ui.last = true;
    auto D = [&](Button b) { return (down & bit(b)) != 0; };
    auto tap = [&](int sc, bool shift, int action) { taps.push_back({sc, shift, action}); };

    // Every input's value now (the held actions read these: buttons 0 / 1, the triggers and stick directions
    // shaped) and the ones pressed this frame (a button; a trigger past half way; a stick pushed most of the way).
    float v[PI_COUNT] = {};
    for (int b = 0; b < B_COUNT; b++) v[b + 1] = D((Button)b) ? 1.0f : 0.0f;
    v[PI_LT] = lt, v[PI_RT] = rt;
    v[PI_LS_LEFT] = std::max(0.0f, -L.x), v[PI_LS_RIGHT] = std::max(0.0f, L.x), v[PI_LS_UP] = std::max(0.0f, -L.y), v[PI_LS_DOWN] = std::max(0.0f, L.y);
    v[PI_RS_LEFT] = std::max(0.0f, -R.x), v[PI_RS_RIGHT] = std::max(0.0f, R.x), v[PI_RS_UP] = std::max(0.0f, -R.y), v[PI_RS_DOWN] = std::max(0.0f, R.y);
    int fresh[PI_COUNT];
    int nFresh = 0;
    for (int b = 0; b < B_COUNT; b++)
        if (pressed & bit((Button)b)) fresh[nFresh++] = b + 1;
    for (int i = PI_LT; i < PI_COUNT; i++) {
        const bool trig = i == PI_LT || i == PI_RT;
        const bool now = analogOn_[i] ? v[i] > (trig ? 0.3f : 0.4f) : v[i] > (trig ? 0.5f : 0.75f);
        if (now && !analogOn_[i]) fresh[nFresh++] = i;
        analogOn_[i] = now;
    }
    // The layers: LB and VIEW held. Each is a modifier only while something is pressed with it; let go alone, it was a tap.
    const int mods = (D(B_LB) ? PM_LB : 0) | (D(B_BACK) ? PM_VIEW : 0);
    bool lbTap = false, viewTap = false;
    for (int k = 0; k < nFresh; k++) {
        const int i = fresh[k];
        if (i == PI_LB) lbClean_ = !D(B_BACK), viewClean_ = false;
        else if (i == PI_BACK) viewClean_ = !D(B_LB), lbClean_ = false;
        else lbClean_ = viewClean_ = false;
    }
    if (released & bit(B_LB)) lbTap = lbClean_, lbClean_ = false;
    if (released & bit(B_BACK)) viewTap = viewClean_, viewClean_ = false;
    if (ctx == Mode::Off) {
        for (bool& d : dir_) d = false;
        lbClean_ = viewClean_ = false;
        return;
    }
    for (int k = 0; k < nFresh; k++) {
        const int i = fresh[k];
        const bool isMod = i == PI_LB || i == PI_BACK;
        presses.push_back({i, isMod ? 0 : mods, isMod, false});
    }
    if (lbTap) presses.push_back({PI_LB, 0, false, true});
    if (viewTap) presses.push_back({PI_BACK, 0, false, true});

    if (ctx == Mode::Drive) {
        in.modifier = mods != 0;
        auto lookup = [&](int input, int m) {
            int a = kb.padActionFor(input, m, contexts, devKeys);
            if (a < 0 && fallback) a = kb.padActionFor(input, m, fallback, devKeys);
            return a;
        };
        auto tapAction = [&](int a) {
            if (a >= 0 && !isHeldAction((KeyAction)a)) tap(defaultKey((KeyAction)a), defaultShift((KeyAction)a), a);
        };
        for (int k = 0; k < nFresh; k++) {
            const int i = fresh[k];
            if (i == PI_LB || i == PI_BACK || i == PI_GUIDE) continue;  // (the layers answer when let go, below)
            if (i == PI_START && mods == 0) {  // (Start: the menu, always)
                tap(SC_ESCAPE, false, KA_MENU);
                continue;
            }
            tapAction(lookup(i, mods));
        }
        if (viewTap) tapAction(lookup(PI_BACK, 0));  // (VIEW alone: the map)
        if (lbTap) tapAction(lookup(PI_LB, 0));
        // The held actions, from whatever they are bound to.
        auto H = [&](KeyAction a) { return kb.padHeld(a, v, mods); };
        in.throttle = H(KA_THROTTLE);
        in.brake = H(KA_BRAKE);
        const float steer = H(KA_STEER_LEFT) - H(KA_STEER_RIGHT);
        in.steerStick = steer != 0;
        in.steerTarget = expoCurve(steer, steerExponent(tune.steering));
        in.handbrake = H(KA_HANDBRAKE) > 0.5f;
        in.horn = H(KA_HORN) > 0.5f;
        in.winch = H(KA_WINCH_IN) > 0.3f ? 1 : H(KA_WINCH_OUT) > 0.3f ? -1 : 0;
        in.airing = H(KA_AIR_DOWN) > 0.5f ? -1 : H(KA_AIR_UP) > 0.5f ? 1 : 0;
        in.interact = H(KA_INTERACT) > 0.5f;
        in.crouchHeld = H(KA_CROUCH) > 0.5f;
        in.hudAll = H(KA_HUD_ALL) > 0.5f;
        // The right stick looks round, but for the directions a chord with what's held takes (LB: the winch speed).
        const unsigned all = contexts | fallback;
        const bool chordX = kb.padChord(PI_RS_LEFT, mods, all) || kb.padChord(PI_RS_RIGHT, mods, all);
        const bool chordY = kb.padChord(PI_RS_UP, mods, all) || kb.padChord(PI_RS_DOWN, mods, all);
        in.lookX = chordX ? 0.0f : R.x;
        in.lookY = chordY ? 0.0f : R.y;
        in.lookBack = D(B_RSTICK) && mods == 0;
        in.resetView = (released & bit(B_RSTICK)) != 0 && mods == 0;
        in.walkForward = -L.y;
        in.walkTurn = -L.x;
        return;
    }

    auto H0 = [&](KeyAction a) { return kb.padHeld(a, v, 0); };
    if (ctx == Mode::Map) {
        for (int k = 0; k < nFresh; k++) {
            const int i = fresh[k];
            if (i == PI_B || i == PI_START) tap(SC_ESCAPE, false, KA_UI_BACK);
            else if (i == kb.pad[KA_MAP]) tap(defaultKey(KA_MAP), defaultShift(KA_MAP), KA_MAP);  // (the map's own button closes it)
            else if (i == PI_A || kb.padActionFor(i, 0, KCTX_MENU, false) == KA_UI_CONFIRM) in.mapSelect = true;
            else if (kb.padActionFor(i, 0, KCTX_MENU, false) == KA_UI_BACK) tap(SC_ESCAPE, false, KA_UI_BACK);
            else if (const int a = kb.padActionFor(i, 0, KCTX_MENU, false); a == KA_UI_SECOND || a == KA_UI_FACTORY) tap(uiCanon((KeyAction)a), false, a);  // (the pings)
        }
        in.mapPanX = (std::fabs(R.x) > std::fabs(L.x) ? R.x : L.x) + H0(KA_UI_RIGHT) - H0(KA_UI_LEFT);
        in.mapPanY = (std::fabs(R.y) > std::fabs(L.y) ? R.y : L.y) + H0(KA_UI_DOWN) - H0(KA_UI_UP);
        // Zoom: a step four times a second while it's held.
        const int z = H0(KA_UI_ZOOM_IN) > 0.3f ? 1 : H0(KA_UI_ZOOM_OUT) > 0.3f ? -1 : 0;
        if (z == 0) held_[0] = 0;
        else if ((held_[0] -= (float)dt) <= 0) held_[0] = 0.25f, in.mapZoom = z;
        return;
    }

    if (ctx == Mode::Aim) {  // point with the stick, A chooses the highlighted recovery point
        for (int k = 0; k < nFresh; k++) {
            if (fresh[k] == PI_A) tap(defaultKey(KA_WINCH), false, KA_WINCH);
            if (fresh[k] == PI_B || fresh[k] == PI_START) tap(SC_ESCAPE, false, KA_UI_BACK);
        }
        in.lookX = std::fabs(R.x) > std::fabs(L.x) ? R.x : L.x;
        in.lookY = std::fabs(R.y) > std::fabs(L.y) ? R.y : L.y;
        return;
    }

    // Menu: A is OK, B and Start back out, always; the rest as bound (the D-pad and the left stick move, holding
    // repeats; LB / RB the tabs...), each arriving as its menu key.
    static const KeyAction navAct[4] = {KA_UI_UP, KA_UI_DOWN, KA_UI_LEFT, KA_UI_RIGHT};
    for (int k = 0; k < nFresh; k++) {
        const int i = fresh[k];
        if (i == PI_GUIDE) continue;
        if (i == PI_A) {  // (in a dialog with a layer held, A is back: LB + A asked for the career repair, LB + A again leaves it, as R does)
            if ((menuContexts & KCTX_DIALOG) && mods) tap(SC_ESCAPE, false, KA_UI_BACK);
            else tap(SC_RETURN, false, KA_UI_CONFIRM);
            continue;
        }
        if (i == PI_B || i == PI_START) {
            tap(SC_ESCAPE, false, KA_UI_BACK);
            continue;
        }
        int a = -1;
        if (menuContexts & KCTX_DIALOG) a = kb.padActionFor(i, 0, KCTX_DIALOG, false);
        if (a < 0) a = kb.padActionFor(i, 0, KCTX_MENU, false);
        if (a < 0 || a == KA_UI_UP || a == KA_UI_DOWN || a == KA_UI_LEFT || a == KA_UI_RIGHT) continue;  // (the directions repeat, below)
        tap(uiCanon((KeyAction)a), false, a);
    }
    static const int dirKey[4] = {SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT};
    const bool want[4] = {H0(navAct[0]) > 0.5f || L.y < -0.5f, H0(navAct[1]) > 0.5f || L.y > 0.5f, H0(navAct[2]) > 0.5f || L.x < -0.5f, H0(navAct[3]) > 0.5f || L.x > 0.5f};
    for (int d = 0; d < 4; d++) {
        if (!want[d]) {
            dir_[d] = false;
            continue;
        }
        if (!dir_[d]) {
            dir_[d] = true;
            held_[d] = 0.4f;
            tap(dirKey[d], false, navAct[d]);
        } else if ((held_[d] -= (float)dt) <= 0) {
            held_[d] = 0.09f;
            tap(dirKey[d], false, navAct[d]);
        }
    }
    // (The garage: the right stick turns the model.)
    in.lookX = R.x, in.lookY = R.y;
}

bool Logic::apply(Car& c, double dt, bool steeringAssist) {
    bool did = false;
    // Steering: the stick sets a target between full lock left and right; the wheel follows it a
    // little quicker the slower you go (hands get gentler as the speed climbs), and lets go fast.
    const double speed = len(c.frame().vel);
    const double target = in.steerTarget;
    const double rate = (target == 0 || target * steerState < 0) ? 10.0 : 7.0 * std::min(1.0, 30.0 / std::max(1.0, speed)) + 3.0;
    steerState += clampd(target - steerState, -rate * dt, rate * dt);
    if (in.steerStick || std::fabs(steerState) > 1e-3) {
        // Full stick is the keyboard's full key (what the tires hold at this speed), times a
        // gain: full lock when crawling, a few degrees at 130 km/h; half stick is half of that.
        c.steerInput = steerState;
        if (steeringAssist) {
            c.steer = steerState * std::min(kMaxLock, steerLimit(c, steerState) * steerGain(tune.steering));
        } else {
            // Simulation mode: full lock at all speeds (kMaxLock = 0.6 rad = 34 degrees), matching
            // the keyboard's assist-off path in Sim::drive.
            c.steer = steerState * kMaxLock;
        }
        did = true;
    } else {
        steerState = 0;
    }
    if (in.throttle > 0 || in.brake > 0) {
        const Frame fr = c.frame();
        const Pedals p = pedals(in.throttle, in.brake, dot(fr.vel, fr.fwd), c.gear, c.manualShift);
        c.throttle = p.throttle * tune.throttleScale;
        c.brake = p.brake;
        c.gear = p.gear;
        did = true;
    }
    return did;
}

// ---- Rumble ---------------------------------------------------------------------------------

RumbleOut Rumbler::update(const Car& c, int level, double dt, double impactJ) {
    if (level <= 0 || dt <= 0 || dt > 0.25) {
        reset();
        return {};
    }
    // Parts breaking: a flag that wasn't set last frame. (A different or rebuilt car just sets the baseline.)
    std::vector<uint8_t> flags(c.wheels.size());
    int panels = 0;
    for (size_t i = 0; i < c.wheels.size(); i++) {
        const Wheel& w = c.wheels[i];
        flags[i] = (uint8_t)(w.shaftBroken | w.debeaded << 1 | w.popped << 2 | (!w.mounted || w.detached) << 3);
    }
    for (const Panel& p : c.panels) panels += p.state;
    if (car_ != &c || build_ != c.buildCount || flags_.size() != flags.size()) {
        car_ = &c, build_ = c.buildCount, flags_ = flags, panels_ = panels, chains_ = c.chainsSnapped, dead_ = c.engineDead;
        armed_ = true, bumpWin_ = 0;
    }
    for (size_t i = 0; i < flags.size(); i++) {
        const uint8_t fresh = flags[i] & ~flags_[i];
        if (fresh & 8) pulse(1.0, 0.8, 0.14, 2.5);       // the wheel came off
        else if (fresh & 1) pulse(0.9, 0.9, 0.12, 2.5);  // a shaft snapped
        else if (fresh & 2) pulse(0.7, 0.6, 0.1, 3);     // the tire rolled off its rim
        else if (fresh & 4) pulse(0.6, 0.5, 0.08, 3.5);  // a tire blew
    }
    if (panels > panels_) pulse(0.4, 0.35, 0.06, 4);
    if (c.chainsSnapped > chains_) pulse(0.45, 0.5, 0.06, 4);
    if (c.engineDead && !dead_) pulse(0.8, 0.6, 0.12, 2.5);
    flags_ = flags, panels_ = panels, chains_ = c.chainsSnapped, dead_ = c.engineDead;

    // A hit: scaled by how much it bent (a bump in the road bends nothing).
    if (impactJ > 100) {
        const double s = std::min(1.0, std::max(0.15, 0.25 + 0.375 * std::log10(impactJ / 150)));
        pulse(s, s * 0.5, 0.04 + 0.1 * s, 3);
    }

    // The suspension: how far into its bump stops the worst corner is (a landing, a big hit), and, smoothed, how fast it
    // is moving (washboard barely, rock and whoops properly).
    double depth = 0, rate = 0;
    bool rock = false;
    for (const Wheel& w : c.wheels) {
        rock |= w.load > 50 && (w.surface == SURF_ROCK || w.surface == SURF_SCREE);
        for (int bi : {w.spring, w.bumpStop}) {
            if (bi < 0) continue;
            const Beam& b = c.beams[bi];
            if (b.minL <= 0) continue;
            const V3 d = c.nodes[b.b].p - c.nodes[b.a].p;
            const double L = len(d);
            depth = std::max(depth, b.minL + 0.01 - L);
            if (bi == w.spring && L > 1e-6) rate = std::max(rate, std::fabs(dot(c.nodes[b.b].v - c.nodes[b.a].v, d / L)));
        }
    }
    if (depth < 0.01) armed_ = true;
    if (armed_ && depth > 0.022) armed_ = false, bumpWin_ = 0.1;  // (onto the stops: one pulse per bottoming-out)
    if (bumpWin_ > 0) {
        bumpWin_ -= dt;
        const double s = 0.35 + 0.65 * std::min(1.0, std::max(0.0, depth - 0.022) / 0.05);
        pulse(s, s * 0.6, 0.06, 3);
    }
    speed_ += (rate - speed_) * std::min(1.0, dt / 0.12);
    const double dead = rock ? 0.55 : 0.9;  // (rock is rough at a crawl; dirt only when it's really bad)
    if ((tickWait_ -= dt) <= 0 && speed_ > dead) {  // really rough ground: a light tick, a few a second at most
        high_.hit(0.2 + 0.25 * std::min(1.0, (speed_ - dead) / 1.6), 0.03, 10);
        tickWait_ = 0.3;
    }

    // The engine hitting its limiter: a tick per bounce, not a buzz.
    limiter_ = c.spec->redline > 0 && c.rpm > c.spec->redline * 0.985 && c.throttle > 0.5 ? limiter_ + dt : 0;
    if ((limitWait_ -= dt) <= 0 && limiter_ > 0.05) {
        high_.hit(0.35, 0.03, 10);
        limitWait_ = 0.3;
    }

    low_.step(dt), high_.step(dt);
    const double scale = level >= 2 ? 1.0 : 0.5;
    auto out = [&](float v) { return v > 0.02f ? (float)(scale * (0.3 + 0.7 * v)) : 0.0f; };  // (every pulse is strong enough to feel)
    return {out(low_.v), out(high_.v)};
}

// ---- Names ----------------------------------------------------------------------------------

Style styleFromName(const std::string& name) {
    std::string n;
    for (char ch : name) n += (char)std::toupper((unsigned char)ch);
    for (const char* key : {"PLAYSTATION", "PS3", "PS4", "PS5", "DUALSHOCK", "DUALSENSE", "SONY", "WIRELESS CONTROLLER"})
        if (n.find(key) != std::string::npos) return Style::PlayStation;
    return Style::Xbox;
}

const char* buttonName(Button b, Style s) {
    static const char* xbox[B_COUNT] = {"A", "B", "X", "Y", "VIEW", "HOME", "MENU", "L3", "R3", "LB", "RB", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"};
    static const char* ps[B_COUNT] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "TOUCHPAD", "PS", "OPTIONS", "L3", "R3", "L1", "R1", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"};
    return (s == Style::PlayStation ? ps : xbox)[b];
}

const char* hint(const char* keyboard, Button b) { return ui.last ? buttonName(b, ui.style) : keyboard; }

std::vector<std::pair<std::string, std::string>> helpRows(Style s, const Keybinds* binds) {
    const Keybinds& kb = binds ? *binds : defaultKeybinds();
    const bool ps = s == Style::PlayStation;
    auto n = [&](KeyAction a) { return kb.padName(a, ps); };
    auto two = [&](KeyAction a, KeyAction b) { return n(a) + " / " + n(b); };
    std::vector<std::pair<std::string, std::string>> rows = {
        {two(KA_THROTTLE, KA_BRAKE), "THROTTLE / BRAKE (" + n(KA_BRAKE) + " AT A STOP: REVERSE)"},
        {two(KA_STEER_LEFT, KA_STEER_RIGHT), "STEER (FULL LOCK CRAWLING, GENTLE AT SPEED)"},
        {"LEFT STICK", "WALK ON FOOT, FLY THE DRONE"},
        {"RIGHT STICK", "LOOK AROUND (R3 HELD: LOOK BACK)"},
        {n(KA_HANDBRAKE), "HANDBRAKE (ON FOOT: JUMP)"},
        {two(KA_GEAR_DOWN, KA_GEAR_UP), "SHIFT DOWN / UP"},
        {two(KA_INTERACT, KA_INVENTORY), "ON FOOT: HOLD TO WORK / INVENTORY"},
        {two(KA_PUT_DOWN, KA_FUEL_CAN), "ON FOOT: PUT DOWN / POUR THE FUEL CAN"},
        {n(KA_CROUCH), "ON FOOT: CROUCH"},
        {n(KA_CAMERA), "CAMERA: CHASE / ORBIT / COCKPIT / DRONE"},
        {n(KA_TRANSFER), "4X4 MODE (2H 4H 4L)"},
        {two(KA_LOCK_FRONT, KA_LOCK_REAR), "FRONT / REAR LOCKER"},
        {n(KA_SWAY), "FRONT SWAY BAR DISCONNECT"},
        {n(KA_LIGHTS), "HEADLIGHTS"},
        {n(KA_MAP), "MAP (TAP; HOLD IT FOR THE " + padInputName(PI_BACK, ps) + " LAYER)"},
        {n(KA_MENU), "PAUSE MENU"},
        {two(KA_WINCH, KA_STRAP), "WINCH HOOK / TOW STRAP"},
        {two(KA_WINCH_IN, KA_WINCH_OUT), "WINCH IN / OUT"},
        {two(KA_WINCH_FASTER, KA_WINCH_SLOWER), "WINCH SPEED UP / DOWN"},
        {two(KA_AIR_DOWN, KA_AIR_UP), "AIR DOWN / PUMP UP (HOLD, STOPPED)"},
        {n(KA_REPAIR), "REPAIR"},
        {n(KA_FLIP), "BACK ON ITS WHEELS"},
        {n(KA_TRACTION), "TRACTION CONTROL"},
        {n(KA_GET_IN_OUT), "GET OUT / IN"},
        {n(KA_MANUAL), "MANUAL / AUTOMATIC GEARBOX"},
        {two(KA_AUTOPILOT, KA_AUTOPILOT_PICK), "AUTOPILOT / WHERE TO"},
        {n(KA_HORN), "HORN (HOLD)"},
        {n(KA_PARK_BRAKE), "PARKING BRAKE"},
        {n(KA_STEER_ASSIST), "STEERING ASSIST"},
        {n(KA_THROTTLE_CAP), "THROTTLE LIMIT"},
        {n(KA_GARAGE), "GARAGE"},
        {n(KA_NEXT_VEHICLE), "NEXT VEHICLE"},
        {n(KA_JOBS), "JOBS ON OFFER HERE"},
        {two(KA_PHOTO, KA_HUD_ALL), "HIDE THE HUD / HOLD: THE FULL HUD"},
        {n(KA_CHAT), "CHAT"},
        {n(KA_HELP), "THIS CONTROLS LIST"},
    };
    return rows;
}
}  // namespace pad
