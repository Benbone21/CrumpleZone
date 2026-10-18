#pragma once
// Terrain-horizon occlusion: is a box hidden behind the ground as seen from the eye? The ground is a
// heightfield, so a sight line from the eye that dips under it before reaching the box is blocked. The test
// marches a fan of rays across the box's footprint and looks for a hill on each. A hill counts only through
// a lower bound of the DRAWN mesh (min over a few cells around the ray, a mip pyramid), so it is conservative
// by construction: a box is called hidden only when every sight line to it is under the drawn surface.
// Never given the shadow passes' views: ground that is out of sight still casts shadows.
#include "visibility.h"
#include <vector>

// The terrain chunk LOD (mesh steps) for a distance in meters: the draw and the horizon test both use it, so
// the test knows how coarse the surface it leans on can be.
inline int terrainLodForDistance(double d) { return d < 100 ? 0 : d < 220 ? 1 : d < 450 ? 2 : 3; }

class TerrainOcclusion {
public:
    // Heights of the drawn mesh: n by n, index i * n + j, vertex (i, j) at (origin + i * spacing, origin + j * spacing).
    void build(const std::vector<float>& heights, int n, double spacing, double origin);
    // Editor: a mesh vertex changed. The pyramid is stale (occluded() says false) until `refresh` rebuilds it.
    void patch(int i, int j, float h);
    void refresh(bool force = false);  // call once a frame; rebuilds a stale pyramid after it settles
    void setEye(const V3& eye);        // the camera, once a frame, before any occluded()
    void enable(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_ && ready_ && !dirty_ && eyeOk_; }
    // True only if no point of the box can be seen from the eye past the terrain.
    bool occluded(const RenderBounds& box) const;
    // Per-frame counters (for F3 and the bench): boxes asked, boxes hidden.
    mutable unsigned asked = 0, hidden = 0;
private:
    static constexpr int kMinLevel = 1, kLevels = 8;
    static constexpr double kRayStep = 0.025;  // radians between rays
    int n_ = 0;
    double sp_ = 1, origin_ = 0;
    std::vector<float> raw_;
    // level L (cells of 2^L mesh steps, L >= 1): lower bound of the mesh within two cells of the cell.
    std::vector<float> lvl_[kLevels];
    int cells_[kLevels] = {};
    bool ready_ = false, dirty_ = false, enabled_ = true, eyeOk_ = false;
    int sinceEdit_ = 0;
    V3 eye_;
    float lowerBound(int level, double u, double v) const;
};
