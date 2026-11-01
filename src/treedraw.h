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
extern const char* const kTreePreFSBody;  // (after kCommonFS: the main pass's depth pre-pass, with kTreeVS)
// Trees past their meshes' reach as pictures (impostors): each tree model's look baked from 8 sides into a texture array
// (TreeDraw::enableImpostors), a quad per tree turned to the eye (to the light for the shadow maps) showing the two nearest
// sides. kImpostorVS places the window's trees; kFarForestVS (farforest.h) the far forest's; both end in the same shading.
extern const char* const kImpostorVS;
extern const char* const kImpostorFSBody;  // (after kCommonFS)
extern const char* const kImpostorDepthFS;
extern const char* const kImpostorPlace;   // (the vertex stages' shared part: #version, this, then kImpostorVS or kFarForestVS)
extern const char* const kImpostorCaptureVS;
extern const char* const kImpostorCaptureFS;

// What the vegetation sent the GPU last frame (CZ_BENCH, tests): per pass, the draw calls, triangles and
// instances (trees, bushes) or tufts (grass) handed over.
struct VegStats {
    struct Pass {
        int draws = 0, inst = 0;
        long long tris = 0;
        void add(int d, long long t, int i = 0) { draws += d, tris += t, inst += i; }
    };
    Pass trees, bushes, logs, grass, farTrees;       // the main pass (farTrees: the far forest's tiles)
    Pass treesShadow, bushesShadow, logsShadow;      // both shadow cascades
};

// The baked looks (one texture array layer each: a tree kind's design, snowy or not, seen from 8 sides), shared by every TreeDraw
// that draws pictures (the window's and the one staged for the next): baked once, kept across windows.
struct ImpostorAtlas {
    static constexpr int kViews = 8, kCellW = 96, kCellH = 192, kMaxLayers = 32;
    GLuint captureProg = 0, tex[2] = {0, 0}, fbo = 0, depth = 0;
    struct Layer {
        int kind = 0, design = 0;
        bool snow = false;
        float R = 1, H = 10;  // the quad's half-width and height (model units)
    };
    std::vector<Layer> layers;
    bool ready() const { return tex[0] != 0; }
    void bind(int unitCol, int unitNrm) const;
};

class TreeDraw {
public:
    void build(const forest::Set& set);
    // The same in two halves: the CPU's part (any thread), then onto the GPU.
    struct Built;
    static void prepare(const forest::Set& set, Built& out);
    void upload(Built&& b);
    // Draws what the view sees with the tree program in use. Returns the vertices sent.
    unsigned draw(const forest::View& view, VegStats::Pass* stats = nullptr);
    // What the last draw() drew, again (another program: the shading pass after a depth pre-pass).
    unsigned redraw(VegStats::Pass* stats = nullptr);
    int instances() const { return (int)set_.inst.size(); }
    size_t meshVertices() const { return meshVerts_; }  // every model at every LOD, three a triangle
    size_t bufferVertices() const { return uniqueVerts_; }  // what the buffer holds (each corner once)
    size_t bakedVertices() const { return baked_; }     // what one baked mesh of every instance would hold

    // Pictures (trees only): `captureProg` bakes them (kImpostorCapture*). Call before build()/upload().
    void enableImpostors(ImpostorAtlas* atlas) { atlas_ = atlas; }
    const ImpostorAtlas* atlas() const { return atlas_; }
    // Where the pictures take over from the meshes, and hand over to the far forest (m, at detail 1).
    struct ImpostorRange {
        double from = 200, band = 30;    // meshes to pictures over [from - band, from] (a hard switch at `from` for shadows)
        double to = 0, toBand = 150;     // pictures to the far forest over [to - toBand, to] (0: none)
        double thin = 0;                 // thinned with distance as forest::View::maxDist thins the meshes (0: not)
    };
    // The window's trees past `from` as pictures, with `prog` (kImpostorVS + kImpostorFSBody, or the depth one) in use: the
    // cells in view, each tree placed, faded and thinned on the GPU. Returns the vertices sent.
    unsigned drawImpostors(const forest::View& view, const ImpostorRange& r, GLuint prog, bool shadow, VegStats::Pass* stats = nullptr);
    bool impostorsReady() const { return atlas_ && atlas_->ready() && impVao_ != 0; }
    // The pictures' layers by kind (0 pine, 1 broadleaf, 2 snag): which layers the window's trees of that kind use and how
    // often (shares sum to 1), and each layer's half-width and height in model units (the far forest's trees).
    struct KindLayers { std::vector<std::pair<int, float>> layers; };
    const KindLayers& kindLayers(int kind) const { return kindLayers_[kind < 0 || kind > 2 ? 0 : kind]; }

private:
    forest::Set set_;  // (the meshes dropped once they're in the buffer)
    // Pictures.
    ImpostorAtlas* atlas_ = nullptr;
    GLuint impVao_ = 0, impVbo_ = 0;
    std::vector<int> shapeLayer_;          // per shape: its layer (-1 none)
    std::vector<int> impCellFirst_, impCellCount_;  // per picker cell: its trees in the picture buffer
    std::vector<int> impCells_;
    std::vector<std::pair<int, int>> impRuns_;
    KindLayers kindLayers_[3];
    void captureImpostors();
    void uploadImpostors();
    forest::Picker picker_;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0, instVbo_ = 0;
    size_t instCap_ = 0;  // floats the instance buffer holds
    struct Range {
        int first = 0, count = 0;
    };
    std::vector<std::array<Range, forest::kLods>> ranges_;  // per shape
public:
    struct Built {
        forest::Set set;
        std::vector<float> v;
        std::vector<unsigned> idx;
        std::vector<std::array<Range, forest::kLods>> ranges;
        forest::Picker picker;
        size_t meshVerts = 0, baked = 0;
    };
private:
    std::vector<std::vector<forest::Pick>> buckets_;  // per (shape, LOD): what a pass draws
    std::vector<float> data_;
    struct Run {
        int bucket, n;
        size_t at;
    };
    std::vector<Run> runs_;
    size_t meshVerts_ = 0, baked_ = 0, uniqueVerts_ = 0;
};
