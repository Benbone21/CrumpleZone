// Crumple Zone dedicated server.
// Hosts a multiplayer session without a window or player vehicle.
// Clients connect via --join HOST:PORT [--password PASSWORD].
//
// Usage: crumplezone-server [--port PORT (27015, or CZ_PORT)] --map 1 [--name "My Session"] [--players 8] [--password SECRET]
//                           [--time HOURS] [--day-length MINUTES] [--frozen]
//
// The server hosts the session, relays vehicle states between clients, and keeps the shared
// world (parked vehicles, crash props, everyone's vehicles for whoever joins later). It runs
// no physics: each client runs the physics of its own car. Everything it hears from the
// network is checked first (net.cpp); docs/server-security.md says what and why. It owns the
// session's clock, and the time of day on it: every player sees the same sky.
//
// Run it as an ordinary user, not root, behind a firewall that lets through only the game port.

#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "bgwork.h"
#include "daylight.h"
#include "net.h"
#include "procrules.h"
#include "procworld.h"
#include "props.h"
#include "softbody.h"
#include "vehicles.h"
#include "onemap.h"
#include "world.h"
#include "realterrain.h"

namespace {

volatile sig_atomic_t running = 1;
void handleSignal(int /*sig*/) {
    running = 0;
}

struct Options {
    int port = net::kDefaultPort;
    int map = 1;  // mountain (a start on the big Backcountry: onemap.h)
    unsigned long long seed = 0;  // the procedural world's (map 6; 0: a new one)
    std::string name = "Crumple Zone Server";
    int maxPlayers = 8;
    std::string password, passwordFile, bind;
    std::string codeServer;  // --code HOST[:PORT]: also take a join code from that join-code server
    bool lan = false, allowRoot = false;
    double hours = 10, dayMinutes = 24;  // the time of day it starts at, and a whole day in real minutes
    bool frozen = false;                 // the clock holds at `hours`
    bool bad = false;
};

// A plain number from lo to hi ("12", "17.5"; not "12h", "nan" or "").
bool takeNumber(const char* what, const std::string& text, double lo, double hi, double& out) {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    bool plain = !text.empty() && end && *end == 0;
    for (char ch : text) plain &= (ch >= '0' && ch <= '9') || ch == '.';
    if (!plain || !(v >= lo && v <= hi)) {
        fprintf(stderr, "bad %s \"%s\": a number from %g to %g\n", what, text.c_str(), lo, hi);
        return false;
    }
    out = v;
    return true;
}

// A port for the game (TCP and UDP): 1024 to 65535, plain digits.
bool takePort(const char* what, const std::string& text, int& port, bool quiet = false) {
    const int p = net::parsePort(text, net::kMinHostPort);
    if (p < 0) {
        if (!quiet) fprintf(stderr, "bad %s \"%s\": the port must be a number from %d to %d (default %d)\n", what, text.c_str(), net::kMinHostPort, net::kMaxPort,
                net::kDefaultPort);
        return false;
    }
    port = p;
    return true;
}

Options parseArgs(int argc, char** argv) {
    Options opts;
    if (const char* env = std::getenv("CZ_PASSWORD")) opts.password = env;  // (not in `ps`, unlike --password)
    const char* env = std::getenv("CZ_PORT");
    bool envBad = env && *env && !takePort("CZ_PORT", env, opts.port, true);  // (--port overrides it, so it's only a problem if nothing does)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--port" && more) {
            if (!takePort("--port", argv[++i], opts.port)) opts.bad = true;
            else envBad = false;
        } else if (arg == "--map" && more) {
            opts.map = atoi(argv[++i]);
        } else if (arg == "--seed" && more) {
            opts.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--real" && more) {  // (MAP_REAL's terrain: a folder with terrain.txt, or the sidecar: docs/real-terrain.md)
            std::string why;
            if (!realterrain::selectPath(argv[++i], &why)) {
                fprintf(stderr, "--real %s: %s\n", argv[i], why.c_str());
                opts.bad = true;
            }
        } else if (arg == "--name" && more) {
            opts.name = argv[++i];
        } else if (arg == "--players" && more) {
            opts.maxPlayers = atoi(argv[++i]);
        } else if (arg == "--password" && more) {
            opts.password = argv[++i];
        } else if (arg == "--password-file" && more) {
            opts.passwordFile = argv[++i];
        } else if (arg == "--bind" && more) {
            opts.bind = argv[++i];
        } else if (arg == "--time" && more) {
            if (!takeNumber("--time", argv[++i], 0, 24, opts.hours)) opts.bad = true;
            opts.hours = std::fmod(opts.hours, 24.0);
        } else if (arg == "--day-length" && more) {
            if (!takeNumber("--day-length", argv[++i], 1, 1440, opts.dayMinutes)) opts.bad = true;
        } else if (arg == "--frozen") {
            opts.frozen = true;
        } else if (arg == "--code" && more) {
            opts.codeServer = argv[++i];
        } else if (arg == "--lan") {
            opts.lan = true;
        } else if (arg == "--allow-root") {
            opts.allowRoot = true;
        } else if (arg == "--help") {
            printf("Usage: crumplezone-server [OPTIONS]\n");
            printf("  --port PORT      TCP/UDP game port, any from %d to %d (default %d; or the CZ_PORT variable)\n", net::kMinHostPort, net::kMaxPort,
                   net::kDefaultPort);
            printf("  --map MAP        Map number: 0 proving ground, 1 mountain,\n");
            printf("                   2 freeway, 3 frontier, 4 backcountry, 6 procedural (default 1)\n");
            printf("  --seed N         The procedural world's seed (map 6; default: a new world)\n");
            printf("  --name NAME      Session name in lobby (default \"Crumple Zone Server\")\n");
            printf("  --players N      Slots, the server's own included (default 8, max %d: 7 guests)\n", net::kMaxPeers);
            printf("  --password PW    Players must give this to join (strongly recommended on the internet).\n");
            printf("                   Visible in `ps`: prefer --password-file FILE or the CZ_PASSWORD variable.\n");
            printf("  --password-file FILE   Read the password from the first line of FILE\n");
            printf("  --bind IP        Listen only on this local address (default: every interface)\n");
            printf("  --time HOURS     Time of day it starts at, 0 to 24 (default 10: mid-morning; 13.5 is 1:30 pm)\n");
            printf("  --day-length MIN A whole day takes this many real minutes, 1 to 1440 (default 24)\n");
            printf("  --frozen         The clock holds at --time (always that time of day)\n");
            printf("  --lan            Also answer \"who's hosting?\" on the local network (off by default:\n");
            printf("                   for a game on your own network, not a public server)\n");
            printf("  --code HOST[:PORT]  Also take a join code from that join-code server (crumplezone-rendezvous,\n");
            printf("                   UDP 28015 by default): players join with the code, no port forwarding\n");
            printf("  --allow-root     Run as root anyway (don't)\n");
            printf("  --help           Show this help\n");
            exit(0);
        } else {
            fprintf(stderr, "unknown or incomplete option: %s (try --help)\n", arg.c_str());
            opts.bad = true;
        }
    }
    if (envBad) takePort("CZ_PORT", env, opts.port), opts.bad = true;  // (says why)
    return opts;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts = parseArgs(argc, argv);
    if (opts.bad) return 2;
    proc::loadShippedRules();  // (before any land is made)
    if (!opts.passwordFile.empty()) {
        std::ifstream f(opts.passwordFile);
        std::string line;
        if (!f || !std::getline(f, line)) {
            fprintf(stderr, "can't read the password from %s\n", opts.passwordFile.c_str());
            return 2;
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        opts.password = line;
    }
    if (opts.map < 0 || opts.map >= MAP_COUNT || opts.maxPlayers < 2 || opts.maxPlayers > net::kMaxPeers) {
        fprintf(stderr, "bad option: map 0..%d, players 2..%d\n", MAP_COUNT - 1, net::kMaxPeers);
        return 2;
    }
    if (geteuid() == 0 && !opts.allowRoot) {
        fprintf(stderr, "Refusing to run as root: a bug in a network service should not own the machine.\n"
                        "Run it as an ordinary user (docs/server-security.md has a systemd unit), or pass --allow-root.\n");
        return 2;
    }

    const std::string sessionName = net::sanitize(opts.name, 40);
    printf("Crumple Zone Dedicated Server\n");
    printf("  Port:       %d (TCP and UDP)%s\n", opts.port, opts.bind.empty() ? "" : (", on " + opts.bind).c_str());
    printf("  Map:        %d\n", opts.map);
    printf("  Session:    %s\n", sessionName.c_str());
    printf("  Players:    up to %d\n", opts.maxPlayers - 1);
    printf("  Password:   %s\n", opts.password.empty() ? "NONE" : "yes");
    if (opts.frozen) printf("  Time:       %02d:%02d, held\n", (int)opts.hours, (int)(std::fmod(opts.hours, 1.0) * 60));
    else printf("  Time:       from %02d:%02d, a day every %g min\n", (int)opts.hours, (int)(std::fmod(opts.hours, 1.0) * 60), opts.dayMinutes);
    if (opts.password.empty())
        printf("WARNING: no password. Anyone who can reach this port can join. Use --password-file or CZ_PASSWORD.\n");
    else if (opts.password.size() < 8)
        printf("WARNING: that password is short; players' traffic is not encrypted, but the password is never sent in the clear.\n");
    fflush(stdout);

    // Signal handling for graceful shutdown
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGPIPE, SIG_IGN);

    // The scanned rocks, as the players' games have them (their hulls are what cars hit): without the game's assets/props
    // beside the server, code-made rocks stand in, and the rocks aren't where the players' are (World::landHash differs).
    props::setActiveRockScans(&props::sharedHullLibrary());
    std::printf("  Rocks:      %s\n", props::activeRockScans() ? "scanned (assets/props)" : "code-made (no assets/props found: players' rocks will differ)");
    // Create the world and parked vehicles
    Sim sim;
    World::useLocalEdits = false;  // (the players' games build it the same way: no saved edits)
    World::withShowcase = true;  // (the same proving ground the players' games build: the model yard, the section park)
    if (opts.map == MAP_PROCEDURAL) {  // (an endless world from a seed: the server slides its window with the players, procworld.h)
        World::procSeed = opts.seed ? opts.seed : proc::ProcWorld::randomSeed();
        printf("  Seed:       %llu\n", (unsigned long long)World::procSeed);
    }
    sim.world = worldForPick(opts.map);  // (the Mountain, the Frontier, the Open Country: the big map, started there: onemap.h)
    sim.lineup = listedVehicles();
    sim.setup();
    // (The server runs no physics: its parked trucks stay as built, and are told to the others
    // at the sleeping rate rather than twenty times a second.)
    for (Car& c : sim.cars) c.sleeping = true;
    // Only what the server uses of the world stays: the land, what's solid on it, the spawn pads and the shops (it puts a
    // joining player down on it, and relays the rest). What's only drawn goes, and the memory the build used is handed back.
    sim.world.trimForServer();
#ifdef __GLIBC__
    malloc_trim(0);
#endif

    // Host the session
    NetSession net;
    net.password = opts.password;
    net.discovery = opts.lan;
    net.bindAddress = opts.bind;
    if (!net.host(opts.port, "SERVER", sim.world.map, opts.maxPlayers, sessionName)) {
        if (net.portInUse) fprintf(stderr, "Port %d is in use: pick another with --port PORT (or CZ_PORT)\n", opts.port);
        else fprintf(stderr, "Failed to host on port %d: %s\n", opts.port, net.error.c_str());
        return 1;
    }
    net.adopt(sim);  // (so what the guests spawn is kept, and told to whoever joins later)
    printf("Hosting on port %d\n", opts.port);
    printf("Tell players to join with: --join <YOUR_PUBLIC_IP>:%d%s\n", opts.port, opts.password.empty() ? "" : " --password <the password>");
    if (!opts.codeServer.empty()) {
        net.rendezvous = opts.codeServer;
        if (!net.openToFriends()) printf("No join code: %s\n", net.codeError.c_str());
    }
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    // Main loop: handle network, send states to clients
    auto start = std::chrono::steady_clock::now();
    int lastPlayerCount = 0;
    std::future<std::unique_ptr<World>> nextWindow;  // (the procedural world's next window, on its way)

    while (running) {
        // Convert current time to seconds since start
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        double t = elapsed.count() / 1000000.0;

        // The sky everyone shares (sent on joining and every second, stamped with the session's clock).
        net.dayRate = opts.frozen ? 0 : clockAdvance(1.0, opts.dayMinutes);
        net.dayHours = std::fmod(opts.hours + net.dayRate * t, 24.0);

        // Handle network input and send states
        net.receive(sim, t);
        net.send(sim, t);
        // The procedural world: its window follows the players (the middle of the box round their vehicles), told 3 s ahead and
        // swapped by everyone at that moment on the shared clock. (Built on a worker; the server only keeps the land and the solids.)
        if (sim.world.map == MAP_PROCEDURAL) {
            net.proc.on = true, net.proc.gen = proc::genStamp(), net.proc.seed = sim.world.proc.seed;
            net.proc.ox = sim.world.proc.ox, net.proc.oz = sim.world.proc.oz;
            const double shared = net.sharedTime(t);
            if (!net.shiftPending()) {
                V3 lo(1e18, 0, 1e18), hi(-1e18, 0, -1e18);
                for (const Car& c : sim.cars)
                    if (c.remote && !c.isProp()) {
                        const V3 p = c.frame().com;
                        lo = V3(std::min(lo.x, p.x), 0, std::min(lo.z, p.z)), hi = V3(std::max(hi.x, p.x), 0, std::max(hi.z, p.z));
                    }
                const V3 focus = (lo + hi) * 0.5;
                if (lo.x < 1e17 && std::max(std::fabs(focus.x), std::fabs(focus.z)) >= proc::kSlide) {
                    int64_t ox, oz;
                    proc::ProcWorld::snapOrigin((double)sim.world.proc.ox + focus.x, (double)sim.world.proc.oz + focus.z, ox, oz);
                    const double due = shared + 3.0;
                    char msg[160];
                    std::snprintf(msg, sizeof msg, "procshift %llu %lld %lld %.4f", (unsigned long long)sim.world.proc.seed, (long long)ox, (long long)oz, due);
                    net.sendGame(msg);
                    net.scheduleShift(due, V3((double)(sim.world.proc.ox - ox), 0, (double)(sim.world.proc.oz - oz)));
                    const uint64_t seed = sim.world.proc.seed;
                    const props::HullLibrary* hulls = props::activeRockScans();
                    nextWindow = std::async(std::launch::async, [=] {
                        props::setActiveRockScans(hulls);
                        World::procSeed = seed, World::procOriginSet = true, World::procOriginX = ox, World::procOriginZ = oz;
                        auto w = std::make_unique<World>(MAP_PROCEDURAL);
                        w->trimForServer();
                        return w;
                    });
                }
            } else if (shared >= net.shiftDue() && nextWindow.valid()) {
                std::unique_ptr<World> w = nextWindow.get();
                const V3 d((double)(sim.world.proc.ox - w->proc.ox), 0, (double)(sim.world.proc.oz - w->proc.oz));
                waitWorldReaders();  // (background work reading the world: not while it changes)
                sim.world = std::move(*w);
                proc::shiftSim(sim, d);
                net.shiftNow();
                printf("window -> %lld,%lld\n", (long long)sim.world.proc.ox, (long long)sim.world.proc.oz);
                fflush(stdout);
            }
        }

        // The game shows these on the HUD and the race screen; here they're just log lines.
        for (const std::string& e : net.events) printf("%s\n", e.c_str());
        if (!net.events.empty()) fflush(stdout);
        net.events.clear();
        for (const std::string& e : net.log) printf("[net] %s\n", e.c_str());
        net.log.clear();
        net.gameIn.clear();
        if (net.codeState == NetSession::CODE_FAILED && !opts.codeServer.empty()) {  // (said once; a restart tries again)
            printf("No join code: %s\n", net.codeError.c_str());
            opts.codeServer.clear();
        }

        // Periodically report player count
        int count = net.playerCount() - 1;  // (the server itself is player 0)
        if (count != lastPlayerCount) {
            printf("Players: %d/%d\n", count, opts.maxPlayers - 1);
            for (int k = 1; k < net::kMaxPeers; k++) {
                if (net.players[k].active) {
                    printf("  [%d] %s (%.0f ms)\n", k, net.players[k].name.c_str(),
                           net.players[k].ping * 1000.0);
                }
            }
            lastPlayerCount = count;
            fflush(stdout);
        }

        // Sleep a bit to avoid burning CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    net.close();
    printf("Server stopped.\n");
    return 0;
}
