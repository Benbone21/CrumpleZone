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
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "softbody.h"

namespace net {
constexpr int kDefaultPort = 27015;
constexpr int kDiscoveryPort = 27016;  // UDP: "who's hosting?" on the local network
constexpr int kBeaconPort = 27017;     // UDP: hosts also announce themselves here every couple of seconds
constexpr int kMaxPeers = 8;
constexpr uint16_t kVersion = 3;  // engine state and reliable per-car light switches
constexpr double kSendRate = 20;      // vehicle states per second
constexpr double kPlaybackDelay = 0.1; // s behind the newest state received
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
    uint32_t addr = 0, broadcast = 0;  // host byte order
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
    struct Conn;
    friend struct NetSessionTest;  // Socket-free wire/playback regression tests.
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
    bool adopted = false;

    void accept();
    void readTcp(Conn& c);
    void flush(Conn& c);
    void dropConn(size_t i);
    void sendTo(Conn& c, const std::vector<uint8_t>& msg);
    void broadcast(const std::vector<uint8_t>& msg, Conn* except = nullptr);
    void handle(Sim* sim, Conn* from, const std::vector<uint8_t>& msg);
    void apply(Sim& sim, const std::vector<uint8_t>& msg);
    void readUdp(Sim& sim, double now);
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
    Remote* remoteFor(uint32_t netId);
    void removeCars(Sim& sim, int owner);
    Sim* lastSim = nullptr;
};
