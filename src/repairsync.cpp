#include "repairsync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "anchors.h"
#include "fixes.h"
#include "net.h"

namespace repairsync {

namespace {

constexpr double kPosLimit = 5.0e4;  // (as net.cpp: nothing in any world is further out)
constexpr size_t kMaxDropped = 12;   // (as interact.cpp keeps)

bool okPos(const V3& p) { return std::isfinite(p.x + p.y + p.z) && std::fabs(p.x) < kPosLimit && std::fabs(p.y) < kPosLimit && std::fabs(p.z) < kPosLimit; }
bool inRange(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool whole(double v, double lo, double hi) { return inRange(v, lo, hi) && v == std::floor(v); }

// "1.5,2,-3" -> numbers (false on anything else).
bool numbers(const std::string& s, std::vector<double>& out) {
    out.clear();
    const char* p = s.c_str();
    while (*p) {
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) return false;
        out.push_back(v);
        p = end;
        if (*p == ',') p++;
        else if (*p) return false;
        if (out.size() > 32) return false;
    }
    return !out.empty();
}

bool validHand(const TrunkRef& r) {
    switch (r.kind) {
    case TK_TOOL: return r.id >= 0 && r.id < TOOL_COUNT;
    case TK_PART: return r.id >= 0 && r.id < PART_COUNT;
    case TK_GEAR: return r.id >= 0 && r.id < GEAR_COUNT;
    case TK_CHAINS:
    case TK_JACK: return r.id == 0;
    default: return false;
    }
}

}  // namespace

int carByNetId(const Sim& sim, uint32_t id) {
    if (!id) return -1;
    for (int i = 0; i < (int)sim.cars.size(); i++)
        if (sim.cars[i].netId == id) return i;
    return -1;
}

bool Work::same(const Work& o) const {
    if (jack != o.jack || chain != o.chain || hand != o.hand || carrying != o.carrying) return false;
    if (carrying && (carry.R != o.carry.R || carry.width != o.carry.width || carry.style != o.carry.style || carry.popped != o.carry.popped || carry.tireOnly != o.carry.tireOnly)) return false;
    const InteractOption &a = job, &b = o.job;
    return a.kind == b.kind && carId == o.carId && a.wheel == b.wheel && a.step == b.step && a.tool == b.tool && a.part == b.part && a.fix.kind == b.fix.kind &&
           a.fix.wheel == b.fix.wheel && a.atEnd == b.atEnd;
}

Work workOf(const Sim& sim, const Walker* wk, const Interact& ia) {
    Work w;
    if (!wk || !wk->active) return w;
    w.jack = wk->holdingJack, w.chain = wk->holdingChain, w.hand = wk->hand;
    if (wk->carrying) w.carrying = true, w.carry = *wk->carrying;
    const InteractOption& o = ia.current;
    if (ia.progress > 0 && o.kind != IA_NONE && !o.blocked && std::isfinite(o.at.x + o.at.y + o.at.z)) {
        w.job.kind = o.kind, w.job.wheel = o.wheel, w.job.step = o.step, w.job.tool = o.tool, w.job.part = o.part;
        w.job.fix.kind = o.fix.kind, w.job.fix.wheel = o.fix.wheel, w.job.fix.part = o.fix.part;
        w.job.continuous = o.continuous, w.job.atEnd = o.atEnd, w.job.hold = o.hold, w.job.at = o.at;
        w.carId = o.car >= 0 && o.car < (int)sim.cars.size() ? sim.cars[o.car].netId : 0;
        w.secs = ia.progress;
    }
    return w;
}

std::string encode(const Work& w) {
    std::string s = "work";
    char b[256];
    if (w.hand.any()) std::snprintf(b, sizeof b, " h=%d,%d", w.hand.kind, w.hand.id), s += b;
    if (w.jack) s += " j=1";
    if (w.chain) s += " c=1";
    if (w.carrying) {
        const WheelItem& it = w.carry;
        std::snprintf(b, sizeof b, " w=%.3f,%.3f,%.3f,%d,%d,%d", it.R, it.rimR, it.width, it.style, it.popped ? 1 : 0, it.tireOnly ? 1 : 0);
        s += b;
    }
    if (w.working()) {
        const InteractOption& o = w.job;
        std::snprintf(b, sizeof b, " do=%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f", (int)o.kind, (unsigned)w.carId, o.wheel, o.step, o.tool, o.part, (int)o.fix.kind,
                      o.fix.wheel, (int)o.fix.part, o.continuous ? 1 : 0, o.atEnd ? 1 : 0, o.hold, w.secs, o.at.x, o.at.y, o.at.z);
        s += b;
    }
    return s;
}

bool decode(const std::string& text, Work& out) {
    std::istringstream in(text);
    std::string tok;
    if (!(in >> tok) || tok != "work") return false;
    out = Work();
    std::vector<double> v;
    while (in >> tok) {
        const size_t eq = tok.find('=');
        if (eq == std::string::npos || !numbers(tok.substr(eq + 1), v)) continue;
        const std::string key = tok.substr(0, eq);
        if (key == "h" && v.size() >= 2 && whole(v[0], TK_TOOL, TK_JACK) && whole(v[1], 0, 255)) {
            const TrunkRef r{(int)v[0], (int)v[1]};
            if (validHand(r)) out.hand = r;
        } else if (key == "j") {
            out.jack = v[0] == 1;
        } else if (key == "c") {
            out.chain = v[0] == 1;
        } else if (key == "w" && v.size() >= 6 && inRange(v[0], 0.1, 1.0) && inRange(v[1], 0.05, v[0]) && inRange(v[2], 0.05, 1.0) && whole(v[3], 0, WHEEL_STYLES - 1)) {
            out.carrying = true;
            out.carry.R = v[0], out.carry.rimR = v[1], out.carry.width = v[2], out.carry.style = (int)v[3];
            out.carry.popped = v[4] == 1, out.carry.tireOnly = v[5] == 1;
        } else if (key == "do" && v.size() >= 16) {
            const bool ok = whole(v[0], IA_NONE + 1, IA_CLOSE_HOOD) && whole(v[1], 0, 4294967295.0) && whole(v[2], -1, 31) && whole(v[3], -1, 31) && whole(v[4], -1, TOOL_COUNT - 1) &&
                            whole(v[5], -1, PART_COUNT - 1) && whole(v[6], Fix::CV_AXLE, Fix::SPARK_PLUGS) && whole(v[7], -1, 31) && whole(v[8], 0, PART_COUNT - 1) &&
                            inRange(v[11], 0, 60) && inRange(v[12], 0, 600) && okPos(V3(v[13], v[14], v[15]));
            if (!ok) continue;
            InteractOption& o = out.job;
            o.kind = (InteractKind)(int)v[0];
            out.carId = (uint32_t)v[1];
            o.wheel = (int)v[2], o.step = (int)v[3], o.tool = (int)v[4], o.part = (int)v[5];
            o.fix.kind = (Fix::Kind)(int)v[6], o.fix.wheel = (int)v[7], o.fix.part = (RepairPart)(int)v[8];
            o.continuous = v[9] == 1, o.atEnd = v[10] == 1, o.hold = v[11];
            out.secs = v[12];
            o.at = V3(v[13], v[14], v[15]);
        }
    }
    return true;
}

void Sender::update(const Sim& sim, const Walker* wk, Interact& ia, double now, bool online, const std::function<void(const std::string&)>& send) {
    char b[160];
    // The news of the step just finished.
    if (online && ia.droppedPart >= 0 && ia.droppedPart < (int)sim.droppedParts.size()) {
        const Sim::DroppedPart& d = sim.droppedParts[ia.droppedPart];
        std::snprintf(b, sizeof b, "drop %d %.2f %.2f %.2f %.3f", d.part, d.p.x, d.p.y, d.p.z, d.yaw);
        send(b);
    }
    if (online && ia.fixedCar >= 0 && ia.fixedCar < (int)sim.cars.size() && sim.cars[ia.fixedCar].netId) {
        const Fix& f = ia.fixedFix;
        std::snprintf(b, sizeof b, "fixed %u %d %d %d %d", (unsigned)sim.cars[ia.fixedCar].netId, (int)f.kind, f.wheel, f.beam, f.axle);
        send(b);
    }
    ia.droppedPart = -1, ia.fixedCar = -1;
    if (!online) {
        last = Work(), lastAt = -1e9;
        jacks.clear();
        hoods.clear();
        return;
    }
    // The hoods of your vehicles.
    hoods.erase(std::remove_if(hoods.begin(), hoods.end(), [&](const HoodSent& h) { return carByNetId(sim, h.id) < 0; }), hoods.end());
    for (const Car& c : sim.cars) {
        if (c.remote || !c.netId || c.isProp() || !c.hasHood()) continue;
        auto it = std::find_if(hoods.begin(), hoods.end(), [&](const HoodSent& h) { return h.id == c.netId; });
        if (it == hoods.end()) {
            hoods.push_back(HoodSent{c.netId});
            it = hoods.end() - 1;
        }
        const bool open = c.hoodOpen() && !c.hoodTorn();
        if (open != it->open || (open && now - it->at >= kHoodRefresh)) {
            std::snprintf(b, sizeof b, "hood %u %d", (unsigned)c.netId, open ? 1 : 0);
            send(b);
            it->open = open, it->at = now;
        }
    }
    // What you hold and do.
    const Work w = workOf(sim, wk, ia);
    const bool changed = !w.same(last) || (w.working() && std::fabs(w.secs - (last.secs + (now - lastAt))) > 0.35);
    const bool due = !w.nothing() && now - lastAt >= kRefresh;
    if ((changed || due) && now - lastAt >= kMinGap) {
        send(encode(w));
        last = w, lastAt = now;
    }
    // The jacks under your vehicles.
    jacks.erase(std::remove_if(jacks.begin(), jacks.end(), [&](const JackSent& j) { return carByNetId(sim, j.id) < 0; }), jacks.end());
    for (const Car& c : sim.cars) {
        if (c.remote || !c.netId || c.isProp()) continue;
        auto it = std::find_if(jacks.begin(), jacks.end(), [&](const JackSent& j) { return j.id == c.netId; });
        const bool placed = c.jackPlaced && c.jackWheel >= 0 && c.jackWheel < (int)c.wheels.size();
        if (it == jacks.end()) {
            if (!placed) continue;
            jacks.push_back(JackSent{c.netId});
            it = jacks.end() - 1;
        }
        if (placed) {
            if (it->wheel != c.jackWheel || (std::fabs(c.jackHeight - it->height) > 0.01 && now - it->at >= kJackEvery) || now - it->at >= kJackRefresh) {
                std::snprintf(b, sizeof b, "jack %u %d %.3f %.3f %.2f %.2f %.2f", (unsigned)c.netId, c.jackWheel, c.jackBase, c.jackHeight, c.jackFoot.x, c.jackFoot.y, c.jackFoot.z);
                send(b);
                it->wheel = c.jackWheel, it->height = c.jackHeight, it->at = now;
            }
        } else if (it->wheel >= 0) {
            std::snprintf(b, sizeof b, "jack %u -1", (unsigned)c.netId);
            send(b);
            it->wheel = -1, it->at = now;
        }
    }
}

Remote::Player* Remote::find(const std::string& who) {
    for (Player& p : players)
        if (p.who == who) return &p;
    return nullptr;
}

bool Remote::onMessage(Sim& sim, int player, const std::string& who, const std::string& text, double now) {
    std::istringstream in(text);
    std::string tag;
    in >> tag;
    // A vehicle of theirs, here (theirs: its id says whose).
    auto theirs = [&](uint32_t id) -> Car* {
        const int ci = carByNetId(sim, id);
        if (ci < 0 || !sim.cars[ci].remote || sim.cars[ci].isProp() || NetSession::ownerOf(id) != player) return nullptr;
        return &sim.cars[ci];
    };
    if (tag == "work") {
        Work w;
        decode(text, w);
        auto it = std::find_if(players.begin(), players.end(), [&](const Player& p) { return p.player == player; });
        if (it == players.end()) {
            players.emplace_back();
            it = players.end() - 1;
            it->player = player;
        }
        it->who = who, it->work = w, it->heard = now;
        return true;
    }
    if (tag == "jack") {
        double id = 0, wheel = 0;
        if (!(in >> id >> wheel) || !whole(id, 1, 4294967295.0)) return true;
        Car* c = theirs((uint32_t)id);
        if (!c) return true;
        if (wheel < 0) {
            c->jackPlaced = false, c->jackWheel = -1, c->jackHeight = 0;
            jacks.erase(std::remove_if(jacks.begin(), jacks.end(), [&](const Jack& j) { return j.id == c->netId; }), jacks.end());
            return true;
        }
        double base = 0, height = 0;
        V3 foot;
        if (!(in >> base >> height >> foot.x >> foot.y >> foot.z) || !whole(wheel, 0, (int)c->wheels.size() - 1) || !inRange(base, -1, 3) || !inRange(height, 0, 2) || !okPos(foot)) return true;
        if (!c->jackPlaced || c->jackWheel != (int)wheel) c->jackHeight = height;
        c->jackPlaced = true, c->jackWheel = (int)wheel, c->jackBase = base, c->jackFoot = foot, c->jackWorked = true;
        auto it = std::find_if(jacks.begin(), jacks.end(), [&](const Jack& j) { return j.id == c->netId; });
        if (it == jacks.end()) jacks.push_back(Jack{c->netId}), it = jacks.end() - 1;
        it->height = height, it->heard = now;
        return true;
    }
    if (tag == "hood") {
        double id = 0, open = 0;
        if (!(in >> id >> open) || !whole(id, 1, 4294967295.0) || !whole(open, 0, 1)) return true;
        Car* c = theirs((uint32_t)id);
        if (!c || !c->hasHood()) return true;
        c->hood.dir = 0, c->hood.share = open > 0 ? 1 : 0;
        hoods.erase(std::remove_if(hoods.begin(), hoods.end(), [&](const Hood& h) { return h.id == c->netId; }), hoods.end());
        if (open > 0) hoods.push_back(Hood{c->netId, now});
        return true;
    }
    if (tag == "drop") {
        double part = 0;
        Sim::DroppedPart d;
        if (!(in >> part >> d.p.x >> d.p.y >> d.p.z >> d.yaw) || !whole(part, 0, PART_COUNT - 1) || !okPos(d.p) || !inRange(d.yaw, -100, 100)) return true;
        d.part = (int)part;
        sim.droppedParts.push_back(d);
        if (sim.droppedParts.size() > kMaxDropped) sim.droppedParts.erase(sim.droppedParts.begin());
        return true;
    }
    if (tag == "fixed") {
        double id = 0, kind = 0, wheel = 0, beam = 0, axle = 0;
        if (!(in >> id >> kind >> wheel >> beam >> axle) || !whole(id, 1, 4294967295.0) || !whole(kind, Fix::CV_AXLE, Fix::SPARK_PLUGS)) return true;
        Car* c = theirs((uint32_t)id);
        if (!c || !whole(wheel, -1, (int)c->wheels.size() - 1) || !whole(beam, -1, (int)c->beams.size() - 1) || !whole(axle, -1, (int)c->axles.size() - 1)) return true;
        Fix f = InteractOption().fix;
        f.kind = (Fix::Kind)(int)kind;
        f.wheel = (int)wheel, f.beam = (int)beam, f.axle = (int)axle;
        if (f.kind == Fix::DRIVE_SHAFT && f.beam < 0) return true;  // (applyFix reads it)
        applyFix(*c, f);
        return true;
    }
    return false;
}

void Remote::update(Sim& sim, double dt, double now) {
    players.erase(std::remove_if(players.begin(), players.end(), [&](const Player& p) { return now - p.heard > kExpiry; }), players.end());
    for (size_t k = 0; k < hoods.size();) {
        const int ci = carByNetId(sim, hoods[k].id);
        Car* c = ci >= 0 && sim.cars[ci].remote ? &sim.cars[ci] : nullptr;
        if (!c || now - hoods[k].heard > kHoodExpiry) {  // (gone, or its owner went quiet)
            if (c) c->hood.share = 0, c->hood.dir = 0;
            hoods.erase(hoods.begin() + k);
            continue;
        }
        k++;
    }
    for (size_t k = 0; k < jacks.size();) {
        const int ci = carByNetId(sim, jacks[k].id);
        Car* c = ci >= 0 && sim.cars[ci].remote ? &sim.cars[ci] : nullptr;
        if (!c || !c->jackPlaced || now - jacks[k].heard > kJackExpiry) {  // (gone, rebuilt, or its owner went quiet)
            if (c) c->jackPlaced = false, c->jackWheel = -1, c->jackHeight = 0;
            jacks.erase(jacks.begin() + k);
            continue;
        }
        const double step = 0.4 * std::max(0.0, dt);  // (m/s: a little quicker than anyone works one)
        c->jackHeight += clampd(jacks[k].height - c->jackHeight, -step, step);
        k++;
    }
}

bool Remote::handwork(const Sim& sim, Player& p, const V3& feet, double yaw, double crouch, double now, Handwork& out, V3* eyeOut) {
    const Work& w = p.work;
    out = Handwork();
    p.ia = Interact();
    if (w.nothing()) return false;
    Walker wk;
    wk.active = true, wk.p = feet, wk.yaw = yaw;
    wk.holdingJack = w.jack, wk.holdingChain = w.chain, wk.hand = w.hand;
    if (w.carrying) wk.carrying = w.carry;
    const double cr = clampd(crouch, 0, 1);
    const V3 eye = feet + V3(0, Walker::kEyeStand * (1 - (1 - Walker::kEyeCrouch) * cr), 0);
    if (eyeOut) *eyeOut = eye;
    V3 look(std::cos(yaw), -0.35, -std::sin(yaw));
    if (w.working()) {
        p.ia.current = w.job;
        p.ia.current.car = carByNetId(sim, w.carId);
        p.ia.current.loose = -1;  // (a wheel lying on their ground isn't on ours)
        const double secs = w.secs + clampd(now - p.heard, 0, kExpiry);
        p.ia.progress = std::max(1e-3, w.job.continuous || w.job.hold <= 0 ? secs : std::min(secs, w.job.hold));
        const V3 to = w.job.at - eye;
        if (len(to) > 0.05) look = to;
    }
    out = ::handwork(sim, wk, p.ia, eye, look, now, false);
    return true;
}

void Remote::sounds(Player& p, const Handwork& hw, const ToolSounds::Play& play, const V3& listener) { p.sounds.update(play, p.ia, hw, listener); }

}  // namespace repairsync
