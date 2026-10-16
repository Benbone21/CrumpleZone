// Crumple Zone dedicated server.
// Hosts a multiplayer session without a window or player vehicle.
// Clients connect via --join HOST:PORT.
//
// Usage: crumplezone-server --port 27015 --map 1 [--name "My Session"] [--players 8]
//
// This is intentionally simple: the server hosts the session, relays vehicle
// states between clients, and maintains the shared world (parked vehicles,
// crash props). Each client runs physics for their own car.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "net.h"
#include "softbody.h"
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
};

Options parseArgs(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            opts.port = atoi(argv[++i]);
        } else if (arg == "--map" && i + 1 < argc) {
            opts.map = atoi(argv[++i]);
        } else if (arg == "--name" && i + 1 < argc) {
            opts.name = argv[++i];
        } else if (arg == "--players" && i + 1 < argc) {
            opts.maxPlayers = atoi(argv[++i]);
        } else if (arg == "--help") {
            printf("Usage: crumplezone-server [OPTIONS]\n");
            printf("  --port PORT      TCP/UDP port (default 27015)\n");
            printf("  --map MAP        Map number: 0 proving ground, 1 mountain,\n");
            printf("                   2 freeway, 3 frontier, 4 backcountry (default 1)\n");
            printf("  --name NAME      Session name in lobby (default \"Crumple Zone Server\")\n");
            printf("  --players N      Max players (default 8, max %d)\n", net::kMaxPeers);
            printf("  --help           Show this help\n");
            exit(0);
        }
    }
    return opts;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts = parseArgs(argc, argv);

    printf("Crumple Zone Dedicated Server\n");
    printf("  Port:       %d\n", opts.port);
    printf("  Map:        %d\n", opts.map);
    printf("  Session:    %s\n", opts.name.c_str());
    printf("  Max players: %d\n", opts.maxPlayers);
    printf("Waiting for clients...\n");

    // Signal handling for graceful shutdown
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    // Create the world and parked vehicles
    Sim sim;
    sim.world = World(opts.map);
    sim.setup();

    // Host the session
    NetSession net;
    if (!net.host(opts.port, opts.name, opts.map, opts.maxPlayers)) {
        fprintf(stderr, "Failed to host on port %d: %s\n", opts.port, net.error.c_str());
        return 1;
    }
    printf("Hosting on port %d\n", opts.port);
    printf("Tell players to join with: --join <YOUR_PUBLIC_IP>:%d\n", opts.port);
    printf("Press Ctrl+C to stop.\n");

    // Main loop: handle network, send states to clients
    auto now = std::chrono::steady_clock::now();
    int lastPlayerCount = 0;

    while (running) {
        // Convert current time to seconds since start
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - now);
        double t = elapsed.count() / 1000000.0;

        // Handle network input and send states
        net.receive(sim, t);
        net.send(sim, t);

        // Periodically report player count
        int count = net.playerCount();
        if (count != lastPlayerCount) {
            printf("Players: %d/%d\n", count, opts.maxPlayers);
            for (int k = 0; k < net::kMaxPeers; k++) {
                if (net.players[k].active) {
                    printf("  [%d] %s (%.0f ms)\n", k, net.players[k].name.c_str(),
                           net.players[k].ping * 1000.0);
                }
            }
            lastPlayerCount = count;
        }

        // Sleep a bit to avoid burning CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    net.close();
    printf("Server stopped. Total players connected: %d\n", lastPlayerCount);
    return 0;
}
