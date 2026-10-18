#include "occlusion.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void TerrainOcclusion::build(const std::vector<float>& heights, int n, double spacing, double origin) {
    raw_ = heights, n_ = n, sp_ = spacing, origin_ = origin;
    dirty_ = true;
    ready_ = false;
    refresh(true);
}

void TerrainOcclusion::patch(int i, int j, float h) {
    if (i < 0 || j < 0 || i >= n_ || j >= n_) return;
    raw_[(size_t)i * n_ + j] = h;
    dirty_ = true;
    sinceEdit_ = 0;
}

void TerrainOcclusion::refresh(bool force) {
    if (!dirty_ || n_ < 4) return;
    if (!force && ++sinceEdit_ < 20) return;
    // Raw cell minimum (closed ranges, so neighbors share the border vertices), then a 5 by 5 cell dilation.
    for (int L = 1; L < kLevels; L++) {
        const int c = ((n_ - 1) >> L) + 1;  // cells covering vertices 0 .. n-1
        std::vector<float> m((size_t)c * c, 1e30f);
        const int w = 1 << L;
        for (int ci = 0; ci < c; ci++)
            for (int cj = 0; cj < c; cj++) {
                float lo = 1e30f;
                for (int i = ci * w; i <= std::min(n_ - 1, (ci + 1) * w); i++)
                    for (int j = cj * w; j <= std::min(n_ - 1, (cj + 1) * w); j++) lo = std::min(lo, raw_[(size_t)i * n_ + j]);
                m[(size_t)ci * c + cj] = lo;
            }
        std::vector<float> t(m.size());
        for (int ci = 0; ci < c; ci++)
            for (int cj = 0; cj < c; cj++) {
                float lo = 1e30f;
                for (int k = std::max(0, cj - 2); k <= std::min(c - 1, cj + 2); k++) lo = std::min(lo, m[(size_t)ci * c + k]);
                t[(size_t)ci * c + cj] = lo;
            }
        lvl_[L].assign(m.size(), 0);
        for (int ci = 0; ci < c; ci++)
            for (int cj = 0; cj < c; cj++) {
                float lo = 1e30f;
                for (int k = std::max(0, ci - 2); k <= std::min(c - 1, ci + 2); k++) lo = std::min(lo, t[(size_t)k * c + cj]);
                // (a window off the grid takes in the backdrop, which isn't here: no bound)
                if (ci < 2 || cj < 2 || ci > c - 3 || cj > c - 3) lo = -1e30f;
                lvl_[L][(size_t)ci * c + cj] = lo;
            }
        cells_[L] = c;
        if (c <= 5) {
            for (int k = L + 1; k < kLevels; k++) lvl_[k].clear(), cells_[k] = 0;
            break;
        }
    }
    dirty_ = false;
    ready_ = true;
}

void TerrainOcclusion::setEye(const V3& eye) {
    eye_ = eye;
    asked = hidden = 0;
    eyeOk_ = false;
    if (!ready_) return;
    // Under (or in) the ground, or off the map: sight lines start inside the terrain; nothing is hidden.
    const double u = (eye.x - origin_) / sp_, v = (eye.z - origin_) / sp_;
    if (u < 0 || v < 0 || u > n_ - 1 || v > n_ - 1) { eyeOk_ = true; return; }  // (off the map: marching stops at the edge)
    const int i = std::min(n_ - 2, (int)u), j = std::min(n_ - 2, (int)v);
    float top = -1e30f;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) top = std::max(top, raw_[(size_t)(i + a) * n_ + j + b]);
    eyeOk_ = eye.y > top + 0.5;
}

float TerrainOcclusion::lowerBound(int level, double u, double v) const {
    const int c = cells_[level];
    const int ci = (int)std::floor(u / (1 << level)), cj = (int)std::floor(v / (1 << level));
    if (ci < 0 || cj < 0 || ci >= c || cj >= c) return -1e30f;
    return lvl_[level][(size_t)ci * c + cj];
}

bool TerrainOcclusion::occluded(const RenderBounds& box) const {
    if (!enabled()) return false;
    ++asked;
    const double ex = eye_.x, ey = eye_.y, ez = eye_.z;
    // Eye in (or within a cell of) the footprint: no.
    const double pad = 2 * sp_;
    if (ex > box.lo.x - pad && ex < box.hi.x + pad && ez > box.lo.z - pad && ez < box.hi.z + pad) return false;
    const double dmin = box.distance(eye_);
    const double cx[4] = {box.lo.x, box.lo.x, box.hi.x, box.hi.x}, cz[4] = {box.lo.z, box.hi.z, box.lo.z, box.hi.z};
    double dmax = 0;
    const double ref = std::atan2(0.5 * (box.lo.z + box.hi.z) - ez, 0.5 * (box.lo.x + box.hi.x) - ex);
    double a0 = 1e9, a1 = -1e9;
    for (int k = 0; k < 4; k++) {
        const double dx = cx[k] - ex, dz = cz[k] - ez;
        dmax = std::max(dmax, std::hypot(dx, dz));
        double da = std::atan2(dz, dx) - ref;
        while (da > M_PI) da -= 2 * M_PI;
        while (da < -M_PI) da += 2 * M_PI;
        a0 = std::min(a0, da), a1 = std::max(a1, da);
    }
    if (a1 - a0 > 3.0) return false;
    // Every point of the box has slope (y - ey) / d <= this bound.
    const double rise = box.hi.y - ey;
    const double slope = rise >= 0 ? rise / std::max(dmin, 1e-3) : rise / dmax;
    const double padA = 0.003;
    a0 -= padA, a1 += padA;
    const double extent = a1 - a0;
    const int rays = std::clamp((int)std::ceil(extent / kRayStep) + 1, 2, 64);
    const double dth = extent / (rays - 1);
    const double gridEnd = (n_ - 1) * sp_;
    // Middle first (the likeliest to fail alone), then the ends, then the rest.
    auto test = [&](int r) {
        const double phi = ref + a0 + dth * r;
        const double dirx = std::cos(phi), dirz = std::sin(phi);
        // Clip the march to the map: past it, no terrain data.
        double t = 2.0 * sp_;
        while (true) {
            const double tl = std::max(t * dth, 0.0);  // lateral gap between rays here
            const int lodStep = 1 << terrainLodForDistance(t);
            int level = kMinLevel;
            while (level < kLevels - 1 && (double)(1 << level) * sp_ < tl) level++;
            while (level < kLevels - 1 && (1 << level) < lodStep) level++;
            const double cell = (double)(1 << level) * sp_;
            if (t + 1.5 * cell > dmin) return false;  // reached the box without a hill
            if (cells_[level] == 0) return false;
            const double u = (ex + dirx * t - origin_) / sp_, v = (ez + dirz * t - origin_) / sp_;
            if (u < 0 || v < 0 || u > gridEnd / sp_ || v > gridEnd / sp_) { t += cell; continue; }
            const double h = lowerBound(level, u, v);
            const double reach = 1.5 * cell;
            const double need = ey + std::max(slope * (t - reach), slope * (t + reach)) + 0.3 + 0.002 * t;
            if (h > need) return true;
            t += cell;
        }
    };
    if (!test(rays / 2)) return false;
    if (!test(0) || !test(rays - 1)) return false;
    for (int r = 1; r < rays - 1; r++)
        if (r != rays / 2 && !test(r)) return false;
    ++hidden;
    return true;
}
