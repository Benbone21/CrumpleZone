// Join codes: the messages and the backend. See rendezvous.h and docs/plans/join-codes.md.
#include "rendezvous.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "netsock.h"

namespace rv {

const char kAlphabet[] = "ABCDEFGHJKMNPQRSTVWXYZ23456789";

std::string endpointText(const Endpoint& e) { return net::ipText(e.ip) + ":" + std::to_string(e.port); }

bool codeChar(char c) {
    const char u = (char)std::toupper((unsigned char)c);
    return u && std::strchr(kAlphabet, u) != nullptr;
}

std::string normalizeCode(const std::string& typed) {
    std::string c;
    for (char ch : typed) {
        if (ch == ' ' || ch == '-' || ch == '\t') continue;
        if (!codeChar(ch)) return "";
        c += (char)std::toupper((unsigned char)ch);
        if (c.size() > (size_t)kCodeLen) return "";
    }
    return c.size() == (size_t)kCodeLen ? c : "";
}

std::string formatCode(const std::string& code) {
    if (code.size() != (size_t)kCodeLen) return code;
    return code.substr(0, 3) + "-" + code.substr(3);
}

std::string randomCode() {
    const size_t n = sizeof kAlphabet - 1;  // 30
    std::string c;
    while ((int)c.size() < kCodeLen) {
        uint8_t b = 0;
        net::randomBytes(&b, 1);
        if (b < 240) c += kAlphabet[b % n];  // (240 = 8 x 30: no symbol more likely than another)
    }
    return c;
}

// ---- The wire --------------------------------------------------------------------------

namespace {
struct W {
    std::vector<uint8_t> b;
    template <class T> void put(T v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
    }
    void code(const std::string& c) {
        for (int i = 0; i < kCodeLen; i++) b.push_back(i < (int)c.size() ? (uint8_t)c[i] : (uint8_t)'?');
    }
    void ep(const Endpoint& e) { put<uint32_t>(e.ip), put<uint16_t>(e.port); }
    void locals(const std::vector<Endpoint>& l) {
        const size_t n = std::min(l.size(), (size_t)kMaxLocals);
        put<uint8_t>((uint8_t)n);
        for (size_t i = 0; i < n; i++) ep(l[i]);
    }
};
struct R {
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
    std::string code() {
        if (at + kCodeLen > n) {
            ok = false;
            return "";
        }
        std::string c((const char*)p + at, kCodeLen);
        at += kCodeLen;
        for (char ch : c)
            if (!std::strchr(kAlphabet, ch) || !ch) ok = false;
        return c;
    }
    Endpoint ep() {
        Endpoint e;
        e.ip = get<uint32_t>();
        e.port = get<uint16_t>();
        return e;
    }
    std::vector<Endpoint> locals() {
        const int k = get<uint8_t>();
        std::vector<Endpoint> l;
        if (k > kMaxLocals) {
            ok = false;
            return l;
        }
        for (int i = 0; i < k; i++) l.push_back(ep());
        return l;
    }
};
}  // namespace

std::vector<uint8_t> encode(const Msg& m) {
    W w;
    w.put<uint16_t>(kMagic);
    w.put<uint8_t>(m.proto);
    w.put<uint8_t>(m.type);
    bool pad = false;
    switch (m.type) {
        case RV_REGISTER:
            w.put<uint16_t>(m.version), w.put<uint8_t>(m.players), w.put<uint8_t>(m.maxPlayers), w.put<uint8_t>(m.flags);
            w.put<uint64_t>(m.nonce), w.locals(m.locals);
            pad = true;
            break;
        case RV_REGISTERED:
            w.put<uint64_t>(m.nonce), w.code(m.code), w.put<uint64_t>(m.key), w.ep(m.seen), w.put<uint16_t>(m.keepSecs);
            break;
        case RV_KEEP: w.code(m.code), w.put<uint64_t>(m.key), w.put<uint8_t>(m.players), w.put<uint8_t>(m.maxPlayers), w.put<uint8_t>(m.flags); break;
        case RV_KEPT: w.code(m.code); break;
        case RV_CLOSE: w.code(m.code), w.put<uint64_t>(m.key), w.put<uint8_t>(m.flags); break;
        case RV_LOOKUP:
            w.put<uint16_t>(m.version), w.code(m.code), w.put<uint64_t>(m.nonce), w.put<uint64_t>(m.cookie), w.locals(m.locals);
            pad = true;
            break;
        case RV_COOKIE: w.put<uint64_t>(m.nonce), w.put<uint64_t>(m.cookie); break;
        case RV_INTRO:
            w.put<uint32_t>(m.pair), w.put<uint64_t>(m.secret), w.put<uint64_t>(m.key), w.put<uint64_t>(m.nonce), w.ep(m.seen), w.locals(m.locals);
            break;
        case RV_INTRO_ACK: w.code(m.code), w.put<uint64_t>(m.key), w.put<uint32_t>(m.pair); break;
        case RV_FAIL:
            w.put<uint64_t>(m.nonce), w.put<uint8_t>(m.reason), w.put<uint16_t>(m.version), w.put<uint8_t>(m.players), w.put<uint8_t>(m.maxPlayers);
            break;
        case RV_RELAY: w.put<uint32_t>(m.pair), w.put<uint64_t>(m.key), w.b.insert(w.b.end(), m.payload.begin(), m.payload.end()); break;
        case RV_RELAYED: w.put<uint32_t>(m.pair), w.b.insert(w.b.end(), m.payload.begin(), m.payload.end()); break;
        case RV_PING: pad = true; break;
        case RV_PONG:
            for (uint32_t s : m.stats) w.put<uint32_t>(s);
            break;
        default: break;
    }
    if (pad && w.b.size() < kPad) w.b.resize(kPad, 0);
    return w.b;
}

bool decode(const uint8_t* p, size_t n, Msg& m) {
    R r{p, n};
    if (r.get<uint16_t>() != kMagic) return false;
    m = Msg();
    m.proto = r.get<uint8_t>();
    m.type = r.get<uint8_t>();
    if (!r.ok) return false;
    if (m.proto != kProto) return true;  // (the caller says it's a different proto)
    switch (m.type) {
        case RV_REGISTER:
            m.version = r.get<uint16_t>(), m.players = r.get<uint8_t>(), m.maxPlayers = r.get<uint8_t>(), m.flags = r.get<uint8_t>();
            m.nonce = r.get<uint64_t>(), m.locals = r.locals();
            break;
        case RV_REGISTERED:
            m.nonce = r.get<uint64_t>(), m.code = r.code(), m.key = r.get<uint64_t>(), m.seen = r.ep(), m.keepSecs = r.get<uint16_t>();
            break;
        case RV_KEEP: m.code = r.code(), m.key = r.get<uint64_t>(), m.players = r.get<uint8_t>(), m.maxPlayers = r.get<uint8_t>(), m.flags = r.get<uint8_t>(); break;
        case RV_KEPT: m.code = r.code(); break;
        case RV_CLOSE: m.code = r.code(), m.key = r.get<uint64_t>(), m.flags = r.get<uint8_t>(); break;
        case RV_LOOKUP:
            m.version = r.get<uint16_t>(), m.code = r.code(), m.nonce = r.get<uint64_t>(), m.cookie = r.get<uint64_t>(), m.locals = r.locals();
            break;
        case RV_COOKIE: m.nonce = r.get<uint64_t>(), m.cookie = r.get<uint64_t>(); break;
        case RV_INTRO:
            m.pair = r.get<uint32_t>(), m.secret = r.get<uint64_t>(), m.key = r.get<uint64_t>(), m.nonce = r.get<uint64_t>(), m.seen = r.ep(), m.locals = r.locals();
            break;
        case RV_INTRO_ACK: m.code = r.code(), m.key = r.get<uint64_t>(), m.pair = r.get<uint32_t>(); break;
        case RV_FAIL:
            m.nonce = r.get<uint64_t>(), m.reason = r.get<uint8_t>(), m.version = r.get<uint16_t>(), m.players = r.get<uint8_t>(), m.maxPlayers = r.get<uint8_t>();
            break;
        case RV_RELAY:
            m.pair = r.get<uint32_t>(), m.key = r.get<uint64_t>();
            if (r.ok) m.payload.assign(p + r.at, p + n);
            break;
        case RV_RELAYED:
            m.pair = r.get<uint32_t>();
            if (r.ok) m.payload.assign(p + r.at, p + n);
            break;
        case RV_PING: break;
        case RV_PONG:
            for (uint32_t& s : m.stats) s = r.get<uint32_t>();
            break;
        default: return false;
    }
    return r.ok;
}

void relayFrame(std::vector<uint8_t>& out, uint32_t pair, uint64_t key, const uint8_t* payload, size_t n) {
    out.resize(kHeader + 12 + n);
    uint8_t* q = out.data();
    const uint16_t magic = kMagic;
    std::memcpy(q, &magic, 2);
    q[2] = kProto, q[3] = RV_RELAY;
    std::memcpy(q + 4, &pair, 4);
    std::memcpy(q + 8, &key, 8);
    if (n) std::memcpy(q + 16, payload, n);
}

bool relayedPayload(const uint8_t* p, size_t n, uint32_t& pair, const uint8_t*& payload, size_t& len) {
    if (n < kHeader + 4) return false;
    uint16_t magic;
    std::memcpy(&magic, p, 2);
    if (magic != kMagic || p[2] != kProto || p[3] != RV_RELAYED) return false;
    std::memcpy(&pair, p + 4, 4);
    payload = p + 8;
    len = n - 8;
    return true;
}

// ---- The backend -----------------------------------------------------------------------

Server::~Server() { close(); }

bool Server::open(int port, const std::string& bind, std::string* err) {
    close();
    uint32_t ip = 0;
    if (!bind.empty()) {
        in_addr a{};
        if (inet_pton(AF_INET, bind.c_str(), &a) != 1) {
            if (err) *err = "can't listen on " + bind + " (an IPv4 address)";
            return false;
        }
        ip = ntohl(a.s_addr);
    }
    fd = udpOpen(ip, (uint16_t)port);
    if (fd < 0) {
        if (err) *err = "can't open UDP port " + std::to_string(port) + (addrInUse() ? ": in use" : "");
        return false;
    }
    boundTo = boundPort(fd);
    net::randomBytes(cookieKey, sizeof cookieKey);
    return true;
}

void Server::close() {
    if (fd >= 0) closeSock(fd);
    fd = -1;
    hosts.clear();
    pairs.clear();
    addrs.clear();
}

void Server::note(const std::string& what, double now) {
    if (!logRate.take(now, 1, 2, 10)) return;
    log.push_back(what);
    if (log.size() > 64) log.erase(log.begin(), log.begin() + (long)(log.size() - 64));
}

void Server::send(const Endpoint& to, const std::vector<uint8_t>& b) {
    if (outbox) outbox->push_back({to, b});
    else if (fd >= 0) udpSend(fd, b.data(), b.size(), to.ip, to.port);
}

void Server::reply(const Endpoint& to, size_t asked, const Msg& m) {
    const std::vector<uint8_t> b = encode(m);
    if (b.size() <= asked) send(to, b);  // (never more than was sent to us: no amplification)
}

Server::Addr& Server::addr(uint32_t ip, double now) {
    if (addrs.size() >= lim.maxAddresses && !addrs.count(ip))  // (a flood of addresses: forget the ones holding no code)
        for (auto it = addrs.begin(); it != addrs.end();) it = it->second.codes > 0 ? std::next(it) : addrs.erase(it);
    Addr& a = addrs[ip];
    a.seen = now;
    return a;
}

uint64_t Server::cookieFor(const Endpoint& e, long minute) const {
    std::vector<uint8_t> d(14);
    std::memcpy(d.data(), &e.ip, 4);
    std::memcpy(d.data() + 4, &e.port, 2);
    const int64_t m = minute;
    std::memcpy(d.data() + 6, &m, 8);
    const std::vector<uint8_t> h = net::hmacSha256(std::string((const char*)cookieKey, sizeof cookieKey), d);
    uint64_t c;
    std::memcpy(&c, h.data(), 8);
    return c | 1;
}

void Server::dropPair(uint32_t id) { pairs.erase(id); }

void Server::dropHost(std::map<std::string, Host>::iterator it, bool keepPairs) {
    if (!keepPairs)
        for (uint32_t id : it->second.pairs) dropPair(id);
    auto a = addrs.find(it->second.at.ip);
    if (a != addrs.end() && a->second.codes > 0) a->second.codes--;
    hosts.erase(it);
}

void Server::sendIntros(Pair& p, bool toGuest, bool toHost) {
    auto h = hosts.find(p.code);
    if (h == hosts.end()) return;
    Msg m;
    m.type = RV_INTRO;
    m.pair = p.id;
    m.secret = p.secret;
    const bool sameNet = p.host.ip == p.guest.ip;  // (LAN addresses only between games behind one public address)
    if (toGuest) {
        m.key = p.guestKey, m.nonce = p.guestNonce, m.seen = p.host;
        m.locals = sameNet ? h->second.locals : std::vector<Endpoint>();
        send(p.guest, encode(m));
    }
    if (toHost) {
        m.key = p.hostKey, m.nonce = 0, m.seen = p.guest;
        m.locals = sameNet ? p.guestLocals : std::vector<Endpoint>();
        send(p.host, encode(m));
    }
}

void Server::sweep(double now) {
    if (now < nextSweep) return;
    nextSweep = now + 0.1;
    for (auto& kv : pairs) {
        Pair& p = kv.second;
        if (p.introsLeft > 0 && now >= p.nextIntro) {
            p.introsLeft--;
            p.nextIntro = now + 0.4;
            sendIntros(p, false, true);
        }
    }
    if (now < nextSlow) return;
    nextSlow = now + 1.0;
    for (auto it = hosts.begin(); it != hosts.end();) {
        const Host& h = it->second;
        if (now - h.lastKeep > lim.codeLife || now - h.born > lim.maxLife) {
            auto dead = it++;
            dropHost(dead, false);
        } else {
            ++it;
        }
    }
    for (auto it = addrs.begin(); it != addrs.end();) it = it->second.codes <= 0 && now - it->second.seen > 120 ? addrs.erase(it) : std::next(it);
}

void Server::poll(double now, int ms) {
    if (started < 0) started = now;
    if (fd >= 0 && ms > 0) {
        pollfd pf{};
        pf.fd = fd;
        pf.events = POLLIN;
        pollFds(&pf, 1, ms);
    }
    uint8_t buf[2048];
    for (int k = 0; k < 4096 && fd >= 0; k++) {
        Endpoint from;
        const int n = udpRecv(fd, buf, sizeof buf, from.ip, from.port);
        if (n <= 0) break;
        handle(buf, (size_t)n, from, now);
    }
    sweep(now);
}

void Server::handle(const uint8_t* p, size_t n, const Endpoint& from, double now) {
    if (started < 0) started = now;
    Msg m;
    if (n < kHeader || !decode(p, n, m)) {
        // (not ours, or garbage: no answer at all; a different proto still decodes its header)
        if (n >= kHeader && p[0] == (kMagic & 255) && p[1] == (kMagic >> 8)) stats.dropped++;
        return;
    }
    Addr& a = addr(from.ip, now);
    if (m.type != RV_RELAY && !a.packets.take(now, 1, lim.packetsPerSec, lim.packetsPerSec * 2)) {
        stats.dropped++;
        return;
    }
    if (m.proto != kProto) {
        Msg f;
        f.type = RV_FAIL, f.reason = F_PROTO, f.version = kProto;
        reply(from, n, f);
        return;
    }
    auto fail = [&](Reason why, uint64_t nonce, uint16_t version = 0, uint8_t players = 0, uint8_t maxP = 0) {
        Msg f;
        f.type = RV_FAIL, f.reason = why, f.nonce = nonce, f.version = version, f.players = players, f.maxPlayers = maxP;
        reply(from, n, f);
    };
    auto hostByKey = [&](const std::string& code, uint64_t key) -> Host* {
        auto it = hosts.find(code);
        if (it == hosts.end() || !net::sameBytes((const uint8_t*)&key, (const uint8_t*)&it->second.key, 8)) return nullptr;
        return &it->second;
    };
    switch (m.type) {
        case RV_REGISTER: {
            if (n < kPad) return;  // (unpadded: our answer could be bigger)
            for (auto& kv : hosts)  // the same request again (its answer was lost): the same code
                if (kv.second.at == from && kv.second.nonce == m.nonce) {
                    Msg r;
                    r.type = RV_REGISTERED, r.nonce = m.nonce, r.code = kv.first, r.key = kv.second.key, r.seen = from, r.keepSecs = (uint16_t)kKeepEvery;
                    reply(from, n, r);
                    return;
                }
            if (!a.registers.take(now, 1, lim.registersPerMin / 60, lim.registersPerMin) || a.codes >= lim.codesPerIp || (int)hosts.size() >= lim.maxCodes) {
                stats.refused++;
                note("refused a registration from " + net::ipText(from.ip) + (a.codes >= lim.codesPerIp ? ": too many codes" : ": too often or full"), now);
                return fail(F_BUSY, m.nonce);
            }
            std::string code;
            for (int k = 0; k < 64 && (code.empty() || hosts.count(code)); k++) code = randomCode();
            if (hosts.count(code)) return fail(F_BUSY, m.nonce);
            Host h;
            h.code = code, h.at = from, h.locals = m.locals, h.key = net::random64(), h.nonce = m.nonce;
            h.version = m.version, h.players = m.players, h.maxPlayers = m.maxPlayers, h.flags = m.flags;
            h.born = h.lastKeep = now;
            hosts[code] = h;
            a.codes++;
            stats.registrations++;
            Msg r;
            r.type = RV_REGISTERED, r.nonce = m.nonce, r.code = code, r.key = h.key, r.seen = from, r.keepSecs = (uint16_t)kKeepEvery;
            reply(from, n, r);
            return;
        }
        case RV_KEEP: {
            Host* h = hostByKey(m.code, m.key);
            if (!h) return fail(F_GONE, 0);
            if (h->at != from) {  // (the host's NAT moved it: its pairs follow)
                for (uint32_t id : h->pairs) {
                    auto pit = pairs.find(id);
                    if (pit != pairs.end()) pit->second.host = from;
                }
                h->at = from;
            }
            h->players = m.players, h->maxPlayers = m.maxPlayers, h->flags = m.flags & FL_LOCKED;
            h->lastKeep = now;
            Msg r;
            r.type = RV_KEPT, r.code = m.code;
            reply(from, n, r);
            return;
        }
        case RV_CLOSE: {
            Host* h = hostByKey(m.code, m.key);
            if (!h) return;
            if (m.flags & FL_RETIRE) h->retired = true;
            else dropHost(hosts.find(m.code), false);
            return;
        }
        case RV_LOOKUP: {
            if (n < kPad) return;
            const long minute = (long)(now / 60);
            if (m.cookie != cookieFor(from, minute) && m.cookie != cookieFor(from, minute - 1)) {
                Msg c;  // prove that address is yours first (a spoofed one never sees this)
                c.type = RV_COOKIE, c.nonce = m.nonce, c.cookie = cookieFor(from, minute);
                reply(from, n, c);
                return;
            }
            // (The same lookup again, its answer lost: the same pair.)
            for (auto& kv : pairs)
                if (kv.second.guest == from && kv.second.guestNonce == m.nonce && kv.second.code == m.code) return sendIntros(kv.second, true, kv.second.introsLeft > 0);
            if (!a.lookups.take(now, 1, lim.lookupsPerMin / 60, lim.lookupsPerMin)) {
                stats.refused++;
                note("too many lookups from " + net::ipText(from.ip), now);
                return fail(F_BUSY, m.nonce);
            }
            stats.lookups++;
            auto it = hosts.find(m.code);
            if (it == hosts.end() || it->second.retired) return fail(F_NOT_FOUND, m.nonce);
            Host& h = it->second;
            if (m.version != h.version) return fail(m.version < h.version ? F_OLDER : F_NEWER, m.nonce, h.version, h.players, h.maxPlayers);
            if (h.players >= h.maxPlayers && h.maxPlayers > 0) return fail(F_FULL, m.nonce, h.version, h.players, h.maxPlayers);
            if ((int)pairs.size() >= lim.maxPairs) return fail(F_BUSY, m.nonce);
            Pair pr;
            do pr.id = (uint32_t)net::random64();
            while (!pr.id || pairs.count(pr.id));
            pr.code = m.code, pr.secret = net::random64(), pr.hostKey = net::random64(), pr.guestKey = net::random64(), pr.guestNonce = m.nonce;
            pr.host = h.at, pr.guest = from, pr.guestLocals = m.locals;
            pr.born = pr.lastUse = now;
            pr.introsLeft = 3, pr.nextIntro = now + 0.4;
            h.pairs.push_back(pr.id);
            if ((int)h.pairs.size() > lim.pairsPerCode) {
                dropPair(h.pairs.front());
                h.pairs.erase(h.pairs.begin());
            }
            Pair& p = pairs[pr.id] = pr;
            stats.intros++;
            sendIntros(p, true, true);
            return;
        }
        case RV_INTRO_ACK: {
            if (!hostByKey(m.code, m.key)) return;
            auto it = pairs.find(m.pair);
            if (it != pairs.end() && it->second.code == m.code) it->second.introsLeft = 0;
            return;
        }
        case RV_RELAY: {
            auto it = pairs.find(m.pair);
            if (it == pairs.end()) return void(stats.dropped++);
            Pair& pr = it->second;
            Endpoint to;
            if (from == pr.host && net::sameBytes((const uint8_t*)&m.key, (const uint8_t*)&pr.hostKey, 8)) to = pr.guest;
            else if (from == pr.guest && net::sameBytes((const uint8_t*)&m.key, (const uint8_t*)&pr.guestKey, 8)) to = pr.host;
            else return void(stats.dropped++);  // (no open relay: only between the two it introduced, from where they were)
            const double bytes = (double)n;
            if (!relayAll.take(now, bytes, lim.relayBytesPerSec, lim.relayBytesPerSec) || !pr.rate.take(now, bytes, lim.pairBytesPerSec, lim.pairBurst) ||
                !a.relay.take(now, bytes, lim.ipRelayBytesPerSec, lim.ipRelayBytesPerSec * 2))
                return void(stats.dropped++);
            pr.lastUse = now;
            std::vector<uint8_t> out(n - 8);  // (RELAYED: the header and pair, no key)
            std::memcpy(out.data(), p, 8);
            out[3] = RV_RELAYED;
            std::memcpy(out.data() + 8, p + 16, n - 16);
            stats.relayedPackets++;
            stats.relayedBytes += out.size();
            send(to, out);
            return;
        }
        case RV_PING: {
            if (n < kPad) return;
            Msg r;
            r.type = RV_PONG;
            if ((from.ip >> 24) == 127) {  // (the counts only to this machine: the health check)
                r.stats[0] = (uint32_t)hosts.size(), r.stats[1] = (uint32_t)pairs.size(), r.stats[2] = (uint32_t)(stats.relayedBytes >> 10);
                r.stats[3] = (uint32_t)stats.relayedPackets, r.stats[4] = (uint32_t)stats.dropped, r.stats[5] = (uint32_t)stats.lookups;
                r.stats[6] = (uint32_t)stats.registrations, r.stats[7] = (uint32_t)(now - started);
            }
            reply(from, n, r);
            return;
        }
        default: stats.dropped++; return;
    }
}

}  // namespace rv
