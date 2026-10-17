// The forest as instances: every distinct tree model (its kind's design, seed and snow) is built
// once, and each tree is one copy of it (where it stands, its turn, size and tint). Far trees draw
// the models' plainer far copies. No OpenGL here: treedraw.cpp draws them.
#pragma once
#include <cstdint>
#include <vector>

#include "modelkit.h"
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

constexpr int kLods = 3;
double lodReach(int lod);  // a tree nearer than this draws at `lod` (else a coarser one)

struct Look {
    int kind = 0, design = 0, seed = 1;
    bool snow = false;
};
struct Shape {
    Look look;
    mk::Mesh lod[kLods];  // the full model, then far copies (the models' "lod" param: fewer sides, small bits left out)
};
struct Inst {
    float x, y, z, yaw;  // its base and turn
    float scale, tint, warm;  // size (model units to m), brightness, and the foliage's hue (-1 bluer .. 1 yellower)
    int shape, tree;     // Set::shapes, World::trees
};
struct Set {
    std::vector<Shape> shapes;
    std::vector<Inst> inst;  // one per tree drawn
    size_t vertices() const;  // in all the shapes' meshes (every LOD)
    size_t bakedVertices() const;  // what baking every tree into one mesh at full detail would take
};
// `skipPines`, `skipSnags`: drawn by the scanned props instead.
void build(const World& w, bool skipPines, bool skipSnags, Set& out);

// A fallen log's mesh, in the world (its Log is the capsule a to b): rough ridged bark, darker underneath,
// mossy on top in the woods and by water (snowy on snow), each end sawn (rings) or snapped (splinters),
// a stub or two of branch. All of it inside the capsule, but the stubs.
void logMesh(const World& w, const Log& g, uint32_t seed, mk::Mesh& out);

}  // namespace forest
