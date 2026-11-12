// Cliff walls (docs/real-terrain.md, "Cliff walls"): on a real map, where the ground drops more than a few metres, the face of the
// cliff is drawn again over the terrain as a mesh of its own, carved into the rock: beds undercut under harder ones (their lips
// overhang a little), an overhanging cap under the rim, arched alcoves, a rough skin instead of the 1 m grid's facets.
//
// The collider is the land (World::height, the 1 m grid): the carving only ever goes into the rock, never out of it, and only on
// the part of a face no tire reaches (from a couple of metres above its foot up to just below its rim). So nothing drawn stands
// out past what a wheel hits: where the wall is carved, it's behind the land's own surface (the ground shader's wall build draws it
// in front of the terrain's: render.cpp drawWalls).
//
// The surface is a surface-nets mesh of a field (the horizontal distance into the rock, less the carving) on a lattice of 1/2 m
// across by 0.5 m up, over the cells of a 64 m terrain chunk that are steep. Pure: any thread; the same chunk built twice is the
// same mesh, and two chunks' meshes meet along their edge.
#pragma once
#include <functional>
#include <vector>

class World;

namespace cliffwalls {

struct Vert {
    float p[3], n[3], c[4];  // render.h Vtx's layout. c: 0, how far (m) it's carved in, the outward direction (radians from +x
                             // toward +z), the material (SM_TERRAIN)
};

// Where the rock's beds are offset across a fault (the sidecar's strata_fault, on the map's frame): left of the line from a to b
// they sit `shift` m higher, eased over `width`. No fault: shift 0.
struct Beds {
    double ax = 0, az = 0, bx = 1, bz = 0, shift = 0, width = 300;
    // How much the land at (x, z) is the 1 m lidar's (0..1): its faces are drawn as they are; elsewhere (10 m data, whose bicubic
    // faces are ribbed every pixel) a face is first smoothed along itself, only ever inward (null: none is lidar).
    std::function<double(double, double)> lidar;
};

// The carved faces over the terrain chunk of cells [i0, i0 + w) x [j0, j0 + h) (grid point (i, j) at x = -half + i, z = -half + j).
// `material` goes in each vertex's c[3]. Appends to verts and idx (indices into verts as they stand on return).
void build(const World& world, const Beds& beds, int i0, int j0, int w, int h, float material, std::vector<Vert>& verts, std::vector<unsigned>& idx);

// How far (m) the face is carved in at (x, y, z), given the height above the wall's foot, below its rim and the wall's height
// there (any of it 0 where a tire may reach). Exposed for the tests.
double carve(const Beds& beds, double x, double y, double z, double aboveFoot, double belowTop, double wallHeight);

}  // namespace cliffwalls
