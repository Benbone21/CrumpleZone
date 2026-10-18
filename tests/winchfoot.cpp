// Winch lines and tow straps are hooked up on foot: you can't do it from the seat. You grab the winch line at the fairlead (or a strap
// at a recovery point), walk it out (it pays out of the drum, as far as there is line), hook it at the anchor, and unhook it by hand at
// either end. Running the drum, once it's hooked, doesn't need you at it.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/interact.h"
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
