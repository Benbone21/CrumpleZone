#pragma once
#include "mathx.h"
#include <algorithm>
#include <vector>

struct RenderBounds {
    V3 lo = V3(1e30, 1e30, 1e30), hi = V3(-1e30, -1e30, -1e30);
    void add(const V3& p) {
        lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    bool visible(const M4& vp) const {
        // OpenGL homogeneous clip planes. A positive support vertex outside one
        // plane rejects the entire box, including boxes behind the eye.
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                const double a = vp.m[3] + sign * vp.m[axis];
                const double b = vp.m[7] + sign * vp.m[4 + axis];
                const double c = vp.m[11] + sign * vp.m[8 + axis];
                const double d = vp.m[15] + sign * vp.m[12 + axis];
                if (a * (a >= 0 ? hi.x : lo.x) + b * (b >= 0 ? hi.y : lo.y) +
                    c * (c >= 0 ? hi.z : lo.z) + d < -0.001) return false;
            }
        return true;
    }
    double distance(const V3& p) const {
        const double x = std::max({lo.x - p.x, 0.0, p.x - hi.x});
        const double z = std::max({lo.z - p.z, 0.0, p.z - hi.z});
        return std::sqrt(x * x + z * z);
    }
};

// Interior quads use the original diagonal. Coarse edge quads fan through an
// existing center vertex and retain every fine perimeter vertex, avoiding cracks
// even when neighboring chunks select different LODs. No skirts hide terrain.
inline void appendTerrainLod(std::vector<unsigned>& out, int n, int x0, int z0, int x1, int z1, int step) {
    auto index = [=](int x, int z) { return (unsigned)(x * n + z); };
    for (int x = x0; x < x1; x += step)
        for (int z = z0; z < z1; z += step) {
            const int xe = std::min(x + step, x1), ze = std::min(z + step, z1);
            if (step == 1 || (x > x0 && z > z0 && xe < x1 && ze < z1) || xe - x < 2 || ze - z < 2) {
                out.insert(out.end(), {index(x,z), index(x,ze), index(xe,ze), index(x,z), index(xe,ze), index(xe,z)});
                continue;
            }
            const unsigned center = index((x + xe) / 2, (z + ze) / 2);
            auto edge = [&](int ax, int az, int bx, int bz, bool fine) {
                const int count = fine ? std::max(std::abs(bx - ax), std::abs(bz - az)) : 1;
                for (int k = 0; k < count; ++k)
                    out.insert(out.end(), {center, index(ax + (bx-ax)*k/count, az + (bz-az)*k/count),
                        index(ax + (bx-ax)*(k+1)/count, az + (bz-az)*(k+1)/count)});
            };
            edge(x,z,x,ze,x == x0);
            edge(x,ze,xe,ze,ze == z1);
            edge(xe,ze,xe,z,xe == x1);
            edge(xe,z,x,z,z == z0);
        }
}
