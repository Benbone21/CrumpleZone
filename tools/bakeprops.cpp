// Offline baker: Poly Haven glTF scans -> game-ready props in assets/props/.
//
//   make bake                       (needs ImageMagick's `convert`; Linux)
//   build/bakeprops --src DIR --out assets/props [--only NAME] [--no-tex]
//
// Solid scans (rocks, stumps, logs, cliffs) are simplified by quadric edge collapse to three
// levels of detail, each given a fresh box-projected atlas, and the diffuse, normal and
// AO/roughness/metal maps are baked onto it from the full-resolution scan (so a 400
// triangle rock still has the detail of the 60k triangle one). Rocks also get a convex
// hull reduced to <= 48 corners (rocks.hull) for the physics. Card-built plants and trees
// keep their original textures and get their lower LODs by dropping a share of their
// leaf/needle pieces and scaling the survivors up.
//
// Conventions: the models are in metres, y up. UVs stay in glTF's convention (v down, so
// image row = v * height); the tangent frame is T = dp/du, B = -dp/dv, which is what a
// glTF (OpenGL-style) normal map expects, and what the game's shader rebuilds.
#define CGLTF_IMPLEMENTATION
#include "../src/third_party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_PNM
#include "../src/third_party/stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/props.h"

using props::Kind;
typedef std::array<int, 3> Tri;

namespace {

std::string gSrc = "assets/source/polyhaven/models", gOut = "assets/props";
bool gNoTex = false;

// ---------------------------------------------------------------------------------------
// Images
struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGB
    bool has() const { return w > 0; }
    bool load(const std::string& path) {
        int c = 0;
        uint8_t* d = stbi_load(path.c_str(), &w, &h, &c, 3);
        if (!d) {
            w = h = 0;
            return false;
        }
        px.assign(d, d + (size_t)w * h * 3);
        stbi_image_free(d);
        return true;
    }
    // Bilinear, repeating; u right, v down (glTF).
    void sample(double u, double v, float* rgb) const {
        const double x = u * w - 0.5, y = v * h - 0.5;
        const double fx = std::floor(x), fy = std::floor(y);
        const double tx = x - fx, ty = y - fy;
        int x0 = ((int)fx % w + w) % w, y0 = ((int)fy % h + h) % h;
        const int x1 = (x0 + 1) % w, y1 = (y0 + 1) % h;
        for (int k = 0; k < 3; k++) {
            const double a = px[((size_t)y0 * w + x0) * 3 + k], b = px[((size_t)y0 * w + x1) * 3 + k];
            const double c = px[((size_t)y1 * w + x0) * 3 + k], d = px[((size_t)y1 * w + x1) * 3 + k];
            rgb[k] = (float)(((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty) / 255.0);
        }
    }
};

float toLinear(float c) { return std::pow(c, 2.2f); }
float toSrgb(float c) { return std::pow(std::fmax(c, 0.0f), 1.0f / 2.2f); }


// Writes a PPM and has ImageMagick turn it into the final JPEG.
void writeJpg(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb, int quality, bool full444) {
    const std::string tmp = path + ".ppm";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", tmp.c_str());
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    const std::string cmd = "convert '" + tmp + "' -sampling-factor " + (full444 ? "4:4:4" : "4:2:0") + " -quality " + std::to_string(quality) + " '" +
                            path + "'";
    if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed: %s\n", cmd.c_str());
    std::remove(tmp.c_str());
}

// ---------------------------------------------------------------------------------------
// Hi-res source mesh (as in the glTF: split along UV seams)
struct HiMesh {
    std::vector<V3> p, n, T;
    std::vector<float> tw;
    std::vector<std::array<double, 2>> uv;
    std::vector<Tri> t;
    Img tex[3];       // diffuse, normal, arm
    bool roughOnly = false;  // the third map is a single roughness map (rock_moss sets)
};

// Per-vertex tangent frames from the UVs: T = dp/du, B = -dp/dv.
void computeTangents(HiMesh& m) {
    std::vector<V3> ta(m.p.size()), ba(m.p.size());
    for (const Tri& t : m.t) {
        const V3 e1 = m.p[t[1]] - m.p[t[0]], e2 = m.p[t[2]] - m.p[t[0]];
        const double du1 = m.uv[t[1]][0] - m.uv[t[0]][0], du2 = m.uv[t[2]][0] - m.uv[t[0]][0];
        const double dv1 = m.uv[t[1]][1] - m.uv[t[0]][1], dv2 = m.uv[t[2]][1] - m.uv[t[0]][1];
        const double det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-14) continue;
        const V3 T = (e1 * dv2 - e2 * dv1) / det, B = (e2 * du1 - e1 * du2) / det * -1.0;  // B = -dp/dv
        const V3 fn = cross(e1, e2);
        const double area = len(fn);
        if (area < 1e-14) continue;
        for (int k = 0; k < 3; k++) {
            const V3 a = m.p[t[(k + 1) % 3]] - m.p[t[k]], b = m.p[t[(k + 2) % 3]] - m.p[t[k]];
            const double ang = std::acos(clampd(dot(norm(a), norm(b)), -1, 1));
            ta[t[k]] += norm(T) * ang;
            ba[t[k]] += norm(B) * ang;
        }
    }
    m.T.assign(m.p.size(), V3());
    m.tw.assign(m.p.size(), 1.0f);
    for (size_t i = 0; i < m.p.size(); i++) {
        V3 T = ta[i] - m.n[i] * dot(m.n[i], ta[i]);
        if (len(T) < 1e-9) {
            T = cross(m.n[i], std::fabs(m.n[i].y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0));
        }
        T = norm(T);
        m.T[i] = T;
        m.tw[i] = dot(cross(m.n[i], T), ba[i]) < 0 ? -1.0f : 1.0f;
    }
}

// ---------------------------------------------------------------------------------------
// glTF loading
struct Gltf {
    cgltf_data* d = nullptr;
    std::string dir;
    bool load(const std::string& id) {
        dir = gSrc + "/" + id + "/";
        const std::string path = dir + id + "_1k.gltf";
        cgltf_options o = {};
        if (cgltf_parse_file(&o, path.c_str(), &d) != cgltf_result_success) return false;
        if (cgltf_load_buffers(&o, d, path.c_str()) != cgltf_result_success) return false;
        return true;
    }
    ~Gltf() {
        if (d) cgltf_free(d);
    }
};

void nodeMatrix(const cgltf_node* n, float* m) { cgltf_node_transform_world(n, m); }

// Appends one primitive (transformed by its node, translation dropped when `keepOffset`
// is false) to the mesh.
void addPrim(HiMesh& hm, const cgltf_primitive& pr, const float* M, bool keepOffset) {
    cgltf_accessor *pos = nullptr, *nor = nullptr, *uv = nullptr;
    for (size_t i = 0; i < pr.attributes_count; i++) {
        const cgltf_attribute& a = pr.attributes[i];
        if (a.type == cgltf_attribute_type_position) pos = a.data;
        if (a.type == cgltf_attribute_type_normal) nor = a.data;
        if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
    }
    const size_t base = hm.p.size(), nv = pos->count;
    for (size_t i = 0; i < nv; i++) {
        float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
        cgltf_accessor_read_float(pos, i, p, 3);
        if (nor) cgltf_accessor_read_float(nor, i, n, 3);
        if (uv) cgltf_accessor_read_float(uv, i, t, 2);
        const V3 pp(M[0] * p[0] + M[4] * p[1] + M[8] * p[2] + (keepOffset ? M[12] : 0), M[1] * p[0] + M[5] * p[1] + M[9] * p[2] + (keepOffset ? M[13] : 0),
                    M[2] * p[0] + M[6] * p[1] + M[10] * p[2] + (keepOffset ? M[14] : 0));
        const V3 nn(M[0] * n[0] + M[4] * n[1] + M[8] * n[2], M[1] * n[0] + M[5] * n[1] + M[9] * n[2], M[2] * n[0] + M[6] * n[1] + M[10] * n[2]);
        hm.p.push_back(pp);
        hm.n.push_back(norm(nn));
        hm.uv.push_back({t[0], t[1]});
    }
    for (size_t i = 0; i < pr.indices->count; i += 3) {
        Tri t = {(int)(base + cgltf_accessor_read_index(pr.indices, i)), (int)(base + cgltf_accessor_read_index(pr.indices, i + 1)),
                 (int)(base + cgltf_accessor_read_index(pr.indices, i + 2))};
        hm.t.push_back(t);
    }
}

void loadTextures(HiMesh& hm, const std::string& dir, const cgltf_material* mat) {
    if (!mat) return;
    auto uri = [&](const cgltf_texture* t) { return t && t->image && t->image->uri ? dir + t->image->uri : std::string(); };
    const std::string d = uri(mat->pbr_metallic_roughness.base_color_texture.texture), n = uri(mat->normal_texture.texture),
                      a = uri(mat->pbr_metallic_roughness.metallic_roughness_texture.texture);
    if (!d.empty()) hm.tex[0].load(d);
    if (!n.empty()) hm.tex[1].load(n);
    if (!a.empty()) {
        hm.tex[2].load(a);
        hm.roughOnly = a.find("_rough_") != std::string::npos;
    }
}

// ---------------------------------------------------------------------------------------
// Welding and simplification
struct Mesh {
    std::vector<V3> p;
    std::vector<Tri> t;
};

Mesh weld(const HiMesh& hm) {
    struct Key {
        int64_t x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct H {
        size_t operator()(const Key& k) const { return (size_t)(k.x * 73856093) ^ (size_t)(k.y * 19349663) ^ (size_t)(k.z * 83492791); }
    };
    std::unordered_map<Key, int, H> map;
    Mesh m;
    std::vector<int> remap(hm.p.size());
    for (size_t i = 0; i < hm.p.size(); i++) {
        const Key k = {(int64_t)std::llround(hm.p[i].x * 1e5), (int64_t)std::llround(hm.p[i].y * 1e5), (int64_t)std::llround(hm.p[i].z * 1e5)};
        auto it = map.find(k);
        if (it == map.end()) {
            it = map.emplace(k, (int)m.p.size()).first;
            m.p.push_back(hm.p[i]);
        }
        remap[i] = it->second;
    }
    for (const Tri& t : hm.t) {
        Tri r = {remap[t[0]], remap[t[1]], remap[t[2]]};
        if (r[0] == r[1] || r[1] == r[2] || r[0] == r[2]) continue;
        m.t.push_back(r);
    }
    return m;
}

struct Quadric {
    double q[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    void plane(double a, double b, double c, double d, double w) {
        q[0] += w * a * a, q[1] += w * a * b, q[2] += w * a * c, q[3] += w * a * d;
        q[4] += w * b * b, q[5] += w * b * c, q[6] += w * b * d;
        q[7] += w * c * c, q[8] += w * c * d;
        q[9] += w * d * d;
    }
    void add(const Quadric& o) {
        for (int i = 0; i < 10; i++) q[i] += o.q[i];
    }
    double eval(const V3& p) const {
        return q[0] * p.x * p.x + 2 * q[1] * p.x * p.y + 2 * q[2] * p.x * p.z + 2 * q[3] * p.x + q[4] * p.y * p.y + 2 * q[5] * p.y * p.z +
               2 * q[6] * p.y + q[7] * p.z * p.z + 2 * q[8] * p.z + q[9];
    }
    bool solve(V3& out) const {
        const double a = q[0], b = q[1], c = q[2], d = q[4], e = q[5], f = q[7];
        const double det = a * (d * f - e * e) - b * (b * f - c * e) + c * (b * e - c * d);
        if (std::fabs(det) < 1e-12) return false;
        const double r0 = -q[3], r1 = -q[6], r2 = -q[8];
        const double i00 = d * f - e * e, i01 = c * e - b * f, i02 = b * e - c * d;
        const double i11 = a * f - c * c, i12 = b * c - a * e, i22 = a * d - b * b;
        out = V3((i00 * r0 + i01 * r1 + i02 * r2) / det, (i01 * r0 + i11 * r1 + i12 * r2) / det, (i02 * r0 + i12 * r1 + i22 * r2) / det);
        return true;
    }
};

// Garland-Heckbert edge collapse down to `target` triangles. The mesh is scaled to a unit
// box inside so the error terms mean the same for every model.
Mesh simplify(const Mesh& in, int target) {
    V3 mn(1e30, 1e30, 1e30), mx(-1e30, -1e30, -1e30);
    for (const V3& p : in.p) {
        mn = V3(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
        mx = V3(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
    }
    const double D = std::max(1e-9, len(mx - mn));
    std::vector<V3> P(in.p.size());
    for (size_t i = 0; i < P.size(); i++) P[i] = (in.p[i] - mn) / D;
    std::vector<Tri> T = in.t;
    const int nv = (int)P.size();
    std::vector<char> alive(nv, 1), talive(T.size(), 1);
    std::vector<std::vector<int>> vt(nv);
    std::vector<Quadric> Q(nv);
    std::vector<int> ver(nv, 0);
    for (size_t i = 0; i < T.size(); i++)
        for (int k = 0; k < 3; k++) vt[T[i][k]].push_back((int)i);
    auto faceNormal = [&](const Tri& t, const V3& a, const V3& b, const V3& c) {
        (void)t;
        return cross(b - a, c - a);
    };
    for (size_t i = 0; i < T.size(); i++) {
        const V3 fn = faceNormal(T[i], P[T[i][0]], P[T[i][1]], P[T[i][2]]);
        const double area = len(fn) * 0.5;
        if (area < 1e-14) continue;
        const V3 n = norm(fn);
        const double d = -dot(n, P[T[i][0]]);
        for (int k = 0; k < 3; k++) Q[T[i][k]].plane(n.x, n.y, n.z, d, area);
    }
    // Boundary edges (one face): a plane through the edge, square to the face, holds them in place.
    {
        std::map<std::pair<int, int>, std::vector<int>> edges;
        for (size_t i = 0; i < T.size(); i++)
            for (int k = 0; k < 3; k++) {
                int a = T[i][k], b = T[i][(k + 1) % 3];
                edges[{std::min(a, b), std::max(a, b)}].push_back((int)i);
            }
        for (const auto& e : edges)
            if (e.second.size() == 1) {
                const Tri& t = T[e.second[0]];
                const V3 fn = norm(faceNormal(t, P[t[0]], P[t[1]], P[t[2]]));
                const V3 ed = P[e.first.second] - P[e.first.first];
                V3 n = norm(cross(ed, fn));
                const double d = -dot(n, P[e.first.first]);
                const double w = len(ed) * len(ed) * 50.0;
                Quadric bq;
                bq.plane(n.x, n.y, n.z, d, w);
                Q[e.first.first].add(bq);
                Q[e.first.second].add(bq);
            }
    }
    struct Cand {
        double cost;
        int a, b, va, vb;
        V3 pos;
        bool operator<(const Cand& o) const { return cost > o.cost; }
    };
    std::priority_queue<Cand> heap;
    auto edgeCand = [&](int a, int b, Cand& c) {
        Quadric q = Q[a];
        q.add(Q[b]);
        const V3 mid = (P[a] + P[b]) * 0.5;
        const double L2 = dot(P[a] - P[b], P[a] - P[b]);
        V3 best = mid;
        double bc = q.eval(mid);
        for (const V3& cand : {P[a], P[b]}) {
            const double e = q.eval(cand);
            if (e < bc) bc = e, best = cand;
        }
        V3 opt;
        if (q.solve(opt) && len(opt - mid) < 1.5 * std::sqrt(L2) + 1e-9) {
            const double e = q.eval(opt);
            if (e < bc + 1e-15) bc = e, best = opt;
        }
        c.cost = std::max(bc, 0.0) + 2e-3 * L2;
        c.a = a, c.b = b, c.va = ver[a], c.vb = ver[b], c.pos = best;
    };
    auto fillHeap = [&]() {
        heap = std::priority_queue<Cand>();
        std::set<std::pair<int, int>> seen;
        for (size_t ti = 0; ti < T.size(); ti++) {
            if (!talive[ti]) continue;
            const Tri& t = T[ti];
            for (int k = 0; k < 3; k++) {
                int a = t[k], b = t[(k + 1) % 3];
                if (a > b) std::swap(a, b);
                if (seen.insert({a, b}).second) {
                    Cand c;
                    edgeCand(a, b, c);
                    heap.push(c);
                }
            }
        }
    };
    fillHeap();
    int tris = (int)T.size();
    auto neighbours = [&](int v, std::vector<int>& out) {
        out.clear();
        for (int ti : vt[v])
            for (int k = 0; k < 3; k++)
                if (T[ti][k] != v) out.push_back(T[ti][k]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    std::vector<int> na, nb, common;
    int rejLink = 0, rejFlip = 0, rejThin = 0;
    int pass = 0, collapsedThisPass = 0;
    while (tris > target) {
        if (heap.empty()) {
            // Edges rejected early may be fine now: look again, a little less picky each time.
            if (collapsedThisPass == 0 && pass >= 3) break;
            if (collapsedThisPass == 0) pass++;
            collapsedThisPass = 0;
            fillHeap();
            if (heap.empty()) break;
        }
        const double flipMin = pass == 0 ? 0.25 : pass == 1 ? 0.0 : pass == 2 ? -0.3 : -0.6;
        const double thinMin = pass == 0 ? 0.04 : pass == 1 ? 0.02 : 0.0;
        Cand c = heap.top();
        heap.pop();
        if (!alive[c.a] || !alive[c.b] || ver[c.a] != c.va || ver[c.b] != c.vb) continue;
        const int a = c.a, b = c.b;
        // Link condition: the shared neighbours must be exactly the apexes of the triangles on the edge.
        neighbours(a, na);
        neighbours(b, nb);
        common.clear();
        std::set_intersection(na.begin(), na.end(), nb.begin(), nb.end(), std::back_inserter(common));
        int shared = 0;
        for (int ti : vt[a])
            if (T[ti][0] == b || T[ti][1] == b || T[ti][2] == b) shared++;
        if ((int)common.size() > shared) {
            rejLink++;
            continue;
        }
        // No flips, no slivers.
        bool ok = true;
        for (int side = 0; side < 2 && ok; side++) {
            const int v = side ? b : a;
            for (int ti : vt[v]) {
                const Tri& t = T[ti];
                if ((t[0] == a || t[1] == a || t[2] == a) && (t[0] == b || t[1] == b || t[2] == b)) continue;
                V3 q[3];
                for (int k = 0; k < 3; k++) q[k] = t[k] == v ? c.pos : P[t[k]];
                const V3 n0 = cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]), n1 = cross(q[1] - q[0], q[2] - q[0]);
                const double l0 = len(n0), l1 = len(n1);
                if (l0 < 1e-12) continue;  // already a sliver (scans have many): nothing to protect
                if (l1 < 1e-13 || (l0 > 1e-13 && dot(n0, n1) / (l0 * l1) < flipMin)) {
                    ok = false;
                    rejFlip++;
                    if (std::getenv("BAKE_DEBUG2") && rejFlip < 15) std::fprintf(stderr, "flip: pass %d l0 %g l1 %g cos %g side %d edge %g\n", pass, l0, l1, l0 > 0 && l1 > 0 ? dot(n0, n1) / (l0 * l1) : 9.0, side, len(P[a] - P[b]));
                    break;
                }
                // Keep triangles from going needle-thin.
                const double e0 = len(q[1] - q[0]), e1 = len(q[2] - q[1]), e2 = len(q[0] - q[2]);
                const double emax = std::max({e0, e1, e2});
                if (l1 * 0.5 < thinMin * emax * emax * 0.5 && l1 < l0) {
                    ok = false;
                    rejThin++;
                    break;
                }
            }
        }
        if (!ok) continue;
        // Collapse b into a.
        collapsedThisPass++;
        P[a] = c.pos;
        Q[a].add(Q[b]);
        alive[b] = 0;
        for (int ti : vt[b]) {
            Tri& t = T[ti];
            if (!talive[ti]) continue;
            if (t[0] == a || t[1] == a || t[2] == a) {
                talive[ti] = 0;
                tris--;
                continue;
            }
            for (int k = 0; k < 3; k++)
                if (t[k] == b) t[k] = a;
            vt[a].push_back(ti);
        }
        vt[b].clear();
        vt[a].erase(std::remove_if(vt[a].begin(), vt[a].end(), [&](int ti) { return !talive[ti]; }), vt[a].end());
        ver[a]++;
        neighbours(a, na);
        for (int n : na) {
            Cand cc;
            edgeCand(std::min(a, n), std::max(a, n), cc);
            heap.push(cc);
        }
    }
    if (std::getenv("BAKE_DEBUG")) std::fprintf(stderr, "    simplify: %d tris left (target %d), rejected link %d flip %d thin %d, heap left %zu\n", tris, target, rejLink, rejFlip, rejThin, heap.size());
    Mesh out;
    std::vector<int> remap(nv, -1);
    for (size_t i = 0; i < T.size(); i++) {
        if (!talive[i]) continue;
        Tri r;
        for (int k = 0; k < 3; k++) {
            if (remap[T[i][k]] < 0) {
                remap[T[i][k]] = (int)out.p.size();
                out.p.push_back(P[T[i][k]] * D + mn);
            }
            r[k] = remap[T[i][k]];
        }
        out.t.push_back(r);
    }
    return out;
}

// Smooth normals, weighted by the angle at each corner.
std::vector<V3> smoothNormals(const Mesh& m) {
    std::vector<V3> n(m.p.size());
    for (const Tri& t : m.t) {
        const V3 fn = cross(m.p[t[1]] - m.p[t[0]], m.p[t[2]] - m.p[t[0]]);
        if (len(fn) < 1e-14) continue;
        const V3 f = norm(fn);
        for (int k = 0; k < 3; k++) {
            const V3 a = m.p[t[(k + 1) % 3]] - m.p[t[k]], b = m.p[t[(k + 2) % 3]] - m.p[t[k]];
            n[t[k]] += f * std::acos(clampd(dot(norm(a), norm(b)), -1, 1));
        }
    }
    for (V3& v : n) v = norm(v);
    return n;
}

// ---------------------------------------------------------------------------------------
// BVH over the hi-res triangles, for the bake's rays
struct BVH {
    struct Node {
        float mn[3], mx[3];
        int l = -1, r = -1, start = 0, count = 0;
    };
    std::vector<Node> nodes;
    std::vector<int> order;
    std::vector<float> tv;  // 9 floats per triangle
    std::vector<float> cen;
    void build(const HiMesh& hm) {
        const int nt = (int)hm.t.size();
        tv.resize((size_t)nt * 9);
        cen.resize((size_t)nt * 3);
        for (int i = 0; i < nt; i++) {
            for (int k = 0; k < 3; k++) {
                const V3& p = hm.p[hm.t[i][k]];
                tv[i * 9 + k * 3 + 0] = (float)p.x, tv[i * 9 + k * 3 + 1] = (float)p.y, tv[i * 9 + k * 3 + 2] = (float)p.z;
            }
            for (int a = 0; a < 3; a++) cen[i * 3 + a] = (tv[i * 9 + a] + tv[i * 9 + 3 + a] + tv[i * 9 + 6 + a]) / 3;
        }
        order.resize(nt);
        std::iota(order.begin(), order.end(), 0);
        nodes.reserve(nt / 2 + 4);
        buildNode(0, nt);
    }
    int buildNode(int s, int e) {
        const int id = (int)nodes.size();
        nodes.emplace_back();
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f}, cmn[3] = {1e30f, 1e30f, 1e30f}, cmx[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = s; i < e; i++) {
            const int t = order[i];
            for (int v = 0; v < 3; v++)
                for (int a = 0; a < 3; a++) {
                    mn[a] = std::fmin(mn[a], tv[t * 9 + v * 3 + a]);
                    mx[a] = std::fmax(mx[a], tv[t * 9 + v * 3 + a]);
                }
            for (int a = 0; a < 3; a++) cmn[a] = std::fmin(cmn[a], cen[t * 3 + a]), cmx[a] = std::fmax(cmx[a], cen[t * 3 + a]);
        }
        for (int a = 0; a < 3; a++) nodes[id].mn[a] = mn[a], nodes[id].mx[a] = mx[a];
        if (e - s <= 4) {
            nodes[id].start = s, nodes[id].count = e - s;
            return id;
        }
        int axis = 0;
        for (int a = 1; a < 3; a++)
            if (cmx[a] - cmn[a] > cmx[axis] - cmn[axis]) axis = a;
        const int mid = (s + e) / 2;
        std::nth_element(order.begin() + s, order.begin() + mid, order.begin() + e,
                         [&](int x, int y) { return cen[x * 3 + axis] < cen[y * 3 + axis]; });
        const int l = buildNode(s, mid), r = buildNode(mid, e);
        nodes[id].l = l, nodes[id].r = r;
        return id;
    }
    // Nearest hit of the ray o + t d, t in (0, tmax]; either side of a surface counts.
    bool hit(const float* o, const float* d, float tmax, int& tri, float& bu, float& bv) const {
        float inv[3] = {1.0f / (std::fabs(d[0]) > 1e-12f ? d[0] : 1e-12f), 1.0f / (std::fabs(d[1]) > 1e-12f ? d[1] : 1e-12f),
                        1.0f / (std::fabs(d[2]) > 1e-12f ? d[2] : 1e-12f)};
        int stack[64], sp = 0;
        stack[sp++] = 0;
        bool found = false;
        float best = tmax;
        while (sp) {
            const Node& n = nodes[stack[--sp]];
            float t0 = 0, t1 = best;
            for (int a = 0; a < 3; a++) {
                float ta = (n.mn[a] - o[a]) * inv[a], tb = (n.mx[a] - o[a]) * inv[a];
                if (ta > tb) std::swap(ta, tb);
                t0 = std::fmax(t0, ta), t1 = std::fmin(t1, tb);
            }
            if (t0 > t1) continue;
            if (n.count) {
                for (int i = n.start; i < n.start + n.count; i++) {
                    const int t = order[i];
                    const float* v = &tv[t * 9];
                    const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
                    const float pv[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
                    const float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
                    if (std::fabs(det) < 1e-14f) continue;
                    const float id = 1.0f / det;
                    const float tv_[3] = {o[0] - v[0], o[1] - v[1], o[2] - v[2]};
                    const float u = (tv_[0] * pv[0] + tv_[1] * pv[1] + tv_[2] * pv[2]) * id;
                    if (u < 0 || u > 1) continue;
                    const float qv[3] = {tv_[1] * e1[2] - tv_[2] * e1[1], tv_[2] * e1[0] - tv_[0] * e1[2], tv_[0] * e1[1] - tv_[1] * e1[0]};
                    const float w = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * id;
                    if (w < 0 || u + w > 1) continue;
                    const float tt = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * id;
                    if (tt > 1e-7f && tt < best) best = tt, tri = t, bu = u, bv = w, found = true;
                }
            } else {
                stack[sp++] = n.l;
                stack[sp++] = n.r;
            }
        }
        return found;
    }
};

// ---------------------------------------------------------------------------------------
// Unwrapping: box-projected charts (grown from the dominant axis of each face), packed in a
// square with a gutter. UVs follow the convention above (v down, T = +u, B = -dv = up).
struct Unwrapped {
    std::vector<V3> p, n;
    std::vector<std::array<double, 2>> uv;
    std::vector<V3> tan;
    std::vector<float> tw;
    std::vector<Tri> t;
};

Unwrapped unwrap(const Mesh& m, int texSize) {
    const std::vector<V3> vn = smoothNormals(m);
    const int nt = (int)m.t.size();
    std::vector<V3> fnrm(nt);
    std::vector<double> farea(nt);
    std::vector<int> label(nt);
    for (int i = 0; i < nt; i++) {
        const V3 fn = cross(m.p[m.t[i][1]] - m.p[m.t[i][0]], m.p[m.t[i][2]] - m.p[m.t[i][0]]);
        farea[i] = len(fn) * 0.5;
        fnrm[i] = norm(fn);
        // Which of the six directions the face looks most toward: a blend of its own
        // normal and its corners' smooth normals, so a curved patch doesn't flip labels.
        V3 s = fnrm[i] * 1.0;
        for (int k = 0; k < 3; k++) s += vn[m.t[i][k]] * 0.4;
        const double ax = std::fabs(s.x), ay = std::fabs(s.y), az = std::fabs(s.z);
        label[i] = ax >= ay && ax >= az ? (s.x > 0 ? 0 : 1) : ay >= az ? (s.y > 0 ? 2 : 3) : (s.z > 0 ? 4 : 5);
    }
    // Neighbours across edges.
    std::map<std::pair<int, int>, std::vector<int>> edges;
    for (int i = 0; i < nt; i++)
        for (int k = 0; k < 3; k++) {
            int a = m.t[i][k], b = m.t[i][(k + 1) % 3];
            edges[{std::min(a, b), std::max(a, b)}].push_back(i);
        }
    std::vector<std::vector<int>> adj(nt);
    for (const auto& e : edges)
        for (size_t a = 0; a < e.second.size(); a++)
            for (size_t b = a + 1; b < e.second.size(); b++) adj[e.second[a]].push_back(e.second[b]), adj[e.second[b]].push_back(e.second[a]);
    // Charts by flood fill, then tiny ones merged into the neighbour they face most like.
    std::vector<int> chart(nt, -1);
    int nCharts = 0;
    for (int i = 0; i < nt; i++) {
        if (chart[i] >= 0) continue;
        std::vector<int> st = {i};
        chart[i] = nCharts;
        while (!st.empty()) {
            const int c = st.back();
            st.pop_back();
            for (int o : adj[c])
                if (chart[o] < 0 && label[o] == label[i]) chart[o] = nCharts, st.push_back(o);
        }
        nCharts++;
    }
    static const V3 kAxis[6] = {V3(1, 0, 0), V3(-1, 0, 0), V3(0, 1, 0), V3(0, -1, 0), V3(0, 0, 1), V3(0, 0, -1)};
    double total = 0;
    for (double a : farea) total += a;
    for (int pass = 0; pass < 6; pass++) {
        std::vector<double> carea(nCharts, 0);
        std::vector<int> clabel(nCharts, 0);
        for (int i = 0; i < nt; i++) carea[chart[i]] += farea[i], clabel[chart[i]] = label[i];
        bool changed = false;
        for (int c = 0; c < nCharts; c++) {
            if (carea[c] <= 0 || carea[c] > total * 0.02) continue;
            // Best neighbouring chart by shared edges and normal agreement.
            std::map<int, double> score;
            for (int i = 0; i < nt; i++)
                if (chart[i] == c)
                    for (int o : adj[i])
                        if (chart[o] != c) {
                            const double al = dot(fnrm[i], kAxis[clabel[chart[o]]]);
                            if (al > 0.35) score[chart[o]] += al;
                        }
            int best = -1;
            double bs = 0;
            for (const auto& s : score)
                if (s.second > bs) bs = s.second, best = s.first;
            if (best < 0) continue;
            for (int i = 0; i < nt; i++)
                if (chart[i] == c) chart[i] = best, label[i] = clabel[best];
            carea[best] += carea[c];
            carea[c] = 0;
            changed = true;
        }
        if (!changed) break;
    }
    // Relabel compactly.
    std::map<int, int> ids;
    for (int i = 0; i < nt; i++)
        if (!ids.count(chart[i])) {
            const int k = (int)ids.size();
            ids[chart[i]] = k;
        }
    nCharts = (int)ids.size();
    std::vector<int> cid(nt);
    for (int i = 0; i < nt; i++) cid[i] = ids[chart[i]];
    struct Chart {
        std::vector<int> tris;
        V3 U, Vv;
        double u0, u1, v0, v1;
        double ox = 0, oy = 0;  // packed position (uv units, before padding)
        bool rot = false;
        double w() const { return rot ? v1 - v0 : u1 - u0; }
        double h() const { return rot ? u1 - u0 : v1 - v0; }
    };
    std::vector<Chart> charts(nCharts);
    for (int i = 0; i < nt; i++) charts[cid[i]].tris.push_back(i);
    for (Chart& c : charts) {
        // The chart's axis: its area-weighted mean normal, snapped to the closest of the
        // six directions (so side faces keep "up" up, and the texture stays upright).
        V3 mean;
        for (int i : c.tris) mean += fnrm[i] * farea[i];
        mean = norm(mean);
        int best = 0;
        for (int a = 1; a < 6; a++)
            if (dot(mean, kAxis[a]) > dot(mean, kAxis[best])) best = a;
        const V3 a = kAxis[best];
        const V3 v0 = std::fabs(a.y) < 0.9 ? V3(0, 1, 0) : V3(0, 0, 1);
        c.U = norm(cross(v0, a));
        c.Vv = cross(a, c.U);  // up on the texture
        c.u0 = c.v0 = 1e30, c.u1 = c.v1 = -1e30;
        for (int i : c.tris)
            for (int k = 0; k < 3; k++) {
                const V3& p = m.p[m.t[i][k]];
                const double u = dot(p, c.U), v = -dot(p, c.Vv);
                c.u0 = std::min(c.u0, u), c.u1 = std::max(c.u1, u), c.v0 = std::min(c.v0, v), c.v1 = std::max(c.v1, v);
            }
        if (c.u1 - c.u0 < c.v1 - c.v0) c.rot = true;  // landscape shelves pack tighter
    }
    // Shelf packing at the largest scale that fits.
    const double pad = 5.0 / texSize;
    std::vector<int> ord(nCharts);
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { return charts[a].h() > charts[b].h(); });
    auto pack = [&](double s) {
        double x = 0, y = 0, rowH = 0;
        for (int id : ord) {
            Chart& c = charts[id];
            const double w = c.w() * s + 2 * pad, h = c.h() * s + 2 * pad;
            if (x + w > 1.0) x = 0, y += rowH, rowH = 0;
            if (w > 1.0) return false;
            c.ox = x + pad, c.oy = y + pad;
            x += w;
            rowH = std::max(rowH, h);
            if (y + rowH > 1.0) return false;
        }
        return true;
    };
    double lo = 1e-4, hi = 1000;
    for (int it = 0; it < 50; it++) {
        const double mid = std::sqrt(lo * hi);
        (pack(mid) ? lo : hi) = mid;
    }
    const double scale = lo * 0.995;
    pack(scale);
    // Emit vertices per (point, chart).
    Unwrapped out;
    std::map<std::pair<int, int>, int> vmap;
    for (int i = 0; i < nt; i++) {
        const Chart& c = charts[cid[i]];
        Tri r;
        for (int k = 0; k < 3; k++) {
            const int pi = m.t[i][k];
            auto key = std::make_pair(pi, cid[i]);
            auto it = vmap.find(key);
            if (it == vmap.end()) {
                const V3& p = m.p[pi];
                double u = dot(p, c.U) - c.u0, v = -dot(p, c.Vv) - c.v0;
                if (c.rot) {
                    const double nu = (c.v1 - c.v0) - v, nv = u;  // quarter turn
                    u = nu, v = nv;
                }
                out.p.push_back(p);
                out.n.push_back(vn[pi]);
                out.uv.push_back({c.ox + u * scale, c.oy + v * scale});
                it = vmap.emplace(key, (int)out.p.size() - 1).first;
            }
            r[k] = it->second;
        }
        out.t.push_back(r);
    }
    // Tangents (same recipe as the hi-res mesh).
    HiMesh tmp;
    tmp.p = out.p, tmp.n = out.n, tmp.uv = out.uv, tmp.t = out.t;
    computeTangents(tmp);
    out.tan = tmp.T;
    out.tw = tmp.tw;
    return out;
}

// ---------------------------------------------------------------------------------------
// Baking
struct Baked {
    std::vector<uint8_t> diff, nor, arm;
};

Baked bake(const Unwrapped& lo, const HiMesh& hi, const BVH& bvh, int S, double cage) {
    const int nt = (int)lo.t.size();
    std::vector<int> ids((size_t)S * S, -1);
    // Rasterise triangle ids: first the pixels whose centres are inside, then a 2 px gutter.
    auto edgeFn = [](double ax, double ay, double bx, double by, double px, double py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nt; i++) {
            double x[3], y[3];
            for (int k = 0; k < 3; k++) x[k] = lo.uv[lo.t[i][k]][0] * S, y[k] = lo.uv[lo.t[i][k]][1] * S;
            const double area = edgeFn(x[0], y[0], x[1], y[1], x[2], y[2]);
            if (std::fabs(area) < 1e-9) continue;
            const double margin = pass == 0 ? 0.0 : 2.0;
            const int x0 = std::max(0, (int)std::floor(std::min({x[0], x[1], x[2]}) - margin)), x1 = std::min(S - 1, (int)std::ceil(std::max({x[0], x[1], x[2]}) + margin));
            const int y0 = std::max(0, (int)std::floor(std::min({y[0], y[1], y[2]}) - margin)), y1 = std::min(S - 1, (int)std::ceil(std::max({y[0], y[1], y[2]}) + margin));
            for (int py = y0; py <= y1; py++)
                for (int px = x0; px <= x1; px++) {
                    if (ids[(size_t)py * S + px] >= 0) continue;
                    const double cx = px + 0.5, cy = py + 0.5;
                    double inside = 1e30;
                    const double s = area < 0 ? -1 : 1;
                    for (int k = 0; k < 3; k++) {
                        const int k2 = (k + 1) % 3;
                        const double ex = x[k2] - x[k], ey = y[k2] - y[k];
                        const double l = std::sqrt(ex * ex + ey * ey);
                        inside = std::min(inside, s * edgeFn(x[k], y[k], x[k2], y[k2], cx, cy) / std::max(l, 1e-9));  // signed distance, + inside
                    }
                    if (inside >= -margin) ids[(size_t)py * S + px] = i;
                }
        }
    }
    Baked out;
    out.diff.assign((size_t)S * S * 3, 0);
    out.nor.assign((size_t)S * S * 3, 128);
    out.arm.assign((size_t)S * S * 3, 0);
    std::vector<char> valid((size_t)S * S, 0);
    double bmn[3] = {1e30, 1e30, 1e30}, bmx[3] = {-1e30, -1e30, -1e30};
    for (const V3& p : lo.p) {
        bmn[0] = std::min(bmn[0], p.x), bmn[1] = std::min(bmn[1], p.y), bmn[2] = std::min(bmn[2], p.z);
        bmx[0] = std::max(bmx[0], p.x), bmx[1] = std::max(bmx[1], p.y), bmx[2] = std::max(bmx[2], p.z);
    }
    const double diag = std::sqrt((bmx[0] - bmn[0]) * (bmx[0] - bmn[0]) + (bmx[1] - bmn[1]) * (bmx[1] - bmn[1]) + (bmx[2] - bmn[2]) * (bmx[2] - bmn[2]));
    const double reach = cage * diag;
    const int SS = 3;
#pragma omp parallel for schedule(dynamic, 8)
    for (int py = 0; py < S; py++)
        for (int px = 0; px < S; px++) {
            const int ti = ids[(size_t)py * S + px];
            if (ti < 0) continue;
            const Tri& t = lo.t[ti];
            double x[3], y[3];
            for (int k = 0; k < 3; k++) x[k] = lo.uv[t[k]][0] * S, y[k] = lo.uv[t[k]][1] * S;
            const double area = edgeFn(x[0], y[0], x[1], y[1], x[2], y[2]);
            double dsum[3] = {0, 0, 0}, nsum[3] = {0, 0, 0}, asum[3] = {0, 0, 0};
            int count = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    const double cx = px + (sx + 0.5) / SS, cy = py + (sy + 0.5) / SS;
                    // Barycentrics (extrapolated in the gutter).
                    const double w0 = edgeFn(x[1], y[1], x[2], y[2], cx, cy) / area, w1 = edgeFn(x[2], y[2], x[0], y[0], cx, cy) / area, w2 = 1 - w0 - w1;
                    const V3 P = lo.p[t[0]] * w0 + lo.p[t[1]] * w1 + lo.p[t[2]] * w2;
                    const V3 N = norm(lo.n[t[0]] * w0 + lo.n[t[1]] * w1 + lo.n[t[2]] * w2);
                    V3 Tn = lo.tan[t[0]] * w0 + lo.tan[t[1]] * w1 + lo.tan[t[2]] * w2;
                    Tn = norm(Tn - N * dot(N, Tn));
                    const V3 Bn = cross(N, Tn) * lo.tw[t[0]];
                    const V3 O = P + N * reach;
                    const float o[3] = {(float)O.x, (float)O.y, (float)O.z}, d[3] = {(float)-N.x, (float)-N.y, (float)-N.z};
                    int ht = -1;
                    float bu = 0, bv = 0;
                    if (!bvh.hit(o, d, (float)(reach * 2), ht, bu, bv)) continue;
                    const Tri& h = hi.t[ht];
                    const double b0 = 1 - bu - bv, b1 = bu, b2 = bv;
                    const double u = hi.uv[h[0]][0] * b0 + hi.uv[h[1]][0] * b1 + hi.uv[h[2]][0] * b2;
                    const double v = hi.uv[h[0]][1] * b0 + hi.uv[h[1]][1] * b1 + hi.uv[h[2]][1] * b2;
                    float c[3] = {0.5f, 0.5f, 0.5f}, a[3] = {1, 0.8f, 0}, nm[3] = {0.5f, 0.5f, 1.0f};
                    if (hi.tex[0].has()) hi.tex[0].sample(u, v, c);
                    if (hi.tex[2].has()) {
                        hi.tex[2].sample(u, v, a);
                        if (hi.roughOnly) a[1] = a[0], a[0] = 1.0f, a[2] = 0.0f;
                    }
                    // The scan's own normal map, taken through its tangent frame into world space,
                    // then into this triangle's frame.
                    V3 Nw = norm(hi.n[h[0]] * b0 + hi.n[h[1]] * b1 + hi.n[h[2]] * b2);
                    if (hi.tex[1].has()) {
                        hi.tex[1].sample(u, v, nm);
                        V3 Th = hi.T[h[0]] * b0 + hi.T[h[1]] * b1 + hi.T[h[2]] * b2;
                        const V3 Nh = Nw;
                        Th = norm(Th - Nh * dot(Nh, Th));
                        const V3 Bh = cross(Nh, Th) * (double)hi.tw[h[0]];
                        const V3 ts(nm[0] * 2 - 1, nm[1] * 2 - 1, nm[2] * 2 - 1);
                        Nw = norm(Th * ts.x + Bh * ts.y + Nh * ts.z);
                    }
                    const double nx = dot(Nw, Tn), ny = dot(Nw, Bn), nz = dot(Nw, N);
                    for (int k = 0; k < 3; k++) dsum[k] += toLinear(c[k]);
                    for (int k = 0; k < 3; k++) asum[k] += a[k];
                    nsum[0] += nx, nsum[1] += ny, nsum[2] += nz;
                    count++;
                }
            if (!count) continue;
            valid[(size_t)py * S + px] = 1;
            const size_t o = ((size_t)py * S + px) * 3;
            for (int k = 0; k < 3; k++) {
                out.diff[o + k] = (uint8_t)std::lround(255 * clampd(toSrgb((float)(dsum[k] / count)), 0, 1));
                out.arm[o + k] = (uint8_t)std::lround(255 * clampd(asum[k] / count, 0, 1));
            }
            V3 nn = norm(V3(nsum[0], nsum[1], nsum[2]));
            if (nn.z < 0.05) nn = norm(V3(nn.x, nn.y, 0.05));
            out.nor[o + 0] = (uint8_t)std::lround(255 * (nn.x * 0.5 + 0.5));
            out.nor[o + 1] = (uint8_t)std::lround(255 * (nn.y * 0.5 + 0.5));
            out.nor[o + 2] = (uint8_t)std::lround(255 * (nn.z * 0.5 + 0.5));
        }
    // Fill the holes (gutters beyond the gutter, misses) from their neighbours.
    for (int pass = 0; pass < 8; pass++) {
        std::vector<char> nv = valid;
        Baked nb = out;
        for (int py = 0; py < S; py++)
            for (int px = 0; px < S; px++) {
                if (valid[(size_t)py * S + px]) continue;
                int cnt = 0;
                int acc[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int qx = px + dx, qy = py + dy;
                        if (qx < 0 || qy < 0 || qx >= S || qy >= S || !valid[(size_t)qy * S + qx]) continue;
                        const size_t q = ((size_t)qy * S + qx) * 3;
                        for (int k = 0; k < 3; k++) acc[0][k] += out.diff[q + k], acc[1][k] += out.nor[q + k], acc[2][k] += out.arm[q + k];
                        cnt++;
                    }
                if (!cnt) continue;
                const size_t o = ((size_t)py * S + px) * 3;
                for (int k = 0; k < 3; k++) nb.diff[o + k] = (uint8_t)(acc[0][k] / cnt), nb.nor[o + k] = (uint8_t)(acc[1][k] / cnt), nb.arm[o + k] = (uint8_t)(acc[2][k] / cnt);
                nv[(size_t)py * S + px] = 1;
            }
        valid = nv;
        out = nb;
    }
    for (size_t i = 0; i < valid.size(); i++)
        if (!valid[i]) {
            out.diff[i * 3] = out.diff[i * 3 + 1] = out.diff[i * 3 + 2] = 90;
            out.nor[i * 3] = 128, out.nor[i * 3 + 1] = 128, out.nor[i * 3 + 2] = 255;
            out.arm[i * 3] = 255, out.arm[i * 3 + 1] = 200, out.arm[i * 3 + 2] = 0;
        }
    return out;
}

// ---------------------------------------------------------------------------------------
// Units
enum Mode { CENTER_BASE, CENTER_ALL };

struct Unit {
    const char* src;       // glTF folder
    int mesh;              // mesh (node) index, -1 = every node merged (cards)
    const char* name;      // output model name
    Kind kind;
    bool solid;            // true: simplify and bake; false: cards
    int tris[3];           // solid: triangle targets per LOD
    int tex[3];            // solid: atlas sizes per LOD
    bool hull = false;
    Mode mode = CENTER_BASE;
    bool upright = false;  // stand a lying trunk on its thicker end
    double cage = 0.04;
    // Cards: share of the pieces kept per LOD, size cap for the scaled-up survivors, wind.
    float keep[3] = {1, 0.3f, 0.08f};
    float wind = 0;
    bool foliage = false;
    const char* texOut = nullptr;  // cards: shared texture base name
    int texSize = 512;
    float lodDist[3] = {25, 70, 1e9f};
};

const Unit kUnits[] = {
    // Rocks: closed scans, with hulls.
    {"rock_moss_set_01", 0, "rock_moss1_a", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_01", 1, "rock_moss1_b", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_01", 2, "rock_moss1_c", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_01", 3, "rock_moss1_d", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_01", 4, "rock_moss1_e", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_01", 5, "rock_moss1_f", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 0, "rock_moss2_a", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 1, "rock_moss2_b", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 2, "rock_moss2_c", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 3, "rock_moss2_d", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 4, "rock_moss2_e", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 5, "rock_moss2_f", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_moss_set_02", 6, "rock_moss2_g", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_07", 0, "rock_07", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"rock_09", 0, "rock_09", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"boulder_01", 0, "boulder_01", props::KIND_ROCK, true, {1500, 400, 100}, {1024, 512, 256}, true},
    {"stone_01", 0, "stone_01", props::KIND_ROCK, true, {1200, 350, 90}, {512, 256, 128}, true},
    {"namaqualand_boulder_03", 0, "nama_boulder_03", props::KIND_ROCK, true, {1800, 450, 110}, {1024, 512, 256}, true},
    {"namaqualand_boulder_04", 0, "nama_boulder_04", props::KIND_ROCK, true, {1800, 450, 110}, {1024, 512, 256}, true},
    {"namaqualand_boulder_05", 0, "nama_boulder_05", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"namaqualand_boulders_01", 0, "nama_boulders_01a", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"namaqualand_boulders_01", 1, "nama_boulders_01b", props::KIND_ROCK, true, {1500, 400, 100}, {512, 256, 128}, true},
    {"namaqualand_stones_01", 0, "nama_stone_a", props::KIND_ROCK, true, {1000, 300, 80}, {512, 256, 128}, true},
    {"namaqualand_stones_01", 1, "nama_stone_b", props::KIND_ROCK, true, {1000, 300, 80}, {512, 256, 128}, true},
    {"namaqualand_stones_01", 2, "nama_stone_c", props::KIND_ROCK, true, {1000, 300, 80}, {512, 256, 128}, true},
    {"namaqualand_stones_01", 3, "nama_stone_d", props::KIND_ROCK, true, {1000, 300, 80}, {512, 256, 128}, true},
    {"namaqualand_stones_01", 4, "nama_stone_e", props::KIND_ROCK, true, {1000, 300, 80}, {512, 256, 128}, true},
    // Cliffs: open surfaces, visual only.
    {"rock_face_01", 0, "cliff_face_01", props::KIND_CLIFF, true, {3000, 900, 250}, {1024, 512, 256}},
    {"rock_face_02", 0, "cliff_face_02", props::KIND_CLIFF, true, {3000, 900, 250}, {1024, 512, 256}},
    {"namaqualand_cliff_01", 0, "cliff_nama_01", props::KIND_CLIFF, true, {3500, 1000, 280}, {1024, 512, 256}},
    // Stumps, logs, branches.
    {"tree_stump_01", 0, "stump_01", props::KIND_STUMP, true, {1800, 500, 120}, {512, 256, 128}},
    {"tree_stump_02", 0, "stump_02", props::KIND_STUMP, true, {1800, 500, 120}, {512, 256, 128}},
    {"dead_tree_trunk_02", 0, "log_01", props::KIND_LOG, true, {2800, 800, 220}, {1024, 512, 256}, false, CENTER_ALL},
    {"dead_tree_trunk", 0, "snag_01", props::KIND_LOG, true, {2800, 800, 220}, {1024, 512, 256}, false, CENTER_BASE, true},
    {"dry_branches_medium_01", 0, "branches_a", props::KIND_BRANCHES, true, {1500, 450, 120}, {512, 256, 128}, false, CENTER_ALL},
    {"dry_branches_medium_01", 1, "branches_b", props::KIND_BRANCHES, true, {1500, 450, 120}, {512, 256, 128}, false, CENTER_ALL},
    {"dry_branches_medium_01", 2, "branches_c", props::KIND_BRANCHES, true, {1500, 450, 120}, {512, 256, 128}, false, CENTER_ALL},
};

}  // namespace

// ---------------------------------------------------------------------------------------
// Baking one solid unit
namespace {

void centreMesh(HiMesh& hm, const Unit& u) {
    if (u.upright) {
        // The trunk's long axis is x. Turn it upright with the thicker end down.
        double xmin = 1e30, xmax = -1e30;
        for (const V3& p : hm.p) xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x);
        auto thick = [&](bool low) {
            double mn = 1e30, mx = -1e30, zn = 1e30, zx = -1e30;
            const double lim = (xmax - xmin) * 0.15;
            for (const V3& p : hm.p)
                if (low ? p.x < xmin + lim : p.x > xmax - lim) mn = std::min(mn, p.y), mx = std::max(mx, p.y), zn = std::min(zn, p.z), zx = std::max(zx, p.z);
            return (mx - mn) + (zx - zn);
        };
        const bool lowThick = thick(true) > thick(false);
        // x -> y (up) (+x up when the thick end is at the low-x side would put it on top, so flip).
        for (size_t i = 0; i < hm.p.size(); i++) {
            auto rot = [&](const V3& v) { return lowThick ? V3(v.y, -v.x, v.z) : V3(-v.y, v.x, v.z); };
            hm.p[i] = rot(hm.p[i]);
            hm.n[i] = rot(hm.n[i]);
        }
    }
    V3 mn(1e30, 1e30, 1e30), mx(-1e30, -1e30, -1e30);
    for (const V3& p : hm.p) {
        mn = V3(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
        mx = V3(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
    }
    const V3 shift(-(mn.x + mx.x) / 2, u.mode == CENTER_ALL ? -(mn.y + mx.y) / 2 : -mn.y, -(mn.z + mx.z) / 2);
    for (V3& p : hm.p) p += shift;
}

bool bakeSolid(const Unit& u, props::HullLibrary& hulls, props::Model& model) {
    Gltf g;
    if (!g.load(u.src)) {
        std::fprintf(stderr, "cannot load %s (skipped)\n", u.src);
        return false;
    }
    HiMesh hm;
    int node = 0;
    for (size_t i = 0; i < g.d->nodes_count; i++) {
        if (!g.d->nodes[i].mesh) continue;
        if (u.mesh >= 0 && node++ != u.mesh) continue;
        float M[16];
        nodeMatrix(&g.d->nodes[i], M);
        for (size_t p = 0; p < g.d->nodes[i].mesh->primitives_count; p++) {
            addPrim(hm, g.d->nodes[i].mesh->primitives[p], M, false);
            if (hm.tex[0].w == 0) loadTextures(hm, g.dir, g.d->nodes[i].mesh->primitives[p].material);
        }
    }
    if (hm.t.empty()) {
        std::fprintf(stderr, "%s: no mesh %d in %s (skipped)\n", u.name, u.mesh, u.src);
        return false;
    }
    std::printf("%s: %zu hi-res tris\n", u.name, hm.t.size());
    centreMesh(hm, u);
    computeTangents(hm);
    Mesh welded = weld(hm);
    BVH bvh;
    bvh.build(hm);

    model.name = u.name;
    model.kind = u.kind;
    props::Material mat;
    mat.alphaMode = 0;
    mat.doubleSided = u.kind == props::KIND_CLIFF || u.kind == props::KIND_BRANCHES;
    model.mats.push_back(mat);
    Mesh cur = welded;
    for (int lod = 0; lod < 3; lod++) {
        cur = simplify(cur, u.tris[lod]);
        Unwrapped uw = unwrap(cur, u.tex[lod]);
        std::printf("  LOD%d: %zu tris, %zu verts, atlas %d\n", lod, uw.t.size(), uw.p.size(), u.tex[lod]);
        if (!gNoTex) {
            Baked bk = bake(uw, hm, bvh, u.tex[lod], u.cage);
            const std::string base = gOut + "/tex/" + u.name + "_" + std::to_string(lod);
            writeJpg(base + "_d.jpg", u.tex[lod], u.tex[lod], bk.diff, 86, false);
            writeJpg(base + "_n.jpg", u.tex[lod], u.tex[lod], bk.nor, 92, true);
            writeJpg(base + "_a.jpg", u.tex[lod], u.tex[lod], bk.arm, 82, false);
        }
        props::Material lm = mat;
        lm.tex[0] = std::string(u.name) + "_" + std::to_string(lod) + "_d.jpg";
        lm.tex[1] = std::string(u.name) + "_" + std::to_string(lod) + "_n.jpg";
        lm.tex[2] = std::string(u.name) + "_" + std::to_string(lod) + "_a.jpg";
        if (lod == 0) model.mats[0] = lm;
        else model.mats.push_back(lm);
        props::Lod L;
        L.maxDist = u.lodDist[lod];
        props::Sub s;
        s.mat = lod;
        for (size_t i = 0; i < uw.p.size(); i++) {
            props::Vert v;
            v.p[0] = (float)uw.p[i].x, v.p[1] = (float)uw.p[i].y, v.p[2] = (float)uw.p[i].z;
            v.n[0] = (float)uw.n[i].x, v.n[1] = (float)uw.n[i].y, v.n[2] = (float)uw.n[i].z;
            v.uv[0] = (float)uw.uv[i][0], v.uv[1] = (float)uw.uv[i][1];
            v.t[0] = (float)uw.tan[i].x, v.t[1] = (float)uw.tan[i].y, v.t[2] = (float)uw.tan[i].z, v.t[3] = uw.tw[i];
            s.v.push_back(v);
        }
        for (const Tri& t : uw.t)
            for (int k = 0; k < 3; k++) s.idx.push_back((uint16_t)t[k]);
        if (s.v.size() > 65535) {
            std::fprintf(stderr, "too many vertices\n");
            std::exit(1);
        }
        L.subs.push_back(std::move(s));
        model.lods.push_back(std::move(L));
    }
    V3 mn(1e30, 1e30, 1e30), mx(-1e30, -1e30, -1e30);
    for (const props::Vert& v : model.lods[0].subs[0].v)
        for (int k = 0; k < 3; k++) {
            ((double*)&mn)[k] = std::min(((double*)&mn)[k], (double)v.p[k]);
            ((double*)&mx)[k] = std::max(((double*)&mx)[k], (double)v.p[k]);
        }
    model.bmin[0] = (float)mn.x, model.bmin[1] = (float)mn.y, model.bmin[2] = (float)mn.z;
    model.bmax[0] = (float)mx.x, model.bmax[1] = (float)mx.y, model.bmax[2] = (float)mx.z;
    if (u.hull) {
        double miss = 0;
        hulls.hulls.push_back(props::buildRockHull(u.name, welded.p, 48, &miss));
        const props::RockHull& h = hulls.hulls.back();
        std::printf("    hull: %zu corners, %zu faces, worst miss %.1f mm\n", h.v.size(), h.f.size(), miss * 1000);
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Cards (plants and trees)
namespace {

struct Card {
    const char* src;
    int mesh;           // node index, -1 = all nodes with this prefix? (single mesh each)
    const char* name;
    Kind kind;
    const char* texBase;  // output texture base (shared by variants) per material slot
    float keep[3];        // fractions of pieces kept per LOD
    float maxScale;
    float wind;
    bool tree;
    float lodDist[3];
    int texSize;
    int budget[3];        // triangles per LOD at most (the share kept shrinks to fit a dense scan)
};

const Card kCards[] = {
    {"shrub_02", 0, "shrub_02_a", props::KIND_PLANT, "shrub_02", {1.0f, 0.4f, 0.12f}, 2.2f, 0.035f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_02", 1, "shrub_02_b", props::KIND_PLANT, "shrub_02", {1.0f, 0.4f, 0.12f}, 2.2f, 0.035f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_02", 2, "shrub_02_c", props::KIND_PLANT, "shrub_02", {1.0f, 0.4f, 0.12f}, 2.2f, 0.035f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_02", 3, "shrub_02_d", props::KIND_PLANT, "shrub_02", {1.0f, 0.4f, 0.12f}, 2.2f, 0.035f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_03", 0, "shrub_03_a", props::KIND_PLANT, "shrub_03", {1.0f, 0.4f, 0.12f}, 2.0f, 0.04f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_03", 1, "shrub_03_b", props::KIND_PLANT, "shrub_03", {1.0f, 0.4f, 0.12f}, 2.0f, 0.04f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_03", 2, "shrub_03_c", props::KIND_PLANT, "shrub_03", {1.0f, 0.4f, 0.12f}, 2.0f, 0.04f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_03", 3, "shrub_03_d", props::KIND_PLANT, "shrub_03", {1.0f, 0.4f, 0.12f}, 2.0f, 0.04f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"shrub_04", 0, "shrub_04_a", props::KIND_PLANT, "shrub_04", {0.5f, 0.15f, 0.05f}, 2.2f, 0.03f, false, {20, 55, 1e9f}, 512, {5000, 1500, 400}},
    {"fern_02", 0, "fern_02_a", props::KIND_PLANT, "fern_02", {1.0f, 0.5f, 0.2f}, 1.8f, 0.05f, false, {18, 45, 1e9f}, 512, {2500, 800, 250}},
    {"fern_02", 1, "fern_02_b", props::KIND_PLANT, "fern_02", {1.0f, 0.5f, 0.2f}, 1.8f, 0.05f, false, {18, 45, 1e9f}, 512, {2500, 800, 250}},
    {"fern_02", 2, "fern_02_c", props::KIND_PLANT, "fern_02", {1.0f, 0.5f, 0.2f}, 1.8f, 0.05f, false, {18, 45, 1e9f}, 512, {2500, 800, 250}},
    {"fern_02", 3, "fern_02_d", props::KIND_PLANT, "fern_02", {1.0f, 0.5f, 0.2f}, 1.8f, 0.05f, false, {18, 45, 1e9f}, 512, {2500, 800, 250}},
    {"grass_medium_02", 0, "grass_02_a", props::KIND_PLANT, "grass_medium_02", {1.0f, 0.5f, 0.2f}, 1.7f, 0.12f, false, {15, 35, 1e9f}, 512, {1500, 500, 150}},
    {"grass_medium_02", 1, "grass_02_b", props::KIND_PLANT, "grass_medium_02", {1.0f, 0.5f, 0.2f}, 1.7f, 0.12f, false, {15, 35, 1e9f}, 512, {1500, 500, 150}},
    {"grass_medium_02", 2, "grass_02_c", props::KIND_PLANT, "grass_medium_02", {1.0f, 0.5f, 0.2f}, 1.7f, 0.12f, false, {15, 35, 1e9f}, 512, {1500, 500, 150}},
    {"grass_medium_02", 3, "grass_02_d", props::KIND_PLANT, "grass_medium_02", {1.0f, 0.5f, 0.2f}, 1.7f, 0.12f, false, {15, 35, 1e9f}, 512, {1500, 500, 150}},
    {"grass_medium_02", 4, "grass_02_e", props::KIND_PLANT, "grass_medium_02", {1.0f, 0.5f, 0.2f}, 1.7f, 0.12f, false, {15, 35, 1e9f}, 512, {1500, 500, 150}},
    {"fir_sapling", 0, "fir_a", props::KIND_TREE, "fir_sapling", {0.22f, 0.05f, 0.012f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
    {"fir_sapling", 1, "fir_b", props::KIND_TREE, "fir_sapling", {0.22f, 0.05f, 0.012f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
    {"fir_sapling", 2, "fir_c", props::KIND_TREE, "fir_sapling", {0.22f, 0.05f, 0.012f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
    {"pine_sapling_small", 1, "pine_a", props::KIND_TREE, "pine_sapling_small", {0.30f, 0.08f, 0.02f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
    {"pine_sapling_small", 0, "pine_b", props::KIND_TREE, "pine_sapling_small", {0.30f, 0.08f, 0.02f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
    {"pine_sapling_small", 2, "pine_c", props::KIND_TREE, "pine_sapling_small", {0.30f, 0.08f, 0.02f}, 3.0f, 0.02f, true, {18, 60, 1e9f}, 512, {14000, 3500, 900}},
};

struct DSU {
    std::vector<int> p;
    explicit DSU(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int f(int x) {
        while (p[x] != x) p[x] = p[p[x]], x = p[x];
        return x;
    }
    void u(int a, int b) { p[f(a)] = f(b); }
};

uint32_t hash32(uint32_t x) {
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return x;
}

// One material slot's pieces (connected triangle groups) of a card model.
struct Piece {
    std::vector<int> tris;
    V3 anchor;
    double order;
};

// An image with alpha (the cards' diffuse), nearest-texel lookups with repeat.
struct Img4 {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool load4(const std::string& path) {
        int c = 0;
        uint8_t* d = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!d) {
            w = h = 0;
            return false;
        }
        px.assign(d, d + (size_t)w * h * 4);
        stbi_image_free(d);
        return true;
    }
    const uint8_t* at(double u, double v) const {
        int x = (int)std::floor(u * w) % w, y = (int)std::floor(v * h) % h;
        if (x < 0) x += w;
        if (y < 0) y += h;
        return &px[((size_t)y * w + x) * 4];
    }
};

// Far-view billboard of a tree: its LOD0 drawn orthographically from kBillboardViews sides
// (view k looks from the direction (cos a, 0, sin a), a = 2 pi k / views, toward the trunk),
// side by side in one atlas: <name>_bb_d.png (sRGB colour, alpha = coverage) and
// <name>_bb_n.jpg (the tree's own-frame normal, 0.5 + 0.5 n, turned to face the viewer).
// The quad spans x in [-R, R] round the trunk, y in [0, height] (R = the widest reach).
// Rendered at 4x and boxed down; colour is spread into the empty texels so mipmaps don't
// darken the edges.
const int kBillboardViews = 8, kBillboardW = 128, kBillboardH = 256;
void bakeBillboard(const props::Model& m, const std::vector<Img4>& tex) {
    if (gNoTex || m.lods.empty()) return;
    const int SS = 4, W = kBillboardW * SS, H = kBillboardH * SS;
    double R = 0.01;
    for (const props::Sub& s : m.lods[0].subs)
        for (const props::Vert& v : s.v) R = std::max(R, std::hypot((double)v.p[0], (double)v.p[2]));
    const double top = std::max(0.01f, m.bmax[1]);
    const int AW = kBillboardW * kBillboardViews, AH = kBillboardH;
    std::vector<uint8_t> outD((size_t)AW * AH * 4, 0), outN((size_t)AW * AH * 3, 128);
    for (int view = 0; view < kBillboardViews; view++) {
        const double a = 2 * M_PI * view / kBillboardViews;
        const V3 e(std::cos(a), 0, std::sin(a)), up(0, 1, 0), r = cross(up, e);
        std::vector<float> zb((size_t)W * H, -1e30f);
        std::vector<float> col((size_t)W * H * 3, 0.0f), nrm((size_t)W * H * 3, 0.0f);
        std::vector<char> cov((size_t)W * H, 0);
        for (const props::Sub& s : m.lods[0].subs) {
            const props::Material& mt = m.mats[s.mat];
            const Img4* img = s.mat < (int)tex.size() && tex[s.mat].w ? &tex[s.mat] : nullptr;
            for (size_t t = 0; t + 2 < s.idx.size(); t += 3) {
                const props::Vert* vv[3] = {&s.v[s.idx[t]], &s.v[s.idx[t + 1]], &s.v[s.idx[t + 2]]};
                double X[3], Y[3], Z[3];
                for (int k = 0; k < 3; k++) {
                    const V3 p(vv[k]->p[0], vv[k]->p[1], vv[k]->p[2]);
                    X[k] = (dot(p, r) / R * 0.5 + 0.5) * W;
                    Y[k] = (1.0 - p.y / top) * H;  // row 0 at the top
                    Z[k] = dot(p, e);
                }
                const double area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
                if (std::fabs(area) < 1e-12) continue;
                const int x0 = std::max(0, (int)std::floor(std::min({X[0], X[1], X[2]}))), x1 = std::min(W - 1, (int)std::ceil(std::max({X[0], X[1], X[2]})));
                const int y0 = std::max(0, (int)std::floor(std::min({Y[0], Y[1], Y[2]}))), y1 = std::min(H - 1, (int)std::ceil(std::max({Y[0], Y[1], Y[2]})));
                for (int py = y0; py <= y1; py++)
                    for (int px = x0; px <= x1; px++) {
                        const double cx = px + 0.5, cy = py + 0.5;
                        const double w0 = ((X[1] - cx) * (Y[2] - cy) - (X[2] - cx) * (Y[1] - cy)) / area;
                        const double w1 = ((X[2] - cx) * (Y[0] - cy) - (X[0] - cx) * (Y[2] - cy)) / area;
                        const double w2 = 1 - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        const double z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2];
                        const size_t o = (size_t)py * W + px;
                        if (z <= zb[o]) continue;
                        const double u = w0 * vv[0]->uv[0] + w1 * vv[1]->uv[0] + w2 * vv[2]->uv[0];
                        const double v = w0 * vv[0]->uv[1] + w1 * vv[1]->uv[1] + w2 * vv[2]->uv[1];
                        float c3[3] = {0.25f, 0.2f, 0.15f};
                        if (img) {
                            const uint8_t* q = img->at(u, v);
                            if (mt.alphaMode && q[3] < mt.cutoff * 255) continue;
                            for (int k = 0; k < 3; k++) c3[k] = q[k] / 255.0f;
                        }
                        V3 n = norm(V3(vv[0]->n[0], vv[0]->n[1], vv[0]->n[2]) * w0 + V3(vv[1]->n[0], vv[1]->n[1], vv[1]->n[2]) * w1 +
                                    V3(vv[2]->n[0], vv[2]->n[1], vv[2]->n[2]) * w2);
                        if (dot(n, e) < 0) n = -n;  // two-sided cards face the viewer
                        zb[o] = (float)z;
                        cov[o] = 1;
                        for (int k = 0; k < 3; k++) col[o * 3 + k] = toLinear(c3[k]);
                        nrm[o * 3] = (float)n.x, nrm[o * 3 + 1] = (float)n.y, nrm[o * 3 + 2] = (float)n.z;
                    }
            }
        }
        // Box down to the atlas cell.
        for (int y = 0; y < kBillboardH; y++)
            for (int x = 0; x < kBillboardW; x++) {
                double cs[3] = {0, 0, 0}, ns[3] = {0, 0, 0};
                int n = 0;
                for (int sy = 0; sy < SS; sy++)
                    for (int sx = 0; sx < SS; sx++) {
                        const size_t o = (size_t)(y * SS + sy) * W + (x * SS + sx);
                        if (!cov[o]) continue;
                        n++;
                        for (int k = 0; k < 3; k++) cs[k] += col[o * 3 + k], ns[k] += nrm[o * 3 + k];
                    }
                const size_t q = (size_t)y * AW + view * kBillboardW + x;
                outD[q * 4 + 3] = (uint8_t)std::lround(255.0 * n / (SS * SS));
                if (!n) continue;
                for (int k = 0; k < 3; k++) outD[q * 4 + k] = (uint8_t)std::lround(255 * clampd(toSrgb((float)(cs[k] / n)), 0, 1));
                const V3 nn = norm(V3(ns[0], ns[1], ns[2]));
                outN[q * 3] = (uint8_t)std::lround(255 * (nn.x * 0.5 + 0.5)), outN[q * 3 + 1] = (uint8_t)std::lround(255 * (nn.y * 0.5 + 0.5)),
                outN[q * 3 + 2] = (uint8_t)std::lround(255 * (nn.z * 0.5 + 0.5));
            }
    }
    // Spread colour (and normals) outward into the empty texels, within each view's cell.
    std::vector<char> valid((size_t)AW * AH);
    for (size_t i = 0; i < valid.size(); i++) valid[i] = outD[i * 4 + 3] > 0;
    for (int pass = 0; pass < 16; pass++) {
        std::vector<char> nv = valid;
        std::vector<uint8_t> d2 = outD, n2 = outN;
        for (int y = 0; y < AH; y++)
            for (int x = 0; x < AW; x++) {
                const size_t i = (size_t)y * AW + x;
                if (valid[i]) continue;
                int acc[6] = {0, 0, 0, 0, 0, 0}, cnt = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int qx = x + dx, qy = y + dy;
                        if (qx < 0 || qy < 0 || qx >= AW || qy >= AH || qx / kBillboardW != x / kBillboardW) continue;
                        const size_t j = (size_t)qy * AW + qx;
                        if (!valid[j]) continue;
                        for (int k = 0; k < 3; k++) acc[k] += outD[j * 4 + k], acc[3 + k] += outN[j * 3 + k];
                        cnt++;
                    }
                if (!cnt) continue;
                for (int k = 0; k < 3; k++) d2[i * 4 + k] = (uint8_t)(acc[k] / cnt), n2[i * 3 + k] = (uint8_t)(acc[3 + k] / cnt);
                nv[i] = 1;
            }
        valid.swap(nv);
        outD.swap(d2);
        outN.swap(n2);
    }
    const std::string base = gOut + "/tex/" + m.name + "_bb";
    {
        const std::string tmp = base + "_d.pam";
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return;
        std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", AW, AH);
        std::fwrite(outD.data(), 1, outD.size(), f);
        std::fclose(f);
        const std::string cmd = "convert '" + tmp + "' -define png:compression-level=9 '" + base + "_d.png'";
        if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed: %s\n", cmd.c_str());
        std::remove(tmp.c_str());
    }
    writeJpg(base + "_n.jpg", AW, AH, outN, 90, true);
    std::printf("    billboard: %d views, %dx%d, R %.2f m, height %.2f m\n", kBillboardViews, AW, AH, R, top);
}

void convertTexture(const std::string& srcDir, const std::string& srcRel, const std::string& outName, int size, int quality, bool full444) {
    if (gNoTex) return;
    const std::string dst = gOut + "/tex/" + outName;
    const std::string src = srcDir + srcRel;
    const bool png = outName.size() > 4 && outName.compare(outName.size() - 4, 4, ".png") == 0;
    const std::string cmd = "convert '" + src + "' -resize " + std::to_string(size) + "x" + std::to_string(size) +
                            (png ? std::string(" -define png:compression-level=9") : " -sampling-factor " + std::string(full444 ? "4:4:4" : "4:2:0") + " -quality " + std::to_string(quality)) +
                            " '" + dst + "'";
    if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed: %s\n", cmd.c_str());
}

bool bakeCard(const Card& c, props::Model& model) {
    Gltf g;
    if (!g.load(c.src)) {
        std::fprintf(stderr, "cannot load %s (skipped)\n", c.src);
        return false;
    }
    // Find the node, one HiMesh per primitive (each has its own material).
    int node = 0;
    const cgltf_node* nd = nullptr;
    for (size_t i = 0; i < g.d->nodes_count; i++) {
        if (!g.d->nodes[i].mesh) continue;
        if (node++ == c.mesh) nd = &g.d->nodes[i];
    }
    if (!nd) {
        std::fprintf(stderr, "no node %d in %s (skipped)\n", c.mesh, c.src);
        return false;
    }
    std::vector<HiMesh> prims(nd->mesh->primitives_count);
    std::vector<const cgltf_material*> mats(prims.size());
    float M[16];
    nodeMatrix(nd, M);
    for (size_t p = 0; p < prims.size(); p++) {
        addPrim(prims[p], nd->mesh->primitives[p], M, false);
        mats[p] = nd->mesh->primitives[p].material;
    }
    // Recentre: x, z on the trunk (origin), y on the lowest point.
    double ymin = 1e30;
    for (const HiMesh& hm : prims)
        for (const V3& p : hm.p) ymin = std::min(ymin, p.y);
    for (HiMesh& hm : prims) {
        for (V3& p : hm.p) p.y -= ymin;
        computeTangents(hm);
    }
    model.name = c.name;
    model.kind = c.kind;
    std::vector<Img4> srcTex;  // the diffuse with its alpha, per material (trees: for the billboards)
    // Materials: one per primitive, textures shared by name across variants.
    for (size_t p = 0; p < prims.size(); p++) {
        props::Material m;
        const cgltf_material* gm = mats[p];
        m.doubleSided = true;
        m.foliage = true;
        m.alphaMode = gm ? (int)gm->alpha_mode : 0;
        m.wind = c.wind;
        std::string tag = c.texBase;
        if (c.tree) {
            // fir: branches / twigs; pine: bark / twig.
            const std::string nm = gm && gm->name ? gm->name : "";
            const bool bark = nm.find("branches") != std::string::npos || nm.find("bark") != std::string::npos;
            tag = nm.empty() ? std::string(c.texBase) + std::to_string(p) : nm;
            m.foliage = !bark;
            if (bark) m.wind = c.wind * 0.3f;
        }
        const std::string dir = g.dir;
        auto texName = [&](const cgltf_texture* t) { return t && t->image && t->image->uri ? std::string(t->image->uri) : std::string(); };
        const std::string d = gm ? texName(gm->pbr_metallic_roughness.base_color_texture.texture) : "", n = gm ? texName(gm->normal_texture.texture) : "",
                          a = gm ? texName(gm->pbr_metallic_roughness.metallic_roughness_texture.texture) : "";
        const std::string base = tag;
        if (m.alphaMode == 2) m.alphaMode = 1;  // (blended leaves are drawn cut out: no sorting)
        if (gm && gm->alpha_mode != cgltf_alpha_mode_opaque) m.cutoff = gm->alpha_cutoff > 0 ? gm->alpha_cutoff : 0.5f;
        // Cut-out cards keep the diffuse's alpha (PNG); a texture shared by several variants
        // is converted once.
        const std::string dName = base + (m.alphaMode ? "_d.png" : "_d.jpg");
        static std::set<std::string> done;
        auto conv = [&](const std::string& rel, const std::string& out, int q, bool full) {
            if (done.insert(out).second) convertTexture(dir, rel, out, c.texSize, q, full);
        };
        if (!d.empty()) conv(d, dName, 84, false), m.tex[0] = dName;
        if (!n.empty()) conv(n, base + "_n.jpg", 90, true), m.tex[1] = base + "_n.jpg";
        if (!a.empty()) conv(a, base + "_a.jpg", 80, false), m.tex[2] = base + "_a.jpg";
        model.mats.push_back(m);
        srcTex.emplace_back();
        if (c.tree && !d.empty()) srcTex.back().load4(dir + d);
    }
    // Pieces (connected triangle groups by position) of every primitive, once.
    struct PrimPieces {
        std::vector<Piece> pieces;
        bool bark = false;
        int tris = 0;
    };
    std::vector<PrimPieces> pp(prims.size());
    int leafTotal = 0, barkTotal = 0;
    for (size_t p = 0; p < prims.size(); p++) {
        HiMesh& hm = prims[p];
        std::map<std::array<int64_t, 3>, int> pid;
        std::vector<int> wid(hm.p.size());
        for (size_t i = 0; i < hm.p.size(); i++) {
            std::array<int64_t, 3> k = {(int64_t)std::llround(hm.p[i].x * 1e5), (int64_t)std::llround(hm.p[i].y * 1e5), (int64_t)std::llround(hm.p[i].z * 1e5)};
            auto it = pid.find(k);
            if (it == pid.end()) it = pid.emplace(k, (int)pid.size()).first;
            wid[i] = it->second;
        }
        DSU ds((int)pid.size());
        for (const Tri& t : hm.t) ds.u(wid[t[0]], wid[t[1]]), ds.u(wid[t[1]], wid[t[2]]);
        std::map<int, Piece> pieces;
        for (size_t i = 0; i < hm.t.size(); i++) pieces[ds.f(wid[hm.t[i][0]])].tris.push_back((int)i);
        pp[p].bark = c.tree && !model.mats[p].foliage;
        for (auto& kv : pieces) {
            Piece pc = kv.second;
            // Anchor: the piece's vertex nearest the model's vertical axis (where it joins the stem).
            double best = 1e30;
            for (int ti : pc.tris)
                for (int k = 0; k < 3; k++) {
                    const V3& q = hm.p[hm.t[ti][k]];
                    const double dd = std::hypot(q.x, q.z);
                    if (dd < best) best = dd, pc.anchor = q;
                }
            pc.order = hash32((uint32_t)kv.first * 2654435761u + (uint32_t)p) / 4294967296.0;
            pp[p].pieces.push_back(pc);
        }
        // Bark: the biggest pieces first (the trunk, then the main limbs).
        if (pp[p].bark)
            std::sort(pp[p].pieces.begin(), pp[p].pieces.end(), [](const Piece& a, const Piece& b) { return a.tris.size() > b.tris.size(); });
        pp[p].tris = (int)hm.t.size();
        (pp[p].bark ? barkTotal : leafTotal) += pp[p].tris;
    }
    for (int lod = 0; lod < 3; lod++) {
        props::Lod L;
        L.maxDist = c.lodDist[lod];
        // The triangle budget: leaves get three quarters of it on a tree, bark the rest.
        const double leafBudget = c.budget[lod] * (barkTotal > 0 ? 0.75 : 1.0), barkBudget = c.budget[lod] - (leafTotal > 0 ? leafBudget : 0.0);
        const double leafKeep = leafTotal > 0 ? std::min((double)c.keep[lod], leafBudget / leafTotal) : 1.0;
        for (size_t p = 0; p < prims.size(); p++) {
            HiMesh& hm = prims[p];
            const bool bark = pp[p].bark;
            const double keep = bark ? 1.0 : leafKeep;
            const double sc = bark ? 1.0 : std::min((double)c.maxScale, 1.0 / std::sqrt(std::max(keep, 1e-3)));
            props::Sub s;
            s.mat = (int)p;
            std::map<int, int> vmap;
            int barkTris = 0;
            // Leaves are kept in the order of a shuffled hash, so the survivors stay spread out.
            for (size_t pi = 0; pi < pp[p].pieces.size(); pi++) {
                const Piece* pc = &pp[p].pieces[pi];
                if (bark) {
                    if (pi > 0 && barkTris + (int)pc->tris.size() > barkBudget) continue;
                    barkTris += (int)pc->tris.size();
                } else if (pc->order >= keep)
                    continue;
                vmap.clear();
                for (int ti : pc->tris)
                    for (int k = 0; k < 3; k++) {
                        const int vi = hm.t[ti][k];
                        auto it = vmap.find(vi);
                        if (it == vmap.end()) {
                            props::Vert v;
                            V3 q = pc->anchor + (hm.p[vi] - pc->anchor) * sc;
                            v.p[0] = (float)q.x, v.p[1] = (float)q.y, v.p[2] = (float)q.z;
                            v.n[0] = (float)hm.n[vi].x, v.n[1] = (float)hm.n[vi].y, v.n[2] = (float)hm.n[vi].z;
                            v.uv[0] = (float)hm.uv[vi][0], v.uv[1] = (float)hm.uv[vi][1];
                            v.t[0] = (float)hm.T[vi].x, v.t[1] = (float)hm.T[vi].y, v.t[2] = (float)hm.T[vi].z, v.t[3] = hm.tw[vi];
                            s.v.push_back(v);
                            it = vmap.emplace(vi, (int)s.v.size() - 1).first;
                        }
                        s.idx.push_back((uint16_t)it->second);
                    }
                if (s.v.size() > 65000) break;
            }
            if (!s.idx.empty()) L.subs.push_back(std::move(s));
        }
        std::printf("%s LOD%d: %d tris\n", c.name, lod, L.tris());
        model.lods.push_back(std::move(L));
    }
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const props::Sub& s : model.lods[0].subs)
        for (const props::Vert& v : s.v)
            for (int k = 0; k < 3; k++) mn[k] = std::fmin(mn[k], v.p[k]), mx[k] = std::fmax(mx[k], v.p[k]);
    for (int k = 0; k < 3; k++) model.bmin[k] = mn[k], model.bmax[k] = mx[k];
    if (c.tree) bakeBillboard(model, srcTex);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string only;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--src" && i + 1 < argc) gSrc = argv[++i];
        else if (a == "--out" && i + 1 < argc) gOut = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--no-tex") gNoTex = true;
        else {
            std::fprintf(stderr, "usage: bakeprops [--src DIR] [--out DIR] [--only NAME] [--no-tex]\n");
            return 1;
        }
    }
    const std::string mk = "mkdir -p '" + gOut + "/tex'";
    if (std::system(mk.c_str()) != 0) return 1;
    props::HullLibrary hulls;
    // Hulls of models not rebuilt this run are kept.
    {
        props::HullLibrary old;
        if (old.load(gOut)) hulls = old;
    }
    auto matches = [&](const char* name) { return only.empty() || only == name || std::string(name).find(only) != std::string::npos; };
    int skipped = 0;
    for (const Unit& u : kUnits) {
        if (!matches(u.name)) continue;
        props::Model m;
        for (size_t i = 0; i < hulls.hulls.size(); i++)
            if (hulls.hulls[i].name == u.name) hulls.hulls.erase(hulls.hulls.begin() + i--);
        if (!bakeSolid(u, hulls, m)) {
            skipped++;
            continue;
        }
        if (!props::saveModel(gOut + "/" + u.name + ".prop", m)) return 1;
    }
    for (const Card& c : kCards) {
        if (!matches(c.name)) continue;
        props::Model m;
        if (!bakeCard(c, m)) {
            skipped++;
            continue;
        }
        if (!props::saveModel(gOut + "/" + c.name + ".prop", m)) return 1;
    }
    // The manifest: every model whose file is there (from this run or an earlier one).
    {
        FILE* f = std::fopen((gOut + "/props.txt").c_str(), "w");
        if (!f) return 1;
        auto put = [&](const char* name, int kind) {
            if (FILE* t = std::fopen((gOut + "/" + name + ".prop").c_str(), "rb")) {
                std::fclose(t);
                std::fprintf(f, "%s %d\n", name, kind);
            }
        };
        for (const Unit& u : kUnits) put(u.name, u.kind);
        for (const Card& c : kCards) put(c.name, c.kind);
        std::fclose(f);
    }
    // Hulls in a stable order.
    std::sort(hulls.hulls.begin(), hulls.hulls.end(), [](const props::RockHull& a, const props::RockHull& b) { return a.name < b.name; });
    if (!hulls.save(gOut + "/rocks.hull")) return 1;
    std::printf("done: %zu hulls%s\n", hulls.hulls.size(), skipped ? " (some sources missing: see above)" : "");
    return skipped ? 2 : 0;
}
