#include "menuscene.h"

#include <algorithm>
#include <cmath>

#include "daylight.h"

namespace menuscene {

namespace {
// The camp is laid out around the fire in "camp" coordinates: x to the right of the camera, z away from it (north), in metres.
// The fire is at the origin of those. kRot turns the camp on the map (the wilds' ground slopes one way).
const double kFx = 80, kFz = 40;  // (the wilds' ground is level and bare here, a hundred metres from its spawn pad)

// The camp is turned on the map so the sunset lies behind it, a little right of the camera's line (kSunLocal radians, in camp terms).
const double kSunLocal = 0.15;
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

V3 fire() { return at(0, 0); }

World buildWorld() {
    const Level lv = level();
    World w = levelWorld(lv);
    // The clearing: whatever the forest put in the camp's patch (and the way the camera looks at it) comes off.
    auto in = [&](double x, double z, double pad) {
        double cx, cz;
        local(x, z, cx, cz);
        return (cx > -21 - pad && cx < 21 + pad && cz > -21 - pad && cz < 27 + pad) || inSunGap(cx, cz);
    };
    auto drop = [](auto& vec, auto gone) { vec.erase(std::remove_if(vec.begin(), vec.end(), gone), vec.end()); };
    drop(w.trees, [&](const Tree& t) { return in(t.base.x, t.base.z, t.radius); });
    drop(w.rocks, [&](const Rock& r) { return in(r.c.x, r.c.z, r.r); });
    drop(w.poles, [&](const Pole& p) { return in(p.x, p.z, p.r); });
    drop(w.boxes, [&](const Box& b) { return in(b.c.x, b.c.z, 0); });
    drop(w.logs, [&](const Log& l) { return in(l.a.x, l.a.z, l.r) || in(l.b.x, l.b.z, l.r); });
    drop(w.bushes, [&](const Bush& b) { return in(b.p.x, b.p.z, b.r); });
    drop(w.fixtures, [&](const Prop& p) { return in(p.a.x, p.a.z, 0); });
    drop(w.models, [&](const Placed& p) { return in(p.p.x, p.p.z, 0); });
    // A little stream across the foreground (camp terms: from (-9, 8.5) to (15, 10)): a flattened bed dug a hand deep with water in it.
    {
        const V3 a = at(-9, 8.5), b = at(15, 10);
        const double dx = b.x - a.x, dz = b.z - a.z, L = std::hypot(dx, dz);
        double g = 0;
        for (int k = 0; k <= 12; k++) g += w.height(a.x + dx * k / 12, a.z + dz * k / 12) / 13;
        for (double x = std::min(a.x, b.x) - 8; x <= std::max(a.x, b.x) + 8; x += 1)
            for (double z = std::min(a.z, b.z) - 8; z <= std::max(a.z, b.z) + 8; z += 1) {
                const double u = ((x - a.x) * dx + (z - a.z) * dz) / (L * L);
                if (u < -0.05 || u > 1.05) continue;
                const double wob = 0.9 * std::sin(u * 9.0), d = std::fabs(((x - a.x) * dz - (z - a.z) * dx) / L - wob);
                const int id = w.cellIndex(x, z);
                if (id < 0 || d > 6) continue;
                const double orig = w.height(x, z), k = std::min(1.0, std::max(0.0, (6 - d) / 3.5));
                double h = orig + (g - orig) * k * k * (3 - 2 * k);
                if (d < 1.6) h = g - 0.42 * (1 - d * d / 2.56);
                w.setCellHeight(id, (float)h);
                if (d < 1.45) w.setCellWater(id, (float)(g - 0.14));
            }
    }
    applyLevel(lv, w);
    return w;
}

const Spot* spots() {
    // (camp terms: the Bronco and the 4Runner right of the fire, the Blazer left of it with its nose to the flames, as if parked for the night)
    static const Spot s[3] = {{7.6, -0.8, 2.95}, {5.0, -8.6, 3.55}, {-4.6, -5.6, -0.72}};
    static Spot w[3];
    for (int k = 0; k < 3; k++) {
        const V3 p = at(s[k].x, s[k].z), f = fire();
        w[k] = {p.x - f.x, p.z - f.z, s[k].yaw - theta()};
    }
    return w;
}

Level level() {
    Level lv;
    lv.name = "MENU CAMP";
    lv.map = MAP_WILDS;
    lv.terrain = WILDS_FOREST;
    lv.timeOfDay = kHours;
    std::vector<LevelObject>& o = lv.objects;
    o.push_back(model("cabin", 3, -16.5, -80, 1, 0));
    o.push_back(model("woodpile", -4.5, -14.0, 90));
    o.push_back(model("campfire", 0, 0, 0));
    o.push_back(model("tent", 12.5, -9.5, -150));
    o.push_back(model("picnic_table", 3.0, 3.5, 20));
    o.push_back(logSeat(-3.2, -1.2, -2.2, 1.6));
    o.push_back(logSeat(2.4, -2.6, 3.4, 0.2));
    // The tree line: pines and broadleaf in a loose ring that opens on the camera's side.
    uint32_t h = 12345;
    auto rnd = [&]() {
        h = h * 1664525u + 1013904223u;
        return (h >> 8) / double(1 << 24);
    };
    for (int i = 0; i < 44; i++) {
        const double a = -0.35 * M_PI + (i / 43.0) * 1.7 * M_PI;  // sweeps round the back; the front (toward -z) stays open
        const double r = 17 + rnd() * 16;
        const double cx = std::sin(a) * r * 1.25, cz = -std::cos(a) * r * 0.8 - 4 + rnd() * 3;
        if (cz > 3 || inSunGap(cx, cz)) continue;
        o.push_back(tree(cx, cz, 9 + rnd() * 9, rnd() < 0.72 ? 0 : 1, 0.85f + 0.3f * (float)rnd()));
    }
    for (int i = 0; i < 9; i++) if (!inSunGap(-26 + i * 6.5, -30)) o.push_back(tree(-26 + i * 6.5 + rnd() * 3, -26 - rnd() * 8, 12 + rnd() * 8, 0, 0.8f + 0.2f * (float)rnd()));
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
    for (int i = 0; i < 14; i++) o.push_back(bush(-20 + rnd() * 40, -12 + rnd() * 20, 0.5 + rnd() * 0.5, 0));
    return lv;
}

View camera(const World& w, double t) {
    const V3 f = fire();
    // A slow drift, never an orbit: the eye swings about 11 degrees either way round the fire (two waves, periods about 121 s and 370 s), moves in and
    // out by a metre and a bit (period about 203 s) and bobs 25 cm (about 143 s). The view stays on the camp.
    const double az = 0.16 * std::sin(t * 0.052) + 0.04 * std::sin(t * 0.017 + 1);
    const double dist = 15.5 + 1.2 * std::sin(t * 0.031 + 0.7);
    const double gy = w.height(f.x, f.z);
    View v;
    v.fovy = 0.78f;
    v.eye = at(std::sin(az) * dist - 2.0, std::cos(az) * dist);
    v.eye.y = std::max(w.height(v.eye.x, v.eye.z) + 1.5, gy + 1.5) + 0.25 * std::sin(t * 0.044);
    // The camp sits to the right of the middle: the menu has the left of the screen.
    v.target = at(-1.2, -6.0);
    v.target.y = gy + 1.7;
    v.up = V3(0, 1, 0);
    return v;
}

float flicker(double t) {
    // Two slow waves and a quick one, plus a hash for the jumps: 0.7 .. 1.3.
    const double s = 0.5 * std::sin(t * 7.3) + 0.3 * std::sin(t * 13.1 + 1.7) + 0.2 * std::sin(t * 23.9 + 0.4) + 0.12 * std::sin(t * 41.0);
    return (float)(1.0 + 0.3 * s);
}

}  // namespace menuscene
