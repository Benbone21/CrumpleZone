#include "treedraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>

using namespace gl;

// Per instance: its base and turn (location 3); its scale, tint, foliage hue and fade (4); how much taller
// than its scale (5). A model's front is +X, its left +Z: turned by yaw, front (cos, 0, -sin) and left
// (sin, 0, cos), as appendModel.
#define TREE_PLACE                                                                 \
    "layout(location=0) in vec3 aPos;\n"                                           \
    "layout(location=1) in vec3 aNrm;\n"                                           \
    "layout(location=2) in vec4 aCol;\n"                                           \
    "layout(location=3) in vec4 iPos;\n"                                           \
    "layout(location=4) in vec4 iLook;\n"                                          \
    "layout(location=5) in vec4 iMore;\n"                                          \
    "vec3 turn(vec3 v){ float c = cos(iPos.w), s = sin(iPos.w); return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }\n" \
    "vec3 placed(){ return iPos.xyz + turn(aPos * vec3(iLook.x, iLook.x * iMore.x, iLook.x)); }\n"

const char* const kTreeVS = "#version 330 core\n" TREE_PLACE R"(
uniform mat4 uVP; uniform mat4 uLightVP;
out vec3 vPos; out vec3 vNrm; centroid out vec4 vCol; out vec4 vLight; out vec3 vLocal; out float vFade;  // (centroid: see kLitVS)
void main(){
  vec3 wp = placed();
  vec3 col = aCol.rgb * iLook.y;
  // (each tree's leaves a little yellower or bluer than the next)
  if (int(aCol.a + 0.5) == 13) col *= iLook.z > 0.0 ? mix(vec3(1.0), vec3(1.14, 1.05, 0.72), iLook.z) : mix(vec3(1.0), vec3(0.9, 0.98, 1.1), -iLook.z);
  vPos = wp; vNrm = turn(aNrm * vec3(1.0, 1.0 / iMore.x, 1.0)); vCol = vec4(col, aCol.a); vLocal = aPos; vFade = iLook.w;
  vLight = uLightVP * vec4(wp, 1.0);
  gl_Position = uVP * vec4(wp, 1.0);
})";

// The lit shader's (kLitFSBody) for what trees, bushes and logs are made of, and no more: matte, rock,
// foliage, wood, snow on the boughs. (It does what the lit one does for those, so they look the same.)
const char* const kTreeFSBody = R"(
in float vFade;
void main(){
  if (vFade < ditherAt(gl_FragCoord.xy)) discard;  // (fading out at its reach)
  vec3 n = normalize(vNrm);
  vec3 base = vCol.rgb;
  int mat = int(vCol.a + 0.5);
  if (uTreeFadeN > 0 && (mat == 13 || mat == 15)) {  // a tree the camera sees through: its leaves (and the snow on them) dither away; trunks and limbs never do
    float fade = 0.0;
    for (int i = 0; i < uTreeFadeN; i++) {
      vec4 t = uTreeFade[i];
      vec2 d = vPos.xz - t.xy;
      if (dot(d, d) < t.z * t.z) fade = max(fade, t.w);
    }
    if (fade > ditherAt(gl_FragCoord.xy)) discard;
  }
  float specK = 0.04, specP = 16.0;
  if (mat == 12) {
    float g1 = vnoise(vPos.xz * 2.3 + vPos.y * 1.1), g2 = vnoise(vPos.zy * 2.7 - vPos.x * 0.9);
    base *= 0.72 + 0.5 * (0.5 * g1 + 0.5 * g2);
    float vein = 1.0 - smoothstep(0.0, 0.04, abs(vnoise(vPos.xy * 0.9 + vPos.z * 0.6) - 0.5));
    base *= 1.0 - 0.35 * vein;
    float lichen = smoothstep(0.6, 0.72, vnoise(vPos.xz * 3.1 + vPos.y * 2.0)) * smoothstep(0.35, 0.85, n.y);
    base = mix(base, vec3(0.42, 0.45, 0.26), lichen * 0.5);
    specK = 0.06; specP = 20.0;
  } else if (mat == 14) {
    float g = vnoise(vec2((vPos.x + vPos.z) * 2.0, vPos.y * 14.0)) * 0.6 + vnoise(vec2(vPos.y * 2.0, (vPos.x - vPos.z) * 16.0)) * 0.4;
    base *= 0.78 + 0.35 * g;
    specK = 0.03;
  } else if (mat == 13) {
    base *= 0.8 + 0.4 * vnoise(vPos.xz * 2.2 + vPos.y * 1.7);
    specK = 0.0;
  }
  vec3 lin = pow(max(base, 0.0), vec3(2.2));
  float sh = shadowAt(vPos, n);
  vec3 amb = ambientAt(n);
  vec3 V = normalize(uCam - vPos);
  vec3 lspec, lamp = lampLight(vPos, n, V, max(specP, 12.0), mat == 13 ? 0.4 : 0.0, lspec);
  vec3 col;
  if (mat == 13) {  // leaves: soft wrapped light, and sun glowing through from behind
    float wrap = max(dot(n, uSun) * 0.6 + 0.4, 0.0);
    float through = pow(max(dot(-V, uSun), 0.0), 4.0) * 0.5;
    col = lin * (amb * 0.9 + (wrap + through) * sh * kSunCol + lamp);
  } else {
    vec3 H = normalize(V + uSun);
    col = lin * (amb + max(dot(n, uSun), 0.0) * sh * kSunCol + lamp) + pow(max(dot(n, H), 0.0), specP) * specK * sh * 2.0 * kSunSpec + lspec * specK * 2.0;
  }
  oCol = finishColor(col, vPos, 1.0);
})";

const char* const kTreeDepthVS = "#version 330 core\n" TREE_PLACE R"(
uniform mat4 uLightVP;
void main(){ gl_Position = uLightVP * vec4(placed(), 1.0); })";

namespace {
constexpr int kInstFloats = 12;
}

void GpuTimer::begin() {
    if (!q[0]) GenQueries(4, q);
    f = (f + 1) % 4;
    const int old = (f + 1) % 4;  // (the one begun three frames back is done by now)
    if (used[old]) {
        GLuint64 ns = 0;
        GetQueryObjectui64v(q[old], GL_QUERY_RESULT, &ns);
        ms = ns / 1e6;
        used[old] = false;
    }
    BeginQuery(GL_TIME_ELAPSED, q[f]);
    used[f] = true;
}
void GpuTimer::end() { EndQuery(GL_TIME_ELAPSED); }

void TreeDraw::build(const forest::Set& set) {
    set_ = set;
    meshVerts_ = set.vertices(), baked_ = set.bakedVertices();
    // The meshes, one after another: position, normal, colour and material (the lit shader's Vtx), each
    // corner once (the smooth ones are shared by a few triangles: shaded once), and the triangles' indices.
    std::vector<float> v;
    std::vector<unsigned> idx;
    v.reserve(meshVerts_ * 6);
    idx.reserve(meshVerts_);
    ranges_.assign(set.shapes.size(), {});
    std::map<std::array<float, 10>, unsigned> seen;
    for (size_t s = 0; s < set.shapes.size(); s++) {
        forest::Shape& sh = set_.shapes[s];
        for (int l = 0; l < forest::kLods; l++) {
            ranges_[s][l].first = (int)idx.size(), ranges_[s][l].count = (int)sh.lod[l].v.size();
            seen.clear();
            for (const mk::MVert& q : sh.lod[l].v) {
                const std::array<float, 10> k = {q.p[0], q.p[1], q.p[2], q.n[0], q.n[1], q.n[2], q.c[0], q.c[1], q.c[2], (float)q.mat};
                auto it = seen.find(k);
                if (it == seen.end()) it = seen.emplace(k, (unsigned)(v.size() / 10)).first, v.insert(v.end(), k.begin(), k.end());
                idx.push_back(it->second);
            }
            // (the picker only asks whether a LOD has any: keep one corner of it)
            if (sh.lod[l].v.size() > 1) sh.lod[l].v.resize(1), sh.lod[l].v.shrink_to_fit();
        }
    }
    if (!vao_) {
        GenVertexArrays(1, &vao_);
        GenBuffers(1, &vbo_);
        GenBuffers(1, &instVbo_);
        GenBuffers(1, &ibo_);
        BindVertexArray(vao_);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        BindBuffer(GL_ARRAY_BUFFER, vbo_);
        VertexAttribPointer(0, 3, GL_FLOAT, 0, 40, (void*)0);
        VertexAttribPointer(1, 3, GL_FLOAT, 0, 40, (void*)12);
        VertexAttribPointer(2, 4, GL_FLOAT, 0, 40, (void*)24);
        for (int a = 0; a < 3; a++) EnableVertexAttribArray(a);
        BindBuffer(GL_ARRAY_BUFFER, instVbo_);
        for (int a = 3; a < 6; a++) {
            EnableVertexAttribArray(a);
            VertexAttribPointer(a, 4, GL_FLOAT, 0, kInstFloats * 4, (void*)(size_t)((a - 3) * 16));
            VertexAttribDivisor(a, 1);
        }
        BindVertexArray(0);
    }
    BindBuffer(GL_ARRAY_BUFFER, vbo_);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    BindVertexArray(vao_);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    BindVertexArray(0);
    uniqueVerts_ = v.size() / 10;
    picker_.build(set_);
    // (room for every instance: a pass refills it, never grows it)
    instCap_ = std::max<size_t>(1, set_.inst.size()) * kInstFloats;
    data_.reserve(instCap_);
    BindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, instCap_ * sizeof(float), nullptr, GL_STREAM_DRAW);
}

unsigned TreeDraw::draw(const forest::View& view, VegStats::Pass* stats) {
    if (!vao_ || set_.inst.empty()) return 0;
    const int L = forest::kLods;
    static const int forced = std::getenv("CZ_TREE_LOD") ? std::clamp(std::atoi(std::getenv("CZ_TREE_LOD")), 0, L - 1) : -1;  // (screenshots of one LOD)
    picker_.pick(set_, view, buckets_);
    if (forced >= 0)
        for (size_t b = 0; b < buckets_.size(); b++)
            if (b % L != (size_t)forced && !buckets_[b].empty()) {
                auto& to = buckets_[b - b % L + forced];
                to.insert(to.end(), buckets_[b].begin(), buckets_[b].end()), buckets_[b].clear();
            }
    data_.clear();
    runs_.clear();
    for (int l = 0; l < L; l++)
        for (int b = l; b < (int)buckets_.size(); b += L) {
            if (buckets_[b].empty() || !ranges_[b / L][l].count) continue;
            runs_.push_back({b, (int)buckets_[b].size(), data_.size() / kInstFloats});
            for (const forest::Pick& p : buckets_[b]) {
                const forest::Inst& in = set_.inst[p.inst];
                data_.insert(data_.end(), {in.x, in.y, in.z, in.yaw, in.scale, in.tint, in.warm, p.fade, in.stretch, 0.0f, 0.0f, 0.0f});
            }
        }
    if (runs_.empty()) return 0;
    BindVertexArray(vao_);
    BindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, instCap_ * sizeof(float), nullptr, GL_STREAM_DRAW);  // (orphaned: the last pass's draws keep theirs)
    BufferSubData(GL_ARRAY_BUFFER, 0, data_.size() * sizeof(float), data_.data());
    unsigned verts = 0;
    for (const Run& r : runs_) {
        const size_t off = r.at * kInstFloats * 4;
        for (int a = 3; a < 6; a++) VertexAttribPointer(a, 4, GL_FLOAT, 0, kInstFloats * 4, (void*)(off + (a - 3) * 16));
        const Range& g = ranges_[r.bucket / L][r.bucket % L];
        DrawElementsInstanced(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, (const void*)((size_t)g.first * sizeof(unsigned)), r.n);
        verts += (unsigned)g.count * r.n;
        if (stats) stats->add(1, (long long)g.count / 3 * r.n, r.n);
    }
    BindVertexArray(0);
    return verts;
}
