// Winch payout: letting line out (Shift+Z, or LB + LT on a pad) on a tree and on a slope.
#include <cmath>
#include <utility>

#include "../src/gamepad.h"
#include "../src/keybinds.h"
#include "../src/sdl_min.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

Rope* lineOf(Ctx& ctx, int ci) {
    const int r = ctx.sim.ropeOn(ci, ROPE_WINCH);
    return r >= 0 ? &ctx.sim.ropes[r] : nullptr;
}

}  // namespace

SCENARIO(winchPayout, "recovery/winch-payout", "A Bronco on a pole lets line out at the drum's speed to all 60 m, reels back in, and backs away on a paid-out line") {
    // Proving-ground poles stand in a row at z = 38.
    Car& c = ctx.add(bronco(), 50, 16, -90);
    ctx.focus(c);
    ctx.run(1.0);
    const std::string msg = ctx.sim.hookWinch(0);
    ctx.check("winch hooks the pole ahead", lineOf(ctx, 0) && lineOf(ctx, 0)->b.kind == ANCHOR_TREE, msg);
    if (!lineOf(ctx, 0)) return;
    c.winchSpeed = 4;  // (the default preference)
    const double start = lineOf(ctx, 0)->length;
    ctx.run(3.0, [&] { ctx.sim.setWinch(0, -1); });
    const double paid = lineOf(ctx, 0)->length - start;
    ctx.metric("line paid out in 3 s at 4X", paid, "m", 2.5, 3.5);
    ctx.run(60, [&] { ctx.sim.setWinch(0, -1); });
    ctx.metric("all the line there is (winch line and its extension)", lineOf(ctx, 0)->length, "m", 59.9, 60.0);
    // Backing away on the slack line: it goes taut at the line's length, not before.
    const V3 p0 = c.frame().com;
    ctx.run(8, [&] { c.gear = -1; c.throttle = 0.5; c.brake = c.handbrake = 0; ctx.sim.setWinch(0, 0); });
    ctx.metric("backed away from the pole", len(c.frame().com - p0), "m", 4, 60);
    ctx.check("line still on", lineOf(ctx, 0) != nullptr);
    if (!lineOf(ctx, 0)) return;
    // Reel in, pay out, reel in: each way moves the line.
    c.throttle = 0; c.gear = 0;
    double l0 = lineOf(ctx, 0)->length;
    ctx.run(2, [&] { ctx.sim.setWinch(0, 1); });
    ctx.metric("reeled in after paying out", l0 - lineOf(ctx, 0)->length, "m", 0.5, 1.5);
    l0 = lineOf(ctx, 0)->length;
    ctx.run(0.5, [&] { ctx.sim.setWinch(0, -1); });
    ctx.metric("paid out again (switched straight over)", lineOf(ctx, 0)->length - l0, "m", 0.4, 0.6);
    l0 = lineOf(ctx, 0)->length;
    ctx.run(0.5, [&] { ctx.sim.setWinch(0, 1); });
    ctx.metric("and back in", l0 - lineOf(ctx, 0)->length, "m", 0.2, 0.3);
}

SCENARIO(winchLower, "recovery/winch-lower", "A Bronco winched up the slickrock dome hangs on its line, then pays out and rolls back down with it") {
    ctx.sim.world = World(MAP_MOUNTAIN);
    const V3 dome = mountainFeature("dome");
    World& w = ctx.sim.world;
    w.addRock(dome.x, dome.z + 2, V3(1.3, 1.1, 1.3), 1.0, 4242, 1);
    w.buildGrid();
    Car& c = ctx.add(bronco(), dome.x, dome.z - 27, -90);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.check("winch hooks the boulder", ctx.sim.hookWinch(0).rfind("WINCH HOOKED", 0) == 0);
    if (!lineOf(ctx, 0)) return;
    c.winchSpeed = 4;
    auto drive = [&](int winch) { ctx.sim.drive(false, false, false, false, false, Ctx::kFrame); ctx.sim.setWinch(0, winch); };
    ctx.run(12, [&] { drive(1); });
    ctx.run(2, [&] { drive(0); });
    const V3 p0 = c.frame().com;
    const double l0 = lineOf(ctx, 0)->length;
    ctx.run(4, [&] {
        drive(-1);
        ctx.sample("Line out", "line", lineOf(ctx, 0)->length, "m");
        ctx.sample("Line out", "hook to boulder", len(c.hookPoint(0) - lineOf(ctx, 0)->b.p), "m");
    });
    ctx.metric("line paid out in 4 s at 4X", lineOf(ctx, 0)->length - l0, "m", 3.5, 4.5);
    ctx.metric("rolled back down the dome with it", len(c.frame().com - p0), "m", 2.5, 5);
    ctx.metric("hook to boulder vs line out (it follows the line down)", len(c.hookPoint(0) - lineOf(ctx, 0)->b.p) - lineOf(ctx, 0)->length, "m", -1, 0.1);
}

SCENARIO(winchSlackDrawn, "recovery/winch-slack-drawn", "A paid-out line sags, and lies along the ground rather than through it; a loaded one runs straight") {
    Car& c = ctx.add(bronco(), 50, 16, -90);
    ctx.focus(c);
    ctx.run(1.0);
    ctx.sim.hookWinch(0);
    if (!ctx.check("winch hooks the pole ahead", lineOf(ctx, 0) != nullptr)) return;
    auto lowest = [&](double lift) {  // (lowest point of the line over the ground under it, and its sag below the chord)
        const std::vector<V3> p = ctx.sim.ropePath(*lineOf(ctx, 0), lift);
        double under = 1e9, sag = 0;
        for (size_t k = 0; k < p.size(); k++) {
            const double t = (double)k / (p.size() - 1);
            under = std::min(under, p[k].y - ctx.sim.world.height(p[k].x, p[k].z));
            sag = std::max(sag, (p.front().y + (p.back().y - p.front().y) * t) - p[k].y);
        }
        return std::make_pair(under, sag);
    };
    ctx.run(10, [&] { ctx.sim.setWinch(0, -1); });
    const auto slack = lowest(0.02);
    ctx.metric("slack line: sags below its ends", slack.second, "m", 0.3, 5);
    ctx.metric("slack line: never below the ground", slack.first, "m", 0.0, 1);
    ctx.check("backs away until the line comes taut", ctx.runUntil(20, [&] { return lineOf(ctx, 0)->tension > 2000; },
                                                                  [&] { c.gear = -1; c.throttle = 0.4; c.brake = c.handbrake = 0; ctx.sim.setWinch(0, 0); }));
    ctx.metric("loaded line (backed away against it): straight", lowest(0.02).second, "m", 0, 0.05);
}

SCENARIO(winchPayoutInput, "recovery/winch-payout-input", "Shift+Z and LB + LT pay the line out; Z and LB + RT reel it in; switching straight between them") {
    Keybinds kb;
    uint8_t keys[512] = {};
    auto keyWinch = [&](bool z, bool shift) {  // (main.cpp's read of the held keys)
        keys[SC_Z] = z;
        return kb.held(KA_WINCH_OUT, keys, shift) ? -1 : kb.held(KA_WINCH_IN, keys, shift) ? 1 : 0;
    };
    ctx.check("Shift+Z pays out", keyWinch(true, true) == -1);
    ctx.check("Z reels in", keyWinch(true, false) == 1);
    ctx.check("Shift alone holds", keyWinch(false, true) == 0);
    ctx.check("Shift+Z again: out", keyWinch(true, true) == -1);
    pad::Logic l;
    pad::Raw r;
    auto padWinch = [&](float rt, float lt) {
        r = pad::Raw();
        r.buttons = 1u << pad::B_LB;
        r.rt = rt, r.lt = lt;
        l.update(r, pad::Mode::Drive, 0.016);
        return l.in.winch;
    };
    ctx.check("LB + LT pays out, no brake or reverse", padWinch(0, 1) == -1 && l.in.brake == 0 && l.in.throttle == 0);
    ctx.check("LB + RT reels in", padWinch(1, 0) == 1);
    ctx.check("LB + LT again: out", padWinch(0, 1) == -1);
    ctx.check("LB alone holds", padWinch(0, 0) == 0);
}
