// Game controller: the shaping, the speed-sensitive steering, the trigger pedals, the button
// bindings, and a Bronco driven by synthetic pad input (a lane change at 80 km/h, a 4L crawl).
// No SDL here: the device is gamepad_sdl.cpp; this is the logic in gamepad.cpp.
#include <cmath>
#include <cstdio>

#include "../src/gamepad.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

bool near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }

bool hasTap(const pad::Logic& l, int sc, bool shift = false) {
    for (const pad::Tap& t : l.taps)
        if (t.scancode == sc && t.shift == shift) return true;
    return false;
}

// One frame of the pad on a car: the logic, the keyboard path (nothing held), then the pad on top.
void padFrame(Ctx& ctx, pad::Logic& logic, Car& c, const pad::Raw& raw) {
    logic.update(raw, pad::Mode::Drive, Ctx::kFrame);
    ctx.sim.drive(false, false, false, false, logic.in.handbrake, Ctx::kFrame);
    logic.apply(c, Ctx::kFrame);
}

}  // namespace

SCENARIO(padShaping, "gamepad/shaping", "Radial deadzone (0.12), response curve, trigger shaping and the deadzone / steering tables") {
    using namespace pad;
    ctx.check("inside the deadzone is nothing", radialDeadzone(0.11f, 0, 0.12f).x == 0 && radialDeadzone(0.08f, 0.08f, 0.12f).y == 0);
    ctx.check("the rim is 1", near(radialDeadzone(1, 0, 0.12f).x, 1) && near(radialDeadzone(0, -1, 0.12f).y, -1));
    ctx.check("the middle of the live range is half", near(radialDeadzone(0.56f, 0, 0.12f).x, 0.5));
    {  // (a diagonal keeps its direction, and a corner past the rim still reads 1 at most)
        const Stick d = radialDeadzone(0.6f, 0.3f, 0.12f);
        ctx.check("direction is kept", near(d.y / d.x, 0.5, 1e-3));
        const Stick c = radialDeadzone(1, 1, 0.12f);
        ctx.check("a corner never passes 1", std::hypot(c.x, c.y) <= 1.0001);
    }
    ctx.check("expo keeps the sign and the rim", near(expoCurve(-1, 1.4), -1) && expoCurve(-0.5f, 1.4) < 0 && near(expoCurve(0.5f, 1.4), std::pow(0.5, 1.4)));
    ctx.check("SOFT is flatter than SHARP near the middle", expoCurve(0.4f, steerExponent(0)) < expoCurve(0.4f, steerExponent(2)));
    ctx.check("a light touch on a trigger is nothing", triggerShape(0.04f) == 0);
    ctx.check("all the way is 1", near(triggerShape(1.0f), 1) && near(triggerShape(0.97f), 1));
    ctx.check("finer at the bottom (a crawl)", triggerShape(0.5f) < 0.5f && triggerShape(0.5f) > 0.3f);
    ctx.check("MEDIUM deadzone is 0.12", near(deadzoneRadius(1), 0.12) && deadzoneRadius(0) < deadzoneRadius(1) && deadzoneRadius(1) < deadzoneRadius(2));
    ctx.check("Xbox and PlayStation names", styleFromName("Xbox 360 Controller") == Style::Xbox && styleFromName("PS5 Controller") == Style::PlayStation &&
                                                styleFromName("Sony DualSense") == Style::PlayStation);
    ctx.check("glyphs", std::string(buttonName(B_A, Style::Xbox)) == "A" && std::string(buttonName(B_A, Style::PlayStation)) == "CROSS");
}

SCENARIO(padSteerRatio, "gamepad/steer-ratio", "Full stick is full lock at a crawl and a few degrees at 130 km/h (what the tires hold), shrinking with speed") {
    using namespace pad;
    const double wb = 3.0, aLat = 7.0;  // (Bronco-like: 3 m wheelbase, ~0.7 g of usable grip)
    double prev = 2;
    bool falling = true;
    for (double kmh : {0.0, 5.0, 10.0, 30.0, 60.0, 80.0, 100.0, 130.0, 180.0}) {
        const double r = steerRatio(steerLimit(wb, aLat, kmh / 3.6), steerGain(1));
        falling = falling && r <= prev + 1e-9;
        prev = r;
        ctx.sample("Full-stick lock", "share of full lock", r, "");
    }
    ctx.check("the ratio only falls as speed climbs", falling);
    ctx.metric("full stick at 5 km/h (full lock)", steerRatio(steerLimit(wb, aLat, 5 / 3.6), steerGain(1)), "x", 0.999, 1.0);
    const double at130 = steerRatio(steerLimit(wb, aLat, 130 / 3.6), steerGain(1)) * kMaxLock * 180 / M_PI;
    ctx.metric("full stick at 130 km/h", at130, "deg of road wheel", 2.5, 6.0);
    const double at80 = steerRatio(steerLimit(wb, aLat, 80 / 3.6), steerGain(1)) * kMaxLock * 180 / M_PI;
    ctx.metric("full stick at 80 km/h", at80, "deg of road wheel", 4.0, 9.0);
    ctx.check("a sliding car gets room to catch it", steerLimit(wb, aLat, 36, 0.1) > steerLimit(wb, aLat, 36, 0) + 0.09);
}

SCENARIO(padPedals, "gamepad/pedals", "RT is throttle, LT brake; LT at a stop reverses, RT while rolling backwards brakes then drives, as W and S do") {
    using namespace pad;
    Pedals p = pedals(0.6, 0, 0, 1);
    ctx.check("RT alone is throttle", near(p.throttle, 0.6) && p.brake == 0 && p.gear == 1);
    p = pedals(0, 0.4, 10, 1);
    ctx.check("LT rolling forward brakes, by how far it's pulled", near(p.brake, 0.4) && p.throttle == 0 && p.gear == 1);
    p = pedals(0, 0.7, 0.2, 1);
    ctx.check("LT at a stop selects reverse and drives backwards", p.gear == -1 && near(p.throttle, 0.7) && p.brake == 0);
    p = pedals(0, 0.5, -3, -1);
    ctx.check("LT in reverse stays the gas", p.gear == -1 && near(p.throttle, 0.5) && p.brake == 0);
    p = pedals(0.5, 0, -3, -1);
    ctx.check("RT while rolling backwards brakes", p.gear == -1 && p.throttle == 0 && near(p.brake, 0.5));
    p = pedals(0.5, 0, -0.2, -1);
    ctx.check("RT at a stop in reverse selects drive", p.gear == 1 && near(p.throttle, 0.5));
    p = pedals(0.8, 0.8, 0, 1);
    ctx.check("both at a stop: brakes held, no reverse", p.gear == 1 && near(p.brake, 0.8));
}

SCENARIO(padBindings, "gamepad/bindings", "Button presses become the keys they stand for; LB is the second layer; menus repeat; the pad is dead when told to be") {
    using namespace pad;
    Logic l;
    Raw r;
    auto press = [&](Button b, Mode c = Mode::Drive, uint32_t hold = 0) {
        r = Raw();
        l.update(r, c, 0.016);  // (released)
        r.buttons = hold | bit(b);
        l.update(r, c, 0.016);
    };
    press(B_X);
    ctx.check("X shifts down (Q)", hasTap(l, SC_Q) && l.taps.size() == 1);
    press(B_Y);
    ctx.check("Y shifts up (E)", hasTap(l, SC_E));
    press(B_START);
    ctx.check("Start is Esc (the menu)", hasTap(l, SC_ESCAPE));
    press(B_BACK);
    ctx.check("Back is the map (M)", hasTap(l, SC_M));
    press(B_UP);
    ctx.check("D-pad up is the transfer case (L)", hasTap(l, SC_L));
    press(B_LEFT);
    ctx.check("D-pad left is the front locker (F)", hasTap(l, SC_F));
    press(B_RIGHT);
    ctx.check("D-pad right is the rear locker (B)", hasTap(l, SC_B));
    press(B_DOWN);
    ctx.check("D-pad down is the sway bar (Y)", hasTap(l, SC_Y));
    press(B_RB);
    ctx.check("RB cycles the camera (C)", hasTap(l, SC_C));
    press(B_LSTICK);
    ctx.check("L3 is the lights (Shift+L)", hasTap(l, SC_L, true));
    press(B_A, Mode::Drive, bit(B_LB));
    ctx.check("LB + A is repair (R)", hasTap(l, SC_R) && !l.in.handbrake);
    press(B_UP, Mode::Drive, bit(B_LB));
    ctx.check("LB + D-pad up hooks the winch (V)", hasTap(l, SC_V) && !hasTap(l, SC_L));
    press(B_DOWN, Mode::Drive, bit(B_LB));
    ctx.check("LB + D-pad down is the tow strap (O)", hasTap(l, SC_O));
    press(B_Y, Mode::Drive, bit(B_LB));
    ctx.check("LB + Y gets out (J)", hasTap(l, SC_J));
    press(B_RB, Mode::Drive, bit(B_LB));
    ctx.check("LB + RB is manual / automatic (Shift+M)", hasTap(l, SC_M, true));
    // Held controls.
    r = Raw();
    r.buttons = bit(B_A);
    l.update(r, Mode::Drive, 0.016);
    ctx.check("A holds the handbrake", l.in.handbrake);
    r.buttons = bit(B_LB) | bit(B_LEFT);
    l.update(r, Mode::Drive, 0.016);
    ctx.check("LB + D-pad left lets the air out", l.in.airing == -1);
    r = Raw();
    r.buttons = bit(B_LB);
    r.rt = 1;
    l.update(r, Mode::Drive, 0.016);
    ctx.check("LB + RT reels the winch in and isn't throttle", l.in.winch == 1 && l.in.throttle == 0);
    r = Raw();
    r.lx = -1;
    r.rt = 1;
    l.update(r, Mode::Drive, 0.016);
    ctx.check("stick left steers left (+), RT is throttle", l.in.steerTarget > 0.99 && near(l.in.throttle, 1));
    r = Raw();
    r.lx = 0.1f;
    r.ly = 0.05f;
    l.update(r, Mode::Drive, 0.016);
    ctx.check("a stick at rest is nothing", !l.in.steerStick && l.in.steerTarget == 0);
    // Menus: D-pad moves, holding repeats; A selects; B backs out.
    press(B_DOWN, Mode::Menu);
    ctx.check("menu: D-pad down is Down", hasTap(l, SC_DOWN) && !hasTap(l, SC_Y));
    int repeats = 0;
    r = Raw();
    r.buttons = bit(B_DOWN);
    for (int k = 0; k < 90; k++) {  // (1.5 s held)
        l.update(r, Mode::Menu, 1.0 / 60);
        repeats += (int)l.taps.size();
    }
    ctx.check("menu: holding repeats after a pause", repeats >= 8 && repeats <= 16);
    press(B_A, Mode::Menu);
    ctx.check("menu: A is Enter", hasTap(l, SC_RETURN));
    press(B_B, Mode::Menu);
    ctx.check("menu: B is Esc", hasTap(l, SC_ESCAPE));
    press(B_A, Mode::Aim);
    ctx.check("winch aim: A hooks (V)", hasTap(l, SC_V));
    press(B_RIGHT, Mode::Aim);
    ctx.check("recovery picker does not cycle or switch vehicles with D-pad right", l.taps.empty());
    press(B_A, Mode::Map);
    ctx.check("map: A sets the waypoint", l.in.mapSelect);
    press(B_X, Mode::Off);
    ctx.check("off (editor, chat): nothing", l.taps.empty());
}

SCENARIO(padLaneChange, "gamepad/lane-change-80", "A Bronco on the pad (RT holds 80 km/h, a smooth stick lane change left then back) stays stable and ends up a lane over") {
    pad::Logic logic;
    Car& c = ctx.add(bronco(), -125, -50, 0);
    ctx.sim.player = (int)(&c - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 80 / 3.6);
    const double z0 = c.frame().com.z;
    double slip = 0, steerMax = 0, latMax = 0, rollMax = 0;
    V3 prevVel = c.frame().vel;
    const double t0 = ctx.t;
    double smooth = 0;
    ctx.run(8.0, [&] {
        const double t = ctx.t - t0;
        const Frame f = c.frame();
        pad::Raw raw;
        // Hold the speed on the right trigger (a thumb does).
        raw.rt = (float)clampd(0.18 + (80 / 3.6 - dot(f.vel, f.fwd)) * 0.25, 0, 1);
        // Left for 1.6 s (a half sine), then back the other way for 1.6 s.
        const double ph = t - 0.5;
        if (ph > 0 && ph < 3.2) raw.lx = (float)(-0.4 * std::sin(2 * M_PI * ph / 3.2));
        padFrame(ctx, logic, c, raw);
        const V3 acc = (f.vel - prevVel) / Ctx::kFrame;
        prevVel = f.vel;
        const V3 side = norm(cross(V3(0, 1, 0), norm(f.vel + V3(1e-9, 0, 0))));
        smooth += (std::fabs(dot(acc, side)) / 9.81 - smooth) * 0.1;
        latMax = std::max(latMax, smooth);
        slip = std::max(slip, std::fabs(std::atan2(dot(f.vel, f.left), std::max(0.1, dot(f.vel, f.fwd)))) * 180 / M_PI);
        steerMax = std::max(steerMax, std::fabs(c.steer) * 180 / M_PI);
        rollMax = std::max(rollMax, std::acos(clampd(f.up.y, -1, 1)) * 180 / M_PI);
        ctx.sample("Pad lane change", "steer", c.steer * 180 / M_PI, "deg");
        ctx.sample("Pad lane change", "stick", -raw.lx * 10, "x10");
    });
    const Frame f = c.frame();
    const double yawErr = std::fabs(std::atan2(f.fwd.z, f.fwd.x)) * 180 / M_PI;
    ctx.metric("worst body sideslip", slip, "deg", 0, 4.0);
    ctx.metric("peak lateral acceleration", latMax, "g", 0.02, 0.6);
    ctx.metric("peak road-wheel angle (a few degrees at 80 km/h)", steerMax, "deg", 0.5, 8.0);
    ctx.metric("body roll", rollMax, "deg", 0, 8);
    ctx.metric("lane gained, to the left (-z)", std::fabs(f.com.z - z0), "m", 0.3, 8.0);
    ctx.metric("heading back along the road", yawErr, "deg", 0, 6);
    ctx.check("still doing about 80 km/h", dot(f.vel, f.fwd) * 3.6 > 65);
    ctx.check("no damage", c.damage < 0.02);
}

SCENARIO(padCrawl, "gamepad/crawl-4l", "A Bronco in 4L on the pad: a light RT crawls at walking pace, a full stick is full lock, LT stops it, LT again reverses") {
    pad::Logic logic;
    Car& c = ctx.add(bronco(), -125, -50, 0);
    ctx.sim.player = (int)(&c - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(bronco().transfer).size() - 1);  // 4L
    ctx.check("in low range", c.mode() && c.mode()->low);
    c.tractionControl = false;
    // Phase 1: RT eased to hold a walking pace (a thumb does), stick straight.
    double vmax = 0;
    ctx.run(10.0, [&] {
        pad::Raw raw;
        raw.rt = (float)clampd(0.2 + (1.5 - dot(c.frame().vel, c.frame().fwd)) * 0.3, 0, 0.6);
        padFrame(ctx, logic, c, raw);
        vmax = std::max(vmax, dot(c.frame().vel, c.frame().fwd));
    });
    const double v1 = dot(c.frame().vel, c.frame().fwd);
    ctx.metric("crawl speed held on a light trigger", v1 * 3.6, "km/h", 3.0, 8.0);
    ctx.metric("never ran away", vmax * 3.6, "km/h", 0, 12.0);
    // Phase 2: full stick left at a crawl is full lock.
    double lock = 0;
    ctx.run(1.5, [&] {
        pad::Raw raw;
        raw.rt = 0.3f;
        raw.lx = -1;
        padFrame(ctx, logic, c, raw);
        lock = std::max(lock, c.steer);
    });
    ctx.metric("full stick at a crawl", lock, "rad", 0.55, 0.61);
    ctx.run(1.5, [&] {
        pad::Raw raw;
        raw.rt = 0.3f;
        padFrame(ctx, logic, c, raw);
    });
    ctx.metric("stick let go: the wheel centres", std::fabs(c.steer), "rad", 0, 0.03);
    // Phase 3: LT stops it, and held at a stop it reverses.
    ctx.run(6.0, [&] {
        pad::Raw raw;
        raw.lt = 0.6f;
        padFrame(ctx, logic, c, raw);
    });
    const double v3 = dot(c.frame().vel, c.frame().fwd);
    ctx.check("LT brought it to rest and then backwards", c.gear == -1 && v3 < -0.15, "gear " + std::to_string(c.gear) + " v " + std::to_string(v3));
    // Phase 4: RT while backing brakes, then drives forward again.
    ctx.run(8.0, [&] {
        pad::Raw raw;
        raw.rt = 0.6f;
        padFrame(ctx, logic, c, raw);
    });
    ctx.check("RT stopped the reverse and drove forward again", c.gear >= 1 && dot(c.frame().vel, c.frame().fwd) > 0.2);
    ctx.check("no damage crawling", c.damage < 0.02);
}

SCENARIO(padRumble, "gamepad/rumble", "Rumble stays quiet cruising, thumps on a wall hit, and is silent when switched off (more in rumble/)") {
    pad::Rumbler on, off;
    Car& c = ctx.add(bronco(), 70, 0, 0);  // the crash wall is at x = 114
    ctx.sim.player = (int)(&c - ctx.sim.cars.data());
    ctx.focus(c);
    ctx.run(1.0);
    ctx.launch(c, 14);
    double cruise = 0, hit = 0, silent = 0;
    ctx.run(4.0, [&] {
        const pad::RumbleOut a = on.update(c, 2, Ctx::kFrame, c.impactEnergy), b = off.update(c, 0, Ctx::kFrame, c.impactEnergy);
        c.impactEnergy = 0;
        silent = std::max(silent, (double)std::max(b.low, b.high));
        if (c.frame().com.x < 104) cruise = std::max(cruise, (double)std::max(a.low, a.high));
        else hit = std::max(hit, (double)a.low);
        ctx.sample("Rumble", "heavy motor", a.low, "");
    });
    ctx.check("it hit the wall", c.frame().com.x > 104);
    ctx.metric("cruising on pavement", cruise, "0..1", 0, 0.2);
    ctx.metric("the heavy motor on a wall hit", hit, "0..1", 0.3, 1.0);
    ctx.metric("switched off", silent, "0..1", 0, 0);
}
