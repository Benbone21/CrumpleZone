#include "sections.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Calibration (from the handoff's probes): grid walls >= 65 deg seal a chokepoint even for the rock
// buggy; a set-up Bronco holds a 35 deg side slope and rolls at 40; stock slides at 30 (dirt);
// shallow mud pits are easy, so pits get steep exit banks and water. No step in a notch staircase
// goes over ~0.4 m (the Bronco's limit is 0.45 on square ledges).

namespace {

// ---- Tuning: every range a generator draws from. Lengths and heights are metres, angles degrees;
// positions along the section (u) are from the entry. ----------------------------------------------
struct Range {
    double lo, hi;
};

struct Spec {
    const char* name;
    double length;  // entry to exit
    double half;    // cleared ground each side of the trail
};
constexpr Spec kSpecs[SEC_COUNT] = {
    {"STEEP HILL", 140, 28}, {"MUD PIT", 150, 30}, {"ROCKY RIVER", 140, 32}, {"ROCK WALL", 150, 34},
    {"DUNES", 440, 80},      {"CLIFF SHELF", 180, 33}, {"TIMBER", 140, 30},
};
constexpr double kApron = 20;        // flat run-in and run-out inside the cleared ground
constexpr double kBlend = 24;        // the ground blends back to its surroundings over this far
constexpr double kMaxSlope = 0.04;   // the section tilts to follow the ground along its length, this much at most
constexpr double kLandAmp = 7, kLandFine = 2;  // rolling country under everything: broad and fine relief
constexpr double kForestPerM2 = 0.32;          // tree attempts per square metre (before density)
constexpr double kPadFlat = 18;                // the first and last this many m of a section are as flat as the ground was (the stitch points)

// Steep hill: a loose scree climb in a gully, rock ledges across it near the top.
constexpr double kHillStart = 45;                    // the climb starts here
constexpr Range kHillLen = {34, 44}, kHillAngle = {24, 30}, kHillLedgeH = {0.32, 0.5};
constexpr double kHillTrailHalf = 2.6, kHillTrailBlend = 10, kHillWallHalf = 4.2, kHillWallDeg = 68, kHillWallH = 4.5;
constexpr int kHillLedges = 3;

// Mud pit: a long pit, floor 0.3-1.5 m (varying), standing water, a steep exit bank.
constexpr double kMudStart = 40, kMudHalf = 7.5, kMudDepthMin = 0.3, kMudDepthVar = 1.2, kMudWater = -0.12;
constexpr double kMudBankIn = 3.0, kMudBankOut = 1.1;  // entry gentle (3 m), exit 1:1
constexpr double kNearH = 10;  // (that height, for the timber and the mud pit's tree walls)
constexpr Range kMudLen = {42, 56};
constexpr double kMudClear = 3.5;  // trunks stand this far back from the mud's edge (and so from the trail, the camera's room)

// Rocky river: a channel across the trail, knee-to-thigh deep, a current, boulders everywhere.
constexpr double kRiverAt = 65;  // the channel's middle
constexpr Range kRiverWidth = {20, 28}, kRiverDepth = {1.2, 1.5}, kRiverSkew = {-0.35, 0.35}, kRiverWater = {0.55, 0.85};
constexpr double kRiverCurrent = 1.4;
constexpr int kRiverBoulders = 140;

// Rock wall (v3): sandstone ledges in a slot canyon.
constexpr double kWallStart = 36;  // the first ledge's mean position
constexpr int kWallLedges = 6;
constexpr Range kWallRise = {0.75, 1.15}, kWallLastRise = {1.3, 1.6}, kWallGap = {9, 12}, kWallAmp = {0.8, 2.2}, kWallTilt = {-0.10, 0.10};
constexpr double kWallSqueezeAfter = 2, kWallSqueezeLen = 8, kWallSqueezeHalf = 2.7, kWallHeight = 8, kWallStep = 0.32;  // (a hull rock's top is lumpy: steps are cut finer than the 0.4 limit)
constexpr double kWallRimSlope = 0.12, kWallFadeIn = 36;  // the land rises this much per m beyond the canyon's walls; the walls fade in over kWallFadeIn m

// Dunes: a sea of sand (duneHeight below): ridges with gentle windward backs and steep slip faces.
constexpr Range kDuneWind = {18, 38}, kDuneSpacing = {125, 150}, kDuneCrest = {18, 21};  // degrees from the heading; m; m
constexpr double kDuneRepose = 32;   // sand holds no steeper than this (degrees): the field is relaxed to it
constexpr double kDuneHardpan = 0.7, kDunePatch = 0.5;  // hollows lower than this above the floor (m) are hard ground where the patch noise tops that
constexpr int kDuneSmooth = 2, kDuneRelax = 60;  // passes rounding the crests, passes settling the slopes

// Cliff shelf: a one-truck shelf cut into a cliff, a real drop off the edge, fallen rock and talus, scanned rock to
// climb, a line through it all. The terrain's 1 m grid fills the corner where a road meets a wall into a gentler ramp
// than the face, and a wheel is held up by the ground under its hub alone, so tires sank 10 cm into the foot of a bare
// wall (measured): the cliff stands back from the road's edge and a talus of scanned rocks stands against its foot (the toe), which the
// tires meet exactly.
constexpr Range kShelfHalf = {2.05, 2.35}, kShelfTilt = {4, 8};  // 4.1-4.7 m of road, 0.4 of it under the toe; leans you toward the drop
constexpr double kShelfDrop = 22, kShelfCliffMax = 24, kShelfEase = 40;
constexpr double kShelfLine = 1.3;  // the line through it keeps 2.6 m clear (a Blazer is 2.0 with its mirrors)
constexpr double kShelfBack = 0.6;  // the cliff's foot stands this far behind the road's edge (the toe fills the gap)
constexpr double kShelfToe = 0.4;  // the toe's rocks reach this far in from the road's edge at their widest
constexpr double kShelfRockTop = 0.24;  // a rock in the line stands no higher than this
constexpr double kShelfBuriedLine = 0.40;  // a big buried boulder's crown in the line stands no higher than this over the road (its mound and its cap together)


// Timber: a forest track blocked by standing trees and windfall.
constexpr double kTimberHalf = 1.9;
constexpr double kTimberClear = 4.2;  // trunks start this far from the centre line: 2 m from the track's edge, room for a truck, its mirrors and the camera
constexpr double kTimberNear = 6.0;  // within this far of the trunks' line the trees stay under kNearH tall, so crowns don't close over the track
constexpr Range kTimberLog = {9, 14}, kTimberLogR = {0.32, 0.45};

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9e3779b9u) { next(); }
    uint32_t next() { s ^= s << 13, s ^= s >> 17, s ^= s << 5; return s; }
    double operator()() { return (next() & 0xFFFFFF) / double(0x1000000); }
    double range(double a, double b) { return a + (b - a) * (*this)(); }
    double range(const Range& r) { return range(r.lo, r.hi); }
};
double sq(double v) { return v * v; }

// Rolling country under the section (seeded offset into the shared noise).
double land(double u, double v, uint32_t seed) {
    const double ox = (seed % 997) * 13.1, oz = (seed % 991) * 7.7;
    return kLandAmp * (fbmNoise(u * 0.012 + ox, v * 0.012 + oz, 4) - 0.5) + kLandFine * (valueNoise(u * 0.05 + ox, v * 0.05 + oz) - 0.5);
}

}  // namespace

void clearObjectsWhere(World& w, const std::function<bool(double, double)>& in) {
    auto drop = [](auto& vec, auto gone) { vec.erase(std::remove_if(vec.begin(), vec.end(), gone), vec.end()); };
    drop(w.rocks, [&](const Rock& r) { return in(r.c.x, r.c.z); });
    drop(w.trees, [&](const Tree& t) { return in(t.base.x, t.base.z); });
    drop(w.poles, [&](const Pole& p) { return in(p.x, p.z); });
    drop(w.boxes, [&](const Box& b) { return in(b.c.x, b.c.z); });
    drop(w.logs, [&](const Log& l) { return in(l.a.x, l.a.z) || in(l.b.x, l.b.z); });
    drop(w.bushes, [&](const Bush& b) { return in(b.p.x, b.p.z); });
    drop(w.fixtures, [&](const Prop& p) { return in(p.a.x, p.a.z); });
    drop(w.models, [&](const Placed& p) { return in(p.p.x, p.p.z); });
    drop(w.stations, [&](const Station& s) { return in(s.p.x, s.p.z); });
}

namespace {

// A section's working copy of the ground: every cell its footprint touches, in local (u, v), with
// the height measured from the entry and the surface and water to give it. commit() blends that
// over the world's ground; objects go in after it (they sit on the finished ground).
struct Canvas {
    struct Cell {
        int id;
        float u, v, wgt;   // wgt: 1 on the cleared ground, 0 at the edge of the blend
        float h, orig;     // local height (above the entry's plane); the world's height before
        int16_t surf = -1; // -1: leave it
        uint8_t paint = 0;
        float water = -1e9f, fu = 0, fv = 0;  // water level above the plane, flow (local)
    };
    World& w;
    const SectionFootprint fp;
    const SectionFrame f;
    const double dx, dz, px, pz;  // along, and to the left (world x, z of each)
    double ey, slope = 0;
    const uint32_t seed;
    Rng rng;
    std::vector<Cell> cells;
    std::vector<LevelObject>* rec;  // (the editor: also keep what's stood up)
    std::vector<std::pair<double, double>> lineUV;  // (u, v) of the line meant to be driven, if it isn't straight down the middle
    struct Spot {
        double u, v, r;
    };
    std::vector<Spot> spots;  // the boulders stood up so far (they don't overlap much)
    int rockN = 0;
    std::vector<V3> line;           // the marked way through, if the section has one (world coordinates, ~1 m apart)

    Canvas(World& w, SectionType t, uint32_t seed, const SectionFrame& fr, std::vector<LevelObject>* record = nullptr)
        : w(w), fp(sectionFootprint(t)), f(fr), dx(std::cos(fr.heading)), dz(-std::sin(fr.heading)), px(std::sin(fr.heading)), pz(std::cos(fr.heading)),
          ey(fr.entry.y), seed(seed), rng((seed + 1) * 1009u + (uint32_t)t), rec(record) {
        const double reachU0 = -fp.apron - fp.blend, reachU1 = fp.length + fp.apron + fp.blend, reachV = fp.half + fp.blend;
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (double uu : {reachU0, reachU1})
            for (double vv : {-reachV, reachV}) {
                const V3 p = at(uu, vv);
                x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
            }
        // The ground tilts along the section to follow the land (a little), so run-in and run-out meet it.
        const V3 a = at(-fp.apron, 0), b = at(fp.length + fp.apron, 0);
        slope = clampd((w.height(b.x, b.z) - w.height(a.x, a.z)) / (fp.length + 2 * fp.apron), -kMaxSlope, kMaxSlope);
        for (int xi = (int)std::floor(x0); xi <= (int)std::ceil(x1); xi++)
            for (int zi = (int)std::floor(z0); zi <= (int)std::ceil(z1); zi++) {
                const int id = w.cellIndex(xi, zi);
                if (id < 0) continue;
                const V3 c = w.cellCenter(id);
                double u, v;
                local(c.x, c.z, u, v);
                const double d = outside(u, v);
                if (d >= fp.blend) continue;
                Cell k;
                k.id = id, k.u = (float)u, k.v = (float)v, k.wgt = (float)(d <= 0 ? 1.0 : smoothstep(fp.blend, 0, d));
                k.orig = w.cellHeight(id);
                k.h = (float)(land(u, v, seed) * smoothstep(kPadFlat - 3, kPadFlat + 17, u) * smoothstep(fp.length - kPadFlat + 3, fp.length - kPadFlat - 17, u));  // (flat for the pad's 15 m at both ends)
                if (k.wgt > 0.5f && !fr.keepCountry) k.surf = SURF_GRASS, k.paint = PAINT_NONE;  // (on country of its own the ground keeps its own kind: sand stays sand)
                cells.push_back(k);
            }
    }
    void local(double x, double z, double& u, double& v) const {
        u = (x - f.entry.x) * dx + (z - f.entry.z) * dz;
        v = (x - f.entry.x) * px + (z - f.entry.z) * pz;
    }
    // World point at (u, v), `y` above the plane through the entry.
    V3 at(double u, double v, double y = 0) const { return V3(f.entry.x + dx * u + px * v, ey + slope * u + y, f.entry.z + dz * u + pz * v); }
    // How far outside the cleared ground (negative inside).
    double outside(double u, double v) const { return std::max({-fp.apron - u, u - (fp.length + fp.apron), std::fabs(v) - fp.half}); }

    template <class F> void each(double u0, double u1, double v0, double v1, F fn) {
        for (size_t i = 0; i < cells.size(); i++) {
            const Cell& k = cells[i];
            if (k.u >= u0 && k.u <= u1 && k.v >= v0 && k.v <= v1) fn((int)i, (double)k.u, (double)k.v);
        }
    }
    void setH(int i, double h) { cells[i].h = (float)h; }
    double getH(int i) const { return cells[i].h; }
    void surface(int i, Surface s, uint8_t look) { cells[i].surf = s, cells[i].paint = look; }
    void paint(int i, Surface s) {
        static const uint8_t look[SURF_COUNT] = {PAINT_PAD, PAINT_NONE, PAINT_TRAIL, PAINT_ROCK, PAINT_SCREE, PAINT_GRAVEL,
                                                 PAINT_SAND, PAINT_MUD, PAINT_SNOW, PAINT_ICE, PAINT_WET};
        surface(i, s, look[s]);
    }
    void water(int i, double level, double fu = 0, double fv = 0) { cells[i].water = (float)level, cells[i].fu = (float)fu, cells[i].fv = (float)fv; }

    // Takes the ground (everything standing on it goes), then blends this over it. With f.keepCountry only the section's own
    // ground is cleared: what grows in the blend (the country round it) stays, and is set down on the new ground after.
    struct Seat {
        int kind, i;   // 0 rock, 1 tree, 2 pole, 3 bush, 4 log
        double y;      // the ground there before
    };
    std::vector<Seat> ring;
    void clearGround() {
        const bool keep = f.keepCountry;
        clearObjectsWhere(w, [&](double x, double z) {
            double u, v;
            local(x, z, u, v);
            return outside(u, v) < (keep ? 0.5 : fp.blend + 1);
        });
        if (!keep) return;
        auto inRing = [&](double x, double z) {
            double u, v;
            local(x, z, u, v);
            return outside(u, v) < fp.blend + 1;
        };
        for (int i = 0; i < (int)w.rocks.size(); i++)
            if (inRing(w.rocks[i].c.x, w.rocks[i].c.z)) ring.push_back({0, i, w.height(w.rocks[i].c.x, w.rocks[i].c.z)});
        for (int i = 0; i < (int)w.trees.size(); i++)
            if (inRing(w.trees[i].base.x, w.trees[i].base.z)) ring.push_back({1, i, w.height(w.trees[i].base.x, w.trees[i].base.z)});
        for (int i = 0; i < (int)w.poles.size(); i++)
            if (inRing(w.poles[i].x, w.poles[i].z)) ring.push_back({2, i, w.height(w.poles[i].x, w.poles[i].z)});
        for (int i = 0; i < (int)w.bushes.size(); i++)
            if (inRing(w.bushes[i].p.x, w.bushes[i].p.z)) ring.push_back({3, i, w.height(w.bushes[i].p.x, w.bushes[i].p.z)});
        for (int i = 0; i < (int)w.logs.size(); i++) {
            const V3 m = (w.logs[i].a + w.logs[i].b) * 0.5;
            if (inRing(m.x, m.z)) ring.push_back({4, i, w.height(m.x, m.z)});
        }
    }
    // What stood in the blend moves with the ground under it.
    void reseat() {
        for (const Seat& s : ring) {
            const double dy = [&] {
                switch (s.kind) {
                    case 0: return w.height(w.rocks[s.i].c.x, w.rocks[s.i].c.z) - s.y;
                    case 1: return w.height(w.trees[s.i].base.x, w.trees[s.i].base.z) - s.y;
                    case 2: return w.height(w.poles[s.i].x, w.poles[s.i].z) - s.y;
                    case 3: return w.height(w.bushes[s.i].p.x, w.bushes[s.i].p.z) - s.y;
                    default: {
                        const V3 m = (w.logs[s.i].a + w.logs[s.i].b) * 0.5;
                        return w.height(m.x, m.z) - s.y;
                    }
                }
            }();
            if (std::fabs(dy) < 1e-4) continue;
            switch (s.kind) {
                case 0: {
                    Rock& r = w.rocks[s.i];
                    for (V3& q : r.v) q.y += dy;
                    r.c.y += dy, r.origin.y += dy;
                    for (size_t f = 0; f < r.f.size(); f++) r.fd[f] = dot(r.fn[f], r.v[r.f[f][0]]);
                    break;
                }
                case 1: w.trees[s.i].base.y += dy; break;
                case 2: w.poles[s.i].y0 += dy, w.poles[s.i].y1 += dy; break;
                case 3: w.bushes[s.i].p.y += dy; break;
                default: w.logs[s.i].a.y += dy, w.logs[s.i].b.y += dy; break;
            }
        }
        ring.clear();
    }
    void commit() {
        for (const Cell& k : cells) {
            const double plane = ey + slope * k.u;
            w.setCellHeight(k.id, (float)(k.orig + (plane + k.h - k.orig) * k.wgt));
            if (k.wgt <= 0.5f) continue;
            if (k.surf >= 0) w.setCellSurface(k.id, (uint8_t)k.surf, k.paint);
            if (k.water > -1e8f) {
                w.setCellWater(k.id, (float)(plane + k.water));
                w.flowX[k.id] = (float)(k.fu * dx + k.fv * px), w.flowZ[k.id] = (float)(k.fu * dz + k.fv * pz);
            }
        }
        reseat();
    }

    // ---- Objects (after commit(): they sit on the finished ground) ------------------------------
    Rock& rock(double u, double v, const V3& r, double top, uint32_t rseed, float tint = 1, const RockPose* pose = nullptr) {
        const V3 p = at(u, v);
        if (rec) {
            LevelObject o;
            o.kind = OBJ_ROCK, o.p = p, o.size = r, o.top = top, o.seed = rseed, o.col[0] = tint;
            if (pose) o.sub = 1, o.yaw = pose->yaw, o.pitch = pose->pitch, o.roll = pose->roll;
            rec->push_back(o);
        }
        return w.addRock(p.x, p.z, r, top, rseed, tint, pose);
    }
    // A hull rock laid in place, the stuff ledges, steps, ramps and toes are made of: half-sizes `r` (long, up, across), its
    // long axis along `dir` (radians from +u toward +v) and climbing `pitch`, its top leaning `lean` toward +v, its highest
    // point `topH` above the section's plane at u (so the ground there is likely above its foot: it's sunk in). It's made
    // tall enough to stand on the ground it's in (a hull rock is placed by the ground at its centre alone).
    Rock& laid(double u, double v, double dir, V3 r, double topH, double pitch = 0, double lean = 0, float tint = 1) {
        const V3 p = at(u, v);
        const double top = std::max(0.06, ey + slope * u + topH - w.height(p.x, p.z));
        r.y = std::max(r.y, (top + 0.25) / 2);
        RockPose pose;
        pose.yaw = f.heading - dir, pose.pitch = pitch;
        if (lean != 0) {  // (the roll that tips the top toward +v)
            const V3 toward(px, 0, pz);
            const double cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pitch), sp = std::sin(pitch);
            const V3 along(cy * cp, sp, -sy * cp), up0(-cy * sp, cp, sy * sp);
            pose.roll = dot(rotateAxis(up0, along, 0.1), toward) > 0 ? lean : -lean;
        }
        spots.push_back({u, v, std::min(r.x, r.z)});
        return rock(u, v, r, top, seed * 211 + (uint32_t)rockN++, tint, &pose);
    }
    // The finished ground at (u, v), above the plane through the entry (after commit()).
    double ground(double u, double v) const {
        const V3 p = at(u, v);
        return w.height(p.x, p.z) - (ey + slope * u);
    }
    // A boulder in the rocky river's style: a scanned hull of "radius" r, flattened, sunk in about half (`top` m
    // of it showing above the ground, or `lift` more than that). Not on top of another one.
    bool boulder(double u, double v, double r, double lift = 0, double top = 0) {
        for (const Spot& sp : spots)
            if (std::hypot(sp.u - u, sp.v - v) < 0.45 * (sp.r + r)) return false;
        spots.push_back({u, v, r});
        const double hy = r * rng.range(0.55, 0.8), hz = r * rng.range(0.7, 1.0), tp = top > 0 ? top : r * rng.range(0.5, 0.8) + lift;
        rock(u, v, V3(r, hy, hz), tp, seed * 131 + (uint32_t)rockN++, (float)rng.range(0.85, 1.1));
        return true;
    }
    // A rock slab: an oriented box (yaw, pitch, roll in the section's frame) drawn and gripping as rock.
    void slab(double u, double y, double v, const V3& h, double yaw, double pitch, double roll, const V3& col) {
        w.addBox(at(u, v, y), h, yaw + f.heading, pitch, roll, col);
        w.boxes.back().rock = true;
        if (rec) {
            LevelObject o;
            o.kind = OBJ_BOX, o.p = at(u, v, y), o.size = h, o.yaw = yaw + f.heading, o.pitch = pitch, o.roll = roll, o.sub = 1;
            o.col[0] = (float)col.x, o.col[1] = (float)col.y, o.col[2] = (float)col.z;
            rec->push_back(o);
        }
    }
    void tree(double u, double v, double h, int kind, float tint = 1) {
        const V3 p = at(u, v);
        if (rec) {
            LevelObject o;
            o.kind = OBJ_TREE, o.p = p, o.size = V3(0, h, 0), o.sub = kind, o.col[0] = tint;
            rec->push_back(o);
        }
        w.addTree(p.x, p.z, h, kind, tint);
    }
    // Forest outside |v| > clear (dense when `wall`: trunks too close to drive between).
    // Trees within `lowBand` m of `clear` stay under `lowH` tall (their crowns reach less far over the track).
    void forestLow(double clear, double density, bool wall, double lowBand, double lowH) {
        forest(clear, density, wall, -fp.apron, fp.length + fp.apron, kForestPerM2, lowBand, lowH);
    }
    void forest(double clear, double density, bool wall, double u0, double u1, double perM2 = kForestPerM2, double lowBand = 0, double lowH = 17) {
        const int attempts = (int)((u1 - u0) * 2 * (fp.half - 1) * perM2);
        for (int k = 0; k < attempts; k++) {
            const double u = rng.range(u0, u1), v = rng.range(-(fp.half - 1), fp.half - 1);
            const double lat = std::fabs(v);
            if (lat < clear) continue;
            const bool edge = wall && lat < clear + 8;
            if (!edge && rng() > density) continue;
            if (edge && rng() > 0.9) continue;
            const V3 p = at(u, v);
            if (w.waterLevel(p.x, p.z) > w.height(p.x, p.z) - 0.2) continue;
            const int kind = rng() < 0.75 ? 0 : (rng() < 0.8 ? 1 : 2);
            const double hi = lat < clear + lowBand ? lowH : 17;
            tree(u, v, rng.range(std::min(8.0, hi - 1), hi), kind, (float)rng.range(0.8, 1.15));
        }
    }
    void forest(double clear, double density, bool wall) { forest(clear, density, wall, -fp.apron, fp.length + fp.apron); }
};

// A trail along the section at heights y(u), `half` wide, blended into the land within `blend`.
template <class Y, class S> void trail(Canvas& c, double half, double blend, Y y, S surf) {
    c.each(-c.fp.apron, c.fp.length + c.fp.apron, -half - blend, half + blend, [&](int id, double u, double v) {
        const double e = std::fabs(v) - half;
        const double wgt = e < 0.5 ? 1.0 : smoothstep(blend, 0.5, e);
        c.setH(id, c.getH(id) + (y(u) - c.getH(id)) * wgt);
        if (e < 0.5) c.paint(id, surf(u));
    });
}
// Walls either side: from |v| = half the ground rises at `deg` to H(u) above the trail (seals it).
template <class HF, class Y> void walls(Canvas& c, double ua, double ub, double half, double deg, HF height, Y y, Surface face) {
    const double slopeTan = std::tan(deg * M_PI / 180);
    c.each(ua - 10, ub + 10, -c.fp.half, c.fp.half, [&](int id, double u, double v) {
        const double e = std::fabs(v) - half;
        if (e < 0) return;
        const double H = height(u), run = H / slopeTan;
        const double fade = smoothstep(ua - 10, ua, u) * smoothstep(ub + 10, ub, u);
        const double rise = e < run ? H * e / run : H + 0.15 * (e - run);
        const double target = y(u) + rise + 0.6 * (valueNoise(u * 0.4, v * 0.4) - 0.5) * (e < run ? 1 : 0);
        c.setH(id, std::max(c.getH(id), c.getH(id) + (target - c.getH(id)) * fade));
        if (e < run + 1 && fade > 0.5) c.paint(id, face);
    });
}

// How big the river's boulders are: `small` of them 0.25-0.5 m, `mid` 0.5-0.85, the rest 0.9-1.4 (all times k).
double boulderR(Rng& g, double small, double mid, double k = 1) {
    const double q = g();
    return k * (q < small ? g.range(0.25, 0.5) : q < small + mid ? g.range(0.5, 0.85) : g.range(0.9, 1.4));
}

// Uneven ground: bumps and dips (2-8 m across, `amp` m high), the odd hole, a washed-out gully across the line,
// off-camber dips (one side low), tyre ruts, and mounds. Flat for the first and last kPadFlat m (and eased in).
double roughNoise(double u, double v, uint32_t seed) {
    const double ox = (seed % 997) * 5.3 + 11, oz = (seed % 991) * 3.7 + 7;
    return 0.65 * (2 * valueNoise(u / 7 + ox, v / 5 + oz) - 1) + 0.35 * (2 * valueNoise(u / 3 + oz, v / 2.5 + ox) - 1);
}
struct Rough {
    double amp = 0.25;  // peak height of the bumps (m)
    double rut = 0;     // depth of two tyre grooves along the line, 0 for none
    int holes = 2, washouts = 1, cambers = 1;
    struct Mound {
        double u, v, sigma, h;
    };
    std::vector<Mound> mounds;
    std::vector<double> keep;  // u's kept smooth (a ledge stands here)
};
double padEnv(double u, double L) { return smoothstep(kPadFlat, kPadFlat + 14, u) * smoothstep(L - kPadFlat, L - kPadFlat - 14, u); }
template <class LV> void roughen(Canvas& c, double half, double blend, const Rough& r, LV lineV) {
    const double L = c.fp.length;
    auto nearKeep = [&](double u) {
        double d = 1e9;
        for (double k : r.keep) d = std::min(d, std::fabs(u - k));
        return d;
    };
    auto pick = [&](double lo, double hi) {  // a u away from the pads' ends and the kept ones
        for (int t = 0; t < 40; t++) {
            const double u = c.rng.range(lo, hi);
            if (nearKeep(u) > 4.5) return u;
        }
        return c.rng.range(lo, hi);
    };
    const double lo = kPadFlat + 8, hi = L - kPadFlat - 8;
    struct Hole {
        double u, v, rad, depth;
    };
    std::vector<Hole> holes;
    for (int i = 0; i < r.holes; i++) {
        const double u = pick(lo, hi);
        holes.push_back({u, lineV(u) + c.rng.range(-0.9, 0.9), c.rng.range(1.2, 1.9), c.rng.range(0.22, 0.4)});
    }
    struct Wash {
        double u, skew, sigma, depth;
    };
    std::vector<Wash> washes;
    for (int i = 0; i < r.washouts; i++) washes.push_back({pick(lo, hi), c.rng.range(-0.5, 0.5), c.rng.range(0.9, 1.4), c.rng.range(0.25, 0.4)});
    struct Cam {
        double u, tilt, sigma;
    };
    std::vector<Cam> cams;
    for (int i = 0; i < r.cambers; i++) cams.push_back({pick(lo, hi), (c.rng() < 0.5 ? -1 : 1) * c.rng.range(0.12, 0.2), c.rng.range(3.5, 5.5)});
    const double ph = c.rng.range(0, 6.28);
    c.each(kPadFlat, L - kPadFlat, -half - blend, half + blend, [&](int id, double u, double v) {
        const double e = std::fabs(v) - half, wgt = e < 0.5 ? 1.0 : smoothstep(blend, 0.5, e), env = padEnv(u, L);
        if (wgt <= 0 || env <= 0) return;
        const double lv = lineV(u), dv = v - lv;
        double dh = r.amp * 1.4 * roughNoise(u, v, c.seed) * smoothstep(0.8, 3.2, nearKeep(u));
        for (const Hole& h : holes) dh -= h.depth * std::exp(-(sq(u - h.u) + sq(v - h.v)) / sq(h.rad * 0.6));
        for (const Wash& w : washes) dh -= w.depth * std::exp(-sq((u - w.u + w.skew * v) / w.sigma));
        for (const Cam& m : cams) dh -= m.tilt * clampd(dv, -3, 3) * std::exp(-sq((u - m.u) / m.sigma)) + 0.08 * std::exp(-sq((u - m.u) / m.sigma));
        for (const Rough::Mound& m : r.mounds) dh += m.h * std::exp(-(sq(u - m.u) + sq(v - m.v)) / sq(m.sigma));
        if (r.rut > 0) {
            const double vc = lv + 0.5 * std::sin(u * 0.09 + ph) + 0.3 * std::sin(u * 0.23 + ph * 2), depth = r.rut * (0.7 + 0.3 * (2 * valueNoise(u / 6 + ph, 3.3) - 1));
            dh -= depth * (std::exp(-sq((v - vc - 0.9) / 0.55)) + std::exp(-sq((v - vc + 0.9) / 0.55)));
        }
        c.setH(id, c.getH(id) + dh * env * wgt);
    });
}

// ---- 1. STEEP HILL -------------------------------------------------------------------------------
void steepHill(Canvas& c) {
    const uint32_t seed = c.seed;
    const double xs = kHillStart, len = c.rng.range(kHillLen), rise = len * std::tan(c.rng.range(kHillAngle) * M_PI / 180);
    auto y = [&](double u) { return rise * smoothstep(xs, xs + len, u) * 0.15 + rise * clampd((u - xs) / len, 0, 1) * 0.85; };
    // The line winds a little up the climb, between the scree piles.
    auto lineV = [&](double u) { return 1.1 * std::sin(u * 0.16 + seed) * smoothstep(xs - 8, xs + 4, u) * smoothstep(xs + len + 6, xs + len - 6, u); };
    trail(c, kHillTrailHalf, kHillTrailBlend, y, [&](double u) { return u > xs && u < xs + len ? SURF_SCREE : SURF_DIRT; });
    Rough fx;
    fx.amp = 0.2, fx.holes = 2, fx.washouts = 1, fx.cambers = 1;
    double ledgeU[kHillLedges];
    for (int k = 0; k < kHillLedges; k++) ledgeU[k] = xs + len * (0.45 + 0.18 * k), fx.keep.push_back(ledgeU[k]);
    roughen(c, 2.6, 2.0, fx, lineV);
    // The gully's walls run on past the top, opening out onto the summit.
    walls(c, xs - 6, c.fp.length + c.fp.apron, kHillWallHalf, kHillWallDeg, [&](double u) { return kHillWallH * smoothstep(xs + len + 40, xs + len + 6, u); }, y, SURF_ROCK);
    for (double u = 0; u <= c.fp.length; u += 2) c.lineUV.push_back({u, lineV(u)});
    c.commit();
    // Ledges on the upper half: square rock lips the line must climb.
    for (int k = 0; k < kHillLedges; k++) {
        const double u = ledgeU[k], h = c.rng.range(kHillLedgeH);  // a row of big rocks laid across the climb, sunk to make a lip h high
        for (double v = -3.4; v < 3.5; v += c.rng.range(1.2, 1.7)) {
            const double rl = c.rng.range(0.95, 1.35), rd = c.rng.range(0.8, 1.1), g = c.ground(u, v);
            c.laid(u, v, M_PI / 2 + c.rng.range(-0.25, 0.25), V3(rl, h / 2 + 0.3, rd), g + h + c.rng.range(0.0, 0.1), 0, 0, (float)c.rng.range(0.9, 1.08));
        }
    }
    // Scree: piles of loose rock down the climb (small ones everywhere, the bigger ones off the line), thicker toward the ledges,
    // rubble at the foot of each ledge and the big ones beside its ends.
    auto clearOfLine = [&](double u, double v, double r) { return r < 0.34 || std::fabs(v - lineV(u)) > r * 0.85 + 1.25; };
    const int piles = 10;
    for (int p = 0; p < piles; p++) {
        const double pu = xs + len * (0.04 + 0.92 * (p + c.rng.range(0.1, 0.9)) / piles), pv = c.rng.range(-2.3, 2.3), radius = c.rng.range(1.6, 3.0);
        const double up = (pu - xs) / len;  // (rocks get bigger toward the top)
        const int n = 4 + (int)(c.rng() * 5);
        for (int i = 0; i < n; i++) {
            const double a = c.rng.range(0, 6.283), d = radius * std::sqrt(c.rng()), u = pu + d * std::cos(a), v = clampd(pv + d * std::sin(a) * 0.8, -3.4, 3.4);
            const double r = boulderR(c.rng, 0.9 - 0.25 * up, 0.1 + 0.15 * up, 0.55);
            if (clearOfLine(u, v, r)) c.boulder(u, v, r);
        }
    }
    for (int k = 0; k < kHillLedges; k++) {
        const double lu = ledgeU[k];
        for (int i = 0; i < 7; i++) {  // rubble at the foot of the ledge
            const double u = lu - c.rng.range(1.2, 4.2), v = c.rng.range(-3.2, 3.2), r = boulderR(c.rng, 0.75, 0.25, 0.55);
            if (clearOfLine(u, v, r)) c.boulder(u, v, r);
        }
        for (int side : {-1, 1}) {  // big ones where the ledge meets the walls
            const double r = boulderR(c.rng, 0, 0.7, 0.9);
            c.boulder(lu + c.rng.range(-2, 1.5), side * c.rng.range(3.0, 3.9), r);
        }
    }
    c.forest(14, 0.08, false);
}

// ---- 2. MUD PIT ----------------------------------------------------------------------------------
void mudPit(Canvas& c) {
    const uint32_t seed = c.seed;
    trail(c, 2.6, 12, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    const double xa = kMudStart, xb = xa + c.rng.range(kMudLen), half = kMudHalf;
    Rough fx;  // bumps, holes, a gully and a dip on the run-in and run-out, the trucks' ruts all the way
    fx.amp = 0.22, fx.rut = 0.15, fx.holes = 2, fx.washouts = 1, fx.cambers = 1;
    roughen(c, 3.5, 6, fx, [](double) { return 0.0; });
    c.each(xa - 6, xb + 6, -half - 8, half + 8, [&](int id, double u, double v) {
        const double inU = std::min(u - xa, xb - u), inV = half + 2.5 * (valueNoise(u * 0.12, seed) - 0.5) - std::fabs(v);
        if (inU < -4 || inV < -6) return;
        const double depth = kMudDepthMin + kMudDepthVar * fbmNoise(u * 0.08 + seed, v * 0.08, 3);  // varies along and across
        // Entry bank gentle, exit bank steep (1:1): you drop in easy and fight your way out.
        const double bankIn = clampd((u - xa) / kMudBankIn, 0, 1), bankOut = clampd((xb - u) / kMudBankOut, 0, 1), side = clampd(inV / 2.0, 0, 1);
        const double f = std::min({bankIn, bankOut, side});
        const double h = -depth * f + 0.12 * (valueNoise(u * 0.6, v * 0.6) - 0.5);
        c.setH(id, std::min(c.getH(id), h));
        if (f > 0 || (inU > -3 && inV > -4)) c.paint(id, SURF_MUD);
        if (f > 0.3) c.water(id, kMudWater);
    });
    // Ruts carried through by the trucks before you.
    c.each(xa - 25, xa, -2, 2, [&](int id, double, double v) {
        c.setH(id, c.getH(id) - 0.12 * std::exp(-sq((std::fabs(v) - 0.9) / 0.4)));
        c.paint(id, SURF_MUD);
    });
    for (double u = 0; u <= c.fp.length; u += 2) c.lineUV.push_back({u, 0.0});
    c.commit();
    // Rock: strewn over both banks (thickest on the way out, where it's steepest), along the shores, and a few hidden under the water.
    auto clearOfLane = [&](double v, double r) { return r < 0.34 || std::fabs(v) > r * 0.85 + 1.25; };
    for (int i = 0; i < 16; i++) {  // the entry bank
        const double u = xa + c.rng.range(-7, 3), v = c.rng.range(-half, half), r = boulderR(c.rng, 0.6, 0.3, 0.75);
        if (clearOfLane(v, r)) c.boulder(u, v, r);
    }
    for (int i = 0; i < 24; i++) {  // the exit bank
        const double u = xb + c.rng.range(-3, 9), v = c.rng.range(-half - 1, half + 1), r = boulderR(c.rng, 0.5, 0.35, 0.9);
        if (clearOfLane(v, r)) c.boulder(u, v, r);
    }
    for (int i = 0; i < 16; i++) {  // along the shores
        const double u = c.rng.range(xa + 3, xb - 3), v = (c.rng() < 0.5 ? -1 : 1) * (half + c.rng.range(-1.2, 1.2)), r = boulderR(c.rng, 0.5, 0.4, 0.7);
        c.boulder(u, v, r);
    }
    for (int i = 0, got = 0; i < 40 && got < 3; i++) {  // hidden: tops just under the water
        const double u = c.rng.range(xa + 8, xb - 8), v = c.rng.range(-3.2, 3.2), r = c.rng.range(0.45, 0.8), g = c.ground(u, v);
        if (g > kMudWater - 0.3 || std::fabs(v) < 0.9) continue;  // (too shallow to hide one, or in the middle of the line)
        got += c.boulder(u, v, r, 0, kMudWater - 0.1 - g);
    }
    // Dense timber both sides (no driving round), its trunks 3.5 m back from the mud and its crowns kept low near it.
    c.forest(half + kMudClear, 0.15, true, xa - 6, xb + 6, 1.0, 6.0, kNearH);
    c.forest(half + kMudClear, 0.05, false, -c.fp.apron, c.fp.length + c.fp.apron, kForestPerM2, 6.0, kNearH);
}

// ---- 3. ROCKY RIVER ------------------------------------------------------------------------------
void rockyRiver(Canvas& c) {
    const uint32_t seed = c.seed;
    trail(c, 2.6, 12, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    Rough fx;  // the banks either side of the channel are lumpy too
    fx.amp = 0.2, fx.holes = 1, fx.washouts = 0, fx.cambers = 1;
    roughen(c, 3.0, 8, fx, [](double) { return 0.0; });
    for (double u = 0; u <= c.fp.length; u += 2) c.lineUV.push_back({u, 0.0});
    const double xr = kRiverAt, width = c.rng.range(kRiverWidth), depth = c.rng.range(kRiverDepth), skew = c.rng.range(kRiverSkew);
    const double level = -depth + c.rng.range(kRiverWater);  // water depth over the bed
    const double hw = c.fp.half;
    auto bend = [&](double v) { return xr + skew * v + 6 * std::sin(v * 0.03 + seed); };
    c.each(-c.fp.apron, c.fp.length + c.fp.apron, -hw, hw, [&](int id, double u, double v) {
        const double d = std::fabs(u - bend(v)) - width / 2;
        if (d > 14) return;
        const double bed = -depth + 0.25 * (valueNoise(u * 0.2, v * 0.2) - 0.5);
        const double bank = d < 0 ? bed : bed + (c.getH(id) - bed) * smoothstep(0, 14, d);
        const double h = d < 0 ? bed : std::min(c.getH(id), bank + d * 0.35);
        // The channel runs out at the sides of the cleared ground (a marsh, not a trench).
        const double run = smoothstep(hw - 2, hw - 18, std::fabs(v)), end = c.getH(id) + (h - c.getH(id)) * run;
        c.setH(id, end);
        if (d < 0 && run > 0.3) {
            c.paint(id, SURF_GRAVEL);
            if (end < level) c.water(id, level, 0, kRiverCurrent);
        } else if (d < 5 && run > 0.3) c.paint(id, SURF_SAND);
    });
    c.commit();
    // Boulders: lots, mixed sizes, thickest mid-channel. Some too big to climb: pick a line.
    int placed = 0;
    for (int k = 0; k < 4000 && placed < kRiverBoulders; k++) {
        const double v = c.rng.range(-(hw - 10), hw - 10), cu = bend(v);
        const double u = cu + c.rng.range(-width / 2, width / 2);
        const double mid = 1 - std::fabs(u - cu) / (width / 2);
        if (c.rng() > 0.35 + 0.65 * mid) continue;
        const double q = c.rng(), r = q < 0.6 ? c.rng.range(0.25, 0.5) : q < 0.9 ? c.rng.range(0.5, 0.85) : c.rng.range(0.9, 1.4);
        c.rock(u, v, V3(r, r * c.rng.range(0.55, 0.8), r * c.rng.range(0.7, 1.0)), r * c.rng.range(0.5, 0.8), seed * 131 + k, (float)c.rng.range(0.85, 1.1));
        placed++;
    }
    c.forest(8, 0.07, false);
}

// ---- 4. ROCK WALL (v4): a slickrock formation in a slot canyon -------------------------------------
// Sandstone ledges whose lips wander across the canyon; each ledge a row of big scanned rocks laid along the lip, sunk
// into the tread (their tops the step, their flanks the face), with a notch built as a staircase (the ground steps, a pair
// of rocks on each tread), no step over ~0.35 m, on alternating sides: find it or stay put. A V-notch squeeze with
// chockstones, a pour-over pool under the fifth ledge, banded strata (flat rocks jutting from the canyon walls) and a
// final ledge too tall for anyone (winch to the junipers above). Gaps between ledges are long enough for a truck to turn
// from one notch to the next. No boxes.
struct SlickLedge {
    double x, rise, amp, phase, notchZ, tilt;
};
struct Slick {
    std::vector<SlickLedge> L;
    double xs = kWallStart, xe = 0, vx0 = 0, vx1 = 0, wallH = kWallHeight, len = 1e9;  // (len: the section's length, so the exit pad stays flat)
    uint32_t seed = 1;
    // A notch's staircase carried by the ground (a tenth under the rock treads laid on it): the steps climb from `lower` to `upper`
    // over ns - 1 treads a metre deep, 2.6 m wide across the notch.
    struct Stair {
        double lx, nz, lower, upper;
        int ns;
        double front(int j) const { return lx + 0.3 - ((ns - j) + 0.4); }  // where step j's tread begins
    };
    std::vector<Stair> stairs;
    double lip(int k, double v) const { return L[k].x + L[k].amp * std::sin(v * 0.45 + L[k].phase) + 0.9 * (valueNoise(v * 0.3, k * 7.3 + seed) - 0.5); }
    double halfW(double x) const {
        if (x > vx0 && x < vx1) return kWallSqueezeHalf + 0.4 * std::sin(x);  // the squeeze
        return 4.6 + 1.6 * std::sin(x * 0.09 + seed) + 0.8 * valueNoise(x * 0.2, seed);
    }
    double floorY(double x, double v) const {
        double h = 0;
        for (size_t k = 0; k < L.size(); k++) {
            const double s = smoothstep(lip((int)k, v) - 0.25, lip((int)k, v) + 0.55, x);
            h += L[k].rise * s + L[k].tilt * v * s;
        }
        h += 0.35 * (valueNoise(x * 0.17, v * 0.17) - 0.5) * smoothstep(xs - 10, xs, x) * smoothstep(len - kPadFlat, len - kPadFlat - 16, x);  // slickrock humps
        if (x > vx0 - 2 && x < vx1 + 2) h += 0.6 * std::fabs(v) * smoothstep(vx0 - 2, vx0 + 1, x) * smoothstep(vx1 + 2, vx1 - 1, x);
        for (const Stair& st : stairs) {
            const double lat = smoothstep(1.75, 1.25, std::fabs(v - st.nz));
            if (lat <= 0 || x < st.lx - 4 || x > st.lx + 1) continue;
            double prof = 0;
            for (int j = 1; j < st.ns; j++) prof += smoothstep(st.front(j) - 0.4, st.front(j) + 0.4, x);
            h = std::max(h, st.lower + (st.upper - st.lower) * prof / st.ns * lat - 0.1 * smoothstep(0, 0.6, prof) * lat);
        }
        return h;
    }
};

void rockWall(Canvas& c) {
    const uint32_t seed = c.seed;
    Slick s;
    s.seed = seed;
    s.len = c.fp.length;
    double x = s.xs;
    const int n = kWallLedges;
    for (int k = 0; k < n; k++) {
        SlickLedge l;
        l.x = x;
        l.rise = k == n - 1 ? c.rng.range(kWallLastRise) : c.rng.range(kWallRise);
        l.amp = c.rng.range(kWallAmp);
        l.phase = c.rng.range(0, 6.28);
        l.notchZ = (k % 2 ? 1 : -1) * c.rng.range(0.5, 1.2);
        l.tilt = c.rng.range(kWallTilt);
        s.L.push_back(l);
        x += c.rng.range(kWallGap);
        if (k == kWallSqueezeAfter) s.vx0 = x - 1, s.vx1 = x + kWallSqueezeLen - 1, x += kWallSqueezeLen + 4;  // the squeeze after the third ledge
    }
    s.xe = x;
    for (int k = 0; k + 1 < n; k++) {  // (each notch's staircase: found from the ground without it)
        const double lx = s.lip(k, s.L[k].notchZ), nz = s.L[k].notchZ, lower = s.floorY(lx - 3.0, nz), upper = s.floorY(lx + 2.0, nz) + 0.1;
        s.stairs.push_back({lx, nz, lower, upper, (int)std::ceil((upper - lower) / kWallStep)});
    }
    // Terrain: dirt wash in, the formation, the plateau out; canyon walls either side.
    c.each(-c.fp.apron, c.fp.length + c.fp.apron, -c.fp.half, c.fp.half, [&](int id, double pu, double pv) {
        const double hw = s.halfW(pu), e = std::fabs(pv) - hw;
        const double inCanyon = smoothstep(s.xs - kWallFadeIn, s.xs - 10, pu) * smoothstep(s.xe + 18, s.xe + 6, pu);
        double h = s.floorY(pu, pv) + c.getH(id) * (1 - inCanyon);  // (the land's own relief outside the canyon)
        if (pu < s.xs - 6 || pu > s.xe + 12)  // the wash in and the plateau out are lumpy (and flat at the ends)
            h += 0.3 * roughNoise(pu, pv, seed) * padEnv(pu, c.fp.length) * smoothstep(hw + 5, hw, std::fabs(pv));
        if (e > 0) {
            const double rise = std::min(e * 3.8, s.wallH) + kWallRimSlope * std::max(0.0, e - s.wallH / 3.8);
            h += rise * inCanyon + 1.5 * (valueNoise(pu * 0.25, pv * 0.25) - 0.5) * smoothstep(0, 1.5, e) * inCanyon;
        }
        c.setH(id, h);
        const bool lane = pu < s.xs - 3 && e < 0;
        c.surface(id, lane ? SURF_DIRT : SURF_ROCK, lane ? PAINT_DESERT : PAINT_REDROCK);
    });
    // The pour-over pool under the fifth ledge.
    {
        const int k = n - 2;
        c.each(s.L[k].x - 6, s.L[k].x + 1, -6, 6, [&](int id, double pu, double pv) {
            const double u = s.lip(k, pv) - pu;  // m in front of the lip
            if (u < 0.2 || u > 4.5 || std::fabs(pv) > s.halfW(pu) - 0.5) return;
            const double d = 0.9 * smoothstep(4.5, 2.5, u) * smoothstep(0.2, 1.2, u);
            c.setH(id, c.getH(id) - d);
            if (d > 0.3) c.water(id, s.floorY(s.L[k].x - 5, 0) - 0.12);
        });
    }
    c.commit();
    // Each ledge is a row of real rock: big hull rocks laid along the lip, overlapping, sunk into the tread so their tops
    // make the step and their flanks the face. At the notch they stop, and a staircase of rocks climbs it (no step over
    // ~0.35 m: a hull's top is lumpy). No boxes: the canyon is made of scanned rock.
    for (int k = 0; k < n; k++) {
        const SlickLedge& l = s.L[k];
        const double hw = s.halfW(l.x), gapHalf = 1.5;
        auto row = [&](double v0, double v1, int dirn) {  // from v0 outward to v1 (dirn +1 or -1)
            for (double v = v0; dirn * (v1 - v) > 0;) {
                const double rl = c.rng.range(1.05, 1.5), rd = c.rng.range(0.95, 1.35), vc = v + dirn * rl * 0.85;
                const double lx = s.lip(k, vc), dl = (s.lip(k, vc + 0.3) - s.lip(k, vc - 0.3)) / 0.6;  // (where the lip runs)
                const double tread = s.floorY(lx + 2.0, vc) + c.rng.range(0.0, 0.14), foot = s.floorY(lx - 1.5, vc);
                c.laid(lx + rd - 1.0, vc, std::atan2(1.0, dl), V3(rl, (tread - foot + 0.5) / 2, rd), tread, 0, 0, (float)c.rng.range(0.9, 1.08));
                v += dirn * rl * c.rng.range(1.2, 1.5);
            }
        };
        if (k < n - 1) {
            row(l.notchZ + gapHalf + 0.05, hw + 1.6, 1);
            row(l.notchZ - gapHalf - 0.05, -hw - 1.6, -1);
        } else row(-hw - 1.6, hw + 1.6, 1);
        if (k < n - 1) {  // the notch: a staircase of rocks, each step a pair side by side on the ground's own steps
            const Slick::Stair& st = s.stairs[k];
            for (int j = 1; j < st.ns; j++) {
                const double top = st.lower + (st.upper - st.lower) * j / st.ns, rd = 0.62;
                for (int side : {-1, 1}) {
                    const double vc = st.nz + side * 0.66;
                    c.laid(st.front(j) + rd, vc, M_PI / 2 + c.rng.range(-0.12, 0.12), V3(0.85, 0.35, rd), top + 0.03, 0, 0, (float)c.rng.range(0.92, 1.06));
                }
            }
        }
    }
    // Banded strata in the canyon walls: flat rocks jutting from the face at each layer.
    for (double pu = s.xs - 12; pu < s.xe + 6; pu += c.rng.range(1.8, 3.2))
        for (int side : {-1, 1})
            for (double yr = c.rng.range(0.9, 1.6); yr < s.wallH - 0.5; yr += c.rng.range(1.0, 1.8)) {
                if (c.rng() < 0.3) continue;
                const double hw = s.halfW(pu), v = side * (hw + yr / 3.8), ry = c.rng.range(0.25, 0.42);
                c.laid(pu, v, c.rng.range(-0.15, 0.15), V3(c.rng.range(1.1, 2.0), ry, c.rng.range(0.6, 0.95)), c.ground(pu, v) + ry, 0, 0, (float)c.rng.range(0.9, 1.08));
            }
    // Slabs on the treads, boulders heaped along the canyon walls.
    for (int k = 0; k + 1 < n; k++)
        if (c.rng() < 0.7) {
            const double pu = (s.L[k].x + s.L[k + 1].x) / 2 + c.rng.range(-1, 1), side = c.rng() < 0.5 ? -1 : 1;
            const double pv = side * (s.halfW(pu) - c.rng.range(1.5, 2.5));
            c.rock(pu, pv, V3(c.rng.range(1.6, 2.6), c.rng.range(0.35, 0.6), c.rng.range(1.2, 2.0)), 0.45, seed * 1000 + (uint32_t)(c.rng() * 1e6), 0.95f);
        }
    for (double pu = s.xs - 15; pu < s.xe + 8; pu += c.rng.range(1.5, 3.5))
        for (int side : {-1, 1}) {
            const double r = c.rng.range(1.2, 3.2), pv = side * (s.halfW(pu) + r * 0.35 + c.rng.range(0, 1.5) * (r > 2.2));
            c.rock(pu, pv, V3(r, r * c.rng.range(0.6, 0.9), r * c.rng.range(0.7, 1.0)), r * c.rng.range(0.6, 0.95), seed * 1000 + (uint32_t)(c.rng() * 1e6),
                   (float)c.rng.range(0.8, 1.05));
            if (c.rng() < 0.35) {  // one up in the wall face
                const double r2 = c.rng.range(1.5, 3.0), pv2 = side * (s.halfW(pu) + c.rng.range(2, 5));
                c.rock(pu + c.rng.range(-1, 1), pv2, V3(r2, r2 * 0.7, r2), r2 * 0.6, seed * 1000 + (uint32_t)(c.rng() * 1e6), (float)c.rng.range(0.8, 1.0));
            }
        }
    // ---- Real rock in the path: boulders on the treads, rubble at the foot of each riser, rock in and beside the notches,
    // chockstones in the squeeze, a garden between ledges, and a row of flat rocks along each lip (so the lips read as scanned
    // rock, not slab). The line (through the notches, one to the next) keeps a lane the truck's width clear of anything over
    // ~0.35 m; smaller rock may lie in it, and the staircases themselves are left alone.
    const int nn = n - 1;  // ledges with a notch
    std::vector<double> nx(n, 0.0), nz(n, 0.0);
    for (int k = 0; k < nn; k++) nx[k] = s.lip(k, s.L[k].notchZ), nz[k] = s.L[k].notchZ;
    std::vector<std::pair<double, double>> A = {{0.0, 0.0}, {nx[0] - 14, 0.0}};
    for (int k = 0; k < nn; k++) {
        if (k > 0 && nx[k - 1] < s.vx0 && s.vx0 < nx[k]) A.push_back({s.vx0 + 1.5, 0.0}), A.push_back({std::max(s.vx0 + 2.5, nx[k] - 6), 0.0});
        A.push_back({nx[k] - 3.2, nz[k]}), A.push_back({nx[k] + 2.2, nz[k]});
    }
    A.push_back({s.L[n - 1].x - 3.0, 0.0}), A.push_back({c.fp.length, 0.0});
    for (size_t i = 1; i < A.size(); i++) A[i].first = std::max(A[i].first, A[i - 1].first + 0.5);
    auto lineV = [&](double u) {
        for (size_t i = 1; i < A.size(); i++)
            if (u <= A[i].first) return A[i - 1].second + (A[i].second - A[i - 1].second) * smoothstep(A[i - 1].first, A[i].first, u);
        return 0.0;
    };
    auto onStairs = [&](double u, double v, double r) {
        for (int k = 0; k < nn; k++)
            if (u > nx[k] - 3.0 - r && u < nx[k] + 0.9 + r && std::fabs(v - nz[k]) < 1.45 + r * 0.7) return true;
        return false;
    };
    auto laneOK = [&](double u, double v, double r) { return r < 0.34 || std::fabs(v - lineV(u)) > r * 0.85 + 1.25; };
    auto place = [&](double u, double v, double r, double lift) {
        if (std::fabs(v) > s.halfW(u) - 0.3 - r * 0.5 || onStairs(u, v, r) || !laneOK(u, v, r)) return false;
        return c.boulder(u, v, r, lift);
    };
    for (int k = 0; k < n; k++) {
        const double xa = k == 0 ? s.xs - 12 : s.L[k - 1].x + 0.9, xb = s.L[k].x - 0.6;
        const int want = k == 1 ? 18 : k == 3 ? 22 : 9;  // (the garden after the second ledge and the one in front of the squeeze's notch)
        for (int i = 0, got = 0; i < 400 && got < want; i++) {  // boulders on the treads (over the slabs' tops: a little proud of them)
            const double u = c.rng.range(xa, xb), v = c.rng.range(-5.5, 5.5), r = boulderR(c.rng, 0.62, 0.3, 0.75);
            got += place(u, v, r, 0.12);
        }
        for (int i = 0, got = 0; i < 200 && got < (k == n - 1 ? 14 : 8); i++) {  // rubble at the foot of the riser
            const double v = c.rng.range(-5, 5), u = s.lip(k, v) - c.rng.range(0.7, 3.0), r = boulderR(c.rng, 0.85, 0.15, 0.6);
            got += place(u, v, r, 0.1);
        }
    }
    for (int k = 0; k < nn; k++)  // beside and below the notch
        for (int i = 0; i < 4; i++) {
            const double r = c.rng.range(0.28, 0.65), side = i % 2 ? 1 : -1, v = nz[k] + side * (1.55 + r * 0.8 + c.rng.range(0, 0.7)), u = nx[k] - c.rng.range(2.0, 0.0);
            if (std::fabs(v) < s.halfW(u) - 0.4) c.boulder(u, v, r, 0.05);
        }
    for (int k = 0; k < nn; k++)  // a couple of small ones in the mouth of the notch, in front of its first step
        for (int i = 0; i < 2; i++) {
            const double r = c.rng.range(0.16, 0.26), u = nx[k] - c.rng.range(3.1, 4.4), v = nz[k] + c.rng.range(-1.0, 1.0);
            c.boulder(u, v, r, 0.05);
        }
    for (int i = 0; i < 5; i++) {  // chockstones wedged in the V of the squeeze, alternating sides
        const double u = s.vx0 + 1.5 + i * (s.vx1 - s.vx0 - 3) / 4, r = c.rng.range(0.35, 0.6), v = (i % 2 ? 1 : -1) * c.rng.range(0.45, 1.4);
        c.boulder(u, v, r, 0, r * c.rng.range(0.9, 1.3));
    }
    for (double u = 0; u <= c.fp.length; u += 2) c.lineUV.push_back({u, lineV(u)});
    // Junipers on the plateau (anchors for the last ledge) and scattered over the rims.
    for (int k = 0; k < 12; k++) c.tree(s.xe + c.rng.range(3, 16), c.rng.range(-7, 7), c.rng.range(4, 7), 1, 0.8f);
    for (int k = 0; k < 80; k++) {
        const double pu = c.rng.range(-c.fp.apron, c.fp.length + c.fp.apron), pv = (c.rng() < 0.5 ? -1 : 1) * c.rng.range(12, c.fp.half - 1);
        c.tree(pu, pv, c.rng.range(3.5, 7), c.rng() < 0.8 ? 1 : 2, 0.8f);
    }
}

// ---- 5. DUNES ------------------------------------------------------------------------------------
// Heights over the whole field come from duneHeight; the cells are then rounded off a little and settled
// to the angle of repose, so the slip faces are as steep as sand stands and nothing is steeper.
void dunes(Canvas& c) {
    const uint32_t seed = c.seed;
    const double L = c.fp.length, hw = c.fp.half;
    DuneParams P;
    P.windDeg = c.rng.range(kDuneWind), P.spacing = c.rng.range(kDuneSpacing), P.crestMax = c.rng.range(kDuneCrest);
    P.subSpacing = P.spacing * 0.5, P.halfU = L / 2, P.halfV = hw - 12;
    auto edgeD = [&](double u, double v) { return std::cbrt(std::pow(std::fabs((u - L / 2) / P.halfU), 3) + std::pow(std::fabs(v / P.halfV), 3)); };
    // The cells of the cleared ground, and where each one's neighbours are.
    std::vector<int> mine;
    std::vector<float> above;  // each one's dune height over its hollow floor
    std::vector<int> at(c.w.gridSize() * c.w.gridSize(), -1);
    c.each(-c.fp.apron, L + c.fp.apron, -hw, hw, [&](int i, double u, double v) {
        at[c.cells[i].id] = (int)mine.size(), mine.push_back(i);
        const double wgt = smoothstep(1.0, 0.62, edgeD(u, v));  // (the dunes fade into the rolling country round the ends and sides)
        double ab = 0;
        const double h = duneHeight(u - L / 2, v, P, seed, &ab);
        above.push_back((float)ab);
        c.setH(i, c.getH(i) * (1 - wgt) + h * wgt);
    });
    const int n = c.w.gridSize();
    const int off[8][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};  // (cell steps, +1 in the id = j)
    // The field cell next to grid cell `id` (-1: none; the map's edge is no neighbour, and a row's end doesn't wrap to the next).
    auto next = [&](int id, const int (&d)[2]) {
        const int i = id / n + d[0], j = id % n + d[1];
        return i < 0 || j < 0 || i >= n || j >= n ? -1 : at[i * n + j];
    };
    std::vector<float> tmp(mine.size());
    // Rounded crests and toes (a 3x3 kernel), then the repose: sand slides off anything steeper than kDuneRepose.
    for (int pass = 0; pass < kDuneSmooth; pass++) {
        for (size_t m = 0; m < mine.size(); m++) {
            const int id = c.cells[mine[m]].id;
            double sum = c.cells[mine[m]].h * 0.4, wsum = 0.4;
            for (int k = 0; k < 8; k++) {
                const int j = next(id, off[k]);
                if (j < 0) continue;
                const double wk = k < 4 ? 0.1 : 0.05;
                sum += c.cells[mine[j]].h * wk, wsum += wk;
            }
            tmp[m] = (float)(sum / wsum);
        }
        for (size_t m = 0; m < mine.size(); m++) c.cells[mine[m]].h = tmp[m];
    }
    // (Each pair of neighbours in place, sweeping the field forward then back, so a slope settles in a few passes.)
    const double tanR = std::tan(kDuneRepose * M_PI / 180);
    const int fwd[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int pass = 0; pass < kDuneRelax; pass++)
        for (size_t mm = 0; mm < mine.size(); mm++) {
            const size_t m = pass & 1 ? mine.size() - 1 - mm : mm;
            Canvas::Cell& a = c.cells[mine[m]];
            for (int k = 0; k < 4; k++) {
                const int j = next(a.id, fwd[k]);
                if (j < 0) continue;
                Canvas::Cell& b = c.cells[mine[j]];
                const double lim = tanR * (k == 0 || k == 1 ? 1.0 : 1.4142), diff = a.h - b.h;
                if (std::fabs(diff) > lim) {
                    const float move = (float)((std::fabs(diff) - lim) * 0.5 * (diff > 0 ? 1 : -1));
                    a.h -= move, b.h += move;
                }
            }
        }
    // Sand over the field; its rim and both ends are firm desert floor (never start on sand: cars bog), and
    // so are patches of the hollows.
    const double ox = (seed % 997) * 13.1 + 31, oz = (seed % 991) * 7.7 + 17;
    for (size_t m = 0; m < mine.size(); m++) {
        Canvas::Cell& k = c.cells[mine[m]];
        const double d = edgeD(k.u, k.v);
        if (d > 0.93) c.surface(mine[m], SURF_DIRT, PAINT_DESERT);
        else if (above[m] < kDuneHardpan && fbmNoise(k.u * 0.022 + ox, k.v * 0.022 + oz, 2) > kDunePatch) c.surface(mine[m], SURF_DIRT, PAINT_DESERT);
        else c.paint(mine[m], SURF_SAND);
    }
    for (double u = 0; u <= L; u += 2) c.lineUV.push_back({u, 0.0});
    c.commit();
    // A few half-buried rocks down in the hollows (where the dunes aren't).
    for (int i = 0, got = 0; i < 600 && got < 12; i++) {
        const double u = c.rng.range(30, L - 30), v = c.rng.range(-(hw - 24), hw - 24), r = c.rng.range(0.35, 0.9);
        double ab = 0;
        duneHeight(u - L / 2, v, P, seed, &ab);
        if (ab < 0.6) got += c.boulder(u, v, r, 0, r * 0.45);
    }
}

// ---- 6. CLIFF SHELF ------------------------------------------------------------------------------
void cliffShelf(Canvas& c) {
    const uint32_t seed = c.seed;
    const double half = c.rng.range(kShelfHalf);
    const double tiltDeg = c.rng.range(kShelfTilt), tilt = std::tan(tiltDeg * M_PI / 180);  // leans you toward the drop (+v)
    const double L = c.fp.length;
    auto curve = [&](double u) { return 5 * std::sin(u * 0.05 + seed) * smoothstep(0, kShelfEase, u); };  // (eases onto the line from the entry)
    // Where the set pieces stand along the shelf (u): the road under them is kept nearly level.
    struct Zone {
        double a, b;
    };
    const Zone chokeA = {24, 36}, table = {40, 53}, camber = {74, 93}, chokeB = {98, 110}, stairs = {112, 133}, straddle = {138, 152};
    const Zone zones[] = {chokeA, table, camber, chokeB, stairs, straddle};
    auto bump = [](double u, double at, double w) { return std::exp(-sq((u - at) / w)); };
    auto lineLat = [&](double u) { return 0.10 + 0.15 * std::sin(u * 0.09 + seed) + 0.25 * bump(u, 30, 5) - 0.25 * bump(u, 104, 5); };
    // The staircase: three steps up (0.25-0.30 each, treads 2.8 m: one axle takes a step at a time), a platform of 2.4 m, three down.
    // The ground carries it (a tenth under the rock treads laid on it), `stairH` the height of its treads above the road.
    double stairRise[3];
    for (double& r : stairRise) r = c.rng.range(0.25, 0.30);
    const double tread = 2.8, plat = 2.4, stairW = 1.55;
    struct Tread {
        double a, b, y;  // from u a to b at height y (above the road)
    };
    std::vector<Tread> treads;
    {
        double u = stairs.a, y = 0;
        for (int k = 0; k < 3; k++) y += stairRise[k], treads.push_back({u - 0.15, u + tread, y}), u += tread;
        treads.push_back({u - 0.15, u + plat, y}), u += plat;
        for (int k = 2; k >= 0; k--) y -= stairRise[k], treads.push_back({u - 0.15, u + tread, y}), u += tread;
    }
    auto stairH = [&](double u) {
        double h = 0, prev = 0;
        for (const Tread& t : treads) h += (t.y - prev) * smoothstep(t.a - 0.35, t.a + 0.45, u), prev = t.y;
        return h;
    };
    // The table: up a ramp (10 deg), across a platform, down another; carried by the ground (a tenth under the rock laid on it).
    const double tableRise = 0.55, tableW = 1.55, tableRun = tableRise / std::tan(10 * M_PI / 180), tablePlat = 3.6;
    auto tableH = [&](double u) { return tableRise * std::min(clampd((u - table.a) / tableRun, 0, 1), clampd((table.a + 2 * tableRun + tablePlat - u) / tableRun, 0, 1)); };
    auto zoneMask = [&](double u) {
        double m = 0;
        for (const Zone& z : zones) m = std::max(m, smoothstep(z.a - 3, z.a, u) * smoothstep(z.b + 3, z.b, u));
        return m;
    };
    // Big boulders sunk into the ground, 1.2-2.5 m across with only their crowns showing: in the line (a crown to drive over), against the
    // talus, and on the drop side by the lip. The ground heaps up round each (a mound, so a low gentle rise to a hump of rock rather than
    // a step); the rock itself is laid after the ground is done. Drawn from their own generator, so the rest of the shelf is as it was.
    struct Bury {
        double u, lat, hx, hz, ry, yaw, mound, cap, mx, mz;  // (mx, mz: how far the mound reaches along and across)
        int where;                                          // 0 the line, 1 against the talus, 2 at the lip
    };
    std::vector<Bury> buried;
    Rng nr((seed + 1) * 4099u + 77);
    auto corridorHit = [&](double u, double lat, double rx, double rz) {  // does the ellipse (rx along, rz across) at (u, lat) reach the line's 2.6 m corridor?
        for (double uu = u - rx; uu <= u + rx + 1e-9; uu += 0.5) {
            const double w = rz * std::sqrt(std::max(0.0, 1 - sq((uu - u) / rx))), l = lineLat(uu);
            if (lat + w > l - kShelfLine && lat - w < l + kShelfLine) return true;
        }
        return false;
    };
    {
        struct Spot {
            double u;
            int where;
        };
        // (in the clear stretches between the set pieces)
        for (const Spot& sp : {Spot{16, 0}, Spot{22, 1}, Spot{13, 2}, Spot{56, 1}, Spot{63, 0}, Spot{70, 2}, Spot{156, 2}, Spot{158, 0}, Spot{164, 1}, Spot{169, 0}}) {
            Bury b;
            b.where = sp.where, b.u = sp.u + nr.range(-1.0, 1.0);
            b.hx = nr.range(0.8, 1.25), b.hz = nr.range(0.6, 0.95), b.yaw = nr.range(-0.5, 0.5);
            if (sp.where == 1) b.hz = nr.range(0.55, 0.8);
            if (sp.where == 2) b.hx = nr.range(0.7, 1.05), b.hz = nr.range(0.55, 0.75), b.yaw *= 0.25;  // (the lip's: turned along the shelf, so it doesn't reach out over the drop)
            b.lat = sp.where == 0 ? lineLat(b.u) + nr.range(0.15, 0.55) : sp.where == 1 ? -half + 0.05 + b.hz * 0.65 : half - 0.4;
            b.ry = std::min(b.hx, b.hz) * nr.range(0.5, 0.7);
            // how high the crown stands over the road: low where it meets the corridor (and a little toward the drop side of the line, so a wheel that climbs it tips the truck toward the wall), more beside it
            const bool inLine = corridorHit(b.u, b.lat, b.hx * 0.95, b.hz * 0.95);
            const double crown = inLine ? nr.range(kShelfBuriedLine - 0.08, kShelfBuriedLine) : nr.range(0.5, 0.8);
            b.mound = crown * nr.range(0.2, 0.3), b.cap = crown - b.mound;
            b.mx = b.hx * 1.5, b.mz = b.hz * (sp.where == 2 ? 2.1 : 1.5);  // (more shoulder past the lip: the ground there falls away)
            buried.push_back(b);
        }
    }
    // The mound's height at (u, lat): flat on top, falling away smoothly; and how much of it there is there (0 to 1).
    auto moundW = [&](double u, double lat, double* height) {
        double m = 0, wt = 0;
        for (const Bury& b : buried) {
            const double d = std::hypot((u - b.u) / b.mx, (lat - b.lat) / b.mz);
            if (d < 1) m = std::max(m, b.mound * (1 - smoothstep(0.3, 1.0, d))), wt = std::max(wt, 1 - smoothstep(0.3, 1.0, d));
        }
        if (height) *height = m;
        return wt;
    };
    c.each(-c.fp.apron, L + c.fp.apron, -c.fp.half, c.fp.half, [&](int id, double u, double v) {
        const double fade = smoothstep(-c.fp.apron, 10, u) * smoothstep(L + c.fp.apron, L - 10, u);
        const double lat = v - curve(u);  // + toward the drop
        double h;
        if (lat < -half - kShelfBack) {  // cliff above (~69 deg), levelling off on top (rough above, clean at the foot)
            const double d = -lat - half - kShelfBack;
            const double raw = 0.3 + d * 2.6 + 3 * (valueNoise(u * 0.1, v * 0.1) - 0.5) * smoothstep(0, 3, d);
            h = raw < kShelfCliffMax ? raw : kShelfCliffMax + 0.1 * (raw - kShelfCliffMax);
        } else if (lat <= half) {  // (the road runs under the toe to the cliff's foot)
            h = -tilt * (lat + half);                                                                              // the road
            // bumps and dips, 0.1-0.35 m over 2-8 m, from seeded noise; flat at both ends and under the set pieces
            const double ox = (seed % 997) * 7.1, oz = (seed % 991) * 3.3;
            const double rough = 0.20 * (2 * valueNoise(u * 0.125 + ox, lat * 0.08 + oz) - 1) + 0.16 * (2 * valueNoise(u * 0.25 + ox + 40, lat * 0.12 + oz) - 1) +
                                 0.08 * (2 * valueNoise(u * 0.5 + ox + 80, lat * 0.2 + oz) - 1);
            h += rough * smoothstep(8, 26, u) * smoothstep(L - 8, L - 26, u) * (1 - zoneMask(u)) * (1 - 0.85 * moundW(u, lat, nullptr));  // (smoothed under a buried boulder, so its crown shows all round)
            if (u > table.a - 1 && u < table.a + 2 * tableRun + tablePlat + 1)
                h += (tableH(u) - 0.1 * smoothstep(0.05, 0.2, tableH(u))) * smoothstep(tableW + 0.5, tableW - 0.1, std::fabs(lat - lineLat(u)));
            if (u > stairs.a - 1 && u < stairs.b + 1) h += (stairH(u) - 0.1 * smoothstep(0.05, 0.2, stairH(u))) * smoothstep(stairW + 0.5, stairW - 0.1, std::fabs(lat - lineLat(u)));
        } else h = -tilt * 2 * half - (lat - half) * 2.8 + 2 * (valueNoise(u * 0.1, v * 0.1) - 0.5);               // the drop
        if (lat >= -half - kShelfBack) {  // (the mounds round the buried boulders; past the lip they make a shoulder for a boulder there to sit on)
            double m;
            moundW(u, lat, &m);
            if (m > 0) h = lat <= half ? h + m : std::max(h, -tilt * 2 * half + m);
        }
        h = std::max(h, -kShelfDrop + 4 * valueNoise(u * 0.03, v * 0.03));                                         // valley floor
        const double base = c.getH(id);
        c.setH(id, base + (h - base) * fade);
        if (fade > 0.5) c.paint(id, std::fabs(lat) <= half ? SURF_GRAVEL : SURF_ROCK);
    });
    c.commit();

    // ---- The line: where a truck goes, 2.6 m clear to either side of it (see kShelfLine). It wanders a little
    // across the road, and the two boulder chokes push it to one side then the other. ----
    auto ground = [&](double u, double v) {  // the finished ground's height above the section's plane
        const V3 p = c.at(u, v);
        return c.w.height(p.x, p.z) - p.y;
    };
    auto yawAt = [&](double u) { return -std::atan((curve(u + 0.5) - curve(u - 0.5)) / 1.0); };  // along the shelf here
    // A rock laid by its top face: the point of it at (u, lat) and that point's height, its half sizes along (hx) and across (hz),
    // and about half its thickness (th, a little thicker where it must reach the ground); turned yaw about up, tipped pitch (up along
    // the shelf) and roll (the top falls toward the drop). The scanned hull that fits, sunk in so its top is where it's asked: rock
    // where this used to be slab boxes, so the colour is the scans' own.
    auto slabTop = [&](double u, double lat, double topY, double hx, double hz, double th, double yaw, double pitch, double roll, double) {
        c.laid(u, curve(u) + lat, -yaw, V3(hx, th * 0.8, hz), topY + 0.05, pitch, roll, (float)c.rng.range(0.92, 1.08));
    };
    const double bedRoll = std::atan(tilt);  // the road's own cross-slope (a slab lying on it has this roll)
    auto warm = [&]() { return sq(c.rng()); };  // (mostly grey, some rusty, all shades between)

    // ---- The toe: a talus of scanned rocks packed against the cliff's foot all along the shelf, 1.2-1.9 m high, overlapping, their
    // widest part at the road's side of the foot: a tire meets a rock (an exact collider), never the grid's ramp into the wall. ----
    for (double u = -8; u < L + 8; u += c.rng.range(1.2, 1.7)) {
        const double uc = u + c.rng.range(-0.15, 0.15), rl = c.rng.range(0.85, 1.25), rd = c.rng.range(0.55, 0.8);
        const double latB = -half + kShelfToe + c.rng.range(-0.04, 0.06), lat = latB - rd * 0.9;
        const double ry = c.rng.range(0.7, 1.0), hgt = c.rng.range(1.2, 1.9);
        c.laid(uc, curve(uc) + lat, -(yawAt(uc) + c.rng.range(-0.25, 0.25)), V3(rl, ry, rd), ground(uc, curve(uc) + lat) + hgt, 0, 0, (float)c.rng.range(0.9, 1.08));
    }

    // ---- Rock: fallen boulders, talus, perched stones. Never in the line's corridor (but pebbles). ----
    uint32_t rockN = 0;
    auto corridorFree = [&](double u, double lat, double rh) {
        for (double uu = u - rh; uu <= u + rh + 1e-9; uu += 0.5) {
            const double w = std::sqrt(std::max(0.0, rh * rh - sq(uu - u))), l = lineLat(uu);
            if (lat + w > l - kShelfLine && lat - w < l + kShelfLine) return false;
        }
        return true;
    };
    // A hull rock: r its biggest radius across, `flat` how squat (height/width), buried a third. In the line's corridor it
    // stays a rock a truck climbs (0.34 m or less over the ground, so the line has no step over the 0.45 limit); bigger
    // ones keep out of it.
    auto stone = [&](double u, double lat, double r, double flat) {
        const double rh = r * 1.12;
        double ry = r * flat;
        const double lift = c.rng.range(1.0, 1.35);
        if (ry * lift > 0.2 && !corridorFree(u, lat, rh)) {
            if (ry * lift > kShelfRockTop && r < 0.9) ry = kShelfRockTop / lift;  // (squatter, to fit)
            else if (ry * lift > kShelfRockTop) return false;
        }
        for (const Bury& b : buried)  // (not on a buried boulder's crown; its draws are spent all the same, so the rest falls as it did)
            if (sq((u - b.u) / (0.9 * b.hx + r * 0.5)) + sq((lat - b.lat) / (0.9 * b.hz + r * 0.5)) < 1) {
                for (int k = 0; k < 4; k++) c.rng();
                return false;
            }
        c.rock(u, curve(u) + lat, V3(r * c.rng.range(0.85, 1.0), ry, r * c.rng.range(0.8, 1.0)), ry * lift, seed * 7919 + rockN++, (float)c.rng.range(0.85, 1.05));
        return true;
    };
    // Boulders in the chokes: a pair against the wall and a stone or two on the lip, their edges 0.1 m off the line's corridor.
    for (const Zone& z : {chokeA, chokeB})
        for (int k = 0; k < 3; k++) {
            const double u = z.a + 2 + k * (z.b - z.a - 4) / 2 + c.rng.range(-1, 1);
            const double r = c.rng.range(0.55, 0.75), flat = c.rng.range(0.7, 0.9), l = lineLat(u);
            stone(u, l - kShelfLine - 0.1 - r * 1.12 + c.rng.range(-0.15, 0.0), r, flat);      // against the wall
            if (k != 1) stone(u + c.rng.range(-1, 1), l + kShelfLine + 0.1 + 0.25 + c.rng.range(0, 0.2), c.rng.range(0.28, 0.4), c.rng.range(0.7, 0.9));  // on the lip
        }
    // Talus heaped along the wall's foot: small stones all along, three heaps of bigger ones.
    for (double u = 12; u < L - 12; u += c.rng.range(0.8, 2.0)) {
        if (zoneMask(u) > 0.5) continue;
        const double r = c.rng.range(0.15, 0.42) * (c.rng() < 0.15 ? 1.5 : 1.0);
        stone(u, -half + kShelfToe + c.rng.range(0.0, 0.5) + r * 0.6, r, c.rng.range(0.55, 0.85));
    }
    for (double hu : {17.0, 62.0, 160.0}) {
        for (int k = 0; k < 9; k++) {
            const double r = c.rng.range(0.2, 0.55);
            stone(hu + c.rng.range(-3.5, 3.5), -half + kShelfToe + c.rng.range(0.0, 0.9) + r * 0.5, r, c.rng.range(0.55, 0.85));
        }
    }
    // Fallen from above, strewn over the road the way the river's boulders are: mixed sizes (0.25-1.4 m radius, mostly small),
    // in clusters, thickest where you drive. Rocks in the line are low ones; the big ones lie beside it.
    {
        double cl[10];
        for (int k = 0; k < 10; k++) cl[k] = 22 + k * 14.5 + c.rng.range(-4, 4);  // (spread along it, not at the entry)
        int placed = 0;
        for (int k = 0; k < 6000 && placed < 150; k++) {
            const double u = c.rng.range(12, L - 12), lat = c.rng.range(-half + 0.5, half - 0.3);
            if (zoneMask(u) > 0.3) continue;
            double near = 0;
            for (double x : cl) near = std::max(near, bump(u, x, 6));
            const double mid = bump(lat - lineLat(u), 0, 1.4);
            if (c.rng() > 0.10 + 0.40 * near * (0.4 + 0.6 * mid) + 0.1 * mid) continue;
            if (mid > 0.37 && c.rng() > 0.4) continue;  // (thinner in the line itself: it's a way through)
            const double q = c.rng(), r = q < 0.6 ? c.rng.range(0.25, 0.5) : q < 0.9 ? c.rng.range(0.5, 0.85) : c.rng.range(0.9, 1.4);
            placed += stone(u, lat, r, c.rng.range(0.55, 0.8));
        }
    }
    // Stones perched on the drop's edge.
    // (Close enough together to catch a wheel: the first thing a sliding truck meets.)
    for (double u = 4; u < L - 6; u += c.rng.range(1.6, 3.4)) {
        if (zoneMask(u) > 0.5 && (u < straddle.a || u > straddle.b)) continue;
        const double r = c.rng.range(0.2, 0.42);
        stone(u, half - 0.1 + c.rng.range(-0.1, 0.2), r, c.rng.range(0.7, 1.0));
    }

    // ---- Slabs to drive. ----
    const double slopeA = std::atan(c.slope);  // the plane's tilt along the shelf: slabs on the road lie parallel to it
    // A slab whose top runs from (ua, rise above the road there) to (ub, rise there), `w` either side of lat: how the table's
    // ramps and platforms and the staircase's treads are laid, so each meets the next at the height asked.
    auto flat = [&](double ua, double ra, double ub, double rb, double lat, double w, double yaw, double warmth) {
        const double ya = ground(ua, curve(ua) + lat) + ra, yb = ground(ub, curve(ub) + lat) + rb, d = ub - ua;
        slabTop(0.5 * (ua + ub), lat, 0.5 * (ya + yb), 0.5 * std::hypot(d, yb - ya) + 0.06, w, 0.5, yaw, std::atan2(yb - ya, d), bedRoll, warmth);
    };
    // The table's rocks: a pair side by side along each of the ramps and the platform, laid on the ground's own ramps.
    {
        const double lat = lineLat(table.a + 7), yaw = yawAt(table.a + 7), pitch = std::atan2(tableRise, tableRun);
        struct Piece {
            double a, b, pitch;
        };
        for (const Piece& pc : {Piece{table.a, table.a + tableRun, pitch}, Piece{table.a + tableRun - 0.1, table.a + tableRun + tablePlat + 0.1, 0.0},
                                Piece{table.a + tableRun + tablePlat, table.a + 2 * tableRun + tablePlat, -pitch}}) {
            const double uc = 0.5 * (pc.a + pc.b), hl = 0.5 * std::hypot(pc.b - pc.a, pc.pitch * (pc.b - pc.a)) + 0.1;
            for (int side : {-1, 1})
                c.laid(uc, curve(uc) + lat + side * 0.8, -yaw, V3(hl, 0.3, 0.88), ground(uc, curve(uc) + lat + side * 0.8) + 0.14 + std::fabs(std::sin(pc.pitch)) * hl, slopeA + pc.pitch, bedRoll,
                       (float)c.rng.range(0.92, 1.06));
        }
    }
    // The off-camber run: slabs whose tilt grows toward the drop, a long one at 18 deg (the road itself leans 4-8; a stock Blazer slides off past ~20 on rock), then
    // eases back. Each lies flush with the road at its low edge (the line's drop side) so the step from one to the next is
    // at most ~0.25 m on the line and the truck is tipped toward the drop for ~8 m.
    {
        const double deg[] = {6, 10, 13, 16, 18, 18, 18, 16, 13, 10, 6};
        const int n = 11;
        const double len = (camber.b - camber.a) / n;
        for (int k = 0; k < n; k++) {
            const double u = camber.a + (k + 0.5) * len, lat = lineLat(u), th = deg[k] * M_PI / 180;
            const double latH = lat + 1.2;  // the hinge: the truck's drop-side edge
            const double latLo = -half - 0.3, latHi = latH + 0.3, latC = 0.5 * (latLo + latHi), w = 0.5 * (latHi - latLo);
            // the top's centre line: through the hinge at the road's height, rising toward the wall
            const double topY = ground(u, curve(u) + latH) + std::tan(th) * (latH - latC);
            slabTop(u, latC, topY, len * 0.5 + 0.12, w, 0.6, yawAt(u), slopeA, th, warm());
        }
    }
    // The staircase's treads: rocks laid on the ground's steps, a pair side by side across each (hulls with rounded ends, so they
    // overlap a hand's width and the ground between is a tenth under their tops).
    {
        const double lat = lineLat(stairs.a + 6), yaw = yawAt(stairs.a + 6);
        for (const Tread& t : treads) {
            const double uc = 0.5 * (t.a + t.b), hl = 0.5 * (t.b - t.a) + 0.12;
            for (int side : {-1, 1})
                c.laid(uc, curve(uc) + lat + side * 0.8, -(yaw + c.rng.range(-0.04, 0.04)), V3(hl, 0.3, 0.88), ground(uc, curve(uc) + lat + side * 0.8) + 0.13, slopeA, bedRoll, (float)c.rng.range(0.92, 1.06));
        }
    }
    // A pair of slabs side by side with a gap between them, 0.3 m proud (ramped on and off, 0.9 m each): a wheel on each, the gap between.
    {
        const double lat = lineLat(straddle.a + 6), yaw = yawAt(straddle.a + 6), ramp = 0.9;
        for (int side : {-1, 1}) {
            const double l = lat + side * 0.95, yw = yaw + c.rng.range(-0.015, 0.015);
            flat(straddle.a, 0, straddle.a + ramp, 0.3, l, 0.62, yw, warm());
            flat(straddle.a + ramp - 0.05, 0.3, straddle.b - 2 - ramp, 0.3, l, 0.62, yw, warm());
            flat(straddle.b - 2 - ramp, 0.3, straddle.b - 2, 0, l, 0.62, yw, warm());
        }
    }

    // ---- The buried boulders, laid in their mounds: the crown `cap` over the mound, the rest of the rock under the ground. ----
    for (const Bury& b : buried) {
        const double v = curve(b.u) + b.lat;
        c.laid(b.u, v, -(yawAt(b.u) + b.yaw), V3(b.hx, b.ry, b.hz), ground(b.u, v) + b.cap, slopeA, bedRoll * 0.8, (float)nr.range(0.9, 1.08));
    }
    // More fallen rock, thicker through the stretches the first strewing left bare (its clusters sit between theirs): a quarter as many
    // again, mixed sizes, thickest in the line, then a few stones heaped round each boulder.
    {
        double cl[10];
        for (int k = 0; k < 10; k++) cl[k] = 29 + k * 14.5 + c.rng.range(-4, 4);
        int placed = 0;
        for (int k = 0; k < 6000 && placed < 36; k++) {
            const double u = c.rng.range(12, L - 12), lat = c.rng.range(-half + 0.5, half - 0.3);
            if (zoneMask(u) > 0.3) continue;
            double near = 0;
            for (double x : cl) near = std::max(near, bump(u, x, 6));
            const double mid = bump(lat - lineLat(u), 0, 1.4);
            if (c.rng() > 0.10 + 0.40 * near * (0.4 + 0.6 * mid) + 0.1 * mid) continue;
            if (mid > 0.37 && c.rng() > 0.4) continue;
            const double q = c.rng(), r = q < 0.55 ? c.rng.range(0.25, 0.5) : q < 0.88 ? c.rng.range(0.5, 0.85) : c.rng.range(0.9, 1.4);
            placed += stone(u, lat, r, c.rng.range(0.55, 0.8));
        }
        for (const Bury& b : buried)
            for (int k = 0, got = 0; k < 12 && got < 2; k++) {
                const double a = c.rng.range(0, 2 * M_PI), r = c.rng.range(0.14, 0.34), e = c.rng.range(1.0, 1.35);
                const double lat = b.lat + std::sin(a) * (b.hz + r) * e;
                if (lat > half + 0.1 || lat < -half) continue;
                got += stone(b.u + std::cos(a) * (b.hx + r) * e, lat, r, c.rng.range(0.55, 0.85));
            }
    }

    // The line, one point a metre, on the finished ground.
    for (double u = 0; u <= L; u += 1) {
        const V3 p = c.at(u, curve(u) + lineLat(u));
        c.line.push_back(V3(p.x, c.w.height(p.x, p.z), p.z));
    }
}

// ---- 7. TIMBER -----------------------------------------------------------------------------------
void timber(Canvas& c) {
    trail(c, kTimberHalf, 6, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    // Trees growing in the track, with roots humped up round them.
    struct Trunk {
        double u, v, h;
    };
    std::vector<Trunk> trunks;
    for (int k = 0; k < 4; k++) trunks.push_back({30 + 25 * k + c.rng.range(-5, 5), c.rng.range(-1.0, 1.0), c.rng.range(12, 18)});
    Rough fx;  // ruts, bumps, roots, a gully, a dip
    fx.amp = 0.2, fx.rut = 0.15, fx.holes = 2, fx.washouts = 1, fx.cambers = 2;
    for (const Trunk& t : trunks) {
        fx.mounds.push_back({t.u, t.v, 1.0, 0.22});
        for (int i = 0; i < 3; i++) {
            const double a = c.rng.range(0, 6.283), d = c.rng.range(1.1, 1.8);
            fx.mounds.push_back({t.u + d * std::cos(a), t.v + d * std::sin(a), 0.55, c.rng.range(0.12, 0.22)});
        }
    }
    roughen(c, kTimberHalf, 6, fx, [](double) { return 0.0; });
    for (double u = 0; u <= c.fp.length; u += 2) c.lineUV.push_back({u, 0.0});
    c.commit();
    c.forestLow(kTimberClear, 0.5, true, kTimberNear, kNearH);  // a wall of trunks either side, back from the track
    for (const Trunk& t : trunks) c.tree(t.u, t.v, t.h, 0);
    for (int k = 0; k < 3; k++) {  // windfall across it: too big to climb in a truck
        const double u = 42 + 25 * k + c.rng.range(-4, 4), a = c.rng.range(-0.4, 0.4), len = c.rng.range(kTimberLog), r = c.rng.range(kTimberLogR);
        V3 p0 = c.at(u - std::sin(a) * len / 2, -std::cos(a) * len / 2), p1 = c.at(u + std::sin(a) * len / 2, std::cos(a) * len / 2);
        p0.y = c.w.height(p0.x, p0.z) + r * 0.85, p1.y = c.w.height(p1.x, p1.z) + r * 0.85;
        c.w.addLog(p0, p1, r);
        if (c.rec) {
            LevelObject o;
            o.kind = OBJ_LOG, o.p = p0, o.p2 = p1, o.size = V3(r, 0, 0);
            c.rec->push_back(o);
        }
    }
    // Rock among the roots: a few round each trunk in the track, more heaped beside it, and one at the foot of the trunks along it.
    for (const Trunk& t : trunks)
        for (int i = 0; i < 3; i++) {
            const double a = c.rng.range(0, 6.283), d = c.rng.range(1.0, 1.7), r = c.rng.range(0.18, 0.4);
            c.boulder(t.u + d * std::cos(a), t.v + d * std::sin(a), r);
        }
    for (int cl = 0; cl < 9; cl++) {  // heaps beside the track
        const double pu = c.rng.range(24, c.fp.length - 24), side = c.rng() < 0.5 ? -1 : 1, pv = side * c.rng.range(2.6, 5.5), radius = c.rng.range(1.3, 2.6);
        const int n = 4 + (int)(c.rng() * 4);
        for (int i = 0; i < n; i++) {
            const double a = c.rng.range(0, 6.283), d = radius * std::sqrt(c.rng()), v = pv + d * std::sin(a);
            if (std::fabs(v) < 2.5) continue;  // (the track's edge stays free but for the odd stray)
            c.boulder(pu + d * std::cos(a), v, boulderR(c.rng, 0.55, 0.35, 0.8));
        }
    }
    int foot = 0;
    for (size_t i = 0; i < c.w.trees.size() && foot < 40; i++) {
        double u, v;
        const Tree& t = c.w.trees[i];
        c.local(t.base.x, t.base.z, u, v);
        if (std::fabs(v) < 3.5 || std::fabs(v) > 9 || u < 22 || u > c.fp.length - 22 || c.rng() > 0.2) continue;
        const double a = c.rng.range(0, 6.283), d = t.radius + c.rng.range(0.3, 0.9);
        foot += c.boulder(u + d * std::cos(a), v + d * std::sin(a), c.rng.range(0.2, 0.55));
    }
}

}  // namespace

// ---- Dunes as a height function ------------------------------------------------------------------
// Summed directional waveforms. The dominant one is a transverse-ridge wave running along the wind (so
// the ridges lie across it): a sawtooth with a long gentle windward back, a short slip face at the
// angle of repose and a flat hollow before the next. Its phase is bent by a second, longer wave at
// another angle (the ridges become crescents and sinuous linguoid ridges) and by low-frequency noise
// (a domain warp: crests meander and merge). Crest height swells to the middle of the field and
// dwindles toward its edge. Two smoother, smaller waves run over it at slight angles (swells, then
// whoops) and the floor between the dunes undulates.
double duneHeight(double u, double v, const DuneParams& P, uint32_t seed, double* aboveFloor) {
    if (aboveFloor) *aboveFloor = 0;
    const double ox = (seed % 997) * 13.1 + 200, oz = (seed % 991) * 7.7 + 200;
    auto signedNoise = [&](double x, double z, int oct) { return clampd((fbmNoise(x + ox, z + oz, oct) - 0.5) * 3.4, -1, 1); };
    const double du = std::fabs(u / P.halfU), dv = std::fabs(v / P.halfV);
    const double d = std::cbrt(du * du * du + dv * dv * dv);
    const double eEdge = smoothstep(1.0, 0.4, d), eCore = smoothstep(0.95, 0.25, d);
    if (eEdge <= 0) return 0;
    const double th = P.windDeg * M_PI / 180;
    const double a2 = th + P.bendDeg * M_PI / 180, k2 = 2 * M_PI / P.bendLength;
    const double bendPh = (seed % 13) * 0.7;
    // Where a wave's crests are: along its direction, plus the meander and the bending wave.
    auto phaseAt = [&](double dir, double spacing, double warpAmp, double bendAmp, double x, double z) {
        const double warp = warpAmp * signedNoise(x / P.warpScale, z / P.warpScale, 2);
        const double bend = bendAmp * std::sin(k2 * (x * std::cos(a2) + z * std::sin(a2)) + bendPh);
        return (x * std::cos(dir) + z * std::sin(dir) + warp + bend) / spacing;
    };
    // Ridges: a sawtooth wave across the wind. A ridge's height is that where its back begins (found by stepping
    // upwind from here), so it's the same all across the ridge's profile and the slopes aren't bent by the field's
    // swelling or the noise. The back's and the slip's lengths follow from that height and the real spacing of the
    // crests here (the phase gradient), so the slopes come out right wherever the warp squeezes them.
    auto ridges = [&](double dir, double spacing, double warpAmp, double bendAmp, double heightFrac, double windward, double slipAngle, double noiseScale, double noiseAt) {
        const double tW = std::tan(windward * M_PI / 180), tS = std::tan(slipAngle * M_PI / 180);
        const double ph = phaseAt(dir, spacing, warpAmp, bendAmp, u, v);
        const double gx = phaseAt(dir, spacing, warpAmp, bendAmp, u + 1, v) - ph, gz = phaseAt(dir, spacing, warpAmp, bendAmp, u, v + 1) - ph;
        const double g = std::max(std::hypot(gx, gz), 0.25 / spacing);  // crests per metre, across them
        const double phi = ph - std::floor(ph);
        const double back0 = std::min(phi / g, 1.5 * spacing);          // metres up the wind to where this ridge's back begins
        const double u0 = u - std::cos(dir) * back0, v0 = v - std::sin(dir) * back0;
        const double e0 = std::cbrt(std::pow(std::fabs(u0 / P.halfU), 3) + std::pow(std::fabs(v0 / P.halfV), 3));
        const double base = P.crestMin * smoothstep(1.0, 0.5, e0) + (P.crestMax - P.crestMin) * smoothstep(0.92, 0.3, e0);
        const double crest = base * heightFrac * (0.86 + 0.28 * signedNoise(u0 / noiseScale + noiseAt, v0 / noiseScale + noiseAt * 0.45, 2));
        const double A = std::min(crest, 0.9 / (g * (1 / tW + 1 / tS)));  // (a crest the spacing has no room for is cut down to size)
        const double back = A * g / tW, slip = A * g / tS;
        double prof = 0;
        if (phi < back) {
            const double t = phi / back;
            prof = 0.8 * t + 0.2 * t * t * (3 - 2 * t);
        } else if (phi < back + slip) prof = 1 - (phi - back) / slip;
        return A * prof * smoothstep(1.0, 0.94, d);  // (nothing's left of a ridge at the very rim: the sand there settles)
    };
    const double bendAmp = P.bend * (0.65 + 0.35 * signedNoise(u / 400 + 7, v / 400 + 3, 2));
    const double big = ridges(th, P.spacing, P.warp, bendAmp, 1.0, P.windwardDeg, P.slipDeg, 190, 11);
    const double mid = ridges(th + 0.12, P.subSpacing, P.warp * 0.8, P.bend * 0.5, P.sub, P.windwardDeg * 0.8, P.slipDeg * 0.95, 120, 3);
    // Small lopsided swells over the lot (a cosine with its phase pushed along itself), far gentler than the slip faces.
    const double baseHere = P.crestMin * eEdge + (P.crestMax - P.crestMin) * eCore;
    const double p3 = 2 * M_PI * phaseAt(th - 0.15, P.subSpacing * 0.45, P.warp * 0.5, P.bend * 0.3, u, v);
    const double small = P.sub * 0.25 * baseHere * (0.7 + 0.6 * signedNoise(u / 80 + 9, v / 80 + 1, 2)) * 0.5 * (1 - std::cos(p3 + 0.5 * std::sin(p3)));
    const double dunes = big + mid + small;
    // The hollows' floor: a slow swell (and a long wave across it).
    const double floorH = P.swell * eEdge * (0.7 * signedNoise(u / 210 + 1, v / 210 + 4, 3) + 0.3 * std::sin(2 * M_PI * (u * std::sin(th) - v * std::cos(th)) / 410 + (seed % 7)));
    if (aboveFloor) *aboveFloor = dunes;
    return floorH + dunes;
}

namespace {
const Spec& specOf(SectionType t) { return kSpecs[std::min((int)t, (int)SEC_COUNT - 1)]; }
}  // namespace

const char* sectionName(SectionType t) { return specOf(t).name; }
double sectionLength(SectionType t) { return specOf(t).length; }
SectionFootprint sectionFootprint(SectionType t) { return {specOf(t).length, specOf(t).half, kApron, kBlend}; }

SectionBuilt buildSection(World& w, SectionType t, uint32_t seed, const SectionFrame& f, std::vector<LevelObject>* record) {
    Canvas c(w, t, seed, f, record);
    c.clearGround();
    switch (t) {
        case SEC_STEEP_HILL: steepHill(c); break;
        case SEC_MUD_PIT: mudPit(c); break;
        case SEC_ROCKY_RIVER: rockyRiver(c); break;
        case SEC_ROCK_WALL: rockWall(c); break;
        case SEC_DUNES: dunes(c); break;
        case SEC_CLIFF_SHELF: cliffShelf(c); break;
        case SEC_TIMBER: timber(c); break;
        default: break;
    }
    SectionBuilt out;
    out.line = c.line;
    const double L = sectionLength(t);
    // (The shelf's line wanders, and ends a little off the heading.)
    const double exitV = t == SEC_CLIFF_SHELF ? 5 * std::sin(L * 0.05 + seed) : 0;
    out.entry = c.at(0, 0), out.exit = c.at(L, exitV);
    out.entry.y = w.height(out.entry.x, out.entry.z), out.exit.y = w.height(out.exit.x, out.exit.z);
    out.exitHeading = f.heading;
    // The line to drive: what the generator marked in world coordinates (the shelf), else the (u, v) one it set, else straight along.
    if (out.line.empty()) {
        if (c.lineUV.empty())
            for (double u = 0; u <= L; u += 2) c.lineUV.push_back({u, exitV * u / L});
        for (const auto& uv : c.lineUV) {
            V3 p = c.at(uv.first, uv.second);
            p.y = w.height(p.x, p.z);
            out.line.push_back(p);
        }
    }
    return out;
}
