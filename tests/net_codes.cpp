// Join codes end to end over loopback: the backend (rv::Server), a host that opens its game to
// friends, and guests that join with the code. They go straight through, through a simulated
// NAT (a port-restricted cone: the punch gets through), and through a symmetric NAT (it can't, so
// the relay carries everything). Also the plain-words failures, the password, a kick, the host
// leaving, closing to friends, the tunnel's stream under loss.
#include <unistd.h>

#include <map>
#include <mutex>
#include <random>
#include <set>

#include "../src/net.h"
#include "../src/netsock.h"
#include "../src/nettunnel.h"
#include "../src/rendezvous.h"
#include "harness.h"

struct NetSessionCodeTest {
    static int udp(const NetSession& s) { return s.udpFd; }
};

namespace {

// A NAT in front of chosen sockets: what they send goes out from a "public" socket of the NAT's
// (one for every destination when it's symmetric, else one for all), and only what comes back
// from an address:port they sent to gets in (port-restricted). One for the whole test runner
// (the scenarios run on threads): boxes by socket, forgotten when the socket isn't theirs anymore.
struct SimNat : net::UdpHook {
    struct Box {
        uint16_t insidePort = 0;
        bool symmetric = false;
        int cone = -1;
        std::map<std::pair<uint32_t, uint16_t>, int> byDest;
        std::set<std::pair<uint32_t, uint16_t>> allowed;
        uint64_t sent = 0, refusedIn = 0;
    };
    std::mutex m;
    std::map<int, Box> boxes;
    static SimNat& get() {
        static SimNat nat;
        net::udpHook = &nat;
        return nat;
    }
    void add(int fd, bool symmetric) {
        std::lock_guard<std::mutex> l(m);
        Box b;
        b.insidePort = boundPort(fd);
        b.symmetric = symmetric;
        drop(fd);
        boxes[fd] = b;
    }
    void drop(int fd) {  // (lock held)
        auto it = boxes.find(fd);
        if (it == boxes.end()) return;
        if (it->second.cone >= 0) closeSock(it->second.cone);
        for (auto& kv : it->second.byDest) closeSock(kv.second);
        boxes.erase(it);
    }
    void remove(int fd) {
        std::lock_guard<std::mutex> l(m);
        drop(fd);
    }
    Box* boxFor(int fd) {  // (lock held)
        auto it = boxes.find(fd);
        if (it == boxes.end()) return nullptr;
        if (boundPort(fd) != it->second.insidePort) {  // (closed, and the number reused by someone else)
            drop(fd);
            return nullptr;
        }
        return &it->second;
    }
    bool send(int fd, const uint8_t* p, size_t n, uint32_t ip, uint16_t port) override {
        std::lock_guard<std::mutex> l(m);
        Box* b = boxFor(fd);
        if (!b) return false;
        int& pub = b->symmetric ? b->byDest[{ip, port}] : b->cone;
        if (b->symmetric && pub == 0) pub = -1;
        if (pub < 0) pub = udpOpen(0x7F000001, 0);
        b->allowed.insert({ip, port});
        b->sent++;
        const sockaddr_in a = sockAddr(ip, port);
        sendto(pub, (const char*)p, (int)n, 0, (const sockaddr*)&a, sizeof a);
        return true;
    }
    int recv(int fd, uint8_t* buf, size_t cap, uint32_t& ip, uint16_t& port) override {
        std::lock_guard<std::mutex> l(m);
        Box* b = boxFor(fd);
        if (!b) return 0;
        std::vector<int> pubs;
        if (b->cone >= 0) pubs.push_back(b->cone);
        for (auto& kv : b->byDest) pubs.push_back(kv.second);
        for (int pub : pubs)
            for (;;) {
                sockaddr_in from{};
                socklen_t fl = sizeof from;
                const int k = (int)recvfrom(pub, (char*)buf, (int)cap, 0, (sockaddr*)&from, &fl);
                if (k <= 0) break;
                const uint32_t fip = ntohl(from.sin_addr.s_addr);
                const uint16_t fport = ntohs(from.sin_port);
                // Symmetric: a mapping lets in only the one destination it was made for.
                bool ok = b->allowed.count({fip, fport}) > 0;
                if (b->symmetric) {
                    ok = false;
                    for (auto& kv : b->byDest)
                        if (kv.second == pub && kv.first == std::make_pair(fip, fport)) ok = true;
                }
                if (!ok) {
                    b->refusedIn++;
                    continue;
                }
                ip = fip, port = fport;
                return k;
            }
        return -1;  // (nothing reaches the inside socket but through the NAT)
    }
};

int basePort() { return 29100 + (int)(getpid() % 200) * 4; }

// The backend, a host with a truck, and the frames to run them all.
struct Rig {
    Ctx& ctx;
    rv::Server backend;
    NetSession host;
    std::string rvAddr;
    std::vector<NetSession*> guests;
    std::vector<Sim*> sims;
    Rig(Ctx& c, int slot) : ctx(c) {
        std::string err;
        const int rvPort = basePort() + slot * 800 + 1;
        ctx.check("the backend opens", backend.open(rvPort, "127.0.0.1", &err), err);
        rvAddr = "127.0.0.1:" + std::to_string(rvPort);
        host.rendezvous = rvAddr;
        host.shareLocals = false;
        host.punchSecs = 1.5;
    }
    ~Rig() {
        for (NetSession* g : guests) SimNat::get().remove(NetSessionCodeTest::udp(*g));
        SimNat::get().remove(NetSessionCodeTest::udp(host));
        for (NetSession* g : guests) delete g;
        for (Sim* s : sims) delete s;
    }
    NetSession& guest() {
        NetSession* g = new NetSession;
        g->rendezvous = rvAddr;
        g->shareLocals = false;
        g->punchSecs = 1.5;
        guests.push_back(g);
        Sim* s = new Sim;
        s->world = World(MAP_PROVING_GROUND);
        s->setupEmpty();
        s->addCar(vehicleCatalog()[3], V3(-30 - 8.0 * sims.size(), 0.5, -30), 0, V3(0.2, 0.2, 0.2));
        s->player = 0;
        sims.push_back(s);
        return *g;
    }
    Sim& simOf(const NetSession& g) {
        for (size_t i = 0; i < guests.size(); i++)
            if (guests[i] == &g) return *sims[i];
        return *sims[0];
    }
    void frame() {
        backend.poll(net::clockNow());
        host.send(ctx.sim, ctx.t);
        host.receive(ctx.sim, ctx.t);
        for (size_t i = 0; i < guests.size(); i++) {
            NetSession& g = *guests[i];
            if (g.joining() || !g.active()) continue;
            g.receive(*sims[i], ctx.t);
            sims[i]->advance(Ctx::kFrame, 1000);
            g.send(*sims[i], ctx.t);
        }
        usleep(400);
    }
    // Frames until `secs` of real time have passed (the session's timers are on the wall clock).
    void wall(double secs) {
        const double t0 = net::clockNow();
        while (net::clockNow() - t0 < secs) ctx.step(), frame();
    }
    // Opens the host to friends (with a NAT in front, if asked): the code.
    std::string open(int slot, bool nat) {
        const int port = basePort() + slot * 800 + 2;
        if (!ctx.check("the host hosts", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error)) return "";
        host.adopt(ctx.sim);
        if (nat) SimNat::get().add(NetSessionCodeTest::udp(host), false);
        ctx.check("and opens to friends", host.openToFriends(), host.codeError);
        ctx.runUntil(5, [&] { return host.codeState != NetSession::CODE_ASKING; }, [&] { frame(); });
        ctx.check("it gets a code", host.codeState == NetSession::CODE_OPEN && rv::normalizeCode(host.joinCode) == host.joinCode, host.codeError);
        return host.joinCode;
    }
    // A guest joins with the code; -1 failed, 1 in.
    int join(NetSession& g, const std::string& code, int natKind /* 0 none, 1 cone, 2 symmetric */, double maxSecs = 15) {
        if (!g.beginJoinCode(code, "BUDDY")) return -1;
        if (natKind) SimNat::get().add(NetSessionCodeTest::udp(g), natKind == 2);
        int st = 0;
        ctx.runUntil(maxSecs, [&] { return st != 0; }, [&] {
            st = g.pollJoin();
            frame();
        });
        if (st > 0) g.adopt(simOf(g));
        return st;
    }
    int remotes(const Sim& s) const {
        int n = 0;
        for (const Car& c : s.cars) n += c.remote;
        return n;
    }
};

}  // namespace

SCENARIO(netCodeDirect, "net/code-direct", "join with a code over loopback: the backend introduces, the punch gets straight through, both see each other's truck") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    Rig rig(ctx, 0);
    const std::string code = rig.open(0, false);
    NetSession& g = rig.guest();
    ctx.check("the guest gets in with the code", rig.join(g, rv::formatCode(code), 0) > 0, g.error);
    ctx.check("straight through, not the relay", g.myLink() == NetSession::LINK_DIRECT);
    ctx.run(2.5, [&] { rig.frame(); });
    ctx.check("the host sees the guest's truck", rig.remotes(ctx.sim) == 1);
    ctx.check("the guest sees the host's", rig.remotes(rig.simOf(g)) == 1);
    ctx.check("the host lists the guest as direct", rig.host.players[1].active && rig.host.players[1].link == NetSession::LINK_DIRECT && rig.host.players[1].name == "BUDDY");
    ctx.metric("relayed packets", (double)rig.backend.stats.relayedPackets, "", 0, 0);
    // The host leaves: the guest hears so at once.
    rig.host.close();
    ctx.run(0.5, [&] { rig.frame(); });
    bool told = false;
    for (const std::string& e : g.events) told |= e == "THE HOST CLOSED THE GAME";
    ctx.check("the host leaves: the guest is told, in plain words", told && !g.active());
    ctx.run(0.2, [&] { rig.frame(); });
    ctx.check("and the code is gone at the backend", rig.backend.codes() == 0);
}

SCENARIO(netCodeNat, "net/code-nat", "both behind a simulated port-restricted NAT: the punch gets through both, and the truck states go straight") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 1);
    const std::string code = rig.open(1, true);
    NetSession& g = rig.guest();
    ctx.check("in with the code", rig.join(g, code, 1) > 0, g.error);
    ctx.check("punched through both NATs", g.myLink() == NetSession::LINK_DIRECT);
    ctx.run(2.0, [&] { rig.frame(); });
    ctx.check("both see each other", rig.remotes(ctx.sim) == 1 && rig.remotes(rig.simOf(g)) == 1);
    ctx.metric("relayed packets", (double)rig.backend.stats.relayedPackets, "", 0, 0);
}

SCENARIO(netCodeRelay, "net/code-relay", "a guest behind a symmetric NAT can't punch through: the relay carries it all, a level too; a second guest; kick; closing to friends") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 2);
    const std::string code = rig.open(2, true);
    // A level of a few hundred KB, to cross the relay in the welcome.
    std::string level = "# a big level\n";
    while (level.size() < 300000) level += "note padding padding padding padding padding padding padding\n";
    rig.host.setLevel(level);
    NetSession& g = rig.guest();
    ctx.check("in with the code", rig.join(g, code, 2, 20) > 0, g.error);
    ctx.check("through the relay", g.myLink() == NetSession::LINK_RELAYED);
    ctx.run(2.5, [&] { rig.frame(); });
    ctx.check("the level crossed the relay whole", g.levelText == level, std::to_string(g.levelText.size()) + " of " + std::to_string(level.size()));
    ctx.check("both see each other through it", rig.remotes(ctx.sim) == 1 && rig.remotes(rig.simOf(g)) == 1);
    ctx.check("the host lists the guest as relayed", rig.host.players[1].link == NetSession::LINK_RELAYED);
    ctx.metric("relayed packets", (double)rig.backend.stats.relayedPackets, "", 100, 1e9);
    // A second guest, straight through, and the two see each other.
    NetSession& g2 = rig.guest();
    ctx.check("a second guest gets in", rig.join(g2, code, 0) > 0, g2.error);
    ctx.run(2.0, [&] { rig.frame(); });
    ctx.check("everyone sees everyone", rig.remotes(ctx.sim) == 2 && rig.remotes(rig.simOf(g)) == 2 && rig.remotes(rig.simOf(g2)) == 2);
    // Closing to friends: the ones in stay, nobody new.
    rig.host.closeToFriends();
    ctx.run(0.5, [&] { rig.frame(); });
    NetSession& g3 = rig.guest();
    ctx.check("closed: a new guest is told no such game", rig.join(g3, code, 0) < 0 && g3.joinFail == NetSession::JF_NOT_FOUND, g3.error);
    ctx.run(1.0, [&] { rig.frame(); });
    ctx.check("the relayed guest stays in", g.active() && rig.remotes(rig.simOf(g)) == 2);
    // Kick.
    rig.host.kick(1);
    rig.wall(1.0);
    bool told = false;
    for (const std::string& e : g.events) told |= e == "THE HOST REMOVED YOU FROM THE GAME";
    ctx.check("kicked: told so, and out", told && !g.active());
    ctx.check("the host's list drops them", !rig.host.players[1].active && rig.remotes(ctx.sim) == 1);
}

SCENARIO(netCodeFails, "net/code-fails", "joining by code, the failures in plain words: not a code, no such game, full, a password, no backend") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 3);
    rig.host.password = "hunter2";
    rig.host.maxPlayers = 2;
    const std::string code = rig.open(3, false);
    rig.host.maxPlayers = 2;
    {
        NetSession& g = rig.guest();
        ctx.check("not a code", !g.beginJoinCode("KX7-M40", "X") && g.joinFail == NetSession::JF_NOT_A_CODE);
        ctx.check("no such game", rig.join(g, code == "ABCDEF" ? "ABCDEG" : "ABCDEF", 0) < 0 && g.joinFail == NetSession::JF_NOT_FOUND, g.error);
    }
    {
        NetSession& g = rig.guest();
        ctx.check("no password: refused, asked for one", rig.join(g, code, 0) < 0 && g.passwordIssue == NetSession::PW_NEEDED, g.error);
        g.password = "wrong";
        ctx.check("the wrong one: refused", rig.join(g, code, 0) < 0 && g.passwordIssue == NetSession::PW_WRONG, g.error);
    }
    {
        NetSession& g = rig.guest();
        g.password = "hunter2";
        ctx.check("the right one: in", rig.join(g, code, 0) > 0, g.error);
        rig.wall(1.3);  // (the backend hears the game is full)
        NetSession& g2 = rig.guest();
        g2.password = "hunter2";
        ctx.check("full: says so, with how full", rig.join(g2, code, 0) < 0 && g2.joinFail == NetSession::JF_FULL && g2.failPlayers == 2 && g2.failMax == 2, g2.error);
    }
    {
        NetSession g;
        g.rendezvous = "127.0.0.1:" + std::to_string(basePort() + 3 * 800 + 3);  // (nobody there)
        bool st = g.beginJoinCode(code, "X");
        int r = 0;
        const double t0 = net::clockNow();
        while (st && r == 0 && net::clockNow() - t0 < 10) r = g.pollJoin(), usleep(2000);
        ctx.check("no backend: can't reach the join-code server", r < 0 && g.joinFail == NetSession::JF_BACKEND, g.error);
        NetSession none;
        ctx.check("no backend set: join codes aren't set up", !none.beginJoinCode(code, "X") && none.joinFail == NetSession::JF_BACKEND, none.error);
    }
}

SCENARIO(netTunnelStream, "net/tunnel-stream", "the tunnel's reliable stream: 2 MB through 10% loss, reordering and duplicates arrives whole and in order") {
    net::ReliableStream a, b;
    std::mt19937 rng(3);
    struct Pkt {
        double at;
        bool toB;
        net::ReliableStream::Out o;
        std::vector<uint8_t> data;
    };
    std::vector<Pkt> wire;
    double now = 0;
    std::vector<uint8_t> sent(2u << 20);
    for (size_t i = 0; i < sent.size(); i++) sent[i] = (uint8_t)(rng() >> 7);
    for (size_t at = 0; at < sent.size(); at += 50000) a.write(sent.data() + at, std::min<size_t>(50000, sent.size() - at));
    std::vector<uint8_t> got;
    auto out = [&](bool toB) {
        return [&, toB](const net::ReliableStream::Out& o) {
            if (rng() % 10 == 0) return;  // lost
            Pkt p{now + 0.02 + (rng() % 30) / 1000.0, toB, o, std::vector<uint8_t>(o.p, o.p + o.n)};
            wire.push_back(p);
            if (rng() % 20 == 0) wire.push_back(p);  // and some twice
        };
    };
    for (int step = 0; step < 200000 && got.size() < sent.size() && !a.broken; step++) {
        now += 0.002;
        a.service(now, out(true));
        b.service(now, out(false));
        for (size_t i = 0; i < wire.size();) {
            if (wire[i].at > now) {
                i++;
                continue;
            }
            Pkt p = wire[i];
            wire.erase(wire.begin() + (long)i);
            net::ReliableStream& to = p.toB ? b : a;
            if (p.o.ack) to.onAck(p.o.seq, p.o.bits, now);
            else to.onData(p.o.seq, p.data.data(), p.data.size(), now);
        }
        got.insert(got.end(), b.rx.begin(), b.rx.end());
        b.rx.clear();
    }
    ctx.check("all of it, in order", got == sent && !a.broken, std::to_string(got.size()) + " of " + std::to_string(sent.size()));
    ctx.metric("time for 2 MB through 10% loss at ~35 ms", now, "s", 0, 30);
    ctx.metric("segments resent", (double)a.resent, "", 0, 2500);
    ctx.note("cwnd " + std::to_string(a.cwnd) + " srtt " + std::to_string(a.srtt) + " rto " + std::to_string(a.rto) + " sent " + std::to_string(a.segmentsSent));
}

