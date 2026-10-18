#include "meshopt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <unordered_map>

#include "mathx.h"
#include "modelkit.h"

MeshOptStats& MeshOptStats::operator+=(const MeshOptStats& o) {
    trianglesIn += o.trianglesIn, trianglesOut += o.trianglesOut;
    degenerate += o.degenerate, repeated += o.repeated, hidden += o.hidden, buried += o.buried;
    coplanarSaved += o.coplanarSaved, verticesIn += o.verticesIn, uniqueVertices += o.uniqueVertices;
    return *this;
}

namespace {

V3 pos(const OptVert& v) { return V3(v.p[0], v.p[1], v.p[2]); }

// The shader's material, whatever's added to it (sunlit +100, wear +200 a step).
int baseMat(float c3) { return (int)std::lround(c3) % 100; }
bool seeThrough(int mat) { return mat == mk::GLASS || mat == mk::FOLIAGE || mat == mk::SNOWLEAF; }

// The same shading at two vertices: normal, colour and material.
bool sameLook(const OptVert& a, const OptVert& b) {
    for (int k = 0; k < 3; k++)
        if (std::fabs(a.n[k] - b.n[k]) > 1e-5f || std::fabs(a.c[k] - b.c[k]) > 1e-5f) return false;
    return a.c[3] == b.c[3];
}

struct Hash3 {
    size_t operator()(const std::array<int64_t, 3>& k) const {
        return (size_t)(k[0] * 73856093) ^ (size_t)(k[1] * 19349663) ^ (size_t)(k[2] * 83492791);
    }
};

// One id per place: corners within eps of one another (looked for in the neighbouring cells too) share it.
std::vector<int> weld(const std::vector<OptVert>& v, double eps, std::vector<V3>& where) {
    std::unordered_map<std::array<int64_t, 3>, std::vector<int>, Hash3> grid;
    std::vector<int> id(v.size());
    where.clear();
    for (size_t i = 0; i < v.size(); i++) {
        const V3 p = pos(v[i]);
        const int64_t cx = (int64_t)std::floor(p.x / eps), cy = (int64_t)std::floor(p.y / eps), cz = (int64_t)std::floor(p.z / eps);
        int found = -1;
        for (int dx = -1; dx <= 1 && found < 0; dx++)
            for (int dy = -1; dy <= 1 && found < 0; dy++)
                for (int dz = -1; dz <= 1 && found < 0; dz++) {
                    auto it = grid.find({cx + dx, cy + dy, cz + dz});
                    if (it == grid.end()) continue;
                    for (int q : it->second) {
                        const V3 d = where[q] - p;
                        if (std::fabs(d.x) <= eps && std::fabs(d.y) <= eps && std::fabs(d.z) <= eps) {
                            found = q;
                            break;
                        }
                    }
                }
        if (found < 0) {
            found = (int)where.size();
            where.push_back(p);
            grid[{cx, cy, cz}].push_back(found);
        }
        id[i] = found;
    }
    return id;
}

uint64_t fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    return h;
}

size_t countUnique(const std::vector<OptVert>& v) {  // (by hash: a count, nothing hangs on it)
    std::vector<uint64_t> h(v.size());
    for (size_t i = 0; i < v.size(); i++) h[i] = fnv(&v[i], sizeof(OptVert));
    std::sort(h.begin(), h.end());
    return (size_t)(std::unique(h.begin(), h.end()) - h.begin());
}

struct UF {
    std::vector<int> p;
    explicit UF(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) {
        while (p[x] != x) x = p[x] = p[p[x]];
        return x;
    }
    void join(int a, int b) { p[find(a)] = find(b); }
};

uint64_t edgeKey(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }

struct Tri {
    int a, b, c;  // place ids
    V3 gn;        // unit geometric normal
    double area;
    bool alive = true;
};

// ---- Hidden faces ---------------------------------------------------------------------------------

struct Plane {
    V3 n;
    double d;
};
struct Solid {
    std::vector<Plane> planes;  // outward
    V3 lo, hi;
};

void dropHidden(std::vector<Tri>& tris, const std::vector<OptVert>& v, const std::vector<V3>& where, double eps, MeshOptStats& st) {
    const int nt = (int)tris.size();
    UF uf(where.size());
    for (const Tri& t : tris)
        if (t.alive) uf.join(t.a, t.b), uf.join(t.a, t.c);
    std::unordered_map<int, std::vector<int>> parts;  // root place -> its triangles
    for (int i = 0; i < nt; i++)
        if (tris[i].alive) parts[uf.find(tris[i].a)].push_back(i);
    if (parts.size() < 2) return;
    // Which part each triangle is in, whether that part is closed and turned outward.
    std::vector<int> partOf(nt, -1);
    std::vector<char> closedOut;
    std::vector<int> solidOf;  // part -> solid index or -1
    std::vector<Solid> solids;
    int pi = 0;
    for (auto& kv : parts) {
        const std::vector<int>& ts = kv.second;
        for (int t : ts) partOf[t] = pi;
        std::unordered_map<uint64_t, int> edges;
        for (int t : ts) {
            const Tri& r = tris[t];
            edges[edgeKey(r.a, r.b)]++, edges[edgeKey(r.b, r.c)]++, edges[edgeKey(r.c, r.a)]++;
        }
        bool closed = true;
        for (auto& e : edges) {
            const int a = (int)(e.first >> 32), b = (int)(e.first & 0xffffffffu);
            auto it = edges.find(edgeKey(b, a));
            if (e.second != 1 || it == edges.end() || it->second != 1) {
                closed = false;
                break;
            }
        }
        double vol = 0;
        V3 c0 = where[tris[ts[0]].a];
        bool clear = false;
        for (int t : ts) {
            const Tri& r = tris[t];
            vol += dot(where[r.a] - c0, cross(where[r.b] - c0, where[r.c] - c0)) / 6;
            for (int k = 0; k < 3; k++) clear |= seeThrough(baseMat(v[t * 3 + k].c[3]));
        }
        const bool outward = closed && vol > 1e-12;
        closedOut.push_back(outward);
        solidOf.push_back(-1);
        if (outward && !clear && ts.size() <= 4000) {
            // Convex: every corner of it on the inside of every face's plane.
            Solid s;
            s.lo = V3(1e30, 1e30, 1e30), s.hi = V3(-1e30, -1e30, -1e30);
            std::vector<int> corners;
            for (int t : ts)
                for (int q : {tris[t].a, tris[t].b, tris[t].c}) corners.push_back(q);
            std::sort(corners.begin(), corners.end());
            corners.erase(std::unique(corners.begin(), corners.end()), corners.end());
            for (int q : corners) {
                const V3& p = where[q];
                s.lo = V3(std::min(s.lo.x, p.x), std::min(s.lo.y, p.y), std::min(s.lo.z, p.z));
                s.hi = V3(std::max(s.hi.x, p.x), std::max(s.hi.y, p.y), std::max(s.hi.z, p.z));
            }
            bool convex = true;
            for (int t : ts) {
                const Tri& r = tris[t];
                const Plane pl{r.gn, dot(r.gn, where[r.a])};
                bool have = false;
                for (const Plane& o : s.planes)
                    if (dot(o.n, pl.n) > 1 - 1e-9 && std::fabs(o.d - pl.d) < eps) have = true;
                if (have) continue;
                for (int q : corners)
                    if (dot(pl.n, where[q]) - pl.d > eps) {
                        convex = false;
                        break;
                    }
                if (!convex) break;
                s.planes.push_back(pl);
            }
            if (convex && s.planes.size() >= 4) solidOf.back() = (int)solids.size(), solids.push_back(s);
        }
        pi++;
    }
    if (solids.empty()) return;
    std::vector<int> solidPart(solids.size());
    for (int p = 0; p < (int)solidOf.size(); p++)
        if (solidOf[p] >= 0) solidPart[solidOf[p]] = p;
    // The solids in a grid over x, z (the big ones in a list of their own).
    V3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
    for (const V3& p : where) lo = V3(std::min(lo.x, p.x), 0, std::min(lo.z, p.z)), hi = V3(std::max(hi.x, p.x), 0, std::max(hi.z, p.z));
    const double cell = std::max({(hi.x - lo.x) / 64, (hi.z - lo.z) / 64, 0.25});
    const int gw = (int)((hi.x - lo.x) / cell) + 1, gh = (int)((hi.z - lo.z) / cell) + 1;
    std::vector<std::vector<int>> grid((size_t)gw * gh);
    auto cellOf = [&](double x, double z, int& i, int& j) {
        i = std::clamp((int)((x - lo.x) / cell), 0, gw - 1), j = std::clamp((int)((z - lo.z) / cell), 0, gh - 1);
    };
    for (int s = 0; s < (int)solids.size(); s++) {
        int i0, j0, i1, j1;
        cellOf(solids[s].lo.x - eps, solids[s].lo.z - eps, i0, j0), cellOf(solids[s].hi.x + eps, solids[s].hi.z + eps, i1, j1);
        for (int i = i0; i <= i1; i++)
            for (int j = j0; j <= j1; j++) grid[(size_t)i * gh + j].push_back(s);
    }
    for (int t = 0; t < nt; t++) {
        Tri& r = tris[t];
        if (!r.alive) continue;
        const V3 p[3] = {where[r.a], where[r.b], where[r.c]};
        int ci, cj;
        cellOf(p[0].x, p[0].z, ci, cj);
        for (int s : grid[(size_t)ci * gh + cj]) {
            if (solidPart[s] == partOf[t]) continue;
            const Solid& S = solids[s];
            bool inBox = true;
            for (const V3& q : p)
                inBox &= q.x >= S.lo.x - eps && q.y >= S.lo.y - eps && q.z >= S.lo.z - eps && q.x <= S.hi.x + eps && q.y <= S.hi.y + eps && q.z <= S.hi.z + eps;
            if (!inBox) continue;
            bool inside = true;
            const Plane* on = nullptr;
            for (const Plane& pl : S.planes) {
                const double d0 = dot(pl.n, p[0]) - pl.d, d1 = dot(pl.n, p[1]) - pl.d, d2 = dot(pl.n, p[2]) - pl.d;
                if (std::max({d0, d1, d2}) > eps) {
                    inside = false;
                    break;
                }
                if (std::min({d0, d1, d2}) >= -eps) on = &pl;
            }
            if (!inside) continue;
            // On its skin: only a face of a closed part pressed against it, facing in (the part's own body is on the far side).
            if (on && !(closedOut[partOf[t]] && dot(r.gn, on->n) < -0.999)) continue;
            r.alive = false, st.hidden++;
            break;
        }
    }
}

// ---- Flat regions -----------------------------------------------------------------------------------

double cross2(const double* a, const double* b, const double* c) { return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); }

// Ear clipping of a simple counter-clockwise polygon (n - 2 triangles, or false).
bool earClip(const std::vector<std::array<double, 2>>& P, std::vector<std::array<int, 3>>& out) {
    const int n = (int)P.size();
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    out.clear();
    double scale = 0;
    for (const auto& q : P) scale = std::max({scale, std::fabs(q[0]), std::fabs(q[1])});
    const double tiny = 1e-14 * std::max(1.0, scale * scale);
    int guard = 0;
    while (idx.size() > 3 && guard++ < 100000) {
        const int m = (int)idx.size();
        // (The ear with the best-shaped triangle, so long thin slivers come last.)
        int best = -1;
        double bestQ = -1;
        for (int i = 0; i < m; i++) {
            const int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
            const double *a = P[ia].data(), *b = P[ib].data(), *c = P[ic].data();
            const double ar = cross2(a, b, c);
            if (ar <= tiny) continue;
            bool blocked = false;
            for (int j = 0; j < m && !blocked; j++) {
                const int ij = idx[j];
                if (ij == ia || ij == ib || ij == ic) continue;
                const double* p = P[ij].data();
                if ((p[0] == a[0] && p[1] == a[1]) || (p[0] == b[0] && p[1] == b[1]) || (p[0] == c[0] && p[1] == c[1])) {
                    blocked = true;  // (a pinch: the polygon touches itself)
                    break;
                }
                blocked = cross2(a, b, p) >= -tiny && cross2(b, c, p) >= -tiny && cross2(c, a, p) >= -tiny;
            }
            if (blocked) continue;
            const double e = std::hypot(a[0] - c[0], a[1] - c[1]) + std::hypot(a[0] - b[0], a[1] - b[1]) + std::hypot(b[0] - c[0], b[1] - c[1]);
            const double q = ar / (e * e);
            if (q > bestQ) bestQ = q, best = i;
            if (m > 200) break;  // (a long outline: the first ear will do)
        }
        if (best < 0) return false;
        out.push_back({idx[(best + m - 1) % m], idx[best], idx[(best + 1) % m]});
        idx.erase(idx.begin() + best);
    }
    if (idx.size() != 3 || cross2(P[idx[0]].data(), P[idx[1]].data(), P[idx[2]].data()) <= tiny) return false;
    out.push_back({idx[0], idx[1], idx[2]});
    return (int)out.size() == n - 2;
}

struct Region {
    std::vector<int> tris;
    std::vector<int> loop;       // place ids, round the outline (counter-clockwise seen from the normal side)
    std::vector<char> straight;  // per loop corner: on a straight line between its neighbours
    std::vector<int> inner;      // places inside it (on none of its outline)
    bool ok = true;
    std::vector<std::array<int, 3>> out;  // the new triangles (place ids)
};

size_t mergeFlat(std::vector<Tri>& tris, const std::vector<OptVert>& v, const std::vector<V3>& where, double eps,
                 std::vector<Region>& regions, std::vector<int>& regionOf) {
    const int nt = (int)tris.size();
    std::vector<char> flat(nt, 0);
    for (int t = 0; t < nt; t++) {
        if (!tris[t].alive) continue;
        const OptVert* q = &v[t * 3];
        flat[t] = sameLook(q[0], q[1]) && sameLook(q[0], q[2]);
    }
    std::unordered_map<uint64_t, std::vector<int>> edges;  // undirected -> triangles
    for (int t = 0; t < nt; t++) {
        if (!flat[t]) continue;
        const Tri& r = tris[t];
        for (auto e : {std::make_pair(r.a, r.b), std::make_pair(r.b, r.c), std::make_pair(r.c, r.a)})
            edges[edgeKey(std::min(e.first, e.second), std::max(e.first, e.second))].push_back(t);
    }
    UF uf(nt);
    for (auto& kv : edges) {
        if (kv.second.size() != 2) continue;
        const int s = kv.second[0], t = kv.second[1];
        if (!sameLook(v[s * 3], v[t * 3]) || dot(tris[s].gn, tris[t].gn) < 1 - 1e-9) continue;
        uf.join(s, t);
    }
    std::map<int, std::vector<int>> groups;
    for (int t = 0; t < nt; t++)
        if (flat[t]) groups[uf.find(t)].push_back(t);
    regionOf.assign(nt, -1);
    regions.clear();
    for (auto& kv : groups) {
        if (kv.second.size() < 2) continue;
        Region g;
        g.tris = kv.second;
        // One plane, all of it.
        const Tri& t0 = tris[g.tris[0]];
        const double d0 = dot(t0.gn, where[t0.a]);
        for (int t : g.tris)
            for (int q : {tris[t].a, tris[t].b, tris[t].c})
                if (std::fabs(dot(t0.gn, where[q]) - d0) > eps) g.ok = false;
        // The outline: the edges with nothing on their other side, chained.
        std::unordered_map<uint64_t, int> dir;
        for (int t : g.tris) {
            const Tri& r = tris[t];
            dir[edgeKey(r.a, r.b)]++, dir[edgeKey(r.b, r.c)]++, dir[edgeKey(r.c, r.a)]++;
        }
        std::unordered_map<int, int> next;
        for (auto& e : dir) {
            if (e.second != 1) g.ok = false;
            const int a = (int)(e.first >> 32), b = (int)(e.first & 0xffffffffu);
            if (dir.count(edgeKey(b, a))) continue;
            if (next.count(a)) g.ok = false;  // (pinched)
            next[a] = b;
        }
        if (g.ok && !next.empty()) {
            const int start = next.begin()->first;
            int at = start;
            do {
                g.loop.push_back(at);
                auto it = next.find(at);
                if (it == next.end()) break;
                at = it->second;
            } while (at != start && g.loop.size() <= next.size());
            if (at != start || g.loop.size() != next.size()) g.ok = false;  // (more than one outline: holes)
        } else g.ok = false;
        if (g.ok) {
            std::vector<int> all;
            for (int t : g.tris)
                for (int q : {tris[t].a, tris[t].b, tris[t].c}) all.push_back(q);
            std::sort(all.begin(), all.end());
            all.erase(std::unique(all.begin(), all.end()), all.end());
            std::vector<int> onLoop = g.loop;
            std::sort(onLoop.begin(), onLoop.end());
            std::set_difference(all.begin(), all.end(), onLoop.begin(), onLoop.end(), std::back_inserter(g.inner));
            const int m = (int)g.loop.size();
            g.straight.assign(m, 0);
            for (int i = 0; i < m; i++) {
                const V3 &a = where[g.loop[(i + m - 1) % m]], &b = where[g.loop[i]], &c = where[g.loop[(i + 1) % m]];
                const V3 ac = c - a;
                const double L = len(ac);
                g.straight[i] = L > 0 && len(cross(ac, b - a)) / L < eps * 0.5 && dot(b - a, ac) > 0 && dot(c - b, ac) > 0;
            }
        }
        if (!g.ok) continue;
        for (int t : g.tris) regionOf[t] = (int)regions.size();
        regions.push_back(std::move(g));
    }
    if (regions.empty()) return 0;
    // Who touches each place.
    std::vector<std::vector<int>> touch(where.size());
    for (int t = 0; t < nt; t++)
        if (tris[t].alive)
            for (int q : {tris[t].a, tris[t].b, tris[t].c}) touch[q].push_back(t);
    for (int pass = 0; pass < 8; pass++) {
        // A corner can go when only regions that are flat through it use it.
        std::vector<char> keep(where.size(), 0);
        for (size_t q = 0; q < where.size(); q++)
            for (int t : touch[q])
                if (regionOf[t] < 0 || !regions[regionOf[t]].ok) keep[q] = 1;
        for (const Region& g : regions) {
            if (!g.ok) continue;
            for (size_t i = 0; i < g.loop.size(); i++)
                if (!g.straight[i]) keep[g.loop[i]] = 1;
        }
        bool changed = false;
        for (Region& g : regions) {
            if (!g.ok) continue;
            bool fine = true;
            for (int q : g.inner) fine &= !keep[q];  // (something else stands on its inside: leave it be)
            const Tri& t0 = tris[g.tris[0]];
            std::vector<int> corners;
            for (size_t i = 0; i < g.loop.size(); i++)
                if (!g.straight[i] || keep[g.loop[i]]) corners.push_back(g.loop[i]);
            // Into the plane: (u, w, normal) right-handed, so counter-clockwise stays counter-clockwise.
            const V3 n = t0.gn;
            const V3 u = norm(std::fabs(n.x) < 0.9 ? cross(n, V3(1, 0, 0)) : cross(n, V3(0, 1, 0))), w = cross(n, u);
            const V3 o = where[t0.a];
            std::vector<std::array<double, 2>> P;
            for (int q : corners) P.push_back({dot(where[q] - o, u), dot(where[q] - o, w)});
            std::vector<std::array<int, 3>> ear;
            fine = fine && corners.size() >= 3 && corners.size() - 2 < g.tris.size() && earClip(P, ear);
            if (fine) {  // (the same area: nothing folded or left out)
                double was = 0, now = 0;
                for (int t : g.tris) was += tris[t].area;
                for (auto& e : ear) now += 0.5 * cross2(P[e[0]].data(), P[e[1]].data(), P[e[2]].data());
                fine = std::fabs(was - now) <= 1e-6 * was + 1e-12;
            }
            if (!fine) {
                g.ok = false, changed = true;
                continue;
            }
            g.out.clear();
            for (auto& e : ear) g.out.push_back({corners[e[0]], corners[e[1]], corners[e[2]]});
        }
        if (!changed) break;
        if (pass == 7)
            for (Region& g : regions) g.ok = false;
    }
    size_t saved = 0;
    for (const Region& g : regions)
        if (g.ok) saved += g.tris.size() - g.out.size();
    return saved;
}

}  // namespace

static MeshOptStats optimizeOnce(std::vector<OptVert>& v, const MeshOptOptions& o) {
    MeshOptStats st;
    v.resize(v.size() / 3 * 3);
    st.verticesIn = v.size();
    st.trianglesIn = v.size() / 3;
    st.uniqueVertices = countUnique(v);
    if (v.empty()) return st;
    const double eps = o.weldEps;
    std::vector<V3> where;
    const std::vector<int> id = weld(v, eps, where);
    std::vector<OptVert> firstAt(where.size());
    std::vector<char> seen(where.size(), 0);
    for (size_t i = 0; i < v.size(); i++)
        if (!seen[id[i]]) seen[id[i]] = 1, firstAt[id[i]] = v[i];
    const int nt = (int)(v.size() / 3);
    std::vector<Tri> tris(nt);
    for (int t = 0; t < nt; t++) {
        Tri& r = tris[t];
        r.a = id[t * 3], r.b = id[t * 3 + 1], r.c = id[t * 3 + 2];
        const V3 pa = pos(v[t * 3]), pb = pos(v[t * 3 + 1]), pc = pos(v[t * 3 + 2]);
        const V3 cr = cross(pb - pa, pc - pa);
        const double l = len(cr), longest = std::max({len(pb - pa), len(pc - pb), len(pa - pc)});
        r.gn = l > 0 ? cr / l : V3(0, 1, 0);
        r.area = 0.5 * l;
        // (Nothing is culled, so the models don't keep to a winding: the normals say which way a face looks. Turned to agree.)
        V3 nv;
        for (int k = 0; k < 3; k++) nv += V3(v[t * 3 + k].n[0], v[t * 3 + k].n[1], v[t * 3 + k].n[2]);
        if (dot(nv, r.gn) < 0) std::swap(r.b, r.c), r.gn = -r.gn;
        if (o.degenerate && (r.a == r.b || r.b == r.c || r.a == r.c || l <= longest * eps * 0.1)) r.alive = false, st.degenerate++;
    }
    if (o.degenerate) {  // (the same triangle again, looking the same: either winding, as nothing is culled)
        auto corners = [&](int t, int k[3]) {
            k[0] = 0, k[1] = 1, k[2] = 2;
            std::sort(k, k + 3, [&](int x, int y) { return id[t * 3 + x] < id[t * 3 + y]; });
        };
        auto same = [&](int s, int t) {
            int a[3], b[3];
            corners(s, a), corners(t, b);
            for (int i = 0; i < 3; i++) {
                const OptVert &p = v[s * 3 + a[i]], &q = v[t * 3 + b[i]];
                if (id[s * 3 + a[i]] != id[t * 3 + b[i]] || std::memcmp(p.n, q.n, sizeof p.n + sizeof p.c)) return false;
            }
            return true;
        };
        std::unordered_multimap<uint64_t, int> once;
        once.reserve(nt);
        for (int t = 0; t < nt; t++) {
            if (!tris[t].alive) continue;
            int k[3];
            corners(t, k);
            uint64_t h = 1469598103934665603ull;
            for (int j : k) h = fnv(&id[t * 3 + j], sizeof(int), h), h = fnv(v[t * 3 + j].n, sizeof(float) * 7, h);
            bool dup = false;
            auto range = once.equal_range(h);
            for (auto it = range.first; it != range.second && !dup; ++it) dup = same(it->second, t);
            if (dup) tris[t].alive = false, st.repeated++;
            else once.emplace(h, t);
        }
    }
    if (o.hidden) dropHidden(tris, v, where, eps, st);
    if (o.groundMin)
        for (int t = 0; t < nt; t++) {
            Tri& r = tris[t];
            if (!r.alive || r.gn.y > -0.5) continue;  // (bottoms only: a trench dug beside it in the editor shows its sides)
            const V3 &a = where[r.a], &b = where[r.b], &c = where[r.c];
            const double top = std::max({a.y, b.y, c.y});
            if (top < o.groundMin(std::min({a.x, b.x, c.x}), std::min({a.z, b.z, c.z}), std::max({a.x, b.x, c.x}), std::max({a.z, b.z, c.z})))
                r.alive = false, st.buried++;
        }
    std::vector<Region> regions;
    std::vector<int> regionOf(nt, -1);
    if (o.coplanar) st.coplanarSaved = mergeFlat(tris, v, where, eps, regions, regionOf);
    // Out, in the order they came (a merged region where its first triangle was).
    std::vector<OptVert> out;
    out.reserve(v.size());
    std::vector<char> done(regions.size(), 0);
    for (int t = 0; t < nt; t++) {
        if (!tris[t].alive) continue;
        const int g = regionOf[t];
        if (g >= 0 && regions[g].ok) {
            if (done[g]) continue;
            done[g] = 1;
            const OptVert look = v[t * 3];
            for (const auto& e : regions[g].out)
                for (int q : e) {
                    OptVert x = look;
                    std::memcpy(x.p, firstAt[q].p, sizeof x.p);
                    out.push_back(x);
                }
            continue;
        }
        out.insert(out.end(), v.begin() + t * 3, v.begin() + t * 3 + 3);
    }
    v.swap(out);
    st.trianglesOut = v.size() / 3;
    return st;
}

MeshOptStats optimizeVerts(std::vector<OptVert>& v, const MeshOptOptions& o) {
    if (!o.degenerate && !o.hidden && !o.coplanar) {  // (just the ground: no welding needed, quick)
        MeshOptStats st;
        v.resize(v.size() / 3 * 3);
        st.verticesIn = st.uniqueVertices = v.size(), st.trianglesIn = v.size() / 3;  // (not counted here)
        size_t out = 0;
        for (size_t t = 0; t < v.size(); t += 3) {
            const V3 a = pos(v[t]), b = pos(v[t + 1]), c = pos(v[t + 2]);
            V3 n = cross(b - a, c - a), nv;
            for (int k = 0; k < 3; k++) nv += V3(v[t + k].n[0], v[t + k].n[1], v[t + k].n[2]);
            if (dot(n, nv) < 0) n = -n;
            const double l = len(n);
            if (o.groundMin && l > 0 && n.y / l <= -0.5 &&
                std::max({a.y, b.y, c.y}) < o.groundMin(std::min({a.x, b.x, c.x}), std::min({a.z, b.z, c.z}), std::max({a.x, b.x, c.x}), std::max({a.z, b.z, c.z}))) {
                st.buried++;
                continue;
            }
            if (out != t) std::copy(v.begin() + t, v.begin() + t + 3, v.begin() + out);
            out += 3;
        }
        v.resize(out);
        st.trianglesOut = out / 3;
        return st;
    }
    // (Again while it finds more: a part merged flat can close up and hide what's inside it.)
    MeshOptStats st = optimizeOnce(v, o);
    for (int pass = 0; pass < 3 && st.trianglesOut < st.trianglesIn; pass++) {
        const size_t before = v.size();
        MeshOptStats more = optimizeOnce(v, o);
        st.degenerate += more.degenerate, st.repeated += more.repeated, st.hidden += more.hidden, st.buried += more.buried;
        st.coplanarSaved += more.coplanarSaved, st.trianglesOut = more.trianglesOut;
        if (v.size() == before) break;
    }
    return st;
}

MeshOptStats optimizeMesh(mk::Mesh& m, const MeshOptOptions& o) {
    std::vector<OptVert> w(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        std::memcpy(w[i].p, m.v[i].p, sizeof w[i].p), std::memcpy(w[i].n, m.v[i].n, sizeof w[i].n);
        w[i].c[0] = m.v[i].c[0], w[i].c[1] = m.v[i].c[1], w[i].c[2] = m.v[i].c[2], w[i].c[3] = m.v[i].mat;
    }
    const MeshOptStats s = optimizeVerts(w, o);
    m.v.resize(w.size());
    for (size_t i = 0; i < w.size(); i++) {
        std::memcpy(m.v[i].p, w[i].p, sizeof w[i].p), std::memcpy(m.v[i].n, w[i].n, sizeof w[i].n);
        m.v[i].c[0] = w[i].c[0], m.v[i].c[1] = w[i].c[1], m.v[i].c[2] = w[i].c[2], m.v[i].mat = (uint8_t)w[i].c[3];
    }
    m.finish();
    return s;
}

MeshOptStats analyzeMesh(const mk::Mesh& m, const MeshOptOptions& o) {
    mk::Mesh copy = m;
    return optimizeMesh(copy, o);
}
