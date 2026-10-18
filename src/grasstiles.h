#pragma once
// The grass's tiles: one ring's grid of side x side cells is drawn in 8 x 8 tiles (kGrassTiles), each a run of cells the shader
// places from the camera's cell (kGrassVS: floor(uCam / cell) + tile + i - side / 2). The CPU culls whole tiles on these boxes.
#include <algorithm>
#include <cmath>

constexpr int kGrassTiles = 8;
constexpr double kGrassReach = 3.5;  // m (the shader's cull keeps the same): a tuft sits anywhere in its cell, its blade 0.7 cell off that, standing under 2 m and leaning a metre

// The ring's grid origin (its cell 0), in metres, on one axis: where the shader puts it for a camera at `eye`.
inline double grassGridOrigin(double eye, double cell, int side) { return (std::floor(eye / cell) - side / 2) * cell; }

// Cells per tile.
inline int grassTileSpan(int side) { return (side + kGrassTiles - 1) / kGrassTiles; }

// Tile `t`'s extent along one axis, from `origin`, widened by the reach of what grows in it.
inline void grassTileRange(double origin, double cell, int side, int t, double& lo, double& hi) {
    const int ts = grassTileSpan(side);
    const double m = std::max(cell, kGrassReach);
    lo = origin + t * ts * cell - m, hi = origin + std::min(side, t * ts + ts) * cell + m;
}
