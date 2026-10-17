// Mesh simplification: the same picture from fewer triangles. A triangle soup (every three vertices a triangle, as
// modelkit.h and the renderer's Vtx lists are) goes in, a soup comes out, never with more triangles:
//   - degenerate triangles (two corners in one place, or no width) and repeated copies of a triangle go;
//   - faces hidden inside other parts go: wholly inside a closed convex part (a box inside a box), or lying on its surface
//     facing in (the bottom of a box stood on another); with `groundMin`, wholly under the ground too;
//   - flat regions (joined edge to edge, one plane, one material, one colour, one normal) are cut again from their
//     outline into the fewest triangles: a flat rectangle made of 50 is 2.
// Nothing is merged across a change of colour, material or normal, and a corner another face still uses stays (no
// T-junctions), so the shading is the same pixel for pixel. Glass and foliage hide nothing (they're see-through).
#pragma once
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>

namespace mk {
struct Mesh;
}

// One vertex as the optimiser works on it: the layout of render.h's Vtx (c[3]: the shader material, plus 100 sunlit,
// plus 200 per wear step).
struct OptVert {
    float p[3], n[3], c[4];
};

struct MeshOptOptions {
    bool degenerate = true;  // drop degenerate and repeated triangles
    bool hidden = true;      // drop faces hidden inside closed convex parts
    bool coplanar = true;    // merge flat regions
    double weldEps = 1e-5;   // m: corners closer than this are one (world-sized meshes in floats: 1e-4)
    // Optional: the lowest the ground gets over the rectangle x0..x1, z0..z1 (with whatever margin the caller wants);
    // a triangle wholly below it is buried.
    std::function<double(double x0, double z0, double x1, double z1)> groundMin;
};

struct MeshOptStats {
    size_t trianglesIn = 0, trianglesOut = 0;
    size_t degenerate = 0, repeated = 0, hidden = 0, buried = 0;
    size_t coplanarSaved = 0;   // triangles saved by merging flat regions
    size_t verticesIn = 0;
    size_t uniqueVertices = 0;  // distinct (position, normal, colour, material) in the input: what an indexed copy would keep
    size_t wasted() const { return trianglesIn - trianglesOut; }
    MeshOptStats& operator+=(const MeshOptStats& o);
};

MeshOptStats optimizeVerts(std::vector<OptVert>& v, const MeshOptOptions& o = MeshOptOptions());
MeshOptStats optimizeMesh(mk::Mesh& m, const MeshOptOptions& o = MeshOptOptions());  // (bounds refreshed)
// What optimizeMesh would do, leaving the mesh alone.
MeshOptStats analyzeMesh(const mk::Mesh& m, const MeshOptOptions& o = MeshOptOptions());

// The renderer's Vtx (or anything laid out like OptVert).
template <class V>
MeshOptStats optimizeVtx(std::vector<V>& v, const MeshOptOptions& o = MeshOptOptions()) {
    static_assert(sizeof(V) == sizeof(OptVert), "laid out like OptVert");
    std::vector<OptVert> w(v.size());
    if (!v.empty()) std::memcpy((void*)w.data(), (const void*)v.data(), v.size() * sizeof(OptVert));
    const MeshOptStats s = optimizeVerts(w, o);
    v.resize(w.size());
    if (!w.empty()) std::memcpy((void*)v.data(), (const void*)w.data(), w.size() * sizeof(OptVert));
    return s;
}
