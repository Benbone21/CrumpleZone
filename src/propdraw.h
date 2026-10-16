// The scanned props on screen (Poly Haven, CC0; baked by tools/bakeprops.cpp into
// assets/props/): instanced, textured meshes (diffuse, normal and AO/roughness/metal maps)
// lit by the scene's own sun, sky, fog, headlamps and shadow cascades, with distance LODs,
// frustum culling and far billboards for the trees.
//
// What it draws, per map:
//   scanned rocks (Rock::scan) with the very frame their hull was placed with, and cliff faces
//   on steep ground. The code-made meshes skip whatever this covers (ScanCover).
//   The vegetation paths below (pines -> fir/pine saplings, snags, logs, bushes -> shrubs, and
//   visual-only scatter near the camera: grass, ferns, stumps, branches) wake up only if
//   assets/props has those models: the baker leaves them out unless run with --plants, so the
//   game's own code-made trees, bushes, logs and stumps are what you see.
// Quality (RenderSettings::props): 0 off (all code-made), 1 low, 2 high, 3 ultra: LOD
// distances, the billboard distance, scatter reach and density, texture size.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gl.h"
#include "mathx.h"
#include "props.h"
#include "treefade.h"
#include "world.h"

// What the scans draw instead of the code-made scenery meshes.
struct ScanCover {
    bool pines = false, snags = false, rocks = false, logs = false, bushes = false;
    bool any() const { return pines || snags || rocks || logs || bushes; }
};

// What the props cost the last frame. GPU time comes from timer queries a few frames old.
struct PropStats {
    int draws = 0, instances = 0, billboards = 0, scatter = 0;
    long long tris = 0;
    int shadowDraws = 0, shadowInstances = 0;
    long long shadowTris = 0;
    double cpuMs = 0;   // selection, buffer upload and draw submission (CPU, GL driver calls included)
    double selectMs = 0;  // of which picking LODs and culling (no GL)
    double gpuMs = -1;  // the props' share of the GPU frame: shadow passes + main pass (-1: none yet)
};

class PropRenderer {
public:
    using Linker = std::function<GLuint(const std::string& vs, const std::string& fs, std::string& err)>;
    // Compiles the shaders (the lit ones follow `commonFS`, render.cpp's shared chunk).
    bool init(const std::string& commonFS, const Linker& link, std::string& err);
    // Loads the models and textures the first time scans are wanted (texture size by
    // quality); false if assets/props isn't there (the scenery stays code-made).
    bool ensureLoaded(int quality);
    bool loaded() const { return !models.empty(); }
    ScanCover cover(int quality) const;
    // The map's static instances (trees, snags, logs, bushes, scanned rocks, cliffs).
    void setWorld(const World& world, int quality);
    // Per frame, before beginFrame: the camera trees dissolve for (treefade.h).
    void setSeeThrough(const SeeThrough& s) { see = s; }
    // Per frame, before any pass: picks LODs, culls against the view and updates the scatter.
    void beginFrame(const World& world, const V3& eye, const M4& viewProj, int quality, float time);
    // Shadow pass into the bound depth target (the caller sets polygon offset). `cascade`
    // 0 near, 1 far; lightDir points toward the light.
    void drawShadow(const M4& lightVP, int cascade, const V3& lightDir);
    // Main pass: `setLighting` sets the shared uniforms (sun, sky, fog, shadows, lamps) on a program.
    void draw(const M4& viewProj, const std::function<void(GLuint)>& setLighting);
    void endFrame();  // collects the GPU timings
    // The editor changed the ground in [x0, x1] x [z0, z1]: the scatter there is placed again.
    void terrainChanged(double x0, double z0, double x1, double z1);
    PropStats stats;

private:
    struct Mat {
        GLuint tex[3] = {0, 0, 0};
        int alpha = 0;
        bool twoSided = false, foliage = false;
        float wind = 0, cutoff = 0.5f;
    };
    struct Sub {
        GLuint vao = 0, vbo = 0, ibo = 0;
        int count = 0, mat = 0;
    };
    struct Model {
        std::string name;
        int kind = 0;
        float bmin[3], bmax[3];
        float radius = 1, height = 1;      // widest reach round the y axis, top (model units)
        V3 faceNormal;                     // cliffs: the way the face looks (area-weighted mean normal)
        std::vector<Mat> mats;
        std::vector<std::vector<Sub>> lods;
        std::vector<int> lodTris;
        GLuint bb[2] = {0, 0};             // billboard atlas: colour + coverage, normal
    };
    // One placed copy: model -> world affine (rows), and per-instance extras
    // (tint, wind phase, snow, unused), bounds, and how it picks its LOD.
    struct Inst {
        int model;
        float m[12];
        float extra[4];
        V3 c;          // bounding sphere
        float r;
        float size;    // what the LOD distances scale with (m)
        bool tree;     // far billboard
        bool shadow;   // casts shadows
        bool fades = false;   // dissolves when the camera is in it or it's in the way (trees and snags)
        float fadeNow = 0;    // how much of it is hidden now, and the frame that was worked out in
        unsigned fadeFrame = 0;
    };
    struct Cell {
        V3 mn, mx;
        std::vector<int> inst;
    };
    std::vector<Model> models;
    std::map<std::string, int> byName;
    std::map<std::string, GLuint> texCache;
    int loadedQuality = -1;
    bool tried = false;
    GLuint progLit = 0, progDepth = 0, progBB = 0, progBBDepth = 0;
    GLuint instVbo = 0, bbVao = 0, bbVbo = 0;
    size_t instCap = 0, bbCap = 0;

    std::vector<Inst> statics;
    std::vector<Cell> cells;      // statics in 32 m cells
    int cellN = 0;
    const World* world = nullptr;
    int quality = 2;
    float time = 0;
    V3 eye;
    SeeThrough see;
    unsigned frameNo = 0;
    double fadeDt = 0, lastFadeTime = 0;
    // Scatter: visual-only clumps round the camera, by 12 m cell (made when the camera
    // comes near, dropped when it leaves).
    std::map<long long, std::vector<Inst>> scatter;
    std::vector<int> scatterGrass, scatterFern, scatterLitter;
    std::vector<uint8_t> treeNear;  // trees per 16 m cell (forest floor or meadow)
    int treeNearN = 0;
    // The models the world's objects map to.
    std::vector<int> pineModels, snagModels, logModels, bushModels, cliffModels;

    // This frame's draw lists: per (model, lod) the instance records, and per model the billboards.
    struct Bucket {
        int model, lod;
        std::vector<float> data;  // 16 floats an instance
    };
    std::vector<Bucket> buckets;
    std::map<long long, int> bucketOf;
    std::vector<std::vector<float>> bbData;  // per model: 8 floats a billboard
    std::vector<float> uploadBuf;

    // Timer queries: ring of frames, a query per pass.
    static const int kQueryFrames = 4;
    GLuint queries[kQueryFrames][3] = {};
    bool queryUsed[kQueryFrames][3] = {};
    int queryFrame = 0;
    double lastGpuMs = -1;
    void beginQuery(int pass);
    void endQuery();

    GLuint texture(const std::string& file, bool srgb, int maxSize);
    void addInstance(std::vector<Inst>& out, int model, const V3 ax[3], const V3& origin, float tint, float snow, uint32_t seed, bool tree,
                     bool shadow, float size);
    void buildScatterCell(int cx, int cz, std::vector<Inst>& out);
    void select(const float planes[6][4], bool useBillboards, int lodBias, bool shadowPass);
    void submit(GLuint prog, bool depth, long long& tris, int& draws, int& instances);
    void submitBillboards(GLuint prog, bool depth);
    int lodFor(const Inst& in, double dist) const;
};
