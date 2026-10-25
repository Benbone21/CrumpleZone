#ifdef _WIN32
#define _CRT_RAND_S  // (rand_s: the secrets come from the system's generator)
#endif
// Shared by the game's multiplayer and the join-code backend: see netbase.h.
#include "netbase.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

bool parseAddress(const std::string& address, std::string& host, int& port, std::string* why, int defaultPort) {
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
    port = defaultPort;
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

uint64_t random64() {
    uint64_t v = 0;
    while (!v) randomBytes((uint8_t*)&v, sizeof v);
    return v;
}

std::string ipText(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}

UdpHook* udpHook = nullptr;

}  // namespace net
