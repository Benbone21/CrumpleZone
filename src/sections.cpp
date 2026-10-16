#include "sections.h"

#include <algorithm>
#include <cmath>
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

// Steep hill: a loose scree climb in a gully, rock ledges across it near the top.
constexpr double kHillStart = 45;                    // the climb starts here
constexpr Range kHillLen = {34, 44}, kHillAngle = {24, 30}, kHillLedgeH = {0.32, 0.5}, kHillRock = {0.2, 0.45};
constexpr double kHillTrailHalf = 2.6, kHillTrailBlend = 10, kHillWallHalf = 4.2, kHillWallDeg = 68, kHillWallH = 4.5;
constexpr int kHillLedges = 3, kHillRocks = 14;

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
constexpr double kWallStart = 48;  // the first ledge's mean position
constexpr int kWallLedges = 6;
constexpr Range kWallRise = {0.75, 1.15}, kWallLastRise = {1.3, 1.6}, kWallGap = {4.5, 7.5}, kWallAmp = {0.8, 2.2}, kWallTilt = {-0.10, 0.10};
constexpr double kWallSqueezeAfter = 2, kWallSqueezeLen = 8, kWallSqueezeHalf = 2.7, kWallHeight = 8, kWallStep = 0.40;
constexpr double kWallRimSlope = 0.12, kWallFadeIn = 36;  // the land rises this much per m beyond the canyon's walls; the walls fade in over kWallFadeIn m
constexpr Range kWallSlabHx = {1.3, 2.4}, kWallSlabHz = {0.8, 1.4}, kWallSlabTh = {0.45, 0.8};

// Dunes: a sea of sand (duneHeight below): ridges with gentle windward backs and steep slip faces.
constexpr Range kDuneWind = {18, 38}, kDuneSpacing = {125, 150}, kDuneCrest = {18, 21};  // degrees from the heading; m; m
constexpr double kDuneRepose = 32;   // sand holds no steeper than this (degrees): the field is relaxed to it
constexpr double kDuneHardpan = 0.7, kDunePatch = 0.5;  // hollows lower than this above the floor (m) are hard ground where the patch noise tops that
constexpr int kDuneSmooth = 2, kDuneRelax = 60;  // passes rounding the crests, passes settling the slopes

// Cliff shelf: a one-truck shelf cut into a cliff, a real drop off the edge, rocks and dips.
constexpr Range kShelfHalf = {1.7, 2.0}, kShelfTilt = {5, 10};  // 3.4-4 m of road; leans you toward the drop
constexpr double kShelfDrop = 22, kShelfCliffMax = 24, kShelfEase = 40;
constexpr int kShelfRocks = 18;

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
                k.h = (float)(land(u, v, seed) * smoothstep(0, 30, u) * smoothstep(fp.length, fp.length - 30, u));  // (calm at both ends)
                if (k.wgt > 0.5f) k.surf = SURF_GRASS, k.paint = PAINT_NONE;
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

    // Takes the ground (everything standing on it goes), then blends this over it.
    void clearGround() {
        clearObjectsWhere(w, [&](double x, double z) {
            double u, v;
            local(x, z, u, v);
            return outside(u, v) < fp.blend + 1;
        });
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
    }

    // ---- Objects (after commit(): they sit on the finished ground) ------------------------------
    Rock& rock(double u, double v, const V3& r, double top, uint32_t rseed, float tint = 1) {
        const V3 p = at(u, v);
        if (rec) {
            LevelObject o;
            o.kind = OBJ_ROCK, o.p = p, o.size = r, o.top = top, o.seed = rseed, o.col[0] = tint;
            rec->push_back(o);
        }
        return w.addRock(p.x, p.z, r, top, rseed, tint);
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

// ---- 1. STEEP HILL -------------------------------------------------------------------------------
void steepHill(Canvas& c) {
    const double xs = kHillStart, len = c.rng.range(kHillLen), rise = len * std::tan(c.rng.range(kHillAngle) * M_PI / 180);
    auto y = [&](double u) { return rise * smoothstep(xs, xs + len, u) * 0.15 + rise * clampd((u - xs) / len, 0, 1) * 0.85; };
    trail(c, kHillTrailHalf, kHillTrailBlend, y, [&](double u) { return u > xs && u < xs + len ? SURF_SCREE : SURF_DIRT; });
    // The gully's walls run on past the top, opening out onto the summit.
    walls(c, xs - 6, c.fp.length + c.fp.apron, kHillWallHalf, kHillWallDeg, [&](double u) { return kHillWallH * smoothstep(xs + len + 40, xs + len + 6, u); }, y, SURF_ROCK);
    c.commit();
    // Ledges on the upper half: square rock lips the line must climb.
    for (int k = 0; k < kHillLedges; k++) {
        const double u = xs + len * (0.45 + 0.18 * k), h = c.rng.range(kHillLedgeH), g = rise / len;
        c.slab(u, y(u) + h / 2 - 0.4, 0, V3(0.9, h / 2 + 0.4, 3.0), 0, -std::atan(g), 0, V3(0.45, 0.42, 0.38));
    }
    for (int k = 0; k < kHillRocks; k++) {  // loose rocks on the climb
        const double u = c.rng.range(xs, xs + len), v = c.rng.range(-2.4, 2.4), r = c.rng.range(kHillRock);
        c.rock(u, v, V3(r, r * 0.7, r * 0.9), r * 0.5, c.seed * 31 + k);
    }
    c.forest(14, 0.08, false);
}

// ---- 2. MUD PIT ----------------------------------------------------------------------------------
void mudPit(Canvas& c) {
    const uint32_t seed = c.seed;
    trail(c, 2.6, 12, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    const double xa = kMudStart, xb = xa + c.rng.range(kMudLen), half = kMudHalf;
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
    c.commit();
    // Dense timber both sides (no driving round), its trunks 3.5 m back from the mud and its crowns kept low near it.
    c.forest(half + kMudClear, 0.15, true, xa - 6, xb + 6, 1.0, 6.0, kNearH);
    c.forest(half + kMudClear, 0.05, false, -c.fp.apron, c.fp.length + c.fp.apron, kForestPerM2, 6.0, kNearH);
}

// ---- 3. ROCKY RIVER ------------------------------------------------------------------------------
void rockyRiver(Canvas& c) {
    const uint32_t seed = c.seed;
    trail(c, 2.6, 12, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
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

// ---- 4. ROCK WALL (v3): a slickrock formation in a slot canyon -------------------------------------
// Sandstone ledges whose lips wander across the canyon; each ledge a row of thick rock slabs (tops at
// the upper tread, faces 0.75-1.3 m) with a notch built as a staircase of slabs, no step over ~0.4 m,
// on alternating sides: find it or stay put. A V-notch squeeze, a pour-over pool under the fifth
// ledge, banded strata jutting from the canyon walls and a final ledge too tall for anyone (winch to
// the junipers above).
struct SlickLedge {
    double x, rise, amp, phase, notchZ, tilt;
};
struct Slick {
    std::vector<SlickLedge> L;
    double xs = kWallStart, xe = 0, vx0 = 0, vx1 = 0, wallH = kWallHeight;
    uint32_t seed = 1;
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
        h += 0.35 * (valueNoise(x * 0.17, v * 0.17) - 0.5) * smoothstep(xs - 10, xs, x);  // slickrock humps
        if (x > vx0 - 2 && x < vx1 + 2) h += 0.6 * std::fabs(v) * smoothstep(vx0 - 2, vx0 + 1, x) * smoothstep(vx1 + 2, vx1 - 1, x);
        return h;
    }
};

void rockWall(Canvas& c) {
    const uint32_t seed = c.seed;
    Slick s;
    s.seed = seed;
    double x = s.xs;
    const int n = kWallLedges;
    for (int k = 0; k < n; k++) {
        SlickLedge l;
        l.x = x;
        l.rise = k == n - 1 ? c.rng.range(kWallLastRise) : c.rng.range(kWallRise);
        l.amp = c.rng.range(kWallAmp);
        l.phase = c.rng.range(0, 6.28);
        l.notchZ = (k % 2 ? 1 : -1) * c.rng.range(0.8, 2.2);
        l.tilt = c.rng.range(kWallTilt);
        s.L.push_back(l);
        x += c.rng.range(kWallGap);
        if (k == kWallSqueezeAfter) s.vx0 = x - 1, s.vx1 = x + kWallSqueezeLen - 1, x += kWallSqueezeLen;  // the squeeze after the third ledge
    }
    s.xe = x;
    // Terrain: dirt wash in, the formation, the plateau out; canyon walls either side.
    c.each(-c.fp.apron, c.fp.length + c.fp.apron, -c.fp.half, c.fp.half, [&](int id, double pu, double pv) {
        const double hw = s.halfW(pu), e = std::fabs(pv) - hw;
        const double inCanyon = smoothstep(s.xs - kWallFadeIn, s.xs - 10, pu) * smoothstep(s.xe + 18, s.xe + 6, pu);
        double h = s.floorY(pu, pv) + c.getH(id) * (1 - inCanyon);  // (the land's own relief outside the canyon)
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
    // Sandstone slabs make each ledge: a row of thick rock slabs across the canyon, their fronts on
    // the lip, tops at the upper tread, each turned and tipped a little, overlapping. At the notch a
    // half-height slab stands in front: two steps of ~half the rise. The last ledge has no notch.
    auto sand = [&](double k) { const double v = c.rng.range(-0.06, 0.06) + k; return V3(0.70 + v, 0.42 + v * 0.8, 0.30 + v * 0.6); };
    for (int k = 0; k < n; k++) {
        const SlickLedge& l = s.L[k];
        const double hw = s.halfW(l.x);
        for (double v = -hw - 1.5; v <= hw + 1.5;) {
            const double hz = c.rng.range(kWallSlabHz), hx = c.rng.range(kWallSlabHx), th = c.rng.range(kWallSlabTh);
            const double lx = s.lip(k, v), top = s.floorY(lx + 2.0, v) + c.rng.range(-0.05, 0.18);
            const double yaw = c.rng.range(-0.3, 0.3);
            c.slab(lx + hx * 0.9, top - th, v, V3(hx, th, hz), yaw, c.rng.range(-0.05, 0.05), c.rng.range(-0.06, 0.06), sand(0));
            v += hz * c.rng.range(1.2, 1.7);
        }
        if (k < n - 1) {  // the notch: a staircase of slabs, no step over ~0.4 m
            const double lx = s.lip(k, l.notchZ), lower = s.floorY(lx - 3.0, l.notchZ), upper = s.floorY(lx + 2.0, l.notchZ) + 0.18;
            const int ns = (int)std::ceil((upper - lower) / kWallStep);
            for (int j = ns - 1; j >= 1; j--) {
                const double top = lower + (upper - lower) * j / ns, len = 1.0 * (ns - j) + 0.4, th = std::max(0.3, top - lower + 0.3) / 2;
                c.slab(lx - len / 2 + 0.3, top - th, l.notchZ, V3(len / 2, th, 1.4), c.rng.range(-0.08, 0.08), 0, 0, sand(-0.04));
            }
        }
    }
    // Banded strata in the canyon walls: slabs jutting from the face at each layer.
    for (double pu = s.xs - 12; pu < s.xe + 6; pu += c.rng.range(1.8, 3.2))
        for (int side : {-1, 1})
            for (double yr = c.rng.range(0.6, 1.4); yr < s.wallH - 0.5; yr += c.rng.range(0.9, 1.7)) {
                if (c.rng() < 0.25) continue;
                const double hw = s.halfW(pu), face = hw + yr / 3.8, ha = c.rng.range(0.8, 1.6), jut = c.rng.range(0.2, 0.7);
                const double floor = s.floorY(pu, side * hw);
                c.slab(pu, floor + yr, side * (face + ha - jut), V3(c.rng.range(1.0, 2.2), c.rng.range(0.25, 0.5), ha), c.rng.range(-0.15, 0.15), 0, c.rng.range(-0.05, 0.05),
                       sand(c.rng.range(-0.08, 0.05)));
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
    const int off[8] = {1, -1, n, -n, n + 1, n - 1, -n + 1, -n - 1};
    std::vector<float> tmp(mine.size());
    // Rounded crests and toes (a 3x3 kernel), then the repose: sand slides off anything steeper than kDuneRepose.
    for (int pass = 0; pass < kDuneSmooth; pass++) {
        for (size_t m = 0; m < mine.size(); m++) {
            const int id = c.cells[mine[m]].id;
            double sum = c.cells[mine[m]].h * 0.4, wsum = 0.4;
            for (int k = 0; k < 8; k++) {
                const int j = at[id + off[k]];
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
    const int fwd[4] = {1, n, n + 1, n - 1};
    for (int pass = 0; pass < kDuneRelax; pass++)
        for (size_t mm = 0; mm < mine.size(); mm++) {
            const size_t m = pass & 1 ? mine.size() - 1 - mm : mm;
            Canvas::Cell& a = c.cells[mine[m]];
            for (int k = 0; k < 4; k++) {
                const int j = at[a.id + fwd[k]];
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
    c.commit();
}

// ---- 6. CLIFF SHELF ------------------------------------------------------------------------------
void cliffShelf(Canvas& c) {
    const uint32_t seed = c.seed;
    const double half = c.rng.range(kShelfHalf);  // 3.4-4 m of road: a truck is ~1.9
    const double tilt = std::tan(c.rng.range(kShelfTilt) * M_PI / 180);  // leans you toward the drop (+v)
    const double L = c.fp.length;
    auto curve = [&](double u) { return 5 * std::sin(u * 0.05 + seed) * smoothstep(0, kShelfEase, u); };  // (eases onto the line from the entry)
    c.each(-c.fp.apron, L + c.fp.apron, -c.fp.half, c.fp.half, [&](int id, double u, double v) {
        const double fade = smoothstep(-c.fp.apron, 10, u) * smoothstep(L + c.fp.apron, L - 10, u);
        const double lat = v - curve(u);  // + toward the drop
        double h;
        if (lat < -half) {  // cliff above (~69 deg), levelling off on top
            const double raw = 0.3 + (-lat - half) * 2.6 + 3 * (valueNoise(u * 0.1, v * 0.1) - 0.5);
            h = raw < kShelfCliffMax ? raw : kShelfCliffMax + 0.1 * (raw - kShelfCliffMax);
        } else if (lat <= half) {
            h = -tilt * (lat + half);                                                                              // the road
            h -= 0.32 * std::pow(std::max(0.0, std::sin(u * 0.42 + 3 * valueNoise(u * 0.05, seed))), 6);           // dips
        } else h = -tilt * 2 * half - (lat - half) * 2.8 + 2 * (valueNoise(u * 0.1, v * 0.1) - 0.5);               // the drop
        h = std::max(h, -kShelfDrop + 4 * valueNoise(u * 0.03, v * 0.03));                                         // valley floor
        const double base = c.getH(id);
        c.setH(id, base + (h - base) * fade);
        if (fade > 0.5) c.paint(id, std::fabs(lat) <= half ? SURF_GRAVEL : SURF_ROCK);
    });
    c.commit();
    // Rocks on the road: some fallen from above narrow it to barely a truck.
    for (int k = 0; k < kShelfRocks; k++) {
        const double u = c.rng.range(10, L - 10), big = c.rng() < 0.3;
        const double r = big ? c.rng.range(0.6, 0.9) : c.rng.range(0.2, 0.4);
        const double lat = big ? -half + r * 0.4 : c.rng.range(-half, half);
        c.rock(u, curve(u) + lat, V3(r, r * 0.7, r * 0.9), r * 0.6, seed * 17 + k);
    }
}

// ---- 7. TIMBER -----------------------------------------------------------------------------------
void timber(Canvas& c) {
    trail(c, kTimberHalf, 6, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    c.commit();
    c.forestLow(kTimberClear, 0.5, true, kTimberNear, kNearH);  // a wall of trunks either side, back from the track
    for (int k = 0; k < 4; k++) c.tree(30 + 25 * k + c.rng.range(-5, 5), c.rng.range(-1.0, 1.0), c.rng.range(12, 18), 0);  // trees growing in the track
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
    const double L = sectionLength(t);
    // (The shelf's line wanders, and ends a little off the heading.)
    const double exitV = t == SEC_CLIFF_SHELF ? 5 * std::sin(L * 0.05 + seed) : 0;
    out.entry = c.at(0, 0), out.exit = c.at(L, exitV);
    out.entry.y = w.height(out.entry.x, out.entry.z), out.exit.y = w.height(out.exit.x, out.exit.z);
    out.exitHeading = f.heading;
    return out;
}
