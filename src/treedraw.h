// Instanced vegetation (forest.h): the trees, and the bushes the same way. Every model's meshes in one
// buffer; a pass picks the instances in view (forest::Picker: cells, then each one's sphere, its reach
// and LOD by its distance from the eye) and draws each (model, LOD) once for all of them.
#pragma once
#include <array>
#include <vector>

#include "forest.h"
#include "gl.h"
#include "visibility.h"

// The lit shader's vertex stage, placing an instance, and its own fragment stage (the lit one cut down to
// what vegetation is made of, and dithering away an instance fading out at its reach); the same for the
// shadow maps.
extern const char* const kTreeVS;
extern const char* const kTreeFSBody;
extern const char* const kTreeDepthVS;

// What the vegetation sent the GPU last frame (CZ_BENCH, tests): per pass, the draw calls, triangles and
// instances (trees, bushes) or tufts (grass) handed over.
struct VegStats {
    struct Pass {
        int draws = 0, inst = 0;
        long long tris = 0;
        void add(int d, long long t, int i = 0) { draws += d, tris += t, inst += i; }
    };
    Pass trees, bushes, logs, grass;                 // the main pass
    Pass treesShadow, bushesShadow, logsShadow;      // both shadow cascades
};

class TreeDraw {
public:
    void build(const forest::Set& set);
    // Draws what the view sees with the tree program in use. Returns the vertices sent.
    unsigned draw(const forest::View& view, VegStats::Pass* stats = nullptr);
    int instances() const { return (int)set_.inst.size(); }
    size_t meshVertices() const { return meshVerts_; }  // every model at every LOD, three a triangle
    size_t bufferVertices() const { return uniqueVerts_; }  // what the buffer holds (each corner once)
    size_t bakedVertices() const { return baked_; }     // what one baked mesh of every instance would hold

private:
    forest::Set set_;  // (the meshes dropped once they're in the buffer)
    forest::Picker picker_;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0, instVbo_ = 0;
    size_t instCap_ = 0;  // floats the instance buffer holds
    struct Range {
        int first = 0, count = 0;
    };
    std::vector<std::array<Range, forest::kLods>> ranges_;  // per shape
    std::vector<std::vector<forest::Pick>> buckets_;  // per (shape, LOD): what a pass draws
    std::vector<float> data_;
    struct Run {
        int bucket, n;
        size_t at;
    };
    std::vector<Run> runs_;
    size_t meshVerts_ = 0, baked_ = 0, uniqueVerts_ = 0;
};
