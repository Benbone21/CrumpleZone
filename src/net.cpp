#ifdef _WIN32
#define _CRT_RAND_S  // (rand_s: the secrets come from the system's generator)
#endif
#include "thread_cache.h"
// Multiplayer session: see net.h.
#include "net.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "mods.h"

// ---- Sockets: POSIX here, Winsock on Windows --------------------------------------
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // (Vista and later: the adapters' prefix lengths)
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef int socklen_t;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static void closeSock(int fd) { closesocket((SOCKET)fd); }
static bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static bool addrInUse() { return WSAGetLastError() == WSAEADDRINUSE; }
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
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS uses the per-socket SO_NOSIGPIPE option instead.
#endif
static void closeSock(int fd) { ::close(fd); }
static bool addrInUse() { return errno == EADDRINUSE; }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static void nonBlocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    // A disconnected peer must return an error, including in tools without main's signal handler.
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}
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
    MSG_LAMPS,        // u32 car id, u8 LampMode: reliable, also while the car sleeps
    MSG_CHALLENGE,    // host -> client: 16 random bytes; answer with MSG_AUTH (only if there's a password)
    MSG_AUTH,         // client -> host: HMAC-SHA-256(password, those 16 bytes)
};
// (WELCOME: u8 id, u8 map, u64 the guest's datagram secret.)
enum DgramType : uint8_t { DG_SNAP = 1, DG_HELLO = 2 };
constexpr uint16_t kMagic = 0xC27A;
// ... and the u64 secret of whoever the datagram is for (told in the welcome): anyone else's is dropped.
constexpr size_t kDgramHeader = 2 + 1 + 1 + 4 + 4 + 1 + 1 + 8;
constexpr size_t kTokenAt = 2 + 1 + 1 + 4 + 4 + 1 + 1;
constexpr size_t kChunk = 1150;
constexpr int kMaxFragments = 32;  // (a state is a few KB: more is garbage)
constexpr double kPosLimit = 5.0e4;  // m: nothing in any world is further out than this
constexpr size_t kMaxPending = 8192, kMaxPendingBytes = 32u << 20;
constexpr size_t kMaxOut = 16u << 20;  // queued for one peer that isn't reading

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

// ---- Checks on what comes in ----------------------------------------------------------

bool okNum(double v, double lim) { return std::isfinite(v) && std::fabs(v) <= lim; }
bool okPos(const V3& p) { return okNum(p.x, kPosLimit) && okNum(p.y, kPosLimit) && okNum(p.z, kPosLimit); }

double clockNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void randomBytes(uint8_t* out, size_t n) {
    size_t got = 0;
#ifdef _WIN32
    while (got < n) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) break;
        for (int k = 0; k < 4 && got < n; k++) out[got++] = (uint8_t)(v >> (8 * k));
    }
#else
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        got = std::fread(out, 1, n, f);
        std::fclose(f);
    }
#endif
    for (uint64_t mix = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count(); got < n; got++, mix = mix * 6364136223846793005ull + 1442695040888963407ull)
        out[got] = (uint8_t)(mix >> 33);  // (no system generator: better than nothing)
}

bool sameBytes(const uint8_t* a, const uint8_t* b, size_t n) {  // (in constant time)
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return d == 0;
}

sockaddr_in udpAddress(uint32_t ip, uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port);
    return a;
}

// Race chatter only the host may say: a guest sending these is lying about who's in charge.
bool hostOnlyChatter(const std::string& t) { return t.rfind("job", 0) == 0 || t.rfind("restart", 0) == 0; }

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

// ---- Text and proofs from the network -----------------------------------------------

namespace net {

int parsePort(const std::string& s, int minPort) {
    if (s.empty() || s.size() > 5) return -1;
    int v = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return -1;
        v = v * 10 + (ch - '0');
    }
    return v >= std::max(minPort, 1) && v <= kMaxPort ? v : -1;
}

bool parseAddress(const std::string& address, std::string& host, int& port, std::string* why) {
    auto no = [&](const std::string& m) {
        if (why) *why = m;
        return false;
    };
    size_t a = address.find_first_not_of(" \t"), b = address.find_last_not_of(" \t");
    const std::string s = a == std::string::npos ? "" : address.substr(a, b - a + 1);
    if (s.empty()) return no("no address");
    const size_t colon = s.find(':');
    if (colon != std::string::npos && s.find(':', colon + 1) != std::string::npos) return no("one ':' only: IPv6 isn't supported (use IP:PORT) in " + s);
    host = colon == std::string::npos ? s : s.substr(0, colon);
    port = kDefaultPort;
    if (colon != std::string::npos) {
        port = parsePort(s.substr(colon + 1));
        if (port < 0) return no("no good port in " + s + " (1 to 65535)");
    }
    if (host.empty()) return no("no address");
    return true;
}

std::string sanitize(const std::string& s, size_t maxLen) {
    std::string o;
    for (unsigned char ch : s)
        if (ch >= 32 && ch <= 126) o += (char)ch;  // (the HUD font has nothing else: controls and bytes above ASCII go)
    const size_t a = o.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    o.erase(0, a);
    o.resize(std::min(o.size(), maxLen));
    o.erase(o.find_last_not_of(' ') + 1);
    return o;
}

namespace {
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t fill = 0;
    uint64_t total = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t v[8];
        std::memcpy(v, h, sizeof v);
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
            const uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            std::memmove(v + 1, v, 7 * sizeof(uint32_t));
            v[4] += t1;
            v[0] = t1 + t2;
        }
        for (int i = 0; i < 8; i++) h[i] += v[i];
    }
    void add(const uint8_t* p, size_t n) {
        total += n;
        while (n) {
            const size_t k = std::min(n, sizeof buf - fill);
            std::memcpy(buf + fill, p, k);
            fill += k, p += k, n -= k;
            if (fill == sizeof buf) block(buf), fill = 0;
        }
    }
    std::vector<uint8_t> finish() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (fill != 56) add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(len, 8);
        std::vector<uint8_t> out(32);
        for (int i = 0; i < 32; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
        return out;
    }
};
}  // namespace

std::vector<uint8_t> hmacSha256(const std::string& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> k(key.begin(), key.end());
    if (k.size() > 64) {
        Sha256 h;
        h.add(k.data(), k.size());
        k = h.finish();
    }
    k.resize(64, 0);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) ipad[i] = k[i] ^ 0x36, opad[i] = k[i] ^ 0x5c;
    Sha256 in;
    in.add(ipad, 64);
    in.add(data.data(), data.size());
    const std::vector<uint8_t> inner = in.finish();
    Sha256 out;
    out.add(opad, 64);
    out.add(inner.data(), inner.size());
    return out.finish();
}

}  // namespace net

// ---- Connection and remote-vehicle state ----------------------------------------

struct NetSession::Snap {
    double t = 0;
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
    double newestT = -1, newestArrival = 0;
    double clockOffset = 0, playbackTime = 0, playbackNow = -1;
    bool clockSet = false;
    struct Assembly {
        bool active = false;
        uint32_t seq = 0;
        std::vector<uint8_t> data, got;
        int count = 0;
    };
    std::array<Assembly, 3> assemblies;  // A newer partial packet must not discard an older complete one.
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
        rp.fd = conns.empty() ? connectFd : conns[0]->fd;
        rp.events = conns.empty() ? POLLOUT : POLLIN;
        if (rp.fd >= 0) pollFds(&rp, 1, 20);
    }
}

bool NetSession::beginJoin(const std::string& address, const std::string& name, double timeout) {
    close();
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
        sendTo(*c, frame(MSG_HELLO, w));
        joinStage = 2;
        joinT0 = now;
    }
    if (joinStage == 2) {  // waiting to be let in
        if (role != CLIENT || conns.empty() || conns[0]->fd < 0) return fail(error.empty() ? "connection closed" : error);
        Conn& c = *conns[0];
        flush(c);
        readTcp(c);  // handles WELCOME / REJECT (other messages wait for adopt())
        if (role != CLIENT || (self < 0 && c.fd < 0)) return fail(error.empty() ? "connection closed" : error);
        if (self < 0) {
            if (now - joinT0 > joinTimeout) return fail("the host didn't let us in");
            return 0;
        }
        joinStage = 3;
        joinT0 = joinQuiet = now;
    }
    if (joinStage == 3) {
        // The host sends its vehicles right behind the welcome: take them in now (they wait
        // in `pending` for adopt()), so we park clear of them rather than on one.
        if (role != CLIENT || conns.empty() || conns[0]->fd < 0) return fail(error.empty() ? "connection closed" : error);
        const size_t before = pending.size();
        readTcp(*conns[0]);
        if (role != CLIENT || conns[0]->fd < 0) return fail(error.empty() ? "connection closed" : error);
        if (pending.size() != before) joinQuiet = now;
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
    if (c.out.size() + msg.size() > kMaxOut) {  // not reading what we send: that's enough of them
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
            if (!r.ok) return violation(from, "a bad hello");
            if (ver != net::kVersion) {
                Writer w;
                w.str("different game version");
                sendTo(*from, frame(MSG_REJECT, w));
                from->kill = true, from->killT = now;
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
            case MSG_PING: {
                Writer w;
                w.put<double>(r.get<double>());
                sendTo(*from, frame(MSG_PONG, w));
                return;
            }
            case MSG_PONG: {
                double rtt = now - r.get<double>();
                if (!r.ok) return;
                if (!std::isfinite(rtt)) rtt = 0;
                rtt = clampd(rtt, 0, 10);
                players[from->peer].ping = rtt;
                Writer w;
                w.put<uint8_t>((uint8_t)from->peer);
                w.put<float>((float)rtt);
                broadcast(frame(MSG_RTT, w));
                return;
            }
            case MSG_GAME: {
                r.get<uint8_t>();  // (who it says it is from: it's from the guest)
                const std::string text = net::sanitize(r.str(), 255);
                if (!r.ok) return violation(from, "a bad chatter message");
                if (!from->games.take(now, 1, 30, 60)) return;
                if (hostOnlyChatter(text)) return note(players[from->peer].name + " SENT A HOST'S MESSAGE");
                Writer w;
                w.put<uint8_t>((uint8_t)from->peer);
                w.str(text);
                broadcast(frame(MSG_GAME, w), from);
                gameIn.push_back({from->peer, text});
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
            if (!r.ok || id < 1 || id >= net::kMaxPeers || m >= MAP_COUNT) {
                error = "the host's welcome made no sense";
                role = OFF;
                return;
            }
            self = id;
            map = m;
            udpToken = token;
            return;
        }
        case MSG_REJECT: {
            if (role != CLIENT || self >= 0) return;
            error = "rejected: " + net::sanitize(r.str(), 60);
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
            if (!r.ok || who >= net::kMaxPeers || who == self || (who != 0 && hostOnlyChatter(text))) return;
            gameIn.push_back({who, text});
            return;
        }
        case MSG_CLOCK: {
            if (role != CLIENT) return;
            const double hours = r.get<float>(), rate = r.get<float>();
            if (!r.ok || !okNum(hours, 1e6) || !okNum(rate, 1e3)) return;
            dayHours = std::fmod(std::max(0.0, hours), 24.0);
            dayRate = rate;
            dayFresh = true;
            return;
        }
        case MSG_PONG: {
            double rtt = clockNow() - r.get<double>();
            if (!r.ok) return;
            if (!std::isfinite(rtt)) rtt = 0;
            if (self >= 0 && self < net::kMaxPeers) players[self].ping = clampd(rtt, 0, 10);
            return;
        }
        case MSG_HELLO:  // (what only a host hears)
        case MSG_AUTH:
            return;
        default:
            break;
    }
    // Everything else concerns the world: applied once the sim has joined the session.
    if (sim && adopted) {
        apply(*sim, msg, -1);
    } else if (role == CLIENT && pending.size() < kMaxPending && pendingBytes + msg.size() <= kMaxPendingBytes) {
        pending.push_back(msg);
        pendingBytes += msg.size();
    }
}

void NetSession::welcome(Sim& sim, Conn& c) {
    Writer w;
    w.put<uint8_t>((uint8_t)c.peer);
    w.put<uint8_t>((uint8_t)map);
    w.put<uint64_t>(c.token);
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
    return frame(MSG_SPAWN, w);
}

std::vector<uint8_t> NetSession::lampMsg(const Car& c) const {
    Writer w;
    w.put<uint32_t>(c.netId);
    w.put<uint8_t>(c.lampMode);
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
                sim.cars[i].build(*spec, pos, yaw, color);
            }
            Car& c = sim.cars[i];
            c.remote = true;
            c.netId = netId;
            c.handbrake = 0;
            c.lampMode = std::min<uint8_t>(lamps, 3);
            c.buildCount = (int)build;
            if (Remote* rm = remoteFor(netId)) {
                rm->buf.clear();
                rm->newestT = -1;
                rm->clockSet = false;
                rm->playbackNow = -1;
                for (auto& assembly : rm->assemblies) assembly.active = false;
            }
            return true;
        }
        case MSG_LAMPS: {
            const uint32_t id = r.get<uint32_t>();
            const uint8_t mode = r.get<uint8_t>();
            const int i = findCar(sim, id);
            if (!r.ok || !ownerOk(id) || mode >= 4 || i < 0 || !sim.cars[i].remote) return false;
            sim.cars[i].lampMode = mode;
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
        case MSG_UNROPE: {
            const uint32_t netId = r.get<uint32_t>();
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

std::vector<uint8_t> NetSession::snapshotMsg(const Sim& sim, const Car& c, double now) const {
    Writer w;
    const Frame fr = c.frame();
    const Quat rot = c.chassis.nodes.empty() ? Quat::axisAngle(V3(0, 1, 0), std::atan2(-fr.fwd.z, fr.fwd.x)) : c.chassis.rot;
    w.put<double>(now - sessionStart);
    w.put<uint32_t>((uint32_t)c.buildCount);
    w.v3(fr.com);
    w.put<float>((float)rot.w), w.put<float>((float)rot.x), w.put<float>((float)rot.y), w.put<float>((float)rot.z);
    w.v3(fr.vel);
    w.put<float>((float)c.steer);
    w.put<uint8_t>((uint8_t)std::lround(clampd(std::max(c.brake, c.handbrake * 0.6), 0, 1) * 255));
    w.put<float>((float)c.damage);
    w.put<uint8_t>((uint8_t)(c.engineDead | (c.hydrolocked << 1)));
    w.put<float>((float)c.rpm);
    w.put<float>((float)clampd(c.throttle * c.tcCut, 0, 1));
    w.put<float>((float)c.boost);
    w.put<int8_t>((int8_t)c.gear);
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
    return w.b;
}

void NetSession::sendSnapshot(Sim& sim, const Car& c, double now) {
    const std::vector<uint8_t> data = snapshotMsg(sim, c, now);
    // Split into datagrams.
    const size_t total = data.size(), frags = (total + kChunk - 1) / kChunk;
    seq++;
    for (size_t f = 0; f < frags; f++) {
        auto datagram = [&](uint64_t token) {
            Writer d;
            d.put<uint16_t>(kMagic);
            d.put<uint8_t>(DG_SNAP);
            d.put<uint8_t>((uint8_t)self);
            d.put<uint32_t>(c.netId);
            d.put<uint32_t>(seq);
            d.put<uint8_t>((uint8_t)f);
            d.put<uint8_t>((uint8_t)frags);
            d.put<uint64_t>(token);
            const size_t a = f * kChunk, b = std::min(total, a + kChunk);
            d.b.insert(d.b.end(), data.begin() + a, data.begin() + b);
            return d.b;
        };
        if (role == CLIENT && hostUdp) {
            const std::vector<uint8_t> d = datagram(udpToken);
            const int n = (int)sendto(udpFd, (const char*)d.data(), (int)d.size(), 0, (sockaddr*)hostUdp, sizeof(sockaddr_in));
            if (n > 0) bytesOut += n;
        } else {
            for (Conn* cn : conns)
                if (cn->udpKnown && cn->hello && !cn->kill) {
                    const std::vector<uint8_t> d = datagram(cn->token);
                    const sockaddr_in to = udpAddress(cn->udpIp, cn->udpPort);
                    const int n = (int)sendto(udpFd, (const char*)d.data(), (int)d.size(), 0, (const sockaddr*)&to, sizeof to);
                    if (n > 0) bytesOut += n;
                }
        }
    }
}

void NetSession::readUdp(Sim& sim, double now) {
    uint8_t buf[2048];
    for (int k = 0; k < 1024; k++) {  // (a frame's worth; the rest waits, or the kernel drops it)
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const int n = (int)recvfrom(udpFd, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return;
        bytesIn += n;
        onDatagram(sim, buf, (size_t)n, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), now);
    }
}

// One datagram, from ip:port (host byte order). It must carry the secret of whoever it's for:
// the host told each guest theirs in the welcome, so a stranger's packet (a scan, a forged
// state, a forged address for a guest) is dropped here, unanswered.
void NetSession::onDatagram(Sim& sim, const uint8_t* buf, size_t n, uint32_t ip, uint16_t port, double now) {
    if (n < kDgramHeader || n > kDgramHeader + kChunk) return;
    Reader h{buf, n};
    if (h.get<uint16_t>() != kMagic) return;
    const uint8_t type = h.get<uint8_t>(), peer = h.get<uint8_t>();
    const uint32_t netId = h.get<uint32_t>(), sq = h.get<uint32_t>();
    const uint8_t frag = h.get<uint8_t>(), frags = h.get<uint8_t>();
    const uint64_t token = h.get<uint64_t>();
    if (type != DG_SNAP && type != DG_HELLO) return;
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
        if (type == DG_HELLO) {  // the guest's UDP address (what a NAT made of it): say we heard, with just the header
            guest->udpIp = ip, guest->udpPort = port;
            guest->udpKnown = true;
            const sockaddr_in to = udpAddress(guest->udpIp, guest->udpPort);
            sendto(udpFd, (const char*)buf, (int)kDgramHeader, 0, (const sockaddr*)&to, sizeof to);
            return;
        }
        const double t = clockNow();
        if (!guest->udpKnown || !guest->dgrams.take(t, 1, 300, 600) || !guest->dgramBytes.take(t, (double)n, 600000, 1200000)) return;
        if (!piece()) return;
        const int ci = findCar(sim, netId);
        if (!adopted || ci < 0 || !sim.cars[ci].remote) return;
        for (Conn* c : conns)  // pass it on to everyone else, in their own envelope
            if (c->udpKnown && c->hello && !c->kill && c->peer != peer) {
                uint8_t out[2048];
                std::memcpy(out, buf, n);
                std::memcpy(out + kTokenAt, &c->token, sizeof c->token);
                const sockaddr_in to = udpAddress(c->udpIp, c->udpPort);
                sendto(udpFd, (const char*)out, (int)n, 0, (const sockaddr*)&to, sizeof to);
            }
        takeFragment(sim, netId, sq, frag, frags, buf + kDgramHeader, size, now);
        return;
    }
    if (role != CLIENT || token != udpToken || !udpToken) return;
    if (type == DG_HELLO) {  // the host heard us
        udpReady = true;
        return;
    }
    if (!piece() || peer == self) return;
    udpReady = true;
    if (!adopted) return;
    takeFragment(sim, netId, sq, frag, frags, buf + kDgramHeader, size, now);
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
        takeSnapshot(sim, netId, slot->data, now);
        slot->active = false;
    }
}

void NetSession::takeSnapshot(Sim& sim, uint32_t netId, const std::vector<uint8_t>& data, double now) {
    // A whole state: decode it. Also exercised without sockets by the regression tests.
    const int ci = findCar(sim, netId);
    if (ci < 0 || !sim.cars[ci].remote) return;
    Remote* rm = remoteFor(netId);
    const Car& car = sim.cars[ci];
    Reader r{data.data(), data.size()};
    Snap s;
    s.t = r.get<double>();
    const uint32_t build = r.get<uint32_t>();
    if (build != (uint32_t)car.buildCount) return;
    s.com = r.v3();
    s.rot.w = r.get<float>(), s.rot.x = r.get<float>(), s.rot.y = r.get<float>(), s.rot.z = r.get<float>();
    s.vel = r.v3();
    s.steer = r.get<float>();
    s.brake = r.get<uint8_t>() / 255.0f;
    s.damage = r.get<float>();
    s.status = r.get<uint8_t>() & 3;
    s.rpm = r.get<float>();
    s.load = r.get<float>();
    s.boost = r.get<float>();
    s.gear = r.get<int8_t>();
    const int nw = r.get<uint8_t>();
    if (nw != (int)car.wheels.size()) return;  // a rebuilt spec on the way
    for (int k = 0; k < nw; k++) {
        s.wheelAngle.push_back(r.get<float>());
        s.wheelOmega.push_back(r.get<float>());
        s.wheelFlags.push_back(r.get<uint8_t>());
    }
    const int nn = r.get<uint16_t>();
    if (nn != (int)car.nodes.size()) return;
    s.local.resize(nn);
    for (int k = 0; k < nn; k++) {
        const double x = r.get<int16_t>() * 0.001, y = r.get<int16_t>() * 0.001, z = r.get<int16_t>() * 0.001;
        s.local[k] = V3(x, y, z);
    }
    const int nr = r.get<uint8_t>();
    if (nr > 8) return;
    for (int k = 0; k < nr; k++) {
        const uint32_t id = r.get<uint32_t>();
        s.ropeLen.push_back({id, r.get<float>()});
    }
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
    const double offset = now - s.t;
    if (!rm->clockSet) {
        rm->clockOffset = offset;
        rm->clockSet = true;
    } else {
        // Arrival jitter must not reset the playback clock on every packet. Track the
        // clock difference slowly; the presentation clock below never runs backwards.
        rm->clockOffset += clampd(offset - rm->clockOffset, -0.05, 0.05) * 0.02;
    }
    if (!rm->buf.empty()) {
        const Snap& previous = rm->buf.back();
        const double elapsed = s.t - previous.t;
        const V3 predicted = previous.com + (previous.vel + s.vel) * (elapsed * 0.5);
        if (len(s.com - predicted) > std::max(4.0, elapsed * 20.0)) {
            // Repairs/recovery/spawn changes jump immediately, without interpolating
            // a car through everything between the old and new positions.
            rm->buf.clear();
            rm->playbackNow = -1;
        }
    }
    rm->newestT = s.t;
    rm->newestArrival = now;
    rm->buf.push_back(std::move(s));
    while (rm->buf.size() > 30) rm->buf.pop_front();
}

void NetSession::applyRemotes(Sim& sim, double now) {
    for (Remote* rm : remotes) {
        const int ci = findCar(sim, rm->netId);
        if (ci < 0 || rm->buf.empty()) continue;
        Car& c = sim.cars[ci];
        if (!c.remote) continue;
        // Play back a little behind the newest state, in the sender's time.
        const double target = now - rm->clockOffset - net::kPlaybackDelay;
        if (rm->playbackNow < 0) rm->playbackTime = target;
        else rm->playbackTime += clampd(target - rm->playbackTime, 0, std::max(0.0, now - rm->playbackNow) * 1.1);
        rm->playbackNow = now;
        const double t = rm->playbackTime;
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
            if (&n == &a) {
                com += a.vel * clampd(at - a.t, 0, 0.25);  // nothing newer: coast briefly
                if (at > a.t + 0.25) vel = V3();
            }
            rot = nlerp(a.rot, n.rot, alpha);
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
        const bool stopped = t >= rm->buf.back().t + 0.25;
        for (size_t i = 0; i < c.nodes.size(); i++) {
            c.nodes[i].p = now0[i];
            c.nodes[i].v = jumped || stopped ? vel : (now0[i] - prev[i]) / 0.05;
            c.nodes[i].f = V3();
        }
        if (!c.chassis.nodes.empty()) c.chassis.rot = rot;
        const Snap& s = rm->buf[std::min(idx + (alpha > 0.5), rm->buf.size() - 1)];
        const Snap& a = rm->buf[idx];
        const Snap& b = rm->buf[std::min(idx + 1, rm->buf.size() - 1)];
        auto blend = [alpha](double x, double y) { return x + (y - x) * alpha; };
        for (size_t k = 0; k < c.wheels.size() && k < s.wheelAngle.size(); k++) {
            Wheel& wh = c.wheels[k];
            wh.angle = blend(a.wheelAngle[k], b.wheelAngle[k]);
            wh.omega = blend(a.wheelOmega[k], b.wheelOmega[k]);
            if (&a == &b) wh.angle += wh.omega * clampd(t - a.t, 0, 0.25);
            wh.popped = s.wheelFlags[k] & 1;
            wh.detached = (s.wheelFlags[k] >> 1) & 1;
            wh.shaftBroken = (s.wheelFlags[k] >> 2) & 1;
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
    const double wall = clockNow();
    if (role == HOST) accept();
    for (size_t i = 0; i < conns.size();) {
        Conn& c = *conns[i];
        if (role == HOST && !c.kill) {  // nobody sits on a slot saying nothing
            if (!c.hello && wall - c.born > handshakeSecs) violation(&c, "no hello in time");
            else if (wall - c.lastRx > idleSecs) violation(&c, "silent");
        }
        readTcp(c);
        if (role == OFF) return;
        if (c.kill) flush(c);
        if (c.fd < 0 || (c.kill && wall - c.killT > 0.5)) {  // (a refused guest gets half a second to read why)
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
            knownCars.push_back({c.netId, c.spec, c.buildCount, c.lampMode});
            broadcast(spawnMsg(c));
        } else if (k->spec != c.spec || k->builds != c.buildCount) {
            k->spec = c.spec;
            k->builds = c.buildCount;
            k->lamps = c.lampMode;
            broadcast(spawnMsg(c));
        } else if (k->lamps != c.lampMode) {
            k->lamps = c.lampMode;
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
    if (text.size() > net::kMaxLevelBytes) {
        events.push_back("THAT LEVEL IS TOO BIG TO SHARE");
        return;
    }
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
