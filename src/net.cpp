// Multiplayer session: see net.h.
#include "net.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstring>

#include "mods.h"

// ---- Sockets: POSIX here, Winsock on Windows --------------------------------------
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static void closeSock(int fd) { closesocket((SOCKET)fd); }
static bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static void nonBlocking(int fd) {
    u_long one = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &one);
}
static int pollFds(pollfd* p, int n, int ms) { return WSAPoll(p, (ULONG)n, ms); }
static void netInit() {
    static bool done = false;
    if (done) return;
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
    done = true;
}
static int newSocket(int type) { return (int)socket(AF_INET, type, 0); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
static void closeSock(int fd) { ::close(fd); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static void nonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static int pollFds(pollfd* p, int n, int ms) { return poll(p, (nfds_t)n, ms); }
static void netInit() {}
static int newSocket(int type) { return socket(AF_INET, type, 0); }
#endif

namespace {

// ---- Wire format --------------------------------------------------------------
// Reliable messages: u16 length (of what follows), u8 type, payload.
// Datagrams: u16 magic, u8 type, u8 player, u32 netId, u32 seq, u8 frag, u8 frags, payload.

enum MsgType : uint8_t {
    MSG_HELLO = 1,    // client -> host: version, name
    MSG_WELCOME,      // host -> client: your id, the map
    MSG_REJECT,       // host -> client: why not
    MSG_PLAYER,       // id, name: someone is in the session
    MSG_LEFT,         // id
    MSG_SPAWN,        // a vehicle appeared or was rebuilt
    MSG_DESPAWN,      // netId
    MSG_ROPE,         // a line was hooked
    MSG_UNROPE,       // netId
    MSG_PING,         // f64 time
    MSG_PONG,         // f64 time (echoed)
    MSG_RTT,          // id, f32 ping: the host telling everyone the pings
    MSG_CLOCK,        // f32 hours, f32 hours per second: the host's time of day
    MSG_LEVEL,        // u32 serial, u16 part, u16 parts, bytes: the level's text, in parts
    MSG_GAME,         // u8 player, str: race chatter
};
enum DgramType : uint8_t { DG_SNAP = 1, DG_HELLO = 2 };
constexpr uint16_t kMagic = 0xC27A;
constexpr size_t kDgramHeader = 2 + 1 + 1 + 4 + 4 + 1 + 1;
constexpr size_t kChunk = 1150;

struct Writer {
    std::vector<uint8_t> b;
    template <class T> void put(T v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
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
    V3 v3() {
        float x = get<float>(), y = get<float>(), z = get<float>();
        return V3(x, y, z);
    }
};

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
    uint8_t model = 0, mods[3] = {0, 0, 0};
};
SpecId specId(const VehicleSpec* s) {
    SpecId id;
    if (s == &crashBoxSpec()) return id.kind = 1, id;
    if (s == &barrierSpec()) return id.kind = 2, id;
    const auto& cat = vehicleCatalog();
    if (s->model >= 0) {
        id.model = (uint8_t)s->model;
        for (int k = 0; k < 3; k++) id.mods[k] = (uint8_t)s->mods[k];
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
    for (int k = 0; k < 3; k++) m.pick[k] = id.mods[k];
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

// ---- Connection and remote-vehicle state ----------------------------------------

struct NetSession::Conn {
    int fd = -1;
    int peer = -1;
    bool hello = false;
    std::vector<uint8_t> in, out;
    sockaddr_in udp{};
    bool udpKnown = false;
    double pingSent = 0;
};

struct NetSession::Snap {
    double t = 0;
    V3 com, vel;
    Quat rot;
    std::vector<V3> local;  // node offsets from the rigid rest shape
    std::vector<float> wheelAngle, wheelOmega;
    std::vector<uint8_t> wheelFlags;
    float steer = 0, brake = 0, damage = 0;
    uint8_t status = 0;
    std::vector<std::pair<uint32_t, float>> ropeLen;
};

struct NetSession::Remote {
    uint32_t netId = 0;
    std::deque<Snap> buf;
    double newestT = -1, newestArrival = 0;
    uint32_t asmSeq = 0;
    std::vector<uint8_t> asmData;
    std::vector<uint8_t> asmGot;
    int asmCount = 0;
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
    for (Conn* c : conns) {
        if (c->fd >= 0) closeSock(c->fd);
        delete c;
    }
    conns.clear();
    if (listenFd >= 0) closeSock(listenFd);
    if (udpFd >= 0) closeSock(udpFd);
    if (findFd >= 0) closeSock(findFd);
    listenFd = udpFd = findFd = -1;
    delete (sockaddr_in*)hostUdp;
    hostUdp = nullptr;
    for (Remote* r : remotes) delete r;
    remotes.clear();
    knownCars.clear();
    knownRopes.clear();
    pending.clear();
    players.assign(net::kMaxPeers, NetPlayer());
    role = OFF;
    self = -1;
    nextCarId = nextRopeId = 1;
    adopted = false;
    udpReady = false;
    sessionStart = -1;
}

bool NetSession::host(int port, const std::string& name, int m) {
    close();
    netInit();
    listenFd = newSocket(SOCK_STREAM);
    udpFd = newSocket(SOCK_DGRAM);
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((uint16_t)port);
    if (listenFd < 0 || udpFd < 0 || bind(listenFd, (sockaddr*)&a, sizeof a) < 0 || listen(listenFd, 8) < 0 ||
        bind(udpFd, (sockaddr*)&a, sizeof a) < 0) {
        error = std::string("can't open port ") + std::to_string(port) + ": " + std::strerror(errno);
        close();
        return false;
    }
    nonBlocking(listenFd);
    nonBlocking(udpFd);
    hostPort = port;
    // Listen for "who's hosting?" from the local network (if the port's taken, say by another
    // game hosted on this machine, it just can't be found that way: typing the address works).
    findFd = newSocket(SOCK_DGRAM);
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
        }
    }
    role = HOST;
    self = 0;
    map = m;
    players[0].active = true;
    players[0].name = name;
    return true;
}

bool NetSession::join(const std::string& address, const std::string& name, double timeout) {
    close();
    netInit();
    std::string hostName = address;
    int port = net::kDefaultPort;
    const size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        hostName = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostName.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        error = "can't find " + hostName;
        return false;
    }
    sockaddr_in addr = *(sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    const int fd = newSocket(SOCK_STREAM);
    nonBlocking(fd);
    connect(fd, (sockaddr*)&addr, sizeof addr);
    pollfd pf{};
    pf.fd = fd;
    pf.events = POLLOUT;
    int err = 0;
    socklen_t el = sizeof err;
    if (pollFds(&pf, 1, (int)(timeout * 1000)) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &el) < 0 || err) {
        error = "no answer from " + address;
        closeSock(fd);
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    Conn* c = new Conn;
    c->fd = fd;
    c->peer = 0;
    conns.push_back(c);
    udpFd = newSocket(SOCK_DGRAM);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    bind(udpFd, (sockaddr*)&any, sizeof any);
    nonBlocking(udpFd);
    hostUdp = new sockaddr_in(addr);
    role = CLIENT;
    Writer w;
    w.put<uint16_t>(net::kVersion);
    w.str(name);
    sendTo(*c, frame(MSG_HELLO, w));
    // Wait to be let in.
    const auto t0 = std::chrono::steady_clock::now();
    while (self < 0 && role == CLIENT) {
        flush(*c);
        pollfd rp{};
        rp.fd = fd;
        rp.events = POLLIN;
        pollFds(&rp, 1, 50);
        readTcp(*c);  // handles WELCOME / REJECT (other messages wait for adopt())
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout) {
            error = "the host didn't let us in";
            close();
            return false;
        }
    }
    if (self < 0) {
        if (error.empty()) error = "connection closed";
        close();
        return false;
    }
    // The host sends its vehicles right behind the welcome: take them in now (they wait in
    // `pending` for adopt()), so we park clear of them rather than on one.
    {
        const auto t1 = std::chrono::steady_clock::now();
        while (role == CLIENT && std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < 0.5) {
            pollfd rp{};
            rp.fd = fd;
            rp.events = POLLIN;
            if (pollFds(&rp, 1, 120) <= 0) break;  // quiet for a moment: that's all of it
            readTcp(*c);
        }
    }
    if (role != CLIENT) {
        if (error.empty()) error = "connection closed";
        return false;
    }
    players[self].active = true;
    players[self].name = name;
    return true;
}

// ---- Reliable messages ------------------------------------------------------------

void NetSession::sendTo(Conn& c, const std::vector<uint8_t>& msg) {
    c.out.insert(c.out.end(), msg.begin(), msg.end());
    flush(c);
}

void NetSession::broadcast(const std::vector<uint8_t>& msg, Conn* except) {
    for (Conn* c : conns)
        if (c != except && (role == CLIENT || c->hello)) sendTo(*c, msg);
}

void NetSession::flush(Conn& c) {
    while (!c.out.empty() && c.fd >= 0) {
        const int n = (int)::send(c.fd, (const char*)c.out.data(), (int)c.out.size(), MSG_NOSIGNAL);
        if (n <= 0) break;
        bytesOut += n;
        c.out.erase(c.out.begin(), c.out.begin() + n);
    }
}

void NetSession::readTcp(Conn& c) {
    uint8_t buf[16384];
    for (;;) {
        const int n = (int)recv(c.fd, (char*)buf, sizeof buf, 0);
        if (n > 0) {
            bytesIn += n;
            c.in.insert(c.in.end(), buf, buf + n);
            continue;
        }
        if (n == 0 || !wouldBlock()) {
            closeSock(c.fd);
            c.fd = -1;
        }
        break;
    }
    size_t at = 0;
    while (c.in.size() - at >= 3) {
        const size_t len = c.in[at] | (c.in[at + 1] << 8);
        if (c.in.size() - at - 2 < len) break;
        std::vector<uint8_t> msg(c.in.begin() + at + 2, c.in.begin() + at + 2 + len);
        at += 2 + len;
        handle(lastSim, &c, msg);
    }
    c.in.erase(c.in.begin(), c.in.begin() + at);
}

void NetSession::handle(Sim* sim, Conn* from, const std::vector<uint8_t>& msg) {
    if (msg.empty()) return;
    Reader r{msg.data() + 1, msg.size() - 1};
    const uint8_t type = msg[0];
    switch (type) {
        case MSG_HELLO: {  // host
            if (role != HOST || from->hello) return;
            const uint16_t ver = r.get<uint16_t>();
            const std::string name = r.str();
            int id = -1;
            for (int k = 1; k < net::kMaxPeers && id < 0; k++)
                if (!players[k].active) id = k;
            if (ver != net::kVersion || id < 0) {
                Writer w;
                w.str(ver != net::kVersion ? "different game version" : "session full");
                sendTo(*from, frame(MSG_REJECT, w));
                return;
            }
            from->hello = true;
            from->peer = id;
            players[id].active = true;
            players[id].name = name;
            if (sim) welcome(*sim, *from);
            Writer w;
            w.put<uint8_t>((uint8_t)id);
            w.str(name);
            broadcast(frame(MSG_PLAYER, w), from);
            events.push_back(name + " JOINED");
            return;
        }
        case MSG_WELCOME: {  // client
            self = r.get<uint8_t>();
            map = r.get<uint8_t>();
            return;
        }
        case MSG_REJECT:
            error = "rejected: " + r.str();
            role = OFF;
            return;
        case MSG_PING: {
            Writer w;
            w.put<double>(r.get<double>());
            sendTo(*from, frame(MSG_PONG, w));
            return;
        }
        case MSG_LEVEL: {  // client
            if (role != CLIENT) return;
            const uint32_t serial = r.get<uint32_t>();
            const uint16_t part = r.get<uint16_t>(), parts = r.get<uint16_t>();
            if (part == 0 || serial != levelInSerial) levelIn.clear(), levelInSerial = serial;
            if (r.at < r.n) levelIn.append((const char*)r.p + r.at, r.n - r.at);
            if (part + 1 >= parts) {
                levelText = levelIn;
                levelSerial = serial;
                levelFresh = true;
                levelIn.clear();
            }
            return;
        }
        case MSG_GAME: {
            int who = r.get<uint8_t>();
            const std::string text = r.str();
            if (role == HOST && from) {  // a client's: it's theirs, whatever it says
                who = from->peer;
                Writer w;
                w.put<uint8_t>((uint8_t)who);
                w.str(text);
                broadcast(frame(MSG_GAME, w), from);
            }
            gameIn.push_back({who, text});
            return;
        }
        case MSG_CLOCK:  // client
            if (role != CLIENT) return;
            dayHours = r.get<float>();
            dayRate = r.get<float>();
            dayFresh = true;
            return;
        case MSG_PONG: {
            const double rtt = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - r.get<double>();
            const int who = role == HOST ? from->peer : self;
            if (who >= 0 && who < net::kMaxPeers) players[who].ping = rtt;
            if (role == HOST) {
                Writer w;
                w.put<uint8_t>((uint8_t)who);
                w.put<float>((float)rtt);
                broadcast(frame(MSG_RTT, w));
            }
            return;
        }
        default:
            break;
    }
    // Everything else concerns the world: the host passes it on to the others, and it's
    // applied once the sim has joined the session.
    if (role == HOST && from) broadcast(msg, from);
    if (sim && adopted) apply(*sim, msg);
    else pending.push_back(msg);
}

void NetSession::welcome(Sim& sim, Conn& c) {
    Writer w;
    w.put<uint8_t>((uint8_t)c.peer);
    w.put<uint8_t>((uint8_t)map);
    sendTo(c, frame(MSG_WELCOME, w));
    for (int k = 0; k < net::kMaxPeers; k++)
        if (players[k].active && k != c.peer) {
            Writer p;
            p.put<uint8_t>((uint8_t)k);
            p.str(players[k].name);
            sendTo(c, frame(MSG_PLAYER, p));
        }
    if (!levelText.empty()) sendLevel(&c);
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
    for (int k = 0; k < 3; k++) w.put<uint8_t>(id.mods[k]);
    w.v3(c.color);
    w.v3(fr.com);
    w.put<float>((float)std::atan2(-fr.fwd.z, fr.fwd.x));
    return frame(MSG_SPAWN, w);
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
    return frame(MSG_ROPE, w);
}

void NetSession::apply(Sim& sim, const std::vector<uint8_t>& msg) {
    Reader r{msg.data() + 1, msg.size() - 1};
    switch (msg[0]) {
        case MSG_PLAYER: {
            const int id = r.get<uint8_t>();
            const std::string name = r.str();
            if (id >= 0 && id < net::kMaxPeers && id != self) {
                if (!players[id].active) events.push_back(name + " IS HERE");
                players[id].active = true;
                players[id].name = name;
            }
            return;
        }
        case MSG_LEFT: {
            const int id = r.get<uint8_t>();
            if (id < 0 || id >= net::kMaxPeers) return;
            events.push_back(players[id].name + " LEFT");
            players[id] = NetPlayer();
            removeCars(sim, id);
            return;
        }
        case MSG_RTT: {
            const int id = r.get<uint8_t>();
            const float p = r.get<float>();
            if (id >= 0 && id < net::kMaxPeers && id != self) players[id].ping = p;
            return;
        }
        case MSG_SPAWN: {
            const uint32_t netId = r.get<uint32_t>();
            SpecId id;
            id.kind = r.get<uint8_t>();
            id.model = r.get<uint8_t>();
            for (int k = 0; k < 3; k++) id.mods[k] = r.get<uint8_t>();
            const V3 color = r.v3(), pos = r.v3();
            const double yaw = r.get<float>();
            const VehicleSpec* spec = specFrom(id);
            if (!r.ok || !spec || ownerOf(netId) == self) return;
            int i = findCar(sim, netId);
            if (i < 0) {
                if ((int)sim.cars.size() >= Sim::kMaxBodies) return;
                sim.addCar(*spec, pos, yaw, color);
                i = (int)sim.cars.size() - 1;
            } else {
                sim.dropRopes(i);
                sim.cars[i].build(*spec, pos, yaw, color);
            }
            Car& c = sim.cars[i];
            c.remote = true;
            c.netId = netId;
            c.handbrake = 0;
            if (Remote* rm = remoteFor(netId)) {
                rm->buf.clear();
                rm->newestT = -1;
            }
            return;
        }
        case MSG_DESPAWN: {
            const uint32_t netId = r.get<uint32_t>();
            const int i = findCar(sim, netId);
            if (i >= 0 && sim.cars[i].remote) eraseCar(sim, i);
            for (size_t k = 0; k < remotes.size(); k++)
                if (remotes[k]->netId == netId) {
                    delete remotes[k];
                    remotes.erase(remotes.begin() + k);
                    break;
                }
            return;
        }
        case MSG_ROPE: {
            Rope rp;
            rp.netId = r.get<uint32_t>();
            rp.kind = (RopeKind)r.get<uint8_t>();
            bool ok = true;
            for (RopeEnd* e : {&rp.a, &rp.b}) {
                e->kind = (AnchorKind)r.get<uint8_t>();
                const uint32_t car = r.get<uint32_t>();
                e->hook = r.get<uint8_t>();
                e->p = r.v3();
                if (e->kind == ANCHOR_CAR) {
                    e->car = findCar(sim, car);
                    ok &= e->car >= 0;
                }
            }
            float v[6];
            for (float& x : v) x = r.get<float>();
            rp.length = v[0], rp.maxLen = v[1], rp.k = v[2], rp.c = v[3], rp.breakN = v[4], rp.stallN = v[5];
            if (!r.ok || !ok) return;
            for (const Rope& o : sim.ropes)
                if (o.netId == rp.netId) return;
            sim.ropes.push_back(rp);
            knownRopes.push_back(rp.netId);
            return;
        }
        case MSG_UNROPE: {
            const uint32_t netId = r.get<uint32_t>();
            knownRopes.erase(std::remove(knownRopes.begin(), knownRopes.end(), netId), knownRopes.end());
            for (size_t k = 0; k < sim.ropes.size(); k++)
                if (sim.ropes[k].netId == netId) {
                    sim.ropes.erase(sim.ropes.begin() + k);
                    break;
                }
            return;
        }
        default:
            return;
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

void NetSession::accept() {
    for (;;) {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        const int fd = (int)::accept(listenFd, (sockaddr*)&a, &al);
        if (fd < 0) return;
        nonBlocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        Conn* c = new Conn;
        c->fd = fd;
        conns.push_back(c);
    }
}

void NetSession::dropConn(size_t i) {
    Conn* c = conns[i];
    conns.erase(conns.begin() + i);
    if (c->fd >= 0) closeSock(c->fd);
    const int peer = c->peer;
    const bool wasIn = c->hello;
    delete c;
    if (role == HOST && wasIn && peer > 0) {
        events.push_back(players[peer].name + " LEFT");
        players[peer] = NetPlayer();
        if (lastSim) removeCars(*lastSim, peer);
        Writer w;
        w.put<uint8_t>((uint8_t)peer);
        broadcast(frame(MSG_LEFT, w));
    }
    if (role == CLIENT) {
        events.push_back("LOST THE CONNECTION TO THE HOST");
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
    if (role == CLIENT) {
        // Then my vehicle beside the spawn, in my slot or the first one along that's clear
        // of everything already there (the host's parked trucks, the other players): two
        // trucks built inside each other fight it out.
        Car& me = sim.cars[sim.player];
        const V3 side = V3(std::sin(sim.world.spawnYaw), 0, std::cos(sim.world.spawnYaw));
        const V3 back = V3(-std::cos(sim.world.spawnYaw), 0, std::sin(sim.world.spawnYaw));
        V3 p = sim.world.spawn + side * (6.5 * self);
        for (int k = 0; k < 40; k++) {
            const V3 q = sim.world.spawn + side * (6.5 * (self + k % 8)) + back * (9.0 * (k / 8));
            bool clear = true;
            for (int i = 0; i < (int)sim.cars.size() && clear; i++) {
                if (i == sim.player) continue;
                const Car& o = sim.cars[i];
                const V3 d = o.frame().com - q;
                const double r = std::max(o.spec->length, o.spec->width) / 2 + 3.0;
                if (d.x * d.x + d.z * d.z < r * r) clear = false;
            }
            if (clear) {
                p = q;
                break;
            }
        }
        me.respawn(V3(p.x, sim.world.clearY(p.x, p.z) + 0.05, p.z), sim.world.spawnYaw);
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

void NetSession::sendSnapshot(Sim& sim, const Car& c, double now) {
    Writer w;
    const Frame fr = c.frame();
    const Quat rot = c.chassis.nodes.empty() ? Quat::axisAngle(V3(0, 1, 0), std::atan2(-fr.fwd.z, fr.fwd.x)) : c.chassis.rot;
    w.put<double>(now - sessionStart);
    w.v3(fr.com);
    w.put<float>((float)rot.w), w.put<float>((float)rot.x), w.put<float>((float)rot.y), w.put<float>((float)rot.z);
    w.v3(fr.vel);
    w.put<float>((float)c.steer);
    w.put<uint8_t>((uint8_t)std::lround(clampd(std::max(c.brake, c.handbrake * 0.6), 0, 1) * 255));
    w.put<float>((float)c.damage);
    w.put<uint8_t>((uint8_t)(c.engineDead | (c.hydrolocked << 1)));
    w.put<uint8_t>((uint8_t)c.wheels.size());
    for (const Wheel& wh : c.wheels) {
        w.put<float>((float)wh.angle);
        w.put<float>((float)wh.omega);
        w.put<uint8_t>((uint8_t)(wh.popped | (wh.detached << 1) | (wh.shaftBroken << 2)));
    }
    const V3 rc = restCom(c);
    w.put<uint16_t>((uint16_t)c.nodes.size());
    for (size_t i = 0; i < c.nodes.size(); i++) {
        const V3 d = rot.unrotate(c.nodes[i].p - fr.com) + rc - c.restLocal[i];
        for (double v : {d.x, d.y, d.z}) w.put<int16_t>((int16_t)std::lround(clampd(v, -32.7, 32.7) * 1000));
    }
    // The winch lines this vehicle works: how much line is out.
    std::vector<const Rope*> mine;
    for (const Rope& r : sim.ropes)
        if (r.netId && r.kind == ROPE_WINCH && r.a.kind == ANCHOR_CAR && &sim.cars[r.a.car] == &c) mine.push_back(&r);
    w.put<uint8_t>((uint8_t)mine.size());
    for (const Rope* r : mine) {
        w.put<uint32_t>(r->netId);
        w.put<float>((float)r->length);
    }
    // Split into datagrams.
    const size_t total = w.b.size(), frags = (total + kChunk - 1) / kChunk;
    seq++;
    for (size_t f = 0; f < frags; f++) {
        Writer d;
        d.put<uint16_t>(kMagic);
        d.put<uint8_t>(DG_SNAP);
        d.put<uint8_t>((uint8_t)self);
        d.put<uint32_t>(c.netId);
        d.put<uint32_t>(seq);
        d.put<uint8_t>((uint8_t)f);
        d.put<uint8_t>((uint8_t)frags);
        const size_t a = f * kChunk, b = std::min(total, a + kChunk);
        d.b.insert(d.b.end(), w.b.begin() + a, w.b.begin() + b);
        if (role == CLIENT && hostUdp) {
            const int n = (int)sendto(udpFd, (const char*)d.b.data(), (int)d.b.size(), 0, (sockaddr*)hostUdp, sizeof(sockaddr_in));
            if (n > 0) bytesOut += n;
        } else {
            for (Conn* cn : conns)
                if (cn->udpKnown) {
                    const int n = (int)sendto(udpFd, (const char*)d.b.data(), (int)d.b.size(), 0, (sockaddr*)&cn->udp, sizeof cn->udp);
                    if (n > 0) bytesOut += n;
                }
        }
    }
}

void NetSession::readUdp(Sim& sim, double now) {
    uint8_t buf[2048];
    for (;;) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const int n = (int)recvfrom(udpFd, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return;
        bytesIn += n;
        if ((size_t)n < kDgramHeader) continue;
        Reader h{buf, (size_t)n};
        if (h.get<uint16_t>() != kMagic) continue;
        const uint8_t type = h.get<uint8_t>(), peer = h.get<uint8_t>();
        const uint32_t netId = h.get<uint32_t>(), sq = h.get<uint32_t>();
        const uint8_t frag = h.get<uint8_t>(), frags = h.get<uint8_t>();
        if (type == DG_HELLO) {
            if (role == HOST) {  // a client's UDP address: answer so it knows we heard
                for (Conn* c : conns)
                    if (c->peer == peer && c->hello) {
                        c->udp = from;
                        c->udpKnown = true;
                        sendto(udpFd, (const char*)buf, n, 0, (sockaddr*)&from, fl);
                    }
            } else {
                udpReady = true;
            }
            continue;
        }
        if (type != DG_SNAP || frags == 0 || frag >= frags || peer == self) continue;
        if (role == HOST)  // pass it on to everyone else
            for (Conn* c : conns)
                if (c->udpKnown && c->peer != peer) sendto(udpFd, (const char*)buf, n, 0, (sockaddr*)&c->udp, sizeof c->udp);
        if (role == CLIENT) udpReady = true;
        if (!adopted) continue;
        Remote* rm = remoteFor(netId);
        if (sq < rm->asmSeq) continue;  // older than what we're building
        if (sq > rm->asmSeq) {
            rm->asmSeq = sq;
            rm->asmData.assign((size_t)frags * kChunk, 0);
            rm->asmGot.assign(frags, 0);
            rm->asmCount = 0;
        }
        if (rm->asmGot.size() != frags || rm->asmGot[frag]) continue;
        const size_t len = n - kDgramHeader;
        std::memcpy(rm->asmData.data() + (size_t)frag * kChunk, buf + kDgramHeader, len);
        rm->asmGot[frag] = 1;
        if (frag + 1 == frags) rm->asmData.resize((size_t)frag * kChunk + len);
        if (++rm->asmCount < frags) continue;
        // A whole state: decode it.
        const int ci = findCar(sim, netId);
        if (ci < 0 || !sim.cars[ci].remote) continue;
        const Car& car = sim.cars[ci];
        Reader r{rm->asmData.data(), rm->asmData.size()};
        Snap s;
        s.t = r.get<double>();
        s.com = r.v3();
        s.rot.w = r.get<float>(), s.rot.x = r.get<float>(), s.rot.y = r.get<float>(), s.rot.z = r.get<float>();
        s.vel = r.v3();
        s.steer = r.get<float>();
        s.brake = r.get<uint8_t>() / 255.0f;
        s.damage = r.get<float>();
        s.status = r.get<uint8_t>();
        const int nw = r.get<uint8_t>();
        for (int k = 0; k < nw; k++) {
            s.wheelAngle.push_back(r.get<float>());
            s.wheelOmega.push_back(r.get<float>());
            s.wheelFlags.push_back(r.get<uint8_t>());
        }
        const int nn = r.get<uint16_t>();
        if (nn != (int)car.nodes.size() || nw != (int)car.wheels.size()) continue;  // built from a different spec (a respawn on the way)
        s.local.resize(nn);
        for (int k = 0; k < nn; k++) {
            const double x = r.get<int16_t>() * 0.001, y = r.get<int16_t>() * 0.001, z = r.get<int16_t>() * 0.001;
            s.local[k] = V3(x, y, z);
        }
        const int nr = r.get<uint8_t>();
        for (int k = 0; k < nr; k++) {
            const uint32_t id = r.get<uint32_t>();
            s.ropeLen.push_back({id, r.get<float>()});
        }
        if (!r.ok || s.t <= rm->newestT) continue;
        rm->newestT = s.t;
        rm->newestArrival = now;
        rm->buf.push_back(std::move(s));
        while (rm->buf.size() > 30) rm->buf.pop_front();
    }
}

void NetSession::applyRemotes(Sim& sim, double now) {
    for (Remote* rm : remotes) {
        const int ci = findCar(sim, rm->netId);
        if (ci < 0 || rm->buf.empty()) continue;
        Car& c = sim.cars[ci];
        if (!c.remote) continue;
        // Play back a little behind the newest state, in the sender's time.
        const double t = rm->newestT + (now - rm->newestArrival) - net::kPlaybackDelay;
        while (rm->buf.size() > 2 && rm->buf[1].t < t - 0.5) rm->buf.pop_front();
        const V3 rc = restCom(c);
        auto pose = [&](double at, std::vector<V3>& out, V3& vel, Quat& rot, size_t& idx, double& alpha) {
            const std::deque<Snap>& b = rm->buf;
            idx = 0;
            while (idx + 1 < b.size() && b[idx + 1].t <= at) idx++;
            const Snap& a = b[idx];
            const Snap& n = idx + 1 < b.size() ? b[idx + 1] : a;
            alpha = n.t > a.t ? clampd((at - a.t) / (n.t - a.t), 0, 1) : 0;
            V3 com = a.com + (n.com - a.com) * alpha;
            vel = a.vel + (n.vel - a.vel) * alpha;
            if (&n == &a) com += a.vel * clampd(at - a.t, 0, 0.25);  // nothing newer: coast briefly
            rot = nlerp(a.rot, n.rot, alpha);
            out.resize(c.nodes.size());
            for (size_t i = 0; i < c.nodes.size(); i++)
                out[i] = com + rot.rotate(c.restLocal[i] - rc + a.local[i] + (n.local[i] - a.local[i]) * alpha);
        };
        static thread_local std::vector<V3> now0, prev;
        V3 vel;
        Quat rot;
        size_t idx;
        double alpha;
        pose(t - 0.05, prev, vel, rot, idx, alpha);
        pose(t, now0, vel, rot, idx, alpha);
        // A jump (put back on its wheels, towed home) is a teleport, not a speed.
        const bool jumped = len((now0[0] - prev[0]) / 0.05 - vel) > 25;
        for (size_t i = 0; i < c.nodes.size(); i++) {
            c.nodes[i].p = now0[i];
            c.nodes[i].v = jumped ? vel : (now0[i] - prev[i]) / 0.05;
            c.nodes[i].f = V3();
        }
        if (!c.chassis.nodes.empty()) c.chassis.rot = rot;
        const Snap& s = rm->buf[std::min(idx + (alpha > 0.5), rm->buf.size() - 1)];
        for (size_t k = 0; k < c.wheels.size() && k < s.wheelAngle.size(); k++) {
            Wheel& wh = c.wheels[k];
            wh.angle = s.wheelAngle[k];
            wh.omega = s.wheelOmega[k];
            wh.popped = s.wheelFlags[k] & 1;
            wh.detached = (s.wheelFlags[k] >> 1) & 1;
            wh.shaftBroken = (s.wheelFlags[k] >> 2) & 1;
        }
        c.steer = c.rackSteer = s.steer;
        c.brake = s.brake;
        c.engineDead = s.status & 1;
        c.hydrolocked = (s.status >> 1) & 1;
        c.sleeping = false;
        c.updateBounds();
        for (const auto& rl : s.ropeLen)
            for (Rope& rp : sim.ropes)
                if (rp.netId == rl.first) rp.length = rl.second;
    }
}

// ---- Per frame ------------------------------------------------------------------------------

void NetSession::receive(Sim& sim, double now) {
    if (role == OFF) return;
    lastSim = &sim;
    if (sessionStart < 0) sessionStart = now;
    if (role == HOST) accept();
    for (size_t i = 0; i < conns.size();) {
        readTcp(*conns[i]);
        if (role == OFF) return;
        if (conns[i]->fd < 0) {
            dropConn(i);
            if (role == OFF) return;
        } else {
            flush(*conns[i]);
            i++;
        }
    }
    readUdp(sim, now);
    if (role == HOST) answerFinders();
    // Clients: say hello over UDP until the host has heard (that's how it learns our
    // address through any NAT).
    if (role == CLIENT && !udpReady && now >= nextUdpHello) {
        nextUdpHello = now + 0.25;
        Writer d;
        d.put<uint16_t>(kMagic);
        d.put<uint8_t>(DG_HELLO);
        d.put<uint8_t>((uint8_t)self);
        d.put<uint32_t>(0), d.put<uint32_t>(0), d.put<uint8_t>(0), d.put<uint8_t>(1);
        sendto(udpFd, (const char*)d.b.data(), (int)d.b.size(), 0, (sockaddr*)hostUdp, sizeof(sockaddr_in));
    }
    if (now >= nextPing) {
        nextPing = now + 1.0;
        Writer w;
        w.put<double>(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const auto msg = frame(MSG_PING, w);
        for (Conn* c : conns)
            if (role == CLIENT || c->hello) sendTo(*c, msg);
        if (role == HOST && dayHours >= 0) {  // the sky, once a second (the clients' clocks run on in between)
            Writer t;
            t.put<float>((float)dayHours);
            t.put<float>((float)dayRate);
            const auto clockMsg = frame(MSG_CLOCK, t);
            for (Conn* c : conns)
                if (c->hello) sendTo(*c, clockMsg);
        }
    }
    if (adopted) applyRemotes(sim, now);
}

void NetSession::send(Sim& sim, double now) {
    if (role == OFF || !adopted) return;
    lastSim = &sim;
    // My vehicles: new ones, rebuilt ones (respawns, repairs, a different model), gone ones.
    std::vector<uint32_t> seen;
    for (Car& c : sim.cars) {
        if (c.remote) continue;
        // (Only ever an id with my player number in it: one left from an earlier session, as
        // host or as someone else's guest, would clash with another player's and be taken
        // for theirs.)
        if (!c.netId || (c.netId >> 16) != (uint32_t)self) c.netId = ((uint32_t)self << 16) | (nextCarId++ & 0xffff);
        seen.push_back(c.netId);
        auto k = std::find_if(knownCars.begin(), knownCars.end(), [&](const Known& kc) { return kc.netId == c.netId; });
        if (k == knownCars.end()) {
            knownCars.push_back({c.netId, c.spec, c.buildCount});
            broadcast(spawnMsg(c));
        } else if (k->spec != c.spec || k->builds != c.buildCount) {
            k->spec = c.spec;
            k->builds = c.buildCount;
            broadcast(spawnMsg(c));
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
            broadcast(frame(MSG_UNROPE, w));
            knownRopes.erase(knownRopes.begin() + k);
        } else {
            k++;
        }
    }
    // States, ~20 a second (parked ones twice a second).
    if (sessionStart < 0) sessionStart = now;
    if (now - lastSend < 1.0 / net::kSendRate) return;
    const bool slowTick = std::fmod(now, 0.5) < now - lastSend;
    lastSend = now;
    if (role == CLIENT && !udpReady) return;
    for (const Car& c : sim.cars)
        if (!c.remote && (!c.sleeping || slowTick)) sendSnapshot(sim, c, now);
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
    levelText = text;
    levelSerial++;
    sendLevel(nullptr);
}

void NetSession::sendGame(const std::string& text) {
    if (role == OFF) return;
    Writer w;
    w.put<uint8_t>((uint8_t)std::max(0, self));
    w.str(text);
    if (role == HOST) broadcast(frame(MSG_GAME, w));
    else if (!conns.empty()) sendTo(*conns[0], frame(MSG_GAME, w));
}

// ---- Finding games on the local network -------------------------------------------------

void NetSession::answerFinders() {
    if (findFd < 0) return;
    char buf[256];
    for (int k = 0; k < 16; k++) {
        sockaddr_in from{};
#ifdef _WIN32
        int fl = sizeof from;
#else
        socklen_t fl = sizeof from;
#endif
        const int n = (int)recvfrom(findFd, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) break;
        buf[n] = 0;
        if (std::strncmp(buf, "CZFIND", 6) != 0) continue;
        char reply[160];
        std::snprintf(reply, sizeof reply, "CZHOST %d %d %d %d %s", (int)net::kVersion, hostPort, map, playerCount(),
                      players[0].name.substr(0, 40).c_str());
        sendto(findFd, reply, (int)std::strlen(reply), 0, (sockaddr*)&from, sizeof from);
    }
}

namespace net {

Finder::~Finder() { close(); }

void Finder::close() {
    if (fd >= 0) closeSock(fd);
    fd = -1;
    hosts.clear();
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
    }
    if (now >= nextAsk) {
        nextAsk = now + 1.0;
        // The whole local network, and this machine (a game hosted in another window).
        for (uint32_t ip : {0xffffffffu, 0x7f000001u}) {
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_addr.s_addr = htonl(ip);
            to.sin_port = htons((uint16_t)kDiscoveryPort);
            sendto(fd, "CZFIND", 6, 0, (sockaddr*)&to, sizeof to);
        }
    }
    char buf[256];
    for (int k = 0; k < 32; k++) {
        sockaddr_in from{};
#ifdef _WIN32
        int fl = sizeof from;
#else
        socklen_t fl = sizeof from;
#endif
        const int n = (int)recvfrom(fd, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) break;
        buf[n] = 0;
        int ver = 0, port = 0, mp = 0, pl = 0, used = 0;
        if (std::sscanf(buf, "CZHOST %d %d %d %d %n", &ver, &port, &mp, &pl, &used) < 4 || ver != kVersion) continue;
        char ip[32];
        const uint32_t a = ntohl(from.sin_addr.s_addr);
        std::snprintf(ip, sizeof ip, "%u.%u.%u.%u:%d", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255, port);
        FoundHost h;
        h.address = ip;
        h.name = used > 0 ? std::string(buf + used) : "";
        h.map = mp, h.players = pl, h.seen = now;
        auto it = std::find_if(hosts.begin(), hosts.end(), [&](const FoundHost& o) { return o.address == h.address; });
        if (it != hosts.end()) *it = h;
        else hosts.push_back(h);
    }
    hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [&](const FoundHost& h) { return now - h.seen > 4.0; }), hosts.end());
}

}  // namespace net
