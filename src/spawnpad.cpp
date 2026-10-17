#include "spawnpad.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGuard = 1.5;    // the slab's own ground reaches this far past its edge (the 1 m grid is bilinear: the slab must be flat to its edge)
constexpr double kBlend = 6.0;    // and the ground blends back into the land over this much more (at least: more where it has to be cut or filled deep), to either side and behind
constexpr double kMaxGrade = 0.12; // the ramp's grade: as long as it takes to come down at this
constexpr double kMaxRamp = 30.0;

// A pad's frame: u ahead (the way it faces), v to its left.
struct PadFrame {
    double x, z, fx, fz, lx, lz;
    PadFrame(const SpawnPad& s) : x(s.p.x), z(s.p.z), fx(std::cos(s.yaw)), fz(-std::sin(s.yaw)), lx(std::sin(s.yaw)), lz(std::cos(s.yaw)) {}
    void local(double wx, double wz, double& u, double& v) const {
        u = (wx - x) * fx + (wz - z) * fz;
        v = (wx - x) * lx + (wz - z) * lz;
    }
    V3 world(double u, double v) const { return V3(x + fx * u + lx * v, 0, z + fz * u + lz * v); }
};

// A post that goes when what it holds up does: a tree's trunk, a stump; not a fence's or a shed's.
bool looseProp(PropKind k) { return k != PROP_FENCE && k != PROP_GARAGE && k != PROP_CABIN; }
bool clearablePole(const World& w, const Pole& pl) {
    for (const Tree& t : w.trees)
        if (std::hypot(t.base.x - pl.x, t.base.z - pl.z) < 0.05) return true;
    for (const Prop& f : w.fixtures)
        if (looseProp(f.kind) && std::hypot(f.a.x - pl.x, f.a.z - pl.z) < 1.6) return true;
    return false;
}

// How far (u, v) is outside the slab and its ramp (negative inside).
double outside(const SpawnPad& s, double u, double v) {
    const double du = std::max(-s.back - u, u - (s.front + s.ramp)), dv = std::fabs(v) - s.half;
    if (du <= 0 && dv <= 0) return std::max(du, dv);
    return std::hypot(std::max(du, 0.0), std::max(dv, 0.0));
}

// The ground the slab sits on, before it changes: its mean height (what the pad is cut or filled to), and the
// ground at the middle of its front edge.
void sampleGround(const World& w, const SpawnPad& s, double& mean, double& ahead) {
    const PadFrame f(s);
    double sum = 0;
    int n = 0;
    for (double u = -s.back; u <= s.front + 1e-9; u += 1.0)
        for (double v = -s.half; v <= s.half + 1e-9; v += 1.0) {
            const V3 q = f.world(u, v);
            sum += w.height(q.x, q.z), n++;
        }
    mean = n ? sum / n : w.height(s.p.x, s.p.z);
    const V3 q = f.world(s.front, 0);
    ahead = w.height(q.x, q.z);
}

// Already a flat concrete paddock (the Frontier's): nothing for a pad to add.
bool concreteAlready(const World& w, const SpawnPad& s) {
    const PadFrame f(s);
    double lo = 1e9, hi = -1e9;
    for (double u = -s.back; u <= s.front + 1e-9; u += 1.0)
        for (double v = -s.half; v <= s.half + 1e-9; v += 1.0) {
            const V3 q = f.world(u, v);
            const int id = w.cellIndex(q.x, q.z);
            if (id < 0 || w.cellSurface(id) != SURF_PAVED || w.cellPaint(id) != PAINT_PAD) return false;
            const double y = w.height(q.x, q.z);
            lo = std::min(lo, y), hi = std::max(hi, y);
        }
    return hi - lo < 0.1;
}

// Takes what stands on the ground the pad takes over (and under the crowns over it) off the map.
void clearPad(World& w, const SpawnPad& s) {
    const PadFrame f(s);
    auto within = [&](double x, double z, double clear) {
        double u, v;
        f.local(x, z, u, v);
        return outside(s, u, v) < clear;
    };
    auto nearStation = [&](double x, double z) {
        for (const Station& st : w.stations)
            if (std::hypot(st.p.x - x, st.p.z - z) < 7.0) return true;
        return false;
    };
    auto drop = [](auto& vec, auto gone) { vec.erase(std::remove_if(vec.begin(), vec.end(), gone), vec.end()); };
    if (s.openAhead > 0) {  // the lane ahead: trees and rocks off it
        auto lane = [&](double x, double z, double r) {
            double u, v;
            f.local(x, z, u, v);
            return u > s.front && u < s.front + std::max(s.ramp, s.openAhead) && std::fabs(v) < 3.2 + r;
        };
        drop(w.rocks, [&](const Rock& r) { return lane(r.c.x, r.c.z, r.r); });
        drop(w.trees, [&](const Tree& t) { return lane(t.base.x, t.base.z, t.radius * 1.5); });
        drop(w.poles, [&](const Pole& p) { return lane(p.x, p.z, p.r) && clearablePole(w, p); });
        drop(w.fixtures, [&](const Prop& p) { return looseProp(p.kind) && lane(p.a.x, p.a.z, 0.5); });
        drop(w.logs, [&](const Log& l) { return lane(l.a.x, l.a.z, l.r) || lane(l.b.x, l.b.z, l.r) || lane((l.a.x + l.b.x) / 2, (l.a.z + l.b.z) / 2, l.r); });
        drop(w.bushes, [&](const Bush& b) { return lane(b.p.x, b.p.z, b.r); });
    }
    drop(w.rocks, [&](const Rock& r) { return within(r.c.x, r.c.z, 2.0 + r.r); });
    drop(w.trees, [&](const Tree& t) { return within(t.base.x, t.base.z, 2.0 + t.radius * 1.5); });
    drop(w.poles, [&](const Pole& p) { return within(p.x, p.z, 2.0) && !nearStation(p.x, p.z); });
    drop(w.boxes, [&](const Box& b) { return within(b.c.x, b.c.z, 1.0 + b.bound * 0.5) && !nearStation(b.c.x, b.c.z); });
    drop(w.logs, [&](const Log& l) { return within(l.a.x, l.a.z, 2.0) || within(l.b.x, l.b.z, 2.0) || within((l.a.x + l.b.x) / 2, (l.a.z + l.b.z) / 2, 2.0); });
    drop(w.bushes, [&](const Bush& b) { return within(b.p.x, b.p.z, 2.0 + b.r); });
    drop(w.fixtures, [&](const Prop& p) { return p.kind != PROP_GARAGE && within(p.a.x, p.a.z, 2.0); });
    drop(w.models, [&](const Placed& p) { return within(p.p.x, p.p.z, 2.0) && !nearStation(p.p.x, p.p.z); });
}

void build(World& w, SpawnPad& s) {
    double mean, ahead;
    sampleGround(w, s, mean, ahead);
    const double H0 = std::max(mean, ahead);
    // The ramp: down to the ground ahead at a gentle grade (as long as that takes).
    const PadFrame f(s);
    auto groundAt = [&](double u) {
        const V3 q = f.world(u, 0);
        return w.height(q.x, q.z);
    };
    double L = 0;
    for (int it = 0; it < 4; it++) L = std::min(kMaxRamp, std::fabs(H0 - groundAt(s.front + L)) / kMaxGrade);
    if (std::fabs(H0 - groundAt(s.front + L)) < 0.12) L = 0;  // (level with it already: the slab's own edge is the way off)
    s.ramp = L < 3.0 && L > 0 ? 3.0 : L;
    const double Hr = L > 0 ? groundAt(s.front + s.ramp) : H0;
    s.drop = H0 - Hr;
    s.p.y = H0;

    clearPad(w, s);

    // How far the ground has to come up or down along the slab's edges: the deeper, the longer the blend, so the banks stay gentle.
    double dig = 0;
    for (double u = -s.back; u <= s.front + 1e-9; u += 2.0)
        for (double v : {-s.half, s.half}) {
            const V3 q = f.world(u, v);
            dig = std::max(dig, std::fabs(H0 - w.height(q.x, q.z)));
        }
    for (double v = -s.half; v <= s.half + 1e-9; v += 2.0) {
        const V3 q = f.world(-s.back, v);
        dig = std::max(dig, std::fabs(H0 - w.height(q.x, q.z)));
    }
    const double blend = clampd(7.0 * dig, kBlend, 20.0);
    const double reach = kGuard + blend + 1;
    double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (double u : {-s.back - reach, s.front + s.ramp + reach})
        for (double v : {-s.half - reach, s.half + reach}) {
            const V3 q = f.world(u, v);
            x0 = std::min(x0, q.x), x1 = std::max(x1, q.x), z0 = std::min(z0, q.z), z1 = std::max(z1, q.z);
        }
    const bool wet = w.hasWater();
    for (int xi = (int)std::floor(x0); xi <= (int)std::ceil(x1); xi++)
        for (int zi = (int)std::floor(z0); zi <= (int)std::ceil(z1); zi++) {
            const int id = w.cellIndex(xi, zi);
            if (id < 0) continue;
            const V3 c = w.cellCenter(id);
            double u, v;
            f.local(c.x, c.z, u, v);
            const double d = outside(s, u, v);
            if (d >= kGuard + blend) continue;
            bool atStation = false;  // (a repair station's own ground is left as it is: its pad stays level)
            for (const Station& st : w.stations) atStation |= std::hypot(st.p.x - c.x, st.p.z - c.z) < 9.0;
            if (atStation && d > 0) continue;
            const double plane = u <= s.front || s.ramp <= 0 ? H0 : H0 - s.drop * clampd((u - s.front) / s.ramp, 0, 1);
            const double wgt = d <= kGuard ? 1.0 : 1.0 - smoothstep(kGuard, kGuard + blend, d);
            w.setCellHeight(id, (float)(c.y + (plane - c.y) * wgt));
            if (d <= -1.0) w.setCellSurface(id, SURF_PAVED, PAINT_PAD);  // (well inside the slab: the ground shader rims paved paint in yellow, which would show past its edge)
            if (wet && d <= kGuard && w.waterLevel(c.x, c.z) > -1e8) w.setCellWater(id, -1e9f);
        }

    // Drawn: the slab, its ramp, joints and paint.
    Placed pl;
    pl.id = "spawn_pad";
    pl.p = V3(s.p.x, H0, s.p.z);
    pl.yaw = s.yaw;
    pl.set = {{"back", s.back}, {"front", s.front}, {"half", s.half}, {"ramp", s.ramp}, {"drop", s.drop}, {"slots", (double)s.slots}, {"cols", (double)s.cols}};
    w.models.push_back(pl);
}

SpawnPad& add(World& w, SpawnPad s) {
    s.drawn = !concreteAlready(w, s);
    if (s.drawn) build(w, s);
    w.spawnPads.push_back(s);
    return w.spawnPads.back();
}

}  // namespace

const SpawnPad& addSpawnPad(World& w, V3 p, double yaw, int slots, int cols, double openAhead) {
    slots = std::max(1, slots), cols = std::max(1, cols);
    double umin = 0, vabs = 0;
    for (int k = 0; k < slots; k++) {
        double u, v;
        padSlotUV(cols, k, u, v);
        umin = std::min(umin, u), vabs = std::max(vabs, std::fabs(v));
    }
    SpawnPad s;
    s.p = p, s.yaw = yaw, s.slots = slots, s.cols = cols, s.openAhead = openAhead;
    s.back = -umin + 4.5, s.front = 5.5, s.half = std::max(vabs + 2.6, 3.4);
    return add(w, s);
}

const SpawnPad& addParkingPad(World& w, const std::vector<V3>& spots, double yaw) {
    SpawnPad s;
    s.yaw = yaw;
    const PadFrame f(s);  // (at the origin: just the directions)
    double u0 = 1e9, u1 = -1e9, v0 = 1e9, v1 = -1e9;
    for (const V3& q : spots) {
        double u, v;
        f.local(q.x, q.z, u, v);
        u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
    }
    const double uc = (u0 + u1) / 2, vc = (v0 + v1) / 2;
    s.p = V3(f.fx * uc + f.lx * vc, 0, f.fz * uc + f.lz * vc);
    s.slots = (int)spots.size(), s.cols = std::max(1, (int)spots.size());
    s.back = (u1 - u0) / 2 + 4.5, s.front = (u1 - u0) / 2 + 5.5, s.half = (v1 - v0) / 2 + 2.6;
    s.half = std::max(s.half, 3.4);
    return add(w, s);
}

namespace {

// Whether a pad laid at (p, yaw) would be a good place to start: nothing it can't clear (a repair shed, a cabin) on or in
// front of it, ground that's reasonably even under it and ahead of it, no water, and the way ahead open for 30 m.
bool goodSpot(const World& w, const SpawnPad& s0) {
    SpawnPad s = s0;
    const PadFrame f(s);
    auto blocked = [&](double x, double z, double u0, double u1, double half) {
        auto in = [&](double px, double pz, double r) {
            double u, v;
            f.local(px, pz, u, v);
            return u > u0 - r && u < u1 + r && std::fabs(v) < half + r;
        };
        // A repair shed (10 m by 7.4 and its roof): any point of its footprint in.
        for (const Station& st : w.stations) {
            const double sfx = std::cos(st.yaw), sfz = -std::sin(st.yaw);
            for (double a = -5.6; a <= 5.6; a += 1.0)
                for (double b = -4.4; b <= 4.4; b += 1.0) {
                    double u, v;
                    f.local(st.p.x + sfx * a + sfz * -b, st.p.z + sfz * a + sfx * b, u, v);
                    if (u > u0 - 0.5 && u < u1 + 0.5 && std::fabs(v) < half + 0.5) return true;
                }
        }
        for (const Prop& pr : w.fixtures)
            if (!looseProp(pr.kind) && pr.kind != PROP_FENCE && in(pr.a.x, pr.a.z, 3.5)) return true;
        (void)x, (void)z;
        // Posts (a fence, a shed's) that aren't a tree's.
        for (const Pole& pl : w.poles) {
            double u, v;
            f.local(pl.x, pl.z, u, v);
            if (!(u > u0 && u < u1 && std::fabs(v) < half + pl.r)) continue;
            if (!clearablePole(w, pl)) return true;
        }
        // Boxes (ramps, walls) that touch it: any of a box's corners and edge middles inside.
        for (const Box& b : w.boxes) {
            if (b.hidden && !b.solid) continue;
            for (int i = -1; i <= 1; i++)
                for (int j = -1; j <= 1; j++) {
                    const V3 q = b.c + b.ax[0] * (b.h.x * i) + b.ax[2] * (b.h.z * j);
                    double u, v;
                    f.local(q.x, q.z, u, v);
                    if (u > u0 + 0.5 && u < u1 - 0.5 && std::fabs(v) < half - 0.5) return true;
                }
        }
        return false;
    };
    // The pad itself (with its ramp, say 12 m), then the corridor ahead.
    if (blocked(0, 0, -s.back - 1.5, s.front + 12, s.half + 1.5)) return false;
    if (blocked(0, 0, s.front, 30, 2.4)) return false;
    double lo = 1e9, hi = -1e9;
    for (double u = -s.back; u <= s.front; u += 2.0)
        for (double v = -s.half; v <= s.half + 1e-9; v += 2.0) {
            const V3 q = f.world(u, v);
            const double h = w.height(q.x, q.z);
            lo = std::min(lo, h), hi = std::max(hi, h);
            if (w.waterLevel(q.x, q.z) > h - 0.3) return false;
        }
    if (hi - lo > 3.0) return false;
    double prev = (lo + hi) / 2;
    for (double u = s.front; u <= 30; u += 2.0) {
        const V3 q = f.world(u, 0);
        const double h = w.height(q.x, q.z);
        if (std::fabs(h - prev) > 0.7 || w.waterLevel(q.x, q.z) > h - 0.3) return false;  // (35% and over: a bank or a drop)
        prev = h;
    }
    return true;
}

}  // namespace

void addMapSpawnPads(World& w) {
    if (w.map == MAP_FREEWAY) return;  // (it starts in a lane of the road itself)
    // The map's own start if it's a good one, else the nearest spot that is (round it, turned toward open ground).
    SpawnPad probe;
    probe.slots = 8, probe.cols = 3;
    probe.back = kPadRowGap * 2 + 4.5, probe.front = 5.5, probe.half = kPadColGap + 2.6;
    V3 at = w.spawn;
    double yaw = w.spawnYaw;
    double bestCost = 1e18;
    for (double r = 0; r <= 30; r += 3)
        for (int k = 0; k < (r == 0 ? 1 : 8); k++) {
            const double ang = k * M_PI / 4;
            const V3 q = w.spawn + V3(std::cos(ang), 0, std::sin(ang)) * r;
            for (int d = 0; d <= 12; d++) {
                const double dy = (d % 2 ? -1 : 1) * ((d + 1) / 2) * M_PI / 12;  // 0, -15, +15, ... 180 degrees
                const double cost = r + std::fabs(dy) * 4;
                if (cost >= bestCost) continue;
                probe.p = V3(q.x, w.height(q.x, q.z), q.z), probe.yaw = w.spawnYaw + dy;
                if (goodSpot(w, probe)) bestCost = cost, at = q, yaw = probe.yaw;
            }
        }
    if (bestCost < 1e17 && (at.x != w.spawn.x || at.z != w.spawn.z || yaw != w.spawnYaw)) w.spawnYaw = yaw, w.spawn = V3(at.x, w.height(at.x, at.z), at.z);
    // And the corridor ahead of it clear of whatever can be taken off the ground.
    const SpawnPad& s = addSpawnPad(w, w.spawn, w.spawnYaw, 8, 3, 30);
    w.spawn = s.p;
    if (w.parking.empty()) return;
    // The parked trucks beside it, on a slab of their own when they're well clear of this one (where they stand close,
    // whatever the start's lot is, they keep).
    const PadFrame f(s);
    for (const V3& q : w.parking) {
        double u, v;
        f.local(q.x, q.z, u, v);
        if (outside(s, u, v) < 14.0) return;
    }
    addParkingPad(w, w.parking, w.parkingYaw);
}

bool World::spawnSlot(int k, V3& p, double& yaw) const {
    for (const SpawnPad& s : spawnPads) {
        if (std::hypot(s.p.x - spawn.x, s.p.z - spawn.z) > 0.5) continue;
        if (k < 0 || k >= s.slots) return false;
        double u, v;
        padSlotUV(s.cols, k, u, v);
        const PadFrame f(s);
        p = f.world(u, v);
        p.y = s.p.y;
        yaw = s.yaw;
        return true;
    }
    return false;
}
