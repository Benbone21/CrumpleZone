// Multiplayer over loopback: a host and a client, each with its own sim, talking through
// real sockets on this machine.
#include "../src/horn.h"
#include <unistd.h>

#include <cmath>
#include <deque>
#include <functional>
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
    // truck really was, by the shared clock, to check what the host shows at its moment on it.
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
        Past p{client.sharedTime(ctx.t), {}};
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
        // Compare with where the client's truck was at the moment the host shows (on the
        // shared clock: now, run on from its newest state), interpolated between the two frames either side of it.
        const double want = host.vehicleTime();
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
    ctx.metric("host's copy vs the truck at the moment shown: center", worstCom, "m", 0, 0.1);
    ctx.metric("host's copy vs the truck at the moment shown: worst node", worstNode, "m", 0, 0.15);

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
    ctx.metric("client upload for one truck", (client.bytesOut - out0) / (ctx.t - t0) / 1024, "KB/s", 0.5, 20);  // (only the dents that moved: a whole state is ~3 KB, 60 KB/s)

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
    double t = ctx.t;
    auto pump = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            host.send(ctx.sim, t);
            host.receive(ctx.sim, t);
            client.receive(sim2, t);
            client.send(sim2, t);
            usleep(1000);
            t += 0.002;  // (chatter is held till the others are shown at the moment it was said: time must pass)
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
    pump(150);  // (out once the others are shown at the moment it was said: a view delay later)
    bool hostHeard = false, clientHeard = false;
    for (const auto& m : host.gameIn) hostHeard |= m.player == client.self && m.text == "prog 3 12.5";
    for (const auto& m : client.gameIn) clientHeard |= m.player == 0 && m.text == "go 1.5";
    ctx.check("host hears the client's progress", hostHeard);
    ctx.check("client hears the host's go", clientHeard);
    {  // the horn: "horn 1" / "horn 0" chatter (horn.h), no new message type
        host.gameIn.clear(), client.gameIn.clear();
        client.sendGame(horn::message(true));
        pump(150);
        horn::Remote remote;
        bool on = false;
        for (const auto& m : host.gameIn)
            if (m.player == client.self && horn::parse(m.text, on)) remote.hear(m.player, on, t);
        ctx.check("a guest's horn reaches the host as sounding", remote.sounding(client.self, t));
        ctx.check("...and goes quiet on its own if the guest never says stop", !remote.sounding(client.self, t + horn::kExpiry + 1));
        host.gameIn.clear();
        client.sendGame(horn::message(false));
        pump(150);
        for (const auto& m : host.gameIn)
            if (m.player == client.self && horn::parse(m.text, on)) remote.hear(m.player, on, t);
        ctx.check("the guest letting go silences it", !remote.sounding(client.self, t));
        host.gameIn.clear(), client.gameIn.clear();
        host.sendGame(horn::message(true));
        pump(150);
        bool clientHeardHorn = false;
        for (const auto& m : client.gameIn) clientHeardHorn |= m.player == 0 && horn::parse(m.text, on) && on;
        ctx.check("the host's horn reaches the guest", clientHeardHorn);
        host.sendGame(horn::message(false));
        pump(50);
    }
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

namespace {
// A second "machine": its own sim with one truck.
void otherMachine(Sim& sim2, int map) {
    sim2.world = World(map);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
}
}  // namespace

SCENARIO(netLanDiscovery, "net/lan-discovery", "The network list: a game's own name, the most players, one row per game (heard over the network and on this machine), the count going up when someone joins, gone a few seconds after the host closes") {
    NetSession host;
    const int port = testPort() + 47;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_FRONTIER, 4, "MUD RUN"), host.error);
    host.adopt(ctx.sim);
    net::Finder finder;
    double t = 0;
    auto pump = [&](double secs, const std::function<bool()>& until) {
        for (double end = t + secs; t < end; t += 0.01) {
            finder.poll(t);
            if (host.active()) host.receive(ctx.sim, t);
            if (until && until()) return true;
            usleep(10000);
        }
        return false;
    };
    auto mine = [&]() -> const net::FoundHost* {
        for (const net::FoundHost& h : finder.hosts)
            if (h.address.find(":" + std::to_string(port)) != std::string::npos) return &h;
        return nullptr;
    };
    ctx.check("found within 2 s", pump(2.0, [&] { return mine() != nullptr; }));
    pump(1.5, nullptr);  // (every way it's heard: broadcast, each adapter, this machine, the announcements)
    const net::FoundHost* h = mine();
    if (!h) return;
    int rows = 0;
    for (const net::FoundHost& o : finder.hosts) rows += o.id == h->id;
    ctx.check("one row for the game", rows == 1, std::to_string(rows) + " rows");
    ctx.check("its name, host, map, 1 of 4", h->session == "MUD RUN" && h->name == "HOSTY" && h->map == MAP_FRONTIER && h->players == 1 && h->maxPlayers == 4,
              h->session + " / " + h->name + " " + std::to_string(h->players) + "/" + std::to_string(h->maxPlayers));
    ctx.check("joinable", h->joinable());
    const std::string address = h->address;
    // Someone joins (without waiting: the host keeps running here).
    NetSession client;
    ctx.check("join starts", client.beginJoin(address, "JOINER"), client.error);
    int st = 0;
    pump(5.0, [&] {
        st = client.pollJoin();
        return st != 0;
    });
    ctx.check("joined", st == 1, client.error);
    Sim sim2;
    otherMachine(sim2, MAP_FRONTIER);
    if (st == 1) {
        client.adopt(sim2);
        ctx.check("the list says 2 of 4", pump(3.0, [&] { return mine() && mine()->players == 2; }));
    }
    client.close();
    host.close();
    ctx.check("gone after the host closes", pump(6.0, [&] { return mine() == nullptr; }));
}

SCENARIO(netAsyncJoin, "net/async-join", "Joining without freezing the game: beginJoin + pollJoin get in while the host keeps running; a dead address and a full game say why") {
    NetSession host;
    const int port = testPort() + 53;
    ctx.check("host opens a 2-player session", host.host(port, "HOSTY", MAP_PROVING_GROUND, 2, ""), host.error);
    host.adopt(ctx.sim);
    double t = 0;
    auto joinOne = [&](NetSession& c, const std::string& addr, int& polls) {
        if (!c.beginJoin(addr, "J")) return -1;
        int st = 0;
        for (polls = 0; polls < 800 && st == 0; polls++, t += 0.01) {
            st = c.pollJoin();
            host.receive(ctx.sim, t);
            usleep(5000);
        }
        return st;
    };
    NetSession a, b, dead;
    int polls = 0;
    ctx.check("first joiner gets in", joinOne(a, "127.0.0.1:" + std::to_string(port), polls) == 1, a.error);
    ctx.check("(and pollJoin returned straight away each time: " + std::to_string(polls) + " polls)", polls > 1);
    Sim sim2;
    otherMachine(sim2, MAP_PROVING_GROUND);
    if (a.active()) a.adopt(sim2);
    ctx.check("a third player is turned away: full", joinOne(b, "127.0.0.1:" + std::to_string(port), polls) == -1 && b.error.find("full") != std::string::npos, b.error);
    ctx.check("nobody listening: no answer", joinOne(dead, "127.0.0.1:" + std::to_string(port + 1), polls) == -1 && !dead.error.empty(), dead.error);
    a.close();
    host.close();
}

// ---- Any port --------------------------------------------------------------------------

namespace {
// A port nothing is listening on right now (the OS picks it; asked for and given back).
int freePort() {
    NetSession probe;
    for (int p = 28123 + (int)(getpid() % 500);; p += 7)
        if (probe.host(p, "PROBE", MAP_PROVING_GROUND)) {
            probe.close();
            return p;
        }
}
}  // namespace

SCENARIO(netPortParse, "net/port-parse", "Ports and addresses: IP, IP:PORT and HOST:PORT parse; no port means 27015; bad, out-of-range and extra-colon forms are refused with a reason") {
    std::string host, why;
    int port = 0;
    auto ok = [&](const std::string& a, const std::string& wantHost, int wantPort) {
        host.clear(), port = 0;
        const bool good = net::parseAddress(a, host, port, &why);
        ctx.check("'" + a + "' parses as " + wantHost + ":" + std::to_string(wantPort), good && host == wantHost && port == wantPort, host + ":" + std::to_string(port));
    };
    auto bad = [&](const std::string& a, const char* about) {
        why.clear();
        ctx.check("'" + a + "' is refused (" + about + ")", !net::parseAddress(a, host, port, &why) && !why.empty());
    };
    ok("1.2.3.4", "1.2.3.4", net::kDefaultPort);
    ok("1.2.3.4:28123", "1.2.3.4", 28123);
    ok("play.example.org", "play.example.org", net::kDefaultPort);
    ok("play.example.org:7", "play.example.org", 7);  // (joining takes any port a host could have forwarded)
    ok("localhost:65535", "localhost", 65535);
    ok("  10.0.0.5:2000 ", "10.0.0.5", 2000);
    ok("1.2.3.4:07015", "1.2.3.4", 7015);
    bad("", "empty");
    bad(":28123", "no host");
    bad("1.2.3.4:", "empty port");
    bad("1.2.3.4:0", "port 0");
    bad("1.2.3.4:65536", "too high");
    bad("1.2.3.4:99999", "too high");
    bad("1.2.3.4:123456", "six digits");
    bad("1.2.3.4:-5", "negative");
    bad("1.2.3.4:+5", "sign");
    bad("1.2.3.4:80x", "junk");
    bad("1.2.3.4:abc", "text");
    bad("1.2.3.4:28123:5", "extra colon");
    bad("::1", "IPv6");
    bad("[::1]:28123", "IPv6");
    ctx.check("parsePort: digits only", net::parsePort("28123") == 28123 && net::parsePort("1") == 1 && net::parsePort("65535") == 65535);
    ctx.check("parsePort: out of range or junk", net::parsePort("0") < 0 && net::parsePort("65536") < 0 && net::parsePort("") < 0 && net::parsePort(" 5") < 0 &&
                                                     net::parsePort("5 ") < 0 && net::parsePort("0x50") < 0 && net::parsePort("99999") < 0);
    ctx.check("hosting wants 1024 and up", net::parsePort("1023", net::kMinHostPort) < 0 && net::parsePort("80", net::kMinHostPort) < 0 &&
                                               net::parsePort("1024", net::kMinHostPort) == 1024 && net::parsePort("27015", net::kMinHostPort) == 27015);
    NetSession s;
    ctx.check("joining a bad port fails before connecting, saying why", !s.beginJoin("127.0.0.1:99999", "X") && s.error.find("port") != std::string::npos, s.error);
    ctx.check("joining with extra colons fails the same way", !s.beginJoin("127.0.0.1:1:2", "X") && !s.error.empty(), s.error);
}

SCENARIO(netCustomPort, "net/custom-port", "A host on a port other than 27015: a client joins it by IP:PORT, a second host on that port is told it is in use, and the LAN list shows it and joins on it") {
    const int port = freePort();
    ctx.check("(not the default)", port != net::kDefaultPort);
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    ctx.check("host opens a session on a custom port", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    ctx.check("and reports it", host.port() == port);
    host.adopt(ctx.sim);

    NetSession clash;
    ctx.check("a second host on the same port fails", !clash.host(port, "CLASH", MAP_PROVING_GROUND), "");
    ctx.check("and knows it is the port that is taken", clash.portInUse && clash.error.find(std::to_string(port)) != std::string::npos &&
                                                         clash.error.find("in use") != std::string::npos, clash.error);
    NetSession elsewhere;
    ctx.check("another port is fine (no flag left over)", elsewhere.host(port + 1, "OTHER", MAP_PROVING_GROUND) && !elsewhere.portInUse, elsewhere.error);
    elsewhere.close();

    // The LAN list: the host announces its real port, and the row joins on it.
    net::Finder finder;
    double t = 0;
    auto pump = [&](double secs, const std::function<bool()>& until) {
        for (double end = t + secs; t < end; t += 0.01) {
            finder.poll(t);
            host.receive(ctx.sim, t);
            if (until && until()) return true;
            usleep(10000);
        }
        return false;
    };
    auto mine = [&]() -> const net::FoundHost* {
        for (const net::FoundHost& h : finder.hosts)
            if (h.address.size() > std::to_string(port).size() && h.address.compare(h.address.size() - std::to_string(port).size() - 1, std::string::npos, ":" + std::to_string(port)) == 0)
                return &h;
        return nullptr;
    };
    ctx.check("the list finds it", pump(3.0, [&] { return mine() != nullptr; }));
    const net::FoundHost* h = mine();
    if (!h) return;
    ctx.check("the announced address carries the real port, not 27015", h->address.find(":27015") == std::string::npos, h->address);
    std::string hostName;
    int p = 0;
    ctx.check("and parses back to it", net::parseAddress(h->address, hostName, p) && p == port, h->address);

    // Joining on exactly that address (what the list's row does).
    Sim sim2;
    sim2.world = World(MAP_PROVING_GROUND);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
    NetSession client;
    ctx.check("join starts", client.beginJoin(h->address, "LISTED"), client.error);
    int st = 0;
    pump(5.0, [&] {
        st = client.pollJoin();
        return st != 0;
    });
    ctx.check("joined through the list's address", st == 1 && client.self == 1, client.error);
    if (st == 1) client.adopt(sim2);
    client.close();

    // And by hand, IP:PORT and HOST:PORT, with the join loop.
    for (const char* hostText : {"127.0.0.1", "localhost"}) {
        NetSession typed;
        bool joined = false, done = false;
        std::thread joiner([&] {
            joined = typed.join(std::string(hostText) + ":" + std::to_string(port), "TYPED", 3.0);
            done = true;
        });
        for (int k = 0; k < 400 && !done; k++) {
            host.receive(ctx.sim, t += 0.005);
            usleep(5000);
        }
        joiner.join();
        ctx.check(std::string("typing ") + hostText + ":" + std::to_string(port) + " joins", joined, typed.error);
        typed.close();
    }
    NetSession wrong;
    ctx.check("the next port up has no game: refused, not hanging", !wrong.join("127.0.0.1:" + std::to_string(port + 2), "LOST", 1.0) && !wrong.error.empty(), wrong.error);
    host.close();
}
