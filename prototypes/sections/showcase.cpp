// PROTOTYPE obstacle-section generators (spike): each stamps one seeded section into a blank region
// of the proving ground so it can be driven and screenshotted. Not repo code; informs the spec.
//
// A section runs along +X from x = X0 to X1 through the region's middle (z = ZC). Calibration from
// the probes: grid walls >= 65 deg seal a chokepoint even for the rock buggy; a set-up Bronco holds a
// 35 deg side slope and rolls at 40; stock slides at 30 (dirt); shallow mud pits are easy, so pits get
// steep exit banks and water.
#include <cmath>
#include <cstdio>
#include <string>

#include "harness.h"
#include "props.h"
#include "level.h"

namespace {

constexpr double RX0 = -560, RX1 = -200, ZC = -380, X0 = -450, X1 = -310;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9e3779b9u) { next(); }
    uint32_t next() { s ^= s << 13, s ^= s >> 17, s ^= s << 5; return s; }
    double operator()() { return (next() & 0xFFFFFF) / double(0x1000000); }
    double range(double a, double b) { return a + (b - a) * (*this)(); }
};
double sq(double v) { return v * v; }

// ---- Recording what's placed, so a section can be saved as a level (.czl) and driven in the game.
Level gLv;
Rock& addRockR(World& w, double x, double z, const V3& r, double top, uint32_t seed, float tint = 1) {
    LevelObject o;
    o.kind = OBJ_ROCK, o.p = V3(x, 0, z), o.size = r, o.top = top, o.seed = seed, o.col[0] = tint;
    gLv.objects.push_back(o);
    return w.addRock(x, z, r, top, seed, tint);
}
void addBoxR(World& w, V3 c, V3 h, double yaw, double pitch, double roll, V3 col) {
    LevelObject o;
    o.kind = OBJ_BOX, o.p = c, o.size = h, o.yaw = yaw, o.pitch = pitch, o.roll = roll;
    o.col[0] = (float)col.x, o.col[1] = (float)col.y, o.col[2] = (float)col.z;
    gLv.objects.push_back(o);
    w.addBox(c, h, yaw, pitch, roll, col);
}
void markRock(World& w) { w.boxes.back().rock = true, gLv.objects.back().sub = 1; }
void addTreeR(World& w, double x, double z, double h, int kind, float tint = 1) {
    LevelObject o;
    o.kind = OBJ_TREE, o.p = V3(x, 0, z), o.size = V3(0, h, 0), o.sub = kind, o.col[0] = tint;
    gLv.objects.push_back(o);
    w.addTree(x, z, h, kind, tint);
}
void addLogR(World& w, V3 a, V3 b, double r) {
    LevelObject o;
    o.kind = OBJ_LOG, o.p = a, o.p2 = b, o.size = V3(r, 0, 0);
    gLv.objects.push_back(o);
    w.addLog(a, b, r);
}


// Rolling country under everything (seeded offset into the shared noise).
double land(double x, double z, uint32_t seed) {
    const double ox = (seed % 997) * 13.1, oz = (seed % 991) * 7.7;
    return 7 * (fbmNoise(x * 0.012 + ox, z * 0.012 + oz, 4) - 0.5) + 2 * (valueNoise(x * 0.05 + ox, z * 0.05) - 0.5);
}

struct Canvas {
    World& w;
    uint32_t seed;
    Rng rng;
    Canvas(World& w, uint32_t seed) : w(w), seed(seed), rng(seed) {}
    template <class F> void each(double x0, double x1, double z0, double z1, F f) {
        for (double x = x0; x <= x1; x += 1)
            for (double z = z0; z <= z1; z += 1) {
                const int id = w.cellIndex(x, z);
                if (id >= 0) f(id, x, z);
            }
    }
    void setH(int id, double h) { w.setCellHeight(id, (float)h); }
    double getH(int id) const { return w.cellHeight(id); }
    void paint(int id, Surface s) {
        static const uint8_t look[SURF_COUNT] = {PAINT_PAD, PAINT_NONE, PAINT_TRAIL, PAINT_ROCK, PAINT_SCREE, PAINT_GRAVEL,
                                                 PAINT_SAND, PAINT_MUD, PAINT_SNOW, PAINT_ICE, PAINT_WET};
        w.setCellSurface(id, s, look[s]);
    }
    void water(int id, double level, double fx = 0, double fz = 0) {
        w.setCellWater(id, (float)level);
        w.flowX[id] = (float)fx, w.flowZ[id] = (float)fz;
    }
    // Forest outside |z - ZC| > clear (dense when `wall`: trunks too close to drive between).
    void forest(double clear, double density, bool wall, double x0 = X0 - 30, double x1 = X1 + 30) {
        for (int k = 0; k < 9000; k++) {
            const double x = rng.range(x0, x1), z = rng.range(ZC - 70, ZC + 70);
            const double lat = std::fabs(z - ZC);
            if (lat < clear) continue;
            const bool edge = wall && lat < clear + 8;
            if (!edge && rng() > density) continue;
            if (edge && rng() > 0.9) continue;
            const int id = w.cellIndex(x, z);
            if (w.waterLevel(x, z) > w.height(x, z) - 0.2) continue;
            (void)id;
            const int kind = rng() < 0.75 ? 0 : (rng() < 0.8 ? 1 : 2);
            addTreeR(w, x, z, rng.range(8, 17), kind, (float)rng.range(0.8, 1.15));
        }
    }
};

World& blank(Ctx& ctx, uint32_t seed) {
    World& w = ctx.sim.world;
    gLv = Level();
    gLv.map = MAP_PROVING_GROUND;
    auto inR = [](double x, double z) { return x > RX0 && x < RX1 && z > RX0 && z < RX1; };
    for (const Rock& r : w.rocks) if (inR(r.c.x, r.c.z)) gLv.removed.push_back(V3(r.c.x, 0, r.c.z));
    for (const Tree& t : w.trees) if (inR(t.base.x, t.base.z)) gLv.removed.push_back(V3(t.base.x, 0, t.base.z));
    for (const Box& b : w.boxes) if (inR(b.c.x, b.c.z)) gLv.removed.push_back(V3(b.c.x, 0, b.c.z));
    w.rocks.clear(), w.trees.clear(), w.poles.clear(), w.boxes.clear(), w.logs.clear(), w.bushes.clear();
    w.stations.clear(), w.fixtures.clear();
    w.crashProps = false;
    Canvas c(w, seed);
    c.each(RX0, RX1, RX0, RX1, [&](int id, double x, double z) {
        // Fade the noise to 0 at the region's rim so it meets the proving ground.
        const double rim = std::min({x - RX0, RX1 - x, z - RX0, RX1 - z});
        c.setH(id, land(x, z, seed) * smoothstep(0, 40, rim));
        c.paint(id, SURF_GRASS);
    });
    return w;
}

// A trail along the section at heights y(x), `half` wide, blended into the land within `blend`.
template <class Y, class S>
void trail(Canvas& c, double half, double blend, Y y, S surf) {
    c.each(X0 - 25, X1 + 25, ZC - half - blend, ZC + half + blend, [&](int id, double x, double z) {
        const double e = std::fabs(z - ZC) - half;
        const double wgt = e < 0.5 ? 1.0 : smoothstep(blend, 0.5, e);
        c.setH(id, c.getH(id) + (y(x) - c.getH(id)) * wgt);
        if (e < 0.5) c.paint(id, surf(x));
    });
}
// Walls either side: from |lat| = half the ground rises at `deg` to `H` above the trail (seals it).
template <class Y>
void walls(Canvas& c, double xa, double xb, double half, double deg, double H, Y y, Surface face) {
    const double run = H / std::tan(deg * M_PI / 180);
    c.each(xa - 10, xb + 10, ZC - half - run - 25, ZC + half + run + 25, [&](int id, double x, double z) {
        const double e = std::fabs(z - ZC) - half;
        if (e < 0) return;
        const double fade = smoothstep(xa - 10, xa, x) * smoothstep(xb + 10, xb, x);
        const double rise = e < run ? H * e / run : H + 0.15 * (e - run);
        const double target = y(x) + rise + 0.6 * (valueNoise(x * 0.4, z * 0.4) - 0.5) * (e < run ? 1 : 0);
        c.setH(id, std::max(c.getH(id), c.getH(id) + (target - c.getH(id)) * fade));
        if (e < run + 1 && fade > 0.5) c.paint(id, face);
    });
}

// ---- 1. STEEP HILL: a loose climb in a gully, rock ledges across it near the top ----------------
void steepHill(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    const double xs = -405, len = c.rng.range(34, 44), rise = len * std::tan(c.rng.range(24, 30) * M_PI / 180);
    auto y = [&](double x) { return rise * smoothstep(xs, xs + len, x) * 0.15 + rise * clampd((x - xs) / len, 0, 1) * 0.85; };
    trail(c, 2.6, 10, y, [&](double x) { return x > xs && x < xs + len ? SURF_SCREE : SURF_DIRT; });
    walls(c, xs - 6, xs + len + 4, 4.2, 68, 4.5, y, SURF_ROCK);
    // Ledges on the upper half: square rock lips the line must climb.
    for (int k = 0; k < 3; k++) {
        const double x = xs + len * (0.45 + 0.18 * k), h = c.rng.range(0.32, 0.5), g = std::tan(std::atan(rise / len));
        addBoxR(w, V3(x, y(x) + h / 2 - 0.4, ZC), V3(0.9, h / 2 + 0.4, 3.0), 0, -std::atan(g), 0, V3(0.45, 0.42, 0.38));
        markRock(w);
    }
    for (int k = 0; k < 14; k++) {  // loose rocks on the climb
        const double x = c.rng.range(xs, xs + len), z = ZC + c.rng.range(-2.4, 2.4), r = c.rng.range(0.2, 0.45);
        addRockR(w, x, z, V3(r, r * 0.7, r * 0.9), r * 0.5, seed * 31 + k);
    }
    c.forest(14, 0.08, false);
    w.buildGrid();
}

// ---- 2. MUD PIT: a long pit, floor depth varying 0.3-1.5 m, standing water, steep exit bank -------
void mudPit(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    trail(c, 2.6, 12, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    const double xa = -410, xb = xa + c.rng.range(42, 56), half = 7.5;
    c.each(xa - 6, xb + 6, ZC - half - 8, ZC + half + 8, [&](int id, double x, double z) {
        const double u = std::min(x - xa, xb - x), v = half + 2.5 * (valueNoise(x * 0.12, seed) - 0.5) - std::fabs(z - ZC);
        if (u < -4 || v < -6) return;
        const double depth = 0.3 + 1.2 * fbmNoise(x * 0.08 + seed, z * 0.08, 3);  // varies along and across
        // Entry bank gentle, exit bank steep (1:1): you drop in easy and fight your way out.
        const double bankIn = clampd((x - xa) / 3.0, 0, 1), bankOut = clampd((xb - x) / 1.1, 0, 1), side = clampd(v / 2.0, 0, 1);
        const double f = std::min({bankIn, bankOut, side});
        const double h = -depth * f + 0.12 * (valueNoise(x * 0.6, z * 0.6) - 0.5);
        c.setH(id, std::min(c.getH(id), h));
        if (f > 0 || (u > -3 && v > -4)) c.paint(id, SURF_MUD);
        if (f > 0.3) c.water(id, -0.12);
    });
    // Ruts carried through by the trucks before you.
    c.each(xa - 25, xa, ZC - 2, ZC + 2, [&](int id, double, double z) {
        c.setH(id, c.getH(id) - 0.12 * std::exp(-sq((std::fabs(z - ZC) - 0.9) / 0.4)));
        c.paint(id, SURF_MUD);
    });
    c.forest(half + 2, 0.15, true, xa - 6, xb + 6);  // dense timber both sides: no driving round
    c.forest(5, 0.05, false);
    w.buildGrid();
}

// ---- 3. ROCKY RIVER: a channel across the trail, knee-to-thigh deep, current, boulders everywhere --
void rockyRiver(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    trail(c, 2.6, 12, [](double) { return 1.0; }, [](double) { return SURF_DIRT; });
    const double xr = -385, width = c.rng.range(20, 28), depth = c.rng.range(1.2, 1.5), skew = c.rng.range(-0.35, 0.35);
    const double level = 1.0 - depth + c.rng.range(0.55, 0.85);  // water depth over the bed
    c.each(RX0 + 20, RX1 - 20, RX0 + 20, RX1 - 20, [&](int id, double x, double z) {
        const double cx = xr + skew * (z - ZC) + 6 * std::sin(z * 0.03 + seed);
        const double d = std::fabs(x - cx) - width / 2;
        if (d > 14) return;
        const double bed = 1.0 - depth + 0.25 * (valueNoise(x * 0.2, z * 0.2) - 0.5);
        const double bank = d < 0 ? bed : bed + (c.getH(id) - bed) * smoothstep(0, 14, d) + d * 0.0;
        c.setH(id, d < 0 ? bed : std::min(c.getH(id), bank + d * 0.35));
        if (d < 0) c.paint(id, SURF_GRAVEL), c.water(id, level, 0, 1.4);
        else if (d < 5) c.paint(id, SURF_SAND);
    });
    // Boulders: lots, mixed sizes, thickest mid-channel. Some too big to climb: pick a line.
    int placed = 0;
    for (int k = 0; k < 4000 && placed < 140; k++) {
        const double z = ZC + c.rng.range(-26, 26), cx = xr + skew * (z - ZC) + 6 * std::sin(z * 0.03 + seed);
        const double x = cx + c.rng.range(-width / 2, width / 2);
        const double mid = 1 - std::fabs(x - cx) / (width / 2);
        if (c.rng() > 0.35 + 0.65 * mid) continue;
        const double q = c.rng(), r = q < 0.6 ? c.rng.range(0.25, 0.5) : q < 0.9 ? c.rng.range(0.5, 0.85) : c.rng.range(0.9, 1.4);
        addRockR(w, x, z, V3(r, r * c.rng.range(0.55, 0.8), r * c.rng.range(0.7, 1.0)), r * c.rng.range(0.5, 0.8), seed * 131 + k,
                  (float)c.rng.range(0.85, 1.1));
        placed++;
    }
    c.forest(8, 0.07, false);
    w.buildGrid();
}

// ---- 4. ROCK WALL (v2): a slickrock formation in a slot canyon ----------------------------------
// Sandstone ledges whose lips wander across the canyon; boulders sit on every lip so each riser is
// a rounded rock face 0.7-1.6 m tall. Each ledge has one notch (a stepping stone halves the step,
// ~0.45-0.6 m each, the Bronco's limit) on alternating sides: find it or stay put. A V-notch
// squeeze, slabs on the treads, a pour-over pool under the big ledge and a final ledge too tall
// for anyone (winch to the junipers above).
struct SlickLedge {
    double x, rise, amp, phase, notchZ, tilt;
};
struct Slick {
    std::vector<SlickLedge> L;
    double xs = -402, xe = 0, vx0 = 0, vx1 = 0, wallH = 8;
    uint32_t seed = 1;
    double lip(int k, double z) const {
        return L[k].x + L[k].amp * std::sin(z * 0.45 + L[k].phase) + 0.9 * (valueNoise(z * 0.3, k * 7.3 + seed) - 0.5);
    }
    double halfW(double x) const {
        if (x > vx0 && x < vx1) return 2.7 + 0.4 * std::sin(x);  // the squeeze
        return 4.6 + 1.6 * std::sin(x * 0.09 + seed) + 0.8 * valueNoise(x * 0.2, seed);
    }
    double floorY(double x, double z) const {
        double h = 0;
        for (size_t k = 0; k < L.size(); k++) {
            const double s = smoothstep(lip((int)k, z) - 0.25, lip((int)k, z) + 0.55, x);
            h += L[k].rise * s + L[k].tilt * (z - ZC) * s;
        }
        h += 0.35 * (valueNoise(x * 0.17, z * 0.17) - 0.5) * smoothstep(xs - 10, xs, x);  // slickrock humps
        if (x > vx0 - 2 && x < vx1 + 2) h += 0.6 * std::fabs(z - ZC) * smoothstep(vx0 - 2, vx0 + 1, x) * smoothstep(vx1 + 2, vx1 - 1, x);
        return h;
    }
};
std::vector<V3> gSlickLine;  // the line through the notches (for the crawl)

void rockWall(Ctx& ctx, uint32_t seed) {
    props::setActiveRockScans(&props::sharedHullLibrary());
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    Slick s;
    s.seed = seed;
    double x = s.xs;
    const int n = 6;
    for (int k = 0; k < n; k++) {
        SlickLedge l;
        l.x = x;
        l.rise = k == n - 1 ? c.rng.range(1.3, 1.6) : c.rng.range(0.75, 1.15);
        l.amp = c.rng.range(0.8, 2.2);
        l.phase = c.rng.range(0, 6.28);
        l.notchZ = ZC + (k % 2 ? 1 : -1) * c.rng.range(0.8, 2.2);
        l.tilt = c.rng.range(-0.10, 0.10);
        s.L.push_back(l);
        x += c.rng.range(4.5, 7.5);
        if (k == 2) s.vx0 = x - 1, s.vx1 = x + 7, x += 8;  // the squeeze after the third ledge
    }
    s.xe = x;
    // Terrain: sand wash in, the formation, the plateau out; canyon walls either side.
    c.each(X0 - 30, X1 + 30, ZC - 40, ZC + 40, [&](int id, double px, double pz) {
        const double hw = s.halfW(px), e = std::fabs(pz - ZC) - hw;
        double h = s.floorY(px, pz);
        const double inCanyon = smoothstep(s.xs - 25, s.xs - 12, px) * smoothstep(s.xe + 18, s.xe + 6, px);
        if (e > 0) {
            const double rise = std::min(e * 3.8, s.wallH) + 0.25 * std::max(0.0, e - s.wallH / 3.8);
            h += rise * inCanyon + 1.5 * (valueNoise(px * 0.25, pz * 0.25) - 0.5) * smoothstep(0, 1.5, e) * inCanyon;
        }
        const double base = c.getH(id), wgt = smoothstep(55, 30, std::fabs(pz - ZC)) * smoothstep(X0 - 30, X0 - 5, px) * smoothstep(X1 + 30, X1 + 5, px);
        c.setH(id, base + (h - base) * wgt);
        if (wgt > 0.5) w.setCellSurface(id, px < s.xs - 3 && e < 0 ? SURF_DIRT : SURF_ROCK, px < s.xs - 3 && e < 0 ? PAINT_DESERT : PAINT_REDROCK);
    });
    // The pour-over pool under the fifth ledge.
    {
        const int k = n - 2;
        c.each(s.L[k].x - 6, s.L[k].x + 1, ZC - 6, ZC + 6, [&](int id, double px, double pz) {
            const double u = s.lip(k, pz) - px;  // m in front of the lip
            if (u < 0.2 || u > 4.5 || std::fabs(pz - ZC) > s.halfW(px) - 0.5) return;
            const double d = 0.9 * smoothstep(4.5, 2.5, u) * smoothstep(0.2, 1.2, u);
            c.setH(id, c.getH(id) - d);
            if (d > 0.3) c.water(id, s.floorY(s.L[k].x - 5, ZC) - 0.12);
        });
    }
    // Sandstone slabs make each ledge: a row of thick rock slabs across the canyon, their fronts on
    // the lip, tops at the upper tread, each turned and tipped a little, overlapping. At the notch a
    // half-height slab stands in front: two steps of ~half the rise. The last ledge has no notch.
    gSlickLine.clear();
    auto sand = [&](double k) { const double v = c.rng.range(-0.06, 0.06) + k; return V3(0.70 + v, 0.42 + v * 0.8, 0.30 + v * 0.6); };
    for (int k = 0; k < n; k++) {
        const SlickLedge& l = s.L[k];
        const double hw = s.halfW(l.x);
        for (double z = ZC - hw - 1.5; z <= ZC + hw + 1.5;) {
            const double hz = c.rng.range(0.8, 1.4), hx = c.rng.range(1.3, 2.4), th = c.rng.range(0.45, 0.8);
            const double lx = s.lip(k, z), top = s.floorY(lx + 2.0, z) + c.rng.range(-0.05, 0.18);
            const double yaw = c.rng.range(-0.3, 0.3);
            addBoxR(w, V3(lx + hx * 0.9, top - th, z), V3(hx, th, hz), yaw, c.rng.range(-0.05, 0.05), c.rng.range(-0.06, 0.06), sand(0));
            markRock(w);
            z += hz * c.rng.range(1.2, 1.7);
        }
        if (k < n - 1) {  // the notch: a staircase of slabs, no step over ~0.4 m
            const double lx = s.lip(k, l.notchZ), lower = s.floorY(lx - 3.0, l.notchZ), upper = s.floorY(lx + 2.0, l.notchZ) + 0.18;
            const int ns = (int)std::ceil((upper - lower) / 0.40);
            for (int j = ns - 1; j >= 1; j--) {
                const double top = lower + (upper - lower) * j / ns, len = 1.0 * (ns - j) + 0.4, th = std::max(0.3, top - lower + 0.3) / 2;
                addBoxR(w, V3(lx - len / 2 + 0.3, top - th, l.notchZ), V3(len / 2, th, 1.4), c.rng.range(-0.08, 0.08), 0, 0, sand(-0.04));
                markRock(w);
            }
        }
        gSlickLine.push_back(V3(s.lip(k, l.notchZ) - 4, 0, l.notchZ));
        gSlickLine.push_back(V3(s.lip(k, l.notchZ) + 2.5, 0, l.notchZ));
    }
    // Banded strata in the canyon walls: slabs jutting from the face at each layer.
    for (double px = s.xs - 12; px < s.xe + 6; px += c.rng.range(1.8, 3.2))
        for (int side : {-1, 1})
            for (double yr = c.rng.range(0.6, 1.4); yr < s.wallH - 0.5; yr += c.rng.range(0.9, 1.7)) {
                if (c.rng() < 0.25) continue;
                const double hw = s.halfW(px), face = hw + yr / 3.8, ha = c.rng.range(0.8, 1.6), jut = c.rng.range(0.2, 0.7);
                const double floor = s.floorY(px, ZC + side * hw);
                addBoxR(w, V3(px, floor + yr, ZC + side * (face + ha - jut)), V3(c.rng.range(1.0, 2.2), c.rng.range(0.25, 0.5), ha), c.rng.range(-0.15, 0.15), 0,
                        c.rng.range(-0.05, 0.05), sand(c.rng.range(-0.08, 0.05)));
                markRock(w);
            }
    // Slabs on the treads, boulders heaped along the canyon walls.
    for (int k = 0; k + 1 < n; k++)
        if (c.rng() < 0.7) {
            const double px = (s.L[k].x + s.L[k + 1].x) / 2 + c.rng.range(-1, 1), side = c.rng() < 0.5 ? -1 : 1;
            const double pz = ZC + side * (s.halfW(px) - c.rng.range(1.5, 2.5));
            addRockR(w, px, pz, V3(c.rng.range(1.6, 2.6), c.rng.range(0.35, 0.6), c.rng.range(1.2, 2.0)), 0.45, seed * 1000 + (uint32_t)(c.rng() * 1e6), 0.95f);
        }
    for (double px = s.xs - 15; px < s.xe + 8; px += c.rng.range(1.5, 3.5))
        for (int side : {-1, 1}) {
            const double r = c.rng.range(1.2, 3.2), pz = ZC + side * (s.halfW(px) + r * 0.35 + c.rng.range(0, 1.5) * (r > 2.2));
            addRockR(w, px, pz, V3(r, r * c.rng.range(0.6, 0.9), r * c.rng.range(0.7, 1.0)), r * c.rng.range(0.6, 0.95), seed * 1000 + (uint32_t)(c.rng() * 1e6),
                      (float)c.rng.range(0.8, 1.05));
            if (c.rng() < 0.35) {  // one up in the wall face
                const double r2 = c.rng.range(1.5, 3.0), pz2 = ZC + side * (s.halfW(px) + c.rng.range(2, 5));
                addRockR(w, px + c.rng.range(-1, 1), pz2, V3(r2, r2 * 0.7, r2), r2 * 0.6, seed * 1000 + (uint32_t)(c.rng() * 1e6), (float)c.rng.range(0.8, 1.0));
            }
        }
    // Junipers on the plateau (anchors for the last ledge) and scattered over the rims.
    for (int k = 0; k < 12; k++) addTreeR(w, s.xe + c.rng.range(3, 16), ZC + c.rng.range(-7, 7), c.rng.range(4, 7), 1, 0.8f);
    for (int k = 0; k < 80; k++) {
        const double px = c.rng.range(X0 - 20, X1 + 20), pz = ZC + (c.rng() < 0.5 ? -1 : 1) * c.rng.range(12, 50);
        addTreeR(w, px, pz, c.rng.range(3.5, 7), c.rng() < 0.8 ? 1 : 2, 0.8f);
    }
    gSlickLine.push_back(V3(s.xe + 12, 0, ZC));
    w.buildGrid();
}

// A set-up Bronco crawls the line through the notches (steers at the next point ahead).
void crawlLine(Ctx& ctx, double seconds) {
    VehicleSpec& spec = *new VehicleSpec(vehicleCatalog()[2]);
    spec.lockerFront = true;
    const V3 st = gSlickLine.front();
    Car& car = ctx.add(spec, st.x - 6, st.z, 0);
    ctx.focus(car);
    ctx.run(1.0);
    car.setDriveMode((int)driveModes(spec.transfer).size() - 1);
    car.setLocker(true, true), car.setLocker(false, true);
    car.tractionControl = false, car.tirePsi = 12, car.swayDisconnected = spec.swayDisconnect > 0;
    size_t next = 0, bestNext = 0;
    double best = st.x, bestT = 0;
    ctx.run(seconds, [&] {
        Frame fr = car.frame();
        while (next + 1 < gSlickLine.size() && fr.com.x > gSlickLine[next].x - 1.0) next++;
        const V3 d = V3(gSlickLine[next].x, fr.com.y, gSlickLine[next].z) - fr.com;
        car.steer = clampd(std::atan2(dot(d, fr.left), std::max(dot(d, fr.fwd), 0.5)) * 1.6, -0.6, 0.6);
        const double v = dot(fr.vel, fr.fwd);
        car.throttle = clampd(0.35 + (0.9 - v) * 1.2, 0, 1);
        car.brake = v > 1.8 ? 0.3 : 0;
        if (fr.com.x > best + 0.5) best = fr.com.x, bestT = ctx.t, bestNext = next;
    });
    std::printf("CRAWLEND reached x=%.1f at t=%.0f (passed %zu of %zu line points; top of the wall at x=%.1f)%s\n", best, bestT, bestNext, gSlickLine.size(), gSlickLine.back().x - 12, car.engineDead ? " ENGINE DEAD" : "");
}

// ---- 5. SAND DUNES: a dune field the trail crosses: ridges with steep slip faces ---------------
void dunes(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    const double ang = c.rng.range(-0.5, 0.5), amp = c.rng.range(4, 6.5);
    c.each(RX0 + 10, RX1 - 10, RX0 + 10, RX1 - 10, [&](int id, double x, double z) {
        const double fade = smoothstep(150, 110, std::hypot((x + 380) * 0.8, z - ZC));
        if (fade <= 0) return;
        const double u = std::cos(ang) * x + std::sin(ang) * z + 18 * std::sin(0.013 * z + seed);
        // Asymmetric ridge: gentle windward back, steep slip face.
        const double ph = std::fmod(u * 0.035 + 1000, 1.0), ridge = ph < 0.8 ? ph / 0.8 : (1 - ph) / 0.2;
        const double h = amp * std::pow(ridge, 1.6) * (0.6 + 0.6 * valueNoise(x * 0.015, z * 0.015)) + 1.0 * valueNoise(x * 0.07, z * 0.07);
        c.setH(id, c.getH(id) * (1 - fade) + h * fade);
        if (fade > 0.3) c.paint(id, SURF_SAND);
    });
    c.forest(120, 0.02, false);
    w.buildGrid();
}

// ---- 6. CLIFF SHELF: a one-truck shelf cut into a cliff, a real drop off the edge, rocks and dips ----
void cliffShelf(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    const double roadY = 22, half = c.rng.range(1.7, 2.0);  // 3.4-4 m of road: a truck is ~1.9
    const double tilt = std::tan(c.rng.range(5, 10) * M_PI / 180);  // leans you toward the drop (+z)
    auto curve = [&](double x) { return ZC + 5 * std::sin(x * 0.05 + seed); };
    c.each(RX0 + 10, RX1 - 10, RX0 + 10, RX1 - 10, [&](int id, double x, double z) {
        const double fade = smoothstep(X0 - 40, X0 - 10, x) * smoothstep(X1 + 40, X1 + 10, x);
        const double lat = z - curve(x);  // + toward the drop
        double h;
        if (lat < -half) h = roadY + 0.3 + (-lat - half) * 2.6 + 3 * (valueNoise(x * 0.1, z * 0.1) - 0.5);   // cliff above (~69 deg)
        else if (lat <= half) {
            h = roadY - tilt * (lat + half);                                                                     // the road
            h -= 0.32 * std::pow(std::max(0.0, std::sin(x * 0.42 + 3 * valueNoise(x * 0.05, seed))), 6);          // dips
        } else h = roadY - tilt * 2 * half - (lat - half) * 2.8 + 2 * (valueNoise(x * 0.1, z * 0.1) - 0.5);       // the drop
        h = std::max(h, 0.0 + 4 * valueNoise(x * 0.03, z * 0.03));                                               // valley floor
        const double base = c.getH(id);
        c.setH(id, base + (h - base) * fade);
        if (fade > 0.5) c.paint(id, std::fabs(lat) <= half ? SURF_GRAVEL : SURF_ROCK);
    });
    // Rocks on the road: some fallen from above narrow it to barely a truck.
    for (int k = 0; k < 18; k++) {
        const double x = c.rng.range(X0 + 10, X1 - 10), big = c.rng() < 0.3;
        const double r = big ? c.rng.range(0.6, 0.9) : c.rng.range(0.2, 0.4);
        const double lat = big ? -half + r * 0.4 : c.rng.range(-half, half);
        addRockR(w, x, curve(x) + lat, V3(r, r * 0.7, r * 0.9), r * 0.6, seed * 17 + k);
    }
    w.buildGrid();
}

// ---- 7. TIMBER: a forest track blocked by standing trees and windfall (cutting comes later) -------
void timber(Ctx& ctx, uint32_t seed) {
    World& w = blank(ctx, seed);
    Canvas c(w, seed);
    trail(c, 1.9, 6, [](double) { return 0.0; }, [](double) { return SURF_DIRT; });
    c.forest(2.6, 0.5, true);  // a wall of trunks either side
    for (int k = 0; k < 4; k++) {  // trees growing in the track
        const double x = -420 + 25 * k + c.rng.range(-5, 5);
        addTreeR(w, x, ZC + c.rng.range(-1.0, 1.0), c.rng.range(12, 18), 0);
    }
    for (int k = 0; k < 3; k++) {  // windfall across it: too big to climb in a truck
        const double x = -408 + 25 * k + c.rng.range(-4, 4), a = c.rng.range(-0.4, 0.4), L = c.rng.range(9, 14), r = c.rng.range(0.32, 0.45);
        const V3 m(x, 0, ZC), d(std::sin(a) * L / 2, 0, std::cos(a) * L / 2);
        V3 p0 = m - d, p1 = m + d;
        p0.y = w.height(p0.x, p0.z) + r * 0.85, p1.y = w.height(p1.x, p1.z) + r * 0.85;
        addLogR(w, p0, p1, r);
    }
    w.buildGrid();
}


// Saves the section just built as a level: every changed cell, surface and water level in the region,
// everything placed, and the start.
void exportLevel(Ctx& ctx, const std::string& name, V3 start, double yaw) {
    World& w = ctx.sim.world;
    World base(MAP_PROVING_GROUND);
    for (double x = RX0; x <= RX1; x += 1)
        for (double z = RX0; z <= RX1; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id < 0) continue;
            if (std::fabs(w.cellHeight(id) - base.cellHeight(id)) > 0.004) gLv.heights[id] = w.cellHeight(id);
            if (w.cellSurface(id) != base.cellSurface(id) || w.cellPaint(id) != base.cellPaint(id))
                gLv.surfaces[id] = (uint16_t)(w.cellSurface(id) << 8 | w.cellPaint(id));
            if (!w.waterY.empty() && w.waterY[id] > -1e8f) gLv.water[id] = w.waterY[id];
        }
    gLv.name = "GEN " + name;
    for (char& ch : gLv.name) ch = (char)std::toupper((unsigned char)ch);
    gLv.about = "GENERATED OBSTACLE SECTION (PROTOTYPE)";
    gLv.author = "GENERATOR";
    gLv.hasStart = true, gLv.start = start, gLv.startYaw = yaw;
    std::string err, path = userLevelFolder() + "/gen-" + name + ".czl";
    for (char& ch : path) if (ch == ' ') ch = '-';
    const bool ok = saveLevelFile(gLv, path, &err);
    std::printf("EXPORT %s -> %s %s (%zu cells, %zu objects)\n", name.c_str(), path.c_str(), ok ? "ok" : err.c_str(), gLv.heights.size(), gLv.objects.size());
}

// The truck parked at `x` on the section, a buddy behind it, the camera on it (orbit set on the CLI).
void park(Ctx& ctx, double x, double zOff = 0, double heading = 0) {
    Car& a = ctx.add(vehicleCatalog()[2], x, ZC + zOff, heading);
    ctx.focus(a);
    Car& b = ctx.add(vehicleCatalog()[3], x - 14, ZC + zOff, heading);
    (void)b;
    for (Car& car : ctx.sim.cars) car.handbrake = 1;
    ctx.run(600);
}

int reg = [] {
    auto add = [](const std::string& n, std::function<void(Ctx&)> f) { scenarioRegistry().push_back({"show/" + n, n, std::move(f)}); };
    for (uint32_t seed : {1u, 2u, 3u}) {
        const std::string s = std::to_string(seed);
        add("hill-" + s, [seed](Ctx& c) { steepHill(c, seed); park(c, -390); });
        add("mud-" + s, [seed](Ctx& c) { mudPit(c, seed); park(c, -395); });
        add("river-" + s, [seed](Ctx& c) { rockyRiver(c, seed); park(c, -392); });
        add("wall-" + s, [seed](Ctx& c) { rockWall(c, seed); crawlLine(c, 90); });
        add("dunes-" + s, [seed](Ctx& c) { dunes(c, seed); park(c, -385); });
        add("cliff-" + s, [seed](Ctx& c) { cliffShelf(c, seed); park(c, -385, 5 * std::sin(-385 * 0.05 + seed)); });
        add("timber-" + s, [seed](Ctx& c) { timber(c, seed); park(c, -428); });
        auto ex = [&](const std::string& n, void (*gen)(Ctx&, uint32_t), double x) {
            scenarioRegistry().push_back({"export/" + n + "-" + s, n, [seed, n, gen, x, s](Ctx& c) {
                gen(c, seed);
                exportLevel(c, n + "-" + s, V3(x, 0, n == "cliff" ? ZC + 5 * std::sin(x * 0.05 + seed) : ZC), 0);
            }});
        };
        ex("hill", steepHill, -440);
        ex("mud", mudPit, -440);
        ex("river", rockyRiver, -440);
        ex("wall", rockWall, -420);
        ex("dunes", dunes, -440);
        ex("cliff", cliffShelf, -440);
        ex("timber", timber, -445);
    }
    return 0;
}();

}  // namespace
