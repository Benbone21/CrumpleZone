#include "props.h"
#include "loadprof.h"
#include "meshcache.h"
#include "platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace props {

namespace {
const char kMagic[4] = {'C', 'Z', 'P', '2'};

struct File {
    FILE* f = nullptr;
    explicit File(const std::string& path, const char* mode) { f = std::fopen(path.c_str(), mode); }
    ~File() {
        if (f) std::fclose(f);
    }
    bool ok() const { return f != nullptr; }
    template <class T>
    void put(const T& v) {
        std::fwrite(&v, sizeof(T), 1, f);
    }
    template <class T>
    bool get(T& v) {
        return std::fread(&v, sizeof(T), 1, f) == 1;
    }
    void putStr(const std::string& s) {
        const uint16_t n = (uint16_t)s.size();
        put(n);
        std::fwrite(s.data(), 1, n, f);
    }
    bool getStr(std::string& s) {
        uint16_t n = 0;
        if (!get(n)) return false;
        s.resize(n);
        return n == 0 || std::fread(&s[0], 1, n, f) == n;
    }
};

int8_t q8(float v) { return (int8_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 127.0f); }
}  // namespace

bool saveModel(const std::string& path, const Model& m) {
    File out(path, "wb");
    if (!out.ok()) return false;
    // Quantisation bounds: every position of every LOD.
    float qmin[3] = {1e30f, 1e30f, 1e30f}, qmax[3] = {-1e30f, -1e30f, -1e30f};
    for (const Lod& l : m.lods)
        for (const Sub& s : l.subs)
            for (const Vert& v : s.v)
                for (int k = 0; k < 3; k++) qmin[k] = std::fmin(qmin[k], v.p[k]), qmax[k] = std::fmax(qmax[k], v.p[k]);
    for (int k = 0; k < 3; k++)
        if (qmax[k] - qmin[k] < 1e-6f) qmax[k] = qmin[k] + 1e-6f;
    std::fwrite(kMagic, 1, 4, out.f);
    out.putStr(m.name);
    out.put((uint8_t)m.kind);
    for (int k = 0; k < 3; k++) out.put(m.bmin[k]);
    for (int k = 0; k < 3; k++) out.put(m.bmax[k]);
    for (int k = 0; k < 3; k++) out.put(qmin[k]);
    for (int k = 0; k < 3; k++) out.put(qmax[k]);
    out.put((uint8_t)m.mats.size());
    for (const Material& mt : m.mats) {
        out.put((uint8_t)mt.alphaMode);
        out.put((uint8_t)((mt.doubleSided ? 1 : 0) | (mt.foliage ? 2 : 0)));
        out.put(mt.wind);
        out.put(mt.cutoff);
        for (int k = 0; k < 3; k++) out.putStr(mt.tex[k]);
    }
    out.put((uint8_t)m.lods.size());
    for (const Lod& l : m.lods) {
        out.put(l.maxDist);
        out.put((uint8_t)l.subs.size());
        for (const Sub& s : l.subs) {
            out.put((uint8_t)s.mat);
            out.put((uint32_t)s.v.size());
            out.put((uint32_t)s.idx.size());
            for (const Vert& v : s.v) {
                for (int k = 0; k < 3; k++) {
                    const double t = (v.p[k] - qmin[k]) / (qmax[k] - qmin[k]);
                    out.put((int16_t)std::lround(t * 65535.0 - 32768.0));
                }
                for (int k = 0; k < 3; k++) out.put(q8(v.n[k]));
                for (int k = 0; k < 2; k++) {
                    const double t = std::fmax(-7.99, std::fmin(7.99, (double)v.uv[k]));
                    out.put((int16_t)std::lround(t * 4096.0));
                }
                for (int k = 0; k < 4; k++) out.put(q8(v.t[k]));
            }
            std::fwrite(s.idx.data(), sizeof(uint16_t), s.idx.size(), out.f);
        }
    }
    return true;
}

bool loadModel(const std::string& path, Model& m) {
    File in(path, "rb");
    if (!in.ok()) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, in.f) != 4 || std::memcmp(magic, kMagic, 4) != 0) return false;
    m = Model();
    uint8_t u8 = 0;
    float qmin[3], qmax[3];
    if (!in.getStr(m.name) || !in.get(u8)) return false;
    m.kind = u8;
    for (int k = 0; k < 3; k++) in.get(m.bmin[k]);
    for (int k = 0; k < 3; k++) in.get(m.bmax[k]);
    for (int k = 0; k < 3; k++) in.get(qmin[k]);
    for (int k = 0; k < 3; k++) in.get(qmax[k]);
    uint8_t nMat = 0, nLod = 0;
    if (!in.get(nMat)) return false;
    m.mats.resize(nMat);
    for (Material& mt : m.mats) {
        uint8_t a = 0, fl = 0;
        in.get(a);
        in.get(fl);
        mt.alphaMode = a;
        mt.doubleSided = fl & 1;
        mt.foliage = (fl & 2) != 0;
        in.get(mt.wind);
        in.get(mt.cutoff);
        for (int k = 0; k < 3; k++)
            if (!in.getStr(mt.tex[k])) return false;
    }
    if (!in.get(nLod)) return false;
    m.lods.resize(nLod);
    for (Lod& l : m.lods) {
        uint8_t nSub = 0;
        in.get(l.maxDist);
        if (!in.get(nSub)) return false;
        l.subs.resize(nSub);
        for (Sub& s : l.subs) {
            uint8_t mat = 0;
            uint32_t nv = 0, ni = 0;
            in.get(mat);
            in.get(nv);
            if (!in.get(ni) || nv > 65536 || ni > (1u << 24)) return false;
            s.mat = mat;
            s.v.resize(nv);
            for (Vert& v : s.v) {
                for (int k = 0; k < 3; k++) {
                    int16_t q = 0;
                    in.get(q);
                    v.p[k] = qmin[k] + (float)((q + 32768.0) / 65535.0) * (qmax[k] - qmin[k]);
                }
                for (int k = 0; k < 3; k++) {
                    int8_t q = 0;
                    in.get(q);
                    v.n[k] = q / 127.0f;
                }
                for (int k = 0; k < 2; k++) {
                    int16_t q = 0;
                    in.get(q);
                    v.uv[k] = q / 4096.0f;
                }
                for (int k = 0; k < 4; k++) {
                    int8_t q = 0;
                    in.get(q);
                    v.t[k] = q / 127.0f;
                }
            }
            s.idx.resize(ni);
            if (ni && std::fread(s.idx.data(), sizeof(uint16_t), ni, in.f) != ni) return false;
        }
    }
    return true;
}

std::string assetDir() {
    static const std::string found = [] {  // (made once, thread-safely: worlds are built on several threads)
        if (const char* env = std::getenv("CZ_PROPS_DIR")) return std::string(env);  // (testing: another bake)
        std::vector<std::string> bases = {"."};
        if (const std::string exe = exeDir(); !exe.empty()) bases.push_back(exe), bases.push_back(exe + "/../Resources");  // (../Resources: inside a macOS .app)
        const char* rel[] = {"/assets/props", "/../assets/props", "/../../assets/props", "/../../../assets/props"};
        for (const std::string& b : bases)
            for (const char* r : rel) {
                const std::string dir = b + r;
                if (FILE* f = std::fopen((dir + "/rocks.hull").c_str(), "rb")) {
                    std::fclose(f);
                    return dir;
                }
            }
        return std::string();
    }();
    return found;
}

bool HullLibrary::load(const std::string& dir) {
    LOADPROF("hulls.load");
    hulls.clear();
    FILE* f = std::fopen((dir + "/rocks.hull").c_str(), "rb");
    if (!f) return false;
    int count = 0;
    if (std::fscanf(f, "CZHULL 1 %d", &count) != 1 || count <= 0 || count > 4096) {
        std::fclose(f);
        return false;
    }
    for (int i = 0; i < count; i++) {
        RockHull h;
        char name[128];
        int nv = 0, nf = 0;
        if (std::fscanf(f, " %127s %d %d", name, &nv, &nf) != 3 || nv < 4 || nv > 200 || nf < 4 || nf > 400) {
            hulls.clear();
            std::fclose(f);
            return false;
        }
        h.name = name;
        for (int v = 0; v < nv; v++) {
            double x, y, z;
            if (std::fscanf(f, "%lf %lf %lf", &x, &y, &z) != 3) {
                hulls.clear();
                std::fclose(f);
                return false;
            }
            h.v.push_back(V3(x, y, z));
        }
        for (int t = 0; t < nf; t++) {
            int a, b, c;
            if (std::fscanf(f, "%d %d %d", &a, &b, &c) != 3 || a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) {
                hulls.clear();
                std::fclose(f);
                return false;
            }
            h.f.push_back({a, b, c});
        }
        for (int k = 0; k < 3; k++) h.bmin[k] = 1e30f, h.bmax[k] = -1e30f;
        for (const V3& p : h.v) {
            const double c3[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; k++) h.bmin[k] = std::fmin(h.bmin[k], (float)c3[k]), h.bmax[k] = std::fmax(h.bmax[k], (float)c3[k]);
        }
        hulls.push_back(std::move(h));
    }
    std::fclose(f);
    return true;
}

bool HullLibrary::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "CZHULL 1 %d\n", (int)hulls.size());
    for (const RockHull& h : hulls) {
        std::fprintf(f, "%s %d %d\n", h.name.c_str(), (int)h.v.size(), (int)h.f.size());
        for (const V3& p : h.v) std::fprintf(f, "%.5f %.5f %.5f\n", p.x, p.y, p.z);
        for (const auto& t : h.f) std::fprintf(f, "%d %d %d\n", t[0], t[1], t[2]);
    }
    std::fclose(f);
    return true;
}

// ---- Hull building ---------------------------------------------------------------------
namespace {
struct HullFace {
    std::array<int, 3> i;
    V3 n;
    double d;
    bool alive;
};

// Incremental 3D hull over a point set (only the points added take part).
struct Hull {
    const std::vector<V3>* p = nullptr;
    std::vector<HullFace> faces;
    std::set<int> used;  // (corners(): kept up to date only when asked for)
    bool usedStale = false;
    int dead = 0;  // faces no longer on the hull, still in `faces`
    V3 centroid;
    bool init(const std::vector<V3>& pts) {
        p = &pts;
        const int n = (int)pts.size();
        if (n < 4) return false;
        int t[4] = {0, 0, 0, 0};
        for (int i = 0; i < n; i++)
            if (pts[i].x < pts[t[0]].x) t[0] = i;
        for (int i = 0; i < n; i++)
            if (len(pts[i] - pts[t[0]]) > len(pts[t[1]] - pts[t[0]])) t[1] = i;
        const V3 axis = norm(pts[t[1]] - pts[t[0]]);
        auto lineDist = [&](int i) {
            const V3 dd = pts[i] - pts[t[0]];
            return len(dd - axis * dot(dd, axis));
        };
        for (int i = 0; i < n; i++)
            if (lineDist(i) > lineDist(t[2])) t[2] = i;
        const V3 pn = norm(cross(pts[t[1]] - pts[t[0]], pts[t[2]] - pts[t[0]]));
        for (int i = 0; i < n; i++)
            if (std::fabs(dot(pts[i] - pts[t[0]], pn)) > std::fabs(dot(pts[t[3]] - pts[t[0]], pn))) t[3] = i;
        if (std::fabs(dot(pts[t[3]] - pts[t[0]], pn)) < 1e-9) return false;  // flat
        centroid = (pts[t[0]] + pts[t[1]] + pts[t[2]] + pts[t[3]]) * 0.25;
        make(t[0], t[1], t[2]), make(t[0], t[1], t[3]), make(t[0], t[2], t[3]), make(t[1], t[2], t[3]);
        used = {t[0], t[1], t[2], t[3]};
        return true;
    }
    void make(int a, int b, int c) {
        const std::vector<V3>& pts = *p;
        V3 nrm = norm(cross(pts[b] - pts[a], pts[c] - pts[a]));
        if (dot(nrm, pts[a] - centroid) < 0) {
            std::swap(b, c);
            nrm = -nrm;
        }
        faces.push_back({{a, b, c}, nrm, dot(nrm, pts[a]), true});
    }
    void add(int k) {
        const std::vector<V3>& pts = *p;
        std::vector<std::pair<int, int>> edges;
        for (HullFace& f : faces)
            if (f.alive && dot(f.n, pts[k]) - f.d > 1e-9) {
                f.alive = false;
                for (int e = 0; e < 3; e++) edges.push_back({f.i[e], f.i[(e + 1) % 3]});
            }
        if (edges.empty()) return;
        for (const auto& e : edges) {
            bool shared = false;
            for (const auto& o : edges) shared |= o.first == e.second && o.second == e.first;
            if (!shared) make(e.first, e.second, k);
        }
        dead += (int)edges.size() / 3;  // (one per face that went: 3 edges each)
        if (dead > 64 && dead * 2 > (int)faces.size()) {  // (drop the dead faces, in order, once they are half of them: nothing else changes)
            std::vector<HullFace> keep;
            keep.reserve(faces.size() - dead);
            for (const HullFace& f : faces)
                if (f.alive) keep.push_back(f);
            faces.swap(keep);
            dead = 0;
        }
        usedStale = true;
    }
    // The corners of the hull so far.
    const std::set<int>& corners() {
        if (usedStale) {
            used.clear();
            for (const HullFace& f : faces)
                if (f.alive) used.insert({f.i[0], f.i[1], f.i[2]});
            usedStale = false;
        }
        return used;
    }
    double outside(const V3& q) const {
        double m = -1e30;
        for (const HullFace& f : faces)
            if (f.alive) m = std::max(m, dot(f.n, q) - f.d);
        return m;
    }
};
}  // namespace

RockHull buildRockHull(const std::string& name, const std::vector<V3>& pts, int maxVerts, double* worstMiss, double pushMax) {
    RockHull out;
    out.name = name;
    // The exact hull first: its corners are the only candidates.
    Hull full;
    if (!full.init(pts)) return out;
    for (int k = 0; k < (int)pts.size(); k++) full.add(k);
    std::vector<V3> cand;
    for (int i : full.corners()) cand.push_back(pts[i]);
    // Then grow a small one from the biggest misses.
    Hull h;
    h.init(cand);
    if ((int)cand.size() <= maxVerts)  // (every corner fits: no need to hunt for the misses)
        for (int k = 0; k < (int)cand.size(); k++) h.add(k);
    while ((int)h.corners().size() < maxVerts) {
        int best = -1;
        double bd = 1e-4;  // 0.1 mm
        for (int k = 0; k < (int)cand.size(); k++) {
            const double d = h.outside(cand[k]);
            if (d > bd) bd = d, best = k;
        }
        if (best < 0) break;
        h.add(best);  // (one more corner at most: some may drop inside)
    }
    double err = 0;
    for (const V3& q : pts) err = std::max(err, h.outside(q));
    // Push the corners out a little so the scan doesn't poke through, then re-hull them
    // (they stay extreme; the windings need redoing).
    const std::vector<int> ids(h.corners().begin(), h.corners().end());
    std::vector<V3> corner;
    for (int id : ids) {
        V3 nsum;
        for (const HullFace& f : h.faces)
            if (f.alive && (f.i[0] == id || f.i[1] == id || f.i[2] == id)) nsum += f.n;
        const double push = std::min(err * 0.5, pushMax);
        corner.push_back(push > 0 ? cand[id] + norm(nsum) * push : cand[id]);  // (none: the corners are the points themselves, exactly)
    }
    Hull h2;
    if (!h2.init(corner)) return out;
    for (int k = 0; k < (int)corner.size(); k++) h2.add(k);
    std::map<int, int> r2;
    for (int i : h2.corners()) {
        r2[i] = (int)out.v.size();
        out.v.push_back(corner[i]);
    }
    for (const HullFace& f : h2.faces)
        if (f.alive) out.f.push_back({r2[f.i[0]], r2[f.i[1]], r2[f.i[2]]});
    for (int k = 0; k < 3; k++) out.bmin[k] = 1e30f, out.bmax[k] = -1e30f;
    for (const V3& p : out.v) {
        const double c3[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; k++) out.bmin[k] = std::fmin(out.bmin[k], (float)c3[k]), out.bmax[k] = std::fmax(out.bmax[k], (float)c3[k]);
    }
    if (worstMiss) *worstMiss = err;
    return out;
}

// ---- The active hull library ------------------------------------------------------------
namespace {
thread_local const HullLibrary* tActive = nullptr;
}
const HullLibrary* activeRockScans() { return tActive && tActive->loaded() ? tActive : nullptr; }
void setActiveRockScans(const HullLibrary* lib) { tActive = lib; }

const HullLibrary& sharedHullLibrary() {
    static HullLibrary lib;
    static std::once_flag once;
    std::call_once(once, [] {
        const std::string dir = assetDir();
        if (!dir.empty()) lib.load(dir);
    });
    return lib;
}

std::vector<ManifestEntry> readManifest(const std::string& dir) {
    std::vector<ManifestEntry> out;
    FILE* f = std::fopen((dir + "/props.txt").c_str(), "r");
    if (!f) return out;
    char name[128];
    int kind = 0;
    while (std::fscanf(f, " %127s %d", name, &kind) == 2) out.push_back({name, kind});
    std::fclose(f);
    return out;
}

// ---- Drawing helpers ---------------------------------------------------------------------
void frustumPlanes(const M4& m, float out[6][4]) {
    const float* a = m.m;
    for (int i = 0; i < 3; i++)
        for (int s = 0; s < 2; s++) {
            float* p = out[i * 2 + s];
            const float sg = s ? -1.0f : 1.0f;
            for (int k = 0; k < 4; k++) p[k] = a[k * 4 + 3] + sg * a[k * 4 + i];
            const float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (l > 0)
                for (int k = 0; k < 4; k++) p[k] /= l;
        }
}
bool sphereInFrustum(const float pl[6][4], const V3& c, double r) {
    for (int i = 0; i < 6; i++)
        if (pl[i][0] * c.x + pl[i][1] * c.y + pl[i][2] * c.z + pl[i][3] < -r) return false;
    return true;
}
bool boxInFrustum(const float pl[6][4], const V3& mn, const V3& mx) {
    for (int i = 0; i < 6; i++) {
        const V3 p(pl[i][0] >= 0 ? mx.x : mn.x, pl[i][1] >= 0 ? mx.y : mn.y, pl[i][2] >= 0 ? mx.z : mn.z);
        if (pl[i][0] * p.x + pl[i][1] * p.y + pl[i][2] * p.z + pl[i][3] < 0) return false;
    }
    return true;
}

int solidLod(const int lodTris[3], double size, double dist, double detail) {
    const double s = size / std::max(dist, 0.1);
    if (s < kLodCullSize / detail) return -1;
    // About sqrt(tris / 2) triangles across the side it shows: the coarsest LOD whose edges are short enough.
    const double px = s * kLodRefPxPerRad, edge = kLodEdgePx / detail;
    for (int l = 2; l > 0; l--)
        if (px <= edge * std::sqrt(std::max(1, lodTris[l]) / 2.0)) return l;
    return 0;
}

}  // namespace props

// ---- Meshes and convex pieces -----------------------------------------------------------------
namespace props {
const std::vector<V3>& rockMeshTris(const std::string& name) {
    static std::mutex mu;
    static std::map<std::string, std::vector<V3>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    std::vector<V3>& out = cache[name];
    Model m;
    if (!loadModel(assetDir() + "/" + name + ".prop", m) || m.lods.empty()) return out;
    for (const Sub& s : m.lods[0].subs)
        for (size_t i = 0; i + 2 < s.idx.size(); i += 3)
            for (int k = 0; k < 3; k++) out.push_back(V3(s.v[s.idx[i + k]].p[0], s.v[s.idx[i + k]].p[1], s.v[s.idx[i + k]].p[2]));
    return out;
}


namespace {
// The split is best-first, and each cut depends only on the piece cut and the way across it, so a scan's cuts form a tree whatever
// the rock is placed as. An affine map keeps a convex piece convex, so the tree is built in the scan's own frame, once per scan (and
// kept on disk), with how far each piece's hull stands off the drawn surface; a placed rock only rescales those (its stretch, tilt
// and the ground change which pieces need cutting, and which way) and maps the pieces it keeps.
struct SplitNode {
    std::vector<int> tri;  // its triangles (numbers)
    RockHull hull;         // of them, in the mesh's own frame
    std::vector<int> hv;   // which point of the mesh each of its corners is (pointOf)
    V3 lo, hi;             // the box round its triangles' middles
    // Stand-off along the normals: per triangle (as `tri`), how far out from its middle along its unit normal (turned away from the
    // hull's middle: flip) the hull's skin is, in the mesh's frame; < 0: not counted.
    std::vector<float> reach;
    std::vector<char> flip;
    // Stand-off straight up (the mesh's +y): points of the drawn surface's top under the hull (each upward triangle of the mesh at
    // its middle and halfway to each corner: id 4 tri + k), with nothing drawn above them short of the hull's skin, and how far up that
    // skin is. This is what a wheel meets: a hull over a hollow lined by other pieces' triangles shows here, not along their normals.
    std::vector<int> vprobe;
    std::vector<float> vreach;
    float most = 0;  // the largest of reach and vreach (no placement stretches it more than its longest axis)
    std::vector<int> order, vorder;  // reach and vreach, largest first (the ones counted)
    int kid[3] = {-1, -1, -1};  // cut across axis a: the first of its two halves (the second follows); -1 not cut yet, -2 can't be
};
struct SplitTree {
    std::shared_mutex mu;  // (worlds are built on several threads: shared to use the tree, exclusive to add to it)
    bool ready = false;
    bool dirty = false;  // cut further since it was read from (or last written to) the disk cache
    uint64_t key = 0;    // its disk cache entry (0: none)
    std::vector<V3> tris;    // 3 corners each
    std::vector<V3> mid, n;  // per triangle: the middle and the unit normal (as wound: the scans are wound outward)
    std::vector<char> flat;  // per triangle: no area to speak of in its own frame
    std::vector<SplitNode> nodes;
};

bool finite3(const V3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

// Hull corners are points of the mesh: corner c of triangle tr (id 3 tr + c), or that corner sunk (id + kSunk; see hullOf).
const int kSunk = 1 << 30;
V3 pointOf(const SplitTree& t, int id) {
    const int i = id & (kSunk - 1), tr = i / 3;
    if (!(id & kSunk)) return t.tris[i];
    const V3 &a = t.tris[3 * tr], &b = t.tris[3 * tr + 1], &c = t.tris[3 * tr + 2];
    return t.tris[i] - t.n[tr] * (0.25 * std::max(len(b - a), std::max(len(c - b), len(a - c))));
}

// The hull of some of the mesh's triangles. A few triangles (or a flat run of them) are given depth: their corners again, sunk into
// the rock (against their normals) by a quarter of their size, so a piece can come down to one triangle.
RockHull hullOf(const SplitTree& t, const std::vector<int>& ids) {
    auto build = [&](bool deep) {
        std::vector<V3> pts;
        for (int tr : ids)
            for (int k = 0; k < 3; k++) {
                const V3& p = t.tris[3 * tr + k];
                pts.push_back(p);
                if (deep && !t.flat[tr]) pts.push_back(pointOf(t, (3 * tr + k) | kSunk));
            }
        std::sort(pts.begin(), pts.end(), [](const V3& a, const V3& b) { return a.x < b.x || (a.x == b.x && (a.y < b.y || (a.y == b.y && a.z < b.z))); });
        pts.erase(std::unique(pts.begin(), pts.end(), [](const V3& a, const V3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }), pts.end());  // (corners shared by triangles, once)
        if (pts.size() < 4) return RockHull();
        V3 lo = pts[0], hi = pts[0];
        for (const V3& p : pts) lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)), hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        const double size = std::max(hi.x - lo.x, std::max(hi.y - lo.y, hi.z - lo.z));
        if (!(size > 1e-6)) return RockHull();
        // (Not pushed out: the frame is the scan's own, and stretched where it is placed a push would be any size.)
        RockHull h = buildRockHull("part", pts, 40, nullptr, 0.0);
        if (h.v.size() < 4 || h.f.size() < 4) return RockHull();
        double thick = 1e30;  // (all but flat for its size is as good as none)
        for (const auto& f : h.f) {
            const V3 n = norm(cross(h.v[f[1]] - h.v[f[0]], h.v[f[2]] - h.v[f[0]]));
            double a = 1e30, b = -1e30;
            for (const V3& p : h.v) a = std::min(a, dot(n, p)), b = std::max(b, dot(n, p));
            thick = std::min(thick, b - a);
        }
        return thick > 1e-3 * size ? h : RockHull();
    };
    RockHull h = ids.size() > 3 ? build(false) : RockHull();
    return h.v.empty() ? build(true) : h;
}

// Probe k of triangle tr (id 4 tr + k): its middle (k 0), or halfway from there to corner k - 1.
V3 probePoint(const SplitTree& t, int id) {
    const int tr = id >> 2, k = id & 3;
    return k == 0 ? t.mid[tr] : (t.mid[tr] + t.tris[3 * tr + k - 1]) * 0.5;
}

void setBox(const SplitTree& t, SplitNode& nd) {
    nd.lo = V3(1e30, 1e30, 1e30), nd.hi = V3(-1e30, -1e30, -1e30);
    for (int tr : nd.tri) {
        const V3& m = t.mid[tr];
        nd.lo = V3(std::min(nd.lo.x, m.x), std::min(nd.lo.y, m.y), std::min(nd.lo.z, m.z));
        nd.hi = V3(std::max(nd.hi.x, m.x), std::max(nd.hi.y, m.y), std::max(nd.hi.z, m.z));
    }
}

void setMost(SplitNode& nd, bool orders = true) {
    nd.most = 0;
    for (float r : nd.reach) nd.most = std::max(nd.most, r);
    for (float r : nd.vreach) nd.most = std::max(nd.most, r);
    if (!orders) return;
    auto byReach = [](const std::vector<float>& r, std::vector<int>& o) {
        o.clear();
        for (size_t k = 0; k < r.size(); k++)
            if (r[k] >= 0) o.push_back((int)k);
        std::sort(o.begin(), o.end(), [&](int a, int b) { return r[a] > r[b] || (r[a] == r[b] && a < b); });
    };
    byReach(nd.reach, nd.order), byReach(nd.vreach, nd.vorder);
}

// A node's hull, box and stand-offs. False: no hull.
bool fillNode(const SplitTree& t, SplitNode& nd) {
    nd.hull = hullOf(t, nd.tri);
    if (nd.hull.v.empty()) return false;
    setBox(t, nd);
    std::map<std::array<double, 3>, int> at;  // (each corner is one of its triangles' points, as they were)
    for (int tr : nd.tri)
        for (int k = 0; k < 3; k++) {
            const V3 p = t.tris[3 * tr + k], q = pointOf(t, (3 * tr + k) | kSunk);
            at.emplace(std::array<double, 3>{p.x, p.y, p.z}, 3 * tr + k), at.emplace(std::array<double, 3>{q.x, q.y, q.z}, (3 * tr + k) | kSunk);
        }
    nd.hv.clear();
    for (const V3& p : nd.hull.v) {
        auto it = at.find({p.x, p.y, p.z});
        nd.hv.push_back(it == at.end() ? -1 : it->second);  // (-1: not one of them, which a hull of them can't have: the tree isn't kept)
    }
    V3 hc, hlo(1e30, 1e30, 1e30), hhi(-1e30, -1e30, -1e30);
    for (const V3& p : nd.hull.v) {
        hc += p;
        hlo = V3(std::min(hlo.x, p.x), std::min(hlo.y, p.y), std::min(hlo.z, p.z)), hhi = V3(std::max(hhi.x, p.x), std::max(hhi.y, p.y), std::max(hhi.z, p.z));
    }
    hc = hc / (double)nd.hull.v.size();
    const double size = std::max(hhi.x - hlo.x, std::max(hhi.y - hlo.y, hhi.z - hlo.z));
    std::vector<V3> fn;
    std::vector<double> fd;
    for (const auto& f : nd.hull.f) {
        const V3 n = norm(cross(nd.hull.v[f[1]] - nd.hull.v[f[0]], nd.hull.v[f[2]] - nd.hull.v[f[0]]));
        fn.push_back(n), fd.push_back(dot(n, nd.hull.v[f[0]]));
    }
    // Along each of its triangles' normals.
    nd.reach.assign(nd.tri.size(), -1.0f);
    nd.flip.assign(nd.tri.size(), 0);
    for (size_t i = 0; i < nd.tri.size(); i++) {
        const int tr = nd.tri[i];
        if (t.flat[tr]) continue;
        V3 n = t.n[tr];
        if (dot(n, t.mid[tr] - hc) < 0) n = -n, nd.flip[i] = 1;
        double reach = 1e9;
        for (size_t f = 0; f < fn.size(); f++) {
            const double den = dot(fn[f], n);
            if (den > 1e-9) reach = std::min(reach, (fd[f] - dot(fn[f], t.mid[tr])) / den);
        }
        if (reach < 1e8) nd.reach[i] = (float)std::max(reach, 0.0);
    }
    // Straight up from the top of the drawn surface under it (a point at most a quarter of the hull's size below it).
    const double below = 0.25 * size;
    std::vector<int> col;  // the triangles in the hull's column
    for (int tr = 0; tr < (int)t.mid.size(); tr++) {
        if (t.flat[tr]) continue;
        const V3 &a = t.tris[3 * tr], &b = t.tris[3 * tr + 1], &c = t.tris[3 * tr + 2];
        if (std::max(a.x, std::max(b.x, c.x)) < hlo.x || std::min(a.x, std::min(b.x, c.x)) > hhi.x || std::max(a.z, std::max(b.z, c.z)) < hlo.z ||
            std::min(a.z, std::min(b.z, c.z)) > hhi.z || std::max(a.y, std::max(b.y, c.y)) < hlo.y - below || std::min(a.y, std::min(b.y, c.y)) > hhi.y)
            continue;
        col.push_back(tr);
    }
    nd.vprobe.clear(), nd.vreach.clear();
    for (int tr : col) {
        if (t.n[tr].y <= 0) continue;  // (facing up, at all: whether it is a top depends on how the rock is placed)
        for (int k = 0; k < 4; k++) {
            const V3 m = probePoint(t, 4 * tr + k);
            double tin = -1e30, tout = 1e30;
            for (size_t f = 0; f < fn.size() && tin <= tout; f++) {
                const double sd = dot(fn[f], m) - fd[f], den = fn[f].y;
                if (std::fabs(den) < 1e-12) {
                    if (sd > 0) tin = 1e30;  // (alongside, outside: never in)
                } else if (den > 0) tout = std::min(tout, -sd / den);
                else tin = std::max(tin, -sd / den);
            }
            if (tin > tout || tout <= 0.005 * size || tin > below || tout > 1e29) continue;  // (a few mm on a metre never matters)
            bool covered = false;  // (something drawn above it, short of the skin: that is the top here)
            for (size_t j = 0; j < col.size() && !covered; j++) {
                const int o = col[j];
                if (o == tr) continue;
                const V3 &a = t.tris[3 * o], &b = t.tris[3 * o + 1], &c = t.tris[3 * o + 2];
                const double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
                if (std::fabs(d) < 1e-18) continue;
                const double l1 = ((b.z - c.z) * (m.x - c.x) + (c.x - b.x) * (m.z - c.z)) / d, l2 = ((c.z - a.z) * (m.x - c.x) + (a.x - c.x) * (m.z - c.z)) / d;
                if (l1 < 0 || l2 < 0 || l1 + l2 > 1) continue;
                const double y = l1 * a.y + l2 * b.y + (1 - l1 - l2) * c.y;
                covered = y > m.y + 1e-9 * size && y < m.y + tout;
            }
            if (!covered) nd.vprobe.push_back(4 * tr + k), nd.vreach.push_back((float)tout);
        }
    }
    setMost(nd);
    return true;
}

// Cuts node i in two across `axis` (0 x, 1 y, 2 z) at the median triangle (once; the halves are kept). False if it can't be cut.
bool cutNode(SplitTree& t, int i, int axis) {
    if (t.nodes[i].kid[axis] != -1) return t.nodes[i].kid[axis] >= 0;
    t.nodes[i].kid[axis] = -2;
    const std::vector<int> src = t.nodes[i].tri;
    if (src.size() < 2) return false;
    auto key = [&](int k) { const V3& m = t.mid[src[k]]; return axis == 0 ? m.x : axis == 2 ? m.z : m.y; };
    std::vector<int> order(src.size());
    for (size_t k = 0; k < order.size(); k++) order[k] = (int)k;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return key(a) < key(b) || (key(a) == key(b) && a < b); });
    SplitNode a, b;
    for (size_t k = 0; k < order.size(); k++) (k < order.size() / 2 ? a : b).tri.push_back(src[order[k]]);
    if (!fillNode(t, a) || !fillNode(t, b)) return false;
    const int at = (int)t.nodes.size();
    t.nodes.push_back(std::move(a));
    t.nodes.push_back(std::move(b));
    t.nodes[i].kid[axis] = at;
    t.dirty = true;
    return true;
}

// ---- The trees on disk (meshcache blobs): the cuts are the slow part (a hull each), and every run places the same scans ----
// Keyed by this format and the model file's stamp, not the build (that would leave a few MB behind with every rebuild): bump
// kSplitFormat whenever the cutting or the measuring changes, or old trees are used as they are.
const uint32_t kSplitMagic = 0x50534b52u;  // "RKSP"
const uint64_t kSplitFormat = 3;

// Compact: numbers of triangles and probes as 16 bits, corners as points of the mesh (pointOf), faces as 8-bit corner numbers.
// Empty if the tree doesn't fit that (it is then made again each run).
std::vector<uint8_t> packTree(const SplitTree& t) {
    std::vector<uint8_t> o;
    const size_t nt = t.tris.size() / 3;
    if (4 * nt > 65535) return o;
    auto put = [&](const void* p, size_t n) { o.insert(o.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    auto u32 = [&](uint32_t v) { put(&v, 4); };
    auto u16s = [&](const std::vector<int>& v) {
        u32((uint32_t)v.size());
        for (int x : v) {
            const uint16_t h = (uint16_t)x;
            put(&h, 2);
        }
    };
    u32(kSplitMagic), u32((uint32_t)nt), u32((uint32_t)t.nodes.size());
    for (const SplitNode& nd : t.nodes) {
        if (nd.hv.size() != nd.hull.v.size() || nd.hv.size() > 255) return {};
        const int32_t kid[3] = {nd.kid[0], nd.kid[1], nd.kid[2]};
        put(kid, sizeof kid);
        u16s(nd.tri), put(nd.reach.data(), nd.reach.size() * 4), put(nd.flip.data(), nd.flip.size());
        u16s(nd.vprobe), put(nd.vreach.data(), nd.vreach.size() * 4);
        u16s(nd.order), u16s(nd.vorder);
        u32((uint32_t)nd.hv.size());
        for (int x : nd.hv) {
            if (x < 0) return {};
            u32((uint32_t)x);
        }
        u32((uint32_t)nd.hull.f.size());
        for (const auto& f : nd.hull.f) {
            const uint8_t c[3] = {(uint8_t)f[0], (uint8_t)f[1], (uint8_t)f[2]};
            put(c, 3);
        }
    }
    return o;
}

// (Anything that doesn't add up is a miss: the tree is made again.)
bool unpackTree(const std::vector<uint8_t>& o, SplitTree& t) {
    size_t at = 0;
    auto get = [&](void* p, size_t n) {
        if (n > o.size() - at) return false;
        std::memcpy(p, o.data() + at, n), at += n;
        return true;
    };
    std::vector<uint16_t> h16;
    auto u16s = [&](std::vector<int>& v, uint32_t below) {
        uint32_t n;
        if (!get(&n, 4) || n > below) return false;
        h16.resize(n), v.resize(n);
        if (!get(h16.data(), 2 * (size_t)n)) return false;
        for (uint32_t k = 0; k < n; k++) {
            if (h16[k] >= below) return false;
            v[k] = h16[k];
        }
        return true;
    };
    std::vector<uint32_t> ids;
    std::vector<uint8_t> f8;
    uint32_t magic, nt, nn;
    if (!get(&magic, 4) || !get(&nt, 4) || !get(&nn, 4) || magic != kSplitMagic || nt != t.tris.size() / 3 || nn == 0 || nn > 64 * nt + 1) return false;
    std::vector<SplitNode> nodes(nn);
    for (SplitNode& nd : nodes) {
        int32_t kid[3];
        uint32_t nv, nf;
        if (!get(kid, sizeof kid) || !u16s(nd.tri, nt)) return false;
        for (int a = 0; a < 3; a++) {
            if (kid[a] < -2 || kid[a] >= (int64_t)nn - 1) return false;
            nd.kid[a] = kid[a];
        }
        nd.reach.resize(nd.tri.size()), nd.flip.resize(nd.tri.size());
        if (!get(nd.reach.data(), 4 * nd.reach.size()) || !get(nd.flip.data(), nd.flip.size()) || !u16s(nd.vprobe, 4 * nt)) return false;
        nd.vreach.resize(nd.vprobe.size());
        if (!get(nd.vreach.data(), 4 * nd.vreach.size())) return false;
        // (The orders, kept: sorting them all again would be most of the reading.)
        if (!u16s(nd.order, (uint32_t)nd.tri.size()) || !u16s(nd.vorder, (uint32_t)nd.vprobe.size())) return false;
        if (!get(&nv, 4) || nv < 4 || nv > 255) return false;
        nd.hv.resize(nv), nd.hull.v.resize(nv), ids.resize(nv);
        if (!get(ids.data(), 4 * (size_t)nv)) return false;
        for (uint32_t k = 0; k < nv; k++) {
            const uint32_t id = ids[k];
            if ((id & (kSunk - 1)) >= 3 * nt || (id & ~((uint32_t)kSunk | (uint32_t)(kSunk - 1)))) return false;
            nd.hv[k] = (int)id, nd.hull.v[k] = pointOf(t, (int)id);
        }
        if (!get(&nf, 4) || nf < 4 || nf > 8192) return false;
        nd.hull.f.resize(nf), f8.resize(3 * (size_t)nf);
        if (!get(f8.data(), f8.size())) return false;
        for (uint32_t k = 0; k < nf; k++)
            for (int c = 0; c < 3; c++) {
                if (f8[3 * k + c] >= nv) return false;
                nd.hull.f[k][c] = f8[3 * k + c];
            }
        nd.hull.name = "part";
        setBox(t, nd), setMost(nd, false);
    }
    if (at != o.size()) return false;
    t.nodes = std::move(nodes);
    return true;
}

// Where a cut-up mesh is put: world = o + ax[0] p.x + ax[1] p.y + ax[2] p.z.
struct Placed {
    V3 ax[3], o;
    V3 map(const V3& p) const { return o + ax[0] * p.x + ax[1] * p.y + ax[2] * p.z; }
};

RockHull placeHull(const RockHull& h, const Placed& pl, bool flip) {
    RockHull out;
    out.name = h.name;
    out.v.reserve(h.v.size());
    for (const V3& p : h.v) out.v.push_back(pl.map(p));
    out.f = h.f;
    if (flip)  // (a mirroring map turns the windings inside out)
        for (auto& f : out.f) std::swap(f[1], f[2]);
    for (int k = 0; k < 3; k++) out.bmin[k] = 1e30f, out.bmax[k] = -1e30f;
    for (const V3& p : out.v) {
        const double c3[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; k++) out.bmin[k] = std::fmin(out.bmin[k], (float)c3[k]), out.bmax[k] = std::fmax(out.bmax[k], (float)c3[k]);
    }
    return out;
}

// False (and nothing in `out`) if it needed a cut the tree hasn't got and mayCut is false.
bool splitTree(SplitTree& t, const Placed& pl, double (*ground)(void*, double, double), void* gc, double tol, int maxParts, bool mayCut, std::vector<RockHull>& out) {
    out.clear();
    const int nt = (int)t.tris.size() / 3;
    if (nt < 8 || t.nodes.empty() || t.nodes[0].hull.v.empty()) return true;
    const V3 c0 = cross(pl.ax[1], pl.ax[2]), c1 = cross(pl.ax[2], pl.ax[0]), c2 = cross(pl.ax[0], pl.ax[1]);
    const double det = dot(pl.ax[0], c0);
    if (!(std::fabs(det) > 1e-12)) return true;
    // Per triangle, placed: a stand-off along its unit normal in the mesh's frame is `scale` times that off the placed triangle's
    // plane (the normal goes by the inverse transpose, nW ~ (c0 n.x + c1 n.y + c2 n.z) / det, and a distance off a plane by 1 / |that|);
    // whether its normal (as wound, or turned) faces up; whether it is buried or too small to count.
    // Per triangle, placed (worked out when first needed): a stand-off along its unit normal in the mesh's frame is `scale` times that
    // off the placed triangle's plane (the normal goes by the inverse transpose, nW ~ (c0 n.x + c1 n.y + c2 n.z) / det, and a distance
    // off a plane by 1 / |that|, never more than the longest axis); whether its normal (as wound, or turned) faces up, and how much;
    // whether it is buried or too small to count.
    struct Placed3 {
        float scale = 0, nwy = 0;
        char state = 0;  // 0 not yet, 1 not counted, 2 counted
        char upW = 0, upT = 0;
    };
    std::vector<Placed3> info(nt);
    const double area2 = std::fabs(det);  // (twice the placed area is |det| |A^-T n2|, n2 the frame's cross product)
    auto triInfo = [&](int i) -> const Placed3& {
        Placed3& o = info[i];
        if (o.state) return o;
        o.state = 1;
        const V3& n = t.n[i];
        V3 nw = (c0 * n.x + c1 * n.y + c2 * n.z) / det;  // A^-T n
        const double l = len(nw);
        if (!(l > 1e-12) || t.flat[i]) return o;
        const V3& a = t.tris[3 * i];
        const double localArea2 = len(cross(t.tris[3 * i + 1] - a, t.tris[3 * i + 2] - a));
        if (localArea2 * area2 * l * 0.5 < 1e-5) return o;  // (no area to speak of, placed)
        if (gc) {
            const V3 m = pl.map(t.mid[i]);
            if (m.y < ground(gc, m.x, m.z) - 0.05) return o;  // (buried)
        }
        nw = nw / l;
        o.scale = (float)(1.0 / l), o.nwy = (float)nw.y, o.upW = nw.y > 0.35, o.upT = -nw.y > 0.35, o.state = 2;
        return o;
    };
    // (The mesh's straight up is near enough straight up placed, unless the rock is stood on end: then only the normals count.)
    const double upright = pl.ax[1].y > 0.9 * len(pl.ax[1]) ? pl.ax[1].y : 0;
    const double stretch = std::max(len(pl.ax[0]), std::max(len(pl.ax[1]), len(pl.ax[2])));
    // The worst stand-off of a piece: the largest first, until no more can beat it (or take it over `tol`: below that, how far below
    // doesn't matter).
    auto slackOf = [&](int node) {
        const SplitNode& nd = t.nodes[node];
        if (nd.most * stretch <= tol) return 0.0;  // (within it however it is measured: no need to)
        double worst = 0;
        for (int k : nd.order) {
            if (nd.reach[k] * stretch <= std::max(worst, tol)) break;
            const Placed3& p = triInfo(nd.tri[k]);
            if (p.state != 2) continue;
            const double r = nd.reach[k] * p.scale;
            worst = std::max(worst, (nd.flip[k] ? p.upT : p.upW) ? r : r * 0.5);  // (the top is what's driven on; a flank matters less)
        }
        if (upright > 0)
            for (int k : nd.vorder) {
                if (nd.vreach[k] * upright <= std::max(worst, tol)) break;
                const Placed3& p = triInfo(nd.vprobe[k] >> 2);
                if (p.state == 2 && p.nwy > 0.7) worst = std::max(worst, nd.vreach[k] * upright);  // (a top, placed)
            }
        return worst;
    };
    struct Part {
        int node;
        double slack;
    };
    std::vector<Part> parts;
    parts.push_back({0, slackOf(0)});
    if (parts[0].slack <= tol) return true;
    for (;;) {
        int worst = -1;
        for (int i = 0; i < (int)parts.size(); i++)
            if (parts[i].slack > tol && t.nodes[parts[i].node].tri.size() >= 2 && (worst < 0 || parts[i].slack > parts[worst].slack)) worst = i;
        if (worst < 0 || (int)parts.size() >= maxParts) break;
        const int node = parts[worst].node;
        // Across its longest run as placed (the box of its middles, each of the scan's axes stretched as the rock is).
        const SplitNode& nd = t.nodes[node];
        const double ex = (nd.hi.x - nd.lo.x) * len(pl.ax[0]), ey = (nd.hi.y - nd.lo.y) * len(pl.ax[1]), ez = (nd.hi.z - nd.lo.z) * len(pl.ax[2]);
        const int axis = ex >= ez && ex >= ey ? 0 : ez >= ey ? 2 : 1;
        if (t.nodes[node].kid[axis] == -1 && !mayCut) return false;
        if (!cutNode(t, node, axis)) {
            parts[worst].slack = 0;
            continue;
        }
        const int kid = t.nodes[node].kid[axis];
        parts[worst] = {kid, slackOf(kid)};
        parts.push_back({kid + 1, slackOf(kid + 1)});
    }
    for (const Part& p : parts) out.push_back(placeHull(t.nodes[p.node].hull, pl, det < 0));
    return true;
}

void startTree(SplitTree& t, const std::vector<V3>& tris) {
    t.ready = true;
    t.tris = tris;
    t.tris.resize(tris.size() / 3 * 3);
    for (const V3& p : t.tris)
        if (!finite3(p)) return t.tris.clear();  // (a broken mesh: no pieces)
    const int nt = (int)t.tris.size() / 3;
    if (nt < 8) return;
    t.mid.resize(nt), t.n.resize(nt), t.flat.resize(nt);
    for (int i = 0; i < nt; i++) {
        const V3 &a = t.tris[3 * i], &b = t.tris[3 * i + 1], &c = t.tris[3 * i + 2];
        const V3 n = cross(b - a, c - a);
        const double l = len(n);
        t.mid[i] = (a + b + c) / 3.0;
        t.flat[i] = !(l > 1e-12);
        t.n[i] = t.flat[i] ? V3(0, 1, 0) : n / l;
    }
    std::vector<uint8_t> blob;
    if (t.key && meshcache::getBlob(t.key, blob) && unpackTree(blob, t)) return;
    SplitNode root;
    for (int i = 0; i < nt; i++) root.tri.push_back(i);
    if (fillNode(t, root)) t.nodes.push_back(std::move(root));
    t.dirty = true;
}

std::mutex gTreesMu;
std::map<std::string, std::unique_ptr<SplitTree>> gTrees;
SplitTree* treeFor(const std::string& name) {
    std::lock_guard<std::mutex> lock(gTreesMu);
    std::unique_ptr<SplitTree>& slot = gTrees[name];
    if (!slot) slot.reset(new SplitTree);
    return slot.get();
}
}  // namespace

void saveRockSplits() {
    if (!meshcache::enabled()) return;
    std::vector<SplitTree*> all;
    {
        std::lock_guard<std::mutex> lock(gTreesMu);
        for (auto& e : gTrees) all.push_back(e.second.get());
    }
    for (SplitTree* t : all) {
        std::vector<uint8_t> blob;
        {
            std::unique_lock<std::shared_mutex> lock(t->mu);
            if (!t->ready || !t->dirty || !t->key || t->nodes.empty()) continue;
            blob = packTree(*t);
            t->dirty = false;
        }
        if (!blob.empty()) meshcache::putBlob(t->key, blob);
    }
}

std::vector<RockHull> splitRockMesh(const std::vector<V3>& tris, double (*ground)(void*, double, double), void* gc, double tol, int maxParts) {
    SplitTree t;
    startTree(t, tris);
    const Placed pl{{V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)}, V3()};
    std::vector<RockHull> out;
    splitTree(t, pl, ground, gc, tol, maxParts, true, out);
    return out;
}

std::vector<RockHull> splitPlacedScan(const std::string& name, const V3 ax[3], const V3& origin, double (*ground)(void*, double, double), void* gc, double tol,
                                      int maxParts) {
    for (int k = 0; k < 3; k++)
        if (!finite3(ax[k])) return {};
    if (!finite3(origin)) return {};
    SplitTree* t = treeFor(name);
    loadprof::Scope lp("rocks.split");
    const Placed pl{{ax[0], ax[1], ax[2]}, origin};
    std::vector<RockHull> out;
    {  // (Most placements need only cuts already made: several threads at once.)
        std::shared_lock<std::shared_mutex> lock(t->mu);
        if (t->ready && splitTree(*t, pl, ground, gc, tol, maxParts, false, out)) return out;
    }
    std::unique_lock<std::shared_mutex> lock(t->mu);
    if (!t->ready) {
        if (meshcache::enabled()) {
            meshcache::Hasher h;
            h.str("rock split tree"), h.u64(kSplitFormat), h.str(name), h.u64(meshcache::fileStamp(assetDir() + "/" + name + ".prop"));
            t->key = h.h;
        }
        startTree(*t, rockMeshTris(name));
    }
    splitTree(*t, pl, ground, gc, tol, maxParts, true, out);
    return out;
}
}  // namespace props
