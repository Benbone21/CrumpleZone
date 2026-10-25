// Engine work (src/fixes.*, src/interact.*, src/trunk.cpp): the hood opens on its hinges (on foot, E at the nose; Car::hoodOpen),
// and a flooded engine, drowned in a river for real, is fixed by the book: hood up, plugs out, the water cranked out, the
// sump drained and refilled, new plugs in, hood down; every step needed, in order, and the engine runs after.
#include <cmath>
#include <string>

#include "../src/handwork.h"
#include "../src/interact.h"
#include "../src/modelkit.h"
#include "../src/prompts.h"
#include "../src/toolsounds.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

const VehicleSpec& civic() { return vehicleCatalog()[0]; }
const VehicleSpec& bronco() { return vehicleCatalog()[2]; }

V3 eyeOf(const Walker& wk) { return wk.p + V3(0, 1.62, 0); }

V3 flatFwd(const Car& c) {
    V3 f = c.frame().fwd;
    f.y = 0;
    return norm(f);
}

// Stands `d` m past the nose, on the ground.
void standAtNose(const World& w, const Car& c, Walker& wk, double d) {
    const V3 q = c.hoodFront() + flatFwd(c) * d;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

const InteractOption& lookAt(Ctx& ctx, Interact& ia, Walker& wk, const V3& at) {
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return ia.current;
}

// Holds E on `at` until something's said (or `secs` pass), then lets go.
std::string holdOn(Ctx& ctx, Interact& ia, Walker& wk, const V3& at, double secs) {
    std::string said;
    lookAt(ctx, ia, wk, at);
    for (double t = 0; t < secs && said.empty(); t += Ctx::kFrame) {
        said = ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), true, Ctx::kFrame);
        ctx.run(Ctx::kFrame);
    }
    ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return said;
}

double hoodHeight(const Car& c) {
    const Panel& p = c.panels[c.hoodPanel()];
    double y = 0;
    for (int n : p.panelNodes) y += c.nodes[n].p.y;
    return y / (double)p.panelNodes.size();
}

TrunkStock stock(int parts) {
    TrunkStock s;
    s.kit.unlimited = false;
    for (int t = 0; t < TOOL_COUNT; t++) s.kit.tools[t] = true;
    for (int p = 0; p < PART_COUNT; p++) s.kit.parts[p] = parts;
    return s;
}

std::string rows(const RepairChecklist& l) {
    std::string s = l.title + ":";
    for (size_t i = 0; i < l.rows.size(); i++) s += std::string(l.done[i] ? " [X] " : " [ ] ") + l.rows[i];
    return s;
}

// Rivers, to drown an engine for real (the same spot as rivers/pool-drown).
const RiverSpot* spotNear(const World& w, const char* feature) {
    const V3 f = backcountryFeature(feature);
    const RiverSpot* best = nullptr;
    double bd = 1e18;
    for (const River& r : w.rivers)
        for (const RiverSpot& s : r.spots) {
            const double d = std::hypot(s.p.x - f.x, s.p.z - f.z);
            if (d < bd) bd = d, best = &s;
        }
    return best;
}

}  // namespace

SCENARIO(engineHood, "enginework/hood", "The hood of every vehicle opens on its hinges by hand and stays up (even driving off), closes and latches; E at the nose offers OPEN THE HOOD / CLOSE THE HOOD, only when stopped; a hood that's torn off counts as open") {
    int n = 0;
    for (const VehicleSpec& vs : vehicleCatalog()) {
        if (vs.openFrame) continue;  // (tube-frame buggies: a hood panel but no cab; checked below too if they have one)
        Car& c = ctx.add(vs, -60 + 14.0 * n, -30, 0);
        n++;
        ctx.run(0.6);
        const std::string tag = std::string(vs.name) + ": ";
        if (!c.hasHood()) {
            ctx.check(tag + "no hood panel: reported shut, nothing to open", c.hoodShut() && !c.hoodOpen());
            continue;
        }
        ctx.check(tag + "hood starts shut", c.hoodShut() && !c.hoodOpen() && c.hood.share == 0);
        const double h0 = hoodHeight(c);
        c.setHood(true);
        ctx.check(tag + "swinging, not open yet", !c.hoodOpen() && c.hood.dir == 1);
        ctx.runUntil(4, [&] { return c.hoodOpen(); });
        ctx.run(0.8);
        ctx.check(tag + "open", c.hoodOpen() && !c.hoodShut(), "share " + std::to_string(c.hood.share));
        ctx.check(tag + "the panel is up on its hinges, still attached", hoodHeight(c) > h0 + 0.12 && c.panels[c.hoodPanel()].state != 2, std::to_string(hoodHeight(c) - h0));
        ctx.check(tag + "the car is undamaged by it", c.damage < 0.01, std::to_string(c.damage));
        c.setHood(false);
        ctx.runUntil(4, [&] { return c.hoodShut(); });
        ctx.run(0.6);
        ctx.check(tag + "shut again, latched", c.hoodShut() && !c.panels[c.hoodPanel()].hand && std::fabs(hoodHeight(c) - h0) < 0.04, std::to_string(hoodHeight(c) - h0));
    }
    // Up, it stays up while the truck drives off and is not ripped off.
    ctx.sim.cars.clear();
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.focus(c);
    ctx.run(0.6);
    c.setHood(true);
    ctx.runUntil(4, [&] { return c.hoodOpen(); });
    ctx.run(5.0, [&] { c.throttle = 0.6; });
    ctx.check("driven off with the hood up: still on its hinges and still up", c.panels[c.hoodPanel()].state != 2 && c.hoodOpen(), std::to_string(c.frame().vel.x));
    c.throttle = 0, c.brake = 1;
    ctx.run(3.0);
    c.brake = 0, c.handbrake = 1;
    // The prompt at the nose.
    Walker wk;
    Interact ia;
    standAtNose(ctx.sim.world, c, wk, 1.1);
    const V3 look = c.hoodFront() - flatFwd(c) * 0.15;
    const InteractOption& o1 = lookAt(ctx, ia, wk, look);
    ctx.check("at the nose with the hood up: CLOSE THE HOOD", o1.kind == IA_CLOSE_HOOD && !o1.blocked && o1.prompt == "CLOSE THE HOOD", o1.prompt);
    std::string said = holdOn(ctx, ia, wk, look, 3);
    ctx.check("hold E: closing", said == "HOOD CLOSED" && c.hood.dir == -1, said);
    ctx.runUntil(4, [&] { return c.hoodShut(); });
    ctx.run(0.5);
    ctx.check("...and shut", c.hoodShut());
    const InteractOption& o2 = lookAt(ctx, ia, wk, look);
    ctx.check("shut: OPEN THE HOOD", o2.kind == IA_OPEN_HOOD && o2.prompt == "OPEN THE HOOD", o2.prompt);
    ctx.check("...with its own marker and hint text", prompts::info(IA_OPEN_HOOD).verb == std::string("OPEN IT") && prompts::info(IA_CLOSE_HOOD).verb == std::string("CLOSE IT"));
    {
        prompts::Prompter pr;
        const prompts::Focus fo = pr.update(ctx.sim, wk, ia, ia.nearby, eyeOf(wk), look - eyeOf(wk));
        const prompts::Marker* m = fo.current();
        ctx.check("the target marker is the hood's, labelled OPEN THE HOOD, holdable", m && m->kind == IA_OPEN_HOOD && m->label == "OPEN THE HOOD" && m->hold && !m->blocked, m ? m->label : "no target");
    }
    said = holdOn(ctx, ia, wk, look, 3);
    ctx.check("hold E: opening", said == "HOOD OPEN" && c.hood.dir == 1, said);
    // Not while it's rolling.
    c.hood.dir = 0, c.hood.share = 0;
    c.handbrake = 0;
    for (Node& nd : c.nodes) nd.v = c.frame().fwd * 6.0;
    const InteractOption& o3 = lookAt(ctx, ia, wk, look);
    ctx.check("rolling: no hood prompt", o3.kind != IA_OPEN_HOOD && o3.kind != IA_CLOSE_HOOD, o3.prompt);
    // Torn off counts as open (nothing to hold up), never as shut-and-latched.
    ctx.sim.cars.clear();
    Car& t = ctx.add(civic(), -60, -30, 0);
    ctx.run(0.4);
    t.panels[t.hoodPanel()].state = 2;
    ctx.check("a torn-off hood: open, not shut", t.hoodOpen() && t.hoodShut() && t.hoodTorn());
}

SCENARIO(engineFlooded, "enginework/flooded-flow", "A Civic drowns its engine in the east pool (real hydrolock), is moved to the bank, and is fixed by the book: hood, plugs out, water cranked out, oil drained and refilled, new plugs in, hood down; every step needs its tool/part and its turn, hydrolocked clears only at the plugs, then it drives") {
    ctx.sim.world = World(MAP_BACKCOUNTRY);
    const World& w = ctx.sim.world;
    const RiverSpot* sp = spotNear(w, "eastpool");
    ctx.check("there's a pool", sp != nullptr);
    if (!sp) return;
    V3 f = w.waterFlow(sp->p.x, sp->p.z);
    f.y = 0;
    f = len(f) < 1e-6 ? V3(-1, 0, 0) : norm(f);
    const V3 l(-f.z, 0, f.x);
    const V3 a = sp->p - l * (sp->halfWidth + 14), b = sp->p + l * (sp->halfWidth + 14);
    Car& c = ctx.add(civic(), a.x, a.z, std::atan2(-(b.z - a.z), b.x - a.x) * 180 / M_PI);
    ctx.focus(c);
    ctx.run(1.0);
    const V3 dir = norm(V3(b.x - a.x, 0, b.z - a.z)), mid = (a + b) * 0.5;
    ctx.runUntil(40, [&] { return dot(c.frame().com - mid, dir) > 0; }, [&] {
        const Frame fr = c.frame();
        V3 d = b - fr.com;
        d.y = 0;
        c.steer = clampd(std::atan2(dot(d, fr.left), dot(d, fr.fwd)) * 1.2, -0.4, 0.4);
        c.throttle = clampd(0.3 + (6 / 3.6 - dot(fr.vel, fr.fwd)) * 0.6, 0, 1), c.brake = 0, c.handbrake = 0;
    });
    ctx.run(3.0, [&] { c.throttle = 0.4; });
    ctx.check("the river drowned the engine", c.engineDead && c.hydrolocked);
    if (!(c.engineDead && c.hydrolocked)) return;
    // Towed to the bank (dry land): the work is done there.
    V3 shift = a - c.frame().com;
    shift.y = (w.height(a.x, a.z) + 0.6) - c.frame().com.y;
    c.translate(shift);
    c.throttle = 0, c.brake = 0, c.handbrake = 1;
    for (Node& n : c.nodes) n.v = V3();
    ctx.run(2.0);
    ctx.check("still flooded on the bank", c.hydrolocked && c.engineDead && !c.hoodOpen());

    const std::vector<Fix> fixes = fixesFor(c);
    const Fix* fp = nullptr;
    for (const Fix& fx : fixes)
        if (fx.kind == Fix::SPARK_PLUGS) fp = &fx;
    ctx.check("a flooded engine is a repair", fp != nullptr);
    if (!fp) return;
    const Fix fix = *fp;
    Walker wk;
    Interact ia;
    ia.trunkOn = true;
    ia.own = stock(1);
    ia.kit = ia.own.kit;
    standAtNose(w, c, wk, 0.9);
    const V3 hoodLook = c.hoodFront() - flatFwd(c) * 0.15;

    // The checklist and its first line.
    lookAt(ctx, ia, wk, fix.at);
    ctx.check("the checklist: 7 lines, the hood first", ia.checklist.title == "FLOODED ENGINE" && ia.checklist.rows.size() == 7 && ia.checklist.next == 0 && ia.checklist.rows[0] == "HOOD OPEN" && ia.checklist.rows[6] == "HOOD CLOSED", rows(ia.checklist));
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    const InteractOption& o0 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("socket set in hand, hood shut: it says open the hood first", o0.kind == IA_FIX && o0.blocked && o0.prompt.find("OPEN THE HOOD") != std::string::npos, o0.prompt);
    ctx.check("...and holding E on the engine does nothing", holdOn(ctx, ia, wk, fix.at, 6).empty() && c.fieldJobs.empty());

    // 1. The hood.
    wk.hand = TrunkRef();
    const double h0 = hoodHeight(c);
    const InteractOption& oh = lookAt(ctx, ia, wk, hoodLook);
    ctx.check("looking at the nose: OPEN THE HOOD", oh.kind == IA_OPEN_HOOD && !oh.blocked, oh.prompt);
    std::string said = holdOn(ctx, ia, wk, hoodLook, 4);
    ctx.check("hold E: the hood swings up", said == "HOOD OPEN", said);
    ctx.runUntil(4, [&] { return c.hoodOpen(); });
    ctx.run(0.6);
    ctx.check("...it's open on the car model", c.hoodOpen() && hoodHeight(c) > h0 + 0.12);
    lookAt(ctx, ia, wk, fix.at);
    ctx.check("checklist: the hood is ticked, the plugs next", ia.checklist.done[0] && ia.checklist.next == 1, rows(ia.checklist));

    // 2. Out of turn: nothing but the plugs can come first.
    wk.hand = TrunkRef{TK_TOOL, TOOL_WRENCH};
    const InteractOption& ow = lookAt(ctx, ia, wk, fix.at);
    ctx.check("wrench in hand: the plugs need the socket set", ow.blocked && ow.prompt.find("SOCKET SET") != std::string::npos, ow.prompt);
    ctx.check("...holding E: nothing", holdOn(ctx, ia, wk, fix.at, 6).empty() && c.fieldJobs.empty());
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    const InteractOption& o1 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("socket set: REMOVE SPARK PLUGS", o1.kind == IA_FIX && !o1.blocked && o1.prompt == "REMOVE SPARK PLUGS (SOCKET SET)", o1.prompt);
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("the plugs come out (dropped beside the truck)", said == "REMOVE SPARK PLUGS" && c.fieldJobs.size() == 1 && c.fieldJobs[0].done == 1 && !ctx.sim.droppedParts.empty() && ctx.sim.droppedParts.back().part == PART_SPARK_PLUGS, said);
    ctx.check("...still flooded", c.hydrolocked && c.engineDead);

    // 3. Crank the water out: no tool; new plugs and oil can't go in yet.
    wk.hand = TrunkRef{TK_PART, PART_SPARK_PLUGS};
    const InteractOption& oc = lookAt(ctx, ia, wk, fix.at);
    ctx.check("(plugs in hand) the next step is to crank it over, not to fit them", oc.kind == IA_FIX && oc.prompt.find("CRANK") != std::string::npos && oc.tool < 0 && oc.part < 0, oc.prompt);
    wk.hand = TrunkRef();
    const InteractOption& oc2 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("empty-handed: CRANK THE ENGINE OVER, BLOW THE WATER OUT", oc2.kind == IA_FIX && !oc2.blocked && oc2.prompt.find("CRANK THE ENGINE OVER") != std::string::npos, oc2.prompt);
    // The sounds and hands of cranking, over the hold.
    ToolSounds ts;
    int cranks = 0, sprays = 0, glugs = 0;
    auto play = [&](Sfx s, const V3&, float, float, float) {
        cranks += s == SFX_ENGINE_CRANK, sprays += s == SFX_WATER_SPRAY, glugs += s == SFX_GLUG;
    };
    bool handOn = false;
    said.clear();
    lookAt(ctx, ia, wk, fix.at);
    for (double t = 0; t < 9 && said.empty(); t += Ctx::kFrame) {
        said = ia.update(ctx.sim, wk, eyeOf(wk), fix.at - eyeOf(wk), true, Ctx::kFrame);
        const Handwork hw = handwork(ctx.sim, wk, ia, eyeOf(wk), fix.at - eyeOf(wk), ctx.t);
        handOn |= hw.hands[1].on;
        ts.update(play, ia, hw, eyeOf(wk));
        ctx.run(Ctx::kFrame);
    }
    ia.update(ctx.sim, wk, eyeOf(wk), fix.at - eyeOf(wk), false, Ctx::kFrame);
    ctx.check("hold E: the water is cranked out", said == "CRANK THE WATER OUT" && c.fieldJobs[0].done == 2, said);
    ctx.check("...a hand at the engine while it turns, the starter heard and water spurting", handOn && cranks >= 3 && sprays >= 3, std::to_string(cranks) + " " + std::to_string(sprays));
    ctx.check("...and the engine is still flooded (the oil's still wet)", c.hydrolocked);

    // 4. Drain the sump: under the engine, the wrench.
    const V3 plug = enginePlugAt(c);
    wk.hand = TrunkRef{TK_TOOL, TOOL_RATCHET};
    const InteractOption& od0 = lookAt(ctx, ia, wk, plug);
    ctx.check("under the engine, socket set in hand: the drain plug needs the wrench", od0.kind == IA_FIX && od0.blocked && od0.prompt.find("WRENCHES") != std::string::npos, od0.prompt);
    wk.hand = TrunkRef{TK_TOOL, TOOL_WRENCH};
    const InteractOption& od = lookAt(ctx, ia, wk, plug);
    ctx.check("wrench: PULL THE DRAIN PLUG, DRAIN THE WATER AND OIL, down at the sump", od.kind == IA_FIX && !od.blocked && od.prompt.find("DRAIN") != std::string::npos && len(od.at - plug) < 0.01, od.prompt);
    said.clear();
    int glug0 = glugs;
    lookAt(ctx, ia, wk, plug);
    for (double t = 0; t < 10 && said.empty(); t += Ctx::kFrame) {
        said = ia.update(ctx.sim, wk, eyeOf(wk), plug - eyeOf(wk), true, Ctx::kFrame);
        const Handwork hw = handwork(ctx.sim, wk, ia, eyeOf(wk), plug - eyeOf(wk), ctx.t);
        ts.update(play, ia, hw, eyeOf(wk));
        ctx.run(Ctx::kFrame);
    }
    ia.update(ctx.sim, wk, eyeOf(wk), plug - eyeOf(wk), false, Ctx::kFrame);
    ctx.check("the sump is drained (with the wrench, glugging)", said == "DRAIN THE OIL" && c.fieldJobs[0].done == 3 && glugs > glug0, said + " " + std::to_string(glugs - glug0));

    // 5. Refill the oil (a part from the back, in hand).
    wk.hand = TrunkRef();
    ia.own.kit.parts[PART_ENGINE_OIL] = 1;
    ia.kit = ia.own.kit;
    const InteractOption& of0 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("no oil in hand: it says to get it from the back", of0.blocked && of0.prompt.find("NEW ENGINE OIL") != std::string::npos, of0.prompt);
    ia.own.kit.parts[PART_ENGINE_OIL] = 0;
    ia.kit = ia.own.kit;
    const InteractOption& of1 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("none left: it says so", of1.blocked && of1.prompt.find("NO ENGINE OIL") != std::string::npos, of1.prompt);
    ia.own.kit.parts[PART_ENGINE_OIL] = 1;
    ia.kit = ia.own.kit;
    wk.hand = TrunkRef{TK_PART, PART_ENGINE_OIL};
    const InteractOption& of2 = lookAt(ctx, ia, wk, fix.at);
    ctx.check("oil in hand: POUR IN NEW OIL", of2.kind == IA_FIX && !of2.blocked && of2.prompt.find("POUR IN NEW OIL") != std::string::npos && of2.part == PART_ENGINE_OIL, of2.prompt);
    bool jugShown = false;
    said.clear();
    lookAt(ctx, ia, wk, fix.at);
    for (double t = 0; t < 8 && said.empty(); t += Ctx::kFrame) {
        said = ia.update(ctx.sim, wk, eyeOf(wk), fix.at - eyeOf(wk), true, Ctx::kFrame);
        const Handwork hw = handwork(ctx.sim, wk, ia, eyeOf(wk), fix.at - eyeOf(wk), ctx.t);
        for (const Placed& pl : hw.models) jugShown |= pl.id == std::string(repairPartModel(PART_ENGINE_OIL)) && hw.hands[1].on;
        ts.update(play, ia, hw, eyeOf(wk));
        ctx.run(Ctx::kFrame);
    }
    ia.update(ctx.sim, wk, eyeOf(wk), fix.at - eyeOf(wk), false, Ctx::kFrame);
    ctx.check("the oil goes in (used up, out of your hand)", said == "REFILL THE OIL" && ia.usedPart == PART_ENGINE_OIL && !wk.hand.any() && c.fieldJobs[0].done == 4, said);
    ctx.check("...the jug is tipped over the engine in your hands", jugShown && mk::findModel("oil_jug") != nullptr);
    ia.usedPart = -1;
    ctx.check("...and the engine is STILL flooded: only the plugs make it whole", c.hydrolocked && c.engineDead);

    // 6. New plugs in: fixed.
    wk.hand = TrunkRef{TK_PART, PART_SPARK_PLUGS};
    said = holdOn(ctx, ia, wk, fix.at, 8);
    ctx.check("the new plugs go in: the engine's whole", said == "FIT SPARK PLUGS: FLOODED ENGINE FIXED" && ia.usedPart == PART_SPARK_PLUGS && !c.hydrolocked && !c.engineDead, said + " used " + std::to_string(ia.usedPart) + " locked " + std::to_string(c.hydrolocked) + " dead " + std::to_string(c.engineDead) + " health " + std::to_string(c.engineHealth));
    ia.usedPart = -1;
    ctx.check("...it isn't a repair any more", [&] {
        for (const Fix& fx : fixesFor(c))
            if (fx.kind == Fix::SPARK_PLUGS) return false;
        return true;
    }());
    // 7. The hood down.
    wk.hand = TrunkRef();
    lookAt(ctx, ia, wk, hoodLook);
    ctx.check("checklist: all but the hood closed", ia.checklist.next == 6 && ia.checklist.done[5] && !ia.checklist.done[6], rows(ia.checklist));
    const InteractOption& ocl = lookAt(ctx, ia, wk, hoodLook);
    ctx.check("CLOSE THE HOOD", ocl.kind == IA_CLOSE_HOOD, ocl.prompt);
    said = holdOn(ctx, ia, wk, hoodLook, 4);
    ctx.check("hold E: down", said == "HOOD CLOSED", said);
    ctx.runUntil(4, [&] { return c.hoodShut(); });
    ctx.run(0.5);
    lookAt(ctx, ia, wk, hoodLook);
    ctx.check("shut, and the job's done and gone", c.hoodShut() && c.fieldJobs.empty() && ia.checklist.rows.empty(), rows(ia.checklist));
    // Start it and drive.
    c.handbrake = 0;
    const V3 p0 = c.frame().com;
    ctx.run(2.0, [&] { c.throttle = 0.7; });  // (the pool is dead ahead: a short run)
    ctx.check("it starts and drives (the engine as healthy as before the flood)", !c.engineDead && !c.hydrolocked && c.engineHealth > 0.9 && c.rpm > 800 && len(c.frame().com - p0) > 3.0, std::to_string(len(c.frame().com - p0)) + " health " + std::to_string(c.engineHealth) + " rpm " + std::to_string(c.rpm));
}

SCENARIO(engineHoodSounds, "enginework/hood-sounds", "The hood makes its sounds through ToolSounds while it's heaved up and while it's let down and latched") {
    Car& c = ctx.add(bronco(), -60, -30, 0);
    ctx.run(0.5);
    Walker wk;
    Interact ia;
    standAtNose(ctx.sim.world, c, wk, 1.1);
    const V3 look = c.hoodFront() - flatFwd(c) * 0.15 + V3(0, 0.2, 0);  // (the winch hook is lower)
    ToolSounds ts;
    int up = 0, down = 0;
    auto play = [&](Sfx s, const V3&, float, float, float) {
        up += s == SFX_HOOD_OPEN;
        down += s == SFX_HOOD_SHUT;
    };
    auto hold = [&] {
        lookAt(ctx, ia, wk, look);
        std::string said;
        bool hands = false;
        for (double t = 0; t < 4 && said.empty(); t += Ctx::kFrame) {
            said = ia.update(ctx.sim, wk, eyeOf(wk), look - eyeOf(wk), true, Ctx::kFrame);
            const Handwork hw = handwork(ctx.sim, wk, ia, eyeOf(wk), look - eyeOf(wk), ctx.t);
            hands |= hw.hands[0].on && hw.hands[1].on;
            ts.update(play, ia, hw, eyeOf(wk));
            ctx.run(Ctx::kFrame);
        }
        ia.update(ctx.sim, wk, eyeOf(wk), look - eyeOf(wk), false, Ctx::kFrame);
        return std::make_pair(said, hands);
    };
    auto r = hold();
    ctx.check("opening: both hands on the hood's edge, the latch and prop heard once", r.first == "HOOD OPEN" && r.second && up == 1 && down == 0, r.first + " " + std::to_string(up));
    ctx.runUntil(4, [&] { return c.hoodOpen(); });
    ctx.run(0.5);
    r = hold();
    ctx.check("closing: both hands, the slam heard once", r.first == "HOOD CLOSED" && r.second && up == 1 && down == 1, r.first + " " + std::to_string(down));
}
