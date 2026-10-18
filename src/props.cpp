#include "props.h"
#include "platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

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
    std::set<int> used;
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
        if (faces.size() > 4096) {
            std::vector<HullFace> keep;
            for (const HullFace& f : faces)
                if (f.alive) keep.push_back(f);
            faces.swap(keep);
        }
        used.clear();
        for (const HullFace& f : faces)
            if (f.alive) used.insert({f.i[0], f.i[1], f.i[2]});
    }
    double outside(const V3& q) const {
        double m = -1e30;
        for (const HullFace& f : faces)
            if (f.alive) m = std::max(m, dot(f.n, q) - f.d);
        return m;
    }
};
}  // namespace

RockHull buildRockHull(const std::string& name, const std::vector<V3>& pts, int maxVerts, double* worstMiss) {
    RockHull out;
    out.name = name;
    // The exact hull first: its corners are the only candidates.
    Hull full;
    if (!full.init(pts)) return out;
    for (int k = 0; k < (int)pts.size(); k++) full.add(k);
    std::vector<V3> cand;
    for (int i : full.used) cand.push_back(pts[i]);
    // Then grow a small one from the biggest misses.
    Hull h;
    h.init(cand);
    while ((int)h.used.size() < maxVerts) {
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
    const std::vector<int> ids(h.used.begin(), h.used.end());
    std::vector<V3> corner;
    for (int id : ids) {
        V3 nsum;
        for (const HullFace& f : h.faces)
            if (f.alive && (f.i[0] == id || f.i[1] == id || f.i[2] == id)) nsum += f.n;
        corner.push_back(cand[id] + norm(nsum) * std::min(err * 0.5, 0.01));
    }
    Hull h2;
    if (!h2.init(corner)) return out;
    for (int k = 0; k < (int)corner.size(); k++) h2.add(k);
    std::map<int, int> r2;
    for (int i : h2.used) {
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
