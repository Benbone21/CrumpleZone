#include "treedraw.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

using namespace gl;

// Per instance: its base and turn (location 3), and its scale, tint and foliage hue (4). A model's
// front is +X, its left +Z: turned by yaw, front (cos, 0, -sin) and left (sin, 0, cos), as appendModel.
#define TREE_PLACE                                                                 \
    "layout(location=0) in vec3 aPos;\n"                                           \
    "layout(location=1) in vec3 aNrm;\n"                                           \
    "layout(location=2) in vec4 aCol;\n"                                           \
    "layout(location=3) in vec4 iPos;\n"                                           \
    "layout(location=4) in vec4 iLook;\n"                                          \
    "vec3 turn(vec3 v){ float c = cos(iPos.w), s = sin(iPos.w); return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }\n"

const char* const kTreeVS = "#version 330 core\n" TREE_PLACE R"(
uniform mat4 uVP; uniform mat4 uLightVP;
out vec3 vPos; out vec3 vNrm; out vec4 vCol; out vec4 vLight; out vec3 vLocal;
void main(){
  vec3 wp = iPos.xyz + turn(aPos * iLook.x);
  vec3 col = aCol.rgb * iLook.y;
  // (each tree's leaves a little yellower or bluer than the next)
  if (int(aCol.a + 0.5) == 13) col *= iLook.z > 0.0 ? mix(vec3(1.0), vec3(1.14, 1.05, 0.72), iLook.z) : mix(vec3(1.0), vec3(0.9, 0.98, 1.1), -iLook.z);
  vPos = wp; vNrm = turn(aNrm); vCol = vec4(col, aCol.a); vLocal = aPos;
  vLight = uLightVP * vec4(wp, 1.0);
  gl_Position = uVP * vec4(wp, 1.0);
})";

const char* const kTreeDepthVS = "#version 330 core\n" TREE_PLACE R"(
uniform mat4 uLightVP;
void main(){ gl_Position = uLightVP * vec4(iPos.xyz + turn(aPos * iLook.x), 1.0); })";

void TreeDraw::build(const World& w, bool skipPines, bool skipSnags) {
    forest::Set set;
    forest::build(w, skipPines, skipSnags, set);
    inst_ = set.inst;
    meshVerts_ = set.vertices(), baked_ = set.bakedVertices();
    // The meshes, one after another: position, normal, colour and material (the lit shader's Vtx).
    std::vector<float> v;
    v.reserve(meshVerts_ * 10);
    ranges_.assign(set.shapes.size(), {});
    std::vector<double> reach(set.shapes.size()), top(set.shapes.size());
    for (size_t s = 0; s < set.shapes.size(); s++) {
        const forest::Shape& sh = set.shapes[s];
        for (int l = 0; l < forest::kLods; l++) {
            ranges_[s][l].first = (int)(v.size() / 10), ranges_[s][l].count = (int)sh.lod[l].v.size();
            for (const mk::MVert& q : sh.lod[l].v) v.insert(v.end(), {q.p[0], q.p[1], q.p[2], q.n[0], q.n[1], q.n[2], q.c[0], q.c[1], q.c[2], (float)q.mat});
        }
        const mk::Mesh& m = sh.lod[0];
        reach[s] = std::sqrt(2.0) * std::max({std::fabs(m.lo.x), std::fabs(m.hi.x), std::fabs(m.lo.z), std::fabs(m.hi.z), 0.5});
        top[s] = std::max(m.hi.y, 1.0);
    }
    if (!vao_) {
        GenVertexArrays(1, &vao_);
        GenBuffers(1, &vbo_);
        GenBuffers(1, &instVbo_);
        BindVertexArray(vao_);
        BindBuffer(GL_ARRAY_BUFFER, vbo_);
        VertexAttribPointer(0, 3, GL_FLOAT, 0, 40, (void*)0);
        VertexAttribPointer(1, 3, GL_FLOAT, 0, 40, (void*)12);
        VertexAttribPointer(2, 4, GL_FLOAT, 0, 40, (void*)24);
        for (int a = 0; a < 3; a++) EnableVertexAttribArray(a);
        BindBuffer(GL_ARRAY_BUFFER, instVbo_);
        for (int a = 3; a < 5; a++) {
            EnableVertexAttribArray(a);
            VertexAttribPointer(a, 4, GL_FLOAT, 0, 32, (void*)(size_t)((a - 3) * 16));
            VertexAttribDivisor(a, 1);
        }
        BindVertexArray(0);
    }
    BindBuffer(GL_ARRAY_BUFFER, vbo_);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    // The trees in 64 m cells, each cell's box round all its trees.
    std::map<std::pair<int, int>, int> at;
    cells_.clear();
    for (size_t i = 0; i < inst_.size(); i++) {
        const forest::Inst& in = inst_[i];
        const std::pair<int, int> key((int)std::floor(in.x / 64), (int)std::floor(in.z / 64));
        auto it = at.find(key);
        if (it == at.end()) it = at.emplace(key, (int)cells_.size()).first, cells_.emplace_back();
        Cell& c = cells_[it->second];
        c.inst.push_back((int)i);
        const double r = reach[in.shape] * in.scale, h = top[in.shape] * in.scale;
        c.bounds.add(V3(in.x - r, in.y - 0.5, in.z - r));
        c.bounds.add(V3(in.x + r, in.y + h, in.z + r));
    }
    buckets_.assign(set.shapes.size() * forest::kLods, {});
}

unsigned TreeDraw::draw(const M4& vp, const V3& eye, int lodBias) {
    if (!vao_ || inst_.empty()) return 0;
    const int L = forest::kLods;
    static const int forced = std::getenv("CZ_TREE_LOD") ? std::clamp(std::atoi(std::getenv("CZ_TREE_LOD")), 0, L - 1) : -1;  // (screenshots of one LOD)
    for (auto& b : buckets_) b.clear();
    // (nearest cells first, and the near LODs drawn first: what's in front fills the depth first, so less is shaded behind it)
    order_.clear();
    for (int k = 0; k < (int)cells_.size(); k++)
        if (cells_[k].bounds.visible(vp)) order_.push_back({cells_[k].bounds.distance(eye), k});
    std::sort(order_.begin(), order_.end());
    for (const auto& ok : order_) {
        const Cell& c = cells_[ok.second];
        for (int i : c.inst) {
            const forest::Inst& in = inst_[i];
            const double d = std::hypot(in.x - eye.x, in.z - eye.z);
            int lod = 0;
            while (lod < L - 1 && d > forest::lodReach(lod)) lod++;
            if (forced >= 0) lod = forced;
            buckets_[in.shape * L + std::min(L - 1, lod + lodBias)].push_back(i);
        }
    }
    data_.clear();
    struct Run {
        int bucket, n;
        size_t at;
    };
    std::vector<Run> runs;
    for (int l = 0; l < L; l++)
        for (int b = l; b < (int)buckets_.size(); b += L) {
            if (buckets_[b].empty()) continue;
            runs.push_back({b, (int)buckets_[b].size(), data_.size() / 8});
            for (int i : buckets_[b]) {
                const forest::Inst& in = inst_[i];
                data_.insert(data_.end(), {in.x, in.y, in.z, in.yaw, in.scale, in.tint, in.warm, 0.0f});
            }
        }
    if (runs.empty()) return 0;
    BindVertexArray(vao_);
    BindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, data_.size() * sizeof(float), data_.data(), GL_STREAM_DRAW);
    unsigned verts = 0;
    for (const Run& r : runs) {
        const size_t off = r.at * 32;
        VertexAttribPointer(3, 4, GL_FLOAT, 0, 32, (void*)off);
        VertexAttribPointer(4, 4, GL_FLOAT, 0, 32, (void*)(off + 16));
        const Range& g = ranges_[r.bucket / L][r.bucket % L];
        if (g.count) DrawArraysInstanced(GL_TRIANGLES, g.first, g.count, r.n);
        verts += (unsigned)g.count * r.n;
    }
    BindVertexArray(0);
    return verts;
}
