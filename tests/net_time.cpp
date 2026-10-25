// The shared clock, the timeline the others are shown on, and states told only by what
// changed, over simulated lines: 50 to 200 ms each way, jitter, lost packets. Socket-free
// (the production message handlers, fed by a little network model), plus one loopback run
// for the time of day.
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <limits>
#include <random>

#include "../src/daylight.h"
#include "../src/net.h"
#include "harness.h"

struct NetSessionTimeTest {
    using Stream = NetSession::Stream;
    static void asSender(NetSession& s) { s.self = 0, s.sessionStart = 0; }
    static void asClient(NetSession& s, int self) { s.role = NetSession::CLIENT, s.self = self; }
    static void at(NetSession& s, double now) { s.curNow = now; }
    static void spawn(NetSession& from, NetSession& to, Sim& sim, const Car& car) {
        auto m = from.spawnMsg(car);
        to.apply(sim, {m.begin() + 2, m.end()});
    }
    static std::vector<uint8_t> next(NetSession& from, std::vector<Stream>& st, const Sim& sim, const Car& car, double now, uint32_t& seq) {
        if (st.empty()) st.push_back(Stream()), st.back().netId = car.netId;
        auto d = from.nextStateOf(st[0], sim, car, now);
        seq = from.seq;
        return d;
    }
    static std::vector<int16_t> dents(NetSession& from, const Sim& sim, const Car& car) { return from.dentsOf(sim, car); }
    static void take(NetSession& to, Sim& sim, uint32_t netId, const std::vector<uint8_t>& d, double now, uint32_t seq) { to.takeSnapshot(sim, netId, d, now, seq); }
    // What the receiver says it has, as the ack datagram's payload (and forgets it said so).
    static std::vector<uint8_t> acks(NetSession& to) {
        std::vector<uint8_t> b;
        for (auto& a : to.upAcks) {
            const uint8_t* p = (const uint8_t*)&a;
            b.insert(b.end(), p, p + 4);
            b.insert(b.end(), p + 4, p + 8);
        }
        to.upAcks.clear();
        return b;
    }
    static void hear(NetSession& from, std::vector<Stream>& st, const std::vector<uint8_t>& acks) { from.takeAcks(st, acks.data(), acks.size()); }
    static const std::vector<int16_t>* lastTaken(NetSession& s, uint32_t& seq) { return s.lastTaken(seq); }
    static void handle(NetSession& s, Sim* sim, const std::vector<uint8_t>& msg) {
        if (s.conns.empty()) s.fakeConn(0, true);
        s.handle(sim, s.conns[0], msg);
    }
    static void play(NetSession& s, Sim& sim, double now) {
        s.curNow = now;
        s.applyRemotes(sim, now);
        s.release(&sim);
    }
    static void datagram(NetSession& s, Sim& sim, const std::vector<uint8_t>& d) { s.onDatagram(&sim, d.data(), d.size(), 0x7f000001, 1, s.curNow); }
    static uint64_t token(const NetSession& s) { return s.udpToken; }
    static void setToken(NetSession& s, uint64_t t) { s.udpToken = t; }
};

namespace {
using TT = NetSessionTimeTest;

template <class T> void put(std::vector<uint8_t>& b, T v) {
    const uint8_t* p = (const uint8_t*)&v;
    b.insert(b.end(), p, p + sizeof v);
}
std::vector<uint8_t> pongMsg(double sent, double hostT) {
    std::vector<uint8_t> m = {11};  // MSG_PONG
    put(m, sent), put(m, hostT);
    return m;
}
std::vector<uint8_t> gameMsg(int who, const std::string& text, double said) {
    std::vector<uint8_t> m = {15, (uint8_t)who, (uint8_t)text.size()};  // MSG_GAME
    m.insert(m.end(), text.begin(), text.end());
    put(m, said);
    return m;
}
std::vector<uint8_t> clockMsg(double stamp, float hours, float rate) {
    std::vector<uint8_t> m = {13};  // MSG_CLOCK
    put(m, stamp), put(m, hours), put(m, rate);
    return m;
}

// One way along a line: `base` s plus jitter (most packets a little late, a few a lot), and
// `loss` of them gone.
struct Line {
    double base, jitter, loss;
    std::mt19937 rng;
    Line(double b, double j, double l, unsigned seed) : base(b), jitter(j), loss(l), rng(seed) {}
    bool lost() { return std::uniform_real_distribution<double>(0, 1)(rng) < loss; }
    double delay() { return base + std::min(jitter * 6, std::exponential_distribution<double>(1.0 / jitter)(rng)); }
};

// The host's truck goes round a circle 40 m across at 12 m/s.
V3 circle(double t) { return V3(-40 + 40 * std::cos(t * 0.3), 0.6, -40 + 40 * std::sin(t * 0.3)); }
V3 circleVel(double t) { return V3(-12 * std::sin(t * 0.3), 0, 12 * std::cos(t * 0.3)); }
void place(Car& c, double t) {
    c.translate(circle(t) - c.frame().com);
    for (Node& n : c.nodes) n.v = circleVel(t);
}

struct Queue {
    std::multimap<double, std::function<void()>> q;
    void at(double t, std::function<void()> f) { q.emplace(t, std::move(f)); }
    void runTo(double t) {
        while (!q.empty() && q.begin()->first <= t) {
            auto f = std::move(q.begin()->second);
            q.erase(q.begin());
            f();
        }
    }
};

}  // namespace

SCENARIO(netClockSync, "net/clock-sync", "A guest's idea of the host's clock converges, from pings over a line 50 or 200 ms each way with jitter and lost packets") {
    struct Case {
        const char* what;
        double base, jitter;
    };
    for (const Case& cs : {Case{"50 ms", 0.05, 0.01}, Case{"200 ms", 0.2, 0.03}, Case{"200 ms, bad jitter", 0.2, 0.08}}) {
        const double trueOffset = 4321.5678;  // the host's clock minus mine
        Line up(cs.base, cs.jitter, 0.1, 7), down(cs.base, cs.jitter, 0.1, 8);
        net::ClockSync c;
        Queue q;
        double next = 0, firstSynced = -1, after3 = 0, after10 = 0;
        for (double t = 0; t < 40; t += 0.005) {
            q.runTo(t);
            if (c.synced && firstSynced < 0) firstSynced = t;
            if (t >= next) {
                next = t + (c.samples < 8 ? 0.15 : 1.0);
                if (!up.lost()) {
                    const double sent = t, hostT = t + up.delay() + trueOffset;
                    if (!down.lost()) {
                        const double back = hostT - trueOffset + down.delay();
                        q.at(back, [&c, sent, hostT, back] { c.add(sent, hostT, back); });
                    }
                }
            }
            const double err = std::fabs(c.offset - trueOffset);
            if (t > 3) after3 = std::max(after3, err);
            if (t > 10) after10 = std::max(after10, err);
        }
        ctx.check(std::string(cs.what) + ": set within a second", firstSynced >= 0 && firstSynced < 1.0, std::to_string(firstSynced));
        ctx.metric(std::string(cs.what) + ": worst clock error after 3 s", after3 * 1000, "ms", 0, 40);
        ctx.metric(std::string(cs.what) + ": worst clock error after 10 s", after10 * 1000, "ms", 0, cs.jitter < 0.05 ? 15 : 30);  // (80 ms jitter: rarely both ways quick)
    }
    net::ClockSync c;
    ctx.check("nonsense answers are thrown away", !c.add(1, NAN, 2) && !c.add(2, 5, 1) && !c.add(0, 1e300, 1) && !c.add(0, 1, 100) && !c.synced);
}

SCENARIO(netTimeline, "net/timeline", "Two guests, one 50 ms and one 200 ms from the host with jitter and loss, show the host's truck where it was at the same moment on the shared clock, and its chatter at the moment it was said") {
    Sim origin;
    origin.setupEmpty();
    origin.addCar(vehicleCatalog()[2], V3(-40, 0.6, -40), 0, V3(0.4, 0.5, 0.6));
    Car& car = origin.cars[0];
    car.netId = 1;
    NetSession host;
    TT::asSender(host);

    struct Guest {
        const char* name;
        double clockAhead;  // my clock minus the host's
        Line toMe, toHost;
        Sim sim;
        NetSession net;
        std::vector<TT::Stream> stream;  // the host's states to this guest
        std::vector<std::pair<double, V3>> shown;  // (moment shown on the shared clock, where the truck is)
        double worstEvent = 0, worstEventPlace = 0, worstTruth = 0;
        int events = 0;
        double nextPing = -1e18;
        std::vector<double> truthErr;
        Guest(const char* n, double ahead, double base, double jitter, unsigned seed)
            : name(n), clockAhead(ahead), toMe(base, jitter, 0.1, seed), toHost(base, jitter, 0.1, seed + 1) {}
    };
    Guest a("near guest", -1000.25, 0.05, 0.01, 21), b("far guest", 777.75, 0.2, 0.03, 31);
    Guest* guests[2] = {&a, &b};
    for (int g = 0; g < 2; g++) {
        Guest& x = *guests[g];
        x.sim.setupEmpty();
        TT::asClient(x.net, 1 + g);
        TT::spawn(host, x.net, x.sim, car);
    }
    Queue q;
    std::map<std::string, double> said;  // "mark N" -> when
    double nextState = 0, nextMark = 3;
    int mark = 0;
    const double dt = 1.0 / 120;
    for (double t = 0; t < 20; t += dt) {  // (t: the shared clock, the host's)
        q.runTo(t);
        if (t >= nextState - 1e-9) {  // the host's truck: 20 states a second, to each guest on its own stream
            nextState += 1.0 / net::kSendRate;
            place(car, t);
            for (Guest* x : guests) {
                uint32_t seq = 0;
                const std::vector<uint8_t> d = TT::next(host, x->stream, origin, car, t, seq);
                if (x->toMe.lost()) continue;
                const double arrive = t + x->toMe.delay();
                q.at(arrive, [x, d, seq, arrive, &q, &host] {  // (by the guest's own clock at arrival)
                    const double mine = arrive + x->clockAhead;
                    TT::at(x->net, mine);
                    TT::take(x->net, x->sim, 1, d, mine, seq);
                    const std::vector<uint8_t> acks = TT::acks(x->net);  // ("got it", back over the line, maybe lost)
                    if (!acks.empty() && !x->toHost.lost()) q.at(arrive + x->toHost.delay(), [x, acks, &host] { TT::hear(host, x->stream, acks); });
                });
            }
        }
        if (t >= nextMark) {  // the host says something: everyone should hear it when they're shown that moment
            nextMark += 0.5;
            const std::string text = "mark " + std::to_string(mark++);
            said[text] = t;
            for (Guest* x : guests) {
                const double arrive = t + x->toMe.delay();  // (TCP: late, but never lost)
                const std::vector<uint8_t> m = gameMsg(0, text, t);
                q.at(arrive, [x, m, arrive] {
                    TT::at(x->net, arrive + x->clockAhead);
                    TT::handle(x->net, &x->sim, m);
                });
            }
        }
        for (Guest* x : guests) {
            // Pings: setting the clock by the host's, over the same line.
            const double mine = t + x->clockAhead;
            if (mine >= x->nextPing) {
                x->nextPing = mine + (x->net.clockSynced() ? 1.0 : 0.15);
                if (!x->toHost.lost()) {
                    const double hostT = t + x->toHost.delay();
                    if (!x->toMe.lost()) {
                        const double back = hostT + x->toMe.delay();
                        const std::vector<uint8_t> m = pongMsg(mine, hostT);
                        q.at(back, [x, m, back] {
                            TT::at(x->net, back + x->clockAhead);
                            TT::handle(x->net, &x->sim, m);
                        });
                    }
                }
            }
            const size_t heard = x->net.gameIn.size();
            TT::play(x->net, x->sim, mine);
            if (x->sim.cars.empty() || !x->net.clockSynced()) continue;
            const V3 at = x->sim.cars[0].frame().com;
            const double shownAt = x->net.viewTime();
            if (t > 5) {
                x->shown.push_back({shownAt, at});
                x->truthErr.push_back(len(at - circle(shownAt)));
            }
            for (size_t k = heard; k < x->net.gameIn.size(); k++) {
                const auto it = said.find(x->net.gameIn[k].text);
                if (it == said.end() || t < 5) continue;
                x->events++;
                x->worstEvent = std::max(x->worstEvent, std::fabs(shownAt - it->second));
                x->worstEventPlace = std::max(x->worstEventPlace, len(at - circle(it->second)));
            }
        }
    }
    for (Guest* x : guests) {
        std::sort(x->truthErr.begin(), x->truthErr.end());
        const std::string n = x->name;
        ctx.metric(n + ": clock error", std::fabs(x->net.clockOffset() + x->clockAhead) * 1000, "ms", 0, 15);
        ctx.metric(n + ": shown this far behind", x->net.viewDelay() * 1000, "ms", 0, 600);
        ctx.metric(n + ": truck vs where it really was then (95%)", x->truthErr.empty() ? 99 : x->truthErr[x->truthErr.size() * 95 / 100], "m", 0, 0.1);
        ctx.metric(n + ": truck vs where it really was then (worst)", x->truthErr.empty() ? 99 : x->truthErr.back(), "m", 0, 0.5);
        ctx.check(n + ": heard every mark", x->events >= 30, std::to_string(x->events));
        ctx.metric(n + ": chatter vs the moment it was said", x->worstEvent * 1000, "ms", 0, 12);
        ctx.metric(n + ": the truck when it's heard vs where it was when said", x->worstEventPlace, "m", 0, 0.3);
    }
    ctx.check("the far guest is shown further behind than the near one", a.net.viewDelay() < b.net.viewDelay() && b.net.viewDelay() > 0.2,
              std::to_string(a.net.viewDelay()) + " vs " + std::to_string(b.net.viewDelay()));
    // At the same moment on the shared clock, both have the truck in the same place.
    double worst = 0;
    size_t j = 0;
    for (const auto& s : a.shown) {
        while (j + 1 < b.shown.size() && b.shown[j + 1].first < s.first) j++;
        if (j + 1 >= b.shown.size() || b.shown[j].first > s.first) continue;
        const double u = (s.first - b.shown[j].first) / std::max(1e-9, b.shown[j + 1].first - b.shown[j].first);
        if (b.shown[j + 1].first - b.shown[j].first > 0.05) continue;
        worst = std::max(worst, len(s.second - (b.shown[j].second + (b.shown[j + 1].second - b.shown[j].second) * u)));
    }
    ctx.metric("the two guests' trucks at the same shared moment", worst, "m", 0, 0.5);
}

SCENARIO(netDeltaLoss, "net/delta-loss", "States told only by the dents that moved, against the newest the receiver said it has, over a lossy 100 ms line: every state taken is right to the dent step, and two seconds of total loss is recovered from with a whole one") {
    Sim origin;
    origin.setupEmpty();
    origin.addCar(vehicleCatalog()[2], V3(-40, 0.6, -40), 0, V3(0.4, 0.5, 0.6));
    Car& car = origin.cars[0];
    car.netId = 1;
    NetSession host, guest;
    TT::asSender(host);
    Sim gsim;
    gsim.setupEmpty();
    TT::asClient(guest, 1);  // (it says what it has to the host; no clock needed here)
    TT::spawn(host, guest, gsim, car);
    std::vector<TT::Stream> stream;
    Line toMe(0.1, 0.02, 0.15, 41), toHost(0.1, 0.02, 0.15, 42);
    std::mt19937 rng(5);
    Queue q;
    std::map<uint32_t, std::vector<int16_t>> truth;
    int sent = 0, taken = 0, wrong = 0, takenAfterBlackout = 0;
    size_t bytes = 0, wholeBytes = 0;
    double worst = 0;
    for (double t = 0; t < 12; t += 0.05) {
        q.runTo(t);
        // The truck drives and dents: a few nodes pushed in each time, the rest still.
        place(car, t);
        for (int k = 0; k < 12; k++) {
            Node& n = car.nodes[rng() % car.bodyNodes];
            n.p += V3((int)(rng() % 21) - 10, (int)(rng() % 21) - 10, (int)(rng() % 21) - 10) * 0.001;
        }
        uint32_t seq = 0;
        const std::vector<uint8_t> d = TT::next(host, stream, origin, car, t, seq);
        truth[seq] = TT::dents(host, origin, car);
        sent++, bytes += d.size();
        if (wholeBytes == 0) wholeBytes = d.size();
        const bool blackout = t > 6 && t < 8;
        if (blackout || toMe.lost()) continue;
        q.at(t + toMe.delay(), [&, d, seq, t] {
            uint32_t before = 0, after = 0;
            TT::lastTaken(guest, before);
            TT::take(guest, gsim, 1, d, t + 0.1, seq);
            const std::vector<int16_t>* got = TT::lastTaken(guest, after);
            if (!got || after != seq || before == seq) return;
            taken++;
            if (t > 8) takenAfterBlackout++;
            const std::vector<int16_t>& want = truth[seq];
            for (size_t k = 0; k < want.size(); k++) {
                worst = std::max(worst, (double)std::abs(want[k] - (*got)[k]));
                wrong += std::abs(want[k] - (*got)[k]) >= net::kDentStep;
            }
            const std::vector<uint8_t> acks = TT::acks(guest);
            if (!toHost.lost()) q.at(t + 0.2 + toHost.delay(), [&, acks] { TT::hear(host, stream, acks); });
        });
    }
    q.runTo(1e9);
    ctx.metric("states taken of those sent (15% lost each way, 2 s all lost)", taken / (double)sent, "", 0.6, 1);
    ctx.check("every dent of every state taken is within the step", wrong == 0, std::to_string(wrong) + " off, worst " + std::to_string(worst) + " mm");
    ctx.check("after the blackout, states get through again", takenAfterBlackout > 50, std::to_string(takenAfterBlackout));
    ctx.metric("a state, on average, vs a whole one", bytes / (double)sent / wholeBytes, "", 0.02, 0.5);
}

SCENARIO(netDayClock, "net/day-clock", "The server's time of day: two guests with clocks far from the host's see the same hour as the host, running and held") {
    NetSession host;
    host.discovery = false;
    const int port = 28700 + (int)(getpid() % 200);
    Sim hsim;
    hsim.world = World(MAP_PROVING_GROUND);
    hsim.setupEmpty();
    ctx.check("host opens a session", host.host(port, "SERVER", MAP_PROVING_GROUND), host.error);
    host.adopt(hsim);
    NetSession a, b;
    Sim asim, bsim;
    for (Sim* s : {&asim, &bsim}) {
        s->world = World(MAP_PROVING_GROUND);
        s->setupEmpty();
        s->addCar(vehicleCatalog()[3], V3(-30, 0.5, -30), 0, V3(0.2, 0.2, 0.2));
        s->player = 0;
    }
    double t = 0, hours = 17.25, rate = clockAdvance(1.0, 24);
    auto frame = [&] {
        host.dayHours = std::fmod(hours + rate * host.sharedTime(t), 24.0), host.dayRate = rate;
        host.receive(hsim, t), host.send(hsim, t);
        if (!a.joining()) a.receive(asim, t + 5000), a.send(asim, t + 5000);
        if (!b.joining()) b.receive(bsim, t + 300.5), b.send(bsim, t + 300.5);
        usleep(1000);
        t += 0.002;
    };
    a.beginJoin("127.0.0.1:" + std::to_string(port), "A");
    b.beginJoin("127.0.0.1:" + std::to_string(port), "B");
    int sa = 0, sb = 0;
    for (int k = 0; k < 3000 && (sa == 0 || sb == 0); k++) {
        if (sa == 0 && (sa = a.pollJoin()) == 1) a.adopt(asim);
        if (sb == 0 && (sb = b.pollJoin()) == 1) b.adopt(bsim);
        frame();
    }
    ctx.check("both guests are in", sa == 1 && sb == 1, a.error + b.error);
    for (int k = 0; k < 600; k++) frame();
    const double shared = host.sharedTime(t);
    ctx.metric("guest A's clock vs the host's", std::fabs(a.sharedTime(t + 5000) - shared) * 1000, "ms", 0, 10);
    ctx.metric("guest B's clock vs the host's", std::fabs(b.sharedTime(t + 300.5) - shared) * 1000, "ms", 0, 10);
    ctx.metric("guest A's hour vs the host's", std::fabs(a.dayHours - host.dayHours) * 3600, "s of day", 0, 2);
    ctx.metric("guest B's hour vs the host's", std::fabs(b.dayHours - host.dayHours) * 3600, "s of day", 0, 2);
    hours = 3.5, rate = 0;  // the server holds it at night: the guests follow at once
    for (int k = 0; k < 200; k++) frame();
    ctx.check("held at 03:30 for everyone", std::fabs(a.dayHours - 3.5) < 1e-3 && std::fabs(b.dayHours - 3.5) < 1e-3 && a.dayRate == 0,
              std::to_string(a.dayHours) + " " + std::to_string(b.dayHours));
    a.close(), b.close(), host.close();
}

SCENARIO(netTimeFuzz, "net/time-fuzz", "Garbled clock, pong, chatter and ack messages and garbled told-by-change states at a guest: nothing crashes, the clock, the hour and the vehicles stay finite, chatter is never stuck") {
    NetSession g;
    TT::asClient(g, 2);
    Sim sim;
    sim.setupEmpty();
    Sim origin;
    origin.setupEmpty();
    origin.addCar(vehicleCatalog()[2], V3(-40, 0.6, -40), 0, V3(0.4, 0.5, 0.6));
    origin.cars[0].netId = 1;
    NetSession host;
    TT::asSender(host);
    TT::spawn(host, g, sim, origin.cars[0]);
    std::mt19937 rng(99);
    auto garble = [&](std::vector<uint8_t> m) {
        for (int k = 0, n = 1 + (int)(rng() % 4); k < n && m.size() > 1; k++) m[1 + rng() % (m.size() - 1)] = (uint8_t)rng();
        if (rng() % 5 == 0) m.resize(rng() % (m.size() + 1));
        return m;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    std::vector<TT::Stream> stream;
    std::vector<std::vector<uint8_t>> states;
    for (int k = 0; k < 40; k++) {
        place(origin.cars[0], k * 0.05);
        uint32_t seq;
        states.push_back(TT::next(host, stream, origin, origin.cars[0], k * 0.05, seq));
        if (k % 3 == 0) TT::hear(host, stream, std::vector<uint8_t>{(uint8_t)1, 0, 0, 0, (uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(seq >> 16), (uint8_t)(seq >> 24)});
    }
    bool finite = true;
    double now = 10;
    for (int k = 0; k < 20000; k++, now += 0.004) {
        TT::at(g, now);
        const double odd[] = {nan, inf, -inf, 1e300, -1e300, 0, now - 9.9, now + 1e6};
        switch (rng() % 5) {
            case 0: TT::handle(g, &sim, garble(clockMsg(rng() % 3 ? now : odd[rng() % 8], (float)odd[rng() % 8], rng() % 2 ? 0.01f : (float)odd[rng() % 8]))); break;
            case 1: TT::handle(g, &sim, garble(pongMsg(rng() % 2 ? now - 0.05 : odd[rng() % 8], odd[rng() % 8]))); break;
            case 2: TT::handle(g, &sim, garble(gameMsg(rng() % 3, "chat hi", rng() % 2 ? now : odd[rng() % 8]))); break;
            case 3: TT::take(g, sim, 1, garble(states[rng() % states.size()]), now, 1 + rng() % 60); break;
            default: {
                std::vector<uint8_t> d(22 + 8 * (rng() % 4));
                for (uint8_t& x : d) x = (uint8_t)rng();
                d[0] = 0x7a, d[1] = 0xc2, d[2] = 3;  // (an ack, with a wrong secret mostly)
                TT::datagram(g, sim, d);
            }
        }
        if (k % 50 == 0) TT::play(g, sim, now);
        const bool was = finite;
        finite &= std::isfinite(g.clockOffset()) && std::isfinite(g.viewTime()) && (g.dayHours == -1 || (g.dayHours >= 0 && g.dayHours < 24));
        for (const Node& n : sim.cars[0].nodes) finite &= std::isfinite(n.p.x + n.p.y + n.p.z);
        if (was && !finite) ctx.check("(first went wrong at " + std::to_string(k) + ")", false);
        if (g.gameIn.size() > 600) g.gameIn.clear();
    }
    for (int k = 0; k < 400; k++) TT::play(g, sim, now += 0.01);
    ctx.check("everything stays finite and the hour in range", finite);
    g.gameIn.clear();
    TT::handle(g, &sim, gameMsg(0, "chat last", nan));
    TT::play(g, sim, now += 2);
    ctx.check("chatter with any stamp comes out", !g.gameIn.empty() && g.gameIn.back().text == "chat last");
}
