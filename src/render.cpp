#include <future>
#include <chrono>
#include "render.h"
#include "loadprof.h"
#include "watermesh.h"
#include "texcomp.h"
#include "perf.h"

// Render targets and data textures in the VRAM report (texcomp.h); no mips on purpose.
static void noteRT(const char* name, const char* fmt, int w, int h, int bytesPerTexel, int mips = 0, bool runtime = true) {
    texcomp::note(name, fmt, w, h, mips, (size_t)w * h * bytesPerTexel * (mips > 1 ? 4 : 3) / 3, runtime);
}
#include <stdexcept>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include "carvisual.h"
#include "meshopt.h"
#include "scenery.h"
#include "treefade.h"
#include "font.h"
#include "groundtex.h"
#include "fx.h"
#include "platform.h"
#include <cstring>

using namespace gl;

namespace {

// (The sun and sky come from the clock: see daylight.h and drawScene.)
const float kFog[3] = {0.66f, 0.74f, 0.84f};

// Shared fragment-shader helpers: noise, shadows, sky, fog. Everything renders into a
// linear HDR target; the composite pass tone maps it (see kCompositeFS).
const char* kCommonFS = R"(
uniform vec3 uSun; uniform vec3 uCam; uniform vec3 uFog;
uniform sampler2DShadow uShadow0, uShadow1;  // near and far cascade
uniform mat4 uLightVP0, uLightVP1;
uniform vec4 uShadowCfg;                     // on, PCF taps, -, -
uniform float uTime, uClouds;
uniform float uFogFar;  // (a short draw distance: the fog closes in to hide where the world stops; 0 off)
// Time of day (daylight.h). uSun is the key light that shades and casts the shadows: the
// sun by day, the moon by night; uSunDir is where the sun really is.
uniform vec3 uKeyCol, uAmbSky, uAmbGround;
uniform vec3 uSunDir, uMoonDir, uSkyZen, uSkyHor, uDusk, uSunGlow, uSunDisc, uCloudLit, uCloudDark, uMoonCol, uGround;
uniform float uStars, uNight, uMoonPhase;
uniform mat3 uSkyRot;                        // world direction -> star sphere
#define kSunCol uKeyCol
#define kSunSpec (uKeyCol * 0.385)           // key-light highlights: white by day, faint blue by moonlight
// Vehicle headlamps (lights.h): spot lights, two per car for the nearest few cars.
uniform int uLampN;
uniform vec4 uLampPos[16];                   // lens center, beam (1 low, 2 high)
uniform vec4 uLampDir[16];                   // beam axis (the car's forward), range (m)
uniform vec4 uLampUp[16];                    // the car's up, peak intensity
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
// Ordered dither (4x4 Bayer) at a pixel, 0..1: a fade of f drops the pixels whose threshold is under f (screen door).
const int kBayer[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
float ditherAt(vec2 fc){ ivec2 p = ivec2(fc) & 3; return (float(kBayer[p.x + p.y * 4]) + 0.5) / 16.0; }
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  return mix(mix(hash(i), hash(i+vec2(1,0)), f.x), mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p, int oct){
  float s = 0.0, a = 0.5;
  for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = mat2(1.6, 1.2, -1.2, 1.6) * p; a *= 0.5; }
  return s;
}
const vec2 kPoisson[16] = vec2[](
  vec2(-0.942, -0.399), vec2(0.946, -0.769), vec2(-0.094, -0.929), vec2(0.345, 0.294),
  vec2(-0.916, 0.458), vec2(-0.815, -0.879), vec2(-0.383, 0.277), vec2(0.975, 0.756),
  vec2(0.443, -0.975), vec2(0.537, -0.474), vec2(-0.265, -0.419), vec2(0.792, 0.191),
  vec2(-0.242, 0.997), vec2(-0.814, 0.914), vec2(0.200, 0.786), vec2(0.144, -0.141));
float pcf(sampler2DShadow tex, vec3 c, float radius, float rot){
  int taps = int(uShadowCfg.y);
  mat2 R = mat2(cos(rot), sin(rot), -sin(rot), cos(rot));
  float s = 0.0;
  for (int i = 0; i < taps; i++) s += texture(tex, vec3(c.xy + R * kPoisson[i] * radius, c.z));
  return s / float(taps);
}
// Sun visibility at world position p with surface normal n: a sharp near cascade around
// the camera's target, blended into a coarse far one that reaches the treeline.
// Shade from the clouds overhead: the same cloud layer the sky draws, traced toward the sun.
float cloudShadow(vec3 p){
  if (uClouds <= 0.0) return 1.0;
  vec2 w = p.xz + uSun.xz / max(uSun.y, 0.2) * (1800.0 - p.y);
  float cov = smoothstep(0.50, 0.78, fbm(w * 0.00045 + vec2(uTime * 0.004, uTime * 0.0015), 3)) * uClouds;
  return 1.0 - 0.65 * cov;
}
float shadowMap(vec3 p, vec3 n){
  if (uShadowCfg.x < 0.5) return 1.0;
  float ndl = dot(n, uSun);
  vec3 q = p + n * (0.015 + 0.04 * (1.0 - abs(ndl)));
  float rot = uShadowCfg.y > 12.0 ? hash(gl_FragCoord.xy) * 6.2832 : 0.0;  // few taps: a fixed pattern, not noise
  vec4 L0 = uLightVP0 * vec4(q, 1.0);
  vec3 c0 = L0.xyz / L0.w * 0.5 + 0.5;
  float e0 = max(abs(c0.x - 0.5), abs(c0.y - 0.5));
  float far = 1.0;
  if (e0 > 0.42) {
    vec4 L1 = uLightVP1 * vec4(q, 1.0);
    vec3 c1 = L1.xyz / L1.w * 0.5 + 0.5;
    if (max(abs(c1.x - 0.5), abs(c1.y - 0.5)) < 0.5 && c1.z < 1.0)
      far = pcf(uShadow1, vec3(c1.xy, c1.z - 0.0004), 1.6 / vec2(textureSize(uShadow1, 0)).x, rot);
  }
  if (e0 >= 0.5 || c0.z >= 1.0) return far;
  float nearS = pcf(uShadow0, vec3(c0.xy, c0.z - 0.00012), 2.2 / vec2(textureSize(uShadow0, 0)).x, rot);
  return mix(nearS, far, smoothstep(0.42, 0.5, e0));
}
float shadowAt(vec3 p, vec3 n){ return shadowMap(p, n) * cloudShadow(p); }
float hash3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
// Stars: a grid over the star sphere (which turns with the clock) with one star in a
// few of its cells, most faint, some bright, gently twinkling; a faint milky band.
vec3 starField(vec3 d){
  vec3 s = uSkyRot * d;
  float px = max(length(fwidth(d)), 2e-4);          // one pixel, in radians
  float r = max(px * 0.55, 4e-4);
  vec3 col = vec3(0.0);
  for (int l = 0; l < 2; l++) {
    float N = l == 0 ? 150.0 : 340.0;
    vec3 c = floor(s * N);
    float h = hash3(c + float(l) * 17.0);
    if (h < (l == 0 ? 0.9965 : 0.992)) continue;
    vec3 j = vec3(hash3(c + 1.3), hash3(c + 2.7), hash3(c + 4.1));
    vec3 sp = normalize((c + 0.15 + 0.7 * j) / N);
    if (floor(sp * N) != c) continue;               // its spot must be in this cell, or it's cut in half
    float k = hash3(c + 7.7);
    float b = l == 0 ? 0.4 + 5.0 * pow(k, 6.0) : 0.10 + 0.5 * pow(k, 4.0);
    vec3 tint = mix(vec3(0.70, 0.80, 1.0), vec3(1.0, 0.86, 0.66), hash3(c + 9.1));
    float tw = 0.75 + 0.25 * sin(uTime * (2.0 + 4.0 * k) + k * 60.0);
    vec3 dd = s - sp;
    col += tint * b * tw * exp(-dot(dd, dd) / (r * r)) * min(1.0, 2.5e-7 / (r * r));
  }
  vec3 bn = normalize(vec3(0.35, 0.55, 0.76));
  float band = exp(-pow(dot(s, bn), 2.0) * 22.0) * fbm((s.xy + s.z * 0.7) * 7.0, 4);
  col += vec3(0.010, 0.011, 0.016) * band;
  return col * uStars * smoothstep(0.0, 0.18, d.y);  // dimmed through the thick air near the horizon
}
// The moon's disk: lit on the side facing the sun (so it shows its phase), darker seas,
// and a soft edge one pixel wide. `cover` = how much of the sky behind it it hides.
vec3 moonDisc(vec3 d, out float cover){
  const float R = 0.016;  // radians: about twice its real size, so it reads on screen
  cover = 0.0;
  vec3 m = uMoonDir;
  float px = max(length(fwidth(d)), 1e-4) / R;  // (before any branch: derivatives need the whole quad)
  if (dot(d, m) < 0.99) return vec3(0.0);
  vec3 rt = normalize(cross(m, vec3(0.0, 1.0, 0.0))), up = cross(rt, m);
  vec2 q = vec2(dot(d, rt), dot(d, up)) / R;
  float rr = dot(q, q);
  cover = 1.0 - smoothstep(1.0 - px, 1.0 + px, sqrt(rr));
  if (cover <= 0.0) return vec3(0.0);
  vec3 N = rt * q.x + up * q.y - m * sqrt(max(1.0 - rr, 0.0));
  float lit = smoothstep(-0.04, 0.10, dot(N, uSunDir));
  float seas = 0.72 + 0.28 * smoothstep(0.35, 0.65, fbm(q * 1.7 + 3.0, 3));
  return uMoonCol * (0.025 + lit) * seas * cover;
}
// Linear HDR sky: zenith to horizon gradient (from the clock), the dusk band and glow
// round the sun, the moon, stars at night, drifting cumulus lit by the key light. `oct`
// = cloud detail (0 = no clouds, sun or moon: for fog lookups).
vec3 skyColor(vec3 d, int oct){
  float y = max(d.y, 0.0);
  vec3 c = mix(uSkyHor, uSkyZen, pow(y, 0.42));
  float sd = max(dot(d, uSunDir), 0.0);
  vec2 hz = d.xz / max(length(d.xz), 1e-4), sz = uSunDir.xz / max(length(uSunDir.xz), 1e-4);
  c += uDusk * pow(dot(hz, sz) * 0.5 + 0.5, 3.0) * pow(1.0 - y, 6.0);  // sunrise and sunset burn along that horizon
  c += uSunGlow * (pow(sd, 6.0) * 0.22 * (1.0 - y) + pow(sd, 90.0) * 0.6);
  float md = max(dot(d, uMoonDir), 0.0), moonLit = 0.5 - 0.5 * cos(6.2832 * uMoonPhase);
  if (oct > 0) {
    c += vec3(0.55, 0.65, 0.85) * moonLit * uNight * (pow(md, 1500.0) * 0.10 + pow(md, 40.0) * 0.006);  // halo
    if (oct > 4 && uStars > 0.0) c += starField(d);  // the sky dome only: in reflections they'd only sparkle
    float cover;
    vec3 moon = moonDisc(d, cover);
    c = c * (1.0 - cover) + moon;
  }
  float cl = 0.0;
  if (oct > 0 && uClouds > 0.0 && d.y > 0.0) {
    vec2 p = (uCam.xz + d.xz * (1800.0 / max(d.y, 0.04))) * 0.00045 + vec2(uTime * 0.004, uTime * 0.0015);
    float n = fbm(p, oct), n2 = fbm(p + uSun.xz * 0.05, max(oct - 2, 1));
    float cov = smoothstep(0.50, 0.78, n) * smoothstep(0.02, 0.22, d.y) * uClouds;
    float lit = clamp(0.62 + (n - n2) * 2.4, 0.25, 1.0);  // edges facing the key light (moonlit at night)
    vec3 cc = mix(uCloudDark, uCloudLit, lit) + uSunGlow * pow(sd, 12.0) * 0.8
            + vec3(0.10, 0.12, 0.16) * moonLit * uNight * pow(md, 12.0);
    c = mix(c, cc, cov * 0.92);
    cl = cov;
  }
  // The sun's disk: the sky dome only (a reflected disk blooms on every matte roof; the
  // shaders' highlights stand in for it there).
  if (oct > 3) c += uSunDisc * smoothstep(0.99965, 0.9998, sd) * (1.0 - 0.85 * cl);
  if (d.y < 0.0) c = mix(uSkyHor * 0.85, uGround, clamp(-d.y * 4.0, 0.0, 1.0));
  return c;
}
vec3 skyEnv(vec3 d){ return skyColor(d, 3); }
vec3 ambientAt(vec3 n){ return mix(uAmbGround, uAmbSky, n.y * 0.5 + 0.5); }
// Light from the vehicles' headlamps at p: irradiance, and the highlight `spec` for a
// surface with exponent specP. `wrap` softens the angle term (grass, leaves, smoke).
vec3 lampLight(vec3 p, vec3 n, vec3 V, float specP, float wrap, out vec3 spec){
  vec3 E = vec3(0.0);
  spec = vec3(0.0);
  for (int i = 0; i < uLampN; i++) {
    vec3 L = uLampPos[i].xyz - p;
    float d2 = dot(L, L), range = uLampDir[i].w;
    if (d2 > range * range) continue;
    vec3 f = uLampDir[i].xyz, up = uLampUp[i].xyz;
    float fz = -dot(L, f);
    if (fz < 0.3) continue;  // behind the lens, or the car's own bumper just ahead of it
    float x = dot(L, cross(up, f)) / fz, y = -dot(L, up) / fz;  // tangents right of and above the beam axis
    // Low beam: a flat cutoff just below level (so it doesn't dazzle), spread wide. High
    // beam: a long round hot spot. Both are dimmer on the ground close in (steep below
    // the axis): that's what keeps the pool even down the road.
    float across = 0.45 * smoothstep(1.1, 0.25, abs(x)) + 0.55 * exp(-x * x * 18.0);
    float near = min(1.0, pow(0.035 / max(abs(y), 1e-3), 3.0));
    float prof = uLampPos[i].w > 1.5 ? (y < 0.0 ? near : exp(-y * y * 300.0) + 0.15 * smoothstep(0.25, 0.0, y))
                                     : smoothstep(0.004, -0.012, y) * near;
    float w = clamp(1.0 - d2 * d2 / (range * range * range * range), 0.0, 1.0);
    float e = uLampUp[i].w * across * prof * w * w * smoothstep(0.3, 1.2, fz) / (d2 + 0.3);
    vec3 Ln = L * inversesqrt(d2);
    float ndl = max(dot(n, Ln) * (1.0 - wrap) + wrap, 0.0);
    float Ei = ndl * e;
    Ei = Ei * 10.0 / (Ei + 10.0);  // a wall close in is bright, not blinding
    E += vec3(Ei);
    spec += vec3(pow(max(dot(n, normalize(Ln + V)), 0.0), specP) * Ei);
  }
  const vec3 tint = vec3(1.0, 0.95, 0.86);  // warm white
  spec *= tint;
  return E * tint;
}
// Aerial perspective: height fog (thinner up the mountains), tinted by the sky toward
// the sun. Returns linear HDR; the composite tone maps.
vec3 applyFog(vec3 col, vec3 pos){
  vec3 d = pos - uCam;
  float dist = length(d), b = 0.0065;
  float k = abs(d.y) > 0.5 ? (1.0 - exp(-b * d.y)) / (b * d.y) : 1.0;
  float fog = 1.0 - exp(-0.0007 * exp(-b * max(uCam.y, 0.0)) * dist * k);
  vec3 dir = d / max(dist, 1e-3);
  vec3 fc = skyColor(vec3(dir.x, max(dir.y, 0.02) * 0.3, dir.z), 0);
  col = mix(col, fc, fog);
  // (Past the draw distance there's only sky: the land fades into the sky behind it, so the edge doesn't show.)
  if (uFogFar > 0.0) col = mix(col, skyColor(vec3(dir.x, max(dir.y, 0.0), dir.z), 0), smoothstep(uFogFar * 0.45, uFogFar * 0.97, dist));
  return col;
}
vec4 finishColor(vec3 col, vec3 pos, float alpha){ return vec4(applyFog(col, pos), alpha); }
)";

const char* kLitVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform mat4 uVP; uniform mat4 uModel; uniform mat4 uLightVP;
// (centroid: with MSAA a pixel only partly on a thin triangle - a blade, a needle, a petal - would
// otherwise take the colour extrapolated out past its corners, which can go negative: pow() of that
// is NaN, and the bloom spreads it into a coloured spark)
out vec3 vPos; out vec3 vNrm; centroid out vec4 vCol; out vec4 vLight; out vec3 vLocal;
void main(){
  vec4 wp = uModel * vec4(aPos, 1.0);
  vPos = wp.xyz; vNrm = mat3(uModel) * aNrm; vCol = aCol; vLocal = aPos;
  vLight = uLightVP * wp;
  gl_Position = uVP * wp;
})";

// The lit shader's declarations (the terrain shaders share them).
const char* kLitIns = R"(
in vec3 vPos; in vec3 vNrm; centroid in vec4 vCol; in vec4 vLight; in vec3 vLocal;
out vec4 oCol;
uniform int uMap;                 // 0 proving ground, 1 mountain, 2 freeway
uniform sampler2D uTerrainTex;    // per meter: height, grass, paint
uniform float uTerrainN;          // texels per side
uniform vec2 uRockSnow;           // heights where bare rock, then snow, take over
uniform int uTreeFadeN;           // see-through trees: this many, each (x, z, reach, fade 0..1)
uniform vec4 uTreeFade[24];
)";

// The procedural ground: the look the terrain had before photo-scanned materials, and still has for
// paved ground, ice and the like (and everywhere with ground detail off).
const char* kTerrainProcFS = R"(
float paintAt(vec2 xz){
  vec2 uv = (xz + 600.0 + 0.5) / uTerrainN;
  return texture(uTerrainTex, uv).b;
}
float gridLine(vec2 p, float w){
  vec2 d = abs(fract(p - 0.5) - 0.5) / max(fwidth(p), vec2(1e-4));
  return 1.0 - clamp(min(d.x, d.y) - w, 0.0, 1.0);
}
// The loose and slippery surfaces (paint 4 and 7..11): gravel, sand, mud, snow, ice and
// wet asphalt. Returns false for anything else.
bool surfacePaint(int paint, inout vec3 base, inout float specK, inout float specP){
  vec2 p = vPos.xz;
#ifdef PROC_LITE
  if (false) {
#else
  if (paint == 4) {  // gravel: grey-brown stones
    base = vec3(0.40, 0.37, 0.33) * (0.8 + 0.25 * vnoise(p * 9.0) + 0.15 * vnoise(p * 23.0)) * (0.9 + 0.2 * fbm(p * 0.2, 2));
  } else if (paint == 7) {  // sand: pale, rippled by the wind
    float rip = 0.5 + 0.5 * sin(dot(p, vec2(0.8, 2.3)) * 1.6 + 2.5 * fbm(p * 0.3, 2));
    base = mix(vec3(0.74, 0.64, 0.46), vec3(0.83, 0.74, 0.56), rip * 0.5 + 0.5 * vnoise(p * 3.0)) * (0.92 + 0.12 * vnoise(p * 17.0));
  } else if (paint == 8) {  // mud: dark and glossy, puddles in the hollows
    float wetp = smoothstep(0.55, 0.7, fbm(p * 0.25, 3));
    base = mix(vec3(0.24, 0.18, 0.11), vec3(0.17, 0.13, 0.08), vnoise(p * 2.0)) * (0.85 + 0.2 * vnoise(p * 11.0));
    base = mix(base, vec3(0.10, 0.09, 0.07), wetp * 0.7);
    specK = mix(0.10, 0.35, wetp); specP = mix(24.0, 90.0, wetp);
  } else if (paint == 9) {  // snow: white with blue in the dips, sparkles
    base = mix(vec3(0.84, 0.87, 0.93), vec3(0.95, 0.96, 0.99), fbm(p * 0.4, 3)) * (0.96 + 0.06 * vnoise(p * 9.0));
    // On a road or trail (it has a distance along it): packed, greyer wheel tracks.
    float ruts = (1.0 - smoothstep(0.2, 0.5, abs(abs(vCol.g) - 0.85))) * step(0.5, vCol.b) * (0.75 + 0.25 * vnoise(p * 3.0));
    base *= mix(vec3(1.0), vec3(0.70, 0.74, 0.80), ruts * 0.85);
    specK = 0.08; specP = 30.0;
#endif
  } else if (paint == 10) {  // ice: blue-grey, glassy, white cracks
    float crack = 1.0 - smoothstep(0.0, 0.025, abs(vnoise(p * 0.7) - 0.5));
    base = mix(vec3(0.62, 0.72, 0.80), vec3(0.78, 0.85, 0.90), fbm(p * 0.15, 3));
    base = mix(base, vec3(0.95), crack * 0.6);
    specK = 0.45; specP = 140.0;
  } else if (paint == 12) {  // freeway: three 3.6 m lanes each way between a 1.5 m inner and a 3 m outer shoulder
    float az = abs(p.y), fw = max(fwidth(p.y), 1e-3) * 1.5;
    base = vec3(0.17, 0.172, 0.18) * (0.86 + 0.12 * vnoise(p * 1.1) + 0.06 * vnoise(p * 9.0));
    base *= mix(1.0, 1.12, step(12.3, az));  // the shoulders are older, paler
    // Darker, polished wheel tracks down each lane.
    float lanePos = mod(az - 1.5, 3.6);
    float track = (1.0 - smoothstep(0.25, 0.55, abs(lanePos - 0.9))) + (1.0 - smoothstep(0.25, 0.55, abs(lanePos - 2.7)));
    base *= 1.0 - 0.10 * track * step(1.5, az) * step(az, 12.3);
    float yellow = 1.0 - smoothstep(0.08, 0.08 + fw, abs(az - 1.45));
    float edge = 1.0 - smoothstep(0.1, 0.1 + fw, abs(az - 12.35));
    float dash = step(fract(p.x / 10.0), 0.4);  // 4 m dashes, 6 m gaps
    float lanes = (1.0 - smoothstep(0.07, 0.07 + fw, abs(az - 5.1))) + (1.0 - smoothstep(0.07, 0.07 + fw, abs(az - 8.7)));
    base = mix(base, vec3(0.92, 0.72, 0.16), yellow);
    base = mix(base, vec3(0.92), max(edge, lanes * dash));
    specK = 0.05; specP = 30.0;
  } else if (paint == 13) {  // race circuit: fresh asphalt, white edge lines, red and white kerbs, the start line
    float lat = abs(vCol.g), along = vCol.b, fw = max(fwidth(lat), 1e-3) * 1.5;
    base = vec3(0.15, 0.152, 0.16) * (0.88 + 0.1 * vnoise(p * 1.4) + 0.05 * vnoise(p * 11.0));
    base *= 1.0 - 0.12 * (1.0 - smoothstep(0.6, 2.2, abs(lat - 1.6))) * 0.5;  // the racing line, rubbered in
    float edge = 1.0 - smoothstep(0.08, 0.08 + fw, abs(lat - 5.2));
    base = mix(base, vec3(0.9), edge);
    float kerb = step(5.35, lat) * (1.0 - smoothstep(6.2, 6.2 + fw, lat));
    vec3 kc = mod(floor(along / 2.0), 2.0) < 1.0 ? vec3(0.75, 0.08, 0.06) : vec3(0.92);
    base = mix(base, kc, kerb);
    float start = (1.0 - step(1.6, abs(along - 90.0))) * step(lat, 5.2);
    float check = mod(floor(p.x / 0.8) + floor(p.y / 0.8), 2.0);
    base = mix(base, vec3(check * 0.9 + 0.05), start);
    specK = 0.05; specP = 30.0;
#ifndef PROC_LITE
  } else if (paint == 14) {  // desert hardpan: pale cracked clay
    base = mix(vec3(0.66, 0.55, 0.40), vec3(0.74, 0.63, 0.47), fbm(p * 0.05, 3)) * (0.9 + 0.12 * vnoise(p * 6.0));
    base *= 1.0 - 0.3 * (1.0 - smoothstep(0.0, 0.03, abs(vnoise(p * 1.1) - 0.5)));
  } else if (paint == 15) {  // red rock: banded sandstone, darker streaks down the faces
    float band = vnoise(vec2(vPos.y * 0.9 + 2.0 * fbm(p * 0.03, 2), (p.x - p.y) * 0.01));
    base = mix(vec3(0.55, 0.27, 0.16), vec3(0.72, 0.42, 0.26), band) * (0.82 + 0.25 * fbm(p * 0.15, 3));
    base = mix(base, base * 0.6, smoothstep(0.6, 0.72, vnoise(vec2(p.x * 0.5, vPos.y * 0.08))) * 0.5);
    specK = 0.05; specP = 20.0;
#endif
  } else if (paint == 11) {  // wet asphalt: dark, shiny, puddles
    float pud = smoothstep(0.6, 0.72, fbm(p * 0.18, 3));
    base = vec3(0.13, 0.135, 0.14) * (0.85 + 0.15 * vnoise(p * 1.3));
    specK = mix(0.25, 0.6, pud); specP = mix(60.0, 200.0, pud);
  } else return false;
  return true;
}
void terrainProcedural(vec3 n, inout vec3 base, inout float specK, inout float specP){
    float r = length(vPos.xz);
    float pad = 1.0 - smoothstep(130.0, 140.0, r);
    vec3 padc = vec3(0.41, 0.415, 0.43) * (0.92 + 0.10 * vnoise(vPos.xz * 0.5)) * (0.9 + 0.14 * fbm(vPos.xz * 0.04, 3));
    padc *= 1.0 - 0.13 * gridLine(vPos.xz, 0.0);
    padc = mix(padc, vec3(0.30, 0.31, 0.33), 0.75 * gridLine(vPos.xz / 10.0, 0.6));
    padc = mix(padc, vec3(0.92, 0.45, 0.10), 0.85 * gridLine(vPos.xz / 50.0, 1.2));
#ifdef PROC_LITE
    // (LOW and POTATO ground: the land under a road is a flat tone, the photo layers draw the countryside.)
    float n2 = 0.5, steep = 0.0, snow = 0.0;
    vec3 land = vec3(0.22, 0.27, 0.11);
    base = uMap == 0 ? mix(land, padc, pad) : land;
#else
    // Countryside: two-tone meadow with dry patches, rock on steep ground and up the rim
    // mountains (banded strata), snow on the high tops.
    float n1 = fbm(vPos.xz * 0.02, 4), n2 = vnoise(vPos.xz * 0.9);
    vec3 grass = mix(vec3(0.14, 0.22, 0.07), vec3(0.29, 0.33, 0.12), n1);
    grass = mix(grass, vec3(0.42, 0.38, 0.21), smoothstep(0.6, 0.8, fbm(vPos.xz * 0.006 + 3.1, 3)) * 0.6);
    grass *= (0.82 + 0.26 * n2) * (0.9 + 0.2 * vnoise(vPos.xz * 4.0));
    float strata = vnoise(vec2(vPos.y * 0.22 + 6.0 * fbm(vPos.xz * 0.008, 3), (vPos.x - vPos.z) * 0.004));
    vec3 rock = mix(vec3(0.29, 0.27, 0.25), vec3(0.37, 0.34, 0.31), strata) * (0.7 + 0.45 * fbm(vPos.xz * 0.05 + vPos.y * 0.03, 4));
    // Warmer and cooler stone across the range, and streaks running down the faces.
    rock *= mix(vec3(1.06, 0.97, 0.86), vec3(0.88, 0.93, 1.02), fbm(vPos.xz * 0.003 + 9.0, 2));
    rock *= 0.8 + 0.3 * vnoise(vec2((vPos.x + vPos.z) * 0.12, vPos.y * 0.012));
    float steep = (uMap == 0 || uMap == 2) ? smoothstep(0.84, 0.62, n.y + 0.14 * (n1 - 0.5)) : smoothstep(0.72, 0.55, n.y + 0.14 * (n1 - 0.5));
    float high = smoothstep(uRockSnow.x, uRockSnow.x + 90.0, vPos.y + 30.0 * n1);
    vec3 land = mix(grass, rock, max(steep, high * 0.8));
    float snow = smoothstep(uRockSnow.y, uRockSnow.y + 50.0, vPos.y + 70.0 * n1) * smoothstep(0.5, 0.75, n.y);
    land = mix(land, vec3(0.90, 0.92, 0.96), snow);
    base = uMap == 0 ? mix(land, padc, pad) : land;
#endif
    // Road: r = near-road flag, g = signed lateral offset, b = distance along.
    // Evaluated unconditionally so fwidth() stays well defined, then masked.
    float lat = abs(vCol.g), along = vCol.b;
    float e = max(fwidth(lat), 0.02) * 1.5;
    float onRoad = 1.0 - smoothstep(5.0 - e, 5.0 + e, lat);
    float onShoulder = 1.0 - smoothstep(6.6 - e, 6.6 + e, lat);
    vec3 asphalt = vec3(0.21, 0.215, 0.22) * (0.88 + 0.16 * vnoise(vPos.xz * 1.3));
    vec3 gravel = vec3(0.45, 0.42, 0.36) * (0.85 + 0.3 * vnoise(vPos.xz * 2.0));
    float edgeLine = 1.0 - smoothstep(0.09, 0.09 + e, abs(lat - 4.6));
    asphalt = mix(asphalt, vec3(0.92), edgeLine);
    float dash = step(fract(along / 9.0), 0.5);
    float centerLine = (1.0 - smoothstep(0.09, 0.09 + e, lat)) * dash;
    asphalt = mix(asphalt, vec3(0.95, 0.76, 0.18), centerLine);
    vec3 roadCol = mix(mix(base, gravel, onShoulder), asphalt, onRoad);
    if (uMap == 0 || uMap == 2) {
      if (uMap == 0) base = mix(base, roadCol, step(0.5, vCol.r));
      // The surface course (and its concrete apron).
      int paint = int(paintAt(vPos.xz) + 0.5);
      if (paint == 5) base = vec3(0.50, 0.49, 0.47) * (0.9 + 0.12 * vnoise(vPos.xz * 1.7)) * (1.0 - 0.3 * gridLine(vPos.xz / 3.0, 0.2));
      else surfacePaint(paint, base, specK, specP);
    } else {
#ifdef PROC_LITE
      int paint = int(paintAt(vPos.xz) + 0.5);
      if (paint == 5) base = vec3(0.50, 0.49, 0.47) * (0.9 + 0.12 * vnoise(vPos.xz * 1.7)) * (1.0 - 0.35 * gridLine(vPos.xz / 3.0, 0.2));
      else surfacePaint(paint, base, specK, specP);
#else
      // Mountain: paint looked up per pixel (from a slightly wobbled spot, so the edges
      // are ragged rather than 1 m stairs): gravel road, dirt trail, slickrock, gravel.
      vec2 wob = (vec2(vnoise(vPos.xz * 0.8), vnoise(vPos.zx * 0.8 + 7.0)) - 0.5) * 1.3;
      int paint = int(paintAt(vPos.xz + wob) + 0.5);
      vec3 dirt = mix(vec3(0.29, 0.25, 0.20), vec3(0.37, 0.32, 0.26), fbm(vPos.xz * 0.35, 3)) * (0.88 + 0.18 * vnoise(vPos.xz * 4.0));
      vec3 grav = vec3(0.37, 0.35, 0.32) * (0.85 + 0.2 * vnoise(vPos.xz * 9.0) + 0.1 * vnoise(vPos.xz * 23.0)) * (0.9 + 0.2 * fbm(vPos.xz * 0.2, 2));
      // Worn wheel tracks: a darker, smoother pair along trails and each lane of the road.
      float sl = vCol.g;
      float trk = paint == 1 ? abs(abs(sl) - 2.6) : abs(sl);
      float ruts = 1.0 - smoothstep(0.15, 0.4, abs(trk - 0.8));
      if (paint == 1) base = mix(grav, dirt * 0.85, ruts * 0.5);
      else if (paint == 2) base = mix(dirt, dirt * 0.72, ruts * 0.55);
      else if (paint == 3) {
        vec3 slick = mix(vec3(0.46, 0.43, 0.39), vec3(0.36, 0.34, 0.32), fbm(vPos.xz * 0.12, 3)) * (0.88 + 0.18 * vnoise(vPos.xz * 2.5));
        slick = mix(slick, vec3(0.22, 0.21, 0.20), smoothstep(0.62, 0.7, fbm(vec2(vPos.x * 0.6, vPos.z * 0.08), 2)) * 0.6);  // water streaks
        float crack = 1.0 - smoothstep(0.0, 0.03, abs(vnoise(vPos.xz * 0.45) - 0.5));
        base = mix(slick, slick * 0.45, crack * 0.8);
      } else if (paint == 4) base = grav;
      else if (surfacePaint(paint, base, specK, specP)) {
      } else if (paint == 5) {  // concrete pad with saw-cut joints every 3 m
        base = vec3(0.50, 0.49, 0.47) * (0.9 + 0.12 * vnoise(vPos.xz * 1.7)) * (0.95 + 0.1 * fbm(vPos.xz * 0.3, 2));
        base *= 1.0 - 0.35 * gridLine(vPos.xz / 3.0, 0.2);
      } else if (paint == 6) {  // scree: loose broken stone, pale with dark chips
        vec3 sc = mix(vec3(0.47, 0.45, 0.41), vec3(0.33, 0.31, 0.29), vnoise(vPos.xz * 2.7));
        sc *= 0.72 + 0.45 * vnoise(vPos.xz * 13.0);
        base = mix(sc, sc * 0.55, smoothstep(0.62, 0.75, vnoise(vPos.xz * 6.0)));
      }
      // Forest floor: brown needle litter under the pines.
      if (paint == 0) base = mix(base, vec3(0.24, 0.20, 0.13) * (0.8 + 0.3 * n2), smoothstep(0.55, 0.75, fbm(vPos.xz * 0.012 + vec2(7.0, -3.0), 3)) * smoothstep(18.0, 30.0, vPos.y) * 0.55 * (1.0 - snow) * (1.0 - steep));
#endif
    }
    // Wet ground at the water's edge, and a dark silty bed under it.
    float wl = texture(uTerrainTex, (vPos.xz + 600.5) / uTerrainN).a;
    float wet = 1.0 - smoothstep(0.0, 0.5, vPos.y - wl);
    base = mix(base, base * 0.45 + vec3(0.04, 0.035, 0.02), wet);
    base = mix(base, vec3(0.10, 0.11, 0.07), smoothstep(0.0, -0.4, vPos.y - wl) * 0.8);
}
)";

const char* kLitFSBody = R"(
void main(){
  vec3 n = normalize(vNrm);
  vec3 base = vCol.rgb;
  int mat = int(vCol.a + 0.5);
  int wear = mat / 200;  // (a damaged car part: Placed::wear)
  mat -= wear * 200;
  bool sunlit = mat >= 100;  // (a cargo model inside a body's shadow: Placed::sunlit)
  if (sunlit) mat -= 100;
  if (wear == 1) base = mix(base, base * vec3(1.0, 0.72, 0.5), 0.7);  // bent: warmed
  else if (wear == 2) base = mix(base, vec3(0.62, 0.17, 0.08), 0.45) * 0.9;  // broken: rust red
  else if (wear == 3) base *= 0.3;  // dead: soot
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
  if (mat == 7) {
    terrainProcedural(n, base, specK, specP);
  } else if (mat == 12 || mat == 16) {
    // Rock: grainy, with darker veins, crusty lichen on the tops (not on a slab: sandstone and granite stay their own colour).
    float g1 = vnoise(vPos.xz * 2.3 + vPos.y * 1.1), g2 = vnoise(vPos.zy * 2.7 - vPos.x * 0.9);
    base *= 0.72 + 0.5 * (0.5 * g1 + 0.5 * g2);
    float vein = 1.0 - smoothstep(0.0, 0.04, abs(vnoise(vPos.xy * 0.9 + vPos.z * 0.6) - 0.5));
    base *= 1.0 - 0.35 * vein;
    float lichen = smoothstep(0.6, 0.72, vnoise(vPos.xz * 3.1 + vPos.y * 2.0)) * smoothstep(0.35, 0.85, n.y);
    if (mat == 12) base = mix(base, vec3(0.42, 0.45, 0.26), lichen * 0.5);
    specK = 0.06; specP = 20.0;
  } else if (mat == 14) {
    // Wood: weathered grain.
    float g = vnoise(vec2((vPos.x + vPos.z) * 2.0, vPos.y * 14.0)) * 0.6 + vnoise(vec2(vPos.y * 2.0, (vPos.x - vPos.z) * 16.0)) * 0.4;
    base *= 0.78 + 0.35 * g;
    specK = 0.03;
  } else if (mat == 13) {
    // Foliage: clumps of lighter and darker leaves.
    base *= 0.8 + 0.4 * vnoise(vPos.xz * 2.2 + vPos.y * 1.7);
    specK = 0.0;
  } else if (mat == 3) {
    vec3 a = abs(n);
    vec2 uv = a.y > 0.6 ? vPos.xz : (a.x > a.z ? vPos.zy : vPos.xy);
    base *= 1.0 - 0.22 * gridLine(uv, 0.0);
    base *= 0.9 + 0.15 * vnoise(uv * 1.7);
  } else if (mat == 9) {
    // Tire (wheel-local coords: z = axle, tread at r = 0.32): grooves and tread blocks.
    float r = length(vLocal.xy), z = abs(vLocal.z);
    float ang = atan(vLocal.y, vLocal.x) / 6.2832;
    if (r > 0.305) {
      float rib = 1.0 - smoothstep(0.004, 0.007, abs(z - 0.035));
      float block = step(0.6, fract(ang * 52.0 + z * 4.0)) * step(0.045, z) * step(z, 0.1);
      base = mix(vec3(0.075), vec3(0.025), max(rib, block * 0.85));
    } else {
      base = vec3(0.06) * (0.9 + 0.2 * step(0.5, fract(r * 70.0)) * step(0.26, r));
    }
    specK = 0.05; specP = 8.0;
  }

  vec3 lin = pow(max(base, 0.0), vec3(2.2));
  float sh = shadowAt(vPos, n);
  if (sunlit) sh = max(sh, 0.85);
  float diff = max(dot(n, uSun), 0.0);
  vec3 amb = ambientAt(n);
  vec3 V = normalize(uCam - vPos);
  vec3 H = normalize(V + uSun);
  vec3 env = skyEnv(reflect(-V, n));
  float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
  // (Rough ground throws some light back the way it came, so a road ahead reads brighter
  // from behind the lamps than its grazing angle alone gives: a little wrap.)
  vec3 lspec, lamp = lampLight(vPos, n, V, mat == 4 || mat == 5 ? 120.0 : max(specP, 12.0), mat == 13 ? 0.4 : mat == 7 ? 0.1 : 0.0, lspec);
  vec3 col;
  if (mat == 8) {         // metal: tinted reflections
    col = lin * (amb + diff * sh * kSunCol + lamp) * 0.5 + lin * env * 0.7 + pow(max(dot(n, H), 0.0), 40.0) * sh * 0.8 * kSunSpec + lspec * 0.3;
  } else if (mat == 11) { // chrome
    col = env * 0.85 * (0.6 + 0.4 * lin) + pow(max(dot(n, H), 0.0), 200.0) * sh * 4.0 * kSunSpec + lspec * 0.6;
  } else if (mat == 4) {  // painted parts: clear-coat
    col = lin * (amb + diff * sh * kSunCol + lamp) + env * fres * 0.9 + lin * env * 0.1 + pow(max(dot(n, H), 0.0), 120.0) * sh * 1.5 * kSunSpec + lspec * 0.4;
  } else if (mat == 5) {  // glass
    col = vec3(0.01) + env * (0.1 + 0.9 * fres) + lspec * 0.5;
  } else if (mat == 13) { // leaves: soft wrapped light, and sun glowing through from behind
    float wrap = max(dot(n, uSun) * 0.6 + 0.4, 0.0);
    float through = pow(max(dot(-V, uSun), 0.0), 4.0) * 0.5;
    col = lin * (amb * 0.9 + (wrap + through) * sh * kSunCol + lamp);
  } else {
    col = lin * (amb + diff * sh * kSunCol + lamp) + pow(max(dot(n, H), 0.0), specP) * specK * sh * 2.0 * kSunSpec + lspec * specK * 2.0;
  }
  if (mat == 6) col = lin * 2.2;
  if (wear == 1 || wear == 2) {  // a rim of hot light, breathing slowly: reads from a few metres, in the dark under a car too
    float rim = pow(1.0 - max(dot(n, V), 0.0), 2.0);
    float pulse = 0.7 + 0.3 * sin(uTime * 3.5);
    col += (wear == 2 ? vec3(1.0, 0.2, 0.04) * 1.1 : vec3(0.9, 0.45, 0.04) * 0.6) * (0.14 + 0.95 * rim) * pulse;
  }
  oCol = finishColor(col, vPos, 1.0);
})";

// Vehicle bodies: smooth subdivided shell with per-pixel detail drawn from lattice
// coordinates (i along, j across, k up). Pass 0 draws everything but the window
// glass; pass 1 draws only the glass, blended.
const char* kCarVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in float aBroken;
layout(location=3) in vec3 aLat;
layout(location=4) in float aRegion;
uniform mat4 uVP; uniform mat4 uLightVP;
uniform mat4 uModel;  // the car's frame (its skin is kept in it: Renderer::CarSlot)
out vec3 vPos; out vec3 vNrm; out vec3 vLat; out float vRegion; out float vBroken; out vec4 vLight;
void main(){
  vec4 wp = uModel * vec4(aPos, 1.0);
  vPos = wp.xyz; vNrm = mat3(uModel) * aNrm; vLat = aLat; vRegion = aRegion; vBroken = aBroken;
  vLight = uLightVP * wp;
  gl_Position = uVP * wp;
})";

const char* kCarFSBody = R"(
in vec3 vPos; in vec3 vNrm; in vec3 vLat; in float vRegion; in float vBroken; in vec4 vLight;
uniform vec3 uPaint;   // body color
uniform vec4 uCabin;   // glass cabin: i start, i end, k bottom, k top
uniform vec4 uBody;    // length cells, width cells, front lamp row k, rear lamp row k
uniform vec4 uStyle;   // lamp style (0 rect, 1 round, 2 slim), grille (0 center, 1 wide, 2 low intake), k per j
uniform float uBrake;  // 0..1 brake light intensity
uniform vec4 uLampOn;  // lit: headlamp left, right (1 low, 1.5 high), tail lamp left, right (lights.h)
uniform vec4 uLampOk;  // still whole: the same four (0 = smashed)
uniform float uReverse;  // 1 in reverse: the reversing lamps
uniform vec4 uTrim;    // two-tone lower body: color, top (lattice k; 0 = none)
uniform int uPass;     // 0 = body, 1 = glass
uniform int uVehicle;  // catalog body style: EF, Jesko, Bronco, 4Runner
// Sculpted shell (bodyshell.cpp): vLat is then the rest position in meters (x along,
// y up, z across) and these are its lines: floor, belt, roof, hood front | front x,
// half width, windshield base x, roof front x | B-pillar x, C-pillar x, liftgate
// bottom y, liftgate half width | trim top y, roof tuck, rear x, liftgate top x.
uniform int uShell;      // 1 4Runner, 2 Bronco, 3 Civic, 4 Tacoma (uShD.w: its cab's back x), 5 K5 Blazer
uniform vec4 uShA, uShB, uShC, uShD;
uniform vec4 uShE;       // front door's front edge x, gate top y, bumper top y, wheel opening top y
out vec4 oCol;
// Signed distance to a rounded rectangle (negative inside).
float rbox(vec2 p, vec2 c, vec2 h, float r){
  vec2 q = abs(p - c) - h + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float lineAt(float x, float at, float w, float fw){ return 1.0 - smoothstep(w, w + fw * 1.5, abs(x - at)); }
void main(){
  int region = int(vRegion + 0.5);
  float i = vLat.x, j = vLat.y, k = vLat.z;
  float fwI = max(fwidth(i), 1e-4);
  vec3 n = normalize(vNrm);
  vec3 V = normalize(uCam - vPos);

  // ---- Window shapes ----
  float ci0 = uCabin.x, ci1 = uCabin.y, ck0 = uCabin.z, ck1 = uCabin.w, W = uBody.y;
  float kc = 0.5 * (ck0 + ck1), kh = 0.5 * (ck1 - ck0) - 0.13;
  float win = 1.0;
  // Doors: uStyle.w = lattice columns per door (negative: two-door).
  float dcells = max(abs(uStyle.w), 1.0);
  bool rearDoor = uStyle.w > 0.5;
  if (region == 1) {
    float clen = ci1 - ci0;
    if (clen > 2.5) {  // two side windows split by a B-pillar
      float mid = rearDoor ? ci0 + dcells : ci0 + clen * 0.52;
      float w1 = rbox(vec2(i, k), vec2(0.5 * (ci0 + mid), kc), vec2(0.5 * (mid - ci0) - 0.16, kh), 0.18);
      float w2 = rbox(vec2(i, k), vec2(0.5 * (mid + ci1), kc), vec2(0.5 * (ci1 - mid) - 0.16, kh), 0.18);
      win = min(w1, w2);
      if (rearDoor) {
        // Each door has its own glass; the fixed quarter window sits behind the C pillar.
        float rear = min(mid + dcells, ci1 - 0.3);
        w2 = rbox(vec2(i, k), vec2(0.5 * (mid + rear), kc), vec2(0.5 * (rear - mid) - 0.12, kh), 0.09);
        float w3 = rbox(vec2(i, k), vec2(0.5 * (rear + ci1), kc), vec2(0.5 * (ci1 - rear) - 0.13, kh), 0.10);
        win = min(w1, min(w2, w3));
      }
      if (uVehicle == 1) { // one long side window in each dihedral door
        win = rbox(vec2(i, k), vec2(0.5 * (ci0 + ci1), kc), vec2(0.5 * clen - 0.10, kh + 0.07), 0.10);
      }
    } else {
      win = rbox(vec2(i, k), vec2(0.5 * (ci0 + ci1), kc), vec2(0.5 * clen - 0.16, kh), 0.18);
    }
  } else if (region == 2 || region == 3) {
    win = rbox(vec2(j, k), vec2(0.5 * W, kc), vec2(0.5 * W - 0.24, kh + 0.03), 0.16);
    if (uVehicle == 1)
      win = rbox(vec2(j, k), vec2(0.5 * W, kc), vec2(0.5 * W - 0.08, kh + 0.08), 0.06);
  }
  // Sculpted shell: the real window outlines, in meters.
  vec3 P = vLat;
  float shF = uShA.x, shB = uShA.y, shT = uShA.z, shHood = uShA.w;
  float shXf = uShB.x, shW2 = uShB.y, shXa = uShB.z, shXrf = uShB.w;
  float shXB = uShC.x, shXC = uShC.y, shLiftY = uShC.z, shLiftZ = uShC.w;
  float shTrim = uShD.x, shTuck = uShD.y, shXr = uShD.z, shXrr = uShD.w;
  float shYlo = shB + 0.035, shYhi = shT - 0.085, shXD = shXr + 0.32;
  float shAx = shXa - (P.y - shB) / max(0.1, shT - shB) * (shXa - shXrf);  // A-pillar line at this height
  float shXE = uShE.x, shGateTop = uShE.y, shBump = uShE.z, shArch = uShE.w;
  if (uShell == 2) {
    win = 1.0;
    if (region == 1) {
      // Bronco 4-door: front and rear door glass, then the big quarter window in the hardtop.
      float yc = 0.5 * (shYlo + shYhi), yh = 0.5 * (shYhi - shYlo);
      float w1 = rbox(P.xy, vec2(0.5 * (shXB + shAx), yc), vec2(0.5 * (shAx - shXB) - 0.06, yh), 0.025);
      w1 = max(w1, P.x - (shAx - 0.06));
      float w2 = rbox(P.xy, vec2(0.5 * (shXC + shXB), yc), vec2(0.5 * (shXB - shXC) - 0.055, yh), 0.025);
      float w3 = rbox(P.xy, vec2(0.5 * (shXC + shXr + 0.2), yc), vec2(0.5 * (shXC - shXr - 0.2) - 0.06, yh), 0.03);
      win = min(w1, min(w2, w3));
    }
  } else if (uShell == 3) {
    win = 1.0;
    if (region == 1) {
      // EF hatch: a long door glass behind a thin A pillar, and a quarter window that runs
      // back to the hatch, its rear edge raked with it.
      float ylo = shB + 0.03, yhi = shT - 0.06, yc = 0.5 * (ylo + yhi), yh = 0.5 * (yhi - ylo);
      float w1 = rbox(P.xy, vec2(0.5 * (shXB + shAx), yc), vec2(0.5 * (shAx - shXB) - 0.045, yh), 0.05);
      w1 = max(w1, P.x - (shAx - 0.05));
      float rearEdge = shXr + 0.2 + (P.y - shB) / max(0.1, shT - shB) * (shXrr - shXr);
      float w3 = rbox(P.xy, vec2(0.5 * (shXB - 0.06 + shXr), yc), vec2(0.5 * (shXB - 0.06 - shXr), yh), 0.06);
      w3 = max(w3, rearEdge - P.x);
      win = min(w1, w3);
    }
  }
  if (uShell == 3 && (region == 2 || region == 3)) {
    float zRef = shW2 - 0.09, zLim = zRef - shTuck * (P.y - shB) / max(0.1, shT - shB);
    float u = abs(P.z) / max(0.1, zLim) * zRef;
    float y0 = shB + 0.04, y1 = shT - 0.06;
    win = rbox(vec2(u, P.y), vec2(0.0, 0.5 * (y0 + y1)), vec2(zRef, 0.5 * (y1 - y0)), 0.07);
  }
  if (uShell == 2 && (region == 2 || region == 3)) {
    float zRef = (region == 2 ? shW2 : shLiftZ) - 0.08, zLim = region == 2 ? zRef - shTuck * (P.y - shB) / max(0.1, shT - shB) : zRef;
    float u = abs(P.z) / max(0.1, zLim) * zRef;
    float y0 = shB + 0.05, y1 = shT - 0.07;
    win = rbox(vec2(u, P.y), vec2(0.0, 0.5 * (y0 + y1)), vec2(zRef, 0.5 * (y1 - y0)), 0.04);
  }
  if (uShell == 1) {
    win = 1.0;
    if (region == 1) {
      float yc = 0.5 * (shYlo + shYhi), yh = 0.5 * (shYhi - shYlo);
      // Front door glass: behind the A pillar and the mirror sail, ahead of the B pillar.
      float w1 = rbox(P.xy, vec2(0.5 * (shXB + shXa), yc), vec2(0.5 * (shXa - shXB) - 0.045, yh), 0.03);
      w1 = max(w1, P.x - (shAx - 0.075));
      w1 = max(w1, -max((shXa - 0.19) - P.x, P.y - (shB + 0.16)));
      float w2 = rbox(P.xy, vec2(0.5 * (shXC + shXB), yc), vec2(0.5 * (shXB - shXC) - 0.05, yh), 0.035);
      float w3 = rbox(P.xy, vec2(0.5 * (shXD + shXC - 0.06), yc), vec2(0.5 * (shXC - 0.06 - shXD), yh), 0.07);
      win = min(w1, min(w2, w3));
    } else if (region == 2 || region == 3) {
      // Windshield (narrowing with the tumblehome) and the liftgate glass.
      float zRef = (region == 2 ? shW2 : shLiftZ) - 0.075;
      float zLim = region == 2 ? zRef - shTuck * (P.y - shB) / max(0.1, shT - shB) : zRef;
      float u = abs(P.z) / max(0.1, zLim) * zRef;
      float y0 = shB + (region == 2 ? 0.05 : 0.04), y1 = shT - 0.07;
      win = rbox(vec2(u, P.y), vec2(0.0, 0.5 * (y0 + y1)), vec2(zRef, 0.5 * (y1 - y0)), 0.05);
    }
  }
  if (uShell == 4) {
    win = 1.0;
    if (region == 1) {
      // Double cab: front and rear door glass between the A, B and C pillars.
      float yc = 0.5 * (shYlo + shYhi), yh = 0.5 * (shYhi - shYlo);
      float w1 = rbox(P.xy, vec2(0.5 * (shXB + shXa), yc), vec2(0.5 * (shXa - shXB) - 0.05, yh), 0.03);
      w1 = max(w1, P.x - (shAx - 0.075));
      float w2 = rbox(P.xy, vec2(0.5 * (shXC + shXB), yc), vec2(0.5 * (shXB - shXC) - 0.05, yh), 0.04);
      w2 = max(w2, (shXrr + 0.07) - P.x);
      win = min(w1, w2);
    } else if (region == 2) {
      float zRef = shW2 - 0.075, zLim = zRef - shTuck * (P.y - shB) / max(0.1, shT - shB);
      float u = abs(P.z) / max(0.1, zLim) * zRef;
      win = rbox(vec2(u, P.y), vec2(0.0, 0.5 * (shB + 0.05 + shT - 0.07)), vec2(zRef, 0.5 * (shT - 0.07 - shB - 0.05)), 0.05);
    } else if (region == 3) {
      // The back glass in the cab's back wall (a slider in the middle).
      win = rbox(P.zy, vec2(0.0, 0.5 * (shB + 0.08 + shT - 0.13)), vec2(0.56, 0.5 * (shT - 0.13 - shB - 0.08)), 0.05);
    }
  }
  if (uShell == 5) {
    win = 1.0;
    if (region == 1) {
      // 1972 K5 Blazer: the door glass (with its vent wing up front) behind a thin A pillar,
      // then the long quarter window in the removable top, back to near the tailgate.
      float yc = 0.5 * (shYlo + shYhi), yh = 0.5 * (shYhi - shYlo);
      float w1 = rbox(P.xy, vec2(0.5 * (shXB + shAx), yc), vec2(0.5 * (shAx - shXB) - 0.05, yh), 0.03);
      w1 = max(w1, P.x - (shAx - 0.06));
      float w2 = rbox(P.xy, vec2(0.5 * (shXB + shXr) - 0.05, yc + 0.01), vec2(0.5 * (shXB - shXr) - 0.2, yh - 0.01), 0.08);
      win = min(w1, w2);
    } else if (region == 2) {
      float zRef = shW2 - 0.07, zLim = zRef - shTuck * (P.y - shB) / max(0.1, shT - shB);
      float u = abs(P.z) / max(0.1, zLim) * zRef;
      win = rbox(vec2(u, P.y), vec2(0.0, 0.5 * (shB + 0.05 + shT - 0.06)), vec2(zRef, 0.5 * (shT - 0.06 - shB - 0.05)), 0.03);
    } else if (region == 3) {
      // The tailgate's roll-down glass.
      win = rbox(P.zy, vec2(0.0, 0.5 * (shB + 0.06 + shT - 0.08)), vec2(shW2 - 0.17, 0.5 * (shT - 0.08 - shB - 0.06)), 0.05);
    }
  }
  bool isWindow = region >= 1 && region <= 3 && win < 0.0;

  if (uPass == 1) {
    if (!isWindow || vBroken > 0.5) discard;
    vec3 nn = gl_FrontFacing ? n : -n;
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(nn, V), 0.0), 5.0);
    float sh = shadowAt(vPos, nn);
    vec3 H = normalize(V + uSun);
    vec3 lspec;
    lampLight(vPos, nn, V, 300.0, 0.0, lspec);
    vec3 col = vec3(0.004, 0.005, 0.006) + skyEnv(reflect(-V, nn)) * (0.07 + 0.9 * fres)
             + pow(max(dot(nn, H), 0.0), 300.0) * sh * 5.0 * kSunSpec + lspec * 2.0;
    oCol = finishColor(col, vPos, clamp((uVehicle == 1 ? 0.62 : uVehicle >= 2 ? 0.34 : 0.22) + 0.65 * fres, 0.0, 0.94));
    return;
  }
  if (isWindow) discard;  // the glass pass draws it (or it has shattered: a hole)

  if (!gl_FrontFacing) {  // the inside of the shell, seen through a window or a tear
    oCol = finishColor(pow(vec3(0.11, 0.105, 0.1), vec3(2.2)) * ambientAt(-n) * 1.2, vPos, 1.0);
    return;
  }

  // ---- Surface detail: 0 paint, 1 black plastic, 2 chrome, 3 headlamp, 4 tail lamp,
  //      5 dark matte, 6 number plate, 7 grille ----
  vec3 base = uPaint;
  int kind = 0;
  if (region == 7) { base = vec3(0.07); kind = 5; }
  else if (region == 8) { base = vec3(0.025); kind = 5; }
  else if (region == 9) { base = vec3(0.06) * (0.9 + 0.2 * step(0.5, fract(j * 6.0))); kind = 5; }
  else if (region >= 1 && region <= 3) {
    if (win < 0.08) { base = vec3(0.025); kind = 1; }  // rubber seal around the glass
  } else if (region == 5 || region == 6) {
    bool front = region == 5;
    float lampK = front ? uBody.z : uBody.w;
    if (k < 0.32) { base = vec3(0.035); kind = 1; }  // lower bumper / diffuser
    int lampStyle = int(uStyle.x + 0.5), grille = int(uStyle.y + 0.5);
    float lamp = min(rbox(vec2(j, k), vec2(0.62, lampK), vec2(0.44, 0.2), 0.12),
                     rbox(vec2(j, k), vec2(W - 0.62, lampK), vec2(0.44, 0.2), 0.12));
    if (front && lampStyle == 1) {  // round headlamps
      vec2 q = vec2(min(j, W - j) - 0.55, (k - lampK - 0.05) / uStyle.z);
      lamp = length(q) - 0.25;
    } else if (front && lampStyle == 2) {  // slim lamps sweeping into the corners
      lamp = min(rbox(vec2(j, k), vec2(0.56, lampK + 0.14), vec2(0.5, 0.075), 0.07),
                 rbox(vec2(j, k), vec2(W - 0.56, lampK + 0.14), vec2(0.5, 0.075), 0.07));
    } else if (!front && lampStyle == 2) {  // thin full-width tail lamp bar
      lamp = rbox(vec2(j, k), vec2(0.5 * W, lampK + 0.12), vec2(0.5 * W - 0.12, 0.06), 0.05);
    }
    float plate = rbox(vec2(j, k), vec2(0.5 * W, 0.62), vec2(0.6, 0.13), 0.03);
    if (front) {
      float g = grille == 1 ? rbox(vec2(j, k), vec2(0.5 * W, lampK - 0.02), vec2(0.5 * W - 0.22, 0.3), 0.06)
              : grille == 2 ? rbox(vec2(j, k), vec2(0.5 * W, 0.42), vec2(0.5 * W - 0.35, 0.2), 0.1)
                            : rbox(vec2(j, k), vec2(0.5 * W, lampK - 0.05), vec2(0.5 * W - 1.18, 0.22), 0.08);
      if (g < 0.0) kind = 7;
    }
    if (plate < 0.0) kind = 6;
    if (lamp < 0.0) kind = front ? 3 : 4;
    else if (lamp < 0.05) { base = vec3(0.04); kind = 1; }  // lamp bezel
  } else if (region == 0) {
    if (k < 0.16) { base = vec3(0.035); kind = 1; }  // side skirt
    else if (k < uTrim.w) {  // two-tone lower body, with a dark pinstripe on top
      base = mix(uTrim.rgb, vec3(0.03), lineAt(k, uTrim.w - 0.02, 0.012, max(fwidth(k), 1e-4)));
    }
    // Door gaps line up with the physical doors: two cells from the front of the cabin,
    // and a rear door behind it on long cabins.
    float doorFront = ci0, doorRear = ci0 + dcells;
    if (k > 0.22 && k < ck0 - 0.02) {
      float seam = max(lineAt(i, doorFront, 0.012, fwI), lineAt(i, doorRear, 0.012, fwI));
      if (rearDoor) seam = max(seam, lineAt(i, doorRear + dcells, 0.012, fwI));
      base = mix(base, vec3(0.02), seam * 0.85);
      if (rbox(vec2(i, k), vec2(doorRear - 0.45, ck0 - 0.3), vec2(0.17, 0.04), 0.03) < 0.0) kind = 2;  // handle
      if (rearDoor && rbox(vec2(i, k), vec2(doorRear + dcells - 0.45, ck0 - 0.3), vec2(0.17, 0.04), 0.03) < 0.0) kind = 2;
    }
  } else if (region == 10) {
    float seam = max(lineAt(i, ci0, 0.012, fwI), lineAt(i, ci1, 0.012, fwI));  // hood and trunk gaps
    base = mix(base, vec3(0.02), seam * 0.8);
  }

  // Model-specific trim, lamps and stamped details. Coordinates are skinned along
  // with the panel, so a lamp or seam remains on its own deformed piece of bodywork.
  if (uVehicle == 0) {
    if (region == 0 && k > 0.78 && k < 0.89) { base = vec3(0.035); kind = 1; }
    if ((region == 5 || region == 6) && k < 0.82) { base = vec3(0.045); kind = 1; }
    if (region == 5) {
      float edge = min(j, W-j);
      if (k > 1.15 && k < 1.72 && edge < 1.13) kind = edge < 0.29 ? 8 : 3;
      if (k > 1.31 && k < 1.51 && edge > 1.15) kind = 7;
      if (k > 0.33 && k < 0.51 && edge < 0.9 && edge > 0.35) kind = 8;
    }
    if (region == 6 && k > 1.05 && k < 1.62) kind = min(j, W-j) < 1.12 ? 4 : 1;
  } else if (uVehicle == 1) {
    if (region == 5) { // the actual lamps are swept lenses on the fenders
      kind = 0; base = uPaint;
      float mouth = rbox(vec2(j,k),vec2(2.0,0.65),vec2(1.72,0.43),0.22);
      if (mouth < 0.0) kind = 7;
      if (k < 0.16) { kind = 5; base = vec3(0.025); }
    }
    if (region == 0) {
      // Swept side intake: a sloping forward edge and a tall rear opening.
      float intake = max(abs(k-1.25)-0.63, max(i-(ci1+0.16), ci1-1.95+0.8*(1.88-k)-i));
      if (intake < 0.0) kind = 7;
      if (k < 0.32) { base = vec3(0.035); kind = 5; }
    }
    if (region == 10 && i < ci0 && abs(j-W*0.5) < 0.40 && i > 1.55 && i < 2.55) kind = 7;
    if (region == 10 && i > ci1 + 0.3 && abs(j-W*0.5) < 0.9) kind = 7;
    if (region == 6) {
      kind = 7;
      float edge = min(j,W-j);
      float tail = rbox(vec2(edge,k),vec2(0.67,1.61),vec2(0.47,0.19),0.10);
      if (abs(tail) < 0.038) kind = 4;
      if (k < 0.60) { kind = 5; base = vec3(0.025); }
    }
  } else if (uVehicle == 2) {
    if (region == 4 || (region == 1 && i > ci0 + dcells)) { base = vec3(0.07); kind = 1; }
    if (region == 5) {
      float edge = min(j,W-j);
      vec2 q=vec2(edge-0.62,(k-uBody.z)/uStyle.z);
      float r=length(q);
      if (k > 1.13 && k < 1.94) kind = 7;
      if (r < 0.28) { kind = 1; base = vec3(0.025); }
      if (abs(r-0.23)<0.032 || (abs(q.y)<0.031 && edge>0.60 && edge<1.36)) kind = 3;
    }
    if (region == 6 && min(j,W-j)<0.57 && k>0.95 && k<2.35) kind = 4;
  } else if (uVehicle == 3) {
    if ((region == 5 || region == 6) && k < uTrim.w) base = uTrim.rgb;
    if (region == 5) {
      float edge=min(j,W-j);
      if (edge>1.1 && k>1.1 && k<1.83) {
        kind=7;
        if (fract(k*7.0)<0.17 || k<1.16 || k>1.77) kind=2;
      }
      if (edge<1.07 && k>1.1 && k<1.86) kind=edge<0.30?8:3;
    }
    if (region == 6 && min(j,W-j)<0.55 && k>0.98 && k<2.20) kind=4;
  }

  // Sculpted shell: the real truck's lamps, grille, seams and trim, in meters.
  if (uShell == 1) {
    base = uPaint;
    kind = 0;
    float fw = max(fwidth(P.x) + fwidth(P.y) + fwidth(P.z), 1e-4);
    float az = abs(P.z);
    float lampY = shHood - 0.135;                         // headlamp center height
    float tailLo = shB - 0.36, tailHi = shB + 0.10;       // tail lamps up the rear corners
    if (region == 7 || region == 8) {
      base = vec3(region == 8 ? 0.025 : 0.06);
      kind = 5;
    } else if (region == 0) {
      if (P.y < shF + 0.06) { base = vec3(0.035); kind = 1; }  // rocker
      else if (P.y < shTrim) base = mix(uTrim.rgb, vec3(0.03), lineAt(P.y, shTrim - 0.012, 0.004, fw));
      if (P.y > shF + 0.02 && P.y < shB + 0.01) {
        float seam = max(lineAt(P.x, shXa, 0.004, fw), max(lineAt(P.x, shXB, 0.004, fw), lineAt(P.x, shXC, 0.004, fw)));
        base = mix(base, vec3(0.012), seam * 0.9);
        // Door handles: a dark pocket with the handle across it.
        float h1 = rbox(P.xy, vec2(shXB + 0.15, shB - 0.08), vec2(0.075, 0.022), 0.012);
        float h2 = rbox(P.xy, vec2(shXC + 0.14, shB - 0.08), vec2(0.07, 0.022), 0.012);
        float h = min(h1, h2);
        if (h < 0.0) { base = h < -0.008 ? uPaint * 0.8 : vec3(0.02); kind = h < -0.008 ? 0 : 1; }
      }
      // Fuel door on the left rear quarter.
      if (P.z < 0.0 && abs(rbox(P.xy, vec2(shXr + 0.78, shB - 0.15), vec2(0.09, 0.075), 0.02)) < 0.0035) base = vec3(0.015);
      // Headlamp and amber marker wrapping round the front corners.
      if (P.x > shXf - 0.09 && abs(P.y - lampY) < 0.075) kind = 3;
      else if (P.x > shXf - 0.17 && abs(P.y - (lampY - 0.04)) < 0.03) kind = 8;
      // Tail lamps wrapping round the rear corners.
      if (P.x < shXr + 0.09 && P.y > tailLo && P.y < tailHi) kind = 4;
    } else if (region == 1) {
      // Window frames and the B and C pillars are black; the A pillar, roof rail and D
      // pillar are body color.
      if (P.x > shXD - 0.02 && P.x < shAx - 0.045 && P.y > shB + 0.008 && P.y < shYhi + 0.035) { base = vec3(0.03); kind = 1; }
      if (win < 0.012) { base = vec3(0.018); kind = 1; }  // rubber seal
      if (P.x < shXr + 0.09 && P.y < tailHi) kind = 4;
    } else if (region == 2 || region == 3) {
      if (win < 0.012) { base = vec3(0.018); kind = 1; }
      if (region == 2 && P.y < shB + 0.05) { base = vec3(0.03); kind = 1; }  // cowl
      if (region == 3 && az > shLiftZ - 0.004 && az < shLiftZ + 0.004) base = vec3(0.012);
    } else if (region == 10) {
      float seam = max(lineAt(P.x, shXf - 0.10, 0.004, fw), lineAt(az, shW2 - 0.06, 0.004, fw));
      base = mix(base, vec3(0.012), seam * 0.9);
      if (P.x < shXa + 0.05) { base = vec3(0.03); kind = 1; }  // cowl vent
    } else if (region == 5) {
      // 2001-02 nose: big headlamps with amber corners, a slatted grille with the emblem,
      // bumper below (hidden behind the steel bumper on this truck).
      base = uPaint;
      float lamp = rbox(vec2(az, P.y), vec2(0.535, lampY), vec2(0.235, 0.072), 0.022);
      float grille = rbox(vec2(az, P.y), vec2(0.0, lampY - 0.005), vec2(0.265, 0.082), 0.018);
      if (P.y < lampY - 0.11) { base = vec3(0.03); kind = 1; }
      if (grille < 0.0) {
        kind = 7;
        float e = length(vec2(P.z / 0.075, (P.y - lampY) / 0.048));
        if (e > 0.78 && e < 1.0) kind = 2;  // emblem ring
        if (grille > -0.012) { base = vec3(0.02); kind = 1; }
      }
      if (lamp < 0.0) kind = (az > 0.62 && P.y < lampY - 0.02) ? 8 : 3;
      else if (lamp < 0.012) { base = vec3(0.02); kind = 1; }
    } else if (region == 6) {
      // Liftgate: seams, plate, chrome garnish; tail lamps outboard of it.
      if (P.y < shLiftY) { base = vec3(0.03); kind = 1; }
      float seam = P.y > shLiftY ? lineAt(az, shLiftZ, 0.004, fw) : 0.0;
      seam = max(seam, az < shLiftZ ? lineAt(P.y, shLiftY, 0.004, fw) : 0.0);
      base = mix(base, vec3(0.012), seam * 0.9);
      float plate = rbox(P.zy, vec2(0.0, shLiftY + 0.17), vec2(0.155, 0.075), 0.01);
      if (plate < 0.0) {
        float chars = step(abs(P.y - (shLiftY + 0.17)), 0.03) * step(0.45, fract(P.z * 22.0)) * step(abs(P.z), 0.12);
        base = mix(vec3(0.88, 0.88, 0.84), vec3(0.06), chars);
        kind = 1;
      }
      if (rbox(P.zy, vec2(0.0, shLiftY + 0.31), vec2(0.3, 0.022), 0.01) < 0.0) kind = 2;
      if (az > shLiftZ + 0.006 && P.y > tailLo && P.y < tailHi) kind = P.y < tailLo + 0.07 ? 3 : 4;
    }
    // Feed the lamp and grille shading the coordinates it expects.
    if (kind == 7) k = P.y * 2.2;
    if (kind == 3) {
      j = (P.z / max(0.1, shW2) * 0.5 + 0.5) * W;
      k = uBody.z + (P.y - lampY) / 0.15;
    }
  } else if (uShell == 2) {
    // 2021 Bronco: black hardtop and B/C/D pillars, exposed door hinges, a full-width
    // grille with round lamps and C-shaped running lights, BRONCO across the middle,
    // vertical tail lamps on the corners, black steel bumpers.
    base = uPaint;
    kind = 0;
    float fw = max(fwidth(P.x) + fwidth(P.y) + fwidth(P.z), 1e-4);
    float az = abs(P.z);
    float gBot = shBump + 0.03, gTop = shHood - 0.03, lampY = 0.5 * (gBot + gTop), lampZ = shW2 - 0.25;
    if (region == 7 || region == 8) {
      base = vec3(region == 8 ? 0.025 : 0.05);
      kind = 5;
    } else if (region == 0) {
      if (P.y < shF + 0.05) { base = vec3(0.035); kind = 1; }  // rocker
      if (P.y > shF + 0.02 && P.y < shB + 0.01) {
        float seam = max(lineAt(P.x, shXE, 0.004, fw), max(lineAt(P.x, shXB, 0.004, fw), lineAt(P.x, shXC, 0.004, fw)));
        base = mix(base, vec3(0.012), seam * 0.9);
        // Hinges on the doors' front edges; flush handles at the back.
        for (int d = 0; d < 2; d++) {
          float hx = d == 0 ? shXE - 0.035 : shXB - 0.035;
          for (int h = 0; h < 2; h++)
            if (rbox(P.xy, vec2(hx, h == 0 ? shB - 0.16 : shF + 0.24), vec2(0.03, 0.05), 0.008) < 0.0) { base = vec3(0.03); kind = 1; }
          float hx2 = d == 0 ? shXB + 0.12 : shXC + 0.12;
          if (rbox(P.xy, vec2(hx2, shB - 0.09), vec2(0.07, 0.018), 0.01) < 0.0) { base = vec3(0.03); kind = 1; }
        }
      }
      if (P.x < shXr + 0.06 && P.y > shB - 0.40 && P.y < shB - 0.02) kind = P.y < shB - 0.31 ? 3 : 4;  // tail lamp wrap
      if (P.x > shXf - 0.05 && abs(P.y - (lampY + 0.05)) < 0.03) kind = 8;                          // side marker
    } else if (region == 1) {
      if (P.x < shAx - 0.05 && P.y > shB + 0.01) { base = vec3(0.035); kind = 1; }  // the hardtop's pillars
      if (win < 0.012) { base = vec3(0.02); kind = 1; }
      if (P.x < shXr + 0.06 && P.y < shB + 0.01) kind = 4;
    } else if (region == 4) {
      base = vec3(0.04) * (0.9 + 0.2 * vnoise(P.xz * 30.0));  // textured black hardtop
      kind = 5;
    } else if (region == 2 || region == 3) {
      if (win < 0.012) { base = vec3(0.02); kind = 1; }
      if (region == 2 && P.y < shB + 0.05) { base = vec3(0.03); kind = 1; }  // cowl
      if (region == 3) { base = vec3(0.035); kind = 1; }                     // hardtop round the back glass
      if (region == 3 && win < 0.0) kind = 0;
    } else if (region == 10) {
      float seam = max(lineAt(P.x, shXf - 0.10, 0.004, fw), lineAt(az, shW2 - 0.03, 0.004, fw));
      base = mix(base, vec3(0.012), seam * 0.9);
      // Trail sights: the black tie-down bumps on the front corners of the fenders.
      if (P.x > shXf - 0.32 && P.x < shXf - 0.12 && az > shW2 - 0.12) { base = vec3(0.03); kind = 1; }
      if (P.x < shXa + 0.06) { base = vec3(0.03); kind = 1; }  // cowl vent
    } else if (region == 5) {
      if (P.y < gBot) { base = vec3(0.04) * (0.9 + 0.2 * vnoise(P.zy * 40.0)); kind = 5; }  // steel bumper
      if (P.y > gBot && P.y < gTop && az < shW2 - 0.05) {
        kind = 7;
        // BRONCO: six letter blocks across the middle.
        float lx = (P.z + 0.33) / 0.11;
        if (abs(P.y - lampY) < 0.035 && lx > 0.0 && lx < 6.0 && fract(lx) > 0.18 && fract(lx) < 0.82) { base = vec3(0.85); kind = 2; }
        float r = length(vec2(az - lampZ, P.y - lampY));
        if (r < 0.06) kind = 3;                                              // LED projector
        else if (r < 0.105) { base = vec3(0.6); kind = 2; }                  // chrome bowl
        else if (r < 0.132 && (az - lampZ) > -0.07) kind = 3;               // C-shaped running light round the outside
        else if (r < 0.14) { base = vec3(0.02); kind = 1; }
      }
      float plate = rbox(P.zy, vec2(0.0, shF + 0.15), vec2(0.15, 0.07), 0.01);
      if (plate < 0.0) { base = vec3(0.88, 0.88, 0.84); kind = 1; }
    } else if (region == 6) {
      if (P.y < shLiftY) { base = vec3(0.04) * (0.9 + 0.2 * vnoise(P.zy * 40.0)); kind = 5; }
      float seam = P.y > shLiftY ? lineAt(az, shLiftZ, 0.004, fw) : 0.0;
      seam = max(seam, az < shLiftZ ? lineAt(P.y, shLiftY, 0.004, fw) : 0.0);
      base = mix(base, vec3(0.012), seam * 0.9);
      if (az > shLiftZ + 0.006 && P.y > shB - 0.40 && P.y < shB - 0.02) kind = P.y < shB - 0.31 ? 3 : 4;
      if (P.y > shGateTop - 0.03) { base = vec3(0.035); kind = 1; }  // hardtop over the gate
      // Hinges on the gate's right edge (it swings from the passenger side).
      if (P.z > shLiftZ - 0.07 && P.z < shLiftZ - 0.01 && (abs(P.y - (shLiftY + 0.2)) < 0.05 || abs(P.y - (shB - 0.05)) < 0.05)) { base = vec3(0.03); kind = 1; }
    }
    if (kind == 7) k = P.y * 2.2;
    if (kind == 3) {
      j = (P.z > 0.0 ? W - 0.55 : 0.55) + (P.z > 0.0 ? -(az - lampZ) : (az - lampZ)) * 3.0;
      k = uBody.z + (P.y - lampY) * 3.0 / 2.2;
    }
  } else if (uShell == 3) {
    // 1990 EF Civic Si hatch: flush wide headlamps with amber corners, a slim grille
    // slot, body-color bumpers with black rub strips, a black B pillar, a moonroof, and
    // a full-width tail lamp band across the hatch.
    base = uPaint;
    kind = 0;
    float fw = max(fwidth(P.x) + fwidth(P.y) + fwidth(P.z), 1e-4);
    float az = abs(P.z);
    float lampY = shHood - 0.065, lampZ = 0.52;
    float strip = shBump - 0.07;  // rub strip height round the car
    if (region == 7 || region == 8) {
      base = vec3(region == 8 ? 0.025 : 0.05);
      kind = 5;
    } else if (region == 0) {
      if (P.y < shF + 0.04) { base = vec3(0.04); kind = 1; }
      if (P.y > shF + 0.02 && P.y < shB + 0.01) {
        float seam = max(lineAt(P.x, shXE, 0.004, fw), lineAt(P.x, shXB, 0.004, fw));
        base = mix(base, vec3(0.012), seam * 0.9);
        if (rbox(P.xy, vec2(shXB + 0.11, shB - 0.06), vec2(0.06, 0.016), 0.01) < 0.0) { base = vec3(0.03); kind = 1; }  // handle
      }
      if (P.z > 0.0 && abs(rbox(P.xy, vec2(shXr + 0.55, shB - 0.12), vec2(0.075, 0.06), 0.02)) < 0.0035) base = vec3(0.015);  // fuel door
      if (P.x > shXf - 0.10 && abs(P.y - lampY) < 0.045) kind = 8;              // corner lamp wrap
      if (P.x < shXr + 0.07 && P.y > shLiftY + 0.16 && P.y < shB - 0.02) kind = 4;  // tail lamp wrap
    } else if (region == 1) {
      if (win < 0.014) { base = vec3(0.02); kind = 1; }                         // frames
      if (P.x > shXB - 0.07 && P.x < shXB + 0.05 && P.y > shB) { base = vec3(0.03); kind = 1; }  // black B pillar
    } else if (region == 4) {
      // Power moonroof: tinted glass in a black surround.
      float roof = rbox(P.xz, vec2(-0.10, 0.0), vec2(0.31, 0.34), 0.04);
      if (roof < 0.0) { base = roof < -0.02 ? vec3(0.02, 0.025, 0.03) : vec3(0.02); kind = roof < -0.02 ? 2 : 1; }
    } else if (region == 2 || region == 3) {
      if (win < 0.014) { base = vec3(0.02); kind = 1; }
      if (region == 2 && P.y < shB + 0.04) { base = vec3(0.03); kind = 1; }
    } else if (region == 10) {
      float seam = max(lineAt(P.x, shXf - 0.05, 0.004, fw), lineAt(az, shW2 - 0.05, 0.004, fw));
      base = mix(base, vec3(0.012), seam * 0.9);
      if (P.x < shXa + 0.05) { base = vec3(0.03); kind = 1; }  // cowl
    } else if (region == 5) {
      float lamp = rbox(vec2(az, P.y), vec2(lampZ, lampY), vec2(0.27, 0.045), 0.012);
      if (lamp < 0.0) kind = az > 0.70 ? 8 : 3;
      else if (lamp < 0.01) { base = vec3(0.02); kind = 1; }
      float grille = rbox(vec2(az, P.y), vec2(0.0, lampY), vec2(0.24, 0.03), 0.01);
      if (grille < 0.0) {
        kind = 7;
        if (length(vec2(P.z / 0.05, (P.y - lampY) / 0.022)) < 1.0) kind = 2;  // the H
      }
      if (rbox(vec2(az, P.y), vec2(0.0, shF + 0.09), vec2(0.42, 0.04), 0.02) < 0.0) { base = vec3(0.02); kind = 5; }  // intake
      if (rbox(P.zy, vec2(0.0, strip - 0.10), vec2(0.16, 0.055), 0.008) < 0.0) { base = vec3(0.88, 0.88, 0.84); kind = 1; }  // plate
    } else if (region == 6) {
      // Lower hatch face: the full-width lamp band, a red reflector between the lamps.
      if (P.y > shLiftY + 0.16 && P.y < shB - 0.02) kind = az > 0.42 ? 4 : 4;
      if (P.y > shLiftY + 0.16 && P.y < shLiftY + 0.21 && az > 0.42) kind = 3;  // reverse lamps
      if (P.y > shLiftY + 0.16 && P.y < shB - 0.02 && az < 0.42) { base = vec3(0.35, 0.02, 0.02); kind = 1; }
      float seam = lineAt(P.y, shLiftY, 0.004, fw);
      base = mix(base, vec3(0.012), seam * 0.9);
      if (rbox(P.zy, vec2(0.0, strip - 0.10), vec2(0.16, 0.055), 0.008) < 0.0) { base = vec3(0.88, 0.88, 0.84); kind = 1; }
    }
    if (kind == 7) k = P.y * 2.2;
    if (kind == 3) {
      j = (P.z > 0.0 ? W - 0.62 : 0.62) + (P.z > 0.0 ? -(az - lampZ) : (az - lampZ)) * 2.0;
      k = uBody.z + (P.y - lampY) * 2.0 / 2.2;
    }
  } else if (uShell == 4) {
    // 3rd-gen Tacoma TRD Off-Road: swept headlamps either side of a big black hex grille,
    // a black lower valance, black window frames and pillars, a stamped tailgate between
    // tall tail lamps, a chrome step bumper, the bed lined dark.
    base = uPaint;
    kind = 0;
    float fw = max(fwidth(P.x) + fwidth(P.y) + fwidth(P.z), 1e-4);
    float az = abs(P.z);
    float lampY = shHood - 0.10;                        // headlamp center height
    float tailLo = shB - 0.36, tailHi = shB - 0.02;      // tail lamps up the bed's corners
    float xCab = shXrr;
    if (region == 7 || region == 8) {
      base = vec3(region == 8 ? 0.025 : 0.06);
      kind = 5;
    } else if (region == 9 || region == 11) {
      base = vec3(0.045, 0.045, 0.048) * (0.9 + 0.2 * step(0.5, fract(P.z * 7.0)));  // bedliner, ribbed
      kind = 5;
    } else if (region == 0) {
      if (P.y < shF + 0.07) { base = vec3(0.035); kind = 1; }  // rocker
      if (P.y > shF + 0.02 && P.y < shB + 0.01 && P.x > xCab) {
        float seam = max(lineAt(P.x, shXE, 0.004, fw), max(lineAt(P.x, shXB, 0.004, fw), lineAt(P.x, shXC, 0.004, fw)));
        base = mix(base, vec3(0.012), seam * 0.9);
        float h1 = rbox(P.xy, vec2(shXB + 0.15, shB - 0.09), vec2(0.075, 0.022), 0.012);
        float h2 = rbox(P.xy, vec2(shXC + 0.14, shB - 0.09), vec2(0.07, 0.022), 0.012);
        float h = min(h1, h2);
        if (h < 0.0) { base = h < -0.008 ? uPaint * 0.8 : vec3(0.02); kind = h < -0.008 ? 0 : 1; }
      }
      // The gap between the cab and the bed.
      if (P.y > shF + 0.04) base = mix(base, vec3(0.01), lineAt(P.x, xCab - 0.03, 0.006, fw));
      // Fuel door on the bed's left side.
      if (P.z < 0.0 && abs(rbox(P.xy, vec2(xCab - 0.42, shB - 0.17), vec2(0.09, 0.075), 0.02)) < 0.0035) base = vec3(0.015);
      if (P.x > shXf - 0.12 && abs(P.y - lampY) < 0.055) kind = 3;  // headlamps round the corners
      if (P.x < shXr + 0.06 && P.y > tailLo && P.y < tailHi) kind = 4;  // tail lamps round the corners
    } else if (region == 1) {
      if (P.x > xCab + 0.02 && P.x < shAx - 0.045 && P.y > shB + 0.008 && P.y < shYhi + 0.035) { base = vec3(0.03); kind = 1; }
      if (win < 0.012) { base = vec3(0.018); kind = 1; }
    } else if (region == 2 || region == 3) {
      if (win < 0.012) { base = vec3(0.018); kind = 1; }
      if (region == 2 && P.y < shB + 0.05) { base = vec3(0.03); kind = 1; }  // cowl
      if (region == 3 && win < 0.0 && az < 0.012) base = vec3(0.02);  // the slider's frame
    } else if (region == 10) {
      if (P.x > shXa) {
        float seam = max(lineAt(P.x, shXf - 0.10, 0.004, fw), lineAt(az, shW2 - 0.07, 0.004, fw));
        base = mix(base, vec3(0.012), seam * 0.9);
        if (P.x < shXa + 0.05) { base = vec3(0.03); kind = 1; }  // cowl vent
        // The hood scoop (a TRD Off-Road styling bulge): a darker inlet.
        if (rbox(vec2(P.x, az), vec2(shXa + 0.55, 0.0), vec2(0.13, 0.20), 0.05) < 0.0 && P.x > shXa + 0.62) { base = vec3(0.02); kind = 1; }
      } else {
        base = vec3(0.03); kind = 1;  // the bed rail caps: black plastic
      }
    } else if (region == 5) {
      base = uPaint;
      float lamp = rbox(vec2(az, P.y), vec2(0.60, lampY), vec2(0.22, 0.05), 0.03);
      float grille = rbox(vec2(az, P.y), vec2(0.0, lampY - 0.07), vec2(0.40, 0.15), 0.05);
      if (P.y < lampY - 0.26) { base = vec3(0.03); kind = 1; }  // lower valance
      if (grille < 0.0) {
        kind = 7;
        base = vec3(0.02);
        vec2 hx = vec2(P.z * 14.0, P.y * 16.0);
        float honey = abs(fract(hx.x + 0.5 * step(0.5, fract(hx.y * 0.5))) - 0.5) + abs(fract(hx.y) - 0.5);
        if (honey < 0.18) base = vec3(0.06);
        float e = length(vec2(P.z / 0.09, (P.y - (lampY - 0.03)) / 0.06));
        if (e > 0.75 && e < 1.0) kind = 2;  // the emblem
        if (grille > -0.014) { base = vec3(0.025); kind = 1; }
      }
      if (lamp < 0.0) kind = (az > 0.74) ? 8 : 3;
      else if (lamp < 0.012) { base = vec3(0.02); kind = 1; }
      // Fog lamps in the valance.
      if (length(vec2(az - 0.68, P.y - (lampY - 0.33))) < 0.045) kind = 3;
    } else if (region == 6) {
      // Tailgate: its seams, the stamped name, a handle; tail lamps outboard; the step bumper.
      if (P.y < shLiftY) { base = vec3(0.55, 0.56, 0.58); kind = 2; }  // chrome step bumper
      float seam = P.y > shLiftY ? lineAt(az, shLiftZ, 0.004, fw) : 0.0;
      seam = max(seam, az < shLiftZ ? lineAt(P.y, shLiftY, 0.004, fw) : 0.0);
      base = mix(base, vec3(0.012), seam * 0.9);
      if (rbox(P.zy, vec2(0.0, shB - 0.06), vec2(0.13, 0.03), 0.012) < 0.0) { base = vec3(0.02); kind = 1; }  // handle
      float word = step(abs(P.y - (shB - 0.20)), 0.035) * step(abs(P.z), 0.42) * step(0.55, fract(P.z * 9.0));
      base = mix(base, uPaint * 0.82, word);  // TACOMA stamped across it
      if (az > shLiftZ + 0.006 && P.y > tailLo && P.y < tailHi) kind = P.y < tailLo + 0.06 ? 3 : 4;
    }
    if (kind == 7) k = P.y * 2.2;
    if (kind == 3) {
      j = (P.z / max(0.1, shW2) * 0.5 + 0.5) * W;
      k = uBody.z + (P.y - lampY) / 0.15;
    }
  }

  if (uShell == 5) {
    // 1972 K5 Blazer: a full-width egg-crate grille between round headlamps in square chrome
    // bezels, amber parking lamps under them, the bowtie in the middle; chrome bumpers; a
    // crease with a chrome molding down the side; side markers; the white removable top;
    // vertical tail lamps in the rear corners; CHEVROLET across the tailgate.
    base = uPaint;
    kind = 0;
    float fw = max(fwidth(P.x) + fwidth(P.y) + fwidth(P.z), 1e-4);
    float az = abs(P.z);
    vec3 top = uTrim.rgb;                                   // the hardtop's color (white)
    // (Its own bumper line: the chrome bars sit at the frame, ~0.53-0.70 m up; the physics
    // bumper panel here reaches much higher.)
    float bumpLo = shF - 0.02, bumpHi = shF + 0.16;
    float gBot = bumpHi + 0.03, gTop = shHood - 0.04;       // grille opening
    float lampY = 0.5 * (gBot + gTop) + 0.02, lampZ = shW2 - 0.22;
    float crease = shB - 0.17;                              // the side's body line
    if (region == 7 || region == 8) {
      base = vec3(region == 8 ? 0.025 : 0.05);
      kind = 5;
    } else if (region == 0) {
      if (P.y < shF + 0.04) { base = vec3(0.04); kind = 1; }  // rocker
      // The crease: a light edge over a shadow, with a slim chrome molding on it.
      base = mix(base, base * 1.18, lineAt(P.y, crease + 0.012, 0.006, fw));
      base = mix(base, base * 0.6, lineAt(P.y, crease - 0.006, 0.005, fw));
      if (abs(P.y - crease) < 0.008 && P.x > shXr + 0.12 && P.x < shXf - 0.12) { base = vec3(0.75); kind = 2; }
      if (P.y > shF + 0.02 && P.y < shB + 0.01) {
        float seam = max(lineAt(P.x, shXE, 0.004, fw), lineAt(P.x, shXB, 0.004, fw));
        base = mix(base, vec3(0.012), seam * 0.9);
        // Chrome push-button handle near the door's back edge.
        if (rbox(P.xy, vec2(shXB + 0.12, shB - 0.07), vec2(0.07, 0.015), 0.008) < 0.0) { base = vec3(0.8); kind = 2; }
      }
      // BLAZER on the front fender, above the crease behind the wheel.
      if (P.x > shXE + 0.12 && P.x < shXE + 0.48 && abs(P.y - (crease + 0.07)) < 0.018 && fract((P.x - shXE) * 16.0) > 0.3) { base = vec3(0.8); kind = 2; }
      // Side markers: amber at the front corners, red at the back.
      if (P.x > shXf - 0.16 && P.x < shXf - 0.06 && abs(P.y - (crease - 0.08)) < 0.025) kind = 8;
      if (P.x < shXr + 0.16 && P.x > shXr + 0.06 && abs(P.y - (crease - 0.08)) < 0.025) kind = 4;
      // Tail lamps wrapping onto the sides at the rear corners.
      if (P.x < shXr + 0.03 && P.y > bumpHi + 0.06 && P.y < bumpHi + 0.34) kind = 4;
      // The top's lower edge where it bolts on behind the doors.
      if (P.x < shXB - 0.01 && P.y > shB - 0.02) base = top;
    } else if (region == 1) {
      // Body color ahead of the B pillar (the door frame chrome), the white top behind it.
      if (P.x < shXB + 0.03) base = top;
      if (win < 0.014) { base = P.x > shXB + 0.03 ? vec3(0.75) : vec3(0.025); kind = P.x > shXB + 0.03 ? 2 : 1; }  // chrome frames, rubber on the top
      if (abs(P.x - (shAx - 0.30)) < 0.008 && P.y > shYlo && P.y < shYhi) { base = vec3(0.7); kind = 2; }  // vent wing post
    } else if (region == 4) {
      base = top;  // the removable hardtop, the whole roof
      if (lineAt(P.x, shXrf - 0.03, 0.004, fw) > 0.5) base = vec3(0.02);  // its seam at the windshield header
    } else if (region == 2) {
      if (win < 0.014) { base = vec3(0.7); kind = 2; }  // chrome reveal molding
      if (P.y < shB + 0.05) { base = vec3(0.03); kind = 1; }  // cowl
    } else if (region == 3) {
      base = top;
      if (win < 0.014) { base = vec3(0.02); kind = 1; }
    } else if (region == 10) {
      float seam = max(lineAt(P.x, shXf - 0.08, 0.004, fw), lineAt(az, shW2 - 0.05, 0.004, fw));
      base = mix(base, vec3(0.012), seam * 0.9);
      base = mix(base, base * 1.12, lineAt(az, 0.30, 0.01, fw) * step(shXa + 0.15, P.x));  // the hood's two raised ribs
      if (P.x < shXa + 0.06) { base = vec3(0.03); kind = 1; }  // cowl vent
    } else if (region == 5) {
      if (P.y < bumpHi && P.y > bumpLo) { base = vec3(0.82, 0.82, 0.84); kind = 2; }  // chrome front bumper
      else if (P.y < gBot) { base = P.y < bumpLo ? vec3(0.03) : uPaint * 0.9; kind = P.y < bumpLo ? 1 : 0; }  // valance; under it, dark
      if (P.y > gBot && P.y < gTop && az < shW2 - 0.04) {
        base = vec3(0.78); kind = 2;  // chrome surround
        if (P.y > gBot + 0.025 && P.y < gTop - 0.025 && az < shW2 - 0.07) {
          // Egg-crate: wide cells, a strong horizontal bar through the middle.
          kind = 7;
          base = vec3(0.025);
          float cx = fract(P.z * 9.0), cy = fract((P.y - gBot) * 14.0);
          if (cx < 0.12 || cy < 0.18) { base = vec3(0.62); kind = 2; }
          if (abs(P.y - lampY) < 0.012) { base = vec3(0.8); kind = 2; }
        }
        // Bowtie in the middle of the bar.
        if (abs(P.y - lampY) < 0.03 - 0.25 * max(0.0, az - 0.035) && az < 0.07) { base = vec3(0.85, 0.68, 0.25); kind = 2; }
        // Round headlamps in square chrome bezels.
        vec2 q = vec2(az - lampZ, P.y - lampY);
        float bez = max(abs(q.x), abs(q.y)) - 0.105;
        if (bez < 0.0) { base = vec3(0.8); kind = 2; }
        if (length(q) < 0.085) kind = 3;
        else if (length(q) < 0.095) { base = vec3(0.6); kind = 2; }
        // Amber parking lamps below them.
        if (rbox(vec2(az, P.y), vec2(lampZ, gBot + 0.05), vec2(0.07, 0.022), 0.008) < 0.0) kind = 8;
      }
      float plate = rbox(P.zy, vec2(0.0, 0.5 * (bumpLo + bumpHi)), vec2(0.15, 0.06), 0.01);
      if (plate < 0.0) { base = vec3(0.9, 0.88, 0.8); kind = 1; }
    } else if (region == 6) {
      if (P.y < bumpHi && P.y > bumpLo) { base = vec3(0.82, 0.82, 0.84); kind = 2; }  // chrome rear bumper
      else if (P.y < bumpLo) { base = vec3(0.03); kind = 1; }
      float seam = P.y > shLiftY ? lineAt(az, shLiftZ, 0.004, fw) : 0.0;
      seam = max(seam, az < shLiftZ ? lineAt(P.y, shLiftY, 0.004, fw) : 0.0);
      base = mix(base, vec3(0.012), seam * 0.9);
      // CHEVROLET pressed across the tailgate: nine letter blocks.
      float lx = (P.z + 0.45) / 0.10;
      if (abs(P.y - (shB - 0.16)) < 0.03 && lx > 0.0 && lx < 9.0 && fract(lx) > 0.15 && fract(lx) < 0.85) base = uPaint * 0.72;
      // Tail lamps: tall rectangles in the corners, a white reversing lamp at the bottom.
      if (az > shW2 - 0.17 && az < shW2 - 0.02 && P.y > bumpHi + 0.06 && P.y < bumpHi + 0.34) kind = P.y < bumpHi + 0.11 ? 3 : 4;
      if (P.y > shB - 0.02) base = top;  // the top over the tailgate glass
      float plate = rbox(P.zy, vec2(0.0, bumpHi + 0.12), vec2(0.15, 0.065), 0.01);
      if (plate < 0.0) { base = vec3(0.9, 0.88, 0.8); kind = 1; }
    }
    if (kind == 7) k = P.y * 2.2;
    if (kind == 3) {
      j = (P.z > 0.0 ? W - 0.55 : 0.55) + (P.z > 0.0 ? -(az - lampZ) : (az - lampZ)) * 3.0;
      k = uBody.z + (P.y - lampY) * 3.0 / 2.2;
    }
  }

  vec3 lin = pow(base, vec3(2.2));
  float sh = shadowAt(vPos, n);
  float ndl = max(dot(n, uSun), 0.0);
  vec3 amb = ambientAt(n);
  vec3 H = normalize(V + uSun);
  vec3 env = skyEnv(reflect(-V, n));
  float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
  vec3 lspec, lamp = lampLight(vPos, n, V, kind == 0 ? 140.0 : 40.0, 0.0, lspec);
  vec3 lit = amb + ndl * sh * kSunCol + lamp;
  vec3 spec = kSunSpec * sh;
  // Which lamp a lens pixel belongs to: its side, and front or back.
  int side = (uShell > 0 ? P.z > 0.0 : j > 0.5 * W) ? 1 : 0;
  bool rearLens = uShell > 0 ? P.x < 0.0 : region == 6;
  vec3 col;
  if (kind == 0) {  // clear-coated paint
    col = lin * lit + lin * env * 0.12 + env * fres * 0.9 + pow(max(dot(n, H), 0.0), 140.0) * 1.8 * spec + lspec * 0.5;
  } else if (kind == 1 || kind == 5) {
    col = lin * lit + env * fres * (kind == 1 ? 0.18 : 0.05) + lspec * (kind == 1 ? 0.05 : 0.01);
  } else if (kind == 2) {
    col = env * 0.8 + pow(max(dot(n, H), 0.0), 200.0) * 3.0 * spec + lspec * 0.6;
  } else if (kind == 3) {  // headlamp (or reversing lamp at the back): reflector bowl behind a clear lens
    float cx = j < 0.5 * W ? (uStyle.x > 0.5 ? 0.55 : 0.62) : W - (uStyle.x > 0.5 ? 0.55 : 0.62);
    float ring = 0.5 + 0.5 * sin(length(vec2(j - cx, (k - uBody.z) * 2.2)) * 55.0);
    vec3 refl = pow(mix(vec3(0.5, 0.52, 0.56), vec3(0.95), ring * 0.35), vec3(2.2));
    float ok = rearLens ? uLampOk[2 + side] : uLampOk[side];
    float on = rearLens ? uReverse * ok : uLampOn[side];
    col = refl * lit * 0.35 + env * (0.15 + fres) + vec3(1.0, 0.96, 0.88) * (0.25 + (rearLens ? 14.0 : 28.0) * on) * (0.8 + 0.4 * ring);
    col = mix(vec3(0.006) + env * fres * 0.4, col, ok);  // smashed: a dark hole with shards
  } else if (kind == 4) {  // tail lamp: lit with the headlamps, much brighter when braking
    vec3 red = pow(vec3(0.7, 0.04, 0.03), vec3(2.2));
    float ok = uLampOk[2 + side];
    col = red * lit * 0.5 + env * fres * 0.6 + vec3(1.0, 0.05, 0.03) * (0.12 + 2.5 * uLampOn[2 + side] + 9.0 * uBrake * ok);
    col = mix(red * lit * 0.2 + env * fres * 0.3, col, ok);
  } else if (kind == 8) {  // amber corner / indicator lens: the parking lights glow with the headlamps
    col = vec3(0.85, 0.25, 0.015) * (0.05 + lit * 0.4 + 1.2 * uLampOn[side]) + env * fres * 0.4;
  } else if (kind == 6) {  // number plate with unreadable characters
    float chars = step(abs(k - 0.62), 0.05) * step(0.45, fract(j * 7.0)) * step(abs(j - 0.5 * W), 0.48);
    col = pow(mix(vec3(0.92, 0.92, 0.88), vec3(0.05), chars), vec3(2.2)) * (lit + (rearLens ? 0.5 * max(uLampOn.z, uLampOn.w) : 0.0));  // plate lamp
  } else {  // grille slats
    vec3 g = mix(vec3(0.015), vec3(0.09), step(0.55, fract(k * 10.0)));
    col = pow(g, vec3(2.2)) * lit + env * fres * 0.25;
  }
  oCol = finishColor(col, vPos, 1.0);
})";

// A car's parts merged into one mesh (Renderer::CarModels): each vertex carries its part's number, and the part's
// transform comes from uParts (four texels: its axes scaled, its origin; the clip range along its X and what's
// added to the material ride in the w's). As appendModel places them on the CPU.
const char* kPartsXf = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
layout(location=3) in float aPart;
uniform sampler2D uParts; uniform int uPartBase;
vec4 c0, c1, c2, c3;
vec3 partPos(){
  int i = uPartBase + int(aPart + 0.5);
  ivec2 b = ivec2((i % 256) * 4, i / 256);
  c0 = texelFetch(uParts, b, 0); c1 = texelFetch(uParts, b + ivec2(1, 0), 0);
  c2 = texelFetch(uParts, b + ivec2(2, 0), 0); c3 = texelFetch(uParts, b + ivec2(3, 0), 0);
  float x = clamp(aPos.x, c0.w, c1.w);
  return c3.xyz + c0.xyz * x + c1.xyz * aPos.y + c2.xyz * aPos.z;
}
)";
const char* kPartsVS = R"(
uniform mat4 uVP; uniform mat4 uLightVP;
out vec3 vPos; out vec3 vNrm; centroid out vec4 vCol; out vec4 vLight; out vec3 vLocal;
void main(){
  vec3 wp = partPos();
  vec3 s2 = max(vec3(dot(c0.xyz, c0.xyz), dot(c1.xyz, c1.xyz), dot(c2.xyz, c2.xyz)), vec3(1e-12));
  vPos = wp; vLocal = wp;
  vNrm = normalize(c0.xyz * (aNrm.x / s2.x) + c1.xyz * (aNrm.y / s2.y) + c2.xyz * (aNrm.z / s2.z));
  vCol = vec4(aCol.rgb, aCol.a + c2.w);
  vLight = uLightVP * vec4(wp, 1.0);
  gl_Position = uVP * vec4(wp, 1.0);
})";
const char* kPartsDepthVS = R"(
uniform mat4 uLightVP;
void main(){ gl_Position = uLightVP * vec4(partPos(), 1.0); })";

const char* kDepthVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uLightVP; uniform mat4 uModel;
void main(){ gl_Position = uLightVP * uModel * vec4(aPos, 1.0); })";

const char* kDepthFS = R"(#version 330 core
void main(){})";

const char* kLineVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec4 aCol;
uniform mat4 uVP;
out vec4 vCol;
void main(){ vCol = aCol; gl_Position = uVP * vec4(aPos, 1.0); gl_PointSize = 5.0; })";

const char* kLineFS = R"(#version 330 core
in vec4 vCol; out vec4 oCol;
void main(){ oCol = vCol; })";

// A bent or broken car part seen through what hides it (Renderer::xrayRange): a rim of hot light, added over the
// scene where the part is behind something.
const char* kXrayVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform mat4 uVP;
out vec3 vPos; out vec3 vNrm; out float vWear;
void main(){ vPos = aPos; vNrm = aNrm; vWear = floor((aCol.a + 0.5) / 200.0); gl_Position = uVP * vec4(aPos, 1.0); })";

const char* kXrayFS = R"(#version 330 core
in vec3 vPos; in vec3 vNrm; in float vWear; out vec4 oCol;
uniform vec3 uCam; uniform float uTime;
void main(){
  vec3 n = normalize(vNrm), V = normalize(uCam - vPos);
  float rim = pow(1.0 - abs(dot(n, V)), 1.6);
  float pulse = 0.65 + 0.35 * sin(uTime * 3.5);
  vec3 c = vWear > 1.5 ? vec3(1.0, 0.22, 0.05) : vec3(0.95, 0.55, 0.06);
  oCol = vec4(c * (0.35 + 1.1 * rim) * pulse * 0.75, 1.0);
})";

const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
  vNdc = p; gl_Position = vec4(p, 0.9999, 1.0);
})";

const char* kSkyFSBody = R"(
in vec2 vNdc; out vec4 oCol;
uniform mat4 uInvVP;
void main(){
  vec4 p = uInvVP * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(p.xyz / p.w - uCam);
  oCol = vec4(skyColor(dir, 6), 1.0);
})";

const char* kFxVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform mat4 uVP;
out vec2 vUv; out float vMode; out vec4 vCol; out vec3 vPos;
void main(){ vUv = aNrm.xy; vMode = aNrm.z; vCol = aCol; vPos = aPos; gl_Position = uVP * vec4(aPos, 1.0); })";

const char* kFxFSBody = R"(
in vec2 vUv; in float vMode; in vec4 vCol; in vec3 vPos;
out vec4 oCol;
void main(){
  vec3 col = pow(vCol.rgb, vec3(2.2)); float a = vCol.a;
  if (vMode < 0.5) {            // soft puff (smoke, dust), lit by sun and sky
    float r2 = dot(vUv, vUv);
    if (r2 > 1.0) discard;
    float f = 1.0 - r2; a *= f * f;
    vec3 lspec, lamp = lampLight(vPos, vec3(0, 1, 0), normalize(uCam - vPos), 4.0, 1.0, lspec);  // dust in the beams
    col *= (0.55 + 0.45 * (vUv.y * 0.5 + 0.5)) * (ambientAt(vec3(0, 1, 0)) + kSunCol * 0.45 + lamp * 0.6);
  } else if (vMode < 1.5) {     // glowing streak (sparks): hot enough to bloom
    vec2 q = vUv * vec2(1.0, 0.45);
    a *= clamp(1.0 - dot(q, q), 0.0, 1.0);
    col *= 7.0;
  } else if (vMode > 2.5) {     // tire tracks: multiplied into the ground (drawn blending dst * 2 src)
    // The mark changes the ground's color and the way its surface faces the light, so
    // it's lit (and shadowed, and dark at night) exactly like the ground it's pressed into.
    float u = vUv.x, au = abs(u), along = vUv.y;
    float edge = 1.0 - smoothstep(0.8, 1.0, au);
    vec3 f = pow(vCol.rgb, vec3(2.2));
    if (vMode < 3.995) {
      float depth = clamp(vMode - 3.0, 0.0, 1.0);     // 0..1 = 0..15 cm
      // Surface frame from the screen derivatives: the ground normal, and which way
      // across and along the mark run.
      vec3 dp1 = dFdx(vPos), dp2 = dFdy(vPos);
      vec3 N0 = normalize(cross(dp1, dp2));
      if (N0.y < 0.0) N0 = -N0;
      vec2 du1 = dFdx(vUv), du2 = dFdy(vUv);
      vec3 p2 = cross(dp2, N0), p1 = cross(N0, dp1);
      vec3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
      T = dot(T, T) > 1e-20 ? normalize(T) : vec3(0.0);
      B = dot(B, B) > 1e-20 ? normalize(B) : vec3(0.0);
      // Groove: a flat floor, walls up to the edges (steeper the deeper it is).
      float wall = smoothstep(0.62, 0.95, au);
      float slope = (6.0 * au * (1.0 - au) + 0.4) * smoothstep(0.55, 0.7, au) * (1.0 - smoothstep(0.95, 1.0, au)) * (0.35 + 2.6 * depth);
      // Tread print on the floor: chevron lugs every 16 cm (gone where it's too fine to see).
      float tv = along / 0.16 + 0.45 * au;
      float fw = fwidth(tv);
      float tread = (1.0 - smoothstep(0.25, 0.6, fw)) * (1.0 - wall) * smoothstep(0.06, 0.16, au);
      float lugSlope = -sin(6.2832 * tv) * 0.55 * tread;
      vec3 N1 = normalize(N0 - T * sign(u) * slope + B * lugSlope);
      float ao = 1.0 - (0.25 + 0.45 * depth) * (1.0 - wall) - 0.15 * tread * (0.5 + 0.5 * cos(6.2832 * tv));
      float sh = shadowAt(vPos, N0);
      vec3 lit0 = ambientAt(N0) + kSunCol * max(dot(N0, uSun), 0.0) * sh;
      vec3 lit1 = ambientAt(N1) + kSunCol * max(dot(N1, uSun), 0.0) * sh;
      f = mix(vec3(1.0), f, 1.0 - 0.6 * wall) * ao * lit1 / max(lit0, vec3(1e-4));
    }
    f = mix(vec3(1.0), f, a * edge);
    // Into the distance the fog takes over (the mark fades with the ground under it).
    vec3 d = vPos - uCam;
    float dist = length(d), fb = 0.0065;
    float fk = abs(d.y) > 0.5 ? (1.0 - exp(-fb * d.y)) / (fb * d.y) : 1.0;
    float fog = 1.0 - exp(-0.0007 * exp(-fb * max(uCam.y, 0.0)) * dist * fk);
    f = mix(f, vec3(1.0), fog);
    oCol = vec4(clamp(f, 0.0, 2.0) * 0.5, 1.0);
    return;
  }
  oCol = vec4(applyFog(col, vPos), a);
})";

const char* kHudVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUv; out vec4 vCol;
void main(){
  vUv = aUv; vCol = aCol;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
})";

const char* kHudFS = R"(#version 330 core
in vec2 vUv; in vec4 vCol; out vec4 oCol;
uniform sampler2D uFont;
void main(){ oCol = vec4(vCol.rgb, vCol.a * texture(uFont, vUv).r); })";

// A picture drawn on the HUD: the texture (premultiplied alpha), tinted by the vertex colour.
const char* kIconFS = R"(#version 330 core
in vec2 vUv; in vec4 vCol; out vec4 oCol;
uniform sampler2D uFont;
void main(){ vec4 t = texture(uFont, vUv); oCol = vec4(t.rgb * vCol.rgb, t.a) * vCol.a; })";

// A model's thumbnail (Renderer::iconTexture): orthographic, lit by a key, a fill and a rim light fixed to the camera, with
// a sky-and-ground reflection for the metals; out comes sRGB.
const char* kIconMeshVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform vec3 uC, uR, uU, uF;
uniform vec3 uOff;      // the picture's middle in view space
uniform vec2 uScale;    // 1 / half-size across, 1 / half-depth
out vec3 vN; out vec4 vCol; out vec3 vP;
void main(){
  vec3 q = aPos - uC;
  vec3 v = vec3(dot(q, uR), dot(q, uU), dot(q, uF));
  gl_Position = vec4((v.x - uOff.x) * uScale.x, (v.y - uOff.y) * uScale.x, (v.z - uOff.z) * uScale.y, 1.0);
  vN = aNrm; vCol = aCol; vP = q;
})";
const char* kIconMeshFS = R"(#version 330 core
in vec3 vN; in vec4 vCol; in vec3 vP; out vec4 oCol;
uniform vec3 uR, uU, uF;
vec3 sky(vec3 d){  // a studio: bright above, a warm floor, a soft window to the front-left
  float h = d.y * 0.5 + 0.5;
  vec3 c = mix(vec3(0.16, 0.15, 0.14), vec3(0.78, 0.84, 0.95), smoothstep(0.15, 0.95, h));
  vec3 win = normalize(-uR * 0.6 + uU * 0.5 - uF * 0.7);
  c += vec3(1.0, 0.97, 0.9) * pow(max(dot(d, win), 0.0), 18.0) * 1.2;
  return c;
}
void main(){
  vec3 n = normalize(vN);
  vec3 nv = vec3(dot(n, uR), dot(n, uU), -dot(n, uF));   // camera space, +z toward the viewer
  float m = vCol.a;
  vec3 alb = max(vCol.rgb, vec3(0.07));   // (black rubber and steel still read on a dark card)
  if (m > 7.5 && m < 8.5) alb = mix(alb, vec3(0.62, 0.64, 0.68), 0.30);
  vec3 V = -uF;
  vec3 key = normalize(vec3(-0.55, 0.7, 0.55)), fill = normalize(vec3(0.75, 0.1, 0.45)), rim = normalize(vec3(0.3, 0.55, -0.75));
  float dk = max(dot(nv, key), 0.0), df = max(dot(nv, fill), 0.0), dr = max(dot(nv, rim), 0.0);
  float hemi = n.y * 0.5 + 0.5;
  vec3 amb = mix(vec3(0.20, 0.19, 0.18), vec3(0.34, 0.37, 0.43), hemi);
  vec3 diff = amb + vec3(1.05, 1.0, 0.92) * dk + vec3(0.35, 0.4, 0.5) * df;
  vec3 col = alb * diff;
  vec3 R = reflect(-V, n);
  float fres = pow(1.0 - max(dot(n, V), 0.0), 4.0);
  vec3 H = normalize(key + vec3(0, 0, 1));
  float sp = pow(max(dot(nv, H), 0.0), 40.0);
  if (m > 10.5 && m < 11.5) {            // chrome
    col = sky(R) * mix(vec3(0.9), alb * 1.1, 0.35) + sp * 0.5;
  } else if (m > 7.5 && m < 8.5) {       // bare steel
    col = alb * (amb * 0.6 + vec3(0.75) * dk + 0.2 * df) * 0.55 + sky(R) * alb * 0.75 + sp * alb * 0.6;
  } else if (m > 3.5 && m < 4.5) {       // paint
    col += sky(R) * 0.10 * (0.4 + 2.0 * fres) + sp * 0.35;
  } else if (m > 8.5 && m < 9.5) {       // tire rubber
    col += sp * 0.06;
  } else if (m > 5.5 && m < 6.5) {       // glow
    col = alb * 1.4;
  } else if (m > 4.5 && m < 5.5) {       // glass
    col = mix(col, sky(R) * 0.6, 0.45 + fres) + sp * 0.4;
  } else {
    col += sp * 0.05 + alb * dr * 0.1;
  }
  col += vec3(0.5, 0.55, 0.65) * dr * (0.18 + 0.5 * fres);   // the rim light edges it off the card
  col = col / (1.0 + col * 0.35);
  oCol = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
})";

// Grass: tufts of blades on a world-aligned grid round the camera, made entirely in the
// vertex shader (one instance a tuft, 7 vertices a blade: base, two joints, a tip, so the
// blade curves and tapers). A few rings of coarser and coarser grids, each fading into the
// next by thinning (a tuft shrinks away as its ring's weight drops under its own roll), so
// there is no ring edge to see. The terrain texture gives the ground, how thick the grass
// grows and the water; the ground shader's own noise says where it is dry, bald, under
// trees, on bare rock. What grows: meadow grass, tall grass with seed heads, flowers in
// patches, golden dry grass, short alpine grass, sedges at the water. Wind sways it and
// vehicles push it down and aside.
const char* kGrassVS = R"(#version 330 core
uniform sampler2D uTerrainTex; uniform float uTerrainN;
uniform mat4 uVP; uniform vec3 uCam; uniform float uTime;
uniform int uSide; uniform int uBlades; uniform int uMap;
uniform vec3 uTile;     // this draw's tufts: a run of the ring's grid from (x, z), x wide
uniform float uCell;
uniform vec4 uRing;     // this ring comes in over x..y metres from the camera, and goes out over z..w
uniform vec4 uLook;     // x height scale, y width scale, z 1: plain blades (the far rings), w 1: seed heads and flowers
uniform vec2 uRockSnow;
uniform int uPushN;
uniform vec4 uPush[8];  // vehicles as capsules: (x, z, radius) of one end, then (x, z) of the other
uniform vec3 uSun; uniform float uClouds;       // (the shadows are worked out here, a few times a blade instead of at every pixel)
uniform sampler2DShadow uShadow0, uShadow1; uniform mat4 uLightVP0, uLightVP1; uniform vec4 uShadowCfg;
out vec3 vPos; out vec3 vN; centroid out vec3 vC0; centroid out vec3 vC1; centroid out vec3 vC2; out vec2 vT; out float vSh;  // (centroid: see kLitVS)
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  return mix(mix(hash(i), hash(i+vec2(1,0)), f.x), mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p, int oct){
  float s = 0.0, a = 0.5;
  for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = mat2(1.6, 1.2, -1.2, 1.6) * p; a *= 0.5; }
  return s;
}
float hs(vec2 c, uint s){  // integer hash of a cell and a salt: sin() hashes repeat at these coordinates
  uvec2 q = uvec2(ivec2(c) + 1000000);
  uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u) ^ (s * 2246822519u + 374761393u);
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return float(h & 0xFFFFFFu) / 16777216.0;
}
vec4 tx(ivec2 i){ return texelFetch(uTerrainTex, clamp(i, ivec2(0), ivec2(int(uTerrainN) - 1)), 0); }
float grassShadow(vec3 p){
  float sh = 1.0;
  if (uShadowCfg.x > 0.5) {
    vec3 q = p + vec3(0.0, 0.035, 0.0);
    vec4 L0 = uLightVP0 * vec4(q, 1.0);
    vec3 c0 = L0.xyz / L0.w * 0.5 + 0.5;
    float e0 = max(abs(c0.x - 0.5), abs(c0.y - 0.5));
    float far = 1.0;
    if (e0 > 0.42) {
      vec4 L1 = uLightVP1 * vec4(q, 1.0);
      vec3 c1 = L1.xyz / L1.w * 0.5 + 0.5;
      if (max(abs(c1.x - 0.5), abs(c1.y - 0.5)) < 0.5 && c1.z < 1.0) far = texture(uShadow1, vec3(c1.xy, c1.z - 0.0004));
    }
    sh = far;
    if (e0 < 0.5 && c0.z < 1.0) sh = mix(texture(uShadow0, vec3(c0.xy, c0.z - 0.00012)), far, smoothstep(0.42, 0.5, e0));
  }
  if (uClouds > 0.0) {
    vec2 w = p.xz + uSun.xz / max(uSun.y, 0.2) * (1800.0 - p.y);
    sh *= 1.0 - 0.65 * uClouds * smoothstep(0.50, 0.78, fbm(w * 0.00045 + vec2(uTime * 0.004, uTime * 0.0015), 3));
  }
  return sh;
}
void gone(){ vSh = 1.0; vPos = vec3(0.0); vN = vec3(0.0, 1.0, 0.0); vC0 = vC1 = vC2 = vec3(0.0); vT = vec2(0.0, 2.0); gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }
void main(){
  int id = gl_VertexID, k = id % 7, blade = id / 7;
  int tuft = gl_InstanceID;
  int tw = int(uTile.z + 0.5);
  vec2 cell = floor(uCam.xz / uCell) + uTile.xy + vec2(tuft % tw, tuft / tw) - float(uSide / 2);
  vec2 base = (cell + vec2(hs(cell, 1u), hs(cell, 2u))) * uCell;
  float dist = length(base - uCam.xz);
  // This ring's share of the ground here against the rings either side: a tuft is there while its own roll is under the share.
  float share = smoothstep(uRing.x, uRing.y, dist) * (1.0 - smoothstep(uRing.z, uRing.w, dist));
  float grow = smoothstep(0.0, 0.3, share - hs(cell, 3u));
  if (grow <= 0.0) { gone(); return; }
  {  // Out of the view sideways (the height hardly matters for that): gone before any texture or noise work.
    vec4 c0 = uVP * vec4(base.x, uCam.y - 1.0, base.y, 1.0);
    if (c0.w < -2.0 || abs(c0.x) > c0.w + 2.5 * uVP[0][0] + 2.0) { gone(); return; }
  }
  // The four terrain texels round the tuft: height and slope, how thick it grows, paint, water.
  vec2 g = base + 600.0;
  ivec2 i0 = ivec2(floor(g));
  vec2 f = g - vec2(i0);
  vec4 t00 = tx(i0), t10 = tx(i0 + ivec2(1, 0)), t01 = tx(i0 + ivec2(0, 1)), t11 = tx(i0 + ivec2(1, 1));
  float dens = mix(mix(t00.g, t10.g, f.x), mix(t01.g, t11.g, f.x), f.y);
  float paint = max(max(t00.b, t10.b), max(t01.b, t11.b));
  if (dens < 0.02 || paint > 0.5) { gone(); return; }
  float gy = mix(mix(t00.r, t10.r, f.x), mix(t01.r, t11.r, f.x), f.y);
  vec2 slope = vec2(mix(t10.r - t00.r, t11.r - t01.r, f.y), mix(t01.r - t00.r, t11.r - t10.r, f.x));
  vec4 clip = uVP * vec4(base.x, gy + 0.4, base.y, 1.0);
  if (clip.w < -0.3 || abs(clip.x) > clip.w + 1.2 * uVP[0][0] || abs(clip.y) > clip.w + 1.2 * uVP[1][1]) { gone(); return; }
  // The ground shader's own noise: what it makes of the meadow here.
  float n1 = fbm(base * 0.02, 4);
  float dry = smoothstep(0.6, 0.8, fbm(base * 0.006 + 3.1, 3));
  float bald = smoothstep(0.60, 0.80, fbm(base * 0.30 + 9.0, 3));
  bool meadow = uMap == 0 || uMap == 2;
  float forest = meadow ? 0.0 : smoothstep(0.40, 0.65, fbm(base * 0.012 + vec2(7.0, -3.0), 3)) * smoothstep(6.0, 22.0, gy);
  float ny = inversesqrt(1.0 + dot(slope, slope));
  float steep = meadow ? smoothstep(0.84, 0.62, ny + 0.14 * (n1 - 0.5)) : smoothstep(0.72, 0.55, ny + 0.14 * (n1 - 0.5));
  float high = smoothstep(uRockSnow.x, uRockSnow.x + 90.0, gy + 30.0 * n1);
  float snow = smoothstep(uRockSnow.y, uRockSnow.y + 50.0, gy + 70.0 * n1) * smoothstep(0.5, 0.75, ny);
  float wl = t00.a;
  float wet = wl > -500.0 ? 1.0 - smoothstep(0.4, 3.5, gy - wl) : 0.0;
  // How thick: the world's density, clumped (patches and gaps), thinner where the ground shows through.
  float clump = smoothstep(0.25, 0.75, fbm(base * 0.45 + 2.0, 2));
  float d = dens * (0.7 + 0.8 * clump) * (1.0 - 0.8 * bald) * (1.0 - 0.3 * smoothstep(0.35, 0.65, n1) * (1.0 - dry))
          * (1.0 - 0.5 * dry) * (1.0 - 0.75 * forest) * (1.0 - steep) * (1.0 - 0.7 * high) * (1.0 - snow) * (1.0 + 0.5 * wet);
  float e = d - hs(cell, 4u);
  if (e <= 0.0) { gone(); return; }
  grow *= smoothstep(0.0, 0.2, e);
  // What kind of grass.
  float tallN = smoothstep(0.55, 0.72, fbm(base * 0.045 + 20.0, 2)) * (1.0 - wet) * (1.0 - high);
  float flowers = smoothstep(0.58, 0.78, fbm(base * 0.07 + 50.0, 2)) * (1.0 - dry * 0.7) * (1.0 - wet) * (1.0 - high);
  float tuftH = 0.42 * (0.7 + 0.6 * clump) * (1.0 + 0.9 * tallN + 0.7 * wet) * mix(1.0, 0.7, dry) * mix(1.0, 0.5, high);
  vec3 body0 = mix(vec3(0.11, 0.23, 0.04), vec3(0.30, 0.24, 0.09), dry);
  vec3 body1 = mix(vec3(0.34, 0.50, 0.11), vec3(0.74, 0.60, 0.28), dry);
  body0 = mix(body0, vec3(0.11, 0.18, 0.07), high); body1 = mix(body1, vec3(0.30, 0.42, 0.20), high);
  body0 = mix(body0, vec3(0.05, 0.14, 0.04), wet); body1 = mix(body1, vec3(0.26, 0.40, 0.10), wet);
  float tint = hs(cell, 5u);
  body1 = mix(body1, body1 * vec3(1.3, 1.12, 0.5), tint * 0.6 * (1.0 - wet));      // some tufts yellower
  float bright = 0.8 + 0.4 * clump + 0.15 * (hs(cell, 6u) - 0.5);
  body0 *= bright; body1 *= bright;
  // This blade.
  uint sb = 100u + uint(blade) * 8u;
  float rr = sqrt(hs(cell, sb)) * uCell * 0.7, ra = hs(cell, sb + 1u) * 6.2832;
  vec2 off = vec2(cos(ra), sin(ra)) * rr;
  vec2 bp = base + off;
  vec2 out2 = off / max(length(off), 1e-3);
  float la = hs(cell, sb + 2u) * 6.2832;
  vec2 ld = normalize(mix(out2, vec2(cos(la), sin(la)), 0.45) + vec2(1e-4));
  float hr = hs(cell, sb + 3u), wr = hs(cell, sb + 4u), kr = hs(cell, sb + 5u);
  float lean = 0.25 + 0.6 * hs(cell, sb + 6u) + 0.35 * tallN + 0.25 * wet;
  float len = tuftH * (0.55 + 0.7 * hr) * uLook.x * grow;
  float w0 = 0.021 * uLook.y * (0.8 + 0.5 * wr) * mix(1.0, 0.7, wet);
  // 0 blade, 1 seed head (tall grass, sedge), 2 flower.
  float tA = 0.38, tB = 0.72;
  vec4 wd4 = vec4(1.0, 0.86, 0.5, 0.0);
  vec3 head = vec3(0.0);
  float headAt = 2.0;
  if (uLook.w > 0.5) {
    float fl = hs(cell, 7u);
    if (blade == 0 && fl < 0.01 + 0.4 * flowers) {  // a flower on a stem over the tuft
      float hue = hs(floor(base * 0.02), 8u);
      head = hue < 0.28 ? vec3(0.95, 0.80, 0.12) : hue < 0.55 ? vec3(0.92, 0.92, 0.86) : hue < 0.78 ? vec3(0.58, 0.36, 0.82) : hue < 0.92 ? vec3(0.95, 0.45, 0.60) : vec3(0.88, 0.22, 0.14);
      len *= 1.25 + 0.5 * hs(cell, 9u); lean *= 0.5;
      tA = 0.80; tB = 0.92; wd4 = vec4(0.45, 0.35, 2.2, 0.0); headAt = tA;
    } else if (kr < 0.4 * tallN + 0.2 * wet * (1.0 - tallN)) {  // an ear of seed
      head = wet > 0.5 ? vec3(0.30, 0.18, 0.08) : vec3(0.62, 0.50, 0.24) * (0.85 + 0.3 * hs(cell, 9u));
      tA = 0.66; tB = 0.84; wd4 = vec4(0.7, 0.5, 1.5, 0.0); headAt = tA;
    }
  }
  bool plain = uLook.z > 0.5 && headAt > 1.0;
  if (plain) { tA = 0.45; wd4 = vec4(1.0, 0.7, 0.0, 0.0); }
  // Where on the blade this vertex is.
  float t = k < 2 ? 0.0 : k < 4 ? tA : k < 6 ? tB : 1.0;
  float wv = k < 2 ? wd4.x : k < 4 ? wd4.y : k < 6 ? wd4.z : 0.0;
  float sd = k == 6 ? 0.0 : (k % 2 == 0 ? -1.0 : 1.0);
  // Vehicles push it down and aside.
  vec2 push = vec2(0.0); float squash = 0.0;
  for (int c = 0; c < uPushN; c++) {
    vec4 a = uPush[2 * c], b = uPush[2 * c + 1];
    vec2 ab = b.xy - a.xy;
    vec2 q = a.xy + ab * clamp(dot(bp - a.xy, ab) / max(dot(ab, ab), 1e-4), 0.0, 1.0);
    vec2 dv = bp - q;
    float dl = length(dv);
    float fp = 1.0 - smoothstep(a.z * 0.6, a.z + 0.8, dl);
    push += dv / max(dl, 1e-3) * fp;
    squash = max(squash, fp);
  }
  len *= 1.0 - 0.6 * squash;
  // Wind: gust fronts crossing the ground, a flutter on top; taller blades give more.
  float gust = 0.5 + 0.5 * sin(dot(bp, vec2(0.07, 0.045)) - uTime * 0.85 + 1.7 * sin(dot(bp, vec2(-0.03, 0.05)) + uTime * 0.31));
  vec2 wdir = vec2(0.82, 0.57);
  float flutter = sin(uTime * 3.1 + bp.x * 3.7 + bp.y * 2.9 + float(blade) * 1.7) * 0.03;
  vec2 sway = (wdir * (0.03 + 0.14 * gust) + vec2(-wdir.y, wdir.x) * flutter) * len;
  // The blade is a curve: up and over toward its lean, with the sway and any push on top.
  vec2 hz = ld * (lean * len * t * t) + (sway + push * len * 0.95) * t * t;
  float up = len * (t - 0.18 * lean * t * t) * (1.0 - 0.5 * squash * t);
  vec2 wdv = vec2(-ld.y, ld.x);
  float gh = gy + dot(slope, off) - 0.02;
  vec3 p = vec3(bp.x + hz.x + wdv.x * w0 * wv * sd, gh + up, bp.y + hz.y + wdv.y * w0 * wv * sd);
  vPos = p;
  vSh = grassShadow(vec3(bp.x, gh + 0.05 + up * 0.5, bp.y));
  vN = normalize(vec3(-ld.x, 0.4 + 1.6 * lean * t, -ld.y));
  vC0 = body0 * (0.85 + 0.3 * wr); vC1 = body1 * (0.85 + 0.3 * hr); vC2 = headAt > 1.0 ? vC1 : head;
  vT = vec2(t, headAt);
  gl_Position = uVP * vec4(p, 1.0);
})";

const char* kGrassFSBody = R"(
in vec3 vPos; in vec3 vN; centroid in vec3 vC0; centroid in vec3 vC1; centroid in vec3 vC2; in vec2 vT; in float vSh;
out vec4 oCol;
void main(){
  float t = vT.x;
  float hm = smoothstep(vT.y - 0.02, vT.y + 0.03, t);
  vec3 lin = pow(max(mix(mix(vC0, vC1, smoothstep(0.0, 0.9, t)), vC2, hm), 0.0), vec3(2.2));
  vec3 V = normalize(uCam - vPos);
  vec3 n = normalize(vN);
  if (dot(n, V) < 0.0) n = -n;                       // (a blade has two faces: the one you see)
  n = normalize(mix(n, vec3(0, 1, 0), 0.5));
  float sh = vSh;
  float ao = mix(0.25, 1.0, smoothstep(0.0, 0.6, t));   // dark down at the root
  float lit = max(dot(n, uSun) * 0.6 + 0.4, 0.0);
  float through = pow(max(dot(-V, uSun), 0.0), 3.0) * 0.45 * smoothstep(0.1, 0.8, t);  // the sun shining through the blades
  vec3 lspec, lamp = lampLight(vPos, n, V, 8.0, 0.6, lspec);
  vec3 col = lin * (ambientAt(n) * ao + ((lit + through) * sh * kSunCol + lamp) * mix(0.4, 1.0, ao));
  oCol = finishColor(col, vPos, 1.0);
})";

// Water: the lake bed seen through it (the framebuffer behind, dimmed and tinted by how far
// the light goes through: depth from the terrain texture), clear and blue-green over gravel,
// brown and soon opaque over mud; the sky and a sun glint on rippling normals by Fresnel;
// a flow-stretched current; foam and a fading edge where it meets the shore. Dual-source
// blending: oCol is what the surface adds, oKeep how much of the bed shows through, per channel.
const char* kWaterFSBody = R"(
in vec3 vPos; in vec3 vNrm; centroid in vec4 vCol; in vec4 vLight; in vec3 vLocal;
layout(location = 0, index = 0) out vec4 oCol;
layout(location = 0, index = 1) out vec4 oKeep;
uniform sampler2D uTerrainTex; uniform float uTerrainN;
float groundAt(vec2 xz){
  vec2 g = xz + 600.0;
  ivec2 i = clamp(ivec2(floor(g)), ivec2(0), ivec2(int(uTerrainN) - 2));
  vec2 f = g - vec2(i);
  float a = texelFetch(uTerrainTex, i, 0).r, b = texelFetch(uTerrainTex, i + ivec2(1, 0), 0).r;
  float c = texelFetch(uTerrainTex, i + ivec2(0, 1), 0).r, d = texelFetch(uTerrainTex, i + ivec2(1, 1), 0).r;
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// Value noise and its slope, for ripple normals without finite differences.
vec3 vnd(vec2 p){
  vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f), du = 6.0 * f * (1.0 - f);
  float a = hash(i), b = hash(i + vec2(1.0, 0.0)), c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
  float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
  return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y, du.x * (k1 + k4 * u.y), du.y * (k2 + k4 * u.x));
}
// Surface slope: a long swell, ripples and fine chop, each scrolling its own way (rotated
// against each other so no lattice shows), fading out with distance. fd: downstream (zero:
// still water); streaks lengthen along it.
vec2 ripples(vec2 p, vec2 fd, float dist, float rough){
  float t = uTime;
  if (dot(fd, fd) > 0.01) {  // current: stretch the pattern along it
    vec2 pp = vec2(-fd.y, fd.x), pe = vec2(dot(p, fd) * 0.5, dot(p, pp));
    vec3 g1 = vnd(pe * 0.55 + vec2(0.0, t * 0.05)), g2 = vnd(pe * vec2(1.4, 2.6) + vec2(7.1, 3.3)), g3 = vnd(pe * vec2(3.0, 7.0) + vec2(1.7, 9.2));
    vec2 gq = g1.yz * 0.55 * 0.55 + g2.yz * vec2(1.4, 2.6) * 0.20 + g3.yz * vec2(3.0, 7.0) * 0.06 * clamp(14.0 / (dist + 6.0), 0.0, 1.0);
    return (fd * gq.x * 0.5 + pp * gq.y) * rough;
  }
  vec3 g1 = vnd(p * 0.42 + t * vec2(0.09, 0.035));
  vec3 g2 = vnd(mat2(0.8, 0.6, -0.6, 0.8) * p * 1.7 - t * vec2(0.14, 0.27));
  vec3 g3 = vnd(mat2(0.6, -0.8, 0.8, 0.6) * p * 5.3 + t * vec2(0.45, -0.31));
  vec2 gr = g1.yz * 0.42 * 0.55 + (mat2(0.8, -0.6, 0.6, 0.8) * g2.yz) * 1.7 * 0.20 + (mat2(0.6, 0.8, -0.8, 0.6) * g3.yz) * 5.3 * 0.05 * clamp(14.0 / (dist + 6.0), 0.0, 1.0);
  return gr * rough;
}
void main(){
  float gy = groundAt(vPos.xz);
  float depth = vPos.y - gy;
  if (depth < 0.0) discard;
  vec2 p = vPos.xz;
  vec3 Vv = uCam - vPos;
  float dist = length(Vv);
  vec3 V = Vv / dist;
  float sh = shadowAt(vPos, vec3(0, 1, 0));
  vec3 irr = ambientAt(vec3(0, 1, 0)) + sh * kSunCol * 0.55;
  // What it flows over: the share of mud round here (a few taps, so the change is gradual).
  ivec2 ti = clamp(ivec2(floor(p + 600.5)), ivec2(2), ivec2(int(uTerrainN) - 3));
  float mud = 0.0;
  for (int k = 0; k < 4; k++) {
    ivec2 o = ti + ivec2((k & 1) * 4 - 2, (k >> 1) * 4 - 2);
    mud += 1.0 - step(0.5, abs(texelFetch(uTerrainTex, o, 0).b - 8.0));
  }
  float murk = smoothstep(0.2, 0.8, mud * 0.25);
  vec2 flow = vCol.rg;
  float fl = length(flow);
  vec2 fd = fl > 0.1 ? flow / fl : vec2(0.0);
  float rough = (1.0 + 0.5 * min(fl, 2.0)) * (1.0 - 0.45 * murk);
  vec2 sl;
  float wb = 0.0, ph0 = 0.0, ph1 = 0.0;
  vec2 o0 = vec2(0.0), o1 = vec2(13.7, 4.1);
  if (fl > 0.1) {  // two staggered phases of the pattern, blended, so it drifts without smearing
    ph0 = fract(uTime * 0.2), ph1 = fract(uTime * 0.2 + 0.5), wb = abs(1.0 - 2.0 * ph0);
    o0 = flow * ph0 * 5.0, o1 = flow * ph1 * 5.0 + vec2(13.7, 4.1);
    sl = mix(ripples(p - o1, fd, dist, rough), ripples(p - o0, fd, dist, rough), wb);
  } else sl = ripples(p, fd, dist, rough);
  sl *= 0.26 / (1.0 + dist / 60.0);
  vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
  bool under = V.y < 0.0;  // camera below the surface
  if (under) n.y = -n.y, V.y = -V.y;
  float ndv = max(dot(n, V), 0.0);
  float fres = (0.02 + 0.98 * pow(1.0 - ndv, 5.0)) * (1.0 - 0.6 * murk);  // (a muddy skin reflects less)
  vec3 r = reflect(-V, n);
  r.y = abs(r.y);
  vec3 refl = skyColor(vec3(r.x, max(r.y, 0.02), r.z), 3);
  // Low in the reflection the banks and the trees on them, not sky: the ground rising
  // along the reflected ray (a few coarse steps), and a band round the horizon.
  float occ = smoothstep(0.35, 0.03, r.y) * 0.7;
  vec2 rd = r.xz / max(length(r.xz), 1e-3);
  for (int k = 0; k < 6; k++) {
    float s = 2.5 * exp2(float(k));
    ivec2 gi = clamp(ivec2(floor(p + rd * s + 600.5)), ivec2(0), ivec2(int(uTerrainN) - 1));
    occ = max(occ, smoothstep(0.0, 3.0, texelFetch(uTerrainTex, gi, 0).r - (vPos.y + r.y / max(length(r.xz), 0.05) * s)) * 0.85);
  }
  refl = mix(refl, vec3(0.035, 0.05, 0.03) * (ambientAt(vec3(0, 1, 0)) + 0.4 * sh * kSunCol), occ);
  vec3 H = normalize(V + uSun);
  vec3 lspec, lamp = lampLight(vPos, n, V, 400.0, 0.0, lspec);
  float nh = max(dot(n, H), 0.0);
  vec3 glint = kSunSpec * (pow(nh, 700.0) * 45.0 + pow(nh, 70.0) * 0.9) * sh * (1.0 - 0.7 * murk) + lspec * 3.0;  // sun, moon and headlamps on the ripples
  // The light through the water: red goes first, then green; mud stops all of it within a half metre.
  vec3 sigma = mix(vec3(0.60, 0.16, 0.10), vec3(3.4, 3.0, 2.6), murk);
  float vy = max(V.y, 0.0), cosr = sqrt(max(1.0 - (1.0 - vy * vy) / 1.78, 0.04));  // (the ray bent into the water)
  float path = min(depth, 8.0) / cosr;
  vec3 T = exp(-sigma * path);
  vec3 body = mix(vec3(0.020, 0.080, 0.085), vec3(0.080, 0.046, 0.020), murk) * (irr + lamp * 0.3);
  vec3 col = refl * fres + body * (1.0 - T) * (1.0 - fres) + glint;
  vec3 keep = T * (1.0 - fres);
  // Soft shore: fades in over the first hand's depth; a fringe of foam that laps in and out.
  float edge = smoothstep(0.0, 0.12, depth);
  col *= edge;
  keep = 1.0 - edge * (1.0 - keep);
  // (Foam by distance to the shore, not depth: a beach is a hand deep for metres.)
  float foam = 0.0;
  if (depth < 0.2) {
    vec2 gd = vec2(groundAt(p + vec2(0.75, 0.0)) - groundAt(p - vec2(0.75, 0.0)), groundAt(p + vec2(0.0, 0.75)) - groundAt(p - vec2(0.0, 0.75))) / 1.5;
    float dd = depth / max(length(gd), 0.03);  // metres to where the water ends
    float lap = 0.22 + 0.12 * sin(uTime * 1.1 + vnoise(p * 0.7) * 6.28);
    foam = (1.0 - smoothstep(lap * 0.3, lap + 0.1, dd)) * smoothstep(0.3, 0.7, vnoise(p * 7.0 + uTime * 0.35));
    foam += (1.0 - smoothstep(0.0, 0.1, dd)) * 0.5;  // the wet line itself
  }
  // White water where it runs fast and shallow.
  float fast = smoothstep(0.9, 2.2, fl) * (1.0 - smoothstep(0.3, 1.2, depth));
  foam = max(foam, fast * smoothstep(0.45, 0.8, mix(vnoise((p - o1) * 2.3), vnoise((p - o0) * 2.3), wb)));
  foam *= 1.0 - 0.8 * murk;
  foam = clamp(foam, 0.0, 1.0) * 0.8;
  col = mix(col, vec3(0.8, 0.84, 0.84) * (irr * 0.7 + 0.05), foam);
  keep *= 1.0 - foam;
  if (under) { col = body * 1.5 + refl * 0.04; keep = vec3(0.45, 0.6, 0.62) * edge + (1.0 - edge); }  // from below: a green-blue ceiling
  float cov = max(1.0 - dot(keep, vec3(0.3333)), 0.02);
  oCol = vec4(applyFog(col / cov, vPos) * cov, 1.0);
  oKeep = vec4(keep, 1.0);
})";

// ---- Post-processing (full-screen triangle) ----
const char* kPostVS = R"(#version 330 core
out vec2 vUv;
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// Bloom/blur chain, dual-filter style: each level is a half-size, filtered copy of the
// one above (the first one keeps only what's brighter than the threshold).
const char* kDownFS = R"(#version 330 core
in vec2 vUv; out vec4 oCol;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uThreshold;  // < 0: keep everything
vec3 pre(vec3 c){
  if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);  // a stray bad pixel must not bloom into a spark
  c = clamp(c, vec3(0.0), vec3(30.0));  // the sun's disk must not turn into a huge square
  if (uThreshold < 0.0) return c;
  float br = max(c.r, max(c.g, c.b)), knee = uThreshold * 0.5;
  float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee + 1e-4);
  return c * max(soft, br - uThreshold) / max(br, 1e-4);
}
void main(){
  vec2 h = uTexel;
  vec3 s = pre(texture(uSrc, vUv).rgb) * 4.0;
  s += pre(texture(uSrc, vUv + vec2(-h.x, -h.y)).rgb) + pre(texture(uSrc, vUv + vec2(h.x, -h.y)).rgb);
  s += pre(texture(uSrc, vUv + vec2(-h.x, h.y)).rgb) + pre(texture(uSrc, vUv + vec2(h.x, h.y)).rgb);
  oCol = vec4(s / 8.0, 1.0);
})";

const char* kUpFS = R"(#version 330 core
in vec2 vUv; out vec4 oCol;
uniform sampler2D uSrc; uniform vec2 uTexel;
void main(){
  vec2 h = uTexel;
  vec3 s = texture(uSrc, vUv + vec2(-h.x, 0)).rgb + texture(uSrc, vUv + vec2(h.x, 0)).rgb
         + texture(uSrc, vUv + vec2(0, -h.y)).rgb + texture(uSrc, vUv + vec2(0, h.y)).rgb;
  s += 2.0 * (texture(uSrc, vUv + 0.5 * vec2(-h.x, -h.y)).rgb + texture(uSrc, vUv + 0.5 * vec2(h.x, -h.y)).rgb
            + texture(uSrc, vUv + 0.5 * vec2(-h.x, h.y)).rgb + texture(uSrc, vUv + 0.5 * vec2(h.x, h.y)).rgb);
  oCol = vec4(s / 12.0, 1.0);
})";

// Scalable ambient occlusion (McGuire et al.) from the depth buffer alone: a spiral of
// samples around each pixel; geometry above its tangent plane and within R occludes.
const char* kAOFS = R"(#version 330 core
in vec2 vUv; out vec4 oCol;
uniform sampler2D uDepth; uniform mat4 uInvProj;
uniform vec2 uTexel;       // one depth texel
uniform vec2 uAOTexel;     // one AO texel
uniform float uProjScale;  // AO pixels per meter at 1 m
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 posAt(vec2 uv){
  float d = texture(uDepth, uv).r;
  vec4 v = uInvProj * vec4(vec3(uv, d) * 2.0 - 1.0, 1.0);
  return v.xyz / v.w;
}
void main(){
  if (texture(uDepth, vUv).r >= 1.0) { oCol = vec4(1.0); return; }
  vec3 P = posAt(vUv);
  vec3 Pr = posAt(vUv + vec2(uTexel.x, 0)), Pl = posAt(vUv - vec2(uTexel.x, 0));
  vec3 Pu = posAt(vUv + vec2(0, uTexel.y)), Pd = posAt(vUv - vec2(0, uTexel.y));
  vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
  vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
  vec3 n = normalize(cross(dx, dy));
  if (dot(n, P) > 0.0) n = -n;
  const float R = 0.55;
  const int N = 16;
  float rpx = min(uProjScale * R / -P.z, 90.0);
  if (rpx < 1.5) { oCol = vec4(1.0); return; }
  float rot = hash(gl_FragCoord.xy) * 6.2832, sum = 0.0, bias = 0.006 - 0.0015 * P.z;
  for (int i = 0; i < N; i++) {
    float a = (float(i) + 0.5) / float(N), ang = a * 43.98 + rot;  // 7 turns
    vec3 Q = posAt(vUv + vec2(cos(ang), sin(ang)) * a * rpx * uAOTexel);
    vec3 v = Q - P;
    float vv = dot(v, v), f = max(R * R - vv, 0.0);
    sum += f * f * f * max((dot(v, n) - bias) / (vv + 0.01), 0.0);
  }
  oCol = vec4(max(0.0, 1.0 - sum * 1.1 / pow(R, 6.0) * (5.0 / float(N))));
})";

// Final image: occlusion, bloom, exposure, ACES tone mapping, a light grade, vignette.
const char* kCompositeFS = R"(#version 330 core
in vec2 vUv; out vec4 oCol;
uniform sampler2D uScene, uBloom, uAO, uDepth;
uniform float uBloomK, uAOK, uBlur, uBlurNorm, uExposure, uNight;
uniform vec2 uAOTexel, uNearFar;
uniform vec3 uSharpen;  // (x: strength, 0 off; yz: the scene's texel) a low render scale, sharpened as it's scaled up
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float linZ(vec2 uv){
  float d = texture(uDepth, uv).r * 2.0 - 1.0, n = uNearFar.x, f = uNearFar.y;
  return 2.0 * n * f / (f + n - d * (f - n));
}
float aoAt(vec2 uv){  // depth-aware 3x3 blur of the half-res AO
  float zc = linZ(uv), s = 0.0, w = 0.0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++) {
      vec2 t = uv + vec2(x, y) * uAOTexel;
      float wt = exp(-abs(linZ(t) - zc) / (0.02 * zc));
      s += texture(uAO, t).r * wt;
      w += wt;
    }
  return s / max(w, 1e-4);
}
vec3 aces(vec3 x){ return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main(){
  vec3 c = texture(uScene, vUv).rgb;
  if (uSharpen.x > 0.0) {  // (contrast-limited: the cross of neighbours pulls it apart, held within their range so edges don't ring)
    vec3 n0 = texture(uScene, vUv + vec2(uSharpen.y, 0.0)).rgb, n1 = texture(uScene, vUv - vec2(uSharpen.y, 0.0)).rgb;
    vec3 n2 = texture(uScene, vUv + vec2(0.0, uSharpen.z)).rgb, n3 = texture(uScene, vUv - vec2(0.0, uSharpen.z)).rgb;
    vec3 lo = min(min(min(n0, n1), min(n2, n3)), c), hi = max(max(max(n0, n1), max(n2, n3)), c);
    c = clamp(c + (c - 0.25 * (n0 + n1 + n2 + n3)) * uSharpen.x, lo, hi);
  }
  if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
  if (uAOK > 0.0) c *= mix(1.0, aoAt(vUv), uAOK);
  vec3 b = texture(uBloom, vUv).rgb;
  c += b * uBloomK;
  c = mix(c, b * uBlurNorm, uBlur);
  // Night vision: in the dark the eye loses color and sees blue (lamplit things keep theirs).
  float lum = dot(c, vec3(0.2126, 0.7152, 0.0722)) * uExposure;
  c = mix(c, vec3(lum / uExposure) * vec3(0.78, 0.92, 1.30), uNight * 0.45 * (1.0 - smoothstep(0.03, 0.4, lum)));
  c = aces(c * uExposure);
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = max(mix(vec3(l), c, 1.03), 0.0);
  vec2 q = vUv - 0.5;
  c *= 1.0 - dot(q, q) * 0.38;
  c = pow(c, vec3(1.0 / 2.2)) + (hash(gl_FragCoord.xy) - 0.5) / 255.0;
  oCol = vec4(c, 1.0);
})";

GLuint compileShader(GLenum type, const char* src, std::string& err) {
    GLuint s = CreateShader(type);
    ShaderSource(s, 1, &src, nullptr);
    CompileShader(s);
    GLint ok = 0;
    GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GetShaderInfoLog(s, sizeof(log), nullptr, log);
        err += log;
        return 0;
    }
    return s;
}

// ---- Linked programs kept on disk (shaderCacheDir()/<hash>.bin): the second launch skips compiling. The key is the
// sources and the driver (vendor, renderer, version); a binary the driver turns down is compiled afresh and replaced.
namespace {
std::string gShaderCacheDir;
int gBinaryFormats = -1;  // (-1: not asked yet)
uint64_t fnv(uint64_t h, const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
    return h;
}
bool binariesOk() {
    if (gShaderCacheDir.empty() || !GetProgramBinary || !ProgramBinary || !ProgramParameteri) return false;
    if (gBinaryFormats < 0) {
        gBinaryFormats = 0;
        GLint major = 0, minor = 0;
        GetIntegerv(0x821B, &major), GetIntegerv(0x821C, &minor);  // GL_MAJOR_VERSION, GL_MINOR_VERSION
        bool ext = major > 4 || (major == 4 && minor >= 1);
        GLint count = 0;
        GetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count && !ext; i++) {
            const char* name = (const char*)GetStringi(GL_EXTENSIONS, (GLuint)i);
            ext = name && !std::strcmp(name, "GL_ARB_get_program_binary");
        }
        if (ext) GetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &gBinaryFormats);
        std::printf("shader cache: %d binary formats%s\n", gBinaryFormats, gBinaryFormats > 0 ? "" : " (compiling every launch)");
    }
    return gBinaryFormats > 0;
}
std::string shaderCachePath(const std::string& vs, const std::string& fs) {
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, vs.data(), vs.size()), h = fnv(h, "|", 1), h = fnv(h, fs.data(), fs.size());
    for (GLenum e : {GL_VENDOR, GL_RENDERER, GL_VERSION}) {
        const char* t = (const char*)GetString(e);
        if (t) h = fnv(h, t, std::strlen(t));
    }
    char name[40];
    std::snprintf(name, sizeof name, "/%016llx.bin", (unsigned long long)h);
    return gShaderCacheDir + name;
}
GLuint loadCachedProgram(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    uint32_t format = 0;
    std::vector<char> data;
    if (std::fread(&format, 4, 1, f) == 1) {
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f) - 4;
        if (n > 0 && n < (64 << 20)) {
            data.resize((size_t)n);
            std::fseek(f, 4, SEEK_SET);
            if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        }
    }
    std::fclose(f);
    if (data.empty()) return 0;
    GLuint p = CreateProgram();
    ProgramBinary(p, format, data.data(), (GLsizei)data.size());
    GLint ok = 0;
    GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok) return p;
    DeleteProgram(p);  // (a new driver, or a corrupt file: compiled again below, and the file replaced)
    return 0;
}
void saveCachedProgram(GLuint p, const std::string& path) {
    GLint n = 0;
    GetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &n);
    if (n <= 0) return;
    std::vector<char> data((size_t)n);
    GLenum format = 0;
    GLsizei got = 0;
    GetProgramBinary(p, n, &got, &format, data.data());
    if (got <= 0) return;
    makeDir(gShaderCacheDir);
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const uint32_t fmt = format;
    const bool ok = std::fwrite(&fmt, 4, 1, f) == 1 && std::fwrite(data.data(), 1, (size_t)got, f) == (size_t)got;
    std::fclose(f);
    if (ok) {
        std::remove(path.c_str());  // (Windows won't rename over a file)
        std::rename(tmp.c_str(), path.c_str());
    } else std::remove(tmp.c_str());
}
}  // namespace

}  // namespace
void setShaderCacheDir(const std::string& dir) { gShaderCacheDir = dir; }
ShaderCacheStats gShaderCacheStats;
namespace {

GLuint linkProgram(const std::string& vsSrc, const std::string& fsSrc, std::string& err) {
    const bool cache = binariesOk();
    const std::string path = cache ? shaderCachePath(vsSrc, fsSrc) : std::string();
    if (cache)
        if (GLuint p = loadCachedProgram(path)) {
            gShaderCacheStats.hits++;
            return p;
        }
    const char* vs = vsSrc.c_str();
    const char* fs = fsSrc.c_str();
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, err);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, err);
    if (!v || !f) return 0;
    GLuint p = CreateProgram();
    AttachShader(p, v);
    AttachShader(p, f);
    if (cache) ProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, 1);
    LinkProgram(p);
    DeleteShader(v);
    DeleteShader(f);
    GLint ok = 0;
    GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GetProgramInfoLog(p, sizeof(log), nullptr, log);
        err += log;
        return 0;
    }
    gShaderCacheStats.compiled++;
    if (cache) saveCachedProgram(p, path);
    return p;
}

void setupVtxVao(GLuint vao, GLuint vbo) {
    BindVertexArray(vao);
    BindBuffer(GL_ARRAY_BUFFER, vbo);
    VertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(Vtx), (void*)0);
    VertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(Vtx), (void*)12);
    VertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(Vtx), (void*)24);
    EnableVertexAttribArray(0);
    EnableVertexAttribArray(1);
    EnableVertexAttribArray(2);
}

// Uploads a static mesh, creating its VAO and buffer the first time.
void uploadStatic(GLuint& vao, GLuint& vbo, const std::vector<Vtx>& v) {
    if (!vao) {
        GenVertexArrays(1, &vao);
        GenBuffers(1, &vbo);
        setupVtxVao(vao, vbo);
    }
    BindBuffer(GL_ARRAY_BUFFER, vbo);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
}

Vtx vtx(const V3& p, const V3& n, float r, float g, float b, int mat) {
    return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {(float)n.x, (float)n.y, (float)n.z}, {r, g, b, (float)mat}};
}

void uniformM4(GLuint prog, const char* name, const M4& m) {
    UniformMatrix4fv(uniformLocation(prog, name), 1, 0, m.m);
}
void uniformV3(GLuint prog, const char* name, const V3& v) {
    Uniform3f(uniformLocation(prog, name), (float)v.x, (float)v.y, (float)v.z);
}

// World transform for a wheel mesh (local Z = axle, +Z = outer face). The meshes are
// built for a 0.32 m x 0.24 m wheel and scaled to each vehicle's size.
M4 wheelMatrix(const Car& car, const Wheel& wh, const V3& up, bool spin, double radialScale = 1.0, double verticalScale = 0.0, double widthScale = 1.0) {
    const V3& a = car.nodes[wh.inner].p;
    const V3& b = car.nodes[wh.outer].p;
    V3 c = (a + b) * 0.5;
    V3 axis = norm(b - a);
    if (!wh.left) axis = -axis;  // points to the car's left (the knuckle steers it physically)
    V3 f0 = norm(cross(axis, up));
    if (len(f0) < 0.5) f0 = norm(cross(axis, V3(0, 1, 0)));
    V3 u0 = cross(f0, axis);
    double ang = spin ? wh.angle : 0.0;
    double radial = wh.R / 0.32 * radialScale, axial = wh.width / 0.24 * widthScale;
    double vertScale = verticalScale > 0 ? verticalScale : radialScale;
    V3 e1 = rotateAxis(f0, axis, ang) * radial;
    V3 e2 = rotateAxis(u0, axis, ang) * radial;
    const double squash = vertScale / radialScale - 1;
    e1 += u0 * (dot(e1, u0) * squash);
    e2 += u0 * (dot(e2, u0) * squash);
    V3 out = (wh.left ? axis : -axis) * axial;
    M4 m = m4identity();
    m.m[0] = (float)e1.x; m.m[1] = (float)e1.y; m.m[2] = (float)e1.z;
    m.m[4] = (float)e2.x; m.m[5] = (float)e2.y; m.m[6] = (float)e2.z;
    m.m[8] = (float)out.x; m.m[9] = (float)out.y; m.m[10] = (float)out.z;
    m.m[12] = (float)c.x; m.m[13] = (float)c.y; m.m[14] = (float)c.z;
    return m;
}

// ---- Mesh helpers for the wheel ----
struct MeshOut {
    std::vector<Vtx>& v;
    void tri(const V3& a, const V3& b, const V3& c, const V3& col, int mat) {
        V3 n = norm(cross(b - a, c - a));
        for (const V3& p : {a, b, c}) v.push_back(vtx(p, n, (float)col.x, (float)col.y, (float)col.z, mat));
    }
    void quad(const V3& a, const V3& b, const V3& c, const V3& d, const V3& col, int mat) {
        tri(a, b, c, col, mat);
        tri(a, c, d, col, mat);
    }
    // Hexahedron from 8 corners in bit order (bit0 = x, bit1 = y, bit2 = z), outward faces.
    void hexa(const V3 p[8], const V3& col, int mat) {
        quad(p[0], p[2], p[3], p[1], col, mat);
        quad(p[4], p[5], p[7], p[6], col, mat);
        quad(p[0], p[4], p[6], p[2], col, mat);
        quad(p[1], p[3], p[7], p[5], col, mat);
        quad(p[0], p[1], p[5], p[4], col, mat);
        quad(p[2], p[6], p[7], p[3], col, mat);
    }
    // Ring between radii r0 and r1 at axial z, facing +z (facing = 1) or -z.
    void annulus(double r0, double r1, double z, int segs, int facing, const V3& col, int mat, const V3& at = V3()) {
        for (int s = 0; s < segs; s++) {
            double a0 = 2 * M_PI * s / segs, a1 = 2 * M_PI * (s + 1) / segs;
            V3 p0 = at + V3(std::cos(a0) * r0, std::sin(a0) * r0, z), p1 = at + V3(std::cos(a1) * r0, std::sin(a1) * r0, z);
            V3 q0 = at + V3(std::cos(a0) * r1, std::sin(a0) * r1, z), q1 = at + V3(std::cos(a1) * r1, std::sin(a1) * r1, z);
            if (facing > 0) quad(p0, q0, q1, p1, col, mat);
            else quad(p0, p1, q1, q0, col, mat);
        }
    }
    // Cylinder around the axle between z0 and z1; inward = normals pointing at the axle.
    void barrel(double r, double z0, double z1, int segs, bool inward, const V3& col, int mat) {
        for (int s = 0; s < segs; s++) {
            double a0 = 2 * M_PI * s / segs, a1 = 2 * M_PI * (s + 1) / segs;
            V3 p0(std::cos(a0) * r, std::sin(a0) * r, z0), p1(std::cos(a1) * r, std::sin(a1) * r, z0);
            V3 q0(std::cos(a0) * r, std::sin(a0) * r, z1), q1(std::cos(a1) * r, std::sin(a1) * r, z1);
            if (inward) quad(p0, q0, q1, p1, col, mat);
            else quad(p0, p1, q1, q0, col, mat);
        }
    }
};

}  // namespace

bool Renderer::init(const World& world, std::string& err) {
    const std::string header = "#version 330 core\n";
    const double lpShaders0 = loadprof::nowMs();
    progLit = linkProgram(kLitVS, header + kCommonFS + kLitIns + kTerrainProcFS + kLitFSBody, err);
    progCar = linkProgram(kCarVS, header + kCommonFS + kCarFSBody, err);
    progDepth = linkProgram(kDepthVS, kDepthFS, err);
    progParts = linkProgram(std::string(kPartsXf) + kPartsVS, header + kCommonFS + kLitIns + kTerrainProcFS + kLitFSBody, err);
    progPartsDepth = linkProgram(std::string(kPartsXf) + kPartsDepthVS, kDepthFS, err);
    progTree = linkProgram(kTreeVS, header + kCommonFS + kLitIns + kTreeFSBody, err);
    progTreeDepth = linkProgram(kTreeDepthVS, kDepthFS, err);
    progLine = linkProgram(kLineVS, kLineFS, err);
    progXray = linkProgram(kXrayVS, kXrayFS, err);
    progSky = linkProgram(kSkyVS, header + kCommonFS + kSkyFSBody, err);
    progHud = linkProgram(kHudVS, kHudFS, err);
    {  // (menu pictures: if their shaders won't build, the cards go without)
        std::string ierr;
        progIcon = linkProgram(kHudVS, kIconFS, ierr);
        progIconMesh = linkProgram(kIconMeshVS, kIconMeshFS, ierr);
        if (!progIcon || !progIconMesh) std::fprintf(stderr, "icon shaders: %s\n", ierr.c_str()), progIcon = progIconMesh = 0;
    }
    progFx = linkProgram(kFxVS, header + kCommonFS + kFxFSBody, err);
    progAO = linkProgram(kPostVS, kAOFS, err);
    progDown = linkProgram(kPostVS, kDownFS, err);
    progUp = linkProgram(kPostVS, kUpFS, err);
    progComposite = linkProgram(kPostVS, kCompositeFS, err);
    progGrass = linkProgram(kGrassVS, header + kCommonFS + kGrassFSBody, err);
    progWater = linkProgram(kLitVS, header + kCommonFS + kWaterFSBody, err);
    // (Photo-scanned ground: a program per tier, built when first wanted (ensureGround); if one can't be built the terrain keeps its procedural look.)
    if (!progLit || !progDepth || !progParts || !progPartsDepth || !progLine || !progXray || !progSky || !progHud || !progFx || !progCar || !progAO || !progDown ||
        !progUp || !progComposite || !progGrass || !progWater || !progTree || !progTreeDepth)
        return false;
    loadprof::add("gl.shaders (main)", loadprof::nowMs() - lpShaders0);
    // (Scanned props: if their shaders won't build, the scenery stays code-made.)
    {
        LOADPROF("gl.shaders (props)");
        std::string perr;
        propsOk = props.init(kCommonFS, linkProgram, perr);
        if (!propsOk) std::fprintf(stderr, "prop shaders: %s\n", perr.c_str());
    }

    { LOADPROF("gl.wheel+font"); buildWheel(); buildFont(); }
    setWorld(world);

    GenVertexArrays(1, &dynVao);
    GenBuffers(1, &dynVbo);
    setupVtxVao(dynVao, dynVbo);
    GenVertexArrays(1, &lineVao);
    GenBuffers(1, &lineVbo);
    setupVtxVao(lineVao, lineVbo);
    GenVertexArrays(1, &fxVao);
    GenBuffers(1, &fxVbo);
    setupVtxVao(fxVao, fxVbo);
    GenVertexArrays(1, &skyVao);

    GenVertexArrays(1, &hudVao);
    GenBuffers(1, &hudVbo);
    BindVertexArray(hudVao);
    BindBuffer(GL_ARRAY_BUFFER, hudVbo);
    VertexAttribPointer(0, 2, GL_FLOAT, 0, 32, (void*)0);
    VertexAttribPointer(1, 2, GL_FLOAT, 0, 32, (void*)8);
    VertexAttribPointer(2, 4, GL_FLOAT, 0, 32, (void*)16);
    EnableVertexAttribArray(0);
    EnableVertexAttribArray(1);
    EnableVertexAttribArray(2);

    GenVertexArrays(1, &postVao);
    {  // The grass's blades: no vertex data at all, only which vertices (blade * 7 + corner) make the triangles.
        std::vector<unsigned short> idx;
        for (int plain = 1; plain >= 0; plain--)
            for (int b = 0; b < 8; b++) {
                const unsigned short o = (unsigned short)(b * 7);
                const unsigned short plainTris[9] = {0, 1, 2, 1, 3, 2, 2, 3, 6};
                const unsigned short fullTris[15] = {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4, 4, 5, 6};
                if (plain) for (unsigned short i : plainTris) idx.push_back(o + i);
                else for (unsigned short i : fullTris) idx.push_back(o + i);
            }
        for (int b = 0; b < 8; b++)  // (single: the base's corners and the tip)
            for (unsigned short i : {0, 1, 6}) idx.push_back((unsigned short)(b * 7 + i));
        GenVertexArrays(1, &grassVao);
        GenBuffers(1, &grassIbo);
        BindVertexArray(grassVao);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, grassIbo);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned short), idx.data(), GL_STATIC_DRAW);
        BindVertexArray(0);
    }
    ensureShadows(2048);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        err += "shadow framebuffer incomplete";
        return false;
    }
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

// (Re)creates both shadow cascades at size x size.
void Renderer::ensureShadows(int size) {
    if (size == shadowSize) return;
    shadowSize = size;
    for (int c = 0; c < 2; c++) {
        if (!shadowTex[c]) {
            GenTextures(1, &shadowTex[c]);
            GenFramebuffers(1, &shadowFbo[c]);
        }
        BindTexture(GL_TEXTURE_2D, shadowTex[c]);
        TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        noteRT(c ? "shadow cascade 1" : "shadow cascade 0", "D24", size, size, 4);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float white[4] = {1, 1, 1, 1};
        TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, white);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        BindFramebuffer(GL_FRAMEBUFFER, shadowFbo[c]);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex[c], 0);
        DrawBuffer(GL_NONE);
        ReadBuffer(GL_NONE);
    }
}

namespace {
GLuint makeTex(int w, int h, GLenum internal, GLenum format, GLenum type, GLenum filter) {
    GLuint t = 0;
    GenTextures(1, &t);
    BindTexture(GL_TEXTURE_2D, t);
    TexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
GLuint makeFbo(GLuint color, GLuint depth = 0) {
    GLuint f = 0;
    GenFramebuffers(1, &f);
    BindFramebuffer(GL_FRAMEBUFFER, f);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    const GLenum status = CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char msg[200];
        std::snprintf(msg, sizeof msg, "Could not create an OpenGL render target (status 0x%x). Try a smaller window or lower render scale.", status);
        throw std::runtime_error(msg);
    }
    return f;
}
}  // namespace

// (Re)creates the HDR scene targets for a w x h scene with the given MSAA sample count.
void Renderer::ensureTargets(int w, int h, int samples) {
    GLint maxSamples = 0;
    GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples = samples > 1 ? std::min(samples, (int)maxSamples) : 0;
    if (w == targetW && h == targetH && samples == targetRequestedSamples) return;
    GLuint fbos[] = {msFbo, sceneFbo, aoFbo}, rbs[] = {msColor, msDepth}, texs[] = {sceneTex, depthTex, aoTex};
    DeleteFramebuffers(3, fbos);
    DeleteRenderbuffers(2, rbs);
    DeleteTextures(3, texs);
    DeleteFramebuffers(kBloomLevels, bloomFbo);
    DeleteTextures(kBloomLevels, bloomTex);
    msFbo = msColor = msDepth = 0;
    targetW = w, targetH = h, targetRequestedSamples = samples;

    sceneTex = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    depthTex = makeTex(w, h, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
    sceneFbo = makeFbo(sceneTex, depthTex);
    // GL_MAX_SAMPLES is a ceiling, not a guarantee for this color/depth combination.
    // Some drivers support fewer samples for floating-point HDR. Retry down to no MSAA.
    int actualSamples = samples;
    while (actualSamples > 1) {
        GenRenderbuffers(1, &msColor);
        BindRenderbuffer(GL_RENDERBUFFER, msColor);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, actualSamples, GL_RGBA16F, w, h);
        GenRenderbuffers(1, &msDepth);
        BindRenderbuffer(GL_RENDERBUFFER, msDepth);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, actualSamples, GL_DEPTH_COMPONENT32F, w, h);
        GenFramebuffers(1, &msFbo);
        BindFramebuffer(GL_FRAMEBUFFER, msFbo);
        FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
        if (CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) break;
        std::fprintf(stderr, "HDR MSAA %dx unavailable at %dx%d; retrying with fewer samples\n", actualSamples, w, h);
        DeleteFramebuffers(1, &msFbo);
        GLuint failed[] = {msColor, msDepth};
        DeleteRenderbuffers(2, failed);
        msFbo = msColor = msDepth = 0;
        actualSamples /= 2;
    }
    targetSamples = actualSamples > 1 ? actualSamples : 0;
    std::printf("HDR target: %dx%d, MSAA %dx (requested %dx)\n", w, h, targetSamples, samples);
    aoW = std::max(1, w / 2), aoH = std::max(1, h / 2);
    aoTex = makeTex(aoW, aoH, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
    aoFbo = makeFbo(aoTex);
    noteRT("scene color", "RGBA16F", w, h, 8), noteRT("scene depth", "D32F", w, h, 4), noteRT("ambient occlusion", "R8", aoW, aoH, 1);
    if (targetSamples) texcomp::note("MSAA buffers", "RGBA16F+D32F", w, h, 0, (size_t)w * h * 12 * targetSamples, true);
    size_t bloomBytes = 0;
    for (int l = 0; l < kBloomLevels; l++) bloomBytes += (size_t)std::max(1, w >> (l + 1)) * std::max(1, h >> (l + 1)) * 4;
    texcomp::note("bloom chain", "R11G11B10F", w / 2, h / 2, 0, bloomBytes, true);
    for (int l = 0; l < kBloomLevels; l++) {
        bloomW[l] = std::max(1, w >> (l + 1));
        bloomH[l] = std::max(1, h >> (l + 1));
        bloomTex[l] = makeTex(bloomW[l], bloomH[l], GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, GL_LINEAR);
        bloomFbo[l] = makeFbo(bloomTex[l]);
    }
    BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Resolves the scene, then: ambient occlusion, the bloom chain, and the tone-mapped
// composite into the window.
void Renderer::postProcess(const View& view, const M4& proj) {
    const RenderSettings& rs = settings;
    const int w = targetW, h = targetH;
    if (targetSamples) {
        BindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
        BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
        BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
    Disable(GL_DEPTH_TEST);
    Disable(GL_BLEND);
    BindVertexArray(postVao);
    const float nearZ = 0.05f, farZ = farZ_;

    const bool ao = fadeAO_ > 0, bloom = fadeBloom_ > 0;
    if (ao) {
        BindFramebuffer(GL_FRAMEBUFFER, aoFbo);
        Viewport(0, 0, aoW, aoH);
        UseProgram(progAO);
        M4 invProj;
        invert(proj, invProj);
        uniformM4(progAO, "uInvProj", invProj);
        Uniform2f(uniformLocation(progAO, "uTexel"), 1.0f / w, 1.0f / h);
        Uniform2f(uniformLocation(progAO, "uAOTexel"), 1.0f / aoW, 1.0f / aoH);
        Uniform1f(uniformLocation(progAO, "uProjScale"), aoH / (2.0f * std::tan(view.fovy * 0.5f)));
        ActiveTexture(GL_TEXTURE0);
        BindTexture(GL_TEXTURE_2D, depthTex);
        Uniform1i(uniformLocation(progAO, "uDepth"), 0);
        DrawArrays(GL_TRIANGLES, 0, 3);
    }

    const bool chain = bloom || rs.blur > 0;
    if (chain) {
        UseProgram(progDown);
        Uniform1i(uniformLocation(progDown, "uSrc"), 0);
        ActiveTexture(GL_TEXTURE0);
        for (int l = 0; l < kBloomLevels; l++) {
            BindFramebuffer(GL_FRAMEBUFFER, bloomFbo[l]);
            Viewport(0, 0, bloomW[l], bloomH[l]);
            BindTexture(GL_TEXTURE_2D, l ? bloomTex[l - 1] : sceneTex);
            int sw = l ? bloomW[l - 1] : w, sh = l ? bloomH[l - 1] : h;
            Uniform2f(uniformLocation(progDown, "uTexel"), 1.0f / sw, 1.0f / sh);
            Uniform1f(uniformLocation(progDown, "uThreshold"), l == 0 && rs.blur <= 0 ? 1.2f : -1.0f);
            DrawArrays(GL_TRIANGLES, 0, 3);
        }
        UseProgram(progUp);
        Uniform1i(uniformLocation(progUp, "uSrc"), 0);
        Enable(GL_BLEND);
        BlendFunc(GL_ONE, GL_ONE);
        for (int l = kBloomLevels - 2; l >= 0; l--) {
            BindFramebuffer(GL_FRAMEBUFFER, bloomFbo[l]);
            Viewport(0, 0, bloomW[l], bloomH[l]);
            BindTexture(GL_TEXTURE_2D, bloomTex[l + 1]);
            Uniform2f(uniformLocation(progUp, "uTexel"), 1.0f / bloomW[l + 1], 1.0f / bloomH[l + 1]);
            DrawArrays(GL_TRIANGLES, 0, 3);
        }
        Disable(GL_BLEND);
    }

    BindFramebuffer(GL_FRAMEBUFFER, 0);
    Viewport(0, 0, view.width, view.height);
    UseProgram(progComposite);
    const GLuint texs[4] = {sceneTex, bloomTex[0], aoTex, depthTex};
    const char* names[4] = {"uScene", "uBloom", "uAO", "uDepth"};
    for (int k = 0; k < 4; k++) {
        ActiveTexture(GL_TEXTURE0 + k);
        BindTexture(GL_TEXTURE_2D, texs[k]);
        Uniform1i(uniformLocation(progComposite, names[k]), k);
    }
    ActiveTexture(GL_TEXTURE0);
    // The chain sums kBloomLevels blurred copies: normalize it here.
    // (More glow round the lamps at night, when the eye is open wide.)
    Uniform1f(uniformLocation(progComposite, "uBloomK"), bloom && rs.blur <= 0 ? fadeBloom_ * 0.08f * (1 + 0.8f * daylight.night) / kBloomLevels : 0.0f);
    Uniform1f(uniformLocation(progComposite, "uNight"), daylight.night);
    Uniform1f(uniformLocation(progComposite, "uBlur"), chain ? rs.blur : 0.0f);
    Uniform1f(uniformLocation(progComposite, "uBlurNorm"), 0.8f / kBloomLevels);
    Uniform1f(uniformLocation(progComposite, "uAOK"), ao ? 0.75f * fadeAO_ : 0.0f);
    Uniform1f(uniformLocation(progComposite, "uExposure"), daylight.exposure);
    Uniform2f(uniformLocation(progComposite, "uAOTexel"), 1.0f / aoW, 1.0f / aoH);
    Uniform2f(uniformLocation(progComposite, "uNearFar"), nearZ, farZ);
    Uniform3f(uniformLocation(progComposite, "uSharpen"), rs.sharpen ? 0.8f : 0.0f, 1.0f / w, 1.0f / h);
    DrawArrays(GL_TRIANGLES, 0, 3);
    Enable(GL_DEPTH_TEST);
}

namespace {
// What the scenery looks like: the physics terrain, with crags carved into the rim
// mountains (too steep to drive, so the physics keeps them smooth), and beyond the map a
// range of mountains to the horizon.
double sceneryHeight(const World& world, double x, double z) {
    const double half = World::kHalfSize;
    if (world.wrapPeriod > 0 && std::fabs(z) < half - 80) {
        // The freeway: the valley and the road run on beyond the map, a period at a time.
        const double P = world.wrapPeriod;
        const double fx = x - P * std::floor(x / P + 0.5);
        return world.height(fx, z);
    }
    double cx = clampd(x, -half, half), cz = clampd(z, -half, half);
    double h = world.height(cx, cz);
    double ex = std::max(0.0, std::fabs(cx) - 520), ez = std::max(0.0, std::fabs(cz) - 520);
    double e = std::sqrt(ex * ex + ez * ez);
    if (e > 20) h += smoothstep(20, 70, e) * (ridgedNoise(cx * 0.008, cz * 0.008, 5) - 0.45) * 0.5 * (0.02 * e * e);
    double d = std::max(std::fabs(x) - half, std::fabs(z) - half);
    if (d > 0) {
        double range = 40 + 300 * ridgedNoise(x * 0.0011 + 3.7, z * 0.0011 - 1.3, 6) * smoothstep(0, 900, d);
        h = h + (range + 0.3 * h - h) * smoothstep(0, 400, d);
    }
    return h;
}
}  // namespace

void Renderer::setWorld(const World& world) {
    LOADPROF("renderer.setWorld");
    mapId = world.map == MAP_WILDS ? (int)MAP_FREEWAY : world.map;  // (the wilds are painted as the freeway's meadows are: land and paints, no pad or road)
    { LOADPROF("terrain.build+upload"); buildTerrain(world); }
    treeSig_ = scenerySig_ = bushSig_ = 0;  // (a new world: everything is built afresh)
    updateObjects(world);
    updateWater(world);
}

namespace {
// A running hash of what a mesh is built from (FNV-1a over the values).
struct Sig {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    }
    void num(double v) { bytes(&v, sizeof v); }
    void vec(const V3& v) { num(v.x), num(v.y), num(v.z); }
    void str(const std::string& t) { bytes(t.data(), t.size()), num((double)t.size()); }
};
// What every scenery mesh shares: the world, its hand-edited cells, the props quality and what the scans cover.
Sig sceneryBase(const World& w, int props, const ScanCover& c) {
    Sig s;
    const void* wp = &w;
    s.bytes(&wp, sizeof wp);
    s.num((double)w.map), s.num((double)w.cellEdits), s.num(props);
    s.num(c.pines), s.num(c.snags), s.num(c.rocks), s.num(c.logs), s.num(c.bushes);
    for (const Water& wt : w.waters) s.num(wt.x), s.num(wt.z), s.num(wt.r), s.num(wt.y);
    return s;
}
}  // namespace

void Renderer::updateObjects(const World& world) {
    for (int s = 0; s < 4; s++) updateObjectsStage(world, s);
}

// One of four blocks of updateObjects: 0 the scanned props and boxes, 1 the trees, 2 the bushes, 3 the rocks and models. (A map
// switch does one per frame; stage 0 sets the quality and cover the others use.)
void Renderer::updateObjectsStage(const World& world, int stage) {
    // The scanned props first: whatever they cover, the code-made meshes leave out.
    if (stage == 0) stagePq_ = propsOk ? std::min(std::max(settings.props, 0), 3) : 0;
    const int pq = stagePq_;
    const ScanCover cover = scanCover_;
    if (stage == 0) {
    if (pq) { LOADPROF("props.ensureLoaded"); props.ensureLoaded(pq); }
    { LOADPROF("props.setWorld"); props.setWorld(world, pq); }
    scanCover_ = props.cover(pq);
    treeFade_.assign(world.trees.size(), 0.0f);
    propsApplied = settings.props;
    buildBoxes(world);
    return;
    }
    // (The trees and the rest of the scenery are only rebuilt when something they're made from changed: moving a block in the
    // editor leaves the forest alone, so a drag keeps up with the mouse.)
    if (stage == 1) {
    Sig ts = sceneryBase(world, pq, cover);
    for (const Tree& t : world.trees) ts.vec(t.base), ts.num(t.height), ts.num(t.radius), ts.num(t.kind), ts.num(t.tint);
    if (ts.h != treeSig_ || treeVertexCount == 0) {
        treeSig_ = ts.h;
        forest::Set set;
        { LOADPROF("trees.generate"); forest::build(world, cover.pines, cover.snags, set); }
        trees_.build(set);
        treeVertexCount = (int)trees_.meshVertices();
    }
    return;
    }
    if (stage == 2) {
    Sig bs = sceneryBase(world, pq, cover);
    for (const Bush& b : world.bushes) bs.vec(b.p), bs.num(b.r), bs.num(b.tint), bs.num(b.kind);
    for (const Log& g : world.logs) bs.vec(g.a), bs.vec(g.b), bs.num(g.r);
    if (bs.h != bushSig_ || bushVertexCount == 0) {
        bushSig_ = bs.h;
        forest::Set set;
        if (!cover.bushes) forest::buildBushes(world, set);
        bushes_.build(set);
        std::vector<Vtx> v;
        { LOADPROF("bushes.mesh"); buildBushMesh(world, v, cover); }
        LOADPROF("bushes.bin+upload");
        uploadBatches(bushVao, bushVbo, v, bushBatches);
        bushVertexCount = (int)v.size();
    }
    return;
    }
    Sig ss = sceneryBase(world, pq, cover);
    for (const Rock& k : world.rocks) ss.vec(k.c), ss.num(k.r), ss.num(k.tint), ss.num(k.scan), ss.num((double)k.v.size()), ss.vec(k.v.empty() ? V3() : k.v[0]);
    for (const Box& b : world.boxes)
        if (b.rock) ss.vec(b.c), ss.vec(b.h), ss.vec(b.ax[0]), ss.vec(b.ax[1]);
    for (const Log& g : world.logs) ss.vec(g.a), ss.vec(g.b), ss.num(g.r);
    for (const Prop& f : world.fixtures) ss.num(f.kind), ss.vec(f.a), ss.vec(f.b), ss.num(f.yaw), ss.num(f.col[0]), ss.num(f.col[1]), ss.num(f.col[2]);
    for (const Placed& m : world.models) {
        ss.str(m.id), ss.vec(m.p), ss.num(m.yaw), ss.vec(m.scale), ss.num(m.variant), ss.num(m.oriented), ss.num(m.wear);
        for (int i = 0; i < 3; i++) ss.vec(m.ax[i]);
        for (const auto& kv : m.set) ss.str(kv.first), ss.num(kv.second);
    }
    if (ss.h != scenerySig_ || sceneryVertexCount == 0) {
        scenerySig_ = ss.h;
        std::vector<Vtx> v;
        { LOADPROF("rocks.mesh"); buildRockMesh(world, v, cover); }
        { LOADPROF("models.mesh (props)"); buildPropMesh(world, v); }
        LOADPROF("scenery.bin+upload");
        uploadBatches(sceneryVao, sceneryVbo, v, sceneryBatches);
        sceneryVertexCount = (int)v.size();
    }
}

// Reorder complete triangles into spatial bins. Bounds include all three
// vertices, so large objects crossing bin edges remain conservatively visible.
void Renderer::uploadBatches(GLuint& vao, GLuint& vbo, std::vector<Vtx>& vertices,
                             std::vector<StaticBatch>& batches) {
    std::map<std::pair<int, int>, std::vector<Vtx>> bins;
    for (size_t k = 0; k + 2 < vertices.size(); k += 3) {
        const float x = (vertices[k].p[0] + vertices[k+1].p[0] + vertices[k+2].p[0]) / 3;
        const float z = (vertices[k].p[2] + vertices[k+1].p[2] + vertices[k+2].p[2]) / 3;
        auto& bin = bins[{(int)std::floor(x / 64), (int)std::floor(z / 64)}];
        bin.insert(bin.end(), vertices.begin() + k, vertices.begin() + k + 3);
    }
    vertices.clear();
    batches.clear();
    for (const auto& entry : bins) {
        StaticBatch batch;
        batch.first = (int)vertices.size();
        batch.count = (int)entry.second.size();
        for (const auto& v : entry.second) batch.bounds.add(V3(v.p[0], v.p[1], v.p[2]));
        vertices.insert(vertices.end(), entry.second.begin(), entry.second.end());
        batches.push_back(batch);
    }
    uploadStatic(vao, vbo, vertices);
}

void Renderer::drawBatches(GLuint vao, const std::vector<StaticBatch>& batches, const M4& vp, bool shadow) {
    BindVertexArray(vao);
    for (const auto& batch : batches) {
        if (!batch.bounds.visible(vp) || (!shadow && occl_.occluded(batch.bounds))) continue;
        DrawArrays(GL_TRIANGLES, batch.first, batch.count);
        (shadow ? geometryStats_.shadowSceneryVertices : geometryStats_.sceneryVertices) += batch.count;
        (shadow ? geometryStats_.shadowModelVertices : geometryStats_.modelVertices) += batch.count;
        (shadow ? geometryStats_.shadowModelDraws : geometryStats_.modelDraws)++;
    }
}

// Reeds and logs are small and many: the batches (64 m squares) nearer than 300 m, and for shadows 80 m (a reed's shadow past that is a speck).
void Renderer::drawBushes(const M4& vp, const V3& eye, bool shadow, VegStats::Pass& stats) {
    const double reach = (shadow ? 80 : 300) * vegDetail();
    BindVertexArray(bushVao);
    for (const auto& batch : bushBatches) {
        if (batch.bounds.distance(eye) > reach || !batch.bounds.visible(vp) || (!shadow && occl_.occluded(batch.bounds))) continue;
        DrawArrays(GL_TRIANGLES, batch.first, batch.count);
        (shadow ? geometryStats_.shadowSceneryVertices : geometryStats_.sceneryVertices) += batch.count;
        stats.add(1, batch.count / 3);
    }
}

void Renderer::drawTerrain(const M4& vp, const View& view) {
    BindVertexArray(terrainVao);
    geometryStats_.terrainChunks = (unsigned)terrainChunks.size();
    // Keep collision-scale detail nearby; select distance from the nearest point
    // of the chunk, not its center, to protect terrain under the camera.
    // Nearest first, so a hill hides the land behind it before that's shaded.
    static std::vector<std::pair<float, int>> terrainOrder_;  // (kept: no allocation a frame)
    terrainOrder_.clear();
    for (size_t i = 0; i < terrainChunks.size(); i++) {
        const auto& chunk = terrainChunks[i];
        geometryStats_.terrainFullTriangles += chunk.count[0] / 3;
        if (!chunk.bounds.visible(vp)) continue;
        if (occl_.occluded(chunk.bounds)) { ++geometryStats_.terrainOccluded; continue; }
        terrainOrder_.push_back({(float)chunk.bounds.distance(view.eye), (int)i});
    }
    std::sort(terrainOrder_.begin(), terrainOrder_.end());
    for (const auto& o : terrainOrder_) {
        const auto& chunk = terrainChunks[o.second];
        const double distance = o.first;
        const int lod = terrainLodForDistance(distance);
        DrawElements(GL_TRIANGLES, chunk.count[lod], GL_UNSIGNED_INT,
                     reinterpret_cast<const void*>((uintptr_t)chunk.first[lod] * sizeof(unsigned)));
        geometryStats_.terrainTriangles += chunk.count[lod] / 3;
        ++geometryStats_.terrainVisibleChunks;
    }
}

void Renderer::updateWater(const World& world) {
    std::vector<Vtx> wv;
    buildWaterMesh(world, wv);
    uploadStatic(waterVao, waterVbo, wv);
    waterVertexCount = (int)wv.size();

    // Terrain data for the shaders, one texel per meter: height, grass density, paint.
    const double half = World::kHalfSize;
    const int n = (int)(2 * half) + 1;
    terrainGridN = n;
    std::vector<float> data((size_t)n * n * 4);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            double x = -half + i, z = -half + j;
            float* t = &data[((size_t)j * n + i) * 4];
            t[0] = (float)world.height(x, z);
            t[1] = world.grassDensity(x, z);
            t[2] = (float)world.roadAt(x, z).paint;
            t[3] = (float)std::max(world.waterLevel(x, z), -1000.0);
        }
    if (!terrainTex) GenTextures(1, &terrainTex);
    BindTexture(GL_TEXTURE_2D, terrainTex);
    TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, n, n, 0, GL_RGBA, GL_FLOAT, data.data());
    noteRT("terrain height map", "RGBA32F", n, n, 16, 0, true);  // (data, read texel by texel: exact, no mips)
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// One terrain vertex (the mesh's grid point (i, j)), as buildTerrain makes it.
static Vtx terrainVertex(const World& world, double x, double z) {
    RoadInfo road = world.roadAt(x, z);
    V3 nrm = world.normal(x, z);
    double y = world.height(x, z);
    if ((std::fabs(x) > 540 && world.wrapPeriod <= 0) || std::fabs(z) > 540) {  // the crags (not across the freeway)
        y = sceneryHeight(world, x, z);
        const double e = 1.0;
        nrm = norm(V3(sceneryHeight(world, x - e, z) - sceneryHeight(world, x + e, z), 2 * e, sceneryHeight(world, x, z - e) - sceneryHeight(world, x, z + e)));
    }
    return vtx(V3(x, y, z), nrm, road.near ? 1.0f : 0.0f, road.lateral, road.along, SM_TERRAIN);
}

void Renderer::updateTerrainRegion(const World& world, int i0, int j0, int i1, int j1) {
    if (!terrainVao || terrainGridN <= 0) return;
    const double half = World::kHalfSize;
    // (One past the edit each way: the normals there changed too.)
    i0 = std::max(0, i0 - 2), j0 = std::max(0, j0 - 2), i1 = std::min(terrainGridN - 1, i1 + 2), j1 = std::min(terrainGridN - 1, j1 + 2);
    if (i1 < i0 || j1 < j0) return;
    // The mesh (its own spacing).
    const double sp = terrainSpacing;
    const int n = terrainMeshN;
    const int mi0 = std::max(0, (int)std::floor(i0 / sp)), mi1 = std::min(n - 1, (int)std::ceil(i1 / sp));
    const int mj0 = std::max(0, (int)std::floor(j0 / sp)), mj1 = std::min(n - 1, (int)std::ceil(j1 / sp));
    BindBuffer(GL_ARRAY_BUFFER, terrainVbo);
    std::vector<Vtx> row;
    for (int i = mi0; i <= mi1; i++) {
        row.clear();
        for (int j = mj0; j <= mj1; j++) row.push_back(terrainVertex(world, -half + i * sp, -half + j * sp)), occl_.patch(i, j, row.back().p[1]);
        BufferSubData(GL_ARRAY_BUFFER, (GLintptr)(((size_t)i * n + mj0) * sizeof(Vtx)), (GLsizeiptr)(row.size() * sizeof(Vtx)), row.data());
    }
    // Bounds must include raised/lowered editor vertices before the next culling pass.
    for (auto& chunk : terrainChunks) {
        if (chunk.bounds.hi.x < -half + mi0 * sp || chunk.bounds.lo.x > -half + mi1 * sp ||
            chunk.bounds.hi.z < -half + mj0 * sp || chunk.bounds.lo.z > -half + mj1 * sp) continue;
        const int xi0 = std::max(mi0, (int)std::ceil((chunk.bounds.lo.x + half) / sp));
        const int xi1 = std::min(mi1, (int)std::floor((chunk.bounds.hi.x + half) / sp));
        const int zj0 = std::max(mj0, (int)std::ceil((chunk.bounds.lo.z + half) / sp));
        const int zj1 = std::min(mj1, (int)std::floor((chunk.bounds.hi.z + half) / sp));
        for (int i = xi0; i <= xi1; ++i)
            for (int j = zj0; j <= zj1; ++j) {
                const auto v = terrainVertex(world, -half + i * sp, -half + j * sp);
                chunk.bounds.lo.y = std::min(chunk.bounds.lo.y, (double)v.p[1]);
                chunk.bounds.hi.y = std::max(chunk.bounds.hi.y, (double)v.p[1]);
            }
    }
    // The shaders' terrain texture (one texel per meter: x across, z down).
    std::vector<float> data((size_t)(i1 - i0 + 1) * (j1 - j0 + 1) * 4);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            const double x = -half + i, z = -half + j;
            float* t = &data[((size_t)(j - j0) * (i1 - i0 + 1) + (i - i0)) * 4];
            t[0] = (float)world.height(x, z);
            t[1] = world.grassDensity(x, z);
            t[2] = (float)world.roadAt(x, z).paint;
            t[3] = (float)std::max(world.waterLevel(x, z), -1000.0);
        }
    BindTexture(GL_TEXTURE_2D, terrainTex);
    TexSubImage2D(GL_TEXTURE_2D, 0, i0, j0, i1 - i0 + 1, j1 - j0 + 1, GL_RGBA, GL_FLOAT, data.data());
    props.terrainChanged(-half + i0, -half + j0, -half + i1, -half + j1);  // (grass doesn't grow on a new trail)
}
// The CPU half of the terrain: vertices, the chunks' indices at four LODs, the backdrop and the occlusion pyramid. Pure: any thread.
void Renderer::prepareTerrain(const World& world, Prepared& p) {
    // 1 m on the mountain (its crawl features are small) and the wilds (the sections' steps), 2 m on the proving ground.
    const double spacing = world.map == MAP_MOUNTAIN || world.map == MAP_FRONTIER || world.map == MAP_BACKCOUNTRY || world.map == MAP_WILDS ? 1.0 : 2.0, half = World::kHalfSize;
    const int n = (int)(2 * half / spacing) + 1;
    p.spacing = spacing;
    p.n = n;
    std::vector<Vtx>& verts = p.verts;
    std::vector<unsigned>& idx = p.idx;
    std::vector<TerrainChunk>& chunks = p.chunks;
    verts.reserve(n * n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            verts.push_back(terrainVertex(world, -half + i * spacing, -half + j * spacing));
        }
    // Keep every boundary vertex at all LODs so adjacent chunks share exact edges.
    for (int i = 0; i < n - 1; i += 64)
        for (int j = 0; j < n - 1; j += 64) {
            const int ie = std::min(i + 64, n - 1), je = std::min(j + 64, n - 1);
            TerrainChunk chunk;
            for (int x = i; x <= ie; ++x)
                for (int z = j; z <= je; ++z) {
                    const auto& v = verts[x * n + z];
                    chunk.bounds.add(V3(v.p[0], v.p[1], v.p[2]));
                }
            for (int lod = 0; lod < 4; ++lod) {
                chunk.first[lod] = (unsigned)idx.size();
                appendTerrainLod(idx, n, i, j, ie, je, 1 << lod);
                chunk.count[lod] = (unsigned)idx.size() - chunk.first[lod];
            }
            chunks.push_back(chunk);
        }
    // Backdrop is already coarse; spatially batch it for camera culling too.
    const unsigned backdropFirst = (unsigned)idx.size();
    buildBackdrop(world, verts, idx);
    std::map<std::pair<int, int>, std::vector<unsigned>> backdrop;
    for (unsigned k = backdropFirst; k < idx.size(); k += 3) {
        const auto& v = verts[idx[k]];
        auto& bin = backdrop[{(int)std::floor(v.p[0] / 384), (int)std::floor(v.p[2] / 384)}];
        bin.insert(bin.end(), {idx[k], idx[k + 1], idx[k + 2]});
    }
    idx.resize(backdropFirst);
    for (const auto& entry : backdrop) {
        TerrainChunk chunk;
        for (unsigned index : entry.second) {
            const auto& v = verts[index];
            chunk.bounds.add(V3(v.p[0], v.p[1], v.p[2]));
        }
        for (int lod = 0; lod < 4; ++lod) {
            chunk.first[lod] = (unsigned)idx.size();
            chunk.count[lod] = (unsigned)entry.second.size();
        }
        idx.insert(idx.end(), entry.second.begin(), entry.second.end());
        chunks.push_back(chunk);
    }
    std::vector<float> hs(verts.size());
    for (size_t k = 0; k < verts.size(); k++) hs[k] = verts[k].p[1];
    p.occl.build(hs, n, spacing, -half);
}

void Renderer::buildTerrain(const World& world) {
    Prepared p;
    prepareTerrain(world, p);
    terrainSpacing = p.spacing;
    terrainMeshN = p.n;
    terrainChunks = std::move(p.chunks);
    occl_ = std::move(p.occl);
    if (!terrainVao) {
        GenVertexArrays(1, &terrainVao);
        GenBuffers(1, &terrainVbo);
        GenBuffers(1, &terrainIbo);
        setupVtxVao(terrainVao, terrainVbo);
    }
    BindVertexArray(terrainVao);
    BindBuffer(GL_ARRAY_BUFFER, terrainVbo);
    BufferData(GL_ARRAY_BUFFER, p.verts.size() * sizeof(Vtx), p.verts.data(), GL_DYNAMIC_DRAW);  // (the editor patches it)
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainIbo);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, p.idx.size() * sizeof(unsigned), p.idx.data(), GL_STATIC_DRAW);
}

// ---- A map switch without a frozen frame (render.h: prepareWorld) ----

std::shared_ptr<Renderer::Prepared> Renderer::prepareWorld(const World& world, const ScanCover& cover) {
    auto p = std::make_shared<Prepared>();
    prepareTerrain(world, *p);
    buildWaterMesh(world, p->water);
    {  // (builds the placed models' meshes, which the scenery stage then finds made)
        LOADPROF("models.warm (worker)");
        std::vector<Vtx> scratch;
        buildPropMesh(world, scratch);
        forest::Set set;  // (and the trees' models, which the tree stage would otherwise make on the frame thread)
        forest::build(world, cover.pines, cover.snags, set);
    }
    // The shaders' terrain texels, one per meter: height, grass density, paint, water level.
    const double half = World::kHalfSize;
    const int n = (int)(2 * half) + 1;
    p->gridN = n;
    p->data.resize((size_t)n * n * 4);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            double x = -half + i, z = -half + j;
            float* t = &p->data[((size_t)j * n + i) * 4];
            t[0] = (float)world.height(x, z);
            t[1] = world.grassDensity(x, z);
            t[2] = (float)world.roadAt(x, z).paint;
            t[3] = (float)std::max(world.waterLevel(x, z), -1000.0);
        }
    return p;
}

void Renderer::beginUpload(std::shared_ptr<Prepared> p) {
    pending_ = std::move(p);
    pending_->stage = 0;
}

bool Renderer::pumpUpload(double budgetMs) {
    Prepared* p = pending_.get();
    if (!p) return true;
    const double t0 = loadprof::nowMs();
    auto over = [&] { return loadprof::nowMs() - t0 >= budgetMs; };
    const size_t slice = 1 << 20;  // a megabyte at a time
    if (p->stage == 0) {
        GenBuffers(1, &p->vbo);
        GenBuffers(1, &p->ibo);
        BindBuffer(GL_ARRAY_BUFFER, p->vbo);
        BufferData(GL_ARRAY_BUFFER, p->verts.size() * sizeof(Vtx), nullptr, GL_DYNAMIC_DRAW);  // (the editor patches it)
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, p->idx.size() * sizeof(unsigned), nullptr, GL_STATIC_DRAW);
        GenTextures(1, &p->tex);
        BindTexture(GL_TEXTURE_2D, p->tex);
        TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, p->gridN, p->gridN, 0, GL_RGBA, GL_FLOAT, nullptr);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p->stage = 1;
        if (over()) return false;
    }
    if (p->stage == 1) {
        const size_t total = p->verts.size() * sizeof(Vtx);
        BindBuffer(GL_ARRAY_BUFFER, p->vbo);
        while (p->vDone < total) {
            const size_t k = std::min(slice, total - p->vDone);
            BufferSubData(GL_ARRAY_BUFFER, (GLintptr)p->vDone, (GLsizeiptr)k, (const char*)p->verts.data() + p->vDone);
            p->vDone += k;
            if (over()) return false;
        }
        p->stage = 2;
    }
    if (p->stage == 2) {
        const size_t total = p->idx.size() * sizeof(unsigned);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
        while (p->iDone < total) {
            const size_t k = std::min(slice, total - p->iDone);
            BufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)p->iDone, (GLsizeiptr)k, (const char*)p->idx.data() + p->iDone);
            p->iDone += k;
            if (over()) return false;
        }
        p->stage = 3;
    }
    if (p->stage == 3) {
        BindTexture(GL_TEXTURE_2D, p->tex);
        const int rows = std::max(1, (int)(slice / ((size_t)p->gridN * 16)));
        while (p->rowsDone < p->gridN) {
            const int k = std::min(rows, p->gridN - p->rowsDone);
            TexSubImage2D(GL_TEXTURE_2D, 0, 0, p->rowsDone, p->gridN, k, GL_RGBA, GL_FLOAT, &p->data[(size_t)p->rowsDone * p->gridN * 4]);
            p->rowsDone += k;
            if (over()) return false;
        }
        std::vector<Vtx>().swap(p->verts);
        std::vector<unsigned>().swap(p->idx);
        std::vector<float>().swap(p->data);
        p->stage = 4;
    }
    return true;
}

void Renderer::adoptWorld(const World& world) {
    LOADPROF("renderer.adoptWorld");
    std::shared_ptr<Prepared> p = std::move(pending_);
    mapId = world.map == MAP_WILDS ? (int)MAP_FREEWAY : world.map;
    if (terrainVbo) DeleteBuffers(1, &terrainVbo);
    if (terrainIbo) DeleteBuffers(1, &terrainIbo);
    if (terrainTex) DeleteTextures(1, &terrainTex);
    terrainVbo = p->vbo, terrainIbo = p->ibo, terrainTex = p->tex;
    terrainSpacing = p->spacing, terrainMeshN = p->n, terrainGridN = p->gridN;
    terrainChunks = std::move(p->chunks);
    occl_ = std::move(p->occl);
    BindVertexArray(terrainVao);
    setupVtxVao(terrainVao, terrainVbo);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainIbo);
    noteRT("terrain height map", "RGBA32F", p->gridN, p->gridN, 16, 0, true);
    uploadStatic(waterVao, waterVbo, p->water);
    waterVertexCount = (int)p->water.size();
    treeSig_ = scenerySig_ = bushSig_ = 0;
    updateObjectsStage(world, 0);
    objStage_ = 1;
}

bool Renderer::stepWorld(const World& world) {
    if (objStage_ <= 3) updateObjectsStage(world, objStage_++);
    return objStage_ > 3;
}

// The mountain range around the map, out to 3.6 km: a coarse grid with the map's
// square cut out of it (its edge meets the crags at the rim).
void Renderer::buildBackdrop(const World& world, std::vector<Vtx>& verts, std::vector<unsigned>& idx) {
    const double spacing = 24.0, reach = 3600.0, half = World::kHalfSize;
    const int n = (int)(2 * reach / spacing) + 1;
    const unsigned base = (unsigned)verts.size();
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double x = -reach + i * spacing, z = -reach + j * spacing;
            const double e = 6.0;
            V3 nrm = norm(V3(sceneryHeight(world, x - e, z) - sceneryHeight(world, x + e, z), 2 * e,
                             sceneryHeight(world, x, z - e) - sceneryHeight(world, x, z + e)));
            verts.push_back(vtx(V3(x, sceneryHeight(world, x, z), z), nrm, 0, 0, 0, SM_TERRAIN));
        }
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - 1; j++) {
            double x0 = -reach + i * spacing, z0 = -reach + j * spacing;
            // Skip cells entirely inside the map (its own finer mesh is there).
            if (x0 >= -half - 1e-6 && x0 + spacing <= half + 1e-6 && z0 >= -half - 1e-6 && z0 + spacing <= half + 1e-6) continue;
            unsigned a = base + i * n + j, b = base + (i + 1) * n + j, c = base + (i + 1) * n + j + 1, d = base + i * n + j + 1;
            idx.insert(idx.end(), {a, d, c, a, c, b});
        }
}

void Renderer::buildBoxes(const World& world) {
    std::vector<Vtx> v;
    buildBoxMesh(world, v);
    uploadBatches(boxVao, boxVbo, v, boxBatches);
    boxVertexCount = (int)v.size();
}

void Renderer::buildFigures() {
    static std::vector<Vtx> v;  // (kept between frames: a car's parts are hundreds of thousands of vertices)
    v.clear();
    auto box = [&](const V3& c, const V3& he, const V3 ax[3], const V3& col) {
        const double h[3] = {he.x, he.y, he.z};
        for (int axis = 0; axis < 3; axis++)
            for (int s = -1; s <= 1; s += 2) {
                const int a = (axis + 1) % 3, b = (axis + 2) % 3;
                const V3 n = ax[axis] * s, f = c + n * h[axis], du = ax[a] * h[a], dw = ax[b] * h[b];
                const V3 q[4] = {f - du - dw, f + du - dw, f + du + dw, f - du + dw};
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int k : s > 0 ? order : order) v.push_back(vtx(q[k], n, (float)col.x, (float)col.y, (float)col.z, SM_CONCRETE));
            }
    };
    for (const Placed& pl : frameModels)  // (a car's parts are cached by key; the jack, whose own param moves every frame, is not)
    {  // (not one ?: over the three: its by-value branch would copy every cached mesh, every frame)
        if (pl.bare || pl.variant >= 0) appendPlaced(v, pl, placedMesh(pl));
        else if (pl.set.empty()) appendPlaced(v, pl, mk::modelMesh(pl.id));
        else appendPlaced(v, pl, mk::modelMesh(pl.id, pl.set));
    }
    const bool modelled = mk::findModel("person") != nullptr;
    for (const Figure& fg : figures) {
        if (modelled) {  // the picked person (modelkit.h), posed for this frame
            int jacket = 0;
            double best = 1e9;
            for (int i = 0; i < (int)mk::paintNames().size(); i++)
                if (len(mk::paint(i) - fg.col) < best) best = len(mk::paint(i) - fg.col), jacket = i;
            appendModel(v, mk::modelMesh("person", {{"swing", std::sin(fg.stride)}, {"jacket", jacket}}), fg.p, fg.yaw);
            continue;
        }
        const V3 f(std::cos(fg.yaw), 0, -std::sin(fg.yaw)), l(-f.z, 0, f.x), up(0, 1, 0);
        const V3 ax[3] = {f, up, l};
        const V3 skin(0.85, 0.66, 0.5), pants(0.22, 0.24, 0.3), boots(0.18, 0.13, 0.1);
        const double swing = std::sin(fg.stride) * 0.45;
        // A limb hanging from `top`, swung `a` radians forward about the left axis.
        auto limb = [&](const V3& top, double length, double halfW, double a, const V3& col) {
            const V3 dir = up * -std::cos(a) + f * std::sin(a);
            const V3 lax[3] = {norm(cross(l, dir)), dir * -1.0, l};
            box(top + dir * (length / 2), V3(halfW, length / 2, halfW), lax, col);
        };
        const V3 hip = fg.p + up * 0.95;
        limb(hip + l * 0.11, 0.9, 0.075, swing, pants);
        limb(hip - l * 0.11, 0.9, 0.075, -swing, pants);
        box(fg.p + up * 0.04 + l * 0.11 + f * (std::sin(swing) * 0.85 + 0.04), V3(0.13, 0.05, 0.07), ax, boots);
        box(fg.p + up * 0.04 - l * 0.11 + f * (std::sin(-swing) * 0.85 + 0.04), V3(0.13, 0.05, 0.07), ax, boots);
        box(fg.p + up * 1.27, V3(0.13, 0.33, 0.21), ax, fg.col);  // torso
        const V3 shoulder = fg.p + up * 1.55;
        limb(shoulder + l * 0.28, 0.68, 0.06, -swing * 0.8, fg.col);
        limb(shoulder - l * 0.28, 0.68, 0.06, swing * 0.8, fg.col);
        box(fg.p + up * 1.74, V3(0.11, 0.12, 0.1), ax, skin);  // head
        box(fg.p + up * 1.87, V3(0.12, 0.035, 0.11), ax, fg.col * 0.6);  // cap
        box(fg.p + up * 1.85 + f * 0.13, V3(0.06, 0.012, 0.1), ax, fg.col * 0.6);  // peak
    }
    if (!figVao) {
        GenVertexArrays(1, &figVao);
        GenBuffers(1, &figVbo);
        setupVtxVao(figVao, figVbo);
    }
    BindVertexArray(figVao);
    BindBuffer(GL_ARRAY_BUFFER, figVbo);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STREAM_DRAW);
    figVertexCount = (int)v.size();
    // The hurt parts near you again, for the see-through glow.
    static std::vector<Vtx> xr;
    xr.clear();
    if (xrayRange > 0) {
        for (const Placed& pl : frameModels)
            if ((pl.wear == 1 || pl.wear == 2) && len(pl.p - xrayEye) < xrayRange) appendPlaced(xr, pl, placedMesh(pl));
        for (const CarModels& cm : carModels)
            for (const Placed& pl : cm.parts)
                if ((pl.wear == 1 || pl.wear == 2) && len(pl.p - xrayEye) < xrayRange) appendPlaced(xr, pl, placedMesh(pl));
    }
    if (!xrayVao) {
        GenVertexArrays(1, &xrayVao);
        GenBuffers(1, &xrayVbo);
        setupVtxVao(xrayVao, xrayVbo);
    }
    BindVertexArray(xrayVao);
    BindBuffer(GL_ARRAY_BUFFER, xrayVbo);
    BufferData(GL_ARRAY_BUFFER, xr.size() * sizeof(Vtx), xr.empty() ? nullptr : xr.data(), GL_STREAM_DRAW);
    xrayVertexCount = (int)xr.size();
}

void Renderer::buildWheelStyle(int style) {
    // Built for R = 0.32, half-width 0.12; local z = axle (+z = outer face).
    // Road wheel: low-profile tire on a big five-spoke alloy. Off-road wheel: a tall
    // all-terrain tire (35x12.50R17 proportions) with tread blocks and raised white
    // letters, on an eight-spoke rim.
    const bool steelWheel = style == WHEEL_STEEL;
    const bool offroad = style == WHEEL_OFFROAD_BRONZE || style == WHEEL_OFFROAD_BLACK || steelWheel;
    const bool cover = style == WHEEL_HONDA_COVER, forged = style == WHEEL_FORGED;
    const int segs = buildLod_ ? 16 : 48;  // (the far copy: a coarser round, no tread blocks, letters or lugs)
    const double rimR = offroad ? 0.158 : cover ? 0.197 : forged ? 0.23 : 0.215;
    std::vector<Vtx> v;
    MeshOut m{v};

    // Tire: a rounded profile revolved around the axle, smooth normals.
    static const double road[][2] = {{0.212, 0.104}, {0.23, 0.117}, {0.27, 0.122}, {0.302, 0.114}, {0.316, 0.098},
                                     {0.32, 0.07},   {0.32, 0.0},   {0.32, -0.07}, {0.316, -0.098}, {0.302, -0.114},
                                     {0.27, -0.122}, {0.23, -0.117}, {0.212, -0.104}};
    static const double allTerrain[][2] = {{0.155, 0.108}, {0.18, 0.122}, {0.23, 0.13},  {0.28, 0.126}, {0.305, 0.112},
                                           {0.312, 0.09},  {0.312, 0.0},  {0.312, -0.09}, {0.305, -0.112}, {0.28, -0.126},
                                           {0.23, -0.13},  {0.18, -0.122}, {0.155, -0.108}};
    double roadProfile[13][2];
    for (int k = 0; k < 13; k++) {
        roadProfile[k][0] = rimR + (road[k][0] - 0.215) * (0.32 - rimR) / (0.32 - 0.215);
        roadProfile[k][1] = road[k][1];
    }
    const double(*prof)[2] = offroad ? allTerrain : roadProfile;
    const int np = 13;
    auto profNormal = [&](int k) {  // (radial, axial) normal from neighbouring profile points
        int a = std::max(0, k - 1), b = std::min(np - 1, k + 1);
        double tr = prof[b][0] - prof[a][0], tz = prof[b][1] - prof[a][1];
        double l = std::sqrt(tr * tr + tz * tz);
        return std::make_pair(-tz / l, tr / l);
    };
    const V3 rubber(0.06, 0.06, 0.06);
    for (int s = 0; s < segs; s++) {
        double a0 = 2 * M_PI * s / segs, a1 = 2 * M_PI * (s + 1) / segs;
        for (int k = 0; k + 1 < np; k++) {
            auto P = [&](double a, int pk) { return V3(std::cos(a) * prof[pk][0], std::sin(a) * prof[pk][0], prof[pk][1]); };
            auto N = [&](double a, int pk) {
                auto nn = profNormal(pk);
                return V3(std::cos(a) * nn.first, std::sin(a) * nn.first, nn.second);
            };
            V3 p[4] = {P(a0, k), P(a1, k), P(a1, k + 1), P(a0, k + 1)};
            V3 n[4] = {N(a0, k), N(a1, k), N(a1, k + 1), N(a0, k + 1)};
            const int order[6] = {0, 2, 1, 0, 3, 2};
            for (int o : order) v.push_back(vtx(p[o], n[o], (float)rubber.x, (float)rubber.y, (float)rubber.z, SM_TIRE));
        }
    }
    if (offroad) {
        // Tread: two staggered rows of blocks, and shoulder lugs wrapping onto the sidewalls.
        const int blocks = 36;
        auto block = [&](double a, double half, double r0, double r1, double z0, double z1) {
            V3 p[8];
            for (int b = 0; b < 8; b++) {
                double aa = a + ((b & 1) ? half : -half), r = (b & 2) ? r1 : r0, z = (b & 4) ? z1 : z0;
                p[b] = V3(std::cos(aa) * r, std::sin(aa) * r, z);
            }
            // hexa() wants bit1 = "up" (here: outward), bit2 = +z.
            m.hexa(p, rubber, SM_TIRE);
        };
        for (int b = 0; b < (buildLod_ ? 0 : blocks); b++) {
            double a = 2 * M_PI * b / blocks, h = M_PI / blocks * 0.62;
            for (int side = -1; side <= 1; side += 2) {
                double shift = side * 0.25 * (2 * M_PI / blocks);
                block(a + shift, h, 0.309, 0.322, side > 0 ? 0.004 : -0.052, side > 0 ? 0.052 : -0.004);
                // Shoulder lug down the sidewall.
                double zs = side * 0.118;
                block(a + shift + side * 0.03, h * 0.8, 0.27, 0.312, std::min(zs, zs + side * 0.016), std::max(zs, zs + side * 0.016));
            }
        }
        // Raised white letters on the outer sidewall: two arcs of short strokes.
        const V3 white(0.82, 0.82, 0.8);
        for (int arc = 0; arc < (style == WHEEL_OFFROAD_BRONZE && !buildLod_ ? 2 : 0); arc++)
            for (int c = 0; c < 15; c++) {
                double a = (arc ? M_PI : 0) + 0.55 + c * 0.13, h = 0.035 * ((c % 3) ? 1.0 : 0.6);
                V3 p[8];
                for (int b = 0; b < 8; b++) {
                    double aa = a + ((b & 1) ? h : -h), r = (b & 2) ? 0.255 : 0.215, z = (b & 4) ? 0.133 : 0.127;
                    p[b] = V3(std::cos(aa) * r, std::sin(aa) * r, z);
                }
                m.hexa(p, white, SM_MATTE);
            }
    }

    tireVertexCount[buildLod_ ? 1 : 0][style] = (int)v.size();
    // Rim: barrel, outer lip, dished spokes, hub with lug nuts and a center cap.
    const V3 alloy = steelWheel ? V3(0.86, 0.86, 0.83) : forged ? V3(0.12, 0.13, 0.15) : cover ? V3(0.55, 0.56, 0.58) : style == WHEEL_OFFROAD_BRONZE ? V3(0.36, 0.27, 0.16)
                   : style == WHEEL_OFFROAD_BLACK  ? V3(0.05, 0.05, 0.055)
                                                   : V3(0.62, 0.63, 0.66);
    const int rimMat = offroad || forged || cover ? SM_MATTE : SM_METAL;
    const V3 dark(0.12, 0.12, 0.13), chrome(0.85, 0.85, 0.87);
    const int spokes = steelWheel ? 0 : offroad ? 8 : forged ? 10 : cover ? 12 : 5;
    m.barrel(rimR, -0.104, 0.1, segs, true, offroad ? alloy * 0.7 : V3(0.3, 0.3, 0.32), rimMat);
    if (steelWheel) {
        // Pressed steel: a dished center disc welded in the barrel, a ring of round vent
        // holes, painted white; a small chrome "dog dish" cap over the lugs.
        m.annulus(0.075, rimR - 0.012, 0.072, segs, 1, alloy, SM_MATTE);
        m.barrel(rimR - 0.012, 0.072, 0.095, segs, false, alloy, SM_MATTE);
        for (int hole = 0; hole < 6; hole++) {
            const double a = 2 * M_PI * hole / 6, hr = 0.022, rr = 0.5 * (0.075 + rimR - 0.012);
            m.annulus(0.0, hr, 0.0735, 12, 1, V3(0.02, 0.02, 0.02), SM_MATTE, V3(std::cos(a) * rr, std::sin(a) * rr, 0));
        }
        m.annulus(0.0, 0.075, 0.074, 24, 1, V3(0.9, 0.9, 0.92), SM_CHROME);
    }
    m.annulus(rimR - 0.012, rimR + 0.006, 0.104, segs, 1, alloy, rimMat);
    m.barrel(rimR - 0.012, 0.085, 0.104, segs, true, alloy, rimMat);
    if (offroad) m.annulus(rimR - 0.012, rimR - 0.004, 0.108, segs, 1, V3(0.02, 0.02, 0.02), SM_MATTE);  // dark beauty ring
    for (int sp = 0; sp < spokes; sp++) {
        double a = 2 * M_PI * sp / spokes;
        V3 dir(std::cos(a), std::sin(a), 0), side(-std::sin(a), std::cos(a), 0);
        // Hub end: narrow and deep in the dish; rim end: flush with the lip.
        V3 p[8];
        for (int b = 0; b < 8; b++) {
            bool rimEnd = b & 1, top = b & 2, front = b & 4;
            double r = rimEnd ? rimR - 0.01 : 0.05, w = offroad ? (rimEnd ? 0.016 : 0.02) : (rimEnd ? 0.022 : 0.032);
            double zf = rimEnd ? 0.095 : (offroad ? 0.08 : 0.062), zb = zf - 0.022;
            p[b] = dir * r + side * (top ? w : -w) + V3(0, 0, front ? zf : zb);
        }
        m.hexa(p, alloy, rimMat);
    }
    const double hubZ = steelWheel ? 0.076 : offroad ? 0.08 : 0.06;
    m.annulus(0.0, 0.058, hubZ, 20, 1, alloy, rimMat);
    m.barrel(0.058, 0.035, hubZ, 20, false, alloy, rimMat);
    const V3 cap = steelWheel ? chrome : offroad ? V3(0.03, 0.03, 0.03) : chrome;
    m.annulus(0.0, 0.026, hubZ + 0.01, 12, 1, cap, SM_CHROME);
    m.barrel(0.026, hubZ, hubZ + 0.01, 12, false, cap, SM_CHROME);
    if (cover) { // period Honda wheel cover, radial cooling slots and raised center
        m.annulus(0.0, rimR * 0.71, 0.100, segs, 1, alloy, SM_MATTE);
        m.annulus(rimR * 0.88, rimR, 0.100, segs, 1, alloy, SM_MATTE);
    }
    const int lugs = buildLod_ || steelWheel ? 0 : offroad ? 6 : cover ? 4 : forged ? 1 : 5;
    for (int l = 0; l < lugs; l++) {
        double a = 2 * M_PI * (l + 0.5) / lugs;
        V3 c(std::cos(a) * 0.04, std::sin(a) * 0.04, 0);
        for (int s = 0; s < 6; s++) {
            double b0 = 2 * M_PI * s / 6, b1 = 2 * M_PI * (s + 1) / 6;
            V3 d0(std::cos(b0) * 0.008, std::sin(b0) * 0.008, 0), d1(std::cos(b1) * 0.008, std::sin(b1) * 0.008, 0);
            V3 z0(0, 0, hubZ), z1(0, 0, hubZ + 0.012);
            m.quad(c + d0 + z0, c + d1 + z0, c + d1 + z1, c + d0 + z1, chrome, SM_CHROME);
            m.tri(c + z1, c + d0 + z1, c + d1 + z1, chrome, SM_CHROME);
        }
    }
    // Brake disc and its hat, behind the spokes.
    const double disc = std::min(0.175, rimR - 0.02);
    m.annulus(0.07, disc, 0.0, segs, 1, V3(0.42, 0.42, 0.43), SM_METAL);
    m.annulus(0.07, disc, -0.022, segs, -1, V3(0.42, 0.42, 0.43), SM_METAL);
    m.barrel(disc, -0.022, 0.0, segs, false, V3(0.35, 0.33, 0.32), SM_METAL);
    m.barrel(0.07, 0.0, 0.035, 20, false, dark, SM_METAL);
    m.annulus(0.0, 0.07, 0.035, 20, 1, dark, SM_METAL);

    optimizeVtx(v);  // (degenerate and hidden triangles of the spokes, lugs and hub)
    const int L = buildLod_ ? 1 : 0;
    GenVertexArrays(1, &wheelVao[L][style]);
    GenBuffers(1, &wheelVbo[L][style]);
    setupVtxVao(wheelVao[L][style], wheelVbo[L][style]);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
    wheelVertexCount[L][style] = (int)v.size();
}

// One tread on one wheel style: the tire alone, built (like the wheel) for R = 0.32 and a
// half-width of 0.12, its bead on that style's rim.
void Renderer::buildTire(int style, int tread) {
    const bool offStyle = style == WHEEL_OFFROAD_BRONZE || style == WHEEL_OFFROAD_BLACK || style == WHEEL_STEEL;
    const double rimR = offStyle ? 0.158 : style == WHEEL_HONDA_COVER ? 0.197 : style == WHEEL_FORGED ? 0.23 : 0.215;
    const bool knobby = tread == TIRE_ALL_TERRAIN || tread == TIRE_MUD_TERRAIN || tread == TIRE_SNOW || tread == TIRE_CHAINS;
    const bool slick = tread == TIRE_RACE_SLICK;
    std::vector<Vtx> v;
    MeshOut m{v};
    const int segs = buildLod_ ? 16 : 48;  // (the far copy: a coarser round, no tread blocks, letters or lugs)
    // The carcass: a rounded profile from bead to bead. Knobby tires have a tall, rounded
    // sidewall and their tread blocks stand on it; road tires a low, square shoulder.
    static const double road[][2] = {{0.212, 0.104}, {0.23, 0.117}, {0.27, 0.122}, {0.302, 0.114}, {0.316, 0.098},
                                     {0.32, 0.07},   {0.32, 0.0},   {0.32, -0.07}, {0.316, -0.098}, {0.302, -0.114},
                                     {0.27, -0.122}, {0.23, -0.117}, {0.212, -0.104}};
    static const double tall[][2] = {{0.155, 0.108}, {0.18, 0.122}, {0.23, 0.13},  {0.28, 0.126}, {0.305, 0.112},
                                     {0.312, 0.09},  {0.312, 0.0},  {0.312, -0.09}, {0.305, -0.112}, {0.28, -0.126},
                                     {0.23, -0.13},  {0.18, -0.122}, {0.155, -0.108}};
    const int np = 13;
    double prof[13][2];
    for (int k = 0; k < np; k++) {
        const double (*src)[2] = knobby ? tall : road;
        const double r0 = knobby ? 0.155 : 0.212, rOut = knobby ? 0.312 : 0.32;
        prof[k][0] = rimR + (src[k][0] - r0) * (rOut - rimR) / (rOut - r0);
        prof[k][1] = src[k][1];
        if (slick && (k == 4 || k == 8)) prof[k][0] += 0.003;  // (a squarer shoulder: all contact patch)
    }
    auto profNormal = [&](int k) {
        int a = std::max(0, k - 1), b = std::min(np - 1, k + 1);
        double tr = prof[b][0] - prof[a][0], tz = prof[b][1] - prof[a][1];
        double l = std::sqrt(tr * tr + tz * tz);
        return std::make_pair(-tz / l, tr / l);
    };
    const V3 rubber = slick ? V3(0.075, 0.075, 0.078) : V3(0.06, 0.06, 0.06);  // (a slick's scrubbed, greyer face)
    for (int sg = 0; sg < segs; sg++) {
        double a0 = 2 * M_PI * sg / segs, a1 = 2 * M_PI * (sg + 1) / segs;
        for (int k = 0; k + 1 < np; k++) {
            auto P = [&](double a, int pk) { return V3(std::cos(a) * prof[pk][0], std::sin(a) * prof[pk][0], prof[pk][1]); };
            auto N = [&](double a, int pk) {
                auto nn = profNormal(pk);
                return V3(std::cos(a) * nn.first, std::sin(a) * nn.first, nn.second);
            };
            V3 p[4] = {P(a0, k), P(a1, k), P(a1, k + 1), P(a0, k + 1)};
            V3 n[4] = {N(a0, k), N(a1, k), N(a1, k + 1), N(a0, k + 1)};
            const int order[6] = {0, 2, 1, 0, 3, 2};
            for (int o : order) v.push_back(vtx(p[o], n[o], (float)rubber.x, (float)rubber.y, (float)rubber.z, SM_TIRE));
        }
    }
    // A raised block of tread: centred at angle a, half-angle `half`, from radius r0 up to r1, z0..z1.
    auto block = [&](double a, double half, double r0, double r1, double z0, double z1, const V3& col = V3(0.06, 0.06, 0.06)) {
        V3 p[8];
        for (int b = 0; b < 8; b++) {
            double aa = a + ((b & 1) ? half : -half), r = (b & 2) ? r1 : r0, z = (b & 4) ? z1 : z0;
            p[b] = V3(std::cos(aa) * r, std::sin(aa) * r, z);
        }
        m.hexa(p, col, SM_TIRE);
    };
    // A dark band around the tread at z: a groove's shadow (road tires).
    auto groove = [&](double z, double w) {
        for (int sg = 0; sg < segs; sg++) {
            double a0 = 2 * M_PI * sg / segs, a1 = 2 * M_PI * (sg + 1) / segs, r = 0.3205;
            V3 p0(std::cos(a0) * r, std::sin(a0) * r, z - w), p1(std::cos(a1) * r, std::sin(a1) * r, z - w);
            V3 q0(std::cos(a0) * r, std::sin(a0) * r, z + w), q1(std::cos(a1) * r, std::sin(a1) * r, z + w);
            m.quad(p0, q0, q1, p1, V3(0.012, 0.012, 0.012), SM_MATTE);
        }
    };
    if (buildLod_) {
        // (far off: the carcass alone)
    } else if (tread == TIRE_STREET) {
        for (double z : {-0.062, -0.022, 0.022, 0.062}) groove(z, 0.0045);
        // Sipes across the shoulders.
        for (int b = 0; b < 60; b++) {
            const double a = 2 * M_PI * b / 60;
            for (int sd = -1; sd <= 1; sd += 2) block(a, 0.004, 0.3195, 0.3215, sd > 0 ? 0.075 : -0.105, sd > 0 ? 0.105 : -0.075, V3(0.015, 0.015, 0.015));
        }
    } else if (tread == TIRE_SEMI_SLICK) {
        for (double z : {-0.038, 0.038}) groove(z, 0.005);
    } else if (slick) {
        // A colored band round the sidewall (a race tire's compound marking).
        for (int sg = 0; sg < segs; sg++) {
            double a0 = 2 * M_PI * sg / segs, a1 = 2 * M_PI * (sg + 1) / segs;
            const double r0 = 0.262, r1 = 0.274, z = 0.1225;
            V3 p0(std::cos(a0) * r0, std::sin(a0) * r0, z), p1(std::cos(a1) * r0, std::sin(a1) * r0, z);
            V3 q0(std::cos(a0) * r1, std::sin(a0) * r1, z), q1(std::cos(a1) * r1, std::sin(a1) * r1, z);
            m.quad(p0, q0, q1, p1, V3(0.95, 0.78, 0.08), SM_MATTE);
        }
    } else if (tread == TIRE_ALL_TERRAIN || tread == TIRE_CHAINS) {
        // Two staggered rows of blocks, and shoulder lugs wrapping onto the sidewalls.
        const int blocks = 36;
        for (int b = 0; b < blocks; b++) {
            double a = 2 * M_PI * b / blocks, h = M_PI / blocks * 0.62;
            for (int side = -1; side <= 1; side += 2) {
                double shift = side * 0.25 * (2 * M_PI / blocks);
                block(a + shift, h, 0.309, 0.322, side > 0 ? 0.004 : -0.052, side > 0 ? 0.052 : -0.004);
                double zs = side * 0.118;
                block(a + shift + side * 0.03, h * 0.8, 0.27, 0.312, std::min(zs, zs + side * 0.016), std::max(zs, zs + side * 0.016));
            }
        }
        if (style == WHEEL_OFFROAD_BRONZE) {  // raised white letters
            const V3 white(0.82, 0.82, 0.8);
            for (int arc = 0; arc < 2; arc++)
                for (int c = 0; c < 15; c++) {
                    double a = (arc ? M_PI : 0) + 0.55 + c * 0.13, h = 0.035 * ((c % 3) ? 1.0 : 0.6);
                    V3 p[8];
                    for (int b = 0; b < 8; b++) {
                        double aa = a + ((b & 1) ? h : -h), r = (b & 2) ? 0.255 : 0.215, z = (b & 4) ? 0.133 : 0.127;
                        p[b] = V3(std::cos(aa) * r, std::sin(aa) * r, z);
                    }
                    m.hexa(p, white, SM_MATTE);
                }
        }
    } else if (tread == TIRE_MUD_TERRAIN) {
        // Big lugs with wide voids between them, angled in a V, standing proud; heavy shoulder
        // lugs down the sidewall.
        const int blocks = 20;
        for (int b = 0; b < blocks; b++) {
            double a = 2 * M_PI * b / blocks, h = M_PI / blocks * 0.52;
            for (int side = -1; side <= 1; side += 2) {
                double shift = side * 0.3 * (2 * M_PI / blocks);
                block(a + shift, h, 0.306, 0.330, side > 0 ? 0.008 : -0.062, side > 0 ? 0.062 : -0.008);
                double zs = side * 0.116;
                block(a + shift + side * 0.05, h * 0.9, 0.24, 0.318, std::min(zs, zs + side * 0.022), std::max(zs, zs + side * 0.022));
            }
        }
    } else if (tread == TIRE_SNOW) {
        // Many small siped blocks in three staggered rows.
        const int blocks = 52;
        for (int b = 0; b < blocks; b++) {
            double a = 2 * M_PI * b / blocks, h = M_PI / blocks * 0.66;
            const double zs[3][2] = {{-0.10, -0.038}, {-0.03, 0.03}, {0.038, 0.10}};
            for (int row = 0; row < 3; row++) {
                double shift = (row - 1) * 0.33 * (2 * M_PI / blocks);
                block(a + shift, h, 0.306, 0.326, zs[row][0], zs[row][1]);
                // Two sipes across each block: thin dark slots.
                for (double d : {-0.4, 0.4}) block(a + shift + d * h, 0.0025, 0.3255, 0.3268, zs[row][0] + 0.004, zs[row][1] - 0.004, V3(0.012, 0.012, 0.012));
            }
        }
        // The mountain-snowflake mark on the outer sidewall, four times round.
        for (int mk = 0; mk < 4; mk++) {
            const double a = mk * M_PI / 2 + 0.4;
            for (int spoke = 0; spoke < 3; spoke++) {
                const double b = spoke * M_PI / 3;
                const V3 c(std::cos(a) * 0.255, std::sin(a) * 0.255, 0.131);
                const V3 d(std::cos(b) * 0.016, std::sin(b) * 0.016, 0);
                V3 p[8];
                const V3 side = norm(cross(d, V3(0, 0, 1))) * 0.0025;
                for (int q = 0; q < 8; q++) p[q] = c + ((q & 1) ? d : d * -1.0) + ((q & 2) ? side : side * -1.0) + V3(0, 0, (q & 4) ? 0.002 : 0.0);
                m.hexa(p, V3(0.85, 0.86, 0.88), SM_MATTE);
            }
        }
    }
    const int L = buildLod_ ? 1 : 0;
    GenVertexArrays(1, &tireVao[L][style][tread]);
    GenBuffers(1, &tireVbo[L][style][tread]);
    setupVtxVao(tireVao[L][style][tread], tireVbo[L][style][tread]);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
    treadVertexCount[L][style][tread] = (int)v.size();
}

// Snow chains: cross links over the tread every few inches, and a side chain round each
// shoulder holding them. (Drawn on a chained wheel with the tire's own matrix.)
void Renderer::buildChains() {
    std::vector<Vtx> v;
    const V3 steel(0.55, 0.56, 0.58);
    // A link from a to b: an oval loop of round wire, each turned a quarter from the last.
    int linkN = 0;
    auto link = [&](const V3& a, const V3& b, double r) {
        const V3 d = b - a, c = (a + b) * 0.5;
        const double L = len(d);
        if (L < 1e-6) return;
        const V3 ax = d / L, ref = std::fabs(ax.z) < 0.9 ? V3(0, 0, 1) : V3(1, 0, 0), ay = norm(cross(ref, ax)), az = cross(ax, ay);
        const V3 side = linkN++ % 2 ? ay : az;
        const double hl = L * 0.62, hw = std::max(r * 2.4, L * 0.3), wire = r * 0.85;
        const int n = 8, sides = 3;
        V3 p[n];
        for (int i = 0; i < n; i++) {
            const double t = 2 * M_PI * i / n;
            p[i] = c + ax * (std::cos(t) * hl) + side * (std::sin(t) * hw);
        }
        auto ring = [&](int i, int j, V3& at, V3& nn) {
            const V3 T = norm(p[(i + 1) % n] - p[(i + n - 1) % n]);
            V3 out = p[i] - c;
            out = norm(out - T * dot(out, T));
            const V3 o2 = cross(T, out);
            const double t = 2 * M_PI * j / sides;
            nn = out * std::cos(t) + o2 * std::sin(t);
            at = p[i] + nn * wire;
        };
        for (int i = 0; i < n; i++)
            for (int j = 0; j < sides; j++) {
                V3 q[4], nq[4];
                ring(i, j, q[0], nq[0]), ring((i + 1) % n, j, q[1], nq[1]), ring((i + 1) % n, j + 1, q[2], nq[2]), ring(i, j + 1, q[3], nq[3]);
                for (int k : {0, 1, 2, 0, 2, 3}) v.push_back(vtx(q[k], nq[k], (float)steel.x, (float)steel.y, (float)steel.z, SM_METAL));
            }
    };
    // In order round the tire, so drawing the first part of it shows a chain part wrapped on.
    const int cross = 14, segs = 56;
    for (int k = 0; k < segs; k++) {
        for (int sd = -1; sd <= 1; sd += 2) {
            const double a0 = 2 * M_PI * k / segs, a1 = 2 * M_PI * (k + 1) / segs, r = 0.30, z = sd * 0.124;
            link(V3(std::cos(a0) * r, std::sin(a0) * r, z), V3(std::cos(a1) * r, std::sin(a1) * r, z), 0.0045);
        }
        if (k % (segs / cross)) continue;
        // A zig of links across the tread from one side chain to the other.
        const double a = 2 * M_PI * k / segs;
        for (int j = 0; j < 6; j++) {
            const double z0 = -0.118 + 0.236 * j / 6, z1 = -0.118 + 0.236 * (j + 1) / 6, wob = (j % 2 ? 1 : -1) * 0.015;
            const double r = 0.326;
            link(V3(std::cos(a + wob) * r, std::sin(a + wob) * r, z0), V3(std::cos(a - wob) * r, std::sin(a - wob) * r, z1), 0.004);
        }
    }
    GenVertexArrays(1, &chainVao);
    GenBuffers(1, &chainVbo);
    setupVtxVao(chainVao, chainVbo);
    BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
    chainVertexCount = (int)v.size();
}

void Renderer::buildWheel() {
    for (int lod = 0; lod < 2; lod++) {  // (and a far copy of each: drawWheels picks by distance)
        buildLod_ = lod == 1;
        for (int st = 0; st < WHEEL_STYLES; st++) buildWheelStyle(st);
        for (int st = 0; st < WHEEL_STYLES; st++)
            for (int tt = 0; tt < TIRE_TYPES; tt++) buildTire(st, tt);
    }
    buildLod_ = false;
    buildChains();
    std::vector<Vtx> cal;

    // Parts that don't spin: brake caliper straddling the disc, and the hub carrier.
    MeshOut c{cal};
    {
        double a0 = 2 * M_PI * 0.38, a1 = 2 * M_PI * 0.47;  // upper rear of the disc
        V3 p[8];
        for (int b = 0; b < 8; b++) {
            double a = (b & 1) ? a1 : a0, r = (b & 2) ? 0.19 : 0.13, z = (b & 4) ? 0.018 : -0.045;
            p[b] = V3(std::cos(a) * r, std::sin(a) * r, z);
        }
        c.hexa(p, V3(0.75, 0.1, 0.07), SM_PAINT);
    }
    c.barrel(0.05, -0.13, -0.025, 12, false, V3(0.16, 0.16, 0.17), SM_METAL);
    c.annulus(0.0, 0.05, -0.13, 12, -1, V3(0.16, 0.16, 0.17), SM_METAL);
    GenVertexArrays(1, &caliperVao);
    GenBuffers(1, &caliperVbo);
    setupVtxVao(caliperVao, caliperVbo);
    BufferData(GL_ARRAY_BUFFER, cal.size() * sizeof(Vtx), cal.data(), GL_STATIC_DRAW);
    caliperVertexCount = (int)cal.size();
}

void Renderer::buildFont() {
    // 16 x 6 grid of 6x8 cells covering ASCII 32..127; 127 is a solid block for rects.
    const int W = 96, H = 48;
    std::vector<unsigned char> px(W * H, 0);
    for (const Glyph& g : kGlyphs) {
        int code = g.c - 32, cx = (code % 16) * 6, cy = (code / 16) * 8;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g.rows[r][c] == '#') px[(cy + r) * W + cx + c] = 255;
    }
    int code = 127 - 32, cx = (code % 16) * 6, cy = (code / 16) * 8;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 6; c++) px[(cy + r) * W + cx + c] = 255;
    GenTextures(1, &fontTex);
    BindTexture(GL_TEXTURE_2D, fontTex);
    PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    TexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    noteRT("font", "R8", W, H, 1, 0, false);  // (pixel-exact: uncompressed, no mips)
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// The same, for a wheel off its vehicle: centered at c, axle `axis` (local +Z, the outer
// face), turned by `spin` about it, sized R x width.
M4 wheelMatrixAt(const V3& c, const V3& axis, const V3& up, double spin, double R, double width, double radialScale = 1.0) {
    V3 f0 = norm(cross(axis, up));
    if (len(f0) < 0.5) f0 = norm(cross(axis, V3(1, 0, 0)));
    const V3 u0 = cross(f0, axis);
    const double radial = R / 0.32 * radialScale, axial = width / 0.24;
    const V3 e1 = rotateAxis(f0, axis, spin) * radial, e2 = rotateAxis(u0, axis, spin) * radial, out = axis * axial;
    M4 m = m4identity();
    m.m[0] = (float)e1.x; m.m[1] = (float)e1.y; m.m[2] = (float)e1.z;
    m.m[4] = (float)e2.x; m.m[5] = (float)e2.y; m.m[6] = (float)e2.z;
    m.m[8] = (float)out.x; m.m[9] = (float)out.y; m.m[10] = (float)out.z;
    m.m[12] = (float)c.x; m.m[13] = (float)c.y; m.m[14] = (float)c.z;
    return m;
}

void Renderer::drawWheels(const Sim& sim, GLuint prog) {
    carKind_ = 4;
    // A wheel (or a bare tire, or a flat) off a vehicle, by its matrix at full size.
    auto drawItem = [&](const WheelItem& it, const M4& full, const M4& flat) {
        const int st = std::min(std::max(it.style, 0), WHEEL_STYLES - 1);
        BindVertexArray(wheelVao[0][st]);
        if (it.tireOnly) {
            uniformM4(prog, "uModel", full);
            carDrawArrays(0, tireVertexCount[0][st]);
        } else if (it.popped) {
            uniformM4(prog, "uModel", flat);
            carDrawArrays(0, tireVertexCount[0][st]);
            uniformM4(prog, "uModel", full);
            carDrawArrays(tireVertexCount[0][st], wheelVertexCount[0][st] - tireVertexCount[0][st]);
        } else {
            uniformM4(prog, "uModel", full);
            carDrawArrays(0, wheelVertexCount[0][st]);
        }
    };
    for (const Car& car : sim.cars) {
        if (car.wheels.empty() || !carShown(&car - sim.cars.data())) continue;
        Frame fr = car.frame();
        const int L = len(fr.com - eye_) > wheelLodDist_ ? 1 : 0;  // (far off: the coarse copies, no calipers)
        // The car's own tread on this wheel (and chains over it when they're on).
        const int tread = std::min(std::max(car.spec->tireType, 0), TIRE_TYPES - 1);
        auto drawTread = [&](int st, const M4& tm, double chain) {  // (chain: how much of one is wrapped on, 0..1)
            uniformM4(prog, "uModel", tm);
            BindVertexArray(tireVao[L][st][tread]);
            carDrawArrays(0, treadVertexCount[L][st][tread]);
            if (chain > 0) {
                BindVertexArray(chainVao);
                carDrawArrays(0, chain >= 1 ? chainVertexCount : (int)(chainVertexCount * chain / 3) * 3);
            }
        };
        const int ci = (int)(&car - sim.cars.data());
        for (const Wheel& wh : car.wheels) {
            // The wheel you're working on (handwork.h): slid out along its axle, a chain going on or off.
            const bool worked = wheelWork.car == ci && wheelWork.wheel == (int)(&wh - car.wheels.data());
            const V3 slide = worked ? norm(car.nodes[wh.outer].p - car.nodes[wh.inner].p) * wheelWork.out : V3();
            const double chain = worked && wheelWork.chain >= 0 ? wheelWork.chain : wh.chained ? 1.0 : 0.0;
            auto wm = [&](bool spin, double rs = 1.0, double vs = 0.0, double ws = 1.0) {
                M4 m = wheelMatrix(car, wh, fr.up, spin, rs, vs, ws);
                m.m[12] += (float)slide.x, m.m[13] += (float)slide.y, m.m[14] += (float)slide.z;
                return m;
            };
            const int st = std::min(std::max(wh.mounted ? wh.item.style : car.spec->wheelStyle, 0), WHEEL_STYLES - 1);
            BindVertexArray(wheelVao[L][st]);
            if (!wh.mounted) {
                // Nothing on the hub but its rotor (the caliper below) and the studs: a sliver of rim face.
                uniformM4(prog, "uModel", wm(true, kBareHubR / wh.R));
                carDrawArrays(tireVertexCount[L][st], wheelVertexCount[L][st] - tireVertexCount[L][st]);
            } else if (wh.debeaded) {  // the rim alone, the tire gone
                uniformM4(prog, "uModel", wm(true));
                carDrawArrays(tireVertexCount[L][st], wheelVertexCount[L][st] - tireVertexCount[L][st]);
            } else if (wh.popped) {
                // Smooth deflation animation: interpolate between inflated and flat
                const double flatScale = (wh.rimR + 0.015) / wh.R;
                double scale = (1.0 - wh.poppedAnim) * 1.0 + wh.poppedAnim * flatScale;
                const M4 tm = wm(true, scale);
                drawTread(st, tm, chain);
                BindVertexArray(wheelVao[L][st]);
                uniformM4(prog, "uModel", wm(true));
                carDrawArrays(tireVertexCount[L][st], wheelVertexCount[L][st] - tireVertexCount[L][st]);
            } else {
                // Match load-dependent sidewall sag; keep the rim full size and
                // the compressed axis stationary while the tread rotates.
                const double vertical = wh.loadedRadius(car.tirePsi, car.spec->tirePsi, car.spec->tireSoft) / wh.R;
                const double bulge = 1 + 0.3 * (1 - vertical);
                drawTread(st, wm(true, 1.0, vertical, bulge), chain);
                BindVertexArray(wheelVao[L][st]);
                uniformM4(prog, "uModel", wm(true));
                carDrawArrays(tireVertexCount[L][st], wheelVertexCount[L][st] - tireVertexCount[L][st]);
            }
            if (L) continue;
            BindVertexArray(caliperVao);
            // (The off-road rim is smaller for its tire: its brake is too.)
            uniformM4(prog, "uModel", wheelMatrix(car, wh, fr.up, false, st == WHEEL_OFFROAD_BRONZE || st == WHEEL_OFFROAD_BLACK || st == WHEEL_STEEL ? 0.72 : st == WHEEL_HONDA_COVER ? 0.82 : 1.0));
            carDrawArrays(0, caliperVertexCount);
        }
        // The spare where it rides (gone when it's been taken).
        if (car.hasSpare && !car.spareNodes.empty()) {
            const V3 c = car.spareAt(), ax = car.spareAxis();
            const WheelItem& it = car.spare;
            drawItem(it, wheelMatrixAt(c, ax, fr.fwd, 0, it.R, it.width), wheelMatrixAt(c, ax, fr.fwd, 0, it.R, it.width, (it.rimR + 0.015) / it.R));
        }
    }
    // Wheels lying about: their own frame (local z the axle).
    for (const LooseWheel& lw : sim.looseWheels) {
        const WheelItem& it = lw.item;
        const V3 e1 = lw.q.rotate(V3(1, 0, 0)), e2 = lw.q.rotate(V3(0, 1, 0)), ez = lw.q.rotate(V3(0, 0, 1));
        auto mat = [&](double rs) {
            const double radial = it.R / 0.32 * rs, axial = it.width / 0.24;
            M4 m = m4identity();
            const V3 a = e1 * radial, b = e2 * radial, o = ez * axial;
            m.m[0] = (float)a.x; m.m[1] = (float)a.y; m.m[2] = (float)a.z;
            m.m[4] = (float)b.x; m.m[5] = (float)b.y; m.m[6] = (float)b.z;
            m.m[8] = (float)o.x; m.m[9] = (float)o.y; m.m[10] = (float)o.z;
            m.m[12] = (float)lw.p.x; m.m[13] = (float)lw.p.y; m.m[14] = (float)lw.p.z;
            return m;
        };
        drawItem(it, mat(1.0), mat((it.rimR + 0.015) / it.R));
    }
    // The one in your arms.
    if (carried.on) {
        const WheelItem& it = carried.item;
        drawItem(it, wheelMatrixAt(carried.c, carried.axis, carried.up, 0, it.R, it.width),
                 wheelMatrixAt(carried.c, carried.axis, carried.up, 0, it.R, it.width, (it.rimR + 0.015) / it.R));
    }
    uniformM4(prog, "uModel", m4identity());
}

namespace {
// The nodes a vehicle type's skin and bound parts are made from (and its torn-panel test reads).
const std::vector<int>& watchedNodes(const Car& car, const VisualTemplate& t) {
    static std::map<const VisualTemplate*, std::vector<int>> cache;
    auto it = cache.find(&t);
    if (it != cache.end()) return it->second;
    std::vector<char> used(car.nodes.size(), 0);
    auto use = [&](int n) {
        if (n >= 0 && n < (int)used.size()) used[n] = 1;
    };
    for (const auto& b : t.sculptBind)
        for (int k = 0; k < 8; k++) use(b.n[k]);
    for (const auto& b : t.partBind)
        for (int k = 0; k < 8; k++) use(b.n[k]);
    for (int n : t.wNode) use(n);
    for (const SkinQuad& q : car.quads)
        for (int k = 0; k < 2; k++)
            for (int v : car.tris[q.tri + k].v) use(v);
    for (const Panel& p : car.panels) {
        for (int n : p.bodyNodes) use(n);
        for (int n : p.panelNodes) use(n);
    }
    std::vector<int>& w = cache[&t];
    for (int n = 0; n < (int)used.size(); n++)
        if (used[n]) w.push_back(n);
    return w;
}
}  // namespace

// Evaluates every vehicle's body and bound parts (in its own frame, when its shape has moved on) and its running gear
// for this frame, and uploads them.
void Renderer::prepareCars(const Sim& sim, DebugView debug, const V3& eye) {
    partVerts.clear();
    debug_ = (int)debug;
    if (carSlots.size() < sim.cars.size()) carSlots.resize(sim.cars.size());
    static std::vector<BodyDynamic> dyn;
    static std::vector<unsigned> opaque, glass;
    static std::vector<Vtx> local;
    static std::vector<float> now;
    rebuilds_ = 0;
    for (size_t ci = 0; ci < sim.cars.size(); ci++) {
        const Car& car = sim.cars[ci];
        const VisualTemplate* tp = tryVisualTemplate(car);
        if (!tp) {  // (still being made on a worker: the car joins the picture when it is ready)
            CarSlot& sl = carSlots[ci];
            sl.opaqueCount = sl.glassCount = sl.partsCount = 0, sl.builtTpl = nullptr, sl.intact = false, sl.lpos.clear();
            continue;
        }
        const VisualTemplate& t = *tp;
        CarSlot& slot = carSlots[ci];
        if (!slot.vao) {
            GenVertexArrays(1, &slot.vao);
            GenBuffers(1, &slot.dyn);
            GenBuffers(1, &slot.ibo);
            GenBuffers(1, &slot.glassIbo);
            GenVertexArrays(1, &slot.partsVao);
            GenBuffers(1, &slot.partsVbo);
            setupVtxVao(slot.partsVao, slot.partsVbo);
        }
        if (slot.tpl != &t) {
            GLuint staticVbo = 0;
            for (auto& tv : templateVbos)
                if (tv.first == &t) staticVbo = tv.second;
            if (!staticVbo) {
                GenBuffers(1, &staticVbo);
                BindBuffer(GL_ARRAY_BUFFER, staticVbo);
                BufferData(GL_ARRAY_BUFFER, t.rvStatic.size() * sizeof(BodyStatic), t.rvStatic.data(), GL_STATIC_DRAW);
                templateVbos.push_back({&t, staticVbo});
            }
            BindVertexArray(slot.vao);
            BindBuffer(GL_ARRAY_BUFFER, slot.dyn);
            VertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(BodyDynamic), (void*)0);
            VertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(BodyDynamic), (void*)12);
            VertexAttribPointer(2, 1, GL_FLOAT, 0, sizeof(BodyDynamic), (void*)24);
            BindBuffer(GL_ARRAY_BUFFER, staticVbo);
            VertexAttribPointer(3, 3, GL_FLOAT, 0, sizeof(BodyStatic), (void*)0);
            VertexAttribPointer(4, 1, GL_FLOAT, 0, sizeof(BodyStatic), (void*)12);
            for (int a = 0; a < 5; a++) EnableVertexAttribArray(a);
            slot.tpl = &t;
        }
        // The car's frame: its model matrix, and where its nodes are in it.
        const Frame fr = car.frame();
        M4& M = slot.model;
        M = m4identity();
        const V3 right = fr.left * -1.0;  // (fwd, up, right: a right-handed frame, so the cross products in it keep their sense)
        const V3 ax[3] = {fr.fwd, fr.up, right};
        for (int c = 0; c < 3; c++) M.m[c * 4] = (float)ax[c].x, M.m[c * 4 + 1] = (float)ax[c].y, M.m[c * 4 + 2] = (float)ax[c].z;
        M.m[12] = (float)fr.com.x, M.m[13] = (float)fr.com.y, M.m[14] = (float)fr.com.z;
        auto toLocal = [&](const V3& p) {
            const V3 d = p - fr.com;
            return V3(dot(d, fr.fwd), dot(d, fr.up), dot(d, right));
        };
        const std::vector<int>& watch = watchedNodes(car, t);
        now.resize(watch.size() * 3);
        double r2 = 0;
        for (size_t k = 0; k < watch.size(); k++) {
            const V3 l = toLocal(car.nodes[watch[k]].p);
            now[k * 3] = (float)l.x, now[k * 3 + 1] = (float)l.y, now[k * 3 + 2] = (float)l.z;
            r2 = std::max(r2, dot(l, l));
        }
        // Built again when anything has moved on by more than ~1/4 pixel (1.5 mm close up) since it last was.
        const double eps = std::max(0.0015, 0.0003 * len(fr.com - eye));
        bool moved = slot.builtTpl != &t || slot.snap.size() != now.size() || slot.debug != (int)debug;
        for (size_t k = 0; !moved && k < now.size(); k += 3) {
            const float dx = now[k] - slot.snap[k], dy = now[k + 1] - slot.snap[k + 1], dz = now[k + 2] - slot.snap[k + 2];
            moved = dx * dx + dy * dy + dz * dz > eps * eps;
        }
        int shattered = 0;
        for (const Tri& tr : car.tris) shattered += tr.shattered;
        const bool bodyDirty = moved || shattered != slot.shattered;

        const int beam = ci < carBeams.size() ? carBeams[ci] : 0;
        if (carLampState.size() < sim.cars.size()) carLampState.resize(sim.cars.size());
        CarLamps& lamps = carLampState[ci];
        carLamps(car, t, beam, lamps);
        // Lamps modelled as glowing parts (the Jesko's LED strips, the 4Runner's bumper lights): brighter when
        // lit, dark when smashed. Their glow is color^2.2 (lit shader).
        float redK[2], frontK[2];
        for (int sd = 0; sd < 2; sd++) {
            redK[sd] = lamps.tail[sd] < 0.5f ? 0.15f : 1 + 0.6f * (beam > 0) + 1.4f * (float)std::max(car.brake, car.handbrake * 0.6);
            frontK[sd] = lamps.head[sd] < 0.5f ? 0.15f : 1 + (beam == 2 ? 3.0f : beam ? 2.5f : 0.0f);
        }
        const float lampKey[5] = {redK[0], redK[1], frontK[0], frontK[1], 0};
        bool lampsChanged = false;
        for (int k = 0; k < 4; k++) lampsChanged |= std::fabs(lampKey[k] - slot.lampKey[k]) > 0.01f;

        if (debug != DEBUG_BEAMS_ONLY && (bodyDirty || lampsChanged)) {
            // Everything in the car's frame: the nodes relative to it, as evalBody and evalParts read them.
            static std::vector<V3> P;
            P.resize(car.nodes.size());
            for (size_t n = 0; n < car.nodes.size(); n++) P[n] = toLocal(car.nodes[n].p);
            if (bodyDirty) {
                dyn.clear();
                opaque.clear();
                glass.clear();
                if (debug != DEBUG_PARTS) evalBody(car, P, t, dyn, opaque, glass);
                BindBuffer(GL_ARRAY_BUFFER, slot.dyn);
                BufferData(GL_ARRAY_BUFFER, dyn.size() * sizeof(BodyDynamic), dyn.data(), GL_DYNAMIC_DRAW);
                BindVertexArray(slot.vao);
                BindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.glassIbo);
                BufferData(GL_ELEMENT_ARRAY_BUFFER, glass.size() * sizeof(unsigned), glass.data(), GL_DYNAMIC_DRAW);
                BindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ibo);
                BufferData(GL_ELEMENT_ARRAY_BUFFER, opaque.size() * sizeof(unsigned), opaque.data(), GL_DYNAMIC_DRAW);
                slot.opaqueCount = (int)opaque.size();
                slot.glassCount = (int)glass.size();
                slot.lpos.resize(dyn.size() * 3);
                for (size_t v = 0; v < dyn.size(); v++)
                    for (int k = 0; k < 3; k++) slot.lpos[v * 3 + k] = dyn[v].p[k];
                slot.lopaque = opaque, slot.lglass = glass;
                size_t whole = 0;  // (indices with no face torn off)
                for (const auto& f : t.faces) whole += f.rv[3] == f.rv[2] ? 3 : 6;
                slot.intact = !car.isProp() && opaque.size() == whole && car.damage < 0.03;
                slot.snap = now;
                slot.shattered = shattered, slot.debug = (int)debug, slot.builtTpl = &t;
                slot.builds++;
                rebuilds_++;
            }
            local.clear();
            evalParts(P, t, local);
            for (Vtx& pv : local) {
                if (pv.c[3] < 5.5f || pv.c[3] > 6.5f) continue;
                const int sd = pv.p[2] < 0 ? 0 : 1;  // (local z: toward the car's right)
                const bool front = pv.p[0] > 0, red = pv.c[0] > 2.5f * pv.c[1], amber = pv.c[2] < 0.5f * pv.c[0];
                const float k = red ? redK[sd] : front && !amber ? frontK[sd] : 1.0f;
                for (int c = 0; c < 3; c++) pv.c[c] *= k;
            }
            BindBuffer(GL_ARRAY_BUFFER, slot.partsVbo);
            BufferData(GL_ARRAY_BUFFER, local.size() * sizeof(Vtx), local.data(), GL_DYNAMIC_DRAW);
            slot.partsCount = (int)local.size();
            for (int k = 0; k < 4; k++) slot.lampKey[k] = lampKey[k];
        }
        if (debug == DEBUG_BEAMS_ONLY) slot.opaqueCount = slot.glassCount = slot.partsCount = 0, slot.debug = (int)debug;
        buildRunningGear(car, partVerts);
        slot.center = fr.com, slot.radius = std::sqrt(r2) + 0.5;
    }
    buildRopes(sim, partVerts);
    BindBuffer(GL_ARRAY_BUFFER, dynVbo);
    BufferData(GL_ARRAY_BUFFER, partVerts.size() * sizeof(Vtx), partVerts.data(), GL_STREAM_DRAW);
}

// Which copy of its skin a car draws: a far one where its cells (half of them: what clustering moves an edge by) come
// to under 3/4 of a pixel. The copy is built the first time it's wanted after the skin was.
int Renderer::skinLod(const CarSlot& slot) const {
    if (!slot.intact || slot.lpos.empty()) return 0;
    const double px = pxPerM_ / std::max(1.0, len(slot.center - eye_));
    static const double kCell[2] = {0.06, 0.15};
    int L = 0;
    for (int k = 0; k < 2; k++)
        if (kCell[k] * 0.5 * px < 0.75) L = k + 1;
    if (!L) return 0;
    CarSlot& s = const_cast<CarSlot&>(slot);
    if (s.lodBuilt[L - 1] != s.builds) {
        const VisualTemplate& t = *static_cast<const VisualTemplate*>(s.tpl);
        const double c = kCell[L - 1];
        const size_t nv = s.lpos.size() / 3;
        std::unordered_map<uint64_t, unsigned> cell;
        std::vector<unsigned> rep(nv);
        for (size_t v = 0; v < nv; v++) {
            const int64_t ix = (int64_t)std::floor(s.lpos[v * 3] / c), iy = (int64_t)std::floor(s.lpos[v * 3 + 1] / c), iz = (int64_t)std::floor(s.lpos[v * 3 + 2] / c);
            const int region = v < t.rvStatic.size() ? (int)t.rvStatic[v].region : 0;
            const uint64_t key = ((uint64_t)(ix & 0xffff) << 48) ^ ((uint64_t)(iy & 0xffff) << 32) ^ ((uint64_t)(iz & 0xffff) << 16) ^ (uint64_t)region;
            rep[v] = cell.emplace(key, (unsigned)v).first->second;
        }
        auto cluster = [&](const std::vector<unsigned>& in, std::vector<unsigned>& out) {
            out.clear();
            for (size_t i = 0; i + 2 < in.size(); i += 3) {
                const unsigned a = rep[in[i]], b = rep[in[i + 1]], d = rep[in[i + 2]];
                if (a != b && b != d && a != d) out.insert(out.end(), {a, b, d});
            }
        };
        static std::vector<unsigned> o, g;
        cluster(s.lopaque, o);
        cluster(s.lglass, g);
        if (!s.lodIbo[L - 1]) GenBuffers(1, &s.lodIbo[L - 1]), GenBuffers(1, &s.lodGlassIbo[L - 1]);
        BindVertexArray(s.vao);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.lodGlassIbo[L - 1]);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, g.size() * sizeof(unsigned), g.data(), GL_DYNAMIC_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.lodIbo[L - 1]);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, o.size() * sizeof(unsigned), o.data(), GL_DYNAMIC_DRAW);
        s.lodCount[L - 1] = (int)o.size(), s.lodGlassCount[L - 1] = (int)g.size();
        s.lodBuilt[L - 1] = s.builds;
    }
    return L;
}

void Renderer::drawCarBodies(const Sim& sim, GLuint prog, bool glassPass) {
    for (size_t ci = 0; ci < sim.cars.size() && ci < carSlots.size(); ci++) {
        if (!carShown(ci)) continue;
        const Car& car = sim.cars[ci];
        const CarSlot& slot = carSlots[ci];
        if (!slot.tpl) continue;
        const VisualTemplate& t = *static_cast<const VisualTemplate*>(slot.tpl);
        if (prog == progCar) {
            Uniform3f(uniformLocation(prog, "uPaint"), (float)car.color.x, (float)car.color.y, (float)car.color.z);
            Uniform1i(uniformLocation(prog, "uPass"), glassPass ? 1 : 0);
            Uniform1f(uniformLocation(prog, "uBrake"), (float)std::max(car.brake, car.handbrake * 0.6));
            const CarLamps lp = ci < carLampState.size() ? carLampState[ci] : CarLamps();
            const float level = lp.beam == 2 ? 1.5f : lp.beam ? 1.0f : 0.0f, tailOn = lp.beam ? 1.0f : 0.0f;
            Uniform4f(uniformLocation(prog, "uLampOn"), level * lp.head[0], level * lp.head[1], tailOn * lp.tail[0], tailOn * lp.tail[1]);
            Uniform4f(uniformLocation(prog, "uLampOk"), lp.head[0], lp.head[1], lp.tail[0], lp.tail[1]);
            Uniform1f(uniformLocation(prog, "uReverse"), car.gear < 0 ? 1.0f : 0.0f);
            Uniform4f(uniformLocation(prog, "uCabin"), t.cabin[0], t.cabin[1], t.cabin[2], t.cabin[3]);
            Uniform4f(uniformLocation(prog, "uBody"), t.body[0], t.body[1], t.body[2], t.body[3]);
            Uniform4f(uniformLocation(prog, "uStyle"), t.style[0], t.style[1], t.style[2], t.style[3]);
            const VehicleSpec& vs = *car.spec;
            Uniform1i(uniformLocation(prog, "uVehicle"), vs.model);
            Uniform4f(uniformLocation(prog, "uTrim"), vs.trimColor[0], vs.trimColor[1], vs.trimColor[2], vs.trimTop);
            const float* sp = t.shellParam;
            Uniform1i(uniformLocation(prog, "uShell"), t.shell);
            Uniform4f(uniformLocation(prog, "uShA"), sp[0], sp[1], sp[2], sp[3]);
            Uniform4f(uniformLocation(prog, "uShB"), sp[4], sp[5], sp[6], sp[7]);
            Uniform4f(uniformLocation(prog, "uShC"), sp[8], sp[9], sp[10], sp[11]);
            Uniform4f(uniformLocation(prog, "uShD"), sp[12], sp[13], sp[14], sp[15]);
            Uniform4f(uniformLocation(prog, "uShE"), sp[16], sp[17], sp[18], sp[19]);
        }
        uniformM4(prog, "uModel", slot.model);
        BindVertexArray(slot.vao);
        const int L = skinLod(slot);
        const int n = L ? (glassPass ? slot.lodGlassCount[L - 1] : slot.lodCount[L - 1]) : glassPass ? slot.glassCount : slot.opaqueCount;
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, L ? (glassPass ? slot.lodGlassIbo[L - 1] : slot.lodIbo[L - 1]) : glassPass ? slot.glassIbo : slot.ibo);
        DrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, nullptr);
        carKind_ = glassPass ? 1 : 0;
        countCar(n);
    }
    uniformM4(prog, "uModel", m4identity());
}

// Which cars the pass with this view-projection sees (a box round each one's frame, wheels and all), and in the main
// pass, which the terrain doesn't hide (occlusion.h).
void Renderer::cullCars(size_t cars, const M4& vp, bool mainPass) {
    carVisible_.assign(cars, 1);
    for (size_t ci = 0; ci < cars && ci < carSlots.size(); ci++) {
        const CarSlot& slot = carSlots[ci];
        if (slot.radius <= 0) continue;
        RenderBounds b;
        const double r = slot.radius + 0.6;
        b.add(slot.center - V3(r, r, r));
        b.add(slot.center + V3(r, r, r));
        carVisible_[ci] = b.visible(vp) && !(mainPass && occl_.occluded(b));  // (behind a hill: the main pass only, never the shadows)
    }
}

// The parts bound to each car's lattice, in its frame (prepareCars).
void Renderer::drawCarParts(const Sim& sim, GLuint prog) {
    for (size_t ci = 0; ci < sim.cars.size() && ci < carSlots.size(); ci++) {
        const CarSlot& slot = carSlots[ci];
        if (!slot.partsCount || !carShown(ci)) continue;
        uniformM4(prog, "uModel", slot.model);
        BindVertexArray(slot.partsVao);
        carKind_ = 2;
        carDrawArrays(0, slot.partsCount);
    }
    uniformM4(prog, "uModel", m4identity());
}

namespace {
// What a car's parts mesh is merged from: the models and their builds, in order (not where they are).
uint64_t partsSignature(const std::vector<Placed>& parts) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const Placed& pl : parts) {
        for (char ch : pl.id) mix((unsigned char)ch);
        mix((uint64_t)(pl.variant + 7) * 31 + pl.bare * 3 + pl.clip);
        for (const auto& kv : pl.set) {
            for (char ch : kv.first) mix((unsigned char)ch);
            mix((uint64_t)std::llround(kv.second * 4096));
        }
        mix(0xff);
    }
    return h ^ parts.size();
}
// The same without the params: which models are in the merge, in which order.
uint64_t partsShape(const std::vector<Placed>& parts) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const Placed& pl : parts) {
        for (char ch : pl.id) mix((unsigned char)ch);
        mix((uint64_t)(pl.variant + 7) * 31 + pl.bare * 3 + pl.clip);
        mix(0xff);
    }
    return h ^ parts.size();
}
// Whether p (the car's frame) is shut inside its skin as last built: a ray up, down and to either side each crosses
// it an odd number of times. (A crack or a shared edge only ever makes it look outside: then it's drawn.)
bool insideSkin(const std::vector<float>& P, const std::vector<unsigned>& idx, const V3& p) {
    int odd[4] = {0, 0, 0, 0};
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const float* a = &P[idx[i] * 3];
        const float* b = &P[idx[i + 1] * 3];
        const float* c = &P[idx[i + 2] * 3];
        for (int dir = 0; dir < 2; dir++) {  // 0: along y (the x, z plane), 1: along z (the x, y plane)
            const int u = 0, v = dir == 0 ? 2 : 1, w = dir == 0 ? 1 : 2;
            const double pu = u == 0 ? p.x : 0, pv = v == 2 ? p.z : p.y, pw = w == 1 ? p.y : p.z;
            if (pu < std::min({a[u], b[u], c[u]}) || pu > std::max({a[u], b[u], c[u]}) || pv < std::min({a[v], b[v], c[v]}) || pv > std::max({a[v], b[v], c[v]})) continue;
            const double d = (b[u] - a[u]) * (c[v] - a[v]) - (c[u] - a[u]) * (b[v] - a[v]);
            if (std::fabs(d) < 1e-12) continue;
            const double l1 = ((b[u] - pu) * (c[v] - pv) - (c[u] - pu) * (b[v] - pv)) / d;
            const double l2 = ((c[u] - pu) * (a[v] - pv) - (a[u] - pu) * (c[v] - pv)) / d;
            const double l3 = 1 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            const double hit = l1 * a[w] + l2 * b[w] + l3 * c[w];
            odd[dir * 2 + (hit > pw ? 0 : 1)] ^= 1;
        }
    }
    return odd[0] && odd[1] && odd[2] && odd[3];
}
// The part's mesh without the faces hidden inside it (meshopt.h: degenerate and hidden faces only; the flats aren't
// merged, so a clipped or stretched part is cut and shaded as before). Kept per model build.
std::map<std::string, mk::Mesh>& partMeshCache() {
    static std::map<std::string, mk::Mesh> cache;
    static uint64_t version = 0;
    if (version != mk::picksVersion()) cache.clear(), version = mk::picksVersion();
    return cache;
}
std::string partMeshKey(const Placed& pl) {
    std::string key = pl.id + "#" + std::to_string(pl.variant) + (pl.bare ? "b" : "");
    for (const auto& kv : pl.set) key += "|" + kv.first + "=" + std::to_string(kv.second);
    return key;
}
mk::Mesh buildPartMesh(const Placed& pl) {  // (no shared state besides the model kit's own: any thread)
    mk::Mesh m = pl.bare || pl.variant >= 0 ? buildPlacedMesh(pl) : pl.set.empty() ? mk::modelMesh(pl.id) : mk::modelMesh(pl.id, pl.set);
    MeshOptOptions o;
    o.coplanar = false;
    optimizeMesh(m, o);
    return m;
}
const mk::Mesh& partMesh(const Placed& pl, mk::Mesh&) {
    auto& cache = partMeshCache();
    const std::string key = partMeshKey(pl);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    return cache.emplace(key, buildPartMesh(pl)).first->second;
}
// Whether the part's mesh is ready. If not, it is being built on another thread (started here): a car whose params only
// ease (a leaf spring's camber as it settles) keeps its last merge a few frames instead of stalling 7 ms on the build.
bool partMeshReady(const Placed& pl) {
    static std::map<std::string, std::future<mk::Mesh>> pending;
    auto& cache = partMeshCache();
    const std::string key = partMeshKey(pl);
    if (cache.count(key)) return true;
    auto it = pending.find(key);
    if (it == pending.end()) {
        if (pending.size() >= 6) return false;
        pending.emplace(key, std::async(std::launch::async, [pl] { return buildPartMesh(pl); }));
        return false;
    }
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    cache.emplace(key, it->second.get());
    pending.erase(it);
    return true;
}
}  // namespace

// Merges each car's parts again if they changed, and lays out this frame's transforms (one texture for all cars).
void Renderer::prepareCarModels() {
    for (CarSlot& slot : carSlots) slot.modelsParts = 0;
    partXf.clear();
    int base = 0;
    for (const CarModels& cm : carModels) {
        if (cm.car < 0 || cm.car >= (int)carSlots.size() || cm.parts.empty()) continue;
        CarSlot& slot = carSlots[cm.car];
        if (!slot.vao) continue;  // (not prepared: prepareCars runs first)
        const uint64_t sig = partsSignature(cm.parts);
        if (!slot.modelsVao) {
            GenVertexArrays(1, &slot.modelsVao);
            GenBuffers(1, &slot.modelsVbo);
            GenBuffers(1, &slot.modelsIdx);
            setupVtxVao(slot.modelsVao, slot.modelsVbo);
            BindBuffer(GL_ARRAY_BUFFER, slot.modelsIdx);
            VertexAttribPointer(3, 1, GL_FLOAT, 0, sizeof(float), (void*)0);
            EnableVertexAttribArray(3);
        }
        const uint64_t shape = partsShape(cm.parts);
        bool wait = false;
        if (sig != slot.modelsSig && slot.modelsCount && slot.modelsShape == shape)
            for (const Placed& pl : cm.parts) wait |= !partMeshReady(pl);
        if ((sig != slot.modelsSig || !slot.modelsCount) && !wait) {
            slot.modelsShape = shape;
            static std::vector<Vtx> v;
            static std::vector<float> idx;
            mk::Mesh scratch;
            v.clear(), idx.clear();
            // The parts shut inside the intact body (the engine under its hood...) go last, so they can be left off the draw:
            // every corner of their bounds (padded 3 cm) inside the skin and below its lowest glass.
            std::vector<char> shut(cm.parts.size(), 0);
            // A car already shown that gains parts (the frame's, as the camera comes near) fills in over a few frames, not one stall:
            // a few shut tests and the meshes that are ready; the rest is drawn as it is (inside the body, hidden by the depth test) or left out
            // until its mesh is built (on a worker).
            const bool fresh = slot.modelsCount == 0;
            int testsLeft = 4;
            bool unfinished = false;
            // (the test costs 10 ms or more for a car: kept per part, by its model, params and place in the body, for as long as the skin stands)
            if (slot.shutSkin != slot.lpos.size() || slot.shutIntact != slot.intact) slot.shutCache.clear(), slot.shutSkin = slot.lpos.size(), slot.shutIntact = slot.intact;
            if (slot.intact && !slot.lglass.empty()) {
                const M4& M = slot.model;
                const V3 ax[3] = {V3(M.m[0], M.m[1], M.m[2]), V3(M.m[4], M.m[5], M.m[6]), V3(M.m[8], M.m[9], M.m[10])}, o(M.m[12], M.m[13], M.m[14]);
                double belt = 1e9;
                for (unsigned i : slot.lglass) belt = std::min(belt, (double)slot.lpos[i * 3 + 1]);
                for (size_t k = 0; k < cm.parts.size(); k++) {
                    const Placed& pl = cm.parts[k];
                    V3 a[3];
                    if (pl.oriented) a[0] = pl.ax[0], a[1] = pl.ax[1], a[2] = pl.ax[2];
                    else a[0] = V3(std::cos(pl.yaw), 0, -std::sin(pl.yaw)), a[1] = V3(0, 1, 0), a[2] = V3(-a[0].z, 0, a[0].x);
                    uint64_t h = partsSignature({pl});
                    const auto mix = [&](double v, double q = 100) { h = (h ^ (uint64_t)std::llround(v * q)) * 1099511628211ull; };  // (1 cm, the axes to 0.05: the body flexes)
                    const V3 d = pl.p - o;
                    mix(dot(d, ax[0])), mix(dot(d, ax[1])), mix(dot(d, ax[2]));
                    for (int c = 0; c < 3; c++) mix(dot(a[c], ax[0]), 20), mix(dot(a[c], ax[1]), 20), mix(dot(a[c], ax[2]), 20);
                    mix(pl.scale.x, 20), mix(pl.scale.y, 20), mix(pl.scale.z, 20), mix(pl.wear, 20);
                    auto hit = slot.shutCache.find(h);
                    if (hit != slot.shutCache.end()) { shut[k] = hit->second; continue; }
                    if (!fresh && (testsLeft <= 0 || !partMeshReady(pl))) { unfinished = true; continue; }
                    testsLeft--;
                    const mk::Mesh& m = partMesh(pl, scratch);
                    if (m.v.empty() || pl.wear) { slot.shutCache[h] = 0; continue; }
                    bool in = true;
                    for (int cnr = 0; cnr < 8 && in; cnr++) {
                        const double pad = 0.03;
                        const V3 q((cnr & 1 ? m.hi.x : m.lo.x), (cnr & 2 ? m.hi.y : m.lo.y), (cnr & 4 ? m.hi.z : m.lo.z));
                        V3 w = pl.p + a[0] * (q.x * pl.scale.x) + a[1] * (q.y * pl.scale.y) + a[2] * (q.z * pl.scale.z);
                        w += (a[0] * (cnr & 1 ? 1.0 : -1.0) + a[1] * (cnr & 2 ? 1.0 : -1.0) + a[2] * (cnr & 4 ? 1.0 : -1.0)) * pad;
                        const V3 dd = w - o, l(dot(dd, ax[0]), dot(dd, ax[1]), dot(dd, ax[2]));
                        in = l.y < belt - 0.02 && insideSkin(slot.lpos, slot.lopaque, l);
                    }
                    shut[k] = in;
                    slot.shutCache[h] = in;
                }
                if (slot.shutCache.size() > 4000) slot.shutCache.clear();
            }
            int shown = 0;
            for (int pass = 0; pass < 2; pass++) {
                for (size_t k = 0; k < cm.parts.size(); k++) {
                    if (shut[k] != pass) continue;
                    if (!fresh && !partMeshReady(cm.parts[k])) { unfinished = true; continue; }
                    for (const mk::MVert& q : partMesh(cm.parts[k], scratch).v) {
                        v.push_back(Vtx{{q.p[0], q.p[1], q.p[2]}, {q.n[0], q.n[1], q.n[2]}, {q.c[0], q.c[1], q.c[2], (float)q.mat}});
                        idx.push_back((float)k);
                    }
                }
                if (pass == 0) shown = (int)v.size();
            }
            slot.modelsShown = shown;
            BindBuffer(GL_ARRAY_BUFFER, slot.modelsVbo);
            BufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vtx), v.data(), GL_STATIC_DRAW);
            BindBuffer(GL_ARRAY_BUFFER, slot.modelsIdx);
            BufferData(GL_ARRAY_BUFFER, idx.size() * sizeof(float), idx.data(), GL_STATIC_DRAW);
            slot.modelsCount = (int)v.size();
            slot.modelsSig = unfinished ? ~sig : sig;  // (not done: the next frame goes on)
            rebuilds_++;
        }
        slot.modelsBase = base, slot.modelsParts = (int)cm.parts.size();
        for (const Placed& pl : cm.parts) {
            V3 a[3];
            if (pl.oriented) a[0] = pl.ax[0], a[1] = pl.ax[1], a[2] = pl.ax[2];
            else {
                const V3 f(std::cos(pl.yaw), 0, -std::sin(pl.yaw));
                a[0] = f, a[1] = V3(0, 1, 0), a[2] = V3(-f.z, 0, f.x);
            }
            const double sc[3] = {pl.scale.x, pl.scale.y, pl.scale.z};
            const float w[3] = {pl.clip ? pl.clipLo : -1e9f, pl.clip ? pl.clipHi : 1e9f, (pl.sunlit ? 100.0f : 0.0f) + 200.0f * pl.wear};
            for (int c = 0; c < 3; c++) {
                const V3 e = a[c] * sc[c];
                partXf.insert(partXf.end(), {(float)e.x, (float)e.y, (float)e.z, w[c]});
            }
            partXf.insert(partXf.end(), {(float)pl.p.x, (float)pl.p.y, (float)pl.p.z, 0.0f});
        }
        base += (int)cm.parts.size();
    }
    if (!base) return;
    const int rows = (base + 255) / 256;
    partXf.resize((size_t)rows * 256 * 16, 0.0f);
    if (!partTex) {
        GenTextures(1, &partTex);
        BindTexture(GL_TEXTURE_2D, partTex);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    BindTexture(GL_TEXTURE_2D, partTex);
    if (rows != partRows) TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1024, rows, 0, GL_RGBA, GL_FLOAT, partXf.data()), partRows = rows;
    else TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, rows, GL_RGBA, GL_FLOAT, partXf.data());
}

// Every car's parts, one draw each (prog: progParts or progPartsDepth, its other uniforms set).
void Renderer::drawCarModels(GLuint prog) {
    if (!partRows) return;
    ActiveTexture(GL_TEXTURE0 + 9);
    BindTexture(GL_TEXTURE_2D, partTex);
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(uniformLocation(prog, "uParts"), 9);
    for (size_t ci = 0; ci < carSlots.size(); ci++) {
        const CarSlot& slot = carSlots[ci];
        if (!slot.modelsParts || !slot.modelsCount || !carShown(ci)) continue;
        Uniform1i(uniformLocation(prog, "uPartBase"), slot.modelsBase);
        BindVertexArray(slot.modelsVao);
        carKind_ = 5;
        // (what the intact body shuts in isn't drawn: in the parts view it's all there, and a body that's been opened up shows it)
        carDrawArrays(0, slot.intact && debug_ == DEBUG_OFF ? slot.modelsShown : slot.modelsCount);
    }
}

void Renderer::drawScene(const Sim& sim, const View& view, DebugView debug, Effects* effects) {
    static const bool noOcclusion = std::getenv("CZ_NO_OCCLUSION") != nullptr;  // (the reference render of the pixel test)
    occl_.enable(!noOcclusion);
    occl_.refresh();
    occl_.setEye(view.eye);
    passMark(FP_OPAQUE);
    const auto benchClock = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const double t0 = benchClock();
    buildFigures();
    const double t1 = benchClock();
    prepareCars(sim, debug, view.eye);
    prepareCarModels();
    const double t2 = benchClock();

    // Headlamps lit as real lights: the nearest cars with their lamps on, two spots each,
    // so the shaders' per-pixel loop stays short however many cars there are.
    {
        std::vector<std::pair<double, int>> lit;
        for (size_t ci = 0; ci < sim.cars.size() && ci < carLampState.size(); ci++) {
            const CarLamps& lp = carLampState[ci];
            if (lp.placed && lp.beam > 0 && lp.head[0] + lp.head[1] > 0.1f)
                lit.push_back({len(lp.headPos[0] - view.eye), (int)ci});
        }
        std::sort(lit.begin(), lit.end());
        lampCount = 0;
        for (const auto& l : lit) {
            const CarLamps& lp = carLampState[l.second];
            for (int s = 0; s < 2 && lampCount < kMaxLamps; s++) {
                if (lp.head[s] < 0.1f) continue;
                float* P = lampPos + lampCount * 4;
                float* D = lampDir + lampCount * 4;
                float* U = lampUp + lampCount * 4;
                const V3 p = lp.headPos[s] + lp.fwd * 0.05;
                P[0] = (float)p.x, P[1] = (float)p.y, P[2] = (float)p.z, P[3] = (float)lp.beam;
                D[0] = (float)lp.fwd.x, D[1] = (float)lp.fwd.y, D[2] = (float)lp.fwd.z, D[3] = lp.beam == 2 ? 160.0f : 90.0f;
                // Peak intensities: a low beam lights the road about evenly out to ~30 m,
                // a high beam reaches past 100 m.
                U[0] = (float)lp.up.x, U[1] = (float)lp.up.y, U[2] = (float)lp.up.z;
                U[3] = (lp.beam == 2 ? 45000.0f : 20000.0f) * lp.head[s];
                lampCount++;
            }
        }
    }

    {  // (switched effects ease rather than pop)
        const float step = (float)clampd(view.time - fadeClock_, 0.0, 0.1) / 0.6f;
        fadeClock_ = view.time;
        auto ease = [&](float& f, bool on) { f = f < 0 ? (on ? 1.0f : 0.0f) : on ? std::min(1.0f, f + step) : std::max(0.0f, f - step); };
        ease(fadeAO_, settings.ao), ease(fadeBloom_, settings.bloom), ease(fadeClouds_, settings.clouds);
    }
    static const int kShadowRes[4] = {0, 1024, 2048, 4096}, kShadowTaps[4] = {0, 5, 9, 16};
    const int sq = std::min(std::max(settings.shadows, 0), 3);
    if (sq) ensureShadows(kShadowRes[sq]);
    const float scale = std::min(std::max(settings.renderScale, 0.25f), 2.0f);
    ensureTargets(std::max(1, (int)std::lround(view.width * scale)), std::max(1, (int)std::lround(view.height * scale)),
                  settings.msaa);

    // Light cameras: orthographic boxes, a sharp one just ahead of the view target and a
    // wide one out to the treeline, each snapped to its texels so edges don't shimmer.
    // (The key light: the sun, or the moon at night.)
    const V3 keyDir = daylight.keyDir;
    M4 lightView = lookAt(V3(0, 0, 0), -keyDir, V3(0, 1, 0));
    V3 ahead = view.target - view.eye;
    ahead.y = 0;
    ahead = len(ahead) > 1e-3 ? norm(ahead) : V3();
    const float cascadeHalf[2] = {26.0f, 230.0f}, cascadeDepth[2] = {150.0f, 500.0f};
    M4 lightVP[2];
    for (int c = 0; c < 2; c++) {
        V3 ft = transformPoint(lightView, view.target + ahead * (cascadeHalf[c] * 0.5));
        float texel = 2 * cascadeHalf[c] / std::max(1, shadowSize), hh = cascadeHalf[c];
        float fx = std::floor((float)ft.x / texel) * texel, fy = std::floor((float)ft.y / texel) * texel;
        lightVP[c] = ortho(fx - hh, fx + hh, fy - hh, fy + hh, (float)-ft.z - cascadeDepth[c], (float)-ft.z + cascadeDepth[c]) *
                     lightView;
    }

    Enable(GL_DEPTH_TEST);
    DepthFunc(GL_LEQUAL);
    Disable(GL_CULL_FACE);

    // Scanned props: rebuild the scenery if their quality changed, pick the scatter round the camera.
    passMark(FP_PROPS);
    if (settings.props != propsApplied) updateObjects(sim.world);
    static const bool noSeeThrough = std::getenv("CZ_NO_SEETHROUGH") != nullptr;  // (for comparing screenshots)
    const SeeThrough seeThrough{view.eye, view.focus, view.hasFocus && !noSeeThrough, noSeeThrough ? 0.0f : view.fadeNear};
    props.setSeeThrough(seeThrough);
    props.occluder = &occl_;
    props.beginFrame(sim.world, view.eye, M4(), propsOk ? settings.props : 0, view.time);

    farZ_ = settings.drawDistance > 0 ? std::max(150.0f, settings.drawDistance) : 6000.0f;  // (a draw distance: everything past it is clipped)
    M4 proj = perspective(view.fovy, (float)view.width / std::max(1, view.height), 0.05f, farZ_);
    M4 vp = proj * lookAt(view.eye, view.target, view.up);
    // Wheels past where a mud tire's lug (2.4 cm proud) is under half a pixel draw their far copies.
    eye_ = view.eye;
    pxPerM_ = view.height * scale / (2 * std::tan(view.fovy * 0.5));
    wheelLodDist_ = 0.05 * pxPerM_;
    static const char* noLod = std::getenv("CZ_NO_CARLOD");  // (for comparing screenshots: 1 no far copies, 2 no wheel ones, 3 no skin ones)
    if (noLod && (noLod[0] == '1' || noLod[0] == '2')) wheelLodDist_ = 1e9;
    if (noLod && (noLod[0] == '1' || noLod[0] == '3')) pxPerM_ = 1e9;
    geometryStats_ = {};
    geometryStats_.carPartsMs = t1 - t0, geometryStats_.carPrepMs = t2 - t1, geometryStats_.carRebuilds = rebuilds_;
    vegStats_ = {};
    static const char* noVegEnv = std::getenv("CZ_NO_VEG");  // (timing: the frame without the trees and bushes; "shadow" or "main": that pass only)
    static const bool noVeg = noVegEnv && std::strcmp(noVegEnv, "shadow") != 0, noVegShadow = noVegEnv && std::strcmp(noVegEnv, "main") != 0;

    // Shadow passes.
    passMark(FP_SHADOWS);
    for (int c = 0; sq && c < 2; c++) {
        BindFramebuffer(GL_FRAMEBUFFER, shadowFbo[c]);
        Viewport(0, 0, shadowSize, shadowSize);
        Clear(GL_DEPTH_BUFFER_BIT);
        Enable(GL_POLYGON_OFFSET_FILL);
        PolygonOffset(1.5f, 3.0f);
        UseProgram(progDepth);
        uniformM4(progDepth, "uLightVP", lightVP[c]);
        uniformM4(progDepth, "uModel", m4identity());
        drawBatches(boxVao, boxBatches, lightVP[c], true);
        if (figVertexCount > 0) {
            BindVertexArray(figVao);
            DrawArrays(GL_TRIANGLES, 0, figVertexCount);
            geometryStats_.shadowModelVertices += figVertexCount, geometryStats_.shadowModelDraws++;
        }
        UseProgram(progTreeDepth);
        uniformM4(progTreeDepth, "uLightVP", lightVP[c]);
        if (!noVegShadow) {
            // (shadows from the lighter copies; a bush's only near the eye: further off it's a speck)
            geometryStats_.shadowSceneryVertices += trees_.draw({lightVP[c], view.eye, vegDetail(), 1 + c, true, 1}, &vegStats_.treesShadow);
            geometryStats_.shadowSceneryVertices += bushes_.draw({lightVP[c], view.eye, vegDetail(), 0, true, 0.5}, &vegStats_.bushesShadow);
        }
        UseProgram(progDepth);
        drawBatches(sceneryVao, sceneryBatches, lightVP[c], true);
        if (!noVegShadow) drawBushes(lightVP[c], view.eye, true, vegStats_.logsShadow);
        // (The far cascade, a hand a texel, takes the bodies and wheels: what's under and inside them doesn't show at that size.)
        shadowPass_ = true;
        cullCars(sim.cars.size(), lightVP[c], false);
        drawCarBodies(sim, progDepth, false);
        BindVertexArray(dynVao);
        if (c == 0) carKind_ = 3, carDrawArrays(0, (GLsizei)partVerts.size());
        if (c == 0) drawCarParts(sim, progDepth);
        drawWheels(sim, progDepth);
        if (partRows && c == 0) {
            UseProgram(progPartsDepth);
            uniformM4(progPartsDepth, "uLightVP", lightVP[c]);
            drawCarModels(progPartsDepth);
            UseProgram(progDepth);
        }
        carVisible_.clear();
        shadowPass_ = false;
        props.drawShadow(lightVP[c], c, keyDir);
        Disable(GL_POLYGON_OFFSET_FILL);
    }

    // Main pass, into the HDR target.
    passMark(FP_SKY);
    cullCars(sim.cars.size(), vp, true);
    BindFramebuffer(GL_FRAMEBUFFER, targetSamples ? msFbo : sceneFbo);
    Viewport(0, 0, targetW, targetH);
    ClearColor(kFog[0], kFog[1], kFog[2], 1);
    Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    M4 invVP;
    invert(vp, invVP);

    auto setCommon = [&](GLuint prog) {
        UseProgram(prog);
        uniformV3(prog, "uSun", keyDir);
        uniformV3(prog, "uCam", view.eye);
        Uniform3f(uniformLocation(prog, "uFog"), kFog[0], kFog[1], kFog[2]);
        Uniform1f(uniformLocation(prog, "uTime"), view.time);
        Uniform1f(uniformLocation(prog, "uClouds"), fadeClouds_);
        Uniform1f(uniformLocation(prog, "uFogFar"), settings.drawDistance > 0 ? farZ_ : 0.0f);
        const Daylight& d = daylight;
        const std::pair<const char*, V3> sky[] = {
            {"uKeyCol", d.keyCol},     {"uAmbSky", d.ambSky},       {"uAmbGround", d.ambGround}, {"uSunDir", d.sunDir},
            {"uMoonDir", d.moonDir},   {"uSkyZen", d.skyZen},       {"uSkyHor", d.skyHor},       {"uDusk", d.duskCol},
            {"uSunGlow", d.sunGlow},   {"uSunDisc", d.sunDisc},     {"uCloudLit", d.cloudLit},   {"uCloudDark", d.cloudDark},
            {"uMoonCol", d.moonCol},   {"uGround", d.groundCol}};
        for (const auto& u : sky) uniformV3(prog, u.first, u.second);
        Uniform1f(uniformLocation(prog, "uStars"), d.stars);
        Uniform1f(uniformLocation(prog, "uNight"), d.night);
        Uniform1f(uniformLocation(prog, "uMoonPhase"), d.moonPhase);
        float rot[9];
        for (int k = 0; k < 9; k++) rot[k] = (float)d.skyRot[k];
        UniformMatrix3fv(uniformLocation(prog, "uSkyRot"), 1, 1, rot);  // (row major: transposed)
        Uniform1i(uniformLocation(prog, "uLampN"), lampCount);
        if (lampCount) {
            Uniform4fv(uniformLocation(prog, "uLampPos"), lampCount, lampPos);
            Uniform4fv(uniformLocation(prog, "uLampDir"), lampCount, lampDir);
            Uniform4fv(uniformLocation(prog, "uLampUp"), lampCount, lampUp);
        }
    };
    DepthMask(0);
    setCommon(progSky);
    uniformM4(progSky, "uInvVP", invVP);
    BindVertexArray(skyVao);
    DrawArrays(GL_TRIANGLES, 0, 3);
    DepthMask(1);

    auto setLighting = [&](GLuint prog) {
        setCommon(prog);
        uniformM4(prog, "uVP", vp);
        uniformM4(prog, "uLightVP", lightVP[0]);
        uniformM4(prog, "uLightVP0", lightVP[0]);
        uniformM4(prog, "uLightVP1", lightVP[1]);
        Uniform4f(uniformLocation(prog, "uShadowCfg"), sq ? 1.0f : 0.0f, (float)kShadowTaps[sq], 0, 0);
        for (int c = 0; c < 2; c++) {
            ActiveTexture(GL_TEXTURE0 + c);
            BindTexture(GL_TEXTURE_2D, shadowTex[c]);
        }
        ActiveTexture(GL_TEXTURE0);
        Uniform1i(uniformLocation(prog, "uShadow0"), 0);
        Uniform1i(uniformLocation(prog, "uShadow1"), 1);
    };
    auto bindTerrainTex = [&](GLuint prog) {
        ActiveTexture(GL_TEXTURE0 + 2);
        BindTexture(GL_TEXTURE_2D, terrainTex);
        ActiveTexture(GL_TEXTURE0);
        Uniform1i(uniformLocation(prog, "uTerrainTex"), 2);
        Uniform1f(uniformLocation(prog, "uTerrainN"), (float)terrainGridN);
    };
    setLighting(progLit);
    uniformM4(progLit, "uModel", m4identity());
    bindTerrainTex(progLit);
    Uniform1i(uniformLocation(progLit, "uMap"), mapId);
    // Bare rock and snow: high on the proving ground's rim; lower on the mountain,
    // whose forest climbs to ~175 m.
    if (mapId == MAP_MOUNTAIN) Uniform2f(uniformLocation(progLit, "uRockSnow"), 165.0f, 205.0f);
    else if (mapId == MAP_FRONTIER) Uniform2f(uniformLocation(progLit, "uRockSnow"), 125.0f, 400.0f);  // (its snow is painted on)
    else if (mapId == MAP_BACKCOUNTRY) Uniform2f(uniformLocation(progLit, "uRockSnow"), 105.0f, 400.0f);  // (its snow is painted on)
    else Uniform2f(uniformLocation(progLit, "uRockSnow"), 60.0f, 240.0f);
    passMark(FP_OPAQUE);
    UseProgram(progLit);
    drawBatches(boxVao, boxBatches, vp, false);
    if (figVertexCount > 0) {
        BindVertexArray(figVao);
        DrawArrays(GL_TRIANGLES, 0, figVertexCount);
        geometryStats_.modelVertices += figVertexCount, geometryStats_.modelDraws++;
    }
    passMark(FP_VEG);
    // The code-made trees the camera sees through (close to it, or across the line to the car) dither away:
    // the few most hidden go to the shader as (x, z, reach, fade).
    {
        const SeeThrough& st = seeThrough;
        float list[24 * 4];
        int n = 0;
        const double dt = clampd(view.time - fadeTime_, 0.0, 0.1);
        fadeTime_ = view.time;
        if (treeFade_.size() == sim.world.trees.size() && (st.nearScale > 0 || st.hasFocus))
            for (size_t i = 0; i < sim.world.trees.size(); i++) {
                const Tree& t = sim.world.trees[i];
                if ((t.kind == 0 && scanCover_.pines) || (t.kind == 2 && scanCover_.snags)) continue;
                float& f = treeFade_[i];
                const bool far = std::fabs(t.base.x - st.eye.x) > 40 || std::fabs(t.base.z - st.eye.z) > 40;
                f = far ? 0.0f : treefade::ease(f, treefade::target(st, t.base, t.height, t.radius), dt);
                if (f < 0.02f) continue;
                int at = n;
                if (n < 24) n++;
                else {  // (full: replace the least hidden if this one is more)
                    at = 0;
                    for (int k = 1; k < 24; k++)
                        if (list[k * 4 + 3] < list[at * 4 + 3]) at = k;
                    if (list[at * 4 + 3] >= f) continue;
                }
                list[at * 4] = (float)t.base.x, list[at * 4 + 1] = (float)t.base.z, list[at * 4 + 2] = (float)(t.radius * 1.1 + 0.4), list[at * 4 + 3] = f;
            }
        setLighting(progTree);
        bindTerrainTex(progTree);
        Uniform1i(uniformLocation(progTree, "uMap"), mapId);
        Uniform1i(uniformLocation(progTree, "uTreeFadeN"), n);
        if (n) Uniform4fv(uniformLocation(progTree, "uTreeFade"), n, list);
    }
    if (!noVeg) {
        const forest::View tv{vp, view.eye, vegDetail(), 0, false, 1, &occl_};
        geometryStats_.sceneryVertices += trees_.draw(tv, &vegStats_.trees);
        Uniform1i(uniformLocation(progTree, "uTreeFadeN"), 0);  // (the see-through trees' leaves only)
        geometryStats_.sceneryVertices += bushes_.draw(tv, &vegStats_.bushes);
    }
    passMark(FP_PROPS);
    UseProgram(progLit);
    drawBatches(sceneryVao, sceneryBatches, vp, false);
    passMark(FP_VEG);
    if (!noVeg) drawBushes(vp, view.eye, false, vegStats_.logs);
    geometryStats_.occlusionTests = occl_.asked, geometryStats_.occlusionHidden = occl_.hidden;
    passMark(FP_PROPS);
    props.draw(vp, [&](GLuint prog) {
        setLighting(prog);
        Uniform1i(uniformLocation(prog, "uMap"), mapId);
    });
    passMark(FP_VEG);
    UseProgram(progLit);
    if (settings.grass > 0) {
        // The grass: rings of tufts, finer near the camera (one grid each), as many as the setting allows.
        // (blades: 0 full, curved in five triangles with any seed head or flower; 1 plain, three (heads and flowers stay full);
        // 2 one triangle, base to tip. Further rings: fewer, wider and plainer blades on a coarser grid.)
        struct GrassRing { float cell; int side, blades, shape; float in0, in1, out0, out1, height, width; bool heads; };
        static const GrassRing kRings[3][3] = {
            {{0.50f, 56, 5, 1, -2, -1, 7, 12, 1.0f, 1.4f, false}, {1.00f, 72, 3, 2, 7, 12, 24, 34, 1.0f, 2.9f, false}, {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, false}},
            {{0.36f, 80, 6, 0, -2, -1, 8.5f, 13.5f, 1.0f, 1.0f, true}, {0.62f, 108, 6, 0, 8.5f, 13.5f, 22, 32, 1.05f, 1.9f, true}, {1.20f, 106, 3, 2, 22, 32, 46, 62, 1.0f, 4.2f, false}},
            {{0.30f, 96, 7, 0, -2, -1, 9, 14, 1.0f, 1.0f, true}, {0.52f, 132, 6, 0, 9, 14, 24, 34, 1.05f, 1.8f, true}, {0.95f, 152, 4, 2, 24, 34, 54, 72, 1.0f, 3.6f, false}},
        };
        setLighting(progGrass);
        bindTerrainTex(progGrass);
        Uniform1i(uniformLocation(progGrass, "uMap"), mapId);
        if (mapId == MAP_MOUNTAIN) Uniform2f(uniformLocation(progGrass, "uRockSnow"), 165.0f, 205.0f);
        else if (mapId == MAP_FRONTIER) Uniform2f(uniformLocation(progGrass, "uRockSnow"), 125.0f, 400.0f);
        else if (mapId == MAP_BACKCOUNTRY) Uniform2f(uniformLocation(progGrass, "uRockSnow"), 105.0f, 400.0f);
        else Uniform2f(uniformLocation(progGrass, "uRockSnow"), 60.0f, 240.0f);
        // Vehicles near the camera, each as a capsule along its wheels, for the grass to bend away from.
        float push[32] = {};
        int pushN = 0;
        {
            struct Near { double d; float v[8]; };
            std::vector<Near> near;
            for (const Car& car : sim.cars) {
                if (car.wheels.empty()) continue;
                const Frame fr = car.frame();
                const double d = std::hypot(fr.com.x - view.eye.x, fr.com.z - view.eye.z);
                if (d > 70) continue;
                double lo = 1e9, hi = -1e9, side = 0;
                for (const Wheel& wh : car.wheels) {
                    const V3 c = (car.nodes[wh.inner].p + car.nodes[wh.outer].p) * 0.5 - fr.com;
                    lo = std::min(lo, dot(c, fr.fwd)), hi = std::max(hi, dot(c, fr.fwd));
                    side = std::max(side, std::fabs(dot(c, fr.left)));
                }
                if (lo > hi) continue;
                const V3 a = fr.com + fr.fwd * (lo - 0.8), b = fr.com + fr.fwd * (hi + 0.8);
                near.push_back({d, {(float)a.x, (float)a.z, (float)side + 0.45f, 0, (float)b.x, (float)b.z, 0, 0}});
            }
            std::sort(near.begin(), near.end(), [](const Near& x, const Near& y) { return x.d < y.d; });
            for (size_t i = 0; i < near.size() && i < 4; i++, pushN++) std::copy(near[i].v, near[i].v + 8, push + 8 * i);
        }
        Uniform1i(uniformLocation(progGrass, "uPushN"), pushN);
        if (pushN) Uniform4fv(uniformLocation(progGrass, "uPush"), pushN * 2, push);
        BindVertexArray(grassVao);
        static const bool timed = std::getenv("CZ_BENCH") != nullptr;
        if (timed) {
            if (!grassQuery_[0]) GenQueries(4, grassQuery_);
            const int f = grassQueryFrame_ = (grassQueryFrame_ + 1) % 4, old = (f + 1) % 4;  // (the one begun three frames back is done by now)
            if (grassQueryUsed_[old]) {
                GLuint64 ns = 0;
                GetQueryObjectui64v(grassQuery_[old], GL_QUERY_RESULT, &ns);
                grassGpuMs_ = ns / 1e6;
                grassQueryUsed_[old] = false;
            }
            BeginQuery(GL_TIME_ELAPSED, grassQuery_[f]);
            grassQueryUsed_[f] = true;
        }
        for (const GrassRing& r : kRings[std::min(settings.grass, 3) - 1]) {
            if (!r.side) continue;  // (the setting's unused ring)
            Uniform1i(uniformLocation(progGrass, "uSide"), r.side);
            Uniform1i(uniformLocation(progGrass, "uBlades"), r.blades);
            Uniform1f(uniformLocation(progGrass, "uCell"), r.cell);
            Uniform4f(uniformLocation(progGrass, "uRing"), r.in0, r.in1, r.out0, r.out1);
            Uniform4f(uniformLocation(progGrass, "uLook"), r.height, r.width, r.shape ? 1.0f : 0.0f, r.heads ? 1.0f : 0.0f);
            // The ring's grid in tiles: only those in view and in the ring's reach are drawn (a row's run of them at once).
            const int T = 8, ts = (r.side + T - 1) / T;
            const double c0x = (std::floor(view.eye.x / r.cell) - r.side / 2) * r.cell, c0z = (std::floor(view.eye.z / r.cell) - r.side / 2) * r.cell;
            const GLint tileAt = uniformLocation(progGrass, "uTile");
            for (int tz = 0; tz < T; tz++) {
                int runX = -1;
                for (int tx = 0; tx <= T; tx++) {
                    bool in = false;
                    if (tx < T && tx * ts < r.side && tz * ts < r.side) {
                        // (a cell's margin round it: the shader finds the camera's cell itself; tufts sit anywhere in theirs)
                        const double x0 = c0x + (tx * ts - 1) * r.cell, x1 = c0x + (std::min(r.side, tx * ts + ts) + 1) * r.cell;
                        const double z0 = c0z + (tz * ts - 1) * r.cell, z1 = c0z + (std::min(r.side, tz * ts + ts) + 1) * r.cell;
                        const double nx = std::max({x0 - view.eye.x, 0.0, view.eye.x - x1}), nz = std::max({z0 - view.eye.z, 0.0, view.eye.z - z1});
                        const double fx = std::max(std::fabs(x0 - view.eye.x), std::fabs(x1 - view.eye.x)), fz = std::max(std::fabs(z0 - view.eye.z), std::fabs(z1 - view.eye.z));
                        if (std::hypot(nx, nz) < r.out1 && std::hypot(fx, fz) > r.in0) {
                            double lo = 1e9, hi = -1e9;
                            for (int k = 0; k < 9; k++) {
                                const double y = sim.world.height(x0 + (x1 - x0) * (k % 3) * 0.5, z0 + (z1 - z0) * (k / 3) * 0.5);
                                lo = std::min(lo, y), hi = std::max(hi, y);
                            }
                            RenderBounds b;
                            b.add(V3(x0, lo - 3.0, z0)), b.add(V3(x1, hi + 3.0, z1));  // (a blade stands under 2 m; the ground between the samples)
                            in = b.visible(vp) && !occl_.occluded(b);
                        }
                    }
                    if (in && runX < 0) runX = tx;
                    if (!in && runX >= 0) {
                        const int ix = runX * ts, w = std::min(r.side, tx * ts) - ix, iz = tz * ts, h = std::min(r.side - iz, ts);
                        Uniform3f(tileAt, (float)ix, (float)iz, (float)w);
                        // (indices: plain blades, 3 triangles of 9 indices from the first; full ones, 5 of 15, after them; then single ones, 1 of 3)
                        static const int kTris[3] = {5, 3, 1};
                        static const uintptr_t kAt[3] = {8 * 9 * 2, 0, (8 * 9 + 8 * 15) * 2};
                        DrawElementsInstanced(GL_TRIANGLES, r.blades * kTris[r.shape] * 3, GL_UNSIGNED_SHORT, (const void*)kAt[r.shape], w * h);
                        vegStats_.grass.add(1, (long long)r.blades * kTris[r.shape] * w * h, w * h);
                        runX = -1;
                    }
                }
            }
        }
        if (timed) EndQuery(GL_TIME_ELAPSED);
        setLighting(progLit);
    }
    passMark(FP_OPAQUE);
    BindVertexArray(dynVao);
    carKind_ = 3, carDrawArrays(0, (GLsizei)partVerts.size());
    drawCarParts(sim, progLit);
    drawWheels(sim, progLit);
    if (partRows) {  // the cars' mechanical parts (CarModels)
        setLighting(progParts);
        bindTerrainTex(progParts);
        Uniform1i(uniformLocation(progParts, "uMap"), mapId);
        drawCarModels(progParts);
    }

    setLighting(progCar);
    drawCarBodies(sim, progCar, false);

    // The ground last of the solid things, nearest chunks first: what's in front of it (cars, rocks, trees, grass, nearer
    // hills) has its depth down already, so the ground's costly shader only runs where the ground shows.
    passMark(FP_TERRAIN);
    // The ground: photo-scanned materials where they're loaded, else the procedural look.
    int gq = std::min(std::max(settings.ground, 0), 3);
    ensureGround(gq);
    if (groundFailed || !groundTex.ok()) gq = 0;
    progTerrain = gq ? terrainProgram(groundTier(gq)) : 0;
    if (!progTerrain) gq = 0;
    if (gq) {
        setLighting(progTerrain);
        uniformM4(progTerrain, "uModel", m4identity());
        bindTerrainTex(progTerrain);
        Uniform1i(uniformLocation(progTerrain, "uMap"), mapId);
        float rs[2];
        if (mapId == MAP_MOUNTAIN) rs[0] = 165.0f, rs[1] = 205.0f;
        else if (mapId == MAP_FRONTIER) rs[0] = 125.0f, rs[1] = 400.0f;
        else if (mapId == MAP_BACKCOUNTRY) rs[0] = 105.0f, rs[1] = 400.0f;
        else rs[0] = 60.0f, rs[1] = 240.0f;
        Uniform2f(uniformLocation(progTerrain, "uRockSnow"), rs[0], rs[1]);
        ActiveTexture(GL_TEXTURE0 + 3);
        BindTexture(GL_TEXTURE_2D_ARRAY, groundTex.albedo);
        ActiveTexture(GL_TEXTURE0 + 4);
        BindTexture(GL_TEXTURE_2D_ARRAY, groundTex.normal);
        ActiveTexture(GL_TEXTURE0);
        Uniform1i(uniformLocation(progTerrain, "uGroundA"), 3);
        Uniform1i(uniformLocation(progTerrain, "uGroundB"), 4);
        if (groundTier(gq) <= 1) {  // (the cheap tiers read their noise from a texture)
            ActiveTexture(GL_TEXTURE0 + 5);
            BindTexture(GL_TEXTURE_2D, noiseTex);
            ActiveTexture(GL_TEXTURE0);
            Uniform1i(uniformLocation(progTerrain, "uNoiseTex"), 5);
        }
    } else {
        UseProgram(progLit);
        uniformM4(progLit, "uModel", m4identity());  // (the wheels moved it)
    }
    drawTerrain(vp, view);
    if (effects) {
        // The ground shoved up beside tire tracks and round sunk tires, painted like the
        // terrain under it.
        static GLuint rutVao = 0, rutVbo = 0;
        static std::vector<Vtx> rutVerts;
        if (!rutVao) {
            GenVertexArrays(1, &rutVao);
            GenBuffers(1, &rutVbo);
            setupVtxVao(rutVao, rutVbo);
        }
        rutVerts.clear();
        effects->buildRutWalls(sim, view.eye, rutVerts);
        if (!rutVerts.empty()) {
            BindBuffer(GL_ARRAY_BUFFER, rutVbo);
            BufferData(GL_ARRAY_BUFFER, rutVerts.size() * sizeof(Vtx), rutVerts.data(), GL_STREAM_DRAW);
            BindVertexArray(rutVao);
            DrawArrays(GL_TRIANGLES, 0, (GLsizei)rutVerts.size());
        }
    }

    passMark(FP_FX);
    if (effects) {
        fxVerts.clear();
        effects->buildSkids(view.eye, fxVerts);
        size_t skidCount = fxVerts.size();
        effects->buildParticles(view.eye, fxVerts);
        BindBuffer(GL_ARRAY_BUFFER, fxVbo);
        BufferData(GL_ARRAY_BUFFER, fxVerts.size() * sizeof(Vtx), fxVerts.data(), GL_STREAM_DRAW);
        setCommon(progFx);
        uniformM4(progFx, "uVP", vp);
        BindVertexArray(fxVao);
        Enable(GL_BLEND);
        BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        DepthMask(0);
        Enable(GL_POLYGON_OFFSET_FILL);
        PolygonOffset(-1.0f, -4.0f);
        BlendFunc(0x0306, 0x0300);  // GL_DST_COLOR, GL_SRC_COLOR: tracks multiply the ground (x2: they can lighten it too)
        DrawArrays(GL_TRIANGLES, 0, (GLsizei)skidCount);
        BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Disable(GL_POLYGON_OFFSET_FILL);
        DrawArrays(GL_TRIANGLES, (GLint)skidCount, (GLsizei)(fxVerts.size() - skidCount));
        DepthMask(1);
        Disable(GL_BLEND);
    }

    // Lakes, blended over their beds.
    if (waterVertexCount) {
        setLighting(progWater);
        uniformM4(progWater, "uModel", m4identity());
        bindTerrainTex(progWater);
        Enable(GL_BLEND);
        BlendFunc(GL_ONE, 0x88F9);  // GL_SRC1_COLOR: dual-source, the surface's light plus the bed dimmed per channel
        DepthMask(0);
        BindVertexArray(waterVao);
        DrawArrays(GL_TRIANGLES, 0, waterVertexCount);
        DepthMask(1);
        Disable(GL_BLEND);
    }

    // Window glass last, blended over the cabin interior.
    if (debug != DEBUG_BEAMS_ONLY) {
        setLighting(progCar);
        Enable(GL_BLEND);
        BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        DepthMask(0);
        drawCarBodies(sim, progCar, true);
        DepthMask(1);
        Disable(GL_BLEND);
    }

    // Hurt parts glowing through what hides them: only where something's in front (depth greater), added on.
    if (xrayVertexCount > 0 && debug != DEBUG_PARTS) {
        UseProgram(progXray);
        uniformM4(progXray, "uVP", vp);
        Uniform3f(uniformLocation(progXray, "uCam"), (float)view.eye.x, (float)view.eye.y, (float)view.eye.z);
        Uniform1f(uniformLocation(progXray, "uTime"), view.time);
        Enable(GL_BLEND);
        BlendFunc(GL_ONE, GL_ONE);
        DepthMask(0);
        DepthFunc(GL_GREATER);
        BindVertexArray(xrayVao);
        DrawArrays(GL_TRIANGLES, 0, xrayVertexCount);
        DepthFunc(GL_LEQUAL);
        DepthMask(1);
        Disable(GL_BLEND);
    }

    if (debug != DEBUG_OFF && debug != DEBUG_PARTS) {
        // X-ray: beams colored by what they are (body shell dim, frame orange, engine
        // yellow, suspension cyan, springs magenta, steering blue, drive shafts white).
        // Beams-only: colored by strain (blue = compressed, green = relaxed, red =
        // stretched; a yellow tint marks beams that have permanently deformed).
        lineVerts.clear();
        for (const Car& car : sim.cars) {
            for (const Beam& b : car.beams) {
                if (b.broken) continue;
                const V3& p = car.nodes[b.a].p;
                const V3& q = car.nodes[b.b].p;
                float r, g, bl;
                if (debug == DEBUG_XRAY) {
                    switch (b.type) {
                    case BEAM_FRAME: r = 1.0f, g = 0.55f, bl = 0.1f; break;
                    case BEAM_ENGINE: r = 1.0f, g = 0.9f, bl = 0.2f; break;
                    case BEAM_MOUNT: r = 0.9f, g = 0.3f, bl = 0.2f; break;
                    case BEAM_ARM: case BEAM_KNUCKLE: r = 0.2f, g = 0.9f, bl = 1.0f; break;
                    case BEAM_SPRING: case BEAM_BUMPSTOP: r = 1.0f, g = 0.3f, bl = 1.0f; break;
                    case BEAM_TIEROD: r = 0.3f, g = 0.5f, bl = 1.0f; break;
                    case BEAM_SHAFT: r = 1.0f, g = 1.0f, bl = 1.0f; break;
                    case BEAM_PANEL: r = 0.75f, g = 0.8f, bl = 0.9f; break;
                    default: r = 0.15f, g = 0.35f, bl = 0.18f; break;  // body shell
                    }
                    if (std::fabs(b.L0 - b.Lorig) > 0.02 * b.Lorig && b.type != BEAM_TIEROD) r = 1.0f, g = 0.15f, bl = 0.1f;
                } else {
                    double s = clampd((len(q - p) - b.L0) / b.L0 * 40.0, -1, 1);
                    r = (float)std::max(0.0, s), bl = (float)std::max(0.0, -s), g = 1.0f - r - bl;
                    if (std::fabs(b.L0 - b.Lorig) > 0.02 * b.Lorig) {
                        r = std::min(1.0f, r + 0.8f);
                        g = std::min(1.0f, g + 0.6f);
                    }
                    if (b.type == BEAM_SPRING) r = 1.0f, g = 0.5f, bl = 1.0f;
                }
                lineVerts.push_back(vtx(p, V3(), r, g, bl, 1));
                lineVerts.push_back(vtx(q, V3(), r, g, bl, 1));
            }
        }
        size_t lineCount = lineVerts.size();
        for (const Car& car : sim.cars)
            for (const Node& n : car.nodes) lineVerts.push_back(vtx(n.p, V3(), 1.0f, 1.0f, 1.0f, 1));
        BindBuffer(GL_ARRAY_BUFFER, lineVbo);
        BufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(Vtx), lineVerts.data(), GL_STREAM_DRAW);
        UseProgram(progLine);
        uniformM4(progLine, "uVP", vp);
        Enable(GL_PROGRAM_POINT_SIZE);
        Disable(GL_DEPTH_TEST);  // x-ray
        BindVertexArray(lineVao);
        DrawArrays(GL_LINES, 0, (GLsizei)lineCount);
        DrawArrays(GL_POINTS, (GLint)lineCount, (GLsizei)(lineVerts.size() - lineCount));
        Enable(GL_DEPTH_TEST);
    }
    passMark(FP_POST);
    postProcess(view, proj);
    passMark(FP_PROPS);
    props.endFrame();
}

// The ground shader's tier for a ground setting: POTATO has a tier of its own (the cheapest), LOW the next (see groundshader.cpp).
int Renderer::groundTier(int gq) const { return gq == 1 && presetOf(settings) == Q_POTATO ? 0 : gq; }

GLuint Renderer::terrainProgram(int tier) {
    if (progTerrainTier[tier] || groundFailed) return progTerrainTier[tier];
    std::string xd = "#define GQ " + std::to_string(tier) + "\n";
    if (tier <= 1) xd += "#define PROC_LITE\n";
    if (const char* e = std::getenv("CZ_GDEF")) {  // (a tryout switch: comma-separated defines)
        std::string t = e;
        for (size_t i = 0; i < t.size();) {
            size_t j = t.find(',', i);
            if (j == std::string::npos) j = t.size();
            xd += "#define " + t.substr(i, j - i) + "\n";
            i = j + 1;
        }
    }
    if (tier <= 1 && !noiseTex) {  // the shader's own lattice hash drawn into a 256 x 256 texture (wrapped), on this GPU so it's the very numbers HIGH computes: vnoiseL samples between the texels
        static const char* kBakeFS = R"(#version 330 core
in vec2 vUv; out vec4 oCol;
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main(){
  vec2 i = floor(vUv * 256.0);
  i -= 256.0 * step(128.0, i);
  oCol = vec4(hash(i), 0.0, 0.0, 1.0);
})";
        std::string berr;
        const GLuint bake = linkProgram(kPostVS, kBakeFS, berr);
        GLint fbo0 = 0, vp[4] = {0, 0, 0, 0};
        GetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &fbo0);
        GetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
        GenTextures(1, &noiseTex);
        BindTexture(GL_TEXTURE_2D, noiseTex);
        TexImage2D(GL_TEXTURE_2D, 0, 0x822A /* GL_R16 */, 256, 256, 0, GL_RED, GL_UNSIGNED_SHORT, nullptr);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (bake) {
            GLuint f = 0;
            GenFramebuffers(1, &f);
            BindFramebuffer(GL_FRAMEBUFFER, f);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, noiseTex, 0);
            Viewport(0, 0, 256, 256);
            UseProgram(bake);
            BindVertexArray(postVao);
            DrawArrays(GL_TRIANGLES, 0, 3);
            BindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo0);
            DeleteFramebuffers(1, &f);
            DeleteProgram(bake);
            Viewport(vp[0], vp[1], vp[2], vp[3]);
        } else std::fprintf(stderr, "ground noise bake: %s\n", berr.c_str());
    }
    std::string gerr;
    progTerrainTier[tier] = linkProgram(kLitVS, "#version 330 core\n" + xd + kCommonFS + kLitIns + kTerrainProcFS + kGroundShaderFS, gerr);
    if (!progTerrainTier[tier]) {
        std::fprintf(stderr, "ground shader: %s\n", gerr.c_str());
        groundFailed = true;
    }
    return progTerrainTier[tier];
}

void Renderer::ensureGround(int gq) {
    if (!gq || groundFailed) return;
    terrainProgram(groundTier(gq));
    {
        static int lastAniso = -1;  // (anisotropy follows the ground quality: 4x, 8x, 16x)
        if (gq != lastAniso) texcomp::setQuality(gq), lastAniso = gq;
    }
    const int want = gq >= 2 ? 1024 : 512;
    if (groundTex.ok() && groundSize == want) return;
    std::string gerr;
    if (loadGroundTextures(groundTex, want, gerr)) groundSize = want;
    else {
        std::fprintf(stderr, "ground textures: %s (staying procedural)\n", gerr.c_str());
        groundFailed = true;
    }
}

// Behind the loading screen: what the first frames would otherwise load mid-game (the ground's textures, the shadow
// maps, the scanned props for this quality).
void Renderer::preload(const World& world) {
    ensureGround(std::min(std::max(settings.ground, 0), 3));
    static const int kShadowRes[4] = {0, 1024, 2048, 4096};
    if (settings.shadows > 0) ensureShadows(kShadowRes[std::min(settings.shadows, 3)]);
    if (settings.props != propsApplied) updateObjects(world);
}

void Renderer::passMark(FramePass p) {
    if (syncTiming) Finish();
    frameProfile().enter(p);
    if (!gpuTiming) return;
    GpuMarks& m = gpuMarks_[gpuMarkFrame_];
    if (m.n >= GpuMarks::kMax) return;
    if (!m.q[0]) GenQueries(GpuMarks::kMax, m.q);
    QueryCounter(m.q[m.n], GL_TIMESTAMP);
    m.pass[m.n++] = (uint8_t)p;
}

// The slot about to be reused was filled four frames ago: its timestamps are in by now (if not, that frame goes unread
// rather than stalling on the GPU). Each stage's GPU time is from its mark to the next.
void Renderer::gpuFrameBegin() {
    gpuMarkFrame_ = (gpuMarkFrame_ + 1) % 4;
    GpuMarks& m = gpuMarks_[gpuMarkFrame_];
    if (m.n >= 2) {
        GLint ready = 0;
        GetQueryObjectiv(m.q[m.n - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            double ms[FP_COUNT];
            for (int i = 0; i < FP_COUNT; i++) ms[i] = framePassDraws((FramePass)i) ? 0 : -1;
            GLuint64 prev = 0;
            for (int k = 0; k < m.n; k++) {
                GLuint64 t = 0;
                GetQueryObjectui64v(m.q[k], GL_QUERY_RESULT, &t);
                if (k && t >= prev && framePassDraws((FramePass)m.pass[k - 1])) ms[m.pass[k - 1]] += (t - prev) / 1e6;
                prev = t;
            }
            frameProfile().setGpu(ms);
        }
    }
    m.n = 0;
}

void Renderer::text(float x, float y, float s, const std::string& str, float r, float g, float b, float a) {
    a *= hudAlpha;
    if (a <= 0) return;
    const float cw = 6.0f / 96.0f, ch = 8.0f / 48.0f;
    for (char raw : str) {
        int c = (unsigned char)raw;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 127) c = '?';
        if (c != ' ') {
            float u = ((c - 32) % 16) * cw, v = ((c - 32) / 16) * ch;
            float x1 = x + 6 * s, y1 = y + 8 * s;
            float q[6][4] = {{x, y, u, v}, {x1, y, u + cw, v}, {x1, y1, u + cw, v + ch},
                             {x, y, u, v}, {x1, y1, u + cw, v + ch}, {x, y1, u, v + ch}};
            for (auto& p : q) hudVerts.insert(hudVerts.end(), {p[0], p[1], p[2], p[3], r, g, b, a});
        }
        x += 6 * s;
    }
}

void Renderer::rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    a *= hudAlpha;
    if (a <= 0) return;
    // Sample the middle of the solid glyph (code 127).
    float u = ((127 - 32) % 16 + 0.5f) * 6.0f / 96.0f, v = ((127 - 32) / 16 + 0.5f) * 8.0f / 48.0f;
    float q[6][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y}, {x + w, y + h}, {x, y + h}};
    for (auto& p : q) hudVerts.insert(hudVerts.end(), {p[0], p[1], u, v, r, g, b, a});
}

void Renderer::tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
    a *= hudAlpha;
    if (a <= 0) return;
    float u = ((127 - 32) % 16 + 0.5f) * 6.0f / 96.0f, v = ((127 - 32) / 16 + 0.5f) * 8.0f / 48.0f;
    hudVerts.insert(hudVerts.end(), {x0, y0, u, v, r, g, b, a, x1, y1, u, v, r, g, b, a, x2, y2, u, v, r, g, b, a});
}

void Renderer::arc(float cx, float cy, float r0, float r1, float a0, float a1, float r, float g, float b, float a,
                   int segs) {
    for (int s = 0; s < segs; s++) {
        float t0 = a0 + (a1 - a0) * s / segs, t1 = a0 + (a1 - a0) * (s + 1) / segs;
        float c0 = std::cos(t0), s0 = std::sin(t0), c1 = std::cos(t1), s1 = std::sin(t1);
        tri(cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, r, g, b, a);
        tri(cx + c0 * r0, cy + s0 * r0, cx + c1 * r1, cy + s1 * r1, cx + c1 * r0, cy + s1 * r0, r, g, b, a);
    }
}

void Renderer::icon(const std::string& key, const mk::Mesh& mesh, float x, float y, float w, float h, float a, float shade) {
    if (!progIcon || mesh.v.empty()) return;
    iconDraws.push_back({key, &mesh, hudVerts.size() / 8});
    // (Textures run bottom-up: the picture's top is v = 1.)
    const float q[6][4] = {{x, y, 0, 1}, {x + w, y, 1, 1}, {x + w, y + h, 1, 0}, {x, y, 0, 1}, {x + w, y + h, 1, 0}, {x, y + h, 0, 0}};
    for (const auto& p : q) hudVerts.insert(hudVerts.end(), {p[0], p[1], p[2], p[3], shade, shade, shade, a});
}

// Draws a model once into a texture of its own: 4x multisampled at 256 x 256, resolved, mipmapped.
GLuint Renderer::iconTexture(const std::string& key, const mk::Mesh& mesh) {
    const uint64_t picks = mk::picksVersion();
    IconTex& it = iconCache[key];
    if (it.tex && it.picks == picks) return it.tex;
    if (!progIconMesh || mesh.v.empty()) return 0;
    constexpr int N = 256;
    // The view: from the front and a little to one side, above; the long way across the frame whatever the model's shape.
    V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
    for (const mk::MVert& v : mesh.v) {
        lo = V3(std::min(lo.x, (double)v.p[0]), std::min(lo.y, (double)v.p[1]), std::min(lo.z, (double)v.p[2]));
        hi = V3(std::max(hi.x, (double)v.p[0]), std::max(hi.y, (double)v.p[1]), std::max(hi.z, (double)v.p[2]));
    }
    const V3 c = (lo + hi) * 0.5, ext = hi - lo;
    const double flat = ext.y / std::max(1e-6, std::max(ext.x, ext.z));  // (a coil or a tire lies flat: look down on it more)
    const double az = 0.62, el = flat < 0.35 ? 0.78 : flat > 1.6 ? 0.30 : 0.52;
    const V3 eye = norm(V3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)));
    const V3 F = eye * -1.0, R = norm(cross(F, V3(0, 1, 0))), U = cross(R, F);
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (const mk::MVert& v : mesh.v) {
        const V3 q = V3(v.p[0], v.p[1], v.p[2]) - c;
        const double px = dot(q, R), py = dot(q, U), pz = dot(q, F);
        x0 = std::min(x0, px), x1 = std::max(x1, px), y0 = std::min(y0, py), y1 = std::max(y1, py), z0 = std::min(z0, pz), z1 = std::max(z1, pz);
    }
    const double half = std::max(x1 - x0, y1 - y0) * 0.5 * 1.12 + 1e-6, hz = (z1 - z0) * 0.5 * 1.1 + 1e-6;
    std::vector<Vtx> verts;
    verts.reserve(mesh.v.size());
    for (const mk::MVert& v : mesh.v) verts.push_back(Vtx{{v.p[0], v.p[1], v.p[2]}, {v.n[0], v.n[1], v.n[2]}, {v.c[0], v.c[1], v.c[2], (float)v.mat}});
    GLint prevFbo = 0, prevVp[4] = {};
    GetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &prevFbo);
    GetIntegerv(0x0BA2 /*GL_VIEWPORT*/, prevVp);
    GLuint msFb = 0, msRb[2] = {}, outFb = 0;
    if (!it.tex) GenTextures(1, &it.tex);
    BindTexture(GL_TEXTURE_2D, it.tex);
    TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GenFramebuffers(1, &outFb);
    BindFramebuffer(GL_FRAMEBUFFER, outFb);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it.tex, 0);
    GLint maxSamples = 0;
    GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const int samples = std::min(4, (int)maxSamples);
    bool multi = samples > 1;
    if (multi) {
        GenRenderbuffers(2, msRb);
        BindRenderbuffer(GL_RENDERBUFFER, msRb[0]);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, N, N);
        BindRenderbuffer(GL_RENDERBUFFER, msRb[1]);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, N, N);
        GenFramebuffers(1, &msFb);
        BindFramebuffer(GL_FRAMEBUFFER, msFb);
        FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msRb[0]);
        FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msRb[1]);
        multi = CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    if (!multi) {  // (no multisampling here: the texture itself, with a depth buffer of its own)
        if (msFb) DeleteFramebuffers(1, &msFb), msFb = 0;
        if (msRb[0]) DeleteRenderbuffers(2, msRb), msRb[0] = msRb[1] = 0;
        GenRenderbuffers(1, &msRb[1]);
        BindRenderbuffer(GL_RENDERBUFFER, msRb[1]);
        RenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH_COMPONENT24, N, N);
        BindFramebuffer(GL_FRAMEBUFFER, outFb);
        FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msRb[1]);
    }
    BindFramebuffer(GL_FRAMEBUFFER, multi ? msFb : outFb);
    Viewport(0, 0, N, N);
    Disable(GL_BLEND);
    Disable(GL_CULL_FACE);
    Enable(GL_DEPTH_TEST);
    DepthFunc(GL_LESS);
    DepthMask(1);
    ClearColor(0, 0, 0, 0);
    Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!iconVao) {
        GenVertexArrays(1, &iconVao);
        GenBuffers(1, &iconVbo);
        setupVtxVao(iconVao, iconVbo);
    }
    BindVertexArray(iconVao);
    BindBuffer(GL_ARRAY_BUFFER, iconVbo);
    BufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vtx), verts.data(), GL_STREAM_DRAW);
    UseProgram(progIconMesh);
    Uniform3f(uniformLocation(progIconMesh, "uC"), (float)c.x, (float)c.y, (float)c.z);
    Uniform3f(uniformLocation(progIconMesh, "uR"), (float)R.x, (float)R.y, (float)R.z);
    Uniform3f(uniformLocation(progIconMesh, "uU"), (float)U.x, (float)U.y, (float)U.z);
    Uniform3f(uniformLocation(progIconMesh, "uF"), (float)F.x, (float)F.y, (float)F.z);
    Uniform3f(uniformLocation(progIconMesh, "uOff"), (float)((x0 + x1) * 0.5), (float)((y0 + y1) * 0.5), (float)((z0 + z1) * 0.5));
    Uniform2f(uniformLocation(progIconMesh, "uScale"), (float)(1.0 / half), (float)(1.0 / hz));
    DrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    if (multi) {
        BindFramebuffer(GL_READ_FRAMEBUFFER, msFb);
        BindFramebuffer(GL_DRAW_FRAMEBUFFER, outFb);
        BlitFramebuffer(0, 0, N, N, 0, 0, N, N, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    BindTexture(GL_TEXTURE_2D, it.tex);
    GenerateMipmap(GL_TEXTURE_2D);
    noteRT(("icon " + key).c_str(), "RGBA8", N, N, 4, texenc::mipCount(N, N));
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    Viewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    DepthFunc(GL_LEQUAL);
    if (msFb) DeleteFramebuffers(1, &msFb);
    DeleteFramebuffers(1, &outFb);
    GLuint rbs[2] = {msRb[0], msRb[1]};
    DeleteRenderbuffers(2, rbs);
    it.picks = picks;
    return it.tex;
}

void Renderer::flushHud(int width, int height) {
    if (hudVerts.empty()) {
        iconDraws.clear();
        return;
    }
    std::vector<GLuint> tex;  // (the pictures are drawn first: they take a render target of their own)
    for (const IconDraw& d : iconDraws) tex.push_back(iconTexture(d.key, *d.mesh));
    Disable(GL_DEPTH_TEST);
    Enable(GL_BLEND);
    BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ActiveTexture(GL_TEXTURE0);
    BindVertexArray(hudVao);
    BindBuffer(GL_ARRAY_BUFFER, hudVbo);
    BufferData(GL_ARRAY_BUFFER, hudVerts.size() * sizeof(float), hudVerts.data(), GL_STREAM_DRAW);
    const size_t total = hudVerts.size() / 8;
    size_t from = 0;
    auto run = [&](size_t upTo) {
        if (upTo <= from) return;
        UseProgram(progHud);
        Uniform2f(uniformLocation(progHud, "uScreen"), (float)width, (float)height);
        BindTexture(GL_TEXTURE_2D, fontTex);
        Uniform1i(uniformLocation(progHud, "uFont"), 0);
        DrawArrays(GL_TRIANGLES, (GLint)from, (GLsizei)(upTo - from));
        from = upTo;
    };
    for (size_t i = 0; i < iconDraws.size(); i++) {
        run(iconDraws[i].vert);
        if (!tex[i]) {
            from = iconDraws[i].vert + 6;
            continue;
        }
        UseProgram(progIcon);
        Uniform2f(uniformLocation(progIcon, "uScreen"), (float)width, (float)height);
        BindTexture(GL_TEXTURE_2D, tex[i]);
        Uniform1i(uniformLocation(progIcon, "uFont"), 0);
        BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DrawArrays(GL_TRIANGLES, (GLint)iconDraws[i].vert, 6);
        BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        from = iconDraws[i].vert + 6;
    }
    run(total);
    Disable(GL_BLEND);
    Enable(GL_DEPTH_TEST);
    hudVerts.clear();
    iconDraws.clear();
}

std::vector<unsigned char> Renderer::readPixels(int width, int height) {
    std::vector<unsigned char> px(width * height * 3);
    PixelStorei(GL_PACK_ALIGNMENT, 1);
    ReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    return px;
}

int Renderer::badScenePixels() {
    if (!sceneFbo || targetW <= 0 || targetH <= 0) return 0;
    std::vector<float> px((size_t)targetW * targetH * 4);
    BindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
    PixelStorei(GL_PACK_ALIGNMENT, 4);
    ReadPixels(0, 0, targetW, targetH, GL_RGBA, GL_FLOAT, px.data());
    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    int bad = 0;
    for (size_t i = 0; i < px.size(); i += 4)
        if (!std::isfinite(px[i]) || !std::isfinite(px[i + 1]) || !std::isfinite(px[i + 2]) || px[i] < 0 || px[i + 1] < 0 || px[i + 2] < 0) bad++;
    return bad;
}
