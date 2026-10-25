// Join codes: the backend that runs on the owner's server box (`crumplezone-rendezvous`), and the
// messages the games speak with it. docs/plans/join-codes.md has the whole design.
//
// A host's game registers and gets a short code. A friend's game looks the code up. The backend
// introduces the two (each gets the other's public address and a shared pair secret) so they
// can punch through their NATs and talk directly. When that fails, it relays their packets,
// only between those two addresses and within limits. It holds no world and writes nothing to
// disk. All of it is UDP, on kDefaultPort.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "netbase.h"

namespace rv {

constexpr uint16_t kMagic = 0xC27C;
constexpr uint8_t kProto = 1;          // this backend protocol (a game and a backend on different ones get FAIL F_PROTO)
constexpr int kDefaultPort = 28015;
constexpr size_t kPad = 128;           // REGISTER, LOOKUP and PING are padded to this: no answer is bigger than its question
constexpr size_t kHeader = 4;          // u16 magic, u8 proto, u8 type
constexpr int kCodeLen = 6;
constexpr int kMaxLocals = 4;
constexpr double kKeepEvery = 10;      // s: a host's keepalive
constexpr double kCodeLife = 45;       // s: a code without a keepalive dies
constexpr double kMaxLife = 12 * 3600; // s: and any code after this (the game registers again)

enum Type : uint8_t {
    RV_REGISTER = 1,  // host: game version, players, max, flags, nonce, locals (padded)
    RV_REGISTERED,    // -> host: nonce, code, host key, its public address, keepalive seconds
    RV_KEEP,          // host: code, host key, players, max, flags
    RV_KEPT,          // -> host: code
    RV_CLOSE,         // host: code, host key, flags (F_RETIRE: no new joiners, keep the pairs; else all gone)
    RV_LOOKUP,        // guest: game version, code, nonce, cookie (0 the first time), locals (padded)
    RV_COOKIE,        // -> guest: nonce, cookie (send the LOOKUP again with it: proves the address is real)
    RV_INTRO,         // -> guest and host: pair, pair secret, relay key, nonce, the other side's public address and locals
    RV_INTRO_ACK,     // host: code, host key, pair
    RV_FAIL,          // -> guest (or host): nonce, reason, the host's version, players, max
    RV_RELAY,         // either side: pair, relay key, payload (forwarded to the other side)
    RV_RELAYED,       // -> the other side: pair, payload
    RV_PING,          // anyone (padded): answered with PONG; the counts only to this machine (loopback)
    RV_PONG,
};
enum Reason : uint8_t { F_NONE, F_NOT_FOUND, F_FULL, F_OLDER, F_NEWER, F_BUSY, F_PROTO, F_GONE };
enum Flags : uint8_t { FL_LOCKED = 1, FL_RETIRE = 2 };

struct Endpoint {
    uint32_t ip = 0;  // host byte order
    uint16_t port = 0;
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
    bool any() const { return ip || port; }
};
std::string endpointText(const Endpoint& e);

// The codes: six of these 30 (no 0 O 1 I L U, nothing two fonts draw alike).
extern const char kAlphabet[];
// Typed or pasted -> the code: case, spaces and dashes don't matter. "" if it isn't six of the alphabet.
std::string normalizeCode(const std::string& typed);
// "KX7M4Q" -> "KX7-M4Q".
std::string formatCode(const std::string& code);
// Whether `c` may be typed into a code (a letter or digit of the alphabet, either case).
bool codeChar(char c);
std::string randomCode();

// Every message, decoded: the fields its type has (see Type).
struct Msg {
    uint8_t proto = kProto, type = 0;
    uint16_t version = 0;
    uint8_t players = 0, maxPlayers = 0, flags = 0, reason = 0;
    uint16_t keepSecs = 0;
    uint64_t nonce = 0, key = 0, cookie = 0, secret = 0;
    uint32_t pair = 0;
    std::string code;
    Endpoint seen;                  // REGISTERED: the host as the backend sees it; INTRO: the other side
    std::vector<Endpoint> locals;   // REGISTER, LOOKUP: this machine's LAN addresses; INTRO: the other side's
    uint32_t stats[8] = {};         // PONG to loopback: codes, pairs, relayed KB, relayed packets, dropped, lookups, registrations, uptime s
    std::vector<uint8_t> payload;   // RELAY, RELAYED
};
std::vector<uint8_t> encode(const Msg& m);
// False: not one of ours, or malformed. A different proto still decodes the header (m.proto says).
bool decode(const uint8_t* p, size_t n, Msg& m);
// RELAY and RELAYED without copying through Msg: the header in front of `payload`.
void relayFrame(std::vector<uint8_t>& out, uint32_t pair, uint64_t key, const uint8_t* payload, size_t n);
// A RELAYED packet: false if it isn't one; else the pair and where its payload is.
bool relayedPayload(const uint8_t* p, size_t n, uint32_t& pair, const uint8_t*& payload, size_t& len);

// What the backend puts up with. The defaults suit a small box; the tool's flags change them.
struct Limits {
    double relayBytesPerSec = 5.0e6;   // all relayed traffic together (40 Mbit/s)
    double pairBytesPerSec = 512 * 1024, pairBurst = 2u << 20;
    double ipRelayBytesPerSec = 2.0e6; // relayed from one address, all its pairs
    int maxCodes = 20000, maxPairs = 50000, codesPerIp = 4, pairsPerCode = 32;
    double registersPerMin = 6, lookupsPerMin = 10, packetsPerSec = 50;  // per address (relay packets aside)
    double codeLife = kCodeLife, maxLife = kMaxLife;
    size_t maxAddresses = 100000;      // addresses tracked for the rate limits
};

class Server {
public:
    Limits lim;
    ~Server();
    // Opens UDP `port` on `bind` ("" every interface). False and `err` if it can't.
    bool open(int port, const std::string& bind = "", std::string* err = nullptr);
    void close();
    bool isOpen() const { return fd >= 0; }
    int port() const { return boundTo; }
    // Waits up to `ms` for a packet (0: doesn't wait), then handles everything waiting and the timers.
    void poll(double now, int ms = 0);
    // One packet, as if it came from `from` (the tests; poll() uses it too).
    void handle(const uint8_t* p, size_t n, const Endpoint& from, double now);

    struct Stats {
        uint64_t registrations = 0, lookups = 0, intros = 0, relayedPackets = 0, relayedBytes = 0, dropped = 0, refused = 0;
    } stats;
    size_t codes() const { return hosts.size(); }
    size_t pairCount() const { return pairs.size(); }
    // Tests: everything it sends goes here instead of the socket.
    struct Sent {
        Endpoint to;
        std::vector<uint8_t> b;
    };
    std::vector<Sent>* outbox = nullptr;
    std::vector<std::string> log;  // what was refused and why (rate limited, newest last, capped): the tool prints and clears it
    double started = -1;

private:
    struct Host {
        std::string code;
        Endpoint at;
        std::vector<Endpoint> locals;
        uint64_t key = 0, nonce = 0;
        uint16_t version = 0;
        uint8_t players = 0, maxPlayers = 0, flags = 0;
        bool retired = false;
        double born = 0, lastKeep = 0;
        std::vector<uint32_t> pairs;  // oldest first
    };
    struct Pair {
        uint32_t id = 0;
        std::string code;
        uint64_t secret = 0, hostKey = 0, guestKey = 0, guestNonce = 0;
        Endpoint host, guest;
        std::vector<Endpoint> guestLocals;
        double born = 0, lastUse = 0, nextIntro = 0;
        int introsLeft = 0;  // INTROs to the host still to send until it acks
        net::Bucket rate;
    };
    struct Addr {
        net::Bucket registers, lookups, packets, relay;
        int codes = 0;
        double seen = 0;
    };
    int fd = -1, boundTo = 0;
    uint8_t cookieKey[32] = {};
    std::map<std::string, Host> hosts;
    std::unordered_map<uint32_t, Pair> pairs;
    std::unordered_map<uint64_t, Addr> addrs;  // by ip (the rate limits are per address, not per port)
    net::Bucket relayAll, logRate;
    double nextSweep = 0, nextSlow = 0;
    void send(const Endpoint& to, const std::vector<uint8_t>& b);
    void reply(const Endpoint& to, size_t asked, const Msg& m);  // only if no bigger than what asked
    void note(const std::string& what, double now);
    Addr& addr(uint32_t ip, double now);
    uint64_t cookieFor(const Endpoint& e, long minute) const;
    void dropHost(std::map<std::string, Host>::iterator it, bool keepPairs);
    void dropPair(uint32_t id);
    void sendIntros(Pair& p, bool toGuest, bool toHost);
    void sweep(double now);
};

}  // namespace rv
