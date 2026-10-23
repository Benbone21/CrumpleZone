// Field repair seen by the others in a session (src/repairsync.*): a host and a guest over loopback, each with its own
// sim. One of them jacks a corner, takes the wheel off, knocks out the broken CV axle, fits a new one, puts the wheel
// back and lets the jack down; the other sees the tool in their hands, the jack under the car going up and down, the
// wheel off the hub and in their arms, the broken axle on the ground, the new one going in, the car whole again at the
// end, and hears the tools. Both ways round. And the messages read safely (bad fields dropped, unknown ones skipped,
// nothing from a player about a vehicle that isn't theirs).
#include <unistd.h>

#include <cmath>
#include <set>
#include <thread>

#include "../src/handwork.h"
#include "../src/interact.h"
#include "../src/net.h"
#include "../src/repairsync.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

int testPort() { return 27900 + (int)(getpid() % 300); }

V3 hubOf(const Car& c, int w) { return (c.nodes[c.wheels[w].inner].p + c.nodes[c.wheels[w].outer].p) * 0.5; }

void standAt(const World& w, const Car& c, Walker& wk, const V3& at, double back) {
    V3 out = at - c.frame().com;
    out.y = 0;
    out = norm(out);
    const V3 q = at + out * back;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

struct Side {
    Sim& sim;
    NetSession& net;
    Walker wk;
    Interact ia;
    repairsync::Sender out;
    repairsync::Remote in;
    size_t bytes = 0, msgs = 0;
    // What this side has seen of the other's work.
    std::set<int> kinds, tools, parts;
    std::set<std::string> models;
    int sounds[SFX_COUNT] = {};
    bool sawCarrying = false, sawJackInHand = false;
    double worstJack = 0;  // the jack's height here against theirs (m, while it's under)
    int jackFrames = 0;
};

// The car of `owner` as `viewer` has it (its copy).
Car* copyOf(Side& viewer, const Car& mine) {
    const int i = repairsync::carByNetId(viewer.sim, mine.netId);
    return i >= 0 && viewer.sim.cars[i].remote ? &viewer.sim.cars[i] : nullptr;
}

}  // namespace

SCENARIO(netRepairSync, "net/repair-sync",
         "A field repair seen by the other player, both ways: the tool in hand, the jack under the car and its height, the wheel off and in their arms, the broken part on the ground, the new one going in, the car whole at the end, the tools heard; a few hundred bytes a second") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = testPort();
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);

    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[2], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    NetSession client;
    bool joined = false, joinDone = false;
    std::thread joiner([&] {
        joined = client.join("127.0.0.1:" + std::to_string(port), "BUDDY", 3.0);
        joinDone = true;
    });
    for (int k = 0; k < 400 && !joinDone; k++) {
        host.receive(ctx.sim, ctx.t);
        usleep(5000);
    }
    joiner.join();
    ctx.check("guest is let in", joined && client.self == 1, client.error);
    if (!joined) return;
    client.adopt(sim2);

    Side hs{ctx.sim, host}, gs{sim2, client};
    auto frame = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        sim2.drive(false, false, false, false, true, Ctx::kFrame);
        sim2.advance(Ctx::kFrame, 1000);
        for (Car& c : sim2.cars) c.updateStats();
        client.send(sim2, ctx.t);
        for (Side* s : {&hs, &gs}) {
            Side& o = s == &hs ? gs : hs;
            s->out.update(s->sim, s->wk.active ? &s->wk : nullptr, s->ia, ctx.t, true, [&](const std::string& t) {
                s->net.sendGame(t);
                s->bytes += t.size() + 12, s->msgs++;  // (the chatter frame: length, type, player, text length, time)
            });
            for (const NetSession::GameMsg& m : s->net.gameIn)
                if (m.player >= 0 && m.player < net::kMaxPeers) s->in.onMessage(s->sim, m.player, s->net.players[m.player].name, m.text, ctx.t);
            s->net.gameIn.clear();
            s->in.update(s->sim, Ctx::kFrame, ctx.t);
            // The other one's work as drawn and heard here (standing where they are: the walk message's job, not tested here).
            for (repairsync::Remote::Player& p : s->in.players) {
                Handwork hw;
                s->in.handwork(s->sim, p, o.wk.p, o.wk.yaw, 1.0, ctx.t, hw);
                s->in.sounds(p, hw, [&](Sfx what, const V3&, float, float, float) { s->sounds[what]++; }, o.wk.p + V3(3, 1.6, 0));
                if (p.work.working()) s->kinds.insert(p.work.job.kind), s->tools.insert(p.work.job.tool), s->parts.insert(p.work.job.part);
                for (const Placed& pl : hw.models) s->models.insert(pl.id);
                s->sawCarrying |= p.work.carrying;
                s->sawJackInHand |= p.work.jack;
            }
            // Their jack here against theirs there.
            for (const Car& c : o.sim.cars)
                if (!c.remote && c.jackPlaced)
                    if (Car* cp = copyOf(*s, c); cp && cp->jackPlaced && cp->jackWheel == c.jackWheel) {
                        s->worstJack = std::max(s->worstJack, ctx.t > 0 ? std::fabs(cp->jackHeight - c.jackHeight) : 0.0);
                        s->jackFrames++;
                    }
        }
        usleep(300);
    };
    ctx.run(1.5, frame);
    ctx.check("each sees the other's truck", copyOf(hs, sim2.cars[0]) && copyOf(gs, hostCar));

    // `w` does the whole CV axle job on its own truck `c`; `v` watches.
    auto repair = [&](Side& w, Side& v, Car& c, const std::string& tag) {
        const int k = 2;  // rear left
        c.wheels[k].shaftBroken = true;
        c.handbrake = 1;
        ctx.run(1.5, frame);
        Car* copy = copyOf(v, c);
        ctx.check(tag + "the watcher's copy has the broken axle", copy && copy->wheels[k].shaftBroken);
        const std::vector<Fix> fixes = fixesFor(c);
        const Fix* fp = nullptr;
        for (const Fix& f : fixes)
            if (f.kind == Fix::CV_AXLE) fp = &f;
        ctx.check(tag + "the CV axle is a repair", fp != nullptr);
        if (!fp || !copy) return;
        const Fix fix = *fp;
        const World& world = w.sim.world;
        auto eye = [&] { return w.wk.p + V3(0, 1.62, 0); };
        auto hold = [&](const V3& at, double secs, const std::function<bool()>& until = nullptr) {
            for (int i = 0; i < 2; i++) w.ia.update(w.sim, w.wk, eye(), at - eye(), false, Ctx::kFrame);
            std::string said;
            ctx.runUntil(secs, [&] { return !said.empty() || (until && until()); }, [&] {
                const std::string s = w.ia.update(w.sim, w.wk, eye(), at - eye(), true, Ctx::kFrame);
                if (!s.empty()) said = s;
                frame();
            });
            w.ia.update(w.sim, w.wk, eye(), at - eye(), false, Ctx::kFrame);
            return said;
        };
        const size_t bytes0 = w.bytes;
        const double t0 = ctx.t;
        // The jack under and up.
        const V3 jackAt = Interact::jackAim(w.sim, c, k);
        standAt(world, c, w.wk, jackAt, 1.1);
        w.wk.holdingJack = true;
        ctx.run(0.5, frame);
        ctx.check(tag + "the watcher sees the jack in their hands", v.sawJackInHand);
        std::string said = hold(jackAt, 6);
        ctx.check(tag + "the jack goes under", said == "JACK UNDER" && c.jackPlaced, said);
        hold(jackAt, 10, [&] { return Interact::cornerLifted(w.sim, c, k) && c.jackHeight > 0.1; });
        ctx.run(1.0, frame);
        ctx.check(tag + "the corner's up", Interact::cornerLifted(w.sim, c, k), std::to_string(c.jackHeight));
        ctx.check(tag + "the watcher saw them work the jack", v.kinds.count(IA_PLACE_JACK) && v.kinds.count(IA_RAISE_JACK));
        ctx.check(tag + "the watcher has the jack under the same corner", copy->jackPlaced && copy->jackWheel == k);
        ctx.metric(tag + "the jack's height here and there, settled", std::fabs(copy->jackHeight - c.jackHeight), "m", 0, 0.01);
        ctx.metric(tag + "...and at worst while it went up", v.worstJack, "m", 0, 0.08);
        ctx.check(tag + "the watcher heard the jack pumped", v.sounds[SFX_JACK_PUMP] > 0, std::to_string(v.sounds[SFX_JACK_PUMP]));
        // The wheel off.
        standAt(world, c, w.wk, fix.at, 1.2);
        said = hold(fix.at, 8);
        ctx.check(tag + "the wheel comes off", said == "WHEEL OFF" && !c.wheels[k].mounted, said);
        ctx.run(1.0, frame);
        ctx.check(tag + "the watcher sees the bare hub and the wheel in their arms", !copy->wheels[k].mounted && v.sawCarrying);
        ctx.check(tag + "...the lug wrench on the nuts", v.kinds.count(IA_REMOVE_WHEEL) && v.models.count(toolModel(TOOL_LUG_WRENCH)));
        ctx.check(tag + "...and heard it", v.sounds[SFX_WRENCH] > 0 && v.sounds[SFX_WHEEL_OFF] > 0, std::to_string(v.sounds[SFX_WRENCH]) + " " + std::to_string(v.sounds[SFX_WHEEL_OFF]));
        Interact::drop(w.sim, w.wk, V3(1, 0, 0));
        // The broken axle out (the hammer).
        const size_t drops = v.sim.droppedParts.size();
        said = hold(fix.at, 8);
        ctx.check(tag + "the broken axle comes out", said == "REMOVE CV AXLE" && !w.sim.droppedParts.empty(), said);
        ctx.run(0.5, frame);
        ctx.check(tag + "the watcher saw the hammer at it", v.kinds.count(IA_FIX) && v.tools.count(TOOL_HAMMER) && v.models.count(toolModel(TOOL_HAMMER)));
        ctx.check(tag + "...heard it", v.sounds[SFX_HAMMER] > 0, std::to_string(v.sounds[SFX_HAMMER]));
        const bool dropped = v.sim.droppedParts.size() == drops + 1 && v.sim.droppedParts.back().part == PART_CV_AXLE &&
                             len(v.sim.droppedParts.back().p - w.sim.droppedParts.back().p) < 0.02;
        ctx.check(tag + "...and has the broken axle lying where they put it", dropped);
        // The new one in.
        said = hold(fix.at, 8);
        ctx.check(tag + "the new axle goes in: fixed", said == "FIT CV AXLE: CV AXLE FIXED" && !c.wheels[k].shaftBroken, said);
        ctx.run(1.0, frame);
        ctx.check(tag + "the watcher saw the new axle going in", v.parts.count(PART_CV_AXLE) && v.models.count(repairPartModel(PART_CV_AXLE)));
        ctx.check(tag + "...and has the car whole", !copy->wheels[k].shaftBroken);
        // The wheel back on, the jack down and out.
        int li = -1;
        for (int i = 0; i < (int)w.sim.looseWheels.size(); i++)
            if (li < 0 || len(w.sim.looseWheels[i].p - w.wk.p) < len(w.sim.looseWheels[li].p - w.wk.p)) li = i;
        if (li >= 0) hold(w.sim.looseWheels[li].p, 2);
        ctx.check(tag + "picked the wheel up", w.wk.carrying.has_value());
        said = hold(hubOf(c, k), 8);
        ctx.check(tag + "the wheel back on", said == "WHEEL ON" && c.wheels[k].mounted, said);
        standAt(world, c, w.wk, hubOf(c, k), 1.1);
        said = hold(Interact::jackAim(w.sim, c, k), 8);
        ctx.check(tag + "the jack down", said == "JACK DOWN", said);
        hold(Interact::jackAim(w.sim, c, k), 4);
        w.wk.holdingJack = false;
        ctx.run(1.5, frame);
        ctx.check(tag + "the jack's out", !c.jackPlaced);
        ctx.check(tag + "the watcher: the wheel on, the jack gone, nothing in their hands", copy->wheels[k].mounted && !copy->jackPlaced && v.in.players.size() == 1 && v.in.players[0].work.nothing());
        ctx.check(tag + "...heard the wheel go on and the jack let down", v.sounds[SFX_WHEEL_ON] > 0 && v.sounds[SFX_JACK_LOWER] > 0);
        ctx.metric(tag + "repair chatter sent", (double)(w.bytes - bytes0) / std::max(1.0, ctx.t - t0), "B/s", 0, 400);
        w.wk.active = false;
        ctx.run(0.5, frame);
    };
    repair(gs, hs, sim2.cars[0], "guest's repair, host watching: ");
    repair(hs, gs, hostCar, "host's repair, guest watching: ");
}

SCENARIO(netRepairFormat, "net/repair-sync-format", "The repair messages read safely: a good one round trips, bad fields are dropped, unknown keys skipped, a vehicle that isn't the sender's left alone, other chatter not taken") {
    Car& mine = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Car& theirs = ctx.add(vehicleCatalog()[2], -10, -8, 0);
    mine.netId = (0u << 16) | 1, theirs.netId = (2u << 16) | 1;
    theirs.remote = true;
    ctx.run(0.3);
    repairsync::Work w;
    w.hand = TrunkRef{TK_TOOL, TOOL_HAMMER};
    w.carrying = true, w.carry.R = 0.4, w.carry.rimR = 0.2;
    w.job.kind = IA_FIX, w.job.wheel = 2, w.job.step = 2, w.job.tool = TOOL_HAMMER, w.job.fix.kind = Fix::CV_AXLE, w.job.fix.part = PART_CV_AXLE, w.job.hold = 4, w.job.at = V3(1, 2, 3);
    w.carId = theirs.netId, w.secs = 1.25;
    const std::string text = repairsync::encode(w);
    repairsync::Work back;
    ctx.check("a work message round trips", repairsync::decode(text, back) && back.same(w) && std::fabs(back.secs - 1.25) < 1e-6 && len(back.job.at - w.job.at) < 1e-6, text);
    ctx.check("...and stays short", text.size() < 120, std::to_string(text.size()));
    repairsync::Work bad;
    repairsync::decode("work h=0,99 do=99,1,2,3,4 w=5,1,1,0,0,0 j=1", bad);
    ctx.check("bad fields dropped, the good kept", !bad.hand.any() && !bad.working() && !bad.carrying && bad.jack);
    repairsync::decode("work later=1,2,3 c=1 h=0,3", bad);
    ctx.check("unknown keys skipped", bad.chain && bad.hand.kind == TK_TOOL && bad.hand.id == TOOL_HAMMER);
    repairsync::decode("work do=21,1,0,0,0,0,0,0,0,0,0,4,1,nan,0,0", bad);
    ctx.check("a job at no number isn't one", !bad.working());

    repairsync::Remote in;
    const std::string jack = "jack " + std::to_string(theirs.netId) + " 1 0.3 0.1 -10 0 -8";
    ctx.check("a jack from someone else about their car: left alone", in.onMessage(ctx.sim, 1, "B", jack, 0) && !theirs.jackPlaced);
    ctx.check("...from its owner: under", in.onMessage(ctx.sim, 2, "C", jack, 0) && theirs.jackPlaced && theirs.jackWheel == 1);
    in.onMessage(ctx.sim, 2, "C", "jack " + std::to_string(theirs.netId) + " 9 0.3 0.1 -10 0 -8", 0);
    ctx.check("...a wheel it hasn't: ignored", theirs.jackWheel == 1);
    in.onMessage(ctx.sim, 0, "A", "jack " + std::to_string(mine.netId) + " 1 0.3 0.1 10 0 -8", 0);
    ctx.check("...and nobody puts a jack under one of mine", !mine.jackPlaced);
    in.update(ctx.sim, 0.1, repairsync::kJackExpiry + 1);
    ctx.check("...unheard, it's gone", !theirs.jackPlaced);
    theirs.wheels[0].popped = true;
    in.onMessage(ctx.sim, 1, "B", "fixed " + std::to_string(theirs.netId) + " " + std::to_string((int)Fix::NEW_TIRE) + " 0 -1 -1", 0);
    ctx.check("a fix from someone else about their car: not done", theirs.wheels[0].popped);
    in.onMessage(ctx.sim, 2, "C", "fixed " + std::to_string(theirs.netId) + " " + std::to_string((int)Fix::DRIVE_SHAFT) + " -1 999999 -1", 0);
    in.onMessage(ctx.sim, 2, "C", "fixed " + std::to_string(theirs.netId) + " " + std::to_string((int)Fix::NEW_TIRE) + " 0 -1 -1", 0);
    ctx.check("...from its owner: done (and a beam it hasn't is no crash)", !theirs.wheels[0].popped);
    const size_t n = ctx.sim.droppedParts.size();
    in.onMessage(ctx.sim, 1, "B", "drop 99 0 0 0 0", 0);
    in.onMessage(ctx.sim, 1, "B", "drop 1 inf 0 0 0", 0);
    ctx.check("a dropped part that isn't one: none", ctx.sim.droppedParts.size() == n);
    for (int i = 0; i < 20; i++) in.onMessage(ctx.sim, 1, "B", "drop 1 3 0 4 0.5", 0);
    ctx.check("...a flood of them: no more than a dozen kept", ctx.sim.droppedParts.size() <= 12 && ctx.sim.droppedParts.back().part == 1);
    ctx.check("an older build's chatter isn't taken (walk, horn, ping)",
              !in.onMessage(ctx.sim, 1, "B", "walk 1 2 3 0.5 1", 0) && !in.onMessage(ctx.sim, 1, "B", "horn 1", 0) && !in.onMessage(ctx.sim, 1, "B", "ping 0 1 2 3", 0));
    in.onMessage(ctx.sim, 1, "B", "work", 0);
    ctx.check("a bare work message: a player holding nothing", in.players.size() == 1 && in.players[0].work.nothing());
    in.update(ctx.sim, 0.1, repairsync::kExpiry + 1);
    ctx.check("...forgotten when unheard", in.players.empty());
}
