#include "propdraw.h"
#include "loadprof.h"
#include "meshcache.h"
#include "texcomp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

// stb_image as static functions (groundtex.cpp has its own copy).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

using namespace gl;

namespace {

// ---- Shaders ---------------------------------------------------------------------------
// Instanced props: per instance a model -> world affine (three rows) and extras (tint, wind
// phase, snow, hidden). Normals go through the cofactor matrix, so stretched rocks still shade
// right. Leaves sway with the wind, more toward the top.
const char* kPropVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aTan;
layout(location=3) in vec2 aUv;
layout(location=4) in vec4 aM0;
layout(location=5) in vec4 aM1;
layout(location=6) in vec4 aM2;
layout(location=7) in vec4 aExtra;
uniform mat4 uVP;
uniform float uTime, uWind, uTop;
out vec3 vPos; out vec3 vNrm; out vec4 vTan; out vec2 vUv; out vec4 vExtra;
void main(){
  mat3 A = transpose(mat3(aM0.xyz, aM1.xyz, aM2.xyz));  // columns: the model's axes in the world
  vec3 off = vec3(0.0);
  if (uWind > 0.0) {
    float h = clamp(aPos.y / max(uTop, 1e-3), 0.0, 1.0);
    float ph = aExtra.y;
    vec2 sway = vec2(sin(uTime * 1.3 + ph), cos(uTime * 1.1 + ph * 1.7)) * 0.6
              + vec2(sin(uTime * 4.7 + ph * 3.0 + aPos.x * 9.0), cos(uTime * 5.3 + aPos.z * 9.0)) * 0.15;
    off = vec3(sway.x, 0.0, sway.y) * uWind * uTop * h * h;
  }
  vPos = vec3(dot(aM0, vec4(aPos, 1.0)), dot(aM1, vec4(aPos, 1.0)), dot(aM2, vec4(aPos, 1.0))) + A * off;
  mat3 C = mat3(cross(A[1], A[2]), cross(A[2], A[0]), cross(A[0], A[1]));  // det(A) x inverse transpose
  vNrm = C * aNrm;
  vTan = vec4(A * aTan.xyz, aTan.w);
  vUv = aUv;
  vExtra = aExtra;
  gl_Position = uVP * vec4(vPos, 1.0);
})";

const char* kPropFSBody = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vTan; in vec2 vUv; in vec4 vExtra;
out vec4 oCol;
uniform sampler2D uDiff, uNorm, uArm;
uniform int uAlpha, uFoliage, uHasNorm, uHasArm;
uniform float uCutoff;
void main(){
  if (uFoliage == 1 && vExtra.w > ditherAt(gl_FragCoord.xy)) discard;  // a tree the camera sees through: its leaves only, never the trunk (the extra's w: how much is hidden)
  vec4 d = texture(uDiff, vUv);
  if (uAlpha == 1 && d.a < uCutoff) discard;
  vec3 N = normalize(vNrm);
  if (!gl_FrontFacing) N = -N;  // two-sided cards: the back is lit as a back
  vec3 n = N;
  if (uHasNorm == 1) {
    vec3 T = vTan.xyz - N * dot(N, vTan.xyz);
    if (dot(T, T) > 1e-10) {
      T = normalize(T);
      vec3 B = cross(N, T) * (vTan.w < 0.0 ? -1.0 : 1.0);
      vec2 t = texture(uNorm, vUv).xy * 2.0 - 1.0;  // (BC5: two channels; Z is rebuilt)
      float tz = sqrt(max(1.0 - dot(t, t), 0.0025));
      n = normalize(T * t.x + B * t.y + N * tz);
    }
  }
  float ao = 1.0, rough = 0.85;
  if (uHasArm == 1) { vec3 a = texture(uArm, vUv).rgb; ao = a.r; rough = max(a.g, 0.05); }
  vec3 alb = d.rgb * vExtra.x;
  // Snow lying on whatever faces up (props standing in snow).
  float snow = vExtra.z * smoothstep(0.45, 0.8, n.y + 0.15 * (hash(floor(vPos.xz * 6.0)) - 0.5));
  alb = mix(alb, vec3(0.80, 0.83, 0.88), snow);
  rough = mix(rough, 0.6, snow);
  float sh = shadowAt(vPos, N);
  vec3 V = normalize(uCam - vPos);
  vec3 lspec, col;
  if (uFoliage == 1) {  // leaves and needles: soft wrapped light, the sun glowing through
    float wrap = max(dot(n, uSun) * 0.6 + 0.4, 0.0);
    float through = pow(max(dot(-V, uSun), 0.0), 4.0) * 0.6;
    vec3 lamp = lampLight(vPos, n, V, 12.0, 0.4, lspec);
    col = alb * (ambientAt(n) * ao * 0.9 + (wrap + through) * sh * kSunCol + lamp);
  } else {
    float NL = max(dot(n, uSun), 0.0);
    vec3 H = normalize(V + uSun);
    float P = clamp(2.0 / (rough * rough) - 2.0, 2.0, 400.0);
    float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 spec = vec3(pow(max(dot(n, H), 0.0), P) * (P + 2.0) * 0.0398 * F * NL * sh) * kSunSpec * 2.6;
    vec3 lamp = lampLight(vPos, n, V, max(P, 12.0), 0.0, lspec);
    col = alb * (ambientAt(n) * ao + NL * mix(1.0, ao, 0.5) * sh * kSunCol + lamp) + spec + lspec * 0.1 * (1.0 - rough);
  }
  oCol = finishColor(col, vPos, 1.0);
})";

const char* kPropDepthFS = R"(#version 330 core
in vec3 vPos; in vec3 vNrm; in vec4 vTan; in vec2 vUv; in vec4 vExtra;
uniform sampler2D uDiff;
uniform int uAlpha;
uniform float uCutoff;
void main(){ if (uAlpha == 1 && texture(uDiff, vUv).a < uCutoff) discard; })";

// Far trees: a quad standing at the trunk, turned to the camera (or to the light, for the
// shadow pass), showing the two baked views nearest the direction it's seen from, blended.
// Per instance: base and yaw, then reach, height, tint and snow.
const char* kBillboardVS = R"(#version 330 core
layout(location=4) in vec4 aB0;
layout(location=5) in vec4 aB1;
uniform mat4 uVP;
uniform vec3 uEye, uLightDir;
uniform int uFaceLight;
uniform float uViews;
out vec3 vPos; out vec2 vUv0; out vec2 vUv1; out float vMix; out vec4 vB1; out vec3 vAx0; out vec3 vAx2;
void main(){
  int id = gl_VertexID;
  vec2 q = vec2((id == 1 || id == 4 || id == 5) ? 1.0 : 0.0, (id == 2 || id == 3 || id == 5) ? 1.0 : 0.0);
  vec3 base = aB0.xyz;
  float yaw = aB0.w, R = aB1.x, H = aB1.y;
  vec3 toward = uFaceLight == 1 ? uLightDir : uEye - base;
  vec2 e = toward.xz;
  e = dot(e, e) > 1e-8 ? normalize(e) : vec2(1.0, 0.0);
  vec3 right = vec3(e.y, 0.0, -e.x);
  vPos = base + right * ((q.x * 2.0 - 1.0) * R) + vec3(0.0, q.y * H, 0.0);
  // The view direction in the tree's own frame picks the atlas cells.
  vec3 ax0 = vec3(cos(yaw), 0.0, -sin(yaw)), ax2 = vec3(sin(yaw), 0.0, cos(yaw));
  float a = atan(dot(vec3(e.x, 0.0, e.y), ax2), dot(vec3(e.x, 0.0, e.y), ax0));
  float f = a / 6.2831853 * uViews;
  f = f - uViews * floor(f / uViews);
  float k0 = floor(f);
  vMix = f - k0;
  float k1 = mod(k0 + 1.0, uViews);
  vUv0 = vec2((k0 + q.x) / uViews, 1.0 - q.y);
  vUv1 = vec2((k1 + q.x) / uViews, 1.0 - q.y);
  vB1 = aB1; vAx0 = ax0; vAx2 = ax2;
  gl_Position = uVP * vec4(vPos, 1.0);
})";

const char* kBillboardFSBody = R"(
in vec3 vPos; in vec2 vUv0; in vec2 vUv1; in float vMix; in vec4 vB1; in vec3 vAx0; in vec3 vAx2;
out vec4 oCol;
uniform sampler2D uDiff, uNorm;
void main(){
  vec4 a = texture(uDiff, vUv0), b = texture(uDiff, vUv1);
  vec4 d = mix(a, b, vMix);
  if (d.a < 0.5) discard;
  vec3 nm = mix(texture(uNorm, vUv0).xyz, texture(uNorm, vUv1).xyz, vMix) * 2.0 - 1.0;
  vec3 n = normalize(vAx0 * nm.x + vec3(0.0, nm.y, 0.0) + vAx2 * nm.z);
  vec3 alb = d.rgb * vB1.z;
  alb = mix(alb, vec3(0.80, 0.83, 0.88), vB1.w * smoothstep(0.45, 0.8, n.y));
  float sh = shadowAt(vPos, n);
  vec3 V = normalize(uCam - vPos);
  float wrap = max(dot(n, uSun) * 0.6 + 0.4, 0.0);
  float through = pow(max(dot(-V, uSun), 0.0), 4.0) * 0.6;
  vec3 lspec, lamp = lampLight(vPos, n, V, 12.0, 0.4, lspec);
  vec3 col = alb * (ambientAt(n) * 0.85 + (wrap + through) * sh * kSunCol + lamp);
  oCol = finishColor(col, vPos, 1.0);
})";

const char* kBillboardDepthFS = R"(#version 330 core
in vec3 vPos; in vec2 vUv0; in vec2 vUv1; in float vMix; in vec4 vB1; in vec3 vAx0; in vec3 vAx2;
uniform sampler2D uDiff;
void main(){ if (mix(texture(uDiff, vUv0).a, texture(uDiff, vUv1).a, vMix) < 0.5) discard; })";

// Quality: how far each LOD reaches (x the nominal), where trees turn to billboards (m),
// scatter reach (m) and clumps tried per 12 m cell, texture size.
const float kLodK[4] = {0, 0.6f, 1.0f, 1.5f};
const float kBillboardDist[4] = {0, 55, 80, 110};
const float kScatterReach[4] = {0, 28, 42, 60};
const int kScatterTries[4] = {0, 8, 14, 22};
const int kTexSize[4] = {0, 512, 1024, 2048};
const int kBillboardViews = 8;  // (tools/bakeprops.cpp)
const double kScatterCell = 12.0;
const double kStaticCell = 32.0;

uint32_t hash32(uint32_t x) {
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return x;
}
double hashf(uint32_t a, uint32_t b) { return hash32(a * 0x9e3779b9u ^ hash32(b + 0x632be5abu)) / 4294967296.0; }

using props::boxInFrustum;
using props::sphereInFrustum;

// A decoded image, RGBA.
struct Decoded {
    std::string file;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool alpha = false;  // a real alpha channel (PNG)
    texenc::Encoded enc;  // mips, in the upload format (made on the decode threads)
};
// What a texture is for: colour (sRGB), a normal map (RG; the shader rebuilds Z), or data (roughness, occlusion).
enum Role { ROLE_COLOR, ROLE_NORMAL, ROLE_DATA };
struct Want {
    Role role;
    float cutoff;
};

void decode(const std::string& dir, Decoded& d, int maxSize) {
    int c = 0;
    uint8_t* p = stbi_load((dir + "/tex/" + d.file).c_str(), &d.w, &d.h, &c, 4);
    if (!p) {
        d.w = d.h = 0;
        return;
    }
    d.alpha = c == 4;
    d.px.assign(p, p + (size_t)d.w * d.h * 4);
    stbi_image_free(p);
    while (d.w > maxSize || d.h > maxSize) {  // halve (box)
        const int w2 = std::max(1, d.w / 2), h2 = std::max(1, d.h / 2);
        std::vector<uint8_t> o((size_t)w2 * h2 * 4);
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++)
                for (int k = 0; k < 4; k++) {
                    const int x0 = std::min(2 * x, d.w - 1), x1 = std::min(2 * x + 1, d.w - 1), y0 = std::min(2 * y, d.h - 1), y1 = std::min(2 * y + 1, d.h - 1);
                    o[((size_t)y * w2 + x) * 4 + k] = (uint8_t)((d.px[((size_t)y0 * d.w + x0) * 4 + k] + d.px[((size_t)y0 * d.w + x1) * 4 + k] +
                                                                  d.px[((size_t)y1 * d.w + x0) * 4 + k] + d.px[((size_t)y1 * d.w + x1) * 4 + k] + 2) / 4);
                }
        d.px.swap(o);
        d.w = w2, d.h = h2;
    }
}

// The mip chain: cut-out textures keep their coverage down it (each level's alpha is scaled so as
// many texels pass the cutoff as at the top), so leaves don't thin away into the distance.
std::vector<std::vector<uint8_t>> cutoutChain(const Decoded& d, float cutoff) {
    auto coverage = [&](const std::vector<uint8_t>& px, float scale) {
        size_t n = 0;
        for (size_t i = 3; i < px.size(); i += 4) n += px[i] * scale > cutoff * 255.0f;
        return (double)n / std::max<size_t>(1, px.size() / 4);
    };
    const double want = coverage(d.px, 1.0f);
    std::vector<std::vector<uint8_t>> levels{d.px};
    std::vector<uint8_t> cur = d.px;
    int w = d.w, h = d.h;
    while (w > 1 || h > 1) {
        const int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
        std::vector<uint8_t> o((size_t)w2 * h2 * 4);
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++) {
                const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                const uint8_t* q[4] = {&cur[((size_t)y0 * w + x0) * 4], &cur[((size_t)y0 * w + x1) * 4], &cur[((size_t)y1 * w + x0) * 4], &cur[((size_t)y1 * w + x1) * 4]};
                // Colour weighted by coverage, so the empty texels' colour doesn't bleed in.
                int asum = q[0][3] + q[1][3] + q[2][3] + q[3][3];
                for (int k = 0; k < 3; k++) {
                    int s = 0;
                    for (int j = 0; j < 4; j++) s += asum ? q[j][k] * q[j][3] : q[j][k];
                    o[((size_t)y * w2 + x) * 4 + k] = (uint8_t)(asum ? s / asum : s / 4);
                }
                o[((size_t)y * w2 + x) * 4 + 3] = (uint8_t)((asum + 2) / 4);
            }
        // The alpha scale that keeps the coverage.
        float lo = 0.5f, hi = 8.0f;
        for (int it = 0; it < 14; it++) {
            const float mid = (lo + hi) * 0.5f;
            (coverage(o, mid) < want ? lo : hi) = mid;
        }
        std::vector<uint8_t> up = o;
        for (size_t i = 3; i < up.size(); i += 4) up[i] = (uint8_t)std::min(255.0f, up[i] * hi);
        levels.push_back(std::move(up));
        cur.swap(o);
        w = w2, h = h2;
    }
    return levels;
}

// Mips and block compression (BC1 colour, BC3 with alpha, BC5 normals), on a decode thread.
void encodeTex(Decoded& d, const Want& w) {
    const bool srgb = w.role == ROLE_COLOR;
    auto levels = d.alpha ? cutoutChain(d, w.cutoff) : texenc::mipChain(d.px.data(), d.w, d.h, srgb);
    const texenc::Kind kind = w.role == ROLE_NORMAL ? texenc::Kind::Normal2 : w.role == ROLE_DATA ? texenc::Kind::Data : d.alpha ? texenc::Kind::ColorAlpha : texenc::Kind::Color;
    d.enc = texcomp::make(kind, srgb, levels, d.w, d.h);
    std::vector<uint8_t>().swap(d.px);
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

}  // namespace

bool PropRenderer::init(const std::string& commonFS, const Linker& link, std::string& err) {
    const std::string header = "#version 330 core\n";
    progLit = link(kPropVS, header + commonFS + kPropFSBody, err);
    progDepth = link(kPropVS, kPropDepthFS, err);
    progBB = link(kBillboardVS, header + commonFS + kBillboardFSBody, err);
    progBBDepth = link(kBillboardVS, kBillboardDepthFS, err);
    if (!progLit || !progDepth || !progBB || !progBBDepth) return false;
    GenBuffers(1, &instVbo);
    GenVertexArrays(1, &bbVao);
    GenBuffers(1, &bbVbo);
    BindVertexArray(bbVao);
    BindBuffer(GL_ARRAY_BUFFER, bbVbo);
    for (int k = 0; k < 2; k++) {
        VertexAttribPointer(4 + k, 4, GL_FLOAT, 0, 32, (void*)(size_t)(k * 16));
        EnableVertexAttribArray(4 + k);
        VertexAttribDivisor(4 + k, 1);
    }
    BindVertexArray(0);
    for (auto& f : queries) GenQueries(3, f);
    return true;
}

GLuint PropRenderer::texture(const std::string& file, bool, int) {
    auto it = texCache.find(file);
    return it == texCache.end() ? 0 : it->second;
}

bool PropRenderer::ensureLoaded(int q) {
    if (q <= 0) return loaded();
    if (loaded() || tried) return loaded();
    tried = true;
    const std::string dir = props::assetDir();
    if (dir.empty()) {
        std::fprintf(stderr, "props: assets/props not found (scenery stays code-made)\n");
        return false;
    }
    const std::vector<props::ManifestEntry> list = props::readManifest(dir);
    std::vector<props::Model> src;
    double lp0 = loadprof::nowMs();
    for (const props::ManifestEntry& e : list) {
        props::Model m;
        if (props::loadModel(dir + "/" + e.name + ".prop", m) && !m.lods.empty()) src.push_back(std::move(m));
        else std::fprintf(stderr, "props: can't read %s.prop\n", e.name.c_str());
    }
    if (src.empty()) {
        std::fprintf(stderr, "props: no models in %s (scenery stays code-made)\n", dir.c_str());
        return false;
    }
    loadprof::add("props.read models", loadprof::nowMs() - lp0), lp0 = loadprof::nowMs();
    // Textures: every file once, decoded on a few threads.
    std::vector<Decoded> dec;
    std::map<std::string, Want> want;  // file -> its role, alpha cutoff
    for (const props::Model& m : src) {
        for (const props::Material& mt : m.mats)
            for (int k = 0; k < 3; k++)
                if (!mt.tex[k].empty()) want[mt.tex[k]] = {k == 0 ? ROLE_COLOR : k == 1 ? ROLE_NORMAL : ROLE_DATA, mt.alphaMode ? mt.cutoff : 0.5f};
        if (m.kind == props::KIND_TREE) {
            want[m.name + "_bb_d.png"] = {ROLE_COLOR, 0.5f};
            want[m.name + "_bb_n.jpg"] = {ROLE_DATA, 0.5f};  // (billboard normals use all three channels)
        }
    }
    for (const auto& w : want) {
        Decoded d;
        d.file = w.first;
        dec.push_back(d);
    }
    const int maxSize = kTexSize[std::min(std::max(q, 1), 3)];
    const auto t0 = std::chrono::steady_clock::now();
    texcomp::s3tcAvailable();  // (asks GL: here, not on the threads)
    {
        std::vector<std::thread> pool;
        const int nt = 4;
        for (int t = 0; t < nt; t++)
            pool.emplace_back([&, t] {
                for (size_t i = t; i < dec.size(); i += nt) {
                    // (The encoded blocks are kept on disk, keyed by the source file and the encoder: a later launch skips decode and encode.)
                    const Want& wt = want.at(dec[i].file);
                    meshcache::Hasher key;
                    key.u64((uint64_t)texenc::kEncoderVersion), key.u64(meshcache::fileStamp(dir + "/tex/" + dec[i].file)), key.u64((uint64_t)maxSize);
                    key.u64((uint64_t)wt.role), key.num(wt.cutoff), key.u64(texcomp::s3tcAvailable());
                    std::vector<uint8_t> blob;
                    if (meshcache::getBlob(key.h, blob) && meshcache::unpackEncoded(blob, dec[i].enc)) {
                        dec[i].w = dec[i].enc.w, dec[i].h = dec[i].enc.h;
                        continue;
                    }
                    decode(dir, dec[i], maxSize);
                    if (dec[i].w) {
                        encodeTex(dec[i], wt);
                        meshcache::putBlob(key.h, meshcache::packEncoded(dec[i].enc));
                    }
                }
            });
        for (std::thread& t : pool) t.join();
    }
    loadprof::add("props.decode textures", loadprof::nowMs() - lp0), lp0 = loadprof::nowMs();
    for (const Decoded& d : dec) {
        if (!d.w) {
            std::fprintf(stderr, "props: missing texture %s\n", d.file.c_str());
            continue;
        }
        texCache[d.file] = texcomp::upload2D(d.enc, "prop " + d.file);
    }
    loadprof::add("props.upload textures", loadprof::nowMs() - lp0), lp0 = loadprof::nowMs();
    if (std::getenv("CZ_TEXSTATS"))
        std::fprintf(stderr, "texstats: %zu prop textures, %.0f ms (decode, mips, encode, upload)\n", dec.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    loadedQuality = q;
    // Meshes.
    for (props::Model& m : src) {
        Model g;
        g.name = m.name;
        g.kind = m.kind;
        for (int k = 0; k < 3; k++) g.bmin[k] = m.bmin[k], g.bmax[k] = m.bmax[k];
        g.height = std::max(0.01f, m.bmax[1]);
        g.radius = 0.01f;
        double area = 0;
        for (const props::Sub& s : m.lods[0].subs) {
            for (const props::Vert& v : s.v) g.radius = std::max(g.radius, std::hypot(v.p[0], v.p[2]));
            for (size_t i = 0; i + 2 < s.idx.size(); i += 3) {
                const props::Vert &a = s.v[s.idx[i]], &b = s.v[s.idx[i + 1]], &c = s.v[s.idx[i + 2]];
                const V3 fn = cross(V3(b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2]), V3(c.p[0] - a.p[0], c.p[1] - a.p[1], c.p[2] - a.p[2]));
                g.faceNormal += fn;
                area += len(fn);
            }
        }
        g.faceNormal = norm(g.faceNormal);
        for (const props::Material& mt : m.mats) {
            Mat o;
            for (int k = 0; k < 3; k++) o.tex[k] = mt.tex[k].empty() ? 0 : texture(mt.tex[k], k == 0, maxSize);
            o.alpha = mt.alphaMode ? 1 : 0;
            o.twoSided = mt.doubleSided;
            o.foliage = mt.foliage;
            o.wind = mt.wind;
            o.cutoff = mt.cutoff;
            g.mats.push_back(o);
        }
        for (const props::Lod& l : m.lods) {
            std::vector<Sub> subs;
            int tris = 0;
            for (const props::Sub& s : l.subs) {
                Sub o;
                o.mat = s.mat;
                o.count = (int)s.idx.size();
                tris += o.count / 3;
                // 28 bytes a vertex: position, normal and tangent as bytes, texcoord.
                std::vector<uint8_t> vb(s.v.size() * 28);
                for (size_t i = 0; i < s.v.size(); i++) {
                    uint8_t* p = &vb[i * 28];
                    std::memcpy(p, s.v[i].p, 12);
                    for (int k = 0; k < 3; k++) p[12 + k] = (uint8_t)(int8_t)std::lround(std::fmax(-1.f, std::fmin(1.f, s.v[i].n[k])) * 127);
                    p[15] = 0;
                    for (int k = 0; k < 4; k++) p[16 + k] = (uint8_t)(int8_t)std::lround(std::fmax(-1.f, std::fmin(1.f, s.v[i].t[k])) * 127);
                    std::memcpy(p + 20, s.v[i].uv, 8);
                }
                GenVertexArrays(1, &o.vao);
                GenBuffers(1, &o.vbo);
                GenBuffers(1, &o.ibo);
                BindVertexArray(o.vao);
                BindBuffer(GL_ARRAY_BUFFER, o.vbo);
                BufferData(GL_ARRAY_BUFFER, vb.size(), vb.data(), GL_STATIC_DRAW);
                VertexAttribPointer(0, 3, GL_FLOAT, 0, 28, (void*)0);
                VertexAttribPointer(1, 3, GL_BYTE, 1, 28, (void*)12);
                VertexAttribPointer(2, 4, GL_BYTE, 1, 28, (void*)16);
                VertexAttribPointer(3, 2, GL_FLOAT, 0, 28, (void*)20);
                for (int a = 0; a < 4; a++) EnableVertexAttribArray(a);
                BindBuffer(GL_ARRAY_BUFFER, instVbo);
                for (int a = 0; a < 4; a++) {
                    VertexAttribPointer(4 + a, 4, GL_FLOAT, 0, 64, (void*)(size_t)(a * 16));
                    EnableVertexAttribArray(4 + a);
                    VertexAttribDivisor(4 + a, 1);
                }
                BindBuffer(GL_ELEMENT_ARRAY_BUFFER, o.ibo);
                BufferData(GL_ELEMENT_ARRAY_BUFFER, s.idx.size() * 2, s.idx.data(), GL_STATIC_DRAW);
                subs.push_back(o);
            }
            g.lods.push_back(subs);
            g.lodTris.push_back(tris);
        }
        while (g.lods.size() < 3) g.lods.push_back(g.lods.back()), g.lodTris.push_back(g.lodTris.back());
        if (g.kind == props::KIND_TREE) {
            g.bb[0] = texture(g.name + "_bb_d.png", true, maxSize);
            g.bb[1] = texture(g.name + "_bb_n.jpg", false, maxSize);
        }
        byName[g.name] = (int)models.size();
        models.push_back(std::move(g));
    }
    BindVertexArray(0);
    // Which models stand in for what.
    for (int i = 0; i < (int)models.size(); i++) {
        const std::string& n = models[i].name;
        if (startsWith(n, "fir_") || startsWith(n, "pine_")) pineModels.push_back(i);
        else if (startsWith(n, "snag_")) snagModels.push_back(i);
        else if (startsWith(n, "log_")) logModels.push_back(i);
        else if (startsWith(n, "shrub_")) bushModels.push_back(i);
        else if (startsWith(n, "cliff_")) cliffModels.push_back(i);
        else if (startsWith(n, "grass_")) scatterGrass.push_back(i);
        else if (startsWith(n, "fern_")) scatterFern.push_back(i);
        else if (startsWith(n, "stump_") || startsWith(n, "branches_")) scatterLitter.push_back(i);
    }
    loadprof::add("props.upload meshes", loadprof::nowMs() - lp0);
    std::printf("props: %zu models, %zu textures from %s\n", models.size(), texCache.size(), dir.c_str());
    return true;
}

ScanCover PropRenderer::cover(int q) const {
    ScanCover c;
    if (q <= 0 || !loaded()) return c;
    c.pines = !pineModels.empty();
    c.snags = !snagModels.empty();
    c.logs = !logModels.empty();
    c.bushes = !bushModels.empty();
    // Rocks: only when every hull the physics may have used has its mesh.
    const props::HullLibrary& lib = props::sharedHullLibrary();
    c.rocks = lib.loaded();
    for (const props::RockHull& h : lib.hulls) c.rocks &= byName.count(h.name) > 0;
    return c;
}

void PropRenderer::addInstance(std::vector<Inst>& out, int model, const V3 ax[3], const V3& origin, float tint, float snow, uint32_t seed, bool tree,
                               bool shadow, float size) {
    const Model& g = models[model];
    Inst in;
    in.model = model;
    for (int r = 0; r < 3; r++) {
        const double row[3] = {r == 0 ? ax[0].x : r == 1 ? ax[0].y : ax[0].z, r == 0 ? ax[1].x : r == 1 ? ax[1].y : ax[1].z,
                               r == 0 ? ax[2].x : r == 1 ? ax[2].y : ax[2].z};
        for (int k = 0; k < 3; k++) in.m[r * 4 + k] = (float)row[k];
        in.m[r * 4 + 3] = (float)(r == 0 ? origin.x : r == 1 ? origin.y : origin.z);
    }
    in.extra[0] = tint, in.extra[1] = (float)(hashf(seed, 7) * 6.2832), in.extra[2] = snow, in.extra[3] = 0;
    // Bounding sphere of the model's box, through the transform.
    const V3 mid((g.bmin[0] + g.bmax[0]) * 0.5, (g.bmin[1] + g.bmax[1]) * 0.5, (g.bmin[2] + g.bmax[2]) * 0.5);
    const V3 half((g.bmax[0] - g.bmin[0]) * 0.5, (g.bmax[1] - g.bmin[1]) * 0.5, (g.bmax[2] - g.bmin[2]) * 0.5);
    in.c = origin + ax[0] * mid.x + ax[1] * mid.y + ax[2] * mid.z;
    in.r = (float)(len(ax[0]) * half.x + len(ax[1]) * half.y + len(ax[2]) * half.z);
    in.size = size;
    in.tree = tree && g.bb[0] && g.bb[1];
    in.shadow = shadow;
    out.push_back(in);
}

void PropRenderer::setWorld(const World& w, int q) {
    world = &w;
    statics.clear();
    cells.clear();
    scatter.clear();
    if (!loaded() || q <= 0) return;
    const ScanCover cv = cover(q);
    const V3 up(0, 1, 0);
    auto yawAxes = [](double yaw, double sx, double sy, double sz, V3 ax[3]) {
        ax[0] = V3(std::cos(yaw), 0, -std::sin(yaw)) * sx;
        ax[1] = V3(0, sy, 0);
        ax[2] = V3(std::sin(yaw), 0, std::cos(yaw)) * sz;
    };
    // Trees: the sapling scans, scaled to the tree (a touch wider or narrower each).
    for (size_t i = 0; i < w.trees.size(); i++) {
        const Tree& t = w.trees[i];
        const uint32_t h = (uint32_t)i * 2654435761u + 17;
        const bool snowy = w.surfaceAt(t.base.x, t.base.z) == SURF_SNOW;
        V3 ax[3];
        if (t.kind == 0 && cv.pines) {
            const int m = pineModels[hash32(h) % pineModels.size()];
            const double s = t.height / models[m].height, sw = s * (0.85 + 0.3 * hashf(h, 2));
            yawAxes(hashf(h, 3) * 2 * M_PI, sw, s, sw, ax);
            addInstance(statics, m, ax, t.base - V3(0, 0.05, 0), t.tint, snowy ? 1.0f : 0.0f, h, true, true, (float)t.height);
            statics.back().fades = true;
        } else if (t.kind == 2 && cv.snags) {
            const int m = snagModels[hash32(h) % snagModels.size()];
            const double s = t.height / models[m].height;
            yawAxes(hashf(h, 3) * 2 * M_PI, s, s, s, ax);
            addInstance(statics, m, ax, t.base - V3(0, 0.1, 0), t.tint, snowy ? 1.0f : 0.0f, h, false, true, (float)t.height);
            statics.back().fades = true;
        }
    }
    // Logs: the fallen trunk fitted to the capsule (its long axis is the model's x).
    if (cv.logs)
        for (size_t i = 0; i < w.logs.size(); i++) {
            const Log& g = w.logs[i];
            const int m = logModels[hash32((uint32_t)i + 99) % logModels.size()];
            const Model& md = models[m];
            const V3 d = g.b - g.a;
            const double L = len(d);
            if (L < 1e-3) continue;
            const V3 ex = d / L;
            V3 ez = cross(ex, up);
            ez = len(ez) > 1e-6 ? norm(ez) : V3(0, 0, 1);
            const V3 ey = cross(ez, ex);
            const double lenM = md.bmax[0] - md.bmin[0], diaM = std::max(0.01f, std::max(md.bmax[1] - md.bmin[1], md.bmax[2] - md.bmin[2]));
            const double sx = (L + 2 * g.r) / lenM, sr = 2 * g.r / diaM;
            const V3 ax[3] = {ex * sx, ey * sr, ez * sr};
            const V3 mid((md.bmin[0] + md.bmax[0]) * 0.5, (md.bmin[1] + md.bmax[1]) * 0.5, (md.bmin[2] + md.bmax[2]) * 0.5);
            const V3 origin = (g.a + g.b) * 0.5 - (ax[0] * mid.x + ax[1] * mid.y + ax[2] * mid.z);
            addInstance(statics, m, ax, origin, 1.0f, w.surfaceAt(origin.x, origin.z) == SURF_SNOW ? 1.0f : 0.0f, (uint32_t)i + 99, false, true,
                        (float)(L + 2 * g.r));
        }
    // Bushes: shrubs as wide as the bush.
    if (cv.bushes)
        for (size_t i = 0; i < w.bushes.size(); i++) {
            const Bush& b = w.bushes[i];
            const uint32_t h = (uint32_t)i * 40503u + 5;
            const int m = bushModels[hash32(h) % bushModels.size()];
            const double s = b.r / std::max(0.05f, models[m].radius) * 1.1;
            V3 ax[3];
            yawAxes(hashf(h, 1) * 2 * M_PI, s, s * (0.85 + 0.3 * hashf(h, 4)), s, ax);
            addInstance(statics, m, ax, b.p - V3(0, 0.05, 0), b.tint, 0.0f, h, false, true, (float)(b.r * 2));
        }
    // Scanned rocks, in the frame their hull was placed with.
    if (cv.rocks) {
        const props::HullLibrary& lib = props::sharedHullLibrary();
        for (size_t i = 0; i < w.rocks.size(); i++) {
            const Rock& k = w.rocks[i];
            if (k.scan < 0 || k.scan >= (int)lib.hulls.size()) continue;
            auto it = byName.find(lib.hulls[k.scan].name);
            if (it == byName.end()) continue;
            const bool snowy = w.surfaceAt(k.c.x, k.c.z) == SURF_SNOW;
            addInstance(statics, it->second, k.axis, k.origin, k.tint, snowy ? 1.0f : 0.0f, k.seed, false, true, (float)(k.r * 2));
        }
    }
    // Cliff faces on the steep ground (visual only: the physics has the slope), laid on it
    // facing out, a little sunk in, away from trails and water.
    if (!cliffModels.empty()) {
        // Every steep spot inside the rim (the crags beyond are scenery far off), then a
        // spread-out share of them by hash, at least 9 m apart.
        struct Spot {
            uint32_t h;
            double x, z;
        };
        std::vector<Spot> spots;
        const double step = 6.0, lim = w.halfSize() - 80;
        for (double x = -lim; x < lim; x += step)
            for (double z = -lim; z < lim; z += step) {
                const uint32_t h = hash32((uint32_t)(x * 7 + 5000) * 73856093u ^ (uint32_t)(z * 7 + 5000) * 19349663u);
                const double jx = x + (hashf(h, 1) - 0.5) * step, jz = z + (hashf(h, 2) - 0.5) * step;
                const V3 n = w.normal(jx, jz);
                if (n.y > 0.72 || n.y < 0.2) continue;  // steeper than ~44 degrees
                const RoadInfo r = w.roadAt(jx, jz);
                if (r.near || r.paint != PAINT_NONE) continue;
                if (w.waterLevel(jx, jz) > w.height(jx, jz) - 0.5) continue;
                spots.push_back({h, jx, jz});
            }
        std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.h < b.h; });
        const int cap = q >= 3 ? 900 : q == 2 ? 600 : 300;
        std::set<long long> taken;
        int placed = 0;
        for (const Spot& sp : spots) {
            if (placed >= cap) break;
            const uint32_t h = sp.h;
            const double jx = sp.x, jz = sp.z;
            const long long key = (long long)std::floor(jx / 9.0) * 100000 + (long long)std::floor(jz / 9.0);
            if (taken.count(key)) continue;
            taken.insert(key);
            const V3 n = w.normal(jx, jz);
            const double gy = w.height(jx, jz);
            const int m = cliffModels[h % cliffModels.size()];
            const Model& md = models[m];
            // Model frame (its face's look, its up) onto the slope's (normal, uphill).
            const V3 zM = md.faceNormal, xM = norm(cross(up, zM)), yM = cross(zM, xM);
            const V3 zW = n, xW = norm(cross(up, zW)), yW = cross(zW, xW);
            const double width = std::max(0.5f, md.bmax[0] - md.bmin[0]);
            const double s = (7.0 + 5.0 * hashf(h, 3)) / width;
            const double roll = (hashf(h, 4) - 0.5) * 0.6;
            const V3 xR = xW * std::cos(roll) + yW * std::sin(roll), yR = yW * std::cos(roll) - xW * std::sin(roll);
            // R maps model -> world: R = [xR yR zW] [xM yM zM]^T, then scale.
            V3 ax[3];
            const V3 e[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
            for (int a = 0; a < 3; a++) ax[a] = (xR * dot(e[a], xM) + yR * dot(e[a], yM) + zW * dot(e[a], zM)) * s;
            const V3 mid((md.bmin[0] + md.bmax[0]) * 0.5, (md.bmin[1] + md.bmax[1]) * 0.5, (md.bmin[2] + md.bmax[2]) * 0.5);
            const V3 origin = V3(jx, gy, jz) - n * 0.35 - (ax[0] * mid.x + ax[1] * mid.y + ax[2] * mid.z);
            addInstance(statics, m, ax, origin, (float)(0.85 + 0.2 * hashf(h, 5)), w.surfaceAt(jx, jz) == SURF_SNOW ? 1.0f : 0.0f, h, false, true,
                        (float)(width * s));
            placed++;
        }
    }
    // Static cells.
    cellN = (int)std::ceil(2 * w.halfSize() / kStaticCell) + 2;
    cells.assign((size_t)cellN * cellN, Cell());
    for (Cell& c : cells) c.mn = V3(1e30, 1e30, 1e30), c.mx = V3(-1e30, -1e30, -1e30);
    for (int i = 0; i < (int)statics.size(); i++) {
        const Inst& in = statics[i];
        const int cx = std::min(cellN - 1, std::max(0, (int)((in.c.x + w.halfSize()) / kStaticCell) + 1));
        const int cz = std::min(cellN - 1, std::max(0, (int)((in.c.z + w.halfSize()) / kStaticCell) + 1));
        Cell& c = cells[(size_t)cx * cellN + cz];
        c.inst.push_back(i);
        c.maxSize = std::max(c.maxSize, in.tree ? 1e30f : in.size);
        c.mn = V3(std::min(c.mn.x, in.c.x - in.r), std::min(c.mn.y, in.c.y - in.r), std::min(c.mn.z, in.c.z - in.r));
        c.mx = V3(std::max(c.mx.x, in.c.x + in.r), std::max(c.mx.y, in.c.y + in.r), std::max(c.mx.z, in.c.z + in.r));
    }
    blocks.clear();
    for (int bx = 0; bx < cellN; bx += 8)
        for (int bz = 0; bz < cellN; bz += 8) {
            Block b;
            for (int cx = bx; cx < std::min(cellN, bx + 8); cx++)
                for (int cz = bz; cz < std::min(cellN, bz + 8); cz++) {
                    const Cell& c = cells[(size_t)cx * cellN + cz];
                    if (c.inst.empty()) continue;
                    b.cells.push_back(cx * cellN + cz);
                    b.mn = V3(std::min(b.mn.x, c.mn.x), std::min(b.mn.y, c.mn.y), std::min(b.mn.z, c.mn.z));
                    b.mx = V3(std::max(b.mx.x, c.mx.x), std::max(b.mx.y, c.mx.y), std::max(b.mx.z, c.mx.z));
                    b.maxSize = std::max(b.maxSize, c.maxSize);
                }
            if (!b.cells.empty()) blocks.push_back(std::move(b));
        }
    // Trees near each 16 m cell (forest floor or open ground, for the scatter).
    treeNearN = (int)(2 * w.halfSize() / 16.0) + 1;
    treeNear.assign((size_t)treeNearN * treeNearN, 0);
    for (const Tree& t : w.trees) {
        const int i = (int)((t.base.x + w.halfSize()) / 16.0), j = (int)((t.base.z + w.halfSize()) / 16.0);
        if (i >= 0 && j >= 0 && i < treeNearN && j < treeNearN) treeNear[(size_t)i * treeNearN + j] = (uint8_t)std::min(255, treeNear[(size_t)i * treeNearN + j] + 1);
    }
    std::printf("props: %zu placed (%zu trees, %zu rocks on the map)\n", statics.size(), w.trees.size(), w.rocks.size());
}

void PropRenderer::buildScatterCell(int cx, int cz, std::vector<Inst>& out) {
    const World& w = *world;
    const int tries = kScatterTries[quality];
    const uint32_t base = hash32((uint32_t)(cx + 40000) * 73856093u ^ (uint32_t)(cz + 40000) * 19349663u);
    auto forest = [&](double x, double z) {
        const int i = (int)((x + w.halfSize()) / 16.0), j = (int)((z + w.halfSize()) / 16.0);
        if (i < 0 || j < 0 || i >= treeNearN || j >= treeNearN) return 0;
        return (int)treeNear[(size_t)i * treeNearN + j];
    };
    for (int k = 0; k < tries; k++) {
        const uint32_t h = hash32(base + (uint32_t)k * 0x9e3779b9u);
        const double x = (cx + hashf(h, 1)) * kScatterCell, z = (cz + hashf(h, 2)) * kScatterCell;
        if (std::fabs(x) > w.halfSize() - 30 || std::fabs(z) > w.halfSize() - 30) continue;
        const RoadInfo r = w.roadAt(x, z);
        if (r.near || r.paint != PAINT_NONE) continue;  // off roads, trails, pads and painted ground
        const Surface s = w.surfaceAt(x, z);
        if (s != SURF_GRASS && s != SURF_DIRT) continue;
        const double gy = w.height(x, z);
        if (w.waterLevel(x, z) > gy - 0.2 || w.waterLevel(x + 1.5, z) > gy - 0.2 || w.waterLevel(x - 1.5, z) > gy - 0.2) continue;
        const V3 n = w.normal(x, z);
        if (n.y < 0.8) continue;
        bool pad = false;
        for (const Station& st : w.stations) pad |= std::hypot(x - st.p.x, z - st.p.z) < st.r + 4;
        if (pad) continue;
        const int f = forest(x, z);
        const double pick = hashf(h, 3);
        int m = -1;
        double scale = 1, yawK = hashf(h, 4) * 2 * M_PI;
        bool shadow = true;
        if (f >= 2) {  // forest floor: ferns, litter
            if (pick < 0.55 && !scatterFern.empty()) m = scatterFern[hash32(h + 1) % scatterFern.size()], scale = 0.8 + 0.6 * hashf(h, 5);
            else if (pick < 0.68 && !scatterLitter.empty()) m = scatterLitter[hash32(h + 2) % scatterLitter.size()], scale = 0.8 + 0.4 * hashf(h, 5);
            else if (!scatterGrass.empty() && s == SURF_GRASS) m = scatterGrass[hash32(h + 3) % scatterGrass.size()], scale = 0.7 + 0.5 * hashf(h, 5), shadow = false;
        } else {  // open ground: grass clumps where it grows thick, the odd branch
            if (s == SURF_GRASS && w.grassDensity(x, z) > 0.25 && pick < 0.9 && !scatterGrass.empty())
                m = scatterGrass[hash32(h + 3) % scatterGrass.size()], scale = 0.8 + 0.6 * hashf(h, 5), shadow = false;
            else if (pick > 0.97 && !scatterLitter.empty()) m = scatterLitter[hash32(h + 2) % scatterLitter.size()], scale = 0.8 + 0.4 * hashf(h, 5);
        }
        if (m < 0) continue;
        // Upright, leaning a little with the slope.
        const V3 yA = norm(V3(0, 1, 0) * 0.6 + n * 0.4);
        V3 xA = norm(cross(yA, V3(std::sin(yawK), 0, std::cos(yawK))));
        if (len(xA) < 0.5) xA = V3(1, 0, 0);
        const V3 zA = cross(xA, yA);
        const V3 ax[3] = {xA * scale, yA * scale, zA * scale};
        addInstance(out, m, ax, V3(x, gy - 0.03, z), (float)(0.85 + 0.3 * hashf(h, 6)), 0.0f, h, false, shadow, (float)(models[m].radius * 2 * scale));
    }
}

int PropRenderer::lodFor(const Inst& in, double dist, bool shadowPass) const {
    const double k = kLodK[quality];
    if (in.tree) {  // trees by distance: the near ones carry the most cards
        if (dist < 14 * k) return 0;
        if (dist < 40 * k) return 1;
        return 2;
    }
    const Model& g = models[in.model];
    if (g.kind == props::KIND_ROCK || g.kind == props::KIND_CLIFF) {  // by how big their triangles come out on screen
        const int tris[3] = {g.lodTris[0], g.lodTris[1], g.lodTris[2]};
        // (A shadow from one under ~10 px on screen is a speck: those cast none.)
        if (shadowPass && in.size < props::kLodShadowCullSize / k * std::max(dist, 0.1)) return -1;
        return props::solidLod(tris, in.size, dist, k);
    }
    const double s = in.size / std::max(dist, 0.1);  // by size on screen
    if (s > 0.06 / k) return 0;
    if (s > 0.018 / k) return 1;
    if (s > 0.0035 / k) return 2;
    return -1;  // too small to see
}

void PropRenderer::beginFrame(const World& w, const V3& eyePos, const M4& viewProj, int q, float t) {
    const auto t0 = std::chrono::steady_clock::now();
    stats = PropStats();
    quality = std::min(std::max(q, 0), 3);
    frameNo++;
    fadeDt = std::min(0.1, std::max(0.0, (double)t - lastFadeTime));
    lastFadeTime = t;
    eye = eyePos;
    time = t;
    if (world != &w) world = &w;
    // Scatter cells round the camera: make the new ones, drop the far ones.
    if (quality > 0 && loaded() && (!scatterGrass.empty() || !scatterFern.empty() || !scatterLitter.empty())) {
        const double reach = kScatterReach[quality];
        const int c0x = (int)std::floor((eye.x - reach) / kScatterCell), c1x = (int)std::floor((eye.x + reach) / kScatterCell);
        const int c0z = (int)std::floor((eye.z - reach) / kScatterCell), c1z = (int)std::floor((eye.z + reach) / kScatterCell);
        for (auto it = scatter.begin(); it != scatter.end();) {
            const int cx = (int)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xffffffff);
            if (cx < c0x - 1 || cx > c1x + 1 || cz < c0z - 1 || cz > c1z + 1) it = scatter.erase(it);
            else ++it;
        }
        for (int cx = c0x; cx <= c1x; cx++)
            for (int cz = c0z; cz <= c1z; cz++) {
                const long long key = ((long long)cx << 32) | (uint32_t)cz;
                if (!scatter.count(key)) buildScatterCell(cx, cz, scatter[key]);
            }
    }
    (void)viewProj;  // (each pass culls for itself)
    stats.cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void PropRenderer::select(const float pl[6][4], bool useBillboards, int lodBias, bool shadowPass) {
    const auto t0 = std::chrono::steady_clock::now();
    struct Timer {
        const std::chrono::steady_clock::time_point t0;
        double& out;
        ~Timer() { out += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    } timer{t0, stats.selectMs};
    for (Bucket& b : buckets) b.data.clear();
    bbData.resize(models.size());
    for (auto& v : bbData) v.clear();
    const double bbDist = kBillboardDist[quality];
    // How much of a tree the camera hides (eased from frame to frame; an instance not seen last frame starts at its target).
    const double dt = fadeDt;
    auto hidden = [&](Inst& in) {
        if (shadowPass || !in.fades || (see.nearScale <= 0 && !see.hasFocus)) return 0.0f;
        float target = 0;
        if (std::fabs(in.c.x - eye.x) < 40 && std::fabs(in.c.z - eye.z) < 40) {
            const Model& g = models[in.model];
            const double sx = std::hypot(in.m[0], in.m[8]);
            target = treefade::target(see, V3(in.m[3], in.m[7], in.m[11]), g.height * in.m[5], g.radius * sx);
        }
        in.fadeNow = in.fadeFrame + 1 >= frameNo ? treefade::ease(in.fadeNow, target, dt) : target;
        in.fadeFrame = frameNo;
        return in.fadeNow;
    };
    auto put = [&](const Inst& in, bool scatterInst, float fade) {
        const double dist = len(in.c - eye);
        if (in.tree && useBillboards && dist > bbDist) {
            std::vector<float>& v = bbData[in.model];
            const Model& g = models[in.model];
            // Base, yaw, reach, height (from the instance's axes).
            const double sx = std::hypot(in.m[0], in.m[8]), sy = in.m[5];
            const float yaw = (float)std::atan2(-in.m[8], in.m[0]);
            const float vals[8] = {in.m[3], in.m[7], in.m[11], yaw, (float)(g.radius * sx), (float)(g.height * sy), in.extra[0], in.extra[2]};
            v.insert(v.end(), vals, vals + 8);
            return;
        }
        int lod = lodFor(in, dist, shadowPass);
        if (lod < 0) return;
        if (scatterInst && dist > kScatterReach[quality]) return;
        lod = std::min(2, lod + lodBias);
        const long long key = (long long)in.model * 4 + lod;
        auto it = bucketOf.find(key);
        int bi;
        if (it == bucketOf.end()) {
            bi = (int)buckets.size();
            bucketOf[key] = bi;
            buckets.push_back({in.model, lod, {}});
        } else
            bi = it->second;
        std::vector<float>& d = buckets[bi].data;
        d.insert(d.end(), in.m, in.m + 12);
        d.insert(d.end(), in.extra, in.extra + 3);
        d.push_back(fade);
    };
    // (Nothing in a cell or block whose biggest instance is under the cull size at its nearest is drawn: it's passed over.)
    const double cull = props::kLodCullSize / std::max(0.1, (double)kLodK[quality]);
    auto tooFar = [&](const V3& mn, const V3& mx, float maxSize) {
        const double dx = std::max({mn.x - eye.x, 0.0, eye.x - mx.x}), dy = std::max({mn.y - eye.y, 0.0, eye.y - mx.y}),
                     dz = std::max({mn.z - eye.z, 0.0, eye.z - mx.z});
        return maxSize < cull * std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const bool big = world && world->halfSize() > 1000;
    for (const Block& bl : blocks) {
      if (!boxInFrustum(pl, bl.mn, bl.mx) || tooFar(bl.mn, bl.mx, bl.maxSize)) continue;
      for (int ci : bl.cells) {
        const Cell& c = cells[ci];
        if (c.inst.empty() || !boxInFrustum(pl, c.mn, c.mx) || tooFar(c.mn, c.mx, c.maxSize)) continue;
        if (!shadowPass && occluder) {
            RenderBounds b;
            b.add(c.mn), b.add(c.mx);
            // (A big map: the horizon test only from 64 to 450 m, as for the ground: render.cpp drawTerrain.)
            const double d = big ? b.distance(eye) : 100;
            if (d > 64 && d < 450 && occluder->occluded(b)) continue;
        }
        for (int i : c.inst) {
            Inst& in = statics[i];
            if (shadowPass && !in.shadow) continue;
            if (!sphereInFrustum(pl, in.c, in.r)) continue;
            put(in, false, hidden(in));
        }
      }
    }
    if (!shadowPass || lodBias == 0)
        for (const auto& kv : scatter) {
            if (!shadowPass && occluder && !kv.second.empty()) {
                RenderBounds b;
                for (const Inst& in : kv.second) b.add(in.c - V3(in.r, in.r, in.r)), b.add(in.c + V3(in.r, in.r, in.r));
                if (occluder->occluded(b)) continue;
            }
            for (const Inst& in : kv.second) {
                if (shadowPass && !in.shadow) continue;
                if (!sphereInFrustum(pl, in.c, in.r)) continue;
                put(in, true, 0.0f);
                if (!shadowPass) stats.scatter++;
            }
        }
}

void PropRenderer::submit(GLuint prog, bool depth, long long& tris, int& draws, int& instances) {
    // One upload for every bucket, then a draw per (model, LOD, sub-mesh).
    uploadBuf.clear();
    std::vector<size_t> offset(buckets.size());
    for (size_t b = 0; b < buckets.size(); b++) {
        offset[b] = uploadBuf.size() * 4;
        uploadBuf.insert(uploadBuf.end(), buckets[b].data.begin(), buckets[b].data.end());
    }
    if (uploadBuf.empty()) return;
    BindBuffer(GL_ARRAY_BUFFER, instVbo);
    const size_t bytes = uploadBuf.size() * 4;
    if (bytes > instCap) instCap = bytes * 3 / 2;
    BufferData(GL_ARRAY_BUFFER, instCap, nullptr, GL_STREAM_DRAW);  // (orphan the old one)
    BufferSubData(GL_ARRAY_BUFFER, 0, bytes, uploadBuf.data());
    UseProgram(prog);
    Uniform1f(uniformLocation(prog, "uTime"), time);
    Uniform1i(uniformLocation(prog, "uDiff"), 5);
    Uniform1i(uniformLocation(prog, "uNorm"), 6);
    Uniform1i(uniformLocation(prog, "uArm"), 7);
    const GLint uAlpha = uniformLocation(prog, "uAlpha"), uCut = uniformLocation(prog, "uCutoff"), uFol = uniformLocation(prog, "uFoliage");
    const GLint uHasN = uniformLocation(prog, "uHasNorm"), uHasA = uniformLocation(prog, "uHasArm"), uWind = uniformLocation(prog, "uWind");
    const GLint uTop = uniformLocation(prog, "uTop");
    for (size_t b = 0; b < buckets.size(); b++) {
        const int n = (int)(buckets[b].data.size() / 16);
        if (!n) continue;
        const Model& g = models[buckets[b].model];
        Uniform1f(uTop, g.height);
        for (const Sub& s : g.lods[buckets[b].lod]) {
            const Mat& mt = g.mats[std::min<size_t>(s.mat, g.mats.size() - 1)];
            if (mt.twoSided || mt.alpha) Disable(GL_CULL_FACE);
            else Enable(GL_CULL_FACE);
            BindVertexArray(s.vao);
            BindBuffer(GL_ARRAY_BUFFER, instVbo);
            for (int a = 0; a < 4; a++) VertexAttribPointer(4 + a, 4, GL_FLOAT, 0, 64, (void*)(offset[b] + a * 16));
            ActiveTexture(GL_TEXTURE0 + 5);
            BindTexture(GL_TEXTURE_2D, mt.tex[0]);
            if (!depth) {
                ActiveTexture(GL_TEXTURE0 + 6);
                BindTexture(GL_TEXTURE_2D, mt.tex[1]);
                ActiveTexture(GL_TEXTURE0 + 7);
                BindTexture(GL_TEXTURE_2D, mt.tex[2]);
                Uniform1i(uFol, mt.foliage ? 1 : 0);
                Uniform1i(uHasN, mt.tex[1] ? 1 : 0);
                Uniform1i(uHasA, mt.tex[2] ? 1 : 0);
            }
            ActiveTexture(GL_TEXTURE0);
            Uniform1i(uAlpha, mt.alpha && mt.tex[0] ? 1 : 0);
            Uniform1f(uCut, mt.cutoff);
            Uniform1f(uWind, mt.wind);
            DrawElementsInstanced(GL_TRIANGLES, s.count, GL_UNSIGNED_SHORT, nullptr, n);
            draws++;
            tris += (long long)s.count / 3 * n;
        }
        instances += n;
    }
    Disable(GL_CULL_FACE);
    BindVertexArray(0);
}

void PropRenderer::submitBillboards(GLuint prog, bool depth) {
    size_t total = 0;
    for (const auto& v : bbData) total += v.size();
    if (!total) return;
    uploadBuf.clear();
    std::vector<size_t> offset(bbData.size());
    for (size_t m = 0; m < bbData.size(); m++) {
        offset[m] = uploadBuf.size() * 4;
        uploadBuf.insert(uploadBuf.end(), bbData[m].begin(), bbData[m].end());
    }
    BindBuffer(GL_ARRAY_BUFFER, bbVbo);
    const size_t bytes = uploadBuf.size() * 4;
    if (bytes > bbCap) bbCap = bytes * 3 / 2;
    BufferData(GL_ARRAY_BUFFER, bbCap, nullptr, GL_STREAM_DRAW);
    BufferSubData(GL_ARRAY_BUFFER, 0, bytes, uploadBuf.data());
    UseProgram(prog);
    Uniform1f(uniformLocation(prog, "uViews"), (float)kBillboardViews);
    Uniform1i(uniformLocation(prog, "uDiff"), 5);
    Uniform1i(uniformLocation(prog, "uNorm"), 6);
    BindVertexArray(bbVao);
    for (size_t m = 0; m < bbData.size(); m++) {
        const int n = (int)(bbData[m].size() / 8);
        if (!n) continue;
        for (int k = 0; k < 2; k++) VertexAttribPointer(4 + k, 4, GL_FLOAT, 0, 32, (void*)(offset[m] + k * 16));
        ActiveTexture(GL_TEXTURE0 + 5);
        BindTexture(GL_TEXTURE_2D, models[m].bb[0]);
        ActiveTexture(GL_TEXTURE0 + 6);
        BindTexture(GL_TEXTURE_2D, models[m].bb[1]);
        ActiveTexture(GL_TEXTURE0);
        DrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
        if (depth) stats.shadowDraws++;
        else stats.draws++, stats.billboards += n, stats.tris += 2LL * n;
    }
    BindVertexArray(0);
}

void PropRenderer::beginQuery(int pass) {
    if (!queries[queryFrame][pass]) return;
    BeginQuery(GL_TIME_ELAPSED, queries[queryFrame][pass]);
    queryUsed[queryFrame][pass] = true;
}
void PropRenderer::endQuery() { EndQuery(GL_TIME_ELAPSED); }

void PropRenderer::drawShadow(const M4& lightVP, int cascade, const V3& lightDir) {
    if (!loaded() || quality <= 0 || statics.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    beginQuery(cascade);
    float pl[6][4];
    props::frustumPlanes(lightVP, pl);
    // The far cascade takes a coarser LOD; the scatter only casts into the near one.
    select(pl, true, cascade, true);
    UseProgram(progDepth);
    UniformMatrix4fv(uniformLocation(progDepth, "uVP"), 1, 0, lightVP.m);
    submit(progDepth, true, stats.shadowTris, stats.shadowDraws, stats.shadowInstances);
    UseProgram(progBBDepth);
    UniformMatrix4fv(uniformLocation(progBBDepth, "uVP"), 1, 0, lightVP.m);
    Uniform1i(uniformLocation(progBBDepth, "uFaceLight"), 1);
    Uniform3f(uniformLocation(progBBDepth, "uLightDir"), (float)lightDir.x, (float)lightDir.y, (float)lightDir.z);
    submitBillboards(progBBDepth, true);
    endQuery();
    stats.cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void PropRenderer::draw(const M4& viewProj, const std::function<void(GLuint)>& setLighting) {
    if (!loaded() || quality <= 0 || statics.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    beginQuery(2);
    float pl[6][4];
    props::frustumPlanes(viewProj, pl);
    select(pl, true, 0, false);
    setLighting(progLit);
    submit(progLit, false, stats.tris, stats.draws, stats.instances);
    setLighting(progBB);
    Uniform3f(uniformLocation(progBB, "uEye"), (float)eye.x, (float)eye.y, (float)eye.z);
    Uniform1i(uniformLocation(progBB, "uFaceLight"), 0);
    submitBillboards(progBB, false);
    endQuery();
    stats.cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void PropRenderer::endFrame() {
    // The oldest frame's queries: they're done by now (or we wait for them next time round).
    const int f = (queryFrame + 1) % kQueryFrames;
    double ms = 0;
    bool any = false, ready = true;
    for (int p = 0; p < 3; p++) {
        if (!queryUsed[f][p]) continue;
        GLint avail = 0;
        GetQueryObjectiv(queries[f][p], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) {
            ready = false;
            continue;
        }
        GLuint64 ns = 0;
        GetQueryObjectui64v(queries[f][p], GL_QUERY_RESULT, &ns);
        ms += ns / 1e6;
        any = true;
        queryUsed[f][p] = false;
    }
    if (any && ready) lastGpuMs = ms;
    stats.gpuMs = lastGpuMs;
    queryFrame = (queryFrame + 1) % kQueryFrames;
}

void PropRenderer::terrainChanged(double x0, double z0, double x1, double z1) {
    for (auto it = scatter.begin(); it != scatter.end();) {
        const int cx = (int)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xffffffff);
        const bool hit = (cx + 1) * kScatterCell >= x0 && cx * kScatterCell <= x1 && (cz + 1) * kScatterCell >= z0 && cz * kScatterCell <= z1;
        it = hit ? scatter.erase(it) : std::next(it);
    }
}
