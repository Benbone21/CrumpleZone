// Multiplayer against hostile peers: scanners, floods, forged and mangled messages. The host
// must stay up, its session intact, and nothing a stranger sends may reach or hurt the others.
// (docs/server-security.md lists what each of these guards.)
#include <unistd.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#define HAVE_RAW_SOCKETS 1  // (the hostile-peer tests below speak TCP and UDP by hand: POSIX sockets only)
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <thread>

#include "../src/jobs.h"
#include "../src/level.h"
#include "../src/mods.h"
#include "../src/net.h"
#include "../src/vehicles.h"
#include "harness.h"

// The test's way into NetSession's private parts: the message handlers, without sockets.
struct NetSessionSecTest {
    using Conn = NetSession::Conn;
    static Conn* fake(NetSession& s, int peer, bool hello) { return s.fakeConn(peer, hello); }
    static void handle(NetSession& s, Sim* sim, Conn* c, const std::vector<uint8_t>& msg) { s.handle(sim, c, msg); }
    static bool apply(NetSession& s, Sim& sim, const std::vector<uint8_t>& msg, int from) { return s.apply(sim, msg, from); }
    static void datagram(NetSession& s, Sim& sim, const std::vector<uint8_t>& d, uint32_t ip, uint16_t port) {
        s.onDatagram(sim, d.data(), d.size(), ip, port, 1.0);
    }
    static void snapshot(NetSession& s, Sim& sim, uint32_t netId, const std::vector<uint8_t>& d) { s.takeSnapshot(sim, netId, d, 1.0); }
    static void fragment(NetSession& s, Sim& sim, uint32_t netId, uint32_t seq, uint8_t f, uint8_t n, const std::vector<uint8_t>& d) {
        s.takeFragment(sim, netId, seq, f, n, d.data(), d.size(), 1.0);
    }
    static void play(NetSession& s, Sim& sim, double now) { s.applyRemotes(sim, now); }
    static size_t conns(const NetSession& s) { return s.conns.size(); }
    static std::vector<uint8_t> strip(const std::vector<uint8_t>& framed) { return {framed.begin() + 2, framed.end()}; }
    static std::vector<uint8_t> spawn(NetSession& s, const Car& c) { return strip(s.spawnMsg(c)); }
    static std::vector<uint8_t> lamps(NetSession& s, const Car& c) { return strip(s.lampMsg(c)); }
    static std::vector<uint8_t> rope(NetSession& s, const Sim& sim, const Rope& r) { return strip(s.ropeMsg(sim, r)); }
    static std::vector<uint8_t> state(NetSession& s, const Sim& sim, const Car& c, double t) {
        s.sessionStart = 0;
        return s.snapshotMsg(sim, c, t);
    }
    static uint64_t token(Conn* c) { return c->token; }
    static void timeouts(NetSession& s, double handshake, double idle) { s.handshakeSecs = handshake, s.idleSecs = idle; }
    static size_t pending(const NetSession& s) { return s.pending.size(); }
    static size_t known(const NetSession& s) { return s.knownRopes.size(); }
    // (Every guest without a socket, and the players they made: a clean slate.)
    static void dropFakes(NetSession& s) {
        for (Conn* c : s.conns) delete c;
        s.conns.clear();
        for (int k = 1; k < net::kMaxPeers; k++) s.players[k] = NetPlayer();
    }
};

namespace {

using T = NetSessionSecTest;

enum : uint8_t { HELLO = 1, WELCOME, REJECT, PLAYER, LEFT, SPAWN, DESPAWN, ROPE, UNROPE, PING, PONG, RTT, CLOCK, LEVEL, GAME, LAMPS, CHALLENGE, AUTH };

int testPort() { return 27900 + (int)(getpid() % 300); }

struct Rig {
    Rig(Sim& sim, NetSession& h) : hsim(sim), host(h) {}
    Sim& hsim;
    NetSession& host;
    double t = 0;
    std::vector<std::pair<NetSession*, Sim*>> clients;  // real clients that are kept going too
    void pump(int ms) {
        for (int k = 0; k < ms / 2; k++) {
            host.receive(hsim, t);
            host.send(hsim, t);
            for (auto& c : clients) c.first->receive(*c.second, t), c.first->send(*c.second, t);
            usleep(2000);
            t += 0.002;
        }
    }
};

void otherMachine(Sim& sim2, int map) {
    sim2.world = World(map);
    sim2.setupEmpty();
    sim2.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
    sim2.player = 0;
}

// A real client, joined to `host` (which must be pumped while it joins).
bool joinClient(Rig& rig, NetSession& c, Sim& csim, int port, const std::string& name, std::string* why = nullptr) {
    if (!c.beginJoin("127.0.0.1:" + std::to_string(port), name)) return false;
    int st = 0;
    for (int k = 0; k < 600 && st == 0; k++) {
        st = c.pollJoin();
        rig.pump(4);
    }
    if (why) *why = c.error;
    if (st != 1) return false;
    otherMachine(csim, MAP_PROVING_GROUND);
    c.adopt(csim);
    rig.clients.push_back({&c, &csim});
    return true;
}

#ifdef HAVE_RAW_SOCKETS
std::vector<uint8_t> frameOf(uint8_t type, const std::vector<uint8_t>& body = {}) {
    std::vector<uint8_t> m;
    const uint16_t len = (uint16_t)(body.size() + 1);
    m.push_back(len & 255), m.push_back(len >> 8), m.push_back(type);
    m.insert(m.end(), body.begin(), body.end());
    return m;
}
std::vector<uint8_t> helloOf(uint16_t version, const std::string& name) {
    std::vector<uint8_t> b = {(uint8_t)(version & 255), (uint8_t)(version >> 8), (uint8_t)name.size()};
    b.insert(b.end(), name.begin(), name.end());
    return frameOf(HELLO, b);
}
std::vector<uint8_t> gameOf(const std::string& text, double said = 0) {
    std::vector<uint8_t> b = {0, (uint8_t)text.size()};
    b.insert(b.end(), text.begin(), text.end());
    const uint8_t* p = (const uint8_t*)&said;  // (when it was said, by the shared clock)
    b.insert(b.end(), p, p + 8);
    return frameOf(GAME, b);
}

// ---- A raw TCP peer: speaks (or garbles) the protocol by hand ----
struct Raw {
    int fd = -1;
    std::vector<uint8_t> buf;
    int id = -1;
    uint64_t token = 0;
    ~Raw() {
        if (fd >= 0) ::close(fd);
    }
    bool open(int port, const char* from = nullptr) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (from) {
            sockaddr_in s{};
            s.sin_family = AF_INET;
            inet_pton(AF_INET, from, &s.sin_addr);
            if (bind(fd, (sockaddr*)&s, sizeof s) < 0) return false;
        }
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        connect(fd, (sockaddr*)&a, sizeof a);  // (EINPROGRESS: the kernel finishes it)
        return true;
    }
    void send(const std::vector<uint8_t>& b) {
        pollfd p{fd, POLLOUT, 0};
        poll(&p, 1, 100);
        ::send(fd, b.data(), b.size(), MSG_NOSIGNAL);
    }
    // Reads what's there; true once the other end has closed.
    bool read() {
        for (;;) {
            uint8_t tmp[4096];
            const ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n > 0) {
                buf.insert(buf.end(), tmp, tmp + n);
                continue;
            }
            return n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK);
        }
    }
    // The next whole message (type, body), if one has come.
    bool next(uint8_t& type, std::vector<uint8_t>& body) {
        if (buf.size() < 3) return false;
        const size_t len = buf[0] | (buf[1] << 8);
        if (buf.size() < 2 + len || len == 0) return false;
        type = buf[2];
        body.assign(buf.begin() + 3, buf.begin() + 2 + len);
        buf.erase(buf.begin(), buf.begin() + 2 + len);
        return true;
    }
};

// A raw guest, let in (with `password`, if the host asked).
bool rawJoin(Rig& rig, Raw& g, int port, const std::string& name, const std::string& password = "") {
    if (!g.open(port)) return false;
    g.send(helloOf(net::kVersion, name));
    for (int k = 0; k < 100; k++) {
        rig.pump(10);
        g.read();
        uint8_t type;
        std::vector<uint8_t> body;
        while (g.next(type, body)) {
            if (type == CHALLENGE && body.size() == 16 && !password.empty())
                g.send(frameOf(AUTH, net::hmacSha256(password, body)));
            if (type == WELCOME && body.size() == 10) {
                g.id = body[0];
                std::memcpy(&g.token, body.data() + 2, 8);
                return true;
            }
            if (type == REJECT) return false;
        }
    }
    return false;
}
// Pumps, then true if the guest was cut off.
bool rawDropped(Rig& rig, Raw& g) {
    for (int k = 0; k < 40; k++) {
        rig.pump(20);
        if (g.read()) return true;
    }
    return false;
}

#endif

int remoteCount(const Sim& s) {
    int n = 0;
    for (const Car& c : s.cars) n += c.remote;
    return n;
}

}  // namespace

SCENARIO(netSecHmac, "net/sec-hmac", "The password proof is real HMAC-SHA-256 (RFC 4231 vectors), and text from the network is cut down to what the HUD font can draw") {
    auto hex = [](const std::vector<uint8_t>& v) {
        std::string s;
        char b[4];
        for (uint8_t x : v) std::snprintf(b, sizeof b, "%02x", x), s += b;
        return s;
    };
    auto bytes = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    ctx.check("RFC 4231 case 1", hex(net::hmacSha256(std::string(20, '\x0b'), bytes("Hi There"))) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    ctx.check("RFC 4231 case 2", hex(net::hmacSha256("Jefe", bytes("what do ya want for nothing?"))) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    ctx.check("RFC 4231 case 6 (a key longer than a block)",
              hex(net::hmacSha256(std::string(131, '\xaa'), bytes("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
                  "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    ctx.check("sanitize drops controls and high bytes", net::sanitize(std::string("A\x01\x1b[31mB\x7f\xc3\xa9 C\n"), 20) == "A[31mB C");
    ctx.check("sanitize cuts to the length and trims", net::sanitize("   ABCDEFGHIJ   ", 5) == "ABCDE" && net::sanitize("   ", 5).empty());
}

SCENARIO(netSecPassword, "net/sec-password", "A password-protected host: the wrong password is refused, the right one let in, none asked for refused, and five wrong ones lock an address out") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.password = "correct horse battery";
    host.discovery = false;
    const int port = testPort();
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Rig rig(ctx.sim, host);

    NetSession wrong, none, right;
    Sim sw, sn, sr;
    wrong.password = "correct horse batterY";
    std::string why;
    ctx.check("the wrong password is refused", !joinClient(rig, wrong, sw, port, "WRONG", &why) && why.find("wrong password") != std::string::npos, why);
    ctx.check("(and the host has no such player)", host.playerCount() == 1);
    ctx.check("no password at all: refused, and told why", !joinClient(rig, none, sn, port, "NONE", &why) && why.find("password") != std::string::npos, why);
    right.password = "correct horse battery";
    ctx.check("the right password is let in", joinClient(rig, right, sr, port, "RIGHT", &why), why);
    ctx.check("(and is a player)", host.playerCount() == 2 && right.self == 1);
    // A client that offers a password to a host that wants none is fine.
    NetSession open;
    open.password = "whatever";
    NetSession hostOpen;
    hostOpen.discovery = false;
    Sim so, ho;
    otherMachine(ho, MAP_PROVING_GROUND);
    ctx.check("an open host", hostOpen.host(port + 1, "OPEN", MAP_PROVING_GROUND), hostOpen.error);
    hostOpen.adopt(ho);
    Rig rig2(ho, hostOpen);
    ctx.check("anyone may join a host with no password", joinClient(rig2, open, so, port + 1, "GUEST", &why), why);
    open.close();
    hostOpen.close();
    right.close();
    // Five wrong in a row from one address: a minute away, even for the right password.
    for (int k = 0; k < 4; k++) {
        NetSession again;
        again.password = "nope" + std::to_string(k);
        Sim s;
        joinClient(rig, again, s, port, "TRY", &why);
    }
    NetSession late;
    late.password = "correct horse battery";
    Sim sl;
    ctx.check("locked out after repeated wrong passwords", !joinClient(rig, late, sl, port, "LATE", &why), why);
    ctx.check("(the log says so)", !host.log.empty());
    host.close();
}

SCENARIO(netSecMenuPassword, "net/sec-menu-password", "Joining the way the menu does (the typed password through net::joinPassword): the right one gets in, none or a wrong one says which, so the card can ask again, and the command line's wins") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.password = "Tr1cky/Pass word!";
    host.discovery = false;
    const int port = testPort() + 8;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Rig rig(ctx.sim, host);

    ctx.check("the command line's password wins over the typed one", net::joinPassword("CLI", "typed") == "CLI" && net::joinPassword("", "typed") == "typed" && net::joinPassword("", "").empty());
    NetSession none, wrong, right;
    Sim sn, sw, sr;
    std::string why;
    none.password = net::joinPassword("", "");
    ctx.check("no password: refused as NEEDED", !joinClient(rig, none, sn, port, "NONE", &why) && none.passwordIssue == NetSession::PW_NEEDED, why);
    wrong.password = net::joinPassword("", "tr1cky/Pass word!");  // (the case matters)
    ctx.check("a wrong password: refused as WRONG", !joinClient(rig, wrong, sw, port, "WRONG", &why) && wrong.passwordIssue == NetSession::PW_WRONG, why);
    // Retry on the same session object, as the card does: a fresh password, same address.
    wrong.password = net::joinPassword("", "Tr1cky/Pass word!");
    ctx.check("RETRY with the right one gets in", joinClient(rig, wrong, sw, port, "WRONG", &why) && wrong.passwordIssue == NetSession::PW_OK, why);
    ctx.check("(and is a player)", host.playerCount() == 2);
    host.close();
}

#ifdef HAVE_RAW_SOCKETS
SCENARIO(netSecFlood, "net/sec-flood", "A flood of connections is capped per address and overall, a real player still gets in, and silent connections are dropped") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.discovery = false;
    const int port = testPort() + 2;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Rig rig(ctx.sim, host);
    // One address opens 100 connections.
    std::vector<Raw> flood(100);
    for (Raw& f : flood) f.open(port, "127.0.0.2");
    rig.pump(200);
    ctx.check("one address gets " + std::to_string(net::kMaxConnsPerIp) + " connections, no more", T::conns(host) <= (size_t)net::kMaxConnsPerIp, std::to_string(T::conns(host)));
    int closed = 0;
    for (Raw& f : flood) closed += f.read();
    ctx.check("the ones the host got to were closed on them", closed >= 20, std::to_string(closed));  // (the rest wait in the kernel's queue)
    // Many addresses, four each.
    std::vector<Raw> many(4 * 40);
    for (size_t k = 0; k < many.size(); k++) many[k].open(port, ("127.0.1." + std::to_string(1 + k / 4)).c_str());
    rig.pump(300);
    ctx.check("the total is capped", T::conns(host) <= (size_t)net::kMaxConns, std::to_string(T::conns(host)) + " of " + std::to_string(net::kMaxConns));
    ctx.check("the host counts what it turned away", host.refused >= 20, std::to_string(host.refused));
    flood.clear();
    many.clear();
    rig.pump(300);
    // Connections that never say a word (slowloris) are dropped after the handshake time.
    // (The host counts wall-clock seconds, pump() sleeps: where usleep(2000) overshoots, as on a loaded macOS runner, scale the times by how slow it runs.)
    const auto w0 = std::chrono::steady_clock::now();
    rig.pump(100);
    const double slow = std::max(1.0, std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count() / 0.1);
    T::timeouts(host, 0.4 * slow, 30);
    std::vector<Raw> silent(3);
    for (Raw& f : silent) f.open(port, "127.0.0.3");
    rig.pump(200);
    ctx.check("(they're held for now)", T::conns(host) == 3, std::to_string(T::conns(host)));
    rig.pump(1200);
    ctx.check("silent connections are dropped", T::conns(host) == 0, std::to_string(T::conns(host)));
    int cut = 0;
    for (Raw& f : silent) cut += f.read();
    ctx.check("and closed on the other end", cut == 3, std::to_string(cut));
    NetSession c;
    Sim csim;
    std::string why;
    ctx.check("and a real player still gets in", joinClient(rig, c, csim, port, "REAL", &why), why);
    // A player who answers pings stays; one who goes silent for the idle time is dropped.
    T::timeouts(host, 8, 2.5 * slow);  // (the host pings once a second)
    rig.pump(2000);
    ctx.check("a player is fine while it answers pings", host.playerCount() == 2);
    rig.clients.clear();  // (c goes silent)
    rig.pump(4000);
    ctx.check("a joined player who goes silent is dropped", host.playerCount() == 1, std::to_string(host.playerCount()));
    host.close();
}

SCENARIO(netSecHostOnly, "net/sec-host-only", "A guest can't send the host's messages (welcome, reject, level, clock, who left), forge another's vehicle, or pass on host-only chatter; its name and chat are cleaned") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.discovery = false;
    const int port = testPort() + 3;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_FRONTIER), host.error);
    host.adopt(ctx.sim);
    Rig rig(ctx.sim, host);
    NetSession buddy;
    Sim bsim;
    std::string why;
    ctx.check("a buddy joins", joinClient(rig, buddy, bsim, port, "BUDDY", &why), why);
    const int hostMap = host.map;
    auto stillFine = [&] { return host.role == NetSession::HOST && host.self == 0 && host.map == hostMap && host.active() && host.players[buddy.self].active; };

    // Each of the host's own messages, said by a guest: it's cut off, nothing changes.
    struct Case {
        const char* what;
        uint8_t type;
        std::vector<uint8_t> body;
    };
    const std::vector<Case> cases = {
        {"WELCOME (re-numbering the host)", WELCOME, {5, 0, 1, 2, 3, 4, 5, 6, 7, 8}},
        {"REJECT (shutting the session down)", REJECT, {4, 'N', 'O', 'P', 'E'}},
        {"PLAYER (a made-up player)", PLAYER, {3, 3, 'X', 'Y', 'Z'}},
        {"LEFT (kicking the buddy)", LEFT, {1}},
        {"RTT", RTT, {1, 0, 0, 0xc0, 0x7f}},
        {"CLOCK", CLOCK, {0, 0, 0xc0, 0x7f, 0, 0, 0x80, 0x3f}},
        {"LEVEL", LEVEL, {1, 0, 0, 0, 0, 0, 1, 0, 'x'}},
        {"CHALLENGE", CHALLENGE, std::vector<uint8_t>(16, 7)},
        {"AUTH out of turn", AUTH, std::vector<uint8_t>(32, 7)},
        {"an unknown type", 200, {1, 2, 3}},
        {"a second HELLO", HELLO, {5, 0, 1, 'X'}},
    };
    for (const Case& cs : cases) {
        Raw g;
        if (!rawJoin(rig, g, port, "EVIL")) {
            ctx.check(std::string("a raw guest joins for ") + cs.what, false);
            continue;
        }
        g.send(frameOf(cs.type, cs.body));
        ctx.check(std::string("guest sending ") + cs.what + " is dropped", rawDropped(rig, g));
        rig.pump(30);
        ctx.check(std::string("(session intact after ") + cs.what + ")", stillFine() && buddy.role == NetSession::CLIENT && buddy.players[0].active && !buddy.levelFresh && !buddy.dayFresh);
    }
    rig.pump(200);
    ctx.check("the buddy never lost anyone or got a made-up player", buddy.playerCount() == 2, std::to_string(buddy.playerCount()));

    // Before the hello, only a hello: anything else, or something huge, ends it.
    {
        Raw g;
        g.open(port);
        g.send(frameOf(SPAWN, std::vector<uint8_t>(40, 1)));
        ctx.check("a vehicle before any hello: dropped", rawDropped(rig, g));
        Raw h;
        h.open(port);
        h.send({0xff, 0xff, PING});
        ctx.check("a 65535-byte message before hello: dropped", rawDropped(rig, h));
        Raw i;
        i.open(port);
        i.send(helloOf(net::kVersion + 1, "OLD"));
        i.read();
        rig.pump(100);
        i.read();
        uint8_t type = 0;
        std::vector<uint8_t> body;
        const bool told = i.next(type, body) && type == REJECT;
        const std::string why(body.begin() + std::min<size_t>(1, body.size()), body.end());
        ctx.check("another version is told so, then dropped", told && rawDropped(rig, i));
        ctx.check("in plain words: what to update to", why.find("version mismatch") != std::string::npos && why.find("older") != std::string::npos, why);
        Raw old;
        old.open(port);
        old.send(helloOf(net::kVersion - 1, "OLD"));
        rig.pump(100);
        old.read();
        const bool oldTold = old.next(type, body) && type == REJECT;
        const std::string oldWhy(body.begin() + std::min<size_t>(1, body.size()), body.end());
        ctx.check("an older game is told to update, and to what", oldTold && oldWhy == std::string("version mismatch: update to ") + net::kRelease + " or newer", oldWhy);
        Raw j;
        j.open(port);
        std::vector<uint8_t> noise(3000);
        std::mt19937 rng(7);
        for (uint8_t& b : noise) b = (uint8_t)rng();
        j.send(noise);
        ctx.check("random bytes: dropped", rawDropped(rig, j));
        ctx.check("(the session still stands)", stillFine());
    }

    // A guest's name and chat are cleaned; host-only chatter isn't passed on; chat is.
    {
        Raw g;
        std::string dirty = std::string("dr\x1b[2Jiv\x07") + "er name that is far too long";
        ctx.check("a guest with a dirty name joins", rawJoin(rig, g, port, dirty));
        ctx.check("its name is plain, short text", host.players[g.id].name.rfind("dr[2Jiver", 0) == 0 && host.players[g.id].name.size() <= net::kMaxNameLen, host.players[g.id].name);
        for (char ch : host.players[g.id].name) ctx.check("(printable)", ch >= 32 && ch < 127);
        Raw twin;
        ctx.check("a second guest with the same name joins", rawJoin(rig, twin, port, "BUDDY"));
        ctx.check("and gets a different one", host.players[twin.id].name != "BUDDY" && host.players[twin.id].name.find("BUDDY") == 0, host.players[twin.id].name);
        buddy.gameIn.clear();
        for (const char* t : {"restart", "jobdone", "jobstop", "job|0|WIN|X|1|2|3|Y|2|0|0", "chat hello\x01\x02 there"}) g.send(gameOf(t));
        rig.pump(200);
        for (int k = 0; k < 20; k++) buddy.receive(bsim, rig.t), usleep(2000), rig.pump(4);
        int hostOnly = 0, chat = 0;
        std::string chatText;
        for (const auto& m : buddy.gameIn) {
            hostOnly += m.text == "restart" || m.text == "jobdone" || m.text == "jobstop" || m.text.rfind("job|", 0) == 0;
            if (m.text.rfind("chat", 0) == 0) chat++, chatText = m.text;
        }
        ctx.check("host-only chatter from a guest reaches nobody", hostOnly == 0, std::to_string(hostOnly));
        ctx.check("ordinary chat does, cleaned, and as that guest", chat == 1 && chatText == "chat hello there", chatText);
        bool hostSaw = false;
        for (const auto& m : host.gameIn) hostSaw |= m.text == "restart" || m.text == "jobdone";
        ctx.check("(nor does the host act on it)", !hostSaw);
    }
    // Forging: a guest's vehicle with another player's id, or the host's.
    {
        Raw g;
        ctx.check("a guest joins", rawJoin(rig, g, port, "FORGER"));
        Sim origin;
        origin.setupEmpty();
        origin.addCar(vehicleCatalog()[2], V3(0, 0.5, 0), 0, V3(0.2, 0.2, 0.2));
        NetSession enc;
        const int before = (int)ctx.sim.cars.size();
        for (uint32_t forged : {0x00000001u, (uint32_t)(1u << 16) | 5u, (uint32_t)(g.id + 1) << 16 | 1u, 0u, 0xffff0001u}) {
            origin.cars[0].netId = forged;
            enc.self = (int)(forged >> 16);
            std::vector<uint8_t> body = T::spawn(enc, origin.cars[0]);
            body.erase(body.begin());  // (the type byte: frameOf adds it)
            g.send(frameOf(SPAWN, body));
        }
        rig.pump(200);
        ctx.check("a vehicle claiming another's id (or the host's) is not added", (int)ctx.sim.cars.size() == before, std::to_string(ctx.sim.cars.size()) + " vs " + std::to_string(before));
        ctx.check("(nor shown to the buddy)", remoteCount(bsim) == 1);  // (just the host's)
    }
    ctx.check("the host's session is intact at the end", stillFine());
    buddy.close();
    host.close();
}

SCENARIO(netSecUdp, "net/sec-udp", "Datagrams must carry the receiver's secret: a scan, a forged state or a forged address for a guest is dropped, unanswered") {
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.discovery = false;
    const int port = testPort() + 4;
    ctx.check("host opens a session", host.host(port, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    Rig rig(ctx.sim, host);
    NetSession a;
    Sim asim;
    std::string why;
    ctx.check("a guest joins", joinClient(rig, a, asim, port, "A", &why), why);
    rig.pump(100);
    // Something that scans: the right magic, any peer, no secret.
    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(udp, F_SETFL, fcntl(udp, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    auto header = [&](uint8_t type, uint8_t peer, uint32_t netId, uint64_t token, size_t payload) {
        std::vector<uint8_t> d(22 + payload, 0);
        d[0] = 0x7a, d[1] = 0xc2, d[2] = type, d[3] = peer;
        std::memcpy(&d[4], &netId, 4);
        d[12] = 0, d[13] = 1;  // fragment 0 of 1
        std::memcpy(&d[14], &token, 8);
        return d;
    };
    int answers = 0;
    auto probe = [&](const std::vector<uint8_t>& d) {
        sendto(udp, d.data(), d.size(), 0, (sockaddr*)&to, sizeof to);
        rig.pump(30);
        uint8_t buf[2048];
        while (recv(udp, buf, sizeof buf, 0) > 0) answers++;
    };
    probe(header(2, 1, 0, 0, 0));                       // HELLO, no secret
    probe(header(2, 1, 0, 0x1122334455667788ull, 0));   // HELLO, a guess
    probe(header(1, 1, (1u << 16) | 1, 0, 100));        // a state
    probe(std::vector<uint8_t>(5, 0));                  // junk
    probe(std::vector<uint8_t>(3000, 0x7a));            // big junk
    ctx.check("none of that is answered", answers == 0, std::to_string(answers));
    // The real thing, through the real client: the guest's address is learned, the car is seen.
    rig.pump(300);
    for (int k = 0; k < 30; k++) a.receive(asim, rig.t), a.send(asim, rig.t), rig.pump(10);
    ctx.check("the real guest's states arrive (the host sees its truck)", remoteCount(ctx.sim) == 1, std::to_string(remoteCount(ctx.sim)));
    // A forged state in the guest's name, from the scanner, with a guessed secret: nothing moves.
    const V3 where = [&] {
        for (const Car& c : ctx.sim.cars)
            if (c.remote) return c.frame().com;
        return V3();
    }();
    for (int k = 0; k < 50; k++) {
        auto d = header(1, 1, (1u << 16) | 1, 0x0123456789abcdefull + k, 200);
        sendto(udp, d.data(), d.size(), 0, (sockaddr*)&to, sizeof to);
    }
    rig.pump(100);
    V3 now;
    for (const Car& c : ctx.sim.cars)
        if (c.remote) now = c.frame().com;
    ctx.check("forged states move nothing", len(now - where) < 5, std::to_string(len(now - where)));
    ::close(udp);
    a.close();
    host.close();
}

#endif

SCENARIO(netSecLevel, "net/sec-level", "An oversized level is refused (by the host sending it and by the client taking it); a level in parts arrives only whole and in order") {
    NetSession host;
    host.discovery = false;
    ctx.check("host opens a session", host.host(testPort() + 5, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    host.setLevel(std::string(net::kMaxLevelBytes + 1, 'x'));
    ctx.check("the host won't send a level over the limit", host.levelText.empty() && host.levelSerial == 0 && !host.events.empty());
    host.close();

    NetSession c;
    c.role = NetSession::CLIENT;
    auto* conn = T::fake(c, 0, true);
    auto part = [&](uint32_t serial, uint16_t k, uint16_t parts, size_t bytes) {
        std::vector<uint8_t> b(8 + bytes, 'x');
        std::memcpy(&b[0], &serial, 4), std::memcpy(&b[4], &k, 2), std::memcpy(&b[6], &parts, 2);
        b.insert(b.begin(), LEVEL);
        return b;
    };
    const uint16_t parts = 150;  // 150 x 60000 = 9 MB
    for (uint16_t k = 0; k < parts; k++) T::handle(c, nullptr, conn, part(1, k, parts, 60000));
    ctx.check("a client refuses a level over the limit", !c.levelFresh && c.levelText.empty());
    for (uint16_t k = 0; k < 100; k++) T::handle(c, nullptr, conn, part(2, k, 100, 60000));
    ctx.check("one under the limit is taken, whole", c.levelFresh && c.levelText.size() == 100 * 60000, std::to_string(c.levelText.size()));
    c.levelFresh = false;
    c.levelText.clear();
    T::handle(c, nullptr, conn, part(3, 0, 3, 100));
    T::handle(c, nullptr, conn, part(3, 2, 3, 100));  // a part missing
    ctx.check("a level with a part missing is not taken", !c.levelFresh);
    T::handle(c, nullptr, conn, part(4, 0, 0, 100));
    T::handle(c, nullptr, conn, part(4, 5, 3, 100));
    T::handle(c, nullptr, conn, part(4, 0, 60000, 100));
    ctx.check("nonsense counts are ignored", !c.levelFresh);
}

SCENARIO(netSecServerKeeps, "net/sec-late-join", "A host with no truck of its own (the dedicated server) keeps what its guests spawn and tells it to whoever joins later") {
    Sim hsim;
    hsim.world = World(MAP_PROVING_GROUND);
    hsim.setupEmpty();
    NetSession host;
    host.discovery = false;
    const int port = testPort() + 6;
    ctx.check("host opens a session", host.host(port, "SERVER", MAP_PROVING_GROUND), host.error);
    host.adopt(hsim);
    Rig rig(hsim, host);
    NetSession a, b;
    Sim asim, bsim;
    std::string why;
    ctx.check("A joins", joinClient(rig, a, asim, port, "A", &why), why);
    for (int k = 0; k < 40; k++) a.receive(asim, rig.t), a.send(asim, rig.t), rig.pump(10);
    ctx.check("the server holds A's truck", remoteCount(hsim) == 1, std::to_string(remoteCount(hsim)));
    ctx.check("B joins later", joinClient(rig, b, bsim, port, "B", &why), why);
    for (int k = 0; k < 40; k++) a.receive(asim, rig.t), a.send(asim, rig.t), b.receive(bsim, rig.t), b.send(bsim, rig.t), rig.pump(10);
    ctx.check("B sees A's truck", remoteCount(bsim) == 1, std::to_string(remoteCount(bsim)));
    ctx.check("A sees B's", remoteCount(asim) == 1, std::to_string(remoteCount(asim)) + " host:" + std::to_string(remoteCount(hsim)) + " players A:" + std::to_string(a.playerCount()) + " host log " + (host.log.empty() ? "" : host.log.back()));
    a.close();
    b.close();
    host.close();
}

// ---- The fuzzer ------------------------------------------------------------------------

namespace {

struct Fuzz {
    std::mt19937 rng;
    explicit Fuzz(unsigned seed) : rng(seed) {}
    size_t below(size_t n) { return n ? rng() % n : 0; }
    std::vector<uint8_t> mutate(std::vector<uint8_t> m) {
        static const uint32_t specials[] = {0x7fc00000, 0x7f800000, 0xff800000, 0x7f7fffff, 0xff7fffff, 0x00000001, 0x80000000, 0x4e800000, 0x5f000000,
                                            0xffffffff, 0x00000000, 0x3f800000, 0xbf800000, 0x7fffffff, 0x0000ffff, 0x00010000};
        const int ops = 1 + (int)below(4);
        for (int o = 0; o < ops; o++) {
            switch (below(8)) {
                case 0:  // flip some bytes
                    for (size_t k = 0, n = 1 + below(6); k < n && !m.empty(); k++) m[below(m.size())] ^= (uint8_t)(1u << below(8));
                    break;
                case 1:  // truncate
                    m.resize(below(m.size() + 1));
                    break;
                case 2:  // append garbage
                    for (size_t k = 0, n = below(200); k < n; k++) m.push_back((uint8_t)rng());
                    break;
                case 3:  // a special 32-bit value anywhere
                    if (m.size() >= 4) {
                        const uint32_t v = specials[below(sizeof specials / sizeof *specials)];
                        std::memcpy(&m[below(m.size() - 3)], &v, 4);
                    }
                    break;
                case 4:  // a byte set to an extreme
                    if (!m.empty()) {
                        static const uint8_t extremes[] = {0, 1, 0x7f, 0x80, 0xff};
                        m[below(m.size())] = extremes[below(5)];
                    }
                    break;
                case 5:  // another type
                    if (!m.empty()) m[0] = (uint8_t)(below(4) ? 1 + below(18) : rng());
                    break;
                case 6:  // a window of random bytes
                    if (!m.empty()) {
                        const size_t a = below(m.size());
                        for (size_t k = a; k < m.size() && k < a + 8; k++) m[k] = (uint8_t)rng();
                    }
                    break;
                default:  // repeat a chunk
                    if (m.size() > 4) {
                        const size_t a = below(m.size() - 2), n = 1 + below(std::min<size_t>(40, m.size() - a));
                        std::vector<uint8_t> chunk(m.begin() + a, m.begin() + a + n);
                        m.insert(m.begin() + a, chunk.begin(), chunk.end());
                    }
                    break;
            }
        }
        return m;
    }
};

bool finiteCars(const Sim& s) {
    for (const Car& c : s.cars)
        for (const Node& n : c.nodes)
            if (!std::isfinite(n.p.x + n.p.y + n.p.z + n.v.x + n.v.y + n.v.z)) return false;
    return true;
}

}  // namespace

SCENARIO(netSecFuzz, "net/sec-fuzz", "Thousands of random, truncated, oversized, NaN and bad-index messages and datagrams at a host and a client: nothing crashes, no state is poisoned, nothing grows without bound") {
    // The world a host and a guest would share.
    Car& hostCar = ctx.add(vehicleCatalog()[2], 10, -8, 0);
    ctx.focus(hostCar);
    NetSession host;
    host.discovery = false;
    ctx.check("host opens a session", host.host(testPort() + 7, "HOSTY", MAP_PROVING_GROUND), host.error);
    host.adopt(ctx.sim);
    host.send(ctx.sim, 0);

    // The guest's side, for building real messages to mangle (peer 1 and peer 2).
    Sim origin;
    origin.world = World(MAP_PROVING_GROUND);
    origin.setupEmpty();
    for (int k = 0; k < 3; k++) origin.addCar(vehicleCatalog()[1 + k], V3(-40 + 12 * k, 0.5, -40), 0, V3(0.3, 0.4, 0.5));
    std::vector<NetSession> enc(3);
    for (int k = 0; k < 3; k++) enc[k].self = 1;
    std::vector<std::vector<uint8_t>> seeds;
    for (int k = 0; k < 3; k++) {
        origin.cars[k].netId = (1u << 16) | (uint32_t)(k + 1);
        seeds.push_back(T::spawn(enc[k], origin.cars[k]));
        seeds.push_back(T::lamps(enc[k], origin.cars[k]));
        Rope r;
        r.netId = (1u << 16) | (uint32_t)(k + 1);
        r.kind = k == 1 ? ROPE_STRAP : ROPE_WINCH;
        r.a.kind = ANCHOR_CAR, r.a.car = k, r.a.hook = 0;
        if (k == 0) r.b.kind = ANCHOR_TREE, r.b.p = V3(-60, 0, -40);
        else r.b.kind = ANCHOR_CAR, r.b.car = (k + 1) % 3, r.b.hook = 1;
        r.length = 8, r.maxLen = 30, r.k = 2.5e5, r.c = 3000, r.breakN = 9e4, r.stallN = 4.4e4;
        origin.ropes.push_back(r);
        seeds.push_back(T::rope(enc[k], origin, r));
    }
    seeds.push_back({DESPAWN, 1, 0, 1, 0});
    seeds.push_back({UNROPE, 1, 0, 1, 0});
    seeds.push_back({GAME, 0, 5, 'c', 'h', 'a', 't', ' ', 0, 0, 0, 0, 0, 0, 0xf0, 0x3f});  // (said at 1.0 on the shared clock)
    seeds.push_back({PING, 0, 0, 0, 0, 0, 0, 0, 0});
    seeds.push_back({PONG, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf0, 0x3f});
    seeds.push_back({HELLO, 5, 0, 3, 'A', 'B', 'C'});
    seeds.push_back({AUTH, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32});
    seeds.push_back({WELCOME, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8});
    seeds.push_back({PLAYER, 2, 3, 'B', 'O', 'B'});
    seeds.push_back({LEFT, 2});
    seeds.push_back({RTT, 2, 0, 0, 0x80, 0x3f});
    seeds.push_back({CLOCK, 0, 0, 0, 0, 0, 0, 0xf0, 0x3f, 0, 0, 0xc0, 0x41, 0, 0, 0x80, 0x3f});  // (at 1.0: 24 h, 1 h/s)
    seeds.push_back({LEVEL, 1, 0, 0, 0, 0, 0, 1, 0, 'a', 'b'});
    seeds.push_back({REJECT, 2, 'N', 'O'});
    seeds.push_back({CHALLENGE, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});

    // 1. The host hearing a guest (and a stranger).
    Fuzz fz(12345);
    auto* guest = T::fake(host, 1, true);
    auto* stranger = T::fake(host, -1, false);
    int iters = 0;
    for (int k = 0; k < 6000; k++, iters++) {
        std::vector<uint8_t> m = fz.below(10) == 0 ? std::vector<uint8_t>(fz.below(300)) : fz.mutate(seeds[fz.below(seeds.size())]);
        if (m.empty() || fz.below(10) == 0) for (uint8_t& b : m) b = (uint8_t)fz.rng();
        T::handle(host, &ctx.sim, fz.below(5) ? guest : stranger, m);
        T::apply(host, ctx.sim, m, 1 + (int)fz.below(3));
        if (k % 16 == 15) {  // (a fresh pair: a dropped guest is a dead end)
            T::dropFakes(host);
            guest = T::fake(host, 1, true);
            stranger = T::fake(host, -1, false);
        }
        if (k % 1500 == 1499) {
            host.send(ctx.sim, 0);
            for (int s = 0; s < 20; s++) ctx.sim.advance(Ctx::kFrame, 1000);
            ctx.check("host: bodies stay finite after " + std::to_string(k + 1) + " messages", finiteCars(ctx.sim));
        }
    }
    ctx.check("host: still hosting, same id", host.role == NetSession::HOST && host.self == 0);
    ctx.check("host: bounded vehicles", (int)ctx.sim.cars.size() <= Sim::kMaxBodies, std::to_string(ctx.sim.cars.size()));
    ctx.check("host: bounded lines", ctx.sim.ropes.size() <= 64 && T::known(host) <= 64 + 64, std::to_string(ctx.sim.ropes.size()));
    ctx.check("host: bounded chat and events", host.gameIn.size() <= 300 && host.events.size() <= 300);
    int ownedByOne = 0;
    for (const Car& c : ctx.sim.cars) ownedByOne += c.remote && NetSession::ownerOf(c.netId) == 1;
    ctx.check("host: no guest has more than the allowed vehicles", ownedByOne <= net::kMaxCarsPerPlayer, std::to_string(ownedByOne));
    for (const Rope& r : ctx.sim.ropes) {
        ctx.check("host: every line is sane", std::isfinite(r.length + r.maxLen + r.k + r.c + r.breakN + r.stallN) && r.a.kind == ANCHOR_CAR && r.a.car >= 0 &&
                  r.a.car < (int)ctx.sim.cars.size() && r.stallN >= (r.kind == ROPE_WINCH ? 1 : 0) && r.b.hook >= 0 && r.b.hook <= 1);
        if (!(r.a.kind == ANCHOR_CAR && r.a.car >= 0 && r.a.car < (int)ctx.sim.cars.size())) break;
    }

    // 2. A client hearing the host (a hostile one).
    Sim csim;
    otherMachine(csim, MAP_PROVING_GROUND);
    NetSession client;
    client.role = NetSession::CLIENT;
    client.self = 3;
    client.adopt(csim);
    auto* hostConn = T::fake(client, 0, true);
    for (int k = 0; k < 6000; k++) {
        std::vector<uint8_t> m = fz.mutate(seeds[fz.below(seeds.size())]);
        T::handle(client, &csim, hostConn, m);
        if (client.role != NetSession::CLIENT) {  // (a REJECT can end it: start again)
            client.role = NetSession::CLIENT;
            client.self = 3;
            client.error.clear();
        }
        client.levelFresh = false;
        if (client.levelText.size() > 1000) client.levelText.clear();
        client.gameIn.clear();
        if (k % 1500 == 1499) {
            for (int s = 0; s < 20; s++) csim.advance(Ctx::kFrame, 1000);
            ctx.check("client: bodies stay finite after " + std::to_string(k + 1) + " messages", finiteCars(csim));
        }
    }
    ctx.check("client: bounded vehicles and lines", (int)csim.cars.size() <= Sim::kMaxBodies && csim.ropes.size() <= 64);
    ctx.check("client: my own vehicle is untouched", !csim.cars[csim.player].remote);

    // 3. States and pieces of states, at both.
    {
        Car& src = origin.cars[0];
        src.netId = (1u << 16) | 1;
        std::vector<uint8_t> good = T::state(enc[0], origin, src, 0.5);
        std::vector<std::vector<uint8_t>> grid;
        Sim target;
        target.world = World(MAP_PROVING_GROUND);
        target.setupEmpty();
        NetSession rx;
        rx.self = 0;
        rx.role = NetSession::HOST;
        rx.adopt(target);
        ctx.check("states: the truck they're for is there", T::apply(rx, target, seeds[0], 1) && target.cars[0].buildCount == src.buildCount);
        const uint32_t id = (1u << 16) | 1;
        double t = 0.5;
        for (int k = 0; k < 4000; k++) {
            std::vector<uint8_t> d = fz.below(4) ? fz.mutate(good) : good;
            if (fz.below(3) == 0) {  // keep the front true, garble numbers further in
                for (size_t b = 0; b < 8; b++) d[fz.below(d.size())] = (uint8_t)fz.rng();
            }
            std::vector<uint8_t> clean = good;
            std::memcpy(&clean[0], &t, 8);  // (a rising time, so a good one isn't thrown out as old)
            t += 0.05;
            T::snapshot(rx, target, id, fz.below(3) ? d : clean);
            const size_t chunk = 1150;
            const uint8_t n = (uint8_t)(fz.below(6) ? (d.size() + chunk - 1) / chunk : fz.rng());
            const uint8_t f = (uint8_t)(fz.below(4) ? fz.below(std::max<size_t>(n, 1)) : fz.rng());
            T::fragment(rx, target, id, (uint32_t)fz.below(5), f, n, std::vector<uint8_t>(d.begin(), d.begin() + std::min(d.size(), chunk)));
            if (k % 1000 == 999) {
                T::apply(rx, target, seeds[0], 1);
                for (int s = 0; s < 10; s++) target.advance(Ctx::kFrame, 1000);
                ctx.check("states: bodies stay finite after " + std::to_string(k + 1), finiteCars(target));
            }
        }
        ctx.check("states: bounded", (int)target.cars.size() <= Sim::kMaxBodies);
    }

    // 3b. The same, built field by field (the byte-flipping above rarely lands a whole valid
    // message with just one number wrong; this does, so what's accepted gets stepped in the physics).
    {
        auto special = [&]() -> float {
            static const float v[] = {0.f, 1.f, -1.f, 0.5f, 2.f, 8.f, 30.f, 100.f, 250000.f, 3000.f, 1e9f, -1e9f, 1e30f, -1e30f, 1e-30f, 49000.f, 60000.f, 1000.f, -0.f,
                                      std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
            return fz.below(3) ? v[fz.below(sizeof v / sizeof *v)] : (float)((int)fz.below(2000) - 1000) / 10.0f;
        };
        auto small = [&]() -> uint8_t { return fz.below(5) ? (uint8_t)fz.below(4) : (uint8_t)fz.rng(); };
        auto put = [](std::vector<uint8_t>& b, const auto& v) {
            const uint8_t* q = reinterpret_cast<const uint8_t*>(&v);
            b.insert(b.end(), q, q + sizeof v);
        };
        NetSession rx;
        rx.self = 0;
        rx.role = NetSession::HOST;
        Sim target;
        target.world = World(MAP_PROVING_GROUND);
        target.setupEmpty();
        rx.adopt(target);
        const uint32_t base = 1u << 16;
        int spawned = 0, roped = 0, stated = 0;
        for (int k = 0; k < 1500; k++) {
            // A vehicle with one or two odd fields.
            std::vector<uint8_t> m = {SPAWN};
            const uint32_t id = base | (uint32_t)(2 + fz.below(2));  // (vehicle 1 is kept true, for the states below)
            put(m, id);
            m.push_back(fz.below(8) ? 0 : small());              // kind
            m.push_back((uint8_t)(fz.below(6) ? 1 + fz.below(4) : fz.rng()));  // model
            for (int j = 0; j < MOD_SLOTS; j++) m.push_back(fz.below(3) ? 0 : small());  // mods
            for (int j = 0; j < 3; j++) put(m, fz.below(3) ? 0.4f : special());  // color
            put(m, fz.below(3) ? (float)(-40 + 12 * (int)fz.below(3)) : special());
            put(m, fz.below(3) ? 0.6f : special());
            put(m, fz.below(3) ? -40.f : special());
            put(m, fz.below(3) ? 0.f : special());  // yaw
            m.push_back(small());
            put(m, (uint32_t)fz.below(4));
            if (T::apply(rx, target, m, 1)) spawned++;
            if (k % 4 == 0) T::apply(rx, target, seeds[0], 1);
            // A line between them (the board is cleared of lines each time, so the id is free).
            std::vector<uint8_t> r = {ROPE};
            put(r, base | (uint32_t)(1 + fz.below(6)));
            r.push_back(fz.below(5) ? (uint8_t)fz.below(2) : small());
            for (int e = 0; e < 2; e++) {
                r.push_back(e == 0 && fz.below(8) ? 0 : small());
                put(r, fz.below(8) ? base | (uint32_t)(1 + fz.below(3)) : (uint32_t)fz.rng());
                r.push_back(fz.below(10) < 7 ? (uint8_t)fz.below(2) : (uint8_t)fz.rng());  // the hook
                for (int j = 0; j < 3; j++) put(r, fz.below(3) ? -50.f + (float)j : special());
            }
            for (float def : {8.f, 30.f, 2.5e5f, 3000.f, 9e4f, 4.4e4f}) put(r, fz.below(8) ? def : special());
            put(r, fz.below(4) ? 0.5 + 0.05 * k : (double)special());  // when (any number: it only says when to show it)
            if (T::apply(rx, target, r, 1)) {
                roped++;
                for (int s = 0; s < 2; s++) target.advance(Ctx::kFrame, 4);  // (a line that got in is pulling on something now)
            }
            if (!finiteCars(target)) {
                ctx.check("structured: bodies stay finite after a line (iteration " + std::to_string(k) + ")", false);
                break;
            }
            for (uint32_t j = 1; j <= 6; j++) {
                std::vector<uint8_t> d = {UNROPE};
                put(d, base | j);
                put(d, fz.below(4) ? 0.5 + 0.05 * k : (double)special());
                T::apply(rx, target, d, 1);
            }
            // A state with one odd number.
            Car& src = origin.cars[0];
            src.netId = base | 1;
            std::vector<uint8_t> st = T::state(enc[0], origin, src, 0.5 + 0.05 * k);
            // (Where the numbers are: the base, the center, the turn, the speed, the controls and engine, each wheel's angle and spin.)
            static const size_t floatAt[] = {12, 16, 20, 24, 28, 29, 35, 41, 44, 47, 49, 51, 55, 59, 62, 66, 69, 76};
            for (int j = 0, n = (int)fz.below(3); j < n; j++) {
                const float v = special();
                const size_t at = floatAt[fz.below(sizeof floatAt / sizeof *floatAt)];
                if (at + 4 <= st.size()) std::memcpy(&st[at], &v, 4);
            }
            if (fz.below(6) == 0) st[53] = (uint8_t)fz.rng();  // gear
            T::snapshot(rx, target, base | 1, st);
            stated++;
            T::play(rx, target, 0.6 + 0.05 * k);
            if (k % 3 == 0) {
                for (int s = 0; s < 3; s++) target.advance(Ctx::kFrame, 4);
                if (!finiteCars(target)) {
                    ctx.check("structured: bodies stay finite (iteration " + std::to_string(k) + ")", false);
                    break;
                }
            }
            if (k % 14 == 13)  // (and now and then, the vehicles go: so the next spawns build new ones)
                for (int j = 1; j <= 3; j++) {
                    std::vector<uint8_t> d = {DESPAWN};
                    put(d, base | (uint32_t)j);
                    T::apply(rx, target, d, 1);
                }
        }
        ctx.check("structured: bodies finite after all of it", finiteCars(target));
        ctx.check("structured: it reached the physics (" + std::to_string(spawned) + " vehicles, " + std::to_string(roped) + " lines, " + std::to_string(stated) + " states)", spawned > 100 && roped > 50);
    }

    // 4. Datagrams, with the right secret (so the parsing past the first check is reached) and without.
    {
        // Peer 2's truck, known to the host (so its states are relayed and taken).
        const uint32_t nid = (2u << 16) | 1;
        origin.cars[0].netId = nid;
        NetSession enc2;
        enc2.self = 2;
        ctx.check("a guest's truck is accepted", T::apply(host, ctx.sim, T::spawn(enc2, origin.cars[0]), 2));
        std::vector<uint8_t> payload = T::state(enc2, origin, origin.cars[0], 0.7);
        payload.resize(std::min<size_t>(payload.size(), 1150));
        std::vector<uint8_t> base(22, 0);
        base[0] = 0x7a, base[1] = 0xc2, base[2] = 1, base[3] = 2;
        std::memcpy(&base[4], &nid, 4);
        base[12] = 0, base[13] = 1;
        base.insert(base.end(), payload.begin(), payload.end());
        uint64_t token = 0;
        for (int k = 0; k < 6000; k++) {
            if (k % 100 == 0) {  // (a fresh guest: the rate limits start full again)
                T::dropFakes(host);
                token = T::token(T::fake(host, 2, true));
                std::memcpy(&base[14], &token, 8);
                std::vector<uint8_t> hello = base;
                hello[2] = 2;
                hello.resize(22);
                T::datagram(host, ctx.sim, hello, 0x7f000001, 40000);
            }
            std::vector<uint8_t> d = fz.below(3) ? fz.mutate(base) : base;
            if (d.size() >= 22 && fz.below(2)) std::memcpy(&d[14], &token, 8);  // (the secret kept right)
            T::datagram(host, ctx.sim, d, 0x7f000001, (uint16_t)(1024 + fz.below(60000)));
            T::datagram(client, csim, d, 0x7f000001, 27015);
        }
        ctx.check("datagrams: bounded", (int)ctx.sim.cars.size() <= Sim::kMaxBodies && (int)csim.cars.size() <= Sim::kMaxBodies);
        for (int s = 0; s < 20; s++) ctx.sim.advance(Ctx::kFrame, 1000), csim.advance(Ctx::kFrame, 1000);
        ctx.check("datagrams: bodies stay finite", finiteCars(ctx.sim) && finiteCars(csim));
    }

    // 5. Text formats: a level, a job, a "found game" announcement.
    {
        std::string level = "CRUMPLEZONE LEVEL 1\nname FUZZ\nmap 1\nmode race\nlaps 3\nstart 1 2 90\ngate 10 10 0 8\ngate 20 20 90 8\nrock 5 5 3 3 3 1 7 0.5\nbox 1 1 1 2 2 2 0 0 0 0.5 0.5 0.5 0\n"
                            "log 0 0 0 5 0 5 0.4\ntree 3 3 8 1 0.5\nstation 9 9 0 SHOP\nremove 1 1\nheights 100 100 1 2 3 4\nsurfaces 100 100 1 2\nwater 100 100 1 2\n";
        int parsed = 0;
        for (int k = 0; k < 3000; k++) {
            const std::vector<uint8_t> mv = fz.mutate(std::vector<uint8_t>(level.begin(), level.end()));
            const std::string m(mv.begin(), mv.end());
            std::string text = k % 7 == 0 ? m : level.substr(0, fz.below(level.size())) + m.substr(0, fz.below(60));
            Level lv;
            std::string err;
            parsed += parseLevelText(text, lv, &err);
        }
        ctx.check("levels: mangled text is parsed or refused, never a crash (" + std::to_string(parsed) + " parsed)", true);
        const std::string job = "job|2|SCOUT|ABOUT|10|0|20|NAME|2|0|3|1|2|A|3|4|B|5|6|C";
        Job good;
        ctx.check("jobs: a real one parses", Job::parse(job, good) && good.spots.size() == 3);
        int accepted = 0;
        bool inRange = true;
        for (int k = 0; k < 4000; k++) {
            std::vector<uint8_t> m = fz.mutate(std::vector<uint8_t>(job.begin(), job.end()));
            Job j;
            if (Job::parse(std::string(m.begin(), m.end()), j)) {
                accepted++;
                inRange &= j.kind >= 0 && j.kind < JOB_KIND_COUNT && j.spots.size() <= 32 && j.visited.size() == j.spots.size();
            }
        }
        ctx.check("jobs: everything that parses is in range (" + std::to_string(accepted) + " did)", inRange);
        Job nasty;
        ctx.check("jobs: a huge spot count is refused", !Job::parse("job|0|T|A|0|0|0|N|2|0|715827883|1|2|X", nasty) && !Job::parse("job|9|T|A|0|0|0|N|2|0|0", nasty) &&
                                                         !Job::parse("job|0|T|A|nan|0|0|N|2|0|0", nasty));
    }
    ctx.check("(" + std::to_string(iters) + " host messages, 6000 client, 4000 states, 6000 datagrams, 3000 levels, 4000 jobs)", true);
    host.close();
}
