// Multiplayer: up to 8 players on one map, one of them hosting.
//
// Every machine runs the physics of its own vehicles (so driving has no lag) and sends
// their state, dents and all, about 20 times a second. Other players' vehicles show up as
// "remote" cars: moved by the network, solid to touch, winch to and strap to. A line
// between two players' trucks pulls on each truck on its owner's machine.
//
// The host relays everything and picks the map. Reliable messages (joins, spawns, lines)
// go over TCP; vehicle states over UDP, played back ~0.1 s behind so they stay smooth.
// Plain POSIX sockets, nothing else.
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "softbody.h"

namespace net {
constexpr int kDefaultPort = 27015;    // TCP and UDP: only the default; a host can use any port from kMinHostPort up
constexpr int kMinHostPort = 1024;     // (below it needs root; hosting there is refused)
constexpr int kMaxPort = 65535;
// The two LAN-only ports are fixed (every game and finder must agree on them: a host's real game
// port is in what it announces).
constexpr int kDiscoveryPort = 27016;  // UDP: "who's hosting?" on the local network
constexpr int kBeaconPort = 27017;     // UDP: hosts also announce themselves here every couple of seconds
constexpr int kMaxPeers = 8;
constexpr uint16_t kVersion = 5;  // 5: passwords, a secret in every datagram, strict checks (4: every garage slot in a car's spec)
constexpr double kSendRate = 20;      // vehicle states per second
constexpr double kPlaybackDelay = 0.1; // s behind the newest state received

// What a host puts up with (docs/server-security.md): connections, silence, size.
constexpr int kMaxConns = 24;          // TCP connections held at once, in the session or not
constexpr int kMaxConnsPerIp = 8;      // (a household behind one address can fill the game)
constexpr double kHandshakeSecs = 8;   // to say hello (and give the password) before being dropped
constexpr double kIdleSecs = 30;       // without a word from a player before they're dropped
constexpr size_t kMaxLevelBytes = 8u << 20;  // a level's text
constexpr size_t kMaxNameLen = 16;     // a player's name
constexpr int kMaxCarsPerPlayer = 6;   // vehicles one guest can have in the session
constexpr int kMaxRopesPerPlayer = 6;

// A token bucket: `rate` a second, up to `burst` at once.
struct Bucket {
    double tokens = -1, t = 0;
    bool take(double now, double cost, double rate, double burst) {
        if (tokens < 0) tokens = burst, t = now;
        tokens = std::min(burst, tokens + (now - t) * rate);
        t = now;
        if (tokens < cost) return false;
        tokens -= cost;
        return true;
    }
};

// "28123" -> 28123. -1 unless it's 1 to 5 plain digits making minPort..65535 (no sign, space or junk).
// Joining takes any port (minPort 1); hosting wants kMinHostPort.
int parsePort(const std::string& s, int minPort = 1);
// "HOST" or "HOST:PORT" (a name or an IPv4 address) -> host and port (kDefaultPort with none).
// False, and `why`, if there is no host, a second ':' (IPv6 isn't supported), or a bad port.
bool parseAddress(const std::string& address, std::string& host, int& port, std::string* why = nullptr);
// Text from the network, fit for the HUD font: printable ASCII only, at most `maxLen` long.
std::string sanitize(const std::string& s, size_t maxLen);
// The password a join offers: the command line's if given, else the one typed in the menu.
inline std::string joinPassword(const std::string& fromCommandLine, const std::string& typed) { return fromCommandLine.empty() ? typed : fromCommandLine; }
// HMAC-SHA-256 (the password proof), 32 bytes.
std::vector<uint8_t> hmacSha256(const std::string& key, const std::vector<uint8_t>& data);
}  // namespace net

// Finds games hosted on the local network, two ways, so either direction getting through is
// enough (a firewall often lets only one): it asks "who's hosting?" (a UDP broadcast to the
// whole network, to each network adapter's own subnet, and to this machine) every second and
// lists who answers; and it listens for the hosts' own announcements. Poll it while the
// multiplayer page is open.
namespace net {
struct FoundHost {
    std::string address;  // "ip:port", for join()
    std::string name;     // the host player's name
    std::string session;  // the game's name ("" from an older version: use the host's name)
    int map = 0, players = 0, maxPlayers = kMaxPeers;
    bool locked = false;     // needs a password
    int version = kVersion;  // a different one can't be joined
    uint32_t id = 0;         // the session (the same game heard on two addresses is one row)
    double seen = 0;
    bool joinable() const { return version == kVersion && players < maxPlayers; }
};
class Finder {
public:
    ~Finder();
    void poll(double now);  // opens itself on first use
    void close();
    std::vector<FoundHost> hosts;
private:
    int fd = -1, beaconFd = -1;
    double nextAsk = 0;
    void take(const char* msg, uint32_t fromIp, double now);
};
// This machine's IPv4 addresses on its networks (not loopback), with each one's broadcast
// address: "192.168.8.112" and 192.168.8.255.
struct LocalAddress {
    std::string ip;
    uint32_t addr = 0, broadcast = 0, mask = 0;  // host byte order
};
std::vector<LocalAddress> localAddresses();
}  // namespace net

struct NetPlayer {
    bool active = false;
    std::string name;
    double ping = 0;  // s, round trip to the host (as this machine knows it)
};

class NetSession {
public:
    enum Role { OFF, HOST, CLIENT };
    Role role = OFF;
    int self = -1;    // my player id (the host is 0)
    int map = 0;      // the session's map
    std::vector<NetPlayer> players = std::vector<NetPlayer>(net::kMaxPeers);
    std::vector<std::string> events;  // joins, leaves, errors: for the HUD
    std::string error;
    uint64_t bytesIn = 0, bytesOut = 0;
    // Time of day: the host's clock goes out every couple of seconds so everyone sees the
    // same sky. Host: the game sets these each frame. Client: the host's latest (-1 = none yet).
    double dayHours = -1, dayRate = 0;  // clock hours, and hours per real second (0 = held)
    bool dayFresh = false;              // client: a new one came in
    // Levels: everyone plays the host's level (its whole text goes out, edits and all; "" is
    // the plain map). Client: levelFresh when a new one has come in.
    std::string levelText;
    uint32_t levelSerial = 0;
    bool levelFresh = false;
    // Race chatter (progress, finishes, the host's "go"): short text, relayed to everyone.
    struct GameMsg {
        int player;
        std::string text;
    };
    std::vector<GameMsg> gameIn;

    // A shared secret. Host: if set, guests must prove they know it (an HMAC of a fresh
    // challenge; the password itself never goes over the wire). Client: what to offer. Kept
    // across close(); set it before host() / join().
    std::string password;
    // Client: why a join failed over the password (the menu then asks for it again).
    enum PasswordIssue { PW_OK, PW_NEEDED, PW_WRONG } passwordIssue = PW_OK;
    // Host: also answer "who's hosting?" on the local network and announce itself there. A
    // public server turns this off (it would answer anyone, with more bytes than they asked
    // with); a game on your own network keeps it on. Set before host().
    bool discovery = true;
    // Host: the local address to listen on ("" = every interface).
    std::string bindAddress;
    // Host: what went wrong or was refused (a flood, a wrong password, a malformed message),
    // for a server's log. Newest last, capped.
    std::vector<std::string> log;
    int refused = 0;  // connections turned away or dropped for the limits and for garbage

    NetSession();
    ~NetSession();
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Opens a session on `port` (TCP and UDP) on the given map. `session` is the game's name
    // in other players' lists ("" = "<NAME>'S GAME"); at most `maxPlayers` (2..8) get in.
    bool host(int port, const std::string& name, int map, int maxPlayers = net::kMaxPeers, const std::string& session = "");
    std::string sessionName;     // host: what the others' lists call this game
    int maxPlayers = net::kMaxPeers;
    // Connects to a host and waits (up to `timeout` s) to be let in; `map` then says
    // which map to load. `address` is a host name or IP, optionally "host:port".
    bool join(const std::string& address, const std::string& name, double timeout = 4.0);
    // The same without waiting: beginJoin starts it (false: the address is no good), then
    // pollJoin every frame until it says 1 (in: load `map`, adopt()) or -1 (`error` says why).
    // Don't receive() or send() in between.
    bool beginJoin(const std::string& address, const std::string& name, double timeout = 5.0);
    int pollJoin();
    bool joining() const { return joinStage != 0; }
    void close();
    bool active() const { return role != OFF; }
    int playerCount() const;
    int port() const { return hostPort; }  // host: the game port it listens on (0: not hosting)
    bool portInUse = false;                // host() failed because the port is taken

    // Makes the local sim part of the session. A client keeps only its own vehicle (the
    // host's parked trucks and crash props are the shared ones) and parks it beside the
    // spawn.
    void adopt(Sim& sim);
    // Every frame before the physics: reads the network and applies other players'
    // vehicles, lines and states.
    void receive(Sim& sim, double now);
    // Every frame after the physics: announces new, rebuilt or removed vehicles and lines,
    // and sends vehicle states.
    void send(Sim& sim, double now);

    // Host: everyone loads this level (and anyone who joins later).
    void setLevel(const std::string& text);
    // Sends a line of race chatter to everyone else (it comes out of their gameIn).
    void sendGame(const std::string& text);

    std::string ownerName(uint32_t netId) const;
    static int ownerOf(uint32_t netId) { return (int)(netId >> 16); }

private:
    // One TCP connection: the host's, from a client; or, on a client, to the host.
    struct Conn {
        int fd = -1;
        int peer = -1;
        bool hello = false;
        // Host: a guest that's said something it shouldn't (or been refused) stays only long
        // enough for its last words to go out, hearing nothing.
        bool kill = false;
        double killT = 0;
        bool challenged = false;  // host: waiting for the password's proof
        uint8_t nonce[16] = {};
        std::string name;         // (said in the hello; the guest is let in once the proof is right)
        uint64_t token = 0;       // host: the secret that goes in this guest's datagrams
        uint32_t ip = 0;          // host: where it came from (host byte order)
        double born = 0, lastRx = 0;
        net::Bucket msgs, bytes, spawns, ropes, games, dgrams, dgramBytes;
        std::vector<uint8_t> in, out;
        uint32_t udpIp = 0;       // host: its UDP address, once it has said hello there
        uint16_t udpPort = 0;
        bool udpKnown = false;
        double pingSent = 0;
    };
    friend struct NetSessionTest;     // Socket-free wire/playback regression tests.
    friend struct NetSessionSecTest;  // And the hostile-input ones.
    struct Snap;
    struct Remote;
    int listenFd = -1, udpFd = -1;
    int findFd = -1;            // host: answers "who's hosting?" (net::kDiscoveryPort)
    int hostPort = 0;
    uint32_t sessionId = 0;     // host: random, so one game heard on two addresses lists once
    int connectFd = -1, joinStage = 0;  // joining: 1 connecting, 2 waiting to be let in, 3 taking in the host's vehicles
    double joinT0 = 0, joinQuiet = 0, joinTimeout = 5;
    std::string joinAddress, joinName;
    double nextBeacon = 0;
    void answerFinders(double now);
    std::string hostInfo(bool v2) const;
    std::vector<Conn*> conns;   // host: one per client; client: just the host
    uint64_t udpToken = 0;      // client: the secret the host gave us for datagrams
    struct Strike {
        int fails = 0;
        double until = 0;
    };
    std::map<uint32_t, Strike> strikes;  // host: wrong passwords, by address (host byte order)
    net::Bucket logRate, findRate;       // (a flood must not become a flood of log, or of answers)
    double handshakeSecs = net::kHandshakeSecs, idleSecs = net::kIdleSecs;
    void* hostUdp = nullptr;    // client: the host's UDP address (sockaddr_in)
    bool udpReady = false;      // client: the host has heard our UDP
    double nextUdpHello = 0, nextPing = 0, lastSend = -1, sessionStart = -1;
    uint32_t nextCarId = 1, nextRopeId = 1, seq = 0;
    struct Known {
        uint32_t netId;
        const VehicleSpec* spec;
        int builds;
        uint8_t lamps;
    };
    std::vector<Known> knownCars;       // my vehicles as the others know them
    std::vector<uint32_t> knownRopes;   // lines everyone knows about
    std::vector<Remote*> remotes;
    std::vector<std::vector<uint8_t>> pending;  // reliable messages read before adopt()
    size_t pendingBytes = 0;
    bool adopted = false;

    void accept();
    void readTcp(Conn& c);
    void flush(Conn& c);
    void dropConn(size_t i);
    void sendTo(Conn& c, const std::vector<uint8_t>& msg);
    void broadcast(const std::vector<uint8_t>& msg, Conn* except = nullptr);
    void handle(Sim* sim, Conn* from, const std::vector<uint8_t>& msg);
    // Applies a world message. `from` is the guest it came from (host: its player number; the
    // vehicles and lines in it must be that guest's own), or -1 from the host. False: refused.
    bool apply(Sim& sim, const std::vector<uint8_t>& msg, int from = -1);
    void readUdp(Sim& sim, double now);
    void onDatagram(Sim& sim, const uint8_t* buf, size_t n, uint32_t ip, uint16_t port, double now);
    void admit(Conn& c, const std::string& name);  // host: let a guest in
    void violation(Conn* c, const std::string& why);  // host: drop this guest after garbage
    void note(const std::string& what);
    Conn* fakeConn(int peer, bool hello);  // (tests: a guest without a socket)
    void sendSnapshot(Sim& sim, const Car& c, double now);
    std::vector<uint8_t> snapshotMsg(const Sim& sim, const Car& c, double now) const;
    void takeSnapshot(Sim& sim, uint32_t netId, const std::vector<uint8_t>& data, double now);
    void takeFragment(Sim& sim, uint32_t netId, uint32_t sequence, uint8_t fragment, uint8_t fragments,
                      const uint8_t* data, size_t size, double now);
    void applyRemotes(Sim& sim, double now);
    void welcome(Sim& sim, Conn& c);
    std::vector<uint8_t> spawnMsg(const Car& c) const;
    std::vector<uint8_t> lampMsg(const Car& c) const;
    std::vector<uint8_t> ropeMsg(const Sim& sim, const Rope& r) const;
    void sendLevel(Conn* to);  // the level's text, in parts (to one client, or everyone)
    std::string levelIn;       // client: parts so far
    uint32_t levelInSerial = 0;
    uint16_t levelInNext = 0;  // client: the part we expect next
    Remote* remoteFor(uint32_t netId);
    void removeCars(Sim& sim, int owner);
    Sim* lastSim = nullptr;
};
