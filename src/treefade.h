// See-through trees: how much of a tree the camera should hide, so a chase camera in the woods still sees
// its car. A tree that's close to the camera, or stands across the line from the camera to the car,
// dissolves (the shaders drop pixels by an ordered dither: no sorting, no blending). 0 shows it whole,
// 1 hides it. Shared by the code-made trees (render.cpp) and the scanned ones (propdraw.cpp).
#pragma once
#include <algorithm>
#include <cmath>

#include "mathx.h"

struct SeeThrough {
    V3 eye, focus;          // the camera, and what it must keep in sight (the car)
    bool hasFocus = false;  // (chase and orbit views: the line eye -> focus is kept clear)
    float nearScale = 0;    // scales how far from the camera trees dissolve: 1 fully gone inside 2.5 m, back by 7 m; 0 off
};

namespace treefade {
constexpr double kGoneNear = 2.5, kBackNear = 7.0;  // metres from the camera to the tree's crown: fully gone, fully back
constexpr double kLineMargin = 1.2;                 // how far clear of the sight line a crown must be to stay whole
constexpr float kLineMax = 0.96f;                   // trees across the line keep a few pixels: they stay a ghost, not a pop

// A tree standing at `base`, `h` tall, its crown `R` wide (a cone: widest low down).
inline float target(const SeeThrough& s, const V3& base, double h, double R) {
    if (s.nearScale <= 0 && !s.hasFocus) return 0;
    float f = 0;
    if (s.nearScale > 0) {
        // The camera's distance to the tree's trunk line, less most of the crown's reach at that height.
        const double y = clampd(s.eye.y, base.y, base.y + h), yRel = (y - base.y) / std::max(h, 0.1);
        const double reach = R * (yRel < 0.12 ? 0.1 : clampd((1.05 - yRel) / 0.9, 0.0, 1.0)) * 0.8;
        const double dx = s.eye.x - base.x, dz = s.eye.z - base.z, dy = s.eye.y - y;
        const double d = std::sqrt(dx * dx + dz * dz + dy * dy) - reach;
        f = (float)(1 - smoothstep(kGoneNear * s.nearScale, kBackNear * s.nearScale, d));
    }
    if (s.hasFocus) {
        // Where the sight line passes the trunk (in plan), and the crown's reach at that height.
        const double ax = s.eye.x, az = s.eye.z, bx = s.focus.x - ax, bz = s.focus.z - az;
        const double L2 = bx * bx + bz * bz;
        if (L2 > 4.0) {
            const double t = clampd(((base.x - ax) * bx + (base.z - az) * bz) / L2, 0.0, 1.0);
            const double px = ax + bx * t, pz = az + bz * t, py = s.eye.y + (s.focus.y - s.eye.y) * t;
            const double yRel = (py - base.y) / std::max(h, 0.1);
            if (yRel > -0.05 && yRel < 1.02) {
                const double reach = std::max(0.35, R * (yRel < 0.12 ? 0.1 : clampd((1.05 - yRel) / 0.9, 0.0, 1.0)));
                const double d = std::hypot(px - base.x, pz - base.z) - reach;
                f = std::max(f, kLineMax * (float)(1 - smoothstep(0.0, kLineMargin, d)));
            }
        }
    }
    return f;
}

// Eases a tree's fade toward its target: out quickly, back a little slower.
inline float ease(float now, float target, double dt) {
    const float rate = (float)(target > now ? 9.0 : 4.0) * (float)dt;
    return target > now ? std::min(target, now + rate) : std::max(target, now - rate);
}
}  // namespace treefade
