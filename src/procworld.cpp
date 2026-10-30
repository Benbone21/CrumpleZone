// The procedural world: the generator (pure functions of seed and global position), the chunk cache and World::buildProcedural
// (a window of the land). See procworld.h and docs/superpowers/specs/2026-10-25-procedural-world-design.md.
#include "procworld.h"
#include "procdress.h"
#include "procrules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <future>
#include <limits>
#include <list>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "bgwork.h"
#include "forest.h"
#include "loadprof.h"
#include "parallel.h"
#include "plants.h"
#include "proceros.h"
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
    S_GROUND2, S_GROVE, S_DITHER, S_RIVER, S_CREEK, S_WARP1, S_WARP2, S_SNOW, S_JIT, S_D8, S_COUNT
};
struct ProcWorld::Params {
    uint64_t s[S_COUNT];
    double scale = 1;          // how big the land's features are (0.85 - 1.2)
    double bias[PB_COUNT];     // how much of each biome the world has
    double wetShift = 0, rugShift = 0, heatShift = 0;
    double windX = 1, windZ = 0;  // the dunes' crests run across this
    double grainX = 1, grainZ = 0;  // the ranges run along this (the world's grain)
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
    double gx = u(8) - 0.5, gz = u(9) - 0.5;
    const double gl = std::sqrt(gx * gx + gz * gz);
    if (gl > 1e-6) p.grainX = gx / gl, p.grainZ = gz / gl;
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

// ==== The drainage network (docs/plans/procworld-next.md "Slice: land shape with real drainage") ===================================
// The land is shaped round its water, not the water cut into the land:
//   1. A lattice of nodes, one in each 160 m cell (jittered in it). Each has a routing height R: the large-scale land alone (a slow
//      tilt of the lowland, the ranges' broad massifs), no hills or crags, so there are almost no pits.
//   2. Each node drains to the lower neighbour (of 8) it falls to fastest (a little dice in the choice, so the channels branch and
//      join like real ones). R falls strictly downstream, so the network is a forest of trees: no loops. A node with no lower
//      neighbour is a sink (a lake basin).
//   3. A node's drainage A: itself and every node upstream (memoised; a pure function of seed and node, the same on every machine).
//      A gully from a few nodes, a creek from ~0.3 km2, a river from ~1.5 km2, a big river from ~12 km2.
//   4. Each channel's level: from its sink up, rising toward its target (the valley floor R for a big river; for a small channel high
//      up the mountain's mass, as it lies high on the slopes) but never steeper than its class may climb (gentle for a big river,
//      cascades for a creek high in the ranges) and never less than a whisper: so the level never rises downstream.
//   5. The channels: each node's stretch (a curve from the middle of the link above it to the middle of the link below, through it)
//      and, where it joins a bigger one, a short link into it. Straight pieces of those curves are what the land is cut by.
//   6. The land: the mountains' mass M = R + U (U: the uplift over the valley floors, tall in the ranges), cut by every channel's
//      valley (its bed, a flat floor, then a side rising to the mass, steepest at its foot, as wide as the batter makes it), the
//      lowest of them; then crags on the high ground away from the valleys. The bed is under its banks by construction: the land is
//      never filled, only cut.
namespace {
constexpr double kD = 160;           // m: the drainage lattice's cell
constexpr int kB = 32, kAp = kB + 2;  // nodes a side of a cached block of them, and with its apron (the neighbours' heights)
constexpr int32_t kACap = 20000;     // (drainage counted up to this: plenty for the widest river)
constexpr double kMaxReach = 4000;   // m: the furthest a valley's side reaches from its channel
constexpr double kNotchReach = 520;  // m: the furthest a creek's or gully's notch reaches (its side: 6 + batter x 34 m at most)
constexpr double kCrag = 0.22;       // (the crags on the high ground: this share of the mountains' mass, times the ridged noise squared)
constexpr double kNodeKm2 = kD * kD / 1e6;
constexpr int kTile = 1024, kTC = 8, kTCell = kTile / kTC;  // the channels near a 1 km tile, binned in 128 m cells
const int kN8[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

struct Route {
    double R = 0;       // the routing height: the valley floors' level the channels follow down
    double mk = 0;      // 0 the lowland .. 1 the heart of a range
    double massif = 0;  // the range's broad rise of its valley floors (0..1)
    double U = 0;       // the uplift: how far the land's mass stands over the valley floors (the valleys are cut into it)
    double summit = 0;  // ...of it, the summits' (m)
    double bad = 0;     // how much badlands the climate makes it (mesas: a taller, flat-topped mass)
};

// The climate's lowland biomes' weights (no alpine: that comes from height): meadow, forest, badlands, -, dunes.
void lowWeights(const ProcWorld::Params& P, double wet, double heat, double mk, double w[PB_COUNT]);
}  // namespace

namespace {
// How much of the summits stand at a point: in massifs here and there along a range's axis, a few km across, not all along it.
// (slack > 0: the same, looser, for a bound over the land round about.)
double summitShare(const ProcWorld::Params& P, double ga, double gc, double massif, double slack) {  // (ga, gc: along and across the grain, the belts' warp in them)
    const double S = P.scale;
    const RangeRule& RR = rules().range;
    const double a = smoothstep(0.6 - slack, 1.0, massif);
    if (a <= 0) return 0;
    // (The field is stretched along the grain like the belts, and bent with them: the summits are crests along the range, not domes.)
    const double f = 1 / (RR.summitSize * S);
    return a * smoothstep(RR.summitFrom - slack, RR.summitFull, fbm(P.s[S_RIDGE] + 31, ga * f / RR.beltStretch, gc * f, 2));
}
}  // namespace
namespace {
Route routeAt(const ProcWorld::Params& P, double x, double z, double wet, double heat) {  // (x, z: copies, warped below)
    const double S = P.scale;
    const RangeRule& RR = rules().range;
    Route r;
    // (Everything below is read through a warp ~3 km across: the slopes' fall lines bend, so the channels gather into valleys
    // rather than run side by side. A smooth warp gentler than 1:1 makes no new pits.)
    const double qx = 900 * (fbm(P.s[S_WARP1] + 21, x * 0.00028 / S, z * 0.00028 / S, 2) - 0.5), qz = 900 * (fbm(P.s[S_WARP2] + 21, x * 0.00028 / S, z * 0.00028 / S, 2) - 0.5);
    x += qx, z += qz;
    // The lowland's floor: a broad, slow tilt (features 20-odd km across). The rivers run down it.
    const double base = 60 + 200 * (fbm(P.s[S_CONT], x * 0.00004 / S + 0.37, z * 0.00004 / S - 0.21, 2) - 0.5);
    // The ranges: belts of a slow field, stretched along the world's grain (long ranges) and warped (so they bend).
    const double ga = x * P.grainX + z * P.grainZ, gc = -x * P.grainZ + z * P.grainX;
    // (The bend's warp scales with the belts: about half a belt across, over features twice a belt's length.)
    const double bk = RR.beltSize / 8300, wf = 0.00006 / bk;
    const double wa = 4000 * bk * (fbm(P.s[S_WARP1] + 5, x * wf, z * wf, 2) - 0.5), wc = 4000 * bk * (fbm(P.s[S_WARP2] + 5, x * wf, z * wf, 2) - 0.5);
    const double fc = 1 / (RR.beltSize * S), fa = fc / RR.beltStretch;
    double belt = fbm(P.s[S_RANGE], (ga + wa) * fa, (gc + wc) * fc, 3);
    {  // (Desert country, hot and dry: the ranges sink away there into mesa and sand, several km of it, before the next range.)
        double lw0[PB_COUNT];
        lowWeights(P, wet, heat, 0, lw0);
        belt -= RR.desertSink * smoothstep(0.25, 0.6, lw0[PB_BADLANDS] + lw0[PB_DUNES]);
    }
    r.mk = smoothstep(RR.beltFrom - 0.06 * P.relief, RR.beltFull, belt);
    // (The range rises all the way to its axis, the belt field's crest: no flat-topped plateau with a rim.)
    r.massif = smoothstep(0.0, 1.0, (belt - RR.beltFrom) / std::max(0.05, RR.beltTop - RR.beltFrom));
    r.R = base + RR.passHeight * P.relief * r.massif;
    double w[PB_COUNT];
    lowWeights(P, wet, heat, r.mk, w);
    r.bad = smoothstep(0.25, 0.6, w[PB_BADLANDS]) * (1 - smoothstep(0.15, 0.55, r.mk));
    const double lowU = 30 + 110 * r.bad;
    r.U = lowU + (RR.peakHeight * P.relief - lowU) * r.massif * r.massif * r.massif;  // (the mass gathers on the axis: the tops)
    // The summits: broad domes rising over km in massifs here and there on the axis (the peaks, 1.5-2.5 km). In the uplift, so the
    // channels see them: the creeks run down off them and climb up onto them, rather than cut through them.
    r.summit = RR.summitHeight * P.relief * summitShare(P, ga + wa, gc + wc, r.massif, 0);
    r.U += r.summit;
    return r;
}
double wetAt(const ProcWorld::Params& P, double x, double z) {
    return stretch(fbm(P.s[S_WET], x * 0.00022 / P.scale + 0.3, z * 0.00022 / P.scale - 0.7, 3), P.wetShift);
}
double heatAt(const ProcWorld::Params& P, double x, double z) {
    return stretch(fbm(P.s[S_HEAT], x * 0.00017 / P.scale + 2.2, z * 0.00017 / P.scale + 1.3, 2), P.heatShift);
}
void lowWeights(const ProcWorld::Params& P, double wet, double heat, double mk, double w[PB_COUNT]) {
    const double nearRange = smoothstep(0.05, 0.45, mk);
    double sum = 0;
    for (int b = 0; b < PB_COUNT; b++) {
        w[b] = 0;
        if (b == PB_MOUNTAINS) continue;
        const double d2 = sqr(wet - kLowAt[b][0]) + sqr(heat - kLowAt[b][1]);
        const double q = d2 + 0.004, q2 = q * q;
        w[b] = P.bias[b] / (q2 * q2 * q);
        if (b == PB_BADLANDS || b == PB_DUNES) w[b] *= 1 - 0.995 * nearRange;
        sum += w[b];
    }
    for (int b = 0; b < PB_COUNT; b++) w[b] /= sum;
}

// ---- The lattice: blocks of 32 x 32 nodes, made once and kept (each node's place, routing height and where it drains) -----------
struct DBlock {
    uint64_t seed = 0;
    int64_t bi = 0, bj = 0;
    double x[kAp * kAp], z[kAp * kAp], R[kAp * kAp];
    int8_t down[kB * kB];
    float mk[kB * kB], U[kB * kB], wet[kB * kB], heat[kB * kB];
    std::atomic<int32_t> A[kB * kB];     // the drainage (-1: not yet counted)
    std::atomic<int8_t> up[kB * kB];     // the biggest neighbour draining into it (-2: not yet found, -1: none)
    std::atomic<float> level[kB * kB];   // the channel's level at the node (NaN: not yet worked out)
    // (The second pass: the creeks and gullies drain down the land the rivers' valleys made, not the broad routing height.)
    std::atomic<int8_t> down2[kB * kB];  // where it drains in the end (-3: not yet worked out, -1: a sink)
    std::atomic<int32_t> A2[kB * kB];    // its drainage in the end (-1: not yet counted)
    std::atomic<int8_t> up2[kB * kB];    // the biggest neighbour draining into it in the end (-2: not yet found)
    std::atomic<float> K[kB * kB];       // the smooth land with the rivers' valleys at the node (NaN: not yet)
    std::atomic<int8_t> perched[kB * kB];  // a creek's stretch whose water would stand over the land beside it: dry (-1: not yet)
    std::atomic<float> lakeW[kB * kB];     // a sink's lake's level (NaN: not yet)
    std::atomic<int8_t> lakeOk[kB * kB];   // a lake's rim stands over its water all round but its lip (-1: not yet)
};
struct BKey {
    uint64_t seed;
    int64_t i, j;
    bool operator==(const BKey& o) const { return seed == o.seed && i == o.i && j == o.j; }
};
struct BKHash {
    size_t operator()(const BKey& k) const { return (size_t)hashIJ(k.seed ^ 0xB10Cull, k.i, k.j); }
};
std::mutex blockMutex;
std::unordered_map<BKey, std::shared_ptr<DBlock>, BKHash> blockMap;
std::atomic<uint64_t> drainEpoch{1};  // (bumped when the caches are dropped: the threads' own short lists go stale)

inline void nodePos(const ProcWorld::Params& P, int64_t i, int64_t j, double& x, double& z) {
    x = ((double)i + 0.5 + 0.72 * (hash01(P.s[S_JIT], i, j) - 0.5)) * kD;
    z = ((double)j + 0.5 + 0.72 * (hash01(P.s[S_JIT] + 1, i, j) - 0.5)) * kD;
}
// (Ties in the routing height go by a hash: a strict order, so nothing drains in a ring.)
inline bool lowerKey(double Ra, uint64_t ha, double Rb, uint64_t hb) { return Ra < Rb || (Ra == Rb && ha < hb); }

// (A diagonal link crosses the other diagonal of the same lattice square: two channels in an X, one over the other. Each square lets
// one of its diagonals be drained along, by its dice; the other never, so no two channels ever cross.)
inline bool diagOk(const ProcWorld::Params& P, int64_t i, int64_t j, int dx, int dz) {
    if (!dx || !dz) return true;
    const int64_t si = dx > 0 ? i : i - 1, sj = dz > 0 ? j : j - 1;
    return ((hashIJ(P.s[S_D8] + 911, si, sj) & 1) != 0) == (dx == dz);
}
std::shared_ptr<DBlock> makeBlock(const ProcWorld& G, int64_t bi, int64_t bj) {
    const ProcWorld::Params& P = G.params();
    auto b = std::make_shared<DBlock>();
    b->seed = G.seed(), b->bi = bi, b->bj = bj;
    for (int a = 0; a < kAp; a++)
        for (int c = 0; c < kAp; c++) {
            const int64_t i = bi * kB + a - 1, j = bj * kB + c - 1;
            const int k = a * kAp + c;
            nodePos(P, i, j, b->x[k], b->z[k]);
            const double wet = wetAt(P, b->x[k], b->z[k]), heat = heatAt(P, b->x[k], b->z[k]);
            const Route r = routeAt(P, b->x[k], b->z[k], wet, heat);
            b->R[k] = r.R;
            if (a >= 1 && c >= 1 && a <= kB && c <= kB) {
                const int q = (a - 1) * kB + (c - 1);
                b->mk[q] = (float)r.mk, b->U[q] = (float)r.U, b->wet[q] = (float)wet, b->heat[q] = (float)heat;
            }
        }
    for (int a = 0; a < kB; a++)
        for (int c = 0; c < kB; c++) {
            const int64_t i = bi * kB + a, j = bj * kB + c;
            const int k = (a + 1) * kAp + (c + 1), q = a * kB + c;
            const uint64_t hk = hashIJ(P.s[S_JIT] + 7, i, j);
            int best = -1;
            double bestS = 0;
            for (int n = 0; n < 8; n++) {
                const int kn = (a + 1 + kN8[n][0]) * kAp + (c + 1 + kN8[n][1]);
                if (!diagOk(P, i, j, kN8[n][0], kN8[n][1])) continue;
                if (!lowerKey(b->R[kn], hashIJ(P.s[S_JIT] + 7, i + kN8[n][0], j + kN8[n][1]), b->R[k], hk)) continue;
                const double dist = std::sqrt(sqr(b->x[kn] - b->x[k]) + sqr(b->z[kn] - b->z[k]));
                // (the steepest way down, with a little dice: on an even slope it wanders left or right, so channels meet)
                const double s = (b->R[k] - b->R[kn]) / dist * (1 + 1.1 * (hash01(P.s[S_D8] + (uint64_t)n, i, j) - 0.5)) + 1e-12;
                if (s > bestS) bestS = s, best = n;
            }
            b->down[q] = (int8_t)best;
            b->A[q].store(-1, std::memory_order_relaxed);
            b->up[q].store(-2, std::memory_order_relaxed);
            b->level[q].store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
            b->down2[q].store(-3, std::memory_order_relaxed), b->A2[q].store(-1, std::memory_order_relaxed), b->up2[q].store(-2, std::memory_order_relaxed);
            b->K[q].store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
            b->perched[q].store(-1, std::memory_order_relaxed);
            b->lakeW[q].store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
            b->lakeOk[q].store(-1, std::memory_order_relaxed);
        }
    return b;
}

// The block holding node (i, j). (Each thread keeps its last few: the pointer is good until this thread's next 16 lookups.)
DBlock* blockFor(const ProcWorld& G, int64_t i, int64_t j, int& q) {
    const int64_t bi = floorDiv(i, kB), bj = floorDiv(j, kB);
    q = (int)(i - bi * kB) * kB + (int)(j - bj * kB);
    struct Slot {
        uint64_t seed = 0, epoch = 0;
        int64_t bi = 0, bj = 0;
        std::shared_ptr<DBlock> b;
    };
    thread_local Slot slots[16];
    thread_local int next = 0;
    const uint64_t ep = drainEpoch.load(std::memory_order_relaxed);
    for (Slot& s : slots)
        if (s.b && s.epoch == ep && s.bi == bi && s.bj == bj && s.seed == G.seed()) return s.b.get();
    std::shared_ptr<DBlock> b;
    {
        std::lock_guard<std::mutex> lock(blockMutex);
        auto it = blockMap.find({G.seed(), bi, bj});
        if (it != blockMap.end()) b = it->second;
    }
    if (!b) {
        auto made = makeBlock(G, bi, bj);
        std::lock_guard<std::mutex> lock(blockMutex);
        if (blockMap.size() > 600) blockMap.clear(), drainEpoch++;  // (~32 MB: dropped whole; what's in use is held)
        b = blockMap.emplace(BKey{G.seed(), bi, bj}, made).first->second;
    }
    Slot& s = slots[next++ & 15];
    s.seed = G.seed(), s.epoch = drainEpoch.load(std::memory_order_relaxed), s.bi = bi, s.bj = bj, s.b = b;
    return b.get();
}

struct NodeInfo {
    double x, z, R;
    float mk, U, wet, heat;
    int down;
};
NodeInfo nodeInfo(const ProcWorld& G, int64_t i, int64_t j) {
    int q;
    const DBlock* b = blockFor(G, i, j, q);
    const int k = (q / kB + 1) * kAp + (q % kB + 1);
    return {b->x[k], b->z[k], b->R[k], b->mk[q], b->U[q], b->wet[q], b->heat[q], b->down[q]};
}
inline int down1Of(const ProcWorld& G, int64_t i, int64_t j) {
    int q;
    return blockFor(G, i, j, q)->down[q];
}

// The drainage of node (i, j): 1 + the drainage of every neighbour draining into it (capped). Counted without recursion.
int32_t accum1Of(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const int32_t a = blockFor(G, i, j, q)->A[q].load(std::memory_order_relaxed);
        if (a >= 0) return a;
    }
    struct Fr {
        int64_t i, j;
        int k;
        int32_t sum;
    };
    thread_local std::vector<Fr> st;
    const size_t base = st.size();  // (reentrant: the stack is shared with any caller up the line)
    st.push_back({i, j, 0, 1});
    int32_t result = 1;
    while (st.size() > base) {
        Fr& f = st.back();
        if (f.k == 8 || f.sum >= kACap) {
            const int32_t v = std::min(kACap, f.sum);
            int q;
            blockFor(G, f.i, f.j, q)->A[q].store(v, std::memory_order_relaxed);
            st.pop_back();
            if (st.size() > base) st.back().sum += v, st.back().k++;
            else result = v;
            continue;
        }
        const int64_t ni = f.i + kN8[f.k][0], nj = f.j + kN8[f.k][1];
        if (down1Of(G, ni, nj) != ((f.k + 4) & 7)) {  // (it drains elsewhere)
            f.k++;
            continue;
        }
        int q;
        const int32_t a = blockFor(G, ni, nj, q)->A[q].load(std::memory_order_relaxed);
        if (a >= 0) f.sum += a, f.k++;
        else st.push_back({ni, nj, 0, 1});
    }
    return result;
}

// The neighbour with the most drainage that drains into (i, j) (-1: a source).
int upMain1Of(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const int u = blockFor(G, i, j, q)->up[q].load(std::memory_order_relaxed);
        if (u > -2) return u;
    }
    int best = -1;
    int32_t bestA = 0;
    for (int k = 0; k < 8; k++) {
        const int64_t ni = i + kN8[k][0], nj = j + kN8[k][1];
        if (down1Of(G, ni, nj) != ((k + 4) & 7)) continue;
        const int32_t a = accum1Of(G, ni, nj);
        if (a > bestA) bestA = a, best = k;
    }
    int q;
    blockFor(G, i, j, q)->up[q].store((int8_t)best, std::memory_order_relaxed);
    return best;
}

struct DrainClass {
    double gully, creek, river, big;  // nodes of drainage
};
DrainClass drainClass() {
    const WaterRule& W = rules().water;
    return {W.gullyArea / kNodeKm2, W.creekArea / kNodeKm2, W.riverArea / kNodeKm2, W.bigRiverArea / kNodeKm2};
}
inline bool isRiver(const ProcWorld& G, int64_t i, int64_t j) { return accum1Of(G, i, j) >= drainClass().river; }

double valeSmoothAt(const ProcWorld& G, double x, double z);  // (below: the smooth land with the rivers' valleys)

// ---- The second pass: the small channels drain down the land the rivers made -------------------------------------------------------
// (The rivers, and the headwaters of each (its chain of biggest feeders up from where it becomes a river), keep the first pass's
// way down. Every other node drains to the neighbour it falls to fastest on the smooth land with the rivers' valleys cut: so a creek
// runs straight down a valley's side to its river, not across it. The first pass's edges fall in the routing height, the second's
// in that land, and a second-pass chain ends in a river or a headwater and never leaves it: still no loops.)
bool headwater(const ProcWorld& G, int64_t i, int64_t j) {
    int64_t ci = i, cj = j;
    for (int k = 0; k < 2000; k++) {
        const int d = down1Of(G, ci, cj);
        if (d < 0) return false;
        const int64_t ni = ci + kN8[d][0], nj = cj + kN8[d][1];
        if (upMain1Of(G, ni, nj) != ((d + 4) & 7)) return false;
        if (isRiver(G, ni, nj)) return true;
        ci = ni, cj = nj;
    }
    return false;
}
double nodeK(const ProcWorld& G, int64_t i, int64_t j) {
    int q;
    {
        const float k = blockFor(G, i, j, q)->K[q].load(std::memory_order_relaxed);
        if (k == k) return k;
    }
    const NodeInfo n = nodeInfo(G, i, j);
    const float k = (float)valeSmoothAt(G, n.x, n.z);
    blockFor(G, i, j, q)->K[q].store(k, std::memory_order_relaxed);
    return k;
}
int down2Of(const ProcWorld& G, int64_t i, int64_t j) {
    int q;
    {
        const int d = blockFor(G, i, j, q)->down2[q].load(std::memory_order_relaxed);
        if (d > -3) return d;
    }
    int best;
    if (isRiver(G, i, j) || headwater(G, i, j)) best = down1Of(G, i, j);
    else {
        const ProcWorld::Params& P = G.params();
        const NodeInfo n = nodeInfo(G, i, j);
        const double kn = nodeK(G, i, j);
        const uint64_t hn = hashIJ(P.s[S_JIT] + 7, i, j);
        best = -1;
        double bestS = 0;
        for (int k = 0; k < 8; k++) {
            const int64_t ni = i + kN8[k][0], nj = j + kN8[k][1];
            if (!diagOk(P, i, j, kN8[k][0], kN8[k][1])) continue;
            const double km = nodeK(G, ni, nj);
            if (!lowerKey(km, hashIJ(P.s[S_JIT] + 7, ni, nj), kn, hn)) continue;
            const NodeInfo m = nodeInfo(G, ni, nj);
            const double s = (kn - km) / std::sqrt(sqr(m.x - n.x) + sqr(m.z - n.z)) * (1 + 0.5 * (hash01(P.s[S_D8] + 50 + (uint64_t)k, i, j) - 0.5)) + 1e-12;
            if (s > bestS) bestS = s, best = k;
        }
    }
    blockFor(G, i, j, q)->down2[q].store((int8_t)best, std::memory_order_relaxed);
    return best;
}
// The drainage in the end: a river's from the first pass (its size), a creek's or gully's counted over the second.
int32_t areaOf(const ProcWorld& G, int64_t i, int64_t j) {
    if (isRiver(G, i, j)) return accum1Of(G, i, j);
    {
        int q;
        const int32_t a = blockFor(G, i, j, q)->A2[q].load(std::memory_order_relaxed);
        if (a >= 0) return a;
    }
    struct Fr {
        int64_t i, j;
        int k;
        int32_t sum;
    };
    thread_local std::vector<Fr> st;
    const size_t base = st.size();
    st.push_back({i, j, 0, 1});
    int32_t result = 1;
    while (st.size() > base) {
        Fr& f = st.back();
        if (f.k == 8 || f.sum >= kACap) {
            const int32_t v = std::min(kACap, f.sum);
            int q;
            blockFor(G, f.i, f.j, q)->A2[q].store(v, std::memory_order_relaxed);
            st.pop_back();
            if (st.size() > base) st.back().sum += v, st.back().k++;
            else result = v;
            continue;
        }
        const int64_t ni = f.i + kN8[f.k][0], nj = f.j + kN8[f.k][1];
        if (down2Of(G, ni, nj) != ((f.k + 4) & 7)) {
            f.k++;
            continue;
        }
        int q;
        const int32_t a = blockFor(G, ni, nj, q)->A2[q].load(std::memory_order_relaxed);
        if (a >= 0) f.sum += a, f.k++;
        else st.push_back({ni, nj, 0, 1});
    }
    return result;
}
int upMain2Of(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const int u = blockFor(G, i, j, q)->up2[q].load(std::memory_order_relaxed);
        if (u > -2) return u;
    }
    int best = -1;
    int32_t bestA = 0;
    for (int k = 0; k < 8; k++) {
        const int64_t ni = i + kN8[k][0], nj = j + kN8[k][1];
        if (down2Of(G, ni, nj) != ((k + 4) & 7)) continue;
        const int32_t a = areaOf(G, ni, nj);
        if (a > bestA) bestA = a, best = k;
    }
    int q;
    blockFor(G, i, j, q)->up2[q].store((int8_t)best, std::memory_order_relaxed);
    return best;
}

// How high up the mass a channel's target lies (1 a gully at the top .. 0 a big river on the valley floor).
inline double upFrac(double A) { return 0.95 * 60 / (60 + A); }
// The steepest its level may climb: by its size and how far in the ranges it is.
inline double gradeMax(double A, double mk) {
    const WaterRule& W = rules().water;
    const double small = 20 / (20 + A);
    const double gl = 0.1 * W.riverGrade + 0.055 * small, gm = W.riverGrade + (W.creekGrade - W.riverGrade) * small;
    return gl + (gm - gl) * mk;
}
constexpr double kGradeMin = 0.0004;  // (and never flat: the level always falls downstream)

// A river's level at node (i, j): worked out from its sink up (memoised). (Rivers: drainage over river_area. Everything downstream
// of a river is a river.)
double riverLevelOf(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const float l = blockFor(G, i, j, q)->level[q].load(std::memory_order_relaxed);
        if (l == l) return l;
    }
    thread_local std::vector<std::pair<int64_t, int64_t>> path;
    const size_t base = path.size();
    int64_t ci = i, cj = j;
    double known = 0;
    bool haveKnown = false;
    for (;;) {
        int q;
        const DBlock* b = blockFor(G, ci, cj, q);
        const float l = b->level[q].load(std::memory_order_relaxed);
        if (l == l) {
            known = l, haveKnown = true;
            break;
        }
        path.push_back({ci, cj});
        const int d = b->down[q];
        if (d < 0) break;
        ci += kN8[d][0], cj += kN8[d][1];
    }
    double below = known;  // (the level of the node below the one being worked out)
    for (size_t k = path.size(); k-- > base;) {
        const int64_t pi = path[k].first, pj = path[k].second;
        const NodeInfo n = nodeInfo(G, pi, pj);
        double lv;
        if (n.down < 0 || (k + 1 == path.size() && !haveKnown)) lv = n.R;  // (a sink: its own floor)
        else {
            const NodeInfo d = nodeInfo(G, pi + kN8[n.down][0], pj + kN8[n.down][1]);
            const double A = accum1Of(G, pi, pj), len = std::sqrt(sqr(d.x - n.x) + sqr(d.z - n.z));
            const double T = n.R + n.U * upFrac(A);
            double g = std::min(std::max((T - below) / len, kGradeMin), std::max(kGradeMin, gradeMax(A, n.mk)));
            // (Into a confluence: every river joining there comes in at a whisper of grade over the last stretch, so where the two
            // run side by side into it neither stands metres over the other's bed beside it (rules 3).)
            // (Two rivers may run side by side a few links before they meet: so the last three links into a confluence.)
            {
                int64_t di = pi + kN8[n.down][0], dj = pj + kN8[n.down][1];
                for (int hop = 0; hop < 3; hop++) {
                    int inflows = 0;
                    for (int e = 0; e < 8 && inflows < 2; e++) {
                        const int64_t mi = di + kN8[e][0], mj = dj + kN8[e][1];
                        if (isRiver(G, mi, mj) && down1Of(G, mi, mj) == ((e + 4) & 7)) inflows++;
                    }
                    if (inflows >= 2) {
                        g = std::min(g, 4 * kGradeMin);
                        break;
                    }
                    const int dd = down1Of(G, di, dj);
                    if (dd < 0) break;
                    di += kN8[dd][0], dj += kN8[dd][1];
                }
            }
            lv = below + g * len;
        }
        lv = (double)(float)lv;  // (as kept)
        int q;
        blockFor(G, pi, pj, q)->level[q].store((float)lv, std::memory_order_relaxed);
        below = lv;
    }
    path.resize(base);
    return below;
}

double valeAt(const ProcWorld& G, double x, double z);  // (below: the land with the rivers' valleys cut)

// How deep a creek or gully cuts into the land the rivers left (m): by its size and the ranges; and where it runs across a slope (a
// valley's side, at a slant), as deep as the land falls in 30 m across it, so the land beside it stands over it on its low side too:
// it runs in a ravine, not along a shelf.
inline double notchDepth(double A, double mk, double cross, double along) {
    const DrainClass DC = drainClass();
    const double base = A >= DC.creek ? 2.5 + 0.45 * std::sqrt(A) : 1.2 + 0.3 * std::sqrt(A);
    return base * (1 + 0.5 * mk) + 34 * std::min(1.3, cross) + 4 * std::min(1.0, along);
}

// A creek's or gully's level at node (i, j) (drainage under river_area): as low as its notch under the land the rivers left at each of
// its nodes, and never higher than any above it (a running low from its sources down: it cuts through a rise rather than climb it),
// and never lower than the river it runs into. Worked out over its whole tree upstream (small: under river_area), memoised.
double creekLevelOf(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const float l = blockFor(G, i, j, q)->level[q].load(std::memory_order_relaxed);
        if (l == l) return l;
    }
    const DrainClass DC = drainClass();
    double floorL = -1e9;  // (the river it runs into; none if it ends in a sink of its own)
    {
        int64_t ci = i, cj = j;
        for (int k = 0; k < 100000; k++) {
            if (isRiver(G, ci, cj)) {
                floorL = riverLevelOf(G, ci, cj);
                break;
            }
            const int d = down2Of(G, ci, cj);
            if (d < 0) break;
            ci += kN8[d][0], cj += kN8[d][1];
        }
    }
    struct Fr {
        int64_t i, j;
        int k;
        double mn;  // the lowest of the levels above it (each less a whisper for the way down)
    };
    thread_local std::vector<Fr> st;
    const size_t base = st.size();
    st.push_back({i, j, 0, 1e300});
    double result = 0;
    while (st.size() > base) {
        Fr& f = st.back();
        if (f.k == 8) {
            const NodeInfo n = nodeInfo(G, f.i, f.j);
            const double A = areaOf(G, f.i, f.j);
            const int nd = down2Of(G, f.i, f.j);
            const double v = valeAt(G, n.x, n.z);
            const double gx = valeAt(G, n.x + 10, n.z) - valeAt(G, n.x - 10, n.z), gz = valeAt(G, n.x, n.z + 10) - valeAt(G, n.x, n.z - 10);
            double cross = 0, along = std::sqrt(gx * gx + gz * gz) / 20;
            if (nd >= 0) {  // (the fall across its way down, and along it)
                const NodeInfo dn = nodeInfo(G, f.i + kN8[nd][0], f.j + kN8[nd][1]);
                const double dx = dn.x - n.x, dz = dn.z - n.z, dl = std::sqrt(dx * dx + dz * dz);
                cross = std::fabs(gx * dz - gz * dx) / (20 * dl), along = std::fabs(gx * dx + gz * dz) / (20 * dl);
            }
            const double t = v - notchDepth(A, n.mk, cross, along);
            const double lv = (double)(float)std::max(floorL, std::min(t, f.mn));
            int q;
            blockFor(G, f.i, f.j, q)->level[q].store((float)lv, std::memory_order_relaxed);
            const int64_t ci = f.i, cj = f.j;
            st.pop_back();
            if (st.size() > base) {
                Fr& pa = st.back();
                const NodeInfo pn = nodeInfo(G, pa.i, pa.j);
                pa.mn = std::min(pa.mn, lv - kGradeMin * std::sqrt(sqr(pn.x - n.x) + sqr(pn.z - n.z)));
                pa.k++;
                (void)ci, (void)cj;
            } else result = lv;
            continue;
        }
        const int64_t ni = f.i + kN8[f.k][0], nj = f.j + kN8[f.k][1];
        if (down2Of(G, ni, nj) != ((f.k + 4) & 7) || areaOf(G, ni, nj) < DC.gully) {
            f.k++;
            continue;
        }
        int q;
        const float l = blockFor(G, ni, nj, q)->level[q].load(std::memory_order_relaxed);
        if (l == l) {
            const NodeInfo a = nodeInfo(G, f.i, f.j), b = nodeInfo(G, ni, nj);
            f.mn = std::min(f.mn, (double)l - kGradeMin * std::sqrt(sqr(a.x - b.x) + sqr(a.z - b.z)));
            f.k++;
        } else st.push_back({ni, nj, 0, 1e300});
    }
    return result;
}

double levelOf(const ProcWorld& G, int64_t i, int64_t j) {
    {
        int q;
        const float l = blockFor(G, i, j, q)->level[q].load(std::memory_order_relaxed);
        if (l == l) return l;
    }
    return isRiver(G, i, j) ? riverLevelOf(G, i, j) : creekLevelOf(G, i, j);
}

// ---- The channels' straight pieces ---------------------------------------------------------------------------------------------
struct DrainSeg {
    float ax, az, bx, bz;       // relative to the tile's corner
    float la, lb;               // the level at a and b (never rising a -> b)
    float dx, dz, len;          // which way it runs (unit), and how long
    float hw, depth, F, bat, bank;  // the bed's half-width and depth, the flat floor past it, the side's batter, the bank over the water
    float reach;                // hw + F + the side's widest
    float A, wet;               // its drainage (nodes) and how much water (0 a dry wash .. 1)
    uint8_t cls;                // 0 gully, 1 creek, 2 river, 3 big river
    uint8_t ford;               // a known ford (a riffle) on a big river
    uint8_t notch;              // a creek's or gully's: cut into the land the rivers left, its side a fixed width (wfix)
    float wfix;
    int64_t ni, nj;             // the node it's the stretch of
    uint8_t fall;               // the lip-to-foot stretch of a waterfall
    uint8_t wall;               // ...and the stretch below it: a wall behind its start
};

// How much water a channel of node n carries, by the climate there: a river fades to a dry wash in the desert, a creek needs wet ground.
double channelWet(const ProcWorld::Params& P, const NodeInfo& n, int cls) {
    if (cls == 0) return 0;
    double w[PB_COUNT];
    lowWeights(P, n.wet, n.heat, n.mk, w);
    if (cls >= 2) return clamp01(1.25 - 1.7 * (w[PB_DUNES] + 0.75 * w[PB_BADLANDS]) * (1 - n.mk));  // (the ranges' rivers run from the snows)
    return clamp01((n.wet - 0.3 + 0.6 * n.mk) * 3) * clamp01(1.2 - 1.6 * (w[PB_DUNES] + 0.6 * w[PB_BADLANDS]));  // (the ranges are wet: snowmelt)
}

struct FallInfo {
    double height = 0;  // m: the step (0: no waterfall)
};
// Is there a waterfall on node (i, j)'s stretch (its drop ln - l3 across it)? Steep (20 m or more in the stretch: over 1 in 8), wet,
// in the ranges, not a ford; one in eight of those, by the node's dice. Up to 14 m.
FallInfo fallOf(const ProcWorld& G, int64_t i, int64_t j, const DrainSeg& s, const NodeInfo& n, double ln, double l3) {
    FallInfo f;
    const double d = ln - l3;
    if (s.cls >= 1 && s.wet > 0.5 && !s.ford && n.mk > 0.4 && d >= 20 && hash01(G.params().s[S_D8] + 77, i, j) < 0.12) f.height = std::min(14.0, 0.6 * d);
    return f;
}

// Node (i, j)'s stretch of channel (and its link into the bigger one it joins), as straight pieces, relative to (ox, oz).
void nodeSegs(const ProcWorld& G, int64_t i, int64_t j, double ox, double oz, std::vector<DrainSeg>& out, bool riversOnly) {
    const DrainClass DC = drainClass();
    const bool river = isRiver(G, i, j);
    if (riversOnly && !river) return;
    const double A = areaOf(G, i, j);
    if (A < DC.gully) return;
    const ProcWorld::Params& P = G.params();
    const NodeInfo n = nodeInfo(G, i, j);
    const double ln = levelOf(G, i, j);
    const int cls = river ? (A >= DC.big ? 3 : 2) : A >= DC.creek ? 1 : 0;
    // (A river's curve and joins are the first pass's alone (the rivers' own tiles are made from them, before any second pass); a
    // creek's the second's.)
    auto upMain = [&](int64_t a, int64_t b) { return isRiver(G, a, b) ? upMain1Of(G, a, b) : upMain2Of(G, a, b); };
    auto downIn = [&](int64_t a, int64_t b) { return isRiver(G, a, b) ? down1Of(G, a, b) : down2Of(G, a, b); };
    const int nDown = downIn(i, j);
    DrainSeg s{};
    s.ni = i, s.nj = j;
    s.A = (float)A, s.cls = (uint8_t)cls, s.wet = (float)channelWet(P, n, cls);
    // (Its bed by its class: a creek a couple of metres across and fordable anywhere, a river a few metres, a big river 10-26 m and
    // 1.5-2 m deep, a real obstacle.)
    const double sq = std::sqrt(A);
    if (cls == 3) s.hw = (float)std::min(13.0, 5 + 0.12 * std::sqrt(A - DC.big)), s.depth = (float)(1.5 + 0.5 * std::min(1.0, (A - DC.big) / (3 * DC.big)));
    else if (cls == 2) s.hw = (float)std::min(5.0, 2.0 + 0.3 * sq), s.depth = (float)std::min(1.0, 0.5 + 0.05 * sq);
    else if (cls == 1 && s.wet < 0.5) s.hw = (float)std::min(5.0, 1.2 + 0.32 * sq), s.depth = (float)std::min(1.1, 0.3 + 0.06 * sq);  // (a dry wash: wider and sandier downstream)
    else if (cls == 1) s.hw = (float)std::min(2.6, 1.0 + 0.25 * sq), s.depth = (float)std::min(0.5, 0.25 + 0.04 * sq);
    else s.hw = 0.5, s.depth = 0;
    s.F = (float)(cls ? std::min(120.0, 0.5 * s.hw + 0.05 * A) * (1 - 0.6 * n.mk) : 0.0);
    s.bank = (float)(cls ? 0.15 + 0.1 * s.depth : 0.0);
    double bat = rules().water.lowBatter + (rules().water.bankBatter - rules().water.lowBatter) * n.mk;
    double w[PB_COUNT];
    lowWeights(P, n.wet, n.heat, n.mk, w);
    bat += (1.6 - bat) * smoothstep(0.25, 0.6, w[PB_BADLANDS]) * (1 - smoothstep(0.15, 0.55, n.mk));  // (canyons under the mesas)
    // (The side is as wide as the batter makes it for the height it climbs at each point: the mass there, crags and all. Its reach:
    // the tallest the mass round here could be.)
    const RangeRule& RR = rules().range;
    const double Hmax = std::max(2.0, n.R + n.U + 60 + n.mk * (kCrag * RR.peakHeight * P.relief + 18) - ln);
    s.bat = (float)bat;
    s.reach = (float)std::min(kMaxReach, s.hw + s.F + 12 + bat * Hmax);
    if (cls < 2) {  // (a creek's or gully's notch: its sides as wide as the batter makes them for how deep it is at each point, up to
        // its deepest along this stretch, gentler in the lowland)
        double deepest = valeAt(G, n.x, n.z) - ln;
        if (nDown >= 0) {
            const NodeInfo dn = nodeInfo(G, i + kN8[nDown][0], j + kN8[nDown][1]);
            const double ld = levelOf(G, i + kN8[nDown][0], j + kN8[nDown][1]);
            deepest = std::max(deepest, std::max(valeAt(G, 0.5 * (n.x + dn.x), 0.5 * (n.z + dn.z)) - 0.5 * (ln + ld), valeAt(G, dn.x, dn.z) - ld));
        }
        deepest = std::min(120.0, std::max(1.5, deepest));
        const double nb = 13 + (9 - 13) * n.mk;
        // (A deep cut, where it saws through a rise, is a gorge with steep sides: they don't spread out over its neighbours' land.)
        s.notch = 1, s.bat = (float)nb, s.wfix = (float)(6 + nb * std::min(deepest + 4, 30.0));
        s.reach = s.hw + s.F + s.wfix;
    }
    // (Its known fords: a big river's riffles, one every ~ford_spacing; a pure function of the node.)
    s.ford = cls == 3 && hash01(P.s[S_D8] + 99, i, j) < kD / std::max(kD, rules().water.fordSpacing);
    auto emit = [&](double ax, double az, double bx, double bz, double la, double lb) {
        const double dx = bx - ax, dz = bz - az, L = std::sqrt(dx * dx + dz * dz);
        if (L < 1e-3) return;
        DrainSeg e = s;
        e.ax = (float)(ax - ox), e.az = (float)(az - oz), e.bx = (float)(bx - ox), e.bz = (float)(bz - oz);
        e.la = (float)la, e.lb = (float)lb, e.dx = (float)(dx / L), e.dz = (float)(dz / L), e.len = (float)L;
        out.push_back(e);
    };
    // The curve: from the middle of the link above (or the node, at a source) through the node to the middle of the link below.
    const int u = upMain(i, j);
    double p0x = n.x, p0z = n.z;
    if (u >= 0 && (river ? accum1Of(G, i + kN8[u][0], j + kN8[u][1]) : areaOf(G, i + kN8[u][0], j + kN8[u][1])) >= DC.gully) {
        const NodeInfo un = nodeInfo(G, i + kN8[u][0], j + kN8[u][1]);
        p0x = 0.5 * (un.x + n.x), p0z = 0.5 * (un.z + n.z);
    }
    double p2x = n.x, p2z = n.z, l2 = ln;
    int64_t di = 0, dj = 0;
    if (nDown >= 0) {
        di = i + kN8[nDown][0], dj = j + kN8[nDown][1];
        const NodeInfo d = nodeInfo(G, di, dj);
        p2x = 0.5 * (n.x + d.x), p2z = 0.5 * (n.z + d.z), l2 = levelOf(G, di, dj);
    }
    // (Where it joins a bigger channel, its curve ends on the bigger one's, at the middle of that one's curve, rather than at the
    // middle of the link between them: one smooth join, no island in a triangle of channels.)
    double l3 = l2;
    if (nDown >= 0 && upMain(di, dj) != ((nDown + 4) & 7)) {
        const NodeInfo d = nodeInfo(G, di, dj);
        const int du = upMain(di, dj);
        const int dDown = downIn(di, dj);
        const NodeInfo dun = nodeInfo(G, di + kN8[du][0], dj + kN8[du][1]);
        double d2x = d.x, d2z = d.z, ldd = l2;
        if (dDown >= 0) {
            const NodeInfo dd = nodeInfo(G, di + kN8[dDown][0], dj + kN8[dDown][1]);
            d2x = 0.5 * (d.x + dd.x), d2z = 0.5 * (d.z + dd.z), ldd = levelOf(G, di + kN8[dDown][0], dj + kN8[dDown][1]);
        }
        // (Into the bigger one's curve where it comes nearest this node: straight in, not alongside it first.)
        const double c0x = 0.5 * (dun.x + d.x), c0z = 0.5 * (dun.z + d.z);
        double bestD = 1e300, bestT = 0.5;
        for (int k = 0; k <= 16; k++) {
            const double t = k / 16.0, a = (1 - t) * (1 - t), b = 2 * t * (1 - t), c = t * t;
            const double qx = a * c0x + b * d.x + c * d2x, qz = a * c0z + b * d.z + c * d2z, dd2 = sqr(qx - n.x) + sqr(qz - n.z);
            if (dd2 < bestD) bestD = dd2, bestT = t;
        }
        bestT = std::max(0.25, bestT);  // (not up at its very start: past where its own feeder comes in)
        const double a = (1 - bestT) * (1 - bestT), b = 2 * bestT * (1 - bestT), c = bestT * bestT;
        p2x = a * c0x + b * d.x + c * d2x, p2z = a * c0z + b * d.z + c * d2z;
        l3 = l2 + (ldd - l2) * bestT;
    }
    double px = p0x, pz = p0z, pl = ln;
    const size_t first = out.size();
    // (A waterfall: only where the channel really drops, a steep stretch high in the ranges with water in it; most of the stretch's
    // drop then comes at once over a rock step ~4 m long half-way along, with a plunge pool below it. The rest of the drop is spread
    // over the stretch, so the level still never rises.)
    const FallInfo fi = fallOf(G, i, j, s, n, ln, l3);
    double tl = 0.5, tf = 0.5, tp = 0.5;
    if (fi.height > 0) {
        const double plen = std::sqrt(sqr(n.x - p0x) + sqr(n.z - p0z)) + std::sqrt(sqr(p2x - n.x) + sqr(p2z - n.z));
        tf = tl + 4 / std::max(40.0, plen), tp = tf + 14 / std::max(40.0, plen);
    }
    const double drop = ln - l3, rest = drop - fi.height;
    auto levelAtT = [&](double t) {
        if (fi.height <= 0) return ln + (l3 - ln) * t;
        if (t <= tl) return ln - rest * t;
        if (t < tf) return ln - rest * tl - fi.height * (t - tl) / (tf - tl);
        return ln - fi.height - rest * (tl + (1 - tl) * (t - tf) / (1 - tf));
    };
    double ts[8];
    int nt = 0;
    for (double t : {0.25, 0.5}) ts[nt++] = t;
    if (fi.height > 0) ts[nt++] = tf, ts[nt++] = tp;
    for (double t : {0.75, 1.0}) ts[nt++] = t;
    double prevT = 0;
    for (int q = 0; q < nt; q++) {
        const double t = ts[q], a = (1 - t) * (1 - t), b = 2 * t * (1 - t), c = t * t;
        const double ql = levelAtT(t);
        const double qx = a * p0x + b * n.x + c * p2x, qz = a * p0z + b * n.z + c * p2z;
        emit(px, pz, qx, qz, pl, ql);
        if (std::fabs(t - 0.5) < 1e-9 && s.ford) out.back().depth = (float)std::min((double)s.depth, rules().water.fordDepth);
        if (fi.height > 0 && std::fabs(prevT - tl) < 1e-9 && !out.empty()) out.back().fall = 1;
        if (fi.height > 0 && std::fabs(prevT - tf) < 1e-9 && !out.empty()) out.back().depth += 1.3f, out.back().wall = 1;  // (the plunge pool)
        px = qx, pz = qz, pl = ql, prevT = t;
    }
    if (s.ford)  // (the ford is the middle stretch alone: the rest of the node's curve deep)
        for (size_t k = first; k < out.size(); k++) out[k].ford = out[k].depth < s.depth;
}

// The lakes, one at most at a node (procworld-rules.md 3; the owner, after review: "lakes fill natural low ground, not dug holes").
// A lake is water let into ground the land already has, up to a lip:
//   1 a basin: a sink of the network; filled to just under the lowest point round it (lakeLevelOf). No outlet.
//   2 a tarn: high in the ranges at a creek's head, gentle there: a cirque. Its floor is shaped as a broad flat dish (a 1:8 shore, a back
//     wall where the dish meets the slope above) and a low lip (a moraine) across the creek below holds the water up to it.
//   3 a valley lake: on a gentle stretch of a river or creek in the lowland and foothills, a low ridge across the valley (a rock bar or
//     moraine) a little below the node, with its lip where the channel crosses it; the water backs up the valley floor and into the
//     side valleys, as far as the land is under the lip, so it's long and follows the valley.
//   4 a pond: the same, small and shallow, on a gully or small creek in the wet lowland.
// The water: where the land is under the lip and drains to the lake's node (upstream of the lip): its shore is the land's own contour.
// The outlet is the node's own channel, over the lip and on down: the lake stands on the drainage tree.
struct Lake {
    int kind = 0;
    int64_t ni = 0, nj = 0;
    double cx = 0, cz = 0;               // the node (global)
    double qx = 0, qz = 0, fx = 0, fz = 0;  // the lip (on the channel) and which way the water leaves over it
    double W = 0;                        // the water's level (a basin's: lakeLevelOf)
    double crest = 0, notch = 0, lat = 0;   // the dam over the water, the lip's half-width, how far across the valley the dam runs
    double dishR = 0, dishD = 0;         // the floor shaped under the water: its flat bed's radius (a valley lake's: half-width) and depth
    double ex = 0, ez = 0, ea = 0;       // a valley lake's or pond's floor: its middle and half-length along the valley (a tarn's: round its node)
    int64_t di = 0, dj = 0;              // the node below it
    double reach = 0;                    // how far up from the lip the water may go
};
inline bool even2(int64_t a, int m) { return ((a % m) + m) % m == 0; }
struct TKeyLake {
    uint64_t seed;
    int64_t i, j;
    bool operator==(const TKeyLake& o) const { return seed == o.seed && i == o.i && j == o.j; }
};
struct TKLHash {
    size_t operator()(const TKeyLake& k) const { return (size_t)hashIJ(k.seed ^ 0x1A4Eull, k.i, k.j); }
};
// (The cheap first part: which kind of lake node (i, j) would be, by its place in the network, the climate and the dice. Sinks aside.)
int lakeKindAt(const ProcWorld& G, int64_t i, int64_t j) {
    if (!(even2(i, 2) && even2(j, 2))) return 0;
    const DrainClass DC = drainClass();
    const double A = areaOf(G, i, j);
    if (A < DC.gully || down2Of(G, i, j) < 0) return 0;
    const ProcWorld::Params& P = G.params();
    const NodeInfo n = nodeInfo(G, i, j);
    const double roll = hash01(P.s[S_D8] + 211, i, j);
    const bool river = isRiver(G, i, j);
    if (n.mk > 0.45 && !river && A < 4 * DC.creek && even2(i, 6) && even2(j, 6) && roll < 0.95 && levelOf(G, i, j) > rules().alpine.treeLine - 800) return 2;
    if (A >= DC.creek && A < DC.big && (river || (even2(i, 4) && even2(j, 4))) && roll < 0.75) return 3;
    if (n.wet > 0.35 && !river && A < 3 * DC.creek && roll < 0.45) return 4;
    return 0;
}
std::atomic<long> gLakeFunnel[16];
struct FunnelPrint { ~FunnelPrint() { if (std::getenv("CZ_LAKE_DBG")) { for (int k = 0; k < 12; k++) std::fprintf(stderr, "funnel %d: %ld\n", k, gLakeFunnel[k].load()); } } } gFunnelPrint;
Lake lakeOfRaw(const ProcWorld& G, int64_t i, int64_t j);
// (memoised: every tile round a lake asks for it)
std::mutex lakeMemoMutex;
std::unordered_map<TKeyLake, Lake, TKLHash> lakeMemo;
Lake lakeOf(const ProcWorld& G, int64_t i, int64_t j) {
    const TKeyLake key{G.seed(), i, j};
    {
        std::lock_guard<std::mutex> lock(lakeMemoMutex);
        auto it = lakeMemo.find(key);
        if (it != lakeMemo.end()) return it->second;
    }
    const Lake L = lakeOfRaw(G, i, j);
    std::lock_guard<std::mutex> lock(lakeMemoMutex);
    if (lakeMemo.size() > 400000) lakeMemo.clear();  // (~60 MB at most)
    lakeMemo.emplace(key, L);
    return L;
}
Lake lakeOfRaw(const ProcWorld& G, int64_t i, int64_t j) {
    Lake L;
    const DrainClass DC = drainClass();
    const double A = areaOf(G, i, j);
    if (A < DC.gully) return L;
    const ProcWorld::Params& P = G.params();
    const NodeInfo n = nodeInfo(G, i, j);
    L.ni = i, L.nj = j, L.cx = n.x, L.cz = n.z;
    const int d = down2Of(G, i, j);
    if (d < 0) {
        L.kind = 1, L.reach = 300;
        return L;
    }
    const double size = hash01(P.s[S_D8] + 212, i, j);
    const double lv = levelOf(G, i, j);
    const int kind = lakeKindAt(G, i, j);
    if (!kind) return L;
    // (room round it: no stronger lake within 3 nodes, by the dice)
    const double pri = hash01(P.s[S_D8] + 214, i, j);
    for (int a = -3; a <= 3; a++)
        for (int b = -3; b <= 3; b++)
            if ((a || b) && lakeKindAt(G, i + a, j + b) && hash01(P.s[S_D8] + 214, i + a, j + b) > pri) return L;
    gLakeFunnel[kind]++;
    // (The node's channel: where on it the lip goes (past a tarn's floor; a little below the node for the others), its level there.)
    std::vector<DrainSeg> own;
    nodeSegs(G, i, j, 0, 0, own, false);
    if (own.empty()) return L;
    if (kind == 2) L.dishR = 18 + 22 * size, L.dishD = 1.2 + 1.3 * size;
    const double want = kind == 2 ? L.dishR + 8 * L.dishD + 6 : 0;  // (m along from the node)
    double best = 1e300;
    const DrainSeg* q = nullptr;
    double qt = 0;
    for (const DrainSeg& e : own)  // (the point of its curve nearest the node, or that far on past it)
        for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const double x = e.ax + (e.bx - e.ax) * t, z = e.az + (e.bz - e.az) * t, r = std::sqrt(sqr(x - n.x) + sqr(z - n.z));
            const bool past = (x - n.x) * e.dx + (z - n.z) * e.dz >= 0;
            const double score = past ? std::fabs(r - std::max(want, 30.0)) : 1e9;
            if (score < best) best = score, q = &e, qt = t;
        }
    if (!q) return L;
    L.qx = q->ax + (q->bx - q->ax) * qt, L.qz = q->az + (q->bz - q->az) * qt, L.fx = q->dx, L.fz = q->dz;
    const double lq = q->la + (q->lb - q->la) * qt;
    L.di = i + kN8[d][0], L.dj = j + kN8[d][1];
    if (kind == 2) {
        L.W = lq + 2.0;  // (the water up to the moraine's lip, 2 m over the creek where it leaves)
        if (lv - L.W > 15) return gLakeFunnel[5]++, Lake{};  // (the cirque's floor cut back into the slope 15 m at most: steeper, a meadow, no tarn)
        L.crest = 0.8, L.notch = q->hw + 1.5, L.reach = L.dishR + 8 * L.dishD + 60;
        L.ex = n.x, L.ez = n.z, L.ea = L.dishR;
    } else {
        const double drop = lv - lq, along = std::sqrt(sqr(L.qx - n.x) + sqr(L.qz - n.z));
        const double grade = drop / std::max(along, 20.0);
        if (grade > 0.08) return gLakeFunnel[6 + (kind == 4)]++, Lake{};  // (a gentle stretch only: a steep one makes no lake)
        const double s2 = hash01(P.s[S_D8] + 213, i, j);
        const double Hd = kind == 3 ? 1.5 + 2.5 * size : 0.8 + 0.6 * size;
        L.W = lq + Hd, L.crest = kind == 3 ? 1.0 : 0.6, L.notch = q->hw + 2, L.reach = kind == 3 ? 1600 : 500;
        // (its floor: a long shallow dish up the valley from the lip, flat-bedded, a 1:8 shore)
        // (its floor no longer than the valley's rise lets it be without cutting deep: a few metres of cut at its head at most)
        const double longest = (Hd + 4) / std::max(grade, 0.004);
        L.ea = std::min(longest, kind == 3 ? 80 + 220 * s2 : 22 + 30 * s2), L.dishR = std::min(L.ea, kind == 3 ? 30 + 55 * size : 14 + 18 * s2), L.dishD = kind == 3 ? 1.8 + 1.8 * s2 : 1.0 + 0.5 * size;
        L.ex = L.qx - L.fx * (L.ea + 4), L.ez = L.qz - L.fz * (L.ea + 4);
    }
    L.kind = kind;
    // (How far across the valley the dam runs: out to where the land the rivers left is over the water, past the creek's own notch.)
    const double px = -L.fz, pz = L.fx;
    double lat = 0;
    double latMin = 1e9;
    for (int sd = -1; sd <= 1; sd += 2) {
        double c = 10;
        for (; c < 700; c += 10)
            if (valeAt(G, L.qx + px * c * sd, L.qz + pz * c * sd) > L.W + 0.6) break;
        if (c >= 700) return gLakeFunnel[8]++, Lake{};  // (no valley to dam: the land round is too low)
        lat = std::max(lat, c), latMin = std::min(latMin, c);
    }
    // (A valley lake or pond only in a valley broad enough: its sides by the dam 1:4 or gentler on average.)
    if (kind != 2 && latMin < 3 * (L.W + 0.6 - lq)) return gLakeFunnel[9]++, Lake{};
    L.lat = std::max(lat + (q->notch ? q->wfix : 0) + 10, L.dishR + 8 * L.dishD + 20);
    return L;
}

// ---- The channels near a 1 km tile (cached), binned by 128 m cell ---------------------------------------------------------------
struct DTile {
    double ox = 0, oz = 0;        // its corner (global)
    std::vector<DrainSeg> segs;
    std::vector<uint32_t> start;  // kTC * kTC + 1
    std::vector<uint16_t> idx;    // the pieces whose valleys reach each cell (and 8 m round it)
    std::vector<Lake> lakes;  // (the lakes whose water, dam or floor may reach it)
};
struct TKey {
    uint64_t seed;
    int64_t i, j;
    bool operator==(const TKey& o) const { return seed == o.seed && i == o.i && j == o.j; }
};
struct TKHash {
    size_t operator()(const TKey& k) const { return (size_t)hashIJ(k.seed ^ 0x7113ull, k.i, k.j); }
};
std::mutex tileMutex;
std::unordered_map<TKey, std::shared_ptr<const DTile>, TKHash> tileMap[2];  // (all the channels; the rivers alone)

std::shared_ptr<const DTile> makeTile(const ProcWorld& G, int64_t ti, int64_t tj, bool riversOnly) {
    auto t = std::make_shared<DTile>();
    t->ox = (double)(ti * kTile), t->oz = (double)(tj * kTile);
    const int64_t i0 = (int64_t)std::floor((t->ox - kMaxReach) / kD) - 2, i1 = (int64_t)std::floor((t->ox + kTile + kMaxReach) / kD) + 2;
    const int64_t j0 = (int64_t)std::floor((t->oz - kMaxReach) / kD) - 2, j1 = (int64_t)std::floor((t->oz + kTile + kMaxReach) / kD) + 2;
    std::vector<DrainSeg> all;
    const double gully = drainClass().gully;
    const int64_t in0 = (int64_t)std::floor((t->ox - kNotchReach) / kD) - 2, in1 = (int64_t)std::floor((t->ox + kTile + kNotchReach) / kD) + 2;
    const int64_t jn0 = (int64_t)std::floor((t->oz - kNotchReach) / kD) - 2, jn1 = (int64_t)std::floor((t->oz + kTile + kNotchReach) / kD) + 2;
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++) {
            // (Quick: a node too far for even the widest valley, or too small to cut one, is skipped before its level is needed.)
            // (A creek's or gully's valley reaches a few hundred metres at most (kNotchReach): only a river's reaches further.)
            const bool near = i >= in0 && i <= in1 && j >= jn0 && j <= jn1;
            if (riversOnly || !near ? !isRiver(G, i, j) : areaOf(G, i, j) < gully) continue;
            const size_t from = all.size();
            nodeSegs(G, i, j, t->ox, t->oz, all, riversOnly);
            for (size_t k = from; k < all.size(); k++) {
                const DrainSeg& s = all[k];
                const double r = s.reach + 8;
                if (std::max(s.ax, s.bx) + r < 0 || std::min(s.ax, s.bx) - r > kTile || std::max(s.az, s.bz) + r < 0 || std::min(s.az, s.bz) - r > kTile) continue;
                t->segs.push_back(s);
            }
        }
    if (!riversOnly) {  // (the lakes: any whose lip lies within its water's reach of the tile)
        const int64_t a0 = (int64_t)std::floor((t->ox - 1700) / kD), a1 = (int64_t)std::floor((t->ox + kTile + 1700) / kD);
        const int64_t b0 = (int64_t)std::floor((t->oz - 1700) / kD), b1 = (int64_t)std::floor((t->oz + kTile + 1700) / kD);
        for (int64_t i = a0; i <= a1; i++)
            for (int64_t j = b0; j <= b1; j++) {
                const bool cand = (even2(i, 2) && even2(j, 2)) || down2Of(G, i, j) < 0;  // (cheap first: lakeOf only takes even nodes or sinks)
                if (!cand) continue;
                const Lake L = lakeOf(G, i, j);
                if (!L.kind) continue;
                const double r = L.reach + 200;
                if (L.qx + r < t->ox || L.qx - r > t->ox + kTile || L.qz + r < t->oz || L.qz - r > t->oz + kTile) continue;
                t->lakes.push_back(L);
            }
    }
    // (The rivers' pieces first: a point's land is the rivers' valleys, then the creeks' notches cut into that.)
    std::stable_sort(t->segs.begin(), t->segs.end(), [](const DrainSeg& a, const DrainSeg& b) { return a.notch < b.notch; });
    if (t->segs.size() > 65535) t->segs.resize(65535);  // (never near: a few thousand)
    std::vector<std::vector<uint16_t>> cell(kTC * kTC);
    for (size_t k = 0; k < t->segs.size(); k++) {
        const DrainSeg& s = t->segs[k];
        const double r = s.reach + 8;
        const int a0 = std::max(0, (int)std::floor((std::min(s.ax, s.bx) - r) / kTCell)), a1 = std::min(kTC - 1, (int)std::floor((std::max(s.ax, s.bx) + r) / kTCell));
        const int b0 = std::max(0, (int)std::floor((std::min(s.az, s.bz) - r) / kTCell)), b1 = std::min(kTC - 1, (int)std::floor((std::max(s.az, s.bz) + r) / kTCell));
        for (int a = a0; a <= a1; a++)
            for (int b = b0; b <= b1; b++) {
                // (Nearer than its reach to the cell's box: the distance from the segment to the box's middle, less the box's half-diagonal.)
                const double cx = (a + 0.5) * kTCell, cz = (b + 0.5) * kTCell;
                const double vx = cx - s.ax, vz = cz - s.az, tt = std::min((double)s.len, std::max(0.0, vx * s.dx + vz * s.dz));
                const double d = std::sqrt(sqr(cx - (s.ax + s.dx * tt)) + sqr(cz - (s.az + s.dz * tt)));
                if (d - kTCell * 0.7072 > r) continue;
                cell[a * kTC + b].push_back((uint16_t)k);
            }
    }
    t->start.assign(kTC * kTC + 1, 0);
    for (int c = 0; c < kTC * kTC; c++) t->start[c + 1] = t->start[c] + (uint32_t)cell[c].size();
    t->idx.reserve(t->start.back());
    for (auto& v : cell) t->idx.insert(t->idx.end(), v.begin(), v.end());
    return t;
}

std::shared_ptr<const DTile> tileAt(const ProcWorld& G, double x, double z, bool riversOnly = false) {
    const int64_t ti = (int64_t)std::floor(x / kTile), tj = (int64_t)std::floor(z / kTile);
    struct Slot {
        uint64_t seed = 0, epoch = 0;
        int64_t i = 0, j = 0;
        std::shared_ptr<const DTile> t;
    };
    thread_local Slot slots[2][4];
    thread_local int next[2] = {0, 0};
    const int kind = riversOnly ? 1 : 0;
    const uint64_t ep = drainEpoch.load(std::memory_order_relaxed);
    for (Slot& s : slots[kind])
        if (s.t && s.epoch == ep && s.i == ti && s.j == tj && s.seed == G.seed()) return s.t;
    std::shared_ptr<const DTile> t;
    {
        std::lock_guard<std::mutex> lock(tileMutex);
        auto it = tileMap[kind].find({G.seed(), ti, tj});
        if (it != tileMap[kind].end()) t = it->second;
    }
    if (!t) {
        auto made = makeTile(G, ti, tj, riversOnly);
        std::lock_guard<std::mutex> lock(tileMutex);
        if (tileMap[kind].size() > 400) tileMap[kind].clear();  // (~80 MB: dropped whole; what's in use is held)
        t = tileMap[kind].emplace(TKey{G.seed(), ti, tj}, made).first->second;
    }
    Slot& s = slots[kind][next[kind]++ & 3];
    s.seed = G.seed(), s.epoch = ep, s.i = ti, s.j = tj, s.t = t;
    return t;
}

// The valley a piece cuts, at distance d from its middle where its level is L: the bed under the water, a low bank, the flat floor,
// then the side up to the mass M (steepest at its foot; flat on top). bed=false: the valley without its bed (the floor right across).
inline double valleyCore(const DrainSeg& s, double d, double L, double M, bool bed);
// (Below a waterfall the stretch has a wall behind its start (3:1, rock): its valley doesn't wrap round up the step and smear it.)
inline double valleyAt(const DrainSeg& s, double d, double L, double M, bool bed, double behind = 0) { return valleyCore(s, d, L, M, bed) + (s.wall ? 3.0 * behind : 0.0); }
inline double valleyCore(const DrainSeg& s, double d, double L, double M, bool bed) {
    if (d < s.hw) return bed ? L - s.depth * (1 - sqr(d / s.hw)) : L + s.bank;
    if (d < s.hw + 1.5) return bed ? L + s.bank * (d - s.hw) / 1.5 : L + s.bank;
    const double e = d - s.hw - s.F;
    const double top = L + s.bank;
    if (e <= 0 || M <= top) return top;
    const double W = s.notch ? std::min((double)s.wfix, 6 + s.bat * (M - top)) : std::min((double)s.reach - s.hw - s.F, 12 + s.bat * (M - top));
    const double u = e / W;
    return u >= 1 ? M : top + (M - top) * u * (2 - u);
}
// (Scenery rocks off the trails (slope rocks, clusters' companions): their single hull is what they are to the physics too, not cut
// into convex pieces (world.h kWholeRockSeeds: a few dm of slack, a fraction of the memory). Seeds in that range, by a hash.)
inline uint32_t wholeRockSeed(uint32_t h) { return kWholeRockSeeds + 0xC00000u + (h & 0x3FFFFFu); }
// (Placement math with + - * / and sqrt alone, so every machine makes the same bytes: no libm atan, sin or pow.)
inline double detAtan2(double y, double x) {  // (within ~1e-5 rad)
    const double ax = std::fabs(x), ay = std::fabs(y), mx = std::max(ax, ay);
    if (mx <= 0) return 0;
    const double t = std::min(ax, ay) / mx, t2 = t * t;
    double r = t * (0.9998660 + t2 * (-0.3302995 + t2 * (0.1801410 + t2 * (-0.0851330 + t2 * 0.0208351))));
    if (ay > ax) r = 1.5707963267948966 - r;
    if (x < 0) r = 3.141592653589793 - r;
    return y < 0 ? -r : r;
}
const double kDir8[8][2] = {{1, 0}, {0.70710678118654752, 0.70710678118654752}, {0, 1}, {-0.70710678118654752, 0.70710678118654752},
                            {-1, 0}, {-0.70710678118654752, -0.70710678118654752}, {0, -1}, {0.70710678118654752, -0.70710678118654752}};
// A creek's or dry wash's bed as it lies here, not one stamped section (owner: "the dry river bed is the exact same width and depth
// everywhere"): wider and shallower where its valley's floor is open (sandy flats, braids), narrower and deeper where it's confined,
// varying along its course; a dry one meanders across its floor, cutting a steep bank on the outside of each bend and leaving a gentle
// bar on the inside. (Wet creeks vary in width and depth alone: their water lies where the bed is.) Rivers with water keep their own.
// Returns the bed's height (L: its level) where it's in the bed or its bank, else 1e30; hwOut: its half-width here.
inline bool naturalBed(const DrainSeg& s) { return !s.fall && !s.wall && (s.cls <= 1 || (s.wet < 0.5 && s.cls == 2)); }  // (a waterfall's stretches keep their own)
double naturalBedAt(const ProcWorld::Params& P, const DrainSeg& s, double ox, double oz, double px, double pz, double L, double& hwOut) {
    const double vx = px - s.ax, vz = pz - s.az, a0 = vx * s.dx + vz * s.dz;
    const double a = std::min((double)s.len, std::max(0.0, a0));
    const double gx = ox + s.ax + s.dx * a, gz = oz + s.az + s.dz * a;  // (the middle of the channel here, global: the same on either piece)
    double lat = s.dx * (pz - s.az - s.dz * a) - s.dz * (px - s.ax - s.dx * a);  // (signed: which side of it)
    if (a0 != a) lat = std::copysign(std::sqrt(lat * lat + (a0 - a) * (a0 - a)), lat == 0 ? 1.0 : lat);  // (past an end: as far as it really is)
    const double hw0 = std::max(0.5, (double)s.hw), open = smoothstep(1.5, 10.0, s.F) * (s.wall || s.fall ? 0.0 : 1.0);
    const double nw = smoothstep(0.2, 0.8, vnoise(P.s[S_CREEK] + 11, gx / 45.0, gz / 45.0));  // (0..1: narrow and deep .. wide and shallow)
    const bool dry = s.wet < 0.5;
    double hw, dep, off = 0;
    if (dry) {
        hw = hw0 * (0.5 + 1.1 * nw) * (1 + 0.8 * open);
        dep = std::max(0.15, (double)s.depth * (1.5 - 0.9 * nw) * (1.25 - 0.6 * open));
        const double lam = 30 + 10 * hw0;  // (the meanders' length grows with the channel)
        off = (0.3 + 0.8 * open) * hw0 * (2 * vnoise(P.s[S_CREEK] + 12, gx / lam, gz / lam) - 1);
    } else {
        hw = hw0 * (0.75 + 0.5 * nw);
        dep = (double)s.depth * (1.25 - 0.5 * nw);
    }
    const double r = lat - off;  // (across the bed as it lies here)
    const bool outside = dry && r * off > 0;  // (the side the bed has swung toward: the outside of its bend)
    const double side = outside ? hw : dry ? hw * (1 + 0.4 * std::fabs(off) / std::max(0.5, hw0)) : hw;
    const double bankW = outside ? 0.5 : 1.5;
    hwOut = side;
    const double d = std::fabs(r);
    if (d >= side + bankW) return 1e30;
    if (d >= side) return L + s.bank * (d - side) / bankW;
    const double u = d / side, up = outside ? u * u * u * u * u : dry ? u * std::sqrt(u) : u * u;  // (a cut bank's wall, a bar's slope, a creek's bowl)
    double b = L - dep * (1 - up);
    if (dry && open > 0) b += dep * 0.5 * open * (vnoise(P.s[S_CREEK] + 13, px * 0.16 + ox * 0.16, pz * 0.16 + oz * 0.16) - 0.5) * (1 - u);  // (braids on the flats)
    return std::min(b, L + s.bank);
}
// Where point (px, pz) (relative to the tile) is along the piece: the distance from it, and how far along (0..1).
inline void segNear(const DrainSeg& s, double px, double pz, double& d, double& t, double& behind) {
    const double vx = px - s.ax, vz = pz - s.az;
    double a = vx * s.dx + vz * s.dz;
    behind = a < 0 ? -a : 0;
    a = a < 0 ? 0 : a > s.len ? s.len : a;
    d = std::sqrt(sqr(vx - s.dx * a) + sqr(vz - s.dz * a));
    t = a / s.len;
}
}  // namespace

// The land at a point from the channels near it (indices into a tile's pieces): the mass, cut by the lowest valley, crags on it.
namespace {
// The mass the valleys are cut into at a point (and the route and climate there): the valley floors' rise and the uplift over them,
// and its rough: crags on the high ground (ridged, rocky), rolling knolls in the lowland, spurs and hollows everywhere; the desert's
// dunes and stepped mesas. (All of it in the mass, so a valley's side rises to it and it fades to nothing down at the floor: the land
// stays continuous.)
struct Mass {
    double M = 0, wet = 0, heat = 0;
    Route r;
    double lw[PB_COUNT] = {};
};
Mass massAt(const ProcWorld::Params& P, double x, double z) {
    const double S = P.scale;
    const RangeRule& RR = rules().range;
    Mass ms;
    ms.wet = wetAt(P, x, z), ms.heat = heatAt(P, x, z);
    const Route& r = ms.r = routeAt(P, x, z, ms.wet, ms.heat);
    double M = r.R + r.U;
    double rough = 0;
    if (r.mk > 0.01) {
        const double rr = ridgedMF(P.s[S_RIDGE], x / RR.crestSpacing / S, z / RR.crestSpacing / S, 3);
        rough += r.mk * kCrag * RR.peakHeight * P.relief * rr * rr;
        // (The summits: on the range's axis alone, peaks over the mass, broken by the same ridged noise. The trails and the valleys
        // keep to the lower land; the rivers' valleys still cut up into them.)
    }
    if (r.summit > 0) {  // (...and carved into crests and spurs with gullies between, a ridged field twice the crags' size: not domes)
        const double rs = ridgedMF(P.s[S_RIDGE] + 77, x / (2 * RR.crestSpacing * S), z / (2 * RR.crestSpacing * S), 3);
        rough -= RR.summitCarve * r.summit * (1 - rs);
    }
    if (r.mk < 0.99) rough += (1 - r.mk) * (1 - r.bad) * 24 * fbm(P.s[S_HILL1], x * 0.0012 / S, z * 0.0012 / S, 3);
    rough += (3 + 10 * r.mk * r.mk) * (2 * fbm(P.s[S_HILL2], x * 0.0042 / S + 3.0, z * 0.0042 / S - 1.0, 3) - 1);
    rough += (1 + 3 * r.mk * r.mk) * (2 * fbm(P.s[S_TERR] + 3, x * 0.012, z * 0.012, 2) - 1);
    M += rough;
    lowWeights(P, ms.wet, ms.heat, r.mk, ms.lw);
    const double low = 1 - smoothstep(0.15, 0.55, r.mk);
    const double dunes = smoothstep(0.2, 0.6, ms.lw[PB_DUNES]) * low, bench = smoothstep(0.25, 0.6, ms.lw[PB_BADLANDS]) * low;
    if (bench > 0) {  // (the mesas stepped in benches with steep risers: continuous)
        const double step = 9.0, f = M / step + 0.35 * (fbm(P.s[S_TERR], x * 0.006, z * 0.006, 2) - 0.5), fl = std::floor(f);
        const double q = step * (fl + smoothstep(0.5, 1.0, f - fl));
        M += (q - M) * bench;
    }
    if (dunes > 0) {  // (sand ridges across the wind)
        const double u = (x * P.windX + z * P.windZ) / 52.0 + 2.2 * fbm(P.s[S_DUNE], x * 0.0035, z * 0.0035, 2);
        const double t = u - std::floor(u);
        const double prof = smoothstep(0.0, 0.74, t) * (1 - smoothstep(0.74, 1.0, t));
        const double amp = 4 + 9 * fbm(P.s[S_DUNE2], x * 0.002, z * 0.002, 2);
        M += amp * prof * dunes;
    }
    ms.M = M;
    return ms;
}
}  // namespace

namespace {
// The land with the rivers' valleys cut (what the creeks' and gullies' notches are cut into): from the rivers' tile alone.
double valeAt(const ProcWorld& G, double x, double z) {
    const Mass ms = massAt(G.params(), x, z);
    const auto T = tileAt(G, x, z, true);
    const int a = std::min(kTC - 1, std::max(0, (int)std::floor((x - T->ox) / kTCell))), b = std::min(kTC - 1, std::max(0, (int)std::floor((z - T->oz) / kTCell)));
    const int c = a * kTC + b;
    const double px = x - T->ox, pz = z - T->oz;
    double env = ms.M;
    for (uint32_t k = T->start[c]; k < T->start[c + 1]; k++) {
        const DrainSeg& s = T->segs[T->idx[k]];
        if (px < std::min(s.ax, s.bx) - s.reach || px > std::max(s.ax, s.bx) + s.reach || pz < std::min(s.az, s.bz) - s.reach || pz > std::max(s.az, s.bz) + s.reach) continue;
        double d, t, bh;
        segNear(s, px, pz, d, t, bh);
        if (d > s.reach) continue;
        env = std::min(env, valleyAt(s, d, s.la + (s.lb - s.la) * t, ms.M, false, bh));
    }
    return env;
}
// ...the same with the mass smooth (no crags or knolls): the land the creeks and gullies drain down (second pass).
double valeSmoothAt(const ProcWorld& G, double x, double z) {
    const ProcWorld::Params& P = G.params();
    const Route r = routeAt(P, x, z, wetAt(P, x, z), heatAt(P, x, z));
    const double M = r.R + r.U;
    const auto T = tileAt(G, x, z, true);
    const int a = std::min(kTC - 1, std::max(0, (int)std::floor((x - T->ox) / kTCell))), b = std::min(kTC - 1, std::max(0, (int)std::floor((z - T->oz) / kTCell)));
    const int c = a * kTC + b;
    const double px = x - T->ox, pz = z - T->oz;
    double env = M;
    for (uint32_t k = T->start[c]; k < T->start[c + 1]; k++) {
        const DrainSeg& s = T->segs[T->idx[k]];
        if (px < std::min(s.ax, s.bx) - s.reach || px > std::max(s.ax, s.bx) + s.reach || pz < std::min(s.az, s.bz) - s.reach || pz > std::max(s.az, s.bz) + s.reach) continue;
        double d, t, bh;
        segNear(s, px, pz, d, t, bh);
        if (d > s.reach) continue;
        env = std::min(env, valleyAt(s, d, s.la + (s.lb - s.la) * t, M, false, bh));
    }
    return env;
}
}  // namespace

double lakeLevelOf(const ProcWorld& G, const Lake& L);
bool lakeOkOf(const ProcWorld& G, const Lake& L);
namespace {
// Does the land of node (ai, aj) drain down to node (ni, nj) (within a few km)?
bool drainsTo(const ProcWorld& G, int64_t ai, int64_t aj, int64_t ni, int64_t nj) {
    for (int k = 0; k < 40; k++) {
        if (ai == ni && aj == nj) return true;
        const int d = down2Of(G, ai, aj);
        if (d < 0) return false;
        ai += kN8[d][0], aj += kN8[d][1];
    }
    return false;
}
// A lake's dam across its valley at a point (-1e300: none here): its crest the lip's level (in the notch where the channel crosses)
// or a little over it, falling away 0.35 a metre up and down the valley and fading out past the valley's sides.
// (A dam bows out downstream in the middle, like a moraine or a rock bar, not a straight wall: its line runs back upstream toward its
// ends, by c^2 / 2R with R the dam's half-length.)
inline double damBow(const Lake& L, double c) { return c * c / (2 * std::max(30.0, L.lat)); }
double damAt(const Lake& L, double x, double z) {
    if (L.kind < 2) return -1e300;
    if (L.kind == 2) {  // (a tarn's: a horseshoe moraine round the downhill half of its floor, the outlet's notch where the creek leaves)
        const double vx = x - L.ex, vz = z - L.ez, r = std::sqrt(vx * vx + vz * vz);
        const double rd = std::sqrt(sqr(L.qx - L.ex) + sqr(L.qz - L.ez)), ring = std::fabs(r - rd);  // (through the lip)
        if (ring > 40 || r < 1e-6) return -1e300;
        const double cs = (vx * L.fx + vz * L.fz) / r;  // (1 at the lip, -1 at the back wall)
        if (cs < -0.45) return -1e300;
        const double nx = x - L.qx, nz = z - L.qz, notchD = std::sqrt(nx * nx + nz * nz);
        return L.W + L.crest * smoothstep(L.notch, L.notch + 6, notchD) - 0.35 * std::max(0.0, ring - 2) - 3 * std::max(0.0, -0.3 - cs);
    }
    const double vx = x - L.qx, vz = z - L.qz, c = std::fabs(-vx * L.fz + vz * L.fx), b = vx * L.fx + vz * L.fz + damBow(L, c);
    if (std::fabs(b) > 40 || c > L.lat + 60) return -1e300;
    return L.W + L.crest * smoothstep(L.notch, L.notch + 6, c) - 0.35 * std::max(0.0, std::fabs(b) - 2) - 0.5 * std::max(0.0, c - L.lat);
}
// A tarn's cirque floor (1e300: none here): flat under the water, then a 1:8 shore, steepening further out (where it meets the slope
// above, the cirque's back wall).
double dishAt(const Lake& L, double x, double z) {
    if (L.kind < 2) return 1e300;
    const double vx = x - L.ex, vz = z - L.ez;
    double r;
    if (L.kind == 2) r = std::sqrt(vx * vx + vz * vz);
    else {  // (stretched along the valley: the half-length ea counts as the half-width)
        const double u = (vx * L.fx + vz * L.fz) * L.dishR / L.ea, v = -vx * L.fz + vz * L.fx;
        r = std::sqrt(u * u + v * v);
    }
    // (Flat under the water, a 1:8 shore to 0.6 m over it, then up steeply: it shapes the shore, it doesn't cut into the hills round.)
    const double e = std::max(0.0, r - L.dishR), e1 = 8 * (L.dishD + 0.6);
    if (e <= e1) return L.W - L.dishD + e / 8;
    if (e - e1 > 90) return 1e300;
    return L.W + 0.6 + 1.5 * (e - e1) + 0.05 * sqr(e - e1);
}
// Is a point (whose ground is under the water's level, its valley that of node fi, fj) in the lake: upstream of the lip and draining to it.
bool inLake(const ProcWorld& G, const Lake& L, double x, double z, bool hasFloor, int64_t fi, int64_t fj) {
    if (L.kind == 1) return hasFloor && drainsTo(G, fi, fj, L.ni, L.nj) && sqr(x - L.cx) + sqr(z - L.cz) < sqr(L.reach);
    const double vx = x - L.qx, vz = z - L.qz;
    if (vx * L.fx + vz * L.fz + damBow(L, std::fabs(-vx * L.fz + vz * L.fx)) > -0.5 || vx * vx + vz * vz > sqr(L.reach)) return false;
    const double dish = dishAt(L, x, z);
    if (dish < L.W + 0.5) return true;  // (over its shaped floor)
    if (L.kind == 2 || L.kind == 4) return false;  // (a tarn or a pond: its own floor alone)
    // (a valley lake: on up its valley and its inlets as far as the land is under the lip: land draining to its node, or to the node
    // below it short of the lip)
    return hasFloor && (drainsTo(G, fi, fj, L.ni, L.nj) || (fi == L.di && fj == L.dj));
}
}  // namespace

// The land at a point from the channels near it (indices into a tile's pieces, the rivers' first): the mass, cut by the rivers'
// valleys (the lowest of them), then by the creeks' and gullies' notches; the lakes' cirque floors and dams; and their water.
static Macro evalMacro(const ProcWorld& G, double x, double z, const DTile& T, const uint16_t* idx, int n, bool withLakes = true) {
    const RangeRule& RR = rules().range;
    Macro m;
    const Mass ms = massAt(G.params(), x, z);
    const double M = ms.M;
    m.wet = ms.wet, m.heat = ms.heat, m.mass = M, m.rug = ms.r.mk;
    const double px = x - T.ox, pz = z - T.oz;
    double env = M, Lmin = M, rd = 1e9, cd = 1e9, vale = M;
    bool valeDone = false;
    const DrainSeg *rs = nullptr, *cs = nullptr;
    double rt = 0, ct = 0;
    int64_t fi = 0, fj = 0;
    bool hasFloor = false;
    for (int k = 0; k < n; k++) {
        const DrainSeg& s = T.segs[idx[k]];
        if (s.notch && !valeDone) vale = env, valeDone = true;
        // (Quick reject: further than its reach from either end's box.)
        if (px < std::min(s.ax, s.bx) - s.reach || px > std::max(s.ax, s.bx) + s.reach || pz < std::min(s.az, s.bz) - s.reach || pz > std::max(s.az, s.bz) + s.reach) continue;
        double d, t, bh;
        segNear(s, px, pz, d, t, bh);
        if (d > s.reach) continue;
        const double L = s.la + (s.lb - s.la) * t;
        const double v = valleyAt(s, d, L, s.notch ? vale : M, false, bh);
        if (v < env) env = v, Lmin = L, m.floorArea = s.A, fi = s.ni, fj = s.nj, hasFloor = true;
        if (s.cls >= 2 && d < rd) rd = d, rs = &s, rt = t;
        if (s.cls == 1 && d < cd) cd = d, cs = &s, ct = t;
    }
    if (!valeDone) vale = env;
    if (withLakes && rules().erosion.on <= 0) {
        for (const Lake& L : T.lakes) {  // (a lake's shaped floor: cut)
            const double v = dishAt(L, x, z);
            if (v < env && !lakeOkOf(G, L)) continue;
            if (v < env) env = v, Lmin = std::min(Lmin, L.W);
        }
        for (const Lake& L : T.lakes) {  // (the dams: a low ridge across the valley, over the land)
            double v = damAt(L, x, z);
            if (v > -1e299 && !lakeOkOf(G, L)) v = -1e300;
            if (v > -1e299) m.damH = std::max(m.damH, v);
            if (v > env) env = v;
        }
        for (const Lake& L : T.lakes) {  // (the water: where the ground's under the lip, in the lake's valley, upstream of the lip)
            const double W = L.kind == 1 ? lakeLevelOf(G, L) : L.W;
            if (env < W && W > m.lakeLevel && inLake(G, L, x, z, hasFloor, fi, fj) && lakeOkOf(G, L)) m.lakeLevel = W, Lmin = std::min(Lmin, W);
        }
    }
    m.floor = Lmin, m.vale = vale;
    if (hasFloor) m.floorNode = fi, m.floorNode2 = fj;
    m.h = env;

    // ---- The biomes: up high, the mountains; below, by the climate ----
    // (Mesa country stays desert however high its tops stand: no alpine meadow on a mesa)
    const double alpine = smoothstep(RR.alpineFrom, RR.alpineFull, m.h + RR.alpineHeatShift * (0.5 - m.heat) * 2) * (1 - smoothstep(0.15, 0.35, ms.lw[PB_BADLANDS]));
    for (int b = 0; b < PB_COUNT; b++) m.w[b] = b == PB_MOUNTAINS ? alpine : ms.lw[b] * (1 - alpine);
    // ---- The knolls (procrules.h KnollRule): lumpy hills, saddles and gullies over what the valleys left ----
    {
        const KnollRule& KR = rules().knoll;
        const ProcWorld::Params& P = G.params();
        const double S = P.scale;
        const double K = KR.knollHeight * 1.6 * (fbm(P.s[S_GROUND1] + 41, x / (KR.knollSize * S), z / (KR.knollSize * S), 4) - 0.5) +
                         KR.hillHeight * 1.6 * (fbm(P.s[S_GROUND2] + 41, x / (KR.hillSize * S) + 3.7, z / (KR.hillSize * S) - 1.9, 3) - 0.5) +
                         KR.knobHeight * 1.6 * (ridgedMF(P.s[S_GROUND1] + 43, x / (KR.knobSize * S), z / (KR.knobSize * S), 3) - 0.3);
        const double bm = 1 - (1 - KR.desert) * (m.w[PB_DUNES] + m.w[PB_BADLANDS]) - (1 - KR.mountains) * m.w[PB_MOUNTAINS];
        const double over = env - Lmin;
        const double bank = std::min(rs ? rd - rs->hw : 1e9, cs ? cd - cs->hw : 1e9);
        // (No hollow within 60 m of a creek's or river's bank: one valley's water may run higher than the next's floor, and a hollow
        // there would put the land beside it under its water.)
        // (And none on a channel itself or its banks: a waterfall's lip and pool, a ford, stay as the water made them.)
        const double wneg = smoothstep(0.0, KR.floorClear, over) * smoothstep(4.0, 60.0, bank);
        // (rising gently from the bank: no steep step up beside the water; a river's floor, where its lakes stand, wider)
        const double wpos = std::max(smoothstep(0.0, KR.floorClear, over), 0.5 * smoothstep(4.0, KR.channelClear, bank)) * smoothstep(1.0, 25.0, bank) *
                            smoothstep(0.0, 70.0, rs ? rd - rs->hw : 1e9);
        const double k = bm * (K >= 0 ? K * wpos : K * wneg);
        m.h = std::max(Lmin + 0.5 * std::max(0.0, over), env + k);
    }
    // ---- The mesas (procrules.h MesaRule; Monument Valley): over the land the valleys left, flat-topped at the desert floor's level
    // and a mesa's height over it, a sheer band on a talus slope, lone spires; the rivers and creeks cut canyons through them ----
    {
        const double bench = smoothstep(0.3, 0.6, m.w[PB_BADLANDS]);  // (wherever the mesa biome holds, in the ranges' foothills too)
        if (bench > 0) {
            const MesaRule& MR = rules().mesa;
            const ProcWorld::Params& P = G.params();
            const double f = fbm(P.s[S_MESA] + 3, x / (MR.size * P.scale), z / (MR.size * P.scale), 3);
            const double talus = smoothstep(0.46, 0.56, f);
            // (Breaks in the band here and there: a steep ramp up through it, the way onto the top (the trails find them))
            const double brk = smoothstep(0.72, 0.8, vnoise(P.s[S_MESA] + 21, x / 90.0, z / 90.0));
            const double band = smoothstep(0.565, 0.58, f) * (1 - brk) + smoothstep(0.5, 0.62, f) * brk;
            double shape = (1 - MR.cliff) * talus + MR.cliff * band;
            const double sp = ridgedMF(P.s[S_MESA] + 9, x / 140.0, z / 140.0, 2);  // (spires: needles where the ridged field peaks)
            shape = std::max(shape, 0.6 * MR.spires * smoothstep(0.82, 0.9, sp) * (1 - talus) * 1.6);
            const double top = ms.r.R + 30 + MR.height * P.relief * shape;  // (the desert floor, 30 m of fill, then the mesa)
            const double bank = std::min(rs ? rd - rs->hw : 1e9, cs ? cd - cs->hw : 1e9);
            const double wch = smoothstep(6.0, 45.0, bank);  // (the canyons: a channel's water cuts down through it)
            if (top > m.h) m.h += (top - m.h) * bench * wch * smoothstep(0.0, 0.02, shape);
        }
    }
    // The water near: the nearest river's and creek's channel.
    if (rs) {
        const DrainSeg& s = *rs;
        m.riverS = rd, m.riverHW = s.hw, m.riverDepth = s.depth;
        m.riverLevel = s.la + (s.lb - s.la) * rt, m.riverOn = s.wet, m.flowX = s.dx, m.flowZ = s.dz, m.riverArea = s.A, m.bigRiver = s.cls == 3, m.riverFord = s.ford;
    }
    if (cs) {
        const DrainSeg& s = *cs;
        m.creekS = cd, m.creekHW = s.hw, m.creekDepth = s.depth, m.creekLevel = s.la + (s.lb - s.la) * ct, m.creekOn = s.wet, m.cflowX = s.dx, m.cflowZ = s.dz;
        m.creekNode = s.ni, m.creekNodeJ = s.nj;
    }
    return m;
}

// A valley lake's natural dam (a rock bar or a landslide across the valley) stands on the land before it's worn: a share of the sites.
inline bool damOn(const ProcWorld& G, const Lake& L) { return L.kind == 3 && hash01(G.seed() ^ 0xDA3ull, L.ni, L.nj) < rules().erosion.lakeShare; }
// The dam itself (-1e300: none here): a broad mound across the valley at the site's lip, 4-16 m over the channel there (by the site's
// dice: varied lakes), sides 1:4 up and down the valley; its crest a metre over the spill, which is where the channel crosses (the
// water leaves by its own creek). Only on the ground of this valley (the land draining to the site, or just below it): it never
// spills over a ridge into the next one.
double rockDamAt(const ProcWorld& G, const Lake& L, double x, double z, int64_t fi, int64_t fj) {
    const double H = rules().erosion.damMin + (rules().erosion.damMax - rules().erosion.damMin) * hash01(G.seed() ^ 0xDA4ull, L.ni, L.nj);
    const double spill = L.W + H;
    const double vx = x - L.qx, vz = z - L.qz, c = std::fabs(-vx * L.fz + vz * L.fx), b = std::fabs(vx * L.fx + vz * L.fz + damBow(L, c));
    const double reachB = 6 + 4 * (H + 1);
    if (b > reachB || c > 900) return -1e300;
    if (fi == INT64_MIN || !(drainsTo(G, fi, fj, L.ni, L.nj) || (fi == L.di && fj == L.dj))) return -1e300;
    return spill + smoothstep(L.notch + 2, L.notch + 10, c) - 0.25 * std::max(0.0, b - 6);
}

thread_local bool gTileWorker = false;  // (this thread makes tiles for prewarmErosion: no threads of its own)

// ---- Erosion (proceros.h): the land worn by water and frost, in fixed tiles ---------------------------------------------------------
// The land (evalMacro, without lakes) is sampled every kECell m over a kETile square tile and kEMargin round it, worn down by
// erodeGrid, and what that changed (the worn land less the land) is kept over the tile and kEBlend round it. A point takes its own
// tile's change, and near a tile's edge (within kEBlend) a smooth blend of the tiles either side: the same from both sides, and every
// point in a blend band is at least kEMargin - kEBlend from the edge of every grid that wore it.
namespace {
constexpr int64_t kETile = 2048;
constexpr int kECell = 8, kEBlend = 128, kEMargin = 384;  // (8 m: the land sampled and worn every 8 m, read between by bilinear blend)
constexpr int kEN = (int)((kETile + 2 * kEBlend) / kECell) + 1;   // the change's grid: cells a side
constexpr int kEGrid = (int)((kETile + 2 * kEMargin) / kECell) + 1;  // the worn grid: cells a side
struct ETile {
    double ox = 0, oz = 0;  // the change's grid's corner (global)
    std::vector<float> d;   // kEN x kEN, a * kEN + b (a along x)
    std::vector<float> lake;  // kEN x kEN: the water's level where the worn land holds a lake (-1e30: none)
    struct Lk { double x, z, level, area, depth, lipX, lipZ; };
    std::vector<Lk> lakes;    // (its lakes: the middle, the level, m2, the deepest, where it spills (global))
    double ms = 0;          // how long it took
};
struct ESlot {
    std::shared_future<std::shared_ptr<const ETile>> f;
    uint64_t used = 0;
};
std::mutex eMutex;
std::unordered_map<TKey, ESlot, TKHash> eMap;
std::atomic<uint64_t> eClock{0}, eMade{0};

void sampleErosionGrid(const ProcWorld& G, int64_t ti, int64_t tj, ErosionGrid& g) {
    const ErosionRule& ER = rules().erosion;
    const double gx0 = (double)(ti * kETile - kEMargin), gz0 = (double)(tj * kETile - kEMargin);
    const int n = kEGrid;
    g.n = n, g.cell = kECell;
    const size_t N = (size_t)n * n;
    g.h.resize(N), g.kErode.resize(N), g.gDep.resize(N), g.talus.resize(N), g.creep.resize(N), g.fixed.assign(N, 0), g.floor.resize(N);
    // (The land, without lakes: a pure function of the point, so the rows can be made on any number of threads. CZ_GEN_THREADS caps
    // them: tests, and while someone plays.)
    const char* cap = std::getenv("CZ_GEN_THREADS");
    const int saveCap = parallelRowsCap;
    if (cap && *cap) parallelRowsCap = std::max(1, std::atoi(cap));
    if (gTileWorker) parallelRowsCap = 1;  // (prewarm's own threads: one each, so the total stays under its cap)
    parallelRows(n, [&](int a) {
        for (int b = 0; b < n; b++) {
            const double x = gx0 + (double)a * kECell, z = gz0 + (double)b * kECell;
            const auto T = tileAt(G, x, z);
            const int ca = std::min(kTC - 1, std::max(0, (int)std::floor((x - T->ox) / kTCell))), cb = std::min(kTC - 1, std::max(0, (int)std::floor((z - T->oz) / kTCell)));
            const int c = ca * kTC + cb;
            const Macro m = evalMacro(G, x, z, *T, T->idx.data() + T->start[c], (int)(T->start[c + 1] - T->start[c]), false);
            const size_t i = (size_t)a * n + b;
            double h = m.h;
            // (The lakes' natural dams (a moraine, a rock bar) across their valleys, on the land before it's worn: the water is then
            // whatever the worn land holds behind them. No floor is shaped: nothing is dug.)
            for (const Lake& L : T->lakes)
                if (damOn(G, L)) h = std::max(h, rockDamAt(G, L, x, z, m.floorNode, m.floorNode2));  // (valleys' natural dams alone: the owner's choice)
            g.h[i] = (float)h;
            g.floor[i] = m.floorNode != INT64_MIN ? (float)(m.floor + 0.3) : -1e30f;
            if (h > m.h) g.floor[i] = (float)std::max((double)g.floor[i], h - 1.0);  // (a dam is rock: it weathers a little, never breached)
            // (Within 35 m of a channel with water, never worn under its water: a gully beside a creek doesn't leave it standing over
            // the land.)
            if (m.creekOn > 0.5 && m.creekS < 35) g.floor[i] = (float)std::max((double)g.floor[i], m.creekLevel + 0.35);
            if (m.riverOn > 0.5 && m.riverS < 35) g.floor[i] = (float)std::max((double)g.floor[i], m.riverLevel + 0.35);
            double ke = 0, gd = 0, ta = 0, cr = 0;
            for (int k = 0; k < PB_COUNT; k++) {
                const ErosionBiomeRule& B = ER.biome[k];
                ke += m.w[k] * B.cut, gd += m.w[k] * B.deposit, ta += m.w[k] * B.talus, cr += m.w[k] * B.creep;
            }
            g.kErode[i] = (float)ke, g.gDep[i] = (float)gd, g.talus[i] = (float)ta, g.creep[i] = (float)cr;
            // (A channel's water is never worn: its bed and level are the drainage's. The grid's edge holds.)
            const bool wet = (m.riverOn > 0.5 && m.riverS < m.riverHW + kECell * 0.5) || (m.creekOn > 0.5 && m.creekS < m.creekHW + kECell * 0.5);
            if (wet || a == 0 || b == 0 || a == n - 1 || b == n - 1) g.fixed[i] = 1;
        }
    }, 8);
    parallelRowsCap = saveCap;
}

std::shared_ptr<const ETile> makeErosionTile(const ProcWorld& G, int64_t ti, int64_t tj) {
    const auto t0 = std::chrono::steady_clock::now();
    const ErosionRule& ER = rules().erosion;
    const int n = kEGrid;
    ErosionGrid g;
    sampleErosionGrid(G, ti, tj, g);
    {  // (The waterfalls' steps aren't worn: their rock stands, 24 m round each lip.)
        std::set<std::pair<int64_t, int64_t>> seen;
        const double gx0 = (double)(ti * kETile - kEMargin), gz0 = (double)(tj * kETile - kEMargin);
        for (double x = gx0; x < gx0 + n * kECell + kTile; x += kTile)
            for (double z = gz0; z < gz0 + n * kECell + kTile; z += kTile) {
                const auto T = tileAt(G, std::min(x, gx0 + (n - 1) * kECell), std::min(z, gz0 + (n - 1) * kECell));
                if (!seen.insert({(int64_t)T->ox, (int64_t)T->oz}).second) continue;
                for (const DrainSeg& sg : T->segs) {
                    if (!sg.fall) continue;
                    const double lx = T->ox + sg.ax, lz = T->oz + sg.az;
                    const int a0 = (int)std::floor((lx - 24 - gx0) / kECell), a1 = (int)std::ceil((lx + 24 - gx0) / kECell);
                    const int b0 = (int)std::floor((lz - 24 - gz0) / kECell), b1 = (int)std::ceil((lz + 24 - gz0) / kECell);
                    for (int a = std::max(0, a0); a <= std::min(n - 1, a1); a++)
                        for (int b = std::max(0, b0); b <= std::min(n - 1, b1); b++)
                            if (sqr(gx0 + a * kECell - lx) + sqr(gz0 + b * kECell - lz) < 24.0 * 24.0) g.fixed[(size_t)a * n + b] = std::max<uint8_t>(g.fixed[(size_t)a * n + b], 1);
                }
            }
    }
    const std::vector<float> base = g.h;
    {  // (Standing water from the start (behind the valleys' dams, in the land's own hollows) is a lake through the wear: its bed isn't
       // silted up nor worn; what comes down settles out of sight.)
        std::vector<float> F;
        fillDepressions(n, g.h, F);
        for (size_t i = 0; i < F.size(); i++)
            if (F[i] - g.h[i] > 0.1f) g.fixed[i] = 2;
    }
    ErosionRun run;
    // (One resolution, the grid's own (8 m): the steps the prototype ran on its 8 m pass. A 4 m pass after it (rules: iterations) would
    // need the land sampled every 4 m: four times the cost, for a little smoothing the bilinear read gives anyway.)
    run.coarseIterations = 0, run.iterations = (int)ER.coarseIterations, run.maxCut = ER.maxCut;
    run.glacierIterations = (int)ER.glacierIterations, run.glacierLine = ER.glacierLine, run.glacierCut = ER.glacierCut, run.pitFill = ER.pitFill;
    const double sampleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    erodeGrid(g, run);
    auto t = std::make_shared<ETile>();
    t->ox = (double)(ti * kETile - kEBlend), t->oz = (double)(tj * kETile - kEBlend);
    t->d.resize((size_t)kEN * kEN);
    const int off = (kEMargin - kEBlend) / kECell;
    const float lim = (float)ER.maxDepth;
    std::vector<float> fin(g.h.size());
    for (size_t i = 0; i < fin.size(); i++) fin[i] = base[i] + std::max(-lim, std::min(lim, g.h[i] - base[i]));
    for (int a = 0; a < kEN; a++)
        for (int b = 0; b < kEN; b++) {
            const size_t i = (size_t)(a + off) * n + (b + off);
            t->d[(size_t)a * kEN + b] = fin[i] - base[i];
        }
    {  // (The lakes: the worn ground's closed hollows, filled to their spill points. Only those wholly inside the tile, clear of the
       // blend bands: their water and its shore are this tile's ground alone, the same from either side.)
        std::vector<float> lv;
        std::vector<LakeInfo> info;
        findLakes(n, kECell, fin, lv, 0.5e4, 1.0, &info);
        // (Each lake belongs to the tile its middle is in, and may run out over the blend band round it (its change grid), not past.)
        const int lo = off + 2, hi = off + kEN - 3, c0 = kEMargin / kECell, c1 = kEMargin / kECell + (int)kETile / kECell;
        t->lake.assign((size_t)kEN * kEN, -1e30f);
        std::vector<uint8_t> keep(n > 0 ? (size_t)n * n : 0, 0);
        const double gx0 = (double)(ti * kETile - kEMargin), gz0 = (double)(tj * kETile - kEMargin);
        // (The water a valley's natural dam holds (the owner's choice): water within 60 m of a dam's lip; and the biggest of
        // the worn land's own hollows. Its small ones stay dry.)
        std::vector<std::pair<double, double>> lips;
        {
            std::set<std::pair<int64_t, int64_t>> seen;
            for (double x = gx0; x < gx0 + n * kECell + kTile; x += kTile)
                for (double z = gz0; z < gz0 + n * kECell + kTile; z += kTile)
                    for (const Lake& L : tileAt(G, std::min(x, gx0 + (n - 1) * kECell), std::min(z, gz0 + (n - 1) * kECell))->lakes)
                        if (damOn(G, L) && seen.insert({L.ni, L.nj}).second) lips.push_back({L.qx, L.qz});
        }
        for (const LakeInfo& L : info) {
            {  // (none among the mesas: the hollows between them are dry pans, and a mesa's band would stand at its shore)
                const double lx = gx0 + L.ca * kECell, lz = gz0 + L.cb * kECell;
                const auto TT = tileAt(G, lx, lz);
                const int ta = std::min(kTC - 1, std::max(0, (int)std::floor((lx - TT->ox) / kTCell))), tb = std::min(kTC - 1, std::max(0, (int)std::floor((lz - TT->oz) / kTCell)));
                const int tc = ta * kTC + tb;
                const Macro mm = evalMacro(G, lx, lz, *TT, TT->idx.data() + TT->start[tc], (int)(TT->start[tc + 1] - TT->start[tc]), false);
                if (mm.w[PB_BADLANDS] > 0.45) continue;
            }
            if (L.a0 < lo || L.b0 < lo || L.a1 > hi || L.b1 > hi || L.ca < c0 || L.ca >= c1 || L.cb < c0 || L.cb >= c1) continue;
            bool dammed = false;
            for (const auto& q : lips) {  // (its water comes within 60 m of a dam's lip: that dam holds it, whichever way it spills)
                const double qa = (q.first - gx0) / kECell, qb = (q.second - gz0) / kECell;
                const int r = 60 / kECell;
                if (qa < L.a0 - r || qa > L.a1 + r || qb < L.b0 - r || qb > L.b1 + r) continue;
                for (int a = std::max(L.a0, (int)qa - r); a <= std::min(L.a1, (int)qa + r) && !dammed; a++)
                    for (int b = std::max(L.b0, (int)qb - r); b <= std::min(L.b1, (int)qb + r) && !dammed; b++)
                        dammed = std::fabs(lv[(size_t)a * n + b] - (float)L.level) < 1e-6f;
            }
            if (dammed && (L.area < ER.lakeMinArea * 1e4 || L.depth < ER.lakeMinDepth)) continue;
            // (Water in a hollow no dam made (a sink of the drainage, a low spot the wear left): only a big, deep one.)
            if (!dammed && (L.area < ER.lakeOpenArea * 1e4 || L.depth < 2 * ER.lakeMinDepth)) continue;
            t->lakes.push_back({gx0 + L.ca * kECell, gz0 + L.cb * kECell, L.level, L.area, L.depth, gx0 + L.la * (double)kECell, gz0 + L.lb * (double)kECell});
            for (int a = L.a0; a <= L.a1; a++)
                for (int b = L.b0; b <= L.b1; b++)
                    if (std::fabs(lv[(size_t)a * n + b] - (float)L.level) < 1e-6f) keep[(size_t)a * n + b] = 1;
        }
        // (Each kept lake's cells, and the shore cells round them (ground within 0.3 m under the level, or over it): the water's edge
        // is then the land's own contour, read metre by metre, not the 8 m cells' outline. Never past the lip, where the land falls.)
        {  // (Two kept lakes side by side at nearly the same level, split by a sill a hand high: the smaller goes, no step in the water.)
            std::vector<int> lab(keep.size(), -1);
            std::vector<long> size;
            std::vector<size_t> st;
            for (size_t s0 = 0; s0 < keep.size(); s0++) {
                if (!keep[s0] || lab[s0] >= 0) continue;
                const int id = (int)size.size();
                size.push_back(0);
                st.assign(1, s0), lab[s0] = id;
                while (!st.empty()) {
                    const size_t i = st.back();
                    st.pop_back();
                    size[id]++;
                    const int a = (int)(i / n), b = (int)(i % n);
                    for (int da = -1; da <= 1; da++)
                        for (int db = -1; db <= 1; db++) {
                            const int na = a + da, nb = b + db;
                            if (na < 0 || nb < 0 || na >= n || nb >= n) continue;
                            const size_t j = (size_t)na * n + nb;
                            if (keep[j] && lab[j] < 0 && lv[j] == lv[i]) lab[j] = id, st.push_back(j);
                        }
                }
            }
            std::vector<uint8_t> drop(size.size(), 0);
            for (int a = 0; a + 2 < n; a++)
                for (int b = 0; b + 2 < n; b++) {
                    const size_t i = (size_t)a * n + b;
                    if (lab[i] < 0) continue;
                    for (int da = 0; da <= 2; da++)  // (within two cells: their shores would meet)
                        for (int db = -2; db <= 2; db++) {
                            if (b + db < 0) continue;
                            const size_t j = (size_t)(a + da) * n + b + db;
                            if (lab[j] < 0 || lab[j] == lab[i]) continue;
                            const int small = size[lab[i]] < size[lab[j]] || (size[lab[i]] == size[lab[j]] && lab[i] > lab[j]) ? lab[i] : lab[j];
                            drop[small] = 1;
                        }
                }
            for (size_t i = 0; i < keep.size(); i++)
                if (lab[i] >= 0 && drop[lab[i]]) keep[i] = 0;
            // (and the dropped one's record)
            std::vector<ETile::Lk> kept;
            for (const ETile::Lk& L : t->lakes) {
                const int a = (int)std::lround((L.x - gx0) / kECell), b = (int)std::lround((L.z - gz0) / kECell);
                bool any = false;
                for (int da = -3; da <= 3 && !any; da++)
                    for (int db = -3; db <= 3 && !any; db++) {
                        const int na = a + da, nb = b + db;
                        if (na >= 0 && nb >= 0 && na < n && nb < n) any = keep[(size_t)na * n + nb] && std::fabs(lv[(size_t)na * n + nb] - (float)L.level) < 1e-6f;
                    }
                if (any) kept.push_back(L);
            }
            t->lakes.swap(kept);
        }
        std::vector<float> wl(keep.size(), -1e30f);
        for (size_t i = 0; i < keep.size(); i++)
            if (keep[i]) wl[i] = lv[i];
        for (int a = 1; a + 1 < n; a++)
            for (int b = 1; b + 1 < n; b++) {
                const size_t i = (size_t)a * n + b;
                if (keep[i]) continue;
                float w = -1e30f;
                for (int da = -1; da <= 1; da++)
                    for (int db = -1; db <= 1; db++)
                        if (keep[(size_t)(a + da) * n + b + db]) w = std::max(w, lv[(size_t)(a + da) * n + b + db]);
                if (w > -1e29f && fin[i] >= w - 0.3f) wl[i] = w;
            }
        for (int a = 0; a < kEN; a++)
            for (int b = 0; b < kEN; b++) t->lake[(size_t)a * kEN + b] = wl[(size_t)(a + off) * n + (b + off)];
    }
    t->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    eMade++;
    if (std::getenv("CZ_EROSION_LOG")) std::fprintf(stderr, "erosion tile %lld,%lld: %.0f ms (the land sampled in %.0f)\n", (long long)ti, (long long)tj, t->ms, sampleMs);
    return t;
}

std::shared_ptr<const ETile> erosionTile(const ProcWorld& G, int64_t ti, int64_t tj) {
    struct Slot {
        uint64_t seed = 0, epoch = 0;
        int64_t i = 0, j = 0;
        std::shared_ptr<const ETile> t;
    };
    thread_local Slot slots[4];
    thread_local int next = 0;
    const uint64_t ep = drainEpoch.load(std::memory_order_relaxed);
    for (Slot& s : slots)
        if (s.t && s.epoch == ep && s.i == ti && s.j == tj && s.seed == G.seed()) return s.t;
    std::shared_future<std::shared_ptr<const ETile>> f;
    std::promise<std::shared_ptr<const ETile>> mine;
    bool make = false;
    {
        std::lock_guard<std::mutex> lock(eMutex);
        auto it = eMap.find({G.seed(), ti, tj});
        if (it != eMap.end()) f = it->second.f, it->second.used = ++eClock;
        else {
            if (eMap.size() >= 160) {  // (~100 MB at 0.6 MB a tile: the least lately used goes; what's in use is held)
                auto old = eMap.begin();
                for (auto q = eMap.begin(); q != eMap.end(); ++q)
                    if (q->second.used < old->second.used) old = q;
                eMap.erase(old);
            }
            f = mine.get_future().share();
            eMap[{G.seed(), ti, tj}] = ESlot{f, ++eClock};
            make = true;
        }
    }
    if (make) mine.set_value(makeErosionTile(G, ti, tj));
    std::shared_ptr<const ETile> t = f.get();
    Slot& s = slots[next++ & 3];
    s.seed = G.seed(), s.epoch = ep, s.i = ti, s.j = tj, s.t = t;
    return t;
}

// A tile if it's made already (else null: never waits, never starts one).
std::shared_ptr<const ETile> erosionTileIfMade(const ProcWorld& G, int64_t ti, int64_t tj) {
    std::shared_future<std::shared_ptr<const ETile>> f;
    {
        std::lock_guard<std::mutex> lock(eMutex);
        auto it = eMap.find({G.seed(), ti, tj});
        if (it == eMap.end()) return nullptr;
        f = it->second.f;
    }
    if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return f.get();
}

// What erosion changed at a point (m: + silted, - worn away). wait false: from the tiles made already alone (the far backdrop).
double erosionAt(const ProcWorld& G, double x, double z, bool wait = true) {
    const int64_t ti = (int64_t)std::floor(x / (double)kETile), tj = (int64_t)std::floor(z / (double)kETile);
    const double u = x - (double)(ti * kETile), v = z - (double)(tj * kETile);
    int64_t is[2] = {ti, ti}, js[2] = {tj, tj};
    double wi[2] = {1, 0}, wj[2] = {1, 0};
    if (u < kEBlend) is[1] = ti - 1, wi[0] = smoothstep(-kEBlend, kEBlend, u), wi[1] = 1 - wi[0];
    else if (u > kETile - kEBlend) is[1] = ti + 1, wi[0] = 1 - smoothstep(kETile - kEBlend, kETile + kEBlend, u), wi[1] = 1 - wi[0];
    if (v < kEBlend) js[1] = tj - 1, wj[0] = smoothstep(-kEBlend, kEBlend, v), wj[1] = 1 - wj[0];
    else if (v > kETile - kEBlend) js[1] = tj + 1, wj[0] = 1 - smoothstep(kETile - kEBlend, kETile + kEBlend, v), wj[1] = 1 - wj[0];
    double sum = 0;
    for (int p = 0; p < 2; p++)
        for (int q = 0; q < 2; q++) {
            const double w = wi[p] * wj[q];
            if (w <= 0) continue;
            const auto T = wait ? erosionTile(G, is[p], js[q]) : erosionTileIfMade(G, is[p], js[q]);
            if (!T) continue;
            const double fx = (x - T->ox) / kECell, fz = (z - T->oz) / kECell;
            const int a = std::min(kEN - 2, std::max(0, (int)std::floor(fx))), b = std::min(kEN - 2, std::max(0, (int)std::floor(fz)));
            const double tx = std::min(1.0, std::max(0.0, fx - a)), tz = std::min(1.0, std::max(0.0, fz - b));
            const float* d = T->d.data();
            const double d00 = d[(size_t)a * kEN + b], d10 = d[(size_t)(a + 1) * kEN + b], d01 = d[(size_t)a * kEN + b + 1], d11 = d[(size_t)(a + 1) * kEN + b + 1];
            sum += w * ((d00 + (d10 - d00) * tx) * (1 - tz) + (d01 + (d11 - d01) * tx) * tz);
        }
    return sum;
}

// The land at a point, worn: what erosion changed added, but never lower than the floor of the valley it's in (the channel's level
// stays the drainage's: no water stands over the land beside it).
void applyErosion(const ProcWorld& G, double x, double z, Macro& m, bool wait = true) {
    if (rules().erosion.on <= 0) return;
    const double D = erosionAt(G, x, z, wait);
    double keep = -1e30;  // (as the tiles keep it, cell by cell: within 35 m of a channel's water never worn under it; here exactly,
                          // between the cells too)
    if (m.creekOn > 0.5 && m.creekS < 35) keep = std::max(keep, m.creekLevel + 0.35);
    if (m.riverOn > 0.5 && m.riverS < 35) keep = std::max(keep, m.riverLevel + 0.35);
    m.h = std::max(m.h + D, std::min(m.h, keep));
    // (A lake the worn land holds here: its own tile's, the nearest of its cells.)
    // (Each lake is its own tile's, but may run out over the blend band into the next: the tiles whose change grids hold the point.)
    const int64_t ti = (int64_t)std::floor(x / (double)kETile), tj = (int64_t)std::floor(z / (double)kETile);
    const double u = x - (double)(ti * kETile), v = z - (double)(tj * kETile);
    const int64_t i1 = u < kEBlend ? ti - 1 : u > kETile - kEBlend ? ti + 1 : ti, j1 = v < kEBlend ? tj - 1 : v > kETile - kEBlend ? tj + 1 : tj;
    for (int64_t qi : {ti, i1})
        for (int64_t qj : {tj, j1}) {
            if ((qi == i1 && i1 == ti && qi != ti) || (qj == j1 && j1 == tj && qj != tj)) continue;
            const auto T = wait ? erosionTile(G, qi, qj) : erosionTileIfMade(G, qi, qj);
            if (!T || T->lakes.empty()) continue;
            const int a = (int)std::floor((x - T->ox) / kECell + 0.5), b = (int)std::floor((z - T->oz) / kECell + 0.5);
            if (a < 0 || b < 0 || a >= kEN || b >= kEN) continue;
            const float w = T->lake[(size_t)a * kEN + b];
            if (w > -1e29f && w > m.lakeLevel) m.lakeLevel = w, m.floor = std::min(m.floor, (double)w);
        }
}
}  // namespace

namespace {
Macro macroNoPerch(const ProcWorld& G, double x, double z, bool withLakes = true) {
    const auto T = tileAt(G, x, z);
    const int a = std::min(kTC - 1, std::max(0, (int)std::floor((x - T->ox) / kTCell))), b = std::min(kTC - 1, std::max(0, (int)std::floor((z - T->oz) / kTCell)));
    const int c = a * kTC + b;
    Macro m = evalMacro(G, x, z, *T, T->idx.data() + T->start[c], (int)(T->start[c + 1] - T->start[c]), withLakes);
    applyErosion(G, x, z, m);
    return m;
}
}  // namespace
// A basin's level: filled to just under the lowest point round it (the land, without lakes, 40-140 m from its middle), and no more than
// a metre over where its channels come down to. (The other lakes' levels are their lips'.)
double lakeLevelOf(const ProcWorld& G, const Lake& L) {
    if (L.kind != 1) return L.W;
    int q;
    {
        const float w = blockFor(G, L.ni, L.nj, q)->lakeW[q].load(std::memory_order_relaxed);
        if (w == w) return w;
    }
    double rim = 1e300;
    static const double kDir[16][2] = {{1, 0}, {0.92388, 0.38268}, {0.70711, 0.70711}, {0.38268, 0.92388}, {0, 1}, {-0.38268, 0.92388}, {-0.70711, 0.70711}, {-0.92388, 0.38268},
                                       {-1, 0}, {-0.92388, -0.38268}, {-0.70711, -0.70711}, {-0.38268, -0.92388}, {0, -1}, {0.38268, -0.92388}, {0.70711, -0.70711}, {0.92388, -0.38268}};
    for (const auto& d : kDir)
        for (double f : {40.0, 80.0, 140.0}) rim = std::min(rim, macroNoPerch(G, L.cx + d[0] * f, L.cz + d[1] * f, false).h);
    const float W = (float)std::min(rim - 0.3, levelOf(G, L.ni, L.nj) + 1.0);
    blockFor(G, L.ni, L.nj, q)->lakeW[q].store(W, std::memory_order_relaxed);
    return W;
}
// Does a lake hold (its floor's rim, the land without lakes just past its shaped floor, over its water all round but across its lip)?
// Worked out when first asked for, memoised. (A basin's level is its rim's already.)
bool lakeOkOf(const ProcWorld& G, const Lake& L) {
    if (L.kind == 1) return true;
    int q;
    {
        const int o = blockFor(G, L.ni, L.nj, q)->lakeOk[q].load(std::memory_order_relaxed);
        if (o >= 0) return o;
    }
    bool ok = true;
    for (int k = 0; k < 48 && ok; k++)
        for (double add : {1.0, 4.0, 10.0}) {  // (just past where its water can reach on its floor, and a little further)
            if (!ok) break;
            const double rr = L.dishR + 8 * (L.dishD + 0.5) + add;
            const double a = k * (6.283185307179586 / 48);
            // (unit vector, by a short series: no libm in what's generated)
            const double a2 = a > 3.141592653589793 ? a - 6.283185307179586 : a;
            const double cs = 1 - a2 * a2 / 2 + a2 * a2 * a2 * a2 / 24 - a2 * a2 * a2 * a2 * a2 * a2 / 720 + a2 * a2 * a2 * a2 * a2 * a2 * a2 * a2 / 40320;
            const double sn = a2 - a2 * a2 * a2 / 6 + a2 * a2 * a2 * a2 * a2 / 120 - a2 * a2 * a2 * a2 * a2 * a2 * a2 / 5040 + a2 * a2 * a2 * a2 * a2 * a2 * a2 * a2 * a2 / 362880;
            if (cs > 0.75) continue;  // (the lip's side: the dam holds it)
            // (round its floor: a tarn's a circle; a valley lake's stretched along the valley)
            const double along = cs * rr * (L.kind == 2 ? 1 : L.ea / L.dishR), across = sn * rr;
            const double x = L.ex + L.fx * along - L.fz * across, z = L.ez + L.fz * along + L.fx * across;
            const Macro mm = macroNoPerch(G, x, z, false);
            ok = std::max(mm.h, damAt(L, x, z)) > L.W + 0.2;  // (its own dam counts)
            // (a creek's valley coming in, under the water's level: the water backs up it, that's the lake, not a gap in its rim)
            if (!ok && L.kind == 3 && mm.floorNode != INT64_MIN) ok = drainsTo(G, mm.floorNode, mm.floorNode2, L.ni, L.nj);
            if (!ok && (L.kind == 2 || L.kind == 4) && mm.floorNode != INT64_MIN) ok = drainsTo(G, mm.floorNode, mm.floorNode2, L.ni, L.nj) && add < 5;
        }
    blockFor(G, L.ni, L.nj, q)->lakeOk[q].store(ok ? 1 : 0, std::memory_order_relaxed);
    gLakeFunnel[10 + ok]++;
    return ok;
}
namespace {
// (The rule: water never stands over the land beside it. A creek's stretch, where the land 30 m to either side, across it, is lower than
// its water (on a valley's side at a slant, or beside a deeper neighbour), carries no water there: it's dry, seeped into the scree.)
bool creekPerchedRaw(const ProcWorld& G, int64_t i, int64_t j) {
    int q;
    {
        const int p = blockFor(G, i, j, q)->perched[q].load(std::memory_order_relaxed);
        if (p >= 0) return p;
    }
    bool perched = false;
    std::vector<DrainSeg> own;
    nodeSegs(G, i, j, 0, 0, own, false);
    for (size_t k = 0; k < own.size() && !perched; k++) {  // (along its own curve: the middle of each straight piece)
        if (own[k].cls != 1) continue;
        const DrainSeg& e = own[k];
        for (double t : {0.1, 0.5, 0.9}) {  // (a few points along each straight piece)
            const double cx = e.ax + (e.bx - e.ax) * t, cz = e.az + (e.bz - e.az) * t, lv = e.la + (e.lb - e.la) * t;
            for (int sd = -1; sd <= 1 && !perched; sd += 2) perched = macroNoPerch(G, cx - e.dz * 30 * sd, cz + e.dx * 30 * sd).h < lv - 0.35;
        }
    }
    blockFor(G, i, j, q)->perched[q].store(perched ? 1 : 0, std::memory_order_relaxed);
    return perched;
}
// (A creek's water as a run, not in pieces (owner: "drawn as separate segments with gaps between them"): a lone wet stretch between
// dry ones is dry too; a lone dry stretch can't carry its water through, as there it would stand over the land beside it.)
bool creekPerched(const ProcWorld& G, int64_t i, int64_t j) {
    const bool here = creekPerchedRaw(G, i, j);
    const int d = down2Of(G, i, j), u = upMain2Of(G, i, j);
    const bool down = d >= 0 ? creekPerchedRaw(G, i + kN8[d][0], j + kN8[d][1]) : here;
    const bool up = u >= 0 ? creekPerchedRaw(G, i + kN8[u][0], j + kN8[u][1]) : here;
    if (!here && up && down) return true;
    return here;
}
}  // namespace

int ProcWorld::erosionTileSize() { return (int)kETile; }
void ProcWorld::erosionInput(int64_t ti, int64_t tj, ErosionGrid& g, double& ox, double& oz) const {
    sampleErosionGrid(*this, ti, tj, g);
    ox = (double)(ti * kETile - kEMargin), oz = (double)(tj * kETile - kEMargin);
}
std::vector<float> ProcWorld::erosionTile(int64_t ti, int64_t tj, double& ox, double& oz, int& n, double& cell, double& ms) const {
    const auto T = proc::erosionTile(*this, ti, tj);
    ox = T->ox, oz = T->oz, n = kEN, cell = kECell, ms = T->ms;
    return T->d;
}
double ProcWorld::erosionLake(double gx, double gz) const { return rules().erosion.on > 0 ? macroAt(gx, gz, false).lakeLevel : -1e9; }
double ProcWorld::erosionAt(double gx, double gz) const { return rules().erosion.on > 0 ? proc::erosionAt(*this, gx, gz) : 0.0; }

Macro ProcWorld::macroCached(double x, double z) const {
    const auto T = tileAt(*this, x, z);
    const int a = std::min(kTC - 1, std::max(0, (int)std::floor((x - T->ox) / kTCell))), b = std::min(kTC - 1, std::max(0, (int)std::floor((z - T->oz) / kTCell)));
    const int c = a * kTC + b;
    Macro m = evalMacro(*this, x, z, *T, T->idx.data() + T->start[c], (int)(T->start[c + 1] - T->start[c]), true);
    applyErosion(*this, x, z, m, false);
    return m;
}

// The erosion tiles a box needs (and its blend bands' neighbours), made now, a few at once (CZ_GEN_THREADS caps the threads).
void prewarmErosion(const ProcWorld& G, double x0, double z0, double x1, double z1) {
    if (rules().erosion.on <= 0) return;
    std::vector<std::pair<int64_t, int64_t>> want;
    for (int64_t i = (int64_t)std::floor((x0 - kEBlend) / (double)kETile); i <= (int64_t)std::floor((x1 + kEBlend) / (double)kETile); i++)
        for (int64_t j = (int64_t)std::floor((z0 - kEBlend) / (double)kETile); j <= (int64_t)std::floor((z1 + kEBlend) / (double)kETile); j++) want.push_back({i, j});
    const char* cap = std::getenv("CZ_GEN_THREADS");
    const int save = parallelRowsCap;
    parallelRowsCap = cap && *cap ? std::max(1, std::atoi(cap)) : std::min(4, (int)std::max(1u, std::thread::hardware_concurrency()));
    parallelRows((int)want.size(), [&](int k) {
        const bool was = gTileWorker;
        gTileWorker = true;
        proc::erosionTile(G, want[k].first, want[k].second);
        gTileWorker = was;
    }, 1);
    parallelRowsCap = save;
}
bool erosionMade(const ProcWorld& G, double x0, double z0, double x1, double z1) {
    if (rules().erosion.on <= 0) return true;
    for (int64_t i = (int64_t)std::floor((x0 - kEBlend) / (double)kETile); i <= (int64_t)std::floor((x1 + kEBlend) / (double)kETile); i++)
        for (int64_t j = (int64_t)std::floor((z0 - kEBlend) / (double)kETile); j <= (int64_t)std::floor((z1 + kEBlend) / (double)kETile); j++)
            if (!erosionTileIfMade(G, i, j)) return false;
    return true;
}
std::atomic<bool>& prefetchStopping() {
    static std::atomic<bool> v{false};
    return v;
}
BgWork* prefetchWork() {
    static BgWork* w = [] {
        BgWork* b = new BgWork;  // (never destroyed)
        std::atexit([] { prefetchStopping() = true; prefetchWork()->wait(); });
        return b;
    }();
    return w;
}
// ...or later, one at a time on a background thread below the game's priority (the tiles ahead of a window: ready before it slides).
void prefetchErosion(const ProcWorld& G, double x0, double z0, double x1, double z1) {
    if (rules().erosion.on <= 0 || std::getenv("CZ_NO_EROSION_PREFETCH")) return;
    // (Made once and never destroyed; at exit, before the caches it fills are torn down, what's queued is dropped and the one tile
    // in hand finished.)
    BgWork* work = prefetchWork();
    const uint64_t seed = G.seed();
    for (int64_t i = (int64_t)std::floor((x0 - kEBlend) / (double)kETile); i <= (int64_t)std::floor((x1 + kEBlend) / (double)kETile); i++)
        for (int64_t j = (int64_t)std::floor((z0 - kEBlend) / (double)kETile); j <= (int64_t)std::floor((z1 + kEBlend) / (double)kETile); j++) {
            if (erosionTileIfMade(G, i, j)) continue;
            static std::mutex& qm = *new std::mutex;  // (never destroyed: a job in hand at exit still unlocks it)
            static std::set<std::tuple<uint64_t, int64_t, int64_t>>& queued = *new std::set<std::tuple<uint64_t, int64_t, int64_t>>;
            {  // (queued once: asked again every frame until it's made)
                std::lock_guard<std::mutex> lock(qm);
                if (queued.size() > 4096) queued.clear();
                if (!queued.insert({seed, i, j}).second) continue;
            }
            work->post([seed, i, j] {
                if (prefetchStopping()) return;
                static thread_local bool lowered = false;
                if (!lowered) lowerThreadPriority(), lowered = true;
                gTileWorker = true;  // (one thread: the land ahead is made quietly)
                proc::erosionTile(ProcWorld::forSeed(seed), i, j);
                std::lock_guard<std::mutex> lock(qm);
                queued.erase({seed, i, j});
            });
        }    // (and the trails ahead, laid out on the same quiet thread once their land is worn: the chunks then only read them, no hitch)
    {
        static std::mutex& em = *new std::mutex;
        static std::set<std::tuple<uint64_t, int64_t, int64_t>>& done = *new std::set<std::tuple<uint64_t, int64_t, int64_t>>;
        for (int64_t i = (int64_t)std::floor(x0 / kCell) - 1; i <= (int64_t)std::floor(x1 / kCell); i++)
            for (int64_t j = (int64_t)std::floor(z0 / kCell) - 1; j <= (int64_t)std::floor(z1 / kCell); j++) {
                {
                    std::lock_guard<std::mutex> lock(em);
                    if (done.size() > 20000) done.clear();
                    if (!done.insert({seed, i, j}).second) continue;
                }
                work->post([seed, i, j] {
                    if (prefetchStopping()) return;
                    gTileWorker = true;
                    const ProcWorld& G2 = ProcWorld::forSeed(seed);
                    for (int dir = 0; dir < 3; dir++) G2.edge(i, j, dir);
                });
            }
    }
}

Macro ProcWorld::land(double x, double z) const { return macroAt(x, z, false); }
Macro ProcWorld::macro(double x, double z) const { return macroAt(x, z, true); }
Macro ProcWorld::macroAt(double x, double z, bool water) const {
    Macro m = macroNoPerch(*this, x, z);
    if (water && m.creekOn > 0 && m.creekNode != INT64_MIN && creekPerched(*this, m.creekNode, m.creekNodeJ)) m.creekOn = 0;
    return m;
}

double ProcWorld::riverHalfWidth(double x, double z) const { return macroAt(x, z, false).riverHW; }
double ProcWorld::creekHalfWidth(double x, double z) const { return macroAt(x, z, false).creekHW; }
double ProcWorld::riverDepth(double x, double z) const { return macroAt(x, z, false).riverDepth; }
double ProcWorld::macroHeight(double x, double z) const { return macroAt(x, z, false).h; }

// The ground as the chunks cut it, less the bumps and the trails: the valleys, and the nearest river's and creek's beds in them.
double ProcWorld::groundHeight(double x, double z) const { return groundHeight(x, z, macroAt(x, z, false)); }
double ProcWorld::groundHeight(double, double, const Macro& m) const {
    double h = m.h;
    if (m.riverS < m.riverHW) h = std::min(h, m.riverLevel - m.riverDepth * (1 - sqr(m.riverS / m.riverHW)));
    if (m.creekS < m.creekHW) h = std::min(h, m.creekLevel - m.creekDepth * (1 - sqr(m.creekS / m.creekHW)));
    return std::max(h, m.damH);  // (a lake's dam: over the channel's bed, its lip)
}

ProcWorld::DrainStats ProcWorld::drainStats(double x0, double z0, double x1, double z1) const {
    DrainStats st;
    const DrainClass DC = drainClass();
    const int64_t i0 = (int64_t)std::floor(x0 / kD), i1 = (int64_t)std::floor(x1 / kD), j0 = (int64_t)std::floor(z0 / kD), j1 = (int64_t)std::floor(z1 / kD);
    double sumA = 0;
    std::unordered_map<uint64_t, int> seen;
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++) {
            st.nodes++;
            const double A = areaOf(*this, i, j);
            const bool river = isRiver(*this, i, j);
            sumA += A;
            const int dn = down2Of(*this, i, j);
            st.sinks += dn < 0;
            st.creeks += !river && A >= DC.creek, st.rivers += river && A < DC.big, st.bigRivers += river && A >= DC.big;
            // (Its way down in the end: no node twice, and it ends in a sink; and where it cuts a channel, the level never rises.)
            if (dn >= 0 && A >= DC.gully) st.levelRises += levelOf(*this, i + kN8[dn][0], j + kN8[dn][1]) > levelOf(*this, i, j) + 1e-3;
            seen.clear();
            int64_t ci = i, cj = j;
            long steps = 0;
            bool ok = true;
            for (;; steps++) {
                const uint64_t key = hashIJ(0x5EE4ull, ci, cj);
                if (seen.count(key)) {
                    ok = false;
                    break;
                }
                seen[key] = 1;
                const int d = down2Of(*this, ci, cj);
                if (d < 0 || steps > 20000) break;
                ci += kN8[d][0], cj += kN8[d][1];
            }
            st.chainsChecked++, st.chainsBad += !ok || steps > 20000, st.maxChain = std::max(st.maxChain, steps);
            if (A >= DC.gully) {  // (along its own pieces: each never rising, and each starting no higher than the one before ended)
                std::vector<DrainSeg> own;
                nodeSegs(*this, i, j, 0, 0, own, false);
                for (size_t k = 0; k < own.size(); k++) st.pieceRises += own[k].lb > own[k].la + 1e-3f || (k && own[k].la > own[k - 1].lb + 1e-3f);
            }
            double lw[PB_COUNT];
            const NodeInfo ni = nodeInfo(*this, i, j);
            lowWeights(params(), ni.wet, ni.heat, ni.mk, lw);
            if (!river && A >= DC.creek && ni.mk > 0.5 && lw[PB_DUNES] + lw[PB_BADLANDS] < 0.3) {  // (the ranges' creeks, out of the desert: dry washes there)
                st.rangeCreeks++;
                std::vector<DrainSeg> own;
                nodeSegs(*this, i, j, 0, 0, own, false);
                const bool perched = creekPerched(*this, i, j);
                st.rangeCreeksWet += !own.empty() && own[0].wet > 0.5 && !perched, st.rangeCreeksPerched += perched;
            }
        }
    st.meanA = st.nodes ? sumA / st.nodes : 0;
    return st;
}

std::vector<ProcWorld::WaterFeature> ProcWorld::waterFeatures(double x0, double z0, double x1, double z1) const {
    std::vector<WaterFeature> out;
    const DrainClass DC = drainClass();
    if (rules().erosion.on > 0) {  // (the lakes the worn land holds behind its valleys' natural dams: each tile's own)
        for (int64_t ti = (int64_t)std::floor(x0 / (double)kETile); ti <= (int64_t)std::floor(x1 / (double)kETile); ti++)
            for (int64_t tj = (int64_t)std::floor(z0 / (double)kETile); tj <= (int64_t)std::floor(z1 / (double)kETile); tj++)
                for (const ETile::Lk& L : proc::erosionTile(*this, ti, tj)->lakes) {
                    if (L.x < x0 || L.x >= x1 || L.z < z0 || L.z >= z1) continue;
                    double fx = L.lipX - L.x, fz = L.lipZ - L.z;
                    const double fl = std::sqrt(fx * fx + fz * fz);
                    if (fl > 1e-6) fx /= fl, fz /= fl;
                    WaterFeature f{0, L.x, L.z, L.level, std::sqrt(L.area / 3.141592653589793), fx, fz};
                    f.sub = 3, f.outlet = L.level;
                    out.push_back(f);
                }
    }
    const int64_t i0 = (int64_t)std::floor(x0 / kD) - 1, i1 = (int64_t)std::floor(x1 / kD) + 1, j0 = (int64_t)std::floor(z0 / kD) - 1, j1 = (int64_t)std::floor(z1 / kD) + 1;
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++) {
            if (areaOf(*this, i, j) < DC.gully) continue;
            const NodeInfo n = nodeInfo(*this, i, j);
            const Lake lk = (even2(i, 2) && even2(j, 2)) || down2Of(*this, i, j) < 0 ? lakeOf(*this, i, j) : Lake{};
            if (rules().erosion.on <= 0 && lk.kind && n.x >= x0 && n.x < x1 && n.z >= z0 && n.z < z1 && lakeOkOf(*this, lk)) {
                // (a lake: its lip (a basin: its middle), the water's level, how far across (a tarn: its floor's radius; a valley
                // lake or pond: its dam's half-length), and its outlet: the level of its channel at the lip)
                WaterFeature f{0, lk.kind == 1 ? n.x : lk.qx, lk.kind == 1 ? n.z : lk.qz, lakeLevelOf(*this, lk), lk.kind == 2 ? lk.dishR : lk.lat, lk.fx, lk.fz};
                f.sub = lk.kind;
                f.outlet = lk.kind == 1 ? -1e9 : lk.W;
                out.push_back(f);
            }
            if (down2Of(*this, i, j) < 0) continue;
            std::vector<DrainSeg> own;
            nodeSegs(*this, i, j, 0, 0, own, false);
            bool dry = false, any = false;
            for (const DrainSeg& e : own) any |= e.fall != 0;
            if (any && own[0].cls == 1) dry = creekPerched(*this, i, j);  // (a dry step: no waterfall)
            for (const DrainSeg& e : own)
                if (e.fall && !dry && e.ax >= x0 && e.ax < x1 && e.az >= z0 && e.az < z1) {
                    WaterFeature f{1, e.ax, e.az, e.la, e.la - e.lb, e.dx, e.dz};
                    f.width = 2 * e.hw;
                    out.push_back(f);
                }
        }
    return out;
}

std::vector<std::pair<double, double>> ProcWorld::channelsAcross(double x0, double z0, double x1, double z1, double minArea) const {
    // (Each channel piece the line crosses, as (how far along the line, the channel's drainage). A channel's two pieces either side of
    // a crossing point count once: crossings within 60 m of each other with the same drainage are one.)
    std::vector<std::pair<double, double>> out;
    const double L = std::sqrt(sqr(x1 - x0) + sqr(z1 - z0)), ux = (x1 - x0) / L, uz = (z1 - z0) / L;
    const double minA = minArea / kNodeKm2;
    for (double a = 0; a < L; a += kTile / 2) {
        const double cx = x0 + ux * (a + kTile / 4), cz = z0 + uz * (a + kTile / 4);
        const auto T = tileAt(*this, cx, cz);
        for (const DrainSeg& s : T->segs) {
            if (s.A < minA) continue;
            // (The line's crossing with the piece.)
            const double ax = s.ax + T->ox, az = s.az + T->oz, bx = s.bx + T->ox, bz = s.bz + T->oz;
            const double ex = bx - ax, ez = bz - az, den = ux * ez - uz * ex;
            if (std::fabs(den) < 1e-9) continue;
            const double wx = ax - x0, wz = az - z0;
            const double tl = (wx * ez - wz * ex) / den, ts = (wx * uz - wz * ux) / den;
            if (ts < 0 || ts >= 1 || tl < a || tl >= a + kTile / 2 || tl >= L) continue;
            bool dup = false;
            for (auto& o : out) dup |= std::fabs(o.first - tl) < 60 && std::fabs(o.second - s.A) < 0.5;
            if (!dup) out.push_back({tl, s.A});
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// (The caches of the drainage: dropped with the chunks'.)
static void clearGridCache();
static void clearLakeMemo();
static void clearDrainCaches() {
    clearLakeMemo();
    {
        std::lock_guard<std::mutex> lock(blockMutex);
        blockMap.clear();
    }
    {
        std::lock_guard<std::mutex> lock(tileMutex);
        tileMap[0].clear(), tileMap[1].clear();
    }
    clearGridCache();
    // (The erosion's tiles stay: a pure function of the seed and the rules, and dear to make (a test dropping the chunks beside another
    // test's window build mustn't make it wait for them again). clearErosionTiles drops them.)
    drainEpoch++;
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
    // An outpost near the world's origin in the foothills (a range rising beside it, the valley floors starting to climb: neither
    // the open lowland nor up on the range's axis), the nearest such: you start on its lot, beside the shed, facing the way it does.
    const Params& P = *params_;
    // (First by the cells' middles alone, cheaply (the routing's field, no worn land): the outposts' cells within ~4 km, the
    // foothills' nearest few; then those few sited, and the one on the gentlest ground round its lot, near.)
    const int64_t pi = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 0, 0) % 3), pj = (int64_t)(hashIJ(seed_ ^ 0x0F0Full, 1, 1) % 3);
    std::vector<std::pair<double, std::pair<int64_t, int64_t>>> cand;
    for (int64_t i = -8; i <= 7; i++)
        for (int64_t j = -8; j <= 7; j++) {
            if (((i - pi) % 3 + 3) % 3 || ((j - pj) % 3 + 3) % 3) continue;
            const double cx = (i + 0.5) * kCell, cz = (j + 0.5) * kCell;
            const double massif = routeAt(P, cx, cz, wetAt(P, cx, cz), heatAt(P, cx, cz)).massif;
            const double off = std::max(0.0, std::fabs(massif - 0.3) - 0.15);  // (foothills: massif 0.15-0.45)
            cand.push_back({std::sqrt(cx * cx + cz * cz) + 10000 * off, {i, j}});
        }
    std::sort(cand.begin(), cand.end());
    double best = 1e18;
    TrailNode at;
    for (size_t c = 0; c < std::min<size_t>(6, cand.size()); c++) {
        const TrailNode n = node(cand[c].second.first, cand[c].second.second);
        double steep = 0;  // (the trucks start on the lot and beside it, a few trucks' lengths round: not half into a slope)
        for (int q = 0; q < 16; q++) {
            const double r = q < 8 ? 18 : 34;
            steep = std::max(steep, std::fabs(groundHeight(n.x + kDir8[q & 7][0] * r, n.z + kDir8[q & 7][1] * r) - n.h) / r);
        }
        const double d = cand[c].first + 20000 * std::max(0.0, steep - 0.08);
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
    for (const RockSpec& r : rocks) bytes(&r.x, sizeof(float) * 7), bytes(&r.seed, 4), bytes(&r.yaw, sizeof(float) * 2);
    for (const BushSpec& b : bushes) bytes(&b.x, sizeof(float) * 4), bytes(&b.habitat, 2);
    for (const LogSpec& l : logs) bytes(&l.ax, sizeof(float) * 5);
    for (const StumpSpec& t : stumps) bytes(&t.x, sizeof(float) * 5);
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
    for (int k = 0; k < 36; k++) {
        if (k == 12 && best > -1e8) break;  // (the first dozen found dry ground; else on, over the whole cell)
        const double spread = k < 12 ? 90 : 20;
        const double x = (double)(i * kCell) + spread + (kCell - 2 * spread) * hash01(P.s[S_DITHER] + 3 + k, i, j);
        const double z = (double)(j * kCell) + spread + (kCell - 2 * spread) * hash01(P.s[S_DITHER] + 13 + k, i, j);
        const Macro m = macroAt(x, z, false);
        const double g = groundHeight(x, z, m), e = groundHeight(x + 20, z), w = groundHeight(x - 20, z), s = groundHeight(x, z + 20), nn = groundHeight(x, z - 20);
        const double slope = (std::fabs(e - w) + std::fabs(s - nn)) / 40 + (std::fabs(e + w - 2 * g) + std::fabs(s + nn - 2 * g)) / 40;
        const double clear = m.lakeLevel > -1e8 ? -40.0 : std::min(std::fabs(m.riverS) - m.riverHW - (m.riverOn > 0 ? 45 : 15), std::fabs(m.creekS) - 20);
        const double score = (m.riverOn > 0 && std::fabs(m.riverS) < m.riverHW + 3 ? -1e9 : 0.0) + std::min(clear, 30.0) - 160 * slope - 0.12 * g;  // (never in a river)
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

namespace {
// The trails' search lattice (16 m), cached by 1 km tile and shared by every edge over it: the carved ground, and what water is there
// (0 none, 1 a river or creek to wade, 2 a big river off its fords: a bridge or nothing, 3 a lake: round it).
struct GridPoint {
    float h;
    uint8_t wet;
};
struct GridTile {
    GridPoint p[64 * 64];
};
std::mutex gridMutex;
std::unordered_map<TKey, std::shared_ptr<const GridTile>, TKHash> gridMap;
}  // namespace
static void clearLakeMemo() {
    std::lock_guard<std::mutex> lock(lakeMemoMutex);
    lakeMemo.clear();
}
static void clearGridCache() {
    std::lock_guard<std::mutex> lock(gridMutex);
    gridMap.clear();
}
namespace {
GridPoint gridPoint(const ProcWorld& G, int64_t u, int64_t v) {
    const int64_t ti = floorDiv(u, 64), tj = floorDiv(v, 64);
    thread_local struct {
        uint64_t seed = 0, epoch = 0;
        int64_t i = INT64_MIN, j = 0;
        std::shared_ptr<const GridTile> t;
    } slot[4];
    thread_local int next = 0;
    const uint64_t ep = drainEpoch.load(std::memory_order_relaxed);
    const GridTile* t = nullptr;
    for (auto& s : slot)
        if (s.t && s.epoch == ep && s.i == ti && s.j == tj && s.seed == G.seed()) t = s.t.get();
    if (!t) {
        std::shared_ptr<const GridTile> got;
        {
            std::lock_guard<std::mutex> lock(gridMutex);
            auto it = gridMap.find({G.seed(), ti, tj});
            if (it != gridMap.end()) got = it->second;
        }
        if (!got) {
            auto made = std::make_shared<GridTile>();
            for (int a = 0; a < 64; a++)
                for (int b = 0; b < 64; b++) {
                    const double x = (double)((ti * 64 + a) * 16), z = (double)((tj * 64 + b) * 16);
                    const Macro m = G.macroAt(x, z, false);  // (a creek dry just there still costs a crossing)
                    GridPoint& g = made->p[a * 64 + b];
                    g.h = (float)G.groundHeight(x, z, m);
                    g.wet = (m.riverOn > 0.35 && std::fabs(m.riverS) < m.riverHW + 8) || (m.creekOn > 0.35 && std::fabs(m.creekS) < m.creekHW + 4);
                    if (m.bigRiver && !m.riverFord && m.riverOn > 0.5 && std::fabs(m.riverS) < m.riverHW + 4) g.wet = 2;
                    if ((m.lakeLevel > -1e8 && m.h < m.lakeLevel + 2) || m.damH > -1e8) g.wet = 3;  // (a lake, or its dam: round it, never through its lip)
                }
            std::lock_guard<std::mutex> lock(gridMutex);
            if (gridMap.size() > 800) gridMap.clear();  // (~13 MB)
            got = gridMap.emplace(TKey{G.seed(), ti, tj}, made).first->second;
        }
        auto& s = slot[next++ & 3];
        s.seed = G.seed(), s.epoch = ep, s.i = ti, s.j = tj, s.t = got;
        t = got.get();
    }
    return t->p[(u - ti * 64) * 64 + (v - tj * 64)];
}
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
        const double W = e->road ? 320 : 220;  // (how far it may wander: a road's gentler grade wants longer switchbacks; makeChunk's gather allows ~460)
        const double gx0 = std::floor((std::min(a.x, b.x) - W) / C) * C, gz0 = std::floor((std::min(a.z, b.z) - W) / C) * C;
        const int NX = (int)((std::max(a.x, b.x) + W - gx0) / C) + 1, NZ = (int)((std::max(a.z, b.z) + W - gz0) / C) + 1;
        std::vector<float> H((size_t)NX * NZ);
        std::vector<uint8_t> wet((size_t)NX * NZ);
        for (int u = 0; u < NX; u++)
            for (int v = 0; v < NZ; v++) {
                const GridPoint g = gridPoint(*this, (int64_t)std::llround(gx0 / C) + u, (int64_t)std::llround(gz0 / C) + v);
                H[(size_t)u * NZ + v] = g.h, wet[(size_t)u * NZ + v] = g.wet;
            }
        // (Which trails may bridge a big river: most main trails, a few others. The rest cross at a known ford, or not at all.)
        const bool canBridge = hash01(P.s[S_DITHER] + 801 + (uint64_t)dir, i, j) < (e->road ? 0.75 : 0.15);
        auto cellOf = [&](double x, double z) {
            const int u = std::min(NX - 1, std::max(0, (int)std::lround((x - gx0) / C))), v = std::min(NZ - 1, std::max(0, (int)std::lround((z - gz0) / C)));
            return u * NZ + v;
        };
        const int start = cellOf(a.x, a.z), goal = cellOf(b.x, b.z);
        // (The search's state is a cell and the way it came in: turning costs, a hairpin more, so a climb is a few long legs with
        // real switchbacks between them rather than a saw's teeth of short ones.)
        static const int kStep[16][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
                                         {2, 1}, {2, -1}, {-2, 1}, {-2, -1}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
        constexpr int ND = 16, NSD = ND + 1;
        double ux[ND], uz[ND];
        for (int d = 0; d < ND; d++) {
            const double l = std::sqrt((double)(kStep[d][0] * kStep[d][0] + kStep[d][1] * kStep[d][1]));
            ux[d] = kStep[d][0] / l, uz[d] = kStep[d][1] / l;
        }
        const double turnK = e->road ? 26 : 16, hairpinK = e->road ? 70 : 40;
        const size_t NS = (size_t)NX * NZ * NSD;
        std::vector<double> cost(NS, 1e300);
        std::vector<int> from(NS, -1);
        std::vector<uint8_t> done(NS, 0);
        using QE = std::pair<double, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
        auto heur = [&](int c) { return 2.2 * std::sqrt(sqr((c / NZ - goal / NZ) * C) + sqr((c % NZ - goal % NZ) * C)); };  // (a little greedy: much quicker)
        const int s0 = start * NSD + ND;
        cost[s0] = 0, open.push({heur(start), s0});
        int endState = -1;
        while (!open.empty()) {
            const int sc = open.top().second;
            open.pop();
            if (done[sc]) continue;
            done[sc] = 1;
            const int c = sc / NSD, din = sc % NSD;
            if (c == goal) {
                endState = sc;
                break;
            }
            const int u = c / NZ, v = c % NZ;
            for (int d = 0; d < ND; d++) {
                const int nu = u + kStep[d][0], nv = v + kStep[d][1];
                if (nu < 0 || nv < 0 || nu >= NX || nv >= NZ) continue;
                const int nc = nu * NZ + nv, ns = nc * NSD + d;
                if (done[ns]) continue;
                const double L = C * std::sqrt((double)(kStep[d][0] * kStep[d][0] + kStep[d][1] * kStep[d][1]));
                const double g = std::fabs(H[nc] - H[c]) / L, q = g / gmax;
                double step = L * (1 + 3 * q * q);
                if (q > 1) step += L * (e->road ? 150 : 100) * (q - 1);  // (past its grade: only where there's no other way)
                {  // (across a steep side: a level tread there is a deep cut on one hand and a bank on the other; round by gentler ground)
                    auto Hc = [&](int a, int b) { return (double)H[(size_t)std::min(NX - 1, std::max(0, a)) * NZ + std::min(NZ - 1, std::max(0, b))]; };
                    const double sx = (Hc(nu + 1, nv) - Hc(nu - 1, nv)) / (2 * C), sz = (Hc(nu, nv + 1) - Hc(nu, nv - 1)) / (2 * C);
                    const double cross = std::fabs(-sx * uz[d] + sz * ux[d]);  // (the land's fall across the way)
                    if (cross > 0.5) step += L * 12 * (cross - 0.5);
                }
                if (wet[nc] == 3) step += 8000;  // (into a lake: never, nearly)
                else if (wet[nc] == 2 && wet[c] != 2) step += canBridge || e->road ? 350 : 8000;  // (a bridge costs; wading a big river: never, nearly)
                else if (wet[nc] == 2) step += 2000;  // (and never along one: a gorge's river is no road, nor a bridge down its length)
                else if (wet[nc] && !wet[c]) step += 120;  // (into the water: a crossing costs, so it crosses once)
                else if (wet[nc] == 1 && wet[c] == 1) step += L * 6;  // (and not down a creek's bed: beside it, or across)
                if (din < ND) {
                    const double cs = ux[din] * ux[d] + uz[din] * uz[d];
                    step += turnK * (1 - cs) + (cs < -0.3 ? hairpinK : 0);
                }
                const double nc2 = cost[sc] + step;
                if (nc2 < cost[ns]) cost[ns] = nc2, from[ns] = sc, open.push({nc2 + heur(nc), ns});
            }
        }
        // The path, node to node, smoothed (corners cut, twice), then a point every ~2 m.
        std::vector<double> qx, qz;
        for (int sc = endState; sc >= 0; sc = from[sc]) {
            const int c = sc / NSD;
            qx.push_back(gx0 + (c / NZ) * C - a.x), qz.push_back(gz0 + (c % NZ) * C - a.z);
            if (sc == s0) break;
        }
        if (qx.empty()) qx.push_back(b.x - a.x), qz.push_back(b.z - a.z);
        if (qx.size() < 2) qx.push_back(0), qz.push_back(0);
        std::reverse(qx.begin(), qx.end()), std::reverse(qz.begin(), qz.end());
        qx.front() = 0, qz.front() = 0, qx.back() = b.x - a.x, qz.back() = b.z - a.z;
        for (int pass = 0; pass < 2; pass++) {
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
        // (...and the ground itself every 4 m (between, in a line: a gully's dip is caught): the most the tread may stand over, at the end. In a valley's hollow
        // or beside a wash the 16 m lattice's corners up the sides would hold it up on a bank over the floor.)
        std::vector<double> fine(n);
        for (int k = 0; k < n; k++) {
            raw[k] = Hat(px[k], pz[k]);
            fine[k] = (k % 2 == 0 || k == n - 1) ? groundHeight(a.x + px[k], a.z + pz[k]) : 0.0;
            if (k) along[k] = along[k - 1] + std::sqrt(sqr(px[k] - px[k - 1]) + sqr(pz[k] - pz[k - 1]));
        }
        for (int k = 0; k + 1 < n; k++)
            if (k % 2) {
                const int k0 = k - k % 2, k1 = std::min(n - 1, k0 + 2);
                fine[k] = fine[k0] + (fine[k1] - fine[k0]) * (double)(k - k0) / (k1 - k0);
            }
        const int smoothR = e->road ? 8 : 4;  // (a road's profile smoothed over ~32 m; a trail's follows the land closer, ~16 m)
        for (int k = 0; k < n; k++) {
            double s = 0;
            int cnt = 0;
            for (int q = std::max(0, k - smoothR); q <= std::min(n - 1, k + smoothR); q++) s += raw[q], cnt++;
            y[k] = s / cnt;
        }
        // Bridges: where it's over a big river off its known fords, a deck from bank to bank, level, over the water by 1.5 m or more (1.8 at the deepest-sampled level).
        // (A trail that may not bridge and still found no ford is left out: big rivers are real obstacles.)
        std::vector<char> br(n, 0), app(n, 0);
        {
            std::vector<char> deep(n, 0);
            std::vector<double> wl(n, -1e9);
            bool any = false;
            for (int k = 0; k < n; k++) {
                const double gx = a.x + px[k], gz = a.z + pz[k];
                const Macro m = macroAt(gx, gz, false);
                if (m.bigRiver && !m.riverFord && m.riverOn > 0.5 && std::fabs(m.riverS) < m.riverHW + 2) deep[k] = 1, wl[k] = m.riverLevel, any = true;
            }
            for (int k = 0; k < n && any; k++) {
                if (!deep[k]) continue;
                int k1 = k;
                double level = wl[k];
                for (int q = k; q < n && q <= k1 + 4; q++)
                    if (deep[q]) k1 = q, level = std::max(level, wl[q]);
                if (k1 - k > 30) {  // (a "bridge" 60 m and more over a big river: a trail along it, not across. None.)
                    e->on = false;
                    break;
                }
                if (!canBridge && !e->road) {
                    e->on = false;
                    break;
                }
                const int b0 = std::max(1, k - 5), b1 = std::min(n - 2, k1 + 5);  // (onto the banks, ~10 m past the water)
                // (The deck over the highest bank under it, and ~6 m of level approach at each end graded to it (earthworks): the
                // ground meets the deck at its ends, never stands over it.)
                double deck = level + 1.8;
                for (int q = b0; q <= b1; q++) deck = std::max(deck, fine[q]);  // (the ground itself: the 16 m lattice in a gorge reads its rims)
                for (int q = b0; q <= b1; q++) br[q] = 1, y[q] = deck;
                for (int q = 1; q <= 3; q++) {
                    if (b0 - q >= 1) app[b0 - q] = 1, y[b0 - q] = deck;
                    if (b1 + q <= n - 2) app[b1 + q] = 1, y[b1 + q] = deck;
                }
                e->bridges.push_back({a.x + px[b0], a.z + pz[b0], a.x + px[b1], a.z + pz[b1], deck});
                k = std::max(k1, b1);  // (on past this one: never back, even where it ends in the water)
                if (e->bridges.size() > 16) break;  // (never near: a trail crosses a river or two)
            }
        }
        if (!e->on) {  // (no way over: no trail)
            std::lock_guard<std::mutex> lock(edgeMutex);
            if (edgeCache.size() > 40000) edgeCache.clear();
            e->bridges.clear();
            return edgeCache.emplace(key, std::move(e)).first->second;
        }
        for (int k = 0; k < n; k += 4) {  // (fords: where it's in a river or a creek, the water's level less a wading depth)
            const double gx = a.x + px[k], gz = a.z + pz[k];
            if (!wet[cellOf(gx, gz)] || br[k]) continue;
            const Macro m = macro(gx, gz);
            double fy = 1e9;
            if (m.riverOn > 0.5 && std::fabs(m.riverS) < m.riverHW + 2) fy = m.riverLevel - 0.45, e->bigFord |= m.bigRiver;
            else if (m.creekOn > 0.5 && std::fabs(m.creekS) < m.creekHW + 1) fy = m.creekLevel - 0.3;
            if (fy > 1e8) continue;
            for (int q = std::max(0, k - 2); q <= std::min(n - 1, k + 2); q++) ford[q] = 1, y[q] = fy;
            if (!e->ford) e->ford = true, e->fordX = gx, e->fordZ = gz;
        }
        y[0] = a.h, y[n - 1] = b.h;
        // (Where a creek runs into a river, the two fords lie side by side at their own levels: the higher lets go, graded down.)
        for (int pass = 0; pass < 2; pass++)
            for (int k = 1; k < n; k++)
                if (ford[k] && ford[k - 1] && std::fabs(y[k] - y[k - 1]) > gmax * (along[k] - along[k - 1]) + 1e-9) ford[y[k] > y[k - 1] ? k : k - 1] = 0;
        auto pinned = [&](int k) { return k == 0 || k == n - 1 || ford[k] || br[k] || app[k]; };
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
        {  // (A link that can't keep near its grade (over a summit massif, up a gorge's wall) isn't laid: the trails go round by the
            // valleys and passes. Its share of length past the limit, over 20 m stretches, as procworld/grades measures it.)
            const double lim = e->road ? 0.12 : gmax;
            double len = 0, over = 0, cut = 0;
            for (int k = 0; k + 10 < n; k++) {
                const double run = along[k + 10] - along[k], seg = along[k + 1] - along[k];
                len += seg, over += std::fabs(y[k + 10] - y[k]) > lim * run ? seg : 0;
                cut += !pinned(k) && std::fabs(y[k] - raw[k]) >= 1.5 ? seg : 0;  // (on a cut or a bank: a trail across a steep side)
                if (!pinned(k) && k + 1 < n) {  // (...or benched across a side steeper than 1.1:1, its cut and bank walls)
                    const double tx = px[k + 1] - px[k], tz = pz[k + 1] - pz[k], tl = std::sqrt(tx * tx + tz * tz) + 1e-9;
                    const double cs = std::fabs(Hat(px[k] - tz / tl * 4, pz[k] + tx / tl * 4) - Hat(px[k] + tz / tl * 4, pz[k] - tx / tl * 4)) / 8;
                    cut += cs > 1.1 ? seg : 0;
                }
            }
            // (Over the knolls a trail takes short pitches past its grade: a rougher rating more of them, rules 6's steep climbs.)
            const double overShare = e->road ? 0.2 : e->rating == 0 ? 0.07 : e->rating == 1 ? 0.15 : 0.25;
            // (...nor one that would sit in a trench or on a bank tens of metres deep anywhere: a lot at a gorge's foot, a climb up its
            // wall it couldn't spread out)
            double deep = 0;
            for (int k = 0; k < n; k++)
                if (!pinned(k)) deep = std::max(deep, std::fabs(y[k] - fine[k]));
            if (len > 0 && (over > overShare * len || cut > (e->road ? 0.35 : 0.25) * len || deep > 12)) {
                e->on = false, e->bridges.clear();
                std::lock_guard<std::mutex> lock(edgeMutex);
                if (edgeCache.size() > 40000) edgeCache.clear();
                return edgeCache.emplace(key, std::move(e)).first->second;
            }
        }
        {  // (Rules 6: a trail hugs the land, never built up over it: the tread at most a hand over the ground under it, and never cut
            // deeper into it than its kind's fill_max (a worn trench, not a cutting). Only where it's held up on purpose, a bridge's deck and approaches or a place's lot at its ends, does it stand
            // higher, let down to the land over 30 m from there.)
            std::vector<double> lift(n, kTreadLift), sink(n, rule.fillMax);
            for (int p = 0; p < n; p++) {
                if (!pinned(p) || ford[p]) continue;
                const double over = y[p] - fine[p], under = fine[p] - y[p];
                for (int k = std::max(0, p - 15); k <= std::min(n - 1, p + 15); k++) {
                    const double f = 1 - std::abs(k - p) / 15.0;
                    if (over > kTreadLift) lift[k] = std::max(lift[k], kTreadLift + over * f);
                    if (under > rule.fillMax) sink[k] = std::max(sink[k], rule.fillMax + (under - rule.fillMax) * f);
                }
            }
            for (int pass = 0; pass < 2; pass++) {
                for (int k = 0; k < n; k++)  // (not by the water: its crossing stays as the grading laid it, out of the deep)
                    if (!pinned(k)) {
                        if (wet[cellOf(a.x + px[k], a.z + pz[k])] < 2) y[k] = std::min(y[k], fine[k] + lift[k]);  // (down through a creek's ravine, a ford: never a bank over it; a big river's or a lake's edge stays as laid)
                        y[k] = std::max(y[k], fine[k] - sink[k]);  // (nor a cut deeper than its kind's, by the water too)
                    }
                if (pass) break;
                const std::vector<double> y0 = y;  // (the clamp's corners eased: a short running mean, clamped again)
                for (int k = 0; k < n; k++) {
                    if (pinned(k)) continue;
                    double sum = 0;
                    int cnt = 0;
                    for (int q = std::max(0, k - 2); q <= std::min(n - 1, k + 2); q++) sum += y0[q], cnt++;
                    y[k] = sum / cnt;
                }
            }
        }
        e->x.resize(n), e->z.resize(n), e->y.resize(n), e->along.resize(n), e->fill.resize(n);
        if (!e->bridges.empty()) e->onBridge.assign(br.begin(), br.end());
        double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
        for (int k = 0; k < n; k++) {
            e->x[k] = (float)px[k], e->z[k] = (float)pz[k], e->y[k] = (float)y[k], e->along[k] = (float)along[k], e->fill[k] = (float)(y[k] - fine[k]);
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
    // (The worn land's tiles it stands on, made first if they aren't yet: their own cost, not the chunk's (the game makes them ahead).)
    const auto tw = std::chrono::steady_clock::now();
    // (and the trails it looks at: from nodes up to two 512 m cells off, their search ~420 m round, on 1 km lattice tiles: ~3 km)
    prewarmErosion(*this, (double)(cx * kChunk) - 3000, (double)(cz * kChunk) - 3000, (double)(cx * kChunk) + 3064, (double)(cz * kChunk) + 3064);
    const double tileWait = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tw).count();
    const auto t0 = std::chrono::steady_clock::now();
    const Params& P = *params_;
    auto c = std::make_shared<Chunk>();
    c->cx = cx, c->cz = cz;
    c->tileMs = tileWait;
    const int64_t gx0 = cx * kChunk, gz0 = cz * kChunk;
    // The channels near the chunk (from its tile), and the smooth fields every 4 m, from 4 m before the chunk to 4 m past it
    // (global lattice points: the same in every chunk).
    const auto tile = tileAt(*this, (double)gx0 + 32, (double)gz0 + 32);
    const DTile& T = *tile;
    const int tcA = std::min(kTC - 1, std::max(0, (int)std::floor(((double)gx0 + 32 - T.ox) / kTCell)));
    const int tcB = std::min(kTC - 1, std::max(0, (int)std::floor(((double)gz0 + 32 - T.oz) / kTCell)));
    const uint16_t* tIdx = T.idx.data() + T.start[tcA * kTC + tcB];
    const int tN = (int)(T.start[tcA * kTC + tcB + 1] - T.start[tcA * kTC + tcB]);
    constexpr int L = 19, A = 66;  // lattice points a side; cells a side with a one-cell apron (for slopes)
    std::vector<Macro> M((size_t)L * L);
    for (int a = 0; a < L; a++)
        for (int b = 0; b < L; b++) {
            const double x = (double)(gx0 - 4 + 4 * a), z = (double)(gz0 - 4 + 4 * b);
            M[(size_t)a * L + b] = evalMacro(*this, x, z, T, tIdx, tN);
            applyErosion(*this, x, z, M[(size_t)a * L + b]);
        }
    auto blend = [&](int i, int j, auto get) {
        const int ii = i + 4, jj = j + 4, la = ii >> 2, lb = jj >> 2;
        const double fu = (ii & 3) * 0.25, fv = (jj & 3) * 0.25;
        const Macro &m00 = M[(size_t)la * L + lb], &m10 = M[(size_t)(la + 1) * L + lb];
        const Macro &m01 = M[(size_t)la * L + lb + 1], &m11 = M[(size_t)(la + 1) * L + lb + 1];
        return (get(m00) * (1 - fu) + get(m10) * fu) * (1 - fv) + (get(m01) * (1 - fu) + get(m11) * fu) * fv;
    };
    // (The pieces whose beds, banks and floors come near it: those are cut exactly, metre by metre.)
    std::vector<const DrainSeg*> beds;
    for (int k = 0; k < tN; k++) {
        const DrainSeg& s = T.segs[tIdx[k]];
        if (s.cls == 0) continue;
        // (a natural bed swings and widens out to ~5 half-widths: every chunk it reaches must cut it, or a seam)
        const double r = (naturalBed(s) ? std::max((double)s.hw + s.F, 5.5 * s.hw + 2) : (double)s.hw + s.F) + 6, cx0 = (double)gx0 - T.ox - 2, cz0 = (double)gz0 - T.oz - 2;
        if (std::max(s.ax, s.bx) + r < cx0 || std::min(s.ax, s.bx) - r > cx0 + 68 || std::max(s.az, s.bz) + r < cz0 || std::min(s.az, s.bz) - r > cz0 + 68) continue;
        beds.push_back(&s);
    }
    // The trails that come near this chunk.
    std::vector<std::shared_ptr<const TrailEdge>> edges;
    {
        const auto te = std::chrono::steady_clock::now();
        const int64_t ci = floorDiv(gx0 + 32, kCell), cj = floorDiv(gz0 + 32, kCell);
        for (int64_t i = ci - 2; i <= ci + 1; i++)  // (an edge runs east or north of its cell's node, wandering up to ~260 m off)
            for (int64_t j = cj - 2; j <= cj + 1; j++)
                for (int dir = 0; dir < 3; dir++) {
                    auto e = edge(i, j, dir);
                    if (e->on && e->x1 >= (double)gx0 - 2 && e->x0 <= (double)gx0 + 66 && e->z1 >= (double)gz0 - 2 && e->z0 <= (double)gz0 + 66) edges.push_back(e);
                }
        c->edgeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - te).count();
    }
    // (Each one's stretches that come within reach of this chunk: the rest needn't be looked at cell by cell.)
    std::vector<std::vector<uint32_t>> reach(edges.size());
    for (size_t q = 0; q < edges.size(); q++) {
        const TrailEdge& e = *edges[q];
        const double rx0 = (double)gx0 - 2 - kTreadReach - e.ax, rx1 = (double)gx0 + 66 + kTreadReach - e.ax;
        const double rz0 = (double)gz0 - 2 - kTreadReach - e.az, rz1 = (double)gz0 + 66 + kTreadReach - e.az;
        for (size_t k = 0; k + 1 < e.x.size(); k++)
            if ((e.onBridge.empty() || (!e.onBridge[k] && !e.onBridge[k + 1])) && std::max(e.x[k], e.x[k + 1]) >= rx0 && std::min(e.x[k], e.x[k + 1]) <= rx1 && std::max(e.z[k], e.z[k + 1]) >= rz0 && std::min(e.z[k], e.z[k + 1]) <= rz1)
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
        float w[PB_COUNT] = {};  // the biomes' weights here
        uint8_t keep = 0;      // nothing grows or lies here (trails, water, their banks)
        int8_t bed = 0;        // 1 river or creek bed, 2 a dry wash, 3 trail tread, 4 road tread
        float lat = 0, along = 0;
        bool roadNear = false;
        bool bridge = false;   // under or by a bridge's deck or its approach: nothing at all grows or lies here
        double lakeW = -1e9;   // a worn-land lake's level, within its lattice's reach of here
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
            double bumpH = bump * (0.9 * (vnoise(P.s[S_BUMP1], x * 0.09 + 5.0, z * 0.09 + 1.0) - 0.5) + 0.35 * (vnoise(P.s[S_BUMP2], x * 0.31, z * 0.27) - 0.5));
            // (On a valley's floor the bumps only rise: never a hollow under the water's level beside it.)
            const double overFloor = h - blend(i, j, [](const Macro& m) { return m.floor; });
            bumpH += (std::fabs(bumpH) - bumpH) * (1 - smoothstep(0.6, 2.5, overFloor));
            h += bumpH;
            const double preCut = h;  // (the land before the channels' beds: a lake's dam stands back up over a bed cut through it)
            // The channels: each bed, its banks and floor, cut exactly here (the lowest of them: nothing is filled).
            {
                const double Mc = blend(i, j, [](const Macro& m) { return m.mass; }), Vc = blend(i, j, [](const Macro& m) { return m.vale; });
                const DrainSeg* arg = nullptr;
                double argL = 0, argD = 0, argV = 0, argHW = 0;
                // (Out to 18 m past its floor: exactly by the metre near it, eased into the 4 m lattice's blend from 8 m out, no step.)
                const double h0c = h;
                double cut = 0;
                for (const DrainSeg* s : beds) {
                    double d, t, bh;
                    segNear(*s, x - T.ox, z - T.oz, d, t, bh);
                    const double e = d - s->hw - s->F;
                    if (e > 18) continue;
                    const double lv = s->la + (s->lb - s->la) * t;
                    double v, hwHere = s->hw, dHere = d;
                    if (naturalBed(*s)) {  // (its bed as it lies here: varied, meandering)
                        v = valleyAt(*s, d, lv, s->notch ? Vc : Mc, false, bh);
                        const double nb = d > 5.5 * s->hw + 2 ? 1e30 : naturalBedAt(P, *s, T.ox, T.oz, x - T.ox, z - T.oz, lv, hwHere);  // (out of its reach: quick)
                        if (nb < 1e29) v = std::min(v, nb), dHere = 0;  // (in it: dHere 0 marks the bed)
                        else dHere = 1e9;
                    } else
                        v = valleyAt(*s, d, lv, s->notch ? Vc : Mc, true, bh);
                    const double c = (h0c - v) * (1 - smoothstep(8.0, 18.0, e));
                    if (c > cut) cut = c;
                    if (v < h0c - cut + 1e-9 && e < 8 && (!arg || v < argV)) arg = s, argL = lv, argD = naturalBed(*s) ? (dHere < 1 ? 0.0 : 1e9) : d, argV = v, argHW = naturalBed(*s) ? 1.0 : hwHere;
                }
                h = h0c - cut;
                if (arg) {
                    // (A natural bed says itself whether a point's in it (argD 0, argHW 1: inside a metre's half-width); a river's by its section.)
                    if (argD < argHW + 0.5) cl.bed = arg->wet > 0.5 ? 1 : 2;  // (a dry wash in the desert)
                    if (argD < argHW + 3 || (naturalBed(*arg) && argD < 1)) cl.keep = 1;
                    // (Water never stands over the valley's land round it: where a creek crosses a bigger one's floor on its way in, the
                    // floor is lower than its water, and it's dry there.)
                    const double valley = blend(i, j, [](const Macro& m) { return m.h; });
                    if (arg->wet > 0.5 && argD < argHW + 1.5 && h < argL - 0.02 && argL <= valley + 0.3 && !(arg->cls == 1 && creekPerched(*this, arg->ni, arg->nj)) &&
                        (argL <= valley - 0.2 || argL <= macroAt(x, z, false).h + 0.3)) {  // (near the line, the land itself, not the 4 m lattice's blend: at a channel's sharp turn the blend stands over it)
                        cl.water = argL;
                        const double sp = arg->cls == 3 ? (arg->ford ? 1.9 : 1.5) : arg->cls == 2 ? 1.0 : 0.7;  // (big rivers fast: fastest over a ford's riffle)
                        cl.fx = arg->dx * sp, cl.fz = arg->dz * sp;
                    }
                }
            }
            // The lakes: their cirque floors and dams exactly here; still water at their level where the ground's under it in a lake
            // (the lattice says which: the lake's valley), no channel's trench under it, and a gravel or sand shore round it.
            for (const Lake& L : T.lakes)
                if (rules().erosion.on <= 0 && dishAt(L, x, z) < h && lakeOkOf(*this, L)) h = std::min(h, dishAt(L, x, z) + std::max(0.0, bumpH));
            for (const Lake& L : T.lakes)
                if (rules().erosion.on <= 0 && damAt(L, x, z) > h && lakeOkOf(*this, L)) h = std::max(h, damAt(L, x, z));
            if (rules().erosion.on > 0)  // (the worn land's lakes: their dams were worn with the land; a channel's bed doesn't cut through one)
                for (const Lake& Lk : T.lakes)
                    if (h < preCut && damOn(*this, Lk)) {
                        const Macro& mn = M[(size_t)std::min(L - 1, (i + 6) >> 2) * L + std::min(L - 1, (j + 6) >> 2)];
                        h = std::max(h, std::min(preCut, rockDamAt(*this, Lk, x, z, mn.floorNode, mn.floorNode2)));
                    }
            {
                double W = -1e9;
                for (int qa = -1; qa <= 2; qa++)  // (the lattice round it, 4-8 m: is this ground in a lake's valley?)
                    for (int qb = -1; qb <= 2; qb++) {
                        const int la = std::min(L - 1, std::max(0, ((i + 4) >> 2) + qa)), lb = std::min(L - 1, std::max(0, ((j + 4) >> 2) + qb));
                        W = std::max(W, M[(size_t)la * L + lb].lakeLevel);
                    }
                bool upstream = false;  // (and this metre itself upstream of that lake's lip)
                if (rules().erosion.on > 0 && W > -1e8) {  // (the worn land's lakes: this metre nearer the lake's lattice points than the dry ones round it)
                    const int la = (i + 4) >> 2, lb = (j + 4) >> 2;
                    const double fu = ((i + 4) & 3) * 0.25, fv = ((j + 4) & 3) * 0.25;
                    auto in = [&](int qa, int qb) { return M[(size_t)std::min(L - 1, la + qa) * L + std::min(L - 1, lb + qb)].lakeLevel > -1e8 ? 1.0 : 0.0; };
                    upstream = (in(0, 0) * (1 - fu) + in(1, 0) * fu) * (1 - fv) + (in(0, 1) * (1 - fu) + in(1, 1) * fu) * fv >= 0.5;
                }
                for (const Lake& Lk : T.lakes)
                    if (rules().erosion.on <= 0)
                    if (std::fabs(Lk.kind == 1 ? lakeLevelOf(*this, Lk) - W : Lk.W - W) < 1e-6)
                        upstream |= Lk.kind == 1 || ((x - Lk.qx) * Lk.fx + (z - Lk.qz) * Lk.fz + damBow(Lk, std::fabs(-(x - Lk.qx) * Lk.fz + (z - Lk.qz) * Lk.fx)) < -0.5 && ((Lk.kind != 2 && Lk.kind != 4) || dishAt(Lk, x, z) < Lk.W + 0.5));  // (a tarn or pond: on its floor, exactly)
                if (!upstream && rules().erosion.on > 0 && W > -1e8 && h < W + 0.05 && h > W - 3) {
                    // (Just off a worn-land lake (within its lattice's reach) and under its level: a channel's bed or a bump's hollow
                    // the 4 m lattice didn't see. Built up to the shore, so the water never stands over the land beside it. Far under
                    // it, the land past the lip falling away down a steep valley below its dam: left as it is, not raised to a wall.)
                    h = W + 0.05, cl.water = -1e9, cl.fx = cl.fz = 0, cl.keep = 1, cl.bed = cl.bed == 1 ? 5 : cl.bed;
                }
                if (rules().erosion.on > 0) cl.lakeW = W;
                if (rules().erosion.on > 0 && W > -1e8 && cl.water > -1e8 && cl.water < W - 0.02 && h < W) cl.water = W;  // (a river into a lake that stands over it: its backwater, level with the lake, no step up)
                if (!upstream) W = -1e9;
                if (W > -1e8) {
                    if (h < W - 0.02) {
                        h = std::max(h, blend(i, j, [](const Macro& m) { return m.h; }));  // (no trench under the water)
                        if (h < W - 0.02) {
                            if (cl.water < W) cl.water = W, cl.fx = cl.fz = 0;
                            cl.keep = 1, cl.bed = 0;
                        }
                    }
                    // (the shore: a gravel or sand beach where it's gentle; where the land comes down steep to the water, the land as it is)
                    const int sa = std::min(L - 2, (i + 4) >> 2), sb = std::min(L - 2, (j + 4) >> 2);
                    const double sl = std::max(std::fabs(M[(size_t)(sa + 1) * L + sb].h - M[(size_t)sa * L + sb].h), std::fabs(M[(size_t)sa * L + sb + 1].h - M[(size_t)sa * L + sb].h)) / 4;
                    if (cl.water < W && h < W + 0.9 && (rules().erosion.on <= 0 || sl < 0.3)) cl.bed = cl.bed ? cl.bed : 5, cl.keep = 1;
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
                    const double bw = std::min(kTreadReach - 8, base + rules().batter * std::fabs(fill)), ed = std::sqrt(dd) - e.half();
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
                near[nNear++] = {wMax, s8y / s8, edMin, std::min(kTreadReach - 8, base + rules().batter * std::fabs(s8f / s8)), e.road};
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
            double bridgeOff = 1e9, bridgeY = 0;  // (how far outside the nearest bridge's clear ground, and its deck)
            for (const auto& ep : edges)  // (a bridge's span and approaches, 8 m past each end and 5 m either side of the deck: clear)
                for (const TrailEdge::Span& sp : ep->bridges) {
                    const double vx = sp.bx - sp.ax, vz = sp.bz - sp.az, len = std::sqrt(vx * vx + vz * vz);
                    if (len < 1e-6) continue;
                    const double ux = vx / len, uz = vz / len, rx = x - sp.ax, rz = z - sp.az;
                    const double along = rx * ux + rz * uz, across = std::fabs(-rx * uz + rz * ux);
                    if (along > -8 && along < len + 8 && across < ep->half() + 5) cl.keep = 1, cl.bridge = true;
                    const double off = std::max({0.0, -8 - along, along - len - 8, across - ep->half() - 5});
                    if (off < bridgeOff) bridgeOff = off, bridgeY = sp.y;
                }
            if (bridgeOff <= 0 && h > bridgeY) h = bridgeY;  // (a bridge's ends and approaches: the ground never over its deck)
            for (const Plaza& pz : plazas) {  // (level ground for the place's buildings, easing into the land)
                const double d = std::sqrt(sqr(x - pz.x) + sqr(z - pz.z));
                const double bw = std::min(80.0, std::max(14.0, rules().batter * std::fabs(pz.h - h)));  // (as the trails' earthworks)
                if (d > pz.r + bw) continue;
                const double h1 = h + (pz.h - h) * smoothstep(pz.r + bw, pz.r, d);
                // (A bridge and its approaches keep their own grade: a place's earthworks never raise the ground over a deck, rising
                // from it no steeper than 1:1 away from its clear ground.)
                h = h1 > h && bridgeOff < 1e8 ? std::max(h, std::min(h1, bridgeY + bridgeOff)) : h1;
                if (d < pz.r + 3) cl.keep = 1, cl.water = -1e9, cl.fx = cl.fz = 0;
                if (pz.lot && d < pz.r - 1) cl.bed = 4;
            }
            // (By a worn-land lake, after the trails and places have had their way: water where the ground's under its level, else
            // the ground never under it beside the water.)
            if (cl.lakeW > -1e8 && h < cl.lakeW - 0.02 && cl.water < cl.lakeW) {
                if (cl.bed == 3 || cl.bed == 4) cl.water = cl.lakeW, cl.fx = cl.fz = 0;  // (a tread into the shallows)
                else h = cl.lakeW + 0.05;
            }
            cl.h = h;
            int b1 = 0, b2 = -1;
            for (int b = 1; b < PB_COUNT; b++)
                if (w[b] > w[b1]) b1 = b;
            for (int b = 0; b < PB_COUNT; b++)
                if (b != b1 && (b2 < 0 || w[b] > w[b2])) b2 = b;
            const double t = w[b2] / (w[b1] + w[b2]);  // 0..0.5
            const double dith = 0.5 + 0.35 * (vnoise(P.s[S_DITHER], x * 0.03, z * 0.03) - 0.5);  // (one short, natural edge where two biomes meet: no patchwork of the other inside)
            cl.dom = (uint8_t)(2 * t > dith ? b2 : b1);
            if (w[PB_BADLANDS] > 0.15 + 0.1 * (dith - 0.5)) cl.dom = PB_BADLANDS;  // (mesa country is desert ground right across: red sand and sage, no grass)
            for (int b = 0; b < PB_COUNT; b++) cl.w[b] = (float)w[b];
        }
    auto CC = [&](int i, int j) -> const Cell& { return C[(size_t)(i + 1) * A + (j + 1)]; };
    auto slopeAt = [&](int i, int j) { return std::sqrt(sqr(CC(i + 1, j).h - CC(i - 1, j).h) + sqr(CC(i, j + 1).h - CC(i, j - 1).h)) * 0.5; };
    // (The alpine rules, procrules.h: the tree line here (a slow noise round its height), and how far each point lies in a hollow (the
    // land 32 m round over it, from a 16 m lattice of the land: gullies hold snow). Only worked out where the snow could lie.)
    const AlpineRule& AR = rules().alpine;
    auto treeLineAt = [&](double x, double z) { return AR.treeLine + AR.treeLineWobble * (2 * fbm(P.s[S_SNOW] + 1, x * 0.0022, z * 0.0022, 2) - 1); };
    double hollowLat[5][5] = {};
    bool snowy = false;
    {
        double top = -1e9;
        for (const Cell& c0 : C) top = std::max(top, c0.h);
        snowy = top > AR.snowPatchy - AR.snowNorth - AR.snowHollow - 10;
        if (snowy && AR.snowHollow > 0)
            for (int a = 0; a < 5; a++)
                for (int b = 0; b < 5; b++) {
                    const double x = (double)gx0 + 16 * a, z = (double)gz0 + 16 * b;
                    const double h0 = land(x, z).h, ring = 0.25 * (land(x + 32, z).h + land(x - 32, z).h + land(x, z + 32).h + land(x, z - 32).h);
                    hollowLat[a][b] = clamp01((ring - h0) / 10);
                }
    }
    auto hollowAt = [&](int i, int j) {
        const double u = std::min(3.999, i / 16.0), v = std::min(3.999, j / 16.0);
        const int a = (int)u, b = (int)v;
        const double fu = u - a, fv = v - b;
        return (hollowLat[a][b] * (1 - fu) + hollowLat[a + 1][b] * fu) * (1 - fv) + (hollowLat[a][b + 1] * (1 - fu) + hollowLat[a + 1][b + 1] * fu) * fv;
    };
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
                case PB_MOUNTAINS: {  // (the alpine rules, procrules.h)
                    const double tl = treeLineAt(x, z);
                    // Snow: more the higher (lower on north faces and in gullies), in patches that join into snowfields.
                    const double dhdz = (CC(i, j + 1).h - CC(i, j - 1).h) * 0.5;
                    const double north = clamp01(-dhdz * 3), hEff = h + AR.snowNorth * north + AR.snowHollow * (snowy ? hollowAt(i, j) : 0.0);
                    const double cover = AR.snowFull > AR.snowPatchy ? smoothstep(AR.snowPatchy, AR.snowFull, hEff) : (hEff > AR.snowFull ? 1.0 : 0.0);
                    const double patch = smoothstep(0.3, 0.7, fbm(P.s[S_SNOW] + 2, x * 0.03, z * 0.03, 3));  // (0..1, patches ~30 m across)
                    const double ledge = (h + 6 * n2) / AR.rockBandSpacing;
                    const bool band = slope > AR.rockBandSlope && ledge - std::floor(ledge) < AR.rockBandWidth;
                    if (cover > patch && slope < AR.snowSlopeMax) s = SURF_SNOW, pt = PAINT_SNOW;
                    else if (slope > AR.rockSlope) s = SURF_ROCK;
                    else if (band) s = SURF_ROCK, pt = PAINT_ROCK;
                    else if (h > tl - 40 && ((slope > AR.screeSlope && n1 > 0.5) || (h > tl + 200 && n2 > 0.55))) s = SURF_SCREE, pt = PAINT_SCREE;
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
            else if (cl.bed == 5 && s != SURF_SNOW) {  // (a lake's shore: sand in the desert, gravel elsewhere)
                if (cl.dom == PB_BADLANDS || cl.dom == PB_DUNES) s = SURF_SAND, pt = PAINT_SAND;
                else s = SURF_GRAVEL, pt = PAINT_GRAVEL;
            }
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
    // Trees, by the rules (procrules.h): each spot tried is kept as often as the density there allows (the biomes' weights there, the
    // groves, the alpine meadows, the slope and the tree line), so there's no step at a chunk's edge; its kind by the weighted shares.
    const Rules& RU = rules();
    double maxHa = AR.treesPerHa;
    for (int b = 0; b < PB_COUNT; b++) maxHa = std::max(maxHa, b == PB_MOUNTAINS ? AR.treesPerHa : RU.trees[b].perHa);
    const double haChunk = kChunk * kChunk / 10000.0;
    const int tries = (int)(maxHa * haChunk + rr());
    std::vector<std::pair<float, float>> placed;
    for (int k = 0; k < tries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr(), keep = rr(), q = rr(), q2 = rr(), q3 = rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double h = c->h[id], slope = slopeAt(i, j);
        const Cell& cl = CC(i, j);
        if (!open(i, j) || slope > AR.treeSlopeMax || c->surf[id] == SURF_SAND || c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || c->paint[id] == PAINT_TRAIL) continue;
        const double gx = (double)gx0 + fx, gz = (double)gz0 + fz;
        const double tl = treeLineAt(gx, gz);
        if (h > tl) continue;
        const double groveHere = smoothstep(0.36, 0.62, fbm(P.s[S_GROVE], gx * 0.004, gz * 0.004, 3));
        const double meadow = AR.meadowShare > 0 ? smoothstep(0.36 + 0.28 * AR.meadowShare - 0.03, 0.36 + 0.28 * AR.meadowShare + 0.03, fbm(P.s[S_GROVE] + 5, gx / AR.meadowSize, gz / AR.meadowSize, 3)) : 1.0;
        double dens = 0;
        for (int b = 0; b < PB_COUNT; b++) {
            const double per = b == PB_MOUNTAINS ? AR.treesPerHa * meadow : RU.trees[b].perHa * (RU.trees[b].groveMin + (1 - RU.trees[b].groveMin) * groveHere);
            dens += cl.w[b] * per;
        }
        dens *= 1 - smoothstep(tl - AR.treeLineFade, tl, h) * 0.85;  // (thinning to the line: a few stunted ones right up to it)
        if (keep * maxHa >= dens) continue;
        bool spaced = true;
        for (const auto& pq : placed) spaced &= sqr(pq.first - fx) + sqr(pq.second - fz) > sqr(AR.trunkSpacing);  // (drivable between, tight in places)
        if (!spaced) continue;
        TreeSpec t;
        t.x = (float)fx, t.z = (float)fz;
        // (Its kind: conifer, snag or broadleaf, by the biomes' shares here; in the alpine, aspens only low down.)
        double conifer = 0, snag = 0;
        for (int b = 0; b < PB_COUNT; b++) {
            if (b == PB_MOUNTAINS) {
                const double aspen = h < AR.aspenBelow ? AR.aspenShare : 0;
                conifer += cl.w[b] * AR.coniferShare * (1 - aspen), snag += cl.w[b] * (1 - AR.coniferShare) * (1 - aspen);
            } else {
                double cf = RU.trees[b].conifer;
                if (b == PB_FOREST) cf *= 1 - 0.65 * smoothstep(0.6, 0.9, mid.wet);  // (broadleaf in the wet woods)
                conifer += cl.w[b] * cf, snag += cl.w[b] * RU.trees[b].snag;
            }
        }
        t.kind = q < snag ? 2 : q < snag + conifer ? 0 : 1;
        double th = t.kind == 0 ? 9 + q2 * 10 : t.kind == 1 ? 7 + q2 * 7 : 5 + q2 * 6;
        th *= 1 - 0.4 * (cl.w[PB_BADLANDS] + cl.w[PB_DUNES]);
        th *= 1 - AR.krummholz * smoothstep(tl - AR.treeLineFade, tl, h);  // (stunted up at the line)
        t.height = (float)th;
        t.radius = (float)(th * (t.kind == 0 ? 0.2 + q3 * 0.06 : 0.26 + q3 * 0.08));
        t.tint = (float)(0.8 + rr() * 0.4);
        placed.push_back({(float)fx, (float)fz});
        c->trees.push_back(t);
    }
    // Rocks: more on the steep and in the badlands and mountains; now and then a big outcrop; stones along the riffles' edges.
    const double rockRate = 1.2 * w[PB_MEADOW] + 2.0 * w[PB_FOREST] + 6.0 * w[PB_BADLANDS] + 0.4 * w[PB_DUNES];
    const ScatterRule& SCR = RU.scatter;
    // (A big rock reaches out of its chunk: clear of every trail's tread within its reach, read off the trails themselves.)
    auto trailNear = [&](double fx, double fz, double reach) {
        const double gx = (double)gx0 + fx, gz = (double)gz0 + fz;
        for (const auto& ep : edges) {
            const TrailEdge& e = *ep;
            const double R = reach + e.half() + 2;
            if (gx < e.x0 - R + kTreadReach || gx > e.x1 + R - kTreadReach || gz < e.z0 - R + kTreadReach || gz > e.z1 + R - kTreadReach) continue;
            for (size_t k = 0; k < e.x.size(); k += 2) {
                const double dx = e.ax + e.x[k] - gx, dz = e.az + e.z[k] - gz;
                if (dx * dx + dz * dz < R * R) return true;
            }
        }
        return false;
    };
    const int rockTries = (int)(SCR.rockRate * (rockRate * 1.6 + w[PB_MOUNTAINS] * AR.rocksPerHa * haChunk) + rr());
    for (int k = 0; k < rockTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const double slope = slopeAt(i, j);
        if (!open(i, j)) continue;
        if (c->surf[id] == SURF_SAND && rr() < 0.85) continue;
        if (rr() > 0.25 + 0.75 * smoothstep(0.15, 0.6, slope)) continue;
        const bool big = slope > 0.4 && rr() < 0.15;
        const double rad = SCR.rockSize * (big ? 1.4 + 2.2 * rr() : 0.35 + rr() * rr() * 0.9), flat = 0.5 + 0.4 * rr();
        if (rad > 2 && trailNear(fx, fz, rad)) continue;
        RockSpec r;
        r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * flat), r.rz = (float)(rad * (0.7 + 0.3 * rr()));
        r.top = (float)(rad * flat * (0.5 + 0.5 * rr()));
        r.tint = (float)(0.85 + 0.25 * rr());
        r.seed = (uint32_t)(hashIJ(seed_ ^ 0x50C4ull, cx, cz) >> 8) + (uint32_t)k * 7919u;
        if (rad > 1.2) r.seed = wholeRockSeed(r.seed);  // (a big one off the trails: its single hull, not cut into pieces: the memory)
        if (rr() < SCR.clusterShare) {  // (a cluster (rules 5): this one bigger, smaller ones round it)
            const double grow = 1.5;
            r.rx *= (float)grow, r.ry *= (float)grow, r.rz *= (float)grow, r.top *= (float)grow;
            r.seed = wholeRockSeed(r.seed * 2246822519u);
            const int extra = (int)(SCR.clusterRocks * (0.5 + rr()) + 0.5);
            for (int q = 0; q < extra; q++) {
                const int dq = (int)(8 * rr()) & 7;
                const double dist = r.rx * (1.1 + 1.6 * rr()), qx = fx + kDir8[dq][0] * dist, qz = fz + kDir8[dq][1] * dist;
                if (qx < 0 || qz < 0 || qx >= 64 || qz >= 64) continue;
                int qi, qj;
                cellOf(qx, qz, qi, qj);
                if (!open(qi, qj)) continue;
                const double qr = rad * (0.25 + 0.4 * rr()), qf = 0.5 + 0.4 * rr();
                RockSpec s2;
                s2.x = (float)qx, s2.z = (float)qz, s2.rx = (float)qr, s2.ry = (float)(qr * qf), s2.rz = (float)(qr * (0.7 + 0.3 * rr()));
                s2.top = (float)(qr * qf * (0.5 + 0.5 * rr()));
                s2.tint = r.tint;
                s2.seed = wholeRockSeed(r.seed * 2654435761u + 101u + (uint32_t)q * 31u);
                c->rocks.push_back(s2);
            }
        }
        c->rocks.push_back(r);
    }
    {  // The rated trails' rocks (rules 6: blue rocks, black boulders): beside the tread, and low ones on a black one's
        for (const auto& ep : edges) {
            const TrailEdge& e = *ep;
            if (e.road || e.rating == 0 || e.x.size() < 4) continue;
            const double side = e.rating >= 2 ? SCR.blackRocks : SCR.blueRocks, on = e.rating >= 2 ? SCR.blackTread : 0;
            const uint64_t ek = hashIJ(seed_ ^ 0x7A11ull, std::llround(e.ax), std::llround(e.az));
            for (size_t k = 8; k + 8 < e.x.size(); k++) {
                const double gxk = e.ax + e.x[k], gzk = e.az + e.z[k];
                const double fx = gxk - (double)gx0, fz = gzk - (double)gz0;
                if (fx < -4 || fz < -4 || fx > 68 || fz > 68) continue;
                if (!e.onBridge.empty() && e.onBridge[k]) continue;
                const double seg = e.along[k + 1] - e.along[k], u = hash01(ek, (int64_t)k, 1), v = hash01(ek, (int64_t)k, 2);
                const double tx = e.x[k + 1] - e.x[k - 1], tz = e.z[k + 1] - e.z[k - 1], tl = std::sqrt(tx * tx + tz * tz) + 1e-9;
                const double nx = -tz / tl, nz = tx / tl, sd = v < 0.5 ? -1.0 : 1.0;
                double px = fx, pz = fz, rad, top;
                if (u < side * seg / 100) {  // (beside it: a rock at the tread's edge, a wheel's width in or out)
                    const double off = e.half() + 0.2 + 1.2 * hash01(ek, (int64_t)k, 3);
                    px += nx * sd * off, pz += nz * sd * off, rad = 0.45 + 0.7 * hash01(ek, (int64_t)k, 4), top = rad * 0.55;
                } else if (u < (side + on) * seg / 100) {  // (on it: low, rounded, driven over)
                    const double off = (hash01(ek, (int64_t)k, 3) - 0.5) * e.half();
                    px += nx * off, pz += nz * off, rad = 0.3 + 0.25 * hash01(ek, (int64_t)k, 4), top = 0.22 + 0.12 * hash01(ek, (int64_t)k, 5);
                } else
                    continue;
                if (px < 0 || pz < 0 || px >= 64 || pz >= 64) continue;
                int i, j;
                cellOf(px, pz, i, j);
                const Cell& cq = CC(i, j);
                if (cq.water > -1e8 || cq.bridge || cq.bed == 4) continue;  // (not in a ford, by a bridge, nor on a place's lot or a main trail)
                RockSpec r;
                r.x = (float)px, r.z = (float)pz, r.rx = (float)rad, r.ry = (float)(rad * 0.6), r.rz = (float)(rad * (0.75 + 0.25 * hash01(ek, (int64_t)k, 6)));
                r.top = (float)top;
                r.tint = (float)(0.85 + 0.2 * hash01(ek, (int64_t)k, 7));
                r.seed = (uint32_t)(ek >> 20) + (uint32_t)k * 2654435761u;
                c->rocks.push_back(r);
            }
        }
    }
    {  // Downed trees across the trails in thick woods (owner): a trunk over the tread to drive over, or round where it's thick
        const double woods = w[PB_FOREST] + 0.8 * w[PB_MOUNTAINS] + 0.3 * w[PB_MEADOW] * grove;
        const double per100 = SCR.downedTrees * smoothstep(0.35, 0.85, woods);
        if (per100 > 0)
            for (const auto& ep : edges) {
                const TrailEdge& e = *ep;
                if (e.x.size() < 20) continue;
                const uint64_t ek = hashIJ(seed_ ^ 0x10C5ull, std::llround(e.ax), std::llround(e.az));
                for (size_t k = 10; k + 10 < e.x.size(); k++) {
                    const double fx = e.ax + e.x[k] - (double)gx0, fz = e.az + e.z[k] - (double)gz0;
                    if (fx < 0 || fz < 0 || fx >= 64 || fz >= 64) continue;
                    if (!e.onBridge.empty() && e.onBridge[k]) continue;
                    if (hash01(ek, (int64_t)k, 31) >= per100 * (e.along[k + 1] - e.along[k]) / 100) continue;
                    int i, j;
                    cellOf(fx, fz, i, j);
                    if (CC(i, j).water > -1e8 || CC(i, j).bridge || CC(i, j).bed == 4) continue;
                    const double tx = e.x[k + 1] - e.x[k - 1], tz = e.z[k + 1] - e.z[k - 1], tl = std::sqrt(tx * tx + tz * tz) + 1e-9;
                    // (across it, slanted up to ~35 degrees, reaching past both edges; thick ones now and then, to go round)
                    const double sl = (hash01(ek, (int64_t)k, 32) - 0.5) * 1.2, nx = -tz / tl + sl * tx / tl, nz = tx / tl + sl * tz / tl, nl = std::sqrt(nx * nx + nz * nz);
                    const double L2 = e.half() + 2.5 + 3 * hash01(ek, (int64_t)k, 33), r = 0.2 + 0.18 * hash01(ek, (int64_t)k, 34) + (hash01(ek, (int64_t)k, 35) < 0.2 ? 0.2 : 0.0);
                    const double off = (hash01(ek, (int64_t)k, 36) - 0.5) * 2;
                    const double cx2 = fx + nx / nl * off, cz2 = fz + nz / nl * off;
                    c->logs.push_back({(float)(cx2 - nx / nl * L2), (float)(cz2 - nz / nl * L2), (float)(cx2 + nx / nl * L2), (float)(cz2 + nz / nl * L2), (float)r});
                }
            }
    }
    {  // Rocks by the steepness (procrules.h SlopeRockRule): scree on the moderate slopes, rock walls on the steep, boulders at their feet.
        const SlopeRockRule& SR = RU.slopeRock;
        const double bio = w[PB_MOUNTAINS] + SR.desert * (w[PB_BADLANDS] + w[PB_DUNES]) + SR.lowland * (w[PB_MEADOW] + w[PB_FOREST]);
        const double round = w[PB_BADLANDS] + w[PB_DUNES];  // (the desert's rock rounder)
        auto cellH = [&](double fx, double fz) {
            const int a = std::min(63, std::max(0, (int)fx)), b = std::min(63, std::max(0, (int)fz));
            return (double)c->h[(size_t)a * kChunk + b];
        };
        // (Clear of the trails, water, bridges and places: open ground right round its footprint.)
        auto clearRound = [&](double fx, double fz, double rad) {
            if (rad > 2 && trailNear(fx, fz, rad)) return false;
            for (int q = 0; q < 9; q++) {
                const double ax = q == 8 ? 0 : kDir8[q][0], az = q == 8 ? 0 : kDir8[q][1];
                int i, j;
                cellOf(std::min(63.9, std::max(0.0, fx + ax * (rad + 1.5))), std::min(63.9, std::max(0.0, fz + az * (rad + 1.5))), i, j);
                if (!open(i, j) || CC(i, j).roadNear || CC(i, j).bridge) return false;
            }
            return true;
        };
        auto laid = [&](double fx, double fz, double rad, double thick, double across, uint32_t sd, double sink) {
            // (Laid along the slope: its long axis up the fall line, its face the slope's; sunk so `sink` of its thickness shows.)
            int i, j;
            cellOf(fx, fz, i, j);
            const double gx = (CC(i + 1, j).h - CC(i - 1, j).h) * 0.5, gz = (CC(i, j + 1).h - CC(i, j - 1).h) * 0.5, g = std::sqrt(gx * gx + gz * gz);
            RockSpec r;
            r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)thick, r.rz = (float)across;
            const double pitch = detAtan2(g, 1.0), sp = g / std::sqrt(1 + g * g), cp = 1 / std::sqrt(1 + g * g);
            r.yaw = g > 1e-6 ? (float)detAtan2(-gz, gx) : 0.0f, r.pitch = g > 1e-6 ? (float)pitch : 0.0f;
            if (r.pitch == 0) r.pitch = 1e-4f;
            r.top = (float)(rad * sp + 2 * sink * thick * cp);  // (its top (the uphill end's) over the ground at its middle: `sink` of its thickness out)
            r.tint = (float)(0.8 + 0.25 * rr());
            r.seed = wholeRockSeed(sd);
            c->rocks.push_back(r);
        };
        const uint32_t base = (uint32_t)(hashIJ(seed_ ^ 0x5C4Eull, cx, cz) >> 8);
        // Scree and outcrops, and the rock walls: tried over the chunk, kept by the steepness where they land.
        const int tries = (int)(bio * std::max(SR.screePerHa, SR.cliffPerHa) * haChunk + rr());
        for (int k = 0; k < tries; k++) {
            const double fx = 64 * rr(), fz = 64 * rr(), keep = rr(), q = rr(), q2 = rr();
            int i, j;
            cellOf(fx, fz, i, j);
            const double slope = slopeAt(i, j);
            if (slope < SR.screeFrom) continue;
            const double wall = smoothstep(SR.cliffFrom, SR.cliffFull, slope);
            const double scree = smoothstep(SR.screeFrom, SR.cliffFrom, slope) * (1 - wall);
            const double want = wall * SR.cliffPerHa + scree * SR.screePerHa;
            if (keep * std::max(SR.screePerHa, SR.cliffPerHa) >= want) continue;
            if (q < wall && rr() < SR.torShare) {  // (a tor: a house-sized outcrop standing up out of the face, sunk a third)
                const double rad = SR.torSize * (0.67 + 0.66 * q2);
                if (!clearRound(fx, fz, rad)) continue;
                RockSpec r;
                r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * (0.7 + 0.3 * rr())), r.rz = (float)(rad * (0.7 + 0.3 * rr()));
                r.top = (float)(r.ry * (0.9 + 0.4 * rr()));
                r.tint = (float)(0.8 + 0.25 * rr());
                r.seed = wholeRockSeed(base + 9u + (uint32_t)k * 7919u);
                c->rocks.push_back(r);
            } else if (q < wall) {  // (a slab of the wall: big, laid along the face, overlapping its neighbours into a solid face)
                const double rad = SR.cliffSize * (0.67 + 0.66 * q2);
                if (!clearRound(fx, fz, rad)) continue;
                laid(fx, fz, rad, rad * (0.28 + 0.12 * rr() + 0.25 * round), rad * (0.7 + 0.4 * rr()), base + (uint32_t)k * 7919u, 0.5);
            } else {  // (scree: blocks lying on the slope, half sunk; now and then an outcrop)
                const double t = smoothstep(SR.screeFrom, SR.cliffFrom, slope);
                double rad = (0.8 + (SR.screeSize - 0.8) * t) * (0.5 + q2);
                if (rr() < 0.08) rad *= 2.2;
                if (!clearRound(fx, fz, rad)) continue;
                laid(fx, fz, rad, rad * (0.45 + 0.25 * rr() + 0.25 * round), rad * (0.7 + 0.3 * rr()), base + 3u + (uint32_t)k * 7919u, 0.6);
            }
        }
        // The boulder fields at the walls' feet: gentler ground with a wall just up the fall line.
        const int btries = (int)(bio * SR.basePerHa * haChunk + rr());
        for (int k = 0; k < btries; k++) {
            const double fx = 64 * rr(), fz = 64 * rr(), q = rr();
            int i, j;
            cellOf(fx, fz, i, j);
            const double slope = slopeAt(i, j);
            if (slope >= SR.cliffFrom) continue;
            const double gx = (CC(i + 1, j).h - CC(i - 1, j).h) * 0.5, gz = (CC(i, j + 1).h - CC(i, j - 1).h) * 0.5, g = std::sqrt(gx * gx + gz * gz);
            if (g < 1e-4) continue;
            double foot = -1;  // (how far up the fall line the wall starts)
            for (double d = 3; d <= SR.baseReach && foot < 0; d += 3) {
                const double ux = fx + gx / g * d, uz = fz + gz / g * d;
                if (ux < 1 || uz < 1 || ux > 62 || uz > 62) break;
                const double ddx = cellH(ux + 1, uz) - cellH(ux - 1, uz), ddz = cellH(ux, uz + 1) - cellH(ux, uz - 1), s2 = std::sqrt(ddx * ddx + ddz * ddz) * 0.5;
                if (s2 > SR.cliffFrom) foot = d;
            }
            if (foot < 0) continue;
            const double near = 1 - foot / SR.baseReach;
            const double rad = (0.9 + 3.0 * near) * (0.6 + 0.8 * q);  // (the biggest, car- to house-sized, nearest the wall)
            if (!clearRound(fx, fz, rad)) continue;
            RockSpec r;
            r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * (0.6 + 0.3 * rr() + 0.2 * round)), r.rz = (float)(rad * (0.7 + 0.3 * rr()));
            r.top = (float)(r.ry * (1.1 + 0.5 * rr()));
            r.tint = (float)(0.8 + 0.25 * rr());
            r.seed = wholeRockSeed(base + 5u + (uint32_t)k * 6151u);
            c->rocks.push_back(r);
        }
    }
    for (int k = 0; k < 10 && anyWater; k++) {  // (the river's edges: a few stones where the water's shallow)
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        cellOf(fx, fz, i, j);
        const Cell& cl = CC(i, j);
        if (cl.water < -1e8 || cl.water - cl.h > 0.35 || cl.roadNear || cl.lat != 0 || cl.bridge) continue;
        const double rad = 0.3 + 0.35 * rr();
        RockSpec r;
        r.x = (float)fx, r.z = (float)fz, r.rx = (float)rad, r.ry = (float)(rad * 0.6), r.rz = (float)(rad * 0.85), r.top = (float)(rad * 0.45);
        r.tint = (float)(0.8 + 0.2 * rr());
        r.seed = (uint32_t)(hashIJ(seed_ ^ 0x51C4ull, cx, cz) >> 8) + (uint32_t)k * 6007u;
        c->rocks.push_back(r);
    }
    // A fallen log in the woods.
    for (int lt = 0; lt < 4; lt++)  // (up to four tries: logs_per_ha a hectare of woods, where there's level ground for one)
    if (w[PB_FOREST] + (w[PB_MEADOW] + w[PB_MOUNTAINS]) * grove > 0.3 && rr() < 0.4 * SCR.logsPerHa * haChunk * (w[PB_FOREST] + 0.5 * (w[PB_MEADOW] + w[PB_MOUNTAINS]))) {
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
    // The alpine forest floor (biome.alpine): fallen logs and stumps among the trees, below the tree line. Both are things to drive
    // over or round (logs: capsules; stumps: posts), kept off the trails and the water.
    {
        auto nearTree = [&](double fx, double fz, double r) {
            for (const auto& pq : placed)
                if (sqr(pq.first - fx) + sqr(pq.second - fz) < r * r) return true;
            return false;
        };
        const int logTries = (int)(w[PB_MOUNTAINS] * AR.logsPerHa * haChunk + rr());
        for (int k = 0; k < logTries; k++) {
            const double fx = 6 + 52 * rr(), fz = 6 + 52 * rr(), L2 = 1.5 + 3.5 * rr(), r = 0.12 + 0.2 * rr();
            double dx = rr() - 0.5, dz = rr() - 0.5;
            const double dl = std::sqrt(dx * dx + dz * dz) + 1e-9;
            dx /= dl, dz /= dl;
            int i, j, i2, j2, i3, j3;
            const int id = cellOf(fx, fz, i, j);
            cellOf(fx - dx * L2, fz - dz * L2, i2, j2), cellOf(fx + dx * L2, fz + dz * L2, i3, j3);
            if (c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || slopeAt(i, j) > 0.5 || !open(i, j) || !open(i2, j2) || !open(i3, j3)) continue;
            if (!nearTree(fx, fz, 7) || c->h[id] > treeLineAt((double)gx0 + fx, (double)gz0 + fz)) continue;
            bool clear = true;  // (not through a trunk)
            for (const auto& pq : placed)
                for (int t = -2; t <= 2 && clear; t++) clear = sqr(pq.first - (fx + dx * L2 * t / 2)) + sqr(pq.second - (fz + dz * L2 * t / 2)) > 1.0;
            if (clear) c->logs.push_back({(float)(fx - dx * L2), (float)(fz - dz * L2), (float)(fx + dx * L2), (float)(fz + dz * L2), (float)r});
        }
        const int stumpTries = (int)(w[PB_MOUNTAINS] * AR.stumpsPerHa * haChunk + rr());
        for (int k = 0; k < stumpTries; k++) {
            const double fx = 64 * rr(), fz = 64 * rr(), r = 0.2 + 0.2 * rr(), hh = 0.3 + 0.5 * rr(), yaw = 6.283 * rr();
            int i, j;
            const int id = cellOf(fx, fz, i, j);
            if (c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || slopeAt(i, j) > 0.6 || !open(i, j) || nearTree(fx, fz, 1.5) || !nearTree(fx, fz, 8)) continue;
            if (c->h[id] > treeLineAt((double)gx0 + fx, (double)gz0 + fz)) continue;
            c->stumps.push_back({(float)fx, (float)fz, (float)r, (float)hh, (float)yaw});
        }
    }
    // Bushes: what grows where (reeds by the water).
    // (And the alpine forest's undergrowth (biome.alpine undergrowth_per_ha): the more trees round it, the more of it, none in the open.)
    const int baseBush = (int)(14 * (1 + 1.6 * SCR.bushThicket) * (0.3 + 0.7 * std::max(grove, w[PB_BADLANDS] + w[PB_DUNES] * 0.3)) + rr()) + (anyWater ? 10 : 0);
    const int bushTries = baseBush + (int)((1 + 1.6 * SCR.bushThicket) * w[PB_MOUNTAINS] * AR.undergrowthPerHa * haChunk + rr());
    for (int k = 0; k < bushTries; k++) {
        const double fx = 64 * rr(), fz = 64 * rr();
        int i, j;
        const int id = cellOf(fx, fz, i, j);
        const int b = CC(i, j).dom;
        if (k >= baseBush) {  // (undergrowth: under the trees, below the tree line)
            int near = 0;
            for (const auto& pq : placed) near += sqr(pq.first - fx) + sqr(pq.second - fz) < 6.0 * 6.0;
            if (near == 0 || c->h[id] > treeLineAt((double)gx0 + fx, (double)gz0 + fz)) continue;
        }
        if (c->surf[id] == SURF_SNOW || c->surf[id] == SURF_ROCK || CC(i, j).water > -1e8 || CC(i, j).roadNear) continue;
        if (b == PB_DUNES && rr() < 0.8) continue;
        {  // (in thickets, 30-60 m across, open ground between: the Backcountry's)
            const double th = smoothstep(0.47, 0.53, fbm(P.s[S_GROVE] + 17, ((double)gx0 + fx) / 45.0, ((double)gz0 + fz) / 45.0, 2));
            if (rr() > 1 - 0.95 * SCR.bushThicket * (1 - th)) continue;
        }
        // (Near the water: reeds and wet ground; elsewhere what the biome grows.)
        bool wetSide = false;
        for (int a = -3; a <= 3 && !wetSide; a += 3)
            for (int q = -3; q <= 3 && !wetSide; q += 3) wetSide = i + a >= -1 && i + a <= 64 && j + q >= -1 && j + q <= 64 && CC(i + a, j + q).water > -1e8;
        if ((CC(i, j).keep && !wetSide) || CC(i, j).bridge) continue;
        BushSpec s;
        s.x = (float)fx, s.z = (float)fz, s.u = (float)rr(), s.tint = (float)(0.75 + 0.45 * rr());
        s.habitat = (uint8_t)(wetSide ? HAB_WETLAND : b == PB_BADLANDS || b == PB_DUNES ? HAB_DESERT : k >= baseBush ? HAB_FOREST : b == PB_MOUNTAINS ? HAB_UPLAND : b == PB_FOREST ? HAB_FOREST : HAB_MEADOW);
        s.edge = grove > 0.4;
        c->bushes.push_back(s);
    }
    c->genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() - c->edgeMs;  // (its own work: the trails are laid out ahead)
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
        const bool wet = (m.riverOn > 0.3 && std::fabs(m.riverS) < m.riverHW + 6) || (m.creekOn > 0.3 && std::fabs(m.creekS) < m.creekHW + 4) || m.lakeLevel > -1e8;
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
    // The waterfalls in its cell (one at most: the tallest), a place to find.
    {
        const double cx0 = (double)(i * kCell), cz0 = (double)(j * kCell);
        const auto wf = waterFeatures(cx0, cz0, cx0 + kCell, cz0 + kCell);
        const WaterFeature* best = nullptr;
        for (const WaterFeature& f : wf)
            if (f.kind == 1 && (!best || f.size > best->size)) best = &f;
        if (best) add("FALLS", placeName(i, j, 3) + " FALLS", PK_WATERFALL, best->x, best->z, 40, 0, 0);
    }
    // The fords on the trails out of it (east and north): river crossings.
    for (int dir = 0; dir < 2; dir++) {
        const auto e = edge(i, j, dir);
        if (e->on && e->ford) add(dir ? "FORD.N" : "FORD.E", nm + (dir ? " CROSSING" : " FORD"), PK_RIVER_CROSSING, e->fordX, e->fordZ, 45, 0, 0);
        if (e->on && !e->bridges.empty()) {  // (and its bridges over the big rivers)
            const TrailEdge::Span& b = e->bridges.front();
            add(dir ? "BRIDGE.N" : "BRIDGE.E", nm + (dir ? " LOG BRIDGE" : " BRIDGE"), PK_RIVER_CROSSING, 0.5 * (b.ax + b.bx), 0.5 * (b.az + b.bz), 45, 0, 0);
        }
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
        if (pl.kind != PK_CACHE && pl.kind != PK_RIVER_CROSSING && pl.kind != PK_ABANDONED_VEHICLE && pl.kind != PK_WATERFALL) sign(rel(P, yw, 3, -(pl.pad + 2)), yw, PROP_SIGN);
    }
}

// The timber bridges over the big rivers on the trails of a window (stand-ins: a plank deck on log stringers, rails, a crib pier in the
// middle of a long one), from the edges' spans, so every window that holds one builds it alike.
void buildBridges(World& w) {
    const ProcWorld& G = ProcWorld::forSeed(w.proc.seed);
    const double half = w.halfSize() - 12;
    const int64_t i0 = floorDiv(w.proc.ox - (int64_t)w.halfSize(), kCell) - 1, i1 = floorDiv(w.proc.ox + (int64_t)w.halfSize(), kCell) + 1;
    const int64_t j0 = floorDiv(w.proc.oz - (int64_t)w.halfSize(), kCell) - 1, j1 = floorDiv(w.proc.oz + (int64_t)w.halfSize(), kCell) + 1;
    const V3 plank(0.47, 0.35, 0.23), log(0.36, 0.27, 0.18);
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++)
            for (int dir = 0; dir < 2; dir++) {
                const auto e = G.edge(i, j, dir);
                if (!e->on) continue;
                for (const TrailEdge::Span& b : e->bridges) {
                    const double ax = b.ax - (double)w.proc.ox, az = b.az - (double)w.proc.oz, bx = b.bx - (double)w.proc.ox, bz = b.bz - (double)w.proc.oz;
                    const double mx = 0.5 * (ax + bx), mz = 0.5 * (az + bz);
                    if (std::fabs(mx) > half || std::fabs(mz) > half) continue;
                    const double dx = bx - ax, dz = bz - az, L = std::sqrt(dx * dx + dz * dz);
                    if (L < 1) continue;
                    const double yaw = std::atan2(-dz, dx), hw = e->half() + 0.6, sx = -dz / L, sz = dx / L;  // (sx, sz: to its left)
                    w.addBox(V3(mx, b.y - 0.15, mz), V3(0.5 * L + 1, 0.15, hw), yaw, 0, 0, plank);  // (the deck)
                    for (int sd = -1; sd <= 1; sd += 2) {
                        w.addBox(V3(mx + sx * sd * (hw - 0.3), b.y - 0.55, mz + sz * sd * (hw - 0.3)), V3(0.5 * L + 1, 0.25, 0.25), yaw, 0, 0, log);  // (stringers)
                        w.addBox(V3(mx + sx * sd * (hw + 0.05), b.y + 0.45, mz + sz * sd * (hw + 0.05)), V3(0.5 * L, 0.08, 0.08), yaw, 0, 0, log);    // (rails)
                        for (int k = 0; k <= 4; k++) {  // (rail posts)
                            const double t = -0.5 + 0.25 * k, px = mx + dx * t + sx * sd * (hw + 0.05), pz = mz + dz * t + sz * sd * (hw + 0.05);
                            w.poles.push_back({px, pz, b.y - 0.1, b.y + 0.55, 0.09});
                        }
                    }
                    if (L > 22) {  // (a crib pier of logs in the middle, from the bed up)
                        const double bed = w.height(mx, mz);
                        if (b.y - 0.7 > bed) w.addBox(V3(mx, 0.5 * (bed + b.y - 0.7) - 0.3, mz), V3(1.0, 0.5 * (b.y - 0.7 - bed) + 0.3, hw - 0.4), yaw, 0, 0, log);
                    }
                }
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

size_t cacheBytes() {
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(blockMutex);
        n += blockMap.size() * sizeof(DBlock);
    }
    {
        std::lock_guard<std::mutex> lock(tileMutex);
        for (const auto& m : tileMap)
            for (const auto& kv : m) n += sizeof(DTile) + kv.second->segs.capacity() * sizeof(DrainSeg) + kv.second->idx.capacity() * 2 + kv.second->start.capacity() * 4 + kv.second->lakes.capacity() * sizeof(Lake);
    }
    {
        std::lock_guard<std::mutex> lock(gridMutex);
        n += gridMap.size() * sizeof(GridTile);
    }
    {
        std::lock_guard<std::mutex> lock(eMutex);
        n += eMap.size() * (sizeof(ETile) + (size_t)kEN * kEN * 2 * sizeof(float));
    }
    {
        Cache& C = cache();
        std::lock_guard<std::mutex> lock(C.m);
        for (const auto& kv : C.map) {
            const Chunk& c = *kv.second.first;
            n += sizeof(Chunk) + c.h.capacity() * 4 + c.surf.capacity() + c.paint.capacity() + (c.water.capacity() + c.flowX.capacity() + c.flowZ.capacity() + c.roadLat.capacity() + c.roadAlong.capacity()) * 4 +
                 c.roadNear.capacity() + c.trees.capacity() * sizeof(TreeSpec) + c.rocks.capacity() * sizeof(RockSpec) + c.bushes.capacity() * sizeof(BushSpec) + c.logs.capacity() * sizeof(LogSpec) + c.stumps.capacity() * sizeof(StumpSpec);
        }
    }
    {
        std::lock_guard<std::mutex> lock(edgeMutex);
        for (const auto& kv : edgeCache) {
            const TrailEdge& e = *kv.second;
            n += sizeof(TrailEdge) + (e.x.capacity() + e.z.capacity() + e.y.capacity() + e.along.capacity() + e.fill.capacity()) * 4 + e.onBridge.capacity() + e.bridges.capacity() * sizeof(TrailEdge::Span);
        }
    }
    return n;
}

size_t cachedChunks() {
    Cache& C = cache();
    std::lock_guard<std::mutex> lock(C.m);
    return C.map.size();
}
void clearErosionTiles() {
    std::lock_guard<std::mutex> lock(eMutex);
    eMap.clear();
}

void clearChunkCache() {
    {
        Cache& C = cache();
        std::lock_guard<std::mutex> lock(C.m);
        C.map.clear(), C.order.clear();
    }
    clearDrainCaches();
}

double farHeight(const World& w, double x, double z) {
    const double half = w.halfSize();
    if (std::fabs(x) <= half && std::fabs(z) <= half) return w.height(x, z);
    const ProcWorld& G = ProcWorld::forSeed(w.proc.seed);
    const double gx = x + (double)w.proc.ox, gz = z + (double)w.proc.oz;
    return G.groundHeight(gx, gz, G.macroCached(gx, gz));  // (its valleys cut, as the window's; worn where its tiles are made)
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
        // (The worn land's tiles first, a few at once: the window and what its trails look at round it.)
        loadprof::Scope lp("proc.erosion");
        prewarmErosion(G, (double)gx0 - 3000, (double)gz0 - 3000, (double)gx0 + 2 * half + 3000, (double)gz0 + 2 * half + 3000);
    }
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
                for (const RockSpec& s : c.rocks) {
                    RockPose pose;
                    pose.yaw = s.yaw, pose.pitch = s.pitch;
                    addRock(lx0 + s.x, lz0 + s.z, V3(s.rx, s.ry, s.rz), s.top, s.seed, s.tint, s.pitch != 0 ? &pose : nullptr);
                }
                for (const StumpSpec& s : c.stumps) {
                    const double x = lx0 + s.x, z = lz0 + s.z, y = height(x, z);
                    fixtures.push_back({PROP_STUMP, V3(x, y, z), V3(s.r, s.h, 0), s.yaw});
                    poles.push_back({x, z, y - 0.3, y + s.h, s.r});
                }
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
        const size_t bridgeBox0 = boxes.size(), bridgePole0 = poles.size();
        buildBridges(*this);
        dressBridges(*this, bridgeBox0, bridgePole0);  // (procdress.h: the bridges' and falls' models)
        dressFalls(*this);
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
    // The tiles the next windows will want (it slides kSlide at a time), worn in the background, before they're needed.
    prefetchErosion(G, (double)gx0 - kSlide - 3000, (double)gz0 - kSlide - 3000, (double)gx0 + 2 * half + kSlide + 3000, (double)gz0 + 2 * half + kSlide + 3000);
}

namespace proc {
// (The far forest's hook: the tree rule of makeChunk's "Trees, by the rules", the same sums at one point. The chunks' own cells also
// rule out sand, snow, bare rock and trail paint; on far land that leaves the rock bands of steep alpine slopes, counted here as the
// share of the slope they take.)
static constexpr double kTreeRough = 1.5;  // (fitted: forest/far-field)
ProcWorld::TreeCover ProcWorld::treeCover(double gx, double gz, const Macro& m, double h, double slope) const {
    const Params& P = *params_;
    const Rules& RU = rules();
    const AlpineRule& AR = RU.alpine;
    TreeCover tc;
    tc.maxPerHa = AR.treesPerHa;
    for (int b = 0; b < PB_COUNT; b++) tc.maxPerHa = std::max(tc.maxPerHa, b == PB_MOUNTAINS ? AR.treesPerHa : RU.trees[b].perHa);
    const double tl = AR.treeLine + AR.treeLineWobble * (2 * fbm(P.s[S_SNOW] + 1, gx * 0.0022, gz * 0.0022, 2) - 1);
    if (h > tl || slope > AR.treeSlopeMax || slope > AR.rockSlope) return tc;
    const double groveHere = smoothstep(0.36, 0.62, fbm(P.s[S_GROVE], gx * 0.004, gz * 0.004, 3));
    const double meadow = AR.meadowShare > 0 ? smoothstep(0.36 + 0.28 * AR.meadowShare - 0.03, 0.36 + 0.28 * AR.meadowShare + 0.03, fbm(P.s[S_GROVE] + 5, gx / AR.meadowSize, gz / AR.meadowSize, 3)) : 1.0;
    double dens = 0, conifer = 0, snag = 0;
    for (int b = 0; b < PB_COUNT; b++) {
        const double per = b == PB_MOUNTAINS ? AR.treesPerHa * meadow : RU.trees[b].perHa * (RU.trees[b].groveMin + (1 - RU.trees[b].groveMin) * groveHere);
        dens += m.w[b] * per;
        if (b == PB_MOUNTAINS) {
            const double aspen = h < AR.aspenBelow ? AR.aspenShare : 0;
            conifer += m.w[b] * AR.coniferShare * (1 - aspen), snag += m.w[b] * (1 - AR.coniferShare) * (1 - aspen);
        } else {
            double cf = RU.trees[b].conifer;
            if (b == PB_FOREST) cf *= 1 - 0.65 * smoothstep(0.6, 0.9, m.wet);
            conifer += m.w[b] * cf, snag += m.w[b] * RU.trees[b].snag;
        }
    }
    dens *= 1 - smoothstep(tl - AR.treeLineFade, tl, h) * 0.85;
    if (slope > AR.rockBandSlope && m.w[PB_MOUNTAINS] > 0) dens *= 1 - AR.rockBandWidth * m.w[PB_MOUNTAINS];
    if (m.w[PB_DUNES] > 0.5) dens *= 0.2;  // (sand: the chunks' trees skip it)
    if (rules().erosion.on > 0) {  // (the worn land's gullies and spurs, under the field's spacing: their steep sides grow fewer (tiles made only: never waits))
        const double e = 8;
        const double gxE = proc::erosionAt(*this, gx + e, gz, false) - proc::erosionAt(*this, gx - e, gz, false);
        const double gzE = proc::erosionAt(*this, gx, gz + e, false) - proc::erosionAt(*this, gx, gz - e, false);
        const double rough = std::sqrt(gxE * gxE + gzE * gzE) / (2 * e);
        dens *= std::max(0.0, 1 - kTreeRough * rough);
    }
    tc.triedPerHa = dens, tc.conifer = conifer, tc.snag = snag;
    tc.shrink = 1 - (1 - 0.4 * (m.w[PB_BADLANDS] + m.w[PB_DUNES])) * (1 - AR.krummholz * smoothstep(tl - AR.treeLineFade, tl, h));
    return tc;
}
}  // namespace proc

namespace proc {
// (procmodels' hook: the falls' render data, read-only; the same falls as waterFeatures' kind 1, with the step's width and length.)
std::vector<FallSpan> fallSpans(const ProcWorld& G, double x0, double z0, double x1, double z1) {
    std::vector<FallSpan> out;
    const DrainClass DC = drainClass();
    const int64_t i0 = (int64_t)std::floor(x0 / kD) - 1, i1 = (int64_t)std::floor(x1 / kD) + 1, j0 = (int64_t)std::floor(z0 / kD) - 1, j1 = (int64_t)std::floor(z1 / kD) + 1;
    for (int64_t i = i0; i <= i1; i++)
        for (int64_t j = j0; j <= j1; j++) {
            if (areaOf(G, i, j) < DC.gully || down2Of(G, i, j) < 0) continue;
            std::vector<DrainSeg> own;
            nodeSegs(G, i, j, 0, 0, own, false);
            bool any = false;
            for (const DrainSeg& e : own) any |= e.fall != 0;
            if (!any || (own[0].cls == 1 && creekPerched(G, i, j))) continue;
            for (const DrainSeg& e : own)
                if (e.fall && e.ax >= x0 && e.ax < x1 && e.az >= z0 && e.az < z1) out.push_back({e.ax, e.az, e.la, e.lb, 2.0 * e.hw, e.dx, e.dz, e.len});
        }
    return out;
}
}  // namespace proc
