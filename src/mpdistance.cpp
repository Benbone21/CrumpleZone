#include "mpdistance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "procworld.h"
#include "softbody.h"

namespace mpdist {

namespace {
bool finite2(const V3& p) { return std::isfinite(p.x) && std::isfinite(p.z); }
int bitsIn(uint32_t v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}
bool inSet(uint32_t set, int peer) { return (set >> peer) & 1u; }
// The box round some of them.
struct Box {
    V3 lo = V3(1e18, 0, 1e18), hi = V3(-1e18, 0, -1e18);
    void add(const V3& p) { lo = V3(std::min(lo.x, p.x), 0, std::min(lo.z, p.z)), hi = V3(std::max(hi.x, p.x), 0, std::max(hi.z, p.z)); }
    bool any() const { return lo.x <= hi.x; }
    V3 mid() const { return (lo + hi) * 0.5; }
    double wide() const { return any() ? std::max(hi.x - lo.x, hi.z - lo.z) : 0; }
};
}  // namespace

double apart(const V3& a, const V3& b) { return std::max(std::fabs(a.x - b.x), std::fabs(a.z - b.z)); }

uint32_t groupOf(const std::vector<Body>& b, int must, uint32_t was) {
    uint32_t best = 0;
    int bestN = -1, bestKept = -1;
    double bestOff = 1e18;
    // (The biggest square's left and near sides can be taken to be on someone: a few players, every pair of them.)
    for (const Body& l : b)
        for (const Body& n : b) {
            uint32_t set = 0;
            Box box;
            for (const Body& o : b)
                if (o.p.x >= l.p.x && o.p.x <= l.p.x + kLimit && o.p.z >= n.p.z && o.p.z <= n.p.z + kLimit) set |= 1u << o.peer, box.add(o.p);
            if (!set || (must >= 0 && !inSet(set, must))) continue;
            const int count = bitsIn(set), kept = bitsIn(set & was);
            const V3 mid = box.mid();
            const double off = std::max(std::fabs(mid.x), std::fabs(mid.z));
            if (count > bestN || (count == bestN && (kept > bestKept || (kept == bestKept && off < bestOff)))) best = set, bestN = count, bestKept = kept, bestOff = off;
        }
    return best;
}

std::vector<double> distances(const std::vector<Body>& b, uint32_t group) {
    std::vector<double> d(b.size(), 0.0);
    for (size_t i = 0; i < b.size(); i++)
        for (size_t j = 0; j < b.size(); j++)
            if (i != j && inSet(group, b[j].peer)) d[i] = std::max(d[i], apart(b[i].p, b[j].p));
    return d;
}

bool focusOf(const std::vector<Body>& b, V3& focus, bool& edge) {
    edge = false;
    if (b.empty()) return false;
    Box box;
    double out = 0;
    for (const Body& o : b) {
        V3 p = o.p;
        if (b.size() > 1 && finite2(o.vel)) {  // (where they'll be when the slide goes in)
            const double v = std::max(std::fabs(o.vel.x), std::fabs(o.vel.z)), k = v > kLeadSpeed ? kLeadSpeed / v : 1.0;
            p.x += o.vel.x * k * kLead, p.z += o.vel.z * k * kLead;
        }
        box.add(p);
        out = std::max(out, std::max(std::fabs(p.x), std::fabs(p.z)));
    }
    focus = box.mid();
    edge = b.size() > 1 && out > kEdge && std::max(std::fabs(focus.x), std::fabs(focus.z)) >= kEdgeMove;
    return true;
}

void returnSpot(const Sim& sim, const std::vector<Body>& b, uint32_t group, size_t who, V3& p, double& yaw) {
    Box box;
    for (const Body& o : b)
        if (inSet(group, o.peer)) box.add(o.p);
    const V3 mid = box.mid();
    const Body* near = nullptr;  // (a place someone in the group is: the middle of a group may be a lake)
    for (const Body& o : b)
        if (inSet(group, o.peer) && (!near || apart(o.p, mid) < apart(near->p, mid))) near = &o;
    const int self = b[who].peer, skip = b[who].car;
    double facing = sim.world.spawnYaw;
    if (near && near->car >= 0) {
        const V3 f = sim.cars[near->car].frame().fwd;
        if (std::isfinite(f.x) && std::isfinite(f.z)) facing = std::atan2(-f.z, f.x);
    }
    if (near && NetSession::ringSpot(sim, self, skip, near->p, facing, Sim::kHomeMargin, 2, 12, p, yaw)) return;
    const int st = sim.nearestStation(near ? near->p : mid);
    if (st >= 0) {
        const auto& s = sim.world.stations[st];
        if (NetSession::ringSpot(sim, self, skip, s.p, s.yaw, Sim::kHomeMargin, 1, 12, p, yaw)) return;
    }
    NetSession::spawnSpot(sim, self, skip, p, yaw);
}

// ---- The host ------------------------------------------------------------------------------------------------------------------

void Referee::hear(const Sim& sim, const NetSession& net, const NetSession::GameMsg& m, double now) {
    if (m.player <= 0 || m.player >= net::kMaxPeers || m.text.rfind("walk ", 0) != 0) return;
    double x = 0, y = 0, z = 0;
    if (std::sscanf(m.text.c_str() + 5, "%lf %lf %lf", &x, &y, &z) != 3 || !std::isfinite(x) || !std::isfinite(z)) return;
    seats_[m.player].walk = proc::ProcWorld::toGlobal(sim.world, V3(x, y, z) + net.frameFix(m.said));
    seats_[m.player].walkAt = now;
}

std::vector<Body> Referee::bodies(const Sim& sim, const NetSession& net, double now, const V3* me) const {
    std::vector<Body> out;
    if (me && finite2(*me)) {
        Body h;
        h.p = V3(me->x, 0, me->z);
        if (sim.player >= 0 && sim.player < (int)sim.cars.size()) {
            h.car = sim.player;
            h.vel = sim.cars[sim.player].frame().vel;  // (on foot it's parked: 0)
        }
        out.push_back(h);
    }
    for (int peer = 1; peer < net::kMaxPeers; peer++) {
        if (!net.players[peer].active) continue;
        // (Riding along: they go where the driver goes, and it's the driver who's measured.)
        const uint32_t ride = net.ridingIn(peer);
        bool riding = false;
        for (const Car& c : sim.cars) riding |= ride && c.netId == ride;
        if (riding) continue;
        int car = -1;
        for (int i = 0; i < (int)sim.cars.size(); i++) {
            const Car& c = sim.cars[i];
            if (!c.remote || c.isProp() || NetSession::ownerOf(c.netId) != peer || !finite2(c.frame().com)) continue;
            if (car < 0 || (c.occupied && !sim.cars[car].occupied)) car = i;
        }
        Body g;
        g.peer = peer, g.car = car;
        const bool driving = car >= 0 && sim.cars[car].occupied, walking = now - seats_[peer].walkAt < 2.0;
        if (!driving && walking) {
            g.p = proc::ProcWorld::toLocal(sim.world, seats_[peer].walk), g.p.y = 0;
        } else if (car >= 0) {
            const Frame f = sim.cars[car].frame();
            g.p = V3(f.com.x, 0, f.com.z);
            if (finite2(f.vel)) g.vel = f.vel;
        } else {
            continue;  // (nothing of theirs here yet)
        }
        out.push_back(g);
    }
    return out;
}

void Referee::sendBack(Sim& sim, NetSession& net, const std::vector<Body>& b, size_t who, double now) {
    const Body& g = b[who];
    V3 p;
    double yaw = 0;
    returnSpot(sim, b, group_, who, p, yaw);
    const V3 at = proc::ProcWorld::toGlobal(sim.world, p);
    char msg[160];
    std::snprintf(msg, sizeof msg, "mpdist %d back %u %.2f %.2f %.4f", g.peer, g.car >= 0 ? sim.cars[g.car].netId : 0u, at.x, at.z, yaw);
    net.sendGame(msg);
    if (g.car >= 0) sim.detachCar(g.car);  // (a line to it from here would stretch km and tear whatever's on this end)
    Seat& s = seats_[g.peer];
    s.phase = COOLING, s.due = now + kCool;
    events.push_back(net.players[g.peer].name + " WAS TOO FAR AWAY: BROUGHT BACK TO THE GROUP");
}

void Referee::update(Sim& sim, NetSession& net, double now, const V3* me) {
    have_ = edge_ = false;
    if (sim.world.map != MAP_PROCEDURAL || net.role != NetSession::HOST) return;
    for (int peer = 1; peer < net::kMaxPeers; peer++)
        if (!net.players[peer].active) seats_[peer] = Seat();
    const std::vector<Body> b = bodies(sim, net, now, me);
    group_ = groupOf(b, !b.empty() && b[0].peer == 0 ? 0 : -1, group_);
    const std::vector<double> d = distances(b, group_);
    std::vector<char> fire(b.size(), 0);
    auto tell = [&](const Body& g, Seat& s, double dist) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "mpdist %d w %.0f %.0f %.1f", g.peer, dist, kLimit, s.phase == COUNTING ? std::max(0.0, s.due - now) : -1.0);
        net.sendGame(msg);
        s.said = now;
    };
    for (size_t i = 0; i < b.size(); i++) {
        const Body& g = b[i];
        if (g.peer == 0) continue;  // (the host is the group)
        Seat& s = seats_[g.peer];
        if (s.seen < 0) s.seen = now;
        if (now - s.seen < kJoinGrace) continue;
        const bool out = !inSet(group_, g.peer);
        if (s.phase == COOLING) {
            if ((!out && d[i] < kWarn) || now >= s.due) s.phase = CLEAR;
            else continue;
        }
        const Phase was = s.phase;
        switch (s.phase) {
            case CLEAR:
                if (out || d[i] >= kWarn) s.phase = WARNED, s.since = now;
                break;
            case WARNED:
                if (!out && d[i] < kWarnClear) s.phase = CLEAR;
                break;
            case COUNTING:
                if (!out && d[i] < kLimitClear) s.phase = d[i] < kWarnClear ? CLEAR : WARNED;
                break;
            case COOLING: break;
        }
        if (s.phase == WARNED && out) {
            s.phase = COUNTING, s.due = now + kCountdown;
            events.push_back(net.players[g.peer].name + " IS TOO FAR FROM THE GROUP");
        }
        if (s.phase == CLEAR) {
            if (was != CLEAR) net.sendGame("mpdist " + std::to_string(g.peer) + " ok");
            continue;
        }
        if (s.phase != was || now - s.said >= 1.0) tell(g, s, d[i]);
        if (s.phase == COUNTING) fire[i] = now >= s.due || (d[i] >= kHard && now - s.since >= kMinWarned);
    }
    // What the window holds: everyone it can. Not someone already sent back (their move's on its way, or they're an older game that
    // won't), nor anyone past kHard from the group.
    std::vector<Body> held;
    for (size_t i = 0; i < b.size(); i++)
        if (inSet(group_, b[i].peer) || (seats_[b[i].peer].phase != COOLING && d[i] < kHard)) held.push_back(b[i]);
    // (Stragglers each within reach of the group can still be too far apart from each other, on either side of it: the farthest goes.)
    for (;;) {
        Box box;
        for (const Body& o : held) box.add(o.p);
        if (box.wide() <= kHard) break;
        int far = -1;
        double fd = -1;
        for (size_t k = 0; k < held.size(); k++)
            for (size_t i = 0; i < b.size(); i++)
                if (b[i].peer == held[k].peer && !inSet(group_, b[i].peer) && d[i] > fd) far = (int)k, fd = d[i];
        if (far < 0) break;
        for (size_t i = 0; i < b.size(); i++) {
            const Seat& s = seats_[b[i].peer];
            if (b[i].peer == held[far].peer && s.phase == COUNTING && now - s.since >= kMinWarned) fire[i] = 1;
        }
        held.erase(held.begin() + far);
    }
    have_ = focusOf(held, focus_, edge_);
    // (Not while a slide's on its way: the spot might not be in the window it brings. It holds at 0 for those seconds.)
    if (!net.shiftPending())
        for (size_t i = 0; i < b.size(); i++)
            if (fire[i]) sendBack(sim, net, b, i, now);
}

// ---- A guest -------------------------------------------------------------------------------------------------------------------

bool Guest::hear(Sim& sim, const NetSession& net, const NetSession::GameMsg& m) {
    if (m.text.rfind("mpdist ", 0) != 0) return false;
    if (m.player != 0 || sim.world.map != MAP_PROCEDURAL) return true;
    int peer = -1, used = 0;
    char what[8] = "";
    if (std::sscanf(m.text.c_str() + 7, "%d %7s %n", &peer, what, &used) < 2 || peer <= 0 || peer >= net::kMaxPeers) return true;
    const char* rest = m.text.c_str() + 7 + used;
    const std::string w = what;
    if (w == "back") {
        unsigned id = 0;
        double x = 0, z = 0, yaw = 0;
        if (std::sscanf(rest, "%u %lf %lf %lf", &id, &x, &z, &yaw) != 4 || !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(yaw)) return true;
        if (peer == net.self) {
            sent_ = true, to_ = V3(x, 0, z), yaw_ = yaw, car_ = id;
        } else {
            for (int i = 0; i < (int)sim.cars.size(); i++)
                if (id && sim.cars[i].netId == id) sim.detachCar(i);
            events.push_back(net.players[peer].name + " WAS TOO FAR AWAY: BROUGHT BACK TO THE GROUP");
        }
        return true;
    }
    if (peer != net.self) return true;
    if (w == "ok") {
        warned_ = false;
    } else if (w == "w") {
        double dist = 0, limit = 0, secs = -1;
        if (std::sscanf(rest, "%lf %lf %lf", &dist, &limit, &secs) == 3 && std::isfinite(dist) && std::isfinite(limit) && std::isfinite(secs))
            warned_ = true, dist_ = dist, limit_ = limit, secs_ = secs, quiet_ = 0;
    }
    return true;
}

bool Guest::apply(Sim& sim, const NetSession& net, int& car) {
    if (!sent_ || net.shiftPending()) return false;
    sent_ = false;
    warned_ = false;
    car = -1;
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (car_ && sim.cars[i].netId == car_ && !sim.cars[i].remote) car = i;
    if (car < 0 && sim.player >= 0 && sim.player < (int)sim.cars.size() && !sim.cars[sim.player].remote) car = sim.player;
    if (car < 0) return false;  // (riding along in someone else's: there's nothing here to move)
    V3 p = proc::ProcWorld::toLocal(sim.world, to_);
    double yaw = yaw_;
    const double in = sim.world.halfSize() - 30;
    if (std::fabs(p.x) > in || std::fabs(p.z) > in) sim.homeSpot(car, p, yaw);  // (not this window's, whatever happened: land anyway)
    sim.detachCar(car);
    sim.teleportCar(car, p.x, p.z, yaw);  // (rests on the ground where it's put, dents and all)
    done_ = 6;
    return true;
}

void Guest::tick(double dt) {
    done_ = std::max(0.0, done_ - dt);
    if (warned_) {
        if (secs_ >= 0) secs_ = std::max(0.0, secs_ - dt);
        quiet_ += dt;
        if (quiet_ > 3.0) warned_ = false;  // (the host's gone quiet: it says this every second)
    }
}

std::string Guest::banner() const {
    if (done_ > 0) return "TOO FAR FROM THE GROUP: YOU WERE BROUGHT BACK TO THE OTHERS";
    if (!warned_) return "";
    char b[96];
    if (secs_ >= 0) std::snprintf(b, sizeof b, "TOO FAR FROM THE GROUP: TURN BACK   %.1f KM   BROUGHT BACK IN %d", dist_ / 1000, (int)std::ceil(secs_));
    else std::snprintf(b, sizeof b, "TOO FAR FROM THE GROUP: TURN BACK   %.1f KM (LIMIT %.1f KM)", dist_ / 1000, limit_ / 1000);
    return b;
}

}  // namespace mpdist
