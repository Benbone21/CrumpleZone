// What the game's multiplayer (net.h) and the join-code backend (rendezvous.h) share, with no
// physics behind it: addresses, cleaning text from the network, HMAC-SHA-256, random secrets,
// a token bucket, and the hook the tests put a simulated network in with.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net {
constexpr int kDefaultPort = 27015;    // TCP and UDP: only the default; a host can use any port from kMinHostPort up
constexpr int kMinHostPort = 1024;     // (below it needs root; hosting there is refused)
constexpr int kMaxPort = 65535;

// "28123" -> 28123. -1 unless it's 1 to 5 plain digits making minPort..65535 (no sign, space or junk).
// Joining takes any port (minPort 1); hosting wants kMinHostPort.
int parsePort(const std::string& s, int minPort = 1);
// "HOST" or "HOST:PORT" (a name or an IPv4 address) -> host and port (`defaultPort` with none).
// False, and `why`, if there is no host, a second ':' (IPv6 isn't supported), or a bad port.
bool parseAddress(const std::string& address, std::string& host, int& port, std::string* why = nullptr, int defaultPort = kDefaultPort);
// Text from the network, fit for the HUD font: printable ASCII only, at most `maxLen` long.
std::string sanitize(const std::string& s, size_t maxLen);
// HMAC-SHA-256, 32 bytes.
std::vector<uint8_t> hmacSha256(const std::string& key, const std::vector<uint8_t>& data);
// From the system's generator (with a fallback that is better than nothing).
void randomBytes(uint8_t* out, size_t n);
uint64_t random64();  // never 0
bool sameBytes(const uint8_t* a, const uint8_t* b, size_t n);  // (in constant time)
double clockNow();  // s, steady
std::string ipText(uint32_t ip);  // host byte order -> "203.0.113.5"

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

// Tests: a simulated network in front of chosen UDP sockets (a NAT, loss). Every UDP send and
// receive in net.cpp and rendezvous.cpp asks it first. Addresses in host byte order.
struct UdpHook {
    virtual ~UdpHook() {}
    // True: the hook took the packet (it is on its way through the simulated network, or lost).
    virtual bool send(int fd, const uint8_t* p, size_t n, uint32_t ip, uint16_t port) = 0;
    // > 0: a packet for `fd` of that size, from ip:port. 0: nothing from the hook, read the real
    // socket. < 0: nothing at all.
    virtual int recv(int fd, uint8_t* buf, size_t cap, uint32_t& ip, uint16_t& port) = 0;
};
extern UdpHook* udpHook;
}  // namespace net
