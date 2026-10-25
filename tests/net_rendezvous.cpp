// The join-code backend (src/rendezvous.h) without sockets: packets handed to Server::handle from
// made-up addresses, what it sends caught in its outbox. Codes, introductions, the relay, and the
// limits (rate, size, no open relay, no reply bigger than its question).
#include <cstring>
#include <random>

#include "../src/rendezvous.h"
#include "harness.h"

namespace {
using namespace rv;

struct RvBox {
    Server s;
    std::vector<Server::Sent> out;
    double now = 1000;
    RvBox() { s.outbox = &out; }
    // Hands it one packet; returns what it sent in answer (and keeps the outbox empty).
    std::vector<Server::Sent> in(const Msg& m, const Endpoint& from, double dt = 0.01) {
        now += dt;
        const std::vector<uint8_t> b = encode(m);
        s.handle(b.data(), b.size(), from, now);
        std::vector<Server::Sent> got;
        got.swap(out);
        return got;
    }
    std::vector<Server::Sent> raw(const std::vector<uint8_t>& b, const Endpoint& from) {
        s.handle(b.data(), b.size(), from, now);
        std::vector<Server::Sent> got;
        got.swap(out);
        return got;
    }
    void tick(double secs) {
        for (double t = 0; t < secs; t += 0.05) now += 0.05, s.poll(now);
    }
};
Endpoint ep(uint32_t ip, uint16_t port) {
    Endpoint e;
    e.ip = ip, e.port = port;
    return e;
}
bool firstIs(const std::vector<Server::Sent>& v, uint8_t type, Msg& m, const Endpoint* to = nullptr) {
    for (const auto& s : v)
        if (decode(s.b.data(), s.b.size(), m) && m.type == type && (!to || s.to == *to)) return true;
    return false;
}
// A host registered from `at`: its code and key.
bool registerHost(RvBox& b, const Endpoint& at, std::string& code, uint64_t& key, uint16_t version = 8, uint8_t players = 1, uint8_t maxP = 8) {
    Msg r;
    r.type = RV_REGISTER, r.version = version, r.players = players, r.maxPlayers = maxP, r.nonce = 77 + at.port;
    r.locals = {ep(0xC0A80105, at.port)};
    Msg got;
    if (!firstIs(b.in(r, at), RV_REGISTERED, got)) return false;
    code = got.code, key = got.key;
    return true;
}
// A guest's lookup, cookie and all: the INTRO it gets (false: a FAIL, in `fail`).
bool lookup(RvBox& b, const Endpoint& at, const std::string& code, Msg& intro, std::vector<Server::Sent>& sent, Msg* fail = nullptr, uint16_t version = 8, uint64_t nonce = 5) {
    Msg l;
    l.type = RV_LOOKUP, l.version = version, l.code = code, l.nonce = nonce;
    Msg c;
    if (!firstIs(b.in(l, at), RV_COOKIE, c)) return false;
    l.cookie = c.cookie;
    sent = b.in(l, at);
    if (fail) firstIs(sent, RV_FAIL, *fail);
    return firstIs(sent, RV_INTRO, intro, &at);
}
}  // namespace

SCENARIO(rvCodes, "net/rv-codes", "join codes: six of a 30-symbol alphabet with no 0 O 1 I L U; typing ignores case, spaces and dashes") {
    ctx.check("30 symbols", std::strlen(kAlphabet) == 30);
    for (char c : std::string("0O1ILU")) ctx.check(std::string("no ") + c, std::strchr(kAlphabet, c) == nullptr);
    ctx.check("normalized", normalizeCode("kx7-m4q") == "KX7M4Q" && normalizeCode(" KX7 M4Q ") == "KX7M4Q");
    ctx.check("refused: too short, too long, a confusable letter", normalizeCode("KX7M4") == "" && normalizeCode("KX7M4QQ") == "" && normalizeCode("KX7M4O") == "");
    ctx.check("shown grouped", formatCode("KX7M4Q") == "KX7-M4Q");
    int counts[256] = {};
    bool ok = true;
    for (int i = 0; i < 3000; i++) {
        const std::string c = randomCode();
        ok &= normalizeCode(c) == c;
        for (char ch : c) counts[(uint8_t)ch]++;
    }
    ctx.check("random codes are valid", ok);
    int lo = 1 << 30, hi = 0;
    for (char ch : std::string(kAlphabet)) lo = std::min(lo, counts[(uint8_t)ch]), hi = std::max(hi, counts[(uint8_t)ch]);
    ctx.check("and use every symbol about as often", lo > 400 && hi < 800, std::to_string(lo) + ".." + std::to_string(hi));
}

SCENARIO(rvFlow, "net/rv-flow", "register, look up (with the address cookie), introduce both sides, relay only between them, retire and close, expiry") {
    RvBox b;
    const Endpoint host = ep(0x0A000001, 40000), guest = ep(0x0A000002, 50000), stranger = ep(0x0A000003, 50000);
    std::string code;
    uint64_t key = 0;
    ctx.check("a host registers and gets a code", registerHost(b, host, code, key) && normalizeCode(code) == code, code);
    {  // the same REGISTER again (its answer was lost): the same code
        Msg r;
        r.type = RV_REGISTER, r.version = 8, r.players = 1, r.maxPlayers = 8, r.nonce = 77 + host.port;
        Msg got;
        ctx.check("a repeated registration gets the same code", firstIs(b.in(r, host), RV_REGISTERED, got) && got.code == code && got.seen == host);
    }
    Msg intro;
    std::vector<Server::Sent> sent;
    {
        Msg l;
        l.type = RV_LOOKUP, l.version = 8, l.code = code, l.nonce = 5;
        Msg got;
        const auto first = b.in(l, guest);
        ctx.check("a lookup without the cookie gets only the cookie", first.size() == 1 && firstIs(first, RV_COOKIE, got) && got.nonce == 5);
    }
    ctx.check("with it, the guest is introduced", lookup(b, guest, code, intro, sent) && intro.seen == host && intro.locals.empty());
    Msg hostIntro;
    ctx.check("and so is the host", firstIs(sent, RV_INTRO, hostIntro, &host) && hostIntro.pair == intro.pair && hostIntro.secret == intro.secret && hostIntro.seen == guest);
    ctx.check("each side has its own relay key", hostIntro.key != intro.key && intro.key && hostIntro.key);
    ctx.check("LAN addresses only between games behind one address", hostIntro.locals.empty());
    {
        const auto before = b.s.pairCount();
        Msg again;
        std::vector<Server::Sent> s2;
        lookup(b, guest, code, again, s2);
        ctx.check("a repeated lookup is the same pair", b.s.pairCount() == before && again.pair == intro.pair);
    }
    // The host doesn't ack: the introduction is repeated to it, then stops.
    b.tick(2.0);
    {
        Msg ack;
        ack.type = RV_INTRO_ACK, ack.code = code, ack.key = key, ack.pair = intro.pair;
        b.in(ack, host);
    }
    // The relay.
    auto relay = [&](const Endpoint& from, uint32_t pair, uint64_t k, const std::string& text) {
        std::vector<uint8_t> f;
        relayFrame(f, pair, k, (const uint8_t*)text.data(), text.size());
        return b.raw(f, from);
    };
    {
        const auto r = relay(guest, intro.pair, intro.key, "hello host");
        uint32_t pr = 0;
        const uint8_t* pl = nullptr;
        size_t len = 0;
        ctx.check("guest -> host is relayed to the host, the key stripped",
                  r.size() == 1 && r[0].to == host && relayedPayload(r[0].b.data(), r[0].b.size(), pr, pl, len) && pr == intro.pair && std::string((const char*)pl, len) == "hello host");
        const auto back = relay(host, intro.pair, hostIntro.key, "hi guest");
        ctx.check("host -> guest too", back.size() == 1 && back[0].to == guest);
    }
    ctx.check("the wrong key goes nowhere", relay(guest, intro.pair, intro.key ^ 1, "x").empty());
    ctx.check("the other side's key from this side goes nowhere", relay(guest, intro.pair, hostIntro.key, "x").empty());
    ctx.check("the right key from another address goes nowhere (no open relay)", relay(stranger, intro.pair, intro.key, "x").empty());
    ctx.check("another port of the guest's address too", relay(ep(guest.ip, guest.port + 1), intro.pair, intro.key, "x").empty());
    ctx.check("an unknown pair goes nowhere", relay(guest, intro.pair + 1, intro.key, "x").empty());
    // Retired: no new joiners, the relay carries on.
    {
        Msg c;
        c.type = RV_CLOSE, c.code = code, c.key = key ^ 7, c.flags = FL_RETIRE;
        b.in(c, host);
        Msg i2, f;
        std::vector<Server::Sent> s2;
        ctx.check("CLOSE with a wrong key does nothing", lookup(b, stranger, code, i2, s2, &f, 8, 99));
        c.key = key;
        b.in(c, host);
        ctx.check("retired: a lookup is told no such game", !lookup(b, ep(0x0A000004, 1), code, i2, s2, &f, 8, 98) && f.type == RV_FAIL && f.reason == F_NOT_FOUND);
        ctx.check("but the relay still carries the players in it", relay(guest, intro.pair, intro.key, "still").size() == 1);
        Msg k;
        k.type = RV_KEEP, k.code = code, k.key = key, k.players = 2, k.maxPlayers = 8;
        Msg kept;
        ctx.check("and keepalives still answer", firstIs(b.in(k, host), RV_KEPT, kept));
        c.flags = 0;
        b.in(c, host);
        ctx.check("closed: the relay stops", relay(guest, intro.pair, intro.key, "gone").empty() && b.s.codes() == 0);
        ctx.check("and a keepalive is told it's gone", firstIs(b.in(k, host), RV_FAIL, kept) && kept.reason == F_GONE);
    }
    // Expiry: no keepalive for 45 s.
    {
        std::string c2;
        uint64_t k2;
        registerHost(b, ep(0x0A000010, 4000), c2, k2);
        b.tick(30);
        Msg k;
        k.type = RV_KEEP, k.code = c2, k.key = k2, k.players = 1, k.maxPlayers = 8;
        b.in(k, ep(0x0A000010, 4000));
        b.tick(30);
        ctx.check("a code kept alive lives on", b.s.codes() == 1);
        b.tick(50);
        ctx.check("one without keepalives for 45 s dies", b.s.codes() == 0);
    }
    // Version and full.
    {
        std::string c3;
        uint64_t k3;
        registerHost(b, ep(0x0A000020, 4000), c3, k3, 8, 8, 8);
        Msg i, f;
        std::vector<Server::Sent> s3;
        ctx.check("an older game is told so, with the host's version", !lookup(b, ep(0x0A000021, 1), c3, i, s3, &f, 7) && f.reason == F_OLDER && f.version == 8);
        ctx.check("a newer game too", !lookup(b, ep(0x0A000022, 1), c3, i, s3, &f, 9) && f.reason == F_NEWER);
        ctx.check("a full game says full", !lookup(b, ep(0x0A000023, 1), c3, i, s3, &f, 8) && f.reason == F_FULL && f.players == 8);
        ctx.check("a wrong code: no such game", !lookup(b, ep(0x0A000024, 1), "ABCDEF", i, s3, &f, 8) && f.reason == F_NOT_FOUND);
    }
    // Same public address: LAN addresses are shared.
    {
        std::string c4;
        uint64_t k4;
        const Endpoint h4 = ep(0x0A000030, 4000);
        registerHost(b, h4, c4, k4);
        Msg i;
        std::vector<Server::Sent> s4;
        ctx.check("behind one public address, the host's LAN address is given", lookup(b, ep(h4.ip, 5000), c4, i, s4) && i.locals.size() == 1 && i.locals[0].ip == 0xC0A80105);
    }
}

SCENARIO(rvLimits, "net/rv-limits", "the backend's limits: rate per address, codes per address, the relay's bandwidth, no answer bigger than its question, garbage ignored") {
    RvBox b;
    // Registrations: 6 a minute, 4 codes per address.
    {
        const Endpoint at = ep(0x0B000001, 1);
        int got = 0;
        for (int i = 0; i < 10; i++) {
            Msg r;
            r.type = RV_REGISTER, r.version = 8, r.maxPlayers = 8, r.nonce = 1000 + i;
            Msg m;
            got += firstIs(b.in(r, ep(at.ip, (uint16_t)(100 + i))), RV_REGISTERED, m);
        }
        ctx.check("at most 4 codes from one address", got == 4, std::to_string(got));
    }
    // Lookups: 10 a minute per address, wrong codes included.
    {
        const Endpoint g = ep(0x0B000002, 9);
        int answered = 0, busy = 0;
        for (int i = 0; i < 20; i++) {
            Msg in, f;
            std::vector<Server::Sent> s;
            lookup(b, g, "ABCDEF", in, s, &f, 8, 500 + i);
            answered += f.type == RV_FAIL && f.reason == F_NOT_FOUND;
            busy += f.type == RV_FAIL && f.reason == F_BUSY;
        }
        ctx.check("guessing codes: 10 a minute, then busy", answered == 10 && busy == 10, std::to_string(answered) + " / " + std::to_string(busy));
        b.now += 60;
        Msg in, f;
        std::vector<Server::Sent> s;
        lookup(b, g, "ABCDEF", in, s, &f, 8, 999);
        ctx.check("and again after a minute", f.reason == F_NOT_FOUND);
    }
    // Unpadded questions get nothing (their answer could be bigger).
    {
        Msg r;
        r.type = RV_REGISTER, r.version = 8, r.maxPlayers = 8, r.nonce = 1;
        std::vector<uint8_t> raw = encode(r);
        raw.resize(40);
        ctx.check("an unpadded RV_REGISTER is ignored", b.raw(raw, ep(0x0B000003, 1)).empty());
        Msg p;
        p.type = RV_PING;
        raw = encode(p);
        raw.resize(8);
        ctx.check("an unpadded RV_PING too", b.raw(raw, ep(0x0B000003, 1)).empty());
    }
    // Relay bandwidth: a pair's 512 KB/s (2 MB burst).
    {
        RvBox c;
        std::string code;
        uint64_t key;
        const Endpoint h = ep(0x0C000001, 1), g = ep(0x0C000002, 2);
        registerHost(c, h, code, key);
        Msg intro;
        std::vector<Server::Sent> s;
        lookup(c, g, code, intro, s);
        std::vector<uint8_t> payload(1100, 0x55), f;
        relayFrame(f, intro.pair, intro.key, payload.data(), payload.size());
        size_t through = 0;
        for (int i = 0; i < 6000; i++) {  // 6.6 MB in one second
            c.now += 1.0 / 6000;
            c.s.handle(f.data(), f.size(), g, c.now);
            for (const auto& o : c.out) through += o.b.size();
            c.out.clear();
        }
        ctx.metric("relayed in a second of flooding", through / 1024.0, "KB", 1500, 3200);
        ctx.check("a flooding pair gets its burst and its rate, no more", through < (size_t)(2.75e6) && through > 1.5e6, std::to_string(through));
    }
    // No answer is bigger than its question, for any message and any field, and garbage gets nothing.
    {
        RvBox f;
        std::mt19937 rng(7);
        bool bigger = false, crashed = false;
        size_t answers = 0;
        std::string code;
        uint64_t key;
        registerHost(f, ep(0x0D000001, 1), code, key);
        for (int i = 0; i < 20000; i++) {
            Msg m;
            m.type = (uint8_t)(1 + rng() % 15);
            m.version = (uint16_t)(rng() % 10), m.players = (uint8_t)(rng() % 10), m.maxPlayers = (uint8_t)(rng() % 10);
            m.nonce = rng(), m.key = (rng() % 4 == 0) ? key : rng(), m.cookie = rng(), m.pair = rng();
            m.code = rng() % 2 ? code : randomCode();
            for (uint32_t k = 0; k < rng() % 6; k++) m.locals.push_back(ep(rng(), (uint16_t)rng()));
            std::vector<uint8_t> raw = encode(m);
            if (rng() % 3 == 0 && !raw.empty()) raw.resize(rng() % raw.size());
            if (rng() % 5 == 0)
                for (auto& x : raw)
                    if (rng() % 8 == 0) x = (uint8_t)rng();
            const Endpoint from = ep(0x0E000000 + rng() % 64, (uint16_t)rng());
            f.now += 0.001;
            f.s.handle(raw.data(), raw.size(), from, f.now);
            for (const auto& o : f.out) {
                answers++;
                if (o.b.size() > raw.size()) bigger = true;
            }
            f.out.clear();
        }
        ctx.check("20000 mangled messages: no answer bigger than its question", !bigger && !crashed, std::to_string(answers) + " answers");
        std::vector<uint8_t> junk(64, 0);
        ctx.check("bytes that aren't ours get nothing", f.raw(junk, ep(1, 1)).empty());
    }
}
