#include "farforest.h"

#include <algorithm>
#include <cmath>

#include "occlusion.h"
#include "visibility.h"

using namespace gl;

// A try: cell (uTileCell + its place in the tile), slot. Its numbers from a hash of those (and the level), so it's the same try
// wherever the window is. Kept when a number is under the trees the field says stand in the cell, thinned with distance (to a
// quarter at 1.2 km as the window's trees, then as the distance, the picture as much wider and a little taller: a far wood reads
// as a wood, not as the odd tree).
const char* const kFarForestVS = R"(
uniform sampler2D uFFCover; uniform sampler2D uFFHeight;
uniform vec4 uFFGrid;       // the grid's first sample (x, z), its spacing, samples a side
uniform vec2 uTileLocal;    // the tile's corner (window m)
uniform ivec2 uTileCell;    // its first cell (global, in cells)
uniform float uCell;        // a cell's side (m)
uniform int uTileN, uSlots, uLevel;
uniform vec4 uLevelFade;    // this level from (x, over the band y before it) to (z, band w before it)
uniform vec4 uKindLayer[3], uKindShare[3];  // per kind: up to 4 layers, and their shares summed
uniform vec4 uLayerRH[32];  // per layer: the quad's half-width and height (model units)
uniform float uPerHaUnit;
uvec3 pcg3(uvec3 v){
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  return v;
}
float u01(uint h){ return float(h >> 8) / 16777216.0; }
void none(){ vFade = vec2(1.0, 0.0); gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }
void main(){
  int id = gl_InstanceID;
  int slot = id % uSlots, c = id / uSlots;
  ivec2 cl = ivec2(c % uTileN, c / uTileN);
  ivec2 gc = uTileCell + cl;
  uvec3 a = pcg3(uvec3(uint(gc.x), uint(gc.y), uint(slot + 16 * uLevel)));
  uvec3 b = pcg3(a ^ uvec3(0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u));
  uvec3 e = pcg3(b ^ uvec3(0x27D4EB2Fu, 0x165667B1u, 0xD3A2646Cu));
  vec2 p = uTileLocal + (vec2(cl) + vec2(u01(a.x), u01(a.y))) * uCell;
  float d = length(p - uEye.xz);
  float lo = clamp((uLevelFade.x - d) / uLevelFade.y, 0.0, 1.0), hi = clamp((uLevelFade.z - d) / uLevelFade.w, 0.0, 1.0);
  if (hi <= lo) { none(); return; }
  vec2 uv = ((p - uFFGrid.xy) / uFFGrid.z + 0.5) / uFFGrid.w;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { none(); return; }
  vec4 cov = texture(uFFCover, uv);
  float rho = cov.r * 255.0 * uPerHaUnit / 10000.0;
  float keep = d <= 1200.0 ? 1.0 - 0.75 * smoothstep(400.0, 1200.0, d) : 0.25 * 1200.0 / d;
  if (u01(a.z) >= rho * keep * uCell * uCell / float(uSlots)) { none(); return; }
  float q = u01(b.x);
  int kind = q < cov.b ? 2 : q < cov.b + cov.g ? 0 : 1;
  if (uKindShare[kind].w <= 0.0) kind = uKindShare[0].w > 0.0 ? 0 : uKindShare[1].w > 0.0 ? 1 : 2;
  if (uKindShare[kind].w <= 0.0) { none(); return; }
  float r = u01(b.y) * uKindShare[kind].w;
  vec4 S = uKindShare[kind], L = uKindLayer[kind];
  float layer = r < S.x ? L.x : r < S.y ? L.y : r < S.z ? L.z : L.w;
  float h01 = u01(b.z);
  float height = (kind == 0 ? 9.0 + 10.0 * h01 : kind == 1 ? 7.0 + 7.0 * h01 : 5.0 + 6.0 * h01) * cov.a;
  float scale = height / (kind == 2 ? 8.0 : 10.0);
  float g = max(1.0, d / 1200.0);
  vec4 RH = uLayerRH[int(layer + 0.5)];
  float y = texture(uFFHeight, uv).r - 1.0;  // (sunk a little: the backdrop between its samples, and the tree's own slope)
  impostor(vec3(p.x, y, p.y), u01(e.x) * 6.2831853, RH.x * scale * g, RH.y * scale * sqrt(g), layer,
           vec2(0.85 + 0.15 * (0.8 + 0.4 * u01(e.y)), (u01(e.z) - 0.5) * 1.2), vec2(lo, hi));
})";

void FarForestGrid::finish() {
    bn = (n + kBlock - 1) / kBlock;
    blockLo.assign((size_t)bn * bn, 1e9f), blockHi.assign((size_t)bn * bn, -1e9f);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            const size_t b = (size_t)(i / kBlock) * bn + j / kBlock;
            const float h = height[(size_t)i * n + j];
            blockLo[b] = std::min(blockLo[b], h), blockHi[b] = std::max(blockHi[b], h);
        }
}

void FarForest::upload(const std::shared_ptr<const FarForestGrid>& g) {
    grid_ = g;
    n_ = 0;
    if (!g || g->n <= 0) return;
    if (!tex_[0]) {
        GenTextures(2, tex_);
        for (int t = 0; t < 2; t++) {
            BindTexture(GL_TEXTURE_2D, tex_[t]);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        GenVertexArrays(1, &vao_);
    }
    // (Row i of the grid is x: stored as the texture's rows by j, so (u, v) = (x, z).)
    std::vector<uint8_t> c((size_t)g->n * g->n * 4);
    std::vector<float> h((size_t)g->n * g->n);
    for (int i = 0; i < g->n; i++)
        for (int j = 0; j < g->n; j++) {
            const size_t s = (size_t)i * g->n + j, t = (size_t)j * g->n + i;
            for (int k = 0; k < 4; k++) c[t * 4 + k] = g->cover[s * 4 + k];
            h[t] = g->height[s];
        }
    PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    BindTexture(GL_TEXTURE_2D, tex_[0]);
    TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g->n, g->n, 0, GL_RGBA, GL_UNSIGNED_BYTE, c.data());
    BindTexture(GL_TEXTURE_2D, tex_[1]);
    TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, g->n, g->n, 0, GL_RED, GL_FLOAT, h.data());
    BindTexture(GL_TEXTURE_2D, 0);
    PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    n_ = g->n;
}

void FarForest::bindGround(GLuint prog, int unit, double from, double to) const {
    ActiveTexture(GL_TEXTURE0 + unit);
    BindTexture(GL_TEXTURE_2D, tex_[0]);
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(uniformLocation(prog, "uFFCover"), unit);
    if (!ready()) {
        Uniform2f(uniformLocation(prog, "uFFFade"), 0, 0);
        return;
    }
    Uniform4f(uniformLocation(prog, "uFFGrid"), (float)grid_->origin, (float)grid_->origin, (float)grid_->spacing, (float)grid_->n);
    Uniform2f(uniformLocation(prog, "uFFFade"), (float)from, (float)to);
}

unsigned FarForest::draw(const forest::View& view, const Range& r, GLuint prog, const TreeDraw& trees, const TerrainOcclusion* occl,
                         VegStats::Pass* stats) {
    tilesDrawn = 0;
    const ImpostorAtlas* atlas = trees.atlas();
    if (!ready() || !prog || !atlas || !atlas->ready()) return 0;
    const FarForestGrid& G = *grid_;
    // The looks the window's trees have, by kind, for the far ones'.
    float kl[12] = {}, ks[12] = {};
    bool any = false;
    for (int k = 0; k < 3; k++) {
        const auto& L = trees.kindLayers(k).layers;
        float sum = 0;
        for (int i = 0; i < 4; i++) {
            if (i < (int)L.size()) sum += L[i].second, kl[k * 4 + i] = (float)L[i].first;
            else kl[k * 4 + i] = i ? kl[k * 4 + i - 1] : 0;
            ks[k * 4 + i] = sum;
        }
        any |= sum > 0;
    }
    if (!any) return 0;
    float rh[ImpostorAtlas::kMaxLayers * 4] = {};
    for (size_t l = 0; l < atlas->layers.size() && l < (size_t)ImpostorAtlas::kMaxLayers; l++) rh[l * 4] = atlas->layers[l].R, rh[l * 4 + 1] = atlas->layers[l].H;
    Uniform4fv(uniformLocation(prog, "uKindLayer"), 3, kl);
    Uniform4fv(uniformLocation(prog, "uKindShare"), 3, ks);
    Uniform4fv(uniformLocation(prog, "uLayerRH"), ImpostorAtlas::kMaxLayers, rh);
    Uniform4f(uniformLocation(prog, "uFFGrid"), (float)G.origin, (float)G.origin, (float)G.spacing, (float)G.n);
    Uniform1f(uniformLocation(prog, "uPerHaUnit"), (float)FarForestGrid::kPerHaUnit);
    Uniform3f(uniformLocation(prog, "uEye"), (float)view.eye.x, (float)view.eye.y, (float)view.eye.z);
    ActiveTexture(GL_TEXTURE0 + 12);
    BindTexture(GL_TEXTURE_2D, tex_[0]);
    ActiveTexture(GL_TEXTURE0 + 15);
    BindTexture(GL_TEXTURE_2D, tex_[1]);
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(uniformLocation(prog, "uFFCover"), 12);
    Uniform1i(uniformLocation(prog, "uFFHeight"), 15);
    Uniform1i(uniformLocation(prog, "uImpCol"), 13);
    Uniform1i(uniformLocation(prog, "uImpNrm"), 14);
    atlas->bind(13, 14);
    BindVertexArray(vao_);
    constexpr int kTileN = 32;
    const int kCell[3] = {16, 32, 32}, kSlots[3] = {3, 6, 6};  // (at most about one try in a cell kept, in the thickest wood)
    Uniform1i(uniformLocation(prog, "uTileN"), kTileN);
    const GLint locTileLocal = uniformLocation(prog, "uTileLocal"), locTileCell = uniformLocation(prog, "uTileCell");
    // Levels: from where the window's trees hand over, twice that and four times (a level's tries a fixed lattice of the world).
    const double E[4] = {r.from, 2 * r.from, 4 * r.from, r.to};
    const double B[4] = {r.fromBand, 0.15 * E[1], 0.15 * E[2], 0.1 * r.to};
    const double gx0 = G.origin, gx1 = G.origin + (G.n - 1) * G.spacing;
    const double eyeGx = view.eye.x + (double)G.ox, eyeGz = view.eye.z + (double)G.oz;
    unsigned verts = 0;
    for (int lv = 0; lv < 3; lv++) {
        const double from = E[lv], to = lv == 2 ? E[3] : std::min(E[lv + 1], E[3]);
        if (from - B[lv] >= E[3]) break;
        const double bandTo = lv == 2 ? B[3] : (E[lv + 1] >= E[3] ? B[3] : B[lv + 1]);
        Uniform4f(uniformLocation(prog, "uLevelFade"), (float)from, (float)B[lv], (float)to, (float)bandTo);
        Uniform1i(uniformLocation(prog, "uLevel"), lv);
        const int cell = kCell[lv];
        Uniform1f(uniformLocation(prog, "uCell"), (float)cell);
        Uniform1i(uniformLocation(prog, "uSlots"), kSlots[lv]);
        const int64_t T = (int64_t)cell * kTileN;
        const int64_t tx0 = (int64_t)std::floor((eyeGx - to) / T), tx1 = (int64_t)std::floor((eyeGx + to) / T);
        const int64_t tz0 = (int64_t)std::floor((eyeGz - to) / T), tz1 = (int64_t)std::floor((eyeGz + to) / T);
        for (int64_t tx = tx0; tx <= tx1; tx++)
            for (int64_t tz = tz0; tz <= tz1; tz++) {
                const double lx0 = (double)(tx * T - G.ox), lz0 = (double)(tz * T - G.oz), lx1 = lx0 + (double)T, lz1 = lz0 + (double)T;
                if (lx1 < gx0 || lz1 < gx0 || lx0 > gx1 || lz0 > gx1) continue;
                const double nx = std::clamp(view.eye.x, lx0, lx1) - view.eye.x, nz = std::clamp(view.eye.z, lz0, lz1) - view.eye.z;
                if (std::hypot(nx, nz) >= to) continue;
                const double fx = std::max(std::fabs(lx0 - view.eye.x), std::fabs(lx1 - view.eye.x)), fz = std::max(std::fabs(lz0 - view.eye.z), std::fabs(lz1 - view.eye.z));
                if (std::hypot(fx, fz) < from - B[lv]) continue;
                // (its box: the ground's range under it, and the tallest picture)
                const int bi0 = std::clamp((int)((lx0 - G.origin) / G.spacing) / FarForestGrid::kBlock, 0, G.bn - 1);
                const int bi1 = std::clamp((int)((lx1 - G.origin) / G.spacing) / FarForestGrid::kBlock, 0, G.bn - 1);
                const int bj0 = std::clamp((int)((lz0 - G.origin) / G.spacing) / FarForestGrid::kBlock, 0, G.bn - 1);
                const int bj1 = std::clamp((int)((lz1 - G.origin) / G.spacing) / FarForestGrid::kBlock, 0, G.bn - 1);
                float lo = 1e9f, hi = -1e9f;
                for (int bi = bi0; bi <= bi1; bi++)
                    for (int bj = bj0; bj <= bj1; bj++) lo = std::min(lo, G.blockLo[(size_t)bi * G.bn + bj]), hi = std::max(hi, G.blockHi[(size_t)bi * G.bn + bj]);
                RenderBounds box;
                const double grow = 30 * std::max(1.0, to / 1200.0);
                box.lo = V3(lx0 - grow, lo - 2, lz0 - grow), box.hi = V3(lx1 + grow, hi + 60 * std::sqrt(std::max(1.0, to / 1200.0)), lz1 + grow);
                if (!box.visible(view.vp)) continue;
                if (occl && occl->occluded(box)) continue;
                Uniform2f(locTileLocal, (float)lx0, (float)lz0);
                Uniform2i(locTileCell, (int)(tx * kTileN), (int)(tz * kTileN));
                DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, kTileN * kTileN * kSlots[lv]);
                verts += 4u * kTileN * kTileN * kSlots[lv];
                tilesDrawn++;
                if (stats) stats->add(1, 0, kTileN * kTileN * kSlots[lv]);  // (tries, not trees: most are dropped in the vertex stage)
            }
    }
    BindVertexArray(0);
    return verts;
}
