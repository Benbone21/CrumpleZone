// Multiplayer traffic: a dedicated-server-style host (no truck of its own) and 1, 3 or 7
// guests over loopback, each driving its own truck. What each guest downloads and uploads
// a second, and what the server sends in all, have budgets.
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

#include "../src/net.h"
#include "harness.h"

namespace {

int bwPort() { return 28300 + (int)(getpid() % 300); }

struct Traffic {
    double down = 0, up = 0, server = 0;  // bytes a second: per guest (mean), and the server's whole upload (what all the guests download)
    int sees = 0;                         // the fewest remote trucks any guest has
};

// `guests` trucks driving in circles; all at the spawn, or `spread` over the map (330 m and more apart).
Traffic measure(Ctx& ctx, int port, int guests, bool spread) {
    Traffic out;
    Sim hsim;
    hsim.world = World(MAP_PROVING_GROUND);
    hsim.setupEmpty();
    NetSession host;
    host.discovery = false;
    if (!host.host(port, "SERVER", MAP_PROVING_GROUND)) {
        ctx.check("host opens on port " + std::to_string(port), false, host.error);
        return out;
    }
    host.adopt(hsim);
    std::vector<std::unique_ptr<Sim>> sims;
    std::vector<std::unique_ptr<NetSession>> nets;
    double t = 0;
    // The host judges guests by the clock (messages a second), so frames must not run ahead of it: each one
    // waits until its time has come. (A bare usleep(200) is a no-op where sleeps are coarse, as on Windows,
    // and the first guest got dropped for "sending too much".)
    const auto wall0 = std::chrono::steady_clock::now();
    auto frame = [&](bool drive) {
        host.receive(hsim, t);
        host.send(hsim, t);
        for (size_t k = 0; k < nets.size(); k++) {
            Sim& s = *sims[k];
            NetSession& n = *nets[k];
            if (n.joining()) continue;
            n.receive(s, t);
            s.drive(drive, false, drive && k % 2, drive && !(k % 2), false, Ctx::kFrame);
            s.advance(Ctx::kFrame, 1000);
            for (Car& c : s.cars) c.updateStats();
            n.send(s, t);
        }
        t += Ctx::kFrame;
        std::this_thread::sleep_until(wall0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(t)));
    };
    for (int g = 0; g < guests; g++) {
        sims.emplace_back(new Sim);
        Sim& s = *sims.back();
        s.world = World(MAP_PROVING_GROUND);
        s.setupEmpty();
        s.addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
        s.player = 0;
        nets.emplace_back(new NetSession);
        NetSession& n = *nets.back();
        if (!n.beginJoin("127.0.0.1:" + std::to_string(port), "G" + std::to_string(g))) return out;
        int st = 0;
        for (int k = 0; k < 2000 && st == 0; k++) {
            st = n.pollJoin();
            frame(false);
        }
        if (st != 1) {
            ctx.check("guest " + std::to_string(g) + " joins", false, n.error);
            return out;
        }
        n.adopt(s);
        if (spread) {
            Car& c = s.cars[s.player];
            const double x = -500 + 330 * (g % 4), z = g < 4 ? -450 : 450;
            c.respawn(V3(x, s.world.clearY(x, z) + 0.05, z), 0);
            c.setDown(s.world);
        }
    }
    for (int k = 0; k < 120; k++) frame(false);  // everyone in, everyone seen
    std::vector<uint64_t> in0, out0;
    for (auto& n : nets) in0.push_back(n->bytesIn), out0.push_back(n->bytesOut);
    const uint64_t server0 = host.bytesOut;
    const double t0 = t;
    for (int k = 0; k < 180; k++) frame(true);
    const double secs = t - t0;
    out.sees = 1 << 20;
    for (size_t k = 0; k < nets.size(); k++) {
        out.down += (nets[k]->bytesIn - in0[k]) / secs / guests;
        out.up += (nets[k]->bytesOut - out0[k]) / secs / guests;
        int remote = 0;
        for (const Car& c : sims[k]->cars) remote += c.remote;
        out.sees = std::min(out.sees, remote);
    }
    out.server = out.down * guests;  // (everything the guests get comes from the server)
    (void)server0;
    for (auto& n : nets) n->close();
    host.close();
    return out;
}

}  // namespace

SCENARIO(netBandwidth, "net/bandwidth", "Bytes a second per guest and from the server, with 2, 4 and 8 players (the server's slot is one) all together and spread over the map") {
    const int port = bwPort();
    int p = 0;
    for (int players : {2, 4, 8}) {
        const int guests = players - 1;
        const Traffic near = measure(ctx, port + p++, guests, false);
        const std::string n = std::to_string(players) + " players";
        ctx.check(n + ": every guest sees every other truck", near.sees == guests - 1, std::to_string(near.sees));
        // Budgets (0.2.0, whole states relayed as they came: 284 KB/s down per guest with 8, 1987 from the server).
        ctx.metric(n + " together: download per guest", near.down / 1024, "KB/s", 0, 15.0 * (guests - 1) + 2);
        ctx.metric(n + " together: upload per guest", near.up / 1024, "KB/s", 0, 20);
        ctx.metric(n + " together: server upload", near.server / 1024, "KB/s", 0, 15.0 * guests * (guests - 1) + 2 * guests);
        if (players == 8) {
            const Traffic far = measure(ctx, port + p++, guests, true);
            ctx.check(n + " spread out: every guest sees every other truck", far.sees == guests - 1, std::to_string(far.sees));
            ctx.metric(n + " spread out: download per guest", far.down / 1024, "KB/s", 0, 20);  // (far trucks 2 to 10 times a second)
            ctx.metric(n + " spread out: server upload", far.server / 1024, "KB/s", 0, 140);
        }
    }
}
