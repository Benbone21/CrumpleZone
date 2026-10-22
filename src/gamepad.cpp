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
    const double aMax = 9.81 * c.spec->tireGrip * c.spec->lateralGrip * grip * (1 + df / std::max(1.0, c.mass * 9.81)) * 0.85;
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
    auto P = [&](Button b) { return (pressed & bit(b)) != 0; };
    auto D = [&](Button b) { return (down & bit(b)) != 0; };
    auto tap = [&](int sc, bool shift = false) { taps.push_back({sc, shift}); };
    if (ctx == Mode::Off) {
        for (bool& d : dir_) d = false;
        return;
    }

    if (ctx == Mode::Drive) {
        const bool mod = D(B_LB);
        in.modifier = mod;
        if (mod) {  // LB held: the second layer (repairs, recovery, winch, strap, air)
            if (P(B_A)) tap(SC_R);  // twice quickly: back to the start
            if (P(B_B)) tap(SC_T);
            if (P(B_X)) tap(SC_X);
            if (P(B_Y)) tap(SC_J);
            if (P(B_RB)) tap(SC_M, true);
            if (P(B_LSTICK)) tap(SC_U);
            if (P(B_START)) tap(SC_H);
            if (P(B_BACK)) tap(SC_TAB);
            if (P(B_UP)) tap(SC_V);
            if (P(B_DOWN)) tap(SC_O);
            if (D(B_LEFT)) in.airing = -1;
            else if (D(B_RIGHT)) in.airing = 1;
            if (rt > 0.3f) in.winch = 1;
            else if (lt > 0.3f) in.winch = -1;
            // The right stick up and down sets the winch speed (a flick each).
            if (std::fabs(R.y) < 0.4f) flick_ = false;
            else if (!flick_ && std::fabs(R.y) > 0.8f) {
                flick_ = true;
                tap(R.y < 0 ? SC_EQUALS : SC_MINUS);
            }
        } else {
            if (P(B_START)) tap(SC_ESCAPE);
            if (P(B_BACK)) tap(SC_M);
            if (P(B_X)) tap(SC_Q);
            if (P(B_Y)) tap(SC_E);
            if (P(B_RB)) tap(SC_C);
            if (P(B_LSTICK) && !onFoot) tap(SC_L, true);
            in.crouchHeld = onFoot && D(B_LSTICK);
            if (P(B_UP)) tap(SC_L);
            if (P(B_LEFT)) tap(SC_F);
            if (P(B_RIGHT)) tap(SC_B);
            if (P(B_DOWN)) tap(SC_Y);
            in.throttle = rt;
            in.brake = lt;
            in.handbrake = D(B_A) || D(B_B);
            in.interact = D(B_X);  // (on foot; driving, X is the gear down tap above)
            in.drop = P(B_B);
            in.inventory = P(B_Y);  // (driving, Y is the gear up tap above)
            flick_ = false;
        }
        in.steerStick = std::fabs(L.x) > 0;
        in.steerTarget = -expoCurve(L.x, steerExponent(tune.steering));  // (stick right = steer right = negative)
        in.lookX = R.x;
        in.lookY = mod ? 0.0f : R.y;
        in.lookBack = D(B_RSTICK);
        in.resetView = (released & bit(B_RSTICK)) != 0;
        in.walkForward = -L.y;
        in.walkTurn = -L.x;
        return;
    }

    if (ctx == Mode::Map) {
        if (P(B_BACK)) tap(SC_M);
        if (P(B_B) || P(B_START)) tap(SC_ESCAPE);
        in.mapSelect = P(B_A);
        in.mapPanX = std::fabs(R.x) > std::fabs(L.x) ? R.x : L.x;
        in.mapPanY = std::fabs(R.y) > std::fabs(L.y) ? R.y : L.y;
        // Zoom: a step four times a second while a trigger is held.
        const int z = rt > 0.3f ? 1 : lt > 0.3f ? -1 : 0;
        if (z == 0) held_[0] = 0;
        else if ((held_[0] -= (float)dt) <= 0) held_[0] = 0.25f, in.mapZoom = z;
        return;
    }

    if (ctx == Mode::Aim) {  // point with the stick, A chooses the highlighted recovery point
        if (P(B_A)) tap(SC_V);
        if (P(B_B) || P(B_START)) tap(SC_ESCAPE);
        in.lookX = std::fabs(R.x) > std::fabs(L.x) ? R.x : L.x;
        in.lookY = std::fabs(R.y) > std::fabs(L.y) ? R.y : L.y;
        return;
    }

    // Menu: D-pad or left stick move (holding repeats), A selects, B / Start back out.
    static const int dirKey[4] = {SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT};
    const bool want[4] = {D(B_UP) || L.y < -0.5f, D(B_DOWN) || L.y > 0.5f, D(B_LEFT) || L.x < -0.5f, D(B_RIGHT) || L.x > 0.5f};
    for (int d = 0; d < 4; d++) {
        if (!want[d]) {
            dir_[d] = false;
            continue;
        }
        if (!dir_[d]) {
            dir_[d] = true;
            held_[d] = 0.4f;
            tap(dirKey[d]);
        } else if ((held_[d] -= (float)dt) <= 0) {
            held_[d] = 0.09f;
            tap(dirKey[d]);
        }
    }
    if (P(B_A)) tap(SC_RETURN);
    if (P(B_B) || P(B_START)) tap(SC_ESCAPE);
    if (P(B_Y)) tap(SC_B);  // (the garage: buy the part, at a station in a career)
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

std::vector<std::pair<std::string, std::string>> helpRows(Style s) {
    auto n = [&](Button b) { return std::string(buttonName(b, s)); };
    const std::string lb = n(B_LB), lt = s == Style::PlayStation ? "L2" : "LT", rt = s == Style::PlayStation ? "R2" : "RT";
    const std::string hold = lb + " + ";
    return {
        {"LEFT STICK", "STEER (FULL LOCK CRAWLING, GENTLE AT SPEED)"},
        {rt + " / " + lt, "THROTTLE / BRAKE (" + lt + " AT A STOP: REVERSE)"},
        {n(B_A) + " OR " + n(B_B), "HANDBRAKE"},
        {n(B_X) + " / " + n(B_Y), "SHIFT DOWN / UP (ON FOOT: HOLD " + n(B_X) + " TO WORK, " + n(B_Y) + " INVENTORY)"},
        {"RIGHT STICK", "LOOK AROUND (RECENTRES ON ITS OWN)"},
        {"R3 (HOLD)", "LOOK BACK (LET GO: RESET THE VIEW)"},
        {n(B_RB), "CAMERA: CHASE / ORBIT / COCKPIT / DRONE"},
        {"D-PAD UP", "4X4 MODE (2H 4H 4L)"},
        {"D-PAD LEFT / RIGHT", "FRONT / REAR LOCKER"},
        {"D-PAD DOWN", "FRONT SWAY BAR DISCONNECT"},
        {"L3", "HEADLIGHTS (ON FOOT: CROUCH)"},
        {n(B_BACK), "MAP (STICK PANS, " + rt + " / " + lt + " ZOOM, " + n(B_A) + " WAYPOINT)"},
        {n(B_START), "PAUSE MENU (D-PAD / STICK MOVE, " + n(B_A) + " SELECT, " + n(B_B) + " BACK)"},
        {hold + "D-PAD UP / DOWN", "WINCH HOOK / TOW STRAP"},
        {hold + rt + " / " + lt, "WINCH IN / OUT (RIGHT STICK UP DOWN: SPEED)"},
        {hold + "D-PAD LEFT / RIGHT", "AIR DOWN / PUMP UP (HOLD, STOPPED)"},
        {hold + n(B_A), "REPAIR (TWICE: BACK TO THE START)"},
        {hold + n(B_B), "BACK ON ITS WHEELS"},
        {hold + n(B_X), "TRACTION CONTROL"},
        {hold + n(B_Y), "GET OUT / IN"},
        {hold + n(B_RB), "MANUAL / AUTOMATIC GEARBOX"},
        {hold + "L3", "AUTOPILOT"},
        {hold + n(B_BACK), "NEXT VEHICLE"},
        {hold + n(B_START), "THIS CONTROLS LIST"},
    };
}

}  // namespace pad
