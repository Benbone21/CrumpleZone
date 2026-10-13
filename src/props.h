// Scanned props (Poly Haven, CC0): the baked model files in assets/props/ and the rock
// collision hulls made from them. Nothing here touches OpenGL, so the physics (and the
// tests) can use it; the renderer's instanced drawing is in propdraw.cpp.
//
// A model is one scanned object in its own frame (x, z centred, y = 0 at the base) at up to
// three levels of detail, each a few sub-meshes sharing a material (diffuse, normal and
// AO/roughness/metal textures). tools/bakeprops.cpp makes them from the glTF sources.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "mathx.h"

namespace props {

enum Kind { KIND_ROCK, KIND_STUMP, KIND_LOG, KIND_CLIFF, KIND_TREE, KIND_PLANT, KIND_BRANCHES };

struct Vert {
    float p[3], n[3], uv[2], t[4];  // position, normal, texcoord, tangent (w = bitangent sign)
};

struct Material {
    int alphaMode = 0;        // 0 opaque, 1 alpha-tested (leaf cards), 2 blended (kept for the impostors)
    bool doubleSided = false;
    bool foliage = false;     // lit like leaves: wrapped light, sun glowing through
    float wind = 0;           // how far the tips sway (fraction of the model height)
    float cutoff = 0.5f;
    std::string tex[3];       // diffuse, normal, ARM (file names inside assets/props/tex/); "" = none
};

struct Sub {
    int mat = 0;
    std::vector<Vert> v;
    std::vector<uint16_t> idx;
};

struct Lod {
    float maxDist = 1e9f;  // used up to about this far from the camera (m, at scale 1)
    std::vector<Sub> subs;
    int tris() const {
        int n = 0;
        for (const Sub& s : subs) n += (int)s.idx.size() / 3;
        return n;
    }
};

struct Model {
    std::string name;
    int kind = KIND_ROCK;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};  // of the first LOD
    std::vector<Material> mats;
    std::vector<Lod> lods;
};

// File I/O (the baker writes, the game reads). Positions are quantised to 16 bits across
// the model's bounds, normals and tangents to 8 bits, texcoords to 1/4096.
bool saveModel(const std::string& path, const Model& m);
bool loadModel(const std::string& path, Model& m);

// The assets/props folder: $CZ_PROPS_DIR if set, else next to the working directory, or the
// executable, or a few levels up from it (empty if not found). Cached after the first call.
std::string assetDir();

// ---- Rock collision hulls -------------------------------------------------------------
// One per scanned rock: the convex hull of the scan reduced to <= 48 corners, in the model's
// own frame (the same one the visual mesh is in, so the two line up under any transform).
struct RockHull {
    std::string name;
    float bmin[3], bmax[3];                 // of the hull
    std::vector<V3> v;                      // corners
    std::vector<std::array<int, 3>> f;      // faces, wound outward
    V3 half() const { return V3((bmax[0] - bmin[0]) * 0.5, (bmax[1] - bmin[1]) * 0.5, (bmax[2] - bmin[2]) * 0.5); }
};
struct HullLibrary {
    std::vector<RockHull> hulls;
    bool loaded() const { return !hulls.empty(); }
    // Reads rocks.hull from the folder; false (and nothing loaded) if it isn't there.
    bool load(const std::string& dir);
    bool save(const std::string& path) const;
};

// The convex hull of `pts` cut down to at most `maxVerts` corners (each a corner of the true
// hull: the farthest miss is added until the budget runs out), then pushed out along the
// corner normals by half the worst miss so the scan hardly pokes through. Faces wound outward.
// worstMiss (optional) gets how far the farthest point of `pts` was outside the cut-down hull.
RockHull buildRockHull(const std::string& name, const std::vector<V3>& pts, int maxVerts, double* worstMiss = nullptr);

// ---- Which rocks World::addRock makes --------------------------------------------------
// Scanned hulls are opt-in, per thread: the game turns them on (on the thread that builds
// its maps) when assets/props is there; build/run_tests never does, so the physics tests
// keep the code-made rocks they were tuned on. tests/props.cpp turns them on for its own
// scenarios with ScopedRockScans.
const HullLibrary* activeRockScans();            // nullptr: code-made rocks
void setActiveRockScans(const HullLibrary* lib);
// rocks.hull from assetDir() (or $CZ_PROPS_DIR), read once; empty if there's none.
const HullLibrary& sharedHullLibrary();
struct ScopedRockScans {
    const HullLibrary* prev;
    explicit ScopedRockScans(const HullLibrary* lib) : prev(activeRockScans()) { setActiveRockScans(lib); }
    ~ScopedRockScans() { setActiveRockScans(prev); }
};

// The models listed in props.txt (the baker writes it): name and kind, in file order.
struct ManifestEntry {
    std::string name;
    int kind = KIND_ROCK;
};
std::vector<ManifestEntry> readManifest(const std::string& dir);

}  // namespace props
