#pragma once
#include <string>
#include <vector>

#include "daylight.h"
#include "gl.h"
#include "groundtex.h"
#include "lights.h"
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
};

struct View {
    V3 eye, target, up;
    float fovy = 1.15f;
    int width = 1, height = 1;
    float time = 0;  // seconds, drives the drifting clouds
};

// Graphics options (the pause menu's GRAPHICS section).
struct RenderSettings {
    int shadows = 3;            // 0 off, 1 low, 2 medium, 3 high
    int msaa = 4;               // samples: 0, 2, 4, 8
    bool ao = true;             // screen-space ambient occlusion
    bool bloom = true;
    bool clouds = true;
    bool grass = true;          // grass blades round the camera
    int ground = 2;             // terrain materials: 0 procedural, 1 photo-scanned (one tap), 2 full, 3 full + more layers
    int props = 2;              // scanned props (propdraw.h): 0 off (code-made scenery), 1 low, 2 high, 3 ultra
    float renderScale = 1.0f;   // scene resolution relative to the window, 0.5 .. 2
    float blur = 0;             // 0..1: blurs the scene behind a menu
};

enum DebugView { DEBUG_OFF, DEBUG_XRAY, DEBUG_BEAMS_ONLY, DEBUG_VIEW_COUNT };

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
    void flushHud(int width, int height);

    std::vector<unsigned char> readPixels(int width, int height);
    const PropStats& propStats() const { return props.stats; }

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
    GroundTextures groundTex;
    bool groundFailed = false;  // the textures couldn't be loaded: stay procedural
    int groundSize = 0;         // texels a side of what's loaded
    GLuint progAO = 0, progDown = 0, progUp = 0, progComposite = 0, postVao = 0, progGrass = 0;
    GLuint sceneryVao = 0, sceneryVbo = 0, terrainTex = 0, waterVao = 0, waterVbo = 0, progWater = 0;
    int waterVertexCount = 0;
    int sceneryVertexCount = 0, terrainGridN = 0, mapId = 0, terrainMeshN = 0;
    unsigned figVao = 0, figVbo = 0;
    int figVertexCount = 0;
    void buildFigures();
    double terrainSpacing = 1;
    GLuint terrainVao = 0, terrainVbo = 0, terrainIbo = 0;
    int terrainIndexCount = 0;
    GLuint boxVao = 0, boxVbo = 0;
    int boxVertexCount = 0;
    GLuint treeVao = 0, treeVbo = 0;
    int treeVertexCount = 0;
    GLuint wheelVao[WHEEL_STYLES] = {}, wheelVbo[WHEEL_STYLES] = {}, caliperVao = 0, caliperVbo = 0;
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

    void buildTerrain(const World& world);
    void buildBoxes(const World& world);
    void buildWheel();
    void buildWheelStyle(int style);
    void buildFont();
    void buildBackdrop(const World& world, std::vector<Vtx>& verts, std::vector<unsigned>& idx);
    void ensureShadows(int size);
    void ensureTargets(int w, int h, int samples);
    void postProcess(const View& view, const M4& proj);
    void prepareCars(const Sim& sim, DebugView debug);
    void drawCarBodies(const Sim& sim, GLuint prog, bool glassPass);
    void drawWheels(const Sim& sim, GLuint prog);
};
