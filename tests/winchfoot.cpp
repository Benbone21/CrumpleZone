// Winch lines and tow straps are hooked up on foot: you can't do it from the seat. You grab the winch line at the fairlead (or a strap
// at a recovery point), walk it out (it pays out of the drum, as far as there is line), hook it at the anchor, and unhook it by hand at
// either end. Running the drum, once it's hooked, doesn't need you at it.
#include <cmath>
#include <cstdio>
#include <cstring>

#include <chrono>

#include "../src/anchors.h"
#include "../src/interact.h"
#include "../src/props.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

// Stands the walker at `at` (on the ground), looking along `dir` from head height.
void stand(Sim& sim, Walker& wk, const V3& at, const V3& dir, V3& eye, V3& look) {
    wk.active = true;
    wk.v = V3();
    wk.p = V3(at.x, sim.world.height(at.x, at.z), at.z);
    eye = wk.p + V3(0, 1.6, 0);
    look = norm(dir);
}

// Holds E for `seconds` at that spot; returns what it said last.
std::string holdE(Sim& sim, Interact& it, Walker& wk, const V3& eye, const V3& look, double seconds) {
    std::string said;
    for (double t = 0; t < seconds; t += 1.0 / 60) {
        const std::string s = it.update(sim, wk, eye, look, true, 1.0 / 60);
        if (!s.empty()) said = s;
    }
    it.update(sim, wk, eye, look, false, 1.0 / 60);  // (let go)
    return said;
}

}  // namespace

SCENARIO(winchOnFoot, "recovery/winch-on-foot", "The winch is hooked on foot: from the seat the hook-up is refused; on foot you hold E at the fairlead to grab the line, it pays out as you walk and stops at the end of the line, you hold E at a stake spot to hook it, and hold E at the anchor to unhook") {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_PROVING_GROUND);
    Car& c = ctx.add(bronco(), -60, -270, 0);  // open ground (anchors/counts)
    ctx.focus(c);
    ctx.run(0.5);
    Sim& sim = ctx.sim;
    const int ci = (int)(&c - sim.cars.data());
    sim.anchorStock.fill(anchors::kCareerStart);
    // From the seat: refused, nothing changes.
    const std::string seat = sim.seatHook(ci, ROPE_WINCH), seatStrap = sim.seatHook(ci, ROPE_STRAP);
    ctx.check("from the seat the winch hook-up is refused and says to get out", seat.rfind("GET OUT", 0) == 0, seat);
    ctx.check("...and so is the strap", seatStrap.rfind("GET OUT", 0) == 0, seatStrap);
    ctx.check("...and no line or grip was made", sim.ropes.empty() && !sim.handLine.on());
    // On foot, at the fairlead.
    Walker wk;
    Interact it;
    V3 eye, look;
    const V3 fl = c.hookPoint(0);
    stand(sim, wk, fl + V3(1.2, 0, 0.3), (fl + V3(0, 0.15, 0)) - (V3(fl.x + 1.2, sim.world.height(fl.x + 1.2, fl.z + 0.3) + 1.6, fl.z + 0.3)), eye, look);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the fairlead E offers the winch line", it.current.kind == IA_GRAB_LINE && it.current.lineKind == ROPE_WINCH, it.current.prompt);
    holdE(sim, it, wk, eye, look, 0.4);
    ctx.check("a short hold does nothing", !sim.handLine.on());
    const std::string got = holdE(sim, it, wk, eye, look, 1.3);
    ctx.check("holding E grabs the line", sim.handLine.on() && sim.handLine.car == ci && sim.handLine.kind == ROPE_WINCH && got.rfind("GOT THE WINCH LINE", 0) == 0, got);
    ctx.check("...which is no hooked line yet", sim.ropes.empty());
    // Walk it out as far as the legs go: the line runs out.
    double far = 0;
    wk.p = V3(fl.x + 2, sim.world.height(fl.x + 2, fl.z), fl.z);
    for (int i = 0; i < 60 * 25; i++) {
        wk.step(sim.world, sim, 1.0 / 60, 1, 0, true, false, 0);
        far = std::max(far, std::hypot(wk.p.x - fl.x, wk.p.z - fl.z));
    }
    ctx.check("the line pays out with you, and stops at its end", far > 40 && far <= sim.handLineReach(sim.handLine) + 0.05, std::to_string(far));
    ctx.check("...short of the line's whole length so the hook still reaches", sim.handLineReach(sim.handLine) < recovery::kWinchReach);
    // Back to a stake spot 10 m out: hold E to hook.
    const V3 spot = fl + V3(10, 0, 0);
    stand(sim, wk, spot, V3(1, -0.8, 0), eye, look);
    sim.handLine.hand = wk.p + V3(0, 1, 0);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("looking at the ground in reach offers to hook a stake", it.current.kind == IA_HOOK_LINE && it.current.end.kind == ANCHOR_STAKE, it.current.prompt);
    const int stakes = sim.anchorStock.n[GEAR_STAKE];
    const std::string hooked = holdE(sim, it, wk, eye, look, 2.0);
    ctx.check("holding E hooks it", hooked.rfind("WINCH HOOKED", 0) == 0 && sim.ropeOn(ci, ROPE_WINCH) >= 0, hooked);
    ctx.check("...the hands are empty and the stake is used", !sim.handLine.on() && sim.anchorStock.n[GEAR_STAKE] == stakes - 1);
    // Hooked: it can't be grabbed again, and winching works with nobody at the drum.
    const double len0 = sim.ropes[0].length;
    sim.setWinch(ci, 1);
    ctx.run(1.0);
    sim.setWinch(ci, 0);
    ctx.check("reeling in needs no one at the winch", sim.ropes.size() == 1 && sim.ropes[0].length < len0 - 0.05, std::to_string(len0 - sim.ropes[0].length));
    stand(sim, wk, fl + V3(1.2, 0, 0.3), V3(-1, 0.1, -0.2), eye, look);
    eye = wk.p + V3(0, 1.6, 0);
    look = norm(fl - eye);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the fairlead it's now an unhook, not a grab", it.current.kind == IA_UNHOOK_LINE, it.current.prompt);
    const std::string off = holdE(sim, it, wk, eye, look, 1.5);
    ctx.check("holding E at the fairlead unhooks it", sim.ropeOn(ci, ROPE_WINCH) < 0 && off == "WINCH LINE UNHOOKED AND SPOOLED", off);
    // Unhooking at the far end works too.
    ctx.check("hooked again programmatically (tests, autopilot)", sim.hookWinchTo(ci, [&] { RopeEnd e; e.kind = ANCHOR_STAKE; e.p = spot; return e; }()).rfind("WINCH HOOKED", 0) == 0);
    stand(sim, wk, spot + V3(1, 0, 0), V3(-1, -0.4, 0), eye, look);
    look = norm((spot + V3(0, 0.1, 0)) - eye);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the stake end E offers the unhook", it.current.kind == IA_UNHOOK_LINE, it.current.prompt);
    holdE(sim, it, wk, eye, look, 1.5);
    ctx.check("...and it comes off", sim.ropeOn(ci, ROPE_WINCH) < 0);
    // Let go: the line spools back.
    ctx.check("grabbing again works", sim.takeLine(ci, ROPE_WINCH, 0, wk.p).empty() && sim.handLine.on());
    sim.dropHandLine();
    ctx.check("letting go puts it away", !sim.handLine.on() && sim.ropes.empty());
}

SCENARIO(strapOnFoot, "recovery/strap-on-foot", "A tow strap is grabbed at a recovery point on foot, carried to the other vehicle's point and hooked there with E; unhooked by hand at either end; refused from the seat") {
    ctx.sim = Sim();
    ctx.sim.world = World(MAP_PROVING_GROUND);
    ctx.add(bronco(), -60, -270, 0);
    ctx.add(bronco(), -72, -270, 0);  // behind it, nose to tail about 6 m
    Sim& sim = ctx.sim;
    const int ai = 0, bi = 1;
    Car& a = sim.cars[ai];
    Car& b = sim.cars[bi];
    ctx.focus(a);
    ctx.run(0.5);
    ctx.check("from the seat the strap is refused", sim.seatHook(ai, ROPE_STRAP).rfind("GET OUT", 0) == 0 && sim.ropes.empty());
    Walker wk;
    Interact it;
    V3 eye, look;
    const V3 rear = a.hookPoint(1);
    // Grab it at the rear point (any recovery point will do).
    const V3 at(rear.x - 1.0, 0, rear.z + 0.4);
    stand(sim, wk, at, V3(1, 0, 0), eye, look);
    look = norm((rear - V3(0, 0.15, 0)) - eye);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the rear point E offers the strap", it.current.kind == IA_GRAB_LINE && it.current.lineKind == ROPE_STRAP && it.current.hook == 1, it.current.prompt);
    holdE(sim, it, wk, eye, look, 1.3);
    ctx.check("holding E takes the strap's end", sim.handLine.on() && sim.handLine.kind == ROPE_STRAP && sim.handLine.hook == 1);
    // Carry it to the other truck's front point.
    const V3 bf = b.hookPoint(0);
    stand(sim, wk, V3(bf.x + 1.0, 0, bf.z + 0.3), V3(1, 0, 0), eye, look);
    sim.handLine.hand = wk.p + V3(0, 1, 0);
    look = norm(bf - eye);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the other truck's point E offers to hook it", it.current.kind == IA_HOOK_LINE && it.current.end.kind == ANCHOR_CAR && it.current.end.car == bi && it.current.end.hook == 0, it.current.prompt);
    const std::string said = holdE(sim, it, wk, eye, look, 2.0);
    const int r = sim.ropeOn(ai, ROPE_STRAP);
    ctx.check("holding E hooks the strap", said.rfind("TOW STRAP:", 0) == 0 && r >= 0 && sim.ropes[r].a.car == ai && sim.ropes[r].a.hook == 1 && sim.ropes[r].b.car == bi && !sim.handLine.on(), said);
    // Unhook by hand at the other end.
    look = norm(bf - eye);
    it.update(sim, wk, eye, look, false, 1.0 / 60);
    ctx.check("at the hooked end E offers the unhook", it.current.kind == IA_UNHOOK_LINE && it.current.lineKind == ROPE_STRAP, it.current.prompt);
    holdE(sim, it, wk, eye, look, 1.5);
    ctx.check("...and the strap comes off", sim.ropeOn(ai, ROPE_STRAP) < 0);
    // A truck that's been removed out from under a held line drops it.
    ctx.check("a grip on a car that's gone is let go", sim.takeLine(bi, ROPE_STRAP, 0, wk.p).empty() && (sim.removeCar(bi), !sim.handLine.on()));
}

// Carrying the winch line (or aiming it at an anchor) used to cost a frame drop in a forest or a boulder field: every frame the
// hook-up prompt asked for every anchor in the line's 59 m reach, a walk over the whole map's trees and rocks and a sling
// wrapped round each boulder in reach (slicing its hull top to bottom for the strap height), to offer the ones in arm's reach.
// Now it only looks at what the lookup grid has round your hands. Counted, not timed: the work (things tested, boulders
// wrapped) per frame stays bounded, and the anchors offered are exactly those the whole list has within arm's reach.
SCENARIO(winchAimCost, "recovery/winch-aim-cost", "Carrying the winch line through dense woods and boulders: each frame's anchor search tests only what's round your hands (a few boulders wrapped, not the 100+ in the line's reach), and offers exactly the anchors the full search finds in arm's reach") {
    props::ScopedRockScans scans(&props::sharedHullLibrary());
    for (int map : {MAP_BACKCOUNTRY, MAP_PROCEDURAL}) {
        const std::string mn = map == MAP_BACKCOUNTRY ? "backcountry" : "procedural";
        ctx.sim = Sim();
        ctx.sim.world = World(map);
        const World& w = ctx.sim.world;
        const double R = recovery::kWinchReach;
        // The densest spot: the most boulders and trees within the line's reach.
        V3 best = w.spawn;
        int bestN = -1;
        for (size_t i = 0; i < w.rocks.size(); i += std::max<size_t>(1, w.rocks.size() / 400)) {
            const V3 q = w.rocks[i].c;
            int n = 0;
            for (const Rock& k : w.rocks) n += k.r >= anchors::kBoulderMinR && std::hypot(k.c.x - q.x, k.c.z - q.z) < R;
            for (const Pole& p : w.poles) n += p.r >= anchors::kTreeMinR && std::hypot(p.x - q.x, p.z - q.z) < R;
            if (n > bestN) bestN = n, best = q;
        }
        Car& c = ctx.add(bronco(), best.x + 3, best.z + 3, 0);
        ctx.focus(c);
        ctx.run(0.2);
        Sim& sim = ctx.sim;
        const int ci = (int)(&c - sim.cars.data());
        const V3 fl = c.hookPoint(0);
        const std::vector<WinchTarget> all = sim.winchTargets(ci);
        const long wrapsAll = recovery::winchScanCounts().wraps;
        ctx.note(mn + ": " + std::to_string(bestN) + " trees and boulders within the line's reach, " + std::to_string(all.size()) + " anchors, " +
                 std::to_string(wrapsAll) + " boulders wrapped for the whole list");
        // Stand beside each of a dozen of those anchors (boulders first), holding the line, aiming at it.
        std::vector<V3> spots;
        for (const WinchTarget& t : all)
            if (t.end.kind == ANCHOR_ROCK && spots.size() < 8) spots.push_back(t.at);
        for (const WinchTarget& t : all)
            if (t.end.kind == ANCHOR_TREE && spots.size() < 12) spots.push_back(t.at);
        ctx.check(mn + ": a dense spot (boulders and trees to stand by)", wrapsAll >= 20 && spots.size() >= 10, std::to_string(wrapsAll) + " " + std::to_string(spots.size()));
        long worstTested = 0, worstWraps = 0;
        int same = 0, offered = 0;
        double msNear = 0, msAll = 0;
        for (const V3& at : spots) {
            Walker wk;
            Interact it;
            V3 eye, look;
            const V3 dir = at - fl;
            stand(sim, wk, at - norm(V3(dir.x, 0, dir.z)) * 1.2, V3(), eye, look);
            look = norm(at - eye);
            sim.handLine = Sim::HandLine();
            sim.handLine.car = ci, sim.handLine.hook = 0, sim.handLine.kind = ROPE_WINCH, sim.handLine.hand = wk.p + V3(0, 1, 0);
            const auto t0 = std::chrono::steady_clock::now();
            it.update(sim, wk, eye, look, false, 1.0 / 60);
            msNear += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const recovery::WinchScan sc = recovery::winchScanCounts();
            worstTested = std::max(worstTested, sc.poles + sc.rocks), worstWraps = std::max(worstWraps, sc.wraps);
            offered += it.current.kind == IA_HOOK_LINE;
            // The same anchors in arm's reach as the whole list has (what the prompt offered before).
            const V3 chest = wk.p + V3(0, 1.0, 0), lk = at;
            const std::vector<WinchTarget> mine = sim.winchTargets(ci, &lk, &chest, 3.0);
            const auto t1 = std::chrono::steady_clock::now();
            const std::vector<WinchTarget> full = sim.winchTargets(ci, &lk);
            msAll += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
            std::vector<const WinchTarget*> inReach;
            for (const WinchTarget& t : full)
                if (len(t.at - chest) <= 3.0) inReach.push_back(&t);
            bool eq = (long)inReach.size() == std::count_if(mine.begin(), mine.end(), [&](const WinchTarget& t) { return len(t.at - chest) <= 3.0; });
            for (size_t i = 0, j = 0; eq && i < mine.size(); i++) {
                if (len(mine[i].at - chest) > 3.0) continue;
                const WinchTarget& f = *inReach[j++];
                eq = f.end.kind == mine[i].end.kind && f.at.x == mine[i].at.x && f.at.y == mine[i].at.y && f.at.z == mine[i].at.z && f.dist == mine[i].dist &&
                     f.usable == mine[i].usable && std::string(f.what) == mine[i].what;
            }
            same += eq;
        }
        sim.dropHandLine();
        const int n = (int)spots.size();
        ctx.note(mn + ": per frame by an anchor, " + std::to_string(msNear / std::max(1, n)) + " ms (the whole list: " + std::to_string(msAll / std::max(1, n)) + " ms)");
        ctx.check(mn + ": by each anchor, the same ones in arm's reach as the whole search finds", same == n, std::to_string(same) + "/" + std::to_string(n));
        ctx.check(mn + ": ...and one is offered to hook", offered >= n - 2, std::to_string(offered) + "/" + std::to_string(n));
        ctx.metric(mn + " boulders wrapped per frame carrying the line", (double)worstWraps, "", 0, 6);
        ctx.metric(mn + " trees and rocks tested per frame carrying the line", (double)worstTested, "", 0, 4000);
    }
}
