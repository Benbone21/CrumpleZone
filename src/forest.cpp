#include "forest.h"

#include "biome.h"
#include "plants.h"
#include "visibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace forest {

const char* modelId(int kind) {
    static const char* const ids[3] = {"tree_pine", "tree_broadleaf", "tree_snag"};
    return ids[std::clamp(kind, 0, 2)];
}
double modelHeight(int kind) { return kind == 2 ? 8.0 : 10.0; }

double lodReach(int lod) {
    static const double reach[kLods] = {30, 75, 200, 1e9};
    return reach[std::clamp(lod, 0, kLods - 1)];
}

int designFor(int kind, uint32_t seed, double x, double z, double height) {
    const mk::ModelDef* d = mk::findModel(modelId(kind));
    if (!d) return 0;
    if (d->variants.size() < 3) return mk::variantFor(*d, (uint32_t)seed);
    const double r = latticeHash((int)(seed % 1000003), 31), stand = valueNoise(x * 0.018 + 11.3, z * 0.018 - 4.1);
    const int v = [&] {
    switch (kind) {
        case 0:  // firs; tall spruce among the tallest, a stand of scraggly lodgepole here and there
            if (r < 0.08 + 0.4 * smoothstep(12.5, 17.0, height)) return 1;
            if (stand > 0.7 && r > 0.55) return 2;
            return 0;
        case 1:  // oaks; aspen (the pale bark) in groves
            return (stand > 0.6 ? r < 0.85 : r < 0.06) ? 1 : 0;
        default:  // grey snags; one in four snapped off, the odd burnt one
            return r < 0.25 ? 2 : r < 0.35 ? 1 : 0;
    }
    }();
    // (A design left out in the lab: another in use by the same dice.)
    return mk::variantIncluded(*d, v) ? v : mk::variantFor(*d, (uint32_t)seed);
}

namespace {
mk::Mesh buildModel(const Look& lk, int lod = 0) {
    mk::Mesh m;
    if (const mk::ModelDef* d = mk::findModel(modelId(lk.kind))) {
        const int v = std::clamp(lk.design, 0, std::max(0, (int)d->variants.size() - 1));
        mk::P p = mk::pickedParams(*d, v);
        p.v["seed"] = lk.seed;
        p.v["snow"] = lk.snow ? 1 : 0;
        p.v["lod"] = lod;  // (the tree models' far copies: fewer sides, the small bits left out)
        mk::build(*d, v, p, m);
    }
    return m;
}

// The trunk's radius up a model (model units), every 5 cm: the wood cut by a level plane there, the
// distance of the cut from its middle (the median: a root or stub crossing the plane doesn't count).
struct Profile {
    static constexpr double kStep = 0.05;
    std::vector<double> r;
    double at(double y) const {
        if (r.empty()) return 0.2;
        const double f = std::clamp(y / kStep, 0.0, (double)r.size() - 1.001);
        const int i = (int)f;
        return r[i] + (r[i + 1] - r[i]) * (f - i);
    }
};
Profile measure(const mk::Mesh& m) {
    Profile pr;
    for (double y = 0; y < 6.0; y += Profile::kStep) {
        std::vector<V3> cut;
        for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
            if (m.v[i].mat != mk::WOOD) continue;
            for (int e = 0; e < 3; e++) {
                const float* a = m.v[i + e].p;
                const float* b = m.v[i + (e + 1) % 3].p;
                if ((a[1] - y) * (b[1] - y) > 0 || a[1] == b[1]) continue;
                const double t = (y - a[1]) / (b[1] - a[1]);
                cut.push_back(V3(a[0] + (b[0] - a[0]) * t, y, a[2] + (b[2] - a[2]) * t));
            }
        }
        double r = pr.r.empty() ? 0.2 : pr.r.back();
        if (!cut.empty()) {
            // (the middle: of the points nearest the axis line, so a branch off to one side doesn't drag it)
            std::sort(cut.begin(), cut.end(), [](const V3& a, const V3& b) { return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z; });
            const size_t n = std::max<size_t>(3, cut.size() * 2 / 3);
            V3 c;
            for (size_t k = 0; k < std::min(n, cut.size()); k++) c = c + cut[k];
            c = c / (double)std::min(n, cut.size());
            std::vector<double> d;
            for (const V3& q : cut) d.push_back(std::hypot(q.x - c.x, q.z - c.z));
            std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
            r = d[d.size() / 2];
        }
        pr.r.push_back(r);
    }
    return pr;
}
}  // namespace

double trunkRadius(int kind, int design, double height) {
    static std::mutex lock;
    static std::map<std::pair<int, int>, Profile> cache;
    const Profile* pr;
    {
        std::lock_guard<std::mutex> lk(lock);
        auto it = cache.find({kind, design});
        if (it == cache.end()) {
            Look l;
            l.kind = kind, l.design = design, l.seed = 1;
            it = cache.emplace(std::make_pair(kind, design), measure(buildModel(l))).first;
        }
        pr = &it->second;
    }
    // (the base stands 0.2 m into the ground: World::addTree)
    const double k = std::max(0.05, height / modelHeight(kind));
    return std::clamp(pr->at(1.2 / k) * k, 0.06, 0.9);
}

size_t Set::vertices() const {
    size_t n = 0;
    for (const Shape& s : shapes)
        for (const mk::Mesh& m : s.lod) n += m.v.size();
    return n;
}
size_t Set::bakedVertices() const {
    size_t n = 0;
    for (const Inst& i : inst) n += shapes[i.shape].lod[0].v.size();
    return n;
}

namespace {
// Its reach round the base and its top, from the full model.
void measureShape(Shape& s) {
    const mk::Mesh& m = s.lod[0];
    s.reach = (float)std::max({std::fabs(m.lo.x), std::fabs(m.hi.x), std::fabs(m.lo.z), std::fabs(m.hi.z), 0.3}) * (float)std::sqrt(2.0);
    s.top = (float)std::max(m.hi.y, 0.3);
}
}  // namespace

mk::Mesh simplify(const mk::Mesh& m, double cell) {
    // Each corner to a cluster (by its cell and material): the cluster's corners averaged.
    struct Acc {
        double p[3] = {}, n[3] = {}, c[3] = {};
        int k = 0;
    };
    std::map<std::tuple<int, int, int, int>, int> at;
    std::vector<Acc> acc;
    std::vector<int> id(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        const mk::MVert& q = m.v[i];
        const auto key = std::make_tuple((int)std::floor(q.p[0] / cell), (int)std::floor(q.p[1] / cell), (int)std::floor(q.p[2] / cell), (int)q.mat);
        auto it = at.find(key);
        if (it == at.end()) it = at.emplace(key, (int)acc.size()).first, acc.emplace_back();
        Acc& a = acc[it->second];
        for (int k = 0; k < 3; k++) a.p[k] += q.p[k], a.n[k] += q.n[k], a.c[k] += q.c[k];
        a.k++;
        id[i] = it->second;
    }
    mk::Mesh out;
    std::map<std::tuple<int, int, int>, bool> seen;  // (a sheet drawn from both sides comes out once: nothing is culled)
    for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
        int t[3] = {id[i], id[i + 1], id[i + 2]};
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
        int u[3] = {t[0], t[1], t[2]};
        std::sort(u, u + 3);
        if (!seen.emplace(std::make_tuple(u[0], u[1], u[2]), true).second) continue;
        for (int k = 0; k < 3; k++) {
            const Acc& a = acc[t[k]];
            mk::MVert q = m.v[i + k];
            V3 n(a.n[0], a.n[1], a.n[2]);
            n = len(n) > 1e-6 ? norm(n) : V3(q.n[0], q.n[1], q.n[2]);
            for (int j = 0; j < 3; j++) q.p[j] = (float)(a.p[j] / a.k), q.c[j] = (float)(a.c[j] / a.k);
            q.n[0] = (float)n.x, q.n[1] = (float)n.y, q.n[2] = (float)n.z;
            out.v.push_back(q);
        }
    }
    out.finish();
    return out;
}

constexpr double kFarCell = 0.9;  // (model units: a tree is 10 tall)

void build(const World& w, bool skipPines, bool skipSnags, Set& out) {
    out = Set();
    for (int l = 0; l < kLods; l++) out.lodAt[l] = (float)lodReach(l);
    std::map<std::tuple<int, int, int, int>, int> index;
    for (size_t i = 0; i < w.trees.size(); i++) {
        const Tree& t = w.trees[i];
        const int seed = (int)i + 1;  // (as the baked forest had it: a tree keeps its look)
        if (t.kind < 0 || t.kind > 2 || (t.kind == 0 && skipPines) || (t.kind == 2 && skipSnags)) continue;
        Look lk;
        lk.kind = t.kind;
        lk.design = t.design >= 0 ? t.design : designFor(t.kind, (uint32_t)seed, t.base.x, t.base.z, t.height);
        lk.seed = 1 + seed % 6;
        lk.snow = w.surfaceAt(t.base.x, t.base.z) == SURF_SNOW;
        const auto key = std::make_tuple(lk.kind, lk.design, lk.seed, lk.snow ? 1 : 0);
        auto it = index.find(key);
        if (it == index.end()) {
            it = index.emplace(key, (int)out.shapes.size()).first;
            Shape s;
            s.look = lk;
            for (int l = 0; l < kLods - 1; l++) s.lod[l] = buildModel(lk, l);
            s.lod[kLods - 1] = simplify(s.lod[kLods - 2], kFarCell);
            measureShape(s);
            out.shapes.push_back(std::move(s));
        }
        Inst in;
        in.x = (float)t.base.x, in.y = (float)t.base.y, in.z = (float)t.base.z;
        in.yaw = (float)(latticeHash(seed, 7) * 6.2832);
        in.scale = (float)(t.height / modelHeight(t.kind));
        in.tint = (float)(0.85 + 0.15 * t.tint);
        in.warm = (float)((latticeHash(seed, 13) - 0.5) * 1.2);
        in.shape = it->second, in.tree = (int)i;
        out.inst.push_back(in);
    }
}

void buildBushes(const World& w, Set& out) {
    out = Set();
    const mk::ModelDef* d = mk::findModel("bush");
    if (!d) return;
    std::map<std::tuple<int, int, int>, int> index;
    int seed = 0;
    for (size_t i = 0; i < w.bushes.size(); i++) {
        const Bush& b = w.bushes[i];
        seed++;  // (as the baked bushes had it: a bush keeps its look)
        const int kind = plantKindOf(b.kind), sd = 1 + seed % 6, dry = seed % 7 == 0 ? 1 : 0;
        const auto key = std::make_tuple(kind, sd, dry);
        auto it = index.find(key);
        if (it == index.end()) {
            it = index.emplace(key, (int)out.shapes.size()).first;
            Shape s;
            s.look.kind = kind, s.look.design = kind, s.look.seed = sd;
            const int v = std::min(kind, std::max(0, (int)d->variants.size() - 1));
            mk::P p = mk::pickedParams(*d, v);
            p.v["seed"] = sd, p.v["dry"] = dry;
            mk::build(*d, v, p, s.lod[0]);
            measureShape(s);
            out.shapes.push_back(std::move(s));
        }
        Inst in;
        in.x = (float)b.p.x, in.y = (float)b.p.y, in.z = (float)b.p.z;
        in.yaw = (float)(seed * 2.39996);
        in.scale = (float)(b.r / 0.8);
        in.stretch = (float)(0.8 + 0.4 * latticeHash(seed, 2));
        in.tint = (float)(0.85 + 0.15 * b.tint);
        in.warm = 0;
        // Out to where it's a few pixels across: further for a big one.
        in.reach = (float)(100 + 80 * b.r);
        in.shape = it->second, in.tree = (int)i;
        out.inst.push_back(in);
    }
}

void Picker::sphere(const Set& s, const Inst& in, V3& c, double& r) {
    const Shape& sh = s.shapes[in.shape];
    const double h = sh.top * in.scale * in.stretch, w = sh.reach * in.scale;
    c = V3(in.x, in.y + h * 0.5, in.z);
    r = std::sqrt(w * w + h * h * 0.25);
}

void Picker::build(const Set& s) {
    // The instances in 64 m cells, each cell's box round all of them.
    std::map<std::pair<int, int>, int> at;
    cells_.clear();
    for (size_t i = 0; i < s.inst.size(); i++) {
        const Inst& in = s.inst[i];
        const std::pair<int, int> key((int)std::floor(in.x / 64), (int)std::floor(in.z / 64));
        auto it = at.find(key);
        if (it == at.end()) {
            it = at.emplace(key, (int)cells_.size()).first, cells_.emplace_back();
            cells_.back().lo = V3(1e30, 1e30, 1e30), cells_.back().hi = V3(-1e30, -1e30, -1e30);
        }
        Cell& c = cells_[it->second];
        c.inst.push_back((int)i);
        V3 m;
        double r;
        sphere(s, in, m, r);
        c.lo = V3(std::min(c.lo.x, m.x - r), std::min(c.lo.y, m.y - r), std::min(c.lo.z, m.z - r));
        c.hi = V3(std::max(c.hi.x, m.x + r), std::max(c.hi.y, m.y + r), std::max(c.hi.z, m.z + r));
    }
}

void Picker::pick(const Set& s, const View& v, std::vector<std::vector<Pick>>& buckets) {
    buckets.resize(s.shapes.size() * kLods);
    for (auto& b : buckets) b.clear();
    // The view's six planes (a, b, c, d: inside where a x + b y + c z + d >= 0), and each normal's length.
    double pl[6][5];
    {
        int k = 0;
        for (int axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                for (int c = 0; c < 4; c++) pl[k][c] = v.vp.m[c * 4 + 3] + sign * v.vp.m[c * 4 + axis];
                pl[k][4] = std::sqrt(pl[k][0] * pl[k][0] + pl[k][1] * pl[k][1] + pl[k][2] * pl[k][2]);
                k++;
            }
    }
    // (nearest cells first, and the near LODs drawn first: what's in front fills the depth first, so less is shaded behind it)
    order_.clear();
    for (int k = 0; k < (int)cells_.size(); k++) {
        RenderBounds b;
        b.lo = cells_[k].lo, b.hi = cells_[k].hi;
        if (b.visible(v.vp)) order_.push_back({b.distance(v.eye), k});
    }
    std::sort(order_.begin(), order_.end());
    for (const auto& ok : order_)
        for (int i : cells_[ok.second].inst) {
            const Inst& in = s.inst[i];
            const double d = std::hypot(in.x - v.eye.x, in.z - v.eye.z);
            float fade = 1;
            if (in.reach > 0) {
                const double R = in.reach * v.detail * v.reach;
                if (d >= R) continue;
                fade = (float)std::min(1.0, (R - d) / (0.15 * R));
                if (v.shadow) {
                    if (fade < 0.5f) continue;
                    fade = 1;
                }
            }
            V3 c;
            double r;
            sphere(s, in, c, r);
            bool out = false;
            for (int k = 0; k < 6 && !out; k++) out = pl[k][0] * c.x + pl[k][1] * c.y + pl[k][2] * c.z + pl[k][3] < -r * pl[k][4];
            if (out) continue;
            int lod = 0;
            while (lod < kLods - 1 && d > s.lodAt[lod] * v.detail) lod++;
            lod = std::min(kLods - 1, lod + v.lodBias);
            const Shape& sh = s.shapes[in.shape];
            while (lod > 0 && sh.lod[lod].v.empty()) lod--;
            buckets[in.shape * kLods + lod].push_back({i, fade});
        }
}

void logMesh(const World& w, const Log& g, uint32_t seed, mk::Mesh& out) {
    const V3 ax0 = g.b - g.a;
    const double L = len(ax0), r = g.r;
    if (L < 1e-3 || r <= 0) return;
    const V3 ax = ax0 / L, u = norm(cross(ax, std::fabs(ax.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), vv = cross(ax, u);
    auto h01 = [&](int i) { return latticeHash((int)(seed % 100003) * 7 + 3, i); };
    auto tri = [&](const V3& p0, const V3& p1, const V3& p2, const V3& n0, const V3& n1, const V3& n2, const V3& c0, const V3& c1, const V3& c2, mk::Mat m) {
        const V3* P[3] = {&p0, &p1, &p2};
        const V3* N[3] = {&n0, &n1, &n2};
        const V3* C[3] = {&c0, &c1, &c2};
        for (int k = 0; k < 3; k++) {
            mk::MVert q;
            q.p[0] = (float)P[k]->x, q.p[1] = (float)P[k]->y, q.p[2] = (float)P[k]->z;
            q.n[0] = (float)N[k]->x, q.n[1] = (float)N[k]->y, q.n[2] = (float)N[k]->z;
            q.c[0] = (float)C[k]->x, q.c[1] = (float)C[k]->y, q.c[2] = (float)C[k]->z;
            q.mat = m;
            out.v.push_back(q);
        }
    };
    // Where it lies: moss in the woods and by water, none in sand and red rock; snow on snow.
    const V3 mid = (g.a + g.b) * 0.5;
    const bool snow = w.surfaceAt(mid.x, mid.z) == SURF_SNOW;
    const Biome bio = biomeAt(w, mid.x, mid.z);
    const double moss = snow ? 0.0 : bio == BIOME_PINE || bio == BIOME_RIVER ? 1.0 : bio == BIOME_DEFAULT ? 0.45 : 0.0;
    const V3 barkA(0.31, 0.24, 0.17), barkB(0.39, 0.30, 0.21), mossC(0.20, 0.26, 0.11), snowC(0.82, 0.84, 0.88);
    const V3 sap(0.64, 0.52, 0.36), heart(0.47, 0.34, 0.21);
    // (bark a colour a facet, so it reads in strips; the moss and snow on it by the vertex, in soft patches)
    auto barkCol = [&](const V3& n, int i) { return mk::mix(barkA, barkB, h01(i)) * (0.8 + 0.25 * std::max(0.0, n.y)); };  // (darker underneath)
    auto cover = [&](const V3& c, const V3& p, const V3& n) {
        V3 o = c;
        if (moss > 0) o = mk::mix(o, mossC, 0.8 * moss * smoothstep(0.1, 0.8, n.y) * smoothstep(0.35, 0.65, valueNoise(p.x * 0.8 + p.y * 0.5, p.z * 0.8 - p.y * 0.5)));
        if (snow) o = mk::mix(o, snowC, smoothstep(0.35, 0.7, n.y));
        return o;
    };
    // Bark: rings along it, ridged (alternate facets in and out), all inside the capsule.
    const int sides = 11, segs = std::max(3, (int)std::ceil(L / 0.55));
    std::vector<V3> pos((segs + 1) * sides), nrm((segs + 1) * sides);
    for (int i = 0; i <= segs; i++) {
        const double t = (double)i / segs;
        const double ri = r * (0.95 - 0.07 * t) * (0.97 + 0.03 * std::sin(t * 9.0 + h01(1) * 6.0));
        const V3 c = g.a + ax * (L * t) + vv * (0.04 * r * std::sin(t * 3.1 + h01(2) * 6.0));  // (a little bent)
        for (int j = 0; j < sides; j++) {
            const double a = 2 * M_PI * j / sides + h01(3);
            const V3 d = u * std::cos(a) + vv * std::sin(a);
            const double ridge = (j & 1) ? 1.0 : 0.93 - 0.04 * h01(i * 17 + j);
            pos[i * sides + j] = c + d * (ri * ridge);
            nrm[i * sides + j] = d;
        }
    }
    for (int i = 0; i < segs; i++)
        for (int j = 0; j < sides; j++) {
            const int k = (j + 1) % sides;
            const int a0 = i * sides + j, a1 = i * sides + k, b0 = a0 + sides, b1 = a1 + sides;
            const V3 col = barkCol(norm(nrm[a0] + nrm[a1]), i * 31 + j);
            auto c = [&](int q, double k) { return cover(col * k, pos[q], nrm[q]); };
            tri(pos[a0], pos[b1], pos[a1], nrm[a0], nrm[b1], nrm[a1], c(a0, 1), c(b1, 0.96), c(a1, 1), mk::WOOD);
            tri(pos[a0], pos[b0], pos[b1], nrm[a0], nrm[b0], nrm[b1], c(a0, 1), c(b0, 0.96), c(b1, 0.96), mk::WOOD);
        }
    // The ends: sawn (pale rings round a darker heart) or snapped (splinters).
    for (int e = 0; e < 2; e++) {
        const int ring = e == 0 ? 0 : segs;
        const V3 out_ = e == 0 ? -ax : ax;
        V3 ctr;
        for (int j = 0; j < sides; j++) ctr = ctr + pos[ring * sides + j];
        ctr = ctr / sides;
        if (h01(10 + e) < 0.55) {
            const double fr[4] = {1.0, 0.82, 0.5, 0.0};
            const V3 cols[4] = {V3(0.36, 0.27, 0.18), sap, mk::mix(sap, heart, 0.6), heart * 0.9};
            for (int q = 0; q < 3; q++)
                for (int j = 0; j < sides; j++) {
                    const int k = (j + 1) % sides;
                    const V3 o0 = ctr + (pos[ring * sides + j] - ctr) * fr[q], o1 = ctr + (pos[ring * sides + k] - ctr) * fr[q];
                    const V3 i0 = ctr + (pos[ring * sides + j] - ctr) * fr[q + 1], i1 = ctr + (pos[ring * sides + k] - ctr) * fr[q + 1];
                    const V3 c0 = cols[q] * (0.95 + 0.1 * h01(e * 97 + q * 13 + j)), c1 = cols[q + 1];
                    tri(o0, o1, i1, out_, out_, out_, c0, c0, c1, mk::MATTE);
                    if (q < 2) tri(o0, i1, i0, out_, out_, out_, c0, c1, c1, mk::MATTE);
                }
        } else {
            // Snapped: a recessed, pale break, and splinters standing out of it, longest at the middle.
            const V3 pit = ctr - out_ * (0.15 * r);
            for (int j = 0; j < sides; j++) {
                const int k = (j + 1) % sides;
                const V3 p0 = pos[ring * sides + j], p1 = pos[ring * sides + k];
                tri(p0, p1, pit, out_, out_, out_, sap * 0.8, sap * 0.8, heart * 0.8, mk::MATTE);
                if (h01(e * 50 + j + 20) < 0.6) {
                    const V3 base = (p0 + p1) * 0.5 + (pit - (p0 + p1) * 0.5) * 0.35;
                    const V3 tip = base + out_ * (r * (0.3 + 0.9 * h01(e * 50 + j + 30))) + (ctr - base) * 0.25;
                    const V3 sn = norm(((p0 + p1) * 0.5 - ctr) + out_ * 0.3);
                    tri(p0 + (pit - p0) * 0.2, p1 + (pit - p1) * 0.2, tip, sn, sn, sn, sap * 0.9, sap * 0.9, sap * 1.05, mk::MATTE);
                }
            }
        }
    }
    // A stub or two of branch, out of the top half, snapped off.
    const int stubs = 1 + (int)(h01(40) * 2.2);
    for (int s = 0; s < stubs; s++) {
        const double t = 0.2 + 0.6 * h01(41 + s), a = (h01(44 + s) < 0.5 ? -1 : 1) * (0.55 + 0.6 * h01(45 + s));  // (round from straight up, to one side)
        V3 up = V3(0, 1, 0) - ax * ax.y;
        up = len(up) > 1e-6 ? norm(up) : vv;
        const V3 side = cross(ax, up), dir = norm(up * std::cos(a) + side * std::sin(a) + ax * (0.35 * (h01(47 + s) - 0.3)));
        const V3 o = g.a + ax * (L * t) + dir * (r * 0.6);
        const double sl = r * (0.8 + 1.1 * h01(50 + s)), sr = r * (0.24 + 0.12 * h01(53 + s));
        const V3 tip = o + dir * sl;
        const V3 su = norm(cross(dir, std::fabs(dir.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), sv = cross(dir, su);
        const int n = 5;
        for (int j = 0; j < n; j++) {
            const double a0 = 2 * M_PI * j / n, a1 = 2 * M_PI * (j + 1) / n;
            const V3 d0 = su * std::cos(a0) + sv * std::sin(a0), d1 = su * std::cos(a1) + sv * std::sin(a1);
            const V3 q0 = o + d0 * sr, q1 = o + d1 * sr;
            const V3 t0 = tip + d0 * (sr * 0.6) + dir * (sr * (h01(60 + s * 7 + j) - 0.5)), t1 = tip + d1 * (sr * 0.6) + dir * (sr * (h01(60 + s * 7 + (j + 1) % n) - 0.5));
            const V3 col = cover(barkCol(d0, 70 + s * 7 + j), o, d0);
            tri(q0, t1, q1, d0, d1, d1, col, col, col, mk::WOOD);
            tri(q0, t0, t1, d0, d0, d1, col, col, col, mk::WOOD);
            tri(t0, tip + dir * (sr * 0.2), t1, dir, dir, dir, sap * 0.85, sap, sap * 0.85, mk::MATTE);
        }
    }
    out.finish();
}

}  // namespace forest
