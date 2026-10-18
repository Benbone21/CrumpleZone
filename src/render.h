#pragma once
#include <map>
#include <string>
#include <vector>

#include "daylight.h"
#include "frameprof.h"
#include "gl.h"
#include "treedraw.h"
#include "visibility.h"
#include "groundtex.h"
#include "lights.h"
#include "modelkit.h"
#include "propdraw.h"
#include "softbody.h"

class Effects;

struct Vtx {
    float p[3], n[3], c[4];  // c.a carries the shader material id
};

// Shader material ids (stored in Vtx color alpha).
enum ShaderMat {
    SM_MATTE = 0,
    SM_CONCRETE = 3,
    SM_PAINT = 4,
    SM_GLASS = 5,
    SM_EMISSIVE = 6,
    SM_TERRAIN = 7,
    SM_METAL = 8,
    SM_TIRE = 9,
    SM_CHROME = 11,
    SM_ROCK = 12,
    SM_FOLIAGE = 13,
    SM_WOOD = 14,
    SM_SLAB = 16,       // (a rock slab: SM_ROCK's grain and veins, none of its moss)
    SM_SNOWLEAF = 15,  // (matte; snow on a tree's foliage, which fades with it)
};

struct View {
    V3 eye, target, up;
    float fovy = 1.15f;
    int width = 1, height = 1;
    float time = 0;  // seconds, drives the drifting clouds
    // See-through trees (treefade.h): trees in the way dissolve. `focus` is what to keep in sight (the car, in
    // the chase and orbit views); `fadeNear` scales how far from the camera they dissolve (0 off, 1 chase).
    V3 focus;
    bool hasFocus = false;
    float fadeNear = 0;
};

// Graphics options (the pause menu's GRAPHICS section).
struct RenderSettings {
    int shadows = 3;            // 0 off, 1 low, 2 medium, 3 high
    int msaa = 4;               // samples: 0, 2, 4, 8
    bool ao = true;             // screen-space ambient occlusion
    bool bloom = true;
    bool clouds = true;
    int grass = 2;              // grass round the camera: 0 off, 1 low, 2 medium, 3 high
    int ground = 2;             // terrain materials: 0 procedural, 1 photo-scanned (one tap), 2 full, 3 full + more layers
    int props = 2;              // scanned props (propdraw.h): 0 off (code-made scenery), 1 low, 2 high, 3 ultra
    float renderScale = 1.0f;   // scene resolution relative to the window, 0.5 .. 2
    float drawDistance = 0;     // m: nothing drawn past it, fog thickening to hide the edge (0: everything, to the horizon)
    bool sharpen = false;       // sharpen the scene as it's scaled to the window (for a low render scale)
    float blur = 0;             // 0..1: blurs the scene behind a menu
};

// Linked shader programs cached on disk (render.cpp): "" turns it off (the default; scripted runs leave it so).
void setShaderCacheDir(const std::string& dir);
struct ShaderCacheStats { int hits = 0, compiled = 0; };
extern ShaderCacheStats gShaderCacheStats;

enum DebugView { DEBUG_OFF, DEBUG_XRAY, DEBUG_BEAMS_ONLY, DEBUG_PARTS, DEBUG_VIEW_COUNT };  // (parts: no body skin, to see the engine and running gear)

class Renderer {
public:
    RenderSettings settings;
    Daylight daylight;           // the sky and light for this frame (the game runs the clock)
    std::vector<int> carBeams;   // per car (sim.cars order): 0 lamps off, 1 low beam, 2 high beam
    bool init(const World& world, std::string& err);
    // Rebuilds the static scenery (terrain, boxes, trees, rocks, bushes, grass data) for
    // a new map.
    void setWorld(const World& world);
    // People on foot this frame (you, your buddies): drawn as simple figures.
    struct Figure {
        V3 p;            // feet, on the ground
        double yaw = 0;  // facing (0 = +x)
        V3 col;          // jacket
        double stride = 0;  // walk cycle phase (radians), legs and arms swing with it
    };
    std::vector<Figure> figures;
    // Code-made models (modelkit.h) drawn this frame only: a jack under a wheel, the tools in
    // your hands (handwork.h)...
    std::vector<Placed> frameModels;
    // On foot, a car part that's bent or broken (Placed::wear 1, 2) within `xrayRange` m of `xrayEye` glows through
    // whatever hides it (the body, a tire), faint and slow-pulsing, so you can tell from outside (0: off).
    V3 xrayEye;
    double xrayRange = 0;
    // A wheel being worked on this frame: slid out along its axle (coming off), and its snow
    // chain part wrapped (0..1; -1 as it is).
    struct WheelWork {
        int car = -1, wheel = -1;
        double out = 0, chain = -1;
    } wheelWork;
    // A wheel in your arms, drawn this frame (first person: low in front of the eye).
    struct Carried {
        bool on = false;
        V3 c, axis, up;  // center, its axle, which way is up for it
        WheelItem item;
    } carried;
    // The world editor: redraw just what changed (terrain cells i, j on the 1 m grid,
    // inclusive), the water, or the objects.
    void updateTerrainRegion(const World& world, int i0, int j0, int i1, int j1);
    void updateWater(const World& world);
    void updateObjects(const World& world);
    void drawScene(const Sim& sim, const View& view, DebugView debug, Effects* effects);

    // HUD (pixel coordinates, origin top-left). Queue with text()/rect(), then flushHud().
    void text(float x, float y, float scale, const std::string& s, float r, float g, float b, float a = 1);
    void rect(float x, float y, float w, float h, float r, float g, float b, float a);
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a);
    // Ring segment from angle a0 to a1 (radians, 0 = right, clockwise on screen).
    void arc(float cx, float cy, float r0, float r1, float a0, float a1, float r, float g, float b, float a, int segs = 32);
    float textWidth(const std::string& s, float scale) const { return s.size() * 6 * scale; }
    // A model's picture on the HUD (for menus): the mesh, lit from the front-left above and turned three-quarters, fitted and
    // centred in the rect; drawn in order with whatever else is queued. It's rendered once to a small texture of its own, when
    // the HUD is flushed, and kept under `key` (the model lab's picks changing draws it again). `a` fades it, `shade` darkens it.
    void icon(const std::string& key, const mk::Mesh& mesh, float x, float y, float w, float h, float a = 1, float shade = 1);
    void flushHud(int width, int height);

    std::vector<unsigned char> readPixels(int width, int height);
    int badScenePixels();  // (tests) pixels of the last frame's linear HDR scene, before bloom, that are NaN, infinite or negative
    const PropStats& propStats() const { return props.stats; }
    struct GeometryStats {
        unsigned terrainTriangles = 0, terrainFullTriangles = 0;
        unsigned terrainChunks = 0, terrainVisibleChunks = 0;
        unsigned sceneryVertices = 0, shadowSceneryVertices = 0;
        unsigned modelVertices = 0, shadowModelVertices = 0, modelDraws = 0, shadowModelDraws = 0;  // (of those: the code-made models, boxes and figures)
    };
    const GeometryStats& geometryStats() const { return geometryStats_; }
    double grassGpuMs() const { return grassGpuMs_; }  // (CZ_BENCH: the grass pass on the GPU, a few frames late)
    const VegStats& vegStats() const { return vegStats_; }  // (trees, bushes, logs and grass: what they sent the GPU last frame)
    // Frame timing (frameprof.h): the stage `p` starts now, on the CPU (frameProfile()) and, with gpuTiming, on the GPU
    // (a timestamp query). gpuFrameBegin() at the top of each frame reads back a frame from a few frames ago.
    bool gpuTiming = false;
    bool syncTiming = false;  // (--bench-sync: wait for the GPU at each mark, so the CPU times are each stage's whole cost)
    void passMark(FramePass p);
    void preload(const World& world);  // (after init and settings: loads what the first frame would)
    void gpuFrameBegin();

private:
    struct CarSlot {
        GLuint vao = 0, dyn = 0, ibo = 0, glassIbo = 0;
        const void* tpl = nullptr;
        int opaqueCount = 0, glassCount = 0;
    };

    static const int kBloomLevels = 6;
    GLuint progLit = 0, progDepth = 0, progLine = 0, progSky = 0, progHud = 0, progFx = 0, progCar = 0;
    GLuint progTerrain = 0;
    PropRenderer props;
    bool propsOk = false;     // its shaders built
    int propsApplied = -1;    // the props quality the scenery was last built for
    uint64_t treeSig_ = 0, scenerySig_ = 0, bushSig_ = 0;  // what the tree, scenery and bush meshes were last built from (unchanged: not rebuilt; an editor drag moves one thing)
    GLuint bushVao = 0, bushVbo = 0;  // reeds and logs (apart from the scenery: they seldom change when it does; the bushes themselves are instanced)
    int bushVertexCount = 0;
    GroundTextures groundTex;
    bool groundFailed = false;  // the textures couldn't be loaded: stay procedural
    int groundSize = 0;         // texels a side of what's loaded
    GLuint progAO = 0, progDown = 0, progUp = 0, progComposite = 0, postVao = 0, progGrass = 0, grassVao = 0, grassIbo = 0;
    GLuint sceneryVao = 0, sceneryVbo = 0, terrainTex = 0, waterVao = 0, waterVbo = 0, progWater = 0;
    int waterVertexCount = 0;
    int sceneryVertexCount = 0, terrainGridN = 0, mapId = 0, terrainMeshN = 0;
    unsigned figVao = 0, figVbo = 0;
    int figVertexCount = 0;
    unsigned xrayVao = 0, xrayVbo = 0;
    int xrayVertexCount = 0;
    GLuint progXray = 0;
    void buildFigures();
    double terrainSpacing = 1;
    GLuint terrainVao = 0, terrainVbo = 0, terrainIbo = 0;
    struct TerrainChunk {
        RenderBounds bounds;
        unsigned first[4] = {}, count[4] = {};
    };
    struct StaticBatch { RenderBounds bounds; int first = 0, count = 0; };
    std::vector<TerrainChunk> terrainChunks;
    std::vector<StaticBatch> boxBatches, sceneryBatches, bushBatches;
    GeometryStats geometryStats_;
    VegStats vegStats_;
    GLuint grassQuery_[4] = {};
    int grassQueryFrame_ = 0;
    bool grassQueryUsed_[4] = {};
    double grassGpuMs_ = 0;
    struct GpuMarks {
        static const int kMax = 48;
        GLuint q[kMax] = {};
        uint8_t pass[kMax] = {};
        int n = 0;
    };
    GpuMarks gpuMarks_[4];
    // AO, bloom and clouds ease in and out over ~0.6 s when they're switched (adaptive quality: no pops); -1 = not yet drawn.
    float fadeAO_ = -1, fadeBloom_ = -1, fadeClouds_ = -1;
    double fadeClock_ = 0;
    float farZ_ = 6000;  // this frame's far plane (a draw distance brings it in)
    int gpuMarkFrame_ = 0;
    void uploadBatches(GLuint& vao, GLuint& vbo, std::vector<Vtx>& vertices, std::vector<StaticBatch>& batches);
    void drawBatches(GLuint vao, const std::vector<StaticBatch>& batches, const M4& vp, bool shadow);
    void drawBushes(const M4& vp, const V3& eye, bool shadow, VegStats::Pass& stats);  // (the reed and log batches within sight: small things, not drawn far off)
    void drawTerrain(const M4& vp, const View& view);
    GLuint boxVao = 0, boxVbo = 0;
    int boxVertexCount = 0;
    TreeDraw trees_, bushes_;       // the forest and the bushes, instanced (treedraw.h)
    // How far the vegetation's detail reaches (its LOD distances and reaches), by the scenery quality.
    double vegDetail() const { static const double k[4] = {0.6, 0.7, 1.0, 1.3}; return k[std::min(std::max(settings.props, 0), 3)]; }
    GLuint progTree = 0, progTreeDepth = 0;
    int treeVertexCount = 0;
    ScanCover scanCover_;           // what the scanned props draw instead of the code-made meshes
    std::vector<float> treeFade_;   // per tree (World::trees): how much of it the camera hides now (treefade.h)
    double fadeTime_ = 0;
    GLuint wheelVao[WHEEL_STYLES] = {}, wheelVbo[WHEEL_STYLES] = {}, caliperVao = 0, caliperVbo = 0;
    // Each tread on each wheel style (the tire alone: the rim is the wheel's), and snow chains.
    GLuint tireVao[WHEEL_STYLES][TIRE_TYPES] = {}, tireVbo[WHEEL_STYLES][TIRE_TYPES] = {}, chainVao = 0, chainVbo = 0;
    int treadVertexCount[WHEEL_STYLES][TIRE_TYPES] = {}, chainVertexCount = 0;
    int tireVertexCount[WHEEL_STYLES] = {}, wheelVertexCount[WHEEL_STYLES] = {}, caliperVertexCount = 0;
    GLuint dynVao = 0, dynVbo = 0;  // per-frame lit geometry: car parts and running gear
    GLuint lineVao = 0, lineVbo = 0;
    GLuint fxVao = 0, fxVbo = 0;
    GLuint hudVao = 0, hudVbo = 0, fontTex = 0;
    GLuint skyVao = 0;
    GLuint shadowFbo[2] = {}, shadowTex[2] = {};  // near and far cascade
    int shadowSize = 0;
    // Offscreen HDR targets: the multisampled scene (when MSAA is on) resolves into
    // sceneTex/depthTex; ambient occlusion and the bloom chain work from those.
    int targetW = 0, targetH = 0, targetSamples = -1, aoW = 1, aoH = 1;
    int targetRequestedSamples = -1;
    GLuint msFbo = 0, msColor = 0, msDepth = 0, sceneFbo = 0, sceneTex = 0, depthTex = 0, aoFbo = 0, aoTex = 0;
    GLuint bloomFbo[kBloomLevels] = {}, bloomTex[kBloomLevels] = {};
    int bloomW[kBloomLevels] = {}, bloomH[kBloomLevels] = {};

    std::vector<CarSlot> carSlots;
    std::vector<CarLamps> carLampState;  // per car this frame (prepareCars)
    // The headlamps lit as spot lights this frame (the nearest cars'), for the shaders.
    static const int kMaxLamps = 16;
    int lampCount = 0;
    float lampPos[kMaxLamps * 4], lampDir[kMaxLamps * 4], lampUp[kMaxLamps * 4];
    std::vector<std::pair<const void*, GLuint>> templateVbos;
    std::vector<Vtx> partVerts, lineVerts, fxVerts;
    std::vector<float> hudVerts;
    struct IconDraw {
        std::string key;
        const mk::Mesh* mesh;
        size_t vert;  // where its six vertices start in hudVerts (in vertices)
    };
    std::vector<IconDraw> iconDraws;
    struct IconTex {
        uint64_t picks = 0;
        GLuint tex = 0;
    };
    std::map<std::string, IconTex> iconCache;
    GLuint progIcon = 0, progIconMesh = 0, iconVao = 0, iconVbo = 0;
    GLuint iconTexture(const std::string& key, const mk::Mesh& mesh);

    void buildTerrain(const World& world);
    void buildBoxes(const World& world);
    void buildWheel();
    void buildWheelStyle(int style);
    void buildTire(int style, int tread);
    void buildChains();
    void buildFont();
    void buildBackdrop(const World& world, std::vector<Vtx>& verts, std::vector<unsigned>& idx);
    void ensureShadows(int size);
    void ensureGround(int quality);
    void ensureTargets(int w, int h, int samples);
    void postProcess(const View& view, const M4& proj);
    void prepareCars(const Sim& sim, DebugView debug);
    void drawCarBodies(const Sim& sim, GLuint prog, bool glassPass);
    void drawWheels(const Sim& sim, GLuint prog);
};
