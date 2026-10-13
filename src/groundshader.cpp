// The terrain's fragment shader: photo-scanned PBR ground blended per pixel from the paint
// on the 1 m cells (see groundtex.h for the layers). Follows kCommonFS, kLitIns and
// kTerrainProcFS (render.cpp): it shares their uniforms, noise and shadows, and falls back
// to the procedural look (terrainProcedural) for paved ground, ice and the like.
#include "groundtex.h"

extern const char* const kGroundShaderFS = R"(
uniform sampler2DArray uGroundA;  // sRGB albedo, A = height
uniform sampler2DArray uGroundB;  // RG tangent normal, B roughness, A ambient occlusion
uniform int uGroundQ;             // 1 one tap a layer, 2 anti-tiling + height blend + steep rock, 3 as 2 with more layers

// ---- Materials: a layer of the arrays, a tint, a size on the ground, a relief strength ----
const int kPROC = 16;  // "not a photo": the procedural look
const int   kVLayer[17] = int[](0, 2, 1, 3, 4, 6, 7, 5, 5, 8, 8, 7, 7, 9, 4, 7, 0);
const vec4  kVTint[17] = vec4[](
  vec4(1.00, 1.00, 1.00, 0.85),  //  0 MUD         wet mud
  vec4(1.00, 1.00, 1.00, 1.00),  //  1 MUDLEAF     mud with sprouts
  vec4(0.85, 0.80, 0.75, 1.00),  //  2 DIRT        dry earth, trails
  vec4(0.66, 0.60, 0.52, 1.00),  //  3 LITTER      leaves and needles
  vec4(0.58, 0.82, 0.42, 1.00),  //  4 GRASSA      sparse grass and twigs, greened
  vec4(0.85, 1.00, 0.80, 1.00),  //  5 GRASSB      rich grass
  vec4(1.00, 0.97, 0.93, 1.00),  //  6 GRAVEL      stones
  vec4(1.00, 0.95, 0.90, 1.00),  //  7 PALE        pale dusty ground
  vec4(1.12, 1.02, 0.82, 1.00),  //  8 SAND        the pale ground, warmed
  vec4(1.00, 1.00, 1.00, 1.00),  //  9 HARDPAN     orange clay
  vec4(1.15, 0.62, 0.45, 1.00),  // 10 REDROCK     the clay, reddened
  vec4(0.66, 0.64, 0.62, 1.00),  // 11 SLICK       the stones, greyed: bare rock
  vec4(0.58, 0.56, 0.54, 1.00),  // 12 SCREE       broken stone, darker
  vec4(1.50, 1.50, 1.55, 1.00),  // 13 SNOW
  vec4(1.00, 0.88, 0.60, 1.00),  // 14 DRYGRASS    sparse grass, bleached
  vec4(0.52, 0.51, 0.50, 1.00),  // 15 CLIFF       the stones, dark grey, big
  vec4(1.0));
const float kVTile[17] = float[](2.5, 3.0, 2.5, 3.0, 3.0, 3.0, 2.5, 3.0, 3.5, 3.5, 4.0, 3.5, 2.0, 3.5, 3.0, 4.5, 3.0);
const float kVBump[17] = float[](1.0, 1.0, 1.0, 1.0, 0.8, 0.8, 1.0, 0.9, 0.5, 0.7, 0.7, 1.1, 1.2, 0.5, 0.8, 1.3, 0.0);

// ---- The material mix at this pixel: up to 12 (variant, weight) slots ----
int sV[12]; float sW[12]; int sN = 0;
float gProc = 0.0;       // weight of the procedural look
float gMud = 0.0, gSnow = 0.0, gTrail = 0.0, gRockPaint = 0.0;
void addSlot(int v, float w){
  if (w <= 0.001) return;
  if (v == kPROC) { gProc += w; return; }
  for (int i = 0; i < sN; i++) if (sV[i] == v) { sW[i] += w; return; }
  if (sN < 12) { sV[sN] = v; sW[sN] = w; sN++; }
}
// Per-pixel environment, worked out once.
float gN1, gDry, gForest, gSteep, gHigh, gSnowH, gAlt, gNoiseA, gNoiseB, gBald;
float gDens;
// Natural ground (paint 0): meadow, forest floor, bare rock on steep faces and up the range, snow on the tops.
void naturalGround(float w){
  float rockW = max(gSteep, gHigh * 0.8);
  float flatW = (1.0 - rockW) * (1.0 - gSnowH) * w;
  bool meadow = uMap == 0 || uMap == 2;
  float t = smoothstep(0.35, 0.65, gN1);
  if (meadow) {
    float bare = gBald * 0.5;
    addSlot(5, flatW * (1.0 - t) * (1.0 - gDry) * (1.0 - bare));
    addSlot(4, flatW * t * (1.0 - gDry) * (1.0 - bare));
    addSlot(14, flatW * gDry * (1.0 - bare));
    addSlot(2, flatW * bare);
  } else {
    float lit = gForest * (1.0 - gBald * 0.3);
    float gr = 1.0 - lit;
    float bare = gBald * 0.55 * (1.0 - lit * 0.5);
    addSlot(3, flatW * lit);
    addSlot(5, flatW * gr * (1.0 - t) * (1.0 - gDry) * (1.0 - bare));
    addSlot(4, flatW * gr * t * (1.0 - gDry) * (1.0 - bare));
    addSlot(14, flatW * gr * gDry * (1.0 - bare));
    addSlot(2, flatW * (gr * bare + lit * gBald * 0.3));
  }
  float rw = rockW * (1.0 - gSnowH) * w;
  float cliff = smoothstep(0.30, 0.55, gNoiseA);
  addSlot(15, rw * cliff);
  addSlot(11, rw * (1.0 - cliff) * 0.7);
  addSlot(12, rw * (1.0 - cliff) * 0.3);
  addSlot(13, gSnowH * w);
}
// What a paint is made of, `w` of the pixel's weight.
void expandPaint(int paint, float w){
  float nz = gNoiseA, nz2 = gNoiseB;
  if (paint == 0) { naturalGround(w); }
  else if (paint == 1) {
    if (uMap == 0 || uMap == 2) {  // asphalt road: the procedural look out to its gravel shoulder, the land beyond
      float e = max(fwidth(abs(vCol.g)), 0.02) * 1.5;
      float road = (1.0 - smoothstep(6.6 - e, 6.6 + e, abs(vCol.g))) * step(0.5, vCol.r);
      addSlot(kPROC, w * road);
      naturalGround(w * (1.0 - road));
    } else {                          // gravel road
      gTrail += w;
      addSlot(6, w * (0.55 + 0.35 * nz));
      addSlot(7, w * (0.45 - 0.35 * nz));
    }
  }
  else if (paint == 2) { gTrail += w; addSlot(2, w * (0.7 - 0.25 * nz)); addSlot(6, w * (0.3 + 0.25 * nz)); }
  else if (paint == 3) { gRockPaint += w; addSlot(11, w * (0.55 + 0.25 * nz)); addSlot(15, w * (0.25 - 0.1 * nz)); addSlot(12, w * (0.2 - 0.15 * nz)); }
  else if (paint == 4) { addSlot(6, w * (0.55 + 0.3 * nz)); addSlot(7, w * (0.45 - 0.3 * nz)); }
  else if (paint == 6) { addSlot(12, w * (0.5 + 0.3 * nz)); addSlot(6, w * (0.5 - 0.3 * nz)); }
  else if (paint == 7) { addSlot(8, w * (0.7 + 0.2 * nz)); addSlot(7, w * (0.3 - 0.2 * nz)); }
  else if (paint == 8) { gMud += w; addSlot(0, w * (0.65 + 0.3 * nz2)); addSlot(1, w * (0.35 - 0.3 * nz2)); }
  else if (paint == 9) { gSnow += w; addSlot(13, w); }
  else if (paint == 14) { addSlot(9, w * (0.7 + 0.2 * nz)); addSlot(7, w * (0.3 - 0.2 * nz)); }
  else if (paint == 15) { addSlot(10, w * (0.8 + 0.1 * nz)); addSlot(9, w * (0.2 - 0.1 * nz)); }
  else addSlot(kPROC, w);  // pad, ice, wet asphalt, freeway, race circuit
}

// ---- Sampling ----
// One tap, or two of different size and turn blended by a noise and put back to the layer's contrast.
vec4 planarTap(sampler2DArray s, float L, vec2 p, float scale, vec2 gx, vec2 gy, float m, vec4 avg, bool two){
  vec4 a = textureGrad(s, vec3(p * scale, L), gx * scale, gy * scale);
  if (!two) return a;
  const mat2 R = mat2(0.8, 0.6, -0.6, 0.8);
  float s2 = scale * 0.73;
  vec4 b = textureGrad(s, vec3(R * p * s2 + vec2(0.37, 0.71), L), R * gx * s2, R * gy * s2);
  float k = inversesqrt((1.0 - m) * (1.0 - m) + m * m);
  return avg + (mix(a, b, m) - avg) * k;
}
// World direction of a tangent-space normal t (OpenGL: +y toward the top of the image) in a frame of
// right R, up U, out N, whiteout-blended onto the surface normal g.
vec3 reorient(vec3 t, vec3 R, vec3 U, vec3 N, vec3 g){
  return (t.x + dot(g, R)) * R + (t.y + dot(g, U)) * U + (t.z * dot(g, N)) * N;
}
vec3 tangentNormal(vec4 b, float strength){
  vec2 xy = (b.xy * 2.0 - 1.0) * strength;
  return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.05)));
}
// The ground seen from above is mapped by x, z (right = +x, image up = -z); a steep face also from the
// two sides (triplanar): seen along x and along z, by (horizontal axis, y). The image rows run down
// with v, so the "up" of a texture is the negative of v.
struct Side { vec3 R, N; vec2 uv, gx, gy; float w; };
Side sideX, sideZ;
void sideFrames(vec3 g, vec3 dPx, vec3 dPy){
  float sx = g.x < 0.0 ? -1.0 : 1.0, sz = g.z < 0.0 ? -1.0 : 1.0;
  sideX.R = vec3(0.0, 0.0, -sx); sideX.N = vec3(sx, 0.0, 0.0);
  sideZ.R = vec3(sz, 0.0, 0.0);  sideZ.N = vec3(0.0, 0.0, sz);
  float wx = pow(abs(g.x), 4.0), wz = pow(abs(g.z), 4.0);
  sideX.w = wx / max(wx + wz, 1e-5); sideZ.w = 1.0 - sideX.w;
  sideX.uv = vec2(dot(vPos, sideX.R), -vPos.y); sideX.gx = vec2(dot(dPx, sideX.R), -dPx.y); sideX.gy = vec2(dot(dPy, sideX.R), -dPy.y);
  sideZ.uv = vec2(dot(vPos, sideZ.R), -vPos.y); sideZ.gx = vec2(dot(dPx, sideZ.R), -dPx.y); sideZ.gy = vec2(dot(dPy, sideZ.R), -dPy.y);
}
vec4 sideTap(sampler2DArray s, float L, Side f, float scale){
  return textureGrad(s, vec3(f.uv * scale, L), f.gx * scale, f.gy * scale);
}

void main(){
  vec3 N0 = normalize(vNrm);
  vec2 p = vPos.xz;
  vec3 dPx = dFdx(vPos), dPy = dFdy(vPos);
  float dist = length(vPos - uCam);

  // ---- Environment noise (shared by every paint) ----
  gN1 = fbm(p * 0.02, 4);
  gNoiseA = fbm(p * 0.35 + 5.0, 3);
  gNoiseB = fbm(p * 0.55 - 3.0, 2);
  gDry = smoothstep(0.6, 0.8, fbm(p * 0.006 + 3.1, 3));
  gBald = smoothstep(0.60, 0.80, fbm(p * 0.30 + 9.0, 3));
  float n2 = vnoise(p * 0.9);
  gSteep = (uMap == 0 || uMap == 2) ? smoothstep(0.84, 0.62, N0.y + 0.14 * (gN1 - 0.5)) : smoothstep(0.72, 0.55, N0.y + 0.14 * (gN1 - 0.5));
  gHigh = smoothstep(uRockSnow.x, uRockSnow.x + 90.0, vPos.y + 30.0 * gN1);
  gSnowH = smoothstep(uRockSnow.y, uRockSnow.y + 50.0, vPos.y + 70.0 * gN1) * smoothstep(0.5, 0.75, N0.y);
  gForest = smoothstep(0.40, 0.65, fbm(p * 0.012 + vec2(7.0, -3.0), 3)) * smoothstep(6.0, 22.0, vPos.y);

  // ---- The four cells round this point, from a slightly wobbled spot so edges are ragged, not 1 m stairs ----
  vec2 wob = (vec2(vnoise(p * 0.8), vnoise(p.yx * 0.8 + 7.0)) - 0.5) * 1.4;
  vec2 gp = p + wob + 600.0;
  ivec2 i0 = clamp(ivec2(floor(gp)), ivec2(0), ivec2(int(uTerrainN) - 2));
  vec2 f = clamp(gp - vec2(i0), 0.0, 1.0);
  f = f * f * (3.0 - 2.0 * f);
  vec4 c00 = texelFetch(uTerrainTex, i0, 0), c10 = texelFetch(uTerrainTex, i0 + ivec2(1, 0), 0);
  vec4 c01 = texelFetch(uTerrainTex, i0 + ivec2(0, 1), 0), c11 = texelFetch(uTerrainTex, i0 + ivec2(1, 1), 0);
  gDens = mix(mix(c00.g, c10.g, f.x), mix(c01.g, c11.g, f.x), f.y);
  int cp[4]; float cw[4]; int cn = 0;
  int pcs[4] = int[](int(c00.b + 0.5), int(c10.b + 0.5), int(c01.b + 0.5), int(c11.b + 0.5));
  float wts[4] = float[]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
  for (int c = 0; c < 4; c++) {
    bool found = false;
    for (int k = 0; k < cn; k++) if (cp[k] == pcs[c]) { cw[k] += wts[c]; found = true; }
    if (!found) { cp[cn] = pcs[c]; cw[cn] = wts[c]; cn++; }
  }
  for (int k = 0; k < cn; k++) expandPaint(cp[k], cw[k]);

  // ---- The procedural part (paved ground and the like) ----
  vec3 procBase = vec3(0.0); float procK = 0.04, procP = 16.0;
  if (gProc > 0.001) { procBase = vCol.rgb; terrainProcedural(N0, procBase, procK, procP); }

  // ---- Pick the strongest materials ----
  int K = uGroundQ >= 3 ? 4 : uGroundQ == 2 ? 3 : 2;
  if (dist > 120.0) K = min(K, 2);
  if (dist > 350.0) K = 1;
  int selV[4] = int[](0, 0, 0, 0); float selW[4] = float[](0.0, 0.0, 0.0, 0.0);
  float texTotal = 0.0;
  for (int k = 0; k < 4; k++) {
    if (k >= K) break;
    int bi = -1; float bw = 0.0;
    for (int i = 0; i < sN; i++) if (sW[i] > bw) { bw = sW[i]; bi = i; }
    if (bi < 0) break;
    selV[k] = sV[bi]; selW[k] = bw; sW[bi] = -1.0; texTotal += bw;
  }
  float texW = clamp(1.0 - gProc, 0.0, 1.0);
  if (texTotal > 0.0) for (int k = 0; k < 4; k++) selW[k] *= texW / texTotal;

  // ---- Photo layers ----
  bool two = uGroundQ >= 2 && dist < 250.0;
  float mix2 = 0.15 + 0.7 * smoothstep(0.25, 0.75, vnoise(p * 0.11 + 4.0));
  float wTop = smoothstep(0.55, 0.93, N0.y);  // how much the top-down mapping counts on a slope
  bool steepFace = uGroundQ >= 2 && wTop < 0.999 && dist < 400.0;
  sideFrames(N0, dPx, dPy);
  vec4 A[4];
  float Lr[4]; float scl[4]; vec4 avgA[4];
  for (int k = 0; k < 4; k++) {
    A[k] = vec4(0.0);
    if (k >= K || selW[k] <= 0.0) continue;
    int v = selV[k];
    Lr[k] = float(kVLayer[v]); scl[k] = 1.0 / kVTile[v];
    avgA[k] = textureLod(uGroundA, vec3(0.5, 0.5, Lr[k]), 12.0);
    vec4 a = planarTap(uGroundA, Lr[k], p, scl[k], dPx.xz, dPy.xz, mix2, avgA[k], two);
    if (steepFace) {
      vec4 sd = vec4(0.0);
      if (sideX.w > 0.02) sd += sideTap(uGroundA, Lr[k], sideX, scl[k]) * sideX.w;
      if (sideZ.w > 0.02) sd += sideTap(uGroundA, Lr[k], sideZ, scl[k]) * sideZ.w;
      a = mix(sd / max(sideX.w * step(0.02, sideX.w) + sideZ.w * step(0.02, sideZ.w), 1e-3), a, wTop);
    }
    // Far off, each layer eases toward its own average colour: no tiling to see from across the valley.
    a.rgb = mix(a.rgb, avgA[k].rgb, smoothstep(180.0, 700.0, dist) * 0.6);
    A[k] = a;
    A[k].a = clamp(A[k].a, 0.0, 1.0);
  }
  // Height blend: the knobbly material shows through first at a border.
  float bwt[4] = float[](selW[0], selW[1], selW[2], selW[3]);
  if (uGroundQ >= 2 && K > 1) {
    float ma = 0.0;
    for (int k = 0; k < 4; k++) if (selW[k] > 0.0) ma = max(ma, A[k].a + selW[k]);
    ma -= 0.35;
    float tot = 0.0;
    for (int k = 0; k < 4; k++) { bwt[k] = selW[k] > 0.0 ? max(A[k].a + selW[k] - ma, 0.0) : 0.0; tot += bwt[k]; }
    if (tot > 0.0) for (int k = 0; k < 4; k++) bwt[k] *= texW / tot;
  }
  vec3 alb = vec3(0.0), nrm = vec3(0.0); float rough = 0.0, ao = 0.0, wsum = 0.0, bump = 0.0;
  float distFade = 1.0 - smoothstep(60.0, 260.0, dist);
  for (int k = 0; k < 4; k++) {
    if (k >= K || bwt[k] < 0.01) continue;
    int v = selV[k];
    float w = bwt[k];
    vec4 b = planarTap(uGroundB, Lr[k], p, scl[k], dPx.xz, dPy.xz, mix2, textureLod(uGroundB, vec3(0.5, 0.5, Lr[k]), 12.0), two);
    float st = kVBump[v] * distFade;
    vec3 nY = reorient(tangentNormal(b, st), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0), vec3(0.0, 1.0, 0.0), N0);
    if (steepFace) {
      vec3 nS = vec3(0.0); vec4 bs = vec4(0.0); float ws = 0.0;
      const vec3 U = vec3(0.0, 1.0, 0.0);
      if (sideX.w > 0.02) { vec4 t = sideTap(uGroundB, Lr[k], sideX, scl[k]); nS += reorient(tangentNormal(t, st), sideX.R, U, sideX.N, N0) * sideX.w; bs += t * sideX.w; ws += sideX.w; }
      if (sideZ.w > 0.02) { vec4 t = sideTap(uGroundB, Lr[k], sideZ, scl[k]); nS += reorient(tangentNormal(t, st), sideZ.R, U, sideZ.N, N0) * sideZ.w; bs += t * sideZ.w; ws += sideZ.w; }
      nY = mix(normalize(nS), nY, wTop);
      b = mix(bs / max(ws, 1e-3), b, wTop);
    }
    vec3 tint = kVTint[v].rgb;
    alb += A[k].rgb * tint * w;
    nrm += nY * w;
    rough += clamp(b.b * kVTint[v].a, 0.04, 1.0) * w;
    ao += b.a * w;
    wsum += w;
  }
  if (wsum > 0.0) { alb /= wsum; rough /= wsum; ao /= wsum; nrm = normalize(nrm); }
  else { alb = vec3(0.1); rough = 0.9; ao = 1.0; nrm = N0; }
  ao = mix(1.0, ao, distFade * 0.9 + 0.1);

  // ---- Large-scale variation: patches of lighter and darker, warmer and cooler ground ----
  float bright = 0.80 + 0.40 * fbm(p * 0.045 + 11.0, 3);
  vec3 warm = mix(vec3(1.05, 1.0, 0.92), vec3(0.94, 0.99, 1.04), vnoise(p * 0.02 - 6.0));
  alb *= bright * warm;
  // ---- Wheel tracks worn into trails and roads, and packed down in snow ----
  float sl = vCol.g;
  float trk = abs(abs(sl) - 0.8);
  float ruts = (1.0 - smoothstep(0.15, 0.4, trk)) * step(0.5, vCol.r);
  alb *= 1.0 - 0.30 * ruts * min(gTrail, 1.0);
  rough = mix(rough, rough * 0.8, ruts * min(gTrail, 1.0));
  float sruts = (1.0 - smoothstep(0.2, 0.5, abs(abs(vCol.g) - 0.85))) * step(0.5, vCol.b) * (0.75 + 0.25 * vnoise(p * 3.0));
  alb *= mix(vec3(1.0), vec3(0.70, 0.74, 0.80), sruts * 0.85 * min(gSnow, 1.0));
  // ---- Mud: puddles in the hollows (dark, glassy, flat) ----
  float pud = smoothstep(0.55, 0.7, fbm(p * 0.25, 3)) * min(gMud, 1.0);
  alb *= 1.0 - 0.45 * pud;
  rough = mix(rough, 0.10, pud);
  nrm = normalize(mix(nrm, N0, pud * 0.85));
  // ---- Wet ground at the water's edge, and a dark silty bed under it ----
  float wl = texture(uTerrainTex, (p + 600.5) / uTerrainN).a;
  float wet = 1.0 - smoothstep(0.0, 0.5, vPos.y - wl);
  alb *= mix(1.0, 0.40, wet);
  rough = mix(rough, 0.25, wet * 0.7);
  alb = mix(alb, vec3(0.010, 0.012, 0.005), smoothstep(0.0, -0.4, vPos.y - wl) * 0.8);

  // ---- Light it ----
  vec3 lin = mix(alb, pow(max(procBase, vec3(0.0)), vec3(2.2)), clamp(gProc, 0.0, 1.0));
  vec3 n = normalize(mix(nrm, N0, clamp(gProc, 0.0, 1.0)));
  float sh = shadowAt(vPos, N0);
  float NL = max(dot(n, uSun), 0.0);
  vec3 V = normalize(uCam - vPos);
  vec3 H = normalize(V + uSun);
  float aoD = mix(1.0, ao, 0.55);
  float P = clamp(2.0 / (rough * rough) - 2.0, 2.0, 700.0);
  float NH = max(dot(n, H), 0.0);
  float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  vec3 sunSpec = vec3(pow(NH, P) * (P + 2.0) * 0.0398 * F * NL * sh) * kSunSpec * 2.6;
  vec3 lspec, lamp = lampLight(vPos, n, V, max(P, 12.0), 0.0, lspec);
  vec3 col = lin * (ambientAt(n) * ao + NL * aoD * sh * kSunCol + lamp);
  col += sunSpec * (1.0 - clamp(gProc, 0.0, 1.0));
  col += lspec * 0.08 * (1.0 - rough) * (1.0 - clamp(gProc, 0.0, 1.0)) * 4.0;
  // Glossy ground (wet mud, puddles, the shore) mirrors the sky.
  float gloss = (1.0 - rough);
  if (gloss > 0.35 && gProc < 0.5) {
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    col += skyEnv(reflect(-V, n)) * fres * gloss * gloss * ao * 0.9;
  }
  if (gProc > 0.001) col += pow(max(dot(n, H), 0.0), procP) * procK * sh * 2.0 * kSunSpec * clamp(gProc, 0.0, 1.0);
  oCol = finishColor(col, vPos, 1.0);
}
)";
