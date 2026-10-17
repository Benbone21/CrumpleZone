// The throttle limit (throttle.h): what W gives under a cap and a response, the settings lines,
// the in-drive key, the pad's scale, and a crawl up a ledge at 10% against full throttle.
// Pure logic and the real Sim::drive: no SDL or GL. Settings files go in build/, never the config dir.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/gamepad.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "../src/throttle.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

// W held for `seconds` at 60 Hz: the throttle after each frame.
std::vector<double> hold(const throttle::Tuning& t, double seconds, throttle::Pedal* p = nullptr) {
    throttle::Pedal own;
    throttle::Pedal& pe = p ? *p : own;
    std::vector<double> out;
    for (int k = 0; k < (int)std::lround(seconds * 60); k++) out.push_back(pe.step(true, t, Ctx::kFrame));
    return out;
}

// What a drive up to a ledge cost: the fastest it hit, the worst wheelspin, the throttle it ever asked for.
struct Crawl {
    double peakThrottle = 0, peakSpeed = 0, peakSpin = 0, x = 0, impact = 0;
    bool cleared = false;
};

Crawl ledgeDrive(Ctx& ctx, const throttle::Tuning& tune, double z, double H) {
    World& w = ctx.sim.world;
    const double faceX = -112;
    w.addBox(V3(faceX + 8, w.height(faceX + 8, z) + H / 2, z), V3(8, H / 2, 5), 0, 0, 0, V3(0.4, 0.38, 0.35));
    w.boxes.back().rock = true;
    w.buildGrid();
    ctx.sim.cars.clear();
    ctx.sim.player = 0;
    VehicleSpec& spec = *new VehicleSpec(bronco());  // (lives for the run)
    spec.lockerFront = true;
    Car& c = ctx.add(spec, faceX - 12, z, 0);
    ctx.run(1.0);
    c.setDriveMode((int)driveModes(spec.transfer).size() - 1);  // 4L
    c.setLocker(true, true);
    c.setLocker(false, true);
    c.tractionControl = false;
    c.tirePsi = 12;
    ctx.focus(c);
    throttle::Pedal pedal;
    Crawl r;
    ctx.run(30, [&] {
        ctx.sim.drive(true, false, false, false, false, Ctx::kFrame);  // W held, straight on
        c.throttle = pedal.step(c.throttle > 0.5, tune, Ctx::kFrame);
        r.peakThrottle = std::max(r.peakThrottle, c.throttle);
        const Frame fr = c.frame();
        const double v = dot(fr.vel, fr.fwd);
        if (fr.com.x < faceX - 1) r.impact = std::max(r.impact, v);  // (the run-up: the speed it hits the ledge at)
        r.peakSpeed = std::max(r.peakSpeed, v);
        for (const Wheel& wh : c.wheels)
            if (c.wheelPowered(wh) && wh.load > 0) r.peakSpin = std::max(r.peakSpin, wh.omega * wh.R - wh.groundSpeed);
    });
    r.x = c.frame().com.x;
    r.cleared = r.x > faceX + 4;
    return r;
}

}  // namespace

SCENARIO(throttlePedal, "throttle/pedal", "W under a cap: the throttle never passes the cap, the ramp reaches it in the response time, letting go is quick, and 100% INSTANT is the key's own 0 and 1") {
    using namespace throttle;
    ctx.check("defaults: 100%, INSTANT, keyboard only", [&] {
        const Tuning d;
        return d.cap == 1 && d.response == 0 && !d.padToo && d.isDefault() && d.padScale() == 1;
    }());
    {  // (the default is the key itself, so nothing that ran before moves)
        Tuning d;
        Pedal p;
        ctx.check("100% INSTANT: W is exactly 1, no W exactly 0", p.step(true, d, 1 / 60.0) == 1 && p.step(true, d, 1 / 60.0) == 1 && p.step(false, d, 1 / 60.0) == 0);
    }
    {
        Tuning t;
        t.cap = 0.1f;
        const std::vector<double> v = hold(t, 1.0);
        ctx.check("10% INSTANT: holding W gives the cap at once", std::fabs(v.front() - 0.1) < 1e-6);
        ctx.check("...and never more", *std::max_element(v.begin(), v.end()) <= (double)t.cap);
        Pedal p;
        hold(t, 0.5, &p);
        ctx.check("let go: nothing at the next frame (INSTANT)", p.step(false, t, 1 / 60.0) == 0);
    }
    for (int resp = 1; resp <= 2; resp++) {
        Tuning t;
        t.cap = 0.25f;
        t.response = resp;
        const double T = riseTime(resp);
        const std::vector<double> v = hold(t, T + 0.5);
        int reached = -1;
        for (int k = 0; k < (int)v.size(); k++)
            if (v[k] >= 0.25 - 1e-9) {
                reached = k;
                break;
            }
        const std::string tag = std::string("25% ") + kResponseNames[resp];
        ctx.metric(tag + ": seconds for W to reach the cap", (reached + 1) / 60.0, "s", T - 0.02, T + 0.02);
        ctx.check(tag + ": never past the cap", *std::max_element(v.begin(), v.end()) <= (double)t.cap);
        ctx.check(tag + ": starts gently (first frame under a tenth of the cap)", v.front() < 0.025);
        bool mono = true;
        for (size_t k = 1; k < v.size(); k++) mono &= v[k] >= v[k - 1];
        ctx.check(tag + ": rises steadily", mono);
        Pedal p;
        hold(t, T + 0.5, &p);
        int frames = 0;
        while (p.step(false, t, 1 / 60.0) > 0 && frames < 120) frames++;
        ctx.metric(tag + ": seconds to let go", (frames + 1) / 60.0, "s", 0.05, kFallTime + 0.04);
    }
    {  // (full power, but built up: SMOOTH at 100% is a ramp to 1)
        Tuning t;
        t.response = 1;
        const std::vector<double> v = hold(t, 1.0);
        ctx.check("100% SMOOTH: a ramp that ends at exactly 1", v.front() < 0.1 && std::fabs(v.back() - 1) < 1e-9);
    }
}

SCENARIO(throttleKey, "throttle/key", "The in-drive key cycles 100, 50, 25, 10 % and back; it is a rebindable action that is free by default; the pad scale follows the toggle") {
    using namespace throttle;
    Tuning t;
    float seen[6];
    for (int k = 0; k < 6; k++) seen[k] = t.cap, t.cyclePreset();
    ctx.check("100 -> 50 -> 25 -> 10 -> 100 -> 50", seen[0] == 1 && seen[1] == 0.5f && seen[2] == 0.25f && seen[3] == 0.1f && seen[4] == 1 && seen[5] == 0.5f);
    t.cap = 0.4f;
    t.cyclePreset();
    ctx.check("from an in-between setting (40%) the key goes down to the next preset (25%)", t.cap == 0.25f);
    t.cap = 0.05f;
    t.cyclePreset();
    ctx.check("from the bottom (5%) it goes back to 100%", t.cap == 1);
    ctx.check("percent text", percent(0.25f) == "25%" && percent(1) == "100%" && percent(0.05f) == "5%");

    t.cap = 0.3f;
    ctx.check("the pad is not limited unless it is switched on", t.padScale() == 1);
    t.padToo = true;
    ctx.check("...with APPLIES TO PAD the pad is scaled by the cap", std::fabs(t.padScale() - 0.3f) < 1e-6);

    const Keybinds kb;
    ctx.check("the key is 8, not used by any other action", kb.key(KA_THROTTLE_CAP) == SC_8 && kb.actionFor(SC_8) == KA_THROTTLE_CAP);
    ctx.check("a press of 8 reaches the switch as 8", kb.gameKey(SC_8, false) == SC_8);
    Keybinds moved;
    ctx.check("rebound to F7: F7 is the limit key, 8 is free and quiet", moved.bind(KA_THROTTLE_CAP, 64) && moved.gameKey(64, false) == SC_8 && moved.gameKey(SC_8, false) == 0);
    ctx.check("the label and the settings name", std::strcmp(actionLabel(KA_THROTTLE_CAP), "THROTTLE LIMIT") == 0 && std::strcmp(settingsName(KA_THROTTLE_CAP), "key_throttlecap") == 0);
    Keybinds old;  // a settings file from before the key, with 8 given to the handbrake
    old.loadSetting("key_handbrake", SC_8);
    old.sanitize();
    ctx.check("an old file that put 8 elsewhere keeps its bindings; the new action moves aside", old.key(KA_HANDBRAKE) == SC_8 && old.key(KA_THROTTLE_CAP) != SC_8 && bindable(old.key(KA_THROTTLE_CAP)) && old.key(KA_THROTTLE) == SC_W);
}

SCENARIO(throttleSettings, "throttle/settings", "The throttle lines round-trip through a settings file; a file without them means 100% INSTANT; bad values are clamped") {
    using namespace throttle;
    const char* path = "build/test-throttle-settings.txt";  // (a scratch file: never the config dir)
    Tuning a;
    a.cap = 0.15f;
    a.response = 2;
    a.padToo = true;
    Keybinds ka;
    ka.bind(KA_THROTTLE_CAP, SC_K);
    {
        FILE* f = std::fopen(path, "w");
        ctx.check("scratch file written", f != nullptr);
        if (!f) return;
        std::fputs("mph 1\nvolume 0.50\n", f);
        std::fputs(a.saveText().c_str(), f);
        for (int k = 0; k < KA_COUNT; k++) std::fprintf(f, "%s %d\n", settingsName((KeyAction)k), ka.saveValue((KeyAction)k));
        std::fclose(f);
    }
    auto load = [&](const char* file, Tuning& t, Keybinds& kb) {
        FILE* f = std::fopen(file, "r");
        if (!f) return false;
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            char key[64];
            double v = 0;
            if (std::sscanf(line, "%63s %lf", key, &v) != 2) continue;
            if (!t.loadSetting(key, v)) kb.loadSetting(key, (int)v);
        }
        std::fclose(f);
        kb.sanitize();
        return true;
    };
    Tuning b;
    Keybinds kb;
    ctx.check("loaded", load(path, b, kb));
    ctx.check("cap, response and pad toggle survive the round trip", std::fabs(b.cap - 0.15f) < 1e-6 && b.response == 2 && b.padToo);
    ctx.check("the limit key survives it too", kb.key(KA_THROTTLE_CAP) == SC_K);
    std::remove(path);

    {  // (the player's file from before this setting)
        FILE* f = std::fopen(path, "w");
        std::fputs("mph 0\nvolume 0.80\nkey_throttle 26\n", f);
        std::fclose(f);
        Tuning old;
        Keybinds okb;
        load(path, old, okb);
        std::remove(path);
        ctx.check("missing lines: 100%, INSTANT, keyboard only, the key on 8", old.isDefault() && okb.key(KA_THROTTLE_CAP) == SC_8);
    }
    Tuning t;
    t.loadSetting("throttlecap", 0);
    ctx.check("0% is clamped to the 5% floor, so the car can always move", std::fabs(t.cap - 0.05f) < 1e-6);
    t.loadSetting("throttlecap", 900);
    ctx.check("900% is clamped to 100%", t.cap == 1);
    t.loadSetting("throttlecap", std::nan(""));
    ctx.check("not a number: still a usable cap", t.cap >= 0.05f && t.cap <= 1);
    t.loadSetting("throttleresp", 9);
    ctx.check("response out of range is clamped", t.response == 2);
    t.loadSetting("throttleresp", -3);
    ctx.check("...both ways", t.response == 0);
    ctx.check("other names are not ours", !t.loadSetting("padsteer", 1) && !t.loadSetting("key_throttle", 26));
    ctx.check("default saves as 100 / 0 / 0", Tuning().saveText() == "throttlecap 100\nthrottleresp 0\nthrottlepad 0\n");
}

SCENARIO(throttlePad, "throttle/pad", "The pad's trigger is scaled by the cap only when asked; the keyboard path and the autopilot are not touched by pad logic") {
    pad::Logic logic;
    pad::Raw raw;
    raw.rt = 1;
    ctx.sim.cars.clear();
    ctx.sim.player = 0;
    Car& c = ctx.add(bronco(), -100, 0, 0);
    ctx.run(0.5);
    c.gear = 1;
    auto frame = [&](float scale) {
        logic.tune.throttleScale = scale;
        logic.update(raw, pad::Mode::Drive, Ctx::kFrame);
        ctx.sim.drive(false, false, false, false, false, Ctx::kFrame);
        logic.apply(c, Ctx::kFrame);
        return c.throttle;
    };
    const double full = frame(1);
    ctx.check("full trigger, no limit on the pad: full throttle", full > 0.95);
    const double part = frame(0.25f);
    ctx.check("full trigger with the limit applying to the pad (25%): a quarter", std::fabs(part - 0.25 * full) < 0.02);
    raw.rt = 0.5f;
    const double half = frame(1), halfCap = frame(0.25f);
    ctx.check("half trigger keeps its analog shape, scaled", halfCap < half && std::fabs(halfCap - 0.25 * half) < 0.02);
}

SCENARIO(throttleCrawl, "throttle/crawl", "Bronco in 4L with lockers, W held at a 0.3 m ledge: at 100% it rushes it with wheelspin; at 10% (GENTLE) the throttle stays under 0.1 and it crawls over with less speed and spin") {
    throttle::Tuning full, low;
    low.cap = 0.1f;
    low.response = 2;
    const Crawl a = ledgeDrive(ctx, full, -55, 0.3);
    const Crawl b = ledgeDrive(ctx, low, -30, 0.3);
    ctx.check("100%: W is the whole throttle", a.peakThrottle > 0.99);
    ctx.check("10%: holding W never asks for more than 0.1", b.peakThrottle <= (double)low.cap, "peak " + std::to_string(b.peakThrottle));
    ctx.check("10%: still gets over a 0.3 m ledge", b.cleared, "x = " + std::to_string(b.x));
    ctx.check("10%: crawls (the run-up speed stays under 2 m/s)", b.impact < 2.0, "impact " + std::to_string(b.impact));
    ctx.check("10%: hits the ledge slower than at 100%", b.impact < a.impact * 0.7, "100%: " + std::to_string(a.impact) + ", 10%: " + std::to_string(b.impact));
    ctx.check("10%: less wheelspin than at 100%", b.peakSpin < a.peakSpin, "100%: " + std::to_string(a.peakSpin) + ", 10%: " + std::to_string(b.peakSpin));
}
