#include "treedraw.h"
#include "loadprof.h"

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
invariant gl_Position;  // (the depth pre-pass and the shading pass must land on the same depths)
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
    float g1 = vnoise(NP.xz * 2.3 + NP.y * 1.1), g2 = vnoise(NP.zy * 2.7 - NP.x * 0.9);
    base *= 0.72 + 0.5 * (0.5 * g1 + 0.5 * g2);
    float vein = 1.0 - smoothstep(0.0, 0.04, abs(vnoise(NP.xy * 0.9 + NP.z * 0.6) - 0.5));
    base *= 1.0 - 0.35 * vein;
    float lichen = smoothstep(0.6, 0.72, vnoise(NP.xz * 3.1 + NP.y * 2.0)) * smoothstep(0.35, 0.85, n.y);
    base = mix(base, vec3(0.42, 0.45, 0.26), lichen * 0.5);
    specK = 0.06; specP = 20.0;
  } else if (mat == 14) {
    float g = vnoise(vec2((NP.x + NP.z) * 2.0, NP.y * 14.0)) * 0.6 + vnoise(vec2(NP.y * 2.0, (NP.x - NP.z) * 16.0)) * 0.4;
    base *= 0.78 + 0.35 * g;
    specK = 0.03;
  } else if (mat == 13) {
    base *= 0.8 + 0.4 * vnoise(NP.xz * 2.2 + NP.y * 1.7);
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

// The depth pre-pass (the main pass's trees, before they're shaded): the same instances, the same pixels dropped (fading at the
// reach, the see-through trees' leaves), depth only. The shading pass after it then shades each pixel once, not once a leaf layer.
const char* const kTreePreFSBody = R"(
in vec3 vPos; centroid in vec4 vCol; in float vFade;
uniform int uTreeFadeN;
uniform vec4 uTreeFade[24];
void main(){
  if (vFade < ditherAt(gl_FragCoord.xy)) discard;
  int mat = int(vCol.a + 0.5);
  if (uTreeFadeN > 0 && (mat == 13 || mat == 15)) {
    float fade = 0.0;
    for (int i = 0; i < uTreeFadeN; i++) {
      vec4 t = uTreeFade[i];
      vec2 d = vPos.xz - t.xy;
      if (dot(d, d) < t.z * t.z) fade = max(fade, t.w);
    }
    if (fade > ditherAt(gl_FragCoord.xy)) discard;
  }
})";

const char* const kTreeDepthVS = "#version 330 core\n" TREE_PLACE R"(
uniform mat4 uLightVP;
void main(){ gl_Position = uLightVP * vec4(placed(), 1.0); })";

// ---- Pictures (impostors) ----------------------------------------------------------------------------------------------------
// Baking: a model seen from side k (k * 45 degrees round it, level), orthographic, into its layer's cell k: the base colour (alpha:
// covered) and the normal in the model's frame (alpha: what it's made of, 1 leaves, 0.5 snow, 0 wood and the rest).
const char* const kImpostorCaptureVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform vec3 uRight, uE; uniform vec2 uRH;
out vec3 vN; out vec4 vC;
void main(){
  vN = aNrm; vC = aCol;
  gl_Position = vec4(dot(aPos, uRight) / uRH.x, aPos.y / uRH.y * 2.0 - 1.0, -dot(aPos, uE) / (uRH.x * 1.5), 1.0);
})";
const char* const kImpostorCaptureFS = R"(#version 330 core
in vec3 vN; in vec4 vC;
layout(location=0) out vec4 oA;
layout(location=1) out vec4 oB;
void main(){
  int m = int(vC.a + 0.5);
  oA = vec4(vC.rgb, 1.0);
  oB = vec4(normalize(vN) * 0.5 + 0.5, m == 13 ? 1.0 : m == 15 ? 0.5 : 0.0);
})";

// The quad: standing at the base, R each side and H tall, turned to the eye (or the light), the two baked sides nearest the
// direction it's seen from. fade: drawn where the pixel's dither is in [x, y) (x: the meshes' share, y: what's left to it before the
// far forest's): the meshes, these and the far forest's dither the same pixels, so a hand-over is a dissolve. y <= x: not drawn.
const char* const kImpostorPlace = R"(
uniform mat4 uVP; uniform vec3 uEye; uniform int uFaceLight; uniform vec3 uLightDir;
out vec3 vPos; out vec3 vUv0; out vec3 vUv1; out float vMix; out vec2 vLook; out vec3 vAx0; out vec3 vAx2; out vec3 vRight; out float vR; out vec2 vFade;
void impostor(vec3 base, float yaw, float R, float H, float layer, vec2 look, vec2 fade){
  vFade = fade;
  if (fade.y <= fade.x) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
  int id = gl_VertexID;  // (a strip of 4)
  vec2 q = vec2(float(id & 1), float(id >> 1));
  vec3 toward = uFaceLight == 1 ? uLightDir : uEye - base;
  vec2 e = toward.xz;
  e = dot(e, e) > 1e-8 ? normalize(e) : vec2(1.0, 0.0);
  vec3 right = vec3(e.y, 0.0, -e.x);
  vPos = base + right * ((q.x * 2.0 - 1.0) * R) + vec3(0.0, q.y * H, 0.0);
  float c = cos(yaw), s = sin(yaw);
  vec3 ax0 = vec3(c, 0.0, -s), ax2 = vec3(s, 0.0, c), E = vec3(e.x, 0.0, e.y);
  float f = atan(dot(E, ax2), dot(E, ax0)) / 6.2831853 * 8.0;
  f = f - 8.0 * floor(f / 8.0);
  float k0 = floor(f), k1 = mod(k0 + 1.0, 8.0);
  vMix = f - k0;
  vUv0 = vec3((k0 + q.x) / 8.0, q.y, layer);
  vUv1 = vec3((k1 + q.x) / 8.0, q.y, layer);
  vLook = look; vAx0 = ax0; vAx2 = ax2; vRight = right; vR = R;
  gl_Position = uVP * vec4(vPos, 1.0);
}
)";

// The window's trees: per instance its base and turn (3), half-width, height, tint and hue (4), layer and thinning hash (5).
// uImpFade: the meshes hand over at x (over the band y before it), the far forest takes over at z (band w; z 0: never). uThin: thinned
// as the meshes were (forest::Picker::pick), 0 not.
const char* const kImpostorVS = R"(
layout(location=3) in vec4 iPos;
layout(location=4) in vec4 iSize;
layout(location=5) in vec4 iMore;
uniform vec4 uImpFade; uniform float uThin;
void main(){
  float d = length(iPos.xz - uEye.xz);
  float lo = clamp((uImpFade.x - d) / uImpFade.y, 0.0, 1.0);
  float hi = uImpFade.z > 0.0 ? clamp((uImpFade.z - d) / uImpFade.w, 0.0, 1.0) : 1.0;
  if (uThin > 0.0 && iMore.y > 1.0 - 0.75 * smoothstep(0.33 * uThin, uThin, d)) hi = 0.0;
  impostor(iPos.xyz, iPos.w, iSize.x, iSize.y, iMore.x, iSize.zw, vec2(lo, hi));
})";

// Shaded as kTreeFSBody shades the meshes: leaves wrapped and lit through, the rest plain; the shadow looked up on the side facing
// the eye, not in the tree's middle (where the quad is).
const char* const kImpostorFSBody = R"(
in vec3 vPos; in vec3 vUv0; in vec3 vUv1; in float vMix; in vec2 vLook; in vec3 vAx0; in vec3 vAx2; in vec3 vRight; in float vR; in vec2 vFade;
out vec4 oCol;
uniform sampler2DArray uImpCol, uImpNrm;
void main(){
  float u = ditherAt(gl_FragCoord.xy);
  if (u < vFade.x || u >= vFade.y) discard;
  vec4 c = mix(texture(uImpCol, vUv0), texture(uImpCol, vUv1), vMix);
  if (c.a < 0.5) discard;
  vec4 nn = mix(texture(uImpNrm, vUv0), texture(uImpNrm, vUv1), vMix);
  vec3 base = c.rgb / c.a * vLook.x;
  vec3 nm = nn.rgb / c.a * 2.0 - 1.0;
  float leaf = smoothstep(0.6, 0.9, nn.a / c.a);
  vec3 n = normalize(vAx0 * nm.x + vec3(0.0, nm.y, 0.0) + vAx2 * nm.z);
  float w = vLook.y;
  base *= mix(vec3(1.0), w > 0.0 ? mix(vec3(1.0), vec3(1.14, 1.05, 0.72), w) : mix(vec3(1.0), vec3(0.9, 0.98, 1.1), -w), leaf);
  vec3 lin = pow(max(base, 0.0), vec3(2.2));
  vec3 P = vPos + (n - dot(n, vRight) * vRight) * vR * 0.7;
  float sh = shadowAt(P, n);
  vec3 amb = ambientAt(n);
  vec3 V = normalize(uCam - vPos);
  vec3 lspec, lamp = lampLight(P, n, V, 12.0, 0.4 * leaf, lspec);
  float wrap = max(dot(n, uSun) * 0.6 + 0.4, 0.0), through = pow(max(dot(-V, uSun), 0.0), 4.0) * 0.5;
  vec3 lit = mix(amb + max(dot(n, uSun), 0.0) * sh * kSunCol, amb * 0.9 + (wrap + through) * sh * kSunCol, leaf);
  oCol = finishColor(lin * (lit + lamp), vPos, 1.0);
})";

const char* const kImpostorDepthFS = R"(#version 330 core
in vec3 vPos; in vec3 vUv0; in vec3 vUv1; in float vMix; in vec2 vLook; in vec3 vAx0; in vec3 vAx2; in vec3 vRight; in float vR; in vec2 vFade;
uniform sampler2DArray uImpCol;
void main(){ if (mix(texture(uImpCol, vUv0).a, texture(uImpCol, vUv1).a, vMix) < 0.5) discard; })";

namespace {
constexpr int kInstFloats = 12;
constexpr GLenum GL_FRAMEBUFFER_BINDING_ = 0x8CA6, GL_VIEWPORT_ = 0x0BA2, GL_CURRENT_PROGRAM_ = 0x8B8D, GL_VERTEX_ARRAY_BINDING_ = 0x85B5;
}

void ImpostorAtlas::bind(int unitCol, int unitNrm) const {
    ActiveTexture(GL_TEXTURE0 + unitCol);
    BindTexture(GL_TEXTURE_2D_ARRAY, tex[0]);
    ActiveTexture(GL_TEXTURE0 + unitNrm);
    BindTexture(GL_TEXTURE_2D_ARRAY, tex[1]);
    ActiveTexture(GL_TEXTURE0);
}

// Bakes the looks the window's trees have that aren't baked yet (a look: kind, design and snow; the seeds' small differences don't
// show that far off), from each one's LOD 2 mesh (LOD 3, the plainest, is what they take over from).
void TreeDraw::captureImpostors() {
    shapeLayer_.assign(set_.shapes.size(), -1);
    if (!atlas_ || !atlas_->captureProg || set_.shapes.empty()) return;
    ImpostorAtlas& A = *atlas_;
    const int kImpViews = A.kViews, kImpCellW = A.kCellW, kImpCellH = A.kCellH, kImpMaxLayers = A.kMaxLayers;
    std::vector<ImpostorAtlas::Layer>& impLayers_ = A.layers;
    const GLuint captureProg_ = A.captureProg;
    const int W = kImpViews * kImpCellW, H = kImpCellH;
    if (!A.tex[0]) {
        GenTextures(2, A.tex);
        for (int t = 0; t < 2; t++) {
            BindTexture(GL_TEXTURE_2D_ARRAY, A.tex[t]);
            int w = W, h = H, lv = 0;
            for (;;) {
                TexImage3D(GL_TEXTURE_2D_ARRAY, lv, GL_RGBA8, w, h, kImpMaxLayers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                if (w == 1 && h == 1) break;
                w = std::max(1, w / 2), h = std::max(1, h / 2), lv++;
            }
            TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        BindTexture(GL_TEXTURE_2D_ARRAY, 0);
        GenFramebuffers(1, &A.fbo);
        GenRenderbuffers(1, &A.depth);
        BindRenderbuffer(GL_RENDERBUFFER, A.depth);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH_COMPONENT24, W, H);
        BindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    GLint prevFbo = 0, prevVp[4] = {}, prevProg = 0, prevVao = 0;
    GetIntegerv(GL_FRAMEBUFFER_BINDING_, &prevFbo);
    GetIntegerv(GL_VIEWPORT_, prevVp);
    GetIntegerv(GL_CURRENT_PROGRAM_, &prevProg);
    GetIntegerv(GL_VERTEX_ARRAY_BINDING_, &prevVao);
    bool any = false;
    for (size_t s = 0; s < set_.shapes.size(); s++) {
        const forest::Shape& sh = set_.shapes[s];
        int layer = -1;
        for (size_t l = 0; l < impLayers_.size(); l++)
            if (impLayers_[l].kind == sh.look.kind && impLayers_[l].design == sh.look.design && impLayers_[l].snow == sh.look.snow) layer = (int)l;
        if (layer < 0 && (int)impLayers_.size() < kImpMaxLayers) {
            int lod = std::min(2, forest::kLods - 1);
            while (lod > 0 && !ranges_[s][lod].count) lod--;
            const Range& g = ranges_[s][lod];
            if (g.count) {
                layer = (int)impLayers_.size();
                ImpostorAtlas::Layer L;
                L.kind = sh.look.kind, L.design = sh.look.design, L.snow = sh.look.snow;
                L.R = sh.reach / 1.41421356f * 1.06f, L.H = sh.top * 1.02f;  // (reach is round the base's square: the farthest a side can stick out)
                impLayers_.push_back(L);
                if (!any) {
                    BindFramebuffer(GL_FRAMEBUFFER, A.fbo);
                    FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, A.depth);
                    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
                    DrawBuffers(2, bufs);
                    UseProgram(captureProg_);
                    BindVertexArray(vao_);
                    Enable(GL_DEPTH_TEST);
                    DepthFunc(GL_LESS);
                    Disable(GL_BLEND);
                    Disable(GL_CULL_FACE);
                    any = true;
                }
                for (int t = 0; t < 2; t++) FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + t, A.tex[t], 0, layer);
                Viewport(0, 0, W, H);
                ClearColor(0, 0, 0, 0);
                Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                Uniform2f(uniformLocation(captureProg_, "uRH"), L.R, L.H);
                for (int k = 0; k < kImpViews; k++) {
                    const double a = 6.283185307 * k / kImpViews;
                    Viewport(k * kImpCellW, 0, kImpCellW, kImpCellH);
                    Uniform3f(uniformLocation(captureProg_, "uRight"), (float)std::sin(a), 0, (float)-std::cos(a));
                    Uniform3f(uniformLocation(captureProg_, "uE"), (float)std::cos(a), 0, (float)std::sin(a));
                    DrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, (const void*)((size_t)g.first * sizeof(unsigned)));
                }
            }
        }
        shapeLayer_[s] = layer;
    }
    if (any) {
        BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        Viewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
        UseProgram((GLuint)prevProg);
        BindVertexArray((GLuint)prevVao);
        DepthFunc(GL_LEQUAL);
        for (int t = 0; t < 2; t++) {
            BindTexture(GL_TEXTURE_2D_ARRAY, A.tex[t]);
            GenerateMipmap(GL_TEXTURE_2D_ARRAY);
        }
        BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
}

// The window's trees for the pictures, a picker cell at a time (a cell's trees one run in the buffer).
void TreeDraw::uploadImpostors() {
    impCellFirst_.clear(), impCellCount_.clear();
    for (auto& k : kindLayers_) k.layers.clear();
    if (!atlas_ || !atlas_->captureProg) return;
    captureImpostors();
    const int kImpMaxLayers = ImpostorAtlas::kMaxLayers;
    const std::vector<ImpostorAtlas::Layer>& impLayers_ = atlas_->layers;
    const auto& cells = picker_.cells();
    std::vector<float> d;
    d.reserve(set_.inst.size() * kInstFloats);
    double count[3][ImpostorAtlas::kMaxLayers] = {};
    for (const auto& c : cells) {
        impCellFirst_.push_back((int)(d.size() / kInstFloats));
        int n = 0;
        for (int i : c.inst) {
            const forest::Inst& in = set_.inst[i];
            const int layer = in.shape >= 0 && in.shape < (int)shapeLayer_.size() ? shapeLayer_[in.shape] : -1;
            if (layer < 0) continue;
            const ImpostorAtlas::Layer& L = impLayers_[layer];
            const uint32_t hsh = (uint32_t)i * 2654435761u;  // (forest::Picker::pick's thinning: the same trees stay)
            d.insert(d.end(), {in.x, in.y, in.z, in.yaw, L.R * in.scale, L.H * in.scale * in.stretch, in.tint, in.warm, (float)layer,
                               (float)((hsh >> 8) * (1.0 / 16777216.0)), 0.0f, 0.0f});
            n++;
            if (L.kind >= 0 && L.kind < 3) count[L.kind][layer] += 1;
        }
        impCellCount_.push_back(n);
    }
    for (int k = 0; k < 3; k++) {
        double tot = 0;
        for (int l = 0; l < kImpMaxLayers; l++) tot += count[k][l];
        for (int l = 0; l < kImpMaxLayers; l++)
            if (count[k][l] > 0) kindLayers_[k].layers.push_back({l, (float)(count[k][l] / tot)});
    }
    if (!impVao_) {
        GenVertexArrays(1, &impVao_);
        GenBuffers(1, &impVbo_);
        BindVertexArray(impVao_);
        BindBuffer(GL_ARRAY_BUFFER, impVbo_);
        for (int a = 3; a < 6; a++) {
            EnableVertexAttribArray(a);
            VertexAttribPointer(a, 4, GL_FLOAT, 0, kInstFloats * 4, (void*)(size_t)((a - 3) * 16));
            VertexAttribDivisor(a, 1);
        }
        BindVertexArray(0);
    }
    BindBuffer(GL_ARRAY_BUFFER, impVbo_);
    BufferData(GL_ARRAY_BUFFER, std::max<size_t>(1, d.size()) * sizeof(float), d.empty() ? nullptr : d.data(), GL_STATIC_DRAW);
}

unsigned TreeDraw::drawImpostors(const forest::View& view, const ImpostorRange& r, GLuint prog, bool shadow, VegStats::Pass* stats) {
    if (!impostorsReady() || impCellFirst_.empty() || !prog) return 0;
    const double from = r.from * view.detail, to = r.to > 0 ? r.to * view.detail : 0, thin = r.thin;
    const double reach = to > 0 ? to : thin > 0 ? thin : 1e9;
    picker_.visibleCells(view, shadow ? from : from - r.band * view.detail, reach, impCells_);
    if (impCells_.empty()) return 0;
    std::sort(impCells_.begin(), impCells_.end(), [&](int a, int b) { return impCellFirst_[a] < impCellFirst_[b]; });
    impRuns_.clear();
    for (int k : impCells_) {
        if (!impCellCount_[k]) continue;
        if (!impRuns_.empty() && impRuns_.back().first + impRuns_.back().second == impCellFirst_[k]) impRuns_.back().second += impCellCount_[k];
        else impRuns_.push_back({impCellFirst_[k], impCellCount_[k]});
    }
    Uniform4f(uniformLocation(prog, "uImpFade"), (float)from, shadow ? 0.001f : (float)(r.band * view.detail), (float)to, (float)(r.toBand * view.detail));
    Uniform1f(uniformLocation(prog, "uThin"), (float)thin);
    Uniform3f(uniformLocation(prog, "uEye"), (float)view.eye.x, (float)view.eye.y, (float)view.eye.z);
    Uniform1i(uniformLocation(prog, "uImpCol"), 13);
    Uniform1i(uniformLocation(prog, "uImpNrm"), 14);
    atlas_->bind(13, 14);
    BindVertexArray(impVao_);
    BindBuffer(GL_ARRAY_BUFFER, impVbo_);
    unsigned verts = 0;
    for (const auto& run : impRuns_) {
        const size_t off = (size_t)run.first * kInstFloats * 4;
        for (int a = 3; a < 6; a++) VertexAttribPointer(a, 4, GL_FLOAT, 0, kInstFloats * 4, (void*)(off + (a - 3) * 16));
        DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, run.second);
        verts += 4u * run.second;
        if (stats) stats->add(1, 2LL * run.second, run.second);
    }
    BindVertexArray(0);
    return verts;
}


void TreeDraw::build(const forest::Set& set) {
    Built b;
    prepare(set, b);
    upload(std::move(b));
}

void TreeDraw::prepare(const forest::Set& set, Built& out) {
    forest::Set& set_ = out.set;
    std::vector<float>& v = out.v;
    std::vector<unsigned>& idx = out.idx;
    std::vector<std::array<Range, forest::kLods>>& ranges_ = out.ranges;
    set_ = set;
    size_t& meshVerts_ = out.meshVerts;
    meshVerts_ = set.vertices(), out.baked = set.bakedVertices();
    // The meshes, one after another: position, normal, colour and material (the lit shader's Vtx), each
    // corner once (the smooth ones are shared by a few triangles: shaded once), and the triangles' indices.
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
    out.picker.build(set_);
}

void TreeDraw::upload(Built&& b) {
    set_ = std::move(b.set);
    ranges_ = std::move(b.ranges);
    picker_ = std::move(b.picker);
    meshVerts_ = b.meshVerts, baked_ = b.baked;
    const std::vector<float>& v = b.v;
    const std::vector<unsigned>& idx = b.idx;
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
    // (room for every instance: a pass refills it, never grows it)
    instCap_ = std::max<size_t>(1, set_.inst.size()) * kInstFloats;
    data_.reserve(instCap_);
    BindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, instCap_ * sizeof(float), nullptr, GL_STREAM_DRAW);
    uploadImpostors();
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
    // (Orphaned: the last pass's draws keep theirs. Sized to what this pass draws, rounded up, not to every instance on the map:
    // on a big map that's megabytes a pass for a few thousand trees.)
    size_t cap = 4096;
    while (cap < data_.size()) cap *= 2;
    BufferData(GL_ARRAY_BUFFER, std::min(cap, std::max(instCap_, data_.size())) * sizeof(float), nullptr, GL_STREAM_DRAW);
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

unsigned TreeDraw::redraw(VegStats::Pass* stats) {
    if (runs_.empty()) return 0;
    const int L = forest::kLods;
    BindVertexArray(vao_);
    BindBuffer(GL_ARRAY_BUFFER, instVbo_);
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
