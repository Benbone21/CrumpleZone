// The procedural world: the generator (pure functions of seed and global position), the chunk cache and World::buildProcedural
// (a window of the land). See procworld.h and docs/superpowers/specs/2026-10-25-procedural-world-design.md.
#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>
#include <random>
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

Macro ProcWorld::macro(double x, double z) const {
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
    {  // badlands: terraced benches, mesas with cliff rims, washes between
        const double mesa = smoothstep(0.56, 0.585, fbm(P.s[S_MESA], x * 0.0034 / S + 7.0, z * 0.0034 / S - 2.0, 3));
        double q = 14 + 22 * hill + 6 * hill2 + mesa * (16 + 10 * vnoise(P.s[S_TERR], x * 0.002, z * 0.002));
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
    double h = cont;
    for (int b = 0; b < PB_COUNT; b++) h += m.w[b] * hb[b];
    m.h = h;
    m.drain = cont + 0.0 * hill;  // (rivers: phase c)
    return m;
}

double ProcWorld::macroHeight(double x, double z) const { return macro(x, z).h; }

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

void ProcWorld::snapOrigin(double gx, double gz, int64_t& ox, int64_t& oz) {
    ox = (int64_t)std::floor(gx / kChunk + 0.5) * kChunk;
    oz = (int64_t)std::floor(gz / kChunk + 0.5) * kChunk;
}

void ProcWorld::start(int64_t& x, int64_t& z, double& yaw) const {
    // (Phase a: the gentlest open ground near the world's origin, not on a mountain. Places come in phase d.)
    double best = 1e18;
    x = 0, z = 0, yaw = 0;
    for (int a = -12; a <= 12; a++)
        for (int b = -12; b <= 12; b++) {
            const double gx = a * 24.0, gz = b * 24.0;
            const double h0 = macroHeight(gx, gz), hx = macroHeight(gx + 12, gz), hz = macroHeight(gx, gz + 12);
            const double slope = std::sqrt(sqr(hx - h0) + sqr(hz - h0)) / 12;
            const double cost = slope * 400 + std::sqrt(gx * gx + gz * gz) * 0.05 + std::max(0.0, h0 - 60) * 0.5;
            if (cost < best) best = cost, x = (int64_t)gx, z = (int64_t)gz;
        }
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
    auto lat = [&](int i, int j, double& fu, double& fv) {  // cell (i, j) of the chunk (-1..64): its lattice square
        const int ii = i + 4, jj = j + 4;
        fu = (ii & 3) * 0.25, fv = (jj & 3) * 0.25;
        return std::make_pair(ii >> 2, jj >> 2);
    };
    auto blend = [&](int i, int j, auto get) {
        double fu, fv;
        const auto q = lat(i, j, fu, fv);
        const Macro &m00 = M[(size_t)q.first * L + q.second], &m10 = M[(size_t)(q.first + 1) * L + q.second];
        const Macro &m01 = M[(size_t)q.first * L + q.second + 1], &m11 = M[(size_t)(q.first + 1) * L + q.second + 1];
        return (get(m00) * (1 - fu) + get(m10) * fu) * (1 - fv) + (get(m01) * (1 - fu) + get(m11) * fu) * fv;
    };
    // Heights over the chunk and its apron.
    std::vector<double> H((size_t)A * A);
    std::vector<uint8_t> dom((size_t)A * A);  // each cell's biome (dithered between the two leading ones)
    static const double kBump[PB_COUNT] = {0.85, 1.0, 0.9, 1.4, 0.25};
    for (int i = -1; i <= 64; i++)
        for (int j = -1; j <= 64; j++) {
            const double x = (double)(gx0 + i), z = (double)(gz0 + j);
            double w[PB_COUNT], bump = 0;
            for (int b = 0; b < PB_COUNT; b++) w[b] = blend(i, j, [b](const Macro& m) { return m.w[b]; }), bump += w[b] * kBump[b];
            double h = blend(i, j, [](const Macro& m) { return m.h; });
            // Uneven underfoot: bumps a few metres across.
            h += bump * (0.9 * (vnoise(P.s[S_BUMP1], x * 0.09 + 5.0, z * 0.09 + 1.0) - 0.5) + 0.35 * (vnoise(P.s[S_BUMP2], x * 0.31, z * 0.27) - 0.5));
            H[(size_t)(i + 1) * A + (j + 1)] = h;
            int b1 = 0, b2 = -1;
            for (int b = 1; b < PB_COUNT; b++)
                if (w[b] > w[b1]) b1 = b;
            for (int b = 0; b < PB_COUNT; b++)
                if (b != b1 && (b2 < 0 || w[b] > w[b2])) b2 = b;
            const double t = w[b2] / (w[b1] + w[b2]);  // 0..0.5
            const double dith = 0.55 + 0.7 * (vnoise(P.s[S_DITHER], x * 0.06, z * 0.06) - 0.5) + 0.3 * (hash01(P.s[S_DITHER], gx0 + i, gz0 + j) - 0.5);
            dom[(size_t)(i + 1) * A + (j + 1)] = (uint8_t)(2 * t > dith ? b2 : b1);
        }
    auto HH = [&](int i, int j) { return H[(size_t)(i + 1) * A + (j + 1)]; };
    auto slopeAt = [&](int i, int j) { return std::sqrt(sqr(HH(i + 1, j) - HH(i - 1, j)) + sqr(HH(i, j + 1) - HH(i, j - 1))) * 0.5; };
    // The ground.
    c->h.resize(kChunk * kChunk), c->surf.resize(kChunk * kChunk), c->paint.resize(kChunk * kChunk);
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++) {
            const int id = i * kChunk + j;
            const double x = (double)(gx0 + i), z = (double)(gz0 + j), h = HH(i, j), slope = slopeAt(i, j);
            c->h[id] = (float)h;
            uint8_t s = SURF_GRASS, pt = PAINT_NONE;
            const double n1 = fbm(P.s[S_GROUND1], x * 0.035, z * 0.035, 2), n2 = vnoise(P.s[S_GROUND2], x * 0.11, z * 0.11);
            switch (dom[(size_t)(i + 1) * A + (j + 1)]) {
                case PB_MEADOW:
                    if (slope > 0.9) s = SURF_ROCK;
                    else if (n1 > 0.72 && n2 > 0.5) s = SURF_DIRT, pt = PAINT_TRAIL;  // bare patches
                    break;
                case PB_FOREST:
                    if (slope > 0.8) s = SURF_ROCK;
                    else if (n1 > 0.75) s = SURF_DIRT, pt = PAINT_TRAIL;
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
                    else if ((slope > 0.35 && n1 > 0.52) || (h > snowline - 40 && n2 > 0.55)) s = SURF_SCREE, pt = PAINT_SCREE;
                    break;
                }
                case PB_DUNES:
                    if (n1 < 0.25 && slope < 0.08) s = SURF_DIRT, pt = PAINT_DESERT;  // hardpan between the dunes
                    else s = SURF_SAND, pt = PAINT_SAND;
                    break;
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
    // Trees.
    const double perChunk = w[PB_FOREST] * 40 + w[PB_MEADOW] * 9 * (0.15 + 0.85 * grove) + w[PB_BADLANDS] * 2.5 + w[PB_MOUNTAINS] * 11 + w[PB_DUNES] * 0.3;
    const int wantTrees = (int)(perChunk * (w[PB_FOREST] > 0.5 ? 0.55 + 0.45 * grove : 1.0) + rr());
    std::vector<std::pair<float, float>> placed;
    for (int k = 0; k < wantTrees * 2 && (int)c->trees.size() < wantTrees; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double h = c->h[id], slope = slopeAt(i, j);
        const int b = dom[(size_t)(i + 1) * A + (j + 1)];
        if (slope > 0.7 || c->surf[id] == SURF_SAND || c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK) continue;
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
    // Rocks: more on the steep and in the badlands and mountains; now and then a big outcrop.
    const double rockRate = 1.2 * w[PB_MEADOW] + 2.0 * w[PB_FOREST] + 6.0 * w[PB_BADLANDS] + 6.5 * w[PB_MOUNTAINS] + 0.4 * w[PB_DUNES];
    const int rockTries = (int)(rockRate * 1.6 + rr());
    for (int k = 0; k < rockTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double slope = slopeAt(i, j);
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
    // A fallen log in the woods.
    if (w[PB_FOREST] + w[PB_MEADOW] * grove > 0.4 && rr() < 0.6 * (w[PB_FOREST] + 0.5 * w[PB_MEADOW])) {
        const double fx = 8 + 48 * rr(), fz = 8 + 48 * rr(), L2 = 2 + 3 * rr(), r = 0.18 + 0.18 * rr();
        double dx = rr() - 0.5, dz = rr() - 0.5;
        const double dl = std::sqrt(dx * dx + dz * dz) + 1e-9;
        dx /= dl, dz /= dl;
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        if (c->surf[id] == SURF_GRASS && slopeAt(i, j) < 0.4) c->logs.push_back({(float)(fx - dx * L2), (float)(fz - dz * L2), (float)(fx + dx * L2), (float)(fz + dz * L2), (float)r});
    }
    // Bushes: what grows where.
    const int bushTries = (int)(14 * (0.3 + 0.7 * std::max(grove, w[PB_BADLANDS] + w[PB_DUNES] * 0.3)) + rr());
    for (int k = 0; k < bushTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const int b = dom[(size_t)(i + 1) * A + (j + 1)];
        if (c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK) continue;
        if (b == PB_DUNES && rr() < 0.8) continue;
        BushSpec s;
        s.x = (float)fx, s.z = (float)fz, s.u = (float)rr(), s.tint = (float)(0.75 + 0.45 * rr());
        s.habitat = (uint8_t)(b == PB_BADLANDS || b == PB_DUNES ? HAB_DESERT : b == PB_MOUNTAINS ? HAB_UPLAND : b == PB_FOREST ? HAB_FOREST : HAB_MEADOW);
        s.edge = grove > 0.4;
        c->bushes.push_back(s);
    }
    c->genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return c;
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
        c.spawnPos += d, c.jackFoot += d, c.shoveP += d;
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
