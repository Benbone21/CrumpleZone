// The far forest: trees past the procedural world's window, out to the fog, so the far hills are wooded and not bare land.
//
// The window's own trees are drawn as meshes near the eye and as pictures further out (treedraw.h). Past those, out to the far
// backdrop's edge, the trees come from a field: on the backdrop's 32 m grid (built with it, on the worker that makes a window), the
// trees a hectare the generator's tree rule grows there (proc::ProcWorld::treeCover, the chunks' rule at one point, less what the
// trunks' spacing turns away), its shares of conifers and snags, and how stunted. The GPU scatters pictures from it: each level of
// cells (16, 32, 32 m) a fixed lattice in world coordinates, a few tries a cell, each kept as the field and the distance say (the
// same thinning as the window's trees out to 1.2 km, then less the farther, the pictures wider to make up), so the same trees stand
// in the same places as the window slides. The levels and the window's trees hand over in dissolves (the same dither everywhere).
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "forest.h"
#include "gl.h"
#include "treedraw.h"

class TerrainOcclusion;
class World;

extern const char* const kFarForestVS;  // (after kImpostorPlace)

struct FarForestGrid {
    int n = 0;
    double spacing = 32, origin = 0;  // sample (i, j) at window coordinates (origin + i * spacing, origin + j * spacing), kept at j * n + i
                                      // (as the textures hold it: x along a row)
    int64_t ox = 0, oz = 0;           // the window's origin (global m)
    std::vector<uint8_t> cover;       // 4 a sample: trees a hectare (x kPerHaUnit), conifer share, snag share, height kept (all /255)
    std::vector<float> height;        // the ground as the backdrop draws it
    int bn = 0;                       // blocks of kBlock x kBlock samples: their lowest and highest ground (the tiles' boxes)
    std::vector<float> blockLo, blockHi;
    static constexpr double kPerHaUnit = 1.2;
    static constexpr int kBlock = 8;
    // The trees a hectare a chunk ends up with where its rule tries `tried` (the trunks' spacing turns many away; fitted to the
    // chunks' own counts, tests/forest.cpp).
    static double realized(double tried) { return 428.0 * (1.0 - std::exp(-tried / 500.0)); }
    void finish();  // (the blocks, from the heights)
};

class FarForest {
public:
    void upload(const std::shared_ptr<const FarForestGrid>& g);
    bool ready() const { return tex_[0] != 0 && n_ > 0; }
    void clear() { n_ = 0; grid_.reset(); }
    struct Range {
        double from = 1000, fromBand = 150;  // where the window's trees hand over to it (their ImpostorRange::to and toBand)
        double to = 6000;                    // the farthest (m), faded out over its last 10%
    };
    // With `prog` (kImpostorPlace + kFarForestVS, kImpostorFSBody) in use and the atlas `trees` bakes into bound for it.
    unsigned draw(const forest::View& view, const Range& r, GLuint prog, const TreeDraw& trees, const TerrainOcclusion* occl,
                  VegStats::Pass* stats = nullptr);
    int tilesDrawn = 0;
    // The ground shader's canopy colour far off (groundshader.cpp uFF*): the field on `unit`, faded in from `from` to `to` m. Off
    // (uFFFade 0) when there's no field.
    void bindGround(GLuint prog, int unit, double from, double to) const;

private:
    GLuint tex_[2] = {0, 0}, vao_ = 0;
    int n_ = 0;
    std::shared_ptr<const FarForestGrid> grid_;
};
