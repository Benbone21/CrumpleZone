// crumplezone-rendezvous: the join-code backend (src/rendezvous.h, docs/plans/join-codes.md).
// Headless and small: codes, introductions for NAT hole punching, and a relay between introduced
// games only. No world, no files, nothing written to disk. Setup on a server:
// docs/server-rendezvous-setup.md.
//
//   crumplezone-rendezvous [--port 28015] [--bind IP] [--relay-mbit 40] [--pair-kbps 512] ...
//   crumplezone-rendezvous --check 127.0.0.1[:28015]      health check (counts, from this machine)
//   crumplezone-rendezvous --selftest 127.0.0.1[:28015]   a host and a guest through it, relay both ways
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/netsock.h"
#include "../src/rendezvous.h"

namespace {

volatile sig_atomic_t running = 1;
void onSignal(int) { running = 0; }

bool number(const char* flag, const char* text, double lo, double hi, double& out) {
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (!end || *end || !(v >= lo && v <= hi)) {
        std::fprintf(stderr, "bad %s \"%s\": a number from %g to %g\n", flag, text, lo, hi);
        return false;
    }
    out = v;
    return true;
}

bool target(const std::string& text, rv::Endpoint& to) {
    std::string host, why;
    int port = 0;
    if (!net::parseAddress(text, host, port, &why, rv::kDefaultPort) || !resolveIPv4(host, to.ip)) {
        std::fprintf(stderr, "bad address \"%s\": %s\n", text.c_str(), why.empty() ? "can't find it" : why.c_str());
        return false;
    }
    to.port = (uint16_t)port;
    return true;
}

// Sends `m` from `fd` to `to` until an answer of one of `want` comes (up to `secs`). False if none.
bool ask(int fd, const rv::Endpoint& to, const rv::Msg& m, std::initializer_list<uint8_t> want, rv::Msg& got, double secs = 3) {
    const std::vector<uint8_t> b = rv::encode(m);
    const double t0 = net::clockNow();
    double next = 0;
    while (net::clockNow() - t0 < secs) {
        if (net::clockNow() >= next) udpSend(fd, b.data(), b.size(), to.ip, to.port), next = net::clockNow() + 0.5;
        pollfd pf{};
        pf.fd = fd;
        pf.events = POLLIN;
        pollFds(&pf, 1, 50);
        uint8_t buf[2048];
        rv::Endpoint from;
        const int n = udpRecv(fd, buf, sizeof buf, from.ip, from.port);
        if (n <= 0 || !rv::decode(buf, (size_t)n, got)) continue;
        for (uint8_t w : want)
            if (got.type == w) return true;
    }
    return false;
}

bool waitFor(int fd, uint8_t type, rv::Msg& got, double secs = 3) {
    const double t0 = net::clockNow();
    while (net::clockNow() - t0 < secs) {
        pollfd pf{};
        pf.fd = fd;
        pf.events = POLLIN;
        pollFds(&pf, 1, 50);
        uint8_t buf[2048];
        rv::Endpoint from;
        const int n = udpRecv(fd, buf, sizeof buf, from.ip, from.port);
        if (n > 0 && rv::decode(buf, (size_t)n, got) && got.type == type) return true;
    }
    return false;
}

int check(const rv::Endpoint& to) {
    const int fd = udpOpen(0, 0);
    rv::Msg ping, pong;
    ping.type = rv::RV_PING;
    if (fd < 0 || !ask(fd, to, ping, {rv::RV_PONG}, pong)) {
        std::printf("FAIL: no answer from %s\n", rv::endpointText(to).c_str());
        return 1;
    }
    std::printf("ok %s: codes %u, pairs %u, relayed %u KB in %u packets, dropped %u, lookups %u, registrations %u, up %u s\n", rv::endpointText(to).c_str(),
                pong.stats[0], pong.stats[1], pong.stats[2], pong.stats[3], pong.stats[4], pong.stats[5], pong.stats[6], pong.stats[7]);
    closeSock(fd);
    return 0;
}

int selftest(const rv::Endpoint& to) {
    const int hostFd = udpOpen(0, 0), guestFd = udpOpen(0, 0);
    auto bad = [&](const char* what) {
        std::printf("selftest RV_FAIL: %s\n", what);
        return 1;
    };
    if (hostFd < 0 || guestFd < 0) return bad("no sockets");
    rv::Msg reg, got;
    reg.type = rv::RV_REGISTER, reg.version = 0xfffe, reg.players = 1, reg.maxPlayers = 8, reg.nonce = net::random64();
    if (!ask(hostFd, to, reg, {rv::RV_REGISTERED, rv::RV_FAIL}, got) || got.type != rv::RV_REGISTERED) return bad("register (no answer, or refused: too many codes from here?)");
    const std::string code = got.code;
    const uint64_t hostKey = got.key;
    std::printf("registered: code %s, seen as %s\n", rv::formatCode(code).c_str(), rv::endpointText(got.seen).c_str());
    rv::Msg look;
    look.type = rv::RV_LOOKUP, look.version = 0xfffe, look.code = code, look.nonce = net::random64();
    if (!ask(guestFd, to, look, {rv::RV_COOKIE}, got)) return bad("lookup: no cookie");
    look.cookie = got.cookie;
    if (!ask(guestFd, to, look, {rv::RV_INTRO, rv::RV_FAIL}, got) || got.type != rv::RV_INTRO) return bad("lookup: no introduction");
    const uint32_t pair = got.pair;
    const uint64_t guestKey = got.key;
    rv::Msg hintro;
    if (!waitFor(hostFd, rv::RV_INTRO, hintro) || hintro.pair != pair || hintro.secret != got.secret) return bad("the host wasn't introduced");
    rv::Msg ack;
    ack.type = rv::RV_INTRO_ACK, ack.code = code, ack.key = hostKey, ack.pair = pair;
    std::vector<uint8_t> b = rv::encode(ack);
    udpSend(hostFd, b.data(), b.size(), to.ip, to.port);
    std::printf("introduced: pair %08x, guest seen as %s\n", pair, rv::endpointText(hintro.seen).c_str());
    // The relay, both ways.
    for (int dir = 0; dir < 2; dir++) {
        const char* text = dir == 0 ? "guest->host" : "host->guest";
        const int from = dir == 0 ? guestFd : hostFd, toFd = dir == 0 ? hostFd : guestFd;
        bool ok = false;
        for (int tries = 0; tries < 6 && !ok; tries++) {
            rv::relayFrame(b, pair, dir == 0 ? guestKey : hintro.key, (const uint8_t*)text, std::strlen(text));
            udpSend(from, b.data(), b.size(), to.ip, to.port);
            const double t0 = net::clockNow();
            while (!ok && net::clockNow() - t0 < 0.5) {
                pollfd pf{};
                pf.fd = toFd;
                pf.events = POLLIN;
                pollFds(&pf, 1, 50);
                uint8_t buf[2048];
                rv::Endpoint src;
                const int n = udpRecv(toFd, buf, sizeof buf, src.ip, src.port);
                uint32_t pr;
                const uint8_t* pl;
                size_t len;
                if (n > 0 && rv::relayedPayload(buf, (size_t)n, pr, pl, len) && pr == pair && len == std::strlen(text) && !std::memcmp(pl, text, len)) ok = true;
            }
        }
        if (!ok) return bad(dir == 0 ? "relay guest -> host" : "relay host -> guest");
        std::printf("relayed %s\n", text);
    }
    // A wrong key goes nowhere.
    rv::relayFrame(b, pair, guestKey ^ 1, (const uint8_t*)"x", 1);
    udpSend(guestFd, b.data(), b.size(), to.ip, to.port);
    rv::Msg close;
    close.type = rv::RV_CLOSE, close.code = code, close.key = hostKey;
    b = rv::encode(close);
    udpSend(hostFd, b.data(), b.size(), to.ip, to.port);
    look.nonce = net::random64();
    if (ask(guestFd, to, look, {rv::RV_FAIL, rv::RV_INTRO}, got, 2) && got.type == rv::RV_INTRO) return bad("the closed code still works");
    std::printf("selftest OK: register, lookup, introduce, relay both ways, close (code %s)\n", rv::formatCode(code).c_str());
    closeSock(hostFd);
    closeSock(guestFd);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int port = rv::kDefaultPort;
    std::string bind;
    bool allowRoot = false, quiet = false;
    rv::Limits lim;
    double v = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--port" && more) {
            port = net::parsePort(argv[++i], 1024);
            if (port < 0) return std::fprintf(stderr, "bad --port: 1024 to 65535\n"), 2;
        } else if (a == "--bind" && more) {
            bind = argv[++i];
        } else if (a == "--relay-mbit" && more) {
            if (!number("--relay-mbit", argv[++i], 0, 100000, v)) return 2;
            lim.relayBytesPerSec = v * 1e6 / 8;
        } else if (a == "--pair-kbps" && more) {
            if (!number("--pair-kbps", argv[++i], 16, 1e6, v)) return 2;
            lim.pairBytesPerSec = v * 1024, lim.pairBurst = std::max(lim.pairBurst, v * 1024 * 2);
        } else if (a == "--max-codes" && more) {
            if (!number("--max-codes", argv[++i], 1, 1e7, v)) return 2;
            lim.maxCodes = (int)v;
        } else if (a == "--codes-per-ip" && more) {
            if (!number("--codes-per-ip", argv[++i], 1, 1000, v)) return 2;
            lim.codesPerIp = (int)v;
        } else if (a == "--lookups-per-min" && more) {
            if (!number("--lookups-per-min", argv[++i], 1, 1e5, v)) return 2;
            lim.lookupsPerMin = v;
        } else if (a == "--allow-root") {
            allowRoot = true;
        } else if (a == "--quiet") {
            quiet = true;
        } else if ((a == "--check" || a == "--selftest") && more) {
            rv::Endpoint to;
            if (!target(argv[++i], to)) return 2;
            return a == "--check" ? check(to) : selftest(to);
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "Usage: crumplezone-rendezvous [OPTIONS]\n"
                "The join-code backend: codes, NAT hole-punching introductions, and a relay between introduced games.\n"
                "  --port PORT            UDP port (default %d)\n"
                "  --bind IP              listen on one IPv4 address (default: all)\n"
                "  --relay-mbit N         all relayed traffic together, Mbit/s (default 40; 0 turns the relay off)\n"
                "  --pair-kbps N          one relayed pair, KB/s (default 512)\n"
                "  --max-codes N          open codes at once (default 20000)\n"
                "  --codes-per-ip N       open codes from one address (default 4)\n"
                "  --lookups-per-min N    code lookups from one address a minute (default 10)\n"
                "  --quiet                no once-a-minute counts line\n"
                "  --allow-root           run as root anyway (don't)\n"
                "  --check HOST[:PORT]    health check: asks a running backend for its counts (from the same machine)\n"
                "  --selftest HOST[:PORT] registers a code, looks it up, relays both ways and closes it\n",
                rv::kDefaultPort);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", a.c_str());
            return 2;
        }
    }
#ifndef _WIN32
    if (geteuid() == 0 && !allowRoot) {
        std::fprintf(stderr, "refusing to run as root (it needs no privileges: run it as an ordinary user, or under systemd's DynamicUser); --allow-root to insist\n");
        return 2;
    }
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    rv::Server srv;
    srv.lim = lim;
    std::string err;
    if (!srv.open(port, bind, &err)) {
        std::fprintf(stderr, "crumplezone-rendezvous: %s\n", err.c_str());
        return 1;
    }
    std::printf("crumplezone-rendezvous: UDP %s:%d, protocol %d, relay %.0f Mbit/s (%.0f KB/s a pair), up to %d codes\n", bind.empty() ? "0.0.0.0" : bind.c_str(),
                srv.port(), rv::kProto, lim.relayBytesPerSec * 8 / 1e6, lim.pairBytesPerSec / 1024, lim.maxCodes);
    std::fflush(stdout);
    double nextStats = net::clockNow() + 60;
    while (running) {
        const double now = net::clockNow();
        srv.poll(now, 100);
        for (const std::string& l : srv.log) std::printf("%s\n", l.c_str());
        srv.log.clear();
        if (!quiet && now >= nextStats) {
            nextStats = now + 60;
            std::printf("codes %zu, pairs %zu, relayed %llu KB (%llu packets), dropped %llu, refused %llu, lookups %llu, registrations %llu\n", srv.codes(),
                        srv.pairCount(), (unsigned long long)(srv.stats.relayedBytes >> 10), (unsigned long long)srv.stats.relayedPackets,
                        (unsigned long long)srv.stats.dropped, (unsigned long long)srv.stats.refused, (unsigned long long)srv.stats.lookups,
                        (unsigned long long)srv.stats.registrations);
        }
        std::fflush(stdout);
    }
    std::printf("crumplezone-rendezvous: stopped\n");
    return 0;
}
