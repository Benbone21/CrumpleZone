// Multiplayer over loopback: a host and a client, each with its own sim, talking through
// real sockets on this machine.
#include <unistd.h>

#include <cmath>
#include <deque>
#include <thread>

#include "../src/net.h"
#include "harness.h"

namespace {

int testPort() { return 27600 + (int)(getpid() % 300); }

int remoteCount(const Sim& s) {
    int n = 0;
    for (const Car& c : s.cars) n += c.remote;
    return n;
}
const Car* remoteCar(const Sim& s) {
    for (const Car& c : s.cars)
        if (c.remote) return &c;
    return nullptr;
}

}  // namespace

SCENARIO(netTwoPlayers, "net/two-players", "Host and client over loopback: each sees the other's truck move and dent, a strap between them tows, leaving cleans up") {
    // Host: a Bronco on the proving ground.
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);  // (the client parks at z = 6.5 and drives off along +x)
    ctx.focus(hostCar);
    NetSession host;
    const int port = testPort();
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);

    // Client: its own sim with a 4Runner, joining from another "machine".
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
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
    ctx.check("client is let in", joined && client.self == 1, client.error);
    if (!joined) return;
    client.adopt(sim2);
    ctx.check("client got the host's map", client.map == MAP_PROVING_GROUND);

    // Both machines run a frame at a time; the client also keeps a history of where its
    // truck really was, to check what the host sees (~0.1 s behind).
    struct Past {
        double t;
        std::vector<V3> p;
    };
    std::deque<Past> history;
    bool clientDrive = false;
    auto frameBoth = [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        sim2.drive(clientDrive, false, false, false, !clientDrive && false, Ctx::kFrame);
        sim2.advance(Ctx::kFrame, 1000);
        for (Car& c : sim2.cars) c.updateStats();
        client.send(sim2, ctx.t);
        Past p{ctx.t, {}};
        for (const Node& n : sim2.cars[sim2.player].nodes) p.p.push_back(n.p);
        history.push_back(p);
        while (history.size() > 120) history.pop_front();
        usleep(300);  // let loopback deliver
    };
    ctx.run(1.5, frameBoth);
    ctx.metric("host sees the client's truck", remoteCount(ctx.sim), "", 1, 1);
    ctx.metric("client sees the host's truck", remoteCount(sim2), "", 1, 1);
    ctx.check("names", host.players[1].name == "BUDDY" && client.players[0].name == "HOSTY");

    // The client drives off: the host's copy follows, node for node.
    double worstNode = 0, worstCom = 0;
    clientDrive = true;
    ctx.run(4.0, [&] {
        frameBoth();
        const Car* r = remoteCar(ctx.sim);
        if (!r || ctx.t < 2.5) return;
        // Compare with where the client's truck was ~0.1 s ago (the playback delay), the
        // closest history entry.
        // (interpolated between the two frames either side of it).
        const double want = ctx.t - net::kPlaybackDelay - Ctx::kFrame;  // (+ a frame: it's read the frame after it's sent)
        size_t k = 0;
        while (k + 2 < history.size() && history[k + 1].t < want) k++;
        const Past &a = history[k], &b = history[k + 1];
        const double u = clampd((want - a.t) / std::max(1e-9, b.t - a.t), 0, 1);
        double w = 0;
        V3 ca, cb;
        for (size_t i = 0; i < r->nodes.size() && i < a.p.size(); i++) {
            const V3 truth = a.p[i] + (b.p[i] - a.p[i]) * u;
            w = std::max(w, len(r->nodes[i].p - truth));
            ca += r->nodes[i].p, cb += truth;
        }
        worstNode = std::max(worstNode, w);
        worstCom = std::max(worstCom, len(ca - cb) / (double)r->nodes.size());
    });
    clientDrive = false;
    const Car& mine = sim2.cars[sim2.player];
    ctx.metric("client truck speed (it drove)", len(mine.frame().vel) * 3.6, "km/h", 10, 80);
    ctx.metric("host's copy vs the truck 0.1 s earlier: center", worstCom, "m", 0, 0.1);
    ctx.metric("host's copy vs the truck 0.1 s earlier: worst node", worstNode, "m", 0, 0.15);

    // Stop the client next to the host, strap them together from the host, and tow.
    ctx.run(3.0, [&] {
        frameBoth();
        Car& c = sim2.cars[sim2.player];
        c.brake = 1;
    });
    {
        // Put the client's truck right behind the host's for the strap.
        Car& c = sim2.cars[sim2.player];
        const V3 target(10 - 9.5, 0, -8);
        const V3 shift = target - V3(c.frame().com.x, 0, c.frame().com.z);
        for (Node& n : c.nodes) n.p += shift, n.v = V3();
        const double yaw = std::atan2(-c.frame().fwd.z, c.frame().fwd.x);
        c.rotateRigid(V3(0, 1, 0), -yaw);
        c.setDown(sim2.world);
    }
    ctx.run(1.0, frameBoth);
    const int hostIdx = ctx.sim.player;
    std::string msg = ctx.sim.hookStrap(hostIdx);
    if (const Car* r = remoteCar(ctx.sim))
        msg += " (client truck at " + std::to_string(r->frame().com.x) + ", " + std::to_string(r->frame().com.z) + "; really at " +
               std::to_string(sim2.cars[sim2.player].frame().com.x) + ", " + std::to_string(sim2.cars[sim2.player].frame().com.z) + ")";
    ctx.check("host straps to the client's truck", ctx.sim.ropeOn(hostIdx, ROPE_STRAP) >= 0, msg);
    ctx.run(0.5, frameBoth);
    ctx.check("the client knows about the strap", !sim2.ropes.empty());
    const V3 c0 = sim2.cars[sim2.player].frame().com;
    double peak = 0;
    ctx.run(8.0, [&] {
        frameBoth();
        Car& h = ctx.sim.cars[hostIdx];
        const double v = dot(h.frame().vel, h.frame().fwd);
        h.throttle = clampd(0.25 + (3.0 - v) * 0.5, 0, 1);
        h.brake = 0, h.handbrake = 0, h.steer = 0;
        Car& c = sim2.cars[sim2.player];
        c.throttle = 0, c.brake = 0, c.handbrake = 0;  // the buddy rolls along in neutral
        if (!sim2.ropes.empty()) peak = std::max(peak, sim2.ropes[0].shown);
    });
    ctx.metric("client truck towed by the host", len(sim2.cars[sim2.player].frame().com - c0), "m", 10, 40);
    ctx.metric("strap pull on the client's side", peak / 1000, "kN", 0.5, 60);
    ctx.check("strap still on (both sides)", !ctx.sim.ropes.empty() && !sim2.ropes.empty());

    // Traffic: what one truck costs per second.
    const double t0 = ctx.t;
    const uint64_t out0 = client.bytesOut;
    ctx.run(2.0, frameBoth);
    ctx.metric("client upload for one truck", (client.bytesOut - out0) / (ctx.t - t0) / 1024, "KB/s", 5, 120);

    // The client leaves: its truck and the strap go away on the host.
    client.close();
    ctx.run(1.0, [&] {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        usleep(300);
    });
    ctx.metric("remote trucks on the host after the client left", remoteCount(ctx.sim), "", 0, 0);
    ctx.check("strap dropped", ctx.sim.ropes.empty());
    host.close();
}

SCENARIO(netLevel, "net/level", "The host's level reaches the client whole (a big one, in parts, and to someone joining after), and race chatter goes both ways") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = testPort() + 1;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    // A level with 150k characters of terrain edits (needs several parts).
    std::string big = "CRUMPLEZONE LEVEL 1\nname BIG ONE\nmap 0\nmode sprint\n";
    while (big.size() < 150000) big += "heights 500 500 1.00 2.00 3.00 4.00 5.00 6.00 7.00 8.00 9.00 10.00\n";
    host.setLevel(big);  // before anyone's here: the joiner gets it in the welcome
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
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
    ctx.check("client is let in", joined, client.error);
    if (!joined) return;
    client.adopt(sim2);
    auto pump = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            host.send(ctx.sim, ctx.t);
            host.receive(ctx.sim, ctx.t);
            client.receive(sim2, ctx.t);
            client.send(sim2, ctx.t);
            usleep(1000);
        }
    };
    pump(200);
    ctx.check("the level arrived whole", client.levelFresh && client.levelText == big, std::to_string(client.levelText.size()) + " of " + std::to_string(big.size()));
    client.levelFresh = false;
    host.setLevel("CRUMPLEZONE LEVEL 1\nname SMALL\nmap 0\n");
    pump(50);
    ctx.check("a new level replaces it", client.levelFresh && client.levelText.find("SMALL") != std::string::npos);
    client.sendGame("prog 3 12.5");
    host.sendGame("go 1.5");
    pump(50);
    bool hostHeard = false, clientHeard = false;
    for (const auto& m : host.gameIn) hostHeard |= m.player == client.self && m.text == "prog 3 12.5";
    for (const auto& m : client.gameIn) clientHeard |= m.player == 0 && m.text == "go 1.5";
    ctx.check("host hears the client's progress", hostHeard);
    ctx.check("client hears the host's go", clientHeard);
    client.close();
    host.close();
}

SCENARIO(netCrawlers, "net/crawlers", "A host in the rock buggy and a client in the side-by-side: each sees the other as the right vehicle (spec ids), driving and steering, node for node") {
    Car& hostCar = ctx.add(vehicleCatalog()[4], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    const int port = testPort() + 2;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[5], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
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
    ctx.check("client is let in", joined, client.error);
    if (!joined) return;
    client.adopt(sim2);
    auto frameBoth = [&](bool hostDrive, bool clientDrive) {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        ctx.sim.drive(hostDrive, false, false, false, false, Ctx::kFrame);
        sim2.drive(clientDrive, false, false, false, false, Ctx::kFrame);
        sim2.advance(Ctx::kFrame, 1000);
        for (Car& c : sim2.cars) c.updateStats();
        client.send(sim2, ctx.t);
        usleep(300);
    };
    ctx.run(1.5, [&] { frameBoth(false, false); });
    const Car* onHost = remoteCar(ctx.sim);
    const Car* onClient = remoteCar(sim2);
    ctx.check("host sees the side-by-side", onHost && onHost->spec->model == 5, onHost ? onHost->spec->name : "none");
    ctx.check("client sees the rock buggy", onClient && onClient->spec->model == 4, onClient ? onClient->spec->name : "none");
    if (!onHost || !onClient) return;
    ctx.check("same node count as the real vehicle", onHost->nodes.size() == sim2.cars[sim2.player].nodes.size() && onClient->nodes.size() == hostCar.nodes.size());
    // Both drive off along +x (the buggy on the host, the UTV on the client).
    ctx.run(4.0, [&] { frameBoth(true, true); });
    const Car* r = remoteCar(ctx.sim);
    const Car& mine = sim2.cars[sim2.player];
    double worst = 0;
    for (size_t i = 0; i < r->nodes.size(); i++) worst = std::max(worst, len(r->nodes[i].p - mine.nodes[i].p));
    ctx.metric("client's side-by-side speed", len(mine.frame().vel) * 3.6, "km/h", 15, 115);
    ctx.metric("host's buggy speed", len(hostCar.frame().vel) * 3.6, "km/h", 15, 90);
    ctx.metric("host's copy of the side-by-side vs the real one: worst node", worst, "m", 0, 4.0);  // (the copy is ~0.1 s behind: ~2.8 m at 100 km/h)
}

// The bug from real play: a machine that had hosted (its truck kept the host's car id) then
// joined someone else's game. The host dropped its truck as its own, and the joiner rebuilt
// its truck on top of the host's. Every session now starts with fresh ids, and the joiner
// is parked clear of everything already there.
SCENARIO(netStaleIdsAndClearSpawn, "net/rejoin", "A machine that hosted before then joins: each side sees the other's truck, and the joiner starts clear of every vehicle") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    // A parked truck right where the joiner's slot is (the Backcountry's trailhead has one).
    const World& w0 = ctx.sim.world;
    const V3 side(std::sin(w0.spawnYaw), 0, std::cos(w0.spawnYaw));
    const V3 slot = w0.spawn + side * 6.5;
    ctx.sim.addCar(vehicleCatalog()[3], V3(slot.x, w0.clearY(slot.x, slot.z) + 0.5, slot.z), w0.spawnYaw, V3(0.4, 0.4, 0.4));
    ctx.run(0.5);
    NetSession host;
    const int port = testPort() + 40;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    host.send(ctx.sim, 0);  // (the game does this every frame: it's what announces the host's trucks)

    // The joining machine hosted a session of its own first, and left it.
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    {
        NetSession before;
        ctx.check("the joiner hosted before", before.host(port + 1, "OLD", MAP_PROVING_GROUND), before.error);
        before.adopt(sim2);
        before.send(sim2, 0);
        ctx.check("its truck took a host's id then", NetSession::ownerOf(sim2.cars[0].netId) == 0 && sim2.cars[0].netId != 0);
        before.close();
    }
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
    ctx.check("client is let in", joined, client.error);
    if (!joined) return;
    client.adopt(sim2);
    for (int k = 0; k < 120; k++) {
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        client.receive(sim2, ctx.t);
        sim2.advance(Ctx::kFrame, 1000);
        client.send(sim2, ctx.t);
        usleep(300);
        ctx.run(Ctx::kFrame);
    }
    const Car& mine = sim2.cars[sim2.player];
    ctx.check("the joiner's truck has its own id", NetSession::ownerOf(mine.netId) == client.self && !mine.remote);
    ctx.check("the host sees the joiner's truck", remoteCount(ctx.sim) == 1, std::to_string(remoteCount(ctx.sim)));
    ctx.check("the joiner sees the host's trucks", remoteCount(sim2) >= 2, std::to_string(remoteCount(sim2)));
    double nearest = 1e9;
    std::string who;
    for (const Car& o : sim2.cars) {
        if (&o == &mine) continue;
        const double d = len(V3(o.frame().com.x - mine.frame().com.x, 0, o.frame().com.z - mine.frame().com.z));
        if (d < nearest) nearest = d, who = std::string(o.spec->name) + (o.remote ? " remote " : " local ") + std::to_string(NetSession::ownerOf(o.netId));
    }
    ctx.check("(nearest: " + who + ")", true);
    ctx.metric("joiner's start: distance to the nearest vehicle", nearest, "m", 4, 1000);
    client.close();
    host.close();
}

SCENARIO(netFindHost, "net/find", "A game hosted on this machine shows up in the multiplayer page's list of games on the network") {
    NetSession host;
    const int port = testPort() + 43;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_BACKCOUNTRY), host.error);
    host.adopt(ctx.sim);
    net::Finder finder;
    double t = 0;
    for (int k = 0; k < 300 && finder.hosts.empty(); k++) {
        finder.poll(t);
        host.receive(ctx.sim, t);
        usleep(10000);
        t += 0.01;
    }
    ctx.check("found it", !finder.hosts.empty());
    if (finder.hosts.empty()) return;
    const net::FoundHost& h = finder.hosts[0];
    ctx.check("with its name, map and port", h.name == "HOSTY" && h.map == MAP_BACKCOUNTRY &&
                                                 h.address.find(":" + std::to_string(port)) != std::string::npos, h.name + " " + h.address);
    host.close();
}
