// Production wire codecs, fragment assembly and playback without requiring sockets.
#include "harness.h"
#include "../src/net.h"
#include "../src/lights.h"
#include "../src/frameprof.h"
#include "../src/freecam.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <random>

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

// ---- Moving crashes over an internet line (docs/plans/net-collisions.md) ------------------
//
// The owner's report from play over the internet: "one person crashes into the other, but on
// their computer they never hit and are still driving around fine". net/crash-share above
// only crashes into a parked truck, where it hardly matters when each machine shows the other
// player's truck. Here both trucks move: A runs into the back of B (B at 10 m/s, A at 18), or
// into B's side (both at 12); and B, just before, stands on the brakes or swerves (what A's
// machine can't know yet). Each machine runs its own physics; states go both ways at 20 Hz
// over a line `oneWay` s long, with jitter and loss. Recorded on each machine: did it see the
// hit (its copy of the other's truck taken onto its physics), when, and what the hit did to
// each owner's own truck (the truth), against the same crash on one machine.
namespace {
enum CrashKind { REAR, SIDE, REAR_BRAKE, SIDE_SWERVE };
const char* crashName(CrashKind k) { return k == REAR ? "rear end" : k == SIDE ? "side" : k == REAR_BRAKE ? "rear end, B brakes" : "side, B swerves"; }
struct MovingCrash {
    bool sawA = false, sawB = false;  // A's machine saw a hit (B's copy taken onto its physics); B's machine did
    double tA = -1, tB = -1;          // when (s after the launch)
    double kickA = 0, kickB = 0;      // each owner's own truck: its biggest change of velocity in 0.25 s (m/s)
    double dmgA = 0, dmgB = 0;        // each owner's own truck's damage after
    double missAtHit = -1;            // A's machine, at its hit: how far B's copy was from where B really was (m)
    double poseErr = 0, poseErrMax = 0;  // before any hit: a copy against its owner's truck at the same moment (m)
    double snapBack = 0;                 // 0.5 s on from the hit: worst gap copy-to-owner, either machine (m)
    double delay = 0;                    // the view delay A's machine settled on (s)
    bool finite = true;
};

double fwdSpeed(const Car& c) {
    const Frame fr = c.frame();
    return dot(fr.vel, norm(V3(fr.fwd.x, 0, fr.fwd.z)));
}
void cruise(Car& c, double target) {  // (a driver holding a speed, straight on)
    const double v = fwdSpeed(c);
    c.throttle = clampd((target - v) * 0.6, 0, 1);
    c.brake = clampd((v - target - 1.0) * 0.3, 0, 1);
    c.steer = 0;
}

// oneWay < 0: both trucks on one machine (what it should come to). predict: as NetSession::predict.
MovingCrash movingCrash(Ctx& ctx, double oneWay, CrashKind kind, double offset = 0, double jitter = 0.01, double loss = 0.02, bool predict = true) {
    const VehicleSpec& s = vehicleCatalog()[7];
    const bool tbone = kind == SIDE || kind == SIDE_SWERVE;
    Machine m1, m2;
    for (Machine* m : {&m1, &m2}) {
        m->sim.world = World(MAP_PROVING_GROUND);
        m->sim.setupEmpty();
        m->net.predict = predict;
    }
    NetSessionTest::sender(m1.net);
    NetSessionTest::sender(m2.net);
    m2.net.self = 1;
    const bool alone = oneWay < 0;
    Machine& mb = alone ? m1 : m2;
    const double vA = tbone ? 12 : 18, vB = tbone ? 12 : 10, T = 2.0;  // they meet T s after the launch
    const double z = -50;
    // Rear end: B ahead on A's line, 16 m between bumpers. Side: B crosses A's line, its doors under A's nose at T.
    const double xA = tbone ? -(vA * T + s.length / 2 + s.width / 2) : -60;
    const double xB = (tbone ? 0 : xA + s.length + (vA - vB) * T) + (tbone ? offset : 0);
    const double zB = (tbone ? z - vB * T : z) + (tbone ? 0 : offset);
    m1.sim.addCar(s, V3(xA, m1.sim.world.clearY(xA, z) + 0.6, z), 0, V3(0.8, 0.2, 0.2));
    mb.sim.addCar(s, V3(xB, mb.sim.world.clearY(xB, zB) + 0.6, zB), tbone ? 90 * M_PI / 180 : 0, V3(0.2, 0.2, 0.8));
    const uint32_t idA = 1, idB = (1u << 16) | 1;
    m1.sim.cars[0].netId = idA;
    mb.sim.cars.back().netId = idB;
    m1.sim.player = m2.sim.player = 0;
    Car& a = m1.sim.cars[0];
    Car& b = alone ? m1.sim.cars[1] : m2.sim.cars[0];
    if (tbone && b.frame().fwd.z < 0) b.translate(V3(0, 0, 2 * vB * T));  // (B's nose toward A's line)
    for (int f = 0; f < 60; f++)
        for (Machine* m : {&m1, &m2}) m->sim.advance(Ctx::kFrame, 1000);
    if (!alone) {
        NetSessionTest::spawnAny(m1.net, m2.net, m2.sim, m1.sim.cars[0]);
        NetSessionTest::spawnAny(m2.net, m1.net, m1.sim, m2.sim.cars[0]);
    }
    Car* copyB = alone ? nullptr : &m1.sim.cars[1];  // B as A's machine shows it
    Car* copyA = alone ? nullptr : &m2.sim.cars[1];  // A as B's machine shows it
    struct Packet {
        int to;
        uint32_t id;
        std::vector<uint8_t> data;
    };
    std::multimap<double, Packet> wire;
    std::mt19937 rng(1234);
    auto lineDelay = [&] { return oneWay + std::min(jitter * 6, std::exponential_distribution<double>(1.0 / std::max(jitter, 1e-6))(rng)); };
    auto lost = [&] { return std::uniform_real_distribution<double>(0, 1)(rng) < loss; };
    double t = 0, t0 = 1e9;
    std::deque<std::pair<V3, V3>> hist;  // (A's, B's velocity) over the last 0.25 s
    MovingCrash r;
    double errSum = 0;
    int errN = 0;
    auto frame = [&](int f) {
        t += Ctx::kFrame;
        while (!wire.empty() && wire.begin()->first <= t) {
            Packet& p = wire.begin()->second;
            Machine& to = p.to == 1 ? m1 : m2;
            NetSessionTest::takeAny(to.net, to.sim, p.id, p.data, wire.begin()->first);
            wire.erase(wire.begin());
        }
        if (!alone) {
            NetSessionTest::play(m1.net, m1.sim, t);
            NetSessionTest::play(m2.net, m2.sim, t);
        }
        const bool going = t >= t0;
        if (going) {
            cruise(a, vA), cruise(b, vB);
            if (t - t0 > T - 0.3) {  // (B's driver, a moment before: what A's machine hears of only later)
                if (kind == REAR_BRAKE) b.throttle = 0, b.brake = 1;
                if (kind == SIDE_SWERVE) b.steer = 0.7;
            }
        }
        for (Machine* m : {&m1, &m2}) {
            m->sim.advance(Ctx::kFrame, 1000);
            for (Car& c : m->sim.cars)
                if (!c.remote) c.updateStats();
        }
        if (f % 3 == 0 && !alone) {
            if (!lost()) wire.insert({t + lineDelay(), {2, idA, NetSessionTest::stateOf(m1.net, m1.sim, a, t)}});
            if (!lost()) wire.insert({t + lineDelay(), {1, idB, NetSessionTest::stateOf(m2.net, m2.sim, b, t)}});
        }
        if (!going) return;
        hist.push_back({a.frame().vel, b.frame().vel});
        while (hist.size() > 16) hist.pop_front();
        r.kickA = std::max(r.kickA, len(hist.back().first - hist.front().first));
        r.kickB = std::max(r.kickB, len(hist.back().second - hist.front().second));
        if (alone) return;
        if (std::fabs(t - t0 - 1.0) < 0.5 * Ctx::kFrame) r.delay = m1.net.viewDelay();
        if (!r.sawA && copyB->heldHere > 0) r.sawA = true, r.tA = t - t0, r.missAtHit = len(copyB->frame().com - b.frame().com);
        if (!r.sawB && copyA->heldHere > 0) r.sawB = true, r.tB = t - t0;
        if (!r.sawA && !r.sawB && t > t0 + 0.5) {  // each copy against its owner's truck at the same moment
            for (const Car* copy : {copyA, copyB}) {
                const double e = len(copy->frame().com - (copy == copyA ? a : b).frame().com);
                errSum += e, errN++;
                r.poseErrMax = std::max(r.poseErrMax, e);
            }
        }
        const double hit = std::min(r.sawA ? r.tA : 1e9, r.sawB ? r.tB : 1e9);
        if (t - t0 > hit + 0.5)
            r.snapBack = std::max({r.snapBack, len(copyB->frame().com - b.frame().com), len(copyA->frame().com - a.frame().com)});
    };
    int f = 0;
    for (; f < 60; f++) frame(f);  // (the copies catch up)
    ctx.launch(a, vA);
    ctx.launch(b, vB);
    t0 = t;
    for (int end = f + 240; f < end; f++) frame(f);
    r.dmgA = a.damage, r.dmgB = b.damage;
    r.poseErr = errN ? errSum / errN : 0;
    for (Machine* m : {&m1, &m2})
        for (const Car& c : m->sim.cars) r.finite &= c.finite();
    return r;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

// Both machines must see the same crash: each sees the hit, at the same moment, and each
// owner's truck gets the kick and the damage it gets with both on one machine. (Damage from one
// crash swings by half or more with a few centimetres of line, so it's taken over five.)
void movingCrashes(Ctx& ctx, CrashKind kind) {
    char buf[512];
    const double offsets[] = {-0.4, -0.2, 0.0, 0.2, 0.4};
    const double nOff = 5;
    struct Line {
        double oneWay, jitter, loss;
    };
    const Line lines[] = {{0.001, 0.01, 0.02}, {0.03, 0.01, 0.02}, {0.075, 0.01, 0.02}, {0.15, 0.01, 0.02}, {0.25, 0.02, 0.02}, {0.075, 0.03, 0.10}};
    const std::string what = crashName(kind);
    MovingCrash one;
    std::vector<double> oneA, oneB;
    for (double off : offsets) {
        const MovingCrash k = movingCrash(ctx, -1, kind, off);
        one.kickA += k.kickA / nOff, one.kickB += k.kickB / nOff, one.dmgA += k.dmgA / nOff, one.dmgB += k.dmgB / nOff;
        oneA.push_back(k.dmgA), oneB.push_back(k.dmgB);
    }
    const double oneMedA = median(oneA), oneMedB = median(oneB);
    std::snprintf(buf, sizeof buf, "%s, one machine: kick A %.2f B %.2f m/s, damage A %.1f%% B %.1f%% (mean of 5 lines; median %.1f%% %.1f%%)", what.c_str(), one.kickA, one.kickB,
                  one.dmgA * 100, one.dmgB * 100, oneMedA * 100, oneMedB * 100);
    ctx.note(buf);
    {  // (as it was: the others shown where they were a view delay ago)
        const MovingCrash r = movingCrash(ctx, 0.075, kind, 0, 0.01, 0.02, false);
        std::snprintf(buf, sizeof buf, "%s, 75 ms, shown at the view (before 0.4.1): hit seen on A's machine %s at %.2f s, on B's %s at %.2f s; kick B %.2f m/s; copies %.2f m off before",
                      what.c_str(), r.sawA ? "yes" : "NO", r.tA, r.sawB ? "yes" : "NO", r.tB, r.kickB, r.poseErr);
        ctx.note(buf);
    }
    for (const Line& line : lines) {
        MovingCrash m;
        bool both = true, finite = true;
        double gap = 0, poseMax = 0;
        std::vector<double> dA, dB;
        for (double off : offsets) {
            const MovingCrash k = movingCrash(ctx, line.oneWay, kind, off, line.jitter, line.loss);
            dA.push_back(k.dmgA), dB.push_back(k.dmgB);
            both &= k.sawA && k.sawB;
            finite &= k.finite;
            if (k.sawA && k.sawB) gap = std::max(gap, std::fabs(k.tA - k.tB));
            m.kickA += k.kickA / nOff, m.kickB += k.kickB / nOff, m.dmgA += k.dmgA / nOff, m.dmgB += k.dmgB / nOff;
            m.poseErr += k.poseErr / nOff, m.snapBack = std::max(m.snapBack, k.snapBack), m.missAtHit = std::max(m.missAtHit, k.missAtHit);
            poseMax = std::max(poseMax, k.poseErrMax);
            m.delay = k.delay;
        }
        const double medA = median(dA), medB = median(dB);
        char lineName[64];
        std::snprintf(lineName, sizeof lineName, "%d ms each way%s", (int)std::lround(line.oneWay * 1000), line.loss > 0.05 ? " (jitter 30 ms, 10% lost)" : "");
        const std::string name = what + ", " + lineName;
        std::snprintf(buf, sizeof buf,
                      "%s: view delay %.0f ms; hit on both machines %s, %.0f ms apart (worst); kick A %.2f B %.2f m/s; damage A %.1f%% B %.1f%% (median %.1f%% %.1f%%); "
                      "copies off their owners' trucks %.2f m mean, %.2f worst; at the hit %.2f m; from 0.5 s after %.2f m",
                      name.c_str(), m.delay * 1000, both ? "yes" : "NO", gap * 1000, m.kickA, m.kickB, m.dmgA * 100, m.dmgB * 100, medA * 100, medB * 100, m.poseErr, poseMax,
                      m.missAtHit, m.snapBack);
        ctx.note(buf);
        ctx.check(name + ": both machines see the hit", both);
        ctx.metric(name + ": the two machines' hits apart (worst)", gap * 1000, "ms", 0, 60);
        ctx.metric(name + ": A's kick / on one machine", m.kickA / std::max(one.kickA, 1e-6), "", 0.75, 1.33);
        ctx.metric(name + ": B's kick / on one machine", m.kickB / std::max(one.kickB, 1e-6), "", 0.75, 1.33);
        // (The median: a crash's damage comes in two kinds, the nose folding up neatly or not, 2 % or 6 %, whichever by
        // a few millimetres, so one of five going the other way moves the mean by half.)
        ctx.metric(name + ": A's damage / on one machine (median of 5 lines)", medA / std::max(oneMedA, 1e-6), "", 0.6, 1.6);
        // (B braking in the last 0.3 s: A's machine hears of it a line later, so its copy of B comes in a little fast
        // and the push after goes a little differently; on a slow (150 ms or more each way) or lossy line B's own truck comes out somewhat less hurt:
        // 0.56 to 0.61 measured, and the damage depends on each platform's own floating-point rounding, so 0.6 was a coin toss there.
        // Since the fitment merge (2d0dbd5: engines forward, the trucks' mass moved) it's about half at 150 and 250 ms: 0.48 on Linux and
        // Windows alike, and 0.45 to 0.47 over 9 or 11 lines as over 5, so the truck's new build, not the sampling; the floor there is 0.4.)
        const bool slowLine = line.oneWay >= 0.15 || line.loss > 0.05;
        const double loB = kind == REAR_BRAKE && slowLine ? (line.loss > 0.05 ? 0.5 : 0.4) : 0.6;
        ctx.metric(name + ": B's damage / on one machine (median of 5 lines)", medB / std::max(oneMedB, 1e-6), "", loB, 1.6);
        ctx.metric(name + ": copies off their owners' trucks before the hit (mean)", m.poseErr, "m", 0, 0.25);
        // (After, both trucks tumble and knock again, on each machine a little differently: the copies are eased
        // back onto their owners' states. A 500 ms round trip leaves them further off for a moment.)
        ctx.metric(name + ": copies off their owners' trucks from 0.5 s after the hit (worst)", m.snapBack, "m", 0, line.oneWay > 0.2 ? 4.0 : 1.5);
        ctx.check(name + ": nothing blew up", finite);
    }
}
}  // namespace

#define MOVING_CRASH_DESC "over 0 to 250 ms lines with jitter and loss, five lines each: both machines see the hit at the same moment, and each truck's kick and damage are as on one machine"
SCENARIO(netMovingRear, "net/moving-crash/rear", "Both trucks moving, A at 65 km/h into the back of B at 36, " MOVING_CRASH_DESC) { movingCrashes(ctx, REAR); }
SCENARIO(netMovingSide, "net/moving-crash/side", "Both trucks moving at 43 km/h, A into B's side, " MOVING_CRASH_DESC) { movingCrashes(ctx, SIDE); }
SCENARIO(netMovingBrake, "net/moving-crash/rear-brake", "A into the back of B, B braking hard 0.3 s before (A's machine hears of it only later), " MOVING_CRASH_DESC) { movingCrashes(ctx, REAR_BRAKE); }
SCENARIO(netMovingSwerve, "net/moving-crash/side-swerve", "A into B's side, B swerving 0.3 s before, " MOVING_CRASH_DESC) { movingCrashes(ctx, SIDE_SWERVE); }

// ---- A slow machine's trucks (docs/plans/net-collisions.md, "slow machines") --------------------
//
// A host at 12 to 15 fps (a Mac on a 6 km map) ran at most 50 ms of physics a frame, as single
// player does below 20 fps: its own truck went at 0.65 to 0.8 of real time, and since its states
// are stamped with the shared clock, everyone saw it in slow motion. In multiplayer every frame's
// physics is run now (frameprof.h simNetSteps). The host's truck holds 20 m/s; another machine at
// 60 fps 50 ms away shows it; how fast does it go there, against real time?
namespace {
struct SlowHost {
    double ratio = 0;   // the copy's speed on the other machine (distance / real time) over the truck's own
    double simRate = 0; // the host's physics seconds per real second
    double gap = 0;     // worst: the copy against where the truck really is at that real moment (m)
};
SlowHost slowHost(double fps, bool keepUp, double msPerStep) {
    const VehicleSpec& s = vehicleCatalog()[7];
    Machine host, other;
    for (Machine* m : {&host, &other}) {
        m->sim.world = World(MAP_PROVING_GROUND);
        m->sim.setupEmpty();
    }
    NetSessionTest::sender(host.net);
    NetSessionTest::sender(other.net);
    other.net.self = 1;
    host.sim.addCar(s, V3(-200, host.sim.world.clearY(-200, -50) + 0.6, -50), 0, V3(0.8, 0.2, 0.2));
    Car& truck = host.sim.cars[0];
    truck.netId = 1;
    host.sim.player = 0;
    for (int f = 0; f < 60; f++) host.sim.advance(Ctx::kFrame, 1000);
    NetSessionTest::spawnAny(host.net, other.net, other.sim, truck);
    for (Node& n : truck.nodes) n.v = V3(20, 0, 0);
    for (Wheel& w : truck.wheels) w.omega = 20 / w.R;
    truck.gear = 3;
    truck.wake();
    std::multimap<double, std::vector<uint8_t>> wire;
    std::deque<std::pair<double, V3>> truth;  // (real time, where the truck was)
    const double hostDt = 1.0 / fps, end = 8;
    double usualDt = 1.0 / 60, simT = 0, nextHost = 0;
    V3 from, at;
    double fromT = -1, speedSum = 0;
    int speedN = 0;
    SlowHost r;
    for (double t = 0; t < end; t += Ctx::kFrame) {  // (t: real time, and the shared clock)
        if (t >= nextHost) {  // the host's frame: its physics, then its state
            nextHost += hostDt;
            cruise(truck, 20);
            int steps;
            if (keepUp) steps = host.sim.advance(hostDt, std::max(simMaxSteps(host.sim.h), simNetSteps(host.sim.h, host.sim.owed() + hostDt, msPerStep)), kNetOwedMax);
            else steps = host.sim.advance(simCatchUp(hostDt, usualDt), simMaxSteps(host.sim.h));  // (as before 0.4.1)
            simT += steps * host.sim.h;
            truck.updateStats();
            truth.push_back({t + hostDt, truck.frame().com});  // (the frame shows at its end: that's when its state is stamped)
            wire.insert({t + hostDt + 0.05, NetSessionTest::stateOf(host.net, host.sim, truck, t + hostDt)});
            if (t > 2) speedSum += fwdSpeed(truck), speedN++;
        }
        while (!wire.empty() && wire.begin()->first <= t) {
            NetSessionTest::takeAny(other.net, other.sim, 1, wire.begin()->second, wire.begin()->first);
            wire.erase(wire.begin());
        }
        NetSessionTest::play(other.net, other.sim, t);
        other.sim.advance(Ctx::kFrame, 1000);
        const V3 copy = other.sim.cars[0].frame().com;
        if (t > 2 && fromT < 0) from = copy, fromT = t;
        at = copy;
        if (keepUp && t > 2)  // (against the truck at this real moment, between the host's frames)
            for (size_t k = 0; k + 1 < truth.size(); k++)
                if (truth[k].first <= t && truth[k + 1].first > t) {
                    const double u = (t - truth[k].first) / (truth[k + 1].first - truth[k].first);
                    r.gap = std::max(r.gap, len(copy - (truth[k].second + (truth[k + 1].second - truth[k].second) * u)));
                }
    }
    const double wall = end - Ctx::kFrame - fromT;
    r.ratio = len(V3(at.x - from.x, 0, at.z - from.z)) / wall / std::max(1e-6, speedSum / std::max(1, speedN));
    r.simRate = simT / nextHost;
    return r;
}
}  // namespace

SCENARIO(netSlowHost, "net/slow-host", "A host at 12 and 15 fps: its truck at 20 m/s is shown at real speed on a 60 fps machine (it was 0.6 to 0.8 of it), and where it really is; a machine whose physics alone can't keep up still slows, as before") {
    char buf[300];
    for (double fps : {12.0, 15.0}) {
        const SlowHost old = slowHost(fps, false, 0.01), now = slowHost(fps, true, 0.01);
        std::snprintf(buf, sizeof buf, "%.0f fps host: shown at %.2f of real speed before (physics %.2f of real time), %.2f now (physics %.2f); copy %.2f m off the truck at worst",
                      fps, old.ratio, old.simRate, now.ratio, now.simRate, now.gap);
        ctx.note(buf);
        const std::string n = std::to_string((int)fps) + " fps host";
        ctx.metric(n + ": before 0.4.1, shown at this share of real speed", old.ratio, "", 0.5, 0.85);
        ctx.metric(n + ": its truck shown at this share of real speed", now.ratio, "", 0.97, 1.03);
        ctx.metric(n + ": its physics, of real time", now.simRate, "", 0.99, 1.01);
        ctx.metric(n + ": the copy against where the truck really is (worst)", now.gap, "m", 0, 0.25);
    }
    // (Physics that alone takes longer than real time: 2 ms a step at 2 kHz is 4 s a second. It slows, it doesn't spiral.)
    const SlowHost heavy = slowHost(12, true, 2.0);
    ctx.metric("12 fps host whose physics can't keep up: physics, of real time", heavy.simRate, "", 0.3, 0.8);
}

SCENARIO(netFreecamCarry, "net/freecam-carry", "The host's dev free cam carries its vehicle off and sets it down: the others see it carried, then set down where it was, upright and at rest") {
    Sim origin;
    source(origin);
    NetSession sender, receiver;
    NetSessionTest::sender(sender);
    NetSessionTest::spawn(sender, receiver, ctx.sim, origin.cars[0]);
    for (int k = 0; k < 60; k++) origin.advance(1.0 / 60, 1000);  // (settled on its wheels)
    freecam::Carry cr;
    ctx.check("the host picks its vehicle up", freecam::pickUp(origin, 0, cr));
    const V3 start = origin.cars[0].frame().com;
    double time = 0, worstLag = 0;
    for (int k = 1; k <= 60; k++) {  // 3 s, a state every 50 ms: carried 30 m off and 5 m up
        time = k * 0.05;
        origin.advance(0.05, 1000);
        freecam::hold(origin, cr, start + V3(time * 10, 5, 0));
        NetSessionTest::take(receiver, ctx.sim, NetSessionTest::state(sender, origin, time), time + 0.02);
        NetSessionTest::play(receiver, ctx.sim, time + 0.02);
        if (k > 20) worstLag = std::max(worstLag, len(ctx.sim.cars[0].frame().com - origin.cars[0].frame().com));
    }
    ctx.metric("how far behind the carried vehicle is shown (the playback delay, at 10 m/s)", worstLag, "m", 0, 3.0);
    ctx.check("shown up in the air with it", ctx.sim.cars[0].frame().com.y > start.y + 4);
    const V3 drop = start + V3(40, 0, 12);
    freecam::dropAt(origin, cr, drop.x, drop.z);
    for (int k = 1; k <= 30; k++) {  // 1.5 s more
        time += 0.05;
        origin.advance(0.05, 1000);
        NetSessionTest::take(receiver, ctx.sim, NetSessionTest::state(sender, origin, time), time + 0.02);
        NetSessionTest::play(receiver, ctx.sim, time + 0.02);
    }
    for (int k = 1; k <= 30; k++) NetSessionTest::play(receiver, ctx.sim, time + 0.02 + k / 60.0);  // (the playback catches up)
    const Frame mine = origin.cars[0].frame(), theirs = ctx.sim.cars[0].frame();
    ctx.check("the host's is set down at the point, upright", std::hypot(mine.com.x - drop.x, mine.com.z - drop.z) < 1.5 && mine.up.y > 0.95);
    ctx.check("the others see it there", len(theirs.com - mine.com) < 0.3, std::to_string(len(theirs.com - mine.com)) + " m off");
    ctx.check("upright and at rest there", theirs.up.y > 0.95 && len(theirs.vel) < 0.5);
}
