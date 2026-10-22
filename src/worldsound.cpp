#include "worldsound.h"
#include "biome.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double kShopRange = 55, kShopLoud = 0.9, kMergeShops = 6;

uint64_t worldSignature(const World& w) {
    uint64_t h = (uint64_t)w.map * 1000003u;
    for (size_t v : {w.stations.size(), w.fixtures.size(), w.models.size(), w.waters.size(), w.waterY.size()}) h = h * 1315423911u + v;
    return h;
}
float horizontalPan(const V3& from, const V3& to, const V3& right, double d) {
    const double dx = to.x - from.x, dz = to.z - from.z, h = std::hypot(dx, dz);
    if (h < 1e-6) return 0;
    const double s = (dx * right.x + dz * right.z) / (h * std::max(1e-9, std::hypot(right.x, right.z)));
    return (float)std::clamp(s * std::min(1.0, d / 8), -1.0, 1.0);  // (close in, it's all around you)
}
}  // namespace

float WorldSounds::falloff(double d, double range) {
    if (!(range > 0) || !std::isfinite(d)) return 0;
    d = std::max(d, 3.0);
    if (d >= range) return 0;
    const double u = 1 - d / range;
    return (float)(u * u);
}

void WorldSounds::prepare(const World& world) {
    const uint64_t sig = worldSignature(world);
    if (world_ == &world && sig == signature_) return;
    build(world);
    world_ = &world;
    signature_ = sig;
}

void WorldSounds::build(const World& w) {
    shops_.clear();
    bodies_.clear();
    auto addShop = [&](const V3& p) {
        for (const V3& s : shops_) if (std::hypot(s.x - p.x, s.z - p.z) < kMergeShops) return;
        shops_.push_back(p);
    };
    for (const Station& s : w.stations) addShop(s.p);
    for (const Prop& f : w.fixtures) if (f.kind == PROP_GARAGE) addShop(f.a);
    for (const Placed& m : w.models) if (m.id == "repair_shed") addShop(m.p);

    if (w.waterY.empty()) {  // lakes
        for (const Water& wt : w.waters) {
            Body b;
            b.disc = true;
            b.c = V3(wt.x, wt.y, wt.z);
            b.r = wt.r;
            b.loop = WL_STREAM;
            b.loud = 0.12f + 0.1f * (float)std::min(1.0, wt.r / 60);  // a still lake barely laps
            b.range = 30;
            b.cells = M_PI * wt.r * wt.r;
            bodies_.push_back(std::move(b));
        }
        return;
    }
    // Water on the height grid: connected runs of wet cells, flowing (>= 0.5 m/s) apart from slack, each one a source.
    const int n = w.gridSize();
    if (n < 4 || w.waterY.size() < (size_t)n * n) return;
    const bool flows = w.flowX.size() >= (size_t)n * n && w.flowZ.size() >= (size_t)n * n;
    std::vector<uint8_t> label((size_t)n * n, 0);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            const size_t k = (size_t)i * n + j;
            if (w.waterY[k] < -1e8f) continue;
            const double x = -w.halfSize() + i, z = -w.halfSize() + j;
            if (w.waterY[k] <= w.height(x, z) + 0.03) continue;  // dry (the shader cuts the sheet at the shore)
            const double sp = flows ? std::hypot(w.flowX[k], w.flowZ[k]) : 0;
            label[k] = sp >= 0.5 ? 1 : 2;
        }
    const int bn = n / 3 + 1;
    std::vector<int> block((size_t)bn * bn, -1);
    std::vector<int> stack;
    int id = 0;
    for (int s = 0; s < n * n; s++) {
        if (!label[s] || label[s] > 2) continue;
        const uint8_t lab = label[s];
        Body b;
        double speed = 0;
        stack.assign(1, s);
        label[s] = 3;
        while (!stack.empty()) {
            const int k = stack.back();
            stack.pop_back();
            const int i = k / n, j = k % n;
            b.cells++;
            speed += flows ? std::hypot(w.flowX[k], w.flowZ[k]) : 0;
            int& bk = block[(size_t)(i / 3) * bn + j / 3];
            if (bk != id) {
                bk = id;
                b.pts.push_back(V3(-w.halfSize() + i, w.waterY[k], -w.halfSize() + j));
            }
            const int nb[4] = {i > 0 ? k - n : -1, i + 1 < n ? k + n : -1, j > 0 ? k - 1 : -1, j + 1 < n ? k + 1 : -1};
            for (int q : nb)
                if (q >= 0 && label[q] == lab) label[q] = 3, stack.push_back(q);
        }
        id++;
        if (b.cells < 30) continue;  // (a puddle)
        b.speed = speed / b.cells;
        if (lab == 1 && b.cells >= 1500) {  // a river
            b.loop = WL_RIVER;
            b.loud = (float)std::clamp(0.4 + 0.6 * b.cells / 12000, 0.4, 1.0);
            b.range = 50 + 70 * b.loud;
        } else if (lab == 1) {  // a brook
            b.loop = WL_STREAM;
            b.loud = 0.35f + 0.4f * (float)std::min(1.0, b.cells / 1500);
            b.range = 40;
        } else {  // slack water: a swamp or pond
            b.loop = WL_STREAM;
            b.loud = 0.12f + 0.1f * (float)std::min(1.0, b.cells / 20000);
            b.range = 30;
        }
        bodies_.push_back(std::move(b));
    }
}

void WorldSounds::update(const V3& at, const V3& right, Voice out[kVoices - 1]) const {
    for (int i = 0; i < kVoices - 1; i++) out[i] = Voice{};
    if (!std::isfinite(at.x) || !std::isfinite(at.z)) return;
    // Shops: the nearest ones.
    struct Cand { double d; float gain; V3 p; int loop; };
    Cand shop[kShopVoices];
    int ns = 0;
    {
        std::vector<Cand> c;
        for (const V3& s : shops_) {
            const double d = std::hypot(s.x - at.x, s.z - at.z);
            if (d < kShopRange) c.push_back({d, (float)(kShopLoud * falloff(d, kShopRange)), s, WL_SHOP});
        }
        std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        for (const Cand& x : c) if (ns < kShopVoices) shop[ns++] = x;
    }
    for (int i = 0; i < ns; i++) out[i] = {WL_SHOP, shop[i].gain, horizontalPan(at, shop[i].p, right, shop[i].d), shop[i].d};
    // Water: the two bodies that sound loudest from here, each from its nearest point.
    std::vector<Cand> c;
    for (const Body& b : bodies_) {
        double best = 1e18;
        V3 bp;
        if (b.disc) {
            const double dc = std::hypot(at.x - b.c.x, at.z - b.c.z);
            best = std::max(0.0, dc - b.r);
            bp = best <= 0 ? at : V3(b.c.x + (at.x - b.c.x) * b.r / dc, b.c.y, b.c.z + (at.z - b.c.z) * b.r / dc);
        } else {
            for (const V3& p : b.pts) {
                const double dx = p.x - at.x, dz = p.z - at.z, d2 = dx * dx + dz * dz;
                if (d2 < best) best = d2, bp = p;
            }
            best = std::sqrt(best);
        }
        if (best >= b.range) continue;
        c.push_back({best, b.loud * falloff(best, b.range), bp, b.loop});
    }
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.gain > b.gain; });
    for (int i = 0; i < kWaterVoices && i < (int)c.size(); i++)
        out[kShopVoices + i] = {c[i].loop, c[i].gain, horizontalPan(at, c[i].p, right, c[i].d), c[i].d};
}

WorldSounds::Voice WorldSounds::forest(int biome, float hours) {
    Voice v;
    if (biome != BIOME_PINE || !std::isfinite(hours)) return v;
    const float day = std::clamp(std::min((hours - 6.0f) / 2, (20.0f - hours) / 2), 0.0f, 1.0f);  // (day only)
    v.loop = WL_FOREST;
    v.gain = 0.5f * day;
    return v;
}
