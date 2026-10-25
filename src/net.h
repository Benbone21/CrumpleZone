// Multiplayer: up to 8 players on one map, one of them hosting.
//
// Every machine runs the physics of its own vehicles (so driving has no lag) and sends
// their state, dents and all, about 20 times a second. Other players' vehicles show up as
// "remote" cars: moved by the network, solid to touch, winch to and strap to. A line
// between two players' trucks pulls on each truck on its owner's machine.
//
// The host relays everything and picks the map. Reliable messages (joins, spawns, lines)
// go over TCP; vehicle states over UDP. Plain POSIX sockets, nothing else.
//
// One clock for everyone: the host's (seconds since it opened the session). Guests learn
// how far theirs is from it with pings, NTP style. Every state, line and chatter line is
// stamped with that clock, and each machine shows the others a little behind it (the delay
// follows the measured latency and jitter), so a remote truck is where it was at the same
// moment on every screen, and what was said or done then happens then too.
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "netbase.h"
#include "softbody.h"

namespace net {
// The two LAN-only ports are fixed (every game and finder must agree on them: a host's real game
// port is in what it announces).
constexpr int kDiscoveryPort = 27016;  // UDP: "who's hosting?" on the local network
constexpr int kBeaconPort = 27017;     // UDP: hosts also announce themselves here every couple of seconds
constexpr int kMaxPeers = 8;
constexpr uint16_t kVersion = 8;  // 8 (0.4.1): vehicles shown now, not a view delay behind (docs/plans/net-collisions.md), each side says its build; join codes (a reliable stream over UDP for guests that joined with one), the park's edited sections in the welcome (MSG_SECTIONS), a link byte in MSG_RTT, REJECT after joining (kicked, host gone); 7: taking over someone else's empty vehicle (MSG_TAKE / GIVE / REFUSE), a driver-in-it bit in states; 6: one clock (stamped states, chatter and lines; the sky) (5: passwords, a secret in every datagram, strict checks)
#ifndef CZ_RENDEZVOUS
#define CZ_RENDEZVOUS "gigachad.website:28015"  // the owner's join-code server (docs/server-rendezvous-setup.md); settings.txt `rendezvous` and --rendezvous override it
#endif
constexpr const char* kDefaultRendezvous = CZ_RENDEZVOUS;  // the join-code server a game uses unless told another ("": none: codes aren't offered)
constexpr const char* kRelease = "0.4.2";  // the first release that speaks kVersion (what an older guest is told to update to)
constexpr double kSendRate = 20;      // vehicle states per second
constexpr double kPlaybackDelay = 0.1; // s: the least the others are shown behind the shared clock (two states)
constexpr double kMaxDelay = 1.0;      // s: and the most, however bad the line
constexpr double kMaxCoast = 0.25;     // s: a vehicle whose states stop coming coasts this long, then holds
constexpr double kMaxLead = 0.5;       // s: shown now (NetSession::predict), how far a vehicle is run on past its newest state, at most
constexpr int kDentStep = 2;           // mm: a dent that moved less than this since the receiver's state isn't sent again
constexpr int kStreamKeep = 32;        // states a sender remembers per receiver (1.6 s): an answer older than that gets a whole one

// What a host puts up with (docs/server-security.md): connections, silence, size.
constexpr int kMaxConns = 24;          // TCP connections held at once, in the session or not
constexpr int kMaxConnsPerIp = 8;      // (a household behind one address can fill the game)
constexpr double kHandshakeSecs = 8;   // to say hello (and give the password) before being dropped
constexpr double kIdleSecs = 30;       // without a word from a player before they're dropped
constexpr size_t kMaxLevelBytes = 8u << 20;  // a level's text
constexpr size_t kMaxNameLen = 16;     // a player's name
constexpr int kMaxCarsPerPlayer = 6;   // vehicles one guest can have in the session
constexpr int kMaxRopesPerPlayer = 6;

// The password a join offers: the command line's if given, else the one typed in the menu.
inline std::string joinPassword(const std::string& fromCommandLine, const std::string& typed) { return fromCommandLine.empty() ? typed : fromCommandLine; }

// A guest's estimate of the host's clock, from pings: each answer says what the host's clock
// read when it answered. The one with the shortest round trip of the last few is the truest
// (it waited least in queues on the way); the estimate moves toward it.
struct ClockSync {
    double offset = 0;  // host's clock minus mine
    bool synced = false;
    int samples = 0;
    // A ping sent at `sent` (my clock), answered with the host's `hostT`, back at `got`. False: thrown away.
    bool add(double sent, double hostT, double got);
    double host(double mine) const { return mine + offset; }
    double rtt() const;  // the shortest of the recent round trips (s)
private:
    struct Sample {
        double rtt, offset;
    };
    std::deque<Sample> recent;
};

// How late one sender's states arrive (by the shared clock), and how far apart they are.
struct Lateness {
    double mean = -1, dev = 0, gap = 1.0 / kSendRate;
    void add(double late, double sinceLast);
    // The delay that leaves this sender's next state, or the one after it if one is lost, in
    // hand when it's needed.
    double need() const { return mean < 0 ? kPlaybackDelay : std::min(kMaxDelay, std::max(kPlaybackDelay, mean + 2 * gap + 2.5 * dev)); }
};

// The moment other players are shown at: the shared clock less a delay. It follows the
// delay the senders need (up at once, down slowly), but never runs backwards and changes
// speed gently, so remote vehicles neither jump nor stall.
struct Timeline {
    double delay = kPlaybackDelay;  // what it aims for
    double render = 0, at = -1;     // the shown moment, and the shared time when it was set (-1: not yet)
    void step(double shared, double need);
    bool started() const { return at >= 0; }
};
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
    int link = 0;     // how it reaches the host: NetSession::Link
};

namespace rv {
struct Endpoint;
}

class NetSession {
public:
    enum Role { OFF, HOST, CLIENT };
    enum Link { LINK_ADDRESS, LINK_DIRECT, LINK_RELAYED };  // joined by address (TCP); by code, punched through; by code, through the relay
    Role role = OFF;
    int self = -1;    // my player id (the host is 0)
    int map = 0;      // the session's map
    std::vector<NetPlayer> players = std::vector<NetPlayer>(net::kMaxPeers);
    std::vector<std::string> events;  // joins, leaves, errors: for the HUD
    std::string error;
    uint64_t bytesIn = 0, bytesOut = 0;
    // Time of day: the host's goes out on joining and every second, stamped with the shared
    // clock, so everyone sees the same sky. Host: the game (or the server) sets these each
    // frame. Client: the host's, run on to now every frame (-1 = none heard yet).
    double dayHours = -1, dayRate = 0;  // clock hours, and hours per real second (0 = held)
    bool dayFresh = false;              // client: a new one came in

    // The shared clock (the host's seconds since it opened). `now` is the caller's clock, as
    // given to receive() and send().
    double sharedTime(double now) const;
    bool clockSynced() const { return role != CLIENT || clock.synced; }
    double clockOffset() const { return clock.offset; }  // client: the host's clock minus mine
    // Where the others are shown: this far behind the shared clock (s), at that moment.
    double viewDelay() const { return view.started() ? view.delay : net::kPlaybackDelay; }
    double viewTime() const { return view.render; }
    // Vehicles, though, are shown now: each run on from its newest state (its owner's truck is
    // where it is, not where it was, so two players' crash happens at the same place on both
    // machines; docs/plans/net-collisions.md). False: at the view, like the rest (as before 0.4.1).
    bool predict = true;
    double vehicleTime() const { return vehicleAt; }  // the moment the vehicles were last shown at (shared clock)
    double vehicleLead() const { return vehicleLeadNow; }  // how far past its newest state the furthest was run on (s): about the latency
    // Levels: everyone plays the host's level (its whole text goes out, edits and all; "" is
    // the plain map). Client: levelFresh when a new one has come in.
    std::string levelText;
    uint32_t levelSerial = 0;
    bool levelFresh = false;
    // The proving ground's park sections the host built from its own edits (World::editedSections):
    // host: set them (setSections), whoever joins gets them before building its world; client: what
    // came (World::setSessionSections before loading the map).
    std::vector<std::pair<int, std::string>> sections;
    void setSections(const std::vector<std::pair<int, std::string>>& s) { sections = s; }
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
    // This game's build (the git commit): said on joining, both ways. A guest on another build is
    // let in (the same protocol), but everyone's told: the physics can differ between builds, so
    // a truck can handle differently on its owner's machine than on yours. "" = don't say.
    std::string build;
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

    // ---- Join codes (docs/plans/join-codes.md) ----
    // The backend ("host" or "host:port", rv::kDefaultPort with none). Set before openToFriends() / beginJoinCode().
    std::string rendezvous;
    // Host: gets this session a join code from the backend (it comes in a moment: codeState).
    // False: not hosting, or no backend set or found (codeError says).
    bool openToFriends();
    // Host: the code stops working; whoever joined with it stays.
    void closeToFriends();
    enum CodeState { CODE_OFF, CODE_ASKING, CODE_OPEN, CODE_FAILED };
    CodeState codeState = CODE_OFF;
    std::string joinCode;   // host: "KX7M4Q" while CODE_OPEN (rv::formatCode shows it KX7-M4Q)
    std::string codeError;  // host: why CODE_FAILED, in plain words
    // Client: joins the game with that code, as beginJoin() does an address: then pollJoin() every frame.
    bool beginJoinCode(const std::string& code, const std::string& name, double timeout = 12.0);
    bool joinWithCode(const std::string& code, const std::string& name, double timeout = 12.0);  // (waiting: the tests)
    enum JoinStep { JS_NONE, JS_FINDING, JS_CONNECTING, JS_RELAY, JS_HELLO, JS_LOADING };
    JoinStep joinStep() const;  // while joining by code: what's happening (the joining card says it)
    // Why the last join failed, for the card's plain words (error has the details).
    enum JoinFail { JF_NONE, JF_NOT_A_CODE, JF_NOT_FOUND, JF_FULL, JF_OLDER, JF_NEWER, JF_BUSY, JF_BACKEND, JF_NO_PATH, JF_PASSWORD, JF_OTHER };
    JoinFail joinFail = JF_NONE;
    int failPlayers = 0, failMax = 0;  // JF_FULL: how full
    Link myLink() const;               // client: how this machine reaches the host
    // Host: removes a player (they're told, and kept out for two minutes).
    void kick(int peer);
    // Tests and odd networks: don't offer this machine's LAN addresses to the backend; never
    // punch (straight to the relay); how long to try punching before the relay.
    bool shareLocals = true, forceRelay = false;
    double punchSecs = 2.5;

    std::string ownerName(uint32_t netId) const;
    static int ownerOf(uint32_t netId) { return (int)(netId >> 16); }

    // Getting into someone else's vehicle with nobody in it (a buddy's truck while they're on
    // foot, the host's parked trucks and stopped traffic): it's handed over. Its owner decides
    // (it's the one that knows nobody's in it) and the host settles who asked first; then on
    // every machine it keeps its body, dents and all, under an id of the new owner's, and is
    // simulated and sent by them. The way back is the same. A player who leaves with someone
    // else's vehicle leaves it behind: the host takes it on (parked).
    // Asks for sim.cars[car] (another player's). False: not one that can be asked for, or asking already.
    bool requestTake(Sim& sim, int car);
    bool takePending(uint32_t oldId) const { for (const PendingTake& t : takes) if (t.oldId == oldId) return true; return false; }  // host: someone's ask for this vehicle is out with its owner
    uint32_t asking() const { return askId; }  // the vehicle asked for (0: none)
    // Owner: whether one of mine may go now (the game: nobody's driving it). Unset: whenever it's
    // stopped. (Either way, never while it's moving.)
    std::function<bool(const Sim&, int car)> mayGive;
    struct CarEvent {
        enum Kind { GOT, REFUSED, GAVE, RENAMED } kind;
        CarEvent(Kind k = GOT) : kind(k) {}
        uint32_t oldId = 0, newId = 0;  // (REFUSED: what was asked for)
        bool asked = false;             // GOT: the one we asked for (else the host taking on a leaver's)
        std::string why;                // REFUSED
    };
    std::vector<CarEvent> carEvents;  // what happened to vehicles changing hands, for the game (it clears them)

private:
    // One vehicle's states to one receiver: what was sent lately (the dents as the receiver
    // will have them), so the next can say only what changed since the newest it has heard.
    struct Stream {
        uint32_t netId = 0;
        struct Sent {
            uint32_t seq;
            std::vector<int16_t> q;
        };
        std::deque<Sent> sent;
        uint32_t acked = 0;     // the newest the receiver says it has (0: none)
        double lastStamp = -1;  // (shared clock) of the last one sent: far vehicles go less often
    };
    static Stream& streamFor(std::vector<Stream>& v, uint32_t netId);
    // One TCP connection: the host's, from a client; or, on a client, to the host.
    struct Tunnel;  // a guest that joined by code: its path (punched or relayed) and its reliable stream
    struct Conn {
        ~Conn();
        Tunnel* tun = nullptr;  // (then fd is -1: the bytes go over UDP)
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
        std::string build;        // (and its build, if it said)
        uint64_t token = 0;       // host: the secret that goes in this guest's datagrams
        uint32_t ip = 0;          // host: where it came from (host byte order)
        double born = 0, lastRx = 0;
        net::Bucket msgs, bytes, spawns, ropes, games, dgrams, dgramBytes;
        std::vector<uint8_t> in, out;
        std::vector<Stream> streams;  // host: every vehicle's states to this guest
        double walkSent[net::kMaxPeers] = {};  // host: when each player on foot was last told to it (0: never)
        std::vector<std::pair<uint32_t, uint32_t>> acks;  // host: states of this guest's vehicles to say we have (id, seq)
        uint32_t udpIp = 0;       // host: its UDP address, once it has said hello there
        uint16_t udpPort = 0;
        bool udpKnown = false;
        double pingSent = 0;
    };
    friend struct NetSessionTest;     // Socket-free wire/playback regression tests.
    friend struct NetSessionSecTest;  // And the hostile-input ones.
    friend struct NetSessionCodeTest;  // And the join-code ones (a simulated NAT).
    friend struct NetSessionTimeTest;  // And the clock, timeline and delta ones (simulated latency and loss).
    struct Snap;
    struct Remote;
    net::ClockSync clock;       // client: the host's clock
    net::Timeline view;         // where the others are shown
    double curNow = 0;          // the caller's clock this frame (what receive() was given)
    double vehicleAt = -1;      // (vehicleTime)
    double vehicleLeadNow = 0;  // (vehicleLead)
    double lastPlayNow = -1e18; // (the caller's clock when they were last placed)
    double nextSync = 0;        // client: the next clock ping
    double dayStamp = 0, dayBase = -1;  // client: the host's hours at that shared moment
    double daySentAt = -1, daySentHours = 0;  // host: the last clock sent
    void sendClock(Conn* to);   // host: the sky, to one guest (or everyone)
    // Held until the shown moment reaches their stamp: chatter, and (on a guest) the world's
    // messages behind a stamped one (in order).
    double daySentRate = 0;
    void runDay();              // client: the host's hours, run on to now
    struct Held {
        double due;
        std::vector<uint8_t> msg;  // a world message (heldWorld), or
        int player;                // chatter (heldGame)
        std::string text;
    };
    std::deque<Held> heldGame, heldWorld;
    void hold(Sim* sim, std::deque<Held>& q, double due, std::vector<uint8_t> msg, int player, const std::string& text);
    void deliver(Sim* sim, Held& h, bool world);
    void release(Sim* sim);
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
    bool live(const Conn& c) const;  // its socket or tunnel is still open
    // Join codes.
    struct Intro {  // host: a guest the backend introduced (punching towards it, then its tunnel)
        uint32_t pair = 0;
        uint64_t secret = 0, relayKey = 0;
        uint32_t pubIp = 0;
        uint16_t pubPort = 0;
        std::vector<std::pair<uint32_t, uint16_t>> cands;  // where to punch (its public address, its LAN ones)
        double born = 0, nextPunch = 0;
    };
    std::vector<Intro> intros;
    Intro codeIntro;                   // client: ours, joining by code
    uint32_t rvIp = 0;                 // the backend
    uint16_t rvPort = 0;
    uint64_t rvKey = 0, rvNonce = 0;   // host: the code's key; the registration's / the lookup's nonce
    uint64_t rvCookie = 0;             // client: the backend's proof of our address
    uint64_t rvOldKey = 0;             // host: a code closed to friends while guests that came by it are still in (kept alive for their relay)
    double rvOldNext = 0;
    int keptAs = -1;                   // host: players, max and locked as the backend last heard (a change is told at once)
    double rvNext = 0, rvHeard = 0, rvLastKeep = 0, punchT0 = 0, nextPunch = 0;
    int rvTries = 0;
    bool rvRetired = false;            // host: closed to friends, but keeping the code's pairs for the guests in
    std::string codeWanted;            // client: the code being joined (host: the retired code, while its guests stay)
    uint32_t directIp = 0;             // client: the host's address the punch found (0: relay)
    uint16_t directPort = 0;
    bool directFound = false;
    std::string lostWhy;               // client: why the host is gone (said in the events)
    bool resolveRendezvous(std::string& why);
    void pumpUdp(double now);  // reads our UDP socket: tunnels and the backend at once, game datagrams held for readUdp
    struct HeldDgram {
        std::vector<uint8_t> b;
        uint32_t ip;
        uint16_t port;
        uint32_t viaPair;  // came through the relay for this pair (0: straight from ip:port)
    };
    std::vector<HeldDgram> udpHeld;
    void onPacket(const uint8_t* p, size_t n, uint32_t ip, uint16_t port, double now, uint32_t relayedPair);
    void onTunnel(const uint8_t* p, size_t n, uint32_t ip, uint16_t port, double now, uint32_t relayedPair);
    Conn* acceptTunnel(const Intro& in, double now);  // host: a guest by code's first bytes: let it in (within the limits)
    void onBackend(const uint8_t* p, size_t n, double now);
    void serviceRendezvous(double now);
    void serviceTunnels(double now);
    void tunRaw(Tunnel& t, const uint8_t* p, size_t n);   // a packet on the tunnel's path (direct, or wrapped for the relay)
    void tunPacket(Tunnel& t, uint8_t type, const uint8_t* body, size_t n);
    void sendDgram(Conn* to, const uint8_t* p, size_t n);  // a game datagram to a guest (or, null, to the host), on its path
    void punchTo(uint32_t pair, uint64_t secret, uint8_t type, uint32_t ip, uint16_t port);
    Conn* tunnelConn(uint32_t pair);
    void sendBackend(const std::vector<uint8_t>& b);
    void registerCode(double now);
    int pollCodeJoin(double now);  // joining by code, stages 10 (asking the backend) and 11 (punching): -1 failed
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
    void onDatagram(Sim* sim, const uint8_t* buf, size_t n, uint32_t ip, uint16_t port, double now, Tunnel* via = nullptr);
    void admit(Conn& c, const std::string& name);  // host: let a guest in
    void violation(Conn* c, const std::string& why);  // host: drop this guest after garbage
    void note(const std::string& what);
    Conn* fakeConn(int peer, bool hello);  // (tests: a guest without a socket)
    void sendSnapshot(Sim& sim, const Car& c, double now);
    Snap makeSnap(const Sim& sim, const Car& c, double now) const;
    // A state on the wire: whole, or (with `base`, a state the receiver has) only the dents
    // that moved more than net::kDentStep from it. `sentQ`: the dents as the receiver will have them.
    static std::vector<uint8_t> encodeState(const Snap& s, const Stream::Sent* base, std::vector<int16_t>& sentQ);
    std::vector<uint8_t> snapshotMsg(const Sim& sim, const Car& c, double now) const;  // (whole: tests)
    void sendState(Conn* to, Stream& st, const Snap& s, uint32_t netId);  // to a guest, or (null) to the host
    std::vector<uint8_t> nextState(Stream& st, const Snap& s);  // (the next one on that stream, numbered `seq`)
    std::vector<uint8_t> nextStateOf(Stream& st, const Sim& sim, const Car& c, double now);  // (tests: the same for one of my vehicles)
    std::vector<int16_t> dentsOf(const Sim& sim, const Car& c) const;
    const std::vector<int16_t>* lastTaken(uint32_t& sequence) const;  // (tests: the dents of the newest state taken, and its number)
    bool worthTelling(Conn& to, int fromPeer, const std::string& text);
    void sendDatagrams(Conn* to, uint32_t netId, const std::vector<uint8_t>& data);
    void relayState(Sim& sim, uint32_t netId, const Snap& s);  // host: a guest's state, on to the others it matters to
    double relevantEvery(const Sim& sim, const Conn& to, const V3& at) const;  // host: how often a vehicle there is worth telling `to` about
    std::vector<Stream> upStreams;  // client: my vehicles' states to the host
    std::vector<std::pair<uint32_t, uint32_t>> upAcks;  // client: states the host sent that we have
    void sendAcks();
    void takeAcks(std::vector<Stream>& v, const uint8_t* p, size_t n);
    void takeSnapshot(Sim& sim, uint32_t netId, const std::vector<uint8_t>& data, double now, uint32_t sequence = 0);
    void takeFragment(Sim& sim, uint32_t netId, uint32_t sequence, uint8_t fragment, uint8_t fragments,
                      const uint8_t* data, size_t size, double now);
    void applyRemotes(Sim& sim, double now);
    void welcome(Sim& sim, Conn& c);
    std::vector<uint8_t> spawnMsg(const Car& c) const;
    std::vector<uint8_t> lampMsg(const Car& c) const;
    std::vector<uint8_t> ropeMsg(const Sim& sim, const Rope& r) const;
    void sendLevel(Conn* to);  // the level's text, in parts (to one client, or everyone)
    void sendSections(Conn& to);
    std::vector<std::pair<int, std::string>> sectionsIn;  // client: coming in
    int sectionsNext = 0;      // client: the part expected next (index << 16 | part); -1: lost the thread
    bool sectionsPending = false;
    std::string levelIn;       // client: parts so far
    uint32_t levelInSerial = 0;
    uint16_t levelInNext = 0;  // client: the part we expect next
    Remote* remoteFor(uint32_t netId);
    void removeCars(Sim& sim, int owner);
    Sim* lastSim = nullptr;
    // Handing vehicles over.
    uint32_t askId = 0, askNew = 0;  // what I asked for, and the id it's to have
    double askAt = 0;
    struct PendingTake {  // host: asked of an owner, not answered yet
        uint32_t oldId, newId;
        int taker;
        double at;
        bool takerGone = false;
    };
    std::vector<PendingTake> takes;
    std::map<uint32_t, int> homeOf;  // host: vehicles handed over: whose they were first
    uint32_t ridingOf[net::kMaxPeers] = {};  // host: the vehicle each player rides along in (chatter "ride ID"), for what's near them
    std::string whyNot(const Sim& sim, int car) const;  // owner: "" if it may go now
    void giveAway(Sim& sim, int car, uint32_t newId);   // owner: mine goes to whoever newId says
    bool rekey(Sim& sim, uint32_t oldId, uint32_t newId, bool asked);  // a remote one changes hands (to me, or between others)
    void refuseTake(int taker, uint32_t oldId, uint32_t newId, const std::string& why);  // host: tell the taker no
    void hostTake(Sim& sim, Conn* from, uint32_t oldId, uint32_t newId);  // host: someone asks (from null: me)
    void hostGiven(Sim& sim, uint32_t oldId, uint32_t newId, Conn* except);  // host: it went: everyone hears
    void hostReclaim(Sim& sim, int car);  // host: a leaver's vehicle that was someone else's is mine now
    void dropTakes(Sim* sim, int peer);   // host: a player left: their asks and answers
    void expireTakes(double now);
    static std::vector<uint8_t> takeMsg(uint8_t type, uint32_t oldId, uint32_t newId, const std::string& why = "");
};
