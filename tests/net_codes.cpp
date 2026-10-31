// Join codes end to end over loopback: the backend (rv::Server), a host that opens its game to
// friends, and guests that join with the code. They go straight through, through a simulated
// NAT (a port-restricted cone: the punch gets through), and through a symmetric NAT (it can't, so
// the relay carries everything). Also the plain-words failures, the password, a kick, the host
// leaving, closing to friends, the tunnel's stream under loss.
#include <unistd.h>

#include <cstdlib>

#include <map>
#include <mutex>
#include <random>
#include <set>

#include "../src/net.h"
#include "../src/netsock.h"
#include "../src/nettunnel.h"
#include "../src/procworld.h"
#include "../src/rendezvous.h"
#include "../src/sectionfile.h"
#include "../src/world.h"
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
    std::string open(int slot, bool nat, int map = MAP_PROVING_GROUND) {
        const int port = basePort() + slot * 800 + 2;
        if (!ctx.check("the host hosts", host.host(port, "HOSTY", map), host.error)) return "";
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

SCENARIO(netCodeSections, "net/code-sections", "the host's edited park sections go to a guest in the welcome (relayed too), and build in place of the generator's") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 4);
    SectionFile mud, rocky;
    const bool have = builtinSectionFor(SEC_MUD_PIT, mud) && builtinSectionFor(SEC_ROCKY_RIVER, rocky);
    ctx.check("two sections to send", have);
    if (!have) return;
    mud.about = "MY OWN MUD PIT";
    const std::vector<std::pair<int, std::string>> mine = {{(int)SEC_MUD_PIT, saveSectionText(mud)}, {(int)SEC_ROCKY_RIVER, saveSectionText(rocky)}};
    const std::string code = rig.open(4, true);
    rig.host.setSections(mine);
    NetSession& g = rig.guest();
    ctx.check("in with the code (relayed)", rig.join(g, code, 2, 20) > 0, g.error);
    ctx.check("the sections came whole, before the world is built", g.sections == mine,
              std::to_string(g.sections.size()) + " sections, " + std::to_string(mine[0].second.size() + mine[1].second.size()) + " bytes");
    World::setSessionSections(g.sections);
    SectionFile got;
    ctx.check("and the world builder takes them in place of the generator's", World::sessionSection(SEC_MUD_PIT, got) && got.about == "MY OWN MUD PIT" && got.nu == mud.nu);
    ctx.check("one not sent is the generator's", !World::sessionSection(SEC_DUNES, got));
    World::setSessionSections({});
}

// On the server box, against the real backend (docs/server-rendezvous-setup.md):
//   CZ_TEST_RENDEZVOUS=127.0.0.1:28015 ./build/run_tests --jobs 1 net/code-live -v
// A host opens to friends through it, one guest punches straight through, another is made to use
// the relay. Without the variable it does nothing.
SCENARIO(netCodeLive, "net/code-live", "(with CZ_TEST_RENDEZVOUS) a host, a punched guest and a relayed guest through a running join-code server") {
    const char* addr = std::getenv("CZ_TEST_RENDEZVOUS");
    if (!addr || !*addr) {
        ctx.check("skipped: set CZ_TEST_RENDEZVOUS=HOST:PORT to run it against a live backend", true);
        return;
    }
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 5);
    rig.backend.close();  // (not ours: the live one)
    rig.rvAddr = addr;
    rig.host.rendezvous = addr;
    const std::string code = rig.open(5, false);
    ctx.note("code " + rv::formatCode(code) + " from " + addr);
    NetSession& g = rig.guest();
    ctx.check("a guest joins with the code", rig.join(g, code, 0) > 0, g.error);
    ctx.check("straight through", g.myLink() == NetSession::LINK_DIRECT);
    NetSession& r = rig.guest();
    r.forceRelay = true;
    ctx.check("a guest made to use the relay joins", rig.join(r, code, 0, 20) > 0, r.error);
    ctx.check("through the relay", r.myLink() == NetSession::LINK_RELAYED);
    rig.wall(2.0);
    ctx.check("everyone sees everyone", rig.remotes(ctx.sim) == 2 && rig.remotes(rig.simOf(g)) == 2 && rig.remotes(rig.simOf(r)) == 2);
    rig.host.close();
    rig.wall(0.5);
    ctx.check("the host closes: both guests are told", !g.active() && !r.active());
}

// ---- The owner's report, step by step (docs/plans/notes-backlog.md, "Multiplayer: badly broken", 0.4.9) ----
// A session is one world. The host leaving it (quitting, the main menu, another world) ends it for everyone: each guest is told
// why and goes back to the menu (NetSession::ended), and the code goes. Hosting again is a fresh session with a fresh code, and
// a guest that joins it takes that host's world as it is now (map, procedural seed and window), nothing of an earlier session's.
namespace {
bool toldOf(const NetSession& g, const std::string& what) {
    for (const std::string& e : g.events)
        if (e.find(what) != std::string::npos) return true;
    return false;
}
// The host on the procedural world, seed `seed`, its window at (ox, oz): what the game sets every frame (main.cpp).
void procHost(NetSession& h, uint64_t seed, int64_t ox, int64_t oz) {
    h.proc.on = true, h.proc.gen = proc::genStamp(), h.proc.seed = seed, h.proc.ox = ox, h.proc.oz = oz;
}
// A guest joining by the host's address (the LAN list, a typed address) while the rig runs.
int joinByAddress(Rig& rig, NetSession& g) {
    if (!g.beginJoin("127.0.0.1:" + std::to_string(rig.host.port()), "ADDRESSY")) return -1;
    int st = 0;
    rig.ctx.runUntil(8, [&] { return st != 0; }, [&] {
        st = g.pollJoin();
        rig.frame();
    });
    if (st > 0) g.adopt(rig.simOf(g));
    return st;
}
bool heardPing(const NetSession& s) {
    for (const NetSession::GameMsg& m : s.gameIn)
        if (m.text.rfind("ping", 0) == 0) return true;
    return false;
}
}  // namespace

SCENARIO(netMpHostQuits, "net/mp-host-quits", "step 1: the host quits (or goes to the main menu): every guest, by code or by address, is told and sent back to the menu; the code goes") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 6);
    const std::string code = rig.open(6, false);
    NetSession& a = rig.guest();
    NetSession& b = rig.guest();
    NetSession& c = rig.guest();
    ctx.check("a guest joins by code", rig.join(a, code, 0) > 0, a.error);
    ctx.check("one by address", joinByAddress(rig, b) > 0, b.error);
    ctx.check("and one more by code", rig.join(c, code, 0) > 0, c.error);
    rig.wall(1.5);
    ctx.check("everyone sees everyone", rig.remotes(ctx.sim) == 3 && rig.remotes(rig.simOf(a)) == 3 && rig.remotes(rig.simOf(b)) == 3);
    // One guest leaves by itself: no "session ended" for it (its game isn't sent anywhere).
    c.close();
    rig.wall(0.5);
    ctx.check("a guest leaving by itself: nothing to send it back for", c.ended.empty() && !c.active());
    ctx.check("the others see it go", rig.remotes(ctx.sim) == 2 && rig.remotes(rig.simOf(a)) == 2);
    // The host quits (main.cpp: MAIN MENU, QUIT and STOP HOSTING all end it).
    rig.host.close("the host left the game");
    rig.wall(1.0);
    ctx.check("the guest by code is out, told the host left (the game sends it to the menu)", !a.active() && a.ended == "THE HOST LEFT THE GAME", a.ended);
    ctx.check("the guest by address too", !b.active() && b.ended == "THE HOST LEFT THE GAME", b.ended);
    ctx.check("and both were told in the events", toldOf(a, "THE HOST LEFT THE GAME") && toldOf(b, "THE HOST LEFT THE GAME"));
    ctx.check("nobody's vehicles linger on a guest", rig.remotes(rig.simOf(a)) == 0 && rig.remotes(rig.simOf(b)) == 0);
    ctx.check("the code is gone at the backend", rig.backend.codes() == 0, std::to_string(rig.backend.codes()) + " codes");
    ctx.check("the host keeps nothing of it: no code, no session", rig.host.joinCode.empty() && rig.host.codeState == NetSession::CODE_OFF && rig.host.sessionKey() == 0);
    NetSession& late = rig.guest();
    ctx.check("the old code finds no game", rig.join(late, code, 0) < 0 && late.joinFail == NetSession::JF_NOT_FOUND, late.error);
}

SCENARIO(netMpRehostProc, "net/mp-rehost-proc", "step 2: the host hosts again, on a procedural world: a fresh session and code; the guest takes its seed and window, and host and guest see each other") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 7);
    const std::string first = rig.open(7, false);
    const uint32_t firstSession = rig.host.sessionKey();
    NetSession& g = rig.guest();
    ctx.check("a guest in the first session", rig.join(g, first, 0) > 0, g.error);
    rig.host.close();
    rig.wall(0.5);
    ctx.check("the first session ends for it", !g.active() && !g.ended.empty());
    // Hosting again (the game reopened, or the menu's HOST): the procedural world, seed 4242, its window away from the start.
    const std::string second = rig.open(7, false, MAP_PROCEDURAL);
    procHost(rig.host, 4242, 2048, -1024);
    ctx.check("a new code", !second.empty() && second != first, rv::formatCode(first) + " then " + rv::formatCode(second));
    ctx.check("a new session", rig.host.sessionKey() != 0 && rig.host.sessionKey() != firstSession);
    ctx.check("the guest gets in with the new code", rig.join(g, second, 0) > 0, g.error);
    ctx.check("and is no longer 'ended'", g.ended.empty());
    ctx.check("it takes the host's procedural world: map, seed, window", g.map == MAP_PROCEDURAL && g.proc.on && g.proc.seed == 4242 && g.proc.ox == 2048 && g.proc.oz == -1024,
              "map " + std::to_string(g.map) + " seed " + std::to_string(g.proc.seed));
    rig.wall(1.5);
    ctx.check("the host sees the guest, the guest sees the host", rig.remotes(ctx.sim) == 1 && rig.remotes(rig.simOf(g)) == 1 && rig.host.players[1].active);
}

SCENARIO(netMpRehostMap, "net/mp-rehost-map", "step 3: from a procedural session to a built map: the old session ends, the new hosting hands out a new code, the guests load the built map (no procedural window, no old level)") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 8);
    const std::string procCode = rig.open(8, false, MAP_PROCEDURAL);
    procHost(rig.host, 777, 0, 0);
    rig.host.setLevel("# the procedural session's level\n");
    NetSession& a = rig.guest();
    NetSession& b = rig.guest();
    ctx.check("two guests in the procedural session", rig.join(a, procCode, 0) > 0 && rig.join(b, procCode, 0) > 0, a.error + " / " + b.error);
    rig.wall(1.0);
    ctx.check("(they have its world and level)", a.map == MAP_PROCEDURAL && a.proc.seed == 777 && !a.levelText.empty());
    // The host quits to the main menu and starts a built map: going to another world ends the session (main.cpp, loadMap).
    rig.host.close("the host went to another world");
    rig.wall(1.0);
    ctx.check("both guests are told and out", !a.active() && !b.active() && a.ended == "THE HOST WENT TO ANOTHER WORLD" && b.ended == a.ended, a.ended);
    ctx.check("the host keeps no procedural window or level for the next session", !rig.host.proc.on && rig.host.levelText.empty());
    const std::string mapCode = rig.open(8, false, MAP_PROVING_GROUND);
    ctx.check("starting multiplayer again hands out a NEW code", !mapCode.empty() && mapCode != procCode, rv::formatCode(procCode) + " then " + rv::formatCode(mapCode));
    NetSession& late = rig.guest();
    ctx.check("the old code no longer works", rig.join(late, procCode, 0) < 0 && late.joinFail == NetSession::JF_NOT_FOUND, late.error);
    ctx.check("both guests rejoin with the new one", rig.join(a, mapCode, 0) > 0 && rig.join(b, mapCode, 0) > 0, a.error + " / " + b.error);
    ctx.check("they load the built map: no procedural world kept from before", a.map == MAP_PROVING_GROUND && b.map == MAP_PROVING_GROUND && !a.proc.on && !b.proc.on);
    ctx.check("and no level from the old session", a.levelText.empty() && b.levelText.empty());
    rig.wall(1.5);
    ctx.check("everyone shows up for everyone", rig.remotes(ctx.sim) == 2 && rig.remotes(rig.simOf(a)) == 2 && rig.remotes(rig.simOf(b)) == 2);
}

SCENARIO(netMpSeparateWorlds, "net/mp-separate-worlds", "step 4: players no longer in one session share nothing: no pings or vehicles cross between the host's new world and a guest left behind") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 9);
    const std::string code = rig.open(9, false);
    NetSession& g = rig.guest();
    ctx.check("a guest in", rig.join(g, code, 0) > 0, g.error);
    rig.wall(1.0);
    g.sendGame("ping 0 12 34");
    rig.wall(1.0);
    ctx.check("(in one session, a ping gets through)", heardPing(rig.host));
    rig.host.gameIn.clear();
    // The host goes to another world and hosts it; the guest hasn't rejoined.
    rig.host.close("the host went to another world");
    rig.wall(0.5);
    const std::string code2 = rig.open(9, false, MAP_FRONTIER);
    g.sendGame("ping 0 56 78");
    rig.wall(1.0);
    ctx.check("the guest left behind can't ping the host's new world", !heardPing(rig.host) && !g.active());
    ctx.check("the host's new session has only the host", rig.host.playerCount() == 1 && !rig.host.players[1].active);  // (the game drops the old session's vehicles: main.cpp leaveSession)
    rig.host.sendGame("ping 1 1 1");
    rig.wall(0.5);
    ctx.check("nor the host the guest", !heardPing(g));
    ctx.check("the new session is on the new map, with a new code", rig.host.map == MAP_FRONTIER && !code2.empty() && code2 != code);
}

SCENARIO(netMpFollowHost, "net/mp-follow-host", "step 5: guests always follow the host's CURRENT world: after a procedural session and a built-map one, a rejoin takes the new seed, never the old one, and no guest becomes a host") {
    ctx.add(vehicleCatalog()[2], 10, -8, 0);
    Rig rig(ctx, 10);
    const std::string c1 = rig.open(10, false, MAP_PROCEDURAL);
    procHost(rig.host, 1111, 0, 0);
    NetSession& a = rig.guest();
    NetSession& b = rig.guest();
    ctx.check("two guests in procedural seed 1111", rig.join(a, c1, 0) > 0 && rig.join(b, c1, 0) > 0 && a.proc.seed == 1111);
    rig.host.close("the host went to another world");
    rig.wall(0.8);
    ctx.check("the guests are out, and neither carries on as a host of the old world", !a.active() && !b.active() && a.role == NetSession::OFF && b.role == NetSession::OFF);
    ctx.check("their old world's window went with the session", !a.proc.on && !b.proc.on && a.map == 0);
    const std::string c2 = rig.open(10, false, MAP_PROVING_GROUND);
    ctx.check("a guest on the built map", rig.join(a, c2, 0) > 0 && a.map == MAP_PROVING_GROUND && !a.proc.on, a.error);
    rig.host.close("the host went to another world");
    rig.wall(0.8);
    const std::string c3 = rig.open(10, false, MAP_PROCEDURAL);
    procHost(rig.host, 2222, 512, 512);
    ctx.check("three hostings, three codes", c1 != c2 && c2 != c3 && c1 != c3);
    ctx.check("both rejoin the new procedural world", rig.join(a, c3, 0) > 0 && rig.join(b, c3, 0) > 0, a.error + " / " + b.error);
    ctx.check("with the host's new seed and window, not the old 1111", a.proc.seed == 2222 && b.proc.seed == 2222 && a.proc.ox == 512 && b.proc.oz == 512,
              std::to_string(a.proc.seed) + " / " + std::to_string(b.proc.seed));
    rig.wall(1.5);
    ctx.check("all three together", rig.remotes(ctx.sim) == 2 && rig.remotes(rig.simOf(a)) == 2 && rig.remotes(rig.simOf(b)) == 2);
}

SCENARIO(netMpSpawnSpots, "net/mp-spawn-spots", "spawning: every player at the same spawn area, each on a clear spot of their own (the pad's places, else a ring round the spawn), never on top of another") {
    for (int pads = 1; pads >= 0; pads--) {
        const std::string tag = pads ? "on the pad" : "no pad (a ring)";
        Sim s;
        s.world = World(MAP_PROVING_GROUND);
        if (!pads) s.world.spawnPads.clear();
        s.setupEmpty();
        const World& w = s.world;
        s.addCar(vehicleCatalog()[2], V3(w.spawn.x, w.clearY(w.spawn.x, w.spawn.z) + 0.5, w.spawn.z), w.spawnYaw, V3(0.2, 0.2, 0.2));  // (the host, where it started)
        std::vector<V3> at = {s.cars[0].frame().com};
        bool allClear = true;
        for (int self = 1; self < net::kMaxPeers; self++) {
            // Each guest's own truck: built off to one side, then placed (as adopt() does on its machine, with everyone before it there).
            s.addCar(vehicleCatalog()[3], V3(300 + 20.0 * self, 5, 300), 0, V3(0.3, 0.3, 0.3));
            const int mine = (int)s.cars.size() - 1;
            V3 p;
            double yaw = 0;
            allClear &= NetSession::spawnSpot(s, self, mine, p, yaw);
            s.cars[mine].respawn(V3(p.x, w.clearY(p.x, p.z) + 0.05, p.z), yaw);
            at.push_back(V3(p.x, 0, p.z));
        }
        double nearest = 1e9, farthest = 0;
        for (size_t i = 0; i < at.size(); i++) {
            farthest = std::max(farthest, std::hypot(at[i].x - w.spawn.x, at[i].z - w.spawn.z));
            for (size_t j = i + 1; j < at.size(); j++) nearest = std::min(nearest, std::hypot(at[i].x - at[j].x, at[i].z - at[j].z));
        }
        ctx.check(tag + ": a clear spot for each of 8 players", allClear);
        ctx.metric(tag + ": closest two players", nearest, "m", 5.0, 1e9);
        ctx.metric(tag + ": farthest from the spawn", farthest, "m", 0, 40);
        if (!pads) {
            int dry = 0;
            for (size_t i = 1; i < at.size(); i++) dry += w.waterLevel(at[i].x, at[i].z) < w.height(at[i].x, at[i].z) + 0.2;
            ctx.check(tag + ": every spot on dry ground", dry == (int)at.size() - 1);
        }
    }
}
