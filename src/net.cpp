#ifdef _WIN32
#define _CRT_RAND_S  // (rand_s: the secrets come from the system's generator)
#endif
#include "thread_cache.h"
// Multiplayer session: see net.h.
#include "net.h"
#include "procworld.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "mods.h"
#include "nettunnel.h"
#include "rendezvous.h"
#include "realterrain.h"
#include "world.h"

#include "netsock.h"

using net::clockNow;
using net::randomBytes;
using net::sameBytes;

namespace {

// ---- Wire format --------------------------------------------------------------
// Reliable messages: u16 length (of what follows), u8 type, payload.
// Datagrams: u16 magic, u8 type, u8 player, u32 netId, u32 seq, u8 frag, u8 frags, payload.

enum MsgType : uint8_t {
    MSG_HELLO = 1,    // client -> host: version, name, build (8)
    MSG_WELCOME,      // host -> client: your id, the map, (8) the host's build
    MSG_REJECT,       // host -> client: why not
    MSG_PLAYER,       // id, name: someone is in the session
    MSG_LEFT,         // id
    MSG_SPAWN,        // a vehicle appeared or was rebuilt
    MSG_DESPAWN,      // netId
    MSG_ROPE,         // a line was hooked
    MSG_UNROPE,       // netId
    MSG_PING,         // f64 time
    MSG_PONG,         // f64 time (echoed); from the host also f64 its shared clock then
    MSG_RTT,          // id, f32 ping: the host telling everyone the pings
    MSG_CLOCK,        // f64 shared time, f32 hours then, f32 hours per second: the host's time of day
    MSG_LEVEL,        // u32 serial, u16 part, u16 parts, bytes: the level's text, in parts
    MSG_GAME,         // u8 player, str, f64 shared time it was said: race chatter
    MSG_LAMPS,        // u32 car id, u8 LampMode: reliable, also while the car sleeps
    MSG_CHALLENGE,    // host -> client: 16 random bytes; answer with MSG_AUTH (only if there's a password)
    MSG_AUTH,         // client -> host: HMAC-SHA-256(password, those 16 bytes)
    // Handing a vehicle over (NetSession::requestTake). All three: u32 its id, u32 the id it's to have (the taker's).
    MSG_TAKE,         // taker -> host: may I have it; host -> its owner: they ask
    MSG_GIVE,         // owner -> host: it's theirs now; host -> everyone: it changed hands
    MSG_REFUSE,       // owner -> host -> taker: no (+ str why)
    MSG_SECTIONS,     // host -> client, in the welcome: u8 index, u8 count, u8 SectionType, u16 part, u16 parts, bytes: the park's edited sections (.czs text)
};
// (WELCOME: u8 id, u8 map, u64 the guest's datagram secret, str the host's build.)
enum DgramType : uint8_t { DG_SNAP = 1, DG_HELLO = 2, DG_ACK = 3 };  // (an ack: u32 netId, u32 seq, ...: states we have, to tell the next against)
constexpr uint16_t kMagic = 0xC27A;
// ... and the u64 secret of whoever the datagram is for (told in the welcome): anyone else's is dropped.
constexpr size_t kDgramHeader = 2 + 1 + 1 + 4 + 4 + 1 + 1 + 8;
constexpr size_t kChunk = 1150;
constexpr int kMaxFragments = 32;  // (a state is a few KB: more is garbage)
constexpr double kPosLimit = 5.0e4;  // m: nothing in any world is further out than this
constexpr size_t kMaxPending = 8192, kMaxPendingBytes = 32u << 20;
constexpr size_t kMaxOut = 16u << 20;  // queued for one peer that isn't reading

struct Writer {
    std::vector<uint8_t> b;
    Writer() { b.reserve(96); }  // (a message is a few puts: one grab, not one per doubling)
    template <class T> void put(T v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
    }
    void paint(const CarPaint& p) {
        uint8_t b[kPaintWire];
        paintToWire(p, b);
        for (uint8_t v : b) put<uint8_t>(v);
    }
    void str(const std::string& s) {
        put<uint8_t>((uint8_t)std::min<size_t>(s.size(), 255));
        b.insert(b.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 255));
    }
    void v3(const V3& v) { put<float>((float)v.x), put<float>((float)v.y), put<float>((float)v.z); }
};
struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (at + sizeof(T) > n) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
    std::string str() {
        size_t len = get<uint8_t>();
        if (at + len > n) {
            ok = false;
            return "";
        }
        std::string s((const char*)p + at, len);
        at += len;
        return s;
    }
    // CarPaint (paint.h) if it's there: an older peer's message ends before it (factory colours).
    bool paint(CarPaint& out) {
        if (at + kPaintWire > n) return false;
        uint8_t b[kPaintWire];
        for (int k = 0; k < kPaintWire; k++) b[k] = get<uint8_t>();
        return paintFromWire(b, out);
    }
    V3 v3() {
        float x = get<float>(), y = get<float>(), z = get<float>();
        return V3(x, y, z);
    }
};

// ---- Checks on what comes in ----------------------------------------------------------

bool okNum(double v, double lim) { return std::isfinite(v) && std::fabs(v) <= lim; }
bool okPos(const V3& p) { return okNum(p.x, kPosLimit) && okNum(p.y, kPosLimit) && okNum(p.z, kPosLimit); }


// Race chatter only the host may say: a guest sending these is lying about who's in charge.
bool hostOnlyChatter(const std::string& t) { return t.rfind("job", 0) == 0 || t.rfind("restart", 0) == 0 || t.rfind("procshift", 0) == 0; }

std::vector<uint8_t> frame(MsgType t, const Writer& w) {
    std::vector<uint8_t> m;
    const uint16_t len = (uint16_t)(w.b.size() + 1);
    m.push_back(len & 0xff);
    m.push_back(len >> 8);
    m.push_back(t);
    m.insert(m.end(), w.b.begin(), w.b.end());
    return m;
}

// Which catalog vehicle (and mods) or prop a spec is, so another machine can build it.
struct SpecId {
    uint8_t kind = 0;  // 0 vehicle, 1 crash box, 2 barrier, 255 unknown
    uint8_t model = 0, mods[MOD_SLOTS] = {0, 0, 0, 0, 0};  // (every garage slot: engine .. tires)
};
SpecId specId(const VehicleSpec* s) {
    SpecId id;
    if (s == &crashBoxSpec()) return id.kind = 1, id;
    if (s == &barrierSpec()) return id.kind = 2, id;
    const auto& cat = vehicleCatalog();
    if (s->model >= 0) {
        id.model = (uint8_t)s->model;
        for (int k = 0; k < MOD_SLOTS; k++) id.mods[k] = (uint8_t)s->mods[k];
        return id;
    }
    for (size_t k = 0; k < cat.size(); k++)
        if (&cat[k] == s) return id.model = (uint8_t)k, id;
    id.kind = 255;
    return id;
}
const VehicleSpec* specFrom(const SpecId& id) {
    if (id.kind == 1) return &crashBoxSpec();
    if (id.kind == 2) return &barrierSpec();
    if (id.kind != 0 || id.model >= vehicleCatalog().size()) return nullptr;
    ModSet m;
    for (int k = 0; k < MOD_SLOTS; k++) {
        // (Each pick must be a real option: tunedSpec keeps a spec for every combination it's asked for.)
        if (id.mods[k] >= modOptions(id.model, k).size()) return nullptr;
        m.pick[k] = id.mods[k];
    }
    return &tunedSpec(id.model, m);
}

// Center of mass of the body nodes at rest, in the car's own frame (states are sent
// relative to the rigid body: node offsets from where the undented car would put them).
V3 restCom(const Car& c) {
    V3 s;
    double m = 0;
    for (int i = 0; i < c.bodyNodes; i++) {
        s += c.restLocal[i] * c.nodes[i].m;
        m += c.nodes[i].m;
    }
    return m > 0 ? s / m : V3();
}

Quat nlerp(Quat a, Quat b, double t) {
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) b = Quat{-b.w, -b.x, -b.y, -b.z};
    Quat q{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    q.normalize();
    return q;
}

int findCar(const Sim& sim, uint32_t netId) {
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (sim.cars[i].netId == netId) return i;
    return -1;
}

void eraseCar(Sim& sim, int i) {
    sim.carRemoved(i);
    sim.cars.erase(sim.cars.begin() + i);
    if (sim.player > i) sim.player--;
    if (sim.player >= (int)sim.cars.size()) sim.player = std::max(0, (int)sim.cars.size() - 1);
}

}  // namespace

namespace net {

// ---- The shared clock ----------------------------------------------------------------

bool ClockSync::add(double sent, double hostT, double got) {
    const double trip = got - sent;
    if (!std::isfinite(trip) || !std::isfinite(hostT) || trip < 0 || trip > 5 || std::fabs(hostT) > 1e9) return false;
    recent.push_back({trip, hostT + trip / 2 - got});  // (the answer was made halfway through the trip)
    while (recent.size() > 8) recent.pop_front();
    const Sample* best = &recent.front();
    for (const Sample& s : recent)
        if (s.rtt < best->rtt) best = &s;
    samples++;
    if (!synced || std::fabs(best->offset - offset) > 0.5) offset = best->offset;  // (the first, or way off: take it)
    else offset += (best->offset - offset) * 0.3;
    synced = true;
    return true;
}

double ClockSync::rtt() const {
    double r = 0;
    for (size_t k = 0; k < recent.size(); k++) r = k ? std::min(r, recent[k].rtt) : recent[k].rtt;
    return r;
}

void Lateness::add(double late, double sinceLast) {
    if (!std::isfinite(late)) return;
    late = std::min(std::max(late, -1.0), 2.0);
    if (mean < 0) {
        mean = std::max(0.0, late), dev = 0;
    } else {
        const double e = late - mean;
        mean = std::max(0.0, mean + e * 0.1);
        dev += (std::fabs(e) - dev) * 0.1;
    }
    if (sinceLast > 0 && sinceLast < 0.2) gap += (sinceLast - gap) * 0.1;  // (parked ones, twice a second, don't count)
}

void Timeline::step(double shared, double need) {
    need = std::min(std::max(need, kPlaybackDelay), kMaxDelay);
    if (!started()) {
        delay = need, render = shared - need, at = shared;
        return;
    }
    const double dt = shared - at;
    at = shared;
    if (dt <= 0) return;  // (the clock estimate stepped back: hold)
    // More delay at once (a late state is worse than a slower clock); less, slowly.
    delay = need > delay ? need : delay + (need - delay) * std::min(1.0, dt * 0.25);
    const double err = shared - delay - (render + dt);
    if (err > 0.5) {
        render = shared - delay;  // far behind (a long stall): skip ahead
        return;
    }
    // Close: 0.9 to 1.1 times real speed; further off, 0.5 to 2.
    const double lo = std::fabs(err) < 0.1 ? 0.9 : 0.5, hi = std::fabs(err) < 0.1 ? 1.1 : 2.0;
    render += dt + std::min(std::max(err, dt * (lo - 1)), dt * (hi - 1));
}

}  // namespace net

// ---- Connection and remote-vehicle state ----------------------------------------

struct NetSession::Snap {
    double t = 0;
    uint32_t build = 0;
    std::vector<int16_t> q;  // the dents as sent (mm; `local` is these in m)
    V3 com, vel;
    Quat rot;
    std::vector<V3> local;  // node offsets from the rigid rest shape
    std::vector<float> wheelAngle, wheelOmega;
    std::vector<uint8_t> wheelFlags;
    float steer = 0, brake = 0, damage = 0;
    float rpm = 0, load = 0, boost = 0;
    int8_t gear = 1;
    uint8_t status = 0;
    std::vector<std::pair<uint32_t, float>> ropeLen;
};

struct NetSession::Remote {
    uint32_t netId = 0;
    std::deque<Snap> buf;
    double newestT = -1, newestArrival = 0;  // (shared clock)
    net::Lateness late;
    std::deque<std::pair<uint32_t, std::vector<int16_t>>> got;  // the dents of the last states taken (seq): what the sender tells the next against
    // Easing out a coast that guessed wrong: what's added to where the states say it is.
    V3 ease, lastShown, lastVel;
    Quat easeRot, lastRot;  // (and the turn: shown now, each state corrects the guess at it too)
    bool wasHeld = false;   // (in a crash with one of ours last frame)
    double heldAt = 0;      // when it was taken onto our physics (vehicle time)
    bool shown = false;
    double coastedTo = -1;  // the newest state's time while coasting past it last frame (-1: wasn't)
    struct Assembly {
        bool active = false;
        uint32_t seq = 0;
        std::vector<uint8_t> data, got;
        int count = 0;
    };
    std::array<Assembly, 3> assemblies;  // A newer partial packet must not discard an older complete one.
};

namespace {
constexpr uint16_t kTunMagic = 0xC27B;
constexpr size_t kTunHeader = 2 + 1 + 4 + 8;  // magic, type, pair, the pair's secret
enum TunType : uint8_t { T_PUNCH = 1, T_PUNCH_ACK, T_DATA, T_ACK, T_CLOSE };
constexpr double kTunnelDead = 15;  // s without a word: the tunnel is gone
constexpr double kDirectQuiet = 4;  // s without a word on a punched path: the guest tries the relay
}  // namespace

struct NetSession::Tunnel {
    uint32_t pair = 0;
    uint64_t secret = 0, relayKey = 0;
    uint32_t ip = 0;  // the punched path (unused while relayed)
    uint16_t port = 0;
    bool relayed = false, closed = false;
    double born = 0;
    net::ReliableStream stream;
};

NetSession::NetSession() {}
NetSession::~NetSession() { close(); }

int NetSession::playerCount() const {
    int n = 0;
    for (const NetPlayer& p : players) n += p.active;
    return n;
}

std::string NetSession::ownerName(uint32_t netId) const {
    const int o = ownerOf(netId);
    return o >= 0 && o < net::kMaxPeers && players[o].active ? players[o].name : std::string("?");
}

void NetSession::close() {
    // Saying goodbye: the guests hear the host is gone (not 15 or 30 s of silence later), the
    // host hears a guest left, and the backend lets the code go.
    if (role == HOST) {
        Writer w;
        w.str("the host closed the game");
        const auto bye = frame(MSG_REJECT, w);
        for (Conn* c : conns)
            if (c->hello && !c->kill) sendTo(*c, bye);
        serviceTunnels(clockNow());
    }
    for (Conn* c : conns)
        if (c->tun && !c->tun->closed) {
            const uint8_t why = 0;
            tunPacket(*c->tun, 5, &why, 1);
            tunPacket(*c->tun, 5, &why, 1);
        }
    if (rvKey && udpFd >= 0) {
        rv::Msg m;
        m.type = rv::RV_CLOSE, m.code = joinCode.empty() ? codeWanted : joinCode, m.key = rvKey;
        sendBackend(rv::encode(m));
    }
    for (Conn* c : conns) {
        if (c->fd >= 0) closeSock(c->fd);
        delete c;
    }
    conns.clear();
    if (listenFd >= 0) closeSock(listenFd);
    if (udpFd >= 0) closeSock(udpFd);
    if (findFd >= 0) closeSock(findFd);
    if (connectFd >= 0) closeSock(connectFd);
    listenFd = udpFd = findFd = connectFd = -1;
    joinStage = 0;
    delete (sockaddr_in*)hostUdp;
    hostUdp = nullptr;
    for (Remote* r : remotes) delete r;
    remotes.clear();
    knownCars.clear();
    knownRopes.clear();
    pending.clear();
    pendingBytes = 0;
    players.assign(net::kMaxPeers, NetPlayer());
    role = OFF;
    self = -1;
    nextCarId = nextRopeId = 1;
    adopted = false;
    udpReady = false;
    sessionStart = -1;
    clock = net::ClockSync();
    view = net::Timeline();
    vehicleAt = -1, lastPlayNow = -1e18, vehicleLeadNow = 0;
    nextSync = 0;
    dayBase = daySentAt = -1;
    dayHours = -1, dayRate = 0, dayFresh = false;
    heldGame.clear();
    heldWorld.clear();
    upStreams.clear();
    askId = askNew = 0;
    takes.clear();
    homeOf.clear();
    for (uint32_t& r : ridingOf) r = 0;
    upAcks.clear();
    intros.clear();
    codeIntro = Intro();
    codeState = CODE_OFF;
    joinCode.clear();
    rvKey = rvNonce = rvCookie = 0;
    rvRetired = false;
    rvTries = 0;
    directFound = false;
    directIp = 0, directPort = 0;
    udpHeld.clear();
    sections.clear();
    sectionsIn.clear();
    sectionsNext = 0;
    sectionsPending = false;
}

bool NetSession::host(int port, const std::string& name, int m, int maxP, const std::string& session) {
    close();
    portInUse = false;
    netInit();
    listenFd = newSocket(SOCK_STREAM);
    udpFd = newSocket(SOCK_DGRAM);
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((uint16_t)port);
    if (!bindAddress.empty() && inet_pton(AF_INET, bindAddress.c_str(), &a.sin_addr) != 1) {
        error = "can't listen on " + bindAddress;
        close();
        return false;
    }
    if (port <= 0 || port > 65535 || listenFd < 0 || udpFd < 0 || bind(listenFd, (sockaddr*)&a, sizeof a) < 0 || listen(listenFd, 32) < 0 ||
        bind(udpFd, (sockaddr*)&a, sizeof a) < 0) {
        const int e = errno;
        const bool inUse = port > 0 && port <= 65535 && addrInUse();
        error = inUse ? "port " + std::to_string(port) + " is in use" : std::string("can't open port ") + std::to_string(port) + ": " + std::strerror(e);
        close();
        portInUse = inUse;
        return false;
    }
    nonBlocking(listenFd);
    nonBlocking(udpFd);
    hostPort = port;
    // Listen for "who's hosting?" from the local network (if the port's taken, say by another
    // game hosted on this machine, it just can't be found that way: typing the address works).
    findFd = discovery ? newSocket(SOCK_DGRAM) : -1;
    if (findFd >= 0) {
        setsockopt(findFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
        sockaddr_in fa{};
        fa.sin_family = AF_INET;
        fa.sin_addr.s_addr = INADDR_ANY;
        fa.sin_port = htons((uint16_t)net::kDiscoveryPort);
        if (bind(findFd, (sockaddr*)&fa, sizeof fa) < 0) {
            closeSock(findFd);
            findFd = -1;
        } else {
            nonBlocking(findFd);
            setsockopt(findFd, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);  // (the announcements)
        }
    }
    maxPlayers = std::min(std::max(maxP, 2), net::kMaxPeers);
    sessionName = net::sanitize(session, 40);
    strikes.clear();
    log.clear();
    refused = 0;
    sessionId = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() * 2654435761u ^ (uint32_t)port;
    if (!sessionId) sessionId = 1;
    nextBeacon = 0;
    role = HOST;
    self = 0;
    map = m;
    players[0].active = true;
    players[0].name = net::sanitize(name, net::kMaxNameLen);
    return true;
}

bool NetSession::join(const std::string& address, const std::string& name, double timeout) {
    if (!beginJoin(address, name, timeout)) return false;
    for (;;) {
        const int st = pollJoin();
        if (st > 0) return true;
        if (st < 0) return false;
        pollfd rp{};
        const bool udp = joinStage >= 10 || (!conns.empty() && conns[0]->tun);
        rp.fd = udp ? udpFd : conns.empty() ? connectFd : conns[0]->fd;
        rp.events = conns.empty() && !udp ? POLLOUT : POLLIN;
        if (rp.fd >= 0) pollFds(&rp, 1, udp ? 5 : 20);
    }
}

bool NetSession::beginJoin(const std::string& address, const std::string& name, double timeout) {
    close();
    passwordIssue = PW_OK;
    joinFail = JF_NONE;
    lostWhy.clear();
    netInit();
    std::string hostName, why;
    int port = 0;
    if (!net::parseAddress(address, hostName, port, &why)) {
        error = why;
        return false;
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (hostName.empty() || getaddrinfo(hostName.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        error = hostName.empty() ? "no address" : "can't find " + hostName;
        return false;
    }
    sockaddr_in addr = *(sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    connectFd = newSocket(SOCK_STREAM);
    nonBlocking(connectFd);
    connect(connectFd, (sockaddr*)&addr, sizeof addr);
    hostUdp = new sockaddr_in(addr);
    joinAddress = address;
    joinName = net::sanitize(name, net::kMaxNameLen);
    if (joinName.empty()) joinName = "PLAYER";
    joinTimeout = timeout;
    joinStage = 1;
    joinT0 = clockNow();
    error.clear();
    return true;
}

int NetSession::pollJoin() {
    auto fail = [&](const std::string& why) {
        error = why;
        if (connectFd >= 0) closeSock(connectFd);
        connectFd = -1;
        const std::string keep = error;
        close();
        error = keep;
        return -1;
    };
    const double now = clockNow();
    if (joinStage == 1) {  // the connection: answered yet?
        pollfd pf{};
        pf.fd = connectFd;
        pf.events = POLLOUT;
        if (pollFds(&pf, 1, 0) <= 0) {
            if (now - joinT0 > joinTimeout) return fail("no answer from " + joinAddress);
            return 0;
        }
        int err = 0;
        socklen_t el = sizeof err;
        if (getsockopt(connectFd, SOL_SOCKET, SO_ERROR, (char*)&err, &el) < 0 || err) return fail("no answer from " + joinAddress);
        int one = 1;
        setsockopt(connectFd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        Conn* c = new Conn;
        c->fd = connectFd;
        c->peer = 0;
        connectFd = -1;
        conns.push_back(c);
        udpFd = newSocket(SOCK_DGRAM);
        sockaddr_in any{};
        any.sin_family = AF_INET;
        bind(udpFd, (sockaddr*)&any, sizeof any);
        nonBlocking(udpFd);
        role = CLIENT;
        Writer w;
        w.put<uint16_t>(net::kVersion);
        w.str(joinName);
        w.str(build);
        sendTo(*c, frame(MSG_HELLO, w));
        joinStage = 2;
        joinT0 = now;
    }
    if (joinStage >= 10 && joinStage <= 11) {
        const int st = pollCodeJoin(now);
        if (st < 0) return fail(error);
        if (joinStage != 2) return 0;
    }
    if (joinStage == 2) {  // waiting to be let in
        if (role != CLIENT || conns.empty() || !live(*conns[0])) return fail(error.empty() ? "connection closed" : error);
        Conn& c = *conns[0];
        if (c.tun) pumpUdp(now), serviceTunnels(now);
        flush(c);
        readTcp(c);  // handles WELCOME / REJECT (other messages wait for adopt())
        if (c.tun) serviceTunnels(now);
        if (role != CLIENT || (self < 0 && !live(c))) return fail(error.empty() ? "connection closed" : error);
        if (self < 0) {
            if (now - joinT0 > joinTimeout) {
                if (c.tun && c.tun->stream.lastHeard <= c.tun->born) {  // (by code, and never a word back: no way through)
                    joinFail = JF_NO_PATH;
                    return fail("couldn't connect to your friend's game");
                }
                return fail("the host didn't let us in");
            }
            return 0;
        }
        joinStage = 3;
        joinT0 = joinQuiet = now;
    }
    if (joinStage == 3) {
        // The host sends its vehicles right behind the welcome: take them in now (they wait
        // in `pending` for adopt()), so we park clear of them rather than on one.
        if (role != CLIENT || conns.empty() || !live(*conns[0])) return fail(error.empty() ? "connection closed" : error);
        const size_t before = pending.size();
        if (conns[0]->tun) pumpUdp(now), serviceTunnels(now);
        readTcp(*conns[0]);
        if (role != CLIENT || !live(*conns[0])) return fail(error.empty() ? "connection closed" : error);
        if (pending.size() != before) joinQuiet = now;
        if (sectionsPending && now - joinT0 < joinTimeout) return 0;  // (the world can't be built without them)
        if (now - joinT0 < 0.5 && now - joinQuiet < 0.12) return 0;
        joinStage = 0;
        players[self].active = true;
        players[self].name = joinName;
        return 1;
    }
    return -1;
}

// ---- Reliable messages ------------------------------------------------------------

void NetSession::sendTo(Conn& c, const std::vector<uint8_t>& msg) {
    if (c.out.size() + (c.tun ? c.tun->stream.queued() : 0) + msg.size() > kMaxOut) {  // not reading what we send: that's enough of them
        if (role == HOST) violation(&c, "not reading");
        return;
    }
    c.out.insert(c.out.end(), msg.begin(), msg.end());
    flush(c);
}

void NetSession::broadcast(const std::vector<uint8_t>& msg, Conn* except) {
    for (Conn* c : conns)
        if (c != except && !c->kill && (role == CLIENT || c->hello)) sendTo(*c, msg);
}

void NetSession::flush(Conn& c) {
    if (c.tun) {  // (into the tunnel's stream: serviceTunnels sends it as the window allows)
        if (!c.out.empty() && c.tun->stream.write(c.out.data(), c.out.size())) bytesOut += c.out.size();
        c.out.clear();
        return;
    }
    while (!c.out.empty() && c.fd >= 0) {
        const int n = (int)::send(c.fd, (const char*)c.out.data(), (int)c.out.size(), MSG_NOSIGNAL);
        if (n <= 0) break;
        bytesOut += n;
        c.out.erase(c.out.begin(), c.out.begin() + n);
    }
}

void NetSession::note(const std::string& what) {
    if (!logRate.take(clockNow(), 1, 2, 10)) return;
    log.push_back(what);
    if (log.size() > 64) log.erase(log.begin(), log.begin() + (log.size() - 64));
}

// Host: a guest said something it may not, or too much: it gets no more of our time.
void NetSession::violation(Conn* c, const std::string& why) {
    if (!c || c->kill) return;
    refused++;
    c->kill = true;
    c->killT = clockNow();
    c->in.clear();
    note((c->peer >= 0 ? players[c->peer].name : std::string("?")) + " DROPPED: " + why);
}

void NetSession::readTcp(Conn& c) {
    uint8_t buf[16384];
    // (At most this much a call: what's left waits for the next frame.)
    size_t budget = role == HOST ? 131072 : 4u << 20;
    if (c.tun) {  // (what the tunnel's stream has put together, in order)
        std::vector<uint8_t>& rx = c.tun->stream.rx;
        const size_t k = std::min(budget, rx.size());
        if (k) {
            bytesIn += k;
            if (!c.kill) c.in.insert(c.in.end(), rx.begin(), rx.begin() + (long)k);
            rx.erase(rx.begin(), rx.begin() + (long)k);
            c.lastRx = clockNow();
        }
        budget = 0;
    }
    while (budget > 0 && c.fd >= 0) {
        const int n = (int)recv(c.fd, (char*)buf, (int)std::min(sizeof buf, budget), 0);
        if (n > 0) {
            bytesIn += n;
            budget -= std::min((size_t)n, budget);
            if (!c.kill) c.in.insert(c.in.end(), buf, buf + n);
            c.lastRx = clockNow();
            continue;
        }
        if (n == 0 || !wouldBlock()) {
            closeSock(c.fd);
            c.fd = -1;
        }
        break;
    }
    size_t at = 0;
    const bool guest = role == HOST;
    // (What a guest may send is small: before it's in, smaller still.)
    const size_t biggest = !guest ? 65535 : c.hello ? 1024 : 256;
    while (!c.kill && c.in.size() - at >= 2) {
        const size_t len = c.in[at] | (c.in[at + 1] << 8);
        if (len > biggest || (guest && len == 0)) {
            if (guest) violation(&c, "message size " + std::to_string(len));
            else at = c.in.size();
            break;
        }
        if (c.in.size() - at - 2 < len) break;
        if (guest) {
            const double now = clockNow();
            if (!c.msgs.take(now, 1, 150, 300) || !c.bytes.take(now, (double)len + 2, 65536, 262144)) {
                violation(&c, "sending too much");
                break;
            }
        }
        std::vector<uint8_t> msg(c.in.begin() + at + 2, c.in.begin() + at + 2 + len);
        at += 2 + len;
        handle(lastSim, &c, msg);
        if (role == OFF) break;
    }
    if (at) c.in.erase(c.in.begin(), c.in.begin() + std::min(at, c.in.size()));
}

void NetSession::admit(Conn& c, const std::string& name) {
    int id = -1;
    for (int k = 1; k < net::kMaxPeers && id < 0; k++)
        if (!players[k].active) id = k;
    if (playerCount() >= maxPlayers) id = -1;
    if (id < 0) {
        Writer w;
        w.str("session full");
        sendTo(c, frame(MSG_REJECT, w));
        c.kill = true, c.killT = clockNow();
        return;
    }
    // (One name per player: a second DRIVER is DRIVER2.)
    const std::string base = name.empty() ? "PLAYER" : name;
    std::string unique = base;
    for (int n = 2;; n++) {
        bool taken = false;
        for (const NetPlayer& p : players) taken |= p.active && p.name == unique;
        if (!taken) break;
        const std::string tail = std::to_string(n);
        unique = base.substr(0, net::kMaxNameLen - tail.size()) + tail;
    }
    c.hello = true;
    c.peer = id;
    randomBytes((uint8_t*)&c.token, sizeof c.token);
    c.token |= 1;
    players[id].active = true;
    players[id].name = unique;
    if (lastSim) welcome(*lastSim, c);
    Writer w;
    w.put<uint8_t>((uint8_t)id);
    w.str(unique);
    broadcast(frame(MSG_PLAYER, w), &c);
    events.push_back(unique + " JOINED");
    if (!build.empty() && !c.build.empty() && c.build != build) {  // (the same protocol, but maybe not the same trucks)
        events.push_back(unique + " RUNS BUILD " + c.build + ", THIS GAME " + build + ": HANDLING MAY DIFFER");
        note(unique + " IS ON BUILD " + c.build + " (THIS IS " + build + ")");
    }
}

void NetSession::handle(Sim* sim, Conn* from, const std::vector<uint8_t>& msg) {
    if (msg.empty() || role == OFF) return;
    Reader r{msg.data() + 1, msg.size() - 1};
    const uint8_t type = msg[0];
    if (events.size() > 64) events.erase(events.begin(), events.begin() + (events.size() - 64));
    if (gameIn.size() > 256) gameIn.erase(gameIn.begin(), gameIn.begin() + (gameIn.size() - 256));

    // ---- Host: a guest's word. Before the hello (and the password) it may say only that.
    if (role == HOST) {
        if (!from || from->kill) return;
        const double now = clockNow();
        if (type == MSG_HELLO) {
            if (from->hello || from->challenged) return violation(from, "a second hello");
            const uint16_t ver = r.get<uint16_t>();
            const std::string name = net::sanitize(r.str(), net::kMaxNameLen);
            if (r.ok && r.at < r.n) from->build = net::sanitize(r.str(), 16);  // (8: the build it is, if it says)
            if (!r.ok) return violation(from, "a bad hello");
            if (ver != net::kVersion) {  // (an older game can't follow this one's clock: say so plainly, not a session that drifts)
                Writer w;
                w.str(ver < net::kVersion ? std::string("version mismatch: update to ") + net::kRelease + " or newer"
                                          : "version mismatch: this server is older than your game");
                sendTo(*from, frame(MSG_REJECT, w));
                from->kill = true, from->killT = now;
                note("TURNED AWAY A GUEST ON PROTOCOL " + std::to_string(ver) + " (THIS IS " + std::to_string(net::kVersion) + ")");
                return;
            }
            if (!password.empty()) {  // prove it: HMAC of a challenge only we know
                from->challenged = true;
                from->name = name;
                randomBytes(from->nonce, sizeof from->nonce);
                Writer w;
                w.b.insert(w.b.end(), from->nonce, from->nonce + sizeof from->nonce);
                sendTo(*from, frame(MSG_CHALLENGE, w));
                return;
            }
            return admit(*from, name);
        }
        if (type == MSG_AUTH) {
            if (!from->challenged || from->hello) return violation(from, "an unasked password");
            std::vector<uint8_t> proof(32);
            for (uint8_t& b : proof) b = r.get<uint8_t>();
            if (!r.ok) return violation(from, "a bad password message");
            const std::vector<uint8_t> want = net::hmacSha256(password, std::vector<uint8_t>(from->nonce, from->nonce + sizeof from->nonce));
            if (!sameBytes(proof.data(), want.data(), 32)) {
                Strike& st = strikes[from->ip];
                if (strikes.size() > 1024) strikes.clear();  // (a spray of addresses: forget them all rather than grow)
                if (++st.fails >= 5) st.until = now + 60, st.fails = 0;  // five wrong in a row: a minute away
                Writer w;
                w.str("wrong password");
                sendTo(*from, frame(MSG_REJECT, w));
                from->kill = true, from->killT = now;
                note("WRONG PASSWORD FROM " + std::to_string(from->ip >> 24) + "." + std::to_string((from->ip >> 16) & 255) + "." +
                     std::to_string((from->ip >> 8) & 255) + "." + std::to_string(from->ip & 255));
                refused++;
                return;
            }
            from->challenged = false;
            return admit(*from, from->name);
        }
        if (!from->hello) return violation(from, "talking before saying hello");
        switch (type) {
            case MSG_PING: {  // (a guest setting its clock by ours)
                Writer w;
                w.put<double>(r.get<double>());
                w.put<double>(sharedTime(curNow));
                sendTo(*from, frame(MSG_PONG, w));
                return;
            }
            case MSG_PONG: {
                double rtt = now - r.get<double>();
                if (!r.ok) return;
                if (!std::isfinite(rtt)) rtt = 0;
                rtt = clampd(rtt, 0, 10);
                players[from->peer].ping = rtt;
                players[from->peer].link = !from->tun ? LINK_ADDRESS : from->tun->relayed ? LINK_RELAYED : LINK_DIRECT;
                Writer w;
                w.put<uint8_t>((uint8_t)from->peer);
                w.put<float>((float)rtt);
                w.put<uint8_t>((uint8_t)players[from->peer].link);
                broadcast(frame(MSG_RTT, w));
                return;
            }
            case MSG_GAME: {
                r.get<uint8_t>();  // (who it says it is from: it's from the guest)
                const std::string text = net::sanitize(r.str(), 255);
                double said = r.get<double>();
                if (!r.ok) return violation(from, "a bad chatter message");
                if (!from->games.take(now, 1, 30, 60)) return;
                if (hostOnlyChatter(text)) return note(players[from->peer].name + " SENT A HOST'S MESSAGE");
                if (text.rfind("ride ", 0) == 0) ridingOf[from->peer] = (uint32_t)std::strtoul(text.c_str() + 5, nullptr, 10);
                else if (text.rfind("walk ", 0) == 0 || text == "inside") ridingOf[from->peer] = 0;
                // (When it was said, by the guest's idea of our clock: no later than now, no more than a second ago.)
                const double shared = sharedTime(curNow);
                said = std::isfinite(said) ? clampd(said, shared - 1.0, shared) : shared;
                Writer w;
                w.put<uint8_t>((uint8_t)from->peer);
                w.str(text);
                w.put<double>(said);
                const auto m = frame(MSG_GAME, w);
                for (Conn* c : conns)
                    if (c != from && c->hello && !c->kill && worthTelling(*c, from->peer, text)) sendTo(*c, m);
                hold(sim, heldGame, said, {}, from->peer, text);
                return;
            }
            case MSG_SPAWN:
            case MSG_DESPAWN:
            case MSG_ROPE:
            case MSG_UNROPE:
            case MSG_LAMPS: {
                // (Building a vehicle or a line costs the most to everyone: those are rationed.)
                if (type == MSG_SPAWN && !from->spawns.take(now, 1, 3, 12)) return;
                if ((type == MSG_ROPE || type == MSG_UNROPE) && !from->ropes.take(now, 1, 6, 12)) return;
                if (sim && adopted && apply(*sim, msg, from->peer)) {
                    std::vector<uint8_t> framed = {(uint8_t)(msg.size() & 255), (uint8_t)(msg.size() >> 8)};  // (as the others read it: length first)
                    framed.insert(framed.end(), msg.begin(), msg.end());
                    broadcast(framed, from);
                }
                return;
            }
            case MSG_TAKE: {  // a guest asks for someone else's vehicle
                const uint32_t oldId = r.get<uint32_t>(), newId = r.get<uint32_t>();
                if (!r.ok) return violation(from, "a bad take");
                if (!from->spawns.take(now, 1, 3, 12)) return refuseTake(from->peer, oldId, newId, "TOO MANY AT ONCE");
                if (sim && adopted) hostTake(*sim, from, oldId, newId);
                else refuseTake(from->peer, oldId, newId, "NOT YET");
                return;
            }
            case MSG_GIVE:
            case MSG_REFUSE: {  // an owner's answer to one we asked of them
                const uint32_t oldId = r.get<uint32_t>(), newId = r.get<uint32_t>();
                const std::string why = type == MSG_REFUSE ? net::sanitize(r.str(), 60) : std::string();
                if (!r.ok) return violation(from, "a bad answer to a take");
                auto it = std::find_if(takes.begin(), takes.end(), [&](const PendingTake& t) { return t.oldId == oldId && t.newId == newId; });
                if (it == takes.end() || ownerOf(oldId) != from->peer) return;  // (too late: it timed out; or never asked)
                const PendingTake pt = *it;
                takes.erase(it);
                if (type == MSG_REFUSE) return refuseTake(pt.taker, oldId, newId, why.empty() ? "NO" : why);
                if (!sim || !adopted) return;
                rekey(*sim, oldId, newId, pt.taker == self && askId == oldId);
                if (pt.taker == self && askId == oldId) askId = 0;
                hostGiven(*sim, oldId, newId, from);
                if (pt.takerGone) {  // (they left while it was on its way to them: it stays, mine)
                    const int i = findCar(*sim, newId);
                    if (i >= 0) hostReclaim(*sim, i);
                }
                return;
            }
            default:  // the host's own words, from a guest, or nothing we know
                return violation(from, "message type " + std::to_string(type));
        }
    }

    // ---- Guest: the host's word.
    switch (type) {
        case MSG_CHALLENGE: {
            if (role != CLIENT || self >= 0 || !from) return;
            uint8_t nonce[16];
            for (uint8_t& b : nonce) b = r.get<uint8_t>();
            if (!r.ok) return;
            if (password.empty()) {
                error = "this game needs a password";
                passwordIssue = PW_NEEDED;
                role = OFF;
                return;
            }
            Writer w;
            const std::vector<uint8_t> proof = net::hmacSha256(password, std::vector<uint8_t>(nonce, nonce + sizeof nonce));
            w.b.insert(w.b.end(), proof.begin(), proof.end());
            sendTo(*from, frame(MSG_AUTH, w));
            return;
        }
        case MSG_WELCOME: {
            if (role != CLIENT || self >= 0) return;
            const int id = r.get<uint8_t>(), m = r.get<uint8_t>();
            const uint64_t token = r.get<uint64_t>();
            const std::string hostBuild = r.ok && r.at < r.n ? net::sanitize(r.str(), 16) : std::string();
            if (!r.ok || id < 1 || id >= net::kMaxPeers || m >= MAP_COUNT) {
                error = "the host's welcome made no sense";
                role = OFF;
                return;
            }
            if (m == MAP_PROCEDURAL) {  // the host's endless world: its generator, seed and window
                proc.gen = r.get<uint16_t>(), proc.seed = r.get<uint64_t>(), proc.ox = r.get<int64_t>(), proc.oz = r.get<int64_t>();
                proc.sliding = r.get<uint8_t>() != 0, proc.tx = r.get<int64_t>(), proc.tz = r.get<int64_t>(), proc.due = r.get<double>();
                proc.on = r.ok;
                proc.sliding = proc.sliding && proc.on;
                if (!r.ok || proc.gen != (uint16_t)proc::kGenVersion) {
                    error = r.ok ? "the host's world generator is v" + std::to_string(proc.gen) + ", this game's v" + std::to_string(proc::kGenVersion) + ": update to the same build"
                                 : "the host's world made no sense";
                    role = OFF;
                    return;
                }
            }
            // (A real terrain (MAP_REAL, realterrain.h): the host says which file; this side needs the same one in its maps/.)
            if (m == MAP_REAL) {
                const uint64_t realHash = r.ok && r.at + 8 <= r.n ? r.get<uint64_t>() : 0;
                const std::string realName = r.ok && r.at < r.n ? net::sanitize(r.str(), 24) : std::string();
                if (!realHash || !realterrain::selectByHash(realHash, realName)) {
                    char hx[24];
                    std::snprintf(hx, sizeof hx, "%016llx", (unsigned long long)realHash);
                    error = "the host plays real terrain " + (realName.empty() ? std::string("?") : realName) + " (" + hx +
                            "): put the same DEM and terrain.txt in maps/" + realName + "/";
                    role = OFF;
                    return;
                }
            }
            self = id;
            map = m;
            udpToken = token;
            if (!build.empty() && !hostBuild.empty() && hostBuild != build) events.push_back("THE HOST RUNS BUILD " + hostBuild + ", YOURS " + build + ": HANDLING MAY DIFFER");
            return;
        }
        case MSG_REJECT: {
            if (role != CLIENT) return;
            const std::string why = net::sanitize(r.str(), 60);
            if (self >= 0) {  // (in the game: kicked, or the host is closing it)
                std::string up = why;
                for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
                lostWhy = up.empty() ? "THE HOST CLOSED THE GAME" : up;
                if (from && from->fd >= 0) closeSock(from->fd), from->fd = -1;
                if (from && from->tun) from->tun->closed = true;
                return;
            }
            error = "rejected: " + why;
            if (why == "wrong password") passwordIssue = PW_WRONG;
            role = OFF;
            return;
        }
        case MSG_PING: {
            if (!from) return;
            Writer w;
            w.put<double>(r.get<double>());
            sendTo(*from, frame(MSG_PONG, w));
            return;
        }
        case MSG_SECTIONS: {
            if (role != CLIENT) return;
            const int index = r.get<uint8_t>(), count = r.get<uint8_t>(), type = r.get<uint8_t>();
            const int part = r.get<uint16_t>(), parts = r.get<uint16_t>();
            if (!r.ok || count < 1 || count > 16 || index >= count || type >= 64 || parts < 1 || parts > 200 || part >= parts) return;
            if (index == 0 && part == 0) sectionsIn.clear(), sectionsNext = 0, sectionsPending = true;
            if (sectionsNext != (index << 16 | part)) {  // (out of order: lost the thread, wait for the next whole set)
                sectionsNext = -1, sectionsPending = false, sectionsIn.clear();
                return;
            }
            if (part == 0) sectionsIn.push_back({type, std::string()});
            size_t total = 0;
            for (const auto& x : sectionsIn) total += x.second.size();
            if (total + (r.n - r.at) > net::kMaxLevelBytes) {
                sectionsNext = -1, sectionsPending = false, sectionsIn.clear();
                return;
            }
            sectionsIn.back().second.append((const char*)r.p + r.at, r.n - r.at);
            sectionsNext = part + 1 < parts ? (index << 16 | (part + 1)) : ((index + 1) << 16);
            if (part + 1 == parts && index + 1 == count) {
                sections = sectionsIn;
                sectionsIn.clear();
                sectionsPending = false;
            }
            return;
        }
        case MSG_LEVEL: {
            if (role != CLIENT) return;
            const uint32_t serial = r.get<uint32_t>();
            const uint16_t part = r.get<uint16_t>(), parts = r.get<uint16_t>();
            if (!r.ok || parts == 0 || parts > 200 || part >= parts) return;
            if (part == 0) levelIn.clear(), levelInSerial = serial, levelInNext = 0;
            if (serial != levelInSerial || part != levelInNext) {  // lost the thread: wait for a part 0
                levelIn.clear();
                levelInNext = 0xffff;
                return;
            }
            levelInNext = part + 1;
            if (r.at < r.n) levelIn.append((const char*)r.p + r.at, r.n - r.at);
            if (levelIn.size() > net::kMaxLevelBytes) {
                levelIn.clear();
                levelInNext = 0xffff;
                return;
            }
            if (part + 1 == parts) {
                levelText = levelIn;
                levelSerial = serial;
                levelFresh = true;
                levelIn.clear();
            }
            return;
        }
        case MSG_GAME: {
            const int who = r.get<uint8_t>();
            const std::string text = net::sanitize(r.str(), 255);
            const double said = r.get<double>();
            if (!r.ok || who >= net::kMaxPeers || who == self || (who != 0 && hostOnlyChatter(text))) return;
            hold(sim, heldGame, said, {}, who, text);
            return;
        }
        case MSG_CLOCK: {
            if (role != CLIENT) return;
            const double stamp = r.get<double>(), hours = r.get<float>(), rate = r.get<float>();
            if (!r.ok || r.at != r.n || !okNum(stamp, 1e9) || !okNum(hours, 1e6) || !okNum(rate, 1e3)) return;
            dayStamp = stamp;
            dayBase = std::fmod(std::max(0.0, hours), 24.0);
            dayRate = rate;
            runDay();
            dayFresh = true;
            return;
        }
        case MSG_PONG: {
            const double sent = r.get<double>(), hostT = r.get<double>();
            if (!r.ok) return;
            const double rtt = curNow - sent;
            if (!clock.add(sent, hostT, curNow)) return;
            if (self >= 0 && self < net::kMaxPeers) players[self].ping = clampd(rtt, 0, 10);
            return;
        }
        case MSG_HELLO:  // (what only a host hears)
        case MSG_AUTH:
            return;
        case MSG_TAKE: {  // someone asks the host for one of mine
            const uint32_t oldId = r.get<uint32_t>(), newId = r.get<uint32_t>();
            if (!r.ok || role != CLIENT || conns.empty()) return;
            const int i = sim && adopted ? findCar(*sim, oldId) : -1;
            std::string why = i < 0 || ownerOf(oldId) != self ? "IT'S GONE" : whyNot(*sim, i);
            if (why.empty() && (ownerOf(newId) == self || ownerOf(newId) >= net::kMaxPeers)) why = "NO";
            if (why.empty()) giveAway(*sim, i, newId);
            sendTo(*conns[0], takeMsg(why.empty() ? MSG_GIVE : MSG_REFUSE, oldId, newId, why));
            return;
        }
        case MSG_REFUSE: {
            const uint32_t oldId = r.get<uint32_t>();
            r.get<uint32_t>();
            const std::string why = net::sanitize(r.str(), 60);
            if (!r.ok || role != CLIENT || !askId || oldId != askId) return;
            CarEvent e{CarEvent::REFUSED};
            e.oldId = oldId, e.newId = askNew, e.why = why;
            carEvents.push_back(e);
            askId = 0;
            return;
        }
        default:
            break;
    }
    // Everything else concerns the world: applied once the sim has joined the session; a line
    // hooked or let go when the others are shown it happening (and whatever came after it, in turn).
    if (sim && adopted) {
        double due = -1e18;
        if ((type == MSG_ROPE || type == MSG_UNROPE) && msg.size() >= 9) std::memcpy(&due, msg.data() + msg.size() - 8, 8);
        hold(sim, heldWorld, due, msg, -1, "");
    } else if (role == CLIENT && pending.size() < kMaxPending && pendingBytes + msg.size() <= kMaxPendingBytes) {
        pending.push_back(msg);
        pendingBytes += msg.size();
    }
}

void NetSession::sendSections(Conn& to) {
    const size_t count = std::min<size_t>(sections.size(), 16);
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += sections[i].second.size();
    if (!count || total > net::kMaxLevelBytes) return;  // (more than a level's worth: they build the generator's)
    constexpr size_t kPart = 60000;
    for (size_t i = 0; i < count; i++) {
        const std::string& t = sections[i].second;
        const size_t parts = std::max<size_t>(1, (t.size() + kPart - 1) / kPart);
        for (size_t p = 0; p < parts; p++) {
            Writer w;
            w.put<uint8_t>((uint8_t)i), w.put<uint8_t>((uint8_t)count), w.put<uint8_t>((uint8_t)sections[i].first);
            w.put<uint16_t>((uint16_t)p), w.put<uint16_t>((uint16_t)parts);
            const size_t a = p * kPart, b = std::min(t.size(), a + kPart);
            w.b.insert(w.b.end(), t.begin() + (long)a, t.begin() + (long)b);
            sendTo(to, frame(MSG_SECTIONS, w));
        }
    }
}

void NetSession::welcome(Sim& sim, Conn& c) {
    Writer w;
    w.put<uint8_t>((uint8_t)c.peer);
    w.put<uint8_t>((uint8_t)map);
    w.put<uint64_t>(c.token);
    w.str(build);
    if (map == MAP_PROCEDURAL) {  // (procworld.h; and a slide on its way, which the guest must make too)
        w.put<uint16_t>(proc.gen), w.put<uint64_t>(proc.seed), w.put<int64_t>(proc.ox), w.put<int64_t>(proc.oz);
        w.put<uint8_t>(shiftOn ? 1 : 0), w.put<int64_t>(proc.ox - (int64_t)std::llround(pendingShift.d.x)), w.put<int64_t>(proc.oz - (int64_t)std::llround(pendingShift.d.z));
        w.put<double>(pendingShift.t);
    }
    if (map == MAP_REAL) w.put<uint64_t>(realterrain::activeHash()), w.str(realterrain::activeName());  // (which real terrain: a guest needs the same file)
    sendTo(c, frame(MSG_WELCOME, w));
    for (int k = 0; k < net::kMaxPeers; k++)
        if (players[k].active && k != c.peer) {
            Writer p;
            p.put<uint8_t>((uint8_t)k);
            p.str(players[k].name);
            sendTo(c, frame(MSG_PLAYER, p));
        }
    sendSections(c);
    if (!levelText.empty()) sendLevel(&c);
    if (dayHours >= 0) sendClock(&c);
    for (const Car& car : sim.cars)
        if (car.netId) sendTo(c, spawnMsg(car));
    for (const Rope& rp : sim.ropes)
        if (rp.netId) sendTo(c, ropeMsg(sim, rp));
}

std::vector<uint8_t> NetSession::spawnMsg(const Car& c) const {
    const SpecId id = specId(c.spec);
    const Frame fr = c.frame();
    Writer w;
    w.put<uint32_t>(c.netId);
    w.put<uint8_t>(id.kind);
    w.put<uint8_t>(id.model);
    for (int k = 0; k < MOD_SLOTS; k++) w.put<uint8_t>(id.mods[k]);
    w.v3(c.color);
    const double yaw = std::atan2(-fr.fwd.z, fr.fwd.x);
    const Quat rot = c.chassis.nodes.empty() ? Quat::axisAngle(V3(0, 1, 0), yaw) : c.chassis.rot;
    // Car::build expects a vehicle origin, not its center of mass. Sending the COM
    // made late-join cars float high, then drop when the first snapshot arrived.
    w.v3(fr.com - rot.rotate(restCom(c)));
    w.put<float>((float)yaw);
    w.put<uint8_t>(c.lampMode);
    w.put<uint32_t>((uint32_t)c.buildCount);
    w.paint(c.paint);  // (after everything an older peer reads: it ignores the rest and shows the factory colour)
    return frame(MSG_SPAWN, w);
}

std::vector<uint8_t> NetSession::lampMsg(const Car& c) const {
    Writer w;
    w.put<uint32_t>(c.netId);
    w.put<uint8_t>(c.lampMode);
    w.paint(c.paint);  // (a repaint rides the lamps' message, which older peers read as far as the mode)
    return frame(MSG_LAMPS, w);
}

std::vector<uint8_t> NetSession::ropeMsg(const Sim& sim, const Rope& r) const {
    Writer w;
    w.put<uint32_t>(r.netId);
    w.put<uint8_t>((uint8_t)r.kind);
    for (const RopeEnd* e : {&r.a, &r.b}) {
        w.put<uint8_t>((uint8_t)e->kind);
        w.put<uint32_t>(e->kind == ANCHOR_CAR ? sim.cars[e->car].netId : 0);
        w.put<uint8_t>((uint8_t)e->hook);
        w.v3(e->p);
    }
    for (double v : {r.length, r.maxLen, r.k, r.c, r.breakN, r.stallN}) w.put<float>((float)v);
    w.put<double>(sharedTime(curNow));  // (when it was hooked)
    return frame(MSG_ROPE, w);
}

bool NetSession::apply(Sim& sim, const std::vector<uint8_t>& msg, int from) {
    if (msg.empty()) return false;
    Reader r{msg.data() + 1, msg.size() - 1};
    const uint8_t type = msg[0];
    if (from >= 0 && type != MSG_SPAWN && type != MSG_DESPAWN && type != MSG_ROPE && type != MSG_UNROPE && type != MSG_LAMPS) return false;  // (a guest has no other say)
    // A vehicle or line must belong to some player but me; from a guest, to that guest.
    auto ownerOk = [&](uint32_t netId) {
        const uint32_t o = ownerOf(netId);
        return netId && o < (uint32_t)net::kMaxPeers && (int)o != self && (from < 0 || (int)o == from);
    };
    switch (type) {
        case MSG_PLAYER: {
            const int id = r.get<uint8_t>();
            const std::string name = net::sanitize(r.str(), net::kMaxNameLen);
            if (!r.ok || id < 0 || id >= net::kMaxPeers || id == self) return false;
            if (!players[id].active) events.push_back(name + " IS HERE");
            players[id].active = true;
            players[id].name = name;
            return true;
        }
        case MSG_LEFT: {
            const int id = r.get<uint8_t>();
            if (!r.ok || id < 0 || id >= net::kMaxPeers || id == self) return false;
            events.push_back(players[id].name + " LEFT");
            players[id] = NetPlayer();
            removeCars(sim, id);
            return true;
        }
        case MSG_RTT: {
            const int id = r.get<uint8_t>();
            const float p = r.get<float>();
            if (!r.ok || id < 0 || id >= net::kMaxPeers || id == self || !std::isfinite(p)) return false;
            players[id].ping = clampd(p, 0, 10);
            if (r.at < r.n) {
                const int link = r.get<uint8_t>();
                players[id].link = link >= 0 && link <= LINK_RELAYED ? link : 0;
            }
            return true;
        }
        case MSG_SPAWN: {
            const uint32_t netId = r.get<uint32_t>();
            SpecId id;
            id.kind = r.get<uint8_t>();
            id.model = r.get<uint8_t>();
            for (int k = 0; k < MOD_SLOTS; k++) id.mods[k] = r.get<uint8_t>();
            V3 color = r.v3();
            const V3 pos = r.v3();
            const double yaw = r.get<float>();
            const uint8_t lamps = r.get<uint8_t>();
            const uint32_t build = r.get<uint32_t>();
            CarPaint paint;
            r.paint(paint);
            if (!r.ok || !ownerOk(netId) || !okPos(pos) || !okNum(yaw, 1e4) || !okNum(color.x, 1e3) || !okNum(color.y, 1e3) || !okNum(color.z, 1e3))
                return false;
            const VehicleSpec* spec = specFrom(id);
            if (!spec) return false;
            color = V3(clampd(color.x, 0, 1), clampd(color.y, 0, 1), clampd(color.z, 0, 1));
            int i = findCar(sim, netId);
            if (i >= 0 && !sim.cars[i].remote) return false;  // (never my own vehicles)
            if (i < 0) {
                if ((int)sim.cars.size() >= Sim::kMaxBodies) return false;
                int theirs = 0;
                for (const Car& c : sim.cars) theirs += c.remote && ownerOf(c.netId) == ownerOf(netId);
                if (ownerOf(netId) != 0 && theirs >= net::kMaxCarsPerPlayer) return false;  // (the host's own are the world's)
                sim.addCar(*spec, pos, yaw, color);
                i = (int)sim.cars.size() - 1;
            } else {
                sim.dropRopes(i);
                sim.cars[i].paint = CarPaint();  // (the colour here is the truth: the paint comes after)
                sim.cars[i].build(*spec, pos, yaw, color);
            }
            Car& c = sim.cars[i];
            c.remote = true;
            c.netId = netId;
            c.setPaint(paint);
            c.handbrake = 0, c.parkBrake = false;
            c.lampMode = std::min<uint8_t>(lamps, 3);
            c.buildCount = (int)build;
            if (Remote* rm = remoteFor(netId)) {
                rm->buf.clear();
                rm->newestT = -1;
                rm->shown = false;
                rm->ease = V3(), rm->easeRot = Quat();
                rm->coastedTo = -1;
                for (auto& assembly : rm->assemblies) assembly.active = false;
            }
            return true;
        }
        case MSG_LAMPS: {
            const uint32_t id = r.get<uint32_t>();
            const uint8_t mode = r.get<uint8_t>();
            CarPaint paint;
            const bool hasPaint = r.paint(paint);
            const int i = findCar(sim, id);
            if (!r.ok || !ownerOk(id) || mode >= 4 || i < 0 || !sim.cars[i].remote) return false;
            sim.cars[i].lampMode = mode;
            if (hasPaint && paint != sim.cars[i].paint) sim.cars[i].setPaint(paint);
            return true;
        }
        case MSG_DESPAWN: {
            const uint32_t netId = r.get<uint32_t>();
            if (!r.ok || !ownerOk(netId)) return false;
            const int i = findCar(sim, netId);
            if (i < 0 || !sim.cars[i].remote) return false;
            eraseCar(sim, i);
            for (size_t k = 0; k < remotes.size(); k++)
                if (remotes[k]->netId == netId) {
                    delete remotes[k];
                    remotes.erase(remotes.begin() + k);
                    break;
                }
            return true;
        }
        case MSG_ROPE: {
            Rope rp;
            rp.netId = r.get<uint32_t>();
            const uint8_t kind = r.get<uint8_t>();
            bool ok = kind <= ROPE_STRAP;
            rp.kind = (RopeKind)std::min<uint8_t>(kind, ROPE_STRAP);
            for (RopeEnd* e : {&rp.a, &rp.b}) {
                const uint8_t ak = r.get<uint8_t>();
                const uint32_t car = r.get<uint32_t>();
                e->hook = r.get<uint8_t>();
                e->p = r.v3();
                ok &= ak <= ANCHOR_STAKE;
                e->kind = (AnchorKind)std::min<uint8_t>(ak, ANCHOR_STAKE);
                if (e->kind == ANCHOR_CAR) {
                    e->car = car ? findCar(sim, car) : -1;
                    ok &= e->car >= 0 && !sim.cars[e->car].isProp() && e->hook <= 1;
                } else {
                    e->hook = 0;
                    ok &= okPos(e->p);
                }
            }
            float v[6];
            for (float& x : v) x = r.get<float>();
            rp.length = v[0], rp.maxLen = v[1], rp.k = v[2], rp.c = v[3], rp.breakN = v[4], rp.stallN = v[5];
            {  // (when: handle() holds it till then; its fixed ends were where the sender's procedural window had them)
                const V3 fix = frameFix(r.get<double>());
                for (RopeEnd* e : {&rp.a, &rp.b})
                    if (e->kind != ANCHOR_CAR) e->p += fix;
            }
            // (A line's numbers all have a sane range; the winch's maths divides by some of them.)
            ok &= okNum(v[0], 200) && v[0] >= 0 && okNum(v[1], 200) && v[1] >= 0.5 && okNum(v[2], 2e6) && v[2] >= 0 && okNum(v[3], 2e5) && v[3] >= 0 &&
                  okNum(v[4], 2e6) && v[4] >= 1 && okNum(v[5], 1e6) && v[5] >= (rp.kind == ROPE_WINCH ? 1 : 0);  // (a strap has no stall)
            // The line belongs to whoever works it (end a: a vehicle that's theirs), and to nobody else.
            ok &= r.ok && ownerOk(rp.netId) && rp.a.kind == ANCHOR_CAR && rp.a.car >= 0 && ownerOf(sim.cars[rp.a.car].netId) == ownerOf(rp.netId);
            ok &= !(rp.b.kind == ANCHOR_CAR && rp.b.car == rp.a.car);
            if (!ok) return false;
            int theirs = 0;
            for (const Rope& o : sim.ropes) {
                if (o.netId == rp.netId) return false;
                theirs += o.netId && ownerOf(o.netId) == ownerOf(rp.netId);
            }
            if (theirs >= net::kMaxRopesPerPlayer || sim.ropes.size() >= 64) return false;
            sim.ropes.push_back(rp);
            knownRopes.push_back(rp.netId);
            return true;
        }
        case MSG_GIVE: {  // the host: a vehicle changed hands
            const uint32_t oldId = r.get<uint32_t>(), newId = r.get<uint32_t>();
            if (!r.ok || from >= 0 || ownerOf(newId) >= net::kMaxPeers || ownerOf(oldId) >= net::kMaxPeers) return false;
            const bool asked = askId && oldId == askId && newId == askNew;
            const bool ok = rekey(sim, oldId, newId, asked);
            if (asked) {
                askId = 0;
                if (!ok) {
                    CarEvent e{CarEvent::REFUSED};
                    e.oldId = oldId, e.newId = newId, e.why = "IT'S GONE";
                    carEvents.push_back(e);
                }
            }
            return ok;
        }
        case MSG_UNROPE: {
            const uint32_t netId = r.get<uint32_t>();
            r.get<double>();  // (when)
            if (!r.ok || !ownerOk(netId)) return false;
            knownRopes.erase(std::remove(knownRopes.begin(), knownRopes.end(), netId), knownRopes.end());
            for (size_t k = 0; k < sim.ropes.size(); k++)
                if (sim.ropes[k].netId == netId) {
                    sim.ropes.erase(sim.ropes.begin() + k);
                    return true;
                }
            return false;
        }
        default:
            return false;
    }
}

void NetSession::removeCars(Sim& sim, int owner) {
    for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
        if (sim.cars[i].remote && ownerOf(sim.cars[i].netId) == owner) eraseCar(sim, i);
    for (size_t k = 0; k < remotes.size();)
        if (ownerOf(remotes[k]->netId) == owner) {
            delete remotes[k];
            remotes.erase(remotes.begin() + k);
        } else {
            k++;
        }
}

// ---- Vehicles changing hands -----------------------------------------------------------

std::vector<uint8_t> NetSession::takeMsg(uint8_t type, uint32_t oldId, uint32_t newId, const std::string& why) {
    Writer w;
    w.put<uint32_t>(oldId);
    w.put<uint32_t>(newId);
    if (type == MSG_REFUSE) w.str(why);
    return frame((MsgType)type, w);
}

bool NetSession::requestTake(Sim& sim, int car) {
    if (role == OFF || !adopted || askId || self < 0 || car < 0 || car >= (int)sim.cars.size()) return false;
    const Car& c = sim.cars[car];
    if (!c.remote || c.isProp() || !c.netId || ownerOf(c.netId) == self) return false;
    if (role == CLIENT && conns.empty()) return false;
    uint32_t newId = 0;
    for (int k = 0; k < 0x10000 && (!(newId & 0xffff) || findCar(sim, newId) >= 0); k++) newId = ((uint32_t)self << 16) | (nextCarId++ & 0xffff);
    askId = c.netId, askNew = newId, askAt = curNow;
    if (role == HOST) hostTake(sim, nullptr, askId, newId);
    else sendTo(*conns[0], takeMsg(MSG_TAKE, askId, newId));
    return true;
}

std::string NetSession::whyNot(const Sim& sim, int car) const {
    const Car& c = sim.cars[car];
    if (c.remote || c.isProp()) return "IT'S GONE";
    if (c.occupied || (mayGive && !mayGive(sim, car))) return "SOMEONE'S IN IT";
    if (len(c.frame().vel) > 1.0) return "IT'S MOVING";
    if (c.jackPlaced) return "IT'S UP ON THE JACK";
    if (sim.handLine.car == car) return "THEY'VE GOT ITS LINE OUT";
    return "";
}

void NetSession::giveAway(Sim& sim, int car, uint32_t newId) {
    Car& c = sim.cars[car];
    const uint32_t oldId = c.netId;
    // The lines it works were mine: they go (send() withdraws them everywhere). Lines others
    // hooked to it stay.
    for (size_t k = sim.ropes.size(); k-- > 0;) {
        const Rope& rp = sim.ropes[k];
        if (rp.a.kind == ANCHOR_CAR && rp.a.car == car && (!rp.netId || ownerOf(rp.netId) == self)) sim.ropes.erase(sim.ropes.begin() + k);
    }
    c.ai = false;
    c.pilot.active = false;
    c.pilot.followCar = -1;
    c.remote = true;
    c.netId = newId;
    c.occupied = false;
    c.heldHere = 0;
    c.handBack = false;
    c.shoveV = c.shoveP = V3();
    knownCars.erase(std::remove_if(knownCars.begin(), knownCars.end(), [&](const Known& k) { return k.netId == oldId; }), knownCars.end());
    CarEvent e{CarEvent::GAVE};
    e.oldId = oldId, e.newId = newId;
    carEvents.push_back(e);
}

bool NetSession::rekey(Sim& sim, uint32_t oldId, uint32_t newId, bool asked) {
    const int i = findCar(sim, oldId);
    if (i < 0 || !sim.cars[i].remote || !newId || findCar(sim, newId) >= 0) return false;
    Car& c = sim.cars[i];
    c.netId = newId;
    size_t ri = remotes.size();
    for (size_t k = 0; k < remotes.size(); k++)
        if (remotes[k]->netId == oldId) ri = k;
    CarEvent e{CarEvent::RENAMED};
    e.oldId = oldId, e.newId = newId, e.asked = asked;
    if (ownerOf(newId) == self) {  // mine now: on my physics from the shape it's in, dents and all
        if (ri < remotes.size()) {
            delete remotes[ri];
            remotes.erase(remotes.begin() + ri);
        }
        c.remote = false;
        c.holdHere();
        c.heldHere = 0;
        c.handBack = false;
        c.occupied = false;
        c.throttle = c.brake = 0;
        c.handbrake = 1, c.parkBrake = true;  // (it was empty: it stays put)
        c.paintKept = true;  // (it keeps the colours everyone has seen on it)
        knownCars.push_back({newId, c.spec, c.buildCount, c.lampMode, c.paint});  // (everyone has it already: no spawn)
        e.kind = CarEvent::GOT;
    } else if (ri < remotes.size()) {  // someone else's: its states come from them now, told afresh
        Remote* rm = remotes[ri];
        rm->netId = newId;
        rm->got.clear();
        for (auto& a : rm->assemblies) a.active = false;
    }
    carEvents.push_back(e);
    return true;
}

void NetSession::refuseTake(int taker, uint32_t oldId, uint32_t newId, const std::string& why) {
    if (taker == self) {
        if (askId != oldId) return;
        CarEvent e{CarEvent::REFUSED};
        e.oldId = oldId, e.newId = newId, e.why = why;
        carEvents.push_back(e);
        askId = 0;
        return;
    }
    for (Conn* c : conns)
        if (c->hello && !c->kill && c->peer == taker) sendTo(*c, takeMsg(MSG_REFUSE, oldId, newId, why));
}

void NetSession::hostTake(Sim& sim, Conn* from, uint32_t oldId, uint32_t newId) {
    const int taker = from ? from->peer : self;
    const int owner = ownerOf(oldId);
    const int i = findCar(sim, oldId);
    auto no = [&](const char* why) { refuseTake(taker, oldId, newId, why); };
    if (ownerOf(newId) != taker || !(newId & 0xffff) || findCar(sim, newId) >= 0) return no("NO");
    if (i < 0 || sim.cars[i].isProp() || owner == taker || owner >= net::kMaxPeers) return no("IT'S GONE");
    for (const PendingTake& t : takes) {
        if (t.oldId == oldId) return no("SOMEONE ELSE IS GETTING IN");  // (first come: the host hears them in order)
        if (t.newId == newId) return no("NO");
    }
    if (taker != self) {  // (a guest's vehicles are rationed, taken ones too)
        int theirs = 0;
        for (const Car& c : sim.cars) theirs += c.remote && ownerOf(c.netId) == taker;
        for (const PendingTake& t : takes) theirs += t.taker == taker;
        if (theirs >= net::kMaxCarsPerPlayer) return no("YOU HAVE TOO MANY VEHICLES OUT");
    }
    if (owner == self) {  // mine: I say
        const std::string why = whyNot(sim, i);
        if (!why.empty()) return refuseTake(taker, oldId, newId, why);
        giveAway(sim, i, newId);
        hostGiven(sim, oldId, newId, nullptr);
        return;
    }
    Conn* oc = nullptr;
    for (Conn* c : conns)
        if (c->hello && !c->kill && c->peer == owner) oc = c;
    if (!oc) return no("IT'S GONE");
    sendTo(*oc, takeMsg(MSG_TAKE, oldId, newId));
    takes.push_back({oldId, newId, taker, curNow});
}

void NetSession::hostGiven(Sim& sim, uint32_t oldId, uint32_t newId, Conn* except) {
    (void)sim;
    const auto h = homeOf.find(oldId);
    const int home = h != homeOf.end() ? h->second : ownerOf(oldId);
    if (h != homeOf.end()) homeOf.erase(h);
    if (home != ownerOf(newId)) homeOf[newId] = home;
    broadcast(takeMsg(MSG_GIVE, oldId, newId), except);
}

void NetSession::hostReclaim(Sim& sim, int car) {
    if (car < 0 || car >= (int)sim.cars.size() || !sim.cars[car].remote) return;
    const uint32_t oldId = sim.cars[car].netId;
    uint32_t newId = 0;
    for (int k = 0; k < 0x10000 && (!(newId & 0xffff) || findCar(sim, newId) >= 0); k++) newId = ((uint32_t)self << 16) | (nextCarId++ & 0xffff);
    if (!rekey(sim, oldId, newId, false)) return;
    hostGiven(sim, oldId, newId, nullptr);
}

void NetSession::dropTakes(Sim* sim, int peer) {
    if (role != HOST || peer < 0 || peer >= net::kMaxPeers) return;
    ridingOf[peer] = 0;
    for (size_t k = 0; k < takes.size();) {
        if (ownerOf(takes[k].oldId) == peer) {  // (its owner left with it: it's gone)
            const PendingTake t = takes[k];
            takes.erase(takes.begin() + k);
            refuseTake(t.taker, t.oldId, t.newId, "THEY LEFT");
            continue;
        }
        if (takes[k].taker == peer) takes[k].takerGone = true;  // (if it comes, it stays: hostReclaim)
        k++;
    }
    if (!sim) return;
    // What they had of someone else's stays behind, mine now (parked); their own go with them.
    for (int i = 0; i < (int)sim->cars.size(); i++) {
        const Car& c = sim->cars[i];
        if (c.remote && ownerOf(c.netId) == peer && homeOf.count(c.netId)) hostReclaim(*sim, i);
    }
    for (auto it = homeOf.begin(); it != homeOf.end();) it = ownerOf(it->first) == peer ? homeOf.erase(it) : std::next(it);
}

void NetSession::expireTakes(double now) {
    if (askId && now - askAt > 10) {  // (no answer: the host or the owner went quiet)
        CarEvent e{CarEvent::REFUSED};
        e.oldId = askId, e.newId = askNew, e.why = "NO ANSWER";
        carEvents.push_back(e);
        askId = 0;
    }
    if (role != HOST) return;
    for (size_t k = 0; k < takes.size();) {
        if (now - takes[k].at > 8) {
            const PendingTake t = takes[k];
            takes.erase(takes.begin() + k);
            if (!t.takerGone) refuseTake(t.taker, t.oldId, t.newId, "NO ANSWER");
        } else {
            k++;
        }
    }
}

void NetSession::accept() {
    const double now = clockNow();
    for (int k = 0; k < 64; k++) {  // (a frame's worth: a flood waits its turn in the backlog)
        sockaddr_in a{};
        socklen_t al = sizeof a;
        const int fd = (int)::accept(listenFd, (sockaddr*)&a, &al);
        if (fd < 0) return;
        const uint32_t ip = ntohl(a.sin_addr.s_addr);
        auto turnAway = [&](const char* why) {
            closeSock(fd);
            refused++;
            note(std::string("TURNED AWAY ") + std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." +
                 std::to_string(ip & 255) + ": " + why);
        };
        const auto strike = strikes.find(ip);
        if (strike != strikes.end() && strike->second.until > now) {
            turnAway("locked out after wrong passwords");
            continue;
        }
        int theirs = 0;
        for (const Conn* c : conns) theirs += c->ip == ip;
        if (theirs >= net::kMaxConnsPerIp) {
            turnAway("too many connections from there");
            continue;
        }
        if ((int)conns.size() >= net::kMaxConns) {
            // Full: whoever's been sitting without a hello the longest makes room (a real player
            // says hello at once; a flood of idle connections shouldn't keep the real ones out).
            size_t oldest = conns.size();
            for (size_t i = 0; i < conns.size(); i++)
                if (!conns[i]->hello && now - conns[i]->born > 1.0 && (oldest == conns.size() || conns[i]->born < conns[oldest]->born)) oldest = i;
            if (oldest == conns.size()) {
                turnAway("full");
                continue;
            }
            refused++;
            dropConn(oldest);
        }
        nonBlocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        Conn* c = new Conn;
        c->fd = fd;
        c->ip = ip;
        c->born = c->lastRx = now;
        conns.push_back(c);
    }
}

NetSession::Conn* NetSession::fakeConn(int peer, bool hello) {
    Conn* c = new Conn;
    c->peer = peer;
    c->hello = hello;
    c->born = c->lastRx = clockNow();
    if (hello && peer >= 0 && peer < net::kMaxPeers) {
        players[peer].active = true;
        players[peer].name = "FAKE" + std::to_string(peer);
        randomBytes((uint8_t*)&c->token, sizeof c->token);
        c->token |= 1;
    }
    conns.push_back(c);
    return c;
}

void NetSession::dropConn(size_t i) {
    Conn* c = conns[i];
    conns.erase(conns.begin() + i);
    if (c->fd >= 0) closeSock(c->fd);
    if (c->tun && !c->tun->closed) {  // (the other end hears it now, not 15 s on)
        const uint8_t why = c->kill ? 1 : 0;
        tunPacket(*c->tun, 5, &why, 1);
        tunPacket(*c->tun, 5, &why, 1);
    }
    const int peer = c->peer;
    const bool wasIn = c->hello;
    delete c;
    if (role == HOST && wasIn && peer > 0) {
        events.push_back(players[peer].name + " LEFT");
        players[peer] = NetPlayer();
        dropTakes(lastSim, peer);  // (what they'd taken from others stays: before their own go)
        if (lastSim) removeCars(*lastSim, peer);
        Writer w;
        w.put<uint8_t>((uint8_t)peer);
        broadcast(frame(MSG_LEFT, w));
    }
    if (role == CLIENT) {
        events.push_back(lostWhy.empty() ? "LOST THE CONNECTION TO THE HOST" : lostWhy);
        if (lastSim) {
            for (int k = 0; k < net::kMaxPeers; k++)
                if (k != self) removeCars(*lastSim, k);
        }
        const std::vector<std::string> keep = events;
        close();
        events = keep;
    }
}

// ---- Joining the session ---------------------------------------------------------------

void NetSession::adopt(Sim& sim) {
    lastSim = &sim;
    // My vehicles get fresh ids in this session (send() hands them out): one left from an
    // earlier session would match another player's vehicle and be taken for it.
    for (Car& c : sim.cars)
        if (!c.remote) c.netId = 0;
    if (role == CLIENT) {
        // The host's parked trucks and props are the shared ones; keep only my vehicle,
        // parked beside the spawn so we don't land on each other.
        for (int i = (int)sim.cars.size() - 1; i >= 0; i--)
            if (i != sim.player) eraseCar(sim, i);
        sim.ropes.clear();
    }
    adopted = true;
    for (const auto& m : pending) apply(sim, m);
    pending.clear();
    pendingBytes = 0;
    if (role == CLIENT) {
        // Then my vehicle beside the spawn, in my slot or the first one along that's clear
        // of everything already there (the host's parked trucks, the other players): two
        // trucks built inside each other fight it out.
        Car& me = sim.cars[sim.player];
        const V3 side = V3(std::sin(sim.world.spawnYaw), 0, std::cos(sim.world.spawnYaw));
        const V3 back = V3(-std::cos(sim.world.spawnYaw), 0, std::sin(sim.world.spawnYaw));
        V3 p = sim.world.spawn + side * (6.5 * self);
        double yaw = sim.world.spawnYaw;
        for (int k = 0; k < 40; k++) {
            // (The places on the spawn pad first, from mine round; then side by side along it.)
            V3 q;
            double qyaw = yaw;
            if (!sim.world.spawnSlot((self + k) % net::kMaxPeers, q, qyaw))
                q = sim.world.spawn + side * (6.5 * (self + k % 8)) + back * (9.0 * (k / 8)), qyaw = sim.world.spawnYaw;
            bool clear = true;
            for (int i = 0; i < (int)sim.cars.size() && clear; i++) {
                if (i == sim.player) continue;
                const Car& o = sim.cars[i];
                const V3 d = o.frame().com - q;
                const double r = std::max(o.spec->length, o.spec->width) / 2 + 3.0;
                if (d.x * d.x + d.z * d.z < r * r) clear = false;
            }
            if (clear) {
                p = q, yaw = qyaw;
                break;
            }
        }
        me.respawn(V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), yaw);
        me.setDown(sim.world);
    }
}

// ---- Vehicle states --------------------------------------------------------------------

NetSession::Remote* NetSession::remoteFor(uint32_t netId) {
    for (Remote* r : remotes)
        if (r->netId == netId) return r;
    Remote* r = new Remote;
    r->netId = netId;
    remotes.push_back(r);
    return r;
}

// ---- A vehicle's state on the wire ------------------------------------------------------
// f64 shared time, u32 build, u32 the state it's told against (0: whole), then everything
// quantized: the center in 1/1024 m, the turn as its three smallest parts (the fourth follows),
// the speed in 1/32 m/s, the controls and engine in 8 or 16 bits, each wheel, and the dents:
// whole (3 x i16 mm a node) or, against a state the receiver has, a bit a node that moved
// at least net::kDentStep mm and how far, in a few bits each. Then the winch lines' lengths.
namespace {
constexpr double kPosScale = 1024, kVelScale = 128, kRotLimit = 0.70711;
void putVar(Writer& w, int v) {
    uint32_t z = ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
    while (z >= 0x80) w.put<uint8_t>((uint8_t)(z | 0x80)), z >>= 7;
    w.put<uint8_t>((uint8_t)z);
}
int getVar(Reader& r) {
    uint32_t z = 0;
    for (int sh = 0; sh < 35 && r.ok; sh += 7) {
        const uint8_t b = r.get<uint8_t>();
        z |= (uint32_t)(b & 0x7f) << sh;
        if (!(b & 0x80)) return (int)(z >> 1) ^ -(int)(z & 1);
    }
    r.ok = false;
    return 0;
}
template <class T> T quant(double v, double scale, double lo, double hi) { return (T)std::lround(clampd(v, lo, hi) * scale); }
void putRot(Writer& w, const Quat& q) {
    const double c[4] = {q.w, q.x, q.y, q.z};
    int big = 0;
    for (int i = 1; i < 4; i++)
        if (std::fabs(c[i]) > std::fabs(c[big])) big = i;
    const double sign = c[big] < 0 ? -1 : 1;
    w.put<uint8_t>((uint8_t)big);
    for (int i = 0; i < 4; i++)
        if (i != big) w.put<int16_t>(quant<int16_t>(c[i] * sign, 32767 / kRotLimit, -kRotLimit, kRotLimit));
}
Quat getRot(Reader& r) {
    const int big = r.get<uint8_t>();
    if (big > 3) r.ok = false;
    double c[4], sq = 0;
    for (int i = 0; i < 4; i++)
        if (i != big) c[i] = r.get<int16_t>() * (kRotLimit / 32767), sq += c[i] * c[i];
    c[std::min(std::max(big, 0), 3)] = std::sqrt(std::max(0.0, 1 - sq));
    Quat q{c[0], c[1], c[2], c[3]};
    q.normalize();
    return q;
}
}  // namespace

NetSession::Stream& NetSession::streamFor(std::vector<Stream>& v, uint32_t netId) {
    for (Stream& st : v)
        if (st.netId == netId) return st;
    v.push_back(Stream());
    v.back().netId = netId;
    return v.back();
}

NetSession::Snap NetSession::makeSnap(const Sim& sim, const Car& c, double now) const {
    Snap s;
    const Frame fr = c.frame();
    s.t = sharedTime(now);
    s.build = (uint32_t)c.buildCount;
    s.rot = c.chassis.nodes.empty() ? Quat::axisAngle(V3(0, 1, 0), std::atan2(-fr.fwd.z, fr.fwd.x)) : c.chassis.rot;
    s.com = fr.com, s.vel = fr.vel;
    s.steer = (float)c.steer;
    s.brake = (float)clampd(std::max(c.brake, c.handbrake * 0.6), 0, 1);
    s.damage = (float)c.damage;
    s.status = (uint8_t)(c.engineDead | (c.hydrolocked << 1) | (c.occupied << 2) | (c.engineOff << 3) | ((c.battery < 0.2) << 4));  // (bits 3, 4: ignition off, flat battery; older builds mask them)
    s.rpm = (float)c.rpm;
    s.load = (float)clampd(c.throttle * c.tcCut, 0, 1);
    s.boost = (float)c.boost;
    s.gear = (int8_t)c.gear;
    s.wheelAngle.reserve(c.wheels.size()), s.wheelOmega.reserve(c.wheels.size()), s.wheelFlags.reserve(c.wheels.size());
    for (const Wheel& wh : c.wheels) {
        s.wheelAngle.push_back((float)wh.angle);
        s.wheelOmega.push_back((float)wh.omega);
        s.wheelFlags.push_back((uint8_t)(wh.popped | (wh.detached << 1) | (wh.shaftBroken << 2) | (!wh.mounted << 3)));  // (a wheel off its hub for a repair: others see the bare hub)
    }
    const V3 rc = restCom(c);
    s.q.resize(c.nodes.size() * 3);
    for (size_t i = 0; i < c.nodes.size(); i++) {
        const V3 d = s.rot.unrotate(c.nodes[i].p - fr.com) + rc - c.restLocal[i];
        s.q[i * 3] = quant<int16_t>(d.x, 1000, -32.7, 32.7), s.q[i * 3 + 1] = quant<int16_t>(d.y, 1000, -32.7, 32.7), s.q[i * 3 + 2] = quant<int16_t>(d.z, 1000, -32.7, 32.7);
    }
    // The winch lines this vehicle works: how much line is out.
    for (const Rope& r : sim.ropes)
        if (r.netId && r.kind == ROPE_WINCH && r.a.kind == ANCHOR_CAR && &sim.cars[r.a.car] == &c && s.ropeLen.size() < 8) s.ropeLen.push_back({r.netId, (float)r.length});
    return s;
}

std::vector<uint8_t> NetSession::encodeState(const Snap& s, const Stream::Sent* base, std::vector<int16_t>& sentQ) {
    Writer w;
    w.b.reserve(128 + s.q.size());
    const size_t nn = s.q.size() / 3;
    if (base && base->q.size() != s.q.size()) base = nullptr;  // (rebuilt: a whole one)
    w.put<double>(s.t);
    w.put<uint32_t>(s.build);
    w.put<uint32_t>(base ? base->seq : 0);
    for (double v : {s.com.x, s.com.y, s.com.z}) w.put<int32_t>(quant<int32_t>(v, kPosScale, -kPosLimit, kPosLimit));
    putRot(w, s.rot);
    for (double v : {s.vel.x, s.vel.y, s.vel.z}) w.put<int16_t>(quant<int16_t>(v, kVelScale, -255, 255));
    w.put<int16_t>(quant<int16_t>(s.steer, 16000, -2, 2));
    w.put<uint8_t>(quant<uint8_t>(s.brake, 255, 0, 1));
    w.put<uint16_t>(quant<uint16_t>(s.damage, 65535, 0, 1));
    w.put<uint8_t>(s.status);
    w.put<uint16_t>(quant<uint16_t>(s.rpm, 3, 0, 20000));
    w.put<uint16_t>(quant<uint16_t>(s.load, 65535, 0, 1));
    w.put<uint16_t>(quant<uint16_t>(s.boost, 6000, 0, 10));
    w.put<int8_t>(s.gear);
    w.put<uint8_t>((uint8_t)s.wheelAngle.size());
    for (size_t k = 0; k < s.wheelAngle.size(); k++) {
        w.put<float>(s.wheelAngle[k]);
        w.put<int16_t>(quant<int16_t>(s.wheelOmega[k], 10, -3000, 3000));
        w.put<uint8_t>(s.wheelFlags[k]);
    }
    w.put<uint16_t>((uint16_t)nn);
    sentQ = s.q;
    if (base) {
        const size_t mask = w.b.size();
        w.b.resize(mask + (nn + 7) / 8, 0);
        for (size_t i = 0; i < nn; i++) {
            int d[3], most = 0;
            for (int k = 0; k < 3; k++) d[k] = s.q[i * 3 + k] - base->q[i * 3 + k], most = std::max(most, std::abs(d[k]));
            if (most < net::kDentStep) {  // (hasn't moved: the receiver keeps what it has)
                for (int k = 0; k < 3; k++) sentQ[i * 3 + k] = base->q[i * 3 + k];
                continue;
            }
            w.b[mask + i / 8] |= (uint8_t)(1 << (i % 8));
            for (int k = 0; k < 3; k++) putVar(w, d[k]);
        }
    } else {
        for (int16_t v : s.q) w.put<int16_t>(v);
    }
    w.put<uint8_t>((uint8_t)s.ropeLen.size());
    for (const auto& rl : s.ropeLen) {
        w.put<uint32_t>(rl.first);
        w.put<float>(rl.second);
    }
    return w.b;
}

std::vector<uint8_t> NetSession::snapshotMsg(const Sim& sim, const Car& c, double now) const {
    std::vector<int16_t> sentQ;
    return encodeState(makeSnap(sim, c, now), nullptr, sentQ);
}

void NetSession::sendState(Conn* to, Stream& st, const Snap& s, uint32_t netId) {
    const std::vector<uint8_t> data = nextState(st, s);
    sendDatagrams(to, netId, data);
}

std::vector<uint8_t> NetSession::nextStateOf(Stream& st, const Sim& sim, const Car& c, double now) { return nextState(st, makeSnap(sim, c, now)); }
std::vector<int16_t> NetSession::dentsOf(const Sim& sim, const Car& c) const { return makeSnap(sim, c, 0).q; }
const std::vector<int16_t>* NetSession::lastTaken(uint32_t& sequence) const {
    if (remotes.empty() || remotes[0]->got.empty()) return nullptr;
    sequence = remotes[0]->got.back().first;
    return &remotes[0]->got.back().second;
}

std::vector<uint8_t> NetSession::nextState(Stream& st, const Snap& s) {
    const Stream::Sent* base = nullptr;
    for (const Stream::Sent& x : st.sent)
        if (st.acked && x.seq == st.acked) base = &x;
    std::vector<int16_t> sentQ;
    const std::vector<uint8_t> data = encodeState(s, base, sentQ);
    if (++seq == 0) seq = 1;  // (0 means "no state")
    st.sent.push_back({seq, std::move(sentQ)});
    while (st.sent.size() > (size_t)net::kStreamKeep) st.sent.pop_front();
    bool still = false;
    for (const Stream::Sent& x : st.sent) still |= x.seq == st.acked;
    if (!still) st.acked = 0;  // (fell out of what we remember: a whole one next)
    st.lastStamp = s.t;
    return data;  // (as `seq`)
}

void NetSession::sendDatagrams(Conn* to, uint32_t netId, const std::vector<uint8_t>& data) {
    const size_t total = data.size(), frags = (total + kChunk - 1) / kChunk;
    if (frags > (size_t)kMaxFragments) return;
    const uint64_t token = to ? to->token : udpToken;
    if (!to && !hostUdp) return;
    for (size_t f = 0; f < frags; f++) {
        Writer d;
        d.b.reserve(32 + std::min(total, (f + 1) * kChunk) - f * kChunk);
        d.put<uint16_t>(kMagic);
        d.put<uint8_t>(DG_SNAP);
        d.put<uint8_t>((uint8_t)ownerOf(netId));
        d.put<uint32_t>(netId);
        d.put<uint32_t>(seq);
        d.put<uint8_t>((uint8_t)f);
        d.put<uint8_t>((uint8_t)frags);
        d.put<uint64_t>(token);
        const size_t a = f * kChunk, b = std::min(total, a + kChunk);
        d.b.insert(d.b.end(), data.begin() + a, data.begin() + b);
        sendDgram(to, d.b.data(), d.b.size());
    }
}

// Host: how often a vehicle at `at` is worth telling `to` about: every state near any of its
// vehicles, less often further off (it's a speck there), twice a second across the map.
double NetSession::relevantEvery(const Sim& sim, const Conn& to, const V3& at) const {
    double d2 = 1e18;
    for (const Car& c : sim.cars)
        if (c.remote && ownerOf(c.netId) == to.peer) {
            const V3 d = c.frame().com - at;
            d2 = std::min(d2, d.x * d.x + d.z * d.z);
        }
    if (to.peer >= 0 && to.peer < net::kMaxPeers && ridingOf[to.peer]) {  // (riding along in someone's: where they are)
        const int ri = findCar(sim, ridingOf[to.peer]);
        if (ri >= 0) {
            const V3 d = sim.cars[ri].frame().com - at;
            d2 = std::min(d2, d.x * d.x + d.z * d.z);
        }
    }
    if (d2 > 1e17) return 0;  // (nothing of theirs yet: everything)
    const double d = std::sqrt(d2);
    return d < 150 ? 0 : d < 400 ? 0.1 : d < 800 ? 0.2 : 0.5;
}

// Host: chatter only for some: fuel poured into a vehicle goes to its owner; where a player
// on foot is goes out ten times a second near them, less often far off (a dot on the map).
bool NetSession::worthTelling(Conn& to, int fromPeer, const std::string& text) {
    if (text.rfind("fuel ", 0) == 0) return ownerOf((uint32_t)std::strtoul(text.c_str() + 5, nullptr, 10)) == to.peer;
    if (text.rfind("walk ", 0) == 0 && lastSim && fromPeer >= 0 && fromPeer < net::kMaxPeers) {
        double x = 0, y = 0, z = 0;
        if (std::sscanf(text.c_str() + 5, "%lf %lf %lf", &x, &y, &z) != 3 || !okNum(x, kPosLimit) || !okNum(z, kPosLimit)) return true;
        const double every = relevantEvery(*lastSim, to, V3(x, y, z)) * 5;  // (0, 0.5, 1 or 2.5 s: a walker is forgotten after 2 s unheard)
        double& last = to.walkSent[fromPeer];
        if (last != 0 && curNow >= last && curNow - last < std::min(every, 1.5) - 0.01) return false;
        last = curNow;
    }
    return true;
}

void NetSession::relayState(Sim& sim, uint32_t netId, const Snap& s) {
    for (Conn* cn : conns)
        if (cn->udpKnown && cn->hello && !cn->kill && cn->peer != ownerOf(netId)) {
            Stream& st = streamFor(cn->streams, netId);
            if (st.lastStamp >= 0 && s.t - st.lastStamp < relevantEvery(sim, *cn, s.com) - 0.01) continue;
            sendState(cn, st, s, netId);
        }
}

void NetSession::sendSnapshot(Sim& sim, const Car& c, double now) {
    const Snap s = makeSnap(sim, c, now);
    if (role == CLIENT) {
        sendState(nullptr, streamFor(upStreams, c.netId), s, c.netId);
        return;
    }
    relayState(sim, c.netId, s);
}

// "We have these": u32 netId, u32 seq, as many as fit, to whoever sent them.
void NetSession::sendAcks() {
    auto out = [&](std::vector<std::pair<uint32_t, uint32_t>>& acks, uint64_t token, Conn* to) {
        while (!acks.empty()) {
            Writer d;
            d.put<uint16_t>(kMagic);
            d.put<uint8_t>(DG_ACK);
            d.put<uint8_t>((uint8_t)std::max(0, self));
            d.put<uint32_t>(0), d.put<uint32_t>(0), d.put<uint8_t>(0), d.put<uint8_t>(1);
            d.put<uint64_t>(token);
            const size_t n = std::min<size_t>(acks.size(), kChunk / 8);
            for (size_t k = 0; k < n; k++) d.put<uint32_t>(acks[k].first), d.put<uint32_t>(acks[k].second);
            acks.erase(acks.begin(), acks.begin() + n);
            sendDgram(to, d.b.data(), d.b.size());
        }
    };
    if (role == CLIENT) {
        if (hostUdp && udpToken) out(upAcks, udpToken, nullptr);
        upAcks.clear();
        return;
    }
    for (Conn* c : conns) {
        if (c->udpKnown && c->hello && !c->kill) out(c->acks, c->token, c);
        c->acks.clear();
    }
}

void NetSession::takeAcks(std::vector<Stream>& v, const uint8_t* p, size_t n) {
    for (size_t at = 0; at + 8 <= n; at += 8) {
        uint32_t netId, sq;
        std::memcpy(&netId, p + at, 4);
        std::memcpy(&sq, p + at + 4, 4);
        for (Stream& st : v)
            if (st.netId == netId) {
                bool known = false;
                for (const Stream::Sent& x : st.sent) known |= x.seq == sq;
                if (known && (!st.acked || (int32_t)(sq - st.acked) > 0)) st.acked = sq;  // (only one we sent, and newer)
            }
    }
}

void NetSession::readUdp(Sim& sim, double now) {
    pumpUdp(clockNow());
    std::vector<HeldDgram> held;
    held.swap(udpHeld);
    for (const HeldDgram& h : held) {
        Tunnel* via = nullptr;
        if (h.viaPair) {
            Conn* c = tunnelConn(h.viaPair);
            if (!c) continue;
            via = c->tun;
        }
        onDatagram(&sim, h.b.data(), h.b.size(), h.ip, h.port, now, via);
    }
}

// One datagram, from ip:port (host byte order). It must carry the secret of whoever it's for:
// the host told each guest theirs in the welcome, so a stranger's packet (a scan, a forged
// state, a forged address for a guest) is dropped here, unanswered.
void NetSession::onDatagram(Sim* simp, const uint8_t* buf, size_t n, uint32_t ip, uint16_t port, double now, Tunnel* via) {
    if (n < kDgramHeader || n > kDgramHeader + kChunk) return;
    Reader h{buf, n};
    if (h.get<uint16_t>() != kMagic) return;
    const uint8_t type = h.get<uint8_t>(), peer = h.get<uint8_t>();
    const uint32_t netId = h.get<uint32_t>(), sq = h.get<uint32_t>();
    const uint8_t frag = h.get<uint8_t>(), frags = h.get<uint8_t>();
    const uint64_t token = h.get<uint64_t>();
    if (type != DG_SNAP && type != DG_HELLO && type != DG_ACK) return;
    const size_t size = n - kDgramHeader;
    // A state's piece: a real size, in a real place, from the player the vehicle belongs to.
    auto piece = [&] {
        return frags >= 1 && frags <= kMaxFragments && frag < frags && size >= 1 && size <= kChunk && (frag + 1 == frags || size == kChunk) &&
               peer < net::kMaxPeers && ownerOf(netId) == peer;
    };
    if (role == HOST) {
        Conn* guest = nullptr;
        for (Conn* c : conns)
            if (c->hello && !c->kill && c->peer == peer) guest = c;
        if (!guest || peer == 0 || !sameBytes((const uint8_t*)&token, (const uint8_t*)&guest->token, sizeof token)) return;
        if (via && guest->tun != via) return;  // (relayed: only for the guest whose pair it came by)
        if (type == DG_HELLO) {  // the guest's UDP address (what a NAT made of it): say we heard, with just the header
            if (!via) guest->udpIp = ip, guest->udpPort = port;  // (a guest by code: its tunnel's path, whichever it is)
            guest->udpKnown = true;
            sendDgram(guest, buf, kDgramHeader);
            return;
        }
        const double t = clockNow();
        if (!guest->udpKnown || !guest->dgrams.take(t, 1, 300, 600) || !guest->dgramBytes.take(t, (double)n, 600000, 1200000)) return;
        if (type == DG_ACK) {
            if (size % 8 == 0) takeAcks(guest->streams, buf + kDgramHeader, size);
            return;
        }
        if (!piece() || !simp) return;
        Sim& sim = *simp;
        const int ci = findCar(sim, netId);
        if (!adopted || ci < 0 || !sim.cars[ci].remote) return;
        takeFragment(sim, netId, sq, frag, frags, buf + kDgramHeader, size, now);  // (whole, it goes on to the others it matters to: takeSnapshot)
        return;
    }
    if (role != CLIENT || token != udpToken || !udpToken) return;
    if (type == DG_HELLO) {  // the host heard us
        udpReady = true;
        return;
    }
    if (type == DG_ACK) {
        if (size % 8 == 0) takeAcks(upStreams, buf + kDgramHeader, size);
        return;
    }
    if (!piece() || peer == self) return;
    udpReady = true;
    if (!adopted || !simp) return;
    takeFragment(*simp, netId, sq, frag, frags, buf + kDgramHeader, size, now);
}

void NetSession::takeFragment(Sim& sim, uint32_t netId, uint32_t sequence, uint8_t fragment, uint8_t fragments,
                              const uint8_t* data, size_t size, double now) {
    if (!fragments || fragments > kMaxFragments || fragment >= fragments || size > kChunk || (fragment + 1 < fragments && size != kChunk)) return;
    if (findCar(sim, netId) < 0) return;
    Remote* remote = remoteFor(netId);
    Remote::Assembly* slot = nullptr;
    for (auto& assembly : remote->assemblies)
        if (assembly.active && assembly.seq == sequence) slot = &assembly;
    if (!slot) {
        for (auto& assembly : remote->assemblies)
            if (!assembly.active) { slot = &assembly; break; }
        if (!slot) slot = &*std::min_element(remote->assemblies.begin(), remote->assemblies.end(),
            [](const auto& a, const auto& b) { return (int32_t)(a.seq - b.seq) < 0; });
        slot->active = true;
        slot->seq = sequence;
        slot->data.assign((size_t)fragments * kChunk, 0);
        slot->got.assign(fragments, 0);
        slot->count = 0;
    }
    if (slot->got.size() != fragments || slot->got[fragment]) return;
    std::memcpy(slot->data.data() + (size_t)fragment * kChunk, data, size);
    slot->got[fragment] = 1;
    if (fragment + 1 == fragments) slot->data.resize((size_t)fragment * kChunk + size);
    if (++slot->count == fragments) {
        takeSnapshot(sim, netId, slot->data, now, sequence);
        slot->active = false;
    }
}

void NetSession::takeSnapshot(Sim& sim, uint32_t netId, const std::vector<uint8_t>& data, double now, uint32_t sequence) {
    // A whole state: decode it. Also exercised without sockets by the regression tests.
    const int ci = findCar(sim, netId);
    if (ci < 0 || !sim.cars[ci].remote) return;
    Remote* rm = remoteFor(netId);
    const Car& car = sim.cars[ci];
    Reader r{data.data(), data.size()};
    Snap s;
    s.t = r.get<double>();
    s.build = r.get<uint32_t>();
    if (s.build != (uint32_t)car.buildCount) return;
    const uint32_t baseSeq = r.get<uint32_t>();
    const std::vector<int16_t>* base = nullptr;
    if (baseSeq) {
        for (const auto& g : rm->got)
            if (g.first == baseSeq) base = &g.second;
        if (!base) return;  // (told against one we never had: the sender hears what we do have)
    }
    for (double* v : {&s.com.x, &s.com.y, &s.com.z}) *v = r.get<int32_t>() / kPosScale;
    s.rot = getRot(r);
    for (double* v : {&s.vel.x, &s.vel.y, &s.vel.z}) *v = r.get<int16_t>() / kVelScale;
    s.steer = r.get<int16_t>() / 16000.0f;
    s.brake = r.get<uint8_t>() / 255.0f;
    s.damage = r.get<uint16_t>() / 65535.0f;
    s.status = r.get<uint8_t>() & 31;
    s.rpm = r.get<uint16_t>() / 3.0f;
    s.load = r.get<uint16_t>() / 65535.0f;
    s.boost = r.get<uint16_t>() / 6000.0f;
    s.gear = r.get<int8_t>();
    const int nw = r.get<uint8_t>();
    if (nw != (int)car.wheels.size()) return;  // a rebuilt spec on the way
    s.wheelAngle.reserve(nw), s.wheelOmega.reserve(nw), s.wheelFlags.reserve(nw);
    for (int k = 0; k < nw; k++) {
        s.wheelAngle.push_back(r.get<float>());
        s.wheelOmega.push_back(r.get<int16_t>() / 10.0f);
        s.wheelFlags.push_back(r.get<uint8_t>());
    }
    const int nn = r.get<uint16_t>();
    if (nn != (int)car.nodes.size() || (base && base->size() != (size_t)nn * 3)) return;
    s.q.resize((size_t)nn * 3);
    if (base) {
        const size_t mask = r.at;
        r.at += (nn + 7) / 8;
        if (r.at > r.n) return;
        for (int i = 0; i < nn && r.ok; i++)
            for (int k = 0; k < 3; k++) {
                int v = (*base)[i * 3 + k];
                if (data[mask + i / 8] & (1 << (i % 8))) v += getVar(r);
                if (v < -32768 || v > 32767) return;
                s.q[i * 3 + k] = (int16_t)v;
            }
    } else {
        for (int16_t& v : s.q) v = r.get<int16_t>();
    }
    s.local.resize(nn);
    for (int k = 0; k < nn; k++) s.local[k] = V3(s.q[k * 3], s.q[k * 3 + 1], s.q[k * 3 + 2]) * 0.001;
    const int nr = r.get<uint8_t>();
    if (nr > 8) return;
    for (int k = 0; k < nr; k++) {
        const uint32_t id = r.get<uint32_t>();
        s.ropeLen.push_back({id, r.get<float>()});
    }
    if (r.at != r.n) r.ok = false;  // (nothing after it either)
    // Every number is finite and plausible (a NaN or a million kilometers would wreck the physics
    // of whoever's near it); anything else and the whole state is thrown away.
    bool sane = r.ok && okNum(s.t, 1e8) && okPos(s.com) && okNum(s.vel.x, 1000) && okNum(s.vel.y, 1000) && okNum(s.vel.z, 1000) &&
                okNum(s.rot.w, 1e3) && okNum(s.rot.x, 1e3) && okNum(s.rot.y, 1e3) && okNum(s.rot.z, 1e3) && okNum(s.steer, 100) &&
                okNum(s.damage, 100) && okNum(s.rpm, 1e5) && okNum(s.load, 100) && okNum(s.boost, 100);
    for (int k = 0; sane && k < nw; k++) sane = okNum(s.wheelAngle[k], 1e7) && okNum(s.wheelOmega[k], 1e4);
    for (const auto& rl : s.ropeLen) sane = sane && okNum(rl.second, 1e3);
    const double rotLen = std::sqrt(s.rot.w * s.rot.w + s.rot.x * s.rot.x + s.rot.y * s.rot.y + s.rot.z * s.rot.z);
    if (!sane || rotLen < 0.01 || s.t <= rm->newestT) return;
    s.rot = Quat{s.rot.w / rotLen, s.rot.x / rotLen, s.rot.y / rotLen, s.rot.z / rotLen};
    s.steer = clampd(s.steer, -2, 2);
    s.damage = clampd(s.damage, 0, 1);
    s.rpm = clampd(s.rpm, 0, 20000);
    s.load = clampd(s.load, 0, 1);
    s.boost = clampd(s.boost, 0, 10);
    s.gear = (int8_t)std::min(std::max((int)s.gear, -1), (int)car.spec->gears.size());  // (it indexes the gear table)
    for (float& o : s.wheelOmega) o = (float)clampd(o, -3000, 3000);
    // (The procedural world's window slid between when it was sent and now, here or there: into this machine's frame. Relayed as sent.)
    const V3 fix = frameFix(s.t);
    s.com += fix;
    // Stamped with the shared clock: how late it is says how far behind the others must be shown.
    const double shared = sharedTime(now);
    if (clockSynced()) {
        if (s.t > shared + 1.0) return;  // (from the future: a sender whose clock is broken)
        rm->late.add(shared - s.t, rm->newestT >= 0 ? s.t - rm->newestT : -1);
    }
    if (!rm->buf.empty()) {
        const Snap& previous = rm->buf.back();
        const double elapsed = s.t - previous.t;
        const V3 predicted = previous.com + (previous.vel + s.vel) * (elapsed * 0.5);
        if (len(s.com - predicted) > std::max(4.0, elapsed * 20.0)) {
            // Repairs/recovery/spawn changes jump immediately, without interpolating
            // a car through everything between the old and new positions.
            rm->buf.clear();
            rm->shown = false;
            rm->ease = V3(), rm->easeRot = Quat();
            rm->coastedTo = -1;
        }
    }
    rm->newestT = s.t;
    rm->newestArrival = shared;
    if (sequence) {  // (the next can be told against this one: say we have it)
        rm->got.push_back({sequence, s.q});
        while (rm->got.size() > (size_t)net::kStreamKeep + 8) rm->got.pop_front();
        if (role == CLIENT) upAcks.push_back({netId, sequence});
        else
            for (Conn* c : conns)
                if (c->peer == ownerOf(netId) && c->hello && c->acks.size() < 256) c->acks.push_back({netId, sequence});
    }
    if (role == HOST && ownerOf(netId) != self) {  // (on to the other guests, each told against what they have)
        if (fix.x != 0 || fix.z != 0) {
            Snap sent = s;
            sent.com -= fix;
            relayState(sim, netId, sent);
        } else relayState(sim, netId, s);
    }
    rm->buf.push_back(std::move(s));
    while (rm->buf.size() > 30) rm->buf.pop_front();
}

void NetSession::applyRemotes(Sim& sim, double now) {
    if (!clockSynced()) return;  // (a guest that doesn't know the host's clock yet can't place anything on it)
    // Everyone's vehicles at one moment on the shared clock: as far behind it as the latest
    // sender needs (so their trucks stay where they were relative to each other).
    const double shared = sharedTime(now);
    double need = net::kPlaybackDelay;
    bool any = false;
    for (const Remote* rm : remotes)
        if (!rm->buf.empty() && shared - rm->newestArrival < 3) need = std::max(need, rm->late.need()), any = true;
    if (!any && !view.started()) return;  // (start where the first states say, not at a guess)
    const bool had = view.started();
    const double before = view.render;
    view.step(shared, need);
    // The vehicles themselves are shown now, not then (docs/plans/net-collisions.md): each one
    // run on from its newest state on its own speed and turn, so a truck is where its owner has
    // it (to a few tens of cm) and a crash happens at the same place on both machines. (Shown
    // `kPlaybackDelay` and more behind, a truck at 65 km/h was 2.5 to 5 m back of where it
    // really was: each player hit the other's past.) Chatter, lines and the sky keep the view.
    // (Less the frame about to be run: its states are stamped after their physics, these are
    // placed before ours, which then carries them on for the frame.)
    const double frame = clampd(now - lastPlayNow, 0, 0.05);
    lastPlayNow = now;
    const double t = predict ? std::max(shared - frame, view.render) : view.render;
    const double coastMax = predict ? net::kMaxLead : net::kMaxCoast;
    const double dt = predict ? (vehicleAt >= 0 ? std::max(0.0, t - vehicleAt) : 0) : (had ? std::max(0.0, t - before) : 0);
    vehicleAt = t;
    vehicleLeadNow = 0;
    for (Remote* rm : remotes) {
        const int ci = findCar(sim, rm->netId);
        if (ci < 0 || rm->buf.empty()) continue;
        Car& c = sim.cars[ci];
        if (!c.remote) continue;
        const double frameDt = dt;
        while (rm->buf.size() > 2 && rm->buf[1].t < t - 0.5) rm->buf.pop_front();
        const V3 rc = restCom(c);
        V3 com;
        auto pose = [&](double at, std::vector<V3>& out, V3& vel, Quat& rot, size_t& idx, double& alpha) {
            const std::deque<Snap>& b = rm->buf;
            idx = 0;
            while (idx + 1 < b.size() && b[idx + 1].t <= at) idx++;
            const Snap& a = b[idx];
            const Snap& n = idx + 1 < b.size() ? b[idx + 1] : a;
            alpha = n.t > a.t ? clampd((at - a.t) / (n.t - a.t), 0, 1) : 0;
            com = a.com + (n.com - a.com) * alpha;
            vel = a.vel + (n.vel - a.vel) * alpha;
            rot = nlerp(a.rot, n.rot, alpha);
            if (&n == &a) {  // nothing newer (late, lost, or shown now): run on from it, a while
                const double ahead = clampd(at - a.t, 0, coastMax);
                com += a.vel * ahead;
                if (at > a.t + coastMax) vel = V3();
                else if (predict && idx > 0 && ahead > 0) {  // (turning: on at the rate it was turning)
                    const Snap& p = b[idx - 1];
                    const double span = a.t - p.t;
                    if (span > 1e-3 && span < 0.3) {
                        // On the arc it was on, too: its way of going turns (seen from above) at the rate it turned
                        // between the last two states. (Faster than a car can corner is a knock: straight on.)
                        const double h0 = std::atan2(p.vel.z, p.vel.x), h1 = std::atan2(a.vel.z, a.vel.x);
                        const double w = std::remainder(h1 - h0, 2 * M_PI) / span, sp = std::hypot(a.vel.x, a.vel.z);
                        if (std::hypot(p.vel.x, p.vel.z) > 2 && sp > 2 && std::fabs(w) < 1.5 && std::fabs(w) * ahead > 1e-6) {
                            const double half = w * ahead / 2, chord = std::sin(half) / half * ahead;
                            const double ch = std::cos(h1 + half), sh = std::sin(h1 + half);
                            com += V3(ch * sp * chord, 0, sh * sp * chord) - V3(a.vel.x, 0, a.vel.z) * ahead;
                            vel = V3(std::cos(h1 + 2 * half) * sp, a.vel.y, std::sin(h1 + 2 * half) * sp);
                        }
                        Quat d = a.rot * Quat{p.rot.w, -p.rot.x, -p.rot.y, -p.rot.z};
                        if (d.w < 0) d = Quat{-d.w, -d.x, -d.y, -d.z};
                        const double sn = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        const double ang = 2 * std::atan2(sn, d.w);
                        if (sn > 1e-9 && ang / span < 6) rot = Quat::axisAngle(V3(d.x, d.y, d.z) / sn, ang / span * ahead) * a.rot, rot.normalize();
                    }
                }
            }
            out.resize(c.nodes.size());
            for (size_t i = 0; i < c.nodes.size(); i++)
                out[i] = com + rot.rotate(c.restLocal[i] - rc + a.local[i] + (n.local[i] - a.local[i]) * alpha);
        };
        CZ_THREAD_CACHE(std::vector<V3>, now0);
        CZ_THREAD_CACHE(std::vector<V3>, prev);
        V3 vel;
        Quat rot;
        size_t idx;
        double alpha;
        pose(t - 0.05, prev, vel, rot, idx, alpha);
        pose(t, now0, vel, rot, idx, alpha);
        // A jump (put back on its wheels, towed home) is a teleport, not a speed.
        const bool jumped = len((now0[0] - prev[0]) / 0.05 - vel) > 25;
        const bool stopped = t >= rm->buf.back().t + coastMax;
        // States that come in after a coast put it somewhere else: ease into that over a
        // moment rather than snapping (a real jump, a recovery, still snaps: takeSnapshot).
        // (Shown now, it's always run on from the newest: each state that comes in corrects it.)
        const double newest = rm->buf.back().t;
        if (shared - rm->newestArrival < 3) vehicleLeadNow = std::max(vehicleLeadNow, clampd(t - newest, 0, coastMax));
        if (rm->shown && rm->coastedTo >= 0 && newest > rm->coastedTo && !jumped) {
            rm->ease = rm->lastShown + rm->lastVel * dt - com;
            rm->easeRot = rm->lastRot * Quat{rot.w, -rot.x, -rot.y, -rot.z};
            if (rm->easeRot.w < 0) rm->easeRot = Quat{-rm->easeRot.w, -rm->easeRot.x, -rm->easeRot.y, -rm->easeRot.z};
            if (len(rm->ease) > 3) rm->ease = V3(), rm->easeRot = Quat();
        }
        const double keep = std::exp(-dt / 0.15);
        rm->ease = jumped ? V3() : rm->ease * keep;
        rm->easeRot = jumped ? Quat() : nlerp(Quat(), rm->easeRot, keep);
        if (len(rm->ease) < 1e-4) rm->ease = V3();
        rm->coastedTo = t > newest ? newest : -1;
        rm->lastShown = com + rm->ease, rm->lastVel = vel, rm->shown = true;
        rm->lastRot = rm->easeRot * rot;
        rm->lastRot.normalize();
        const bool turned = rm->easeRot.w < 1 - 1e-12;
        // (One of ours is in a crash with it: it's on our physics till they part, then eased back.)
        const bool held = c.heldHere > 0 && !jumped;
        if (jumped) c.heldHere = 0, c.handBack = false, c.shoveV = c.shoveP = V3();
        if (turned)
            for (size_t i = 0; i < now0.size(); i++) now0[i] = com + rm->easeRot.rotate(now0[i] - com), prev[i] = com + rm->easeRot.rotate(prev[i] - com);
        // Held, it runs on our physics from where it was guessed to be, at the speed it was
        // guessed to have: off by what its driver did in the last latency (braking, it came in
        // a metre or two a second too fast, and kept that, pushed along metres from where its
        // owner had it). Once its owner's states have the crash in them too, and a moment past it
        // (both machines have it at the same moment; the knock itself stays ours alone, or the
        // damage comes out different), it's steered gently onto them: its speed toward theirs,
        // with what closes the gap in place, over 0.15 s or so.
        if (held && !rm->wasHeld) rm->heldAt = t;
        rm->wasHeld = held;
        if (held && predict && newest >= rm->heldAt + 0.15 && dt > 0) {
            V3 p, v;
            double M = 0;
            for (const Node& n : c.nodes) p += n.p * n.m, v += n.v * n.m, M += n.m;
            if (M > 0) {
                p = p / M, v = v / M;
                V3 want = vel + (com + rm->ease - p) / 0.3;
                if (len(want - v) > 8) want = v + norm(want - v) * 8;  // (a wild guess isn't followed far)
                const V3 dv = (want - v) * (1 - std::exp(-dt / 0.15));
                for (Node& n : c.nodes) n.v += dv;
            }
        }
        if (c.handBack) c.handBackTo(now0, vel);
        c.settleShove(vel, frameDt);
        for (size_t i = 0; i < c.nodes.size() && !held; i++) {
            c.nodes[i].p = now0[i] + rm->ease + c.shoveP;
            // (One node that jumped between two states, a part torn loose and flung, a dent put back by a repair,
            // is a teleport too, not a speed: as the whole body's, above.)
            const V3 nodeVel = (now0[i] - prev[i]) / 0.05;
            c.nodes[i].v = (jumped || stopped || len(nodeVel - vel) > 25 ? vel : nodeVel) + c.shoveV;
            c.nodes[i].f = V3();
        }
        if (!held && !c.chassis.nodes.empty()) c.chassis.rot = rm->lastRot;
        const Snap& s = rm->buf[std::min(idx + (alpha > 0.5), rm->buf.size() - 1)];
        const Snap& a = rm->buf[idx];
        const Snap& b = rm->buf[std::min(idx + 1, rm->buf.size() - 1)];
        auto blend = [alpha](double x, double y) { return x + (y - x) * alpha; };
        for (size_t k = 0; k < c.wheels.size() && k < s.wheelAngle.size(); k++) {
            Wheel& wh = c.wheels[k];
            if (!held) {
                wh.angle = blend(a.wheelAngle[k], b.wheelAngle[k]);
                wh.omega = blend(a.wheelOmega[k], b.wheelOmega[k]);
                if (&a == &b) wh.angle += wh.omega * clampd(t - a.t, 0, 0.25);
            }
            wh.popped = s.wheelFlags[k] & 1;
            wh.detached = (s.wheelFlags[k] >> 1) & 1;
            wh.shaftBroken = (s.wheelFlags[k] >> 2) & 1;
            wh.mounted = !((s.wheelFlags[k] >> 3) & 1);
        }
        c.steer = c.rackSteer = blend(a.steer, b.steer);
        c.brake = blend(a.brake, b.brake);
        c.damage = blend(a.damage, b.damage);
        c.rpm = blend(a.rpm, b.rpm);
        c.throttle = blend(a.load, b.load);
        c.tcCut = 1;
        c.boost = blend(a.boost, b.boost);
        c.gear = s.gear;
        c.engineDead = s.status & 1;
        c.hydrolocked = (s.status >> 1) & 1;
        c.occupied = (s.status >> 2) & 1;
        c.engineOff = (s.status >> 3) & 1;
        c.battery = (s.status >> 4) & 1 ? 0.05 : std::max(c.battery, 0.8);  // (their charge isn't sent: only whether it's flat)
        c.sleeping = false;
        c.updateBounds();
        for (const auto& rl : s.ropeLen)  // (only the winch lines this vehicle works)
            for (Rope& rp : sim.ropes)
                if (rp.netId == rl.first && rp.kind == ROPE_WINCH && rp.a.kind == ANCHOR_CAR && rp.a.car == ci) rp.length = clampd(rl.second, 0.6, rp.maxLen);
    }
}

// ---- Per frame ------------------------------------------------------------------------------

void NetSession::receive(Sim& sim, double now) {
    if (role == OFF) return;
    lastSim = &sim;
    if (sessionStart < 0) sessionStart = now;
    curNow = now;
    const double wall = clockNow();
    if (role == HOST) accept();
    pumpUdp(wall);  // (a tunnel's bytes come over UDP: in before the connections are read)
    for (size_t i = 0; i < conns.size();) {
        Conn& c = *conns[i];
        if (role == HOST && !c.kill) {  // nobody sits on a slot saying nothing
            if (!c.hello && wall - c.born > handshakeSecs) violation(&c, "no hello in time");
            else if (wall - c.lastRx > idleSecs) violation(&c, "silent");
        }
        readTcp(c);
        if (role == OFF) return;
        if (c.kill) flush(c);
        if (!live(c) || (c.kill && wall - c.killT > 0.5)) {  // (a refused guest gets half a second to read why)
            dropConn(i);
            if (role == OFF) return;
        } else {
            flush(c);
            i++;
        }
    }
    readUdp(sim, now);
    if (role == HOST) answerFinders(now);
    // Clients: say hello over UDP until the host has heard (that's how it learns our
    // address through any NAT).
    if (role == CLIENT && !udpReady && now >= nextUdpHello) {
        nextUdpHello = now + 0.25;
        Writer d;
        d.put<uint16_t>(kMagic);
        d.put<uint8_t>(DG_HELLO);
        d.put<uint8_t>((uint8_t)self);
        d.put<uint32_t>(0), d.put<uint32_t>(0), d.put<uint8_t>(0), d.put<uint8_t>(1);
        d.put<uint64_t>(udpToken);
        sendDgram(nullptr, d.b.data(), d.b.size());
    }
    if (role == HOST && now >= nextPing) {  // (the pings everyone sees on the player list)
        nextPing = now + 1.0;
        Writer w;
        w.put<double>(clockNow());
        const auto msg = frame(MSG_PING, w);
        for (Conn* c : conns)
            if (c->hello) sendTo(*c, msg);
    }
    if (role == CLIENT && now >= nextSync && !conns.empty()) {  // setting our clock by the host's: quickly at first, then once a second
        nextSync = now + (clock.samples < 8 ? 0.15 : 1.0);
        Writer w;
        w.put<double>(now);
        sendTo(*conns[0], frame(MSG_PING, w));
    }
    if (role == HOST && dayHours >= 0) {
        // The sky: once a second, and at once when it's been moved (the guests run it on in between).
        const double shared = sharedTime(now), expect = daySentHours + daySentRate * (shared - daySentAt);
        const double off = std::fabs(std::remainder(dayHours - expect, 24.0));
        if (daySentAt < 0 || shared - daySentAt >= 1.0 || off > 0.02 || dayRate != daySentRate) sendClock(nullptr);
    }
    if (role == CLIENT) runDay();
    if (adopted) applyRemotes(sim, now);
    expireTakes(now);
    release(&sim);
    sendAcks();
    serviceRendezvous(wall);
    serviceTunnels(wall);
}

void NetSession::sendClock(Conn* to) {
    Writer t;
    const double shared = sharedTime(curNow);
    t.put<double>(shared);
    t.put<float>((float)dayHours);
    t.put<float>((float)dayRate);
    const auto m = frame(MSG_CLOCK, t);
    if (to) sendTo(*to, m);
    else broadcast(m);
    if (!to) daySentAt = shared, daySentHours = dayHours, daySentRate = dayRate;
}

void NetSession::runDay() {
    if (dayBase < 0 || !clock.synced) return;
    dayHours = std::fmod(dayBase + dayRate * (sharedTime(curNow) - dayStamp), 24.0);
    if (dayHours < 0) dayHours += 24.0;
    if (!(dayHours >= 0 && dayHours < 24)) dayHours = 0;  // (-1e-17 + 24 is 24)
}

double NetSession::sharedTime(double now) const {
    if (role == CLIENT) return clock.host(now);
    return sessionStart < 0 ? now : now - sessionStart;
}

// Chatter and lines come stamped with when they happened. They're held until the others are
// shown at that moment, so they happen with the vehicles, not ahead of them. (Never later than
// now on the shared clock: then any stamp is out within the view delay.)
void NetSession::hold(Sim* sim, std::deque<Held>& q, double due, std::vector<uint8_t> msg, int player, const std::string& text) {
    if (!std::isfinite(due)) due = -1e18;
    due = std::min(due, sharedTime(curNow));
    Held h{due, std::move(msg), player, text};
    const bool world = &q == &heldWorld;
    if (!clockSynced() || !view.started() || (q.empty() && due <= view.render)) return deliver(sim, h, world);
    q.push_back(std::move(h));
    while (q.size() > 512) {  // (a flood: out with the oldest now)
        deliver(sim, q.front(), world);
        q.pop_front();
    }
}

void NetSession::deliver(Sim* sim, Held& h, bool world) {
    if (!world) gameIn.push_back({h.player, h.text, h.due});
    else if (sim) apply(*sim, h.msg, -1);
}

void NetSession::release(Sim* sim) {
    for (std::deque<Held>* q : {&heldWorld, &heldGame})
        while (!q->empty() && (q->front().due <= view.render || !view.started())) {
            Held h = std::move(q->front());
            q->pop_front();
            deliver(sim, h, q == &heldWorld);
            if (role == OFF) return;
        }
}

void NetSession::send(Sim& sim, double now) {
    if (role == OFF || !adopted) return;
    lastSim = &sim;
    // My vehicles: new ones, rebuilt ones (respawns, repairs, a different model), gone ones.
    std::vector<uint32_t> seen;
    seen.reserve(sim.cars.size());
    for (Car& c : sim.cars) {
        if (c.remote) continue;
        // (Only ever an id with my player number in it: one left from an earlier session, as
        // host or as someone else's guest, would clash with another player's and be taken
        // for theirs.)
        if (!c.netId || (c.netId >> 16) != (uint32_t)self) c.netId = ((uint32_t)self << 16) | (nextCarId++ & 0xffff);
        seen.push_back(c.netId);
        auto k = std::find_if(knownCars.begin(), knownCars.end(), [&](const Known& kc) { return kc.netId == c.netId; });
        if (k == knownCars.end()) {
            knownCars.push_back({c.netId, c.spec, c.buildCount, c.lampMode, c.paint});
            broadcast(spawnMsg(c));
        } else if (k->spec != c.spec || k->builds != c.buildCount) {
            k->spec = c.spec;
            k->builds = c.buildCount;
            k->lamps = c.lampMode;
            k->paint = c.paint;
            broadcast(spawnMsg(c));
        } else if (k->lamps != c.lampMode || k->paint != c.paint) {
            k->lamps = c.lampMode;
            k->paint = c.paint;
            broadcast(lampMsg(c));
        }
    }
    for (size_t k = 0; k < knownCars.size();) {
        if (std::find(seen.begin(), seen.end(), knownCars[k].netId) == seen.end()) {
            Writer w;
            w.put<uint32_t>(knownCars[k].netId);
            broadcast(frame(MSG_DESPAWN, w));
            knownCars.erase(knownCars.begin() + k);
        } else {
            k++;
        }
    }
    // Lines: new ones get an id and are announced; ones that are gone (unhooked, snapped,
    // dropped by a repair) are withdrawn everywhere.
    std::vector<uint32_t> ropesNow;
    ropesNow.reserve(sim.ropes.size());
    for (Rope& r : sim.ropes) {
        if (!r.netId) {
            r.netId = ((uint32_t)self << 16) | (nextRopeId++ & 0xffff);
            knownRopes.push_back(r.netId);
            broadcast(ropeMsg(sim, r));
        }
        ropesNow.push_back(r.netId);
    }
    for (size_t k = 0; k < knownRopes.size();) {
        if (std::find(ropesNow.begin(), ropesNow.end(), knownRopes[k]) == ropesNow.end()) {
            Writer w;
            w.put<uint32_t>(knownRopes[k]);
            w.put<double>(sharedTime(now));
            broadcast(frame(MSG_UNROPE, w));
            knownRopes.erase(knownRopes.begin() + k);
        } else {
            k++;
        }
    }
    // States, ~20 a second (parked ones twice a second).
    if (sessionStart < 0) sessionStart = now;
    curNow = now;
    const double interval = 1.0 / net::kSendRate;
    if (now - lastSend < interval) return;
    const bool slowTick = std::fmod(now, 0.5) < now - lastSend;
    // On the beat, not a frame late each time (20 a second at any frame rate, not 15 at 60 fps).
    lastSend = lastSend < 0 || now - lastSend > 2 * interval ? now : lastSend + interval;
    if (role == CLIENT && (!udpReady || !clock.synced)) return;  // (a state stamped by a clock we don't know yet would be wrong)
    // (Streams of vehicles that have gone go too.)
    auto prune = [&](std::vector<Stream>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Stream& st) { return findCar(sim, st.netId) < 0; }), v.end());
    };
    prune(upStreams);
    for (Conn* cn : conns) prune(cn->streams);
    for (const Car& c : sim.cars)
        if (!c.remote && (!c.sleeping || c.proxy || slowTick)) sendSnapshot(sim, c, now);
}

void NetSession::sendLevel(Conn* to) {
    const size_t chunk = 60000;
    const uint16_t parts = (uint16_t)std::max<size_t>(1, (levelText.size() + chunk - 1) / chunk);
    for (uint16_t k = 0; k < parts; k++) {
        Writer w;
        w.put<uint32_t>(levelSerial);
        w.put<uint16_t>(k);
        w.put<uint16_t>(parts);
        const size_t a = k * chunk, b = std::min(levelText.size(), a + chunk);
        if (a < b) w.b.insert(w.b.end(), levelText.begin() + a, levelText.begin() + b);
        if (to) sendTo(*to, frame(MSG_LEVEL, w));
        else broadcast(frame(MSG_LEVEL, w));
    }
}

void NetSession::setLevel(const std::string& text) {
    if (role != HOST) return;
    if (text.size() > net::kMaxLevelBytes) {
        events.push_back("THAT LEVEL IS TOO BIG TO SHARE");
        return;
    }
    levelText = text;
    levelSerial++;
    sendLevel(nullptr);
}

void NetSession::scheduleShift(double t, const V3& d) {
    pendingShift.t = t, pendingShift.d = d;
    shiftOn = true;
}

void NetSession::shiftNow() {
    if (!shiftOn) return;
    for (Remote* rm : remotes)
        for (Snap& s : rm->buf) s.com += pendingShift.d;
    pastShifts.push_back(pendingShift);
    while (pastShifts.size() > 4) pastShifts.erase(pastShifts.begin());
    shiftOn = false;
}

V3 NetSession::frameFix(double stamp) const {
    V3 fix;
    if (shiftOn && stamp >= pendingShift.t) fix -= pendingShift.d;  // (sent from the new frame; here it's the old one still)
    for (const WorldShift& s : pastShifts)
        if (stamp < s.t) fix += s.d;  // (sent from the old frame, before a slide that's happened here)
    return fix;
}

void NetSession::sendGame(const std::string& text) {
    if (role == OFF) return;
    Writer w;
    w.put<uint8_t>((uint8_t)std::max(0, self));
    w.str(text);
    w.put<double>(sharedTime(curNow));
    if (role == HOST) {
        const auto m = frame(MSG_GAME, w);
        for (Conn* c : conns)
            if (c->hello && !c->kill && worthTelling(*c, 0, text)) sendTo(*c, m);
    } else if (!conns.empty()) {
        sendTo(*conns[0], frame(MSG_GAME, w));
    }
}

// ---- Finding games on the local network -------------------------------------------------

// What a host tells anyone looking for games. The old form (0.1.4) is "CZHOST version port map
// players name"; the new one adds the most players, the session's id and its name, a line each.
std::string NetSession::hostInfo(bool v2) const {
    char head[96];
    if (!v2) {
        std::snprintf(head, sizeof head, "CZHOST %d %d %d %d ", (int)net::kVersion, hostPort, map, playerCount());
        return head + players[0].name.substr(0, 40);
    }
    std::snprintf(head, sizeof head, "CZHOST2 %d %d %d %d %d %u %d\n", (int)net::kVersion, hostPort, map, playerCount(), maxPlayers, sessionId, password.empty() ? 0 : 1);
    return head + sessionName.substr(0, 40) + "\n" + players[0].name.substr(0, 40);
}

void NetSession::answerFinders(double now) {
    if (findFd < 0) return;
    char buf[256];
    std::vector<net::LocalAddress> local;
    for (int k = 0; k < 16; k++) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const int n = (int)recvfrom(findFd, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) break;
        buf[n] = 0;
        if (std::strncmp(buf, "CZFIND", 6) != 0) continue;
        // Only the local network is answered (a forged "from" on the internet would turn this
        // into a way to send our reply, bigger than the question, at someone else), and at
        // most a few a second.
        const uint32_t ip = ntohl(from.sin_addr.s_addr);
        if (local.empty()) local = net::localAddresses();
        bool onLan = (ip >> 24) == 127;
        for (const net::LocalAddress& la : local) onLan |= (ip & la.mask) == (la.addr & la.mask);
        if (!onLan || !findRate.take(clockNow(), 1, 20, 40)) continue;
        const std::string reply = hostInfo(std::strncmp(buf, "CZFIND2", 7) == 0);
        sendto(findFd, reply.data(), (int)reply.size(), 0, (sockaddr*)&from, sizeof from);
    }
    // And say so, every couple of seconds, to everyone on each network this machine is on
    // (for when a firewall lets our announcement out but not their question in).
    if (now >= nextBeacon) {
        nextBeacon = now + 2.0;
        const std::string msg = hostInfo(true);
        std::vector<uint32_t> to = {0xffffffffu, 0x7f000001u};
        for (const net::LocalAddress& la : net::localAddresses())
            if (std::find(to.begin(), to.end(), la.broadcast) == to.end()) to.push_back(la.broadcast);
        for (uint32_t ip : to) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(ip);
            a.sin_port = htons((uint16_t)net::kBeaconPort);
            sendto(findFd, msg.data(), (int)msg.size(), 0, (sockaddr*)&a, sizeof a);
        }
    }
}

namespace net {

std::vector<LocalAddress> localAddresses() {
    std::vector<LocalAddress> out;
    auto add = [&](uint32_t ip, uint32_t mask) {
        if ((ip >> 24) == 127 || ip == 0) return;
        if ((ip >> 16) == 0xA9FE) return;  // (169.254: a link-local address, no network behind it)
        LocalAddress la;
        la.addr = ip;
        la.mask = mask;
        la.broadcast = ip | ~mask;
        char b[20];
        std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        la.ip = b;
        for (const LocalAddress& o : out)
            if (o.addr == ip) return;
        out.push_back(la);
    };
#ifdef _WIN32
    netInit();
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                    (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                  (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    }
    if (rc == NO_ERROR)
        for (IP_ADAPTER_ADDRESSES* ad = (IP_ADAPTER_ADDRESSES*)buf.data(); ad; ad = ad->Next) {
            if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
                if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET) continue;
                const uint32_t ip = ntohl(((sockaddr_in*)ua->Address.lpSockaddr)->sin_addr.s_addr);
                const int bits = std::min(std::max((int)ua->OnLinkPrefixLength, 0), 32);
                add(ip, bits == 0 ? 0u : 0xffffffffu << (32 - bits));
            }
        }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            const uint32_t ip = ntohl(((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr);
            const uint32_t mask = i->ifa_netmask ? ntohl(((sockaddr_in*)i->ifa_netmask)->sin_addr.s_addr) : 0xffffff00u;
            add(ip, mask);
        }
        freeifaddrs(list);
    }
#endif
    return out;
}

Finder::~Finder() { close(); }

void Finder::close() {
    if (fd >= 0) closeSock(fd);
    if (beaconFd >= 0) closeSock(beaconFd);
    fd = beaconFd = -1;
    hosts.clear();
}

// One answer or announcement, from `fromIp` (host byte order).
void Finder::take(const char* buf, uint32_t fromIp, double now) {
    FoundHost h;
    int ver = 0, port = 0, mp = 0, pl = 0, mx = kMaxPeers, used = 0, locked = 0;
    unsigned id = 0;
    if (std::strncmp(buf, "CZHOST2 ", 8) == 0) {
        const int got = std::sscanf(buf + 8, "%d %d %d %d %d %u %d%n", &ver, &port, &mp, &pl, &mx, &id, &locked, &used);
        if (got < 6) return;
        const char* p = std::strchr(buf + 8, '\n');
        if (p) {
            const char* q = std::strchr(p + 1, '\n');
            h.session = q ? std::string(p + 1, q) : std::string(p + 1);
            h.name = q ? std::string(q + 1) : "";
        }
        h.locked = got >= 7 && locked != 0;
    } else if (std::strncmp(buf, "CZHOST ", 7) == 0) {
        if (std::sscanf(buf + 7, "%d %d %d %d %n", &ver, &port, &mp, &pl, &used) < 4) return;
        h.name = used > 0 ? std::string(buf + 7 + used) : "";
    } else {
        return;
    }
    if (port <= 0 || port > 65535) return;
    char ip[32];
    std::snprintf(ip, sizeof ip, "%u.%u.%u.%u:%d", fromIp >> 24, (fromIp >> 16) & 255, (fromIp >> 8) & 255, fromIp & 255, port);
    h.address = ip;
    // (Whatever was on the network said it: names fit the HUD font, counts fit the game.)
    h.session = sanitize(h.session, 40), h.name = sanitize(h.name, 40);
    h.map = std::min(std::max(mp, 0), 255), h.players = std::min(std::max(pl, 0), kMaxPeers), h.maxPlayers = std::min(std::max(mx, 2), kMaxPeers);
    h.version = ver, h.id = id, h.seen = now;
    const bool loop = (fromIp >> 24) == 127;
    // The same game heard twice (over the network and on this machine, or on two adapters):
    // one row, at its network address (that's the one to give a friend).
    auto it = std::find_if(hosts.begin(), hosts.end(), [&](const FoundHost& o) { return o.address == h.address || (h.id && o.id == h.id); });
    if (it == hosts.end()) {
        if (hosts.size() < 64) hosts.push_back(h);  // (anyone can send these: a flood of made-up games is not a list)
        return;
    }
    const bool wasLoop = it->address.compare(0, 4, "127.") == 0;
    if (loop && !wasLoop) h.address = it->address;
    *it = h;
}

void Finder::poll(double now) {
    if (fd < 0) {
        netInit();
        fd = newSocket(SOCK_DGRAM);
        if (fd < 0) return;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);
        nonBlocking(fd);
        nextAsk = 0;
        // The hosts' announcements (shared: several games open on one machine all hear them).
        beaconFd = newSocket(SOCK_DGRAM);
        if (beaconFd >= 0) {
            setsockopt(beaconFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#ifdef SO_REUSEPORT
            setsockopt(beaconFd, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof one);
#endif
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = INADDR_ANY;
            a.sin_port = htons((uint16_t)kBeaconPort);
            if (bind(beaconFd, (sockaddr*)&a, sizeof a) < 0) {
                closeSock(beaconFd);
                beaconFd = -1;
            } else {
                nonBlocking(beaconFd);
            }
        }
    }
    if (now >= nextAsk) {
        nextAsk = now + 1.0;
        // The whole local network, each adapter's own subnet (a computer on wired and Wi-Fi,
        // or with a VPN, sends the first only one way), and this machine (a game hosted in
        // another window).
        std::vector<uint32_t> to = {0xffffffffu, 0x7f000001u};
        for (const LocalAddress& la : localAddresses())
            if (std::find(to.begin(), to.end(), la.broadcast) == to.end()) to.push_back(la.broadcast);
        for (uint32_t ip : to) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(ip);
            a.sin_port = htons((uint16_t)kDiscoveryPort);
            sendto(fd, "CZFIND2", 7, 0, (sockaddr*)&a, sizeof a);
        }
    }
    char buf[512];
    for (int sock : {fd, beaconFd}) {
        if (sock < 0) continue;
        for (int k = 0; k < 32; k++) {
            sockaddr_in from{};
            socklen_t fl = sizeof from;
            const int n = (int)recvfrom(sock, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
            if (n <= 0) break;
            buf[n] = 0;
            take(buf, ntohl(from.sin_addr.s_addr), now);
        }
    }
    hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [&](const FoundHost& h) { return now - h.seen > 4.0; }), hosts.end());
}

}  // namespace net

// ---- Join codes: the backend, hole punching, the relay, the tunnel ----------------------------
// docs/plans/join-codes.md. A guest that joins by code has no TCP connection: its reliable
// messages go over a ReliableStream (nettunnel.h) in "tunnel" packets on the same UDP socket and
// path as its vehicle states: straight to the host once a punch got through, else wrapped for
// the backend's relay. Above that it is an ordinary Conn: the same hello, password, secret,
// limits and checks, and the host is the session's authority as for anyone.

NetSession::Conn::~Conn() { delete tun; }

bool NetSession::live(const Conn& c) const { return c.fd >= 0 || (c.tun && !c.tun->closed && !c.tun->stream.broken); }

NetSession::Conn* NetSession::tunnelConn(uint32_t pair) {
    for (Conn* c : conns)
        if (c->tun && c->tun->pair == pair) return c;
    return nullptr;
}

NetSession::Link NetSession::myLink() const {
    if (role != CLIENT || conns.empty() || !conns[0]->tun) return LINK_ADDRESS;
    return conns[0]->tun->relayed ? LINK_RELAYED : LINK_DIRECT;
}

NetSession::JoinStep NetSession::joinStep() const {
    switch (joinStage) {
        case 10: return JS_FINDING;
        case 11: return JS_CONNECTING;
        case 2: return !conns.empty() && conns[0]->tun && conns[0]->tun->relayed ? JS_RELAY : JS_HELLO;
        case 1: return JS_HELLO;
        case 3: return JS_LOADING;
        default: return JS_NONE;
    }
}

void NetSession::sendBackend(const std::vector<uint8_t>& b) {
    if (udpFd < 0 || !rvPort) return;
    const int n = udpSend(udpFd, b.data(), b.size(), rvIp, rvPort);
    if (n > 0) bytesOut += n;
}

void NetSession::tunRaw(Tunnel& t, const uint8_t* p, size_t n) {
    if (udpFd < 0) return;
    int sent = 0;
    if (t.relayed) {
        if (!rvPort) return;
        std::vector<uint8_t> f;
        rv::relayFrame(f, t.pair, t.relayKey, p, n);
        sent = udpSend(udpFd, f.data(), f.size(), rvIp, rvPort);
    } else if (t.port) {
        sent = udpSend(udpFd, p, n, t.ip, t.port);
    }
    if (sent > 0) bytesOut += sent;
}

void NetSession::tunPacket(Tunnel& t, uint8_t type, const uint8_t* body, size_t n) {
    Writer w;
    w.b.reserve(kTunHeader + n);
    w.put<uint16_t>(kTunMagic);
    w.put<uint8_t>(type);
    w.put<uint32_t>(t.pair);
    w.put<uint64_t>(t.secret);
    if (n) w.b.insert(w.b.end(), body, body + n);
    tunRaw(t, w.b.data(), w.b.size());
}

void NetSession::punchTo(uint32_t pair, uint64_t secret, uint8_t type, uint32_t ip, uint16_t port) {
    if (udpFd < 0 || !port) return;
    Writer w;
    w.put<uint16_t>(kTunMagic);
    w.put<uint8_t>(type);
    w.put<uint32_t>(pair);
    w.put<uint64_t>(secret);
    const int n = udpSend(udpFd, w.b.data(), w.b.size(), ip, port);
    if (n > 0) bytesOut += n;
}

void NetSession::sendDgram(Conn* to, const uint8_t* p, size_t n) {
    if (udpFd < 0) return;
    Conn* path = to ? to : role == CLIENT && !conns.empty() ? conns[0] : nullptr;
    if (path && path->tun) return tunRaw(*path->tun, p, n);
    int sent = 0;
    if (to) {
        sent = udpSend(udpFd, p, n, to->udpIp, to->udpPort);
    } else if (hostUdp) {
        const sockaddr_in& a = *(const sockaddr_in*)hostUdp;
        sent = udpSend(udpFd, p, n, ntohl(a.sin_addr.s_addr), ntohs(a.sin_port));
    }
    if (sent > 0) bytesOut += sent;
}

void NetSession::pumpUdp(double now) {
    if (udpFd < 0) return;
    uint8_t buf[2048];
    for (int k = 0; k < 2048; k++) {  // (a frame's worth; the rest waits, or the kernel drops it)
        uint32_t ip = 0;
        uint16_t port = 0;
        const int n = udpRecv(udpFd, buf, sizeof buf, ip, port);
        if (n <= 0) break;
        bytesIn += n;
        onPacket(buf, (size_t)n, ip, port, now, 0);
    }
}

void NetSession::onPacket(const uint8_t* p, size_t n, uint32_t ip, uint16_t port, double now, uint32_t relayedPair) {
    if (n < 4) return;
    uint16_t magic;
    std::memcpy(&magic, p, 2);
    if (magic == kTunMagic) return onTunnel(p, n, ip, port, now, relayedPair);
    if (magic == kMagic) {  // (a game datagram: read after the connections, as before: readUdp)
        if (udpHeld.size() < 4096) udpHeld.push_back({std::vector<uint8_t>(p, p + n), ip, port, relayedPair});
        return;
    }
    // From the backend, and only from its address: its answers, and what it relays to us.
    if (magic != rv::kMagic || relayedPair || !rvPort || ip != rvIp || port != rvPort) return;
    uint32_t pair = 0;
    const uint8_t* inner = nullptr;
    size_t len = 0;
    if (rv::relayedPayload(p, n, pair, inner, len)) {
        // (Only for a pair we were introduced to: the tunnel packet inside also has its secret.)
        const bool ours = role == HOST ? tunnelConn(pair) != nullptr || std::any_of(intros.begin(), intros.end(), [&](const Intro& i) { return i.pair == pair; })
                                       : pair == codeIntro.pair && pair != 0;
        if (ours && pair) onPacket(inner, len, 0, 0, now, pair);
        return;
    }
    onBackend(p, n, now);
}

NetSession::Conn* NetSession::acceptTunnel(const Intro& in, double now) {
    const uint32_t ip = in.pubIp;
    auto turnAway = [&](const char* why) -> Conn* {
        refused++;
        note("TURNED AWAY " + net::ipText(ip) + " (BY CODE): " + why);
        return nullptr;
    };
    const auto strike = strikes.find(ip);
    if (strike != strikes.end() && strike->second.until > now) return turnAway("locked out");
    int theirs = 0;
    for (const Conn* c : conns) theirs += c->ip == ip;
    if (theirs >= net::kMaxConnsPerIp) return turnAway("too many connections from there");
    if ((int)conns.size() >= net::kMaxConns) return turnAway("full");
    Conn* c = new Conn;
    c->ip = ip;
    c->born = c->lastRx = now;
    c->tun = new Tunnel;
    c->tun->pair = in.pair, c->tun->secret = in.secret, c->tun->relayKey = in.relayKey;
    c->tun->born = now;
    c->tun->stream.lastHeard = now;
    conns.push_back(c);
    return c;
}

void NetSession::onTunnel(const uint8_t* p, size_t n, uint32_t ip, uint16_t port, double now, uint32_t relayedPair) {
    if (n < kTunHeader) return;
    Reader r{p, n};
    r.get<uint16_t>();
    const uint8_t type = r.get<uint8_t>();
    const uint32_t pair = r.get<uint32_t>();
    const uint64_t secret = r.get<uint64_t>();
    if (!pair || (relayedPair && pair != relayedPair)) return;
    const uint8_t* body = p + kTunHeader;
    const size_t bn = n - kTunHeader;
    auto secretIs = [&](uint64_t want) { return want && sameBytes((const uint8_t*)&secret, (const uint8_t*)&want, 8); };
    auto stream = [&](Tunnel& t) {
        switch (type) {
            case T_DATA:
                if (bn >= 4) {
                    uint32_t sq;
                    std::memcpy(&sq, body, 4);
                    t.stream.onData(sq, body + 4, bn - 4, now);
                }
                break;
            case T_ACK:
                if (bn >= 12) {
                    uint32_t next;
                    uint64_t bits;
                    std::memcpy(&next, body, 4);
                    std::memcpy(&bits, body + 4, 8);
                    t.stream.onAck(next, bits, now);
                }
                break;
            case T_CLOSE:
                t.closed = true;
                if (role == CLIENT && lostWhy.empty()) lostWhy = bn && body[0] == 1 ? "THE HOST REMOVED YOU FROM THE GAME" : "THE HOST CLOSED THE GAME";
                break;
            default: break;
        }
    };
    if (role == CLIENT || joinStage == 11) {  // ---- a guest: only the host it was introduced to
        if (pair != codeIntro.pair || !secretIs(codeIntro.secret)) return;
        if (type == T_PUNCH || type == T_PUNCH_ACK) {
            if (relayedPair) return;
            if (type == T_PUNCH) punchTo(pair, secret, T_PUNCH_ACK, ip, port);
            if (joinStage == 11 && !forceRelay && !directFound) directFound = true, directIp = ip, directPort = port;
            return;
        }
        Conn* c = tunnelConn(pair);
        if (!c) return;
        if (!relayedPair && (ip != c->tun->ip || port != c->tun->port)) return;  // (straight: only from where the punch found the host)
        stream(*c->tun);
        return;
    }
    if (role != HOST) return;  // ---- the host: a guest it was introduced to
    Conn* c = tunnelConn(pair);
    const Intro* in = nullptr;
    for (const Intro& x : intros)
        if (x.pair == pair) in = &x;
    if (!secretIs(c ? c->tun->secret : in ? in->secret : 0)) return;  // (a stranger's packet: dropped, unanswered)
    if (type == T_PUNCH || type == T_PUNCH_ACK) {
        if (!relayedPair && type == T_PUNCH) punchTo(pair, secret, T_PUNCH_ACK, ip, port);
        return;
    }
    if (!c) {
        if (type != T_DATA || !in) return;
        c = acceptTunnel(*in, now);
        if (!c) return;
    }
    Tunnel& t = *c->tun;
    // (Answers go back the way the guest's last word came: it picks the path, the host follows.)
    if (relayedPair) t.relayed = true;
    else t.relayed = false, t.ip = ip, t.port = port;
    stream(t);
}

void NetSession::serviceTunnels(double now) {
    for (Conn* c : conns) {
        if (!c->tun || c->tun->closed) continue;
        Tunnel& t = *c->tun;
        flush(*c);
        t.stream.service(now, [&](const net::ReliableStream::Out& o) {
            uint8_t body[12 + net::ReliableStream::kSegment];
            std::memcpy(body, &o.seq, 4);
            if (o.ack) {
                std::memcpy(body + 4, &o.bits, 8);
                tunPacket(t, T_ACK, body, 12);
            } else {
                std::memcpy(body + 4, o.p, o.n);
                tunPacket(t, T_DATA, body, 4 + o.n);
            }
        });
        if (now - t.stream.lastHeard > kTunnelDead) t.closed = true;
        // A punched path gone quiet (a NAT forgot it): the guest moves to the relay, the host follows.
        if (role == CLIENT && !t.relayed && t.relayKey && now - t.stream.lastHeard > kDirectQuiet && !forceRelay) {
            t.relayed = true;
            events.push_back("THE DIRECT LINE TO THE HOST WENT QUIET: USING THE RELAY");
        }
    }
}

bool NetSession::resolveRendezvous(std::string& why) {
    std::string host;
    int port = 0;
    if (rendezvous.empty()) {
        why = "join codes aren't set up in this game (no join-code server)";
        return false;
    }
    if (!net::parseAddress(rendezvous, host, port, &why, rv::kDefaultPort)) return false;
    uint32_t ip = 0;
    if (!resolveIPv4(host, ip)) {
        why = "can't find the join-code server (" + host + "): check your internet";
        return false;
    }
    rvIp = ip, rvPort = (uint16_t)port;
    return true;
}

void NetSession::registerCode(double now) {
    (void)now;
    rv::Msg m;
    m.type = rv::RV_REGISTER, m.version = net::kVersion, m.players = (uint8_t)playerCount(), m.maxPlayers = (uint8_t)maxPlayers;
    m.flags = password.empty() ? 0 : rv::FL_LOCKED;
    m.nonce = rvNonce;
    if (shareLocals) {
        const uint16_t mine = boundPort(udpFd);
        for (const net::LocalAddress& a : net::localAddresses())
            if ((int)m.locals.size() < rv::kMaxLocals) m.locals.push_back({a.addr, mine});
    }
    sendBackend(rv::encode(m));
}

bool NetSession::openToFriends() {
    if (role != HOST || udpFd < 0) {
        codeState = CODE_FAILED, codeError = "start a game first";
        return false;
    }
    if (codeState == CODE_OPEN || codeState == CODE_ASKING) return true;
    std::string why;
    if (!resolveRendezvous(why)) {
        codeState = CODE_FAILED, codeError = why;
        return false;
    }
    codeError.clear();
    joinCode.clear();
    rvKey = 0;
    rvNonce = net::random64();
    rvTries = 0;
    rvNext = 0;
    codeState = CODE_ASKING;
    serviceRendezvous(clockNow());
    return true;
}

void NetSession::closeToFriends() {
    if (role != HOST) return;
    if (codeState == CODE_OPEN && rvKey) {
        const bool guests = std::any_of(conns.begin(), conns.end(), [](const Conn* c) { return c->tun != nullptr; });
        if (rvOldKey) {  // (one closed code kept alive at a time: the older one goes now)
            rv::Msg m;
            m.type = rv::RV_CLOSE, m.code = codeWanted, m.key = rvOldKey;
            sendBackend(rv::encode(m));
            rvOldKey = 0;
        }
        rv::Msg m;
        m.type = rv::RV_CLOSE, m.code = joinCode, m.key = rvKey, m.flags = guests ? rv::FL_RETIRE : 0;
        sendBackend(rv::encode(m));
        if (guests) rvOldKey = rvKey, codeWanted = joinCode, rvOldNext = clockNow() + rv::kKeepEvery;
    }
    codeState = CODE_OFF;
    joinCode.clear();
    rvKey = 0;
    intros.clear();
}

void NetSession::kick(int peer) {
    if (role != HOST || peer <= 0) return;
    const double now = clockNow();
    for (Conn* c : conns)
        if (c->hello && c->peer == peer && !c->kill) {
            Writer w;
            w.str("the host removed you from the game");
            sendTo(*c, frame(MSG_REJECT, w));
            strikes[c->ip].until = now + 120;  // (not straight back in)
            note(players[peer].name + " KICKED");
            c->kill = true, c->killT = now;
        }
}

void NetSession::serviceRendezvous(double now) {
    if (role != HOST || !rvPort) return;
    if (codeState == CODE_ASKING && now >= rvNext) {
        if (rvTries >= 8) {
            codeState = CODE_FAILED;
            codeError = "can't reach the join-code server: check your internet, or have friends join by address";
            return;
        }
        rvTries++;
        rvNext = now + 1.0;
        registerCode(now);
    }
    const int as = playerCount() | maxPlayers << 8 | (password.empty() ? 0 : 1 << 16);
    if (codeState == CODE_OPEN && as != keptAs) rvNext = std::min(rvNext, std::max(now, rvLastKeep + 1.0));  // (full, or room again: the backend's answers to lookups say so)
    if (codeState == CODE_OPEN && rvKey && now >= rvNext) {
        rvNext = now + rv::kKeepEvery;
        rvLastKeep = now;
        keptAs = as;
        rv::Msg m;
        m.type = rv::RV_KEEP, m.code = joinCode, m.key = rvKey, m.players = (uint8_t)playerCount(), m.maxPlayers = (uint8_t)maxPlayers;
        m.flags = password.empty() ? 0 : rv::FL_LOCKED;
        sendBackend(rv::encode(m));
        if (now - rvHeard > 3.5 * rv::kKeepEvery) {  // (the backend forgot us, or can't be reached: a new code)
            events.push_back("THE JOIN CODE WAS LOST: GETTING A NEW ONE");
            codeState = CODE_ASKING, rvNonce = net::random64(), rvTries = 0, rvNext = now;
        }
    }
    if (rvOldKey) {  // a closed code its guests still use (their relay): alive while any is in
        const bool guests = std::any_of(conns.begin(), conns.end(), [](const Conn* c) { return c->tun != nullptr; });
        rv::Msg m;
        m.code = codeWanted, m.key = rvOldKey;
        if (!guests) {
            m.type = rv::RV_CLOSE;
            sendBackend(rv::encode(m));
            rvOldKey = 0;
        } else if (now >= rvOldNext) {
            rvOldNext = now + rv::kKeepEvery;
            m.type = rv::RV_KEEP, m.maxPlayers = (uint8_t)maxPlayers, m.players = (uint8_t)playerCount(), m.flags = rv::FL_RETIRE;
            sendBackend(rv::encode(m));
        }
    }
    // Punching towards the guests the backend introduced, until their tunnel opens.
    for (size_t i = 0; i < intros.size();) {
        Intro& in = intros[i];
        if (now - in.born > 120) {
            intros.erase(intros.begin() + (long)i);
            continue;
        }
        if (now - in.born < punchSecs + 1.0 && now >= in.nextPunch && !tunnelConn(in.pair)) {
            in.nextPunch = now + 0.1;
            for (const auto& c : in.cands) punchTo(in.pair, in.secret, T_PUNCH, c.first, c.second);
        }
        i++;
    }
}

void NetSession::onBackend(const uint8_t* p, size_t n, double now) {
    rv::Msg m;
    if (!rv::decode(p, n, m)) return;
    rvHeard = now;
    const bool asking = joinStage == 10;
    if (m.proto != rv::kProto || (m.type == rv::RV_FAIL && m.reason == rv::F_PROTO)) {
        const std::string why = "the join-code server runs a different version";
        if (asking) joinFail = JF_BACKEND, error = why;
        else if (role == HOST && codeState == CODE_ASKING) codeState = CODE_FAILED, codeError = why;
        return;
    }
    switch (m.type) {
        case rv::RV_REGISTERED:
            if (role != HOST || codeState != CODE_ASKING || m.nonce != rvNonce) return;
            joinCode = m.code, rvKey = m.key;
            codeState = CODE_OPEN;
            rvNext = now + rv::kKeepEvery;
            events.push_back("OPEN TO FRIENDS: CODE " + rv::formatCode(joinCode));
            return;
        case rv::RV_KEPT: return;
        case rv::RV_INTRO: {
            if (role == HOST && m.nonce == 0) {
                if (codeState != CODE_OPEN) return;  // (closed to friends: nobody new)
                bool known = false;
                for (const Intro& in : intros) known |= in.pair == m.pair;
                if (!known && intros.size() < 64) {
                    Intro in;
                    in.pair = m.pair, in.secret = m.secret, in.relayKey = m.key, in.pubIp = m.seen.ip, in.pubPort = m.seen.port;
                    in.cands.push_back({m.seen.ip, m.seen.port});
                    for (const rv::Endpoint& e : m.locals) in.cands.push_back({e.ip, e.port});
                    in.born = now, in.nextPunch = now;
                    intros.push_back(in);
                }
                rv::Msg a;
                a.type = rv::RV_INTRO_ACK, a.code = joinCode, a.key = rvKey, a.pair = m.pair;
                sendBackend(rv::encode(a));
                serviceRendezvous(now);  // (punch at once)
                return;
            }
            if (!asking || m.nonce != rvNonce) return;
            codeIntro = Intro();
            codeIntro.pair = m.pair, codeIntro.secret = m.secret, codeIntro.relayKey = m.key, codeIntro.pubIp = m.seen.ip, codeIntro.pubPort = m.seen.port;
            codeIntro.cands.push_back({m.seen.ip, m.seen.port});
            for (const rv::Endpoint& e : m.locals) codeIntro.cands.push_back({e.ip, e.port});
            joinStage = 11;
            punchT0 = now, nextPunch = 0;
            directFound = false;
            return;
        }
        case rv::RV_COOKIE:
            if (!asking || m.nonce != rvNonce) return;
            rvCookie = m.cookie;
            rvNext = 0;  // (ask again at once, with it)
            return;
        case rv::RV_FAIL: {
            if (asking && m.nonce == rvNonce) {
                failPlayers = m.players, failMax = m.maxPlayers;
                switch (m.reason) {
                    case rv::F_NOT_FOUND: joinFail = JF_NOT_FOUND, error = "no game has that code"; break;
                    case rv::F_FULL: joinFail = JF_FULL, error = "that game is full"; break;
                    case rv::F_OLDER: joinFail = JF_OLDER, error = std::string("your game is older than your friend's: update to ") + net::kRelease + " or newer"; break;
                    case rv::F_NEWER: joinFail = JF_NEWER, error = "your friend's game is older than yours: they need to update"; break;
                    case rv::F_BUSY: joinFail = JF_BUSY, error = "too many tries: wait a minute"; break;
                    default: joinFail = JF_OTHER, error = "the join-code server said no"; break;
                }
                return;
            }
            if (role != HOST) return;
            if (codeState == CODE_ASKING && m.nonce == rvNonce) {
                codeState = CODE_FAILED;
                codeError = m.reason == rv::F_BUSY ? "the join-code server is busy, or you have too many codes open: try again in a minute" : "the join-code server said no";
            } else if (m.reason == rv::F_GONE && codeState == CODE_OPEN) {
                events.push_back("THE JOIN CODE EXPIRED: GETTING A NEW ONE");
                codeState = CODE_ASKING, rvNonce = net::random64(), rvTries = 0, rvNext = now;
            }
            return;
        }
        default: return;
    }
}

bool NetSession::beginJoinCode(const std::string& code, const std::string& name, double timeout) {
    close();
    passwordIssue = PW_OK;
    joinFail = JF_NONE;
    lostWhy.clear();
    error.clear();
    netInit();
    codeWanted = rv::normalizeCode(code);
    if (codeWanted.empty()) {
        joinFail = JF_NOT_A_CODE;
        error = "that isn't a join code: six letters and digits, like KX7-M4Q";
        return false;
    }
    std::string why;
    if (!resolveRendezvous(why)) {
        joinFail = JF_BACKEND;
        error = why;
        return false;
    }
    udpFd = udpOpen(0, 0);
    if (udpFd < 0) {
        joinFail = JF_OTHER;
        error = "can't open a network socket";
        return false;
    }
    joinAddress = "code " + rv::formatCode(codeWanted);
    joinName = net::sanitize(name, net::kMaxNameLen);
    if (joinName.empty()) joinName = "PLAYER";
    joinTimeout = timeout;
    rvNonce = net::random64();
    rvCookie = 0;
    rvNext = 0;
    rvHeard = 0;
    joinStage = 10;
    joinT0 = clockNow();
    return true;
}

bool NetSession::joinWithCode(const std::string& code, const std::string& name, double timeout) {
    if (!beginJoinCode(code, name, timeout)) return false;
    for (;;) {
        const int st = pollJoin();
        if (st > 0) return true;
        if (st < 0) return false;
        pollfd rp{};
        rp.fd = udpFd;
        rp.events = POLLIN;
        if (rp.fd >= 0) pollFds(&rp, 1, 5);
    }
}

int NetSession::pollCodeJoin(double now) {
    pumpUdp(now);
    if (joinFail != JF_NONE) return -1;
    if (joinStage == 10) {  // asking the backend where the code's game is
        if (now >= rvNext) {
            rv::Msg m;
            m.type = rv::RV_LOOKUP, m.version = net::kVersion, m.code = codeWanted, m.nonce = rvNonce, m.cookie = rvCookie;
            if (shareLocals) {
                const uint16_t mine = boundPort(udpFd);
                for (const net::LocalAddress& a : net::localAddresses())
                    if ((int)m.locals.size() < rv::kMaxLocals) m.locals.push_back({a.addr, mine});
            }
            sendBackend(rv::encode(m));
            rvNext = now + 0.7;
        }
        if (joinStage == 10 && now - joinT0 > 6) {
            joinFail = JF_BACKEND;
            error = rvHeard > 0 ? "the join-code server stopped answering" : "can't reach the join-code server: check your internet, or join by address";
            return -1;
        }
        if (joinStage == 10) return 0;
    }
    if (joinStage == 11) {  // punching: both send at once; the first to get through is the path
        if (!directFound && !forceRelay && now - punchT0 < punchSecs) {
            if (now >= nextPunch) {
                nextPunch = now + 0.1;
                for (const auto& c : codeIntro.cands) punchTo(codeIntro.pair, codeIntro.secret, T_PUNCH, c.first, c.second);
            }
            return 0;
        }
        Conn* c = new Conn;
        c->peer = 0;
        c->born = c->lastRx = now;
        c->tun = new Tunnel;
        Tunnel& t = *c->tun;
        t.pair = codeIntro.pair, t.secret = codeIntro.secret, t.relayKey = codeIntro.relayKey;
        t.relayed = !directFound;
        t.ip = directIp, t.port = directPort;
        t.born = now;
        t.stream.lastHeard = now;
        conns.push_back(c);
        delete (sockaddr_in*)hostUdp;
        hostUdp = new sockaddr_in(sockAddr(directFound ? directIp : rvIp, directFound ? directPort : rvPort));  // (unused for sending: the tunnel's path is)
        role = CLIENT;
        Writer w;
        w.put<uint16_t>(net::kVersion);
        w.str(joinName);
        sendTo(*c, frame(MSG_HELLO, w));
        serviceTunnels(now);
        joinStage = 2;
        joinT0 = now;
        joinTimeout = std::max(joinTimeout - (now - punchT0), 8.0);
    }
    return 0;
}
