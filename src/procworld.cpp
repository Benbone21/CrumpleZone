// The procedural world: the generator (pure functions of seed and global position), the chunk cache and World::buildProcedural
// (a window of the land). See procworld.h and docs/superpowers/specs/2026-10-25-procedural-world-design.md.
#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
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
// The low-lying level of each biome's land (what its rivers are cut down to), and how much its hills rise and fall.
const double kBase[PB_COUNT] = {0, 4, 5, 14, 2};
const double kHillAmp[PB_COUNT] = {24, 48, 22, 26, 8};
}  // namespace

Macro ProcWorld::land(double x, double z) const {
    const Params& P = *params_;
    const double S = P.scale;
    Macro m;
    m.wet = stretch(fbm(P.s[S_WET], x * 0.00022 / S + 0.3, z * 0.00022 / S - 0.7, 3), P.wetShift);
    m.rug = stretch(fbm(P.s[S_RUG], x * 0.00030 / S - 1.1, z * 0.00030 / S + 0.4, 3), P.rugShift);
    m.heat = stretch(fbm(P.s[S_HEAT], x * 0.00017 / S + 2.2, z * 0.00017 / S + 1.3, 2), P.heatShift);
    double sum = 0;
    for (int b = 0; b < PB_COUNT; b++) {
        const double d2 = sqr(m.wet - kBiomeAt[b][0]) + 1.4 * sqr(m.rug - kBiomeAt[b][1]) + sqr(m.heat - kBiomeAt[b][2]);
        const double q = d2 + 0.004;
        const double q2 = q * q, q4 = q2 * q2;
        m.w[b] = P.bias[b] / (q4 * q2);  // (sharp enough that a biome holds its ground, soft enough to blend over a few hundred metres)
        sum += m.w[b];
    }
    for (int b = 0; b < PB_COUNT; b++) m.w[b] /= sum;

    // The land of each biome.
    const double hill = fbm(P.s[S_HILL1], x * 0.0022 / S, z * 0.0022 / S, 4) - 0.5;
    const double hill2 = fbm(P.s[S_HILL2], x * 0.0070 / S + 3.0, z * 0.0070 / S - 1.0, 3) - 0.5;
    const double cont = 36 * (fbm(P.s[S_CONT], x * 0.00035 / S, z * 0.00035 / S, 3) - 0.5);
    double hb[PB_COUNT];
    hb[PB_MEADOW] = 6 + 24 * hill + 6 * hill2;
    hb[PB_FOREST] = 16 + 48 * hill + 15 * hill2;
    {  // badlands: terraced benches, mesas and buttes with cliff rims, washes between
        const double mesa = smoothstep(0.535, 0.56, fbm(P.s[S_MESA], x * 0.0034 / S + 7.0, z * 0.0034 / S - 2.0, 3));
        const double butte = smoothstep(0.66, 0.68, fbm(P.s[S_MESA] + 77, x * 0.011 / S - 3.0, z * 0.011 / S + 5.0, 2));
        double q = 12 + 22 * hill + 6 * hill2 + mesa * (16 + 10 * vnoise(P.s[S_TERR], x * 0.002, z * 0.002)) + butte * 12;
        const double step = 4.5, f = q / step, fl = std::floor(f);
        q = step * (fl + smoothstep(0.55, 1.0, f - fl));  // (a riser over the last part of each bench: continuous)
        hb[PB_BADLANDS] = q;
    }
    {  // mountains: ridged ranges with high valleys between
        const double rr = ridged(P.s[S_RIDGE], x * 0.0011 / S + 0.7, z * 0.0011 / S + 2.9, 5);
        const double range = 0.35 + 0.65 * smoothstep(0.3, 0.7, fbm(P.s[S_RANGE], x * 0.0006 / S, z * 0.0006 / S, 2));
        hb[PB_MOUNTAINS] = 40 + P.relief * 250 * rr * rr * range + 26 * hill;
    }
    {  // dunes: long crests across the wind, a gentle windward face and a steep lee
        const double u = (x * P.windX + z * P.windZ) / 52.0 + 2.2 * fbm(P.s[S_DUNE], x * 0.0035, z * 0.0035, 2);
        const double t = u - std::floor(u);
        const double prof = smoothstep(0.0, 0.74, t) * (1 - smoothstep(0.74, 1.0, t));
        const double amp = 3 + 6 * fbm(P.s[S_DUNE2], x * 0.002, z * 0.002, 2);
        hb[PB_DUNES] = 4 + 8 * hill + amp * prof;
    }
    double h = cont, drain = 0.5 * cont, low = cont;  // (the drain: half the broad swell, so rivers run gently)
    for (int b = 0; b < PB_COUNT; b++) h += m.w[b] * hb[b], drain += m.w[b] * kBase[b], low += m.w[b] * (kBase[b] + kHillAmp[b] * (hill + 0.25));
    m.h = h;
    m.drain = drain;
    m.creekLevel = drain + std::max(0.0, 0.6 * (low - drain));
    return m;
}

Macro ProcWorld::macro(double x, double z) const {
    const Params& P = *params_;
    const double S = P.scale;
    Macro m = land(x, z);
    const double cont = 36 * (fbm(P.s[S_CONT], x * 0.00035 / S, z * 0.00035 / S, 3) - 0.5);

    // Rivers and creeks: where a warped field crosses 0.5. The distance to the channel is the field's offset over its slope.
    const double wx = 260 * (fbm(P.s[S_WARP1], x * 0.0016, z * 0.0016, 2) - 0.5), wz = 260 * (fbm(P.s[S_WARP2], x * 0.0016, z * 0.0016, 2) - 0.5);
    auto field = [&](uint64_t s, double f, double px, double pz) { return fbm(s, (px + wx) * f, (pz + wz) * f, 3); };
    auto channel = [&](uint64_t s, double f, double& sdist, double& tx, double& tz) {
        const double r0 = field(s, f, x, z), rx = field(s, f, x + 2, z), rz = field(s, f, x, z + 2);
        const double gx = (rx - r0) / 2, gz = (rz - r0) / 2, g = std::sqrt(gx * gx + gz * gz);
        sdist = (r0 - 0.5) / std::max(g, 1e-6);
        tx = g > 1e-9 ? -gz / g : 1, tz = g > 1e-9 ? gx / g : 0;
    };
    double tx, tz, ctx2, ctz;
    channel(P.s[S_RIVER], 0.00045 / S, m.riverS, tx, tz);
    channel(P.s[S_CREEK], 0.0012 / S, m.creekS, ctx2, ctz);
    // (Downhill: the way the land's broad swell falls along the channel.)
    const double c1 = 36 * (fbm(P.s[S_CONT], (x + 16) * 0.00035 / S, z * 0.00035 / S, 3) - 0.5), c2 = 36 * (fbm(P.s[S_CONT], x * 0.00035 / S, (z + 16) * 0.00035 / S, 3) - 0.5);
    const double dgx = c1 - cont, dgz = c2 - cont;
    const double sgn = tx * dgx + tz * dgz > 0 ? -1.0 : 1.0, csgn = ctx2 * dgx + ctz * dgz > 0 ? -1.0 : 1.0;
    m.flowX = tx * sgn, m.flowZ = tz * sgn, m.cflowX = ctx2 * csgn, m.cflowZ = ctz * csgn;
    m.riverOn = clamp01(1.25 - 1.7 * (m.w[PB_DUNES] + 0.75 * m.w[PB_BADLANDS]));          // (dry washes in the desert)
    m.creekOn = clamp01((m.wet - 0.3) * 3) * clamp01(1.2 - 1.6 * (m.w[PB_DUNES] + 0.6 * m.w[PB_BADLANDS]));
    m.creekOn *= 1 - smoothstep(6, 14, m.h - m.creekLevel);  // (creeks run in the low country: no gorges cut through the hills)
    return m;
}

double ProcWorld::riverHalfWidth(double x, double z) const { return 5 + 5 * vnoise(params_->s[S_RIVER] + 7, x * 0.003, z * 0.003); }
double ProcWorld::creekHalfWidth(double x, double z) const { return 1.4 + 1.3 * vnoise(params_->s[S_CREEK] + 7, x * 0.006, z * 0.006); }
double ProcWorld::riverDepth(double x, double z) const {
    const double q = vnoise(params_->s[S_RIVER] + 11, x * 0.006, z * 0.006);
    return 0.3 + 0.45 * smoothstep(0.22, 0.32, q) + 0.45 * smoothstep(0.74, 0.84, q);  // riffles 0.3, runs 0.75, pools 1.2
}

double ProcWorld::macroHeight(double x, double z) const { return land(x, z).h; }

// Where a trail's tread lies: the land low-passed over ~30 m (a function of where, so two trails that meet or cross agree), and down
// into the water where it fords a river or a creek.
double ProcWorld::trailLevel(double x, double z) const {
    // (Many taps, each a small share: a cliff or a terrace riser inside the kernel moves the level a little as each tap crosses it.)
    static const double kDir[12][2] = {{1, 0},     {0.866, 0.5},   {0.5, 0.866},   {0, 1},  {-0.5, 0.866}, {-0.866, 0.5},
                                       {-1, 0},    {-0.866, -0.5}, {-0.5, -0.866}, {0, -1}, {0.5, -0.866}, {0.866, -0.5}};
    double s = 2 * land(x, z).h, wsum = 2;
    for (int k = 0; k < 12; k++) {
        const double* d = kDir[k];
        if (k % 2 == 0) s += 1.5 * land(x + 12 * d[0], z + 12 * d[1]).h, wsum += 1.5;
        s += land(x + 24 * d[0], z + 24 * d[1]).h + 0.6 * land(x + 36 * d[1], z - 36 * d[0]).h, wsum += 1.6;
    }
    double y = s / wsum;
    const Macro m = macro(x, z);
    // Down the valley a river or creek has cut (as the chunks cut it), then into the water at the ford. (Weighed in as there's water,
    // not switched: a trail level that jumped would step the tread.)
    if (m.riverOn > 0.02) {
        const double d = std::fabs(m.riverS), hw = riverHalfWidth(x, z), level = m.drain, bank = level + 0.3, wetK = smoothstep(0.3, 0.6, m.riverOn);
        const double V = std::min(350.0, 25 + 3.0 * std::max(0.0, y - level));
        const double wetCut = y > bank ? bank + (y - bank) * smoothstep(hw, hw + V, d) : bank;
        const double dryCut = y - 0.8 * (1 - smoothstep(hw * 0.5, hw + 8, d));  // (a dry wash: a sandy trough in the land as it is)
        y += (dryCut + (wetCut - dryCut) * wetK - y) * std::min(1.0, m.riverOn * 2);
        y += (level - 0.45 - y) * (1 - smoothstep(hw + 2, hw + 8, d)) * smoothstep(0.35, 0.6, m.riverOn);
    }
    if (m.creekOn > 0.02) {
        const double d = std::fabs(m.creekS), hw = creekHalfWidth(x, z), level = m.creekLevel, bank = level + 0.2;
        const double V = std::min(120.0, 10 + 1.5 * std::max(0.0, y - level));
        const double cut = y > bank ? bank + (y - bank) * smoothstep(hw, hw + V, d) : bank;
        y += (cut - y) * std::min(1.0, m.creekOn * 2);
        y += (level - 0.3 - y) * (1 - smoothstep(hw + 1, hw + 5, d)) * smoothstep(0.35, 0.6, m.creekOn);
    }
    return y;
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
    // A spot in the cell, clear of rivers and creeks (a few tries), not up a cliff.
    double best = -1e18;
    for (int k = 0; k < 6; k++) {
        const double x = (double)(i * kCell) + 90 + (kCell - 180) * hash01(P.s[S_DITHER] + 3 + k, i, j);
        const double z = (double)(j * kCell) + 90 + (kCell - 180) * hash01(P.s[S_DITHER] + 13 + k, i, j);
        const Macro m = macro(x, z);
        const double slope = std::fabs(macroHeight(x + 10, z) - m.h) / 10 + std::fabs(macroHeight(x, z + 10) - m.h) / 10;
        const double clear = std::min(std::fabs(m.riverS) - riverHalfWidth(x, z) - (m.riverOn > 0 ? 45 : 15), std::fabs(m.creekS) - 20);
        const double score = std::min(clear, 30.0) - 120 * slope - 0.4 * std::max(0.0, m.h - 120);
        if (score > best) best = score, n.x = x, n.z = z;
        if (clear > 30 && slope < 0.08) break;
    }
    n.h = trailLevel(n.x, n.z);
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
        const double bx = b.x - a.x, bz = b.z - a.z, L = std::sqrt(bx * bx + bz * bz);
        const double nx = -bz / L, nz = bx / L;
        const double wig = e->road ? 30 : 75;
        const double j1 = wig * (2 * hash01(P.s[S_DITHER] + 201 + (uint64_t)dir, i, j) - 1), j2 = wig * (2 * hash01(P.s[S_DITHER] + 301 + (uint64_t)dir, i, j) - 1);
        // (Control points relative to the start: small numbers, the same on every machine.)
        const double cx[4] = {0, bx / 3 + nx * j1, bx * 2 / 3 + nx * j2, bx}, cz[4] = {0, bz / 3 + nz * j1, bz * 2 / 3 + nz * j2, bz};
        std::vector<double> px, pz;
        for (int s = 0; s < 3; s++) {
            const int i0 = std::max(s - 1, 0), i3 = std::min(s + 2, 3);
            const double seg = std::sqrt(sqr(cx[s + 1] - cx[s]) + sqr(cz[s + 1] - cz[s]));
            const int steps = std::max(2, (int)(seg / 2.0));
            for (int k = 0; k < steps; k++) {
                const double t = (double)k / steps, t2 = t * t, t3 = t2 * t;
                auto cr = [&](const double* c) {
                    return 0.5 * (2 * c[s] + (c[s + 1] - c[i0]) * t + (2 * c[i0] - 5 * c[s] + 4 * c[s + 1] - c[i3]) * t2 + (3 * c[s] - c[i0] - 3 * c[s + 1] + c[i3]) * t3);
                };
                px.push_back(cr(cx)), pz.push_back(cr(cz));
            }
        }
        px.push_back(bx), pz.push_back(bz);
        const int n = (int)px.size();
        // The tread's height all along it: the trail level (a function of where: see trailLevel), and its rating from how steep that runs.
        std::vector<double> y(n), along(n, 0.0);
        for (int k = 0; k < n; k++)
            if (k % 4 == 0 || k == n - 1) {
                const double gx = a.x + px[k], gz = a.z + pz[k];
                y[k] = trailLevel(gx, gz);  // (every ~8 m: it's smooth; the rest between)
                if (!e->ford) {  // (where it first wades: a river crossing to find)
                    const Macro m = macro(gx, gz);
                    if ((m.riverOn > 0.5 && std::fabs(m.riverS) < riverHalfWidth(gx, gz)) || (m.creekOn > 0.5 && std::fabs(m.creekS) < creekHalfWidth(gx, gz)))
                        e->ford = true, e->fordX = gx, e->fordZ = gz;
                }
            }
        for (int k = 0; k < n; k++) {
            if (k % 4 && k != n - 1) {
                const int k0 = k - k % 4, k1 = std::min(n - 1, k0 + 4);
                y[k] = y[k0] + (y[k1] - y[k0]) * (double)(k - k0) / (k1 - k0);
            }
            if (k) along[k] = along[k - 1] + std::sqrt(sqr(px[k] - px[k - 1]) + sqr(pz[k] - pz[k - 1]));
        }
        double steep = 0;
        for (int k = 10; k < n; k++) steep = std::max(steep, std::fabs(y[k] - y[k - 10]) / std::max(1.0, along[k] - along[k - 10]));
        e->rating = e->road ? 0 : steep <= 0.15 ? 0 : steep <= 0.25 ? 1 : 2;
        e->x.resize(n), e->z.resize(n), e->y.resize(n), e->along.resize(n);
        double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
        for (int k = 0; k < n; k++) {
            e->x[k] = (float)px[k], e->z[k] = (float)pz[k], e->y[k] = (float)y[k], e->along[k] = (float)along[k];
            x0 = std::min(x0, px[k]), x1 = std::max(x1, px[k]), z0 = std::min(z0, pz[k]), z1 = std::max(z1, pz[k]);
        }
        const double margin = 20;
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
        for (int64_t i = ci - 1; i <= ci + 1; i++)
            for (int64_t j = cj - 1; j <= cj + 1; j++)
                for (int dir = 0; dir < 3; dir++) {
                    auto e = edge(i, j, dir);
                    if (e->on && e->x1 >= (double)gx0 - 2 && e->x0 <= (double)gx0 + 66 && e->z1 >= (double)gz0 - 2 && e->z0 <= (double)gz0 + 66) edges.push_back(e);
                }
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
                if (n.x + r + 14 < (double)gx0 - 1 || n.x - r - 14 > (double)gx0 + 65 || n.z + r + 14 < (double)gz0 - 1 || n.z - r - 14 > (double)gz0 + 65) continue;
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
            h += bump * (0.9 * (vnoise(P.s[S_BUMP1], x * 0.09 + 5.0, z * 0.09 + 1.0) - 0.5) + 0.35 * (vnoise(P.s[S_BUMP2], x * 0.31, z * 0.27) - 0.5));
            // A river: its channel cut down to the drain level, banks easing back up to the land (wider the higher the land stands).
            const double rOn = blend(i, j, [](const Macro& m) { return m.riverOn; });
            if (rOn > 0.02) {
                const double d = std::fabs(blend(i, j, [](const Macro& m) { return m.riverS; }));
                const double hw = riverHalfWidth(x, z), level = blend(i, j, [](const Macro& m) { return m.drain; });
                const double wetK = smoothstep(0.3, 0.6, rOn);  // (a river fades into a dry wash in the desert: carved as one, blended)
                const double V = std::min(350.0, 25 + 3.0 * std::max(0.0, h - level));
                if (d < hw + 10 + V * wetK) {
                    const bool wet = rOn > 0.5;
                    const double depth = riverDepth(x, z);
                    const double bank = level + 0.3;
                    double nh = d < hw ? level - depth * (1 - sqr(d / hw)) : bank + (h - bank) * smoothstep(hw, hw + V, d);
                    if (d >= hw && h < bank) nh = bank + (h - bank) * smoothstep(hw + 4, hw + 30, d);  // (low ground: a levee, then down)
                    const double dry = h - 0.8 * (1 - smoothstep(hw * 0.5, hw + 8, d));
                    h = h + (dry + (nh - dry) * wetK - h) * std::min(1.0, rOn * 2);
                    if (d < hw + 0.5) cl.bed = wet ? 1 : 2;
                    if (d < hw + 3) cl.keep = 1;
                    if (wet && d < hw + 1.5 && h < level - 0.02) {
                        cl.water = level;
                        const double sp = 0.4 + 0.9 * (1 - smoothstep(0.3, 1.2, depth));  // (fast over the riffles, slow in the pools)
                        cl.fx = blend(i, j, [](const Macro& m) { return m.flowX; }) * sp, cl.fz = blend(i, j, [](const Macro& m) { return m.flowZ; }) * sp;
                    }
                }
            }
            // A creek: the same, small, at its own level.
            const double cOn = blend(i, j, [](const Macro& m) { return m.creekOn; });
            if (cOn > 0.02) {
                const double d = std::fabs(blend(i, j, [](const Macro& m) { return m.creekS; }));
                const double hw = creekHalfWidth(x, z), level = blend(i, j, [](const Macro& m) { return m.creekLevel; });
                const double V = std::min(120.0, 10 + 1.5 * std::max(0.0, h - level));
                if (d < hw + V && cl.water < -1e8) {
                    const bool wet = cOn > 0.5;
                    const double depth = 0.25 + 0.2 * vnoise(P.s[S_CREEK] + 3, x * 0.01, z * 0.01);
                    const double bank = level + 0.2;
                    double nh = d < hw ? level - depth * (1 - sqr(d / hw)) : bank + (h - bank) * smoothstep(hw, hw + V, d);
                    if (d >= hw && h < bank) nh = bank + (h - bank) * smoothstep(hw + 2, hw + 14, d);
                    h = h + (nh - h) * std::min(1.0, cOn * 2);
                    if (d < hw + 0.4 && !cl.bed) cl.bed = wet ? 1 : 2;
                    if (d < hw + 2) cl.keep = 1;
                    if (wet && d < hw + 1 && h < level - 0.02) {
                        cl.water = level;
                        cl.fx = blend(i, j, [](const Macro& m) { return m.cflowX; }) * 0.7, cl.fz = blend(i, j, [](const Macro& m) { return m.cflowZ; }) * 0.7;
                    }
                }
            }
            // The trails: the nearest one's tread, the land easing into it over a few metres.
            double best = 1e9, bestY = 0, bestLat = 0, bestAlong = 0;
            const TrailEdge* bestE = nullptr;
            for (const auto& ep : edges) {
                const TrailEdge& e = *ep;
                if (x < e.x0 || x > e.x1 || z < e.z0 || z > e.z1) continue;
                const double lx = x - e.ax, lz = z - e.az;
                for (size_t k = 0; k + 1 < e.x.size(); k++) {
                    const double axp = e.x[k], azp = e.z[k], dx = e.x[k + 1] - axp, dz = e.z[k + 1] - azp, L2 = dx * dx + dz * dz;
                    if (L2 < 1e-9) continue;
                    // (Quick reject: further than the blend from this stretch's ends.)
                    if (std::fabs(lx - axp) > 30 && std::fabs(lx - e.x[k + 1]) > 30) continue;
                    if (std::fabs(lz - azp) > 30 && std::fabs(lz - e.z[k + 1]) > 30) continue;
                    const double t = std::min(1.0, std::max(0.0, ((lx - axp) * dx + (lz - azp) * dz) / L2));
                    const double qx = axp + dx * t - lx, qz = azp + dz * t - lz, dd = qx * qx + qz * qz;
                    if (dd < best) {
                        best = dd, bestE = &e;
                        bestY = e.y[k] + (e.y[k + 1] - e.y[k]) * t;
                        bestAlong = e.along[k] + (e.along[k + 1] - e.along[k]) * t;
                        bestLat = (dx * (lz - azp) - dz * (lx - axp)) / std::sqrt(L2);
                    }
                }
            }
            if (bestE) {
                const double d = std::sqrt(best), hw = bestE->half(), blendW = bestE->road ? 10 : 8, e = d - hw;
                if (e < blendW) {
                    const double wgt = e < 1.0 ? 1.0 : smoothstep(blendW, 1.0, e);
                    h = h + (bestY - h) * wgt;
                    cl.lat = (float)bestLat, cl.along = (float)bestAlong;
                    if (e < 0.5) cl.bed = bestE->road ? 4 : 3, cl.roadNear = true;
                    if (e < 3) cl.keep = 1;
                    if (cl.water > -1e8 && h >= cl.water - 0.05) cl.water = -1e9, cl.fx = cl.fz = 0;  // (the bank it climbs out on)
                }
            }
            for (const Plaza& pz : plazas) {  // (level ground for the place's buildings, easing into the land)
                const double d = std::sqrt(sqr(x - pz.x) + sqr(z - pz.z));
                if (d > pz.r + 14) continue;
                h += (pz.h - h) * smoothstep(pz.r + 14, pz.r, d);
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
                    const double snowline = 175 + 40 * (fbm(P.s[S_SNOW], x * 0.006, z * 0.006, 3) - 0.5);
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
        if (b == PB_MOUNTAINS && h > 150 + 20 * rr()) continue;  // the treeline
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
        if (b == PB_MOUNTAINS) th *= 1 - 0.35 * smoothstep(90, 160, h);
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
    return ProcWorld::forSeed(w.proc.seed).macroHeight(x + (double)w.proc.ox, z + (double)w.proc.oz);
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
