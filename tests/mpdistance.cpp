// How far apart players may get on the procedural world (src/mpdistance.h): the warning, the countdown, being brought back, what the
// window does about it. Real host and guests, as net/mp-far-respawn has them.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../src/mpdistance.h"
#include "../src/net.h"
#include "../src/procworld.h"
#include "harness.h"

using namespace proc;
using namespace mpdist;

namespace {
const VehicleSpec& p4Runner() { return vehicleCatalog()[3]; }

World windowAt(uint64_t seed, double gx, double gz) {
    World::procSeed = seed;
    World::procOriginSet = true;
    World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
    World w(MAP_PROCEDURAL);
    World::procOriginSet = false;
    return w;
}

Body body(int peer, double x, double z, double vx = 0, double vz = 0) {
    Body b;
    b.peer = peer, b.p = V3(x, 0, z), b.vel = V3(vx, 0, vz);
    return b;
}

bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

// A guest: its own machine, its own sim, its end of the rules.
struct Player {
    NetSession net;
    Sim sim;
    Guest guest;
    int moved = 0;
    void frame(double t) {
        net.receive(sim, t);
        for (const NetSession::GameMsg& m : net.gameIn) guest.hear(sim, net, m);
        net.gameIn.clear();
        int car = -1;
        moved += guest.apply(sim, net, car);
        guest.tick(Ctx::kFrame);
        sim.advance(Ctx::kFrame, 1000);
        net.send(sim, t);
    }
    V3 at() const { return sim.cars[sim.player].frame().com; }
    // Parked at that x, on the nearest level open ground along z (these hillsides would have it sliding away; the distances are along x).
    void put(double x, double z) {
        const World& w = sim.world;
        auto level = [&](double q) {
            const double h = w.height(x, q);
            for (double a : {-6.0, 6.0})
                if (std::fabs(w.height(x + a, q) - h) > 1.0 || std::fabs(w.height(x, q + a) - h) > 1.0 || w.clearY(x + a, q) - w.height(x + a, q) > 0.2 || w.clearY(x, q + a) - w.height(x, q + a) > 0.2) return false;
            return w.waterLevel(x, q) < h;
        };
        double at = z;
        for (double k = 0; k < 500 && !level(at); k += 8) at = level(z + k) ? z + k : z - k;
        sim.teleportCar(sim.player, x, at, 0);
    }
};

std::unique_ptr<Player> joinUp(Ctx& ctx, NetSession& host, int port, const char* name, const V3& near, double dx) {
    auto p = std::make_unique<Player>();
    bool joined = false, done = false;
    std::thread joiner([&] {
        joined = p->net.join("127.0.0.1:" + std::to_string(port), name, 3.0);
        done = true;
    });
    for (int k = 0; k < 400 && !done; k++) host.receive(ctx.sim, ctx.t), usleep(5000);
    joiner.join();
    ctx.check(std::string(name) + " let in", joined, p->net.error);
    if (!joined) return nullptr;
    p->sim.world = windowAt(p->net.proc.seed, (double)p->net.proc.ox, (double)p->net.proc.oz);
    p->sim.setupEmpty();
    p->sim.addCar(p4Runner(), V3(near.x + dx, p->sim.world.clearY(near.x + dx, near.z + 8) + 0.6, near.z + 8), 0, V3(0.2, 0.2, 0.2));
    p->sim.player = 0;
    p->net.adopt(p->sim);
    p->sim.cars[0].occupied = true;
    return p;
}

// Land under a place is the land: what a window built round it has there, not a window's edge stretched out.
double offLand(const Sim& s, const V3& p) {
    const V3 g = ProcWorld::toGlobal(s.world, p);
    World there = windowAt(s.world.proc.seed, g.x, g.z);
    const V3 q = ProcWorld::toLocal(there, g);
    return std::fabs(s.world.height(p.x, p.z) - there.height(q.x, q.z));
}

bool inWindow(const World& w, const V3& p, double margin) { return std::fabs(p.x) < w.halfSize() - margin && std::fabs(p.z) < w.halfSize() - margin; }

const Car* shownOf(const Sim& s, uint32_t id) {
    for (const Car& c : s.cars)
        if (c.netId == id) return &c;
    return nullptr;
}

// The window as main.cpp and server_main.cpp slide it, in numbers: told kLead ahead, rounded to a chunk, swapped on time. Returns the
// farthest anyone was from the window's middle while `go` moved them.
double slideRun(std::vector<Body> b, double seconds, const std::function<void(std::vector<Body>&, double)>& go) {
    double centre[2] = {0, 0}, next[2] = {0, 0}, due = -1, worst = 0;
    const double dt = 1.0 / 60;
    for (double t = 0; t < seconds; t += dt) {
        go(b, dt);
        if (due >= 0 && t >= due) centre[0] = next[0], centre[1] = next[1], due = -1;
        std::vector<Body> local = b;
        for (Body& o : local) {
            o.p.x -= centre[0], o.p.z -= centre[1];
            worst = std::max(worst, std::max(std::fabs(o.p.x), std::fabs(o.p.z)));
        }
        V3 f;
        bool edge = false;
        if (due < 0 && focusOf(local, f, edge) && (edge || std::max(std::fabs(f.x), std::fabs(f.z)) >= kSlide)) {
            next[0] = std::floor((centre[0] + f.x) / kChunk + 0.5) * kChunk, next[1] = std::floor((centre[1] + f.z) / kChunk + 0.5) * kChunk;
            due = t + kLead;
        }
    }
    return worst;
}
}  // namespace

static_assert(kWarn < kLimit && kLimit < kHard, "the banner first");
static_assert(kHard / 2 + kChunk / 2 < kWindowHalf - 200, "at rest, everyone inside homeSpot's margin");

SCENARIO(netMpDistanceRules, "net/mp-distance-rules", "Who's too far and where the window goes: the group is the biggest set that fits (a host's has the host in it); of two as big, the one it was; the numbers hold a convoy and a runaway on the land at speed") {
    auto bit = [](int p) { return 1u << p; };
    ctx.check("a host and a guest inside the limit: one group", groupOf({body(0, 0, 0), body(1, kLimit - 10, 0)}, 0, 0) == (bit(0) | bit(1)));
    ctx.check("past it: the host's the group", groupOf({body(0, 0, 0), body(1, kLimit + 10, 0)}, 0, 0) == bit(0));
    ctx.check("even with more guests off together than with the host", groupOf({body(0, 0, 0), body(1, 3000, 0), body(2, 3010, 0)}, 0, 0) == bit(0));
    ctx.check("the distance is the window's: along an axis, a diagonal's no further", groupOf({body(0, 0, 0), body(1, kLimit - 10, kLimit - 10)}, 0, 0) == (bit(0) | bit(1)));
    ctx.check("server: two together beat a straggler", groupOf({body(1, 0, 0), body(2, 30, 0), body(3, 3000, 0)}, -1, 0) == (bit(1) | bit(2)));
    ctx.check("server: a straggler can't take the group with them, whichever way", groupOf({body(1, -2500, 0), body(2, 0, 0), body(3, 30, 0), body(4, 60, 20)}, -1, bit(1)) == (bit(2) | bit(3) | bit(4)));
    ctx.check("server: of two as big, the one it was", groupOf({body(1, 0, 0), body(2, 3000, 0)}, -1, bit(2)) == bit(2) && groupOf({body(1, 0, 0), body(2, 3000, 0)}, -1, bit(1)) == bit(1));
    ctx.check("server: with nothing to go on, the one nearest the window's middle", groupOf({body(1, 200, 0), body(2, 2600, 0)}, -1, 0) == bit(1));
    const std::vector<Body> three = {body(0, 0, 0), body(1, 100, 0), body(2, 2500, 0)};
    const std::vector<double> d = distances(three, bit(0) | bit(1));
    ctx.check("distances: from the farthest of the group", std::fabs(d[0] - 100) < 1e-9 && std::fabs(d[1] - 100) < 1e-9 && std::fabs(d[2] - 2500) < 1e-9);
    V3 f;
    bool edge = false;
    ctx.check("1 km apart: the middle of the two", focusOf({body(0, 0, 0), body(1, 1000, 0)}, f, edge) && std::fabs(f.x - 500) < 1e-9 && !edge);
    ctx.check("a group at rest at the limit, in the middle: no slide for nothing", focusOf({body(0, -1100, 0), body(1, 1100, 0)}, f, edge) && !edge);
    ctx.check("a player near the edge with the middle well off: a slide, before kSlide says", focusOf({body(0, -850, 0), body(1, 1300, 0)}, f, edge) && edge && std::fabs(f.x) < kSlide);
    ctx.check("alone: the window follows as ever", focusOf({body(0, 300, 0, 80, 0)}, f, edge) && std::fabs(f.x - 300) < 1e-9 && !edge);
    ctx.check("nobody: nowhere", !focusOf({}, f, edge));
    // At speed, with slides that take their kLead. A convoy strung out to the limit, then a guest leaving a parked host until kHard.
    const double land = kWindowHalf, margin = land - 200;
    for (double v : {30.0, 68.0}) {
        const double w = slideRun({body(0, -kLimit / 2, 0, v, 0), body(1, kLimit / 2, 0, v, 0)}, 120, [&](std::vector<Body>& b, double dt) {
            for (Body& o : b) o.p.x += v * dt;
        });
        ctx.metric("convoy at the limit, " + std::to_string((int)v) + " m/s: farthest from the window's middle", w, "m", 0, margin + 1);
    }
    {
        const double v = 130;
        const double w = slideRun({body(0, 0, 0), body(1, 0, 0, v, 0)}, kHard / v, [&](std::vector<Body>& b, double dt) { b[1].p.x += v * dt; });
        ctx.metric("a guest leaving a parked host at 130 m/s, until kHard: farthest from the window's middle", w, "m", 0, land);
    }
    {
        const double v = 100;  // (leaving each other: the middle doesn't move)
        const double w = slideRun({body(0, 0, 0, -v, 0), body(1, 0, 0, v, 0)}, kHard / (2 * v), [&](std::vector<Body>& b, double dt) { b[0].p.x -= v * dt, b[1].p.x += v * dt; });
        ctx.metric("two leaving each other at 100 m/s each, until kHard", w, "m", 0, kHard / 2 + 10);
    }
}

SCENARIO(netMpDistanceOff, "net/mp-distance-off", "Single player and the fixed maps: nothing is measured, said or moved, however far apart") {
    Car& a = ctx.add(p4Runner(), 0, 0, 0);
    Car& b = ctx.add(p4Runner(), 60, 0, 0);
    ctx.focus(a);
    b.remote = true, b.occupied = true, b.netId = (1u << 16) | 7;
    ctx.check("(this map isn't the procedural one)", ctx.sim.world.map != MAP_PROCEDURAL);
    NetSession host;
    const int port = 29800 + (int)(getpid() % 100);
    ctx.check("host opens a session on a fixed map", host.host(port, "HOSTY", ctx.sim.world.map), host.error);
    host.players[1].active = true, host.players[1].name = "BUDDY";
    b.translate(V3(5000, 0, 0));
    Referee r;
    const V3 me = a.frame().com;
    for (int k = 0; k < 40; k++) r.update(ctx.sim, host, k * 1.0, &me);
    V3 f;
    bool edge = false;
    ctx.check("fixed map: the referee has nothing to say and nowhere to send the window", r.events.empty() && !r.focus(f, edge));
    host.players[1] = NetPlayer();
    host.close();
    Guest g;
    NetSession off;
    NetSession::GameMsg m{0, "mpdist 1 back 7 0 0 0", 0};
    off.self = 1;
    ctx.check("the words are taken and ignored on a fixed map", g.hear(ctx.sim, off, m) && !has(g.banner(), "TOO FAR"));
    int car = -1;
    ctx.check("and nothing moves", !g.apply(ctx.sim, off, car));
    // Single player on the procedural world: no session, no referee.
    Sim s;
    s.world = windowAt(6060, 0, 0);
    s.setupEmpty();
    s.addCar(p4Runner(), V3(0, s.world.clearY(0, 0) + 0.6, 0), 0, V3(0.2, 0.2, 0.2));
    s.player = 0;
    NetSession none;
    const V3 at = s.cars[0].frame().com;
    r.update(s, none, 100, &at);
    ctx.check("single player: the referee doesn't run", r.events.empty() && !r.focus(f, edge));
}

SCENARIO(netMpDistanceLimit, "net/mp-distance-limit", "A host and a guest on the procedural world: past the warning the guest is told; past the limit it's counted down and brought back to real land beside the host, damage kept, lines let go, and both sides agree; turning back cancels it; under the limit nothing happens") {
    const uint64_t seed = 6060;
    int64_t sx0, sz0;
    double syaw0;
    ProcWorld::forSeed(seed).start(sx0, sz0, syaw0);
    ctx.sim.world = windowAt(seed, (double)sx0, (double)sz0);
    const V3 at = ctx.sim.world.spawn;
    Car& hostCar = ctx.add(p4Runner(), at.x + 12, at.z - 10, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = 29900 + (int)(getpid() % 90);
    ctx.check("host opens a session on the procedural world", host.host(port, "HOSTY", MAP_PROCEDURAL), host.error);
    host.proc.on = true, host.proc.gen = (uint16_t)kGenVersion, host.proc.seed = seed, host.proc.ox = ctx.sim.world.proc.ox, host.proc.oz = ctx.sim.world.proc.oz;
    host.adopt(ctx.sim);
    std::unique_ptr<Player> pl = joinUp(ctx, host, port, "BUDDY", at, -10);
    if (!pl) return;
    Player& me = *pl;
    Referee referee;
    std::vector<std::string> said;
    auto frameBoth = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        for (const NetSession::GameMsg& m : host.gameIn) referee.hear(ctx.sim, host, m, ctx.t);
        host.gameIn.clear();
        const V3 hostAt = ctx.sim.cars[ctx.sim.player].frame().com;
        referee.update(ctx.sim, host, ctx.t, &hostAt);
        for (const std::string& e : referee.events) said.push_back(e);
        referee.events.clear();
        me.frame(ctx.t);
        usleep(300);
    };
    ctx.run(1.5, frameBoth);
    me.sim.cars[0].occupied = true;
    ctx.sim.teleportCar(ctx.sim.player, -800, 0, 0);
    const double hostX = ctx.sim.cars[ctx.sim.player].frame().com.x;
    ctx.run(0.5, frameBoth);

    // A guest that's only just come is left alone (the spawn logic placed it), and under the limit it's left alone for good.
    // (Whether it's been moved is counted, not measured: left on these hillsides a truck slides a way by itself.)
    me.put(hostX + kWarn - 150, 0);
    bool anyBanner = false;
    ctx.run(kJoinGrace + 4, [&] {
        frameBoth();
        anyBanner |= !me.guest.banner().empty();
    });
    ctx.check("under the warning: no banner", !anyBanner, me.guest.banner());
    ctx.check("under the warning: never moved", me.moved == 0);

    // Past the warning: told, with the distance, and nothing else.
    me.put(hostX + kWarn + 60, 0);
    ctx.run(1.5, frameBoth);
    ctx.check("past the warning: the banner", has(me.guest.banner(), "TOO FAR FROM THE GROUP: TURN BACK") && has(me.guest.banner(), "KM"), me.guest.banner());
    ctx.check("(no countdown yet)", !has(me.guest.banner(), "BROUGHT BACK"), me.guest.banner());
    ctx.run(kCountdown + 2, frameBoth);
    ctx.check("past the warning: never moved", me.moved == 0);

    // Past the limit, then back inside before the end: cancelled.
    me.put(hostX + kLimit + 80, 0);
    ctx.run(1.5, frameBoth);
    ctx.check("past the limit: the countdown shows", has(me.guest.banner(), "BROUGHT BACK IN"), me.guest.banner());
    ctx.check("the host hears of it", !said.empty() && has(said.back(), "BUDDY IS TOO FAR"), said.empty() ? "" : said.back());
    me.put(hostX + kLimit - 300, 0);
    ctx.run(0.8, frameBoth);
    ctx.check("turned back: no countdown", !has(me.guest.banner(), "BROUGHT BACK"), me.guest.banner());
    ctx.run(kCountdown + 2, frameBoth);
    ctx.check("turned back: never moved", me.moved == 0);
    me.put(hostX + kWarn - 300, 0);
    ctx.run(1.5, frameBoth);
    ctx.check("well inside again: the banner's gone", me.guest.banner().empty(), me.guest.banner());

    // Past the limit and staying there, dented, and on a strap to a truck of the host's parked out there: a line that would stretch km
    // and tear whatever's on its end.
    me.sim.cars[0].damage = 0.25;
    me.put(hostX + kLimit + 80, 0);
    const int stuck = (int)ctx.sim.cars.size();
    ctx.add(p4Runner(), hostX + kLimit + 80 + 9, me.at().z, 0);
    ctx.run(1.0, frameBoth);
    int guestOnHost = -1;
    for (int i = 0; i < (int)ctx.sim.cars.size(); i++)
        if (ctx.sim.cars[i].remote) guestOnHost = i;
    ctx.check("(the host has the guest's truck)", guestOnHost >= 0);
    if (guestOnHost < 0) return;
    Rope strap;
    strap.kind = ROPE_STRAP;
    strap.a.car = stuck, strap.b.car = guestOnHost;
    strap.length = 14, strap.maxLen = 30, strap.k = 2e5, strap.c = 1e4, strap.breakN = 1e6;
    ctx.sim.ropes.push_back(strap);
    ctx.run(0.5, frameBoth);
    ctx.check("(the strap reached the guest)", me.sim.ropes.size() == 1);
    double top = 0;
    ctx.run(kCountdown + 1, [&] {
        frameBoth();
        for (const Node& n : ctx.sim.cars[stuck].nodes) top = std::max(top, len(n.v));
    });
    ctx.check("brought back once", me.moved == 1, std::to_string(me.moved));
    ctx.check("a banner says what happened", has(me.guest.banner(), "BROUGHT BACK TO THE OTHERS"), me.guest.banner());
    ctx.check("the host hears of it too", !said.empty() && has(said.back(), "BROUGHT BACK TO THE GROUP"), said.empty() ? "" : said.back());
    ctx.run(1.5, frameBoth);
    const V3 p = me.at(), h = ctx.sim.cars[ctx.sim.player].frame().com;
    ctx.metric("back beside the host", apart(p, h), "m", 5, 110);
    ctx.check("in the window, a margin in from its edge", inWindow(me.sim.world, p, Sim::kHomeMargin), std::to_string(p.x) + ", " + std::to_string(p.z));
    ctx.metric("the ground under it vs the land there", offLand(me.sim, p), "m", 0, 0.05);
    double low = 1e9;
    for (const Node& n : me.sim.cars[0].nodes) low = std::min(low, n.p.y - me.sim.world.height(n.p.x, n.p.z));
    ctx.metric("its lowest point over the ground", low, "m", -0.3, 0.6);
    ctx.check("its damage came with it", me.sim.cars[0].damage >= 0.25, std::to_string(me.sim.cars[0].damage));
    const Car* shown = shownOf(ctx.sim, me.sim.cars[0].netId);
    ctx.check("the host sees it", shown != nullptr);
    if (shown) ctx.metric("where the host shows it vs where it is", len(V3(shown->frame().com.x - p.x, 0, shown->frame().com.z - p.z)), "m", 0, 1.5);
    ctx.check("the strap's let go on both sides", ctx.sim.ropes.empty() && me.sim.ropes.empty());
    ctx.check("nothing tore: both trucks whole", ctx.sim.cars[stuck].finite() && me.sim.cars[0].finite());
    ctx.metric("the truck on the strap, fastest any node went", top, "m/s", 0, 30);

    // And that's the end of it: no loop.
    bool again = false;
    ctx.run(kCool + 4, [&] {
        frameBoth();
        again |= has(me.guest.banner(), "TURN BACK");
    });
    ctx.check("no banner after", !again && me.moved == 1, std::to_string(me.moved));

    // A guest that can't be followed for more than a moment: straight past kHard, it still gets the banner first, and it's sent sooner.
    me.put(hostX + kHard + 40, 0);
    const double t0 = ctx.t;
    double seen = -1, back = -1;
    ctx.run(kCountdown + 1, [&] {
        frameBoth();
        if (seen < 0 && has(me.guest.banner(), "TURN BACK")) seen = ctx.t - t0;
        if (back < 0 && me.moved == 2) back = ctx.t - t0;
    });
    ctx.check("past kHard: the banner came first", seen >= 0 && back > seen, std::to_string(seen) + " then " + std::to_string(back));
    ctx.metric("past kHard: brought back after", back, "s", kMinWarned, kCountdown - 1);

    // In the seconds before a slide nobody's sent anywhere: it waits for the new window.
    me.put(hostX + kLimit + 80, 0);
    host.scheduleShift(host.sharedTime(ctx.t) + 60, V3());
    me.net.scheduleShift(host.sharedTime(ctx.t) + 60, V3());
    ctx.run(kCountdown + 1, frameBoth);
    ctx.check("a slide on its way: held", me.moved == 2, std::to_string(me.moved));
    host.shiftNow(), me.net.shiftNow();
    ctx.run(1.5, frameBoth);
    ctx.check("the slide's done: brought back", me.moved == 3, std::to_string(me.moved));
    me.net.close();
    host.close();
}

SCENARIO(netMpDistanceServer, "net/mp-distance-server", "The dedicated server: no host car, so the group is the most players together. One straggler is brought back to them and they keep their land; the window goes with them, not the straggler; someone riding along isn't measured") {
    const uint64_t seed = 6060;
    int64_t sx0, sz0;
    double syaw0;
    ProcWorld::forSeed(seed).start(sx0, sz0, syaw0);
    ctx.sim.world = windowAt(seed, (double)sx0, (double)sz0);
    const V3 at = ctx.sim.world.spawn;
    Car& parked = ctx.add(p4Runner(), at.x + 12, at.z - 10, 0);  // (the server's own: nobody's in it, it doesn't count)
    ctx.focus(parked);
    NetSession server;
    const int port = 29990 + (int)(getpid() % 9);
    ctx.check("server opens a session on the procedural world", server.host(port, "SERVER", MAP_PROCEDURAL), server.error);
    server.proc.on = true, server.proc.gen = (uint16_t)kGenVersion, server.proc.seed = seed, server.proc.ox = ctx.sim.world.proc.ox, server.proc.oz = ctx.sim.world.proc.oz;
    server.adopt(ctx.sim);
    std::unique_ptr<Player> pa = joinUp(ctx, server, port, "ANN", at, -10), pb = pa ? joinUp(ctx, server, port, "BOB", at, -25) : nullptr, pc = pb ? joinUp(ctx, server, port, "CY", at, -40) : nullptr;
    if (!pc) return;
    Player &a = *pa, &b = *pb, &c = *pc;
    Player* all[3] = {&a, &b, &c};
    Referee referee;
    auto frameAll = [&] {
        server.send(ctx.sim, ctx.t);
        server.receive(ctx.sim, ctx.t);
        for (const NetSession::GameMsg& m : server.gameIn) referee.hear(ctx.sim, server, m, ctx.t);
        server.gameIn.clear();
        referee.update(ctx.sim, server, ctx.t, nullptr);
        referee.events.clear();
        for (Player* p : all) p->frame(ctx.t);
        usleep(300);
    };
    ctx.run(1.5, frameAll);
    for (Player* p : all) p->sim.cars[0].occupied = true;
    ctx.run(kJoinGrace + 0.5, frameAll);
    a.put(-1100, 0), b.put(-1080, 12), c.put(-1060, 24);
    ctx.run(1.5, frameAll);
    V3 f;
    bool edge = false;
    ctx.check("together: nobody's told anything", a.guest.banner().empty() && b.guest.banner().empty() && c.guest.banner().empty());
    ctx.metric("together, the window's wanted round them", referee.focus(f, edge) ? std::fabs(f.x + 1080) : 1e9, "m", 0, 300);

    // Cy heads off past the limit. Ann and Bob stay; the window can't leave Cy yet (it's the countdown), and they keep their land.
    c.put(1300, 0);
    bool bothQuiet = true, kept = true;
    ctx.run(1.5, frameAll);
    ctx.check("the straggler gets the countdown", has(c.guest.banner(), "BROUGHT BACK IN"), c.guest.banner());
    ctx.run(kCountdown + 1, [&] {
        frameAll();
        bothQuiet &= a.guest.banner().empty() && b.guest.banner().empty();
        kept &= inWindow(a.sim.world, a.at(), Sim::kHomeMargin) && inWindow(b.sim.world, b.at(), Sim::kHomeMargin);
    });
    ctx.check("the group's told nothing", bothQuiet);
    ctx.check("the group keeps its land throughout", kept);
    ctx.check("the straggler's brought back, once", c.moved == 1 && a.moved == 0 && b.moved == 0, std::to_string(c.moved));
    ctx.run(1.5, frameAll);
    ctx.metric("back with the group", apart(c.at(), a.at()), "m", 5, 130);
    ctx.metric("the ground under it vs the land there", offLand(c.sim, c.at()), "m", 0, 0.05);

    // The window goes where the group is: slid there, on every machine, they're all still on land.
    ctx.check("the window's wanted round the group", referee.focus(f, edge) && std::fabs(f.x - (a.at().x + c.at().x) / 2) < 120 && (edge || std::fabs(f.x) >= kSlide), std::to_string(f.x));
    int64_t ox, oz;
    ProcWorld::snapOrigin((double)ctx.sim.world.proc.ox + f.x, (double)ctx.sim.world.proc.oz + f.z, ox, oz);
    const V3 d((double)(ctx.sim.world.proc.ox - ox), 0, (double)(ctx.sim.world.proc.oz - oz));
    World nw = windowAt(seed, (double)ox, (double)oz);
    ctx.sim.world = std::move(nw), shiftSim(ctx.sim, d), server.scheduleShift(server.sharedTime(ctx.t), d), server.shiftNow();
    for (Player* p : all) {
        World w = windowAt(seed, (double)ox, (double)oz);
        p->sim.world = std::move(w), shiftSim(p->sim, d), p->net.scheduleShift(server.sharedTime(ctx.t), d), p->net.shiftNow();
    }
    ctx.run(1.5, frameAll);
    for (Player* p : all) ctx.check(std::string(p == &a ? "Ann" : p == &b ? "Bob" : "Cy") + " is well inside the new window", inWindow(p->sim.world, p->at(), 600));

    // Past kHard from the group the window lets go of a straggler at once: it doesn't get dragged along for the seconds that's left.
    ctx.run(kCool, frameAll);
    const double gx = a.at().x;
    c.put(gx + kHard - 1 > 1500 ? 1500 : gx + kHard - 1, 0);
    a.put(c.at().x - kHard - 60, 0);
    b.put(c.at().x - kHard - 40, 14);
    ctx.run(0.6, frameAll);
    ctx.metric("past kHard: the window's wanted round the group alone", referee.focus(f, edge) ? std::fabs(f.x - (a.at().x + b.at().x) / 2) : 1e9, "m", 0, 40);
    ctx.run(kCountdown, frameAll);
    ctx.check("and the straggler's brought back", c.moved == 2, std::to_string(c.moved));
    ctx.run(kCool + 1, frameAll);

    // Riding along: Bob's in Ann's truck, his own left far off. He's where she is, so nothing's said to him and it stays there.
    const uint32_t annsCar = a.sim.cars[0].netId;
    b.sim.cars[0].occupied = false;
    b.net.sendGame("ride " + std::to_string(annsCar));
    b.put(a.at().x + kLimit + 300, 0);
    const int bMoved = b.moved;
    bool quiet = true;
    ctx.run(kCountdown + 3, [&] {
        frameAll();
        quiet &= b.guest.banner().empty();
    });
    ctx.check("a passenger isn't measured", quiet && b.moved == bMoved, b.guest.banner());
    for (Player* p : all) p->net.close();
    server.close();
}
