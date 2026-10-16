// Crumple Zone dedicated server.
// Hosts a multiplayer session without a window or player vehicle.
// Clients connect via --join HOST:PORT [--password PASSWORD].
//
// Usage: crumplezone-server --port 27015 --map 1 [--name "My Session"] [--players 8] [--password SECRET]
//
// The server hosts the session, relays vehicle states between clients, and keeps the shared
// world (parked vehicles, crash props, everyone's vehicles for whoever joins later). It runs
// no physics: each client runs the physics of its own car. Everything it hears from the
// network is checked first (net.cpp); docs/server-security.md says what and why.
//
// Run it as an ordinary user, not root, behind a firewall that lets through only the game port.

#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include "net.h"
#include "softbody.h"
#include "vehicles.h"
#include "world.h"

namespace {

volatile sig_atomic_t running = 1;
void handleSignal(int /*sig*/) {
    running = 0;
}

struct Options {
    int port = 27015;
    int map = 1;  // mountain
    std::string name = "Crumple Zone Server";
    int maxPlayers = 8;
    std::string password, passwordFile, bind;
    bool lan = false, allowRoot = false;
    bool bad = false;
};

Options parseArgs(int argc, char** argv) {
    Options opts;
    if (const char* env = std::getenv("CZ_PASSWORD")) opts.password = env;  // (not in `ps`, unlike --password)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--port" && more) {
            opts.port = atoi(argv[++i]);
        } else if (arg == "--map" && more) {
            opts.map = atoi(argv[++i]);
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
        } else if (arg == "--lan") {
            opts.lan = true;
        } else if (arg == "--allow-root") {
            opts.allowRoot = true;
        } else if (arg == "--help") {
            printf("Usage: crumplezone-server [OPTIONS]\n");
            printf("  --port PORT      TCP/UDP port (default 27015)\n");
            printf("  --map MAP        Map number: 0 proving ground, 1 mountain,\n");
            printf("                   2 freeway, 3 frontier, 4 backcountry (default 1)\n");
            printf("  --name NAME      Session name in lobby (default \"Crumple Zone Server\")\n");
            printf("  --players N      Slots, the server's own included (default 8, max %d: 7 guests)\n", net::kMaxPeers);
            printf("  --password PW    Players must give this to join (strongly recommended on the internet).\n");
            printf("                   Visible in `ps`: prefer --password-file FILE or the CZ_PASSWORD variable.\n");
            printf("  --password-file FILE   Read the password from the first line of FILE\n");
            printf("  --bind IP        Listen only on this local address (default: every interface)\n");
            printf("  --lan            Also answer \"who's hosting?\" on the local network (off by default:\n");
            printf("                   for a game on your own network, not a public server)\n");
            printf("  --allow-root     Run as root anyway (don't)\n");
            printf("  --help           Show this help\n");
            exit(0);
        } else {
            fprintf(stderr, "unknown or incomplete option: %s (try --help)\n", arg.c_str());
            opts.bad = true;
        }
    }
    return opts;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts = parseArgs(argc, argv);
    if (opts.bad) return 2;
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
    if (opts.port < 1 || opts.port > 65535 || opts.map < 0 || opts.map >= MAP_COUNT || opts.maxPlayers < 2 || opts.maxPlayers > net::kMaxPeers) {
        fprintf(stderr, "bad option: port 1..65535, map 0..%d, players 2..%d\n", MAP_COUNT - 1, net::kMaxPeers);
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
    if (opts.password.empty())
        printf("WARNING: no password. Anyone who can reach this port can join. Use --password-file or CZ_PASSWORD.\n");
    else if (opts.password.size() < 8)
        printf("WARNING: that password is short; players' traffic is not encrypted, but the password is never sent in the clear.\n");
    fflush(stdout);

    // Signal handling for graceful shutdown
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGPIPE, SIG_IGN);

    // Create the world and parked vehicles
    Sim sim;
    World::withShowcase = true;  // (the same proving ground the players' games build: the model yard, the section park)
    sim.world = World(opts.map);
    sim.lineup = listedVehicles();
    sim.setup();
    // (The server runs no physics: its parked trucks stay as built, and are told to the others
    // at the sleeping rate rather than twenty times a second.)
    for (Car& c : sim.cars) c.sleeping = true;

    // Host the session
    NetSession net;
    net.password = opts.password;
    net.discovery = opts.lan;
    net.bindAddress = opts.bind;
    if (!net.host(opts.port, "SERVER", opts.map, opts.maxPlayers, sessionName)) {
        fprintf(stderr, "Failed to host on port %d: %s\n", opts.port, net.error.c_str());
        return 1;
    }
    net.adopt(sim);  // (so what the guests spawn is kept, and told to whoever joins later)
    printf("Hosting on port %d\n", opts.port);
    printf("Tell players to join with: --join <YOUR_PUBLIC_IP>:%d%s\n", opts.port, opts.password.empty() ? "" : " --password <the password>");
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    // Main loop: handle network, send states to clients
    auto start = std::chrono::steady_clock::now();
    int lastPlayerCount = 0;

    while (running) {
        // Convert current time to seconds since start
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        double t = elapsed.count() / 1000000.0;

        // Handle network input and send states
        net.receive(sim, t);
        net.send(sim, t);

        // The game shows these on the HUD and the race screen; here they're just log lines.
        for (const std::string& e : net.events) printf("%s\n", e.c_str());
        net.events.clear();
        for (const std::string& e : net.log) printf("[net] %s\n", e.c_str());
        net.log.clear();
        net.gameIn.clear();

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
