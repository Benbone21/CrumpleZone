// The forest as instances: every distinct tree model (its kind's design, seed and snow) is built
// once, and each tree is one copy of it (where it stands, its turn, size and tint). Far trees draw
// the models' plainer far copies. No OpenGL here: treedraw.cpp draws them.
#pragma once
#include <cstdint>
#include <vector>

#include "modelkit.h"
#include "occlusion.h"
#include "world.h"

namespace forest {

const char* modelId(int kind);  // tree_pine, tree_broadleaf, tree_snag
double modelHeight(int kind);   // what a model is built to (10 m; a snag 8): a tree's scale is its height over this

// The design (the model's variant) a tree of `kind` gets: the one picked in the model lab, with some of
// the kind's others mixed in when that's the first (spruce among the tall firs, aspen groves among the
// oaks, broken and burnt snags), by its seed and where it stands.
int designFor(int kind, uint32_t seed, double x, double z, double height);
// How thick the drawn trunk is 1 m off the ground on a tree `height` tall: what its Pole is.
double trunkRadius(int kind, int design, double height);

// Far copies: 0 the full model, 1 and 2 the models' own plainer ones (the "lod" param: fewer sides, small
// bits left out), 3 the plainest of those boiled down (simplify) for trees far off.
constexpr int kLods = 4;
double lodReach(int lod);  // a tree nearer than this draws at `lod` (else a coarser one), at detail 1

struct Look {
    int kind = 0, design = 0, seed = 1;
    bool snow = false;
};
struct Shape {
    Look look;
    mk::Mesh lod[kLods];  // an empty one: the next finer is drawn instead
    float reach = 1, top = 1;  // round its base (model units, before the instance's scale), and its height
};
struct Inst {
    float x, y, z, yaw;  // its base and turn
    float scale, tint, warm;  // size (model units to m), brightness, and the foliage's hue (-1 bluer .. 1 yellower)
    float stretch = 1;   // up, over the scale (a bush a little taller or squatter)
    float reach = 0;     // drawn out to this far (m, at detail 1), fading out over its last 15%; 0: as far as it can be seen
    int shape, tree;     // Set::shapes, World::trees (bushes: World::bushes)
};
struct Set {
    std::vector<Shape> shapes;
    std::vector<Inst> inst;  // one per tree drawn
    float lodAt[kLods] = {1e9f, 1e9f, 1e9f, 1e9f};  // nearer than lodAt[l]: LOD l (at detail 1)
    size_t vertices() const;  // in all the shapes' meshes (every LOD)
    size_t bakedVertices() const;  // what baking every tree into one mesh at full detail would take
};
// `skipPines`, `skipSnags`: drawn by the scanned props instead.
void build(const World& w, bool skipPines, bool skipSnags, Set& out);
// The bushes and small plants (World::bushes, the "bush" model) as instances: a model for each kind, seed
// and dry or not (they're a few dozen triangles: no far copies). Each is drawn out to a reach by its size.
void buildBushes(const World& w, Set& out);
// A plainer copy of a mesh: its corners within `cell` of each other (by material) drawn together into one,
// the triangles left with no size dropped.
mk::Mesh simplify(const mk::Mesh& m, double cell);

// Which instances a view draws, and at what LOD: the 64 m cells in view, then each instance's own sphere
// against the view's planes, its reach and LOD by its distance from the eye. No OpenGL (treedraw.cpp draws).
struct View {
    M4 vp;              // what it sees
    V3 eye;             // where distances are from
    double detail = 1;  // LOD distances and reaches scaled by this (the scenery quality)
    int lodBias = 0;    // coarser than the distance says (a shadow cascade)
    bool shadow = false;  // shadows: no fades (an instance fading out casts while it's over half there)
    double reach = 1;   // the instances' reaches scaled by this too (shadows: only the near bushes cast)
    const TerrainOcclusion* hidden = nullptr;  // main pass only: cells behind the ground are skipped (occlusion.h)
    double maxDist = 0;  // nothing farther (m; faded out over its last 15%): a big map's distant forest is left to the fog (0: no limit)
};
struct Pick {
    int inst;
    float fade;  // 1 solid .. 0 gone (dithered away)
};
class Picker {
public:
    void build(const Set& s);
    // Fills buckets[shape * kLods + lod] (cleared first) nearest cells first.
    void pick(const Set& s, const View& v, std::vector<std::vector<Pick>>& buckets);
    struct Cell {
        V3 lo, hi;  // round all its instances
        std::vector<int> inst;
    };
    const std::vector<Cell>& cells() const { return cells_; }
    // An instance's bounding sphere (world): its middle and radius.
    static void sphere(const Set& s, const Inst& in, V3& c, double& r);

private:
    std::vector<Cell> cells_;
    // The cells 8 x 8 at a go (512 m), each block's box round its cells': a big map's cells are passed over a block at a time.
    struct Block {
        V3 lo = V3(1e30, 1e30, 1e30), hi = V3(-1e30, -1e30, -1e30);
        std::vector<int> cells;
    };
    std::vector<Block> blocks_;
    std::vector<std::pair<double, int>> order_;
};

// A fallen log's mesh, in the world (its Log is the capsule a to b): rough ridged bark, darker underneath,
// mossy on top in the woods and by water (snowy on snow), each end sawn (rings) or snapped (splinters),
// a stub or two of branch. All of it inside the capsule, but the stubs.
void logMesh(const World& w, const Log& g, uint32_t seed, mk::Mesh& out);

}  // namespace forest
