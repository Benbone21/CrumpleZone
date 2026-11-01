// Getting into other vehicles on foot (carentry.h): any one with nobody in it, offline; and in
// multiplayer a buddy's handed over (NetSession::requestTake) or ridden along in, over loopback
// with real sockets: take-over, hand-back, riding along, two asking at once, and a player
// leaving with someone else's.
#include <unistd.h>

#include <cmath>
#include <functional>
#include <memory>
#include <thread>

#include "../src/carentry.h"
#include "../src/interact.h"
#include "../src/net.h"
#include "../src/walker.h"
#include "harness.h"

namespace {

int testPort() { return 29200 + (int)(getpid() % 300); }  // (clear of net, net_security, net_bandwidth and net_time)

int byId(const Sim& s, uint32_t id) {
    for (int i = 0; i < (int)s.cars.size(); i++)
        if (id && s.cars[i].netId == id) return i;
    return -1;
}
// The car of `s` nearest to p (anyone's).
int nearTo(const Sim& s, const V3& p) {
    int best = -1;
    double bd = 1e18;
    for (int i = 0; i < (int)s.cars.size(); i++) {
        const V3 d = s.cars[i].frame().com - p;
        if (len(V3(d.x, 0, d.z)) < bd) bd = len(V3(d.x, 0, d.z)), best = i;
    }
    return bd < 3 ? best : -1;
}
bool hasEvent(const NetSession& n, NetSession::CarEvent::Kind k, std::string* why = nullptr) {
    for (const auto& e : n.carEvents)
        if (e.kind == k) {
            if (why) *why = e.why;
            return true;
        }
    return false;
}

// One more machine in the session: its own sim with one truck, joined over loopback.
struct Guest {
    Sim sim;
    NetSession net;
    bool drive = false;
};

std::unique_ptr<Guest> joinGuest(Ctx& ctx, NetSession& host, int port, const std::string& name, int spec, const std::function<void()>& others) {
    auto g = std::make_unique<Guest>();
    g->sim.world = World(MAP_PROVING_GROUND);
    g->sim.setupEmpty();
    g->sim.addCar(vehicleCatalog()[spec], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    g->sim.player = 0;
    bool joined = false, done = false;
    std::thread joiner([&] {
        joined = g->net.join("127.0.0.1:" + std::to_string(port), name, 3.0);
        done = true;
    });
    for (int k = 0; k < 400 && !done; k++) {
        host.receive(ctx.sim, ctx.t);
        if (others) others();
        usleep(5000);
    }
    joiner.join();
    ctx.check(name + " is let in", joined, g->net.error);
    if (!joined) return nullptr;
    g->net.adopt(g->sim);
    return g;
}

// A host and its guests, a frame at a time (the host's sim is the scenario's).
struct Session {
    Ctx& ctx;
    NetSession host;
    std::vector<std::unique_ptr<Guest>> guests;
    explicit Session(Ctx& c) : ctx(c) {}
    void frame() {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        for (auto& g : guests) {
            if (!g->net.active()) continue;
            g->net.receive(g->sim, ctx.t);
            g->sim.drive(g->drive, false, false, false, !g->drive, Ctx::kFrame);
            g->sim.advance(Ctx::kFrame, 1000);
            for (Car& c : g->sim.cars) c.updateStats();
            g->net.send(g->sim, ctx.t);
        }
        usleep(300);
    }
    void run(double secs, const std::function<void()>& also = nullptr) {
        ctx.run(secs, [&] {
            frame();
            if (also) also();
        });
    }
    bool until(double secs, const std::function<bool()>& done) {
        return ctx.runUntil(secs, done, [&] { frame(); });
    }
    bool open(int port) {
        // Host: its own truck, and a parked one of the world's.
        ctx.add(vehicleCatalog()[2], 10, -8, 0);
        ctx.add(vehicleCatalog()[3], 10, -24, 0);
        ctx.sim.player = 0;
        ctx.focus(ctx.sim.cars[0]);
        ctx.run(0.5);
        if (!ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error)) return false;
        host.adopt(ctx.sim);
        host.send(ctx.sim, 0);
        return true;
    }
    Guest* join(int port, const std::string& name, int spec) {
        auto g = joinGuest(ctx, host, port, name, spec, [&] {
            for (auto& o : guests) o->net.receive(o->sim, ctx.t);
        });
        if (!g) return nullptr;
        guests.push_back(std::move(g));
        run(1.5);
        return guests.back().get();
    }
    void close() {
        for (auto& g : guests) g->net.close();
        host.close();
    }
};

}  // namespace

SCENARIO(enterAnyCar, "entercar/offline", "On foot, any vehicle with nobody in it can be got into at its driver's door: a parked one, a wreck, traffic only once it's stopped; riding along is the passenger's door") {
    Car& mine = ctx.add(vehicleCatalog()[2], 0, 0, 0);
    ctx.add(vehicleCatalog()[3], 0, -12, 0);  // parked
    Car& traffic = ctx.add(vehicleCatalog()[0], 0, -24, 0);
    (void)mine;
    ctx.sim.player = 0;
    ctx.run(1.0);
    ctx.check("your own: get in", carentry::verdict(ctx.sim, 0) == carentry::DRIVE);
    ctx.check("a parked one: get in", carentry::verdict(ctx.sim, 1) == carentry::DRIVE);
    Car& t = ctx.sim.cars[2];
    (void)traffic;
    t.ai = true;
    ctx.launch(t, 8);
    ctx.check("traffic on its way: wait for it to stop", carentry::verdict(ctx.sim, 2) == carentry::WAIT);
    t.ai = false;
    t.brake = 1;
    ctx.runUntil(6, [&] { return len(ctx.sim.cars[2].frame().vel) < 0.3; }, [&] { ctx.sim.cars[2].brake = 1; });
    ctx.sim.cars[2].ai = true;
    ctx.check("stopped traffic: get in", carentry::verdict(ctx.sim, 2) == carentry::DRIVE, std::to_string(len(ctx.sim.cars[2].frame().vel)));

    // Standing at the parked one's driver's door, looking at it: E gets you in.
    Walker wk;
    wk.exitCar(ctx.sim.world, ctx.sim.cars[1]);
    const V3 eye = wk.p + V3(0, 1.62, 0);
    const V3 at = carentry::door(ctx.sim.cars[1], false);
    InteractOption o = Interact::pick(ctx.sim, wk, eye, at - eye);
    ctx.check("at its door: hold E to get in", o.kind == IA_ENTER_CAR && o.car == 1 && !o.blocked, o.prompt);
    Interact ia;
    ia.perform(ctx.sim, wk, o);
    ctx.check("E done: the game is told which", ia.enterCar == 1);
    ctx.check("the nearest for J is that one", carentry::nearest(ctx.sim, wk.p) == 1);
    // Out of the passenger's side: on the other side from the driver's.
    Walker pw;
    pw.exitCar(ctx.sim.world, ctx.sim.cars[1], true);
    const Frame f = ctx.sim.cars[1].frame();
    ctx.check("the passenger gets out on the other side", dot(pw.p - f.com, f.left) < 0 && dot(wk.p - f.com, f.left) > 0);
    const V3 pe = carentry::acrossCab(ctx.sim.cars[1], f.com + f.left * 0.4 + f.up);
    ctx.check("the passenger's seat is across the cab", std::fabs(dot(pe - f.com, f.left) + 0.4) < 1e-6);
    // A wreck: no fuel, a flat, a broken shaft: still yours to get into (and tow).
    Car& wreck = ctx.add(vehicleCatalog()[1], 20, 0, 0);
    wreck.fuel = 0;
    wreck.wheels[1].popped = true;
    ctx.run(0.5);
    ctx.check("a wreck: get in", carentry::verdict(ctx.sim, (int)ctx.sim.cars.size() - 1) == carentry::DRIVE);
}

SCENARIO(netTakeOver, "net/take-over", "A buddy's empty truck changes hands: asked for, handed over with its shape, driven and seen moving by the others, refused while someone's in it, and handed back") {
    Session s(ctx);
    const int port = testPort();
    if (!s.open(port)) return;
    Guest* a = s.join(port, "ALICE", 4);
    if (!a) return;
    ctx.check("the guest sees the host's two", a->sim.cars.size() == 3, std::to_string(a->sim.cars.size()));
    const V3 parkedAt = ctx.sim.cars[1].frame().com;
    const int ai = nearTo(a->sim, parkedAt);
    if (!ctx.check("the guest has the parked one", ai >= 0 && a->sim.cars[ai].remote)) return;
    const uint32_t oldId = a->sim.cars[ai].netId;
    ctx.check("nobody's in it: take it over", carentry::verdict(a->sim, ai) == carentry::TAKE);
    ctx.check("asked", a->net.requestTake(a->sim, ai));
    ctx.check("one ask at a time", !a->net.requestTake(a->sim, ai));
    std::vector<V3> before;
    for (const Node& n : ctx.sim.cars[1].nodes) before.push_back(n.p);
    bool got = false;
    s.until(3, [&] {
        got = got || hasEvent(a->net, NetSession::CarEvent::GOT);
        a->net.carEvents.clear();
        return got;
    });
    if (!ctx.check("handed over", got)) return;
    const Car& taken = a->sim.cars[ai];
    ctx.check("the guest's now, under its id", !taken.remote && NetSession::ownerOf(taken.netId) == a->net.self && taken.netId != oldId);
    double worst = 0;
    for (size_t k = 0; k < taken.nodes.size() && k < before.size(); k++) worst = std::max(worst, len(taken.nodes[k].p - before[k]));
    ctx.metric("its shape as handed over vs the owner's", worst, "m", 0, 0.15);
    const int hi = byId(ctx.sim, taken.netId);
    ctx.check("the host has it as the guest's", hi == 1 && ctx.sim.cars[hi].remote, std::to_string(hi));
    ctx.check("nothing doubled or lost", ctx.sim.cars.size() == 3 && a->sim.cars.size() == 3);
    ctx.check("the host was told it's gone", hasEvent(s.host, NetSession::CarEvent::GAVE));
    s.host.carEvents.clear();

    // The guest drives it off; the host's copy follows.
    a->sim.player = ai;
    a->sim.cars[ai].handbrake = 0;
    a->sim.cars[ai].occupied = true;
    a->drive = true;
    s.run(3.0);
    a->drive = false;
    ctx.metric("the host's copy moved with it", len(ctx.sim.cars[hi].frame().com - parkedAt), "m", 5, 200);
    ctx.metric("...and matches it", len(ctx.sim.cars[hi].frame().com - a->sim.cars[ai].frame().com), "m", 0, 3);
    // Someone's in it: the host may ride along, not take it.
    s.run(0.5);
    ctx.check("driven: the host's copy says someone's in it", ctx.sim.cars[hi].occupied && carentry::verdict(ctx.sim, hi) == carentry::RIDE);
    ctx.check("the host asks anyway", s.host.requestTake(ctx.sim, hi));
    std::string why;
    s.until(3, [&] { return hasEvent(s.host, NetSession::CarEvent::REFUSED, &why); });
    ctx.check("refused while someone's in it", why == "SOMEONE'S IN IT", why);
    s.host.carEvents.clear();
    ctx.check("still the guest's", !a->sim.cars[ai].remote);

    // The guest gets out and stops: the host takes it back.
    a->sim.cars[ai].occupied = false;
    a->sim.player = 0;
    // (On its handbrake from ~47 km/h it takes ~4-4.5 s to stop: 4 s was only just enough.)
    s.until(7, [&] { return len(ctx.sim.cars[hi].frame().vel) < 0.5 && len(a->sim.cars[ai].frame().vel) < 0.5; });
    s.run(0.5);
    ctx.check("empty: take it back", carentry::verdict(ctx.sim, hi) == carentry::TAKE);
    ctx.check("asked for it back", s.host.requestTake(ctx.sim, hi));
    got = false;
    s.until(3, [&] { return got = hasEvent(s.host, NetSession::CarEvent::GOT); });
    ctx.check("handed back", got && !ctx.sim.cars[hi].remote && NetSession::ownerOf(ctx.sim.cars[hi].netId) == 0);
    ctx.check("the guest has it as the host's again", a->sim.cars[ai].remote && a->sim.cars[ai].netId == ctx.sim.cars[hi].netId);
    ctx.check("the guest was told", hasEvent(a->net, NetSession::CarEvent::GAVE));
    s.run(1.0);
    ctx.check("still three each", ctx.sim.cars.size() == 3 && a->sim.cars.size() == 3);
    s.close();
}

SCENARIO(netRideAlong, "net/ride-along", "Riding along in a buddy's while they drive: the guest is told someone's in it, the ride shows the truck at full rate however far the guest's own is, and getting out the empty one can be taken") {
    Session s(ctx);
    const int port = testPort() + 4;
    if (!s.open(port)) return;
    Guest* a = s.join(port, "ALICE", 4);
    if (!a) return;
    // The guest's own truck far off; the host drives its own round.
    {
        Car& own = a->sim.cars[a->sim.player];
        own.placeUpright(a->sim.world, 260, 0, 0);
    }
    ctx.sim.cars[0].occupied = true;
    s.run(1.0, [&] { ctx.sim.cars[0].throttle = 0.4, ctx.sim.cars[0].handbrake = 0, ctx.sim.cars[0].steer = 0.3; });
    const int ri = nearTo(a->sim, ctx.sim.cars[0].frame().com);
    if (!ctx.check("the guest has the host's truck", ri >= 0 && a->sim.cars[ri].remote)) return;
    ctx.check("someone's in it: ride along", carentry::verdict(a->sim, ri) == carentry::RIDE);
    auto rate = [&](double secs) {
        const uint64_t in0 = a->net.bytesIn;
        s.run(secs, [&] { ctx.sim.cars[0].throttle = 0.4, ctx.sim.cars[0].handbrake = 0, ctx.sim.cars[0].steer = 0.3; });
        return (a->net.bytesIn - in0) / secs;
    };
    const double far = rate(2.0);
    a->net.sendGame("ride " + std::to_string(a->sim.cars[ri].netId));  // (what the game says on getting in)
    s.run(0.3);
    const double riding = rate(2.0);
    ctx.metric("riding along: the truck's states vs from afar (bytes in)", riding / std::max(1.0, far), "x", 1.4, 10);
    bool heard = false;
    for (const auto& m : s.host.gameIn) heard |= m.text.rfind("ride ", 0) == 0;
    ctx.check("the host's game hears who's riding with it", heard);
    // The host gets out: it's empty: the passenger could take it over.
    ctx.sim.cars[0].occupied = false;
    ctx.runUntil(5, [&] { return len(ctx.sim.cars[0].frame().vel) < 0.3; }, [&] { s.frame(); ctx.sim.cars[0].throttle = 0, ctx.sim.cars[0].brake = 1; });
    s.run(0.6, [&] { ctx.sim.cars[0].brake = 1; });
    ctx.check("empty now: take it over", carentry::verdict(a->sim, ri) == carentry::TAKE, std::to_string(len(a->sim.cars[ri].frame().vel)));
    s.close();
}

SCENARIO(netTakeContention, "net/take-contention", "Two ask for the same empty truck at once: the host settles it, one gets it, the other is told no, and every machine agrees whose it is") {
    Session s(ctx);
    const int port = testPort() + 8;
    if (!s.open(port)) return;
    Guest* a = s.join(port, "ALICE", 4);
    Guest* b = a ? s.join(port, "BOB", 3) : nullptr;
    if (!b) return;
    s.run(0.5);
    // The host's parked one (the host decides at once: the second ask finds it gone).
    const V3 parkedAt = ctx.sim.cars[1].frame().com;
    const int ia = nearTo(a->sim, parkedAt), ib = nearTo(b->sim, parkedAt);
    if (!ctx.check("both see it", ia >= 0 && ib >= 0)) return;
    ctx.check("both ask", a->net.requestTake(a->sim, ia) && b->net.requestTake(b->sim, ib));
    int got = 0, refused = 0;
    std::string why;
    s.until(4, [&] {
        for (Guest* g : {a, b}) {
            for (const auto& e : g->net.carEvents) {
                got += e.kind == NetSession::CarEvent::GOT;
                if (e.kind == NetSession::CarEvent::REFUSED) refused++, why = e.why;
            }
            g->net.carEvents.clear();
        }
        return got + refused >= 2;
    });
    ctx.check("one gets it, one is told no", got == 1 && refused == 1, std::to_string(got) + " got, " + std::to_string(refused) + " refused: " + why);
    s.run(0.5);
    const uint32_t id = ctx.sim.cars[1].netId;
    const int owner = NetSession::ownerOf(id);
    ctx.check("everyone agrees whose", (owner == 1 || owner == 2) && a->sim.cars[nearTo(a->sim, parkedAt)].netId == id && b->sim.cars[nearTo(b->sim, parkedAt)].netId == id);
    Guest* winner = owner == a->net.self ? a : b;
    Guest* loser = winner == a ? b : a;
    ctx.check("only the winner simulates it", !winner->sim.cars[nearTo(winner->sim, parkedAt)].remote && loser->sim.cars[nearTo(loser->sim, parkedAt)].remote && ctx.sim.cars[1].remote);

    // Now it's a guest's: the other guest asks its owner through the host, and once the host has that ask out with the owner (not
    // before: over a real socket "once the host has it" is a matter of milliseconds, and an owner that answers first makes the
    // second ask "IT'S GONE" instead) the host asks too, and waits its turn.
    const int li = nearTo(loser->sim, parkedAt);
    const uint32_t id1 = ctx.sim.cars[1].netId;
    ctx.check("the other guest asks its new owner", loser->net.requestTake(loser->sim, li));
    ctx.check("the host has that ask out with the owner", s.until(4, [&] { return s.host.takePending(id1); }));
    ctx.check("the host asks too", s.host.requestTake(ctx.sim, 1));
    got = refused = 0;
    s.until(4, [&] {
        for (NetSession* n : {&s.host, &loser->net}) {
            for (const auto& e : n->carEvents) {
                got += e.kind == NetSession::CarEvent::GOT;
                if (e.kind == NetSession::CarEvent::REFUSED) refused++, why = e.why;
            }
            n->carEvents.clear();
        }
        return got + refused >= 2;
    });
    ctx.check("again one gets it, one is told no", got == 1 && refused == 1, std::to_string(got) + " got, " + std::to_string(refused) + " refused: " + why);
    ctx.check("the one told no: someone else was first", why == "SOMEONE ELSE IS GETTING IN", why);
    s.run(0.5);
    const uint32_t id2 = ctx.sim.cars[1].netId;
    int locals = !ctx.sim.cars[1].remote;
    bool agree = true;
    for (auto& g : s.guests) {
        const int k = nearTo(g->sim, parkedAt);
        agree &= k >= 0 && g->sim.cars[k].netId == id2;
        locals += k >= 0 && !g->sim.cars[k].remote;
    }
    ctx.check("everyone agrees, one machine simulates it", agree && locals == 1 && id2 != id);
    s.close();
}

SCENARIO(netTakeDisconnect, "net/take-disconnect", "A player leaves holding a buddy's truck: it stays in the world (the host's now, parked) on every machine, its owner can take it back, and the leaver's own truck goes with them") {
    Session s(ctx);
    const int port = testPort() + 12;
    if (!s.open(port)) return;
    Guest* a = s.join(port, "ALICE", 4);
    Guest* b = a ? s.join(port, "BOB", 3) : nullptr;
    if (!b) return;
    s.run(0.5);
    // Alice takes Bob's truck while he's on foot.
    const V3 bobsAt = b->sim.cars[0].frame().com;
    const int ia = nearTo(a->sim, bobsAt);
    if (!ctx.check("Alice sees Bob's", ia >= 0 && a->sim.cars[ia].remote)) return;
    ctx.check("Alice asks for Bob's", a->net.requestTake(a->sim, ia));
    bool got = false;
    s.until(4, [&] { return got = hasEvent(a->net, NetSession::CarEvent::GOT); });
    if (!ctx.check("Alice has Bob's", got && !a->sim.cars[ia].remote)) return;
    ctx.check("Bob was told", hasEvent(b->net, NetSession::CarEvent::GAVE));
    b->net.carEvents.clear();
    const size_t hostCars = ctx.sim.cars.size();
    s.run(0.5);
    // Alice leaves (her game shuts) with it.
    a->net.close();
    s.run(2.0);
    const int hi = nearTo(ctx.sim, bobsAt);
    ctx.check("Bob's truck is still there on the host", hi >= 0, std::to_string(hi));
    if (hi < 0) return;
    ctx.check("...the host's now, parked", !ctx.sim.cars[hi].remote && NetSession::ownerOf(ctx.sim.cars[hi].netId) == 0);
    ctx.check("Alice's own went with her", ctx.sim.cars.size() == hostCars - 1, std::to_string(ctx.sim.cars.size()) + " of " + std::to_string(hostCars));
    const int bi = nearTo(b->sim, bobsAt);
    ctx.check("Bob still sees his truck, as the host's", bi >= 0 && b->sim.cars[bi].remote && b->sim.cars[bi].netId == ctx.sim.cars[hi].netId);
    if (bi < 0) return;
    ctx.check("Bob sees no more of Alice's", b->sim.cars.size() == ctx.sim.cars.size(), std::to_string(b->sim.cars.size()));
    // Bob takes it back.
    ctx.check("Bob asks for it back", b->net.requestTake(b->sim, bi));
    got = false;
    s.until(4, [&] { return got = hasEvent(b->net, NetSession::CarEvent::GOT); });
    ctx.check("Bob has his truck back", got && !b->sim.cars[bi].remote && NetSession::ownerOf(b->sim.cars[bi].netId) == b->net.self);
    // And a host gone with Bob in his: Bob keeps it (and loses only the others').
    s.host.close();
    s.run(1.0);
    int bobs = 0;
    for (const Car& c : b->sim.cars) bobs += !c.remote;
    ctx.check("the host gone: Bob keeps his own, nothing of anyone else's", bobs >= 1 && b->sim.cars.size() == (size_t)bobs, std::to_string(b->sim.cars.size()));
    s.close();
}
