// Mesh audit: builds every mesh the game makes and counts what's wasted in it (meshopt.h): triangles of flat regions
// that could be fewer, degenerate and repeated ones, faces hidden inside other parts or under the ground, and
// vertices an indexed copy wouldn't repeat. The models as written (meshopt off) against what the game draws now.
//
//   SDL_VIDEODRIVER=offscreen build/meshaudit [--out build/review/meshaudit.txt] [--fast]
//
// Sorted by waste x copies in a typical scene (the Frontier, as you start it), so the worst offenders come first;
// then a Frontier start view's triangles, every draw call counted, as written and as drawn now. (--fast: no terrain
// or levels.) Needs an OpenGL context for the renderer's own meshes (terrain, water, wheels): it records what goes
// up to the GPU.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
// (The renderer's buffers, to read back what it built.)
#define private public
#include "../src/render.h"
#undef private
#include "../src/carvisual.h"
#include "../src/forest.h"
#include "../src/level.h"
#include "../src/meshopt.h"
#include "../src/modelkit.h"
#include "../src/props.h"
#include "../src/scenery.h"
#include "../src/sdl_min.h"
#include "../src/vehicles.h"

namespace {

// ---- What goes up to the GPU -------------------------------------------------------------------------

std::map<GLenum, GLuint> bound;
std::map<GLuint, std::vector<char>> uploads;
gl::PFN_BindBuffer realBind;
gl::PFN_BufferData realData;
gl::PFN_DrawArrays realDrawArrays;
gl::PFN_DrawElements realDrawElements;
gl::PFN_DrawArraysInstanced realDrawArraysInst;
gl::PFN_DrawElementsInstanced realDrawElementsInst;
gl::PFN_UseProgram realUse;
gl::PFN_BindVertexArray realVao;
gl::PFN_BindFramebuffer realFbo;
long long drawnTris = 0, drawCalls = 0;
GLuint curProg = 0, curVao = 0, curFbo = 0;
std::map<GLuint, std::string> progName, vaoName;
std::set<GLuint> shadowFbos;
std::map<std::string, long long> byWhat;  // a frame's triangles by what drew them
void hookUse(GLuint p) { curProg = p, realUse(p); }
void hookVao(GLuint a) { curVao = a, realVao(a); }
void hookFbo(GLenum t, GLuint f) { curFbo = f, realFbo(t, f); }
void hookBind(GLenum t, GLuint b) { bound[t] = b, realBind(t, b); }
void hookData(GLenum t, GLsizeiptr n, const void* p, GLenum u) {
    std::vector<char>& d = uploads[bound[t]];
    d.assign((const char*)p, p ? (const char*)p + n : (const char*)p);
    realData(t, n, p, u);
}
void tally(GLenum mode, long long count, long long inst) {
    drawCalls++;
    if (mode != GL_TRIANGLES) return;
    drawnTris += count / 3 * inst;
    auto v = vaoName.find(curVao);
    auto p = progName.find(curProg);
    std::string what = v != vaoName.end() ? v->second : p != progName.end() ? p->second : "other";
    if (shadowFbos.count(curFbo)) what += " [shadow]";
    byWhat[what] += count / 3 * inst;
}
void hookDA(GLenum m, GLint f, GLsizei c) { tally(m, c, 1), realDrawArrays(m, f, c); }
void hookDE(GLenum m, GLsizei c, GLenum t, const void* o) { tally(m, c, 1), realDrawElements(m, c, t, o); }
void hookDAI(GLenum m, GLint f, GLsizei c, GLsizei n) { tally(m, c, n), realDrawArraysInst(m, f, c, n); }
void hookDEI(GLenum m, GLsizei c, GLenum t, const void* o, GLsizei n) { tally(m, c, n), realDrawElementsInst(m, c, t, o, n); }

std::vector<OptVert> vtxOf(GLuint vbo) {
    std::vector<OptVert> v;
    auto it = uploads.find(vbo);
    if (vbo == 0 || it == uploads.end()) return v;
    v.resize(it->second.size() / sizeof(OptVert));
    if (!v.empty()) std::memcpy((void*)v.data(), it->second.data(), v.size() * sizeof(OptVert));
    return v;
}

// ---- Meshes ------------------------------------------------------------------------------------------

std::vector<OptVert> soup(const mk::Mesh& m) {
    std::vector<OptVert> v(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        std::memcpy(v[i].p, m.v[i].p, sizeof v[i].p), std::memcpy(v[i].n, m.v[i].n, sizeof v[i].n);
        v[i].c[0] = m.v[i].c[0], v[i].c[1] = m.v[i].c[1], v[i].c[2] = m.v[i].c[2], v[i].c[3] = m.v[i].mat;
    }
    return v;
}
template <class V>
std::vector<OptVert> soupOf(const std::vector<V>& x) {
    std::vector<OptVert> v(x.size());
    if (!x.empty()) std::memcpy((void*)v.data(), (const void*)x.data(), x.size() * sizeof(OptVert));
    return v;
}

struct Row {
    std::string name, owner;
    long long inst = 0;
    size_t raw = 0, now = 0, verts = 0;  // triangles as written, as the game builds it; vertices now
    MeshOptStats was, is;                // what meshopt finds in each
    long long before() const { return (long long)was.wasted() * std::max(1LL, inst); }
    long long after() const { return (long long)is.wasted() * std::max(1LL, inst); }
};
std::vector<Row> rows;
double lastMs = 0, lastRawMs = 0;

MeshOptOptions worldOpt() {
    MeshOptOptions o;
    o.weldEps = 1e-4;
    return o;
}

void add(const std::string& name, const std::string& owner, long long inst, const std::vector<OptVert>& raw, const std::vector<OptVert>& now,
         const MeshOptOptions& o = MeshOptOptions()) {
    Row r;
    r.name = name, r.owner = owner, r.inst = inst;
    r.raw = raw.size() / 3, r.now = now.size() / 3, r.verts = now.size();
    std::vector<OptVert> a = raw, b = now;
    r.was = optimizeVerts(a, o);
    r.is = optimizeVerts(b, o);
    rows.push_back(r);
}

std::string ownerOf(const mk::ModelDef& d) {
    if (d.id.rfind("tree_", 0) == 0 || d.id == "bush" || d.id == "stump") return "perfveg";
    if (!mk::simplified(d)) return "perfcars";
    return "perfaudit";
}

// A Frontier start: the map, the lineup on its pad.
void frontierSim(Sim& sim) {
    sim.setupEmpty();
    sim.world = World(MAP_FRONTIER);
    sim.setup();
}

void auditModels(const World& w) {
    // How many of each design stand in the scene.
    std::map<std::pair<std::string, int>, long long> count;
    for (const Placed& pl : w.models) count[{pl.id, placedVariant(pl)}]++;
    for (const Prop& p : w.fixtures) {
        static const std::map<int, std::string> ids = {{PROP_CABIN, "cabin"}, {PROP_GARAGE, "repair_shed"}, {PROP_TABLE, "picnic_table"}, {PROP_MAPBOARD, "map_board"},
                                                       {PROP_MARKER, "marker_post"}, {PROP_CAIRN, "cairn"}, {PROP_SIGN, "trail_sign"}, {PROP_STUMP, "stump"}};
        auto it = ids.find(p.kind);
        if (it == ids.end()) continue;
        Placed pl;
        pl.id = it->second, pl.p = p.a;
        count[{pl.id, placedVariant(pl)}]++;
    }
    for (const mk::ModelDef& d : mk::catalog())
        for (int v = 0; v < (int)d.variants.size(); v++) {
            const mk::P p = mk::pickedParams(d, v);
            mk::Mesh raw, now;
            mk::simplifyModels() = false;
            mk::build(d, v, p, raw);
            mk::simplifyModels() = true;
            mk::build(d, v, p, now);
            auto it = count.find({d.id, v});
            add("model " + d.id + " " + std::string(1, (char)('A' + v)), ownerOf(d), it == count.end() ? 0 : it->second, soup(raw), soup(now));
        }
}

void auditWorldScenery(const std::string& tag, const World& w, const ScanCover& cover, bool rocks) {
    auto both = [&](const std::function<void(std::vector<Vtx>&)>& f, std::vector<OptVert>& raw, std::vector<OptVert>& now) {
        std::vector<Vtx> a, b;
        mk::simplifyModels() = true, simplifyScenery() = true;
        f(b);  // (the models cached: what an editor drag pays)
        const auto t0 = std::chrono::steady_clock::now();
        b.clear(), f(b);
        lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        simplifyScenery() = false, a.clear();
        const auto t1 = std::chrono::steady_clock::now();
        f(a);
        lastRawMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        simplifyScenery() = true;
        mk::simplifyModels() = false, mk::forgetMeshes(), a.clear();
        f(a);
        mk::simplifyModels() = true, mk::forgetMeshes();
        raw = soupOf(a), now = soupOf(b);
        std::printf("  %s: %.1f ms to build, %.1f ms without meshopt\n", tag.c_str(), lastMs, lastRawMs);
    };
    std::vector<OptVert> raw, now;
    both([&](std::vector<Vtx>& v) { buildBoxMesh(w, v); }, raw, now);
    int boxes = 0;
    for (const Box& b : w.boxes) boxes += !b.hidden && !b.rock;
    if (boxes) add(tag + ": level boxes (" + std::to_string(boxes) + ")", "perfaudit", 1, raw, now, sceneryOptions(w, true));
    both([&](std::vector<Vtx>& v) { buildPropMesh(w, v); }, raw, now);
    add(tag + ": placed models AND fixtures, whole map (" + std::to_string(w.models.size() + w.fixtures.size()) + ")", "perfaudit", 1, raw, now, sceneryOptions(w, true));
    if (!rocks) return;
    both([&](std::vector<Vtx>& v) { buildRockMesh(w, v, cover); }, raw, now);
    add(tag + ": code-made rocks, slabs AND logs, whole map", "perfprops", 1, raw, now, worldOpt());
    both([&](std::vector<Vtx>& v) { buildBushMesh(w, v, cover); }, raw, now);
    add(tag + ": bushes AND reeds, whole map (" + std::to_string(w.bushes.size()) + ")", "perfveg", 1, raw, now, worldOpt());
}

void auditTrees(const World& w, const ScanCover& cover) {
    forest::Set set;
    forest::build(w, cover.pines, cover.snags, set);
    std::vector<long long> n(set.shapes.size(), 0);
    for (const forest::Inst& i : set.inst) n[i.shape]++;
    for (size_t s = 0; s < set.shapes.size(); s++)
        for (int l = 0; l < forest::kLods; l++) {
            const forest::Look& k = set.shapes[s].look;
            const std::vector<OptVert> v = soup(set.shapes[s].lod[l]);
            add(std::string("tree ") + forest::modelId(k.kind) + " design " + std::to_string(k.design) + " seed " + std::to_string(k.seed) + (k.snow ? " snow" : "") +
                    " LOD" + std::to_string(l),
                "perfveg", l == 0 ? n[s] : 0, v, v);
        }
}

void auditScans(const World& w) {
    const std::string dir = props::assetDir();
    if (dir.empty()) return;
    std::map<std::string, long long> used;
    const props::HullLibrary& lib = props::sharedHullLibrary();
    for (const Rock& r : w.rocks)
        if (r.scan >= 0 && r.scan < (int)lib.hulls.size()) used[lib.hulls[r.scan].name]++;
    for (const props::ManifestEntry& e : props::readManifest(dir)) {
        props::Model m;
        if (!props::loadModel(dir + "/" + e.name + ".prop", m)) continue;
        for (size_t l = 0; l < m.lods.size(); l++) {
            std::vector<OptVert> v;  // (the texture coordinates go in the colour: nothing textured differently merges)
            for (size_t s = 0; s < m.lods[l].subs.size(); s++) {
                const props::Sub& sub = m.lods[l].subs[s];
                for (uint16_t i : sub.idx) {
                    const props::Vert& q = sub.v[i];
                    OptVert o;
                    std::memcpy(o.p, q.p, sizeof o.p), std::memcpy(o.n, q.n, sizeof o.n);
                    o.c[0] = q.uv[0], o.c[1] = q.uv[1], o.c[2] = q.t[3], o.c[3] = (float)(s + 1);
                    v.push_back(o);
                }
            }
            auto it = used.find(e.name);
            add("scan " + e.name + " LOD" + std::to_string(l), "perfprops", l == 0 && it != used.end() ? it->second : 0, v, v);
        }
    }
}

void auditVehicles() {
    const auto& cat = vehicleCatalog();
    for (const VehicleSpec& spec : cat) {
        Car c;
        c.build(spec, V3(), 0, V3(0.4, 0.5, 0.6));
        const VisualTemplate& t = visualTemplate(c);
        std::vector<BodyDynamic> dyn;
        std::vector<unsigned> opaque, glass;
        evalBody(c, t, dyn, opaque, glass);
        std::vector<OptVert> body;
        for (unsigned i : opaque) {
            OptVert o{};
            std::memcpy(o.p, dyn[i].p, sizeof o.p), std::memcpy(o.n, dyn[i].n, sizeof o.n);
            o.c[0] = i < t.rvStatic.size() ? t.rvStatic[i].region : 0, o.c[3] = 4;
            body.push_back(o);
        }
        add(std::string("vehicle ") + spec.name + ": body shell", "perfcars", 1, body, body);
        std::vector<Vtx> parts, gear, rails;
        evalParts(c, t, parts);
        buildRunningGear(c, gear);
        buildFrameRails(c, rails);
        add(std::string("vehicle ") + spec.name + ": parts on the body", "perfcars", 1, soupOf(parts), soupOf(parts));
        add(std::string("vehicle ") + spec.name + ": running gear", "perfcars", 1, soupOf(gear), soupOf(gear));
        add(std::string("vehicle ") + spec.name + ": frame rails", "perfcars", 1, soupOf(rails), soupOf(rails));
    }
}

void auditRenderer(Renderer& r, const World& w, bool terrain) {
    for (int s = 0; s < WHEEL_STYLES; s++) {
        const std::vector<OptVert> wh = vtxOf(r.wheelVbo[s]);
        add("wheel style " + std::to_string(s) + " (rim)", "perfcars", 4, wh, wh);
        for (int t = 0; t < TIRE_TYPES; t++) {
            const std::vector<OptVert> ti = vtxOf(r.tireVbo[s][t]);
            add("wheel style " + std::to_string(s) + " tire tread " + std::to_string(t), "perfcars", t == 0 ? 4 : 0, ti, ti);
        }
    }
    const std::vector<OptVert> ch = vtxOf(r.chainVbo), ca = vtxOf(r.caliperVbo);
    add("snow chains", "perfcars", 0, ch, ch);
    add("brake caliper", "perfcars", 4, ca, ca);
    const std::vector<OptVert> wa = vtxOf(r.waterVbo);
    if (!wa.empty()) add("frontier: water (" + std::to_string(w.waters.size()) + " lakes, rivers on the grid)", "perfframe", 1, wa, wa, worldOpt());
    if (!terrain) return;
    // The terrain at full detail (LOD 0 of every chunk), and the backdrop round it.
    const std::vector<OptVert> tv = vtxOf(r.terrainVbo);
    auto it = uploads.find(r.terrainIbo);
    if (tv.empty() || it == uploads.end()) return;
    const unsigned* idx = (const unsigned*)it->second.data();
    std::vector<OptVert> full, backdrop;
    for (const auto& c : r.terrainChunks) {
        const bool far = c.count[0] == c.count[3];  // (backdrop bins: one LOD)
        for (unsigned k = c.first[0]; k < c.first[0] + c.count[0]; k++) (far ? backdrop : full).push_back(tv[idx[k]]);
    }
    add("frontier: terrain LOD0, whole map", "perfframe", 1, full, full, worldOpt());
    add("frontier: backdrop mountains", "perfframe", 1, backdrop, backdrop, worldOpt());
}

// Every triangle a Frontier start view sends, shadows included.
long long frameTris(Renderer& r, const Sim& sim, long long& calls) {
    View view;
    const Car& car = sim.cars[sim.player];
    const V3 c = car.frame().com, f(std::cos(sim.world.spawnYaw), 0, -std::sin(sim.world.spawnYaw));
    view.eye = c - f * 8 + V3(0, 3.2, 0), view.target = c + f * 6 + V3(0, 0.8, 0), view.up = V3(0, 1, 0);
    view.width = 1280, view.height = 720;
    view.focus = c, view.hasFocus = true, view.fadeNear = 1;
    r.daylight = computeDaylight(10);
    progName = {{r.progLit, "lit (dynamic: parts, figures, models in hand)"}, {r.progDepth, "lit (dynamic)"}, {r.progTerrain, "terrain"}, {r.progTree, "trees (instanced)"},
                {r.progTreeDepth, "trees (instanced)"}, {r.progGrass, "grass"}, {r.progWater, "water"}, {r.progSky, "sky"}, {r.progCar, "car bodies"},
                {r.progFx, "effects"}, {r.props.progLit, "scanned props"}, {r.props.progDepth, "scanned props"}, {r.props.progBB, "prop billboards"},
                {r.props.progBBDepth, "prop billboards"}};
    vaoName = {{r.sceneryVao, "scenery (rocks, placed models, fixtures)"}, {r.boxVao, "level boxes"}, {r.bushVao, "bushes"}, {r.terrainVao, "terrain"},
               {r.waterVao, "water"}, {r.dynVao, "dynamic (car parts, running gear, models in hand)"}, {r.figVao, "people"}, {r.chainVao, "snow chains"},
               {r.caliperVao, "wheels"}};
    for (int s = 0; s < WHEEL_STYLES; s++) {
        vaoName[r.wheelVao[s]] = "wheels";
        for (int t = 0; t < TIRE_TYPES; t++) vaoName[r.tireVao[s][t]] = "wheels";
    }
    vaoName.erase(0), progName.erase(0);
    shadowFbos = {r.shadowFbo[0], r.shadowFbo[1]};
    long long t = 0;
    for (int k = 0; k < 4; k++) {  // (the last of a few: LODs and caches settled)
        drawnTris = 0, drawCalls = 0, byWhat.clear();
        r.drawScene(sim, view, DEBUG_OFF, nullptr);
        t = drawnTris, calls = drawCalls;
    }
    return t;
}

std::string fmt(long long n) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld", n);
    return b;
}

void table(FILE* f, const char* title, bool after, size_t top) {
    std::vector<const Row*> s;
    for (const Row& r : rows) s.push_back(&r);
    std::stable_sort(s.begin(), s.end(), [&](const Row* a, const Row* b) { return (after ? a->after() : a->before()) > (after ? b->after() : b->before()); });
    std::fprintf(f, "\n%s\n", title);
    std::fprintf(f, "%4s %-58s %-9s %6s %8s %8s %8s %7s %7s %7s %7s %7s %8s %11s\n", "#", "mesh", "owner", "copies", "written", "now", "min", "verts", "dupV", "degen",
                 "hidden", "coplan", "waste", "waste*cop");
    for (size_t i = 0; i < s.size() && i < top; i++) {
        const Row& r = *s[i];
        const MeshOptStats& m = after ? r.is : r.was;
        std::fprintf(f, "%4zu %-58.58s %-9s %6lld %8zu %8zu %8zu %7zu %7zu %7zu %7zu %7zu %8zu %11lld\n", i + 1, r.name.c_str(), r.owner.c_str(), r.inst, r.raw, r.now,
                     m.trianglesOut, after ? r.verts : r.raw * 3, m.verticesIn - m.uniqueVertices, m.degenerate + m.repeated, m.hidden + m.buried, m.coplanarSaved,
                     m.wasted(), after ? r.after() : r.before());
    }
}

void writePPM(const std::string& path, const std::vector<unsigned char>& px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) std::fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);  // (GL rows are bottom-up)
    std::fclose(f);
}

// --shots DIR: every simplified model in the model yard, drawn as written and as simplified from the same eye: how many
// pixels differ (and both pictures, and where they differ, for the worst and a few chosen ones).
// (frontier: its start, and the first of each model placed on it; else the proving ground's model yard, every one.)
int shots(Renderer& r, const std::string& dir, bool frontier) {
    std::system(("mkdir -p '" + dir + "'").c_str());
    const std::string tag = frontier ? "frontier_" : "yard_";
    Sim sim;
    if (frontier) frontierSim(sim);
    else {
        World::withShowcase = true;
        sim.setupEmpty();
        sim.world = World(MAP_PROVING_GROUND);
        World::withShowcase = false;
        sim.addCar(vehicleCatalog()[0], V3(sim.world.spawn.x, 0.05, sim.world.spawn.z), 0, V3(0.4, 0.5, 0.6));
    }
    struct Shot {
        std::string id;
        View view;
        std::vector<unsigned char> a, b;
    };
    std::vector<Shot> list;
    const int W = 640, H = 400;
    std::set<std::string> seen;
    if (frontier) {
        Shot s;
        s.id = "start";
        const V3 c = sim.cars[sim.player].frame().com, f(std::cos(sim.world.spawnYaw), 0, -std::sin(sim.world.spawnYaw));
        s.view.eye = c - f * 8 + V3(0, 3.2, 0), s.view.target = c + f * 6 + V3(0, 0.8, 0), s.view.up = V3(0, 1, 0);
        s.view.width = W, s.view.height = H, s.view.time = 3;
        list.push_back(s);
        std::set<int> kinds;  // (the first of each fixture)
        for (const Prop& p : sim.world.fixtures) {
            if (!kinds.insert(p.kind).second) continue;
            const double size = p.kind == PROP_CABIN || p.kind == PROP_GARAGE ? 7 : p.kind == PROP_FENCE ? 4 : 1.8;
            const V3 f(std::cos(p.yaw + 0.6), 0, -std::sin(p.yaw + 0.6));
            Shot k;
            k.id = "fixture" + std::to_string(p.kind);
            k.view.eye = p.a + f * (size * 1.5 + 0.8) + V3(0, size * 0.55 + 0.5, 0), k.view.target = p.a + V3(0, size * 0.3, 0), k.view.up = V3(0, 1, 0);
            k.view.width = W, k.view.height = H, k.view.time = 3;
            list.push_back(k);
        }
    }
    for (const Placed& pl : sim.world.models) {
        const mk::ModelDef* d = mk::findModel(pl.id);
        if (!d || !mk::simplified(*d) || (frontier && !seen.insert(pl.id).second)) continue;
        Shot s;
        s.id = pl.id + "_" + std::to_string(list.size());
        const double size = std::max(0.6, d->size);
        const V3 f(std::cos(pl.yaw + 0.6), 0, -std::sin(pl.yaw + 0.6));  // (three-quarters on, from a little above)
        s.view.eye = pl.p + f * (size * 1.5 + 0.8) + V3(0, size * 0.55 + 0.5, 0), s.view.target = pl.p + V3(0, size * 0.3, 0), s.view.up = V3(0, 1, 0);
        s.view.width = W, s.view.height = H, s.view.time = 3;
        list.push_back(s);
    }
    r.daylight = computeDaylight(10);
    auto draw = [&](bool simplify, bool second) {
        mk::simplifyModels() = simplify, simplifyScenery() = simplify, mk::forgetMeshes();
        r.setWorld(sim.world);
        for (Shot& s : list) {
            for (int k = 0; k < 3; k++) r.drawScene(sim, s.view, DEBUG_OFF, nullptr);  // (settled)
            (second ? s.b : s.a) = r.readPixels(W, H);
        }
    };
    // (Twice as written first: what differs then is the renderer's own noise, not the meshes.)
    draw(false, false);
    std::vector<std::vector<unsigned char>> again;
    {
        draw(false, true);
        for (Shot& s : list) again.push_back(s.b);
    }
    draw(true, true);
    int worst = 0;
    long long total = 0, noise = 0;
    std::vector<std::pair<int, size_t>> order;
    FILE* f = std::fopen((dir + "/" + tag + "shots.txt").c_str(), "w");
    for (size_t i = 0; i < list.size(); i++) {
        Shot& s = list[i];
        int n = 0, nn = 0, peak = 0;
        std::vector<unsigned char> diff(s.a.size(), 0);
        for (size_t p = 0; p + 2 < s.a.size(); p += 3) {
            int d = 0, e = 0;
            for (int c = 0; c < 3; c++) d = std::max(d, std::abs(s.a[p + c] - s.b[p + c])), e = std::max(e, std::abs(s.a[p + c] - again[i][p + c]));
            if (d > 2) n++, diff[p] = 255, diff[p + 1] = (unsigned char)std::min(255, d * 8);
            else diff[p] = diff[p + 1] = diff[p + 2] = s.a[p] / 4;
            nn += e > 2;
            peak = std::max(peak, d);
        }
        total += n, noise += nn, worst = std::max(worst, n);
        order.push_back({n, i});
        if (f) std::fprintf(f, "%-20s %6d pixels differ (renderer noise %d), largest %d/255\n", s.id.c_str(), n, nn, peak);
        writePPM(dir + "/" + tag + s.id + "_diff.ppm", diff, W, H);
        writePPM(dir + "/" + tag + s.id + "_written.ppm", s.a, W, H);
        writePPM(dir + "/" + tag + s.id + "_now.ppm", s.b, W, H);
    }
    if (f) std::fclose(f);
    std::printf("meshaudit: %zu views, %lld pixels differ in all (renderer noise alone %lld), worst view %d; see %s/shots.txt\n", list.size(), total, noise, worst,
                dir.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string out = "build/review/meshaudit.txt";
    bool fast = false;
    std::string shotDir;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--fast") fast = true;
        else if (a == "--shots" && i + 1 < argc) shotDir = argv[++i];
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return std::puts(SDL_GetError()), 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("mesh audit", 0, 0, 1280, 720, SDL_WINDOW_OPENGL);
    if (!win) return std::puts(SDL_GetError()), 1;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) return std::puts(SDL_GetError()), 1;
    if (const char* missing = gl::load(SDL_GL_GetProcAddress)) return std::printf("no %s\n", missing), 1;
    realBind = gl::BindBuffer, gl::BindBuffer = hookBind;
    realData = gl::BufferData, gl::BufferData = hookData;
    realDrawArrays = gl::DrawArrays, gl::DrawArrays = hookDA;
    realUse = gl::UseProgram, gl::UseProgram = hookUse;
    realVao = gl::BindVertexArray, gl::BindVertexArray = hookVao;
    realFbo = gl::BindFramebuffer, gl::BindFramebuffer = hookFbo;
    realDrawElements = gl::DrawElements, gl::DrawElements = hookDE;
    realDrawArraysInst = gl::DrawArraysInstanced, gl::DrawArraysInstanced = hookDAI;
    realDrawElementsInst = gl::DrawElementsInstanced, gl::DrawElementsInstanced = hookDEI;

    props::setActiveRockScans(&props::sharedHullLibrary());
    if (!shotDir.empty()) {
        Sim sim;
        sim.setupEmpty();
        Renderer r;
        std::string err;
        if (!r.init(sim.world, err)) return std::printf("renderer: %s\n", err.c_str()), 1;
        return shots(r, shotDir, false) | shots(r, shotDir, true);
    }
    std::puts("meshaudit: building the Frontier");
    Sim sim;
    frontierSim(sim);
    Renderer r;
    std::string err;
    // As written first (a frame's triangles), then as the game simplifies them.
    mk::simplifyModels() = false, simplifyScenery() = false, mk::forgetMeshes();
    if (!r.init(sim.world, err)) return std::printf("renderer: %s\n", err.c_str()), 1;
    long long callsRaw = 0, callsNow = 0;
    const long long viewRaw = frameTris(r, sim, callsRaw);
    const std::map<std::string, long long> whatRaw = byWhat;
    const Renderer::GeometryStats gsRaw = r.geometryStats();
    mk::simplifyModels() = true, simplifyScenery() = true, mk::forgetMeshes();
    r.setWorld(sim.world);
    const long long viewNow = frameTris(r, sim, callsNow);
    const Renderer::GeometryStats gsNow = r.geometryStats();
    std::printf("meshaudit: Frontier start view %lld -> %lld triangles\n", viewRaw, viewNow);

    std::puts("meshaudit: models");
    auditModels(sim.world);
    std::puts("meshaudit: scenery");
    auditWorldScenery("frontier", sim.world, r.scanCover_, true);
    auditTrees(sim.world, r.scanCover_);
    auditScans(sim.world);
    std::puts("meshaudit: vehicles");
    auditVehicles();
    std::puts("meshaudit: renderer meshes");
    auditRenderer(r, sim.world, !fast);
    if (!fast) {
        std::puts("meshaudit: the proving ground");
        World::withShowcase = true;
        {
            const World pg(MAP_PROVING_GROUND);
            auditWorldScenery("proving ground", pg, ScanCover(), false);
        }
        World::withShowcase = false;
        std::puts("meshaudit: levels");
        for (const Level& lv : builtinLevels()) {
            const World w = levelWorld(lv);
            auditWorldScenery("level " + lv.name, w, ScanCover(), false);
        }
    }
    add("sky (one full-screen triangle)", "perfframe", 1, {}, {});
    add("HUD text AND panels (2 triangles a glyph or rect, made per frame)", "perfframe", 1, {}, {});

    std::string dir = out.substr(0, out.find_last_of('/'));
    if (dir != out) std::system(("mkdir -p '" + dir + "'").c_str());
    FILE* f = std::fopen(out.c_str(), "w");
    if (!f) return std::printf("can't write %s\n", out.c_str()), 1;
    MeshOptStats tw, tn;
    size_t raw = 0, now = 0;
    for (const Row& x : rows) tw += x.was, tn += x.is, raw += x.raw, now += x.now;
    std::fprintf(f, "Mesh audit (tools/meshaudit.cpp): every mesh the game builds, %zu of them.\n", rows.size());
    std::fprintf(f, "written: triangles as the code makes them; now: as the game builds them (meshopt.h); min: the fewest meshopt gets from them\n");
    std::fprintf(f, "(merging flat regions, dropping degenerate, repeated, hidden and buried faces). dupV: vertices an indexed copy wouldn't repeat.\n");
    std::fprintf(f, "copies: how many a Frontier start has (0: not placed there; counted as 1). Sorted by waste x copies.\n\n");
    std::fprintf(f, "Frontier start view, every draw (shadows too): %s triangles in %lld draws as written, %s in %lld now.\n", fmt(viewRaw).c_str(), callsRaw,
                 fmt(viewNow).c_str(), callsNow);
    std::fprintf(f, "  (scenery batches %u -> %u vertices, terrain %u triangles of %u)\n", gsRaw.sceneryVertices, gsNow.sceneryVertices, gsNow.terrainTriangles,
                 gsNow.terrainFullTriangles);
    {
        std::vector<std::pair<long long, std::string>> w;
        for (const auto& kv : byWhat) w.push_back({kv.second, kv.first});
        std::sort(w.rbegin(), w.rend());
        for (const auto& x : w) {
            auto it = whatRaw.find(x.second);
            std::fprintf(f, "  %10lld  (written %10lld)  %s\n", x.first, it == whatRaw.end() ? 0LL : it->second, x.second.c_str());
        }
    }
    std::fprintf(f, "All meshes, once each: %zu triangles written, %zu now; wasted %zu written, %zu now.\n", raw, now, tw.wasted(), tn.wasted());
    table(f, "WORST NOW (what the game builds)", true, 60);
    table(f, "WORST AS WRITTEN (before meshopt)", false, 60);
    table(f, "EVERYTHING (now)", true, rows.size());
    std::fclose(f);
    std::printf("meshaudit: wrote %s\n", out.c_str());
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
