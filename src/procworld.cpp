// The procedural world: the generator (pure functions of seed and global position), the chunk cache and World::buildProcedural
// (a window of the land). See procworld.h and docs/superpowers/specs/2026-10-25-procedural-world-design.md.
#include "procworld.h"
#include "procrules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>

#include "forest.h"
#include "loadprof.h"
#include "parallel.h"
#include "plants.h"
#include "softbody.h"
#include "spawnpad.h"
#include "world.h"

namespace proc {

const char* biomeName(int b) {
    static const char* n[PB_COUNT] = {"MEADOW", "FOREST", "BADLANDS", "MOUNTAINS", "DUNES"};
    return b >= 0 && b < PB_COUNT ? n[b] : "";
}

namespace {

// ---- Integer hashing and noise (no libm: the same bytes on every machine) ------------------------------------------------------
inline uint64_t mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
inline uint64_t hashIJ(uint64_t s, int64_t i, int64_t j) { return mix64(s ^ mix64((uint64_t)i * 0x9E3779B97F4A7C15ull + (uint64_t)j * 0xD1B54A32D192ED03ull)); }
inline double to01(uint64_t h) { return (double)(h >> 11) * (1.0 / 9007199254740992.0); }
inline double hash01(uint64_t s, int64_t i, int64_t j) { return to01(hashIJ(s, i, j)); }

// Smooth value noise in [0, 1] on the unit lattice.
double vnoise(uint64_t s, double x, double z) {
    const double fx = std::floor(x), fz = std::floor(z);
    const int64_t i = (int64_t)fx, j = (int64_t)fz;
    double tx = x - fx, tz = z - fz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    const double a = hash01(s, i, j), b = hash01(s, i + 1, j), c = hash01(s, i, j + 1), d = hash01(s, i + 1, j + 1);
    return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
}
// Fractal noise in [0, 1] (each octave its own lattice, turned, so no grid shows).
double fbm(uint64_t s, double x, double z, int oct) {
    double sum = 0, a = 0.5, tot = 0;
    for (int o = 0; o < oct; o++) {
        sum += a * vnoise(s + (uint64_t)o * 0x632BE59BD9B4E019ull, x, z);
        tot += a;
        const double nx = 1.6 * x + 1.2 * z, nz = -1.2 * x + 1.6 * z;
        x = nx + 5.1, z = nz + 3.7;
        a *= 0.5;
    }
    return sum / tot;
}
// Ridged fractal noise in [0, 1]: sharp crests.
double ridged(uint64_t s, double x, double z, int oct) {
    double sum = 0, a = 0.5, tot = 0;
    for (int o = 0; o < oct; o++) {
        const double n = 1 - std::fabs(2 * vnoise(s + (uint64_t)o * 0x632BE59BD9B4E019ull, x, z) - 1);
        sum += a * n * n;
        tot += a;
        const double nx = 1.6 * x + 1.2 * z, nz = -1.2 * x + 1.6 * z;
        x = nx + 17.3, z = nz - 9.1;
        a *= 0.5;
    }
    return sum / tot;
}
inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double sqr(double v) { return v * v; }

// A chunk's own random numbers (xorshift64*), from the seed, the chunk and a salt.
struct Rand {
    uint64_t s;
    Rand(uint64_t seed, int64_t cx, int64_t cz, uint64_t salt) : s(hashIJ(seed ^ salt, cx, cz) | 1) {}
    double operator()() {
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return to01(s * 2685821657736338717ull);
    }
    double range(double a, double b) { return a + (b - a) * (*this)(); }
};

}  // namespace

// ---- The seed's parameters ----------------------------------------------------------------------------------------------------
enum Salt {
    S_WET, S_RUG, S_HEAT, S_CONT, S_HILL1, S_HILL2, S_MESA, S_TERR, S_RIDGE, S_RANGE, S_DUNE, S_DUNE2, S_BUMP1, S_BUMP2, S_GROUND1,
    S_GROUND2, S_GROVE, S_DITHER, S_RIVER, S_CREEK, S_WARP1, S_WARP2, S_SNOW, S_COUNT
};
struct ProcWorld::Params {
    uint64_t s[S_COUNT];
    double scale = 1;          // how big the land's features are (0.85 - 1.2)
    double bias[PB_COUNT];     // how much of each biome the world has
    double wetShift = 0, rugShift = 0, heatShift = 0;
    double windX = 1, windZ = 0;  // the dunes' crests run across this
    double relief = 1;         // how tall the mountains
};

ProcWorld::ProcWorld(uint64_t seed) : seed_(seed), params_(std::make_shared<Params>()) {
    Params& p = *params_;
    for (int k = 0; k < S_COUNT; k++) p.s[k] = mix64(seed * 0x9E3779B97F4A7C15ull + (uint64_t)k * 0xA24BAED4963EE407ull + 0x1234567ull);
    auto u = [&](int k) { return to01(mix64(seed ^ (0xC0FFEEull + (uint64_t)k * 0x9E3779B97F4A7C15ull))); };
    p.scale = 0.85 + 0.35 * u(1);
    for (int b = 0; b < PB_COUNT; b++) p.bias[b] = 0.45 + 1.1 * u(10 + b);
    p.wetShift = 0.3 * (u(2) - 0.5), p.rugShift = 0.3 * (u(3) - 0.5), p.heatShift = 0.3 * (u(4) - 0.5);
    double wx = u(5) - 0.5, wz = u(6) - 0.5;
    const double wl = std::sqrt(wx * wx + wz * wz);
    if (wl > 1e-6) p.windX = wx / wl, p.windZ = wz / wl;
    p.relief = 0.75 + 0.5 * u(7);
}

const ProcWorld& ProcWorld::forSeed(uint64_t seed) {
    static std::mutex m;
    static std::unordered_map<uint64_t, std::unique_ptr<ProcWorld>> made;
    std::lock_guard<std::mutex> lock(m);
    auto& slot = made[seed];
    if (!slot) slot = std::make_unique<ProcWorld>(seed);
    return *slot;
}

uint64_t ProcWorld::randomSeed() {
    uint64_t s = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    try {
        std::random_device rd;
        s ^= ((uint64_t)rd() << 32) ^ rd();
    } catch (...) {
    }
    s = mix64(s);
    return s ? s : 1;
}

// ---- The smooth fields ----------------------------------------------------------------------------------------------------------
namespace {
// The climate's spots for each biome: (wet, rug, heat).
const double kBiomeAt[PB_COUNT][3] = {
    {0.52, 0.22, 0.50},  // meadow
    {0.82, 0.50, 0.40},  // forest
    {0.25, 0.58, 0.78},  // badlands
    {0.55, 0.95, 0.30},  // mountains
    {0.08, 0.15, 0.88},  // dunes
};
inline double stretch(double v, double shift) { return clamp01((v - 0.5) * 2.6 + 0.5 + shift); }
}  // namespace

namespace {
// Ridged multifractal (after Musgrave) in about [0, 1]: sharp crests whose detail is strongest high up (eroded-looking flanks).
double ridgedMF(uint64_t s, double x, double z, int oct) {
    double sum = 0, amp = 0.5, weight = 1, tot = 0;
    for (int o = 0; o < oct; o++) {
        double n = 1 - std::fabs(2 * vnoise(s + (uint64_t)o * 0x632BE59BD9B4E019ull, x, z) - 1);
        n *= n;
        n *= weight;
        weight = clamp01(n * 2.0);
        sum += n * amp;
        tot += amp;
        const double nx = 1.6 * x + 1.2 * z, nz = -1.2 * x + 1.6 * z;
        x = nx + 17.3, z = nz - 9.1;
        amp *= 0.5;
    }
    return sum / tot;
}
// Where the climate puts the lowland biomes (wet, heat): meadow, forest, badlands, dunes (the mountains come from height).
const double kLowAt[PB_COUNT][2] = {{0.5, 0.5}, {0.82, 0.38}, {0.25, 0.75}, {0, 0}, {0.08, 0.9}};
}  // namespace

Macro ProcWorld::land(double x, double z) const {
    const Params& P = *params_;
    const double S = P.scale;
    Macro m;
    m.wet = stretch(fbm(P.s[S_WET], x * 0.00022 / S + 0.3, z * 0.00022 / S - 0.7, 3), P.wetShift);
    m.heat = stretch(fbm(P.s[S_HEAT], x * 0.00017 / S + 2.2, z * 0.00017 / S + 1.3, 2), P.heatShift);

    // ---- The relief: a broad swell, ranges of ridged mountains in belts, rolling foothills everywhere ----
    const double cont = 120 * (fbm(P.s[S_CONT], x * 0.00012 / S, z * 0.00012 / S, 4) - 0.5);
    // Where the ranges are: belts of a slow field (warped, so they bend), how high this world's go.
    const double bwx = 2500 * (fbm(P.s[S_WARP1] + 5, x * 0.00008, z * 0.00008, 2) - 0.5), bwz = 2500 * (fbm(P.s[S_WARP2] + 5, x * 0.00008, z * 0.00008, 2) - 0.5);
    const double belt = fbm(P.s[S_RANGE], (x + bwx) * 0.00011 / S, (z + bwz) * 0.00011 / S, 3);
    const double mk = smoothstep(0.40 - 0.06 * P.relief, 0.60, belt);  // 0 the lowlands .. 1 the heart of a range
    // The ridges: domain-warped ridged multifractal (crests a couple of km apart, spurs and gullies down their flanks).
    const double wx = 650 * (fbm(P.s[S_WARP1], x * 0.0004, z * 0.0004, 3) - 0.5), wz = 650 * (fbm(P.s[S_WARP2], x * 0.0004, z * 0.0004, 3) - 0.5);
    const double rr = ridgedMF(P.s[S_RIDGE], (x + wx) * 0.00052 / S, (z + wz) * 0.00052 / S, 6);
    const double range = mk * (260 + P.relief * (rr * rr * 820 + rr * 90));  // high valleys and passes ~260 m, peaks to ~1.2 km
    // Foothills everywhere, bigger near the ranges; and a smaller roll on them.
    const double hill = fbm(P.s[S_HILL1], x * 0.0011 / S, z * 0.0011 / S, 4) - 0.5;
    const double hill2 = fbm(P.s[S_HILL2], x * 0.0042 / S + 3.0, z * 0.0042 / S - 1.0, 3) - 0.5;
    const double foot = (110 + 170 * mk) * hill + 26 * hill2;
    double h = cont + 45 + foot + range;

    // ---- The biomes: up high, the mountains; below, by the climate ----
    const double alpine = smoothstep(330, 560, h);
    double sum = 0;
    for (int b = 0; b < PB_COUNT; b++) {
        if (b == PB_MOUNTAINS) continue;
        const double d2 = sqr(m.wet - kLowAt[b][0]) + sqr(m.heat - kLowAt[b][1]);
        const double q = d2 + 0.004, q2 = q * q;
        m.w[b] = P.bias[b] / (q2 * q2 * q);
        sum += m.w[b];
    }
    for (int b = 0; b < PB_COUNT; b++) m.w[b] = b == PB_MOUNTAINS ? alpine : m.w[b] / sum * (1 - alpine);
    m.rug = mk;
    // The desert's own land, where it is (low, away from the ranges): dunes in sand basins; badlands as a plateau cut by canyons.
    const double low = 1 - smoothstep(0.15, 0.55, mk);
    if (m.w[PB_DUNES] > 0.01) {
        const double u = (x * P.windX + z * P.windZ) / 52.0 + 2.2 * fbm(P.s[S_DUNE], x * 0.0035, z * 0.0035, 2);
        const double t = u - std::floor(u);
        const double prof = smoothstep(0.0, 0.74, t) * (1 - smoothstep(0.74, 1.0, t));
        const double amp = 4 + 9 * fbm(P.s[S_DUNE2], x * 0.002, z * 0.002, 2);
        const double basin = cont * 0.5 + 25 + 18 * hill + amp * prof;
        h += (basin - h) * smoothstep(0.2, 0.6, m.w[PB_DUNES]) * low;
    }
    if (m.w[PB_BADLANDS] > 0.01) {
        const double plateau = cont * 0.5 + 150 + 70 * hill;
        const double c = fbm(P.s[S_MESA], x * 0.0016 / S + 7.0, z * 0.0016 / S - 2.0, 3) - 0.5;  // canyons where it crosses 0
        const double c2 = fbm(P.s[S_MESA] + 9, x * 0.0045 / S - 3.0, z * 0.0045 / S + 5.0, 2) - 0.5;  // side canyons
        const double cut = 125 * (1 - smoothstep(0.012, 0.13, std::fabs(c))) + 55 * (1 - smoothstep(0.01, 0.085, std::fabs(c2)));
        double q = plateau - cut + 20 * (fbm(P.s[S_TERR], x * 0.006, z * 0.006, 2) - 0.5);
        const double step = 9.0, f = q / step, fl = std::floor(f);
        q = step * (fl + smoothstep(0.5, 1.0, f - fl));  // (benches with steep risers: continuous)
        h += (q - h) * smoothstep(0.25, 0.6, m.w[PB_BADLANDS]) * low;
    }
    m.h = h;
    // Water lies at the drain: the broad swell, raised in the ranges (their rivers run in high valleys), always under the land round.
    m.drain = cont * 0.5 + 8 + mk * 200;
    const double lowLand = cont + 45 + mk * 260 + 0.5 * foot;
    m.creekLevel = m.drain + std::max(0.0, 0.6 * (lowLand - m.drain));
    return m;
}

// The drain alone (macro's flow direction asks for it either side): the same as land()'s.
double ProcWorld::drainAt(double x, double z) const {
    const Params& P = *params_;
    const double S = P.scale;
    const double cont = 120 * (fbm(P.s[S_CONT], x * 0.00012 / S, z * 0.00012 / S, 4) - 0.5);
    const double bwx = 2500 * (fbm(P.s[S_WARP1] + 5, x * 0.00008, z * 0.00008, 2) - 0.5), bwz = 2500 * (fbm(P.s[S_WARP2] + 5, x * 0.00008, z * 0.00008, 2) - 0.5);
    const double belt = fbm(P.s[S_RANGE], (x + bwx) * 0.00011 / S, (z + bwz) * 0.00011 / S, 3);
    const double mk = smoothstep(0.40 - 0.06 * P.relief, 0.60, belt);
    return cont * 0.5 + 8 + mk * 200;
}

Macro ProcWorld::macro(double x, double z) const { return macroAt(x, z, true); }
Macro ProcWorld::macroAt(double x, double z, bool flow) const {
    const Params& P = *params_;
    const double S = P.scale;
    Macro m = land(x, z);

    // Rivers and creeks: where a warped field crosses 0.5. The distance to the channel is the field's offset over its slope.
    const double wx = 260 * (fbm(P.s[S_WARP1], x * 0.0016, z * 0.0016, 2) - 0.5), wz = 260 * (fbm(P.s[S_WARP2], x * 0.0016, z * 0.0016, 2) - 0.5);
    auto field = [&](uint64_t s, double f, double px, double pz) { return fbm(s, (px + wx) * f, (pz + wz) * f, 3); };
    // (Far from the channel that's a poor guess, changing many metres a metre where the field's slope does: the valley's walls,
    // hundreds of metres out, go by the field's offset over its usual slope instead, which changes smoothly.)
    // (The rule, procrules.h kWater: no rings and no hairpins. A channel is a contour of the field, and a contour closes on itself
    // round the field's bumps: there it bends tightly all the way round. Where its radius of curvature, from the field's second
    // differences over D m, is under rMin, the channel fades out: no water, no valley.)
    auto channel = [&](uint64_t s, double f, double rMin, double& sdist, double& vdist, double& tx, double& tz, double& keep) {
        const double r0 = field(s, f, x, z), rx = field(s, f, x + 2, z), rz = field(s, f, x, z + 2);
        const double gx = (rx - r0) / 2, gz = (rz - r0) / 2, g = std::sqrt(gx * gx + gz * gz);
        sdist = (r0 - 0.5) / std::max(g, 1e-6);
        vdist = std::fabs(r0 - 0.5) / f;  // (near the channel, under the bank's smoothstep either way)
        tx = g > 1e-9 ? -gz / g : 1, tz = g > 1e-9 ? gx / g : 0;
        keep = 1;
        const double taper = 1 - smoothstep(1.5 * rMin, 2.5 * rMin, std::fabs(sdist));  // (far out a bend doesn't matter: spared the cost)
        if (taper > 0) {
            const double D = 0.12 * rMin;
            const double px = field(s, f, x + D, z), mx = field(s, f, x - D, z), pz = field(s, f, x, z + D), mz = field(s, f, x, z - D);
            const double pp = field(s, f, x + D, z + D), pm = field(s, f, x + D, z - D), mp = field(s, f, x - D, z + D), mm = field(s, f, x - D, z - D);
            const double fx = (px - mx) / (2 * D), fz = (pz - mz) / (2 * D), fxx = (px - 2 * r0 + mx) / (D * D), fzz = (pz - 2 * r0 + mz) / (D * D);
            const double fxz = (pp - pm - mp + mm) / (4 * D * D), g2 = fx * fx + fz * fz;
            const double k = std::fabs(fxx * fz * fz - 2 * fxz * fx * fz + fzz * fx * fx) / std::max(g2 * std::sqrt(g2), 1e-30);  // (1/m)
            keep = 1 - (1 - smoothstep(rMin, 2 * rMin, 1 / std::max(k, 1e-9))) * taper;
        }
    };
    double tx, tz, ctx2, ctz, rKeep, cKeep;
    channel(P.s[S_RIVER], 0.00045 / S, kWater.riverBendMin, m.riverS, m.riverV, tx, tz, rKeep);
    channel(P.s[S_CREEK], 0.0012 / S, kWater.creekBendMin, m.creekS, m.creekV, ctx2, ctz, cKeep);
    // (Downhill: the way the land's broad swell falls along the channel.)
    if (!flow) tx = tz = ctx2 = ctz = 0;
    const double dgx = !flow ? 0 : drainAt(x + 24, z) - drainAt(x - 24, z), dgz = !flow ? 0 : drainAt(x, z + 24) - drainAt(x, z - 24);
    // (A creek runs down its own level, not the river's.)
    const bool nearCreek = flow && std::fabs(m.creekS) < 60;  // (the way it runs only matters in the water)
    const double cgx = !nearCreek ? 0 : land(x + 24, z).creekLevel - land(x - 24, z).creekLevel, cgz = !nearCreek ? 0 : land(x, z + 24).creekLevel - land(x, z - 24).creekLevel;
    const double sgn = tx * dgx + tz * dgz > 0 ? -1.0 : 1.0, csgn = ctx2 * cgx + ctz * cgz > 0 ? -1.0 : 1.0;
    m.flowX = tx * sgn, m.flowZ = tz * sgn, m.cflowX = ctx2 * csgn, m.cflowZ = ctz * csgn;
    m.riverOn = clamp01(1.25 - 1.7 * (m.w[PB_DUNES] + 0.75 * m.w[PB_BADLANDS]));          // (dry washes in the desert)
    m.creekOn = clamp01((m.wet - 0.3) * 3) * clamp01(1.2 - 1.6 * (m.w[PB_DUNES] + 0.6 * m.w[PB_BADLANDS]));
    m.creekOn *= 1 - smoothstep(6, 14, m.h - m.creekLevel);  // (creeks run in the low country: no gorges cut through the hills)
    // (The rule: water never stands above the land. Where the land isn't kWater.freeboard over a channel's level, the channel fades:
    // a river to a dry wash, a creek out. Nothing is ever filled to carry water.)
    m.riverOn *= smoothstep(0.0, kWater.freeboard, m.h - m.drain) * rKeep;
    m.creekOn *= smoothstep(0.0, kWater.freeboard, m.h - m.creekLevel) * cKeep;
    return m;
}

// How far out a valley's wall a point is: the channel's distance near it, the valley's own (smoother) further out, so the bank
// starts where the water ends.
static double wallDist(double d, double dv, double hw) { return d + (dv - d) * smoothstep(hw, hw + 40, d); }

double ProcWorld::riverHalfWidth(double x, double z) const { return 5 + 5 * vnoise(params_->s[S_RIVER] + 7, x * 0.003, z * 0.003); }
double ProcWorld::creekHalfWidth(double x, double z) const { return 1.4 + 1.3 * vnoise(params_->s[S_CREEK] + 7, x * 0.006, z * 0.006); }
double ProcWorld::riverDepth(double x, double z) const {
    const double q = vnoise(params_->s[S_RIVER] + 11, x * 0.006, z * 0.006);
    return 0.3 + 0.45 * smoothstep(0.22, 0.32, q) + 0.45 * smoothstep(0.74, 0.84, q);  // riffles 0.3, runs 0.75, pools 1.2
}

double ProcWorld::macroHeight(double x, double z) const { return land(x, z).h; }

// The ground as the chunks cut it, less the bumps and the trails: the valleys the rivers and creeks have carved. (What the trails
// are routed over, and the places sited on: a river in the ranges lies in a gorge hundreds of metres below the land either side.)
double ProcWorld::groundHeight(double x, double z) const { return groundHeight(x, z, macroAt(x, z, false)); }
double ProcWorld::groundHeight(double x, double z, const Macro& m) const {
    double h = m.h;
    {  // (every river's valley, wet or a dry wash: see makeChunk)
        const double d = std::fabs(m.riverS), dv = m.riverV, hw = riverHalfWidth(x, z), level = m.drain, bank = level + 0.3, wetK = smoothstep(0.3, 0.6, m.riverOn);
        const double V = std::min(kWater.riverValleyMax, 25 + kWater.bankBatter * std::max(0.0, h - level));
        const double wall = h > bank ? bank + (h - bank) * smoothstep(hw, hw + V, wallDist(d, dv, hw)) : h;
        const double wetNh = d < hw ? std::min(wall, level - riverDepth(x, z) * (1 - sqr(d / hw))) : wall;
        const double dryNh = wall - 0.8 * std::min(1.0, m.riverOn * 4) * (1 - smoothstep(hw * 0.5, hw + 8, d));
        h = dryNh + (wetNh - dryNh) * wetK;
    }
    const double cOn = m.creekOn * smoothstep(0.0, kWater.freeboard, h - m.creekLevel);  // (the river's valley cut under it: the creek ends above it)
    if (cOn > 0.02) {
        const double d = std::fabs(m.creekS), dv = m.creekV, hw = creekHalfWidth(x, z), level = m.creekLevel, bank = level + 0.2;
        const double V = std::min(kWater.creekValleyMax, 10 + kWater.bankBatter * std::max(0.0, h - level));
        const double nh = h > bank ? (d < hw ? level - 0.3 : bank + (h - bank) * smoothstep(hw, hw + V, wallDist(d, dv, hw))) : h;
        h += (nh - h) * std::min(1.0, cOn * 2);
    }
    return h;
}

int ProcWorld::biomeAt(double x, double z) const {
    const Macro m = macro(x, z);
    int best = 0;
    for (int b = 1; b < PB_COUNT; b++)
        if (m.w[b] > m.w[best]) best = b;
    return best;
}

double ProcWorld::remoteness(double gx, double gz) const {
    const double d = std::sqrt(gx * gx + gz * gz);
    return smoothstep(500, 8000, d);
}

std::string ProcWorld::regionKey(double gx, double gz) {
    const int64_t rx = floorDiv((int64_t)std::floor(gx), 2048), rz = floorDiv((int64_t)std::floor(gz), 2048);
    return "R." + std::to_string(rx) + "." + std::to_string(rz);
}

std::string ProcWorld::regionName(double gx, double gz) const {
    static const char* a[] = {"RED",    "WILLOW", "BEAR",  "COYOTE", "CEDAR",  "IRON",   "LONG",   "HIGH",  "LOST",   "STONE", "ASPEN",
                              "SILVER", "BLACK",  "EAGLE", "PINE",   "ELK",    "COPPER", "DRY",    "HOLLOW", "MULE",  "BADGER", "JUNIPER",
                              "CROW",   "SAGE",   "GRANITE", "RAVEN", "BUFFALO", "LONESOME", "SHADOW", "THUNDER", "BRUSHY"};
    static const char* b[] = {"BASIN", "FLATS", "RIDGE", "VALLEY", "MESA", "HILLS", "BREAKS", "PARK", "BENCH", "DRAW", "PLATEAU",
                              "COUNTRY", "HIGHLANDS", "BOTTOMS", "RANGE", "SPRINGS", "WASH"};
    const int64_t rx = floorDiv((int64_t)std::floor(gx), 2048), rz = floorDiv((int64_t)std::floor(gz), 2048);
    const int na = (int)(sizeof a / sizeof a[0]), nb = (int)(sizeof b / sizeof b[0]);
    const uint64_t h = hashIJ(seed_ ^ 0xBEEFull, rx, rz);
    return std::string(a[h % na]) + " " + b[(h >> 20) % nb];
}

std::string ProcWorld::saveLine(const World& w) {
    return "map=procedural seed=" + std::to_string(w.proc.seed) + " origin=" + std::to_string(w.proc.ox) + "," + std::to_string(w.proc.oz);
}

bool ProcWorld::useSaveLine(const std::string& line) {
    if (line.find("map=procedural") == std::string::npos) return false;
    const size_t s = line.find("seed=");
    if (s == std::string::npos) return false;
    const unsigned long long seed = std::strtoull(line.c_str() + s + 5, nullptr, 10);
    if (!seed) return false;
    World::procSeed = seed, World::procOriginSet = false;
    const size_t o = line.find("origin=");
    long long x = 0, z = 0;
    if (o != std::string::npos && std::sscanf(line.c_str() + o + 7, "%lld,%lld", &x, &z) == 2) World::procOriginSet = true, World::procOriginX = x, World::procOriginZ = z;
    return true;
}

void ProcWorld::snapOrigin(double gx, double gz, int64_t& ox, int64_t& oz) {
    ox = (int64_t)std::floor(gx / kChunk + 0.5) * kChunk;
    oz = (int64_t)std::floor(gz / kChunk + 0.5) * kChunk;
}

void ProcWorld::start(int64_t& x, int64_t& z, double& yaw) const {
    // The outpost nearest the world's origin: you start on its lot, beside the shed, facing the way it does.
    double best = 1e18;
    TrailNode at;
    for (int64_t i = -3; i <= 2; i++)
        for (int64_t j = -3; j <= 2; j++) {
            const TrailNode n = node(i, j);
            if (!n.outpost) continue;
            const double d = n.x * n.x + n.z * n.z;
            if (d < best) best = d, at = n;
        }
    yaw = nodeYaw(at);
    // (12 m to the side of the shed's open end: on the lot, clear of the posts. The yaw is one of four, so these are whole.)
    const double fx = std::cos(yaw), fz = -std::sin(yaw);
    x = std::llround(at.x + fz * 14), z = std::llround(at.z - fx * 14);
}

void ProcWorld::startOrigin(int64_t& ox, int64_t& oz) const {
    int64_t sx, sz;
    double yaw;
    start(sx, sz, yaw);
    snapOrigin((double)sx, (double)sz, ox, oz);
}

V3 ProcWorld::toGlobal(const World& w, const V3& p) { return V3(p.x + (double)w.proc.ox, p.y, p.z + (double)w.proc.oz); }
V3 ProcWorld::toLocal(const World& w, const V3& p) { return V3(p.x - (double)w.proc.ox, p.y, p.z - (double)w.proc.oz); }

// ---- A chunk ------------------------------------------------------------------------------------------------------------------------
uint64_t Chunk::hash() const {
    uint64_t hh = 1469598103934665603ull;
    auto bytes = [&](const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++) hh = (hh ^ c[i]) * 1099511628211ull;
    };
    auto vec = [&](const auto& v) {
        const uint64_t n = v.size();
        bytes(&n, sizeof n);
        if (n) bytes(v.data(), n * sizeof(v[0]));
    };
    bytes(&cx, sizeof cx), bytes(&cz, sizeof cz);
    vec(h), vec(surf), vec(paint), vec(water), vec(flowX), vec(flowZ), vec(roadLat), vec(roadAlong), vec(roadNear);
    for (const TreeSpec& t : trees) bytes(&t.x, sizeof(float) * 5), bytes(&t.kind, 1);
    for (const RockSpec& r : rocks) bytes(&r.x, sizeof(float) * 7), bytes(&r.seed, 4);
    for (const BushSpec& b : bushes) bytes(&b.x, sizeof(float) * 4), bytes(&b.habitat, 2);
    for (const LogSpec& l : logs) bytes(&l.ax, sizeof(float) * 5);
    return hh;
}

// ---- The trail network ----------------------------------------------------------------------------------------------------------
namespace {
struct NodeKey {
    uint64_t seed;
    int64_t i, j;
    bool operator==(const NodeKey& o) const { return seed == o.seed && i == o.i && j == o.j; }
};
struct NodeHash {
    size_t operator()(const NodeKey& k) const { return (size_t)hashIJ(k.seed ^ 0x4E0DEull, k.i, k.j); }
};
std::mutex nodeMutex;
std::unordered_map<NodeKey, TrailNode, NodeHash> nodeCache;
}  // namespace

TrailNode ProcWorld::node(int64_t i, int64_t j) const {
    const NodeKey key{seed_, i, j};
    {
        std::lock_guard<std::mutex> lock(nodeMutex);
        auto it = nodeCache.find(key);
        if (it != nodeCache.end()) return it->second;
    }
    const TrailNode n = makeNode(i, j);
    std::lock_guard<std::mutex> lock(nodeMutex);
    if (nodeCache.size() > 40000) nodeCache.clear();
    nodeCache.emplace(key, n);
    return n;
}

TrailNode ProcWorld::makeNode(int64_t i, int64_t j) const {
    const Params& P = *params_;
    TrailNode n;
    n.i = i, n.j = j;
    // (Outposts on every third cell each way, at the seed's phase: one every ~1.5 km. A fuel stop in each block too.)
    const int64_t pi = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 0, 0) % 3), pj = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 1, 1) % 3);
    const int64_t mi = ((i - pi) % 3 + 3) % 3, mj = ((j - pj) % 3 + 3) % 3;
    n.outpost = mi == 0 && mj == 0;
    n.fuel = mi == 1 && mj == 2;
    // A spot in the cell to build on: clear of rivers and creeks, the gentlest ground of a dozen tries, low rather than high (in the
    // ranges: the valleys and passes, where the roads can get to). Its flat ground is the land's level there.
    double best = -1e18;
    for (int k = 0; k < 12; k++) {
        const double x = (double)(i * kCell) + 90 + (kCell - 180) * hash01(P.s[S_DITHER] + 3 + k, i, j);
        const double z = (double)(j * kCell) + 90 + (kCell - 180) * hash01(P.s[S_DITHER] + 13 + k, i, j);
        const Macro m = macro(x, z);
        const double g = groundHeight(x, z, m), e = groundHeight(x + 20, z), w = groundHeight(x - 20, z), s = groundHeight(x, z + 20), nn = groundHeight(x, z - 20);
        const double slope = (std::fabs(e - w) + std::fabs(s - nn)) / 40 + (std::fabs(e + w - 2 * g) + std::fabs(s + nn - 2 * g)) / 40;
        const double clear = std::min(std::fabs(m.riverS) - riverHalfWidth(x, z) - (m.riverOn > 0 ? 45 : 15), std::fabs(m.creekS) - 20);
        const double score = std::min(clear, 30.0) - 160 * slope - 0.12 * g;
        if (score > best) best = score, n.x = x, n.z = z, n.h = (2 * g + e + w + s + nn) / 6;
    }
    return n;
}

namespace {
struct EdgeKey {
    uint64_t seed;
    int64_t i, j;
    int dir;
    bool operator==(const EdgeKey& o) const { return seed == o.seed && i == o.i && j == o.j && dir == o.dir; }
};
struct EdgeHash {
    size_t operator()(const EdgeKey& k) const { return (size_t)hashIJ(k.seed + (uint64_t)k.dir * 0x9E37ull, k.i, k.j); }
};
std::mutex edgeMutex;
std::unordered_map<EdgeKey, std::shared_ptr<const TrailEdge>, EdgeHash> edgeCache;
}  // namespace

std::shared_ptr<const TrailEdge> ProcWorld::edge(int64_t i, int64_t j, int dir) const {
    const EdgeKey key{seed_, i, j, dir};
    {
        std::lock_guard<std::mutex> lock(edgeMutex);
        auto it = edgeCache.find(key);
        if (it != edgeCache.end()) return it->second;
    }
    const Params& P = *params_;
    auto e = std::make_shared<TrailEdge>();
    const int64_t pi = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 0, 0) % 3), pj = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 1, 1) % 3);
    const double roll = hash01(P.s[S_DITHER] + 101 + (uint64_t)dir, i, j);
    // Roads: the rows and columns of cells the outposts are on. Trails: the rest, a few left out; diagonals in some cells.
    if (dir == 0) e->road = ((j - pj) % 3 + 3) % 3 == 0;
    else if (dir == 1) e->road = ((i - pi) % 3 + 3) % 3 == 0;
    e->on = e->road || (dir < 2 && roll > 0.18);  // (no diagonals for now: they crossed other trails at other heights)
    if (e->on) {
        const TrailNode a = node(i, j), b = node(i + (dir == 1 ? 0 : 1), j + (dir == 0 ? 0 : 1));
        e->ax = a.x, e->az = a.z;
        // Its rating (and so the steepest it may climb): roads are gentle; trails by the dice, rougher in the ranges.
        const double mtn = 0.5 * (land(a.x, a.z).rug + land(b.x, b.z).rug);
        const double rr = hash01(P.s[S_DITHER] + 401 + (uint64_t)dir, i, j);
        e->rating = e->road ? 0 : mtn > 0.4 ? (rr < 0.35 ? 1 : 2) : (rr < 0.45 ? 0 : rr < 0.85 ? 1 : 2);
        const double gmax = e->road ? 0.12 : e->rating == 0 ? 0.15 : e->rating == 1 ? 0.22 : 0.32;
        // The way: the cheapest path over a 16 m grid of the land round the two nodes (A*), each step dearer the steeper it climbs and
        // far dearer past the rating's grade, so it zigzags up a slope it can't take straight (switchbacks); a river crossed once, at a ford.
        constexpr double C = 16;
        const double W = e->road ? 420 : 260;  // (how far it may wander: a road's gentler grade wants longer switchbacks; makeChunk's gather allows ~460)
        const double gx0 = std::floor((std::min(a.x, b.x) - W) / C) * C, gz0 = std::floor((std::min(a.z, b.z) - W) / C) * C;
        const int NX = (int)((std::max(a.x, b.x) + W - gx0) / C) + 1, NZ = (int)((std::max(a.z, b.z) + W - gz0) / C) + 1;
        std::vector<float> H((size_t)NX * NZ);
        std::vector<uint8_t> wet((size_t)NX * NZ);
        for (int u = 0; u < NX; u++)
            for (int v = 0; v < NZ; v++) {
                const double x = gx0 + u * C, z = gz0 + v * C;
                const Macro m = macro(x, z);
                H[(size_t)u * NZ + v] = (float)groundHeight(x, z, m);
                wet[(size_t)u * NZ + v] = (m.riverOn > 0.35 && std::fabs(m.riverS) < riverHalfWidth(x, z) + 8) || (m.creekOn > 0.35 && std::fabs(m.creekS) < creekHalfWidth(x, z) + 4);
            }
        auto cellOf = [&](double x, double z) {
            const int u = std::min(NX - 1, std::max(0, (int)std::lround((x - gx0) / C))), v = std::min(NZ - 1, std::max(0, (int)std::lround((z - gz0) / C)));
            return u * NZ + v;
        };
        const int start = cellOf(a.x, a.z), goal = cellOf(b.x, b.z);
        std::vector<double> cost((size_t)NX * NZ, 1e300);
        std::vector<int> from((size_t)NX * NZ, -1);
        std::vector<uint8_t> done((size_t)NX * NZ, 0);
        using QE = std::pair<double, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
        auto heur = [&](int c) { return std::sqrt(sqr((c / NZ - goal / NZ) * C) + sqr((c % NZ - goal % NZ) * C)); };
        cost[start] = 0, open.push({heur(start), start});
        static const int kStep[16][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
                                         {2, 1}, {2, -1}, {-2, 1}, {-2, -1}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
        while (!open.empty()) {
            const int c = open.top().second;
            open.pop();
            if (done[c]) continue;
            done[c] = 1;
            if (c == goal) break;
            const int u = c / NZ, v = c % NZ;
            for (const auto& st : kStep) {
                const int nu = u + st[0], nv = v + st[1];
                if (nu < 0 || nv < 0 || nu >= NX || nv >= NZ) continue;
                const int nc = nu * NZ + nv;
                if (done[nc]) continue;
                const double L = C * std::sqrt((double)(st[0] * st[0] + st[1] * st[1]));
                const double g = std::fabs(H[nc] - H[c]) / L, q = g / gmax;
                double step = L * (1 + 3 * q * q);
                if (q > 1) step += L * 40 * (q - 1);  // (past its grade: only where there's no other way)
                if (wet[nc] && !wet[c]) step += 120;  // (into the water: a crossing costs, so it crosses once)
                const double nc2 = cost[c] + step;
                if (nc2 < cost[nc]) cost[nc] = nc2, from[nc] = c, open.push({nc2 + heur(nc), nc});
            }
        }
        // The path, node to node, smoothed (corners cut, three times), then a point every ~2 m.
        std::vector<double> qx, qz;
        for (int c = goal; c >= 0; c = from[c]) {
            qx.push_back(gx0 + (c / NZ) * C - a.x), qz.push_back(gz0 + (c % NZ) * C - a.z);
            if (c == start) break;
        }
        std::reverse(qx.begin(), qx.end()), std::reverse(qz.begin(), qz.end());
        qx.front() = 0, qz.front() = 0, qx.back() = b.x - a.x, qz.back() = b.z - a.z;
        for (int pass = 0; pass < 3; pass++) {
            std::vector<double> sx{qx.front()}, sz{qz.front()};
            for (size_t k = 0; k + 1 < qx.size(); k++) {
                sx.push_back(0.75 * qx[k] + 0.25 * qx[k + 1]), sz.push_back(0.75 * qz[k] + 0.25 * qz[k + 1]);
                sx.push_back(0.25 * qx[k] + 0.75 * qx[k + 1]), sz.push_back(0.25 * qz[k] + 0.75 * qz[k + 1]);
            }
            sx.push_back(qx.back()), sz.push_back(qz.back());
            qx.swap(sx), qz.swap(sz);
        }
        std::vector<double> px{qx[0]}, pz{qz[0]};
        for (size_t k = 0; k + 1 < qx.size(); k++) {
            const double sl = std::sqrt(sqr(qx[k + 1] - qx[k]) + sqr(qz[k + 1] - qz[k]));
            const int steps = std::max(1, (int)std::lround(sl / 2.0));
            for (int t = 1; t <= steps; t++) px.push_back(qx[k] + (qx[k + 1] - qx[k]) * t / steps), pz.push_back(qz[k] + (qz[k + 1] - qz[k]) * t / steps);
        }
        const int n = (int)px.size();
        // The tread's heights: the land along it smoothed, held to the grade (cut and fill), its ends at the nodes' flat ground, down
        // into the water where it fords.
        auto Hat = [&](double lx, double lz) {  // (bilinear on the grid)
            const double fu = std::min((double)NX - 1.001, std::max(0.0, (lx + a.x - gx0) / C)), fv = std::min((double)NZ - 1.001, std::max(0.0, (lz + a.z - gz0) / C));
            const int u = (int)fu, v = (int)fv;
            const double tu = fu - u, tv = fv - v;
            auto at = [&](int p, int q) { return (double)H[(size_t)p * NZ + q]; };
            return (at(u, v) * (1 - tu) + at(u + 1, v) * tu) * (1 - tv) + (at(u, v + 1) * (1 - tu) + at(u + 1, v + 1) * tu) * tv;
        };
        std::vector<double> raw(n), y(n), along(n, 0.0);
        std::vector<char> ford(n, 0);
        for (int k = 0; k < n; k++) {
            raw[k] = Hat(px[k], pz[k]);
            if (k) along[k] = along[k - 1] + std::sqrt(sqr(px[k] - px[k - 1]) + sqr(pz[k] - pz[k - 1]));
        }
        const int smoothR = e->road ? 8 : 4;  // (a road's profile smoothed over ~32 m; a trail's follows the land closer, ~16 m)
        for (int k = 0; k < n; k++) {
            double s = 0;
            int cnt = 0;
            for (int q = std::max(0, k - smoothR); q <= std::min(n - 1, k + smoothR); q++) s += raw[q], cnt++;
            y[k] = s / cnt;
        }
        for (int k = 0; k < n; k += 4) {  // (fords: where it's in a river or a creek, the water's level less a wading depth)
            const double gx = a.x + px[k], gz = a.z + pz[k];
            if (!wet[cellOf(gx, gz)]) continue;
            const Macro m = macro(gx, gz);
            double fy = 1e9;
            if (m.riverOn > 0.5 && std::fabs(m.riverS) < riverHalfWidth(gx, gz) + 2) fy = m.drain - 0.45;
            else if (m.creekOn > 0.5 && std::fabs(m.creekS) < creekHalfWidth(gx, gz) + 1) fy = m.creekLevel - 0.3;
            if (fy > 1e8) continue;
            for (int q = std::max(0, k - 2); q <= std::min(n - 1, k + 2); q++) ford[q] = 1, y[q] = fy;
            if (!e->ford) e->ford = true, e->fordX = gx, e->fordZ = gz;
        }
        y[0] = a.h, y[n - 1] = b.h;
        // (Where a creek runs into a river, the two fords lie side by side at their own levels: the higher lets go, graded down.)
        for (int pass = 0; pass < 2; pass++)
            for (int k = 1; k < n; k++)
                if (ford[k] && ford[k - 1] && std::fabs(y[k] - y[k - 1]) > gmax * (along[k] - along[k - 1]) + 1e-9) ford[y[k] > y[k - 1] ? k : k - 1] = 0;
        auto pinned = [&](int k) { return k == 0 || k == n - 1 || ford[k]; };
        // Held to the grade; where that would leave it far above or below the land (a climb the way round couldn't spread out: a
        // gorge's wall), steeper instead, a step at a time, rather than on a tall bank or down a deep cut.
        // (A ford the grade can't reach from the next one, a creek's just up from where it joins a river: let go, graded.)
        // (The rule, roadrules.h kRoadKinds: a road's cut or fill under kFillMax[road] of the land, a trail's a couple of metres: a trail
        // follows the land and climbs steeper rather than ride a causeway. The grade is let out a step at a time to its kind's steepest.)
        const std::vector<double> lay = y;
        const RoadRule& rule = roadRule(e->road ? RK_GRAVEL : e->rating >= 2 ? RK_TWOTRACK : RK_TRAIL);
        double g = gmax;
        for (int tries = 0, guard = 0; tries < 12; tries++, g = std::min(g * 1.25, rule.gradeMax)) {
            y = lay;
            for (int pass = 0; pass < 10; pass++)
                for (int dirn = 0; dirn < 2; dirn++)
                    for (int q = 1; q < n; q++) {
                        const int k = dirn ? n - 1 - q : q, prev = dirn ? k + 1 : k - 1;
                        if (pinned(k)) continue;
                        const double run = std::fabs(along[k] - along[prev]);
                        y[k] = std::min(std::max(y[k], y[prev] - g * run), y[prev] + g * run);
                    }
            bool let = false;
            for (int k = 1; k + 1 < n && guard < 64; k++)
                if (ford[k] && (std::fabs(y[k] - y[k - 1]) > g * (along[k] - along[k - 1]) + 1e-6 || std::fabs(y[k + 1] - y[k]) > g * (along[k + 1] - along[k]) + 1e-6))
                    ford[k] = 0, let = true, guard++;
            if (let) {
                tries--, g /= 1.35;
                continue;
            }
            double dev = 0;
            bool kept = true;  // (held to the grade everywhere: its two ends, too far apart in height for the way between, are not)
            for (int k = 0; k < n; k++) {
                if (!pinned(k)) dev = std::max(dev, std::fabs(y[k] - raw[k]));  // (against the land itself, not the smoothed profile)
                if (k && std::fabs(y[k] - y[k - 1]) > g * (along[k] - along[k - 1]) + 1e-6) kept = false;
            }
            if ((kept && dev < rule.fillMax) || g >= rule.gradeMax) break;
        }
        // (Its corners rounded, the crests and dips where the grade turns: a running mean, which keeps it to the grade.)
        for (int pass = 0; pass < 2; pass++) {
            const std::vector<double> y0 = y;
            for (int k = 0; k < n; k++) {
                if (pinned(k)) continue;
                double sum = 0;
                int cnt = 0;
                for (int q = std::max(0, k - 3); q <= std::min(n - 1, k + 3); q++) sum += y0[q], cnt++;
                y[k] = sum / cnt;
            }
        }
        e->x.resize(n), e->z.resize(n), e->y.resize(n), e->along.resize(n), e->fill.resize(n);
        double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
        for (int k = 0; k < n; k++) {
            e->x[k] = (float)px[k], e->z[k] = (float)pz[k], e->y[k] = (float)y[k], e->along[k] = (float)along[k], e->fill[k] = (float)(y[k] - raw[k]);
            x0 = std::min(x0, px[k]), x1 = std::max(x1, px[k]), z0 = std::min(z0, pz[k]), z1 = std::max(z1, pz[k]);
        }
        const double margin = kTreadReach;  // (the widest earthworks)
        e->x0 = a.x + x0 - margin, e->x1 = a.x + x1 + margin, e->z0 = a.z + z0 - margin, e->z1 = a.z + z1 + margin;
    }
    std::lock_guard<std::mutex> lock(edgeMutex);
    if (edgeCache.size() > 40000) edgeCache.clear();
    return edgeCache.emplace(key, std::move(e)).first->second;
}

std::shared_ptr<Chunk> ProcWorld::makeChunk(int64_t cx, int64_t cz) const {
    const auto t0 = std::chrono::steady_clock::now();
    const Params& P = *params_;
    auto c = std::make_shared<Chunk>();
    c->cx = cx, c->cz = cz;
    const int64_t gx0 = cx * kChunk, gz0 = cz * kChunk;
    // The smooth fields every 4 m, from 4 m before the chunk to 4 m past it (global lattice points: the same in every chunk).
    constexpr int L = 19, A = 66;  // lattice points a side; cells a side with a one-cell apron (for slopes)
    std::vector<Macro> M((size_t)L * L);
    for (int a = 0; a < L; a++)
        for (int b = 0; b < L; b++) M[(size_t)a * L + b] = macro((double)(gx0 - 4 + 4 * a), (double)(gz0 - 4 + 4 * b));
    auto blend = [&](int i, int j, auto get) {
        const int ii = i + 4, jj = j + 4, la = ii >> 2, lb = jj >> 2;
        const double fu = (ii & 3) * 0.25, fv = (jj & 3) * 0.25;
        const Macro &m00 = M[(size_t)la * L + lb], &m10 = M[(size_t)(la + 1) * L + lb];
        const Macro &m01 = M[(size_t)la * L + lb + 1], &m11 = M[(size_t)(la + 1) * L + lb + 1];
        return (get(m00) * (1 - fu) + get(m10) * fu) * (1 - fv) + (get(m01) * (1 - fu) + get(m11) * fu) * fv;
    };
    // The trails that come near this chunk.
    std::vector<std::shared_ptr<const TrailEdge>> edges;
    {
        const int64_t ci = floorDiv(gx0 + 32, kCell), cj = floorDiv(gz0 + 32, kCell);
        for (int64_t i = ci - 2; i <= ci + 1; i++)  // (an edge runs east or north of its cell's node, wandering up to ~260 m off)
            for (int64_t j = cj - 2; j <= cj + 1; j++)
                for (int dir = 0; dir < 3; dir++) {
                    auto e = edge(i, j, dir);
                    if (e->on && e->x1 >= (double)gx0 - 2 && e->x0 <= (double)gx0 + 66 && e->z1 >= (double)gz0 - 2 && e->z0 <= (double)gz0 + 66) edges.push_back(e);
                }
    }
    // (Each one's stretches that come within reach of this chunk: the rest needn't be looked at cell by cell.)
    std::vector<std::vector<uint32_t>> reach(edges.size());
    for (size_t q = 0; q < edges.size(); q++) {
        const TrailEdge& e = *edges[q];
        const double rx0 = (double)gx0 - 2 - kTreadReach - e.ax, rx1 = (double)gx0 + 66 + kTreadReach - e.ax;
        const double rz0 = (double)gz0 - 2 - kTreadReach - e.az, rz1 = (double)gz0 + 66 + kTreadReach - e.az;
        for (size_t k = 0; k + 1 < e.x.size(); k++)
            if (std::max(e.x[k], e.x[k + 1]) >= rx0 && std::min(e.x[k], e.x[k + 1]) <= rx1 && std::max(e.z[k], e.z[k + 1]) >= rz0 && std::min(e.z[k], e.z[k + 1]) <= rz1)
                reach[q].push_back((uint32_t)k);
    }
    // The places' flat ground (their plazas) near this chunk.
    struct Plaza {
        double x, z, h, r;
        bool lot;  // an outpost's gravel lot
    };
    std::vector<Plaza> plazas;
    {
        const int64_t ci = floorDiv(gx0 + 32, kCell), cj = floorDiv(gz0 + 32, kCell);
        for (int64_t i = ci - 1; i <= ci + 1; i++)
            for (int64_t j = cj - 1; j <= cj + 1; j++) {
                const TrailNode n = node(i, j);
                const double r = plazaRadius(n);
                if (n.x + r + 80 < (double)gx0 - 1 || n.x - r - 80 > (double)gx0 + 65 || n.z + r + 80 < (double)gz0 - 1 || n.z - r - 80 > (double)gz0 + 65) continue;
                plazas.push_back({n.x, n.z, n.h, r, n.outpost || n.fuel});
            }
    }
    // Every cell (and its apron): the land, rivers and creeks cut into it, the trails laid on it.
    struct Cell {
        double h, water = -1e9, fx = 0, fz = 0;
        uint8_t dom = 0;
        uint8_t keep = 0;      // nothing grows or lies here (trails, water, their banks)
        int8_t bed = 0;        // 1 river or creek bed, 2 a dry wash, 3 trail tread, 4 road tread
        float lat = 0, along = 0;
        bool roadNear = false;
    };
    std::vector<Cell> C((size_t)A * A);
    static const double kBump[PB_COUNT] = {0.85, 1.0, 0.9, 1.4, 0.25};
    for (int i = -1; i <= 64; i++)
        for (int j = -1; j <= 64; j++) {
            Cell& cl = C[(size_t)(i + 1) * A + (j + 1)];
            const double x = (double)(gx0 + i), z = (double)(gz0 + j);
            double w[PB_COUNT], bump = 0;
            for (int b = 0; b < PB_COUNT; b++) w[b] = blend(i, j, [b](const Macro& m) { return m.w[b]; }), bump += w[b] * kBump[b];
            double h = blend(i, j, [](const Macro& m) { return m.h; });
            // Uneven underfoot: bumps a few metres across.
            const double bumpH = bump * (0.9 * (vnoise(P.s[S_BUMP1], x * 0.09 + 5.0, z * 0.09 + 1.0) - 0.5) + 0.35 * (vnoise(P.s[S_BUMP2], x * 0.31, z * 0.27) - 0.5));
            h += bumpH;
            // A river: its channel cut down to the drain level, banks easing back up to the land (wider the higher the land stands).
            const double rOn = blend(i, j, [](const Macro& m) { return m.riverOn; });
            {  // (Every river's valley is cut, wet or a dry wash: one that faded with the wet would leave its walls standing.)
                const double d = std::fabs(blend(i, j, [](const Macro& m) { return m.riverS; })), dv = blend(i, j, [](const Macro& m) { return m.riverV; });
                const double hw = riverHalfWidth(x, z), level = blend(i, j, [](const Macro& m) { return m.drain; });
                const double wetK = smoothstep(0.3, 0.6, rOn);  // (a river fades into a dry wash in the desert: carved as one, blended)
                const double V = std::min(kWater.riverValleyMax, 25 + kWater.bankBatter * std::max(0.0, h - level));
                if (wallDist(d, dv, hw) < hw + 10 + V) {  // (all the way out: cut short, the walls would end in a step)
                    const bool wet = rOn > 0.5;
                    const double depth = riverDepth(x, z);
                    const double bank = level + 0.3;
                    const double wall = h > bank ? bank + (h - bank) * smoothstep(hw, hw + V, wallDist(d, dv, hw)) : h;  // (never filled)
                    const double wetNh = d < hw ? std::min(wall, level - depth * (1 - sqr(d / hw))) : wall;
                    const double dryNh = (h > bank ? wall : h) - 0.8 * std::min(1.0, rOn * 4) * (1 - smoothstep(hw * 0.5, hw + 8, d));
                    h = dryNh + (wetNh - dryNh) * wetK;
                    if (d < hw + 0.5 && rOn > 0.02) cl.bed = wet ? 1 : 2;
                    if (d < hw + 3 && rOn > 0.02) cl.keep = 1;
                    if (wet && d < hw + 1.5 && h < level - 0.02) {
                        cl.water = level;
                        const double sp = 0.4 + 0.9 * (1 - smoothstep(0.3, 1.2, depth));  // (fast over the riffles, slow in the pools)
                        cl.fx = blend(i, j, [](const Macro& m) { return m.flowX; }) * sp, cl.fz = blend(i, j, [](const Macro& m) { return m.flowZ; }) * sp;
                    }
                }
            }
            // A creek: the same, small, at its own level.
            const double cLevel = blend(i, j, [](const Macro& m) { return m.creekLevel; });
            const double hNoBump = h - bumpH;  // (the land as groundHeight has it here: carved, no bumps)
            const double cOn = blend(i, j, [](const Macro& m) { return m.creekOn; }) * smoothstep(0.0, kWater.freeboard, hNoBump - cLevel);
            if (cOn > 0.02) {
                const double d = std::fabs(blend(i, j, [](const Macro& m) { return m.creekS; })), dv = blend(i, j, [](const Macro& m) { return m.creekV; });
                const double hw = creekHalfWidth(x, z), level = cLevel;
                const double V = std::min(kWater.creekValleyMax, 10 + kWater.bankBatter * std::max(0.0, h - level));
                if (wallDist(d, dv, hw) < hw + V && cl.water < -1e8) {
                    const bool wet = cOn > 0.5;
                    const double depth = 0.25 + 0.2 * vnoise(P.s[S_CREEK] + 3, x * 0.01, z * 0.01);
                    const double bank = level + 0.2;
                    const double nh = h > bank ? (d < hw ? level - depth * (1 - sqr(d / hw)) : bank + (h - bank) * smoothstep(hw, hw + V, wallDist(d, dv, hw))) : h;  // (never filled)
                    h = h + (nh - h) * std::min(1.0, cOn * 2);
                    if (d < hw + 0.4 && !cl.bed) cl.bed = wet ? 1 : 2;
                    if (d < hw + 2) cl.keep = 1;
                    if (wet && d < hw + 1 && h < level - 0.02) {
                        cl.water = level;
                        cl.fx = blend(i, j, [](const Macro& m) { return m.cflowX; }) * 0.7, cl.fz = blend(i, j, [](const Macro& m) { return m.cflowZ; }) * 0.7;
                    }
                }
            }
            // The trails: each one's tread, the land easing into it over a few metres (as wide as the tread's cut or fill is deep, about:
            // ~1:1.3, not a wall). Each trail's height here is its nearest stretches' (a switchback's two legs: the land goes the way of
            // each as near as it is; the nearest's alone would step between them), and so between trails.
            double best = 1e9, bestLat = 0, bestAlong = 0, bestBw = 0;
            const TrailEdge* bestE = nullptr;
            const double h0 = h;
            struct Near { double w, y, ed, bw; bool road; };
            struct Seg { double ed, y, fill; };
            static thread_local std::vector<Seg> seg;
            seg.clear();
            Near near[12];
            int nNear = 0;
            for (size_t eq = 0; eq < edges.size(); eq++) {
                const TrailEdge& e = *edges[eq];
                if (x < e.x0 || x > e.x1 || z < e.z0 || z > e.z1) continue;
                const double lx = x - e.ax, lz = z - e.az, base = e.road ? 10.0 : 8.0;
                double wMax = 0, edMin = 1e9, s8 = 0, s8y = 0, s8f = 0;
                for (const uint32_t k : reach[eq]) {
                    const double axp = e.x[k], azp = e.z[k], dx = e.x[k + 1] - axp, dz = e.z[k + 1] - azp, L2 = dx * dx + dz * dz;
                    if (L2 < 1e-9) continue;
                    // (Quick reject: further than the widest earthworks from this stretch's ends.)
                    if (std::fabs(lx - axp) > kTreadReach && std::fabs(lx - e.x[k + 1]) > kTreadReach) continue;
                    if (std::fabs(lz - azp) > kTreadReach && std::fabs(lz - e.z[k + 1]) > kTreadReach) continue;
                    const double t = std::min(1.0, std::max(0.0, ((lx - axp) * dx + (lz - azp) * dz) / L2));
                    const double qx = axp + dx * t - lx, qz = azp + dz * t - lz, dd = qx * qx + qz * qz;
                    const double fill = e.fill[k] + (e.fill[k + 1] - e.fill[k]) * t;
                    const double bw = std::min(kTreadReach - 8, base + kBatter * std::fabs(fill)), ed = std::sqrt(dd) - e.half();
                    if (dd < best) {
                        best = dd, bestE = &e, bestBw = bw;
                        bestAlong = e.along[k] + (e.along[k + 1] - e.along[k]) * t;
                        bestLat = (dx * (lz - azp) - dz * (lx - axp)) / std::sqrt(L2);
                    }
                    if (ed >= kTreadReach - 8) continue;
                    edMin = std::min(edMin, ed);
                    if (ed < bw) wMax = std::max(wMax, ed < 1.0 ? 1.0 : smoothstep(bw, 1.0, ed));
                    seg.push_back({ed, e.y[k] + (e.y[k + 1] - e.y[k]) * t, fill});
                }
                // (Which stretches it's near: those within a few metres of the nearest, so the choice moves smoothly between two legs.)
                for (const Seg& g : seg) {
                    const double r = (g.ed - edMin) / 3, wk = 1 / (1 + r * r * r * r);
                    s8 += wk, s8y += wk * g.y, s8f += wk * g.fill;
                }
                seg.clear();
                if (s8 <= 0 || nNear == 12) continue;
                near[nNear++] = {wMax, s8y / s8, edMin, std::min(kTreadReach - 8, base + kBatter * std::fabs(s8f / s8)), e.road};
            }
            double sumW = 0, sumWY = 0, open = 1;  // (open: how much of the land is left, each tread taking its share of what the others left)
            for (int q = 0; q < nNear; q++) {
                // (The nearest tread has its way: another's share falls away within a few metres of being further, or as many as it
                // is above or below it: two trails that cross at different heights meet in a ramp, not a step.)
                double r = 0;
                for (int o = 0; o < nNear; o++)
                    if (o != q) r = std::max(r, (near[q].ed - near[o].ed) / (3 + 1.5 * std::fabs(near[q].y - near[o].y)));
                const double wq = near[q].w / (1 + r * r * r * r);
                sumW += wq, sumWY += wq * near[q].y, open *= 1 - std::min(1.0, wq);
            }
            if (sumW > 0) h = h0 + (sumWY / sumW - h0) * (1 - open);  // (two half-weights make three quarters, not a whole: no step where earthworks overlap)
            if (bestE) {
                const double e = std::sqrt(best) - bestE->half();
                if (e < bestBw) {
                    cl.lat = (float)bestLat, cl.along = (float)bestAlong;
                    if (e < 0.5) cl.bed = bestE->road ? 4 : 3, cl.roadNear = true;
                    if (e < 3) cl.keep = 1;
                    if (cl.water > -1e8 && h >= cl.water - 0.05) cl.water = -1e9, cl.fx = cl.fz = 0;  // (the bank it climbs out on)
                }
            }
            for (const Plaza& pz : plazas) {  // (level ground for the place's buildings, easing into the land)
                const double d = std::sqrt(sqr(x - pz.x) + sqr(z - pz.z));
                const double bw = std::min(80.0, std::max(14.0, kBatter * std::fabs(pz.h - h)));  // (as the trails' earthworks)
                if (d > pz.r + bw) continue;
                h += (pz.h - h) * smoothstep(pz.r + bw, pz.r, d);
                if (d < pz.r + 3) cl.keep = 1, cl.water = -1e9, cl.fx = cl.fz = 0;
                if (pz.lot && d < pz.r - 1) cl.bed = 4;
            }
            cl.h = h;
            int b1 = 0, b2 = -1;
            for (int b = 1; b < PB_COUNT; b++)
                if (w[b] > w[b1]) b1 = b;
            for (int b = 0; b < PB_COUNT; b++)
                if (b != b1 && (b2 < 0 || w[b] > w[b2])) b2 = b;
            const double t = w[b2] / (w[b1] + w[b2]);  // 0..0.5
            const double dith = 0.55 + 0.7 * (vnoise(P.s[S_DITHER], x * 0.06, z * 0.06) - 0.5) + 0.3 * (hash01(P.s[S_DITHER], gx0 + i, gz0 + j) - 0.5);
            cl.dom = (uint8_t)(2 * t > dith ? b2 : b1);
        }
    auto CC = [&](int i, int j) -> const Cell& { return C[(size_t)(i + 1) * A + (j + 1)]; };
    auto slopeAt = [&](int i, int j) { return std::sqrt(sqr(CC(i + 1, j).h - CC(i - 1, j).h) + sqr(CC(i, j + 1).h - CC(i, j - 1).h)) * 0.5; };
    // The ground, and the water and trail layers.
    const int N = kChunk * kChunk;
    c->h.resize(N), c->surf.resize(N), c->paint.resize(N);
    bool anyWater = false, anyRoad = false;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++) {
            const Cell& cl = CC(i, j);
            anyWater |= cl.water > -1e8, anyRoad |= cl.roadNear || cl.lat != 0;
        }
    if (anyWater) c->water.assign(N, -1e9f), c->flowX.assign(N, 0), c->flowZ.assign(N, 0);
    if (anyRoad) c->roadLat.assign(N, 0), c->roadAlong.assign(N, 0), c->roadNear.assign(N, 0);
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++) {
            const int id = i * kChunk + j;
            const Cell& cl = CC(i, j);
            const double x = (double)(gx0 + i), z = (double)(gz0 + j), h = cl.h, slope = slopeAt(i, j);
            c->h[id] = (float)h;
            if (anyWater && cl.water > -1e8) c->water[id] = (float)cl.water, c->flowX[id] = (float)cl.fx, c->flowZ[id] = (float)cl.fz;
            if (anyRoad) c->roadLat[id] = cl.lat, c->roadAlong[id] = cl.along, c->roadNear[id] = cl.roadNear;
            uint8_t s = SURF_GRASS, pt = PAINT_NONE;
            const double n1 = fbm(P.s[S_GROUND1], x * 0.035, z * 0.035, 2), n2 = vnoise(P.s[S_GROUND2], x * 0.11, z * 0.11);
            switch (cl.dom) {
                case PB_MEADOW:
                    if (slope > 0.9) s = SURF_ROCK;
                    else if (n1 > 0.8 && n2 > 0.55) s = SURF_DIRT, pt = PAINT_TRAIL;  // a bare patch now and then
                    break;
                case PB_FOREST:
                    if (slope > 0.8) s = SURF_ROCK;
                    break;
                case PB_BADLANDS:
                    if (slope > 0.42 + 0.1 * n2) s = SURF_ROCK, pt = PAINT_REDROCK;
                    else if (n1 < 0.3 && slope < 0.15) s = SURF_SAND, pt = PAINT_SAND;  // sand in the flats
                    else s = SURF_DIRT, pt = PAINT_DESERT;
                    break;
                case PB_MOUNTAINS: {
                    const double snowline = 760 + 90 * (fbm(P.s[S_SNOW], x * 0.006, z * 0.006, 3) - 0.5);
                    if (h > snowline && slope < 0.75) s = SURF_SNOW, pt = PAINT_SNOW;
                    else if (slope > 0.7) s = SURF_ROCK;
                    else if ((slope > 0.4 && n1 > 0.55) || (h > snowline - 15 && n2 > 0.55)) s = SURF_SCREE, pt = PAINT_SCREE;
                    break;
                }
                case PB_DUNES:
                    if (n1 < 0.25 && slope < 0.08) s = SURF_DIRT, pt = PAINT_DESERT;  // hardpan between the dunes
                    else s = SURF_SAND, pt = PAINT_SAND;
                    break;
            }
            // Beds and treads over whatever the land is.
            if (cl.bed == 1) s = SURF_GRAVEL, pt = PAINT_GRAVEL;
            else if (cl.bed == 2) s = SURF_SAND, pt = PAINT_SAND;
            else if (cl.bed == 4) s = SURF_GRAVEL, pt = PAINT_GRAVEL;
            else if (cl.bed == 3) {
                if (pt == PAINT_SNOW) s = SURF_SNOW;  // (packed snow up high)
                else if (cl.water > -1e8 || (anyWater && c->water[id] > -1e8)) s = SURF_GRAVEL, pt = PAINT_GRAVEL;  // (a ford)
                else s = SURF_DIRT, pt = PAINT_TRAIL;
            }
            c->surf[id] = s, c->paint[id] = pt;
        }

    // ---- Growth: trees, rocks, logs, bushes (the chunk's own random numbers) --------------------------------------------------
    Rand rr(seed_, cx, cz, 0x7EE5ull);
    Macro mid = M[(size_t)9 * L + 9];  // (the chunk's middle)
    const double* w = mid.w;
    const double grove = smoothstep(0.36, 0.62, fbm(P.s[S_GROVE], (gx0 + 32) * 0.004, (gz0 + 32) * 0.004, 3));
    auto cellOf = [&](double fx, double fz, int& i, int& j) {
        i = std::min(63, std::max(0, (int)fx)), j = std::min(63, std::max(0, (int)fz));
        return i * kChunk + j;
    };
    auto open = [&](int i, int j) { return !CC(i, j).keep && CC(i, j).water < -1e8; };
    // Trees.
    const double perChunk = w[PB_FOREST] * 40 + w[PB_MEADOW] * 9 * (0.15 + 0.85 * grove) + w[PB_BADLANDS] * 2.5 + w[PB_MOUNTAINS] * 11 + w[PB_DUNES] * 0.3;
    const int wantTrees = (int)(perChunk * (w[PB_FOREST] > 0.5 ? 0.55 + 0.45 * grove : 1.0) + rr());
    std::vector<std::pair<float, float>> placed;
    for (int k = 0; k < wantTrees * 2 && (int)c->trees.size() < wantTrees; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double h = c->h[id], slope = slopeAt(i, j);
        const int b = CC(i, j).dom;
        if (!open(i, j) || slope > 0.7 || c->surf[id] == SURF_SAND || c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || c->paint[id] == PAINT_TRAIL) continue;
        if (h > 540 + 40 * rr()) continue;  // the treeline
        bool spaced = true;
        for (const auto& q : placed) spaced &= sqr(q.first - fx) + sqr(q.second - fz) > 3.5 * 3.5;
        if (!spaced) continue;
        TreeSpec t;
        t.x = (float)fx, t.z = (float)fz;
        const double q = rr();
        if (b == PB_BADLANDS || b == PB_DUNES) t.kind = q < 0.55 ? 2 : 1;
        else if (b == PB_FOREST) t.kind = q < 0.06 ? 2 : (q < 0.25 + 0.4 * smoothstep(0.6, 0.9, mid.wet) ? 1 : 0);
        else if (b == PB_MEADOW) t.kind = q < 0.04 ? 2 : (q < 0.55 ? 1 : 0);
        else t.kind = q < 0.1 ? 2 : 0;
        double th = t.kind == 0 ? 9 + rr() * 10 : t.kind == 1 ? 7 + rr() * 7 : 5 + rr() * 6;
        if (b == PB_BADLANDS || b == PB_DUNES) th *= 0.6;
        th *= 1 - 0.4 * smoothstep(380, 560, h);  // (stunted up high)
        t.height = (float)th;
        t.radius = (float)(th * (t.kind == 0 ? 0.2 + rr() * 0.06 : 0.26 + rr() * 0.08));
        t.tint = (float)(0.8 + rr() * 0.4);
        placed.push_back({(float)fx, (float)fz});
        c->trees.push_back(t);
    }
    // Rocks: more on the steep and in the badlands and mountains; now and then a big outcrop; stones along the riffles' edges.
    const double rockRate = 1.2 * w[PB_MEADOW] + 2.0 * w[PB_FOREST] + 6.0 * w[PB_BADLANDS] + 6.5 * w[PB_MOUNTAINS] + 0.4 * w[PB_DUNES];
    const int rockTries = (int)(rockRate * 1.6 + rr());
    for (int k = 0; k < rockTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double slope = slopeAt(i, j);
        if (!open(i, j)) continue;
        if (c->surf[id] == SURF_SAND && rr() < 0.85) continue;
        if (rr() > 0.25 + 0.75 * smoothstep(0.15, 0.6, slope)) continue;
        const bool big = slope > 0.4 && rr() < 0.15;
        const double rad = big ? 1.4 + 2.2 * rr() : 0.35 + rr() * rr() * 0.9, flat = 0.5 + 0.4 * rr();
        RockSpec r;
        r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * flat), r.rz = (float)(rad * (0.7 + 0.3 * rr()));
        r.top = (float)(rad * flat * (0.5 + 0.5 * rr()));
        r.tint = (float)(0.85 + 0.25 * rr());
        r.seed = (uint32_t)(hashIJ(seed_ ^ 0x50C4ull, cx, cz) >> 8) + (uint32_t)k * 7919u;
        c->rocks.push_back(r);
    }
    for (int k = 0; k < 10 && anyWater; k++) {  // (the river's edges: a few stones where the water's shallow)
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        cellOf(fx, fz, i, j);
        const Cell& cl = CC(i, j);
        if (cl.water < -1e8 || cl.water - cl.h > 0.35 || cl.roadNear || cl.lat != 0) continue;
        const double rad = 0.3 + 0.35 * rr();
        RockSpec r;
        r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * 0.6), r.rz = (float)(rad * 0.85), r.top = (float)(rad * 0.45);
        r.tint = (float)(0.8 + 0.2 * rr());
        r.seed = (uint32_t)(hashIJ(seed_ ^ 0x51C4ull, cx, cz) >> 8) + (uint32_t)k * 6007u;
        c->rocks.push_back(r);
    }
    // A fallen log in the woods.
    if (w[PB_FOREST] + w[PB_MEADOW] * grove > 0.4 && rr() < 0.6 * (w[PB_FOREST] + 0.5 * w[PB_MEADOW])) {
        const double fx = 8 + 48 * rr(), fz = 8 + 48 * rr(), L2 = 2 + 3 * rr(), r = 0.18 + 0.18 * rr();
        double dx = rr() - 0.5, dz = rr() - 0.5;
        const double dl = std::sqrt(dx * dx + dz * dz) + 1e-9;
        dx /= dl, dz /= dl;
        int i, j, i2, j2, i3, j3;
        const int id = cellOf(fx, fz, i, j);
        cellOf(fx - dx * L2, fz - dz * L2, i2, j2), cellOf(fx + dx * L2, fz + dz * L2, i3, j3);
        if (c->surf[id] == SURF_GRASS && slopeAt(i, j) < 0.4 && open(i, j) && open(i2, j2) && open(i3, j3))
            c->logs.push_back({(float)(fx - dx * L2), (float)(fz - dz * L2), (float)(fx + dx * L2), (float)(fz + dz * L2), (float)r});
    }
    // Bushes: what grows where (reeds by the water).
    const int bushTries = (int)(14 * (0.3 + 0.7 * std::max(grove, w[PB_BADLANDS] + w[PB_DUNES] * 0.3)) + rr()) + (anyWater ? 10 : 0);
    for (int k = 0; k < bushTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const int b = CC(i, j).dom;
        if (c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || CC(i, j).water > -1e8 || CC(i, j).roadNear) continue;
        if (b == PB_DUNES && rr() < 0.8) continue;
        // (Near the water: reeds and wet ground; elsewhere what the biome grows.)
        bool wetSide = false;
        for (int a = -3; a <= 3 && !wetSide; a += 3)
            for (int q = -3; q <= 3 && !wetSide; q += 3) wetSide = i + a >= -1 && i + a <= 64 && j + q >= -1 && j + q <= 64 && CC(i + a, j + q).water > -1e8;
        if (CC(i, j).keep && !wetSide) continue;
        BushSpec s;
        s.x = (float)fx, s.z = (float)fz, s.u = (float)rr(), s.tint = (float)(0.75 + 0.45 * rr());
        s.habitat = (uint8_t)(wetSide ? HAB_WETLAND : b == PB_BADLANDS || b == PB_DUNES ? HAB_DESERT : b == PB_MOUNTAINS ? HAB_UPLAND : b == PB_FOREST ? HAB_FOREST : HAB_MEADOW);
        s.edge = grove > 0.4;
        c->bushes.push_back(s);
    }
    c->genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return c;
}

// ---- Places -----------------------------------------------------------------------------------------------------------------------
namespace {
const char* intern(const std::string& s) {  // (names the game keeps const char* to: stations, regions)
    static std::mutex m;
    static std::set<std::string> pool;
    std::lock_guard<std::mutex> lock(m);
    return pool.insert(s).first->c_str();
}
const char* const kAdj[] = {"RED",     "WILLOW",  "BEAR",     "COYOTE",   "CEDAR",   "IRON",    "LONG",    "HIGH",    "LOST",    "STONE",   "ASPEN",
                            "SILVER",  "BLACK",   "EAGLE",    "PINE",     "ELK",     "COPPER",  "DRY",     "HOLLOW",  "MULE",    "BADGER",  "JUNIPER",
                            "CROW",    "SAGE",    "GRANITE",  "RAVEN",    "BUFFALO", "LONESOME", "SHADOW", "THUNDER", "BRUSHY",  "COTTONWOOD", "TIMBER",
                            "SPRUCE",  "HAWK",    "OWL",      "LITTLE",   "BIG",     "ANTELOPE", "WOLF",   "FOX",     "CANYON",  "GOLD",    "BITTER",
                            "SWEETWATER", "CLEAR", "MUDDY",   "ROCKY",    "WINDY",   "COLD",    "WARM",    "TWIN",    "BROKEN",  "HIDDEN",  "OLD",
                            "WHISKEY", "POWDER",  "LODGEPOLE", "BLUEBIRD", "MAGPIE", "OTTER"};
const char* const kNoun[] = {"FLATS", "CREEK", "RIDGE", "HOLLOW", "MESA",  "PASS",   "SPRINGS", "BASIN", "GULCH", "MEADOW", "PEAK",   "BUTTE",
                             "DRAW",  "BENCH", "POINT", "KNOB",   "PARK",  "FORK",   "BEND",    "WASH",  "LAKE",  "BLUFF",  "CANYON", "GAP",
                             "TOP",   "RUN",   "ROCKS", "PINES",  "GROVE", "HILL",   "BOTTOM",  "SADDLE", "DOME", "CROSSING", "VALLEY", "TANK",
                             "SEEP",  "SLIDE", "SHELF", "LEDGE",  "COVE",  "NOTCH",  "BRAKE",   "GLADE", "COULEE", "BREAKS", "SLOPE",  "WELLS",
                             "CORNER", "LOOP", "CUT",   "HEIGHTS", "MOUND", "PRAIRIE", "SLOUGH", "BOG",  "CAMP",  "TRACE",  "VISTA"};
constexpr int kNA = (int)(sizeof kAdj / sizeof kAdj[0]), kNN = (int)(sizeof kNoun / sizeof kNoun[0]);
inline int64_t pmod(int64_t a, int64_t m) { return ((a % m) + m) % m; }
const char* kindTag(int kind) {
    switch (kind) {
        case PK_RANGER_STATION: return "OUTPOST";
        case PK_FUEL_STOP: return "FUEL";
        case PK_CABIN: return "CABIN";
        case PK_OLD_CAMP: return "CAMP";
        case PK_LOOKOUT: return "LOOKOUT";
        case PK_MINE: return "MINE";
        case PK_RUIN: return "RUINS";
        default: return "PLACE";
    }
}
// Eight ways out from a node (for where its cache lies): unit vectors, no trig.
const double kOut8[8][2] = {{1, 0}, {0.7071067811865476, 0.7071067811865476}, {0, 1}, {-0.7071067811865476, 0.7071067811865476},
                            {-1, 0}, {-0.7071067811865476, -0.7071067811865476}, {0, -1}, {0.7071067811865476, -0.7071067811865476}};
}  // namespace

std::string ProcWorld::placeName(int64_t i, int64_t j, int salt) const {
    // (The cell, folded 59 x 59 (30 km a side), numbered, then scrambled one to one over the word pairs (61 x 59 of them): no two
    // cells within 30 km share a name. The seed shifts the scramble.)
    const int64_t c = pmod(i, 59) * 59 + pmod(j, 59);
    const int64_t k = pmod(c * 1009 + (int64_t)(seed_ % 3599) + salt * 1201, (int64_t)kNA * kNN);
    return std::string(kAdj[k / kNN]) + " " + kNoun[k % kNN];
}

int ProcWorld::nodeKind(const TrailNode& n) const {
    if (n.outpost) return PK_RANGER_STATION;
    if (n.fuel) return PK_FUEL_STOP;
    const double r = hash01(params_->s[S_DITHER] + 501, n.i, n.j);
    const int b = biomeAt(n.x, n.z);
    if (r < 0.26) return PK_CABIN;
    if (r < 0.44) return PK_OLD_CAMP;
    if (r < 0.58) return PK_LOOKOUT;
    if (r < 0.68) return b == PB_BADLANDS || b == PB_MOUNTAINS ? PK_MINE : PK_RUIN;
    if (r < 0.76) return PK_RUIN;
    return -1;
}

double ProcWorld::plazaRadius(const TrailNode& n) const {
    switch (nodeKind(n)) {
        case PK_RANGER_STATION: return 30;
        case PK_FUEL_STOP: return 16;
        case PK_CABIN: return 14;
        case PK_OLD_CAMP: return 13;
        case PK_MINE: return 14;
        case PK_LOOKOUT: return 9;
        case PK_RUIN: return 10;
        default: return 5;
    }
}

double ProcWorld::nodeYaw(const TrailNode& n) const { return M_PI / 2 * (double)(hashIJ(params_->s[S_DITHER] + 601, n.i, n.j) % 4); }

void ProcWorld::nodePlaces(int64_t i, int64_t j, std::vector<MapPlace>& out) const {
    const TrailNode n = node(i, j);
    const int kind = nodeKind(n);
    const std::string base = "PW." + std::to_string(i) + "." + std::to_string(j);
    auto add = [&](const std::string& what, const std::string& name, int k, double x, double z, double radius, double pad, unsigned svc) {
        MapPlace m;
        m.id = base + "." + what, m.name = name, m.kind = k, m.map = MAP_PROCEDURAL, m.p = V3(x, 0, z), m.yaw = nodeYaw(n), m.radius = radius, m.pad = pad;
        m.services = svc;
        m.region = intern(regionName(x, z));
        out.push_back(m);
        return &out.back();
    };
    const std::string nm = placeName(i, j, 0);
    if (kind == PK_RANGER_STATION)
        add("OUTPOST", nm + " OUTPOST", kind, n.x, n.z, 50, plazaRadius(n), SVC_REPAIR | SVC_SHOP | SVC_FUEL | SVC_JOBS | SVC_TOW | SVC_SAVE | SVC_PARTS);
    else if (kind == PK_FUEL_STOP) add("FUEL", nm + " FUEL", kind, n.x, n.z, 35, plazaRadius(n), SVC_FUEL | SVC_PARTS | SVC_TOW);
    else if (kind == PK_CABIN) add("CABIN", nm + " CABIN", kind, n.x, n.z, 35, plazaRadius(n), SVC_TOW | SVC_SAVE);
    else if (kind == PK_OLD_CAMP) add("CAMP", nm + " CAMP", kind, n.x, n.z, 35, plazaRadius(n), SVC_TOW | SVC_SAVE);
    else if (kind >= 0) add(kindTag(kind), nm + " " + kindTag(kind), kind, n.x, n.z, 40, plazaRadius(n), 0);
    // A supply cache off the trail near about every second node (richer further out).
    const uint64_t hc = hashIJ(params_->s[S_DITHER] + 701, i, j);
    if (hc % 100 < 55) {
        const double* d = kOut8[(hc >> 8) % 8];
        const double dist = 45 + (double)((hc >> 16) % 40);
        const double cx = n.x + d[0] * dist, cz = n.z + d[1] * dist;
        const Macro m = macro(cx, cz);
        const bool wet = (m.riverOn > 0.3 && std::fabs(m.riverS) < riverHalfWidth(cx, cz) + 6) || (m.creekOn > 0.3 && std::fabs(m.creekS) < creekHalfWidth(cx, cz) + 4);
        if (!wet) add("CACHE", nm + " CACHE", PK_CACHE, cx, cz, 12, 0, 0)->reward = 150 + (int)(450 * remoteness(cx, cz)) / 10 * 10;
    }
    // Now and then an abandoned vehicle by a node, for the career to recover.
    if (hc % 1000 >= 960) {
        static const char* kVeh[][2] = {{"blazer", "FLAT TIRES, A DEAD BATTERY"}, {"4runner", "WATER IN THE ENGINE"}, {"civic", "OUT OF FUEL"}, {"utv", "A BROKEN AXLE"}};
        const auto& v = kVeh[(hc >> 24) % 4];
        const double* d = kOut8[((hc >> 8) + 4) % 8];
        MapPlace* p = add("VEHICLE", nm + " WRECK", PK_ABANDONED_VEHICLE, n.x + d[0] * 30, n.z + d[1] * 30, 35, 0, 0);
        p->vehicle = v[0], p->condition = v[1];
    }
    // The fords on the trails out of it (east and north): river crossings.
    for (int dir = 0; dir < 2; dir++) {
        const auto e = edge(i, j, dir);
        if (e->on && e->ford) add(dir ? "FORD.N" : "FORD.E", nm + (dir ? " CROSSING" : " FORD"), PK_RIVER_CROSSING, e->fordX, e->fordZ, 45, 0, 0);
    }
}

namespace {
std::mutex activeMutex;
std::vector<MapPlace> active;
uint64_t activeSeed_ = 0;
int64_t activeOx = 0, activeOz = 0;
}  // namespace

std::vector<MapPlace> windowPlaces(const World& w) {
    std::vector<MapPlace> out;
    if (w.map != MAP_PROCEDURAL) return out;
    const ProcWorld& G = ProcWorld::forSeed(w.proc.seed);
    const double half = w.halfSize(), in = half - 100;
    const int64_t i0 = floorDiv(w.proc.ox - (int64_t)half, kCell), i1 = floorDiv(w.proc.ox + (int64_t)half, kCell);
    const int64_t j0 = floorDiv(w.proc.oz - (int64_t)half, kCell), j1 = floorDiv(w.proc.oz + (int64_t)half, kCell);
    std::vector<MapPlace> all;
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++) G.nodePlaces(i, j, all);
    for (MapPlace& p : all) {
        p.p = V3(p.p.x - (double)w.proc.ox, 0, p.p.z - (double)w.proc.oz);
        if (std::fabs(p.p.x) < in && std::fabs(p.p.z) < in) out.push_back(std::move(p));
    }
    return out;
}

void setActiveWindow(const World& w) { setActiveWindow(w, windowPlaces(w)); }

void setActiveWindow(const World& w, std::vector<MapPlace>&& v) {
    std::lock_guard<std::mutex> lock(activeMutex);
    active = std::move(v);
    activeSeed_ = w.proc.seed, activeOx = w.proc.ox, activeOz = w.proc.oz;
}
const std::vector<MapPlace>& activePlaces() { return active; }
uint64_t activeSeed() { return activeSeed_; }
const char* activeRegionAt(double x, double z) {
    if (!activeSeed_) return "";
    return intern(ProcWorld::forSeed(activeSeed_).regionName(x + (double)activeOx, z + (double)activeOz));
}

// The buildings and things at a window's places (World::buildProcedural): the outposts' sheds and stations, the cabins, camps,
// lookouts, mines and ruins, a sign at every junction. All from the place (where it is in the world), so every window has them alike.
void buildPlaces(World& w, const std::vector<MapPlace>& places) {
    const V3 brown(0.4, 0.3, 0.2);
    auto grounded = [&](V3 q) {
        q.y = w.height(q.x, q.z);
        return q;
    };
    auto rel = [&](const V3& o, double yaw, double ahead, double left) {
        return grounded(V3(o.x + std::cos(yaw) * ahead + std::sin(yaw) * left, 0, o.z - std::sin(yaw) * ahead + std::cos(yaw) * left));
    };
    using Set = std::vector<std::pair<std::string, double>>;
    auto build = [&](const char* id, const V3& at, double yaw, const V3& half, Set set = {}, int variant = -1) {
        const size_t from = w.boxes.size();
        w.addBox(V3(at.x, at.y + half.y, at.z), half, yaw, 0, 0, brown);
        w.standIn(from, id, at, yaw, std::move(set));
        if (variant >= 0 && !w.models.empty() && w.models.back().id == id) w.models.back().variant = variant;
    };
    auto decor = [&](const char* id, const V3& at, double yaw, Set set = {}) { w.placeModel(id, at, yaw, std::move(set)); };
    auto fixture = [&](PropKind k, const V3& at, double yaw) {
        Prop p{k, grounded(at), V3(), yaw};
        w.fixtures.push_back(p);
    };
    auto table = [&](const V3& at, double yaw) {
        fixture(PROP_TABLE, at, yaw);
        w.addBox(V3(at.x, at.y + 0.4, at.z), V3(0.95, 0.4, 0.8), yaw, 0, 0, brown);
        w.boxes.back().hidden = true;
    };
    auto sign = [&](const V3& at, double yaw, PropKind kind) {
        Prop sg{kind, grounded(at), V3(), yaw};
        sg.col[0] = 0.1f, sg.col[1] = 0.55f, sg.col[2] = 0.2f;
        w.fixtures.push_back(sg);
        w.poles.push_back({sg.a.x, sg.a.z, sg.a.y - 0.4, sg.a.y + 2.0, 0.07});
    };
    for (const MapPlace& pl : places) {
        const V3 P = grounded(pl.p);
        const double yw = pl.yaw;
        switch (pl.kind) {
            case PK_RANGER_STATION:
                w.addStation(P, yw, intern(pl.name));
                build("ranger_station", rel(P, yw, 0, 17), yw + M_PI / 2, V3(5.2, 1.8, 3.4));
                build("fuel_pump", rel(P, yw, 6, -14), yw - M_PI / 2, V3(3.0, 0.6, 1.8), {}, 1);
                build("woodpile", rel(P, yw, -10, 22), yw, V3(1.4, 0.5, 0.5));
                build("outhouse", rel(P, yw, 12, 23), yw + M_PI / 2, V3(0.6, 1.1, 0.6));
                table(rel(P, yw, -4, 22), yw);
                sign(rel(P, yw, 16, 8), yw, PROP_MAPBOARD);
                break;
            case PK_FUEL_STOP: {
                const V3 I = rel(P, yw, 0, 6);
                build("fuel_pump", I, yw, V3(2.6, 0.5, 0.6), {}, 1);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) {
                        const V3 q = rel(I, yw, a * 2.7, b * 1.4);
                        w.poles.push_back({q.x, q.z, q.y - 0.3, q.y + 3.7, 0.1});
                    }
                decor("fuel_pump", rel(P, yw, 3, -7), yw + 0.3, {});
                fixture(PROP_CAIRN, rel(P, yw, -7, -6), 0.4);
                break;
            }
            case PK_CABIN:
                build("cabin", rel(P, yw, -6, 0), yw, V3(2.7, 1.9, 2.2), {}, (int)(pl.name.size() % 2));
                build("woodpile", rel(P, yw, 0, 4.6), yw, V3(1.4, 0.5, 0.5));
                build("outhouse", rel(P, yw, -8, -7), yw + M_PI, V3(0.6, 1.1, 0.6));
                table(rel(P, yw, 5, 4), yw);
                break;
            case PK_OLD_CAMP:
                decor("tent", rel(P, yw, 0, -4), yw + 0.3);
                decor("tent", rel(P, yw, 2, 4), yw - 0.5);
                decor("campfire", rel(P, yw, 6, 0), 0);
                table(rel(P, yw, 8, 5), yw);
                break;
            case PK_LOOKOUT:
                fixture(PROP_CAIRN, rel(P, yw, 3, 0), 0.7);
                table(rel(P, yw, -3, 2), yw);
                break;
            case PK_MINE:
                build("mine_portal", rel(P, yw, -9, 0), yw, V3(1.6, 1.6, 2.2));
                decor("ore_cart", rel(P, yw, -3, 1.5), yw);
                break;
            case PK_RUIN:
                decor("wreck_pickup", rel(P, yw, 0, 3), yw + 0.6);
                fixture(PROP_CAIRN, rel(P, yw, -4, -3), 1.1);
                break;
            default: break;
        }
        if (pl.kind != PK_CACHE && pl.kind != PK_RIVER_CROSSING && pl.kind != PK_ABANDONED_VEHICLE) sign(rel(P, yw, 3, -(pl.pad + 2)), yw, PROP_SIGN);
    }
}

// ---- The chunk cache ----------------------------------------------------------------------------------------------------------------
namespace {
struct CacheKey {
    uint64_t seed;
    int64_t cx, cz;
    bool operator==(const CacheKey& o) const { return seed == o.seed && cx == o.cx && cz == o.cz; }
};
struct KeyHash {
    size_t operator()(const CacheKey& k) const { return (size_t)hashIJ(k.seed, k.cx, k.cz); }
};
struct Cache {
    std::mutex m;
    std::list<CacheKey> order;  // most recently used first
    std::unordered_map<CacheKey, std::pair<std::shared_ptr<const Chunk>, std::list<CacheKey>::iterator>, KeyHash> map;
    size_t cap = 6000;  // (about 2.5 windows)
};
Cache& cache() {
    static Cache c;
    return c;
}
}  // namespace

std::shared_ptr<const Chunk> ProcWorld::chunk(int64_t cx, int64_t cz) const {
    Cache& C = cache();
    const CacheKey key{seed_, cx, cz};
    {
        std::lock_guard<std::mutex> lock(C.m);
        auto it = C.map.find(key);
        if (it != C.map.end()) {
            C.order.splice(C.order.begin(), C.order, it->second.second);
            return it->second.first;
        }
    }
    std::shared_ptr<const Chunk> made = makeChunk(cx, cz);
    std::lock_guard<std::mutex> lock(C.m);
    auto it = C.map.find(key);
    if (it != C.map.end()) return it->second.first;  // (another thread made it meanwhile: the same bytes)
    C.order.push_front(key);
    C.map.emplace(key, std::make_pair(made, C.order.begin()));
    while (C.map.size() > C.cap) {
        C.map.erase(C.order.back());
        C.order.pop_back();
    }
    return made;
}

size_t cachedChunks() {
    Cache& C = cache();
    std::lock_guard<std::mutex> lock(C.m);
    return C.map.size();
}
void clearChunkCache() {
    Cache& C = cache();
    std::lock_guard<std::mutex> lock(C.m);
    C.map.clear(), C.order.clear();
}

double farHeight(const World& w, double x, double z) {
    const double half = w.halfSize();
    if (std::fabs(x) <= half && std::fabs(z) <= half) return w.height(x, z);
    return ProcWorld::forSeed(w.proc.seed).groundHeight(x + (double)w.proc.ox, z + (double)w.proc.oz);  // (its valleys cut, as the window's)
}

void shiftSim(Sim& sim, const V3& d) {
    for (Car& c : sim.cars) {
        c.translate(d);
        c.spawnPos += d, c.jackFoot += d;  // (not shoveP: an offset, not a place)
        for (V3& g : c.glassBursts) g += d;
    }
    for (LooseWheel& lw : sim.looseWheels) lw.p += d;
    for (Rope& r : sim.ropes) {
        if (r.a.kind != ANCHOR_CAR) r.a.p += d;
        if (r.b.kind != ANCHOR_CAR) r.b.p += d;
    }
    for (SetAnchor& sa : sim.setAnchors) sa.p += d;
    for (Sim::DroppedPart& dp : sim.droppedParts) dp.p += d;
    sim.handLine.hand += d;
    sim.handCable.hand += d;
    for (V3& p : sim.lodWatch) p += d;
}

}  // namespace proc

// ================================================================================================================================
// A window of the endless world, round World::procOriginX/Z (or the seed's start), from the chunk cache.
void World::buildProcedural() {
    using namespace proc;
    map = MAP_PROCEDURAL;
    crashProps = false;
    const ProcWorld& G = ProcWorld::forSeed(procSeed);
    proc.seed = procSeed;
    int64_t sx, sz;
    double syaw;
    G.start(sx, sz, syaw);
    proc.startX = sx, proc.startZ = sz;
    if (procOriginSet) ProcWorld::snapOrigin((double)procOriginX, (double)procOriginZ, proc.ox, proc.oz);
    else G.startOrigin(proc.ox, proc.oz);
    half = kWindowHalf;
    hN = (int)(2 * half / kRes) + 1;
    const int N = hN * hN;
    hgrid.assign(N, 0.0f);
    surf.assign(N, SURF_GRASS);
    paint.assign(N, PAINT_NONE);
    roadLat.reset(hN, 0);
    roadAlong.reset(hN, 0);
    roadNear.reset(hN, 0);
    waterY.reset(hN, -1e9f);
    flowX.reset(hN, 0);
    flowZ.reset(hN, 0);
    // The chunks: 48 a side inside, and a 49th row and column for the window's last cells.
    const int64_t gx0 = proc.ox - (int64_t)half, gz0 = proc.oz - (int64_t)half;
    const int64_t cx0 = floorDiv(gx0, kChunk), cz0 = floorDiv(gz0, kChunk);
    const int cn = (hN + kChunk - 1) / kChunk;
    std::vector<std::shared_ptr<const Chunk>> ch((size_t)cn * cn);
    {
        loadprof::Scope lp("proc.chunks");
        parallelRows(cn * cn, [&](int k) { ch[k] = G.chunk(cx0 + k / cn, cz0 + k % cn); }, 4);
    }
    // The land, chunk by chunk.
    parallelRows(cn, [&](int ci) {
        for (int cj = 0; cj < cn; cj++) {
            const Chunk& c = *ch[(size_t)ci * cn + cj];
            for (int a = 0; a < kChunk; a++) {
                const int i = ci * kChunk + a;
                if (i >= hN) break;
                for (int b = 0; b < kChunk; b++) {
                    const int j = cj * kChunk + b;
                    if (j >= hN) break;
                    const int id = i * hN + j, k = a * kChunk + b;
                    hgrid[id] = c.h[k], surf[id] = c.surf[k], paint[id] = c.paint[k];
                }
            }
        }
    }, 1);
    // (The sparse layers, one thread: CellTiles makes its tiles as it's written.)
    for (int ci = 0; ci < cn; ci++)
        for (int cj = 0; cj < cn; cj++) {
            const Chunk& c = *ch[(size_t)ci * cn + cj];
            if (c.water.empty() && c.roadNear.empty()) continue;
            for (int a = 0; a < kChunk; a++) {
                const int i = ci * kChunk + a;
                if (i >= hN) break;
                for (int b = 0; b < kChunk; b++) {
                    const int j = cj * kChunk + b;
                    if (j >= hN) break;
                    const int id = i * hN + j, k = a * kChunk + b;
                    if (!c.water.empty() && c.water[k] > -1e8f) waterY[id] = c.water[k], flowX[id] = c.flowX[k], flowZ[id] = c.flowZ[k];
                    if (!c.roadNear.empty()) {
                        if (c.roadNear[k]) roadNear[id] = c.roadNear[k];
                        if (c.roadLat[k] != 0) roadLat[id] = c.roadLat[k], roadAlong[id] = c.roadAlong[k];
                    }
                }
            }
        }
    // What stands on it: only from the chunks wholly inside the window (so nothing is cut at its edge).
    {
        loadprof::Scope lp("proc.objects");
        for (int ci = 0; ci + 1 < cn; ci++)
            for (int cj = 0; cj + 1 < cn; cj++) {
                const Chunk& c = *ch[(size_t)ci * cn + cj];
                const double lx0 = (double)(c.cx * kChunk - proc.ox), lz0 = (double)(c.cz * kChunk - proc.oz);
                const double gxc = (double)(c.cx * kChunk), gzc = (double)(c.cz * kChunk);
                for (const TreeSpec& s : c.trees) {
                    Tree t;
                    const double x = lx0 + s.x, z = lz0 + s.z;
                    t.base = V3(x, height(x, z) - 0.2, z);
                    t.height = s.height, t.radius = s.radius, t.tint = s.tint, t.kind = s.kind;
                    // (Its design from where it is in the world, not from where it falls in this window's list: the same tree in every window.)
                    t.design = forest::designFor(t.kind, (uint32_t)(hashIJ(0x7EEull, std::llround((gxc + s.x) * 100), std::llround((gzc + s.z) * 100)) >> 40), gxc + s.x, gzc + s.z, t.height);
                    plant(t);
                }
                for (const RockSpec& s : c.rocks) addRock(lx0 + s.x, lz0 + s.z, V3(s.rx, s.ry, s.rz), s.top, s.seed, s.tint);
                for (const LogSpec& s : c.logs) {
                    V3 a(lx0 + s.ax, 0, lz0 + s.az), b(lx0 + s.bx, 0, lz0 + s.bz);
                    a.y = height(a.x, a.z) + s.r * 0.8, b.y = height(b.x, b.z) + s.r * 0.8;
                    logs.push_back({a, b, s.r});
                }
                for (const BushSpec& s : c.bushes) {
                    Bush bu;
                    const double x = lx0 + s.x, z = lz0 + s.z;
                    bu.p = V3(x, height(x, z), z);
                    bu.kind = plantKindFor(s.habitat, s.edge != 0, plantRoll(gxc + s.x, gzc + s.z, 1));
                    bu.r = plantRadius(bu.kind, s.u);
                    bu.tint = s.tint;
                    bushes.push_back(bu);
                }
            }
    }
    // The places (their buildings, signs and camps), from where they are in the world.
    {
        loadprof::Scope lp("proc.places");
        buildPlaces(*this, windowPlaces(*this));
    }
    // The start: a pad there when it's well inside this window. (Elsewhere the spawn is the window's middle: nothing starts there.)
    const double lsx = (double)(sx - proc.ox), lsz = (double)(sz - proc.oz);
    if (std::fabs(lsx) < half - 100 && std::fabs(lsz) < half - 100) {
        spawn = V3(lsx, height(lsx, lsz), lsz);
        spawnYaw = syaw;
        addMapSpawnPads(*this);
    } else {
        spawn = V3(0, height(0, 0), 0);
        spawnYaw = 0;
    }
}
