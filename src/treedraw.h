// The forest drawn instanced (forest.h): every tree model's meshes in one buffer, the trees sorted into
// cells. A pass takes the cells in view, gives each tree a LOD by how far it is from the eye, and draws
// each (model, LOD) once for all of its trees.
#pragma once
#include <array>
#include <vector>

#include "forest.h"
#include "gl.h"
#include "visibility.h"

// The lit shader's vertex stage, placing an instance (its fragment stage is the lit one), and the same
// for the shadow maps.
extern const char* const kTreeVS;
extern const char* const kTreeDepthVS;

class TreeDraw {
public:
    // (`skipPines`, `skipSnags`: drawn by the scanned props instead.)
    void build(const World& w, bool skipPines, bool skipSnags);
    // Draws what `vp` sees with the tree program in use; `lodBias` coarser than the distance says (a far
    // shadow cascade). Returns the vertices sent.
    unsigned draw(const M4& vp, const V3& eye, int lodBias = 0);
    int instances() const { return (int)inst_.size(); }
    size_t meshVertices() const { return meshVerts_; }  // in the buffer: every model at every LOD
    size_t bakedVertices() const { return baked_; }     // what one baked mesh of every tree would hold

private:
    std::vector<forest::Inst> inst_;
    GLuint vao_ = 0, vbo_ = 0, instVbo_ = 0;
    struct Range {
        int first = 0, count = 0;
    };
    std::vector<std::array<Range, forest::kLods>> ranges_;  // per shape
    struct Cell {
        RenderBounds bounds;
        std::vector<int> inst;
    };
    std::vector<Cell> cells_;
    std::vector<std::vector<int>> buckets_;  // per (shape, LOD): the trees a pass draws
    std::vector<float> data_;
    std::vector<std::pair<double, int>> order_;  // the cells in view, nearest first
    size_t meshVerts_ = 0, baked_ = 0;
};
