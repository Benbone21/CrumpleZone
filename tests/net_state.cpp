// Production wire codecs, fragment assembly and playback without requiring sockets.
#include "harness.h"
#include "../src/net.h"
#include "../src/lights.h"
#include <algorithm>
#include <cstdio>
#include <deque>

struct NetSessionTest {
    static void sender(NetSession& session) { session.self = 0; session.sessionStart = 0; }
    static void spawn(NetSession& sender, NetSession& receiver, Sim& sim, const Car& car) {
        receiver.self = 1;
        auto msg = sender.spawnMsg(car);
        receiver.apply(sim, {msg.begin() + 2, msg.end()});
    }
    static std::vector<uint8_t> state(NetSession& sender, const Sim& sim, double time) {
        return sender.snapshotMsg(sim, sim.cars[0], time);
    }
    static void take(NetSession& receiver, Sim& sim, const std::vector<uint8_t>& msg, double now) {
        receiver.takeSnapshot(sim, 1, msg, now);
    }
    static void play(NetSession& receiver, Sim& sim, double now) { receiver.applyRemotes(sim, now); }
    // Any car, either way (two machines that each own one).
    static void spawnAny(NetSession& sender, NetSession& receiver, Sim& sim, const Car& car) {
        auto msg = sender.spawnMsg(car);
        receiver.apply(sim, {msg.begin() + 2, msg.end()});
    }
    static std::vector<uint8_t> stateOf(NetSession& sender, const Sim& sim, const Car& car, double time) {
        return sender.snapshotMsg(sim, car, time);
    }
    static void takeAny(NetSession& receiver, Sim& sim, uint32_t netId, const std::vector<uint8_t>& msg, double now) {
        receiver.takeSnapshot(sim, netId, msg, now);
    }
    static void lamps(NetSession& sender, NetSession& receiver, Sim& sim, const Car& car) {
        auto msg = sender.lampMsg(car);
        receiver.apply(sim, {msg.begin() + 2, msg.end()});
    }
    static void fragment(NetSession& receiver, Sim& sim, uint32_t seq, const std::vector<uint8_t>& msg, int f, double now) {
        constexpr size_t chunk = 1150;
        const size_t count = (msg.size() + chunk - 1) / chunk, offset = f * chunk;
        receiver.takeFragment(sim, 1, seq, (uint8_t)f, (uint8_t)count, msg.data() + offset,
                              std::min(chunk, msg.size() - offset), now);
    }
};

namespace {
void source(Sim& sim) {
    sim.setupEmpty();
    sim.addCar(vehicleCatalog()[0], V3(-40, 0.05, -40), 0, V3(0.4, 0.5, 0.6));
    sim.cars[0].netId = 1;
}
}

SCENARIO(netEngineState, "net/state-engine-lamps", "Engine telemetry, smooth controls, reverse lamps and reliable headlight switches survive the wire format") {
    Sim origin;
    source(origin);
    NetSession sender, receiver;
    NetSessionTest::sender(sender);
    receiver.predict = false;  // (shown at the view: between the two states)
    Car& car = origin.cars[0];
    car.lampMode = LAMPS_OFF;
    NetSessionTest::spawn(sender, receiver, ctx.sim, car);
    Car& remote = ctx.sim.cars[0];
    ctx.check("spawn origin does not add the center-of-mass height twice", len(remote.frame().com - car.frame().com) < 1e-4);
    ctx.check("late join gets the owner's OFF switch", remote.lampMode == LAMPS_OFF);
    car.rpm = 3000; car.throttle = 0.8; car.tcCut = 0.6; car.boost = 0.4; car.gear = -1;
    car.steer = 0.1; car.brake = 0.2; car.damage = 0.3;
    NetSessionTest::take(receiver, ctx.sim, NetSessionTest::state(sender, origin, 0), 0);
    car.rpm = 5000; car.steer = 0.3;
    NetSessionTest::take(receiver, ctx.sim, NetSessionTest::state(sender, origin, 0.05), 0.05);
    NetSessionTest::play(receiver, ctx.sim, 0.125);
    ctx.check("RPM and steering interpolated between packets", std::fabs(remote.rpm - 4000) < 0.01 && std::fabs(remote.steer - 0.2) < 1e-6);
    ctx.check("engine load and boost transmitted", std::fabs(remote.throttle - 0.48) < 1e-4 && std::fabs(remote.boost - 0.4) < 1e-4);  // (16 bits each)
    ctx.check("reverse gear and damage transmitted", remote.gear == -1 && std::fabs(remote.damage - 0.3) < 1e-4);
    bool switches = true;
    for (int mode : {LAMPS_HIGH, LAMPS_LOW, LAMPS_OFF, LAMPS_AUTO}) {
        car.lampMode = (uint8_t)mode;
        NetSessionTest::lamps(sender, receiver, ctx.sim, car);
        switches &= remote.lampMode == mode;
        NetSessionTest::play(receiver, ctx.sim, 0.13);
        switches &= remote.lampMode == mode;  // Older buffered snapshots cannot revert a reliable switch.
    }
    ctx.check("every switch position takes effect immediately and stays set", switches);
}

SCENARIO(netJitter, "net/state-jitter", "Variable latency and reordered states do not reverse remote motion or make wheel animations jump") {
    Sim origin;
    source(origin);
    NetSession sender, receiver;
    NetSessionTest::sender(sender);
    NetSessionTest::spawn(sender, receiver, ctx.sim, origin.cars[0]);
    const V3 start = origin.cars[0].frame().com;
    struct Packet { double arrival; std::vector<uint8_t> data; };
    std::vector<Packet> packets;
    const double delays[] = {0.02, 0.055, 0.015, 0.08, 0.025, 0.01};
    for (int k = 0; k <= 40; k++) {
        const double time = k * 0.05;
        Car& c = origin.cars[0];
        c.translate(start + V3(time * 10, 0, 0) - c.frame().com);
        for (Node& n : c.nodes) n.v = V3(10, 0, 0);
        for (Wheel& w : c.wheels) { w.angle = time * 20; w.omega = 20; }
        packets.push_back({time + delays[k % 6], NetSessionTest::state(sender, origin, time)});
    }
    std::sort(packets.begin(), packets.end(), [](const auto& a, const auto& b) { return a.arrival < b.arrival; });
    size_t next = 0;
    double lastX = 0, maxStep = 0, worstReverse = 0, lastAngle = 0, maxAngleStep = 0;
    for (int frame = 0; frame < 250; frame++) {
        const double now = frame / 120.0;
        while (next < packets.size() && packets[next].arrival <= now) {
            NetSessionTest::take(receiver, ctx.sim, packets[next].data, packets[next].arrival);
            next++;
        }
        NetSessionTest::play(receiver, ctx.sim, now);
        const double x = ctx.sim.cars[0].frame().com.x, angle = ctx.sim.cars[0].wheels[0].angle;
        if (frame > 30) {
            maxStep = std::max(maxStep, x - lastX);
            worstReverse = std::max(worstReverse, lastX - x);
            maxAngleStep = std::max(maxAngleStep, std::fabs(angle - lastAngle));
        }
        lastX = x; lastAngle = angle;
    }
    ctx.metric("worst backward jump from jitter", worstReverse, "m", 0, 1e-5);
    ctx.metric("largest move at 120 Hz and 10 m/s", maxStep, "m", 0.05, 0.15);
    ctx.metric("largest wheel animation step", maxAngleStep, "rad", 0.1, 0.3);
    NetSessionTest::play(receiver, ctx.sim, 2.6);  // Just past the coasting limit (behind the view delay).
    const V3 frozen = ctx.sim.cars[0].frame().com;
    ctx.sim.advance(1.0 / 60, 1000);
    ctx.check("prolonged packet loss stops remote coasting", len(ctx.sim.cars[0].frame().com - frozen) < 1e-9);
    Car& car = origin.cars[0];
    car.translate(V3(100, 0, 0));
    for (Node& n : car.nodes) n.v = V3();
    NetSessionTest::take(receiver, ctx.sim, NetSessionTest::state(sender, origin, 3.05), 3.07);
    NetSessionTest::play(receiver, ctx.sim, 3.07);
    ctx.check("recovery teleports directly to the new position", len(ctx.sim.cars[0].frame().com - car.frame().com) < 0.01);
    ctx.check("teleport does not invent collision velocity", len(ctx.sim.cars[0].frame().vel) < 1e-6);
}

SCENARIO(netFragments, "net/state-fragments", "Overlapping, reordered multipart snapshots finish independently and old builds are ignored") {
    Sim origin;
    source(origin);
    NetSession sender, receiver;
    NetSessionTest::sender(sender);
    NetSessionTest::spawn(sender, receiver, ctx.sim, origin.cars[0]);
    origin.cars[0].rpm = 2000;
    const auto older = NetSessionTest::state(sender, origin, 0.1);
    origin.cars[0].rpm = 4000;
    const auto newer = NetSessionTest::state(sender, origin, 0.15);
    const int count = (int)((older.size() + 1149) / 1150);
    ctx.check("real vehicle needs multiple datagrams", count > 1);
    for (int f = 0; f < count - 1; f++) NetSessionTest::fragment(receiver, ctx.sim, 5, older, f, 0.12);
    NetSessionTest::fragment(receiver, ctx.sim, 6, newer, 0, 0.17);
    NetSessionTest::fragment(receiver, ctx.sim, 5, older, count - 1, 0.18);
    NetSessionTest::play(receiver, ctx.sim, 0.18);
    ctx.check("newer partial snapshot did not discard the previous state", ctx.sim.cars[0].rpm == 2000);
    for (int f = count - 1; f > 0; f--) NetSessionTest::fragment(receiver, ctx.sim, 6, newer, f, 0.19);
    NetSessionTest::play(receiver, ctx.sim, 0.4);
    ctx.check("newer snapshot also finishes out of fragment order", ctx.sim.cars[0].rpm == 4000);
    origin.cars[0].respawn(V3(20, 0.05, -40), 0);
    NetSessionTest::spawn(sender, receiver, ctx.sim, origin.cars[0]);
    const V3 rebuilt = ctx.sim.cars[0].frame().com;
    NetSessionTest::take(receiver, ctx.sim, newer, 0.5);
    NetSessionTest::play(receiver, ctx.sim, 0.5);
    ctx.check("queued states from before a rebuild cannot move the new car", len(ctx.sim.cars[0].frame().com - rebuilt) < 1e-9);
    auto truncated = NetSessionTest::state(sender, origin, 0.55);
    truncated.resize(truncated.size() - 1);
    NetSessionTest::take(receiver, ctx.sim, truncated, 0.56);
    NetSessionTest::play(receiver, ctx.sim, 0.6);
    ctx.check("truncated state was ignored", len(ctx.sim.cars[0].frame().com - rebuilt) < 1e-9);
}

namespace {
struct Machine {
    Sim sim;
    NetSession net;
};
struct NetCrash {
    double striker = 0, struck = 0;  // damage each owner has on its own vehicle
    double drift = 0;                // worst gap, at the end, between a copy and its owner's truck (m)
    bool finite = true;
};
// Two machines, one Blazer each: mine drives at 50 km/h into the other player's, parked. Each
// runs its own physics; states go both ways at 20 Hz, `lag` s on the wire. `lag` < 0: both on
// one machine (what it should come to).
NetCrash netCrash(double lag, bool tbone, double offset = 0) {
    const VehicleSpec& s = vehicleCatalog()[7];
    Machine m1, m2;
    for (Machine* m : {&m1, &m2}) {
        m->sim.world = World(MAP_PROVING_GROUND);
        m->sim.setupEmpty();
    }
    NetSessionTest::sender(m1.net);
    NetSessionTest::sender(m2.net);
    m2.net.self = 1;
    const bool alone = lag < 0;
    Machine& mb = alone ? m1 : m2;
    const double z = -50, xb = 0, xa = -(s.length / 2 + (tbone ? s.width : s.length) / 2 + 6);
    m1.sim.addCar(s, V3(xa, m1.sim.world.clearY(xa, z + offset) + 0.6, z + offset), 0, V3(0.8, 0.2, 0.2));
    mb.sim.addCar(s, V3(xb, mb.sim.world.clearY(xb, z) + 0.6, z), (tbone ? 90 : 180) * M_PI / 180, V3(0.2, 0.2, 0.8));
    const uint32_t idA = 1, idB = (1u << 16) | 1;
    m1.sim.cars[0].netId = idA;
    mb.sim.cars.back().netId = idB;
    m1.sim.player = m2.sim.player = 0;
    // Settle first, then tell each other.
    for (int f = 0; f < 60; f++)
        for (Machine* m : {&m1, &m2}) m->sim.advance(Ctx::kFrame, 1000);
    if (!alone) {
        NetSessionTest::spawnAny(m1.net, m2.net, m2.sim, m1.sim.cars[0]);
        NetSessionTest::spawnAny(m2.net, m1.net, m1.sim, m2.sim.cars[0]);
    }
    struct Packet {
        double arrival;
        int to;
        uint32_t id;
        std::vector<uint8_t> data;
    };
    std::deque<Packet> wire;
    Car& a = m1.sim.cars[0];
    Car& b = alone ? m1.sim.cars[1] : m2.sim.cars[0];
    double t = 0;
    auto frame = [&](int f) {
        t += Ctx::kFrame;
        while (!wire.empty() && wire.front().arrival <= t) {
            Packet& p = wire.front();
            Machine& to = p.to == 1 ? m1 : m2;
            NetSessionTest::takeAny(to.net, to.sim, p.id, p.data, p.arrival);
            wire.pop_front();
        }
        if (!alone) {
            NetSessionTest::play(m1.net, m1.sim, t);
            NetSessionTest::play(m2.net, m2.sim, t);
        }
        for (Machine* m : {&m1, &m2}) {
            m->sim.advance(Ctx::kFrame, 1000);
            for (Car& c : m->sim.cars)
                if (!c.remote) c.updateStats();
        }
        if (f % 3 == 0 && !alone) {
            wire.push_back({t + lag, 2, idA, NetSessionTest::stateOf(m1.net, m1.sim, a, t)});
            wire.push_back({t + lag, 1, idB, NetSessionTest::stateOf(m2.net, m2.sim, b, t)});
        }
    };
    for (int f = 0; f < 60; f++) frame(f);  // (the copies catch up)
    Frame fr = a.frame();
    const V3 dir = norm(V3(fr.fwd.x, 0, fr.fwd.z));
    for (Node& n : a.nodes) n.v = dir * (50 / 3.6);
    for (Wheel& w : a.wheels) w.omega = 50 / 3.6 / w.R;
    a.gear = 2;
    a.wake();
    NetCrash r;
    for (int f = 60; f < 60 + 360; f++) {
        if (f == 60 + 150) a.brake = b.brake = 1;  // (both drivers stand on it after)
        frame(f);
    }
    r.striker = a.damage, r.struck = b.damage;
    for (Machine* m : {&m1, &m2})
        for (const Car& c : m->sim.cars) r.finite &= c.finite();
    if (!alone) {
        // Both stopped by now: each machine's copy sits where its owner has the truck.
        r.drift = std::max(len(m2.sim.cars[1].frame().com - a.frame().com), len(m1.sim.cars[1].frame().com - b.frame().com));
    }
    return r;
}
}

// A single crash is a poor yardstick: dead centre, two Blazers' noses happen to fold up
// neatly (3 % each, against 7-10 % a few centimetres either side), so one line's ratio swings
// by half. Averaged over several lines either side, the two machines' crash is the one
// machine's, and neither owner's truck takes the lion's share.
SCENARIO(netCrashShare, "net/crash-share", "Two players' Blazers, one at 50 km/h into the other parked, over several lines: each owner's damage comes out as on one machine (not the striker wrecked, the struck one barely touched)") {
    for (bool tbone : {false, true}) {
        const std::string what = tbone ? "t-bone" : "head-on";
        const std::vector<double> lines = tbone ? std::vector<double>{-0.2, 0.0, 0.2} : std::vector<double>{-0.3, -0.15, 0.0, 0.15, 0.3};
        NetCrash one;
        for (double off : lines) {
            const NetCrash r = netCrash(-1, tbone, off);
            one.striker += r.striker / lines.size(), one.struck += r.struck / lines.size();
        }
        ctx.note(what + " on one machine: striker " + std::to_string(one.striker * 100) + " %, struck " + std::to_string(one.struck * 100) + " % (mean)");
        for (double lag : {0.0, 0.06}) {
            NetCrash r;
            for (double off : lines) {
                const NetCrash k = netCrash(lag, tbone, off);
                r.striker += k.striker / lines.size(), r.struck += k.struck / lines.size();
                r.drift = std::max(r.drift, k.drift);
                r.finite &= k.finite;
            }
            const std::string name = what + ", " + std::to_string((int)std::lround(lag * 1000)) + " ms";
            ctx.note(name + ": striker " + std::to_string(r.striker * 100) + " %, struck " + std::to_string(r.struck * 100) + " % (mean)");
            ctx.metric(name + ": striker's damage / on one machine", r.striker / std::max(one.striker, 1e-6), "", 0.7, 1.4);
            ctx.metric(name + ": struck one's damage / on one machine", r.struck / std::max(one.struck, 1e-6), "", 0.7, 1.4);
            // (What the players saw: the striker wrecked, the struck one barely touched.)
            ctx.metric(name + ": striker's damage / struck one's", r.striker / std::max(r.struck, 1e-6), "", 0.7, 1.4);
            ctx.metric(name + ": copies off their owners' trucks after (worst)", r.drift, "m", 0, 0.5);
            ctx.check(name + ": nothing blew up", r.finite);
        }
    }
}
