// The battery, the jump pack and jumper cables (src/battery.*): lights left on flatten it and it won't crank; the jump pack starts it; cables
// from a running vehicle start it; another player sees the cables and the start.
#include <unistd.h>

#include <cmath>
#include <thread>

#include "../src/battery.h"
#include "../src/handwork.h"
#include "../src/interact.h"
#include "../src/net.h"
#include "../src/repairsync.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

int testPort() { return 28300 + (int)(getpid() % 300); }

V3 eyeOf(const Walker& wk) { return wk.p + V3(0, 1.62, 0); }

// Stands `back` m from `at`, on the side away from the vehicle's middle.
void standAt(const World& w, const Car& c, Walker& wk, const V3& at, double back) {
    V3 out = at - c.frame().com;
    out.y = 0;
    out = norm(out);
    const V3 q = at + out * back;
    wk.p = V3(q.x, w.height(q.x, q.z), q.z);
    wk.v = V3();
    wk.active = true;
}

// Holds E on `at` until something is said (or `secs` pass).
std::string holdOn(Sim& sim, Interact& ia, Walker& wk, const V3& at, double secs, const std::function<void()>& frame) {
    std::string said;
    for (int i = 0; i < 2; i++) ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    for (double t = 0; t < secs && said.empty(); t += Ctx::kFrame) {
        said = ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), true, Ctx::kFrame);
        frame();
    }
    ia.update(sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    return said;
}

}  // namespace

SCENARIO(batteryFlat, "battery/flat", "Lights left on with the engine off flatten the battery; the starter then clicks and the engine won't turn over; the jump pack brings it back and it starts") {
    Car& car = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    ctx.focus(car);
    ctx.run(0.5);
    ctx.check("a new vehicle's battery is full and the engine runs", car.battery > 0.99 && !car.engineOff);
    // Out and away, lights off: nothing drains.
    car.engineOff = true, car.lampMode = LAMPS_OFF;
    for (int i = 0; i < 60; i++) ctx.sim.updateTrail(10);
    ctx.check("engine off, lights off: it holds its charge", car.battery > 0.99, std::to_string(car.battery));
    // Lights left on: flat in minutes.
    car.lampMode = LAMPS_LOW;
    double flatAfter = -1;
    for (int i = 0; i < 100 && flatAfter < 0; i++) {
        ctx.sim.updateTrail(10);
        if (battery::flat(car)) flatAfter = (i + 1) * 10.0;
    }
    ctx.check("lights on, engine off: the battery goes flat", flatAfter > 60 && flatAfter < 600, std::to_string(flatAfter));
    car.lampMode = LAMPS_OFF;
    // Getting in: the starter only clicks.
    car.occupied = true;
    for (int i = 0; i < 30; i++) ctx.sim.updateTrail(0.2);
    ctx.check("a flat battery won't crank the engine", car.engineOff && car.cranking && car.crank < 0.01);
    int clicks = 0, cranks = 0;
    battery::Sounds snd;
    for (int i = 0; i < 60; i++) snd.update(ctx.sim, 0.05, [&](Sfx s, const V3&, float, float, float) { clicks += s == SFX_CLICK, cranks += s == SFX_STARTER; });
    ctx.check("...it clicks", clicks >= 3 && cranks == 0, std::to_string(clicks) + " " + std::to_string(cranks));
    // A weak battery cranks slowly, a good one quickly.
    car.battery = 0.28;
    double tWeak = 0;
    for (; tWeak < 60 && car.engineOff && !battery::flat(car); tWeak += 0.1) ctx.sim.updateTrail(0.1);
    const bool weakStarted = !car.engineOff;
    car.engineOff = true, car.battery = 0.9, car.crank = 0;
    double tGood = 0;
    for (; tGood < 20 && car.engineOff; tGood += 0.1) ctx.sim.updateTrail(0.1);
    ctx.check("a good battery starts it in about a second", !car.engineOff && tGood > 0.5 && tGood < 1.6, std::to_string(tGood));
    ctx.check("a weak one is slow (and may die trying)", !weakStarted || tWeak > tGood * 1.5, std::to_string(tWeak) + " vs " + std::to_string(tGood));
    // The jump pack, on foot.
    car.engineOff = true, car.battery = 0.02, car.crank = 0, car.occupied = false;
    Walker wk;
    Interact ia;
    ia.kit.unlimited = true;
    wk.hand = TrunkRef{TK_TOOL, TOOL_JUMP_PACK};
    const V3 at = battery::aim(car);
    standAt(ctx.sim.world, car, wk, at, 1.2);
    ctx.run(0.2);
    for (int i = 0; i < 2; i++) ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    ctx.check("the pack in your hands: a job at the battery", ia.current.kind == IA_JUMP_PACK && !ia.current.blocked, ia.current.prompt);
    wk.hand = TrunkRef();
    for (int i = 0; i < 2; i++) ia.update(ctx.sim, wk, eyeOf(wk), at - eyeOf(wk), false, Ctx::kFrame);
    ctx.check("...without it, none", ia.current.kind != IA_JUMP_PACK);
    wk.hand = TrunkRef{TK_TOOL, TOOL_JUMP_PACK};
    const std::string said = holdOn(ctx.sim, ia, wk, at, 8, [&] { ctx.run(Ctx::kFrame); });
    ctx.check("hold E: the pack goes on the battery", said.find("JUMP PACK ON") == 0 && car.battery >= battery::kPackLevel - 0.05, said + " " + std::to_string(car.battery));
    car.occupied = true;
    ctx.runUntil(8, [&] { return !car.engineOff; }, [&] {});
    ctx.check("...and it starts", !car.engineOff && battery::running(car));
    ctx.run(0.5);
    Handwork hw = handwork(ctx.sim, wk, ia, eyeOf(wk), at - eyeOf(wk), ctx.t);
    (void)hw;
}

SCENARIO(batteryCables, "battery/cables", "Jumper cables walked from a running vehicle's battery to a dead one's: the dead one starts; the cables come off if the vehicles part; running, the battery charges") {
    ctx.add(vehicleCatalog()[2], 0, 0, 0);
    ctx.add(vehicleCatalog()[2], 3.2, 0, 0);
    ctx.focus(ctx.sim.cars[0]);
    ctx.run(0.5);
    Car &dead = ctx.sim.cars[0], &live = ctx.sim.cars[1];
    dead.engineOff = true, dead.battery = 0.03, dead.occupied = true;
    ctx.run(1.0);
    ctx.check("the dead one won't start by itself", dead.engineOff);
    dead.occupied = false;
    Walker wk;
    Interact ia;
    ia.kit.unlimited = true;
    wk.hand = TrunkRef{TK_TOOL, TOOL_CABLES};
    const V3 a = battery::aim(dead), b = battery::aim(live);
    standAt(ctx.sim.world, dead, wk, a, 1.2);
    auto frame = [&] {
        if (ctx.sim.handCable.on()) ctx.sim.handCable.hand = wk.p + V3(0, 1.0, 0);
        ctx.run(Ctx::kFrame);
    };
    std::string said = holdOn(ctx.sim, ia, wk, a, 6, frame);
    ctx.check("the first clamp goes on the dead battery", said.find("CLAMPED") == 0 && ctx.sim.handCable.car == 0, said);
    ctx.check("...and the cables are still in your hands", wk.hand.kind == TK_TOOL && wk.hand.id == TOOL_CABLES);
    // Looking at the same battery: take them off; at the other: clamp.
    standAt(ctx.sim.world, live, wk, b, 1.2);
    said = holdOn(ctx.sim, ia, wk, b, 6, frame);
    ctx.check("the second clamp makes the cable", said == "CABLES ON" && ctx.sim.cables.size() == 1 && !ctx.sim.handCable.on() && !wk.hand.any(), said);
    ctx.run(3.0);
    ctx.check("the dead battery charges from the running one", dead.battery > 0.2, std::to_string(dead.battery));
    dead.occupied = true;
    ctx.runUntil(6, [&] { return !dead.engineOff; }, [&] {});
    ctx.check("...and it starts, even at a flat battery's start", !dead.engineOff);
    // Off again.
    dead.occupied = false;
    standAt(ctx.sim.world, dead, wk, a, 1.2);
    said = holdOn(ctx.sim, ia, wk, a, 6, frame);
    ctx.check("hold E at a clamp: the cables come off into your hands", said == "CABLES OFF" && ctx.sim.cables.empty() && wk.hand.id == TOOL_CABLES, said);
    // Driving off pulls them free.
    standAt(ctx.sim.world, dead, wk, a, 1.2);
    holdOn(ctx.sim, ia, wk, a, 6, frame);
    standAt(ctx.sim.world, live, wk, b, 1.2);
    holdOn(ctx.sim, ia, wk, b, 6, frame);
    ctx.check("cables on again", ctx.sim.cables.size() == 1);
    V3 far = live.frame().com;
    live.respawn(V3(far.x + 12, far.y + 0.5, far.z), 0);
    ctx.run(0.5);
    ctx.check("the vehicles part: the cables come off", ctx.sim.cables.empty());
}

SCENARIO(batteryNet, "net/battery-cables", "Jumper cables between two players' vehicles: the guest, whose vehicle is flat, sees the cables the host clamped on and its vehicle starts from the host's running one; the host sees it too") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    Walker wk;
    Interact ia;
    ia.kit.unlimited = true;
    NetSession host;
    const int port = testPort();
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[2], V3(13.2, 0.5, -8), 0, V3(0.2, 0.2, 0.2));
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
    repairsync::Sender hostOut, guestOut;
    repairsync::Remote hostIn, guestIn;
    bool guestSawCables = false;
    auto frame = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        sim2.drive(false, false, false, false, true, Ctx::kFrame);
        sim2.advance(Ctx::kFrame, 1000);
        for (Car& c : sim2.cars) c.updateStats();
        client.send(sim2, ctx.t);
        if (ctx.sim.handCable.on()) ctx.sim.handCable.hand = wk.p + V3(0, 1.0, 0);
        hostOut.update(ctx.sim, &wk, ia, ctx.t, true, [&](const std::string& t) { host.sendGame(t); });
        Interact none;
        guestOut.update(sim2, nullptr, none, ctx.t, true, [&](const std::string& t) { client.sendGame(t); });
        for (const NetSession::GameMsg& m : host.gameIn)
            if (m.player >= 0 && m.player < net::kMaxPeers) hostIn.onMessage(ctx.sim, m.player, host.players[m.player].name, m.text, ctx.t);
        host.gameIn.clear();
        for (const NetSession::GameMsg& m : client.gameIn)
            if (m.player >= 0 && m.player < net::kMaxPeers) guestIn.onMessage(sim2, m.player, client.players[m.player].name, m.text, ctx.t);
        client.gameIn.clear();
        hostIn.update(ctx.sim, Ctx::kFrame, ctx.t);
        guestIn.update(sim2, Ctx::kFrame, ctx.t);
        guestSawCables |= !sim2.cables.empty();
        usleep(300);
    };
    ctx.run(2.0, frame);
    const int hostCopy = repairsync::carByNetId(ctx.sim, sim2.cars[0].netId);
    const int guestCopy = repairsync::carByNetId(sim2, hostCar.netId);
    ctx.check("each sees the other's vehicle", hostCopy >= 0 && guestCopy >= 0 && ctx.sim.cars[hostCopy].remote && sim2.cars[guestCopy].remote);
    if (hostCopy < 0 || guestCopy < 0) return;
    Car& guestCar = sim2.cars[0];
    guestCar.respawn(V3(13.2, sim2.world.height(13.2, -8) + 0.5, -8), 0);  // (parked beside the host's, a cable's length apart)
    guestCar.handbrake = 1;
    ctx.run(2.0, frame);
    guestCar.engineOff = true, guestCar.battery = 0.02, guestCar.occupied = true;
    ctx.run(2.0, frame);
    ctx.check("the guest's flat vehicle won't start; the host sees it flat", guestCar.engineOff && ctx.sim.cars[hostCopy].engineOff && ctx.sim.cars[hostCopy].battery < 0.2);
    ctx.check("the host's runs, and the guest sees it so", !hostCar.engineOff && !sim2.cars[guestCopy].engineOff);
    // The host clamps its cables on both batteries (the guest's is a remote vehicle here).
    wk.hand = TrunkRef{TK_TOOL, TOOL_CABLES};
    const V3 a = battery::aim(hostCar), b = battery::aim(ctx.sim.cars[hostCopy]);
    standAt(ctx.sim.world, hostCar, wk, a, 1.2);
    std::string said = holdOn(ctx.sim, ia, wk, a, 6, frame);
    ctx.check("host: first clamp", said.find("CLAMPED") == 0, said);
    standAt(ctx.sim.world, ctx.sim.cars[hostCopy], wk, b, 1.2);
    said = holdOn(ctx.sim, ia, wk, b, 6, frame);
    ctx.check("host: second clamp on the buddy's battery", said == "CABLES ON", said);
    ctx.run(1.0, frame);
    ctx.check("the guest sees the cables between the two vehicles", guestSawCables && sim2.cables.size() == 1 && sim2.cables[0].owner == 0);
    ctx.runUntil(10, [&] { return !guestCar.engineOff; }, frame);
    ctx.check("the guest's vehicle starts from the host's running one", !guestCar.engineOff && guestCar.battery > 0.1);
    ctx.run(1.0, frame);
    ctx.check("the host sees it running", !ctx.sim.cars[hostCopy].engineOff);
    // Taken off: gone for the guest too.
    wk.active = false;
    const V3 b2 = battery::aim(ctx.sim.cars[hostCopy]);
    standAt(ctx.sim.world, ctx.sim.cars[hostCopy], wk, b2, 1.2);
    said = holdOn(ctx.sim, ia, wk, b2, 6, frame);
    ctx.check("host takes them off", said == "CABLES OFF", said);
    ctx.run(1.0, frame);
    ctx.check("...and the guest's copy goes", sim2.cables.empty());
    // And the pack on the guest's vehicle from the host.
    guestCar.engineOff = true, guestCar.battery = 0.02;
    ctx.run(0.5, frame);
    wk.hand = TrunkRef{TK_TOOL, TOOL_JUMP_PACK};
    standAt(ctx.sim.world, ctx.sim.cars[hostCopy], wk, b2, 1.2);
    said = holdOn(ctx.sim, ia, wk, b2, 8, frame);
    ctx.check("host: the pack on a buddy's battery", said.find("JUMP PACK ON") == 0, said);
    ctx.run(1.0, frame);
    ctx.check("the buddy's battery comes up on their machine", guestCar.battery >= battery::kPackLevel - 0.1, std::to_string(guestCar.battery));
}

SCENARIO(batteryFormat, "net/battery-format", "The cable and jump messages read safely: junk ignored, nothing about a vehicle that isn't there, no jump on someone else's vehicle") {
    Car& mine = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Car& theirs = ctx.add(vehicleCatalog()[2], -10, -8, 0);
    mine.netId = (0u << 16) | 1, theirs.netId = (2u << 16) | 1;
    theirs.remote = true;
    ctx.run(0.3);
    repairsync::Remote in;
    const std::string cab = "cable " + std::to_string(mine.netId) + " " + std::to_string(theirs.netId);
    in.onMessage(ctx.sim, 2, "C", cab, 0);
    ctx.check("a cable between two known vehicles is taken", ctx.sim.cables.size() == 1 && ctx.sim.cables[0].owner == 2);
    in.onMessage(ctx.sim, 2, "C", "cable " + std::to_string(mine.netId) + " 999999", 0);
    ctx.check("...a new one replaces it; a vehicle that isn't there leaves none", ctx.sim.cables.empty());
    in.onMessage(ctx.sim, 2, "C", cab, 0);
    in.update(ctx.sim, 0.1, repairsync::kCableExpiry + 1);
    ctx.check("unheard, it comes off", ctx.sim.cables.empty());
    in.onMessage(ctx.sim, 2, "C", "cable nan nan", 0);
    in.onMessage(ctx.sim, 2, "C", "cable 1 1", 0);
    ctx.check("junk and a vehicle to itself: none", ctx.sim.cables.empty());
    mine.engineOff = true, mine.battery = 0.01;
    theirs.engineOff = true, theirs.battery = 0.01;
    in.onMessage(ctx.sim, 2, "C", "jump " + std::to_string(mine.netId), 0);
    ctx.check("a jump on one of mine brings its battery up", mine.battery >= battery::kPackLevel - 0.01);
    in.onMessage(ctx.sim, 0, "A", "jump " + std::to_string(theirs.netId), 0);
    ctx.check("a jump on a vehicle that's theirs here is left to its owner", theirs.battery < 0.1);
}
