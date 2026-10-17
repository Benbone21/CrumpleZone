// Production wire codecs, fragment assembly and playback without requiring sockets.
#include "harness.h"
#include "../src/net.h"
#include "../src/lights.h"
#include <algorithm>

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
    ctx.check("engine load and boost transmitted", std::fabs(remote.throttle - 0.48) < 1e-6 && std::fabs(remote.boost - 0.4) < 1e-6);
    ctx.check("reverse gear and damage transmitted", remote.gear == -1 && std::fabs(remote.damage - 0.3) < 1e-6);
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
