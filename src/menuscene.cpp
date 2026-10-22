#include "menuscene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <set>

#include "daylight.h"
#include "menucamp.h"
#include "platform.h"

namespace menuscene {

namespace {
// The camp is laid out around the fire in "camp" coordinates: x to the right of the camera, z away from it (north), in metres.
// The fire is at the origin of those. kRot turns the camp on the map (the wilds' ground slopes one way).
const double kFx = 80, kFz = 40;  // (the wilds' ground is level and bare here, a hundred metres from its spawn pad)

// The camp is turned on the map so the sunset lies behind it, a little right of the camera's line (kSunLocal radians, in camp terms).
const double kSunLocal = std::getenv("CZ_SUNLOCAL") ? std::atof(std::getenv("CZ_SUNLOCAL")) : 0.15;
double theta() {
    static const double t = [] {
        const Daylight d = computeDaylight(kHours);
        return std::atan2(d.sunDir.x, d.sunDir.z) - kSunLocal;
    }();
    return t;
}
V3 at(double cx, double cz) {
    const double c = std::cos(theta()), sn = std::sin(theta());
    return V3(kFx + cx * c - cz * sn, 0, kFz + cx * sn + cz * c);
}
// A world point as camp coordinates, and whether it lies in the wedge toward the sun (the gap in the trees).
void local(double x, double z, double& cx, double& cz) {
    const double c = std::cos(theta()), sn = std::sin(theta()), dx = x - kFx, dz = z - kFz;
    cx = dx * c + dz * sn, cz = -dx * sn + dz * c;
}
bool inSunGap(double cx, double cz) {
    const double d = std::hypot(cx, cz);
    if (d < 9) return false;
    const double a = std::atan2(cx, -cz) - kSunLocal;
    return std::fabs(a) < 0.26 + 0.6 / d * 8;
}

// What stands on the ground in the camp, as (camp x, z, radius): nothing grows through any of it (bushes are kept out, the ground under it is bare earth).
struct Keep { double x, z, r; };
const Keep kKeep[] = {{0, 0, 3.6},      {0.6, 4.6, 2.6},   {-1.5, -16.5, 5.8}, {-5.0, -13.0, 2.2}, {9.5, -13, 2.8},
                      {3.6, -2.4, 3.4}, {1.5, -8.0, 3.4},  {-2.6, -2.2, 3.4},  {-2.7, 2.7, 1.8},    {2.9, 1.9, 1.6}};
bool blocked(double cx, double cz, double pad) {
    for (const Keep& k : kKeep)
        if (std::hypot(cx - k.x, cz - k.z) < k.r + pad) return true;
    return false;
}

LevelObject model(const char* id, double cx, double cz, double yawDeg, double scale = 1, int variant = -1, int sub = 0) {
    LevelObject o;
    o.kind = OBJ_MODEL, o.name = id, o.p = at(cx, cz), o.size = V3(scale, scale, scale), o.yaw = yawDeg * M_PI / 180 - theta(), o.variant = variant, o.sub = sub;
    return o;
}
LevelObject tree(double cx, double cz, double h, int kind, float tint = 1) {
    LevelObject o;
    o.kind = OBJ_TREE, o.sub = kind, o.p = at(cx, cz), o.size = V3(0, h, 0), o.col[0] = tint;
    return o;
}
LevelObject rock(double cx, double cz, double a, double b, double c, uint32_t seed) {
    LevelObject o;
    o.kind = OBJ_ROCK, o.p = at(cx, cz), o.size = V3(a, b, c), o.top = 2 * b * 0.78, o.seed = seed, o.col[0] = 1.0f;
    return o;
}
LevelObject bush(double cx, double cz, double r, int kind = 0) {
    LevelObject o;
    o.kind = OBJ_BUSH, o.sub = kind, o.p = at(cx, cz), o.size = V3(r, 0, 0), o.col[0] = 1.0f;
    return o;
}
LevelObject logSeat(double cx0, double cz0, double cx1, double cz1) {
    LevelObject o;
    o.kind = OBJ_LOG, o.p = at(cx0, cz0), o.p2 = at(cx1, cz1), o.size = V3(0.22, 0, 0);
    o.p.y = o.p2.y = 0, o.rel = true, o.lift = 0, o.lift2 = 0;
    return o;
}
}  // namespace

Level level();  // (below)

Level generateDefault() {
    Level lv = level();
    const World w0 = levelWorld(lv);
    World w = w0;
    // The clearing: whatever the forest put in the camp's patch (and the way the camera looks at it) comes off.
    auto in = [&](double x, double z, double pad) {
        double cx, cz;
        local(x, z, cx, cz);
        return (cx > -21 - pad && cx < 21 + pad && cz > -21 - pad && cz < 27 + pad) || (cx > -34 - pad && cx < 34 + pad && cz > -34 - pad && cz <= -21 + pad) || inSunGap(cx, cz);
    };
    // (What comes off is written into the level as "removed" entries: the editor and the game apply them like any level's.)
    auto drop = [&](auto& vec, int kind, auto pos, auto gone) {
        for (auto it = vec.begin(); it != vec.end();) {
            if (gone(*it)) {
                const V3 p = pos(*it);
                lv.removed.push_back(V3(p.x, 1 + kind, p.z));
                it = vec.erase(it);
            } else ++it;
        }
    };
    drop(w.trees, 1, [](const Tree& t) { return t.base; }, [&](const Tree& t) { return in(t.base.x, t.base.z, t.radius); });
    drop(w.rocks, 0, [](const Rock& r) { return r.c; }, [&](const Rock& r) { return in(r.c.x, r.c.z, r.r); });
    drop(w.boxes, 3, [](const Box& b) { return b.c; }, [&](const Box& b) { return in(b.c.x, b.c.z, 0); });
    drop(w.logs, 2, [](const Log& l) { return (l.a + l.b) * 0.5; }, [&](const Log& l) { return in(l.a.x, l.a.z, l.r) || in(l.b.x, l.b.z, l.r); });
    drop(w.bushes, 6, [](const Bush& b) { return b.p; }, [&](const Bush& b) { return in(b.p.x, b.p.z, b.r); });
    drop(w.fixtures, 4, [](const Prop& p) { return p.a; }, [&](const Prop& p) { return in(p.a.x, p.a.z, 0); });
    // Bare earth under the fire, the cabin, the table, the tent and the trucks (grass tufts grow by the ground's surface): nothing grows through them.
    for (double x = kFx - 40; x <= kFx + 40; x += 1)
        for (double z = kFz - 40; z <= kFz + 40; z += 1) {
            double cx, cz;
            local(x, z, cx, cz);
            if (!blocked(cx, cz, 0.3)) continue;
            const int id = w.cellIndex(x, z);
            if (id >= 0 && w.cellSurface(id) != SURF_PAVED) w.setCellSurface(id, SURF_DIRT, 0);
        }
    // A valley river behind the camp (camp terms: from (-40, -24.5) to (40, -24)): a flattened bed dug into the ground, water in it; it catches the sunset.
    {
        const V3 a = at(-40, -24.5), b = at(40, -24);
        const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
        double g = 1e9;
        for (int k = 0; k <= 16; k++) g = std::min(g, w.height(a.x + dx * k / 16, a.z + dz * k / 16));
        g += 0.5;
        for (double x = std::min(a.x, b.x) - 12; x <= std::max(a.x, b.x) + 12; x += 1)
            for (double z = std::min(a.z, b.z) - 12; z <= std::max(a.z, b.z) + 12; z += 1) {
                const double u = ((x - a.x) * dx + (z - a.z) * dz) / (L * L);
                if (u < -0.02 || u > 1.02) continue;
                const double wob = 2.2 * std::sin(u * 9.0), d = std::fabs(((x - a.x) * dz - (z - a.z) * dx) / L - wob);
                const int id = w.cellIndex(x, z);
                if (id < 0 || d > 11) continue;
                const double orig = w.height(x, z), k = std::min(1.0, std::max(0.0, (11 - d) / 5.0));
                double h = orig + (g - orig) * k * k * (3 - 2 * k);
                if (d < 5.6) h = g - 0.6 * (1 - d * d / 31.4);
                w.setCellHeight(id, (float)h);
                if (d < 5.2) w.setCellWater(id, (float)(g - 0.2));
            }
    }
    // What the ground became (the river bed and its water, the bare earth) goes into the level as edited cells.
    std::set<int> seen;
    for (double x = kFx - 75; x <= kFx + 75; x += 1)
        for (double z = kFz - 75; z <= kFz + 75; z += 1) {
            const int id = w.cellIndex(x, z);
            if (id < 0 || !seen.insert(id).second) continue;
            if (std::fabs(w.cellHeight(id) - w0.cellHeight(id)) > 1e-4f) lv.heights[id] = w.cellHeight(id);
            const uint16_t sf = (uint16_t)(w.cellSurface(id) << 8 | w.cellPaint(id)), s0 = (uint16_t)(w0.cellSurface(id) << 8 | w0.cellPaint(id));
            if (sf != s0) lv.surfaces[id] = sf;
            const double wl = w.waterLevel(std::round(x), std::round(z));
            if (wl > -1e8 && std::fabs(wl - w0.waterLevel(std::round(x), std::round(z))) > 1e-4) lv.water[id] = (float)wl;
        }
    // The vehicles, the camera and the sky's colours.
    menucamp::Meta m;
    const struct { double cx, cz, yaw; int slot; } spot[3] = {{3.6, -2.4, 2.95, 1}, {1.5, -8.0, 4.0, 2}, {-2.6, -2.2, -0.55, 0}};
    for (int k = 0; k < 3; k++) {
        const V3 p = at(spot[k].cx, spot[k].cz);
        m.veh[k] = {p.x, p.z, (spot[k].yaw - theta()) * 180 / M_PI, spot[k].slot};
    }
    const V3 e = at(-1.8, 11.8), t = at(-3.9, -5.0);
    m.cam = {e.x, 2.6, e.z, t.x, 2.2, t.z, 1.0};
    m.look = kSunsetLook;
    menucamp::setMeta(lv, m);
    return lv;
}

Level level() {
    Level lv;
    lv.name = "MENU CAMP";
    lv.map = MAP_WILDS;
    lv.terrain = WILDS_FOREST;
    lv.timeOfDay = kHours;
    std::vector<LevelObject>& o = lv.objects;
    o.push_back(model("cabin", -1.5, -16.5, -80, 1, 0));
    o.push_back(model("woodpile", -5.0, -13.0, 90));
    o.push_back(model("campfire", 0, 0, 0));
    o.push_back(model("tent", 9.5, -13.0, -150));
    o.push_back(model("picnic_table", 0.6, 4.6, 20));
    o.push_back(logSeat(-3.8, 1.9, -1.7, 3.6));
    o.push_back(logSeat(2.2, 1.2, 3.6, 2.6));
    // The tree line: pines and broadleaf in a loose ring that opens on the camera's side.
    uint32_t h = 12345;
    auto rnd = [&]() {
        h = h * 1664525u + 1013904223u;
        return (h >> 8) / double(1 << 24);
    };
    for (int i = 0; i < 130; i++) {
        const double a = -0.35 * M_PI + (i / 129.0) * 1.7 * M_PI;  // sweeps round the back; the front (toward -z) stays open
        const double r = 17 + rnd() * 30;
        const double cx = std::sin(a) * r * 1.25, cz = -std::cos(a) * r * 0.8 - 4 + rnd() * 3;
        if (cz > -34 && std::fabs(cx) < 34) continue;  // (the river and its banks stay open)
        if (cz > 6 || inSunGap(cx, cz) || blocked(cx, cz, 2)) continue;
        o.push_back(tree(cx, cz, 9 + rnd() * 9, rnd() < 0.72 ? 0 : 1, 0.85f + 0.3f * (float)rnd()));
    }
    for (int i = 0; i < 12; i++) if (!inSunGap(-33 + i * 6.0, -42)) o.push_back(tree(-33 + i * 6.0 + rnd() * 3, -38 - rnd() * 10, 12 + rnd() * 8, 0, 0.8f + 0.2f * (float)rnd()));
    // The slopes behind and the forest on both sides: a dense pine wood out to the far ridge (the sun gap and the river's view stay open).
    for (int i = 0; i < 230; i++) {
        const double cx = -75 + rnd() * 150, cz = -36 - rnd() * 70;
        if (inSunGap(cx, cz)) continue;
        o.push_back(tree(cx, cz, 11 + rnd() * 10, rnd() < 0.85 ? 0 : 1, 0.78f + 0.3f * (float)rnd()));
    }
    for (int i = 0; i < 110; i++) {
        const double side = i % 2 ? 1 : -1, cx = side * (30 + rnd() * 45), cz = -34 + rnd() * 60;
        if (inSunGap(cx, cz) || (cx > 0 && cz > 8)) continue;
        o.push_back(tree(cx, cz, 9 + rnd() * 10, rnd() < 0.8 ? 0 : 1, 0.8f + 0.3f * (float)rnd()));
    }
    // Rocks round the fire and in the grass, bushes at the edges.
    o.push_back(rock(-1.4, 1.1, 0.42, 0.3, 0.36, 3));
    o.push_back(rock(1.5, 0.9, 0.38, 0.28, 0.4, 7));
    o.push_back(rock(1.1, -1.4, 0.4, 0.3, 0.34, 11));
    o.push_back(rock(-1.3, -1.1, 0.36, 0.3, 0.38, 13));
    o.push_back(rock(-14, 3, 1.5, 1.0, 1.2, 21));
    o.push_back(rock(13, -6, 1.1, 0.8, 1.0, 22));
    o.push_back(rock(10.5, 8, 0.7, 0.5, 0.6, 23));
    o.push_back(rock(-6, 8.5, 0.6, 0.45, 0.55, 24));
    o.push_back(rock(16, 0.5, 2.0, 1.4, 1.7, 25));
    for (int i = 0; i < 16; i++) {  // the river's banks: rocks along both edges
        const double cx = -30 + i * 4.0 + rnd() * 2.5, cz = -24.5 + 0.9 * std::sin((cx + 30) / 68.0 * 9.0) + (i % 2 ? 4.2 : -4.0) + rnd();
        o.push_back(rock(cx, cz, 0.5 + rnd() * 0.9, 0.35 + rnd() * 0.5, 0.45 + rnd() * 0.7, 40 + i));
    }
    for (int i = 0, n = 0; i < 60 && n < 14; i++) {
        const double cx = -20 + rnd() * 40, cz = -12 + rnd() * 20, r = 0.5 + rnd() * 0.5;
        if (blocked(cx, cz, r + 1.0)) continue;
        o.push_back(bush(cx, cz, r, 0)), n++;
    }
    return lv;
}

View camera(const Level& lv, const World& w, double t) {
    const menucamp::Camera c = menucamp::metaOf(lv).cam;
    // A slow drift, never an orbit: the eye swings about 11 degrees either way (two waves, periods about 121 s and 370 s) round the point it looks at,
    // moves in and out by about 8% (about 203 s) and bobs 25 cm (about 143 s); `drift` scales all three (0 holds still).
    const double k = c.drift, az = k * (0.16 * std::sin(t * 0.052) + 0.04 * std::sin(t * 0.017 + 1)), scale = 1 + k * 0.085 * std::sin(t * 0.031 + 0.7);
    const double ox = c.ex - c.tx, oz = c.ez - c.tz, ca = std::cos(az), sa = std::sin(az);
    View v;
    v.fovy = 0.78f;
    v.eye = V3(c.tx + (ox * ca - oz * sa) * scale, 0, c.tz + (ox * sa + oz * ca) * scale);
    v.eye.y = std::max(w.height(v.eye.x, v.eye.z), w.height(c.ex, c.ez)) + c.ey + 0.25 * k * std::sin(t * 0.044);
    v.target = V3(c.tx, w.height(c.tx, c.tz) + c.ty, c.tz);
    v.up = V3(0, 1, 0);
    return v;
}

Daylight daylight(const Level& lv) {
    // The sun's place from the level's time of day, clear of the far ridge; the sky's colours from a minute before it sets (Meta::look): orange, pink and gold, the clouds lit from below.
    Daylight d = computeDaylight(lv.timeOfDay >= 0 ? lv.timeOfDay : kHours);
    const Daylight s = computeDaylight(menucamp::metaOf(lv).look);
    d.skyZen = s.skyZen, d.skyHor = s.skyHor, d.duskCol = s.duskCol * 1.25, d.sunGlow = s.sunGlow * 1.6, d.ambSky = s.ambSky * 0.9, d.ambGround = s.ambGround;
    d.cloudLit = V3(s.cloudLit.x * 1.9, s.cloudLit.y * 1.2, s.cloudLit.z * 0.55), d.cloudDark = s.cloudDark;
    d.keyCol = V3(d.keyCol.x, d.keyCol.y * 0.82, d.keyCol.z * 0.62);
    return d;
}

std::string userPath() { return configDir() + "/menu_camp.czl"; }
std::string repoPath() { return "assets/menu/camp.czl"; }

Level load(std::string* from) {
    for (int pass = 0; pass < 2; pass++) {
        const std::string path = pass == 0 ? userPath() : repoPath();
        Level lv;
        if (loadLevelFile(path, lv) && menucamp::isCamp(lv)) {
            lv.file = path, lv.builtin = pass == 1;
            if (from) *from = pass == 0 ? "YOUR SAVED SCENE" : "THE GAME'S SCENE";
            return lv;
        }
    }
    if (from) *from = "THE GAME'S SCENE";
    return generateDefault();
}

bool dump(const std::string& path) {
    std::string err;
    return saveLevelFile(generateDefault(), path, &err, false);
}

bool resetUser() {
    const bool a = std::remove(userPath().c_str()) == 0;
    std::remove((userPath() + ".bak").c_str());  // (the loader falls back to the backup of a file it can't read: that would bring the old scene back)
    return a;
}

World buildWorld(const Level& lv) {
    World w = levelWorld(lv);
    applyLevel(lv, w);
    return w;
}

float flicker(double t) {
    // Two slow waves and a quick one, plus a hash for the jumps: 0.7 .. 1.3.
    const double s = 0.5 * std::sin(t * 7.3) + 0.3 * std::sin(t * 13.1 + 1.7) + 0.2 * std::sin(t * 23.9 + 0.4) + 0.12 * std::sin(t * 41.0);
    return (float)(1.0 + 0.3 * s);
}

}  // namespace menuscene
