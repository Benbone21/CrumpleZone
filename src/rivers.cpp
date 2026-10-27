#include "rivers.h"
#include "detmath.h"

#include <algorithm>
#include <cmath>

const char* riverClassName(int cls) {
    static const char* names[RIVER_CLASSES] = {"RUN", "RIFFLE", "FORD", "POOL", "DEEP", "CHUTE", "PLUNGE POOL", "FALLS"};
    return cls >= 0 && cls < RIVER_CLASSES ? names[cls] : "?";
}

DepthBand riverClassDepth(int cls) {
    switch (cls) {
        case RIVER_RIFFLE: return {0.2, 0.4};
        case RIVER_FORD: return {0.4, 0.6};
        case RIVER_POOL: return {0.9, 1.5};
        case RIVER_DEEP: return {1.5, 3.0};
        case RIVER_CHUTE: return {0.4, 0.8};
        case RIVER_PLUNGE: return {2.0, 3.0};
        case RIVER_FALLS: return {0, 0};
        default: return {0.6, 0.9};
    }
}

int riverClassForDepth(double d) {
    return d < 0.4 ? RIVER_RIFFLE : d < 0.6 ? RIVER_FORD : d < 0.9 ? RIVER_RUN : d <= 1.5 ? RIVER_POOL : RIVER_DEEP;
}

double Falls::loudness() const { return std::clamp(0.35 + std::sqrt(std::max(0.0, height * width)) / 26.0, 0.35, 1.0); }
double Falls::hearRange() const { return 100 + 16 * std::sqrt(std::max(0.0, height * width)); }  // (Bear Falls, 18 x 16 m: ~370 m)

namespace {
struct Feature {
    double x;
    int cls;
};
// Elk River's stretches out in the new country, upstream (east) and downstream (west) of Elk Valley.
const Feature kFeatures[] = {
    {800, RIVER_FORD},    {1060, RIVER_RIFFLE},  {1300, RIVER_POOL},   {1560, RIVER_FORD},    {1720, RIVER_RIFFLE},
    {2250, RIVER_POOL},   {2420, RIVER_RIFFLE},  {2800, RIVER_FORD},   {2960, RIVER_RIFFLE},
    {-800, RIVER_FORD},   {-1100, RIVER_POOL},   {-1400, RIVER_RIFFLE}, {-1750, RIVER_FORD},  {-2100, RIVER_POOL},
    {-2450, RIVER_RIFFLE}, {-2750, RIVER_POOL},
};
RiverStretch stretchOf(int cls) {
    RiverStretch s;
    s.cls = cls;
    switch (cls) {
        case RIVER_FORD: s.depth = 0.5, s.widthScale = 1.45, s.speed = 0.8, s.bankSlope = 0.16; break;
        case RIVER_RIFFLE: s.depth = 0.3, s.widthScale = 1.25, s.speed = 1.4, s.bankSlope = 0.3; break;
        case RIVER_POOL: s.depth = 1.25, s.widthScale = 1.3, s.speed = 0.5, s.bankSlope = 0.4; break;
        case RIVER_CHUTE: s.depth = 0.6, s.widthScale = 0.9, s.speed = 2.6, s.bankSlope = 0.9, s.bed = BED_ROCK; break;
        default: break;
    }
    return s;
}
double halfLength(int cls) { return cls == RIVER_FORD ? 18 : cls == RIVER_RIFFLE ? 14 : cls == RIVER_POOL ? 26 : 0; }
void blend(RiverStretch& s, const RiverStretch& to, double w) {
    s.depth += (to.depth - s.depth) * w, s.widthScale += (to.widthScale - s.widthScale) * w;
    s.speed += (to.speed - s.speed) * w, s.bankSlope += (to.bankSlope - s.bankSlope) * w;
    if (w > 0.5) s.cls = to.cls, s.bed = to.bed;
}
}  // namespace

const std::vector<FallsSite>& elkFalls() {
    // BEAR FALLS: an 18 m curtain in Bear Creek Canyon; UPPER FALLS: a 6 m cascade further up it.
    static const std::vector<FallsSite> f = {{"BEAR FALLS", 1900, 18, 3.2, 0, 30, 2.7}, {"UPPER FALLS", 2600, 6, 2.6, 2, 20, 2.2}};
    return f;
}

double elkRiverZ(double x) { return 14 + 36 * dm::sin(x * 0.0068 + 0.9) + 12 * dm::sin(x * 0.019 + 2.3); }

// Below each falls the river runs in a gorge it has cut back into the canyon floor: steep walls kFallsFace wide either side, the
// gorge widening downstream and its walls sinking away into the canyon over a few hundred metres.
double gorgeHalf(const FallsSite& f, double x) { return 15 + 0.25 * std::max(0.0, f.x - x); }
double gorgeTaper(const FallsSite& f, double x) { return smoothstep(f.x - 320, f.x - 140, x); }
double gorgeWall(const FallsSite& f, double x, double z) {  // 1 up on the floor beside the gorge, 0 down in it
    if (x >= f.x || x < f.x - 320) return 0;
    const double dz = std::fabs(z - elkRiverZ(x)) + 2.0 * (valueNoise(x * 0.07 + 3.1, z * 0.07 - f.x * 0.01) - 0.5), w = gorgeHalf(f, x);
    return smoothstep(w, w + kFallsFace, dz) * gorgeTaper(f, x);
}

double elkLipX(const FallsSite& f, double z) {
    // Straight across the channel, bending back a little (the falls has cut a horseshoe), and ragged.
    const double dz = std::fabs(z - elkRiverZ(f.x)), off = std::max(0.0, dz - 6);
    return f.x - std::min(6.0, 0.03 * off * off) - 1.2 * (valueNoise(z * 0.15 + f.x * 0.01, 7.7) - 0.5) * smoothstep(6, 12, dz);
}

double elkFallsRise(double x, double z) {
    double r = 0;
    for (const FallsSite& f : elkFalls()) {
        if (x < f.x - 330) continue;
        const double lx = elkLipX(f, z);
        r += f.drop * std::max(smoothstep(lx - kFallsFace, lx, x), gorgeWall(f, x, z));
    }
    return r;
}

int elkFallsFace(double x, double z) {
    for (size_t i = 0; i < elkFalls().size(); i++) {
        const FallsSite& f = elkFalls()[i];
        const double lx = elkLipX(f, z);
        if (x >= lx - kFallsFace && x < lx && gorgeWall(f, x, z) < 0.5) return (int)i;
    }
    return -1;
}

int elkFallsZone(double x, double z) {
    int n = 0;
    for (const FallsSite& f : elkFalls()) n += x >= elkLipX(f, z) - kFallsFace || gorgeWall(f, x, z) >= 0.5;
    return n;
}

const std::vector<double>& elkFords() {
    static const std::vector<double> xs = [] {
        std::vector<double> v;
        for (const Feature& f : kFeatures)
            if (f.cls == RIVER_FORD) v.push_back(f.x);
        return v;
    }();
    return xs;
}

RiverStretch elkRiverAt(double x) {
    RiverStretch s;
    const double ax = std::fabs(x);
    s.depth = 0.8 + 0.1 * smoothstep(700, 600, ax);  // (into the old channel's 0.9 m at Elk Valley's edge)
    for (const Feature& f : kFeatures) {
        const double hl = halfLength(f.cls), d = std::fabs(x - f.x);
        if (d < hl + 14) blend(s, stretchOf(f.cls), smoothstep(hl + 14, hl, d));
    }
    for (const FallsSite& f : elkFalls()) {
        if (x >= f.x && x < f.x + 100) blend(s, stretchOf(RIVER_CHUTE), smoothstep(f.x + 100, f.x + 70, x));  // the run-in: fast over rock
        const double p0 = f.x - kFallsFace;  // the cliff's foot
        if (x < p0 && x > p0 - f.poolLen - 16) {
            RiverStretch pool;
            pool.cls = RIVER_PLUNGE, pool.bed = BED_ROCK;
            pool.depth = f.poolDepth, pool.widthScale = 1.8, pool.speed = 1.1, pool.bankSlope = 0.8;
            blend(s, pool, smoothstep(p0 - f.poolLen - 16, p0 - f.poolLen, x));
            s.widthScale = std::max(s.widthScale, 1.0 + 0.8 * smoothstep(p0 - f.poolLen - 16, p0 - 4, x));
        }
        if (x >= p0 && x < f.x) {
            s.cls = RIVER_FALLS, s.bed = BED_ROCK;
            s.depth = 0, s.speed = 0, s.bankSlope = 0.9;
        }
    }
    return s;
}

bool elkRiverSpot(const std::string& n, double& x) {
    if (n == "bearfalls") return x = elkFalls()[0].x, true;
    if (n == "upperfalls") return x = elkFalls()[1].x, true;
    if (n == "eastford") return x = 800, true;
    if (n == "eastpool") return x = 1300, true;
    if (n == "eastriffle") return x = 1060, true;
    if (n == "westford") return x = -800, true;
    if (n == "westpool") return x = -1100, true;
    return false;
}
